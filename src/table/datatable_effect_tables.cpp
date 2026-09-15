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
template <size_t N, typename RenderSpecialCellFn>
void render_event_table(const char* table_id,
                        const TableColumnDef (&columns)[N],
                        int distance_column,
                        int file_path_column,
                        const std::vector<CachedTableRow>& rows,
                        float distance_width,
                        float file_path_width,
                        const std::string& file_name_header,
                        const std::string& open_menu_label,
                        int& scroll_row,
                        int& highlight_row,
                        ImU32 highlight_color,
                        RenderSpecialCellFn render_special_cell) {
    if (!ImGui::BeginTable(table_id, static_cast<int>(N),
                           ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                           ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollX |
                           ImGuiTableFlags_ScrollY)) {
        return;
    }
    for (int column = 0; column < static_cast<int>(N); ++column) {
        float width = columns[column].width;
        if (column == distance_column) width = distance_width;
        if (column == file_path_column) width = file_path_width;
        const char* header = column == file_path_column
            ? file_name_header.c_str()
            : columns[column].header;
        ImGui::TableSetupColumn(
            header, width > 0.0f ? ImGuiTableColumnFlags_WidthFixed : 0, width);
    }
    setup_fixed_table_header();
    ImGui::TableHeadersRow();
    ImGuiListClipper clipper;
    const int row_count = static_cast<int>(rows.size());
    if (scroll_row >= row_count) scroll_row = -1;
    if (highlight_row >= row_count) highlight_row = -1;
    const int scroll_target_row = scroll_row;
    clipper.Begin(row_count);
    if (scroll_target_row >= 0 && scroll_target_row < row_count) {
        clipper.IncludeItemByIndex(scroll_target_row);
    }
    while (clipper.Step()) {
        for (int row_index = clipper.DisplayStart; row_index < clipper.DisplayEnd; ++row_index) {
            const CachedTableRow& row = rows[static_cast<size_t>(row_index)];
            ImGui::TableNextRow();
            if (row_index == highlight_row) {
                ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, highlight_color);
            }
            if (row_index == scroll_target_row) {
                ImGui::SetScrollHereY(0.5f);
                scroll_row = -1;
            }
            ImGui::PushID(row_index);
            for (int column = 0; column < static_cast<int>(N); ++column) {
                ImGui::TableSetColumnIndex(column);
                const std::string& value = row.cells[static_cast<size_t>(column)];
                if (render_special_cell(row_index, column, value)) continue;
                if (value.empty()) continue;
                if (column == file_path_column) {
                    render_file_path_cell_with_context(
                        value, row.open_path, open_menu_label, row.open_path);
                } else {
                    ImGui::TextUnformatted(value.c_str());
                }
            }
            ImGui::PopID();
        }
    }
    ImGui::EndTable();
}

template <size_t N, typename CanLocateFn, typename LocateFn, typename LocateSceneFn,
          typename CanEditFn, typename EditFn, typename DeleteFn>
void render_change_point_table(const char* table_id,
                               const TableColumnDef (&columns)[N],
                               int distance_column,
                               int file_path_column,
                               const std::vector<CachedTableRow>& rows,
                               float distance_width,
                               float file_path_width,
                               const std::string& file_name_header,
                               const std::string& locate_on_plan_label,
                               const std::string& locate_in_scene_label,
                               const std::string& open_menu_label,
                               int& scroll_row,
                               int& highlight_row,
                               ImU32 highlight_color,
                               CanLocateFn can_locate,
                               LocateFn locate_row_on_plan,
                               bool can_locate_scene,
                               LocateSceneFn locate_row_in_scene,
                               const std::string& properties_label,
                               const std::string& delete_label,
                               CanEditFn can_edit_row,
                               EditFn edit_row,
                               DeleteFn delete_row) {
    render_event_table(
        table_id, columns, distance_column, file_path_column, rows,
        distance_width, file_path_width, file_name_header, open_menu_label,
        scroll_row, highlight_row, highlight_color,
        [&](int row_index, int column, const std::string& value) {
            if (column != distance_column) return false;
            const size_t marker_index = static_cast<size_t>(row_index);
            const bool can_edit = can_edit_row(marker_index);
            const TextCellContextAction action = render_marker_text_cell_with_context(
                value,
                locate_on_plan_label, can_locate(marker_index),
                locate_in_scene_label, can_locate_scene,
                properties_label, can_edit,
                delete_label, can_edit);
            if (action == TextCellContextAction::Primary) {
                locate_row_on_plan(marker_index);
            } else if (action == TextCellContextAction::Secondary) {
                locate_row_in_scene(marker_index);
            } else if (action == TextCellContextAction::Tertiary) {
                edit_row(marker_index);
            } else if (action == TextCellContextAction::Quaternary) {
                delete_row(marker_index);
            }
            return true;
        });
}
template <size_t N, typename CanLocateFn, typename LocateFn, typename LocateSceneFn, typename FindFn,
          typename CanEditFn, typename EditFn, typename DeleteFn>
void render_map_sound_event_table(const char* table_id,
                                  const TableColumnDef (&columns)[N],
                                  int distance_column,
                                  int sound_key_column,
                                  int file_path_column,
                                  const std::vector<CachedTableRow>& rows,
                                  float distance_width,
                                  float file_path_width,
                                  const std::string& file_name_header,
                                  const std::string& locate_on_plan_label,
                                  const std::string& locate_in_scene_label,
                                  const std::string& find_menu_label,
                                  const std::string& open_menu_label,
                                  int& scroll_row,
                                  int& highlight_row,
                                  ImU32 highlight_color,
                                  CanLocateFn can_locate,
                                  LocateFn locate_row_on_plan,
                                  bool can_locate_scene,
                                  LocateSceneFn locate_row_in_scene,
                                  FindFn find_sound_file,
                                  const std::string& properties_label,
                                  const std::string& delete_label,
                                  CanEditFn can_edit_row,
                                  EditFn edit_row,
                                  DeleteFn delete_row) {
    render_event_table(
        table_id, columns, distance_column, file_path_column, rows,
        distance_width, file_path_width, file_name_header, open_menu_label,
        scroll_row, highlight_row, highlight_color,
        [&](int row_index, int column, const std::string& value) {
            if (column == distance_column) {
                const size_t marker_index = static_cast<size_t>(row_index);
                const bool can_edit = can_edit_row(marker_index);
                ImGui::PushID(column);
                const TextCellContextAction action = render_marker_text_cell_with_context(
                    value,
                    locate_on_plan_label, can_locate(marker_index),
                    locate_in_scene_label, can_locate_scene,
                    properties_label, can_edit,
                    delete_label, can_edit);
                ImGui::PopID();
                if (action == TextCellContextAction::Primary) {
                    locate_row_on_plan(marker_index);
                } else if (action == TextCellContextAction::Secondary) {
                    locate_row_in_scene(marker_index);
                } else if (action == TextCellContextAction::Tertiary) {
                    edit_row(marker_index);
                } else if (action == TextCellContextAction::Quaternary) {
                    delete_row(marker_index);
                }
                return true;
            }
            if (column == sound_key_column) {
                ImGui::PushID(column);
                const bool should_find = render_text_cell_with_context(
                    value, find_menu_label, !blank_ascii(value));
                ImGui::PopID();
                if (should_find) find_sound_file(value);
                return true;
            }
            return false;
        });
}
} // namespace

