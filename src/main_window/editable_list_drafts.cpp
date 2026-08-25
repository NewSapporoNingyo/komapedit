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
    return row.inserted || row.deleted || row.secondary_row_deleted ||
        row.secondary_row_added ||
        row.payload_edit_id != row.target_edit_id ||
        row.values != row.original_values;
}

std::optional<ResourceListKind> resource_list_kind_for_edit_spec(
    const EditableListSpec& spec) {
    if (std::string_view(spec.row_kind) == "station.list") {
        return ResourceListKind::Station;
    }
    if (std::string_view(spec.row_kind) == "structure.model") {
        return ResourceListKind::Structure;
    }
    if (std::string_view(spec.row_kind) == "signal.aspect") {
        return ResourceListKind::Signal;
    }
    if (std::string_view(spec.row_kind) == "sound.list") return ResourceListKind::Sound;
    if (std::string_view(spec.row_kind) == "sound3D.list") {
        return ResourceListKind::Sound3D;
    }
    return std::nullopt;
}

const std::string& editable_list_row_identity(const EditableListDraftRow& row) {
    return row.target_edit_id.empty() ? row.local_draft_id : row.target_edit_id;
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
    if (left.inserted || right.inserted) {
        std::swap(left, right);
        return true;
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
    std::swap(left.secondary_row_added,
              right.secondary_row_added);
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
    const auto set_field = [&](MapElementPendingChange& change,
                               size_t field, const std::string& value) {
        const std::string field_name = editable_list_field_name(spec, field);
        if (field_name.empty()) {
            error_message = std::string(spec.row_kind) +
                " draft has no field name for column " +
                std::to_string(field);
            return false;
        }
        change.field_changes[field_name] = value;
        return true;
    };
    const auto validate_insert_shape = [&](const EditableListDraftRow& row) {
        if (!spec.numbered_structure_key_fields) {
            if (row.values.size() != spec.field_count) {
                error_message = std::string(spec.row_kind) +
                    " insert has an invalid field count";
                return false;
            }
            return true;
        }
        const size_t glare_count = row.secondary_row_added
            ? row.secondary_structure_field_count : 0;
        if (row.primary_structure_field_count != 5 ||
            (glare_count != 0 && glare_count != 5) ||
            row.values.size() != 6 + glare_count) {
            error_message = "signal.aspect insert must contain one aspect key and five structure keys";
            return false;
        }
        return true;
    };
    const auto append_all_fields = [&](MapElementPendingChange& change,
                                       const EditableListDraftRow& row) {
        for (size_t field = 0; field < row.values.size(); ++field) {
            if (!set_field(change, field, row.values[field])) return false;
        }
        return true;
    };
    for (size_t row_index = 0; row_index < rows.size(); ++row_index) {
        const EditableListDraftRow& row = rows[row_index];
        if (!editable_list_row_has_draft(row)) continue;
        if (row.inserted) {
            if (row.deleted) continue;
            if (row.local_draft_id.empty() || row.target_source_file.empty()) {
                error_message = std::string(spec.row_kind) +
                    " insert has no local ID or source file";
                return false;
            }
            if (!validate_insert_shape(row)) return false;

            MapElementPendingChange change;
            change.change_id = std::string(spec.change_prefix) + row.local_draft_id;
            change.edit_id = row.local_draft_id;
            change.row_kind = spec.row_kind;
            change.operation = "insert";
            change.target_file_path = row.target_source_file;
            change.expected_source_hash = row.target_expected_source_hash;
            change.resource_list_insert_order =
                static_cast<std::uint64_t>(row_index + 1);
            if (!append_all_fields(change, row)) return false;
            if (row.secondary_row_added) {
                const size_t glare_begin = row.values.size() -
                    row.secondary_structure_field_count;
                const bool has_glare_key = std::any_of(
                    row.values.begin() + static_cast<std::ptrdiff_t>(glare_begin),
                    row.values.end(),
                    [](const std::string& value) { return !value.empty(); });
                if (!has_glare_key) {
                    error_message = "signal.aspect glare requires at least one structure key";
                    return false;
                }
                change.field_changes["addGlare"] =
                    std::to_string(row.secondary_structure_field_count);
            }
            for (size_t next = row_index + 1; next < rows.size(); ++next) {
                const EditableListDraftRow& anchor = rows[next];
                if (!anchor.inserted && !anchor.deleted &&
                    anchor.target_source_file == row.target_source_file &&
                    !anchor.target_edit_id.empty()) {
                    change.insert_before_edit_id = anchor.target_edit_id;
                    break;
                }
            }
            candidate_changes[row.local_draft_id] = std::move(change);
            continue;
        }
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

        const bool pending_insert = existing != candidate_changes.end() &&
            existing->second.operation == "insert";
        if (row.deleted && pending_insert) {
            candidate_changes.erase(existing);
            continue;
        }
        if (row.deleted) {
            change.operation = "delete";
            change.field_changes.clear();
            change.replacement_statement.clear();
        } else {
            if (existing != candidate_changes.end() &&
                existing->second.operation != "update" && !pending_insert) {
                error_message = std::string(spec.row_kind) +
                    " draft conflicts with a pending " +
                    existing->second.operation + ": " + row.target_edit_id;
                return false;
            }
            change.operation = pending_insert ? "insert" : "update";
            const bool moved = row.payload_edit_id != row.target_edit_id;
            if (pending_insert) {
                if (!validate_insert_shape(row)) return false;
                change.field_changes.clear();
                change.replacement_statement.clear();
                if (!append_all_fields(change, row)) return false;
                if (spec.numbered_structure_key_fields &&
                    row.secondary_structure_field_count != 0) {
                    const size_t glare_begin = row.values.size() -
                        row.secondary_structure_field_count;
                    const bool has_glare_key = std::any_of(
                        row.values.begin() + static_cast<std::ptrdiff_t>(glare_begin),
                        row.values.end(),
                        [](const std::string& value) { return !value.empty(); });
                    if (!has_glare_key) {
                        error_message = "signal.aspect glare requires at least one structure key";
                        return false;
                    }
                    change.field_changes["addGlare"] =
                        std::to_string(row.secondary_structure_field_count);
                }
            } else if (moved) {
                if (row.payload_source_file != row.target_source_file ||
                    row.payload_raw_statement.empty()) {
                    error_message = std::string(spec.row_kind) +
                        " row move lost its same-file source template: " +
                        row.target_edit_id;
                    return false;
                }
                change.field_changes.clear();
                for (size_t field = 0; field < row.values.size(); ++field) {
                    if (!set_field(change, field, row.values[field])) return false;
                }
                change.replacement_statement = row.payload_raw_statement;
            } else {
                const size_t expected_field_count = row.original_values.size() +
                    (row.secondary_row_added
                        ? row.secondary_structure_field_count : 0);
                if (row.values.size() != expected_field_count) {
                    error_message = std::string(spec.row_kind) +
                        " draft field count changed unexpectedly";
                    return false;
                }
                for (size_t field = 0; field < row.values.size(); ++field) {
                    if (field >= row.original_values.size() ||
                        row.values[field] != row.original_values[field]) {
                        if (!set_field(change, field, row.values[field])) return false;
                    }
                }
            }
            if (row.secondary_row_deleted) {
                change.field_changes["deleteGlare"] = "1";
            }
            if (row.secondary_row_added) {
                const size_t glare_begin = row.values.size() -
                    row.secondary_structure_field_count;
                const bool has_glare_key = std::any_of(
                    row.values.begin() + static_cast<std::ptrdiff_t>(glare_begin),
                    row.values.end(),
                    [](const std::string& value) { return !value.empty(); });
                if (!has_glare_key) {
                    error_message = "signal.aspect glare requires at least one structure key";
                    return false;
                }
                change.field_changes["addGlare"] =
                    std::to_string(row.secondary_structure_field_count);
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
                return editable_list_row_identity(candidate) ==
                    edit.editing_edit_id;
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

void App::run_pending_editable_list_actions() {
    if (pending_editable_list_actions_.empty()) return;
    // Take the queue so an action that triggers another deferred push (none
    // today) can never recurse into itself while it is being consumed.
    std::vector<DeferredEditableListAction> actions =
        std::move(pending_editable_list_actions_);
    pending_editable_list_actions_.clear();
    for (const DeferredEditableListAction& action : actions) {
        if (!action.edit || !action.spec) continue;
        EditableListEditState& edit = *action.edit;
        const EditableListSpec& spec = *action.spec;
        switch (action.kind) {
        case DeferredEditableListAction::Kind::InsertAbove:
            insert_editable_list_row(edit, spec, action.visible_row, true);
            break;
        case DeferredEditableListAction::Kind::InsertBelow:
            insert_editable_list_row(edit, spec, action.visible_row, false);
            break;
        case DeferredEditableListAction::Kind::MoveUp:
            move_editable_list_row(edit, spec, action.visible_row, -1);
            break;
        case DeferredEditableListAction::Kind::MoveDown:
            move_editable_list_row(edit, spec, action.visible_row, 1);
            break;
        case DeferredEditableListAction::Kind::ClearCell:
            if (clear_editable_list_cell(edit, spec, action.visible_row,
                                         action.column) &&
                action.select_secondary) {
                edit.selected_secondary_row = true;
            }
            break;
        case DeferredEditableListAction::Kind::DeleteRow:
            delete_editable_list_row(edit, spec, action.visible_row);
            break;
        case DeferredEditableListAction::Kind::ChooseFile:
            choose_editable_list_file(edit, spec, action.visible_row);
            break;
        case DeferredEditableListAction::Kind::AddGlare:
            add_editable_list_secondary_row(edit, spec, action.visible_row);
            break;
        case DeferredEditableListAction::Kind::DeleteGlare:
            delete_editable_list_secondary_row(edit, spec, action.visible_row);
            break;
        }
    }
}

bool App::move_editable_list_row(EditableListEditState& edit,
                                 const EditableListSpec& spec,
                                 int visible_row, int direction) {
    commit_editable_list_active_edit(edit, spec);
    if (!initialize_editable_list_draft_rows(edit, spec)) return false;
    if (!move_editable_list_draft_row(
            edit.rows, edit.visible_rows, visible_row, direction)) return false;
    edit.visible_rows = editable_list_visible_row_indices(edit.rows);
    edit.selected_row = visible_row + direction;
    edit.selected_secondary_row = false;
    rebuild_editable_list_display_rows(edit, spec);
    return true;
}

bool App::append_empty_list_row_draft(EditableListEditState& edit,
                                      const EditableListSpec& spec) {
    // A list file that only carries its header has no existing row to anchor
    // to. Take the target file and disk-baseline hash from the corresponding
    // ResourceListSource so the first draft row can still be created.
    const std::optional<ResourceListKind> kind =
        resource_list_kind_for_edit_spec(spec);
    if (!kind || !has_model_) return false;
    const ResourceListSource& source =
        model_.resource_list_sources[static_cast<size_t>(*kind)];
    if (!source.present || source.resolved_path.empty()) return false;

    EditableListDraftRow row;
    const std::string local_prefix = std::string(spec.change_prefix) + "draft-";
    do {
        row.local_draft_id = local_prefix +
            std::to_string(edit.next_local_draft_id++);
    } while (pending_edit_changes_.find(row.local_draft_id) !=
                 pending_edit_changes_.end() ||
             std::any_of(edit.rows.begin(), edit.rows.end(),
                         [&](const EditableListDraftRow& candidate) {
                             return editable_list_row_identity(candidate) ==
                                 row.local_draft_id;
                         }));
    row.target_source_file = source.resolved_path;
    for (const EditSourceFileInfo& file : model_.edit_files) {
        if (file.file_path == source.resolved_path) {
            row.target_expected_source_hash = file.source_hash;
            break;
        }
    }
    row.inserted = true;
    if (spec.numbered_structure_key_fields) {
        row.values.assign(6, {});
        row.primary_structure_field_count = 5;
    } else {
        row.values.assign(spec.field_count, {});
    }
    row.original_values = row.values;
    edit.rows.push_back(std::move(row));
    edit.visible_rows = editable_list_visible_row_indices(edit.rows);
    edit.selected_row = static_cast<int>(edit.visible_rows.size()) - 1;
    edit.selected_column = 0;
    edit.selected_secondary_row = false;
    rebuild_editable_list_display_rows(edit, spec);
    return true;
}

bool App::insert_editable_list_row(EditableListEditState& edit,
                                   const EditableListSpec& spec,
                                   int visible_row, bool above) {
    commit_editable_list_active_edit(edit, spec);
    if (!initialize_editable_list_draft_rows(edit, spec)) return false;
    if (edit.visible_rows.empty()) {
        return append_empty_list_row_draft(edit, spec);
    }
    if (visible_row < 0 ||
        visible_row >= static_cast<int>(edit.visible_rows.size())) {
        return false;
    }
    const size_t anchor_index =
        edit.visible_rows[static_cast<size_t>(visible_row)];
    if (anchor_index >= edit.rows.size()) return false;
    const EditableListDraftRow& anchor = edit.rows[anchor_index];
    if (anchor.deleted || anchor.target_source_file.empty()) return false;

    EditableListDraftRow row;
    const std::string local_prefix = std::string(spec.change_prefix) + "draft-";
    do {
        row.local_draft_id = local_prefix +
            std::to_string(edit.next_local_draft_id++);
    } while (pending_edit_changes_.find(row.local_draft_id) !=
                 pending_edit_changes_.end() ||
             std::any_of(edit.rows.begin(), edit.rows.end(),
                         [&](const EditableListDraftRow& candidate) {
                             return editable_list_row_identity(candidate) ==
                                 row.local_draft_id;
                         }));
    row.target_source_file = anchor.target_source_file;
    row.target_expected_source_hash = anchor.target_expected_source_hash;
    row.inserted = true;
    if (spec.numbered_structure_key_fields) {
        row.values.assign(6, {});
        row.primary_structure_field_count = 5;
    } else {
        row.values.assign(spec.field_count, {});
    }
    row.original_values = row.values;
    const size_t insert_index = above ? anchor_index : anchor_index + 1;
    edit.rows.insert(
        edit.rows.begin() + static_cast<std::ptrdiff_t>(insert_index),
        std::move(row));
    edit.visible_rows = editable_list_visible_row_indices(edit.rows);
    edit.selected_row = above ? visible_row : visible_row + 1;
    edit.selected_column = 0;
    edit.selected_secondary_row = false;
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
    if (row.deleted || row.target_source_file.empty() ||
        static_cast<size_t>(spec.path_field) >= row.values.size()) return false;

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
    if (visible_row < 0 ||
        visible_row >= static_cast<int>(edit.visible_rows.size())) return false;
    const size_t draft_index = edit.visible_rows[static_cast<size_t>(visible_row)];
    if (draft_index >= edit.rows.size()) return false;
    if (edit.rows[draft_index].inserted) {
        edit.rows.erase(edit.rows.begin() + static_cast<std::ptrdiff_t>(draft_index));
        edit.visible_rows = editable_list_visible_row_indices(edit.rows);
        edit.selected_row = std::min(
            visible_row, static_cast<int>(edit.visible_rows.size()) - 1);
        edit.selected_column = -1;
        edit.selected_secondary_row = false;
        rebuild_editable_list_display_rows(edit, spec);
        return true;
    }
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
    if (row.secondary_row_added) {
        if (row.values.size() < row.secondary_structure_field_count) return false;
        row.values.resize(
            row.values.size() - row.secondary_structure_field_count);
        row.secondary_structure_field_count = 0;
        row.secondary_row_added = false;
        edit.selected_row = visible_row;
        edit.selected_secondary_row = false;
        rebuild_editable_list_display_rows(edit, spec);
        return true;
    }
    row.secondary_row_deleted = true;
    edit.selected_row = visible_row;
    edit.selected_secondary_row = true;
    return true;
}

bool App::add_editable_list_secondary_row(
    EditableListEditState& edit,
    const EditableListSpec& spec,
    int visible_row) {
    commit_editable_list_active_edit(edit, spec);
    if (!spec.numbered_structure_key_fields ||
        !initialize_editable_list_draft_rows(edit, spec) ||
        visible_row < 0 ||
        visible_row >= static_cast<int>(edit.visible_rows.size())) {
        return false;
    }
    EditableListDraftRow& row = edit.rows[
        edit.visible_rows[static_cast<size_t>(visible_row)]];
    if (row.deleted || row.secondary_row_deleted ||
        row.secondary_structure_field_count != 0 ||
        row.primary_structure_field_count == 0) {
        return false;
    }
    row.values.resize(
        row.values.size() + row.primary_structure_field_count);
    row.secondary_structure_field_count = row.primary_structure_field_count;
    row.secondary_row_added = true;
    edit.selected_row = visible_row;
    edit.selected_secondary_row = true;
    rebuild_editable_list_display_rows(edit, spec);
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
            if (row.inserted) continue;
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
