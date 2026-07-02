/*
 * Copyright (c) 2026 Sapporo_ningyo
 *
 * Licensed under Apache License 2.0; see LICENSE and NOTICE.
 * The GUI uses Dear ImGui and ImPlot; see THIRD_PARTY_NOTICES.md.
 */

#pragma execution_character_set("utf-8")

#include "app_settings.h"

#include "imgui.h"
#include "imgui_internal.h"
#include "implot.h"

#include <windows.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <vector>
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

float clamp_marker_size_percent(float value) {
    if (!std::isfinite(value)) return kDefaultMarkerSizePercent;
    const int rounded = static_cast<int>(std::round(value / kMarkerSizePercentStep)) * kMarkerSizePercentStep;
    return std::clamp(static_cast<float>(rounded), kMinMarkerSizePercent, kMaxMarkerSizePercent);
}

float marker_size_scale_from_percent(float value) {
    return clamp_marker_size_percent(value) / 100.0f;
}

float clamp_canvas_line_width(float value, float fallback) {
    if (!std::isfinite(value)) return fallback;
    const float rounded = std::round(value / kCanvasLineWidthStepPx) * kCanvasLineWidthStepPx;
    return std::clamp(rounded, kMinCanvasLineWidthPx, kMaxCanvasLineWidthPx);
}

CanvasLineWidthSettings clamp_canvas_line_widths(CanvasLineWidthSettings value) {
    value.own_track_px = clamp_canvas_line_width(value.own_track_px, kDefaultOwnTrackLineWidthPx);
    value.other_track_px = clamp_canvas_line_width(value.other_track_px, kDefaultOtherTrackLineWidthPx);
    value.chart_marker_px = clamp_canvas_line_width(value.chart_marker_px, kDefaultChartMarkerLineWidthPx);
    value.background_grid_px = clamp_canvas_line_width(value.background_grid_px, kDefaultBackgroundGridLineWidthPx);
    return value;
}

