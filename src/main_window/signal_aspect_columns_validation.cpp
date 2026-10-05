/*
 * Copyright (c) 2026 Sapporo_ningyo
 *
 * Licensed under Apache License 2.0; see LICENSE and NOTICE.
 */

#include "kme.h"
#include "app_settings.h"
#include "debug_headless.h"
#include "maploader.h"
#include "imgui.h"
#include "implot.h"

#include <windows.h>
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <stdexcept>
#include <thread>

#ifndef NDEBUG
namespace {
namespace fs = std::filesystem;

std::string signal_columns_bytes(const fs::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) throw std::runtime_error("cannot read source: " + path.u8string());
    std::string bytes(std::istreambuf_iterator<char>(stream), {});
    if (stream.bad()) throw std::runtime_error("cannot finish source read: " + path.u8string());
    return bytes;
}

std::uint64_t signal_columns_hash(const std::string& bytes) {
    std::uint64_t hash = 14695981039346656037ull;
    for (unsigned char ch : bytes) { hash ^= ch; hash *= 1099511628211ull; }
    return hash;
}

struct SignalColumnShape {
    size_t main = 0;
    size_t glare = 0;
    std::vector<std::string> values;
    bool operator==(const SignalColumnShape& other) const {
        return main == other.main && glare == other.glare && values == other.values;
    }
};
using SignalColumnShapes = std::vector<SignalColumnShape>;

SignalColumnShapes signal_column_shapes(const EditableListEditState& edit) {
    SignalColumnShapes result;
    for (size_t index : edit.visible_rows) {
        const auto& row = edit.rows.at(index);
        if (!row.deleted) result.push_back({row.primary_structure_field_count,
            row.secondary_structure_field_count, row.values});
    }
    return result;
}

// A bounded list of exclusively created regular files, never links to routes.
struct SignalColumnFixtures {
    fs::path directory;
    std::vector<fs::path> files;
    SignalColumnFixtures() {
        directory = fs::temp_directory_path() / ("komapedit-signal-columns-" +
            std::to_string(GetCurrentProcessId()) + "-" +
            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        if (!fs::create_directory(directory)) throw std::runtime_error("fixture directory collision");
    }
    void create(const fs::path& path, const std::string& bytes) {
        if (path.parent_path() != directory) throw std::runtime_error("fixture escaped its directory");
        std::string error;
        if (!create_utf8_bve_file_exclusive(path, bytes, error))
            throw std::runtime_error("fixture creation failed: " + error);
        files.push_back(path);
    }
    bool cleanup() {
        bool ok = true;
        for (const auto& file : files) {
            std::error_code error;
            fs::remove(file, error);
            ok = !error && ok;
        }
        std::error_code error;
        fs::remove(directory, error);
        return !error && ok;
    }
    ~SignalColumnFixtures() { cleanup(); }
};
}

