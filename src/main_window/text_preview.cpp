/*
 * Copyright (c) 2026 Sapporo_ningyo
 *
 * Licensed under Apache License 2.0; see LICENSE and NOTICE.
 * The GUI uses Dear ImGui; see THIRD_PARTY_NOTICES.md.
 */

#pragma execution_character_set("utf-8")

#include "kme.h"
#include "text_decoder.h"

#include "imgui.h"

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <climits>
#include <cstdio>
#include <exception>
#include <string>
#include <utility>

namespace {

bool ascii_suffix_equal(const std::string& text, const char* suffix) {
    const size_t suffix_size = std::char_traits<char>::length(suffix);
    if (text.size() < suffix_size) return false;
    const size_t offset = text.size() - suffix_size;
    for (size_t i = 0; i < suffix_size; ++i) {
        const unsigned char left = static_cast<unsigned char>(text[offset + i]);
        const unsigned char right = static_cast<unsigned char>(suffix[i]);
        if (std::tolower(left) != std::tolower(right)) return false;
    }
    return true;
}

void build_text_preview_lines(TextPreviewState& preview) {
    preview.lines.clear();
    preview.lines.reserve(
        static_cast<size_t>(std::count(preview.text.begin(), preview.text.end(), '\n')) + 1);

    size_t line_begin = 0;
    for (size_t i = 0; i < preview.text.size(); ++i) {
        const char ch = preview.text[i];
        if (ch != '\r' && ch != '\n') continue;
        preview.lines.push_back({line_begin, i});
        if (ch == '\r' && i + 1 < preview.text.size() && preview.text[i + 1] == '\n') ++i;
        line_begin = i + 1;
    }
    preview.lines.push_back({line_begin, preview.text.size()});
}

std::string decode_preview_bytes(const std::string& bytes, std::string& encoding) {
    using namespace kme::maploader;
    if (!encoding.empty()) return decode_text_bytes(bytes, encoding);

    if (bytes.size() >= 2 &&
        static_cast<unsigned char>(bytes[0]) == 0xff &&
        static_cast<unsigned char>(bytes[1]) == 0xfe) {
        encoding = "utf-16le";
        return decode_text_bytes(bytes, encoding);
    }
    if (bytes.size() >= 2 &&
        static_cast<unsigned char>(bytes[0]) == 0xfe &&
        static_cast<unsigned char>(bytes[1]) == 0xff) {
        encoding = "utf-16be";
        return decode_text_bytes(bytes, encoding);
    }
    try {
        encoding = "utf-8";
        return decode_text_bytes(bytes, encoding);
    } catch (...) {
        encoding = "cp932";
        return decode_text_bytes(bytes, encoding);
    }
}

} // namespace

bool App::is_supported_text_preview_file(const std::string& file_path) {
    return ascii_suffix_equal(file_path, ".txt") || ascii_suffix_equal(file_path, ".csv");
}

void App::open_text_preview(const std::string& file_path) {
    if (!is_supported_text_preview_file(file_path)) return;

    TextPreviewState next;
    next.open = true;
    next.focus_next = true;
    next.file_path = file_path;
    for (const EditSourceFileInfo& file : model_.edit_files) {
        if (file.file_path == file_path) {
            next.encoding = file.encoding;
            break;
        }
    }

    try {
        const std::string bytes =
            kme::maploader::read_binary_file(kme::maploader::path_from_utf8(file_path));
        next.text = decode_preview_bytes(bytes, next.encoding);
        build_text_preview_lines(next);
    } catch (const std::exception& e) {
        next.error = e.what();
        add_log(LogSeverity::Error, "Text preview failed: " + next.error);
    }

    text_preview_ = std::move(next);
}

void App::refresh_text_preview_after_map_load() {
    if (!text_preview_.open) return;

    std::string target_path;
    for (const FileStructureNode& node : model_.file_structure) {
        if (node.absolute_path == text_preview_.file_path &&
            is_supported_text_preview_file(node.absolute_path)) {
            target_path = node.absolute_path;
            break;
        }
    }
    if (target_path.empty() && !model_.file_structure.empty() &&
        is_supported_text_preview_file(model_.file_structure.front().absolute_path)) {
        target_path = model_.file_structure.front().absolute_path;
    }
    if (target_path.empty()) {
        text_preview_ = TextPreviewState{};
        return;
    }

    open_text_preview(target_path);
    text_preview_.focus_next = false;
}

