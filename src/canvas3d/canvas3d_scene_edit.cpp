/*
 * Copyright (c) 2026 Sapporo_ningyo
 *
 * Licensed under Apache License 2.0; see LICENSE and NOTICE.
 */

#ifdef _MSC_VER
#pragma execution_character_set("utf-8")
#endif

#include "canvas3d_impl.h"
#include "canvas3d_scene_data.h"
#include "canvas3d_scene_geometry.h"
#include "kme.h"
#include "imgui.h"
#include <d3d11.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iterator>
#include <mutex>
#include <limits>
#include <optional>
#include <string>
#include <utility>
#include <vector>

using namespace canvas3d_detail;

bool Canvas3D::Impl::same_placement_edit_target(const Canvas3DPlacementEditTarget& a,
                                       const Canvas3DPlacementEditTarget& b) {
    return a.kind == b.kind && a.edit_id == b.edit_id &&
        a.model_path == b.model_path && a.track_key == b.track_key &&
        a.put_between_track_key1 == b.put_between_track_key1 &&
        a.put_between_track_key2 == b.put_between_track_key2 &&
        a.put_between_flag == b.put_between_flag &&
        a.distance == b.distance &&
        a.placement_distance_gizmo == b.placement_distance_gizmo &&
        a.has_repeater_end_distance == b.has_repeater_end_distance &&
        a.repeater_end_distance == b.repeater_end_distance &&
        a.x == b.x && a.y == b.y && a.z == b.z &&
        a.rx == b.rx && a.ry == b.ry && a.rz == b.rz &&
        a.tilt == b.tilt && a.span == b.span;
}

const std::string* Canvas3D::Impl::placement_edit_id_for_object(int object_index) const {
    if (!scene_object_index_valid(object_index)) return nullptr;
    const Canvas3DSceneObject& object = scene_data.objects[static_cast<size_t>(object_index)];
    const bool editable_placement =
        object.kind == Canvas3DSceneObjectKind::Structure ||
        object.kind == Canvas3DSceneObjectKind::Signal;
    if (!editable_placement || object.edit_id.empty()) {
        return nullptr;
    }
    return &object.edit_id;
}

size_t Canvas3D::Impl::scene_chunk_index_for_distance(double distance) const {
    if (scene_chunks.empty()) return 0;
    const double first = scene_chunks.front().d_min;
    const double relative = (distance - first) / scene_chunk_m;
    if (!std::isfinite(relative)) {
        return relative > 0.0 ? scene_chunks.size() - 1 : 0;
    }
    if (relative <= 0.0) return 0;
    if (relative >= static_cast<double>(scene_chunks.size())) {
        return scene_chunks.size() - 1;
    }
    return static_cast<size_t>(std::floor(relative));
}

Canvas3DModelInstance Canvas3D::Impl::placement_instance_from_target(
    const Canvas3DPlacementEditTarget& target,
    const Canvas3DModelInstance& base) const {
    Canvas3DModelInstance desired = base;
    desired.track_key = target.track_key;
    desired.distance = target.distance;
    desired.follow_track = true;
    desired.put_between = false;
    desired.x = target.x;
    desired.y = target.y;
    desired.z = target.z;
    desired.rx = target.rx;
    desired.ry = target.ry;
    desired.rz = target.rz;
    desired.tilt = target.tilt;
    desired.span = target.span;
    return desired;
}

Canvas3DModelInstance Canvas3D::Impl::put_between_instance_from_target(
    const Canvas3DPlacementEditTarget& target,
    const Canvas3DModelInstance& base) const {
    Canvas3DModelInstance desired = base;
    desired.model_path = target.model_path;
    desired.distance = target.distance;
    desired.follow_track = false;
    desired.put_between = true;
    desired.put_between_track_key1 = target.put_between_track_key1;
    desired.put_between_track_key2 = target.put_between_track_key2;
    desired.put_between_flag = target.put_between_flag & 1;
    return desired;
}

Canvas3DPlacementEditTarget Canvas3D::Impl::put_between_target_from_instance(
    const std::string& edit_id,
    const Canvas3DModelInstance& instance) const {
    Canvas3DPlacementEditTarget target;
    target.kind = Canvas3DSceneEditKind::StructurePutBetween;
    target.edit_id = edit_id;
    target.model_path = instance.model_path;
    target.put_between_track_key1 = instance.put_between_track_key1;
    target.put_between_track_key2 = instance.put_between_track_key2;
    target.put_between_flag = instance.put_between_flag & 1;
    target.distance = instance.distance;
    return target;
}

Canvas3DRepeaterSegment Canvas3D::Impl::repeater_segment_from_target(
    const Canvas3DPlacementEditTarget& target,
    const Canvas3DRepeaterSegment& base) const {
    Canvas3DRepeaterSegment desired = base;
    desired.track_key = target.track_key;
    desired.begin_distance = target.distance;
    if (target.has_repeater_end_distance) {
        desired.end_distance = target.repeater_end_distance;
        desired.has_end_or_change_position = true;
    }
    desired.x = target.x;
    desired.y = target.y;
    desired.z = target.z;
    desired.rx = target.rx;
    desired.ry = target.ry;
    desired.rz = target.rz;
    desired.tilt = target.tilt;
    desired.span = target.span;
    return desired;
}

