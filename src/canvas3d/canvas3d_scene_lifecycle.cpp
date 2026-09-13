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
#include "operation_timing.h"
#include "scene_track_sampling.h"
#include "scene_frame_profile.h"
#include <d3d11.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <map>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

using namespace canvas3d_detail;

Canvas3D::Impl::Impl(ID3D11Device* device, Canvas3DWakeCallback wake_callback)
    : wake_callback(wake_callback), device(device) {
    if (device) {
        device->AddRef();
        device->GetImmediateContext(&context);
    }
}

Canvas3D::Impl::~Impl() {
    stop_scene_put_between_preview_worker();
    stop_scene_loader();
    release_scene_resources();
    release_scene_mileage_highlight_resources();
    release_resources();
    release_render_target();
    release_com(scene_input_layout);
    release_com(scene_vertex_shader);
    release_com(scene_pixel_shader);
    release_com(scene_fog_pixel_shader);
    release_com(scene_constant_buffer);
    release_com(scene_marker_input_layout);
    release_com(scene_marker_vertex_shader);
    release_com(scene_marker_pixel_shader);
    release_com(scene_marker_pick_pixel_shader);
    release_com(scene_marker_mask_pixel_shader);
    release_com(scene_marker_constant_buffer);
    release_com(scene_marker_pick_constant_buffer);
    release_com(scene_depth_state);
    release_com(scene_depth_read_state);
    release_com(blend_state);
    release_com(alpha_mask_rasterizer_state);
    release_com(track_rasterizer_state);
    release_com(rasterizer_state);
    release_com(scene_outline_sampler_state);
    release_com(scene_outline_constant_buffer);
    release_com(scene_outline_pixel_shader);
    release_com(scene_outline_vertex_shader);
    release_com(scene_pick_constant_buffer);
    release_com(scene_pick_pixel_shader);
    release_com(sampler_state);
    release_com(context);
    release_com(device);
}

