/*
 * Copyright (c) 2026 Sapporo_ningyo
 *
 * Licensed under Apache License 2.0; see LICENSE and NOTICE.
 * The GUI uses Dear ImGui and ImPlot; see THIRD_PARTY_NOTICES.md.
 */

#ifdef _MSC_VER
#pragma execution_character_set("utf-8")
#endif

#include "app_settings.h"
#include "runtime_paths.h"

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
#include <string_view>
#include <vector>
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
    if (!std::isfinite(value)) return k_default_font_size;
    return std::clamp(value, k_min_font_size, k_max_font_size);
}

float clamp_ui_component_size(float value) {
    if (!std::isfinite(value)) return k_default_ui_component_size;
    return std::clamp(value, k_min_ui_component_size, k_max_ui_component_size);
}

float clamp_marker_size_percent(float value) {
    if (!std::isfinite(value)) return k_default_marker_size_percent;
    value = std::clamp(value, k_min_marker_size_percent, k_max_marker_size_percent);
    const int rounded = static_cast<int>(std::round(value / k_marker_size_percent_step)) * k_marker_size_percent_step;
    return std::clamp(static_cast<float>(rounded), k_min_marker_size_percent, k_max_marker_size_percent);
}

float marker_size_scale_from_percent(float value) {
    return clamp_marker_size_percent(value) / 100.0f;
}

float clamp_canvas_line_width(float value, float fallback) {
    if (!std::isfinite(value)) return fallback;
    const float rounded = std::round(value / k_canvas_line_width_step_px) * k_canvas_line_width_step_px;
    return std::clamp(rounded, k_min_canvas_line_width_px, k_max_canvas_line_width_px);
}

CanvasLineWidthSettings clamp_canvas_line_widths(CanvasLineWidthSettings value) {
    value.own_track_px = clamp_canvas_line_width(value.own_track_px, k_default_own_track_line_width_px);
    value.other_track_px = clamp_canvas_line_width(value.other_track_px, k_default_other_track_line_width_px);
    value.chart_marker_px = clamp_canvas_line_width(value.chart_marker_px, k_default_chart_marker_line_width_px);
    value.background_grid_px = clamp_canvas_line_width(value.background_grid_px, k_default_background_grid_line_width_px);
    return value;
}

int clamp_scene_draw_distance(double value) {
    if (!std::isfinite(value)) return k_default_scene_draw_distance_m;
    value = std::clamp(value,
                       static_cast<double>(k_min_scene_draw_distance_m),
                       static_cast<double>(k_max_scene_draw_distance_m));
    const int rounded = static_cast<int>(std::round(value / k_scene_draw_distance_step_m)) * k_scene_draw_distance_step_m;
    return std::clamp(rounded, k_min_scene_draw_distance_m, k_max_scene_draw_distance_m);
}

int clamp_scene_edit_component_size_percent(double value) {
    if (!std::isfinite(value)) return k_default_scene_edit_component_size_percent;
    value = std::clamp(value,
                       static_cast<double>(k_min_scene_edit_component_size_percent),
                       static_cast<double>(k_max_scene_edit_component_size_percent));
    const int rounded = static_cast<int>(
        std::round(value / k_scene_edit_component_size_step_percent)) *
        k_scene_edit_component_size_step_percent;
    return std::clamp(rounded,
                      k_min_scene_edit_component_size_percent,
                      k_max_scene_edit_component_size_percent);
}

int clamp_scene_instance_warning_threshold(double value, int fallback) {
    if (!std::isfinite(value)) value = fallback;
    value = std::clamp(value,
                       static_cast<double>(k_min_scene_instance_warning_threshold),
                       static_cast<double>(k_max_scene_instance_warning_threshold));
    const int rounded = static_cast<int>(
        std::round(value / k_scene_instance_warning_threshold_step)) *
        k_scene_instance_warning_threshold_step;
    return std::clamp(rounded,
                      k_min_scene_instance_warning_threshold,
                      k_max_scene_instance_warning_threshold);
}