bool Canvas3D::Impl::replace_scene_placement_instance(const std::string& edit_id,
                                      Canvas3DModelInstance desired,
                                      const std::string& render_model_path) {
    auto location_it = scene_placement_locations.find(edit_id);
    if (location_it == scene_placement_locations.end() || scene_chunks.empty()) return false;
    ScenePlacementInstanceLocation location = location_it->second;
    if (location.source_index >= scene_data.instances.size() ||
        location.chunk_index >= scene_chunks.size() ||
        location.chunk_instance_index >= scene_chunks[location.chunk_index].instances.size()) {
        return false;
    }

    SceneInstance replacement;
    replacement.model_path = render_model_path;
    replacement.distance = desired.distance;
    replacement.object_index = desired.object_index;
    std::copy(desired.world, desired.world + 16, replacement.world);

    const size_t destination_chunk_index = scene_chunk_index_for_distance(desired.distance);
    if (destination_chunk_index == location.chunk_index) {
        scene_chunks[location.chunk_index].instances[location.chunk_instance_index] =
            std::move(replacement);
    } else {
        SceneChunk& old_chunk = scene_chunks[location.chunk_index];
        const size_t last_index = old_chunk.instances.size() - 1;
        if (location.chunk_instance_index != last_index) {
            old_chunk.instances[location.chunk_instance_index] =
                std::move(old_chunk.instances[last_index]);
            const SceneInstance& moved = old_chunk.instances[location.chunk_instance_index];
            if (const std::string* moved_edit_id = placement_edit_id_for_object(moved.object_index)) {
                auto moved_location = scene_placement_locations.find(*moved_edit_id);
                if (moved_location != scene_placement_locations.end()) {
                    moved_location->second.chunk_index = location.chunk_index;
                    moved_location->second.chunk_instance_index = location.chunk_instance_index;
                }
            }
        }
        old_chunk.instances.pop_back();

        SceneChunk& destination = scene_chunks[destination_chunk_index];
        location.chunk_index = destination_chunk_index;
        location.chunk_instance_index = destination.instances.size();
        destination.instances.push_back(std::move(replacement));
        scene_placement_locations[edit_id] = location;
    }

    scene_data.instances[location.source_index] = desired;
    if (scene_focus_highlight_object_index == desired.object_index) {
        scene_focus_highlight_model_path = render_model_path.empty()
            ? desired.model_path : render_model_path;
        std::copy(desired.world, desired.world + 16, scene_focus_highlight_world);
    }
    return true;
}

bool Canvas3D::Impl::write_scene_placement_instance(const std::string& edit_id,
                                    Canvas3DModelInstance desired) {
    const std::string placement_track_key = desired.track_key;
    const double placement_distance = desired.distance;
    StructurePlacementFrame frame;
    if (!make_track_placement_frame(desired.track_key, desired.distance,
                                    desired.x, desired.y, desired.z,
                                    desired.rx, desired.ry, desired.rz,
                                    desired.tilt, desired.span, frame)) {
        return false;
    }
    store_world(desired.world, frame.model_right, frame.model_up,
                frame.model_forward, frame.origin);
    const std::string render_model_path =
        scene_model_key_for_instance(desired, scene_geometry_generation);
    if (!replace_scene_placement_instance(
            edit_id, std::move(desired), render_model_path)) {
        return false;
    }
    if (scene_structure_edit.active && scene_structure_edit.edit_id == edit_id) {
        if (scene_structure_edit.current.placement_distance_gizmo) {
            track_distance_gizmo_frame(
                placement_track_key, placement_distance,
                scene_structure_edit.placement_gizmo);
        } else {
            scene_structure_edit.placement_gizmo.origin = frame.origin;
            scene_structure_edit.placement_gizmo.axes = frame.parameter_axes;
        }
    }
    return true;
}

bool Canvas3D::Impl::put_between_edit_frame(double distance, DVec3& origin,
                            std::array<DVec3, 3>& axes) const {
    Canvas3DTrackPoint point;
    if (!sample_own_track(distance, point)) return false;
    origin = {point.x, point.y, point.z};
    const double gradient = std::isfinite(point.gradient)
        ? point.gradient / 1000.0 : 0.0;
    axes = {};
    axes[2] = normalize(DVec3{
        std::sin(point.theta), gradient, -std::cos(point.theta)});
    return true;
}

