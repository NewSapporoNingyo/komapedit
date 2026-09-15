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
#include "datatable_internal.h"
#include "touch_input.h"

#include "canvas3D.h"
#include "text_decoder.h"
#include "imgui.h"
#include "misc/cpp/imgui_stdlib.h"
#include "repeater_linkage.h"

#include <windows.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <initializer_list>
#include <map>
#include <string>
#include <system_error>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

using namespace datatable_internal;

namespace {
template <typename NoteUsedKeysFn, typename UndefinedKeyFn>
void run_unused_key_search(TableFindState& state,
                           const TableFindRowsView& definition_rows,
                           size_t definition_key_column,
                           NoteUsedKeysFn note_used_keys,
                           UndefinedKeyFn on_undefined_key) {
    state.exact = true;
    state.committed.clear();
    state.matches.clear();
    state.row_matches.clear();
    state.current = -1;
    state.scroll_row = -1;
    state.has_run = false;
    state.unused_row_matches.assign(definition_rows.size(), 0);
    state.unused_count = 0;
    state.unused_total = 0;
    state.unused_has_run = true;

    std::unordered_map<std::string, std::vector<size_t>> rows_by_key;
    rows_by_key.reserve(definition_rows.size() * 2 + 1);
    for (size_t row_index = 0; row_index < definition_rows.size(); ++row_index) {
        if (!definition_rows.searchable(row_index)) continue;
        ++state.unused_total;
        const std::string* key =
            definition_rows.cell(row_index, definition_key_column);
        if (!key || blank_ascii(*key)) continue;
        rows_by_key[ascii_case_key(*key)].push_back(row_index);
    }

    std::vector<unsigned char> used_rows(definition_rows.size(), 0);
    std::unordered_set<std::string> warned_undefined_keys;
    auto note_key = [&](const std::string& raw_key) {
        std::string key = trim_gui_ascii_copy(raw_key);
        if (key.empty()) return;
        std::string folded_key = ascii_case_key(key);
        auto match = rows_by_key.find(folded_key);
        if (match == rows_by_key.end()) {
            if (warned_undefined_keys.insert(folded_key).second) {
                on_undefined_key(key);
            }
            return;
        }
        for (size_t row_index : match->second) used_rows[row_index] = 1;
    };

    note_used_keys(note_key);

    for (size_t row_index = 0; row_index < definition_rows.size(); ++row_index) {
        if (!definition_rows.searchable(row_index)) continue;
        if (used_rows[row_index]) continue;
        state.unused_row_matches[row_index] = 1;
        if (state.scroll_row < 0) state.scroll_row = static_cast<int>(row_index);
        ++state.unused_count;
    }
}
} // namespace

void App::reset_structure_model_find_results() {
    reset_table_find_results(structure_model_find_);
}

void App::run_structure_model_find() {
    commit_editable_list_active_edit(
        structure_model_edit_, k_structure_model_edit_spec);
    ensure_table_cache();
    run_table_find(structure_model_find_,
                   editable_table_find_rows(
                       table_cache_.structure_model_rows,
                       structure_model_edit_,
                       k_structure_model_edit_spec),
                   {static_cast<size_t>(k_structure_model_key_column),
                    static_cast<size_t>(k_structure_model_file_path_column)});
}

void App::run_unused_structure_model_search() {
    commit_editable_list_active_edit(
        structure_model_edit_, k_structure_model_edit_spec);
    ensure_table_cache();
    KME_ADD_LOG("[INFO]Searching unused models...");

    run_unused_key_search(
        structure_model_find_,
        editable_table_find_rows(
            table_cache_.structure_model_rows,
            structure_model_edit_,
            k_structure_model_edit_spec),
        static_cast<size_t>(k_structure_model_key_column),
        [this](auto& note_structure_key) {
            auto note_structure_rows = [&](const std::vector<CachedTableRow>& rows) {
                for (const CachedTableRow& row : rows) {
                    if (row.cells.size() > static_cast<size_t>(k_structure_key_column)) {
                        note_structure_key(row.cells[static_cast<size_t>(k_structure_key_column)]);
                    }
                }
            };
            note_structure_rows(table_cache_.structure_rows);
            note_structure_rows(table_cache_.structure_between_rows);
            for (const CachedTableRow& row : table_cache_.repeater_rows) {
                if (row.cells.size() <= static_cast<size_t>(k_repeater_structure_keys_column)) continue;
                for (const std::string& key :
                     split_structure_key_list(row.cells[static_cast<size_t>(k_repeater_structure_keys_column)])) {
                    note_structure_key(key);
                }
            }
            for (const CachedTableRow& row : table_cache_.background_rows) {
                if (row.cells.size() > static_cast<size_t>(k_background_structure_key_column)) {
                    note_structure_key(row.cells[static_cast<size_t>(k_background_structure_key_column)]);
                }
            }
            if (signal_aspect_edit_.rows_initialized) {
                for (size_t visible_index :
                     signal_aspect_edit_.visible_rows) {
                    if (visible_index >=
                        signal_aspect_edit_.rows.size()) {
                        continue;
                    }
                    const EditableListDraftRow& row =
                        signal_aspect_edit_.rows[visible_index];
                    if (row.deleted) continue;
                    const size_t main_end = std::min(
                        row.values.size(),
                        1 + row.primary_structure_field_count);
                    for (size_t field = 1;
                         field < main_end; ++field) {
                        note_structure_key(row.values[field]);
                    }
                    if (!row.secondary_row_deleted) {
                        const size_t glare_end = std::min(
                            row.values.size(),
                            main_end +
                                row.secondary_structure_field_count);
                        for (size_t field = main_end;
                             field < glare_end; ++field) {
                            note_structure_key(row.values[field]);
                        }
                    }
                }
            } else {
                for (const CachedTableRow& row :
                     table_cache_.signal_aspect_rows) {
                    for (size_t i =
                             k_signal_aspect_structure_key_column_offset;
                         i < row.cells.size(); ++i) {
                        note_structure_key(row.cells[i]);
                    }
                }
            }
            for (const TableRow& row : model_.other_train_structure_keys) {
                note_structure_key(table_cell(row, "key"));
            }
        },
        [this](const std::string& key) {
            KME_ADD_LOG("[WARN]Found undefined structureKey:\"" +
                    key + "\"");
        });

    if (structure_model_find_.unused_count == 0) {
        KME_ADD_LOG("[INFO]No unused models found");
    } else {
        KME_ADD_LOG("[INFO]Found unused models (" +
                std::to_string(structure_model_find_.unused_count) + "/" +
                std::to_string(structure_model_find_.unused_total) + ")");
    }
}

