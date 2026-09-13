/*
 * Copyright (c) 2026 Sapporo_ningyo
 *
 * Licensed under Apache License 2.0; see LICENSE and NOTICE.
 */

#ifdef _MSC_VER
#pragma execution_character_set("utf-8")
#endif

#ifndef NDEBUG
#include "../canvas3d_impl.h"
#include "../canvas3d_scene_data.h"
#include "../canvas3d_scene_geometry.h"
#include "kme.h"
#include <d3d11.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <map>
#include <limits>
#include <string>
#include <utility>
#include <vector>

using namespace canvas3d_detail;

Canvas3DSceneFogDebugState Canvas3D::Impl::debug_scene_fog_state() const {
    Canvas3DSceneFogDebugState state;
    state.keyframe_count = scene_data.fog_keyframes.size();
    state.fog_draw_part_count = debug_scene_fog_draw_part_count;
    state.setting_enabled = scene_fog_enabled;
    state.shader_ready = scene_fog_pixel_shader != nullptr;
    state.camera_distance = scene_camera_distance;
    const SceneFogSample sample = sample_canvas3d_scene_fog(
        scene_data.fog_keyframes, scene_camera_distance, scene_fog_enabled);
    state.sampled_enabled = sample.enabled;
    state.density = sample.density;
    state.color = sample.color;
    for (const Canvas3DSceneFogKeyframe& keyframe : scene_data.fog_keyframes) {
        if (keyframe.density > state.max_density) {
            state.max_density = keyframe.density;
            state.max_density_distance = keyframe.distance;
        }
    }
    return state;
}

bool Canvas3D::Impl::debug_read_scene_render_pixels(std::vector<std::uint8_t>& rgba,
                                    int& width, int& height,
                                    std::string& error) {
    rgba.clear();
    width = 0;
    height = 0;
    if (!device || !context || !render_texture) {
        error = "3D scene render target is not available";
        return false;
    }

    D3D11_TEXTURE2D_DESC desc = {};
    render_texture->GetDesc(&desc);
    D3D11_TEXTURE2D_DESC staging_desc = desc;
    staging_desc.Usage = D3D11_USAGE_STAGING;
    staging_desc.BindFlags = 0;
    staging_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    staging_desc.MiscFlags = 0;
    ID3D11Texture2D* staging = nullptr;
    HRESULT hr = device->CreateTexture2D(&staging_desc, nullptr, &staging);
    if (FAILED(hr)) {
        error = hresult_text("CreateTexture2D(scene debug readback)", hr);
        return false;
    }

    context->CopyResource(staging, render_texture);
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    hr = context->Map(staging, 0, D3D11_MAP_READ, 0, &mapped);
    if (FAILED(hr)) {
        error = hresult_text("Map(scene debug readback)", hr);
        release_com(staging);
        return false;
    }

    width = static_cast<int>(desc.Width);
    height = static_cast<int>(desc.Height);
    const size_t row_bytes = static_cast<size_t>(desc.Width) * 4;
    rgba.resize(row_bytes * static_cast<size_t>(desc.Height));
    for (UINT y = 0; y < desc.Height; ++y) {
        std::memcpy(rgba.data() + static_cast<size_t>(y) * row_bytes,
                    static_cast<const std::uint8_t*>(mapped.pData) +
                        static_cast<size_t>(y) * mapped.RowPitch,
                    row_bytes);
    }
    context->Unmap(staging, 0);
    release_com(staging);
    return true;
}