bool Canvas3D::Impl::write_scene_put_between_instance(const std::string& edit_id,
                                      Canvas3DModelInstance desired,
                                      const std::string& render_model_path) {
    DVec3 origin;
    std::array<DVec3, 3> axes{};
    if (!put_between_edit_frame(desired.distance, origin, axes)) return false;
    std::fill(std::begin(desired.world), std::end(desired.world), 0.0);
    desired.world[0] = 1.0;
    desired.world[5] = 1.0;
    desired.world[10] = 1.0;
    desired.world[15] = 1.0;
    desired.world[12] = origin.x;
    desired.world[13] = origin.y;
    desired.world[14] = origin.z;
    if (!replace_scene_placement_instance(
            edit_id, std::move(desired), render_model_path)) {
        return false;
    }
    if (scene_structure_edit.active && scene_structure_edit.edit_id == edit_id) {
        scene_structure_edit.placement_gizmo.origin = origin;
        scene_structure_edit.placement_gizmo.axes = axes;
    }
    return true;
}

bool Canvas3D::Impl::track_distance_gizmo_frame(const std::string& track_key,
                                double distance,
                                SceneGizmoHandle& gizmo) const {
    gizmo.enabled = {{false, false, true}};
    gizmo.axes = {};
    const Canvas3DTrackPath* path = placement_track_path_for_key(track_key);
    if (!path || path->points.empty()) return false;

    const double path_begin = path->points.front().distance;
    const double path_end = path->points.back().distance;
    if (!std::isfinite(path_begin) || !std::isfinite(path_end)) return false;
    const double range_min = std::min(path_begin, path_end);
    const double range_max = std::max(path_begin, path_end);
    const double sample_distance = std::clamp(
        distance, range_min, range_max);
    const std::optional<Canvas3DTrackPoint> center =
        scene_sample_track_path_points(*path, sample_distance);
    if (!center) return false;
    gizmo.origin = {center->x, center->y, center->z};

    double low_distance = std::max(range_min, sample_distance - 0.5);
    double high_distance = std::min(range_max, sample_distance + 0.5);
    if (high_distance - low_distance < k_scene_repeater_distance_epsilon) {
        low_distance = std::max(range_min, sample_distance - 1.0);
        high_distance = std::min(range_max, sample_distance + 1.0);
    }
    const double distance_span = high_distance - low_distance;
    if (!std::isfinite(distance_span) ||
        distance_span < k_scene_repeater_distance_epsilon) {
        return false;
    }
    const std::optional<Canvas3DTrackPoint> low =
        scene_sample_track_path_points(*path, low_distance);
    const std::optional<Canvas3DTrackPoint> high =
        scene_sample_track_path_points(*path, high_distance);
    if (!low || !high) return false;
    const DVec3 distance_axis = DVec3{
        high->x - low->x,
        high->y - low->y,
        high->z - low->z,
    } * (1.0 / distance_span);
    if (!std::isfinite(distance_axis.x) ||
        !std::isfinite(distance_axis.y) ||
        !std::isfinite(distance_axis.z) ||
        dot(distance_axis, distance_axis) <= 1e-12) {
        return false;
    }
    gizmo.axes[2] = distance_axis;
    return true;
}

bool Canvas3D::Impl::sound3d_gizmo_frame(const Canvas3DPlacementEditTarget& target,
                         SceneGizmoHandle& gizmo) const {
    if (!track_distance_gizmo_frame({}, target.distance, gizmo)) return false;
    Canvas3DTrackPoint track_point;
    if (!sample_own_track(target.distance, track_point)) return false;
    DVec3 right;
    DVec3 up;
    DVec3 forward;
    scene_track_surface_frame(track_point, right, up, forward);
    const Canvas3DTrackPoint source = scene_sound3d_source_point(
        track_point, target.x, target.y);
    gizmo.origin = {source.x, source.y, source.z};
    gizmo.axes[0] = right;
    gizmo.axes[1] = up;
    gizmo.enabled = {{true, true, true}};
    return true;
}

bool Canvas3D::Impl::sound3d_marker_baseline_target(
    const Canvas3DPlacementEditTarget& target,
    Canvas3DPlacementEditTarget& baseline) const {
    const auto index_it = scene_sound3d_marker_indices.find(target.edit_id);
    if (index_it == scene_sound3d_marker_indices.end() ||
        index_it->second >= scene_data.markers.size()) {
        return false;
    }
    const Canvas3DSceneMarker& marker = scene_data.markers[index_it->second];
    if (marker.kind != MapMarkerVisualKind::MapSound3D) return false;
    Canvas3DTrackPoint track_point;
    if (!sample_own_track(marker.track_point.distance, track_point)) return false;
    DVec3 right;
    DVec3 up;
    DVec3 forward;
    scene_track_surface_frame(track_point, right, up, forward);
    const DVec3 offset =
        DVec3{marker.track_point.x, marker.track_point.y, marker.track_point.z} -
        DVec3{track_point.x, track_point.y, track_point.z};
    baseline = target;
    baseline.distance = track_point.distance;
    baseline.x = dot(offset, right);
    baseline.y = dot(offset, up);
    return true;
}

