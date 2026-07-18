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

} // namespace

extern "C" {

KV_API void kv_set_log_callback(KvLogCallback callback) {
    set_log_callback(callback);
}

KV_API uint32_t kv_api_version(void) {
    return KV_MAPLOADER_API_VERSION;
}

KV_API void* kv_load_map(const char* path, double unit_distance) {
    return kv_load_map_ex(path, unit_distance, KV_LOAD_EDIT_METADATA);
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
        kme::maploader::detail::invalidate_map_snapshot(*ctx, false, true);
        kme::maploader::detail::invalidate_scene_geometry_snapshot(*ctx, true);
        kme::maploader::detail::generate_geometry(*ctx, unit_distance, has_arbitrary_distribution != 0,
                                                  arbitrary_start, arbitrary_end, arbitrary_step);
        return 1;
    } catch (const std::exception& e) {
        set_last_error(e.what());
        log_error(e.what());
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
            ctx->scene_owntrack_buffer = std::move(ctx->owntrack_buffer);
            ctx->scene_othertrack_buffers = std::move(ctx->othertrack_buffers);
            restore_regular_geometry();
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
    }
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
        MapEditReport report = kme::maploader::detail::build_edit_report(*ctx, changes, false);
        *out_report = kme::maploader::detail::build_edit_report_snapshot(*ctx, report);
        return 1;
    } catch (const std::exception& e) {
        set_last_error(e.what());
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
        MapEditReport report = kme::maploader::detail::build_edit_report(*ctx, changes, false);
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
        MapEditReport report = kme::maploader::detail::build_edit_report(*ctx, changes, true);
        *out_report = kme::maploader::detail::build_edit_report_snapshot(*ctx, report);
        return 1;
    } catch (const std::exception& e) {
        set_last_error(e.what());
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
