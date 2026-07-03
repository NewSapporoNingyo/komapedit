/*
 * Copyright (c) 2026 Sapporo_ningyo
 *
 * Licensed under Apache License 2.0; see LICENSE and NOTICE.
 * The GUI uses Dear ImGui and ImPlot; see THIRD_PARTY_NOTICES.md.
 */

#pragma execution_character_set("utf-8")

#include "kme.h"
#include "touch_input.h"

#include "canvas3D.h"
#include "imgui.h"

#include <windows.h>
#include <shellapi.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
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

float scroll_x_table_height_for_rows(int row_count) {
    const ImGuiStyle& style = ImGui::GetStyle();
    const float text_row_height = ImGui::GetTextLineHeight() + style.CellPadding.y * 2.0f;
    const float row_height = std::ceil(std::max(text_row_height, ImGui::GetFrameHeight()));
    const int rows_with_header = std::max(0, row_count) + 1;
    return row_height * static_cast<float>(rows_with_header) +
        style.ScrollbarSize + style.CellPadding.y * 3.0f;
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
    touch_input::open_popup_on_last_item_long_press("text_cell_context");
    if (ImGui::BeginPopupContextItem("text_cell_context", ImGuiPopupFlags_MouseButtonRight)) {
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
};

TextCellContextAction render_text_cell_with_context_actions(const std::string& display_text,
                                                           const std::string& primary_label,
                                                           bool primary_enabled,
                                                           const std::string& secondary_label,
                                                           bool secondary_enabled) {
    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImVec2 text_size = ImGui::CalcTextSize(display_text.c_str());
    ImVec2 item_size(
        std::max(1.0f, ImGui::GetContentRegionAvail().x),
        std::max(ImGui::GetTextLineHeight(), text_size.y));
    ImGui::InvisibleButton("text_cell_context_actions_item", item_size);
    if (ImGui::IsItemHovered()) {
        ImGui::GetWindowDrawList()->AddRectFilled(pos, ImVec2(pos.x + item_size.x, pos.y + item_size.y),
                                                  ImGui::GetColorU32(ImGuiCol_HeaderHovered));
    }
    if (!display_text.empty()) {
        ImGui::GetWindowDrawList()->AddText(pos, ImGui::GetColorU32(ImGuiCol_Text), display_text.c_str());
    }

    TextCellContextAction action = TextCellContextAction::None;
    touch_input::open_popup_on_last_item_long_press("text_cell_context_actions");
    if (ImGui::BeginPopupContextItem("text_cell_context_actions", ImGuiPopupFlags_MouseButtonRight)) {
        ImGui::BeginDisabled(!primary_enabled);
        if (ImGui::MenuItem(primary_label.c_str())) action = TextCellContextAction::Primary;
        ImGui::EndDisabled();
        ImGui::BeginDisabled(!secondary_enabled);
        if (ImGui::MenuItem(secondary_label.c_str())) action = TextCellContextAction::Secondary;
        ImGui::EndDisabled();
        ImGui::EndPopup();
    }
    return action;
}

std::string trim_ascii_copy(const std::string& text) {
    size_t first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    size_t last = text.find_last_not_of(" \t\r\n");
    return text.substr(first, last - first + 1);
}

std::vector<std::string> split_structure_key_list(const std::string& text) {
    std::vector<std::string> keys;
    size_t start = 0;
    while (start <= text.size()) {
        size_t comma = text.find(',', start);
        size_t end = comma == std::string::npos ? text.size() : comma;
        std::string key = trim_ascii_copy(text.substr(start, end - start));
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
                    const std::vector<CachedTableRow>& rows,
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
        const CachedTableRow& row = rows[row_index];
        bool matched = false;
        for (size_t column : search_columns) {
            if (column < row.cells.size() &&
                matches_find_query(row.cells[column], state.committed, state.exact)) {
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
                           const std::vector<CachedTableRow>& definition_rows,
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
    state.unused_total = definition_rows.size();
    state.unused_has_run = true;

    std::unordered_map<std::string, std::vector<size_t>> rows_by_key;
    rows_by_key.reserve(definition_rows.size() * 2 + 1);
    for (size_t row_index = 0; row_index < definition_rows.size(); ++row_index) {
        const CachedTableRow& row = definition_rows[row_index];
        if (definition_key_column >= row.cells.size()) continue;
        const std::string& key = row.cells[definition_key_column];
        if (blank_ascii(key)) continue;
        rows_by_key[ascii_case_key(key)].push_back(row_index);
    }

    std::vector<unsigned char> used_rows(definition_rows.size(), 0);
    std::unordered_set<std::string> warned_undefined_keys;
    auto note_key = [&](const std::string& raw_key) {
        std::string key = trim_ascii_copy(raw_key);
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

void set_all_flags(std::vector<unsigned char>& flags, bool value) {
    std::fill(flags.begin(), flags.end(), value ? 1 : 0);
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
        ImGui::Spacing();
        ImGui::Indent(indent);
        if (ImGui::Button(find_label.c_str())) run_find();
        ImGui::SameLine();
        const float arrow_button_width = ImGui::GetFrameHeight();
        const float spacing = ImGui::GetStyle().ItemSpacing.x;
        const float input_width = std::max(80.0f, ImGui::GetContentRegionAvail().x -
            arrow_button_width * 2.0f - spacing * 2.0f - right_padding);
        ImGui::SetNextItemWidth(input_width);
        if (ImGui::InputText(input_id.c_str(), state.query, IM_ARRAYSIZE(state.query),
                             ImGuiInputTextFlags_EnterReturnsTrue)) {
            run_find();
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(state.matches.empty());
        if (ImGui::Button(prev_id.c_str(), ImVec2(arrow_button_width, 0.0f))) {
            step(-1);
        }
        ImGui::SameLine();
        if (ImGui::Button(next_id.c_str(), ImVec2(arrow_button_width, 0.0f))) {
            step(1);
        }
        ImGui::EndDisabled();
        if (ImGui::RadioButton(partial_id.c_str(), !state.exact)) {
            state.exact = false;
            if (state.has_run || !blank_ascii(state.query)) run_find();
        }
        ImGui::SameLine();
        if (ImGui::RadioButton(exact_id.c_str(), state.exact)) {
            state.exact = true;
            if (state.has_run || !blank_ascii(state.query)) run_find();
        }
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

constexpr float kShowColumnWidth = 56.0f;
constexpr ImU32 kFindMatchRowColor = IM_COL32(104, 184, 255, 96);
constexpr ImU32 kUnusedStructureModelRowColor = IM_COL32(72, 196, 112, 120);
constexpr ImU32 kInvalidTrackKeyRowColor = IM_COL32(255, 72, 72, 130);
constexpr const char* kInvalidTrackKeyCell = "_invalidTrackKey";

static const TableColumnDef kStructureModelColumns[] = {
    {"rowNumber", "#", 40.0f},
    {"structureKey", "structureKey", 120.0f},
    {"filePath", "filePath", 200.0f},
};
constexpr int kStructureModelKeyColumn = 1;
constexpr int kStructureModelFilePathColumn = IM_ARRAYSIZE(kStructureModelColumns) - 1;

static const TableColumnDef kOtherTrainColumns[] = {
    {"rowNumber", "#", 40.0f},
    {"distance", "distance", 110.0f},
    {"trainKey", "trainKey", 120.0f},
    {"filePath", "filePath", 200.0f},
    {"trackKey", "trackKey", 90.0f},
    {"direction", "direction", 80.0f},
};
constexpr int kOtherTrainDistanceColumn = 1;
constexpr int kOtherTrainFilePathColumn = 3;

static const TableColumnDef kOtherTrainStopColumns[] = {
    {"rowNumber", "#", 40.0f},
    {"distance", "distance", 110.0f},
    {"trainKey", "trainKey", 120.0f},
    {"decelerate", "decelerate", 90.0f},
    {"stopTime", "stopTime", 90.0f},
    {"accelerate", "accelerate", 90.0f},
    {"speed", "speed", 80.0f},
    {"filePath", "filePath", 200.0f},
};
constexpr int kOtherTrainStopDistanceColumn = 1;
constexpr int kOtherTrainStopFilePathColumn = IM_ARRAYSIZE(kOtherTrainStopColumns) - 1;

static const TableColumnDef kSoundListColumns[] = {
    {"rowNumber", "#", 40.0f},
    {"soundKey", "soundKey", 120.0f},
    {"filePath", "filePath", 200.0f},
    {"bufferCount", "bufferCount", 90.0f},
};
constexpr int kSoundListKeyColumn = 1;
constexpr int kSoundListFilePathColumn = 2;
constexpr int kSoundListBufferCountColumn = 3;

static const char* kStructureColumns[] = {
    "distance", "method", "structureKey", "trackKey", "x", "y", "z", "rx", "ry", "rz",
    "tilt", "span", "trackKey1", "trackKey2", "flag", "filePath"
};
constexpr int kStructureDistanceColumn = 0;
constexpr int kStructureKeyColumn = 2;
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
constexpr int kRepeaterStructureKeysColumn = 14;
constexpr int kRepeaterFilePathColumn = IM_ARRAYSIZE(kRepeaterColumns) - 1;

static const TableColumnDef kSignalAspectFixedColumns[] = {
    {"rowNumber", "#", 40.0f},
    {"signalAspectKey", "signalAspectKey", 150.0f},
};
constexpr int kSignalAspectKeyColumn = 1;
constexpr int kSignalAspectStructureKeyColumnOffset = IM_ARRAYSIZE(kSignalAspectFixedColumns);
constexpr size_t kMaxSignalAspectTableColumns = 511;
constexpr size_t kMaxSignalAspectStructureKeyColumns =
    kMaxSignalAspectTableColumns - static_cast<size_t>(kSignalAspectStructureKeyColumnOffset);

static const TableColumnDef kSignalColumns[] = {
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
constexpr int kSignalDistanceColumn = 1;
constexpr int kSignalSignalAspectKeyColumn = 3;
constexpr int kSignalFilePathColumn = IM_ARRAYSIZE(kSignalColumns) - 1;

static const TableColumnDef kBeaconColumns[] = {
    {"rowNumber", "#", 40.0f},
    {"distance", "distance", 110.0f},
    {"type", "type", 70.0f},
    {"section", "section", 70.0f},
    {"sendData", "sendData", 90.0f},
    {"filePath", "filePath", 200.0f},
};
constexpr int kBeaconDistanceColumn = 1;
constexpr int kBeaconFilePathColumn = IM_ARRAYSIZE(kBeaconColumns) - 1;

static const TableColumnDef kIrregularityColumns[] = {
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
constexpr int kIrregularityDistanceColumn = 1;
constexpr int kIrregularityFilePathColumn = IM_ARRAYSIZE(kIrregularityColumns) - 1;

static const TableColumnDef kMapSoundColumns[] = {
    {"rowNumber", "#", 40.0f},
    {"distance", "distance", 110.0f},
    {"soundKey", "soundKey", 120.0f},
    {"filePath", "filePath", 200.0f},
};
constexpr int kMapSoundDistanceColumn = 1;
constexpr int kMapSoundKeyColumn = 2;
constexpr int kMapSoundFilePathColumn = IM_ARRAYSIZE(kMapSoundColumns) - 1;

static const TableColumnDef kRollingNoiseColumns[] = {
    {"rowNumber", "#", 40.0f},
    {"distance", "distance", 110.0f},
    {"index", "index", 70.0f},
    {"filePath", "filePath", 200.0f},
};
constexpr int kRollingNoiseDistanceColumn = 1;
constexpr int kRollingNoiseFilePathColumn = IM_ARRAYSIZE(kRollingNoiseColumns) - 1;

static const TableColumnDef kFlangeNoiseColumns[] = {
    {"rowNumber", "#", 40.0f},
    {"distance", "distance", 110.0f},
    {"index", "index", 70.0f},
    {"filePath", "filePath", 200.0f},
};
constexpr int kFlangeNoiseDistanceColumn = 1;
constexpr int kFlangeNoiseFilePathColumn = IM_ARRAYSIZE(kFlangeNoiseColumns) - 1;

static const TableColumnDef kJointNoiseColumns[] = {
    {"rowNumber", "#", 40.0f},
    {"distance", "distance", 110.0f},
    {"index", "index", 70.0f},
    {"filePath", "filePath", 200.0f},
};
constexpr int kJointNoiseDistanceColumn = 1;
constexpr int kJointNoiseFilePathColumn = IM_ARRAYSIZE(kJointNoiseColumns) - 1;

static const TableColumnDef kBackgroundColumns[] = {
    {"rowNumber", "#", 40.0f},
    {"distance", "distance", 110.0f},
    {"structureKey", "structureKey", 120.0f},
    {"filePath", "filePath", 200.0f},
};
constexpr int kBackgroundDistanceColumn = 1;
constexpr int kBackgroundStructureKeyColumn = 2;
constexpr int kBackgroundFilePathColumn = IM_ARRAYSIZE(kBackgroundColumns) - 1;

static const TableColumnDef kAdhesionColumns[] = {
    {"rowNumber", "#", 40.0f},
    {"distance", "distance", 110.0f},
    {"a", "a", 70.0f},
    {"b", "b", 70.0f},
    {"c", "c", 70.0f},
    {"filePath", "filePath", 200.0f},
};
constexpr int kAdhesionDistanceColumn = 1;
constexpr int kAdhesionFilePathColumn = IM_ARRAYSIZE(kAdhesionColumns) - 1;

static const TableColumnDef kCabIlluminanceColumns[] = {
    {"rowNumber", "#", 40.0f},
    {"distance", "distance", 110.0f},
    {"value", "value", 70.0f},
    {"filePath", "filePath", 200.0f},
};
constexpr int kCabIlluminanceDistanceColumn = 1;
constexpr int kCabIlluminanceFilePathColumn = IM_ARRAYSIZE(kCabIlluminanceColumns) - 1;

static const TableColumnDef kFogColumns[] = {
    {"rowNumber", "#", 40.0f},
    {"distance", "distance", 110.0f},
    {"density", "density", 80.0f},
    {"red", "red", 70.0f},
    {"green", "green", 70.0f},
    {"blue", "blue", 70.0f},
    {"filePath", "filePath", 200.0f},
};
constexpr int kFogDistanceColumn = 1;
constexpr int kFogFilePathColumn = IM_ARRAYSIZE(kFogColumns) - 1;

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

namespace {

bool is_scene_table_own_track_key(const std::string& normalized_key) {
    return normalized_key.empty() || normalized_key == "0";
}

bool is_scene_table_track_key_valid(const std::string& raw_key,
                                    const std::unordered_set<std::string>& other_track_keys) {
    std::string normalized_key = normalize_track_lookup_key(raw_key);
    return is_scene_table_own_track_key(normalized_key) ||
        other_track_keys.find(normalized_key) != other_track_keys.end();
}

bool is_invalid_track_key_row(const TableRow& row) {
    return table_cell(row, kInvalidTrackKeyCell) == "true";
}

void clear_invalid_track_key_flags(std::vector<TableRow>& rows) {
    for (TableRow& row : rows) row.cells.erase(kInvalidTrackKeyCell);
}

void mark_invalid_track_key_row(TableRow& row) {
    row.cells[kInvalidTrackKeyCell] = "true";
}

void append_scene_track_key_warning(MapModel& model,
                                    TableRow& row,
                                    const std::string& item_label,
                                    size_t display_row,
                                    const std::string& column_key,
                                    const std::string& track_key) {
    mark_invalid_track_key_row(row);
    std::string message = "[WARN]datatable.cpp: " + item_label + " #" +
        std::to_string(display_row) + " was placed on nonexistent track \"" + track_key + "\"";
    if (!column_key.empty()) message += " (" + column_key + ")";
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
    append_scene_track_key_warning(model, row, item_label, display_row, column_key, key);
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

template <size_t N, typename CanLocateFn, typename LocateFn>
void render_change_point_table(const char* table_id,
                               const TableColumnDef (&columns)[N],
                               int distance_column,
                               int file_path_column,
                               const std::vector<CachedTableRow>& rows,
                               float distance_width,
                               float file_path_width,
                               const std::string& file_name_header,
                               const std::string& locate_menu_label,
                               const std::string& open_menu_label,
                               int& scroll_row,
                               int& highlight_row,
                               ImU32 highlight_color,
                               CanLocateFn can_locate,
                               LocateFn locate_row_on_plan) {
    if (ImGui::BeginTable(table_id, static_cast<int>(N),
                          ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                          ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollX |
                          ImGuiTableFlags_ScrollY)) {
        for (int i = 0; i < static_cast<int>(N); ++i) {
            float width = columns[i].width;
            if (i == distance_column) width = distance_width;
            if (i == file_path_column) width = file_path_width;
            const char* header = i == file_path_column
                ? file_name_header.c_str()
                : columns[i].header;
            ImGui::TableSetupColumn(header, width > 0.0f ? ImGuiTableColumnFlags_WidthFixed : 0, width);
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
                for (int i = 0; i < static_cast<int>(N); ++i) {
                    ImGui::TableSetColumnIndex(i);
                    const std::string& value = row.cells[static_cast<size_t>(i)];
                    if (i == distance_column) {
                        const size_t marker_index = static_cast<size_t>(row_index);
                        if (render_text_cell_with_context(value, locate_menu_label, can_locate(marker_index))) {
                            locate_row_on_plan(marker_index);
                        }
                        continue;
                    }
                    if (value.empty()) continue;
                    if (i == file_path_column) {
                        render_file_path_cell_with_context(value, row.open_path, open_menu_label, row.open_path);
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

template <size_t N, typename CanLocateFn, typename LocateFn, typename FindFn>
void render_map_sound_event_table(const char* table_id,
                                  const TableColumnDef (&columns)[N],
                                  int distance_column,
                                  int sound_key_column,
                                  int file_path_column,
                                  const std::vector<CachedTableRow>& rows,
                                  float distance_width,
                                  float file_path_width,
                                  const std::string& file_name_header,
                                  const std::string& locate_menu_label,
                                  const std::string& find_menu_label,
                                  const std::string& open_menu_label,
                                  int& scroll_row,
                                  int& highlight_row,
                                  ImU32 highlight_color,
                                  CanLocateFn can_locate,
                                  LocateFn locate_row_on_plan,
                                  FindFn find_sound_file) {
    if (ImGui::BeginTable(table_id, static_cast<int>(N),
                          ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                          ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollX |
                          ImGuiTableFlags_ScrollY)) {
        for (int i = 0; i < static_cast<int>(N); ++i) {
            float width = columns[i].width;
            if (i == distance_column) width = distance_width;
            if (i == file_path_column) width = file_path_width;
            const char* header = i == file_path_column
                ? file_name_header.c_str()
                : columns[i].header;
            ImGui::TableSetupColumn(header, width > 0.0f ? ImGuiTableColumnFlags_WidthFixed : 0, width);
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
                for (int i = 0; i < static_cast<int>(N); ++i) {
                    ImGui::TableSetColumnIndex(i);
                    const std::string& value = row.cells[static_cast<size_t>(i)];
                    if (i == distance_column) {
                        const size_t marker_index = static_cast<size_t>(row_index);
                        ImGui::PushID(i);
                        const bool should_locate =
                            render_text_cell_with_context(value, locate_menu_label, can_locate(marker_index));
                        ImGui::PopID();
                        if (should_locate) {
                            locate_row_on_plan(marker_index);
                        }
                        continue;
                    }
                    if (i == sound_key_column) {
                        ImGui::PushID(i);
                        const bool should_find =
                            render_text_cell_with_context(value, find_menu_label, !blank_ascii(value));
                        ImGui::PopID();
                        if (should_find) {
                            find_sound_file(value);
                        }
                        continue;
                    }
                    if (value.empty()) continue;
                    if (i == file_path_column) {
                        render_file_path_cell_with_context(value, row.open_path, open_menu_label, row.open_path);
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

static void render_sound_list_table(const char* table_id,
                                    const std::vector<CachedTableRow>& rows,
                                    float file_path_width,
                                    float buffer_count_width,
                                    TableFindState& find_state,
                                    const std::string& file_name_header,
                                    const std::string& open_menu_label) {
    if (ImGui::BeginTable(table_id, IM_ARRAYSIZE(kSoundListColumns),
                          ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                          ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollX |
                          ImGuiTableFlags_ScrollY)) {
        for (int i = 0; i < IM_ARRAYSIZE(kSoundListColumns); ++i) {
            float width = kSoundListColumns[i].width;
            if (i == kSoundListFilePathColumn) width = file_path_width;
            if (i == kSoundListBufferCountColumn) width = buffer_count_width;
            const char* header = i == kSoundListFilePathColumn
                ? file_name_header.c_str()
                : kSoundListColumns[i].header;
            ImGui::TableSetupColumn(header, width > 0.0f ? ImGuiTableColumnFlags_WidthFixed : 0, width);
        }
        setup_fixed_table_header();
        ImGui::TableHeadersRow();
        ImGuiListClipper clipper;
        const int row_count = static_cast<int>(rows.size());
        if (find_state.scroll_row >= row_count) find_state.scroll_row = -1;
        const int scroll_target_row = find_state.scroll_row;
        clipper.Begin(row_count);
        if (scroll_target_row >= 0 && scroll_target_row < row_count) {
            clipper.IncludeItemByIndex(scroll_target_row);
        }
        while (clipper.Step()) {
            for (int row_index = clipper.DisplayStart; row_index < clipper.DisplayEnd; ++row_index) {
                const CachedTableRow& row = rows[static_cast<size_t>(row_index)];
                const bool is_find_match =
                    static_cast<size_t>(row_index) < find_state.row_matches.size() &&
                    find_state.row_matches[static_cast<size_t>(row_index)] != 0;
                const bool is_unused =
                    static_cast<size_t>(row_index) < find_state.unused_row_matches.size() &&
                    find_state.unused_row_matches[static_cast<size_t>(row_index)] != 0;
                ImGui::TableNextRow();
                if (is_unused) {
                    ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, kUnusedStructureModelRowColor);
                } else if (is_find_match) {
                    ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, kFindMatchRowColor);
                }
                if (row_index == scroll_target_row) {
                    ImGui::SetScrollHereY(0.0f);
                    find_state.scroll_row = -1;
                }
                ImGui::PushID(row_index);
                for (int i = 0; i < IM_ARRAYSIZE(kSoundListColumns); ++i) {
                    ImGui::TableSetColumnIndex(i);
                    const std::string& value = row.cells[static_cast<size_t>(i)];
                    if (value.empty()) continue;
                    if (i == kSoundListFilePathColumn) {
                        render_file_path_cell_with_context(value, row.open_path,
                                                           open_menu_label, row.open_path);
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
    std::vector<TableRow> ordered_rows = data;
    std::stable_sort(ordered_rows.begin(), ordered_rows.end(), repeater_event_distance_order_less);

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
                previous.cells["_filePathTooltip"] = format_repeater_file_path_tooltip(table_cell(previous, "_beginFilePath"));
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
            new_row.cells["_filePathTooltip"] = format_repeater_file_path_tooltip(begin_file_path);
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
                begin_row.cells["_filePathTooltip"] = format_repeater_file_path_tooltip(begin_file_path, end_file_path);
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

    cache.other_train_distance_width = 0.0f;
    expand_width_for_text(cache.other_train_distance_width, kOtherTrainColumns[kOtherTrainDistanceColumn].header);
    std::vector<std::pair<std::string, std::string>> definition_train_keys;
    std::unordered_set<std::string> seen_definition_train_keys;
    cache.other_train_rows.reserve(model_.other_trains.size());
    for (size_t row_index = 0; row_index < model_.other_trains.size(); ++row_index) {
        const TableRow& row = model_.other_trains[row_index];
        CachedTableRow cached;
        cached.cells.resize(IM_ARRAYSIZE(kOtherTrainColumns));
        cached.cells[0] = std::to_string(row_index + 1);
        cached.cells[kOtherTrainDistanceColumn] = table_cell(row, "distance");
        cached.cells[2] = table_cell(row, "trainKey");
        std::string normalized_train_key = normalize_track_lookup_key(cached.cells[2]);
        if (seen_definition_train_keys.insert(normalized_train_key).second) {
            definition_train_keys.emplace_back(std::move(normalized_train_key), cached.cells[2]);
        }
        cached.cells[kOtherTrainFilePathColumn] = table_cell(row, "filePath");
        cached.cells[4] = table_cell(row, "trackKey");
        cached.cells[5] = table_cell(row, "direction");
        cached.open_path = table_cell(row, "resolvedFilePath");
        cached.tooltip_text = cached.open_path;
        expand_width_for_text(cache.other_train_distance_width,
                              cached.cells[kOtherTrainDistanceColumn]);
        expand_width_for_text(cache.other_train_file_path_width,
                              cached.cells[kOtherTrainFilePathColumn]);
        cache.other_train_rows.push_back(std::move(cached));
    }

    cache.other_train_stop_distance_width = 0.0f;
    cache.other_train_stop_file_path_width = 0.0f;
    expand_width_for_text(cache.other_train_stop_distance_width,
                          kOtherTrainStopColumns[kOtherTrainStopDistanceColumn].header);
    expand_width_for_text(cache.other_train_stop_file_path_width,
                          kOtherTrainStopColumns[kOtherTrainStopFilePathColumn].header);
    std::vector<CachedOtherTrainStopGroup> stop_groups;
    std::unordered_map<std::string, size_t> stop_group_index_by_train_key;
    cache.other_train_stop_rows.reserve(model_.other_train_stops.size());
    for (size_t row_index = 0; row_index < model_.other_train_stops.size(); ++row_index) {
        const TableRow& row = model_.other_train_stops[row_index];
        CachedTableRow cached;
        cached.cells.resize(IM_ARRAYSIZE(kOtherTrainStopColumns));
        cached.cells[kOtherTrainStopDistanceColumn] = table_cell(row, "distance");
        cached.cells[2] = table_cell(row, "trainKey");
        std::string normalized_train_key = normalize_track_lookup_key(cached.cells[2]);
        auto group_it = stop_group_index_by_train_key.find(normalized_train_key);
        if (group_it == stop_group_index_by_train_key.end()) {
            const size_t group_index = stop_groups.size();
            CachedOtherTrainStopGroup group;
            group.train_key = cached.cells[2];
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
        cached.cells[kOtherTrainStopFilePathColumn] = display_name_from_path(cached.open_path);
        cached.tooltip_text = cached.open_path;
        expand_width_for_text(cache.other_train_stop_distance_width,
                              cached.cells[kOtherTrainStopDistanceColumn]);
        expand_width_for_text(cache.other_train_stop_file_path_width,
                              cached.cells[kOtherTrainStopFilePathColumn]);
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
        std::string normalized_train_key = normalize_track_lookup_key(group.train_key);
        if (appended_stop_group_keys.find(normalized_train_key) != appended_stop_group_keys.end()) continue;
        cache.other_train_stop_groups.push_back(std::move(group));
    }

    cache.sound_list_buffer_count_width = 0.0f;
    cache.sound_3d_list_buffer_count_width = 0.0f;
    expand_width_for_text(cache.sound_list_buffer_count_width, kSoundListColumns[kSoundListBufferCountColumn].header);
    expand_width_for_text(cache.sound_3d_list_buffer_count_width, kSoundListColumns[kSoundListBufferCountColumn].header);
    cache.sound_list_rows.reserve(model_.sound_list.size());
    cache.sound_3d_list_rows.reserve(model_.sound_list.size());
    auto append_sound_list_row = [&](const TableRow& row,
                                     std::vector<CachedTableRow>& rows,
                                     float& file_path_width,
                                     float& buffer_count_width) {
        CachedTableRow cached;
        cached.cells.resize(IM_ARRAYSIZE(kSoundListColumns));
        cached.cells[0] = std::to_string(rows.size() + 1);
        cached.cells[1] = table_cell(row, "soundKey");
        cached.open_path = table_cell(row, "filePath");
        cached.cells[2] = display_name_from_path(cached.open_path);
        cached.cells[3] = table_cell(row, "bufferCount");
        expand_width_for_text(file_path_width, cached.cells[2]);
        expand_width_for_text(buffer_count_width, cached.cells[3]);
        rows.push_back(std::move(cached));
    };
    for (const TableRow& row : model_.sound_list) {
        const bool is_3d = table_cell(row, "is3D") == "true";
        if (is_3d) {
            append_sound_list_row(row, cache.sound_3d_list_rows,
                                  cache.sound_3d_list_file_path_width,
                                  cache.sound_3d_list_buffer_count_width);
        } else {
            append_sound_list_row(row, cache.sound_list_rows,
                                  cache.sound_list_file_path_width,
                                  cache.sound_list_buffer_count_width);
        }
    }

    auto append_structure_rows = [&](const std::vector<TableRow>& rows) {
        cache.structure_rows.reserve(cache.structure_rows.size() + rows.size());
        for (const auto& row : rows) {
            CachedTableRow cached;
            cached.cells.resize(IM_ARRAYSIZE(kStructureColumns));
            cached.invalid_track_key = is_invalid_track_key_row(row);
            for (int i = 0; i < IM_ARRAYSIZE(kStructureColumns); ++i) {
                const std::string& value = table_cell(row, kStructureColumns[i]);
                if (i == kStructureFilePathColumn) {
                    cached.open_path = value;
                    cached.tooltip_text = value;
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
        cached.invalid_track_key = is_invalid_track_key_row(row);
        cached.open_path = table_cell(row, "_openFilePath");
        cached.tooltip_text = table_cell(row, "_filePathTooltip");
        if (cached.tooltip_text.empty()) cached.tooltip_text = cached.open_path;
        for (int i = 0; i < IM_ARRAYSIZE(kRepeaterColumns); ++i) {
            cached.cells[i] = table_cell(row, kRepeaterColumns[i].key);
        }
        expand_width_for_text(cache.repeater_distance_width, cached.cells[kRepeaterDistanceColumn]);
        expand_width_for_text(cache.repeater_file_path_width, cached.cells[kRepeaterFilePathColumn]);
        cache.repeater_rows.push_back(std::move(cached));
    }

    cache.signal_aspect_structure_key_columns = 0;
    for (const TableRow& row : model_.signal_aspects) {
        size_t structure_key_count = static_cast<size_t>(table_cell_number(row, "_structureKeyCount"));
        cache.signal_aspect_structure_key_columns =
            std::max(cache.signal_aspect_structure_key_columns, structure_key_count);
    }
    cache.signal_aspect_structure_key_columns =
        std::min(cache.signal_aspect_structure_key_columns, kMaxSignalAspectStructureKeyColumns);
    cache.signal_aspect_structure_key_widths.assign(cache.signal_aspect_structure_key_columns, 120.0f);
    cache.signal_aspect_rows.reserve(model_.signal_aspects.size());
    for (size_t row_index = 0; row_index < model_.signal_aspects.size(); ++row_index) {
        const TableRow& row = model_.signal_aspects[row_index];
        CachedTableRow cached;
        cached.cells.resize(kSignalAspectStructureKeyColumnOffset + cache.signal_aspect_structure_key_columns);
        cached.cells[0] = std::to_string(row_index + 1);
        cached.cells[1] = table_cell(row, "signalAspectKey");
        for (size_t key_index = 0; key_index < cache.signal_aspect_structure_key_columns; ++key_index) {
            std::string column_key = "structureKey" + std::to_string(key_index + 1);
            std::string value = table_cell(row, column_key);
            cached.cells[kSignalAspectStructureKeyColumnOffset + key_index] = value;
            expand_width_for_text(cache.signal_aspect_structure_key_widths[key_index], value);
        }
        cache.signal_aspect_rows.push_back(std::move(cached));
    }

    cache.signal_distance_width = 0.0f;
    expand_width_for_text(cache.signal_distance_width, kSignalColumns[kSignalDistanceColumn].header);
    cache.signal_rows.reserve(model_.signals.size());
    for (size_t row_index = 0; row_index < model_.signals.size(); ++row_index) {
        const TableRow& row = model_.signals[row_index];
        CachedTableRow cached;
        cached.cells.resize(IM_ARRAYSIZE(kSignalColumns));
        cached.open_path = table_cell(row, "filePath");
        cached.tooltip_text = cached.open_path;
        for (int i = 0; i < IM_ARRAYSIZE(kSignalColumns); ++i) {
            if (i == kSignalFilePathColumn) {
                cached.cells[i] = display_name_from_path(cached.open_path);
                expand_width_for_text(cache.signal_file_path_width, cached.cells[i]);
            } else {
                cached.cells[i] = table_cell(row, kSignalColumns[i].key);
            }
        }
        cached.cells[0] = std::to_string(row_index + 1);
        expand_width_for_text(cache.signal_distance_width, cached.cells[kSignalDistanceColumn]);
        cache.signal_rows.push_back(std::move(cached));
    }

    cache.beacon_distance_width = 0.0f;
    expand_width_for_text(cache.beacon_distance_width, kBeaconColumns[kBeaconDistanceColumn].header);
    cache.beacon_rows.reserve(model_.beacons.size());
    for (size_t row_index = 0; row_index < model_.beacons.size(); ++row_index) {
        const TableRow& row = model_.beacons[row_index];
        CachedTableRow cached;
        cached.cells.resize(IM_ARRAYSIZE(kBeaconColumns));
        cached.open_path = table_cell(row, "filePath");
        cached.tooltip_text = cached.open_path;
        for (int i = 0; i < IM_ARRAYSIZE(kBeaconColumns); ++i) {
            if (i == kBeaconFilePathColumn) {
                cached.cells[i] = display_name_from_path(cached.open_path);
                expand_width_for_text(cache.beacon_file_path_width, cached.cells[i]);
            } else {
                cached.cells[i] = table_cell(row, kBeaconColumns[i].key);
            }
        }
        cached.cells[0] = std::to_string(row_index + 1);
        expand_width_for_text(cache.beacon_distance_width, cached.cells[kBeaconDistanceColumn]);
        cache.beacon_rows.push_back(std::move(cached));
    }

    cache.irregularity_distance_width = 0.0f;
    expand_width_for_text(cache.irregularity_distance_width, kIrregularityColumns[kIrregularityDistanceColumn].header);
    cache.irregularity_rows.reserve(model_.irregularities.size());
    for (size_t row_index = 0; row_index < model_.irregularities.size(); ++row_index) {
        const TableRow& row = model_.irregularities[row_index];
        CachedTableRow cached;
        cached.cells.resize(IM_ARRAYSIZE(kIrregularityColumns));
        cached.open_path = table_cell(row, "filePath");
        for (int i = 0; i < IM_ARRAYSIZE(kIrregularityColumns); ++i) {
            if (i == kIrregularityFilePathColumn) {
                cached.cells[i] = display_name_from_path(cached.open_path);
                expand_width_for_text(cache.irregularity_file_path_width, cached.cells[i]);
            } else {
                cached.cells[i] = table_cell(row, kIrregularityColumns[i].key);
            }
        }
        cached.cells[0] = std::to_string(row_index + 1);
        expand_width_for_text(cache.irregularity_distance_width, cached.cells[kIrregularityDistanceColumn]);
        cache.irregularity_rows.push_back(std::move(cached));
    }

    append_change_point_rows(model_.rolling_noises, kRollingNoiseColumns,
                             kRollingNoiseDistanceColumn, kRollingNoiseFilePathColumn,
                             cache.rolling_noise_rows,
                             cache.rolling_noise_distance_width,
                             cache.rolling_noise_file_path_width);
    append_change_point_rows(model_.flange_noises, kFlangeNoiseColumns,
                             kFlangeNoiseDistanceColumn, kFlangeNoiseFilePathColumn,
                             cache.flange_noise_rows,
                             cache.flange_noise_distance_width,
                             cache.flange_noise_file_path_width);
    append_change_point_rows(model_.joint_noises, kJointNoiseColumns,
                             kJointNoiseDistanceColumn, kJointNoiseFilePathColumn,
                             cache.joint_noise_rows,
                             cache.joint_noise_distance_width,
                             cache.joint_noise_file_path_width);

    append_change_point_rows(model_.map_sounds, kMapSoundColumns,
                             kMapSoundDistanceColumn, kMapSoundFilePathColumn,
                             cache.map_sound_rows,
                             cache.map_sound_distance_width,
                             cache.map_sound_file_path_width);
    append_change_point_rows(model_.map_sound_3d, kMapSoundColumns,
                             kMapSoundDistanceColumn, kMapSoundFilePathColumn,
                             cache.map_sound_3d_rows,
                             cache.map_sound_3d_distance_width,
                             cache.map_sound_3d_file_path_width);

    cache.background_distance_width = 0.0f;
    expand_width_for_text(cache.background_distance_width, kBackgroundColumns[kBackgroundDistanceColumn].header);
    cache.background_rows.reserve(model_.backgrounds.size());
    for (size_t row_index = 0; row_index < model_.backgrounds.size(); ++row_index) {
        const TableRow& row = model_.backgrounds[row_index];
        CachedTableRow cached;
        cached.cells.resize(IM_ARRAYSIZE(kBackgroundColumns));
        cached.open_path = table_cell(row, "filePath");
        for (int i = 0; i < IM_ARRAYSIZE(kBackgroundColumns); ++i) {
            if (i == kBackgroundFilePathColumn) {
                cached.cells[i] = display_name_from_path(cached.open_path);
                expand_width_for_text(cache.background_file_path_width, cached.cells[i]);
            } else {
                cached.cells[i] = table_cell(row, kBackgroundColumns[i].key);
            }
        }
        cached.cells[0] = std::to_string(row_index + 1);
        expand_width_for_text(cache.background_distance_width, cached.cells[kBackgroundDistanceColumn]);
        cache.background_rows.push_back(std::move(cached));
    }

    cache.adhesion_distance_width = 0.0f;
    expand_width_for_text(cache.adhesion_distance_width, kAdhesionColumns[kAdhesionDistanceColumn].header);
    cache.adhesion_rows.reserve(model_.adhesions.size());
    for (size_t row_index = 0; row_index < model_.adhesions.size(); ++row_index) {
        const TableRow& row = model_.adhesions[row_index];
        CachedTableRow cached;
        cached.cells.resize(IM_ARRAYSIZE(kAdhesionColumns));
        cached.open_path = table_cell(row, "filePath");
        for (int i = 0; i < IM_ARRAYSIZE(kAdhesionColumns); ++i) {
            if (i == kAdhesionFilePathColumn) {
                cached.cells[i] = display_name_from_path(cached.open_path);
                expand_width_for_text(cache.adhesion_file_path_width, cached.cells[i]);
            } else {
                cached.cells[i] = table_cell(row, kAdhesionColumns[i].key);
            }
        }
        cached.cells[0] = std::to_string(row_index + 1);
        expand_width_for_text(cache.adhesion_distance_width, cached.cells[kAdhesionDistanceColumn]);
        cache.adhesion_rows.push_back(std::move(cached));
    }

    cache.cab_illuminance_distance_width = 0.0f;
    expand_width_for_text(cache.cab_illuminance_distance_width, kCabIlluminanceColumns[kCabIlluminanceDistanceColumn].header);
    cache.cab_illuminance_rows.reserve(model_.cab_illuminance.size());
    for (size_t row_index = 0; row_index < model_.cab_illuminance.size(); ++row_index) {
        const TableRow& row = model_.cab_illuminance[row_index];
        CachedTableRow cached;
        cached.cells.resize(IM_ARRAYSIZE(kCabIlluminanceColumns));
        cached.open_path = table_cell(row, "filePath");
        for (int i = 0; i < IM_ARRAYSIZE(kCabIlluminanceColumns); ++i) {
            if (i == kCabIlluminanceFilePathColumn) {
                cached.cells[i] = display_name_from_path(cached.open_path);
                expand_width_for_text(cache.cab_illuminance_file_path_width, cached.cells[i]);
            } else {
                cached.cells[i] = table_cell(row, kCabIlluminanceColumns[i].key);
            }
        }
        cached.cells[0] = std::to_string(row_index + 1);
        expand_width_for_text(cache.cab_illuminance_distance_width, cached.cells[kCabIlluminanceDistanceColumn]);
        cache.cab_illuminance_rows.push_back(std::move(cached));
    }

    cache.fog_distance_width = 0.0f;
    expand_width_for_text(cache.fog_distance_width, kFogColumns[kFogDistanceColumn].header);
    cache.fog_rows.reserve(model_.fogs.size());
    for (size_t row_index = 0; row_index < model_.fogs.size(); ++row_index) {
        const TableRow& row = model_.fogs[row_index];
        CachedTableRow cached;
        cached.cells.resize(IM_ARRAYSIZE(kFogColumns));
        cached.open_path = table_cell(row, "filePath");
        for (int i = 0; i < IM_ARRAYSIZE(kFogColumns); ++i) {
            if (i == kFogFilePathColumn) {
                cached.cells[i] = display_name_from_path(cached.open_path);
                expand_width_for_text(cache.fog_file_path_width, cached.cells[i]);
            } else {
                cached.cells[i] = table_cell(row, kFogColumns[i].key);
            }
        }
        cached.cells[0] = std::to_string(row_index + 1);
        expand_width_for_text(cache.fog_distance_width, cached.cells[kFogDistanceColumn]);
        cache.fog_rows.push_back(std::move(cached));
    }

    table_cache_ = std::move(cache);
}

void App::reset_structure_model_find_results() {
    reset_table_find_results(structure_model_find_);
}

void App::run_structure_model_find() {
    ensure_table_cache();
    run_table_find(structure_model_find_,
                   table_cache_.structure_model_rows,
                   {static_cast<size_t>(kStructureModelKeyColumn),
                    static_cast<size_t>(kStructureModelFilePathColumn)});
}

void App::run_unused_structure_model_search() {
    ensure_table_cache();
    add_log("[INFO]datatable.cpp: Searching unused models...");

    run_unused_key_search(
        structure_model_find_,
        table_cache_.structure_model_rows,
        static_cast<size_t>(kStructureModelKeyColumn),
        [this](auto& note_structure_key) {
            for (const CachedTableRow& row : table_cache_.structure_rows) {
                if (row.cells.size() > static_cast<size_t>(kStructureKeyColumn)) {
                    note_structure_key(row.cells[static_cast<size_t>(kStructureKeyColumn)]);
                }
            }
            for (const CachedTableRow& row : table_cache_.repeater_rows) {
                if (row.cells.size() <= static_cast<size_t>(kRepeaterStructureKeysColumn)) continue;
                for (const std::string& key :
                     split_structure_key_list(row.cells[static_cast<size_t>(kRepeaterStructureKeysColumn)])) {
                    note_structure_key(key);
                }
            }
            for (const CachedTableRow& row : table_cache_.background_rows) {
                if (row.cells.size() > static_cast<size_t>(kBackgroundStructureKeyColumn)) {
                    note_structure_key(row.cells[static_cast<size_t>(kBackgroundStructureKeyColumn)]);
                }
            }
            for (const CachedTableRow& row : table_cache_.signal_aspect_rows) {
                for (size_t i = kSignalAspectStructureKeyColumnOffset; i < row.cells.size(); ++i) {
                    note_structure_key(row.cells[i]);
                }
            }
            for (const TableRow& row : model_.other_train_structure_keys) {
                note_structure_key(table_cell(row, "key"));
            }
        },
        [this](const std::string& key) {
            add_log("[WARN]datatable.cpp: Found undefined structureKey:\"" + key + "\"");
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
    ensure_table_cache();
    run_table_find(signal_aspect_find_,
                   table_cache_.signal_aspect_rows,
                   {static_cast<size_t>(kSignalAspectKeyColumn)});
}

void App::run_unused_signal_aspect_search() {
    ensure_table_cache();
    add_log("[INFO]datatable.cpp: Searching unused signal aspects...");

    run_unused_key_search(
        signal_aspect_find_,
        table_cache_.signal_aspect_rows,
        static_cast<size_t>(kSignalAspectKeyColumn),
        [this](auto& note_signal_aspect_key) {
            for (const CachedTableRow& row : table_cache_.signal_rows) {
                if (row.cells.size() > static_cast<size_t>(kSignalSignalAspectKeyColumn)) {
                    note_signal_aspect_key(row.cells[static_cast<size_t>(kSignalSignalAspectKeyColumn)]);
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
    ensure_table_cache();
    TableFindState& state = is_3d ? sound_3d_file_find_ : sound_file_find_;
    const std::vector<CachedTableRow>& rows = is_3d
        ? table_cache_.sound_3d_list_rows
        : table_cache_.sound_list_rows;

    run_table_find(state,
                   rows,
                   {static_cast<size_t>(kSoundListKeyColumn),
                    static_cast<size_t>(kSoundListFilePathColumn)});
}

void App::run_unused_sound_file_search(bool is_3d) {
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
        file_rows,
        static_cast<size_t>(kSoundListKeyColumn),
        [&](auto& note_sound_key) {
            for (const CachedTableRow& row : usage_rows) {
                if (row.cells.size() > static_cast<size_t>(kMapSoundKeyColumn)) {
                    note_sound_key(row.cells[static_cast<size_t>(kMapSoundKeyColumn)]);
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
    if (focus_structures_next_) ImGui::SetNextWindowFocus();
    std::string title = tr("frame.structures") + "###Structures";
    if (!ImGui::Begin(title.c_str(), &show_structures_window_)) {
        focus_structures_next_ = false;
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
        const int row_count = static_cast<int>(table_cache_.structure_rows.size());
        if (structure_list_scroll_row_ >= row_count) structure_list_scroll_row_ = -1;
        if (structure_list_highlight_row_ >= row_count) structure_list_highlight_row_ = -1;
        const int scroll_target_row = structure_list_scroll_row_;
        clipper.Begin(row_count);
        if (scroll_target_row >= 0 && scroll_target_row < row_count) {
            clipper.IncludeItemByIndex(scroll_target_row);
        }
        const ImU32 highlight_color = table_row_highlight_color(theme_color_);
        const bool can_locate_scene_preview = can_locate_scene_preview_row();
        while (clipper.Step()) {
            for (int row_index = clipper.DisplayStart; row_index < clipper.DisplayEnd; ++row_index) {
                const CachedTableRow& row = table_cache_.structure_rows[static_cast<size_t>(row_index)];
                ImGui::TableNextRow();
                if (row.invalid_track_key) {
                    ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, kInvalidTrackKeyRowColor);
                } else if (row_index == structure_list_highlight_row_) {
                    ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, highlight_color);
                }
                if (row_index == scroll_target_row) {
                    ImGui::SetScrollHereY(0.5f);
                    structure_list_scroll_row_ = -1;
                }
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
                        const bool can_locate_scene = can_locate_scene_preview && !row.invalid_track_key;
                        TextCellContextAction action = render_text_cell_with_context_actions(
                            value,
                            tr("menu.locate_on_plan"),
                            can_locate,
                            tr("menu.locate_in_scene_preview"),
                            can_locate_scene);
                        if (action == TextCellContextAction::Primary) {
                            locate_structure_row_on_plan(marker_index);
                        } else if (action == TextCellContextAction::Secondary) {
                            locate_structure_row_in_scene_preview(marker_index);
                        }
                        continue;
                    }
                    if (value.empty()) continue;
                    if (i == kStructureKeyColumn) {
                        ImGui::PushID("structure_key");
                        if (render_text_cell_with_context(value, tr("menu.find_in_structure_models"), !blank_ascii(value))) {
                            find_structure_model_for_structure_key(value);
                        }
                        ImGui::PopID();
                    } else if (i == kStructureFilePathColumn) {
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
    focus_structures_next_ = false;
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
    const bool stale_find_results = structure_model_find_.has_run &&
        structure_model_find_.committed != structure_model_find_.query;
    if (stale_find_results) reset_structure_model_find_results();

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
        const int row_count = static_cast<int>(table_cache_.structure_model_rows.size());
        const int scroll_target_row = structure_model_find_.scroll_row;
        clipper.Begin(row_count);
        if (scroll_target_row >= 0 && scroll_target_row < row_count) {
            clipper.IncludeItemByIndex(scroll_target_row);
        }
        const ImVec4 preview_text_color = ImVec4(1.0f, 1.0f, 0.0f, 1.0f);
        while (clipper.Step()) {
            for (int row_index = clipper.DisplayStart; row_index < clipper.DisplayEnd; ++row_index) {
                const CachedTableRow& row = table_cache_.structure_model_rows[static_cast<size_t>(row_index)];
                const bool is_find_match =
                    static_cast<size_t>(row_index) < structure_model_find_.row_matches.size() &&
                    structure_model_find_.row_matches[static_cast<size_t>(row_index)] != 0;
                const bool is_unused_model =
                    static_cast<size_t>(row_index) < structure_model_find_.unused_row_matches.size() &&
                    structure_model_find_.unused_row_matches[static_cast<size_t>(row_index)] != 0;
                const bool is_preview_model = model_preview_canvas_ && model_preview_canvas_->has_model() &&
                    row.open_path == model_preview_canvas_->model_path();
                const ImU32 row_text_color = is_preview_model
                    ? ImGui::GetColorU32(preview_text_color)
                    : ImGui::GetColorU32(ImGuiCol_Text);
                ImGui::TableNextRow();
                if (is_unused_model) {
                    ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, kUnusedStructureModelRowColor);
                } else if (is_find_match) {
                    ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, kFindMatchRowColor);
                }
                if (row_index == scroll_target_row) {
                    ImGui::SetScrollHereY(0.0f);
                    structure_model_find_.scroll_row = -1;
                }
                ImGui::PushID(row_index);
                for (int i = 0; i < IM_ARRAYSIZE(kStructureModelColumns); ++i) {
                    ImGui::TableSetColumnIndex(i);
                    const std::string& value = row.cells[static_cast<size_t>(i)];
                    if (i == kStructureModelKeyColumn) {
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
                        touch_input::open_popup_on_last_item_long_press("structure_key_context");
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

void App::render_other_trains_window() {
    if (!show_other_trains_window_) return;
    if (dock_right_id_) ImGui::SetNextWindowDockID(dock_right_id_, ImGuiCond_FirstUseEver);
    std::string title = tr("frame.other_trains") + "###OtherTrains";
    if (!ImGui::Begin(title.c_str(), &show_other_trains_window_)) {
        ImGui::End();
        return;
    }
    if (!has_model_) {
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
    if (ImGui::BeginTable("other_trains", IM_ARRAYSIZE(kOtherTrainColumns) + 1,
                          ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                          ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollX,
                          definition_table_size)) {
        ImGui::TableSetupColumn(tr("column.show").c_str(), ImGuiTableColumnFlags_WidthFixed, kShowColumnWidth);
        for (int i = 0; i < IM_ARRAYSIZE(kOtherTrainColumns); ++i) {
            float width = kOtherTrainColumns[i].width;
            if (i == kOtherTrainDistanceColumn) width = table_cache_.other_train_distance_width;
            if (i == kOtherTrainFilePathColumn) width = table_cache_.other_train_file_path_width;
            ImGui::TableSetupColumn(kOtherTrainColumns[i].header,
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
                for (int i = 0; i < IM_ARRAYSIZE(kOtherTrainColumns); ++i) {
                    ImGui::TableSetColumnIndex(i + 1);
                    const std::string& value = row.cells[static_cast<size_t>(i)];
                    if (value.empty()) continue;
                    if (i == kOtherTrainFilePathColumn) {
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
            const CachedOtherTrainStopGroup& group = table_cache_.other_train_stop_groups[group_index];
            if (group.row_indices.empty()) continue;

            ImGui::Separator();
            std::string stop_title = tr("frame.other_train_stops") + " - [" + group.train_key + "]";
            ImGui::TextUnformatted(stop_title.c_str());

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
            if (!ImGui::BeginTable(table_id.c_str(), IM_ARRAYSIZE(kOtherTrainStopColumns),
                                   ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                                   ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollX,
                                   table_size)) {
                continue;
            }
            for (int i = 0; i < IM_ARRAYSIZE(kOtherTrainStopColumns); ++i) {
                float width = kOtherTrainStopColumns[i].width;
                if (i == kOtherTrainStopDistanceColumn) width = table_cache_.other_train_stop_distance_width;
                if (i == kOtherTrainStopFilePathColumn) width = table_cache_.other_train_stop_file_path_width;
                ImGui::TableSetupColumn(kOtherTrainStopColumns[i].header,
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
                        ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, kFindMatchRowColor);
                    }
                    if (group_row_index == scroll_target_group_row) {
                        ImGui::SetScrollHereY(0.5f);
                        other_train_stop_list_scroll_row_ = -1;
                    }
                    ImGui::PushID(static_cast<int>(stop_row_index));
                    for (int i = 0; i < IM_ARRAYSIZE(kOtherTrainStopColumns); ++i) {
                        ImGui::TableSetColumnIndex(i);
                        const std::string& value = row.cells[static_cast<size_t>(i)];
                        if (i == kOtherTrainStopDistanceColumn) {
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
                        if (i == kOtherTrainStopFilePathColumn) {
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
    ImGui::End();
}

void App::render_sound_file_find_panel(bool is_3d) {
    TableFindState& state = is_3d ? sound_3d_file_find_ : sound_file_find_;
    const bool stale_find_results = state.has_run && state.committed != state.query;
    if (stale_find_results) reset_sound_file_find_results(is_3d);

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
    ensure_table_cache();
    render_sound_file_find_panel(false);
    render_sound_list_table("sound_list",
                            table_cache_.sound_list_rows,
                            table_cache_.sound_list_file_path_width,
                            table_cache_.sound_list_buffer_count_width,
                            sound_file_find_,
                            tr("column.file_name"),
                            tr("menu.open_in_explorer"));
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
    ensure_table_cache();
    render_sound_file_find_panel(true);
    render_sound_list_table("sound_3d_list",
                            table_cache_.sound_3d_list_rows,
                            table_cache_.sound_3d_list_file_path_width,
                            table_cache_.sound_3d_list_buffer_count_width,
                            sound_3d_file_find_,
                            tr("column.file_name"),
                            tr("menu.open_in_explorer"));
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
                    ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, kInvalidTrackKeyRowColor);
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
                for (int i = 0; i < IM_ARRAYSIZE(kRepeaterColumns); ++i) {
                    ImGui::TableSetColumnIndex(i + 1);
                    const std::string& value = row.cells[static_cast<size_t>(i)];
                    if (i == kRepeaterDistanceColumn) {
                        size_t marker_index = static_cast<size_t>(row_index);
                        bool can_locate = marker_index < repeater_marker_cache_.size() &&
                            repeater_marker_cache_[marker_index].begin_marker.has_value();
                        const bool can_locate_scene = can_locate_scene_preview && !row.invalid_track_key;
                        TextCellContextAction action = render_text_cell_with_context_actions(
                            value,
                            tr("menu.locate_on_plan"),
                            can_locate,
                            tr("menu.locate_in_scene_preview"),
                            can_locate_scene);
                        if (action == TextCellContextAction::Primary) {
                            locate_repeater_row_on_plan(marker_index);
                        } else if (action == TextCellContextAction::Secondary) {
                            locate_repeater_row_in_scene_preview(marker_index);
                        }
                        continue;
                    }
                    if (value.empty()) continue;
                    if (i == kRepeaterStructureKeysColumn) {
                        ImGui::PushID("structure_keys");
                        std::vector<std::string> structure_keys = split_structure_key_list(value);
                        std::string selected_key = render_text_cell_with_submenu(
                            value, tr("menu.find_in_structure_models"), structure_keys);
                        if (!selected_key.empty()) find_structure_model_for_structure_key(selected_key);
                        ImGui::PopID();
                    } else if (i == kRepeaterFilePathColumn) {
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

    const int column_count = static_cast<int>(kSignalAspectStructureKeyColumnOffset +
        table_cache_.signal_aspect_structure_key_columns);
    if (ImGui::BeginTable("signal_aspects", column_count,
                          ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                          ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollX |
                          ImGuiTableFlags_ScrollY)) {
        for (int i = 0; i < IM_ARRAYSIZE(kSignalAspectFixedColumns); ++i) {
            ImGui::TableSetupColumn(kSignalAspectFixedColumns[i].header,
                                    ImGuiTableColumnFlags_WidthFixed,
                                    kSignalAspectFixedColumns[i].width);
        }
        std::vector<std::string> structure_headers;
        structure_headers.reserve(table_cache_.signal_aspect_structure_key_columns);
        for (size_t i = 0; i < table_cache_.signal_aspect_structure_key_columns; ++i) {
            structure_headers.push_back("structureKey" + std::to_string(i + 1));
            float width = i < table_cache_.signal_aspect_structure_key_widths.size()
                ? table_cache_.signal_aspect_structure_key_widths[i]
                : 120.0f;
            ImGui::TableSetupColumn(structure_headers.back().c_str(),
                                    ImGuiTableColumnFlags_WidthFixed, width);
        }
        setup_fixed_table_header();
        ImGui::TableHeadersRow();
        ImGuiListClipper clipper;
        const int row_count = static_cast<int>(table_cache_.signal_aspect_rows.size());
        if (signal_aspect_find_.scroll_row >= row_count) signal_aspect_find_.scroll_row = -1;
        const int scroll_target_row = signal_aspect_find_.scroll_row;
        clipper.Begin(row_count);
        if (scroll_target_row >= 0 && scroll_target_row < row_count) {
            clipper.IncludeItemByIndex(scroll_target_row);
        }
        while (clipper.Step()) {
            for (int row_index = clipper.DisplayStart; row_index < clipper.DisplayEnd; ++row_index) {
                const CachedTableRow& row = table_cache_.signal_aspect_rows[static_cast<size_t>(row_index)];
                const bool is_find_match =
                    static_cast<size_t>(row_index) < signal_aspect_find_.row_matches.size() &&
                    signal_aspect_find_.row_matches[static_cast<size_t>(row_index)] != 0;
                const bool is_unused =
                    static_cast<size_t>(row_index) < signal_aspect_find_.unused_row_matches.size() &&
                    signal_aspect_find_.unused_row_matches[static_cast<size_t>(row_index)] != 0;
                ImGui::TableNextRow();
                if (is_unused) {
                    ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, kUnusedStructureModelRowColor);
                } else if (is_find_match) {
                    ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, kFindMatchRowColor);
                }
                if (row_index == scroll_target_row) {
                    ImGui::SetScrollHereY(0.0f);
                    signal_aspect_find_.scroll_row = -1;
                }
                ImGui::PushID(row_index);
                for (int i = 0; i < column_count; ++i) {
                    ImGui::TableSetColumnIndex(i);
                    const std::string& value = row.cells[static_cast<size_t>(i)];
                    if (i >= kSignalAspectStructureKeyColumnOffset) {
                        ImGui::PushID(i);
                        if (render_text_cell_with_context(value, tr("menu.find_in_structure_models"),
                                                          !blank_ascii(value))) {
                            find_structure_model_for_structure_key(value);
                        }
                        ImGui::PopID();
                    } else if (!value.empty()) {
                        ImGui::TextUnformatted(value.c_str());
                    }
                }
                ImGui::PopID();
            }
        }
        ImGui::EndTable();
    }
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
    if (ImGui::BeginTable("signals", IM_ARRAYSIZE(kSignalColumns) + 1,
                          ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                          ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollX |
                          ImGuiTableFlags_ScrollY)) {
        std::string file_name_header = tr("column.file_name");
        ImGui::TableSetupColumn(tr("column.show").c_str(), ImGuiTableColumnFlags_WidthFixed, kShowColumnWidth);
        for (int i = 0; i < IM_ARRAYSIZE(kSignalColumns); ++i) {
            float width = kSignalColumns[i].width;
            if (i == kSignalDistanceColumn) width = table_cache_.signal_distance_width;
            if (i == kSignalFilePathColumn) width = table_cache_.signal_file_path_width;
            const char* header = i == kSignalFilePathColumn
                ? file_name_header.c_str()
                : kSignalColumns[i].header;
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
        while (clipper.Step()) {
            for (int row_index = clipper.DisplayStart; row_index < clipper.DisplayEnd; ++row_index) {
                const CachedTableRow& row = table_cache_.signal_rows[static_cast<size_t>(row_index)];
                ImGui::TableNextRow();
                if (row_index == signal_list_highlight_row_) {
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
                for (int i = 0; i < IM_ARRAYSIZE(kSignalColumns); ++i) {
                    ImGui::TableSetColumnIndex(i + 1);
                    const std::string& value = row.cells[static_cast<size_t>(i)];
                    if (i == kSignalDistanceColumn) {
                        size_t marker_index = static_cast<size_t>(row_index);
                        bool can_locate = marker_index < signal_marker_cache_.size() &&
                            signal_marker_cache_[marker_index].has_value();
                        if (render_text_cell_with_context(value, tr("menu.locate_on_plan"), can_locate)) {
                            locate_signal_row_on_plan(marker_index);
                        }
                        continue;
                    }
                    if (value.empty()) continue;
                    if (i == kSignalSignalAspectKeyColumn) {
                        ImGui::PushID("signal_aspect_key");
                        if (render_text_cell_with_context(value, tr("menu.find_in_signal_aspects"),
                                                          !blank_ascii(value))) {
                            find_signal_aspect_for_signal_aspect_key(value);
                        }
                        ImGui::PopID();
                    } else if (i == kSignalFilePathColumn) {
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
    if (ImGui::BeginTable("beacons", IM_ARRAYSIZE(kBeaconColumns),
                          ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                          ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollX |
                          ImGuiTableFlags_ScrollY)) {
        std::string file_name_header = tr("column.file_name");
        for (int i = 0; i < IM_ARRAYSIZE(kBeaconColumns); ++i) {
            float width = kBeaconColumns[i].width;
            if (i == kBeaconDistanceColumn) width = table_cache_.beacon_distance_width;
            if (i == kBeaconFilePathColumn) width = table_cache_.beacon_file_path_width;
            const char* header = i == kBeaconFilePathColumn
                ? file_name_header.c_str()
                : kBeaconColumns[i].header;
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
                for (int i = 0; i < IM_ARRAYSIZE(kBeaconColumns); ++i) {
                    ImGui::TableSetColumnIndex(i);
                    const std::string& value = row.cells[static_cast<size_t>(i)];
                    if (i == kBeaconDistanceColumn) {
                        size_t marker_index = static_cast<size_t>(row_index);
                        bool can_locate = marker_index < beacon_marker_cache_.size() &&
                            beacon_marker_cache_[marker_index].has_value();
                        if (render_text_cell_with_context(value, tr("menu.locate_on_plan"), can_locate)) {
                            locate_beacon_row_on_plan(marker_index);
                        }
                        continue;
                    }
                    if (value.empty()) continue;
                    if (i == kBeaconFilePathColumn) {
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
    if (ImGui::BeginTable("irregularities", IM_ARRAYSIZE(kIrregularityColumns),
                          ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                          ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollX |
                          ImGuiTableFlags_ScrollY)) {
        std::string file_name_header = tr("column.file_name");
        for (int i = 0; i < IM_ARRAYSIZE(kIrregularityColumns); ++i) {
            float width = kIrregularityColumns[i].width;
            if (i == kIrregularityDistanceColumn) width = table_cache_.irregularity_distance_width;
            if (i == kIrregularityFilePathColumn) width = table_cache_.irregularity_file_path_width;
            const char* header = i == kIrregularityFilePathColumn
                ? file_name_header.c_str()
                : kIrregularityColumns[i].header;
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
        while (clipper.Step()) {
            for (int row_index = clipper.DisplayStart; row_index < clipper.DisplayEnd; ++row_index) {
                const CachedTableRow& row = table_cache_.irregularity_rows[static_cast<size_t>(row_index)];
                ImGui::TableNextRow();
                if (row_index == irregularity_list_highlight_row_) {
                    ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, highlight_color);
                }
                if (row_index == scroll_target_row) {
                    ImGui::SetScrollHereY(0.5f);
                    irregularity_list_scroll_row_ = -1;
                }
                ImGui::PushID(row_index);
                for (int i = 0; i < IM_ARRAYSIZE(kIrregularityColumns); ++i) {
                    ImGui::TableSetColumnIndex(i);
                    const std::string& value = row.cells[static_cast<size_t>(i)];
                    if (i == kIrregularityDistanceColumn) {
                        size_t marker_index = static_cast<size_t>(row_index);
                        bool can_locate = marker_index < irregularity_marker_cache_.size() &&
                            irregularity_marker_cache_[marker_index].has_value();
                        if (render_text_cell_with_context(value, tr("menu.locate_on_plan"), can_locate)) {
                            locate_irregularity_row_on_plan(marker_index);
                        }
                        continue;
                    }
                    if (value.empty()) continue;
                    if (i == kIrregularityFilePathColumn) {
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
        "rolling_noises", kRollingNoiseColumns,
        kRollingNoiseDistanceColumn, kRollingNoiseFilePathColumn,
        table_cache_.rolling_noise_rows,
        table_cache_.rolling_noise_distance_width,
        table_cache_.rolling_noise_file_path_width,
        tr("column.file_name"),
        tr("menu.locate_on_plan"),
        tr("menu.open_in_explorer"),
        rolling_noise_list_scroll_row_,
        rolling_noise_list_highlight_row_,
        table_row_highlight_color(theme_color_),
        [this](size_t marker_index) {
            return marker_index < rolling_noise_marker_cache_.size() &&
                rolling_noise_marker_cache_[marker_index].has_value();
        },
        [this](size_t marker_index) { locate_rolling_noise_row_on_plan(marker_index); });
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
        "map_sounds", kMapSoundColumns,
        kMapSoundDistanceColumn, kMapSoundKeyColumn, kMapSoundFilePathColumn,
        table_cache_.map_sound_rows,
        table_cache_.map_sound_distance_width,
        table_cache_.map_sound_file_path_width,
        tr("column.file_name"),
        tr("menu.locate_on_plan"),
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
        [this](const std::string& sound_key) { find_sound_file_for_sound_key(sound_key, false); });
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
        "map_sound_3d", kMapSoundColumns,
        kMapSoundDistanceColumn, kMapSoundKeyColumn, kMapSoundFilePathColumn,
        table_cache_.map_sound_3d_rows,
        table_cache_.map_sound_3d_distance_width,
        table_cache_.map_sound_3d_file_path_width,
        tr("column.file_name"),
        tr("menu.locate_on_plan"),
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
        [this](const std::string& sound_key) { find_sound_file_for_sound_key(sound_key, true); });
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
        "flange_noises", kFlangeNoiseColumns,
        kFlangeNoiseDistanceColumn, kFlangeNoiseFilePathColumn,
        table_cache_.flange_noise_rows,
        table_cache_.flange_noise_distance_width,
        table_cache_.flange_noise_file_path_width,
        tr("column.file_name"),
        tr("menu.locate_on_plan"),
        tr("menu.open_in_explorer"),
        flange_noise_list_scroll_row_,
        flange_noise_list_highlight_row_,
        table_row_highlight_color(theme_color_),
        [this](size_t marker_index) {
            return marker_index < flange_noise_marker_cache_.size() &&
                flange_noise_marker_cache_[marker_index].has_value();
        },
        [this](size_t marker_index) { locate_flange_noise_row_on_plan(marker_index); });
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
        "joint_noises", kJointNoiseColumns,
        kJointNoiseDistanceColumn, kJointNoiseFilePathColumn,
        table_cache_.joint_noise_rows,
        table_cache_.joint_noise_distance_width,
        table_cache_.joint_noise_file_path_width,
        tr("column.file_name"),
        tr("menu.locate_on_plan"),
        tr("menu.open_in_explorer"),
        joint_noise_list_scroll_row_,
        joint_noise_list_highlight_row_,
        table_row_highlight_color(theme_color_),
        [this](size_t marker_index) {
            return marker_index < joint_noise_marker_cache_.size() &&
                joint_noise_marker_cache_[marker_index].has_value();
        },
        [this](size_t marker_index) { locate_joint_noise_row_on_plan(marker_index); });
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
    if (ImGui::BeginTable("backgrounds", IM_ARRAYSIZE(kBackgroundColumns),
                          ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                          ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollX |
                          ImGuiTableFlags_ScrollY)) {
        std::string file_name_header = tr("column.file_name");
        for (int i = 0; i < IM_ARRAYSIZE(kBackgroundColumns); ++i) {
            float width = kBackgroundColumns[i].width;
            if (i == kBackgroundDistanceColumn) width = table_cache_.background_distance_width;
            if (i == kBackgroundFilePathColumn) width = table_cache_.background_file_path_width;
            const char* header = i == kBackgroundFilePathColumn
                ? file_name_header.c_str()
                : kBackgroundColumns[i].header;
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
                for (int i = 0; i < IM_ARRAYSIZE(kBackgroundColumns); ++i) {
                    ImGui::TableSetColumnIndex(i);
                    const std::string& value = row.cells[static_cast<size_t>(i)];
                    if (i == kBackgroundDistanceColumn) {
                        size_t marker_index = static_cast<size_t>(row_index);
                        bool can_locate = marker_index < background_marker_cache_.size() &&
                            background_marker_cache_[marker_index].has_value();
                        if (render_text_cell_with_context(value, tr("menu.locate_on_plan"), can_locate)) {
                            locate_background_row_on_plan(marker_index);
                        }
                        continue;
                    }
                    if (value.empty()) continue;
                    if (i == kBackgroundStructureKeyColumn) {
                        ImGui::PushID("background_structure_key");
                        if (render_text_cell_with_context(value, tr("menu.find_in_structure_models"), !blank_ascii(value))) {
                            find_structure_model_for_structure_key(value);
                        }
                        ImGui::PopID();
                    } else if (i == kBackgroundFilePathColumn) {
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
    if (ImGui::BeginTable("adhesions", IM_ARRAYSIZE(kAdhesionColumns),
                          ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                          ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollX |
                          ImGuiTableFlags_ScrollY)) {
        std::string file_name_header = tr("column.file_name");
        for (int i = 0; i < IM_ARRAYSIZE(kAdhesionColumns); ++i) {
            float width = kAdhesionColumns[i].width;
            if (i == kAdhesionDistanceColumn) width = table_cache_.adhesion_distance_width;
            if (i == kAdhesionFilePathColumn) width = table_cache_.adhesion_file_path_width;
            const char* header = i == kAdhesionFilePathColumn
                ? file_name_header.c_str()
                : kAdhesionColumns[i].header;
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
                for (int i = 0; i < IM_ARRAYSIZE(kAdhesionColumns); ++i) {
                    ImGui::TableSetColumnIndex(i);
                    const std::string& value = row.cells[static_cast<size_t>(i)];
                    if (i == kAdhesionDistanceColumn) {
                        size_t marker_index = static_cast<size_t>(row_index);
                        bool can_locate = marker_index < adhesion_marker_cache_.size() &&
                            adhesion_marker_cache_[marker_index].has_value();
                        if (render_text_cell_with_context(value, tr("menu.locate_on_plan"), can_locate)) {
                            locate_adhesion_row_on_plan(marker_index);
                        }
                        continue;
                    }
                    if (value.empty()) continue;
                    if (i == kAdhesionFilePathColumn) {
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
    if (ImGui::BeginTable("cab_illuminance", IM_ARRAYSIZE(kCabIlluminanceColumns),
                          ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                          ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollX |
                          ImGuiTableFlags_ScrollY)) {
        std::string file_name_header = tr("column.file_name");
        for (int i = 0; i < IM_ARRAYSIZE(kCabIlluminanceColumns); ++i) {
            float width = kCabIlluminanceColumns[i].width;
            if (i == kCabIlluminanceDistanceColumn) width = table_cache_.cab_illuminance_distance_width;
            if (i == kCabIlluminanceFilePathColumn) width = table_cache_.cab_illuminance_file_path_width;
            const char* header = i == kCabIlluminanceFilePathColumn
                ? file_name_header.c_str()
                : kCabIlluminanceColumns[i].header;
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
                for (int i = 0; i < IM_ARRAYSIZE(kCabIlluminanceColumns); ++i) {
                    ImGui::TableSetColumnIndex(i);
                    const std::string& value = row.cells[static_cast<size_t>(i)];
                    if (i == kCabIlluminanceDistanceColumn) {
                        size_t marker_index = static_cast<size_t>(row_index);
                        bool can_locate = marker_index < cab_illuminance_marker_cache_.size() &&
                            cab_illuminance_marker_cache_[marker_index].has_value();
                        if (render_text_cell_with_context(value, tr("menu.locate_on_plan"), can_locate)) {
                            locate_cab_illuminance_row_on_plan(marker_index);
                        }
                        continue;
                    }
                    if (value.empty()) continue;
                    if (i == kCabIlluminanceFilePathColumn) {
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
    if (ImGui::BeginTable("fogs", IM_ARRAYSIZE(kFogColumns),
                          ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                          ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollX |
                          ImGuiTableFlags_ScrollY)) {
        std::string file_name_header = tr("column.file_name");
        for (int i = 0; i < IM_ARRAYSIZE(kFogColumns); ++i) {
            float width = kFogColumns[i].width;
            if (i == kFogDistanceColumn) width = table_cache_.fog_distance_width;
            if (i == kFogFilePathColumn) width = table_cache_.fog_file_path_width;
            const char* header = i == kFogFilePathColumn
                ? file_name_header.c_str()
                : kFogColumns[i].header;
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
                for (int i = 0; i < IM_ARRAYSIZE(kFogColumns); ++i) {
                    ImGui::TableSetColumnIndex(i);
                    const std::string& value = row.cells[static_cast<size_t>(i)];
                    if (i == kFogDistanceColumn) {
                        size_t marker_index = static_cast<size_t>(row_index);
                        bool can_locate = marker_index < fog_marker_cache_.size() &&
                            fog_marker_cache_[marker_index].has_value();
                        if (render_text_cell_with_context(value, tr("menu.locate_on_plan"), can_locate)) {
                            locate_fog_row_on_plan(marker_index);
                        }
                        continue;
                    }
                    if (value.empty()) continue;
                    if (i == kFogFilePathColumn) {
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