void normalize_scene_instance_warning_thresholds(int& warning_threshold,
                                                 int& critical_warning_threshold) {
    warning_threshold = clamp_scene_instance_warning_threshold(
        warning_threshold, k_default_scene_instance_warning_threshold);
    critical_warning_threshold = clamp_scene_instance_warning_threshold(
        critical_warning_threshold, k_default_scene_instance_critical_warning_threshold);
    critical_warning_threshold = std::max(critical_warning_threshold, warning_threshold);
}

ImVec4 default_theme_color() {
    return ImVec4(0.26f, 0.59f, 0.98f, 1.0f);
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
    if (value.size() != 6) return std::nullopt;
    for (char ch : value) {
        if (hex_digit(ch) < 0) return std::nullopt;
    }
    auto byte_at = [&](size_t pos) {
        return hex_digit(value[pos]) * 16 + hex_digit(value[pos + 1]);
    };
    return clamp_theme_color(ImVec4(
        byte_at(0) / 255.0f,
        byte_at(2) / 255.0f,
        byte_at(4) / 255.0f,
        1.0f));
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

std::optional<Language> language_from_string(const std::string& text) {
    const std::string value = trim_ascii(text);
    if (value == "ja") return Language::Ja;
    if (value == "en") return Language::En;
    if (value == "zh") return Language::Zh;
    return std::nullopt;
}

std::filesystem::path default_settings_path() {
    return runtime_paths::settings_directory() / L"settings.ini";
}

std::filesystem::path default_history_path() {
    return runtime_paths::settings_directory() / L"history.ini";
}

std::filesystem::path default_imgui_ini_path() {
    return runtime_paths::settings_directory() / L"imgui.ini";
}

std::string bool_to_string(bool value) {
    return value ? "true" : "false";
}

std::optional<bool> parse_bool(const std::string& value) {
    const std::string text = trim_ascii(value);
    if (text == "true") return true;
    if (text == "false") return false;
    return std::nullopt;
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

std::optional<int> view_2d_mode_from_string(const std::string& value) {
    const std::string text = trim_ascii(value);
    if (text == "measure") return 1;
    if (text == "pan") return 0;
    return std::nullopt;
}

std::optional<int> grid_mode_from_string(const std::string& value) {
    const std::string text = trim_ascii(value);
    if (text == "fixed") return 0;
    if (text == "movable") return 1;
    if (text == "none") return 2;
    return std::nullopt;
}

std::optional<double> parse_finite_number(const std::string& value) {
    const std::string text = trim_ascii(value);
    if (text.empty()) return std::nullopt;
    size_t position = 0;
    if (text[position] == '+' || text[position] == '-') ++position;
    bool integer_digits = false;
    while (position < text.size() &&
           std::isdigit(static_cast<unsigned char>(text[position]))) {
        integer_digits = true;
        ++position;
    }
    bool fractional_digits = false;
    if (position < text.size() && text[position] == '.') {
        ++position;
        while (position < text.size() &&
               std::isdigit(static_cast<unsigned char>(text[position]))) {
            fractional_digits = true;
            ++position;
        }
    }
    if (!integer_digits && !fractional_digits) return std::nullopt;
    if (position < text.size() &&
        (text[position] == 'e' || text[position] == 'E')) {
        ++position;
        if (position < text.size() &&
            (text[position] == '+' || text[position] == '-')) {
            ++position;
        }
        const size_t exponent_start = position;
        while (position < text.size() &&
               std::isdigit(static_cast<unsigned char>(text[position]))) {
            ++position;
        }
        if (position == exponent_start) return std::nullopt;
    }
    if (position != text.size()) return std::nullopt;
    try {
        size_t used = 0;
        const double parsed = std::stod(text, &used);
        if (used != text.size() || !std::isfinite(parsed)) return std::nullopt;
        return parsed;
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<float> parse_bounded_float(
    const std::string& value, float minimum, float maximum) {
    auto parsed = parse_finite_number(value);
    if (!parsed) return std::nullopt;
    return static_cast<float>(std::clamp(
        *parsed, static_cast<double>(minimum), static_cast<double>(maximum)));
}

template <typename Owner, size_t Count>
bool apply_bool_setting(
    std::string_view key,
    const std::string& value,
    Owner& target,
    const Owner& defaults,
    const std::array<std::pair<std::string_view, bool Owner::*>, Count>& fields) {
    for (const auto& field : fields) {
        if (field.first != key) continue;
        target.*(field.second) = parse_bool(value).value_or(defaults.*(field.second));
        return true;
    }
    return false;
}

template <typename Owner, size_t Count>
void write_bool_settings(
    std::ostream& out,
    const Owner& source,
    const std::array<std::pair<std::string_view, bool Owner::*>, Count>& fields,
    size_t begin = 0,
    size_t end = Count) {
    for (size_t index = begin; index < end; ++index) {
        const auto& field = fields[index];
        out << field.first << "=" << bool_to_string(source.*(field.second)) << "\n";
    }
}

static constexpr std::array<
    std::pair<std::string_view, bool WindowVisibilitySettings::*>, 33>
    k_window_visibility_bool_fields{{
        {"show_othertracks_window", &WindowVisibilitySettings::show_othertracks_window},
        {"show_station_list_window", &WindowVisibilitySettings::show_station_list_window},
        {"show_structures_window", &WindowVisibilitySettings::show_structures_window},
        {"show_structures_between_window", &WindowVisibilitySettings::show_structures_between_window},
        {"show_structure_models_window", &WindowVisibilitySettings::show_structure_models_window},
        {"show_other_trains_window", &WindowVisibilitySettings::show_other_trains_window},
        {"show_sound_list_window", &WindowVisibilitySettings::show_sound_list_window},
        {"show_sound_3d_list_window", &WindowVisibilitySettings::show_sound_3d_list_window},
        {"show_repeaters_window", &WindowVisibilitySettings::show_repeaters_window},
        {"show_signal_aspects_window", &WindowVisibilitySettings::show_signal_aspects_window},
        {"show_signals_window", &WindowVisibilitySettings::show_signals_window},
        {"show_sections_window", &WindowVisibilitySettings::show_sections_window},
        {"show_variables_window", &WindowVisibilitySettings::show_variables_window},
        {"show_beacons_window", &WindowVisibilitySettings::show_beacons_window},
        {"show_irregularities_window", &WindowVisibilitySettings::show_irregularities_window},
        {"show_map_sounds_window", &WindowVisibilitySettings::show_map_sounds_window},
        {"show_map_sound_3d_window", &WindowVisibilitySettings::show_map_sound_3d_window},
        {"show_rolling_noises_window", &WindowVisibilitySettings::show_rolling_noises_window},
        {"show_flange_noises_window", &WindowVisibilitySettings::show_flange_noises_window},
        {"show_joint_noises_window", &WindowVisibilitySettings::show_joint_noises_window},
        {"show_backgrounds_window", &WindowVisibilitySettings::show_backgrounds_window},
        {"show_adhesions_window", &WindowVisibilitySettings::show_adhesions_window},
        {"show_cab_illuminance_window", &WindowVisibilitySettings::show_cab_illuminance_window},
        {"show_fogs_window", &WindowVisibilitySettings::show_fogs_window},
        {"show_legacy_fogs_window", &WindowVisibilitySettings::show_legacy_fogs_window},
        {"show_lighting_window", &WindowVisibilitySettings::show_lighting_window},
        {"show_draw_distances_window", &WindowVisibilitySettings::show_draw_distances_window},
        {"show_speed_limits_window", &WindowVisibilitySettings::show_speed_limits_window},
        {"show_file_structure_window", &WindowVisibilitySettings::show_file_structure_window},
        {"show_console_window", &WindowVisibilitySettings::show_console_window},
        {"show_plots_window", &WindowVisibilitySettings::show_plots_window},
        {"show_model_preview_window", &WindowVisibilitySettings::show_model_preview_window},
        {"show_scene_preview_window", &WindowVisibilitySettings::show_scene_preview_window},
    }};

static constexpr std::array<std::pair<std::string_view, bool View2DSettings::*>, 28>
    k_view_2d_bool_fields{{
        {"show_stations", &View2DSettings::show_stations},
        {"show_station_names", &View2DSettings::show_station_names},
        {"show_station_mileage", &View2DSettings::show_station_mileage},
        {"show_gradient_pos", &View2DSettings::show_gradient_pos},
        {"show_gradient_values", &View2DSettings::show_gradient_values},
        {"show_curve_values", &View2DSettings::show_curve_values},
        {"show_curve_gauge_markers", &View2DSettings::show_curve_gauge_markers},
        {"show_curve_center_markers", &View2DSettings::show_curve_center_markers},
        {"show_curve_function_markers", &View2DSettings::show_curve_function_markers},
        {"show_profile_other", &View2DSettings::show_profile_other},
        {"show_speedlimits", &View2DSettings::show_speedlimits},
        {"show_section_markers", &View2DSettings::show_section_markers},
        {"show_irregularity_markers", &View2DSettings::show_irregularity_markers},
        {"show_beacon_markers", &View2DSettings::show_beacon_markers},
        {"show_pretrain_markers", &View2DSettings::show_pretrain_markers},
        {"show_map_sound_markers", &View2DSettings::show_map_sound_markers},
        {"show_map_sound_3d_markers", &View2DSettings::show_map_sound_3d_markers},
        {"show_rolling_noise_markers", &View2DSettings::show_rolling_noise_markers},
        {"show_flange_noise_markers", &View2DSettings::show_flange_noise_markers},
        {"show_joint_noise_markers", &View2DSettings::show_joint_noise_markers},
        {"show_background_markers", &View2DSettings::show_background_markers},
        {"show_adhesion_markers", &View2DSettings::show_adhesion_markers},
        {"show_cab_illuminance_markers", &View2DSettings::show_cab_illuminance_markers},
        {"show_fog_markers", &View2DSettings::show_fog_markers},
        {"show_draw_distance_markers", &View2DSettings::show_draw_distance_markers},
        {"show_profile_graph", &View2DSettings::show_profile_graph},
        {"show_radius_graph", &View2DSettings::show_radius_graph},
        {"show_background_image", &View2DSettings::show_background_image},
    }};

static constexpr std::array<std::pair<std::string_view, bool View3DSettings::*>, 6>
    k_view_3d_bool_fields{{
        {"show_scene_owntrack_markers", &View3DSettings::show_scene_owntrack_markers},
        {"show_scene_current_position_on_plan", &View3DSettings::show_scene_current_position_on_plan},
        {"scene_fog_enabled", &View3DSettings::scene_fog_enabled},
        {"scene_map_draw_distance_enabled", &View3DSettings::scene_map_draw_distance_enabled},
        {"scene_auto_load_on_map_open", &View3DSettings::scene_auto_load_on_map_open},
        {"scene_performance_warning_enabled", &View3DSettings::scene_performance_warning_enabled},
    }};

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
    out << "edit_mode_enabled=" << bool_to_string(settings.edit_mode_enabled) << "\n";
    out << "edit_mode_warning_suppressed=" << bool_to_string(settings.edit_mode_warning_suppressed) << "\n";
    out << "\n[WindowVisibility]\n";
    write_bool_settings(out, settings.window_visibility, k_window_visibility_bool_fields);
    out << "\n[View2D]\n";
    write_bool_settings(out, settings.view_2d, k_view_2d_bool_fields);
    out << "mode=" << view_2d_mode_to_string(settings.view_2d.mode) << "\n";
    out << "grid_mode=" << grid_mode_to_string(settings.view_2d.grid_mode) << "\n";
    out << "\n[View3D]\n";
    write_bool_settings(out, settings.view_3d, k_view_3d_bool_fields, 0, 5);
    out << "scene_draw_distance_m=" << clamp_scene_draw_distance(settings.view_3d.scene_draw_distance_m) << "\n";
    out << "scene_edit_component_size_percent="
        << clamp_scene_edit_component_size_percent(settings.view_3d.scene_edit_component_size_percent)
        << "\n";
    out << "scene_camera_speed_percent=" << settings.view_3d.scene_camera_speed_percent << "\n";
    int scene_instance_warning_threshold = settings.view_3d.scene_instance_warning_threshold;
    int scene_instance_critical_warning_threshold =
        settings.view_3d.scene_instance_critical_warning_threshold;
    normalize_scene_instance_warning_thresholds(
        scene_instance_warning_threshold, scene_instance_critical_warning_threshold);
    write_bool_settings(out, settings.view_3d, k_view_3d_bool_fields, 5, 6);
    out << "scene_instance_warning_threshold=" << scene_instance_warning_threshold << "\n";
    out << "scene_instance_critical_warning_threshold="
        << scene_instance_critical_warning_threshold << "\n";
    return true;
}

UserSettings load_user_settings(const std::filesystem::path& path) {
    const UserSettings defaults;
    UserSettings settings = defaults;
    settings.path = path;

    std::error_code ec;
    const bool exists = std::filesystem::exists(path, ec);
    if (ec || !exists) {
        save_user_settings(settings);
        return settings;
    }

    std::ifstream in(path, std::ios::binary);
    if (!in) return settings;

    std::string section;
    std::string line;
    while (std::getline(in, line)) {
        const size_t comment = line.find_first_of(";#");
        if (comment != std::string::npos) line.erase(comment);
        const std::string trimmed_line = trim_ascii(line);
        if (trimmed_line.empty()) continue;
        if (trimmed_line.front() == '[') {
            section.clear();
            if (trimmed_line.back() != ']') continue;
            const std::string candidate =
                trimmed_line.substr(1, trimmed_line.size() - 2);
            if (candidate == "General" || candidate == "WindowVisibility" ||
                candidate == "View2D" || candidate == "View3D") {
                section = candidate;
            }
            continue;
        }

        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = trim_ascii(line.substr(0, eq));
        const std::string value = trim_ascii(line.substr(eq + 1));

        if (section == "General") {
            if (key == "language") {
                settings.language = language_from_string(value).value_or(defaults.language);
            } else if (key == "font_size") {
                auto parsed = parse_bounded_float(value, k_min_font_size, k_max_font_size);
                settings.font_size = parsed ? clamp_font_size(*parsed) : defaults.font_size;
            } else if (key == "ui_component_size") {
                auto parsed = parse_bounded_float(
                    value, k_min_ui_component_size, k_max_ui_component_size);
                settings.ui_component_size =
                    parsed ? clamp_ui_component_size(*parsed) : defaults.ui_component_size;
            } else if (key == "marker_size_percent") {
                auto parsed = parse_bounded_float(
                    value, k_min_marker_size_percent, k_max_marker_size_percent);
                settings.marker_size_percent =
                    parsed ? clamp_marker_size_percent(*parsed) : defaults.marker_size_percent;
            } else if (key == "own_track_line_width_px") {
                auto parsed = parse_bounded_float(
                    value, k_min_canvas_line_width_px, k_max_canvas_line_width_px);
                settings.canvas_line_widths.own_track_px = parsed
                    ? clamp_canvas_line_width(*parsed, k_default_own_track_line_width_px)
                    : defaults.canvas_line_widths.own_track_px;
            } else if (key == "other_track_line_width_px") {
                auto parsed = parse_bounded_float(
                    value, k_min_canvas_line_width_px, k_max_canvas_line_width_px);
                settings.canvas_line_widths.other_track_px = parsed
                    ? clamp_canvas_line_width(*parsed, k_default_other_track_line_width_px)
                    : defaults.canvas_line_widths.other_track_px;
            } else if (key == "chart_marker_line_width_px") {
                auto parsed = parse_bounded_float(
                    value, k_min_canvas_line_width_px, k_max_canvas_line_width_px);
                settings.canvas_line_widths.chart_marker_px = parsed
                    ? clamp_canvas_line_width(*parsed, k_default_chart_marker_line_width_px)
                    : defaults.canvas_line_widths.chart_marker_px;
            } else if (key == "background_grid_line_width_px") {
                auto parsed = parse_bounded_float(
                    value, k_min_canvas_line_width_px, k_max_canvas_line_width_px);
                settings.canvas_line_widths.background_grid_px = parsed
                    ? clamp_canvas_line_width(*parsed, k_default_background_grid_line_width_px)
                    : defaults.canvas_line_widths.background_grid_px;
            } else if (key == "theme_color") {
                settings.theme_color = parse_theme_color(value).value_or(defaults.theme_color);
            } else if (key == "edit_mode_enabled") {
                settings.edit_mode_enabled =
                    parse_bool(value).value_or(defaults.edit_mode_enabled);
            } else if (key == "edit_mode_warning_suppressed") {
                settings.edit_mode_warning_suppressed =
                    parse_bool(value).value_or(defaults.edit_mode_warning_suppressed);
            }
        } else if (section == "WindowVisibility") {
            apply_bool_setting(key, value, settings.window_visibility,
                               defaults.window_visibility, k_window_visibility_bool_fields);
        } else if (section == "View2D") {
            if (!apply_bool_setting(
                    key, value, settings.view_2d, defaults.view_2d, k_view_2d_bool_fields)) {
                if (key == "mode") {
                    settings.view_2d.mode =
                        view_2d_mode_from_string(value).value_or(defaults.view_2d.mode);
                } else if (key == "grid_mode") {
                    settings.view_2d.grid_mode =
                        grid_mode_from_string(value).value_or(defaults.view_2d.grid_mode);
                }
            }
        } else if (section == "View3D") {
            if (apply_bool_setting(
                    key, value, settings.view_3d, defaults.view_3d, k_view_3d_bool_fields)) {
                continue;
            }
            auto parsed = parse_finite_number(value);
            if (key == "scene_draw_distance_m") {
                settings.view_3d.scene_draw_distance_m = parsed
                    ? clamp_scene_draw_distance(*parsed)
                    : defaults.view_3d.scene_draw_distance_m;
            } else if (key == "scene_edit_component_size_percent") {
                settings.view_3d.scene_edit_component_size_percent = parsed
                    ? clamp_scene_edit_component_size_percent(*parsed)
                    : defaults.view_3d.scene_edit_component_size_percent;
            } else if (key == "scene_camera_speed_percent") {
                settings.view_3d.scene_camera_speed_percent = parsed
                    ? static_cast<int>(std::clamp(
                          *parsed,
                          static_cast<double>(k_min_scene_camera_speed_percent),
                          static_cast<double>(k_max_scene_camera_speed_percent)))
                    : defaults.view_3d.scene_camera_speed_percent;
            } else if (key == "scene_instance_warning_threshold") {
                settings.view_3d.scene_instance_warning_threshold = parsed
                    ? clamp_scene_instance_warning_threshold(
                          *parsed, k_default_scene_instance_warning_threshold)
                    : defaults.view_3d.scene_instance_warning_threshold;
            } else if (key == "scene_instance_critical_warning_threshold") {
                settings.view_3d.scene_instance_critical_warning_threshold = parsed
                    ? clamp_scene_instance_warning_threshold(
                          *parsed, k_default_scene_instance_critical_warning_threshold)
                    : defaults.view_3d.scene_instance_critical_warning_threshold;
            }
        }
    }

    settings.canvas_line_widths = clamp_canvas_line_widths(settings.canvas_line_widths);
    settings.theme_color = clamp_theme_color(settings.theme_color);
    settings.view_2d.mode = normalize_view_2d_mode(settings.view_2d.mode);
    settings.view_2d.grid_mode = normalize_grid_mode(settings.view_2d.grid_mode);
    normalize_scene_instance_warning_thresholds(
        settings.view_3d.scene_instance_warning_threshold,
        settings.view_3d.scene_instance_critical_warning_threshold);
    return settings;
}

UserSettings load_user_settings() {
    return load_user_settings(default_settings_path());
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
    return parse_finite_number(value).value_or(fallback);
}

std::string history_number(double value) {
    std::ostringstream out;
    out << std::setprecision(17) << value;
    return out.str();
}

std::optional<size_t> parse_decimal_index(
    std::string_view text, size_t maximum) {
    if (text.empty()) return std::nullopt;
    if (text.size() > 1 && text.front() == '0') return std::nullopt;
    size_t value = 0;
    for (char ch : text) {
        if (ch < '0' || ch > '9') return std::nullopt;
        const size_t digit = static_cast<size_t>(ch - '0');
        if (digit > maximum || value > (maximum - digit) / 10) return std::nullopt;
        value = value * 10 + digit;
    }
    return value;
}

std::optional<int> parse_history_section_index(const std::string& section) {
    if (section.rfind("Map", 0) != 0) return std::nullopt;
    auto index = parse_decimal_index(
        std::string_view(section).substr(3), k_max_recent_maps - 1);
    return index ? std::optional<int>(static_cast<int>(*index)) : std::nullopt;
}

std::vector<RecentMapEntry> load_history_entries(const std::filesystem::path& path) {
    ensure_history_file(path);
    std::ifstream in(path, std::ios::binary);
    if (!in) return {};

    const BackgroundHistory default_background;
    std::optional<size_t> recent_count;
    std::map<int, RecentMapEntry> parsed;
    bool in_recent_section = false;
    int current_index = -1;
    std::string line;
    while (std::getline(in, line)) {
        std::string trimmed_line = trim_ascii(line);
        if (trimmed_line.empty() || trimmed_line.front() == ';' || trimmed_line.front() == '#') continue;
        if (trimmed_line.front() == '[') {
            in_recent_section = false;
            current_index = -1;
            if (trimmed_line.back() != ']') continue;
            std::string section = trimmed_line.substr(1, trimmed_line.size() - 2);
            in_recent_section = section == "Recent";
            auto index = in_recent_section
                ? std::optional<int>{}
                : parse_history_section_index(section);
            current_index = index.value_or(-1);
            if (current_index >= 0 && parsed.find(current_index) == parsed.end()) parsed[current_index] = {};
            continue;
        }
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = trim_ascii(line.substr(0, eq));
        std::string value = trim_ascii(line.substr(eq + 1));
        if (in_recent_section) {
            if (key == "count") {
                recent_count = parse_decimal_index(value, k_max_recent_maps);
            }
            continue;
        }
        if (current_index < 0) continue;
        RecentMapEntry& entry = parsed[current_index];
        if (key == "path") {
            entry.path = normalized_storage_path(value);
        } else if (key == "bg_path") {
            entry.background.has_image = !value.empty();
            entry.background.image_path = normalized_storage_path(value);
        } else if (key == "bg_x") {
            entry.background.x = parse_history_double(value, default_background.x);
        } else if (key == "bg_y") {
            entry.background.y = parse_history_double(value, default_background.y);
        } else if (key == "bg_width") {
            entry.background.width = parse_history_double(value, default_background.width);
        } else if (key == "bg_height") {
            entry.background.height = parse_history_double(value, default_background.height);
        } else if (key == "bg_rotation") {
            entry.background.rotation_deg =
                parse_history_double(value, default_background.rotation_deg);
        } else if (key == "bg_brightness") {
            entry.background.brightness =
                parse_history_double(value, default_background.brightness);
        }
    }

    if (!recent_count) return {};
    std::vector<RecentMapEntry> entries;
    std::set<std::string> seen;
    for (auto& kv : parsed) {
        if (static_cast<size_t>(kv.first) >= *recent_count) continue;
        RecentMapEntry entry = std::move(kv.second);
        if (entry.path.empty()) continue;
        std::string key = normalized_path_key(entry.path);
        if (!seen.insert(key).second) continue;
        entries.push_back(std::move(entry));
        if (entries.size() >= k_max_recent_maps) break;
    }
    return entries;
}

bool save_history_entries(const std::filesystem::path& path, const std::vector<RecentMapEntry>& entries) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    if (entries.empty()) return true;
    size_t count = std::min(entries.size(), k_max_recent_maps);
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
    ImGui::GetStyle().FontScaleMain = clamp_font_size(font_size) / k_default_font_size;
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
