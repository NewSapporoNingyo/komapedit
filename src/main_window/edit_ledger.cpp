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

bool App::edit_ui_operation_pending() const {
    return pending_edit_ui_operation_.operation != PendingEditUiOperation::None;
}

void App::request_edit_ui_operation(PendingEditUiOperation operation) {
    if (operation == PendingEditUiOperation::None || edit_ui_operation_pending()) return;

    const bool apply_operation = operation == PendingEditUiOperation::ApplyInspector ||
        operation == PendingEditUiOperation::ApplyNewElement ||
        operation == PendingEditUiOperation::ApplyOtherTrackRename;
    if (apply_operation && !edit_actions_available()) return;
    if (operation == PendingEditUiOperation::ApplyInspector &&
        (!inspector_.open || inspector_.edit_id.empty())) {
        return;
    }
    if (operation == PendingEditUiOperation::ApplyNewElement && !new_element_wizard_.open) {
        return;
    }
    if (operation == PendingEditUiOperation::ApplyOtherTrackRename &&
        (other_track_rename_.source_key.empty() || other_track_rename_.apply_key.empty())) {
        return;
    }
    if (!apply_operation &&
        (!edit_actions_available() || load_state_.running || !has_unsaved_edit_state())) {
        return;
    }

    PendingEditUiOperationState pending;
    pending.operation = operation;
    pending.progress_status_key = apply_operation
        ? "status.edit.applying"
        : (operation == PendingEditUiOperation::Revert ||
           operation == PendingEditUiOperation::DiscardAndResolveClose)
            ? "status.edit.reverting"
            : "status.edit.saving";
    pending.previous_status_key = program_status_key_;
    pending.previous_status_elapsed_suffix = program_status_elapsed_suffix_;
    pending_edit_ui_operation_ = std::move(pending);
    set_program_status(pending_edit_ui_operation_.progress_status_key);
    wake_main_window();
}

void App::process_pending_edit_ui_operation() {
    if (!edit_ui_operation_pending() ||
        !pending_edit_ui_operation_.progress_presented) {
        return;
    }

    PendingEditUiOperationState pending = std::move(pending_edit_ui_operation_);
    pending_edit_ui_operation_ = PendingEditUiOperationState{};

    switch (pending.operation) {
    case PendingEditUiOperation::ApplyInspector:
        apply_inspector_changes();
        break;
    case PendingEditUiOperation::ApplyNewElement:
        apply_new_element_insert();
        break;
    case PendingEditUiOperation::ApplyOtherTrackRename:
        apply_other_track_rename();
        break;
    case PendingEditUiOperation::Save:
        save_pending_edits();
        break;
    case PendingEditUiOperation::SaveAndResolveClose:
        if (save_pending_edits(false)) finish_pending_close_action();
        break;
    case PendingEditUiOperation::Revert:
        revert_all_pending_edits();
        break;
    case PendingEditUiOperation::DiscardAndResolveClose:
        if (pending_close_action_ != PendingCloseAction::DisableEditMode ||
            discard_pending_edits()) {
            finish_pending_close_action();
        }
        break;
    case PendingEditUiOperation::None:
        break;
    }

    if (std::string_view(program_status_key_) == pending.progress_status_key) {
        program_status_key_ = pending.previous_status_key;
        program_status_elapsed_suffix_ =
            std::move(pending.previous_status_elapsed_suffix);
    }
}

void App::clear_pending_edit_state() {
    clear_scene_placement_edit_target();
    pending_edit_ui_operation_ = PendingEditUiOperationState{};
    pending_edit_changes_.clear();
    edit_memory_matches_pending_ledger_ = true;
    original_edit_rows_.clear();
    distance_resolution_choices_.clear();
    distance_resolution_workflow_ = DistanceResolutionWorkflowState{};
    text_preview_.placement = TextPreviewPlacementState{};
    inspector_ = MapElementInspectorState{};
    pending_inspector_request_.reset();
    pending_delete_request_.reset();
    pending_other_track_rename_request_.reset();
    other_track_rename_ = OtherTrackRenameState{};
    new_element_wizard_ = NewElementWizardState{};
    discard_all_editable_list_drafts();
}

bool App::has_pending_edits() const {
    return !pending_edit_changes_.empty();
}

bool App::has_unsaved_edit_state() const {
    return has_pending_edits() || has_unapplied_editable_list_drafts();
}

bool App::row_has_pending_edit(const std::string& edit_id) const {
    return !edit_id.empty() && pending_edit_changes_.find(edit_id) != pending_edit_changes_.end();
}

bool App::row_is_pending_delete(const std::string& edit_id) const {
    auto it = pending_edit_changes_.find(edit_id);
    return it != pending_edit_changes_.end() && it->second.operation == "delete";
}

bool App::edit_actions_available() const {
    return edit_mode_enabled_ && edit_registry_loaded_ && handle_ && has_model_ && !load_state_.running;
}

void App::set_edit_mode_enabled(bool enabled) {
    if (enabled == edit_mode_enabled_) return;
    if (enabled && !settings_.edit_mode_warning_suppressed) {
        edit_mode_warning_dont_show_ = false;
        popups_.edit_mode_warning = true;
        wake_main_window();
        return;
    }
    if (!enabled && has_unsaved_edit_state()) {
        request_close_action(PendingCloseAction::DisableEditMode);
        return;
    }

    apply_edit_mode_enabled(enabled);
}

void App::apply_edit_mode_enabled(bool enabled) {
    edit_mode_enabled_ = enabled;
    settings_.edit_mode_enabled = edit_mode_enabled_;
    save_user_settings(settings_);

    if (!edit_mode_enabled_) {
        clear_scene_placement_edit_target();
        inspector_.open = false;
        pending_inspector_request_.reset();
        distance_resolution_choices_.clear();
        distance_resolution_workflow_ = DistanceResolutionWorkflowState{};
        text_preview_.placement = TextPreviewPlacementState{};
        discard_all_editable_list_drafts();
        edit_memory_matches_pending_ledger_ = pending_edit_changes_.empty();
        set_program_status("status.edit.mode_disabled");
        add_log("[info]gui_kme.cpp: edit mode disabled");
        return;
    }

    set_program_status("status.edit.mode_enabled");
    add_log("[info]gui_kme.cpp: edit mode enabled");
    if (has_model_ && !file_path_.empty() && !edit_registry_loaded_ && !load_state_.running) {
        begin_edit_metadata_load();
    }
}

void App::request_close_action(PendingCloseAction action) {
    if (action == PendingCloseAction::None || edit_ui_operation_pending()) return;
    if (has_unsaved_edit_state()) {
        pending_close_action_ = action;
        popups_.close_unsaved_confirm = true;
        wake_main_window();
        return;
    }

    if (action == PendingCloseAction::DisableEditMode) {
        apply_edit_mode_enabled(false);
    } else if (action == PendingCloseAction::ExitApplication) {
        PostQuitMessage(0);
    }
}

void App::request_exit() {
    request_close_action(PendingCloseAction::ExitApplication);
}

bool App::snapshot_local_preview_row(const std::string& edit_id, const std::string& row_kind) {
    if (edit_id.empty() || row_kind.empty()) return false;
    if (original_edit_rows_.find(edit_id) != original_edit_rows_.end()) return true;
    std::vector<TableRow>* rows = inspector_rows_for_kind(model_, row_kind);
    if (!rows) return false;
    size_t row_index = 0;
    if (!find_row_index_by_edit_id(*rows, edit_id, row_index)) return false;
    original_edit_rows_[edit_id] = MapElementPreviewSnapshot{row_kind, (*rows)[row_index], row_index};
    return true;
}

bool App::restore_local_preview_change(const std::string& edit_id, const std::string& row_kind,
                                       bool refresh_preview) {
    auto snapshot = original_edit_rows_.find(edit_id);
    if (snapshot == original_edit_rows_.end()) return true;
    const std::string effective_row_kind = row_kind.empty() ? snapshot->second.row_kind : row_kind;
    std::vector<TableRow>* rows = inspector_rows_for_kind(model_, effective_row_kind);
    if (!rows) return false;

    size_t row_index = 0;
    if (find_row_index_by_edit_id(*rows, edit_id, row_index)) {
        (*rows)[row_index] = snapshot->second.row;
    } else {
        size_t insert_index = std::min(snapshot->second.row_index, rows->size());
        rows->insert(rows->begin() + static_cast<std::ptrdiff_t>(insert_index), snapshot->second.row);
    }
    bool requires_full_scene_refresh = false;
    auto pending = pending_edit_changes_.find(edit_id);
    if (pending != pending_edit_changes_.end()) {
        requires_full_scene_refresh =
            (effective_row_kind == "structure.put" &&
             pending->second.field_changes.find("structureKey") !=
                 pending->second.field_changes.end()) ||
            (effective_row_kind == "signal.put" &&
             pending->second.field_changes.find("signalAspectKey") !=
                 pending->second.field_changes.end());
    }
    original_edit_rows_.erase(snapshot);
    if (refresh_preview) {
        refresh_local_preview_after_edit(
            effective_row_kind, requires_full_scene_refresh ? std::string{} : edit_id);
    }
    return true;
}

