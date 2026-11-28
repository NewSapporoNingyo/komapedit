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
#include "canvas3d_put_between.h"
#include "kme.h"
#include "model_loader.h"
#include <d3d11.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <map>
#include <mutex>
#include <limits>
#include <memory>
#include <new>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>
#include <stdexcept>

using namespace canvas3d_detail;

namespace canvas3d_detail {

#ifndef NDEBUG
std::atomic<int> g_debug_put_between_derive_throw_countdown{0};
std::atomic<size_t> g_debug_put_between_prepare_count{0};
#endif

} // namespace canvas3d_detail

namespace canvas3d_detail {

bool update_cpu_model_bounds(CpuModelData& model) {
    if (model.vertices.empty()) return false;

    DVec3 bounds_min{
        std::numeric_limits<double>::infinity(),
        std::numeric_limits<double>::infinity(),
        std::numeric_limits<double>::infinity()
    };
    DVec3 bounds_max{
        -std::numeric_limits<double>::infinity(),
        -std::numeric_limits<double>::infinity(),
        -std::numeric_limits<double>::infinity()
    };
    for (const GpuVertex& vertex : model.vertices) {
        bounds_min.x = std::min(bounds_min.x, static_cast<double>(vertex.px));
        bounds_min.y = std::min(bounds_min.y, static_cast<double>(vertex.py));
        bounds_min.z = std::min(bounds_min.z, static_cast<double>(vertex.pz));
        bounds_max.x = std::max(bounds_max.x, static_cast<double>(vertex.px));
        bounds_max.y = std::max(bounds_max.y, static_cast<double>(vertex.py));
        bounds_max.z = std::max(bounds_max.z, static_cast<double>(vertex.pz));
    }
    const DVec3 center = (bounds_min + bounds_max) * 0.5;
    double radius_squared = 0.0;
    for (const GpuVertex& vertex : model.vertices) {
        const DVec3 offset{
            static_cast<double>(vertex.px) - center.x,
            static_cast<double>(vertex.py) - center.y,
            static_cast<double>(vertex.pz) - center.z
        };
        radius_squared = std::max(radius_squared, dot(offset, offset));
    }
    const double radius = std::sqrt(radius_squared);
    if (!std::isfinite(radius) || radius > std::numeric_limits<float>::max()) return false;

    model.bounds_min = {
        static_cast<float>(bounds_min.x),
        static_cast<float>(bounds_min.y),
        static_cast<float>(bounds_min.z)
    };
    model.bounds_max = {
        static_cast<float>(bounds_max.x),
        static_cast<float>(bounds_max.y),
        static_cast<float>(bounds_max.z)
    };
    model.center = {
        static_cast<float>(center.x),
        static_cast<float>(center.y),
        static_cast<float>(center.z)
    };
    model.radius = std::max(static_cast<float>(radius), 0.001f);
    return true;
}

PutBetweenSourceTemplate prepare_put_between_source(const CpuModelData& source) {
#ifndef NDEBUG
    g_debug_put_between_prepare_count.fetch_add(1, std::memory_order_relaxed);
#endif
    PutBetweenSourceTemplate result;
    if (!source.ok) {
        result.error = source.error;
        return result;
    }
    if (source.vertices.empty()) {
        result.error = "PutBetween model contains no vertices";
        return result;
    }

    double average_x = 0.0;
    for (const GpuVertex& vertex : source.vertices) {
        if (!std::isfinite(vertex.px) || !std::isfinite(vertex.py) ||
            !std::isfinite(vertex.pz)) {
            result.error = "PutBetween model contains invalid vertex coordinates";
            return result;
        }
        average_x += static_cast<double>(vertex.px);
    }
    average_x /= static_cast<double>(source.vertices.size());
    if (!std::isfinite(average_x)) {
        result.error = "PutBetween model contains invalid vertex coordinates";
        return result;
    }

    // BVE PutBetween models extend from x=0 in either the positive or negative x direction.
    // The sign determines which end of the mesh is anchored to trackKey1.
    const double orientation = average_x < 0.0 ? -1.0 : 1.0;
    double min_oriented_x = std::numeric_limits<double>::infinity();
    double max_oriented_x = -std::numeric_limits<double>::infinity();
    for (const GpuVertex& vertex : source.vertices) {
        const double oriented_x = static_cast<double>(vertex.px) * orientation;
        min_oriented_x = std::min(min_oriented_x, oriented_x);
        max_oriented_x = std::max(max_oriented_x, oriented_x);
    }
    const double split_x = average_x * orientation;
    const double opposite_edge_offset = (min_oriented_x + max_oriented_x) * orientation;

    result.vertex_slice_indices.resize(source.vertices.size());
    result.vertex_residual_x.resize(source.vertices.size());
    result.vertex_track1_side.resize(source.vertices.size());
    std::map<float, size_t> slice_indices;
    for (size_t index = 0; index < source.vertices.size(); ++index) {
        const GpuVertex& vertex = source.vertices[index];
        auto [slice, inserted] = slice_indices.emplace(vertex.pz, slice_indices.size());
        if (inserted) result.slice_source_z.push_back(static_cast<double>(vertex.pz));
        const bool track1_side =
            static_cast<double>(vertex.px) * orientation < split_x;
        result.vertex_slice_indices[index] = slice->second;
        result.vertex_track1_side[index] = track1_side ? 1u : 0u;
        result.vertex_residual_x[index] = track1_side
            ? static_cast<double>(vertex.px)
            : static_cast<double>(vertex.px) - opposite_edge_offset;
    }
    result.ok = true;
    return result;
}

CpuModelData derive_put_between_model(const CpuModelData& source,
                                      const PutBetweenSourceTemplate& source_template,
                                      const SceneModelLoadRequest& request) {
#ifndef NDEBUG
    int remaining = g_debug_put_between_derive_throw_countdown.load(std::memory_order_relaxed);
    while (remaining > 0 &&
           !g_debug_put_between_derive_throw_countdown.compare_exchange_weak(
               remaining, remaining - 1, std::memory_order_relaxed)) {
    }
    if (remaining == 1) {
        throw std::runtime_error("debug injected PutBetween derivation failure");
    }
#endif
    CpuModelData result;
    result.path = request.source_path;
    result.scene_key = request.key;
    result.shared_model_key = request.source_path;
    if (!source.ok || !source_template.ok) {
        result.error = source.ok ? source_template.error : source.error;
        return result;
    }
    if (!request.put_between.own_track || !request.put_between.track1 ||
        !request.put_between.track2) {
        result.error = "PutBetween references an unavailable track";
        return result;
    }
    if (source_template.vertex_slice_indices.size() != source.vertices.size() ||
        source_template.vertex_residual_x.size() != source.vertices.size() ||
        source_template.vertex_track1_side.size() != source.vertices.size()) {
        result.error = "PutBetween source template does not match the model";
        return result;
    }

    struct SliceFrame {
        DVec3 track1_anchor;
        DVec3 track2_anchor;
        DVec3 right;
        DVec3 up;
        DVec3 forward;
    };
    std::vector<SliceFrame> frames(source_template.slice_source_z.size());
    for (size_t index = 0; index < source_template.slice_source_z.size(); ++index) {
        const double vertex_distance =
            request.put_between.distance - source_template.slice_source_z[index];
        auto own_point = scene_sample_track_path_points(
            *request.put_between.own_track, vertex_distance);
        auto track1_point = scene_sample_track_path_points(
            *request.put_between.track1, vertex_distance);
        auto track2_point = scene_sample_track_path_points(
            *request.put_between.track2, vertex_distance);
        if (!own_point || !track1_point || !track2_point) {
            result.error = "PutBetween could not sample track geometry";
            return result;
        }

        SliceFrame& frame = frames[index];
        frame.track1_anchor = {track1_point->x, track1_point->y, track1_point->z};
        frame.track2_anchor = {track2_point->x, track2_point->y, track2_point->z};
        if ((request.put_between.flag & 1) != 0) {
            frame.track1_anchor.y = own_point->y;
            frame.track2_anchor.y = own_point->y;
        }
        const double gradient = std::isfinite(own_point->gradient)
            ? own_point->gradient / 1000.0 : 0.0;
        frame.right = right_from_theta_d(own_point->theta);
        frame.forward = normalize(DVec3{
            std::sin(own_point->theta), gradient, -std::cos(own_point->theta)});
        frame.up = normalize(cross(frame.right, frame.forward));
    }

    const double max_float = static_cast<double>(std::numeric_limits<float>::max());
    result.vertices = source.vertices;
    for (size_t index = 0; index < result.vertices.size(); ++index) {
        GpuVertex& vertex = result.vertices[index];
        const GpuVertex& source_vertex = source.vertices[index];
        const SliceFrame& frame = frames[source_template.vertex_slice_indices[index]];
        const DVec3& anchor = source_template.vertex_track1_side[index]
            ? frame.track1_anchor : frame.track2_anchor;
        const DVec3 world_position = anchor +
            frame.right * source_template.vertex_residual_x[index] +
            frame.up * static_cast<double>(source_vertex.py);
        const DVec3 local_position = world_position - request.put_between.origin;
        if (!std::isfinite(local_position.x) || !std::isfinite(local_position.y) ||
            !std::isfinite(local_position.z) || std::abs(local_position.x) > max_float ||
            std::abs(local_position.y) > max_float || std::abs(local_position.z) > max_float) {
            result.vertices.clear();
            result.error = "PutBetween produced invalid vertex coordinates";
            return result;
        }
        vertex.px = static_cast<float>(local_position.x);
        vertex.py = static_cast<float>(local_position.y);
        vertex.pz = static_cast<float>(local_position.z);

        const DVec3 source_normal{source_vertex.nx, source_vertex.ny, source_vertex.nz};
        if (dot(source_normal, source_normal) > 1e-12) {
            const DVec3 world_normal = normalize(
                frame.right * source_normal.x + frame.up * source_normal.y +
                frame.forward * -source_normal.z);
            vertex.nx = static_cast<float>(world_normal.x);
            vertex.ny = static_cast<float>(world_normal.y);
            vertex.nz = static_cast<float>(world_normal.z);
        }
    }

    if (!update_cpu_model_bounds(result)) {
        result.vertices.clear();
        result.error = "PutBetween could not calculate deformed model bounds";
        return result;
    }
    result.ok = true;
    return result;
}

} // namespace canvas3d_detail

