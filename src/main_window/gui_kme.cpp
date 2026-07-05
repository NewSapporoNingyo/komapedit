/*
 * Copyright (c) 2026 Sapporo_ningyo
 *
 * Licensed under Apache License 2.0; see LICENSE and NOTICE.
 * The GUI uses Dear ImGui and ImPlot; see THIRD_PARTY_NOTICES.md.
 */

#pragma execution_character_set("utf-8")

#include "kme.h"
#include "app_settings.h"
#include "debug_headless.h"
#include "touch_input.h"

#include "canvas3D.h"
#include "maploader.h"
#include "resource.h"

#include "imgui.h"
#include "imgui_internal.h"
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
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

#ifndef NDEBUG
std::ostream* g_debug_plan_benchmark_log = nullptr;
#endif

template <typename T>
void release_com(T*& p) {
    if (p) {
        p->Release();
        p = nullptr;
    }
}

void set_crosshair_cursor() {
    ::SetCursor(::LoadCursor(nullptr, IDC_CROSS));
}

void set_move_cursor() {
    ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
    ::SetCursor(::LoadCursor(nullptr, IDC_SIZEALL));
}

void append_gui_json_string(std::ostringstream& out, const std::string& text) {
    out << '"';
    for (unsigned char ch : text) {
        switch (ch) {
            case '\\': out << "\\\\"; break;
            case '"': out << "\\\""; break;
            case '\b': out << "\\b"; break;
            case '\f': out << "\\f"; break;
            case '\n': out << "\\n"; break;
            case '\r': out << "\\r"; break;
            case '\t': out << "\\t"; break;
            default:
                if (ch < 0x20) {
                    out << "\\u" << std::hex << std::setw(4) << std::setfill('0')
                        << static_cast<int>(ch) << std::dec << std::setfill(' ');
                } else {
                    out << static_cast<char>(ch);
                }
        }
    }
    out << '"';
}

std::string edit_field_buffer_text(const MapElementEditFieldState& field) {
    return std::string(field.value);
}

std::string trim_gui_ascii_copy(const std::string& text) {
    size_t first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    size_t last = text.find_last_not_of(" \t\r\n");
    return text.substr(first, last - first + 1);
}

void set_edit_field_buffer(MapElementEditFieldState& field, const std::string& value) {
    std::snprintf(field.value, sizeof(field.value), "%s", value.c_str());
}

bool parse_gui_edit_number(const std::string& text) {
    std::string trimmed = trim_gui_ascii_copy(text);
    if (trimmed.empty()) return false;
    const char* begin = trimmed.c_str();
    char* end = nullptr;
    errno = 0;
    std::strtod(begin, &end);
    return end != begin && errno != ERANGE && end && *end == '\0';
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
    std::ostringstream out;
    out << std::fixed << std::setprecision(precision) << value;
    std::string s = out.str();
    size_t dot = s.find('.');
    if (dot != std::string::npos) {
        while (s.size() > dot + 1 && s.back() == '0') s.pop_back();
        if (s.size() == dot + 1) s.pop_back();
    }
    return s;
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

namespace mini_json {

struct Value {
    enum class Type { Null, Bool, Number, String, Array, Object };
    Type type = Type::Null;
    bool boolean = false;
    double number = 0.0;
    std::string string;
    std::vector<Value> array;
    std::map<std::string, Value> object;

    bool is_null() const { return type == Type::Null; }
    bool is_number() const { return type == Type::Number; }
    bool is_string() const { return type == Type::String; }
    bool is_array() const { return type == Type::Array; }
    bool is_object() const { return type == Type::Object; }

    const Value& at(const std::string& key) const {
        static Value empty;
        auto it = object.find(key);
        return it == object.end() ? empty : it->second;
    }

    std::string scalar_text() const {
        if (type == Type::String) return string;
        if (type == Type::Number) return format_double(number);
        if (type == Type::Bool) return boolean ? "true" : "false";
        return "";
    }
};

class Parser {
public:
    explicit Parser(const std::string& source) : s_(source) {}

    Value parse() {
        skip_ws();
        Value v = parse_value();
        skip_ws();
        return v;
    }

private:
    const std::string& s_;
    size_t p_ = 0;

    bool eof() const { return p_ >= s_.size(); }
    char peek() const { return eof() ? '\0' : s_[p_]; }
    char get() { return eof() ? '\0' : s_[p_++]; }

    void skip_ws() {
        while (!eof() && std::isspace(static_cast<unsigned char>(peek()))) ++p_;
    }

    static void append_utf8(std::string& out, unsigned codepoint) {
        if (codepoint <= 0x7f) {
            out.push_back(static_cast<char>(codepoint));
        } else if (codepoint <= 0x7ff) {
            out.push_back(static_cast<char>(0xc0 | ((codepoint >> 6) & 0x1f)));
            out.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
        } else if (codepoint <= 0xffff) {
            out.push_back(static_cast<char>(0xe0 | ((codepoint >> 12) & 0x0f)));
            out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3f)));
            out.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
        } else {
            out.push_back(static_cast<char>(0xf0 | ((codepoint >> 18) & 0x07)));
            out.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3f)));
            out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3f)));
            out.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
        }
    }

    unsigned parse_hex4() {
        unsigned value = 0;
        for (int i = 0; i < 4; ++i) {
            char c = get();
            value <<= 4;
            if (c >= '0' && c <= '9') value |= static_cast<unsigned>(c - '0');
            else if (c >= 'a' && c <= 'f') value |= static_cast<unsigned>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') value |= static_cast<unsigned>(c - 'A' + 10);
        }
        return value;
    }

    Value parse_string() {
        Value v;
        v.type = Value::Type::String;
        if (get() != '"') return v;
        while (!eof()) {
            char c = get();
            if (c == '"') break;
            if (c == '\\') {
                char esc = get();
                switch (esc) {
                    case '"': v.string.push_back('"'); break;
                    case '\\': v.string.push_back('\\'); break;
                    case '/': v.string.push_back('/'); break;
                    case 'b': v.string.push_back('\b'); break;
                    case 'f': v.string.push_back('\f'); break;
                    case 'n': v.string.push_back('\n'); break;
                    case 'r': v.string.push_back('\r'); break;
                    case 't': v.string.push_back('\t'); break;
                    case 'u': append_utf8(v.string, parse_hex4()); break;
                    default: v.string.push_back(esc); break;
                }
            } else {
                v.string.push_back(c);
            }
        }
        return v;
    }

    Value parse_number() {
        size_t begin = p_;
        if (peek() == '-') ++p_;
        while (std::isdigit(static_cast<unsigned char>(peek()))) ++p_;
        if (peek() == '.') {
            ++p_;
            while (std::isdigit(static_cast<unsigned char>(peek()))) ++p_;
        }
        if (peek() == 'e' || peek() == 'E') {
            ++p_;
            if (peek() == '+' || peek() == '-') ++p_;
            while (std::isdigit(static_cast<unsigned char>(peek()))) ++p_;
        }
        Value v;
        v.type = Value::Type::Number;
        v.number = std::strtod(s_.c_str() + begin, nullptr);
        return v;
    }

    Value parse_array() {
        Value v;
        v.type = Value::Type::Array;
        get();
        skip_ws();
        if (peek() == ']') {
            get();
            return v;
        }
        while (!eof()) {
            v.array.push_back(parse_value());
            skip_ws();
            char c = get();
            if (c == ']') break;
            if (c != ',') break;
            skip_ws();
        }
        return v;
    }

    Value parse_object() {
        Value v;
        v.type = Value::Type::Object;
        get();
        skip_ws();
        if (peek() == '}') {
            get();
            return v;
        }
        while (!eof()) {
            Value key = parse_string();
            skip_ws();
            if (get() != ':') break;
            skip_ws();
            v.object[key.string] = parse_value();
            skip_ws();
            char c = get();
            if (c == '}') break;
            if (c != ',') break;
            skip_ws();
        }
        return v;
    }

    Value parse_literal(const char* literal, Value value) {
        while (*literal) {
            if (get() != *literal++) break;
        }
        return value;
    }

    Value parse_value() {
        skip_ws();
        char c = peek();
        if (c == '"') return parse_string();
        if (c == '[') return parse_array();
        if (c == '{') return parse_object();
        if (c == '-' || std::isdigit(static_cast<unsigned char>(c))) return parse_number();
        if (s_.compare(p_, 4, "true") == 0) {
            Value v; v.type = Value::Type::Bool; v.boolean = true; return parse_literal("true", v);
        }
        if (s_.compare(p_, 5, "false") == 0) {
            Value v; v.type = Value::Type::Bool; v.boolean = false; return parse_literal("false", v);
        }
        Value nullv;
        if (s_.compare(p_, 4, "null") == 0) return parse_literal("null", nullv);
        return nullv;
    }
};

} // namespace mini_json

