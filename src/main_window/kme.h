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
#include <array>
#include <atomic>
#include <chrono>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iosfwd>
#include <memory>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

struct ID3D11Device;
struct ID3D11ShaderResourceView;
class Canvas3D;
enum class Canvas3DSceneMarkerListKind : std::uint8_t;
struct Canvas3DPlacementDragUpdate;
struct Canvas3DPlacementEditTarget;
struct KvEditReportSnapshot;

template <typename T>
inline void release_com(T*& pointer) {
    if (pointer) {
        pointer->Release();
        pointer = nullptr;
    }
}

#ifndef NDEBUG
extern std::ostream* g_debug_plan_benchmark_log;
struct HeadlessOpenBenchmarkOptions;
#endif


inline constexpr float k_default_font_size = 18.0f;
inline constexpr float k_min_font_size = 6.0f;
inline constexpr float k_max_font_size = 32.0f;
inline constexpr float k_default_ui_component_size = 100.0f;
inline constexpr float k_min_ui_component_size = 50.0f;
inline constexpr float k_max_ui_component_size = 200.0f;
inline constexpr float k_default_station_marker_size = 4.0f;
inline constexpr float k_default_marker_size_percent = 100.0f;
inline constexpr float k_min_marker_size_percent = 20.0f;
inline constexpr float k_max_marker_size_percent = 1000.0f;
inline constexpr int k_marker_size_percent_step = 10;
inline constexpr float k_default_own_track_line_width_px = 2.0f;
inline constexpr float k_default_other_track_line_width_px = 1.5f;
inline constexpr float k_default_chart_marker_line_width_px = 1.0f;
inline constexpr float k_default_background_grid_line_width_px = 1.0f;
inline constexpr float k_min_canvas_line_width_px = 1.0f;
inline constexpr float k_max_canvas_line_width_px = 20.0f;
inline constexpr float k_canvas_line_width_step_px = 0.5f;
inline constexpr int k_default_scene_draw_distance_m = 1200;
inline constexpr int k_min_scene_draw_distance_m = 200;
inline constexpr int k_max_scene_draw_distance_m = 10000;
inline constexpr int k_scene_draw_distance_step_m = 100;
inline constexpr int k_default_scene_edit_component_size_percent = 100;
inline constexpr int k_min_scene_edit_component_size_percent = 50;
inline constexpr int k_max_scene_edit_component_size_percent = 500;
inline constexpr int k_scene_edit_component_size_step_percent = 10;
inline constexpr int k_default_scene_camera_speed_percent = 100;
inline constexpr int k_min_scene_camera_speed_percent = 50;
inline constexpr int k_max_scene_camera_speed_percent = 400;
inline constexpr int k_scene_camera_speed_step_percent = 10;
inline constexpr bool k_default_scene_performance_warning_enabled = true;
inline constexpr int k_default_scene_instance_warning_threshold = 3000;
inline constexpr int k_default_scene_instance_critical_warning_threshold = 5000;
inline constexpr int k_min_scene_instance_warning_threshold = 1000;
inline constexpr int k_max_scene_instance_warning_threshold = 8000;
inline constexpr int k_scene_instance_warning_threshold_step = 200;
inline constexpr double k_scene_window_back_distance_m = 100.0;
inline constexpr bool k_default_scene_fog_enabled = true;
inline constexpr bool k_default_scene_map_draw_distance_enabled = true;
inline constexpr size_t k_max_recent_maps = 10;
inline constexpr const char* k_own_track_lookup_aliases[] = {"", "0", "1", "\\", "own", "main"};

inline std::string normalize_track_lookup_key(std::string key) {
    key.erase(std::remove_if(key.begin(), key.end(), [](unsigned char ch) {
        return std::isspace(ch) != 0;
    }), key.end());
    for (char& ch : key) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return key;
}

inline double angle_lerp(double a, double b, double t) {
    const double delta = std::atan2(std::sin(b - a), std::cos(b - a));
    return a + delta * t;
}

inline std::string trim_gui_ascii_copy(const std::string& text) {
    const size_t first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const size_t last = text.find_last_not_of(" \t\r\n");
    return text.substr(first, last - first + 1);
}

inline bool is_own_track_lookup_alias(const std::string& normalized_key) {
    for (const char* alias : k_own_track_lookup_aliases) {
        if (normalized_key == alias) return true;
    }
    return false;
}

inline bool is_own_track_placement_key(const std::string& normalized_key) {
    return normalized_key.empty() || normalized_key == "0" ||
        normalized_key == "''" || normalized_key == "'0'";
}

struct CanvasLineWidthSettings {
    float own_track_px = k_default_own_track_line_width_px;
    float other_track_px = k_default_other_track_line_width_px;
    float chart_marker_px = k_default_chart_marker_line_width_px;
    float background_grid_px = k_default_background_grid_line_width_px;
};

std::wstring utf8_to_wide(const std::string& text);
std::string wide_to_utf8(const std::wstring& text);
void set_crosshair_cursor();
void set_move_cursor();
std::string format_double(double value, int precision = 6);
float clamp_font_size(float value);
float clamp_ui_component_size(float value);
float clamp_marker_size_percent(float value);
float marker_size_scale_from_percent(float value);
float clamp_canvas_line_width(float value, float fallback);
CanvasLineWidthSettings clamp_canvas_line_widths(CanvasLineWidthSettings value);
int clamp_scene_draw_distance(double value);
int clamp_scene_edit_component_size_percent(double value);
int clamp_scene_instance_warning_threshold(double value, int fallback);
void normalize_scene_instance_warning_thresholds(int& warning_threshold,
                                                 int& critical_warning_threshold);
ImVec4 default_theme_color();
inline float clamp_color_component(float value) {
    if (!std::isfinite(value)) return 0.0f;
    return std::clamp(value, 0.0f, 1.0f);
}
inline ImVec4 clamp_theme_color(ImVec4 color) {
    color.x = clamp_color_component(color.x);
    color.y = clamp_color_component(color.y);
    color.z = clamp_color_component(color.z);
    color.w = 1.0f;
    return color;
}
std::string theme_color_to_string(const ImVec4& color);
std::string display_name_from_path(const std::string& path);
void open_parent_directory_in_explorer(const std::string& file_path);

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

inline double matrix_track_tangent(const Matrix& points, size_t row) {
    if (points.rows < 2 || points.cols < 3) return 0.0;
    size_t first = row == 0 ? 0 : row - 1;
    size_t last = row + 1 < points.rows ? row + 1 : row;
    if (first == last && last + 1 < points.rows) ++last;
    if (first == last) return 0.0;
    const double dx = points.at(last, 1) - points.at(first, 1);
    const double dy = points.at(last, 2) - points.at(first, 2);
    if (std::abs(dx) < 1e-9 && std::abs(dy) < 1e-9) return 0.0;
    return std::atan2(dy, dx);
}

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

struct EditSourceInfo {
    std::string file_path;
    int line = 0;
    int column = 0;
    std::string raw_text_preview;
};

inline constexpr size_t k_no_file_structure_parent = static_cast<size_t>(-1);

struct FileStructureNode {
    size_t parent_index = k_no_file_structure_parent;
    std::string include_path;
    std::string absolute_path;
    std::string display_name;
};

struct FileStructureDiagramGroupLayout {
    size_t parent_index = k_no_file_structure_parent;
    bool draw_border = false;
    ImVec2 min;
    ImVec2 max;
};

struct FileStructureDiagramLayoutCache {
    std::uint64_t source_revision = 0;
    size_t node_count = 0;
    float font_size = 0.0f;
    ImVec2 frame_padding;
    ImVec2 item_spacing;
    ImVec2 window_padding;
    float node_height = 0.0f;
    std::vector<ImVec2> text_sizes;
    std::vector<float> node_widths;
    std::vector<ImVec2> node_positions;
    std::vector<FileStructureDiagramGroupLayout> groups;
    ImVec2 content_size;
};

struct TextPreviewLineRange {
    size_t begin = 0;
    size_t end = 0;
};

enum class TextPreviewSelectionKind { None, Line, BetweenLines };

struct TextPreviewSelection {
    TextPreviewSelectionKind kind = TextPreviewSelectionKind::None;
    // Line uses a zero-based line index. BetweenLines uses a gap index in
    // [0, line_count], so future insert/move tools can target source gaps.
    size_t index = 0;
};

struct DistanceResolutionBoundary {
    std::string token;
    int line = 0;
    int column = 0;
    bool recommended = false;
};

struct DistanceResolutionRequest {
    std::string resolution_key;
    std::string reason;
    std::string source_file;
    std::vector<std::string> include_stack;
    std::string target_distance;
    std::string variable_name;
    std::string suggested_expression;
    std::string insertion_preview;
    bool can_confirm_reuse = false;
    std::string source_section_direction;
    std::vector<DistanceResolutionBoundary> allowed_boundaries;
    std::vector<std::string> affected_edit_ids;
};

struct TextPreviewPlacementState {
    bool active = false;
    std::string resolution_key;
    std::string insertion_preview;
    std::vector<DistanceResolutionBoundary> allowed_boundaries;
    std::string selected_boundary_token;
    int scroll_to_line = 0;
    bool scroll_pending = false;
};

