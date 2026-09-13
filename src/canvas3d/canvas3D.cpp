/*
 * Copyright (c) 2026 Sapporo_ningyo
 *
 * Licensed under Apache License 2.0; see LICENSE and NOTICE.
 */

#ifdef _MSC_VER
#pragma execution_character_set("utf-8")
#endif

#include "canvas3d_impl.h"
#include "kme.h"

Canvas3D::Canvas3D(ID3D11Device* device, Canvas3DWakeCallback wake_callback)
    : impl_(std::make_unique<Impl>(device, wake_callback)) {
}

Canvas3D::~Canvas3D() = default;

bool Canvas3D::load_model(const std::string& path, std::string& error) {
    return impl_->load_model(path, error);
}

bool Canvas3D::reload_model(std::string& error) {
    return impl_->reload_model(error);
}

std::vector<std::string> Canvas3D::drain_model_load_warnings() {
    return impl_->drain_model_load_warnings();
}

void Canvas3D::clear_model() {
    impl_->clear_model();
}

bool Canvas3D::has_model() const {
    return impl_->has_model();
}

const std::string& Canvas3D::model_path() const {
    return impl_->model_path_value;
}

void Canvas3D::set_background_color(ImVec4 color) {
    impl_->background_color_value = clamp_theme_color(color);
}

void Canvas3D::render(ImVec2 size) {
    impl_->render(size);
}

bool Canvas3D::load_scene(Canvas3DScene scene, std::string& error,
                          bool preserve_loaded_models,
                          bool preserve_camera) {
    return impl_->load_scene(std::move(scene), error, preserve_loaded_models, preserve_camera);
}

bool Canvas3D::refresh_scene_dynamic_content(const MapModel& model, int station_index, std::string& error) {
    return impl_->refresh_scene_dynamic_content(model, station_index, error);
}

bool Canvas3D::set_scene_marker_visibility(
    const Canvas3DSceneMarkerVisibility& visibility,
    std::string& error) {
    return impl_->set_scene_marker_visibility(visibility, error);
}

bool Canvas3D::refresh_scene_route_stations(
    const MapModel& model,
    std::string& error) {
    return impl_->refresh_scene_route_stations(model, error);
}

bool Canvas3D::refresh_scene_map_content(
    const MapModel& model,
    const Canvas3DSceneMapRefreshOptions& options,
    std::string& error) {
    return impl_->refresh_scene_map_content(model, options, error);
}

void Canvas3D::clear_scene() {
    impl_->clear_scene();
}

bool Canvas3D::has_scene() const {
    return impl_->has_scene();
}

bool Canvas3D::reload_scene_models(std::string& error) {
    return impl_->reload_scene_models(error);
}

bool Canvas3D::set_scene_track_visibility(const std::vector<Canvas3DTrackVisibility>& visibility, std::string& error) {
    return impl_->set_scene_track_visibility(visibility, error);
}

void Canvas3D::set_scene_window(double back_m, double forward_m) {
    impl_->set_scene_window(back_m, forward_m);
}

void Canvas3D::set_scene_edit_component_scale(float scale) {
    impl_->set_scene_edit_component_scale(scale);
}

void Canvas3D::set_scene_interaction_mode(Canvas3DSceneInteractionMode mode) {
    impl_->set_scene_interaction_mode(mode);
}

void Canvas3D::set_scene_fog_enabled(bool enabled) {
    impl_->set_scene_fog_enabled(enabled);
}

void Canvas3D::set_scene_map_draw_distance_enabled(bool enabled) {
    impl_->set_scene_map_draw_distance_enabled(enabled);
}

void Canvas3D::set_scene_camera_speed_percent(int percent) {
    impl_->set_scene_camera_speed_percent(percent);
}

void Canvas3D::set_scene_performance_warning(bool enabled,
                                             size_t warning_threshold,
                                             size_t critical_warning_threshold) {
    impl_->set_scene_performance_warning(
        enabled, warning_threshold, critical_warning_threshold);
}

