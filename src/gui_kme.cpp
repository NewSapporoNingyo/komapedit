/*
 * Copyright (c) 2026 Sapporo_ningyo
 *
 * Licensed under Apache License 2.0; see LICENSE and NOTICE.
 * The GUI uses Dear ImGui and ImPlot; see THIRD_PARTY_NOTICES.md.
 */

#pragma execution_character_set("utf-8")

#include "kme.h"

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

std::string trim_ascii(const std::string& text) {
    size_t begin = 0;
    while (begin < text.size() && std::isspace(static_cast<unsigned char>(text[begin]))) ++begin;
    size_t end = text.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1]))) --end;
    return text.substr(begin, end - begin);
}

std::string ascii_lower(std::string text) {
    for (char& ch : text) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return text;
}

std::string normalized_storage_path(const std::string& path) {
    if (trim_ascii(path).empty()) return {};
    try {
        std::filesystem::path p = utf8_to_wide(path);
        std::error_code ec;
        std::filesystem::path abs = std::filesystem::absolute(p, ec);
        if (ec) abs = p;
        return narrow_path(abs.lexically_normal());
    } catch (...) {
        return path;
    }
}

std::string normalized_path_key(const std::string& path) {
    if (trim_ascii(path).empty()) return {};
    try {
        std::filesystem::path p = utf8_to_wide(path);
        std::error_code ec;
        std::filesystem::path abs = std::filesystem::absolute(p, ec);
        if (ec) abs = p;
        std::filesystem::path canonical = std::filesystem::weakly_canonical(abs, ec);
        if (!ec) abs = canonical;
        return ascii_lower(narrow_path(abs.lexically_normal()));
    } catch (...) {
        return ascii_lower(trim_ascii(path));
    }
}

std::string display_name_from_path(const std::string& path) {
    try {
        std::filesystem::path p = utf8_to_wide(path);
        std::string name = narrow_path(p.filename());
        return name.empty() ? path : name;
    } catch (...) {
        return path;
    }
}

float clamp_font_size(float value) {
    if (!std::isfinite(value)) return kDefaultFontSize;
    return std::clamp(value, kMinFontSize, kMaxFontSize);
}

float clamp_ui_component_size(float value) {
    if (!std::isfinite(value)) return kDefaultUiComponentSize;
    return std::clamp(value, kMinUiComponentSize, kMaxUiComponentSize);
}

float clamp_station_marker_size(float value) {
    if (!std::isfinite(value)) return kDefaultStationMarkerSize;
    return std::clamp(value, kMinStationMarkerSize, kMaxStationMarkerSize);
}

ImVec4 default_theme_color() {
    return ImVec4(0.26f, 0.59f, 0.98f, 1.0f);
}

float clamp_color_component(float value) {
    if (!std::isfinite(value)) return 0.0f;
    return std::clamp(value, 0.0f, 1.0f);
}

ImVec4 clamp_theme_color(ImVec4 color) {
    color.x = clamp_color_component(color.x);
    color.y = clamp_color_component(color.y);
    color.z = clamp_color_component(color.z);
    color.w = 1.0f;
    return color;
}

int color_component_to_byte(float value) {
    return std::clamp(static_cast<int>(std::round(clamp_color_component(value) * 255.0f)), 0, 255);
}

std::string theme_color_to_string(const ImVec4& color) {
    ImVec4 c = clamp_theme_color(color);
    std::ostringstream out;
    out << std::uppercase << std::hex << std::setfill('0')
        << std::setw(2) << color_component_to_byte(c.x)
        << std::setw(2) << color_component_to_byte(c.y)
        << std::setw(2) << color_component_to_byte(c.z);
    return out.str();
}

const std::array<ImVec4, 12>& ui_theme_palette() {
    static const std::array<ImVec4, 12> palette = {
        ImVec4(0.26f, 0.59f, 0.98f, 1.0f),
        ImVec4(0.00f, 0.74f, 0.83f, 1.0f),
        ImVec4(0.26f, 0.75f, 0.48f, 1.0f),
        ImVec4(0.60f, 0.78f, 0.20f, 1.0f),
        ImVec4(0.95f, 0.67f, 0.13f, 1.0f),
        ImVec4(0.95f, 0.42f, 0.18f, 1.0f),
        ImVec4(0.90f, 0.27f, 0.33f, 1.0f),
        ImVec4(0.88f, 0.31f, 0.55f, 1.0f),
        ImVec4(0.70f, 0.38f, 0.94f, 1.0f),
        ImVec4(0.46f, 0.45f, 0.95f, 1.0f),
        ImVec4(0.40f, 0.58f, 0.71f, 1.0f),
        ImVec4(0.58f, 0.63f, 0.68f, 1.0f),
    };
    return palette;
}

int hex_digit(char ch) {
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
    return -1;
}

std::optional<ImVec4> parse_theme_color(const std::string& text) {
    std::string value = trim_ascii(text);
    if (value.empty()) return std::nullopt;
    bool had_hash_prefix = value.front() == '#';
    if (had_hash_prefix) {
        value.erase(value.begin());
        size_t trailing = value.find_first_of(" \t\r\n");
        if (trailing != std::string::npos) value.erase(trailing);
    }
    if (value.size() > 2 && value[0] == '0' && (value[1] == 'x' || value[1] == 'X')) value.erase(0, 2);

    if (value.size() == 6 || value.size() == 8) {
        bool is_hex = true;
        for (char ch : value) {
            if (hex_digit(ch) < 0) {
                is_hex = false;
                break;
            }
        }
        if (is_hex) {
            auto byte_at = [&](size_t pos) {
                return hex_digit(value[pos]) * 16 + hex_digit(value[pos + 1]);
            };
            return clamp_theme_color(ImVec4(
                byte_at(0) / 255.0f,
                byte_at(2) / 255.0f,
                byte_at(4) / 255.0f,
                1.0f));
        }
    }

    std::array<float, 3> components{};
    std::istringstream in(value);
    std::string part;
    int count = 0;
    while (count < static_cast<int>(components.size()) && std::getline(in, part, ',')) {
        try {
            components[count++] = std::stof(trim_ascii(part));
        } catch (...) {
            return std::nullopt;
        }
    }
    if (count == static_cast<int>(components.size())) {
        bool byte_range = components[0] > 1.0f || components[1] > 1.0f || components[2] > 1.0f;
        if (byte_range) {
            for (float& component : components) component /= 255.0f;
        }
        return clamp_theme_color(ImVec4(components[0], components[1], components[2], 1.0f));
    }

    return std::nullopt;
}

ImVec4 with_alpha(ImVec4 color, float alpha) {
    color = clamp_theme_color(color);
    color.w = clamp_color_component(alpha);
    return color;
}

ImVec4 mix_color(ImVec4 a, ImVec4 b, float t, float alpha = 1.0f) {
    a = clamp_theme_color(a);
    b = clamp_theme_color(b);
    t = clamp_color_component(t);
    return ImVec4(
        a.x + (b.x - a.x) * t,
        a.y + (b.y - a.y) * t,
        a.z + (b.z - a.z) * t,
        clamp_color_component(alpha));
}

std::string language_to_string(Language lang) {
    switch (lang) {
        case Language::Ja: return "ja";
        case Language::En: return "en";
        case Language::Zh: return "zh";
    }
    return "zh";
}

Language language_from_string(const std::string& text, Language fallback) {
    std::string value = ascii_lower(trim_ascii(text));
    if (value == "ja" || value == "jp" || value == "japanese") return Language::Ja;
    if (value == "en" || value == "english") return Language::En;
    if (value == "zh" || value == "cn" || value == "chinese" || value == "simplified_chinese") return Language::Zh;
    return fallback;
}

std::filesystem::path executable_directory() {
    std::vector<wchar_t> buffer(MAX_PATH);
    while (true) {
        DWORD len = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (len == 0) break;
        if (len < buffer.size() - 1) {
            return std::filesystem::path(std::wstring(buffer.data(), len)).parent_path();
        }
        buffer.resize(buffer.size() * 2);
    }
    std::error_code ec;
    std::filesystem::path cwd = std::filesystem::current_path(ec);
    return ec ? std::filesystem::path(L".") : cwd;
}

std::filesystem::path default_settings_path() {
    return executable_directory() / L"settings.ini";
}

std::filesystem::path default_history_path() {
    return executable_directory() / L"history.ini";
}

std::filesystem::path default_imgui_ini_path() {
    return executable_directory() / L"imgui.ini";
}

std::string bool_to_string(bool value) {
    return value ? "true" : "false";
}

bool parse_bool(const std::string& value, bool fallback) {
    std::string text = ascii_lower(trim_ascii(value));
    if (text == "1" || text == "true" || text == "yes" || text == "on") return true;
    if (text == "0" || text == "false" || text == "no" || text == "off") return false;
    return fallback;
}

int normalize_view_2d_mode(int value) {
    return value == 1 ? 1 : 0;
}

int normalize_grid_mode(int value) {
    return value >= 0 && value <= 2 ? value : 0;
}

std::string view_2d_mode_to_string(int value) {
    return normalize_view_2d_mode(value) == 1 ? "measure" : "pan";
}

std::string grid_mode_to_string(int value) {
    switch (normalize_grid_mode(value)) {
        case 1: return "movable";
        case 2: return "none";
        default: return "fixed";
    }
}

int view_2d_mode_from_string(const std::string& value, int fallback) {
    std::string text = ascii_lower(trim_ascii(value));
    if (text == "1" || text == "measure" || text == "measurement") return 1;
    if (text == "0" || text == "pan" || text == "move") return 0;
    return normalize_view_2d_mode(fallback);
}

