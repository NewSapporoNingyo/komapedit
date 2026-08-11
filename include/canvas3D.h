/*
 * Copyright (c) 2026 Sapporo_ningyo
 *
 * Licensed under Apache License 2.0; see LICENSE and NOTICE.
 */

#pragma once

#include "imgui.h"
#include "map_marker_visuals.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

struct ID3D11Device;
struct MapModel;

struct Canvas3DTrackPoint {
    double distance = 0.0;
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    double theta = 0.0;
    double gradient = 0.0;
    double cant_angle = 0.0;
};

struct Canvas3DTrackPath {
    std::string key;
    std::vector<Canvas3DTrackPoint> points;
    ImVec4 color = ImVec4(0.7f, 0.7f, 0.7f, 1.0f);
    bool visible = true;
};

struct Canvas3DTrackVisibility {
    std::string key;
    bool visible = true;
};

enum class Canvas3DSceneInteractionMode {
    Move,
    Select,
    MileageSelect,
};

enum class Canvas3DSceneObjectKind {
    Generic,
    Structure,
    Repeater,
    Signal,
};

// GUI-only navigation identity for scene markers that have a matching table.
// This is deliberately separate from MapMarkerVisualKind: several visible
// marker kinds have no table and must not expose list navigation.
enum class Canvas3DSceneMarkerListKind : std::uint8_t {
    None,
    Section,
    Beacon,
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
    DrawDistance,
    SpeedLimit,
    Count,
};

struct Canvas3DSceneModelOption {
    int structure_key_index = 0;
    std::string structure_key;
    std::string model_path;
};

struct Canvas3DSceneObject {
    Canvas3DSceneObjectKind kind = Canvas3DSceneObjectKind::Generic;
    size_t source_row = 0;
    bool structure_put_between = false;
    std::string label;
    std::string edit_id;
    std::vector<Canvas3DSceneModelOption> model_options;
    size_t selected_model_option = 0;
};

struct Canvas3DModelInstance {
    std::string model_path;
    std::string track_key;
    double distance = 0.0;
    int object_index = -1;
    bool follow_track = false;
    bool put_between = false;
    std::string put_between_track_key1;
    std::string put_between_track_key2;
    int put_between_flag = 0;
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    double rx = 0.0;
    double ry = 0.0;
    double rz = 0.0;
    double tilt = 0.0;
    double span = 0.0;
    double world[16] = {
        1.0, 0.0, 0.0, 0.0,
        0.0, 1.0, 0.0, 0.0,
        0.0, 0.0, 1.0, 0.0,
        0.0, 0.0, 0.0, 1.0
    };
};

struct Canvas3DRepeaterSegment {
    std::string edit_id;
    std::string track_key;
    std::vector<std::string> model_paths;
    size_t chain_begin_index = 0;
    size_t chain_begin_count = 1;
    double begin_distance = 0.0;
    double end_distance = 0.0;
    bool has_end_or_change_position = false;
    double interval = 0.0;
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    double rx = 0.0;
    double ry = 0.0;
    double rz = 0.0;
    double tilt = 0.0;
    double span = 0.0;
    int object_index = -1;
};

struct Canvas3DBackgroundChange {
    double distance = 0.0;
    std::string model_path;
};

struct Canvas3DSceneFogKeyframe {
    double distance = 0.0;
    float density = 0.001f;
    ImVec4 color = ImVec4(0.875f, 0.875f, 0.875f, 1.0f);
};

struct Canvas3DSceneDrawDistanceChange {
    double distance = 0.0;
    double value = 0.0;
};

enum class Canvas3DSceneRouteEventKind {
    Value,
    BeginTransition,
    Interpolate,
};

struct Canvas3DSceneRouteValueEvent {
    double distance = 0.0;
    double previous_value = 0.0;
    double value = 0.0;
    Canvas3DSceneRouteEventKind kind = Canvas3DSceneRouteEventKind::Value;
};

struct Canvas3DSceneRouteStation {
    double distance = 0.0;
    std::string name;
};

struct Canvas3DSceneSpeedLimitEvent {
    double distance = 0.0;
    bool has_speed = false;
    double speed = 0.0;
    int order = 0;
};

struct Canvas3DSceneSectionSignalEvent {
    double distance = 0.0;
    int order = 0;
    std::string values;
};

