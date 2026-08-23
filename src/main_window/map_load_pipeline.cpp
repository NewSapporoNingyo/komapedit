/*
 * Copyright (c) 2026 Sapporo_ningyo
 *
 * Licensed under Apache License 2.0; see LICENSE and NOTICE.
 * The GUI uses Dear ImGui and ImPlot; see THIRD_PARTY_NOTICES.md.
 */

#ifdef _MSC_VER
#pragma execution_character_set("utf-8")
#endif

#include "kme.h"
#include "app_settings.h"
#include "debug_headless.h"
#include "touch_input.h"

#include "canvas3D.h"
#include "maploader.h"
#include "numeric_safety.h"
#include "own_track_transition_linkage.h"
#include "repeater_linkage.h"
#include "text_decoder.h"
#include "resource.h"

#include "imgui.h"
#include "imgui_internal.h"
#include "misc/cpp/imgui_stdlib.h"
#include "imgui_impl_dx11.h"
#include "imgui_impl_win32.h"
#include "implot.h"

#include <windows.h>
#if defined(_MSC_VER) && !defined(NDEBUG)
#include <crtdbg.h>
#endif
#include <commdlg.h>
#include <d3d11.h>
#include <shellapi.h>
#include <shlobj.h>
#include <wincodec.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cctype>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <new>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

void App::stop_loader() {
    if (load_state_.worker.joinable()) load_state_.worker.join();
}

void App::handle_loader_start_failure(const std::string& error) {
    load_state_.running = false;
    load_state_.pending_started_at.reset();
    set_program_status("status.map_load_failed");
    add_log(LogSeverity::Error,
            "[ERROR]gui_kme.cpp: failed to start map loader: " + error);
}

void App::poll_loader() {
    std::optional<LoadResult> result;
    {
        std::lock_guard<std::mutex> lock(load_state_.result_mutex);
        if (load_state_.pending_result) {
            result = std::move(load_state_.pending_result);
            load_state_.pending_result.reset();
        }
    }
    if (result) apply_load_result(std::move(*result));
}

bool App::on_frame_presented() {
    if (!edit_ui_operation_pending()) return false;
    if (pending_edit_ui_operation_.progress_rendered) {
        pending_edit_ui_operation_.progress_presented = true;
        pending_edit_ui_operation_.progress_rendered = false;
    }
    return true;
}

