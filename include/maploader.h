/*
 * Copyright (c) 2026 Sapporo_ningyo
 *
 * Licensed under Apache License 2.0; see LICENSE and NOTICE.
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

#include "maploader_snapshot.h"

#if defined(_WIN32)
#  if defined(MAPLOADER_EXPORTS)
#    define KV_API __declspec(dllexport)
#  elif defined(MAPLOADER_RUNTIME_DISPATCH)
#    define KV_API
#  else
#    define KV_API __declspec(dllimport)
#  endif
#else
#  define KV_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*KvLogCallback)(const char* message);

KV_API void kv_set_log_callback(KvLogCallback callback);

/* Returns KV_MAPLOADER_API_VERSION. The bundled executable requires an exact
   match and never falls back to a legacy transport. */
KV_API uint32_t kv_api_version(void);

#define KV_LOAD_PREVIEW (1u << 0)
#define KV_LOAD_EDIT_METADATA (1u << 1)

/* Returns an opaque map handle owned by the caller. Release it exactly once
   with kv_free(). KV_LOAD_PREVIEW skips source/edit metadata when
   KV_LOAD_EDIT_METADATA is not also set. */
KV_API void* kv_load_map_ex(const char* path, double unit_distance, unsigned flags);

/* Regenerates geometry in-place for an existing handle and invalidates the
   regular geometry in KvMapSnapshot. */
KV_API int kv_generate_geometry(
    void* handle,
    double unit_distance,
    int has_arbitrary_distribution,
    double arbitrary_start,
    double arbitrary_end,
    double arbitrary_step);
/* Generates or reuses denser geometry in a separate 3D scene cache without
   changing the regular geometry buffers or parser state. Scene buffer views
   remain valid until the scene cache is regenerated or the handle is freed. */
KV_API int kv_generate_scene_geometry(
    void* handle,
    double unit_distance,
    double min_step,
    double max_step,
    double max_angle_degrees,
    double max_chord_error);
/* Copies versioned, handle-owned typed views into caller-provided top-level
   structs. Nested memory is read-only and must never be freed by the caller. */
KV_API int kv_get_map_snapshot(void* handle, uint32_t version,
                               KvMapSnapshot* out_snapshot, uint64_t out_size);
KV_API int kv_get_scene_geometry_snapshot(void* handle, uint32_t version,
                                          KvSceneGeometrySnapshot* out_snapshot,
                                          uint64_t out_size);

/* Typed edit transport. Input views only need to remain valid for the call.
   Returned target/report views are handle-owned and are invalidated as
   documented by maploader_snapshot.h and the API guide. */
KV_API int kv_get_edit_target_typed(void* handle, KvUtf8View edit_id,
                                    KvEditTargetSnapshot* out_target,
                                    uint64_t out_size);
KV_API int kv_edit_dry_run_typed(void* handle, const KvEditBatch* batch,
                                 KvEditReportSnapshot* out_report,
                                 uint64_t out_size);
KV_API int kv_edit_apply_to_memory_typed(void* handle, const KvEditBatch* batch,
                                         KvEditReportSnapshot* out_report,
                                         uint64_t out_size);
KV_API int kv_edit_apply_typed(void* handle, const KvEditBatch* batch,
                               KvEditReportSnapshot* out_report,
                               uint64_t out_size);
KV_API int kv_edit_commit_typed(void* handle, KvEditReportSnapshot* out_report,
                                uint64_t out_size);

/* Returns the complete decoded UTF-8 text for one parsed source file, including
   the active in-memory edit override when present. The source file must belong
   to the loaded map. Release the returned string with kv_free_string(). */
KV_API const char* kv_get_source_text(void* handle, const char* file_path);

/* Discards in-memory edited source overrides and reparses the active handle from
   the current source files. Does not write source files. Returns nonzero on
   success. */
KV_API int kv_edit_reset_memory(void* handle);

/* Returns a thread-local error string owned by maploader.dll. Do not free it. */
KV_API const char* kv_get_last_error(void);

/* Releases a handle returned by kv_load_map_ex(). Passing NULL is allowed. */
KV_API void kv_free(void* handle);

/* Releases strings returned by kv_get_source_text(). Passing NULL is allowed. */
KV_API void kv_free_string(const char* text);

#ifdef __cplusplus
}
#endif
