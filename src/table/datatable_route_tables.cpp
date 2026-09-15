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

void App::render_othertracks_window() {
    if (!show_othertracks_window_) return;
    if (dock_right_id_) ImGui::SetNextWindowDockID(dock_right_id_, ImGuiCond_FirstUseEver);
    std::string title = tr("frame.othertracks") + "###OtherTracks";
    if (ImGui::Begin(title.c_str(), &show_othertracks_window_)) {
        if (!has_model_) {
            ImGui::TextDisabled("-");
        } else {
            bool scene_track_visibility_changed = false;
            bool all_visible = !model_.other_tracks.empty() &&
                std::all_of(model_.other_tracks.begin(), model_.other_tracks.end(),
                            [](const OtherTrack& t) { return t.visible; });
            ImGui::BeginDisabled(model_.other_tracks.empty());
            if (ImGui::Checkbox(tr("chk.select_all").c_str(), &all_visible)) {
                for (auto& t : model_.other_tracks) t.visible = all_visible;
                scene_track_visibility_changed = true;
            }
            ImGui::EndDisabled();
            if (ImGui::BeginTable("othertracks", 5, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY)) {
                ImGui::TableSetupColumn((tr("column.show") + "###OtherTracksShow").c_str());
                ImGui::TableSetupColumn((tr("column.key") + "###OtherTracksKey").c_str());
                ImGui::TableSetupColumn((tr("column.from") + "###OtherTracksFrom").c_str());
                ImGui::TableSetupColumn((tr("column.to") + "###OtherTracksTo").c_str());
                ImGui::TableSetupColumn((tr("column.color") + "###OtherTracksColor").c_str());
                setup_fixed_table_header();
                ImGui::TableHeadersRow();
                ImGuiListClipper clipper;
                clipper.Begin(static_cast<int>(model_.other_tracks.size()));
                while (clipper.Step()) {
                    for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
                        OtherTrack& t = model_.other_tracks[static_cast<size_t>(row)];
                        ImGui::TableNextRow();
                        ImGui::TableSetColumnIndex(0);
                        ImGui::PushID(t.key.c_str());
                        if (ImGui::Checkbox("##show", &t.visible)) {
                            scene_track_visibility_changed = true;
                        }
                        ImGui::TableSetColumnIndex(1);
                        const std::string display_key = t.key.empty() ? "\\" : t.key;
                        if (begin_text_cell_context_popup(
                                display_key, "other_track_key_item",
                                "other_track_key_context")) {
                            const bool can_rename = edit_actions_available() && !t.key.empty();
                            ImGui::BeginDisabled(!can_rename);
                            if (ImGui::MenuItem(
                                    tr("context.other_track.rename").c_str())) {
                                request_other_track_rename(t.key);
                            }
                            ImGui::EndDisabled();
                            ImGui::EndPopup();
                        }
                        ImGui::TableSetColumnIndex(2);
                        ImGui::SetNextItemWidth(-1);
                        ImGui::InputDouble("##min", &t.range_min, 0, 0, "%.1f");
                        ImGui::TableSetColumnIndex(3);
                        ImGui::SetNextItemWidth(-1);
                        ImGui::InputDouble("##max", &t.range_max, 0, 0, "%.1f");
                        ImGui::TableSetColumnIndex(4);
                        ImGui::ColorEdit3(
                            "##color", &t.color.x, ImGuiColorEditFlags_NoInputs);
                        ImGui::PopID();
                    }
                }
                ImGui::EndTable();
            }
            if (scene_track_visibility_changed) {
                sync_scene_preview_track_visibility();
            }
        }
    }
    ImGui::End();
}

