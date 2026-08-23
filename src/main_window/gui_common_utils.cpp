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
#include "app_settings.h"
#include "debug_headless.h"
#include "touch_input.h"

#include "canvas3D.h"
#include "maploader.h"
#include "numeric_safety.h"
#include "own_track_transition_linkage.h"
#include "repeater_linkage.h"
#include "text_decoder.h"
#include "resource.h"

#include "imgui.h"
#include "imgui_internal.h"
#include "misc/cpp/imgui_stdlib.h"
#include "imgui_impl_dx11.h"
#include "imgui_impl_win32.h"
#include "implot.h"

#include <windows.h>
#if defined(_MSC_VER) && !defined(NDEBUG)
#include <crtdbg.h>
#endif
#include <commdlg.h>
#include <d3d11.h>
#include <shellapi.h>
#include <shlobj.h>
#include <wincodec.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cctype>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <new>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

const ImWchar* application_font_glyph_ranges(ImFontAtlas& fonts) {
    static ImVector<ImWchar> ranges;
    if (ranges.empty()) {
        ImFontGlyphRangesBuilder builder;
        builder.AddRanges(fonts.GetGlyphRangesChineseFull());
        builder.AddText(u8"←→↗↘");
        builder.BuildRanges(&ranges);
    }
    return ranges.Data;
}

void merge_required_symbol_glyphs(ImFontAtlas& fonts, float font_size) {
    constexpr const char* symbol_font = "C:/Windows/Fonts/seguisym.ttf";
    if (!std::filesystem::exists(symbol_font)) return;
    static constexpr ImWchar symbol_ranges[] = {0x2190, 0x21FF, 0};
    ImFontConfig config;
    config.MergeMode = true;
    config.PixelSnapH = true;
    fonts.AddFontFromFileTTF(symbol_font, font_size, &config, symbol_ranges);
}

unsigned char ascii_lower(unsigned char ch) noexcept {
    return ch >= 'A' && ch <= 'Z' ? static_cast<unsigned char>(ch + ('a' - 'A')) : ch;
}

bool starts_with_ascii_case_insensitive(std::string_view text, std::string_view prefix) noexcept {
    if (text.size() < prefix.size()) return false;
    for (size_t i = 0; i < prefix.size(); ++i) {
        if (ascii_lower(static_cast<unsigned char>(text[i])) !=
            ascii_lower(static_cast<unsigned char>(prefix[i]))) {
            return false;
        }
    }
    return true;
}

LogSeverity classify_log_severity(std::string_view text) noexcept {
    if (starts_with_ascii_case_insensitive(text, "[warn]") ||
        starts_with_ascii_case_insensitive(text, "[warning]")) {
        return LogSeverity::Warning;
    }
    if (starts_with_ascii_case_insensitive(text, "[error]")) return LogSeverity::Error;
    return LogSeverity::Info;
}

ImVec4 main_bar_background_color() {
    ImVec4 background = ImGui::GetStyleColorVec4(ImGuiCol_WindowBg);
    background.x = std::min(background.x + 0.035f, 1.0f);
    background.y = std::min(background.y + 0.035f, 1.0f);
    background.z = std::min(background.z + 0.035f, 1.0f);
    return background;
}

ImVec4 darkened_theme_color(ImVec4 color) noexcept {
    constexpr float k_darken_factor = 0.40f;
    return ImVec4(color.x * k_darken_factor, color.y * k_darken_factor,
                  color.z * k_darken_factor, 1.0f);
}

ImVec4 log_severity_color(LogSeverity severity) noexcept {
    if (severity == LogSeverity::Error) return ImVec4(1.0f, 0.35f, 0.35f, 1.0f);
    if (severity == LogSeverity::Warning) return ImVec4(1.0f, 0.78f, 0.25f, 1.0f);
    return ImVec4(0.88f, 0.88f, 0.88f, 1.0f);
}

std::string format_elapsed_seconds_value(double elapsed_seconds) {
    std::ostringstream elapsed;
    elapsed << std::fixed << std::setprecision(2) << elapsed_seconds;
    return elapsed.str();
}

void set_crosshair_cursor() {
    ::SetCursor(::LoadCursor(nullptr, IDC_CROSS));
}

void set_move_cursor() {
    ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
    ::SetCursor(::LoadCursor(nullptr, IDC_SIZEALL));
}

const std::string& edit_field_buffer_text(const MapElementEditFieldState& field) {
    return field.value;
}

void set_edit_field_buffer(MapElementEditFieldState& field, const std::string& value) {
    field.value = value;
}

