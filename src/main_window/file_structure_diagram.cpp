/*
 * Copyright (c) 2026 Sapporo_ningyo
 *
 * Licensed under Apache License 2.0; see LICENSE and NOTICE.
 * The GUI uses Dear ImGui and ImPlot; see THIRD_PARTY_NOTICES.md.
 */

#pragma execution_character_set("utf-8")

#include "kme.h"
#include "touch_input.h"

#include "imgui.h"

#include <windows.h>
#include <shellapi.h>

#include <algorithm>
#include <filesystem>
#include <string>

namespace {

bool blank_ascii(const std::string& text) {
    return text.find_first_not_of(" \t\r\n") == std::string::npos;
}

bool same_vec2(const ImVec2& a, const ImVec2& b) {
    return a.x == b.x && a.y == b.y;
}

bool file_structure_layout_is_current(const MapModel& model,
                                      const FileStructureDiagramLayoutCache& cache,
                                      const ImGuiStyle& style) {
    return cache.source_revision == model.file_structure_revision &&
        cache.node_count == model.file_structure.size() &&
        cache.font_size == ImGui::GetFontSize() &&
        same_vec2(cache.frame_padding, style.FramePadding) &&
        same_vec2(cache.item_spacing, style.ItemSpacing) &&
        same_vec2(cache.window_padding, style.WindowPadding);
}

void rebuild_file_structure_layout(const MapModel& model,
                                   FileStructureDiagramLayoutCache& cache) {
    const ImGuiStyle& style = ImGui::GetStyle();
    const float font_size = ImGui::GetFontSize();
    const float horizontal_gap = std::max(font_size * 2.0f, style.ItemSpacing.x * 6.0f);
    const float vertical_gap = std::max(font_size * 0.75f, style.ItemSpacing.y * 2.0f);
    const float margin_x = std::max(font_size, style.WindowPadding.x * 2.0f);
    const float margin_y = std::max(font_size, style.WindowPadding.y * 2.0f);

    cache.source_revision = model.file_structure_revision;
    cache.node_count = model.file_structure.size();
    cache.font_size = font_size;
    cache.frame_padding = style.FramePadding;
    cache.item_spacing = style.ItemSpacing;
    cache.window_padding = style.WindowPadding;
    cache.node_height = ImGui::GetTextLineHeight() + style.FramePadding.y * 2.0f;
    cache.text_sizes.resize(cache.node_count);
    cache.node_widths.resize(cache.node_count);
    cache.node_positions.assign(cache.node_count, ImVec2());
    cache.content_size = ImVec2();

    for (size_t i = 0; i < cache.node_count; ++i) {
        const std::string& label = model.file_structure[i].display_name;
        cache.text_sizes[i] = ImGui::CalcTextSize(label.empty() ? "-" : label.c_str());
        cache.node_widths[i] = std::max(
            font_size * 6.0f, cache.text_sizes[i].x + style.FramePadding.x * 4.0f);
    }
    if (cache.node_count == 0) return;

    std::vector<size_t> node_depths(cache.node_count, 0);
    size_t max_depth = 0;
    for (size_t i = 1; i < cache.node_count; ++i) {
        const size_t parent_index = model.file_structure[i].parent_index;
        node_depths[i] = parent_index < i ? node_depths[parent_index] + 1 : 1;
        max_depth = std::max(max_depth, node_depths[i]);
    }

    std::vector<std::vector<size_t>> levels(max_depth + 1);
    for (size_t i = 0; i < cache.node_count; ++i) {
        levels[node_depths[i]].push_back(i);
    }

    std::vector<float> level_widths(levels.size(), 0.0f);
    float max_level_height = 0.0f;
    for (size_t depth = 0; depth < levels.size(); ++depth) {
        for (size_t node_index : levels[depth]) {
            level_widths[depth] = std::max(level_widths[depth], cache.node_widths[node_index]);
        }
        const size_t count = levels[depth].size();
        const float level_height = count == 0 ? 0.0f :
            static_cast<float>(count) * cache.node_height +
                static_cast<float>(count - 1) * vertical_gap;
        max_level_height = std::max(max_level_height, level_height);
    }

    float level_x = margin_x;
    for (size_t depth = 0; depth < levels.size(); ++depth) {
        for (size_t row = 0; row < levels[depth].size(); ++row) {
            const size_t node_index = levels[depth][row];
            cache.node_positions[node_index] = ImVec2(
                level_x + (level_widths[depth] - cache.node_widths[node_index]) * 0.5f,
                margin_y + static_cast<float>(row) * (cache.node_height + vertical_gap));
        }
        level_x += level_widths[depth] + horizontal_gap;
    }

    cache.content_size.x = level_x - horizontal_gap + margin_x;
    cache.content_size.y = max_level_height + margin_y * 2.0f;
}

} // namespace

