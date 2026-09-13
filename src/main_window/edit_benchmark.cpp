/*
 * Copyright (c) 2026 Sapporo_ningyo
 * Licensed under Apache License 2.0; see LICENSE and NOTICE.
 */
#include "kme.h"
#include "app_settings.h"
#include "debug_headless.h"
#include "canvas3D.h"
#include "maploader.h"
#include "text_decoder.h"
#include "implot.h"
#include <windows.h>
#include <d3d11.h>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <stdexcept>

#ifndef NDEBUG
namespace {
namespace fs = std::filesystem;

fs::path benchmark_path(const std::string& path) { return fs::path(utf8_to_wide(path)); }
std::string benchmark_bytes(const fs::path& path) {
    return kme::maploader::read_binary_file(path);
}
std::uint64_t benchmark_hash(const std::string& bytes) {
    std::uint64_t value = 14695981039346656037ull;
    for (unsigned char byte : bytes) { value ^= byte; value *= 1099511628211ull; }
    return value;
}
bool benchmark_contains(const fs::path& root, const fs::path& path) {
    const auto relative = path.lexically_normal().lexically_relative(root.lexically_normal());
    return !relative.empty() && !relative.is_absolute() && *relative.begin() != "..";
}
void require_benchmark(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}
struct BenchmarkHandleGuard {
    void*& handle;
    ~BenchmarkHandleGuard() { if (handle) kv_free(handle); }
};

// Independent regular-file copies preserve every relative reference, including
// model textures. Never create links to the protected originals.
class BenchmarkCopy {
public:
    explicit BenchmarkCopy(const fs::path& source) {
        require_benchmark(source != source.root_path(), "refusing to copy a drive root");
        root_ = fs::temp_directory_path() / ("komapedit-edit-bench-" +
            std::to_string(GetCurrentProcessId()) + "-" +
            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        require_benchmark(fs::create_directory(root_), "temporary benchmark directory collision");
        try {
            for (const auto& entry : fs::recursive_directory_iterator(source)) {
                const DWORD attributes = GetFileAttributesW(entry.path().c_str());
                require_benchmark(attributes != INVALID_FILE_ATTRIBUTES &&
                    (attributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0,
                    "benchmark copy refuses reparse points: " + wide_to_utf8(entry.path().wstring()));
                const fs::path target = root_ / entry.path().lexically_relative(source);
                if (entry.is_directory()) fs::create_directories(target);
                else if (entry.is_regular_file()) fs::copy_file(entry.path(), target);
                else throw std::runtime_error("unsupported benchmark copy entry");
            }
        } catch (...) { cleanup(); throw; }
    }
    ~BenchmarkCopy() { cleanup(); }
    const fs::path& root() const { return root_; }
private:
    void cleanup() noexcept {
        std::error_code error;
        fs::path parent = fs::temp_directory_path(error);
        if (!parent.has_filename()) parent = parent.parent_path();
        if (!error && !root_.empty() && root_.parent_path() == parent &&
            root_.filename().wstring().find(L"komapedit-edit-bench-") == 0) {
            fs::remove_all(root_, error);
        }
    }
    fs::path root_;
};
}

int App::run_debug_headless_edit_benchmark(const HeadlessEditBenchmarkOptions& options) {
    std::ofstream file;
    std::ostream* out = &std::cout;
    if (!options.output_path.empty()) {
        file.open(benchmark_path(options.output_path), std::ios::binary | std::ios::trunc);
        if (!file) return 1;
        out = &file;
    }
    *out << "command=debug-headless-edit-bench build=Debug scene=" << (options.scene ? "on" : "off")
         << " repeat=" << options.repeat << " unit_distance=" << options.unit_distance
         << " window_back_m=100 window_forward_m=1200 canvas=1260x680\n"
         << "input=" << options.path << "\n";
    out->flush();

    const HRESULT apartment = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* context = nullptr;
    int result_code = 0;
    std::map<std::string, std::string> protected_sources;
    std::map<std::string, std::vector<double>> measurements;
    fs::path copy_root_used;
    try {
        require_benchmark(SUCCEEDED(apartment) || apartment == RPC_E_CHANGED_MODE, "COM initialization failed");
        if (options.scene) {
            D3D_FEATURE_LEVEL level{};
            require_benchmark(SUCCEEDED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
                D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0, D3D11_SDK_VERSION, &device, &level, &context)),
                "hardware Direct3D device creation failed");
        }
        ImGui::CreateContext();
        ImPlot::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.DisplaySize = ImVec2(1280, 720);
        io.DeltaTime = 1.0f / 60.0f;
        io.Fonts->AddFontDefault();
        io.Fonts->Build();

