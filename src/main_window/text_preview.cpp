/*
 * Copyright (c) 2026 Sapporo_ningyo
 *
 * Licensed under Apache License 2.0; see LICENSE and NOTICE.
 * The GUI uses Dear ImGui; see THIRD_PARTY_NOTICES.md.
 */

#ifdef _MSC_VER
#pragma execution_character_set("utf-8")
#endif

#include "kme.h"
#include "maploader.h"
#include "text_decoder.h"

#include "imgui.h"

#include <algorithm>
#include <cctype>
#include <climits>
#include <cstdio>
#include <exception>
#include <stdexcept>
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

using BoundaryIterator = std::vector<DistanceResolutionBoundary>::const_iterator;

std::pair<BoundaryIterator, BoundaryIterator> boundary_range_for_line(
    const TextPreviewPlacementState& placement, int line) {
    const auto first = std::lower_bound(
        placement.allowed_boundaries.begin(), placement.allowed_boundaries.end(), line,
        [](const DistanceResolutionBoundary& boundary, int target_line) {
            return boundary.line < target_line;
        });
    const auto last = std::upper_bound(
        first, placement.allowed_boundaries.end(), line,
        [](int target_line, const DistanceResolutionBoundary& boundary) {
            return target_line < boundary.line;
        });
    return {first, last};
}

const DistanceResolutionBoundary* gap_boundary_for_line(
    const TextPreviewPlacementState& placement, int line) {
    const auto range = boundary_range_for_line(placement, line);
    const auto found = std::find_if(range.first, range.second,
                                    [](const DistanceResolutionBoundary& boundary) {
                                        return boundary.column <= 1;
                                    });
    return found == range.second ? nullptr : &*found;
}

const DistanceResolutionBoundary* eof_boundary(
    const TextPreviewPlacementState& placement, size_t line_count) {
    const int first_eof_line = line_count >= static_cast<size_t>(INT_MAX)
        ? INT_MAX
        : static_cast<int>(line_count + 1);
    const auto first = std::lower_bound(
        placement.allowed_boundaries.begin(), placement.allowed_boundaries.end(), first_eof_line,
        [](const DistanceResolutionBoundary& boundary, int target_line) {
            return boundary.line < target_line;
        });
    const auto found = std::find_if(
        first, placement.allowed_boundaries.end(),
        [](const DistanceResolutionBoundary& boundary) { return boundary.column <= 1; });
    return found == placement.allowed_boundaries.end() ? nullptr : &*found;
}

struct BoundaryMarkerStyle {
    bool highlighted = false;
    ImU32 color = 0;
    float thickness = 1.5f;
};

BoundaryMarkerStyle boundary_marker_style(const TextPreviewPlacementState& placement,
                                          const DistanceResolutionBoundary& boundary,
                                          bool hovered) {
    BoundaryMarkerStyle style;
    style.highlighted = hovered || placement.selected_boundary_token == boundary.token;
    style.color = ImGui::GetColorU32(
        style.highlighted ? ImGuiCol_HeaderHovered : ImGuiCol_Header);
    style.thickness = style.highlighted ? 3.0f : (boundary.recommended ? 2.5f : 1.5f);
    return style;
}

