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

bool editable_list_row_has_draft(const EditableListDraftRow& row) {
    return row.deleted || row.secondary_row_deleted ||
        row.payload_edit_id != row.target_edit_id ||
        row.values != row.original_values;
}

std::string editable_list_field_name(const EditableListSpec& spec,
                                     size_t field_index) {
    if (spec.numbered_structure_key_fields) {
        return field_index == 0
            ? "signalAspectKey"
            : "structureKey" + std::to_string(field_index);
    }
    if (!spec.field_names || field_index >= spec.field_count) return {};
    return spec.field_names[field_index];
}

std::vector<size_t> editable_list_visible_row_indices(
    const std::vector<EditableListDraftRow>& rows) {
    std::vector<size_t> result;
    result.reserve(rows.size());
    for (size_t index = 0; index < rows.size(); ++index) {
        result.push_back(index);
    }
    return result;
}

bool move_editable_list_draft_row(
    std::vector<EditableListDraftRow>& rows,
    const std::vector<size_t>& visible_rows,
    int visible_row, int direction) {
    if (direction != -1 && direction != 1) return false;
    const int neighbor = visible_row + direction;
    if (visible_row < 0 || visible_row >= static_cast<int>(visible_rows.size()) ||
        neighbor < 0 || neighbor >= static_cast<int>(visible_rows.size())) {
        return false;
    }
    EditableListDraftRow& left =
        rows[visible_rows[static_cast<size_t>(visible_row)]];
    EditableListDraftRow& right =
        rows[visible_rows[static_cast<size_t>(neighbor)]];
    if (left.deleted || right.deleted ||
        left.target_source_file != right.target_source_file) {
        return false;
    }
    std::swap(left.payload_edit_id, right.payload_edit_id);
    std::swap(left.payload_source_file, right.payload_source_file);
    std::swap(left.payload_line, right.payload_line);
    std::swap(left.payload_column, right.payload_column);
    std::swap(left.payload_raw_statement, right.payload_raw_statement);
    std::swap(left.values, right.values);
    std::swap(left.resolved_path, right.resolved_path);
    std::swap(left.primary_structure_field_count,
              right.primary_structure_field_count);
    std::swap(left.secondary_structure_field_count,
              right.secondary_structure_field_count);
    std::swap(left.secondary_row_deleted,
              right.secondary_row_deleted);
    return true;
}

bool clear_editable_list_draft_cell(
    std::vector<EditableListDraftRow>& rows,
    const std::vector<size_t>& visible_rows,
    int visible_row, int column) {
    if (visible_row < 0 || visible_row >= static_cast<int>(visible_rows.size()) ||
        column < 0) {
        return false;
    }
    EditableListDraftRow& row =
        rows[visible_rows[static_cast<size_t>(visible_row)]];
    if (row.deleted ||
        static_cast<size_t>(column) >= row.values.size()) {
        return false;
    }
    row.values[static_cast<size_t>(column)].clear();
    return true;
}

bool delete_editable_list_draft_row(
    std::vector<EditableListDraftRow>& rows,
    const std::vector<size_t>& visible_rows,
    int visible_row) {
    if (visible_row < 0 || visible_row >= static_cast<int>(visible_rows.size())) return false;
    EditableListDraftRow& row =
        rows[visible_rows[static_cast<size_t>(visible_row)]];
    if (row.deleted) return false;
    row.deleted = true;
    return true;
}

void rebuild_editable_list_display_rows(
    EditableListEditState& edit,
    const EditableListSpec& spec) {
    edit.display_rows.clear();
    if (!spec.numbered_structure_key_fields) return;
    edit.display_rows.reserve(
        edit.visible_rows.size() * 2);
    for (size_t visible_row = 0;
         visible_row < edit.visible_rows.size();
         ++visible_row) {
        const size_t draft_index =
            edit.visible_rows[visible_row];
        if (draft_index >= edit.rows.size()) continue;
        const EditableListDraftRow& row =
            edit.rows[draft_index];
        const std::string sequence =
            std::to_string(visible_row + 1);
        edit.display_rows.push_back(
            EditableListDisplayRow{
                visible_row, 1,
                row.primary_structure_field_count,
                sequence, false});
        if (row.secondary_structure_field_count != 0) {
            edit.display_rows.push_back(
                EditableListDisplayRow{
                    visible_row,
                    1 + row.primary_structure_field_count,
                    row.secondary_structure_field_count,
                    sequence + "F", true});
        }
    }
}