        LoadResult initial = load_map_worker(options.path, options.unit_distance,
            false, 0, 0, options.unit_distance, LoadModelOptions{true});
        BenchmarkHandleGuard initial_guard{initial.handle};
        require_benchmark(initial.ok, "input load failed: " + initial.error);
        fs::path source_root = benchmark_path(options.path).parent_path().lexically_normal();
        auto include_path = [&](const std::string& text) {
            if (text.empty()) return;
            const fs::path path = benchmark_path(text).lexically_normal();
            while (!benchmark_contains(source_root, path) && source_root != source_root.root_path()) {
                source_root = source_root.parent_path();
            }
            require_benchmark(source_root != source_root.root_path(), "route dependencies have no bounded common directory");
        };
        for (const auto& source : initial.model.edit_files) {
            protected_sources.emplace(source.file_path, benchmark_bytes(benchmark_path(source.file_path)));
            include_path(source.file_path);
        }
        for (const auto& row : initial.model.structure_models) include_path(table_cell(row, "resolvedFilePath"));
        for (const auto& row : initial.model.sound_list) include_path(table_cell(row, "resolvedFilePath"));
        protected_sources.emplace(options.path, benchmark_bytes(benchmark_path(options.path)));
        kv_free(initial.handle);
        initial.handle = nullptr;
        *out << "copy_source_root=" << wide_to_utf8(source_root.wstring()) << "\n";
        out->flush();