void App::begin_load(std::string path, bool preserve_settings, bool record_history,
                     std::optional<BackgroundHistory> background_to_restore,
                     bool preserve_scene_preview_models,
                     bool preserve_scene_preview_camera) {
    if (path.empty() || load_state_.running || edit_ui_operation_pending()) return;
    auto load_started_at = std::chrono::steady_clock::now();

    std::map<std::string, OtherTrack> old_other;
    if (preserve_settings) {
        for (const auto& t : model_.other_tracks) old_other[t.key] = t;
    }

    stop_loader();
    {
        std::lock_guard<std::mutex> lock(log_mutex_);
        logs_.clear();
        ++log_revision_;
        error_count_.store(0, std::memory_order_relaxed);
        warn_count_.store(0, std::memory_order_relaxed);
    }

    // Open-from-scenario support: a BVE Scenario file resolves to the map file
    // its Route entry points at. Everything downstream (window title, history,
    // Save/Reload targets) keeps operating on the resolved map path.
    if (kv_probe_file_kind(path.c_str()) == KV_FILE_KIND_SCENARIO) {
        uint64_t candidate_count = 0;
        const KvScenarioRouteCandidate* candidates =
            kv_resolve_scenario_routes(path.c_str(), &candidate_count);
        if (!candidates || candidate_count == 0) {
            const char* error = kv_get_last_error();
            set_program_status("status.map_load_failed");
            add_log(LogSeverity::Error,
                    std::string("Failed to resolve scenario route: ") +
                        (error && *error ? error : "maploader failed"));
            return;
        }
        std::vector<ScenarioRoutePickItem> items;
        items.reserve(static_cast<size_t>(candidate_count));
        for (uint64_t i = 0; i < candidate_count; ++i) {
            items.push_back(ScenarioRoutePickItem{candidates[i].route_text,
                                                  candidates[i].resolved_path});
        }
        kv_free_scenario_candidates(candidates);
        if (items.size() == 1) {
            add_log("Opened via scenario: " + path);
            add_log("Resolved route: " + items.front().route_text + " -> " +
                    items.front().resolved_path);
            path = items.front().resolved_path;
        } else {
            scenario_route_pick_.popup_requested = true;
            scenario_route_pick_.scenario_path = path;
            scenario_route_pick_.items = std::move(items);
            scenario_route_pick_.selected = 0;
            wake_main_window();
            return;
        }
    }

    load_state_.running = true;
    load_state_.pending_started_at.reset();
    set_program_status("status.map_loading");
    add_log(std::string("Start loading file: ") + path);

    bool has_cp = preserve_settings && has_model_ && model_.has_cp_arb;
    double cp0 = has_cp ? model_.cp_arb[0] : 0.0;
    double cp1 = has_cp ? model_.cp_arb[1] : 0.0;
    double cp2 = has_cp ? model_.cp_arb[2] : 25.0;
    LoadModelOptions load_options;
    load_options.full_edit_registry = false;
    load_options.load_profile = "preview";

    try {
        load_state_.worker = std::thread([this, path, has_cp, cp0, cp1, cp2, old_other, preserve_settings,
                               record_history, background_to_restore, load_started_at,
                               preserve_scene_preview_models,
                               preserve_scene_preview_camera, load_options]() mutable {
            LoadResult result = load_map_worker(path, unit_distance_, has_cp, cp0, cp1, cp2, load_options);
            result.started_at = load_started_at;
            result.preserve_settings = preserve_settings;
            result.record_history = record_history;
            result.preserve_scene_preview_models = preserve_scene_preview_models;
            result.preserve_scene_preview_camera = preserve_scene_preview_camera;
            result.background_to_restore = background_to_restore;
            if (result.ok && preserve_settings) {
                for (auto& t : result.model.other_tracks) {
                    auto it = old_other.find(t.key);
                    if (it != old_other.end()) {
                        t.visible = it->second.visible;
                        t.color = it->second.color;
                        t.range_min = it->second.range_min;
                        t.range_max = it->second.range_max;
                    }
                }
            }
            {
                std::lock_guard<std::mutex> lock(load_state_.result_mutex);
                load_state_.pending_result = std::move(result);
            }
            load_state_.running = false;
            wake_main_window();
        });
    } catch (const std::exception& e) {
        handle_loader_start_failure(e.what());
    }
}

void App::begin_edit_metadata_load() {
    if (!has_model_ || file_path_.empty() || edit_registry_loaded_ || load_state_.running) return;
    auto load_started_at = std::chrono::steady_clock::now();
    const bool has_cp = model_.has_cp_arb;
    const double cp0 = has_cp ? model_.cp_arb[0] : 0.0;
    const double cp1 = has_cp ? model_.cp_arb[1] : 0.0;
    const double cp2 = has_cp ? model_.cp_arb[2] : 25.0;

    stop_loader();
    load_state_.running = true;
    load_state_.pending_started_at.reset();
    set_program_status("status.edit.loading_metadata");
    add_log("[info]gui_kme.cpp: loading edit metadata");

    LoadModelOptions load_options;
    load_options.full_edit_registry = true;
    load_options.load_profile = "edit";
    std::string path = file_path_;

    try {
        load_state_.worker = std::thread([this, path, has_cp, cp0, cp1, cp2, load_started_at, load_options]() mutable {
            LoadResult result = load_map_worker(path, unit_distance_, has_cp, cp0, cp1, cp2, load_options);
            result.started_at = load_started_at;
            result.edit_metadata_only = true;
            {
                std::lock_guard<std::mutex> lock(load_state_.result_mutex);
                load_state_.pending_result = std::move(result);
            }
            load_state_.running = false;
            wake_main_window();
        });
    } catch (const std::exception& e) {
        handle_loader_start_failure(e.what());
    }
}

