/*
 * Copyright (c) 2026 Sapporo_ningyo
 *
 * Licensed under Apache License 2.0; see LICENSE and NOTICE.
 * The GUI uses Dear ImGui and ImPlot; see THIRD_PARTY_NOTICES.md.
 */

#pragma once

#include "kme.h"
#include "touch_input.h"

#include "canvas3D.h"
#include "text_decoder.h"
#include "imgui.h"
#include "misc/cpp/imgui_stdlib.h"
#include "repeater_linkage.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <initializer_list>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace datatable_internal {

inline constexpr size_t k_max_section_value_columns = 508;
inline constexpr float k_find_input_min_width = 80.0f;
inline constexpr float k_find_input_max_width = 280.0f;

std::string normalize_train_lookup_key(const std::string& key);
float scroll_x_table_height_for_rows(int row_count);
bool render_file_path_cell_with_context(
    const std::string& display_text, const std::string& open_path,
    const std::string& menu_label, const std::string& tooltip_text = {},
    ImU32 text_color = 0, const std::string& change_label = {},
    bool change_enabled = false);
bool render_resource_list_source(
    const ResourceListSource& source, const std::string& label,
    const std::string& open_label, const std::string& change_label = {},
    bool change_enabled = false);
std::string resource_list_unavailable_message(
    bool has_model, const std::string& no_map_message, std::string message,
    const std::string& resource_list_name);
bool render_resource_list_empty_overlay(const std::string& message,
                                        const std::string& button_label);
bool begin_text_cell_context_popup(const std::string& display_text,
                                   const char* item_id,
                                   const char* popup_id,
                                   bool* item_hovered = nullptr);
bool render_text_cell_with_context(const std::string& display_text,
                                   const std::string& menu_label,
                                   bool menu_enabled);

enum class TextCellContextAction {
    None,
    Primary,
    Secondary,
    Tertiary,
    Quaternary,
};

TextCellContextAction render_text_cell_with_context_actions(
    const std::string& display_text,
    const std::string& primary_label, bool primary_enabled,
    const std::string& secondary_label, bool secondary_enabled,
    const std::string& tertiary_label = {}, bool tertiary_enabled = false,
    const std::string& quaternary_label = {}, bool quaternary_enabled = false);
TextCellContextAction render_marker_text_cell_with_context(
    const std::string& display_text,
    const std::string& locate_on_plan_label, bool locate_on_plan_enabled,
    const std::string& locate_in_scene_label, bool locate_in_scene_enabled,
    const std::string& properties_label = {}, bool properties_enabled = false,
    const std::string& delete_label = {}, bool delete_enabled = false);

struct RepeaterTextCellContextAction {
    TextCellContextAction navigation = TextCellContextAction::None;
    RepeaterDeleteMode delete_mode = RepeaterDeleteMode::EntireChain;
    bool delete_requested = false;
    bool hovered = false;
};


RepeaterTextCellContextAction render_repeater_text_cell_with_context_actions(
    const std::string& display_text,
    const std::string& locate_on_plan_label, bool locate_on_plan_enabled,
    const std::string& locate_in_scene_label, bool locate_in_scene_enabled,
    const std::string& properties_label, const std::string& delete_label,
    const std::string& delete_all_label,
    const std::string& delete_change_point_label,
    const std::string& trim_to_change_point_label,
    const std::string& start_from_change_point_label,
    bool edit_enabled, size_t chain_begin_index, size_t chain_begin_count);
std::vector<std::string> split_structure_key_list(const std::string& text);
std::string render_text_cell_with_submenu(
    const std::string& display_text, const std::string& menu_label,
    const std::vector<std::string>& menu_items);

bool contains_ascii_case_insensitive(const std::string& text,
                                     const std::string& query);
bool equals_ascii_case_insensitive(const std::string& text,
                                   const std::string& query);
std::string ascii_case_key(const std::string& text);
bool matches_find_query(const std::string& text, const std::string& query,
                        bool exact_match);
void replace_all(std::string& text, const std::string& from,
                 const std::string& to);
std::string format_find_match_status(std::string format, size_t current,
                                     size_t total);

struct TableFindRowsView {
    const std::vector<CachedTableRow>* cached_rows = nullptr;
    const EditableListEditState* edit = nullptr;
    const EditableListSpec* spec = nullptr;

    size_t size() const {
        return edit && edit->rows_initialized
            ? edit->visible_rows.size()
            : cached_rows->size();
    }

