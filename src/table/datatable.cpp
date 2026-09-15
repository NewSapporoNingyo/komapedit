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

#include "datatable_internal.h"

namespace datatable_internal {

std::string normalize_train_lookup_key(const std::string& key) {
    return ascii_lower(trim_gui_ascii_copy(key));
}

float scroll_x_table_height_for_rows(int row_count) {
    const ImGuiStyle& style = ImGui::GetStyle();
    const float text_row_height = ImGui::GetTextLineHeight() + style.CellPadding.y * 2.0f;
    const float row_height = std::ceil(std::max(text_row_height, ImGui::GetFrameHeight()));
    const int rows_with_header = std::max(0, row_count) + 1;
    return row_height * static_cast<float>(rows_with_header) +
        style.ScrollbarSize + style.CellPadding.y * 3.0f;
}

bool render_file_path_cell_with_context(const std::string& display_text, const std::string& open_path,
                                        const std::string& menu_label, const std::string& tooltip_text,
                                        ImU32 text_color,
                                        const std::string& change_label,
                                        bool change_enabled) {
    if (display_text.empty()) return false;

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

    touch_input::open_popup_on_last_item_long_press("file_path_context");
    bool change_requested = false;
    if (ImGui::BeginPopupContextItem("file_path_context", ImGuiPopupFlags_MouseButtonRight)) {
        bool can_open = !blank_ascii(open_path);
        ImGui::BeginDisabled(!can_open);
        if (ImGui::MenuItem(menu_label.c_str())) {
            open_parent_directory_in_explorer(open_path);
        }
        ImGui::EndDisabled();
        if (!change_label.empty()) {
            ImGui::Separator();
            ImGui::BeginDisabled(!change_enabled);
            change_requested = ImGui::MenuItem(change_label.c_str());
            ImGui::EndDisabled();
        }
        ImGui::EndPopup();
    }
    return change_requested;
}

bool render_resource_list_source(const ResourceListSource& source,
                                 const std::string& label,
                                 const std::string& open_label,
                                 const std::string& change_label,
                                 bool change_enabled) {
    ImGui::TextUnformatted((label + ":").c_str());
    ImGui::SameLine();
    if (!source.present) {
        ImGui::TextDisabled("-");
        return false;
    }
    std::string tooltip;
    if (!source.raw_argument.empty()) {
        tooltip = source.raw_argument;
    }
    if (!source.resolved_path.empty()) {
        if (!tooltip.empty()) tooltip += "\n";
        tooltip += source.resolved_path;
    }
    return render_file_path_cell_with_context(
        source.evaluated_path.empty() ? "-" : source.evaluated_path,
        source.resolved_path, open_label, tooltip, 0,
        change_label, change_enabled);
}

std::string resource_list_unavailable_message(bool has_model,
                                              const std::string& no_map_message,
                                              std::string message,
                                              const std::string& resource_list_name) {
    if (!has_model) return no_map_message;
    constexpr std::string_view placeholder = "{resource_list}";
    const size_t placeholder_position = message.find(placeholder);
    if (placeholder_position != std::string::npos) {
        message.replace(placeholder_position, placeholder.size(), resource_list_name);
    }
    return message;
}

bool render_resource_list_empty_overlay(const std::string& message,
                                        const std::string& button_label) {
    const ImGuiStyle& style = ImGui::GetStyle();
    ImGui::PushStyleColor(ImGuiCol_ChildBg,
                          ImGui::GetStyleColorVec4(ImGuiCol_WindowBg));
    ImGui::BeginChild("##ResourceListEmptyOverlay", ImVec2(0.0f, 0.0f), false,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    const ImVec2 available = ImGui::GetContentRegionAvail();
    const ImVec2 message_size = ImGui::CalcTextSize(message.c_str());
    const float button_width = ImGui::CalcTextSize(button_label.c_str()).x +
        style.FramePadding.x * 2.0f;
    const float group_height = message_size.y + style.ItemSpacing.y +
        ImGui::GetFrameHeight();
    ImGui::SetCursorPos(ImVec2(
        std::max(0.0f, (available.x - message_size.x) * 0.5f),
        std::max(0.0f, (available.y - group_height) * 0.5f)));
    ImGui::TextUnformatted(message.c_str());
    ImGui::SetCursorPosX(std::max(0.0f, (available.x - button_width) * 0.5f));
    const bool clicked = ImGui::Button(button_label.c_str());
    ImGui::EndChild();
    ImGui::PopStyleColor();
    return clicked;
}

bool begin_text_cell_context_popup(const std::string& display_text, const char* item_id,
                                   const char* popup_id, bool* item_hovered) {
    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImVec2 text_size = ImGui::CalcTextSize(display_text.c_str());
    ImVec2 item_size(
        std::max(1.0f, ImGui::GetContentRegionAvail().x),
        std::max(ImGui::GetTextLineHeight(), text_size.y));
    ImGui::InvisibleButton(item_id, item_size);
    const bool hovered = ImGui::IsItemHovered();
    if (hovered) {
        ImGui::GetWindowDrawList()->AddRectFilled(pos, ImVec2(pos.x + item_size.x, pos.y + item_size.y),
                                                  ImGui::GetColorU32(ImGuiCol_HeaderHovered));
    }
    if (!display_text.empty()) {
        ImGui::GetWindowDrawList()->AddText(pos, ImGui::GetColorU32(ImGuiCol_Text), display_text.c_str());
    }

    if (item_hovered) *item_hovered = hovered;
    touch_input::open_popup_on_last_item_long_press(popup_id);
    return ImGui::BeginPopupContextItem(popup_id, ImGuiPopupFlags_MouseButtonRight);
}

bool render_text_cell_with_context(const std::string& display_text, const std::string& menu_label, bool menu_enabled) {
    bool selected = false;
    if (begin_text_cell_context_popup(display_text, "text_cell_context_item", "text_cell_context")) {
        ImGui::BeginDisabled(!menu_enabled);
        selected = ImGui::MenuItem(menu_label.c_str());
        ImGui::EndDisabled();
        ImGui::EndPopup();
    }
    return selected;
}


TextCellContextAction render_text_cell_with_context_actions(const std::string& display_text,
                                                           const std::string& primary_label,
                                                           bool primary_enabled,
                                                           const std::string& secondary_label,
                                                           bool secondary_enabled,
                                                           const std::string& tertiary_label,
                                                           bool tertiary_enabled,
                                                           const std::string& quaternary_label,
                                                           bool quaternary_enabled) {
    TextCellContextAction action = TextCellContextAction::None;
    if (begin_text_cell_context_popup(display_text, "text_cell_context_actions_item",
                                      "text_cell_context_actions")) {
        if (!primary_label.empty()) {
            ImGui::BeginDisabled(!primary_enabled);
            if (ImGui::MenuItem(primary_label.c_str())) action = TextCellContextAction::Primary;
            ImGui::EndDisabled();
        }
        if (!secondary_label.empty()) {
            ImGui::BeginDisabled(!secondary_enabled);
            if (ImGui::MenuItem(secondary_label.c_str())) action = TextCellContextAction::Secondary;
            ImGui::EndDisabled();
        }
        if (!tertiary_label.empty() || !quaternary_label.empty()) {
            ImGui::Separator();
        }
        if (!tertiary_label.empty()) {
            ImGui::BeginDisabled(!tertiary_enabled);
            if (ImGui::MenuItem(tertiary_label.c_str())) action = TextCellContextAction::Tertiary;
            ImGui::EndDisabled();
        }
        if (!quaternary_label.empty()) {
            ImGui::BeginDisabled(!quaternary_enabled);
            if (ImGui::MenuItem(quaternary_label.c_str())) action = TextCellContextAction::Quaternary;
            ImGui::EndDisabled();
        }
        ImGui::EndPopup();
    }
    return action;
}

TextCellContextAction render_marker_text_cell_with_context(
    const std::string& display_text,
    const std::string& locate_on_plan_label, bool locate_on_plan_enabled,
    const std::string& locate_in_scene_label, bool locate_in_scene_enabled,
    const std::string& properties_label, bool properties_enabled,
    const std::string& delete_label, bool delete_enabled) {
    return render_text_cell_with_context_actions(
        display_text,
        locate_on_plan_label, locate_on_plan_enabled,
        locate_in_scene_label, locate_in_scene_enabled,
        properties_label, properties_enabled,
        delete_label, delete_enabled);
}
RepeaterTextCellContextAction render_repeater_text_cell_with_context_actions(
    const std::string& display_text,
    const std::string& locate_on_plan_label, bool locate_on_plan_enabled,
    const std::string& locate_in_scene_label, bool locate_in_scene_enabled,
    const std::string& properties_label, const std::string& delete_label,
    const std::string& delete_all_label, const std::string& delete_change_point_label,
    const std::string& trim_to_change_point_label,
    const std::string& start_from_change_point_label,
    bool edit_enabled, size_t chain_begin_index, size_t chain_begin_count) {
    RepeaterTextCellContextAction action;
    if (!begin_text_cell_context_popup(display_text, "repeater_text_cell_context_item",
                                       "repeater_text_cell_context", &action.hovered)) {
        return action;
    }
    ImGui::BeginDisabled(!locate_on_plan_enabled);
    if (ImGui::MenuItem(locate_on_plan_label.c_str())) {
        action.navigation = TextCellContextAction::Primary;
    }
    ImGui::EndDisabled();
    ImGui::BeginDisabled(!locate_in_scene_enabled);
    if (ImGui::MenuItem(locate_in_scene_label.c_str())) {
        action.navigation = TextCellContextAction::Secondary;
    }
    ImGui::EndDisabled();
    ImGui::Separator();
    ImGui::BeginDisabled(!edit_enabled);
    if (ImGui::MenuItem(properties_label.c_str())) {
        action.navigation = TextCellContextAction::Tertiary;
    }
    ImGui::EndDisabled();
    if (chain_begin_count <= 1) {
        ImGui::BeginDisabled(!edit_enabled);
        if (ImGui::MenuItem(delete_label.c_str())) action.delete_requested = true;
        ImGui::EndDisabled();
    } else if (ImGui::BeginMenu(delete_label.c_str(), edit_enabled)) {
        if (ImGui::MenuItem(delete_all_label.c_str())) {
            action.delete_requested = true;
            action.delete_mode = RepeaterDeleteMode::EntireChain;
        }
        if (ImGui::MenuItem(delete_change_point_label.c_str())) {
            action.delete_requested = true;
            action.delete_mode = RepeaterDeleteMode::ChangePoint;
        }
        if (chain_begin_index != 0) {
            if (ImGui::MenuItem(trim_to_change_point_label.c_str())) {
                action.delete_requested = true;
                action.delete_mode = RepeaterDeleteMode::TrimToChangePoint;
            }
            if (ImGui::MenuItem(start_from_change_point_label.c_str())) {
                action.delete_requested = true;
                action.delete_mode = RepeaterDeleteMode::StartFromChangePoint;
            }
        }
        ImGui::EndMenu();
    }
    ImGui::EndPopup();
    return action;
}

std::vector<std::string> split_structure_key_list(const std::string& text) {
    std::vector<std::string> keys;
    size_t start = 0;
    while (start <= text.size()) {
        size_t comma = text.find(',', start);
        size_t end = comma == std::string::npos ? text.size() : comma;
        std::string key = trim_gui_ascii_copy(text.substr(start, end - start));
        if (!key.empty()) keys.push_back(std::move(key));
        if (comma == std::string::npos) break;
        start = comma + 1;
    }
    return keys;
}

std::string render_text_cell_with_submenu(const std::string& display_text, const std::string& menu_label,
                                          const std::vector<std::string>& menu_items) {
    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImVec2 text_size = ImGui::CalcTextSize(display_text.c_str());
    ImVec2 item_size(
        std::max(1.0f, ImGui::GetContentRegionAvail().x),
        std::max(ImGui::GetTextLineHeight(), text_size.y));
    ImGui::InvisibleButton("text_cell_submenu_context_item", item_size);
    if (ImGui::IsItemHovered()) {
        ImGui::GetWindowDrawList()->AddRectFilled(pos, ImVec2(pos.x + item_size.x, pos.y + item_size.y),
                                                  ImGui::GetColorU32(ImGuiCol_HeaderHovered));
    }
    if (!display_text.empty()) {
        ImGui::GetWindowDrawList()->AddText(pos, ImGui::GetColorU32(ImGuiCol_Text), display_text.c_str());
    }

    std::string selected;
    touch_input::open_popup_on_last_item_long_press("text_cell_submenu_context");
    if (ImGui::BeginPopupContextItem("text_cell_submenu_context", ImGuiPopupFlags_MouseButtonRight)) {
        if (ImGui::BeginMenu(menu_label.c_str(), !menu_items.empty())) {
            for (size_t i = 0; i < menu_items.size(); ++i) {
                const std::string& item = menu_items[i];
                ImGui::PushID(static_cast<int>(i));
                if (ImGui::MenuItem(item.c_str())) selected = item;
                ImGui::PopID();
            }
            ImGui::EndMenu();
        }
        ImGui::EndPopup();
    }
    return selected;
}


bool contains_ascii_case_insensitive(const std::string& text, const std::string& query) {
    if (query.empty() || query.size() > text.size()) return false;
    const size_t last_start = text.size() - query.size();
    for (size_t start = 0; start <= last_start; ++start) {
        bool match = true;
        for (size_t i = 0; i < query.size(); ++i) {
            if (ascii_lower(static_cast<unsigned char>(text[start + i])) !=
                    ascii_lower(static_cast<unsigned char>(query[i]))) {
                match = false;
                break;
            }
        }
        if (match) return true;
    }
    return false;
}

bool equals_ascii_case_insensitive(const std::string& text, const std::string& query) {
    if (query.empty() || text.size() != query.size()) return false;
    for (size_t i = 0; i < text.size(); ++i) {
        if (ascii_lower(static_cast<unsigned char>(text[i])) !=
                ascii_lower(static_cast<unsigned char>(query[i]))) return false;
    }
    return true;
}

std::string ascii_case_key(const std::string& text) {
    return ascii_lower(text);
}

bool matches_find_query(const std::string& text, const std::string& query, bool exact_match) {
    return exact_match
        ? equals_ascii_case_insensitive(text, query)
        : contains_ascii_case_insensitive(text, query);
}

void replace_all(std::string& text, const std::string& from, const std::string& to) {
    if (from.empty()) return;
    size_t pos = 0;
    while ((pos = text.find(from, pos)) != std::string::npos) {
        text.replace(pos, from.size(), to);
        pos += to.size();
    }
}

std::string format_find_match_status(std::string format, size_t current, size_t total) {
    replace_all(format, "{current}", std::to_string(current));
    replace_all(format, "{total}", std::to_string(total));
    return format;
}

TableFindRowsView editable_table_find_rows(
    const std::vector<CachedTableRow>& rows,
    const EditableListEditState& edit,
    const EditableListSpec& spec) {
    return {&rows, &edit, &spec};
}

void reset_table_find_results(TableFindState& state) {
    state.committed.clear();
    state.matches.clear();
    state.row_matches.clear();
    state.unused_row_matches.clear();
    state.unused_count = 0;
    state.unused_total = 0;
    state.current = -1;
    state.scroll_row = -1;
    state.has_run = false;
    state.unused_has_run = false;
}

void run_table_find(TableFindState& state,
                    const TableFindRowsView& rows,
                    std::initializer_list<size_t> search_columns) {
    state.committed = state.query;
    state.matches.clear();
    state.row_matches.assign(rows.size(), 0);
    state.unused_row_matches.clear();
    state.unused_count = 0;
    state.unused_total = 0;
    state.current = -1;
    state.scroll_row = -1;
    state.has_run = true;
    state.unused_has_run = false;

    if (blank_ascii(state.committed)) return;

    for (size_t row_index = 0; row_index < rows.size(); ++row_index) {
        if (!rows.searchable(row_index)) continue;
        bool matched = false;
        for (size_t column : search_columns) {
            const std::string* cell = rows.cell(row_index, column);
            if (cell &&
                matches_find_query(*cell, state.committed, state.exact)) {
                matched = true;
                break;
            }
        }
        if (!matched) continue;
        state.row_matches[row_index] = 1;
        state.matches.push_back(row_index);
    }

    if (!state.matches.empty()) {
        state.current = 0;
        state.scroll_row = static_cast<int>(state.matches.front());
    }
}

void set_exact_table_find_query(TableFindState& state, const std::string& query) {
    const size_t capacity = IM_ARRAYSIZE(state.query);
    const size_t copy_size = std::min(capacity - 1, query.size());
    std::copy_n(query.data(), copy_size, state.query);
    state.query[copy_size] = '\0';
    state.exact = true;
    state.panel_expanded = true;
}

void step_table_find(TableFindState& state, int delta) {
    if (state.matches.empty()) return;
    if (state.current < 0) {
        state.current = 0;
    } else {
        const int count = static_cast<int>(state.matches.size());
        state.current = (state.current + delta + count) % count;
    }
    state.scroll_row = static_cast<int>(state.matches[static_cast<size_t>(state.current)]);
}

std::string table_find_status_text(const TableFindState& state,
                                   const std::string& find_no_match,
                                   const std::string& find_match,
                                   const std::string& unused_no_match,
                                   const std::string& unused_match) {
    if (state.unused_has_run) {
        if (state.unused_count == 0) return unused_no_match;
        std::string text = unused_match;
        replace_all(text, "{unused}", std::to_string(state.unused_count));
        replace_all(text, "{total}", std::to_string(state.unused_total));
        return text;
    }
    if (!state.has_run) return {};
    if (state.matches.empty() || state.current < 0) return find_no_match;
    return format_find_match_status(find_match,
                                    static_cast<size_t>(state.current) + 1,
                                    state.matches.size());
}


void render_status_line(const std::string& text) {
    const ImGuiStyle& style = ImGui::GetStyle();
    const float height = ImGui::GetFrameHeight();
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const ImVec2 size(std::max(1.0f, ImGui::GetContentRegionAvail().x), height);
    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y), ImGui::GetColorU32(ImGuiCol_WindowBg), style.FrameRounding);
    if (!text.empty()) {
        const ImVec2 text_pos(pos.x + style.FramePadding.x,
                              pos.y + std::max(0.0f, (height - ImGui::GetTextLineHeight()) * 0.5f));
        ImGui::PushClipRect(pos, ImVec2(pos.x + size.x, pos.y + size.y), true);
        draw->AddText(text_pos, ImGui::GetColorU32(ImGuiCol_Text), text.c_str());
        ImGui::PopClipRect();
    }
    ImGui::Dummy(size);
}

