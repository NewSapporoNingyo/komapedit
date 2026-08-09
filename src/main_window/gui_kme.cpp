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
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <thread>
#include <utility>
#include <vector>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

#ifndef NDEBUG
std::ostream* g_debug_plan_benchmark_log = nullptr;
#endif

namespace {

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

constexpr unsigned char ascii_lower(unsigned char ch) noexcept {
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

} // namespace

void set_crosshair_cursor() {
    ::SetCursor(::LoadCursor(nullptr, IDC_CROSS));
}

void set_move_cursor() {
    ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
    ::SetCursor(::LoadCursor(nullptr, IDC_SIZEALL));
}

std::string edit_field_buffer_text(const MapElementEditFieldState& field) {
    return field.value;
}

void set_edit_field_buffer(MapElementEditFieldState& field, const std::string& value) {
    field.value = value;
}

bool parse_gui_edit_number(const std::string& text, double* parsed_value = nullptr) {
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
    if (!field.required && trim_gui_ascii_copy(edit_field_buffer_text(field)).empty()) {
        return true;
    }
    double value = 0.0;
    if (!parse_gui_edit_number(edit_field_buffer_text(field), &value)) return false;
    if (field.numeric_constraint == MapElementNumericConstraint::Integer &&
        std::trunc(value) != value) {
        return false;
    }
    if (!canonicalize) return true;
    if (field.numeric_constraint == MapElementNumericConstraint::Truncate3) {
        set_edit_field_buffer(field, format_gui_transform_number(value));
    } else if (field.numeric_constraint == MapElementNumericConstraint::Integer) {
        set_edit_field_buffer(field, format_double(value, 0));
    }
    return true;
}

MapElementNumericConstraint structure_edit_numeric_constraint(const std::string& key) {
    if (key == "tilt") return MapElementNumericConstraint::Integer;
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

bool row_kind_has_source_distance_string(const std::string& row_kind);

template <typename Snapshot>
std::string typed_snapshot_string(const Snapshot& snapshot, KvStringRef ref) {
    if (!snapshot.string_data || ref.offset > snapshot.string_size ||
        ref.length > snapshot.string_size - ref.offset) {
        return {};
    }
    return std::string(snapshot.string_data + static_cast<size_t>(ref.offset),
                       static_cast<size_t>(ref.length));
}

bool typed_snapshot_span_valid(KvSpan span, std::uint64_t size) {
    return span.offset <= size && span.count <= size - span.offset;
}

std::optional<InspectorTargetMetadata> resolve_inspector_target_metadata(
    void* handle, const std::string& edit_id,
    const std::string& expected_row_kind,
    std::string* error_message) {
    if (error_message) error_message->clear();
    if (!handle || edit_id.empty()) {
        if (error_message) *error_message = "invalid edit target request";
        return std::nullopt;
    }

    KvEditTargetSnapshot target{};
    const KvUtf8View edit_id_view{edit_id.data(), static_cast<std::uint64_t>(edit_id.size())};
    if (!kv_get_edit_target_typed(handle, edit_id_view, &target, sizeof(target))) {
        if (error_message) {
            const char* err = kv_get_last_error();
            *error_message = err ? err : "kv_get_edit_target_typed failed";
        }
        return std::nullopt;
    }
    try {
        if (target.version != KV_EDIT_TARGET_SNAPSHOT_VERSION ||
            target.structure_size < sizeof(KvEditTargetSnapshot)) {
            throw std::runtime_error("edit target snapshot version or size mismatch");
        }
        InspectorTargetMetadata info;
        info.row_kind = typed_snapshot_string(target, target.row_kind);
        if (!expected_row_kind.empty() && info.row_kind != expected_row_kind) {
            if (error_message) {
                *error_message = "edit target row kind changed from " +
                    expected_row_kind + " to " + info.row_kind;
            }
            return std::nullopt;
        }
        info.row_index = static_cast<size_t>(target.row_index);
        info.elements_for_statement = static_cast<int>(target.elements_for_statement);
        info.statement_kind = typed_snapshot_string(target, target.statement_kind);
        info.source_hash = typed_snapshot_string(target, target.source_hash);
        info.expected_source_hash = typed_snapshot_string(target, target.expected_source_hash);
        info.source.file_path = typed_snapshot_string(target, target.source_file_path);
        info.source.line = target.source.line;
        info.source.column = target.source.column;
        info.source.raw_text_preview = typed_snapshot_string(target, target.raw_text_preview);
        info.raw_statement = typed_snapshot_string(target, target.raw_text);
        info.raw_arguments = typed_snapshot_string(target, target.raw_arguments);
        info.flags = target.flags;
        if (row_kind_has_source_distance_string(info.row_kind)) {
            info.source_distance_string = typed_snapshot_string(target, target.distance_expression);
        }
        info.distance_value = target.distance_value;
        return info;
    } catch (const std::exception& e) {
        if (error_message) *error_message = e.what();
        return std::nullopt;
    }
}

Matrix copy_buffer(KvDoubleBuffer buffer) {
    Matrix m;
    m.rows = buffer.rows;
    m.cols = buffer.cols;
    if (buffer.data && buffer.rows > 0 && buffer.cols > 0) {
        m.data.assign(buffer.data, buffer.data + buffer.rows * buffer.cols);
    }
    return m;
}

ImVec4 other_track_palette_color(size_t index) {
    static constexpr ImVec4 palette[] = {
        ImVec4(0.12f, 0.47f, 0.71f, 1.0f), ImVec4(1.00f, 0.50f, 0.05f, 1.0f),
        ImVec4(0.17f, 0.63f, 0.17f, 1.0f), ImVec4(0.84f, 0.15f, 0.16f, 1.0f),
        ImVec4(0.58f, 0.40f, 0.74f, 1.0f), ImVec4(0.55f, 0.34f, 0.29f, 1.0f),
        ImVec4(0.89f, 0.47f, 0.76f, 1.0f), ImVec4(0.50f, 0.50f, 0.50f, 1.0f),
        ImVec4(0.74f, 0.74f, 0.13f, 1.0f), ImVec4(0.09f, 0.75f, 0.81f, 1.0f)
    };
    return palette[index % (sizeof(palette) / sizeof(palette[0]))];
}

std::string map_snapshot_string(const KvMapSnapshot& snapshot, KvStringRef ref) {
    return typed_snapshot_string(snapshot, ref);
}

bool map_snapshot_span_valid(KvSpan span, std::uint64_t size) {
    return typed_snapshot_span_valid(span, size);
}

std::string map_snapshot_value_text(const KvMapSnapshot& snapshot, const KvValue& value) {
    switch (value.kind) {
        case KV_VALUE_NUMBER: return format_double(value.number_value, 6);
        case KV_VALUE_STRING: return map_snapshot_string(snapshot, value.string_value);
        case KV_VALUE_CONTINUE: return "c";
        case KV_VALUE_NULL:
        default: return {};
    }
}

std::string map_snapshot_preview_value_text(const KvMapSnapshot& snapshot,
                                            const KvValue& value) {
    if (value.kind == KV_VALUE_NULL) return "null";
    return map_snapshot_value_text(snapshot, value);
}

std::string map_snapshot_track_key_text(const KvMapSnapshot& snapshot, const KvValue& value) {
    if (value.kind == KV_VALUE_STRING) {
        return "'" + map_snapshot_string(snapshot, value.string_value) + "'";
    }
    if (value.kind == KV_VALUE_NUMBER) {
        if (value.number_value == 0.0) return "0";
        std::array<char, 64> buffer{};
        const int written = std::snprintf(buffer.data(), buffer.size(), "%.17g",
                                          value.number_value);
        if (written > 0 && static_cast<size_t>(written) < buffer.size()) {
            return std::string(buffer.data(), static_cast<size_t>(written));
        }
    }
    return map_snapshot_value_text(snapshot, value);
}

std::string map_snapshot_value_span_text(const KvMapSnapshot& snapshot, KvSpan span) {
    if (!map_snapshot_span_valid(span, snapshot.value_count) ||
        (span.count != 0 && !snapshot.values)) {
        return {};
    }
    std::string output;
    for (std::uint64_t i = 0; i < span.count; ++i) {
        if (i) output += ", ";
        output += map_snapshot_value_text(snapshot, snapshot.values[span.offset + i]);
    }
    return output;
}

void apply_map_row_metadata(TableRow& output, const KvMapSnapshot& snapshot,
                            const KvRowMetadata& metadata) {
    output.edit_id = map_snapshot_string(snapshot, metadata.edit_id);
    if (metadata.source_file_index < snapshot.source_file_count && snapshot.source_files) {
        output.source.file_path = map_snapshot_string(
            snapshot, snapshot.source_files[metadata.source_file_index].file_path);
    }
    output.source.line = metadata.line;
    output.source.column = metadata.column;
    output.source.raw_text_preview = map_snapshot_string(snapshot, metadata.raw_text_preview);
}

std::string resolve_list_asset_path(const std::string& list_file,
                                    const std::string& source_path) {
    if (list_file.empty() || source_path.empty()) return {};
    std::filesystem::path resolved = kme::maploader::join_utf8_path(
        kme::maploader::path_from_utf8(list_file).parent_path(), source_path);
    std::error_code ec;
    const std::filesystem::path absolute = std::filesystem::absolute(resolved, ec);
    if (!ec) resolved = absolute;
    return kme::maploader::path_to_utf8(resolved.lexically_normal());
}

namespace {

struct ListAssetSourcePathResult {
    std::string source_path;
    std::string resolved_path;
    std::string fallback_reason;
};

std::string preferred_utf8_path(std::filesystem::path path) {
    path = path.lexically_normal();
    path.make_preferred();
    return kme::maploader::path_to_utf8(path);
}

std::string lowercase_ascii_path(std::filesystem::path path) {
    std::string text = kme::maploader::path_to_utf8(path);
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char ch) {
                       return static_cast<char>(ascii_lower(ch));
                   });
    return text;
}

ListAssetSourcePathResult make_list_asset_source_path(
    const std::string& list_file,
    const std::string& selected_file) {
    ListAssetSourcePathResult result;
    const std::filesystem::path selected_path =
        kme::maploader::path_from_utf8(selected_file);
    std::error_code ec;
    std::filesystem::path selected_absolute =
        std::filesystem::absolute(selected_path, ec);
    if (ec) {
        result.resolved_path = preferred_utf8_path(selected_path);
        if (result.resolved_path.empty()) result.resolved_path = selected_file;
        result.source_path = result.resolved_path;
        result.fallback_reason =
            "failed to make the selected file path absolute: " + ec.message();
        return result;
    }
    selected_absolute = selected_absolute.lexically_normal();
    result.resolved_path = preferred_utf8_path(selected_absolute);

    const auto use_absolute = [&](std::string reason) {
        result.source_path = result.resolved_path;
        result.fallback_reason = std::move(reason);
        return result;
    };
    if (list_file.empty()) {
        return use_absolute("the source list file is unavailable");
    }

    const std::filesystem::path list_path =
        kme::maploader::path_from_utf8(list_file);
    std::filesystem::path list_absolute =
        std::filesystem::absolute(list_path, ec);
    if (ec) {
        return use_absolute(
            "failed to make the source list path absolute: " + ec.message());
    }
    const std::filesystem::path list_directory =
        list_absolute.lexically_normal().parent_path();
    if (list_directory.empty()) {
        return use_absolute("the source list directory is unavailable");
    }
    if (lowercase_ascii_path(selected_absolute.root_name()) !=
            lowercase_ascii_path(list_directory.root_name()) ||
        selected_absolute.has_root_directory() !=
            list_directory.has_root_directory()) {
        return use_absolute(
            "the selected file and source list file are on different filesystem roots");
    }

    ec.clear();
    std::filesystem::path relative =
        std::filesystem::relative(selected_absolute, list_directory, ec);
    if (ec) {
        return use_absolute("relative path calculation failed: " + ec.message());
    }
    if (relative.empty() || relative.is_absolute()) {
        return use_absolute("relative path calculation returned no usable path");
    }
    result.source_path = preferred_utf8_path(std::move(relative));
    return result;
}

std::string list_asset_picker_initial_directory(
    const std::string& resolved_file,
    const std::string& list_file) {
    const auto existing_parent = [](const std::string& file) {
        if (file.empty()) return std::string{};
        std::error_code ec;
        std::filesystem::path absolute = std::filesystem::absolute(
            kme::maploader::path_from_utf8(file), ec);
        if (ec) return std::string{};
        std::filesystem::path parent =
            absolute.lexically_normal().parent_path();
        if (parent.empty() || !std::filesystem::is_directory(parent, ec) || ec) {
            return std::string{};
        }
        return preferred_utf8_path(std::move(parent));
    };
    std::string directory = existing_parent(resolved_file);
    return directory.empty() ? existing_parent(list_file) : directory;
}

} // namespace

std::string resolve_list_asset_path(const KvMapSnapshot& snapshot,
                                    const KvRowMetadata& metadata,
                                    const std::string& source_path) {
    if (metadata.source_file_index >= snapshot.source_file_count ||
        !snapshot.source_files) {
        return {};
    }
    return resolve_list_asset_path(
        map_snapshot_string(
            snapshot, snapshot.source_files[metadata.source_file_index].file_path),
        source_path);
}

template <typename Row>
void put_map_common_event_cells(TableRow& output, const KvMapSnapshot& snapshot,
                                const Row& input) {
    output.cells["distance"] = format_double(input.distance, 6);
    output.cells["filePath"] = map_snapshot_string(snapshot, input.file_path);
    output.cells["order"] = std::to_string(input.order);
}

std::vector<TableRow> hydrate_signal_aspect_rows(
    const KvMapSnapshot& snapshot) {
    std::vector<TableRow> rows;
    rows.reserve(static_cast<size_t>(
        snapshot.signal_aspect_count));
    for (std::uint64_t i = 0;
         i < snapshot.signal_aspect_count; ++i) {
        const KvSignalAspectRow& input =
            snapshot.signal_aspects[i];
        TableRow row;
        row.cells["signalAspectKey"] =
            map_snapshot_string(
                snapshot, input.signal_aspect_key);
        if (map_snapshot_span_valid(
                input.structure_keys,
                snapshot.string_ref_count) &&
            (input.structure_keys.count == 0 ||
             snapshot.string_refs)) {
            for (std::uint64_t j = 0;
                 j < input.structure_keys.count; ++j) {
                row.cells[
                    "structureKey" +
                    std::to_string(j + 1)] =
                    map_snapshot_string(
                        snapshot,
                        snapshot.string_refs[
                            input.structure_keys.offset + j]);
            }
        }
        row.cells["_structureKeyCount"] =
            std::to_string(input.structure_keys.count);
        const size_t total_structure_key_count =
            static_cast<size_t>(
                input.structure_keys.count);
        const size_t main_structure_key_count =
            static_cast<size_t>(input.metadata.reserved);
        if (main_structure_key_count >
            total_structure_key_count) {
            throw std::runtime_error(
                "Signal aspect snapshot has an invalid main-row structure-key count");
        }
        const size_t glare_structure_key_count =
            total_structure_key_count -
            main_structure_key_count;
        row.cells["_signalMainStructureKeyCount"] =
            std::to_string(main_structure_key_count);
        row.cells["_signalGlareStructureKeyCount"] =
            std::to_string(glare_structure_key_count);
        apply_map_row_metadata(
            row, snapshot, input.metadata);
        rows.push_back(std::move(row));
    }
    return rows;
}

void bind_station_position_edit_ids(MapModel& model) {
    for (Station& station : model.station_positions) station.edit_id.clear();

    size_t station_row_count = 0;
    for (const TableRow& row : model.station_list_rows) {
        if (!table_cell(row, "posKey").empty()) ++station_row_count;
    }

    if (station_row_count == model.station_positions.size()) {
        size_t row_cursor = 0;
        for (Station& station : model.station_positions) {
            while (row_cursor < model.station_list_rows.size() &&
                   table_cell(model.station_list_rows[row_cursor], "posKey").empty()) {
                ++row_cursor;
            }
            if (row_cursor >= model.station_list_rows.size()) return;
            station.edit_id = model.station_list_rows[row_cursor++].edit_id;
        }
        return;
    }

    constexpr double k_station_row_distance_epsilon = 1e-6;
    size_t row_cursor = 0;
    for (Station& station : model.station_positions) {
        while (row_cursor < model.station_list_rows.size()) {
            const double row_distance =
                table_cell_number(model.station_list_rows[row_cursor], "_distance");
            if (std::isfinite(row_distance) &&
                row_distance >= station.distance - k_station_row_distance_epsilon) {
                break;
            }
            ++row_cursor;
        }

        const TableRow* matched_row = nullptr;
        size_t scan = row_cursor;
        while (scan < model.station_list_rows.size()) {
            const TableRow& row = model.station_list_rows[scan];
            const double row_distance = table_cell_number(row, "_distance");
            if (std::isfinite(row_distance) &&
                row_distance > station.distance + k_station_row_distance_epsilon) {
                break;
            }
            if (std::isfinite(row_distance) &&
                std::abs(row_distance - station.distance) <=
                    k_station_row_distance_epsilon &&
                table_cell(row, "posKey") == station.key) {
                matched_row = &row;
            }
            ++scan;
        }
        row_cursor = scan;
        if (matched_row) station.edit_id = matched_row->edit_id;
    }
}

void rebuild_speed_limit_runtime_cache(MapModel& model) {
    model.speedlimits.clear();
    model.speedlimits.reserve(model.speed_limit_rows.size());
    for (size_t row_index = 0; row_index < model.speed_limit_rows.size();
         ++row_index) {
        const TableRow& row = model.speed_limit_rows[row_index];
        SpeedLimit speed;
        speed.distance = table_cell_number(row, "distance");
        speed.has_speed = ascii_lower(table_cell(row, "method")) == "begin";
        if (speed.has_speed) speed.speed = table_cell_number(row, "speed");
        speed.order = static_cast<int>(table_cell_number(row, "order"));
        speed.row_index = row_index;
        model.speedlimits.push_back(speed);
    }
    std::stable_sort(
        model.speedlimits.begin(), model.speedlimits.end(),
        [](const SpeedLimit& left, const SpeedLimit& right) {
            if (left.distance != right.distance) {
                return left.distance < right.distance;
            }
            if (left.order != right.order) return left.order < right.order;
            return left.row_index < right.row_index;
        });
}

void annotate_own_track_transition_links(MapModel& model) {
    using namespace own_track_transition_linkage;
    std::vector<Event> events;
    events.reserve(model.curve_rows.size() + model.gradient_rows.size());
    for (size_t i = 0; i < model.curve_rows.size(); ++i) {
        TableRow& row = model.curve_rows[i];
        row.cells["_transitionEditId"].clear();
        row.cells["_primaryEditId"].clear();
        row.cells["_transitionStatus"] = "none";
        const std::string method = ascii_lower(table_cell(row, "method"));
        const EventKind kind = method == "curve.begintransition"
            ? EventKind::CurveBeginTransition
            : method == "curve.begin" ? EventKind::CurveBegin
            : method == "curve.begincircular" ? EventKind::CurveBeginCircular
            : method == "curve.end" ? EventKind::CurveEnd
                                      : EventKind::CurveOther;
        events.push_back(Event{i, static_cast<int>(table_cell_number(row, "order")),
                               static_cast<int>(table_cell_number(row, "argumentCount")), kind});
    }
    const size_t gradient_base = model.curve_rows.size();
    for (size_t i = 0; i < model.gradient_rows.size(); ++i) {
        TableRow& row = model.gradient_rows[i];
        row.cells["_transitionEditId"].clear();
        row.cells["_primaryEditId"].clear();
        row.cells["_transitionStatus"] = "none";
        const std::string method = ascii_lower(table_cell(row, "method"));
        const EventKind kind = method == "gradient.begintransition"
            ? EventKind::GradientBeginTransition
            : method == "gradient.end" ? EventKind::GradientEnd
                                        : EventKind::GradientBegin;
        events.push_back(Event{gradient_base + i,
                               static_cast<int>(table_cell_number(row, "order")),
                               static_cast<int>(table_cell_number(row, "argumentCount")), kind});
    }
    auto row_at = [&](size_t source_index) -> TableRow& {
        return source_index < gradient_base
            ? model.curve_rows[source_index]
            : model.gradient_rows[source_index - gradient_base];
    };
    const Linkage linkage = pair_transitions(std::move(events));
    for (const Pair& pair : linkage.pairs) {
        TableRow& transition = row_at(pair.transition_source_index);
        TableRow& primary = row_at(pair.primary_source_index);
        transition.cells["_primaryEditId"] = primary.edit_id;
        transition.cells["_transitionStatus"] = "paired";
        primary.cells["_transitionEditId"] = transition.edit_id;
        primary.cells["_transitionStatus"] = "paired";
    }
    for (size_t source_index : linkage.orphan_transition_source_indices) {
        row_at(source_index).cells["_transitionStatus"] = "orphan";
    }
}

MapModel hydrate_map_snapshot(const KvMapSnapshot& snapshot,
                              const std::string& path,
                              double snapshot_call_seconds) {
    const auto hydrate_started_at = std::chrono::steady_clock::now();
    MapModel model;
    model.path = path;
    model.snapshot_build_seconds = snapshot.build_seconds > 0.0
        ? snapshot.build_seconds : snapshot_call_seconds;

    model.file_structure.reserve(static_cast<size_t>(snapshot.file_structure_count));
    for (std::uint64_t i = 0; i < snapshot.file_structure_count; ++i) {
        const KvFileStructureRow& input = snapshot.file_structure[i];
        FileStructureNode node;
        node.include_path = map_snapshot_string(snapshot, input.include_path);
        node.absolute_path = map_snapshot_string(snapshot, input.absolute_path);
        node.display_name = display_name_from_path(node.absolute_path);
        if (node.display_name.empty()) node.display_name = node.include_path;
        if (!model.file_structure.empty()) {
            node.parent_index = input.parent_index >= 0 &&
                    static_cast<std::uint64_t>(input.parent_index) < model.file_structure.size()
                ? static_cast<size_t>(input.parent_index) : 0;
        }
        model.file_structure.push_back(std::move(node));
    }
    if (model.file_structure.empty() && !path.empty()) {
        FileStructureNode root;
        root.absolute_path = path;
        root.display_name = display_name_from_path(path);
        model.file_structure.push_back(std::move(root));
    }
    model.file_structure_revision = file_structure_revision(model.file_structure);

    model.edit_files.reserve(static_cast<size_t>(snapshot.source_file_count));
    for (std::uint64_t i = 0; i < snapshot.source_file_count; ++i) {
        const KvSourceFileRow& input = snapshot.source_files[i];
        EditSourceFileInfo file;
        file.file_path = map_snapshot_string(snapshot, input.file_path);
        file.display_path = map_snapshot_string(snapshot, input.display_path);
        file.encoding = map_snapshot_string(snapshot, input.encoding);
        file.newline = map_snapshot_string(snapshot, input.newline);
        file.source_hash = map_snapshot_string(snapshot, input.source_hash);
        file.byte_length = static_cast<size_t>(input.byte_length);
        model.edit_files.push_back(std::move(file));
    }
    model.edit_statements.reserve(static_cast<size_t>(snapshot.statement_count));
    for (std::uint64_t i = 0; i < snapshot.statement_count; ++i) {
        const KvStatementRow& input = snapshot.statements[i];
        EditStatementInfo row;
        row.edit_id = map_snapshot_string(snapshot, input.edit_id);
        row.statement_kind = map_snapshot_string(snapshot, input.statement_kind);
        if (input.source.source_file_index < snapshot.source_file_count && snapshot.source_files) {
            row.source.file_path = map_snapshot_string(
                snapshot, snapshot.source_files[input.source.source_file_index].file_path);
        }
        row.source.line = input.source.line;
        row.source.column = input.source.column;
        row.source.raw_text_preview = map_snapshot_string(snapshot, input.raw_text_preview);
        row.raw_text = map_snapshot_string(snapshot, input.raw_text);
        row.raw_arguments = map_snapshot_string(snapshot, input.raw_arguments);
        row.distance_expression = map_snapshot_string(snapshot, input.distance_expression);
        row.distance_value = input.distance_value;
        row.global_order = input.global_order;
        model.edit_statements.push_back(std::move(row));
    }
    model.edit_elements.reserve(static_cast<size_t>(snapshot.element_count));
    for (std::uint64_t i = 0; i < snapshot.element_count; ++i) {
        const KvElementRow& input = snapshot.elements[i];
        EditElementInfo row;
        row.edit_id = map_snapshot_string(snapshot, input.edit_id);
        row.row_kind = map_snapshot_string(snapshot, input.row_kind);
        row.row_index = static_cast<size_t>(input.row_index);
        if (input.source_file_index < snapshot.source_file_count && snapshot.source_files) {
            row.source_file_path = map_snapshot_string(
                snapshot, snapshot.source_files[input.source_file_index].file_path);
        }
        row.global_order = input.global_order;
        model.edit_elements.push_back(std::move(row));
    }

    double buffer_copy_seconds = 0.0;
    auto copy_buffer_timed = [&buffer_copy_seconds](KvDoubleBuffer buffer) {
        const auto started_at = std::chrono::steady_clock::now();
        Matrix matrix = copy_buffer(buffer);
        buffer_copy_seconds += std::chrono::duration<double>(
            std::chrono::steady_clock::now() - started_at).count();
        return matrix;
    };
    model.own = copy_buffer_timed(snapshot.own_track_geometry);
    model.curve = copy_buffer_timed(snapshot.curve_radius_geometry);
    if (snapshot.controlpoints && snapshot.controlpoint_count != 0) {
        model.controlpoints.assign(snapshot.controlpoints,
                                   snapshot.controlpoints + snapshot.controlpoint_count);
    }
    for (size_t i = 0; i < 3; ++i) model.cp_arb[i] = snapshot.cp_arbdistribution[i];
    if (!model.own.empty()) {
        model.distance_origin = model.own.at(0, 0);
        model.height_origin = model.own.at(0, 3);
        model.origin_angle = model.own.at(0, 4);
    }

    model.other_tracks.reserve(static_cast<size_t>(snapshot.other_track_count));
    for (std::uint64_t i = 0; i < snapshot.other_track_count; ++i) {
        const KvOtherTrackRow& input = snapshot.other_tracks[i];
        OtherTrack track;
        track.key = map_snapshot_string(snapshot, input.key);
        track.color = other_track_palette_color(static_cast<size_t>(i));
        track.range_min = input.range_min;
        track.range_max = input.range_max;
        track.points = copy_buffer_timed(input.points);
        if (!track.points.empty() && track.range_min == track.range_max) {
            track.range_min = track.points.at(0, 0);
            track.range_max = track.points.at(track.points.rows - 1, 0);
        }
        model.other_tracks.push_back(std::move(track));
    }
    model.own_events.reserve(static_cast<size_t>(snapshot.own_track_event_count));
    for (std::uint64_t i = 0; i < snapshot.own_track_event_count; ++i) {
        const KvTrackEventRow& input = snapshot.own_track_events[i];
        TrackEvent event;
        event.distance = input.distance;
        event.key = map_snapshot_string(snapshot, input.key);
        event.flag = map_snapshot_string(snapshot, input.flag);
        if (input.value.kind == KV_VALUE_NUMBER) {
            event.value_number = true;
            event.number = input.value.number_value;
        } else {
            event.text = map_snapshot_value_text(snapshot, input.value);
        }
        model.own_events.push_back(std::move(event));
    }
    model.curve_rows.reserve(static_cast<size_t>(snapshot.curve_count));
    for (std::uint64_t i = 0; i < snapshot.curve_count; ++i) {
        const KvCurveRow& input = snapshot.curves[i];
        TableRow row;
        row.cells["distance"] = format_double(input.distance, 6);
        row.cells["method"] = map_snapshot_string(snapshot, input.method);
        row.cells["radius"] = map_snapshot_value_text(snapshot, input.radius);
        row.cells["cant"] = map_snapshot_value_text(snapshot, input.cant);
        row.cells["argumentCount"] = std::to_string(input.argument_count);
        row.cells["filePath"] = map_snapshot_string(snapshot, input.file_path);
        row.cells["order"] = std::to_string(input.order);
        apply_map_row_metadata(row, snapshot, input.metadata);
        model.curve_rows.push_back(std::move(row));
    }
    model.gradient_rows.reserve(static_cast<size_t>(snapshot.gradient_count));
    for (std::uint64_t i = 0; i < snapshot.gradient_count; ++i) {
        const KvGradientRow& input = snapshot.gradients[i];
        TableRow row;
        row.cells["distance"] = format_double(input.distance, 6);
        row.cells["method"] = map_snapshot_string(snapshot, input.method);
        row.cells["gradient"] = map_snapshot_value_text(snapshot, input.gradient);
        row.cells["argumentCount"] = std::to_string(input.argument_count);
        row.cells["filePath"] = map_snapshot_string(snapshot, input.file_path);
        row.cells["order"] = std::to_string(input.order);
        apply_map_row_metadata(row, snapshot, input.metadata);
        model.gradient_rows.push_back(std::move(row));
    }
    model.other_track_changes.reserve(
        static_cast<size_t>(snapshot.other_track_change_count));
    for (std::uint64_t i = 0; i < snapshot.other_track_change_count; ++i) {
        const KvOtherTrackChangeRow& input = snapshot.other_track_changes[i];
        TableRow row;
        row.cells["distance"] = format_double(input.distance, 6);
        row.cells["trackKey"] = map_snapshot_track_key_text(snapshot, input.track_key);
        row.cells["method"] = map_snapshot_string(snapshot, input.method);
        row.cells["parameterCount"] = std::to_string(input.parameters.count);
        row.cells["parameters"] = map_snapshot_value_span_text(snapshot, input.parameters);
        if (map_snapshot_span_valid(input.parameters, snapshot.value_count) &&
            (input.parameters.count == 0 || snapshot.values)) {
            for (std::uint64_t parameter = 0;
                 parameter < input.parameters.count; ++parameter) {
                row.cells["parameter" + std::to_string(parameter)] =
                    map_snapshot_value_text(
                        snapshot,
                        snapshot.values[input.parameters.offset + parameter]);
            }
        }
        row.cells["filePath"] = map_snapshot_string(snapshot, input.file_path);
        row.cells["order"] = std::to_string(input.order);
        apply_map_row_metadata(row, snapshot, input.metadata);
        model.other_track_changes.push_back(std::move(row));
    }
    annotate_own_track_transition_links(model);
    model.speed_limit_rows.reserve(
        static_cast<size_t>(snapshot.speed_limit_count));
    for (std::uint64_t i = 0; i < snapshot.speed_limit_count; ++i) {
        const KvSpeedLimitRow& input = snapshot.speed_limits[i];
        TableRow row;
        row.cells["distance"] = format_double(input.distance, 6);
        row.cells["method"] =
            input.speed.kind == KV_VALUE_NUMBER ? "Begin" : "End";
        row.cells["speed"] = input.speed.kind == KV_VALUE_NUMBER
            ? map_snapshot_value_text(snapshot, input.speed)
            : std::string{};
        row.cells["filePath"] = map_snapshot_string(snapshot, input.file_path);
        row.cells["order"] = std::to_string(input.order);
        apply_map_row_metadata(row, snapshot, input.metadata);
        model.speed_limit_rows.push_back(std::move(row));
    }
    rebuild_speed_limit_runtime_cache(model);
    for (std::uint64_t i = 0; i < snapshot.station_name_count; ++i) {
        const KvStationNameRow& input = snapshot.station_names[i];
        model.station_names[map_snapshot_string(snapshot, input.key)] =
            map_snapshot_string(snapshot, input.name);
    }
    std::set<std::string> seen_stations;
    size_t own_index = 0;
    model.station_positions.reserve(static_cast<size_t>(snapshot.station_position_count));
    model.stations.reserve(static_cast<size_t>(snapshot.station_position_count));
    for (std::uint64_t i = 0; i < snapshot.station_position_count; ++i) {
        const KvStationPositionRow& input = snapshot.station_positions[i];
        Station station;
        station.distance = input.distance;
        station.key = map_snapshot_string(snapshot, input.key);
        auto name = model.station_names.find(station.key);
        if (name != model.station_names.end()) station.name = name->second;
        if (station.name.empty()) station.name = station.key;
        station.mileage = station.distance - model.distance_origin;
        if (!model.own.empty()) {
            while (own_index + 1 < model.own.rows && model.own.at(own_index, 0) < station.distance) {
                ++own_index;
            }
            station.x = model.own.at(own_index, 1);
            station.y = model.own.at(own_index, 2);
            station.z = model.own.at(own_index, 3);
        }
        model.station_positions.push_back(station);
        if (seen_stations.insert(station.key).second) model.stations.push_back(std::move(station));
    }

    auto add_structure_put = [&](const KvStructurePutRow& input) {
        TableRow row;
        put_map_common_event_cells(row, snapshot, input);
        row.cells["method"] = map_snapshot_string(snapshot, input.method);
        row.cells["structureKey"] = map_snapshot_value_text(snapshot, input.structure_key);
        row.cells["trackKey"] = map_snapshot_track_key_text(snapshot, input.track_key);
        row.cells["x"] = format_double(input.x, 6); row.cells["y"] = format_double(input.y, 6);
        row.cells["z"] = format_double(input.z, 6); row.cells["rx"] = format_double(input.rx, 6);
        row.cells["ry"] = format_double(input.ry, 6); row.cells["rz"] = format_double(input.rz, 6);
        row.cells["tilt"] = format_double(input.tilt, 6);
        row.cells["span"] = format_double(input.span, 6);
        apply_map_row_metadata(row, snapshot, input.metadata);
        model.structures.push_back(std::move(row));
    };
    model.structures.reserve(static_cast<size_t>(snapshot.structure_put_count));
    for (std::uint64_t i = 0; i < snapshot.structure_put_count; ++i) {
        add_structure_put(snapshot.structure_puts[i]);
    }
    model.structures_between.reserve(static_cast<size_t>(snapshot.structure_between_count));
    for (std::uint64_t i = 0; i < snapshot.structure_between_count; ++i) {
        const KvStructureBetweenRow& input = snapshot.structure_betweens[i];
        TableRow row;
        put_map_common_event_cells(row, snapshot, input);
        row.cells["method"] = map_snapshot_string(snapshot, input.method);
        row.cells["structureKey"] = map_snapshot_value_text(snapshot, input.structure_key);
        row.cells["trackKey1"] = map_snapshot_track_key_text(snapshot, input.track_key1);
        row.cells["trackKey2"] = map_snapshot_track_key_text(snapshot, input.track_key2);
        row.cells["flag"] = format_double(input.flag, 6);
        apply_map_row_metadata(row, snapshot, input.metadata);
        model.structures_between.push_back(std::move(row));
    }
    model.structure_models.reserve(static_cast<size_t>(snapshot.structure_model_count));
    for (std::uint64_t i = 0; i < snapshot.structure_model_count; ++i) {
        const KvStructureModelRow& input = snapshot.structure_models[i];
        TableRow row;
        row.cells["structureKey"] = map_snapshot_string(snapshot, input.structure_key);
        row.cells["filePath"] = map_snapshot_string(snapshot, input.file_path);
        row.cells["resolvedFilePath"] = resolve_list_asset_path(
            snapshot, input.metadata, row.cells["filePath"]);
        apply_map_row_metadata(row, snapshot, input.metadata);
        model.structure_models.push_back(std::move(row));
    }
    model.other_trains.reserve(static_cast<size_t>(snapshot.other_train_definition_count));
    for (std::uint64_t i = 0; i < snapshot.other_train_definition_count; ++i) {
        const KvOtherTrainDefinitionRow& input = snapshot.other_train_definitions[i];
        TableRow row;
        row.cells["distance"] = format_double(input.distance, 6);
        row.cells["method"] = map_snapshot_string(snapshot, input.method);
        row.cells["trainKey"] = map_snapshot_value_text(snapshot, input.train_key);
        row.cells["filePath"] = map_snapshot_value_text(snapshot, input.load_file_path);
        row.cells["resolvedFilePath"] = map_snapshot_string(snapshot, input.resolved_file_path);
        row.cells["trackKey"] = map_snapshot_track_key_text(snapshot, input.track_key);
        row.cells["direction"] = map_snapshot_value_text(snapshot, input.direction);
        row.cells["sourceFilePath"] = map_snapshot_string(snapshot, input.source_file_path);
        row.cells["order"] = std::to_string(input.order);
        apply_map_row_metadata(row, snapshot, input.metadata);
        model.other_trains.push_back(std::move(row));
    }
    model.other_train_enables.reserve(
        static_cast<size_t>(snapshot.other_train_enable_count));
    for (std::uint64_t i = 0; i < snapshot.other_train_enable_count; ++i) {
        const KvOtherTrainEnableRow& input = snapshot.other_train_enables[i];
        TableRow row;
        put_map_common_event_cells(row, snapshot, input);
        row.cells["trainKey"] = map_snapshot_value_text(snapshot, input.train_key);
        row.cells["time"] = map_snapshot_value_text(snapshot, input.time);
        apply_map_row_metadata(row, snapshot, input.metadata);
        model.other_train_enables.push_back(std::move(row));
    }
    model.other_train_stops.reserve(static_cast<size_t>(snapshot.other_train_stop_count));
    for (std::uint64_t i = 0; i < snapshot.other_train_stop_count; ++i) {
        const KvOtherTrainStopRow& input = snapshot.other_train_stops[i];
        TableRow row;
        put_map_common_event_cells(row, snapshot, input);
        row.cells["trainKey"] = map_snapshot_value_text(snapshot, input.train_key);
        row.cells["decelerate"] = map_snapshot_value_text(snapshot, input.decelerate);
        row.cells["stopTime"] = map_snapshot_value_text(snapshot, input.stop_time);
        row.cells["accelerate"] = map_snapshot_value_text(snapshot, input.accelerate);
        row.cells["speed"] = map_snapshot_value_text(snapshot, input.speed);
        apply_map_row_metadata(row, snapshot, input.metadata);
        model.other_train_stops.push_back(std::move(row));
    }
    auto add_referenced_keys = [&](const KvReferencedKeyRow* inputs, std::uint64_t count,
                                   std::vector<TableRow>& output) {
        output.reserve(static_cast<size_t>(count));
        for (std::uint64_t i = 0; i < count; ++i) {
            TableRow row;
            row.cells["key"] = map_snapshot_string(snapshot, inputs[i].key);
            row.cells["filePath"] = map_snapshot_string(snapshot, inputs[i].file_path);
            apply_map_row_metadata(row, snapshot, inputs[i].metadata);
            output.push_back(std::move(row));
        }
    };
    add_referenced_keys(snapshot.other_train_structure_keys,
                        snapshot.other_train_structure_key_count,
                        model.other_train_structure_keys);
    add_referenced_keys(snapshot.other_train_sound_3d_keys,
                        snapshot.other_train_sound_3d_key_count,
                        model.other_train_sound_3d_keys);
    auto add_sections = [&](const KvSectionRow* inputs, std::uint64_t count,
                            std::vector<TableRow>& output) {
        output.reserve(static_cast<size_t>(count));
        for (std::uint64_t i = 0; i < count; ++i) {
            const KvSectionRow& input = inputs[i];
            TableRow row;
            row.cells["distance"] = format_double(input.distance, 6);
            row.cells["method"] = map_snapshot_string(snapshot, input.method);
            row.cells["filePath"] = map_snapshot_string(snapshot, input.file_path);
            row.cells["order"] = std::to_string(input.order);
            if (map_snapshot_span_valid(input.values, snapshot.value_count) &&
                (input.values.count == 0 || snapshot.values)) {
                for (std::uint64_t value_index = 0;
                     value_index < input.values.count; ++value_index) {
                    row.cells["value" + std::to_string(value_index)] =
                        map_snapshot_preview_value_text(
                            snapshot,
                            snapshot.values[input.values.offset + value_index]);
                }
            }
            row.cells["valueCount"] = std::to_string(input.values.count);
            apply_map_row_metadata(row, snapshot, input.metadata);
            output.push_back(std::move(row));
        }
    };
    add_sections(snapshot.section_begins, snapshot.section_begin_count,
                 model.section_begins);
    add_sections(snapshot.section_speed_limits,
                 snapshot.section_speed_limit_count,
                 model.section_speed_limits);
    model.sound_list.reserve(static_cast<size_t>(snapshot.sound_list_count));
    model.sound_3d_list.reserve(static_cast<size_t>(snapshot.sound_list_count));
    for (std::uint64_t i = 0; i < snapshot.sound_list_count; ++i) {
        const KvSoundListRow& input = snapshot.sound_list[i];
        TableRow row;
        row.cells["soundKey"] = map_snapshot_string(snapshot, input.sound_key);
        row.cells["filePath"] = map_snapshot_string(snapshot, input.file_path);
        row.cells["resolvedFilePath"] = resolve_list_asset_path(
            snapshot, input.metadata, row.cells["filePath"]);
        row.cells["bufferCount"] = std::to_string(input.buffer_count);
        row.cells["is3D"] = input.is_3d ? "true" : "false";
        apply_map_row_metadata(row, snapshot, input.metadata);
        (input.is_3d ? model.sound_3d_list : model.sound_list).push_back(
            std::move(row));
    }
    model.repeaters.reserve(static_cast<size_t>(snapshot.repeater_count));
    for (std::uint64_t i = 0; i < snapshot.repeater_count; ++i) {
        const KvRepeaterRow& input = snapshot.repeaters[i];
        TableRow row;
        put_map_common_event_cells(row, snapshot, input);
        row.cells["method"] = map_snapshot_string(snapshot, input.method);
        row.cells["repeaterKey"] = map_snapshot_value_text(snapshot, input.repeater_key);
        row.cells["trackKey"] = map_snapshot_track_key_text(snapshot, input.track_key);
        row.cells["x"] = format_double(input.x, 6); row.cells["y"] = format_double(input.y, 6);
        row.cells["z"] = format_double(input.z, 6); row.cells["rx"] = format_double(input.rx, 6);
        row.cells["ry"] = format_double(input.ry, 6); row.cells["rz"] = format_double(input.rz, 6);
        row.cells["tilt"] = format_double(input.tilt, 6);
        row.cells["span"] = format_double(input.span, 6);
        row.cells["interval"] = format_double(input.interval, 6);
        row.cells["structureKeys"] = map_snapshot_value_span_text(snapshot, input.structure_keys);
        apply_map_row_metadata(row, snapshot, input.metadata);
        model.repeaters.push_back(std::move(row));
    }
    model.signal_aspects =
        hydrate_signal_aspect_rows(snapshot);
    model.signals.reserve(static_cast<size_t>(snapshot.signal_put_count));
    for (std::uint64_t i = 0; i < snapshot.signal_put_count; ++i) {
        const KvSignalPutRow& input = snapshot.signal_puts[i];
        TableRow row;
        put_map_common_event_cells(row, snapshot, input);
        row.cells["signalAspectKey"] = map_snapshot_value_text(snapshot, input.signal_aspect_key);
        row.cells["section"] = map_snapshot_value_text(snapshot, input.section);
        row.cells["trackKey"] = map_snapshot_track_key_text(snapshot, input.track_key);
        row.cells["x"] = format_double(input.x, 6); row.cells["y"] = format_double(input.y, 6);
        row.cells["z"] = format_double(input.z, 6); row.cells["rx"] = format_double(input.rx, 6);
        row.cells["ry"] = format_double(input.ry, 6); row.cells["rz"] = format_double(input.rz, 6);
        row.cells["tilt"] = format_double(input.tilt, 6);
        row.cells["span"] = format_double(input.span, 6);
        apply_map_row_metadata(row, snapshot, input.metadata);
        model.signals.push_back(std::move(row));
    }
    model.beacons.reserve(static_cast<size_t>(snapshot.beacon_count));
    for (std::uint64_t i = 0; i < snapshot.beacon_count; ++i) {
        const KvBeaconRow& input = snapshot.beacons[i];
        TableRow row;
        put_map_common_event_cells(row, snapshot, input);
        row.cells["type"] = map_snapshot_value_text(snapshot, input.type);
        row.cells["section"] = map_snapshot_value_text(snapshot, input.section);
        row.cells["sendData"] = map_snapshot_value_text(snapshot, input.send_data);
        apply_map_row_metadata(row, snapshot, input.metadata);
        model.beacons.push_back(std::move(row));
    }
    model.pretrains.reserve(static_cast<size_t>(snapshot.pretrain_count));
    for (std::uint64_t i = 0; i < snapshot.pretrain_count; ++i) {
        const KvPreTrainRow& input = snapshot.pretrains[i];
        TableRow row;
        put_map_common_event_cells(row, snapshot, input);
        row.cells["passTime"] = map_snapshot_value_text(snapshot, input.pass_time);
        apply_map_row_metadata(row, snapshot, input.metadata);
        model.pretrains.push_back(std::move(row));
    }
    model.irregularities.reserve(static_cast<size_t>(snapshot.irregularity_count));
    for (std::uint64_t i = 0; i < snapshot.irregularity_count; ++i) {
        const KvIrregularityRow& input = snapshot.irregularities[i];
        TableRow row;
        put_map_common_event_cells(row, snapshot, input);
        row.cells["x"] = format_double(input.x, 6); row.cells["y"] = format_double(input.y, 6);
        row.cells["r"] = format_double(input.r, 6); row.cells["lx"] = format_double(input.lx, 6);
        row.cells["ly"] = format_double(input.ly, 6); row.cells["lr"] = format_double(input.lr, 6);
        apply_map_row_metadata(row, snapshot, input.metadata);
        model.irregularities.push_back(std::move(row));
    }
    model.map_sounds.reserve(static_cast<size_t>(snapshot.map_sound_count));
    for (std::uint64_t i = 0; i < snapshot.map_sound_count; ++i) {
        const KvMapSoundRow& input = snapshot.map_sounds[i];
        TableRow row;
        put_map_common_event_cells(row, snapshot, input);
        row.cells["soundKey"] = map_snapshot_value_text(snapshot, input.sound_key);
        apply_map_row_metadata(row, snapshot, input.metadata);
        model.map_sounds.push_back(std::move(row));
    }
    model.map_sound_3d.reserve(static_cast<size_t>(snapshot.map_sound_3d_count));
    for (std::uint64_t i = 0; i < snapshot.map_sound_3d_count; ++i) {
        const KvMapSound3DRow& input = snapshot.map_sounds_3d[i];
        TableRow row;
        put_map_common_event_cells(row, snapshot, input);
        row.cells["soundKey"] = map_snapshot_value_text(snapshot, input.sound_key);
        row.cells["x"] = format_double(input.x, 6); row.cells["y"] = format_double(input.y, 6);
        apply_map_row_metadata(row, snapshot, input.metadata);
        model.map_sound_3d.push_back(std::move(row));
    }
    auto add_noises = [&](const KvNoiseRow* inputs, std::uint64_t count,
                          std::vector<TableRow>& output) {
        output.reserve(static_cast<size_t>(count));
        for (std::uint64_t i = 0; i < count; ++i) {
            TableRow row;
            put_map_common_event_cells(row, snapshot, inputs[i]);
            row.cells["index"] = map_snapshot_value_text(snapshot, inputs[i].index);
            apply_map_row_metadata(row, snapshot, inputs[i].metadata);
            output.push_back(std::move(row));
        }
    };
    add_noises(snapshot.rolling_noises, snapshot.rolling_noise_count, model.rolling_noises);
    add_noises(snapshot.flange_noises, snapshot.flange_noise_count, model.flange_noises);
    add_noises(snapshot.joint_noises, snapshot.joint_noise_count, model.joint_noises);
    model.backgrounds.reserve(static_cast<size_t>(snapshot.background_count));
    for (std::uint64_t i = 0; i < snapshot.background_count; ++i) {
        const KvBackgroundRow& input = snapshot.backgrounds[i];
        TableRow row;
        put_map_common_event_cells(row, snapshot, input);
        row.cells["structureKey"] = map_snapshot_value_text(snapshot, input.structure_key);
        apply_map_row_metadata(row, snapshot, input.metadata);
        model.backgrounds.push_back(std::move(row));
    }
    model.adhesions.reserve(static_cast<size_t>(snapshot.adhesion_count));
    for (std::uint64_t i = 0; i < snapshot.adhesion_count; ++i) {
        const KvAdhesionRow& input = snapshot.adhesions[i];
        TableRow row;
        put_map_common_event_cells(row, snapshot, input);
        row.cells["a"] = map_snapshot_value_text(snapshot, input.a);
        row.cells["b"] = map_snapshot_value_text(snapshot, input.b);
        row.cells["c"] = map_snapshot_value_text(snapshot, input.c);
        apply_map_row_metadata(row, snapshot, input.metadata);
        model.adhesions.push_back(std::move(row));
    }
    model.cab_illuminance.reserve(static_cast<size_t>(snapshot.cab_illuminance_count));
    for (std::uint64_t i = 0; i < snapshot.cab_illuminance_count; ++i) {
        const KvCabIlluminanceRow& input = snapshot.cab_illuminance[i];
        TableRow row;
        put_map_common_event_cells(row, snapshot, input);
        row.cells["value"] = map_snapshot_value_text(snapshot, input.value);
        apply_map_row_metadata(row, snapshot, input.metadata);
        model.cab_illuminance.push_back(std::move(row));
    }
    model.fogs.reserve(static_cast<size_t>(snapshot.fog_count));
    for (std::uint64_t i = 0; i < snapshot.fog_count; ++i) {
        const KvFogRow& input = snapshot.fogs[i];
        TableRow row;
        put_map_common_event_cells(row, snapshot, input);
        row.cells["density"] = map_snapshot_value_text(snapshot, input.density);
        row.cells["red"] = map_snapshot_value_text(snapshot, input.red);
        row.cells["green"] = map_snapshot_value_text(snapshot, input.green);
        row.cells["blue"] = map_snapshot_value_text(snapshot, input.blue);
        apply_map_row_metadata(row, snapshot, input.metadata);
        model.fogs.push_back(std::move(row));
    }
    model.draw_distances.reserve(static_cast<size_t>(snapshot.draw_distance_count));
    for (std::uint64_t i = 0; i < snapshot.draw_distance_count; ++i) {
        const KvDrawDistanceRow& input = snapshot.draw_distances[i];
        TableRow row;
        put_map_common_event_cells(row, snapshot, input);
        row.cells["value"] = format_double(input.value, 6);
        apply_map_row_metadata(row, snapshot, input.metadata);
        model.draw_distances.push_back(std::move(row));
    }

    model.variable_assignments.reserve(
        static_cast<size_t>(snapshot.variable_assignment_count));
    for (std::uint64_t i = 0; i < snapshot.variable_assignment_count; ++i) {
        const KvVariableAssignmentRow& input = snapshot.variable_assignments[i];
        TableRow row;
        row.cells["normalizedName"] =
            map_snapshot_string(snapshot, input.normalized_name);
        row.cells["sourceName"] = map_snapshot_string(snapshot, input.source_name);
        row.cells["value"] = map_snapshot_preview_value_text(snapshot, input.value);
        row.cells["expression"] = map_snapshot_string(snapshot, input.expression);
        row.cells["filePath"] = map_snapshot_string(snapshot, input.file_path);
        row.cells["order"] = std::to_string(input.order);
        model.variable_assignments.push_back(std::move(row));
    }
    for (std::uint64_t i = 0; i < snapshot.resource_list_load_count; ++i) {
        const KvResourceListLoadRow& input = snapshot.resource_list_loads[i];
        if (input.kind >= static_cast<std::uint32_t>(ResourceListKind::Count)) continue;
        ResourceListSource& output = model.resource_list_sources[input.kind];
        output.present = true;
        output.evaluated_path = map_snapshot_string(snapshot, input.evaluated_path);
        output.raw_argument = map_snapshot_string(snapshot, input.raw_argument);
        output.resolved_path = map_snapshot_string(snapshot, input.resolved_path);
        output.source_file_path = map_snapshot_string(snapshot, input.file_path);
        output.order = input.order;
    }

    static const char* station_keys[] = {
        "stationKey", "stationName", "arrivalTime", "depertureTime", "stoppageTime",
        "defaultTime", "signalFlag", "alightingTime", "passengers", "arrivalSoundKey",
        "depertureSoundKey", "doorReopen", "stuckInDoor"
    };
    model.station_definition_rows.reserve(static_cast<size_t>(snapshot.station_list_count));
    std::map<std::string, size_t> station_definition_row_indices_by_key;
    for (std::uint64_t i = 0; i < snapshot.station_list_count; ++i) {
        const KvStationListRow& input = snapshot.station_list[i];
        TableRow row;
        for (size_t field = 0; field < 13; ++field) {
            row.cells[station_keys[field]] = map_snapshot_string(snapshot, input.fields[field]);
        }
        apply_map_row_metadata(row, snapshot, input.metadata);
        const std::string object_key = map_snapshot_string(snapshot, input.object_key);
        if (table_cell(row, "stationKey").empty()) row.cells["stationKey"] = object_key;
        const size_t row_index = model.station_definition_rows.size();
        model.station_definition_rows.push_back(std::move(row));
        const std::string normalized_key = ascii_lower(object_key);
        if (!normalized_key.empty()) {
            station_definition_row_indices_by_key[normalized_key] = row_index;
        }
    }
    model.station_list_rows.reserve(static_cast<size_t>(snapshot.station_put_count));
    for (std::uint64_t i = 0; i < snapshot.station_put_count; ++i) {
        const KvStationPutRow& input = snapshot.station_puts[i];
        const std::string key = map_snapshot_value_text(snapshot, input.station_key);
        TableRow row;
        row.cells["_distance"] = format_double(input.distance, 6);
        row.cells["_order"] = std::to_string(input.order);
        row.cells["dist"] = format_double(input.distance - model.distance_origin, 0);
        row.cells["posKey"] = key;
        row.cells["door"] = map_snapshot_value_text(snapshot, input.door);
        row.cells["margin1"] = map_snapshot_value_text(snapshot, input.margin1);
        row.cells["margin2"] = map_snapshot_value_text(snapshot, input.margin2);
        apply_map_row_metadata(row, snapshot, input.metadata);
        auto existing = station_definition_row_indices_by_key.find(ascii_lower(key));
        if (existing != station_definition_row_indices_by_key.end()) {
            const TableRow& definition = model.station_definition_rows[existing->second];
            for (const auto& cell : definition.cells) row.cells[cell.first] = cell.second;
        }
        model.station_list_rows.push_back(std::move(row));
    }
    std::stable_sort(model.station_list_rows.begin(), model.station_list_rows.end(),
                     [](const TableRow& a, const TableRow& b) {
        const double da = table_cell_number(a, "_distance");
        const double db = table_cell_number(b, "_distance");
        if (da != db) return da < db;
        return table_cell_number(a, "_order") < table_cell_number(b, "_order");
    });
    for (size_t i = 0; i < model.station_list_rows.size(); ++i) {
        model.station_list_rows[i].cells["rowNumber"] = std::to_string(i + 1);
    }
    bind_station_position_edit_ids(model);

    if (!model.stations.empty()) {
        double minimum = model.stations.front().distance;
        double maximum = minimum;
        for (const Station& station : model.stations) {
            minimum = std::min(minimum, station.distance);
            maximum = std::max(maximum, station.distance);
        }
        model.default_min = round_to_100(minimum) - 500.0;
        model.default_max = round_to_100(maximum) + 500.0;
    } else if (!model.controlpoints.empty()) {
        auto range = std::minmax_element(model.controlpoints.begin(), model.controlpoints.end());
        model.default_min = round_to_100(*range.first) - 500.0;
        model.default_max = round_to_100(*range.second) + 500.0;
    } else if (!model.own.empty()) {
        model.default_min = model.own.at(0, 0);
        model.default_max = model.own.at(model.own.rows - 1, 0);
    }
    model.buffer_copy_seconds = buffer_copy_seconds;
    model.snapshot_hydrate_seconds = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - hydrate_started_at).count();
    annotate_scene_track_key_warnings(model);
    return model;
}

App* g_app = nullptr;
HWND g_main_hwnd = nullptr;
constexpr UINT k_app_wake_message = WM_APP + 1;

void wake_main_window() {
    if (g_main_hwnd) PostMessageW(g_main_hwnd, k_app_wake_message, 0, 0);
}

App::App(ID3D11Device* device, UserSettings settings, float dpi_scale, bool viewports_enabled, bool has_saved_layout)
    : device_(device), dpi_scale_(dpi_scale), viewports_enabled_(viewports_enabled), settings_(std::move(settings)),
      has_saved_layout_(has_saved_layout) {
    g_app = this;
    kv_set_log_callback(&App::log_callback);
    model_preview_canvas_ = std::make_unique<Canvas3D>(device_);
    model_preview_canvas_->set_background_color(model_preview_bg_color_);
    scene_preview_canvas_ = std::make_unique<Canvas3D>(device_, wake_main_window);
    scene_preview_canvas_->set_background_color(ImVec4(0.0f, 0.0f, 0.0f, 1.0f));
    lang_ = settings_.language;
    font_size_ = clamp_font_size(settings_.font_size);
    ui_component_size_ = clamp_ui_component_size(settings_.ui_component_size);
    marker_size_percent_ = clamp_marker_size_percent(settings_.marker_size_percent);
    canvas_line_widths_ = clamp_canvas_line_widths(settings_.canvas_line_widths);
    theme_color_ = clamp_theme_color(settings_.theme_color);
    settings_.language = lang_;
    settings_.font_size = font_size_;
    settings_.ui_component_size = ui_component_size_;
    settings_.marker_size_percent = marker_size_percent_;
    settings_.canvas_line_widths = canvas_line_widths_;
    settings_.theme_color = theme_color_;
    edit_mode_enabled_ = settings_.edit_mode_enabled;
    settings_.edit_mode_enabled = edit_mode_enabled_;
    pending_font_size_ = font_size_;
    font_size_before_dialog_ = font_size_;
    pending_ui_component_size_ = ui_component_size_;
    ui_component_size_before_dialog_ = ui_component_size_;
    pending_marker_size_percent_ = marker_size_percent_;
    marker_size_percent_before_dialog_ = marker_size_percent_;
    pending_canvas_line_widths_ = canvas_line_widths_;
    canvas_line_widths_before_dialog_ = canvas_line_widths_;
    pending_theme_color_ = theme_color_;
    theme_color_before_dialog_ = theme_color_;
    apply_window_visibility_settings(settings_.window_visibility);
    last_saved_window_visibility_ = current_window_visibility();
    settings_.window_visibility = last_saved_window_visibility_;
    apply_view_2d_settings(settings_.view_2d);
    last_saved_view_2d_settings_ = current_view_2d_settings();
    settings_.view_2d = last_saved_view_2d_settings_;
    apply_view_3d_settings(settings_.view_3d);
    last_saved_view_3d_settings_ = current_view_3d_settings();
    settings_.view_3d = last_saved_view_3d_settings_;
    pending_scene_draw_distance_m_ = scene_draw_distance_m_;
    scene_draw_distance_before_dialog_m_ = scene_draw_distance_m_;
    pending_scene_edit_component_size_percent_ = scene_edit_component_size_percent_;
    scene_edit_component_size_before_dialog_percent_ = scene_edit_component_size_percent_;
    pending_scene_fog_enabled_ = scene_fog_enabled_;
    scene_fog_enabled_before_dialog_ = scene_fog_enabled_;
    pending_scene_map_draw_distance_enabled_ = scene_map_draw_distance_enabled_;
    scene_map_draw_distance_enabled_before_dialog_ = scene_map_draw_distance_enabled_;
    pending_scene_camera_speed_percent_ = scene_camera_speed_percent_;
    scene_camera_speed_percent_before_dialog_ = scene_camera_speed_percent_;
    pending_scene_performance_warning_enabled_ = scene_performance_warning_enabled_;
    scene_performance_warning_enabled_before_dialog_ = scene_performance_warning_enabled_;
    pending_scene_instance_warning_threshold_ = scene_instance_warning_threshold_;
    scene_instance_warning_threshold_before_dialog_ = scene_instance_warning_threshold_;
    pending_scene_instance_critical_warning_threshold_ =
        scene_instance_critical_warning_threshold_;
    scene_instance_critical_warning_threshold_before_dialog_ =
        scene_instance_critical_warning_threshold_;
    apply_ui_settings(font_size_, ui_component_size_, theme_color_, dpi_scale_, viewports_enabled_);
    history_path_ = default_history_path();
    recent_maps_ = load_history_entries(history_path_);
    sync_pending_background_values();
}

App::~App() {
    stop_loader();
    if (handle_) kv_free(handle_);
    bg_image_.release();
    g_app = nullptr;
}

void App::log_callback(const char* message) {
    if (g_app && message) g_app->add_log(message);
}

void TextureImage::release() {
    release_com(srv);
    pixels_rgba.clear();
    width = height = 0;
    path.clear();
}

void App::add_log(std::string text) {
    const LogSeverity severity = classify_log_severity(text);
    add_log(severity, std::move(text));
}

void App::add_log(LogSeverity severity, std::string text) {
    {
        std::lock_guard<std::mutex> lock(log_mutex_);
        if (logs_.size() >= k_max_console_log_lines) {
            const LogSeverity dropped = logs_.front().severity;
            logs_.pop_front();
            if (dropped == LogSeverity::Error) {
                error_count_.fetch_sub(1, std::memory_order_relaxed);
            }
            if (dropped == LogSeverity::Warning) {
                warn_count_.fetch_sub(1, std::memory_order_relaxed);
            }
        }
        logs_.push_back({std::move(text), severity});
        if (severity == LogSeverity::Error) {
            error_count_.fetch_add(1, std::memory_order_relaxed);
        }
        if (severity == LogSeverity::Warning) {
            warn_count_.fetch_add(1, std::memory_order_relaxed);
        }
    }
    wake_main_window();
}

void App::stop_loader() {
    if (load_state_.worker.joinable()) load_state_.worker.join();
}

void App::handle_loader_start_failure(const std::string& error) {
    load_state_.running = false;
    load_state_.pending_started_at.reset();
    set_program_status("status.map_load_failed");
    add_log(LogSeverity::Error,
            "[ERROR]gui_kme.cpp: failed to start map loader: " + error);
}

void App::poll_loader() {
    std::optional<LoadResult> result;
    {
        std::lock_guard<std::mutex> lock(load_state_.result_mutex);
        if (load_state_.pending_result) {
            result = std::move(load_state_.pending_result);
            load_state_.pending_result.reset();
        }
    }
    if (result) apply_load_result(std::move(*result));
}

void App::set_program_status(const char* key, std::string_view elapsed_seconds) {
    program_status_key_ = key;
    program_status_elapsed_suffix_.clear();
    if (elapsed_seconds.empty()) return;

    program_status_elapsed_suffix_ = " (";
    program_status_elapsed_suffix_.append(elapsed_seconds);
    program_status_elapsed_suffix_ += "s)";
}

void App::begin_load(std::string path, bool preserve_settings, bool record_history,
                     std::optional<BackgroundHistory> background_to_restore,
                     bool preserve_scene_preview_models,
                     bool preserve_scene_preview_camera) {
    if (path.empty() || load_state_.running) return;
    auto load_started_at = std::chrono::steady_clock::now();

    std::map<std::string, OtherTrack> old_other;
    if (preserve_settings) {
        for (const auto& t : model_.other_tracks) old_other[t.key] = t;
    }

    stop_loader();
    {
        std::lock_guard<std::mutex> lock(log_mutex_);
        logs_.clear();
        error_count_.store(0, std::memory_order_relaxed);
        warn_count_.store(0, std::memory_order_relaxed);
    }
    load_state_.running = true;
    load_state_.pending_started_at.reset();
    set_program_status("status.map_loading");
    add_log(std::string("Start loading file: ") + path);

    bool has_cp = preserve_settings && has_model_ && model_.has_cp_arb;
    double cp0 = has_cp ? model_.cp_arb[0] : 0.0;
    double cp1 = has_cp ? model_.cp_arb[1] : 0.0;
    double cp2 = has_cp ? model_.cp_arb[2] : 25.0;
    LoadModelOptions load_options;
    load_options.full_edit_registry = false;
    load_options.load_profile = "preview";

    try {
        load_state_.worker = std::thread([this, path, has_cp, cp0, cp1, cp2, old_other, preserve_settings,
                               record_history, background_to_restore, load_started_at,
                               preserve_scene_preview_models,
                               preserve_scene_preview_camera, load_options]() mutable {
            LoadResult result = load_map_worker(path, unit_distance_, has_cp, cp0, cp1, cp2, load_options);
            result.started_at = load_started_at;
            result.preserve_settings = preserve_settings;
            result.record_history = record_history;
            result.preserve_scene_preview_models = preserve_scene_preview_models;
            result.preserve_scene_preview_camera = preserve_scene_preview_camera;
            result.background_to_restore = background_to_restore;
            if (result.ok && preserve_settings) {
                for (auto& t : result.model.other_tracks) {
                    auto it = old_other.find(t.key);
                    if (it != old_other.end()) {
                        t.visible = it->second.visible;
                        t.color = it->second.color;
                        t.range_min = it->second.range_min;
                        t.range_max = it->second.range_max;
                    }
                }
            }
            {
                std::lock_guard<std::mutex> lock(load_state_.result_mutex);
                load_state_.pending_result = std::move(result);
            }
            load_state_.running = false;
            wake_main_window();
        });
    } catch (const std::exception& e) {
        handle_loader_start_failure(e.what());
    }
}

void App::begin_edit_metadata_load() {
    if (!has_model_ || file_path_.empty() || edit_registry_loaded_ || load_state_.running) return;
    auto load_started_at = std::chrono::steady_clock::now();
    const bool has_cp = model_.has_cp_arb;
    const double cp0 = has_cp ? model_.cp_arb[0] : 0.0;
    const double cp1 = has_cp ? model_.cp_arb[1] : 0.0;
    const double cp2 = has_cp ? model_.cp_arb[2] : 25.0;

    stop_loader();
    load_state_.running = true;
    load_state_.pending_started_at.reset();
    set_program_status("status.edit.loading_metadata");
    add_log("[info]gui_kme.cpp: loading edit metadata");

    LoadModelOptions load_options;
    load_options.full_edit_registry = true;
    load_options.load_profile = "edit";
    std::string path = file_path_;

    try {
        load_state_.worker = std::thread([this, path, has_cp, cp0, cp1, cp2, load_started_at, load_options]() mutable {
            LoadResult result = load_map_worker(path, unit_distance_, has_cp, cp0, cp1, cp2, load_options);
            result.started_at = load_started_at;
            result.edit_metadata_only = true;
            {
                std::lock_guard<std::mutex> lock(load_state_.result_mutex);
                load_state_.pending_result = std::move(result);
            }
            load_state_.running = false;
            wake_main_window();
        });
    } catch (const std::exception& e) {
        handle_loader_start_failure(e.what());
    }
}

void App::apply_load_result(LoadResult result) {
    if (result.edit_metadata_only) {
        apply_edit_metadata_result(std::move(result));
        return;
    }
    if (!result.ok) {
        load_state_.pending_started_at.reset();
        pending_scene_preview_started_at_.reset();
        set_program_status("status.map_load_failed");
        add_log(LogSeverity::Error, "Error during loading: " + result.error);
        if (result.handle) kv_free(result.handle);
        return;
    }
    if (handle_) kv_free(handle_);
    handle_ = result.handle;
    model_ = std::move(result.model);
    edit_registry_loaded_ = result.full_edit_registry;
    // A full disk load creates a new maploader identity session. Never keep an
    // inspector request or pending ledger whose stable editIds belong to the
    // replaced handle, including an ordinary same-file Reload with no changes.
    clear_pending_edit_state();
    // A successful disk load starts a new edit batch even when it reloads the
    // same file with no pending ledger.
    distance_resolution_choices_.clear();
    distance_resolution_workflow_ = DistanceResolutionWorkflowState{};
    text_preview_.placement = TextPreviewPlacementState{};
    edit_memory_matches_pending_ledger_ = pending_edit_changes_.empty();
    invalidate_table_cache();
    has_model_ = true;
    rebuild_marker_overlay_cache();
    reset_marker_visibility();
    scene_preview_dirty_ = true;
    scene_preview_preserve_models_on_rebuild_ =
        scene_preview_started_ && result.preserve_scene_preview_models;
    scene_preview_preserve_camera_on_rebuild_ =
        scene_preview_started_ && result.preserve_scene_preview_camera;
    if (!scene_preview_started_ && scene_preview_canvas_) {
        scene_preview_canvas_->clear_scene();
        scene_preview_preserve_models_on_rebuild_ = false;
        scene_preview_preserve_camera_on_rebuild_ = false;
    }
    file_path_ = result.path;
    refresh_text_preview_after_map_load();
    dmin_ = model_.default_min;
    dmax_ = model_.default_max;
    plot_min_ = dmin_;
    plot_max_ = dmax_;
    cp_start_ = model_.cp_arb[0];
    cp_end_ = model_.cp_arb[1];
    cp_interval_ = model_.cp_arb[2];
    if (!result.preserve_settings) {
        plan_view_.fitted = false;
        clear_measure();
    }
    reset_profile_axes_next_ = true;
    reset_radius_axes_next_ = true;
    profile_x_span_ = 0.0;
    radius_x_span_ = 0.0;
    profile_x_zoom_pending_ = false;
    profile_y_zoom_pending_ = false;
    radius_x_zoom_pending_ = false;
    std::ostringstream timing;
    timing << std::fixed << std::setprecision(3)
           << "profile=" << result.load_profile
           << ", maploader=" << result.maploader_seconds << "s"
           << ", model=" << result.model_build_seconds << "s"
           << ", snapshot_build=" << model_.snapshot_build_seconds << "s"
           << ", snapshot_hydrate=" << model_.snapshot_hydrate_seconds << "s"
           << ", buffer copy=" << model_.buffer_copy_seconds << "s";
    add_log("Load timing: " + timing.str());
    for (const std::string& warning : model_.scene_track_key_warnings) add_log(warning);
    add_log("Map loaded: " + result.path);
    set_program_status("status.map_loaded");
    if (result.background_to_restore) {
        apply_background_history(*result.background_to_restore);
    } else if (!result.preserve_settings) {
        clear_background_image();
    }
    if (result.record_history) touch_recent_map(result.path);
    load_state_.pending_started_at = result.started_at;
    if (!show_plots_window_) finish_pending_load_timing_after_plan_data_ready();
    if (edit_mode_enabled_ && has_model_ && !file_path_.empty() &&
        !edit_registry_loaded_ && !load_state_.running) {
        begin_edit_metadata_load();
    }
}

bool source_file_hashes_match(const MapModel& current, const MapModel& edit_model,
                              std::string& error) {
    std::map<std::string, std::string> current_hashes;
    for (const EditSourceFileInfo& file : current.edit_files) {
        current_hashes[file.file_path] = file.source_hash;
    }
    for (const EditSourceFileInfo& file : edit_model.edit_files) {
        auto it = current_hashes.find(file.file_path);
        if (it == current_hashes.end()) {
            error = "edit metadata includes a source file that is not in the current preview: " + file.file_path;
            return false;
        }
        if (!it->second.empty() && !file.source_hash.empty() && it->second != file.source_hash) {
            error = "source file changed since preview load: " + file.file_path;
            return false;
        }
    }
    return true;
}

bool table_row_count_matches(const char* label,
                             const std::vector<TableRow>& current,
                             const std::vector<TableRow>& edit_rows,
                             std::string& error) {
    if (current.size() == edit_rows.size()) return true;
    error = std::string("edit metadata row count mismatch for ") + label +
        ": preview=" + std::to_string(current.size()) +
        ", edit=" + std::to_string(edit_rows.size());
    return false;
}

bool edit_metadata_row_counts_match(const MapModel& current, const MapModel& edit_model,
                                    std::string& error) {
    return table_row_count_matches("curve", current.curve_rows, edit_model.curve_rows, error) &&
        table_row_count_matches("gradient", current.gradient_rows, edit_model.gradient_rows, error) &&
        table_row_count_matches("otherTrack.change", current.other_track_changes,
                                edit_model.other_track_changes, error) &&
        table_row_count_matches("station.put", current.station_list_rows, edit_model.station_list_rows, error) &&
        table_row_count_matches("station.list", current.station_definition_rows, edit_model.station_definition_rows, error) &&
        table_row_count_matches("structure.put", current.structures, edit_model.structures, error) &&
        table_row_count_matches("structure.between", current.structures_between, edit_model.structures_between, error) &&
        table_row_count_matches("structure.model", current.structure_models, edit_model.structure_models, error) &&
        table_row_count_matches("otherTrain.definition", current.other_trains, edit_model.other_trains, error) &&
        table_row_count_matches("otherTrain.stop", current.other_train_stops, edit_model.other_train_stops, error) &&
        table_row_count_matches("otherTrain.structureKey", current.other_train_structure_keys, edit_model.other_train_structure_keys, error) &&
        table_row_count_matches("otherTrain.sound3DKey", current.other_train_sound_3d_keys, edit_model.other_train_sound_3d_keys, error) &&
        table_row_count_matches("signal.aspect", current.signal_aspects, edit_model.signal_aspects, error) &&
        table_row_count_matches("signal.put", current.signals, edit_model.signals, error) &&
        table_row_count_matches("beacon.put", current.beacons, edit_model.beacons, error) &&
        table_row_count_matches("preTrain.pass", current.pretrains, edit_model.pretrains, error) &&
        table_row_count_matches("sound.list", current.sound_list, edit_model.sound_list, error) &&
        table_row_count_matches("sound3D.list", current.sound_3d_list,
                                edit_model.sound_3d_list, error) &&
        table_row_count_matches("repeater", current.repeaters, edit_model.repeaters, error) &&
        table_row_count_matches("irregularity.change", current.irregularities, edit_model.irregularities, error) &&
        table_row_count_matches("mapSound.play", current.map_sounds, edit_model.map_sounds, error) &&
        table_row_count_matches("mapSound3D.put", current.map_sound_3d, edit_model.map_sound_3d, error) &&
        table_row_count_matches("rollingNoise.change", current.rolling_noises, edit_model.rolling_noises, error) &&
        table_row_count_matches("flangeNoise.change", current.flange_noises, edit_model.flange_noises, error) &&
        table_row_count_matches("jointNoise.play", current.joint_noises, edit_model.joint_noises, error) &&
        table_row_count_matches("background.change", current.backgrounds, edit_model.backgrounds, error) &&
        table_row_count_matches("adhesion.change", current.adhesions, edit_model.adhesions, error) &&
        table_row_count_matches("cabIlluminance.change", current.cab_illuminance, edit_model.cab_illuminance, error) &&
        table_row_count_matches("fog.change", current.fogs, edit_model.fogs, error) &&
        table_row_count_matches("drawDistance.change", current.draw_distances,
                                edit_model.draw_distances, error) &&
        table_row_count_matches("speedlimit", current.speed_limit_rows,
                                edit_model.speed_limit_rows, error) &&
        table_row_count_matches("section.begin", current.section_begins,
                                edit_model.section_begins, error) &&
        table_row_count_matches("section.speedLimit", current.section_speed_limits,
                                edit_model.section_speed_limits, error);
}

void merge_table_row_edit_metadata(std::vector<TableRow>& current,
                                   const std::vector<TableRow>& edit_rows) {
    for (size_t i = 0; i < current.size(); ++i) {
        current[i].edit_id = edit_rows[i].edit_id;
        current[i].source = edit_rows[i].source;
    }
}

void merge_edit_metadata(MapModel& current, MapModel&& edit_model) {
    current.edit_files = std::move(edit_model.edit_files);
    current.edit_statements = std::move(edit_model.edit_statements);
    current.edit_elements = std::move(edit_model.edit_elements);
    merge_table_row_edit_metadata(current.curve_rows, edit_model.curve_rows);
    merge_table_row_edit_metadata(current.gradient_rows, edit_model.gradient_rows);
    merge_table_row_edit_metadata(current.other_track_changes,
                                  edit_model.other_track_changes);
    annotate_own_track_transition_links(current);
    merge_table_row_edit_metadata(current.station_list_rows, edit_model.station_list_rows);
    merge_table_row_edit_metadata(current.station_definition_rows, edit_model.station_definition_rows);
    merge_table_row_edit_metadata(current.structures, edit_model.structures);
    merge_table_row_edit_metadata(current.structures_between, edit_model.structures_between);
    merge_table_row_edit_metadata(current.structure_models, edit_model.structure_models);
    merge_table_row_edit_metadata(current.other_trains, edit_model.other_trains);
    merge_table_row_edit_metadata(current.other_train_stops, edit_model.other_train_stops);
    merge_table_row_edit_metadata(current.other_train_structure_keys, edit_model.other_train_structure_keys);
    merge_table_row_edit_metadata(current.other_train_sound_3d_keys, edit_model.other_train_sound_3d_keys);
    merge_table_row_edit_metadata(
        current.signal_aspects, edit_model.signal_aspects);
    merge_table_row_edit_metadata(current.signals, edit_model.signals);
    merge_table_row_edit_metadata(current.beacons, edit_model.beacons);
    merge_table_row_edit_metadata(current.pretrains, edit_model.pretrains);
    merge_table_row_edit_metadata(current.sound_list, edit_model.sound_list);
    merge_table_row_edit_metadata(current.sound_3d_list, edit_model.sound_3d_list);
    merge_table_row_edit_metadata(current.repeaters, edit_model.repeaters);
    merge_table_row_edit_metadata(current.irregularities, edit_model.irregularities);
    merge_table_row_edit_metadata(current.map_sounds, edit_model.map_sounds);
    merge_table_row_edit_metadata(current.map_sound_3d, edit_model.map_sound_3d);
    merge_table_row_edit_metadata(current.rolling_noises, edit_model.rolling_noises);
    merge_table_row_edit_metadata(current.flange_noises, edit_model.flange_noises);
    merge_table_row_edit_metadata(current.joint_noises, edit_model.joint_noises);
    merge_table_row_edit_metadata(current.backgrounds, edit_model.backgrounds);
    merge_table_row_edit_metadata(current.adhesions, edit_model.adhesions);
    merge_table_row_edit_metadata(current.cab_illuminance, edit_model.cab_illuminance);
    merge_table_row_edit_metadata(current.fogs, edit_model.fogs);
    merge_table_row_edit_metadata(current.draw_distances, edit_model.draw_distances);
    merge_table_row_edit_metadata(current.speed_limit_rows,
                                  edit_model.speed_limit_rows);
    merge_table_row_edit_metadata(current.section_begins, edit_model.section_begins);
    merge_table_row_edit_metadata(current.section_speed_limits,
                                  edit_model.section_speed_limits);
    bind_station_position_edit_ids(current);
}

void App::apply_edit_metadata_result(LoadResult result) {
    if (!result.ok) {
        set_program_status("status.map_loaded");
        add_log("[error]gui_kme.cpp: edit metadata load failed: " + result.error);
        if (result.handle) kv_free(result.handle);
        return;
    }
    std::string error;
    if (!has_model_ || result.path != file_path_) {
        error = "edit metadata entry path no longer matches the current preview";
    } else if (!source_file_hashes_match(model_, result.model, error)) {
        // error set by helper
    } else if (!edit_metadata_row_counts_match(model_, result.model, error)) {
        // error set by helper
    }
    if (!error.empty()) {
        set_program_status("status.map_loaded");
        add_log("[warn]gui_kme.cpp: edit metadata discarded: " + error);
        add_log("[warn]gui_kme.cpp: reload from disk before editing this map");
        if (result.handle) kv_free(result.handle);
        return;
    }

    if (handle_) kv_free(handle_);
    handle_ = result.handle;
    result.handle = nullptr;
    distance_resolution_choices_.clear();
    distance_resolution_workflow_ = DistanceResolutionWorkflowState{};
    text_preview_.placement = TextPreviewPlacementState{};
    edit_memory_matches_pending_ledger_ = pending_edit_changes_.empty();
    merge_edit_metadata(model_, std::move(result.model));
    edit_registry_loaded_ = true;
    invalidate_table_cache();
    rebuild_marker_overlay_cache();
    sync_marker_visibility_sizes();
    if (scene_preview_started_ && scene_preview_canvas_) {
        std::string scene_error;
        if (!scene_preview_canvas_->refresh_scene_dynamic_content(model_, station_jump_index_, scene_error)) {
            add_log("[warn]gui_kme.cpp: 3D scene dynamic metadata refresh failed, scheduling full rebuild: " +
                    (scene_error.empty() ? std::string("unknown error") : scene_error));
            scene_preview_dirty_ = true;
            scene_preview_preserve_models_on_rebuild_ = true;
            scene_preview_preserve_camera_on_rebuild_ = true;
        } else if (!scene_preview_canvas_->refresh_scene_route_stations(model_, scene_error)) {
            add_log("[warn]gui_kme.cpp: 3D scene marker metadata refresh failed, scheduling full rebuild: " +
                    (scene_error.empty() ? std::string("unknown error") : scene_error));
            scene_preview_dirty_ = true;
            scene_preview_preserve_models_on_rebuild_ = true;
            scene_preview_preserve_camera_on_rebuild_ = true;
        }
    }
    add_log("[info]gui_kme.cpp: edit metadata loaded");
    set_program_status(edit_mode_enabled_ ? "status.edit.mode_enabled" : "status.map_loaded");
}

void App::finish_pending_load_timing(std::chrono::steady_clock::time_point finished_at) {
    if (!load_state_.pending_started_at) return;

    double elapsed_seconds = std::chrono::duration<double>(
        finished_at - *load_state_.pending_started_at).count();
    load_state_.pending_started_at.reset();

    const std::string elapsed = format_elapsed_seconds_value(elapsed_seconds);
    add_log("Map loaded in " + elapsed + "s");
    set_program_status("status.map_loaded", elapsed);
}

void App::regenerate_geometry() {
    if (!handle_ || load_state_.running) return;
    if (!kv_generate_geometry(handle_, unit_distance_, 1, cp_start_, cp_end_, cp_interval_)) {
        const char* err = kv_get_last_error();
        add_log(std::string("[ERROR]") + (err ? err : "geometry failed"));
        return;
    }
    std::map<std::string, OtherTrack> old_other;
    for (const auto& t : model_.other_tracks) old_other[t.key] = t;
    try {
        LoadModelOptions options;
        options.full_edit_registry = edit_registry_loaded_;
        MapModel updated = build_model_from_handle(handle_, file_path_, options);
        for (auto& t : updated.other_tracks) {
            auto it = old_other.find(t.key);
            if (it != old_other.end()) {
                t.visible = it->second.visible;
                t.color = it->second.color;
                t.range_min = it->second.range_min;
                t.range_max = it->second.range_max;
            }
        }
        model_ = std::move(updated);
        edit_registry_loaded_ = options.full_edit_registry;
        invalidate_table_cache();
        rebuild_marker_overlay_cache();
        sync_marker_visibility_sizes();
        scene_preview_dirty_ = true;
        model_.has_cp_arb = true;
        model_.cp_arb[0] = cp_start_;
        model_.cp_arb[1] = cp_end_;
        model_.cp_arb[2] = cp_interval_;
        dmin_ = plot_min_;
        dmax_ = plot_max_;
        reset_profile_axes_next_ = true;
        reset_radius_axes_next_ = true;
        profile_x_span_ = 0.0;
        radius_x_span_ = 0.0;
        profile_x_zoom_pending_ = false;
        profile_y_zoom_pending_ = false;
        radius_x_zoom_pending_ = false;
        for (const std::string& warning : model_.scene_track_key_warnings) add_log(warning);
        add_log("Geometry regenerated by maploader");
    } catch (const std::exception& e) {
        add_log(std::string("[ERROR]") + e.what());
    }
}

App::LoadResult App::load_map_worker(std::string path, double unit_distance, bool has_cp, double cp_start, double cp_end, double cp_step) {
    return load_map_worker(std::move(path), unit_distance, has_cp, cp_start, cp_end, cp_step, LoadModelOptions{});
}

App::LoadResult App::load_map_worker(std::string path, double unit_distance, bool has_cp, double cp_start, double cp_end, double cp_step,
                                      LoadModelOptions options) {
    if (options.full_edit_registry) options.load_profile = "edit";
    if (options.load_profile.empty()) options.load_profile = "preview";
    auto started_at = std::chrono::steady_clock::now();
    auto elapsed_seconds = [&]() {
        return std::chrono::duration<double>(std::chrono::steady_clock::now() - started_at).count();
    };
    LoadResult out;
    out.path = path;
    out.full_edit_registry = options.full_edit_registry;
    out.load_profile = options.load_profile;
    auto maploader_started_at = std::chrono::steady_clock::now();
    unsigned load_flags = options.full_edit_registry ? KV_LOAD_EDIT_METADATA : KV_LOAD_PREVIEW;
    void* handle = kv_load_map_ex(path.c_str(), unit_distance, load_flags);
    auto maploader_finished_at = std::chrono::steady_clock::now();
    out.maploader_seconds = std::chrono::duration<double>(maploader_finished_at - maploader_started_at).count();
    if (!handle) {
        const char* err = kv_get_last_error();
        out.error = err ? err : "maploader failed";
        out.elapsed_seconds = elapsed_seconds();
        return out;
    }
    if (has_cp) {
        if (!kv_generate_geometry(handle, unit_distance, 1, cp_start, cp_end, cp_step)) {
            const char* err = kv_get_last_error();
            out.error = err ? err : "geometry failed";
            out.elapsed_seconds = elapsed_seconds();
            kv_free(handle);
            return out;
        }
    }
    try {
        auto model_started_at = std::chrono::steady_clock::now();
        out.model = build_model_from_handle(handle, path, options);
        out.model_build_seconds = std::chrono::duration<double>(
            std::chrono::steady_clock::now() - model_started_at).count();
        if (has_cp) {
            out.model.has_cp_arb = true;
            out.model.cp_arb[0] = cp_start;
            out.model.cp_arb[1] = cp_end;
            out.model.cp_arb[2] = cp_step;
        }
        out.handle = handle;
        out.ok = true;
        out.elapsed_seconds = elapsed_seconds();
    } catch (const std::exception& e) {
        out.error = e.what();
        out.elapsed_seconds = elapsed_seconds();
        kv_free(handle);
    }
    return out;
}

MapModel App::build_model_from_handle(void* handle, const std::string& path,
                                      LoadModelOptions options) {
    KvMapSnapshot snapshot{};
    const auto snapshot_started_at = std::chrono::steady_clock::now();
    if (!kv_get_map_snapshot(handle, KV_MAP_SNAPSHOT_VERSION,
                             &snapshot, sizeof(snapshot))) {
        const char* error = kv_get_last_error();
        throw std::runtime_error(std::string("kv_get_map_snapshot failed") +
            (error && *error ? ": " + std::string(error) : std::string{}));
    }
    if (snapshot.version != KV_MAP_SNAPSHOT_VERSION ||
        snapshot.structure_size < sizeof(KvMapSnapshot)) {
        throw std::runtime_error("map snapshot version or structure size mismatch");
    }
    if (options.full_edit_registry &&
        (snapshot.capabilities & KV_MAP_CAP_EDIT_METADATA) == 0) {
        throw std::runtime_error("map snapshot does not contain edit metadata");
    }
    const double snapshot_call_seconds = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - snapshot_started_at).count();
    return hydrate_map_snapshot(snapshot, path, snapshot_call_seconds);

}

void set_inspector_row_field_value(TableRow& row,
                                   const std::string& row_kind,
                                   const std::string& field_key,
                                   const std::string& value,
                                   double distance_origin);
void normalize_station_preview_rows(MapModel& model);

void App::clear_pending_edit_state() {
    clear_scene_placement_edit_target();
    pending_edit_changes_.clear();
    edit_memory_matches_pending_ledger_ = true;
    original_edit_rows_.clear();
    distance_resolution_choices_.clear();
    distance_resolution_workflow_ = DistanceResolutionWorkflowState{};
    text_preview_.placement = TextPreviewPlacementState{};
    inspector_ = MapElementInspectorState{};
    pending_inspector_request_.reset();
    pending_delete_request_.reset();
    new_element_wizard_ = NewElementWizardState{};
    discard_all_editable_list_drafts();
}

bool App::has_pending_edits() const {
    return !pending_edit_changes_.empty();
}

bool App::has_unsaved_edit_state() const {
    return has_pending_edits() || has_unapplied_editable_list_drafts();
}

bool App::row_has_pending_edit(const std::string& edit_id) const {
    return !edit_id.empty() && pending_edit_changes_.find(edit_id) != pending_edit_changes_.end();
}

bool App::row_is_pending_delete(const std::string& edit_id) const {
    auto it = pending_edit_changes_.find(edit_id);
    return it != pending_edit_changes_.end() && it->second.operation == "delete";
}

bool App::edit_actions_available() const {
    return edit_mode_enabled_ && edit_registry_loaded_ && handle_ && has_model_ && !load_state_.running;
}

void App::set_edit_mode_enabled(bool enabled) {
    if (enabled == edit_mode_enabled_) return;
    if (enabled && !settings_.edit_mode_warning_suppressed) {
        edit_mode_warning_dont_show_ = false;
        popups_.edit_mode_warning = true;
        wake_main_window();
        return;
    }
    if (!enabled && has_unsaved_edit_state()) {
        request_close_action(PendingCloseAction::DisableEditMode);
        return;
    }

    apply_edit_mode_enabled(enabled);
}

void App::apply_edit_mode_enabled(bool enabled) {
    edit_mode_enabled_ = enabled;
    settings_.edit_mode_enabled = edit_mode_enabled_;
    save_user_settings(settings_);

    if (!edit_mode_enabled_) {
        clear_scene_placement_edit_target();
        inspector_.open = false;
        pending_inspector_request_.reset();
        distance_resolution_choices_.clear();
        distance_resolution_workflow_ = DistanceResolutionWorkflowState{};
        text_preview_.placement = TextPreviewPlacementState{};
        discard_all_editable_list_drafts();
        edit_memory_matches_pending_ledger_ = pending_edit_changes_.empty();
        set_program_status("status.edit.mode_disabled");
        add_log("[info]gui_kme.cpp: edit mode disabled");
        return;
    }

    set_program_status("status.edit.mode_enabled");
    add_log("[info]gui_kme.cpp: edit mode enabled");
    if (has_model_ && !file_path_.empty() && !edit_registry_loaded_ && !load_state_.running) {
        begin_edit_metadata_load();
    }
}

void App::request_close_action(PendingCloseAction action) {
    if (action == PendingCloseAction::None) return;
    if (has_unsaved_edit_state()) {
        pending_close_action_ = action;
        popups_.close_unsaved_confirm = true;
        wake_main_window();
        return;
    }

    if (action == PendingCloseAction::DisableEditMode) {
        apply_edit_mode_enabled(false);
    } else if (action == PendingCloseAction::ExitApplication) {
        PostQuitMessage(0);
    }
}

void App::request_exit() {
    request_close_action(PendingCloseAction::ExitApplication);
}

bool App::snapshot_local_preview_row(const std::string& edit_id, const std::string& row_kind) {
    if (edit_id.empty() || row_kind.empty()) return false;
    if (original_edit_rows_.find(edit_id) != original_edit_rows_.end()) return true;
    std::vector<TableRow>* rows = inspector_rows_for_kind(model_, row_kind);
    if (!rows) return false;
    size_t row_index = 0;
    if (!find_row_index_by_edit_id(*rows, edit_id, row_index)) return false;
    original_edit_rows_[edit_id] = MapElementPreviewSnapshot{row_kind, (*rows)[row_index], row_index};
    return true;
}

bool App::restore_local_preview_change(const std::string& edit_id, const std::string& row_kind,
                                       bool refresh_preview) {
    auto snapshot = original_edit_rows_.find(edit_id);
    if (snapshot == original_edit_rows_.end()) return true;
    const std::string effective_row_kind = row_kind.empty() ? snapshot->second.row_kind : row_kind;
    std::vector<TableRow>* rows = inspector_rows_for_kind(model_, effective_row_kind);
    if (!rows) return false;

    size_t row_index = 0;
    if (find_row_index_by_edit_id(*rows, edit_id, row_index)) {
        (*rows)[row_index] = snapshot->second.row;
    } else {
        size_t insert_index = std::min(snapshot->second.row_index, rows->size());
        rows->insert(rows->begin() + static_cast<std::ptrdiff_t>(insert_index), snapshot->second.row);
    }
    bool requires_full_scene_refresh = false;
    auto pending = pending_edit_changes_.find(edit_id);
    if (pending != pending_edit_changes_.end()) {
        requires_full_scene_refresh =
            (effective_row_kind == "structure.put" &&
             pending->second.field_changes.find("structureKey") !=
                 pending->second.field_changes.end()) ||
            (effective_row_kind == "signal.put" &&
             pending->second.field_changes.find("signalAspectKey") !=
                 pending->second.field_changes.end());
    }
    original_edit_rows_.erase(snapshot);
    if (refresh_preview) {
        refresh_local_preview_after_edit(
            effective_row_kind, requires_full_scene_refresh ? std::string{} : edit_id);
    }
    return true;
}

bool App::apply_local_preview_change(const MapElementPendingChange& change,
                                     bool refresh_preview) {
    if (change.edit_id.empty() || change.row_kind.empty()) return false;
    std::vector<TableRow>* rows = inspector_rows_for_kind(model_, change.row_kind);
    if (!rows) return false;

    size_t row_index = 0;
    const bool row_found = find_row_index_by_edit_id(*rows, change.edit_id, row_index);
    if (!row_found && change.operation != "delete") return false;
    if (row_found && !snapshot_local_preview_row(change.edit_id, change.row_kind)) return false;

    if (change.operation == "delete") {
        if (row_found) rows->erase(rows->begin() + static_cast<std::ptrdiff_t>(row_index));
        if (refresh_preview) refresh_local_preview_after_edit(change.row_kind);
        return true;
    }

    if (change.operation != "update" || !row_found) return false;
    TableRow& row = (*rows)[row_index];
    for (const auto& field : change.field_changes) {
        set_inspector_row_field_value(
            row, change.row_kind, field.first, field.second,
            model_.distance_origin);
    }
    if ((change.row_kind == "structure.model" ||
         change.row_kind == "sound.list" ||
         change.row_kind == "sound3D.list") &&
        change.field_changes.find("filePath") != change.field_changes.end()) {
        row.cells["resolvedFilePath"] = resolve_list_asset_path(
            row.source.file_path, table_cell(row, "filePath"));
    }
    const bool requires_full_scene_refresh =
        (change.row_kind == "structure.put" &&
         change.field_changes.find("structureKey") != change.field_changes.end()) ||
        (change.row_kind == "signal.put" &&
         change.field_changes.find("signalAspectKey") != change.field_changes.end());
    if (refresh_preview) {
        refresh_local_preview_after_edit(
            change.row_kind, requires_full_scene_refresh ? std::string{} : change.edit_id);
    }
    return true;
}

Canvas3DPlacementEditTarget scene_edit_target_from_row(
    const TableRow& row, Canvas3DSceneEditKind kind = Canvas3DSceneEditKind::Structure) {
    Canvas3DPlacementEditTarget target;
    target.kind = kind;
    target.edit_id = row.edit_id;
    target.track_key = table_cell(row, "trackKey");
    target.distance = table_cell_number(row, "distance");
    target.x = table_cell_number(row, "x");
    target.y = table_cell_number(row, "y");
    target.z = table_cell_number(row, "z");
    target.rx = table_cell_number(row, "rx");
    target.ry = table_cell_number(row, "ry");
    target.rz = table_cell_number(row, "rz");
    target.tilt = table_cell_number(row, "tilt");
    target.span = table_cell_number(row, "span");
    return target;
}

bool App::update_scene_placement_instance_from_model(const std::string& edit_id,
                                                     const std::string& row_kind) {
    if (!scene_preview_started_ || !scene_preview_canvas_) return true;
    const bool signal = row_kind == "signal.put";
    const std::vector<TableRow>& rows = signal ? model_.signals : model_.structures;
    size_t row_index = 0;
    if (!find_row_index_by_edit_id(rows, edit_id, row_index)) return false;
    return scene_preview_canvas_->update_scene_placement_instance(
        scene_edit_target_from_row(rows[row_index], signal
            ? Canvas3DSceneEditKind::Signal
            : Canvas3DSceneEditKind::Structure));
}

bool App::update_scene_repeater_segment_from_model(const std::string& edit_id) {
    if (!scene_preview_started_ || !scene_preview_canvas_) return true;
    size_t row_index = 0;
    if (!find_row_index_by_edit_id(model_.repeaters, edit_id, row_index)) return false;
    return scene_preview_canvas_->update_scene_repeater_segment(
        scene_edit_target_from_row(model_.repeaters[row_index],
                                   Canvas3DSceneEditKind::Repeater));
}

void App::refresh_local_preview_after_edit(const std::string& row_kind,
                                           const std::string& edit_id) {
    if (row_kind == "station.put" || row_kind == "station.list") {
        normalize_station_preview_rows(model_);
    }
    if (row_kind == "speedlimit") rebuild_speed_limit_runtime_cache(model_);
    const bool alignment_changed = row_kind == "curve" || row_kind == "gradient" ||
        row_kind == "otherTrack.change";
    if (alignment_changed && scene_preview_started_ && scene_preview_canvas_) {
        scene_preview_dirty_ = true;
        scene_preview_preserve_models_on_rebuild_ = true;
        scene_preview_preserve_camera_on_rebuild_ = true;
    }
    Canvas3DSceneMapRefreshOptions map_refresh;
    map_refresh.route_stations = row_kind == "station.put" || row_kind == "station.list";
    static constexpr std::array<const char*, 20> k_marker_row_kinds = {
        "station.put", "station.list", "irregularity.change", "beacon.put",
        "mapSound.play", "mapSound3D.put", "rollingNoise.change",
        "flangeNoise.change", "jointNoise.play", "background.change",
        "adhesion.change", "cabIlluminance.change", "fog.change",
        "drawDistance.change", "speedlimit", "section.begin",
        "section.speedLimit", "curve", "gradient",
        "otherTrack.change",
    };
    map_refresh.markers = std::any_of(
        k_marker_row_kinds.begin(), k_marker_row_kinds.end(),
        [&](const char* candidate) { return row_kind == candidate; });
    map_refresh.fog = row_kind == "fog.change";
    map_refresh.draw_distances = row_kind == "drawDistance.change";
    map_refresh.speed_limits = row_kind == "speedlimit";
    map_refresh.section_signals = row_kind == "section.begin" ||
        row_kind == "section.speedLimit";
    if ((map_refresh.route_stations || map_refresh.markers || map_refresh.fog ||
         map_refresh.draw_distances || map_refresh.speed_limits ||
         map_refresh.section_signals) &&
        scene_preview_started_ && scene_preview_canvas_) {
        std::string error;
        if (!scene_preview_canvas_->refresh_scene_map_content(model_, map_refresh, error)) {
            add_log("[warn]gui_kme.cpp: 3D scene marker refresh failed, scheduling full rebuild: " +
                    (error.empty() ? std::string("unknown error") : error));
            scene_preview_dirty_ = true;
            scene_preview_preserve_models_on_rebuild_ = true;
            scene_preview_preserve_camera_on_rebuild_ = true;
        }
    }
    if (row_kind == "speedlimit") {
        refresh_speed_limit_table_cache();
        rebuild_speed_limit_marker_overlay_cache();
    } else {
        invalidate_table_cache();
        rebuild_marker_overlay_cache();
    }
    sync_marker_visibility_sizes();

    bool placement_instance_synced = false;
    if ((row_kind == "structure.put" || row_kind == "signal.put") && !edit_id.empty()) {
        placement_instance_synced =
            update_scene_placement_instance_from_model(edit_id, row_kind);
    }
    bool repeater_segment_synced = false;
    if (row_kind == "repeater" && !edit_id.empty()) {
        const auto pending = pending_edit_changes_.find(edit_id);
        const bool position_only = pending != pending_edit_changes_.end() &&
            !pending->second.field_changes.empty() &&
            std::all_of(pending->second.field_changes.begin(),
                        pending->second.field_changes.end(),
                        [](const auto& field) {
                            return field.first == "x" || field.first == "y" || field.first == "z";
                        });
        if (position_only) {
            repeater_segment_synced = update_scene_repeater_segment_from_model(edit_id);
        }
    }
    const bool affects_scene_dynamic =
        ((row_kind == "structure.put" || row_kind == "signal.put") &&
         !placement_instance_synced) ||
        row_kind == "structure.between" || row_kind == "structure.model" ||
        (row_kind == "repeater" && !repeater_segment_synced) ||
        row_kind == "background.change";
    if (affects_scene_dynamic && scene_preview_started_ && scene_preview_canvas_) {
        std::string error;
        if (!scene_preview_canvas_->refresh_scene_dynamic_content(model_, station_jump_index_, error)) {
            add_log("[warn]gui_kme.cpp: 3D scene dynamic refresh failed, scheduling full rebuild: " +
                    (error.empty() ? std::string("unknown error") : error));
            scene_preview_dirty_ = true;
            scene_preview_preserve_models_on_rebuild_ = true;
            scene_preview_preserve_camera_on_rebuild_ = true;
        }
    }
}

void App::request_element_inspector(const std::string& edit_id, const std::string& row_kind) {
    if (!edit_actions_available()) {
        if (edit_mode_enabled_ && !edit_registry_loaded_) {
            add_log("[info]gui_kme.cpp: edit metadata is still loading");
        }
        return;
    }
    if (edit_id.empty()) return;
    pending_inspector_request_ = MapElementInspectorRequest{edit_id, row_kind};
}

void App::process_pending_element_inspector() {
    if (!pending_inspector_request_) return;
    MapElementInspectorRequest request = std::move(*pending_inspector_request_);
    pending_inspector_request_.reset();
    open_element_inspector(request);
}

bool row_kind_supports_delete(const std::string& row_kind) {
    return row_kind == "structure.model" || row_kind == "structure.put" ||
        row_kind == "structure.between" || row_kind == "station.put" ||
        row_kind == "signal.put" || row_kind == "repeater" ||
        row_kind == "irregularity.change" ||
        row_kind == "beacon.put" || row_kind == "mapSound.play" ||
        row_kind == "mapSound3D.put" || row_kind == "rollingNoise.change" ||
        row_kind == "flangeNoise.change" || row_kind == "jointNoise.play" ||
        row_kind == "background.change" || row_kind == "adhesion.change" ||
        row_kind == "cabIlluminance.change" || row_kind == "fog.change" ||
        row_kind == "drawDistance.change" || row_kind == "speedlimit" ||
        row_kind == "section.begin" || row_kind == "section.speedLimit" ||
        row_kind == "curve" || row_kind == "gradient" ||
        row_kind == "otherTrack.change";
}

struct RepeaterDeleteChain {
    std::vector<size_t> begin_source_indices;
    std::optional<size_t> end_source_index;
    size_t selected_begin_index = 0;
};

std::optional<RepeaterDeleteChain> repeater_delete_chain_for_edit_id(
    const std::vector<TableRow>& rows, const std::string& edit_id) {
    const repeater_linkage::Linkage linkage =
        repeater_linkage::pair_linkage(table_repeater_events(rows));
    for (const repeater_linkage::Segment& segment : linkage.segments) {
        if (segment.begin_source_index >= rows.size() ||
            rows[segment.begin_source_index].edit_id != edit_id ||
            segment.chain_index >= linkage.chains.size()) {
            continue;
        }
        const repeater_linkage::Chain& chain = linkage.chains[segment.chain_index];
        RepeaterDeleteChain result;
        result.begin_source_indices = chain.begin_source_indices;
        result.end_source_index = chain.end_source_index;
        result.selected_begin_index = segment.chain_begin_index;
        return result;
    }
    return std::nullopt;
}

void App::request_element_delete(const std::string& edit_id, const std::string& row_kind,
                                 RepeaterDeleteMode repeater_mode) {
    if (!edit_actions_available() || edit_id.empty() || !row_kind_supports_delete(row_kind)) return;
    pending_delete_request_ = MapElementDeleteRequest{edit_id, row_kind, repeater_mode};
}

void App::process_pending_element_delete() {
    if (!pending_delete_request_) return;
    MapElementDeleteRequest request = std::move(*pending_delete_request_);
    pending_delete_request_.reset();
    delete_element_target(request);
}

const EditSourceFileInfo* find_model_source_file(const MapModel& model, const std::string& path) {
    for (const EditSourceFileInfo& file : model.edit_files) {
        if (file.file_path == path) return &file;
    }
    return nullptr;
}

std::string expected_source_hash_for_edit_target(
    const MapModel& model,
    const std::map<std::string, MapElementPendingChange>& pending_changes,
    const std::string& edit_id,
    const std::string& preferred_hash,
    const std::string& source_file_path) {
    auto pending = pending_changes.find(edit_id);
    if (pending != pending_changes.end() && !pending->second.expected_source_hash.empty()) {
        return pending->second.expected_source_hash;
    }

    // The complete pending ledger is replayed from disk on every Apply. Keep
    // its optimistic-concurrency hash pinned to that disk baseline even while
    // the active maploader handle represents a dirty in-memory working copy.
    if (!preferred_hash.empty()) return preferred_hash;
    const EditSourceFileInfo* source_file = find_model_source_file(model, source_file_path);
    if (source_file && !source_file->source_hash.empty()) return source_file->source_hash;
    return {};
}

std::string inspector_expected_source_hash(
    const MapModel& model,
    const std::map<std::string, MapElementPendingChange>& pending_changes,
    const MapElementInspectorState& inspector) {
    return expected_source_hash_for_edit_target(
        model, pending_changes, inspector.edit_id, inspector.expected_source_hash,
        inspector.source_file);
}

bool find_row_index_by_edit_id(const std::vector<TableRow>& rows,
                               const std::string& edit_id,
                               size_t& row_index) {
    if (edit_id.empty()) return false;
    for (size_t i = 0; i < rows.size(); ++i) {
        if (rows[i].edit_id == edit_id) {
            row_index = i;
            return true;
        }
    }
    return false;
}

std::string inspector_row_field_value(const TableRow& row,
                                      const std::string& row_kind,
                                      const std::string& field_key) {
    if (row_kind == "station.put") {
        if (field_key == "distance") return table_cell(row, "_distance");
        if (field_key == "stationKey") return table_cell(row, "posKey");
    }
    if (row_kind == "section.begin" || row_kind == "section.speedLimit") {
        constexpr std::string_view k_values_prefix = "values.";
        if (field_key.rfind(k_values_prefix.data(), 0) == 0) {
            const size_t index = static_cast<size_t>(std::strtoul(
                field_key.c_str() + k_values_prefix.size(), nullptr, 10));
            return table_cell(row, "value" + std::to_string(index));
        }
    }
    return table_cell(row, field_key);
}

void set_inspector_row_field_value(TableRow& row,
                                   const std::string& row_kind,
                                   const std::string& field_key,
                                   const std::string& value,
                                   double distance_origin) {
    if (row_kind == "signal.put" && field_key == "form") return;
    if (row_kind == "station.put") {
        if (field_key == "distance") {
            row.cells["_distance"] = value;
            double absolute_distance = table_cell_number(row, "_distance");
            row.cells["dist"] = format_double(absolute_distance - distance_origin, 0);
            return;
        }
        if (field_key == "stationKey") {
            row.cells["posKey"] = value;
            return;
        }
    }
    if (row_kind == "repeater") {
        constexpr std::string_view k_structure_key_prefix = "structureKeys.";
        if (field_key.rfind(k_structure_key_prefix.data(), 0) == 0) {
            if (field_key == "structureKeys.count") {
                double parsed_count = 0.0;
                if (!parse_gui_edit_number(value, &parsed_count) || parsed_count < 1.0 ||
                    std::trunc(parsed_count) != parsed_count) {
                    return;
                }
                const size_t count = static_cast<size_t>(parsed_count);
                std::string joined;
                for (size_t index = 0; index < count; ++index) {
                    const std::string key = "_structureKeys." + std::to_string(index);
                    const auto item = row.cells.find(key);
                    if (item == row.cells.end() || item->second.empty()) return;
                    if (!joined.empty()) joined += ",";
                    joined += item->second;
                }
                row.cells["structureKeys"] = std::move(joined);
            } else {
                row.cells["_" + field_key] = value;
            }
            return;
        }
    }
    if (row_kind == "section.begin" || row_kind == "section.speedLimit") {
        constexpr std::string_view k_values_prefix = "values.";
        if (field_key.rfind(k_values_prefix.data(), 0) == 0) {
            if (field_key == "values.count") {
                double parsed_count = 0.0;
                if (!parse_gui_edit_number(value, &parsed_count) || parsed_count < 1.0 ||
                    std::trunc(parsed_count) != parsed_count) {
                    return;
                }
                const size_t count = static_cast<size_t>(parsed_count);
                for (size_t index = 0; index < count; ++index) {
                    const std::string key = "_values." + std::to_string(index);
                    const auto item = row.cells.find(key);
                    if (item == row.cells.end() || item->second.empty()) return;
                    row.cells["value" + std::to_string(index)] = item->second;
                }
                for (size_t stale_index = count;; ++stale_index) {
                    const auto stale = row.cells.find(
                        "value" + std::to_string(stale_index));
                    if (stale == row.cells.end()) break;
                    row.cells.erase(stale);
                }
                row.cells["valueCount"] = std::to_string(count);
            } else {
                row.cells["_" + field_key] = value;
            }
            return;
        }
    }
    row.cells[field_key] = value;
}

void normalize_station_preview_rows(MapModel& model) {
    std::stable_sort(model.station_list_rows.begin(), model.station_list_rows.end(),
                     [](const TableRow& a, const TableRow& b) {
                         double da = table_cell_number(a, "_distance");
                         double db = table_cell_number(b, "_distance");
                         if (da != db) return da < db;
                         return table_cell_number(a, "_order") < table_cell_number(b, "_order");
                     });

    model.station_names.clear();
    for (const TableRow& definition : model.station_definition_rows) {
        const std::string key = ascii_lower(table_cell(definition, "stationKey"));
        if (!key.empty()) {
            model.station_names[key] = table_cell(definition, "stationName");
        }
    }
    model.station_positions.clear();
    model.stations.clear();
    std::set<std::string> seen;
    size_t own_row = 0;
    for (size_t i = 0; i < model.station_list_rows.size(); ++i) {
        TableRow& row = model.station_list_rows[i];
        double distance = table_cell_number(row, "_distance");
        row.cells["rowNumber"] = std::to_string(i + 1);
        row.cells["dist"] = format_double(distance - model.distance_origin, 0);

        std::string key = table_cell(row, "posKey");
        if (key.empty()) continue;
        Station station;
        station.key = key;
        auto station_name = model.station_names.find(ascii_lower(key));
        station.name = station_name == model.station_names.end() ? key : station_name->second;
        if (station.name.empty()) station.name = key;
        row.cells["stationName"] = station.name;
        station.distance = distance;
        station.mileage = distance - model.distance_origin;
        if (!model.own.empty()) {
            while (own_row + 1 < model.own.rows && model.own.at(own_row, 0) < distance) ++own_row;
            if (own_row >= model.own.rows) own_row = model.own.rows - 1;
            station.x = model.own.at(own_row, 1);
            station.y = model.own.at(own_row, 2);
            station.z = model.own.at(own_row, 3);
        }
        model.station_positions.push_back(station);
        if (seen.insert(key).second) model.stations.push_back(std::move(station));
    }
    bind_station_position_edit_ids(model);
}

int inspector_request_match_score(const TableRow& row,
                                  const MapElementInspectorRequest& request) {
    int score = 0;
    bool source_matched = false;
    if (!request.source_file.empty() && row.source.file_path == request.source_file) {
        source_matched = true;
        score += 100;
        if (request.line > 0 && row.source.line > 0) {
            int line_delta = std::abs(row.source.line - request.line);
            if (line_delta == 0) {
                score += 40;
            } else if (line_delta == 1) {
                score += 30;
            } else if (line_delta <= 3) {
                score += 10;
            } else {
                score -= std::min(line_delta, 30);
            }
        }
        if (request.column > 0 && row.source.column == request.column) score += 5;
    }

    int matched_fields = 0;
    for (const auto& field : request.field_values) {
        const std::string row_value = trim_gui_ascii_copy(
            inspector_row_field_value(row, request.row_kind, field.first));
        if (row_value == field.second) {
            score += 8;
            ++matched_fields;
        } else {
            score -= 3;
        }
    }

    const int required_field_matches = request.field_values.size() <= 1 ? 1 : 2;
    if (!source_matched && matched_fields < required_field_matches) return std::numeric_limits<int>::min();
    return score;
}

const TableRow* find_model_row_for_inspector_request(const MapModel& model,
                                                     const MapElementInspectorRequest& request,
                                                     std::string& resolved_edit_id,
                                                     size_t& resolved_row_index) {
    const std::vector<TableRow>* rows = inspector_rows_for_kind(model, request.row_kind);
    if (!rows) return nullptr;

    for (const TableRow& row : *rows) {
        if (!request.edit_id.empty() && row.edit_id == request.edit_id) {
            resolved_edit_id = request.edit_id;
            resolved_row_index = static_cast<size_t>(&row - rows->data());
            return &row;
        }
    }

    if (request.source_file.empty() && request.field_values.empty()) return nullptr;

    const TableRow* best = nullptr;
    int best_score = std::numeric_limits<int>::min();
    bool ambiguous = false;
    for (const TableRow& row : *rows) {
        if (row.edit_id.empty()) continue;
        int score = inspector_request_match_score(row, request);
        if (score > best_score) {
            best = &row;
            best_score = score;
            ambiguous = false;
        } else if (score == best_score) {
            ambiguous = true;
        }
    }

    if (!best || ambiguous || best_score == std::numeric_limits<int>::min()) return nullptr;
    resolved_edit_id = best->edit_id;
    resolved_row_index = static_cast<size_t>(best - rows->data());
    return best;
}

bool row_kind_has_source_distance_string(const std::string& row_kind) {
    static constexpr std::array<const char*, 23> k_distance_row_kinds = {
        "station.put",
        "structure.put",
        "structure.between",
        "repeater",
        "signal.put",
        "irregularity.change",
        "beacon.put",
        "mapSound.play",
        "mapSound3D.put",
        "rollingNoise.change",
        "flangeNoise.change",
        "jointNoise.play",
        "background.change",
        "adhesion.change",
        "cabIlluminance.change",
        "fog.change",
        "drawDistance.change",
        "speedlimit",
        "section.begin",
        "section.speedLimit",
        "curve",
        "gradient",
        "otherTrack.change",
    };
    return std::any_of(k_distance_row_kinds.begin(), k_distance_row_kinds.end(),
                       [&](const char* value) { return row_kind == value; });
}

MapElementKeySource map_element_key_source_for_field(
    std::string_view row_kind, std::string_view field_key) noexcept {
    if (field_key == "structureKey" &&
        (row_kind == "structure.put" || row_kind == "structure.between" ||
         row_kind == "background.change")) {
        return MapElementKeySource::Structure;
    }
    if (row_kind == "repeater" && field_key.rfind("structureKeys.", 0) == 0) {
        return MapElementKeySource::Structure;
    }
    if (row_kind == "mapSound.play" && field_key == "soundKey") {
        return MapElementKeySource::Sound;
    }
    if (row_kind == "mapSound3D.put" && field_key == "soundKey") {
        return MapElementKeySource::Sound3D;
    }
    if (row_kind == "signal.put" && field_key == "signalAspectKey") {
        return MapElementKeySource::SignalAspect;
    }
    if ((field_key == "trackKey" &&
         (row_kind == "structure.put" || row_kind == "signal.put" ||
          row_kind == "repeater")) ||
        (row_kind == "structure.between" &&
         (field_key == "trackKey1" || field_key == "trackKey2"))) {
        return MapElementKeySource::Track;
    }
    return MapElementKeySource::None;
}

std::string_view trim_ascii_view(std::string_view value) noexcept {
    while (!value.empty() &&
           std::isspace(static_cast<unsigned char>(value.front())) != 0) {
        value.remove_prefix(1);
    }
    while (!value.empty() &&
           std::isspace(static_cast<unsigned char>(value.back())) != 0) {
        value.remove_suffix(1);
    }
    return value;
}

bool resource_key_values_equal(std::string_view left, std::string_view right) noexcept {
    left = trim_ascii_view(left);
    right = trim_ascii_view(right);
    if (left.size() != right.size()) return false;
    for (size_t index = 0; index < left.size(); ++index) {
        if (ascii_lower(static_cast<unsigned char>(left[index])) !=
            ascii_lower(static_cast<unsigned char>(right[index]))) {
            return false;
        }
    }
    return true;
}

bool track_key_values_equal(std::string_view left, std::string_view right) noexcept {
    size_t left_index = 0;
    size_t right_index = 0;
    const auto skip_spaces = [](std::string_view value, size_t& index) {
        while (index < value.size() &&
               std::isspace(static_cast<unsigned char>(value[index])) != 0) {
            ++index;
        }
    };
    for (;;) {
        skip_spaces(left, left_index);
        skip_spaces(right, right_index);
        const bool left_end = left_index == left.size();
        const bool right_end = right_index == right.size();
        if (left_end || right_end) return left_end && right_end;
        if (ascii_lower(static_cast<unsigned char>(left[left_index])) !=
            ascii_lower(static_cast<unsigned char>(right[right_index]))) {
            return false;
        }
        ++left_index;
        ++right_index;
    }
}

bool is_repeater_structure_key_field(const MapElementEditFieldState& field) {
    return field.key.rfind("structureKeys.", 0) == 0;
}

bool is_section_values_field(std::string_view field) {
    return field.rfind("values.", 0) == 0;
}

bool is_section_values_field(const MapElementEditFieldState& field) {
    return is_section_values_field(field.key);
}

std::vector<std::string> split_repeater_structure_keys(const std::string& text) {
    std::vector<std::string> keys;
    size_t begin = 0;
    while (begin <= text.size()) {
        const size_t end = text.find(',', begin);
        const std::string key = trim_gui_ascii_copy(
            text.substr(begin, end == std::string::npos ? std::string::npos : end - begin));
        if (!key.empty()) keys.push_back(key);
        if (end == std::string::npos) break;
        begin = end + 1;
    }
    return keys;
}

void reindex_repeater_structure_key_fields(MapElementInspectorState& inspector) {
    size_t index = 0;
    for (MapElementEditFieldState& field : inspector.fields) {
        if (!is_repeater_structure_key_field(field)) continue;
        field.key = "structureKeys." + std::to_string(index);
        field.backend_key = field.key;
        field.label = "structureKey[" + std::to_string(index + 1) + "]";
        ++index;
    }
}

MapElementEditFieldState make_repeater_structure_key_field(
    const MapElementInspectorState& inspector, size_t index, const std::string& value) {
    MapElementEditFieldState field;
    field.key = "structureKeys." + std::to_string(index);
    field.backend_key = field.key;
    field.target_edit_id = inspector.edit_id;
    field.expected_source_hash = inspector.expected_source_hash;
    field.label = "structureKey[" + std::to_string(index + 1) + "]";
    field.original_value = index < inspector.repeater_structure_keys_original.size()
        ? inspector.repeater_structure_keys_original[index]
        : std::string{};
    field.key_source = MapElementKeySource::Structure;
    field.required = true;
    set_edit_field_buffer(field, value);
    return field;
}

const char* section_values_label_prefix(const std::string& row_kind) {
    return row_kind == "section.begin" ? "signal" : "v";
}

void reindex_section_values_fields(MapElementInspectorState& inspector) {
    size_t index = 0;
    for (MapElementEditFieldState& field : inspector.fields) {
        if (!is_section_values_field(field)) continue;
        field.key = "values." + std::to_string(index);
        field.backend_key = field.key;
        field.label = std::string(section_values_label_prefix(inspector.row_kind)) +
            std::to_string(index);
        ++index;
    }
}

MapElementEditFieldState make_section_values_field(
    const MapElementInspectorState& inspector, size_t index, const std::string& value) {
    MapElementEditFieldState field;
    field.key = "values." + std::to_string(index);
    field.backend_key = field.key;
    field.target_edit_id = inspector.edit_id;
    field.expected_source_hash = inspector.expected_source_hash;
    field.label = std::string(section_values_label_prefix(inspector.row_kind)) +
        std::to_string(index);
    field.original_value = index < inspector.section_values_original.size()
        ? inspector.section_values_original[index]
        : std::string{};
    field.numeric_constraint = MapElementNumericConstraint::Finite;
    field.required = true;
    set_edit_field_buffer(field, value);
    return field;
}

void enable_inspector_zero_method_conversion_draft(MapElementInspectorState& inspector) {
    const bool supported_row_kind = inspector.row_kind == "structure.put" ||
        inspector.row_kind == "repeater";
    if (!inspector.open || !supported_row_kind ||
        !inspector.source_method_put0 || inspector.put0_conversion_draft) {
        inspector.put0_prompt_requested = false;
        return;
    }

    auto insert_at = std::find_if(inspector.fields.begin(), inspector.fields.end(),
                                  [](const MapElementEditFieldState& field) {
                                      return field.key == "tilt";
                                  });
    const size_t insertion_index = static_cast<size_t>(
        std::distance(inspector.fields.begin(), insert_at));
    std::vector<MapElementEditFieldState> coordinates;
    coordinates.reserve(6);
    for (const char* key : {"x", "y", "z", "rx", "ry", "rz"}) {
        MapElementEditFieldState field;
        field.key = key;
        field.backend_key = key;
        field.target_edit_id = inspector.edit_id;
        field.expected_source_hash = inspector.expected_source_hash;
        field.label = key;
        field.original_value = "0";
        field.numeric_constraint = structure_edit_numeric_constraint(key);
        field.required = true;
        set_edit_field_buffer(field, "0");
        coordinates.push_back(std::move(field));
    }
    inspector.fields.insert(
        inspector.fields.begin() + static_cast<std::ptrdiff_t>(insertion_index),
        std::make_move_iterator(coordinates.begin()),
        std::make_move_iterator(coordinates.end()));
    inspector.put0_conversion_draft = true;
    inspector.put0_prompt_requested = false;
}

void capture_repeater_inspector_draft(MapElementInspectorState& inspector) {
    if (!inspector.open || inspector.row_kind != "repeater" || inspector.edit_id.empty()) return;
    RepeaterInspectorDraft draft;
    draft.begin0_conversion_draft = inspector.put0_conversion_draft;
    draft.fields.reserve(inspector.fields.size());
    for (const MapElementEditFieldState& field : inspector.fields) {
        if (!field.read_only) draft.fields.emplace_back(field.key, edit_field_buffer_text(field));
    }
    inspector.session.repeater_drafts[inspector.edit_id] = std::move(draft);
}

void restore_repeater_inspector_draft(MapElementInspectorState& inspector) {
    if (!inspector.open || inspector.row_kind != "repeater" || inspector.edit_id.empty()) return;
    const auto draft_it = inspector.session.repeater_drafts.find(inspector.edit_id);
    if (draft_it == inspector.session.repeater_drafts.end()) return;
    const RepeaterInspectorDraft& draft = draft_it->second;

    if (draft.begin0_conversion_draft) {
        enable_inspector_zero_method_conversion_draft(inspector);
    }

    std::vector<std::string> structure_keys;
    for (const auto& entry : draft.fields) {
        if (entry.first.rfind("structureKeys.", 0) == 0) {
            structure_keys.push_back(entry.second);
            continue;
        }
        MapElementEditFieldState* field = find_inspector_field(inspector, entry.first);
        if (field && !field->read_only) set_edit_field_buffer(*field, entry.second);
    }
    if (structure_keys.empty()) return;

    size_t insertion_index = inspector.fields.size();
    for (size_t index = 0; index < inspector.fields.size(); ++index) {
        if (is_repeater_structure_key_field(inspector.fields[index])) {
            insertion_index = index;
            break;
        }
        if (inspector.fields[index].key == "endDistance") {
            insertion_index = index;
            break;
        }
    }
    inspector.fields.erase(
        std::remove_if(inspector.fields.begin(), inspector.fields.end(),
                       [](const MapElementEditFieldState& field) {
                           return is_repeater_structure_key_field(field);
                       }),
        inspector.fields.end());
    insertion_index = std::min(insertion_index, inspector.fields.size());
    std::vector<MapElementEditFieldState> restored_fields;
    restored_fields.reserve(structure_keys.size());
    for (size_t index = 0; index < structure_keys.size(); ++index) {
        restored_fields.push_back(
            make_repeater_structure_key_field(inspector, index, structure_keys[index]));
    }
    inspector.fields.insert(
        inspector.fields.begin() + static_cast<std::ptrdiff_t>(insertion_index),
        std::make_move_iterator(restored_fields.begin()),
        std::make_move_iterator(restored_fields.end()));
}

MapElementInspectorRequest make_inspector_reload_request(const MapElementInspectorState& inspector) {
    MapElementInspectorRequest request;
    request.edit_id = inspector.edit_id;
    request.row_kind = inspector.row_kind;
    request.source_file = inspector.source_file;
    request.line = inspector.line;
    request.column = inspector.column;
    for (const MapElementEditFieldState& field : inspector.fields) {
        if (field.read_only ||
            (!field.target_edit_id.empty() && field.target_edit_id != inspector.edit_id)) {
            continue;
        }
        const std::string& backend_key = field.backend_key.empty() ? field.key : field.backend_key;
        request.field_values[backend_key] = trim_gui_ascii_copy(edit_field_buffer_text(field));
    }
    request.inspector_session = inspector.session;
    return request;
}

bool App::open_element_inspector(const MapElementInspectorRequest& request) {
    if (!edit_actions_available()) return false;
    if (request.edit_id.empty() && request.field_values.empty()) return false;

    std::string edit_id = request.edit_id;
    size_t model_row_index = 0;
    const TableRow* row = find_model_row_for_inspector_request(
        model_, request, edit_id, model_row_index);
    if (!row) {
        add_log("[warn]gui_kme.cpp: edit target row not found: " + request.edit_id);
        return false;
    }
    clear_scene_placement_edit_target();

    if (!request.edit_id.empty() && request.edit_id != edit_id) {
        auto pending = pending_edit_changes_.extract(request.edit_id);
        if (!pending.empty()) {
            pending.key() = edit_id;
            pending.mapped().edit_id = edit_id;
            pending_edit_changes_.insert(std::move(pending));
        }
        auto snapshot = original_edit_rows_.extract(request.edit_id);
        if (!snapshot.empty()) {
            snapshot.key() = edit_id;
            snapshot.mapped().row.edit_id = edit_id;
            original_edit_rows_.insert(std::move(snapshot));
        }
    }

    std::string info_error;
    std::optional<InspectorTargetMetadata> target_info =
        resolve_inspector_target_metadata(handle_, edit_id, request.row_kind, &info_error);
    if (!target_info && !info_error.empty()) {
        add_log("[warn]gui_kme.cpp: edit target metadata fallback: " + info_error);
    }

    EditSourceInfo source = target_info ? target_info->source : row->source;
    if (source.file_path.empty()) source = row->source;

    MapElementInspectorState next;
    next.open = true;
    next.edit_id = edit_id;
    next.row_kind = request.row_kind;
    next.model_row_index = model_row_index;
    next.model_row_source_revision = plan_data_source_revision_;
    next.title = tr("dialog.element_properties");
    if (request.inspector_session) next.session = *request.inspector_session;
    next.source_file = source.file_path;
    next.source_file_name = display_name_from_path(next.source_file);
    if (target_info) next.expected_source_hash = target_info->expected_source_hash;
    next.line = source.line;
    next.column = source.column;
    if (target_info) next.source_distance_string = target_info->source_distance_string;
    next.raw_statement = target_info && !target_info->raw_statement.empty()
        ? target_info->raw_statement
        : source.raw_text_preview;
    if (target_info) next.statement_kind = target_info->statement_kind;
    next.source_signal_short_form = target_info &&
        (target_info->flags & KV_EDIT_TARGET_FLAG_SIGNAL_SHORT_FORM) != 0;
    next.owned_edit_ids.push_back(edit_id);
    if (next.expected_source_hash.empty()) {
        const EditSourceFileInfo* source_file = find_model_source_file(model_, next.source_file);
        if (source_file) next.expected_source_hash = source_file->source_hash;
    }

    const TableRow* original_row = row;
    auto snapshot = original_edit_rows_.find(edit_id);
    if (snapshot != original_edit_rows_.end()) {
        original_row = &snapshot->second.row;
    }

    auto add_field = [&](const std::string& key, const std::string& label,
                         const std::string& value, const std::string& original_value,
                         MapElementNumericConstraint numeric_constraint, bool required) {
        MapElementEditFieldState field;
        field.key = key;
        field.backend_key = key;
        field.target_edit_id = edit_id;
        field.expected_source_hash = next.expected_source_hash;
        field.label = label;
        field.original_value = original_value;
        if (key == "distance") field.source_distance_string = next.source_distance_string;
        field.numeric_constraint = numeric_constraint;
        field.key_source = map_element_key_source_for_field(request.row_kind, key);
        field.required = required;
        set_edit_field_buffer(field, value);
        next.fields.push_back(field);
    };
    auto add_row_field = [&](const std::string& key, const std::string& label,
                             MapElementNumericConstraint numeric_constraint, bool required) {
        add_field(key, label,
                  inspector_row_field_value(*row, request.row_kind, key),
                  inspector_row_field_value(*original_row, request.row_kind, key),
                  numeric_constraint, required);
    };
    auto add_related_field = [&](const std::string& key, const std::string& backend_key,
                                 const std::string& label, const std::string& value,
                                 const std::string& original_value,
                                 MapElementNumericConstraint numeric_constraint, bool required,
                                 const std::string& target_edit_id,
                                 const std::string& expected_source_hash,
                                 const std::string& source_distance_string) {
        add_field(key, label, value, original_value, numeric_constraint, required);
        MapElementEditFieldState& field = next.fields.back();
        field.backend_key = backend_key;
        field.target_edit_id = target_edit_id;
        field.expected_source_hash = expected_source_hash;
        field.source_distance_string = source_distance_string;
    };

    if (request.row_kind == "structure.model") {
        add_row_field("structureKey", "structureKey", MapElementNumericConstraint::None, true);
        add_row_field("filePath", "filePath", MapElementNumericConstraint::None, true);
    } else if (request.row_kind == "structure.put") {
        const std::string method = table_cell(*row, "method");
        const std::string original_method = table_cell(*original_row, "method");
        next.source_method_put0 = ascii_lower(original_method) == "put0";
        next.put0_conversion_draft = next.source_method_put0 && ascii_lower(method) != "put0";
        next.put0_prompt_requested = ascii_lower(method) == "put0";
        add_row_field("distance", "distance", MapElementNumericConstraint::Finite, true);
        add_row_field("structureKey", "structureKey", MapElementNumericConstraint::None, true);
        add_row_field("trackKey", "trackKey", MapElementNumericConstraint::None, false);
        if (ascii_lower(method) != "put0") {
            for (const char* key : {"x", "y", "z", "rx", "ry", "rz"}) {
                add_row_field(key, key, structure_edit_numeric_constraint(key), true);
            }
        }
        add_row_field("tilt", "tilt", structure_edit_numeric_constraint("tilt"), true);
        add_row_field("span", "span", structure_edit_numeric_constraint("span"), true);
    } else if (request.row_kind == "structure.between") {
        add_row_field("distance", "distance", MapElementNumericConstraint::Finite, true);
        add_row_field("structureKey", "structureKey", MapElementNumericConstraint::None, true);
        add_row_field("trackKey1", "trackKey1", MapElementNumericConstraint::None, true);
        add_row_field("trackKey2", "trackKey2", MapElementNumericConstraint::None, true);
        add_row_field("flag", "flag", MapElementNumericConstraint::Finite, true);
    } else if (request.row_kind == "station.put") {
        add_row_field("distance", "distance", MapElementNumericConstraint::Finite, true);
        add_row_field("stationKey", "stationKey", MapElementNumericConstraint::None, true);
        add_row_field("door", "door", MapElementNumericConstraint::Finite, false);
        add_row_field("margin1", "back", MapElementNumericConstraint::Finite, false);
        add_row_field("margin2", "front", MapElementNumericConstraint::Finite, false);
    } else if (request.row_kind == "irregularity.change") {
        add_row_field("distance", "distance", MapElementNumericConstraint::Finite, true);
        for (const char* key : {"x", "y", "r", "lx", "ly", "lr"}) {
            add_row_field(key, key, MapElementNumericConstraint::Finite, true);
        }
    } else if (request.row_kind == "beacon.put") {
        add_row_field("distance", "distance", MapElementNumericConstraint::Finite, true);
        for (const char* key : {"type", "section", "sendData"}) {
            add_row_field(key, key, MapElementNumericConstraint::Finite, true);
        }
    } else if (request.row_kind == "mapSound.play") {
        add_row_field("distance", "distance", MapElementNumericConstraint::Finite, true);
        add_row_field("soundKey", "soundKey", MapElementNumericConstraint::None, true);
    } else if (request.row_kind == "mapSound3D.put") {
        add_row_field("distance", "distance", MapElementNumericConstraint::Finite, true);
        add_row_field("soundKey", "soundKey", MapElementNumericConstraint::None, true);
        add_row_field("x", "x", MapElementNumericConstraint::Finite, true);
        add_row_field("y", "y", MapElementNumericConstraint::Finite, true);
    } else if (request.row_kind == "rollingNoise.change" ||
               request.row_kind == "flangeNoise.change" ||
               request.row_kind == "jointNoise.play") {
        add_row_field("distance", "distance", MapElementNumericConstraint::Finite, true);
        add_row_field("index", "index", MapElementNumericConstraint::Finite, true);
    } else if (request.row_kind == "background.change") {
        add_row_field("distance", "distance", MapElementNumericConstraint::Finite, true);
        add_row_field("structureKey", "structureKey", MapElementNumericConstraint::None, true);
    } else if (request.row_kind == "adhesion.change") {
        add_row_field("distance", "distance", MapElementNumericConstraint::Finite, true);
        add_row_field("a", "a", MapElementNumericConstraint::Finite, true);
        add_row_field("b", "b", MapElementNumericConstraint::Finite, false);
        add_row_field("c", "c", MapElementNumericConstraint::Finite, false);
    } else if (request.row_kind == "cabIlluminance.change") {
        add_row_field("distance", "distance", MapElementNumericConstraint::Finite, true);
        add_row_field("value", "value", MapElementNumericConstraint::Finite, true);
    } else if (request.row_kind == "fog.change") {
        const bool source_set = target_info &&
            ascii_lower(target_info->statement_kind) == "fog.set";
        add_row_field("distance", "distance", MapElementNumericConstraint::Finite, true);
        for (const char* key : {"density", "red", "green", "blue"}) {
            add_row_field(key, key, MapElementNumericConstraint::Finite, source_set);
        }
    } else if (request.row_kind == "drawDistance.change") {
        add_row_field("distance", "distance", MapElementNumericConstraint::Finite, true);
        add_row_field("value", "value", MapElementNumericConstraint::Finite, true);
    } else if (request.row_kind == "speedlimit") {
        add_row_field("distance", "distance", MapElementNumericConstraint::Finite, true);
        if (ascii_lower(table_cell(*row, "method")) == "begin") {
            add_row_field("speed", "speed", MapElementNumericConstraint::Finite, true);
        }
    } else if (request.row_kind == "section.begin" ||
               request.row_kind == "section.speedLimit") {
        const bool begins = request.row_kind == "section.begin";
        const std::vector<std::string> current_values =
            section_row_values(*row);
        const std::vector<std::string> original_values =
            section_row_values(*original_row);
        next.section_values_original = original_values;
        add_row_field("distance", "distance", MapElementNumericConstraint::Finite, true);
        const char* value_prefix = begins ? "signal" : "v";
        for (size_t value_index = 0; value_index < current_values.size(); ++value_index) {
            const std::string label = std::string(value_prefix) + std::to_string(value_index);
            add_row_field("values." + std::to_string(value_index), label,
                          MapElementNumericConstraint::Finite, true);
        }
    } else if (request.row_kind == "curve" || request.row_kind == "gradient") {
        const bool curve = request.row_kind == "curve";
        const std::string method = ascii_lower(table_cell(*row, "method"));
        const int argument_count = static_cast<int>(table_cell_number(*row, "argumentCount"));
        const bool transition = method == (curve ? "curve.begintransition"
                                                  : "gradient.begintransition");
        if (transition) {
            add_log("[warn]gui_kme.cpp: BeginTransition must be edited through its paired Begin/End");
            return false;
        }
        const bool transition_capable = curve
            ? ((method == "curve.begin" && argument_count == 2) ||
               method == "curve.begincircular" || method == "curve.end")
            : (method == "gradient.begin" || method == "gradient.beginconst" ||
               method == "gradient.end");
        if (transition_capable) {
            const std::string transition_edit_id = table_cell(*row, "_transitionEditId");
            const std::vector<TableRow>& related_rows = curve
                ? model_.curve_rows : model_.gradient_rows;
            size_t transition_row_index = 0;
            if (!transition_edit_id.empty() &&
                find_row_index_by_edit_id(related_rows, transition_edit_id,
                                          transition_row_index)) {
                const TableRow& transition_row = related_rows[transition_row_index];
                const TableRow* original_transition_row = &transition_row;
                const auto original_transition = original_edit_rows_.find(transition_edit_id);
                if (original_transition != original_edit_rows_.end()) {
                    original_transition_row = &original_transition->second.row;
                }
                std::string transition_info_error;
                const std::optional<InspectorTargetMetadata> transition_info =
                    resolve_inspector_target_metadata(handle_, transition_edit_id,
                                                      request.row_kind,
                                                      &transition_info_error);
                if (!transition_info && !transition_info_error.empty()) {
                    add_log("[warn]gui_kme.cpp: BeginTransition metadata fallback: " +
                            transition_info_error);
                }
                const EditSourceInfo transition_source = transition_info
                    ? transition_info->source : transition_row.source;
                std::string transition_hash = transition_info
                    ? transition_info->expected_source_hash : std::string{};
                if (transition_hash.empty()) {
                    const EditSourceFileInfo* file = find_model_source_file(
                        model_, transition_source.file_path);
                    if (file) transition_hash = file->source_hash;
                }
                next.owned_edit_ids.push_back(transition_edit_id);
                add_related_field(
                    "transitionStart", "distance", "Transition start",
                    inspector_row_field_value(transition_row, request.row_kind, "distance"),
                    inspector_row_field_value(*original_transition_row,
                                              request.row_kind, "distance"),
                    MapElementNumericConstraint::Finite, true, transition_edit_id,
                    transition_hash,
                    transition_info ? transition_info->source_distance_string : std::string{});
            } else {
                add_field("transitionStart", "Transition start",
                          tr("value.none"), tr("value.none"),
                          MapElementNumericConstraint::None, false);
                next.fields.back().read_only = true;
            }
        }
        add_row_field("distance", "Distance",
                      MapElementNumericConstraint::Finite, true);
        if (curve && argument_count > 0) {
            add_row_field("radius", "Radius",
                          MapElementNumericConstraint::Finite, true);
            if (argument_count == 2) {
                add_row_field("cant", "Cant",
                              MapElementNumericConstraint::Finite, true);
            }
        } else if (!curve && argument_count > 0) {
            add_row_field("gradient", "Gradient",
                          MapElementNumericConstraint::Finite, true);
        }
    } else if (request.row_kind == "otherTrack.change") {
        add_row_field("trackKey", "Track key",
                      MapElementNumericConstraint::None, true);
        next.fields.back().read_only = true;
        add_row_field("distance", "Distance",
                      MapElementNumericConstraint::Finite, true);

        const std::string method = ascii_lower(table_cell(*row, "method"));
        const size_t parameter_count = static_cast<size_t>(std::max(
            0.0, table_cell_number(*row, "parameterCount")));
        auto parameter_label = [&](size_t index) -> std::string {
            if (method == "track.position") {
                static const char* labels[] = {
                    "Lateral position",
                    "Vertical position",
                    "Lateral radius",
                    "Vertical radius",
                };
                if (index < std::size(labels)) return labels[index];
            } else if (method == "track.x.interpolate" ||
                       method == "track.y.interpolate") {
                return index == 0 ? "Position" : "Radius";
            } else if (method == "track.gauge" ||
                       method == "track.cant.setgauge") {
                return "Gauge";
            } else if (method == "track.cant.setcenter") {
                return "Center";
            } else if (method == "track.cant.setfunction") {
                return "Function";
            } else if (method == "track.cant" ||
                       method == "track.cant.begin" ||
                       method == "track.cant.interpolate") {
                return "Cant";
            }
            return "Parameter " + std::to_string(index + 1);
        };
        for (size_t index = 0; index < parameter_count; ++index) {
            const std::string key = "parameter" + std::to_string(index);
            add_row_field(key, parameter_label(index),
                          MapElementNumericConstraint::Finite, true);
        }
    } else if (request.row_kind == "signal.put") {
        add_row_field("distance", "distance", MapElementNumericConstraint::Finite, true);
        add_row_field("signalAspectKey", "signalAspectKey",
                      MapElementNumericConstraint::None, true);
        add_row_field("section", "section", MapElementNumericConstraint::Finite, true);
        add_row_field("trackKey", "trackKey", MapElementNumericConstraint::None, false);
        for (const char* key : {"x", "y", "z", "rx", "ry", "rz", "tilt", "span"}) {
            add_row_field(key, key, structure_edit_numeric_constraint(key), true);
            if (std::strcmp(key, "z") == 0 || std::strcmp(key, "rx") == 0 ||
                std::strcmp(key, "ry") == 0 || std::strcmp(key, "rz") == 0 ||
                std::strcmp(key, "tilt") == 0 || std::strcmp(key, "span") == 0) {
                next.fields.back().requires_signal_full_form = true;
            }
        }
    } else if (request.row_kind == "repeater") {
        const std::vector<repeater_linkage::Segment> segments =
            repeater_linkage::pair_segments(table_repeater_events(model_.repeaters));
        const auto linked = std::find_if(
            segments.begin(), segments.end(), [&](const repeater_linkage::Segment& segment) {
                return segment.begin_source_index < model_.repeaters.size() &&
                    model_.repeaters[segment.begin_source_index].edit_id == edit_id;
            });
        if (linked == segments.end()) {
            add_log("[warn]gui_kme.cpp: Repeater inspector target is not a Begin statement: " +
                    edit_id);
            return false;
        }

        const std::string method = table_cell(*row, "method");
        const std::string original_method = table_cell(*original_row, "method");
        next.source_method_put0 = ascii_lower(original_method) == "begin0";
        next.put0_conversion_draft = next.source_method_put0 && ascii_lower(method) != "begin0";
        next.put0_prompt_requested = ascii_lower(method) == "begin0";
        next.repeater_boundary_kind =
            linked->boundary_kind == repeater_linkage::BoundaryKind::ExplicitEnd ? "end" :
            linked->boundary_kind == repeater_linkage::BoundaryKind::NextBegin ? "change" : "open";
        next.repeater_scene_row_index = linked->display_index - 1;
        const auto begin_edit_id_at = [&](const std::optional<size_t>& source_index) {
            if (!source_index || *source_index >= model_.repeaters.size()) return std::string{};
            return model_.repeaters[*source_index].edit_id;
        };
        next.repeater_previous_begin_edit_id =
            begin_edit_id_at(linked->previous_begin_source_index);
        if (linked->boundary_kind == repeater_linkage::BoundaryKind::NextBegin) {
            next.repeater_next_begin_edit_id =
                begin_edit_id_at(linked->boundary_source_index);
        }
        next.repeater_has_multiple_begins = !next.repeater_previous_begin_edit_id.empty() ||
            !next.repeater_next_begin_edit_id.empty();

        add_row_field("distance", "beginDistance", MapElementNumericConstraint::Finite, true);
        add_row_field("repeaterKey", "repeaterKey", MapElementNumericConstraint::None, true);
        next.fields.back().read_only = true;
        add_row_field("trackKey", "trackKey", MapElementNumericConstraint::None, false);
        if (ascii_lower(method) != "begin0") {
            for (const char* key : {"x", "y", "z", "rx", "ry", "rz"}) {
                add_row_field(key, key, structure_edit_numeric_constraint(key), true);
            }
        }
        add_row_field("tilt", "tilt", structure_edit_numeric_constraint("tilt"), true);
        add_row_field("span", "span", structure_edit_numeric_constraint("span"), true);
        add_row_field("interval", "interval", MapElementNumericConstraint::Finite, true);

        const std::vector<std::string> current_keys =
            split_repeater_structure_keys(table_cell(*row, "structureKeys"));
        next.repeater_structure_keys_original =
            split_repeater_structure_keys(table_cell(*original_row, "structureKeys"));
        for (size_t key_index = 0; key_index < current_keys.size(); ++key_index) {
            const std::string original_key = key_index < next.repeater_structure_keys_original.size()
                ? next.repeater_structure_keys_original[key_index]
                : std::string{};
            add_row_field("structureKeys." + std::to_string(key_index),
                          "structureKey[" + std::to_string(key_index + 1) + "]",
                          MapElementNumericConstraint::None, true);
            MapElementEditFieldState& field = next.fields.back();
            field.original_value = original_key;
            set_edit_field_buffer(field, current_keys[key_index]);
        }

        if (linked->boundary_kind == repeater_linkage::BoundaryKind::ExplicitEnd &&
            linked->boundary_source_index &&
            *linked->boundary_source_index < model_.repeaters.size()) {
            const TableRow& end_row = model_.repeaters[*linked->boundary_source_index];
            const TableRow* original_end_row = &end_row;
            const auto end_snapshot = original_edit_rows_.find(end_row.edit_id);
            if (end_snapshot != original_edit_rows_.end()) {
                original_end_row = &end_snapshot->second.row;
            }
            std::string end_info_error;
            const std::optional<InspectorTargetMetadata> end_info =
                resolve_inspector_target_metadata(handle_, end_row.edit_id, "repeater",
                                                  &end_info_error);
            if (!end_info && !end_info_error.empty()) {
                add_log("[warn]gui_kme.cpp: Repeater End metadata fallback: " + end_info_error);
            }
            EditSourceInfo end_source = end_info ? end_info->source : end_row.source;
            if (end_source.file_path.empty()) end_source = end_row.source;
            next.end_source_file = end_source.file_path;
            next.end_source_file_name = display_name_from_path(next.end_source_file);
            next.end_line = end_source.line;
            next.end_column = end_source.column;
            next.end_raw_statement = end_info && !end_info->raw_statement.empty()
                ? end_info->raw_statement
                : end_source.raw_text_preview;
            next.end_source_distance_string = end_info ? end_info->source_distance_string : "";
            next.end_expected_source_hash = end_info ? end_info->expected_source_hash : "";
            if (next.end_expected_source_hash.empty()) {
                const EditSourceFileInfo* end_file =
                    find_model_source_file(model_, next.end_source_file);
                if (end_file) next.end_expected_source_hash = end_file->source_hash;
            }
            next.owned_edit_ids.push_back(end_row.edit_id);
            add_related_field("endDistance", "distance", "endDistance",
                              inspector_row_field_value(end_row, "repeater", "distance"),
                              inspector_row_field_value(*original_end_row, "repeater", "distance"),
                              MapElementNumericConstraint::Finite, true, end_row.edit_id,
                              next.end_expected_source_hash,
                              next.end_source_distance_string);
        }
    }

    for (MapElementEditFieldState& field : next.fields) {
        const std::string& field_edit_id = field.target_edit_id.empty()
            ? edit_id
            : field.target_edit_id;
        const auto pending = pending_edit_changes_.find(field_edit_id);
        if (pending == pending_edit_changes_.end()) continue;
        const std::string& backend_key = field.backend_key.empty() ? field.key : field.backend_key;
        const auto field_it = pending->second.field_changes.find(backend_key);
        if (field_it != pending->second.field_changes.end()) {
            set_edit_field_buffer(field, field_it->second);
        }
    }

    inspector_ = std::move(next);
    inspector_.expected_source_hash =
        inspector_expected_source_hash(model_, pending_edit_changes_, inspector_);
    if (inspector_.expected_source_hash.empty() && target_info) {
        inspector_.expected_source_hash = target_info->source_hash;
    }
    for (MapElementEditFieldState& field : inspector_.fields) {
        if (field.target_edit_id == inspector_.edit_id && field.expected_source_hash.empty()) {
            field.expected_source_hash = inspector_.expected_source_hash;
        }
    }

    restore_repeater_inspector_draft(inspector_);
    return true;
}

bool App::open_element_inspector(const std::string& edit_id, const std::string& row_kind) {
    return open_element_inspector(MapElementInspectorRequest{edit_id, row_kind});
}

bool App::navigate_repeater_inspector(bool toward_next) {
    if (!inspector_.open || inspector_.row_kind != "repeater") return false;
    const std::string target_edit_id = toward_next
        ? inspector_.repeater_next_begin_edit_id
        : inspector_.repeater_previous_begin_edit_id;
    if (target_edit_id.empty()) return false;

    capture_repeater_inspector_draft(inspector_);
    MapElementInspectorRequest request{target_edit_id, "repeater"};
    request.inspector_session = inspector_.session;
    if (!open_element_inspector(request)) return false;

    if (scene_preview_started_ && scene_preview_canvas_ && scene_preview_canvas_->has_scene()) {
        locate_repeater_row_in_scene_preview(inspector_.repeater_scene_row_index);
    }
    return true;
}

void App::enable_inspector_put0_conversion() {
    enable_inspector_zero_method_conversion_draft(inspector_);
}

void App::clear_scene_placement_edit_target() {
    if (scene_preview_canvas_) scene_preview_canvas_->clear_scene_placement_edit_target();
}

void App::sync_scene_placement_edit_from_inspector() {
    const bool structure_target = inspector_.row_kind == "structure.put";
    const bool signal_target = inspector_.row_kind == "signal.put";
    const bool repeater_target = inspector_.row_kind == "repeater";
    if (!scene_preview_started_ || !scene_preview_canvas_ || !inspector_.open ||
        (!structure_target && !signal_target && !repeater_target)) {
        clear_scene_placement_edit_target();
        return;
    }

    const std::vector<TableRow>& rows = structure_target ? model_.structures :
        signal_target ? model_.signals : model_.repeaters;
    size_t row_index = inspector_.model_row_index;
    const bool cache_hit = inspector_.model_row_source_revision == plan_data_source_revision_ &&
        row_index < rows.size() && rows[row_index].edit_id == inspector_.edit_id;
    if (!cache_hit && !find_row_index_by_edit_id(rows, inspector_.edit_id, row_index)) {
        clear_scene_placement_edit_target();
        return;
    }
#ifndef NDEBUG
    if (!cache_hit) ++inspector_.model_row_cache_scans;
#endif
    if (!cache_hit) {
        inspector_.model_row_index = row_index;
        inspector_.model_row_source_revision = plan_data_source_revision_;
    }
    Canvas3DPlacementEditTarget target =
        scene_edit_target_from_row(rows[row_index], repeater_target
            ? Canvas3DSceneEditKind::Repeater
            : signal_target ? Canvas3DSceneEditKind::Signal
                            : Canvas3DSceneEditKind::Structure);
    const double model_distance = target.distance;
    const std::string model_track_key = target.track_key;
    if (const MapElementEditFieldState* distance_field =
            find_inspector_field(inspector_, "distance")) {
        if (!parse_gui_edit_number(edit_field_buffer_text(*distance_field), &target.distance)) return;
    }
    if (const MapElementEditFieldState* track_field =
            find_inspector_field(inspector_, "trackKey")) {
        target.track_key = trim_gui_ascii_copy(edit_field_buffer_text(*track_field));
    }
    for (const char* key : {"x", "y", "z", "rx", "ry", "rz", "tilt", "span"}) {
        const MapElementEditFieldState* field = find_inspector_field(inspector_, key);
        if (!field) continue;
        double value = 0.0;
        if (!parse_gui_edit_number(edit_field_buffer_text(*field), &value)) return;
        if (field->numeric_constraint == MapElementNumericConstraint::Integer &&
            std::trunc(value) != value) {
            return;
        }
        if (field->numeric_constraint == MapElementNumericConstraint::Truncate3 &&
            trim_gui_ascii_copy(edit_field_buffer_text(*field)) != field->original_value) {
            value = truncate_gui_thousandths(value);
        }
        if (std::strcmp(key, "x") == 0) target.x = value;
        else if (std::strcmp(key, "y") == 0) target.y = value;
        else if (std::strcmp(key, "z") == 0) target.z = value;
        else if (std::strcmp(key, "rx") == 0) target.rx = value;
        else if (std::strcmp(key, "ry") == 0) target.ry = value;
        else if (std::strcmp(key, "rz") == 0) target.rz = value;
        else if (std::strcmp(key, "tilt") == 0) target.tilt = value;
        else if (std::strcmp(key, "span") == 0) target.span = value;
    }
    if (repeater_target &&
        (std::fabs(target.distance - model_distance) > 1e-9 ||
         target.track_key != model_track_key)) {
        clear_scene_placement_edit_target();
        return;
    }
    const bool show_gizmo = !inspector_.source_method_put0 ||
        inspector_.put0_conversion_draft;
    if (repeater_target) {
        scene_preview_canvas_->set_scene_repeater_edit_target(target, show_gizmo);
    } else {
        scene_preview_canvas_->set_scene_placement_edit_target(target, show_gizmo);
    }
}

void App::apply_scene_placement_drag_update(const Canvas3DPlacementDragUpdate& update) {
    const bool matching_kind =
        (update.kind == Canvas3DSceneEditKind::Structure && inspector_.row_kind == "structure.put") ||
        (update.kind == Canvas3DSceneEditKind::Signal && inspector_.row_kind == "signal.put") ||
        (update.kind == Canvas3DSceneEditKind::Repeater && inspector_.row_kind == "repeater");
    if (!inspector_.open || !matching_kind ||
        inspector_.edit_id != update.edit_id) {
        return;
    }
    const char* field_key = nullptr;
    double value = 0.0;
    if (update.axis == Canvas3DSceneDragAxis::X) {
        field_key = "x";
        value = update.x;
    } else if (update.axis == Canvas3DSceneDragAxis::Y) {
        field_key = "y";
        value = update.y;
    } else if (update.axis == Canvas3DSceneDragAxis::Z) {
        field_key = "z";
        value = update.z;
    }
    if (field_key) {
        if (MapElementEditFieldState* field = find_inspector_field(inspector_, field_key)) {
            if (field->requires_signal_full_form &&
                inspector_.source_signal_short_form &&
                !inspector_.signal_full_form_conversion_draft) {
                inspector_.pending_signal_full_form_field = field_key;
                inspector_.pending_signal_full_form_value =
                    format_gui_transform_number(value);
                inspector_.signal_full_form_prompt_requested = true;
                return;
            }
            set_edit_field_buffer(*field, format_gui_transform_number(value));
        }
    }
}

void App::apply_inspector_changes() {
    if (!edit_actions_available()) return;
    if (!inspector_.open || inspector_.edit_id.empty()) return;
    const bool repeater_inspector = inspector_.row_kind == "repeater";
    const std::string repeater_draft_edit_id = inspector_.edit_id;
    if (distance_resolution_workflow_.phase != DistanceResolutionPhase::None ||
        distance_resolution_workflow_.retry_requested) {
        cancel_distance_resolution_workflow();
    }
    std::map<std::string, MapElementPendingChange> replacements;
    auto change_for = [&](const MapElementEditFieldState& field)
        -> MapElementPendingChange& {
        const std::string target_edit_id = field.target_edit_id.empty()
            ? inspector_.edit_id
            : field.target_edit_id;
        auto [it, inserted] = replacements.try_emplace(target_edit_id);
        MapElementPendingChange& change = it->second;
        if (inserted) {
            change.change_id = "change-" + target_edit_id;
            change.edit_id = target_edit_id;
            change.row_kind = inspector_.row_kind;
            change.operation = "update";
            change.expected_source_hash = field.expected_source_hash;
            if (change.expected_source_hash.empty() && target_edit_id == inspector_.edit_id) {
                change.expected_source_hash =
                    inspector_expected_source_hash(model_, pending_edit_changes_, inspector_);
            }
            if (change.expected_source_hash.empty() && target_edit_id != inspector_.edit_id) {
                change.expected_source_hash = inspector_.end_expected_source_hash;
            }
        }
        return change;
    };

    std::vector<std::string> repeater_structure_keys;
    for (MapElementEditFieldState& field : inspector_.fields) {
        if (field.read_only) continue;
        std::string value = trim_gui_ascii_copy(edit_field_buffer_text(field));
        if (field.required && value.empty()) {
            set_program_status("status.edit.required_field");
            return;
        }
        const bool field_changed = value != field.original_value;
        if (!validate_and_canonicalize_edit_field(field, field_changed)) {
            set_program_status("status.edit.invalid_number");
            return;
        }
        value = trim_gui_ascii_copy(edit_field_buffer_text(field));
        if (inspector_.row_kind == "repeater" && is_repeater_structure_key_field(field)) {
            repeater_structure_keys.push_back(value);
            continue;
        }
        if ((inspector_.row_kind == "section.begin" ||
             inspector_.row_kind == "section.speedLimit") &&
            is_section_values_field(field)) {
            continue;
        }
        if (value != field.original_value) {
            MapElementPendingChange& change = change_for(field);
            const std::string& backend_key = field.backend_key.empty() ? field.key : field.backend_key;
            change.field_changes[backend_key] = value;
        }
    }

    auto inspector_field_is_blank = [&](const char* key) {
        const MapElementEditFieldState* field = find_inspector_field(inspector_, key);
        return !field || trim_gui_ascii_copy(edit_field_buffer_text(*field)).empty();
    };
    if (inspector_.row_kind == "adhesion.change" &&
        inspector_field_is_blank("b") != inspector_field_is_blank("c")) {
        set_program_status("status.edit.required_field");
        return;
    }
    if (inspector_.row_kind == "fog.change") {
        const bool density = !inspector_field_is_blank("density");
        const bool red = !inspector_field_is_blank("red");
        const bool green = !inspector_field_is_blank("green");
        const bool blue = !inspector_field_is_blank("blue");
        const bool all_colors = red && green && blue;
        const bool source_set = ascii_lower(inspector_.statement_kind) == "fog.set";
        if ((red || green || blue) != all_colors || (all_colors && !density) ||
            (source_set && (!density || !all_colors))) {
            set_program_status("status.edit.required_field");
            return;
        }
    }

    if (inspector_.row_kind == "repeater" &&
        repeater_structure_keys != inspector_.repeater_structure_keys_original) {
        if (repeater_structure_keys.empty()) {
            set_program_status("status.edit.required_field");
            return;
        }
        MapElementEditFieldState primary_field;
        primary_field.target_edit_id = inspector_.edit_id;
        primary_field.expected_source_hash = inspector_.expected_source_hash;
        MapElementPendingChange& change = change_for(primary_field);
        change.field_changes["structureKeys.count"] =
            std::to_string(repeater_structure_keys.size());
        for (size_t index = 0; index < repeater_structure_keys.size(); ++index) {
            change.field_changes["structureKeys." + std::to_string(index)] =
                repeater_structure_keys[index];
        }
    }

    if (inspector_.row_kind == "section.begin" ||
        inspector_.row_kind == "section.speedLimit") {
        std::vector<std::string> section_values;
        section_values.reserve(inspector_.fields.size());
        for (const MapElementEditFieldState& field : inspector_.fields) {
            if (!is_section_values_field(field)) continue;
            section_values.push_back(trim_gui_ascii_copy(edit_field_buffer_text(field)));
        }
        const bool count_changed =
            section_values.size() != inspector_.section_values_original.size();
        const bool any_value_changed = [&]() {
            for (const MapElementEditFieldState& field : inspector_.fields) {
                if (!is_section_values_field(field)) continue;
                if (trim_gui_ascii_copy(edit_field_buffer_text(field)) !=
                    field.original_value) {
                    return true;
                }
            }
            return false;
        }();
        if (count_changed || any_value_changed) {
            if (section_values.empty()) {
                set_program_status("status.edit.required_field");
                return;
            }
            MapElementEditFieldState primary_field;
            primary_field.target_edit_id = inspector_.edit_id;
            primary_field.expected_source_hash = inspector_.expected_source_hash;
            MapElementPendingChange& change = change_for(primary_field);
            if (count_changed) {
                change.field_changes["values.count"] =
                    std::to_string(section_values.size());
                for (size_t index = 0; index < section_values.size(); ++index) {
                    change.field_changes["values." + std::to_string(index)] =
                        section_values[index];
                }
            } else {
                size_t value_index = 0;
                for (const MapElementEditFieldState& field : inspector_.fields) {
                    if (!is_section_values_field(field)) continue;
                    const std::string value =
                        trim_gui_ascii_copy(edit_field_buffer_text(field));
                    if (value != field.original_value) {
                        change.field_changes["values." + std::to_string(value_index)] =
                            value;
                    }
                    ++value_index;
                }
            }
        }
    }

    if (inspector_.source_method_put0 && inspector_.put0_conversion_draft) {
        MapElementEditFieldState primary_field;
        primary_field.target_edit_id = inspector_.edit_id;
        primary_field.expected_source_hash = inspector_.expected_source_hash;
        MapElementPendingChange& change = change_for(primary_field);
        change.field_changes["method"] = inspector_.row_kind == "repeater" ? "Begin" : "Put";
    }
    if (inspector_.row_kind == "signal.put" &&
        inspector_.source_signal_short_form &&
        inspector_.signal_full_form_conversion_draft) {
        MapElementEditFieldState primary_field;
        primary_field.target_edit_id = inspector_.edit_id;
        primary_field.expected_source_hash = inspector_.expected_source_hash;
        MapElementPendingChange& change = change_for(primary_field);
        change.field_changes["form"] = "full";
    }

    if (inspector_.row_kind == "structure.put") {
        const MapElementEditFieldState* z_field = find_inspector_field(inspector_, "z");
        double z_value = 0.0;
        if (z_field && parse_gui_edit_number(edit_field_buffer_text(*z_field), &z_value) &&
            std::abs(truncate_gui_thousandths(z_value)) > 5.0) {
            inspector_.z_rebase_prompt_requested = true;
            return;
        }
    }

    // Editing a row that was created by an unfinished insert must merge into
    // the pending insert change: the insert edit id does not exist on disk, so
    // a plain update could never be replayed after a working-copy reset. The
    // merged change starts from the original insert so fields not shown in the
    // inspector (such as the selected method) and its resolution/placement
    // state survive, then the current implicit changes and form values win.
    for (auto& item : replacements) {
        auto existing = pending_edit_changes_.find(item.first);
        if (existing == pending_edit_changes_.end() ||
            existing->second.operation != "insert") {
            continue;
        }
        MapElementPendingChange& change = item.second;
        MapElementPendingChange merged = existing->second;
        // Variable-length Section fields are rebuilt from the current form;
        // retaining removed indices would make a reduced count invalid.
        if (inspector_.row_kind == "section.begin" ||
            inspector_.row_kind == "section.speedLimit") {
            for (auto field = merged.field_changes.begin();
                 field != merged.field_changes.end();) {
                if (is_section_values_field(field->first)) {
                    field = merged.field_changes.erase(field);
                } else {
                    ++field;
                }
            }
        }
        for (auto& field : change.field_changes) {
            merged.field_changes.insert_or_assign(field.first, std::move(field.second));
        }
        for (const MapElementEditFieldState& field : inspector_.fields) {
            if (field.read_only) continue;
            const std::string& backend_key =
                field.backend_key.empty() ? field.key : field.backend_key;
            merged.field_changes[backend_key] =
                trim_gui_ascii_copy(edit_field_buffer_text(field));
        }
        size_t section_value_count = 0;
        for (const MapElementEditFieldState& field : inspector_.fields) {
            if (is_section_values_field(field)) ++section_value_count;
        }
        if (section_value_count > 0) {
            merged.field_changes["values.count"] = std::to_string(section_value_count);
        }
        change = std::move(merged);
    }

    std::map<std::string, MapElementPendingChange> candidate = pending_edit_changes_;
    for (const std::string& owned_edit_id : inspector_.owned_edit_ids) {
        candidate.erase(owned_edit_id);
    }
    if (replacements.empty()) {
        if (apply_edit_ledger_to_preview(candidate, std::nullopt, false)) {
            if (repeater_inspector) {
                inspector_.session.repeater_drafts.erase(repeater_draft_edit_id);
            }
            set_program_status("status.edit.no_changes");
        }
        return;
    }

    for (auto& item : replacements) {
        MapElementPendingChange& change = item.second;
        auto existing_change = pending_edit_changes_.find(change.edit_id);
        if (existing_change == pending_edit_changes_.end()) continue;
        auto old_distance = existing_change->second.field_changes.find("distance");
        auto new_distance = change.field_changes.find("distance");
        if (old_distance != existing_change->second.field_changes.end() &&
            new_distance != change.field_changes.end() &&
            old_distance->second == new_distance->second) {
            change.distance_resolution_key = existing_change->second.distance_resolution_key;
            change.distance_boundary_token = existing_change->second.distance_boundary_token;
            change.distance_expression = existing_change->second.distance_expression;
            change.confirm_environment_mismatch =
                existing_change->second.confirm_environment_mismatch;
        }
    }

    for (auto& item : replacements) {
        candidate[item.first] = std::move(item.second);
    }
    std::optional<MapElementInspectorRequest> reload_request =
        make_inspector_reload_request(inspector_);
    if (repeater_inspector && reload_request->inspector_session) {
        reload_request->inspector_session->repeater_drafts.erase(repeater_draft_edit_id);
    }
    if (!apply_edit_ledger_to_preview(candidate, std::move(reload_request), false,
                                      inspector_.edit_id)) {
        if (distance_resolution_workflow_.phase == DistanceResolutionPhase::None &&
            !distance_resolution_workflow_.retry_requested) {
            set_program_status("status.edit.pending");
        }
    }
}

bool editable_list_row_has_draft(const EditableListDraftRow& row) {
    return row.deleted || row.secondary_row_deleted ||
        row.payload_edit_id != row.target_edit_id ||
        row.values != row.original_values;
}

std::string editable_list_field_name(const EditableListSpec& spec,
                                     size_t field_index) {
    if (spec.numbered_structure_key_fields) {
        return field_index == 0
            ? "signalAspectKey"
            : "structureKey" + std::to_string(field_index);
    }
    if (!spec.field_names || field_index >= spec.field_count) return {};
    return spec.field_names[field_index];
}

std::vector<size_t> editable_list_visible_row_indices(
    const std::vector<EditableListDraftRow>& rows) {
    std::vector<size_t> result;
    result.reserve(rows.size());
    for (size_t index = 0; index < rows.size(); ++index) {
        result.push_back(index);
    }
    return result;
}

bool move_editable_list_draft_row(
    std::vector<EditableListDraftRow>& rows,
    const std::vector<size_t>& visible_rows,
    int visible_row, int direction) {
    if (direction != -1 && direction != 1) return false;
    const int neighbor = visible_row + direction;
    if (visible_row < 0 || visible_row >= static_cast<int>(visible_rows.size()) ||
        neighbor < 0 || neighbor >= static_cast<int>(visible_rows.size())) {
        return false;
    }
    EditableListDraftRow& left =
        rows[visible_rows[static_cast<size_t>(visible_row)]];
    EditableListDraftRow& right =
        rows[visible_rows[static_cast<size_t>(neighbor)]];
    if (left.deleted || right.deleted ||
        left.target_source_file != right.target_source_file) {
        return false;
    }
    std::swap(left.payload_edit_id, right.payload_edit_id);
    std::swap(left.payload_source_file, right.payload_source_file);
    std::swap(left.payload_line, right.payload_line);
    std::swap(left.payload_column, right.payload_column);
    std::swap(left.payload_raw_statement, right.payload_raw_statement);
    std::swap(left.values, right.values);
    std::swap(left.resolved_path, right.resolved_path);
    std::swap(left.primary_structure_field_count,
              right.primary_structure_field_count);
    std::swap(left.secondary_structure_field_count,
              right.secondary_structure_field_count);
    std::swap(left.secondary_row_deleted,
              right.secondary_row_deleted);
    return true;
}

bool clear_editable_list_draft_cell(
    std::vector<EditableListDraftRow>& rows,
    const std::vector<size_t>& visible_rows,
    int visible_row, int column) {
    if (visible_row < 0 || visible_row >= static_cast<int>(visible_rows.size()) ||
        column < 0) {
        return false;
    }
    EditableListDraftRow& row =
        rows[visible_rows[static_cast<size_t>(visible_row)]];
    if (row.deleted ||
        static_cast<size_t>(column) >= row.values.size()) {
        return false;
    }
    row.values[static_cast<size_t>(column)].clear();
    return true;
}

bool delete_editable_list_draft_row(
    std::vector<EditableListDraftRow>& rows,
    const std::vector<size_t>& visible_rows,
    int visible_row) {
    if (visible_row < 0 || visible_row >= static_cast<int>(visible_rows.size())) return false;
    EditableListDraftRow& row =
        rows[visible_rows[static_cast<size_t>(visible_row)]];
    if (row.deleted) return false;
    row.deleted = true;
    return true;
}

void rebuild_editable_list_display_rows(
    EditableListEditState& edit,
    const EditableListSpec& spec) {
    edit.display_rows.clear();
    if (!spec.numbered_structure_key_fields) return;
    edit.display_rows.reserve(
        edit.visible_rows.size() * 2);
    for (size_t visible_row = 0;
         visible_row < edit.visible_rows.size();
         ++visible_row) {
        const size_t draft_index =
            edit.visible_rows[visible_row];
        if (draft_index >= edit.rows.size()) continue;
        const EditableListDraftRow& row =
            edit.rows[draft_index];
        const std::string sequence =
            std::to_string(visible_row + 1);
        edit.display_rows.push_back(
            EditableListDisplayRow{
                visible_row, 1,
                row.primary_structure_field_count,
                sequence, false});
        if (row.secondary_structure_field_count != 0) {
            edit.display_rows.push_back(
                EditableListDisplayRow{
                    visible_row,
                    1 + row.primary_structure_field_count,
                    row.secondary_structure_field_count,
                    sequence + "F", true});
        }
    }
}

bool build_editable_list_pending_changes(
    const EditableListSpec& spec,
    const std::vector<EditableListDraftRow>& rows,
    const std::map<std::string, MapElementPendingChange>& existing_changes,
    std::map<std::string, MapElementPendingChange>& candidate_changes,
    std::string& error_message) {
    candidate_changes = existing_changes;
    for (const EditableListDraftRow& row : rows) {
        if (!editable_list_row_has_draft(row)) continue;
        if (row.target_edit_id.empty()) {
            error_message = std::string(spec.row_kind) +
                " draft has no target edit ID";
            return false;
        }

        auto existing = candidate_changes.find(row.target_edit_id);
        if (existing != candidate_changes.end() &&
            existing->second.row_kind != spec.row_kind) {
            error_message = std::string(spec.row_kind) +
                " draft conflicts with another pending row kind: " + row.target_edit_id;
            return false;
        }

        MapElementPendingChange change;
        if (existing != candidate_changes.end()) change = existing->second;
        change.change_id = std::string(spec.change_prefix) + row.target_edit_id;
        change.edit_id = row.target_edit_id;
        change.row_kind = spec.row_kind;
        if (change.expected_source_hash.empty()) {
            change.expected_source_hash = row.target_expected_source_hash;
        }

        if (row.deleted) {
            change.operation = "delete";
            change.field_changes.clear();
            change.replacement_statement.clear();
        } else {
            if (existing != candidate_changes.end() &&
                existing->second.operation != "update") {
                error_message = std::string(spec.row_kind) +
                    " draft conflicts with a pending " +
                    existing->second.operation + ": " + row.target_edit_id;
                return false;
            }
            change.operation = "update";
            const bool moved = row.payload_edit_id != row.target_edit_id;
            if (moved) {
                if (row.payload_source_file != row.target_source_file ||
                    row.payload_raw_statement.empty()) {
                    error_message = std::string(spec.row_kind) +
                        " row move lost its same-file source template: " +
                        row.target_edit_id;
                    return false;
                }
                change.field_changes.clear();
                for (size_t field = 0; field < row.values.size(); ++field) {
                    const std::string field_name =
                        editable_list_field_name(spec, field);
                    if (field_name.empty()) {
                        error_message = std::string(spec.row_kind) +
                            " draft has no field name for column " +
                            std::to_string(field);
                        return false;
                    }
                    change.field_changes[field_name] = row.values[field];
                }
                change.replacement_statement = row.payload_raw_statement;
            } else {
                if (row.values.size() != row.original_values.size()) {
                    error_message = std::string(spec.row_kind) +
                        " draft field count changed unexpectedly";
                    return false;
                }
                for (size_t field = 0; field < row.values.size(); ++field) {
                    if (row.values[field] != row.original_values[field]) {
                        const std::string field_name =
                            editable_list_field_name(spec, field);
                        if (field_name.empty()) {
                            error_message = std::string(spec.row_kind) +
                                " draft has no field name for column " +
                                std::to_string(field);
                            return false;
                        }
                        change.field_changes[field_name] = row.values[field];
                    }
                }
            }
            if (row.secondary_row_deleted) {
                change.field_changes["deleteGlare"] = "1";
            }
        }
        candidate_changes[row.target_edit_id] = std::move(change);
    }
    error_message.clear();
    return true;
}

bool App::has_unapplied_editable_list_drafts() const {
    return has_editable_list_drafts(
               station_definition_edit_, k_station_definition_edit_spec) ||
        has_editable_list_drafts(
               structure_model_edit_, k_structure_model_edit_spec) ||
        has_editable_list_drafts(
               signal_aspect_edit_, k_signal_aspect_edit_spec) ||
        has_editable_list_drafts(
               sound_list_edit_, k_sound_list_edit_spec) ||
        has_editable_list_drafts(
               sound_3d_list_edit_, k_sound_3d_list_edit_spec);
}

bool App::has_editable_list_drafts(const EditableListEditState& edit,
                                   const EditableListSpec& spec) const {
    (void)spec;
    if (!edit.editing_edit_id.empty() && edit.editing_column >= 0 &&
        edit.edit_buffer != edit.editing_baseline) {
        return true;
    }
    return edit.rows_initialized &&
        std::any_of(edit.rows.begin(), edit.rows.end(),
                    [](const EditableListDraftRow& row) {
                        return editable_list_row_has_draft(row);
                    });
}

bool App::initialize_editable_list_draft_rows(EditableListEditState& edit,
                                              const EditableListSpec& spec) {
    if (edit.rows_initialized) return true;
    ensure_table_cache();
    const std::vector<CachedTableRow>* cached_rows = nullptr;
    if (std::string_view(spec.row_kind) == "station.list") {
        cached_rows = &table_cache_.station_definition_rows;
    } else if (std::string_view(spec.row_kind) == "structure.model") {
        cached_rows = &table_cache_.structure_model_rows;
    } else if (std::string_view(spec.row_kind) == "signal.aspect") {
        cached_rows = &table_cache_.signal_aspect_rows;
    } else if (std::string_view(spec.row_kind) == "sound.list") {
        cached_rows = &table_cache_.sound_list_rows;
    } else if (std::string_view(spec.row_kind) == "sound3D.list") {
        cached_rows = &table_cache_.sound_3d_list_rows;
    }
    if (!cached_rows) return false;

    std::vector<EditableListDraftRow> rows;
    rows.reserve(cached_rows->size());
    for (const CachedTableRow& cached : *cached_rows) {
        if (cached.edit_id.empty()) {
            add_log("[error]gui_kme.cpp: " + std::string(spec.row_kind) +
                    " draft row has no edit ID");
            return false;
        }
        std::string metadata_error;
        const std::optional<InspectorTargetMetadata> metadata =
            resolve_inspector_target_metadata(handle_, cached.edit_id, spec.row_kind,
                                              &metadata_error);
        if (!metadata) {
            add_log("[error]gui_kme.cpp: " + std::string(spec.row_kind) +
                    " draft initialization failed: " +
                    (metadata_error.empty() ? std::string("metadata unavailable")
                                            : metadata_error));
            return false;
        }
        EditableListDraftRow row;
        row.target_edit_id = cached.edit_id;
        row.target_source_file = metadata->source.file_path;
        row.target_expected_source_hash = metadata->expected_source_hash;
        row.target_line = metadata->source.line;
        row.target_column = metadata->source.column;
        row.payload_edit_id = cached.edit_id;
        row.payload_source_file = metadata->source.file_path;
        row.payload_line = metadata->source.line;
        row.payload_column = metadata->source.column;
        row.payload_raw_statement = metadata->raw_statement;
        const size_t field_count = cached.editable_field_count != 0
            ? cached.editable_field_count
            : spec.field_count;
        row.values.resize(field_count);
        for (size_t field = 0; field < field_count; ++field) {
            const size_t cached_column = spec.cache_column_offset + field;
            if (cached_column < cached.cells.size()) {
                row.values[field] = cached.cells[cached_column];
            }
        }
        row.original_values = row.values;
        row.resolved_path = cached.open_path;
        row.primary_structure_field_count =
            cached.primary_structure_field_count;
        row.secondary_structure_field_count =
            cached.secondary_structure_field_count;
        rows.push_back(std::move(row));
    }
    edit.rows = std::move(rows);
    edit.visible_rows = editable_list_visible_row_indices(edit.rows);
    rebuild_editable_list_display_rows(edit, spec);
    edit.rows_initialized = true;
    return true;
}

void App::reset_editable_list_find_results(const EditableListSpec& spec) {
    if (std::string_view(spec.row_kind) == "structure.model") {
        reset_structure_model_find_results();
    } else if (std::string_view(spec.row_kind) == "sound.list") {
        reset_sound_file_find_results(false);
    } else if (std::string_view(spec.row_kind) == "sound3D.list") {
        reset_sound_file_find_results(true);
    } else if (std::string_view(spec.row_kind) == "signal.aspect") {
        reset_signal_aspect_find_results();
    }
}

void App::commit_editable_list_active_edit(EditableListEditState& edit,
                                           const EditableListSpec& spec) {
    if (!edit.editing_edit_id.empty() && edit.editing_column >= 0 &&
        initialize_editable_list_draft_rows(edit, spec)) {
        const auto row = std::find_if(
            edit.rows.begin(), edit.rows.end(), [&](const EditableListDraftRow& candidate) {
                return candidate.target_edit_id == edit.editing_edit_id;
            });
        if (row != edit.rows.end() && !row->deleted &&
            static_cast<size_t>(edit.editing_column) <
                row->values.size()) {
            row->values[static_cast<size_t>(edit.editing_column)] = edit.edit_buffer;
            if (edit.editing_column == spec.path_field) {
                row->resolved_path = resolve_list_asset_path(
                    row->target_source_file, edit.edit_buffer);
            }
        }
    }
    edit.editing_column = -1;
    edit.editing_edit_id.clear();
    edit.editing_baseline.clear();
    edit.edit_buffer.clear();
    edit.edit_buffer_fresh = false;
}

void App::discard_all_editable_list_drafts() {
    station_definition_edit_ = EditableListEditState{};
    structure_model_edit_ = EditableListEditState{};
    signal_aspect_edit_ = EditableListEditState{};
    sound_list_edit_ = EditableListEditState{};
    sound_3d_list_edit_ = EditableListEditState{};
}

bool App::move_editable_list_row(EditableListEditState& edit,
                                 const EditableListSpec& spec,
                                 int visible_row, int direction) {
    commit_editable_list_active_edit(edit, spec);
    if (!initialize_editable_list_draft_rows(edit, spec)) return false;
    if (!move_editable_list_draft_row(
            edit.rows, edit.visible_rows, visible_row, direction)) return false;
    edit.selected_row = visible_row + direction;
    rebuild_editable_list_display_rows(edit, spec);
    return true;
}

bool App::clear_editable_list_cell(EditableListEditState& edit,
                                   const EditableListSpec& spec,
                                   int visible_row, int column) {
    commit_editable_list_active_edit(edit, spec);
    if (!initialize_editable_list_draft_rows(edit, spec)) return false;
    if (!clear_editable_list_draft_cell(
            edit.rows, edit.visible_rows, visible_row, column)) return false;
    EditableListDraftRow& row =
        edit.rows[edit.visible_rows[static_cast<size_t>(visible_row)]];
    if (column == spec.path_field) row.resolved_path.clear();
    edit.selected_row = visible_row;
    edit.selected_column = column;
    return true;
}

bool App::choose_editable_list_file(EditableListEditState& edit,
                                    const EditableListSpec& spec,
                                    int visible_row) {
    commit_editable_list_active_edit(edit, spec);
    if (spec.path_field < 0 ||
        !initialize_editable_list_draft_rows(edit, spec) ||
        visible_row < 0 ||
        visible_row >= static_cast<int>(edit.visible_rows.size())) {
        return false;
    }
    EditableListDraftRow& row =
        edit.rows[edit.visible_rows[static_cast<size_t>(visible_row)]];
    if (row.deleted || row.target_edit_id.empty()) return false;

    const std::string selected_file = open_editable_list_file_dialog(
        spec, list_asset_picker_initial_directory(
                  row.resolved_path, row.target_source_file));
    if (selected_file.empty()) return false;

    ListAssetSourcePathResult selected_path =
        make_list_asset_source_path(row.target_source_file, selected_file);
    row.values[static_cast<size_t>(spec.path_field)] = selected_path.source_path;
    row.resolved_path = selected_path.resolved_path;
    edit.selected_row = visible_row;
    edit.selected_column =
        static_cast<int>(spec.cache_column_offset) + spec.path_field;

    if (!selected_path.fallback_reason.empty()) {
        add_log(
            LogSeverity::Warning,
            "[warning]gui_kme.cpp: unable to create a relative resource path; "
            "using absolute path: reason=\"" + selected_path.fallback_reason +
            "\", selected=\"" + selected_path.resolved_path +
            "\", list=\"" + row.target_source_file + "\"");
        set_program_status("status.edit.relative_path_fallback");
    }
    return true;
}

bool App::delete_editable_list_row(EditableListEditState& edit,
                                   const EditableListSpec& spec,
                                   int visible_row) {
    commit_editable_list_active_edit(edit, spec);
    if (!initialize_editable_list_draft_rows(edit, spec)) return false;
    if (!delete_editable_list_draft_row(
            edit.rows, edit.visible_rows, visible_row)) return false;
    edit.selected_row = visible_row;
    return true;
}

bool App::delete_editable_list_secondary_row(
    EditableListEditState& edit,
    const EditableListSpec& spec,
    int visible_row) {
    commit_editable_list_active_edit(edit, spec);
    if (!initialize_editable_list_draft_rows(edit, spec) ||
        visible_row < 0 ||
        visible_row >=
            static_cast<int>(edit.visible_rows.size())) {
        return false;
    }
    EditableListDraftRow& row =
        edit.rows[
            edit.visible_rows[
                static_cast<size_t>(visible_row)]];
    if (row.deleted || row.secondary_row_deleted ||
        row.secondary_structure_field_count == 0) {
        return false;
    }
    row.secondary_row_deleted = true;
    edit.selected_row = visible_row;
    edit.selected_secondary_row = true;
    return true;
}

void App::rebind_other_editable_list_drafts(
    const EditableListEditState* applied) {
    ensure_table_cache();
    struct Binding {
        EditableListEditState* edit;
        const EditableListSpec* spec;
        const std::vector<CachedTableRow>* cached_rows;
    };
    const std::array<Binding, 5> bindings = {{
        {&station_definition_edit_, &k_station_definition_edit_spec,
         &table_cache_.station_definition_rows},
        {&structure_model_edit_, &k_structure_model_edit_spec,
         &table_cache_.structure_model_rows},
        {&signal_aspect_edit_, &k_signal_aspect_edit_spec,
         &table_cache_.signal_aspect_rows},
        {&sound_list_edit_, &k_sound_list_edit_spec,
         &table_cache_.sound_list_rows},
        {&sound_3d_list_edit_, &k_sound_3d_list_edit_spec,
         &table_cache_.sound_3d_list_rows},
    }};
    for (const Binding& binding : bindings) {
        if (binding.edit == applied || !binding.edit->rows_initialized) continue;
        using LocationKey = std::tuple<std::string_view, int, int>;
        std::map<LocationKey, const CachedTableRow*> rows_by_location;
        for (const CachedTableRow& row : *binding.cached_rows) {
            rows_by_location.emplace(
                LocationKey{row.source.file_path, row.source.line, row.source.column},
                &row);
        }
        auto find_location = [&](const std::string& file, int line, int column)
            -> const CachedTableRow* {
            const auto found = rows_by_location.find(
                LocationKey{file, line, column});
            return found == rows_by_location.end() ? nullptr : found->second;
        };
        bool rebound = true;
        for (EditableListDraftRow& row : binding.edit->rows) {
            const CachedTableRow* target = find_location(
                row.target_source_file, row.target_line, row.target_column);
            const CachedTableRow* payload = find_location(
                row.payload_source_file, row.payload_line, row.payload_column);
            if (!target || !payload || target->edit_id.empty() ||
                payload->edit_id.empty()) {
                rebound = false;
                break;
            }
            if (binding.edit->editing_edit_id == row.target_edit_id) {
                binding.edit->editing_edit_id = target->edit_id;
            }
            row.target_edit_id = target->edit_id;
            row.payload_edit_id = payload->edit_id;
        }
        if (!rebound) {
            add_log("[error]gui_kme.cpp: discarded stale " +
                    std::string(binding.spec->row_kind) +
                    " drafts after source rows could not be rebound");
            *binding.edit = EditableListEditState{};
        }
    }
}

void App::apply_editable_list_drafts(EditableListEditState& edit,
                                     const EditableListSpec& spec) {
    if (!edit_actions_available()) return;
    commit_editable_list_active_edit(edit, spec);
    if (!has_editable_list_drafts(edit, spec)) return;

    std::map<std::string, MapElementPendingChange> candidate;
    std::string error;
    if (!build_editable_list_pending_changes(
            spec, edit.rows, pending_edit_changes_, candidate, error)) {
        add_log("[error]gui_kme.cpp: " + std::string(spec.row_kind) +
                " apply blocked: " + error);
        set_program_status("status.edit.pending");
        return;
    }
    if (apply_edit_ledger_to_preview(candidate, std::nullopt, false)) {
        EditableListEditState* applied = &edit;
        edit = EditableListEditState{};
        invalidate_table_cache();
        rebind_other_editable_list_drafts(applied);
        reset_editable_list_find_results(spec);
    }
    set_program_status("status.edit.pending");
}

std::string delete_expected_source_hash(
    const MapModel& model,
    const std::map<std::string, MapElementPendingChange>& pending_changes,
    void* handle,
    const MapElementDeleteRequest& request,
    std::string* metadata_error) {
    const auto pending = pending_changes.find(request.edit_id);
    if (pending != pending_changes.end() &&
        !pending->second.expected_source_hash.empty()) {
        return pending->second.expected_source_hash;
    }

    const std::optional<InspectorTargetMetadata> metadata =
        resolve_inspector_target_metadata(handle, request.edit_id, request.row_kind,
                                          metadata_error);
    std::string source_file_path = metadata ? metadata->source.file_path : std::string{};
    const std::vector<TableRow>* rows = inspector_rows_for_kind(model, request.row_kind);
    if (source_file_path.empty() && rows) {
        size_t row_index = 0;
        if (find_row_index_by_edit_id(*rows, request.edit_id, row_index)) {
            source_file_path = (*rows)[row_index].source.file_path;
        }
    }
    return expected_source_hash_for_edit_target(
        model, pending_changes, request.edit_id,
        metadata ? metadata->expected_source_hash : std::string{}, source_file_path);
}

bool App::delete_element_target(const MapElementDeleteRequest& request) {
    if (!edit_actions_available() || request.edit_id.empty() ||
        !row_kind_supports_delete(request.row_kind)) {
        return false;
    }
    if (distance_resolution_workflow_.phase != DistanceResolutionPhase::None ||
        distance_resolution_workflow_.retry_requested) {
        cancel_distance_resolution_workflow();
    }
    std::map<std::string, MapElementPendingChange> candidate = pending_edit_changes_;
    std::set<std::string> repeater_chain_edit_ids;
    std::set<std::string> related_delete_ids;
    auto make_delete_change = [&](const std::string& edit_id, const std::string& row_kind) {
        MapElementDeleteRequest target_request = request;
        target_request.edit_id = edit_id;
        target_request.row_kind = row_kind;
        MapElementPendingChange change;
        change.change_id = "delete-" + edit_id;
        change.edit_id = edit_id;
        change.row_kind = row_kind;
        change.operation = "delete";
        std::string metadata_error;
        change.expected_source_hash = delete_expected_source_hash(
            model_, pending_edit_changes_, handle_, target_request, &metadata_error);
        if (!metadata_error.empty()) {
            add_log("[warn]gui_kme.cpp: delete target metadata fallback: " + metadata_error);
        }
        return change;
    };

    if (request.row_kind == "curve" || request.row_kind == "gradient") {
        const std::vector<TableRow>& rows = request.row_kind == "curve"
            ? model_.curve_rows : model_.gradient_rows;
        size_t selected_index = 0;
        if (!find_row_index_by_edit_id(rows, request.edit_id, selected_index)) return false;
        const TableRow* primary = &rows[selected_index];
        const std::string primary_edit_id = table_cell(*primary, "_primaryEditId");
        if (!primary_edit_id.empty()) {
            if (!find_row_index_by_edit_id(rows, primary_edit_id, selected_index)) return false;
            primary = &rows[selected_index];
        } else if (ascii_lower(table_cell(*primary, "method")).find("begintransition") !=
                   std::string::npos) {
            add_log("[warn]gui_kme.cpp: unpaired BeginTransition cannot be deleted");
            return false;
        }
        related_delete_ids.insert(primary->edit_id);
        candidate[primary->edit_id] = make_delete_change(primary->edit_id, request.row_kind);
        const std::string transition_edit_id = table_cell(*primary, "_transitionEditId");
        if (!transition_edit_id.empty()) {
            related_delete_ids.insert(transition_edit_id);
            candidate[transition_edit_id] = make_delete_change(
                transition_edit_id, request.row_kind);
        }
    } else if (request.row_kind == "repeater") {
        const std::optional<RepeaterDeleteChain> chain =
            repeater_delete_chain_for_edit_id(model_.repeaters, request.edit_id);
        if (!chain || chain->begin_source_indices.empty() ||
            chain->selected_begin_index >= chain->begin_source_indices.size()) {
            add_log("[warn]gui_kme.cpp: Repeater delete target is not a linked Begin statement: " +
                    request.edit_id);
            return false;
        }
        if (chain->begin_source_indices.size() == 1 &&
            request.repeater_mode != RepeaterDeleteMode::EntireChain) {
            add_log("[warn]gui_kme.cpp: Repeater change-point delete requires multiple Begins");
            return false;
        }
        if (chain->selected_begin_index == 0 &&
            (request.repeater_mode == RepeaterDeleteMode::TrimToChangePoint ||
             request.repeater_mode == RepeaterDeleteMode::StartFromChangePoint)) {
            add_log("[warn]gui_kme.cpp: Repeater delete mode is unavailable for the first Begin");
            return false;
        }

        std::vector<std::string> begin_edit_ids;
        begin_edit_ids.reserve(chain->begin_source_indices.size());
        for (size_t source_index : chain->begin_source_indices) {
            if (source_index >= model_.repeaters.size() ||
                model_.repeaters[source_index].edit_id.empty()) {
                add_log("[warn]gui_kme.cpp: Repeater chain is missing editable Begin metadata");
                return false;
            }
            const std::string& edit_id = model_.repeaters[source_index].edit_id;
            begin_edit_ids.push_back(edit_id);
            repeater_chain_edit_ids.insert(edit_id);
        }
        std::optional<std::string> end_edit_id;
        if (chain->end_source_index) {
            if (*chain->end_source_index >= model_.repeaters.size() ||
                model_.repeaters[*chain->end_source_index].edit_id.empty()) {
                add_log("[warn]gui_kme.cpp: Repeater chain is missing editable End metadata");
                return false;
            }
            end_edit_id = model_.repeaters[*chain->end_source_index].edit_id;
            repeater_chain_edit_ids.insert(*end_edit_id);
        }

        const auto add_delete = [&](const std::string& edit_id) {
            candidate[edit_id] = make_delete_change(edit_id, "repeater");
        };
        const size_t selected = chain->selected_begin_index;
        switch (request.repeater_mode) {
            case RepeaterDeleteMode::EntireChain:
                for (const std::string& edit_id : begin_edit_ids) add_delete(edit_id);
                if (end_edit_id) add_delete(*end_edit_id);
                break;
            case RepeaterDeleteMode::ChangePoint:
                add_delete(begin_edit_ids[selected]);
                break;
            case RepeaterDeleteMode::TrimToChangePoint: {
                MapElementDeleteRequest target_request = request;
                target_request.edit_id = begin_edit_ids[selected];
                target_request.row_kind = "repeater";
                MapElementPendingChange change;
                change.change_id = "repeater-trim-" + target_request.edit_id;
                change.edit_id = target_request.edit_id;
                change.row_kind = "repeater";
                change.operation = "update";
                change.field_changes.emplace("method", "End");
                std::string metadata_error;
                change.expected_source_hash = delete_expected_source_hash(
                    model_, pending_edit_changes_, handle_, target_request, &metadata_error);
                if (!metadata_error.empty()) {
                    add_log("[warn]gui_kme.cpp: Repeater trim metadata fallback: " + metadata_error);
                }
                candidate[change.edit_id] = std::move(change);
                for (size_t index = selected + 1; index < begin_edit_ids.size(); ++index) {
                    add_delete(begin_edit_ids[index]);
                }
                if (end_edit_id) add_delete(*end_edit_id);
                break;
            }
            case RepeaterDeleteMode::StartFromChangePoint:
                for (size_t index = 0; index < selected; ++index) add_delete(begin_edit_ids[index]);
                break;
        }
    } else {
        MapElementPendingChange change = make_delete_change(request.edit_id, request.row_kind);
        candidate[change.edit_id] = std::move(change);
    }

    if (apply_edit_ledger_to_preview(candidate, std::nullopt, true)) {
        const bool close_repeater_inspector = inspector_.open && inspector_.row_kind == "repeater" &&
            repeater_chain_edit_ids.find(inspector_.edit_id) != repeater_chain_edit_ids.end();
        if (close_repeater_inspector ||
            (inspector_.open && related_delete_ids.find(inspector_.edit_id) !=
                                  related_delete_ids.end()) ||
            (inspector_.open && inspector_.edit_id == request.edit_id)) {
            clear_scene_placement_edit_target();
            inspector_ = MapElementInspectorState{};
            pending_inspector_request_.reset();
        }
        set_program_status("status.edit.pending_delete");
        return true;
    }
    return false;
}

KvUtf8View edit_utf8_view(const std::string& text) {
    return {text.empty() ? nullptr : text.data(), static_cast<std::uint64_t>(text.size())};
}

struct TypedEditBatchStorage {
    std::vector<KvEditChange> changes;
    std::vector<KvEditField> fields;

    KvEditBatch view() const {
        return {changes.empty() ? nullptr : changes.data(),
                static_cast<std::uint64_t>(changes.size()),
                fields.empty() ? nullptr : fields.data(),
                static_cast<std::uint64_t>(fields.size())};
    }
};

TypedEditBatchStorage typed_edit_batch(
    const std::map<std::string, MapElementPendingChange>& inputs) {
    // KvUtf8View is non-owning. Keep the reserved field name in static
    // storage instead of creating a temporary std::string from a literal.
    static constexpr KvUtf8View k_insert_row_kind_field_name{
        "rowKind", sizeof("rowKind") - 1};
    TypedEditBatchStorage storage;
    storage.changes.reserve(inputs.size());
    size_t field_count = 0;
    for (const auto& input : inputs) {
        field_count += input.second.field_changes.size();
        if (input.second.operation == "insert") ++field_count;
    }
    storage.fields.reserve(field_count);
    for (const auto& input : inputs) {
        const MapElementPendingChange& source = input.second;
        KvEditChange change{};
        change.change_id = edit_utf8_view(source.change_id);
        change.edit_id = edit_utf8_view(source.edit_id);
        if (source.operation == "update") change.operation = KV_EDIT_UPDATE;
        else if (source.operation == "insert") change.operation = KV_EDIT_INSERT;
        else if (source.operation == "delete") change.operation = KV_EDIT_DELETE;
        else throw std::runtime_error("unsupported GUI edit operation: " + source.operation);
        if (source.confirm_environment_mismatch) {
            change.flags |= KV_EDIT_CHANGE_CONFIRM_ENVIRONMENT_MISMATCH;
        }
        change.fields.offset = static_cast<std::uint64_t>(storage.fields.size());
        change.fields.count = static_cast<std::uint64_t>(source.field_changes.size());
        if (source.operation == "insert") ++change.fields.count;
        if (source.operation == "insert") {
            // The maploader derives insert row kinds from this reserved field
            // and pops it before any typed edit processing runs.
            storage.fields.push_back({k_insert_row_kind_field_name,
                                      edit_utf8_view(source.row_kind)});
        }
        for (const auto& field : source.field_changes) {
            storage.fields.push_back({edit_utf8_view(field.first), edit_utf8_view(field.second)});
        }
        change.replacement_statement = edit_utf8_view(source.replacement_statement);
        change.target_file_path = edit_utf8_view(source.target_file_path);
        change.expected_source_hash = edit_utf8_view(source.expected_source_hash);
        change.distance_resolution_key = edit_utf8_view(source.distance_resolution_key);
        change.distance_boundary_token = edit_utf8_view(source.distance_boundary_token);
        change.distance_expression = edit_utf8_view(source.distance_expression);
        storage.changes.push_back(change);
    }
    return storage;
}

std::string edit_report_string(const KvEditReportSnapshot& report, KvStringRef ref) {
    return typed_snapshot_string(report, ref);
}

bool edit_report_span_valid(KvSpan span, std::uint64_t size) {
    return typed_snapshot_span_valid(span, size);
}

DistanceResolutionRequest distance_resolution_request_from_typed(
    const KvEditReportSnapshot& report, const KvDistanceResolutionRow& input) {
    DistanceResolutionRequest request;
    request.resolution_key = edit_report_string(report, input.resolution_key);
    request.reason = edit_report_string(report, input.reason);
    request.source_file = edit_report_string(report, input.source_file);
    request.target_distance = format_double(input.target_distance, 6);
    request.variable_name = edit_report_string(report, input.variable_name);
    request.suggested_expression = edit_report_string(report, input.suggested_expression);
    request.insertion_preview = edit_report_string(report, input.insertion_preview);
    request.can_confirm_reuse = input.can_confirm_reuse != 0;
    request.source_section_direction = edit_report_string(
        report, input.source_section_direction);
    auto append_strings = [&](KvSpan span, std::vector<std::string>& output) {
        if (!edit_report_span_valid(span, report.string_ref_count) ||
            (span.count != 0 && !report.string_refs)) return;
        output.reserve(static_cast<size_t>(span.count));
        for (std::uint64_t i = 0; i < span.count; ++i) {
            output.push_back(edit_report_string(report, report.string_refs[span.offset + i]));
        }
    };
    append_strings(input.include_stack, request.include_stack);
    append_strings(input.affected_edit_ids, request.affected_edit_ids);
    if (edit_report_span_valid(input.allowed_boundaries, report.boundary_count) &&
        (input.allowed_boundaries.count == 0 || report.boundaries)) {
        request.allowed_boundaries.reserve(static_cast<size_t>(input.allowed_boundaries.count));
        for (std::uint64_t i = 0; i < input.allowed_boundaries.count; ++i) {
            const KvDistanceBoundaryRow& source =
                report.boundaries[input.allowed_boundaries.offset + i];
            request.allowed_boundaries.push_back({edit_report_string(report, source.token),
                                                   source.line, source.column,
                                                   source.recommended != 0});
        }
    }
    return request;
}

struct CommittedEditFileState {
    std::string file_path;
    std::string source_hash;
    size_t byte_length = 0;
};

struct CommittedEditRowState {
    std::string row_kind;
    size_t row_index = 0;
    std::string edit_id;
    EditSourceInfo source;
};

bool apply_committed_edit_state(MapModel& model, const KvEditReportSnapshot& report,
                                std::string& error) {
    if (report.committed_file_count == 0) return true;
    if (!report.committed_files) {
        error = "edit commit report has a null committed-file array";
        return false;
    }

    std::vector<CommittedEditFileState> file_states;
    file_states.reserve(static_cast<size_t>(report.committed_file_count));
    for (std::uint64_t i = 0; i < report.committed_file_count; ++i) {
        const KvEditCommittedFileRow& item = report.committed_files[i];
        CommittedEditFileState state;
        state.file_path = edit_report_string(report, item.file_path);
        state.source_hash = edit_report_string(report, item.source_hash);
        state.byte_length = static_cast<size_t>(item.byte_length);
        file_states.push_back(std::move(state));
    }

    if (report.committed_row_count != 0 && !report.committed_rows) {
        error = "edit commit report is missing committed row metadata";
        return false;
    }
    std::map<std::string, std::vector<CommittedEditRowState>> rows_by_kind;
    for (std::uint64_t i = 0; i < report.committed_row_count; ++i) {
        const KvEditCommittedRow& item = report.committed_rows[i];
        CommittedEditRowState state;
        state.row_kind = edit_report_string(report, item.row_kind);
        if (item.row_index > static_cast<std::uint64_t>(std::numeric_limits<size_t>::max())) {
            error = "edit commit report contains an invalid row index";
            return false;
        }
        state.row_index = static_cast<size_t>(item.row_index);
        state.edit_id = edit_report_string(report, item.edit_id);
        state.source.file_path = edit_report_string(report, item.file_path);
        state.source.line = item.line;
        state.source.column = item.column;
        state.source.raw_text_preview = edit_report_string(report, item.raw_text_preview);
        if (inspector_rows_for_kind(model, state.row_kind)) {
            rows_by_kind[state.row_kind].push_back(std::move(state));
        }
    }

    static constexpr std::array<const char*, 27> k_committed_row_kinds = {
        "curve", "gradient", "structure.model", "structure.put", "structure.between", "station.put",
        "station.list", "sound.list", "sound3D.list", "repeater", "signal.put",
        "signal.aspect", "irregularity.change",
        "beacon.put", "mapSound.play", "mapSound3D.put",
        "rollingNoise.change", "flangeNoise.change", "jointNoise.play",
        "background.change", "adhesion.change", "cabIlluminance.change",
        "fog.change", "drawDistance.change", "speedlimit",
        "section.begin", "section.speedLimit",
    };
    std::map<std::string, std::map<std::string, const CommittedEditRowState*>>
        states_by_edit_id;
    for (const char* row_kind : k_committed_row_kinds) {
        std::vector<TableRow>* target_rows = inspector_rows_for_kind(model, row_kind);
        const std::vector<CommittedEditRowState>& states = rows_by_kind[row_kind];
        if (!target_rows || states.size() != target_rows->size()) {
            error = std::string("edit commit row count mismatch for ") + row_kind;
            return false;
        }
        std::vector<bool> seen(target_rows->size(), false);
        for (const CommittedEditRowState& state : states) {
            if (state.row_index >= target_rows->size() || seen[state.row_index]) {
                error = std::string("edit commit row mapping is invalid for ") + row_kind;
                return false;
            }
            seen[state.row_index] = true;
            if (!state.edit_id.empty()) {
                auto inserted = states_by_edit_id[row_kind].emplace(state.edit_id, &state);
                if (!inserted.second) {
                    error = std::string("edit commit contains a duplicate editId for ") +
                        row_kind + ": " + state.edit_id;
                    return false;
                }
            }
        }
        for (const TableRow& row : *target_rows) {
            if (row.edit_id.empty()) continue;
            if (states_by_edit_id[row_kind].find(row.edit_id) ==
                states_by_edit_id[row_kind].end()) {
                error = std::string("edit commit lost stable row identity for ") +
                    row_kind + ": " + row.edit_id;
                return false;
            }
        }
    }

    for (const CommittedEditFileState& state : file_states) {
        auto file = std::find_if(model.edit_files.begin(), model.edit_files.end(),
                                 [&](const EditSourceFileInfo& candidate) {
                                     return candidate.file_path == state.file_path;
                                 });
        if (file == model.edit_files.end()) continue;
        file->source_hash = state.source_hash;
        file->byte_length = state.byte_length;
    }
    for (const char* row_kind : k_committed_row_kinds) {
        std::vector<TableRow>* target_rows = inspector_rows_for_kind(model, row_kind);
        for (size_t row_index = 0; row_index < target_rows->size(); ++row_index) {
            TableRow& row = (*target_rows)[row_index];
            const CommittedEditRowState* state = nullptr;
            if (!row.edit_id.empty()) {
                state = states_by_edit_id[row_kind].at(row.edit_id);
            } else {
                const std::vector<CommittedEditRowState>& states = rows_by_kind[row_kind];
                auto fallback = std::find_if(
                    states.begin(), states.end(), [&](const CommittedEditRowState& candidate) {
                        return candidate.row_index == row_index && candidate.edit_id.empty();
                    });
                if (fallback != states.end()) state = &*fallback;
            }
            if (!state) {
                error = std::string("edit commit could not bind row metadata for ") + row_kind;
                return false;
            }
            row.source = state->source;
        }
    }
    return true;
}

bool apply_committed_edit_report_to_model(MapModel& model,
                                          const KvEditReportSnapshot& report,
                                          std::string& error_message) {
    error_message.clear();
    if (report.version != KV_EDIT_REPORT_SNAPSHOT_VERSION ||
        report.structure_size < sizeof(KvEditReportSnapshot)) {
        error_message = "edit commit report version or size mismatch";
        return false;
    }
    if (!report.ok) {
        error_message = "edit commit report is not successful";
        return false;
    }
    return apply_committed_edit_state(model, report, error_message);
}

bool App::parse_and_log_edit_report(const KvEditReportSnapshot& report,
                                    const std::string& success_prefix,
                                    int* update_count,
                                    int* delete_count,
                                    int* changed_file_count,
                                    std::vector<DistanceResolutionRequest>* resolution_requests) {
    if (resolution_requests) resolution_requests->clear();
    if (report.version != KV_EDIT_REPORT_SNAPSHOT_VERSION ||
        report.structure_size < sizeof(KvEditReportSnapshot)) {
        add_log("[error]gui_kme.cpp: edit report version or size mismatch");
        return false;
    }
    if (resolution_requests && report.resolution_request_count != 0 &&
        report.resolution_requests) {
        resolution_requests->reserve(static_cast<size_t>(report.resolution_request_count));
        for (std::uint64_t i = 0; i < report.resolution_request_count; ++i) {
            DistanceResolutionRequest request = distance_resolution_request_from_typed(
                report, report.resolution_requests[i]);
            if (!request.resolution_key.empty()) resolution_requests->push_back(std::move(request));
        }
    }
    if (report.warnings) {
        for (std::uint64_t i = 0; i < report.warning_count; ++i) {
            add_log("[warn]gui_kme.cpp: " + edit_report_string(report, report.warnings[i]));
        }
    }
    if (report.blocking_errors) {
        for (std::uint64_t i = 0; i < report.blocking_error_count; ++i) {
            add_log("[error]gui_kme.cpp: " +
                    edit_report_string(report, report.blocking_errors[i]));
        }
    }
    const int updates = report.update_count;
    const int deletes = report.delete_count;
    const int files = static_cast<int>(std::min<std::uint64_t>(
        report.changed_file_count, static_cast<std::uint64_t>(std::numeric_limits<int>::max())));
    if (update_count) *update_count = updates;
    if (delete_count) *delete_count = deletes;
    if (changed_file_count) *changed_file_count = files;
    bool ok = report.ok != 0;
    if (ok) {
        std::string committed_state_error;
        if (!apply_committed_edit_state(model_, report, committed_state_error)) {
            add_log("[error]gui_kme.cpp: saved edit metadata refresh failed; Reload is required: " +
                    committed_state_error);
            clear_pending_edit_state();
            edit_registry_loaded_ = false;
            ok = false;
        }
    }
    if (ok && !success_prefix.empty()) {
        add_log(success_prefix + ": updates=" + std::to_string(updates) +
                ", deletes=" + std::to_string(deletes) +
                ", files=" + std::to_string(files));
    }
    return ok;
}

bool App::sync_edit_memory_with_ledger(
    const std::map<std::string, MapElementPendingChange>& changes,
    std::vector<DistanceResolutionRequest>* resolution_requests) {
    if (resolution_requests) resolution_requests->clear();
    if (!edit_actions_available()) return false;
    if (!handle_ || load_state_.running) return false;

    if (!kv_edit_reset_memory(handle_)) {
        edit_memory_matches_pending_ledger_ = false;
        const char* err = kv_get_last_error();
        add_log(std::string("[error]gui_kme.cpp: edit memory reset failed: ") +
                (err ? err : "unknown error"));
        return false;
    }
    edit_memory_matches_pending_ledger_ = changes.empty();
    if (changes.empty()) {
        add_log("[info]gui_kme.cpp: edit memory reset to disk baseline");
        return true;
    }

    TypedEditBatchStorage batch_storage = typed_edit_batch(changes);
    const KvEditBatch batch = batch_storage.view();
    KvEditReportSnapshot report{};
    if (!kv_edit_apply_to_memory_typed(handle_, &batch, &report, sizeof(report))) {
        edit_memory_matches_pending_ledger_ = false;
        const char* err = kv_get_last_error();
        add_log(std::string("[error]gui_kme.cpp: edit memory apply failed: ") +
                (err ? err : "unknown error"));
        return false;
    }

    if (!parse_and_log_edit_report(report, "[info]gui_kme.cpp: edit memory updated",
                                   nullptr, nullptr, nullptr, resolution_requests)) {
        edit_memory_matches_pending_ledger_ = false;
        return false;
    }
    edit_memory_matches_pending_ledger_ = true;
    return true;
}

bool App::apply_edit_ledger_to_preview(const std::map<std::string, MapElementPendingChange>& changes,
                                       std::optional<MapElementInspectorRequest> reload_request,
                                       bool applying_delete,
                                       std::string resolution_origin_edit_id) {
    if (!edit_actions_available()) return false;
    const bool ended_batch = !pending_edit_changes_.empty() && changes.empty();
    const bool reapplies_inspector_change = reload_request.has_value();

    // The backend working copy and the locally patched table rows form one
    // preview transaction. Back up only the row kinds touched by either ledger
    // so a local lookup failure cannot leave a validated backend candidate that
    // Save would commit while the GUI still shows the previous ledger.
    std::set<std::string> affected_row_kinds;
    for (const auto& entry : pending_edit_changes_) {
        if (!entry.second.row_kind.empty()) affected_row_kinds.insert(entry.second.row_kind);
    }
    for (const auto& entry : changes) {
        if (!entry.second.row_kind.empty()) affected_row_kinds.insert(entry.second.row_kind);
    }
    std::map<std::string, std::vector<TableRow>> row_backups;
    for (const std::string& row_kind : affected_row_kinds) {
        if (std::vector<TableRow>* rows = inspector_rows_for_kind(model_, row_kind)) {
            row_backups.emplace(row_kind, *rows);
        }
    }
    for (const auto& entry : changes) {
        if (entry.second.row_kind == "curve" || entry.second.row_kind == "gradient" ||
            entry.second.row_kind == "otherTrack.change") {
            snapshot_local_preview_row(entry.first, entry.second.row_kind);
        }
    }
    const auto snapshot_backup = original_edit_rows_;

    std::vector<DistanceResolutionRequest> resolution_requests;
    if (!sync_edit_memory_with_ledger(changes, &resolution_requests)) {
        if (!pending_edit_changes_.empty()) {
            sync_edit_memory_with_ledger(pending_edit_changes_);
        }
        if (!resolution_requests.empty()) {
            begin_distance_resolution_workflow(
                changes, std::move(reload_request), applying_delete,
                std::move(resolution_origin_edit_id), resolution_requests);
        }
        return false;
    }

    auto rollback_local_preview = [&]() {
        for (auto& backup : row_backups) {
            if (std::vector<TableRow>* rows =
                    inspector_rows_for_kind(model_, backup.first)) {
                *rows = std::move(backup.second);
            }
        }
        original_edit_rows_ = snapshot_backup;
        for (const std::string& row_kind : affected_row_kinds) {
            refresh_local_preview_after_edit(row_kind);
        }

        edit_memory_matches_pending_ledger_ = false;
        if (!sync_edit_memory_with_ledger(pending_edit_changes_)) {
            add_log("[error]gui_kme.cpp: failed to restore maploader working copy after "
                    "a local preview error; Save is blocked");
        }
        refresh_text_preview_from_working_copy();
        return false;
    };

    const bool signal_aspects_hydrated =
        affected_row_kinds.find("signal.aspect") !=
        affected_row_kinds.end();
    const bool alignment_hydrated =
        affected_row_kinds.find("curve") != affected_row_kinds.end() ||
        affected_row_kinds.find("gradient") != affected_row_kinds.end() ||
        affected_row_kinds.find("otherTrack.change") != affected_row_kinds.end();
    const bool full_insert_hydration = std::any_of(
        changes.begin(), changes.end(),
        [](const auto& entry) { return entry.second.operation == "insert"; });
    if (signal_aspects_hydrated || alignment_hydrated) {
        KvMapSnapshot snapshot{};
        if (!kv_get_map_snapshot(
                handle_, KV_MAP_SNAPSHOT_VERSION,
                &snapshot, sizeof(snapshot)) ||
            snapshot.version != KV_MAP_SNAPSHOT_VERSION ||
            snapshot.structure_size < sizeof(KvMapSnapshot) ||
            (signal_aspects_hydrated && snapshot.signal_aspect_count != 0 &&
             !snapshot.signal_aspects) ||
            (alignment_hydrated &&
             ((snapshot.curve_count != 0 && !snapshot.curves) ||
              (snapshot.gradient_count != 0 && !snapshot.gradients) ||
              (snapshot.other_track_change_count != 0 &&
               !snapshot.other_track_changes)))) {
            const char* error = kv_get_last_error();
            add_log(
                "[error]gui_kme.cpp: failed to refresh typed rows from the "
                "validated working copy" +
                std::string(error && *error
                    ? ": " + std::string(error)
                    : std::string{}));
            return rollback_local_preview();
        }
        if (signal_aspects_hydrated) {
            model_.signal_aspects = hydrate_signal_aspect_rows(snapshot);
        }
        if (alignment_hydrated) {
            std::map<std::string, std::pair<bool, ImVec4>> other_track_state;
            for (const OtherTrack& track : model_.other_tracks) {
                other_track_state[track.key] = {track.visible, track.color};
            }
            MapModel refreshed = hydrate_map_snapshot(snapshot, model_.path, 0.0);
            for (OtherTrack& track : refreshed.other_tracks) {
                auto state = other_track_state.find(track.key);
                if (state != other_track_state.end()) {
                    track.visible = state->second.first;
                    track.color = state->second.second;
                }
            }
            model_.own = std::move(refreshed.own);
            model_.curve = std::move(refreshed.curve);
            model_.other_tracks = std::move(refreshed.other_tracks);
            model_.controlpoints = std::move(refreshed.controlpoints);
            model_.own_events = std::move(refreshed.own_events);
            model_.curve_rows = std::move(refreshed.curve_rows);
            model_.gradient_rows = std::move(refreshed.gradient_rows);
            model_.other_track_changes = std::move(refreshed.other_track_changes);
            model_.distance_origin = refreshed.distance_origin;
            model_.height_origin = refreshed.height_origin;
            model_.origin_angle = refreshed.origin_angle;
            model_.default_min = refreshed.default_min;
            model_.default_max = refreshed.default_max;
            for (size_t i = 0; i < 3; ++i) model_.cp_arb[i] = refreshed.cp_arb[i];
            normalize_station_preview_rows(model_);
        }
        for (auto original = original_edit_rows_.begin();
             original != original_edit_rows_.end();) {
            if (signal_aspects_hydrated &&
                original->second.row_kind == "signal.aspect") {
                original = original_edit_rows_.erase(original);
            } else {
                ++original;
            }
        }
    }

    if (full_insert_hydration) {
        // An insert creates rows with brand-new edit ids, so the local
        // row-patching loops below cannot target them. Re-hydrate the whole
        // model from the validated working-copy snapshot; every pending
        // change of the batch is already reflected in it.
        KvMapSnapshot snapshot{};
        if (!kv_get_map_snapshot(
                handle_, KV_MAP_SNAPSHOT_VERSION,
                &snapshot, sizeof(snapshot)) ||
            snapshot.version != KV_MAP_SNAPSHOT_VERSION ||
            snapshot.structure_size < sizeof(KvMapSnapshot)) {
            const char* error = kv_get_last_error();
            add_log(
                "[error]gui_kme.cpp: failed to refresh the model after element "
                "insertion" +
                std::string(error && *error
                    ? ": " + std::string(error)
                    : std::string{}));
            return rollback_local_preview();
        }
        std::map<std::string, std::pair<bool, ImVec4>> other_track_state;
        for (const OtherTrack& track : model_.other_tracks) {
            other_track_state[track.key] = {track.visible, track.color};
        }
        MapModel refreshed = hydrate_map_snapshot(snapshot, model_.path, 0.0);
        for (OtherTrack& track : refreshed.other_tracks) {
            auto state = other_track_state.find(track.key);
            if (state != other_track_state.end()) {
                track.visible = state->second.first;
                track.color = state->second.second;
            }
        }
        model_ = std::move(refreshed);
        normalize_station_preview_rows(model_);
        original_edit_rows_.clear();
    }

    std::map<std::string, std::vector<std::string>> refresh_targets;
    auto note_refresh_target = [&](const std::string& row_kind, const std::string& edit_id,
                                   bool force_full_refresh) {
        std::vector<std::string>& targets = refresh_targets[row_kind];
        if (force_full_refresh) {
            targets.assign(1, std::string{});
            return;
        }
        if (!targets.empty() && targets.front().empty()) return;
        targets.push_back(edit_id);
    };
    if (signal_aspects_hydrated) {
        note_refresh_target("signal.aspect", std::string{}, true);
    }
    if (alignment_hydrated) {
        note_refresh_target("curve", std::string{}, true);
        note_refresh_target("gradient", std::string{}, true);
        note_refresh_target("otherTrack.change", std::string{}, true);
    }
    if (full_insert_hydration) {
        for (const std::string& row_kind : affected_row_kinds) {
            note_refresh_target(row_kind, std::string{}, true);
        }
    }

    for (const auto& kv : pending_edit_changes_) {
        if (changes.find(kv.first) != changes.end()) continue;
        if ((signal_aspects_hydrated && kv.second.row_kind == "signal.aspect") ||
            (alignment_hydrated &&
             (kv.second.row_kind == "curve" || kv.second.row_kind == "gradient" ||
              kv.second.row_kind == "otherTrack.change")) ||
            full_insert_hydration) {
            continue;
        }
        if (!restore_local_preview_change(kv.first, kv.second.row_kind, false)) {
            add_log("[error]gui_kme.cpp: failed to restore local edit preview: " + kv.first);
            return rollback_local_preview();
        }
        const bool force_full_refresh = kv.second.operation == "delete" ||
            (kv.second.row_kind == "structure.put" &&
             kv.second.field_changes.find("structureKey") != kv.second.field_changes.end()) ||
            (kv.second.row_kind == "signal.put" &&
             kv.second.field_changes.find("signalAspectKey") !=
                 kv.second.field_changes.end());
        note_refresh_target(kv.second.row_kind, kv.first, force_full_refresh);
    }

    for (const auto& kv : changes) {
        if ((signal_aspects_hydrated && kv.second.row_kind == "signal.aspect") ||
            (alignment_hydrated &&
             (kv.second.row_kind == "curve" || kv.second.row_kind == "gradient" ||
              kv.second.row_kind == "otherTrack.change")) ||
            full_insert_hydration) {
            continue;
        }
        if (!apply_local_preview_change(kv.second, false)) {
            add_log("[error]gui_kme.cpp: failed to apply local edit preview: " + kv.first);
            return rollback_local_preview();
        }
        const bool force_full_refresh = kv.second.operation == "delete" ||
            (kv.second.row_kind == "structure.put" &&
             kv.second.field_changes.find("structureKey") != kv.second.field_changes.end()) ||
            (kv.second.row_kind == "signal.put" &&
             kv.second.field_changes.find("signalAspectKey") !=
                 kv.second.field_changes.end());
        note_refresh_target(kv.second.row_kind, kv.first, force_full_refresh);
    }
    if (alignment_hydrated) {
        for (auto original = original_edit_rows_.begin();
             original != original_edit_rows_.end();) {
            const bool alignment_row = original->second.row_kind == "curve" ||
                original->second.row_kind == "gradient" ||
                original->second.row_kind == "otherTrack.change";
            if (alignment_row && changes.find(original->first) == changes.end()) {
                original = original_edit_rows_.erase(original);
            } else {
                ++original;
            }
        }
    }
    pending_edit_changes_ = changes;
    for (auto& entry : refresh_targets) {
        std::vector<std::string>& targets = entry.second;
        std::sort(targets.begin(), targets.end());
        targets.erase(std::unique(targets.begin(), targets.end()), targets.end());
        const std::string edit_id = targets.size() == 1 ? targets.front() : std::string{};
        refresh_local_preview_after_edit(entry.first, edit_id);
    }
    if (ended_batch) {
        distance_resolution_choices_.clear();
    }
    if (reload_request) {
        pending_inspector_request_ = std::move(reload_request);
    } else if (applying_delete && inspector_.open) {
        set_program_status("status.edit.pending_delete");
    } else if (!pending_edit_changes_.empty() && inspector_.open) {
        set_program_status("status.edit.applied_to_preview");
    }
    if (reapplies_inspector_change) set_program_status("status.edit.applied_to_preview");
    refresh_text_preview_from_working_copy();
    return true;
}

void App::begin_distance_resolution_workflow(
    const std::map<std::string, MapElementPendingChange>& changes,
    std::optional<MapElementInspectorRequest> reload_request,
    bool applying_delete,
    std::string origin_edit_id,
    const std::vector<DistanceResolutionRequest>& requests) {
    if (requests.empty()) return;

    DistanceResolutionWorkflowState workflow;
    workflow.request = requests.front();
    workflow.candidate_changes = changes;
    workflow.reload_request = std::move(reload_request);
    workflow.applying_delete = applying_delete;
    workflow.origin_edit_id = std::move(origin_edit_id);
    if (workflow.origin_edit_id.empty()) {
        for (const std::string& edit_id : workflow.request.affected_edit_ids) {
            if (workflow.candidate_changes.find(edit_id) != workflow.candidate_changes.end()) {
                workflow.origin_edit_id = edit_id;
                break;
            }
        }
    }
    if (workflow.origin_edit_id.empty()) {
        for (const auto& item : workflow.candidate_changes) {
            if (item.second.field_changes.find("distance") != item.second.field_changes.end() &&
                item.second.distance_resolution_key.empty()) {
                workflow.origin_edit_id = item.first;
                break;
            }
        }
    }

    const std::string expression = workflow.request.suggested_expression;
    const size_t expression_size = std::min(
        expression.size(), workflow.expression_buffer.size() - 1);
    if (expression_size > 0) {
        std::memcpy(workflow.expression_buffer.data(), expression.data(), expression_size);
    }
    workflow.expression_buffer[expression_size] = '\0';
    distance_resolution_workflow_ = std::move(workflow);

    auto cached = distance_resolution_choices_.find(
        distance_resolution_workflow_.request.resolution_key);
    if (cached != distance_resolution_choices_.end()) {
        const bool needs_expression =
            !distance_resolution_workflow_.request.variable_name.empty() ||
            distance_resolution_workflow_.request.reason ==
                "distanceExpressionRequiresManualEdit" ||
            distance_resolution_workflow_.request.reason ==
                "conflictingManualDistanceExpressions";
        const bool cached_satisfies_request = needs_expression
            ? !cached->second.distance_expression.empty()
            : (!cached->second.boundary_token.empty() ||
               (distance_resolution_workflow_.request.can_confirm_reuse &&
                cached->second.confirm_environment_mismatch));
        if (cached_satisfies_request) {
            size_t target_count = 0;
            size_t matching_count = 0;
            auto count_target = [&](const std::string& edit_id) {
                auto change = distance_resolution_workflow_.candidate_changes.find(edit_id);
                if (change == distance_resolution_workflow_.candidate_changes.end()) return;
                ++target_count;
                const MapElementPendingChange& value = change->second;
                if (value.distance_resolution_key !=
                    distance_resolution_workflow_.request.resolution_key) {
                    return;
                }
                const bool matches = needs_expression
                    ? value.distance_expression == cached->second.distance_expression
                    : (!cached->second.boundary_token.empty()
                       ? value.distance_boundary_token == cached->second.boundary_token
                       : value.confirm_environment_mismatch ==
                         cached->second.confirm_environment_mismatch);
                if (matches) ++matching_count;
            };
            for (const std::string& edit_id :
                 distance_resolution_workflow_.request.affected_edit_ids) {
                count_target(edit_id);
            }
            if (target_count == 0 && !distance_resolution_workflow_.origin_edit_id.empty()) {
                count_target(distance_resolution_workflow_.origin_edit_id);
            }
            if (target_count == 0 || matching_count != target_count) {
                apply_distance_resolution_choice(cached->second);
                return;
            }

            DistanceResolutionChoice remaining = cached->second;
            if (needs_expression) {
                remaining.distance_expression.clear();
            } else {
                remaining.boundary_token.clear();
                remaining.confirm_environment_mismatch = false;
            }
            if (remaining.boundary_token.empty() && remaining.distance_expression.empty() &&
                !remaining.confirm_environment_mismatch) {
                distance_resolution_choices_.erase(cached);
            } else {
                cached->second = std::move(remaining);
            }
        }
    }

    const bool needs_expression =
        !distance_resolution_workflow_.request.variable_name.empty() ||
        distance_resolution_workflow_.request.reason ==
            "distanceExpressionRequiresManualEdit" ||
        distance_resolution_workflow_.request.reason ==
            "conflictingManualDistanceExpressions";
    distance_resolution_workflow_.phase = needs_expression
        ? DistanceResolutionPhase::EditExpression
        : DistanceResolutionPhase::ConfirmAction;
    distance_resolution_workflow_.popup_requested = true;
    if (inspector_.open) {
        set_program_status("status.edit.distance_resolution_required");
    }
}

void App::apply_distance_resolution_choice(const DistanceResolutionChoice& choice) {
    DistanceResolutionWorkflowState& workflow = distance_resolution_workflow_;
    if (workflow.request.resolution_key.empty() || workflow.candidate_changes.empty()) return;

    DistanceResolutionChoice combined;
    auto cached = distance_resolution_choices_.find(workflow.request.resolution_key);
    if (cached != distance_resolution_choices_.end()) combined = cached->second;
    if (!choice.distance_expression.empty()) {
        combined.distance_expression = choice.distance_expression;
    }
    if (!choice.boundary_token.empty()) {
        combined.boundary_token = choice.boundary_token;
        combined.confirm_environment_mismatch = false;
    }
    if (choice.confirm_environment_mismatch) {
        combined.confirm_environment_mismatch = true;
        combined.boundary_token.clear();
    }
    distance_resolution_choices_[workflow.request.resolution_key] = combined;
    bool applied = false;
    auto apply_to_edit = [&](const std::string& edit_id) {
        auto change = workflow.candidate_changes.find(edit_id);
        if (change == workflow.candidate_changes.end()) return;
        MapElementPendingChange& value = change->second;
        value.distance_resolution_key = workflow.request.resolution_key;
        value.distance_boundary_token = combined.boundary_token;
        value.distance_expression = combined.distance_expression;
        value.confirm_environment_mismatch = combined.confirm_environment_mismatch;
        applied = true;
    };
    for (const std::string& edit_id : workflow.request.affected_edit_ids) {
        apply_to_edit(edit_id);
    }
    for (auto& item : workflow.candidate_changes) {
        if (item.second.distance_resolution_key == workflow.request.resolution_key) {
            apply_to_edit(item.first);
        }
    }
    if (!applied && !workflow.origin_edit_id.empty()) apply_to_edit(workflow.origin_edit_id);
    if (!applied) {
        for (auto& item : workflow.candidate_changes) {
            if (item.second.field_changes.find("distance") == item.second.field_changes.end()) continue;
            apply_to_edit(item.first);
            break;
        }
    }

    text_preview_.placement = TextPreviewPlacementState{};
    workflow.phase = DistanceResolutionPhase::None;
    workflow.popup_requested = false;
    workflow.retry_requested = applied;
}

void App::select_distance_resolution_boundary(const std::string& token) {
    if (distance_resolution_workflow_.phase != DistanceResolutionPhase::SelectBoundary) return;
    const auto& boundaries = distance_resolution_workflow_.request.allowed_boundaries;
    const auto selected = std::find_if(
        boundaries.begin(), boundaries.end(),
        [&](const DistanceResolutionBoundary& boundary) { return boundary.token == token; });
    if (selected == boundaries.end()) return;
    text_preview_.placement.selected_boundary_token = token;
}

void App::confirm_distance_resolution_boundary() {
    if (distance_resolution_workflow_.phase != DistanceResolutionPhase::SelectBoundary) return;
    const std::string token = text_preview_.placement.selected_boundary_token;
    if (token.empty()) return;
    const auto& boundaries = distance_resolution_workflow_.request.allowed_boundaries;
    const bool allowed = std::any_of(
        boundaries.begin(), boundaries.end(),
        [&](const DistanceResolutionBoundary& boundary) { return boundary.token == token; });
    if (!allowed) return;
    DistanceResolutionChoice choice;
    choice.boundary_token = token;
    apply_distance_resolution_choice(choice);
}

void App::cancel_distance_resolution_workflow() {
    const bool had_active_workflow =
        distance_resolution_workflow_.phase != DistanceResolutionPhase::None ||
        distance_resolution_workflow_.retry_requested;
    if (!distance_resolution_workflow_.request.resolution_key.empty()) {
        distance_resolution_choices_.erase(
            distance_resolution_workflow_.request.resolution_key);
    }
    text_preview_.placement = TextPreviewPlacementState{};
    distance_resolution_workflow_ = DistanceResolutionWorkflowState{};
    if (had_active_workflow && inspector_.open) {
        set_program_status("status.edit.distance_resolution_cancelled");
    }
}

void App::process_distance_resolution_retry() {
    if (!distance_resolution_workflow_.retry_requested) return;
    DistanceResolutionWorkflowState workflow = std::move(distance_resolution_workflow_);
    distance_resolution_workflow_ = DistanceResolutionWorkflowState{};
    apply_edit_ledger_to_preview(workflow.candidate_changes,
                                 std::move(workflow.reload_request),
                                 workflow.applying_delete,
                                 std::move(workflow.origin_edit_id));
}

bool App::save_pending_edits(bool refresh_inspector) {
    if (!edit_actions_available()) return false;
    if (load_state_.running) return false;
    if (has_unapplied_editable_list_drafts()) {
        add_log("[warning]gui_kme.cpp: Save blocked by unapplied editable-list drafts");
        set_program_status("status.edit.apply_list_before_save");
        return false;
    }
    if (!handle_ || !has_pending_edits()) return false;

    if (!edit_memory_matches_pending_ledger_) {
        std::vector<DistanceResolutionRequest> replay_requests;
        if (!sync_edit_memory_with_ledger(pending_edit_changes_, &replay_requests)) {
            add_log("[error]gui_kme.cpp: edit save blocked because the pending ledger "
                    "could not be restored to the maploader working copy");
            return false;
        }
    }

    const bool inspector_target_deleted = inspector_.open && row_is_pending_delete(inspector_.edit_id);
    std::optional<MapElementInspectorRequest> inspector_request;
    if (refresh_inspector && inspector_.open && !inspector_target_deleted) {
        inspector_request = make_inspector_reload_request(inspector_);
    }

    // Apply/Revert/Delete keep the maploader working copy synchronized with the
    // pending ledger. Save is only the disk-write boundary; resetting, replaying,
    // or rebuilding the GUI model here would parse the same map state again.
    KvEditReportSnapshot report{};
    if (!kv_edit_commit_typed(handle_, &report, sizeof(report))) {
        const char* err = kv_get_last_error();
        add_log(std::string("[error]gui_kme.cpp: edit save failed: ") + (err ? err : "unknown error"));
        return false;
    }

    int committed_file_count = 0;
    if (!parse_and_log_edit_report(report, "[info]gui_kme.cpp: edit save committed",
                                   nullptr, nullptr, &committed_file_count)) {
        return false;
    }
    if (committed_file_count <= 0) {
        edit_memory_matches_pending_ledger_ = false;
        add_log("[error]gui_kme.cpp: edit save returned no committed source files; "
                "the pending ledger was retained");
        return false;
    }
    distance_resolution_choices_.clear();
    distance_resolution_workflow_ = DistanceResolutionWorkflowState{};
    text_preview_.placement = TextPreviewPlacementState{};
    pending_edit_changes_.clear();
    edit_memory_matches_pending_ledger_ = true;
    original_edit_rows_.clear();
    refresh_text_preview_from_working_copy();
    if (refresh_inspector && inspector_target_deleted) {
        inspector_.open = false;
    } else if (inspector_request) {
        open_element_inspector(*inspector_request);
    }
    set_program_status("status.edit.saved");
    return true;
}

bool App::discard_pending_edits() {
    if (!has_pending_edits()) {
        distance_resolution_choices_.clear();
        distance_resolution_workflow_ = DistanceResolutionWorkflowState{};
        text_preview_.placement = TextPreviewPlacementState{};
        discard_all_editable_list_drafts();
        return true;
    }
    if (!apply_edit_ledger_to_preview({}, std::nullopt, false)) return false;
    distance_resolution_choices_.clear();
    distance_resolution_workflow_ = DistanceResolutionWorkflowState{};
    text_preview_.placement = TextPreviewPlacementState{};
    discard_all_editable_list_drafts();
    return true;
}

bool App::revert_all_pending_edits() {
    if (!edit_actions_available() || !has_unsaved_edit_state()) return false;

    std::optional<MapElementInspectorRequest> inspector_request;
    if (inspector_.open && !inspector_.edit_id.empty()) {
        MapElementInspectorRequest request;
        request.edit_id = inspector_.edit_id;
        request.row_kind = inspector_.row_kind;
        request.source_file = inspector_.source_file;
        request.line = inspector_.line;
        request.column = inspector_.column;
        inspector_request = std::move(request);
    }

    clear_scene_placement_edit_target();
    if (!discard_pending_edits()) return false;

    if (inspector_request && !open_element_inspector(*inspector_request)) {
        inspector_ = MapElementInspectorState{};
        pending_inspector_request_.reset();
    }
    set_program_status("status.edit.reverted");
    return true;
}

bool App::resolve_pending_close_action(bool save_changes) {
    const PendingCloseAction action = pending_close_action_;
    if (action == PendingCloseAction::None) return true;

    if (save_changes) {
        if (!save_pending_edits(false)) return false;
    } else if (action == PendingCloseAction::DisableEditMode && !discard_pending_edits()) {
        return false;
    }

    pending_close_action_ = PendingCloseAction::None;
    if (action == PendingCloseAction::DisableEditMode) {
        apply_edit_mode_enabled(false);
    } else if (action == PendingCloseAction::ExitApplication) {
        PostQuitMessage(0);
    }
    return true;
}

bool App::render_map_element_field_control(MapElementEditFieldState& field,
                                           float width) {
    ImGui::SetNextItemWidth(width);
    if (field.key_source == MapElementKeySource::None) {
        return ImGui::InputText(field.label.c_str(), &field.value);
    }

    const std::string current_value = edit_field_buffer_text(field);
    bool input_changed = false;
    if (!ImGui::BeginCombo(field.label.c_str(), current_value.c_str())) {
        return false;
    }

    int option_id = 0;
    auto select_option = [&](const std::string& candidate,
                             const std::string& display_text) {
        const bool selected = field.key_source == MapElementKeySource::Track
            ? track_key_values_equal(current_value, candidate)
            : resource_key_values_equal(current_value, candidate);
        ImGui::PushID(option_id++);
        if (ImGui::Selectable(display_text.c_str(), selected) &&
            candidate != current_value) {
            set_edit_field_buffer(field, candidate);
            input_changed = true;
        }
        if (selected) ImGui::SetItemDefaultFocus();
        ImGui::PopID();
    };
    auto render_table_options = [&](const std::vector<TableRow>& rows,
                                    const std::string& cell_key) {
        for (const TableRow& row : rows) {
            const std::string& candidate = table_cell(row, cell_key);
            if (candidate.empty()) continue;
            select_option(candidate, candidate);
        }
    };

    switch (field.key_source) {
    case MapElementKeySource::Structure:
        render_table_options(model_.structure_models, "structureKey");
        break;
    case MapElementKeySource::Sound:
        render_table_options(model_.sound_list, "soundKey");
        break;
    case MapElementKeySource::Sound3D:
        render_table_options(model_.sound_3d_list, "soundKey");
        break;
    case MapElementKeySource::SignalAspect:
        render_table_options(model_.signal_aspects, "signalAspectKey");
        break;
    case MapElementKeySource::Track: {
        static const std::array<std::string, 3> k_own_track_keys = {
            "0", "''", "'0'"
        };
        for (const std::string& candidate : k_own_track_keys) {
            select_option(candidate, candidate + " " + tr("value.own_track"));
        }
        for (const OtherTrack& track : model_.other_tracks) {
            if (track.key.empty()) continue;
            const bool own_track_key = std::any_of(
                k_own_track_keys.begin(), k_own_track_keys.end(),
                [&](const std::string& candidate) {
                    return track_key_values_equal(track.key, candidate);
                });
            if (!own_track_key) select_option(track.key, track.key);
        }
        break;
    }
    case MapElementKeySource::None:
        break;
    }
    ImGui::EndCombo();
    return input_changed;
}

void App::render_map_element_field_inputs(MapElementInspectorState& inspector) {
    const bool repeater_inspector = inspector.row_kind == "repeater";
    const bool section_inspector = inspector.row_kind == "section.begin" ||
        inspector.row_kind == "section.speedLimit";
    for (size_t field_index = 0; field_index < inspector.fields.size(); ++field_index) {
        MapElementEditFieldState& field = inspector.fields[field_index];
        if (repeater_inspector && is_repeater_structure_key_field(field)) {
            continue;
        }
        if (section_inspector && is_section_values_field(field)) {
            continue;
        }
        if (field.key == "structureKey" && field_index > 0) ImGui::Separator();
        if (repeater_inspector && field.key == "repeaterKey") ImGui::Separator();
        const bool changed = edit_field_buffer_text(field) != field.original_value;
        if (changed) ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.28f, 0.23f, 0.08f, 1.0f));
        const float input_width =
            std::max(160.0f, ImGui::GetContentRegionAvail().x * 0.55f);
        ImGui::BeginDisabled(field.read_only);
        const std::string previous_value = edit_field_buffer_text(field);
        const bool input_changed = render_map_element_field_control(field, input_width);
        ImGui::EndDisabled();
        if (input_changed && field.requires_signal_full_form &&
            inspector.source_signal_short_form &&
            !inspector.signal_full_form_conversion_draft) {
            inspector.pending_signal_full_form_field = field.key;
            inspector.pending_signal_full_form_value = edit_field_buffer_text(field);
            set_edit_field_buffer(field, previous_value);
            inspector.signal_full_form_prompt_requested = true;
        }
        if (ImGui::IsItemDeactivatedAfterEdit() &&
            !validate_and_canonicalize_edit_field(field, true)) {
            set_program_status("status.edit.invalid_number");
        }
        if (changed) ImGui::PopStyleColor();
        if (!field.source_distance_string.empty()) {
            render_inline_wrapped_text(tr("label.source_distance_string").c_str(),
                                       field.source_distance_string);
        }
    }
}

void App::render_section_values_edit_ui(MapElementInspectorState& inspector) {
    ImGui::Separator();
    ImGui::TextUnformatted(tr("label.section_parameters").c_str());
    std::vector<size_t> value_field_indices;
    for (size_t index = 0; index < inspector.fields.size(); ++index) {
        if (is_section_values_field(inspector.fields[index])) {
            value_field_indices.push_back(index);
        }
    }
    std::optional<size_t> remove_value_index;
    std::optional<size_t> move_value_from;
    std::optional<size_t> move_value_to;
    const float section_value_input_x = ImGui::GetCursorPosX();
    const float section_value_input_width =
        std::max(160.0f, ImGui::GetContentRegionAvail().x * 0.42f);
    const float add_section_value_button_width =
        std::max(80.0f, section_value_input_width * 0.5f);
    for (size_t list_index = 0; list_index < value_field_indices.size(); ++list_index) {
        MapElementEditFieldState& field = inspector.fields[value_field_indices[list_index]];
        const bool changed = edit_field_buffer_text(field) != field.original_value;
        if (changed) ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.28f, 0.23f, 0.08f, 1.0f));
        ImGui::SetNextItemWidth(section_value_input_width);
        ImGui::InputText(field.label.c_str(), &field.value);
        if (changed) ImGui::PopStyleColor();
        ImGui::SameLine();
        ImGui::BeginDisabled(list_index == 0);
        if (ImGui::SmallButton((u8"\u2191##SectionValue" + std::to_string(list_index)).c_str())) {
            move_value_from = list_index;
            move_value_to = list_index - 1;
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(list_index + 1 >= value_field_indices.size());
        if (ImGui::SmallButton((u8"\u2193##SectionValue" + std::to_string(list_index)).c_str())) {
            move_value_from = list_index;
            move_value_to = list_index + 1;
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(value_field_indices.size() <= 1);
        if (ImGui::SmallButton(("-##SectionValue" + std::to_string(list_index)).c_str())) {
            remove_value_index = list_index;
        }
        ImGui::EndDisabled();
    }
    ImGui::SetCursorPosX(section_value_input_x +
                         (section_value_input_width - add_section_value_button_width) * 0.5f);
    if (ImGui::Button("+##SectionValue", ImVec2(add_section_value_button_width, 0.0f))) {
        inspector.fields.push_back(make_section_values_field(
            inspector, value_field_indices.size(), {}));
    }
    if (remove_value_index && *remove_value_index < value_field_indices.size()) {
        inspector.fields.erase(inspector.fields.begin() + static_cast<std::ptrdiff_t>(
            value_field_indices[*remove_value_index]));
    } else if (move_value_from && move_value_to &&
               *move_value_from < value_field_indices.size() &&
               *move_value_to < value_field_indices.size()) {
        std::swap(inspector.fields[value_field_indices[*move_value_from]],
                  inspector.fields[value_field_indices[*move_value_to]]);
    }
    if (remove_value_index || move_value_from) {
        reindex_section_values_fields(inspector);
    }
}

void App::render_element_inspector() {
    if (!edit_mode_enabled_) {
        clear_scene_placement_edit_target();
        inspector_.open = false;
        return;
    }
    if (!inspector_.open) return;
    std::string title = tr("dialog.element_properties") + "###ElementInspector";
    if (!ImGui::Begin(title.c_str(), &inspector_.open)) {
        const bool closed = !inspector_.open;
        ImGui::End();
        if (closed) {
            cancel_distance_resolution_workflow();
            clear_scene_placement_edit_target();
        }
        return;
    }

    ImGui::TextUnformatted(tr("label.source_file").c_str());
    ImGui::SameLine();
    ImGui::TextUnformatted(inspector_.source_file_name.c_str());
    const bool source_file_hovered = ImGui::IsItemHovered();
    render_source_file_context_menu("element_inspector_source_file_context",
                                    inspector_.source_file);
    if (source_file_hovered && !inspector_.source_file.empty() &&
        !ImGui::IsPopupOpen("element_inspector_source_file_context")) {
        ImGui::SetTooltip("%s", inspector_.source_file.c_str());
    }
    ImGui::Text("%s %d:%d", tr("label.source_position").c_str(), inspector_.line, inspector_.column);
    if (!inspector_.raw_statement.empty()) {
        ImGui::Separator();
        ImGui::TextUnformatted(tr("label.raw_statement").c_str());
        ImGui::TextWrapped("%s", inspector_.raw_statement.c_str());
    }
    const bool repeater_inspector = inspector_.row_kind == "repeater";
    bool repeater_navigation_changed = false;
    const auto render_repeater_end_source = [&] {
        if (inspector_.repeater_has_multiple_begins) {
            ImGui::TextUnformatted(tr("status.repeater_multiple_begins").c_str());
            ImGui::BeginDisabled(inspector_.repeater_previous_begin_edit_id.empty());
            if (ImGui::Button(tr("button.previous").c_str())) {
                repeater_navigation_changed = navigate_repeater_inspector(false);
            }
            ImGui::EndDisabled();
            if (repeater_navigation_changed) return;
            ImGui::SameLine();
            ImGui::BeginDisabled(inspector_.repeater_next_begin_edit_id.empty());
            if (ImGui::Button(tr("button.next").c_str())) {
                repeater_navigation_changed = navigate_repeater_inspector(true);
            }
            ImGui::EndDisabled();
            if (repeater_navigation_changed) return;
        }
        if (inspector_.repeater_boundary_kind == "open") {
            ImGui::TextUnformatted(tr("status.repeater_no_end").c_str());
        } else if (!inspector_.end_source_file_name.empty()) {
            ImGui::Text("%s %s %d:%d", tr("label.repeater_end_source").c_str(),
                        inspector_.end_source_file_name.c_str(), inspector_.end_line,
                        inspector_.end_column);
            if (!inspector_.end_raw_statement.empty()) {
                ImGui::TextWrapped("%s", inspector_.end_raw_statement.c_str());
            }
        }
    };

    ImGui::Separator();
    const bool section_inspector = inspector_.row_kind == "section.begin" ||
        inspector_.row_kind == "section.speedLimit";
    render_map_element_field_inputs(inspector_);
    if (repeater_inspector) {
        ImGui::Separator();
        render_repeater_end_source();
        if (repeater_navigation_changed) {
            ImGui::End();
            return;
        }
    }
    if (repeater_inspector) {
        ImGui::Separator();
        ImGui::TextUnformatted(tr("label.repeater_structure_keys").c_str());
        std::vector<size_t> key_field_indices;
        for (size_t index = 0; index < inspector_.fields.size(); ++index) {
            if (is_repeater_structure_key_field(inspector_.fields[index])) {
                key_field_indices.push_back(index);
            }
        }
        std::optional<size_t> remove_key_index;
        std::optional<size_t> move_key_from;
        std::optional<size_t> move_key_to;
        const float structure_key_input_x = ImGui::GetCursorPosX();
        const float structure_key_input_width =
            std::max(160.0f, ImGui::GetContentRegionAvail().x * 0.42f);
        const float add_structure_key_button_width =
            std::max(80.0f, structure_key_input_width * 0.5f);
        for (size_t list_index = 0; list_index < key_field_indices.size(); ++list_index) {
            MapElementEditFieldState& field = inspector_.fields[key_field_indices[list_index]];
            const bool changed = edit_field_buffer_text(field) != field.original_value;
            if (changed) ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.28f, 0.23f, 0.08f, 1.0f));
            render_map_element_field_control(field, structure_key_input_width);
            if (changed) ImGui::PopStyleColor();
            ImGui::SameLine();
            ImGui::BeginDisabled(list_index == 0);
            if (ImGui::SmallButton((u8"\u2191##RepeaterKey" + std::to_string(list_index)).c_str())) {
                move_key_from = list_index;
                move_key_to = list_index - 1;
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::BeginDisabled(list_index + 1 >= key_field_indices.size());
            if (ImGui::SmallButton((u8"\u2193##RepeaterKey" + std::to_string(list_index)).c_str())) {
                move_key_from = list_index;
                move_key_to = list_index + 1;
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::BeginDisabled(key_field_indices.size() <= 1);
            if (ImGui::SmallButton(("-##RepeaterKey" + std::to_string(list_index)).c_str())) {
                remove_key_index = list_index;
            }
            ImGui::EndDisabled();
        }
        ImGui::SetCursorPosX(structure_key_input_x +
                             (structure_key_input_width - add_structure_key_button_width) * 0.5f);
        if (ImGui::Button("+##RepeaterKey", ImVec2(add_structure_key_button_width, 0.0f))) {
            inspector_.fields.push_back(make_repeater_structure_key_field(
                inspector_, key_field_indices.size(), {}));
        }
        if (remove_key_index && *remove_key_index < key_field_indices.size()) {
            inspector_.fields.erase(inspector_.fields.begin() + static_cast<std::ptrdiff_t>(
                key_field_indices[*remove_key_index]));
        } else if (move_key_from && move_key_to &&
                   *move_key_from < key_field_indices.size() &&
                   *move_key_to < key_field_indices.size()) {
            std::swap(inspector_.fields[key_field_indices[*move_key_from]],
                      inspector_.fields[key_field_indices[*move_key_to]]);
        }
        if (remove_key_index || move_key_from) {
            reindex_repeater_structure_key_fields(inspector_);
        }
    }
    if (section_inspector) {
        render_section_values_edit_ui(inspector_);
    }
    if (ImGui::Button(tr("button.apply").c_str())) apply_inspector_changes();
    ImGui::SameLine();
    if (ImGui::Button(tr("button.close").c_str())) {
        cancel_distance_resolution_workflow();
        clear_scene_placement_edit_target();
        inspector_.open = false;
    }

    ImGui::End();
    if (!inspector_.open &&
        (distance_resolution_workflow_.phase != DistanceResolutionPhase::None ||
         distance_resolution_workflow_.retry_requested)) {
        cancel_distance_resolution_workflow();
    }
    if (!inspector_.open) clear_scene_placement_edit_target();
}

namespace {

// One wizard entry describes one writeable map statement the user can add.
// The syntax line is BVE syntax itself (language-neutral); the usage text is
// localized through multilanguage keys. These types are local to the wizard
// implementation so shared GUI state does not expose its static metadata.
struct NewElementFieldSpec {
    const char* key;
    const char* label;
    MapElementNumericConstraint constraint = MapElementNumericConstraint::None;
    bool required = true;
    const char* default_value = "";
};

struct NewElementTemplate {
    const char* id;
    std::string_view row_kind;
    std::string_view method;
    const char* syntax;
    const char* usage_key;
    bool section_values = false;
    std::vector<NewElementFieldSpec> fields;
};

const std::vector<NewElementTemplate>& new_element_templates() {
    static const std::vector<NewElementTemplate> templates = {
        {
            "structure.put", "structure.put", "Put",
            "Structure[structureKey].Put(trackKey, x, y, z, rx, ry, rz, tilt, span);",
            "new_element.usage.structure.put", false,
            {
                {"distance", "distance", MapElementNumericConstraint::Finite, true, "0"},
                {"structureKey", "structureKey", MapElementNumericConstraint::None, true, ""},
                {"trackKey", "trackKey", MapElementNumericConstraint::None, true, "0"},
                {"x", "x", MapElementNumericConstraint::Truncate3, true, "0"},
                {"y", "y", MapElementNumericConstraint::Truncate3, true, "0"},
                {"z", "z", MapElementNumericConstraint::Truncate3, true, "0"},
                {"rx", "rx", MapElementNumericConstraint::Truncate3, true, "0"},
                {"ry", "ry", MapElementNumericConstraint::Truncate3, true, "0"},
                {"rz", "rz", MapElementNumericConstraint::Truncate3, true, "0"},
                {"tilt", "tilt", MapElementNumericConstraint::Integer, true, "0"},
                {"span", "span", MapElementNumericConstraint::Truncate3, true, "0"},
            },
        },
        {
            "structure.put0", "structure.put", "Put0",
            "Structure[structureKey].Put0(trackKey, tilt, span);",
            "new_element.usage.structure.put0", false,
            {
                {"distance", "distance", MapElementNumericConstraint::Finite, true, "0"},
                {"structureKey", "structureKey", MapElementNumericConstraint::None, true, ""},
                {"trackKey", "trackKey", MapElementNumericConstraint::None, true, "0"},
                {"tilt", "tilt", MapElementNumericConstraint::Integer, true, "0"},
                {"span", "span", MapElementNumericConstraint::Truncate3, true, "0"},
            },
        },
        {
            "structure.put_between", "structure.between", "",
            "Structure[structureKey].PutBetween(trackKey1, trackKey2, flag);",
            "new_element.usage.structure.put_between", false,
            {
                {"distance", "distance", MapElementNumericConstraint::Finite, true, "0"},
                {"structureKey", "structureKey", MapElementNumericConstraint::None, true, ""},
                {"trackKey1", "trackKey1", MapElementNumericConstraint::None, true, "0"},
                {"trackKey2", "trackKey2", MapElementNumericConstraint::None, true, "0"},
                {"flag", "flag", MapElementNumericConstraint::Finite, true, "0"},
            },
        },
        {
            "station.put", "station.put", "",
            "Station[stationKey].Put(door, margin1, margin2);",
            "new_element.usage.station.put", false,
            {
                {"distance", "distance", MapElementNumericConstraint::Finite, true, "0"},
                {"stationKey", "stationKey", MapElementNumericConstraint::None, true, ""},
                {"door", "door", MapElementNumericConstraint::Finite, true, "0"},
                {"margin1", "back", MapElementNumericConstraint::Finite, true, "0"},
                {"margin2", "front", MapElementNumericConstraint::Finite, true, "0"},
            },
        },
        {
            "signal.put", "signal.put", "",
            "Signal[signalAspectKey].Put(section, trackKey, x, y, z, rx, ry, rz, tilt, span);",
            "new_element.usage.signal.put", false,
            {
                {"distance", "distance", MapElementNumericConstraint::Finite, true, "0"},
                {"signalAspectKey", "signalAspectKey", MapElementNumericConstraint::None, true, ""},
                {"section", "section", MapElementNumericConstraint::Finite, true, "0"},
                {"trackKey", "trackKey", MapElementNumericConstraint::None, true, "0"},
                {"x", "x", MapElementNumericConstraint::Truncate3, true, "0"},
                {"y", "y", MapElementNumericConstraint::Truncate3, true, "0"},
                {"z", "z", MapElementNumericConstraint::Truncate3, true, "0"},
                {"rx", "rx", MapElementNumericConstraint::Truncate3, true, "0"},
                {"ry", "ry", MapElementNumericConstraint::Truncate3, true, "0"},
                {"rz", "rz", MapElementNumericConstraint::Truncate3, true, "0"},
                {"tilt", "tilt", MapElementNumericConstraint::Integer, true, "0"},
                {"span", "span", MapElementNumericConstraint::Truncate3, true, "0"},
            },
        },
        {
            "speedlimit.begin", "speedlimit", "Begin",
            "SpeedLimit.Begin(speed);",
            "new_element.usage.speedlimit.begin", false,
            {
                {"distance", "distance", MapElementNumericConstraint::Finite, true, "0"},
                {"speed", "speed", MapElementNumericConstraint::Finite, true, "0"},
            },
        },
        {
            "speedlimit.end", "speedlimit", "End",
            "SpeedLimit.End();",
            "new_element.usage.speedlimit.end", false,
            {
                {"distance", "distance", MapElementNumericConstraint::Finite, true, "0"},
            },
        },
        {
            "section.begin", "section.begin", "Begin",
            "Section.Begin(signal0, ..., signalN);",
            "new_element.usage.section.begin", true,
            {
                {"distance", "distance", MapElementNumericConstraint::Finite, true, "0"},
            },
        },
        {
            "section.beginnew", "section.begin", "BeginNew",
            "Section.BeginNew(signal0, ..., signalN);",
            "new_element.usage.section.beginnew", true,
            {
                {"distance", "distance", MapElementNumericConstraint::Finite, true, "0"},
            },
        },
        {
            "section.setspeedlimit", "section.speedLimit", "SetSpeedLimit",
            "Section.SetSpeedLimit(v0, ..., vn);",
            "new_element.usage.section.setspeedlimit", true,
            {
                {"distance", "distance", MapElementNumericConstraint::Finite, true, "0"},
            },
        },
        {
            "section.signal_speedlimit", "section.speedLimit", "Signal.SpeedLimit",
            "Signal.SpeedLimit(v0, ..., vn);",
            "new_element.usage.section.signal_speedlimit", true,
            {
                {"distance", "distance", MapElementNumericConstraint::Finite, true, "0"},
            },
        },
        {
            "irregularity.change", "irregularity.change", "",
            "Irregularity.Change(x, y, r, lx, ly, lr);",
            "new_element.usage.irregularity.change", false,
            {
                {"distance", "distance", MapElementNumericConstraint::Finite, true, "0"},
                {"x", "x", MapElementNumericConstraint::Finite, true, "0"},
                {"y", "y", MapElementNumericConstraint::Finite, true, "0"},
                {"r", "r", MapElementNumericConstraint::Finite, true, "0"},
                {"lx", "lx", MapElementNumericConstraint::Finite, true, "0"},
                {"ly", "ly", MapElementNumericConstraint::Finite, true, "0"},
                {"lr", "lr", MapElementNumericConstraint::Finite, true, "0"},
            },
        },
        {
            "beacon.put", "beacon.put", "",
            "Beacon.Put(type, section, sendData);",
            "new_element.usage.beacon.put", false,
            {
                {"distance", "distance", MapElementNumericConstraint::Finite, true, "0"},
                {"type", "type", MapElementNumericConstraint::Finite, true, "0"},
                {"section", "section", MapElementNumericConstraint::Finite, true, "0"},
                {"sendData", "sendData", MapElementNumericConstraint::Finite, true, "0"},
            },
        },
        {
            "map_sound.play", "mapSound.play", "",
            "Sound[soundKey].Play();",
            "new_element.usage.map_sound.play", false,
            {
                {"distance", "distance", MapElementNumericConstraint::Finite, true, "0"},
                {"soundKey", "soundKey", MapElementNumericConstraint::None, true, ""},
            },
        },
        {
            "map_sound3d.put", "mapSound3D.put", "",
            "Sound3D[soundKey].Put(x, y);",
            "new_element.usage.map_sound3d.put", false,
            {
                {"distance", "distance", MapElementNumericConstraint::Finite, true, "0"},
                {"soundKey", "soundKey", MapElementNumericConstraint::None, true, ""},
                {"x", "x", MapElementNumericConstraint::Finite, true, "0"},
                {"y", "y", MapElementNumericConstraint::Finite, true, "0"},
            },
        },
        {
            "rolling_noise.change", "rollingNoise.change", "",
            "RollingNoise.Change(index);",
            "new_element.usage.rolling_noise.change", false,
            {
                {"distance", "distance", MapElementNumericConstraint::Finite, true, "0"},
                {"index", "index", MapElementNumericConstraint::Finite, true, "0"},
            },
        },
        {
            "flange_noise.change", "flangeNoise.change", "",
            "FlangeNoise.Change(index);",
            "new_element.usage.flange_noise.change", false,
            {
                {"distance", "distance", MapElementNumericConstraint::Finite, true, "0"},
                {"index", "index", MapElementNumericConstraint::Finite, true, "0"},
            },
        },
        {
            "joint_noise.play", "jointNoise.play", "",
            "JointNoise.Play(index);",
            "new_element.usage.joint_noise.play", false,
            {
                {"distance", "distance", MapElementNumericConstraint::Finite, true, "0"},
                {"index", "index", MapElementNumericConstraint::Finite, true, "0"},
            },
        },
        {
            "background.change", "background.change", "",
            "Background.Change(structureKey);",
            "new_element.usage.background.change", false,
            {
                {"distance", "distance", MapElementNumericConstraint::Finite, true, "0"},
                {"structureKey", "structureKey", MapElementNumericConstraint::None, true, ""},
            },
        },
        {
            "adhesion.change", "adhesion.change", "",
            "Adhesion.Change(a); / Adhesion.Change(a, b, c);",
            "new_element.usage.adhesion.change", false,
            {
                {"distance", "distance", MapElementNumericConstraint::Finite, true, "0"},
                {"a", "a", MapElementNumericConstraint::Finite, true, "0"},
                {"b", "b", MapElementNumericConstraint::Finite, false, ""},
                {"c", "c", MapElementNumericConstraint::Finite, false, ""},
            },
        },
        {
            "cab_illuminance.set", "cabIlluminance.change", "Set",
            "CabIlluminance.Set(value);",
            "new_element.usage.cab_illuminance.set", false,
            {
                {"distance", "distance", MapElementNumericConstraint::Finite, true, "0"},
                {"value", "value", MapElementNumericConstraint::Finite, true, "0"},
            },
        },
        {
            "cab_illuminance.interpolate", "cabIlluminance.change", "Interpolate",
            "CabIlluminance.Interpolate(value);",
            "new_element.usage.cab_illuminance.interpolate", false,
            {
                {"distance", "distance", MapElementNumericConstraint::Finite, true, "0"},
                {"value", "value", MapElementNumericConstraint::Finite, true, "0"},
            },
        },
        {
            "fog.set", "fog.change", "Set",
            "Fog.Set(density, red, green, blue);",
            "new_element.usage.fog.set", false,
            {
                {"distance", "distance", MapElementNumericConstraint::Finite, true, "0"},
                {"density", "density", MapElementNumericConstraint::Finite, true, "0"},
                {"red", "red", MapElementNumericConstraint::Finite, true, "0"},
                {"green", "green", MapElementNumericConstraint::Finite, true, "0"},
                {"blue", "blue", MapElementNumericConstraint::Finite, true, "0"},
            },
        },
        {
            "fog.interpolate", "fog.change", "Interpolate",
            "Fog.Interpolate(); / Fog.Interpolate(density); / Fog.Interpolate(density, red, green, blue);",
            "new_element.usage.fog.interpolate", false,
            {
                {"distance", "distance", MapElementNumericConstraint::Finite, true, "0"},
                {"density", "density", MapElementNumericConstraint::Finite, false, ""},
                {"red", "red", MapElementNumericConstraint::Finite, false, ""},
                {"green", "green", MapElementNumericConstraint::Finite, false, ""},
                {"blue", "blue", MapElementNumericConstraint::Finite, false, ""},
            },
        },
        {
            "draw_distance.change", "drawDistance.change", "",
            "DrawDistance.Change(value);",
            "new_element.usage.draw_distance.change", false,
            {
                {"distance", "distance", MapElementNumericConstraint::Finite, true, "0"},
                {"value", "value", MapElementNumericConstraint::Finite, true, "0"},
            },
        },
    };
    return templates;
}

bool new_element_target_is_resource_list(
    const MapModel& model, const std::string& file_path) {
    for (const ResourceListSource& source : model.resource_list_sources) {
        if (source.present && !source.resolved_path.empty() &&
            source.resolved_path == file_path) {
            return true;
        }
    }
    return false;
}

std::vector<std::string> new_element_target_candidates(const MapModel& model) {
    std::map<std::string, size_t> distance_counts;
    for (const EditStatementInfo& statement : model.edit_statements) {
        // Distance.Set is the parser's internal classification for a source
        // line whose expression sets the current BVE distance. It is not a
        // BVE source function and must never be emitted as source text.
        if (statement.statement_kind != "Distance.Set" ||
            statement.source.file_path.empty() ||
            new_element_target_is_resource_list(model, statement.source.file_path)) {
            continue;
        }
        ++distance_counts[statement.source.file_path];
    }

    std::vector<std::pair<std::string, size_t>> ranked;
    ranked.reserve(distance_counts.size());
    for (const EditSourceFileInfo& file : model.edit_files) {
        auto count = distance_counts.find(file.file_path);
        if (count != distance_counts.end()) {
            ranked.emplace_back(file.file_path, count->second);
        }
    }
    std::stable_sort(ranked.begin(), ranked.end(),
                     [](const auto& lhs, const auto& rhs) {
                         if (lhs.second != rhs.second) return lhs.second > rhs.second;
                         return lhs.first < rhs.first;
                     });

    std::vector<std::string> result;
    result.reserve(ranked.size());
    for (const auto& entry : ranked) result.push_back(entry.first);
    return result;
}

} // namespace

void App::rebuild_new_element_wizard_form() {
    const std::vector<NewElementTemplate>& templates = new_element_templates();
    NewElementWizardState& wizard = new_element_wizard_;
    if (wizard.selected_template < 0 ||
        wizard.selected_template >= static_cast<int>(templates.size())) {
        wizard.selected_template = 0;
    }
    const NewElementTemplate& tpl = templates[static_cast<size_t>(wizard.selected_template)];
    MapElementInspectorState form;
    form.open = true;
    form.row_kind = std::string(tpl.row_kind);
    form.title = tr("dialog.new_element_wizard");
    form.source_file = wizard.target_file_path;
    form.source_file_name = display_name_from_path(form.source_file);
    for (const NewElementFieldSpec& spec : tpl.fields) {
        MapElementEditFieldState field;
        field.key = spec.key;
        field.backend_key = spec.key;
        field.label = spec.label;
        field.numeric_constraint = spec.constraint;
        field.key_source = map_element_key_source_for_field(form.row_kind, spec.key);
        field.required = spec.required;
        field.original_value = spec.default_value;
        set_edit_field_buffer(field, spec.default_value);
        form.fields.push_back(std::move(field));
    }
    if (tpl.section_values) {
        form.fields.push_back(make_section_values_field(form, 0, "0"));
    }
    wizard.form = std::move(form);
    wizard.built_template = wizard.selected_template;
    wizard.built_target_file = wizard.target_file_path;
}

bool App::apply_new_element_insert() {
    if (!edit_actions_available()) return false;
    const std::vector<NewElementTemplate>& templates = new_element_templates();
    NewElementWizardState& wizard = new_element_wizard_;
    if (wizard.selected_template < 0 ||
        wizard.selected_template >= static_cast<int>(templates.size())) {
        return false;
    }
    const NewElementTemplate& tpl = templates[static_cast<size_t>(wizard.selected_template)];
    MapElementInspectorState& form = wizard.form;
    if (wizard.built_template != wizard.selected_template ||
        wizard.built_target_file != wizard.target_file_path ||
        std::string_view(form.row_kind) != tpl.row_kind) {
        rebuild_new_element_wizard_form();
    }

    size_t section_value_count = 0;
    for (MapElementEditFieldState& field : form.fields) {
        if (is_section_values_field(field)) {
            const std::string value = trim_gui_ascii_copy(edit_field_buffer_text(field));
            if (value.empty()) {
                set_program_status("status.edit.required_field");
                return false;
            }
            double parsed_value = 0.0;
            if (!parse_gui_edit_number(value, &parsed_value)) {
                set_program_status("status.edit.invalid_number");
                return false;
            }
            ++section_value_count;
            continue;
        }
        const std::string value = trim_gui_ascii_copy(edit_field_buffer_text(field));
        if (field.required && value.empty()) {
            set_program_status("status.edit.required_field");
            return false;
        }
        if (!validate_and_canonicalize_edit_field(field, true)) {
            set_program_status("status.edit.invalid_number");
            return false;
        }
    }
    if (tpl.section_values && section_value_count == 0) {
        set_program_status("status.edit.required_field");
        return false;
    }
    auto field_blank = [&](const char* key) {
        const MapElementEditFieldState* field = find_inspector_field(form, key);
        return !field || trim_gui_ascii_copy(edit_field_buffer_text(*field)).empty();
    };
    if (tpl.row_kind == "adhesion.change" &&
        field_blank("b") != field_blank("c")) {
        set_program_status("status.edit.required_field");
        return false;
    }
    if (tpl.row_kind == "fog.change") {
        const bool density = !field_blank("density");
        const bool red = !field_blank("red");
        const bool green = !field_blank("green");
        const bool blue = !field_blank("blue");
        const bool all_colors = red && green && blue;
        const bool any_color = red || green || blue;
        const std::string method(tpl.method);
        if (method == "Set") {
            if (!density || !all_colors) {
                set_program_status("status.edit.required_field");
                return false;
            }
        } else if (any_color != all_colors || (all_colors && !density)) {
            set_program_status("status.edit.required_field");
            return false;
        }
    }

    MapElementPendingChange change;
    change.change_id = "insert-" + std::to_string(++wizard.insert_sequence);
    change.edit_id = change.change_id;
    change.row_kind = std::string(tpl.row_kind);
    change.operation = "insert";
    change.target_file_path = wizard.target_file_path;
    const EditSourceFileInfo* source_file =
        find_model_source_file(model_, change.target_file_path);
    if (!source_file) {
        add_log("[warn]gui_kme.cpp: insert target file is not part of the loaded map: " +
                change.target_file_path);
        return false;
    }
    // An insert has no existing source row whose optimistic-concurrency hash
    // must be preserved. Leave expectedSourceHash empty so maploader compares
    // the physical file against the current handle's authoritative disk
    // baseline. The GUI model may represent a dirty working-copy hash and can
    // otherwise become stale across an Apply -> Save -> Apply cycle.
    for (const MapElementEditFieldState& field : form.fields) {
        if (field.read_only) continue;
        if (is_section_values_field(field)) continue;
        const std::string& backend_key =
            field.backend_key.empty() ? field.key : field.backend_key;
        change.field_changes[backend_key] =
                trim_gui_ascii_copy(edit_field_buffer_text(field));
    }
    if (!tpl.method.empty()) {
        change.field_changes["method"] = std::string(tpl.method);
    }
    if (tpl.section_values) {
        change.field_changes["values.count"] = std::to_string(section_value_count);
        size_t value_index = 0;
        for (const MapElementEditFieldState& field : form.fields) {
            if (!is_section_values_field(field)) continue;
            change.field_changes["values." + std::to_string(value_index++)] =
                trim_gui_ascii_copy(edit_field_buffer_text(field));
        }
    }

    std::map<std::string, MapElementPendingChange> candidate = pending_edit_changes_;
    candidate[change.edit_id] = std::move(change);
    if (!apply_edit_ledger_to_preview(candidate, std::nullopt, false)) {
        if (distance_resolution_workflow_.phase == DistanceResolutionPhase::None &&
            !distance_resolution_workflow_.retry_requested) {
            set_program_status("status.edit.pending");
        }
        return false;
    }
    set_program_status("status.edit.applied_to_preview");
    return true;
}

void App::render_new_element_wizard() {
    if (!new_element_wizard_.open) return;
    if (!edit_actions_available()) {
        new_element_wizard_.open = false;
        return;
    }
    NewElementWizardState& wizard = new_element_wizard_;
    if (!wizard.target_candidates_built) {
        wizard.target_file_candidates = new_element_target_candidates(model_);
        wizard.target_candidates_built = true;
        if (wizard.target_file_path.empty() ||
            std::find(wizard.target_file_candidates.begin(),
                      wizard.target_file_candidates.end(),
                      wizard.target_file_path) == wizard.target_file_candidates.end()) {
            wizard.target_file_path = wizard.target_file_candidates.empty()
                ? std::string{}
                : wizard.target_file_candidates.front();
        }
    }
    const std::vector<NewElementTemplate>& templates = new_element_templates();

    const std::string title = tr("dialog.new_element_wizard") + "###NewElementWizard";
    ImGui::SetNextWindowSize(ImVec2(980.0f, 660.0f), ImGuiCond_Always);
    if (!ImGui::Begin(title.c_str(), &wizard.open, ImGuiWindowFlags_NoResize)) {
        ImGui::End();
        return;
    }

    ImGui::BeginChild("##NewElementTemplateList", ImVec2(360.0f, -ImGui::GetFrameHeightWithSpacing()),
                      true);
    const ImVec4 dim_text = ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled);
    for (int index = 0; index < static_cast<int>(templates.size()); ++index) {
        const NewElementTemplate& tpl = templates[static_cast<size_t>(index)];
        const std::string template_label = std::string(tpl.syntax) +
            "###NewElementTemplate_" + tpl.id;
        if (ImGui::Selectable(template_label.c_str(), wizard.selected_template == index)) {
            wizard.selected_template = index;
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", tr(tpl.usage_key).c_str());
        }
        ImGui::PushStyleColor(ImGuiCol_Text, dim_text);
        ImGui::TextWrapped("%s", tr(tpl.usage_key).c_str());
        ImGui::PopStyleColor();
        ImGui::Separator();
    }
    ImGui::EndChild();

    ImGui::SameLine();
    ImGui::BeginChild("##NewElementForm", ImVec2(-1.0f, -ImGui::GetFrameHeightWithSpacing()), true);
    ImGui::TextUnformatted(tr("label.new_element_target_file").c_str());
    ImGui::SetNextItemWidth(-1.0f);
    const std::string target_preview = wizard.target_file_path.empty()
        ? tr("status.edit.no_distance_source")
        : display_name_from_path(wizard.target_file_path);
    ImGui::BeginDisabled(wizard.target_file_candidates.empty());
    if (ImGui::BeginCombo("##NewElementTargetFile", target_preview.c_str())) {
        for (const std::string& file_path : wizard.target_file_candidates) {
            const bool selected = file_path == wizard.target_file_path;
            if (ImGui::Selectable(display_name_from_path(file_path).c_str(), selected)) {
                wizard.target_file_path = file_path;
            }
        }
        ImGui::EndCombo();
    }
    ImGui::EndDisabled();

    const bool has_target = !wizard.target_file_candidates.empty() &&
        !wizard.target_file_path.empty();
    if (!has_target) {
        ImGui::Separator();
        ImGui::TextWrapped("%s", tr("status.edit.no_distance_source").c_str());
    } else {
        if (wizard.built_template != wizard.selected_template ||
            wizard.built_target_file != wizard.target_file_path) {
            rebuild_new_element_wizard_form();
        }
        ImGui::Separator();
        const NewElementTemplate& tpl = templates[static_cast<size_t>(wizard.selected_template)];
        if (tpl.section_values) {
            render_map_element_field_inputs(wizard.form);
            render_section_values_edit_ui(wizard.form);
        } else {
            render_map_element_field_inputs(wizard.form);
        }
        ImGui::Separator();
        if (ImGui::Button(tr("button.apply").c_str())) apply_new_element_insert();
    }
    ImGui::SameLine();
    if (ImGui::Button(tr("button.close").c_str())) {
        cancel_distance_resolution_workflow();
        wizard.open = false;
    }
    ImGui::EndChild();
    ImGui::End();
}

std::string App::open_map_dialog() {
    wchar_t file[MAX_PATH] = {};
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = nullptr;
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrFilter = L"BVE map files\0*.txt;*.csv\0All files\0*.*\0";
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
    if (GetOpenFileNameW(&ofn)) return wide_to_utf8(file);
    return {};
}

std::string App::open_image_dialog() {
    wchar_t file[MAX_PATH] = {};
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = nullptr;
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrFilter = L"Images\0*.png;*.jpg;*.jpeg;*.bmp;*.tif;*.tiff\0All files\0*.*\0";
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
    if (GetOpenFileNameW(&ofn)) return wide_to_utf8(file);
    return {};
}

std::string App::open_editable_list_file_dialog(
    const EditableListSpec& spec,
    const std::string& initial_directory) {
    const bool is_structure =
        std::string_view(spec.row_kind) == "structure.model";
    const wchar_t* pattern = is_structure
        ? L"*.csv;*.b3d;*.x;*.obj;*.fbx;*.dae;*.gltf;*.glb"
        : L"*.wav;*.ogg;*.mp3;*.flac";
    std::wstring resource_label = utf8_to_wide(
        tr(is_structure ? "dialog.filter.model_files"
                        : "dialog.filter.audio_files"));
    resource_label += L" (";
    resource_label += pattern;
    resource_label += L")";

    std::wstring filter;
    const auto append_filter =
        [&](const std::wstring& label, const wchar_t* value) {
            filter += label;
            filter.push_back(L'\0');
            filter += value;
            filter.push_back(L'\0');
        };
    append_filter(resource_label, pattern);
    append_filter(utf8_to_wide(tr("dialog.filter.all_files")), L"*.*");
    filter.push_back(L'\0');

    wchar_t file[MAX_PATH] = {};
    const std::wstring initial_directory_wide =
        utf8_to_wide(initial_directory);
    const std::wstring title =
        utf8_to_wide(tr("dialog.select_resource_file"));
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = nullptr;
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrFilter = filter.c_str();
    ofn.nFilterIndex = 1;
    ofn.lpstrInitialDir = initial_directory_wide.empty()
        ? nullptr : initial_directory_wide.c_str();
    ofn.lpstrTitle = title.c_str();
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
    if (GetOpenFileNameW(&ofn)) return wide_to_utf8(file);
    return {};
}

std::string App::choose_folder_dialog() {
    BROWSEINFOW bi = {};
    bi.lpszTitle = L"Select export folder";
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    PIDLIST_ABSOLUTE pidl = SHBrowseForFolderW(&bi);
    if (!pidl) return {};
    wchar_t path[MAX_PATH] = {};
    SHGetPathFromIDListW(pidl, path);
    CoTaskMemFree(pidl);
    return wide_to_utf8(path);
}

void App::save_history() {
    if (!save_history_entries(history_path_, recent_maps_)) {
        add_log("[WARN] Failed to save history.ini");
    }
}

void App::upsert_recent_map(const std::string& path,
                            const std::optional<BackgroundHistory>& background) {
    std::string stored_path = normalized_storage_path(path);
    std::string key = normalized_path_key(stored_path);
    if (key.empty()) return;

    RecentMapEntry selected;
    selected.path = stored_path;
    std::vector<RecentMapEntry> kept;
    bool found = false;
    for (const auto& entry : recent_maps_) {
        if (normalized_path_key(entry.path) == key) {
            if (!found) {
                selected = entry;
                selected.path = stored_path;
                found = true;
            }
        } else {
            kept.push_back(entry);
        }
    }
    if (background) selected.background = *background;
    kept.insert(kept.begin(), std::move(selected));
    if (kept.size() > k_max_recent_maps) kept.resize(k_max_recent_maps);
    recent_maps_ = std::move(kept);
    save_history();
}

void App::touch_recent_map(const std::string& path) {
    upsert_recent_map(path, std::nullopt);
}

BackgroundHistory App::current_background_history() const {
    BackgroundHistory bg;
    if (bg_image_.path.empty()) return bg;
    bg.has_image = true;
    bg.image_path = normalized_storage_path(bg_image_.path);
    bg.x = bg_x_;
    bg.y = bg_y_;
    bg.width = bg_width_;
    bg.height = bg_height_;
    bg.rotation_deg = bg_rotation_deg_;
    bg.brightness = bg_brightness_;
    return bg;
}

void App::save_current_background_to_history() {
    if (file_path_.empty()) return;
    if (bg_image_.path.empty()) return;
    upsert_recent_map(file_path_, current_background_history());
}

void App::sync_pending_background_values() {
    pending_bg_x_ = bg_x_;
    pending_bg_y_ = bg_y_;
    pending_bg_width_ = bg_width_;
    pending_bg_height_ = bg_height_;
    pending_bg_rotation_deg_ = bg_rotation_deg_;
    pending_bg_brightness_ = bg_brightness_;
}

void App::apply_pending_background_values(bool save_history_entry) {
    bg_x_ = pending_bg_x_;
    bg_y_ = pending_bg_y_;
    bg_width_ = pending_bg_width_;
    bg_height_ = pending_bg_height_;
    bg_rotation_deg_ = pending_bg_rotation_deg_;
    bg_brightness_ = std::clamp(pending_bg_brightness_, 1.0, 200.0);
    pending_bg_brightness_ = bg_brightness_;
    if (!bg_image_.pixels_rgba.empty()) rebuild_background_texture();
    if (save_history_entry) save_current_background_to_history();
}

void App::clear_background_image() {
    bg_image_.release();
    bg_show_ = false;
    sync_pending_background_values();
}

bool App::apply_background_history(const BackgroundHistory& background) {
    if (!background.has_image || background.image_path.empty()) {
        clear_background_image();
        return true;
    }

    std::error_code ec;
    bool exists = std::filesystem::exists(std::filesystem::path(utf8_to_wide(background.image_path)), ec);
    if (ec || !exists) {
        clear_background_image();
        std::string message = "[WARN] Background image not found: " + background.image_path;
        add_log(message);
        std::cerr << message << std::endl;
        return false;
    }

    bg_x_ = background.x;
    bg_y_ = background.y;
    bg_width_ = background.width;
    bg_height_ = background.height;
    bg_rotation_deg_ = background.rotation_deg;
    bg_brightness_ = std::clamp(background.brightness, 1.0, 200.0);
    bg_show_ = settings_.view_2d.show_background_image;
    if (!load_background_image(background.image_path, false)) {
        bg_show_ = false;
        sync_pending_background_values();
        return false;
    }
    if (bg_width_ <= 0.0) bg_width_ = static_cast<double>(bg_image_.width);
    if (bg_height_ <= 0.0) bg_height_ = static_cast<double>(bg_image_.height);
    sync_pending_background_values();
    return true;
}

bool App::load_background_image(const std::string& path, bool reset_parameters) {
    bg_image_.release();
    bg_image_.path = path;
    IWICImagingFactory* factory = nullptr;
    IWICBitmapDecoder* decoder = nullptr;
    IWICBitmapFrameDecode* frame = nullptr;
    IWICFormatConverter* converter = nullptr;
    UINT w = 0;
    UINT h = 0;

    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
    if (FAILED(hr)) goto fail;
    hr = factory->CreateDecoderFromFilename(utf8_to_wide(path).c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnLoad, &decoder);
    if (FAILED(hr)) goto fail;
    hr = decoder->GetFrame(0, &frame);
    if (FAILED(hr)) goto fail;
    hr = factory->CreateFormatConverter(&converter);
    if (FAILED(hr)) goto fail;
    hr = converter->Initialize(frame, GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom);
    if (FAILED(hr)) goto fail;
    converter->GetSize(&w, &h);
    bg_image_.width = static_cast<int>(w);
    bg_image_.height = static_cast<int>(h);
    bg_image_.pixels_rgba.resize(static_cast<size_t>(w) * h * 4);
    hr = converter->CopyPixels(nullptr, w * 4, static_cast<UINT>(bg_image_.pixels_rgba.size()), bg_image_.pixels_rgba.data());
    if (FAILED(hr)) goto fail;

    release_com(converter);
    release_com(frame);
    release_com(decoder);
    release_com(factory);
    if (reset_parameters) {
        bg_width_ = static_cast<double>(w);
        bg_height_ = static_cast<double>(h);
        bg_brightness_ = 100.0;
    }
    return rebuild_background_texture();

fail:
    release_com(converter);
    release_com(frame);
    release_com(decoder);
    release_com(factory);
    bg_image_.release();
    add_log("[ERROR]Failed to load background image: " + path);
    return false;
}

bool App::rebuild_background_texture() {
    if (!device_ || bg_image_.pixels_rgba.empty()) return false;
    release_com(bg_image_.srv);
    std::vector<unsigned char> adjusted = bg_image_.pixels_rgba;
    double mul = std::clamp(bg_brightness_, 1.0, 200.0) / 100.0;
    for (size_t i = 0; i + 3 < adjusted.size(); i += 4) {
        adjusted[i + 0] = static_cast<unsigned char>(std::clamp(adjusted[i + 0] * mul, 0.0, 255.0));
        adjusted[i + 1] = static_cast<unsigned char>(std::clamp(adjusted[i + 1] * mul, 0.0, 255.0));
        adjusted[i + 2] = static_cast<unsigned char>(std::clamp(adjusted[i + 2] * mul, 0.0, 255.0));
    }

    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = static_cast<UINT>(bg_image_.width);
    desc.Height = static_cast<UINT>(bg_image_.height);
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA sub = {};
    sub.pSysMem = adjusted.data();
    sub.SysMemPitch = static_cast<UINT>(bg_image_.width * 4);
    ID3D11Texture2D* texture = nullptr;
    HRESULT hr = device_->CreateTexture2D(&desc, &sub, &texture);
    if (FAILED(hr)) return false;

    D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
    srv_desc.Format = desc.Format;
    srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srv_desc.Texture2D.MipLevels = 1;
    hr = device_->CreateShaderResourceView(texture, &srv_desc, &bg_image_.srv);
    texture->Release();
    if (FAILED(hr)) return false;
    bg_image_.brightness = bg_brightness_;
    return true;
}

void App::export_csv_to_directory(const std::filesystem::path& dir) const {
    std::string base = narrow_path(dir.filename());
    if (base.empty()) base = "kobushi";

    auto write_matrix = [&](const std::filesystem::path& path, const Matrix& m, const std::string& header) {
        std::ofstream out(path, std::ios::binary);
        out << "#" << header << "\n";
        for (size_t r = 0; r < m.rows; ++r) {
            for (size_t c = 0; c < m.cols; ++c) {
                if (c) out << ",";
                out << std::fixed << std::setprecision(6) << m.at(r, c);
            }
            out << "\n";
        }
    };
    write_matrix(dir / utf8_to_wide(base + "_owntrack.csv"), model_.own,
                 "distance,x,y,z,direction,radius,gradient,interpolate_func,cant,center,gauge");
    for (const auto& t : model_.other_tracks) {
        write_matrix(dir / utf8_to_wide(base + "_" + sanitize_filename(t.key) + ".csv"), t.points,
                     "distance,x,y,z,interpolate_func,cant,center,gauge");
    }
}

void App::export_csv() {
    if (!has_model_) return;
    std::string folder = choose_folder_dialog();
    if (folder.empty()) return;
    export_csv_to_directory(std::filesystem::path(utf8_to_wide(folder)));
    add_log("CSV exported: " + folder);
}

void App::setup_initial_dockspace(ImGuiID dockspace_id) {
    if (initial_dockspace_done_) return;
    initial_dockspace_done_ = true;
    if (has_saved_layout_) return;

    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::DockBuilderRemoveNode(dockspace_id);
    ImGui::DockBuilderAddNode(dockspace_id, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(dockspace_id, viewport->Size);

    ImGuiID dock_main = dockspace_id;
    ImGuiID dock_right = ImGui::DockBuilderSplitNode(dock_main, ImGuiDir_Right, 0.23f, nullptr, &dock_main);
    ImGuiID dock_console = ImGui::DockBuilderSplitNode(dock_right, ImGuiDir_Down, 0.32f, nullptr, &dock_right);
    dock_main_id_ = dock_main;
    dock_right_id_ = dock_right;
    ImGui::DockBuilderDockWindow("OtherTracks", dock_right);
    ImGui::DockBuilderDockWindow("StationList", dock_right);
    ImGui::DockBuilderDockWindow("Structures", dock_right);
    ImGui::DockBuilderDockWindow("StructuresPutBetween", dock_right);
    ImGui::DockBuilderDockWindow("StructureModels", dock_right);
    ImGui::DockBuilderDockWindow("OtherTrains", dock_right);
    ImGui::DockBuilderDockWindow("SoundList", dock_right);
    ImGui::DockBuilderDockWindow("Sound3DList", dock_right);
    ImGui::DockBuilderDockWindow("Repeaters", dock_right);
    ImGui::DockBuilderDockWindow("SignalAspects", dock_right);
    ImGui::DockBuilderDockWindow("Signals", dock_right);
    ImGui::DockBuilderDockWindow("Sections", dock_right);
    ImGui::DockBuilderDockWindow("Variables", dock_right);
    ImGui::DockBuilderDockWindow("Beacons", dock_right);
    ImGui::DockBuilderDockWindow("Irregularities", dock_right);
    ImGui::DockBuilderDockWindow("MapSounds", dock_right);
    ImGui::DockBuilderDockWindow("MapSound3D", dock_right);
    ImGui::DockBuilderDockWindow("RollingNoises", dock_right);
    ImGui::DockBuilderDockWindow("FlangeNoises", dock_right);
    ImGui::DockBuilderDockWindow("JointNoises", dock_right);
    ImGui::DockBuilderDockWindow("Backgrounds", dock_right);
    ImGui::DockBuilderDockWindow("Adhesions", dock_right);
    ImGui::DockBuilderDockWindow("CabIlluminance", dock_right);
    ImGui::DockBuilderDockWindow("Fogs", dock_right);
    ImGui::DockBuilderDockWindow("DrawDistances", dock_right);
    ImGui::DockBuilderDockWindow("Console", dock_console);
    ImGui::DockBuilderDockWindow("FileStructureDiagram", dock_main);
    ImGui::DockBuilderDockWindow("TextPreview", dock_main);
    ImGui::DockBuilderDockWindow("ModelPreview3D", dock_main);
    ImGui::DockBuilderDockWindow("ScenePreview3D", dock_main);
    ImGui::DockBuilderDockWindow("Plots", dock_main);
    if (ImGuiDockNode* main_node = ImGui::DockBuilderGetNode(dock_main)) {
        main_node->SelectedTabId = ImHashStr("Plots");
    }
    focus_plots_next_ = true;
    ImGui::DockBuilderFinish(dockspace_id);
}

WindowVisibilitySettings App::current_window_visibility() const {
    WindowVisibilitySettings visibility;
    visibility.show_othertracks_window = show_othertracks_window_;
    visibility.show_station_list_window = show_station_list_window_;
    visibility.show_structures_window = show_structures_window_;
    visibility.show_structures_between_window = show_structures_between_window_;
    visibility.show_structure_models_window = show_structure_models_window_;
    visibility.show_other_trains_window = show_other_trains_window_;
    visibility.show_sound_list_window = show_sound_list_window_;
    visibility.show_sound_3d_list_window = show_sound_3d_list_window_;
    visibility.show_repeaters_window = show_repeaters_window_;
    visibility.show_signal_aspects_window = show_signal_aspects_window_;
    visibility.show_signals_window = show_signals_window_;
    visibility.show_sections_window = show_sections_window_;
    visibility.show_variables_window = show_variables_window_;
    visibility.show_beacons_window = show_beacons_window_;
    visibility.show_irregularities_window = show_irregularities_window_;
    visibility.show_map_sounds_window = show_map_sounds_window_;
    visibility.show_map_sound_3d_window = show_map_sound_3d_window_;
    visibility.show_rolling_noises_window = show_rolling_noises_window_;
    visibility.show_flange_noises_window = show_flange_noises_window_;
    visibility.show_joint_noises_window = show_joint_noises_window_;
    visibility.show_backgrounds_window = show_backgrounds_window_;
    visibility.show_adhesions_window = show_adhesions_window_;
    visibility.show_cab_illuminance_window = show_cab_illuminance_window_;
    visibility.show_fogs_window = show_fogs_window_;
    visibility.show_draw_distances_window = show_draw_distances_window_;
    visibility.show_speed_limits_window = show_speed_limits_window_;
    visibility.show_file_structure_window = show_file_structure_window_;
    visibility.show_console_window = show_console_window_;
    visibility.show_plots_window = show_plots_window_;
    visibility.show_model_preview_window = show_model_preview_window_;
    visibility.show_scene_preview_window = show_scene_preview_window_;
    return visibility;
}

void App::apply_window_visibility_settings(const WindowVisibilitySettings& visibility) {
    show_othertracks_window_ = visibility.show_othertracks_window;
    show_station_list_window_ = visibility.show_station_list_window;
    show_structures_window_ = visibility.show_structures_window;
    show_structures_between_window_ = visibility.show_structures_between_window;
    show_structure_models_window_ = visibility.show_structure_models_window;
    show_other_trains_window_ = visibility.show_other_trains_window;
    show_sound_list_window_ = visibility.show_sound_list_window;
    show_sound_3d_list_window_ = visibility.show_sound_3d_list_window;
    show_repeaters_window_ = visibility.show_repeaters_window;
    show_signal_aspects_window_ = visibility.show_signal_aspects_window;
    show_signals_window_ = visibility.show_signals_window;
    show_sections_window_ = visibility.show_sections_window;
    show_variables_window_ = visibility.show_variables_window;
    show_beacons_window_ = visibility.show_beacons_window;
    show_irregularities_window_ = visibility.show_irregularities_window;
    show_map_sounds_window_ = visibility.show_map_sounds_window;
    show_map_sound_3d_window_ = visibility.show_map_sound_3d_window;
    show_rolling_noises_window_ = visibility.show_rolling_noises_window;
    show_flange_noises_window_ = visibility.show_flange_noises_window;
    show_joint_noises_window_ = visibility.show_joint_noises_window;
    show_backgrounds_window_ = visibility.show_backgrounds_window;
    show_adhesions_window_ = visibility.show_adhesions_window;
    show_cab_illuminance_window_ = visibility.show_cab_illuminance_window;
    show_fogs_window_ = visibility.show_fogs_window;
    show_draw_distances_window_ = visibility.show_draw_distances_window;
    show_speed_limits_window_ = visibility.show_speed_limits_window;
    show_file_structure_window_ = visibility.show_file_structure_window;
    show_console_window_ = visibility.show_console_window;
    show_plots_window_ = visibility.show_plots_window;
    show_model_preview_window_ = visibility.show_model_preview_window;
    show_scene_preview_window_ = visibility.show_scene_preview_window;
}

View2DSettings App::current_view_2d_settings() const {
    View2DSettings view;
    view.show_stations = show_stations_;
    view.show_station_names = show_station_names_;
    view.show_station_mileage = show_station_mileage_;
    view.show_gradient_pos = show_gradient_pos_;
    view.show_gradient_values = show_gradient_values_;
    view.show_curve_values = show_curve_values_;
    view.show_profile_other = show_profile_other_;
    view.show_speedlimits = show_speedlimits_;
    view.show_section_markers = show_section_markers_;
    view.show_irregularity_markers = show_irregularity_markers_;
    view.show_beacon_markers = show_beacon_markers_;
    view.show_pretrain_markers = show_pretrain_markers_;
    view.show_map_sound_markers = show_map_sound_markers_;
    view.show_map_sound_3d_markers = show_map_sound_3d_markers_;
    view.show_rolling_noise_markers = show_rolling_noise_markers_;
    view.show_flange_noise_markers = show_flange_noise_markers_;
    view.show_joint_noise_markers = show_joint_noise_markers_;
    view.show_background_markers = show_background_markers_;
    view.show_adhesion_markers = show_adhesion_markers_;
    view.show_cab_illuminance_markers = show_cab_illuminance_markers_;
    view.show_fog_markers = show_fog_markers_;
    view.show_draw_distance_markers = show_draw_distance_markers_;
    view.show_profile_graph = show_profile_graph_;
    view.show_radius_graph = show_radius_graph_;
    view.show_background_image = bg_show_;
    view.mode = mode_ == Mode::Measure ? 1 : 0;
    view.grid_mode = grid_mode_ == GridMode::Movable ? 1 : (grid_mode_ == GridMode::None ? 2 : 0);
    return view;
}

void App::apply_view_2d_settings(const View2DSettings& settings) {
    show_stations_ = settings.show_stations;
    show_station_names_ = settings.show_station_names;
    show_station_mileage_ = settings.show_station_mileage;
    show_gradient_pos_ = settings.show_gradient_pos;
    show_gradient_values_ = settings.show_gradient_values;
    show_curve_values_ = settings.show_curve_values;
    show_profile_other_ = settings.show_profile_other;
    show_speedlimits_ = settings.show_speedlimits;
    show_section_markers_ = settings.show_section_markers;
    show_irregularity_markers_ = settings.show_irregularity_markers;
    show_beacon_markers_ = settings.show_beacon_markers;
    show_pretrain_markers_ = settings.show_pretrain_markers;
    show_map_sound_markers_ = settings.show_map_sound_markers;
    show_map_sound_3d_markers_ = settings.show_map_sound_3d_markers;
    show_rolling_noise_markers_ = settings.show_rolling_noise_markers;
    show_flange_noise_markers_ = settings.show_flange_noise_markers;
    show_joint_noise_markers_ = settings.show_joint_noise_markers;
    show_background_markers_ = settings.show_background_markers;
    show_adhesion_markers_ = settings.show_adhesion_markers;
    show_cab_illuminance_markers_ = settings.show_cab_illuminance_markers;
    show_fog_markers_ = settings.show_fog_markers;
    show_draw_distance_markers_ = settings.show_draw_distance_markers;
    show_profile_graph_ = settings.show_profile_graph;
    show_radius_graph_ = settings.show_radius_graph;
    bg_show_ = settings.show_background_image;
    mode_ = normalize_view_2d_mode(settings.mode) == 1 ? Mode::Measure : Mode::Pan;
    switch (normalize_grid_mode(settings.grid_mode)) {
        case 1:
            grid_mode_ = GridMode::Movable;
            break;
        case 2:
            grid_mode_ = GridMode::None;
            break;
        default:
            grid_mode_ = GridMode::Fixed;
            break;
    }
}

View3DSettings App::current_view_3d_settings() const {
    View3DSettings view;
    view.show_scene_owntrack_markers = show_scene_owntrack_markers_;
    view.show_scene_current_position_on_plan = show_scene_current_position_on_plan_;
    view.scene_fog_enabled = scene_fog_enabled_;
    view.scene_map_draw_distance_enabled = scene_map_draw_distance_enabled_;
    view.scene_draw_distance_m = scene_draw_distance_m_;
    view.scene_edit_component_size_percent = scene_edit_component_size_percent_;
    view.scene_camera_speed_percent = scene_camera_speed_percent_;
    view.scene_performance_warning_enabled = scene_performance_warning_enabled_;
    view.scene_instance_warning_threshold = scene_instance_warning_threshold_;
    view.scene_instance_critical_warning_threshold =
        scene_instance_critical_warning_threshold_;
    return view;
}

void App::apply_view_3d_settings(const View3DSettings& settings) {
    show_scene_owntrack_markers_ = settings.show_scene_owntrack_markers;
    show_scene_current_position_on_plan_ = settings.show_scene_current_position_on_plan;
    scene_fog_enabled_ = settings.scene_fog_enabled;
    scene_map_draw_distance_enabled_ = settings.scene_map_draw_distance_enabled;
    scene_draw_distance_m_ = clamp_scene_draw_distance(settings.scene_draw_distance_m);
    scene_edit_component_size_percent_ =
        clamp_scene_edit_component_size_percent(settings.scene_edit_component_size_percent);
    scene_camera_speed_percent_ = std::clamp(settings.scene_camera_speed_percent,
                                              k_min_scene_camera_speed_percent,
                                              k_max_scene_camera_speed_percent);
    scene_performance_warning_enabled_ = settings.scene_performance_warning_enabled;
    scene_instance_warning_threshold_ = settings.scene_instance_warning_threshold;
    scene_instance_critical_warning_threshold_ =
        settings.scene_instance_critical_warning_threshold;
    normalize_scene_instance_warning_thresholds(
        scene_instance_warning_threshold_, scene_instance_critical_warning_threshold_);
    apply_scene_draw_distance_to_canvas(scene_draw_distance_m_);
    apply_scene_edit_component_size_to_canvas(scene_edit_component_size_percent_);
    apply_scene_fog_effect_to_canvas(scene_fog_enabled_);
    apply_scene_map_draw_distance_to_canvas(scene_map_draw_distance_enabled_);
    apply_scene_camera_speed_to_canvas(scene_camera_speed_percent_);
    apply_scene_performance_warning_to_canvas(
        scene_performance_warning_enabled_,
        scene_instance_warning_threshold_,
        scene_instance_critical_warning_threshold_);
}

void App::apply_scene_draw_distance_to_canvas(int distance_m) {
    if (scene_preview_canvas_) {
        scene_preview_canvas_->set_scene_window(k_scene_window_back_distance_m,
                                                static_cast<double>(clamp_scene_draw_distance(distance_m)));
    }
}

void App::apply_scene_edit_component_size_to_canvas(int size_percent) {
    if (scene_preview_canvas_) {
        scene_preview_canvas_->set_scene_edit_component_scale(
            static_cast<float>(clamp_scene_edit_component_size_percent(size_percent)) / 100.0f);
    }
}

void App::apply_scene_fog_effect_to_canvas(bool enabled) {
    if (scene_preview_canvas_) scene_preview_canvas_->set_scene_fog_enabled(enabled);
}

void App::apply_scene_map_draw_distance_to_canvas(bool enabled) {
    if (scene_preview_canvas_) scene_preview_canvas_->set_scene_map_draw_distance_enabled(enabled);
}

void App::apply_scene_camera_speed_to_canvas(int percent) {
    if (scene_preview_canvas_) scene_preview_canvas_->set_scene_camera_speed_percent(percent);
}

void App::apply_scene_performance_warning_to_canvas(bool enabled,
                                                    int warning_threshold,
                                                    int critical_warning_threshold) {
    normalize_scene_instance_warning_thresholds(warning_threshold, critical_warning_threshold);
    if (scene_preview_canvas_) {
        scene_preview_canvas_->set_scene_performance_warning(
            enabled,
            static_cast<size_t>(warning_threshold),
            static_cast<size_t>(critical_warning_threshold));
    }
}

bool App::scene_settings_preview_differs_from_dialog_baseline() const {
    return pending_scene_draw_distance_m_ != scene_draw_distance_before_dialog_m_ ||
        pending_scene_edit_component_size_percent_ !=
            scene_edit_component_size_before_dialog_percent_ ||
        pending_scene_camera_speed_percent_ != scene_camera_speed_percent_before_dialog_ ||
        pending_scene_fog_enabled_ != scene_fog_enabled_before_dialog_ ||
        pending_scene_map_draw_distance_enabled_ !=
            scene_map_draw_distance_enabled_before_dialog_ ||
        pending_scene_performance_warning_enabled_ !=
            scene_performance_warning_enabled_before_dialog_ ||
        pending_scene_instance_warning_threshold_ !=
            scene_instance_warning_threshold_before_dialog_ ||
        pending_scene_instance_critical_warning_threshold_ !=
            scene_instance_critical_warning_threshold_before_dialog_;
}

void App::restore_scene_settings_preview() {
    pending_scene_draw_distance_m_ = scene_draw_distance_before_dialog_m_;
    pending_scene_edit_component_size_percent_ =
        scene_edit_component_size_before_dialog_percent_;
    pending_scene_camera_speed_percent_ = scene_camera_speed_percent_before_dialog_;
    pending_scene_fog_enabled_ = scene_fog_enabled_before_dialog_;
    pending_scene_map_draw_distance_enabled_ =
        scene_map_draw_distance_enabled_before_dialog_;
    pending_scene_performance_warning_enabled_ =
        scene_performance_warning_enabled_before_dialog_;
    pending_scene_instance_warning_threshold_ =
        scene_instance_warning_threshold_before_dialog_;
    pending_scene_instance_critical_warning_threshold_ =
        scene_instance_critical_warning_threshold_before_dialog_;
    apply_scene_draw_distance_to_canvas(scene_draw_distance_m_);
    apply_scene_edit_component_size_to_canvas(scene_edit_component_size_percent_);
    apply_scene_camera_speed_to_canvas(scene_camera_speed_percent_);
    apply_scene_fog_effect_to_canvas(scene_fog_enabled_);
    apply_scene_map_draw_distance_to_canvas(scene_map_draw_distance_enabled_);
    apply_scene_performance_warning_to_canvas(
        scene_performance_warning_enabled_,
        scene_instance_warning_threshold_,
        scene_instance_critical_warning_threshold_);
}

void App::save_runtime_settings_if_changed() {
    bool changed = false;
    WindowVisibilitySettings visibility = current_window_visibility();
    if (visibility != last_saved_window_visibility_) {
        settings_.window_visibility = visibility;
        last_saved_window_visibility_ = visibility;
        changed = true;
    }
    View2DSettings view_2d = current_view_2d_settings();
    if (view_2d != last_saved_view_2d_settings_) {
        settings_.view_2d = view_2d;
        last_saved_view_2d_settings_ = view_2d;
        changed = true;
    }
    View3DSettings view_3d = current_view_3d_settings();
    if (view_3d != last_saved_view_3d_settings_) {
        settings_.view_3d = view_3d;
        last_saved_view_3d_settings_ = view_3d;
        changed = true;
    }
    if (changed) save_user_settings(settings_);
}

void App::render_menu() {
    if (!ImGui::BeginMainMenuBar()) return;
    if (ImGui::BeginMenu(tr("menu.file").c_str())) {
        if (ImGui::MenuItem(tr("menu.open").c_str(), "Ctrl+O")) {
            std::string p = open_map_dialog();
            if (!p.empty()) begin_load(p, false, true);
        }
        if (ImGui::BeginMenu(tr("menu.recent_maps").c_str())) {
            if (recent_maps_.empty()) {
                ImGui::MenuItem(tr("menu.none").c_str(), nullptr, false, false);
            } else {
                for (size_t i = 0; i < recent_maps_.size(); ++i) {
                    const RecentMapEntry& entry = recent_maps_[i];
                    std::string label = display_name_from_path(entry.path) + "###recent_map_" + std::to_string(i);
                    if (ImGui::MenuItem(label.c_str(), nullptr, false, !load_state_.running)) {
                        begin_load(entry.path, false, true, entry.background);
                    }
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", entry.path.c_str());
                }
            }
            ImGui::Separator();
            if (ImGui::MenuItem(tr("menu.clear_recent_maps").c_str())) {
                recent_maps_.clear();
                save_history();
            }
            ImGui::EndMenu();
        }
        if (ImGui::MenuItem(tr("menu.reload").c_str(), "F5", false,
                            !load_state_.running && ((has_model_ && !file_path_.empty()) ||
                                          (model_preview_canvas_ && model_preview_canvas_->has_model())))) {
            reload_current_map_and_model_preview();
        }
        if (ImGui::MenuItem(tr("menu.export_csv").c_str(), nullptr, false, has_model_)) export_csv();
        if (ImGui::MenuItem(tr("menu.exit").c_str())) request_exit();
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu(tr("menu.options").c_str())) {
        if (ImGui::MenuItem(tr("menu.ui_settings").c_str())) {
            pending_font_size_ = font_size_;
            font_size_before_dialog_ = font_size_;
            pending_ui_component_size_ = ui_component_size_;
            ui_component_size_before_dialog_ = ui_component_size_;
            pending_theme_color_ = theme_color_;
            theme_color_before_dialog_ = theme_color_;
            popups_.ui_settings = true;
        }
        if (ImGui::BeginMenu(tr("menu.canvas_2d_settings").c_str())) {
            if (ImGui::MenuItem(tr("menu.canvas_element_sizes").c_str())) {
                pending_marker_size_percent_ = marker_size_percent_;
                marker_size_percent_before_dialog_ = marker_size_percent_;
                pending_canvas_line_widths_ = canvas_line_widths_;
                canvas_line_widths_before_dialog_ = canvas_line_widths_;
                popups_.canvas_element_sizes = true;
            }
            ImGui::Separator();
            if (ImGui::MenuItem(tr("menu.plotlimit").c_str(), nullptr, false, has_model_)) {
                plot_min_ = dmin_;
                plot_max_ = dmax_;
                popups_.range = true;
            }
            if (ImGui::MenuItem(tr("menu.controlpoints").c_str(), nullptr, false, has_model_)) {
                cp_start_ = model_.cp_arb[0];
                cp_end_ = model_.cp_arb[1];
                cp_interval_ = model_.cp_arb[2];
                popups_.control_points = true;
            }
            ImGui::EndMenu();
        }
        if (ImGui::MenuItem(tr("menu.canvas_3d_settings").c_str())) {
            pending_scene_draw_distance_m_ = scene_draw_distance_m_;
            scene_draw_distance_before_dialog_m_ = scene_draw_distance_m_;
            pending_scene_edit_component_size_percent_ = scene_edit_component_size_percent_;
            scene_edit_component_size_before_dialog_percent_ = scene_edit_component_size_percent_;
            pending_scene_fog_enabled_ = scene_fog_enabled_;
            scene_fog_enabled_before_dialog_ = scene_fog_enabled_;
            pending_scene_map_draw_distance_enabled_ = scene_map_draw_distance_enabled_;
            scene_map_draw_distance_enabled_before_dialog_ = scene_map_draw_distance_enabled_;
            pending_scene_performance_warning_enabled_ = scene_performance_warning_enabled_;
            scene_performance_warning_enabled_before_dialog_ = scene_performance_warning_enabled_;
            pending_scene_instance_warning_threshold_ = scene_instance_warning_threshold_;
            scene_instance_warning_threshold_before_dialog_ = scene_instance_warning_threshold_;
            pending_scene_instance_critical_warning_threshold_ =
                scene_instance_critical_warning_threshold_;
            scene_instance_critical_warning_threshold_before_dialog_ =
                scene_instance_critical_warning_threshold_;
            popups_.canvas_3d_settings = true;
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu(tr("menu.map_info").c_str())) {
        struct MapInfoMenuEntry {
            const char* label_key;
            bool App::*window_visible;
        };
        static constexpr std::array<MapInfoMenuEntry, 33> k_map_info_menu_entries = {{
            {"aux.station", nullptr},
            {"menu.map_info.station", &App::show_station_list_window_},
            {"aux.scenery", nullptr},
            {"menu.map_info.structures", &App::show_structures_window_},
            {"menu.map_info.structures_put_between", &App::show_structures_between_window_},
            {"menu.map_info.structure_models", &App::show_structure_models_window_},
            {"menu.map_info.repeaters", &App::show_repeaters_window_},
            {"menu.map_info.other_trains", &App::show_other_trains_window_},
            {"aux.track_geometry", nullptr},
            {"menu.map_info.othertracks", &App::show_othertracks_window_},
            {"menu.map_info.irregularities", &App::show_irregularities_window_},
            {"menu.map_info.adhesions", &App::show_adhesions_window_},
            {"aux.signal", nullptr},
            {"menu.map_info.signal_aspects", &App::show_signal_aspects_window_},
            {"menu.map_info.signals", &App::show_signals_window_},
            {"menu.map_info.sections", &App::show_sections_window_},
            {"menu.map_info.beacons", &App::show_beacons_window_},
            {"menu.map_info.speed_limits", &App::show_speed_limits_window_},
            {"aux.sound", nullptr},
            {"menu.map_info.sound_files", &App::show_sound_list_window_},
            {"menu.map_info.sound_3d_files", &App::show_sound_3d_list_window_},
            {"menu.map_info.map_sounds", &App::show_map_sounds_window_},
            {"menu.map_info.map_sound_3d", &App::show_map_sound_3d_window_},
            {"menu.map_info.rolling_noises", &App::show_rolling_noises_window_},
            {"menu.map_info.flange_noises", &App::show_flange_noises_window_},
            {"menu.map_info.joint_noises", &App::show_joint_noises_window_},
            {"aux.effects", nullptr},
            {"menu.map_info.backgrounds", &App::show_backgrounds_window_},
            {"menu.map_info.cab_illuminance", &App::show_cab_illuminance_window_},
            {"menu.map_info.fogs", &App::show_fogs_window_},
            {"menu.map_info.draw_distances", &App::show_draw_distances_window_},
            {"aux.other", nullptr},
            {"menu.map_info.variables", &App::show_variables_window_},
        }};
        bool has_category = false;
        for (const MapInfoMenuEntry& entry : k_map_info_menu_entries) {
            if (!entry.window_visible) {
                if (has_category) ImGui::Separator();
                ImGui::MenuItem(tr(entry.label_key).c_str(), nullptr, false, false);
                has_category = true;
                continue;
            }
            bool& window_visible = this->*entry.window_visible;
            ImGui::MenuItem(tr(entry.label_key).c_str(), nullptr, &window_visible);
        }
        ImGui::EndMenu();
    }
    auto render_aux_info_menu_items = [&]() {
        ImGui::MenuItem(tr("aux.station").c_str(), nullptr, false, false);
        ImGui::MenuItem(tr("chk.station_pos").c_str(), nullptr, &show_stations_);
        ImGui::MenuItem(tr("chk.station_name").c_str(), nullptr, &show_station_names_);
        ImGui::MenuItem(tr("chk.station_mileage").c_str(), nullptr, &show_station_mileage_);
        ImGui::Separator();
        ImGui::MenuItem(tr("aux.track_geometry").c_str(), nullptr, false, false);
        ImGui::MenuItem(tr("chk.gradient_pos").c_str(), nullptr, &show_gradient_pos_);
        ImGui::MenuItem(tr("chk.gradient_val").c_str(), nullptr, &show_gradient_values_);
        ImGui::MenuItem(tr("chk.curve_val").c_str(), nullptr, &show_curve_values_);
        ImGui::MenuItem(tr("chk.irregularity_markers").c_str(), nullptr, &show_irregularity_markers_);
        ImGui::MenuItem(tr("chk.adhesion_markers").c_str(), nullptr, &show_adhesion_markers_);
        ImGui::Separator();
        ImGui::MenuItem(tr("aux.signal").c_str(), nullptr, false, false);
        ImGui::MenuItem(tr("chk.speedlimit").c_str(), nullptr, &show_speedlimits_);
        ImGui::MenuItem(tr("chk.section_markers").c_str(), nullptr,
                        &show_section_markers_);
        ImGui::MenuItem(tr("chk.beacon_markers").c_str(), nullptr, &show_beacon_markers_);
        ImGui::MenuItem(tr("chk.pretrain_markers").c_str(), nullptr, &show_pretrain_markers_);
        ImGui::Separator();
        ImGui::MenuItem(tr("aux.sound").c_str(), nullptr, false, false);
        ImGui::MenuItem(tr("chk.map_sound_markers").c_str(), nullptr, &show_map_sound_markers_);
        ImGui::MenuItem(tr("chk.map_sound_3d_markers").c_str(), nullptr, &show_map_sound_3d_markers_);
        ImGui::MenuItem(tr("chk.rolling_noise_markers").c_str(), nullptr, &show_rolling_noise_markers_);
        ImGui::MenuItem(tr("chk.flange_noise_markers").c_str(), nullptr, &show_flange_noise_markers_);
        ImGui::MenuItem(tr("chk.joint_noise_markers").c_str(), nullptr, &show_joint_noise_markers_);
        ImGui::Separator();
        ImGui::MenuItem(tr("aux.effects").c_str(), nullptr, false, false);
        ImGui::MenuItem(tr("chk.background_markers").c_str(), nullptr, &show_background_markers_);
        ImGui::MenuItem(tr("chk.cab_illuminance_markers").c_str(), nullptr, &show_cab_illuminance_markers_);
        ImGui::MenuItem(tr("chk.fog_markers").c_str(), nullptr, &show_fog_markers_);
        ImGui::MenuItem(tr("chk.draw_distance_markers").c_str(), nullptr, &show_draw_distance_markers_);
        ImGui::Separator();
        ImGui::MenuItem(tr("aux.scene_3d").c_str(), nullptr, false, false);
        if (ImGui::MenuItem(tr("chk.scene_owntrack_markers").c_str(), nullptr, &show_scene_owntrack_markers_)) {
            sync_scene_preview_track_visibility();
        }
        ImGui::MenuItem(tr("chk.scene_current_position_on_plan").c_str(), nullptr,
                        &show_scene_current_position_on_plan_, scene_preview_started_);
        ImGui::Separator();
        ImGui::MenuItem(tr("aux.other").c_str(), nullptr, false, false);
        if (ImGui::MenuItem(tr("frame.file_structure_diagram").c_str(), nullptr,
                            &show_file_structure_window_) && show_file_structure_window_) {
            focus_file_structure_next_ = true;
        }
        const bool can_preview_root = has_model_ && !model_.file_structure.empty() &&
            is_supported_text_preview_file(model_.file_structure.front().absolute_path);
        ImGui::BeginDisabled(!can_preview_root);
        if (ImGui::MenuItem(tr("frame.text_preview").c_str(), nullptr, text_preview_.open)) {
            open_text_preview(model_.file_structure.front().absolute_path, true);
        }
        ImGui::EndDisabled();
        ImGui::MenuItem(tr("chk.console_window").c_str(), nullptr, &show_console_window_);
    };

    if (ImGui::BeginMenu(tr("menu.view_2d").c_str())) {
        if (ImGui::MenuItem(tr("chk.view_2d_window").c_str(), nullptr, &show_plots_window_) && show_plots_window_) {
            focus_plots_next_ = true;
        }
        ImGui::Separator();
        ImGui::MenuItem(tr("frame.chart_visibility").c_str(), nullptr, false, false);
        if (ImGui::MenuItem(tr("chk.gradient_graph").c_str(), nullptr, &show_profile_graph_) && show_profile_graph_) reset_profile_axes_next_ = true;
        if (ImGui::MenuItem(tr("chk.curve_graph").c_str(), nullptr, &show_radius_graph_) && show_radius_graph_) reset_radius_axes_next_ = true;
        ImGui::MenuItem(tr("chk.gradient_pos").c_str(), nullptr, &show_gradient_pos_);
        ImGui::MenuItem(tr("chk.gradient_val").c_str(), nullptr, &show_gradient_values_);
        ImGui::MenuItem(tr("chk.prof_othert").c_str(), nullptr, &show_profile_other_);
        ImGui::Separator();
        ImGui::MenuItem(tr("frame.bgimage").c_str(), nullptr, false, false);
        if (ImGui::MenuItem(tr("button.import_bg").c_str())) {
            std::string p = open_image_dialog();
            if (!p.empty() && load_background_image(p)) {
                bg_show_ = true;
                sync_pending_background_values();
                save_current_background_to_history();
            }
        }
        ImGui::MenuItem(tr("chk.bgimg_show").c_str(), nullptr, &bg_show_);
        if (ImGui::MenuItem(tr("button.adjust_bg").c_str())) {
            sync_pending_background_values();
            popups_.background_adjust = true;
        }
        if (ImGui::MenuItem(tr("button.align_to_station").c_str(), nullptr, false,
                            has_model_ && model_.stations.size() >= 2 && !bg_image_.path.empty())) {
            align_pick1_.reset();
            align_pick2_.reset();
            pick_slot_ = 0;
            popups_.background_align = true;
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu(tr("menu.view_3d").c_str())) {
        ImGui::MenuItem(tr("menu.structure_model_preview").c_str(), nullptr, &show_model_preview_window_);
        if (ImGui::MenuItem(tr("menu.scene_preview").c_str(), nullptr, &show_scene_preview_window_) &&
            show_scene_preview_window_) {
            focus_scene_preview_next_ = true;
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu(tr("frame.aux_info").c_str())) {
        render_aux_info_menu_items();
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu(tr("menu.lang").c_str())) {
        auto set_language = [&](Language lang) {
            if (lang_ == lang) return;
            lang_ = lang;
            settings_.language = lang_;
            settings_.window_visibility = current_window_visibility();
            last_saved_window_visibility_ = settings_.window_visibility;
            settings_.view_2d = current_view_2d_settings();
            last_saved_view_2d_settings_ = settings_.view_2d;
            settings_.view_3d = current_view_3d_settings();
            last_saved_view_3d_settings_ = settings_.view_3d;
            save_user_settings(settings_);
        };
        if (ImGui::MenuItem("简体中文", nullptr, lang_ == Language::Zh)) set_language(Language::Zh);
        if (ImGui::MenuItem("English", nullptr, lang_ == Language::En)) set_language(Language::En);
        if (ImGui::MenuItem("日本語", nullptr, lang_ == Language::Ja)) set_language(Language::Ja);
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu(tr("menu.help").c_str())) {
        if (ImGui::MenuItem(tr("menu.online_docs").c_str())) {
            ShellExecuteW(nullptr, L"open", L"https://github.com/NewSapporoNingyo/komapedit", nullptr, nullptr, SW_SHOWNORMAL);
        }
        if (ImGui::MenuItem(tr("menu.report_bugs").c_str())) {
            ShellExecuteW(nullptr, L"open", L"https://github.com/NewSapporoNingyo/komapedit/issues/new", nullptr, nullptr, SW_SHOWNORMAL);
        }
        if (ImGui::MenuItem(tr("menu.about").c_str())) popups_.about = true;
        ImGui::EndMenu();
    }
    ImGui::EndMainMenuBar();
}

void App::render_toolbar() {
    ImGuiStyle& style = ImGui::GetStyle();
    const ImVec4 toolbar_bg = main_bar_background_color();

    const float button_height = ImGui::GetFrameHeight();
    const float toolbar_padding_y = button_height * 0.25f;
    const float toolbar_height = button_height + toolbar_padding_y * 2.0f;
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings;
    ImGui::PushStyleColor(ImGuiCol_WindowBg, toolbar_bg);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(style.WindowPadding.x, toolbar_padding_y));
    bool visible = ImGui::BeginViewportSideBar("##MainToolbar", ImGui::GetMainViewport(), ImGuiDir_Up, toolbar_height, flags);
    if (visible) {
        auto render_section_separator = [&style]() {
            ImGui::SameLine(0.0f, style.ItemSpacing.x);
            ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
            ImGui::SameLine(0.0f, style.ItemSpacing.x);
        };

        if (ImGui::Button(tr("button.open").c_str())) {
            std::string p = open_map_dialog();
            if (!p.empty()) begin_load(p, false, true);
        }
        ImGui::SameLine();

        const bool can_reload = !load_state_.running && ((has_model_ && !file_path_.empty()) ||
                                             (model_preview_canvas_ && model_preview_canvas_->has_model()));
        ImGui::BeginDisabled(!can_reload);
        if (ImGui::Button(tr("button.reload").c_str())) reload_current_map_and_model_preview();
        ImGui::EndDisabled();

        ImGui::SameLine();
        const bool can_reload_geometry = !load_state_.running && has_model_ && !file_path_.empty();
        ImGui::BeginDisabled(!can_reload_geometry);
        if (ImGui::Button(tr("button.reload_geometry").c_str())) reload_current_map_geometry();
        ImGui::EndDisabled();

        render_section_separator();
        bool requested_edit_mode = edit_mode_enabled_;
        if (ImGui::Checkbox(tr("chk.edit_mode").c_str(), &requested_edit_mode)) {
            set_edit_mode_enabled(requested_edit_mode);
        }

        ImGui::SameLine();
        ImGui::BeginDisabled(!edit_actions_available());
        if (ImGui::Button(tr("button.add_map_element").c_str())) {
            new_element_wizard_.open = true;
            new_element_wizard_.target_file_path.clear();
            new_element_wizard_.target_file_candidates.clear();
            new_element_wizard_.target_candidates_built = false;
            new_element_wizard_.built_template = -1;
            new_element_wizard_.built_target_file.clear();
        }
        ImGui::EndDisabled();

        ImGui::SameLine();
        ImGui::BeginDisabled(!edit_actions_available() || !has_pending_edits() ||
                             has_unapplied_editable_list_drafts());
        if (ImGui::Button(tr("button.save").c_str())) save_pending_edits();
        ImGui::EndDisabled();

        ImGui::SameLine();
        ImGui::BeginDisabled(!edit_actions_available() || !has_unsaved_edit_state());
        if (ImGui::Button(tr("button.revert").c_str())) {
            popups_.revert_all_edits_confirm = true;
        }
        ImGui::EndDisabled();

        render_section_separator();
        render_station_jump_combo();
        ImGui::SameLine(0.0f, style.ItemSpacing.x);
        render_distance_jump_control();
    }
    ImGui::End();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
}

void App::render_status_bar() {
    constexpr float k_font_scale = 0.80f;
    constexpr float k_height_scale = 1.20f;
    const float status_font_size = ImGui::GetFontSize() * k_font_scale;
    const float status_bar_height = status_font_size * k_height_scale;
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoScrollbar |
                                   ImGuiWindowFlags_NoSavedSettings |
                                   ImGuiWindowFlags_NoNavInputs;

    ImGui::PushStyleColor(ImGuiCol_WindowBg, main_bar_background_color());
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowMinSize, ImVec2(0.0f, 0.0f));
    const bool visible = ImGui::BeginViewportSideBar(
        "##MainStatusBar", ImGui::GetMainViewport(), ImGuiDir_Down, status_bar_height, flags);
    if (visible) {
        ImGui::SetWindowFontScale(k_font_scale);

        const int error_count = error_count_.load(std::memory_order_relaxed);
        const int warning_count = warn_count_.load(std::memory_order_relaxed);

        std::array<char, 32> error_text{};
        std::array<char, 32> warning_text{};
        std::snprintf(error_text.data(), error_text.size(), "Err %d", error_count);
        std::snprintf(warning_text.data(), warning_text.size(), "Warn %d", warning_count);
        const ImVec2 error_text_size = ImGui::CalcTextSize(error_text.data());
        const ImVec2 warning_text_size = ImGui::CalcTextSize(warning_text.data());
        const ImGuiStyle& style = ImGui::GetStyle();
        const float horizontal_padding = style.ItemSpacing.x;
        const float count_spacing = style.ItemInnerSpacing.x;
        const ImVec2 count_region_size(error_text_size.x + count_spacing + warning_text_size.x +
                                           horizontal_padding * 2.0f,
                                       status_bar_height);
        const ImVec2 count_region_min = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton("##StatusDiagnosticsButton", count_region_size);
        const bool count_region_hovered = ImGui::IsItemHovered();
        const bool open_diagnostics = ImGui::IsItemClicked();

        const ImVec4 count_background = darkened_theme_color(theme_color_);
        ImDrawList* draw_list = ImGui::GetWindowDrawList();
        draw_list->AddRectFilled(count_region_min,
                                 ImVec2(count_region_min.x + count_region_size.x,
                                        count_region_min.y + count_region_size.y),
                                 ImGui::GetColorU32(count_background));
        const float text_y = count_region_min.y +
                             (status_bar_height - error_text_size.y) * 0.5f;
        const float error_text_x = count_region_min.x + horizontal_padding;
        const float warning_text_x = error_text_x + error_text_size.x + count_spacing;
        draw_list->AddText(ImVec2(error_text_x, text_y),
                           ImGui::GetColorU32(log_severity_color(LogSeverity::Error)),
                           error_text.data());
        draw_list->AddText(ImVec2(warning_text_x, text_y),
                           ImGui::GetColorU32(log_severity_color(LogSeverity::Warning)),
                           warning_text.data());
        if (count_region_hovered) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        if (open_diagnostics) ImGui::OpenPopup("##StatusDiagnosticsPopup");

        ImGui::SameLine(0.0f, horizontal_padding);
        ImGui::SetCursorPosY((status_bar_height - status_font_size) * 0.5f);
        ImGui::TextUnformatted(tr(program_status_key_).c_str());
        if (!program_status_elapsed_suffix_.empty()) {
            ImGui::SameLine(0.0f, 0.0f);
            ImGui::TextUnformatted(program_status_elapsed_suffix_.c_str());
        }

        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        const ImVec2 popup_size(
            std::max(280.0f * dpi_scale_, std::min(720.0f * dpi_scale_, viewport->WorkSize.x * 0.60f)),
            std::max(160.0f * dpi_scale_, std::min(360.0f * dpi_scale_, viewport->WorkSize.y * 0.40f)));
        ImGui::SetNextWindowSize(popup_size, ImGuiCond_Appearing);
        if (ImGui::BeginPopup("##StatusDiagnosticsPopup")) {
            ImGui::SetWindowFontScale(1.0f);
            ImGui::TextUnformatted(tr("frame.errors_warnings").c_str());
            ImGui::Separator();
            ImGui::BeginChild("##StatusDiagnosticsList", ImVec2(0.0f, 0.0f), false,
                              ImGuiWindowFlags_HorizontalScrollbar);
            bool has_diagnostics = false;
            {
                std::lock_guard<std::mutex> lock(log_mutex_);
                for (const LogLine& line : logs_) {
                    if (line.severity == LogSeverity::Info) continue;
                    has_diagnostics = true;
                    ImGui::TextColored(log_severity_color(line.severity), "%s", line.text.c_str());
                }
            }
            if (!has_diagnostics) {
                ImGui::TextDisabled("%s", tr("status.no_errors_warnings").c_str());
            }
            ImGui::EndChild();
            ImGui::EndPopup();
        }
    }
    ImGui::End();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();
}

void App::render_station_jump_combo() {
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(tr("label.station_jump").c_str());
    ImGui::SameLine();

    const bool can_jump = has_model_ && !model_.stations.empty();
    std::string preview;
    const char* preview_text = "-";
    if (can_jump) {
        station_jump_index_ = std::clamp(station_jump_index_, 0, static_cast<int>(model_.stations.size()) - 1);
        preview = model_.stations[station_jump_index_].key + ", " + model_.stations[station_jump_index_].name;
        preview_text = preview.c_str();
    }

    ImGui::BeginDisabled(!can_jump);
    const ImGuiStyle& style = ImGui::GetStyle();
    const float reserved_width =
        distance_jump_control_width(tr("label.distance_jump").c_str(), tr("button.jump").c_str()) + style.ItemSpacing.x;
    const float available_width = ImGui::GetContentRegionAvail().x;
    float combo_width = std::min(360.0f, available_width);
    if (available_width > reserved_width + 120.0f) {
        combo_width = std::min(360.0f, available_width - reserved_width);
    } else {
        combo_width = std::min(240.0f, std::max(80.0f, available_width * 0.5f));
    }
    ImGui::SetNextItemWidth(std::max(1.0f, combo_width));
    if (ImGui::BeginCombo("##toolbar_station", preview_text)) {
        for (int i = 0; i < static_cast<int>(model_.stations.size()); ++i) {
            std::string label = model_.stations[i].key + ", " + model_.stations[i].name;
            const bool selected = i == station_jump_index_;
            if (ImGui::Selectable(label.c_str(), selected)) {
                station_jump_index_ = i;
                const double distance = model_.stations[i].distance;
                jump_to_distance(distance);
            }
            if (selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    ImGui::EndDisabled();
}

void App::render_distance_jump_control() {
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(tr("label.distance_jump").c_str());
    ImGui::SameLine();

    const bool has_track = has_model_ && !model_.own.empty();
    double distance = 0.0;

    ImGui::BeginDisabled(!has_track);
    ImGui::SetNextItemWidth(distance_jump_input_width());
    const bool enter_pressed = ImGui::InputText(
        "##toolbar_distance_jump", distance_jump_input_, sizeof(distance_jump_input_),
        ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CallbackCharFilter,
        distance_jump_input_filter);
    ImGui::EndDisabled();

    const bool valid_distance = parse_distance_jump_input(distance_jump_input_, distance);
    ImGui::SameLine();
    const bool can_jump = has_track && valid_distance;
    ImGui::BeginDisabled(!can_jump);
    if (ImGui::Button(tr("button.jump").c_str()) || (enter_pressed && can_jump)) {
        jump_to_distance(distance);
    }
    ImGui::EndDisabled();
}

void App::render_console() {
    if (!show_console_window_) return;
    std::string title = tr("frame.console") + "###Console";
    if (!ImGui::Begin(title.c_str(), &show_console_window_)) {
        ImGui::End();
        return;
    }
    if (ImGui::Button(tr("button.clear").c_str())) {
        std::lock_guard<std::mutex> lock(log_mutex_);
        logs_.clear();
        error_count_.store(0, std::memory_order_relaxed);
        warn_count_.store(0, std::memory_order_relaxed);
    }
    ImGui::SameLine();
    if (ImGui::Button(tr("button.copy").c_str())) {
        std::string console_text;
        {
            std::lock_guard<std::mutex> lock(log_mutex_);
            size_t text_size = logs_.empty() ? 0 : logs_.size() - 1;
            for (const auto& line : logs_) text_size += line.text.size();
            console_text.reserve(text_size);
            for (size_t i = 0; i < logs_.size(); ++i) {
                if (i > 0) console_text.push_back('\n');
                console_text.append(logs_[i].text);
            }
        }
        ImGui::SetClipboardText(console_text.c_str());
    }
    ImGui::SameLine();
    ImGui::TextColored(log_severity_color(LogSeverity::Error), "E %d",
                       error_count_.load(std::memory_order_relaxed));
    ImGui::SameLine();
    ImGui::TextColored(log_severity_color(LogSeverity::Warning), "W %d",
                       warn_count_.load(std::memory_order_relaxed));
    ImGui::Separator();
    ImGui::BeginChild("console_scroll", ImVec2(0, 0), true, ImGuiWindowFlags_HorizontalScrollbar);
    const bool was_at_bottom = ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 4.0f;
    const touch_input::TouchFrame& touch = touch_input::current_frame();
    ImGuiWindow* console_window = ImGui::GetCurrentWindow();
    ImRect console_rect(console_window->Pos, ImVec2(console_window->Pos.x + console_window->Size.x,
                                                   console_window->Pos.y + console_window->Size.y));
    const bool touch_vertical_scroll =
        touch.single_drag && touch.active_count == 1 && std::abs(touch.single_drag_delta.y) > 0.01f &&
        (console_rect.Contains(touch.single_start_pos) || console_rect.Contains(touch.single_pos));
    std::lock_guard<std::mutex> lock(log_mutex_);
    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(logs_.size()));
    while (clipper.Step()) {
        for (int index = clipper.DisplayStart; index < clipper.DisplayEnd; ++index) {
            const LogLine& line = logs_[static_cast<size_t>(index)];
            ImGui::TextColored(
                log_severity_color(line.severity), "%s", line.text.c_str());
        }
    }
    if (was_at_bottom && !touch_vertical_scroll) ImGui::SetScrollHereY(1.0f);
    ImGui::EndChild();
    ImGui::End();
}

void App::render_popups() {
    auto sync_runtime_settings_before_save = [&]() {
        settings_.window_visibility = current_window_visibility();
        last_saved_window_visibility_ = settings_.window_visibility;
        settings_.view_2d = current_view_2d_settings();
        last_saved_view_2d_settings_ = settings_.view_2d;
        settings_.view_3d = current_view_3d_settings();
        last_saved_view_3d_settings_ = settings_.view_3d;
    };

    if (popups_.edit_mode_warning) {
        ImGui::OpenPopup(tr("dialog.edit_mode_warning_title").c_str());
        popups_.edit_mode_warning = false;
    }
    ImGui::SetNextWindowSize(ImVec2(560.0f, 0.0f), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal(tr("dialog.edit_mode_warning_title").c_str(), nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 520.0f);
        ImGui::TextUnformatted(tr("dialog.edit_mode_warning_message").c_str());
        ImGui::PopTextWrapPos();
        ImGui::Checkbox(tr("chk.edit_mode_warning_dont_show").c_str(),
                        &edit_mode_warning_dont_show_);
        ImGui::Separator();
        if (ImGui::Button(tr("button.ok").c_str())) {
            if (edit_mode_warning_dont_show_) {
                settings_.edit_mode_warning_suppressed = true;
            }
            apply_edit_mode_enabled(true);
            edit_mode_warning_dont_show_ = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button(tr("button.cancel").c_str())) {
            edit_mode_warning_dont_show_ = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    const bool repeater_begin0 = inspector_.open && inspector_.row_kind == "repeater";
    const char* begin0_title_key = repeater_begin0
        ? "dialog.repeater_begin0_convert_title"
        : "dialog.structure_put0_convert_title";
    const char* begin0_message_key = repeater_begin0
        ? "dialog.repeater_begin0_convert_message"
        : "dialog.structure_put0_convert_message";
    if (inspector_.open && inspector_.put0_prompt_requested) {
        ImGui::OpenPopup(tr(begin0_title_key).c_str());
        inspector_.put0_prompt_requested = false;
    }
    if (ImGui::BeginPopupModal(tr(begin0_title_key).c_str(), nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 480.0f);
        ImGui::TextUnformatted(tr(begin0_message_key).c_str());
        ImGui::PopTextWrapPos();
        ImGui::Separator();
        if (ImGui::Button(tr("button.ok").c_str())) {
            enable_inspector_put0_conversion();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button(tr("button.cancel").c_str())) {
            inspector_.put0_conversion_draft = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    if (inspector_.open && inspector_.signal_full_form_prompt_requested) {
        ImGui::OpenPopup(tr("dialog.signal_full_form_convert_title").c_str());
        inspector_.signal_full_form_prompt_requested = false;
    }
    if (ImGui::BeginPopupModal(tr("dialog.signal_full_form_convert_title").c_str(), nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 480.0f);
        ImGui::TextUnformatted(tr("dialog.signal_full_form_convert_message").c_str());
        ImGui::PopTextWrapPos();
        ImGui::Separator();
        if (ImGui::Button(tr("button.ok").c_str())) {
            inspector_.signal_full_form_conversion_draft = true;
            if (MapElementEditFieldState* field = find_inspector_field(
                    inspector_, inspector_.pending_signal_full_form_field)) {
                set_edit_field_buffer(*field, inspector_.pending_signal_full_form_value);
            }
            inspector_.pending_signal_full_form_field.clear();
            inspector_.pending_signal_full_form_value.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button(tr("button.cancel").c_str())) {
            inspector_.pending_signal_full_form_field.clear();
            inspector_.pending_signal_full_form_value.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    if (inspector_.open && inspector_.z_rebase_prompt_requested) {
        ImGui::OpenPopup(tr("dialog.structure_z_rebase_title").c_str());
        inspector_.z_rebase_prompt_requested = false;
    }
    bool apply_z_rebase_after_popup = false;
    if (ImGui::BeginPopupModal(tr("dialog.structure_z_rebase_title").c_str(), nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 480.0f);
        ImGui::TextUnformatted(tr("dialog.structure_z_rebase_message").c_str());
        ImGui::PopTextWrapPos();
        ImGui::Separator();
        if (ImGui::Button(tr("button.ok").c_str())) {
            MapElementEditFieldState* distance_field = find_inspector_field(inspector_, "distance");
            MapElementEditFieldState* z_field = find_inspector_field(inspector_, "z");
            double distance = 0.0;
            double z = 0.0;
            if (!distance_field || !z_field ||
                !parse_gui_edit_number(edit_field_buffer_text(*distance_field), &distance) ||
                !parse_gui_edit_number(edit_field_buffer_text(*z_field), &z)) {
                set_program_status("status.edit.invalid_number");
                ImGui::CloseCurrentPopup();
            } else {
                z = truncate_gui_thousandths(z);
                const double distance_part = std::trunc(z);
                set_edit_field_buffer(*distance_field,
                                      format_double(distance + distance_part, 12));
                set_edit_field_buffer(*z_field,
                                      format_gui_transform_number(z - distance_part));
                ImGui::CloseCurrentPopup();
                apply_z_rebase_after_popup = true;
            }
        }
        ImGui::SameLine();
        if (ImGui::Button(tr("button.cancel").c_str())) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    if (apply_z_rebase_after_popup) {
        apply_inspector_changes();
        return;
    }

    bool cancel_distance_resolution = false;
    if (distance_resolution_workflow_.phase == DistanceResolutionPhase::ConfirmAction &&
        distance_resolution_workflow_.popup_requested) {
        ImGui::OpenPopup(tr("dialog.distance_resolution_title").c_str());
        distance_resolution_workflow_.popup_requested = false;
    }
    if (ImGui::BeginPopupModal(tr("dialog.distance_resolution_title").c_str(), nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 520.0f);
        ImGui::TextUnformatted(tr("dialog.distance_resolution_message").c_str());
        if (!distance_resolution_workflow_.request.source_file.empty()) {
            ImGui::TextDisabled("%s", distance_resolution_workflow_.request.source_file.c_str());
        }
        ImGui::PopTextWrapPos();
        ImGui::Separator();
        if (distance_resolution_workflow_.request.can_confirm_reuse) {
            if (ImGui::Button(tr("button.confirm_reuse").c_str())) {
                DistanceResolutionChoice choice;
                choice.confirm_environment_mismatch = true;
                apply_distance_resolution_choice(choice);
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
        }
        ImGui::BeginDisabled(
            distance_resolution_workflow_.request.allowed_boundaries.empty());
        if (ImGui::Button(tr("button.manual_select").c_str())) {
            distance_resolution_workflow_.phase = DistanceResolutionPhase::SelectBoundary;
            open_text_preview_for_distance_resolution(
                distance_resolution_workflow_.request);
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button(tr("button.cancel").c_str())) {
            cancel_distance_resolution = true;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    if (distance_resolution_workflow_.phase == DistanceResolutionPhase::EditExpression &&
        distance_resolution_workflow_.popup_requested) {
        ImGui::OpenPopup(tr("dialog.distance_expression_title").c_str());
        distance_resolution_workflow_.popup_requested = false;
    }
    if (ImGui::BeginPopupModal(tr("dialog.distance_expression_title").c_str(), nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        std::string message;
        if (distance_resolution_workflow_.request.variable_name.empty()) {
            message = tr("dialog.distance_expression_message");
        } else {
            message = tr("dialog.distance_variable_message");
            const std::string placeholder = "{variable}";
            size_t placeholder_at = message.find(placeholder);
            if (placeholder_at != std::string::npos) {
                message.replace(placeholder_at, placeholder.size(),
                                distance_resolution_workflow_.request.variable_name);
            }
        }
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 520.0f);
        ImGui::TextUnformatted(message.c_str());
        ImGui::PopTextWrapPos();
        ImGui::SetNextItemWidth(520.0f);
        ImGui::InputText("##distance_source_expression",
                         distance_resolution_workflow_.expression_buffer.data(),
                         distance_resolution_workflow_.expression_buffer.size());
        ImGui::Separator();
        ImGui::BeginDisabled(distance_resolution_workflow_.expression_buffer[0] == '\0');
        if (ImGui::Button(tr("button.apply").c_str())) {
            DistanceResolutionChoice choice;
            choice.distance_expression = distance_resolution_workflow_.expression_buffer.data();
            apply_distance_resolution_choice(choice);
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button(tr("button.cancel").c_str())) {
            cancel_distance_resolution = true;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    if (distance_resolution_workflow_.phase == DistanceResolutionPhase::ConfirmAction &&
        !distance_resolution_workflow_.popup_requested &&
        !ImGui::IsPopupOpen(tr("dialog.distance_resolution_title").c_str())) {
        cancel_distance_resolution = true;
    }
    if (distance_resolution_workflow_.phase == DistanceResolutionPhase::EditExpression &&
        !distance_resolution_workflow_.popup_requested &&
        !ImGui::IsPopupOpen(tr("dialog.distance_expression_title").c_str())) {
        cancel_distance_resolution = true;
    }
    if (cancel_distance_resolution) cancel_distance_resolution_workflow();

    if (popups_.ui_settings) {
        ImGui::OpenPopup(tr("dialog.ui_settings").c_str());
        popups_.ui_settings = false;
    }
    bool ui_settings_popup_open = true;
    if (ImGui::BeginPopupModal(tr("dialog.ui_settings").c_str(), &ui_settings_popup_open, ImGuiWindowFlags_AlwaysAutoResize)) {
        auto apply_pending_ui_settings = [&]() {
            apply_ui_settings(pending_font_size_, pending_ui_component_size_, pending_theme_color_, dpi_scale_, viewports_enabled_);
        };
        ImGui::SetNextItemWidth(260.0f);
        if (ImGui::SliderFloat(tr("label.font_size").c_str(), &pending_font_size_, k_min_font_size, k_max_font_size, "%.0f px", ImGuiSliderFlags_AlwaysClamp)) {
            pending_font_size_ = clamp_font_size(pending_font_size_);
            apply_pending_ui_settings();
        }
        ImGui::SetNextItemWidth(260.0f);
        if (ImGui::SliderFloat(tr("label.ui_component_size").c_str(), &pending_ui_component_size_, k_min_ui_component_size, k_max_ui_component_size, "%.0f%%", ImGuiSliderFlags_AlwaysClamp)) {
            pending_ui_component_size_ = clamp_ui_component_size(pending_ui_component_size_);
            apply_pending_ui_settings();
        }
        ImGui::Separator();
        const ImGuiColorEditFlags color_flags = ImGuiColorEditFlags_NoAlpha
            | ImGuiColorEditFlags_DisplayRGB
            | ImGuiColorEditFlags_InputRGB
            | ImGuiColorEditFlags_Uint8
            | ImGuiColorEditFlags_PickerHueBar;
        std::string theme_hex = theme_color_to_string(pending_theme_color_);
        ImGui::TextUnformatted(tr("label.ui_theme_color").c_str());
        ImGui::SameLine();
        const float preview_size = ImGui::GetFrameHeight();
        if (ImGui::ColorButton("##theme_color_preview", pending_theme_color_, ImGuiColorEditFlags_NoAlpha, ImVec2(preview_size, preview_size))) {
            ImGui::OpenPopup("theme_color_popup");
        }
        ImGui::SameLine();
        ImGui::Text("#%s", theme_hex.c_str());
        const auto& palette = ui_theme_palette();
        if (ImGui::BeginPopup("theme_color_popup")) {
            ImGui::SetNextItemWidth(260.0f);
            if (ImGui::ColorPicker3("##theme_color_picker", &pending_theme_color_.x, color_flags)) {
                pending_theme_color_ = clamp_theme_color(pending_theme_color_);
                apply_pending_ui_settings();
            }
            ImGui::Separator();
            const float swatch_size = ImGui::GetFrameHeight();
            for (size_t i = 0; i < palette.size(); ++i) {
                if (i > 0 && i % 6 != 0) ImGui::SameLine();
                std::string id = "##theme_palette_" + std::to_string(i);
                if (ImGui::ColorButton(id.c_str(), palette[i], ImGuiColorEditFlags_NoAlpha, ImVec2(swatch_size, swatch_size))) {
                    pending_theme_color_ = clamp_theme_color(palette[i]);
                    apply_pending_ui_settings();
                }
            }
            ImGui::EndPopup();
        }
        if (ImGui::Button(tr("button.ok").c_str())) {
            font_size_ = clamp_font_size(pending_font_size_);
            ui_component_size_ = clamp_ui_component_size(pending_ui_component_size_);
            theme_color_ = clamp_theme_color(pending_theme_color_);
            settings_.font_size = font_size_;
            settings_.ui_component_size = ui_component_size_;
            settings_.theme_color = theme_color_;
            sync_runtime_settings_before_save();
            save_user_settings(settings_);
            apply_ui_settings(font_size_, ui_component_size_, theme_color_, dpi_scale_, viewports_enabled_);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button(tr("button.cancel").c_str())) {
            pending_font_size_ = font_size_before_dialog_;
            pending_ui_component_size_ = ui_component_size_before_dialog_;
            pending_theme_color_ = theme_color_before_dialog_;
            apply_ui_settings(font_size_, ui_component_size_, theme_color_, dpi_scale_, viewports_enabled_);
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    if (!ui_settings_popup_open) {
        pending_font_size_ = font_size_;
        pending_ui_component_size_ = ui_component_size_;
        pending_theme_color_ = theme_color_;
        apply_ui_settings(font_size_, ui_component_size_, theme_color_, dpi_scale_, viewports_enabled_);
    }

    if (popups_.canvas_element_sizes) {
        ImGui::OpenPopup(tr("dialog.canvas_element_sizes").c_str());
        popups_.canvas_element_sizes = false;
    }
    bool canvas_element_sizes_popup_open = true;
    if (ImGui::BeginPopupModal(tr("dialog.canvas_element_sizes").c_str(), &canvas_element_sizes_popup_open, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::SetNextItemWidth(260.0f);
        int marker_size_steps = static_cast<int>(clamp_marker_size_percent(pending_marker_size_percent_) / k_marker_size_percent_step);
        if (ImGui::SliderInt(tr("label.station_marker_size").c_str(),
                             &marker_size_steps,
                             static_cast<int>(k_min_marker_size_percent) / k_marker_size_percent_step,
                             static_cast<int>(k_max_marker_size_percent) / k_marker_size_percent_step,
                             "%d0%%",
                             ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_NoInput)) {
            pending_marker_size_percent_ = clamp_marker_size_percent(static_cast<float>(marker_size_steps * k_marker_size_percent_step));
            marker_size_percent_ = pending_marker_size_percent_;
        }
        auto line_width_slider = [&](const char* label_key, float* value, float fallback) {
            ImGui::SetNextItemWidth(260.0f);
            int width_steps = static_cast<int>(std::round(clamp_canvas_line_width(*value, fallback) /
                                                          k_canvas_line_width_step_px));
            const int min_steps = static_cast<int>(std::round(k_min_canvas_line_width_px / k_canvas_line_width_step_px));
            const int max_steps = static_cast<int>(std::round(k_max_canvas_line_width_px / k_canvas_line_width_step_px));
            std::string slider_id = std::string("##") + label_key;
            if (ImGui::SliderInt(slider_id.c_str(),
                                 &width_steps,
                                 min_steps,
                                 max_steps,
                                 "",
                                 ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_NoInput)) {
                *value = clamp_canvas_line_width(static_cast<float>(width_steps) * k_canvas_line_width_step_px,
                                                 fallback);
                pending_canvas_line_widths_ = clamp_canvas_line_widths(pending_canvas_line_widths_);
                canvas_line_widths_ = pending_canvas_line_widths_;
            }
            ImVec2 slider_min = ImGui::GetItemRectMin();
            ImVec2 slider_max = ImGui::GetItemRectMax();
            std::string value_text = format_double(
                static_cast<double>(clamp_canvas_line_width(static_cast<float>(width_steps) *
                                                            k_canvas_line_width_step_px,
                                                            fallback)),
                1) + " px";
            ImVec2 value_size = ImGui::CalcTextSize(value_text.c_str());
            ImGui::GetWindowDrawList()->AddText(
                ImVec2((slider_min.x + slider_max.x - value_size.x) * 0.5f,
                       (slider_min.y + slider_max.y - value_size.y) * 0.5f),
                ImGui::GetColorU32(ImGuiCol_Text),
                value_text.c_str());
            ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
            ImGui::TextUnformatted(tr(label_key).c_str());
        };
        line_width_slider("label.own_track_line_width",
                          &pending_canvas_line_widths_.own_track_px,
                          k_default_own_track_line_width_px);
        line_width_slider("label.other_track_line_width",
                          &pending_canvas_line_widths_.other_track_px,
                          k_default_other_track_line_width_px);
        line_width_slider("label.chart_marker_line_width",
                          &pending_canvas_line_widths_.chart_marker_px,
                          k_default_chart_marker_line_width_px);
        line_width_slider("label.background_grid_line_width",
                          &pending_canvas_line_widths_.background_grid_px,
                          k_default_background_grid_line_width_px);
        if (ImGui::Button(tr("button.ok").c_str())) {
            marker_size_percent_ = clamp_marker_size_percent(pending_marker_size_percent_);
            marker_size_percent_before_dialog_ = marker_size_percent_;
            canvas_line_widths_ = clamp_canvas_line_widths(pending_canvas_line_widths_);
            pending_canvas_line_widths_ = canvas_line_widths_;
            canvas_line_widths_before_dialog_ = canvas_line_widths_;
            settings_.marker_size_percent = marker_size_percent_;
            settings_.canvas_line_widths = canvas_line_widths_;
            sync_runtime_settings_before_save();
            save_user_settings(settings_);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button(tr("button.cancel").c_str())) {
            pending_marker_size_percent_ = marker_size_percent_before_dialog_;
            marker_size_percent_ = marker_size_percent_before_dialog_;
            pending_canvas_line_widths_ = canvas_line_widths_before_dialog_;
            canvas_line_widths_ = canvas_line_widths_before_dialog_;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    if (!canvas_element_sizes_popup_open) {
        pending_marker_size_percent_ = marker_size_percent_before_dialog_;
        marker_size_percent_ = marker_size_percent_before_dialog_;
        pending_canvas_line_widths_ = canvas_line_widths_before_dialog_;
        canvas_line_widths_ = canvas_line_widths_before_dialog_;
    }

    if (popups_.canvas_3d_settings) {
        ImGui::OpenPopup(tr("dialog.canvas_3d_settings").c_str());
        popups_.canvas_3d_settings = false;
    }
    bool canvas_3d_settings_popup_open = true;
    if (ImGui::BeginPopupModal(tr("dialog.canvas_3d_settings").c_str(), &canvas_3d_settings_popup_open, ImGuiWindowFlags_AlwaysAutoResize)) {
        if (ImGui::Checkbox(tr("label.scene_fog_effect").c_str(), &pending_scene_fog_enabled_)) {
            apply_scene_fog_effect_to_canvas(pending_scene_fog_enabled_);
        }
        if (ImGui::Checkbox(tr("label.scene_map_draw_distance").c_str(),
                            &pending_scene_map_draw_distance_enabled_)) {
            apply_scene_map_draw_distance_to_canvas(pending_scene_map_draw_distance_enabled_);
        }
        int draw_distance_chunks = clamp_scene_draw_distance(pending_scene_draw_distance_m_) / k_scene_draw_distance_step_m;
        ImGui::SetNextItemWidth(300.0f);
        if (ImGui::SliderInt(tr("label.scene_draw_distance").c_str(),
                             &draw_distance_chunks,
                             k_min_scene_draw_distance_m / k_scene_draw_distance_step_m,
                             k_max_scene_draw_distance_m / k_scene_draw_distance_step_m,
                             "%d00 m",
                             ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_NoInput)) {
            pending_scene_draw_distance_m_ = clamp_scene_draw_distance(draw_distance_chunks * k_scene_draw_distance_step_m);
            apply_scene_draw_distance_to_canvas(pending_scene_draw_distance_m_);
        }
        int edit_component_size_steps =
            clamp_scene_edit_component_size_percent(pending_scene_edit_component_size_percent_) /
            k_scene_edit_component_size_step_percent;
        ImGui::SetNextItemWidth(300.0f);
        if (ImGui::SliderInt(
                tr("label.scene_edit_component_size").c_str(),
                &edit_component_size_steps,
                k_min_scene_edit_component_size_percent / k_scene_edit_component_size_step_percent,
                k_max_scene_edit_component_size_percent / k_scene_edit_component_size_step_percent,
                "%d0%%",
                ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_NoInput)) {
            pending_scene_edit_component_size_percent_ =
                clamp_scene_edit_component_size_percent(
                    edit_component_size_steps * k_scene_edit_component_size_step_percent);
            apply_scene_edit_component_size_to_canvas(pending_scene_edit_component_size_percent_);
        }
        int camera_speed_steps =
            pending_scene_camera_speed_percent_ / k_scene_camera_speed_step_percent;
        ImGui::SetNextItemWidth(300.0f);
        if (ImGui::SliderInt(
                tr("label.scene_camera_speed").c_str(),
                &camera_speed_steps,
                k_min_scene_camera_speed_percent / k_scene_camera_speed_step_percent,
                k_max_scene_camera_speed_percent / k_scene_camera_speed_step_percent,
                "%d0%%",
                ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_NoInput)) {
            pending_scene_camera_speed_percent_ =
                camera_speed_steps * k_scene_camera_speed_step_percent;
            apply_scene_camera_speed_to_canvas(pending_scene_camera_speed_percent_);
        }
        ImGui::Separator();
        if (ImGui::Checkbox(tr("label.scene_performance_warning").c_str(),
                            &pending_scene_performance_warning_enabled_)) {
            apply_scene_performance_warning_to_canvas(
                pending_scene_performance_warning_enabled_,
                pending_scene_instance_warning_threshold_,
                pending_scene_instance_critical_warning_threshold_);
        }
        const auto render_scene_instance_threshold_slider = [](const char* label, int& value) {
            constexpr int step_count =
                (k_max_scene_instance_warning_threshold -
                 k_min_scene_instance_warning_threshold) /
                k_scene_instance_warning_threshold_step;
            int step_index = (value - k_min_scene_instance_warning_threshold) /
                k_scene_instance_warning_threshold_step;
            ImGui::SetNextItemWidth(300.0f);
            const ImVec2 slider_pos = ImGui::GetCursorScreenPos();
            const float slider_width = ImGui::CalcItemWidth();
            const bool changed = ImGui::SliderInt(
                label, &step_index, 0, step_count, "",
                ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_NoInput);
            if (changed) {
                value = k_min_scene_instance_warning_threshold +
                    step_index * k_scene_instance_warning_threshold_step;
            }
            char value_text[16] = {};
            std::snprintf(value_text, sizeof(value_text), "%d", value);
            const ImVec2 value_size = ImGui::CalcTextSize(value_text);
            ImGui::GetWindowDrawList()->AddText(
                ImVec2(slider_pos.x + (slider_width - value_size.x) * 0.5f,
                       slider_pos.y + ImGui::GetStyle().FramePadding.y),
                ImGui::GetColorU32(ImGuiCol_Text), value_text);
            return changed;
        };
        const auto apply_scene_instance_warning_preview = [this]() {
            if (pending_scene_instance_critical_warning_threshold_ <
                pending_scene_instance_warning_threshold_) {
                pending_scene_instance_critical_warning_threshold_ =
                    pending_scene_instance_warning_threshold_;
            }
            apply_scene_performance_warning_to_canvas(
                pending_scene_performance_warning_enabled_,
                pending_scene_instance_warning_threshold_,
                pending_scene_instance_critical_warning_threshold_);
        };
        if (render_scene_instance_threshold_slider(
                tr("label.scene_instance_warning_threshold").c_str(),
                pending_scene_instance_warning_threshold_)) {
            apply_scene_instance_warning_preview();
        }
        if (render_scene_instance_threshold_slider(
                tr("label.scene_instance_critical_warning_threshold").c_str(),
                pending_scene_instance_critical_warning_threshold_)) {
            apply_scene_instance_warning_preview();
        }
        if (ImGui::Button(tr("button.ok").c_str())) {
            scene_draw_distance_m_ = clamp_scene_draw_distance(pending_scene_draw_distance_m_);
            pending_scene_draw_distance_m_ = scene_draw_distance_m_;
            scene_draw_distance_before_dialog_m_ = scene_draw_distance_m_;
            scene_edit_component_size_percent_ = clamp_scene_edit_component_size_percent(
                pending_scene_edit_component_size_percent_);
            pending_scene_edit_component_size_percent_ = scene_edit_component_size_percent_;
            scene_edit_component_size_before_dialog_percent_ = scene_edit_component_size_percent_;
            scene_camera_speed_percent_ = pending_scene_camera_speed_percent_;
            scene_camera_speed_percent_before_dialog_ = scene_camera_speed_percent_;
            scene_performance_warning_enabled_ = pending_scene_performance_warning_enabled_;
            scene_instance_warning_threshold_ = pending_scene_instance_warning_threshold_;
            scene_instance_critical_warning_threshold_ =
                pending_scene_instance_critical_warning_threshold_;
            normalize_scene_instance_warning_thresholds(
                scene_instance_warning_threshold_,
                scene_instance_critical_warning_threshold_);
            pending_scene_instance_warning_threshold_ = scene_instance_warning_threshold_;
            pending_scene_instance_critical_warning_threshold_ =
                scene_instance_critical_warning_threshold_;
            scene_performance_warning_enabled_before_dialog_ =
                scene_performance_warning_enabled_;
            scene_instance_warning_threshold_before_dialog_ = scene_instance_warning_threshold_;
            scene_instance_critical_warning_threshold_before_dialog_ =
                scene_instance_critical_warning_threshold_;
            scene_fog_enabled_ = pending_scene_fog_enabled_;
            scene_fog_enabled_before_dialog_ = scene_fog_enabled_;
            scene_map_draw_distance_enabled_ = pending_scene_map_draw_distance_enabled_;
            scene_map_draw_distance_enabled_before_dialog_ = scene_map_draw_distance_enabled_;
            apply_scene_draw_distance_to_canvas(scene_draw_distance_m_);
            apply_scene_edit_component_size_to_canvas(scene_edit_component_size_percent_);
            apply_scene_camera_speed_to_canvas(scene_camera_speed_percent_);
            apply_scene_fog_effect_to_canvas(scene_fog_enabled_);
            apply_scene_map_draw_distance_to_canvas(scene_map_draw_distance_enabled_);
            apply_scene_performance_warning_to_canvas(
                scene_performance_warning_enabled_,
                scene_instance_warning_threshold_,
                scene_instance_critical_warning_threshold_);
            sync_runtime_settings_before_save();
            save_user_settings(settings_);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button(tr("button.cancel").c_str())) {
            restore_scene_settings_preview();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    if (!canvas_3d_settings_popup_open &&
        scene_settings_preview_differs_from_dialog_baseline()) {
        restore_scene_settings_preview();
    }

    if (popups_.range) {
        ImGui::OpenPopup(tr("menu.plotlimit").c_str());
        popups_.range = false;
    }
    if (ImGui::BeginPopupModal(tr("menu.plotlimit").c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::InputDouble("Min", &plot_min_);
        ImGui::InputDouble("Max", &plot_max_);
        if (ImGui::Button(tr("button.apply").c_str())) {
            dmin_ = plot_min_;
            dmax_ = plot_max_;
            keep_plan_view_ = false;
            reset_profile_axes_next_ = true;
            reset_radius_axes_next_ = true;
            profile_x_span_ = 0.0;
            radius_x_span_ = 0.0;
            profile_x_zoom_pending_ = false;
            profile_y_zoom_pending_ = false;
            radius_x_zoom_pending_ = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button(tr("button.reset").c_str())) {
            plot_min_ = model_.default_min;
            plot_max_ = model_.default_max;
        }
        ImGui::EndPopup();
    }

    if (popups_.control_points) {
        ImGui::OpenPopup(tr("menu.controlpoints").c_str());
        popups_.control_points = false;
    }
    if (ImGui::BeginPopupModal(tr("menu.controlpoints").c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::InputDouble("Min", &cp_start_);
        ImGui::InputDouble("Max", &cp_end_);
        ImGui::InputDouble("Interval", &cp_interval_);
        if (ImGui::Button(tr("button.apply").c_str())) {
            regenerate_geometry();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button(tr("button.reset").c_str())) {
            cp_start_ = std::max(0.0, round_to_100(model_.own.empty() ? 0.0 : model_.own.at(0, 0)) - 500.0);
            cp_end_ = round_to_100(model_.own.empty() ? 0.0 : model_.own.at(model_.own.rows - 1, 0)) + 500.0;
            cp_interval_ = 25.0;
        }
        ImGui::EndPopup();
    }

    if (popups_.background_adjust) {
        ImGui::OpenPopup(tr("dialog.bgimage_adjust").c_str());
        popups_.background_adjust = false;
    }
    if (ImGui::BeginPopupModal(tr("dialog.bgimage_adjust").c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        if (ImGui::BeginTable("bg_adjust_params", 2, ImGuiTableFlags_SizingStretchProp)) {
            auto input_row = [](const char* id, const std::string& label, double& value, const char* format) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextUnformatted(label.c_str());
                ImGui::TableSetColumnIndex(1);
                ImGui::SetNextItemWidth(220.0f);
                ImGui::InputDouble(id, &value, 0.0, 0.0, format);
            };
            auto slider_row = [](const char* id, const std::string& label, double& value, double min_value, double max_value, const char* format) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextUnformatted(label.c_str());
                ImGui::TableSetColumnIndex(1);
                ImGui::SetNextItemWidth(220.0f);
                float slider_value = static_cast<float>(std::clamp(value, min_value, max_value));
                if (ImGui::SliderFloat(id, &slider_value, static_cast<float>(min_value), static_cast<float>(max_value), format,
                                       ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_NoInput)) {
                    value = slider_value;
                }
            };
            input_row("##bg_adjust_x", tr("label.bgimg_x"), pending_bg_x_, "%.3f");
            input_row("##bg_adjust_y", tr("label.bgimg_y"), pending_bg_y_, "%.3f");
            input_row("##bg_adjust_width", tr("label.bgimg_width"), pending_bg_width_, "%.3f");
            input_row("##bg_adjust_height", tr("label.bgimg_height"), pending_bg_height_, "%.3f");
            input_row("##bg_adjust_rotation", tr("label.bgimg_rotation"), pending_bg_rotation_deg_, "%.3f");
            slider_row("##bg_adjust_brightness", tr("label.bgimg_brightness"), pending_bg_brightness_, 1.0, 200.0, "%.0f%%");
            ImGui::EndTable();
        }
        ImGui::Separator();
        if (ImGui::Button(tr("button.ok").c_str())) {
            apply_pending_background_values(true);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button(tr("button.cancel").c_str())) {
            sync_pending_background_values();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    if (popups_.background_align) {
        ImGui::OpenPopup(tr("dialog.align_to_station").c_str());
        popups_.background_align = false;
    }
    if (ImGui::BeginPopupModal(tr("dialog.align_to_station").c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        auto combo_station = [&](const char* label, int& index) {
            index = std::clamp(index, 0, static_cast<int>(model_.stations.size()) - 1);
            std::string preview = model_.stations[index].key + ", " + model_.stations[index].name;
            if (ImGui::BeginCombo(label, preview.c_str())) {
                for (int i = 0; i < static_cast<int>(model_.stations.size()); ++i) {
                    std::string item = model_.stations[i].key + ", " + model_.stations[i].name;
                    if (ImGui::Selectable(item.c_str(), i == index)) index = i;
                }
                ImGui::EndCombo();
            }
        };
        if (has_model_ && model_.stations.size() >= 2) {
            combo_station("Station 1", align_station1_);
            std::string pick1_label = (align_pick1_ ? tr("button.pick_on_bg_ok") : tr("button.pick_on_bg")) + "##align_pick1";
            if (ImGui::Button(pick1_label.c_str())) {
                pick_slot_ = 1;
                ImGui::CloseCurrentPopup();
            }
            combo_station("Station 2", align_station2_);
            std::string pick2_label = (align_pick2_ ? tr("button.pick_on_bg_ok") : tr("button.pick_on_bg")) + "##align_pick2";
            if (ImGui::Button(pick2_label.c_str())) {
                pick_slot_ = 2;
                ImGui::CloseCurrentPopup();
            }
            if (ImGui::Button(tr("button.apply").c_str())) apply_background_alignment();
            ImGui::SameLine();
        }
        if (ImGui::Button(tr("button.ok").c_str())) {
            pick_slot_ = 0;
            apply_background_alignment();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    if (popups_.reload_unsaved_confirm) {
        ImGui::OpenPopup(tr("dialog.reload_unsaved_title").c_str());
        popups_.reload_unsaved_confirm = false;
    }
    if (ImGui::BeginPopupModal(tr("dialog.reload_unsaved_title").c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 420.0f);
        ImGui::TextUnformatted(tr("dialog.reload_unsaved_message").c_str());
        ImGui::PopTextWrapPos();
        ImGui::Separator();
        if (ImGui::Button(tr("button.ok").c_str())) {
            execute_pending_reload_action();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button(tr("button.cancel").c_str())) {
            pending_reload_action_ = PendingReloadAction::None;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    if (popups_.revert_all_edits_confirm) {
        ImGui::OpenPopup(tr("dialog.revert_all_edits_title").c_str());
        popups_.revert_all_edits_confirm = false;
    }
    if (ImGui::BeginPopupModal(tr("dialog.revert_all_edits_title").c_str(), nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 420.0f);
        ImGui::TextUnformatted(tr("dialog.revert_all_edits_message").c_str());
        ImGui::PopTextWrapPos();
        ImGui::Separator();
        if (ImGui::Button(tr("button.revert").c_str())) {
            if (revert_all_pending_edits()) ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button(tr("button.cancel").c_str())) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    if (popups_.close_unsaved_confirm) {
        ImGui::OpenPopup(tr("dialog.unsaved_changes_title").c_str());
        popups_.close_unsaved_confirm = false;
    }
    if (ImGui::BeginPopupModal(tr("dialog.unsaved_changes_title").c_str(), nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        const bool exiting = pending_close_action_ == PendingCloseAction::ExitApplication;
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 520.0f);
        ImGui::TextUnformatted(tr(exiting
            ? "dialog.unsaved_exit_message"
            : "dialog.unsaved_close_edit_message").c_str());
        const bool list_drafts_pending = has_unapplied_editable_list_drafts();
        if (list_drafts_pending) {
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.25f, 1.0f), "%s",
                               tr("dialog.apply_list_before_save").c_str());
        }
        ImGui::PopTextWrapPos();
        ImGui::Separator();
        if (ImGui::Button(tr("button.cancel").c_str())) {
            pending_close_action_ = PendingCloseAction::None;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(list_drafts_pending);
        if (ImGui::Button(tr(exiting
                ? "button.save_changes_and_exit"
                : "button.save_changes_and_close_edit").c_str())) {
            if (resolve_pending_close_action(true)) ImGui::CloseCurrentPopup();
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button(tr(exiting
                ? "button.discard_changes_and_exit"
                : "button.discard_changes_and_close_edit").c_str())) {
            if (resolve_pending_close_action(false)) ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    if (popups_.about) {
        ImGui::OpenPopup(tr("menu.about").c_str());
        popups_.about = false;
    }
    ImGui::SetNextWindowSize(ImVec2(560.0f, 0.0f), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal(tr("menu.about").c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 520.0f);
        ImGui::TextUnformatted(tr("about.text").c_str());
        ImGui::PopTextWrapPos();
        if (ImGui::Button(tr("button.ok").c_str())) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

void App::start_scene_preview() {
    scene_preview_started_ = true;
    show_scene_preview_window_ = true;
    focus_scene_preview_next_ = true;
    scene_preview_preserve_models_on_rebuild_ = false;
    scene_preview_preserve_camera_on_rebuild_ = false;
    pending_scene_preview_started_at_ = std::chrono::steady_clock::now();
    set_program_status("status.scene_preview_loading");
    add_log("[info]gui_kme.cpp: starting 3D scene preview");
    rebuild_scene_preview(false, false);
}

void App::stop_scene_preview() {
    scene_preview_started_ = false;
    scene_preview_dirty_ = true;
    scene_preview_preserve_models_on_rebuild_ = false;
    scene_preview_preserve_camera_on_rebuild_ = false;
    pending_scene_preview_started_at_.reset();
    if (scene_preview_canvas_) scene_preview_canvas_->clear_scene();
    set_program_status("status.scene_preview_stopped");
    add_log("[INFO]3D scene preview stopped");
}

double App::rebuild_scene_preview(bool preserve_loaded_models, bool preserve_camera) {
    if (!scene_preview_canvas_ || !scene_preview_started_) {
        pending_scene_preview_started_at_.reset();
        return 0.0;
    }
    if (!has_model_ || model_.own.empty()) {
        scene_preview_canvas_->clear_scene();
        scene_preview_dirty_ = true;
        scene_preview_preserve_models_on_rebuild_ = false;
        scene_preview_preserve_camera_on_rebuild_ = false;
        pending_scene_preview_started_at_.reset();
        add_log("[warn]gui_kme.cpp: 3D scene preview has no map geometry loaded");
        return 0.0;
    }
    add_log(preserve_loaded_models
                ? "[info]gui_kme.cpp: reloading 3D scene preview track geometry with preserved models"
                : "[info]gui_kme.cpp: generating 3D scene preview track geometry");
    Canvas3DSceneBuildOptions options;
    options.model = &model_;
    options.map_handle = handle_;
    options.unit_distance = unit_distance_;
    options.control_point_interval = cp_interval_;
    options.station_index = station_jump_index_;
    options.show_own_track_markers = show_scene_owntrack_markers_;
    const auto scene_build_started_at = std::chrono::steady_clock::now();
    Canvas3DSceneBuildResult build_result = build_canvas3d_scene_preview(options);
    const double scene_build_seconds = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - scene_build_started_at).count();
    for (const std::string& message : build_result.log_messages) add_log(message);

    size_t track_point_count = 0;
    for (const Canvas3DTrackPath& track : build_result.scene.tracks) {
        track_point_count += track.points.size();
    }
    add_log("[info]gui_kme.cpp: 3D scene preview track geometry ready: tracks=" +
            std::to_string(build_result.scene.tracks.size()) +
            " points=" + std::to_string(track_point_count) +
            " instances=" + std::to_string(build_result.scene.instances.size()) +
            " repeaters=" + std::to_string(build_result.scene.repeaters.size()));

    std::string error;
    if (!scene_preview_canvas_->load_scene(std::move(build_result.scene), error,
                                           preserve_loaded_models, preserve_camera)) {
        add_log("[error]gui_kme.cpp: 3D scene preview failed: " + error);
        scene_preview_dirty_ = true;
        scene_preview_preserve_models_on_rebuild_ = false;
        scene_preview_preserve_camera_on_rebuild_ = false;
        pending_scene_preview_started_at_.reset();
        set_program_status("status.scene_preview_failed");
        return scene_build_seconds;
    }
    sync_scene_preview_marker_visibility();
    Canvas3DSceneStats stats = scene_preview_canvas_->scene_stats();
    std::ostringstream stage_timing;
    stage_timing << std::fixed << std::setprecision(3)
                 << "[info]gui_kme.cpp: 3D scene preview stage timing: scene_build="
                 << scene_build_seconds << " s track_gpu_setup="
                 << stats.track_gpu_setup_seconds << " s model_queue="
                 << stats.model_queue_seconds << " s";
    add_log(stage_timing.str());
    scene_preview_dirty_ = false;
    scene_preview_preserve_models_on_rebuild_ = false;
    scene_preview_preserve_camera_on_rebuild_ = false;
    if (preserve_loaded_models) {
        add_log("[info]gui_kme.cpp: 3D scene preview line geometry reloaded: models_preserved=" +
                std::to_string(stats.model_ready_count) +
                " models_total=" + std::to_string(stats.model_path_count));
    } else {
        add_log("[info]gui_kme.cpp: 3D scene preview model loading queued: models=" +
                std::to_string(stats.model_path_count));
    }
    add_log("[info]gui_kme.cpp: 3D scene preview started: chunks=" + std::to_string(stats.chunk_count) +
            " instances=" + std::to_string(stats.instance_count) +
            " models=" + std::to_string(stats.model_path_count));
    return scene_build_seconds;
}

void App::finish_pending_scene_preview_load_timing() {
    if (!pending_scene_preview_started_at_ || !scene_preview_canvas_) return;
    const Canvas3DSceneStats stats = scene_preview_canvas_->scene_stats();
    if (!stats.active || stats.loading) return;

    const double elapsed_seconds = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - *pending_scene_preview_started_at_).count();
    pending_scene_preview_started_at_.reset();

    const std::string elapsed = format_elapsed_seconds_value(elapsed_seconds);
    add_log("3D preview loaded in " + elapsed + " s");
    set_program_status("status.scene_preview_loaded", elapsed);
}

void App::reload_scene_preview_models() {
    if (!scene_preview_canvas_ || !scene_preview_started_) return;
    std::string error;
    if (!scene_preview_canvas_->reload_scene_models(error)) {
        add_log("[error]gui_kme.cpp: 3D scene preview model reload failed: " + error);
        return;
    }
    Canvas3DSceneStats stats = scene_preview_canvas_->scene_stats();
    add_log("[info]gui_kme.cpp: 3D scene preview model reload queued: models=" +
            std::to_string(stats.model_path_count));
}

void App::sync_scene_preview_track_visibility() {
    if (!scene_preview_canvas_ || !scene_preview_started_) return;

    std::vector<Canvas3DTrackVisibility> visibility =
        build_canvas3d_scene_track_visibility(model_, show_scene_owntrack_markers_);

    std::string error;
    if (!scene_preview_canvas_->set_scene_track_visibility(visibility, error)) {
        add_log("[error]gui_kme.cpp: 3D scene preview track visibility failed: " + error);
    }
}

void App::sync_scene_preview_marker_visibility() {
    if (!scene_preview_canvas_ || !scene_preview_started_) return;

    Canvas3DSceneMarkerVisibility visibility;
    auto set_marker = [&](MapMarkerVisualKind kind, bool visible) {
        if (visible) visibility.marker_mask |= map_marker_visual_bit(kind);
    };
    auto set_label = [&](MapMarkerVisualKind kind, bool visible) {
        if (visible) visibility.label_mask |= map_marker_visual_bit(kind);
    };
    auto set_marker_and_label = [&](MapMarkerVisualKind kind, bool visible) {
        set_marker(kind, visible);
        set_label(kind, visible);
    };

    set_marker(MapMarkerVisualKind::Station, show_stations_);
    set_label(MapMarkerVisualKind::Station,
              show_stations_ && show_station_names_);

    for (MapMarkerVisualKind kind : {
             MapMarkerVisualKind::CurveTransitionStart,
             MapMarkerVisualKind::CurveCircularStart,
             MapMarkerVisualKind::CurveEnd}) {
        set_marker(kind, show_curve_values_);
    }
    for (MapMarkerVisualKind kind : {
             MapMarkerVisualKind::CurveTransitionStart,
             MapMarkerVisualKind::CurveCircularStart,
             MapMarkerVisualKind::CurveEnd}) {
        set_label(kind, show_curve_values_);
    }

    for (MapMarkerVisualKind kind : {
             MapMarkerVisualKind::GradientTransitionStart,
             MapMarkerVisualKind::GradientStart,
             MapMarkerVisualKind::GradientEnd}) {
        set_marker(kind, show_gradient_pos_);
    }
    for (MapMarkerVisualKind kind : {
             MapMarkerVisualKind::GradientTransitionStart,
             MapMarkerVisualKind::GradientStart,
             MapMarkerVisualKind::GradientEnd}) {
        set_label(kind, show_gradient_pos_ && show_gradient_values_);
    }

    set_marker_and_label(MapMarkerVisualKind::SpeedLimit, show_speedlimits_);
    set_marker_and_label(MapMarkerVisualKind::Section, show_section_markers_);
    set_marker_and_label(MapMarkerVisualKind::Beacon, show_beacon_markers_);
    set_marker_and_label(MapMarkerVisualKind::PreTrain, show_pretrain_markers_);
    set_marker_and_label(MapMarkerVisualKind::Irregularity, show_irregularity_markers_);
    set_marker_and_label(MapMarkerVisualKind::MapSound, show_map_sound_markers_);
    set_marker_and_label(MapMarkerVisualKind::MapSound3D, show_map_sound_3d_markers_);
    set_marker_and_label(MapMarkerVisualKind::RollingNoise, show_rolling_noise_markers_);
    set_marker_and_label(MapMarkerVisualKind::FlangeNoise, show_flange_noise_markers_);
    set_marker_and_label(MapMarkerVisualKind::JointNoise, show_joint_noise_markers_);
    set_marker_and_label(MapMarkerVisualKind::Background, show_background_markers_);
    set_marker_and_label(MapMarkerVisualKind::Adhesion, show_adhesion_markers_);
    set_marker_and_label(MapMarkerVisualKind::CabIlluminance, show_cab_illuminance_markers_);
    set_marker_and_label(MapMarkerVisualKind::Fog, show_fog_markers_);
    set_marker_and_label(MapMarkerVisualKind::DrawDistance, show_draw_distance_markers_);
    set_marker_and_label(MapMarkerVisualKind::OtherTrackChange,
                         edit_actions_available());

    std::string error;
    if (!scene_preview_canvas_->set_scene_marker_visibility(
            visibility, error)) {
        add_log("[error]gui_kme.cpp: 3D scene marker visibility failed: " +
                (error.empty() ? std::string("unknown error") : error));
    }
}

void App::render_scene_preview_window() {
    if (scene_preview_canvas_) scene_preview_canvas_->process_scene_loading();
    auto drain_scene_preview_logs = [this]() {
        if (!scene_preview_canvas_) return;
        for (std::string& message : scene_preview_canvas_->drain_scene_load_messages()) {
            add_log(std::move(message));
        }
    };
    drain_scene_preview_logs();
    finish_pending_scene_preview_load_timing();
    if (!show_scene_preview_window_) return;
    if (dock_main_id_) ImGui::SetNextWindowDockID(dock_main_id_, ImGuiCond_FirstUseEver);
    if (focus_scene_preview_next_) ImGui::SetNextWindowFocus();
    std::string title = tr("frame.scene_preview") + "###ScenePreview3D";
    ImGuiStyle& style = ImGui::GetStyle();
    const float button_height = ImGui::GetFrameHeight();
    const float toolbar_padding_y = button_height * 0.25f;
    const float window_padding_x = style.WindowPadding.x;
    const float item_spacing_x = style.ItemSpacing.x;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(window_padding_x, toolbar_padding_y));
    if (ImGui::Begin(title.c_str(), &show_scene_preview_window_)) {
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(item_spacing_x * 1.35f, 0.0f));
        ImGui::BeginDisabled(scene_preview_started_ || load_state_.running || !has_model_);
        if (ImGui::Button(tr("button.start_scene_preview").c_str())) start_scene_preview();
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(!scene_preview_started_ || load_state_.running || !has_model_);
        if (ImGui::Button(tr("button.reload_scene_models").c_str())) reload_scene_preview_models();
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(!scene_preview_started_);
        if (ImGui::Button(tr("button.close").c_str())) stop_scene_preview();
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(tr("frame.mode").c_str());
        ImGui::SameLine();
        Canvas3DSceneInteractionMode scene_mode = scene_preview_canvas_
            ? scene_preview_canvas_->scene_interaction_mode()
            : Canvas3DSceneInteractionMode::Move;
        if (ImGui::RadioButton((tr("mode.pan") + "##scene_preview_move").c_str(),
                               scene_mode == Canvas3DSceneInteractionMode::Move)) {
            scene_mode = Canvas3DSceneInteractionMode::Move;
            if (scene_preview_canvas_) scene_preview_canvas_->set_scene_interaction_mode(scene_mode);
        }
        ImGui::SameLine();
        if (ImGui::RadioButton((tr("mode.select") + "##scene_preview_select").c_str(),
                               scene_mode == Canvas3DSceneInteractionMode::Select)) {
            scene_mode = Canvas3DSceneInteractionMode::Select;
            if (scene_preview_canvas_) scene_preview_canvas_->set_scene_interaction_mode(scene_mode);
        }
        ImGui::SameLine();
        if (ImGui::RadioButton((tr("mode.mileage_select") + "##scene_preview_mileage_select").c_str(),
                               scene_mode == Canvas3DSceneInteractionMode::MileageSelect)) {
            scene_mode = Canvas3DSceneInteractionMode::MileageSelect;
            if (scene_preview_canvas_) scene_preview_canvas_->set_scene_interaction_mode(scene_mode);
        }
        ImGui::PopStyleVar();
        if (scene_preview_started_ && scene_preview_dirty_ && has_model_ && !load_state_.running) {
            const bool preserve_loaded_models = scene_preview_preserve_models_on_rebuild_;
            const bool preserve_camera = scene_preview_preserve_camera_on_rebuild_;
            rebuild_scene_preview(preserve_loaded_models, preserve_camera);
        }
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + toolbar_padding_y);
        ImVec2 avail = ImGui::GetContentRegionAvail();
        Canvas3DSceneUiText scene_ui_text;
        scene_ui_text.switch_signal_aspect = tr("menu.switch_signal_aspect").c_str();
        scene_ui_text.element_properties = tr("dialog.element_properties").c_str();
        scene_ui_text.delete_element = tr("button.delete").c_str();
        scene_ui_text.unpaired_transition = tr("status.transition_unpaired").c_str();
        scene_ui_text.delete_repeater_all = tr("menu.repeater_delete_all").c_str();
        scene_ui_text.delete_repeater_change_point =
            tr("menu.repeater_delete_change_point").c_str();
        scene_ui_text.trim_repeater_to_change_point =
            tr("menu.repeater_trim_to_change_point").c_str();
        scene_ui_text.start_repeater_from_change_point =
            tr("menu.repeater_start_from_change_point").c_str();
        scene_ui_text.locate_structure_list = tr("menu.locate_in_structure_list").c_str();
        scene_ui_text.locate_structure_put_between_list = tr("menu.locate_in_structure_put_between_list").c_str();
        scene_ui_text.locate_repeater_list = tr("menu.locate_in_repeater_list").c_str();
        scene_ui_text.locate_signal_list = tr("menu.locate_in_signal_list").c_str();
        auto set_marker_list_label = [&scene_ui_text](Canvas3DSceneMarkerListKind kind,
                                                       const std::string& label) {
            scene_ui_text.locate_marker_list_labels[static_cast<size_t>(kind)] = label.c_str();
        };
        set_marker_list_label(Canvas3DSceneMarkerListKind::Beacon,
                              tr("menu.locate_in_beacon_list"));
        set_marker_list_label(Canvas3DSceneMarkerListKind::Section,
                              tr("menu.locate_in_section_list"));
        set_marker_list_label(Canvas3DSceneMarkerListKind::Irregularity,
                              tr("menu.locate_in_irregularity_list"));
        set_marker_list_label(Canvas3DSceneMarkerListKind::MapSound,
                              tr("menu.locate_in_map_sound_list"));
        set_marker_list_label(Canvas3DSceneMarkerListKind::MapSound3D,
                              tr("menu.locate_in_map_sound_3d_list"));
        set_marker_list_label(Canvas3DSceneMarkerListKind::RollingNoise,
                              tr("menu.locate_in_rolling_noise_list"));
        set_marker_list_label(Canvas3DSceneMarkerListKind::FlangeNoise,
                              tr("menu.locate_in_flange_noise_list"));
        set_marker_list_label(Canvas3DSceneMarkerListKind::JointNoise,
                              tr("menu.locate_in_joint_noise_list"));
        set_marker_list_label(Canvas3DSceneMarkerListKind::Background,
                              tr("menu.locate_in_background_list"));
        set_marker_list_label(Canvas3DSceneMarkerListKind::Adhesion,
                              tr("menu.locate_in_adhesion_list"));
        set_marker_list_label(Canvas3DSceneMarkerListKind::CabIlluminance,
                              tr("menu.locate_in_cab_illuminance_list"));
        set_marker_list_label(Canvas3DSceneMarkerListKind::Fog,
                              tr("menu.locate_in_fog_list"));
        set_marker_list_label(Canvas3DSceneMarkerListKind::DrawDistance,
                              tr("menu.locate_in_draw_distance_list"));
        set_marker_list_label(Canvas3DSceneMarkerListKind::SpeedLimit,
                              tr("menu.locate_in_speed_limit_list"));
        scene_ui_text.jump_to_repeater_start_position = tr("menu.jump_to_repeater_start_position").c_str();
        scene_ui_text.jump_to_repeater_end_or_change_position =
            tr("menu.jump_to_repeater_end_or_change_position").c_str();
        scene_ui_text.loading = tr("status.scene_loading").c_str();
        scene_ui_text.straight = tr("scene.route_info.straight").c_str();
        scene_ui_text.interpolate_unsupported = tr("scene.route_info.interpolate_unsupported").c_str();
        scene_ui_text.next_station = tr("scene.route_info.next_station").c_str();
        scene_ui_text.speed_limit = tr("scene.route_info.speed_limit").c_str();
        scene_ui_text.signal = tr("scene.route_info.signal").c_str();
        scene_ui_text.no_station_ahead = tr("scene.route_info.no_station_ahead").c_str();
        Canvas3DSceneContextMenuOptions context_menu_options;
        context_menu_options.element_properties_enabled = edit_actions_available();
        sync_scene_placement_edit_from_inspector();
        sync_scene_preview_marker_visibility();
        Canvas3DSceneFrameResult scene_result =
            scene_preview_canvas_->render_scene_preview(avail, scene_ui_text, context_menu_options);
        if (scene_result.placement_drag) {
            apply_scene_placement_drag_update(*scene_result.placement_drag);
        }
        const Canvas3DSceneContextAction& scene_action = scene_result.context_action;
        if (scene_action.kind == Canvas3DSceneContextActionKind::LocateStructure) {
            locate_structure_row_in_list(scene_action.row_index);
        } else if (scene_action.kind == Canvas3DSceneContextActionKind::LocateRepeater) {
            locate_repeater_row_in_list(scene_action.row_index);
        } else if (scene_action.kind == Canvas3DSceneContextActionKind::LocateSignal) {
            locate_signal_row_in_list(scene_action.row_index);
        } else if (scene_action.kind == Canvas3DSceneContextActionKind::LocateMarkerList) {
            locate_scene_marker_row_in_list(scene_action.marker_list_kind, scene_action.row_index);
        } else if (scene_action.kind == Canvas3DSceneContextActionKind::EditElement) {
            request_element_inspector(scene_action.edit_id, scene_action.row_kind);
        } else if (scene_action.kind == Canvas3DSceneContextActionKind::DeleteElement) {
            request_element_delete(scene_action.edit_id, scene_action.row_kind);
        } else if (scene_action.kind == Canvas3DSceneContextActionKind::DeleteRepeaterAll) {
            request_element_delete(scene_action.edit_id, scene_action.row_kind,
                                   RepeaterDeleteMode::EntireChain);
        } else if (scene_action.kind == Canvas3DSceneContextActionKind::DeleteRepeaterChangePoint) {
            request_element_delete(scene_action.edit_id, scene_action.row_kind,
                                   RepeaterDeleteMode::ChangePoint);
        } else if (scene_action.kind == Canvas3DSceneContextActionKind::TrimRepeaterToChangePoint) {
            request_element_delete(scene_action.edit_id, scene_action.row_kind,
                                   RepeaterDeleteMode::TrimToChangePoint);
        } else if (scene_action.kind == Canvas3DSceneContextActionKind::StartRepeaterFromChangePoint) {
            request_element_delete(scene_action.edit_id, scene_action.row_kind,
                                   RepeaterDeleteMode::StartFromChangePoint);
        }
        drain_scene_preview_logs();
    }
    focus_scene_preview_next_ = false;
    ImGui::End();
    ImGui::PopStyleVar();
}

void App::preview_structure_model(const std::string& path) {
    if (path.empty()) {
        add_log("[WARN]model preview: empty model path");
        return;
    }
    show_model_preview_window_ = true;
    focus_model_preview_next_ = true;
    std::string error;
    if (!model_preview_canvas_->load_model(path, error)) {
        add_log("[ERROR]model preview: " + error);
        return;
    }
    for (std::string& warning : model_preview_canvas_->drain_model_load_warnings()) {
        add_log(std::move(warning));
    }
    add_log("[INFO]model preview: " + path);
}

void App::reload_model_preview() {
    if (!model_preview_canvas_ || !model_preview_canvas_->has_model()) return;
    std::string path = model_preview_canvas_->model_path();
    std::string error;
    if (!model_preview_canvas_->reload_model(error)) {
        add_log("[ERROR]model preview reload: " + error);
        return;
    }
    for (std::string& warning : model_preview_canvas_->drain_model_load_warnings()) {
        add_log(std::move(warning));
    }
    add_log("[INFO]model preview reloaded: " + path);
}

void App::perform_reload_current_map_and_model_preview() {
    if (has_model_ && !file_path_.empty()) {
        begin_load(file_path_, true, false, std::nullopt, false, true);
    }
    reload_model_preview();
}

void App::perform_reload_current_map_geometry() {
    add_log("[info]gui_kme.cpp: reloading map geometry with existing 3D models preserved");
    begin_load(file_path_, true, false, std::nullopt, true, true);
}

bool App::confirm_reload_if_unsaved(PendingReloadAction action) {
    if (!has_unsaved_edit_state() || !has_model_ || file_path_.empty()) return false;
    pending_reload_action_ = action;
    popups_.reload_unsaved_confirm = true;
    return true;
}

void App::execute_pending_reload_action() {
    PendingReloadAction action = pending_reload_action_;
    pending_reload_action_ = PendingReloadAction::None;
    if (action == PendingReloadAction::MapAndModelPreview) {
        perform_reload_current_map_and_model_preview();
    } else if (action == PendingReloadAction::GeometryOnly) {
        perform_reload_current_map_geometry();
    }
}

void App::reload_current_map_and_model_preview() {
    if (load_state_.running) return;
    if (confirm_reload_if_unsaved(PendingReloadAction::MapAndModelPreview)) return;
    perform_reload_current_map_and_model_preview();
}

void App::reload_current_map_geometry() {
    if (load_state_.running || !has_model_ || file_path_.empty()) return;
    if (confirm_reload_if_unsaved(PendingReloadAction::GeometryOnly)) return;
    perform_reload_current_map_geometry();
}

void App::handle_shortcuts() {
    if (ImGui::IsKeyPressed(ImGuiKey_F5, false)) {
        reload_current_map_and_model_preview();
    }
    const ImGuiIO& io = ImGui::GetIO();
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S, false)) {
        save_pending_edits();
    }
}

void App::render_model_preview_window() {
    if (!show_model_preview_window_) return;
    if (dock_main_id_) ImGui::SetNextWindowDockID(dock_main_id_, ImGuiCond_FirstUseEver);
    if (focus_model_preview_next_) ImGui::SetNextWindowFocus();
    std::string title = tr("frame.model_preview") + "###ModelPreview3D";
    ImGuiStyle& style = ImGui::GetStyle();
    const float button_height = ImGui::GetFrameHeight();
    const float toolbar_padding_y = button_height * 0.25f;
    const float window_padding_x = style.WindowPadding.x;
    const float item_spacing_x = style.ItemSpacing.x;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(window_padding_x, toolbar_padding_y));
    if (ImGui::Begin(title.c_str(), &show_model_preview_window_)) {
        const bool has_preview_model = model_preview_canvas_ && model_preview_canvas_->has_model();
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(item_spacing_x * 1.35f, 0.0f));

        ImGui::BeginDisabled(show_structure_models_window_);
        if (ImGui::Button(tr("button.model_list").c_str())) show_structure_models_window_ = true;
        ImGui::EndDisabled();
        ImGui::SameLine();

        ImGui::BeginDisabled(!has_preview_model);
        if (ImGui::Button(tr("button.reload").c_str())) reload_model_preview();
        ImGui::EndDisabled();
        ImGui::SameLine();

        ImGui::BeginDisabled(!has_preview_model);
        if (ImGui::Button(tr("button.clear").c_str())) model_preview_canvas_->clear_model();
        ImGui::EndDisabled();
        ImGui::SameLine();

        if (ImGui::Button(tr("button.background_color").c_str())) {
            ImGui::OpenPopup("model_preview_bg_color_popup");
        }
        if (ImGui::BeginPopup("model_preview_bg_color_popup")) {
            const ImGuiColorEditFlags color_flags = ImGuiColorEditFlags_NoAlpha
                | ImGuiColorEditFlags_DisplayRGB
                | ImGuiColorEditFlags_InputRGB
                | ImGuiColorEditFlags_Uint8
                | ImGuiColorEditFlags_PickerHueBar;
            ImGui::SetNextItemWidth(260.0f);
            if (ImGui::ColorPicker3("##model_preview_bg_color_picker", &model_preview_bg_color_.x, color_flags)) {
                model_preview_bg_color_ = clamp_theme_color(model_preview_bg_color_);
                model_preview_canvas_->set_background_color(model_preview_bg_color_);
            }
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + ImGui::GetStyle().WindowPadding.y);
            const float swatch_size = ImGui::GetFrameHeight();
            const std::array<std::pair<const char*, ImVec4>, 5> quick_colors = {{
                {"color.white", ImVec4(1.0f, 1.0f, 1.0f, 1.0f)},
                {"color.black", ImVec4(0.0f, 0.0f, 0.0f, 1.0f)},
                {"color.gray", ImVec4(0.5f, 0.5f, 0.5f, 1.0f)},
                {"color.blue", ImVec4(0.0f, 0.0f, 1.0f, 1.0f)},
                {"color.green", ImVec4(0.0f, 1.0f, 0.0f, 1.0f)},
            }};
            for (size_t i = 0; i < quick_colors.size(); ++i) {
                if (i > 0) ImGui::SameLine();
                std::string id = "##model_preview_quick_" + std::to_string(i);
                if (ImGui::ColorButton(id.c_str(), quick_colors[i].second, ImGuiColorEditFlags_NoAlpha,
                                       ImVec2(swatch_size, swatch_size))) {
                    model_preview_bg_color_ = quick_colors[i].second;
                    model_preview_canvas_->set_background_color(model_preview_bg_color_);
                }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tr(quick_colors[i].first).c_str());
            }
            ImGui::EndPopup();
        }
        ImGui::PopStyleVar();
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + toolbar_padding_y);
        ImVec2 avail = ImGui::GetContentRegionAvail();
        model_preview_canvas_->render(avail);
    }
    focus_model_preview_next_ = false;
    ImGui::End();
    ImGui::PopStyleVar();
}

void App::render() {
    touch_input::new_frame();
    poll_loader();
    handle_shortcuts();
    render_menu();
    render_toolbar();
    render_status_bar();
    ImGuiID dockspace_id = ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport());
    setup_initial_dockspace(dockspace_id);
    render_othertracks_window();
    render_station_list_window();
    render_console();
    render_plots();
    render_file_structure_window();
    render_text_preview_window();
    render_model_preview_window();
    render_scene_preview_window();
    render_structures_window();
    render_structures_between_window();
    render_structure_models_window();
    render_other_trains_window();
    render_sound_list_window();
    render_sound_3d_list_window();
    render_repeaters_window();
    render_signal_aspects_window();
    render_signals_window();
    render_sections_window();
    render_variables_window();
    render_beacons_window();
    render_irregularities_window();
    render_map_sounds_window();
    render_map_sound_3d_window();
    render_rolling_noises_window();
    render_flange_noises_window();
    render_joint_noises_window();
    render_backgrounds_window();
    render_adhesions_window();
    render_cab_illuminance_window();
    render_fogs_window();
    render_draw_distances_window();
    render_speed_limits_window();
    process_pending_element_delete();
    process_pending_element_inspector();
    render_element_inspector();
    render_new_element_wizard();
    render_popups();
    process_distance_resolution_retry();
    touch_input::apply_touch_scroll_to_hovered_window();
    save_runtime_settings_if_changed();
}

ID3D11Device* g_pd3dDevice = nullptr;
ID3D11DeviceContext* g_pd3dDeviceContext = nullptr;
IDXGISwapChain* g_pSwapChain = nullptr;
bool g_SwapChainOccluded = false;
UINT g_ResizeWidth = 0, g_ResizeHeight = 0;
ID3D11RenderTargetView* g_mainRenderTargetView = nullptr;

void CreateRenderTarget() {
    ID3D11Texture2D* back_buffer = nullptr;
    g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&back_buffer));
    g_pd3dDevice->CreateRenderTargetView(back_buffer, nullptr, &g_mainRenderTargetView);
    back_buffer->Release();
}

void CleanupRenderTarget() {
    release_com(g_mainRenderTargetView);
}

bool CreateDeviceD3D(HWND hWnd) {
    DXGI_SWAP_CHAIN_DESC sd = {};
    sd.BufferCount = 2;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hWnd;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    UINT flags = 0;
    D3D_FEATURE_LEVEL feature_level;
    const D3D_FEATURE_LEVEL levels[2] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0};
    HRESULT hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, levels, 2,
                                               D3D11_SDK_VERSION, &sd, &g_pSwapChain, &g_pd3dDevice,
                                               &feature_level, &g_pd3dDeviceContext);
    if (hr == DXGI_ERROR_UNSUPPORTED) {
        hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, flags, levels, 2,
                                           D3D11_SDK_VERSION, &sd, &g_pSwapChain, &g_pd3dDevice,
                                           &feature_level, &g_pd3dDeviceContext);
    }
    if (FAILED(hr)) return false;
    IDXGIFactory* factory = nullptr;
    if (SUCCEEDED(g_pSwapChain->GetParent(IID_PPV_ARGS(&factory)))) {
        factory->MakeWindowAssociation(hWnd, DXGI_MWA_NO_ALT_ENTER);
        factory->Release();
    }
    CreateRenderTarget();
    return true;
}

void CleanupDeviceD3D() {
    CleanupRenderTarget();
    release_com(g_pSwapChain);
    release_com(g_pd3dDeviceContext);
    release_com(g_pd3dDevice);
}

LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (::ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam)) return true;
    if (touch_input::handle_message(hWnd, msg, wParam, lParam)) return 0;
    switch (msg) {
        case k_app_wake_message:
            return 0;
        case WM_SIZE:
            if (wParam == SIZE_MINIMIZED) return 0;
            g_ResizeWidth = LOWORD(lParam);
            g_ResizeHeight = HIWORD(lParam);
            return 0;
        case WM_SYSCOMMAND:
            if ((wParam & 0xfff0) == SC_KEYMENU) return 0;
            break;
        case WM_CLOSE:
            if (g_app) {
                g_app->request_exit();
                return 0;
            }
            break;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

int main(int, char**) {
#ifndef NDEBUG
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    std::vector<std::string> args = command_line_args_utf8();
    HeadlessTableFindOptions table_find = parse_headless_table_find_options(args);
    if (table_find.requested) {
        if (!table_find.error.empty()) {
            std::cerr << table_find.error << "\n"
                      << "usage: komapedit.exe --debug-headless-table-find [--headless-output FILE]\n";
            return 1;
        }
        return App::run_debug_headless_table_find(table_find.output_path);
    }

    HeadlessTouchInputOptions touch_input_options = parse_headless_touch_input_options(args);
    if (touch_input_options.requested) {
        if (!touch_input_options.error.empty()) {
            std::cerr << touch_input_options.error << "\n"
                      << "usage: komapedit.exe --debug-headless-touch-input [--headless-output FILE]\n";
            return 1;
        }
        return run_debug_headless_touch_input(touch_input_options);
    }

    HeadlessScene3DBenchmarkOptions scene3d_bench = parse_headless_scene3d_benchmark_options(args);
    if (scene3d_bench.requested) {
        if (!scene3d_bench.error.empty()) {
            std::cerr << scene3d_bench.error << "\n"
                      << "usage: komapedit.exe --debug-headless-scene3d-bench <map-path> "
                      << "[--frames N] [--unit-distance M] [--max-frame-ms MS] "
                      << "[--window-back-m M] [--window-forward-m M] "
                      << "[--scene-model-workers N] [--disable-scene-texture-cache] "
                      << "[--headless-output FILE]\n";
            return 1;
        }
        return App::run_debug_headless_scene3d_benchmark(scene3d_bench.path, scene3d_bench.frames,
                                                         scene3d_bench.unit_distance, scene3d_bench.max_frame_ms,
                                                         scene3d_bench.window_back_m,
                                                         scene3d_bench.window_forward_m,
                                                         scene3d_bench.scene_model_workers,
                                                         scene3d_bench.disable_scene_texture_cache,
                                                         scene3d_bench.output_path);
    }

    HeadlessSceneCameraTransferOptions scene_camera_transfer = parse_headless_scene_camera_transfer_options(args);
    if (scene_camera_transfer.requested) {
        if (!scene_camera_transfer.error.empty()) {
            std::cerr << scene_camera_transfer.error << "\n"
                      << "usage: komapedit.exe --debug-headless-scene-camera-transfer <map-path> "
                      << "[--unit-distance M] [--camera-distance M] [--headless-output FILE]\n";
            return 1;
        }
        return App::run_debug_headless_scene_camera_transfer(scene_camera_transfer.path,
                                                             scene_camera_transfer.unit_distance,
                                                             scene_camera_transfer.has_camera_distance,
                                                             scene_camera_transfer.camera_distance,
                                                             scene_camera_transfer.output_path);
    }

    HeadlessSourceAnchorOptions source_anchors = parse_headless_source_anchor_options(args);
    if (source_anchors.requested) {
        if (!source_anchors.error.empty()) {
            std::cerr << source_anchors.error << "\n"
                      << "usage: komapedit.exe --debug-headless-source-anchors <map-path> "
                      << "[--unit-distance M] [--headless-output FILE]\n";
            return 1;
        }
        return App::run_debug_headless_source_anchors(source_anchors.path,
                                                      source_anchors.unit_distance,
                                                      source_anchors.output_path);
    }

    HeadlessStationListEditOptions station_list_edit =
        parse_headless_station_list_edit_options(args);
    if (station_list_edit.requested) {
        if (!station_list_edit.error.empty()) {
            std::cerr << station_list_edit.error << "\n"
                      << "usage: komapedit.exe --debug-headless-station-list-edit <map-path> "
                      << "[--unit-distance M] [--commit] [--headless-output FILE]\n";
            return 2;
        }
        return run_debug_headless_station_list_edit(station_list_edit);
    }

    HeadlessDistanceEditBatchOptions distance_edit_batch =
        parse_headless_distance_edit_batch_options(args);
    if (distance_edit_batch.requested) {
        if (!distance_edit_batch.error.empty()) {
            std::cerr << distance_edit_batch.error << "\n"
                      << "usage: komapedit.exe --debug-headless-distance-edit-batch [map-path] "
                      << "[--unit-distance M] [--commit] [--headless-output FILE]\n";
            return 2;
        }
        return run_debug_headless_distance_edit_batch(distance_edit_batch);
    }

    HeadlessRepeaterEditBatchOptions repeater_edit_batch =
        parse_headless_repeater_edit_batch_options(args);
    if (repeater_edit_batch.requested) {
        if (!repeater_edit_batch.error.empty()) {
            std::cerr << repeater_edit_batch.error << "\n"
                      << "usage: komapedit.exe --debug-headless-repeater-edit-batch [map-path] "
                      << "[--unit-distance M] [--commit] [--headless-output FILE]\n";
            return 2;
        }
        return run_debug_headless_repeater_edit_batch(repeater_edit_batch);
    }

    HeadlessSectionEditBatchOptions section_edit_batch =
        parse_headless_section_edit_batch_options(args);
    if (section_edit_batch.requested) {
        if (!section_edit_batch.error.empty()) {
            std::cerr << section_edit_batch.error << "\n"
                      << "usage: komapedit.exe --debug-headless-section-edit-batch [map-path] "
                      << "[--unit-distance M] [--commit] [--headless-output FILE]\n";
            return 2;
        }
        return run_debug_headless_section_edit_batch(section_edit_batch);
    }

    HeadlessInsertEditOptions insert_edit = parse_headless_insert_edit_options(args);
    if (insert_edit.requested) {
        if (!insert_edit.error.empty()) {
            std::cerr << insert_edit.error << "\n"
                      << "usage: komapedit.exe --debug-headless-insert-edit [map-path] "
                      << "[--unit-distance M] [--commit] [--headless-output FILE]\n";
            return 2;
        }
        return run_debug_headless_insert_edit(insert_edit);
    }

    HeadlessEditRoundtripOptions edit_roundtrip = parse_headless_edit_roundtrip_options(args);
    if (edit_roundtrip.requested) {
        if (!edit_roundtrip.error.empty()) {
            std::cerr << edit_roundtrip.error << "\n"
                      << "usage: komapedit.exe --debug-headless-edit-roundtrip <map-path> "
                      << "[--unit-distance M] [--headless-output FILE]\n";
            return 2;
        }
        return App::run_debug_headless_edit_roundtrip(edit_roundtrip.path,
                                                      edit_roundtrip.unit_distance,
                                                      edit_roundtrip.output_path);
    }

    HeadlessOwnTrackEditOptions own_track_edit =
        parse_headless_own_track_edit_options(args);
    if (own_track_edit.requested) {
        if (!own_track_edit.error.empty()) {
            std::cerr << own_track_edit.error << "\n"
                      << "usage: komapedit.exe --debug-headless-own-track-edit [map-path] "
                      << "[--unit-distance M] [--headless-output FILE]\n";
            return 2;
        }
        return App::run_debug_headless_own_track_edit(own_track_edit);
    }

    HeadlessOtherTrackEditOptions other_track_edit =
        parse_headless_other_track_edit_options(args);
    if (other_track_edit.requested) {
        if (!other_track_edit.error.empty()) {
            std::cerr << other_track_edit.error << "\n"
                      << "usage: komapedit.exe --debug-headless-other-track-edit [map-path] "
                      << "[--unit-distance M] [--commit] [--headless-output FILE]\n";
            return 2;
        }
        return App::run_debug_headless_other_track_edit(other_track_edit);
    }

    HeadlessOpenBenchmarkOptions open_bench = parse_headless_open_benchmark_options(args);
    if (open_bench.requested) {
        if (!open_bench.error.empty()) {
            std::cerr << open_bench.error << "\n"
                      << "usage: komapedit.exe --debug-headless-open-bench <map-path> "
                      << "[--repeat N] [--unit-distance M] [--headless-output FILE]\n";
            return 1;
        }
        return App::run_debug_headless_open_benchmark(open_bench);
    }

    HeadlessPlanBenchmarkOptions plan_bench = parse_headless_plan_benchmark_options(args);
    if (plan_bench.requested) {
        if (!plan_bench.error.empty()) {
            std::cerr << plan_bench.error << "\n"
                      << "usage: komapedit.exe --debug-headless-plan-bench <map-path> "
                      << "[--frames N] [--unit-distance M] [--pan-pixels P] "
                      << "[--max-frame-ms MS] [--headless-output FILE] [--profile-stages]\n";
            return 1;
        }
        return App::run_debug_headless_plan_benchmark(plan_bench.path, plan_bench.frames,
                                                      plan_bench.unit_distance, plan_bench.pan_pixels,
                                                      plan_bench.max_frame_ms, plan_bench.output_path,
                                                      plan_bench.profile_stages);
    }

    HeadlessLoadOptions headless = parse_headless_load_options(args);
    if (headless.requested) {
        if (!headless.error.empty()) {
            std::cerr << headless.error << "\n"
                      << "usage: komapedit.exe --headless-load-map <map-path> "
                      << "[--repeat N] [--unit-distance M] [--load-profile preview|edit] "
                      << "[--headless-output FILE]\n";
            return 1;
        }
        return run_headless_load_map(headless);
    }
#endif

    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    UserSettings settings = load_user_settings();
    ImGui_ImplWin32_EnableDpiAwareness();
    float scale = ImGui_ImplWin32_GetDpiScaleForMonitor(MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY));

    HINSTANCE instance = GetModuleHandleW(nullptr);
    HICON app_icon = static_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(IDI_KOMAPEDIT), IMAGE_ICON,
                                                   GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), 0));
    HICON app_icon_small = static_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(IDI_KOMAPEDIT), IMAGE_ICON,
                                                         GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0));

    WNDCLASSEXW wc = {sizeof(wc), CS_CLASSDC, WndProc, 0L, 0L, instance, app_icon, nullptr, nullptr, nullptr, L"komapedit", app_icon_small};
    RegisterClassExW(&wc);
    HWND hwnd = CreateWindowW(wc.lpszClassName, L"komapedit", WS_OVERLAPPEDWINDOW,
                              100, 100, static_cast<int>(1440 * scale), static_cast<int>(900 * scale),
                              nullptr, nullptr, wc.hInstance, nullptr);
    if (!hwnd) {
        UnregisterClassW(wc.lpszClassName, wc.hInstance);
        if (app_icon) DestroyIcon(app_icon);
        if (app_icon_small) DestroyIcon(app_icon_small);
        CoUninitialize();
        return 1;
    }
    if (app_icon) SendMessageW(hwnd, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(app_icon));
    if (app_icon_small) SendMessageW(hwnd, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(app_icon_small));
    touch_input::initialize(hwnd);
    g_main_hwnd = hwnd;
    if (!CreateDeviceD3D(hwnd)) {
        CleanupDeviceD3D();
        DestroyWindow(hwnd);
        UnregisterClassW(wc.lpszClassName, wc.hInstance);
        if (app_icon) DestroyIcon(app_icon);
        if (app_icon_small) DestroyIcon(app_icon_small);
        g_main_hwnd = nullptr;
        CoUninitialize();
        return 1;
    }

    ShowWindow(hwnd, SW_SHOWDEFAULT);
    UpdateWindow(hwnd);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImPlot::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
    io.IniFilename = nullptr;
    io.IniSavingRate = 0.25f;
    std::filesystem::path layout_path = default_imgui_ini_path();
    bool has_saved_layout = load_imgui_layout(layout_path);

    bool viewports_enabled = (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) != 0;
    apply_ui_settings(settings.font_size, settings.ui_component_size, settings.theme_color, scale, viewports_enabled);
    io.ConfigDpiScaleFonts = true;
    io.ConfigDpiScaleViewports = true;

    const char* font_candidates[] = {
        "C:/Windows/Fonts/msyh.ttc",
        "C:/Windows/Fonts/YuGothM.ttc",
        "C:/Windows/Fonts/meiryo.ttc",
        "C:/Windows/Fonts/segoeui.ttf"
    };
    bool font_loaded = false;
    for (const char* font : font_candidates) {
        if (std::filesystem::exists(font)) {
            io.Fonts->AddFontFromFileTTF(font, k_default_font_size * scale, nullptr,
                                         application_font_glyph_ranges(*io.Fonts));
            font_loaded = true;
            break;
        }
    }
    if (!font_loaded) io.Fonts->AddFontDefault();
    merge_required_symbol_glyphs(*io.Fonts, k_default_font_size * scale);

    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);

    App app(g_pd3dDevice, std::move(settings), scale, viewports_enabled, has_saved_layout);

    bool done = false;
    bool needs_render = true;
    int warmup_frames = 2;
    while (!done) {
        bool received_message = false;
        MSG msg;
        while (PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE)) {
            received_message = true;
            TranslateMessage(&msg);
            DispatchMessage(&msg);
            if (msg.message == WM_QUIT) done = true;
        }
        if (done) break;

        if (!needs_render && !received_message) {
            MsgWaitForMultipleObjectsEx(0, nullptr, INFINITE, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
            continue;
        }
        needs_render = false;

        if (g_SwapChainOccluded && g_pSwapChain->Present(0, DXGI_PRESENT_TEST) == DXGI_STATUS_OCCLUDED) {
            continue;
        }
        g_SwapChainOccluded = false;

        if (g_ResizeWidth != 0 && g_ResizeHeight != 0) {
            CleanupRenderTarget();
            g_pSwapChain->ResizeBuffers(0, g_ResizeWidth, g_ResizeHeight, DXGI_FORMAT_UNKNOWN, 0);
            g_ResizeWidth = g_ResizeHeight = 0;
            CreateRenderTarget();
        }

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();
        app.render();

        ImGui::Render();
        const float clear_color[4] = {0.06f, 0.07f, 0.08f, 1.0f};
        g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, nullptr);
        g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView, clear_color);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

        if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
            ImGui::UpdatePlatformWindows();
            ImGui::RenderPlatformWindowsDefault();
        }

        HRESULT hr = g_pSwapChain->Present(1, 0);
        g_SwapChainOccluded = (hr == DXGI_STATUS_OCCLUDED);
        if (warmup_frames > 0) {
            --warmup_frames;
            needs_render = true;
        }
        save_imgui_layout_if_requested(layout_path);
        if (GImGui && GImGui->InputEventsQueue.Size > 0) needs_render = true;
        if (touch_input::wants_continuous_render()) needs_render = true;
        if (imgui_layout_save_pending()) needs_render = true;
    }

    save_imgui_layout(layout_path);

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImPlot::DestroyContext();
    ImGui::DestroyContext();

    CleanupDeviceD3D();
    g_main_hwnd = nullptr;
    DestroyWindow(hwnd);
    UnregisterClassW(wc.lpszClassName, wc.hInstance);
    if (app_icon) DestroyIcon(app_icon);
    if (app_icon_small) DestroyIcon(app_icon_small);
    CoUninitialize();
    return 0;
}
