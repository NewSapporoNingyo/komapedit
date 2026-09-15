/*
 * Copyright (c) 2026 Sapporo_ningyo
 * Licensed under Apache License 2.0; see LICENSE and NOTICE.
 */

#include "kme.h"
#include "app_settings.h"
#include "debug_headless.h"
#include "maploader.h"
#include "text_decoder.h"

#include "implot.h"

#include <windows.h>
#include <objbase.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#ifndef NDEBUG
namespace {
namespace fs = std::filesystem;

constexpr int k_hot_cache_calls = 100000;

struct BenchmarkHandleGuard {
    void*& handle;
    ~BenchmarkHandleGuard() {
        if (handle) kv_free(handle);
    }
};

struct CacheSummary {
    std::uint64_t hash = 0;
    size_t rows = 0;
    size_t cells = 0;
    size_t identities = 0;
    size_t dynamic_columns = 0;
};

struct MetricSummary {
    double median_ms = 0.0;
    double p95_ms = 0.0;
    double max_ms = 0.0;
};

void require_benchmark(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

void hash_string(KmeByteHash64& hash, std::string_view value) {
    hash.integer(static_cast<std::uint64_t>(value.size()));
    hash.bytes(value);
}

void hash_float(KmeByteHash64& hash, float value) {
    std::uint32_t bits = 0;
    static_assert(sizeof(bits) == sizeof(value), "unexpected float size");
    std::memcpy(&bits, &value, sizeof(bits));
    hash.integer(bits);
}

void hash_source(KmeByteHash64& hash, const EditSourceInfo& source) {
    hash_string(hash, source.file_path);
    hash.integer(static_cast<std::uint64_t>(source.line));
    hash.integer(static_cast<std::uint64_t>(source.column));
    hash_string(hash, source.raw_text_preview);
}

void hash_cached_rows(KmeByteHash64& hash,
                      const std::vector<CachedTableRow>& rows,
                      CacheSummary& summary) {
    hash.integer(static_cast<std::uint64_t>(rows.size()));
    summary.rows += rows.size();
    for (const CachedTableRow& row : rows) {
        hash.integer(static_cast<std::uint64_t>(row.cells.size()));
        summary.cells += row.cells.size();
        for (const std::string& cell : row.cells) hash_string(hash, cell);
        hash_string(hash, row.edit_id);
        hash_source(hash, row.source);
        hash_string(hash, row.open_path);
        hash_string(hash, row.tooltip_text);
        hash.integer(static_cast<std::uint64_t>(row.editable_field_count));
        hash.integer(static_cast<std::uint64_t>(row.primary_structure_field_count));
        hash.integer(static_cast<std::uint64_t>(row.secondary_structure_field_count));
        hash.integer(static_cast<std::uint64_t>(row.repeater_chain_begin_index));
        hash.integer(static_cast<std::uint64_t>(row.repeater_chain_begin_count));
        hash.byte(row.invalid_track_key ? 1 : 0);
        if (!row.edit_id.empty() || !row.source.file_path.empty()) {
            ++summary.identities;
        }
    }
}

CacheSummary summarize_table_cache(const TableUiCache& cache) {
    KmeByteHash64 hash;
    CacheSummary summary;
    hash.byte(cache.valid ? 1 : 0);
    hash_float(hash, cache.font_size);
    hash_float(hash, cache.cell_padding_x);

    hash_cached_rows(hash, cache.station_position_rows, summary);
    hash_cached_rows(hash, cache.station_definition_rows, summary);
    hash_cached_rows(hash, cache.structure_rows, summary);
    hash_cached_rows(hash, cache.structure_between_rows, summary);
    hash_cached_rows(hash, cache.structure_model_rows, summary);
    hash_cached_rows(hash, cache.other_train_rows, summary);
    hash_cached_rows(hash, cache.other_train_stop_rows, summary);

    hash.integer(static_cast<std::uint64_t>(cache.other_train_stop_groups.size()));
    summary.rows += cache.other_train_stop_groups.size();
    for (const CachedOtherTrainStopGroup& group : cache.other_train_stop_groups) {
        hash_string(hash, group.train_key);
        hash_string(hash, group.enable_time);
        hash.integer(static_cast<std::uint64_t>(group.row_indices.size()));
        for (size_t index : group.row_indices) {
            hash.integer(static_cast<std::uint64_t>(index));
        }
    }

    hash_cached_rows(hash, cache.section_begin_rows, summary);
    hash_cached_rows(hash, cache.section_speed_limit_rows, summary);
    hash.integer(static_cast<std::uint64_t>(cache.section_begin_value_columns));
    hash.integer(static_cast<std::uint64_t>(cache.section_speed_limit_value_columns));

    hash.integer(static_cast<std::uint64_t>(cache.variable_rows.size()));
    summary.rows += cache.variable_rows.size();
    for (const CachedVariableRow& row : cache.variable_rows) {
        hash.byte(row.group_header ? 1 : 0);
        hash_string(hash, row.name);
        hash_string(hash, row.value);
        hash_string(hash, row.expression);
        hash_string(hash, row.file_path);
    }

    hash_cached_rows(hash, cache.repeater_rows, summary);
    hash_cached_rows(hash, cache.signal_aspect_rows, summary);

    hash.integer(static_cast<std::uint64_t>(cache.signal_aspect_display_rows.size()));
    summary.rows += cache.signal_aspect_display_rows.size();
    for (const EditableListDisplayRow& row : cache.signal_aspect_display_rows) {
        hash.integer(static_cast<std::uint64_t>(row.logical_row));
        hash.integer(static_cast<std::uint64_t>(row.structure_field_offset));
        hash.integer(static_cast<std::uint64_t>(row.structure_field_count));
        hash_string(hash, row.sequence);
        hash.byte(row.secondary ? 1 : 0);
    }

    hash_cached_rows(hash, cache.signal_rows, summary);
    hash_cached_rows(hash, cache.beacon_rows, summary);
    hash_cached_rows(hash, cache.irregularity_rows, summary);
    hash_cached_rows(hash, cache.map_sound_rows, summary);
    hash_cached_rows(hash, cache.map_sound_3d_rows, summary);
    hash_cached_rows(hash, cache.rolling_noise_rows, summary);
    hash_cached_rows(hash, cache.flange_noise_rows, summary);
    hash_cached_rows(hash, cache.joint_noise_rows, summary);
    hash_cached_rows(hash, cache.background_rows, summary);
    hash_cached_rows(hash, cache.adhesion_rows, summary);
    hash_cached_rows(hash, cache.cab_illuminance_rows, summary);
    hash_cached_rows(hash, cache.fog_rows, summary);
    hash_cached_rows(hash, cache.legacy_fog_rows, summary);
    hash_cached_rows(hash, cache.draw_distance_rows, summary);
    hash_cached_rows(hash, cache.speed_limit_rows, summary);
    hash_cached_rows(hash, cache.sound_list_rows, summary);
    hash_cached_rows(hash, cache.sound_3d_list_rows, summary);

    const float scalar_widths[] = {
        cache.structure_file_path_width,
        cache.structure_between_file_path_width,
        cache.structure_model_file_path_width,
        cache.other_train_distance_width,
        cache.other_train_file_path_width,
        cache.other_train_stop_distance_width,
        cache.other_train_stop_file_path_width,
        cache.signal_distance_width,
        cache.signal_file_path_width,
        cache.beacon_distance_width,
        cache.beacon_file_path_width,
    };
    hash.integer(IM_ARRAYSIZE(scalar_widths));
    for (float value : scalar_widths) hash_float(hash, value);

    hash.integer(static_cast<std::uint64_t>(cache.signal_aspect_structure_key_columns));
    hash.integer(static_cast<std::uint64_t>(cache.signal_aspect_column_headers.size()));
    for (const std::string& header : cache.signal_aspect_column_headers) {
        hash_string(hash, header);
    }
    hash.integer(static_cast<std::uint64_t>(cache.signal_aspect_column_widths.size()));
    for (float width : cache.signal_aspect_column_widths) hash_float(hash, width);

    const float remaining_widths[] = {
        cache.sound_list_file_path_width,
        cache.sound_list_buffer_count_width,
        cache.sound_3d_list_file_path_width,
        cache.sound_3d_list_buffer_count_width,
        cache.repeater_distance_width,
        cache.repeater_interval_width,
        cache.repeater_file_path_width,
        cache.irregularity_distance_width,
        cache.irregularity_file_path_width,
        cache.map_sound_distance_width,
        cache.map_sound_file_path_width,
        cache.map_sound_3d_distance_width,
        cache.map_sound_3d_file_path_width,
        cache.rolling_noise_distance_width,
        cache.rolling_noise_file_path_width,
        cache.flange_noise_distance_width,
        cache.flange_noise_file_path_width,
        cache.joint_noise_distance_width,
        cache.joint_noise_file_path_width,
        cache.background_distance_width,
        cache.background_file_path_width,
        cache.adhesion_distance_width,
        cache.adhesion_file_path_width,
        cache.cab_illuminance_distance_width,
        cache.cab_illuminance_file_path_width,
        cache.fog_distance_width,
        cache.fog_file_path_width,
        cache.legacy_fog_distance_width,
        cache.legacy_fog_file_path_width,
        cache.draw_distance_distance_width,
        cache.draw_distance_file_path_width,
        cache.speed_limit_distance_width,
        cache.speed_limit_file_path_width,
    };
    hash.integer(IM_ARRAYSIZE(remaining_widths));
    for (float value : remaining_widths) hash_float(hash, value);

    summary.dynamic_columns = cache.section_begin_value_columns +
        cache.section_speed_limit_value_columns +
        cache.signal_aspect_column_headers.size();
    summary.hash = hash.value;
    return summary;
}

std::uint64_t source_hash(const std::string& bytes) {
    KmeByteHash64 hash;
    hash.integer(static_cast<std::uint64_t>(bytes.size()));
    hash.bytes(bytes);
    return hash.value;
}

std::string hash_text(std::uint64_t hash) {
    std::ostringstream text;
    text << std::hex << std::setfill('0') << std::setw(16) << hash;
    return text.str();
}

fs::path normalized_source_path(const std::string& path) {
    return fs::absolute(fs::path(utf8_to_wide(path))).lexically_normal();
}

using SourceBytes = std::map<std::string, std::string>;

void capture_source(SourceBytes& sources, const std::string& path) {
    if (path.empty()) return;
    const fs::path normalized = normalized_source_path(path);
    const std::string key = wide_to_utf8(normalized.wstring());
    sources.emplace(key, kme::maploader::read_binary_file(normalized));
}

SourceBytes capture_sources(const std::string& root_path,
                            const std::vector<EditSourceFileInfo>& edit_files) {
    SourceBytes sources;
    capture_source(sources, root_path);
    for (const EditSourceFileInfo& source : edit_files) {
        capture_source(sources, source.file_path);
    }
    return sources;
}

bool verify_sources(const SourceBytes& before, std::ostream& out) {
    bool unchanged = true;
    for (const auto& entry : before) {
        const fs::path path(utf8_to_wide(entry.first));
        const std::string after = kme::maploader::read_binary_file(path);
        const bool matches = after == entry.second;
        out << "source path=\"" << entry.first << "\" bytes="
            << entry.second.size() << " before_hash="
            << hash_text(source_hash(entry.second)) << " after_hash="
            << hash_text(source_hash(after)) << " unchanged="
            << (matches ? 1 : 0) << "\n";
        unchanged = unchanged && matches;
    }
    return unchanged;
}

MetricSummary summarize_metric(std::vector<double> samples) {
    require_benchmark(!samples.empty(), "benchmark produced no samples");
    std::sort(samples.begin(), samples.end());
    const size_t middle = samples.size() / 2;
    const double median = samples.size() % 2 == 0
        ? (samples[middle - 1] + samples[middle]) * 0.5
        : samples[middle];
    const size_t p95_index = std::min(
        samples.size() - 1,
        static_cast<size_t>(std::ceil(samples.size() * 0.95)) - 1);
    return {median, samples[p95_index], samples.back()};
}

double elapsed_ms(std::chrono::steady_clock::time_point start) {
    return std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - start).count();
}

}