void App::render_beacons_window() {
    if (!show_beacons_window_) return;
    if (dock_right_id_) ImGui::SetNextWindowDockID(dock_right_id_, ImGuiCond_FirstUseEver);
    if (focus_beacons_next_) ImGui::SetNextWindowFocus();
    std::string title = tr("frame.beacons") + "###Beacons";
    if (!ImGui::Begin(title.c_str(), &show_beacons_window_)) {
        focus_beacons_next_ = false;
        ImGui::End();
        return;
    }
    if (!has_model_) {
        ImGui::TextDisabled("-");
        focus_beacons_next_ = false;
        ImGui::End();
        return;
    }
    ensure_table_cache();
    if (ImGui::BeginTable("beacons", IM_ARRAYSIZE(k_beacon_columns),
                          ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                          ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollX |
                          ImGuiTableFlags_ScrollY)) {
        std::string file_name_header = tr("column.file_name");
        for (int i = 0; i < IM_ARRAYSIZE(k_beacon_columns); ++i) {
            float width = k_beacon_columns[i].width;
            if (i == k_beacon_distance_column) width = table_cache_.beacon_distance_width;
            if (i == k_beacon_file_path_column) width = table_cache_.beacon_file_path_width;
            const char* header = i == k_beacon_file_path_column
                ? file_name_header.c_str()
                : k_beacon_columns[i].header;
            ImGui::TableSetupColumn(header, width > 0.0f ? ImGuiTableColumnFlags_WidthFixed : 0, width);
        }
        setup_fixed_table_header();
        ImGui::TableHeadersRow();
        ImGuiListClipper clipper;
        const int row_count = static_cast<int>(table_cache_.beacon_rows.size());
        if (beacon_list_scroll_row_ >= row_count) beacon_list_scroll_row_ = -1;
        if (beacon_list_highlight_row_ >= row_count) beacon_list_highlight_row_ = -1;
        const int scroll_target_row = beacon_list_scroll_row_;
        clipper.Begin(row_count);
        if (scroll_target_row >= 0 && scroll_target_row < row_count) {
            clipper.IncludeItemByIndex(scroll_target_row);
        }
        const ImU32 highlight_color = table_row_highlight_color(theme_color_);
        const bool can_locate_scene_preview = can_locate_scene_preview_row();
        while (clipper.Step()) {
            for (int row_index = clipper.DisplayStart; row_index < clipper.DisplayEnd; ++row_index) {
                const CachedTableRow& row = table_cache_.beacon_rows[static_cast<size_t>(row_index)];
                ImGui::TableNextRow();
                if (row_index == beacon_list_highlight_row_) {
                    ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, highlight_color);
                }
                if (row_index == scroll_target_row) {
                    ImGui::SetScrollHereY(0.5f);
                    beacon_list_scroll_row_ = -1;
                }
                ImGui::PushID(row_index);
                for (int i = 0; i < IM_ARRAYSIZE(k_beacon_columns); ++i) {
                    ImGui::TableSetColumnIndex(i);
                    const std::string& value = row.cells[static_cast<size_t>(i)];
                    if (i == k_beacon_distance_column) {
                        size_t marker_index = static_cast<size_t>(row_index);
                        bool can_locate = marker_index < beacon_marker_cache_.size() &&
                            beacon_marker_cache_[marker_index].has_value();
                        const bool can_edit = edit_actions_available() &&
                            marker_index < model_.beacons.size() &&
                            !model_.beacons[marker_index].edit_id.empty();
                        const TextCellContextAction action = render_marker_text_cell_with_context(
                            value,
                            tr("menu.locate_on_plan"), can_locate,
                            tr("menu.locate_in_scene_preview"), can_locate_scene_preview,
                            tr("dialog.element_properties"), can_edit,
                            tr("button.delete"), can_edit);
                        if (action == TextCellContextAction::Primary) {
                            locate_beacon_row_on_plan(marker_index);
                        } else if (action == TextCellContextAction::Secondary) {
                            locate_scene_marker_row_in_scene_preview(
                                Canvas3DSceneMarkerListKind::Beacon, marker_index);
                        } else if (action == TextCellContextAction::Tertiary) {
                            request_element_inspector(model_.beacons[marker_index].edit_id, "beacon.put");
                        } else if (action == TextCellContextAction::Quaternary) {
                            request_element_delete(model_.beacons[marker_index].edit_id, "beacon.put");
                        }
                        continue;
                    }
                    if (value.empty()) continue;
                    if (i == k_beacon_file_path_column) {
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
    focus_beacons_next_ = false;
    ImGui::End();
}

void App::render_irregularities_window() {
    if (!show_irregularities_window_) return;
    if (dock_right_id_) ImGui::SetNextWindowDockID(dock_right_id_, ImGuiCond_FirstUseEver);
    if (focus_irregularities_next_) ImGui::SetNextWindowFocus();
    std::string title = tr("frame.irregularities") + "###Irregularities";
    if (!ImGui::Begin(title.c_str(), &show_irregularities_window_)) {
        focus_irregularities_next_ = false;
        ImGui::End();
        return;
    }
    if (!has_model_) {
        ImGui::TextDisabled("-");
        focus_irregularities_next_ = false;
        ImGui::End();
        return;
    }
    ensure_table_cache();
    if (ImGui::BeginTable("irregularities", IM_ARRAYSIZE(k_irregularity_columns),
                          ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                          ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollX |
                          ImGuiTableFlags_ScrollY)) {
        std::string file_name_header = tr("column.file_name");
        for (int i = 0; i < IM_ARRAYSIZE(k_irregularity_columns); ++i) {
            float width = k_irregularity_columns[i].width;
            if (i == k_irregularity_distance_column) width = table_cache_.irregularity_distance_width;
            if (i == k_irregularity_file_path_column) width = table_cache_.irregularity_file_path_width;
            const char* header = i == k_irregularity_file_path_column
                ? file_name_header.c_str()
                : k_irregularity_columns[i].header;
            ImGui::TableSetupColumn(header, width > 0.0f ? ImGuiTableColumnFlags_WidthFixed : 0, width);
        }
        setup_fixed_table_header();
        ImGui::TableHeadersRow();
        ImGuiListClipper clipper;
        const int row_count = static_cast<int>(table_cache_.irregularity_rows.size());
        if (irregularity_list_scroll_row_ >= row_count) irregularity_list_scroll_row_ = -1;
        if (irregularity_list_highlight_row_ >= row_count) irregularity_list_highlight_row_ = -1;
        const int scroll_target_row = irregularity_list_scroll_row_;
        clipper.Begin(row_count);
        if (scroll_target_row >= 0 && scroll_target_row < row_count) {
            clipper.IncludeItemByIndex(scroll_target_row);
        }
        const ImU32 highlight_color = table_row_highlight_color(theme_color_);
        const bool can_locate_scene_preview = can_locate_scene_preview_row();
        while (clipper.Step()) {
            for (int row_index = clipper.DisplayStart; row_index < clipper.DisplayEnd; ++row_index) {
                const CachedTableRow& row = table_cache_.irregularity_rows[static_cast<size_t>(row_index)];
                ImGui::TableNextRow();
                if (row_is_pending_delete(row.edit_id)) {
                    ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, k_pending_delete_row_color);
                } else if (row_has_pending_edit(row.edit_id)) {
                    ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, k_pending_edit_row_color);
                } else if (row_index == irregularity_list_highlight_row_) {
                    ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, highlight_color);
                }
                if (row_index == scroll_target_row) {
                    ImGui::SetScrollHereY(0.5f);
                    irregularity_list_scroll_row_ = -1;
                }
                ImGui::PushID(row_index);
                for (int i = 0; i < IM_ARRAYSIZE(k_irregularity_columns); ++i) {
                    ImGui::TableSetColumnIndex(i);
                    const std::string& value = row.cells[static_cast<size_t>(i)];
                if (i == k_irregularity_distance_column) {
                    size_t marker_index = static_cast<size_t>(row_index);
                    const bool can_locate = marker_index < irregularity_marker_cache_.size() &&
                        irregularity_marker_cache_[marker_index].has_value();
                    const bool edit_enabled = edit_actions_available() && !row.edit_id.empty();
                    const TextCellContextAction action = render_text_cell_with_context_actions(
                        value,
                        tr("menu.locate_on_plan"), can_locate,
                        tr("menu.locate_in_scene_preview"), can_locate_scene_preview,
                        tr("dialog.element_properties"), edit_enabled,
                        tr("button.delete"), edit_enabled);
                    if (action == TextCellContextAction::Primary) {
                        locate_irregularity_row_on_plan(marker_index);
                    } else if (action == TextCellContextAction::Secondary) {
                        locate_scene_marker_row_in_scene_preview(
                            Canvas3DSceneMarkerListKind::Irregularity, marker_index);
                    } else if (action == TextCellContextAction::Tertiary) {
                        request_element_inspector(row.edit_id, "irregularity.change");
                    } else if (action == TextCellContextAction::Quaternary) {
                        request_element_delete(row.edit_id, "irregularity.change");
                    }
                    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                        request_element_inspector(row.edit_id, "irregularity.change");
                    }
                    continue;
                }
                    if (value.empty()) continue;
                    if (i == k_irregularity_file_path_column) {
                        render_file_path_cell_with_context(value, row.open_path,
                                                           tr("menu.open_in_explorer"), row.open_path);
                    } else {
                        ImGui::TextUnformatted(value.c_str());
                    }
                }
                ImGui::PopID();
            }
        }
        ImGui::EndTable();
    }
    focus_irregularities_next_ = false;
    ImGui::End();
}