bool Canvas3D::Impl::load_scene(Canvas3DScene scene, std::string& error, bool preserve_loaded_models, bool preserve_camera) {
    if (!device || !context) {
        error = "Direct3D device is not available";
        return false;
    }

    SceneCameraState camera_state;
    if (preserve_camera && scene_active) {
        camera_state.valid = true;
        camera_state.pos = scene_camera_pos;
        camera_state.yaw = scene_camera_yaw;
        camera_state.pitch = scene_camera_pitch;
        camera_state.distance = scene_camera_distance;
    }

    stop_scene_put_between_preview_worker();
    stop_scene_loader();
    if (preserve_loaded_models) {
        release_scene_track_chunks();
        release_scene_marker_chunks();
        scene_chunks.clear();
        clear_pending_scene_model_uploads();
        scene_last_error.clear();
        scene_stats_value = {};
        scene_model_worker_count_value.store(0);
        scene_load_summary_pending = false;
    } else {
        release_scene_resources();
    }
    scene_structure_edit = SceneStructureEditState{};
    scene_placement_locations.clear();
    scene_repeater_locations.clear();
    scene_data = std::move(scene);
    scene_placement_tracks.rebuild(scene_data);
    if (++scene_geometry_generation == 0) ++scene_geometry_generation;
    clear_scene_focus_highlight();
    std::sort(scene_data.backgrounds.begin(), scene_data.backgrounds.end(),
              [](const Canvas3DBackgroundChange& a, const Canvas3DBackgroundChange& b) {
                  return a.distance < b.distance;
              });
    if (scene_data.min_distance > scene_data.max_distance) {
        std::swap(scene_data.min_distance, scene_data.max_distance);
    }

    if (camera_state.valid) {
        scene_camera_pos = camera_state.pos;
        scene_camera_yaw = camera_state.yaw;
        scene_camera_pitch = camera_state.pitch;
        scene_camera_distance = scene_track_sampling::clamp_camera_distance(scene_data, camera_state.distance);
        reset_scene_camera_tracking();
        scene_camera_pitch = camera_state.pitch;
    } else {
        scene_camera_pos = {scene_data.camera.x, scene_data.camera.y, scene_data.camera.z};
        scene_camera_yaw = static_cast<float>(scene_data.camera.yaw);
        scene_camera_pitch = static_cast<float>(scene_data.camera.pitch);
        scene_camera_distance = scene_track_sampling::clamp_camera_distance(scene_data, scene_data.camera.distance);
        reset_scene_camera_tracking();
    }
    scene_active = true;
    rebuild_scene_mileage_pick_cache();

    const auto track_gpu_setup_started_at = std::chrono::steady_clock::now();
    if (!build_scene_chunks(error)) {
        clear_scene();
        return false;
    }
    if (!build_scene_track_chunks(error)) {
        clear_scene();
        return false;
    }
    if (!build_scene_marker_chunks(error)) {
        clear_scene();
        return false;
    }
    scene_stats_value.track_gpu_setup_seconds = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - track_gpu_setup_started_at).count();

    const auto model_queue_started_at = std::chrono::steady_clock::now();
    std::map<std::string, SceneModelLoadRequest> requests = collect_scene_model_load_requests();
    std::vector<SceneModelLoadRequest> requests_to_load;
    requests_to_load.reserve(requests.size());
    if (preserve_loaded_models) {
        for (auto it = scene_models.begin(); it != scene_models.end();) {
            if (requests.find(it->first) == requests.end()) {
                release_scene_model(it->second);
                it = scene_models.erase(it);
            } else {
                ++it;
            }
        }
        for (const auto& entry : requests) {
            auto [it, inserted] = scene_models.try_emplace(entry.first);
            if (inserted || it->second.state == SceneModelGpu::State::Pending) {
                release_scene_model(it->second);
                it->second = SceneModelGpu{};
                requests_to_load.push_back(entry.second);
            }
        }
    } else {
        for (const auto& entry : requests) {
            scene_models[entry.first] = SceneModelGpu{};
            requests_to_load.push_back(entry.second);
        }
    }
    scene_stats_value.model_path_count = requests.size();
    scene_stats_value.instance_count = count_scene_instances();
    scene_stats_value.chunk_count = scene_chunks.size();
    scene_stats_value.window_back_m = scene_window_back_m;
    scene_stats_value.window_forward_m = scene_window_forward_m;
    scene_stats_value.camera_distance = scene_camera_distance;
    reset_scene_fps_counter();
    start_scene_model_worker(std::move(requests_to_load));
    scene_stats_value.model_queue_seconds = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - model_queue_started_at).count();
    return true;
}

std::vector<SceneModelLoadRequest> Canvas3D::Impl::reconcile_dynamic_scene_model_requests(
    const std::map<std::string, SceneModelLoadRequest>& requests) {
    const bool request_set_changed = requests.size() != scene_models.size() ||
        std::any_of(scene_models.begin(), scene_models.end(),
                    [&](const auto& entry) {
                        return requests.find(entry.first) == requests.end();
                    });
    if (request_set_changed && scene_worker.joinable()) {
        stop_scene_loader();
        clear_pending_scene_model_uploads();
    }

    std::vector<SceneModelLoadRequest> requests_to_load;
    requests_to_load.reserve(requests.size());
    for (auto it = scene_models.begin(); it != scene_models.end();) {
        if (requests.find(it->first) == requests.end()) {
            release_scene_model(it->second);
            it = scene_models.erase(it);
        } else {
            ++it;
        }
    }
    for (const auto& entry : requests) {
        auto [it, inserted] = scene_models.try_emplace(entry.first);
        if (inserted || it->second.state == SceneModelGpu::State::Pending) {
            release_scene_model(it->second);
            it->second = SceneModelGpu{};
            requests_to_load.push_back(entry.second);
        }
    }
    return requests_to_load;
}