bool App::apply_local_preview_change(const MapElementPendingChange& change,
                                     bool refresh_preview) {
    if (change.edit_id.empty() || change.row_kind.empty()) return false;
    std::vector<TableRow>* rows = inspector_rows_for_kind(model_, change.row_kind);
    if (!rows) return false;

    size_t row_index = 0;
    const bool row_found = find_row_index_by_edit_id(*rows, change.edit_id, row_index);
    if (!row_found && change.operation != "delete") return false;
    if (row_found && !snapshot_local_preview_row(change.edit_id, change.row_kind)) return false;

    if (change.operation == "delete") {
        if (row_found) rows->erase(rows->begin() + static_cast<std::ptrdiff_t>(row_index));
        if (refresh_preview) refresh_local_preview_after_edit(change.row_kind);
        return true;
    }

    if (change.operation != "update" || !row_found) return false;
    TableRow& row = (*rows)[row_index];
    for (const auto& field : change.field_changes) {
        set_inspector_row_field_value(
            row, change.row_kind, field.first, field.second,
            model_.distance_origin);
    }
    if ((change.row_kind == "structure.model" ||
         change.row_kind == "sound.list" ||
         change.row_kind == "sound3D.list") &&
        change.field_changes.find("filePath") != change.field_changes.end()) {
        row.cells["resolvedFilePath"] = resolve_list_asset_path(
            row.source.file_path, table_cell(row, "filePath"));
    }
    const bool requires_full_scene_refresh =
        (change.row_kind == "structure.put" &&
         change.field_changes.find("structureKey") != change.field_changes.end()) ||
        (change.row_kind == "signal.put" &&
         change.field_changes.find("signalAspectKey") != change.field_changes.end());
    if (refresh_preview) {
        refresh_local_preview_after_edit(
            change.row_kind, requires_full_scene_refresh ? std::string{} : change.edit_id);
    }
    return true;
}

bool map_element_inspector_field_forced_read_only(
    std::string_view row_kind, std::string_view field_key) noexcept {
    return row_kind == "otherTrack.change" && field_key == "trackKey";
}

std::string structure_model_path_for_key(const MapModel& model,
                                         const std::string& structure_key) {
    if (structure_key.empty()) return {};
    for (const TableRow& row : model.structure_models) {
        if (resource_key_values_equal(
                table_cell(row, "structureKey"), structure_key)) {
            return table_cell(row, "resolvedFilePath");
        }
    }
    return {};
}

Canvas3DPlacementEditTarget scene_edit_target_from_row(
    const TableRow& row, Canvas3DSceneEditKind kind, std::string model_path) {
    Canvas3DPlacementEditTarget target;
    target.kind = kind;
    target.edit_id = row.edit_id;
    target.model_path = std::move(model_path);
    target.track_key = table_cell(row, "trackKey");
    target.put_between_track_key1 = table_cell(row, "trackKey1");
    target.put_between_track_key2 = table_cell(row, "trackKey2");
    target.put_between_flag =
        kme::truncating_int_or_zero(table_cell_number(row, "flag")) & 1;
    target.distance = table_cell_number(row, "distance");
    target.x = table_cell_number(row, "x");
    target.y = table_cell_number(row, "y");
    target.z = table_cell_number(row, "z");
    target.rx = table_cell_number(row, "rx");
    target.ry = table_cell_number(row, "ry");
    target.rz = table_cell_number(row, "rz");
    target.tilt = table_cell_number(row, "tilt");
    target.span = table_cell_number(row, "span");
    return target;
}

bool App::update_scene_placement_instance_from_model(const std::string& edit_id,
                                                     const std::string& row_kind) {
    if (!scene_preview_started_ || !scene_preview_canvas_) return true;
    const bool signal = row_kind == "signal.put";
    const bool put_between = row_kind == "structure.between";
    const std::vector<TableRow>& rows = signal ? model_.signals
        : put_between ? model_.structures_between : model_.structures;
    size_t row_index = 0;
    if (!find_row_index_by_edit_id(rows, edit_id, row_index)) return false;
    std::string model_path;
    if (put_between) {
        model_path = structure_model_path_for_key(
            model_, table_cell(rows[row_index], "structureKey"));
        if (model_path.empty()) return false;
    }
    return scene_preview_canvas_->update_scene_placement_instance(
        scene_edit_target_from_row(
            rows[row_index],
            signal ? Canvas3DSceneEditKind::Signal
                : put_between ? Canvas3DSceneEditKind::StructurePutBetween
                              : Canvas3DSceneEditKind::Structure,
            std::move(model_path)));
}

bool App::update_scene_repeater_segment_from_model(const std::string& edit_id) {
    if (!scene_preview_started_ || !scene_preview_canvas_) return true;
    size_t row_index = 0;
    if (!find_row_index_by_edit_id(model_.repeaters, edit_id, row_index)) return false;
    return scene_preview_canvas_->update_scene_repeater_segment(
        scene_edit_target_from_row(model_.repeaters[row_index],
                                   Canvas3DSceneEditKind::Repeater));
}

void App::refresh_local_preview_after_edit(const std::string& row_kind,
                                           const std::string& edit_id) {
    if (row_kind == "station.put" || row_kind == "station.list") {
        normalize_station_preview_rows(model_);
    }
    if (row_kind == "cabIlluminance.change") {
        normalize_cab_illuminance_preview_rows(model_);
    }
    if (row_kind == "speedlimit") rebuild_speed_limit_runtime_cache(model_);
    const bool alignment_changed = row_kind == "curve" || row_kind == "gradient" ||
        row_kind == "otherTrack.change";
    if (row_kind == "include" && scene_preview_started_ && scene_preview_canvas_) {
        scene_preview_dirty_ = true;
        scene_preview_preserve_models_on_rebuild_ = true;
        scene_preview_preserve_camera_on_rebuild_ = true;
    }
    if (alignment_changed && scene_preview_started_ && scene_preview_canvas_) {
        scene_preview_dirty_ = true;
        scene_preview_preserve_models_on_rebuild_ = true;
        scene_preview_preserve_camera_on_rebuild_ = true;
    }
    Canvas3DSceneMapRefreshOptions map_refresh;
    map_refresh.route_stations = row_kind == "station.put" || row_kind == "station.list";
    static constexpr std::array<const char*, 20> k_marker_row_kinds = {
        "station.put", "station.list", "irregularity.change", "beacon.put",
        "mapSound.play", "mapSound3D.put", "rollingNoise.change",
        "flangeNoise.change", "jointNoise.play", "background.change",
        "adhesion.change", "cabIlluminance.change", "fog.change",
        "drawDistance.change", "speedlimit", "section.begin",
        "section.speedLimit", "curve", "gradient",
        "otherTrack.change",
    };
    map_refresh.markers = std::any_of(
        k_marker_row_kinds.begin(), k_marker_row_kinds.end(),
        [&](const char* candidate) { return row_kind == candidate; });
    map_refresh.fog = row_kind == "fog.change";
    map_refresh.draw_distances = row_kind == "drawDistance.change";
    map_refresh.speed_limits = row_kind == "speedlimit";
    map_refresh.section_signals = row_kind == "section.begin" ||
        row_kind == "section.speedLimit";
    if ((map_refresh.route_stations || map_refresh.markers || map_refresh.fog ||
         map_refresh.draw_distances || map_refresh.speed_limits ||
         map_refresh.section_signals) &&
        scene_preview_started_ && scene_preview_canvas_) {
        std::string error;
        if (!scene_preview_canvas_->refresh_scene_map_content(model_, map_refresh, error)) {
            add_log("[warn]gui_kme.cpp: 3D scene marker refresh failed, scheduling full rebuild: " +
                    (error.empty() ? std::string("unknown error") : error));
            scene_preview_dirty_ = true;
            scene_preview_preserve_models_on_rebuild_ = true;
            scene_preview_preserve_camera_on_rebuild_ = true;
        }
    }
    if (row_kind == "speedlimit") {
        refresh_speed_limit_table_cache();
        rebuild_speed_limit_marker_overlay_cache();
    } else {
        invalidate_table_cache();
        rebuild_marker_overlay_cache();
    }
    sync_marker_visibility_sizes();

    bool placement_instance_synced = false;
    if ((row_kind == "structure.put" || row_kind == "structure.between" ||
         row_kind == "signal.put") && !edit_id.empty()) {
        placement_instance_synced =
            update_scene_placement_instance_from_model(edit_id, row_kind);
    }
    bool repeater_segment_synced = false;
    if (row_kind == "repeater" && !edit_id.empty()) {
        const auto pending = pending_edit_changes_.find(edit_id);
        const bool position_only = pending != pending_edit_changes_.end() &&
            !pending->second.field_changes.empty() &&
            std::all_of(pending->second.field_changes.begin(),
                        pending->second.field_changes.end(),
                        [](const auto& field) {
                            return field.first == "x" || field.first == "y" || field.first == "z";
                        });
        if (position_only) {
            repeater_segment_synced = update_scene_repeater_segment_from_model(edit_id);
        }
    }
    const bool affects_scene_dynamic =
        ((row_kind == "structure.put" || row_kind == "structure.between" ||
          row_kind == "signal.put") &&
         !placement_instance_synced) ||
        row_kind == "structure.model" ||
        (row_kind == "repeater" && !repeater_segment_synced) ||
        row_kind == "background.change";
    if (affects_scene_dynamic && scene_preview_started_ && scene_preview_canvas_) {
        std::string error;
        if (!scene_preview_canvas_->refresh_scene_dynamic_content(model_, station_jump_index_, error)) {
            add_log("[warn]gui_kme.cpp: 3D scene dynamic refresh failed, scheduling full rebuild: " +
                    (error.empty() ? std::string("unknown error") : error));
            scene_preview_dirty_ = true;
            scene_preview_preserve_models_on_rebuild_ = true;
            scene_preview_preserve_camera_on_rebuild_ = true;
        }
    }
}