void App::render_rolling_noises_window() {
    if (!show_rolling_noises_window_) return;
    if (dock_right_id_) ImGui::SetNextWindowDockID(dock_right_id_, ImGuiCond_FirstUseEver);
    if (focus_rolling_noises_next_) ImGui::SetNextWindowFocus();
    std::string title = tr("frame.rolling_noises") + "###RollingNoises";
    if (!ImGui::Begin(title.c_str(), &show_rolling_noises_window_)) {
        focus_rolling_noises_next_ = false;
        ImGui::End();
        return;
    }
    if (!has_model_) {
        ImGui::TextDisabled("-");
        focus_rolling_noises_next_ = false;
        ImGui::End();
        return;
    }
    ensure_table_cache();
    render_change_point_table(
        "rolling_noises", k_rolling_noise_columns,
        k_rolling_noise_distance_column, k_rolling_noise_file_path_column,
        table_cache_.rolling_noise_rows,
        table_cache_.rolling_noise_distance_width,
        table_cache_.rolling_noise_file_path_width,
        tr("column.file_name"),
        tr("menu.locate_on_plan"),
        tr("menu.locate_in_scene_preview"),
        tr("menu.open_in_explorer"),
        rolling_noise_list_scroll_row_,
        rolling_noise_list_highlight_row_,
        table_row_highlight_color(theme_color_),
        [this](size_t marker_index) {
            return marker_index < rolling_noise_marker_cache_.size() &&
                rolling_noise_marker_cache_[marker_index].has_value();
        },
        [this](size_t marker_index) { locate_rolling_noise_row_on_plan(marker_index); },
        can_locate_scene_preview_row(),
        [this](size_t marker_index) {
            locate_scene_marker_row_in_scene_preview(
                Canvas3DSceneMarkerListKind::RollingNoise, marker_index);
        },
        tr("dialog.element_properties"), tr("button.delete"),
        [this](size_t row) {
            return edit_actions_available() && row < model_.rolling_noises.size() &&
                !model_.rolling_noises[row].edit_id.empty();
        },
        [this](size_t row) {
            request_element_inspector(model_.rolling_noises[row].edit_id, "rollingNoise.change");
        },
        [this](size_t row) {
            request_element_delete(model_.rolling_noises[row].edit_id, "rollingNoise.change");
        });
    focus_rolling_noises_next_ = false;
    ImGui::End();
}

