/*
 * Copyright (c) 2026 Sapporo_ningyo
 *
 * Portions of the map parsing and track-geometry design are derived from or
 * reimplemented with reference to kobushi-trackviewer, Copyright (c) 2021-2024
 * konawasabi, licensed under Apache License 2.0.
 *
 * Licensed under Apache License 2.0; see LICENSE and NOTICE.
 */

#define MAPLOADER_EXPORTS
#include "maploader_internal.h"

#include "c_api.h"
#include "diagnostics.h"
#include "scenario_route.h"

#include <cstring>
#include <cstdlib>
#include <limits>
#include <new>

namespace {

using kme::maploader::copy_c_string;
using kme::maploader::last_error_c_str;
using kme::maploader::log_error;
using kme::maploader::log_info;
using kme::maploader::path_from_utf8;
using kme::maploader::path_to_utf8;
using kme::maploader::set_last_error;
using kme::maploader::set_log_callback;
using kme::maploader::detail::MapContext;
using kme::maploader::detail::MapEditChange;
using kme::maploader::detail::MapEditReport;
using kme::maploader::detail::MapParseOptions;
using kme::maploader::detail::Matrix;
using kme::maploader::detail::SourceTextOverrides;

MapParseOptions parse_options_from_load_flags(unsigned flags) {
    MapParseOptions options;
    const bool preview = (flags & KV_LOAD_PREVIEW) != 0;
    const bool edit_metadata = (flags & KV_LOAD_EDIT_METADATA) != 0;
    options.collect_edit_metadata = edit_metadata || !preview;
    return options;
}

size_t checked_scenario_size_add(size_t left, size_t right) {
    if (right > std::numeric_limits<size_t>::max() - left) throw std::bad_alloc();
    return left + right;
}

size_t checked_scenario_size_mul(size_t left, size_t right) {
    if (left != 0 && right > std::numeric_limits<size_t>::max() / left) {
        throw std::bad_alloc();
    }
    return left * right;
}

const KvScenarioSnapshot* allocate_scenario_snapshot(
    const kme::maploader::detail::ScenarioDocument& document) {
    size_t string_bytes = 0;
    auto add_string = [&](const std::string& value) {
        string_bytes = checked_scenario_size_add(string_bytes, value.size());
    };
    add_string(document.title);
    for (const auto& row : document.routes) add_string(row.path_text);
    add_string(document.route_title);
    for (const auto& row : document.vehicles) add_string(row.path_text);
    add_string(document.vehicle_title);
    add_string(document.author);
    add_string(document.image);
    add_string(document.comment);

    const size_t route_bytes = checked_scenario_size_mul(
        document.routes.size(), sizeof(KvScenarioPathWeightRow));
    const size_t vehicle_bytes = checked_scenario_size_mul(
        document.vehicles.size(), sizeof(KvScenarioPathWeightRow));
    size_t allocation_bytes = checked_scenario_size_add(sizeof(KvScenarioSnapshot), route_bytes);
    allocation_bytes = checked_scenario_size_add(allocation_bytes, vehicle_bytes);
    allocation_bytes = checked_scenario_size_add(allocation_bytes, string_bytes);

    char* block = static_cast<char*>(std::malloc(allocation_bytes));
    if (!block) throw std::bad_alloc();
    auto* snapshot = reinterpret_cast<KvScenarioSnapshot*>(block);
    auto* route_rows = reinterpret_cast<KvScenarioPathWeightRow*>(
        block + sizeof(KvScenarioSnapshot));
    auto* vehicle_rows = reinterpret_cast<KvScenarioPathWeightRow*>(
        block + sizeof(KvScenarioSnapshot) + route_bytes);
    char* string_data = block + sizeof(KvScenarioSnapshot) + route_bytes + vehicle_bytes;
    char* cursor = string_data;
    const auto copy_string = [&](const std::string& value) {
        KvStringRef reference{
            static_cast<uint64_t>(cursor - string_data),
            static_cast<uint64_t>(value.size())};
        if (!value.empty()) std::memcpy(cursor, value.data(), value.size());
        cursor += value.size();
        return reference;
    };

    *snapshot = KvScenarioSnapshot{};
    snapshot->version = KV_SCENARIO_SNAPSHOT_VERSION;
    snapshot->structure_size = sizeof(KvScenarioSnapshot);
    snapshot->string_data = string_data;
    snapshot->string_size = string_bytes;
    snapshot->title = copy_string(document.title);
    snapshot->routes = document.routes.empty() ? nullptr : route_rows;
    snapshot->route_count = document.routes.size();
    for (size_t i = 0; i < document.routes.size(); ++i) {
        route_rows[i].path = copy_string(document.routes[i].path_text);
        route_rows[i].weight = document.routes[i].weight;
        route_rows[i].has_explicit_weight = document.routes[i].has_explicit_weight ? 1u : 0u;
        route_rows[i].reserved = 0;
    }
    snapshot->route_title = copy_string(document.route_title);
    snapshot->vehicles = document.vehicles.empty() ? nullptr : vehicle_rows;
    snapshot->vehicle_count = document.vehicles.size();
    for (size_t i = 0; i < document.vehicles.size(); ++i) {
        vehicle_rows[i].path = copy_string(document.vehicles[i].path_text);
        vehicle_rows[i].weight = document.vehicles[i].weight;
        vehicle_rows[i].has_explicit_weight = document.vehicles[i].has_explicit_weight ? 1u : 0u;
        vehicle_rows[i].reserved = 0;
    }
    snapshot->vehicle_title = copy_string(document.vehicle_title);
    snapshot->author = copy_string(document.author);
    snapshot->image = copy_string(document.image);
    snapshot->comment = copy_string(document.comment);
    return snapshot;
}

} // namespace