void App::apply_load_result(LoadResult result) {
    if (result.edit_metadata_only) {
        apply_edit_metadata_result(std::move(result));
        return;
    }
    if (!result.ok) {
        load_state_.pending_started_at.reset();
        pending_scene_preview_started_at_.reset();
        set_program_status("status.map_load_failed");
        add_log(LogSeverity::Error, "Error during loading: " + result.error);
        if (result.handle) kv_free(result.handle);
        return;
    }
    if (handle_) kv_free(handle_);
    handle_ = result.handle;
    model_ = std::move(result.model);
    edit_registry_loaded_ = result.full_edit_registry;
    // A full disk load creates a new maploader identity session. Never keep an
    // inspector request or pending ledger whose stable editIds belong to the
    // replaced handle, including an ordinary same-file Reload with no changes.
    clear_pending_edit_state();
    // A successful disk load starts a new edit batch even when it reloads the
    // same file with no pending ledger.
    distance_resolution_choices_.clear();
    distance_resolution_workflow_ = DistanceResolutionWorkflowState{};
    text_preview_.placement = TextPreviewPlacementState{};
    edit_memory_matches_pending_ledger_ = pending_edit_changes_.empty();
    invalidate_table_cache();
    has_model_ = true;
    rebuild_marker_overlay_cache();
    reset_marker_visibility();
    scene_preview_dirty_ = true;
    scene_preview_preserve_models_on_rebuild_ =
        scene_preview_started_ && result.preserve_scene_preview_models;
    scene_preview_preserve_camera_on_rebuild_ =
        scene_preview_started_ && result.preserve_scene_preview_camera;
    if (!scene_preview_started_ && scene_preview_canvas_) {
        scene_preview_canvas_->clear_scene();
        scene_preview_preserve_models_on_rebuild_ = false;
        scene_preview_preserve_camera_on_rebuild_ = false;
    }
    file_path_ = result.path;
    refresh_text_preview_after_map_load();
    dmin_ = model_.default_min;
    dmax_ = model_.default_max;
    plot_min_ = dmin_;
    plot_max_ = dmax_;
    cp_start_ = model_.cp_arb[0];
    cp_end_ = model_.cp_arb[1];
    cp_interval_ = model_.cp_arb[2];
    if (!result.preserve_settings) {
        plan_view_.fitted = false;
        clear_measure();
    }
    reset_profile_axes_next_ = true;
    reset_radius_axes_next_ = true;
    profile_x_span_ = 0.0;
    radius_x_span_ = 0.0;
    profile_x_zoom_pending_ = false;
    profile_y_zoom_pending_ = false;
    radius_x_zoom_pending_ = false;
    std::ostringstream timing;
    timing << std::fixed << std::setprecision(3)
           << "profile=" << result.load_profile
           << ", maploader=" << result.maploader_seconds << "s"
           << ", model=" << result.model_build_seconds << "s"
           << ", snapshot_build=" << model_.snapshot_build_seconds << "s"
           << ", snapshot_hydrate=" << model_.snapshot_hydrate_seconds << "s"
           << ", buffer copy=" << model_.buffer_copy_seconds << "s";
    add_log("Load timing: " + timing.str());
    for (const std::string& warning : model_.scene_track_key_warnings) add_log(warning);
    add_log("Map loaded: " + result.path);
    set_program_status("status.map_loaded");
    if (result.background_to_restore) {
        apply_background_history(*result.background_to_restore);
    } else if (!result.preserve_settings) {
        clear_background_image();
    }
    if (result.record_history) touch_recent_map(result.path);
    if (scene_auto_load_on_map_open_ && !scene_preview_started_) {
        start_scene_preview();
    }
    load_state_.pending_started_at = result.started_at;
    if (!show_plots_window_) finish_pending_load_timing_after_plan_data_ready();
    if (edit_mode_enabled_ && has_model_ && !file_path_.empty() &&
        !edit_registry_loaded_ && !load_state_.running) {
        begin_edit_metadata_load();
    }
}

bool source_file_hashes_match(const MapModel& current, const MapModel& edit_model,
                              std::string& error) {
    std::map<std::string, std::string> current_hashes;
    for (const EditSourceFileInfo& file : current.edit_files) {
        current_hashes[file.file_path] = file.source_hash;
    }
    for (const EditSourceFileInfo& file : edit_model.edit_files) {
        auto it = current_hashes.find(file.file_path);
        if (it == current_hashes.end()) {
            error = "edit metadata includes a source file that is not in the current preview: " + file.file_path;
            return false;
        }
        if (!it->second.empty() && !file.source_hash.empty() && it->second != file.source_hash) {
            error = "source file changed since preview load: " + file.file_path;
            return false;
        }
    }
    return true;
}