void normalize_station_preview_rows(MapModel& model) {
    std::stable_sort(model.station_list_rows.begin(), model.station_list_rows.end(),
                     [](const TableRow& a, const TableRow& b) {
                         double da = table_cell_number(a, "_distance");
                         double db = table_cell_number(b, "_distance");
                         if (da != db) return da < db;
                         return table_cell_number(a, "_order") < table_cell_number(b, "_order");
                     });

    model.station_names.clear();
    for (const TableRow& definition : model.station_definition_rows) {
        const std::string key = ascii_lower(table_cell(definition, "stationKey"));
        if (!key.empty()) {
            model.station_names[key] = table_cell(definition, "stationName");
        }
    }
    model.station_positions.clear();
    model.stations.clear();
    std::set<std::string> seen;
    size_t own_row = 0;
    for (size_t i = 0; i < model.station_list_rows.size(); ++i) {
        TableRow& row = model.station_list_rows[i];
        double distance = table_cell_number(row, "_distance");
        row.cells["rowNumber"] = std::to_string(i + 1);
        row.cells["dist"] = format_double(distance - model.distance_origin, 0);

        std::string key = table_cell(row, "posKey");
        if (key.empty()) continue;
        Station station;
        station.key = key;
        auto station_name = model.station_names.find(ascii_lower(key));
        station.name = station_name == model.station_names.end() ? key : station_name->second;
        if (station.name.empty()) station.name = key;
        row.cells["stationName"] = station.name;
        station.distance = distance;
        station.mileage = distance - model.distance_origin;
        if (!model.own.empty()) {
            while (own_row + 1 < model.own.rows && model.own.at(own_row, 0) < distance) ++own_row;
            if (own_row >= model.own.rows) own_row = model.own.rows - 1;
            station.x = model.own.at(own_row, 1);
            station.y = model.own.at(own_row, 2);
            station.z = model.own.at(own_row, 3);
        }
        model.station_positions.push_back(station);
        if (seen.insert(key).second) model.stations.push_back(std::move(station));
    }
    bind_station_position_edit_ids(model);
}

int inspector_request_match_score(const TableRow& row,
                                  const MapElementInspectorRequest& request) {
    int score = 0;
    bool source_matched = false;
    if (!request.source_file.empty() && row.source.file_path == request.source_file) {
        source_matched = true;
        score += 100;
        if (request.line > 0 && row.source.line > 0) {
            int line_delta = std::abs(row.source.line - request.line);
            if (line_delta == 0) {
                score += 40;
            } else if (line_delta == 1) {
                score += 30;
            } else if (line_delta <= 3) {
                score += 10;
            } else {
                score -= std::min(line_delta, 30);
            }
        }
        if (request.column > 0 && row.source.column == request.column) score += 5;
    }

    int matched_fields = 0;
    for (const auto& field : request.field_values) {
        const std::string row_value = trim_gui_ascii_copy(
            inspector_row_field_value(row, request.row_kind, field.first));
        if (row_value == field.second) {
            score += 8;
            ++matched_fields;
        } else {
            score -= 3;
        }
    }

    const int required_field_matches = request.field_values.size() <= 1 ? 1 : 2;
    if (!source_matched && matched_fields < required_field_matches) return std::numeric_limits<int>::min();
    return score;
}

const TableRow* find_model_row_for_inspector_request(const MapModel& model,
                                                     const MapElementInspectorRequest& request,
                                                     std::string& resolved_edit_id,
                                                     size_t& resolved_row_index) {
    const std::vector<TableRow>* rows = inspector_rows_for_kind(model, request.row_kind);
    if (!rows) return nullptr;

    for (const TableRow& row : *rows) {
        if (!request.edit_id.empty() && row.edit_id == request.edit_id) {
            resolved_edit_id = request.edit_id;
            resolved_row_index = static_cast<size_t>(&row - rows->data());
            return &row;
        }
    }

    if (request.source_file.empty() && request.field_values.empty()) return nullptr;

    const TableRow* best = nullptr;
    int best_score = std::numeric_limits<int>::min();
    bool ambiguous = false;
    for (const TableRow& row : *rows) {
        if (row.edit_id.empty()) continue;
        int score = inspector_request_match_score(row, request);
        if (score > best_score) {
            best = &row;
            best_score = score;
            ambiguous = false;
        } else if (score == best_score) {
            ambiguous = true;
        }
    }

    if (!best || ambiguous || best_score == std::numeric_limits<int>::min()) return nullptr;
    resolved_edit_id = best->edit_id;
    resolved_row_index = static_cast<size_t>(best - rows->data());
    return best;
}