bool build_editable_list_pending_changes(
    const EditableListSpec& spec,
    const std::vector<EditableListDraftRow>& rows,
    const std::map<std::string, MapElementPendingChange>& existing_changes,
    std::map<std::string, MapElementPendingChange>& candidate_changes,
    std::string& error_message) {
    candidate_changes = existing_changes;
    for (const EditableListDraftRow& row : rows) {
        if (!editable_list_row_has_draft(row)) continue;
        if (row.target_edit_id.empty()) {
            error_message = std::string(spec.row_kind) +
                " draft has no target edit ID";
            return false;
        }

        auto existing = candidate_changes.find(row.target_edit_id);
        if (existing != candidate_changes.end() &&
            existing->second.row_kind != spec.row_kind) {
            error_message = std::string(spec.row_kind) +
                " draft conflicts with another pending row kind: " + row.target_edit_id;
            return false;
        }

        MapElementPendingChange change;
        if (existing != candidate_changes.end()) change = existing->second;
        change.change_id = std::string(spec.change_prefix) + row.target_edit_id;
        change.edit_id = row.target_edit_id;
        change.row_kind = spec.row_kind;
        if (change.expected_source_hash.empty()) {
            change.expected_source_hash = row.target_expected_source_hash;
        }

        if (row.deleted) {
            change.operation = "delete";
            change.field_changes.clear();
            change.replacement_statement.clear();
        } else {
            if (existing != candidate_changes.end() &&
                existing->second.operation != "update") {
                error_message = std::string(spec.row_kind) +
                    " draft conflicts with a pending " +
                    existing->second.operation + ": " + row.target_edit_id;
                return false;
            }
            change.operation = "update";
            const bool moved = row.payload_edit_id != row.target_edit_id;
            if (moved) {
                if (row.payload_source_file != row.target_source_file ||
                    row.payload_raw_statement.empty()) {
                    error_message = std::string(spec.row_kind) +
                        " row move lost its same-file source template: " +
                        row.target_edit_id;
                    return false;
                }
                change.field_changes.clear();
                for (size_t field = 0; field < row.values.size(); ++field) {
                    const std::string field_name =
                        editable_list_field_name(spec, field);
                    if (field_name.empty()) {
                        error_message = std::string(spec.row_kind) +
                            " draft has no field name for column " +
                            std::to_string(field);
                        return false;
                    }
                    change.field_changes[field_name] = row.values[field];
                }
                change.replacement_statement = row.payload_raw_statement;
            } else {
                if (row.values.size() != row.original_values.size()) {
                    error_message = std::string(spec.row_kind) +
                        " draft field count changed unexpectedly";
                    return false;
                }
                for (size_t field = 0; field < row.values.size(); ++field) {
                    if (row.values[field] != row.original_values[field]) {
                        const std::string field_name =
                            editable_list_field_name(spec, field);
                        if (field_name.empty()) {
                            error_message = std::string(spec.row_kind) +
                                " draft has no field name for column " +
                                std::to_string(field);
                            return false;
                        }
                        change.field_changes[field_name] = row.values[field];
                    }
                }
            }
            if (row.secondary_row_deleted) {
                change.field_changes["deleteGlare"] = "1";
            }
        }
        candidate_changes[row.target_edit_id] = std::move(change);
    }
    error_message.clear();
    return true;
}

bool App::has_unapplied_editable_list_drafts() const {
    return has_editable_list_drafts(
               station_definition_edit_, k_station_definition_edit_spec) ||
        has_editable_list_drafts(
               structure_model_edit_, k_structure_model_edit_spec) ||
        has_editable_list_drafts(
               signal_aspect_edit_, k_signal_aspect_edit_spec) ||
        has_editable_list_drafts(
               sound_list_edit_, k_sound_list_edit_spec) ||
        has_editable_list_drafts(
               sound_3d_list_edit_, k_sound_3d_list_edit_spec);
}

bool App::has_editable_list_drafts(const EditableListEditState& edit,
                                   const EditableListSpec& spec) const {
    (void)spec;
    if (!edit.editing_edit_id.empty() && edit.editing_column >= 0 &&
        edit.edit_buffer != edit.editing_baseline) {
        return true;
    }
    return edit.rows_initialized &&
        std::any_of(edit.rows.begin(), edit.rows.end(),
                    [](const EditableListDraftRow& row) {
                        return editable_list_row_has_draft(row);
                    });
}