int App::run_debug_headless_signal_aspect_columns(const HeadlessSignalAspectColumnsOptions& options) {
    std::ofstream output;
    std::ostream* out = &std::cout;
    if (!options.output_path.empty()) {
        output.open(fs::u8path(options.output_path), std::ios::binary | std::ios::trunc);
        if (!output) return 1;
        out = &output;
    }
    *out << "command=debug-headless-signal-aspect-columns\ninput=" << options.path
         << "\ninput_memory_only=1\n";
    out->flush();
    int failures = 0;
    const auto check = [&](const std::string& name, bool ok) {
        *out << name << '=' << (ok ? 1 : 0) << '\n';
        out->flush();
        if (!ok) ++failures;
        return ok;
    };
    const auto require = [&](const std::string& name, bool ok) {
        if (!check(name, ok)) throw std::runtime_error(name);
    };
    std::map<std::string, std::string> protected_sources;
    ImGui::CreateContext();
    ImPlot::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = ImVec2(1280, 720);
    io.DeltaTime = 1.0f / 60.0f;
    io.Fonts->AddFontDefault();
    io.Fonts->Build();
    ImGui::NewFrame();
    try {
        UserSettings settings;
        settings.language = Language::En;
        const auto open = [&](App& app, const std::string& path) {
            app.edit_mode_enabled_ = true;
            app.unit_distance_ = options.unit_distance;
            app.scene_auto_load_on_map_open_ = false;
            app.open_document(path, false);
            if (!app.scenario_route_pick_.items.empty()) {
                app.scenario_route_pick_.selected = 0;
                app.confirm_scenario_route_selection();
            }
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(300);
            for (;;) {
                if (std::chrono::steady_clock::now() >= deadline)
                    require("open_before_deadline", false);
                if (app.load_state_.running) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                    continue;
                }
                app.stop_loader();
                bool pending = false;
                {
                    std::lock_guard<std::mutex> lock(app.load_state_.result_mutex);
                    if (app.load_state_.pending_result) {
                        pending = true;
                        require("loader_result_ok", app.load_state_.pending_result->ok);
                    }
                }
                if (!pending) break;
                app.poll_loader();
            }
            require("normal_open_edit_metadata", app.has_model_ && app.handle_ && app.edit_registry_loaded_);
            require("signal_drafts_initialized", app.initialize_editable_list_draft_rows(
                app.signal_aspect_edit_, k_signal_aspect_edit_spec));
        };
        const auto shapes = [&](App& app) {
            require("drafts_available", app.initialize_editable_list_draft_rows(
                app.signal_aspect_edit_, k_signal_aspect_edit_spec));
            return signal_column_shapes(app.signal_aspect_edit_);
        };
        const auto row_at = [&](App& app, int visible) -> EditableListDraftRow& {
            auto& edit = app.signal_aspect_edit_;
            return edit.rows.at(edit.visible_rows.at(static_cast<size_t>(visible)));
        };
        const auto row_named = [&](App& app, const std::string& key) {
            shapes(app);
            for (size_t i = 0; i < app.signal_aspect_edit_.visible_rows.size(); ++i)
                if (row_at(app, static_cast<int>(i)).values.at(0) == key) return static_cast<int>(i);
            throw std::runtime_error("missing signal fixture row " + key);
        };
        const auto act = [&](App& app, SignalAspectColumnAction action, int row, bool secondary,
                             const std::string& label) {
            require(label + "_request", app.request_signal_aspect_column_action(action, row, secondary));
            if (app.signal_aspect_column_confirmation_)
                require(label + "_confirm", app.resolve_signal_aspect_column_confirmation(true));
        };
        const auto apply = [&](App& app, const std::string& label) {
            const auto expected = shapes(app);
            app.apply_editable_list_drafts(app.signal_aspect_edit_, k_signal_aspect_edit_spec);
            if (app.has_editable_list_drafts(app.signal_aspect_edit_))
                for (const auto& line : app.logs_) *out << "app_log=" << line.text << '\n';
            require(label + "_drafts_consumed", !app.has_editable_list_drafts(
                app.signal_aspect_edit_));
            require(label + "_shape_and_values_reparsed", shapes(app) == expected);
        };
        const auto render = [&](App& app) {
            // Dear ImGui tables may change column count only between frames.
            ImGui::EndFrame();
            ImGui::NewFrame();
            app.show_signal_aspects_window_ = true;
            app.render_signal_aspects_window();
            require("render_table_column_cap", app.table_cache_.signal_aspect_column_headers.size() <= 511);
        };
        protected_sources.emplace(options.path, signal_columns_bytes(fs::u8path(options.path)));
        {
            App app(nullptr, settings, 1.0f, false, false);
            *out << "stage=real_document_open\n";
            out->flush();
            open(app, options.path);
            *out << "resolved_map=" << app.file_path_ << "\nscenario_preview="
                 << (app.scenario_preview_ ? 1 : 0) << '\n';
            for (const auto& source : app.model_.edit_files)
                protected_sources.emplace(source.file_path, signal_columns_bytes(fs::u8path(source.file_path)));
            require("real_sources_captured", protected_sources.size() > 1);
            const auto baseline = shapes(app);
            require("real_signal_rows_present", !baseline.empty());
            *out << "real_logical_rows=" << baseline.size() << '\n';
            for (size_t i = 0; i < baseline.size(); ++i)
                *out << "real_row=" << i << " main=" << baseline[i].main << " glare=" << baseline[i].glare << '\n';
            render(app);
            act(app, SignalAspectColumnAction::Append, 0, false, "real_row_append");
            require("real_append_only_main", row_at(app, 0).primary_structure_field_count == baseline[0].main + 1 &&
                row_at(app, 0).secondary_structure_field_count == baseline[0].glare);
            act(app, SignalAspectColumnAction::RemoveLast, 0, false, "real_row_remove_empty");
            require("real_inverse_no_dirty", shapes(app) == baseline && !app.has_editable_list_drafts(
                app.signal_aspect_edit_));
            act(app, SignalAspectColumnAction::Append, 0, false, "real_pretrim_append");
            act(app, SignalAspectColumnAction::TrimTrailing, 0, false, "real_row_trim");
            act(app, SignalAspectColumnAction::AlignAll, -1, false, "real_align");
            const auto aligned = shapes(app);
            const size_t width = aligned.front().main;
            require("real_align_all_physical_rows", std::all_of(aligned.begin(), aligned.end(), [&](const auto& row) {
                return row.main == width && (row.glare == 0 || row.glare == width);
            }));
            act(app, SignalAspectColumnAction::Append, -1, false, "real_append_all");
            act(app, SignalAspectColumnAction::RemoveLast, -1, false, "real_remove_all");
            require("real_global_inverse", shapes(app) == aligned);
            act(app, SignalAspectColumnAction::TrimTrailing, -1, false, "real_trim_all");
            apply(app, "real_apply");
            act(app, SignalAspectColumnAction::Append, 0, false, "real_repeated_append");
            apply(app, "real_repeated_apply");
            require("real_revert", app.revert_all_pending_edits());
            require("real_revert_shapes", shapes(app) == baseline && app.pending_edit_changes_.empty());
        }

        SignalColumnFixtures fixtures;
        for (int variant = 0; variant < 3; ++variant) {
            const std::string name = variant == 0 ? "utf8-bom" : variant == 1 ? "shift-jis" : "utf8-lf";
            const std::string newline = variant == 2 ? "\n" : "\r\n";
            const std::string encoding = variant == 1 ? "shift_jis" : "utf-8";
            const std::string bom = variant == 0 ? std::string("\xEF\xBB\xBF") : std::string{};
            const std::string comment = variant == 1 ? std::string("# \x93\xFA\x96\x7B") : std::string("# \xE6\x97\xA5\xE6\x9C\xAC");
            const fs::path map = fixtures.directory / (name + "-map.txt");
            const fs::path signals = fixtures.directory / (name + "-signals.csv");
            const fs::path structures = fixtures.directory / (name + "-structures.csv");
            fixtures.create(map, "BveTs Map 2.02:utf-8\r\nStructure.Load('" + structures.filename().u8string() +
                "');\r\nSignal.Load('" + signals.filename().u8string() + "');\r\n0;\r\n");
            fixtures.create(structures, "BveTs Structure List 2.00:utf-8\r\ns1,missing1.x\r\ns2,missing2.x\r\n");
            std::string contents = bom + "BveTs Signal Aspects List 2.00:" + encoding + newline + comment + newline;
            const auto csv = [&](const std::string& key, size_t count, bool last_value) {
                std::string line = key;
                for (size_t i = 0; i < count; ++i) {
                    line += ',';
                    if (i == 0) line += "s1";
                    else if (last_value && i + 1 == count) line += "s2";
                }
                return line + newline;
            };
            contents += "A,s1,," + newline + ",s2," + newline;
            contents += "B,s1" + newline + "blank,,," + newline + "," + newline;
            contents += csv("wide508", 508, false) + csv("wide509", 509, false) +
                csv("hidden510", 510, true) + csv("hidden512", 512, true);
            fixtures.create(signals, contents);
            const std::string map_before = signal_columns_bytes(map);
            const std::string structures_before = signal_columns_bytes(structures);
            *out << "stage=fixture variant=" << name << '\n';
            out->flush();
            SignalColumnShapes saved_shapes;
            {
                App app(nullptr, settings, 1.0f, false, false);
                open(app, map.u8string());
                const auto baseline = shapes(app);
                require("fixture_source_widths", baseline.size() == 7 && baseline[0].main == 3 &&
                    baseline[0].glare == 2 && baseline[1].main == 1 && baseline[2].main == 3 &&
                    baseline[2].glare == 1 && baseline[3].main == 508 && baseline[4].main == 509 &&
                    baseline[5].main == 510 && baseline[6].main == 512);
                render(app);
                require("hidden_columns_retained", app.table_cache_.signal_aspect_structure_key_columns == 512 &&
                    app.table_cache_.signal_aspect_column_headers.size() == 511 &&
                    row_at(app, 6).values.back() == "s2");
                act(app, SignalAspectColumnAction::Append, 0, true, "glare_append");
                require("glare_append_independent", row_at(app, 0).primary_structure_field_count == 3 &&
                    row_at(app, 0).secondary_structure_field_count == 3);
                act(app, SignalAspectColumnAction::RemoveLast, 0, true, "glare_remove_empty");
                require("glare_inverse_no_dirty", shapes(app) == baseline && !app.has_editable_list_drafts(
                    app.signal_aspect_edit_));

                // Commit a live cell before deciding whether deletion needs confirmation.
                auto& edit = app.signal_aspect_edit_;
                edit.editing_edit_id = editable_list_row_identity(row_at(app, 0));
                edit.editing_column = 3;
                edit.editing_baseline.clear();
                edit.edit_buffer = "s2";
                require("active_edit_remove_requested", app.request_signal_aspect_column_action(
                    SignalAspectColumnAction::RemoveLast, 0, false));
                require("active_edit_committed_before_confirmation", edit.editing_edit_id.empty() &&
                    row_at(app, 0).values[3] == "s2" && app.signal_aspect_column_confirmation_.has_value());
                const auto before_cancel = shapes(app);
                require("remove_cancel", app.resolve_signal_aspect_column_confirmation(false));
                require("remove_cancel_keeps_content", shapes(app) == before_cancel);
                act(app, SignalAspectColumnAction::RemoveLast, 0, false, "remove_nonempty");
                require("remove_preserves_glare_boundary", row_at(app, 0).primary_structure_field_count == 2 &&
                    row_at(app, 0).secondary_structure_field_count == 2 && row_at(app, 0).values[3] == "s2");
                apply(app, "active_edit_delete_apply");
                require("fixture_memory_apply_disk_unchanged", signal_columns_bytes(signals) == contents);
                require("fixture_revert", app.revert_all_pending_edits());
                require("fixture_revert_baseline", shapes(app) == baseline);

                // This leaves flattened values identical while moving the main/glare boundary.
                act(app, SignalAspectColumnAction::RemoveLast, 2, false, "blank_main_shrink");
                act(app, SignalAspectColumnAction::Append, 2, true, "blank_glare_grow");
                require("shape_only_change_is_dirty", row_at(app, 2).values == baseline[2].values &&
                    row_at(app, 2).primary_structure_field_count == 2 &&
                    row_at(app, 2).secondary_structure_field_count == 2 &&
                    app.has_editable_list_drafts(edit));
                apply(app, "shape_only_apply");
                require("shape_only_revert", app.revert_all_pending_edits());
                require("shape_only_revert_baseline", shapes(app) == baseline);

                if (variant == 0) {
                    // A confirmation must not delete a different logical payload after a move,
                    // even when its physical slot, width and last value are all unchanged.
                    act(app, SignalAspectColumnAction::AlignAll, -1, false, "stale_confirm_align");
                    row_at(app, 3).values.back() = "s2";
                    row_at(app, 4).values.back() = "s2";
                    require("stale_payload_confirmation_requested", app.request_signal_aspect_column_action(
                        SignalAspectColumnAction::RemoveLast, 3, false) && app.signal_aspect_column_confirmation_);
                    require("stale_payload_block_moved", app.move_editable_list_row(edit,
                        k_signal_aspect_edit_spec, 3, 1));
                    const auto moved_pending = shapes(app);
                    require("stale_payload_confirmation_rejected", !app.resolve_signal_aspect_column_confirmation(true));
                    require("stale_payload_rejection_is_atomic", shapes(app) == moved_pending);
                    require("stale_payload_revert", app.revert_all_pending_edits());
                    require("stale_payload_baseline", shapes(app) == baseline);

                    require("stale_value_confirmation_requested", app.request_signal_aspect_column_action(
                        SignalAspectColumnAction::RemoveLast) && app.signal_aspect_column_confirmation_);
                    row_at(app, 5).values.back() = "s1";
                    const auto changed_pending = shapes(app);
                    require("stale_value_confirmation_rejected", !app.resolve_signal_aspect_column_confirmation(true));
                    require("stale_value_rejection_is_atomic", shapes(app) == changed_pending);
                    require("stale_value_revert", app.revert_all_pending_edits());
                    require("stale_value_baseline", shapes(app) == baseline);

                    const size_t deleted_index = edit.visible_rows.at(1);
                    const auto deleted_values = edit.rows.at(deleted_index).values;
                    const size_t deleted_main = edit.rows.at(deleted_index).primary_structure_field_count;
                    require("pending_row_delete", app.delete_editable_list_row(edit, k_signal_aspect_edit_spec, 1));
                    for (const auto action : {SignalAspectColumnAction::Append, SignalAspectColumnAction::AlignAll,
                                             SignalAspectColumnAction::TrimTrailing}) {
                        act(app, action, -1, false, "global_skips_deleted");
                        require("deleted_draft_unchanged", edit.rows.at(deleted_index).deleted &&
                            edit.rows.at(deleted_index).values == deleted_values &&
                            edit.rows.at(deleted_index).primary_structure_field_count == deleted_main);
                    }
                    apply(app, "pending_delete_resize_apply");
                    require("pending_delete_removed_only_target", shapes(app).size() + 1 == baseline.size() &&
                        std::none_of(app.model_.signal_aspects.begin(), app.model_.signal_aspects.end(),
                            [](const TableRow& row) { return table_cell(row, "signalAspectKey") == "B"; }));
                    require("pending_delete_revert", app.revert_all_pending_edits());
                    require("pending_delete_baseline", shapes(app) == baseline);

                    require("move_signal_block", app.move_editable_list_row(edit, k_signal_aspect_edit_spec, 0, 1));
                    const int moved_row = row_named(app, "A");
                    require("move_keeps_glare_pair", moved_row == 1 && row_at(app, 0).values[0] == "B" &&
                        row_at(app, moved_row).primary_structure_field_count == 3 &&
                        row_at(app, moved_row).secondary_structure_field_count == 2);
                    act(app, SignalAspectColumnAction::Append, moved_row, false, "moved_main_append");
                    act(app, SignalAspectColumnAction::Append, moved_row, true, "moved_glare_append");
                    apply(app, "moved_block_resize_apply");
                    require("moved_block_revert", app.revert_all_pending_edits());
                    require("moved_block_baseline", shapes(app) == baseline);
                }

                act(app, SignalAspectColumnAction::Append, 3, false, "append_508_to_509");
                require("at_cap_width", row_at(app, 3).primary_structure_field_count == 509);
                act(app, SignalAspectColumnAction::Append, 3, false, "append_509_to_510");
                require("over_cap_width", row_at(app, 3).primary_structure_field_count == 510);
                const auto before_global = shapes(app);
                act(app, SignalAspectColumnAction::Append, -1, false, "append_all_ragged");
                const auto after_global = shapes(app);
                for (size_t i = 0; i < before_global.size(); ++i)
                    require("append_all_preserves_ragged_" + std::to_string(i),
                        after_global[i].main == before_global[i].main + 1 &&
                        after_global[i].glare == (before_global[i].glare ? before_global[i].glare + 1 : 0));
                act(app, SignalAspectColumnAction::RemoveLast, -1, false, "remove_all_empty");
                require("global_empty_delete_inverse", shapes(app) == before_global);
                require("global_nonempty_delete_requested", app.request_signal_aspect_column_action(
                    SignalAspectColumnAction::RemoveLast));
                require("global_nonempty_delete_confirmed_once", app.signal_aspect_column_confirmation_.has_value());
                require("global_delete_cancel", app.resolve_signal_aspect_column_confirmation(false));
                require("global_delete_cancel_is_atomic", shapes(app) == before_global);
                act(app, SignalAspectColumnAction::RemoveLast, -1, false, "global_nonempty_delete");
                const auto after_delete = shapes(app);
                for (size_t i = 0; i < before_global.size(); ++i)
                    require("global_delete_each_physical_row_" + std::to_string(i),
                        after_delete[i].main == std::max(size_t{1}, before_global[i].main - 1) &&
                        after_delete[i].glare == (before_global[i].glare ?
                            std::max(size_t{1}, before_global[i].glare - 1) : 0));
                apply(app, "global_delete_apply");
                require("global_delete_revert", app.revert_all_pending_edits());
                require("global_delete_revert_baseline", shapes(app) == baseline);

                act(app, SignalAspectColumnAction::AlignAll, -1, false, "align_actual_max");
                for (const auto& row : shapes(app)) require("alignment_includes_hidden_columns",
                    row.main == 512 && (row.glare == 0 || row.glare == 512));
                require("alignment_preserves_hidden_value", row_at(app, 5).values[510] == "s2" &&
                    row_at(app, 6).values[512] == "s2");
                apply(app, "align_apply");
                act(app, SignalAspectColumnAction::TrimTrailing, -1, false, "trim_all");
                require("trim_preserves_internal_and_hidden_values", row_at(app, 0).primary_structure_field_count == 1 &&
                    row_at(app, 0).secondary_structure_field_count == 1 &&
                    row_at(app, 2).primary_structure_field_count == 1 &&
                    row_at(app, 2).secondary_structure_field_count == 1 &&
                    row_at(app, 5).primary_structure_field_count == 510 &&
                    row_at(app, 6).primary_structure_field_count == 512);
                const auto before_min = shapes(app);
                app.request_signal_aspect_column_action(SignalAspectColumnAction::RemoveLast, 2, false);
                require("minimum_one_cell_preserved", shapes(app) == before_min &&
                    !app.signal_aspect_column_confirmation_);
                apply(app, "trim_apply");

                // Pending insert replay must retain its changing physical widths.
                require("insert_row", app.insert_editable_list_row(edit, k_signal_aspect_edit_spec, 0, false));
                auto inserted = std::find_if(edit.rows.begin(), edit.rows.end(), [](const auto& row) {
                    return row.inserted && !row.deleted;
                });
                require("inserted_draft_present", inserted != edit.rows.end());
                inserted->values[0] = "inserted";
                // All-empty primary is intentional and supported by this workflow.
                int inserted_row = row_named(app, "inserted");
                act(app, SignalAspectColumnAction::Append, inserted_row, false, "insert_append");
                require("insert_six_cells", row_at(app, inserted_row).primary_structure_field_count == 6);
                require("unapplied_draft_blocks_save", !app.save_pending_edits(false) &&
                    signal_columns_bytes(signals) == contents);
                apply(app, "insert_first_apply");
                inserted_row = row_named(app, "inserted");
                act(app, SignalAspectColumnAction::Append, inserted_row, false, "insert_replay_append");
                apply(app, "insert_repeated_apply");
                inserted_row = row_named(app, "inserted");
                require("insert_replay_seven_cells", row_at(app, inserted_row).primary_structure_field_count == 7);
                require("insert_add_glare", app.add_editable_list_secondary_row(edit, k_signal_aspect_edit_spec, inserted_row));
                act(app, SignalAspectColumnAction::Append, inserted_row, true, "insert_glare_append");
                apply(app, "insert_glare_apply");
                inserted_row = row_named(app, "inserted");
                act(app, SignalAspectColumnAction::TrimTrailing, inserted_row, false, "insert_trim_main");
                act(app, SignalAspectColumnAction::TrimTrailing, inserted_row, true, "insert_trim_glare");
                apply(app, "insert_trim_apply");
                inserted_row = row_named(app, "inserted");
                require("all_empty_insert_retains_glare", row_at(app, inserted_row).primary_structure_field_count == 1 &&
                    row_at(app, inserted_row).secondary_structure_field_count == 1);
                saved_shapes = shapes(app);
                for (const auto& source : app.model_.edit_files)
                    require("save_target_inside_fixture", fs::u8path(source.file_path).parent_path() == fixtures.directory);
                require("fixture_save", app.save_pending_edits(false));
                const std::string saved = signal_columns_bytes(signals);
                require("save_retains_encoding_bom_comments", saved.rfind(bom +
                    "BveTs Signal Aspects List 2.00:" + encoding + newline + comment + newline, 0) == 0);
                require("save_retains_line_endings", variant == 2 ? saved.find('\r') == std::string::npos :
                    std::count(saved.begin(), saved.end(), '\r') == std::count(saved.begin(), saved.end(), '\n'));
                require("save_changes_only_signal_source", signal_columns_bytes(map) == map_before &&
                    signal_columns_bytes(structures) == structures_before);
            }
            {
                App reload(nullptr, settings, 1.0f, false, false);
                open(reload, map.u8string());
                require("save_fresh_reload_shape_values", shapes(reload) == saved_shapes);
                require("save_fresh_reload_clean", reload.pending_edit_changes_.empty() &&
                    !reload.has_editable_list_drafts(reload.signal_aspect_edit_));
                render(reload);
            }
        }
        {
            const fs::path map = fixtures.directory / "empty-map.txt";
            const fs::path signals = fixtures.directory / "empty-signals.csv";
            fixtures.create(map, "BveTs Map 2.02:utf-8\r\nSignal.Load('empty-signals.csv');\r\n");
            fixtures.create(signals, "BveTs Signal Aspects List 2.00:utf-8\r\n");
            App empty(nullptr, settings, 1.0f, false, false);
            open(empty, map.u8string());
            require("empty_list_has_no_rows", shapes(empty).empty());
            for (const auto action : {SignalAspectColumnAction::Append, SignalAspectColumnAction::RemoveLast,
                                     SignalAspectColumnAction::TrimTrailing, SignalAspectColumnAction::AlignAll}) {
                require("empty_list_action_safe", empty.request_signal_aspect_column_action(action));
                require("empty_list_action_no_dirty", shapes(empty).empty() &&
                    !empty.signal_aspect_column_confirmation_ && !empty.has_editable_list_drafts(
                        empty.signal_aspect_edit_));
            }
            require("empty_list_first_insert", empty.insert_editable_list_row(empty.signal_aspect_edit_,
                k_signal_aspect_edit_spec, -1, false));
            row_at(empty, 0).values[0] = "firstEmpty";
            act(empty, SignalAspectColumnAction::TrimTrailing, 0, false, "empty_first_trim");
            apply(empty, "empty_first_apply");
            require("empty_first_blank_retained", row_at(empty, 0).primary_structure_field_count == 1 &&
                row_at(empty, 0).values.size() == 2 && row_at(empty, 0).values[1].empty());
            require("empty_first_revert", empty.revert_all_pending_edits());
            require("empty_first_revert_no_source_write", shapes(empty).empty() &&
                signal_columns_bytes(signals) == "BveTs Signal Aspects List 2.00:utf-8\r\n");
        }
        require("fixture_files_cleaned", fixtures.cleanup());
    } catch (const std::exception& error) {
        *out << "exception=" << error.what() << '\n';
        ++failures;
    }
    for (const auto& source : protected_sources) {
        try {
            const std::string after = signal_columns_bytes(fs::u8path(source.first));
            *out << "source_guard_path=" << source.first << "\nsource_guard_before_fnv64="
                 << signal_columns_hash(source.second) << "\nsource_guard_after_fnv64="
                 << signal_columns_hash(after) << '\n';
            check("source_guard_byte_equal", source.second == after);
        } catch (const std::exception& error) {
            *out << "source_guard_error=" << error.what() << '\n';
            ++failures;
        }
    }
    ImGui::EndFrame();
    ImPlot::DestroyContext();
    ImGui::DestroyContext();
    *out << "failed_cases=" << failures << "\nresult=" << (failures == 0 ? "PASS" : "FAIL") << '\n';
    out->flush();
    return failures == 0 ? 0 : 24;
}
#endif