int App::run_debug_headless_table_cache_benchmark(
    const HeadlessTableCacheBenchmarkOptions& options) {
    std::ofstream output_file;
    std::ostream* out = &std::cout;
    if (!options.output_path.empty()) {
        output_file.open(fs::path(utf8_to_wide(options.output_path)),
                         std::ios::out | std::ios::trunc | std::ios::binary);
        if (!output_file) {
            std::cerr << "failed to open headless output: "
                      << options.output_path << "\n";
            return 1;
        }
        output_file.write("\xEF\xBB\xBF", 3);
        out = &output_file;
    }

    *out << "komapedit debug-headless-table-cache-bench\n"
         << "build=Debug repeat=" << options.repeat
         << " unit_distance=" << options.unit_distance
         << " hot_cache_calls=" << k_hot_cache_calls << "\n"
         << "input=\"" << options.path << "\"\n";
    out->flush();

    int exit_code = 0;
    const HRESULT apartment = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    SourceBytes protected_sources;
    bool source_integrity = false;

    try {
        require_benchmark(SUCCEEDED(apartment) || apartment == RPC_E_CHANGED_MODE,
                          "COM initialization failed");
        ImGui::CreateContext();
        ImPlot::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.DisplaySize = ImVec2(1280.0f, 720.0f);
        io.DeltaTime = 1.0f / 60.0f;
        io.IniFilename = nullptr;
        io.MousePos = ImVec2(-10000.0f, -10000.0f);
        io.Fonts->AddFontDefault();
        io.Fonts->Build();
        *out << "stage=context-ready\n";
        out->flush();

        {
            LoadResult loaded = load_map_worker(
                options.path, options.unit_distance,
                false, 0.0, 0.0, options.unit_distance,
                LoadModelOptions{true, "edit"});
            BenchmarkHandleGuard loaded_guard{loaded.handle};
            *out << "stage=load-returned ok=" << (loaded.ok ? 1 : 0)
                 << "\n";
            out->flush();
            require_benchmark(loaded.ok,
                              "input load failed: " + loaded.error);
            require_benchmark(loaded.handle != nullptr,
                              "input load returned no maploader handle");
            require_benchmark(loaded.full_edit_registry,
                              "input load did not provide the edit registry");

            UserSettings settings;
            settings.language = Language::En;
            settings.edit_mode_enabled = true;
            App app(nullptr, settings, 1.0f, false, false);
            *out << "stage=app-created\n";
            out->flush();
            app.handle_ = loaded.handle;
            loaded.handle = nullptr;
            app.model_ = std::move(loaded.model);
            app.file_path_ = options.path;
            app.has_model_ = true;
            app.edit_mode_enabled_ = true;
            app.edit_registry_loaded_ = true;
            app.edit_memory_matches_pending_ledger_ = true;
            app.unit_distance_ = options.unit_distance;
            app.cp_interval_ = options.unit_distance;
            app.dmin_ = app.model_.default_min;
            app.dmax_ = app.model_.default_max;

            auto render_all_table_windows = [&]() {
                auto render_window = [&](bool& visible,
                                         void (App::*render)()) {
                    visible = true;
                    ImGui::SetNextWindowPos(
                        ImVec2(0.0f, 0.0f), ImGuiCond_Always);
                    ImGui::SetNextWindowSize(
                        ImVec2(1280.0f, 720.0f), ImGuiCond_Always);
                    (app.*render)();
                };

                ImGui::NewFrame();
                render_window(app.show_othertracks_window_,
                              &App::render_othertracks_window);
                render_window(app.show_station_list_window_,
                              &App::render_station_list_window);
                render_window(app.show_structures_window_,
                              &App::render_structures_window);
                render_window(app.show_structures_between_window_,
                              &App::render_structures_between_window);
                render_window(app.show_structure_models_window_,
                              &App::render_structure_models_window);
                render_window(app.show_other_trains_window_,
                              &App::render_other_trains_window);
                render_window(app.show_sound_list_window_,
                              &App::render_sound_list_window);
                render_window(app.show_sound_3d_list_window_,
                              &App::render_sound_3d_list_window);
                render_window(app.show_repeaters_window_,
                              &App::render_repeaters_window);
                render_window(app.show_signal_aspects_window_,
                              &App::render_signal_aspects_window);
                render_window(app.show_signals_window_,
                              &App::render_signals_window);
                render_window(app.show_sections_window_,
                              &App::render_sections_window);
                render_window(app.show_variables_window_,
                              &App::render_variables_window);
                render_window(app.show_scenario_file_window_,
                              &App::render_scenario_file_window);
                render_window(app.show_beacons_window_,
                              &App::render_beacons_window);
                render_window(app.show_irregularities_window_,
                              &App::render_irregularities_window);
                render_window(app.show_map_sounds_window_,
                              &App::render_map_sounds_window);
                render_window(app.show_map_sound_3d_window_,
                              &App::render_map_sound_3d_window);
                render_window(app.show_rolling_noises_window_,
                              &App::render_rolling_noises_window);
                render_window(app.show_flange_noises_window_,
                              &App::render_flange_noises_window);
                render_window(app.show_joint_noises_window_,
                              &App::render_joint_noises_window);
                render_window(app.show_backgrounds_window_,
                              &App::render_backgrounds_window);
                render_window(app.show_adhesions_window_,
                              &App::render_adhesions_window);
                render_window(app.show_cab_illuminance_window_,
                              &App::render_cab_illuminance_window);
                render_window(app.show_fogs_window_,
                              &App::render_fogs_window);
                render_window(app.show_legacy_fogs_window_,
                              &App::render_legacy_fogs_window);
                render_window(app.show_lighting_window_,
                              &App::render_lighting_window);
                render_window(app.show_draw_distances_window_,
                              &App::render_draw_distances_window);
                render_window(app.show_speed_limits_window_,
                              &App::render_speed_limits_window);
                ImGui::Render();
            };

            protected_sources = capture_sources(options.path,
                                                app.model_.edit_files);
            *out << "stage=sources-captured\n";
            out->flush();
            require_benchmark(!protected_sources.empty(),
                              "no physical source files were captured");
            *out << "sources=" << protected_sources.size()
                 << " edit_files=" << app.model_.edit_files.size() << "\n";

            ImGui::NewFrame();
            app.invalidate_table_cache();
            app.ensure_table_cache();
            ImGui::EndFrame();
            *out << "stage=cache-warm\n";
            out->flush();
            render_all_table_windows();
            *out << "stage=render-warm\n";
            out->flush();
            const CacheSummary warmup = summarize_table_cache(app.table_cache_);
            require_benchmark(app.table_cache_.valid,
                              "warm-up did not publish a valid cache");
            *out << "warmup cache_hash=" << hash_text(warmup.hash)
                 << " rows=" << warmup.rows
                 << " cells=" << warmup.cells
                 << " identities=" << warmup.identities
                 << " dynamic_columns=" << warmup.dynamic_columns << "\n";

            std::vector<double> cold_samples;
            std::vector<double> hot_samples;
            std::vector<double> render_samples;
            cold_samples.reserve(static_cast<size_t>(options.repeat));
            hot_samples.reserve(static_cast<size_t>(options.repeat));
            render_samples.reserve(static_cast<size_t>(options.repeat));

            for (int iteration = 1; iteration <= options.repeat; ++iteration) {
                ImGui::NewFrame();
                auto start = std::chrono::steady_clock::now();
                app.invalidate_table_cache();
                app.ensure_table_cache();
                const double cold_ms = elapsed_ms(start);

                start = std::chrono::steady_clock::now();
                for (int call = 0; call < k_hot_cache_calls; ++call) {
                    app.ensure_table_cache();
                }
                const double hot_ms = elapsed_ms(start) / k_hot_cache_calls;
                ImGui::EndFrame();

                start = std::chrono::steady_clock::now();
                render_all_table_windows();
                const double render_ms = elapsed_ms(start);

                const CacheSummary current = summarize_table_cache(app.table_cache_);
                const bool stable = current.hash == warmup.hash &&
                    current.rows == warmup.rows &&
                    current.cells == warmup.cells &&
                    current.identities == warmup.identities &&
                    current.dynamic_columns == warmup.dynamic_columns;
                require_benchmark(stable,
                                  "table cache summary changed between iterations");

                cold_samples.push_back(cold_ms);
                hot_samples.push_back(hot_ms);
                render_samples.push_back(render_ms);
                *out << std::fixed << std::setprecision(6)
                     << "sample=" << iteration
                     << " cold_cache_ms=" << cold_ms
                     << " hot_cache_ms=" << hot_ms
                     << " render_ms=" << render_ms
                     << " cache_hash=" << hash_text(current.hash)
                     << " stable=1\n";
                out->flush();
            }

            const auto print_summary = [&](const char* metric,
                                           const std::vector<double>& samples) {
                const MetricSummary summary = summarize_metric(samples);
                *out << std::fixed << std::setprecision(6)
                     << "summary metric=" << metric
                     << " samples=" << samples.size()
                     << " median_ms=" << summary.median_ms
                     << " p95_ms=" << summary.p95_ms
                     << " max_ms=" << summary.max_ms << "\n";
            };
            print_summary("cold_cache", cold_samples);
            print_summary("hot_cache", hot_samples);
            print_summary("render", render_samples);
        }

        source_integrity = verify_sources(protected_sources, *out);
        require_benchmark(source_integrity,
                          "one or more protected source files changed");
        *out << "source_integrity=PASS\n";
    } catch (const std::exception& error) {
        *out << "error=\"" << error.what() << "\"\n";
        exit_code = 2;
        if (!protected_sources.empty() && !source_integrity) {
            try {
                source_integrity = verify_sources(protected_sources, *out);
                *out << "source_integrity="
                     << (source_integrity ? "PASS" : "FAIL") << "\n";
            } catch (const std::exception& source_error) {
                *out << "source_integrity=FAIL error=\""
                     << source_error.what() << "\"\n";
            }
        }
    }

    if (ImPlot::GetCurrentContext()) ImPlot::DestroyContext();
    if (ImGui::GetCurrentContext()) ImGui::DestroyContext();
    if (SUCCEEDED(apartment)) CoUninitialize();
    *out << "result=" << (exit_code == 0 ? "PASS" : "FAIL") << "\n";
    out->flush();
    return exit_code;
}
#endif