struct TextPreviewState {
    bool open = false;
    bool focus_next = false;
    bool parser_source = false;
    std::string file_path;
    std::string encoding;
    std::string text;
    std::vector<TextPreviewLineRange> lines;
    std::string error;
    TextPreviewSelection selection;
    TextPreviewPlacementState placement;
    const ImFont* measured_font = nullptr;
    float measured_font_size = 0.0f;
    float max_text_width = 0.0f;
};

struct EditSourceFileInfo {
    std::string file_path;
    std::string display_path;
    std::string encoding;
    std::string newline;
    std::string source_hash;
    size_t byte_length = 0;
};

struct EditStatementInfo {
    std::string edit_id;
    std::string statement_kind;
    EditSourceInfo source;
    std::string raw_text;
    std::string raw_arguments;
    std::string distance_expression;
    double distance_value = 0.0;
    int global_order = 0;
};

struct EditElementInfo {
    std::string edit_id;
    std::string row_kind;
    size_t row_index = 0;
    std::string source_file_path;
    int global_order = 0;
};

struct TableRow {
    std::map<std::string, std::string> cells;
    std::string edit_id;
    EditSourceInfo source;
};

struct TableColumnDef {
    const char* key;
    const char* header;
    float width = 0.0f;
};

struct EditableListDisplayRow {
    size_t logical_row = 0;
    size_t structure_field_offset = 0;
    size_t structure_field_count = 0;
    std::string sequence;
    bool secondary = false;
};

struct CachedTableRow {
    std::vector<std::string> cells;
    std::string edit_id;
    EditSourceInfo source;
    std::string open_path;
    std::string tooltip_text;
    size_t editable_field_count = 0;
    size_t primary_structure_field_count = 0;
    size_t secondary_structure_field_count = 0;
    size_t repeater_chain_begin_index = 0;
    size_t repeater_chain_begin_count = 1;
    bool invalid_track_key = false;
};

struct CachedOtherTrainStopGroup {
    std::string train_key;
    std::vector<size_t> row_indices;
};

struct TableUiCache {
    bool valid = false;
    float font_size = 0.0f;
    float cell_padding_x = 0.0f;
    std::vector<CachedTableRow> station_position_rows;
    std::vector<CachedTableRow> station_definition_rows;
    std::vector<CachedTableRow> structure_rows;
    std::vector<CachedTableRow> structure_between_rows;
    std::vector<CachedTableRow> structure_model_rows;
    std::vector<CachedTableRow> other_train_rows;
    std::vector<CachedTableRow> other_train_stop_rows;
    std::vector<CachedOtherTrainStopGroup> other_train_stop_groups;
    std::vector<CachedTableRow> repeater_rows;
    std::vector<CachedTableRow> signal_aspect_rows;
    std::vector<EditableListDisplayRow> signal_aspect_display_rows;
    std::vector<CachedTableRow> signal_rows;
    std::vector<CachedTableRow> beacon_rows;
    std::vector<CachedTableRow> irregularity_rows;
    std::vector<CachedTableRow> map_sound_rows;
    std::vector<CachedTableRow> map_sound_3d_rows;
    std::vector<CachedTableRow> rolling_noise_rows;
    std::vector<CachedTableRow> flange_noise_rows;
    std::vector<CachedTableRow> joint_noise_rows;
    std::vector<CachedTableRow> background_rows;
    std::vector<CachedTableRow> adhesion_rows;
    std::vector<CachedTableRow> cab_illuminance_rows;
    std::vector<CachedTableRow> fog_rows;
    std::vector<CachedTableRow> draw_distance_rows;
    std::vector<CachedTableRow> sound_list_rows;
    std::vector<CachedTableRow> sound_3d_list_rows;
    float structure_file_path_width = 200.0f;
    float structure_between_file_path_width = 200.0f;
    float structure_model_file_path_width = 200.0f;
    float other_train_distance_width = 110.0f;
    float other_train_file_path_width = 200.0f;
    float other_train_stop_distance_width = 110.0f;
    float other_train_stop_file_path_width = 200.0f;
    float signal_distance_width = 110.0f;
    float signal_file_path_width = 200.0f;
    float beacon_distance_width = 110.0f;
    float beacon_file_path_width = 200.0f;
    size_t signal_aspect_structure_key_columns = 0;
    std::vector<std::string> signal_aspect_column_headers;
    std::vector<float> signal_aspect_column_widths;
    float sound_list_file_path_width = 200.0f;
    float sound_list_buffer_count_width = 80.0f;
    float sound_3d_list_file_path_width = 200.0f;
    float sound_3d_list_buffer_count_width = 80.0f;
    float repeater_distance_width = 110.0f;
    float repeater_interval_width = 70.0f;
    float repeater_file_path_width = 200.0f;
    float irregularity_distance_width = 110.0f;
    float irregularity_file_path_width = 200.0f;
    float map_sound_distance_width = 110.0f;
    float map_sound_file_path_width = 200.0f;
    float map_sound_3d_distance_width = 110.0f;
    float map_sound_3d_file_path_width = 200.0f;
    float rolling_noise_distance_width = 110.0f;
    float rolling_noise_file_path_width = 200.0f;
    float flange_noise_distance_width = 110.0f;
    float flange_noise_file_path_width = 200.0f;
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
    float draw_distance_distance_width = 110.0f;
    float draw_distance_file_path_width = 200.0f;
};

const std::string& table_cell(const TableRow& row, const std::string& key);
double table_cell_number(const TableRow& row, const std::string& key);
struct MapModel;
void annotate_scene_track_key_warnings(MapModel& model);

struct MapModel {
    std::string path;
    std::vector<FileStructureNode> file_structure;
    std::uint64_t file_structure_revision = 0;
    std::vector<EditSourceFileInfo> edit_files;
    std::vector<EditStatementInfo> edit_statements;
    std::vector<EditElementInfo> edit_elements;
    Matrix own;
    Matrix curve;
    std::vector<OtherTrack> other_tracks;
    std::vector<Station> station_positions;
    std::vector<Station> stations;
    std::map<std::string, std::string> station_names;
    std::vector<TrackEvent> own_events;
    std::vector<SpeedLimit> speedlimits;
    std::vector<double> controlpoints;
    std::vector<TableRow> station_list_rows;
    std::vector<TableRow> station_definition_rows;
    std::vector<TableRow> structures;
    std::vector<TableRow> structure_models;
    std::vector<TableRow> other_trains;
    std::vector<TableRow> other_train_stops;
    std::vector<TableRow> other_train_structure_keys;
    std::vector<TableRow> other_train_sound_3d_keys;
    std::vector<TableRow> sound_list;
    std::vector<TableRow> sound_3d_list;
    std::vector<TableRow> structures_between;
    std::vector<TableRow> repeaters;
    std::vector<TableRow> signal_aspects;
    std::vector<TableRow> signals;
    std::vector<TableRow> beacons;
    std::vector<TableRow> pretrains;
    std::vector<TableRow> irregularities;
    std::vector<TableRow> map_sounds;
    std::vector<TableRow> map_sound_3d;
    std::vector<TableRow> rolling_noises;
    std::vector<TableRow> flange_noises;
    std::vector<TableRow> joint_noises;
    std::vector<TableRow> backgrounds;
    std::vector<TableRow> adhesions;
    std::vector<TableRow> cab_illuminance;
    std::vector<TableRow> fogs;
    std::vector<TableRow> draw_distances;
    std::vector<std::string> scene_track_key_warnings;
    double distance_origin = 0.0;
    double height_origin = 0.0;
    double origin_angle = 0.0;
    double default_min = 0.0;
    double default_max = 0.0;
    double cp_arb[3] = {0.0, 0.0, 25.0};
    double buffer_copy_seconds = 0.0;
    double snapshot_build_seconds = 0.0;
    double snapshot_hydrate_seconds = 0.0;
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

struct PlanMarker {
    double d = 0.0;
    double x = 0.0;
    double y = 0.0;
    std::string label;
    std::string edit_id;
    size_t row_index = 0;
};

using PlanStructureMarker = PlanMarker;
using PlanRepeaterMarker = PlanMarker;
using PlanSignalMarker = PlanMarker;
using PlanBeaconMarker = PlanMarker;
using PlanPreTrainMarker = PlanMarker;
using PlanIrregularityMarker = PlanMarker;
using PlanMapSoundMarker = PlanMarker;
using PlanMapSound3DMarker = PlanMarker;
using PlanRollingNoiseMarker = PlanMarker;
using PlanFlangeNoiseMarker = PlanMarker;
using PlanJointNoiseMarker = PlanMarker;
using PlanBackgroundMarker = PlanMarker;
using PlanAdhesionMarker = PlanMarker;
using PlanCabIlluminanceMarker = PlanMarker;
using PlanFogMarker = PlanMarker;
using PlanDrawDistanceMarker = PlanMarker;

struct PlanOtherTrainStopMarker {
    double d = 0.0;
    double x = 0.0;
    double y = 0.0;
    double theta = 0.0;
    std::string label;
    std::string edit_id;
    size_t row_index = 0;
    size_t definition_row_index = 0;
    bool reverse_direction = false;
};
enum class PlanMarkerKind {
    None,
    Structure,
    Repeater,
    Signal,
    Beacon,
    PreTrain,
    OtherTrainStop,
    Irregularity,
    MapSound,
    MapSound3D,
    RollingNoise,
    FlangeNoise,
    JointNoise,
    Background,
    Adhesion,
    CabIlluminance,
    Fog,
    DrawDistance
};

struct PlanMarkerSelection {
    PlanMarkerKind kind = PlanMarkerKind::None;
    size_t row_index = 0;