    bool searchable(size_t row_index) const {
        if (!edit || !edit->rows_initialized) {
            return row_index < cached_rows->size();
        }
        if (row_index >= edit->visible_rows.size()) return false;
        const size_t draft_index = edit->visible_rows[row_index];
        return draft_index < edit->rows.size() &&
            !edit->rows[draft_index].deleted;
    }

    const std::string* cell(size_t row_index, size_t column) const {
        if (!edit || !edit->rows_initialized) {
            if (row_index >= cached_rows->size() ||
                column >= (*cached_rows)[row_index].cells.size()) {
                return nullptr;
            }
            return &(*cached_rows)[row_index].cells[column];
        }
        if (!spec || row_index >= edit->visible_rows.size() ||
            column < spec->cache_column_offset) {
            return nullptr;
        }
        const size_t field = column - spec->cache_column_offset;
        const size_t draft_index = edit->visible_rows[row_index];
        if (draft_index >= edit->rows.size() ||
            edit->rows[draft_index].deleted ||
            field >= edit->rows[draft_index].values.size()) {
            return nullptr;
        }
        return &edit->rows[draft_index].values[field];
    }
};

TableFindRowsView editable_table_find_rows(
    const std::vector<CachedTableRow>& rows,
    const EditableListEditState& edit,
    const EditableListSpec& spec);
void reset_table_find_results(TableFindState& state);
void run_table_find(TableFindState& state,
                    const TableFindRowsView& rows,
                    std::initializer_list<size_t> search_columns);
void set_exact_table_find_query(TableFindState& state,
                                const std::string& query);
void step_table_find(TableFindState& state, int delta);
std::string table_find_status_text(
    const TableFindState& state,
    const std::string& find_no_match,
    const std::string& find_match,
    const std::string& unused_no_match,
    const std::string& unused_match);

void render_status_line(const std::string& text);
void expand_width_for_text(float& width, const std::string& text);
bool all_flags_set(const std::vector<unsigned char>& flags);
bool all_flags_set_in_range(const std::vector<unsigned char>& flags,
                            size_t begin, size_t count);
void set_all_flags(std::vector<unsigned char>& flags, bool value);
void set_flags_in_range(std::vector<unsigned char>& flags,
                        size_t begin, size_t count, bool value);
ImU32 table_row_highlight_color(ImVec4 theme_color);
void setup_fixed_table_header();
bool render_find_panel_toggle(const char* id, const std::string& label,
                              bool expanded);
void render_find_panel_border(ImVec2 min, ImVec2 max);
float button_width_for_label(const std::string& label);
float radio_button_width_for_label(const std::string& label);
float width_after_previous_item(std::initializer_list<float> item_widths);
float find_panel_line_right_x(float right_padding);
bool same_line_if_next_item_fits(float next_item_width, float right_padding);
float available_width_after_current_item(float right_padding);
float find_input_width(float available_width, float all_controls_width,
                       float step_controls_width);

struct EditableCellInteraction {
    bool hovered = false;
    bool left_clicked = false;
    bool right_clicked = false;
    bool double_clicked = false;
};


EditableCellInteraction render_editable_cell_button(
    const std::string& display, bool selected, ImU32 text_color,
    float width, float height);
bool is_invalid_track_key_row(const TableRow& row);

inline constexpr float k_show_column_width = 56.0f;
inline constexpr ImU32 k_find_match_row_color = IM_COL32(104, 184, 255, 96);
inline constexpr ImU32 k_unused_structure_model_row_color = IM_COL32(72, 196, 112, 120);
inline constexpr ImU32 k_invalid_track_key_row_color = IM_COL32(255, 72, 72, 130);
inline constexpr ImU32 k_pending_edit_row_color = IM_COL32(255, 196, 64, 92);
inline constexpr ImU32 k_pending_delete_row_color = IM_COL32(255, 96, 64, 118);
inline constexpr const char* k_invalid_track_key_cell = "_invalidTrackKey";

inline constexpr TableColumnDef k_structure_model_columns[] = {
    {"rowNumber", "#", 40.0f},
    {"structureKey", "structureKey", 120.0f},
    {"filePath", "filePath", 200.0f},
};
inline constexpr int k_structure_model_key_column = 1;
inline constexpr int k_structure_model_file_path_column = IM_ARRAYSIZE(k_structure_model_columns) - 1;