bool App::delete_element_target(const MapElementDeleteRequest& request) {
    if (!edit_actions_available() || request.edit_id.empty() ||
        !row_kind_supports_delete(request.row_kind)) {
        return false;
    }
    if (distance_resolution_workflow_.phase != DistanceResolutionPhase::None ||
        distance_resolution_workflow_.retry_requested) {
        cancel_distance_resolution_workflow();
    }
    std::map<std::string, MapElementPendingChange> candidate = pending_edit_changes_;
    std::set<std::string> repeater_chain_edit_ids;
    std::set<std::string> related_delete_ids;
    auto make_delete_change = [&](const std::string& edit_id, const std::string& row_kind) {
        MapElementDeleteRequest target_request = request;
        target_request.edit_id = edit_id;
        target_request.row_kind = row_kind;
        MapElementPendingChange change;
        change.change_id = "delete-" + edit_id;
        change.edit_id = edit_id;
        change.row_kind = row_kind;
        change.operation = "delete";
        std::string metadata_error;
        change.expected_source_hash = delete_expected_source_hash(
            model_, pending_edit_changes_, handle_, target_request, &metadata_error);
        if (!metadata_error.empty()) {
            add_log("[warn]gui_kme.cpp: delete target metadata fallback: " + metadata_error);
        }
        return change;
    };
    auto cancel_insert_or_add_delete = [&](const std::string& edit_id,
                                           const std::string& row_kind) {
        const auto pending = candidate.find(edit_id);
        if (pending != candidate.end() &&
            pending->second.operation == "insert") {
            // The row does not exist on the disk baseline. Removing its insert
            // from the complete replay ledger is the source-backed equivalent
            // of deleting it before Save.
            candidate.erase(pending);
            return;
        }
        candidate[edit_id] = make_delete_change(edit_id, row_kind);
    };

    if (request.row_kind == "curve" || request.row_kind == "gradient") {
        const std::vector<TableRow>& rows = request.row_kind == "curve"
            ? model_.curve_rows : model_.gradient_rows;
        size_t selected_index = 0;
        if (!find_row_index_by_edit_id(rows, request.edit_id, selected_index)) return false;
        const TableRow* primary = &rows[selected_index];
        const std::string primary_edit_id = table_cell(*primary, "_primaryEditId");
        if (!primary_edit_id.empty()) {
            if (!find_row_index_by_edit_id(rows, primary_edit_id, selected_index)) return false;
            primary = &rows[selected_index];
        } else if (ascii_lower(table_cell(*primary, "method")).find("begintransition") !=
                   std::string::npos) {
            add_log("[warn]gui_kme.cpp: unpaired BeginTransition cannot be deleted");
            return false;
        }
        related_delete_ids.insert(primary->edit_id);
        cancel_insert_or_add_delete(primary->edit_id, request.row_kind);
        const std::string transition_edit_id = table_cell(*primary, "_transitionEditId");
        if (!transition_edit_id.empty()) {
            related_delete_ids.insert(transition_edit_id);
            cancel_insert_or_add_delete(transition_edit_id, request.row_kind);
        }
    } else if (request.row_kind == "repeater") {
        const std::optional<RepeaterDeleteChain> chain =
            repeater_delete_chain_for_edit_id(model_.repeaters, request.edit_id);
        if (!chain || chain->begin_source_indices.empty() ||
            chain->selected_begin_index >= chain->begin_source_indices.size()) {
            add_log("[warn]gui_kme.cpp: Repeater delete target is not a linked Begin statement: " +
                    request.edit_id);
            return false;
        }
        if (chain->begin_source_indices.size() == 1 &&
            request.repeater_mode != RepeaterDeleteMode::EntireChain) {
            add_log("[warn]gui_kme.cpp: Repeater change-point delete requires multiple Begins");
            return false;
        }
        if (chain->selected_begin_index == 0 &&
            (request.repeater_mode == RepeaterDeleteMode::TrimToChangePoint ||
             request.repeater_mode == RepeaterDeleteMode::StartFromChangePoint)) {
            add_log("[warn]gui_kme.cpp: Repeater delete mode is unavailable for the first Begin");
            return false;
        }

        std::vector<std::string> begin_edit_ids;
        begin_edit_ids.reserve(chain->begin_source_indices.size());
        for (size_t source_index : chain->begin_source_indices) {
            if (source_index >= model_.repeaters.size() ||
                model_.repeaters[source_index].edit_id.empty()) {
                add_log("[warn]gui_kme.cpp: Repeater chain is missing editable Begin metadata");
                return false;
            }
            const std::string& edit_id = model_.repeaters[source_index].edit_id;
            begin_edit_ids.push_back(edit_id);
            repeater_chain_edit_ids.insert(edit_id);
        }
        std::optional<std::string> end_edit_id;
        if (chain->end_source_index) {
            if (*chain->end_source_index >= model_.repeaters.size() ||
                model_.repeaters[*chain->end_source_index].edit_id.empty()) {
                add_log("[warn]gui_kme.cpp: Repeater chain is missing editable End metadata");
                return false;
            }
            end_edit_id = model_.repeaters[*chain->end_source_index].edit_id;
            repeater_chain_edit_ids.insert(*end_edit_id);
        }

        const auto add_delete = [&](const std::string& edit_id) {
            cancel_insert_or_add_delete(edit_id, "repeater");
        };
        const size_t selected = chain->selected_begin_index;
        switch (request.repeater_mode) {
            case RepeaterDeleteMode::EntireChain:
                for (const std::string& edit_id : begin_edit_ids) add_delete(edit_id);
                if (end_edit_id) add_delete(*end_edit_id);
                break;
            case RepeaterDeleteMode::ChangePoint:
                add_delete(begin_edit_ids[selected]);
                break;
            case RepeaterDeleteMode::TrimToChangePoint: {
                MapElementDeleteRequest target_request = request;
                target_request.edit_id = begin_edit_ids[selected];
                target_request.row_kind = "repeater";
                const auto pending = candidate.find(target_request.edit_id);
                if (pending != candidate.end() &&
                    pending->second.operation == "insert") {
                    MapElementPendingChange change = pending->second;
                    std::map<std::string, std::string> end_fields;
                    for (const char* key : {"distance", "repeaterKey"}) {
                        const auto field = change.field_changes.find(key);
                        if (field != change.field_changes.end()) {
                            end_fields.emplace(field->first, field->second);
                        }
                    }
                    end_fields.emplace("method", "End");
                    change.field_changes = std::move(end_fields);
                    change.repeater_pair_id.clear();
                    change.confirm_repeater_change_point = false;
                    candidate[target_request.edit_id] = std::move(change);
                } else {
                    MapElementPendingChange change;
                    change.change_id = "repeater-trim-" + target_request.edit_id;
                    change.edit_id = target_request.edit_id;
                    change.row_kind = "repeater";
                    change.operation = "update";
                    change.field_changes.emplace("method", "End");
                    std::string metadata_error;
                    change.expected_source_hash = delete_expected_source_hash(
                        model_, pending_edit_changes_, handle_, target_request, &metadata_error);
                    if (!metadata_error.empty()) {
                        add_log("[warn]gui_kme.cpp: Repeater trim metadata fallback: " + metadata_error);
                    }
                    candidate[change.edit_id] = std::move(change);
                }
                for (size_t index = selected + 1; index < begin_edit_ids.size(); ++index) {
                    add_delete(begin_edit_ids[index]);
                }
                if (end_edit_id) add_delete(*end_edit_id);
                break;
            }
            case RepeaterDeleteMode::StartFromChangePoint:
                for (size_t index = 0; index < selected; ++index) add_delete(begin_edit_ids[index]);
                break;
        }
    } else {
        cancel_insert_or_add_delete(request.edit_id, request.row_kind);
    }

    if (apply_edit_ledger_to_preview(candidate, std::nullopt, true)) {
        const bool close_repeater_inspector = inspector_.open && inspector_.row_kind == "repeater" &&
            repeater_chain_edit_ids.find(inspector_.edit_id) != repeater_chain_edit_ids.end();
        if (close_repeater_inspector ||
            (inspector_.open && related_delete_ids.find(inspector_.edit_id) !=
                                  related_delete_ids.end()) ||
            (inspector_.open && inspector_.edit_id == request.edit_id)) {
            clear_scene_placement_edit_target();
            inspector_ = MapElementInspectorState{};
            pending_inspector_request_.reset();
        }
        set_program_status("status.edit.pending_delete");
        return true;
    }
    return false;
}

KvUtf8View edit_utf8_view(const std::string& text) {
    return {text.empty() ? nullptr : text.data(), static_cast<std::uint64_t>(text.size())};
}

struct TypedEditBatchStorage {
    std::vector<KvEditChange> changes;
    std::vector<KvEditField> fields;

    KvEditBatch view() const {
        return {changes.empty() ? nullptr : changes.data(),
                static_cast<std::uint64_t>(changes.size()),
                fields.empty() ? nullptr : fields.data(),
                static_cast<std::uint64_t>(fields.size())};
    }
};

TypedEditBatchStorage typed_edit_batch(
    const std::map<std::string, MapElementPendingChange>& inputs) {
    // KvUtf8View is non-owning. Keep the reserved field name in static
    // storage instead of creating a temporary std::string from a literal.
    static constexpr KvUtf8View k_insert_row_kind_field_name{
        "rowKind", sizeof("rowKind") - 1};
    static constexpr KvUtf8View k_repeater_pair_id_field_name{
        "repeaterPairId", sizeof("repeaterPairId") - 1};
    const auto is_resource_list_content_insert = [](
        const MapElementPendingChange& change) {
        return change.operation == "insert" &&
            (change.row_kind == "station.list" ||
             change.row_kind == "structure.model" ||
             change.row_kind == "signal.aspect" ||
             change.row_kind == "sound.list" ||
             change.row_kind == "sound3D.list");
    };
    std::vector<const MapElementPendingChange*> ordered;
    ordered.reserve(inputs.size());
    for (const auto& input : inputs) ordered.push_back(&input.second);
    std::map<std::pair<std::string, std::string>, std::vector<size_t>>
        insert_groups;
    for (size_t index = 0; index < ordered.size(); ++index) {
        const MapElementPendingChange& change = *ordered[index];
        if (is_resource_list_content_insert(change)) {
            insert_groups[{change.target_file_path, change.insert_before_edit_id}]
                .push_back(index);
        }
    }
    for (auto& entry : insert_groups) {
        std::vector<size_t>& indices = entry.second;
        std::stable_sort(indices.begin(), indices.end(), [&](size_t left, size_t right) {
            const MapElementPendingChange& a = *ordered[left];
            const MapElementPendingChange& b = *ordered[right];
            if (a.resource_list_insert_order != b.resource_list_insert_order) {
                return a.resource_list_insert_order < b.resource_list_insert_order;
            }
            return a.change_id < b.change_id;
        });
        std::vector<const MapElementPendingChange*> members;
        members.reserve(indices.size());
        for (size_t index : indices) members.push_back(ordered[index]);
        std::sort(indices.begin(), indices.end());
        for (size_t index = 0; index < indices.size(); ++index) {
            ordered[indices[index]] = members[index];
        }
    }
    TypedEditBatchStorage storage;
    storage.changes.reserve(inputs.size());
    size_t field_count = 0;
    for (const MapElementPendingChange* source : ordered) {
        field_count += source->field_changes.size();
        if (source->operation == "insert") ++field_count;
        if (!source->repeater_pair_id.empty()) ++field_count;
    }
    storage.fields.reserve(field_count);
    for (const MapElementPendingChange* source_pointer : ordered) {
        const MapElementPendingChange& source = *source_pointer;
        KvEditChange change{};
        change.change_id = edit_utf8_view(source.change_id);
        change.edit_id = edit_utf8_view(source.edit_id);
        if (source.operation == "update") change.operation = KV_EDIT_UPDATE;
        else if (source.operation == "insert") change.operation = KV_EDIT_INSERT;
        else if (source.operation == "delete") change.operation = KV_EDIT_DELETE;
        else throw std::runtime_error("unsupported GUI edit operation: " + source.operation);
        if (source.confirm_environment_mismatch) {
            change.flags |= KV_EDIT_CHANGE_CONFIRM_ENVIRONMENT_MISMATCH;
        }
        if (source.confirm_repeater_change_point) {
            change.flags |= KV_EDIT_CHANGE_CONFIRM_REPEATER_CHANGE_POINT;
        }
        change.fields.offset = static_cast<std::uint64_t>(storage.fields.size());
        change.fields.count = static_cast<std::uint64_t>(source.field_changes.size());
        if (source.operation == "insert") ++change.fields.count;
        if (!source.repeater_pair_id.empty()) ++change.fields.count;
        if (source.operation == "insert") {
            // The maploader derives insert row kinds from this reserved field
            // and pops it before any typed edit processing runs.
            storage.fields.push_back({k_insert_row_kind_field_name,
                                      edit_utf8_view(source.row_kind)});
        }
        if (!source.repeater_pair_id.empty()) {
            storage.fields.push_back({k_repeater_pair_id_field_name,
                                      edit_utf8_view(source.repeater_pair_id)});
        }
        for (const auto& field : source.field_changes) {
            storage.fields.push_back({edit_utf8_view(field.first), edit_utf8_view(field.second)});
        }
        change.replacement_statement = edit_utf8_view(source.replacement_statement);
        change.target_file_path = edit_utf8_view(source.target_file_path);
        change.expected_source_hash = edit_utf8_view(source.expected_source_hash);
        change.insert_before_edit_id = edit_utf8_view(source.insert_before_edit_id);
        change.distance_resolution_key = edit_utf8_view(source.distance_resolution_key);
        change.distance_boundary_token = edit_utf8_view(source.distance_boundary_token);
        change.distance_expression = edit_utf8_view(source.distance_expression);
        storage.changes.push_back(change);
    }
    return storage;
}

