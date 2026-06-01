/*
 * Copyright (c) 2026 Sapporo_ningyo
 *
 * Licensed under Apache License 2.0; see LICENSE and NOTICE.
 * The GUI uses Dear ImGui and ImPlot; see THIRD_PARTY_NOTICES.md.
 */

#pragma execution_character_set("utf-8")

#include "kme.h"

#include "canvas3D.h"
#include "imgui.h"

#include <windows.h>
#include <shellapi.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <string>
#include <utility>
#include <vector>


namespace {

bool blank_ascii(const std::string& text) {
    return text.find_first_not_of(" \t\r\n") == std::string::npos;
}

void open_parent_directory_in_explorer(const std::string& file_path) {
    if (blank_ascii(file_path)) return;
    try {
        std::filesystem::path path = utf8_to_wide(file_path);
        std::error_code ec;
        std::filesystem::path abs = std::filesystem::absolute(path, ec);
        if (!ec) path = abs;
        std::filesystem::path dir = path.parent_path();
        if (dir.empty()) return;
        ShellExecuteW(nullptr, L"open", dir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    } catch (...) {
    }
}

void render_file_path_cell_with_context(const std::string& display_text, const std::string& open_path,
                                        const std::string& menu_label, const std::string& tooltip_text = {},
                                        ImU32 text_color = 0) {
    if (display_text.empty()) return;

    if (text_color == 0) text_color = ImGui::GetColorU32(ImGuiCol_Text);
    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImVec2 text_size = ImGui::CalcTextSize(display_text.c_str());
    ImVec2 item_size(
        std::max(1.0f, ImGui::GetContentRegionAvail().x),
        std::max(ImGui::GetTextLineHeight(), text_size.y));
    ImGui::InvisibleButton("file_path_cell", item_size);
    if (ImGui::IsItemHovered()) {
        ImGui::GetWindowDrawList()->AddRectFilled(pos, ImVec2(pos.x + item_size.x, pos.y + item_size.y), ImGui::GetColorU32(ImGuiCol_HeaderHovered));
        if (!tooltip_text.empty()) ImGui::SetTooltip("%s", tooltip_text.c_str());
    }
    ImGui::GetWindowDrawList()->AddText(pos, text_color, display_text.c_str());

    if (ImGui::BeginPopupContextItem("file_path_context", ImGuiPopupFlags_MouseButtonRight)) {
        bool can_open = !blank_ascii(open_path);
        ImGui::BeginDisabled(!can_open);
        if (ImGui::MenuItem(menu_label.c_str())) {
            open_parent_directory_in_explorer(open_path);
        }
        ImGui::EndDisabled();
        ImGui::EndPopup();
    }
}

bool render_text_cell_with_context(const std::string& display_text, const std::string& menu_label, bool menu_enabled) {
    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImVec2 text_size = ImGui::CalcTextSize(display_text.c_str());
    ImVec2 item_size(
        std::max(1.0f, ImGui::GetContentRegionAvail().x),
        std::max(ImGui::GetTextLineHeight(), text_size.y));
    ImGui::InvisibleButton("text_cell_context_item", item_size);
    if (ImGui::IsItemHovered()) {
        ImGui::GetWindowDrawList()->AddRectFilled(pos, ImVec2(pos.x + item_size.x, pos.y + item_size.y),
                                                  ImGui::GetColorU32(ImGuiCol_HeaderHovered));
    }
    if (!display_text.empty()) {
        ImGui::GetWindowDrawList()->AddText(pos, ImGui::GetColorU32(ImGuiCol_Text), display_text.c_str());
    }

    bool selected = false;
    if (ImGui::BeginPopupContextItem("text_cell_context", ImGuiPopupFlags_MouseButtonRight)) {
        ImGui::BeginDisabled(!menu_enabled);
        selected = ImGui::MenuItem(menu_label.c_str());
        ImGui::EndDisabled();
        ImGui::EndPopup();
    }
    return selected;
}

void expand_width_for_text(float& width, const std::string& text) {
    if (text.empty()) return;
    float text_width = ImGui::CalcTextSize(text.c_str()).x + ImGui::GetStyle().CellPadding.x * 2.0f + 12.0f;
    width = std::max(width, text_width);
}

bool all_flags_set(const std::vector<unsigned char>& flags) {
    return !flags.empty() && std::all_of(flags.begin(), flags.end(), [](unsigned char value) { return value != 0; });
}

void set_all_flags(std::vector<unsigned char>& flags, bool value) {
    std::fill(flags.begin(), flags.end(), value ? 1 : 0);
}

void setup_fixed_table_header() {
    ImGui::TableSetupScrollFreeze(0, 1);
}

} // namespace

constexpr float kShowColumnWidth = 56.0f;

static const TableColumnDef kStructureModelColumns[] = {
    {"rowNumber", "#", 40.0f},
    {"structureKey", "structureKey", 120.0f},
    {"filePath", "filePath", 200.0f},
};
constexpr int kStructureModelFilePathColumn = IM_ARRAYSIZE(kStructureModelColumns) - 1;

static const char* kStructureColumns[] = {
    "distance", "method", "structureKey", "trackKey", "x", "y", "z", "rx", "ry", "rz",
    "tilt", "span", "trackKey1", "trackKey2", "flag", "filePath"
};
constexpr int kStructureDistanceColumn = 0;
constexpr int kStructureFilePathColumn = IM_ARRAYSIZE(kStructureColumns) - 1;

static const TableColumnDef kRepeaterColumns[] = {
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
constexpr int kRepeaterDistanceColumn = 1;
constexpr int kRepeaterIntervalColumn = 13;
constexpr int kRepeaterFilePathColumn = IM_ARRAYSIZE(kRepeaterColumns) - 1;

static const TableColumnDef kStationListColumns[] = {
    {"rowNumber", "#", 40.0f},
    {"dist", "dist", 70.0f},
    {"posKey", "key", 80.0f},
    {"door", "door", 55.0f},
    {"margin1", "back", 65.0f},
    {"margin2", "front", 65.0f},
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

std::string display_name_from_path(const std::string& path);

const std::string& table_cell(const TableRow& row, const std::string& key) {
    static const std::string empty;
    auto it = row.cells.find(key);
    return it == row.cells.end() ? empty : it->second;
}

double table_cell_number(const TableRow& row, const std::string& key) {
    const std::string& text = table_cell(row, key);
    if (text.empty()) return 0.0;
    char* end = nullptr;
    double value = std::strtod(text.c_str(), &end);
    return end == text.c_str() ? 0.0 : value;
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

std::vector<TableRow> merged_repeater_rows(const std::vector<TableRow>& data) {
    std::vector<TableRow> ordered_rows = data;
    std::stable_sort(ordered_rows.begin(), ordered_rows.end(), [](const TableRow& a, const TableRow& b) {
        return table_cell_number(a, "order") < table_cell_number(b, "order");
    });

    std::vector<TableRow> merged_rows;
    std::map<std::string, size_t> open_rows;
    int display_index = 1;

    for (const auto& row : ordered_rows) {
        const std::string& key = table_cell(row, "repeaterKey");
        const std::string& method = table_cell(row, "method");

        if (method == "Begin" || method == "Begin0") {
            auto open_it = open_rows.find(key);
            if (open_it != open_rows.end()) {
                TableRow& previous = merged_rows[open_it->second];
                previous.cells["distance"] = format_changed_distance(table_cell(previous, "_beginDistance"), display_index);
                previous.cells["filePath"] = format_repeater_file_path(table_cell(previous, "_beginFilePath"));
                previous.cells["_openFilePath"] = table_cell(previous, "_beginFilePath");
                open_rows.erase(open_it);
            }

            TableRow new_row = row;
            const std::string& begin_distance = table_cell(row, "distance");
            const std::string& begin_file_path = table_cell(row, "filePath");
            new_row.cells["rowNumber"] = std::to_string(display_index);
            new_row.cells["_beginDistance"] = begin_distance;
            new_row.cells["_beginFilePath"] = begin_file_path;
            new_row.cells["_openFilePath"] = begin_file_path;
            new_row.cells["distance"] = format_distance_range(begin_distance, "NO END");
            new_row.cells["filePath"] = format_repeater_file_path(begin_file_path);
            open_rows[key] = merged_rows.size();
            merged_rows.push_back(std::move(new_row));
            ++display_index;
        } else if (method == "End") {
            auto open_it = open_rows.find(key);
            if (open_it != open_rows.end()) {
                TableRow& begin_row = merged_rows[open_it->second];
                const std::string& begin_file_path = table_cell(begin_row, "_beginFilePath");
                const std::string& end_file_path = table_cell(row, "filePath");
                begin_row.cells["distance"] = format_distance_range(table_cell(begin_row, "_beginDistance"), table_cell(row, "distance"));
                begin_row.cells["filePath"] = format_repeater_file_path(begin_file_path, end_file_path);
                begin_row.cells["_openFilePath"] = begin_file_path.empty() ? end_file_path : begin_file_path;
                open_rows.erase(open_it);
            }
        }
    }

    for (auto& row : merged_rows) {
        row.cells.erase("_beginDistance");
        row.cells.erase("_beginFilePath");
    }
    return merged_rows;
}

void App::invalidate_table_cache() {
    table_cache_ = TableUiCache{};
}

void App::reset_marker_visibility() {
    structure_row_visible_.assign(structure_marker_cache_.size(), 0);
    repeater_row_visible_.assign(repeater_marker_cache_.size(), 0);
}

void App::sync_marker_visibility_sizes() {
    structure_row_visible_.resize(structure_marker_cache_.size(), 0);
    repeater_row_visible_.resize(repeater_marker_cache_.size(), 0);
}

void App::locate_structure_row_on_plan(size_t row_index) {
    sync_marker_visibility_sizes();
    if (row_index >= structure_marker_cache_.size() || !structure_marker_cache_[row_index]) return;
    if (row_index < structure_row_visible_.size()) structure_row_visible_[row_index] = 1;
    const PlanStructureMarker& marker = *structure_marker_cache_[row_index];
    focus_plan_at_model_point(marker.x, marker.y);
}

void App::locate_repeater_row_on_plan(size_t row_index) {
    sync_marker_visibility_sizes();
    if (row_index >= repeater_marker_cache_.size() || !repeater_marker_cache_[row_index].begin_marker) return;
    if (row_index < repeater_row_visible_.size()) repeater_row_visible_[row_index] = 1;
    const PlanRepeaterMarker& marker = *repeater_marker_cache_[row_index].begin_marker;
    focus_plan_at_model_point(marker.x, marker.y);
}

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

    cache.station_rows.reserve(model_.station_list_rows.size());
    for (const auto& row : model_.station_list_rows) {
        CachedTableRow cached;
        cached.cells.resize(IM_ARRAYSIZE(kStationListColumns));
        for (int i = 0; i < IM_ARRAYSIZE(kStationListColumns); ++i) {
            cached.cells[i] = table_cell(row, kStationListColumns[i].key);
        }
        cache.station_rows.push_back(std::move(cached));
    }

    cache.structure_model_rows.reserve(model_.structure_models.size());
    for (size_t row_index = 0; row_index < model_.structure_models.size(); ++row_index) {
        const TableRow& row = model_.structure_models[row_index];
        CachedTableRow cached;
        cached.cells.resize(IM_ARRAYSIZE(kStructureModelColumns));
        cached.cells[0] = std::to_string(row_index + 1);
        cached.cells[1] = table_cell(row, "structureKey");
        cached.open_path = table_cell(row, "filePath");
        cached.cells[2] = display_name_from_path(cached.open_path);
        expand_width_for_text(cache.structure_model_file_path_width, cached.cells[2]);
        cache.structure_model_rows.push_back(std::move(cached));
    }

    auto append_structure_rows = [&](const std::vector<TableRow>& rows) {
        cache.structure_rows.reserve(cache.structure_rows.size() + rows.size());
        for (const auto& row : rows) {
            CachedTableRow cached;
            cached.cells.resize(IM_ARRAYSIZE(kStructureColumns));
            for (int i = 0; i < IM_ARRAYSIZE(kStructureColumns); ++i) {
                const std::string& value = table_cell(row, kStructureColumns[i]);
                if (i == kStructureFilePathColumn) {
                    cached.open_path = value;
                    cached.cells[i] = display_name_from_path(value);
                    expand_width_for_text(cache.structure_file_path_width, cached.cells[i]);
                } else {
                    cached.cells[i] = value;
                }
            }
            cache.structure_rows.push_back(std::move(cached));
        }
    };
    append_structure_rows(model_.structures);
    append_structure_rows(model_.structures_between);

    cache.repeater_interval_width = 0.0f;
    expand_width_for_text(cache.repeater_interval_width, kRepeaterColumns[kRepeaterIntervalColumn].header);
    std::vector<TableRow> repeater_rows = merged_repeater_rows(model_.repeaters);
    cache.repeater_rows.reserve(repeater_rows.size());
    for (const auto& row : repeater_rows) {
        CachedTableRow cached;
        cached.cells.resize(IM_ARRAYSIZE(kRepeaterColumns));
        cached.open_path = table_cell(row, "_openFilePath");
        for (int i = 0; i < IM_ARRAYSIZE(kRepeaterColumns); ++i) {
            cached.cells[i] = table_cell(row, kRepeaterColumns[i].key);
        }
        expand_width_for_text(cache.repeater_distance_width, cached.cells[kRepeaterDistanceColumn]);
        expand_width_for_text(cache.repeater_file_path_width, cached.cells[kRepeaterFilePathColumn]);
        cache.repeater_rows.push_back(std::move(cached));
    }

    table_cache_ = std::move(cache);
}

void App::render_othertracks_window() {
    if (!show_othertracks_window_) return;
    if (dock_right_id_) ImGui::SetNextWindowDockID(dock_right_id_, ImGuiCond_FirstUseEver);
    std::string title = tr("frame.othertracks") + "###OtherTracks";
    if (ImGui::Begin(title.c_str(), &show_othertracks_window_)) {
        if (!has_model_) {
            ImGui::TextDisabled("-");
        } else if (ImGui::BeginTable("othertracks", 5, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY)) {
            ImGui::TableSetupColumn("Show");
            ImGui::TableSetupColumn("Key");
            ImGui::TableSetupColumn("From");
            ImGui::TableSetupColumn("To");
            ImGui::TableSetupColumn("Color");
            setup_fixed_table_header();
            ImGui::TableHeadersRow();
            for (auto& t : model_.other_tracks) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::PushID(t.key.c_str());
                ImGui::Checkbox("##show", &t.visible);
                ImGui::TableSetColumnIndex(1);
                ImGui::TextUnformatted(t.key.empty() ? "\\" : t.key.c_str());
                ImGui::TableSetColumnIndex(2);
                ImGui::SetNextItemWidth(-1);
                ImGui::InputDouble("##min", &t.range_min, 0, 0, "%.1f");
                ImGui::TableSetColumnIndex(3);
                ImGui::SetNextItemWidth(-1);
                ImGui::InputDouble("##max", &t.range_max, 0, 0, "%.1f");
                ImGui::TableSetColumnIndex(4);
                ImGui::ColorEdit3("##color", &t.color.x, ImGuiColorEditFlags_NoInputs);
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
    }
    ImGui::End();
}

void App::render_station_list_window() {
    if (!show_station_list_window_) return;
    if (dock_right_id_) ImGui::SetNextWindowDockID(dock_right_id_, ImGuiCond_FirstUseEver);
    std::string title = tr("frame.station_list") + "###StationList";
    if (!ImGui::Begin(title.c_str(), &show_station_list_window_)) {
        ImGui::End();
        return;
    }
    if (!has_model_) {
        ImGui::TextDisabled("-");
        ImGui::End();
        return;
    }
    ensure_table_cache();
    if (ImGui::BeginTable("station_list", IM_ARRAYSIZE(kStationListColumns), ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollX | ImGuiTableFlags_ScrollY)) {
        for (int i = 0; i < IM_ARRAYSIZE(kStationListColumns); ++i) {
            ImGui::TableSetupColumn(kStationListColumns[i].header, ImGuiTableColumnFlags_WidthFixed, kStationListColumns[i].width);
        }
        setup_fixed_table_header();
        ImGui::TableHeadersRow();
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(table_cache_.station_rows.size()));
        while (clipper.Step()) {
            for (int row_index = clipper.DisplayStart; row_index < clipper.DisplayEnd; ++row_index) {
                const CachedTableRow& row = table_cache_.station_rows[static_cast<size_t>(row_index)];
                ImGui::TableNextRow();
                for (int i = 0; i < IM_ARRAYSIZE(kStationListColumns); ++i) {
                    ImGui::TableSetColumnIndex(i);
                    const std::string& value = row.cells[static_cast<size_t>(i)];
                    if (!value.empty()) ImGui::TextUnformatted(value.c_str());
                }
            }
        }
        ImGui::EndTable();
    }
    ImGui::End();
}

void App::render_structures_window() {
    if (!show_structures_window_) return;
    if (dock_right_id_) ImGui::SetNextWindowDockID(dock_right_id_, ImGuiCond_FirstUseEver);
    std::string title = tr("frame.structures") + "###Structures";
    if (!ImGui::Begin(title.c_str(), &show_structures_window_)) {
        ImGui::End();
        return;
    }
    sync_marker_visibility_sizes();
    bool all_visible = all_flags_set(structure_row_visible_);
    ImGui::BeginDisabled(structure_row_visible_.empty());
    if (ImGui::Checkbox(tr("chk.select_all").c_str(), &all_visible)) {
        set_all_flags(structure_row_visible_, all_visible);
    }
    ImGui::EndDisabled();
    ensure_table_cache();
    if (ImGui::BeginTable("structures", IM_ARRAYSIZE(kStructureColumns) + 1, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollX | ImGuiTableFlags_ScrollY)) {
        ImGui::TableSetupColumn(tr("column.show").c_str(), ImGuiTableColumnFlags_WidthFixed, kShowColumnWidth);
        for (int i = 0; i < IM_ARRAYSIZE(kStructureColumns); ++i) {
            ImGui::TableSetupColumn(kStructureColumns[i],
                                    i == kStructureFilePathColumn ? ImGuiTableColumnFlags_WidthFixed : 0,
                                    i == kStructureFilePathColumn ? table_cache_.structure_file_path_width : 0.0f);
        }
        setup_fixed_table_header();
        ImGui::TableHeadersRow();
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(table_cache_.structure_rows.size()));
        while (clipper.Step()) {
            for (int row_index = clipper.DisplayStart; row_index < clipper.DisplayEnd; ++row_index) {
                const CachedTableRow& row = table_cache_.structure_rows[static_cast<size_t>(row_index)];
                ImGui::TableNextRow();
                ImGui::PushID(row_index);
                ImGui::TableSetColumnIndex(0);
                bool row_visible = static_cast<size_t>(row_index) < structure_row_visible_.size() &&
                    structure_row_visible_[static_cast<size_t>(row_index)] != 0;
                if (ImGui::Checkbox("##show", &row_visible) &&
                    static_cast<size_t>(row_index) < structure_row_visible_.size()) {
                    structure_row_visible_[static_cast<size_t>(row_index)] = row_visible ? 1 : 0;
                }
                for (int i = 0; i < IM_ARRAYSIZE(kStructureColumns); ++i) {
                    ImGui::TableSetColumnIndex(i + 1);
                    const std::string& value = row.cells[static_cast<size_t>(i)];
                    if (i == kStructureDistanceColumn) {
                        size_t marker_index = static_cast<size_t>(row_index);
                        bool can_locate = marker_index < structure_marker_cache_.size() &&
                            structure_marker_cache_[marker_index].has_value();
                        if (render_text_cell_with_context(value, tr("menu.locate_on_plan"), can_locate)) {
                            locate_structure_row_on_plan(marker_index);
                        }
                        continue;
                    }
                    if (value.empty()) continue;
                    if (i == kStructureFilePathColumn) {
                        render_file_path_cell_with_context(value, row.open_path, tr("menu.open_in_explorer"));
                    } else {
                        ImGui::TextUnformatted(value.c_str());
                    }
                }
                ImGui::PopID();
            }
        }
        ImGui::EndTable();
    }
    ImGui::End();
}

void App::render_structure_models_window() {
    if (!show_structure_models_window_) return;
    if (dock_right_id_) ImGui::SetNextWindowDockID(dock_right_id_, ImGuiCond_FirstUseEver);
    std::string title = tr("frame.structure_models") + "###StructureModels";
    if (!ImGui::Begin(title.c_str(), &show_structure_models_window_)) {
        ImGui::End();
        return;
    }
    ensure_table_cache();
    if (ImGui::BeginTable("structure_models", IM_ARRAYSIZE(kStructureModelColumns), ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollX | ImGuiTableFlags_ScrollY)) {
        std::string file_name_header = tr("column.file_name");
        for (int i = 0; i < IM_ARRAYSIZE(kStructureModelColumns); ++i) {
            float width = kStructureModelColumns[i].width;
            if (i == kStructureModelFilePathColumn) width = table_cache_.structure_model_file_path_width;
            const char* header = i == kStructureModelFilePathColumn ? file_name_header.c_str() : kStructureModelColumns[i].header;
            ImGui::TableSetupColumn(header, width > 0.0f ? ImGuiTableColumnFlags_WidthFixed : 0, width);
        }
        setup_fixed_table_header();
        ImGui::TableHeadersRow();
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(table_cache_.structure_model_rows.size()));
        const ImVec4 preview_text_color = ImVec4(1.0f, 1.0f, 0.0f, 1.0f);
        while (clipper.Step()) {
            for (int row_index = clipper.DisplayStart; row_index < clipper.DisplayEnd; ++row_index) {
                const CachedTableRow& row = table_cache_.structure_model_rows[static_cast<size_t>(row_index)];
                const bool is_preview_model = model_preview_canvas_ && model_preview_canvas_->has_model() &&
                    row.open_path == model_preview_canvas_->model_path();
                const ImU32 row_text_color = is_preview_model
                    ? ImGui::GetColorU32(preview_text_color)
                    : ImGui::GetColorU32(ImGuiCol_Text);
                ImGui::TableNextRow();
                ImGui::PushID(row_index);
                for (int i = 0; i < IM_ARRAYSIZE(kStructureModelColumns); ++i) {
                    ImGui::TableSetColumnIndex(i);
                    const std::string& value = row.cells[static_cast<size_t>(i)];
                    if (i == 1) {
                        ImVec2 pos = ImGui::GetCursorScreenPos();
                        ImVec2 text_size = ImGui::CalcTextSize(value.c_str());
                        ImVec2 item_size(
                            std::max(1.0f, ImGui::GetContentRegionAvail().x),
                            std::max(ImGui::GetTextLineHeight(), text_size.y));
                        ImGui::InvisibleButton("structure_key_cell", item_size);
                        if (ImGui::IsItemHovered()) {
                            ImGui::GetWindowDrawList()->AddRectFilled(
                                pos, ImVec2(pos.x + item_size.x, pos.y + item_size.y),
                                ImGui::GetColorU32(ImGuiCol_HeaderHovered));
                        }
                        if (!value.empty()) {
                            ImGui::GetWindowDrawList()->AddText(pos, row_text_color, value.c_str());
                        }
                        if (ImGui::BeginPopupContextItem("structure_key_context", ImGuiPopupFlags_MouseButtonRight)) {
                            bool can_preview = !blank_ascii(row.open_path);
                            ImGui::BeginDisabled(!can_preview);
                            if (ImGui::MenuItem(tr("menu.preview_model").c_str())) {
                                preview_structure_model(row.open_path);
                            }
                            ImGui::EndDisabled();
                            ImGui::EndPopup();
                        }
                    } else if (value.empty()) {
                        continue;
                    } else if (i == kStructureModelFilePathColumn) {
                        render_file_path_cell_with_context(value, row.open_path, tr("menu.open_in_explorer"), row.open_path, row_text_color);
                    } else {
                        if (is_preview_model) ImGui::PushStyleColor(ImGuiCol_Text, preview_text_color);
                        ImGui::TextUnformatted(value.c_str());
                        if (is_preview_model) ImGui::PopStyleColor();
                    }
                }
                ImGui::PopID();
            }
        }
        ImGui::EndTable();
    }
    ImGui::End();
}

void App::render_repeaters_window() {
    if (!show_repeaters_window_) return;
    if (dock_right_id_) ImGui::SetNextWindowDockID(dock_right_id_, ImGuiCond_FirstUseEver);
    std::string title = tr("frame.repeaters") + "###Repeaters";
    if (!ImGui::Begin(title.c_str(), &show_repeaters_window_)) {
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
    if (ImGui::BeginTable("repeaters", IM_ARRAYSIZE(kRepeaterColumns) + 1, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollX | ImGuiTableFlags_ScrollY)) {
        ImGui::TableSetupColumn(tr("column.show").c_str(), ImGuiTableColumnFlags_WidthFixed, kShowColumnWidth);
        for (int i = 0; i < IM_ARRAYSIZE(kRepeaterColumns); ++i) {
            float width = kRepeaterColumns[i].width;
            if (i == kRepeaterDistanceColumn) width = table_cache_.repeater_distance_width;
            if (i == kRepeaterIntervalColumn) width = table_cache_.repeater_interval_width;
            if (i == kRepeaterFilePathColumn) width = table_cache_.repeater_file_path_width;
            ImGui::TableSetupColumn(kRepeaterColumns[i].header, width > 0.0f ? ImGuiTableColumnFlags_WidthFixed : 0, width);
        }
        setup_fixed_table_header();
        ImGui::TableHeadersRow();
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(table_cache_.repeater_rows.size()));
        while (clipper.Step()) {
            for (int row_index = clipper.DisplayStart; row_index < clipper.DisplayEnd; ++row_index) {
                const CachedTableRow& row = table_cache_.repeater_rows[static_cast<size_t>(row_index)];
                ImGui::TableNextRow();
                ImGui::PushID(row_index);
                ImGui::TableSetColumnIndex(0);
                bool row_visible = static_cast<size_t>(row_index) < repeater_row_visible_.size() &&
                    repeater_row_visible_[static_cast<size_t>(row_index)] != 0;
                if (ImGui::Checkbox("##show", &row_visible) &&
                    static_cast<size_t>(row_index) < repeater_row_visible_.size()) {
                    repeater_row_visible_[static_cast<size_t>(row_index)] = row_visible ? 1 : 0;
                }
                for (int i = 0; i < IM_ARRAYSIZE(kRepeaterColumns); ++i) {
                    ImGui::TableSetColumnIndex(i + 1);
                    const std::string& value = row.cells[static_cast<size_t>(i)];
                    if (i == kRepeaterDistanceColumn) {
                        size_t marker_index = static_cast<size_t>(row_index);
                        bool can_locate = marker_index < repeater_marker_cache_.size() &&
                            repeater_marker_cache_[marker_index].begin_marker.has_value();
                        if (render_text_cell_with_context(value, tr("menu.locate_on_plan"), can_locate)) {
                            locate_repeater_row_on_plan(marker_index);
                        }
                        continue;
                    }
                    if (value.empty()) continue;
                    if (i == kRepeaterFilePathColumn) {
                        render_file_path_cell_with_context(value, row.open_path, tr("menu.open_in_explorer"));
                    } else {
                        ImGui::TextUnformatted(value.c_str());
                    }
                }
                ImGui::PopID();
            }
        }
        ImGui::EndTable();
    }
    ImGui::End();
}