void expand_width_for_text(float& width, const std::string& text) {
    if (text.empty()) return;
    float text_width = ImGui::CalcTextSize(text.c_str()).x + ImGui::GetStyle().CellPadding.x * 2.0f + 12.0f;
    width = std::max(width, text_width);
}

bool all_flags_set(const std::vector<unsigned char>& flags) {
    return !flags.empty() && std::all_of(flags.begin(), flags.end(), [](unsigned char value) { return value != 0; });
}

bool all_flags_set_in_range(const std::vector<unsigned char>& flags, size_t begin, size_t count) {
    if (count == 0 || begin >= flags.size()) return false;
    const size_t end = std::min(flags.size(), begin + count);
    if (end - begin != count) return false;
    return std::all_of(flags.begin() + static_cast<std::ptrdiff_t>(begin),
                       flags.begin() + static_cast<std::ptrdiff_t>(end),
                       [](unsigned char value) { return value != 0; });
}

void set_all_flags(std::vector<unsigned char>& flags, bool value) {
    std::fill(flags.begin(), flags.end(), value ? 1 : 0);
}

void set_flags_in_range(std::vector<unsigned char>& flags, size_t begin, size_t count, bool value) {
    if (count == 0 || begin >= flags.size()) return;
    const size_t end = std::min(flags.size(), begin + count);
    std::fill(flags.begin() + static_cast<std::ptrdiff_t>(begin),
              flags.begin() + static_cast<std::ptrdiff_t>(end),
              value ? 1 : 0);
}