        auto run = [&](const std::string& map_path, const fs::path* writable_root, int iteration) {
            LoadResult loaded = load_map_worker(map_path, options.unit_distance,
                false, 0, 0, options.unit_distance, LoadModelOptions{true});
            BenchmarkHandleGuard loaded_guard{loaded.handle};
            require_benchmark(loaded.ok, "benchmark load failed: " + loaded.error);
            UserSettings settings;
            settings.language = Language::En;
            App app(device, settings, 1.0f, false, false);
            app.handle_ = loaded.handle;
            loaded.handle = nullptr;
            app.model_ = std::move(loaded.model);
            app.file_path_ = map_path;
            app.has_model_ = true;
            app.edit_mode_enabled_ = true;
            app.edit_registry_loaded_ = true;
            app.edit_memory_matches_pending_ledger_ = true;
            app.unit_distance_ = options.unit_distance;
            app.cp_interval_ = options.unit_distance;
            app.dmin_ = app.model_.default_min;
            app.dmax_ = app.model_.default_max;
            app.rebuild_marker_overlay_cache();
            app.reset_marker_visibility();
            std::map<std::string, std::string> disk_before;
            for (const auto& source : app.model_.edit_files) {
                if (writable_root) require_benchmark(benchmark_contains(*writable_root, benchmark_path(source.file_path)),
                    "copy still refers to an original source; Save prohibited: " + source.file_path);
                disk_before.emplace(source.file_path, benchmark_bytes(benchmark_path(source.file_path)));
            }
            auto render = [&]() {
                ImGui::NewFrame();
                ImGui::SetNextWindowPos(ImVec2(0, 0));
                ImGui::SetNextWindowSize(io.DisplaySize);
                ImGui::Begin("EditBenchmark", nullptr, ImGuiWindowFlags_NoSavedSettings);
                app.scene_preview_canvas_->render_scene_preview(ImVec2(1260, 680));
                ImGui::End();
                ImGui::EndFrame();
            };
            auto settle = [&]() {
                if (!options.scene) return;
                const auto started = std::chrono::steady_clock::now();
                do {
                    render();
                    if (!app.scene_preview_canvas_->scene_stats().loading) break;
                    require_benchmark(std::chrono::steady_clock::now() - started < std::chrono::seconds(120),
                        "model loading timed out");
                    std::this_thread::sleep_for(std::chrono::milliseconds(5));
                } while (true);
                for (int frame = 0; frame < 5; ++frame) render();
            };
            if (options.scene) {
                app.scene_preview_started_ = true;
                app.show_scene_preview_window_ = true;
                app.scene_preview_canvas_->set_scene_window(100, 1200);
                ImGui::NewFrame();
                app.rebuild_scene_preview(false, false);
                ImGui::EndFrame();
                if (app.scene_preview_dirty_) {
                    std::lock_guard<std::mutex> lock(app.log_mutex_);
                    for (const auto& line : app.logs_) *out << line.text << "\n";
                }
                require_benchmark(!app.scene_preview_dirty_, "initial scene build failed");
                app.scene_preview_canvas_->jump_scene_camera_to_distance(app.dmin_ + 500.0);
                settle();
            }
            *out << "workload iteration=" << iteration << " writable=" << (writable_root ? 1 : 0)
                 << " sources=" << disk_before.size() << " statements=" << app.model_.edit_statements.size()
                 << " structures=" << app.model_.structures.size() << " repeaters=" << app.model_.repeaters.size()
                 << " tracks=" << app.model_.other_tracks.size();
            if (options.scene) {
                const auto stats = app.scene_preview_canvas_->scene_stats();
                *out << " models_ready=" << stats.model_ready_count << " models_failed=" << stats.model_failed_count
                     << " instances=" << stats.instance_count << " camera=" << stats.camera_distance;
            }
            *out << "\n";

            auto target = [&](const std::string& kind, const char* field, const char* method) {
                const auto* rows = inspector_rows_for_kind(app.model_, kind);
                require_benchmark(rows != nullptr, "missing row family: " + kind);
                for (const auto& row : *rows) {
                    if (row.edit_id.empty() || (!std::string(method).empty() &&
                        ascii_lower(table_cell(row, "method")) != method)) continue;
                    app.open_element_inspector({row.edit_id, kind});
                    if ((kind == "repeater" || kind == "structure.put") &&
                        app.inspector_.source_zero_offset_method) continue;
                    const auto* value = find_inspector_field(app.inspector_, field);
                    if (!value || value->read_only || value->disabled) continue;
                    bool form_valid = true;
                    for (auto existing : app.inspector_.fields) {
                        if (!existing.read_only &&
                            ((existing.required && trim_gui_ascii_copy(edit_field_buffer_text(existing)).empty()) ||
                             !validate_and_canonicalize_edit_field(existing, false))) {
                            *out << "target_skipped kind=" << kind << " field=" << existing.key << " reason=invalid_baseline_form\n";
                            form_valid = false;
                            break;
                        }
                    }
                    if (form_valid) return row.edit_id;
                }
                throw std::runtime_error("no writable benchmark target: " + kind + "." + field);
            };
            const std::string structure = target("structure.put", "x", "");
            const std::string repeater = target("repeater", "x", "");
            const std::string gauge = target("curve", "radius", "curve.setgauge");
            *out << "targets structure=" << structure << " repeater=" << repeater << " gauge=" << gauge << "\n";
            const auto camera = app.scene_preview_canvas_->scene_camera_pose();

            auto complete = [&](const char* name) {
                app.process_pending_element_inspector();
                if (options.scene && app.scene_preview_dirty_) {
                    app.rebuild_scene_preview(app.scene_preview_preserve_models_on_rebuild_,
                                              app.scene_preview_preserve_camera_on_rebuild_);
                    require_benchmark(!app.scene_preview_dirty_, "edit scene rebuild failed");
                }
                if (options.scene) {
                    GuiTiming::Activation activation(app.edit_timing_.get());
                    GuiTiming::Stage stage("scene.first_frame");
                    render();
                    app.edit_timing_wait_scene_frame_ = false;
                }
                app.finish_edit_timing();
                {
                    std::lock_guard<std::mutex> lock(app.log_mutex_);
                    for (const auto& line : app.logs_) {
                        if (line.text.find("edit timing:") != std::string::npos || line.severity == LogSeverity::Error)
                            *out << line.text << "\n";
                    }
                    app.logs_.clear();
                }
                out->flush();
                require_benchmark(!app.edit_timing_, "operation timing did not finish");
                require_benchmark(app.edit_timing_outcome_ == "success", std::string("edit failed: ") + name +
                    " outcome=" + app.edit_timing_outcome_ + " status=" + app.program_status_key_);
                require_benchmark(!app.program_status_elapsed_suffix_.empty(), "status elapsed time missing");
                for (const char* stage : {"table.invalidate", "plan.markers", "snapshot.hydrate",
                                          "scene.map_content", "scene.dynamic_content", "scene.rebuild"}) {
                    require_benchmark(app.last_edit_timing_.count(stage) <= 1,
                        std::string("repeated transaction work: ") + stage);
                }
                if (app.last_edit_timing_.count("scene.rebuild")) {
                    require_benchmark(app.last_edit_timing_.count("scene.map_content") == 0 &&
                        app.last_edit_timing_.count("scene.dynamic_content") == 0,
                        "full scene rebuild repeated a local refresh");
                }
                if (options.scene) {
                    const auto after = app.scene_preview_canvas_->scene_camera_pose();
                    require_benchmark(camera.valid && after.valid &&
                        std::abs(camera.distance - after.distance) < 1e-9 &&
                        std::abs(camera.x - after.x) < 1e-9 && std::abs(camera.y - after.y) < 1e-9 &&
                        std::abs(camera.z - after.z) < 1e-9 && std::abs(camera.theta - after.theta) < 1e-9 &&
                        std::abs(camera.pitch - after.pitch) < 1e-9, "edit changed the camera pose");
                    const auto stats = app.scene_preview_canvas_->scene_stats();
                    require_benchmark(!stats.loading && stats.model_ready_count > 0 &&
                        stats.model_ready_count + stats.model_failed_count == stats.model_path_count,
                        "edit reloaded models instead of reusing the completed initial load");
                }
                *out << "sample iteration=" << iteration << " case=" << name
                     << " writable=" << (writable_root ? 1 : 0)
                     << " total_ms=" << std::fixed << std::setprecision(3) << app.last_edit_seconds_ * 1000.0 << "\n";
                if (writable_root) measurements[name].push_back(app.last_edit_seconds_ * 1000.0);
                out->flush();
                const auto settle_started = std::chrono::steady_clock::now();
                settle();
                if (options.scene) *out << "async_settle case=" << name << " excluded_from_total_ms="
                    << std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - settle_started).count() << "\n";
            };
            auto apply = [&](const std::string& id, const std::string& kind, const char* field, double delta, const char* name) {
                app.open_element_inspector({id, kind});
                auto* value = find_inspector_field(app.inspector_, field);
                require_benchmark(value && !value->read_only, "Inspector field unavailable");
                const double before = std::stod(edit_field_buffer_text(*value));
                set_edit_field_buffer(*value, format_double(before + delta, 6));
                app.apply_inspector_changes();
                complete(name);
                const auto* rows = inspector_rows_for_kind(app.model_, kind);
                const auto found = std::find_if(rows->begin(), rows->end(), [&](const TableRow& row) { return row.edit_id == id; });
                require_benchmark(found != rows->end(), "stable edit ID lost after Apply");
                app.open_element_inspector({id, kind});
                const auto* actual = find_inspector_field(app.inspector_, field);
                require_benchmark(actual && std::abs(std::stod(edit_field_buffer_text(*actual)) - before - delta) < 1e-6,
                    "edited value differs from requested value");
                if (options.scene && (kind == "structure.put" || kind == "repeater")) {
                    require_benchmark(app.scene_preview_canvas_->debug_check_scene_edit_target(
                        scene_edit_target_from_row(*found, kind == "repeater"
                            ? Canvas3DSceneEditKind::Repeater : Canvas3DSceneEditKind::Structure)),
                        "scene placement or cached transform differs from the edited row");
                }
            };
            apply(structure, "structure.put", "x", 0.125, "first_apply");
            apply(structure, "structure.put", "x", 0.125, "repeat_apply");
            apply(repeater, "repeater", "x", 0.125, "repeater_apply");
            apply(gauge, "curve", "radius", 0.001, "geometry_mixed_apply");
            app.request_element_delete(structure, "structure.put");
            app.process_pending_element_delete();
            complete("pending_delete");
            require_benchmark(app.row_is_pending_delete(structure), "deletion missing from ledger");
            for (const auto& source : disk_before) require_benchmark(
                benchmark_bytes(benchmark_path(source.first)) == source.second, "memory edit wrote a source file");
            *out << "memory_sources_unchanged=1\n";
            if (writable_root) {
                for (const auto& source : app.model_.edit_files) require_benchmark(
                    benchmark_contains(*writable_root, benchmark_path(source.file_path)), "Save target escaped temporary root");
                require_benchmark(app.save_pending_document_changes(), "Save failed");
                complete("save");
                require_benchmark(app.pending_edit_changes_.empty(), "Save retained ledger");
                apply(repeater, "repeater", "x", 0.125, "post_save_apply");
            }
            require_benchmark(app.discard_pending_edits(), "Revert failed");
            app.process_pending_element_inspector();
            if (options.scene && app.scene_preview_dirty_) app.rebuild_scene_preview(true, true);
            if (options.scene) {
                GuiTiming::Activation activation(app.edit_timing_.get());
                GuiTiming::Stage stage("scene.first_frame");
                render();
                app.edit_timing_wait_scene_frame_ = false;
            }
            app.finish_edit_timing();
            require_benchmark(app.pending_edit_changes_.empty(), "Revert retained ledger");
            LoadResult reloaded = load_map_worker(map_path, options.unit_distance, false, 0, 0,
                options.unit_distance, LoadModelOptions{true});
            BenchmarkHandleGuard reloaded_guard{reloaded.handle};
            require_benchmark(reloaded.ok, "fresh reload failed");
            require_benchmark(reloaded.model.structures.size() == app.model_.structures.size(), "reload row count mismatch");
            for (const char* kind : {"structure.put", "repeater", "curve", "gradient", "otherTrack.change"}) {
                const auto* expected = inspector_rows_for_kind(reloaded.model, kind);
                const auto* actual = inspector_rows_for_kind(app.model_, kind);
                require_benchmark(expected && actual && expected->size() == actual->size(), "reload family mismatch");
                for (size_t row = 0; row < expected->size(); ++row) {
                    for (const auto& field : (*expected)[row].cells) {
                        // Source positions/global ordinals and edit linkage are
                        // remapped by Save; vector order and semantic cells must match.
                        if (!field.first.empty() && field.first.front() == '_') continue;
                        if (field.first == "filePath" || field.first == "line" || field.first == "column" ||
                            field.first == "order" || field.first == "rowNumber") continue;
                        require_benchmark(table_cell((*actual)[row], field.first.c_str()) == field.second,
                            std::string("reload preview value mismatch: ") + kind + "." + field.first);
                    }
                }
            }
            *out << "reload=PASS\n";
        };