void Canvas3D::Impl::run_scene_put_between_preview_worker() {
    using Failure = ScenePutBetweenPreviewResult::Failure;
    try {
#ifndef NDEBUG
        if (std::exchange(debug_faults.put_between_start_failure, false)) throw std::bad_alloc();
#endif
        std::unordered_map<std::string, ScenePutBetweenPreparedSource> source_cache;
        for (;;) {
            ScenePutBetweenPreviewJob job;
            {
                std::unique_lock<std::mutex> lock(scene_put_between_preview_mutex);
                scene_put_between_preview_cv.wait(lock, [this]() {
                    return scene_put_between_preview_state != PutBetweenPreviewWorkerState::Running ||
                        scene_put_between_preview_pending.has_value();
                });
                if (scene_put_between_preview_state != PutBetweenPreviewWorkerState::Running) return;
                job = std::move(*scene_put_between_preview_pending);
                scene_put_between_preview_pending.reset();
            }

            ScenePutBetweenPreviewResult result;
            result.sequence = job.sequence;
            result.geometry_generation = job.geometry_generation;
            try {
#ifndef NDEBUG
                if (debug_faults.put_between_job_started) debug_faults.put_between_job_started();
                if (debug_faults.put_between_allocation_failure.exchange(false)) throw std::bad_alloc();
#endif
                const auto& path = job.request.source_path;
                auto source = source_cache.find(path);
                if (source == source_cache.end()) {
                    if (source_cache.size() >= 4) source_cache.clear();
                    ScenePutBetweenPreparedSource prepared;
                    prepared.source = std::make_shared<CpuModelData>();
                    MlMeshData data = {};
                    std::string load_error;
                    if (load_scene_model_data(path, data, load_error)) {
                        ModelDataGuard data_guard(data);
                        *prepared.source = copy_cpu_model(path, data);
                    } else {
                        prepared.source->error = std::move(load_error);
                    }
                    prepared.source_template = prepare_put_between_source(*prepared.source);
                    source = source_cache.emplace(path, std::move(prepared)).first;
                }

                result.source = source->second.source;
                result.derived = derive_put_between_model(
                    *result.source, source->second.source_template, job.request);
            } catch (const std::bad_alloc&) {
                result.failure = Failure::Allocation;
            } catch (...) {
                result.failure = Failure::Exception;
                result.exception = std::current_exception();
            }
            {
                std::lock_guard<std::mutex> lock(scene_put_between_preview_mutex);
                if (scene_put_between_preview_state == PutBetweenPreviewWorkerState::Running &&
                    result.sequence == scene_put_between_preview_latest_sequence) {
                    scene_put_between_preview_completed = std::move(result);
                }
            }
            notify_scene_loading_progress();
        }
    } catch (...) {
        bool pending_job = false;
        {
            std::lock_guard<std::mutex> lock(scene_put_between_preview_mutex);
            if (scene_put_between_preview_state != PutBetweenPreviewWorkerState::Running) return;
            scene_put_between_preview_state = PutBetweenPreviewWorkerState::Exited;
            if (scene_put_between_preview_pending) {
                auto& result = scene_put_between_preview_completed.emplace();
                result.sequence = scene_put_between_preview_pending->sequence;
                result.geometry_generation = scene_put_between_preview_pending->geometry_generation;
                result.failure = Failure::WorkerExited;
                result.exception = std::current_exception();
                scene_put_between_preview_pending.reset();
                pending_job = true;
            }
        }
        if (!pending_job) {
            log_scene_worker_failure({"[warn]canvas3D.cpp: PutBetween preview worker exited"});
        }
        notify_scene_loading_progress();
    }
}