bool table_row_count_matches(const char* label,
                             const std::vector<TableRow>& current,
                             const std::vector<TableRow>& edit_rows,
                             std::string& error) {
    if (current.size() == edit_rows.size()) return true;
    error = std::string("edit metadata row count mismatch for ") + label +
        ": preview=" + std::to_string(current.size()) +
        ", edit=" + std::to_string(edit_rows.size());
    return false;
}

bool edit_metadata_row_counts_match(const MapModel& current, const MapModel& edit_model,
                                    std::string& error) {
    return table_row_count_matches("curve", current.curve_rows, edit_model.curve_rows, error) &&
        table_row_count_matches("gradient", current.gradient_rows, edit_model.gradient_rows, error) &&
        table_row_count_matches("otherTrack.change", current.other_track_changes,
                                edit_model.other_track_changes, error) &&
        table_row_count_matches("station.put", current.station_list_rows, edit_model.station_list_rows, error) &&
        table_row_count_matches("station.list", current.station_definition_rows, edit_model.station_definition_rows, error) &&
        table_row_count_matches("structure.put", current.structures, edit_model.structures, error) &&
        table_row_count_matches("structure.between", current.structures_between, edit_model.structures_between, error) &&
        table_row_count_matches("structure.model", current.structure_models, edit_model.structure_models, error) &&
        table_row_count_matches("otherTrain.definition", current.other_trains, edit_model.other_trains, error) &&
        table_row_count_matches("otherTrain.stop", current.other_train_stops, edit_model.other_train_stops, error) &&
        table_row_count_matches("otherTrain.structureKey", current.other_train_structure_keys, edit_model.other_train_structure_keys, error) &&
        table_row_count_matches("otherTrain.sound3DKey", current.other_train_sound_3d_keys, edit_model.other_train_sound_3d_keys, error) &&
        table_row_count_matches("signal.aspect", current.signal_aspects, edit_model.signal_aspects, error) &&
        table_row_count_matches("signal.put", current.signals, edit_model.signals, error) &&
        table_row_count_matches("beacon.put", current.beacons, edit_model.beacons, error) &&
        table_row_count_matches("preTrain.pass", current.pretrains, edit_model.pretrains, error) &&
        table_row_count_matches("sound.list", current.sound_list, edit_model.sound_list, error) &&
        table_row_count_matches("sound3D.list", current.sound_3d_list,
                                edit_model.sound_3d_list, error) &&
        table_row_count_matches("repeater", current.repeaters, edit_model.repeaters, error) &&
        table_row_count_matches("irregularity.change", current.irregularities, edit_model.irregularities, error) &&
        table_row_count_matches("mapSound.play", current.map_sounds, edit_model.map_sounds, error) &&
        table_row_count_matches("mapSound3D.put", current.map_sound_3d, edit_model.map_sound_3d, error) &&
        table_row_count_matches("rollingNoise.change", current.rolling_noises, edit_model.rolling_noises, error) &&
        table_row_count_matches("flangeNoise.change", current.flange_noises, edit_model.flange_noises, error) &&
        table_row_count_matches("jointNoise.play", current.joint_noises, edit_model.joint_noises, error) &&
        table_row_count_matches("background.change", current.backgrounds, edit_model.backgrounds, error) &&
        table_row_count_matches("adhesion.change", current.adhesions, edit_model.adhesions, error) &&
        table_row_count_matches("cabIlluminance.change", current.cab_illuminance, edit_model.cab_illuminance, error) &&
        table_row_count_matches("fog.change", current.fogs, edit_model.fogs, error) &&
        table_row_count_matches("drawDistance.change", current.draw_distances,
                                edit_model.draw_distances, error) &&
        table_row_count_matches("speedlimit", current.speed_limit_rows,
                                edit_model.speed_limit_rows, error) &&
        table_row_count_matches("section.begin", current.section_begins,
                                edit_model.section_begins, error) &&
        table_row_count_matches("section.speedLimit", current.section_speed_limits,
                                edit_model.section_speed_limits, error);
}