std::string table_cell_text(const mini_json::Value& value) {
    if (value.is_array()) {
        std::string text;
        for (size_t i = 0; i < value.array.size(); ++i) {
            if (i) text += ", ";
            text += table_cell_text(value.array[i]);
        }
        return text;
    }
    return value.scalar_text();
}

EditSourceInfo edit_source_from_json(const mini_json::Value& value) {
    EditSourceInfo source;
    if (!value.is_object()) return source;
    source.file_path = value.at("filePath").scalar_text();
    source.line = static_cast<int>(value.at("line").number);
    source.column = static_cast<int>(value.at("column").number);
    source.raw_text_preview = value.at("rawTextPreview").scalar_text();
    return source;
}

void apply_table_row_edit_metadata(TableRow& row, const mini_json::Value& value) {
    if (!value.is_object()) return;
    row.edit_id = value.at("editId").scalar_text();
    row.source = edit_source_from_json(value.at("source"));
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

App* g_app = nullptr;
HWND g_main_hwnd = nullptr;
constexpr UINT kAppWakeMessage = WM_APP + 1;

void wake_main_window() {
    if (g_main_hwnd) PostMessageW(g_main_hwnd, kAppWakeMessage, 0, 0);
}

App::App(ID3D11Device* device, UserSettings settings, float dpi_scale, bool viewports_enabled, bool has_saved_layout)
    : device_(device), settings_(std::move(settings)), dpi_scale_(dpi_scale), viewports_enabled_(viewports_enabled),
      has_saved_layout_(has_saved_layout) {
    g_app = this;
    kv_set_log_callback(&App::log_callback);
    model_preview_canvas_ = std::make_unique<Canvas3D>(device_);
    model_preview_canvas_->set_background_color(model_preview_bg_color_);
    scene_preview_canvas_ = std::make_unique<Canvas3D>(device_);
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
    std::string lower_text = text;
    std::transform(lower_text.begin(), lower_text.end(), lower_text.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    int sev = 0;
    if (lower_text.find("[error]") != std::string::npos ||
        lower_text.find("error") != std::string::npos) {
        sev = 2;
    } else if (lower_text.find("[warn]") != std::string::npos ||
               lower_text.find("warning") != std::string::npos) {
        sev = 1;
    }
    {
        std::lock_guard<std::mutex> lock(log_mutex_);
        logs_.push_back({text, sev});
        last_log_ = text;
        if (sev == 2) ++error_count_;
        if (sev == 1) ++warn_count_;
    }
    wake_main_window();
}

void App::stop_loader() {
    if (load_state_.worker.joinable()) load_state_.worker.join();
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
        last_log_.clear();
        error_count_ = warn_count_ = 0;
    }
    load_state_.running = true;
    load_state_.pending_started_at.reset();
    plan_canvas_rendered_this_frame_ = false;
    add_log(std::string("Start loading file: ") + path);

    bool has_cp = preserve_settings && has_model_ && model_.has_cp_arb;
    double cp0 = has_cp ? model_.cp_arb[0] : 0.0;
    double cp1 = has_cp ? model_.cp_arb[1] : 0.0;
    double cp2 = has_cp ? model_.cp_arb[2] : 25.0;

    load_state_.worker = std::thread([this, path, has_cp, cp0, cp1, cp2, old_other, preserve_settings,
                           record_history, background_to_restore, load_started_at,
                           preserve_scene_preview_models,
                           preserve_scene_preview_camera]() mutable {
        LoadResult result = load_map_worker(path, unit_distance_, has_cp, cp0, cp1, cp2);
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
}

void App::apply_load_result(LoadResult result) {
    if (!result.ok) {
        load_state_.pending_started_at.reset();
        plan_canvas_rendered_this_frame_ = false;
        add_log("Error during loading: " + result.error);
        if (result.handle) kv_free(result.handle);
        return;
    }
    const bool loaded_different_file = result.path != file_path_;
    if (handle_) kv_free(handle_);
    handle_ = result.handle;
    model_ = std::move(result.model);
    edit_registry_loaded_ = result.full_edit_registry;
    if (clear_pending_edits_after_load_ || loaded_different_file ||
        !pending_edit_changes_.empty() || has_unsaved_edits_) {
        clear_pending_edit_state();
        clear_pending_edits_after_load_ = false;
    }
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
           << "maploader=" << result.maploader_seconds << "s"
           << ", model=" << result.model_build_seconds << "s"
           << ", ir_json=" << model_.ir_json_seconds << "s"
           << ", json_parse=" << model_.json_parse_seconds << "s"
           << ", hydrate=" << model_.model_hydrate_seconds << "s"
           << ", buffer copy=" << model_.buffer_copy_seconds << "s";
    add_log("Load timing: " + timing.str());
    for (const std::string& warning : model_.scene_track_key_warnings) add_log(warning);
    add_log("Map loaded: " + result.path);
    if (result.background_to_restore) {
        apply_background_history(*result.background_to_restore);
    } else if (!result.preserve_settings) {
        clear_background_image();
    }
    if (result.record_history) touch_recent_map(result.path);
    load_state_.pending_started_at = result.started_at;
    plan_canvas_rendered_this_frame_ = false;
}

void App::after_frame_presented() {
    if (!load_state_.pending_started_at || !plan_canvas_rendered_this_frame_) return;

    double elapsed_seconds = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - *load_state_.pending_started_at).count();
    load_state_.pending_started_at.reset();
    plan_canvas_rendered_this_frame_ = false;

    std::ostringstream elapsed;
    elapsed << std::fixed << std::setprecision(2) << elapsed_seconds;
    add_log("Map loaded in " + elapsed.str() + "s");
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
        options.full_edit_registry = edit_registry_loaded_ || inspector_.open || !pending_edit_changes_.empty();
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
    auto started_at = std::chrono::steady_clock::now();
    auto elapsed_seconds = [&]() {
        return std::chrono::duration<double>(std::chrono::steady_clock::now() - started_at).count();
    };
    LoadResult out;
    out.path = path;
    auto maploader_started_at = std::chrono::steady_clock::now();
    void* handle = kv_load_map(path.c_str(), unit_distance);
    auto maploader_finished_at = std::chrono::steady_clock::now();
    out.maploader_seconds = std::chrono::duration<double>(maploader_finished_at - maploader_started_at).count();
    if (!handle) {
        const char* err = kv_get_last_error();
        out.error = err ? err : "maploader failed";
        out.elapsed_seconds = elapsed_seconds();
        return out;
    }
    if (has_cp) {
        auto geometry_started_at = std::chrono::steady_clock::now();
        if (!kv_generate_geometry(handle, unit_distance, 1, cp_start, cp_end, cp_step)) {
            const char* err = kv_get_last_error();
            out.error = err ? err : "geometry failed";
            out.elapsed_seconds = elapsed_seconds();
            kv_free(handle);
            return out;
        }
        out.geometry_seconds = std::chrono::duration<double>(
            std::chrono::steady_clock::now() - geometry_started_at).count();
    }
    try {
        auto model_started_at = std::chrono::steady_clock::now();
        out.model = build_model_from_handle(handle, path, options);
        out.full_edit_registry = options.full_edit_registry;
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

MapModel App::build_model_from_handle(void* handle, const std::string& path) {
    return build_model_from_handle(handle, path, LoadModelOptions{});
}

MapModel App::build_model_from_handle(void* handle, const std::string& path,
                                      LoadModelOptions options) {
    auto json_started_at = std::chrono::steady_clock::now();
    unsigned ir_flags = options.full_edit_registry
        ? (KV_IR_JSON_FULL_EDIT | KV_IR_JSON_FULL_STATEMENT_SOURCE)
        : KV_IR_JSON_COMPACT;
    const char* raw = kv_get_ir_json_ex(handle, ir_flags);
    if (!raw) throw std::runtime_error("kv_get_ir_json_ex failed");
    std::string json(raw);
    kv_free_string(raw);
    double ir_json_seconds = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - json_started_at).count();

    auto parse_started_at = std::chrono::steady_clock::now();
    auto root = mini_json::Parser(json).parse();
    double json_parse_seconds = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - parse_started_at).count();

    auto hydrate_started_at = std::chrono::steady_clock::now();
    MapModel model;
    model.path = path;
    const auto& edit = root.at("edit");
    const auto& edit_files = edit.at("files");
    if (edit_files.is_array()) {
        model.edit_files.reserve(edit_files.array.size());
        for (const auto& item : edit_files.array) {
            if (!item.is_object()) continue;
            EditSourceFileInfo file;
            file.file_path = item.at("filePath").scalar_text();
            file.display_path = item.at("displayPath").scalar_text();
            file.encoding = item.at("encoding").scalar_text();
            file.newline = item.at("newline").scalar_text();
            file.source_hash = item.at("sourceHash").scalar_text();
            file.byte_length = static_cast<size_t>(std::max(0.0, item.at("byteLength").number));
            model.edit_files.push_back(std::move(file));
        }
    }
    const auto& edit_statements = edit.at("statements");
    if (edit_statements.is_array()) {
        model.edit_statements.reserve(edit_statements.array.size());
        for (const auto& item : edit_statements.array) {
            if (!item.is_object()) continue;
            EditStatementInfo statement;
            statement.edit_id = item.at("editId").scalar_text();
            statement.statement_kind = item.at("statementKind").scalar_text();
            statement.source = edit_source_from_json(item.at("source"));
            statement.raw_text = item.at("rawText").scalar_text();
            statement.raw_arguments = item.at("rawArguments").scalar_text();
            statement.distance_expression = item.at("distanceExpression").scalar_text();
            statement.distance_value = item.at("distanceValue").number;
            statement.global_order = static_cast<int>(item.at("globalOrder").number);
            model.edit_statements.push_back(std::move(statement));
        }
    }
    const auto& edit_elements = edit.at("elements");
    if (edit_elements.is_array()) {
        model.edit_elements.reserve(edit_elements.array.size());
        for (const auto& item : edit_elements.array) {
            if (!item.is_object()) continue;
            EditElementInfo element;
            element.edit_id = item.at("editId").scalar_text();
            element.row_kind = item.at("rowKind").scalar_text();
            element.row_index = static_cast<size_t>(std::max(0.0, item.at("rowIndex").number));
            element.source_file_path = item.at("sourceFilePath").scalar_text();
            element.global_order = static_cast<int>(item.at("globalOrder").number);
            model.edit_elements.push_back(std::move(element));
        }
    }
    double buffer_copy_seconds = 0.0;
    auto copy_buffer_timed = [&buffer_copy_seconds](KvDoubleBuffer buffer) {
        auto started_at = std::chrono::steady_clock::now();
        Matrix matrix = copy_buffer(buffer);
        buffer_copy_seconds += std::chrono::duration<double>(std::chrono::steady_clock::now() - started_at).count();
        return matrix;
    };
    model.own = copy_buffer_timed(kv_get_owntrack_buffer(handle));
    model.curve = copy_buffer_timed(kv_get_curveradius_buffer(handle));

    const auto& cps = root.at("controlpoints");
    if (cps.is_array()) {
        for (const auto& v : cps.array) if (v.is_number()) model.controlpoints.push_back(v.number);
    }

    const auto& cp_default = root.at("cp_defaultrange");
    if (cp_default.is_array() && cp_default.array.size() >= 2) {
        model.cp_default_min = cp_default.array[0].number;
        model.cp_default_max = cp_default.array[1].number;
    }
    const auto& cp_arb = root.at("cp_arbdistribution");
    if (cp_arb.is_array() && cp_arb.array.size() >= 3) {
        model.cp_arb[0] = cp_arb.array[0].number;
        model.cp_arb[1] = cp_arb.array[1].number;
        model.cp_arb[2] = cp_arb.array[2].number;
    }

    if (!model.own.empty()) {
        model.distance_origin = model.own.at(0, 0);
        model.height_origin = model.own.at(0, 3);
        model.origin_angle = model.own.at(0, 4);
    }

    static const ImVec4 palette[] = {
        ImVec4(0.12f, 0.47f, 0.71f, 1.0f), ImVec4(1.00f, 0.50f, 0.05f, 1.0f),
        ImVec4(0.17f, 0.63f, 0.17f, 1.0f), ImVec4(0.84f, 0.15f, 0.16f, 1.0f),
        ImVec4(0.58f, 0.40f, 0.74f, 1.0f), ImVec4(0.55f, 0.34f, 0.29f, 1.0f),
        ImVec4(0.89f, 0.47f, 0.76f, 1.0f), ImVec4(0.50f, 0.50f, 0.50f, 1.0f),
        ImVec4(0.74f, 0.74f, 0.13f, 1.0f), ImVec4(0.09f, 0.75f, 0.81f, 1.0f)
    };

    const auto& other = root.at("othertrack");
    const auto& order = other.at("order");
    const auto& ranges = other.at("cp_range");
    if (order.is_array()) {
        for (size_t i = 0; i < order.array.size(); ++i) {
            std::string key = order.array[i].scalar_text();
            OtherTrack t;
            t.key = key;
            t.color = palette[i % (sizeof(palette) / sizeof(palette[0]))];
            t.range_min = 0.0;
            t.range_max = 0.0;
            const auto& range = ranges.at(key);
            if (range.is_object()) {
                t.range_min = range.at("min").number;
                t.range_max = range.at("max").number;
            }
            Matrix points;
            KvDoubleBuffer buf = kv_get_othertrack_buffer(handle, key.c_str());
            points = copy_buffer_timed(buf);
            t.points = std::move(points);
            if (!t.points.empty() && (t.range_min == t.range_max)) {
                t.range_min = t.points.at(0, 0);
                t.range_max = t.points.at(t.points.rows - 1, 0);
            }
            model.other_tracks.push_back(std::move(t));
        }
    }

    const auto& own_track = root.at("own_track");
    if (own_track.is_array()) {
        for (const auto& row : own_track.array) {
            TrackEvent e;
            e.distance = row.at("distance").number;
            e.key = row.at("key").scalar_text();
            e.flag = row.at("flag").scalar_text();
            const auto& val = row.at("value");
            if (val.is_number()) {
                e.value_number = true;
                e.number = val.number;
            } else {
                e.text = val.scalar_text();
            }
            model.own_events.push_back(std::move(e));
        }
    }

    const auto& speed = root.at("speedlimit");
    if (speed.is_array()) {
        for (const auto& row : speed.array) {
            SpeedLimit s;
            s.distance = row.at("distance").number;
            const auto& v = row.at("speed");
            if (v.is_number()) {
                s.has_speed = true;
                s.speed = v.number;
            }
            model.speedlimits.push_back(s);
        }
    }

    const auto& station = root.at("station");
    const auto& positions = station.at("position");
    const auto& names = station.at("stationkey");
    if (positions.is_array()) {
        std::set<std::string> seen;
        for (const auto& item : positions.array) {
            if (!item.is_array() || item.array.size() < 2) continue;
            Station s;
            s.distance = item.array[0].number;
            s.key = item.array[1].scalar_text();
            s.name = names.at(s.key).scalar_text();
            if (s.name.empty()) s.name = s.key;
            s.mileage = s.distance - model.distance_origin;
            if (!model.own.empty()) {
                size_t idx = 0;
                while (idx + 1 < model.own.rows && model.own.at(idx, 0) < s.distance) ++idx;
                if (idx >= model.own.rows) idx = model.own.rows - 1;
                s.x = model.own.at(idx, 1);
                s.y = model.own.at(idx, 2);
                s.z = model.own.at(idx, 3);
            }
            if (seen.insert(s.key).second) model.stations.push_back(std::move(s));
        }
    }

    auto make_table_rows = [](const mini_json::Value& array) {
        std::vector<TableRow> rows;
        if (!array.is_array()) return rows;
        for (const auto& v : array.array) {
            TableRow r;
            if (v.is_object()) {
                for (const auto& kv : v.object) r.cells[kv.first] = table_cell_text(kv.second);
                apply_table_row_edit_metadata(r, v);
            }
            rows.push_back(std::move(r));
        }
        return rows;
    };
    const auto& structure = root.at("structure");
    model.structures = make_table_rows(structure.at("data"));
    model.structure_models = make_table_rows(structure.at("models"));
    const auto& other_train = root.at("otherTrain");
    model.other_trains = make_table_rows(other_train.at("definitions"));
    model.other_train_stops = make_table_rows(other_train.at("stop"));
    model.other_train_structure_keys = make_table_rows(other_train.at("structureKeys"));
    model.other_train_sound_3d_keys = make_table_rows(other_train.at("sound3DKeys"));
    const auto& signal = root.at("signal");
    const auto& signal_aspects = signal.at("aspects");
    if (signal_aspects.is_array()) {
        for (const auto& item : signal_aspects.array) {
            if (!item.is_object()) continue;
            TableRow row;
            row.cells["signalAspectKey"] = item.at("signalAspectKey").scalar_text();
            const auto& structure_keys = item.at("structureKeys");
            size_t structure_key_count = 0;
            if (structure_keys.is_array()) {
                structure_key_count = structure_keys.array.size();
                for (size_t i = 0; i < structure_keys.array.size(); ++i) {
                    row.cells["structureKey" + std::to_string(i + 1)] =
                        structure_keys.array[i].scalar_text();
                }
            }
            row.cells["_structureKeyCount"] = std::to_string(structure_key_count);
            apply_table_row_edit_metadata(row, item);
            model.signal_aspects.push_back(std::move(row));
        }
    }
    model.signals = make_table_rows(signal.at("data"));
    model.beacons = make_table_rows(root.at("beacon"));
    model.pretrains = make_table_rows(root.at("preTrain"));
    model.sound_list = make_table_rows(root.at("soundList"));
    model.structures_between = make_table_rows(structure.at("between_data"));
    model.repeaters = make_table_rows(root.at("repeater"));
    model.irregularities = make_table_rows(root.at("irregularity"));
    model.map_sounds = make_table_rows(root.at("mapSound"));
    model.map_sound_3d = make_table_rows(root.at("mapSound3D"));
    model.rolling_noises = make_table_rows(root.at("rollingNoise"));
    model.flange_noises = make_table_rows(root.at("flangeNoise"));
    model.joint_noises = make_table_rows(root.at("jointNoise"));
    model.backgrounds = make_table_rows(root.at("background"));
    model.adhesions = make_table_rows(root.at("adhesion"));
    model.cab_illuminance = make_table_rows(root.at("cabIlluminance"));
    model.fogs = make_table_rows(root.at("fog"));

    std::map<std::string, TableRow> station_rows_by_key;
    const auto& station_list = station.at("list");
    if (station_list.is_object()) {
        for (const auto& kv : station_list.object) {
            TableRow row;
            if (kv.second.is_object()) {
                for (const auto& cell : kv.second.object) row.cells[cell.first] = table_cell_text(cell.second);
                apply_table_row_edit_metadata(row, kv.second);
            }
            if (table_cell(row, "stationKey").empty()) row.cells["stationKey"] = kv.first;
            station_rows_by_key[ascii_lower(kv.first)] = std::move(row);
        }
    }
    auto append_station_table_row = [&](TableRow row, const std::string& key) {
        auto it = station_rows_by_key.find(ascii_lower(key));
        if (it != station_rows_by_key.end()) {
            for (const auto& cell : it->second.cells) row.cells[cell.first] = cell.second;
        }
        model.station_list_rows.push_back(std::move(row));
    };
    const auto& station_puts = station.at("put");
    if (station_puts.is_array()) {
        for (const auto& item : station_puts.array) {
            if (!item.is_object()) continue;
            std::string key = item.at("stationKey").scalar_text();
            double distance = item.at("distance").number;
            TableRow row;
            row.cells["_distance"] = format_double(distance);
            row.cells["_order"] = item.at("order").scalar_text();
            row.cells["dist"] = format_double(distance - model.distance_origin, 0);
            row.cells["posKey"] = key;
            row.cells["door"] = item.at("door").scalar_text();
            row.cells["margin1"] = item.at("margin1").scalar_text();
            row.cells["margin2"] = item.at("margin2").scalar_text();
            apply_table_row_edit_metadata(row, item);
            append_station_table_row(std::move(row), key);
        }
    } else if (positions.is_array()) {
        int order_index = 0;
        for (const auto& item : positions.array) {
            if (!item.is_array() || item.array.size() < 2) continue;
            std::string key = item.array[1].scalar_text();
            double distance = item.array[0].number;
            TableRow row;
            row.cells["_distance"] = format_double(distance);
            row.cells["_order"] = std::to_string(++order_index);
            row.cells["dist"] = format_double(distance - model.distance_origin, 0);
            row.cells["posKey"] = key;
            append_station_table_row(std::move(row), key);
        }
    }
    std::stable_sort(model.station_list_rows.begin(), model.station_list_rows.end(), [](const TableRow& a, const TableRow& b) {
        double da = table_cell_number(a, "_distance");
        double db = table_cell_number(b, "_distance");
        if (da != db) return da < db;
        return table_cell_number(a, "_order") < table_cell_number(b, "_order");
    });
    for (size_t i = 0; i < model.station_list_rows.size(); ++i) {
        model.station_list_rows[i].cells["rowNumber"] = std::to_string(i + 1);
    }

    if (!model.stations.empty()) {
        double mn = model.stations.front().distance;
        double mx = model.stations.front().distance;
        for (const auto& s : model.stations) {
            mn = std::min(mn, s.distance);
            mx = std::max(mx, s.distance);
        }
        model.default_min = round_to_100(mn) - 500.0;
        model.default_max = round_to_100(mx) + 500.0;
    } else if (!model.controlpoints.empty()) {
        auto [mn, mx] = std::minmax_element(model.controlpoints.begin(), model.controlpoints.end());
        model.default_min = round_to_100(*mn) - 500.0;
        model.default_max = round_to_100(*mx) + 500.0;
    } else if (!model.own.empty()) {
        model.default_min = model.own.at(0, 0);
        model.default_max = model.own.at(model.own.rows - 1, 0);
    }
    model.buffer_copy_seconds = buffer_copy_seconds;
    model.ir_json_seconds = ir_json_seconds;
    model.json_parse_seconds = json_parse_seconds;
    model.model_hydrate_seconds = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - hydrate_started_at).count();
    annotate_scene_track_key_warnings(model);
    return model;
}

bool App::ensure_full_edit_registry() {
    if (!handle_ || !has_model_) return false;
    if (edit_registry_loaded_ && !model_.edit_statements.empty()) return true;
    if (!rehydrate_model_from_current_handle(LoadModelOptions{true})) return false;
    add_log("[info]gui_kme.cpp: full edit registry loaded");
    return true;
}

bool App::rehydrate_model_from_current_handle(LoadModelOptions options) {
    if (!handle_ || !has_model_) return false;
    try {
        std::map<std::string, OtherTrack> old_other;
        for (const auto& track : model_.other_tracks) old_other[track.key] = track;
        const bool had_cp_arb = model_.has_cp_arb;
        std::array<double, 3> old_cp{model_.cp_arb[0], model_.cp_arb[1], model_.cp_arb[2]};

        MapModel updated = build_model_from_handle(handle_, file_path_, options);
        for (auto& track : updated.other_tracks) {
            auto it = old_other.find(track.key);
            if (it != old_other.end()) {
                track.visible = it->second.visible;
                track.color = it->second.color;
                track.range_min = it->second.range_min;
                track.range_max = it->second.range_max;
            }
        }
        updated.has_cp_arb = had_cp_arb;
        std::copy(std::begin(old_cp), std::end(old_cp), std::begin(updated.cp_arb));
        model_ = std::move(updated);
        edit_registry_loaded_ = options.full_edit_registry;
        invalidate_table_cache();
        rebuild_marker_overlay_cache();
        sync_marker_visibility_sizes();
        scene_preview_dirty_ = true;
        if (scene_preview_started_) {
            scene_preview_preserve_models_on_rebuild_ = true;
            scene_preview_preserve_camera_on_rebuild_ = true;
        }
        for (const std::string& warning : model_.scene_track_key_warnings) add_log(warning);
        return true;
    } catch (const std::exception& e) {
        add_log(std::string("[error]gui_kme.cpp: failed to refresh map cache: ") + e.what());
        return false;
    }
}

void App::clear_pending_edit_state() {
    pending_edit_changes_.clear();
    applied_unsaved_edit_ids_.clear();
    has_unsaved_edits_ = false;
    inspector_ = MapElementInspectorState{};
}

bool App::row_has_pending_edit(const std::string& edit_id) const {
    return !edit_id.empty() &&
        (pending_edit_changes_.find(edit_id) != pending_edit_changes_.end() ||
         applied_unsaved_edit_ids_.find(edit_id) != applied_unsaved_edit_ids_.end());
}

bool App::row_is_pending_delete(const std::string& edit_id) const {
    auto it = pending_edit_changes_.find(edit_id);
    return it != pending_edit_changes_.end() && it->second.operation == "delete";
}

void App::request_element_inspector(const std::string& edit_id, const std::string& row_kind) {
    if (edit_id.empty()) return;
    pending_inspector_request_ = MapElementInspectorRequest{edit_id, row_kind};
}

void App::process_pending_element_inspector() {
    if (!pending_inspector_request_) return;
    MapElementInspectorRequest request = std::move(*pending_inspector_request_);
    pending_inspector_request_.reset();
    open_element_inspector(request);
}

const EditSourceFileInfo* find_model_source_file(const MapModel& model, const std::string& path) {
    for (const EditSourceFileInfo& file : model.edit_files) {
        if (file.file_path == path) return &file;
    }
    return nullptr;
}

const EditElementInfo* find_model_edit_element(const MapModel& model, const std::string& edit_id) {
    for (const EditElementInfo& element : model.edit_elements) {
        if (element.edit_id == edit_id) return &element;
    }
    return nullptr;
}

const EditStatementInfo* find_model_statement_for_element(const MapModel& model,
                                                          const EditElementInfo& element) {
    for (const EditStatementInfo& statement : model.edit_statements) {
        if (statement.global_order == element.global_order &&
            statement.source.file_path == element.source_file_path) {
            return &statement;
        }
    }
    return nullptr;
}

const std::vector<TableRow>* inspector_rows_for_kind(const MapModel& model,
                                                     const std::string& row_kind) {
    if (row_kind == "structure.model") return &model.structure_models;
    if (row_kind == "structure.put") return &model.structures;
    if (row_kind == "structure.between") return &model.structures_between;
    if (row_kind == "station.put") return &model.station_list_rows;
    return nullptr;
}

std::string inspector_row_field_value(const TableRow& row,
                                      const std::string& row_kind,
                                      const std::string& field_key) {
    if (row_kind == "station.put") {
        if (field_key == "distance") return table_cell(row, "_distance");
        if (field_key == "stationKey") return table_cell(row, "posKey");
    }
    return table_cell(row, field_key);
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
                                                     std::string& resolved_edit_id) {
    const std::vector<TableRow>* rows = inspector_rows_for_kind(model, request.row_kind);
    if (!rows) return nullptr;

    for (const TableRow& row : *rows) {
        if (!request.edit_id.empty() && row.edit_id == request.edit_id) {
            resolved_edit_id = request.edit_id;
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
    return best;
}

bool row_kind_has_source_distance_string(const std::string& row_kind) {
    static constexpr std::array<const char*, 3> kDistanceRowKinds = {
        "station.put",
        "structure.put",
        "structure.between",
    };
    return std::any_of(kDistanceRowKinds.begin(), kDistanceRowKinds.end(),
                       [&](const char* value) { return row_kind == value; });
}

MapElementInspectorRequest make_inspector_reload_request(const MapElementInspectorState& inspector) {
    MapElementInspectorRequest request;
    request.edit_id = inspector.edit_id;
    request.row_kind = inspector.row_kind;
    request.source_file = inspector.source_file;
    request.line = inspector.line;
    request.column = inspector.column;
    for (const MapElementEditFieldState& field : inspector.fields) {
        request.field_values[field.key] = trim_gui_ascii_copy(edit_field_buffer_text(field));
    }
    return request;
}

bool App::open_element_inspector(const MapElementInspectorRequest& request) {
    if (request.edit_id.empty() && request.field_values.empty()) return false;
    if (!ensure_full_edit_registry()) return false;

    std::string edit_id = request.edit_id;
    const TableRow* row = find_model_row_for_inspector_request(model_, request, edit_id);
    if (!row) {
        add_log("[warn]gui_kme.cpp: edit target row not found: " + request.edit_id);
        return false;
    }

    const EditElementInfo* element = find_model_edit_element(model_, edit_id);
    const EditStatementInfo* statement = element ? find_model_statement_for_element(model_, *element) : nullptr;
    const EditSourceInfo& source = statement ? statement->source : row->source;
    const EditSourceFileInfo* source_file = find_model_source_file(model_, source.file_path);

    MapElementInspectorState next;
    next.open = true;
    next.edit_id = edit_id;
    next.row_kind = request.row_kind;
    next.title = tr("dialog.element_properties");
    next.source_file = source.file_path;
    next.source_hash = source_file ? source_file->source_hash : std::string{};
    next.line = source.line;
    next.column = source.column;
    if (statement && row_kind_has_source_distance_string(request.row_kind)) {
        next.source_distance_string = statement->distance_expression;
    }
    next.raw_statement = statement && !statement->raw_text.empty()
        ? statement->raw_text
        : source.raw_text_preview;
    next.delete_supported = true;

    auto add_field = [&](const std::string& key, const std::string& label,
                         const std::string& value, bool numeric, bool required) {
        MapElementEditFieldState field;
        field.key = key;
        field.label = label;
        field.original_value = value;
        field.numeric = numeric;
        field.required = required;
        set_edit_field_buffer(field, value);
        next.fields.push_back(field);
    };

    if (request.row_kind == "structure.model") {
        add_field("structureKey", "structureKey", table_cell(*row, "structureKey"), false, true);
        add_field("filePath", "filePath", table_cell(*row, "filePath"), false, true);
    } else if (request.row_kind == "structure.put") {
        const std::string method = table_cell(*row, "method");
        add_field("distance", "distance", table_cell(*row, "distance"), true, true);
        add_field("structureKey", "structureKey", table_cell(*row, "structureKey"), false, true);
        add_field("trackKey", "trackKey", table_cell(*row, "trackKey"), false, true);
        if (ascii_lower(method) != "put0") {
            add_field("x", "x", table_cell(*row, "x"), true, true);
            add_field("y", "y", table_cell(*row, "y"), true, true);
            add_field("z", "z", table_cell(*row, "z"), true, true);
            add_field("rx", "rx", table_cell(*row, "rx"), true, true);
            add_field("ry", "ry", table_cell(*row, "ry"), true, true);
            add_field("rz", "rz", table_cell(*row, "rz"), true, true);
        }
        add_field("tilt", "tilt", table_cell(*row, "tilt"), true, true);
        add_field("span", "span", table_cell(*row, "span"), true, true);
    } else if (request.row_kind == "structure.between") {
        add_field("distance", "distance", table_cell(*row, "distance"), true, true);
        add_field("structureKey", "structureKey", table_cell(*row, "structureKey"), false, true);
        add_field("trackKey1", "trackKey1", table_cell(*row, "trackKey1"), false, true);
        add_field("trackKey2", "trackKey2", table_cell(*row, "trackKey2"), false, true);
        add_field("flag", "flag", table_cell(*row, "flag"), true, true);
    } else if (request.row_kind == "station.put") {
        add_field("distance", "distance", table_cell(*row, "_distance"), true, true);
        add_field("stationKey", "stationKey", table_cell(*row, "posKey"), false, true);
    }

    auto pending = pending_edit_changes_.find(edit_id);
    if (pending != pending_edit_changes_.end()) {
        next.pending_delete = pending->second.operation == "delete";
        for (MapElementEditFieldState& field : next.fields) {
            auto field_it = pending->second.field_changes.find(field.key);
            if (field_it != pending->second.field_changes.end()) {
                set_edit_field_buffer(field, field_it->second);
            }
        }
    }

    inspector_ = std::move(next);
    return true;
}

bool App::open_element_inspector(const std::string& edit_id, const std::string& row_kind) {
    return open_element_inspector(MapElementInspectorRequest{edit_id, row_kind});
}

void App::apply_inspector_changes() {
    if (!inspector_.open || inspector_.edit_id.empty()) return;
    if (inspector_.pending_delete) {
        apply_pending_edits_to_memory();
        return;
    }

    MapElementPendingChange change;
    change.change_id = "change-" + inspector_.edit_id;
    change.edit_id = inspector_.edit_id;
    change.operation = "update";
    change.expected_source_hash = inspector_.source_hash;

    for (const MapElementEditFieldState& field : inspector_.fields) {
        std::string value = trim_gui_ascii_copy(edit_field_buffer_text(field));
        if (field.required && value.empty()) {
            inspector_.status_message = tr("status.edit.required_field");
            return;
        }
        if (field.numeric && !parse_gui_edit_number(value)) {
            inspector_.status_message = tr("status.edit.invalid_number");
            return;
        }
        if (value != field.original_value) {
            change.field_changes[field.key] = value;
        }
    }

    if (change.field_changes.empty()) {
        pending_edit_changes_.erase(inspector_.edit_id);
        inspector_.status_message = tr("status.edit.no_changes");
        return;
    }

    pending_edit_changes_[change.edit_id] = std::move(change);
    if (!apply_pending_edits_to_memory()) {
        inspector_.status_message = tr("status.edit.pending");
    }
}

void App::revert_inspector_changes() {
    if (!inspector_.open || inspector_.edit_id.empty()) return;
    pending_edit_changes_.erase(inspector_.edit_id);
    inspector_.pending_delete = false;
    for (MapElementEditFieldState& field : inspector_.fields) {
        set_edit_field_buffer(field, field.original_value);
    }
    inspector_.status_message = tr("status.edit.reverted");
}

void App::delete_inspector_target() {
    if (!inspector_.open || inspector_.edit_id.empty() || !inspector_.delete_supported) return;
    MapElementPendingChange change;
    change.change_id = "delete-" + inspector_.edit_id;
    change.edit_id = inspector_.edit_id;
    change.operation = "delete";
    change.expected_source_hash = inspector_.source_hash;
    pending_edit_changes_[change.edit_id] = std::move(change);
    inspector_.pending_delete = true;
    inspector_.status_message = tr("status.edit.pending_delete");
}

bool App::parse_and_log_edit_report(const std::string& report_text,
                                    const std::string& success_prefix,
                                    int* update_count,
                                    int* delete_count,
                                    int* changed_file_count) {
    bool ok = false;
    try {
        auto report = mini_json::Parser(report_text).parse();
        ok = report.at("ok").boolean;
        const auto& warnings = report.at("warnings");
        if (warnings.is_array()) {
            for (const auto& item : warnings.array) {
                add_log("[warn]gui_kme.cpp: " + item.scalar_text());
            }
        }
        const auto& errors = report.at("blockingErrors");
        if (errors.is_array()) {
            for (const auto& item : errors.array) {
                add_log("[error]gui_kme.cpp: " + item.scalar_text());
            }
        }
        const int updates = static_cast<int>(report.at("updateCount").number);
        const int deletes = static_cast<int>(report.at("deleteCount").number);
        const auto& changed_files = report.at("changedFiles");
        const int files = changed_files.is_array()
            ? static_cast<int>(changed_files.array.size())
            : 0;
        if (update_count) *update_count = updates;
        if (delete_count) *delete_count = deletes;
        if (changed_file_count) *changed_file_count = files;
        if (ok && !success_prefix.empty()) {
            add_log(success_prefix + ": updates=" + std::to_string(updates) +
                    ", deletes=" + std::to_string(deletes) +
                    ", files=" + std::to_string(files));
        }
    } catch (const std::exception& e) {
        add_log(std::string("[error]gui_kme.cpp: failed to parse edit report: ") + e.what());
        add_log(report_text);
        return false;
    }
    return ok;
}

bool App::apply_pending_edits_to_memory() {
    if (!handle_ || pending_edit_changes_.empty() || load_state_.running) return false;

    std::vector<std::string> applied_ids;
    applied_ids.reserve(pending_edit_changes_.size());
    bool applying_delete = false;
    for (const auto& kv : pending_edit_changes_) {
        applied_ids.push_back(kv.first);
        applying_delete = applying_delete || kv.second.operation == "delete";
    }
    std::optional<MapElementInspectorRequest> reload_request;
    if (inspector_.open && !inspector_.edit_id.empty() && !applying_delete) {
        reload_request = make_inspector_reload_request(inspector_);
    }

    std::string json = pending_changes_json();
    const char* raw = kv_edit_apply_to_memory(handle_, json.c_str());
    if (!raw) {
        const char* err = kv_get_last_error();
        add_log(std::string("[error]gui_kme.cpp: edit apply failed: ") + (err ? err : "unknown error"));
        return false;
    }
    std::string report_text(raw);
    kv_free_string(raw);

    if (!parse_and_log_edit_report(report_text, "[info]gui_kme.cpp: edit applied to preview")) {
        return false;
    }
    if (!rehydrate_model_from_current_handle(LoadModelOptions{true})) return false;

    for (const std::string& edit_id : applied_ids) {
        applied_unsaved_edit_ids_.insert(edit_id);
    }
    pending_edit_changes_.clear();
    has_unsaved_edits_ = true;
    if (reload_request) {
        pending_inspector_request_ = std::move(reload_request);
    } else if (applying_delete) {
        inspector_ = MapElementInspectorState{};
    } else {
        inspector_.status_message = tr("status.edit.applied_to_preview");
    }
    return true;
}

std::string App::pending_changes_json() const {
    std::ostringstream out;
    out << "{\"changes\":[";
    bool first_change = true;
    for (const auto& kv : pending_edit_changes_) {
        const MapElementPendingChange& change = kv.second;
        if (!first_change) out << ",";
        first_change = false;
        out << "{\"changeId\":";
        append_gui_json_string(out, change.change_id);
        out << ",\"editId\":";
        append_gui_json_string(out, change.edit_id);
        out << ",\"operation\":";
        append_gui_json_string(out, change.operation);
        out << ",\"expectedSourceHash\":";
        append_gui_json_string(out, change.expected_source_hash);
        out << ",\"fieldChanges\":{";
        bool first_field = true;
        for (const auto& field : change.field_changes) {
            if (!first_field) out << ",";
            first_field = false;
            append_gui_json_string(out, field.first);
            out << ":";
            append_gui_json_string(out, field.second);
        }
        out << "}}";
    }
    out << "]}";
    return out.str();
}

void App::save_pending_edits() {
    if (!handle_ || !has_unsaved_edits_ || load_state_.running) return;
    const char* raw = kv_edit_commit(handle_);
    if (!raw) {
        const char* err = kv_get_last_error();
        add_log(std::string("[error]gui_kme.cpp: edit save failed: ") + (err ? err : "unknown error"));
        return;
    }
    std::string report_text(raw);
    kv_free_string(raw);

    if (!parse_and_log_edit_report(report_text, "[info]gui_kme.cpp: edit save committed",
                                   nullptr, nullptr, nullptr)) {
        return;
    }
    pending_edit_changes_.clear();
    applied_unsaved_edit_ids_.clear();
    has_unsaved_edits_ = false;
    if (inspector_.open) inspector_.status_message = tr("status.edit.saved");
}

void App::render_element_inspector() {
    if (!inspector_.open) return;
    std::string title = tr("dialog.element_properties") + "###ElementInspector";
    if (!ImGui::Begin(title.c_str(), &inspector_.open)) {
        ImGui::End();
        return;
    }

    const bool dirty = row_has_pending_edit(inspector_.edit_id);
    if (dirty) {
        ImGui::TextUnformatted(tr(row_is_pending_delete(inspector_.edit_id)
            ? "status.edit.pending_delete"
            : "status.edit.pending").c_str());
    }
    render_inline_wrapped_text(tr("label.source_file").c_str(), inspector_.source_file);
    ImGui::Text("%s %d:%d", tr("label.source_position").c_str(), inspector_.line, inspector_.column);
    if (!inspector_.source_distance_string.empty()) {
        render_inline_wrapped_text(tr("label.source_distance_string").c_str(),
                                   inspector_.source_distance_string);
    }
    if (!inspector_.raw_statement.empty()) {
        ImGui::Separator();
        ImGui::TextUnformatted(tr("label.raw_statement").c_str());
        ImGui::TextWrapped("%s", inspector_.raw_statement.c_str());
    }

    ImGui::Separator();
    ImGui::BeginDisabled(inspector_.pending_delete);
    for (MapElementEditFieldState& field : inspector_.fields) {
        const bool changed = edit_field_buffer_text(field) != field.original_value;
        if (changed) ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.28f, 0.23f, 0.08f, 1.0f));
        ImGui::SetNextItemWidth(std::max(160.0f, ImGui::GetContentRegionAvail().x * 0.55f));
        ImGui::InputText(field.label.c_str(), field.value, sizeof(field.value));
        if (changed) ImGui::PopStyleColor();
    }
    ImGui::EndDisabled();

    if (!inspector_.status_message.empty()) {
        ImGui::TextUnformatted(inspector_.status_message.c_str());
    }

    if (ImGui::Button(tr("button.apply").c_str())) apply_inspector_changes();
    ImGui::SameLine();
    if (ImGui::Button(tr("button.revert").c_str())) revert_inspector_changes();
    ImGui::SameLine();
    ImGui::BeginDisabled(!inspector_.delete_supported);
    if (ImGui::Button(tr("button.delete").c_str())) delete_inspector_target();
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button(tr("button.close").c_str())) inspector_.open = false;

    ImGui::End();
}