int grid_mode_from_string(const std::string& value, int fallback) {
    std::string text = ascii_lower(trim_ascii(value));
    if (text == "0" || text == "fixed") return 0;
    if (text == "1" || text == "movable" || text == "moveable") return 1;
    if (text == "2" || text == "none" || text == "off") return 2;
    return normalize_grid_mode(fallback);
}

bool save_user_settings(const UserSettings& settings) {
    std::ofstream out(settings.path, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out << "[General]\n";
    out << "language=" << language_to_string(settings.language) << "\n";
    out << "font_size=" << std::fixed << std::setprecision(1) << clamp_font_size(settings.font_size) << "\n";
    out << "ui_component_size=" << std::fixed << std::setprecision(1) << clamp_ui_component_size(settings.ui_component_size) << "\n";
    out << "station_marker_size=" << std::fixed << std::setprecision(1) << clamp_station_marker_size(settings.station_marker_size) << "\n";
    out << "theme_color=" << theme_color_to_string(settings.theme_color) << "\n";
    out << "\n[WindowVisibility]\n";
    out << "show_othertracks_window=" << bool_to_string(settings.window_visibility.show_othertracks_window) << "\n";
    out << "show_station_list_window=" << bool_to_string(settings.window_visibility.show_station_list_window) << "\n";
    out << "show_structures_window=" << bool_to_string(settings.window_visibility.show_structures_window) << "\n";
    out << "show_structure_models_window=" << bool_to_string(settings.window_visibility.show_structure_models_window) << "\n";
    out << "show_sound_list_window=" << bool_to_string(settings.window_visibility.show_sound_list_window) << "\n";
    out << "show_repeaters_window=" << bool_to_string(settings.window_visibility.show_repeaters_window) << "\n";
    out << "show_irregularities_window=" << bool_to_string(settings.window_visibility.show_irregularities_window) << "\n";
    out << "show_backgrounds_window=" << bool_to_string(settings.window_visibility.show_backgrounds_window) << "\n";
    out << "show_adhesions_window=" << bool_to_string(settings.window_visibility.show_adhesions_window) << "\n";
    out << "show_cab_illuminance_window=" << bool_to_string(settings.window_visibility.show_cab_illuminance_window) << "\n";
    out << "show_fogs_window=" << bool_to_string(settings.window_visibility.show_fogs_window) << "\n";
    out << "show_plots_window=" << bool_to_string(settings.window_visibility.show_plots_window) << "\n";
    out << "show_model_preview_window=" << bool_to_string(settings.window_visibility.show_model_preview_window) << "\n";
    out << "\n[View2D]\n";
    out << "show_stations=" << bool_to_string(settings.view_2d.show_stations) << "\n";
    out << "show_station_names=" << bool_to_string(settings.view_2d.show_station_names) << "\n";
    out << "show_station_mileage=" << bool_to_string(settings.view_2d.show_station_mileage) << "\n";
    out << "show_gradient_pos=" << bool_to_string(settings.view_2d.show_gradient_pos) << "\n";
    out << "show_gradient_values=" << bool_to_string(settings.view_2d.show_gradient_values) << "\n";
    out << "show_curve_values=" << bool_to_string(settings.view_2d.show_curve_values) << "\n";
    out << "show_profile_other=" << bool_to_string(settings.view_2d.show_profile_other) << "\n";
    out << "show_speedlimits=" << bool_to_string(settings.view_2d.show_speedlimits) << "\n";
    out << "show_irregularity_markers=" << bool_to_string(settings.view_2d.show_irregularity_markers) << "\n";
    out << "show_background_markers=" << bool_to_string(settings.view_2d.show_background_markers) << "\n";
    out << "show_adhesion_markers=" << bool_to_string(settings.view_2d.show_adhesion_markers) << "\n";
    out << "show_cab_illuminance_markers=" << bool_to_string(settings.view_2d.show_cab_illuminance_markers) << "\n";
    out << "show_fog_markers=" << bool_to_string(settings.view_2d.show_fog_markers) << "\n";
    out << "show_profile_graph=" << bool_to_string(settings.view_2d.show_profile_graph) << "\n";
    out << "show_radius_graph=" << bool_to_string(settings.view_2d.show_radius_graph) << "\n";
    out << "show_background_image=" << bool_to_string(settings.view_2d.show_background_image) << "\n";
    out << "mode=" << view_2d_mode_to_string(settings.view_2d.mode) << "\n";
    out << "grid_mode=" << grid_mode_to_string(settings.view_2d.grid_mode) << "\n";
    return true;
}

UserSettings load_user_settings() {
    UserSettings settings;
    settings.path = default_settings_path();

    std::error_code ec;
    bool exists = std::filesystem::exists(settings.path, ec);
    if (ec || !exists) {
        save_user_settings(settings);
        return settings;
    }

    std::ifstream in(settings.path, std::ios::binary);
    if (!in) return settings;

    std::string line;
    std::set<std::string> view_2d_keys_seen;
    while (std::getline(in, line)) {
        std::string trimmed_line = trim_ascii(line);
        if (trimmed_line.empty() || trimmed_line.front() == ';' || trimmed_line.front() == '#') continue;
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = ascii_lower(trim_ascii(line.substr(0, eq)));
        std::string value = trim_ascii(line.substr(eq + 1));
        bool is_theme_color_key = key == "theme_color" || key == "ui_theme_color" || key == "interface_theme_color" || key == "accent_color";
        size_t semicolon_comment = value.find(';');
        if (semicolon_comment != std::string::npos) value.erase(semicolon_comment);
        size_t hash_comment = value.find('#');
        if (hash_comment != std::string::npos && !(is_theme_color_key && hash_comment == 0)) value.erase(hash_comment);
        value = trim_ascii(value);
        if (key == "language" || key == "lang") {
            settings.language = language_from_string(value, settings.language);
        } else if (key == "font_size" || key == "fontsize" || key == "text_size") {
            try {
                settings.font_size = clamp_font_size(std::stof(value));
            } catch (...) {
                settings.font_size = kDefaultFontSize;
            }
        } else if (key == "ui_component_size" || key == "component_size" || key == "ui_scale" || key == "ui_component_scale") {
            try {
                settings.ui_component_size = clamp_ui_component_size(std::stof(value));
            } catch (...) {
                settings.ui_component_size = kDefaultUiComponentSize;
            }
        } else if (key == "station_marker_size" || key == "station_marker_radius" || key == "station_size") {
            try {
                settings.station_marker_size = clamp_station_marker_size(std::stof(value));
            } catch (...) {
                settings.station_marker_size = kDefaultStationMarkerSize;
            }
        } else if (is_theme_color_key) {
            if (auto color = parse_theme_color(value)) {
                settings.theme_color = *color;
            } else {
                settings.theme_color = default_theme_color();
            }
        } else if (key == "show_othertracks_window") {
            settings.window_visibility.show_othertracks_window = parse_bool(value, settings.window_visibility.show_othertracks_window);
        } else if (key == "show_station_list_window") {
            settings.window_visibility.show_station_list_window = parse_bool(value, settings.window_visibility.show_station_list_window);
        } else if (key == "show_structures_window") {
            settings.window_visibility.show_structures_window = parse_bool(value, settings.window_visibility.show_structures_window);
        } else if (key == "show_structure_models_window") {
            settings.window_visibility.show_structure_models_window = parse_bool(value, settings.window_visibility.show_structure_models_window);
        } else if (key == "show_sound_list_window" || key == "show_soundlist_window") {
            settings.window_visibility.show_sound_list_window = parse_bool(value, settings.window_visibility.show_sound_list_window);
        } else if (key == "show_repeaters_window") {
            settings.window_visibility.show_repeaters_window = parse_bool(value, settings.window_visibility.show_repeaters_window);
        } else if (key == "show_irregularities_window") {
            settings.window_visibility.show_irregularities_window = parse_bool(value, settings.window_visibility.show_irregularities_window);
        } else if (key == "show_backgrounds_window" || key == "show_background_window") {
            settings.window_visibility.show_backgrounds_window = parse_bool(value, settings.window_visibility.show_backgrounds_window);
        } else if (key == "show_adhesions_window" || key == "show_adhesion_window") {
            settings.window_visibility.show_adhesions_window = parse_bool(value, settings.window_visibility.show_adhesions_window);
        } else if (key == "show_cab_illuminance_window" || key == "show_cabilluminance_window") {
            settings.window_visibility.show_cab_illuminance_window = parse_bool(value, settings.window_visibility.show_cab_illuminance_window);
        } else if (key == "show_fogs_window" || key == "show_fog_window") {
            settings.window_visibility.show_fogs_window = parse_bool(value, settings.window_visibility.show_fogs_window);
        } else if (key == "show_plots_window") {
            settings.window_visibility.show_plots_window = parse_bool(value, settings.window_visibility.show_plots_window);
        } else if (key == "show_model_preview_window") {
            settings.window_visibility.show_model_preview_window = parse_bool(value, settings.window_visibility.show_model_preview_window);
        } else if (key == "show_stations" || key == "show_station_pos" || key == "show_station_positions") {
            view_2d_keys_seen.insert("show_stations");
            settings.view_2d.show_stations = parse_bool(value, settings.view_2d.show_stations);
        } else if (key == "show_station_names" || key == "show_station_name") {
            view_2d_keys_seen.insert("show_station_names");
            settings.view_2d.show_station_names = parse_bool(value, settings.view_2d.show_station_names);
        } else if (key == "show_station_mileage") {
            view_2d_keys_seen.insert("show_station_mileage");
            settings.view_2d.show_station_mileage = parse_bool(value, settings.view_2d.show_station_mileage);
        } else if (key == "show_gradient_pos" || key == "show_gradient_positions") {
            view_2d_keys_seen.insert("show_gradient_pos");
            settings.view_2d.show_gradient_pos = parse_bool(value, settings.view_2d.show_gradient_pos);
        } else if (key == "show_gradient_values" || key == "show_gradient_value") {
            view_2d_keys_seen.insert("show_gradient_values");
            settings.view_2d.show_gradient_values = parse_bool(value, settings.view_2d.show_gradient_values);
        } else if (key == "show_curve_values" || key == "show_curve_value") {
            view_2d_keys_seen.insert("show_curve_values");
            settings.view_2d.show_curve_values = parse_bool(value, settings.view_2d.show_curve_values);
        } else if (key == "show_profile_other" || key == "show_profile_othertracks") {
            view_2d_keys_seen.insert("show_profile_other");
            settings.view_2d.show_profile_other = parse_bool(value, settings.view_2d.show_profile_other);
        } else if (key == "show_speedlimits" || key == "show_speedlimit") {
            view_2d_keys_seen.insert("show_speedlimits");
            settings.view_2d.show_speedlimits = parse_bool(value, settings.view_2d.show_speedlimits);
        } else if (key == "show_irregularity_markers" || key == "show_irregularities" || key == "show_irregularity_points") {
            view_2d_keys_seen.insert("show_irregularity_markers");
            settings.view_2d.show_irregularity_markers = parse_bool(value, settings.view_2d.show_irregularity_markers);
        } else if (key == "show_background_markers" || key == "show_backgrounds" || key == "show_background_points") {
            view_2d_keys_seen.insert("show_background_markers");
            settings.view_2d.show_background_markers = parse_bool(value, settings.view_2d.show_background_markers);
        } else if (key == "show_adhesion_markers" || key == "show_adhesions" || key == "show_adhesion_points") {
            view_2d_keys_seen.insert("show_adhesion_markers");
            settings.view_2d.show_adhesion_markers = parse_bool(value, settings.view_2d.show_adhesion_markers);
        } else if (key == "show_cab_illuminance_markers" || key == "show_cabilluminance_markers" || key == "show_cab_illuminance_points") {
            view_2d_keys_seen.insert("show_cab_illuminance_markers");
            settings.view_2d.show_cab_illuminance_markers = parse_bool(value, settings.view_2d.show_cab_illuminance_markers);
        } else if (key == "show_fog_markers" || key == "show_fogs" || key == "show_fog_points") {
            view_2d_keys_seen.insert("show_fog_markers");
            settings.view_2d.show_fog_markers = parse_bool(value, settings.view_2d.show_fog_markers);
        } else if (key == "show_profile_graph" || key == "show_gradient_graph") {
            view_2d_keys_seen.insert("show_profile_graph");
            settings.view_2d.show_profile_graph = parse_bool(value, settings.view_2d.show_profile_graph);
        } else if (key == "show_radius_graph" || key == "show_curve_graph") {
            view_2d_keys_seen.insert("show_radius_graph");
            settings.view_2d.show_radius_graph = parse_bool(value, settings.view_2d.show_radius_graph);
        } else if (key == "show_background_image" || key == "show_bgimage" || key == "bg_show") {
            view_2d_keys_seen.insert("show_background_image");
            settings.view_2d.show_background_image = parse_bool(value, settings.view_2d.show_background_image);
        } else if (key == "mode" || key == "view_2d_mode") {
            view_2d_keys_seen.insert("mode");
            settings.view_2d.mode = view_2d_mode_from_string(value, settings.view_2d.mode);
        } else if (key == "grid_mode" || key == "view_2d_grid_mode") {
            view_2d_keys_seen.insert("grid_mode");
            settings.view_2d.grid_mode = grid_mode_from_string(value, settings.view_2d.grid_mode);
        }
    }
    settings.font_size = clamp_font_size(settings.font_size);
    settings.ui_component_size = clamp_ui_component_size(settings.ui_component_size);
    settings.station_marker_size = clamp_station_marker_size(settings.station_marker_size);
    settings.theme_color = clamp_theme_color(settings.theme_color);
    settings.view_2d.mode = normalize_view_2d_mode(settings.view_2d.mode);
    settings.view_2d.grid_mode = normalize_grid_mode(settings.view_2d.grid_mode);
    if (view_2d_keys_seen.size() < 18) save_user_settings(settings);
    return settings;
}

bool load_imgui_layout(const std::filesystem::path& path) {
    std::error_code ec;
    if (!std::filesystem::exists(path, ec) || ec) return false;

    std::ifstream in(path, std::ios::binary);
    if (!in) return false;

    std::ostringstream buffer;
    buffer << in.rdbuf();
    std::string data = buffer.str();
    if (data.empty()) return false;

    ImGui::LoadIniSettingsFromMemory(data.data(), data.size());
    return true;
}

bool save_imgui_layout(const std::filesystem::path& path) {
    size_t size = 0;
    const char* data = ImGui::SaveIniSettingsToMemory(&size);
    if (!data || size == 0) return false;

    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out.write(data, static_cast<std::streamsize>(size));
    return static_cast<bool>(out);
}

void save_imgui_layout_if_requested(const std::filesystem::path& path) {
    ImGuiIO& io = ImGui::GetIO();
    if (!io.WantSaveIniSettings) return;
    save_imgui_layout(path);
    io.WantSaveIniSettings = false;
}

bool imgui_layout_save_pending() {
    ImGuiContext* context = ImGui::GetCurrentContext();
    return context && context->SettingsDirtyTimer > 0.0f;
}

bool ensure_history_file(const std::filesystem::path& path) {
    std::error_code ec;
    if (std::filesystem::exists(path, ec) && !ec) return true;
    std::ofstream out(path, std::ios::binary);
    return static_cast<bool>(out);
}

double parse_history_double(const std::string& value, double fallback) {
    try {
        size_t used = 0;
        double parsed = std::stod(value, &used);
        return used == 0 || !std::isfinite(parsed) ? fallback : parsed;
    } catch (...) {
        return fallback;
    }
}

std::string history_number(double value) {
    std::ostringstream out;
    out << std::setprecision(17) << value;
    return out.str();
}

std::optional<int> parse_history_section_index(std::string section) {
    section = ascii_lower(trim_ascii(section));
    size_t pos = std::string::npos;
    if (section.rfind("map", 0) == 0) {
        pos = 3;
    } else if (section.rfind("recent_map", 0) == 0) {
        pos = 10;
    } else if (section.rfind("recent", 0) == 0) {
        pos = 6;
    }
    if (pos == std::string::npos) return std::nullopt;
    while (pos < section.size() && !std::isdigit(static_cast<unsigned char>(section[pos]))) ++pos;
    if (pos >= section.size()) return std::nullopt;
    try {
        return std::stoi(section.substr(pos));
    } catch (...) {
        return std::nullopt;
    }
}

std::vector<RecentMapEntry> load_history_entries(const std::filesystem::path& path) {
    ensure_history_file(path);
    std::ifstream in(path, std::ios::binary);
    if (!in) return {};

    std::map<int, RecentMapEntry> parsed;
    int current_index = -1;
    std::string line;
    while (std::getline(in, line)) {
        std::string trimmed_line = trim_ascii(line);
        if (trimmed_line.empty() || trimmed_line.front() == ';' || trimmed_line.front() == '#') continue;
        if (trimmed_line.front() == '[' && trimmed_line.back() == ']') {
            std::string section = trimmed_line.substr(1, trimmed_line.size() - 2);
            auto index = parse_history_section_index(section);
            current_index = index ? *index : -1;
            if (current_index >= 0 && parsed.find(current_index) == parsed.end()) parsed[current_index] = {};
            continue;
        }
        if (current_index < 0) continue;
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = ascii_lower(trim_ascii(line.substr(0, eq)));
        std::string value = trim_ascii(line.substr(eq + 1));
        RecentMapEntry& entry = parsed[current_index];
        if (key == "path" || key == "map_path" || key == "map") {
            entry.path = normalized_storage_path(value);
        } else if (key == "bg_path" || key == "background_path" || key == "image_path") {
            entry.background.has_image = !value.empty();
            entry.background.image_path = normalized_storage_path(value);
        } else if (key == "bg_x" || key == "background_x") {
            entry.background.x = parse_history_double(value, entry.background.x);
        } else if (key == "bg_y" || key == "background_y") {
            entry.background.y = parse_history_double(value, entry.background.y);
        } else if (key == "bg_width" || key == "background_width") {
            entry.background.width = parse_history_double(value, entry.background.width);
        } else if (key == "bg_height" || key == "background_height") {
            entry.background.height = parse_history_double(value, entry.background.height);
        } else if (key == "bg_rotation" || key == "bg_rotation_deg" || key == "background_rotation") {
            entry.background.rotation_deg = parse_history_double(value, entry.background.rotation_deg);
        } else if (key == "bg_brightness" || key == "background_brightness") {
            entry.background.brightness = parse_history_double(value, entry.background.brightness);
        }
    }

    std::vector<RecentMapEntry> entries;
    std::set<std::string> seen;
    for (auto& kv : parsed) {
        RecentMapEntry entry = std::move(kv.second);
        if (entry.path.empty()) continue;
        std::string key = normalized_path_key(entry.path);
        if (!seen.insert(key).second) continue;
        entries.push_back(std::move(entry));
        if (entries.size() >= kMaxRecentMaps) break;
    }
    return entries;
}

bool save_history_entries(const std::filesystem::path& path, const std::vector<RecentMapEntry>& entries) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    if (entries.empty()) return true;
    size_t count = std::min(entries.size(), kMaxRecentMaps);
    out << "[Recent]\n";
    out << "count=" << count << "\n\n";
    for (size_t i = 0; i < count; ++i) {
        const RecentMapEntry& entry = entries[i];
        out << "[Map" << i << "]\n";
        out << "path=" << normalized_storage_path(entry.path) << "\n";
        if (entry.background.has_image && !entry.background.image_path.empty()) {
            const BackgroundHistory& bg = entry.background;
            out << "bg_path=" << normalized_storage_path(bg.image_path) << "\n";
            out << "bg_x=" << history_number(bg.x) << "\n";
            out << "bg_y=" << history_number(bg.y) << "\n";
            out << "bg_width=" << history_number(bg.width) << "\n";
            out << "bg_height=" << history_number(bg.height) << "\n";
            out << "bg_rotation=" << history_number(bg.rotation_deg) << "\n";
            out << "bg_brightness=" << history_number(bg.brightness) << "\n";
        }
        out << "\n";
    }
    return true;
}