bool parse_gui_edit_number(const std::string& text, double* parsed_value) {
    std::string trimmed = trim_gui_ascii_copy(text);
    if (trimmed.empty()) return false;
    const char* begin = trimmed.c_str();
    char* end = nullptr;
    errno = 0;
    double value = std::strtod(begin, &end);
    const bool valid = end != begin && errno != ERANGE && end && *end == '\0' &&
        std::isfinite(value);
    if (valid && parsed_value) *parsed_value = value;
    return valid;
}

MapElementNumericChoiceSet map_element_numeric_choices(
    MapElementNumericConstraint constraint) {
    static constexpr MapElementNumericChoice k_tilt_choices[] = {
        {0, "value.tilt.always_level"},
        {1, "value.tilt.follow_gradient"},
        {2, "value.tilt.follow_cant"},
        {3, "value.tilt.follow_gradient_and_cant"},
    };
    static constexpr MapElementNumericChoice k_door_choices[] = {
        {-1, "value.door.open_left"},
        {0, "value.door.do_not_open"},
        {1, "value.door.open_right"},
    };
    if (constraint == MapElementNumericConstraint::Tilt) {
        return {k_tilt_choices, std::size(k_tilt_choices)};
    }
    if (constraint == MapElementNumericConstraint::Door) {
        return {k_door_choices, std::size(k_door_choices)};
    }
    return {};
}

int gui_numeric_choice_option_index(double value,
                                    MapElementNumericConstraint constraint) {
    if (!std::isfinite(value) || std::trunc(value) != value) return -1;
    const MapElementNumericChoiceSet options = map_element_numeric_choices(constraint);
    for (size_t index = 0; index < options.count; ++index) {
        if (value == static_cast<double>(options.choices[index].value)) {
            return static_cast<int>(index);
        }
    }
    return -1;
}

int gui_numeric_choice_option_index(const std::string& text,
                                    MapElementNumericConstraint constraint) {
    double value = 0.0;
    if (!parse_gui_edit_number(text, &value)) return -1;
    return gui_numeric_choice_option_index(value, constraint);
}

void render_inline_wrapped_text(const char* label, const std::string& value) {
    ImGui::TextUnformatted(label);
    ImGui::SameLine();
    ImGui::TextWrapped("%s", value.c_str());
}

std::wstring utf8_to_wide(const std::string& text) {
    if (text.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (n <= 0) return {};
    std::wstring out(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), n);
    return out;
}

std::string wide_to_utf8(const std::wstring& text) {
    if (text.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    if (n <= 0) return {};
    std::string out(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), n, nullptr, nullptr);
    return out;
}

std::string narrow_path(const std::filesystem::path& path) {
#if defined(__cpp_char8_t)
    auto s = path.u8string();
    return std::string(reinterpret_cast<const char*>(s.data()), s.size());
#else
    return path.u8string();
#endif
}

std::string format_double(double value, int precision) {
    if (!std::isfinite(value)) return "";
    precision = std::max(0, precision);
    std::array<char, 128> stack_buffer{};
    int written = std::snprintf(stack_buffer.data(), stack_buffer.size(), "%.*f",
                                precision, value);
    if (written < 0) return {};
    std::string s;
    if (static_cast<size_t>(written) < stack_buffer.size()) {
        s.assign(stack_buffer.data(), static_cast<size_t>(written));
    } else {
        std::vector<char> buffer(static_cast<size_t>(written) + 1u);
        written = std::snprintf(buffer.data(), buffer.size(), "%.*f", precision, value);
        if (written < 0) return {};
        s.assign(buffer.data(), static_cast<size_t>(written));
    }
    size_t dot = s.find('.');
    if (dot != std::string::npos) {
        while (s.size() > dot + 1 && s.back() == '0') s.pop_back();
        if (s.size() == dot + 1) s.pop_back();
    }
    if (s == "-0") return "0";
    return s;
}

double truncate_gui_thousandths(double value) {
    if (!std::isfinite(value)) return value;
    double scaled = value * 1000.0;
    const double nearest = std::round(scaled);
    if (std::abs(scaled - nearest) < 1e-9) scaled = nearest;
    double result = std::trunc(scaled) / 1000.0;
    return result == 0.0 ? 0.0 : result;
}

std::string format_gui_transform_number(double value) {
    return format_double(truncate_gui_thousandths(value), 3);
}

MapElementEditFieldState* find_inspector_field(MapElementInspectorState& inspector,
                                                const std::string& key) {
    auto it = std::find_if(inspector.fields.begin(), inspector.fields.end(),
                           [&](const MapElementEditFieldState& field) {
                               return field.key == key;
                           });
    return it == inspector.fields.end() ? nullptr : &*it;
}