struct Canvas3DSceneRouteInfo {
    std::vector<Canvas3DSceneRouteValueEvent> radius_events;
    std::vector<Canvas3DSceneRouteValueEvent> cant_events;
    std::vector<Canvas3DSceneRouteValueEvent> gradient_events;
    std::vector<Canvas3DSceneSpeedLimitEvent> speed_limit_events;
    std::vector<Canvas3DSceneSectionSignalEvent> section_signal_events;
    std::vector<Canvas3DSceneRouteStation> stations;
};

struct Canvas3DSceneMarker {
    MapMarkerVisualKind kind = MapMarkerVisualKind::Station;
    Canvas3DSceneMarkerListKind list_kind = Canvas3DSceneMarkerListKind::None;
    MapMarkerIconVariant icon_variant = MapMarkerIconVariant::Default;
    Canvas3DTrackPoint track_point;
    std::string label;
    std::string row_kind;
    std::optional<size_t> row_index;
    std::string edit_id;
    bool unpaired_transition = false;
    std::string track_key;
    ImVec4 theme_color{};
    bool has_theme_color = false;
};

// String views remain valid until the scene or its marker data is refreshed.
struct Canvas3DSceneMarkerTarget {
    MapMarkerVisualKind kind = MapMarkerVisualKind::Station;
    Canvas3DSceneMarkerListKind list_kind = Canvas3DSceneMarkerListKind::None;
    size_t marker_index = 0;
    std::string_view row_kind;
    std::optional<size_t> row_index;
    std::string_view edit_id;
};

struct Canvas3DSceneMarkerVisibility {
    std::uint64_t marker_mask = 0;
    std::uint64_t label_mask = 0;

    bool operator==(const Canvas3DSceneMarkerVisibility& other) const {
        return marker_mask == other.marker_mask && label_mask == other.label_mask;
    }

    bool operator!=(const Canvas3DSceneMarkerVisibility& other) const {
        return !(*this == other);
    }

    bool marker_visible(MapMarkerVisualKind kind) const {
        return (marker_mask & map_marker_visual_bit(kind)) != 0;
    }

    bool label_visible(MapMarkerVisualKind kind) const {
        return (label_mask & map_marker_visual_bit(kind)) != 0;
    }
};

struct Canvas3DCameraStart {
    double distance = 0.0;
    double x = 0.0;
    double y = 2.0;
    double z = 0.0;
    double yaw = 0.0;
    double pitch = 0.0;
};

struct Canvas3DScene {
    std::vector<Canvas3DTrackPath> tracks;
    std::vector<Canvas3DSceneObject> objects;
    std::vector<Canvas3DModelInstance> instances;
    std::vector<Canvas3DRepeaterSegment> repeaters;
    std::vector<Canvas3DBackgroundChange> backgrounds;
    std::vector<Canvas3DSceneFogKeyframe> fog_keyframes;
    std::vector<Canvas3DSceneDrawDistanceChange> draw_distance_changes;
    Canvas3DSceneRouteInfo route_info;
    std::vector<Canvas3DSceneMarker> markers;
    Canvas3DCameraStart camera;
    double min_distance = 0.0;
    double max_distance = 0.0;
};

struct Canvas3DSceneStats {
    bool active = false;
    bool loading = false;
    size_t chunk_count = 0;
    size_t model_path_count = 0;
    size_t model_ready_count = 0;
    size_t model_failed_count = 0;
    size_t instance_count = 0;
    size_t drawn_instance_count = 0;
    size_t drawn_track_chunk_count = 0;
    size_t model_worker_count = 0;
    size_t texture_cache_hit_count = 0;
    size_t texture_cache_miss_count = 0;
    double track_gpu_setup_seconds = 0.0;
    double model_queue_seconds = 0.0;
    double model_load_seconds = 0.0;
    double camera_distance = 0.0;
    double window_back_m = 100.0;
    double window_forward_m = 1200.0;
};

struct Canvas3DSceneCameraPose {
    bool valid = false;
    double distance = 0.0;
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    double theta = 0.0;
    double pitch = 0.0;
};

struct Canvas3DSceneBuildOptions {
    const MapModel* model = nullptr;
    void* map_handle = nullptr;
    double unit_distance = 25.0;
    double control_point_interval = 25.0;
    int station_index = 0;
    bool show_own_track_markers = true;
};

struct Canvas3DSceneBuildResult {
    Canvas3DScene scene;
    std::vector<std::string> log_messages;
};