std::uint64_t Canvas3D::Impl::submit_scene_put_between_preview_job(ScenePutBetweenPreviewJob job) {
    std::unique_lock<std::mutex> lock(scene_put_between_preview_mutex);
    if (scene_put_between_preview_state == PutBetweenPreviewWorkerState::Exited) {
        scene_put_between_preview_state = PutBetweenPreviewWorkerState::Stopped;
        lock.unlock();
        scene_put_between_preview_worker.join();
        lock.lock();
    }
    if (scene_put_between_preview_state == PutBetweenPreviewWorkerState::Stopped) {
        scene_put_between_preview_worker = std::thread(&Impl::run_scene_put_between_preview_worker, this);
        scene_put_between_preview_state = PutBetweenPreviewWorkerState::Running;
    }
    const std::uint64_t sequence = ++scene_put_between_preview_latest_sequence;
    job.sequence = sequence;
    scene_put_between_preview_pending = std::move(job);
    scene_put_between_preview_completed.reset();
    lock.unlock();
    scene_put_between_preview_cv.notify_one();
    return sequence;
}

void Canvas3D::Impl::stop_scene_put_between_preview_worker() {
    {
        std::lock_guard<std::mutex> lock(scene_put_between_preview_mutex);
        scene_put_between_preview_state = PutBetweenPreviewWorkerState::Stopped;
        scene_put_between_preview_pending.reset();
        scene_put_between_preview_completed.reset();
    }
    scene_put_between_preview_cv.notify_one();
    if (scene_put_between_preview_worker.joinable()) {
        scene_put_between_preview_worker.join();
    }
}