bool Canvas3D::Impl::refresh_scene_dynamic_content(const MapModel& model, int station_index, std::string& error) {
    kme::timing::GuiTiming::Stage edit_timing("scene.dynamic");
    error.clear();
    if (!scene_active) return true;
    if (!device || !context) {
        error = "Direct3D device is not available";
        return false;
    }
    if (scene_data.tracks.empty() || scene_chunks.empty() || scene_track_chunks.empty()) {
        error = "3D scene preview has no reusable track geometry";
        return false;
    }

    stop_scene_put_between_preview_worker();
    clear_scene_placement_edit_target();

    std::vector<Canvas3DSceneObject> old_objects = std::move(scene_data.objects);
    std::vector<Canvas3DModelInstance> old_instances = std::move(scene_data.instances);
    std::vector<Canvas3DRepeaterSegment> old_repeaters = std::move(scene_data.repeaters);
    std::vector<Canvas3DBackgroundChange> old_backgrounds = std::move(scene_data.backgrounds);
    Canvas3DCameraStart old_camera = scene_data.camera;
    const double old_min_distance = scene_data.min_distance;
    const double old_max_distance = scene_data.max_distance;
    std::vector<SceneChunk> old_chunks = std::move(scene_chunks);
    auto old_structure_locations = scene_placement_locations;
    auto old_repeater_locations = scene_repeater_locations;

    auto restore_dynamic_content = [&]() {
        scene_data.objects = std::move(old_objects);
        scene_data.instances = std::move(old_instances);
        scene_data.repeaters = std::move(old_repeaters);
        scene_data.backgrounds = std::move(old_backgrounds);
        scene_data.camera = old_camera;
        scene_data.min_distance = old_min_distance;
        scene_data.max_distance = old_max_distance;
        scene_chunks = std::move(old_chunks);
        scene_placement_locations = std::move(old_structure_locations);
        scene_repeater_locations = std::move(old_repeater_locations);
    };

    if (!populate_canvas3d_scene_dynamic_content(scene_data, model, station_index)) {
        restore_dynamic_content();
        error = "failed to rebuild 3D scene dynamic content";
        return false;
    }
    scene_data.camera = old_camera;
    std::sort(scene_data.backgrounds.begin(), scene_data.backgrounds.end(),
              [](const Canvas3DBackgroundChange& a, const Canvas3DBackgroundChange& b) {
                  return a.distance < b.distance;
              });
    if (scene_data.min_distance > scene_data.max_distance) {
        std::swap(scene_data.min_distance, scene_data.max_distance);
    }

    if (!build_scene_chunks(error)) {
        restore_dynamic_content();
        return false;
    }
    auto same_chunk_signature = [](const std::vector<SceneChunk>& a,
                                   const std::vector<SceneChunk>& b) {
        if (a.size() != b.size()) return false;
        for (size_t i = 0; i < a.size(); ++i) {
            if (std::abs(a[i].d_min - b[i].d_min) > 1e-6 ||
                std::abs(a[i].d_max - b[i].d_max) > 1e-6 ||
                std::abs(a[i].origin.x - b[i].origin.x) > 1e-6 ||
                std::abs(a[i].origin.y - b[i].origin.y) > 1e-6 ||
                std::abs(a[i].origin.z - b[i].origin.z) > 1e-6) {
                return false;
            }
        }
        return true;
    };
    if (!same_chunk_signature(scene_chunks, old_chunks) ||
        scene_track_chunks.size() != scene_chunks.size()) {
        restore_dynamic_content();
        error = "3D scene preview chunk layout changed";
        return false;
    }

    clear_scene_focus_highlight();
    scene_hovered_object_index = -1;
    scene_hovered_marker_index = -1;
    scene_context_object_index = -1;
    scene_context_marker_index = -1;
    scene_hover_highlight_batch.clear();

    std::map<std::string, SceneModelLoadRequest> requests = collect_scene_model_load_requests();
    std::vector<SceneModelLoadRequest> requests_to_load =
        reconcile_dynamic_scene_model_requests(requests);

    scene_stats_value.model_path_count = requests.size();
    scene_stats_value.instance_count = count_scene_instances();
    scene_stats_value.chunk_count = scene_chunks.size();
    scene_stats_value.window_back_m = scene_window_back_m;
    scene_stats_value.window_forward_m = scene_window_forward_m;
    scene_stats_value.camera_distance = scene_camera_distance;
    reset_scene_fps_counter();
    start_scene_model_worker(std::move(requests_to_load));
    return true;
}