void open_parent_directory_in_explorer(const std::string& file_path) {
    if (blank_ascii(file_path)) return;
    try {
        std::filesystem::path path = utf8_to_wide(file_path);
        std::error_code ec;
        std::filesystem::path absolute = std::filesystem::absolute(path, ec);
        if (!ec) path = absolute;
        std::filesystem::path directory = path.parent_path();
        if (directory.empty()) return;
        ShellExecuteW(nullptr, L"open", directory.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    } catch (...) {
    }
}

void App::render_file_structure_window() {
    if (!show_file_structure_window_) return;
    if (dock_main_id_) ImGui::SetNextWindowDockID(dock_main_id_, ImGuiCond_FirstUseEver);
    if (focus_file_structure_next_) ImGui::SetNextWindowFocus();

    std::string title = tr("frame.file_structure_diagram") + "###FileStructureDiagram";
    if (!ImGui::Begin(title.c_str(), &show_file_structure_window_)) {
        focus_file_structure_next_ = false;
        ImGui::End();
        return;
    }
    focus_file_structure_next_ = false;

    if (!has_model_ || model_.file_structure.empty()) {
        ImGui::TextDisabled("%s", tr("status.no_map").c_str());
        ImGui::End();
        return;
    }

    const ImGuiStyle& style = ImGui::GetStyle();
    if (!file_structure_layout_is_current(model_, file_structure_layout_cache_, style)) {
        rebuild_file_structure_layout(model_, file_structure_layout_cache_);
    }

    if (ImGui::BeginChild("file_structure_canvas", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None,
                          ImGuiWindowFlags_HorizontalScrollbar)) {
        const ImVec2 canvas_origin = ImGui::GetCursorScreenPos();
        const ImVec2 draw_origin = canvas_origin;

        ImGui::Dummy(file_structure_layout_cache_.content_size);
        ImDrawList* draw_list = ImGui::GetWindowDrawList();
        const ImU32 connector_color = ImGui::GetColorU32(ImGuiCol_Separator);

        for (size_t i = 1; i < model_.file_structure.size(); ++i) {
            const FileStructureNode& node = model_.file_structure[i];
            if (node.parent_index >= model_.file_structure.size()) continue;
            const size_t parent_index = node.parent_index;
            const ImVec2 parent_pos = file_structure_layout_cache_.node_positions[parent_index];
            const ImVec2 child_pos = file_structure_layout_cache_.node_positions[i];
            const ImVec2 parent_right(
                draw_origin.x + parent_pos.x + file_structure_layout_cache_.node_widths[parent_index],
                draw_origin.y + parent_pos.y + file_structure_layout_cache_.node_height * 0.5f);
            const ImVec2 child_left(
                draw_origin.x + child_pos.x,
                draw_origin.y + child_pos.y + file_structure_layout_cache_.node_height * 0.5f);
            const float middle_x = (parent_right.x + child_left.x) * 0.5f;
            draw_list->AddLine(parent_right, ImVec2(middle_x, parent_right.y), connector_color);
            draw_list->AddLine(ImVec2(middle_x, parent_right.y),
                               ImVec2(middle_x, child_left.y), connector_color);
            draw_list->AddLine(ImVec2(middle_x, child_left.y), child_left, connector_color);
        }

        for (size_t i = 0; i < model_.file_structure.size(); ++i) {
            const FileStructureNode& node = model_.file_structure[i];
            const ImVec2 layout_pos = file_structure_layout_cache_.node_positions[i];
            const ImVec2 node_min(draw_origin.x + layout_pos.x, draw_origin.y + layout_pos.y);
            const ImVec2 node_max(
                node_min.x + file_structure_layout_cache_.node_widths[i],
                node_min.y + file_structure_layout_cache_.node_height);
            if (!ImGui::IsRectVisible(node_min, node_max)) continue;

            ImGui::SetCursorScreenPos(node_min);
            ImGui::PushID(static_cast<int>(i));
            ImGui::InvisibleButton(
                "file_structure_node",
                ImVec2(file_structure_layout_cache_.node_widths[i],
                       file_structure_layout_cache_.node_height),
                ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
            const bool hovered = ImGui::IsItemHovered();
            const bool active = ImGui::IsItemActive();

            touch_input::open_popup_on_last_item_long_press("file_structure_node_context");
            if (ImGui::BeginPopupContextItem("file_structure_node_context",
                                             ImGuiPopupFlags_MouseButtonRight)) {
                const bool can_open = !blank_ascii(node.absolute_path);
                ImGui::BeginDisabled(!can_open);
                if (ImGui::MenuItem(tr("menu.open_in_explorer").c_str())) {
                    open_parent_directory_in_explorer(node.absolute_path);
                }
                ImGui::EndDisabled();
                ImGui::EndPopup();
            }

            ImU32 fill_color = ImGui::GetColorU32(ImGuiCol_Button);
            if (active) fill_color = ImGui::GetColorU32(ImGuiCol_ButtonActive);
            else if (hovered) fill_color = ImGui::GetColorU32(ImGuiCol_ButtonHovered);
            draw_list->AddRectFilled(node_min, node_max, fill_color, style.FrameRounding);
            draw_list->AddRect(node_min, node_max, ImGui::GetColorU32(ImGuiCol_Border),
                               style.FrameRounding, 0, std::max(1.0f, style.FrameBorderSize));

            const ImVec2 text_size = file_structure_layout_cache_.text_sizes[i];
            const ImVec2 text_pos(
                node_min.x + (file_structure_layout_cache_.node_widths[i] - text_size.x) * 0.5f,
                node_min.y + (file_structure_layout_cache_.node_height - text_size.y) * 0.5f);
            draw_list->AddText(text_pos, ImGui::GetColorU32(ImGuiCol_Text),
                               node.display_name.empty() ? "-" : node.display_name.c_str());

            if (hovered) {
                const std::string& first_line =
                    node.include_path.empty() ? node.display_name : node.include_path;
                ImGui::BeginTooltip();
                ImGui::TextUnformatted(first_line.c_str());
                ImGui::TextUnformatted(node.absolute_path.c_str());
                ImGui::EndTooltip();
            }
            ImGui::PopID();
        }
    }
    ImGui::EndChild();
    ImGui::End();
}