bool Canvas3D::Impl::debug_check_scene_edit_target(const Canvas3DPlacementEditTarget& target) const {
    const auto equal_value = [](double a, double b) { return std::abs(a - b) <= 1e-9; };
    const auto parameters_match = [&](const auto& source) {
        const std::array<double, 8> actual = {source.x, source.y, source.z, source.rx,
            source.ry, source.rz, source.tilt, source.span};
        const std::array<double, 8> expected = {target.x, target.y, target.z, target.rx,
            target.ry, target.rz, target.tilt, target.span};
        return source.track_key == target.track_key &&
            std::equal(actual.begin(), actual.end(), expected.begin(), equal_value);
    };
    const auto world_matches = [&](const double* actual, const double* expected) {
        return std::equal(actual, actual + 16, expected, equal_value);
    };
    if (target.kind == Canvas3DSceneEditKind::Repeater) {
        const auto found = scene_repeater_locations.find(target.edit_id);
        if (found == scene_repeater_locations.end() || found->second >= scene_data.repeaters.size()) return false;
        const auto& source = scene_data.repeaters[found->second];
        if (!parameters_match(source) || !equal_value(source.begin_distance, target.distance) ||
            (target.has_repeater_end_distance && !equal_value(source.end_distance, target.repeater_end_distance))) return false;
        for (const auto& chunk : scene_chunks) {
            if (!chunk.repeater_cache_prepared) continue;
            for (size_t slot = 0; slot < chunk.repeater_indices.size(); ++slot) {
                if (chunk.repeater_indices[slot] != found->second) continue;
                if (slot >= chunk.repeater_cache.size()) return false;
                const auto& cache = chunk.repeater_cache[slot];
                for (size_t offset = 0; offset < cache.worlds.size(); ++offset) {
                    const double distance = source.begin_distance + (scene_repeater_has_interval(source)
                        ? static_cast<double>(cache.first_index + static_cast<long long>(offset)) * source.interval : 0.0);
                    double expected[16]{};
                    const bool valid = make_repeater_instance_world(source, distance, expected);
                    if (valid != cache.worlds[offset].valid ||
                        (valid && !world_matches(cache.worlds[offset].world.data(), expected))) return false;
                }
            }
        }
        return true;
    }
    if (target.kind != Canvas3DSceneEditKind::Structure && target.kind != Canvas3DSceneEditKind::Signal) return false;
    const auto found = scene_placement_locations.find(target.edit_id);
    if (found == scene_placement_locations.end()) return false;
    const auto& location = found->second;
    if (location.source_index >= scene_data.instances.size() || location.chunk_index >= scene_chunks.size()) return false;
    const auto& source = scene_data.instances[location.source_index];
    const auto& instances = scene_chunks[location.chunk_index].instances;
    if (!parameters_match(source) || !equal_value(source.distance, target.distance) ||
        location.chunk_instance_index >= instances.size()) return false;
    double expected[16]{};
    return make_track_world(target.track_key, target.distance, target.x, target.y, target.z,
        target.rx, target.ry, target.rz, target.tilt, target.span, expected) &&
        world_matches(instances[location.chunk_instance_index].world, expected);
}

void Canvas3D::Impl::debug_record_scene_world(const std::string& path, int object, double distance, const double* world) {
    if (!debug_scene_capture) return;
    const auto append = [&](const void* data, size_t length) {
        const auto* bytes = static_cast<const unsigned char*>(data);
        for (size_t index = 0; index < length; ++index) {
            debug_scene_signature ^= bytes[index];
            debug_scene_signature *= 1099511628211ULL;
        }
    };
    const size_t length = path.size();
    append(&length, sizeof(length));
    append(path.data(), length);
    append(&object, sizeof(object));
    append(&distance, sizeof(distance));
    append(world, sizeof(double) * 16);
}