    bool matches(PlanMarkerKind other_kind, size_t other_row_index) const {
        return kind == other_kind && row_index == other_row_index;
    }

    void clear() {
        kind = PlanMarkerKind::None;
        row_index = 0;
    }
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
    TrackPoint first_point;
    TrackPoint last_point;
    double d_min = 0.0;
    double d_max = 0.0;
    double x_min = 0.0;
    double y_min = 0.0;
    double x_max = 0.0;
    double y_max = 0.0;
    bool endpoints_valid = false;
    bool bounds_valid = false;
};

struct RepeaterOverlayRow {
    std::optional<PlanRepeaterMarker> begin_marker;
    std::optional<PlanRepeaterMarker> end_marker;
    PlanRepeaterSegment segment;
};

struct OtherTrainPathOverlay {
    std::vector<TrackPoint> points;
    std::string label;
    size_t definition_row_index = 0;
    double d_min = 0.0;
    double d_max = 0.0;
    bool reverse_direction = false;
};

struct PlanData {
    std::vector<TrackPoint> own;
    std::vector<PlanStation> stations;
    std::vector<PlanSpeed> speedlimits;
    std::vector<PlanStructureMarker> structure_markers;
    std::vector<PlanRepeaterMarker> repeater_markers;
    std::vector<PlanSignalMarker> signal_markers;
    std::vector<PlanBeaconMarker> beacon_markers;
    std::vector<PlanPreTrainMarker> pretrain_markers;
    std::vector<PlanOtherTrainStopMarker> other_train_stop_markers;
    std::vector<OtherTrainPathOverlay> other_train_paths;
    std::vector<PlanIrregularityMarker> irregularity_markers;
    std::vector<PlanMapSoundMarker> map_sound_markers;
    std::vector<PlanMapSound3DMarker> map_sound_3d_markers;
    std::vector<PlanRollingNoiseMarker> rolling_noise_markers;
    std::vector<PlanFlangeNoiseMarker> flange_noise_markers;
    std::vector<PlanJointNoiseMarker> joint_noise_markers;
    std::vector<PlanBackgroundMarker> background_markers;
    std::vector<PlanAdhesionMarker> adhesion_markers;
    std::vector<PlanCabIlluminanceMarker> cab_illuminance_markers;
    std::vector<PlanFogMarker> fog_markers;
    std::vector<PlanDrawDistanceMarker> draw_distance_markers;
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

enum class LogSeverity : std::uint8_t {
    Info,
    Warning,
    Error,
};

struct LogLine {
    std::string text;
    LogSeverity severity = LogSeverity::Info;
};

struct WindowVisibilitySettings {
    bool show_othertracks_window = true;
    bool show_station_list_window = false;
    bool show_structures_window = false;
    bool show_structures_between_window = false;
    bool show_structure_models_window = false;
    bool show_other_trains_window = false;
    bool show_sound_list_window = false;
    bool show_sound_3d_list_window = false;
    bool show_repeaters_window = false;
    bool show_signal_aspects_window = false;
    bool show_signals_window = false;
    bool show_beacons_window = false;
    bool show_irregularities_window = false;
    bool show_map_sounds_window = false;
    bool show_map_sound_3d_window = false;
    bool show_rolling_noises_window = false;
    bool show_flange_noises_window = false;
    bool show_joint_noises_window = false;
    bool show_backgrounds_window = false;
    bool show_adhesions_window = false;
    bool show_cab_illuminance_window = false;
    bool show_fogs_window = false;
    bool show_draw_distances_window = false;
    bool show_file_structure_window = false;
    bool show_console_window = true;
    bool show_plots_window = true;
    bool show_model_preview_window = true;
    bool show_scene_preview_window = true;

    bool operator==(const WindowVisibilitySettings& other) const {
        return show_othertracks_window == other.show_othertracks_window &&
            show_station_list_window == other.show_station_list_window &&
            show_structures_window == other.show_structures_window &&
            show_structures_between_window == other.show_structures_between_window &&
            show_structure_models_window == other.show_structure_models_window &&
            show_other_trains_window == other.show_other_trains_window &&
            show_sound_list_window == other.show_sound_list_window &&
            show_sound_3d_list_window == other.show_sound_3d_list_window &&
            show_repeaters_window == other.show_repeaters_window &&
            show_signal_aspects_window == other.show_signal_aspects_window &&
            show_signals_window == other.show_signals_window &&
            show_beacons_window == other.show_beacons_window &&
            show_irregularities_window == other.show_irregularities_window &&
            show_map_sounds_window == other.show_map_sounds_window &&
            show_map_sound_3d_window == other.show_map_sound_3d_window &&
            show_rolling_noises_window == other.show_rolling_noises_window &&
            show_flange_noises_window == other.show_flange_noises_window &&
            show_joint_noises_window == other.show_joint_noises_window &&
            show_backgrounds_window == other.show_backgrounds_window &&
            show_adhesions_window == other.show_adhesions_window &&
            show_cab_illuminance_window == other.show_cab_illuminance_window &&
            show_fogs_window == other.show_fogs_window &&
            show_draw_distances_window == other.show_draw_distances_window &&
            show_file_structure_window == other.show_file_structure_window &&
            show_console_window == other.show_console_window &&
            show_plots_window == other.show_plots_window &&
            show_model_preview_window == other.show_model_preview_window &&
            show_scene_preview_window == other.show_scene_preview_window;
    }

    bool operator!=(const WindowVisibilitySettings& other) const {
        return !(*this == other);
    }
};

inline constexpr bool k_default_station_aux_info_visible = true;
inline constexpr bool k_default_non_station_aux_info_visible = false;

struct View2DSettings {
    bool show_stations = k_default_station_aux_info_visible;
    bool show_station_names = k_default_station_aux_info_visible;
    bool show_station_mileage = k_default_station_aux_info_visible;
    bool show_gradient_pos = true;
    bool show_gradient_values = true;
    bool show_curve_values = k_default_non_station_aux_info_visible;
    bool show_profile_other = false;
    bool show_speedlimits = k_default_non_station_aux_info_visible;
    bool show_irregularity_markers = k_default_non_station_aux_info_visible;
    bool show_beacon_markers = k_default_non_station_aux_info_visible;
    bool show_pretrain_markers = k_default_non_station_aux_info_visible;
    bool show_map_sound_markers = k_default_non_station_aux_info_visible;
    bool show_map_sound_3d_markers = k_default_non_station_aux_info_visible;
    bool show_rolling_noise_markers = k_default_non_station_aux_info_visible;
    bool show_flange_noise_markers = k_default_non_station_aux_info_visible;
    bool show_joint_noise_markers = k_default_non_station_aux_info_visible;
    bool show_background_markers = k_default_non_station_aux_info_visible;
    bool show_adhesion_markers = k_default_non_station_aux_info_visible;
    bool show_cab_illuminance_markers = k_default_non_station_aux_info_visible;
    bool show_fog_markers = k_default_non_station_aux_info_visible;
    bool show_draw_distance_markers = k_default_non_station_aux_info_visible;
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
            show_map_sound_markers == other.show_map_sound_markers &&
            show_map_sound_3d_markers == other.show_map_sound_3d_markers &&
            show_rolling_noise_markers == other.show_rolling_noise_markers &&
            show_flange_noise_markers == other.show_flange_noise_markers &&
            show_joint_noise_markers == other.show_joint_noise_markers &&
            show_background_markers == other.show_background_markers &&
            show_adhesion_markers == other.show_adhesion_markers &&
            show_cab_illuminance_markers == other.show_cab_illuminance_markers &&
            show_fog_markers == other.show_fog_markers &&
            show_draw_distance_markers == other.show_draw_distance_markers &&
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

struct View3DSettings {
    bool show_scene_owntrack_markers = k_default_non_station_aux_info_visible;
    bool show_scene_current_position_on_plan = k_default_non_station_aux_info_visible;
    bool scene_fog_enabled = k_default_scene_fog_enabled;
    bool scene_map_draw_distance_enabled = k_default_scene_map_draw_distance_enabled;
    int scene_draw_distance_m = k_default_scene_draw_distance_m;
    int scene_edit_component_size_percent = k_default_scene_edit_component_size_percent;
    int scene_camera_speed_percent = k_default_scene_camera_speed_percent;
    bool scene_performance_warning_enabled = k_default_scene_performance_warning_enabled;
    int scene_instance_warning_threshold = k_default_scene_instance_warning_threshold;
    int scene_instance_critical_warning_threshold =
        k_default_scene_instance_critical_warning_threshold;