void merge_table_row_edit_metadata(std::vector<TableRow>& current,
                                   const std::vector<TableRow>& edit_rows) {
    for (size_t i = 0; i < current.size(); ++i) {
        current[i].edit_id = edit_rows[i].edit_id;
        current[i].source = edit_rows[i].source;
    }
}

void merge_edit_metadata(MapModel& current, MapModel&& edit_model) {
    current.edit_files = std::move(edit_model.edit_files);
    current.edit_statements = std::move(edit_model.edit_statements);
    current.edit_elements = std::move(edit_model.edit_elements);
    for (size_t i = 0; i < current.resource_list_sources.size(); ++i) {
        current.resource_list_sources[i].edit_id =
            edit_model.resource_list_sources[i].edit_id;
    }
    merge_table_row_edit_metadata(current.curve_rows, edit_model.curve_rows);
    merge_table_row_edit_metadata(current.gradient_rows, edit_model.gradient_rows);
    merge_table_row_edit_metadata(current.other_track_changes,
                                  edit_model.other_track_changes);
    annotate_own_track_transition_links(current);
    merge_table_row_edit_metadata(current.station_list_rows, edit_model.station_list_rows);
    merge_table_row_edit_metadata(current.station_definition_rows, edit_model.station_definition_rows);
    merge_table_row_edit_metadata(current.structures, edit_model.structures);
    merge_table_row_edit_metadata(current.structures_between, edit_model.structures_between);
    merge_table_row_edit_metadata(current.structure_models, edit_model.structure_models);
    merge_table_row_edit_metadata(current.other_trains, edit_model.other_trains);
    merge_table_row_edit_metadata(current.other_train_stops, edit_model.other_train_stops);
    merge_table_row_edit_metadata(current.other_train_structure_keys, edit_model.other_train_structure_keys);
    merge_table_row_edit_metadata(current.other_train_sound_3d_keys, edit_model.other_train_sound_3d_keys);
    merge_table_row_edit_metadata(
        current.signal_aspects, edit_model.signal_aspects);
    merge_table_row_edit_metadata(current.signals, edit_model.signals);
    merge_table_row_edit_metadata(current.beacons, edit_model.beacons);
    merge_table_row_edit_metadata(current.pretrains, edit_model.pretrains);
    merge_table_row_edit_metadata(current.sound_list, edit_model.sound_list);
    merge_table_row_edit_metadata(current.sound_3d_list, edit_model.sound_3d_list);
    merge_table_row_edit_metadata(current.repeaters, edit_model.repeaters);
    merge_table_row_edit_metadata(current.irregularities, edit_model.irregularities);
    merge_table_row_edit_metadata(current.map_sounds, edit_model.map_sounds);
    merge_table_row_edit_metadata(current.map_sound_3d, edit_model.map_sound_3d);
    merge_table_row_edit_metadata(current.rolling_noises, edit_model.rolling_noises);
    merge_table_row_edit_metadata(current.flange_noises, edit_model.flange_noises);
    merge_table_row_edit_metadata(current.joint_noises, edit_model.joint_noises);
    merge_table_row_edit_metadata(current.backgrounds, edit_model.backgrounds);
    merge_table_row_edit_metadata(current.adhesions, edit_model.adhesions);
    merge_table_row_edit_metadata(current.cab_illuminance, edit_model.cab_illuminance);
    merge_table_row_edit_metadata(current.fogs, edit_model.fogs);
    merge_table_row_edit_metadata(current.draw_distances, edit_model.draw_distances);
    merge_table_row_edit_metadata(current.speed_limit_rows,
                                  edit_model.speed_limit_rows);
    merge_table_row_edit_metadata(current.section_begins, edit_model.section_begins);
    merge_table_row_edit_metadata(current.section_speed_limits,
                                  edit_model.section_speed_limits);
    bind_station_position_edit_ids(current);
}

