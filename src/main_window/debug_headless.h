/*
 * Copyright (c) 2026 Sapporo_ningyo
 *
 * Licensed under Apache License 2.0; see LICENSE and NOTICE.
 */

#pragma once

#include <string>
#include <vector>

#ifndef NDEBUG
struct HeadlessLoadOptions {
    bool requested = false;
    std::string path;
    std::string output_path;
    int repeat = 1;
    double unit_distance = 25.0;
    std::string load_profile = "preview";
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

struct HeadlessScene3DBenchmarkOptions {
    bool requested = false;
    std::string path;
    std::string output_path;
    int frames = 300;
    double unit_distance = 25.0;
    double max_frame_ms = 16.667;
    double window_back_m = 100.0;
    double window_forward_m = 1200.0;
    int scene_model_workers = 0;
    bool disable_scene_texture_cache = false;
    std::string error;
};

struct HeadlessOpenBenchmarkOptions {
    bool requested = false;
    std::string path;
    std::string output_path;
    int repeat = 1;
    double unit_distance = 25.0;
    std::string error;
};

struct HeadlessSceneCameraTransferOptions {
    bool requested = false;
    std::string path;
    std::string output_path;
    double unit_distance = 25.0;
    bool has_camera_distance = false;
    double camera_distance = 0.0;
    std::string error;
};

struct HeadlessSourceAnchorOptions {
    bool requested = false;
    std::string path;
    std::string output_path;
    double unit_distance = 25.0;
    std::string error;
};

struct HeadlessEditRoundtripOptions {
    bool requested = false;
    std::string path;
    std::string output_path;
    double unit_distance = 25.0;
    std::string error;
};

struct HeadlessDistanceEditBatchOptions {
    bool requested = false;
    std::string path;
    std::string output_path;
    double unit_distance = 25.0;
    bool commit = false;
    std::string error;
};

struct HeadlessRepeaterEditBatchOptions {
    bool requested = false;
    std::string path;
    std::string output_path;
    double unit_distance = 25.0;
    bool commit = false;
    std::string error;
};

struct HeadlessTableFindOptions {
    bool requested = false;
    std::string output_path;
    std::string error;
};

struct HeadlessTouchInputOptions {
    bool requested = false;
    std::string output_path;
    std::string error;
};

std::vector<std::string> command_line_args_utf8();
HeadlessLoadOptions parse_headless_load_options(const std::vector<std::string>& args);
HeadlessPlanBenchmarkOptions parse_headless_plan_benchmark_options(const std::vector<std::string>& args);
HeadlessOpenBenchmarkOptions parse_headless_open_benchmark_options(const std::vector<std::string>& args);
HeadlessScene3DBenchmarkOptions parse_headless_scene3d_benchmark_options(const std::vector<std::string>& args);
HeadlessSceneCameraTransferOptions parse_headless_scene_camera_transfer_options(const std::vector<std::string>& args);
HeadlessSourceAnchorOptions parse_headless_source_anchor_options(const std::vector<std::string>& args);
HeadlessEditRoundtripOptions parse_headless_edit_roundtrip_options(const std::vector<std::string>& args);
HeadlessDistanceEditBatchOptions parse_headless_distance_edit_batch_options(
    const std::vector<std::string>& args);
HeadlessRepeaterEditBatchOptions parse_headless_repeater_edit_batch_options(
    const std::vector<std::string>& args);
HeadlessTableFindOptions parse_headless_table_find_options(const std::vector<std::string>& args);
HeadlessTouchInputOptions parse_headless_touch_input_options(const std::vector<std::string>& args);
int run_headless_load_map(const HeadlessLoadOptions& options);
int run_debug_headless_distance_edit_batch(const HeadlessDistanceEditBatchOptions& options);
int run_debug_headless_repeater_edit_batch(const HeadlessRepeaterEditBatchOptions& options);
int run_debug_headless_touch_input(const HeadlessTouchInputOptions& options);
#endif