void App::render_structure_rows_window(bool put_between) {
    bool& show_window = put_between ? show_structures_between_window_ : show_structures_window_;
    if (!show_window) return;
    if (dock_right_id_) ImGui::SetNextWindowDockID(dock_right_id_, ImGuiCond_FirstUseEver);
    bool& focus_next = put_between ? focus_structures_between_next_ : focus_structures_next_;
    if (focus_next) ImGui::SetNextWindowFocus();
    std::string title = tr(put_between ? "frame.structures_put_between" : "frame.structures") +
        (put_between ? "###StructuresPutBetween" : "###Structures");
    if (!ImGui::Begin(title.c_str(), &show_window)) {
        focus_next = false;
        ImGui::End();
        return;
    }

    sync_marker_visibility_sizes();
    ensure_table_cache();

    const TableColumnDef* columns = put_between ? k_structure_between_columns : k_structure_put_columns;
    const int column_count =
        put_between ? IM_ARRAYSIZE(k_structure_between_columns) : IM_ARRAYSIZE(k_structure_put_columns);
    const int file_path_column = put_between ? k_structure_between_file_path_column : k_structure_put_file_path_column;
    const std::vector<CachedTableRow>& rows =
        put_between ? table_cache_.structure_between_rows : table_cache_.structure_rows;
    const float file_path_width =
        put_between ? table_cache_.structure_between_file_path_width : table_cache_.structure_file_path_width;
    const size_t source_row_base = put_between ? table_cache_.structure_rows.size() : 0;
    const size_t source_row_end = source_row_base + rows.size();

    const size_t total_structure_rows =
        table_cache_.structure_rows.size() + table_cache_.structure_between_rows.size();
    if (structure_list_scroll_row_ >= 0 &&
        static_cast<size_t>(structure_list_scroll_row_) >= total_structure_rows) {
        structure_list_scroll_row_ = -1;
    }
    if (structure_list_highlight_row_ >= 0 &&
        static_cast<size_t>(structure_list_highlight_row_) >= total_structure_rows) {
        structure_list_highlight_row_ = -1;
    }

    bool all_visible = all_flags_set_in_range(structure_row_visible_, source_row_base, rows.size());
    ImGui::BeginDisabled(rows.empty());
    if (ImGui::Checkbox(tr("chk.select_all").c_str(), &all_visible)) {
        set_flags_in_range(structure_row_visible_, source_row_base, rows.size(), all_visible);
    }
    ImGui::EndDisabled();

    const int row_count = static_cast<int>(rows.size());
    int scroll_target_row = -1;
    if (structure_list_scroll_row_ >= 0) {
        const size_t source_row = static_cast<size_t>(structure_list_scroll_row_);
        if (source_row >= source_row_base && source_row < source_row_end) {
            scroll_target_row = static_cast<int>(source_row - source_row_base);
        }
    }

    const char* table_id = put_between ? "structures_put_between" : "structures_put";
    if (ImGui::BeginTable(table_id, column_count + 1,
                          ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                          ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollX |
                          ImGuiTableFlags_ScrollY)) {
        ImGui::TableSetupColumn(tr("column.show").c_str(), ImGuiTableColumnFlags_WidthFixed, k_show_column_width);
        for (int i = 0; i < column_count; ++i) {
            float width = columns[i].width;
            if (i == file_path_column) width = file_path_width;
            ImGui::TableSetupColumn(columns[i].header,
                                    width > 0.0f ? ImGuiTableColumnFlags_WidthFixed : 0,
                                    width);
        }
        setup_fixed_table_header();
        ImGui::TableHeadersRow();

        const ImU32 highlight_color = table_row_highlight_color(theme_color_);
        const bool can_locate_scene_preview = can_locate_scene_preview_row();
        ImGuiListClipper clipper;
        clipper.Begin(row_count);
        if (scroll_target_row >= 0 && scroll_target_row < row_count) {
            clipper.IncludeItemByIndex(scroll_target_row);
        }
        while (clipper.Step()) {
            for (int row_index = clipper.DisplayStart; row_index < clipper.DisplayEnd; ++row_index) {
                const CachedTableRow& row = rows[static_cast<size_t>(row_index)];
                const size_t marker_index = source_row_base + static_cast<size_t>(row_index);
                ImGui::TableNextRow();
                if (row_is_pending_delete(row.edit_id)) {
                    ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, k_pending_delete_row_color);
                } else if (row_has_pending_edit(row.edit_id)) {
                    ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, k_pending_edit_row_color);
                } else if (row.invalid_track_key) {
                    ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, k_invalid_track_key_row_color);
                } else if (structure_list_highlight_row_ >= 0 &&
                           marker_index == static_cast<size_t>(structure_list_highlight_row_)) {
                    ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, highlight_color);
                }
                if (row_index == scroll_target_row) {
                    ImGui::SetScrollHereY(0.5f);
                    structure_list_scroll_row_ = -1;
                }

                ImGui::PushID(static_cast<int>(marker_index));
                ImGui::TableSetColumnIndex(0);
                bool row_visible = marker_index < structure_row_visible_.size() &&
                    structure_row_visible_[marker_index] != 0;
                if (ImGui::Checkbox("##show", &row_visible) &&
                    marker_index < structure_row_visible_.size()) {
                    structure_row_visible_[marker_index] = row_visible ? 1 : 0;
                }
                for (int i = 0; i < column_count; ++i) {
                    ImGui::TableSetColumnIndex(i + 1);
                    const std::string& value = row.cells[static_cast<size_t>(i)];
                    if (i == k_structure_distance_column) {
                        bool can_locate = marker_index < structure_marker_cache_.size() &&
                            structure_marker_cache_[marker_index].has_value();
                        const bool can_locate_scene = can_locate_scene_preview;
                        TextCellContextAction action = render_text_cell_with_context_actions(
                            value,
                            tr("menu.locate_on_plan"),
                            can_locate,
                            tr("menu.locate_in_scene_preview"),
                            can_locate_scene,
                            tr("dialog.element_properties"),
                            edit_actions_available() && !row.edit_id.empty(),
                            tr("button.delete"),
                            edit_actions_available() && !row.edit_id.empty());
                        if (action == TextCellContextAction::Primary) {
                            locate_structure_row_on_plan(marker_index);
                        } else if (action == TextCellContextAction::Secondary) {
                            locate_structure_row_in_scene_preview(marker_index);
                        } else if (action == TextCellContextAction::Tertiary) {
                            request_element_inspector(row.edit_id, put_between ? "structure.between" : "structure.put");
                        } else if (action == TextCellContextAction::Quaternary) {
                            request_element_delete(row.edit_id,
                                                   put_between ? "structure.between" : "structure.put");
                        }
                        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                            request_element_inspector(row.edit_id, put_between ? "structure.between" : "structure.put");
                        }
                        continue;
                    }
                    if (value.empty()) continue;
                    if (i == k_structure_key_column) {
                        ImGui::PushID("structure_key");
                        if (render_text_cell_with_context(value, tr("menu.find_in_structure_models"), !blank_ascii(value))) {
                            find_structure_model_for_structure_key(value);
                        }
                        ImGui::PopID();
                    } else if (i == file_path_column) {
                        render_file_path_cell_with_context(value, row.open_path,
                                                           tr("menu.open_in_explorer"), row.tooltip_text);
                    } else {
                        ImGui::TextUnformatted(value.c_str());
                    }
                }
                ImGui::PopID();
            }
        }
        ImGui::EndTable();
    }

    focus_next = false;
    ImGui::End();
}