void Canvas3D::Impl::discard_scene_put_between_preview_request() {
    std::lock_guard<std::mutex> lock(scene_put_between_preview_mutex);
    ++scene_put_between_preview_latest_sequence;
    scene_put_between_preview_pending.reset();
    scene_put_between_preview_completed.reset();
    scene_structure_edit.preview_sequence = 0;
    scene_structure_edit.resume_preview_after_model_reload = false;
}

std::uint64_t Canvas3D::Impl::queue_scene_put_between_preview(
    const Canvas3DPlacementEditTarget& target) {
    DVec3 origin;
    std::array<DVec3, 3> axes{};
    if (target.model_path.empty() ||
        !put_between_edit_frame(target.distance, origin, axes)) {
        return 0;
    }
    const Canvas3DTrackPath* own = own_track_path();
    const Canvas3DTrackPath* track1 =
        placement_track_path_for_key(target.put_between_track_key1);
    const Canvas3DTrackPath* track2 =
        placement_track_path_for_key(target.put_between_track_key2);
    if (!own || !track1 || !track2) return 0;

    try {
        return submit_scene_put_between_preview_job({0, scene_geometry_generation, {
            scene_put_between_preview_model_key(target.edit_id, scene_geometry_generation),
            target.model_path,
            {true, target.distance, target.put_between_flag & 1, origin, own, track1, track2}
        }});
    } catch (const std::exception& error) {
        log_scene_worker_failure({
            "[warn]canvas3D.cpp: could not queue PutBetween preview: ", error.what()});
        return 0;
    }
}