    bool operator==(const View3DSettings& other) const {
        return show_scene_owntrack_markers == other.show_scene_owntrack_markers &&
            show_scene_current_position_on_plan == other.show_scene_current_position_on_plan &&
            scene_fog_enabled == other.scene_fog_enabled &&
            scene_map_draw_distance_enabled == other.scene_map_draw_distance_enabled &&
            scene_draw_distance_m == other.scene_draw_distance_m &&
            scene_edit_component_size_percent == other.scene_edit_component_size_percent &&
            scene_camera_speed_percent == other.scene_camera_speed_percent &&
            scene_performance_warning_enabled == other.scene_performance_warning_enabled &&
            scene_instance_warning_threshold == other.scene_instance_warning_threshold &&
            scene_instance_critical_warning_threshold ==
                other.scene_instance_critical_warning_threshold;
    }

    bool operator!=(const View3DSettings& other) const {
        return !(*this == other);
    }
};

struct UserSettings {
    Language language = Language::Zh;
    float font_size = k_default_font_size;
    float ui_component_size = k_default_ui_component_size;
    float marker_size_percent = k_default_marker_size_percent;
    CanvasLineWidthSettings canvas_line_widths;
    ImVec4 theme_color = default_theme_color();
    bool edit_mode_enabled = false;
    bool edit_mode_warning_suppressed = false;
    WindowVisibilitySettings window_visibility;
    View2DSettings view_2d;
    View3DSettings view_3d;
    std::filesystem::path path;
};

struct TableFindState {
    char query[256] = {};
    std::string committed;
    std::vector<size_t> matches;
    std::vector<unsigned char> row_matches;
    std::vector<unsigned char> unused_row_matches;
    size_t unused_count = 0;
    size_t unused_total = 0;
    int current = -1;
    int scroll_row = -1;
    bool has_run = false;
    bool exact = false;
    bool panel_expanded = true;
    bool unused_has_run = false;
};

enum class MapElementNumericConstraint {
    None,
    Finite,
    Integer,
    Truncate3,
};

struct MapElementEditFieldState {
    std::string key;
    std::string backend_key;
    std::string target_edit_id;
    std::string expected_source_hash;
    std::string label;
    std::string original_value;
    std::string source_distance_string;
    std::string value;
    MapElementNumericConstraint numeric_constraint = MapElementNumericConstraint::None;
    bool required = true;
    bool read_only = false;
    bool requires_signal_full_form = false;
};

struct MapElementPendingChange {
    std::string change_id;
    std::string edit_id;
    std::string row_kind;
    std::string operation = "update";
    std::map<std::string, std::string> field_changes;
    std::string replacement_statement;
    std::string expected_source_hash;
    std::string distance_resolution_key;
    std::string distance_boundary_token;
    std::string distance_expression;
    bool confirm_environment_mismatch = false;
};

struct MapElementPreviewSnapshot {
    std::string row_kind;
    TableRow row;
    size_t row_index = 0;
};

struct RepeaterInspectorDraft {
    bool begin0_conversion_draft = false;
    std::vector<std::pair<std::string, std::string>> fields;
};

// Each Properties/Edit window owns one session. The current UI presents one
// window, but keeping Repeater drafts here avoids a process-global draft cache
// when independent inspector windows are added later.
struct MapElementInspectorSessionState {
    std::map<std::string, RepeaterInspectorDraft> repeater_drafts;
};

struct MapElementInspectorState {
    bool open = false;
    std::string edit_id;
    std::string row_kind;
    std::string title;
    std::string source_file;
    std::string source_file_name;
    std::string expected_source_hash;
    std::string source_distance_string;
    int line = 0;
    int column = 0;
    std::string raw_statement;
    std::string end_source_file;
    std::string end_source_file_name;
    std::string end_expected_source_hash;
    std::string end_source_distance_string;
    std::string end_raw_statement;
    int end_line = 0;
    int end_column = 0;
    std::string repeater_boundary_kind;
    std::string repeater_previous_begin_edit_id;
    std::string repeater_next_begin_edit_id;
    size_t repeater_scene_row_index = 0;
    bool repeater_has_multiple_begins = false;
    bool source_method_put0 = false;
    bool put0_conversion_draft = false;
    bool put0_prompt_requested = false;
    bool z_rebase_prompt_requested = false;
    bool source_signal_short_form = false;
    bool signal_full_form_conversion_draft = false;
    bool signal_full_form_prompt_requested = false;
    std::string pending_signal_full_form_field;
    std::string pending_signal_full_form_value;
    std::vector<MapElementEditFieldState> fields;
    std::vector<std::string> owned_edit_ids;
    std::vector<std::string> repeater_structure_keys_original;
    MapElementInspectorSessionState session;
};

struct MapElementInspectorRequest {
    std::string edit_id;
    std::string row_kind;
    std::string source_file;
    int line = 0;
    int column = 0;
    std::map<std::string, std::string> field_values;
    std::optional<MapElementInspectorSessionState> inspector_session;