bool render_gap_boundary_marker(const TextPreviewPlacementState& placement,
                                const DistanceResolutionBoundary& boundary,
                                const ImVec2& gutter_position,
                                float gutter_content_width,
                                float y) {
    const ImVec2 window_position = ImGui::GetWindowPos();
    const float visible_left = window_position.x + ImGui::GetWindowContentRegionMin().x;
    const float visible_right = window_position.x + ImGui::GetWindowContentRegionMax().x;
    const bool hovered = ImGui::IsWindowHovered() && ImGui::IsMouseHoveringRect(
        ImVec2(visible_left, y - 5.0f), ImVec2(visible_right, y + 5.0f), false);
    const BoundaryMarkerStyle marker = boundary_marker_style(placement, boundary, hovered);

    const float arrow_tip_x = gutter_position.x + std::max(1.0f, gutter_content_width);
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    draw_list->PushClipRect(ImVec2(visible_left, y - 6.0f),
                            ImVec2(visible_right, y + 6.0f), false);
    draw_list->AddLine(ImVec2(arrow_tip_x, y), ImVec2(visible_right, y),
                       marker.color, marker.thickness);
    if (marker.highlighted) {
        const float arrow_width = std::min(9.0f, std::max(5.0f, gutter_content_width * 0.45f));
        const float arrow_half_height = 4.5f;
        draw_list->AddTriangleFilled(
            ImVec2(arrow_tip_x - arrow_width, y - arrow_half_height),
            ImVec2(arrow_tip_x - arrow_width, y + arrow_half_height),
            ImVec2(arrow_tip_x, y), marker.color);
    }
    draw_list->PopClipRect();
    return hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left);
}

size_t utf8_byte_for_source_column(const std::string& text,
                                   const TextPreviewLineRange& line,
                                   int column) {
    size_t pos = line.begin;
    int current_column = 1;
    while (pos < line.end && current_column < std::max(1, column)) {
        const unsigned char lead = static_cast<unsigned char>(text[pos]);
        size_t length = 1;
        if ((lead & 0xe0) == 0xc0) length = 2;
        else if ((lead & 0xf0) == 0xe0) length = 3;
        else if ((lead & 0xf8) == 0xf0) length = 4;
        pos = std::min(line.end, pos + length);
        ++current_column;
    }
    return pos;
}

} // namespace

bool App::is_supported_text_preview_file(const std::string& file_path) {
    return ascii_suffix_equal(file_path, ".txt") ||
        ascii_suffix_equal(file_path, ".csv");
}

bool App::load_text_preview_content(TextPreviewState& preview) {
    try {
        std::string text;
        if (preview.parser_source && handle_) {
            const char* current_text = kv_get_source_text(handle_, preview.file_path.c_str());
            if (!current_text) {
                const char* error = kv_get_last_error();
                throw std::runtime_error(error ? error : "unable to read map working copy");
            }
            text = current_text;
            kv_free_string(current_text);
        } else {
            const std::string bytes = kme::maploader::read_binary_file(
                kme::maploader::path_from_utf8(preview.file_path));
            text = decode_preview_bytes(bytes, preview.encoding);
        }

        preview.text = std::move(text);
        build_text_preview_lines(preview);
        preview.error.clear();
        preview.measured_font = nullptr;
        preview.measured_font_size = 0.0f;
        preview.max_text_width = 0.0f;
        return true;
    } catch (const std::exception& e) {
        preview.error = e.what();
        KME_ADD_LOG(LogSeverity::Error, "Text preview failed: " + preview.error);
        return false;
    }
}

void App::refresh_text_preview_from_working_copy() {
    if (!text_preview_.open || !text_preview_.parser_source) return;
    load_text_preview_content(text_preview_);
}

void App::open_text_preview(const std::string& file_path, bool parser_confirmed_source) {
    if (!parser_confirmed_source && !is_supported_text_preview_file(file_path)) return;

    TextPreviewState next;
    next.open = true;
    next.focus_next = true;
    next.parser_source = parser_confirmed_source;
    next.file_path = file_path;
    for (const EditSourceFileInfo& file : model_.edit_files) {
        if (file.file_path == file_path) {
            next.encoding = file.encoding;
            break;
        }
    }

    load_text_preview_content(next);
    text_preview_ = std::move(next);
}

