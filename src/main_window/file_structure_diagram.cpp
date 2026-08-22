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
#include "touch_input.h"

#include "imgui.h"

#include <windows.h>
#include <shellapi.h>

#include <algorithm>
#include <deque>
#include <filesystem>
#include <map>
#include <string>
#include <utility>

namespace {

bool same_vec2(const ImVec2& a, const ImVec2& b) {
    return a.x == b.x && a.y == b.y;
}

struct FileStructureLevelGroup {
    size_t parent_index = k_no_file_structure_parent;
    std::vector<size_t> node_indices;
    float inner_width = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
};

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
    const float group_gap = std::max(font_size, style.ItemSpacing.y * 3.0f);
    const float group_padding_x = std::max(font_size * 0.5f, style.FramePadding.x * 2.0f);
    const float group_padding_y = std::max(font_size * 0.4f, style.FramePadding.y * 2.0f);
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
    cache.groups.clear();
    cache.content_size = ImVec2();

    for (size_t i = 0; i < cache.node_count; ++i) {
        const std::string& label = model.file_structure[i].display_name;
        cache.text_sizes[i] = ImGui::CalcTextSize(label.empty() ? "-" : label.c_str());
        cache.node_widths[i] = std::max(
            font_size * 6.0f, cache.text_sizes[i].x + style.FramePadding.x * 4.0f);
    }
    if (cache.node_count == 0) return;

    std::vector<size_t> node_depths(cache.node_count, 0);
    std::vector<size_t> effective_parents(cache.node_count, k_no_file_structure_parent);
    size_t max_depth = 0;
    for (size_t i = 1; i < cache.node_count; ++i) {
        size_t parent_index = model.file_structure[i].parent_index;
        if (parent_index >= i) parent_index = 0;
        effective_parents[i] = parent_index;
        node_depths[i] = node_depths[parent_index] + 1;
        max_depth = std::max(max_depth, node_depths[i]);
    }

    std::vector<std::vector<FileStructureLevelGroup>> level_groups(max_depth + 1);
    std::vector<size_t> group_index_by_parent(cache.node_count, k_no_file_structure_parent);
    for (size_t i = 1; i < cache.node_count; ++i) {
        const size_t depth = node_depths[i];
        const size_t parent_index = effective_parents[i];
        size_t group_index = group_index_by_parent[parent_index];
        if (group_index == k_no_file_structure_parent) {
            group_index = level_groups[depth].size();
            group_index_by_parent[parent_index] = group_index;
            FileStructureLevelGroup group;
            group.parent_index = parent_index;
            level_groups[depth].push_back(std::move(group));
        }
        level_groups[depth][group_index].node_indices.push_back(i);
    }

    std::vector<float> level_widths(max_depth + 1, 0.0f);
    level_widths[0] = cache.node_widths[0];
    float max_level_height = cache.node_height;
    for (size_t depth = 1; depth < level_groups.size(); ++depth) {
        float level_height = 0.0f;
        for (size_t group_index = 0; group_index < level_groups[depth].size(); ++group_index) {
            FileStructureLevelGroup& group = level_groups[depth][group_index];
            for (size_t node_index : group.node_indices) {
                group.inner_width = std::max(group.inner_width, cache.node_widths[node_index]);
            }
            const bool draw_border = group.node_indices.size() > 1;
            group.width = group.inner_width + (draw_border ? group_padding_x * 2.0f : 0.0f);
            group.height = (draw_border ? group_padding_y * 2.0f : 0.0f) +
                static_cast<float>(group.node_indices.size()) * cache.node_height +
                static_cast<float>(group.node_indices.size() - 1) * vertical_gap;
            level_widths[depth] = std::max(level_widths[depth], group.width);
            if (group_index) level_height += group_gap;
            level_height += group.height;
        }
        max_level_height = std::max(max_level_height, level_height);
    }