    MapElementInspectorRequest() = default;
    MapElementInspectorRequest(std::string requested_edit_id, std::string requested_row_kind)
        : edit_id(std::move(requested_edit_id)), row_kind(std::move(requested_row_kind)) {}
};

enum class RepeaterDeleteMode {
    EntireChain,
    ChangePoint,
    TrimToChangePoint,
    StartFromChangePoint,
};

struct MapElementDeleteRequest {
    std::string edit_id;
    std::string row_kind;
    RepeaterDeleteMode repeater_mode = RepeaterDeleteMode::EntireChain;
};

struct InspectorTargetMetadata {
    std::string row_kind;
    size_t row_index = 0;
    int elements_for_statement = 0;
    std::string statement_kind;
    std::string source_hash;
    std::string expected_source_hash;
    EditSourceInfo source;
    std::string raw_statement;
    std::string raw_arguments;
    std::string source_distance_string;
    double distance_value = 0.0;
    uint32_t flags = 0;
};

std::optional<InspectorTargetMetadata> resolve_inspector_target_metadata(
    void* handle, const std::string& edit_id,
    const std::string& expected_row_kind,
    std::string* error_message = nullptr);

bool apply_committed_edit_report_to_model(MapModel& model,
                                          const KvEditReportSnapshot& report,
                                          std::string& error_message);

struct DistanceResolutionChoice {
    std::string boundary_token;
    std::string distance_expression;
    bool confirm_environment_mismatch = false;
};

enum class DistanceResolutionPhase {
    None,
    ConfirmAction,
    EditExpression,
    SelectBoundary,
};

struct DistanceResolutionWorkflowState {
    DistanceResolutionPhase phase = DistanceResolutionPhase::None;
    bool popup_requested = false;
    bool retry_requested = false;
    DistanceResolutionRequest request;
    std::map<std::string, MapElementPendingChange> candidate_changes;
    std::optional<MapElementInspectorRequest> reload_request;
    bool applying_delete = false;
    std::string origin_edit_id;
    std::array<char, 1024> expression_buffer{};
};

// A target slot is one physical source line. Its payload can come from another
// slot after a row move; this lets the typed-edit layer replace the target with
// the source row's full raw template while retaining stable target edit IDs.
inline constexpr std::array<const char*, 13> k_station_list_field_names = {
    "stationKey", "stationName", "arrivalTime", "depertureTime", "stoppageTime",
    "defaultTime", "signalFlag", "alightingTime", "passengers", "arrivalSoundKey",
    "depertureSoundKey", "doorReopen", "stuckInDoor"
};

inline constexpr std::array<const char*, 2> k_structure_model_field_names = {
    "structureKey", "filePath"
};

inline constexpr std::array<const char*, 3> k_sound_file_field_names = {
    "soundKey", "filePath", "bufferCount"
};

struct EditableListSpec {
    const char* row_kind = "";
    const char* change_prefix = "";
    const char* const* field_names = nullptr;
    size_t field_count = 0;
    size_t cache_column_offset = 0;
    int path_field = -1;
    bool numbered_structure_key_fields = false;
};

inline constexpr EditableListSpec k_station_definition_edit_spec = {
    "station.list", "station-list-", k_station_list_field_names.data(),
    k_station_list_field_names.size(), 0, -1, false
};
inline constexpr EditableListSpec k_structure_model_edit_spec = {
    "structure.model", "structure-model-", k_structure_model_field_names.data(),
    k_structure_model_field_names.size(), 1, 1, false
};
inline constexpr EditableListSpec k_sound_list_edit_spec = {
    "sound.list", "sound-list-", k_sound_file_field_names.data(),
    k_sound_file_field_names.size(), 1, 1, false
};
inline constexpr EditableListSpec k_sound_3d_list_edit_spec = {
    "sound3D.list", "sound3d-list-", k_sound_file_field_names.data(),
    k_sound_file_field_names.size(), 1, 1, false
};
inline constexpr EditableListSpec k_signal_aspect_edit_spec = {
    "signal.aspect", "signal-aspect-", nullptr, 0, 1, -1, true
};

struct EditableListDraftRow {
    std::string target_edit_id;
    std::string target_source_file;
    std::string target_expected_source_hash;
    int target_line = 0;
    int target_column = 0;
    std::vector<std::string> original_values;
    std::string payload_edit_id;
    std::string payload_source_file;
    int payload_line = 0;
    int payload_column = 0;
    std::string payload_raw_statement;
    std::vector<std::string> values;
    std::string resolved_path;
    bool deleted = false;
    size_t primary_structure_field_count = 0;
    size_t secondary_structure_field_count = 0;
    bool secondary_row_deleted = false;
};

struct EditableListEditState {
    int selected_row = -1;
    int selected_column = -1;
    bool selected_secondary_row = false;
    int editing_column = -1;
    std::string editing_edit_id;
    std::string editing_baseline;
    std::string edit_buffer;
    bool edit_buffer_fresh = false;
    bool rows_initialized = false;
    std::vector<EditableListDraftRow> rows;
    std::vector<size_t> visible_rows;
    std::vector<EditableListDisplayRow> display_rows;
};

using StationDefinitionDraftRow = EditableListDraftRow;
using StationDefinitionEditState = EditableListEditState;

bool editable_list_row_has_draft(const EditableListDraftRow& row);
std::string editable_list_field_name(const EditableListSpec& spec,
                                     size_t field_index);
std::vector<size_t> editable_list_visible_row_indices(
    const std::vector<EditableListDraftRow>& rows);
bool move_editable_list_draft_row(
    std::vector<EditableListDraftRow>& rows,
    const std::vector<size_t>& visible_rows,
    int visible_row, int direction);
bool clear_editable_list_draft_cell(
    std::vector<EditableListDraftRow>& rows,
    const std::vector<size_t>& visible_rows,
    int visible_row, int column);
bool delete_editable_list_draft_row(
    std::vector<EditableListDraftRow>& rows,
    const std::vector<size_t>& visible_rows,
    int visible_row);
void rebuild_editable_list_display_rows(
    EditableListEditState& edit,
    const EditableListSpec& spec);
bool build_editable_list_pending_changes(
    const EditableListSpec& spec,
    const std::vector<EditableListDraftRow>& rows,
    const std::map<std::string, MapElementPendingChange>& existing_changes,
    std::map<std::string, MapElementPendingChange>& candidate_changes,
    std::string& error_message);

bool station_definition_row_has_draft(const StationDefinitionDraftRow& row);
std::vector<size_t> station_definition_visible_row_indices(
    const std::vector<StationDefinitionDraftRow>& rows);
bool move_station_definition_draft_row(
    std::vector<StationDefinitionDraftRow>& rows,
    const std::vector<size_t>& visible_rows,
    int visible_row, int direction);
bool clear_station_definition_draft_cell(
    std::vector<StationDefinitionDraftRow>& rows,
    const std::vector<size_t>& visible_rows,
    int visible_row, int column);
bool delete_station_definition_draft_row(
    std::vector<StationDefinitionDraftRow>& rows,
    const std::vector<size_t>& visible_rows,
    int visible_row);
bool build_station_definition_pending_changes(
    const std::vector<StationDefinitionDraftRow>& rows,
    const std::map<std::string, MapElementPendingChange>& existing_changes,
    std::map<std::string, MapElementPendingChange>& candidate_changes,
    std::string& error_message);

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
    void add_log(std::string text);
    void add_log(LogSeverity severity, std::string text);
    void request_exit();
#ifndef NDEBUG
    static int run_debug_headless_plan_benchmark(const std::string& path, int frames,
                                                 double unit_distance, double pan_pixels,
                                                 double max_frame_ms, const std::string& output_path,
                                                 bool profile_stages);
    static int run_debug_headless_open_benchmark(const HeadlessOpenBenchmarkOptions& options);
    static int run_debug_headless_scene3d_benchmark(const std::string& path, int frames,
                                                    double unit_distance, double max_frame_ms,
                                                    double window_back_m, double window_forward_m,
                                                    int scene_model_workers,
                                                    bool disable_scene_texture_cache,
                                                    const std::string& output_path);
    static int run_debug_headless_scene_camera_transfer(const std::string& path, double unit_distance,
                                                        bool has_camera_distance, double camera_distance,
                                                        const std::string& output_path);
    static int run_debug_headless_source_anchors(const std::string& path, double unit_distance,
                                                 const std::string& output_path);
    static int run_debug_headless_edit_roundtrip(const std::string& path, double unit_distance,
                                                 const std::string& output_path);
    static int run_debug_headless_table_find(const std::string& output_path);
#endif

private:
    ID3D11Device* device_ = nullptr;
    float dpi_scale_ = 1.0f;
    bool viewports_enabled_ = false;
    Translation i18n_;
    UserSettings settings_;
    Language lang_ = Language::Zh;
    float font_size_ = k_default_font_size;
    float pending_font_size_ = k_default_font_size;
    float font_size_before_dialog_ = k_default_font_size;
    float ui_component_size_ = k_default_ui_component_size;
    float pending_ui_component_size_ = k_default_ui_component_size;
    float ui_component_size_before_dialog_ = k_default_ui_component_size;
    float marker_size_percent_ = k_default_marker_size_percent;
    float pending_marker_size_percent_ = k_default_marker_size_percent;
    float marker_size_percent_before_dialog_ = k_default_marker_size_percent;
    CanvasLineWidthSettings canvas_line_widths_;
    CanvasLineWidthSettings pending_canvas_line_widths_;
    CanvasLineWidthSettings canvas_line_widths_before_dialog_;
    int scene_draw_distance_m_ = k_default_scene_draw_distance_m;
    int pending_scene_draw_distance_m_ = k_default_scene_draw_distance_m;
    int scene_draw_distance_before_dialog_m_ = k_default_scene_draw_distance_m;
    int scene_edit_component_size_percent_ = k_default_scene_edit_component_size_percent;
    int pending_scene_edit_component_size_percent_ = k_default_scene_edit_component_size_percent;
    int scene_edit_component_size_before_dialog_percent_ = k_default_scene_edit_component_size_percent;
    bool scene_fog_enabled_ = k_default_scene_fog_enabled;
    bool pending_scene_fog_enabled_ = k_default_scene_fog_enabled;
    bool scene_fog_enabled_before_dialog_ = k_default_scene_fog_enabled;
    bool scene_map_draw_distance_enabled_ = k_default_scene_map_draw_distance_enabled;
    bool pending_scene_map_draw_distance_enabled_ = k_default_scene_map_draw_distance_enabled;
    bool scene_map_draw_distance_enabled_before_dialog_ = k_default_scene_map_draw_distance_enabled;
    int scene_camera_speed_percent_ = k_default_scene_camera_speed_percent;
    int pending_scene_camera_speed_percent_ = k_default_scene_camera_speed_percent;
    int scene_camera_speed_percent_before_dialog_ = k_default_scene_camera_speed_percent;
    bool scene_performance_warning_enabled_ = k_default_scene_performance_warning_enabled;
    bool pending_scene_performance_warning_enabled_ = k_default_scene_performance_warning_enabled;
    bool scene_performance_warning_enabled_before_dialog_ =
        k_default_scene_performance_warning_enabled;
    int scene_instance_warning_threshold_ = k_default_scene_instance_warning_threshold;
    int pending_scene_instance_warning_threshold_ = k_default_scene_instance_warning_threshold;
    int scene_instance_warning_threshold_before_dialog_ =
        k_default_scene_instance_warning_threshold;
    int scene_instance_critical_warning_threshold_ =
        k_default_scene_instance_critical_warning_threshold;
    int pending_scene_instance_critical_warning_threshold_ =
        k_default_scene_instance_critical_warning_threshold;
    int scene_instance_critical_warning_threshold_before_dialog_ =
        k_default_scene_instance_critical_warning_threshold;
    ImVec4 theme_color_ = default_theme_color();
    ImVec4 pending_theme_color_ = default_theme_color();
    ImVec4 theme_color_before_dialog_ = default_theme_color();
    WindowVisibilitySettings last_saved_window_visibility_;
    View2DSettings last_saved_view_2d_settings_;
    View3DSettings last_saved_view_3d_settings_;
    std::filesystem::path history_path_;
    std::vector<RecentMapEntry> recent_maps_;

    void* handle_ = nullptr;
    MapModel model_;
    bool has_model_ = false;
    std::string file_path_;
    bool edit_mode_enabled_ = false;
    bool edit_mode_warning_dont_show_ = false;
    bool edit_registry_loaded_ = false;
    std::map<std::string, MapElementPendingChange> pending_edit_changes_;
    bool edit_memory_matches_pending_ledger_ = true;
    std::map<std::string, MapElementPreviewSnapshot> original_edit_rows_;
    std::map<std::string, DistanceResolutionChoice> distance_resolution_choices_;
    DistanceResolutionWorkflowState distance_resolution_workflow_;
    MapElementInspectorState inspector_;
    std::optional<MapElementInspectorRequest> pending_inspector_request_;
    std::optional<MapElementDeleteRequest> pending_delete_request_;
    StationDefinitionEditState station_definition_edit_;
    EditableListEditState structure_model_edit_;
    EditableListEditState signal_aspect_edit_;
    EditableListEditState sound_list_edit_;
    EditableListEditState sound_3d_list_edit_;

