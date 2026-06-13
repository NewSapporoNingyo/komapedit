/*
 * Copyright (c) 2026 Sapporo_ningyo
 *
 * Licensed under Apache License 2.0; see LICENSE and NOTICE.
 */

#pragma once

#include "imgui.h"

#include <memory>
#include <string>
#include <vector>

struct ID3D11Device;

struct Canvas3DTrackPoint {
    double distance = 0.0;
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    double theta = 0.0;
};

struct Canvas3DTrackPath {
    std::string key;
    std::vector<Canvas3DTrackPoint> points;
    ImVec4 color = ImVec4(0.7f, 0.7f, 0.7f, 1.0f);
};

struct Canvas3DModelInstance {
    std::string model_path;
    double distance = 0.0;
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
    double interval = 0.0;
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    double rx = 0.0;
    double ry = 0.0;
    double rz = 0.0;
    double tilt = 0.0;
    double span = 0.0;
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
    bool load_scene(Canvas3DScene scene, std::string& error);
    void clear_scene();
    bool has_scene() const;
    void set_scene_window(double back_m, double forward_m);
    Canvas3DSceneStats scene_stats() const;
    bool jump_scene_camera_to_distance(double distance);
    void render_scene_preview(ImVec2 size);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
