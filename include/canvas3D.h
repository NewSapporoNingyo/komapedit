/*
 * Copyright (c) 2026 Sapporo_ningyo
 *
 * Licensed under Apache License 2.0; see LICENSE and NOTICE.
 */

#pragma once

#include "imgui.h"

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
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
};

enum class Canvas3DSceneObjectKind {
    Generic,
    Structure,
    Repeater,
    Signal,
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
    std::string track_key;
    std::vector<std::string> model_paths;
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
    double control_point_start = 0.0;
    double control_point_end = 0.0;
    double control_point_interval = 25.0;
    int station_index = 0;
    bool show_own_track_markers = true;
};

struct Canvas3DSceneBuildResult {
    Canvas3DScene scene;
    std::vector<std::string> log_messages;
};

struct Canvas3DSceneUiText {
    std::string switch_signal_aspect = "Switch Signal Aspect";
    std::string element_properties = "Properties/Edit";
    std::string locate_structure_list = "Locate in Map Structure List";
    std::string locate_structure_put_between_list = "Locate in Map Structure List (PutBetween)";
    std::string locate_repeater_list = "Locate in Repeater List";
    std::string jump_to_repeater_start_position = "Jump to Start Position";
    std::string jump_to_repeater_end_or_change_position = "Jump to End/Change Position";
    std::string loading = "Loading...";
};

struct Canvas3DSceneContextMenuOptions {
    bool element_properties_enabled = false;
};

enum class Canvas3DSceneContextActionKind {
    None,
    LocateStructure,
    LocateRepeater,
    EditElement,
};

struct Canvas3DSceneContextAction {
    Canvas3DSceneContextActionKind kind = Canvas3DSceneContextActionKind::None;
    size_t row_index = 0;
    std::string edit_id;
    std::string row_kind;
};

struct Canvas3DStructureEditTarget {
    std::string edit_id;
    std::string track_key;
    double distance = 0.0;
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

struct Canvas3DStructureDragUpdate {
    std::string edit_id;
    Canvas3DSceneDragAxis axis = Canvas3DSceneDragAxis::None;
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

struct Canvas3DSceneFrameResult {
    Canvas3DSceneContextAction context_action;
    std::optional<Canvas3DStructureDragUpdate> structure_drag;
};

Canvas3DSceneBuildResult build_canvas3d_scene_preview(const Canvas3DSceneBuildOptions& options);
std::vector<Canvas3DTrackVisibility> build_canvas3d_scene_track_visibility(
    const MapModel& model,
    bool show_own_track_markers);

class Canvas3D {
public:
    explicit Canvas3D(ID3D11Device* device);
    ~Canvas3D();

    Canvas3D(const Canvas3D&) = delete;
    Canvas3D& operator=(const Canvas3D&) = delete;

    bool load_model(const std::string& path, std::string& error);
    bool reload_model(std::string& error);
    void clear_model();
    bool has_model() const;
    const std::string& model_path() const;
    void set_background_color(ImVec4 color);
    ImVec4 background_color() const;

    void render(ImVec2 size);
    bool load_scene(Canvas3DScene scene, std::string& error,
                    bool preserve_loaded_models = false,
                    bool preserve_camera = false);
    bool refresh_scene_dynamic_content(const MapModel& model, int station_index, std::string& error);
    void clear_scene();
    bool has_scene() const;
    bool reload_scene_models(std::string& error);
    bool set_scene_track_visibility(const std::vector<Canvas3DTrackVisibility>& visibility, std::string& error);
    void set_scene_window(double back_m, double forward_m);
    void set_scene_interaction_mode(Canvas3DSceneInteractionMode mode);
    Canvas3DSceneInteractionMode scene_interaction_mode() const;
    Canvas3DSceneStats scene_stats() const;
    std::vector<std::string> drain_scene_load_messages();
    Canvas3DSceneCameraPose scene_camera_pose() const;
    bool jump_scene_camera_to_distance(double distance);
    bool jump_scene_camera_to_object(Canvas3DSceneObjectKind kind, size_t source_row);
    bool set_scene_structure_edit_target(const Canvas3DStructureEditTarget& target,
                                         bool show_gizmo);
    bool update_scene_structure_instance(const Canvas3DStructureEditTarget& target);
    void clear_scene_structure_edit_target();
    Canvas3DSceneFrameResult render_scene_preview(
        ImVec2 size,
        const Canvas3DSceneUiText& ui_text = Canvas3DSceneUiText{},
        const Canvas3DSceneContextMenuOptions& context_menu_options = Canvas3DSceneContextMenuOptions{});

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