inline constexpr TableColumnDef k_other_train_columns[] = {
    {"rowNumber", "#", 40.0f},
    {"distance", "distance", 110.0f},
    {"trainKey", "trainKey", 120.0f},
    {"filePath", "filePath", 200.0f},
    {"trackKey", "trackKey", 90.0f},
    {"direction", "direction", 80.0f},
};
inline constexpr int k_other_train_distance_column = 1;
inline constexpr int k_other_train_file_path_column = 3;

inline constexpr TableColumnDef k_other_train_stop_columns[] = {
    {"rowNumber", "#", 40.0f},
    {"distance", "distance", 110.0f},
    {"trainKey", "trainKey", 120.0f},
    {"decelerate", "decelerate", 90.0f},
    {"stopTime", "stopTime", 90.0f},
    {"accelerate", "accelerate", 90.0f},
    {"speed", "speed", 80.0f},
    {"filePath", "filePath", 200.0f},
};
inline constexpr int k_other_train_stop_distance_column = 1;
inline constexpr int k_other_train_stop_file_path_column = IM_ARRAYSIZE(k_other_train_stop_columns) - 1;

inline constexpr TableColumnDef k_sound_list_columns[] = {
    {"rowNumber", "#", 40.0f},
    {"soundKey", "soundKey", 120.0f},
    {"filePath", "filePath", 200.0f},
    {"bufferCount", "bufferCount", 90.0f},
};
inline constexpr int k_sound_list_key_column = 1;
inline constexpr int k_sound_list_file_path_column = 2;
inline constexpr int k_sound_list_buffer_count_column = 3;

inline constexpr TableColumnDef k_structure_put_columns[] = {
    {"distance", "distance", 0.0f},
    {"method", "method", 0.0f},
    {"structureKey", "structureKey", 0.0f},
    {"trackKey", "trackKey", 0.0f},
    {"x", "x", 0.0f},
    {"y", "y", 0.0f},
    {"z", "z", 0.0f},
    {"rx", "rx", 0.0f},
    {"ry", "ry", 0.0f},
    {"rz", "rz", 0.0f},
    {"tilt", "tilt", 0.0f},
    {"span", "span", 0.0f},
    {"filePath", "filePath", 200.0f},
};

inline constexpr TableColumnDef k_structure_between_columns[] = {
    {"distance", "distance", 0.0f},
    {"method", "method", 0.0f},
    {"structureKey", "structureKey", 0.0f},
    {"trackKey1", "trackKey1", 0.0f},
    {"trackKey2", "trackKey2", 0.0f},
    {"flag", "flag", 0.0f},
    {"filePath", "filePath", 200.0f},
};
inline constexpr int k_structure_distance_column = 0;
inline constexpr int k_structure_key_column = 2;
inline constexpr int k_structure_put_file_path_column = IM_ARRAYSIZE(k_structure_put_columns) - 1;
inline constexpr int k_structure_between_file_path_column = IM_ARRAYSIZE(k_structure_between_columns) - 1;

inline constexpr TableColumnDef k_repeater_columns[] = {
    {"rowNumber", "#", 40.0f},
    {"distance", "distance", 110.0f},
    {"method", "method", 0.0f},
    {"repeaterKey", "repeaterKey", 0.0f},
    {"trackKey", "trackKey", 0.0f},
    {"x", "x", 0.0f},
    {"y", "y", 0.0f},
    {"z", "z", 0.0f},
    {"rx", "rx", 0.0f},
    {"ry", "ry", 0.0f},
    {"rz", "rz", 0.0f},
    {"tilt", "tilt", 0.0f},
    {"span", "span", 0.0f},
    {"interval", "interval", 0.0f},
    {"structureKeys", "structureKeys", 120.0f},
    {"filePath", "filePath", 200.0f},
};
inline constexpr int k_repeater_distance_column = 1;
inline constexpr int k_repeater_interval_column = 13;
inline constexpr int k_repeater_structure_keys_column = 14;
inline constexpr int k_repeater_file_path_column = IM_ARRAYSIZE(k_repeater_columns) - 1;

inline constexpr TableColumnDef k_signal_aspect_fixed_columns[] = {
    {"rowNumber", "#", 40.0f},
    {"signalAspectKey", "signalAspectKey", 150.0f},
};
inline constexpr int k_signal_aspect_key_column = 1;
inline constexpr int k_signal_aspect_structure_key_column_offset = IM_ARRAYSIZE(k_signal_aspect_fixed_columns);
inline constexpr size_t k_max_signal_aspect_table_columns = 511;
inline constexpr size_t k_max_signal_aspect_structure_key_columns =
    k_max_signal_aspect_table_columns - static_cast<size_t>(k_signal_aspect_structure_key_column_offset);