    std::vector<LogLine> logs_;
    std::mutex log_mutex_;
    std::atomic<int> error_count_{0};
    std::atomic<int> warn_count_{0};
    const char* program_status_key_ = "status.ready";
    std::string program_status_elapsed_suffix_;

    struct LoadResult {
        bool ok = false;
        bool preserve_settings = false;
        bool record_history = false;
        bool full_edit_registry = false;
        std::string load_profile = "preview";
        bool preserve_scene_preview_models = false;
        bool preserve_scene_preview_camera = false;
        bool edit_metadata_only = false;
        std::optional<BackgroundHistory> background_to_restore;
        void* handle = nullptr;
        MapModel model;
        std::string path;
        std::string error;
        std::chrono::steady_clock::time_point started_at;
        double elapsed_seconds = 0.0;
        double maploader_seconds = 0.0;
        double model_build_seconds = 0.0;
    };
    struct LoadModelOptions {
        bool full_edit_registry = false;
        std::string load_profile = "preview";
    };
    struct AsyncLoadState {
        std::thread worker;
        std::atomic<bool> running{false};
        std::mutex result_mutex;
        std::optional<LoadResult> pending_result;
        std::optional<std::chrono::steady_clock::time_point> pending_started_at;
    };
    AsyncLoadState load_state_;

    double dmin_ = 0.0;
    double dmax_ = 0.0;
    double plot_min_ = 0.0;
    double plot_max_ = 0.0;
    double cp_start_ = 0.0;
    double cp_end_ = 0.0;
    double cp_interval_ = 25.0;
    double unit_distance_ = 25.0;

    bool show_stations_ = k_default_station_aux_info_visible;
    bool show_station_names_ = k_default_station_aux_info_visible;
    bool show_station_mileage_ = k_default_station_aux_info_visible;
    bool show_gradient_pos_ = true;
    bool show_gradient_values_ = true;
    bool show_curve_values_ = k_default_non_station_aux_info_visible;
    bool show_profile_other_ = false;
    bool show_speedlimits_ = k_default_non_station_aux_info_visible;
    bool show_irregularity_markers_ = k_default_non_station_aux_info_visible;
    bool show_beacon_markers_ = k_default_non_station_aux_info_visible;
    bool show_pretrain_markers_ = k_default_non_station_aux_info_visible;
    bool show_map_sound_markers_ = k_default_non_station_aux_info_visible;
    bool show_map_sound_3d_markers_ = k_default_non_station_aux_info_visible;
    bool show_rolling_noise_markers_ = k_default_non_station_aux_info_visible;
    bool show_flange_noise_markers_ = k_default_non_station_aux_info_visible;
    bool show_joint_noise_markers_ = k_default_non_station_aux_info_visible;
    bool show_background_markers_ = k_default_non_station_aux_info_visible;
    bool show_adhesion_markers_ = k_default_non_station_aux_info_visible;
    bool show_cab_illuminance_markers_ = k_default_non_station_aux_info_visible;
    bool show_fog_markers_ = k_default_non_station_aux_info_visible;
    bool show_draw_distance_markers_ = k_default_non_station_aux_info_visible;
    bool show_scene_owntrack_markers_ = k_default_non_station_aux_info_visible;
    bool show_scene_current_position_on_plan_ = k_default_non_station_aux_info_visible;
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
    bool profile_y_zoom_pending_ = false;
    double profile_y_zoom_min_ = 0.0;
    double profile_y_zoom_max_ = 0.0;
    bool radius_x_zoom_pending_ = false;
    double radius_x_zoom_min_ = 0.0;
    double radius_x_zoom_max_ = 0.0;
    bool profile_plot_rect_valid_ = false;
    ImVec2 profile_plot_pos_ = ImVec2(0.0f, 0.0f);
    ImVec2 profile_plot_size_ = ImVec2(0.0f, 0.0f);
    bool reset_profile_axes_next_ = true;
    bool reset_radius_axes_next_ = true;

    bool show_station_list_window_ = false;
    bool show_structures_window_ = false;
    bool show_structures_between_window_ = false;
    bool show_structure_models_window_ = false;
    bool show_other_trains_window_ = false;
    bool show_sound_list_window_ = false;
    bool show_sound_3d_list_window_ = false;
    bool show_repeaters_window_ = false;
    bool show_signal_aspects_window_ = false;
    bool show_signals_window_ = false;
    bool show_beacons_window_ = false;
    bool show_irregularities_window_ = false;
    bool show_map_sounds_window_ = false;
    bool show_map_sound_3d_window_ = false;
    bool show_rolling_noises_window_ = false;
    bool show_flange_noises_window_ = false;
    bool show_joint_noises_window_ = false;
    bool show_backgrounds_window_ = false;
    bool show_adhesions_window_ = false;
    bool show_cab_illuminance_window_ = false;
    bool show_fogs_window_ = false;
    bool show_draw_distances_window_ = false;
    bool show_file_structure_window_ = false;
    bool show_console_window_ = true;
    bool show_plots_window_ = true;
    bool show_model_preview_window_ = true;
    bool show_scene_preview_window_ = true;
    bool focus_structures_next_ = false;
    bool focus_structures_between_next_ = false;
    bool focus_other_trains_next_ = false;
    bool focus_repeaters_next_ = false;
    bool focus_signal_aspects_next_ = false;
    bool focus_signals_next_ = false;
    bool focus_beacons_next_ = false;
    bool focus_irregularities_next_ = false;
    bool focus_map_sounds_next_ = false;
    bool focus_map_sound_3d_next_ = false;
    bool focus_rolling_noises_next_ = false;
    bool focus_flange_noises_next_ = false;
    bool focus_joint_noises_next_ = false;
    bool focus_backgrounds_next_ = false;
    bool focus_adhesions_next_ = false;
    bool focus_cab_illuminance_next_ = false;
    bool focus_fogs_next_ = false;
    bool focus_draw_distances_next_ = false;
    bool focus_file_structure_next_ = false;
    bool focus_model_preview_next_ = false;
    bool focus_scene_preview_next_ = false;
    bool focus_plots_next_ = true;
    struct PopupState {
        bool range = false;
        bool control_points = false;
        bool background_adjust = false;
        bool background_align = false;
        bool about = false;
        bool ui_settings = false;
        bool canvas_element_sizes = false;
        bool canvas_3d_settings = false;
        bool reload_unsaved_confirm = false;
        bool close_unsaved_confirm = false;
        bool revert_all_edits_confirm = false;
        bool edit_mode_warning = false;
    };
    enum class PendingReloadAction { None, MapAndModelPreview, GeometryOnly };
    enum class PendingCloseAction { None, DisableEditMode, ExitApplication };
    PopupState popups_;
    PendingReloadAction pending_reload_action_ = PendingReloadAction::None;
    PendingCloseAction pending_close_action_ = PendingCloseAction::None;
    bool has_saved_layout_ = false;
    bool initial_dockspace_done_ = false;
    ImGuiID dock_right_id_ = 0;
    ImGuiID dock_main_id_ = 0;
    TableUiCache table_cache_;
    FileStructureDiagramLayoutCache file_structure_layout_cache_;
    TextPreviewState text_preview_;
    TableFindState structure_model_find_;
    TableFindState signal_aspect_find_;
    TableFindState sound_file_find_;
    TableFindState sound_3d_file_find_;
    std::vector<std::optional<PlanStructureMarker>> structure_marker_cache_;
    std::vector<RepeaterOverlayRow> repeater_marker_cache_;
    std::vector<std::optional<PlanSignalMarker>> signal_marker_cache_;
    std::vector<std::optional<PlanBeaconMarker>> beacon_marker_cache_;
    std::vector<std::optional<PlanPreTrainMarker>> pretrain_marker_cache_;
    std::vector<std::optional<PlanOtherTrainStopMarker>> other_train_stop_marker_cache_;
    std::vector<OtherTrainPathOverlay> other_train_path_cache_;
    std::vector<unsigned char> other_train_path_visible_;
    std::vector<std::optional<PlanIrregularityMarker>> irregularity_marker_cache_;
    std::vector<std::optional<PlanMapSoundMarker>> map_sound_marker_cache_;
    std::vector<std::optional<PlanMapSound3DMarker>> map_sound_3d_marker_cache_;
    std::vector<std::optional<PlanRollingNoiseMarker>> rolling_noise_marker_cache_;
    std::vector<std::optional<PlanFlangeNoiseMarker>> flange_noise_marker_cache_;
    std::vector<std::optional<PlanJointNoiseMarker>> joint_noise_marker_cache_;
    std::vector<std::optional<PlanBackgroundMarker>> background_marker_cache_;
    std::vector<std::optional<PlanAdhesionMarker>> adhesion_marker_cache_;
    std::vector<std::optional<PlanCabIlluminanceMarker>> cab_illuminance_marker_cache_;
    std::vector<std::optional<PlanFogMarker>> fog_marker_cache_;
    std::vector<std::optional<PlanDrawDistanceMarker>> draw_distance_marker_cache_;
    std::vector<unsigned char> structure_row_visible_;
    std::vector<unsigned char> repeater_row_visible_;
    std::vector<unsigned char> signal_row_visible_;
    struct PlanDataCache {
        bool valid = false;
        std::uint64_t source_revision = 0;
        bool has_model = false;
        double distance_min = 0.0;
        double distance_max = 0.0;
        Mode mode = Mode::Pan;
        bool fitted = false;
        double scale = 1.0;
        bool show_curve_values = false;
        std::uint32_t marker_visibility_mask = 0;
        std::vector<unsigned char> structure_row_visible;
        std::vector<unsigned char> repeater_row_visible;
        std::vector<unsigned char> signal_row_visible;
        std::vector<unsigned char> other_train_path_visible;
        PlanData data;
#ifndef NDEBUG
        std::uint64_t rebuild_count = 0;
#endif
    };
    PlanDataCache plan_data_cache_;
    std::uint64_t plan_data_source_revision_ = 0;
    int structure_list_scroll_row_ = -1;
    int structure_list_highlight_row_ = -1;
    int repeater_list_scroll_row_ = -1;
    int repeater_list_highlight_row_ = -1;
    int signal_list_scroll_row_ = -1;
    int signal_list_highlight_row_ = -1;
    int other_train_stop_list_scroll_row_ = -1;
    int other_train_stop_list_highlight_row_ = -1;
    int beacon_list_scroll_row_ = -1;
    int beacon_list_highlight_row_ = -1;
    int irregularity_list_scroll_row_ = -1;
    int irregularity_list_highlight_row_ = -1;
    int map_sound_list_scroll_row_ = -1;
    int map_sound_list_highlight_row_ = -1;
    int map_sound_3d_list_scroll_row_ = -1;
    int map_sound_3d_list_highlight_row_ = -1;
    int rolling_noise_list_scroll_row_ = -1;
    int rolling_noise_list_highlight_row_ = -1;
    int flange_noise_list_scroll_row_ = -1;
    int flange_noise_list_highlight_row_ = -1;
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
    int draw_distance_list_scroll_row_ = -1;
    int draw_distance_list_highlight_row_ = -1;
    int plan_structure_popup_row_ = -1;
    int plan_repeater_popup_row_ = -1;
    int plan_signal_popup_row_ = -1;
    int plan_beacon_popup_row_ = -1;
    int plan_other_train_stop_popup_row_ = -1;
    int plan_irregularity_popup_row_ = -1;
    int plan_map_sound_popup_row_ = -1;
    int plan_map_sound_3d_popup_row_ = -1;
    int plan_rolling_noise_popup_row_ = -1;
    int plan_flange_noise_popup_row_ = -1;
    int plan_joint_noise_popup_row_ = -1;
    int plan_background_popup_row_ = -1;
    int plan_adhesion_popup_row_ = -1;
    int plan_cab_illuminance_popup_row_ = -1;
    int plan_fog_popup_row_ = -1;
    int plan_draw_distance_popup_row_ = -1;
    PlanMarkerSelection plan_marker_selection_;
    std::optional<ImVec2> plan_focus_arrow_;
    double plan_focus_arrow_until_ = 0.0;
    std::unique_ptr<Canvas3D> model_preview_canvas_;
    std::unique_ptr<Canvas3D> scene_preview_canvas_;
    ImVec4 model_preview_bg_color_ = ImVec4(0.0f, 0.0f, 0.0f, 1.0f);
    bool scene_preview_started_ = false;
    bool scene_preview_dirty_ = true;
    bool scene_preview_preserve_models_on_rebuild_ = false;
    bool scene_preview_preserve_camera_on_rebuild_ = false;
    std::optional<std::chrono::steady_clock::time_point> pending_scene_preview_started_at_;

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
    char distance_jump_input_[32] = {};
    int align_station1_ = 0;
    int align_station2_ = 1;
    std::optional<ImVec2> align_pick1_;
    std::optional<ImVec2> align_pick2_;
    int pick_slot_ = 0;