bool Canvas3D::Impl::update_scene_sound3d_marker(const std::string& edit_id,
                                 double distance,
                                 double x,
                                 double y) {
    const auto index_it = scene_sound3d_marker_indices.find(edit_id);
    if (index_it == scene_sound3d_marker_indices.end() ||
        index_it->second >= scene_data.markers.size()) {
        return false;
    }
    const size_t marker_index = index_it->second;
    Canvas3DTrackPoint track_point;
    if (!sample_own_track(distance, track_point)) return false;
    Canvas3DSceneMarker replacement = scene_data.markers[marker_index];
    if (replacement.kind != MapMarkerVisualKind::MapSound3D) return false;
    replacement.track_point = scene_sound3d_source_point(track_point, x, y);

    if (marker_index >= scene_marker_locations.size()) {
        scene_data.markers[marker_index] = std::move(replacement);
        return true;
    }
    const SceneMarkerGpuLocation location =
        scene_marker_locations[marker_index];
    if (!location.valid || location.chunk_index >= scene_marker_chunks.size()) {
        scene_data.markers[marker_index] = std::move(replacement);
        return true;
    }

    const size_t destination_chunk = scene_chunk_index_for_distance(
        replacement.track_point.distance);
    if (destination_chunk != location.chunk_index ||
        !scene_marker_font_cache_current()) {
        scene_data.markers[marker_index] = std::move(replacement);
        sort_canvas3d_scene_markers(scene_data.markers);
        std::string error;
        if (build_scene_marker_chunks(error)) return true;
        if (!error.empty()) scene_last_error = error;
        return false;
    }

    SceneMarkerChunkGpu& chunk = scene_marker_chunks[location.chunk_index];
    if (!context || !chunk.vertex_buffer || !scene_marker_font ||
        location.range_index + location.range_count > chunk.ranges.size()) {
        return false;
    }
    ImFontBaked* baked = scene_marker_font->GetFontBaked(scene_marker_font_size);
    if (!baked) return false;

    std::vector<SceneMarkerVertex> vertices;
    std::vector<unsigned int> indices;
    std::vector<SceneMarkerIndexRange> ranges;
    const SceneMarkerGeometrySpan span = append_scene_marker_geometry(
        replacement, marker_index, chunk.origin, 0.0f,
        *scene_marker_font, *baked, scene_marker_font_size,
        vertices, indices, ranges);
    if (span.vertex_count != location.vertex_count ||
        span.range_count != location.range_count ||
        vertices.empty()) {
        return false;
    }
    for (size_t range_index = 0; range_index < ranges.size(); ++range_index) {
        const SceneMarkerIndexRange& previous =
            chunk.ranges[location.range_index + range_index];
        const SceneMarkerIndexRange& updated = ranges[range_index];
        if (previous.kind != updated.kind ||
            previous.marker_index != updated.marker_index ||
            previous.label != updated.label ||
            previous.count != updated.count) {
            return false;
        }
    }
    const size_t byte_first = location.vertex_first * sizeof(SceneMarkerVertex);
    const size_t byte_end =
        (location.vertex_first + location.vertex_count) * sizeof(SceneMarkerVertex);
    if (byte_end > static_cast<size_t>(std::numeric_limits<UINT>::max())) {
        return false;
    }
    D3D11_BOX box = {};
    box.left = static_cast<UINT>(byte_first);
    box.right = static_cast<UINT>(byte_end);
    box.top = 0;
    box.bottom = 1;
    box.front = 0;
    box.back = 1;
    context->UpdateSubresource(
        chunk.vertex_buffer, 0, &box, vertices.data(), 0, 0);
    for (size_t range_index = 0; range_index < ranges.size(); ++range_index) {
        SceneMarkerIndexRange& destination =
            chunk.ranges[location.range_index + range_index];
        const SceneMarkerIndexRange& source = ranges[range_index];
        destination.center = source.center;
        destination.right = source.right;
        destination.up = source.up;
    }
    scene_data.markers[marker_index] = std::move(replacement);
    return true;
}

std::optional<std::pair<size_t, size_t>> Canvas3D::Impl::scene_repeater_chunk_range(
    const Canvas3DRepeaterSegment& repeater) const {
    if (scene_chunks.empty()) return std::nullopt;
    double first_distance = 0.0;
    double last_distance = 0.0;
    if (!scene_repeater_render_distance_span(
            repeater, first_distance, last_distance)) {
        return std::nullopt;
    }
    const size_t first_index = scene_chunk_index_for_distance(first_distance);
    const size_t last_index = scene_chunk_index_for_distance(last_distance);
    return std::pair<size_t, size_t>{
        std::min(first_index, last_index),
        std::max(first_index, last_index),
    };
}