void App::find_structure_model_for_structure_key(const std::string& structure_key) {
    if (blank_ascii(structure_key)) return;
    set_exact_table_find_query(structure_model_find_, structure_key);
    show_structure_models_window_ = true;
    run_structure_model_find();
}

void App::step_structure_model_find(int delta) {
    step_table_find(structure_model_find_, delta);
}

std::string App::structure_model_find_status_text() const {
    return table_find_status_text(structure_model_find_,
                                  tr("status.find.no_match"),
                                  tr("status.find.match"),
                                  tr("status.unused_structure_models.no_match"),
                                  tr("status.unused_structure_models.match"));
}

void App::reset_signal_aspect_find_results() {
    reset_table_find_results(signal_aspect_find_);
}

void App::run_signal_aspect_find() {
    commit_editable_list_active_edit(
        signal_aspect_edit_, k_signal_aspect_edit_spec);
    ensure_table_cache();
    run_table_find(
        signal_aspect_find_,
        editable_table_find_rows(
            table_cache_.signal_aspect_rows,
            signal_aspect_edit_,
            k_signal_aspect_edit_spec),
        {static_cast<size_t>(k_signal_aspect_key_column)});
}

void App::run_unused_signal_aspect_search() {
    commit_editable_list_active_edit(
        signal_aspect_edit_, k_signal_aspect_edit_spec);
    ensure_table_cache();
    KME_ADD_LOG("[INFO]Searching unused signal aspects...");

    run_unused_key_search(
        signal_aspect_find_,
        editable_table_find_rows(
            table_cache_.signal_aspect_rows,
            signal_aspect_edit_,
            k_signal_aspect_edit_spec),
        static_cast<size_t>(k_signal_aspect_key_column),
        [this](auto& note_signal_aspect_key) {
            for (const CachedTableRow& row : table_cache_.signal_rows) {
                if (row.cells.size() > static_cast<size_t>(k_signal_signal_aspect_key_column)) {
                    note_signal_aspect_key(row.cells[static_cast<size_t>(k_signal_signal_aspect_key_column)]);
                }
            }
        },
        [this](const std::string& key) {
            KME_ADD_LOG("[WARN]Found undefined signalAspectKey:\"" + key + "\"");
        });

    if (signal_aspect_find_.unused_count == 0) {
        KME_ADD_LOG("[INFO]No unused signal aspects found");
    } else {
        KME_ADD_LOG("[INFO]Found unused signal aspects (" +
                std::to_string(signal_aspect_find_.unused_count) + "/" +
                std::to_string(signal_aspect_find_.unused_total) + ")");
    }
}

void App::find_signal_aspect_for_signal_aspect_key(const std::string& signal_aspect_key) {
    if (blank_ascii(signal_aspect_key)) return;
    set_exact_table_find_query(signal_aspect_find_, signal_aspect_key);
    show_signal_aspects_window_ = true;
    focus_signal_aspects_next_ = true;
    run_signal_aspect_find();
}

void App::step_signal_aspect_find(int delta) {
    step_table_find(signal_aspect_find_, delta);
}

std::string App::signal_aspect_find_status_text() const {
    return table_find_status_text(signal_aspect_find_,
                                  tr("status.find.no_match"),
                                  tr("status.find.match"),
                                  tr("status.unused_signal_aspects.no_match"),
                                  tr("status.unused_signal_aspects.match"));
}

