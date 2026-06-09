/*
 * Copyright (c) 2026 Sapporo_ningyo
 *
 * Licensed under Apache License 2.0; see LICENSE and NOTICE.
 * The GUI uses Dear ImGui and ImPlot; see THIRD_PARTY_NOTICES.md.
 */

#pragma once

#include "multilanguage.h"

#include "imgui.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <iosfwd>
#include <memory>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

struct ID3D11Device;
struct ID3D11ShaderResourceView;
class Canvas3D;

#ifndef NDEBUG
extern std::ostream* g_debug_plan_benchmark_log;
#endif

inline constexpr float kDefaultFontSize = 18.0f;
inline constexpr float kMinFontSize = 6.0f;
inline constexpr float kMaxFontSize = 32.0f;
inline constexpr float kDefaultUiComponentSize = 100.0f;
inline constexpr float kMinUiComponentSize = 50.0f;
inline constexpr float kMaxUiComponentSize = 200.0f;
inline constexpr float kDefaultStationMarkerSize = 4.0f;
inline constexpr float kMinStationMarkerSize = 1.0f;
inline constexpr float kMaxStationMarkerSize = 16.0f;
inline constexpr size_t kMaxRecentMaps = 10;

std::wstring utf8_to_wide(const std::string& text);
std::string wide_to_utf8(const std::wstring& text);
std::string format_double(double value, int precision = 6);
float clamp_font_size(float value);
float clamp_ui_component_size(float value);
float clamp_station_marker_size(float value);
ImVec4 default_theme_color();
ImVec4 clamp_theme_color(ImVec4 color);
std::string theme_color_to_string(const ImVec4& color);
std::string display_name_from_path(const std::string& path);

struct Matrix {
    std::vector<double> data;
    size_t rows = 0;
    size_t cols = 0;

    double at(size_t r, size_t c) const {
        return data[r * cols + c];
    }

    bool empty() const {
        return rows == 0 || cols == 0;
    }
};

struct TrackEvent {
    double distance = 0.0;
    std::string key;
    std::string flag;
    bool value_number = false;
    double number = 0.0;
    std::string text;
};

struct OtherTrack {
    std::string key;
    Matrix points;
    bool visible = false;
    double range_min = 0.0;
    double range_max = 0.0;
    ImVec4 color = ImVec4(0.2f, 0.7f, 1.0f, 1.0f);
};