void App::render_map_sounds_window() {
    if (!show_map_sounds_window_) return;
    if (dock_right_id_) ImGui::SetNextWindowDockID(dock_right_id_, ImGuiCond_FirstUseEver);
    if (focus_map_sounds_next_) ImGui::SetNextWindowFocus();
    std::string title = tr("frame.map_sounds") + "###MapSounds";
    if (!ImGui::Begin(title.c_str(), &show_map_sounds_window_)) {
        focus_map_sounds_next_ = false;
        ImGui::End();
        return;
    }
    if (!has_model_) {
        ImGui::TextDisabled("-");
        focus_map_sounds_next_ = false;
        ImGui::End();
        return;
    }
    ensure_table_cache();
    render_map_sound_event_table(
        "map_sounds", k_map_sound_columns,
        k_map_sound_distance_column, k_map_sound_key_column, k_map_sound_file_path_column,
        table_cache_.map_sound_rows,
        table_cache_.map_sound_distance_width,
        table_cache_.map_sound_file_path_width,
        tr("column.file_name"),
        tr("menu.locate_on_plan"),
        tr("menu.locate_in_scene_preview"),
        tr("menu.find_in_sound_files"),
        tr("menu.open_in_explorer"),
        map_sound_list_scroll_row_,
        map_sound_list_highlight_row_,
        table_row_highlight_color(theme_color_),
        [this](size_t marker_index) {
            return marker_index < map_sound_marker_cache_.size() &&
                map_sound_marker_cache_[marker_index].has_value();
        },
        [this](size_t marker_index) { locate_map_sound_row_on_plan(marker_index); },
        can_locate_scene_preview_row(),
        [this](size_t marker_index) {
            locate_scene_marker_row_in_scene_preview(
                Canvas3DSceneMarkerListKind::MapSound, marker_index);
        },
        [this](const std::string& sound_key) { find_sound_file_for_sound_key(sound_key, false); },
        tr("dialog.element_properties"), tr("button.delete"),
        [this](size_t row) {
            return edit_actions_available() && row < model_.map_sounds.size() &&
                !model_.map_sounds[row].edit_id.empty();
        },
        [this](size_t row) {
            request_element_inspector(model_.map_sounds[row].edit_id, "mapSound.play");
        },
        [this](size_t row) {
            request_element_delete(model_.map_sounds[row].edit_id, "mapSound.play");
        });
    focus_map_sounds_next_ = false;
    ImGui::End();
}

void App::render_map_sound_3d_window() {
    if (!show_map_sound_3d_window_) return;
    if (dock_right_id_) ImGui::SetNextWindowDockID(dock_right_id_, ImGuiCond_FirstUseEver);
    if (focus_map_sound_3d_next_) ImGui::SetNextWindowFocus();
    std::string title = tr("frame.map_sound_3d") + "###MapSound3D";
    if (!ImGui::Begin(title.c_str(), &show_map_sound_3d_window_)) {
        focus_map_sound_3d_next_ = false;
        ImGui::End();
        return;
    }
    if (!has_model_) {
        ImGui::TextDisabled("-");
        focus_map_sound_3d_next_ = false;
        ImGui::End();
        return;
    }
    ensure_table_cache();
    render_map_sound_event_table(
        "map_sound_3d", k_map_sound_columns,
        k_map_sound_distance_column, k_map_sound_key_column, k_map_sound_file_path_column,
        table_cache_.map_sound_3d_rows,
        table_cache_.map_sound_3d_distance_width,
        table_cache_.map_sound_3d_file_path_width,
        tr("column.file_name"),
        tr("menu.locate_on_plan"),
        tr("menu.locate_in_scene_preview"),
        tr("menu.find_in_sound_3d_files"),
        tr("menu.open_in_explorer"),
        map_sound_3d_list_scroll_row_,
        map_sound_3d_list_highlight_row_,
        table_row_highlight_color(theme_color_),
        [this](size_t marker_index) {
            return marker_index < map_sound_3d_marker_cache_.size() &&
                map_sound_3d_marker_cache_[marker_index].has_value();
        },
        [this](size_t marker_index) { locate_map_sound_3d_row_on_plan(marker_index); },
        can_locate_scene_preview_row(),
        [this](size_t marker_index) {
            locate_scene_marker_row_in_scene_preview(
                Canvas3DSceneMarkerListKind::MapSound3D, marker_index);
        },
        [this](const std::string& sound_key) { find_sound_file_for_sound_key(sound_key, true); },
        tr("dialog.element_properties"), tr("button.delete"),
        [this](size_t row) {
            return edit_actions_available() && row < model_.map_sound_3d.size() &&
                !model_.map_sound_3d[row].edit_id.empty();
        },
        [this](size_t row) {
            request_element_inspector(model_.map_sound_3d[row].edit_id, "mapSound3D.put");
        },
        [this](size_t row) {
            request_element_delete(model_.map_sound_3d[row].edit_id, "mapSound3D.put");
        });
    focus_map_sound_3d_next_ = false;
    ImGui::End();
}

void App::render_flange_noises_window() {
    if (!show_flange_noises_window_) return;
    if (dock_right_id_) ImGui::SetNextWindowDockID(dock_right_id_, ImGuiCond_FirstUseEver);
    if (focus_flange_noises_next_) ImGui::SetNextWindowFocus();
    std::string title = tr("frame.flange_noises") + "###FlangeNoises";
    if (!ImGui::Begin(title.c_str(), &show_flange_noises_window_)) {
        focus_flange_noises_next_ = false;
        ImGui::End();
        return;
    }
    if (!has_model_) {
        ImGui::TextDisabled("-");
        focus_flange_noises_next_ = false;
        ImGui::End();
        return;
    }
    ensure_table_cache();
    render_change_point_table(
        "flange_noises", k_flange_noise_columns,
        k_flange_noise_distance_column, k_flange_noise_file_path_column,
        table_cache_.flange_noise_rows,
        table_cache_.flange_noise_distance_width,
        table_cache_.flange_noise_file_path_width,
        tr("column.file_name"),
        tr("menu.locate_on_plan"),
        tr("menu.locate_in_scene_preview"),
        tr("menu.open_in_explorer"),
        flange_noise_list_scroll_row_,
        flange_noise_list_highlight_row_,
        table_row_highlight_color(theme_color_),
        [this](size_t marker_index) {
            return marker_index < flange_noise_marker_cache_.size() &&
                flange_noise_marker_cache_[marker_index].has_value();
        },
        [this](size_t marker_index) { locate_flange_noise_row_on_plan(marker_index); },
        can_locate_scene_preview_row(),
        [this](size_t marker_index) {
            locate_scene_marker_row_in_scene_preview(
                Canvas3DSceneMarkerListKind::FlangeNoise, marker_index);
        },
        tr("dialog.element_properties"), tr("button.delete"),
        [this](size_t row) {
            return edit_actions_available() && row < model_.flange_noises.size() &&
                !model_.flange_noises[row].edit_id.empty();
        },
        [this](size_t row) {
            request_element_inspector(model_.flange_noises[row].edit_id, "flangeNoise.change");
        },
        [this](size_t row) {
            request_element_delete(model_.flange_noises[row].edit_id, "flangeNoise.change");
        });
    focus_flange_noises_next_ = false;
    ImGui::End();
}