bool Canvas3D::Impl::debug_check_repeater_cache() {
    Impl fixture(device, nullptr);
    auto& scene = fixture.scene_data;
    scene.min_distance = 0.0;
    scene.max_distance = 500.0;
    for (const char* key : {"own", "1", " Right "}) {
        Canvas3DTrackPath track;
        track.key = key;
        for (int index = 0; index <= 20; ++index) {
            Canvas3DTrackPoint point;
            point.distance = index * 25.0;
            point.x = static_cast<double>(scene.tracks.size()) * 3.0 + index * 0.1;
            point.y = index * 0.25;
            point.z = -point.distance;
            point.theta = index * 0.002;
            point.gradient = index * 0.5;
            point.cant_angle = index * 0.001;
            track.points.push_back(point);
        }
        scene.tracks.push_back(std::move(track));
    }
    for (int tilt = 0; tilt < 4; ++tilt) {
        Canvas3DRepeaterSegment repeater;
        repeater.edit_id = "cache-" + std::to_string(tilt);
        repeater.track_key = tilt == 0 ? "0" : tilt == 1 ? "1" : "right";
        repeater.begin_distance = 3.25;
        repeater.end_distance = 403.25;
        repeater.interval = 7.5;
        repeater.model_paths = {"first", "second", "third"};
        repeater.object_index = tilt;
        repeater.tilt = tilt;
        repeater.span = 9.0;
        repeater.x = 0.75;
        repeater.y = 0.5;
        repeater.z = 1.25;
        repeater.rx = 3.0;
        repeater.ry = 17.0;
        repeater.rz = -2.0;
        scene.repeaters.push_back(repeater);
    }
    auto isolated = scene.repeaters.front();
    isolated.edit_id = "isolated";
    isolated.begin_distance = 100.0;
    isolated.interval = 0.0;
    scene.repeaters.push_back(isolated);
    isolated.edit_id = "invalid-interval";
    isolated.interval = std::numeric_limits<double>::quiet_NaN();
    scene.repeaters.push_back(isolated);
    isolated.edit_id = "empty";
    isolated.model_paths.clear();
    scene.repeaters.push_back(isolated);
    fixture.scene_placement_tracks.rebuild(scene);
    std::string error;
    if (!fixture.build_scene_chunks(error)) return false;
    const auto collect = [&](bool reference, double minimum, double maximum) {
        fixture.debug_scene_reference = reference;
        fixture.debug_scene_capture = true;
        fixture.debug_scene_signature = 14695981039346656037ULL;
        std::map<std::string, std::vector<SceneInstanceData>> instances;
        for (auto& chunk : fixture.scene_chunks) {
            if (!fixture.scene_chunk_visible(chunk, minimum, maximum)) {
                if (chunk.repeater_cache_prepared) fixture.invalidate_scene_repeater_cache(chunk);
                continue;
            }
            fixture.append_visible_repeater_instances(chunk, minimum, maximum, {1.0, 2.0, 3.0},
                identity(), 640, 480, false, instances, nullptr);
        }
        return fixture.debug_scene_signature;
    };
    const auto compare = [&](double minimum, double maximum) {
        const auto cached = collect(false, minimum, maximum);
        const auto original = collect(true, minimum, maximum);
        fixture.debug_scene_reference = false;
        return cached == original && fixture.scene_cached_repeater_world_count <= k_scene_repeater_cache_instance_limit;
    };
    for (const auto& range : std::vector<std::pair<double, double>>{
             {-100.0, 100.0}, {3.25, 3.25}, {99.999999, 100.000001}, {100.0, 200.0},
             {300.0, 500.0}, {0.0, 130.0}, {403.249999, 403.25}}) {
        if (!compare(range.first, range.second)) return false;
    }
    const auto baseline = scene.repeaters.front();
    for (bool move_range : {false, true}) {
        auto changed = baseline;
        changed.x += 4.0;
        changed.rx += 8.0;
        changed.interval = 6.75;
        std::reverse(changed.model_paths.begin(), changed.model_paths.end());
        changed.track_key = "1";
        if (move_range) { changed.begin_distance += 100.0; changed.end_distance -= 100.0; }
        if (!fixture.write_scene_repeater_segment(baseline.edit_id, changed) || !compare(0.0, 500.0) ||
            !fixture.write_scene_repeater_segment(baseline.edit_id, baseline) || !compare(0.0, 500.0)) return false;
    }
    // The full chunk exceeds the cache budget, but the tiny visible slice is
    // still enumerated in full by the original path. No dense-instance culling.
    auto dense = baseline;
    dense.begin_distance = 0.0;
    dense.end_distance = 100.0;
    dense.interval = 0.0005;
    if (!fixture.write_scene_repeater_segment(baseline.edit_id, dense) || !compare(50.0, 50.002)) return false;
    // Track geometry replacement invalidates both indices and world caches.
    scene.tracks[1].points[2].y += 2.0;
    fixture.scene_placement_tracks.rebuild(scene);
    if (!fixture.build_scene_chunks(error) || !compare(50.0, 50.002)) return false;
    collect(false, 900.0, 1000.0);
    return fixture.scene_cached_repeater_world_count == 0;
}

