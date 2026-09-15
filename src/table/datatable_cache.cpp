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
void copy_table_row_metadata(const TableRow& source, CachedTableRow& dest) {
    dest.edit_id = source.edit_id;
    dest.source = source.source;
}

template <size_t N>
void append_station_table_rows(const std::vector<TableRow>& source_rows,
                               const TableColumnDef (&columns)[N],
                               std::vector<CachedTableRow>& cached_rows) {
    cached_rows.reserve(source_rows.size());
    for (const TableRow& row : source_rows) {
        CachedTableRow cached;
        copy_table_row_metadata(row, cached);
        cached.cells.resize(N);
        for (size_t i = 0; i < N; ++i) {
            cached.cells[i] = table_cell(row, columns[i].key);
        }
        cached_rows.push_back(std::move(cached));
    }
}

template <size_t N>
void append_change_point_rows(const std::vector<TableRow>& source_rows,
                              const TableColumnDef (&columns)[N],
                              int distance_column,
                              int file_path_column,
                              std::vector<CachedTableRow>& cached_rows,
                              float& distance_width,
                              float& file_path_width) {
    distance_width = 0.0f;
    expand_width_for_text(distance_width, columns[distance_column].header);
    cached_rows.reserve(source_rows.size());
    for (size_t row_index = 0; row_index < source_rows.size(); ++row_index) {
        const TableRow& row = source_rows[row_index];
        CachedTableRow cached;
        copy_table_row_metadata(row, cached);
        cached.cells.resize(N);
        cached.open_path = table_cell(row, "filePath");
        cached.tooltip_text = cached.open_path;
        for (size_t i = 0; i < N; ++i) {
            if (static_cast<int>(i) == file_path_column) {
                cached.cells[i] = display_name_from_path(cached.open_path);
                expand_width_for_text(file_path_width, cached.cells[i]);
            } else {
                cached.cells[i] = table_cell(row, columns[i].key);
            }
        }
        cached.cells[0] = std::to_string(row_index + 1);
        expand_width_for_text(distance_width, cached.cells[static_cast<size_t>(distance_column)]);
        cached_rows.push_back(std::move(cached));
    }
}

template <size_t N>
void append_structure_table_rows(const std::vector<TableRow>& source_rows,
                                 const TableColumnDef (&columns)[N],
                                 int file_path_column,
                                 std::vector<CachedTableRow>& cached_rows,
                                 float& file_path_width) {
    cached_rows.reserve(source_rows.size());
    for (const TableRow& row : source_rows) {
        CachedTableRow cached;
        copy_table_row_metadata(row, cached);
        cached.cells.resize(N);
        cached.invalid_track_key = is_invalid_track_key_row(row);
        for (size_t i = 0; i < N; ++i) {
            const std::string& value = table_cell(row, columns[i].key);
            if (static_cast<int>(i) == file_path_column) {
                cached.open_path = value;
                cached.tooltip_text = value;
                cached.cells[i] = display_name_from_path(value);
                expand_width_for_text(file_path_width, cached.cells[i]);
            } else {
                cached.cells[i] = value;
            }
        }
        cached_rows.push_back(std::move(cached));
    }
}

std::string format_distance_range(const std::string& start, const std::string& end) {
    return start + "~" + end;
}

std::string format_changed_distance(const std::string& start, int next_display_index) {
    return format_distance_range(start, "Changed to #" + std::to_string(next_display_index));
}

std::string format_repeater_file_path(const std::string& begin_path, const std::string& end_path = {}) {
    std::string begin_name = display_name_from_path(begin_path);
    std::string end_name = display_name_from_path(end_path);
    if (!end_path.empty() && !begin_path.empty() && end_path != begin_path) {
        return "Begin:" + begin_name + ", End:" + end_name;
    }
    return begin_path.empty() ? end_name : begin_name;
}