std::string App::open_map_dialog() {
    wchar_t file[MAX_PATH] = {};
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = nullptr;
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrFilter = L"BVE map files\0*.map;*.txt\0All files\0*.*\0";
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

void App::touch_recent_map(const std::string& path) {
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
    kept.insert(kept.begin(), std::move(selected));
    if (kept.size() > kMaxRecentMaps) kept.resize(kMaxRecentMaps);
    recent_maps_ = std::move(kept);
    save_history();
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
    std::string stored_path = normalized_storage_path(file_path_);
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
    selected.background = current_background_history();
    kept.insert(kept.begin(), std::move(selected));
    if (kept.size() > kMaxRecentMaps) kept.resize(kMaxRecentMaps);
    recent_maps_ = std::move(kept);
    save_history();
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

void App::export_csv() {
    if (!has_model_) return;
    std::string folder = choose_folder_dialog();
    if (folder.empty()) return;
    std::filesystem::path dir = utf8_to_wide(folder);
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
    ImGui::DockBuilderDockWindow("Console", dock_console);
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
    view.scene_draw_distance_m = scene_draw_distance_m_;
    return view;
}

void App::apply_view_3d_settings(const View3DSettings& settings) {
    show_scene_owntrack_markers_ = settings.show_scene_owntrack_markers;
    show_scene_current_position_on_plan_ = settings.show_scene_current_position_on_plan;
    scene_draw_distance_m_ = clamp_scene_draw_distance(settings.scene_draw_distance_m);
    apply_scene_draw_distance_to_canvas(scene_draw_distance_m_);
}

void App::apply_scene_draw_distance_to_canvas(int distance_m) {
    if (scene_preview_canvas_) {
        scene_preview_canvas_->set_scene_window(kSceneWindowBackDistanceM,
                                                static_cast<double>(clamp_scene_draw_distance(distance_m)));
    }
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
        if (ImGui::MenuItem(tr("menu.exit").c_str())) PostQuitMessage(0);
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
            popups_.canvas_3d_settings = true;
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu(tr("menu.map_info").c_str())) {
        struct MapInfoMenuEntry {
            const char* label_key;
            bool App::*window_visible;
        };
        static constexpr std::array<MapInfoMenuEntry, 28> kMapInfoMenuEntries = {{
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
            {"menu.map_info.beacons", &App::show_beacons_window_},
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
        }};
        bool has_category = false;
        for (const MapInfoMenuEntry& entry : kMapInfoMenuEntries) {
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
        ImGui::MenuItem(tr("chk.curve_val").c_str(), nullptr, &show_curve_values_);
        ImGui::MenuItem(tr("chk.irregularity_markers").c_str(), nullptr, &show_irregularity_markers_);
        ImGui::MenuItem(tr("chk.adhesion_markers").c_str(), nullptr, &show_adhesion_markers_);
        ImGui::Separator();
        ImGui::MenuItem(tr("aux.signal").c_str(), nullptr, false, false);
        ImGui::MenuItem(tr("chk.speedlimit").c_str(), nullptr, &show_speedlimits_);
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
        ImGui::Separator();
        ImGui::MenuItem(tr("aux.scene_3d").c_str(), nullptr, false, false);
        if (ImGui::MenuItem(tr("chk.scene_owntrack_markers").c_str(), nullptr, &show_scene_owntrack_markers_)) {
            sync_scene_preview_track_visibility();
        }
        ImGui::MenuItem(tr("chk.scene_current_position_on_plan").c_str(), nullptr,
                        &show_scene_current_position_on_plan_, scene_preview_started_);
        ImGui::Separator();
        ImGui::MenuItem(tr("aux.other").c_str(), nullptr, false, false);
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
    ImVec4 toolbar_bg = style.Colors[ImGuiCol_WindowBg];
    toolbar_bg.x = std::min(toolbar_bg.x + 0.035f, 1.0f);
    toolbar_bg.y = std::min(toolbar_bg.y + 0.035f, 1.0f);
    toolbar_bg.z = std::min(toolbar_bg.z + 0.035f, 1.0f);

    const float button_height = ImGui::GetFrameHeight();
    const float toolbar_padding_y = button_height * 0.25f;
    const float toolbar_height = button_height + toolbar_padding_y * 2.0f;
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings;
    ImGui::PushStyleColor(ImGuiCol_WindowBg, toolbar_bg);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(style.WindowPadding.x, toolbar_padding_y));
    bool visible = ImGui::BeginViewportSideBar("##MainToolbar", ImGui::GetMainViewport(), ImGuiDir_Up, toolbar_height, flags);
    if (visible) {
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

        ImGui::SameLine();
        ImGui::BeginDisabled(!has_unsaved_edits_ || load_state_.running);
        if (ImGui::Button(tr("button.save").c_str())) save_pending_edits();
        ImGui::EndDisabled();

        ImGui::SameLine(0.0f, style.ItemSpacing.x);
        ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
        ImGui::SameLine(0.0f, style.ItemSpacing.x);
        render_station_jump_combo();
        ImGui::SameLine(0.0f, style.ItemSpacing.x);
        render_distance_jump_control();
    }
    ImGui::End();
    ImGui::PopStyleVar();
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
        last_log_.clear();
        error_count_ = warn_count_ = 0;
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
    ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "E %d", error_count_);
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(1.0f, 0.78f, 0.25f, 1.0f), "W %d", warn_count_);
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
    for (const auto& line : logs_) {
        ImVec4 color = line.severity == 2 ? ImVec4(1.0f, 0.35f, 0.35f, 1.0f)
                     : line.severity == 1 ? ImVec4(1.0f, 0.78f, 0.25f, 1.0f)
                                          : ImVec4(0.88f, 0.88f, 0.88f, 1.0f);
        ImGui::TextColored(color, "%s", line.text.c_str());
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
        if (ImGui::SliderFloat(tr("label.font_size").c_str(), &pending_font_size_, kMinFontSize, kMaxFontSize, "%.0f px", ImGuiSliderFlags_AlwaysClamp)) {
            pending_font_size_ = clamp_font_size(pending_font_size_);
            apply_pending_ui_settings();
        }
        ImGui::SetNextItemWidth(260.0f);
        if (ImGui::SliderFloat(tr("label.ui_component_size").c_str(), &pending_ui_component_size_, kMinUiComponentSize, kMaxUiComponentSize, "%.0f%%", ImGuiSliderFlags_AlwaysClamp)) {
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
        int marker_size_steps = static_cast<int>(clamp_marker_size_percent(pending_marker_size_percent_) / kMarkerSizePercentStep);
        if (ImGui::SliderInt(tr("label.station_marker_size").c_str(),
                             &marker_size_steps,
                             static_cast<int>(kMinMarkerSizePercent) / kMarkerSizePercentStep,
                             static_cast<int>(kMaxMarkerSizePercent) / kMarkerSizePercentStep,
                             "%d0%%",
                             ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_NoInput)) {
            pending_marker_size_percent_ = clamp_marker_size_percent(static_cast<float>(marker_size_steps * kMarkerSizePercentStep));
            marker_size_percent_ = pending_marker_size_percent_;
        }
        auto line_width_slider = [&](const char* label_key, float* value, float fallback) {
            ImGui::SetNextItemWidth(260.0f);
            int width_steps = static_cast<int>(std::round(clamp_canvas_line_width(*value, fallback) /
                                                          kCanvasLineWidthStepPx));
            const int min_steps = static_cast<int>(std::round(kMinCanvasLineWidthPx / kCanvasLineWidthStepPx));
            const int max_steps = static_cast<int>(std::round(kMaxCanvasLineWidthPx / kCanvasLineWidthStepPx));
            std::string slider_id = std::string("##") + label_key;
            if (ImGui::SliderInt(slider_id.c_str(),
                                 &width_steps,
                                 min_steps,
                                 max_steps,
                                 "",
                                 ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_NoInput)) {
                *value = clamp_canvas_line_width(static_cast<float>(width_steps) * kCanvasLineWidthStepPx,
                                                 fallback);
                pending_canvas_line_widths_ = clamp_canvas_line_widths(pending_canvas_line_widths_);
                canvas_line_widths_ = pending_canvas_line_widths_;
            }
            ImVec2 slider_min = ImGui::GetItemRectMin();
            ImVec2 slider_max = ImGui::GetItemRectMax();
            std::string value_text = format_double(
                static_cast<double>(clamp_canvas_line_width(static_cast<float>(width_steps) *
                                                            kCanvasLineWidthStepPx,
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
                          kDefaultOwnTrackLineWidthPx);
        line_width_slider("label.other_track_line_width",
                          &pending_canvas_line_widths_.other_track_px,
                          kDefaultOtherTrackLineWidthPx);
        line_width_slider("label.chart_marker_line_width",
                          &pending_canvas_line_widths_.chart_marker_px,
                          kDefaultChartMarkerLineWidthPx);
        line_width_slider("label.background_grid_line_width",
                          &pending_canvas_line_widths_.background_grid_px,
                          kDefaultBackgroundGridLineWidthPx);
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
        int draw_distance_chunks = clamp_scene_draw_distance(pending_scene_draw_distance_m_) / kSceneDrawDistanceStepM;
        ImGui::SetNextItemWidth(300.0f);
        if (ImGui::SliderInt(tr("label.scene_draw_distance").c_str(),
                             &draw_distance_chunks,
                             kMinSceneDrawDistanceM / kSceneDrawDistanceStepM,
                             kMaxSceneDrawDistanceM / kSceneDrawDistanceStepM,
                             "%d00 m",
                             ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_NoInput)) {
            pending_scene_draw_distance_m_ = clamp_scene_draw_distance(draw_distance_chunks * kSceneDrawDistanceStepM);
            apply_scene_draw_distance_to_canvas(pending_scene_draw_distance_m_);
        }
        if (ImGui::Button(tr("button.ok").c_str())) {
            scene_draw_distance_m_ = clamp_scene_draw_distance(pending_scene_draw_distance_m_);
            pending_scene_draw_distance_m_ = scene_draw_distance_m_;
            scene_draw_distance_before_dialog_m_ = scene_draw_distance_m_;
            apply_scene_draw_distance_to_canvas(scene_draw_distance_m_);
            sync_runtime_settings_before_save();
            save_user_settings(settings_);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button(tr("button.cancel").c_str())) {
            pending_scene_draw_distance_m_ = scene_draw_distance_before_dialog_m_;
            apply_scene_draw_distance_to_canvas(scene_draw_distance_m_);
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    if (!canvas_3d_settings_popup_open) {
        pending_scene_draw_distance_m_ = scene_draw_distance_before_dialog_m_;
        apply_scene_draw_distance_to_canvas(scene_draw_distance_m_);
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
    add_log("[info]gui_kme.cpp: starting 3D scene preview");
    rebuild_scene_preview(false, false);
}

void App::stop_scene_preview() {
    scene_preview_started_ = false;
    scene_preview_dirty_ = true;
    scene_preview_preserve_models_on_rebuild_ = false;
    scene_preview_preserve_camera_on_rebuild_ = false;
    if (scene_preview_canvas_) scene_preview_canvas_->clear_scene();
    add_log("[INFO]3D scene preview stopped");
}

void App::rebuild_scene_preview(bool preserve_loaded_models, bool preserve_camera) {
    if (!scene_preview_canvas_ || !scene_preview_started_) return;
    if (!has_model_ || model_.own.empty()) {
        scene_preview_canvas_->clear_scene();
        scene_preview_dirty_ = true;
        scene_preview_preserve_models_on_rebuild_ = false;
        scene_preview_preserve_camera_on_rebuild_ = false;
        add_log("[warn]gui_kme.cpp: 3D scene preview has no map geometry loaded");
        return;
    }
    add_log(preserve_loaded_models
                ? "[info]gui_kme.cpp: reloading 3D scene preview track geometry with preserved models"
                : "[info]gui_kme.cpp: generating 3D scene preview track geometry");
    Canvas3DSceneBuildOptions options;
    options.model = &model_;
    options.map_handle = handle_;
    options.unit_distance = unit_distance_;
    options.control_point_start = cp_start_;
    options.control_point_end = cp_end_;
    options.control_point_interval = cp_interval_;
    options.station_index = station_jump_index_;
    options.show_own_track_markers = show_scene_owntrack_markers_;
    Canvas3DSceneBuildResult build_result = build_canvas3d_scene_preview(options);
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
        return;
    }
    scene_preview_dirty_ = false;
    scene_preview_preserve_models_on_rebuild_ = false;
    scene_preview_preserve_camera_on_rebuild_ = false;
    Canvas3DSceneStats stats = scene_preview_canvas_->scene_stats();
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

void App::render_scene_preview_window() {
    auto drain_scene_preview_logs = [this]() {
        if (!scene_preview_canvas_) return;
        for (std::string& message : scene_preview_canvas_->drain_scene_load_messages()) {
            add_log(std::move(message));
        }
    };
    drain_scene_preview_logs();
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
        ImGui::PopStyleVar();
        if (scene_preview_started_ && scene_preview_dirty_ && has_model_ && !load_state_.running) {
            const bool preserve_loaded_models = scene_preview_preserve_models_on_rebuild_;
            const bool preserve_camera = scene_preview_preserve_camera_on_rebuild_;
            rebuild_scene_preview(preserve_loaded_models, preserve_camera);
        }
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + toolbar_padding_y);
        ImVec2 avail = ImGui::GetContentRegionAvail();
        Canvas3DSceneUiText scene_ui_text;
        scene_ui_text.switch_signal_aspect = tr("menu.switch_signal_aspect");
        scene_ui_text.locate_structure_list = tr("menu.locate_in_structure_list");
        scene_ui_text.locate_structure_put_between_list = tr("menu.locate_in_structure_put_between_list");
        scene_ui_text.locate_repeater_list = tr("menu.locate_in_repeater_list");
        scene_ui_text.loading = tr("status.scene_loading");
        Canvas3DSceneContextAction scene_action = scene_preview_canvas_->render_scene_preview(avail, scene_ui_text);
        if (scene_action.kind == Canvas3DSceneContextActionKind::LocateStructure) {
            locate_structure_row_in_list(scene_action.row_index);
        } else if (scene_action.kind == Canvas3DSceneContextActionKind::LocateRepeater) {
            locate_repeater_row_in_list(scene_action.row_index);
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
    if ((!has_unsaved_edits_ && pending_edit_changes_.empty()) || !has_model_ || file_path_.empty()) return false;
    pending_reload_action_ = action;
    popups_.reload_unsaved_confirm = true;
    return true;
}

void App::execute_pending_reload_action() {
    PendingReloadAction action = pending_reload_action_;
    pending_reload_action_ = PendingReloadAction::None;
    clear_pending_edits_after_load_ = true;
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
    plan_canvas_rendered_this_frame_ = false;
    handle_shortcuts();
    render_menu();
    render_toolbar();
    ImGuiID dockspace_id = ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport());
    setup_initial_dockspace(dockspace_id);
    render_othertracks_window();
    render_station_list_window();
    render_console();
    render_plots();
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
    process_pending_element_inspector();
    render_element_inspector();
    render_popups();
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
        case kAppWakeMessage:
            return 0;
        case WM_SIZE:
            if (wParam == SIZE_MINIMIZED) return 0;
            g_ResizeWidth = LOWORD(lParam);
            g_ResizeHeight = HIWORD(lParam);
            return 0;
        case WM_SYSCOMMAND:
            if ((wParam & 0xfff0) == SC_KEYMENU) return 0;
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
                      << "[--window-back-m M] [--window-forward-m M] [--headless-output FILE]\n";
            return 1;
        }
        return App::run_debug_headless_scene3d_benchmark(scene3d_bench.path, scene3d_bench.frames,
                                                         scene3d_bench.unit_distance, scene3d_bench.max_frame_ms,
                                                         scene3d_bench.window_back_m,
                                                         scene3d_bench.window_forward_m,
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
                      << "[--repeat N] [--unit-distance M] [--ir-json-mode compact|full] "
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
            io.Fonts->AddFontFromFileTTF(font, kDefaultFontSize * scale, nullptr, io.Fonts->GetGlyphRangesChineseFull());
            font_loaded = true;
            break;
        }
    }
    if (!font_loaded) io.Fonts->AddFontDefault();

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
        if (SUCCEEDED(hr) && !g_SwapChainOccluded) app.after_frame_presented();
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