std::string edit_report_string(const KvEditReportSnapshot& report, KvStringRef ref) {
    if (!report.string_data || ref.offset > report.string_size ||
        ref.length > report.string_size - ref.offset) {
        return {};
    }
    return std::string(report.string_data + static_cast<size_t>(ref.offset),
                       static_cast<size_t>(ref.length));
}

bool App::validate_resource_list_file_change_candidate(
    const std::map<std::string, MapElementPendingChange>& changes,
    ResourceListKind kind) {
    if (!edit_actions_available() || !handle_ || load_state_.running) return false;
    TypedEditBatchStorage batch_storage = typed_edit_batch(changes);
    const KvEditBatch batch = batch_storage.view();
    KvEditReportSnapshot report{};
    if (!kv_edit_dry_run_typed(handle_, &batch, &report, sizeof(report))) {
        const char* error = kv_get_last_error();
        add_log(std::string("[error]gui_kme.cpp: resource-list replacement dry run failed: ") +
                (error && *error ? error : "unknown error"));
        return false;
    }

    const char* header_error = nullptr;
    const char* status_key = nullptr;
    switch (kind) {
    case ResourceListKind::Station:
        header_error = "resource-list-header-mismatch:station";
        status_key = "status.edit.not_station_list_file";
        break;
    case ResourceListKind::Structure:
        header_error = "resource-list-header-mismatch:structure";
        status_key = "status.edit.not_structure_list_file";
        break;
    case ResourceListKind::Signal:
        header_error = "resource-list-header-mismatch:signal";
        status_key = "status.edit.not_signal_aspect_list_file";
        break;
    case ResourceListKind::Sound:
    case ResourceListKind::Sound3D:
        header_error = "resource-list-header-mismatch:sound";
        status_key = "status.edit.not_sound_list_file";
        break;
    case ResourceListKind::Count:
        return false;
    }
    bool header_mismatch = false;
    if (report.blocking_errors) {
        for (std::uint64_t i = 0; i < report.blocking_error_count; ++i) {
            const std::string message = edit_report_string(
                report, report.blocking_errors[i]);
            header_mismatch = header_mismatch ||
                message.find(header_error) != std::string::npos;
        }
    }
    if (!parse_and_log_edit_report(report, {})) {
        if (header_mismatch) set_program_status(status_key);
        return false;
    }
    return true;
}

bool edit_report_span_valid(KvSpan span, std::uint64_t size) {
    return span.offset <= size && span.count <= size - span.offset;
}

DistanceResolutionRequest distance_resolution_request_from_typed(
    const KvEditReportSnapshot& report, const KvDistanceResolutionRow& input) {
    DistanceResolutionRequest request;
    request.resolution_key = edit_report_string(report, input.resolution_key);
    request.reason = edit_report_string(report, input.reason);
    request.source_file = edit_report_string(report, input.source_file);
    request.target_distance = format_double(input.target_distance, 6);
    request.variable_name = edit_report_string(report, input.variable_name);
    request.suggested_expression = edit_report_string(report, input.suggested_expression);
    request.insertion_preview = edit_report_string(report, input.insertion_preview);
    request.can_confirm_reuse = input.can_confirm_reuse != 0;
    request.source_section_direction = edit_report_string(
        report, input.source_section_direction);
    auto append_strings = [&](KvSpan span, std::vector<std::string>& output) {
        if (!edit_report_span_valid(span, report.string_ref_count) ||
            (span.count != 0 && !report.string_refs)) return;
        output.reserve(static_cast<size_t>(span.count));
        for (std::uint64_t i = 0; i < span.count; ++i) {
            output.push_back(edit_report_string(report, report.string_refs[span.offset + i]));
        }
    };
    append_strings(input.include_stack, request.include_stack);
    append_strings(input.affected_edit_ids, request.affected_edit_ids);
    if (edit_report_span_valid(input.allowed_boundaries, report.boundary_count) &&
        (input.allowed_boundaries.count == 0 || report.boundaries)) {
        request.allowed_boundaries.reserve(static_cast<size_t>(input.allowed_boundaries.count));
        for (std::uint64_t i = 0; i < input.allowed_boundaries.count; ++i) {
            const KvDistanceBoundaryRow& source =
                report.boundaries[input.allowed_boundaries.offset + i];
            request.allowed_boundaries.push_back({edit_report_string(report, source.token),
                                                   source.line, source.column,
                                                   source.recommended != 0});
        }
    }
    return request;
}

struct CommittedEditFileState {
    std::string file_path;
    std::string source_hash;
    size_t byte_length = 0;
};

struct CommittedEditRowState {
    std::string row_kind;
    size_t row_index = 0;
    std::string edit_id;
    EditSourceInfo source;
};