struct Station {
    std::string key;
    std::string name;
    double distance = 0.0;
    double mileage = 0.0;
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

struct SpeedLimit {
    double distance = 0.0;
    bool has_speed = false;
    double speed = 0.0;
};

struct TableRow {
    std::map<std::string, std::string> cells;
};

struct TableColumnDef {
    const char* key;
    const char* header;
    float width = 0.0f;
};

struct CachedTableRow {
    std::vector<std::string> cells;
    std::string open_path;
    std::string tooltip_text;
};

struct TableUiCache {
    bool valid = false;
    float font_size = 0.0f;
    float cell_padding_x = 0.0f;
    std::vector<CachedTableRow> station_rows;
    std::vector<CachedTableRow> structure_rows;
    std::vector<CachedTableRow> structure_model_rows;
    std::vector<CachedTableRow> repeater_rows;
    std::vector<CachedTableRow> signal_aspect_rows;
    std::vector<CachedTableRow> signal_rows;
    std::vector<CachedTableRow> beacon_rows;
    std::vector<CachedTableRow> irregularity_rows;
    std::vector<CachedTableRow> rolling_noise_rows;
    std::vector<CachedTableRow> joint_noise_rows;
    std::vector<CachedTableRow> background_rows;
    std::vector<CachedTableRow> adhesion_rows;
    std::vector<CachedTableRow> cab_illuminance_rows;
    std::vector<CachedTableRow> fog_rows;
    std::vector<CachedTableRow> sound_list_rows;
    std::vector<CachedTableRow> sound_3d_list_rows;
    float structure_file_path_width = 200.0f;
    float structure_model_file_path_width = 200.0f;
    float signal_distance_width = 110.0f;
    float signal_file_path_width = 200.0f;
    float beacon_distance_width = 110.0f;
    float beacon_file_path_width = 200.0f;
    size_t signal_aspect_structure_key_columns = 0;
    std::vector<float> signal_aspect_structure_key_widths;
    float sound_list_file_path_width = 200.0f;
    float sound_list_buffer_count_width = 80.0f;
    float sound_3d_list_file_path_width = 200.0f;
    float sound_3d_list_buffer_count_width = 80.0f;
    float repeater_distance_width = 110.0f;
    float repeater_interval_width = 70.0f;
    float repeater_file_path_width = 200.0f;
    float irregularity_distance_width = 110.0f;
    float irregularity_file_path_width = 200.0f;
    float rolling_noise_distance_width = 110.0f;
    float rolling_noise_file_path_width = 200.0f;
    float joint_noise_distance_width = 110.0f;
    float joint_noise_file_path_width = 200.0f;
    float background_distance_width = 110.0f;
    float background_file_path_width = 200.0f;
    float adhesion_distance_width = 110.0f;
    float adhesion_file_path_width = 200.0f;
    float cab_illuminance_distance_width = 110.0f;
    float cab_illuminance_file_path_width = 200.0f;
    float fog_distance_width = 110.0f;
    float fog_file_path_width = 200.0f;
};

const std::string& table_cell(const TableRow& row, const std::string& key);
double table_cell_number(const TableRow& row, const std::string& key);

struct MapModel {
    std::string path;
    Matrix own;
    Matrix curve;
    std::vector<OtherTrack> other_tracks;
    std::vector<Station> stations;
    std::vector<TrackEvent> own_events;
    std::vector<SpeedLimit> speedlimits;
    std::vector<double> controlpoints;
    std::vector<TableRow> station_list_rows;
    std::vector<TableRow> structures;
    std::vector<TableRow> structure_models;
    std::vector<TableRow> sound_list;
    std::vector<TableRow> structures_between;
    std::vector<TableRow> repeaters;
    std::vector<TableRow> signal_aspects;
    std::vector<TableRow> signals;
    std::vector<TableRow> beacons;
    std::vector<TableRow> pretrains;
    std::vector<TableRow> irregularities;
    std::vector<TableRow> rolling_noises;
    std::vector<TableRow> joint_noises;
    std::vector<TableRow> backgrounds;
    std::vector<TableRow> adhesions;
    std::vector<TableRow> cab_illuminance;
    std::vector<TableRow> fogs;
    double distance_origin = 0.0;
    double height_origin = 0.0;
    double origin_angle = 0.0;
    double default_min = 0.0;
    double default_max = 0.0;
    double cp_default_min = 0.0;
    double cp_default_max = 0.0;
    double cp_arb[3] = {0.0, 0.0, 25.0};
    double buffer_copy_seconds = 0.0;
    bool has_cp_arb = false;
};

struct View2D {
    double cx = 0.0;
    double cy = 0.0;
    double scale = 1.0;
    double rotation = 0.0;
    bool fitted = false;
    bool dragging = false;
    bool rotating = false;
    ImVec2 last_mouse = ImVec2(0, 0);

    ImVec2 world_to_screen(double x, double y, ImVec2 origin, ImVec2 size) const {
        double dx = x - cx;
        double dy = y - cy;
        double c = std::cos(rotation);
        double s = std::sin(rotation);
        double rx = c * dx - s * dy;
        double ry = s * dx + c * dy;
        return ImVec2(origin.x + size.x * 0.5f + static_cast<float>(rx * scale),
                      origin.y + size.y * 0.5f + static_cast<float>(ry * scale));
    }

    ImVec2 screen_to_world(ImVec2 p, ImVec2 origin, ImVec2 size) const {
        double rx = (p.x - origin.x - size.x * 0.5) / scale;
        double ry = (p.y - origin.y - size.y * 0.5) / scale;
        double c = std::cos(rotation);
        double s = std::sin(rotation);
        return ImVec2(static_cast<float>(c * rx + s * ry + cx),
                      static_cast<float>(-s * rx + c * ry + cy));
    }

    void pan_by_screen_delta(ImVec2 delta) {
        double c = std::cos(rotation);
        double s = std::sin(rotation);
        double wx = -(c * delta.x / scale + s * delta.y / scale);
        double wy = (s * delta.x / scale - c * delta.y / scale);
        cx += wx;
        cy += wy;
    }