ImU32 table_row_highlight_color(ImVec4 theme_color) {
    ImVec4 color = clamp_theme_color(theme_color);
    color.x *= 0.68f;
    color.y *= 0.68f;
    color.z *= 0.68f;
    color.w = 0.92f;
    return ImGui::ColorConvertFloat4ToU32(color);
}

void setup_fixed_table_header() {
    ImGui::TableSetupScrollFreeze(0, 1);
}

bool render_find_panel_toggle(const char* id, const std::string& label, bool expanded) {
    const ImGuiStyle& style = ImGui::GetStyle();
    const float height = ImGui::GetFrameHeight();
    const float triangle_size = std::max(4.0f, height * 0.22f);
    const ImVec2 text_size = ImGui::CalcTextSize(label.c_str());
    const float width = std::max(height * 4.0f,
                                 text_size.x + triangle_size * 2.0f + style.FramePadding.x * 3.0f);
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const ImVec2 size(width, height);
    ImGui::InvisibleButton(id, size);

    ImDrawList* draw = ImGui::GetWindowDrawList();
    if (ImGui::IsItemHovered()) {
        draw->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y),
                            ImGui::GetColorU32(ImGuiCol_HeaderHovered), style.FrameRounding);
    }

    const ImU32 text_color = ImGui::GetColorU32(ImGuiCol_Text);
    const ImVec2 text_pos(pos.x + style.FramePadding.x,
                          pos.y + (size.y - text_size.y) * 0.5f);
    draw->AddText(text_pos, text_color, label.c_str());

    const ImVec2 center(pos.x + size.x - style.FramePadding.x - triangle_size,
                        pos.y + size.y * 0.5f);
    if (expanded) {
        draw->AddTriangleFilled(
            ImVec2(center.x - triangle_size, center.y - triangle_size * 0.5f),
            ImVec2(center.x + triangle_size, center.y - triangle_size * 0.5f),
            ImVec2(center.x, center.y + triangle_size),
            text_color);
    } else {
        draw->AddTriangleFilled(
            ImVec2(center.x - triangle_size * 0.5f, center.y - triangle_size),
            ImVec2(center.x - triangle_size * 0.5f, center.y + triangle_size),
            ImVec2(center.x + triangle_size, center.y),
            text_color);
    }

    return ImGui::IsItemClicked();
}