inline constexpr TableColumnDef k_signal_columns[] = {
    {"rowNumber", "#", 40.0f},
    {"distance", "distance", 110.0f},
    {"section", "section", 70.0f},
    {"signalAspectKey", "signalAspectKey", 150.0f},
    {"trackKey", "trackKey", 90.0f},
    {"x", "x", 70.0f},
    {"y", "y", 70.0f},
    {"z", "z", 70.0f},
    {"rx", "rx", 70.0f},
    {"ry", "ry", 70.0f},
    {"rz", "rz", 70.0f},
    {"tilt", "tilt", 70.0f},
    {"span", "span", 70.0f},
    {"filePath", "filePath", 200.0f},
};
inline constexpr int k_signal_distance_column = 1;
inline constexpr int k_signal_signal_aspect_key_column = 3;
inline constexpr int k_signal_file_path_column = IM_ARRAYSIZE(k_signal_columns) - 1;

inline constexpr TableColumnDef k_beacon_columns[] = {
    {"rowNumber", "#", 40.0f},
    {"distance", "distance", 110.0f},
    {"type", "type", 70.0f},
    {"section", "section", 70.0f},
    {"sendData", "sendData", 90.0f},
    {"filePath", "filePath", 200.0f},
};
inline constexpr int k_beacon_distance_column = 1;
inline constexpr int k_beacon_file_path_column = IM_ARRAYSIZE(k_beacon_columns) - 1;

inline constexpr TableColumnDef k_irregularity_columns[] = {
    {"rowNumber", "#", 40.0f},
    {"distance", "distance", 110.0f},
    {"x", "x", 70.0f},
    {"y", "y", 70.0f},
    {"r", "r", 70.0f},
    {"lx", "lx", 70.0f},
    {"ly", "ly", 70.0f},
    {"lr", "lr", 70.0f},
    {"filePath", "filePath", 200.0f},
};
inline constexpr int k_irregularity_distance_column = 1;
inline constexpr int k_irregularity_file_path_column = IM_ARRAYSIZE(k_irregularity_columns) - 1;

inline constexpr TableColumnDef k_map_sound_columns[] = {
    {"rowNumber", "#", 40.0f},
    {"distance", "distance", 110.0f},
    {"soundKey", "soundKey", 120.0f},
    {"filePath", "filePath", 200.0f},
};
inline constexpr int k_map_sound_distance_column = 1;
inline constexpr int k_map_sound_key_column = 2;
inline constexpr int k_map_sound_file_path_column = IM_ARRAYSIZE(k_map_sound_columns) - 1;

inline constexpr TableColumnDef k_rolling_noise_columns[] = {
    {"rowNumber", "#", 40.0f},
    {"distance", "distance", 110.0f},
    {"index", "index", 70.0f},
    {"filePath", "filePath", 200.0f},
};
inline constexpr int k_rolling_noise_distance_column = 1;
inline constexpr int k_rolling_noise_file_path_column = IM_ARRAYSIZE(k_rolling_noise_columns) - 1;

inline constexpr TableColumnDef k_flange_noise_columns[] = {
    {"rowNumber", "#", 40.0f},
    {"distance", "distance", 110.0f},
    {"index", "index", 70.0f},
    {"filePath", "filePath", 200.0f},
};
inline constexpr int k_flange_noise_distance_column = 1;
inline constexpr int k_flange_noise_file_path_column = IM_ARRAYSIZE(k_flange_noise_columns) - 1;

inline constexpr TableColumnDef k_joint_noise_columns[] = {
    {"rowNumber", "#", 40.0f},
    {"distance", "distance", 110.0f},
    {"index", "index", 70.0f},
    {"filePath", "filePath", 200.0f},
};
inline constexpr int k_joint_noise_distance_column = 1;
inline constexpr int k_joint_noise_file_path_column = IM_ARRAYSIZE(k_joint_noise_columns) - 1;

inline constexpr TableColumnDef k_background_columns[] = {
    {"rowNumber", "#", 40.0f},
    {"distance", "distance", 110.0f},
    {"structureKey", "structureKey", 120.0f},
    {"filePath", "filePath", 200.0f},
};
inline constexpr int k_background_distance_column = 1;
inline constexpr int k_background_structure_key_column = 2;
inline constexpr int k_background_file_path_column = IM_ARRAYSIZE(k_background_columns) - 1;