    void fit(double xmin, double ymin, double xmax, double ymax, ImVec2 size) {
        double dx = std::max(xmax - xmin, 1e-6);
        double dy = std::max(ymax - ymin, 1e-6);
        cx = (xmin + xmax) * 0.5;
        cy = (ymin + ymax) * 0.5;
        scale = std::max(0.001, std::min(size.x / dx, size.y / dy) * 0.88);
        fitted = true;
    }
};

struct TrackPoint {
    double d = 0.0;
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    double theta = 0.0;
    double radius = 0.0;
    double gradient = 0.0;
};

struct Section {
    double start = 0.0;
    double end = 0.0;
    double value = 0.0;
};

struct PlanStation {
    Station station;
    double x = 0.0;
    double y = 0.0;
};

struct PlanSpeed {
    double x = 0.0;
    double y = 0.0;
    double theta = 0.0;
    bool has_speed = false;
    double speed = 0.0;
};

struct PlanOther {
    std::string key;
    std::vector<TrackPoint> points;
    ImVec4 color;
};

struct PlanStructureMarker {
    double d = 0.0;
    double x = 0.0;
    double y = 0.0;
    std::string label;
    size_t row_index = 0;
};

struct PlanRepeaterMarker {
    double d = 0.0;
    double x = 0.0;
    double y = 0.0;
    std::string label;
    size_t row_index = 0;
};

struct PlanSignalMarker {
    double d = 0.0;
    double x = 0.0;
    double y = 0.0;
    std::string label;
    size_t row_index = 0;
};

struct PlanBeaconMarker {
    double d = 0.0;
    double x = 0.0;
    double y = 0.0;
    std::string label;
    size_t row_index = 0;
};

struct PlanPreTrainMarker {
    double d = 0.0;
    double x = 0.0;
    double y = 0.0;
    std::string label;
    size_t row_index = 0;
};

struct PlanIrregularityMarker {
    double d = 0.0;
    double x = 0.0;
    double y = 0.0;
    std::string label;
    size_t row_index = 0;
};

struct PlanRollingNoiseMarker {
    double d = 0.0;
    double x = 0.0;
    double y = 0.0;
    std::string label;
    size_t row_index = 0;
};

struct PlanJointNoiseMarker {
    double d = 0.0;
    double x = 0.0;
    double y = 0.0;
    std::string label;
    size_t row_index = 0;
};

struct PlanBackgroundMarker {
    double d = 0.0;
    double x = 0.0;
    double y = 0.0;
    std::string label;
    size_t row_index = 0;
};

struct PlanAdhesionMarker {
    double d = 0.0;
    double x = 0.0;
    double y = 0.0;
    std::string label;
    size_t row_index = 0;
};

struct PlanCabIlluminanceMarker {
    double d = 0.0;
    double x = 0.0;
    double y = 0.0;
    std::string label;
    size_t row_index = 0;
};

struct PlanFogMarker {
    double d = 0.0;
    double x = 0.0;
    double y = 0.0;
    std::string label;
    size_t row_index = 0;
};

struct PlanRepeaterSegment {
    struct Chunk {
        std::vector<TrackPoint> points;
        double d_min = 0.0;
        double d_max = 0.0;
        double x_min = 0.0;
        double y_min = 0.0;
        double x_max = 0.0;
        double y_max = 0.0;
        bool bounds_valid = false;
    };

    std::vector<Chunk> chunks;
    double d_min = 0.0;
    double d_max = 0.0;
    double x_min = 0.0;
    double y_min = 0.0;
    double x_max = 0.0;
    double y_max = 0.0;
    bool bounds_valid = false;
};

struct RepeaterOverlayRow {
    std::optional<PlanRepeaterMarker> begin_marker;
    std::optional<PlanRepeaterMarker> end_marker;
    PlanRepeaterSegment segment;
};

struct PlanData {
    std::vector<TrackPoint> own;
    std::vector<PlanOther> other;
    std::vector<PlanStation> stations;
    std::vector<PlanSpeed> speedlimits;
    std::vector<PlanStructureMarker> structure_markers;
    std::vector<PlanRepeaterMarker> repeater_markers;
    std::vector<PlanSignalMarker> signal_markers;
    std::vector<PlanBeaconMarker> beacon_markers;
    std::vector<PlanPreTrainMarker> pretrain_markers;
    std::vector<PlanIrregularityMarker> irregularity_markers;
    std::vector<PlanRollingNoiseMarker> rolling_noise_markers;
    std::vector<PlanJointNoiseMarker> joint_noise_markers;
    std::vector<PlanBackgroundMarker> background_markers;
    std::vector<PlanAdhesionMarker> adhesion_markers;
    std::vector<PlanCabIlluminanceMarker> cab_illuminance_markers;
    std::vector<PlanFogMarker> fog_markers;
    std::vector<Section> curve_sections;
    std::vector<Section> transition_sections;
    double origin_angle = 0.0;
    double xmin = -1.0;
    double ymin = -1.0;
    double xmax = 1.0;
    double ymax = 1.0;
};

struct ProfileOther {
    std::string key;
    std::vector<double> x;
    std::vector<double> y;
    ImVec4 color;
};

struct LabelPoint {
    double x = 0.0;
    double y = 0.0;
    std::string text;
};

struct ProfileData {
    std::vector<double> own_x;
    std::vector<double> own_y;
    std::vector<double> curve_x;
    std::vector<double> curve_y;
    std::vector<ProfileOther> other;
    std::vector<Station> stations;
    std::vector<LabelPoint> gradient_points;
    std::vector<LabelPoint> gradient_labels;
    std::vector<LabelPoint> radius_labels;
    double ymin = -5.0;
    double ymax = 5.0;
};

struct TextureImage {
    ID3D11ShaderResourceView* srv = nullptr;
    std::vector<unsigned char> pixels_rgba;
    int width = 0;
    int height = 0;
    double brightness = 100.0;
    std::string path;