        run(options.path, nullptr, 0);
        BenchmarkCopy copy(source_root);
        copy_root_used = copy.root();
        const fs::path copied_map = copy.root() / benchmark_path(options.path).lexically_normal().lexically_relative(source_root);
        const std::string copied_path = wide_to_utf8(copied_map.wstring());
        *out << "copy_root=" << wide_to_utf8(copy.root().wstring()) << "\n";
        for (int iteration = 1; iteration <= options.repeat; ++iteration) {
            // Restore only independent source copies outside the timed region.
            for (const auto& source : protected_sources) {
                const fs::path destination = copy.root() / benchmark_path(source.first).lexically_normal().lexically_relative(source_root);
                require_benchmark(benchmark_contains(copy.root(), destination), "fixture restore escaped temporary root");
                std::ofstream restore(destination, std::ios::binary | std::ios::trunc);
                restore.write(source.second.data(), static_cast<std::streamsize>(source.second.size()));
                restore.close();
                require_benchmark(static_cast<bool>(restore), "fixture source restore failed");
            }
            run(copied_path, &copy.root(), iteration);
        }

        // A mixed insert/alignment batch used to hydrate the full snapshot twice.
        // Keep this deterministic regression outside the measured real-route runs.
        const fs::path fixture_path = copy.root() / "komapedit-refresh-contract.txt";
        require_benchmark(!fs::exists(fixture_path), "refresh fixture already exists");
        const std::string fixture_text = "BveTs Map 2.02:utf-8\r\n0;\r\nCurve.SetGauge(1.435);\r\n"
            "Curve.Begin(300);\r\nGradient.Begin(0);\r\n100;\r\nCurve.End();\r\n";
        {
            std::ofstream fixture(fixture_path, std::ios::binary);
            fixture << fixture_text;
            require_benchmark(static_cast<bool>(fixture), "refresh fixture creation failed");
        }
        const std::string fixture_name = wide_to_utf8(fixture_path.wstring());
        LoadResult fixture_load = load_map_worker(fixture_name, options.unit_distance,
            false, 0, 0, options.unit_distance, LoadModelOptions{true});
        BenchmarkHandleGuard fixture_guard{fixture_load.handle};
        require_benchmark(fixture_load.ok, "refresh fixture load failed");
        UserSettings fixture_settings;
        App fixture_app(nullptr, fixture_settings, 1.0f, false, false);
        fixture_app.handle_ = fixture_load.handle;
        fixture_load.handle = nullptr;
        fixture_app.model_ = std::move(fixture_load.model);
        fixture_app.file_path_ = fixture_name;
        fixture_app.has_model_ = fixture_app.edit_mode_enabled_ = fixture_app.edit_registry_loaded_ = true;
        fixture_app.edit_memory_matches_pending_ledger_ = true;
        const auto fixture_check = [&](bool condition, const char* message) {
            if (!condition) {
                std::lock_guard<std::mutex> lock(fixture_app.log_mutex_);
                for (const auto& line : fixture_app.logs_) *out << line.text << "\n";
            }
            require_benchmark(condition, message);
        };
        const auto curve = std::find_if(fixture_app.model_.curve_rows.begin(), fixture_app.model_.curve_rows.end(),
            [](const auto& row) { return ascii_lower(table_cell(row, "method")) == "curve.begin"; });
        require_benchmark(curve != fixture_app.model_.curve_rows.end(), "refresh fixture curve missing");
        const size_t gradient_count = fixture_app.model_.gradient_rows.size();
        MapElementPendingChange update;
        update.change_id = update.edit_id = curve->edit_id;
        update.row_kind = "curve";
        update.operation = "update";
        update.field_changes = {{"radius", "400"}};
        MapElementPendingChange insert;
        insert.change_id = insert.edit_id = "edit-bench-insert-gradient";
        insert.row_kind = "gradient";
        insert.operation = "insert";
        insert.target_file_path = fixture_name;
        insert.field_changes = {{"method", "Gradient.Begin"}, {"distance", "150"}, {"gradient", "5"}};
        std::map<std::string, MapElementPendingChange> mixed{{update.edit_id, update}, {insert.edit_id, insert}};
        fixture_check(fixture_app.apply_edit_ledger_to_preview(mixed, std::nullopt, false), "mixed fixture Apply failed");
        const auto& trace = fixture_app.last_edit_timing_;
        fixture_check(trace.count("snapshot.hydrate") == 1 && trace.count("snapshot.full_refresh") == 1 &&
            trace.count("snapshot.partial_refresh") == 0 && trace.count("table.invalidate") == 1 &&
            trace.count("plan.markers") == 1, "mixed insert repeated full/partial hydration or refresh");
        fixture_check(fixture_app.model_.gradient_rows.size() == gradient_count + 1,
            "mixed insert missing from preview");
        auto invalid = mixed;
        invalid.at(update.edit_id).field_changes["radius"] = "invalid";
        fixture_check(!fixture_app.apply_edit_ledger_to_preview(invalid, std::nullopt, false) &&
            fixture_app.edit_timing_outcome_ == "failed" && !fixture_app.edit_timing_ &&
            fixture_app.edit_memory_matches_pending_ledger_, "failed Apply did not restore the validated ledger");
        fixture_check(fixture_app.pending_edit_changes_.at(update.edit_id).field_changes.at("radius") == "400" &&
            fixture_app.model_.gradient_rows.size() == gradient_count + 1,
            "failed Apply changed the previous preview");
        fixture_check(fixture_app.discard_pending_edits() && fixture_app.pending_edit_changes_.empty() &&
            fixture_app.model_.gradient_rows.size() == gradient_count && benchmark_bytes(fixture_path) == fixture_text,
            "mixed insert Revert did not restore the baseline");
        fixture_check(!fixture_app.save_pending_document_changes() &&
            fixture_app.edit_timing_outcome_ == "no_changes" && !fixture_app.edit_timing_,
            "empty Save did not finish with no_changes");
        *out << "refresh_contract=PASS full_partial_exclusive=1 batched_refresh=1 failure_restore=1 revert=1 no_changes=1\n";
    } catch (const std::exception& error) {
        *out << "error=" << error.what() << "\n";
        result_code = 2;
    }
    try {
        for (const auto& source : protected_sources) {
            const auto bytes = benchmark_bytes(benchmark_path(source.first));
            *out << "protected_source=" << source.first << " before_hash=" << benchmark_hash(source.second)
                 << " after_hash=" << benchmark_hash(bytes) << "\n";
            if (bytes != source.second) result_code = 3;
        }
    } catch (const std::exception& error) { *out << "source_check_error=" << error.what() << "\n"; result_code = 3; }
    if (!copy_root_used.empty()) {
        const bool cleaned = !fs::exists(copy_root_used);
        *out << "fixture_files_cleaned=" << (cleaned ? 1 : 0) << "\n";
        if (!cleaned) result_code = 3;
    }
    for (auto& entry : measurements) {
        auto& values = entry.second;
        std::sort(values.begin(), values.end());
        const size_t p95 = static_cast<size_t>(std::ceil(static_cast<double>(values.size()) * 0.95)) - 1;
        *out << "summary case=" << entry.first << " samples=" << values.size()
             << " median_ms=" << values[values.size() / 2] << " p95_ms=" << values[p95]
             << " max_ms=" << values.back() << "\n";
    }
    if (ImPlot::GetCurrentContext()) ImPlot::DestroyContext();
    if (ImGui::GetCurrentContext()) ImGui::DestroyContext();
    release_com(context);
    release_com(device);
    if (SUCCEEDED(apartment)) CoUninitialize();
    *out << "result=" << (result_code == 0 ? "PASS" : "FAIL") << "\n";
    return result_code;
}
#endif