bool Canvas3D::Impl::refresh_scene_map_content(const MapModel& model,
                               const Canvas3DSceneMapRefreshOptions& options,
                               std::string& error) {
    error.clear();
    if (!scene_active) return true;
    if (options.route_stations) {
        populate_canvas3d_scene_route_stations(scene_data.route_info, model);
    }
    if (options.fog) populate_canvas3d_scene_fog(scene_data, model);
    if (options.draw_distances) {
        populate_canvas3d_scene_draw_distances(scene_data, model);
    }
    if (options.speed_limits) {
        populate_canvas3d_scene_speed_limits(scene_data.route_info, model);
    }
    if (options.section_signals) {
        populate_canvas3d_scene_section_signals(scene_data.route_info, model);
    }
    if (options.markers) {
        // Preview-to-Edit metadata merges can supply event provenance after
        // the scene was built. Refresh the shared events before their boards.
        populate_canvas3d_scene_route_values(scene_data.route_info, model);
        populate_canvas3d_scene_markers(scene_data, model);
        release_scene_marker_chunks();
        if (!build_scene_marker_chunks(error)) {
            if (!error.empty()) scene_last_error = error;
            release_scene_marker_chunks();
            return false;
        }
        if (scene_structure_edit.active &&
            scene_structure_edit.kind == Canvas3DSceneEditKind::Sound3D) {
            scene_structure_edit = SceneStructureEditState{};
        }
    }
    return true;
}

bool Canvas3D::Impl::refresh_scene_route_stations(const MapModel& model, std::string& error) {
    Canvas3DSceneMapRefreshOptions options;
    options.route_stations = true;
    options.markers = true;
    return refresh_scene_map_content(model, options, error);
}

void Canvas3D::Impl::clear_scene() {
    stop_scene_put_between_preview_worker();
    stop_scene_loader();
    release_scene_resources();
    scene_data = {};
    scene_placement_tracks.rebuild(scene_data);
    scene_active = false;
    scene_structure_edit = SceneStructureEditState{};
    scene_placement_locations.clear();
    scene_repeater_locations.clear();
    scene_rotating = false;
    scene_hovered_object_index = -1;
    scene_hovered_marker_index = -1;
    scene_hovered_mileage.reset();
    scene_context_mileage.reset();
    scene_context_object_index = -1;
    scene_context_marker_index = -1;
    scene_hover_highlight_batch.clear();
    clear_scene_focus_highlight();
    scene_stats_value = {};
    scene_model_worker_count_value.store(0);
    reset_scene_fps_counter();
}

bool Canvas3D::Impl::has_scene() const {
    return scene_active;
}

bool Canvas3D::Impl::reload_scene_models(std::string& error) {
    if (!scene_active) {
        error = "3D scene preview is not started";
        return false;
    }
    stop_scene_put_between_preview_worker();
    stop_scene_loader();
    clear_pending_scene_model_uploads();
    std::map<std::string, SceneModelLoadRequest> requests = collect_scene_model_load_requests();
    std::vector<SceneModelLoadRequest> requests_to_load;
    requests_to_load.reserve(requests.size());
    for (auto& kv : scene_models) {
        release_scene_model(kv.second);
        kv.second = SceneModelGpu{};
        auto request_it = requests.find(kv.first);
        if (request_it != requests.end()) requests_to_load.push_back(request_it->second);
    }
    release_scene_texture_cache();
    scene_last_error.clear();
    scene_stats_value.model_path_count = scene_models.size();
    scene_load_summary_pending = false;
    start_scene_model_worker(std::move(requests_to_load));
    return true;
}

bool Canvas3D::Impl::set_scene_track_visibility(const std::vector<Canvas3DTrackVisibility>& visibility, std::string& error) {
    if (!scene_active) return true;

    std::map<std::string, bool> visible_by_key;
    for (const Canvas3DTrackVisibility& item : visibility) {
        visible_by_key[normalize_track_lookup_key(item.key)] = item.visible;
    }

    bool changed = false;
    for (Canvas3DTrackPath& path : scene_data.tracks) {
        auto it = visible_by_key.find(normalize_track_lookup_key(path.key));
        if (it == visible_by_key.end() || path.visible == it->second) continue;
        path.visible = it->second;
        changed = true;
    }
    if (!changed) return true;

    release_scene_track_chunks();
    if (!build_scene_track_chunks(error)) {
        if (!error.empty()) scene_last_error = error;
        release_scene_track_chunks();
        return false;
    }
    if (!rebuild_scene_marker_visible_indices(error)) {
        if (!error.empty()) scene_last_error = error;
        return false;
    }
    return true;
}