bool apply_committed_edit_state(MapModel& model, const KvEditReportSnapshot& report,
                                std::string& error) {
    if (report.committed_file_count == 0) return true;
    if (!report.committed_files) {
        error = "edit commit report has a null committed-file array";
        return false;
    }

    std::vector<CommittedEditFileState> file_states;
    file_states.reserve(static_cast<size_t>(report.committed_file_count));
    for (std::uint64_t i = 0; i < report.committed_file_count; ++i) {
        const KvEditCommittedFileRow& item = report.committed_files[i];
        CommittedEditFileState state;
        state.file_path = edit_report_string(report, item.file_path);
        state.source_hash = edit_report_string(report, item.source_hash);
        state.byte_length = static_cast<size_t>(item.byte_length);
        file_states.push_back(std::move(state));
    }

    if (report.committed_row_count != 0 && !report.committed_rows) {
        error = "edit commit report is missing committed row metadata";
        return false;
    }
    std::map<std::string, std::vector<CommittedEditRowState>> rows_by_kind;
    for (std::uint64_t i = 0; i < report.committed_row_count; ++i) {
        const KvEditCommittedRow& item = report.committed_rows[i];
        CommittedEditRowState state;
        state.row_kind = edit_report_string(report, item.row_kind);
        if (item.row_index > static_cast<std::uint64_t>(std::numeric_limits<size_t>::max())) {
            error = "edit commit report contains an invalid row index";
            return false;
        }
        state.row_index = static_cast<size_t>(item.row_index);
        state.edit_id = edit_report_string(report, item.edit_id);
        state.source.file_path = edit_report_string(report, item.file_path);
        state.source.line = item.line;
        state.source.column = item.column;
        state.source.raw_text_preview = edit_report_string(report, item.raw_text_preview);
        if (inspector_rows_for_kind(model, state.row_kind)) {
            rows_by_kind[state.row_kind].push_back(std::move(state));
        }
    }

    static constexpr std::array<const char*, 27> k_committed_row_kinds = {
        "curve", "gradient", "structure.model", "structure.put", "structure.between", "station.put",
        "station.list", "sound.list", "sound3D.list", "repeater", "signal.put",
        "signal.aspect", "irregularity.change",
        "beacon.put", "mapSound.play", "mapSound3D.put",
        "rollingNoise.change", "flangeNoise.change", "jointNoise.play",
        "background.change", "adhesion.change", "cabIlluminance.change",
        "fog.change", "drawDistance.change", "speedlimit",
        "section.begin", "section.speedLimit",
    };
    std::map<std::string, std::map<std::string, const CommittedEditRowState*>>
        states_by_edit_id;
    for (const char* row_kind : k_committed_row_kinds) {
        std::vector<TableRow>* target_rows = inspector_rows_for_kind(model, row_kind);
        const std::vector<CommittedEditRowState>& states = rows_by_kind[row_kind];
        if (!target_rows || states.size() != target_rows->size()) {
            error = std::string("edit commit row count mismatch for ") + row_kind;
            return false;
        }
        std::vector<bool> seen(target_rows->size(), false);
        for (const CommittedEditRowState& state : states) {
            if (state.row_index >= target_rows->size() || seen[state.row_index]) {
                error = std::string("edit commit row mapping is invalid for ") + row_kind;
                return false;
            }
            seen[state.row_index] = true;
            if (!state.edit_id.empty()) {
                auto inserted = states_by_edit_id[row_kind].emplace(state.edit_id, &state);
                if (!inserted.second) {
                    error = std::string("edit commit contains a duplicate editId for ") +
                        row_kind + ": " + state.edit_id;
                    return false;
                }
            }
        }
        for (const TableRow& row : *target_rows) {
            if (row.edit_id.empty()) continue;
            if (states_by_edit_id[row_kind].find(row.edit_id) ==
                states_by_edit_id[row_kind].end()) {
                error = std::string("edit commit lost stable row identity for ") +
                    row_kind + ": " + row.edit_id;
                return false;
            }
        }
    }

    for (const CommittedEditFileState& state : file_states) {
        auto file = std::find_if(model.edit_files.begin(), model.edit_files.end(),
                                 [&](const EditSourceFileInfo& candidate) {
                                     return candidate.file_path == state.file_path;
                                 });
        if (file == model.edit_files.end()) continue;
        file->source_hash = state.source_hash;
        file->byte_length = state.byte_length;
    }
    for (const char* row_kind : k_committed_row_kinds) {
        std::vector<TableRow>* target_rows = inspector_rows_for_kind(model, row_kind);
        for (size_t row_index = 0; row_index < target_rows->size(); ++row_index) {
            TableRow& row = (*target_rows)[row_index];
            const CommittedEditRowState* state = nullptr;
            if (!row.edit_id.empty()) {
                state = states_by_edit_id[row_kind].at(row.edit_id);
            } else {
                const std::vector<CommittedEditRowState>& states = rows_by_kind[row_kind];
                auto fallback = std::find_if(
                    states.begin(), states.end(), [&](const CommittedEditRowState& candidate) {
                        return candidate.row_index == row_index && candidate.edit_id.empty();
                    });
                if (fallback != states.end()) state = &*fallback;
            }
            if (!state) {
                error = std::string("edit commit could not bind row metadata for ") + row_kind;
                return false;
            }
            row.source = state->source;
        }
    }
    return true;
}

bool apply_committed_edit_report_to_model(MapModel& model,
                                          const KvEditReportSnapshot& report,
                                          std::string& error_message) {
    error_message.clear();
    if (report.version != KV_EDIT_REPORT_SNAPSHOT_VERSION ||
        report.structure_size < sizeof(KvEditReportSnapshot)) {
        error_message = "edit commit report version or size mismatch";
        return false;
    }
    if (!report.ok) {
        error_message = "edit commit report is not successful";
        return false;
    }
    return apply_committed_edit_state(model, report, error_message);
}

bool App::parse_and_log_edit_report(const KvEditReportSnapshot& report,
                                    const std::string& success_prefix,
                                    int* update_count,
                                    int* delete_count,
                                    int* changed_file_count,
                                    std::vector<DistanceResolutionRequest>* resolution_requests) {
    if (resolution_requests) resolution_requests->clear();
    if (report.version != KV_EDIT_REPORT_SNAPSHOT_VERSION ||
        report.structure_size < sizeof(KvEditReportSnapshot)) {
        add_log("[error]gui_kme.cpp: edit report version or size mismatch");
        return false;
    }
    if (resolution_requests && report.resolution_request_count != 0 &&
        report.resolution_requests) {
        resolution_requests->reserve(static_cast<size_t>(report.resolution_request_count));
        for (std::uint64_t i = 0; i < report.resolution_request_count; ++i) {
            DistanceResolutionRequest request = distance_resolution_request_from_typed(
                report, report.resolution_requests[i]);
            if (!request.resolution_key.empty()) resolution_requests->push_back(std::move(request));
        }
    }
    if (report.warnings) {
        for (std::uint64_t i = 0; i < report.warning_count; ++i) {
            add_log("[warn]gui_kme.cpp: " + edit_report_string(report, report.warnings[i]));
        }
    }
    bool repeater_key_conflict = false;
    bool other_track_key_conflict = false;
    if (report.blocking_errors) {
        for (std::uint64_t i = 0; i < report.blocking_error_count; ++i) {
            const std::string message =
                edit_report_string(report, report.blocking_errors[i]);
            repeater_key_conflict = repeater_key_conflict ||
                message.rfind(
                    "Repeater key overlaps another Repeater interval", 0) == 0;
            other_track_key_conflict = other_track_key_conflict ||
                message.rfind("Other-track key already exists in map", 0) == 0;
            add_log("[error]gui_kme.cpp: " + message);
        }
    }
    const int updates = report.update_count;
    const int deletes = report.delete_count;
    const int files = static_cast<int>(std::min<std::uint64_t>(
        report.changed_file_count, static_cast<std::uint64_t>(std::numeric_limits<int>::max())));
    if (update_count) *update_count = updates;
    if (delete_count) *delete_count = deletes;
    if (changed_file_count) *changed_file_count = files;
    bool ok = report.ok != 0;
    if (!ok && repeater_key_conflict) {
        set_program_status("status.edit.repeater_key_conflict");
    } else if (!ok && other_track_key_conflict) {
        set_program_status("status.edit.other_track_key_conflict");
    }
    if (ok) {
        std::string committed_state_error;
        if (!apply_committed_edit_state(model_, report, committed_state_error)) {
            add_log("[error]gui_kme.cpp: saved edit metadata refresh failed; Reload is required: " +
                    committed_state_error);
            clear_pending_edit_state();
            edit_registry_loaded_ = false;
            ok = false;
        }
    }
    if (ok && !success_prefix.empty()) {
        add_log(success_prefix + ": updates=" + std::to_string(updates) +
                ", deletes=" + std::to_string(deletes) +
                ", files=" + std::to_string(files));
    }
    return ok;
}

bool App::sync_edit_memory_with_ledger(
    const std::map<std::string, MapElementPendingChange>& changes,
    std::vector<DistanceResolutionRequest>* resolution_requests) {
    if (resolution_requests) resolution_requests->clear();
    if (!edit_actions_available()) return false;
    if (!handle_ || load_state_.running) return false;

    if (!kv_edit_reset_memory(handle_)) {
        edit_memory_matches_pending_ledger_ = false;
        const char* err = kv_get_last_error();
        add_log(std::string("[error]gui_kme.cpp: edit memory reset failed: ") +
                (err ? err : "unknown error"));
        return false;
    }
    edit_memory_matches_pending_ledger_ = changes.empty();
    if (changes.empty()) {
        add_log("[info]gui_kme.cpp: edit memory reset to disk baseline");
        return true;
    }

    TypedEditBatchStorage batch_storage = typed_edit_batch(changes);
    const KvEditBatch batch = batch_storage.view();
    KvEditReportSnapshot report{};
    if (!kv_edit_apply_to_memory_typed(handle_, &batch, &report, sizeof(report))) {
        edit_memory_matches_pending_ledger_ = false;
        const char* err = kv_get_last_error();
        add_log(std::string("[error]gui_kme.cpp: edit memory apply failed: ") +
                (err ? err : "unknown error"));
        return false;
    }

    if (!parse_and_log_edit_report(report, "[info]gui_kme.cpp: edit memory updated",
                                   nullptr, nullptr, nullptr, resolution_requests)) {
        edit_memory_matches_pending_ledger_ = false;
        return false;
    }
    edit_memory_matches_pending_ledger_ = true;
    return true;
}