void Canvas3D::Impl::update_scene_repeater_chunk_membership(
    size_t repeater_index,
    const std::optional<std::pair<size_t, size_t>>& old_range,
    const std::optional<std::pair<size_t, size_t>>& new_range) {
    const auto contains = [](const std::optional<std::pair<size_t, size_t>>& range,
                             size_t index) {
        return range && index >= range->first && index <= range->second;
    };
    const auto visit_range = [&](const std::pair<size_t, size_t>& range,
                                 const auto& visitor) {
        for (size_t index = range.first;; ++index) {
            visitor(index);
            if (index == range.second) break;
        }
    };
    if (old_range) {
        visit_range(*old_range, [&](size_t chunk_index) {
            if (chunk_index < scene_chunks.size()) invalidate_scene_repeater_cache(scene_chunks[chunk_index]);
            if (contains(new_range, chunk_index) ||
                chunk_index >= scene_chunks.size()) {
                return;
            }
            std::vector<size_t>& indices = scene_chunks[chunk_index].repeater_indices;
            const auto found = std::lower_bound(
                indices.begin(), indices.end(), repeater_index);
            if (found != indices.end() && *found == repeater_index) {
                indices.erase(found);
            }
        });
    }
    if (new_range) {
        visit_range(*new_range, [&](size_t chunk_index) {
            if (chunk_index < scene_chunks.size()) invalidate_scene_repeater_cache(scene_chunks[chunk_index]);
            if (contains(old_range, chunk_index) ||
                chunk_index >= scene_chunks.size()) {
                return;
            }
            std::vector<size_t>& indices = scene_chunks[chunk_index].repeater_indices;
            const auto insertion = std::lower_bound(
                indices.begin(), indices.end(), repeater_index);
            if (insertion == indices.end() || *insertion != repeater_index) {
                indices.insert(insertion, repeater_index);
            }
        });
    }
}

bool Canvas3D::Impl::write_scene_repeater_segment(const std::string& edit_id,
                                  Canvas3DRepeaterSegment desired) {
    const auto location_it = scene_repeater_locations.find(edit_id);
    if (location_it == scene_repeater_locations.end() ||
        location_it->second >= scene_data.repeaters.size() ||
        desired.model_paths.empty() || desired.model_paths.front().empty()) {
        return false;
    }
    const size_t repeater_index = location_it->second;
    const Canvas3DRepeaterSegment& current = scene_data.repeaters[repeater_index];
    StructurePlacementFrame frame;
    if (!make_track_placement_frame(desired.track_key, desired.begin_distance,
                                    desired.x, desired.y, desired.z,
                                    desired.rx, desired.ry, desired.rz,
                                    desired.tilt, desired.span, frame)) {
        return false;
    }
    const std::optional<std::pair<size_t, size_t>> old_range =
        scene_repeater_chunk_range(current);
    const std::optional<std::pair<size_t, size_t>> new_range =
        scene_repeater_chunk_range(desired);
    const size_t old_instance_count = scene_repeater_instance_count(current);
    const size_t new_instance_count = scene_repeater_instance_count(desired);
    update_scene_repeater_chunk_membership(repeater_index, old_range, new_range);
    scene_data.repeaters[repeater_index] = std::move(desired);
    if (scene_stats_value.instance_count >= old_instance_count) {
        const size_t non_repeater_count =
            scene_stats_value.instance_count - old_instance_count;
        if (new_instance_count <=
            std::numeric_limits<size_t>::max() - non_repeater_count) {
            scene_stats_value.instance_count = non_repeater_count + new_instance_count;
        } else {
            scene_stats_value.instance_count = std::numeric_limits<size_t>::max();
        }
    } else {
        scene_stats_value.instance_count = count_scene_instances();
    }
    if (scene_structure_edit.active &&
        scene_structure_edit.kind == Canvas3DSceneEditKind::Repeater &&
        scene_structure_edit.edit_id == edit_id) {
        const Canvas3DRepeaterSegment& updated =
            scene_data.repeaters[repeater_index];
        if (scene_structure_edit.current.placement_distance_gizmo) {
            track_distance_gizmo_frame(
                updated.track_key, updated.begin_distance,
                scene_structure_edit.placement_gizmo);
        } else {
            scene_structure_edit.placement_gizmo.origin = frame.origin;
            scene_structure_edit.placement_gizmo.axes = frame.parameter_axes;
        }
        track_distance_gizmo_frame(
            updated.track_key, updated.end_distance,
            scene_structure_edit.repeater_end_gizmo);
    }
    return true;
}

bool Canvas3D::Impl::scene_source_model_path_in_use(const std::string& path) const {
    if (path.empty()) return false;
    for (const Canvas3DModelInstance& instance : scene_data.instances) {
        if (instance.model_path == path) return true;
    }
    for (const Canvas3DSceneObject& object : scene_data.objects) {
        for (const Canvas3DSceneModelOption& option : object.model_options) {
            if (option.model_path == path) return true;
        }
    }
    for (const Canvas3DBackgroundChange& background : scene_data.backgrounds) {
        if (background.model_path == path) return true;
    }
    for (const Canvas3DRepeaterSegment& repeater : scene_data.repeaters) {
        if (std::find(repeater.model_paths.begin(), repeater.model_paths.end(), path) !=
            repeater.model_paths.end()) {
            return true;
        }
    }
    return false;
}

void Canvas3D::Impl::release_unused_put_between_preview_base_models() {
    for (auto it = scene_put_between_preview_base_model_keys.begin();
         it != scene_put_between_preview_base_model_keys.end();) {
        if (scene_source_model_path_in_use(*it)) {
            ++it;
            continue;
        }
        auto model = scene_models.find(*it);
        if (model != scene_models.end()) {
            release_scene_model(model->second);
            scene_models.erase(model);
        }
        it = scene_put_between_preview_base_model_keys.erase(it);
    }
}