bool Canvas3D::Impl::debug_check_scene_fps_counter(std::string& error) {
    using Clock = SceneFpsCounter::Clock;
    SceneFpsCounter counter;
    Clock::time_point now{};
    const auto advance = [](SceneFpsCounter& target, Clock::time_point& time, double seconds) {
        time += std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(seconds));
        target.update(time);
    };
    const auto check_steady_rate = [&](double fps) {
        counter.reset();
        now = {};
        counter.update(now);
        const size_t interval_count = static_cast<size_t>(std::ceil(fps * 0.3));
        for (size_t i = 0; i < interval_count; ++i) advance(counter, now, 1.0 / fps);
        return counter.value > 0.0f &&
            std::abs(static_cast<double>(counter.value) - fps) <= fps * 0.01;
    };
    const auto fail = [&](const char* label) {
        error = label;
        return false;
    };

    counter.reset();
    if (counter.last_frame_valid || counter.active_seconds != 0.0 ||
        counter.interval_count != 0 || counter.value != 0.0f) {
        return fail("fps-counter-reset");
    }
    counter.update(now);
    if (!counter.last_frame_valid || counter.interval_count != 0 || counter.value != 0.0f) {
        return fail("fps-counter-first-frame");
    }
    advance(counter, now, 0.05);
    advance(counter, now, 0.05);
    if (counter.interval_count != 2 || counter.value != 0.0f) {
        return fail("fps-counter-partial-window");
    }
    const double active_before_nonpositive = counter.active_seconds;
    counter.update(now);
    if (counter.interval_count != 2 || counter.active_seconds != active_before_nonpositive) {
        return fail("fps-counter-zero-interval");
    }
    counter.update(now - std::chrono::milliseconds(1));
    if (counter.interval_count != 2 || counter.active_seconds != active_before_nonpositive) {
        return fail("fps-counter-negative-interval");
    }

    for (double fps : {30.0, 60.0, 144.0}) {
        if (!check_steady_rate(fps)) return fail("fps-counter-steady-rate");
    }

    counter.reset();
    now = {};
    counter.update(now);
    for (size_t i = 0; i < 40; ++i) advance(counter, now, 1.0 / 60.0);
    const float stable_value = counter.value;
    if (std::abs(static_cast<double>(stable_value) - 60.0) > 0.6) {
        return fail("fps-counter-stable-baseline");
    }
    advance(counter, now, 1.0);
    if (counter.active_seconds != 0.0 || counter.interval_count != 0 || counter.value != stable_value) {
        return fail("fps-counter-idle-gap");
    }
    for (size_t i = 0; i < 3; ++i) advance(counter, now, 0.001);
    if (counter.value != stable_value || counter.interval_count != 3) {
        return fail("fps-counter-queue-burst-hold");
    }
    for (size_t i = 0; i < 12; ++i) advance(counter, now, 1.0 / 60.0);
    if (counter.value < 59.0f || counter.value > 80.0f || counter.value >= 100.0f) {
        return fail("fps-counter-queue-burst-publish");
    }
    counter.reset();
    if (counter.last_frame_valid || counter.active_seconds != 0.0 ||
        counter.interval_count != 0 || counter.value != 0.0f) {
        return fail("fps-counter-final-reset");
    }
    return true;
}