const MapElementEditFieldState* find_inspector_field(const MapElementInspectorState& inspector,
                                                      const std::string& key) {
    auto it = std::find_if(inspector.fields.begin(), inspector.fields.end(),
                           [&](const MapElementEditFieldState& field) {
                               return field.key == key;
                           });
    return it == inspector.fields.end() ? nullptr : &*it;
}

bool validate_and_canonicalize_edit_field(MapElementEditFieldState& field,
                                          bool canonicalize) {
    if (field.numeric_constraint == MapElementNumericConstraint::None) return true;
    if (!field.required && field.numeric_constraint != MapElementNumericConstraint::Door &&
        trim_gui_ascii_copy(edit_field_buffer_text(field)).empty()) {
        return true;
    }
    double value = 0.0;
    if (!parse_gui_edit_number(edit_field_buffer_text(field), &value)) return false;
    const MapElementNumericChoiceSet options =
        map_element_numeric_choices(field.numeric_constraint);
    if (options.count != 0 &&
        gui_numeric_choice_option_index(value, field.numeric_constraint) < 0) {
        return false;
    }
    if (!canonicalize) return true;
    if (field.numeric_constraint == MapElementNumericConstraint::Truncate3) {
        set_edit_field_buffer(field, format_gui_transform_number(value));
    } else if (options.count != 0) {
        set_edit_field_buffer(field, format_double(value, 0));
    }
    return true;
}

MapElementNumericConstraint structure_edit_numeric_constraint(const std::string& key) {
    if (key == "tilt") return MapElementNumericConstraint::Tilt;
    static constexpr std::array<const char*, 7> k_truncated_fields = {
        "x", "y", "z", "rx", "ry", "rz", "span"
    };
    if (std::any_of(k_truncated_fields.begin(), k_truncated_fields.end(),
                    [&](const char* field) { return key == field; })) {
        return MapElementNumericConstraint::Truncate3;
    }
    return MapElementNumericConstraint::Finite;
}

float distance_jump_input_width() {
    const ImGuiStyle& style = ImGui::GetStyle();
    return ImGui::CalcTextSize("0000000000").x + style.FramePadding.x * 2.0f;
}

float distance_jump_control_width(const char* label, const char* button_label) {
    const ImGuiStyle& style = ImGui::GetStyle();
    return ImGui::CalcTextSize(label).x +
        ImGui::CalcTextSize(button_label).x + style.FramePadding.x * 2.0f +
        distance_jump_input_width() + style.ItemSpacing.x * 2.0f;
}

int distance_jump_input_filter(ImGuiInputTextCallbackData* data) {
    const unsigned int ch = data->EventChar;
    if (ch >= '0' && ch <= '9') return 0;
    if (ch == '.') return std::strchr(data->Buf, '.') == nullptr ? 0 : 1;
    return 1;
}

bool parse_distance_jump_input(const char* text, double& distance) {
    if (!text) return false;
    const char* begin = text;
    while (*begin && std::isspace(static_cast<unsigned char>(*begin))) ++begin;
    if (*begin == '\0') return false;

    char* end = nullptr;
    double value = std::strtod(begin, &end);
    if (end == begin) return false;
    while (*end && std::isspace(static_cast<unsigned char>(*end))) ++end;
    if (*end != '\0' || !std::isfinite(value) || value < 0.0) return false;

    distance = value;
    return true;
}

double round_to_100(double value) {
    return std::round(value / 100.0) * 100.0;
}

std::string sanitize_filename(std::string text) {
    if (text.empty()) text = "root";
    for (char& ch : text) {
        if (ch == '\\' || ch == '/' || ch == ':' || ch == '*' || ch == '?' ||
            ch == '"' || ch == '<' || ch == '>' || ch == '|') {
            ch = '_';
        }
    }
    return text;
}

std::uint64_t file_structure_revision(const std::vector<FileStructureNode>& nodes) {
    KmeByteHash64 hash;
    for (const FileStructureNode& node : nodes) {
        const std::uint64_t parent = node.parent_index == k_no_file_structure_parent
            ? std::numeric_limits<std::uint64_t>::max()
            : static_cast<std::uint64_t>(node.parent_index);
        hash.integer(parent);
        hash.bytes(node.include_path);
        hash.byte(0xff);
        hash.bytes(node.absolute_path);
        hash.byte(0xff);
    }
    return hash.value;
}
