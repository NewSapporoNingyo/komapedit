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
#include "operation_timing.h"
#include "numeric_safety.h"
#include "scene_track_sampling.h"
#include "scene_frame_profile.h"
#include "imgui.h"
#include <d3d11.h>
#include <algorithm>
#include <cmath>
#include <map>
#include <limits>
#include <optional>
#include <string>
#include <utility>
#include <vector>

using namespace canvas3d_detail;

namespace canvas3d_detail {

constexpr size_t k_scene_chunk_count_limit = 100000;

} // namespace canvas3d_detail

namespace canvas3d_detail {

constexpr float k_scene_track_marker_alpha = 0.8f;

} // namespace canvas3d_detail

namespace canvas3d_detail {

constexpr float k_scene_track_marker_width = 0.5f;

} // namespace canvas3d_detail

namespace canvas3d_detail {

static int scene_tilt_flags(double tilt) {
    return kme::truncating_int_or_zero(tilt);
}
bool scene_repeater_has_interval(const Canvas3DRepeaterSegment& repeater) {
    return repeater.interval > 1e-9 && std::isfinite(repeater.interval);
}

static double scene_repeater_index_epsilon(const Canvas3DRepeaterSegment& repeater) {
    if (!scene_repeater_has_interval(repeater)) return k_scene_repeater_distance_epsilon;
    return std::min(k_scene_repeater_distance_epsilon, repeater.interval * 0.25);
}

bool scene_repeater_index_range(const Canvas3DRepeaterSegment& repeater,
                                double range_min,
                                double range_max,
                                SceneRepeaterIndexRange& out) {
    out = {};
    if (!scene_repeater_has_interval(repeater) || repeater.end_distance < repeater.begin_distance) return false;
    const double begin = std::max(range_min, repeater.begin_distance);
    const double end = std::min(range_max, repeater.end_distance);
    if (end < begin - k_scene_repeater_distance_epsilon) return false;

    const double eps = scene_repeater_index_epsilon(repeater);
    const double first_index_d = std::ceil((begin - repeater.begin_distance - eps) / repeater.interval);
    const double last_visible_index_d = std::floor((end - repeater.begin_distance + eps) / repeater.interval);
    const double last_before_end_index_d =
        std::ceil((repeater.end_distance - repeater.begin_distance - eps) / repeater.interval) - 1.0;
    if (!std::isfinite(first_index_d) ||
        !std::isfinite(last_visible_index_d) ||
        !std::isfinite(last_before_end_index_d)) {
        return false;
    }

    const double index_upper = std::ldexp(1.0, std::numeric_limits<long long>::digits);
    long long first_index = 0;
    if (first_index_d > 0.0) {
        if (first_index_d >= index_upper) return false;
        first_index = static_cast<long long>(first_index_d);
    }

    const double last_index_d = std::min(last_visible_index_d, last_before_end_index_d);
    if (last_index_d < 0.0) return false;
    if (last_index_d >= index_upper) {
        out.first = first_index;
        out.last = std::numeric_limits<long long>::max();
        return out.last >= out.first;
    }

    out.first = first_index;
    out.last = static_cast<long long>(last_index_d);
    return out.last >= out.first;
}

static size_t scene_repeater_index_count(const SceneRepeaterIndexRange& range) {
    if (range.last < range.first) return 0;
    double count = static_cast<double>(range.last - range.first) + 1.0;
    return static_cast<size_t>(std::min<double>(count, static_cast<double>(k_scene_repeater_instance_limit)));
}

size_t scene_repeater_instance_count(const Canvas3DRepeaterSegment& repeater) {
    if (repeater.model_paths.empty() || repeater.end_distance < repeater.begin_distance) return 0;
    if (!scene_repeater_has_interval(repeater)) return 1;

    SceneRepeaterIndexRange range;
    if (!scene_repeater_index_range(repeater, repeater.begin_distance, repeater.end_distance, range)) return 0;
    return scene_repeater_index_count(range);
}

bool scene_repeater_last_instance(const Canvas3DRepeaterSegment& repeater,
                                  double& distance,
                                  size_t& model_index) {
    if (repeater.model_paths.empty() || repeater.end_distance < repeater.begin_distance) return false;
    if (!scene_repeater_has_interval(repeater)) {
        distance = repeater.begin_distance;
        model_index = 0;
        return true;
    }

    SceneRepeaterIndexRange range;
    if (!scene_repeater_index_range(repeater, repeater.begin_distance, repeater.end_distance, range)) {
        return false;
    }
    distance = repeater.begin_distance + static_cast<double>(range.last) * repeater.interval;
    model_index = static_cast<size_t>(range.last) % repeater.model_paths.size();
    return true;
}

bool scene_repeater_render_distance_span(const Canvas3DRepeaterSegment& repeater,
                                         double& first_distance,
                                         double& last_distance) {
    size_t last_model_index = 0;
    if (!scene_repeater_last_instance(repeater, last_distance, last_model_index)) return false;
    first_distance = repeater.begin_distance;
    return true;
}
std::string scene_model_key(std::string key) {
    return ascii_lower(trim_ascii(key));
}
std::optional<Canvas3DTrackPoint> scene_sample_track_path_points(const Canvas3DTrackPath& path,
                                                                 double distance) {
    return scene_track_sampling::sample_track_path_points(path, distance);
}
const Canvas3DTrackPath* scene_own_track_path(const Canvas3DScene& scene) {
    for (const Canvas3DTrackPath& path : scene.tracks) {
        if (path.key == "own" || path.key.empty() || path.key == "0") return &path;
    }
    return scene.tracks.empty() ? nullptr : &scene.tracks.front();
}

const Canvas3DTrackPath* scene_other_track_path_for_key(const Canvas3DScene& scene,
                                                        const std::string& normalized_key) {
    const Canvas3DTrackPath* own = scene_own_track_path(scene);
    for (const Canvas3DTrackPath& path : scene.tracks) {
        if (&path == own) continue;
        if (normalize_track_lookup_key(path.key) == normalized_key) return &path;
    }
    return nullptr;
}

const Canvas3DTrackPath* scene_placement_track_path_for_key(const Canvas3DScene& scene,
                                                            const std::string& key) {
    const std::string normalized_key = normalize_track_lookup_key(key);
    if (is_own_track_placement_key(normalized_key)) return scene_own_track_path(scene);
    if (const Canvas3DTrackPath* other =
            scene_other_track_path_for_key(scene, normalized_key)) {
        return other;
    }
    return scene_own_track_path(scene);
}

static void append_scene_model_key_field(std::string& key, const std::string& value) {
    const size_t size = value.size();
    key.append(reinterpret_cast<const char*>(&size), sizeof(size));
    key.append(value);
}

std::string scene_model_key_for_instance(const Canvas3DModelInstance& instance,
                                         size_t geometry_generation) {
    if (!instance.put_between) return instance.model_path;

    std::string key(1, '\x1f');
    key.append("putbetween");
    append_scene_model_key_field(key, instance.model_path);
    append_scene_model_key_field(key, normalize_track_lookup_key(instance.put_between_track_key1));
    append_scene_model_key_field(key, normalize_track_lookup_key(instance.put_between_track_key2));
    key.append(reinterpret_cast<const char*>(&instance.distance), sizeof(instance.distance));
    const int flag = instance.put_between_flag & 1;
    key.append(reinterpret_cast<const char*>(&flag), sizeof(flag));
    key.append(reinterpret_cast<const char*>(&geometry_generation), sizeof(geometry_generation));
    return key;
}

std::string scene_put_between_preview_model_key(const std::string& edit_id,
                                                size_t geometry_generation) {
    std::string key(1, '\x1f');
    key.append("putbetween-preview");
    append_scene_model_key_field(key, edit_id);
    key.append(reinterpret_cast<const char*>(&geometry_generation),
               sizeof(geometry_generation));
    return key;
}

} // namespace canvas3d_detail