void App::reset_sound_file_find_results(bool is_3d) {
    TableFindState& state = is_3d ? sound_3d_file_find_ : sound_file_find_;
    reset_table_find_results(state);
}

void App::run_sound_file_find(bool is_3d) {
    EditableListEditState& edit =
        is_3d ? sound_3d_list_edit_ : sound_list_edit_;
    const EditableListSpec& spec =
        is_3d ? k_sound_3d_list_edit_spec : k_sound_list_edit_spec;
    commit_editable_list_active_edit(edit, spec);
    ensure_table_cache();
    TableFindState& state = is_3d ? sound_3d_file_find_ : sound_file_find_;
    const std::vector<CachedTableRow>& rows = is_3d
        ? table_cache_.sound_3d_list_rows
        : table_cache_.sound_list_rows;

    run_table_find(state,
                   editable_table_find_rows(rows, edit, spec),
                   {static_cast<size_t>(k_sound_list_key_column),
                    static_cast<size_t>(k_sound_list_file_path_column)});
}

void App::run_unused_sound_file_search(bool is_3d) {
    EditableListEditState& edit =
        is_3d ? sound_3d_list_edit_ : sound_list_edit_;
    const EditableListSpec& spec =
        is_3d ? k_sound_3d_list_edit_spec : k_sound_list_edit_spec;
    commit_editable_list_active_edit(edit, spec);
    if (!is_3d) {
        commit_editable_list_active_edit(station_definition_edit_, k_station_definition_edit_spec);
    }
    ensure_table_cache();
    KME_ADD_LOG(is_3d
        ? "[INFO]Searching unused 3D sounds..."
        : "[INFO]Searching unused sounds...");

    TableFindState& state = is_3d ? sound_3d_file_find_ : sound_file_find_;
    const std::vector<CachedTableRow>& file_rows = is_3d
        ? table_cache_.sound_3d_list_rows
        : table_cache_.sound_list_rows;
    const std::vector<CachedTableRow>& usage_rows = is_3d
        ? table_cache_.map_sound_3d_rows
        : table_cache_.map_sound_rows;

    run_unused_key_search(
        state,
        editable_table_find_rows(file_rows, edit, spec),
        static_cast<size_t>(k_sound_list_key_column),
        [&](auto& note_sound_key) {
            for (const CachedTableRow& row : usage_rows) {
                if (row.cells.size() > static_cast<size_t>(k_map_sound_key_column)) {
                    note_sound_key(row.cells[static_cast<size_t>(k_map_sound_key_column)]);
                }
            }
            if (is_3d) {
                for (const TableRow& row : model_.other_train_sound_3d_keys) {
                    note_sound_key(table_cell(row, "key"));
                }
            } else {
                const auto station_rows = editable_table_find_rows(
                    table_cache_.station_definition_rows,
                    station_definition_edit_, k_station_definition_edit_spec);
                for (size_t row = 0; row < station_rows.size(); ++row) {
                    if (!station_rows.searchable(row)) continue;
                    for (const size_t column : {k_station_definition_arrival_sound_column,
                                                k_station_definition_departure_sound_column}) {
                        if (const std::string* key = station_rows.cell(row, column)) {
                            note_sound_key(*key);
                        }
                    }
                }
            }
        },
        [this, is_3d](const std::string& key) {
            KME_ADD_LOG(std::string("[WARN]Found undefined ") +
                    (is_3d ? "3D " : "") + "soundKey:\"" + key + "\"");
        });

    if (state.unused_count == 0) {
        KME_ADD_LOG(is_3d
            ? "[INFO]No unused 3D sounds found"
            : "[INFO]No unused sounds found");
    } else {
        KME_ADD_LOG(std::string("[INFO]Found unused ") +
                (is_3d ? "3D sounds (" : "sounds (") +
                std::to_string(state.unused_count) + "/" +
                std::to_string(state.unused_total) + ")");
    }
}

void App::find_sound_file_for_sound_key(const std::string& sound_key, bool is_3d) {
    if (blank_ascii(sound_key)) return;
    TableFindState& state = is_3d ? sound_3d_file_find_ : sound_file_find_;
    set_exact_table_find_query(state, sound_key);
    if (is_3d) {
        show_sound_3d_list_window_ = true;
    } else {
        show_sound_list_window_ = true;
    }
    run_sound_file_find(is_3d);
}

void App::step_sound_file_find(bool is_3d, int delta) {
    TableFindState& state = is_3d ? sound_3d_file_find_ : sound_file_find_;
    step_table_find(state, delta);
}

std::string App::sound_file_find_status_text(bool is_3d) const {
    const TableFindState& state = is_3d ? sound_3d_file_find_ : sound_file_find_;
    return table_find_status_text(state,
                                  tr("status.find.no_match"),
                                  tr("status.find.match"),
                                  tr(is_3d ? "status.unused_sound_3d_files.no_match"
                                           : "status.unused_sound_files.no_match"),
                                  tr(is_3d ? "status.unused_sound_3d_files.match"
                                           : "status.unused_sound_files.match"));
}