Canvas3DSceneRenderContractResult Canvas3D::Impl::debug_check_scene_render() {
    Canvas3DSceneRenderContractResult result;
    std::string fps_error;
    result.fps_counter = debug_check_scene_fps_counter(fps_error);
    const DVec3 saved_position = scene_camera_pos;
    const double saved_distance = scene_camera_distance;
    const float saved_yaw = scene_camera_yaw, saved_pitch = scene_camera_pitch;
    const double saved_lateral = scene_camera_lateral_offset, saved_vertical = scene_camera_vertical_offset;
    const float saved_yaw_offset = scene_camera_yaw_offset;
    const double saved_back = scene_window_back_m, saved_forward = scene_window_forward_m;
    const bool saved_fog = scene_fog_enabled;
    const auto saved_interaction = scene_interaction_mode;
    std::vector<Canvas3DTrackVisibility> saved_visibility;
    for (const auto& track : scene_data.tracks) saved_visibility.push_back({track.key, track.visible});
    SceneObjectJumpTarget focus_target;
    for (const auto& repeater : scene_data.repeaters) {
        SceneRepeaterIndexRange range;
        if (repeater.model_paths.empty() || !scene_object_index_valid(repeater.object_index) ||
            !scene_repeater_index_range(repeater, saved_distance + 10.0, saved_distance + saved_forward, range)) continue;
        const double distance = repeater.begin_distance + static_cast<double>(range.first) * repeater.interval;
        if (!make_repeater_instance_world(repeater, distance, focus_target.world)) continue;
        focus_target.object_index = repeater.object_index;
        focus_target.model_path = repeater.model_paths[static_cast<size_t>(range.first) % repeater.model_paths.size()];
        break;
    }
    const auto check = [&](const char* label, bool picking, bool focus) {
        std::array<std::uint64_t, 2> signatures{};
        std::array<int, 2> objects{}, markers{};
        std::array<size_t, 2> instances{}, tracks{};
        std::array<std::vector<std::uint8_t>, 2> pixels;
        for (size_t pass = 0; pass < 2; ++pass) {
            debug_scene_reference = pass == 1;
            debug_scene_capture = true;
            scene_interaction_mode = picking ? Canvas3DSceneInteractionMode::Select : saved_interaction;
            clear_scene_focus_highlight();
            if (focus && focus_target.object_index >= 0) {
                start_scene_focus_highlight(focus_target.object_index, focus_target.model_path, focus_target.world);
            } else if (focus && !scene_data.markers.empty()) {
                start_scene_marker_focus_highlight(0);
            }
            render_scene_preview_target(1260, 680, {630.0f, 500.0f}, picking, false);
            signatures[pass] = debug_scene_signature;
            objects[pass] = scene_hovered_object_index;
            markers[pass] = scene_hovered_marker_index;
            instances[pass] = scene_stats_value.drawn_instance_count;
            tracks[pass] = scene_stats_value.drawn_track_chunk_count;
            int width = 0, height = 0;
            if (!debug_read_scene_render_pixels(pixels[pass], width, height, result.error)) return;
            size_t bytes = 0;
            for (const auto& chunk : scene_chunks) {
                bytes += chunk.repeater_cache.capacity() * sizeof(SceneChunk::RepeaterCache);
                for (const auto& cache : chunk.repeater_cache) bytes += cache.worlds.capacity() * sizeof(SceneChunk::RepeaterWorld);
            }
            result.peak_cache_bytes = std::max(result.peak_cache_bytes, bytes);
            result.peak_cached_worlds = std::max(result.peak_cached_worlds, scene_cached_repeater_world_count);
        }
        if (result.cases == 0) result.signature = signatures[0];
        ++result.cases;
        if (objects[0] >= 0 || markers[0] >= 0) ++result.picked_cases;
        if (tracks[0] > 0) ++result.track_draw_cases;
        result.instances = result.instances && signatures[0] == signatures[1] && instances[0] == instances[1] && tracks[0] == tracks[1];
        result.pixels = result.pixels && !pixels[0].empty() && pixels[0] == pixels[1];
        result.picking = result.picking && objects[0] == objects[1] && markers[0] == markers[1];
        result.cache_budget = result.peak_cached_worlds <= k_scene_repeater_cache_instance_limit;
        if ((!result.instances || !result.pixels || !result.picking || !result.cache_budget) && result.error.empty()) result.error = label;
    };
    try {
        check("stationary", false, false);
        check("hover", true, false);
        check("focus", false, true);
        auto material_model = scene_models.find(focus_target.model_path);
        if (material_model != scene_models.end() && !material_model->second.parts.empty()) {
            const size_t material_index = material_model->second.parts.front().material_index;
            if (material_index < material_model->second.materials.size()) {
                float& alpha = material_model->second.materials[material_index].diffuse[3];
                struct RestoreAlpha {
                    float& alpha;
                    float value;
                    ~RestoreAlpha() { alpha = value; }
                } restore{alpha, alpha};
                alpha = 0.5f;
                check("translucent-material", false, false);
            }
        }
        scene_camera_pos.x += 3.0;
        scene_camera_yaw += 0.12f;
        scene_camera_pitch += 0.03f;
        check("translate-rotate", true, false);
        for (double delta : {101.0, 501.0, -50.0, 0.0}) {
            reset_scene_camera_pose_at_distance(saved_distance + delta);
            check("chunk-jump-return", false, false);
        }
        scene_window_back_m = 35.0;
        scene_window_forward_m = 350.0;
        check("window", false, false);
        scene_fog_enabled = !saved_fog;
        check("fog", false, false);
        auto visible = saved_visibility;
        for (auto& track : visible) track.visible = true;
        if (!set_scene_track_visibility(visible, result.error)) result.instances = false;
        check("tracks-visible", true, false);
    } catch (const std::exception& error) {
        result.error = error.what();
    }
    debug_scene_reference = false;
    debug_scene_capture = false;
    clear_scene_focus_highlight();
    reset_scene_camera_pose_at_distance(saved_distance);
    scene_camera_pos = saved_position;
    scene_camera_yaw = saved_yaw;
    scene_camera_pitch = saved_pitch;
    scene_camera_lateral_offset = saved_lateral;
    scene_camera_vertical_offset = saved_vertical;
    scene_camera_yaw_offset = saved_yaw_offset;
    scene_window_back_m = saved_back;
    scene_window_forward_m = saved_forward;
    scene_fog_enabled = saved_fog;
    scene_interaction_mode = saved_interaction;
    std::string restore_error;
    if (!set_scene_track_visibility(saved_visibility, restore_error)) result.error = restore_error;
    if (!result.fps_counter && result.error.empty()) result.error = fps_error;
    return result;
}

#endif