void App::render_joint_noises_window() {
    if (!show_joint_noises_window_) return;
    if (dock_right_id_) ImGui::SetNextWindowDockID(dock_right_id_, ImGuiCond_FirstUseEver);
    if (focus_joint_noises_next_) ImGui::SetNextWindowFocus();
    std::string title = tr("frame.joint_noises") + "###JointNoises";
    if (!ImGui::Begin(title.c_str(), &show_joint_noises_window_)) {
        focus_joint_noises_next_ = false;
        ImGui::End();
        return;
    }
    if (!has_model_) {
        ImGui::TextDisabled("-");
        focus_joint_noises_next_ = false;
        ImGui::End();
        return;
    }
    ensure_table_cache();
    render_change_point_table(
        "joint_noises", k_joint_noise_columns,
        k_joint_noise_distance_column, k_joint_noise_file_path_column,
        table_cache_.joint_noise_rows,
        table_cache_.joint_noise_distance_width,
        table_cache_.joint_noise_file_path_width,
        tr("column.file_name"),
        tr("menu.locate_on_plan"),
        tr("menu.locate_in_scene_preview"),
        tr("menu.open_in_explorer"),
        joint_noise_list_scroll_row_,
        joint_noise_list_highlight_row_,
        table_row_highlight_color(theme_color_),
        [this](size_t marker_index) {
            return marker_index < joint_noise_marker_cache_.size() &&
                joint_noise_marker_cache_[marker_index].has_value();
        },
        [this](size_t marker_index) { locate_joint_noise_row_on_plan(marker_index); },
        can_locate_scene_preview_row(),
        [this](size_t marker_index) {
            locate_scene_marker_row_in_scene_preview(
                Canvas3DSceneMarkerListKind::JointNoise, marker_index);
        },
        tr("dialog.element_properties"), tr("button.delete"),
        [this](size_t row) {
            return edit_actions_available() && row < model_.joint_noises.size() &&
                !model_.joint_noises[row].edit_id.empty();
        },
        [this](size_t row) {
            request_element_inspector(model_.joint_noises[row].edit_id, "jointNoise.play");
        },
        [this](size_t row) {
            request_element_delete(model_.joint_noises[row].edit_id, "jointNoise.play");
        });
    focus_joint_noises_next_ = false;
    ImGui::End();
}

void App::render_backgrounds_window() {
    if (!show_backgrounds_window_) return;
    if (dock_right_id_) ImGui::SetNextWindowDockID(dock_right_id_, ImGuiCond_FirstUseEver);
    if (focus_backgrounds_next_) ImGui::SetNextWindowFocus();
    std::string title = tr("frame.backgrounds") + "###Backgrounds";
    if (!ImGui::Begin(title.c_str(), &show_backgrounds_window_)) {
        focus_backgrounds_next_ = false;
        ImGui::End();
        return;
    }
    if (!has_model_) {
        ImGui::TextDisabled("-");
        focus_backgrounds_next_ = false;
        ImGui::End();
        return;
    }
    ensure_table_cache();
    const bool can_locate_scene_preview = can_locate_scene_preview_row();
    render_event_table(
        "backgrounds", k_background_columns,
        k_background_distance_column, k_background_file_path_column,
        table_cache_.background_rows,
        table_cache_.background_distance_width,
        table_cache_.background_file_path_width,
        tr("column.file_name"), tr("menu.open_in_explorer"),
        background_list_scroll_row_, background_list_highlight_row_,
        table_row_highlight_color(theme_color_),
        [this, can_locate_scene_preview](int row_index, int column,
                                         const std::string& value) {
            const size_t marker_index = static_cast<size_t>(row_index);
            if (column == k_background_distance_column) {
                const bool can_locate = marker_index < background_marker_cache_.size() &&
                    background_marker_cache_[marker_index].has_value();
                const bool can_edit = edit_actions_available() &&
                    marker_index < model_.backgrounds.size() &&
                    !model_.backgrounds[marker_index].edit_id.empty();
                const TextCellContextAction action = render_marker_text_cell_with_context(
                    value,
                    tr("menu.locate_on_plan"), can_locate,
                    tr("menu.locate_in_scene_preview"), can_locate_scene_preview,
                    tr("dialog.element_properties"), can_edit,
                    tr("button.delete"), can_edit);
                if (action == TextCellContextAction::Primary) {
                    locate_background_row_on_plan(marker_index);
                } else if (action == TextCellContextAction::Secondary) {
                    locate_scene_marker_row_in_scene_preview(
                        Canvas3DSceneMarkerListKind::Background, marker_index);
                } else if (action == TextCellContextAction::Tertiary) {
                    request_element_inspector(model_.backgrounds[marker_index].edit_id,
                                              "background.change");
                } else if (action == TextCellContextAction::Quaternary) {
                    request_element_delete(model_.backgrounds[marker_index].edit_id,
                                           "background.change");
                }
                return true;
            }
            if (column == k_background_structure_key_column && !value.empty()) {
                ImGui::PushID("background_structure_key");
                const bool should_find = render_text_cell_with_context(
                    value, tr("menu.find_in_structure_models"), !blank_ascii(value));
                ImGui::PopID();
                if (should_find) find_structure_model_for_structure_key(value);
                return true;
            }
            return false;
        });
    focus_backgrounds_next_ = false;
    ImGui::End();
}

void App::render_adhesions_window() {
    if (!show_adhesions_window_) return;
    if (dock_right_id_) ImGui::SetNextWindowDockID(dock_right_id_, ImGuiCond_FirstUseEver);
    if (focus_adhesions_next_) ImGui::SetNextWindowFocus();
    std::string title = tr("frame.adhesions") + "###Adhesions";
    if (!ImGui::Begin(title.c_str(), &show_adhesions_window_)) {
        focus_adhesions_next_ = false;
        ImGui::End();
        return;
    }
    if (!has_model_) {
        ImGui::TextDisabled("-");
        focus_adhesions_next_ = false;
        ImGui::End();
        return;
    }
    ensure_table_cache();
    render_change_point_table(
        "adhesions", k_adhesion_columns,
        k_adhesion_distance_column, k_adhesion_file_path_column,
        table_cache_.adhesion_rows,
        table_cache_.adhesion_distance_width,
        table_cache_.adhesion_file_path_width,
        tr("column.file_name"), tr("menu.locate_on_plan"),
        tr("menu.locate_in_scene_preview"), tr("menu.open_in_explorer"),
        adhesion_list_scroll_row_, adhesion_list_highlight_row_,
        table_row_highlight_color(theme_color_),
        [this](size_t row) {
            return row < adhesion_marker_cache_.size() &&
                adhesion_marker_cache_[row].has_value();
        },
        [this](size_t row) { locate_adhesion_row_on_plan(row); },
        can_locate_scene_preview_row(),
        [this](size_t row) {
            locate_scene_marker_row_in_scene_preview(
                Canvas3DSceneMarkerListKind::Adhesion, row);
        },
        tr("dialog.element_properties"), tr("button.delete"),
        [this](size_t row) {
            return edit_actions_available() && row < model_.adhesions.size() &&
                !model_.adhesions[row].edit_id.empty();
        },
        [this](size_t row) {
            request_element_inspector(model_.adhesions[row].edit_id, "adhesion.change");
        },
        [this](size_t row) {
            request_element_delete(model_.adhesions[row].edit_id, "adhesion.change");
        });
    focus_adhesions_next_ = false;
    ImGui::End();
}