void Canvas3D::Impl::clear_scene_placement_edit_target() {
    if (!scene_structure_edit.active) return;
    const std::string edit_id = scene_structure_edit.edit_id;
    Canvas3DModelInstance baseline = scene_structure_edit.baseline_instance;
    Canvas3DRepeaterSegment repeater_baseline = scene_structure_edit.baseline_repeater;
    const Canvas3DSceneEditKind kind = scene_structure_edit.kind;
    const Canvas3DPlacementEditTarget completed = scene_structure_edit.completed;
    const std::string preview_model_key = scene_structure_edit.preview_model_key;
    if (kind == Canvas3DSceneEditKind::StructurePutBetween) {
        stop_scene_put_between_preview_worker();
    }
    scene_structure_edit = SceneStructureEditState{};
    if (kind == Canvas3DSceneEditKind::Repeater) {
        write_scene_repeater_segment(edit_id, std::move(repeater_baseline));
    } else if (kind == Canvas3DSceneEditKind::Sound3D) {
        update_scene_sound3d_marker(
            edit_id, completed.distance, completed.x, completed.y);
    } else if (kind == Canvas3DSceneEditKind::StructurePutBetween) {
        std::string render_model_key =
            scene_model_key_for_instance(baseline, scene_geometry_generation);
        const Canvas3DPlacementEditTarget baseline_target =
            put_between_target_from_instance(edit_id, baseline);
        if (!preview_model_key.empty() &&
            same_placement_edit_target(completed, baseline_target)) {
            auto preview = scene_models.find(preview_model_key);
            if (preview != scene_models.end() && preview_model_key != render_model_key) {
                auto existing = scene_models.find(render_model_key);
                if (existing != scene_models.end()) {
                    release_scene_model(existing->second);
                    scene_models.erase(existing);
                }
                auto node = scene_models.extract(preview);
                node.key() = render_model_key;
                scene_models.insert(std::move(node));
            }
        }
        write_scene_put_between_instance(
            edit_id, std::move(baseline), render_model_key);
        if (!preview_model_key.empty() && preview_model_key != render_model_key) {
            auto preview = scene_models.find(preview_model_key);
            if (preview != scene_models.end()) {
                release_scene_model(preview->second);
                scene_models.erase(preview);
            }
        }
        release_unused_put_between_preview_base_models();
    } else {
        write_scene_placement_instance(edit_id, std::move(baseline));
    }
}

void Canvas3D::Impl::cancel_scene_gizmo_interaction(SceneGizmoTarget target) {
    if (scene_structure_edit.hovered_gizmo == target) {
        scene_structure_edit.hovered_gizmo = SceneGizmoTarget::None;
        scene_structure_edit.hovered_axis = Canvas3DSceneDragAxis::None;
    }
    if (scene_structure_edit.dragging_gizmo == target) {
        scene_structure_edit.dragging_gizmo = SceneGizmoTarget::None;
        scene_structure_edit.dragging_axis = Canvas3DSceneDragAxis::None;
    }
}