void App::render_structures_window() {
    render_structure_rows_window(false);
}

void App::render_structures_between_window() {
    render_structure_rows_window(true);
}

void App::render_other_trains_window() {
    if (!show_other_trains_window_) return;
    if (dock_right_id_) ImGui::SetNextWindowDockID(dock_right_id_, ImGuiCond_FirstUseEver);
    if (focus_other_trains_next_) ImGui::SetNextWindowFocus();
    std::string title = tr("frame.other_trains") + "###OtherTrains";
    if (!ImGui::Begin(title.c_str(), &show_other_trains_window_)) {
        focus_other_trains_next_ = false;
        ImGui::End();
        return;
    }
    if (!has_model_) {
        focus_other_trains_next_ = false;
        ImGui::TextDisabled("-");
        ImGui::End();
        return;
    }
    sync_marker_visibility_sizes();
    bool all_visible = all_flags_set(other_train_path_visible_);
    ImGui::BeginDisabled(other_train_path_visible_.empty());
    if (ImGui::Checkbox(tr("chk.select_all").c_str(), &all_visible)) {
        set_all_flags(other_train_path_visible_, all_visible);
    }
    ImGui::EndDisabled();
    ensure_table_cache();
    const bool has_stop_rows = !table_cache_.other_train_stop_rows.empty();
    if (has_stop_rows) {
        ImGui::TextUnformatted(tr("frame.other_train_definitions").c_str());
    }
    const int definition_row_count = static_cast<int>(table_cache_.other_train_rows.size());
    ImVec2 definition_table_size(0.0f, scroll_x_table_height_for_rows(definition_row_count));
    if (ImGui::BeginTable("other_trains", IM_ARRAYSIZE(k_other_train_columns) + 1,
                          ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                          ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollX,
                          definition_table_size)) {
        ImGui::TableSetupColumn(tr("column.show").c_str(), ImGuiTableColumnFlags_WidthFixed, k_show_column_width);
        for (int i = 0; i < IM_ARRAYSIZE(k_other_train_columns); ++i) {
            float width = k_other_train_columns[i].width;
            if (i == k_other_train_distance_column) width = table_cache_.other_train_distance_width;
            if (i == k_other_train_file_path_column) width = table_cache_.other_train_file_path_width;
            ImGui::TableSetupColumn(k_other_train_columns[i].header,
                                    width > 0.0f ? ImGuiTableColumnFlags_WidthFixed : 0,
                                    width);
        }
        setup_fixed_table_header();
        ImGui::TableHeadersRow();
        ImGuiListClipper clipper;
        clipper.Begin(definition_row_count);
        while (clipper.Step()) {
            for (int row_index = clipper.DisplayStart; row_index < clipper.DisplayEnd; ++row_index) {
                const CachedTableRow& row = table_cache_.other_train_rows[static_cast<size_t>(row_index)];
                ImGui::TableNextRow();
                ImGui::PushID(row_index);
                ImGui::TableSetColumnIndex(0);
                bool row_visible = static_cast<size_t>(row_index) < other_train_path_visible_.size() &&
                    other_train_path_visible_[static_cast<size_t>(row_index)] != 0;
                if (ImGui::Checkbox("##show", &row_visible) &&
                    static_cast<size_t>(row_index) < other_train_path_visible_.size()) {
                    other_train_path_visible_[static_cast<size_t>(row_index)] = row_visible ? 1 : 0;
                }
                for (int i = 0; i < IM_ARRAYSIZE(k_other_train_columns); ++i) {
                    ImGui::TableSetColumnIndex(i + 1);
                    const std::string& value = row.cells[static_cast<size_t>(i)];
                    if (value.empty()) continue;
                    if (i == k_other_train_file_path_column) {
                        render_file_path_cell_with_context(value, row.open_path,
                                                           tr("menu.open_in_explorer"),
                                                           row.tooltip_text);
                    } else {
                        ImGui::TextUnformatted(value.c_str());
                    }
                }
                ImGui::PopID();
            }
        }
        ImGui::EndTable();
    }
    if (has_stop_rows) {
        const int total_stop_rows = static_cast<int>(table_cache_.other_train_stop_rows.size());
        if (other_train_stop_list_scroll_row_ >= total_stop_rows) other_train_stop_list_scroll_row_ = -1;
        if (other_train_stop_list_highlight_row_ >= total_stop_rows) other_train_stop_list_highlight_row_ = -1;
        const int scroll_target_row = other_train_stop_list_scroll_row_;
        for (size_t group_index = 0; group_index < table_cache_.other_train_stop_groups.size(); ++group_index) {
            CachedOtherTrainStopGroup& group = table_cache_.other_train_stop_groups[group_index];
            if (group.row_indices.empty()) continue;

            ImGui::Separator();
            std::string stop_title = tr("frame.other_train_stops") + " - [" + group.train_key + "]";
            ImGui::TextUnformatted(stop_title.c_str());
            ImGui::SetNextItemWidth(220.0f);
            ImGui::InputText((tr("label.enable_time") + "##enable_" +
                              std::to_string(group_index)).c_str(),
                             &group.enable_time, ImGuiInputTextFlags_ReadOnly);

            const int row_count = static_cast<int>(group.row_indices.size());
            ImVec2 table_size(0.0f, scroll_x_table_height_for_rows(row_count));
            int scroll_target_group_row = -1;
            if (scroll_target_row >= 0) {
                for (size_t row_position = 0; row_position < group.row_indices.size(); ++row_position) {
                    if (group.row_indices[row_position] == static_cast<size_t>(scroll_target_row)) {
                        scroll_target_group_row = static_cast<int>(row_position);
                        break;
                    }
                }
            }

            std::string table_id = "other_train_stops_" + std::to_string(group_index);
            if (!ImGui::BeginTable(table_id.c_str(), IM_ARRAYSIZE(k_other_train_stop_columns),
                                   ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                                   ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollX,
                                   table_size)) {
                continue;
            }
            for (int i = 0; i < IM_ARRAYSIZE(k_other_train_stop_columns); ++i) {
                float width = k_other_train_stop_columns[i].width;
                if (i == k_other_train_stop_distance_column) width = table_cache_.other_train_stop_distance_width;
                if (i == k_other_train_stop_file_path_column) width = table_cache_.other_train_stop_file_path_width;
                ImGui::TableSetupColumn(k_other_train_stop_columns[i].header,
                                        width > 0.0f ? ImGuiTableColumnFlags_WidthFixed : 0,
                                        width);
            }
            setup_fixed_table_header();
            ImGui::TableHeadersRow();
            ImGuiListClipper clipper;
            clipper.Begin(row_count);
            if (scroll_target_group_row >= 0 && scroll_target_group_row < row_count) {
                clipper.IncludeItemByIndex(scroll_target_group_row);
            }
            while (clipper.Step()) {
                for (int group_row_index = clipper.DisplayStart; group_row_index < clipper.DisplayEnd; ++group_row_index) {
                    const size_t stop_row_index = group.row_indices[static_cast<size_t>(group_row_index)];
                    if (stop_row_index >= table_cache_.other_train_stop_rows.size()) continue;
                    const CachedTableRow& row = table_cache_.other_train_stop_rows[stop_row_index];
                    ImGui::TableNextRow();
                    if (other_train_stop_list_highlight_row_ >= 0 &&
                        stop_row_index == static_cast<size_t>(other_train_stop_list_highlight_row_)) {
                        ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, k_find_match_row_color);
                    }
                    if (group_row_index == scroll_target_group_row) {
                        ImGui::SetScrollHereY(0.5f);
                        other_train_stop_list_scroll_row_ = -1;
                    }
                    ImGui::PushID(static_cast<int>(stop_row_index));
                    for (int i = 0; i < IM_ARRAYSIZE(k_other_train_stop_columns); ++i) {
                        ImGui::TableSetColumnIndex(i);
                        const std::string& value = row.cells[static_cast<size_t>(i)];
                        if (i == k_other_train_stop_distance_column) {
                            const size_t marker_index = stop_row_index;
                            const bool can_locate = marker_index < other_train_stop_marker_cache_.size() &&
                                other_train_stop_marker_cache_[marker_index].has_value();
                            ImGui::PushID(i);
                            const bool should_locate =
                                render_text_cell_with_context(value, tr("menu.locate_on_plan"), can_locate);
                            ImGui::PopID();
                            if (should_locate) {
                                locate_other_train_stop_row_on_plan(marker_index);
                                other_train_stop_list_highlight_row_ = static_cast<int>(stop_row_index);
                            }
                            continue;
                        }
                        if (value.empty()) continue;
                        if (i == k_other_train_stop_file_path_column) {
                            render_file_path_cell_with_context(value, row.open_path,
                                                               tr("menu.open_in_explorer"),
                                                               row.tooltip_text);
                        } else {
                            ImGui::TextUnformatted(value.c_str());
                        }
                    }
                    ImGui::PopID();
                }
            }
            ImGui::EndTable();
        }
    }
    focus_other_trains_next_ = false;
    ImGui::End();
}