bool App::apply_edit_ledger_to_preview(const std::map<std::string, MapElementPendingChange>& changes,
                                       std::optional<MapElementInspectorRequest> reload_request,
                                       bool applying_delete,
                                       std::string resolution_origin_edit_id) {
    if (!edit_actions_available()) return false;
    const bool ended_batch = !pending_edit_changes_.empty() && changes.empty();
    const bool reapplies_inspector_change = reload_request.has_value();

    // The backend working copy and the locally patched table rows form one
    // preview transaction. Back up only the row kinds touched by either ledger
    // so a local lookup failure cannot leave a validated backend candidate that
    // Save would commit while the GUI still shows the previous ledger.
    std::set<std::string> affected_row_kinds;
    for (const auto& entry : pending_edit_changes_) {
        if (!entry.second.row_kind.empty()) affected_row_kinds.insert(entry.second.row_kind);
    }
    for (const auto& entry : changes) {
        if (!entry.second.row_kind.empty()) affected_row_kinds.insert(entry.second.row_kind);
    }
    std::map<std::string, std::vector<TableRow>> row_backups;
    for (const std::string& row_kind : affected_row_kinds) {
        if (std::vector<TableRow>* rows = inspector_rows_for_kind(model_, row_kind)) {
            row_backups.emplace(row_kind, *rows);
        }
    }
    for (const auto& entry : changes) {
        if (entry.second.row_kind == "curve" || entry.second.row_kind == "gradient" ||
            entry.second.row_kind == "otherTrack.change") {
            snapshot_local_preview_row(entry.first, entry.second.row_kind);
        }
    }
    const auto snapshot_backup = original_edit_rows_;

    std::vector<DistanceResolutionRequest> resolution_requests;
    if (!sync_edit_memory_with_ledger(changes, &resolution_requests)) {
        if (!pending_edit_changes_.empty()) {
            sync_edit_memory_with_ledger(pending_edit_changes_);
        }
        if (!resolution_requests.empty()) {
            begin_distance_resolution_workflow(
                changes, std::move(reload_request), applying_delete,
                std::move(resolution_origin_edit_id), resolution_requests);
        }
        return false;
    }

    auto rollback_local_preview = [&]() {
        for (auto& backup : row_backups) {
            if (std::vector<TableRow>* rows =
                    inspector_rows_for_kind(model_, backup.first)) {
                *rows = std::move(backup.second);
            }
        }
        original_edit_rows_ = snapshot_backup;
        for (const std::string& row_kind : affected_row_kinds) {
            refresh_local_preview_after_edit(row_kind);
        }

        edit_memory_matches_pending_ledger_ = false;
        if (!sync_edit_memory_with_ledger(pending_edit_changes_)) {
            add_log("[error]gui_kme.cpp: failed to restore maploader working copy after "
                    "a local preview error; Save is blocked");
        }
        refresh_text_preview_from_working_copy();
        return false;
    };

    const bool signal_aspects_hydrated =
        affected_row_kinds.find("signal.aspect") !=
        affected_row_kinds.end();
    const bool alignment_hydrated =
        affected_row_kinds.find("curve") != affected_row_kinds.end() ||
        affected_row_kinds.find("gradient") != affected_row_kinds.end() ||
        affected_row_kinds.find("otherTrack.change") != affected_row_kinds.end();
    const auto ledger_contains_insert = [](const auto& ledger) {
        return std::any_of(
            ledger.begin(), ledger.end(),
            [](const auto& entry) { return entry.second.operation == "insert"; });
    };
    const auto ledger_contains_include_edit = [](const auto& ledger) {
        return std::any_of(ledger.begin(), ledger.end(), [](const auto& entry) {
            return entry.second.row_kind == "include" &&
                (entry.second.operation == "delete" ||
                 entry.second.operation == "update");
        });
    };
    const auto ledger_contains_resource_list_load_edit = [](const auto& ledger) {
        return std::any_of(ledger.begin(), ledger.end(), [](const auto& entry) {
            return entry.second.row_kind == "resourceList.load" &&
                entry.second.operation == "update";
        });
    };
    const bool full_insert_hydration =
        ledger_contains_insert(changes) ||
        ledger_contains_insert(pending_edit_changes_) ||
        ledger_contains_include_edit(changes) ||
        ledger_contains_include_edit(pending_edit_changes_) ||
        ledger_contains_resource_list_load_edit(changes) ||
        ledger_contains_resource_list_load_edit(pending_edit_changes_);
    if (signal_aspects_hydrated || alignment_hydrated) {
        KvMapSnapshot snapshot{};
        if (!kv_get_map_snapshot(
                handle_, KV_MAP_SNAPSHOT_VERSION,
                &snapshot, sizeof(snapshot)) ||
            snapshot.version != KV_MAP_SNAPSHOT_VERSION ||
            snapshot.structure_size < sizeof(KvMapSnapshot) ||
            (signal_aspects_hydrated && snapshot.signal_aspect_count != 0 &&
             !snapshot.signal_aspects) ||
            (alignment_hydrated &&
             ((snapshot.curve_count != 0 && !snapshot.curves) ||
              (snapshot.gradient_count != 0 && !snapshot.gradients) ||
              (snapshot.other_track_change_count != 0 &&
               !snapshot.other_track_changes)))) {
            const char* error = kv_get_last_error();
            add_log(
                "[error]gui_kme.cpp: failed to refresh typed rows from the "
                "validated working copy" +
                std::string(error && *error
                    ? ": " + std::string(error)
                    : std::string{}));
            return rollback_local_preview();
        }
        if (signal_aspects_hydrated) {
            model_.signal_aspects = hydrate_signal_aspect_rows(snapshot);
        }
        if (alignment_hydrated) {
            std::map<std::string, std::pair<bool, ImVec4>> other_track_state;
            for (const OtherTrack& track : model_.other_tracks) {
                other_track_state[track.key] = {track.visible, track.color};
            }
            MapModel refreshed = hydrate_map_snapshot(snapshot, model_.path, 0.0);
            for (OtherTrack& track : refreshed.other_tracks) {
                auto state = other_track_state.find(track.key);
                if (state != other_track_state.end()) {
                    track.visible = state->second.first;
                    track.color = state->second.second;
                }
            }
            model_.own = std::move(refreshed.own);
            model_.curve = std::move(refreshed.curve);
            model_.other_tracks = std::move(refreshed.other_tracks);
            model_.controlpoints = std::move(refreshed.controlpoints);
            model_.own_events = std::move(refreshed.own_events);
            model_.curve_rows = std::move(refreshed.curve_rows);
            model_.gradient_rows = std::move(refreshed.gradient_rows);
            model_.other_track_changes = std::move(refreshed.other_track_changes);
            model_.distance_origin = refreshed.distance_origin;
            model_.height_origin = refreshed.height_origin;
            model_.origin_angle = refreshed.origin_angle;
            model_.default_min = refreshed.default_min;
            model_.default_max = refreshed.default_max;
            for (size_t i = 0; i < 3; ++i) model_.cp_arb[i] = refreshed.cp_arb[i];
            normalize_station_preview_rows(model_);
        }
        for (auto original = original_edit_rows_.begin();
             original != original_edit_rows_.end();) {
            if (signal_aspects_hydrated &&
                original->second.row_kind == "signal.aspect") {
                original = original_edit_rows_.erase(original);
            } else {
                ++original;
            }
        }
    }

    if (full_insert_hydration) {
        // An insert creates rows with brand-new edit ids, so the local
        // row-patching loops below cannot target them. Re-hydrate the whole
        // model from the validated working-copy snapshot; every pending
        // change of the batch is already reflected in it.
        KvMapSnapshot snapshot{};
        if (!kv_get_map_snapshot(
                handle_, KV_MAP_SNAPSHOT_VERSION,
                &snapshot, sizeof(snapshot)) ||
            snapshot.version != KV_MAP_SNAPSHOT_VERSION ||
            snapshot.structure_size < sizeof(KvMapSnapshot)) {
            const char* error = kv_get_last_error();
            add_log(
                "[error]gui_kme.cpp: failed to refresh the model after element "
                "insertion" +
                std::string(error && *error
                    ? ": " + std::string(error)
                    : std::string{}));
            return rollback_local_preview();
        }
        std::map<std::string, std::pair<bool, ImVec4>> other_track_state;
        for (const OtherTrack& track : model_.other_tracks) {
            other_track_state[track.key] = {track.visible, track.color};
        }
        MapModel refreshed = hydrate_map_snapshot(snapshot, model_.path, 0.0);
        for (OtherTrack& track : refreshed.other_tracks) {
            auto state = other_track_state.find(track.key);
            if (state != other_track_state.end()) {
                track.visible = state->second.first;
                track.color = state->second.second;
            }
        }
        model_ = std::move(refreshed);
        normalize_station_preview_rows(model_);
        original_edit_rows_.clear();
    }

    std::map<std::string, std::vector<std::string>> refresh_targets;
    auto note_refresh_target = [&](const std::string& row_kind, const std::string& edit_id,
                                   bool force_full_refresh) {
        std::vector<std::string>& targets = refresh_targets[row_kind];
        if (force_full_refresh) {
            targets.assign(1, std::string{});
            return;
        }
        if (!targets.empty() && targets.front().empty()) return;
        targets.push_back(edit_id);
    };
    if (signal_aspects_hydrated) {
        note_refresh_target("signal.aspect", std::string{}, true);
    }
    if (alignment_hydrated) {
        note_refresh_target("curve", std::string{}, true);
        note_refresh_target("gradient", std::string{}, true);
        note_refresh_target("otherTrack.change", std::string{}, true);
    }
    if (full_insert_hydration) {
        for (const std::string& row_kind : affected_row_kinds) {
            note_refresh_target(row_kind, std::string{}, true);
        }
    }

    for (const auto& kv : pending_edit_changes_) {
        if (changes.find(kv.first) != changes.end()) continue;
        if ((signal_aspects_hydrated && kv.second.row_kind == "signal.aspect") ||
            (alignment_hydrated &&
             (kv.second.row_kind == "curve" || kv.second.row_kind == "gradient" ||
              kv.second.row_kind == "otherTrack.change")) ||
            full_insert_hydration) {
            continue;
        }
        if (!restore_local_preview_change(kv.first, kv.second.row_kind, false)) {
            add_log("[error]gui_kme.cpp: failed to restore local edit preview: " + kv.first);
            return rollback_local_preview();
        }
        const bool force_full_refresh = kv.second.operation == "delete" ||
            (kv.second.row_kind == "structure.put" &&
             kv.second.field_changes.find("structureKey") != kv.second.field_changes.end()) ||
            (kv.second.row_kind == "signal.put" &&
             kv.second.field_changes.find("signalAspectKey") !=
                 kv.second.field_changes.end());
        note_refresh_target(kv.second.row_kind, kv.first, force_full_refresh);
    }

    for (const auto& kv : changes) {
        if ((signal_aspects_hydrated && kv.second.row_kind == "signal.aspect") ||
            (alignment_hydrated &&
             (kv.second.row_kind == "curve" || kv.second.row_kind == "gradient" ||
              kv.second.row_kind == "otherTrack.change")) ||
            full_insert_hydration) {
            continue;
        }
        if (!apply_local_preview_change(kv.second, false)) {
            add_log("[error]gui_kme.cpp: failed to apply local edit preview: " + kv.first);
            return rollback_local_preview();
        }
        const bool force_full_refresh = kv.second.operation == "delete" ||
            (kv.second.row_kind == "structure.put" &&
             kv.second.field_changes.find("structureKey") != kv.second.field_changes.end()) ||
            (kv.second.row_kind == "signal.put" &&
             kv.second.field_changes.find("signalAspectKey") !=
                 kv.second.field_changes.end());
        note_refresh_target(kv.second.row_kind, kv.first, force_full_refresh);
    }
    if (alignment_hydrated) {
        for (auto original = original_edit_rows_.begin();
             original != original_edit_rows_.end();) {
            const bool alignment_row = original->second.row_kind == "curve" ||
                original->second.row_kind == "gradient" ||
                original->second.row_kind == "otherTrack.change";
            if (alignment_row && changes.find(original->first) == changes.end()) {
                original = original_edit_rows_.erase(original);
            } else {
                ++original;
            }
        }
    }
    pending_edit_changes_ = changes;
    for (auto& entry : refresh_targets) {
        std::vector<std::string>& targets = entry.second;
        std::sort(targets.begin(), targets.end());
        targets.erase(std::unique(targets.begin(), targets.end()), targets.end());
        const std::string edit_id = targets.size() == 1 ? targets.front() : std::string{};
        refresh_local_preview_after_edit(entry.first, edit_id);
    }
    if (ended_batch) {
        distance_resolution_choices_.clear();
    }
    if (reload_request) {
        pending_inspector_request_ = std::move(reload_request);
    } else if (applying_delete && inspector_.open) {
        set_program_status("status.edit.pending_delete");
    } else if (!pending_edit_changes_.empty() && inspector_.open) {
        set_program_status("status.edit.applied_to_preview");
    }
    if (reapplies_inspector_change) set_program_status("status.edit.applied_to_preview");
    refresh_text_preview_from_working_copy();
    return true;
}