void App::render_cab_illuminance_window() {
    if (!show_cab_illuminance_window_) return;
    if (dock_right_id_) ImGui::SetNextWindowDockID(dock_right_id_, ImGuiCond_FirstUseEver);
    if (focus_cab_illuminance_next_) ImGui::SetNextWindowFocus();
    std::string title = tr("frame.cab_illuminance") + "###CabIlluminance";
    if (!ImGui::Begin(title.c_str(), &show_cab_illuminance_window_)) {
        focus_cab_illuminance_next_ = false;
        ImGui::End();
        return;
    }
    if (!has_model_) {
        ImGui::TextDisabled("-");
        focus_cab_illuminance_next_ = false;
        ImGui::End();
        return;
    }
    ensure_table_cache();
    render_change_point_table(
        "cab_illuminance", k_cab_illuminance_columns,
        k_cab_illuminance_distance_column, k_cab_illuminance_file_path_column,
        table_cache_.cab_illuminance_rows,
        table_cache_.cab_illuminance_distance_width,
        table_cache_.cab_illuminance_file_path_width,
        tr("column.file_name"), tr("menu.locate_on_plan"),
        tr("menu.locate_in_scene_preview"), tr("menu.open_in_explorer"),
        cab_illuminance_list_scroll_row_, cab_illuminance_list_highlight_row_,
        table_row_highlight_color(theme_color_),
        [this](size_t row) {
            return row < cab_illuminance_marker_cache_.size() &&
                cab_illuminance_marker_cache_[row].has_value();
        },
        [this](size_t row) { locate_cab_illuminance_row_on_plan(row); },
        can_locate_scene_preview_row(),
        [this](size_t row) {
            locate_scene_marker_row_in_scene_preview(
                Canvas3DSceneMarkerListKind::CabIlluminance, row);
        },
        tr("dialog.element_properties"), tr("button.delete"),
        [this](size_t row) {
            return edit_actions_available() && row < model_.cab_illuminance.size() &&
                !model_.cab_illuminance[row].edit_id.empty();
        },
        [this](size_t row) {
            request_element_inspector(model_.cab_illuminance[row].edit_id,
                                      "cabIlluminance.change");
        },
        [this](size_t row) {
            request_element_delete(model_.cab_illuminance[row].edit_id,
                                   "cabIlluminance.change");
        });
    focus_cab_illuminance_next_ = false;
    ImGui::End();
}

void App::render_fogs_window() {
    if (!show_fogs_window_) return;
    if (dock_right_id_) ImGui::SetNextWindowDockID(dock_right_id_, ImGuiCond_FirstUseEver);
    if (focus_fogs_next_) ImGui::SetNextWindowFocus();
    std::string title = tr("frame.fogs") + "###Fogs";
    if (!ImGui::Begin(title.c_str(), &show_fogs_window_)) {
        focus_fogs_next_ = false;
        ImGui::End();
        return;
    }
    if (!has_model_) {
        ImGui::TextDisabled("-");
        focus_fogs_next_ = false;
        ImGui::End();
        return;
    }
    ensure_table_cache();
    render_change_point_table(
        "fogs", k_fog_columns,
        k_fog_distance_column, k_fog_file_path_column,
        table_cache_.fog_rows,
        table_cache_.fog_distance_width,
        table_cache_.fog_file_path_width,
        tr("column.file_name"), tr("menu.locate_on_plan"),
        tr("menu.locate_in_scene_preview"), tr("menu.open_in_explorer"),
        fog_list_scroll_row_, fog_list_highlight_row_,
        table_row_highlight_color(theme_color_),
        [this](size_t row) {
            return row < fog_marker_cache_.size() &&
                fog_marker_cache_[row].has_value();
        },
        [this](size_t row) { locate_fog_row_on_plan(row); },
        can_locate_scene_preview_row(),
        [this](size_t row) {
            locate_scene_marker_row_in_scene_preview(
                Canvas3DSceneMarkerListKind::Fog, row);
        },
        tr("dialog.element_properties"), tr("button.delete"),
        [this](size_t row) {
            return edit_actions_available() && row < model_.fogs.size() &&
                !model_.fogs[row].edit_id.empty();
        },
        [this](size_t row) {
            request_element_inspector(model_.fogs[row].edit_id, "fog.change");
        },
        [this](size_t row) {
            request_element_delete(model_.fogs[row].edit_id, "fog.change");
        });
    focus_fogs_next_ = false;
    ImGui::End();
}

void App::render_legacy_fogs_window() {
    if (!show_legacy_fogs_window_) return;
    if (dock_right_id_) ImGui::SetNextWindowDockID(dock_right_id_, ImGuiCond_FirstUseEver);
    if (focus_legacy_fogs_next_) ImGui::SetNextWindowFocus();
    std::string title = tr("frame.legacy_fogs") + "###LegacyFogs";
    if (!ImGui::Begin(title.c_str(), &show_legacy_fogs_window_)) {
        focus_legacy_fogs_next_ = false;
        ImGui::End();
        return;
    }
    if (!has_model_) {
        ImGui::TextDisabled("-");
        focus_legacy_fogs_next_ = false;
        ImGui::End();
        return;
    }
    ensure_table_cache();
    const bool can_locate_scene_preview = can_locate_scene_preview_row();
    render_event_table(
        "legacyFogs", k_legacy_fog_columns,
        k_legacy_fog_distance_column, k_legacy_fog_file_path_column,
        table_cache_.legacy_fog_rows,
        table_cache_.legacy_fog_distance_width,
        table_cache_.legacy_fog_file_path_width,
        tr("column.file_name"), tr("menu.open_in_explorer"),
        legacy_fog_list_scroll_row_, legacy_fog_list_highlight_row_,
        table_row_highlight_color(theme_color_),
        [this, can_locate_scene_preview](int row_index, int column,
                                         const std::string& value) {
            if (column != k_legacy_fog_distance_column) return false;
            const size_t marker_index = static_cast<size_t>(row_index);
            const bool can_locate = marker_index < legacy_fog_marker_cache_.size() &&
                legacy_fog_marker_cache_[marker_index].has_value();
            const TextCellContextAction action = render_marker_text_cell_with_context(
                value,
                tr("menu.locate_on_plan"), can_locate,
                tr("menu.locate_in_scene_preview"), can_locate_scene_preview);
            if (action == TextCellContextAction::Primary) {
                locate_legacy_fog_row_on_plan(marker_index);
            } else if (action == TextCellContextAction::Secondary) {
                locate_scene_marker_row_in_scene_preview(
                    Canvas3DSceneMarkerListKind::LegacyFog, marker_index);
            }
            return true;
        });
    focus_legacy_fogs_next_ = false;
    ImGui::End();
}