void apply_ui_font_size(float font_size) {
    ImGui::GetStyle().FontScaleMain = clamp_font_size(font_size) / kDefaultFontSize;
}

void apply_ui_theme_color(ImVec4 theme_color) {
    ImGuiStyle& style = ImGui::GetStyle();
    ImVec4 accent = clamp_theme_color(theme_color);
    ImVec4 dark_bg(0.06f, 0.07f, 0.08f, 1.0f);
    ImVec4 panel_bg(0.12f, 0.13f, 0.15f, 1.0f);

    style.Colors[ImGuiCol_FrameBg] = with_alpha(mix_color(panel_bg, accent, 0.16f), 0.88f);
    style.Colors[ImGuiCol_Button] = with_alpha(accent, 0.62f);
    style.Colors[ImGuiCol_ButtonHovered] = with_alpha(mix_color(accent, ImVec4(1.0f, 1.0f, 1.0f, 1.0f), 0.16f), 0.86f);
    style.Colors[ImGuiCol_ButtonActive] = with_alpha(mix_color(accent, dark_bg, 0.18f), 1.0f);
    style.Colors[ImGuiCol_Header] = with_alpha(accent, 0.34f);
    style.Colors[ImGuiCol_HeaderHovered] = with_alpha(accent, 0.72f);
    style.Colors[ImGuiCol_HeaderActive] = with_alpha(accent, 0.92f);
    style.Colors[ImGuiCol_CheckMark] = with_alpha(accent, 1.0f);
    style.Colors[ImGuiCol_CheckboxSelectedBg] = with_alpha(accent, 0.58f);
    style.Colors[ImGuiCol_SliderGrab] = with_alpha(accent, 0.86f);
    style.Colors[ImGuiCol_SliderGrabActive] = with_alpha(mix_color(accent, ImVec4(1.0f, 1.0f, 1.0f, 1.0f), 0.18f), 1.0f);
    style.Colors[ImGuiCol_FrameBgHovered] = with_alpha(accent, 0.24f);
    style.Colors[ImGuiCol_FrameBgActive] = with_alpha(accent, 0.42f);
    style.Colors[ImGuiCol_SeparatorHovered] = with_alpha(accent, 0.78f);
    style.Colors[ImGuiCol_SeparatorActive] = with_alpha(accent, 1.0f);
    style.Colors[ImGuiCol_ResizeGrip] = with_alpha(accent, 0.30f);
    style.Colors[ImGuiCol_ResizeGripHovered] = with_alpha(accent, 0.70f);
    style.Colors[ImGuiCol_ResizeGripActive] = with_alpha(accent, 0.95f);
    style.Colors[ImGuiCol_Tab] = with_alpha(mix_color(panel_bg, accent, 0.28f), 0.86f);
    style.Colors[ImGuiCol_TabHovered] = with_alpha(accent, 0.82f);
    style.Colors[ImGuiCol_TabSelected] = with_alpha(mix_color(panel_bg, accent, 0.58f), 1.0f);
    style.Colors[ImGuiCol_TabSelectedOverline] = with_alpha(accent, 1.0f);
    style.Colors[ImGuiCol_TabDimmedSelected] = with_alpha(mix_color(panel_bg, accent, 0.36f), 1.0f);
    style.Colors[ImGuiCol_TabDimmedSelectedOverline] = with_alpha(accent, 0.72f);
    style.Colors[ImGuiCol_TitleBgActive] = with_alpha(mix_color(dark_bg, accent, 0.35f), 1.0f);
    style.Colors[ImGuiCol_DockingPreview] = with_alpha(accent, 0.70f);
    style.Colors[ImGuiCol_TextLink] = with_alpha(mix_color(accent, ImVec4(1.0f, 1.0f, 1.0f, 1.0f), 0.12f), 1.0f);
    style.Colors[ImGuiCol_TextSelectedBg] = with_alpha(accent, 0.42f);
    style.Colors[ImGuiCol_NavCursor] = with_alpha(accent, 1.0f);

    ImPlotStyle& plot_style = ImPlot::GetStyle();
    ImVec4 canvas_bg(12.0f / 255.0f, 13.0f / 255.0f, 15.0f / 255.0f, 1.0f);
    plot_style.Colors[ImPlotCol_FrameBg] = canvas_bg;
    plot_style.Colors[ImPlotCol_PlotBg] = canvas_bg;
    plot_style.Colors[ImPlotCol_PlotBorder] = ImVec4(0.43f, 0.43f, 0.50f, 0.50f);
    plot_style.Colors[ImPlotCol_LegendBg] = ImVec4(0.08f, 0.08f, 0.08f, 0.94f);
    plot_style.Colors[ImPlotCol_LegendBorder] = ImVec4(0.43f, 0.43f, 0.50f, 0.50f);
    plot_style.Colors[ImPlotCol_AxisText] = with_alpha(mix_color(ImVec4(1.0f, 1.0f, 1.0f, 1.0f), accent, 0.20f), 1.0f);
    plot_style.Colors[ImPlotCol_AxisGrid] = ImVec4(48.0f / 255.0f, 52.0f / 255.0f, 58.0f / 255.0f, 1.0f);
    plot_style.Colors[ImPlotCol_AxisTick] = with_alpha(accent, 0.55f);
    plot_style.Colors[ImPlotCol_AxisBg] = with_alpha(accent, 0.08f);
    plot_style.Colors[ImPlotCol_AxisBgHovered] = with_alpha(accent, 0.22f);
    plot_style.Colors[ImPlotCol_AxisBgActive] = with_alpha(accent, 0.34f);
    plot_style.Colors[ImPlotCol_Selection] = ImVec4(1.00f, 0.60f, 0.00f, 1.00f);
    plot_style.Colors[ImPlotCol_Crosshairs] = ImVec4(1.00f, 1.00f, 1.00f, 0.50f);
}