void Canvas3D::Impl::release_track_chunk(SceneTrackChunkGpu& chunk) {
    for (GpuMaterial& material : chunk.materials) {
        release_com(material.texture);
        material.has_texture = false;
        material.texture_has_alpha = false;
    }
    chunk.materials.clear();
    chunk.parts.clear();
    release_com(chunk.vertex_buffer);
    release_com(chunk.index_buffer);
    release_com(chunk.instance_buffer);
    chunk.instance_capacity = 0;
    chunk.index_count = 0;
}

void Canvas3D::Impl::release_scene_track_chunks() {
    for (SceneTrackChunkGpu& chunk : scene_track_chunks) release_track_chunk(chunk);
    scene_track_chunks.clear();
}

bool Canvas3D::Impl::build_scene_chunks(std::string& error) {
    kme::timing::GuiTiming::Stage edit_timing("scene.chunks");
    scene_chunks.clear();
    scene_cached_repeater_world_count = 0;
    scene_placement_locations.clear();
    scene_repeater_locations.clear();
    if (!std::isfinite(scene_chunk_m) || scene_chunk_m <= 0.0) {
        error = "3D scene chunk size is invalid";
        return false;
    }
    double min_d = scene_data.min_distance;
    double max_d = scene_data.max_distance;
    if (max_d <= min_d) {
        min_d = scene_camera_distance - scene_window_back_m;
        max_d = scene_camera_distance + scene_window_forward_m;
    }
    if (!std::isfinite(min_d) || !std::isfinite(max_d)) {
        error = "3D scene distance range is not finite";
        return false;
    }
    const double first = std::floor(min_d / scene_chunk_m) * scene_chunk_m;
    const double last = std::ceil(max_d / scene_chunk_m) * scene_chunk_m;
    const double count_value = std::max(1.0, (last - first) / scene_chunk_m);
    if (!std::isfinite(first) || !std::isfinite(last) ||
        !std::isfinite(count_value) ||
        count_value > static_cast<double>(k_scene_chunk_count_limit)) {
        error = "3D scene chunk count exceeds the supported limit of " +
            std::to_string(k_scene_chunk_count_limit);
        return false;
    }
    const size_t count = static_cast<size_t>(count_value);
    scene_chunks.resize(count);
    for (size_t i = 0; i < count; ++i) {
        scene_chunks[i].d_min = first + static_cast<double>(i) * scene_chunk_m;
        scene_chunks[i].d_max = scene_chunks[i].d_min + scene_chunk_m;
        Canvas3DTrackPoint origin_point;
        if (sample_own_track(scene_chunks[i].d_min, origin_point)) {
            scene_chunks[i].origin = {origin_point.x, origin_point.y, origin_point.z};
        }
    }
    for (size_t source_index = 0; source_index < scene_data.instances.size(); ++source_index) {
        const Canvas3DModelInstance& source = scene_data.instances[source_index];
        if (source.model_path.empty()) continue;
        const size_t index = scene_chunk_index_for_distance(source.distance);
        SceneInstance instance;
        instance.model_path = scene_model_key_for_instance(source, scene_geometry_generation);
        instance.distance = source.distance;
        instance.object_index = source.object_index;
        if (source.follow_track) {
            if (!make_track_world(source.track_key, source.distance,
                                  source.x, source.y, source.z,
                                  source.rx, source.ry, source.rz,
                                  source.tilt, source.span,
                                  instance.world)) {
                continue;
            }
        } else {
            std::copy(source.world, source.world + 16, instance.world);
        }
        SceneChunk& chunk = scene_chunks[index];
        const size_t chunk_instance_index = chunk.instances.size();
        chunk.instances.push_back(std::move(instance));
        if (scene_object_index_valid(source.object_index)) {
            const Canvas3DSceneObject& object =
                scene_data.objects[static_cast<size_t>(source.object_index)];
            const bool editable_placement =
                object.kind == Canvas3DSceneObjectKind::Structure ||
                object.kind == Canvas3DSceneObjectKind::Signal;
            if (editable_placement && !object.edit_id.empty()) {
                scene_placement_locations[object.edit_id] = ScenePlacementInstanceLocation{
                    source_index,
                    index,
                    chunk_instance_index
                };
            }
        }
    }
    for (size_t repeater_index = 0; repeater_index < scene_data.repeaters.size(); ++repeater_index) {
        const Canvas3DRepeaterSegment& repeater = scene_data.repeaters[repeater_index];
        if (!repeater.edit_id.empty()) {
            scene_repeater_locations[repeater.edit_id] = repeater_index;
        }
        update_scene_repeater_chunk_membership(
            repeater_index, std::nullopt,
            scene_repeater_chunk_range(repeater));
    }
    return true;
}