void App::sync_lighting_edit_state() {
    const auto bind = [this](LightingStatementEditState& state,
                             const std::vector<TableRow>& rows,
                             const char* row_kind,
                             std::initializer_list<const char*> field_keys) {
        if (rows.size() != 1 || rows.front().edit_id.empty()) {
            state = LightingStatementEditState{};
            return;
        }

        const TableRow& row = rows.front();
        const TableRow* original_row = &row;
        const auto original = original_edit_rows_.find(row.edit_id);
        if (original != original_edit_rows_.end() &&
            original->second.row_kind == row_kind) {
            original_row = &original->second.row;
        }

        bool rebuild = state.edit_id != row.edit_id || state.row_kind != row_kind ||
            state.fields.size() != field_keys.size();
        if (!rebuild) {
            size_t index = 0;
            for (const char* key : field_keys) {
                if (state.fields[index].original_value != table_cell(*original_row, key)) {
                    rebuild = true;
                    break;
                }
                ++index;
            }
        }
        if (!rebuild) return;

        LightingStatementEditState next;
        next.edit_id = row.edit_id;
        next.row_kind = row_kind;
        const EditSourceFileInfo* source_file =
            find_model_source_file(model_, row.source.file_path);
        next.expected_source_hash = expected_source_hash_for_edit_target(
            model_, pending_edit_changes_, row.edit_id,
            source_file ? source_file->source_hash : std::string{},
            row.source.file_path);
        for (const char* key : field_keys) {
            MapElementEditFieldState field;
            field.key = key;
            field.backend_key = key;
            field.target_edit_id = row.edit_id;
            field.expected_source_hash = next.expected_source_hash;
            field.label = key;
            field.original_value = table_cell(*original_row, key);
            field.numeric_constraint = MapElementNumericConstraint::Finite;
            field.required = true;
            set_edit_field_buffer(field, table_cell(row, key));
            next.fields.push_back(std::move(field));
        }
        state = std::move(next);
    };

    bind(lighting_edit_.ambient, model_.light_ambient, "light.ambient",
         {"red", "green", "blue"});
    bind(lighting_edit_.diffuse, model_.light_diffuse, "light.diffuse",
         {"red", "green", "blue"});
    bind(lighting_edit_.direction, model_.light_direction, "light.direction",
         {"pitch", "yaw"});
}

bool App::apply_lighting_changes() {
    if (!edit_actions_available()) return false;
    sync_lighting_edit_state();

    std::map<std::string, MapElementPendingChange> candidate = pending_edit_changes_;
    const auto apply_statement = [this, &candidate](LightingStatementEditState& state) {
        if (state.edit_id.empty() || state.row_kind.empty()) return true;

        MapElementPendingChange update;
        update.change_id = "change-" + state.edit_id;
        update.edit_id = state.edit_id;
        update.row_kind = state.row_kind;
        update.operation = "update";
        update.expected_source_hash = expected_source_hash_for_edit_target(
            model_, pending_edit_changes_, state.edit_id, state.expected_source_hash, {});

        for (MapElementEditFieldState& field : state.fields) {
            std::string value = trim_gui_ascii_copy(edit_field_buffer_text(field));
            if (field.required && value.empty()) {
                set_program_status("status.edit.required_field");
                return false;
            }
            const bool changed = value != field.original_value;
            if (!validate_and_canonicalize_edit_field(field, changed)) {
                set_program_status("status.edit.invalid_number");
                return false;
            }
            value = trim_gui_ascii_copy(edit_field_buffer_text(field));
            if (value != field.original_value) {
                update.field_changes[field.backend_key.empty() ? field.key : field.backend_key] = value;
            }
        }

        const auto existing = pending_edit_changes_.find(state.edit_id);
        if (existing != pending_edit_changes_.end() &&
            existing->second.operation == "insert") {
            if (!update.field_changes.empty()) {
                MapElementPendingChange merged = existing->second;
                for (const auto& field : update.field_changes) {
                    merged.field_changes[field.first] = field.second;
                }
                candidate[state.edit_id] = std::move(merged);
            }
            return true;
        }

        candidate.erase(state.edit_id);
        if (!update.field_changes.empty()) {
            candidate[state.edit_id] = std::move(update);
        }
        return true;
    };

    if (!apply_statement(lighting_edit_.ambient) ||
        !apply_statement(lighting_edit_.diffuse) ||
        !apply_statement(lighting_edit_.direction)) {
        return false;
    }
    if (!apply_edit_ledger_to_preview(candidate, std::nullopt, false)) return false;

    lighting_edit_ = LightingEditState{};
    set_program_status("status.edit.applied_to_preview");
    return true;
}