void Canvas3D::Impl::set_scene_window(double back_m, double forward_m) {
    if (std::isfinite(back_m) && back_m >= 0.0) scene_window_back_m = back_m;
    if (std::isfinite(forward_m) && forward_m > 0.0) scene_window_forward_m = forward_m;
    scene_stats_value.window_back_m = scene_window_back_m;
    scene_stats_value.window_forward_m = scene_window_forward_m;
}

void Canvas3D::Impl::set_scene_edit_component_scale(float scale) {
    if (!std::isfinite(scale)) scale = 1.0f;
    scene_edit_component_scale = std::clamp(scale, 0.5f, 5.0f);
}

void Canvas3D::Impl::set_scene_interaction_mode(Canvas3DSceneInteractionMode mode) {
    if (scene_interaction_mode == mode) return;
    scene_interaction_mode = mode;
    scene_rotating = false;
    scene_hovered_object_index = -1;
    scene_hovered_marker_index = -1;
    scene_hovered_mileage.reset();
    scene_context_mileage.reset();
    scene_context_object_index = -1;
    scene_context_marker_index = -1;
    scene_hover_highlight_batch.clear();
}

void Canvas3D::Impl::set_scene_fog_enabled(bool enabled) {
    scene_fog_enabled = enabled;
}

void Canvas3D::Impl::set_scene_map_draw_distance_enabled(bool enabled) {
    scene_map_draw_distance_enabled = enabled;
}

void Canvas3D::Impl::set_scene_camera_speed_percent(int percent) {
    scene_camera_speed_percent = std::clamp(percent, 50, 400);
}

void Canvas3D::Impl::set_scene_performance_warning(bool enabled,
                                   size_t warning_threshold,
                                   size_t critical_warning_threshold) {
    scene_performance_warning_enabled = enabled;
    scene_instance_warning_threshold = warning_threshold;
    scene_instance_critical_warning_threshold =
        std::max(warning_threshold, critical_warning_threshold);
}

Canvas3DSceneInteractionMode Canvas3D::Impl::scene_interaction_mode_value() const {
    return scene_interaction_mode;
}

Canvas3DSceneStats Canvas3D::Impl::scene_stats() const {
    KME_SCENE_PROFILE(Loading);
    Canvas3DSceneStats stats = scene_stats_value;
    stats.active = scene_active;
    stats.camera_distance = scene_camera_distance;
    stats.chunk_count = scene_chunks.size();
    stats.model_path_count = scene_models.size();
    stats.instance_count = scene_stats_value.instance_count;
    stats.model_worker_count = scene_model_worker_count_value.load();
    stats.window_back_m = scene_window_back_m;
    stats.window_forward_m = scene_window_forward_m;
    stats.model_ready_count = 0;
    stats.model_failed_count = 0;
    for (const auto& kv : scene_models) {
        if (kv.second.state == SceneModelGpu::State::Ready) ++stats.model_ready_count;
        if (kv.second.state == SceneModelGpu::State::Failed) ++stats.model_failed_count;
    }
    const size_t completed_count = stats.model_ready_count + stats.model_failed_count;
    stats.loading = scene_active &&
        (scene_worker_running.load() || completed_count < stats.model_path_count);
    return stats;
}

std::vector<std::string> Canvas3D::Impl::drain_scene_load_messages() {
    std::vector<std::string> messages;
    std::lock_guard<std::mutex> lock(scene_log_mutex);
    messages.swap(scene_pending_logs);
    return messages;
}

Canvas3DSceneCameraPose Canvas3D::Impl::scene_camera_pose() const {
    Canvas3DSceneCameraPose pose;
    if (!scene_active) return pose;
    pose.valid = true;
    pose.distance = scene_camera_distance;
    pose.x = -scene_camera_pos.z;
    pose.y = scene_camera_pos.x;
    pose.z = scene_camera_pos.y;
    pose.theta = scene_camera_yaw;
    pose.pitch = scene_camera_pitch;
    return pose;
}