void apply_ui_component_size(float component_size, float dpi_scale, bool viewports_enabled) {
    ImGuiStyle& style = ImGui::GetStyle();
    float font_size_base = style.FontSizeBase;
    float font_scale_main = style.FontScaleMain;
    style = ImGuiStyle();
    style.FontSizeBase = font_size_base;
    style.FontScaleMain = font_scale_main;
    ImGui::StyleColorsDark(&style);
    style.ScaleAllSizes(dpi_scale * clamp_ui_component_size(component_size) / 100.0f);
    style.FontScaleDpi = dpi_scale;
    if (viewports_enabled) {
        style.WindowRounding = 0.0f;
        style.Colors[ImGuiCol_WindowBg].w = 1.0f;
    }
}

void apply_ui_settings(float font_size, float component_size, ImVec4 theme_color, float dpi_scale, bool viewports_enabled) {
    apply_ui_component_size(component_size, dpi_scale, viewports_enabled);
    apply_ui_theme_color(theme_color);
    apply_ui_font_size(font_size);
}


App::App(ID3D11Device* device, UserSettings settings, float dpi_scale, bool viewports_enabled, bool has_saved_layout)
    : device_(device), settings_(std::move(settings)), dpi_scale_(dpi_scale), viewports_enabled_(viewports_enabled),
      has_saved_layout_(has_saved_layout) {
    g_app = this;
    kv_set_log_callback(&App::log_callback);
    model_preview_canvas_ = std::make_unique<Canvas3D>(device_);
    model_preview_canvas_->set_background_color(model_preview_bg_color_);
    lang_ = settings_.language;
    font_size_ = clamp_font_size(settings_.font_size);
    ui_component_size_ = clamp_ui_component_size(settings_.ui_component_size);
    station_marker_size_ = clamp_station_marker_size(settings_.station_marker_size);
    theme_color_ = clamp_theme_color(settings_.theme_color);
    settings_.language = lang_;
    settings_.font_size = font_size_;
    settings_.ui_component_size = ui_component_size_;
    settings_.station_marker_size = station_marker_size_;
    settings_.theme_color = theme_color_;
    pending_font_size_ = font_size_;
    font_size_before_dialog_ = font_size_;
    pending_ui_component_size_ = ui_component_size_;
    ui_component_size_before_dialog_ = ui_component_size_;
    pending_station_marker_size_ = station_marker_size_;
    station_marker_size_before_dialog_ = station_marker_size_;
    pending_theme_color_ = theme_color_;
    theme_color_before_dialog_ = theme_color_;
    apply_window_visibility_settings(settings_.window_visibility);
    last_saved_window_visibility_ = current_window_visibility();
    settings_.window_visibility = last_saved_window_visibility_;
    apply_view_2d_settings(settings_.view_2d);
    last_saved_view_2d_settings_ = current_view_2d_settings();
    settings_.view_2d = last_saved_view_2d_settings_;
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
    int sev = 0;
    if (text.find("[ERROR]") != std::string::npos || text.find("Error") != std::string::npos) sev = 2;
    else if (text.find("[WARN]") != std::string::npos || text.find("Warning") != std::string::npos) sev = 1;
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
    if (loader_.joinable()) loader_.join();
}