    void release();
};

struct LogLine {
    std::string text;
    int severity = 0;
};

struct WindowVisibilitySettings {
    bool show_othertracks_window = true;
    bool show_station_list_window = false;
    bool show_structures_window = false;
    bool show_structure_models_window = false;
    bool show_sound_list_window = false;
    bool show_sound_3d_list_window = false;
    bool show_repeaters_window = false;
    bool show_signal_aspects_window = false;
    bool show_signals_window = false;
    bool show_beacons_window = false;
    bool show_irregularities_window = false;
    bool show_rolling_noises_window = false;
    bool show_joint_noises_window = false;
    bool show_backgrounds_window = false;
    bool show_adhesions_window = false;
    bool show_cab_illuminance_window = false;
    bool show_fogs_window = false;
    bool show_plots_window = true;
    bool show_model_preview_window = true;

    bool operator==(const WindowVisibilitySettings& other) const {
        return show_othertracks_window == other.show_othertracks_window &&
            show_station_list_window == other.show_station_list_window &&
            show_structures_window == other.show_structures_window &&
            show_structure_models_window == other.show_structure_models_window &&
            show_sound_list_window == other.show_sound_list_window &&
            show_sound_3d_list_window == other.show_sound_3d_list_window &&
            show_repeaters_window == other.show_repeaters_window &&
            show_signal_aspects_window == other.show_signal_aspects_window &&
            show_signals_window == other.show_signals_window &&
            show_beacons_window == other.show_beacons_window &&
            show_irregularities_window == other.show_irregularities_window &&
            show_rolling_noises_window == other.show_rolling_noises_window &&
            show_joint_noises_window == other.show_joint_noises_window &&
            show_backgrounds_window == other.show_backgrounds_window &&
            show_adhesions_window == other.show_adhesions_window &&
            show_cab_illuminance_window == other.show_cab_illuminance_window &&
            show_fogs_window == other.show_fogs_window &&
            show_plots_window == other.show_plots_window &&
            show_model_preview_window == other.show_model_preview_window;
    }

    bool operator!=(const WindowVisibilitySettings& other) const {
        return !(*this == other);
    }
};

struct View2DSettings {
    bool show_stations = true;
    bool show_station_names = true;
    bool show_station_mileage = true;
    bool show_gradient_pos = true;
    bool show_gradient_values = true;
    bool show_curve_values = true;
    bool show_profile_other = false;
    bool show_speedlimits = true;
    bool show_irregularity_markers = true;
    bool show_beacon_markers = true;
    bool show_pretrain_markers = true;
    bool show_rolling_noise_markers = true;
    bool show_joint_noise_markers = true;
    bool show_background_markers = true;
    bool show_adhesion_markers = true;
    bool show_cab_illuminance_markers = true;
    bool show_fog_markers = true;
    bool show_profile_graph = true;
    bool show_radius_graph = true;
    bool show_background_image = true;
    int mode = 0;
    int grid_mode = 0;

    bool operator==(const View2DSettings& other) const {
        return show_stations == other.show_stations &&
            show_station_names == other.show_station_names &&
            show_station_mileage == other.show_station_mileage &&
            show_gradient_pos == other.show_gradient_pos &&
            show_gradient_values == other.show_gradient_values &&
            show_curve_values == other.show_curve_values &&
            show_profile_other == other.show_profile_other &&
            show_speedlimits == other.show_speedlimits &&
            show_irregularity_markers == other.show_irregularity_markers &&
            show_beacon_markers == other.show_beacon_markers &&
            show_pretrain_markers == other.show_pretrain_markers &&
            show_rolling_noise_markers == other.show_rolling_noise_markers &&
            show_joint_noise_markers == other.show_joint_noise_markers &&
            show_background_markers == other.show_background_markers &&
            show_adhesion_markers == other.show_adhesion_markers &&
            show_cab_illuminance_markers == other.show_cab_illuminance_markers &&
            show_fog_markers == other.show_fog_markers &&
            show_profile_graph == other.show_profile_graph &&
            show_radius_graph == other.show_radius_graph &&
            show_background_image == other.show_background_image &&
            mode == other.mode &&
            grid_mode == other.grid_mode;
    }