void App::open_text_preview_for_distance_resolution(const DistanceResolutionRequest& request) {
    // The parser accepts Include files regardless of extension. The planner only emits
    // source_file values backed by parsed SourceSpan data, so those files are safe to
    // open even when the normal file-browser extension filter would hide them.
    open_text_preview(request.source_file, true);
    if (!text_preview_.open || text_preview_.file_path != request.source_file ||
        !text_preview_.error.empty()) {
        KME_ADD_LOG("[error]unable to open the requested distance "
                "source file for boundary selection: " + request.source_file);
        cancel_distance_resolution_workflow();
        return;
    }

    TextPreviewPlacementState placement;
    placement.active = true;
    placement.resolution_key = request.resolution_key;
    placement.insertion_preview = request.insertion_preview;
    placement.allowed_boundaries = request.allowed_boundaries;
    std::stable_sort(
        placement.allowed_boundaries.begin(), placement.allowed_boundaries.end(),
        [](const DistanceResolutionBoundary& left,
           const DistanceResolutionBoundary& right) {
            if (left.line != right.line) return left.line < right.line;
            if (left.column != right.column) return left.column < right.column;
            return left.token < right.token;
        });

    const DistanceResolutionBoundary* suggested = nullptr;
    for (const DistanceResolutionBoundary& boundary : placement.allowed_boundaries) {
        if (boundary.recommended) {
            suggested = &boundary;
            break;
        }
    }
    if (!suggested && !placement.allowed_boundaries.empty()) {
        suggested = &placement.allowed_boundaries.front();
    }
    if (suggested) {
        placement.scroll_to_line = std::max(1, suggested->line);
        placement.scroll_pending = true;
    }
    text_preview_.selection = TextPreviewSelection{};
    text_preview_.placement = std::move(placement);
    text_preview_.focus_next = true;
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

    open_text_preview(target_path, true);
    text_preview_.focus_next = false;
}