std::string format_repeater_file_path_tooltip(const std::string& begin_path, const std::string& end_path = {}) {
    if (!end_path.empty() && !begin_path.empty() && end_path != begin_path) {
        return "Begin:" + begin_path + ", End:" + end_path;
    }
    return begin_path.empty() ? end_path : begin_path;
}

std::vector<TableRow> merged_repeater_rows(const std::vector<TableRow>& data) {
    std::vector<TableRow> merged_rows;
    const repeater_linkage::Linkage linkage =
        repeater_linkage::pair_linkage(table_repeater_events(data));
    merged_rows.reserve(linkage.segments.size());
    for (const repeater_linkage::Segment& segment : linkage.segments) {
        if (segment.begin_source_index >= data.size()) continue;
        const TableRow& begin = data[segment.begin_source_index];
        TableRow row = begin;
        const std::string& begin_distance = table_cell(begin, "distance");
        const std::string& begin_file_path = table_cell(begin, "filePath");
        row.cells["rowNumber"] = std::to_string(segment.display_index);
        row.cells["_repeaterChainBeginIndex"] = std::to_string(segment.chain_begin_index);
        row.cells["_repeaterChainBeginCount"] = std::to_string(segment.chain_begin_count);
        row.cells["_openFilePath"] = begin_file_path;
        row.cells["_repeaterBoundaryKind"] =
            segment.boundary_kind == repeater_linkage::BoundaryKind::ExplicitEnd ? "end" :
            segment.boundary_kind == repeater_linkage::BoundaryKind::NextBegin ? "change" : "open";
        if (segment.boundary_kind == repeater_linkage::BoundaryKind::ExplicitEnd &&
            segment.boundary_source_index && *segment.boundary_source_index < data.size()) {
            const TableRow& end = data[*segment.boundary_source_index];
            const std::string& end_file_path = table_cell(end, "filePath");
            row.cells["_endEditId"] = end.edit_id;
            row.cells["_endDistance"] = table_cell(end, "distance");
            row.cells["distance"] = format_distance_range(begin_distance, table_cell(end, "distance"));
            row.cells["filePath"] = format_repeater_file_path(begin_file_path, end_file_path);
            row.cells["_openFilePath"] = begin_file_path.empty() ? end_file_path : begin_file_path;
            row.cells["_filePathTooltip"] = format_repeater_file_path_tooltip(begin_file_path, end_file_path);
        } else if (segment.boundary_kind == repeater_linkage::BoundaryKind::NextBegin) {
            row.cells["distance"] = format_changed_distance(
                begin_distance, static_cast<int>(segment.next_begin_display_index.value_or(0)));
            row.cells["filePath"] = format_repeater_file_path(begin_file_path);
            row.cells["_filePathTooltip"] = format_repeater_file_path_tooltip(begin_file_path);
        } else {
            row.cells["distance"] = format_distance_range(begin_distance, "NO END");
            row.cells["filePath"] = format_repeater_file_path(begin_file_path);
            row.cells["_filePathTooltip"] = format_repeater_file_path_tooltip(begin_file_path);
        }
        merged_rows.push_back(std::move(row));
    }
    return merged_rows;
}
} // namespace