    bool operator!=(const View2DSettings& other) const {
        return !(*this == other);
    }
};

struct UserSettings {
    Language language = Language::Zh;
    float font_size = kDefaultFontSize;
    float ui_component_size = kDefaultUiComponentSize;
    float station_marker_size = kDefaultStationMarkerSize;
    ImVec4 theme_color = default_theme_color();
    WindowVisibilitySettings window_visibility;
    View2DSettings view_2d;
    std::filesystem::path path;
};

struct BackgroundHistory {
    bool has_image = false;
    std::string image_path;
    double x = 0.0;
    double y = 0.0;
    double width = 0.0;
    double height = 0.0;
    double rotation_deg = 0.0;
    double brightness = 100.0;
};

struct RecentMapEntry {
    std::string path;
    BackgroundHistory background;
};

class App {
public:
    explicit App(ID3D11Device* device, UserSettings settings, float dpi_scale, bool viewports_enabled, bool has_saved_layout);
    ~App();

    void render();
    void after_frame_presented();
    void add_log(std::string text);
#ifndef NDEBUG
    static int run_debug_headless_plan_benchmark(const std::string& path, int frames,
                                                 double unit_distance, double pan_pixels,
                                                 double max_frame_ms, const std::string& output_path,
                                                 bool profile_stages);
#endif

private:
    ID3D11Device* device_ = nullptr;
    float dpi_scale_ = 1.0f;
    bool viewports_enabled_ = false;
    Translation i18n_;
    UserSettings settings_;
    Language lang_ = Language::Zh;
    float font_size_ = kDefaultFontSize;
    float pending_font_size_ = kDefaultFontSize;
    float font_size_before_dialog_ = kDefaultFontSize;
    float ui_component_size_ = kDefaultUiComponentSize;
    float pending_ui_component_size_ = kDefaultUiComponentSize;
    float ui_component_size_before_dialog_ = kDefaultUiComponentSize;
    float station_marker_size_ = kDefaultStationMarkerSize;
    float pending_station_marker_size_ = kDefaultStationMarkerSize;
    float station_marker_size_before_dialog_ = kDefaultStationMarkerSize;
    ImVec4 theme_color_ = default_theme_color();
    ImVec4 pending_theme_color_ = default_theme_color();
    ImVec4 theme_color_before_dialog_ = default_theme_color();
    WindowVisibilitySettings last_saved_window_visibility_;
    View2DSettings last_saved_view_2d_settings_;
    std::filesystem::path history_path_;
    std::vector<RecentMapEntry> recent_maps_;

    void* handle_ = nullptr;
    MapModel model_;
    bool has_model_ = false;
    std::string file_path_;

    std::vector<LogLine> logs_;
    std::mutex log_mutex_;
    std::string last_log_;
    int error_count_ = 0;
    int warn_count_ = 0;

    std::thread loader_;
    std::atomic<bool> loading_{false};
    struct LoadResult {
        bool ok = false;
        bool preserve_settings = false;
        bool record_history = false;
        std::optional<BackgroundHistory> background_to_restore;
        void* handle = nullptr;
        MapModel model;
        std::string path;
        std::string error;
        std::chrono::steady_clock::time_point started_at;
        double elapsed_seconds = 0.0;
    };
    std::mutex result_mutex_;
    std::optional<LoadResult> pending_result_;

    double dmin_ = 0.0;
    double dmax_ = 0.0;
    double plot_min_ = 0.0;
    double plot_max_ = 0.0;
    double cp_start_ = 0.0;
    double cp_end_ = 0.0;
    double cp_interval_ = 25.0;
    double unit_distance_ = 25.0;
    std::optional<std::chrono::steady_clock::time_point> pending_load_started_at_;
    bool plan_canvas_rendered_this_frame_ = false;