bool Canvas3D::Impl::set_scene_placement_edit_target(const Canvas3DPlacementEditTarget& target,
                                     bool show_gizmo) {
    if (!scene_active || target.edit_id.empty()) return false;
    if (target.kind != Canvas3DSceneEditKind::Structure &&
        target.kind != Canvas3DSceneEditKind::StructurePutBetween &&
        target.kind != Canvas3DSceneEditKind::Signal &&
        target.kind != Canvas3DSceneEditKind::Sound3D) {
        return false;
    }
    if (scene_structure_edit.active &&
        (scene_structure_edit.kind != target.kind ||
         scene_structure_edit.edit_id != target.edit_id)) {
        clear_scene_placement_edit_target();
    }
    if (target.kind == Canvas3DSceneEditKind::Sound3D) {
        if (!scene_structure_edit.active) {
            Canvas3DPlacementEditTarget baseline;
            if (!sound3d_marker_baseline_target(target, baseline)) return false;
            scene_structure_edit.active = true;
            scene_structure_edit.kind = Canvas3DSceneEditKind::Sound3D;
            scene_structure_edit.edit_id = target.edit_id;
            scene_structure_edit.completed = std::move(baseline);
        } else if (same_placement_edit_target(scene_structure_edit.current, target) &&
                   scene_structure_edit.show_gizmo == show_gizmo) {
            return true;
        }
        SceneGizmoHandle gizmo;
        if (!sound3d_gizmo_frame(target, gizmo) ||
            !update_scene_sound3d_marker(
                target.edit_id, target.distance, target.x, target.y)) {
            if (scene_structure_edit.current.edit_id.empty()) {
                scene_structure_edit = SceneStructureEditState{};
            }
            return false;
        }
        scene_structure_edit.current = target;
        scene_structure_edit.show_gizmo = show_gizmo;
        scene_structure_edit.placement_gizmo = gizmo;
        if (!show_gizmo) {
            cancel_scene_gizmo_interaction(SceneGizmoTarget::Placement);
        }
        return true;
    }
    scene_structure_edit.placement_gizmo.enabled =
        target.kind == Canvas3DSceneEditKind::StructurePutBetween ||
        target.placement_distance_gizmo
            ? std::array<bool, 3>{{false, false, true}}
            : std::array<bool, 3>{{true, true, true}};

    auto location_it = scene_placement_locations.find(target.edit_id);
    if (location_it == scene_placement_locations.end() ||
        location_it->second.source_index >= scene_data.instances.size()) {
        return false;
    }

    if (!scene_structure_edit.active) {
        scene_structure_edit.active = true;
        scene_structure_edit.kind = target.kind;
        scene_structure_edit.edit_id = target.edit_id;
        scene_structure_edit.baseline_instance =
            scene_data.instances[location_it->second.source_index];
        if (target.kind == Canvas3DSceneEditKind::StructurePutBetween) {
            if (!scene_structure_edit.baseline_instance.put_between) {
                scene_structure_edit = SceneStructureEditState{};
                return false;
            }
            scene_structure_edit.current = put_between_target_from_instance(
                target.edit_id, scene_structure_edit.baseline_instance);
            scene_structure_edit.completed = scene_structure_edit.current;
            if (!put_between_edit_frame(
                    scene_structure_edit.baseline_instance.distance,
                    scene_structure_edit.placement_gizmo.origin,
                    scene_structure_edit.placement_gizmo.axes)) {
                scene_structure_edit = SceneStructureEditState{};
                return false;
            }
        }
    } else if (same_placement_edit_target(scene_structure_edit.current, target) &&
               scene_structure_edit.show_gizmo == show_gizmo) {
        return true;
    }

    if (target.kind == Canvas3DSceneEditKind::StructurePutBetween) {
        if (target.model_path.empty()) return false;
        scene_structure_edit.show_gizmo = show_gizmo;
        if (same_placement_edit_target(scene_structure_edit.current, target)) {
            return true;
        }
        if (same_placement_edit_target(scene_structure_edit.completed, target)) {
            std::lock_guard<std::mutex> lock(scene_put_between_preview_mutex);
            const std::uint64_t sequence =
                ++scene_put_between_preview_next_sequence;
            scene_put_between_preview_latest_sequence = sequence;
            scene_put_between_preview_pending.reset();
            scene_put_between_preview_completed.reset();
            scene_structure_edit.current = target;
            scene_structure_edit.preview_sequence = sequence;
            return true;
        }
        const std::uint64_t sequence = queue_scene_put_between_preview(target);
        if (sequence == 0) return false;
        scene_structure_edit.current = target;
        scene_structure_edit.preview_sequence = sequence;
        if (!show_gizmo) {
            cancel_scene_gizmo_interaction(SceneGizmoTarget::Placement);
        }
        return true;
    }

    Canvas3DModelInstance desired = placement_instance_from_target(
        target, scene_structure_edit.baseline_instance);
    if (!write_scene_placement_instance(target.edit_id, std::move(desired))) {
        if (scene_structure_edit.current.edit_id.empty()) {
            scene_structure_edit = SceneStructureEditState{};
        }
        return false;
    }
    scene_structure_edit.current = target;
    scene_structure_edit.show_gizmo = show_gizmo;
    if (target.placement_distance_gizmo) {
        track_distance_gizmo_frame(
            target.track_key, target.distance,
            scene_structure_edit.placement_gizmo);
    }
    if (!show_gizmo) {
        cancel_scene_gizmo_interaction(SceneGizmoTarget::Placement);
    }
    return true;
}