    cache.groups.reserve(cache.node_count - 1);
    float level_x = margin_x;
    cache.node_positions[0] = ImVec2(level_x, margin_y);
    level_x += level_widths[0] + horizontal_gap;
    for (size_t depth = 1; depth < level_groups.size(); ++depth) {
        float group_y = margin_y;
        for (FileStructureLevelGroup& group : level_groups[depth]) {
            const bool draw_border = group.node_indices.size() > 1;
            const float group_x = level_x + (level_widths[depth] - group.width) * 0.5f;
            cache.groups.push_back({
                group.parent_index,
                draw_border,
                ImVec2(group_x, group_y),
                ImVec2(group_x + group.width, group_y + group.height)});

            float node_y = group_y + (draw_border ? group_padding_y : 0.0f);
            for (size_t node_index : group.node_indices) {
                cache.node_positions[node_index] = ImVec2(
                    group_x + (draw_border ? group_padding_x : 0.0f) +
                        (group.inner_width - cache.node_widths[node_index]) * 0.5f,
                    node_y);
                node_y += cache.node_height + vertical_gap;
            }
            group_y += group.height + group_gap;
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

const std::vector<std::string>& App::file_structure_include_edit_ids() {
    if (file_structure_include_ids_current_ &&
        file_structure_include_ids_revision_ == model_.file_structure_revision &&
        file_structure_include_ids_handle_ == handle_ &&
        file_structure_include_edit_ids_.size() == model_.file_structure.size()) {
        return file_structure_include_edit_ids_;
    }
    file_structure_include_ids_current_ = true;
    file_structure_include_ids_revision_ = model_.file_structure_revision;
    file_structure_include_ids_handle_ = handle_;
    file_structure_include_edit_ids_.assign(model_.file_structure.size(), {});
    if (!has_model_) return file_structure_include_edit_ids_;
    std::map<std::pair<std::string, std::string>,
             std::deque<const EditStatementInfo*>>
        include_candidates;
    for (const EditStatementInfo& statement : model_.edit_statements) {
        if (statement.statement_kind != "Include") continue;
        if (statement.source.file_path.empty() ||
            statement.first_evaluated_value.empty() ||
            statement.edit_id.empty()) {
            continue;
        }
        include_candidates[{statement.source.file_path,
                            statement.first_evaluated_value}]
            .push_back(&statement);
    }
    for (size_t i = 0; i < model_.file_structure.size(); ++i) {
        const FileStructureNode& node = model_.file_structure[i];
        if (node.parent_index == k_no_file_structure_parent ||
            node.parent_index >= model_.file_structure.size()) {
            continue;
        }
        const FileStructureNode& parent =
            model_.file_structure[node.parent_index];
        auto found = include_candidates.find(
            {parent.absolute_path, node.include_path});
        if (found == include_candidates.end() || found->second.empty()) continue;
        file_structure_include_edit_ids_[i] = found->second.front()->edit_id;
        found->second.pop_front();
    }
    return file_structure_include_edit_ids_;
}

void App::render_source_file_context_menu(const char* popup_id,
                                          const std::string& file_path,
                                          const std::string& include_edit_id) {
    touch_input::open_popup_on_last_item_long_press(popup_id);
    if (!ImGui::BeginPopupContextItem(popup_id, ImGuiPopupFlags_MouseButtonRight)) return;

    const bool can_open = !blank_ascii(file_path);
    const bool can_preview = can_open && is_supported_text_preview_file(file_path);
    ImGui::BeginDisabled(!can_preview);
    if (ImGui::MenuItem(tr("menu.preview_text").c_str())) {
        open_text_preview(file_path, true);
    }
    ImGui::EndDisabled();
    ImGui::BeginDisabled(!can_open);
    if (ImGui::MenuItem(tr("menu.open_in_explorer").c_str())) {
        open_parent_directory_in_explorer(file_path);
    }
    ImGui::EndDisabled();
    ImGui::Separator();
    ImGui::BeginDisabled(!edit_actions_available() || include_edit_id.empty());
    if (ImGui::MenuItem(tr("menu.unreference_include").c_str())) {
        request_element_delete(include_edit_id, "include",
                               RepeaterDeleteMode::EntireChain);
    }
    ImGui::EndDisabled();
    ImGui::EndPopup();
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
        const ImU32 relationship_color = IM_COL32(255, 255, 255, 255);

        for (const FileStructureDiagramGroupLayout& group : file_structure_layout_cache_.groups) {
            if (group.parent_index >= model_.file_structure.size()) continue;
            const size_t parent_index = group.parent_index;
            const ImVec2 parent_pos = file_structure_layout_cache_.node_positions[parent_index];
            const ImVec2 parent_right(
                draw_origin.x + parent_pos.x + file_structure_layout_cache_.node_widths[parent_index],
                draw_origin.y + parent_pos.y + file_structure_layout_cache_.node_height * 0.5f);
            const float child_endpoint_ratio = group.draw_border ? 0.25f : 0.5f;
            const ImVec2 child_endpoint(
                draw_origin.x + group.min.x,
                draw_origin.y + group.min.y +
                    (group.max.y - group.min.y) * child_endpoint_ratio);
            const float middle_x = (parent_right.x + child_endpoint.x) * 0.5f;
            draw_list->AddLine(parent_right,
                               ImVec2(middle_x, parent_right.y), relationship_color);
            draw_list->AddLine(ImVec2(middle_x, parent_right.y),
                               ImVec2(middle_x, child_endpoint.y), relationship_color);
            draw_list->AddLine(ImVec2(middle_x, child_endpoint.y),
                               child_endpoint, relationship_color);
        }

        for (const FileStructureDiagramGroupLayout& group : file_structure_layout_cache_.groups) {
            if (!group.draw_border) continue;
            const ImVec2 group_min(draw_origin.x + group.min.x, draw_origin.y + group.min.y);
            const ImVec2 group_max(draw_origin.x + group.max.x, draw_origin.y + group.max.y);
            draw_list->AddRect(group_min, group_max, relationship_color,
                               style.FrameRounding, 0, 1.0f);
        }

        const std::vector<std::string>& include_edit_ids =
            file_structure_include_edit_ids();
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

            render_source_file_context_menu(
                "file_structure_node_context", node.absolute_path,
                i < include_edit_ids.size() ? include_edit_ids[i]
                                            : std::string{});

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