    bool show_stations_ = true;
    bool show_station_names_ = true;
    bool show_station_mileage_ = true;
    bool show_gradient_pos_ = true;
    bool show_gradient_values_ = true;
    bool show_curve_values_ = true;
    bool show_profile_other_ = false;
    bool show_speedlimits_ = true;
    bool show_irregularity_markers_ = true;
    bool show_beacon_markers_ = true;
    bool show_pretrain_markers_ = true;
    bool show_rolling_noise_markers_ = true;
    bool show_joint_noise_markers_ = true;
    bool show_background_markers_ = true;
    bool show_adhesion_markers_ = true;
    bool show_cab_illuminance_markers_ = true;
    bool show_fog_markers_ = true;
    bool show_profile_graph_ = true;
    bool show_radius_graph_ = true;
    bool show_othertracks_window_ = true;

    enum class GridMode { Fixed, Movable, None };
    enum class Mode { Pan, Measure };
    GridMode grid_mode_ = GridMode::Fixed;
    Mode mode_ = Mode::Pan;

    View2D plan_view_;
    bool keep_plan_view_ = false;
    double plan_height_ = 0.68;
    double graph_split_ = 0.52;

    std::optional<double> measure_distance_;
    std::string measure_text_;
    bool focus_profile_next_ = false;
    double focus_profile_distance_ = 0.0;
    bool focus_radius_next_ = false;
    double focus_radius_distance_ = 0.0;
    double profile_x_span_ = 0.0;
    double radius_x_span_ = 0.0;
    bool profile_x_zoom_pending_ = false;
    double profile_x_zoom_min_ = 0.0;
    double profile_x_zoom_max_ = 0.0;
    bool profile_plot_rect_valid_ = false;
    ImVec2 profile_plot_pos_ = ImVec2(0.0f, 0.0f);
    ImVec2 profile_plot_size_ = ImVec2(0.0f, 0.0f);
    bool reset_profile_axes_next_ = true;
    bool reset_radius_axes_next_ = true;

    bool show_station_list_window_ = false;
    bool show_structures_window_ = false;
    bool show_structure_models_window_ = false;
    bool show_sound_list_window_ = false;
    bool show_sound_3d_list_window_ = false;
    bool show_repeaters_window_ = false;
    bool show_signal_aspects_window_ = false;
    bool show_signals_window_ = false;
    bool show_beacons_window_ = false;
    bool show_irregularities_window_ = false;
    bool show_rolling_noises_window_ = false;
    bool show_joint_noises_window_ = false;
    bool show_backgrounds_window_ = false;
    bool show_adhesions_window_ = false;
    bool show_cab_illuminance_window_ = false;
    bool show_fogs_window_ = false;
    bool show_plots_window_ = true;
    bool show_model_preview_window_ = true;
    bool focus_structures_next_ = false;
    bool focus_repeaters_next_ = false;
    bool focus_signal_aspects_next_ = false;
    bool focus_signals_next_ = false;
    bool focus_beacons_next_ = false;
    bool focus_irregularities_next_ = false;
    bool focus_rolling_noises_next_ = false;
    bool focus_joint_noises_next_ = false;
    bool focus_backgrounds_next_ = false;
    bool focus_adhesions_next_ = false;
    bool focus_cab_illuminance_next_ = false;
    bool focus_fogs_next_ = false;
    bool focus_model_preview_next_ = false;
    bool focus_plots_next_ = true;
    bool show_range_popup_ = false;
    bool show_cp_popup_ = false;
    bool show_bg_adjust_popup_ = false;
    bool show_align_popup_ = false;
    bool show_about_popup_ = false;
    bool show_font_size_popup_ = false;
    bool has_saved_layout_ = false;
    bool initial_dockspace_done_ = false;
    ImGuiID dock_right_id_ = 0;
    ImGuiID dock_main_id_ = 0;
    TableUiCache table_cache_;
    char structure_model_find_query_[256] = {};
    std::string structure_model_find_committed_;
    std::vector<size_t> structure_model_find_matches_;
    std::vector<unsigned char> structure_model_find_row_matches_;
    std::vector<unsigned char> structure_model_unused_row_matches_;
    size_t structure_model_unused_count_ = 0;
    size_t structure_model_unused_total_ = 0;
    int structure_model_find_current_ = -1;
    int structure_model_find_scroll_row_ = -1;
    bool structure_model_find_has_run_ = false;
    bool structure_model_find_exact_ = false;
    bool structure_model_find_panel_expanded_ = true;
    bool structure_model_unused_has_run_ = false;
    std::vector<std::optional<PlanStructureMarker>> structure_marker_cache_;
    std::vector<RepeaterOverlayRow> repeater_marker_cache_;
    std::vector<std::optional<PlanSignalMarker>> signal_marker_cache_;
    std::vector<std::optional<PlanBeaconMarker>> beacon_marker_cache_;
    std::vector<std::optional<PlanPreTrainMarker>> pretrain_marker_cache_;
    std::vector<std::optional<PlanIrregularityMarker>> irregularity_marker_cache_;
    std::vector<std::optional<PlanRollingNoiseMarker>> rolling_noise_marker_cache_;
    std::vector<std::optional<PlanJointNoiseMarker>> joint_noise_marker_cache_;
    std::vector<std::optional<PlanBackgroundMarker>> background_marker_cache_;
    std::vector<std::optional<PlanAdhesionMarker>> adhesion_marker_cache_;
    std::vector<std::optional<PlanCabIlluminanceMarker>> cab_illuminance_marker_cache_;
    std::vector<std::optional<PlanFogMarker>> fog_marker_cache_;
    std::vector<unsigned char> structure_row_visible_;
    std::vector<unsigned char> repeater_row_visible_;
    std::vector<unsigned char> signal_row_visible_;
    int structure_list_scroll_row_ = -1;
    int structure_list_highlight_row_ = -1;
    int repeater_list_scroll_row_ = -1;
    int repeater_list_highlight_row_ = -1;
    int signal_list_scroll_row_ = -1;
    int signal_list_highlight_row_ = -1;
    int beacon_list_scroll_row_ = -1;
    int beacon_list_highlight_row_ = -1;
    int irregularity_list_scroll_row_ = -1;
    int irregularity_list_highlight_row_ = -1;
    int rolling_noise_list_scroll_row_ = -1;
    int rolling_noise_list_highlight_row_ = -1;
    int joint_noise_list_scroll_row_ = -1;
    int joint_noise_list_highlight_row_ = -1;
    int background_list_scroll_row_ = -1;
    int background_list_highlight_row_ = -1;
    int adhesion_list_scroll_row_ = -1;
    int adhesion_list_highlight_row_ = -1;
    int cab_illuminance_list_scroll_row_ = -1;
    int cab_illuminance_list_highlight_row_ = -1;
    int fog_list_scroll_row_ = -1;
    int fog_list_highlight_row_ = -1;
    int plan_structure_popup_row_ = -1;
    int plan_repeater_popup_row_ = -1;
    int plan_signal_popup_row_ = -1;
    int plan_beacon_popup_row_ = -1;
    int plan_irregularity_popup_row_ = -1;
    int plan_rolling_noise_popup_row_ = -1;
    int plan_joint_noise_popup_row_ = -1;
    int plan_background_popup_row_ = -1;
    int plan_adhesion_popup_row_ = -1;
    int plan_cab_illuminance_popup_row_ = -1;
    int plan_fog_popup_row_ = -1;
    std::optional<ImVec2> plan_focus_arrow_;
    double plan_focus_arrow_until_ = 0.0;
    std::unique_ptr<Canvas3D> model_preview_canvas_;
    ImVec4 model_preview_bg_color_ = ImVec4(0.0f, 0.0f, 0.0f, 1.0f);