void App::poll_loader() {
    std::optional<LoadResult> result;
    {
        std::lock_guard<std::mutex> lock(result_mutex_);
        if (pending_result_) {
            result = std::move(pending_result_);
            pending_result_.reset();
        }
    }
    if (result) apply_load_result(std::move(*result));
}

void App::begin_load(std::string path, bool preserve_settings, bool record_history,
                     std::optional<BackgroundHistory> background_to_restore) {
    if (path.empty() || loading_) return;
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
    loading_ = true;
    pending_load_started_at_.reset();
    plan_canvas_rendered_this_frame_ = false;
    add_log(std::string("Start loading file: ") + path);

    bool has_cp = preserve_settings && has_model_ && model_.has_cp_arb;
    double cp0 = has_cp ? model_.cp_arb[0] : 0.0;
    double cp1 = has_cp ? model_.cp_arb[1] : 0.0;
    double cp2 = has_cp ? model_.cp_arb[2] : 25.0;

    loader_ = std::thread([this, path, has_cp, cp0, cp1, cp2, old_other, preserve_settings,
                           record_history, background_to_restore, load_started_at]() mutable {
        LoadResult result = load_map_worker(path, unit_distance_, has_cp, cp0, cp1, cp2);
        result.started_at = load_started_at;
        result.preserve_settings = preserve_settings;
        result.record_history = record_history;
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
            std::lock_guard<std::mutex> lock(result_mutex_);
            pending_result_ = std::move(result);
        }
        loading_ = false;
        wake_main_window();
    });
}

void App::apply_load_result(LoadResult result) {
    if (!result.ok) {
        pending_load_started_at_.reset();
        plan_canvas_rendered_this_frame_ = false;
        add_log("Error during loading: " + result.error);
        if (result.handle) kv_free(result.handle);
        return;
    }
    if (handle_) kv_free(handle_);
    handle_ = result.handle;
    model_ = std::move(result.model);
    invalidate_table_cache();
    has_model_ = true;
    rebuild_marker_overlay_cache();
    reset_marker_visibility();
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
    std::ostringstream buffer_copy_elapsed;
    buffer_copy_elapsed << std::fixed << std::setprecision(3) << model_.buffer_copy_seconds;
    add_log("Load timing: buffer copy=" + buffer_copy_elapsed.str() + "s");
    add_log("Map loaded: " + result.path);
    if (result.background_to_restore) {
        apply_background_history(*result.background_to_restore);
    } else if (!result.preserve_settings) {
        clear_background_image();
    }
    if (result.record_history) touch_recent_map(result.path);
    pending_load_started_at_ = result.started_at;
    plan_canvas_rendered_this_frame_ = false;
}

void App::after_frame_presented() {
    if (!pending_load_started_at_ || !plan_canvas_rendered_this_frame_) return;

    double elapsed_seconds = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - *pending_load_started_at_).count();
    pending_load_started_at_.reset();
    plan_canvas_rendered_this_frame_ = false;

    std::ostringstream elapsed;
    elapsed << std::fixed << std::setprecision(2) << elapsed_seconds;
    add_log("Map loaded in " + elapsed.str() + "s");
}

void App::regenerate_geometry() {
    if (!handle_ || loading_) return;
    if (!kv_generate_geometry(handle_, unit_distance_, 1, cp_start_, cp_end_, cp_interval_)) {
        const char* err = kv_get_last_error();
        add_log(std::string("[ERROR]") + (err ? err : "geometry failed"));
        return;
    }
    std::map<std::string, OtherTrack> old_other;
    for (const auto& t : model_.other_tracks) old_other[t.key] = t;
    try {
        MapModel updated = build_model_from_handle(handle_, file_path_);
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
        invalidate_table_cache();
        rebuild_marker_overlay_cache();
        sync_marker_visibility_sizes();
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
        add_log("Geometry regenerated by maploader");
    } catch (const std::exception& e) {
        add_log(std::string("[ERROR]") + e.what());
    }
}

App::LoadResult App::load_map_worker(std::string path, double unit_distance, bool has_cp, double cp_start, double cp_end, double cp_step) {
    auto started_at = std::chrono::steady_clock::now();
    auto elapsed_seconds = [&]() {
        return std::chrono::duration<double>(std::chrono::steady_clock::now() - started_at).count();
    };
    LoadResult out;
    out.path = path;
    void* handle = kv_load_map(path.c_str(), unit_distance);
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
        out.model = build_model_from_handle(handle, path);
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
    const char* raw = kv_get_ir_json(handle);
    if (!raw) throw std::runtime_error("kv_get_ir_json failed");
    std::string json(raw);
    kv_free_string(raw);
    auto root = mini_json::Parser(json).parse();

    MapModel model;
    model.path = path;
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
            }
            rows.push_back(std::move(r));
        }
        return rows;
    };
    const auto& structure = root.at("structure");
    model.structures = make_table_rows(structure.at("data"));
    model.structure_models = make_table_rows(structure.at("models"));
    model.sound_list = make_table_rows(root.at("soundList"));
    model.structures_between = make_table_rows(structure.at("between_data"));
    model.repeaters = make_table_rows(root.at("repeater"));
    model.irregularities = make_table_rows(root.at("irregularity"));
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
    return model;
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
    ImGui::DockBuilderDockWindow("StructureModels", dock_right);
    ImGui::DockBuilderDockWindow("SoundList", dock_right);
    ImGui::DockBuilderDockWindow("Repeaters", dock_right);
    ImGui::DockBuilderDockWindow("Irregularities", dock_right);
    ImGui::DockBuilderDockWindow("Backgrounds", dock_right);
    ImGui::DockBuilderDockWindow("Adhesions", dock_right);
    ImGui::DockBuilderDockWindow("CabIlluminance", dock_right);
    ImGui::DockBuilderDockWindow("Fogs", dock_right);
    ImGui::DockBuilderDockWindow("Console", dock_console);
    ImGui::DockBuilderDockWindow("ModelPreview3D", dock_main);
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
    visibility.show_structure_models_window = show_structure_models_window_;
    visibility.show_sound_list_window = show_sound_list_window_;
    visibility.show_repeaters_window = show_repeaters_window_;
    visibility.show_irregularities_window = show_irregularities_window_;
    visibility.show_backgrounds_window = show_backgrounds_window_;
    visibility.show_adhesions_window = show_adhesions_window_;
    visibility.show_cab_illuminance_window = show_cab_illuminance_window_;
    visibility.show_fogs_window = show_fogs_window_;
    visibility.show_plots_window = show_plots_window_;
    visibility.show_model_preview_window = show_model_preview_window_;
    return visibility;
}