void App::render_text_preview_window() {
    if (!text_preview_.open) {
        if (text_preview_.placement.active) cancel_distance_resolution_workflow();
        return;
    }
    const bool placement_was_active = text_preview_.placement.active;
    if (dock_main_id_) ImGui::SetNextWindowDockID(dock_main_id_, ImGuiCond_FirstUseEver);
    if (text_preview_.focus_next) ImGui::SetNextWindowFocus();

    std::string title = tr("frame.text_preview") + "###TextPreview";
    if (!ImGui::Begin(title.c_str(), &text_preview_.open)) {
        const bool closed_resolution = text_preview_.placement.active && !text_preview_.open;
        text_preview_.focus_next = false;
        ImGui::End();
        if (closed_resolution) cancel_distance_resolution_workflow();
        return;
    }
    text_preview_.focus_next = false;

    bool cancel_resolution = false;
    bool confirm_resolution = false;
    if (text_preview_.placement.active) {
        const ImGuiTableFlags header_flags = ImGuiTableFlags_NoSavedSettings |
            ImGuiTableFlags_SizingStretchProp;
        if (ImGui::BeginTable("text_preview_placement_header", 2, header_flags)) {
            ImGui::TableSetupColumn("##placement_context", ImGuiTableColumnFlags_WidthStretch, 1.0f);
            ImGui::TableSetupColumn("##placement_statement", ImGuiTableColumnFlags_WidthFixed,
                                    std::min(420.0f, std::max(240.0f, ImGui::GetContentRegionAvail().x * 0.44f)));
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextDisabled("%s", text_preview_.file_path.c_str());
            if (ImGui::IsItemHovered()) {
                ImGui::BeginTooltip();
                ImGui::TextUnformatted(text_preview_.file_path.c_str());
                ImGui::EndTooltip();
            }
            ImGui::TextWrapped("%s", tr("hint.distance_choose_boundary").c_str());
            if (text_preview_.placement.allowed_boundaries.empty()) {
                ImGui::TextWrapped("%s", tr("status.distance_no_boundaries").c_str());
            }
            if (ImGui::Button(tr("button.cancel").c_str())) cancel_resolution = true;
            ImGui::SameLine();
            ImGui::BeginDisabled(text_preview_.placement.selected_boundary_token.empty());
            if (ImGui::Button(tr("button.ok").c_str())) confirm_resolution = true;
            ImGui::EndDisabled();

            ImGui::TableSetColumnIndex(1);
            ImGui::BeginChild("distance_insertion_preview", ImVec2(0.0f, 112.0f), true,
                              ImGuiWindowFlags_HorizontalScrollbar);
            ImGui::TextUnformatted(tr("label.pending_insertion").c_str());
            ImGui::Separator();
            ImGui::TextUnformatted(text_preview_.placement.insertion_preview.c_str());
            ImGui::EndChild();
            ImGui::EndTable();
        }
    } else {
        ImGui::TextDisabled("%s", text_preview_.file_path.c_str());
        if (ImGui::IsItemHovered()) {
            ImGui::BeginTooltip();
            ImGui::TextUnformatted(text_preview_.file_path.c_str());
            ImGui::EndTooltip();
        }
    }
    ImGui::Separator();

    if (cancel_resolution) {
        cancel_distance_resolution_workflow();
        ImGui::End();
        return;
    }
    if (confirm_resolution) {
        confirm_distance_resolution_boundary();
        ImGui::End();
        return;
    }

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
    std::string selected_boundary_token;
    if (ImGui::BeginTable("text_preview_table", 2, flags, ImVec2(0.0f, 0.0f))) {
        ImGui::TableSetupColumn("##line_number", ImGuiTableColumnFlags_WidthFixed, gutter_width);
        ImGui::TableSetupColumn("##text", ImGuiTableColumnFlags_WidthFixed, text_column_width);
        ImGui::TableSetupScrollFreeze(1, 0);

        if (text_preview_.placement.active && text_preview_.placement.scroll_pending) {
            const int line_count = static_cast<int>(
                std::min(text_preview_.lines.size(), static_cast<size_t>(INT_MAX)));
            const int target_line = std::clamp(text_preview_.placement.scroll_to_line, 1,
                                               std::max(1, line_count + 1));
            const float target_y = static_cast<float>(std::max(0, target_line - 3)) *
                ImGui::GetTextLineHeightWithSpacing();
            ImGui::SetScrollY(target_y);
            text_preview_.placement.scroll_pending = false;
        }

        const int line_count = static_cast<int>(
            std::min(text_preview_.lines.size(), static_cast<size_t>(INT_MAX)));
        ImGuiListClipper clipper;
        clipper.Begin(line_count);
        while (clipper.Step()) {
            for (int line_index = clipper.DisplayStart;
                 line_index < clipper.DisplayEnd; ++line_index) {
                const size_t index = static_cast<size_t>(line_index);
                const int source_line = line_index + 1;
                const DistanceResolutionBoundary* gap_boundary = text_preview_.placement.active
                    ? gap_boundary_for_line(text_preview_.placement, source_line)
                    : nullptr;
                const bool selected =
                    text_preview_.selection.kind == TextPreviewSelectionKind::Line &&
                    text_preview_.selection.index == index;

                ImGui::TableNextRow();
                if (selected) {
                    ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0,
                                           ImGui::GetColorU32(ImGuiCol_Header));
                }

                ImGui::TableSetColumnIndex(0);
                const ImVec2 gutter_position = ImGui::GetCursorScreenPos();
                const float gutter_content_width =
                    std::max(1.0f, ImGui::GetContentRegionAvail().x);
                if (gap_boundary) {
                    const float boundary_y = gutter_position.y - style.CellPadding.y;
                    if (render_gap_boundary_marker(
                            text_preview_.placement, *gap_boundary, gutter_position,
                            gutter_content_width, boundary_y)) {
                        selected_boundary_token = gap_boundary->token;
                    }
                }

                char line_number[32] = {};
                std::snprintf(line_number, sizeof(line_number), "%zu", index + 1);
                const float number_width = ImGui::CalcTextSize(line_number).x;
                const float cursor_x = ImGui::GetCursorPosX();
                ImGui::SetCursorPosX(
                    cursor_x + std::max(0.0f, ImGui::GetContentRegionAvail().x - number_width));
                ImGui::TextDisabled("%s", line_number);

                ImGui::TableSetColumnIndex(1);
                ImGui::PushID(line_index);
                const ImVec2 text_position = ImGui::GetCursorScreenPos();
                bool line_activated = false;
                if (!text_preview_.placement.active) {
                    line_activated = ImGui::Selectable(
                        "##text_preview_line", selected,
                        ImGuiSelectableFlags_SpanAllColumns,
                        ImVec2(0.0f, ImGui::GetTextLineHeight()));
                } else {
                    ImGui::Dummy(ImVec2(
                        std::max(1.0f, ImGui::GetContentRegionAvail().x),
                        ImGui::GetTextLineHeight()));
                }
                if (line_activated) {
                    text_preview_.selection.kind = TextPreviewSelectionKind::Line;
                    text_preview_.selection.index = index;
                }
                const TextPreviewLineRange& line = text_preview_.lines[index];
                ImGui::GetWindowDrawList()->AddText(
                    text_position, ImGui::GetColorU32(ImGuiCol_Text),
                    text_preview_.text.data() + line.begin,
                    text_preview_.text.data() + line.end);
                if (text_preview_.placement.active) {
                    const auto boundaries = boundary_range_for_line(
                        text_preview_.placement, source_line);
                    for (auto boundary_it = boundaries.first;
                         boundary_it != boundaries.second; ++boundary_it) {
                        const DistanceResolutionBoundary& boundary = *boundary_it;
                        if (boundary.column <= 1) continue;

                        const size_t marker_byte = utf8_byte_for_source_column(
                            text_preview_.text, line, boundary.column);
                        const float prefix_width = ImGui::CalcTextSize(
                            text_preview_.text.data() + line.begin,
                            text_preview_.text.data() + marker_byte, false).x;
                        const float marker_x = text_position.x + prefix_width;
                        const float marker_top = text_position.y - 2.0f;
                        const float marker_bottom =
                            text_position.y + ImGui::GetTextLineHeight() + 2.0f;
                        const bool marker_hovered = ImGui::IsMouseHoveringRect(
                            ImVec2(marker_x - 5.0f, marker_top),
                            ImVec2(marker_x + 5.0f, marker_bottom));
                        const BoundaryMarkerStyle marker = boundary_marker_style(
                            text_preview_.placement, boundary, marker_hovered);
                        ImGui::GetWindowDrawList()->AddLine(
                            ImVec2(marker_x, marker_top), ImVec2(marker_x, marker_bottom),
                            marker.color, marker.thickness);
                        ImGui::GetWindowDrawList()->AddCircleFilled(
                            ImVec2(marker_x, marker_top + 2.0f),
                            (marker.highlighted || boundary.recommended) ? 3.5f : 2.5f,
                            marker.color);
                        if (marker_hovered &&
                            ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                            selected_boundary_token = boundary.token;
                        }
                    }
                }
                ImGui::PopID();
            }
        }

        if (text_preview_.placement.active) {
            const DistanceResolutionBoundary* boundary =
                eof_boundary(text_preview_.placement, text_preview_.lines.size());
            if (boundary) {
                ImGui::TableNextRow(ImGuiTableRowFlags_None, ImGui::GetTextLineHeight() * 0.6f);
                ImGui::TableSetColumnIndex(0);
                const ImVec2 gutter_position = ImGui::GetCursorScreenPos();
                const float gutter_content_width =
                    std::max(1.0f, ImGui::GetContentRegionAvail().x);
                const float boundary_y = gutter_position.y - style.CellPadding.y;
                if (render_gap_boundary_marker(
                        text_preview_.placement, *boundary, gutter_position,
                        gutter_content_width, boundary_y)) {
                    selected_boundary_token = boundary->token;
                }
                ImGui::TableSetColumnIndex(1);
                ImGui::Dummy(ImVec2(std::max(1.0f, ImGui::GetContentRegionAvail().x),
                                    ImGui::GetTextLineHeight() * 0.5f));
            }
        }
        ImGui::EndTable();
    }
    if (!selected_boundary_token.empty()) {
        select_distance_resolution_boundary(selected_boundary_token);
    }
    ImGui::End();
    if (placement_was_active && !text_preview_.open) {
        cancel_distance_resolution_workflow();
    }
}