void App::render_repeaters_window() {
    if (!show_repeaters_window_) return;
    if (dock_right_id_) ImGui::SetNextWindowDockID(dock_right_id_, ImGuiCond_FirstUseEver);
    if (focus_repeaters_next_) ImGui::SetNextWindowFocus();
    std::string title = tr("frame.repeaters") + "###Repeaters";
    if (!ImGui::Begin(title.c_str(), &show_repeaters_window_)) {
        focus_repeaters_next_ = false;
        ImGui::End();
        return;
    }
    sync_marker_visibility_sizes();
    bool all_visible = all_flags_set(repeater_row_visible_);
    ImGui::BeginDisabled(repeater_row_visible_.empty());
    if (ImGui::Checkbox(tr("chk.select_all").c_str(), &all_visible)) {
        set_all_flags(repeater_row_visible_, all_visible);
    }
    ImGui::EndDisabled();
    ensure_table_cache();
    if (ImGui::BeginTable("repeaters", IM_ARRAYSIZE(k_repeater_columns) + 1, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollX | ImGuiTableFlags_ScrollY)) {
        ImGui::TableSetupColumn(tr("column.show").c_str(), ImGuiTableColumnFlags_WidthFixed, k_show_column_width);
        for (int i = 0; i < IM_ARRAYSIZE(k_repeater_columns); ++i) {
            float width = k_repeater_columns[i].width;
            if (i == k_repeater_distance_column) width = table_cache_.repeater_distance_width;
            if (i == k_repeater_interval_column) width = table_cache_.repeater_interval_width;
            if (i == k_repeater_file_path_column) width = table_cache_.repeater_file_path_width;
            ImGui::TableSetupColumn(k_repeater_columns[i].header, width > 0.0f ? ImGuiTableColumnFlags_WidthFixed : 0, width);
        }
        setup_fixed_table_header();
        ImGui::TableHeadersRow();
        ImGuiListClipper clipper;
        const int row_count = static_cast<int>(table_cache_.repeater_rows.size());
        if (repeater_list_scroll_row_ >= row_count) repeater_list_scroll_row_ = -1;
        if (repeater_list_highlight_row_ >= row_count) repeater_list_highlight_row_ = -1;
        const int scroll_target_row = repeater_list_scroll_row_;
        clipper.Begin(row_count);
        if (scroll_target_row >= 0 && scroll_target_row < row_count) {
            clipper.IncludeItemByIndex(scroll_target_row);
        }
        const ImU32 highlight_color = table_row_highlight_color(theme_color_);
        const bool can_locate_scene_preview = can_locate_scene_preview_row();
        while (clipper.Step()) {
            for (int row_index = clipper.DisplayStart; row_index < clipper.DisplayEnd; ++row_index) {
                const CachedTableRow& row = table_cache_.repeater_rows[static_cast<size_t>(row_index)];
                ImGui::TableNextRow();
                if (row.invalid_track_key) {
                    ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, k_invalid_track_key_row_color);
                } else if (row_index == repeater_list_highlight_row_) {
                    ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, highlight_color);
                }
                if (row_index == scroll_target_row) {
                    ImGui::SetScrollHereY(0.5f);
                    repeater_list_scroll_row_ = -1;
                }
                ImGui::PushID(row_index);
                ImGui::TableSetColumnIndex(0);
                bool row_visible = static_cast<size_t>(row_index) < repeater_row_visible_.size() &&
                    repeater_row_visible_[static_cast<size_t>(row_index)] != 0;
                if (ImGui::Checkbox("##show", &row_visible) &&
                    static_cast<size_t>(row_index) < repeater_row_visible_.size()) {
                    repeater_row_visible_[static_cast<size_t>(row_index)] = row_visible ? 1 : 0;
                }
                for (int i = 0; i < IM_ARRAYSIZE(k_repeater_columns); ++i) {
                    ImGui::TableSetColumnIndex(i + 1);
                    const std::string& value = row.cells[static_cast<size_t>(i)];
                    if (i == k_repeater_distance_column) {
                        size_t marker_index = static_cast<size_t>(row_index);
                        bool can_locate = marker_index < repeater_marker_cache_.size() &&
                            repeater_marker_cache_[marker_index].begin_marker.has_value();
                        const bool can_locate_scene = can_locate_scene_preview;
                        RepeaterTextCellContextAction action =
                            render_repeater_text_cell_with_context_actions(
                            value,
                            tr("menu.locate_on_plan"),
                            can_locate,
                            tr("menu.locate_in_scene_preview"),
                            can_locate_scene,
                            tr("dialog.element_properties"),
                            tr("button.delete"),
                            tr("menu.repeater_delete_all"),
                            tr("menu.repeater_delete_change_point"),
                            tr("menu.repeater_trim_to_change_point"),
                            tr("menu.repeater_start_from_change_point"),
                            edit_actions_available() && !row.edit_id.empty(),
                            row.repeater_chain_begin_index,
                            row.repeater_chain_begin_count);
                        if (action.navigation == TextCellContextAction::Primary) {
                            locate_repeater_row_on_plan(marker_index);
                        } else if (action.navigation == TextCellContextAction::Secondary) {
                            locate_repeater_row_in_scene_preview(marker_index);
                        } else if (action.navigation == TextCellContextAction::Tertiary) {
                            request_element_inspector(row.edit_id, "repeater");
                        }
                        if (action.delete_requested) {
                            request_element_delete(row.edit_id, "repeater", action.delete_mode);
                        }
                        if (action.hovered &&
                            ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                            request_element_inspector(row.edit_id, "repeater");
                        }
                        continue;
                    }
                    if (value.empty()) continue;
                    if (i == k_repeater_structure_keys_column) {
                        ImGui::PushID("structure_keys");
                        std::vector<std::string> structure_keys = split_structure_key_list(value);
                        std::string selected_key = render_text_cell_with_submenu(
                            value, tr("menu.find_in_structure_models"), structure_keys);
                        if (!selected_key.empty()) find_structure_model_for_structure_key(selected_key);
                        ImGui::PopID();
                    } else if (i == k_repeater_file_path_column) {
                        render_file_path_cell_with_context(value, row.open_path,
                                                           tr("menu.open_in_explorer"), row.tooltip_text);
                    } else {
                        ImGui::TextUnformatted(value.c_str());
                    }
                }
                ImGui::PopID();
            }
        }
        ImGui::EndTable();
    }
    focus_repeaters_next_ = false;
    ImGui::End();
}