bool App::initialize_editable_list_draft_rows(EditableListEditState& edit,
                                              const EditableListSpec& spec) {
    if (edit.rows_initialized) return true;
    ensure_table_cache();
    const std::vector<CachedTableRow>* cached_rows = nullptr;
    if (std::string_view(spec.row_kind) == "station.list") {
        cached_rows = &table_cache_.station_definition_rows;
    } else if (std::string_view(spec.row_kind) == "structure.model") {
        cached_rows = &table_cache_.structure_model_rows;
    } else if (std::string_view(spec.row_kind) == "signal.aspect") {
        cached_rows = &table_cache_.signal_aspect_rows;
    } else if (std::string_view(spec.row_kind) == "sound.list") {
        cached_rows = &table_cache_.sound_list_rows;
    } else if (std::string_view(spec.row_kind) == "sound3D.list") {
        cached_rows = &table_cache_.sound_3d_list_rows;
    }
    if (!cached_rows) return false;

    std::vector<EditableListDraftRow> rows;
    rows.reserve(cached_rows->size());
    for (const CachedTableRow& cached : *cached_rows) {
        if (cached.edit_id.empty()) {
            add_log("[error]gui_kme.cpp: " + std::string(spec.row_kind) +
                    " draft row has no edit ID");
            return false;
        }
        std::string metadata_error;
        const std::optional<InspectorTargetMetadata> metadata =
            resolve_inspector_target_metadata(handle_, cached.edit_id, spec.row_kind,
                                              &metadata_error);
        if (!metadata) {
            add_log("[error]gui_kme.cpp: " + std::string(spec.row_kind) +
                    " draft initialization failed: " +
                    (metadata_error.empty() ? std::string("metadata unavailable")
                                            : metadata_error));
            return false;
        }
        EditableListDraftRow row;
        row.target_edit_id = cached.edit_id;
        row.target_source_file = metadata->source.file_path;
        row.target_expected_source_hash = metadata->expected_source_hash;
        row.target_line = metadata->source.line;
        row.target_column = metadata->source.column;
        row.payload_edit_id = cached.edit_id;
        row.payload_source_file = metadata->source.file_path;
        row.payload_line = metadata->source.line;
        row.payload_column = metadata->source.column;
        row.payload_raw_statement = metadata->raw_statement;
        const size_t field_count = cached.editable_field_count != 0
            ? cached.editable_field_count
            : spec.field_count;
        row.values.resize(field_count);
        for (size_t field = 0; field < field_count; ++field) {
            const size_t cached_column = spec.cache_column_offset + field;
            if (cached_column < cached.cells.size()) {
                row.values[field] = cached.cells[cached_column];
            }
        }
        row.original_values = row.values;
        row.resolved_path = cached.open_path;
        row.primary_structure_field_count =
            cached.primary_structure_field_count;
        row.secondary_structure_field_count =
            cached.secondary_structure_field_count;
        rows.push_back(std::move(row));
    }
    edit.rows = std::move(rows);
    edit.visible_rows = editable_list_visible_row_indices(edit.rows);
    rebuild_editable_list_display_rows(edit, spec);
    edit.rows_initialized = true;
    return true;
}

void App::reset_editable_list_find_results(const EditableListSpec& spec) {
    if (std::string_view(spec.row_kind) == "structure.model") {
        reset_structure_model_find_results();
    } else if (std::string_view(spec.row_kind) == "sound.list") {
        reset_sound_file_find_results(false);
    } else if (std::string_view(spec.row_kind) == "sound3D.list") {
        reset_sound_file_find_results(true);
    } else if (std::string_view(spec.row_kind) == "signal.aspect") {
        reset_signal_aspect_find_results();
    }
}

void App::commit_editable_list_active_edit(EditableListEditState& edit,
                                           const EditableListSpec& spec) {
    if (!edit.editing_edit_id.empty() && edit.editing_column >= 0 &&
        initialize_editable_list_draft_rows(edit, spec)) {
        const auto row = std::find_if(
            edit.rows.begin(), edit.rows.end(), [&](const EditableListDraftRow& candidate) {
                return candidate.target_edit_id == edit.editing_edit_id;
            });
        if (row != edit.rows.end() && !row->deleted &&
            static_cast<size_t>(edit.editing_column) <
                row->values.size()) {
            row->values[static_cast<size_t>(edit.editing_column)] = edit.edit_buffer;
            if (edit.editing_column == spec.path_field) {
                row->resolved_path = resolve_list_asset_path(
                    row->target_source_file, edit.edit_buffer);
            }
        }
    }
    edit.editing_column = -1;
    edit.editing_edit_id.clear();
    edit.editing_baseline.clear();
    edit.edit_buffer.clear();
    edit.edit_buffer_fresh = false;
}

