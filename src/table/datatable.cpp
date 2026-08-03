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
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>


namespace {

bool blank_ascii(const std::string& text) {
    return text.find_first_not_of(" \t\r\n") == std::string::npos;
}

std::string normalize_train_lookup_key(const std::string& key) {
    std::string normalized = trim_gui_ascii_copy(key);
    std::transform(normalized.begin(), normalized.end(), normalized.begin(),
                   [](unsigned char ch) {
                       return static_cast<char>(std::tolower(ch));
                   });
    return normalized;
}

float scroll_x_table_height_for_rows(int row_count) {
    const ImGuiStyle& style = ImGui::GetStyle();
    const float text_row_height = ImGui::GetTextLineHeight() + style.CellPadding.y * 2.0f;
    const float row_height = std::ceil(std::max(text_row_height, ImGui::GetFrameHeight()));
    const int rows_with_header = std::max(0, row_count) + 1;
    return row_height * static_cast<float>(rows_with_header) +
        style.ScrollbarSize + style.CellPadding.y * 3.0f;
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

    touch_input::open_popup_on_last_item_long_press("file_path_context");
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

void render_resource_list_source(const ResourceListSource& source,
                                 const std::string& label,
                                 const std::string& open_label) {
    ImGui::TextUnformatted((label + ":").c_str());
    ImGui::SameLine();
    if (!source.present) {
        ImGui::TextDisabled("-");
        return;
    }
    std::string tooltip;
    if (!source.raw_argument.empty()) {
        tooltip = source.raw_argument;
    }
    if (!source.resolved_path.empty()) {
        if (!tooltip.empty()) tooltip += "\n";
        tooltip += source.resolved_path;
    }
    render_file_path_cell_with_context(
        source.evaluated_path.empty() ? "-" : source.evaluated_path,
        source.resolved_path, open_label, tooltip);
}

bool begin_text_cell_context_popup(const std::string& display_text, const char* item_id,
                                   const char* popup_id, bool* item_hovered = nullptr) {
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

enum class TextCellContextAction {
    None,
    Primary,
    Secondary,
    Tertiary,
    Quaternary,
};

TextCellContextAction render_text_cell_with_context_actions(const std::string& display_text,
                                                           const std::string& primary_label,
                                                           bool primary_enabled,
                                                           const std::string& secondary_label,
                                                           bool secondary_enabled,
                                                           const std::string& tertiary_label = {},
                                                           bool tertiary_enabled = false,
                                                           const std::string& quaternary_label = {},
                                                           bool quaternary_enabled = false) {
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
    const std::string& properties_label = {}, bool properties_enabled = false,
    const std::string& delete_label = {}, bool delete_enabled = false) {
    return render_text_cell_with_context_actions(
        display_text,
        locate_on_plan_label, locate_on_plan_enabled,
        locate_in_scene_label, locate_in_scene_enabled,
        properties_label, properties_enabled,
        delete_label, delete_enabled);
}

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

char ascii_fold(char c) {
    return c >= 'A' && c <= 'Z' ? static_cast<char>(c + ('a' - 'A')) : c;
}

bool contains_ascii_case_insensitive(const std::string& text, const std::string& query) {
    if (query.empty() || query.size() > text.size()) return false;
    const size_t last_start = text.size() - query.size();
    for (size_t start = 0; start <= last_start; ++start) {
        bool match = true;
        for (size_t i = 0; i < query.size(); ++i) {
            if (ascii_fold(text[start + i]) != ascii_fold(query[i])) {
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
        if (ascii_fold(text[i]) != ascii_fold(query[i])) return false;
    }
    return true;
}

std::string ascii_case_key(const std::string& text) {
    std::string out = text;
    for (char& ch : out) ch = ascii_fold(ch);
    return out;
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

constexpr float k_find_input_min_width = 80.0f;
constexpr float k_find_input_max_width = 280.0f;

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

template <typename RunFindFn, typename RunUnusedFn, typename StepFn, typename StatusTextFn>
void render_table_find_panel(TableFindState& state,
                             const char* id,
                             const std::string& find_label,
                             const std::string& partial_label,
                             const std::string& exact_label,
                             const std::string& unused_label,
                             RunFindFn run_find,
                             RunUnusedFn run_unused,
                             StepFn step,
                             StatusTextFn status_text) {
    std::string toggle_id = std::string("##") + id + "_find_panel_toggle";
    std::string input_id = std::string("##") + id + "_find_text";
    std::string prev_id = std::string("↑##") + id + "_find_prev";
    std::string next_id = std::string("↓##") + id + "_find_next";
    std::string partial_id = partial_label + "##" + id + "_partial";
    std::string exact_id = exact_label + "##" + id + "_exact";

    ImGui::BeginGroup();
    if (render_find_panel_toggle(toggle_id.c_str(), find_label, state.panel_expanded)) {
        state.panel_expanded = !state.panel_expanded;
    }
    if (state.panel_expanded) {
        const float indent = ImGui::GetStyle().FramePadding.x;
        const float right_padding = ImGui::GetStyle().FramePadding.x;
        const float arrow_button_width = ImGui::GetFrameHeight();
        const float partial_width = radio_button_width_for_label(partial_label);
        const float exact_width = radio_button_width_for_label(exact_label);
        const float unused_width = button_width_for_label(unused_label);
        const float step_controls_width = width_after_previous_item({
            arrow_button_width,
            arrow_button_width,
        });
        const float all_controls_width = width_after_previous_item({
            arrow_button_width,
            arrow_button_width,
            partial_width,
            exact_width,
            unused_width,
        });
        ImGui::Spacing();
        ImGui::Indent(indent);
        if (ImGui::Button(find_label.c_str())) run_find();
        float input_available_width = ImGui::GetContentRegionAvail().x - right_padding;
        const float available_after_find = available_width_after_current_item(right_padding);
        if (available_after_find >= k_find_input_min_width) {
            input_available_width = available_after_find;
            ImGui::SameLine();
        }
        const float input_width = find_input_width(input_available_width,
                                                  all_controls_width,
                                                  step_controls_width);
        ImGui::SetNextItemWidth(input_width);
        if (ImGui::InputText(input_id.c_str(), state.query, IM_ARRAYSIZE(state.query),
                             ImGuiInputTextFlags_EnterReturnsTrue)) {
            run_find();
        }
        same_line_if_next_item_fits(arrow_button_width, right_padding);
        ImGui::BeginDisabled(state.matches.empty());
        if (ImGui::Button(prev_id.c_str(), ImVec2(arrow_button_width, 0.0f))) {
            step(-1);
        }
        same_line_if_next_item_fits(arrow_button_width, right_padding);
        if (ImGui::Button(next_id.c_str(), ImVec2(arrow_button_width, 0.0f))) {
            step(1);
        }
        ImGui::EndDisabled();
        same_line_if_next_item_fits(partial_width, right_padding);
        if (ImGui::RadioButton(partial_id.c_str(), !state.exact)) {
            state.exact = false;
            if (state.has_run || !blank_ascii(state.query)) run_find();
        }
        same_line_if_next_item_fits(exact_width, right_padding);
        if (ImGui::RadioButton(exact_id.c_str(), state.exact)) {
            state.exact = true;
            if (state.has_run || !blank_ascii(state.query)) run_find();
        }
        same_line_if_next_item_fits(unused_width, right_padding);
        if (ImGui::Button(unused_label.c_str())) run_unused();
        render_status_line(status_text());
        ImGui::Unindent(indent);
    }
    ImGui::EndGroup();
    ImVec2 find_panel_max = ImGui::GetItemRectMax();
    if (state.panel_expanded) find_panel_max.x += ImGui::GetStyle().FramePadding.x;
    render_find_panel_border(ImGui::GetItemRectMin(), find_panel_max);
    ImGui::Spacing();
}

} // namespace

constexpr float k_show_column_width = 56.0f;
constexpr ImU32 k_find_match_row_color = IM_COL32(104, 184, 255, 96);
constexpr ImU32 k_unused_structure_model_row_color = IM_COL32(72, 196, 112, 120);
constexpr ImU32 k_invalid_track_key_row_color = IM_COL32(255, 72, 72, 130);
constexpr ImU32 k_pending_edit_row_color = IM_COL32(255, 196, 64, 92);
constexpr ImU32 k_pending_delete_row_color = IM_COL32(255, 96, 64, 118);
constexpr const char* k_invalid_track_key_cell = "_invalidTrackKey";

static const TableColumnDef k_structure_model_columns[] = {
    {"rowNumber", "#", 40.0f},
    {"structureKey", "structureKey", 120.0f},
    {"filePath", "filePath", 200.0f},
};
constexpr int k_structure_model_key_column = 1;
constexpr int k_structure_model_file_path_column = IM_ARRAYSIZE(k_structure_model_columns) - 1;

static const TableColumnDef k_other_train_columns[] = {
    {"rowNumber", "#", 40.0f},
    {"distance", "distance", 110.0f},
    {"trainKey", "trainKey", 120.0f},
    {"filePath", "filePath", 200.0f},
    {"trackKey", "trackKey", 90.0f},
    {"direction", "direction", 80.0f},
};
constexpr int k_other_train_distance_column = 1;
constexpr int k_other_train_file_path_column = 3;

static const TableColumnDef k_other_train_stop_columns[] = {
    {"rowNumber", "#", 40.0f},
    {"distance", "distance", 110.0f},
    {"trainKey", "trainKey", 120.0f},
    {"decelerate", "decelerate", 90.0f},
    {"stopTime", "stopTime", 90.0f},
    {"accelerate", "accelerate", 90.0f},
    {"speed", "speed", 80.0f},
    {"filePath", "filePath", 200.0f},
};
constexpr int k_other_train_stop_distance_column = 1;
constexpr int k_other_train_stop_file_path_column = IM_ARRAYSIZE(k_other_train_stop_columns) - 1;

static const TableColumnDef k_sound_list_columns[] = {
    {"rowNumber", "#", 40.0f},
    {"soundKey", "soundKey", 120.0f},
    {"filePath", "filePath", 200.0f},
    {"bufferCount", "bufferCount", 90.0f},
};
constexpr int k_sound_list_key_column = 1;
constexpr int k_sound_list_file_path_column = 2;
constexpr int k_sound_list_buffer_count_column = 3;

static const TableColumnDef k_structure_put_columns[] = {
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

static const TableColumnDef k_structure_between_columns[] = {
    {"distance", "distance", 0.0f},
    {"method", "method", 0.0f},
    {"structureKey", "structureKey", 0.0f},
    {"trackKey1", "trackKey1", 0.0f},
    {"trackKey2", "trackKey2", 0.0f},
    {"flag", "flag", 0.0f},
    {"filePath", "filePath", 200.0f},
};
constexpr int k_structure_distance_column = 0;
constexpr int k_structure_key_column = 2;
constexpr int k_structure_put_file_path_column = IM_ARRAYSIZE(k_structure_put_columns) - 1;
constexpr int k_structure_between_file_path_column = IM_ARRAYSIZE(k_structure_between_columns) - 1;

static const TableColumnDef k_repeater_columns[] = {
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
constexpr int k_repeater_distance_column = 1;
constexpr int k_repeater_interval_column = 13;
constexpr int k_repeater_structure_keys_column = 14;
constexpr int k_repeater_file_path_column = IM_ARRAYSIZE(k_repeater_columns) - 1;

static const TableColumnDef k_signal_aspect_fixed_columns[] = {
    {"rowNumber", "#", 40.0f},
    {"signalAspectKey", "signalAspectKey", 150.0f},
};
constexpr int k_signal_aspect_key_column = 1;
constexpr int k_signal_aspect_structure_key_column_offset = IM_ARRAYSIZE(k_signal_aspect_fixed_columns);
constexpr size_t k_max_signal_aspect_table_columns = 511;
constexpr size_t k_max_signal_aspect_structure_key_columns =
    k_max_signal_aspect_table_columns - static_cast<size_t>(k_signal_aspect_structure_key_column_offset);

static const TableColumnDef k_signal_columns[] = {
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
constexpr int k_signal_distance_column = 1;
constexpr int k_signal_signal_aspect_key_column = 3;
constexpr int k_signal_file_path_column = IM_ARRAYSIZE(k_signal_columns) - 1;

static const TableColumnDef k_beacon_columns[] = {
    {"rowNumber", "#", 40.0f},
    {"distance", "distance", 110.0f},
    {"type", "type", 70.0f},
    {"section", "section", 70.0f},
    {"sendData", "sendData", 90.0f},
    {"filePath", "filePath", 200.0f},
};
constexpr int k_beacon_distance_column = 1;
constexpr int k_beacon_file_path_column = IM_ARRAYSIZE(k_beacon_columns) - 1;

static const TableColumnDef k_irregularity_columns[] = {
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
constexpr int k_irregularity_distance_column = 1;
constexpr int k_irregularity_file_path_column = IM_ARRAYSIZE(k_irregularity_columns) - 1;

static const TableColumnDef k_map_sound_columns[] = {
    {"rowNumber", "#", 40.0f},
    {"distance", "distance", 110.0f},
    {"soundKey", "soundKey", 120.0f},
    {"filePath", "filePath", 200.0f},
};
constexpr int k_map_sound_distance_column = 1;
constexpr int k_map_sound_key_column = 2;
constexpr int k_map_sound_file_path_column = IM_ARRAYSIZE(k_map_sound_columns) - 1;

static const TableColumnDef k_rolling_noise_columns[] = {
    {"rowNumber", "#", 40.0f},
    {"distance", "distance", 110.0f},
    {"index", "index", 70.0f},
    {"filePath", "filePath", 200.0f},
};
constexpr int k_rolling_noise_distance_column = 1;
constexpr int k_rolling_noise_file_path_column = IM_ARRAYSIZE(k_rolling_noise_columns) - 1;

static const TableColumnDef k_flange_noise_columns[] = {
    {"rowNumber", "#", 40.0f},
    {"distance", "distance", 110.0f},
    {"index", "index", 70.0f},
    {"filePath", "filePath", 200.0f},
};
constexpr int k_flange_noise_distance_column = 1;
constexpr int k_flange_noise_file_path_column = IM_ARRAYSIZE(k_flange_noise_columns) - 1;

static const TableColumnDef k_joint_noise_columns[] = {
    {"rowNumber", "#", 40.0f},
    {"distance", "distance", 110.0f},
    {"index", "index", 70.0f},
    {"filePath", "filePath", 200.0f},
};
constexpr int k_joint_noise_distance_column = 1;
constexpr int k_joint_noise_file_path_column = IM_ARRAYSIZE(k_joint_noise_columns) - 1;

static const TableColumnDef k_background_columns[] = {
    {"rowNumber", "#", 40.0f},
    {"distance", "distance", 110.0f},
    {"structureKey", "structureKey", 120.0f},
    {"filePath", "filePath", 200.0f},
};
constexpr int k_background_distance_column = 1;
constexpr int k_background_structure_key_column = 2;
constexpr int k_background_file_path_column = IM_ARRAYSIZE(k_background_columns) - 1;

static const TableColumnDef k_adhesion_columns[] = {
    {"rowNumber", "#", 40.0f},
    {"distance", "distance", 110.0f},
    {"a", "a", 70.0f},
    {"b", "b", 70.0f},
    {"c", "c", 70.0f},
    {"filePath", "filePath", 200.0f},
};
constexpr int k_adhesion_distance_column = 1;
constexpr int k_adhesion_file_path_column = IM_ARRAYSIZE(k_adhesion_columns) - 1;

static const TableColumnDef k_cab_illuminance_columns[] = {
    {"rowNumber", "#", 40.0f},
    {"distance", "distance", 110.0f},
    {"value", "value", 70.0f},
    {"filePath", "filePath", 200.0f},
};
constexpr int k_cab_illuminance_distance_column = 1;
constexpr int k_cab_illuminance_file_path_column = IM_ARRAYSIZE(k_cab_illuminance_columns) - 1;

static const TableColumnDef k_fog_columns[] = {
    {"rowNumber", "#", 40.0f},
    {"distance", "distance", 110.0f},
    {"density", "density", 80.0f},
    {"red", "red", 70.0f},
    {"green", "green", 70.0f},
    {"blue", "blue", 70.0f},
    {"filePath", "filePath", 200.0f},
};
constexpr int k_fog_distance_column = 1;
constexpr int k_fog_file_path_column = IM_ARRAYSIZE(k_fog_columns) - 1;

static const TableColumnDef k_draw_distance_columns[] = {
    {"rowNumber", "#", 40.0f},
    {"distance", "distance", 110.0f},
    {"value", "value", 70.0f},
    {"filePath", "filePath", 200.0f},
};
constexpr int k_draw_distance_distance_column = 1;
constexpr int k_draw_distance_file_path_column = IM_ARRAYSIZE(k_draw_distance_columns) - 1;

static const TableColumnDef k_station_position_columns[] = {
    {"rowNumber", "#", 40.0f},
    {"dist", "dist", 70.0f},
    {"posKey", "key", 80.0f},
    {"door", "door", 55.0f},
    {"margin1", "back", 65.0f},
    {"margin2", "front", 65.0f},
};

static const TableColumnDef k_station_definition_columns[] = {
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

namespace {

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

} // namespace

void annotate_scene_track_key_warnings(MapModel& model) {
    model.scene_track_key_warnings.clear();
    clear_invalid_track_key_flags(model.structures);
    clear_invalid_track_key_flags(model.structures_between);
    clear_invalid_track_key_flags(model.repeaters);

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

    size_t repeater_display_row = 1;
    for (TableRow& row : model.repeaters) {
        const std::string& method = table_cell(row, "method");
        if (method == "Begin" || method == "Begin0") {
            check_scene_track_key(model, row, other_track_keys, "Repeater", repeater_display_row, "trackKey");
            ++repeater_display_row;
        }
    }
}

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
        output.reserve(rows.size());
        for (size_t row_index = 0; row_index < rows.size(); ++row_index) {
            const TableRow& row = rows[row_index];
            CachedTableRow cached;
            cached.cells.resize(3 + value_columns);
            cached.cells[0] = std::to_string(row_index + 1);
            cached.cells[1] = table_cell(row, "distance");
            for (size_t value_index = 0; value_index < value_columns; ++value_index) {
                cached.cells[2 + value_index] =
                    table_cell(row, "value" + std::to_string(value_index));
            }
            cached.open_path = table_cell(row, "filePath");
            cached.cells[2 + value_columns] = display_name_from_path(cached.open_path);
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

    cache.signal_distance_width = 0.0f;
    expand_width_for_text(cache.signal_distance_width, k_signal_columns[k_signal_distance_column].header);
    cache.signal_rows.reserve(model_.signals.size());
    for (size_t row_index = 0; row_index < model_.signals.size(); ++row_index) {
        const TableRow& row = model_.signals[row_index];
        CachedTableRow cached;
        copy_table_row_metadata(row, cached);
        cached.cells.resize(IM_ARRAYSIZE(k_signal_columns));
        cached.open_path = table_cell(row, "filePath");
        cached.tooltip_text = cached.open_path;
        for (int i = 0; i < IM_ARRAYSIZE(k_signal_columns); ++i) {
            if (i == k_signal_file_path_column) {
                cached.cells[i] = display_name_from_path(cached.open_path);
                expand_width_for_text(cache.signal_file_path_width, cached.cells[i]);
            } else {
                cached.cells[i] = table_cell(row, k_signal_columns[i].key);
            }
        }
        cached.cells[0] = std::to_string(row_index + 1);
        expand_width_for_text(cache.signal_distance_width, cached.cells[k_signal_distance_column]);
        cache.signal_rows.push_back(std::move(cached));
    }

    cache.beacon_distance_width = 0.0f;
    expand_width_for_text(cache.beacon_distance_width, k_beacon_columns[k_beacon_distance_column].header);
    cache.beacon_rows.reserve(model_.beacons.size());
    for (size_t row_index = 0; row_index < model_.beacons.size(); ++row_index) {
        const TableRow& row = model_.beacons[row_index];
        CachedTableRow cached;
        copy_table_row_metadata(row, cached);
        cached.cells.resize(IM_ARRAYSIZE(k_beacon_columns));
        cached.open_path = table_cell(row, "filePath");
        cached.tooltip_text = cached.open_path;
        for (int i = 0; i < IM_ARRAYSIZE(k_beacon_columns); ++i) {
            if (i == k_beacon_file_path_column) {
                cached.cells[i] = display_name_from_path(cached.open_path);
                expand_width_for_text(cache.beacon_file_path_width, cached.cells[i]);
            } else {
                cached.cells[i] = table_cell(row, k_beacon_columns[i].key);
            }
        }
        cached.cells[0] = std::to_string(row_index + 1);
        expand_width_for_text(cache.beacon_distance_width, cached.cells[k_beacon_distance_column]);
        cache.beacon_rows.push_back(std::move(cached));
    }

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

    cache.background_distance_width = 0.0f;
    expand_width_for_text(cache.background_distance_width, k_background_columns[k_background_distance_column].header);
    cache.background_rows.reserve(model_.backgrounds.size());
    for (size_t row_index = 0; row_index < model_.backgrounds.size(); ++row_index) {
        const TableRow& row = model_.backgrounds[row_index];
        CachedTableRow cached;
        copy_table_row_metadata(row, cached);
        cached.cells.resize(IM_ARRAYSIZE(k_background_columns));
        cached.open_path = table_cell(row, "filePath");
        for (int i = 0; i < IM_ARRAYSIZE(k_background_columns); ++i) {
            if (i == k_background_file_path_column) {
                cached.cells[i] = display_name_from_path(cached.open_path);
                expand_width_for_text(cache.background_file_path_width, cached.cells[i]);
            } else {
                cached.cells[i] = table_cell(row, k_background_columns[i].key);
            }
        }
        cached.cells[0] = std::to_string(row_index + 1);
        expand_width_for_text(cache.background_distance_width, cached.cells[k_background_distance_column]);
        cache.background_rows.push_back(std::move(cached));
    }

    cache.adhesion_distance_width = 0.0f;
    expand_width_for_text(cache.adhesion_distance_width, k_adhesion_columns[k_adhesion_distance_column].header);
    cache.adhesion_rows.reserve(model_.adhesions.size());
    for (size_t row_index = 0; row_index < model_.adhesions.size(); ++row_index) {
        const TableRow& row = model_.adhesions[row_index];
        CachedTableRow cached;
        copy_table_row_metadata(row, cached);
        cached.cells.resize(IM_ARRAYSIZE(k_adhesion_columns));
        cached.open_path = table_cell(row, "filePath");
        for (int i = 0; i < IM_ARRAYSIZE(k_adhesion_columns); ++i) {
            if (i == k_adhesion_file_path_column) {
                cached.cells[i] = display_name_from_path(cached.open_path);
                expand_width_for_text(cache.adhesion_file_path_width, cached.cells[i]);
            } else {
                cached.cells[i] = table_cell(row, k_adhesion_columns[i].key);
            }
        }
        cached.cells[0] = std::to_string(row_index + 1);
        expand_width_for_text(cache.adhesion_distance_width, cached.cells[k_adhesion_distance_column]);
        cache.adhesion_rows.push_back(std::move(cached));
    }

    cache.cab_illuminance_distance_width = 0.0f;
    expand_width_for_text(cache.cab_illuminance_distance_width, k_cab_illuminance_columns[k_cab_illuminance_distance_column].header);
    cache.cab_illuminance_rows.reserve(model_.cab_illuminance.size());
    for (size_t row_index = 0; row_index < model_.cab_illuminance.size(); ++row_index) {
        const TableRow& row = model_.cab_illuminance[row_index];
        CachedTableRow cached;
        copy_table_row_metadata(row, cached);
        cached.cells.resize(IM_ARRAYSIZE(k_cab_illuminance_columns));
        cached.open_path = table_cell(row, "filePath");
        for (int i = 0; i < IM_ARRAYSIZE(k_cab_illuminance_columns); ++i) {
            if (i == k_cab_illuminance_file_path_column) {
                cached.cells[i] = display_name_from_path(cached.open_path);
                expand_width_for_text(cache.cab_illuminance_file_path_width, cached.cells[i]);
            } else {
                cached.cells[i] = table_cell(row, k_cab_illuminance_columns[i].key);
            }
        }
        cached.cells[0] = std::to_string(row_index + 1);
        expand_width_for_text(cache.cab_illuminance_distance_width, cached.cells[k_cab_illuminance_distance_column]);
        cache.cab_illuminance_rows.push_back(std::move(cached));
    }

    cache.fog_distance_width = 0.0f;
    expand_width_for_text(cache.fog_distance_width, k_fog_columns[k_fog_distance_column].header);
    cache.fog_rows.reserve(model_.fogs.size());
    for (size_t row_index = 0; row_index < model_.fogs.size(); ++row_index) {
        const TableRow& row = model_.fogs[row_index];
        CachedTableRow cached;
        copy_table_row_metadata(row, cached);
        cached.cells.resize(IM_ARRAYSIZE(k_fog_columns));
        cached.open_path = table_cell(row, "filePath");
        for (int i = 0; i < IM_ARRAYSIZE(k_fog_columns); ++i) {
            if (i == k_fog_file_path_column) {
                cached.cells[i] = display_name_from_path(cached.open_path);
                expand_width_for_text(cache.fog_file_path_width, cached.cells[i]);
            } else {
                cached.cells[i] = table_cell(row, k_fog_columns[i].key);
            }
        }
        cached.cells[0] = std::to_string(row_index + 1);
        expand_width_for_text(cache.fog_distance_width, cached.cells[k_fog_distance_column]);
        cache.fog_rows.push_back(std::move(cached));
    }

    table_cache_ = std::move(cache);
}

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
    add_log("[INFO]datatable.cpp: Searching unused models...");

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
            add_log("[WARN]datatable.cpp: Found undefined structureKey:\"" +
                    key + "\"");
        });

    if (structure_model_find_.unused_count == 0) {
        add_log("[INFO]datatable.cpp: No unused models found");
    } else {
        add_log("[INFO]datatable.cpp: Found unused models (" +
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
    add_log("[INFO]datatable.cpp: Searching unused signal aspects...");

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
            add_log("[WARN]datatable.cpp: Found undefined signalAspectKey:\"" + key + "\"");
        });

    if (signal_aspect_find_.unused_count == 0) {
        add_log("[INFO]datatable.cpp: No unused signal aspects found");
    } else {
        add_log("[INFO]datatable.cpp: Found unused signal aspects (" +
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
    ensure_table_cache();
    add_log(is_3d
        ? "[INFO]datatable.cpp: Searching unused 3D sounds..."
        : "[INFO]datatable.cpp: Searching unused sounds...");

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
            }
        },
        [this, is_3d](const std::string& key) {
            add_log(std::string("[WARN]datatable.cpp: Found undefined ") +
                    (is_3d ? "3D " : "") + "soundKey:\"" + key + "\"");
        });

    if (state.unused_count == 0) {
        add_log(is_3d
            ? "[INFO]datatable.cpp: No unused 3D sounds found"
            : "[INFO]datatable.cpp: No unused sounds found");
    } else {
        add_log(std::string("[INFO]datatable.cpp: Found unused ") +
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
                    if (ImGui::Checkbox("##show", &t.visible)) {
                        scene_track_visibility_changed = true;
                    }
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
            if (scene_track_visibility_changed) {
                sync_scene_preview_track_visibility();
            }
        }
    }
    ImGui::End();
}

void App::render_editable_list_table(
    const char* table_id,
    const TableColumnDef* columns,
    int column_count,
    const std::vector<CachedTableRow>& cached_rows,
    EditableListEditState& edit,
    const EditableListSpec& spec,
    float path_column_width,
    float last_column_width,
    TableFindState* find_state,
    const std::vector<std::string>* cached_column_headers,
    const std::vector<float>* cached_column_widths,
    const std::vector<EditableListDisplayRow>* cached_display_rows) {
    const bool is_station = std::string_view(spec.row_kind) == "station.list";
    const bool is_structure = std::string_view(spec.row_kind) == "structure.model";
    const bool is_signal_aspect =
        std::string_view(spec.row_kind) == "signal.aspect";
    const std::vector<EditableListDisplayRow>* display_rows =
        is_signal_aspect
        ? (edit.rows_initialized
            ? &edit.display_rows
            : cached_display_rows)
        : nullptr;
    const int logical_row_count = edit.rows_initialized
        ? static_cast<int>(edit.visible_rows.size())
        : static_cast<int>(cached_rows.size());
    const int row_count = display_rows
        ? static_cast<int>(display_rows->size())
        : logical_row_count;
    if (find_state &&
        find_state->scroll_row >= logical_row_count) {
        find_state->scroll_row = -1;
    }
    int scroll_target_row =
        find_state ? find_state->scroll_row : -1;
    if (display_rows && scroll_target_row >= 0) {
        const auto target = std::find_if(
            display_rows->begin(), display_rows->end(),
            [&](const EditableListDisplayRow& row) {
                return !row.secondary &&
                    row.logical_row ==
                        static_cast<size_t>(
                            scroll_target_row);
            });
        scroll_target_row =
            target == display_rows->end()
            ? -1
            : static_cast<int>(
                std::distance(
                    display_rows->begin(), target));
    }
    const bool has_cached_columns =
        cached_column_headers && cached_column_widths &&
        cached_column_headers->size() >=
            static_cast<size_t>(column_count) &&
        cached_column_widths->size() >=
            static_cast<size_t>(column_count);
    if (!columns && !has_cached_columns) return;
    const int path_column = spec.path_field < 0
        ? -1
        : static_cast<int>(spec.cache_column_offset) + spec.path_field;
    const ImVec2 table_size = is_station
        ? ImVec2(0.0f, scroll_x_table_height_for_rows(row_count))
        : ImVec2(0.0f, 0.0f);
    if (!ImGui::BeginTable(
            table_id, column_count,
            ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollX |
                (is_station ? ImGuiTableFlags_None : ImGuiTableFlags_ScrollY),
            table_size)) {
        return;
    }

    const std::string file_name_header = tr("column.file_name");
    for (int column = 0; column < column_count; ++column) {
        const bool use_cached_columns = has_cached_columns;
        float width = use_cached_columns
            ? (*cached_column_widths)[static_cast<size_t>(column)]
            : columns[column].width;
        if (column == path_column && path_column_width > 0.0f) {
            width = path_column_width;
        } else if (column == column_count - 1 && last_column_width > 0.0f) {
            width = last_column_width;
        }
        const char* header = column == path_column
            ? file_name_header.c_str()
            : (use_cached_columns
                ? (*cached_column_headers)[
                    static_cast<size_t>(column)].c_str()
                : columns[column].header);
        ImGui::TableSetupColumn(
            header, width > 0.0f ? ImGuiTableColumnFlags_WidthFixed : 0, width);
    }
    setup_fixed_table_header();
    ImGui::TableHeadersRow();

    const bool can_edit = edit_actions_available();
    const ImGuiStyle& style = ImGui::GetStyle();
    const ImVec4 preview_text_color(1.0f, 1.0f, 0.0f, 1.0f);
    const auto display_at =
        [&](int row) -> const EditableListDisplayRow* {
        return display_rows
            ? &(*display_rows)[static_cast<size_t>(row)]
            : nullptr;
    };
    const auto logical_row_at = [&](int row) {
        const EditableListDisplayRow* display =
            display_at(row);
        return display
            ? static_cast<int>(display->logical_row)
            : row;
    };
    const auto draft_at = [&](int logical_row) -> EditableListDraftRow* {
        return edit.rows_initialized
            ? &edit.rows[
                edit.visible_rows[
                    static_cast<size_t>(logical_row)]]
            : nullptr;
    };
    const auto cached_at = [&](int logical_row) -> const CachedTableRow* {
        return edit.rows_initialized
            ? nullptr
            : &cached_rows[static_cast<size_t>(logical_row)];
    };
    const auto source_file_at = [&](int logical_row) -> const std::string& {
        EditableListDraftRow* draft = draft_at(logical_row);
        return draft
            ? draft->target_source_file
            : cached_at(logical_row)->source.file_path;
    };
    const auto edit_id_at = [&](int logical_row) -> const std::string& {
        EditableListDraftRow* draft = draft_at(logical_row);
        return draft
            ? draft->target_edit_id
            : cached_at(logical_row)->edit_id;
    };
    const auto row_deleted_at = [&](int logical_row) {
        EditableListDraftRow* draft = draft_at(logical_row);
        return draft && draft->deleted;
    };

    ImGuiListClipper clipper;
    clipper.Begin(row_count);
    if (scroll_target_row >= 0 && scroll_target_row < row_count) {
        clipper.IncludeItemByIndex(scroll_target_row);
    }
    while (clipper.Step()) {
        for (int row_index = clipper.DisplayStart;
             row_index < clipper.DisplayEnd; ++row_index) {
            const int logical_row =
                logical_row_at(row_index);
            const EditableListDisplayRow* display_row =
                display_at(row_index);
            const bool secondary_row =
                display_row && display_row->secondary;
            EditableListDraftRow* draft =
                draft_at(logical_row);
            const CachedTableRow* cached =
                cached_at(logical_row);
            const std::string& target_edit_id =
                draft ? draft->target_edit_id : cached->edit_id;
            const std::string& resolved_path =
                draft ? draft->resolved_path : cached->open_path;
            const bool whole_draft_deleted =
                draft && draft->deleted;
            const bool draft_deleted =
                whole_draft_deleted ||
                (draft && secondary_row &&
                 draft->secondary_row_deleted);
            const bool row_editable =
                can_edit && !draft_deleted && !target_edit_id.empty();
            const bool is_preview_model =
                is_structure && model_preview_canvas_ &&
                model_preview_canvas_->has_model() &&
                resolved_path == model_preview_canvas_->model_path();
            const ImU32 text_color = ImGui::GetColorU32(
                is_preview_model ? preview_text_color : ImGui::GetStyleColorVec4(ImGuiCol_Text));
            const bool is_find_match =
                find_state &&
                static_cast<size_t>(logical_row) <
                    find_state->row_matches.size() &&
                find_state->row_matches[
                    static_cast<size_t>(logical_row)] != 0;
            const bool is_unused =
                find_state &&
                static_cast<size_t>(logical_row) <
                    find_state->unused_row_matches.size() &&
                find_state->unused_row_matches[
                    static_cast<size_t>(logical_row)] != 0;

            ImGui::TableNextRow();
            if (draft_deleted || row_is_pending_delete(target_edit_id)) {
                ImGui::TableSetBgColor(
                    ImGuiTableBgTarget_RowBg0, k_pending_delete_row_color);
            } else if (is_unused) {
                ImGui::TableSetBgColor(
                    ImGuiTableBgTarget_RowBg0, k_unused_structure_model_row_color);
            } else if (is_find_match) {
                ImGui::TableSetBgColor(
                    ImGuiTableBgTarget_RowBg0, k_find_match_row_color);
            } else if (row_has_pending_edit(target_edit_id) ||
                       (draft && editable_list_row_has_draft(*draft))) {
                ImGui::TableSetBgColor(
                    ImGuiTableBgTarget_RowBg0, k_pending_edit_row_color);
            }
            if (row_index == scroll_target_row && find_state) {
                ImGui::SetScrollHereY(0.0f);
                find_state->scroll_row = -1;
            }

            ImGui::PushID(row_index);
            for (int column = 0; column < column_count; ++column) {
                ImGui::TableSetColumnIndex(column);
                ImGui::PushID(column);
                const int display_field_index =
                    column - static_cast<int>(spec.cache_column_offset);
                const size_t editable_field_count = draft
                    ? draft->values.size()
                    : (cached->editable_field_count != 0
                        ? cached->editable_field_count
                        : spec.field_count);
                int field_index = display_field_index;
                if (display_row && display_field_index >= 0) {
                    if (display_field_index == 0) {
                        field_index =
                            secondary_row ? -1 : 0;
                    } else if (
                        static_cast<size_t>(
                            display_field_index - 1) <
                        display_row->structure_field_count) {
                        field_index = static_cast<int>(
                            display_row->structure_field_offset +
                            static_cast<size_t>(
                                display_field_index - 1));
                    } else {
                        field_index = -1;
                    }
                }
                const bool editable_cell =
                    field_index >= 0 &&
                    static_cast<size_t>(field_index) <
                        editable_field_count;
                const std::string sequence = display_row
                    ? display_row->sequence
                    : std::to_string(
                        static_cast<size_t>(
                            logical_row) + 1);
                static const std::string empty;
                const size_t cached_column = editable_cell
                    ? spec.cache_column_offset +
                        static_cast<size_t>(field_index)
                    : static_cast<size_t>(column);
                const std::string& display = draft
                    ? (editable_cell
                        ? draft->values[static_cast<size_t>(field_index)]
                        : (display_field_index < 0
                            ? sequence : empty))
                    : (display_field_index < 0
                        ? sequence
                        : (editable_cell &&
                           cached_column <
                            cached->cells.size()
                            ? cached->cells[cached_column]
                            : empty));
                const bool is_editing =
                    editable_cell && edit.editing_edit_id == target_edit_id &&
                    edit.editing_column == field_index;
                const bool is_selected =
                    edit.selected_row == logical_row &&
                    edit.selected_secondary_row ==
                        secondary_row &&
                    edit.selected_column == column;

                const auto render_context_menu = [&]() {
                    if (column == path_column && ImGui::IsItemHovered() &&
                        !resolved_path.empty()) {
                        ImGui::SetTooltip("%s", resolved_path.c_str());
                    }
                    touch_input::open_popup_on_last_item_long_press(
                        "##editable_list_context");
                    if (!ImGui::BeginPopupContextItem(
                            "##editable_list_context",
                            ImGuiPopupFlags_MouseButtonRight)) {
                        return;
                    }
                    bool has_top_action = false;
                    if (is_structure) {
                        ImGui::BeginDisabled(blank_ascii(resolved_path));
                        if (ImGui::MenuItem(tr("menu.preview_model").c_str())) {
                            preview_structure_model(resolved_path);
                        }
                        ImGui::EndDisabled();
                        has_top_action = true;
                    }
                    if (is_signal_aspect &&
                        display_field_index > 0) {
                        ImGui::BeginDisabled(blank_ascii(display));
                        if (ImGui::MenuItem(
                                tr("menu.find_in_structure_models").c_str())) {
                            find_structure_model_for_structure_key(display);
                        }
                        ImGui::EndDisabled();
                        has_top_action = true;
                    }
                    if (column == path_column) {
                        ImGui::BeginDisabled(!row_editable);
                        if (ImGui::MenuItem(tr("menu.select_file").c_str())) {
                            choose_editable_list_file(
                                edit, spec, logical_row);
                        }
                        ImGui::EndDisabled();
                        ImGui::BeginDisabled(blank_ascii(resolved_path));
                        if (ImGui::MenuItem(tr("menu.open_in_explorer").c_str())) {
                            open_parent_directory_in_explorer(resolved_path);
                        }
                        ImGui::EndDisabled();
                        has_top_action = true;
                    }
                    if (has_top_action) ImGui::Separator();

                    const bool move_up_available =
                        row_editable && logical_row > 0 &&
                        !edit_id_at(logical_row - 1).empty() &&
                        !row_deleted_at(logical_row - 1) &&
                        source_file_at(logical_row - 1) ==
                            source_file_at(logical_row);
                    const bool move_down_available =
                        row_editable &&
                        logical_row + 1 < logical_row_count &&
                        !edit_id_at(logical_row + 1).empty() &&
                        !row_deleted_at(logical_row + 1) &&
                        source_file_at(logical_row + 1) ==
                            source_file_at(logical_row);
                    ImGui::BeginDisabled(!move_up_available);
                    if (ImGui::MenuItem(
                            tr("context.editable_list.move_up").c_str())) {
                        move_editable_list_row(
                            edit, spec, logical_row, -1);
                    }
                    ImGui::EndDisabled();
                    ImGui::BeginDisabled(!move_down_available);
                    if (ImGui::MenuItem(
                            tr("context.editable_list.move_down").c_str())) {
                        move_editable_list_row(
                            edit, spec, logical_row, 1);
                    }
                    ImGui::EndDisabled();
                    ImGui::BeginDisabled(!row_editable || !editable_cell);
                    if (ImGui::MenuItem(
                            tr("context.editable_list.clear_cell").c_str())) {
                        if (clear_editable_list_cell(
                                edit, spec, logical_row,
                                field_index)) {
                            edit.selected_secondary_row =
                                secondary_row;
                            edit.selected_column = column;
                        }
                    }
                    ImGui::EndDisabled();
                    ImGui::BeginDisabled(!row_editable);
                    if (ImGui::MenuItem(
                            tr("context.editable_list.delete_row").c_str())) {
                        delete_editable_list_row(
                            edit, spec, logical_row);
                    }
                    ImGui::EndDisabled();
                    const size_t secondary_structure_field_count =
                        draft
                        ? draft->secondary_structure_field_count
                        : cached->secondary_structure_field_count;
                    if (is_signal_aspect &&
                        secondary_structure_field_count != 0) {
                        const bool delete_glare_available =
                            row_editable &&
                            (!draft ||
                             !draft->secondary_row_deleted);
                        ImGui::BeginDisabled(
                            !delete_glare_available);
                        if (ImGui::MenuItem(
                                tr("context.signal_aspect.delete_glare")
                                    .c_str())) {
                            delete_editable_list_secondary_row(
                                edit, spec, logical_row);
                        }
                        ImGui::EndDisabled();
                    }
                    ImGui::EndPopup();
                };

                if (row_editable && is_editing) {
                    if (edit.edit_buffer_fresh) {
                        ImGui::SetKeyboardFocusHere(0);
                        edit.edit_buffer_fresh = false;
                    }
                    ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x);
                    const bool returned = ImGui::InputText(
                        "##cell_edit", &edit.edit_buffer,
                        ImGuiInputTextFlags_EnterReturnsTrue |
                            ImGuiInputTextFlags_AutoSelectAll);
                    ImGui::PopItemWidth();
                    render_context_menu();
                    if (returned || ImGui::IsItemDeactivated()) {
                        commit_editable_list_active_edit(edit, spec);
                    }
                    ImGui::PopID();
                    continue;
                }

                const ImVec2 pos = ImGui::GetCursorScreenPos();
                const float cell_width =
                    std::max(1.0f, ImGui::GetContentRegionAvail().x);
                const float cell_height = ImGui::GetFrameHeight();
                ImGui::InvisibleButton(
                    "cell", ImVec2(cell_width, cell_height));
                const bool hovered = ImGui::IsItemHovered();
                if (is_selected) {
                    ImGui::GetWindowDrawList()->AddRect(
                        pos, ImVec2(pos.x + cell_width, pos.y + cell_height),
                        ImGui::GetColorU32(ImGuiCol_HeaderActive),
                        0.0f, 0, 1.5f);
                } else if (hovered) {
                    ImGui::GetWindowDrawList()->AddRect(
                        pos, ImVec2(pos.x + cell_width, pos.y + cell_height),
                        ImGui::GetColorU32(ImGuiCol_HeaderHovered));
                }
                if (!display.empty()) {
                    ImDrawList* draw_list = ImGui::GetWindowDrawList();
                    draw_list->PushClipRect(
                        pos, ImVec2(pos.x + cell_width, pos.y + cell_height), true);
                    draw_list->AddText(
                        ImVec2(pos.x + style.CellPadding.x,
                               pos.y + style.CellPadding.y),
                        text_color, display.c_str());
                    draw_list->PopClipRect();
                }
                if (ImGui::IsItemClicked(ImGuiMouseButton_Left) ||
                    ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
                    edit.selected_row = logical_row;
                    edit.selected_secondary_row =
                        secondary_row;
                    edit.selected_column = column;
                }
                render_context_menu();
                if (row_editable && editable_cell && hovered &&
                    ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                    commit_editable_list_active_edit(edit, spec);
                    if (initialize_editable_list_draft_rows(edit, spec)) {
                        EditableListDraftRow& active =
                            edit.rows[edit.visible_rows[
                                static_cast<size_t>(logical_row)]];
                        edit.editing_column = field_index;
                        edit.editing_edit_id = active.target_edit_id;
                        edit.editing_baseline =
                            active.values[static_cast<size_t>(field_index)];
                        edit.edit_buffer = edit.editing_baseline;
                        edit.edit_buffer_fresh = true;
                    }
                }
                ImGui::PopID();
            }
            ImGui::PopID();
        }
    }
    ImGui::EndTable();
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
    render_resource_list_source(
        model_.resource_list_sources[static_cast<size_t>(ResourceListKind::Station)],
        tr("label.source_path"), tr("menu.open_in_explorer"));
    ensure_table_cache();
    auto render_station_table = [&](const char* table_id,
                                    const TableColumnDef* columns,
                                    int column_count,
                                    const std::vector<CachedTableRow>& rows,
                                    bool allow_station_put_properties) {
        const int row_count = static_cast<int>(rows.size());
        ImVec2 table_size(0.0f, scroll_x_table_height_for_rows(row_count));
        if (!ImGui::BeginTable(table_id, column_count,
                               ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                               ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollX,
                               table_size)) {
            return;
        }
        for (int i = 0; i < column_count; ++i) {
            ImGui::TableSetupColumn(columns[i].header, ImGuiTableColumnFlags_WidthFixed,
                                    columns[i].width);
        }
        setup_fixed_table_header();
        ImGui::TableHeadersRow();
        ImGuiListClipper clipper;
        clipper.Begin(row_count);
        while (clipper.Step()) {
            for (int row_index = clipper.DisplayStart; row_index < clipper.DisplayEnd; ++row_index) {
                const CachedTableRow& row = rows[static_cast<size_t>(row_index)];
                ImGui::TableNextRow();
                if (allow_station_put_properties && row_is_pending_delete(row.edit_id)) {
                    ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, k_pending_delete_row_color);
                } else if (allow_station_put_properties && row_has_pending_edit(row.edit_id)) {
                    ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, k_pending_edit_row_color);
                }
                ImGui::PushID(row_index);
                for (int i = 0; i < column_count; ++i) {
                    ImGui::TableSetColumnIndex(i);
                    const std::string& value = row.cells[static_cast<size_t>(i)];
                    if (value.empty()) continue;
                    if (!allow_station_put_properties) {
                        ImGui::TextUnformatted(value.c_str());
                        continue;
                    }
                    ImGui::PushID(i);
                    const bool edit_enabled = edit_actions_available() && !row.edit_id.empty();
                    const TextCellContextAction action = render_text_cell_with_context_actions(
                        value,
                        tr("dialog.element_properties"), edit_enabled,
                        {}, false,
                        tr("button.delete"), edit_enabled);
                    if (action == TextCellContextAction::Primary) {
                        request_element_inspector(row.edit_id, "station.put");
                    } else if (action == TextCellContextAction::Tertiary) {
                        request_element_delete(row.edit_id, "station.put");
                    }
                    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                        request_element_inspector(row.edit_id, "station.put");
                    }
                    ImGui::PopID();
                }
                ImGui::PopID();
            }
        }
        ImGui::EndTable();
    };

    ImGui::TextUnformatted(tr("frame.station_positions").c_str());
    render_station_table("station_positions", k_station_position_columns,
                         IM_ARRAYSIZE(k_station_position_columns),
                         table_cache_.station_position_rows, true);
    ImGui::Separator();
    ImGui::TextUnformatted(tr("frame.station_definitions").c_str());
    ImGui::SameLine();
    ImGui::BeginDisabled(!edit_actions_available() || !has_station_definition_drafts());
    if (ImGui::Button(tr("button.apply").c_str())) apply_station_definition_drafts();
    ImGui::EndDisabled();
    {
        const TableColumnDef* columns = k_station_definition_columns;
        const int column_count = IM_ARRAYSIZE(k_station_definition_columns);
        const std::vector<CachedTableRow>& rows = table_cache_.station_definition_rows;
        StationDefinitionEditState& edit = station_definition_edit_;
        const int row_count = edit.rows_initialized
            ? static_cast<int>(edit.visible_rows.size())
            : static_cast<int>(rows.size());
        const bool can_edit = edit_actions_available();
        ImVec2 table_size(0.0f, scroll_x_table_height_for_rows(row_count));
        if (ImGui::BeginTable("station_definitions", column_count,
                              ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                              ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollX,
                              table_size)) {
            for (int i = 0; i < column_count; ++i) {
                ImGui::TableSetupColumn(columns[i].header, ImGuiTableColumnFlags_WidthFixed,
                                        columns[i].width);
            }
            setup_fixed_table_header();
            ImGui::TableHeadersRow();
            const ImGuiStyle& style = ImGui::GetStyle();
            const auto source_file_at = [&](int visible_row) -> const std::string& {
                if (edit.rows_initialized) {
                    return edit.rows[edit.visible_rows[static_cast<size_t>(visible_row)]]
                        .target_source_file;
                }
                return rows[static_cast<size_t>(visible_row)].source.file_path;
            };
            const auto edit_id_at = [&](int visible_row) -> const std::string& {
                if (edit.rows_initialized) {
                    return edit.rows[edit.visible_rows[static_cast<size_t>(visible_row)]]
                        .target_edit_id;
                }
                return rows[static_cast<size_t>(visible_row)].edit_id;
            };
            const auto row_deleted_at = [&](int visible_row) {
                return edit.rows_initialized &&
                    edit.rows[edit.visible_rows[static_cast<size_t>(visible_row)]]
                        .deleted;
            };
            const auto render_context_menu = [&](int visible_row, int column,
                                                 bool actions_available) {
                const bool move_up_available = actions_available && visible_row > 0 &&
                    !edit_id_at(visible_row - 1).empty() &&
                    !row_deleted_at(visible_row - 1) &&
                    source_file_at(visible_row - 1) == source_file_at(visible_row);
                const bool move_down_available = actions_available &&
                    visible_row + 1 < row_count &&
                    !edit_id_at(visible_row + 1).empty() &&
                    !row_deleted_at(visible_row + 1) &&
                    source_file_at(visible_row + 1) == source_file_at(visible_row);
                if (!ImGui::BeginPopupContextItem("##station_definition_context")) return;
                ImGui::BeginDisabled(!move_up_available);
                if (ImGui::MenuItem(tr("context.station_list.move_up").c_str())) {
                    move_station_definition_row(visible_row, -1);
                }
                ImGui::EndDisabled();
                ImGui::BeginDisabled(!move_down_available);
                if (ImGui::MenuItem(tr("context.station_list.move_down").c_str())) {
                    move_station_definition_row(visible_row, 1);
                }
                ImGui::EndDisabled();
                ImGui::BeginDisabled(!actions_available);
                if (ImGui::MenuItem(tr("context.station_list.clear_cell").c_str())) {
                    clear_station_definition_cell(visible_row, column);
                }
                if (ImGui::MenuItem(tr("context.station_list.delete_row").c_str())) {
                    delete_station_definition_row(visible_row);
                }
                ImGui::EndDisabled();
                ImGui::EndPopup();
            };
            ImGuiListClipper clipper;
            clipper.Begin(row_count);
            while (clipper.Step()) {
                for (int row_index = clipper.DisplayStart; row_index < clipper.DisplayEnd; ++row_index) {
                    const CachedTableRow* cached_row = edit.rows_initialized ? nullptr :
                        &rows[static_cast<size_t>(row_index)];
                    StationDefinitionDraftRow* draft_row = edit.rows_initialized
                        ? &edit.rows[edit.visible_rows[static_cast<size_t>(row_index)]]
                        : nullptr;
                    const std::string& target_edit_id = draft_row
                        ? draft_row->target_edit_id : cached_row->edit_id;
                    const bool draft_deleted = draft_row && draft_row->deleted;
                    const bool row_editable =
                        can_edit && !draft_deleted && !target_edit_id.empty();
                    ImGui::TableNextRow();
                    if (draft_deleted || row_is_pending_delete(target_edit_id)) {
                        ImGui::TableSetBgColor(
                            ImGuiTableBgTarget_RowBg0, k_pending_delete_row_color);
                    } else if (row_has_pending_edit(target_edit_id) ||
                        (draft_row && station_definition_row_has_draft(*draft_row))) {
                        ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, k_pending_edit_row_color);
                    }
                    ImGui::PushID(row_index);
                    for (int col = 0; col < column_count; ++col) {
                        ImGui::TableSetColumnIndex(col);
                        ImGui::PushID(col);
                        const std::string& display = draft_row
                            ? draft_row->values[static_cast<size_t>(col)]
                            : cached_row->cells[static_cast<size_t>(col)];
                        const bool is_editing =
                            edit.editing_edit_id == target_edit_id &&
                            edit.editing_column == col;
                        const bool is_selected = (edit.selected_row == row_index && edit.selected_column == col);

                        if (row_editable && is_editing) {
                            if (edit.edit_buffer_fresh) {
                                ImGui::SetKeyboardFocusHere(0);
                                edit.edit_buffer_fresh = false;
                            }
                            ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x);
                            const bool returned = ImGui::InputText(
                                "##cell_edit", &edit.edit_buffer,
                                ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
                            ImGui::PopItemWidth();
                            render_context_menu(row_index, col, row_editable);
                            if (returned || ImGui::IsItemDeactivated()) {
                                commit_station_definition_active_edit();
                            }
                            ImGui::PopID();
                            continue;
                        }

                        ImVec2 pos = ImGui::GetCursorScreenPos();
                        float cell_w = ImGui::GetContentRegionAvail().x;
                        float cell_h = ImGui::GetFrameHeight();
                        ImGui::InvisibleButton("cell", ImVec2(cell_w, cell_h));
                        const bool hovered = ImGui::IsItemHovered();
                        if (is_selected) {
                            ImGui::GetWindowDrawList()->AddRect(
                                pos, ImVec2(pos.x + cell_w, pos.y + cell_h),
                                ImGui::GetColorU32(ImGuiCol_HeaderActive), 0.0f, 0, 1.5f);
                        } else if (hovered) {
                            ImGui::GetWindowDrawList()->AddRect(
                                pos, ImVec2(pos.x + cell_w, pos.y + cell_h),
                                ImGui::GetColorU32(ImGuiCol_HeaderHovered), 0.0f, 0, 1.0f);
                        }
                        if (!display.empty()) {
                            ImDrawList* draw_list = ImGui::GetWindowDrawList();
                            draw_list->PushClipRect(
                                pos, ImVec2(pos.x + cell_w, pos.y + cell_h), true);
                            draw_list->AddText(
                                ImVec2(pos.x + style.CellPadding.x, pos.y + style.CellPadding.y),
                                ImGui::GetColorU32(ImGuiCol_Text), display.c_str());
                            draw_list->PopClipRect();
                        }
                        if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
                            edit.selected_row = row_index;
                            edit.selected_column = col;
                        }
                        render_context_menu(row_index, col, row_editable);
                        if (row_editable) {
                            if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
                                edit.selected_row = row_index;
                                edit.selected_column = col;
                            }
                            if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                                commit_station_definition_active_edit();
                                if (!initialize_station_definition_draft_rows()) {
                                    ImGui::PopID();
                                    continue;
                                }
                                StationDefinitionDraftRow& active_row =
                                    edit.rows[edit.visible_rows[static_cast<size_t>(row_index)]];
                                edit.editing_column = col;
                                edit.editing_edit_id = active_row.target_edit_id;
                                edit.editing_baseline =
                                    active_row.values[static_cast<size_t>(col)];
                                edit.edit_buffer = edit.editing_baseline;
                                edit.edit_buffer_fresh = true;
                            }
                        }
                        ImGui::PopID();
                    }
                    ImGui::PopID();
                }
            }
            ImGui::EndTable();
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

void App::render_structure_models_window() {
    if (!show_structure_models_window_) return;
    if (dock_right_id_) ImGui::SetNextWindowDockID(dock_right_id_, ImGuiCond_FirstUseEver);
    std::string title = tr("frame.structure_models") + "###StructureModels";
    if (!ImGui::Begin(title.c_str(), &show_structure_models_window_)) {
        ImGui::End();
        return;
    }
    render_resource_list_source(
        model_.resource_list_sources[static_cast<size_t>(ResourceListKind::Structure)],
        tr("label.source_path"), tr("menu.open_in_explorer"));
    ensure_table_cache();

    render_table_find_panel(
        structure_model_find_,
        "structure_model",
        tr("button.find"),
        tr("find.partial_match"),
        tr("find.exact_match"),
        tr("button.find_unused_structure_models"),
        [this]() { run_structure_model_find(); },
        [this]() { run_unused_structure_model_search(); },
        [this](int delta) { step_structure_model_find(delta); },
        [this]() { return structure_model_find_status_text(); });

    ImGui::BeginDisabled(
        !edit_actions_available() ||
        !has_editable_list_drafts(
            structure_model_edit_, k_structure_model_edit_spec));
    if (ImGui::Button(tr("button.apply").c_str())) {
        apply_editable_list_drafts(
            structure_model_edit_, k_structure_model_edit_spec);
    }
    ImGui::EndDisabled();
    render_editable_list_table(
        "structure_models", k_structure_model_columns,
        IM_ARRAYSIZE(k_structure_model_columns),
        table_cache_.structure_model_rows, structure_model_edit_,
        k_structure_model_edit_spec,
        table_cache_.structure_model_file_path_width, 0.0f,
        &structure_model_find_);
    ImGui::End();
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

void App::render_sound_file_find_panel(bool is_3d) {
    TableFindState& state = is_3d ? sound_3d_file_find_ : sound_file_find_;

    render_table_find_panel(
        state,
        is_3d ? "sound_3d_file" : "sound_file",
        tr("button.find"),
        tr("find.partial_match"),
        tr("find.exact_match"),
        tr(is_3d ? "button.find_unused_sound_3d_files" : "button.find_unused_sound_files"),
        [this, is_3d]() { run_sound_file_find(is_3d); },
        [this, is_3d]() { run_unused_sound_file_search(is_3d); },
        [this, is_3d](int delta) { step_sound_file_find(is_3d, delta); },
        [this, is_3d]() { return sound_file_find_status_text(is_3d); });
}

void App::render_sound_list_window() {
    if (!show_sound_list_window_) return;
    if (dock_right_id_) ImGui::SetNextWindowDockID(dock_right_id_, ImGuiCond_FirstUseEver);
    std::string title = tr("frame.sound_list") + "###SoundList";
    if (!ImGui::Begin(title.c_str(), &show_sound_list_window_)) {
        ImGui::End();
        return;
    }
    if (!has_model_) {
        ImGui::TextDisabled("-");
        ImGui::End();
        return;
    }
    render_resource_list_source(
        model_.resource_list_sources[static_cast<size_t>(ResourceListKind::Sound)],
        tr("label.source_path"), tr("menu.open_in_explorer"));
    ensure_table_cache();
    render_sound_file_find_panel(false);
    ImGui::BeginDisabled(
        !edit_actions_available() ||
        !has_editable_list_drafts(sound_list_edit_, k_sound_list_edit_spec));
    if (ImGui::Button(tr("button.apply").c_str())) {
        apply_editable_list_drafts(sound_list_edit_, k_sound_list_edit_spec);
    }
    ImGui::EndDisabled();
    render_editable_list_table(
        "sound_list", k_sound_list_columns, IM_ARRAYSIZE(k_sound_list_columns),
        table_cache_.sound_list_rows, sound_list_edit_, k_sound_list_edit_spec,
        table_cache_.sound_list_file_path_width,
        table_cache_.sound_list_buffer_count_width, &sound_file_find_);
    ImGui::End();
}

void App::render_sound_3d_list_window() {
    if (!show_sound_3d_list_window_) return;
    if (dock_right_id_) ImGui::SetNextWindowDockID(dock_right_id_, ImGuiCond_FirstUseEver);
    std::string title = tr("frame.sound_3d_list") + "###Sound3DList";
    if (!ImGui::Begin(title.c_str(), &show_sound_3d_list_window_)) {
        ImGui::End();
        return;
    }
    if (!has_model_) {
        ImGui::TextDisabled("-");
        ImGui::End();
        return;
    }
    render_resource_list_source(
        model_.resource_list_sources[static_cast<size_t>(ResourceListKind::Sound3D)],
        tr("label.source_path"), tr("menu.open_in_explorer"));
    ensure_table_cache();
    render_sound_file_find_panel(true);
    ImGui::BeginDisabled(
        !edit_actions_available() ||
        !has_editable_list_drafts(
            sound_3d_list_edit_, k_sound_3d_list_edit_spec));
    if (ImGui::Button(tr("button.apply").c_str())) {
        apply_editable_list_drafts(
            sound_3d_list_edit_, k_sound_3d_list_edit_spec);
    }
    ImGui::EndDisabled();
    render_editable_list_table(
        "sound_3d_list", k_sound_list_columns,
        IM_ARRAYSIZE(k_sound_list_columns),
        table_cache_.sound_3d_list_rows, sound_3d_list_edit_,
        k_sound_3d_list_edit_spec,
        table_cache_.sound_3d_list_file_path_width,
        table_cache_.sound_3d_list_buffer_count_width, &sound_3d_file_find_);
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

void App::render_signal_aspects_window() {
    if (!show_signal_aspects_window_) return;
    if (dock_right_id_) ImGui::SetNextWindowDockID(dock_right_id_, ImGuiCond_FirstUseEver);
    if (focus_signal_aspects_next_) ImGui::SetNextWindowFocus();
    std::string title = tr("frame.signal_aspects") + "###SignalAspects";
    if (!ImGui::Begin(title.c_str(), &show_signal_aspects_window_)) {
        focus_signal_aspects_next_ = false;
        ImGui::End();
        return;
    }
    if (!has_model_) {
        ImGui::TextDisabled("-");
        focus_signal_aspects_next_ = false;
        ImGui::End();
        return;
    }
    render_resource_list_source(
        model_.resource_list_sources[
            static_cast<size_t>(ResourceListKind::Signal)],
        tr("label.source_path"), tr("menu.open_in_explorer"));
    ensure_table_cache();
    const bool stale_find_results = signal_aspect_find_.has_run &&
        signal_aspect_find_.committed != signal_aspect_find_.query;
    if (stale_find_results) reset_signal_aspect_find_results();

    render_table_find_panel(
        signal_aspect_find_,
        "signal_aspect",
        tr("button.find"),
        tr("find.partial_match"),
        tr("find.exact_match"),
        tr("button.find_unused_signal_aspects"),
        [this]() { run_signal_aspect_find(); },
        [this]() { run_unused_signal_aspect_search(); },
        [this](int delta) { step_signal_aspect_find(delta); },
        [this]() { return signal_aspect_find_status_text(); });

    ImGui::BeginDisabled(
        !edit_actions_available() ||
        !has_editable_list_drafts(
            signal_aspect_edit_, k_signal_aspect_edit_spec));
    if (ImGui::Button(tr("button.apply").c_str())) {
        apply_editable_list_drafts(
            signal_aspect_edit_, k_signal_aspect_edit_spec);
    }
    ImGui::EndDisabled();
    render_editable_list_table(
        "signal_aspects", nullptr,
        static_cast<int>(
            table_cache_.signal_aspect_column_headers.size()),
        table_cache_.signal_aspect_rows, signal_aspect_edit_,
        k_signal_aspect_edit_spec, 0.0f, 0.0f,
        &signal_aspect_find_,
        &table_cache_.signal_aspect_column_headers,
        &table_cache_.signal_aspect_column_widths,
        &table_cache_.signal_aspect_display_rows);
    focus_signal_aspects_next_ = false;
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
                            {}, false);
                        if (action == TextCellContextAction::Primary) {
                            locate_signal_row_on_plan(marker_index);
                        } else if (action == TextCellContextAction::Secondary) {
                            locate_signal_row_in_scene_preview(marker_index);
                        } else if (action == TextCellContextAction::Tertiary) {
                            request_element_inspector(row.edit_id, "signal.put");
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
                                    bool has_markers) {
        ImGui::TextUnformatted(heading.c_str());
        const int column_count = static_cast<int>(3 + value_columns);
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
        for (size_t i = 0; i < value_columns; ++i) {
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
                    const TextCellContextAction action =
                        render_marker_text_cell_with_context(
                            value, tr("menu.locate_on_plan"), true,
                            tr("menu.locate_in_scene_preview"),
                            has_markers && can_locate_scene);
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
        table_cache_.section_begin_value_columns, "signal", true);
    ImGui::Separator();
    render_section_table(
        "section_speed_limits", tr("frame.section_speed_limits"),
        table_cache_.section_speed_limit_rows,
        table_cache_.section_speed_limit_value_columns, "v", false);
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
    if (ImGui::BeginTable("backgrounds", IM_ARRAYSIZE(k_background_columns),
                          ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                          ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollX |
                          ImGuiTableFlags_ScrollY)) {
        std::string file_name_header = tr("column.file_name");
        for (int i = 0; i < IM_ARRAYSIZE(k_background_columns); ++i) {
            float width = k_background_columns[i].width;
            if (i == k_background_distance_column) width = table_cache_.background_distance_width;
            if (i == k_background_file_path_column) width = table_cache_.background_file_path_width;
            const char* header = i == k_background_file_path_column
                ? file_name_header.c_str()
                : k_background_columns[i].header;
            ImGui::TableSetupColumn(header, width > 0.0f ? ImGuiTableColumnFlags_WidthFixed : 0, width);
        }
        setup_fixed_table_header();
        ImGui::TableHeadersRow();
        ImGuiListClipper clipper;
        const int row_count = static_cast<int>(table_cache_.background_rows.size());
        if (background_list_scroll_row_ >= row_count) background_list_scroll_row_ = -1;
        if (background_list_highlight_row_ >= row_count) background_list_highlight_row_ = -1;
        const int scroll_target_row = background_list_scroll_row_;
        clipper.Begin(row_count);
        if (scroll_target_row >= 0 && scroll_target_row < row_count) {
            clipper.IncludeItemByIndex(scroll_target_row);
        }
        const ImU32 highlight_color = table_row_highlight_color(theme_color_);
        const bool can_locate_scene_preview = can_locate_scene_preview_row();
        while (clipper.Step()) {
            for (int row_index = clipper.DisplayStart; row_index < clipper.DisplayEnd; ++row_index) {
                const CachedTableRow& row = table_cache_.background_rows[static_cast<size_t>(row_index)];
                ImGui::TableNextRow();
                if (row_index == background_list_highlight_row_) {
                    ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, highlight_color);
                }
                if (row_index == scroll_target_row) {
                    ImGui::SetScrollHereY(0.5f);
                    background_list_scroll_row_ = -1;
                }
                ImGui::PushID(row_index);
                for (int i = 0; i < IM_ARRAYSIZE(k_background_columns); ++i) {
                    ImGui::TableSetColumnIndex(i);
                    const std::string& value = row.cells[static_cast<size_t>(i)];
                    if (i == k_background_distance_column) {
                        size_t marker_index = static_cast<size_t>(row_index);
                        bool can_locate = marker_index < background_marker_cache_.size() &&
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
                        continue;
                    }
                    if (value.empty()) continue;
                    if (i == k_background_structure_key_column) {
                        ImGui::PushID("background_structure_key");
                        if (render_text_cell_with_context(value, tr("menu.find_in_structure_models"), !blank_ascii(value))) {
                            find_structure_model_for_structure_key(value);
                        }
                        ImGui::PopID();
                    } else if (i == k_background_file_path_column) {
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
    if (ImGui::BeginTable("adhesions", IM_ARRAYSIZE(k_adhesion_columns),
                          ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                          ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollX |
                          ImGuiTableFlags_ScrollY)) {
        std::string file_name_header = tr("column.file_name");
        for (int i = 0; i < IM_ARRAYSIZE(k_adhesion_columns); ++i) {
            float width = k_adhesion_columns[i].width;
            if (i == k_adhesion_distance_column) width = table_cache_.adhesion_distance_width;
            if (i == k_adhesion_file_path_column) width = table_cache_.adhesion_file_path_width;
            const char* header = i == k_adhesion_file_path_column
                ? file_name_header.c_str()
                : k_adhesion_columns[i].header;
            ImGui::TableSetupColumn(header, width > 0.0f ? ImGuiTableColumnFlags_WidthFixed : 0, width);
        }
        setup_fixed_table_header();
        ImGui::TableHeadersRow();
        ImGuiListClipper clipper;
        const int row_count = static_cast<int>(table_cache_.adhesion_rows.size());
        if (adhesion_list_scroll_row_ >= row_count) adhesion_list_scroll_row_ = -1;
        if (adhesion_list_highlight_row_ >= row_count) adhesion_list_highlight_row_ = -1;
        const int scroll_target_row = adhesion_list_scroll_row_;
        clipper.Begin(row_count);
        if (scroll_target_row >= 0 && scroll_target_row < row_count) {
            clipper.IncludeItemByIndex(scroll_target_row);
        }
        const ImU32 highlight_color = table_row_highlight_color(theme_color_);
        const bool can_locate_scene_preview = can_locate_scene_preview_row();
        while (clipper.Step()) {
            for (int row_index = clipper.DisplayStart; row_index < clipper.DisplayEnd; ++row_index) {
                const CachedTableRow& row = table_cache_.adhesion_rows[static_cast<size_t>(row_index)];
                ImGui::TableNextRow();
                if (row_index == adhesion_list_highlight_row_) {
                    ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, highlight_color);
                }
                if (row_index == scroll_target_row) {
                    ImGui::SetScrollHereY(0.5f);
                    adhesion_list_scroll_row_ = -1;
                }
                ImGui::PushID(row_index);
                for (int i = 0; i < IM_ARRAYSIZE(k_adhesion_columns); ++i) {
                    ImGui::TableSetColumnIndex(i);
                    const std::string& value = row.cells[static_cast<size_t>(i)];
                    if (i == k_adhesion_distance_column) {
                        size_t marker_index = static_cast<size_t>(row_index);
                        bool can_locate = marker_index < adhesion_marker_cache_.size() &&
                            adhesion_marker_cache_[marker_index].has_value();
                        const bool can_edit = edit_actions_available() &&
                            marker_index < model_.adhesions.size() &&
                            !model_.adhesions[marker_index].edit_id.empty();
                        const TextCellContextAction action = render_marker_text_cell_with_context(
                            value,
                            tr("menu.locate_on_plan"), can_locate,
                            tr("menu.locate_in_scene_preview"), can_locate_scene_preview,
                            tr("dialog.element_properties"), can_edit,
                            tr("button.delete"), can_edit);
                        if (action == TextCellContextAction::Primary) {
                            locate_adhesion_row_on_plan(marker_index);
                        } else if (action == TextCellContextAction::Secondary) {
                            locate_scene_marker_row_in_scene_preview(
                                Canvas3DSceneMarkerListKind::Adhesion, marker_index);
                        } else if (action == TextCellContextAction::Tertiary) {
                            request_element_inspector(model_.adhesions[marker_index].edit_id,
                                                      "adhesion.change");
                        } else if (action == TextCellContextAction::Quaternary) {
                            request_element_delete(model_.adhesions[marker_index].edit_id,
                                                   "adhesion.change");
                        }
                        continue;
                    }
                    if (value.empty()) continue;
                    if (i == k_adhesion_file_path_column) {
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
    if (ImGui::BeginTable("cab_illuminance", IM_ARRAYSIZE(k_cab_illuminance_columns),
                          ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                          ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollX |
                          ImGuiTableFlags_ScrollY)) {
        std::string file_name_header = tr("column.file_name");
        for (int i = 0; i < IM_ARRAYSIZE(k_cab_illuminance_columns); ++i) {
            float width = k_cab_illuminance_columns[i].width;
            if (i == k_cab_illuminance_distance_column) width = table_cache_.cab_illuminance_distance_width;
            if (i == k_cab_illuminance_file_path_column) width = table_cache_.cab_illuminance_file_path_width;
            const char* header = i == k_cab_illuminance_file_path_column
                ? file_name_header.c_str()
                : k_cab_illuminance_columns[i].header;
            ImGui::TableSetupColumn(header, width > 0.0f ? ImGuiTableColumnFlags_WidthFixed : 0, width);
        }
        setup_fixed_table_header();
        ImGui::TableHeadersRow();
        ImGuiListClipper clipper;
        const int row_count = static_cast<int>(table_cache_.cab_illuminance_rows.size());
        if (cab_illuminance_list_scroll_row_ >= row_count) cab_illuminance_list_scroll_row_ = -1;
        if (cab_illuminance_list_highlight_row_ >= row_count) cab_illuminance_list_highlight_row_ = -1;
        const int scroll_target_row = cab_illuminance_list_scroll_row_;
        clipper.Begin(row_count);
        if (scroll_target_row >= 0 && scroll_target_row < row_count) {
            clipper.IncludeItemByIndex(scroll_target_row);
        }
        const ImU32 highlight_color = table_row_highlight_color(theme_color_);
        const bool can_locate_scene_preview = can_locate_scene_preview_row();
        while (clipper.Step()) {
            for (int row_index = clipper.DisplayStart; row_index < clipper.DisplayEnd; ++row_index) {
                const CachedTableRow& row = table_cache_.cab_illuminance_rows[static_cast<size_t>(row_index)];
                ImGui::TableNextRow();
                if (row_index == cab_illuminance_list_highlight_row_) {
                    ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, highlight_color);
                }
                if (row_index == scroll_target_row) {
                    ImGui::SetScrollHereY(0.5f);
                    cab_illuminance_list_scroll_row_ = -1;
                }
                ImGui::PushID(row_index);
                for (int i = 0; i < IM_ARRAYSIZE(k_cab_illuminance_columns); ++i) {
                    ImGui::TableSetColumnIndex(i);
                    const std::string& value = row.cells[static_cast<size_t>(i)];
                    if (i == k_cab_illuminance_distance_column) {
                        size_t marker_index = static_cast<size_t>(row_index);
                        bool can_locate = marker_index < cab_illuminance_marker_cache_.size() &&
                            cab_illuminance_marker_cache_[marker_index].has_value();
                        const bool can_edit = edit_actions_available() &&
                            marker_index < model_.cab_illuminance.size() &&
                            !model_.cab_illuminance[marker_index].edit_id.empty();
                        const TextCellContextAction action = render_marker_text_cell_with_context(
                            value,
                            tr("menu.locate_on_plan"), can_locate,
                            tr("menu.locate_in_scene_preview"), can_locate_scene_preview,
                            tr("dialog.element_properties"), can_edit,
                            tr("button.delete"), can_edit);
                        if (action == TextCellContextAction::Primary) {
                            locate_cab_illuminance_row_on_plan(marker_index);
                        } else if (action == TextCellContextAction::Secondary) {
                            locate_scene_marker_row_in_scene_preview(
                                Canvas3DSceneMarkerListKind::CabIlluminance, marker_index);
                        } else if (action == TextCellContextAction::Tertiary) {
                            request_element_inspector(model_.cab_illuminance[marker_index].edit_id,
                                                      "cabIlluminance.change");
                        } else if (action == TextCellContextAction::Quaternary) {
                            request_element_delete(model_.cab_illuminance[marker_index].edit_id,
                                                   "cabIlluminance.change");
                        }
                        continue;
                    }
                    if (value.empty()) continue;
                    if (i == k_cab_illuminance_file_path_column) {
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
    if (ImGui::BeginTable("fogs", IM_ARRAYSIZE(k_fog_columns),
                          ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                          ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollX |
                          ImGuiTableFlags_ScrollY)) {
        std::string file_name_header = tr("column.file_name");
        for (int i = 0; i < IM_ARRAYSIZE(k_fog_columns); ++i) {
            float width = k_fog_columns[i].width;
            if (i == k_fog_distance_column) width = table_cache_.fog_distance_width;
            if (i == k_fog_file_path_column) width = table_cache_.fog_file_path_width;
            const char* header = i == k_fog_file_path_column
                ? file_name_header.c_str()
                : k_fog_columns[i].header;
            ImGui::TableSetupColumn(header, width > 0.0f ? ImGuiTableColumnFlags_WidthFixed : 0, width);
        }
        setup_fixed_table_header();
        ImGui::TableHeadersRow();
        ImGuiListClipper clipper;
        const int row_count = static_cast<int>(table_cache_.fog_rows.size());
        if (fog_list_scroll_row_ >= row_count) fog_list_scroll_row_ = -1;
        if (fog_list_highlight_row_ >= row_count) fog_list_highlight_row_ = -1;
        const int scroll_target_row = fog_list_scroll_row_;
        clipper.Begin(row_count);
        if (scroll_target_row >= 0 && scroll_target_row < row_count) {
            clipper.IncludeItemByIndex(scroll_target_row);
        }
        const ImU32 highlight_color = table_row_highlight_color(theme_color_);
        const bool can_locate_scene_preview = can_locate_scene_preview_row();
        while (clipper.Step()) {
            for (int row_index = clipper.DisplayStart; row_index < clipper.DisplayEnd; ++row_index) {
                const CachedTableRow& row = table_cache_.fog_rows[static_cast<size_t>(row_index)];
                ImGui::TableNextRow();
                if (row_index == fog_list_highlight_row_) {
                    ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, highlight_color);
                }
                if (row_index == scroll_target_row) {
                    ImGui::SetScrollHereY(0.5f);
                    fog_list_scroll_row_ = -1;
                }
                ImGui::PushID(row_index);
                for (int i = 0; i < IM_ARRAYSIZE(k_fog_columns); ++i) {
                    ImGui::TableSetColumnIndex(i);
                    const std::string& value = row.cells[static_cast<size_t>(i)];
                    if (i == k_fog_distance_column) {
                        size_t marker_index = static_cast<size_t>(row_index);
                        bool can_locate = marker_index < fog_marker_cache_.size() &&
                            fog_marker_cache_[marker_index].has_value();
                        const bool can_edit = edit_actions_available() &&
                            marker_index < model_.fogs.size() &&
                            !model_.fogs[marker_index].edit_id.empty();
                        const TextCellContextAction action = render_marker_text_cell_with_context(
                            value,
                            tr("menu.locate_on_plan"), can_locate,
                            tr("menu.locate_in_scene_preview"), can_locate_scene_preview,
                            tr("dialog.element_properties"), can_edit,
                            tr("button.delete"), can_edit);
                        if (action == TextCellContextAction::Primary) {
                            locate_fog_row_on_plan(marker_index);
                        } else if (action == TextCellContextAction::Secondary) {
                            locate_scene_marker_row_in_scene_preview(
                                Canvas3DSceneMarkerListKind::Fog, marker_index);
                        } else if (action == TextCellContextAction::Tertiary) {
                            request_element_inspector(model_.fogs[marker_index].edit_id,
                                                      "fog.change");
                        } else if (action == TextCellContextAction::Quaternary) {
                            request_element_delete(model_.fogs[marker_index].edit_id,
                                                   "fog.change");
                        }
                        continue;
                    }
                    if (value.empty()) continue;
                    if (i == k_fog_file_path_column) {
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
    focus_fogs_next_ = false;
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