inline constexpr TableColumnDef k_adhesion_columns[] = {
    {"rowNumber", "#", 40.0f},
    {"distance", "distance", 110.0f},
    {"a", "a", 70.0f},
    {"b", "b", 70.0f},
    {"c", "c", 70.0f},
    {"filePath", "filePath", 200.0f},
};
inline constexpr int k_adhesion_distance_column = 1;
inline constexpr int k_adhesion_file_path_column = IM_ARRAYSIZE(k_adhesion_columns) - 1;

inline constexpr TableColumnDef k_cab_illuminance_columns[] = {
    {"rowNumber", "#", 40.0f},
    {"distance", "distance", 110.0f},
    {"value", "value", 70.0f},
    {"filePath", "filePath", 200.0f},
};
inline constexpr int k_cab_illuminance_distance_column = 1;
inline constexpr int k_cab_illuminance_file_path_column = IM_ARRAYSIZE(k_cab_illuminance_columns) - 1;

inline constexpr TableColumnDef k_fog_columns[] = {
    {"rowNumber", "#", 40.0f},
    {"distance", "distance", 110.0f},
    {"density", "density", 80.0f},
    {"red", "red", 70.0f},
    {"green", "green", 70.0f},
    {"blue", "blue", 70.0f},
    {"filePath", "filePath", 200.0f},
};
inline constexpr int k_fog_distance_column = 1;
inline constexpr int k_fog_file_path_column = IM_ARRAYSIZE(k_fog_columns) - 1;

inline constexpr TableColumnDef k_legacy_fog_columns[] = {
    {"rowNumber", "#", 40.0f},
    {"distance", "distance", 110.0f},
    {"start", "start", 80.0f},
    {"end", "end", 80.0f},
    {"red", "red", 70.0f},
    {"green", "green", 70.0f},
    {"blue", "blue", 70.0f},
    {"filePath", "filePath", 200.0f},
};
inline constexpr int k_legacy_fog_distance_column = 1;
inline constexpr int k_legacy_fog_file_path_column = IM_ARRAYSIZE(k_legacy_fog_columns) - 1;

inline constexpr TableColumnDef k_draw_distance_columns[] = {
    {"rowNumber", "#", 40.0f},
    {"distance", "distance", 110.0f},
    {"value", "value", 70.0f},
    {"filePath", "filePath", 200.0f},
};
inline constexpr int k_draw_distance_distance_column = 1;
inline constexpr int k_draw_distance_file_path_column = IM_ARRAYSIZE(k_draw_distance_columns) - 1;

inline constexpr TableColumnDef k_speed_limit_columns[] = {
    {"rowNumber", "#", 40.0f},
    {"distance", "distance", 110.0f},
    {"method", "Begin/End", 80.0f},
    {"speed", "speed", 70.0f},
    {"filePath", "filePath", 200.0f},
};
inline constexpr int k_speed_limit_distance_column = 1;
inline constexpr int k_speed_limit_file_path_column = IM_ARRAYSIZE(k_speed_limit_columns) - 1;

inline constexpr TableColumnDef k_station_position_columns[] = {
    {"rowNumber", "#", 40.0f},
    {"dist", "dist", 70.0f},
    {"posKey", "key", 80.0f},
    {"door", "door", 55.0f},
    {"margin1", "back", 65.0f},
    {"margin2", "front", 65.0f},
};

inline constexpr TableColumnDef k_station_definition_columns[] = {
    {"stationKey", "stKey", 80.0f},
    {"stationName", "name", 120.0f},
    {"arrivalTime", "arr", 70.0f},
    {"depertureTime", "dep", 70.0f},
    {"stoppageTime", "stop", 60.0f},
    {"defaultTime", "def", 70.0f},
    {"signalFlag", "sig", 55.0f},
    {"alightingTime", "alight", 65.0f},
    {"passengers", "pax", 60.0f},
    {"arrivalSoundKey", "arrSnd", 85.0f},
    {"depertureSoundKey", "depSnd", 85.0f},
    {"doorReopen", "reopen", 70.0f},
    {"stuckInDoor", "stuck", 65.0f},
};
inline constexpr size_t k_station_definition_arrival_sound_column = 9;
inline constexpr size_t k_station_definition_departure_sound_column = 10;


} // namespace datatable_internal