Canvas3DSceneInteractionMode Canvas3D::scene_interaction_mode() const {
    return impl_->scene_interaction_mode_value();
}

Canvas3DSceneStats Canvas3D::scene_stats() const {
    return impl_->scene_stats();
}

void Canvas3D::process_scene_loading() {
    impl_->upload_pending_scene_models();
}

#ifndef NDEBUG
bool Canvas3D::debug_check_scene_edit_target(const Canvas3DPlacementEditTarget& target) const {
    return impl_->debug_check_scene_edit_target(target);
}

void Canvas3D::set_debug_scene_loading_tuning(size_t worker_limit, bool texture_cache_enabled) {
    impl_->scene_model_worker_limit = worker_limit;
    impl_->scene_texture_cache_enabled = texture_cache_enabled;
}

Canvas3DSceneLoaderContractResult Canvas3D::debug_run_scene_loader_contract(
    const std::string& valid_model_path,
    const std::string& valid_texture_path) {
    return impl_->debug_run_scene_loader_contract(
        valid_model_path, valid_texture_path);
}

Canvas3DSceneFogDebugState Canvas3D::debug_scene_fog_state() const {
    return impl_->debug_scene_fog_state();
}

bool Canvas3D::set_debug_scene_frame_profiling(bool enabled, std::string& error) {
    return impl_->scene_frame_profiler.configure(impl_->device, enabled, error);
}

Canvas3DSceneFrameProfile Canvas3D::debug_scene_frame_profile() const {
    return impl_->scene_frame_profiler.result;
}

Canvas3DSceneRenderContractResult Canvas3D::debug_check_scene_render() {
    return impl_->debug_check_scene_render();
}

bool Canvas3D::debug_read_scene_render_pixels(std::vector<std::uint8_t>& rgba,
                                              int& width, int& height,
                                              std::string& error) {
    return impl_->debug_read_scene_render_pixels(rgba, width, height, error);
}
#endif

std::vector<std::string> Canvas3D::drain_scene_load_messages() {
    return impl_->drain_scene_load_messages();
}

Canvas3DSceneCameraPose Canvas3D::scene_camera_pose() const {
    return impl_->scene_camera_pose();
}

bool Canvas3D::jump_scene_camera_to_distance(double distance) {
    return impl_->reset_scene_camera_pose_at_distance(distance);
}

bool Canvas3D::jump_scene_camera_to_object(Canvas3DSceneObjectKind kind, size_t source_row) {
    return impl_->jump_scene_camera_to_object(kind, source_row);
}

bool Canvas3D::jump_scene_camera_to_marker(Canvas3DSceneMarkerListKind list_kind,
                                            size_t row_index) {
    return impl_->jump_scene_camera_to_marker(list_kind, row_index);
}

bool Canvas3D::set_scene_placement_edit_target(const Canvas3DPlacementEditTarget& target,
                                               bool show_gizmo) {
    return impl_->set_scene_placement_edit_target(target, show_gizmo);
}

bool Canvas3D::update_scene_placement_instance(const Canvas3DPlacementEditTarget& target) {
    return impl_->update_scene_placement_instance(target);
}

bool Canvas3D::set_scene_repeater_edit_target(const Canvas3DPlacementEditTarget& target,
                                               bool show_gizmo) {
    return impl_->set_scene_repeater_edit_target(target, show_gizmo);
}

bool Canvas3D::update_scene_repeater_segment(const Canvas3DPlacementEditTarget& target) {
    return impl_->update_scene_repeater_segment(target);
}

void Canvas3D::clear_scene_placement_edit_target() {
    impl_->clear_scene_placement_edit_target();
}

Canvas3DSceneFrameResult Canvas3D::render_scene_preview(
    ImVec2 size,
    const Canvas3DSceneUiText& ui_text,
    const Canvas3DSceneContextMenuOptions& context_menu_options) {
    return impl_->render_scene_preview(size, ui_text, context_menu_options);
}
