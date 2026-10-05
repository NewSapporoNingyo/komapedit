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
#include "maploader.h"
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

namespace {
bool model_bounds_contract() {
    struct Fixture {
        std::filesystem::path directory = std::filesystem::temp_directory_path() /
            ("komapedit-model-bounds-" + std::to_string(
                std::chrono::steady_clock::now().time_since_epoch().count()));
        bool owned = false;
        ~Fixture() {
            if (!owned) return;
            std::error_code error;
            std::filesystem::remove_all(directory, error);
        }
    } fixture;
    fixture.owned = std::filesystem::create_directory(fixture.directory);
    if (!fixture.owned) return false;
    struct Case {
        const char* name;
        const char* points;
        double center;
        double radius;
        bool valid;
    };
    const Case cases[] = {
        {"ordinary", "0;0;0;,1;0;0;,0;1;0;;", 0.5, std::sqrt(0.5), true},
        {"large", "0;0;0;,5e19;0;0;,0;5e19;0;;", 2.5e19, std::sqrt(2.0) * 2.5e19, true},
        {"large_center", "2e38;2e38;0;,3e38;2e38;0;,2e38;3e38;0;;", 2.5e38, std::sqrt(2.0) * 0.5e38, true},
        {"unrepresentable", "-3e38;-3e38;0;,3e38;-3e38;0;,0;3e38;0;;", 0, 0, false},
        {"infinite", "0;0;0;,1e39;0;0;,0;1;0;;", 0, 0, false},
        {"nan", "0;0;0;,nan;0;0;,0;1;0;;", 0, 0, false},
    };
    ModelLoaderClient loader;
    bool passed = true;
    for (const auto& test : cases) {
        const auto path = fixture.directory / (std::string(test.name) + ".x");
        {
            std::ofstream file(path, std::ios::binary);
            file << "xof 0303txt 0032\nMesh {3;" << test.points << "1;3;0,1,2;;}\n";
            if (!file) return false;
        }
        MlMeshData data{};
        std::string error;
        const bool loaded = loader.load(path.u8string(), data, error);
        if (test.valid) {
            const auto bounds_close = [](float actual, double expected) {
                return std::isfinite(actual) &&
                    std::abs(static_cast<double>(actual) - expected) <=
                        std::max(1.0, std::abs(expected)) * 1e-6;
            };
            passed = loaded && error.empty() && data.vertex_count == 3 &&
                data.index_count == 3 && bounds_close(data.center[0], test.center) &&
                bounds_close(data.center[1], test.center) && bounds_close(data.center[2], 0) &&
                bounds_close(data.radius, test.radius) && passed;
        } else {
            passed = !loaded && !error.empty() && !data.vertices &&
                data.vertex_count == 0 && !data.indices && data.index_count == 0 &&
                !data.parts && data.part_count == 0 && !data.materials &&
                data.material_count == 0 && passed;
        }
        if (loaded) loader.free_model(data);
        passed = !data.vertices && data.vertex_count == 0 &&
            !data.indices && data.index_count == 0 && !data.parts &&
            data.part_count == 0 && !data.materials && data.material_count == 0 && passed;
    }
    return passed;
}

bool repeater_hydration_contract(const std::string& model_path) {
    struct Fixture {
        std::filesystem::path directory = std::filesystem::temp_directory_path() /
            ("komapedit-repeater-hydration-" + std::to_string(
                std::chrono::steady_clock::now().time_since_epoch().count()));
        void* handle = nullptr;
        ~Fixture() {
            if (handle) kv_free(handle);
            std::error_code error;
            std::filesystem::remove_all(directory, error);
        }
    } fixture;
    if (!std::filesystem::create_directory(fixture.directory)) return false;
    const auto map_path = fixture.directory / "map.txt";
    const std::string source =
        "BveTs Map 2.02:utf-8\r\n0; Structure.Load('structures.csv');\r\n"
        "7; Repeater['r'].Begin0(0,0,0,30,'A name;part','missing','C','A name;part');\r\n"
        "100; Repeater['r'].Begin0(0,0,0,10,'C');\r\n"
        "include 'end.txt';\r\n"
        "Repeater['r'].Begin(0,0,0,0,0,0,0,0,0,10,'missing','C');\r\n"
        "200; Repeater['r'].End();\r\n"
        "250; Repeater['absent'].Begin0(0,0,0,10,'missing','also missing');\r\n"
        "300; Repeater['absent'].End();\r\n"
        "400; Repeater['open'].Begin0(0,0,0,25,'C');\r\n500;\r\n";
    {
        std::ofstream map(map_path, std::ios::binary);
        map << source;
        std::ofstream list(fixture.directory / "structures.csv", std::ios::binary);
        list << "BveTs Structure List 2.00:utf-8\r\nA name;part," << model_path
             << "\r\nC," << model_path << "\r\n";
        std::ofstream child(fixture.directory / "end.txt", std::ios::binary);
        child << "BveTs Map 2.02:utf-8\r\n100; Repeater['r'].End();\r\n";
        if (!map || !list || !child) return false;
    }
    fixture.handle = kv_load_map_ex(map_path.u8string().c_str(), 25,
                                    KV_LOAD_PREVIEW | KV_LOAD_EDIT_METADATA);
    KvMapSnapshot snapshot{};
    if (!fixture.handle || !kv_get_map_snapshot(fixture.handle, KV_MAP_SNAPSHOT_VERSION,
                                                &snapshot, sizeof(snapshot))) return false;
    MapModel model = hydrate_map_snapshot(snapshot, map_path.u8string(), 0);
    if (model.repeaters.empty() || repeater_structure_keys(model.repeaters.front()) !=
        std::vector<std::string>{"A name;part", "missing", "C", "A name;part"}) return false;
    Canvas3DScene scene;
    Canvas3DTrackPath track;
    track.key = "0";
    for (double distance : {0.0, 500.0}) {
        Canvas3DTrackPoint point;
        point.distance = distance;
        point.z = -distance;
        track.points.push_back(point);
    }
    scene.tracks.push_back(std::move(track));
    if (!populate_canvas3d_scene_dynamic_content(scene, model, -1) || scene.repeaters.size() != 5) return false;
    const auto& paths = scene.repeaters[0].model_paths;
    if (paths.size() != 4 || paths[0].empty() || !paths[1].empty() ||
        paths[2] != paths[0] || paths[3] != paths[0] ||
        scene.repeaters[1].begin_distance != scene.repeaters[1].end_distance ||
        scene_repeater_instance_count(scene.repeaters[1]) != 0 ||
        scene.repeaters[2].model_paths.size() != 2 || !scene.repeaters[2].model_paths[0].empty() ||
        scene.repeaters[3].model_paths != std::vector<std::string>{"", ""} ||
        scene.repeaters[4].begin_distance != 400 || scene.repeaters[4].end_distance != 500 ||
        scene_repeater_instance_count(scene.repeaters[4]) != 4) return false;
    TableRow edited = model.repeaters.front();
    set_inspector_row_field_value(edited, "repeater", "structureKeys.0", "C", 0);
    set_inspector_row_field_value(edited, "repeater", "structureKeys.1", "A name;part", 0);
    set_inspector_row_field_value(edited, "repeater", "structureKeys.count", "2", 0);
    if (repeater_structure_keys(edited) != std::vector<std::string>{"C", "A name;part"} ||
        edited.cells.count("_structureKeys.2") != 0) return false;
    std::ifstream unchanged(map_path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(unchanged), {}) == source;
}
} // namespace

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
        debug_scene_log_failure.store(false);
        debug_helper_start_failure_at.store(0);
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
        result.model_bounds = model_bounds_contract();
        CpuModelData radius_overflow;
        const float maximum_coordinate = std::numeric_limits<float>::max();
        GpuVertex positive_corner{}, negative_corner{};
        positive_corner.px = positive_corner.py = positive_corner.pz = maximum_coordinate;
        negative_corner.px = negative_corner.py = negative_corner.pz = -maximum_coordinate;
        radius_overflow.vertices = {positive_corner, negative_corner};
        result.model_bounds = !update_cpu_model_bounds(radius_overflow) && result.model_bounds;
        positive_corner.px = positive_corner.py = positive_corner.pz = 1.0f;
        negative_corner.px = negative_corner.py = negative_corner.pz = -1.0f;
        radius_overflow.vertices = {positive_corner, negative_corner};
        result.model_bounds = update_cpu_model_bounds(radius_overflow) &&
            std::abs(radius_overflow.radius - std::sqrt(3.0f)) < 1e-6f && result.model_bounds;
        result.repeater_cache = debug_check_repeater_cache() && repeater_hydration_contract(valid_model_path);
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
        debug_scene_log_failure.store(true);
        scene_models.try_emplace("copy-failure");
        SceneModelLoadRequest copy_failure;
        copy_failure.key = "copy-failure";
        copy_failure.source_path = valid_model_path;
        start_scene_model_worker({copy_failure});
        if (scene_worker.joinable()) scene_worker.join();
        const std::vector<CpuModelData> failure_outputs = pending_uploads();
        result.copy_exception = !scene_worker_running.load() &&
            !debug_scene_log_failure.load() &&
            failure_outputs.size() == 1 && !failure_outputs[0].ok &&
            failure_outputs[0].error.find("debug injected CPU model copy failure") !=
                std::string::npos;
        record_release_counts();

        reset_scene_worker_state();
        ModelLoaderClient::debug_reset_counts();
        scene_model_worker_limit = 3;
        debug_helper_start_failure_at.store(2);
        debug_scene_log_failure.store(true);
        std::vector<SceneModelLoadRequest> helper_requests;
        for (size_t index = 0; index < 3; ++index) {
            SceneModelLoadRequest request;
            request.key = "helper-failure-" + std::to_string(index);
            request.source_path = valid_model_path + ".missing-" + std::to_string(index);
            scene_models.try_emplace(request.key);
            helper_requests.push_back(std::move(request));
        }
        start_scene_model_worker(helper_requests);
        if (scene_worker.joinable()) scene_worker.join();
        const auto helper_outputs = pending_uploads();
        result.copy_exception = result.copy_exception && !scene_worker_running.load() &&
            !debug_scene_log_failure.load() && scene_model_worker_count_value.load() == 2 &&
            helper_outputs.size() == helper_requests.size() &&
            std::all_of(helper_outputs.begin(), helper_outputs.end(), [](const CpuModelData& output) {
                return !output.ok && !output.error.empty();
            });
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