void App::apply_window_visibility_settings(const WindowVisibilitySettings& visibility) {
    show_othertracks_window_ = visibility.show_othertracks_window;
    show_station_list_window_ = visibility.show_station_list_window;
    show_structures_window_ = visibility.show_structures_window;
    show_structure_models_window_ = visibility.show_structure_models_window;
    show_sound_list_window_ = visibility.show_sound_list_window;
    show_repeaters_window_ = visibility.show_repeaters_window;
    show_irregularities_window_ = visibility.show_irregularities_window;
    show_backgrounds_window_ = visibility.show_backgrounds_window;
    show_adhesions_window_ = visibility.show_adhesions_window;
    show_cab_illuminance_window_ = visibility.show_cab_illuminance_window;
    show_fogs_window_ = visibility.show_fogs_window;
    show_plots_window_ = visibility.show_plots_window;
    show_model_preview_window_ = visibility.show_model_preview_window;
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
                    if (ImGui::MenuItem(label.c_str(), nullptr, false, !loading_)) {
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
                            !loading_ && ((has_model_ && !file_path_.empty()) ||
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
            pending_station_marker_size_ = station_marker_size_;
            station_marker_size_before_dialog_ = station_marker_size_;
            pending_theme_color_ = theme_color_;
            theme_color_before_dialog_ = theme_color_;
            show_font_size_popup_ = true;
        }
        if (ImGui::MenuItem(tr("menu.plotlimit").c_str(), nullptr, false, has_model_)) {
            plot_min_ = dmin_;
            plot_max_ = dmax_;
            show_range_popup_ = true;
        }
        if (ImGui::MenuItem(tr("menu.controlpoints").c_str(), nullptr, false, has_model_)) {
            cp_start_ = model_.cp_arb[0];
            cp_end_ = model_.cp_arb[1];
            cp_interval_ = model_.cp_arb[2];
            show_cp_popup_ = true;
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu(tr("menu.map_info").c_str())) {
        if (ImGui::MenuItem(tr("frame.othertracks").c_str(), nullptr, false, !show_othertracks_window_)) {
            show_othertracks_window_ = true;
        }
        if (ImGui::MenuItem(tr("frame.station_list").c_str(), nullptr, false, !show_station_list_window_)) {
            show_station_list_window_ = true;
        }
        if (ImGui::MenuItem(tr("button.structure_list").c_str(), nullptr, false, !show_structures_window_)) {
            show_structures_window_ = true;
        }
        if (ImGui::MenuItem(tr("frame.structure_models").c_str(), nullptr, false, !show_structure_models_window_)) {
            show_structure_models_window_ = true;
        }
        if (ImGui::MenuItem(tr("frame.sound_list").c_str(), nullptr, false, !show_sound_list_window_)) {
            show_sound_list_window_ = true;
        }
        if (ImGui::MenuItem(tr("button.repeater_list").c_str(), nullptr, false, !show_repeaters_window_)) {
            show_repeaters_window_ = true;
        }
        if (ImGui::MenuItem(tr("frame.irregularities").c_str(), nullptr, false, !show_irregularities_window_)) {
            show_irregularities_window_ = true;
        }
        if (ImGui::MenuItem(tr("frame.backgrounds").c_str(), nullptr, false, !show_backgrounds_window_)) {
            show_backgrounds_window_ = true;
        }
        if (ImGui::MenuItem(tr("frame.adhesions").c_str(), nullptr, false, !show_adhesions_window_)) {
            show_adhesions_window_ = true;
        }
        if (ImGui::MenuItem(tr("frame.cab_illuminance").c_str(), nullptr, false, !show_cab_illuminance_window_)) {
            show_cab_illuminance_window_ = true;
        }
        if (ImGui::MenuItem(tr("frame.fogs").c_str(), nullptr, false, !show_fogs_window_)) {
            show_fogs_window_ = true;
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu(tr("menu.view_2d").c_str())) {
        if (ImGui::MenuItem(tr("chk.view_2d_window").c_str(), nullptr, &show_plots_window_) && show_plots_window_) {
            focus_plots_next_ = true;
        }
        ImGui::Separator();
        ImGui::MenuItem(tr("frame.aux_info").c_str(), nullptr, false, false);
        ImGui::MenuItem(tr("chk.station_pos").c_str(), nullptr, &show_stations_);
        ImGui::MenuItem(tr("chk.station_name").c_str(), nullptr, &show_station_names_);
        ImGui::MenuItem(tr("chk.station_mileage").c_str(), nullptr, &show_station_mileage_);
        ImGui::MenuItem(tr("chk.curve_val").c_str(), nullptr, &show_curve_values_);
        ImGui::MenuItem(tr("chk.speedlimit").c_str(), nullptr, &show_speedlimits_);
        ImGui::Separator();
        ImGui::MenuItem(tr("frame.chart_visibility").c_str(), nullptr, false, false);
        if (ImGui::MenuItem(tr("chk.gradient_graph").c_str(), nullptr, &show_profile_graph_) && show_profile_graph_) reset_profile_axes_next_ = true;
        if (ImGui::MenuItem(tr("chk.curve_graph").c_str(), nullptr, &show_radius_graph_) && show_radius_graph_) reset_radius_axes_next_ = true;
        ImGui::MenuItem(tr("chk.gradient_pos").c_str(), nullptr, &show_gradient_pos_);
        ImGui::MenuItem(tr("chk.gradient_val").c_str(), nullptr, &show_gradient_values_);
        ImGui::MenuItem(tr("chk.prof_othert").c_str(), nullptr, &show_profile_other_);
        ImGui::MenuItem(tr("chk.irregularity_markers").c_str(), nullptr, &show_irregularity_markers_);
        ImGui::MenuItem(tr("chk.background_markers").c_str(), nullptr, &show_background_markers_);
        ImGui::MenuItem(tr("chk.adhesion_markers").c_str(), nullptr, &show_adhesion_markers_);
        ImGui::MenuItem(tr("chk.cab_illuminance_markers").c_str(), nullptr, &show_cab_illuminance_markers_);
        ImGui::MenuItem(tr("chk.fog_markers").c_str(), nullptr, &show_fog_markers_);
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
            show_bg_adjust_popup_ = true;
        }
        if (ImGui::MenuItem(tr("button.align_to_station").c_str(), nullptr, false,
                            has_model_ && model_.stations.size() >= 2 && !bg_image_.path.empty())) {
            align_pick1_.reset();
            align_pick2_.reset();
            pick_slot_ = 0;
            show_align_popup_ = true;
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu(tr("menu.view_3d").c_str())) {
        ImGui::MenuItem(tr("menu.structure_model_preview").c_str(), nullptr, &show_model_preview_window_);
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
        if (ImGui::MenuItem(tr("menu.about").c_str())) show_about_popup_ = true;
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

        const bool can_reload = !loading_ && ((has_model_ && !file_path_.empty()) ||
                                             (model_preview_canvas_ && model_preview_canvas_->has_model()));
        ImGui::BeginDisabled(!can_reload);
        if (ImGui::Button(tr("button.reload").c_str())) reload_current_map_and_model_preview();
        ImGui::EndDisabled();

        ImGui::SameLine(0.0f, style.ItemSpacing.x);
        ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
        ImGui::SameLine(0.0f, style.ItemSpacing.x);
        render_station_jump_combo();
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
    ImGui::SetNextItemWidth(std::max(1.0f, std::min(360.0f, ImGui::GetContentRegionAvail().x)));
    if (ImGui::BeginCombo("##toolbar_station", preview_text)) {
        for (int i = 0; i < static_cast<int>(model_.stations.size()); ++i) {
            std::string label = model_.stations[i].key + ", " + model_.stations[i].name;
            const bool selected = i == station_jump_index_;
            if (ImGui::Selectable(label.c_str(), selected)) {
                station_jump_index_ = i;
                focus_station(model_.stations[i].distance);
            }
            if (selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    ImGui::EndDisabled();
}

void App::render_console() {
    std::string title = tr("frame.console") + "###Console";
    ImGui::Begin(title.c_str());
    if (ImGui::Button(tr("button.clear").c_str())) {
        std::lock_guard<std::mutex> lock(log_mutex_);
        logs_.clear();
        last_log_.clear();
        error_count_ = warn_count_ = 0;
    }
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "E %d", error_count_);
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(1.0f, 0.78f, 0.25f, 1.0f), "W %d", warn_count_);
    ImGui::Separator();
    ImGui::BeginChild("console_scroll", ImVec2(0, 0), true, ImGuiWindowFlags_HorizontalScrollbar);
    std::lock_guard<std::mutex> lock(log_mutex_);
    for (const auto& line : logs_) {
        ImVec4 color = line.severity == 2 ? ImVec4(1.0f, 0.35f, 0.35f, 1.0f)
                     : line.severity == 1 ? ImVec4(1.0f, 0.78f, 0.25f, 1.0f)
                                          : ImVec4(0.88f, 0.88f, 0.88f, 1.0f);
        ImGui::TextColored(color, "%s", line.text.c_str());
    }
    if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 4.0f) ImGui::SetScrollHereY(1.0f);
    ImGui::EndChild();
    ImGui::End();
}

void App::render_popups() {
    if (show_font_size_popup_) {
        ImGui::OpenPopup(tr("dialog.ui_settings").c_str());
        show_font_size_popup_ = false;
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
        ImGui::SetNextItemWidth(260.0f);
        if (ImGui::SliderFloat(tr("label.station_marker_size").c_str(), &pending_station_marker_size_, kMinStationMarkerSize, kMaxStationMarkerSize, "%.0f px", ImGuiSliderFlags_AlwaysClamp)) {
            pending_station_marker_size_ = clamp_station_marker_size(pending_station_marker_size_);
            station_marker_size_ = pending_station_marker_size_;
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
            station_marker_size_ = clamp_station_marker_size(pending_station_marker_size_);
            theme_color_ = clamp_theme_color(pending_theme_color_);
            station_marker_size_before_dialog_ = station_marker_size_;
            settings_.font_size = font_size_;
            settings_.ui_component_size = ui_component_size_;
            settings_.station_marker_size = station_marker_size_;
            settings_.theme_color = theme_color_;
            settings_.window_visibility = current_window_visibility();
            last_saved_window_visibility_ = settings_.window_visibility;
            settings_.view_2d = current_view_2d_settings();
            last_saved_view_2d_settings_ = settings_.view_2d;
            save_user_settings(settings_);
            apply_ui_settings(font_size_, ui_component_size_, theme_color_, dpi_scale_, viewports_enabled_);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button(tr("button.cancel").c_str())) {
            pending_font_size_ = font_size_before_dialog_;
            pending_ui_component_size_ = ui_component_size_before_dialog_;
            pending_station_marker_size_ = station_marker_size_before_dialog_;
            station_marker_size_ = station_marker_size_before_dialog_;
            pending_theme_color_ = theme_color_before_dialog_;
            apply_ui_settings(font_size_, ui_component_size_, theme_color_, dpi_scale_, viewports_enabled_);
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    if (!ui_settings_popup_open) {
        pending_font_size_ = font_size_;
        pending_ui_component_size_ = ui_component_size_;
        pending_station_marker_size_ = station_marker_size_before_dialog_;
        station_marker_size_ = station_marker_size_before_dialog_;
        pending_theme_color_ = theme_color_;
        apply_ui_settings(font_size_, ui_component_size_, theme_color_, dpi_scale_, viewports_enabled_);
    }

    if (show_range_popup_) {
        ImGui::OpenPopup(tr("menu.plotlimit").c_str());
        show_range_popup_ = false;
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
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button(tr("button.reset").c_str())) {
            plot_min_ = model_.default_min;
            plot_max_ = model_.default_max;
        }
        ImGui::EndPopup();
    }

    if (show_cp_popup_) {
        ImGui::OpenPopup(tr("menu.controlpoints").c_str());
        show_cp_popup_ = false;
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

    if (show_bg_adjust_popup_) {
        ImGui::OpenPopup(tr("dialog.bgimage_adjust").c_str());
        show_bg_adjust_popup_ = false;
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

    if (show_align_popup_) {
        ImGui::OpenPopup(tr("dialog.align_to_station").c_str());
        show_align_popup_ = false;
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

    if (show_about_popup_) {
        ImGui::OpenPopup(tr("menu.about").c_str());
        show_about_popup_ = false;
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

void App::reload_current_map_and_model_preview() {
    if (loading_) return;
    if (has_model_ && !file_path_.empty()) begin_load(file_path_, true);
    reload_model_preview();
}

void App::handle_shortcuts() {
    if (ImGui::IsKeyPressed(ImGuiKey_F5, false)) {
        reload_current_map_and_model_preview();
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
    render_structures_window();
    render_structure_models_window();
    render_sound_list_window();
    render_repeaters_window();
    render_irregularities_window();
    render_backgrounds_window();
    render_adhesions_window();
    render_cab_illuminance_window();
    render_fogs_window();
    render_popups();
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

#ifndef NDEBUG
std::vector<std::string> command_line_args_utf8() {
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    std::vector<std::string> args;
    if (!argv) return args;
    args.reserve(static_cast<size_t>(argc));
    for (int i = 0; i < argc; ++i) {
        args.push_back(wide_to_utf8(argv[i]));
    }
    LocalFree(argv);
    return args;
}

struct HeadlessLoadOptions {
    bool requested = false;
    std::string path;
    std::string output_path;
    int repeat = 1;
    double unit_distance = 25.0;
    std::string error;
};

struct HeadlessPlanBenchmarkOptions {
    bool requested = false;
    std::string path;
    std::string output_path;
    int frames = 300;
    double unit_distance = 25.0;
    double pan_pixels = 8.0;
    double max_frame_ms = 16.667;
    bool profile_stages = false;
    std::string error;
};

HeadlessLoadOptions parse_headless_load_options(const std::vector<std::string>& args) {
    HeadlessLoadOptions options;
    for (size_t i = 1; i < args.size(); ++i) {
        const std::string& arg = args[i];
        if (arg == "--headless-load-map" || arg == "--headless-load") {
            options.requested = true;
            if (i + 1 >= args.size()) {
                options.error = arg + " requires a map path";
                return options;
            }
            options.path = args[++i];
        } else if (arg == "--repeat") {
            if (i + 1 >= args.size()) {
                options.error = "--repeat requires a number";
                return options;
            }
            char* end = nullptr;
            long parsed = std::strtol(args[++i].c_str(), &end, 10);
            if (!end || *end != '\0' || parsed <= 0 || parsed > 10000) {
                options.error = "--repeat must be between 1 and 10000";
                return options;
            }
            options.repeat = static_cast<int>(parsed);
        } else if (arg == "--unit-distance") {
            if (i + 1 >= args.size()) {
                options.error = "--unit-distance requires a number";
                return options;
            }
            char* end = nullptr;
            double parsed = std::strtod(args[++i].c_str(), &end);
            if (!end || *end != '\0' || parsed <= 0.0 || !std::isfinite(parsed)) {
                options.error = "--unit-distance must be a positive number";
                return options;
            }
            options.unit_distance = parsed;
        } else if (arg == "--headless-output") {
            if (i + 1 >= args.size()) {
                options.error = "--headless-output requires a path";
                return options;
            }
            options.output_path = args[++i];
        }
    }
    if (options.requested && options.path.empty() && options.error.empty()) {
        options.error = "--headless-load-map requires a map path";
    }
    return options;
}

HeadlessPlanBenchmarkOptions parse_headless_plan_benchmark_options(const std::vector<std::string>& args) {
    HeadlessPlanBenchmarkOptions options;
    for (size_t i = 1; i < args.size(); ++i) {
        const std::string& arg = args[i];
        if (arg == "--debug-headless-plan-bench") {
            options.requested = true;
            if (i + 1 >= args.size()) {
                options.error = arg + " requires a map path";
                return options;
            }
            options.path = args[++i];
        } else if (arg == "--frames") {
            if (i + 1 >= args.size()) {
                options.error = "--frames requires a number";
                return options;
            }
            char* end = nullptr;
            long parsed = std::strtol(args[++i].c_str(), &end, 10);
            if (!end || *end != '\0' || parsed <= 0 || parsed > 100000) {
                options.error = "--frames must be between 1 and 100000";
                return options;
            }
            options.frames = static_cast<int>(parsed);
        } else if (arg == "--unit-distance") {
            if (i + 1 >= args.size()) {
                options.error = "--unit-distance requires a number";
                return options;
            }
            char* end = nullptr;
            double parsed = std::strtod(args[++i].c_str(), &end);
            if (!end || *end != '\0' || parsed <= 0.0 || !std::isfinite(parsed)) {
                options.error = "--unit-distance must be a positive number";
                return options;
            }
            options.unit_distance = parsed;
        } else if (arg == "--pan-pixels") {
            if (i + 1 >= args.size()) {
                options.error = "--pan-pixels requires a number";
                return options;
            }
            char* end = nullptr;
            double parsed = std::strtod(args[++i].c_str(), &end);
            if (!end || *end != '\0' || !std::isfinite(parsed)) {
                options.error = "--pan-pixels must be a finite number";
                return options;
            }
            options.pan_pixels = parsed;
        } else if (arg == "--max-frame-ms") {
            if (i + 1 >= args.size()) {
                options.error = "--max-frame-ms requires a number";
                return options;
            }
            char* end = nullptr;
            double parsed = std::strtod(args[++i].c_str(), &end);
            if (!end || *end != '\0' || parsed <= 0.0 || !std::isfinite(parsed)) {
                options.error = "--max-frame-ms must be a positive number";
                return options;
            }
            options.max_frame_ms = parsed;
        } else if (arg == "--headless-output") {
            if (i + 1 >= args.size()) {
                options.error = "--headless-output requires a path";
                return options;
            }
            options.output_path = args[++i];
        } else if (arg == "--profile-stages") {
            options.profile_stages = true;
        }
    }
    if (options.requested && options.path.empty() && options.error.empty()) {
        options.error = "--debug-headless-plan-bench requires a map path";
    }
    return options;
}

std::uint64_t hash_double_bits(double value) {
    if (value == 0.0) value = 0.0;
    std::uint64_t bits = 0;
    static_assert(sizeof(bits) == sizeof(value), "unexpected double size");
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

struct HeadlessBufferSummary {
    size_t rows = 0;
    size_t cols = 0;
    bool finite = true;
    std::uint64_t hash = 1469598103934665603ULL;
};

HeadlessBufferSummary summarize_headless_buffer(KvDoubleBuffer buffer) {
    HeadlessBufferSummary summary;
    summary.rows = buffer.rows;
    summary.cols = buffer.cols;
    if (!buffer.data || buffer.rows == 0 || buffer.cols == 0) return summary;
    const size_t count = buffer.rows * buffer.cols;
    for (size_t i = 0; i < count; ++i) {
        const double value = buffer.data[i];
        summary.finite = summary.finite && std::isfinite(value);
        std::uint64_t bits = hash_double_bits(value);
        for (int byte = 0; byte < 8; ++byte) {
            summary.hash ^= static_cast<unsigned char>((bits >> (byte * 8)) & 0xff);
            summary.hash *= 1099511628211ULL;
        }
    }
    return summary;
}

std::string hex_u64(std::uint64_t value) {
    std::ostringstream out;
    out << std::hex << std::setw(16) << std::setfill('0') << value;
    return out.str();
}

void print_headless_buffer_summary(std::ostream& out, const char* label, const HeadlessBufferSummary& summary) {
    out << " " << label << "=" << summary.rows << "x" << summary.cols
        << ":" << hex_u64(summary.hash)
        << (summary.finite ? "" : ":nonfinite");
}

int run_headless_load_map(const HeadlessLoadOptions& options) {
    std::ofstream output_file;
    std::ostream* out = &std::cout;
    if (!options.output_path.empty()) {
        output_file.open(std::filesystem::path(utf8_to_wide(options.output_path)), std::ios::out | std::ios::trunc);
        if (!output_file) {
            std::cerr << "failed to open headless output: " << options.output_path << "\n";
            return 1;
        }
        output_file.write("\xEF\xBB\xBF", 3);
        out = &output_file;
    }

    *out << "komapedit headless-load-map path=\"" << options.path
         << "\" repeat=" << options.repeat
         << " unit_distance=" << format_double(options.unit_distance, 3) << "\n";

    for (int run = 1; run <= options.repeat; ++run) {
        auto started_at = std::chrono::steady_clock::now();
        void* handle = kv_load_map(options.path.c_str(), options.unit_distance);
        auto loaded_at = std::chrono::steady_clock::now();
        if (!handle) {
            const char* err = kv_get_last_error();
            std::cerr << "headless run " << run << " failed: "
                      << (err ? err : "maploader failed") << "\n";
            return 2;
        }

        const char* json = kv_get_ir_json(handle);
        const size_t json_bytes = json ? std::strlen(json) : 0;
        if (json) kv_free_string(json);
        auto json_at = std::chrono::steady_clock::now();

        HeadlessBufferSummary own = summarize_headless_buffer(kv_get_owntrack_buffer(handle));
        HeadlessBufferSummary curve = summarize_headless_buffer(kv_get_curveradius_buffer(handle));
        HeadlessBufferSummary structures = summarize_headless_buffer(kv_get_structure_puts(handle));
        const size_t other_count = kv_get_othertrack_count(handle);
        HeadlessBufferSummary other_total;
        other_total.rows = 0;
        other_total.cols = 8;
        for (size_t i = 0; i < other_count; ++i) {
            const char* key = kv_get_othertrack_key(handle, i);
            HeadlessBufferSummary item = summarize_headless_buffer(kv_get_othertrack_buffer(handle, key));
            other_total.rows += item.rows;
            other_total.finite = other_total.finite && item.finite;
            other_total.hash ^= item.hash + 0x9e3779b97f4a7c15ULL + (other_total.hash << 6) + (other_total.hash >> 2);
        }
        kv_free(handle);
        auto finished_at = std::chrono::steady_clock::now();

        const double load_seconds = std::chrono::duration<double>(loaded_at - started_at).count();
        const double json_seconds = std::chrono::duration<double>(json_at - loaded_at).count();
        const double total_seconds = std::chrono::duration<double>(finished_at - started_at).count();
        *out << "headless run " << run
             << " load=" << std::fixed << std::setprecision(3) << load_seconds << "s"
             << " json=" << json_seconds << "s"
             << " total=" << total_seconds << "s"
             << " json_bytes=" << json_bytes
             << " othertracks=" << other_count;
        print_headless_buffer_summary(*out, "own", own);
        print_headless_buffer_summary(*out, "curve", curve);
        print_headless_buffer_summary(*out, "structures", structures);
        print_headless_buffer_summary(*out, "other", other_total);
        *out << "\n";
    }
    return 0;
}

int App::run_debug_headless_plan_benchmark(const std::string& path, int frames,
                                           double unit_distance, double pan_pixels,
                                           double max_frame_ms, const std::string& output_path,
                                           bool profile_stages) {
    std::ofstream output_file;
    std::ostream* out = &std::cout;
    if (!output_path.empty()) {
        output_file.open(std::filesystem::path(utf8_to_wide(output_path)), std::ios::out | std::ios::trunc);
        if (!output_file) {
            std::cerr << "failed to open headless output: " << output_path << "\n";
            return 1;
        }
        output_file.write("\xEF\xBB\xBF", 3);
        out = &output_file;
    }

    *out << "komapedit debug-headless-plan-bench path=\"" << path
         << "\" frames=" << frames
         << " unit_distance=" << format_double(unit_distance, 3)
         << " pan_pixels=" << format_double(pan_pixels, 3)
         << " max_frame_ms=" << format_double(max_frame_ms, 3) << "\n";
    *out << "stage=load-start\n";
    out->flush();

    LoadResult result = load_map_worker(path, unit_distance, false, 0.0, 0.0, 25.0);
    if (!result.ok) {
        std::cerr << "debug headless plan benchmark load failed: " << result.error << "\n";
        return 2;
    }
    *out << "stage=load-complete\n";
    out->flush();

    ImGui::CreateContext();
    ImPlot::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(1280.0f, 720.0f);
    io.DeltaTime = 1.0f / 60.0f;
    io.IniFilename = nullptr;
    io.Fonts->AddFontDefault();
    io.Fonts->Build();
    *out << "stage=imgui-ready\n";
    out->flush();

    int exit_code = 0;
    try {
        g_debug_plan_benchmark_log = profile_stages ? out : nullptr;
        UserSettings settings;
        App app(nullptr, settings, 1.0f, false, false);
        app.handle_ = result.handle;
        result.handle = nullptr;
        app.model_ = std::move(result.model);
        app.file_path_ = path;
        app.has_model_ = true;
        app.dmin_ = app.model_.default_min;
        app.dmax_ = app.model_.default_max;
        app.plot_min_ = app.dmin_;
        app.plot_max_ = app.dmax_;
        app.rebuild_marker_overlay_cache();
        app.reset_marker_visibility();
        std::fill(app.repeater_row_visible_.begin(), app.repeater_row_visible_.end(), 1);
        *out << "stage=overlay-cache-ready\n";
        out->flush();

        size_t chunk_count = 0;
        size_t segment_point_count = 0;
        for (const RepeaterOverlayRow& row : app.repeater_marker_cache_) {
            for (const PlanRepeaterSegment::Chunk& chunk : row.segment.chunks) {
                ++chunk_count;
                segment_point_count += chunk.points.size();
            }
        }
        size_t visible_other_count = 0;
        size_t visible_other_rows = 0;
        for (const OtherTrack& track : app.model_.other_tracks) {
            if (!track.visible) continue;
            ++visible_other_count;
            visible_other_rows += track.points.rows;
        }
        *out << "loaded own_rows=" << app.model_.own.rows
             << " visible_othertracks=" << visible_other_count
             << " visible_other_rows=" << visible_other_rows
             << " backgrounds=" << app.model_.backgrounds.size()
             << " background_markers=" << app.background_marker_cache_.size()
             << " repeaters=" << app.repeater_marker_cache_.size()
             << " selected_repeaters=" << app.repeater_row_visible_.size()
             << " repeater_chunks=" << chunk_count
             << " repeater_chunk_points=" << segment_point_count << "\n";
        out->flush();

        auto render_frame = [&]() {
            io.DisplaySize = ImVec2(1280.0f, 720.0f);
            io.DeltaTime = 1.0f / 60.0f;
            ImGui::NewFrame();
            ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f), ImGuiCond_Always);
            ImGui::SetNextWindowSize(io.DisplaySize, ImGuiCond_Always);
            ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings |
                ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoBringToFrontOnFocus;
            ImGui::Begin("DebugHeadlessPlanBenchmark", nullptr, flags);
            app.render_plan_canvas(ImVec2(1260.0f, 680.0f));
            if (g_debug_plan_benchmark_log) {
                int total_vtx = 0;
                int total_idx = 0;
                ImGuiContext& context = *GImGui;
                for (ImGuiWindow* window : context.Windows) {
                    if (!window || !window->WasActive || !window->DrawList) continue;
                    total_vtx += window->DrawList->VtxBuffer.Size;
                    total_idx += window->DrawList->IdxBuffer.Size;
                }
                *g_debug_plan_benchmark_log << "render_stage=before_imgui_render"
                                            << " scale=" << std::fixed << std::setprecision(6) << app.plan_view_.scale
                                            << " vtx=" << total_vtx
                                            << " idx=" << total_idx << "\n";
                g_debug_plan_benchmark_log->flush();
            }
            ImGui::End();
            ImGui::EndFrame();
        };

        *out << "stage=warmup-start\n";
        out->flush();
        for (int frame = 0; frame < 1; ++frame) {
            render_frame();
        }
        *out << "stage=warmup-complete\n";
        out->flush();

        std::vector<double> frame_ms;
        frame_ms.reserve(static_cast<size_t>(frames));
        for (int frame = 0; frame < frames; ++frame) {
            double step = (frame % 120 < 60) ? pan_pixels : -pan_pixels;
            app.plan_view_.pan_by_screen_delta(ImVec2(static_cast<float>(step), 0.0f));
            auto started_at = std::chrono::steady_clock::now();
            render_frame();
            auto finished_at = std::chrono::steady_clock::now();
            frame_ms.push_back(std::chrono::duration<double, std::milli>(finished_at - started_at).count());
        }
        *out << "stage=frames-complete\n";
        out->flush();

        std::vector<double> sorted_ms = frame_ms;
        std::sort(sorted_ms.begin(), sorted_ms.end());
        double sum_ms = 0.0;
        for (double value : frame_ms) sum_ms += value;
        auto percentile = [&](double p) {
            if (sorted_ms.empty()) return 0.0;
            size_t index = static_cast<size_t>(std::ceil(p * static_cast<double>(sorted_ms.size()))) - 1;
            index = std::min(index, sorted_ms.size() - 1);
            return sorted_ms[index];
        };
        double avg_ms = frame_ms.empty() ? 0.0 : sum_ms / static_cast<double>(frame_ms.size());
        double min_ms = sorted_ms.empty() ? 0.0 : sorted_ms.front();
        double p95_ms = percentile(0.95);
        double max_ms = sorted_ms.empty() ? 0.0 : sorted_ms.back();
        double p95_fps = p95_ms > 0.0 ? 1000.0 / p95_ms : 0.0;
        bool pass = p95_ms <= max_frame_ms;

        *out << std::fixed << std::setprecision(3)
             << "plan_bench avg_ms=" << avg_ms
             << " min_ms=" << min_ms
             << " p95_ms=" << p95_ms
             << " max_ms=" << max_ms
             << " p95_fps=" << p95_fps
             << " result=" << (pass ? "PASS" : "FAIL") << "\n";
        if (!pass) exit_code = 3;
    } catch (const std::exception& e) {
        std::cerr << "debug headless plan benchmark failed: " << e.what() << "\n";
        exit_code = 4;
    }

    g_debug_plan_benchmark_log = nullptr;
    if (result.handle) kv_free(result.handle);
    ImPlot::DestroyContext();
    ImGui::DestroyContext();
    return exit_code;
}
#endif

int main(int, char**) {
#ifndef NDEBUG
    std::vector<std::string> args = command_line_args_utf8();
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
                      << "[--repeat N] [--unit-distance M] [--headless-output FILE]\n";
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