    const std::string& tr(std::string_view key) const { return i18n_.get(lang_, key); }

    static void log_callback(const char* message);

    void stop_loader();
    void handle_loader_start_failure(const std::string& error);
    void poll_loader();
    void set_program_status(const char* key, std::string_view elapsed_seconds = {});
    void finish_pending_load_timing(std::chrono::steady_clock::time_point finished_at);
    void finish_pending_load_timing_after_plan_data_ready();
    void begin_load(std::string path, bool preserve_settings, bool record_history = false,
                    std::optional<BackgroundHistory> background_to_restore = std::nullopt,
                    bool preserve_scene_preview_models = false,
                    bool preserve_scene_preview_camera = false);
    void apply_load_result(LoadResult result);
    void begin_edit_metadata_load();
    void apply_edit_metadata_result(LoadResult result);
    void regenerate_geometry();
    static LoadResult load_map_worker(std::string path, double unit_distance, bool has_cp, double cp_start, double cp_end, double cp_step);
    static LoadResult load_map_worker(std::string path, double unit_distance, bool has_cp, double cp_start, double cp_end, double cp_step,
                                      LoadModelOptions options);
    static MapModel build_model_from_handle(void* handle, const std::string& path);
    static MapModel build_model_from_handle(void* handle, const std::string& path,
                                            LoadModelOptions options);
    void clear_pending_edit_state();
    bool has_pending_edits() const;
    bool parse_and_log_edit_report(const KvEditReportSnapshot& report,
                                   const std::string& success_prefix,
                                   int* update_count = nullptr,
                                   int* delete_count = nullptr,
                                   int* changed_file_count = nullptr,
                                   std::vector<DistanceResolutionRequest>* resolution_requests = nullptr);
    bool sync_edit_memory_with_ledger(
        const std::map<std::string, MapElementPendingChange>& changes,
        std::vector<DistanceResolutionRequest>* resolution_requests = nullptr);
    bool apply_edit_ledger_to_preview(const std::map<std::string, MapElementPendingChange>& changes,
                                      std::optional<MapElementInspectorRequest> reload_request,
                                      bool applying_delete,
                                      std::string resolution_origin_edit_id = {});
    void begin_distance_resolution_workflow(
        const std::map<std::string, MapElementPendingChange>& changes,
        std::optional<MapElementInspectorRequest> reload_request,
        bool applying_delete,
        std::string origin_edit_id,
        const std::vector<DistanceResolutionRequest>& requests);
    void apply_distance_resolution_choice(const DistanceResolutionChoice& choice);
    void select_distance_resolution_boundary(const std::string& token);
    void confirm_distance_resolution_boundary();
    void cancel_distance_resolution_workflow();
    void process_distance_resolution_retry();
    bool apply_local_preview_change(const MapElementPendingChange& change,
                                    bool refresh_preview = true);
    bool restore_local_preview_change(const std::string& edit_id, const std::string& row_kind,
                                      bool refresh_preview = true);
    bool snapshot_local_preview_row(const std::string& edit_id, const std::string& row_kind);
    void refresh_local_preview_after_edit(const std::string& row_kind,
                                          const std::string& edit_id = {});
    void request_element_inspector(const std::string& edit_id, const std::string& row_kind);
    void process_pending_element_inspector();
    void request_element_delete(const std::string& edit_id, const std::string& row_kind,
                                RepeaterDeleteMode repeater_mode = RepeaterDeleteMode::EntireChain);
    void process_pending_element_delete();
    bool delete_element_target(const MapElementDeleteRequest& request);
    bool open_element_inspector(const MapElementInspectorRequest& request);
    bool open_element_inspector(const std::string& edit_id, const std::string& row_kind);
    bool row_has_pending_edit(const std::string& edit_id) const;
    bool row_is_pending_delete(const std::string& edit_id) const;
    bool edit_actions_available() const;
    void set_edit_mode_enabled(bool enabled);
    void apply_edit_mode_enabled(bool enabled);
    void request_close_action(PendingCloseAction action);
    bool resolve_pending_close_action(bool save_changes);
    bool discard_pending_edits();
    bool revert_all_pending_edits();
    void apply_inspector_changes();
    bool has_unsaved_edit_state() const;
    bool has_station_definition_drafts() const;
    bool has_unapplied_editable_list_drafts() const;
    bool has_editable_list_drafts(const EditableListEditState& edit,
                                  const EditableListSpec& spec) const;
    bool initialize_editable_list_draft_rows(EditableListEditState& edit,
                                             const EditableListSpec& spec);
    void commit_editable_list_active_edit(EditableListEditState& edit,
                                          const EditableListSpec& spec);
    void discard_all_editable_list_drafts();
    bool move_editable_list_row(EditableListEditState& edit,
                                const EditableListSpec& spec,
                                int visible_row, int direction);
    bool clear_editable_list_cell(EditableListEditState& edit,
                                  const EditableListSpec& spec,
                                  int visible_row, int column);
    bool choose_editable_list_file(EditableListEditState& edit,
                                   const EditableListSpec& spec,
                                   int visible_row);
    bool delete_editable_list_row(EditableListEditState& edit,
                                  const EditableListSpec& spec,
                                  int visible_row);
    bool delete_editable_list_secondary_row(
        EditableListEditState& edit,
        const EditableListSpec& spec,
        int visible_row);
    void apply_editable_list_drafts(EditableListEditState& edit,
                                    const EditableListSpec& spec);
    void rebind_other_editable_list_drafts(const EditableListEditState* applied);
    void reset_editable_list_find_results(const EditableListSpec& spec);
    bool initialize_station_definition_draft_rows();
    void commit_station_definition_active_edit();
    void discard_station_definition_drafts();
    bool move_station_definition_row(int visible_row, int direction);
    bool clear_station_definition_cell(int visible_row, int column);
    bool delete_station_definition_row(int visible_row);
    void apply_station_definition_drafts();
    void enable_inspector_put0_conversion();
    bool navigate_repeater_inspector(bool toward_next);
    void sync_scene_placement_edit_from_inspector();
    void apply_scene_placement_drag_update(const Canvas3DPlacementDragUpdate& update);
    bool update_scene_placement_instance_from_model(const std::string& edit_id,
                                                    const std::string& row_kind);
    bool update_scene_repeater_segment_from_model(const std::string& edit_id);
    void clear_scene_placement_edit_target();
    bool save_pending_edits(bool refresh_inspector = true);
    void render_element_inspector();