    TextureImage bg_image_;
    bool bg_show_ = true;
    double bg_x_ = 0.0;
    double bg_y_ = 0.0;
    double bg_width_ = 5000.0;
    double bg_height_ = 5000.0;
    double bg_rotation_deg_ = 0.0;
    double bg_brightness_ = 100.0;
    double pending_bg_x_ = 0.0;
    double pending_bg_y_ = 0.0;
    double pending_bg_width_ = 5000.0;
    double pending_bg_height_ = 5000.0;
    double pending_bg_rotation_deg_ = 0.0;
    double pending_bg_brightness_ = 100.0;
    int station_jump_index_ = 0;
    int align_station1_ = 0;
    int align_station2_ = 1;
    std::optional<ImVec2> align_pick1_;
    std::optional<ImVec2> align_pick2_;
    int pick_slot_ = 0;

    const std::string& tr(const std::string& key) const { return i18n_.get(lang_, key); }

    static void log_callback(const char* message);

    void stop_loader();
    void poll_loader();
    void begin_load(std::string path, bool preserve_settings, bool record_history = false,
                    std::optional<BackgroundHistory> background_to_restore = std::nullopt);
    void apply_load_result(LoadResult result);
    void regenerate_geometry();
    static LoadResult load_map_worker(std::string path, double unit_distance, bool has_cp, double cp_start, double cp_end, double cp_step);
    static MapModel build_model_from_handle(void* handle, const std::string& path);