void App::render_signals_window() {
    if (!show_signals_window_) return;
    if (dock_right_id_) ImGui::SetNextWindowDockID(dock_right_id_, ImGuiCond_FirstUseEver);
    if (focus_signals_next_) ImGui::SetNextWindowFocus();
    std::string title = tr("frame.signals") + "###Signals";
    if (!ImGui::Begin(title.c_str(), &show_signals_window_)) {
        focus_signals_next_ = false;
        ImGui::End();
        return;
    }
    sync_marker_visibility_sizes();
    bool all_visible = all_flags_set(signal_row_visible_);
    ImGui::BeginDisabled(signal_row_visible_.empty());
    if (ImGui::Checkbox(tr("chk.select_all").c_str(), &all_visible)) {
        set_all_flags(signal_row_visible_, all_visible);
    }
    ImGui::EndDisabled();
    ensure_table_cache();
    if (ImGui::BeginTable("signals", IM_ARRAYSIZE(k_signal_columns) + 1,
                          ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                          ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollX |
                          ImGuiTableFlags_ScrollY)) {
        std::string file_name_header = tr("column.file_name");
        ImGui::TableSetupColumn(tr("column.show").c_str(), ImGuiTableColumnFlags_WidthFixed, k_show_column_width);
        for (int i = 0; i < IM_ARRAYSIZE(k_signal_columns); ++i) {
            float width = k_signal_columns[i].width;
            if (i == k_signal_distance_column) width = table_cache_.signal_distance_width;
            if (i == k_signal_file_path_column) width = table_cache_.signal_file_path_width;
            const char* header = i == k_signal_file_path_column
                ? file_name_header.c_str()
                : k_signal_columns[i].header;
            ImGui::TableSetupColumn(header, width > 0.0f ? ImGuiTableColumnFlags_WidthFixed : 0, width);
        }
        setup_fixed_table_header();
        ImGui::TableHeadersRow();
        ImGuiListClipper clipper;
        const int row_count = static_cast<int>(table_cache_.signal_rows.size());
        if (signal_list_scroll_row_ >= row_count) signal_list_scroll_row_ = -1;
        if (signal_list_highlight_row_ >= row_count) signal_list_highlight_row_ = -1;
        const int scroll_target_row = signal_list_scroll_row_;
        clipper.Begin(row_count);
        if (scroll_target_row >= 0 && scroll_target_row < row_count) {
            clipper.IncludeItemByIndex(scroll_target_row);
        }
        const ImU32 highlight_color = table_row_highlight_color(theme_color_);
        const bool can_locate_scene_preview = can_locate_scene_preview_row();
        while (clipper.Step()) {
            for (int row_index = clipper.DisplayStart; row_index < clipper.DisplayEnd; ++row_index) {
                const CachedTableRow& row = table_cache_.signal_rows[static_cast<size_t>(row_index)];
                ImGui::TableNextRow();
                if (row_has_pending_edit(row.edit_id)) {
                    ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, k_pending_edit_row_color);
                } else if (row_index == signal_list_highlight_row_) {
                    ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, highlight_color);
                }
                if (row_index == scroll_target_row) {
                    ImGui::SetScrollHereY(0.5f);
                    signal_list_scroll_row_ = -1;
                }
                ImGui::PushID(row_index);
                ImGui::TableSetColumnIndex(0);
                bool row_visible = static_cast<size_t>(row_index) < signal_row_visible_.size() &&
                    signal_row_visible_[static_cast<size_t>(row_index)] != 0;
                if (ImGui::Checkbox("##show", &row_visible) &&
                    static_cast<size_t>(row_index) < signal_row_visible_.size()) {
                    signal_row_visible_[static_cast<size_t>(row_index)] = row_visible ? 1 : 0;
                }
                for (int i = 0; i < IM_ARRAYSIZE(k_signal_columns); ++i) {
                    ImGui::TableSetColumnIndex(i + 1);
                    const std::string& value = row.cells[static_cast<size_t>(i)];
                    if (i == k_signal_distance_column) {
                        size_t marker_index = static_cast<size_t>(row_index);
                        bool can_locate = marker_index < signal_marker_cache_.size() &&
                            signal_marker_cache_[marker_index].has_value();
                        const TextCellContextAction action = render_text_cell_with_context_actions(
                            value,
                            tr("menu.locate_on_plan"), can_locate,
                            tr("menu.locate_in_scene_preview"), can_locate_scene_preview,
                            tr("dialog.element_properties"),
                            edit_actions_available() && !row.edit_id.empty(),
                            tr("button.delete"),
                            edit_actions_available() && !row.edit_id.empty());
                        if (action == TextCellContextAction::Primary) {
                            locate_signal_row_on_plan(marker_index);
                        } else if (action == TextCellContextAction::Secondary) {
                            locate_signal_row_in_scene_preview(marker_index);
                        } else if (action == TextCellContextAction::Tertiary) {
                            request_element_inspector(row.edit_id, "signal.put");
                        } else if (action == TextCellContextAction::Quaternary) {
                            request_element_delete(row.edit_id, "signal.put");
                        }
                        continue;
                    }
                    if (value.empty()) continue;
                    if (i == k_signal_signal_aspect_key_column) {
                        ImGui::PushID("signal_aspect_key");
                        if (render_text_cell_with_context(value, tr("menu.find_in_signal_aspects"),
                                                          !blank_ascii(value))) {
                            find_signal_aspect_for_signal_aspect_key(value);
                        }
                        ImGui::PopID();
                    } else if (i == k_signal_file_path_column) {
                        render_file_path_cell_with_context(value, row.open_path,
                                                           tr("menu.open_in_explorer"), row.tooltip_text);
                    } else {
                        ImGui::TextUnformatted(value.c_str());
                    }
                }
                ImGui::PopID();
            }
        }
        ImGui::EndTable();
    }
    focus_signals_next_ = false;
    ImGui::End();
}