    void handle_shortcuts();
    void render_menu();
    void render_toolbar();
    void render_status_bar();
    void render_mode_grid_controls();
    void render_station_jump_combo();
    void render_distance_jump_control();
    void render_console();
    void render_plots();
    void render_plan_canvas(ImVec2 size);
    void render_profile_plot(const ProfileData& data, ImVec2 size);
    void render_radius_plot(const ProfileData& data, ImVec2 size);
    void render_othertracks_window();
    void render_station_list_window();
    void render_editable_list_table(const char* table_id,
                                    const TableColumnDef* columns,
                                    int column_count,
                                    const std::vector<CachedTableRow>& cached_rows,
                                    EditableListEditState& edit,
                                    const EditableListSpec& spec,
                                    float path_column_width = 0.0f,
                                    float last_column_width = 0.0f,
                                    TableFindState* find_state = nullptr,
                                    const std::vector<std::string>*
                                        cached_column_headers = nullptr,
                                    const std::vector<float>*
                                        cached_column_widths = nullptr,
                                    const std::vector<EditableListDisplayRow>*
                                        cached_display_rows = nullptr);
    void render_structure_rows_window(bool put_between);
    void render_structures_window();
    void render_structures_between_window();
    void render_structure_models_window();
    void render_other_trains_window();
    void render_sound_file_find_panel(bool is_3d);
    void render_sound_list_window();
    void render_sound_3d_list_window();
    void render_repeaters_window();
    void render_signal_aspects_window();
    void render_signals_window();
    void render_beacons_window();
    void render_irregularities_window();
    void render_map_sounds_window();
    void render_map_sound_3d_window();
    void render_rolling_noises_window();
    void render_flange_noises_window();
    void render_joint_noises_window();
    void render_backgrounds_window();
    void render_adhesions_window();
    void render_cab_illuminance_window();
    void render_fogs_window();
    void render_draw_distances_window();
    void render_file_structure_window();
    void render_source_file_context_menu(const char* popup_id,
                                         const std::string& file_path);
    static bool is_supported_text_preview_file(const std::string& file_path);
    void open_text_preview(const std::string& file_path,
                           bool parser_confirmed_source = false);
    bool load_text_preview_content(TextPreviewState& preview);
    void refresh_text_preview_from_working_copy();
    void open_text_preview_for_distance_resolution(const DistanceResolutionRequest& request);
    void refresh_text_preview_after_map_load();
    void render_text_preview_window();
    void render_model_preview_window();
    void render_scene_preview_window();
    void preview_structure_model(const std::string& path);
    void reload_model_preview();
    void start_scene_preview();
    void stop_scene_preview();
    double rebuild_scene_preview(bool preserve_loaded_models = false, bool preserve_camera = false);
    void finish_pending_scene_preview_load_timing();
    void reload_scene_preview_models();
    void sync_scene_preview_track_visibility();
    void sync_scene_preview_marker_visibility();
    void perform_reload_current_map_and_model_preview();
    void perform_reload_current_map_geometry();
    bool confirm_reload_if_unsaved(PendingReloadAction action);
    void execute_pending_reload_action();
    void reload_current_map_and_model_preview();
    void reload_current_map_geometry();
    void render_popups();
    void setup_initial_dockspace(ImGuiID dockspace_id);
    WindowVisibilitySettings current_window_visibility() const;
    void apply_window_visibility_settings(const WindowVisibilitySettings& visibility);
    View2DSettings current_view_2d_settings() const;
    void apply_view_2d_settings(const View2DSettings& settings);
    View3DSettings current_view_3d_settings() const;
    void apply_view_3d_settings(const View3DSettings& settings);
    void apply_scene_draw_distance_to_canvas(int distance_m);
    void apply_scene_edit_component_size_to_canvas(int size_percent);
    void apply_scene_fog_effect_to_canvas(bool enabled);
    void apply_scene_map_draw_distance_to_canvas(bool enabled);
    void apply_scene_camera_speed_to_canvas(int percent);
    void apply_scene_performance_warning_to_canvas(bool enabled,
                                                   int warning_threshold,
                                                   int critical_warning_threshold);
    bool scene_settings_preview_differs_from_dialog_baseline() const;
    void restore_scene_settings_preview();
    void save_runtime_settings_if_changed();
    void invalidate_table_cache();
    void ensure_table_cache();
    void reset_structure_model_find_results();
    void run_structure_model_find();
    void run_unused_structure_model_search();
    void find_structure_model_for_structure_key(const std::string& structure_key);
    void step_structure_model_find(int delta);
    std::string structure_model_find_status_text() const;
    void reset_signal_aspect_find_results();
    void run_signal_aspect_find();
    void run_unused_signal_aspect_search();
    void find_signal_aspect_for_signal_aspect_key(const std::string& signal_aspect_key);
    void step_signal_aspect_find(int delta);
    std::string signal_aspect_find_status_text() const;
    void reset_sound_file_find_results(bool is_3d);
    void run_sound_file_find(bool is_3d);
    void run_unused_sound_file_search(bool is_3d);
    void find_sound_file_for_sound_key(const std::string& sound_key, bool is_3d);
    void step_sound_file_find(bool is_3d, int delta);
    std::string sound_file_find_status_text(bool is_3d) const;
    void rebuild_marker_overlay_cache();
    void reset_marker_visibility();
    void sync_marker_visibility_sizes();
    void locate_structure_row_on_plan(size_t row_index);
    void locate_structure_row_in_list(size_t row_index);
    void locate_structure_row_in_scene_preview(size_t row_index);
    void locate_repeater_row_on_plan(size_t row_index);
    void locate_repeater_row_in_list(size_t row_index);
    void locate_repeater_row_in_scene_preview(size_t row_index);
    void locate_signal_row_on_plan(size_t row_index);
    void locate_signal_row_in_list(size_t row_index);
    void locate_signal_row_in_scene_preview(size_t row_index);
    void locate_standard_marker_on_plan(
        const std::vector<std::optional<PlanMarker>>& cache,
        size_t row_index, bool& markers_visible);
    void locate_standard_marker_in_list(
        const std::vector<std::optional<PlanMarker>>& cache,
        size_t row_index, bool& window_visible, bool& focus_window,
        int& scroll_row, int& highlight_row);
    void locate_beacon_row_on_plan(size_t row_index);
    void locate_beacon_row_in_list(size_t row_index);
    void locate_other_train_stop_row_on_plan(size_t row_index);
    void locate_other_train_stop_row_in_list(size_t row_index);
    void locate_irregularity_row_on_plan(size_t row_index);
    void locate_irregularity_row_in_list(size_t row_index);
    void locate_map_sound_row_on_plan(size_t row_index);
    void locate_map_sound_row_in_list(size_t row_index);
    void locate_map_sound_3d_row_on_plan(size_t row_index);
    void locate_map_sound_3d_row_in_list(size_t row_index);
    void locate_rolling_noise_row_on_plan(size_t row_index);
    void locate_rolling_noise_row_in_list(size_t row_index);
    void locate_flange_noise_row_on_plan(size_t row_index);
    void locate_flange_noise_row_in_list(size_t row_index);
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
    void locate_draw_distance_row_on_plan(size_t row_index);
    void locate_draw_distance_row_in_list(size_t row_index);
    void locate_scene_marker_row_in_list(Canvas3DSceneMarkerListKind list_kind,
                                         size_t row_index);
    void locate_scene_marker_row_in_scene_preview(Canvas3DSceneMarkerListKind list_kind,
                                                   size_t row_index);
    bool can_locate_scene_preview_row() const;

    double current_plan_origin_angle() const;
    PlanData build_plan_data(bool include_other_tracks = true) const;
    const PlanData& current_plan_data();
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
    void jump_to_distance(double distance);
    void export_csv_to_directory(const std::filesystem::path& dir) const;
    void export_csv();
    void save_history();
    void upsert_recent_map(const std::string& path,
                           const std::optional<BackgroundHistory>& background);
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
    std::string open_editable_list_file_dialog(
        const EditableListSpec& spec,
        const std::string& initial_directory);
    std::string choose_folder_dialog();
};