void App::apply_edit_metadata_result(LoadResult result) {
    if (!result.ok) {
        set_program_status("status.map_loaded");
        add_log("[error]gui_kme.cpp: edit metadata load failed: " + result.error);
        if (result.handle) kv_free(result.handle);
        return;
    }
    std::string error;
    if (!has_model_ || result.path != file_path_) {
        error = "edit metadata entry path no longer matches the current preview";
    } else if (!source_file_hashes_match(model_, result.model, error)) {
        // error set by helper
    } else if (!edit_metadata_row_counts_match(model_, result.model, error)) {
        // error set by helper
    }
    if (!error.empty()) {
        set_program_status("status.map_loaded");
        add_log("[warn]gui_kme.cpp: edit metadata discarded: " + error);
        add_log("[warn]gui_kme.cpp: reload from disk before editing this map");
        if (result.handle) kv_free(result.handle);
        return;
    }

    if (handle_) kv_free(handle_);
    handle_ = result.handle;
    result.handle = nullptr;
    distance_resolution_choices_.clear();
    distance_resolution_workflow_ = DistanceResolutionWorkflowState{};
    text_preview_.placement = TextPreviewPlacementState{};
    edit_memory_matches_pending_ledger_ = pending_edit_changes_.empty();
    merge_edit_metadata(model_, std::move(result.model));
    edit_registry_loaded_ = true;
    invalidate_table_cache();
    rebuild_marker_overlay_cache();
    sync_marker_visibility_sizes();
    if (scene_preview_started_ && scene_preview_canvas_) {
        std::string scene_error;
        if (!scene_preview_canvas_->refresh_scene_dynamic_content(model_, station_jump_index_, scene_error)) {
            add_log("[warn]gui_kme.cpp: 3D scene dynamic metadata refresh failed, scheduling full rebuild: " +
                    (scene_error.empty() ? std::string("unknown error") : scene_error));
            scene_preview_dirty_ = true;
            scene_preview_preserve_models_on_rebuild_ = true;
            scene_preview_preserve_camera_on_rebuild_ = true;
        } else if (!scene_preview_canvas_->refresh_scene_route_stations(model_, scene_error)) {
            add_log("[warn]gui_kme.cpp: 3D scene marker metadata refresh failed, scheduling full rebuild: " +
                    (scene_error.empty() ? std::string("unknown error") : scene_error));
            scene_preview_dirty_ = true;
            scene_preview_preserve_models_on_rebuild_ = true;
            scene_preview_preserve_camera_on_rebuild_ = true;
        }
    }
    add_log("[info]gui_kme.cpp: edit metadata loaded");
    set_program_status(edit_mode_enabled_ ? "status.edit.mode_enabled" : "status.map_loaded");
}

void App::finish_pending_load_timing(std::chrono::steady_clock::time_point finished_at) {
    if (!load_state_.pending_started_at) return;

    double elapsed_seconds = std::chrono::duration<double>(
        finished_at - *load_state_.pending_started_at).count();
    load_state_.pending_started_at.reset();

    const std::string elapsed = format_elapsed_seconds_value(elapsed_seconds);
    add_log("Map loaded in " + elapsed + "s");
    set_program_status("status.map_loaded", elapsed);
}

void App::regenerate_geometry() {
    if (!handle_ || load_state_.running) return;
    if (!kv_generate_geometry(handle_, unit_distance_, 1, cp_start_, cp_end_, cp_interval_)) {
        const char* err = kv_get_last_error();
        add_log(std::string("[ERROR]") + (err ? err : "geometry failed"));
        return;
    }
    std::map<std::string, OtherTrack> old_other;
    for (const auto& t : model_.other_tracks) old_other[t.key] = t;
    try {
        LoadModelOptions options;
        options.full_edit_registry = edit_registry_loaded_;
        MapModel updated = build_model_from_handle(handle_, file_path_, options);
        for (auto& t : updated.other_tracks) {
            auto it = old_other.find(t.key);
            if (it != old_other.end()) {
                t.visible = it->second.visible;
                t.color = it->second.color;
                t.range_min = it->second.range_min;
                t.range_max = it->second.range_max;
            }
        }
        model_ = std::move(updated);
        edit_registry_loaded_ = options.full_edit_registry;
        invalidate_table_cache();
        rebuild_marker_overlay_cache();
        sync_marker_visibility_sizes();
        scene_preview_dirty_ = true;
        model_.has_cp_arb = true;
        model_.cp_arb[0] = cp_start_;
        model_.cp_arb[1] = cp_end_;
        model_.cp_arb[2] = cp_interval_;
        dmin_ = plot_min_;
        dmax_ = plot_max_;
        reset_profile_axes_next_ = true;
        reset_radius_axes_next_ = true;
        profile_x_span_ = 0.0;
        radius_x_span_ = 0.0;
        profile_x_zoom_pending_ = false;
        profile_y_zoom_pending_ = false;
        radius_x_zoom_pending_ = false;
        for (const std::string& warning : model_.scene_track_key_warnings) add_log(warning);
        add_log("Geometry regenerated by maploader");
    } catch (const std::exception& e) {
        add_log(std::string("[ERROR]") + e.what());
    }
}