extern "C" {

KV_API void kv_set_log_callback(KvLogCallback callback) {
    set_log_callback(callback);
}

KV_API uint32_t kv_api_version(void) {
    return KV_MAPLOADER_API_VERSION;
}

KV_API void* kv_load_map_ex(const char* path, double unit_distance, unsigned flags) {
    try {
        if (!path) throw std::runtime_error("path is null");
        std::filesystem::path map_path = path_from_utf8(path);
        MapParseOptions options = parse_options_from_load_flags(flags);
        auto ctx = kme::maploader::detail::parse_map_context(
            map_path, unit_distance, SourceTextOverrides{}, false, {0.0, 0.0, 0.0}, options);
        log_info(path_to_utf8(map_path.filename()) +
                 (options.collect_edit_metadata ? " loaded (edit)" : " loaded (preview)"));
        return ctx.release();
    } catch (const std::exception& e) {
        set_last_error(e.what());
        log_error(e.what());
        return nullptr;
    } catch (...) {
        set_last_error("unknown maploader error");
        return nullptr;
    }
}

KV_API int kv_generate_geometry(void* handle, double unit_distance,
                                int has_arbitrary_distribution,
                                double arbitrary_start,
                                double arbitrary_end,
                                double arbitrary_step) {
    try {
        if (!handle) throw std::runtime_error("handle is null");
        auto* ctx = static_cast<MapContext*>(handle);
        if (has_arbitrary_distribution != 0) {
            kme::maploader::detail::validate_control_point_distribution(
                arbitrary_start, arbitrary_end, arbitrary_step);
        }
        kme::maploader::detail::invalidate_map_snapshot(*ctx, false, true);
        kme::maploader::detail::invalidate_scene_geometry_snapshot(*ctx, true);
        kme::maploader::detail::generate_geometry(*ctx, unit_distance, has_arbitrary_distribution != 0,
                                                  arbitrary_start, arbitrary_end, arbitrary_step);
        return 1;
    } catch (const std::exception& e) {
        set_last_error(e.what());
        log_error(e.what());
        return 0;
    } catch (...) {
        set_last_error("unknown maploader error");
        return 0;
    }
}

KV_API int kv_generate_scene_geometry(void* handle, double unit_distance,
                                      double min_step, double max_step,
                                      double max_angle_degrees,
                                      double max_chord_error) {
    try {
        if (!handle) throw std::runtime_error("handle is null");
        auto* ctx = static_cast<MapContext*>(handle);
        const std::array<double, 5> parameters{
            unit_distance, min_step, max_step, max_angle_degrees, max_chord_error};
        if (ctx->scene_geometry_valid && ctx->scene_geometry_parameters == parameters) {
            return 1;
        }
        kme::maploader::detail::invalidate_scene_geometry_snapshot(*ctx, true);
        if (ctx->owntrack_buffer.rows == 0) {
            kme::maploader::detail::generate_geometry(*ctx, unit_distance, false, 0.0, 0.0, 0.0);
        }

        const bool has_arb = ctx->has_cp_arbdistribution;
        const std::array<double, 3> arb = ctx->cp_arbdistribution;
        const Matrix& baseline = ctx->owntrack_buffer;
        std::vector<double> extra = kme::maploader::detail::build_scene_adaptive_controlpoints(
            *ctx, baseline, min_step, max_step, max_angle_degrees, max_chord_error);

        struct RegularGeometryState {
            double unit_distance;
            Matrix owntrack_buffer;
            Matrix curveradius_buffer;
            Matrix structure_put_buffer;
            std::map<std::string, Matrix> othertrack_buffers;
            std::array<double, 3> cp_arbdistribution;
            std::array<double, 3> cp_arbdistribution_default;
            std::array<double, 2> cp_defaultrange;
            bool has_cp_arbdistribution;
            bool cp_arbdistribution_explicit;
            kme::maploader::detail::LoadTiming timing;
            bool load_timing_logged;
        } regular{
            ctx->unit_distance,
            std::move(ctx->owntrack_buffer),
            std::move(ctx->curveradius_buffer),
            std::move(ctx->structure_put_buffer),
            std::move(ctx->othertrack_buffers),
            ctx->cp_arbdistribution,
            ctx->cp_arbdistribution_default,
            ctx->cp_defaultrange,
            ctx->has_cp_arbdistribution,
            ctx->cp_arbdistribution_explicit,
            std::move(ctx->timing),
            ctx->load_timing_logged};

        auto restore_regular_geometry = [&]() {
            ctx->unit_distance = regular.unit_distance;
            ctx->owntrack_buffer = std::move(regular.owntrack_buffer);
            ctx->curveradius_buffer = std::move(regular.curveradius_buffer);
            ctx->structure_put_buffer = std::move(regular.structure_put_buffer);
            ctx->othertrack_buffers = std::move(regular.othertrack_buffers);
            ctx->cp_arbdistribution = regular.cp_arbdistribution;
            ctx->cp_arbdistribution_default = regular.cp_arbdistribution_default;
            ctx->cp_defaultrange = regular.cp_defaultrange;
            ctx->has_cp_arbdistribution = regular.has_cp_arbdistribution;
            ctx->cp_arbdistribution_explicit = regular.cp_arbdistribution_explicit;
            ctx->timing = std::move(regular.timing);
            ctx->load_timing_logged = regular.load_timing_logged;
        };

        try {
            kme::maploader::detail::generate_geometry(
                *ctx, unit_distance, has_arb, arb[0], arb[1], arb[2], &extra, false);
            Matrix generated_owntrack = std::move(ctx->owntrack_buffer);
            std::map<std::string, Matrix> generated_othertracks = std::move(ctx->othertrack_buffers);
            restore_regular_geometry();
            std::swap(ctx->scene_owntrack_buffer, generated_owntrack);
            ctx->scene_othertrack_buffers.swap(generated_othertracks);
        } catch (...) {
            restore_regular_geometry();
            throw;
        }
        ctx->scene_geometry_parameters = parameters;
        ctx->scene_geometry_valid = true;
        return 1;
    } catch (const std::exception& e) {
        set_last_error(e.what());
        log_error(e.what());
        return 0;
    } catch (...) {
        set_last_error("unknown maploader error");
        return 0;
    }
}

KV_API int kv_get_map_snapshot(void* handle, uint32_t version,
                               KvMapSnapshot* out_snapshot, uint64_t out_size) {
    try {
        if (!handle) throw std::runtime_error("handle is null");
        if (!out_snapshot) throw std::runtime_error("map snapshot output is null");
        if (version != KV_MAP_SNAPSHOT_VERSION) {
            throw std::runtime_error("unsupported map snapshot version");
        }
        if (out_size < sizeof(KvMapSnapshot)) {
            throw std::runtime_error("map snapshot output is too small");
        }
        auto* ctx = static_cast<MapContext*>(handle);
        *out_snapshot = kme::maploader::detail::build_map_snapshot(*ctx);
        if (!ctx->load_timing_logged) {
            kme::maploader::detail::log_load_timing(*ctx);
            ctx->load_timing_logged = true;
        }
        return 1;
    } catch (const std::exception& e) {
        set_last_error(e.what());
        return 0;
    } catch (...) {
        set_last_error("unknown maploader error");
        return 0;
    }
}

KV_API int kv_get_scene_geometry_snapshot(void* handle, uint32_t version,
                                          KvSceneGeometrySnapshot* out_snapshot,
                                          uint64_t out_size) {
    try {
        if (!handle) throw std::runtime_error("handle is null");
        if (!out_snapshot) throw std::runtime_error("scene snapshot output is null");
        if (version != KV_SCENE_GEOMETRY_SNAPSHOT_VERSION) {
            throw std::runtime_error("unsupported scene snapshot version");
        }
        if (out_size < sizeof(KvSceneGeometrySnapshot)) {
            throw std::runtime_error("scene snapshot output is too small");
        }
        auto* ctx = static_cast<MapContext*>(handle);
        if (!ctx->scene_geometry_valid) {
            throw std::runtime_error("scene geometry has not been generated");
        }
        *out_snapshot = kme::maploader::detail::build_scene_geometry_snapshot(*ctx);
        return 1;
    } catch (const std::exception& e) {
        set_last_error(e.what());
        return 0;
    } catch (...) {
        set_last_error("unknown maploader error");
        return 0;
    }
}

KV_API int kv_get_edit_target_typed(void* handle, KvUtf8View edit_id,
                                    KvEditTargetSnapshot* out_target,
                                    uint64_t out_size) {
    try {
        if (!handle) throw std::runtime_error("handle is null");
        if (!out_target) throw std::runtime_error("edit target output is null");
        if (out_size < sizeof(KvEditTargetSnapshot)) {
            throw std::runtime_error("edit target output is too small");
        }
        if (edit_id.length != 0 && !edit_id.data) {
            throw std::runtime_error("editId data is null");
        }
        if (edit_id.length > static_cast<std::uint64_t>(std::numeric_limits<size_t>::max())) {
            throw std::runtime_error("editId is too large");
        }
        std::string id(edit_id.data ? edit_id.data : "", static_cast<size_t>(edit_id.length));
        auto* ctx = static_cast<MapContext*>(handle);
        *out_target = kme::maploader::detail::build_edit_target_snapshot(*ctx, id);
        return 1;
    } catch (const std::exception& e) {
        set_last_error(e.what());
        return 0;
    } catch (...) {
        set_last_error("unknown maploader error");
        return 0;
    }
}

KV_API const char* kv_get_source_text(void* handle, const char* file_path) {
    try {
        if (!handle) throw std::runtime_error("handle is null");
        auto* ctx = static_cast<MapContext*>(handle);
        return copy_c_string(kme::maploader::detail::current_source_text(
            *ctx, file_path ? file_path : ""));
    } catch (const std::exception& e) {
        set_last_error(e.what());
        return nullptr;
    } catch (...) {
        set_last_error("unknown maploader error");
        return nullptr;
    }
}

KV_API int kv_probe_file_kind(const char* path) {
    try {
        if (!path) throw std::runtime_error("path is null");
        return static_cast<int>(kme::maploader::detail::probe_bve_file_kind(
            path_from_utf8(path)));
    } catch (const std::exception& e) {
        set_last_error(e.what());
        return KV_FILE_KIND_UNKNOWN;
    } catch (...) {
        set_last_error("unknown maploader error");
        return KV_FILE_KIND_UNKNOWN;
    }
}

KV_API const KvScenarioRouteCandidate* kv_resolve_scenario_routes(
    const char* scenario_path, uint64_t* out_count) {
    try {
        if (!scenario_path) throw std::runtime_error("scenario path is null");
        if (!out_count) throw std::runtime_error("candidate count output is null");
        std::vector<kme::maploader::detail::ScenarioRouteCandidate> candidates =
            kme::maploader::detail::resolve_scenario_route_candidates(
                path_from_utf8(scenario_path));
        if (candidates.empty()) {
            throw std::runtime_error("scenario has no Route entry");
        }

        // One allocation holds the candidate array and every string so the
        // caller releases the whole result with a single free call.
        size_t string_bytes = 0;
        for (const auto& candidate : candidates) {
            string_bytes += candidate.route_text.size() + 1 +
                candidate.resolved_path.size() + 1;
        }
        const size_t array_bytes =
            candidates.size() * sizeof(KvScenarioRouteCandidate);
        char* block = static_cast<char*>(std::malloc(array_bytes + string_bytes));
        if (!block) throw std::bad_alloc();
        auto* out_array = reinterpret_cast<KvScenarioRouteCandidate*>(block);
        char* cursor = block + array_bytes;
        for (size_t i = 0; i < candidates.size(); ++i) {
            out_array[i].route_text = cursor;
            std::memcpy(cursor, candidates[i].route_text.c_str(),
                        candidates[i].route_text.size() + 1);
            cursor += candidates[i].route_text.size() + 1;
            out_array[i].resolved_path = cursor;
            std::memcpy(cursor, candidates[i].resolved_path.c_str(),
                        candidates[i].resolved_path.size() + 1);
            cursor += candidates[i].resolved_path.size() + 1;
        }
        *out_count = candidates.size();
        return out_array;
    } catch (const std::exception& e) {
        set_last_error(e.what());
        return nullptr;
    } catch (...) {
        set_last_error("unknown maploader error");
        return nullptr;
    }
}

KV_API void kv_free_scenario_candidates(const KvScenarioRouteCandidate* candidates) {
    std::free(const_cast<KvScenarioRouteCandidate*>(candidates));
}

KV_API const KvScenarioSnapshot* kv_load_scenario_snapshot(
    const char* scenario_path, uint32_t version) {
    try {
        if (!scenario_path) throw std::runtime_error("scenario path is null");
        if (version != KV_SCENARIO_SNAPSHOT_VERSION) {
            throw std::runtime_error("unsupported scenario snapshot version");
        }
        return allocate_scenario_snapshot(
            kme::maploader::detail::load_scenario_document(path_from_utf8(scenario_path)));
    } catch (const std::exception& e) {
        set_last_error(e.what());
        return nullptr;
    } catch (...) {
        set_last_error("unknown maploader error");
        return nullptr;
    }
}

KV_API void kv_free_scenario_snapshot(const KvScenarioSnapshot* snapshot) {
    std::free(const_cast<KvScenarioSnapshot*>(snapshot));
}

KV_API int kv_edit_dry_run_typed(void* handle, const KvEditBatch* batch,
                                 KvEditReportSnapshot* out_report,
                                 uint64_t out_size) {
    try {
        if (!handle) throw std::runtime_error("handle is null");
        if (!batch) throw std::runtime_error("edit batch is null");
        if (!out_report) throw std::runtime_error("edit report output is null");
        if (out_size < sizeof(KvEditReportSnapshot)) {
            throw std::runtime_error("edit report output is too small");
        }
        auto* ctx = static_cast<MapContext*>(handle);
        ctx->edit_report_snapshot.reset();
        ctx->edit_target_snapshot.reset();
        std::vector<MapEditChange> changes = kme::maploader::detail::copy_edit_batch(*batch);
        MapEditReport report = kme::maploader::detail::plan_staged_edit_batch(*ctx, changes);
        *out_report = kme::maploader::detail::build_edit_report_snapshot(*ctx, report);
        return 1;
    } catch (const std::exception& e) {
        set_last_error(e.what());
        return 0;
    } catch (...) {
        set_last_error("unknown maploader error");
        return 0;
    }
}

KV_API int kv_edit_apply_to_memory_typed(void* handle, const KvEditBatch* batch,
                                         KvEditReportSnapshot* out_report,
                                         uint64_t out_size) {
    try {
        if (!handle) throw std::runtime_error("handle is null");
        if (!batch) throw std::runtime_error("edit batch is null");
        if (!out_report) throw std::runtime_error("edit report output is null");
        if (out_size < sizeof(KvEditReportSnapshot)) {
            throw std::runtime_error("edit report output is too small");
        }
        auto* ctx = static_cast<MapContext*>(handle);
        ctx->edit_report_snapshot.reset();
        ctx->edit_target_snapshot.reset();
        std::vector<MapEditChange> changes = kme::maploader::detail::copy_edit_batch(*batch);
        MapEditReport report = kme::maploader::detail::plan_staged_edit_batch(*ctx, changes);
        if (report.ok()) {
            try {
                kme::maploader::detail::apply_edit_report_to_memory(*ctx, report);
            } catch (const std::exception& e) {
                report.blocking_errors.push_back(std::string("edited cache reload failed: ") + e.what());
            }
        }
        *out_report = kme::maploader::detail::build_edit_report_snapshot(*ctx, report);
        return 1;
    } catch (const std::exception& e) {
        set_last_error(e.what());
        return 0;
    } catch (...) {
        set_last_error("unknown maploader error");
        return 0;
    }
}

KV_API int kv_edit_reset_memory(void* handle) {
    try {
        if (!handle) throw std::runtime_error("handle is null");
        auto* ctx = static_cast<MapContext*>(handle);
        ctx->edit_report_snapshot.reset();
        ctx->edit_target_snapshot.reset();
        kme::maploader::detail::reset_memory_edits(*ctx);
        return 1;
    } catch (const std::exception& e) {
        set_last_error(e.what());
        return 0;
    } catch (...) {
        set_last_error("unknown maploader error");
        return 0;
    }
}

KV_API int kv_edit_apply_typed(void* handle, const KvEditBatch* batch,
                               KvEditReportSnapshot* out_report,
                               uint64_t out_size) {
    try {
        if (!handle) throw std::runtime_error("handle is null");
        if (!batch) throw std::runtime_error("edit batch is null");
        if (!out_report) throw std::runtime_error("edit report output is null");
        if (out_size < sizeof(KvEditReportSnapshot)) {
            throw std::runtime_error("edit report output is too small");
        }
        auto* ctx = static_cast<MapContext*>(handle);
        ctx->edit_report_snapshot.reset();
        ctx->edit_target_snapshot.reset();
        std::vector<MapEditChange> changes = kme::maploader::detail::copy_edit_batch(*batch);
        MapEditReport report = kme::maploader::detail::plan_staged_edit_batch(*ctx, changes);
        if (report.ok()) {
            kme::maploader::detail::finalize_direct_disk_apply(*ctx, report);
        }
        *out_report = kme::maploader::detail::build_edit_report_snapshot(*ctx, report);
        return 1;
    } catch (const std::exception& e) {
        set_last_error(e.what());
        return 0;
    } catch (...) {
        set_last_error("unknown maploader error");
        return 0;
    }
}

KV_API int kv_edit_commit_typed(void* handle, KvEditReportSnapshot* out_report,
                                uint64_t out_size) {
    try {
        if (!handle) throw std::runtime_error("handle is null");
        if (!out_report) throw std::runtime_error("edit report output is null");
        if (out_size < sizeof(KvEditReportSnapshot)) {
            throw std::runtime_error("edit report output is too small");
        }
        auto* ctx = static_cast<MapContext*>(handle);
        ctx->edit_report_snapshot.reset();
        ctx->edit_target_snapshot.reset();
        MapEditReport report = kme::maploader::detail::commit_memory_edits(*ctx);
        *out_report = kme::maploader::detail::build_edit_report_snapshot(*ctx, report);
        return 1;
    } catch (const std::exception& e) {
        set_last_error(e.what());
        return 0;
    } catch (...) {
        set_last_error("unknown maploader error");
        return 0;
    }
}

KV_API const char* kv_get_last_error(void) {
    return last_error_c_str();
}

KV_API void kv_free(void* handle) {
    delete static_cast<MapContext*>(handle);
}

KV_API void kv_free_string(const char* text) {
    std::free(const_cast<char*>(text));
}

}