void App::render_sections_window() {
    if (!show_sections_window_) return;
    if (dock_right_id_) ImGui::SetNextWindowDockID(
        dock_right_id_, ImGuiCond_FirstUseEver);
    if (focus_sections_next_) ImGui::SetNextWindowFocus();
    std::string title = tr("frame.sections") + "###Sections";
    if (!ImGui::Begin(title.c_str(), &show_sections_window_)) {
        focus_sections_next_ = false;
        ImGui::End();
        return;
    }
    if (!has_model_) {
        ImGui::TextDisabled("-");
        focus_sections_next_ = false;
        ImGui::End();
        return;
    }
    ensure_table_cache();
    const bool can_locate_scene = can_locate_scene_preview_row();
    auto render_section_table = [&](const char* table_id,
                                    const std::string& heading,
                                    const std::vector<CachedTableRow>& rows,
                                    size_t value_columns,
                                    const char* value_prefix,
                                    bool has_markers,
                                    const std::vector<TableRow>* source_rows,
                                    const char* row_kind) {
        ImGui::TextUnformatted(heading.c_str());
        const size_t displayed_value_columns =
            std::min(value_columns, k_max_section_value_columns);
        if (value_columns > displayed_value_columns) {
            std::string notice = tr("table.section_values_truncated");
            replace_all(notice, "{shown}", std::to_string(displayed_value_columns));
            replace_all(notice, "{total}", std::to_string(value_columns));
            ImGui::TextWrapped("%s", notice.c_str());
        }
        const int column_count = static_cast<int>(3 + displayed_value_columns);
        if (!ImGui::BeginTable(
                table_id, column_count,
                ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollX |
                ImGuiTableFlags_ScrollY,
                ImVec2(0.0f, scroll_x_table_height_for_rows(
                    static_cast<int>(rows.size()))))) {
            return;
        }
        ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, 45.0f);
        ImGui::TableSetupColumn("distance", ImGuiTableColumnFlags_WidthFixed, 110.0f);
        for (size_t i = 0; i < displayed_value_columns; ++i) {
            const std::string header = value_prefix + std::to_string(i);
            ImGui::TableSetupColumn(header.c_str(), ImGuiTableColumnFlags_WidthFixed, 80.0f);
        }
        ImGui::TableSetupColumn("filePath", ImGuiTableColumnFlags_WidthFixed, 200.0f);
        setup_fixed_table_header();
        ImGui::TableHeadersRow();
        for (size_t row_index = 0; row_index < rows.size(); ++row_index) {
            const CachedTableRow& row = rows[row_index];
            ImGui::TableNextRow();
            if (has_markers && static_cast<int>(row_index) == section_list_highlight_row_) {
                ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0,
                                       table_row_highlight_color(theme_color_));
            }
            if (has_markers && static_cast<int>(row_index) == section_list_scroll_row_) {
                ImGui::SetScrollHereY(0.5f);
                section_list_scroll_row_ = -1;
            }
            ImGui::PushID(static_cast<int>(row_index));
            for (int column = 0; column < column_count; ++column) {
                ImGui::TableSetColumnIndex(column);
                const std::string& value = row.cells[static_cast<size_t>(column)];
                if (column == 1) {
                    const bool can_edit = edit_actions_available() &&
                        source_rows && row_index < source_rows->size() &&
                        !(*source_rows)[row_index].edit_id.empty();
                    const TextCellContextAction action =
                        render_marker_text_cell_with_context(
                            value, tr("menu.locate_on_plan"), true,
                            tr("menu.locate_in_scene_preview"),
                            has_markers && can_locate_scene,
                            tr("dialog.element_properties"), can_edit,
                            tr("button.delete"), can_edit);
                    if (action == TextCellContextAction::Primary) {
                        if (has_markers) {
                            locate_section_row_on_plan(row_index);
                        } else {
                            focus_plan_at_distance(
                                std::strtod(value.c_str(), nullptr));
                        }
                    } else if (action == TextCellContextAction::Secondary) {
                        locate_scene_marker_row_in_scene_preview(
                            Canvas3DSceneMarkerListKind::Section, row_index);
                    } else if (action == TextCellContextAction::Tertiary) {
                        request_element_inspector(
                            (*source_rows)[row_index].edit_id, row_kind);
                    } else if (action == TextCellContextAction::Quaternary) {
                        request_element_delete(
                            (*source_rows)[row_index].edit_id, row_kind);
                    }
                } else if (column == column_count - 1) {
                    render_file_path_cell_with_context(
                        value, row.open_path, tr("menu.open_in_explorer"),
                        row.tooltip_text);
                } else if (!value.empty()) {
                    ImGui::TextUnformatted(value.c_str());
                }
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    };

    render_section_table(
        "section_begins", tr("frame.section_begins"),
        table_cache_.section_begin_rows,
        table_cache_.section_begin_value_columns, "signal", true,
        &model_.section_begins, "section.begin");
    ImGui::Separator();
    render_section_table(
        "section_speed_limits", tr("frame.section_speed_limits"),
        table_cache_.section_speed_limit_rows,
        table_cache_.section_speed_limit_value_columns, "v", false,
        &model_.section_speed_limits, "section.speedLimit");
    focus_sections_next_ = false;
    ImGui::End();
}

