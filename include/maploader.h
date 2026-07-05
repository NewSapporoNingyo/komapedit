/*
 * Copyright (c) 2026 Sapporo_ningyo
 *
 * Licensed under Apache License 2.0; see LICENSE and NOTICE.
 */

#pragma once

#include <stddef.h>

#if defined(_WIN32)
#  if defined(MAPLOADER_EXPORTS)
#    define KV_API __declspec(dllexport)
#  else
#    define KV_API __declspec(dllimport)
#  endif
#else
#  define KV_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct KvDoubleBuffer {
    /* Non-owning view into an active map handle. Do not free data directly.
       The view stays valid until kv_free(handle) or kv_generate_geometry(handle, ...)
       mutates the handle's internal buffers. */
    const double* data;
    size_t rows;
    size_t cols;
} KvDoubleBuffer;

typedef void (*KvLogCallback)(const char* message);

KV_API void kv_set_log_callback(KvLogCallback callback);

/* Returns an opaque map handle owned by the caller. Release it exactly once
   with kv_free(), even after retrieving buffers or IR JSON from it. */
KV_API void* kv_load_map(const char* path, double unit_distance);

/* Regenerates geometry in-place for an existing handle and invalidates any
   KvDoubleBuffer views previously obtained from that handle. */
KV_API int kv_generate_geometry(
    void* handle,
    double unit_distance,
    int has_arbitrary_distribution,
    double arbitrary_start,
    double arbitrary_end,
    double arbitrary_step);
/* Generates denser geometry for 3D scene preview without changing parser state.
   Existing KvDoubleBuffer views are invalidated just like kv_generate_geometry(). */
KV_API int kv_generate_scene_geometry(
    void* handle,
    double unit_distance,
    double min_step,
    double max_step,
    double max_angle_degrees,
    double max_chord_error);
KV_API KvDoubleBuffer kv_get_owntrack_buffer(void* handle);
KV_API KvDoubleBuffer kv_get_curveradius_buffer(void* handle);
KV_API size_t kv_get_othertrack_count(void* handle);
KV_API const char* kv_get_othertrack_key(void* handle, size_t index);
KV_API KvDoubleBuffer kv_get_othertrack_buffer(void* handle, const char* key);
KV_API KvDoubleBuffer kv_get_structure_puts(void* handle);

#define KV_IR_JSON_COMPACT 0u
#define KV_IR_JSON_FULL_EDIT (1u << 0)
#define KV_IR_JSON_FULL_STATEMENT_SOURCE (1u << 1)

/* Returns a newly allocated UTF-8 JSON string owned by the caller.
   KV_IR_JSON_COMPACT keeps row-level edit/source metadata but skips the full
   edit statements/elements registry. Release it with kv_free_string(). */
KV_API const char* kv_get_ir_json_ex(void* handle, unsigned flags);

/* Returns a newly allocated UTF-8 JSON string owned by the caller.
   Release it with kv_free_string(). */
KV_API const char* kv_get_ir_json(void* handle);

/* Returns source/edit metadata for one editable element as newly allocated
   UTF-8 JSON owned by the caller. Release it with kv_free_string(). */
KV_API const char* kv_get_edit_target_info(void* handle, const char* edit_id);

/* Validates edit changes and returns a newly allocated UTF-8 JSON report.
   Does not write source files. Release it with kv_free_string(). */
KV_API const char* kv_edit_dry_run(void* handle, const char* changes_json);

/* Validates edit changes, reparses the active handle from an in-memory patched
   source cache, and returns a newly allocated UTF-8 JSON report. Does not write
   source files. Release it with kv_free_string(). */
KV_API const char* kv_edit_apply_to_memory(void* handle, const char* changes_json);

/* Validates and writes edit changes, then returns a newly allocated UTF-8 JSON
   report. Release it with kv_free_string(). */
KV_API const char* kv_edit_apply(void* handle, const char* changes_json);

/* Writes in-memory edited source cache entries back to their source files, then
   returns a newly allocated UTF-8 JSON report. Release it with kv_free_string(). */
KV_API const char* kv_edit_commit(void* handle);

/* Returns a thread-local error string owned by maploader.dll. Do not free it. */
KV_API const char* kv_get_last_error(void);

/* Releases a handle returned by kv_load_map(). Passing NULL is allowed. */
KV_API void kv_free(void* handle);

/* Releases strings returned by kv_get_ir_json(). Passing NULL is allowed. */
KV_API void kv_free_string(const char* text);

#ifdef __cplusplus
}
#endif
