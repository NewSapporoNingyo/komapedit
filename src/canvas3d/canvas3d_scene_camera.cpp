/*
 * Copyright (c) 2026 Sapporo_ningyo
 *
 * Licensed under Apache License 2.0; see LICENSE and NOTICE.
 */

#ifdef _MSC_VER
#pragma execution_character_set("utf-8")
#endif

#include "canvas3d_impl.h"
#include "canvas3d_scene_geometry.h"
#include "kme.h"
#include "scene_track_sampling.h"
#include "imgui.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <optional>
#include <string>
#include <vector>

using namespace canvas3d_detail;

namespace canvas3d_detail {

constexpr double k_scene_focus_highlight_seconds = 3.0;

} // namespace canvas3d_detail

namespace canvas3d_detail {

constexpr double k_scene_object_jump_back_m = 25.0;

} // namespace canvas3d_detail

bool Canvas3D::Impl::scene_model_center_world(const std::string& model_path, const double world[16], DVec3& out) const {
    auto model_it = scene_models.find(model_path);
    if (model_it == scene_models.end() || model_it->second.state != SceneModelGpu::State::Ready) return false;
    out = transform_point_row(world, model_it->second.center);
    return true;
}

bool Canvas3D::Impl::find_placed_object_jump_target(Canvas3DSceneObjectKind kind,
                                    size_t source_row,
                                    SceneObjectJumpTarget& target) const {
    for (const SceneChunk& chunk : scene_chunks) {
        for (const SceneInstance& instance : chunk.instances) {
            if (!scene_object_index_valid(instance.object_index)) continue;
            const Canvas3DSceneObject& object = scene_data.objects[static_cast<size_t>(instance.object_index)];
            if (object.kind != kind || object.source_row != source_row) continue;
            if (!scene_model_center_world(instance.model_path, instance.world, target.center)) return false;
            target.object_index = instance.object_index;
            target.distance = instance.distance;
            target.model_path = instance.model_path;
            std::copy(instance.world, instance.world + 16, target.world);
            return true;
        }
    }
    return false;
}

const Canvas3DRepeaterSegment* Canvas3D::Impl::find_repeater_segment(size_t source_row) const {
    for (const Canvas3DRepeaterSegment& repeater : scene_data.repeaters) {
        if (!scene_object_index_valid(repeater.object_index)) continue;
        const Canvas3DSceneObject& object = scene_data.objects[static_cast<size_t>(repeater.object_index)];
        if (object.kind == Canvas3DSceneObjectKind::Repeater && object.source_row == source_row) {
            return &repeater;
        }
    }
    return nullptr;
}

bool Canvas3D::Impl::find_repeater_jump_target(size_t source_row, SceneObjectJumpTarget& target) const {
    const Canvas3DRepeaterSegment* repeater = find_repeater_segment(source_row);
    if (!repeater || repeater->model_paths.empty()) return false;
    const std::string& model_path = repeater->model_paths.front();
    if (model_path.empty()) return false;
    double world[16] = {};
    if (!make_repeater_instance_world(*repeater, repeater->begin_distance, world)) return false;
    target.object_index = repeater->object_index;
    target.distance = repeater->begin_distance;
    target.model_path = model_path;
    std::copy(world, world + 16, target.world);
    if (!scene_model_center_world(model_path, world, target.center)) {
        target.center = {world[12], world[13], world[14]};
    }
    return true;
}

bool Canvas3D::Impl::find_repeater_end_or_change_jump_target(size_t source_row,
                                             SceneObjectJumpTarget& target) const {
    const Canvas3DRepeaterSegment* repeater = find_repeater_segment(source_row);
    if (!repeater || !repeater->has_end_or_change_position) return false;

    double last_instance_distance = 0.0;
    size_t last_model_index = 0;
    if (!scene_repeater_last_instance(*repeater, last_instance_distance, last_model_index)) return false;

    const std::string& model_path = repeater->model_paths[last_model_index];
    double last_instance_world[16] = {};
    if (model_path.empty() ||
        !make_repeater_instance_world(*repeater, last_instance_distance, last_instance_world)) {
        return false;
    }

    double boundary_world[16] = {};
    if (!make_repeater_instance_world(*repeater, repeater->end_distance, boundary_world)) return false;

    target.object_index = repeater->object_index;
    target.distance = repeater->end_distance;
    target.model_path = model_path;
    std::copy(last_instance_world, last_instance_world + 16, target.world);
    if (!scene_model_center_world(model_path, last_instance_world, target.center)) {
        target.center = {boundary_world[12], boundary_world[13], boundary_world[14]};
    }
    return true;
}