void Canvas3D::Impl::append_track_quad(std::vector<GpuVertex>& vertices,
                              std::vector<unsigned int>& indices,
                              Vec3 a, Vec3 b, Vec3 side0, Vec3 side1, float half_width) {
    if (vertices.size() > static_cast<size_t>(std::numeric_limits<unsigned int>::max() - 4)) return;
    Vec3 n = {0.0f, 1.0f, 0.0f};
    unsigned int base = static_cast<unsigned int>(vertices.size());
    Vec3 a0 = a - side0 * half_width;
    Vec3 a1 = a + side0 * half_width;
    Vec3 b0 = b - side1 * half_width;
    Vec3 b1 = b + side1 * half_width;
    vertices.push_back({a0.x, a0.y, a0.z, n.x, n.y, n.z, 0.0f, 0.0f});
    vertices.push_back({a1.x, a1.y, a1.z, n.x, n.y, n.z, 1.0f, 0.0f});
    vertices.push_back({b1.x, b1.y, b1.z, n.x, n.y, n.z, 1.0f, 1.0f});
    vertices.push_back({b0.x, b0.y, b0.z, n.x, n.y, n.z, 0.0f, 1.0f});
    indices.insert(indices.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
}

Vec3 Canvas3D::Impl::vec3_from_dvec3(DVec3 v) {
    return {static_cast<float>(v.x), static_cast<float>(v.y), static_cast<float>(v.z)};
}

void Canvas3D::Impl::track_marker_frame(const Canvas3DTrackPoint& point, Vec3& right, Vec3& up) {
    DVec3 right_d = right_from_theta_d(point.theta);
    DVec3 forward_d = forward_from_theta_d(point.theta);
    DVec3 up_d = cross(right_d, forward_d);
    apply_track_cant(right_d, up_d, forward_d, point.cant_angle);
    right = vec3_from_dvec3(normalize(right_d));
    up = vec3_from_dvec3(normalize(up_d));
}

void Canvas3D::Impl::append_track_segment(std::vector<GpuVertex>& vertices,
                                 std::vector<unsigned int>& indices,
                                 DVec3 origin,
                                 const Canvas3DTrackPoint& p0,
                                 const Canvas3DTrackPoint& p1) {
    constexpr float marker_lift = 0.035f;
    constexpr float marker_half_width = k_scene_track_marker_width * 0.5f;
    Vec3 right0;
    Vec3 right1;
    Vec3 up0;
    Vec3 up1;
    track_marker_frame(p0, right0, up0);
    track_marker_frame(p1, right1, up1);
    Vec3 center0{static_cast<float>(p0.x - origin.x),
                 static_cast<float>(p0.y - origin.y),
                 static_cast<float>(p0.z - origin.z)};
    Vec3 center1{static_cast<float>(p1.x - origin.x),
                 static_cast<float>(p1.y - origin.y),
                 static_cast<float>(p1.z - origin.z)};
    center0 = center0 + up0 * marker_lift;
    center1 = center1 + up1 * marker_lift;
    append_track_quad(vertices, indices, center0, center1, right0, right1, marker_half_width);
}

bool Canvas3D::Impl::upload_track_chunk(SceneTrackChunkGpu& chunk,
                        const std::vector<GpuVertex>& vertices,
                        const std::vector<unsigned int>& indices,
                        std::string& error) {
    if (vertices.empty() || indices.empty()) return true;
    if (vertices.size() > static_cast<size_t>(std::numeric_limits<UINT>::max() / sizeof(GpuVertex)) ||
        indices.size() > static_cast<size_t>(std::numeric_limits<UINT>::max() / sizeof(unsigned int))) {
        error = "track chunk is too large for a Direct3D 11 buffer";
        return false;
    }
    D3D11_BUFFER_DESC vb_desc = {};
    vb_desc.ByteWidth = static_cast<UINT>(vertices.size() * sizeof(GpuVertex));
    vb_desc.Usage = D3D11_USAGE_DEFAULT;
    vb_desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    D3D11_SUBRESOURCE_DATA vb_data = {};
    vb_data.pSysMem = vertices.data();
    HRESULT hr = device->CreateBuffer(&vb_desc, &vb_data, &chunk.vertex_buffer);
    if (FAILED(hr)) {
        error = hresult_text("CreateBuffer(track vertex)", hr);
        return false;
    }

    D3D11_BUFFER_DESC ib_desc = {};
    ib_desc.ByteWidth = static_cast<UINT>(indices.size() * sizeof(unsigned int));
    ib_desc.Usage = D3D11_USAGE_DEFAULT;
    ib_desc.BindFlags = D3D11_BIND_INDEX_BUFFER;
    D3D11_SUBRESOURCE_DATA ib_data = {};
    ib_data.pSysMem = indices.data();
    hr = device->CreateBuffer(&ib_desc, &ib_data, &chunk.index_buffer);
    if (FAILED(hr)) {
        error = hresult_text("CreateBuffer(track index)", hr);
        return false;
    }

    chunk.index_count = static_cast<UINT>(indices.size());
    return true;
}

bool Canvas3D::Impl::build_scene_track_chunks(std::string& error) {
    scene_track_chunks.clear();
    scene_track_chunks.resize(scene_chunks.size());
    std::vector<bool> ordered_tracks;
    ordered_tracks.reserve(scene_data.tracks.size());
    for (const Canvas3DTrackPath& track : scene_data.tracks) {
        ordered_tracks.push_back(scene_track_sampling::has_ordered_finite_distances(track));
    }
    for (size_t chunk_index = 0; chunk_index < scene_chunks.size(); ++chunk_index) {
        const SceneChunk& scene_chunk = scene_chunks[chunk_index];
        SceneTrackChunkGpu& gpu_chunk = scene_track_chunks[chunk_index];
        gpu_chunk.d_min = scene_chunk.d_min;
        gpu_chunk.d_max = scene_chunk.d_max;
        gpu_chunk.origin = scene_chunk.origin;
        std::vector<GpuVertex> vertices;
        std::vector<unsigned int> indices;

        for (size_t track_index = 0; track_index < scene_data.tracks.size(); ++track_index) {
            const Canvas3DTrackPath& track = scene_data.tracks[track_index];
            if (!track.visible || track.points.size() < 2) continue;
            size_t part_start = indices.size();
            UINT material_index = static_cast<UINT>(gpu_chunk.materials.size());
            GpuMaterial material;
            material.diffuse[0] = clamp_color_component(track.color.x);
            material.diffuse[1] = clamp_color_component(track.color.y);
            material.diffuse[2] = clamp_color_component(track.color.z);
            material.diffuse[3] = k_scene_track_marker_alpha;
            gpu_chunk.materials.push_back(material);

            const auto range = scene_track_sampling::chunk_segment_range(
                track, scene_chunk.d_min, scene_chunk.d_max, ordered_tracks[track_index]);
            for (size_t i = range.first; i < range.end; ++i) {
                const Canvas3DTrackPoint& a = track.points[i - 1];
                const Canvas3DTrackPoint& b = track.points[i];
                if (b.distance < scene_chunk.d_min || a.distance > scene_chunk.d_max) continue;
                append_track_segment(vertices, indices, gpu_chunk.origin, a, b);
            }

            size_t part_count = indices.size() - part_start;
            if (part_count > 0) {
                gpu_chunk.parts.push_back({static_cast<UINT>(part_start), static_cast<UINT>(part_count), material_index});
            } else {
                gpu_chunk.materials.pop_back();
            }
        }

        if (!upload_track_chunk(gpu_chunk, vertices, indices, error)) return false;
    }
    return true;
}

bool Canvas3D::Impl::make_track_placement_frame(const std::string& track_key,
                                double distance,
                                double x,
                                double y,
                                double z,
                                double rx,
                                double ry,
                                double rz,
                                double tilt,
                                double span,
                                StructurePlacementFrame& frame) const {
    const Canvas3DTrackPath* path = placement_track_path_for_key(track_key);
    if (!path) return false;
    const auto sample = [&](double sample_distance, Canvas3DTrackPoint& output) {
#ifndef NDEBUG
        if (debug_scene_reference) {
            const auto* original_path = scene_placement_track_path_for_key(scene_data, track_key);
            return original_path && sample_track_path(*original_path, sample_distance, output);
        }
#endif
        return sample_track_path(*path, sample_distance, output);
    };
    Canvas3DTrackPoint point;
    if (!sample(distance, point)) return false;

    const int flags = scene_tilt_flags(tilt);
    const bool follow_gradient = (flags & 1) != 0;
    const bool follow_cant = (flags & 2) != 0;

    DVec3 right = right_from_theta_d(point.theta);
    DVec3 forward = forward_from_theta_d(point.theta);
    DVec3 up = cross(right, forward);
    DVec3 offset_right = right;
    DVec3 offset_up = up;
    if (follow_cant) apply_track_cant(offset_right, offset_up, forward, point.cant_angle);

    DVec3 origin{point.x, point.y, point.z};
    origin = origin + offset_right * x + offset_up * y;

    double effective_span = std::isfinite(span) && span >= 1.0 ? span : 1.0;
    Canvas3DTrackPoint span_point;
    if (sample(distance + effective_span, span_point)) {
        DVec3 span_right = right_from_theta_d(span_point.theta);
        DVec3 span_forward = forward_from_theta_d(span_point.theta);
        DVec3 span_up = cross(span_right, span_forward);
        DVec3 span_offset_right = span_right;
        DVec3 span_offset_up = span_up;
        if (follow_cant) apply_track_cant(span_offset_right, span_offset_up, span_forward, span_point.cant_angle);

        DVec3 next{span_point.x, span_point.y, span_point.z};
        next = next + span_offset_right * x + span_offset_up * y;
        if (!follow_gradient) next.y = origin.y;
        DVec3 span_forward_vec = next - origin;
        if (dot(span_forward_vec, span_forward_vec) > 1e-12) {
            forward = normalize(span_forward_vec);
            right = normalize(cross(forward, {0.0, 1.0, 0.0}));
            up = cross(right, forward);
        }
    }

    if (follow_cant) {
        double cant_angle = point.cant_angle;
        Canvas3DTrackPoint mid_point;
        if (sample(distance + effective_span * 0.5, mid_point)) {
            cant_angle = mid_point.cant_angle;
        }
        apply_track_cant(right, up, forward, cant_angle);
    }

    origin = origin + forward * z;
    frame.origin = origin;
    frame.parameter_axes[0] = normalize(offset_right);
    frame.parameter_axes[1] = normalize(offset_up);
    frame.parameter_axes[2] = normalize(forward);
    apply_euler(right, up, forward, rx, ry, rz);
    frame.model_right = right;
    frame.model_up = up;
    frame.model_forward = forward;
    return true;
}

bool Canvas3D::Impl::make_track_world(const std::string& track_key,
                      double distance,
                      double x,
                      double y,
                      double z,
                      double rx,
                      double ry,
                      double rz,
                      double tilt,
                      double span,
                      double out_world[16]) const {
    StructurePlacementFrame frame;
    if (!make_track_placement_frame(track_key, distance, x, y, z,
                                    rx, ry, rz, tilt, span, frame)) {
        return false;
    }
    store_world(out_world, frame.model_right, frame.model_up,
                frame.model_forward, frame.origin);
    return true;
}

bool Canvas3D::Impl::make_repeater_instance_world(const Canvas3DRepeaterSegment& repeater,
                                  double distance,
                                  double out_world[16]) const {
    return make_track_world(repeater.track_key, distance,
                            repeater.x, repeater.y, repeater.z,
                            repeater.rx, repeater.ry, repeater.rz,
                            repeater.tilt, repeater.span,
                            out_world);
}

void Canvas3D::Impl::append_visible_model_instance(const std::string& model_path,
                                   const SceneInstanceData& data,
                                   int object_index,
                                   const SceneScreenBounds* bounds,
                                   std::map<std::string, std::vector<SceneInstanceData>>& visible_instances,
                                   std::map<int, std::vector<SceneVisibleInstanceRef>>* object_refs) const {
    auto instance_it = visible_instances.try_emplace(model_path).first;
    std::vector<SceneInstanceData>& instances = instance_it->second;
    const size_t instance_index = instances.size();
    instances.push_back(data);
    if (object_refs && object_index >= 0 && bounds) {
        (*object_refs)[object_index].push_back(SceneVisibleInstanceRef{
            &instance_it->first,
            instance_index,
            bounds->screen_min,
            bounds->screen_max
        });
    }
}

void Canvas3D::Impl::include_scene_screen_bounds(ImVec2& screen_min,
                                        ImVec2& screen_max,
                                        const SceneVisibleInstanceRef& ref) {
    screen_min.x = std::min(screen_min.x, ref.screen_min.x);
    screen_min.y = std::min(screen_min.y, ref.screen_min.y);
    screen_max.x = std::max(screen_max.x, ref.screen_max.x);
    screen_max.y = std::max(screen_max.y, ref.screen_max.y);
}

void Canvas3D::Impl::invalidate_scene_repeater_cache(SceneChunk& chunk) {
    scene_cached_repeater_world_count -= chunk.cached_world_count;
    chunk.cached_world_count = 0;
    chunk.repeater_cache_prepared = false;
    std::vector<SceneChunk::RepeaterCache>().swap(chunk.repeater_cache);
}

void Canvas3D::Impl::prepare_scene_repeater_cache(SceneChunk& chunk) {
    if (chunk.repeater_cache_prepared) return;
    chunk.repeater_cache_prepared = true;
    chunk.repeater_cache.resize(chunk.repeater_indices.size());
    double chunk_max = chunk.d_max;
    if (chunk.d_max < scene_data.max_distance) chunk_max -= k_scene_repeater_distance_epsilon;
    for (size_t slot = 0; slot < chunk.repeater_indices.size(); ++slot) {
        const size_t index = chunk.repeater_indices[slot];
        if (index >= scene_data.repeaters.size()) continue;
        const auto& repeater = scene_data.repeaters[index];
        if (repeater.model_paths.empty() || repeater.end_distance < repeater.begin_distance) continue;
        SceneRepeaterIndexRange range{0, 0};
        if (scene_repeater_has_interval(repeater)) {
            if (!scene_repeater_index_range(repeater, chunk.d_min, chunk_max, range)) continue;
        } else if (repeater.begin_distance < chunk.d_min - k_scene_repeater_distance_epsilon ||
                   repeater.begin_distance > chunk_max + k_scene_repeater_distance_epsilon) {
            continue;
        }
        const size_t remaining = k_scene_repeater_cache_instance_limit - scene_cached_repeater_world_count;
        // Compare before adding one: the index range may end at LLONG_MAX.
        if (static_cast<unsigned long long>(range.last - range.first) >= remaining) continue;
        const size_t count = static_cast<size_t>(range.last - range.first) + 1;
        auto& cache = chunk.repeater_cache[slot];
        cache.first_index = range.first;
        cache.worlds.resize(count);
        for (size_t offset = 0; offset < count; ++offset) {
            const long long instance_index = range.first + static_cast<long long>(offset);
            const double distance = scene_repeater_has_interval(repeater)
                ? repeater.begin_distance + static_cast<double>(instance_index) * repeater.interval
                : repeater.begin_distance;
            auto& world = cache.worlds[offset];
            world.valid = make_repeater_instance_world(repeater, distance, world.world.data());
        }
        chunk.cached_world_count += count;
        scene_cached_repeater_world_count += count;
    }
}

void Canvas3D::Impl::append_visible_repeater_instances(SceneChunk& chunk,
                                       double visible_min,
                                       double visible_max,
                                       DVec3 render_origin,
                                       const Mat4& view_proj,
                                       int width,
                                       int height,
                                       bool can_pick,
                                       std::map<std::string, std::vector<SceneInstanceData>>& visible_instances,
                                       std::map<int, std::vector<SceneVisibleInstanceRef>>* object_refs) {
    KME_SCENE_PROFILE(Repeaters);
    bool use_cache = true;
#ifndef NDEBUG
    use_cache = !debug_scene_reference;
#endif
    if (use_cache) prepare_scene_repeater_cache(chunk);
    double chunk_max = chunk.d_max;
    if (chunk.d_max < scene_data.max_distance) chunk_max -= k_scene_repeater_distance_epsilon;
    double range_min = std::max(visible_min, chunk.d_min);
    double range_max = std::min(visible_max, chunk_max);
    if (range_max < range_min) return;

    for (size_t repeater_slot = 0; repeater_slot < chunk.repeater_indices.size(); ++repeater_slot) {
        const size_t repeater_index = chunk.repeater_indices[repeater_slot];
        if (repeater_index >= scene_data.repeaters.size()) continue;
        const Canvas3DRepeaterSegment& repeater = scene_data.repeaters[repeater_index];
        if (repeater.model_paths.empty() || repeater.end_distance < repeater.begin_distance) continue;

        const double begin = std::max(range_min, repeater.begin_distance);
        const double end = std::min(range_max, repeater.end_distance);
        if (end < begin - k_scene_repeater_distance_epsilon) continue;

        auto emit = [&](double distance, size_t model_index) {
            const std::string& path = repeater.model_paths[model_index % repeater.model_paths.size()];
            if (path.empty()) return;
            double computed_world[16] = {};
            const double* world = computed_world;
            const auto* cache = use_cache ? &chunk.repeater_cache[repeater_slot] : nullptr;
            const size_t offset = cache ? model_index - static_cast<size_t>(cache->first_index) : 0;
            if (cache && offset < cache->worlds.size()) {
                if (!cache->worlds[offset].valid) return;
                world = cache->worlds[offset].world.data();
            } else if (!make_repeater_instance_world(repeater, distance, computed_world)) return;
#ifndef NDEBUG
            debug_record_scene_world(path, repeater.object_index, distance, world);
#endif
            SceneInstanceData data = make_instance_data_relative(world, render_origin);

            SceneScreenBounds bounds;
            SceneScreenBounds* bounds_ptr = nullptr;
            if (can_pick && scene_object_index_valid(repeater.object_index)) {
                auto model_it = scene_models.find(path);
                if (model_it != scene_models.end() &&
                    compute_scene_instance_screen_bounds(world, model_it->second, render_origin, view_proj,
                                                         width, height, bounds)) {
                    bounds_ptr = &bounds;
                }
            }
            append_visible_model_instance(path, data, repeater.object_index, bounds_ptr,
                                          visible_instances, object_refs);
        };

        if (!scene_repeater_has_interval(repeater)) {
            if (repeater.begin_distance >= begin - k_scene_repeater_distance_epsilon &&
                repeater.begin_distance <= end + k_scene_repeater_distance_epsilon) {
                emit(repeater.begin_distance, 0);
            }
            continue;
        }

        SceneRepeaterIndexRange range;
        if (!scene_repeater_index_range(repeater, begin, end, range)) continue;
        long long emitted = 0;
        const double end_epsilon = scene_repeater_index_epsilon(repeater);
        visit_scene_repeater_indices(range, [&](long long index) {
            double distance = repeater.begin_distance + static_cast<double>(index) * repeater.interval;
            if (distance < begin - k_scene_repeater_distance_epsilon) return true;
            if (distance > end + k_scene_repeater_distance_epsilon) return false;
            if (distance >= repeater.end_distance - end_epsilon) return false;
            emit(distance, static_cast<size_t>(index));
            return ++emitted < k_scene_repeater_instance_limit;
        });
    }
}