void render_find_panel_border(ImVec2 min, ImVec2 max) {
    if (max.x <= min.x || max.y <= min.y) return;
    ImGui::GetWindowDrawList()->AddRect(min, max, IM_COL32(255, 255, 255, 255),
                                        ImGui::GetStyle().FrameRounding, 0, 1.0f);
}

float button_width_for_label(const std::string& label) {
    return ImGui::CalcTextSize(label.c_str()).x + ImGui::GetStyle().FramePadding.x * 2.0f;
}

float radio_button_width_for_label(const std::string& label) {
    const ImGuiStyle& style = ImGui::GetStyle();
    float width = ImGui::GetFrameHeight();
    if (!label.empty()) width += style.ItemInnerSpacing.x + ImGui::CalcTextSize(label.c_str()).x;
    return width;
}

float width_after_previous_item(std::initializer_list<float> item_widths) {
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    float width = 0.0f;
    for (float item_width : item_widths) width += spacing + item_width;
    return width;
}

float find_panel_line_right_x(float right_padding) {
    return ImGui::GetCursorScreenPos().x +
        std::max(1.0f, ImGui::GetContentRegionAvail().x - right_padding);
}

bool same_line_if_next_item_fits(float next_item_width, float right_padding) {
    const float next_item_right_x =
        ImGui::GetItemRectMax().x + ImGui::GetStyle().ItemSpacing.x + next_item_width;
    if (next_item_right_x > find_panel_line_right_x(right_padding)) return false;
    ImGui::SameLine();
    return true;
}