bool Canvas3D::Impl::set_scene_repeater_edit_target(const Canvas3DPlacementEditTarget& input,
                                    bool show_gizmo) {
    if (!scene_active || input.edit_id.empty()) return false;
    Canvas3DPlacementEditTarget target = input;
    target.kind = Canvas3DSceneEditKind::Repeater;
    if (scene_structure_edit.active &&
        (scene_structure_edit.kind != Canvas3DSceneEditKind::Repeater ||
         scene_structure_edit.edit_id != target.edit_id)) {
        clear_scene_placement_edit_target();
    }
    scene_structure_edit.placement_gizmo.enabled = target.placement_distance_gizmo
        ? std::array<bool, 3>{{false, false, true}}
        : std::array<bool, 3>{{true, true, true}};
    scene_structure_edit.repeater_end_gizmo.enabled = {{false, false, true}};

    const auto location_it = scene_repeater_locations.find(target.edit_id);
    if (location_it == scene_repeater_locations.end() ||
        location_it->second >= scene_data.repeaters.size()) {
        return false;
    }
    const Canvas3DRepeaterSegment& current = scene_data.repeaters[location_it->second];
    if (current.model_paths.empty() || current.model_paths.front().empty()) return false;

    if (!scene_structure_edit.active) {
        scene_structure_edit.active = true;
        scene_structure_edit.kind = Canvas3DSceneEditKind::Repeater;
        scene_structure_edit.edit_id = target.edit_id;
        scene_structure_edit.baseline_repeater = current;
    } else if (same_placement_edit_target(scene_structure_edit.current, target) &&
               scene_structure_edit.show_gizmo == show_gizmo) {
        return true;
    }

    const Canvas3DRepeaterSegment desired = repeater_segment_from_target(
        target, scene_structure_edit.baseline_repeater);
    if (desired.track_key != scene_structure_edit.baseline_repeater.track_key ||
        (!target.placement_distance_gizmo &&
         desired.begin_distance != scene_structure_edit.baseline_repeater.begin_distance)) {
        clear_scene_placement_edit_target();
        return false;
    }
    if (!write_scene_repeater_segment(target.edit_id, desired)) {
        if (scene_structure_edit.current.edit_id.empty()) {
            scene_structure_edit = SceneStructureEditState{};
        }
        return false;
    }
    scene_structure_edit.current = target;
    scene_structure_edit.show_gizmo = show_gizmo;
    if (target.placement_distance_gizmo) {
        track_distance_gizmo_frame(
            target.track_key, target.distance,
            scene_structure_edit.placement_gizmo);
    }
    if (!show_gizmo) {
        cancel_scene_gizmo_interaction(SceneGizmoTarget::Placement);
    }
    if (!target.has_repeater_end_distance) {
        cancel_scene_gizmo_interaction(SceneGizmoTarget::RepeaterEndDistance);
    }
    return true;
}

bool Canvas3D::Impl::update_scene_placement_instance(const Canvas3DPlacementEditTarget& target) {
    if (!scene_active) return true;
    if (target.kind != Canvas3DSceneEditKind::Structure &&
        target.kind != Canvas3DSceneEditKind::StructurePutBetween &&
        target.kind != Canvas3DSceneEditKind::Signal) {
        return false;
    }
    auto location_it = scene_placement_locations.find(target.edit_id);
    if (location_it == scene_placement_locations.end() ||
        location_it->second.source_index >= scene_data.instances.size()) {
        return false;
    }
    if (target.kind == Canvas3DSceneEditKind::StructurePutBetween) {
        if (!scene_structure_edit.active ||
            scene_structure_edit.kind != Canvas3DSceneEditKind::StructurePutBetween ||
            scene_structure_edit.edit_id != target.edit_id ||
            !same_placement_edit_target(scene_structure_edit.completed, target)) {
            return false;
        }
        scene_structure_edit.baseline_instance =
            scene_data.instances[location_it->second.source_index];
        scene_structure_edit.current = target;
        return true;
    }
    const Canvas3DModelInstance& base = scene_structure_edit.active &&
        scene_structure_edit.edit_id == target.edit_id
        ? scene_structure_edit.baseline_instance
        : scene_data.instances[location_it->second.source_index];
    Canvas3DModelInstance desired = placement_instance_from_target(target, base);
    if (!write_scene_placement_instance(target.edit_id, std::move(desired))) return false;

    if (scene_structure_edit.active && scene_structure_edit.edit_id == target.edit_id) {
        auto updated_location = scene_placement_locations.find(target.edit_id);
        if (updated_location != scene_placement_locations.end() &&
            updated_location->second.source_index < scene_data.instances.size()) {
            scene_structure_edit.baseline_instance =
                scene_data.instances[updated_location->second.source_index];
        }
        scene_structure_edit.current = target;
    }
    return true;
}

bool Canvas3D::Impl::update_scene_repeater_segment(const Canvas3DPlacementEditTarget& input) {
    if (!scene_active) return true;
    Canvas3DPlacementEditTarget target = input;
    target.kind = Canvas3DSceneEditKind::Repeater;
    const auto location_it = scene_repeater_locations.find(target.edit_id);
    if (location_it == scene_repeater_locations.end() ||
        location_it->second >= scene_data.repeaters.size()) {
        return false;
    }
    const Canvas3DRepeaterSegment& base = scene_structure_edit.active &&
        scene_structure_edit.kind == Canvas3DSceneEditKind::Repeater &&
        scene_structure_edit.edit_id == target.edit_id
        ? scene_structure_edit.baseline_repeater
        : scene_data.repeaters[location_it->second];
    const Canvas3DRepeaterSegment desired = repeater_segment_from_target(target, base);
    if (desired.track_key != base.track_key || desired.begin_distance != base.begin_distance ||
        !write_scene_repeater_segment(target.edit_id, desired)) {
        return false;
    }
    if (scene_structure_edit.active &&
        scene_structure_edit.kind == Canvas3DSceneEditKind::Repeater &&
        scene_structure_edit.edit_id == target.edit_id) {
        const auto updated = scene_repeater_locations.find(target.edit_id);
        if (updated != scene_repeater_locations.end() &&
            updated->second < scene_data.repeaters.size()) {
            scene_structure_edit.baseline_repeater = scene_data.repeaters[updated->second];
        }
        scene_structure_edit.current = target;
    }
    return true;
}
