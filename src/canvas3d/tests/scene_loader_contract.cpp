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
#include "../canvas3d_put_between.h"
#include "kme.h"
#include <d3d11.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <map>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <mutex>
#include <limits>
#include <string>
#include <thread>
#include <utility>
#include <vector>
#include <stdexcept>

using namespace canvas3d_detail;

Canvas3DSceneLoaderContractResult Canvas3D::Impl::debug_run_scene_loader_contract(
    const std::string& valid_model_path,
    const std::string& valid_texture_path) {
    Canvas3DSceneLoaderContractResult result;
    result.release_balance = true;
    auto record_release_counts = [&]() {
        const size_t loaded = ModelLoaderClient::debug_successful_load_count();
        const size_t freed = ModelLoaderClient::debug_free_count();
        result.successful_load_count += loaded;
        result.free_count += freed;
        result.release_balance = result.release_balance && loaded == freed;
    };
    auto reset_scene_worker_state = [&]() {
        stop_scene_loader();
        clear_pending_scene_model_uploads();
        for (auto& entry : scene_models) release_scene_model(entry.second);
        scene_models.clear();
        scene_cancel.store(false);
        scene_worker_running.store(false);
        scene_model_worker_limit = 1;
        debug_copy_cpu_model_throw_countdown.store(0);
        debug_texture_allocation_throw_countdown.store(0);
        debug_scene_index_buffer_failure_countdown.store(0);
        g_debug_put_between_derive_throw_countdown.store(0);
        g_debug_put_between_prepare_count.store(0);
        release_scene_texture_cache();
    };
    auto pending_uploads = [&]() {
        std::lock_guard<std::mutex> lock(scene_upload_mutex);
        return scene_pending_uploads;
    };

    try {
        result.repeater_cache = debug_check_repeater_cache();
        const auto number_text = [](double value) {
            char text[64]{};
            std::snprintf(text, sizeof(text), "%.0f", value);
            return std::string(text);
        };
        const double size_upper = std::ldexp(1.0, std::numeric_limits<size_t>::digits);
        const double size_previous = std::nextafter(size_upper, 0.0);
        const auto previous_index = canvas3d_scene_signal_speed_index(number_text(size_previous));
        result.numeric_boundaries =
            canvas3d_scene_signal_speed_index("0") == size_t{0} &&
            canvas3d_scene_signal_speed_index("1") == size_t{1} &&
            previous_index && *previous_index == static_cast<size_t>(size_previous) &&
            !canvas3d_scene_signal_speed_index(number_text(size_upper)) &&
            !canvas3d_scene_signal_speed_index(number_text(std::nextafter(
                size_upper, std::numeric_limits<double>::infinity()))) &&
            !canvas3d_scene_signal_speed_index("-1") &&
            !canvas3d_scene_signal_speed_index("0.5") &&
            !canvas3d_scene_signal_speed_index("nan") &&
            !canvas3d_scene_signal_speed_index("inf");
        const double signed_upper = std::ldexp(1.0, std::numeric_limits<long long>::digits);
        const double signed_previous = std::nextafter(signed_upper, 0.0);
        Canvas3DRepeaterSegment boundary_repeater;
        boundary_repeater.begin_distance = 0.0;
        boundary_repeater.end_distance = signed_upper * 2.0;
        boundary_repeater.interval = 1.0;
        SceneRepeaterIndexRange boundary_range;
        result.numeric_boundaries = result.numeric_boundaries &&
            scene_repeater_index_range(boundary_repeater, signed_previous, signed_previous, boundary_range) &&
            boundary_range.first == static_cast<long long>(signed_previous) &&
            boundary_range.last == boundary_range.first &&
            !scene_repeater_index_range(boundary_repeater, signed_upper, signed_upper, boundary_range) &&
            !scene_repeater_index_range(boundary_repeater,
                std::nextafter(signed_upper, std::numeric_limits<double>::infinity()),
                boundary_repeater.end_distance, boundary_range) &&
            scene_repeater_index_range(boundary_repeater, 0.0, signed_upper, boundary_range) &&
            boundary_range.first == 0 &&
            boundary_range.last == std::numeric_limits<long long>::max();

        const auto check_index_visit = [](SceneRepeaterIndexRange range,
                                         size_t expected_count) {
            size_t count = 0;
            long long last = range.first;
            visit_scene_repeater_indices(range, [&](long long index) {
                last = index;
                return ++count <= expected_count;
            });
            return count == expected_count &&
                (count == 0 || last == range.last);
        };
        result.numeric_boundaries = result.numeric_boundaries &&
            check_index_visit({1, 0}, 0) && check_index_visit({0, 0}, 1) &&
            check_index_visit({2, 5}, 4) &&
            check_index_visit({static_cast<long long>(signed_previous),
                               std::numeric_limits<long long>::max()}, 1024) &&
            check_index_visit({std::numeric_limits<long long>::max(),
                               std::numeric_limits<long long>::max()}, 1);
        size_t limited_count = 0;
        visit_scene_repeater_indices({0, std::numeric_limits<long long>::max()},
            [&](long long) { return ++limited_count < k_scene_repeater_instance_limit; });
        result.numeric_boundaries = result.numeric_boundaries &&
            limited_count == k_scene_repeater_instance_limit;

        reset_scene_worker_state();
        ModelLoaderClient::debug_reset_counts();
        scene_models.try_emplace("normal");
        SceneModelLoadRequest normal;
        normal.key = "normal";
        normal.source_path = valid_model_path;
        start_scene_model_worker({normal});
        if (scene_worker.joinable()) scene_worker.join();
        const std::vector<CpuModelData> normal_outputs = pending_uploads();
        result.normal_worker = !scene_worker_running.load() &&
            normal_outputs.size() == 1 && normal_outputs[0].ok &&
            normal_outputs[0].scene_key == "normal";
        result.put_between_preparation = g_debug_put_between_prepare_count.load() == 0;
        record_release_counts();

        std::string texture_error;
        ID3D11ShaderResourceView* allocation_texture = nullptr;
        debug_texture_allocation_throw_countdown.store(1);
        const bool allocation_texture_loaded = load_texture(
            valid_texture_path, &allocation_texture, texture_error);
        result.texture_allocation_cleanup = !allocation_texture_loaded &&
            !allocation_texture && !texture_error.empty();
        release_com(allocation_texture);
        debug_texture_allocation_throw_countdown.store(0);

        release_scene_texture_cache();
        scene_stats_value.texture_cache_hit_count = 0;
        scene_stats_value.texture_cache_miss_count = 0;
        ID3D11ShaderResourceView* first_texture = nullptr;
        ID3D11ShaderResourceView* second_texture = nullptr;
        bool first_has_alpha = false;
        bool second_has_alpha = false;
        std::string first_texture_error;
        std::string second_texture_error;
        const bool first_texture_loaded = load_scene_texture(
            valid_texture_path, &first_texture, first_texture_error,
            &first_has_alpha);
        const bool second_texture_loaded = load_scene_texture(
            valid_texture_path, &second_texture, second_texture_error,
            &second_has_alpha);
        result.texture_cache_reuse = first_texture_loaded &&
            second_texture_loaded && first_texture &&
            second_texture == first_texture &&
            first_has_alpha == second_has_alpha &&
            scene_texture_cache.size() == 1 &&
            scene_stats_value.texture_cache_miss_count == 1 &&
            scene_stats_value.texture_cache_hit_count == 1;
        release_com(first_texture);
        release_com(second_texture);
        release_scene_texture_cache();

        if (!normal_outputs.empty() && normal_outputs[0].ok) {
            CpuModelData upload_failure = normal_outputs[0];
            upload_failure.scene_key = "upload-failure";
            upload_failure.shared_model_key.clear();
            scene_models.try_emplace(upload_failure.scene_key);
            debug_scene_index_buffer_failure_countdown.store(1);
            std::string upload_error;
            const bool upload_succeeded =
                upload_scene_model(upload_failure, upload_error);
            const auto failed_model = scene_models.find(upload_failure.scene_key);
            result.upload_failure_cleanup = !upload_succeeded &&
                failed_model != scene_models.end() &&
                failed_model->second.state == SceneModelGpu::State::Failed &&
                !failed_model->second.vertex_buffer &&
                !failed_model->second.index_buffer &&
                !failed_model->second.instance_buffer &&
                failed_model->second.parts.empty() &&
                failed_model->second.materials.empty() &&
                !failed_model->second.error.empty() && !upload_error.empty();
            debug_scene_index_buffer_failure_countdown.store(0);
        }

        Canvas3DTrackPath left_track;
        left_track.points.push_back(Canvas3DTrackPoint{});
        Canvas3DTrackPath right_track = left_track;
        right_track.points.front().x = 1.0;
        for (const bool include_regular : {false, true}) {
            reset_scene_worker_state();
            ModelLoaderClient::debug_reset_counts();
            SceneModelLoadRequest between = normal;
            between.key = "between-a";
            between.put_between.enabled = true;
            between.put_between.own_track = &left_track;
            between.put_between.track1 = &left_track;
            between.put_between.track2 = &right_track;
            SceneModelLoadRequest second_between = between;
            second_between.key = "between-b";
            second_between.put_between.flag = 1;
            std::vector<SceneModelLoadRequest> requests{between, second_between};
            if (include_regular) requests.push_back(normal);
            for (const SceneModelLoadRequest& request : requests) scene_models.try_emplace(request.key);
            start_scene_model_worker(requests);
            if (scene_worker.joinable()) scene_worker.join();
            const std::vector<CpuModelData> outputs = pending_uploads();
            bool outputs_match = !normal_outputs.empty() && outputs.size() == requests.size();
            if (outputs_match) {
                for (const SceneModelLoadRequest& request : requests) {
                    const auto output = std::find_if(outputs.begin(), outputs.end(),
                        [&](const CpuModelData& candidate) { return candidate.scene_key == request.key; });
                    if (output == outputs.end() || !output->ok ||
                        output->vertices.size() != normal_outputs[0].vertices.size()) {
                        outputs_match = false;
                        break;
                    }
                    for (size_t i = 0; i < output->vertices.size(); ++i) {
                        const GpuVertex& actual = output->vertices[i];
                        const GpuVertex& expected = normal_outputs[0].vertices[i];
                        if (std::abs(actual.px - expected.px) > 1e-6f ||
                            std::abs(actual.py - expected.py) > 1e-6f ||
                            std::abs(actual.pz - expected.pz) > 1e-6f) {
                            outputs_match = false;
                            break;
                        }
                    }
                }
            }
            result.put_between_preparation = result.put_between_preparation &&
                outputs_match && g_debug_put_between_prepare_count.load() == 1;
            record_release_counts();
        }

        reset_scene_worker_state();
        ModelLoaderClient::debug_reset_counts();
        debug_copy_cpu_model_throw_countdown.store(1);
        scene_models.try_emplace("copy-failure");
        SceneModelLoadRequest copy_failure;
        copy_failure.key = "copy-failure";
        copy_failure.source_path = valid_model_path;
        start_scene_model_worker({copy_failure});
        if (scene_worker.joinable()) scene_worker.join();
        const std::vector<CpuModelData> failure_outputs = pending_uploads();
        result.copy_exception = !scene_worker_running.load() &&
            failure_outputs.size() == 1 && !failure_outputs[0].ok &&
            failure_outputs[0].error.find("debug injected CPU model copy failure") !=
                std::string::npos;
        record_release_counts();

        reset_scene_worker_state();
        stop_scene_put_between_preview_worker();
        ModelLoaderClient::debug_reset_counts();
        g_debug_put_between_derive_throw_countdown.store(1);
        start_scene_put_between_preview_worker();
        if (scene_put_between_preview_worker.joinable()) {
            ScenePutBetweenPreviewJob job;
            job.sequence = 1;
            job.geometry_generation = 1;
            job.request.key = "put-between-failure";
            job.request.source_path = valid_model_path;
            job.request.put_between.enabled = true;
            {
                std::lock_guard<std::mutex> lock(scene_put_between_preview_mutex);
                scene_put_between_preview_latest_sequence = job.sequence;
                scene_put_between_preview_pending = std::move(job);
                scene_put_between_preview_completed.reset();
            }
            scene_put_between_preview_cv.notify_one();
            for (int attempt = 0; attempt < 5000; ++attempt) {
                {
                    std::lock_guard<std::mutex> lock(scene_put_between_preview_mutex);
                    if (scene_put_between_preview_completed) {
                        const ScenePutBetweenPreviewResult& completed =
                            *scene_put_between_preview_completed;
                        result.put_between_exception = completed.source &&
                            !completed.source->ok && !completed.derived.ok &&
                            completed.derived.error.find(
                                "debug injected PutBetween derivation failure") !=
                                std::string::npos;
                        break;
                    }
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }
        stop_scene_put_between_preview_worker();
        record_release_counts();

        auto start_artificial_worker = [&]() {
            scene_cancel.store(false);
            scene_worker_running.store(true);
            scene_worker = std::thread([this]() noexcept {
                while (!scene_cancel.load(std::memory_order_relaxed)) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }
                scene_worker_running.store(false);
            });
        };
        auto queue_dummy_upload = [&]() {
            std::lock_guard<std::mutex> lock(scene_upload_mutex);
            CpuModelData dummy;
            dummy.scene_key = "stale";
            scene_pending_uploads.push_back(std::move(dummy));
        };
        auto pending_upload_count = [&]() {
            std::lock_guard<std::mutex> lock(scene_upload_mutex);
            return scene_pending_uploads.size();
        };

        reset_scene_worker_state();
        scene_models.try_emplace("keep");
        scene_models.try_emplace("remove");
        start_artificial_worker();
        queue_dummy_upload();
        SceneModelLoadRequest keep_request;
        keep_request.key = "keep";
        keep_request.source_path = valid_model_path;
        std::map<std::string, SceneModelLoadRequest> subset_requests;
        subset_requests.emplace(keep_request.key, keep_request);
        const std::vector<SceneModelLoadRequest> subset_to_load =
            reconcile_dynamic_scene_model_requests(subset_requests);
        result.subset_requeue = !scene_worker.joinable() &&
            !scene_worker_running.load() && pending_upload_count() == 0 &&
            scene_models.size() == 1 && scene_models.count("keep") == 1 &&
            subset_to_load.size() == 1 && subset_to_load[0].key == "keep";

        reset_scene_worker_state();
        scene_models.try_emplace("remove-only");
        start_artificial_worker();
        queue_dummy_upload();
        const std::map<std::string, SceneModelLoadRequest> empty_requests;
        const std::vector<SceneModelLoadRequest> removal_to_load =
            reconcile_dynamic_scene_model_requests(empty_requests);
        result.removal_only_cancel = !scene_worker.joinable() &&
            !scene_worker_running.load() && pending_upload_count() == 0 &&
            scene_models.empty() && removal_to_load.empty();
        reset_scene_worker_state();

        ModelLoaderClient::debug_reset_counts();
        Canvas3DScene reload_fixture;
        reload_fixture.max_distance = 1.0;
        Canvas3DModelInstance reload_instance;
        reload_instance.model_path = valid_model_path;
        reload_fixture.instances.push_back(std::move(reload_instance));
        const auto load_fixture = [&](bool preserve_models) {
            std::string load_error;
            if (!load_scene(reload_fixture, load_error, preserve_models, true)) {
                throw std::runtime_error("scene reload fixture failed: " + load_error);
            }
            if (scene_worker.joinable()) scene_worker.join();
            const auto outputs = pending_uploads();
            upload_pending_scene_models();
            return outputs;
        };
        const auto fixture_model = [&]() -> const SceneModelGpu& {
            const auto found = scene_models.find(valid_model_path);
            if (scene_models.size() != 1 || found == scene_models.end()) {
                throw std::runtime_error("scene reload fixture model is missing");
            }
            return found->second;
        };
        const auto ready_with_width = [&](float width) {
            const auto& model = fixture_model();
            return model.state == SceneModelGpu::State::Ready &&
                model.vertex_buffer && model.index_buffer && model.index_count == 3 &&
                model.bounds_max.x == width;
        };
        const auto output_has_width = [&](const std::vector<CpuModelData>& outputs, float width) {
            return outputs.size() == 1 && outputs.front().ok &&
                outputs.front().path == valid_model_path &&
                outputs.front().scene_key == valid_model_path &&
                outputs.front().bounds_max.x == width &&
                std::any_of(outputs.front().vertices.begin(), outputs.front().vertices.end(),
                    [width](const GpuVertex& vertex) { return vertex.px == width; });
        };
        const auto initial_outputs = load_fixture(false);
        const bool initial_ready = output_has_width(initial_outputs, 1.0f) &&
            ready_with_width(1.0f) && ModelLoaderClient::debug_successful_load_count() == 1;
        ID3D11Buffer* const initial_vertex_buffer = fixture_model().vertex_buffer;

        // The caller owns this model in its temporary fixture directory.
        // Change the source at the same path after the first real load.
        {
            std::ofstream model(std::filesystem::path(utf8_to_wide(valid_model_path)),
                                std::ios::binary | std::ios::trunc);
            model << "xof 0303txt 0032\n"
                     "Mesh {\n"
                     "3;\n"
                     "0.0;0.0;0.0;,\n"
                     "2.0;0.0;0.0;,\n"
                     "0.0;1.0;0.0;;\n"
                     "1;\n"
                     "3;0,1,2;;\n"
                     "}\n";
            model.close();
            if (!model) throw std::runtime_error("could not update scene reload fixture");
        }
        const auto geometry_outputs = load_fixture(true);
        result.geometry_model_load_count = ModelLoaderClient::debug_successful_load_count();
        result.geometry_model_bounds_max_x = fixture_model().bounds_max.x;
        result.geometry_model_reuse = initial_ready && geometry_outputs.empty() &&
            !scene_worker_running.load() && result.geometry_model_load_count == 1 &&
            ready_with_width(1.0f) && fixture_model().vertex_buffer == initial_vertex_buffer;
        const auto full_outputs = load_fixture(false);
        result.full_model_load_count = ModelLoaderClient::debug_successful_load_count();
        result.full_model_bounds_max_x = fixture_model().bounds_max.x;
        result.full_model_reload = initial_ready && output_has_width(full_outputs, 2.0f) &&
            !scene_worker_running.load() && result.full_model_load_count == 2 && ready_with_width(2.0f);
        record_release_counts();
        clear_scene();
    } catch (const std::exception& error) {
        result.error = error.what();
        stop_scene_put_between_preview_worker();
        reset_scene_worker_state();
    } catch (...) {
        result.error = "unknown scene loader contract error";
        stop_scene_put_between_preview_worker();
        reset_scene_worker_state();
    }
    return result;
}

#endif
