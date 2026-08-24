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

struct HeadlessLoadScenarioOptions {
    bool requested = false;
    std::string path;
    std::string output_path;
    int scenario_index = 0;
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
    std::string interaction = "pan";
    bool profile_stages = false;
    std::string error;
};

struct HeadlessScene3DBenchmarkOptions {
    bool requested = false;
    std::string path;
    std::string output_path;
    int frames = 300;
    double unit_distance = 25.0;
    double max_frame_ms = 23.0;
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

struct HeadlessOwnTrackEditOptions {
    bool requested = false;
    std::string path;
    std::string output_path;
    double unit_distance = 25.0;
    std::string error;
};

struct HeadlessOtherTrackEditOptions {
    bool requested = false;
    std::string path;
    std::string output_path;
    double unit_distance = 25.0;
    bool commit = false;
    std::string error;
};

struct HeadlessStationListEditOptions {
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

struct HeadlessRepeaterKeyEditOptions {
    bool requested = false;
    std::string path;
    std::string output_path;
    double unit_distance = 25.0;
    bool commit = false;
    std::string error;
};

struct HeadlessOtherTrackKeyEditOptions {
    bool requested = false;
    std::string path;
    std::string output_path;
    double unit_distance = 25.0;
    bool commit = false;
    std::string error;
};

struct HeadlessSectionEditBatchOptions {
    bool requested = false;
    std::string path;
    std::string output_path;
    double unit_distance = 25.0;
    bool commit = false;
    std::string error;
};

struct HeadlessInsertEditOptions {
    bool requested = false;
    bool repeater_only = false;
    std::string path;
    std::string output_path;
    double unit_distance = 25.0;
    bool commit = false;
    std::string error;
};

struct HeadlessIncludeDeleteOptions {
    bool requested = false;
    int index = 0;
    std::string path;
    std::string output_path;
    double unit_distance = 25.0;
    bool commit = false;
    std::string error;
};

// --debug-headless-include-replace: rewrites one include statement's path
// argument. new_path is the literal source text to embed (the GUI computes
// relative-first text; the headless check proves the typed edit and reparse).
struct HeadlessIncludeReplaceOptions {
    bool requested = false;
    int index = 0;
    std::string path;
    std::string new_path;
    std::string output_path;
    double unit_distance = 25.0;
    bool commit = false;
    std::string error;
};

struct HeadlessIncludeImportCreateOptions {
    bool requested = false;
    std::string path;
    std::string output_path;
    double unit_distance = 25.0;
    std::string error;
};

struct HeadlessResourceListReplaceOptions {
    bool requested = false;
    std::string path;
    std::string output_path;
    double unit_distance = 25.0;
    std::string error;
};

struct HeadlessNewFileWizardOptions {
    bool requested = false;
    std::string path;
    std::string output_path;
    double unit_distance = 25.0;
    std::string error;
};

struct HeadlessNewElementEditOptions {
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

struct HeadlessSettingsPersistenceOptions {
    bool requested = false;
    std::string output_path;
    std::string error;
};

struct HeadlessDiagnosticsPopupBenchOptions {
    bool requested = false;
    std::string output_path;
    std::string error;
};

struct HeadlessSceneLoaderContractOptions {
    bool requested = false;
    std::string output_path;
    std::string error;
};

struct HeadlessResourceSafetyContractResult {
    bool image_layout = false;
    bool image_decode = false;
    bool numeric_conversion = false;
};

HeadlessResourceSafetyContractResult run_debug_resource_safety_contract(
    const std::string& valid_image_path,
    const std::string& missing_image_path);

std::vector<std::string> command_line_args_utf8();
HeadlessLoadOptions parse_headless_load_options(const std::vector<std::string>& args);
HeadlessLoadScenarioOptions parse_headless_load_scenario_options(
    const std::vector<std::string>& args);
HeadlessPlanBenchmarkOptions parse_headless_plan_benchmark_options(const std::vector<std::string>& args);
HeadlessOpenBenchmarkOptions parse_headless_open_benchmark_options(const std::vector<std::string>& args);
HeadlessScene3DBenchmarkOptions parse_headless_scene3d_benchmark_options(const std::vector<std::string>& args);
HeadlessSceneCameraTransferOptions parse_headless_scene_camera_transfer_options(const std::vector<std::string>& args);
HeadlessSourceAnchorOptions parse_headless_source_anchor_options(const std::vector<std::string>& args);
HeadlessEditRoundtripOptions parse_headless_edit_roundtrip_options(const std::vector<std::string>& args);
HeadlessOwnTrackEditOptions parse_headless_own_track_edit_options(
    const std::vector<std::string>& args);
HeadlessOtherTrackEditOptions parse_headless_other_track_edit_options(
    const std::vector<std::string>& args);
HeadlessDistanceEditBatchOptions parse_headless_distance_edit_batch_options(
    const std::vector<std::string>& args);
HeadlessStationListEditOptions parse_headless_station_list_edit_options(
    const std::vector<std::string>& args);
HeadlessRepeaterEditBatchOptions parse_headless_repeater_edit_batch_options(
    const std::vector<std::string>& args);
HeadlessRepeaterKeyEditOptions parse_headless_repeater_key_edit_options(
    const std::vector<std::string>& args);
HeadlessOtherTrackKeyEditOptions parse_headless_other_track_key_edit_options(
    const std::vector<std::string>& args);
HeadlessSectionEditBatchOptions parse_headless_section_edit_batch_options(
    const std::vector<std::string>& args);
HeadlessInsertEditOptions parse_headless_insert_edit_options(
    const std::vector<std::string>& args);
HeadlessIncludeDeleteOptions parse_headless_include_delete_options(
    const std::vector<std::string>& args);
HeadlessIncludeReplaceOptions parse_headless_include_replace_options(
    const std::vector<std::string>& args);
HeadlessIncludeImportCreateOptions parse_headless_include_import_create_options(
    const std::vector<std::string>& args);
HeadlessResourceListReplaceOptions parse_headless_resource_list_replace_options(
    const std::vector<std::string>& args);
HeadlessNewFileWizardOptions parse_headless_new_file_wizard_options(
    const std::vector<std::string>& args);
HeadlessNewElementEditOptions parse_headless_new_element_edit_options(
    const std::vector<std::string>& args);
HeadlessTableFindOptions parse_headless_table_find_options(const std::vector<std::string>& args);
HeadlessTouchInputOptions parse_headless_touch_input_options(const std::vector<std::string>& args);
HeadlessSettingsPersistenceOptions parse_headless_settings_persistence_options(
    const std::vector<std::string>& args);
HeadlessDiagnosticsPopupBenchOptions parse_headless_diagnostics_popup_bench_options(
    const std::vector<std::string>& args);
HeadlessSceneLoaderContractOptions parse_headless_scene_loader_contract_options(
    const std::vector<std::string>& args);
int run_headless_load_map(const HeadlessLoadOptions& options);
int run_headless_load_scenario(const HeadlessLoadScenarioOptions& options);
int run_debug_headless_distance_edit_batch(const HeadlessDistanceEditBatchOptions& options);
int run_debug_headless_station_list_edit(const HeadlessStationListEditOptions& options);
int run_debug_headless_repeater_edit_batch(const HeadlessRepeaterEditBatchOptions& options);
int run_debug_headless_repeater_key_edit(const HeadlessRepeaterKeyEditOptions& options);
int run_debug_headless_section_edit_batch(const HeadlessSectionEditBatchOptions& options);
int run_debug_headless_insert_edit(const HeadlessInsertEditOptions& options);
int run_debug_headless_include_delete(const HeadlessIncludeDeleteOptions& options);
int run_debug_headless_include_replace(const HeadlessIncludeReplaceOptions& options);
int run_debug_headless_include_import_create(
    const HeadlessIncludeImportCreateOptions& options);
int run_debug_headless_touch_input(const HeadlessTouchInputOptions& options);
int run_debug_headless_settings_persistence(
    const HeadlessSettingsPersistenceOptions& options);
int run_debug_headless_scene_loader_contract(
    const HeadlessSceneLoaderContractOptions& options);
#endif