App::LoadResult App::load_map_worker(std::string path, double unit_distance, bool has_cp, double cp_start, double cp_end, double cp_step) {
    return load_map_worker(std::move(path), unit_distance, has_cp, cp_start, cp_end, cp_step, LoadModelOptions{});
}

App::LoadResult App::load_map_worker(std::string path, double unit_distance, bool has_cp, double cp_start, double cp_end, double cp_step,
                                      LoadModelOptions options) {
    if (options.full_edit_registry) options.load_profile = "edit";
    if (options.load_profile.empty()) options.load_profile = "preview";
    auto started_at = std::chrono::steady_clock::now();
    auto elapsed_seconds = [&]() {
        return std::chrono::duration<double>(std::chrono::steady_clock::now() - started_at).count();
    };
    LoadResult out;
    out.path = path;
    out.full_edit_registry = options.full_edit_registry;
    out.load_profile = options.load_profile;
    auto maploader_started_at = std::chrono::steady_clock::now();
    unsigned load_flags = options.full_edit_registry ? KV_LOAD_EDIT_METADATA : KV_LOAD_PREVIEW;
    void* handle = kv_load_map_ex(path.c_str(), unit_distance, load_flags);
    auto maploader_finished_at = std::chrono::steady_clock::now();
    out.maploader_seconds = std::chrono::duration<double>(maploader_finished_at - maploader_started_at).count();
    if (!handle) {
        const char* err = kv_get_last_error();
        out.error = err ? err : "maploader failed";
        out.elapsed_seconds = elapsed_seconds();
        return out;
    }
    if (has_cp) {
        if (!kv_generate_geometry(handle, unit_distance, 1, cp_start, cp_end, cp_step)) {
            const char* err = kv_get_last_error();
            out.error = err ? err : "geometry failed";
            out.elapsed_seconds = elapsed_seconds();
            kv_free(handle);
            return out;
        }
    }
    try {
        auto model_started_at = std::chrono::steady_clock::now();
        out.model = build_model_from_handle(handle, path, options);
        out.model_build_seconds = std::chrono::duration<double>(
            std::chrono::steady_clock::now() - model_started_at).count();
        if (has_cp) {
            out.model.has_cp_arb = true;
            out.model.cp_arb[0] = cp_start;
            out.model.cp_arb[1] = cp_end;
            out.model.cp_arb[2] = cp_step;
        }
        out.handle = handle;
        out.ok = true;
        out.elapsed_seconds = elapsed_seconds();
    } catch (const std::exception& e) {
        out.error = e.what();
        out.elapsed_seconds = elapsed_seconds();
        kv_free(handle);
    }
    return out;
}

MapModel App::build_model_from_handle(void* handle, const std::string& path,
                                      LoadModelOptions options) {
    KvMapSnapshot snapshot{};
    const auto snapshot_started_at = std::chrono::steady_clock::now();
    if (!kv_get_map_snapshot(handle, KV_MAP_SNAPSHOT_VERSION,
                             &snapshot, sizeof(snapshot))) {
        const char* error = kv_get_last_error();
        throw std::runtime_error(std::string("kv_get_map_snapshot failed") +
            (error && *error ? ": " + std::string(error) : std::string{}));
    }
    if (snapshot.version != KV_MAP_SNAPSHOT_VERSION ||
        snapshot.structure_size < sizeof(KvMapSnapshot)) {
        throw std::runtime_error("map snapshot version or structure size mismatch");
    }
    if (options.full_edit_registry &&
        (snapshot.capabilities & KV_MAP_CAP_EDIT_METADATA) == 0) {
        throw std::runtime_error("map snapshot does not contain edit metadata");
    }
    const double snapshot_call_seconds = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - snapshot_started_at).count();
    return hydrate_map_snapshot(snapshot, path, snapshot_call_seconds);

}
