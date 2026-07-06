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
using kme::maploader::detail::Matrix;
using kme::maploader::detail::SourceTextOverrides;

KvDoubleBuffer make_buffer(const Matrix& m) {
    return {m.data.empty() ? nullptr : m.data.data(), m.rows, m.cols};
}

} // namespace

extern "C" {

KV_API void kv_set_log_callback(KvLogCallback callback) {
    set_log_callback(callback);
}

KV_API void* kv_load_map(const char* path, double unit_distance) {
    try {
        if (!path) throw std::runtime_error("path is null");
        std::filesystem::path map_path = path_from_utf8(path);
        auto ctx = kme::maploader::detail::parse_map_context(
            map_path, unit_distance, SourceTextOverrides{}, false, {0.0, 0.0, 0.0});
        log_info(path_to_utf8(map_path.filename()) + " loaded");
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
        if (ctx->owntrack_buffer.rows == 0) {
            kme::maploader::detail::generate_geometry(*ctx, unit_distance, false, 0.0, 0.0, 0.0);
        }

        const bool has_arb = ctx->has_cp_arbdistribution;
        const std::array<double, 3> arb = ctx->cp_arbdistribution;
        Matrix baseline = ctx->owntrack_buffer;
        std::vector<double> extra = kme::maploader::detail::build_scene_adaptive_controlpoints(
            *ctx, baseline, min_step, max_step, max_angle_degrees, max_chord_error);
        kme::maploader::detail::generate_geometry(*ctx, unit_distance, has_arb, arb[0], arb[1], arb[2], &extra);
        return 1;
    } catch (const std::exception& e) {
        set_last_error(e.what());
        log_error(e.what());
        return 0;
    }
}

KV_API KvDoubleBuffer kv_get_owntrack_buffer(void* handle) {
    if (!handle) return {nullptr, 0, 0};
    return make_buffer(static_cast<MapContext*>(handle)->owntrack_buffer);
}

KV_API KvDoubleBuffer kv_get_curveradius_buffer(void* handle) {
    if (!handle) return {nullptr, 0, 0};
    return make_buffer(static_cast<MapContext*>(handle)->curveradius_buffer);
}

KV_API size_t kv_get_othertrack_count(void* handle) {
    if (!handle) return 0;
    return static_cast<MapContext*>(handle)->othertrack_order.size();
}

KV_API const char* kv_get_othertrack_key(void* handle, size_t index) {
    if (!handle) return nullptr;
    auto* ctx = static_cast<MapContext*>(handle);
    if (index >= ctx->othertrack_order.size()) return nullptr;
    return ctx->othertrack_order[index].c_str();
}

KV_API KvDoubleBuffer kv_get_othertrack_buffer(void* handle, const char* key) {
    if (!handle || !key) return {nullptr, 0, 0};
    auto* ctx = static_cast<MapContext*>(handle);
    std::string k = kme::maploader::detail::ascii_lower(key);
    auto it = ctx->othertrack_buffers.find(k);
    if (it == ctx->othertrack_buffers.end()) return {nullptr, 0, 0};
    return make_buffer(it->second);
}

KV_API KvDoubleBuffer kv_get_structure_puts(void* handle) {
    if (!handle) return {nullptr, 0, 0};
    return make_buffer(static_cast<MapContext*>(handle)->structure_put_buffer);
}

KV_API const char* kv_get_ir_json_ex(void* handle, unsigned flags) {
    try {
        if (!handle) throw std::runtime_error("handle is null");
        auto* ctx = static_cast<MapContext*>(handle);
        flags = kme::maploader::detail::normalize_ir_json_flags(flags);
        auto& cache = ctx->ir_json_cache_by_flags[flags];
        if (cache.empty()) {
            kme::maploader::detail::ScopedTimer timer(&ctx->timing.json_seconds);
            cache = kme::maploader::detail::build_ir_json(*ctx, flags);
        }
        if (!ctx->load_timing_logged) {
            kme::maploader::detail::log_load_timing(*ctx);
            ctx->load_timing_logged = true;
        }
        return copy_c_string(cache);
    } catch (const std::exception& e) {
        set_last_error(e.what());
        return nullptr;
    }
}

KV_API const char* kv_get_ir_json(void* handle) {
    return kv_get_ir_json_ex(handle, KV_IR_JSON_FULL_EDIT | KV_IR_JSON_FULL_STATEMENT_SOURCE);
}

KV_API const char* kv_get_edit_target_info(void* handle, const char* edit_id) {
    try {
        if (!handle) throw std::runtime_error("handle is null");
        auto* ctx = static_cast<MapContext*>(handle);
        return copy_c_string(kme::maploader::detail::edit_target_info_json(*ctx, edit_id ? edit_id : ""));
    } catch (const std::exception& e) {
        set_last_error(e.what());
        return nullptr;
    }
}

KV_API const char* kv_edit_dry_run(void* handle, const char* changes_json) {
    try {
        if (!handle) throw std::runtime_error("handle is null");
        auto* ctx = static_cast<MapContext*>(handle);
        std::vector<MapEditChange> changes = kme::maploader::detail::parse_edit_changes_json(changes_json);
        MapEditReport report = kme::maploader::detail::build_edit_report(*ctx, changes, false);
        return copy_c_string(kme::maploader::detail::report_json(report));
    } catch (const std::exception& e) {
        set_last_error(e.what());
        return nullptr;
    }
}

KV_API const char* kv_edit_apply_to_memory(void* handle, const char* changes_json) {
    try {
        if (!handle) throw std::runtime_error("handle is null");
        auto* ctx = static_cast<MapContext*>(handle);
        std::vector<MapEditChange> changes = kme::maploader::detail::parse_edit_changes_json(changes_json);
        MapEditReport report = kme::maploader::detail::build_edit_report(*ctx, changes, false);
        if (report.ok()) {
            try {
                kme::maploader::detail::apply_edit_report_to_memory(*ctx, report);
            } catch (const std::exception& e) {
                report.blocking_errors.push_back(std::string("edited cache reload failed: ") + e.what());
            }
        }
        return copy_c_string(kme::maploader::detail::report_json(report));
    } catch (const std::exception& e) {
        set_last_error(e.what());
        return nullptr;
    }
}

KV_API const char* kv_edit_apply(void* handle, const char* changes_json) {
    try {
        if (!handle) throw std::runtime_error("handle is null");
        auto* ctx = static_cast<MapContext*>(handle);
        std::vector<MapEditChange> changes = kme::maploader::detail::parse_edit_changes_json(changes_json);
        MapEditReport report = kme::maploader::detail::build_edit_report(*ctx, changes, true);
        return copy_c_string(kme::maploader::detail::report_json(report));
    } catch (const std::exception& e) {
        set_last_error(e.what());
        return nullptr;
    }
}

KV_API const char* kv_edit_commit(void* handle) {
    try {
        if (!handle) throw std::runtime_error("handle is null");
        auto* ctx = static_cast<MapContext*>(handle);
        MapEditReport report = kme::maploader::detail::commit_memory_edits(*ctx);
        return copy_c_string(kme::maploader::detail::report_json(report));
    } catch (const std::exception& e) {
        set_last_error(e.what());
        return nullptr;
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