bool Canvas3D::Impl::find_scene_object_jump_target(Canvas3DSceneObjectKind kind,
                                   size_t source_row,
                                   SceneObjectJumpTarget& target) const {
    if (kind == Canvas3DSceneObjectKind::Structure ||
        kind == Canvas3DSceneObjectKind::Signal) {
        return find_placed_object_jump_target(kind, source_row, target);
    }
    if (kind == Canvas3DSceneObjectKind::Repeater) return find_repeater_jump_target(source_row, target);
    return false;
}

void Canvas3D::Impl::clear_scene_focus_highlight() {
    scene_focus_highlight_object_index = -1;
    scene_focus_highlight_marker_index = -1;
    scene_focus_highlight_model_path.clear();
    scene_focus_highlight_until = {};
    scene_focus_highlight_batch.clear();
}

void Canvas3D::Impl::start_scene_focus_highlight(int object_index, const std::string& model_path, const double world[16]) {
    scene_focus_highlight_batch.clear();
    scene_focus_highlight_object_index = object_index;
    scene_focus_highlight_marker_index = -1;
    scene_focus_highlight_model_path = model_path;
    std::copy(world, world + 16, scene_focus_highlight_world);
    scene_focus_highlight_until =
        std::chrono::steady_clock::now() +
        std::chrono::duration_cast<std::chrono::steady_clock::duration>(
            std::chrono::duration<double>(k_scene_focus_highlight_seconds));
}

bool Canvas3D::Impl::scene_focus_highlight_active_now() {
    if (scene_focus_highlight_object_index < 0 && scene_focus_highlight_marker_index < 0) return false;
    if (std::chrono::steady_clock::now() < scene_focus_highlight_until) return true;
    clear_scene_focus_highlight();
    return false;
}

void Canvas3D::Impl::start_scene_marker_focus_highlight(size_t marker_index) {
    scene_focus_highlight_batch.clear();
    scene_focus_highlight_object_index = -1;
    scene_focus_highlight_marker_index = static_cast<int>(marker_index);
    scene_focus_highlight_model_path.clear();
    scene_focus_highlight_until =
        std::chrono::steady_clock::now() +
        std::chrono::duration_cast<std::chrono::steady_clock::duration>(
            std::chrono::duration<double>(k_scene_focus_highlight_seconds));
}

bool Canvas3D::Impl::update_scene_focus_highlight_batch(DVec3 render_origin,
                                        const Mat4& view_proj,
                                        int width,
                                        int height) {
    scene_focus_highlight_batch.clear();
    if (!scene_focus_highlight_active_now() ||
        !scene_object_index_valid(scene_focus_highlight_object_index)) {
        return false;
    }
    auto model_it = scene_models.find(scene_focus_highlight_model_path);
    if (model_it == scene_models.end() || model_it->second.state != SceneModelGpu::State::Ready) return false;

    SceneScreenBounds bounds;
    if (!compute_scene_instance_screen_bounds(scene_focus_highlight_world,
                                              model_it->second,
                                              render_origin,
                                              view_proj,
                                              width,
                                              height,
                                              bounds)) {
        return false;
    }

    scene_focus_highlight_batch.instances.push_back(SceneHighlightInstance{
        scene_focus_highlight_model_path,
        make_instance_data_relative(scene_focus_highlight_world, render_origin),
        bounds.screen_min,
        bounds.screen_max
    });
    scene_focus_highlight_batch.screen_min = bounds.screen_min;
    scene_focus_highlight_batch.screen_max = bounds.screen_max;
    scene_focus_highlight_batch.object_index = scene_focus_highlight_object_index;
    return true;
}

bool Canvas3D::Impl::set_scene_camera_for_target(double distance, DVec3 target_center) {
    scene_camera_distance = scene_track_sampling::clamp_camera_distance(
        scene_data, distance - k_scene_object_jump_back_m);
    scene_camera_lateral_offset = 0.0;
    scene_camera_vertical_offset = k_default_scene_camera_height;
    scene_camera_yaw_offset = 0.0f;
    scene_camera_pitch = 0.0f;
    scene_rotating = false;
    if (!update_scene_camera_from_owntrack()) return false;

    DVec3 to_target = target_center - scene_camera_pos;
    double len_sq = dot(to_target, to_target);
    if (len_sq > 1e-12) {
        double len = std::sqrt(len_sq);
        scene_camera_yaw = static_cast<float>(std::atan2(to_target.x, -to_target.z));
        scene_camera_pitch = static_cast<float>(
            std::clamp(std::asin(std::clamp(to_target.y / len, -1.0, 1.0)), -1.45, 1.45));
        Canvas3DTrackPoint point;
        if (sample_own_track_for_camera(scene_camera_distance, point)) {
            scene_camera_yaw_offset = scene_camera_yaw - static_cast<float>(point.theta);
        }
    }

    return true;
}