float available_width_after_current_item(float right_padding) {
    return std::max(0.0f,
                    find_panel_line_right_x(right_padding) - ImGui::GetItemRectMax().x -
                        ImGui::GetStyle().ItemSpacing.x);
}



float find_input_width(float available_width,
                       float all_controls_width,
                       float step_controls_width) {
    available_width = std::max(1.0f, available_width);
    float width = available_width;
    if (available_width >= k_find_input_min_width + all_controls_width) {
        width = available_width - all_controls_width;
    } else if (available_width >= k_find_input_min_width + step_controls_width) {
        width = available_width - step_controls_width;
    }
    return std::max(1.0f, std::min(k_find_input_max_width, width));
}


EditableCellInteraction render_editable_cell_button(
    const std::string& display, bool selected, ImU32 text_color,
    float width, float height) {
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("cell", ImVec2(width, height));
    EditableCellInteraction interaction;
    interaction.hovered = ImGui::IsItemHovered();
    interaction.left_clicked = ImGui::IsItemClicked(ImGuiMouseButton_Left);
    interaction.right_clicked = ImGui::IsItemClicked(ImGuiMouseButton_Right);
    interaction.double_clicked = interaction.hovered &&
        ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);

    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    const ImVec2 max(pos.x + width, pos.y + height);
    if (selected) {
        draw_list->AddRect(pos, max, ImGui::GetColorU32(ImGuiCol_HeaderActive),
                           0.0f, 0, 1.5f);
    } else if (interaction.hovered) {
        draw_list->AddRect(pos, max, ImGui::GetColorU32(ImGuiCol_HeaderHovered));
    }
    if (!display.empty()) {
        const ImGuiStyle& style = ImGui::GetStyle();
        draw_list->PushClipRect(pos, max, true);
        draw_list->AddText(ImVec2(pos.x + style.CellPadding.x,
                                  pos.y + style.CellPadding.y),
                           text_color, display.c_str());
        draw_list->PopClipRect();
    }
    return interaction;
}

} // namespace datatable_internal