struct Canvas3DSceneMapRefreshOptions {
    bool route_stations = false;
    bool markers = false;
    bool fog = false;
    bool draw_distances = false;
    bool speed_limits = false;
    bool section_signals = false;
};

struct Canvas3DSceneUiText {
    const char* mileage = "Mileage";
    const char* unit_m = "m";
    const char* add_map_element_at_mileage = "Add Map Element at Current Mileage";
    const char* switch_signal_aspect = "Switch Signal Aspect";
    const char* element_properties = "Properties/Edit";
    const char* delete_element = "Delete";
    const char* unpaired_transition = "This BeginTransition has no corresponding Begin/End.";
    const char* delete_repeater_all = "Delete All";
    const char* delete_repeater_change_point = "Delete Change Point";
    const char* trim_repeater_to_change_point = "Trim to Change Point";
    const char* start_repeater_from_change_point = "Start from Change Point";
    const char* locate_structure_list = "Locate in Map Structure List";
    const char* locate_structure_put_between_list = "Locate in Map Structure List (PutBetween)";
    const char* locate_repeater_list = "Locate in Repeater List";
    const char* locate_signal_list = "Locate in Map Signal List";
    std::array<const char*, static_cast<size_t>(Canvas3DSceneMarkerListKind::Count)>
        locate_marker_list_labels{};
    const char* jump_to_repeater_start_position = "Jump to Start Position";
    const char* jump_to_repeater_end_or_change_position = "Jump to End/Change Position";
    const char* loading = "Loading...";
    const char* straight = "Straight";
    const char* interpolate_unsupported = "interpolate(unsupported)";
    const char* next_station = "Next sta. :";
    const char* speed_limit = "Speedlimit:";
    const char* signal = "Signal:";
    const char* no_station_ahead = "No station ahead";
};

#ifndef NDEBUG
struct Canvas3DSceneFogDebugState {
    size_t keyframe_count = 0;
    size_t fog_draw_part_count = 0;
    bool setting_enabled = false;
    bool sampled_enabled = false;
    bool shader_ready = false;
    double camera_distance = 0.0;
    double max_density_distance = 0.0;
    float density = 0.0f;
    float max_density = 0.0f;
    ImVec4 color = ImVec4(0.0f, 0.0f, 0.0f, 1.0f);
};
#endif

struct Canvas3DSceneContextMenuOptions {
    bool element_properties_enabled = false;
    bool new_element_enabled = false;
};

enum class Canvas3DSceneContextActionKind {
    None,
    LocateStructure,
    LocateRepeater,
    LocateSignal,
    LocateMarkerList,
    EditElement,
    DeleteElement,
    DeleteRepeaterAll,
    DeleteRepeaterChangePoint,
    TrimRepeaterToChangePoint,
    StartRepeaterFromChangePoint,
};

struct Canvas3DSceneContextAction {
    Canvas3DSceneContextActionKind kind = Canvas3DSceneContextActionKind::None;
    Canvas3DSceneMarkerListKind marker_list_kind = Canvas3DSceneMarkerListKind::None;
    size_t row_index = 0;
    std::string edit_id;
    std::string row_kind;
};

enum class Canvas3DSceneEditKind {
    Structure,
    StructurePutBetween,
    Signal,
    Repeater,
};

struct Canvas3DPlacementEditTarget {
    Canvas3DSceneEditKind kind = Canvas3DSceneEditKind::Structure;
    std::string edit_id;
    std::string model_path;
    std::string track_key;
    std::string put_between_track_key1;
    std::string put_between_track_key2;
    int put_between_flag = 0;
    double distance = 0.0;
    bool placement_distance_gizmo = false;
    bool has_repeater_end_distance = false;
    double repeater_end_distance = 0.0;
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    double rx = 0.0;
    double ry = 0.0;
    double rz = 0.0;
    double tilt = 0.0;
    double span = 0.0;
};

enum class Canvas3DSceneDragAxis {
    None,
    X,
    Y,
    Z,
};

enum class Canvas3DSceneDragTarget {
    Placement,
    PlacementDistance,
    PutBetweenDistance,
    RepeaterEndDistance,
};