void App::discard_all_editable_list_drafts() {
    station_definition_edit_ = EditableListEditState{};
    structure_model_edit_ = EditableListEditState{};
    signal_aspect_edit_ = EditableListEditState{};
    sound_list_edit_ = EditableListEditState{};
    sound_3d_list_edit_ = EditableListEditState{};
}

bool App::move_editable_list_row(EditableListEditState& edit,
                                 const EditableListSpec& spec,
                                 int visible_row, int direction) {
    commit_editable_list_active_edit(edit, spec);
    if (!initialize_editable_list_draft_rows(edit, spec)) return false;
    if (!move_editable_list_draft_row(
            edit.rows, edit.visible_rows, visible_row, direction)) return false;
    edit.selected_row = visible_row + direction;
    rebuild_editable_list_display_rows(edit, spec);
    return true;
}

bool App::clear_editable_list_cell(EditableListEditState& edit,
                                   const EditableListSpec& spec,
                                   int visible_row, int column) {
    commit_editable_list_active_edit(edit, spec);
    if (!initialize_editable_list_draft_rows(edit, spec)) return false;
    if (!clear_editable_list_draft_cell(
            edit.rows, edit.visible_rows, visible_row, column)) return false;
    EditableListDraftRow& row =
        edit.rows[edit.visible_rows[static_cast<size_t>(visible_row)]];
    if (column == spec.path_field) row.resolved_path.clear();
    edit.selected_row = visible_row;
    edit.selected_column = column;
    return true;
}

bool App::choose_editable_list_file(EditableListEditState& edit,
                                    const EditableListSpec& spec,
                                    int visible_row) {
    commit_editable_list_active_edit(edit, spec);
    if (spec.path_field < 0 ||
        !initialize_editable_list_draft_rows(edit, spec) ||
        visible_row < 0 ||
        visible_row >= static_cast<int>(edit.visible_rows.size())) {
        return false;
    }
    EditableListDraftRow& row =
        edit.rows[edit.visible_rows[static_cast<size_t>(visible_row)]];
    if (row.deleted || row.target_edit_id.empty()) return false;

    const std::string selected_file = open_editable_list_file_dialog(
        spec, list_asset_picker_initial_directory(
                  row.resolved_path, row.target_source_file));
    if (selected_file.empty()) return false;

    ListAssetSourcePathResult selected_path =
        make_list_asset_source_path(row.target_source_file, selected_file);
    row.values[static_cast<size_t>(spec.path_field)] = selected_path.source_path;
    row.resolved_path = selected_path.resolved_path;
    edit.selected_row = visible_row;
    edit.selected_column =
        static_cast<int>(spec.cache_column_offset) + spec.path_field;

    if (!selected_path.fallback_reason.empty()) {
        add_log(
            LogSeverity::Warning,
            "[warning]gui_kme.cpp: unable to create a relative resource path; "
            "using absolute path: reason=\"" + selected_path.fallback_reason +
            "\", selected=\"" + selected_path.resolved_path +
            "\", list=\"" + row.target_source_file + "\"");
        set_program_status("status.edit.relative_path_fallback");
    }
    return true;
}

bool App::delete_editable_list_row(EditableListEditState& edit,
                                   const EditableListSpec& spec,
                                   int visible_row) {
    commit_editable_list_active_edit(edit, spec);
    if (!initialize_editable_list_draft_rows(edit, spec)) return false;
    if (!delete_editable_list_draft_row(
            edit.rows, edit.visible_rows, visible_row)) return false;
    edit.selected_row = visible_row;
    return true;
}

bool App::delete_editable_list_secondary_row(
    EditableListEditState& edit,
    const EditableListSpec& spec,
    int visible_row) {
    commit_editable_list_active_edit(edit, spec);
    if (!initialize_editable_list_draft_rows(edit, spec) ||
        visible_row < 0 ||
        visible_row >=
            static_cast<int>(edit.visible_rows.size())) {
        return false;
    }
    EditableListDraftRow& row =
        edit.rows[
            edit.visible_rows[
                static_cast<size_t>(visible_row)]];
    if (row.deleted || row.secondary_row_deleted ||
        row.secondary_structure_field_count == 0) {
        return false;
    }
    row.secondary_row_deleted = true;
    edit.selected_row = visible_row;
    edit.selected_secondary_row = true;
    return true;
}