bool Canvas3D::Impl::jump_scene_camera_to_object(Canvas3DSceneObjectKind kind, size_t source_row) {
    if (!scene_active) return false;

    SceneObjectJumpTarget target;
    if (!find_scene_object_jump_target(kind, source_row, target) ||
        !set_scene_camera_for_target(target.distance, target.center)) {
        return false;
    }

    start_scene_focus_highlight(target.object_index, target.model_path, target.world);
    return true;
}

bool Canvas3D::Impl::jump_scene_camera_to_marker(Canvas3DSceneMarkerListKind list_kind,
                                 size_t row_index) {
    if (!scene_active || !scene_marker_list_kind_is_navigable(list_kind)) return false;
    const size_t list_slot = scene_marker_list_kind_slot(list_kind);
    if (list_slot >= scene_marker_target_indices.size()) return false;
    const std::vector<size_t>& target_indices = scene_marker_target_indices[list_slot];
    if (row_index >= target_indices.size()) return false;
    const size_t marker_index = target_indices[row_index];
    if (marker_index == k_scene_marker_target_missing ||
        marker_index >= scene_data.markers.size()) {
        return false;
    }
    const Canvas3DSceneMarker& marker = scene_data.markers[marker_index];
    if (marker.list_kind != list_kind || !marker.row_index ||
        *marker.row_index != row_index ||
        !set_scene_camera_for_target(
            marker.track_point.distance,
            DVec3{marker.track_point.x, marker.track_point.y, marker.track_point.z})) {
        return false;
    }
    start_scene_marker_focus_highlight(marker_index);
    return true;
}

bool Canvas3D::Impl::jump_scene_camera_to_repeater_end_or_change(size_t source_row) {
    if (!scene_active) return false;
    SceneObjectJumpTarget target;
    if (!find_repeater_end_or_change_jump_target(source_row, target) ||
        !set_scene_camera_for_target(target.distance, target.center)) {
        return false;
    }

    start_scene_focus_highlight(target.object_index, target.model_path, target.world);
    return true;
}

Vec3 Canvas3D::Impl::scene_forward() const {
    float cp = std::cos(scene_camera_pitch);
    Vec3 forward{cp * std::sin(scene_camera_yaw),
                 std::sin(scene_camera_pitch),
                 -cp * std::cos(scene_camera_yaw)};
    return normalize(forward);
}

Vec3 Canvas3D::Impl::scene_right() const {
    return normalize(cross(scene_forward(), {0.0f, 1.0f, 0.0f}));
}

const Canvas3DTrackPath* Canvas3D::Impl::own_track_path() const {
#ifndef NDEBUG
    if (debug_scene_reference) return scene_own_track_path(scene_data);
#endif
    return scene_placement_tracks.own(scene_data);
}

bool Canvas3D::Impl::sample_track_path(const Canvas3DTrackPath& path, double distance, Canvas3DTrackPoint& out) const {
    std::optional<Canvas3DTrackPoint> sample =
        scene_sample_track_path_points(path, distance);
    if (!sample) return false;
    out = *sample;
    return true;
}

bool Canvas3D::Impl::sample_own_track(double distance, Canvas3DTrackPoint& out) const {
    const Canvas3DTrackPath* path = own_track_path();
    return path && sample_track_path(*path, distance, out);
}

bool Canvas3D::Impl::sample_own_track_for_camera(double distance, Canvas3DTrackPoint& out) const {
    const Canvas3DTrackPath* path = own_track_path();
    if (!path) return false;
    std::optional<Canvas3DTrackPoint> sample =
        scene_track_sampling::camera_sample_track(*path, distance);
    if (!sample) return false;
    out = *sample;
    return true;
}

const Canvas3DTrackPath* Canvas3D::Impl::placement_track_path_for_key(const std::string& key) const {
#ifndef NDEBUG
    if (debug_scene_reference) return scene_placement_track_path_for_key(scene_data, key);
#endif
    return scene_placement_tracks.find(scene_data, key);
}

bool Canvas3D::Impl::update_scene_camera_from_owntrack() {
    Canvas3DTrackPoint point;
    if (!sample_own_track_for_camera(scene_camera_distance, point)) return false;
    DVec3 base{point.x, point.y, point.z};
    DVec3 right = right_from_theta_d(point.theta);
    scene_camera_pos = base + right * scene_camera_lateral_offset;
    scene_camera_pos.y += scene_camera_vertical_offset;
    scene_camera_yaw = point.theta + scene_camera_yaw_offset;
    return true;
}