int clamp_scene_draw_distance(double value) {
    if (!std::isfinite(value)) return kDefaultSceneDrawDistanceM;
    const int rounded = static_cast<int>(std::round(value / kSceneDrawDistanceStepM)) * kSceneDrawDistanceStepM;
    return std::clamp(rounded, kMinSceneDrawDistanceM, kMaxSceneDrawDistanceM);
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
    out << "marker_size_percent=" << std::fixed << std::setprecision(1) << clamp_marker_size_percent(settings.marker_size_percent) << "\n";
    CanvasLineWidthSettings line_widths = clamp_canvas_line_widths(settings.canvas_line_widths);
    out << "own_track_line_width_px=" << std::fixed << std::setprecision(1) << line_widths.own_track_px << "\n";
    out << "other_track_line_width_px=" << std::fixed << std::setprecision(1) << line_widths.other_track_px << "\n";
    out << "chart_marker_line_width_px=" << std::fixed << std::setprecision(1) << line_widths.chart_marker_px << "\n";
    out << "background_grid_line_width_px=" << std::fixed << std::setprecision(1) << line_widths.background_grid_px << "\n";
    out << "theme_color=" << theme_color_to_string(settings.theme_color) << "\n";
    out << "\n[WindowVisibility]\n";
    out << "show_othertracks_window=" << bool_to_string(settings.window_visibility.show_othertracks_window) << "\n";
    out << "show_station_list_window=" << bool_to_string(settings.window_visibility.show_station_list_window) << "\n";
    out << "show_structures_window=" << bool_to_string(settings.window_visibility.show_structures_window) << "\n";
    out << "show_structure_models_window=" << bool_to_string(settings.window_visibility.show_structure_models_window) << "\n";
    out << "show_other_trains_window=" << bool_to_string(settings.window_visibility.show_other_trains_window) << "\n";
    out << "show_sound_list_window=" << bool_to_string(settings.window_visibility.show_sound_list_window) << "\n";
    out << "show_sound_3d_list_window=" << bool_to_string(settings.window_visibility.show_sound_3d_list_window) << "\n";
    out << "show_repeaters_window=" << bool_to_string(settings.window_visibility.show_repeaters_window) << "\n";
    out << "show_signal_aspects_window=" << bool_to_string(settings.window_visibility.show_signal_aspects_window) << "\n";
    out << "show_signals_window=" << bool_to_string(settings.window_visibility.show_signals_window) << "\n";
    out << "show_beacons_window=" << bool_to_string(settings.window_visibility.show_beacons_window) << "\n";
    out << "show_irregularities_window=" << bool_to_string(settings.window_visibility.show_irregularities_window) << "\n";
    out << "show_map_sounds_window=" << bool_to_string(settings.window_visibility.show_map_sounds_window) << "\n";
    out << "show_map_sound_3d_window=" << bool_to_string(settings.window_visibility.show_map_sound_3d_window) << "\n";
    out << "show_rolling_noises_window=" << bool_to_string(settings.window_visibility.show_rolling_noises_window) << "\n";
    out << "show_flange_noises_window=" << bool_to_string(settings.window_visibility.show_flange_noises_window) << "\n";
    out << "show_joint_noises_window=" << bool_to_string(settings.window_visibility.show_joint_noises_window) << "\n";
    out << "show_backgrounds_window=" << bool_to_string(settings.window_visibility.show_backgrounds_window) << "\n";
    out << "show_adhesions_window=" << bool_to_string(settings.window_visibility.show_adhesions_window) << "\n";
    out << "show_cab_illuminance_window=" << bool_to_string(settings.window_visibility.show_cab_illuminance_window) << "\n";
    out << "show_fogs_window=" << bool_to_string(settings.window_visibility.show_fogs_window) << "\n";
    out << "show_plots_window=" << bool_to_string(settings.window_visibility.show_plots_window) << "\n";
    out << "show_model_preview_window=" << bool_to_string(settings.window_visibility.show_model_preview_window) << "\n";
    out << "show_scene_preview_window=" << bool_to_string(settings.window_visibility.show_scene_preview_window) << "\n";
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
    out << "show_beacon_markers=" << bool_to_string(settings.view_2d.show_beacon_markers) << "\n";
    out << "show_pretrain_markers=" << bool_to_string(settings.view_2d.show_pretrain_markers) << "\n";
    out << "show_map_sound_markers=" << bool_to_string(settings.view_2d.show_map_sound_markers) << "\n";
    out << "show_map_sound_3d_markers=" << bool_to_string(settings.view_2d.show_map_sound_3d_markers) << "\n";
    out << "show_rolling_noise_markers=" << bool_to_string(settings.view_2d.show_rolling_noise_markers) << "\n";
    out << "show_flange_noise_markers=" << bool_to_string(settings.view_2d.show_flange_noise_markers) << "\n";
    out << "show_joint_noise_markers=" << bool_to_string(settings.view_2d.show_joint_noise_markers) << "\n";
    out << "show_background_markers=" << bool_to_string(settings.view_2d.show_background_markers) << "\n";
    out << "show_adhesion_markers=" << bool_to_string(settings.view_2d.show_adhesion_markers) << "\n";
    out << "show_cab_illuminance_markers=" << bool_to_string(settings.view_2d.show_cab_illuminance_markers) << "\n";
    out << "show_fog_markers=" << bool_to_string(settings.view_2d.show_fog_markers) << "\n";
    out << "show_profile_graph=" << bool_to_string(settings.view_2d.show_profile_graph) << "\n";
    out << "show_radius_graph=" << bool_to_string(settings.view_2d.show_radius_graph) << "\n";
    out << "show_background_image=" << bool_to_string(settings.view_2d.show_background_image) << "\n";
    out << "mode=" << view_2d_mode_to_string(settings.view_2d.mode) << "\n";
    out << "grid_mode=" << grid_mode_to_string(settings.view_2d.grid_mode) << "\n";
    out << "\n[View3D]\n";
    out << "show_scene_owntrack_markers=" << bool_to_string(settings.view_3d.show_scene_owntrack_markers) << "\n";
    out << "show_scene_current_position_on_plan=" << bool_to_string(settings.view_3d.show_scene_current_position_on_plan) << "\n";
    out << "scene_draw_distance_m=" << clamp_scene_draw_distance(settings.view_3d.scene_draw_distance_m) << "\n";
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
    std::set<std::string> view_3d_keys_seen;
    auto parse_line_width = [](const std::string& value, float fallback) {
        try {
            return clamp_canvas_line_width(std::stof(value), fallback);
        } catch (...) {
            return fallback;
        }
    };
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
        } else if (key == "marker_size_percent" || key == "marker_size_scale_percent" || key == "station_marker_size_percent") {
            try {
                settings.marker_size_percent = clamp_marker_size_percent(std::stof(value));
            } catch (...) {
                settings.marker_size_percent = kDefaultMarkerSizePercent;
            }
        } else if (key == "station_marker_size" || key == "station_marker_radius" || key == "station_size") {
            try {
                float raw = std::stof(value);
                settings.marker_size_percent = raw < kMinMarkerSizePercent
                    ? clamp_marker_size_percent(raw / kDefaultStationMarkerSize * 100.0f)
                    : clamp_marker_size_percent(raw);
            } catch (...) {
                settings.marker_size_percent = kDefaultMarkerSizePercent;
            }
        } else if (key == "own_track_line_width_px" ||
                   key == "own_track_line_width" ||
                   key == "canvas_own_track_line_width_px" ||
                   key == "canvas_own_track_line_width") {
            settings.canvas_line_widths.own_track_px =
                parse_line_width(value, kDefaultOwnTrackLineWidthPx);
        } else if (key == "other_track_line_width_px" ||
                   key == "other_track_line_width" ||
                   key == "canvas_other_track_line_width_px" ||
                   key == "canvas_other_track_line_width") {
            settings.canvas_line_widths.other_track_px =
                parse_line_width(value, kDefaultOtherTrackLineWidthPx);
        } else if (key == "chart_marker_line_width_px" ||
                   key == "chart_marker_line_width" ||
                   key == "canvas_chart_marker_line_width_px" ||
                   key == "canvas_chart_marker_line_width") {
            settings.canvas_line_widths.chart_marker_px =
                parse_line_width(value, kDefaultChartMarkerLineWidthPx);
        } else if (key == "background_grid_line_width_px" ||
                   key == "background_grid_line_width" ||
                   key == "canvas_background_grid_line_width_px" ||
                   key == "canvas_background_grid_line_width") {
            settings.canvas_line_widths.background_grid_px =
                parse_line_width(value, kDefaultBackgroundGridLineWidthPx);
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
        } else if (key == "show_other_trains_window" || key == "show_other_train_window") {
            settings.window_visibility.show_other_trains_window = parse_bool(value, settings.window_visibility.show_other_trains_window);
        } else if (key == "show_sound_list_window" || key == "show_soundlist_window") {
            settings.window_visibility.show_sound_list_window = parse_bool(value, settings.window_visibility.show_sound_list_window);
        } else if (key == "show_sound_3d_list_window" || key == "show_sound3d_list_window") {
            settings.window_visibility.show_sound_3d_list_window = parse_bool(value, settings.window_visibility.show_sound_3d_list_window);
        } else if (key == "show_repeaters_window") {
            settings.window_visibility.show_repeaters_window = parse_bool(value, settings.window_visibility.show_repeaters_window);
        } else if (key == "show_signal_aspects_window" || key == "show_signal_aspect_window") {
            settings.window_visibility.show_signal_aspects_window = parse_bool(value, settings.window_visibility.show_signal_aspects_window);
        } else if (key == "show_signals_window" || key == "show_signal_window") {
            settings.window_visibility.show_signals_window = parse_bool(value, settings.window_visibility.show_signals_window);
        } else if (key == "show_beacons_window" || key == "show_beacon_window") {
            settings.window_visibility.show_beacons_window = parse_bool(value, settings.window_visibility.show_beacons_window);
        } else if (key == "show_irregularities_window") {
            settings.window_visibility.show_irregularities_window = parse_bool(value, settings.window_visibility.show_irregularities_window);
        } else if (key == "show_map_sounds_window" || key == "show_map_sound_window") {
            settings.window_visibility.show_map_sounds_window = parse_bool(value, settings.window_visibility.show_map_sounds_window);
        } else if (key == "show_map_sound_3d_window" || key == "show_map_sounds_3d_window" || key == "show_map_sound3d_window") {
            settings.window_visibility.show_map_sound_3d_window = parse_bool(value, settings.window_visibility.show_map_sound_3d_window);
        } else if (key == "show_rolling_noises_window" || key == "show_rolling_noise_window") {
            settings.window_visibility.show_rolling_noises_window = parse_bool(value, settings.window_visibility.show_rolling_noises_window);
        } else if (key == "show_flange_noises_window" || key == "show_flange_noise_window") {
            settings.window_visibility.show_flange_noises_window = parse_bool(value, settings.window_visibility.show_flange_noises_window);
        } else if (key == "show_joint_noises_window" || key == "show_joint_noise_window") {
            settings.window_visibility.show_joint_noises_window = parse_bool(value, settings.window_visibility.show_joint_noises_window);
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
        } else if (key == "show_scene_preview_window" || key == "show_3d_scene_preview_window") {
            settings.window_visibility.show_scene_preview_window = parse_bool(value, settings.window_visibility.show_scene_preview_window);
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
        } else if (key == "show_beacon_markers" || key == "show_beacons" || key == "show_beacon_points") {
            view_2d_keys_seen.insert("show_beacon_markers");
            settings.view_2d.show_beacon_markers = parse_bool(value, settings.view_2d.show_beacon_markers);
        } else if (key == "show_pretrain_markers" || key == "show_pretrains" || key == "show_pretrain_points") {
            view_2d_keys_seen.insert("show_pretrain_markers");
            settings.view_2d.show_pretrain_markers = parse_bool(value, settings.view_2d.show_pretrain_markers);
        } else if (key == "show_map_sound_markers" || key == "show_map_sounds" || key == "show_map_sound_points") {
            view_2d_keys_seen.insert("show_map_sound_markers");
            settings.view_2d.show_map_sound_markers = parse_bool(value, settings.view_2d.show_map_sound_markers);
        } else if (key == "show_map_sound_3d_markers" || key == "show_map_sound3d_markers" || key == "show_map_sound_3d_points") {
            view_2d_keys_seen.insert("show_map_sound_3d_markers");
            settings.view_2d.show_map_sound_3d_markers = parse_bool(value, settings.view_2d.show_map_sound_3d_markers);
        } else if (key == "show_rolling_noise_markers" || key == "show_rolling_noises" || key == "show_rolling_noise_points") {
            view_2d_keys_seen.insert("show_rolling_noise_markers");
            settings.view_2d.show_rolling_noise_markers = parse_bool(value, settings.view_2d.show_rolling_noise_markers);
        } else if (key == "show_flange_noise_markers" || key == "show_flange_noises" || key == "show_flange_noise_points") {
            view_2d_keys_seen.insert("show_flange_noise_markers");
            settings.view_2d.show_flange_noise_markers = parse_bool(value, settings.view_2d.show_flange_noise_markers);
        } else if (key == "show_joint_noise_markers" || key == "show_joint_noises" || key == "show_joint_noise_points") {
            view_2d_keys_seen.insert("show_joint_noise_markers");
            settings.view_2d.show_joint_noise_markers = parse_bool(value, settings.view_2d.show_joint_noise_markers);
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
        } else if (key == "show_scene_owntrack_markers" ||
                   key == "show_scene_own_track_markers" ||
                   key == "show_3d_scene_owntrack_markers" ||
                   key == "show_scene_owntrack") {
            view_3d_keys_seen.insert("show_scene_owntrack_markers");
            settings.view_3d.show_scene_owntrack_markers = parse_bool(value, settings.view_3d.show_scene_owntrack_markers);
        } else if (key == "show_scene_current_position_on_plan" ||
                   key == "show_scene_current_camera_on_plan" ||
                   key == "show_3d_scene_current_position_on_plan") {
            view_3d_keys_seen.insert("show_scene_current_position_on_plan");
            settings.view_3d.show_scene_current_position_on_plan =
                parse_bool(value, settings.view_3d.show_scene_current_position_on_plan);
        } else if (key == "scene_draw_distance_m" ||
                   key == "scene_draw_distance" ||
                   key == "scene_window_forward_m" ||
                   key == "draw_distance_m" ||
                   key == "draw_distance") {
            view_3d_keys_seen.insert("scene_draw_distance_m");
            try {
                settings.view_3d.scene_draw_distance_m = clamp_scene_draw_distance(std::stod(value));
            } catch (...) {
                settings.view_3d.scene_draw_distance_m = kDefaultSceneDrawDistanceM;
            }
        }
    }
    settings.font_size = clamp_font_size(settings.font_size);
    settings.ui_component_size = clamp_ui_component_size(settings.ui_component_size);
    settings.marker_size_percent = clamp_marker_size_percent(settings.marker_size_percent);
    settings.canvas_line_widths = clamp_canvas_line_widths(settings.canvas_line_widths);
    settings.theme_color = clamp_theme_color(settings.theme_color);
    settings.view_2d.mode = normalize_view_2d_mode(settings.view_2d.mode);
    settings.view_2d.grid_mode = normalize_grid_mode(settings.view_2d.grid_mode);
    settings.view_3d.scene_draw_distance_m = clamp_scene_draw_distance(settings.view_3d.scene_draw_distance_m);
    if (view_2d_keys_seen.size() < 23 || view_3d_keys_seen.size() < 3) save_user_settings(settings);
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
    const float touch_padding = std::max(2.0f, 4.0f * dpi_scale);
    style.TouchExtraPadding = ImVec2(touch_padding, touch_padding);
    style.ScrollbarSize = std::max(style.ScrollbarSize, 16.0f * dpi_scale);
    style.GrabMinSize = std::max(style.GrabMinSize, 12.0f * dpi_scale);
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