void App::rebind_other_editable_list_drafts(
    const EditableListEditState* applied) {
    ensure_table_cache();
    struct Binding {
        EditableListEditState* edit;
        const EditableListSpec* spec;
        const std::vector<CachedTableRow>* cached_rows;
    };
    const std::array<Binding, 5> bindings = {{
        {&station_definition_edit_, &k_station_definition_edit_spec,
         &table_cache_.station_definition_rows},
        {&structure_model_edit_, &k_structure_model_edit_spec,
         &table_cache_.structure_model_rows},
        {&signal_aspect_edit_, &k_signal_aspect_edit_spec,
         &table_cache_.signal_aspect_rows},
        {&sound_list_edit_, &k_sound_list_edit_spec,
         &table_cache_.sound_list_rows},
        {&sound_3d_list_edit_, &k_sound_3d_list_edit_spec,
         &table_cache_.sound_3d_list_rows},
    }};
    for (const Binding& binding : bindings) {
        if (binding.edit == applied || !binding.edit->rows_initialized) continue;
        using LocationKey = std::tuple<std::string_view, int, int>;
        std::map<LocationKey, const CachedTableRow*> rows_by_location;
        for (const CachedTableRow& row : *binding.cached_rows) {
            rows_by_location.emplace(
                LocationKey{row.source.file_path, row.source.line, row.source.column},
                &row);
        }
        auto find_location = [&](const std::string& file, int line, int column)
            -> const CachedTableRow* {
            const auto found = rows_by_location.find(
                LocationKey{file, line, column});
            return found == rows_by_location.end() ? nullptr : found->second;
        };
        bool rebound = true;
        for (EditableListDraftRow& row : binding.edit->rows) {
            const CachedTableRow* target = find_location(
                row.target_source_file, row.target_line, row.target_column);
            const CachedTableRow* payload = find_location(
                row.payload_source_file, row.payload_line, row.payload_column);
            if (!target || !payload || target->edit_id.empty() ||
                payload->edit_id.empty()) {
                rebound = false;
                break;
            }
            if (binding.edit->editing_edit_id == row.target_edit_id) {
                binding.edit->editing_edit_id = target->edit_id;
            }
            row.target_edit_id = target->edit_id;
            row.payload_edit_id = payload->edit_id;
        }
        if (!rebound) {
            add_log("[error]gui_kme.cpp: discarded stale " +
                    std::string(binding.spec->row_kind) +
                    " drafts after source rows could not be rebound");
            *binding.edit = EditableListEditState{};
        }
    }
}

void App::apply_editable_list_drafts(EditableListEditState& edit,
                                     const EditableListSpec& spec) {
    if (!edit_actions_available()) return;
    commit_editable_list_active_edit(edit, spec);
    if (!has_editable_list_drafts(edit, spec)) return;

    std::map<std::string, MapElementPendingChange> candidate;
    std::string error;
    if (!build_editable_list_pending_changes(
            spec, edit.rows, pending_edit_changes_, candidate, error)) {
        add_log("[error]gui_kme.cpp: " + std::string(spec.row_kind) +
                " apply blocked: " + error);
        set_program_status("status.edit.pending");
        return;
    }
    if (apply_edit_ledger_to_preview(candidate, std::nullopt, false)) {
        EditableListEditState* applied = &edit;
        edit = EditableListEditState{};
        invalidate_table_cache();
        rebind_other_editable_list_drafts(applied);
        reset_editable_list_find_results(spec);
    }
    set_program_status("status.edit.pending");
}

std::string delete_expected_source_hash(
    const MapModel& model,
    const std::map<std::string, MapElementPendingChange>& pending_changes,
    void* handle,
    const MapElementDeleteRequest& request,
    std::string* metadata_error) {
    const auto pending = pending_changes.find(request.edit_id);
    if (pending != pending_changes.end() &&
        !pending->second.expected_source_hash.empty()) {
        return pending->second.expected_source_hash;
    }

    const std::optional<InspectorTargetMetadata> metadata =
        resolve_inspector_target_metadata(handle, request.edit_id, request.row_kind,
                                          metadata_error);
    std::string source_file_path = metadata ? metadata->source.file_path : std::string{};
    const std::vector<TableRow>* rows = inspector_rows_for_kind(model, request.row_kind);
    if (source_file_path.empty() && rows) {
        size_t row_index = 0;
        if (find_row_index_by_edit_id(*rows, request.edit_id, row_index)) {
            source_file_path = (*rows)[row_index].source.file_path;
        }
    }
    return expected_source_hash_for_edit_target(
        model, pending_changes, request.edit_id,
        metadata ? metadata->expected_source_hash : std::string{}, source_file_path);
}