void App::render_lighting_window() {
    if (!show_lighting_window_) return;
    if (dock_right_id_) ImGui::SetNextWindowDockID(dock_right_id_, ImGuiCond_FirstUseEver);
    std::string title = tr("frame.lighting") + "###Lighting";
    if (!ImGui::Begin(title.c_str(), &show_lighting_window_)) {
        ImGui::End();
        return;
    }
    if (!has_model_) {
        ImGui::TextDisabled("-");
        ImGui::End();
        return;
    }

    sync_lighting_edit_state();
    const auto has_form_changes = [this](const LightingStatementEditState& state) {
        if (std::any_of(state.fields.begin(), state.fields.end(),
                        [](const MapElementEditFieldState& field) {
                            return edit_field_buffer_text(field) != field.original_value;
                        })) {
            return true;
        }
        const auto pending = pending_edit_changes_.find(state.edit_id);
        return pending != pending_edit_changes_.end() &&
            pending->second.operation == "update";
    };
    const bool has_changes = has_form_changes(lighting_edit_.ambient) ||
        has_form_changes(lighting_edit_.diffuse) || has_form_changes(lighting_edit_.direction);
    ImGui::BeginDisabled(!edit_actions_available() || !has_changes);
    if (ImGui::Button(tr("button.apply").c_str())) {
        apply_lighting_changes();
    }
    ImGui::EndDisabled();
    ImGui::Separator();

    static constexpr std::array<const char*, 3> k_color_parameter_labels = {
        "red", "green", "blue",
    };
    static constexpr std::array<const char*, 2> k_direction_parameter_labels = {
        "pitch", "yaw",
    };
    const auto render_group = [this](const char* statement, const char* template_id,
                                     const auto& parameter_labels,
                                     const std::vector<TableRow>& rows,
                                     LightingStatementEditState& state) {
        const TableRow* row = rows.size() == 1 ? &rows.front() : nullptr;
        const bool editable_row = row && !row->edit_id.empty() &&
            state.edit_id == row->edit_id && state.fields.size() == parameter_labels.size();

        ImGui::PushID(statement);
        ImGui::TextUnformatted(statement);
        ImGui::SameLine();
        ImGui::BeginDisabled(!edit_actions_available() || !row || row->edit_id.empty());
        if (ImGui::SmallButton(tr("button.delete").c_str())) {
            request_element_delete(row->edit_id, template_id);
        }
        ImGui::EndDisabled();

        for (size_t index = 0; index < parameter_labels.size(); ++index) {
            ImGui::PushID(static_cast<int>(index));
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(parameter_labels[index]);
            ImGui::SameLine();
            if (editable_row) {
                MapElementEditFieldState& field = state.fields[index];
                const bool changed = edit_field_buffer_text(field) != field.original_value;
                if (changed) {
                    ImGui::PushStyleColor(
                        ImGuiCol_FrameBg, ImVec4(0.28f, 0.23f, 0.08f, 1.0f));
                }
                ImGui::BeginDisabled(!edit_actions_available());
                render_map_element_field_control(
                    field, std::max(160.0f, ImGui::GetContentRegionAvail().x));
                ImGui::EndDisabled();
                if (ImGui::IsItemDeactivatedAfterEdit() &&
                    !validate_and_canonicalize_edit_field(field, true)) {
                    set_program_status("status.edit.invalid_number");
                }
                if (changed) ImGui::PopStyleColor();
            } else {
                std::string value = row ? table_cell(*row, parameter_labels[index]) : std::string{};
                ImGui::SetNextItemWidth(-1.0f);
                ImGui::BeginDisabled();
                ImGui::InputText("##value", &value);
                ImGui::EndDisabled();
            }
            ImGui::PopID();
        }

        if (!row) {
            ImGui::TextDisabled("%s", tr("label.light_statement_missing").c_str());
            ImGui::BeginDisabled(!edit_actions_available());
            if (ImGui::Button(tr("button.new").c_str())) {
                open_new_element_wizard_for_template(template_id);
            }
            ImGui::EndDisabled();
        }
        ImGui::PopID();
    };

    render_group("Light.Ambient", "light.ambient", k_color_parameter_labels,
                 model_.light_ambient, lighting_edit_.ambient);
    ImGui::Separator();
    render_group("Light.Diffuse", "light.diffuse", k_color_parameter_labels,
                 model_.light_diffuse, lighting_edit_.diffuse);
    ImGui::Separator();
    render_group("Light.Direction", "light.direction", k_direction_parameter_labels,
                 model_.light_direction, lighting_edit_.direction);
    ImGui::End();
}

void App::render_draw_distances_window() {
    if (!show_draw_distances_window_) return;
    if (dock_right_id_) ImGui::SetNextWindowDockID(dock_right_id_, ImGuiCond_FirstUseEver);
    if (focus_draw_distances_next_) ImGui::SetNextWindowFocus();
    std::string title = tr("frame.draw_distances") + "###DrawDistances";
    if (!ImGui::Begin(title.c_str(), &show_draw_distances_window_)) {
        focus_draw_distances_next_ = false;
        ImGui::End();
        return;
    }
    if (!has_model_) {
        ImGui::TextDisabled("-");
        focus_draw_distances_next_ = false;
        ImGui::End();
        return;
    }
    ensure_table_cache();
    render_change_point_table(
        "draw_distances", k_draw_distance_columns,
        k_draw_distance_distance_column, k_draw_distance_file_path_column,
        table_cache_.draw_distance_rows,
        table_cache_.draw_distance_distance_width,
        table_cache_.draw_distance_file_path_width,
        tr("column.file_name"),
        tr("menu.locate_on_plan"),
        tr("menu.locate_in_scene_preview"),
        tr("menu.open_in_explorer"),
        draw_distance_list_scroll_row_,
        draw_distance_list_highlight_row_,
        table_row_highlight_color(theme_color_),
        [this](size_t marker_index) {
            return marker_index < draw_distance_marker_cache_.size() &&
                draw_distance_marker_cache_[marker_index].has_value();
        },
        [this](size_t marker_index) { locate_draw_distance_row_on_plan(marker_index); },
        can_locate_scene_preview_row(),
        [this](size_t marker_index) {
            locate_scene_marker_row_in_scene_preview(
                Canvas3DSceneMarkerListKind::DrawDistance, marker_index);
        },
        tr("dialog.element_properties"), tr("button.delete"),
        [this](size_t row) {
            return edit_actions_available() && row < model_.draw_distances.size() &&
                !model_.draw_distances[row].edit_id.empty();
        },
        [this](size_t row) {
            request_element_inspector(model_.draw_distances[row].edit_id, "drawDistance.change");
        },
        [this](size_t row) {
            request_element_delete(model_.draw_distances[row].edit_id, "drawDistance.change");
        });
    focus_draw_distances_next_ = false;
    ImGui::End();
}

void App::render_speed_limits_window() {
    if (!show_speed_limits_window_) return;
    if (dock_right_id_) ImGui::SetNextWindowDockID(dock_right_id_, ImGuiCond_FirstUseEver);
    if (focus_speed_limits_next_) ImGui::SetNextWindowFocus();
    std::string title = tr("frame.speed_limits") + "###SpeedLimits";
    if (!ImGui::Begin(title.c_str(), &show_speed_limits_window_)) {
        focus_speed_limits_next_ = false;
        ImGui::End();
        return;
    }
    if (!has_model_) {
        ImGui::TextDisabled("-");
        focus_speed_limits_next_ = false;
        ImGui::End();
        return;
    }
    ensure_table_cache();
    render_change_point_table(
        "speed_limits", k_speed_limit_columns,
        k_speed_limit_distance_column, k_speed_limit_file_path_column,
        table_cache_.speed_limit_rows,
        table_cache_.speed_limit_distance_width,
        table_cache_.speed_limit_file_path_width,
        tr("column.file_name"), tr("menu.locate_on_plan"),
        tr("menu.locate_in_scene_preview"), tr("menu.open_in_explorer"),
        speed_limit_list_scroll_row_, speed_limit_list_highlight_row_,
        table_row_highlight_color(theme_color_),
        [this](size_t row) {
            return row < speed_limit_marker_cache_.size() &&
                speed_limit_marker_cache_[row].has_value();
        },
        [this](size_t row) { locate_speed_limit_row_on_plan(row); },
        can_locate_scene_preview_row(),
        [this](size_t row) {
            locate_scene_marker_row_in_scene_preview(
                Canvas3DSceneMarkerListKind::SpeedLimit, row);
        },
        tr("dialog.element_properties"), tr("button.delete"),
        [this](size_t row) {
            return edit_actions_available() && row < model_.speed_limit_rows.size() &&
                !model_.speed_limit_rows[row].edit_id.empty();
        },
        [this](size_t row) {
            request_element_inspector(model_.speed_limit_rows[row].edit_id, "speedlimit");
        },
        [this](size_t row) {
            request_element_delete(model_.speed_limit_rows[row].edit_id, "speedlimit");
        });
    focus_speed_limits_next_ = false;
    ImGui::End();
}