void App::ensure_table_cache() {
    const float font_size = ImGui::GetFontSize();
    const float cell_padding_x = ImGui::GetStyle().CellPadding.x;
    if (table_cache_.valid &&
        std::abs(table_cache_.font_size - font_size) < 0.01f &&
        std::abs(table_cache_.cell_padding_x - cell_padding_x) < 0.01f) {
        return;
    }

    TableUiCache cache;
    cache.valid = true;
    cache.font_size = font_size;
    cache.cell_padding_x = cell_padding_x;

    append_station_table_rows(model_.station_list_rows, k_station_position_columns,
                              cache.station_position_rows);
    append_station_table_rows(model_.station_definition_rows, k_station_definition_columns,
                              cache.station_definition_rows);

    cache.structure_model_rows.reserve(model_.structure_models.size());
    for (size_t row_index = 0; row_index < model_.structure_models.size(); ++row_index) {
        const TableRow& row = model_.structure_models[row_index];
        CachedTableRow cached;
        copy_table_row_metadata(row, cached);
        cached.cells.resize(IM_ARRAYSIZE(k_structure_model_columns));
        cached.cells[0] = std::to_string(row_index + 1);
        cached.cells[1] = table_cell(row, "structureKey");
        cached.cells[2] = table_cell(row, "filePath");
        cached.open_path = table_cell(row, "resolvedFilePath");
        cached.tooltip_text = cached.open_path;
        expand_width_for_text(cache.structure_model_file_path_width, cached.cells[2]);
        cache.structure_model_rows.push_back(std::move(cached));
    }

    cache.other_train_distance_width = 0.0f;
    expand_width_for_text(cache.other_train_distance_width, k_other_train_columns[k_other_train_distance_column].header);
    std::vector<std::pair<std::string, std::string>> definition_train_keys;
    std::unordered_set<std::string> seen_definition_train_keys;
    cache.other_train_rows.reserve(model_.other_trains.size());
    for (size_t row_index = 0; row_index < model_.other_trains.size(); ++row_index) {
        const TableRow& row = model_.other_trains[row_index];
        CachedTableRow cached;
        copy_table_row_metadata(row, cached);
        cached.cells.resize(IM_ARRAYSIZE(k_other_train_columns));
        cached.cells[0] = std::to_string(row_index + 1);
        cached.cells[k_other_train_distance_column] = table_cell(row, "distance");
        cached.cells[2] = table_cell(row, "trainKey");
        std::string normalized_train_key = normalize_train_lookup_key(cached.cells[2]);
        if (seen_definition_train_keys.insert(normalized_train_key).second) {
            definition_train_keys.emplace_back(std::move(normalized_train_key), cached.cells[2]);
        }
        cached.cells[k_other_train_file_path_column] = table_cell(row, "filePath");
        cached.cells[4] = table_cell(row, "trackKey");
        cached.cells[5] = table_cell(row, "direction");
        cached.open_path = table_cell(row, "resolvedFilePath");
        cached.tooltip_text = cached.open_path;
        expand_width_for_text(cache.other_train_distance_width,
                              cached.cells[k_other_train_distance_column]);
        expand_width_for_text(cache.other_train_file_path_width,
                              cached.cells[k_other_train_file_path_column]);
        cache.other_train_rows.push_back(std::move(cached));
    }

    cache.other_train_stop_distance_width = 0.0f;
    cache.other_train_stop_file_path_width = 0.0f;
    expand_width_for_text(cache.other_train_stop_distance_width,
                          k_other_train_stop_columns[k_other_train_stop_distance_column].header);
    expand_width_for_text(cache.other_train_stop_file_path_width,
                          k_other_train_stop_columns[k_other_train_stop_file_path_column].header);
    std::vector<CachedOtherTrainStopGroup> stop_groups;
    std::unordered_map<std::string, size_t> stop_group_index_by_train_key;
    cache.other_train_stop_rows.reserve(model_.other_train_stops.size());
    std::unordered_map<std::string, std::string> enable_time_by_train_key;
    enable_time_by_train_key.reserve(model_.other_train_enables.size());
    for (const TableRow& row : model_.other_train_enables) {
        enable_time_by_train_key.emplace(
            normalize_train_lookup_key(table_cell(row, "trainKey")),
            table_cell(row, "time"));
    }
    for (size_t row_index = 0; row_index < model_.other_train_stops.size(); ++row_index) {
        const TableRow& row = model_.other_train_stops[row_index];
        CachedTableRow cached;
        copy_table_row_metadata(row, cached);
        cached.cells.resize(IM_ARRAYSIZE(k_other_train_stop_columns));
        cached.cells[k_other_train_stop_distance_column] = table_cell(row, "distance");
        cached.cells[2] = table_cell(row, "trainKey");
        std::string normalized_train_key = normalize_train_lookup_key(cached.cells[2]);
        auto group_it = stop_group_index_by_train_key.find(normalized_train_key);
        if (group_it == stop_group_index_by_train_key.end()) {
            const size_t group_index = stop_groups.size();
            CachedOtherTrainStopGroup group;
            group.train_key = cached.cells[2];
            const auto enable = enable_time_by_train_key.find(normalized_train_key);
            if (enable != enable_time_by_train_key.end() && !enable->second.empty()) {
                group.enable_time = enable->second;
            }
            stop_groups.push_back(std::move(group));
            group_it = stop_group_index_by_train_key.emplace(std::move(normalized_train_key), group_index).first;
        }
        cached.cells[0] = std::to_string(stop_groups[group_it->second].row_indices.size() + 1);
        stop_groups[group_it->second].row_indices.push_back(row_index);
        cached.cells[3] = table_cell(row, "decelerate");
        cached.cells[4] = table_cell(row, "stopTime");
        cached.cells[5] = table_cell(row, "accelerate");
        cached.cells[6] = table_cell(row, "speed");
        cached.open_path = table_cell(row, "filePath");
        cached.cells[k_other_train_stop_file_path_column] = display_name_from_path(cached.open_path);
        cached.tooltip_text = cached.open_path;
        expand_width_for_text(cache.other_train_stop_distance_width,
                              cached.cells[k_other_train_stop_distance_column]);
        expand_width_for_text(cache.other_train_stop_file_path_width,
                              cached.cells[k_other_train_stop_file_path_column]);
        cache.other_train_stop_rows.push_back(std::move(cached));
    }
    cache.other_train_stop_groups.reserve(stop_groups.size());
    std::unordered_set<std::string> appended_stop_group_keys;
    for (const auto& definition_key : definition_train_keys) {
        auto group_it = stop_group_index_by_train_key.find(definition_key.first);
        if (group_it == stop_group_index_by_train_key.end()) continue;
        CachedOtherTrainStopGroup group = stop_groups[group_it->second];
        group.train_key = definition_key.second;
        cache.other_train_stop_groups.push_back(std::move(group));
        appended_stop_group_keys.insert(definition_key.first);
    }
    for (auto& group : stop_groups) {
        std::string normalized_train_key = normalize_train_lookup_key(group.train_key);
        if (appended_stop_group_keys.find(normalized_train_key) != appended_stop_group_keys.end()) continue;
        cache.other_train_stop_groups.push_back(std::move(group));
    }

    auto append_section_rows = [&](const std::vector<TableRow>& rows,
                                   std::vector<CachedTableRow>& output,
                                   size_t& value_columns) {
        for (const TableRow& row : rows) {
            value_columns = std::max(
                value_columns,
                static_cast<size_t>(std::max(0.0, table_cell_number(row, "valueCount"))));
        }
        const size_t displayed_value_columns =
            std::min(value_columns, k_max_section_value_columns);
        output.reserve(rows.size());
        for (size_t row_index = 0; row_index < rows.size(); ++row_index) {
            const TableRow& row = rows[row_index];
            CachedTableRow cached;
            cached.cells.resize(3 + displayed_value_columns);
            cached.cells[0] = std::to_string(row_index + 1);
            cached.cells[1] = table_cell(row, "distance");
            for (size_t value_index = 0; value_index < displayed_value_columns; ++value_index) {
                cached.cells[2 + value_index] =
                    table_cell(row, "value" + std::to_string(value_index));
            }
            cached.open_path = table_cell(row, "filePath");
            cached.cells[2 + displayed_value_columns] = display_name_from_path(cached.open_path);
            cached.tooltip_text = cached.open_path;
            output.push_back(std::move(cached));
        }
    };
    append_section_rows(model_.section_begins, cache.section_begin_rows,
                        cache.section_begin_value_columns);
    append_section_rows(model_.section_speed_limits,
                        cache.section_speed_limit_rows,
                        cache.section_speed_limit_value_columns);

    std::vector<const TableRow*> variable_rows;
    variable_rows.reserve(model_.variable_assignments.size());
    for (const TableRow& row : model_.variable_assignments) {
        variable_rows.push_back(&row);
    }
    std::stable_sort(variable_rows.begin(), variable_rows.end(),
                     [](const TableRow* left, const TableRow* right) {
                         return table_cell_number(*left, "order") <
                                table_cell_number(*right, "order");
                     });
    std::vector<std::string> variable_group_order;
    std::unordered_map<std::string, std::vector<const TableRow*>> variable_groups;
    std::unordered_map<std::string, std::string> variable_group_names;
    for (const TableRow* row : variable_rows) {
        const std::string normalized = table_cell(*row, "normalizedName");
        auto inserted = variable_groups.emplace(
            normalized, std::vector<const TableRow*>{});
        if (inserted.second) {
            variable_group_order.push_back(normalized);
            variable_group_names.emplace(normalized, table_cell(*row, "sourceName"));
        }
        inserted.first->second.push_back(row);
    }
    cache.variable_rows.reserve(variable_rows.size() + variable_group_order.size());
    for (const std::string& group_key : variable_group_order) {
        CachedVariableRow heading;
        heading.group_header = true;
        heading.name = variable_group_names[group_key];
        cache.variable_rows.push_back(std::move(heading));
        for (const TableRow* row : variable_groups[group_key]) {
            CachedVariableRow cached;
            cached.value = table_cell(*row, "value");
            cached.expression = table_cell(*row, "expression");
            cached.file_path = table_cell(*row, "filePath");
            cache.variable_rows.push_back(std::move(cached));
        }
    }

    cache.sound_list_buffer_count_width = 0.0f;
    cache.sound_3d_list_buffer_count_width = 0.0f;
    expand_width_for_text(cache.sound_list_buffer_count_width, k_sound_list_columns[k_sound_list_buffer_count_column].header);
    expand_width_for_text(cache.sound_3d_list_buffer_count_width, k_sound_list_columns[k_sound_list_buffer_count_column].header);
    cache.sound_list_rows.reserve(model_.sound_list.size());
    cache.sound_3d_list_rows.reserve(model_.sound_3d_list.size());
    auto append_sound_list_row = [&](const TableRow& row,
                                     std::vector<CachedTableRow>& rows,
                                     float& file_path_width,
                                     float& buffer_count_width) {
        CachedTableRow cached;
        copy_table_row_metadata(row, cached);
        cached.cells.resize(IM_ARRAYSIZE(k_sound_list_columns));
        cached.cells[0] = std::to_string(rows.size() + 1);
        cached.cells[1] = table_cell(row, "soundKey");
        cached.cells[2] = table_cell(row, "filePath");
        cached.open_path = table_cell(row, "resolvedFilePath");
        cached.tooltip_text = cached.open_path;
        cached.cells[3] = table_cell(row, "bufferCount");
        expand_width_for_text(file_path_width, cached.cells[2]);
        expand_width_for_text(buffer_count_width, cached.cells[3]);
        rows.push_back(std::move(cached));
    };
    for (const TableRow& row : model_.sound_list) {
        append_sound_list_row(row, cache.sound_list_rows,
                              cache.sound_list_file_path_width,
                              cache.sound_list_buffer_count_width);
    }
    for (const TableRow& row : model_.sound_3d_list) {
        append_sound_list_row(row, cache.sound_3d_list_rows,
                              cache.sound_3d_list_file_path_width,
                              cache.sound_3d_list_buffer_count_width);
    }

    append_structure_table_rows(model_.structures,
                                k_structure_put_columns,
                                k_structure_put_file_path_column,
                                cache.structure_rows,
                                cache.structure_file_path_width);
    append_structure_table_rows(model_.structures_between,
                                k_structure_between_columns,
                                k_structure_between_file_path_column,
                                cache.structure_between_rows,
                                cache.structure_between_file_path_width);

    cache.repeater_interval_width = 0.0f;
    expand_width_for_text(cache.repeater_interval_width, k_repeater_columns[k_repeater_interval_column].header);
    std::vector<TableRow> repeater_rows = merged_repeater_rows(model_.repeaters);
    cache.repeater_rows.reserve(repeater_rows.size());
    for (const auto& row : repeater_rows) {
        CachedTableRow cached;
        copy_table_row_metadata(row, cached);
        cached.cells.resize(IM_ARRAYSIZE(k_repeater_columns));
        cached.invalid_track_key = is_invalid_track_key_row(row);
        cached.repeater_chain_begin_index = static_cast<size_t>(std::max(
            0.0, table_cell_number(row, "_repeaterChainBeginIndex")));
        cached.repeater_chain_begin_count = std::max<size_t>(1, static_cast<size_t>(std::max(
            0.0, table_cell_number(row, "_repeaterChainBeginCount"))));
        cached.open_path = table_cell(row, "_openFilePath");
        cached.tooltip_text = table_cell(row, "_filePathTooltip");
        if (cached.tooltip_text.empty()) cached.tooltip_text = cached.open_path;
        for (int i = 0; i < IM_ARRAYSIZE(k_repeater_columns); ++i) {
            cached.cells[i] = table_cell(row, k_repeater_columns[i].key);
        }
        expand_width_for_text(cache.repeater_distance_width, cached.cells[k_repeater_distance_column]);
        expand_width_for_text(cache.repeater_file_path_width, cached.cells[k_repeater_file_path_column]);
        cache.repeater_rows.push_back(std::move(cached));
    }

    cache.signal_aspect_structure_key_columns = 0;
    for (const TableRow& row : model_.signal_aspects) {
        const size_t main_structure_key_count =
            static_cast<size_t>(table_cell_number(
                row, "_signalMainStructureKeyCount"));
        const size_t glare_structure_key_count =
            static_cast<size_t>(table_cell_number(
                row, "_signalGlareStructureKeyCount"));
        cache.signal_aspect_structure_key_columns =
            std::max(
                cache.signal_aspect_structure_key_columns,
                std::max(main_structure_key_count,
                         glare_structure_key_count));
    }
    cache.signal_aspect_structure_key_columns =
        std::min(cache.signal_aspect_structure_key_columns, k_max_signal_aspect_structure_key_columns);
    cache.signal_aspect_column_headers.reserve(
        k_signal_aspect_structure_key_column_offset +
        cache.signal_aspect_structure_key_columns);
    cache.signal_aspect_column_widths.reserve(
        k_signal_aspect_structure_key_column_offset +
        cache.signal_aspect_structure_key_columns);
    for (const TableColumnDef& column :
         k_signal_aspect_fixed_columns) {
        cache.signal_aspect_column_headers.emplace_back(column.header);
        cache.signal_aspect_column_widths.push_back(column.width);
    }
    for (size_t key_index = 0;
         key_index < cache.signal_aspect_structure_key_columns;
         ++key_index) {
        cache.signal_aspect_column_headers.push_back(
            "structureKey" + std::to_string(key_index + 1));
        cache.signal_aspect_column_widths.push_back(120.0f);
    }
    cache.signal_aspect_rows.reserve(model_.signal_aspects.size());
    cache.signal_aspect_display_rows.reserve(
        model_.signal_aspects.size() * 2);
    for (size_t row_index = 0; row_index < model_.signal_aspects.size(); ++row_index) {
        const TableRow& row = model_.signal_aspects[row_index];
        const size_t structure_key_count = static_cast<size_t>(
            table_cell_number(row, "_structureKeyCount"));
        const size_t main_structure_key_count =
            static_cast<size_t>(table_cell_number(
                row, "_signalMainStructureKeyCount"));
        const size_t glare_structure_key_count =
            static_cast<size_t>(table_cell_number(
                row, "_signalGlareStructureKeyCount"));
        CachedTableRow cached;
        copy_table_row_metadata(row, cached);
        cached.editable_field_count = 1 + structure_key_count;
        cached.primary_structure_field_count =
            main_structure_key_count;
        cached.secondary_structure_field_count =
            glare_structure_key_count;
        cached.cells.resize(
            k_signal_aspect_structure_key_column_offset +
            std::max(structure_key_count,
                     cache.signal_aspect_structure_key_columns));
        cached.cells[0] = std::to_string(row_index + 1);
        cached.cells[1] = table_cell(row, "signalAspectKey");
        for (size_t key_index = 0;
             key_index < structure_key_count; ++key_index) {
            std::string column_key = "structureKey" + std::to_string(key_index + 1);
            std::string value = table_cell(row, column_key);
            cached.cells[k_signal_aspect_structure_key_column_offset + key_index] = value;
            const size_t display_key_index =
                key_index < main_structure_key_count
                ? key_index
                : key_index - main_structure_key_count;
            if (display_key_index <
                cache.signal_aspect_structure_key_columns) {
                expand_width_for_text(
                    cache.signal_aspect_column_widths[
                        k_signal_aspect_structure_key_column_offset +
                        display_key_index],
                    value);
            }
        }
        cache.signal_aspect_rows.push_back(std::move(cached));
        const std::string sequence =
            std::to_string(row_index + 1);
        cache.signal_aspect_display_rows.push_back(
            EditableListDisplayRow{
                row_index, 1,
                main_structure_key_count,
                sequence, false});
        if (glare_structure_key_count != 0) {
            cache.signal_aspect_display_rows.push_back(
                EditableListDisplayRow{
                    row_index,
                    1 + main_structure_key_count,
                    glare_structure_key_count,
                    sequence + "F", true});
        }
    }

    append_change_point_rows(model_.signals, k_signal_columns,
                             k_signal_distance_column, k_signal_file_path_column,
                             cache.signal_rows,
                             cache.signal_distance_width, cache.signal_file_path_width);
    append_change_point_rows(model_.beacons, k_beacon_columns,
                             k_beacon_distance_column, k_beacon_file_path_column,
                             cache.beacon_rows,
                             cache.beacon_distance_width, cache.beacon_file_path_width);

    cache.irregularity_distance_width = 0.0f;
    expand_width_for_text(cache.irregularity_distance_width, k_irregularity_columns[k_irregularity_distance_column].header);
    cache.irregularity_rows.reserve(model_.irregularities.size());
    for (size_t row_index = 0; row_index < model_.irregularities.size(); ++row_index) {
        const TableRow& row = model_.irregularities[row_index];
        CachedTableRow cached;
        copy_table_row_metadata(row, cached);
        cached.cells.resize(IM_ARRAYSIZE(k_irregularity_columns));
        cached.open_path = table_cell(row, "filePath");
        for (int i = 0; i < IM_ARRAYSIZE(k_irregularity_columns); ++i) {
            if (i == k_irregularity_file_path_column) {
                cached.cells[i] = display_name_from_path(cached.open_path);
                expand_width_for_text(cache.irregularity_file_path_width, cached.cells[i]);
            } else {
                cached.cells[i] = table_cell(row, k_irregularity_columns[i].key);
            }
        }
        cached.cells[0] = std::to_string(row_index + 1);
        expand_width_for_text(cache.irregularity_distance_width, cached.cells[k_irregularity_distance_column]);
        cache.irregularity_rows.push_back(std::move(cached));
    }

    append_change_point_rows(model_.rolling_noises, k_rolling_noise_columns,
                             k_rolling_noise_distance_column, k_rolling_noise_file_path_column,
                             cache.rolling_noise_rows,
                             cache.rolling_noise_distance_width,
                             cache.rolling_noise_file_path_width);
    append_change_point_rows(model_.flange_noises, k_flange_noise_columns,
                             k_flange_noise_distance_column, k_flange_noise_file_path_column,
                             cache.flange_noise_rows,
                             cache.flange_noise_distance_width,
                             cache.flange_noise_file_path_width);
    append_change_point_rows(model_.joint_noises, k_joint_noise_columns,
                             k_joint_noise_distance_column, k_joint_noise_file_path_column,
                             cache.joint_noise_rows,
                             cache.joint_noise_distance_width,
                             cache.joint_noise_file_path_width);

    append_change_point_rows(model_.map_sounds, k_map_sound_columns,
                             k_map_sound_distance_column, k_map_sound_file_path_column,
                             cache.map_sound_rows,
                             cache.map_sound_distance_width,
                             cache.map_sound_file_path_width);
    append_change_point_rows(model_.map_sound_3d, k_map_sound_columns,
                             k_map_sound_distance_column, k_map_sound_file_path_column,
                             cache.map_sound_3d_rows,
                             cache.map_sound_3d_distance_width,
                             cache.map_sound_3d_file_path_width);
    append_change_point_rows(model_.draw_distances, k_draw_distance_columns,
                             k_draw_distance_distance_column, k_draw_distance_file_path_column,
                             cache.draw_distance_rows,
                             cache.draw_distance_distance_width,
                             cache.draw_distance_file_path_width);
    append_change_point_rows(model_.speed_limit_rows, k_speed_limit_columns,
                             k_speed_limit_distance_column, k_speed_limit_file_path_column,
                             cache.speed_limit_rows,
                             cache.speed_limit_distance_width,
                             cache.speed_limit_file_path_width);

    append_change_point_rows(model_.backgrounds, k_background_columns,
                             k_background_distance_column, k_background_file_path_column,
                             cache.background_rows,
                             cache.background_distance_width,
                             cache.background_file_path_width);
    append_change_point_rows(model_.adhesions, k_adhesion_columns,
                             k_adhesion_distance_column, k_adhesion_file_path_column,
                             cache.adhesion_rows,
                             cache.adhesion_distance_width,
                             cache.adhesion_file_path_width);
    append_change_point_rows(model_.cab_illuminance, k_cab_illuminance_columns,
                             k_cab_illuminance_distance_column,
                             k_cab_illuminance_file_path_column,
                             cache.cab_illuminance_rows,
                             cache.cab_illuminance_distance_width,
                             cache.cab_illuminance_file_path_width);
    append_change_point_rows(model_.fogs, k_fog_columns,
                             k_fog_distance_column, k_fog_file_path_column,
                             cache.fog_rows,
                             cache.fog_distance_width,
                             cache.fog_file_path_width);
    append_change_point_rows(model_.legacy_fogs, k_legacy_fog_columns,
                             k_legacy_fog_distance_column, k_legacy_fog_file_path_column,
                             cache.legacy_fog_rows,
                             cache.legacy_fog_distance_width,
                             cache.legacy_fog_file_path_width);

    table_cache_ = std::move(cache);
}

void App::refresh_speed_limit_table_cache() {
    if (!table_cache_.valid) return;
    table_cache_.speed_limit_rows.clear();
    table_cache_.speed_limit_distance_width = 110.0f;
    table_cache_.speed_limit_file_path_width = 200.0f;
    append_change_point_rows(model_.speed_limit_rows, k_speed_limit_columns,
                             k_speed_limit_distance_column,
                             k_speed_limit_file_path_column,
                             table_cache_.speed_limit_rows,
                             table_cache_.speed_limit_distance_width,
                             table_cache_.speed_limit_file_path_width);
    speed_limit_list_scroll_row_ = -1;
    speed_limit_list_highlight_row_ = -1;
    plan_context_menu_entries_.clear();
}