bool Canvas3D::Impl::reset_scene_camera_pose_at_distance(double distance) {
    if (!scene_active) return false;
    scene_camera_distance = scene_track_sampling::clamp_camera_distance(scene_data, distance);
    scene_camera_lateral_offset = 0.0;
    scene_camera_vertical_offset = k_default_scene_camera_height;
    scene_camera_yaw_offset = 0.0f;
    scene_camera_pitch = 0.0f;
    scene_rotating = false;
    return update_scene_camera_from_owntrack();
}

void Canvas3D::Impl::reset_scene_camera_tracking() {
    Canvas3DTrackPoint point;
    scene_camera_lateral_offset = 0.0;
    scene_camera_vertical_offset = k_default_scene_camera_height;
    scene_camera_yaw_offset = 0.0f;
    if (!sample_own_track_for_camera(scene_camera_distance, point)) return;

    DVec3 base{point.x, point.y, point.z};
    DVec3 current = scene_camera_pos;
    DVec3 diff = current - base;
    scene_camera_lateral_offset = dot(diff, right_from_theta_d(point.theta));
    scene_camera_vertical_offset = static_cast<float>(diff.y);
    scene_camera_yaw_offset = scene_camera_yaw - point.theta;
    update_scene_camera_from_owntrack();
}

void Canvas3D::Impl::handle_scene_input(bool hovered, bool block_left_drag) {
    if (!hovered) {
        scene_rotating = false;
        return;
    }
    ImGuiIO& io = ImGui::GetIO();
    if (ImGui::IsKeyPressed(ImGuiKey_X, false)) {
        reset_scene_camera_pose_at_distance(scene_camera_distance);
    }

    const bool rotation_enabled = scene_interaction_mode == Canvas3DSceneInteractionMode::Move;
    if (rotation_enabled && !block_left_drag && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        if (!scene_rotating) {
            scene_rotating = true;
            scene_last_mouse = io.MousePos;
        } else {
            ImVec2 delta(io.MousePos.x - scene_last_mouse.x, io.MousePos.y - scene_last_mouse.y);
            const float yaw_delta = -delta.x * 0.005f;
            scene_camera_yaw += yaw_delta;
            scene_camera_yaw_offset += yaw_delta;
            scene_camera_pitch = std::clamp(scene_camera_pitch + delta.y * 0.005f, -1.45f, 1.45f);
            scene_last_mouse = io.MousePos;
        }
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
    } else {
        scene_rotating = false;
    }

    float dt = std::clamp(io.DeltaTime, 1.0f / 240.0f, 0.1f);
    bool fast = ImGui::IsKeyDown(ImGuiKey_LeftCtrl) || ImGui::IsKeyDown(ImGuiKey_RightCtrl);
    float speed_factor = static_cast<float>(scene_camera_speed_percent) / 100.0f;
    float step = scene_slow_speed_mps * speed_factor * (fast ? scene_fast_multiplier : 1.0f) * dt;
    float distance_delta = 0.0f;
    float lateral_delta = 0.0f;
    float vertical_delta = 0.0f;
    const bool save_shortcut_down =
        io.KeyMods == (ImGuiMod_Ctrl | ImGuiMod_Shift) &&
        ImGui::IsKeyDown(ImGuiKey_S);
    if (ImGui::IsKeyDown(ImGuiKey_W)) distance_delta += step;
    if (ImGui::IsKeyDown(ImGuiKey_S) && !save_shortcut_down) distance_delta -= step;
    if (ImGui::IsKeyDown(ImGuiKey_D)) lateral_delta += step;
    if (ImGui::IsKeyDown(ImGuiKey_A)) lateral_delta -= step;
    if (ImGui::IsKeyDown(ImGuiKey_R)) vertical_delta += step;
    if (ImGui::IsKeyDown(ImGuiKey_F)) vertical_delta -= step;

    Canvas3DTrackPoint own_point;
    if (sample_own_track_for_camera(scene_camera_distance, own_point)) {
        scene_camera_distance = scene_track_sampling::clamp_camera_distance(
            scene_data, scene_camera_distance + static_cast<double>(distance_delta));
        scene_camera_lateral_offset += lateral_delta;
        scene_camera_vertical_offset += vertical_delta;
        update_scene_camera_from_owntrack();
    } else {
        Vec3 forward = scene_forward();
        Vec3 right = scene_right();
        DVec3 delta{};
        delta = delta + dvec3_from_vec3(forward) * static_cast<double>(distance_delta);
        delta = delta + dvec3_from_vec3(right) * static_cast<double>(lateral_delta);
        delta.y += vertical_delta;
        scene_camera_pos = scene_camera_pos + delta;
        scene_camera_distance = scene_track_sampling::clamp_camera_distance(
            scene_data, scene_camera_distance + static_cast<double>(distance_delta));
    }
}