bool App::save_pending_edits(bool refresh_inspector) {
    if (!edit_actions_available()) return false;
    if (load_state_.running) return false;
    if (has_unapplied_editable_list_drafts()) {
        add_log("[warning]gui_kme.cpp: Save blocked by unapplied editable-list drafts");
        set_program_status("status.edit.apply_list_before_save");
        return false;
    }
    if (!handle_ || !has_pending_edits()) return false;

    if (!edit_memory_matches_pending_ledger_) {
        std::vector<DistanceResolutionRequest> replay_requests;
        if (!sync_edit_memory_with_ledger(pending_edit_changes_, &replay_requests)) {
            add_log("[error]gui_kme.cpp: edit save blocked because the pending ledger "
                    "could not be restored to the maploader working copy");
            return false;
        }
    }

    const bool inspector_target_deleted = inspector_.open && row_is_pending_delete(inspector_.edit_id);
    std::optional<MapElementInspectorRequest> inspector_request;
    if (refresh_inspector && inspector_.open && !inspector_target_deleted) {
        inspector_request = make_inspector_reload_request(inspector_);
    }

    // Apply/Revert/Delete keep the maploader working copy synchronized with the
    // pending ledger. Save is only the disk-write boundary; resetting, replaying,
    // or rebuilding the GUI model here would parse the same map state again.
    KvEditReportSnapshot report{};
    if (!kv_edit_commit_typed(handle_, &report, sizeof(report))) {
        const char* err = kv_get_last_error();
        add_log(std::string("[error]gui_kme.cpp: edit save failed: ") + (err ? err : "unknown error"));
        return false;
    }

    int committed_file_count = 0;
    if (!parse_and_log_edit_report(report, "[info]gui_kme.cpp: edit save committed",
                                   nullptr, nullptr, &committed_file_count)) {
        return false;
    }
    if (committed_file_count <= 0) {
        edit_memory_matches_pending_ledger_ = false;
        add_log("[error]gui_kme.cpp: edit save returned no committed source files; "
                "the pending ledger was retained");
        return false;
    }
    distance_resolution_choices_.clear();
    distance_resolution_workflow_ = DistanceResolutionWorkflowState{};
    text_preview_.placement = TextPreviewPlacementState{};
    pending_edit_changes_.clear();
    edit_memory_matches_pending_ledger_ = true;
    original_edit_rows_.clear();
    refresh_text_preview_from_working_copy();
    if (refresh_inspector && inspector_target_deleted) {
        inspector_.open = false;
    } else if (inspector_request) {
        open_element_inspector(*inspector_request);
    }
    set_program_status("status.edit.saved");
    return true;
}

bool App::discard_pending_edits() {
    if (!has_pending_edits()) {
        distance_resolution_choices_.clear();
        distance_resolution_workflow_ = DistanceResolutionWorkflowState{};
        text_preview_.placement = TextPreviewPlacementState{};
        discard_all_editable_list_drafts();
        return true;
    }
    if (!apply_edit_ledger_to_preview({}, std::nullopt, false)) return false;
    distance_resolution_choices_.clear();
    distance_resolution_workflow_ = DistanceResolutionWorkflowState{};
    text_preview_.placement = TextPreviewPlacementState{};
    discard_all_editable_list_drafts();
    return true;
}

bool App::revert_all_pending_edits() {
    if (!edit_actions_available() || !has_unsaved_edit_state()) return false;

    std::optional<MapElementInspectorRequest> inspector_request;
    if (inspector_.open && !inspector_.edit_id.empty()) {
        MapElementInspectorRequest request;
        request.edit_id = inspector_.edit_id;
        request.row_kind = inspector_.row_kind;
        request.source_file = inspector_.source_file;
        request.line = inspector_.line;
        request.column = inspector_.column;
        inspector_request = std::move(request);
    }

    clear_scene_placement_edit_target();
    if (!discard_pending_edits()) return false;

    if (inspector_request && !open_element_inspector(*inspector_request)) {
        inspector_ = MapElementInspectorState{};
        pending_inspector_request_.reset();
    }
    set_program_status("status.edit.reverted");
    return true;
}

bool App::resolve_pending_close_action(bool save_changes) {
    const PendingCloseAction action = pending_close_action_;
    if (action == PendingCloseAction::None) return true;

    if (save_changes) {
        request_edit_ui_operation(PendingEditUiOperation::SaveAndResolveClose);
        return false;
    }
    request_edit_ui_operation(PendingEditUiOperation::DiscardAndResolveClose);
    return false;
}

void App::finish_pending_close_action() {
    const PendingCloseAction action = pending_close_action_;
    pending_close_action_ = PendingCloseAction::None;
    if (action == PendingCloseAction::DisableEditMode) {
        apply_edit_mode_enabled(false);
    } else if (action == PendingCloseAction::ExitApplication) {
        PostQuitMessage(0);
    }
}