using namespace datatable_internal;

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

std::vector<repeater_linkage::Event> table_repeater_events(
    const std::vector<TableRow>& rows) {
    std::vector<repeater_linkage::Event> events;
    events.reserve(rows.size());
    for (size_t row_index = 0; row_index < rows.size(); ++row_index) {
        const TableRow& row = rows[row_index];
        repeater_linkage::Event event;
        event.source_index = row_index;
        event.distance = table_cell_number(row, "distance");
        event.order = table_cell_number(row, "order");
        event.key = table_cell(row, "repeaterKey");
        const std::string& method = table_cell(row, "method");
        if (method == "Begin" || method == "Begin0") {
            event.kind = repeater_linkage::EventKind::Begin;
        } else if (method == "End") {
            event.kind = repeater_linkage::EventKind::End;
        }
        events.push_back(std::move(event));
    }
    return events;
}

std::vector<std::string> section_row_values(const TableRow& row) {
    const double count_value = table_cell_number(row, "valueCount");
    if (!std::isfinite(count_value) || count_value <= 0.0) return {};
    const size_t value_count = static_cast<size_t>(count_value);
    std::vector<std::string> values;
    values.reserve(value_count);
    for (size_t value_index = 0; value_index < value_count; ++value_index) {
        values.push_back(
            table_cell(row, "value" + std::to_string(value_index)));
    }
    return values;
}