void App::render_text_preview_window() {
    if (!text_preview_.open) return;
    if (dock_main_id_) ImGui::SetNextWindowDockID(dock_main_id_, ImGuiCond_FirstUseEver);
    if (text_preview_.focus_next) ImGui::SetNextWindowFocus();

    std::string title = tr("frame.text_preview") + "###TextPreview";
    if (!ImGui::Begin(title.c_str(), &text_preview_.open)) {
        text_preview_.focus_next = false;
        ImGui::End();
        return;
    }
    text_preview_.focus_next = false;

    ImGui::TextDisabled("%s", text_preview_.file_path.c_str());
    if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::TextUnformatted(text_preview_.file_path.c_str());
        ImGui::EndTooltip();
    }
    ImGui::Separator();

    if (!text_preview_.error.empty()) {
        ImGui::TextWrapped("%s\n%s", tr("status.text_preview_load_failed").c_str(),
                           text_preview_.error.c_str());
        ImGui::End();
        return;
    }

    const ImFont* font = ImGui::GetFont();
    const float font_size = ImGui::GetFontSize();
    if (text_preview_.measured_font != font || text_preview_.measured_font_size != font_size) {
        text_preview_.measured_font = font;
        text_preview_.measured_font_size = font_size;
        text_preview_.max_text_width = ImGui::CalcTextSize(
            text_preview_.text.data(),
            text_preview_.text.data() + text_preview_.text.size(), false).x;
    }

    char max_line_number[32] = {};
    std::snprintf(max_line_number, sizeof(max_line_number), "%zu", text_preview_.lines.size());
    const ImGuiStyle& style = ImGui::GetStyle();
    const float gutter_width =
        ImGui::CalcTextSize(max_line_number).x + style.CellPadding.x * 2.0f;
    const float available_width = ImGui::GetContentRegionAvail().x;
    const float text_column_width = std::max(
        text_preview_.max_text_width + style.CellPadding.x * 2.0f,
        std::max(1.0f, available_width - gutter_width));

    const ImGuiTableFlags flags = ImGuiTableFlags_BordersInnerV |
        ImGuiTableFlags_NoSavedSettings | ImGuiTableFlags_RowBg |
        ImGuiTableFlags_ScrollX | ImGuiTableFlags_ScrollY |
        ImGuiTableFlags_SizingFixedFit;
    if (ImGui::BeginTable("text_preview_table", 2, flags, ImVec2(0.0f, 0.0f))) {
        ImGui::TableSetupColumn("##line_number", ImGuiTableColumnFlags_WidthFixed, gutter_width);
        ImGui::TableSetupColumn("##text", ImGuiTableColumnFlags_WidthFixed, text_column_width);
        ImGui::TableSetupScrollFreeze(1, 0);

        const int line_count = static_cast<int>(
            std::min(text_preview_.lines.size(), static_cast<size_t>(INT_MAX)));
        ImGuiListClipper clipper;
        clipper.Begin(line_count);
        while (clipper.Step()) {
            for (int line_index = clipper.DisplayStart;
                 line_index < clipper.DisplayEnd; ++line_index) {
                const size_t index = static_cast<size_t>(line_index);
                const bool selected =
                    text_preview_.selection.kind == TextPreviewSelectionKind::Line &&
                    text_preview_.selection.index == index;

                ImGui::TableNextRow();
                if (selected) {
                    ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0,
                                           ImGui::GetColorU32(ImGuiCol_Header));
                }

                ImGui::TableSetColumnIndex(0);
                char line_number[32] = {};
                std::snprintf(line_number, sizeof(line_number), "%zu", index + 1);
                const float number_width = ImGui::CalcTextSize(line_number).x;
                const float cursor_x = ImGui::GetCursorPosX();
                ImGui::SetCursorPosX(
                    cursor_x + std::max(0.0f, ImGui::GetContentRegionAvail().x - number_width));
                ImGui::TextDisabled("%s", line_number);

                ImGui::TableSetColumnIndex(1);
                ImGui::PushID(line_index);
                if (ImGui::Selectable("##text_preview_line", selected,
                                      ImGuiSelectableFlags_None,
                                      ImVec2(-FLT_MIN, ImGui::GetTextLineHeight()))) {
                    text_preview_.selection.kind = TextPreviewSelectionKind::Line;
                    text_preview_.selection.index = index;
                }
                const ImVec2 text_position = ImGui::GetItemRectMin();
                const TextPreviewLineRange& line = text_preview_.lines[index];
                ImGui::GetWindowDrawList()->AddText(
                    text_position, ImGui::GetColorU32(ImGuiCol_Text),
                    text_preview_.text.data() + line.begin,
                    text_preview_.text.data() + line.end);
                ImGui::PopID();
            }
        }
        ImGui::EndTable();
    }
    ImGui::End();
}