size_t Canvas3D::Impl::count_scene_instances() const {
    size_t count = scene_data.instances.size();
    for (const Canvas3DRepeaterSegment& repeater : scene_data.repeaters) {
        count += scene_repeater_instance_count(repeater);
    }
    return count;
}

std::map<std::string, SceneModelLoadRequest> Canvas3D::Impl::collect_scene_model_load_requests() const {
    kme::timing::GuiTiming::Stage edit_timing("scene.model_requests");
    std::map<std::string, SceneModelLoadRequest> requests;
    auto note_regular_model = [&](const std::string& path) {
        if (path.empty() || requests.find(path) != requests.end()) return;
        SceneModelLoadRequest request;
        request.key = path;
        request.source_path = path;
        requests.emplace(path, std::move(request));
    };

    for (const Canvas3DModelInstance& instance : scene_data.instances) {
        note_regular_model(instance.model_path);
        if (!instance.put_between || instance.model_path.empty()) continue;

        SceneModelLoadRequest request;
        request.key = scene_model_key_for_instance(instance, scene_geometry_generation);
        request.source_path = instance.model_path;
        request.put_between.enabled = true;
        request.put_between.distance = instance.distance;
        request.put_between.flag = instance.put_between_flag & 1;
        request.put_between.origin = {instance.world[12], instance.world[13], instance.world[14]};
        request.put_between.own_track = own_track_path();
        request.put_between.track1 = placement_track_path_for_key(instance.put_between_track_key1);
        request.put_between.track2 = placement_track_path_for_key(instance.put_between_track_key2);
        const std::string request_key = request.key;
        requests.emplace(request_key, std::move(request));
    }
    for (const Canvas3DSceneObject& object : scene_data.objects) {
        for (const Canvas3DSceneModelOption& option : object.model_options) {
            note_regular_model(option.model_path);
        }
    }
    for (const Canvas3DBackgroundChange& bg : scene_data.backgrounds) {
        note_regular_model(bg.model_path);
    }
    for (const Canvas3DRepeaterSegment& repeater : scene_data.repeaters) {
        for (const std::string& path : repeater.model_paths) {
            note_regular_model(path);
        }
    }
    return requests;
}

bool Canvas3D::Impl::set_scene_object_model_option(int object_index, size_t option_index) {
    if (!scene_object_index_valid(object_index)) return false;
    Canvas3DSceneObject& object = scene_data.objects[static_cast<size_t>(object_index)];
    if (option_index >= object.model_options.size()) return false;
    const Canvas3DSceneModelOption& option = object.model_options[option_index];
    if (option.model_path.empty()) return false;

    object.selected_model_option = option_index;
    for (Canvas3DModelInstance& source : scene_data.instances) {
        if (source.object_index == object_index) source.model_path = option.model_path;
    }
    for (SceneChunk& chunk : scene_chunks) {
        for (SceneInstance& instance : chunk.instances) {
            if (instance.object_index == object_index) instance.model_path = option.model_path;
        }
    }
    if (scene_models.find(option.model_path) == scene_models.end()) {
        scene_models[option.model_path] = SceneModelGpu{};
        scene_stats_value.model_path_count = scene_models.size();
        SceneModelLoadRequest request;
        request.key = option.model_path;
        request.source_path = option.model_path;
        start_scene_model_worker({std::move(request)});
    }
    scene_hover_highlight_batch.clear();
    return true;
}

void Canvas3D::Impl::release_scene_resources() {
    for (auto& kv : scene_models) release_scene_model(kv.second);
    scene_models.clear();
    scene_put_between_preview_base_model_keys.clear();
    release_scene_texture_cache();
    release_scene_track_chunks();
    release_scene_marker_chunks();
    scene_chunks.clear();
    scene_cached_repeater_world_count = 0;
    scene_mileage_pick_points.clear();
    scene_hovered_mileage.reset();
    scene_context_mileage.reset();
    scene_placement_locations.clear();
    scene_repeater_locations.clear();
    scene_structure_edit = SceneStructureEditState{};
    clear_pending_scene_model_uploads();
    scene_last_error.clear();
    scene_stats_value = {};
    scene_model_worker_count_value.store(0);
    scene_load_summary_pending = false;
    scene_model_load_timer_active = false;
}