std::string join_table_values(const std::vector<std::string>& values,
                              std::string_view separator) {
    size_t text_size = separator.size() * (values.empty() ? 0 : values.size() - 1);
    for (const std::string& value : values) text_size += value.size();
    std::string text;
    text.reserve(text_size);
    for (size_t index = 0; index < values.size(); ++index) {
        if (index) text.append(separator.data(), separator.size());
        text += values[index];
    }
    return text;
}

namespace datatable_internal {

bool is_scene_table_own_track_key(const std::string& normalized_key) {
    return is_own_track_placement_key(normalized_key);
}

bool is_scene_table_track_key_valid(const std::string& raw_key,
                                    const std::unordered_set<std::string>& other_track_keys) {
    std::string normalized_key = normalize_track_lookup_key(raw_key);
    return is_scene_table_own_track_key(normalized_key) ||
        other_track_keys.find(normalized_key) != other_track_keys.end();
}

bool is_invalid_track_key_row(const TableRow& row) {
    return table_cell(row, k_invalid_track_key_cell) == "true";
}

void clear_invalid_track_key_flags(std::vector<TableRow>& rows) {
    for (TableRow& row : rows) row.cells.erase(k_invalid_track_key_cell);
}

void mark_invalid_track_key_row(TableRow& row) {
    row.cells[k_invalid_track_key_cell] = "true";
}

void append_scene_track_key_warning(MapModel& model,
                                    TableRow& row,
                                    const std::string& item_label,
                                    size_t display_row,
                                    const std::string& track_key) {
    mark_invalid_track_key_row(row);
    std::string message = "[WARN]datatable.cpp: " + item_label + " #" +
        std::to_string(display_row) + " was placed on nonexistent track [" + track_key +
        "] and will be placed on owntrack";
    model.scene_track_key_warnings.push_back(std::move(message));
}

void check_scene_track_key(MapModel& model,
                           TableRow& row,
                           const std::unordered_set<std::string>& other_track_keys,
                           const std::string& item_label,
                           size_t display_row,
                           const std::string& column_key) {
    const std::string& key = table_cell(row, column_key);
    if (is_scene_table_track_key_valid(key, other_track_keys)) return;
    append_scene_track_key_warning(model, row, item_label, display_row, key);
}

} // namespace datatable_internal