struct Canvas3DPlacementDragUpdate {
    Canvas3DSceneEditKind kind = Canvas3DSceneEditKind::Structure;
    std::string edit_id;
    Canvas3DSceneDragTarget target = Canvas3DSceneDragTarget::Placement;
    Canvas3DSceneDragAxis axis = Canvas3DSceneDragAxis::None;
    double distance = 0.0;
    double repeater_end_distance = 0.0;
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

struct Canvas3DSceneFrameResult {
    Canvas3DSceneContextAction context_action;
    std::optional<Canvas3DPlacementDragUpdate> placement_drag;
    std::optional<Canvas3DSceneMarkerTarget> hovered_marker;
    std::optional<double> hovered_mileage;
    std::optional<double> new_element_mileage;
};

using Canvas3DWakeCallback = void (*)();

Canvas3DSceneBuildResult build_canvas3d_scene_preview(const Canvas3DSceneBuildOptions& options);
std::vector<Canvas3DTrackVisibility> build_canvas3d_scene_track_visibility(
    const MapModel& model,
    bool show_own_track_markers);

class Canvas3D {
public:
    explicit Canvas3D(ID3D11Device* device, Canvas3DWakeCallback wake_callback = nullptr);
    ~Canvas3D();

    Canvas3D(const Canvas3D&) = delete;
    Canvas3D& operator=(const Canvas3D&) = delete;

    bool load_model(const std::string& path, std::string& error);
    bool reload_model(std::string& error);
    std::vector<std::string> drain_model_load_warnings();
    void clear_model();
    bool has_model() const;
    const std::string& model_path() const;
    void set_background_color(ImVec4 color);

    void render(ImVec2 size);
    bool load_scene(Canvas3DScene scene, std::string& error,
                    bool preserve_loaded_models = false,
                    bool preserve_camera = false);
    bool refresh_scene_dynamic_content(const MapModel& model, int station_index, std::string& error);
    void clear_scene();
    bool has_scene() const;
    bool reload_scene_models(std::string& error);
    bool set_scene_track_visibility(const std::vector<Canvas3DTrackVisibility>& visibility, std::string& error);
    bool set_scene_marker_visibility(const Canvas3DSceneMarkerVisibility& visibility,
                                     std::string& error);
    bool refresh_scene_route_stations(const MapModel& model, std::string& error);
    bool refresh_scene_map_content(const MapModel& model,
                                   const Canvas3DSceneMapRefreshOptions& options,
                                   std::string& error);
    void set_scene_window(double back_m, double forward_m);
    void set_scene_edit_component_scale(float scale);
    void set_scene_interaction_mode(Canvas3DSceneInteractionMode mode);
    void set_scene_fog_enabled(bool enabled);
    void set_scene_map_draw_distance_enabled(bool enabled);
    void set_scene_camera_speed_percent(int percent);
    void set_scene_performance_warning(bool enabled,
                                       size_t warning_threshold,
                                       size_t critical_warning_threshold);
    Canvas3DSceneInteractionMode scene_interaction_mode() const;
    Canvas3DSceneStats scene_stats() const;
    void process_scene_loading();
#ifndef NDEBUG
    void set_debug_scene_loading_tuning(size_t worker_limit, bool texture_cache_enabled);
    Canvas3DSceneFogDebugState debug_scene_fog_state() const;
    bool debug_read_scene_render_pixels(std::vector<std::uint8_t>& rgba,
                                        int& width, int& height,
                                        std::string& error);
#endif
    std::vector<std::string> drain_scene_load_messages();
    Canvas3DSceneCameraPose scene_camera_pose() const;
    bool jump_scene_camera_to_distance(double distance);
    bool jump_scene_camera_to_object(Canvas3DSceneObjectKind kind, size_t source_row);
    bool jump_scene_camera_to_marker(Canvas3DSceneMarkerListKind list_kind, size_t row_index);
    bool set_scene_placement_edit_target(const Canvas3DPlacementEditTarget& target,
                                         bool show_gizmo);
    bool update_scene_placement_instance(const Canvas3DPlacementEditTarget& target);
    bool set_scene_repeater_edit_target(const Canvas3DPlacementEditTarget& target,
                                        bool show_gizmo);
    bool update_scene_repeater_segment(const Canvas3DPlacementEditTarget& target);
    void clear_scene_placement_edit_target();
    Canvas3DSceneFrameResult render_scene_preview(
        ImVec2 size,
        const Canvas3DSceneUiText& ui_text = Canvas3DSceneUiText{},
        const Canvas3DSceneContextMenuOptions& context_menu_options = Canvas3DSceneContextMenuOptions{});

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