void App::render_variables_window() {
    if (!show_variables_window_) return;
    if (dock_right_id_) ImGui::SetNextWindowDockID(
        dock_right_id_, ImGuiCond_FirstUseEver);
    std::string title = tr("frame.variables") + "###Variables";
    if (!ImGui::Begin(title.c_str(), &show_variables_window_)) {
        ImGui::End();
        return;
    }
    if (!has_model_) {
        ImGui::TextDisabled("-");
        ImGui::End();
        return;
    }
    ensure_table_cache();
    if (ImGui::BeginTable(
            "variables", 3,
            ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
            ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY)) {
        ImGui::TableSetupColumn(tr("column.variable").c_str());
        ImGui::TableSetupColumn(tr("column.value").c_str());
        ImGui::TableSetupColumn("filePath");
        setup_fixed_table_header();
        ImGui::TableHeadersRow();
        for (size_t row_index = 0; row_index < table_cache_.variable_rows.size();
             ++row_index) {
            const CachedVariableRow& row = table_cache_.variable_rows[row_index];
            ImGui::TableNextRow();
            ImGui::PushID(static_cast<int>(row_index));
            if (row.group_header) {
                ImGui::TableSetColumnIndex(0);
                ImGui::TextColored(theme_color_, "$%s", row.name.c_str());
            } else {
                ImGui::TableSetColumnIndex(1);
                ImGui::TextUnformatted(row.value.c_str());
                if (ImGui::IsItemHovered() && !row.expression.empty()) {
                    ImGui::SetTooltip("%s", row.expression.c_str());
                }
                ImGui::TableSetColumnIndex(2);
                render_file_path_cell_with_context(
                    display_name_from_path(row.file_path), row.file_path,
                    tr("menu.open_in_explorer"), row.file_path);
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    ImGui::End();
}