void annotate_scene_track_key_warnings(MapModel& model) {
    model.scene_track_key_warnings.clear();
    clear_invalid_track_key_flags(model.structures);
    clear_invalid_track_key_flags(model.structures_between);
    clear_invalid_track_key_flags(model.repeaters);
    clear_invalid_track_key_flags(model.signals);

    std::unordered_set<std::string> other_track_keys;
    other_track_keys.reserve(model.other_tracks.size() * 2 + 1);
    for (const OtherTrack& track : model.other_tracks) {
        std::string key = normalize_track_lookup_key(track.key);
        if (!is_scene_table_own_track_key(key)) other_track_keys.insert(std::move(key));
    }

    size_t structure_display_row = 1;
    for (TableRow& row : model.structures) {
        check_scene_track_key(model, row, other_track_keys, "Structure", structure_display_row, "trackKey");
        ++structure_display_row;
    }
    for (TableRow& row : model.structures_between) {
        check_scene_track_key(model, row, other_track_keys, "Structure", structure_display_row, "trackKey1");
        check_scene_track_key(model, row, other_track_keys, "Structure", structure_display_row, "trackKey2");
        ++structure_display_row;
    }

    size_t signal_display_row = 1;
    for (TableRow& row : model.signals) {
        check_scene_track_key(model, row, other_track_keys, "Signal",
                              signal_display_row, "trackKey");
        ++signal_display_row;
    }

    size_t repeater_display_row = 1;
    for (TableRow& row : model.repeaters) {
        const std::string& method = table_cell(row, "method");
        if (method == "Begin" || method == "Begin0") {
            check_scene_track_key(model, row, other_track_keys, "Repeater", repeater_display_row, "trackKey");
            ++repeater_display_row;
        }
    }
}