    void handle_shortcuts();
    void render_menu();
    void render_toolbar();
    void render_mode_grid_controls();
    void render_station_jump_combo();
    void render_console();
    void render_plots();
    void render_plan_canvas(ImVec2 size);
    void render_profile_plot(const ProfileData& data, ImVec2 size);
    void render_radius_plot(const ProfileData& data, ImVec2 size);
    void render_othertracks_window();
    void render_station_list_window();
    void render_structures_window();
    void render_structure_models_window();
    void render_sound_list_window();
    void render_sound_3d_list_window();
    void render_repeaters_window();
    void render_signal_aspects_window();
    void render_signals_window();
    void render_beacons_window();
    void render_irregularities_window();
    void render_rolling_noises_window();
    void render_joint_noises_window();
    void render_backgrounds_window();
    void render_adhesions_window();
    void render_cab_illuminance_window();
    void render_fogs_window();
    void render_model_preview_window();
    void preview_structure_model(const std::string& path);
    void reload_model_preview();
    void reload_current_map_and_model_preview();
    void render_popups();
    void setup_initial_dockspace(ImGuiID dockspace_id);
    WindowVisibilitySettings current_window_visibility() const;
    void apply_window_visibility_settings(const WindowVisibilitySettings& visibility);
    View2DSettings current_view_2d_settings() const;
    void apply_view_2d_settings(const View2DSettings& settings);
    void save_runtime_settings_if_changed();
    void invalidate_table_cache();
    void ensure_table_cache();
    void reset_structure_model_find_results();
    void run_structure_model_find();
    void run_unused_structure_model_search();
    void find_structure_model_for_structure_key(const std::string& structure_key);
    void step_structure_model_find(int delta);
    std::string structure_model_find_status_text() const;
    void rebuild_marker_overlay_cache();
    void reset_marker_visibility();
    void sync_marker_visibility_sizes();
    void locate_structure_row_on_plan(size_t row_index);
    void locate_structure_row_in_list(size_t row_index);
    void locate_repeater_row_on_plan(size_t row_index);
    void locate_repeater_row_in_list(size_t row_index);
    void locate_signal_row_on_plan(size_t row_index);
    void locate_signal_row_in_list(size_t row_index);
    void locate_beacon_row_on_plan(size_t row_index);
    void locate_beacon_row_in_list(size_t row_index);
    void locate_irregularity_row_on_plan(size_t row_index);
    void locate_irregularity_row_in_list(size_t row_index);
    void locate_rolling_noise_row_on_plan(size_t row_index);
    void locate_rolling_noise_row_in_list(size_t row_index);
    void locate_joint_noise_row_on_plan(size_t row_index);
    void locate_joint_noise_row_in_list(size_t row_index);
    void locate_background_row_on_plan(size_t row_index);
    void locate_background_row_in_list(size_t row_index);
    void locate_adhesion_row_on_plan(size_t row_index);
    void locate_adhesion_row_in_list(size_t row_index);
    void locate_cab_illuminance_row_on_plan(size_t row_index);
    void locate_cab_illuminance_row_in_list(size_t row_index);
    void locate_fog_row_on_plan(size_t row_index);
    void locate_fog_row_in_list(size_t row_index);

    PlanData build_plan_data(bool include_other_tracks = true) const;
    ProfileData build_profile_data() const;
    std::vector<Section> curve_sections(bool transition) const;
    size_t nearest_own_index(double distance) const;
    double interp_own_z(double distance) const;
    std::optional<TrackPoint> track_info_at(double distance) const;
    std::optional<SpeedLimit> speed_at(double distance) const;
    void clear_measure();
    void update_measure(double distance);
    void center_plan_at_distance(double distance);
    std::optional<ImVec2> plan_point_from_model_xy(double x, double y) const;
    void focus_plan_at_model_point(double x, double y);
    void request_plot_focus(double distance, bool include_profile, bool include_radius);
    void handle_measure_plot_double_click(bool include_profile, bool include_radius);
    void focus_station(double distance);
    void export_csv();
    void save_history();
    void touch_recent_map(const std::string& path);
    void save_current_background_to_history();
    BackgroundHistory current_background_history() const;
    void sync_pending_background_values();
    void apply_pending_background_values(bool save_history_entry);
    bool apply_background_history(const BackgroundHistory& background);
    void clear_background_image();
    bool load_background_image(const std::string& path, bool reset_parameters = true);
    bool rebuild_background_texture();
    std::optional<ImVec2> background_uv_from_world(ImVec2 world) const;
    void draw_background(ImDrawList* draw, const View2D& view, ImVec2 origin, ImVec2 size);
    void apply_background_alignment();
    std::string open_map_dialog();
    std::string open_image_dialog();
    std::string choose_folder_dialog();
};