std::optional<ScenePutBetweenPreviewResult>
Canvas3D::Impl::take_scene_put_between_preview_result() {
    std::unique_lock<std::mutex> lock(scene_put_between_preview_mutex);
    auto result = std::exchange(scene_put_between_preview_completed, std::nullopt);
    if (scene_put_between_preview_state == PutBetweenPreviewWorkerState::Exited) {
        scene_put_between_preview_state = PutBetweenPreviewWorkerState::Stopped;
        lock.unlock();
        scene_put_between_preview_worker.join();
    }
    return result;
}

bool Canvas3D::Impl::upload_scene_put_between_preview_model(
    const CpuModelData& cpu,
    const std::string& preview_key,
    const SceneModelGpu& shared_model,
    std::string& error) {
    if (cpu.vertices.size() >
        static_cast<size_t>(std::numeric_limits<UINT>::max() /
                            sizeof(GpuVertex))) {
        error = "PutBetween preview model is too large for a Direct3D 11 buffer";
        return false;
    }

    SceneModelGpu& current = scene_models[preview_key];
    current.preview_only = true;
    const UINT vertex_count = static_cast<UINT>(cpu.vertices.size());
    const bool rebuild = !current.dynamic_vertices ||
        current.shared_model_key != cpu.shared_model_key ||
        current.vertex_capacity < vertex_count;
    SceneModelGpu replacement;
    const auto release = [this](SceneModelGpu* model) { release_scene_model(*model); };
    const std::unique_ptr<SceneModelGpu, decltype(release)> cleanup(
        rebuild ? &replacement : nullptr, release);
    SceneModelGpu& model = rebuild ? replacement : current;
    if (rebuild) {
        D3D11_BUFFER_DESC vb_desc = {};
        vb_desc.ByteWidth = vertex_count * sizeof(GpuVertex);
        vb_desc.Usage = D3D11_USAGE_DYNAMIC;
        vb_desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        vb_desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        HRESULT hr = device->CreateBuffer(&vb_desc, nullptr, &model.vertex_buffer);
        if (FAILED(hr)) {
            error = hresult_text("CreateBuffer(PutBetween preview vertex)", hr);
            return false;
        }
        model.shared_model_key = cpu.shared_model_key;
        model.index_buffer = shared_model.index_buffer;
        model.index_buffer->AddRef();
        model.parts = shared_model.parts;
        model.materials = shared_model.materials;
        for (GpuMaterial& material : model.materials) {
            if (material.texture) material.texture->AddRef();
        }
        model.index_count = shared_model.index_count;
        model.vertex_capacity = vertex_count;
        model.dynamic_vertices = true;
        model.preview_only = true;
    }

    D3D11_MAPPED_SUBRESOURCE mapped = {};
    HRESULT hr = context->Map(
        model.vertex_buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    if (FAILED(hr)) {
        error = hresult_text("Map(PutBetween preview vertex)", hr);
        return false;
    }
    std::memcpy(mapped.pData, cpu.vertices.data(),
                cpu.vertices.size() * sizeof(GpuVertex));
    context->Unmap(model.vertex_buffer, 0);
    model.bounds_min = cpu.bounds_min;
    model.bounds_max = cpu.bounds_max;
    model.center = cpu.center;
    model.radius = cpu.radius;
    model.state = SceneModelGpu::State::Ready;
    model.error.clear();
    if (rebuild) std::swap(current, replacement);
    return true;
}

void Canvas3D::Impl::apply_scene_put_between_preview_result() {
    if (scene_worker_running.load() || !active_scene_model_requests.empty()) return;
    auto& edit = scene_structure_edit;
    if (std::exchange(edit.resume_preview_after_model_reload, false) && edit.preview_sequence == 0) {
        edit.preview_sequence = queue_scene_put_between_preview(edit.current);
    }
    auto pending = take_scene_put_between_preview_result();
    if (!pending || !scene_active || !edit.active ||
        edit.kind != Canvas3DSceneEditKind::StructurePutBetween ||
        pending->geometry_generation != scene_geometry_generation ||
        pending->sequence != edit.preview_sequence) {
        return;
    }
    edit.preview_sequence = 0;

    using Failure = ScenePutBetweenPreviewResult::Failure;
    auto& result = *pending;
    constexpr std::string_view failure_prefix = "[warn]canvas3D.cpp: PutBetween live preview failed: ";
    if (result.failure == Failure::Allocation) {
        log_scene_worker_failure({failure_prefix, "model allocation failed"});
        return;
    }
    if (result.failure != Failure::None) {
        const std::string_view stage = result.failure == Failure::WorkerExited
            ? "worker exited: " : "";
        try {
            std::rethrow_exception(result.exception);
        } catch (const std::exception& error) {
            log_scene_worker_failure({failure_prefix, stage, error.what()});
        } catch (...) {
            log_scene_worker_failure({failure_prefix, stage, "unknown worker error"});
        }
        return;
    }
    if (!result.derived.ok) {
        log_scene_worker_failure({failure_prefix, result.derived.error});
        return;
    }

    try {
        Canvas3DPlacementEditTarget target = edit.current;
        auto desired = put_between_instance_from_target(target, edit.baseline_instance);
        auto shared = scene_models.find(result.source->path);
        if (shared == scene_models.end()) {
            scene_put_between_preview_base_model_keys.insert(result.source->path);
            shared = scene_models.try_emplace(result.source->path).first;
            shared->second.preview_only = true;
        }
        std::string error;
        if (shared->second.state != SceneModelGpu::State::Ready &&
            (!upload_scene_model(*result.source, error) ||
             shared->second.state != SceneModelGpu::State::Ready)) {
            log_scene_worker_failure({
                failure_prefix, "base upload: ", error.empty() ? shared->second.error : error});
            return;
        }

        auto& preview_key = result.derived.scene_key;
        if (!upload_scene_put_between_preview_model(result.derived, preview_key, shared->second, error)) {
            log_scene_worker_failure({failure_prefix, error});
            return;
        }
        if (!write_scene_put_between_instance(target.edit_id, std::move(desired), preview_key)) {
            log_scene_worker_failure({failure_prefix, "instance update failed"});
            return;
        }
        edit.completed = std::move(target);
        edit.preview_model_key = std::move(preview_key);
        release_unused_put_between_preview_base_models();
    } catch (const std::exception& error) {
        log_scene_worker_failure({failure_prefix, error.what()});
    }
}
