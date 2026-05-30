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
    const double* data;
    size_t rows;
    size_t cols;
} KvDoubleBuffer;

typedef void (*KvLogCallback)(const char* message);

KV_API void kv_set_log_callback(KvLogCallback callback);
KV_API void* kv_load_map(const char* path, double unit_distance);
KV_API int kv_generate_geometry(
    void* handle,
    double unit_distance,
    int has_arbitrary_distribution,
    double arbitrary_start,
    double arbitrary_end,
    double arbitrary_step);
KV_API KvDoubleBuffer kv_get_owntrack_buffer(void* handle);
KV_API KvDoubleBuffer kv_get_curveradius_buffer(void* handle);
KV_API size_t kv_get_othertrack_count(void* handle);
KV_API const char* kv_get_othertrack_key(void* handle, size_t index);
KV_API KvDoubleBuffer kv_get_othertrack_buffer(void* handle, const char* key);
KV_API KvDoubleBuffer kv_get_structure_puts(void* handle);
KV_API const char* kv_get_ir_json(void* handle);
KV_API const char* kv_get_last_error(void);
KV_API void kv_free(void* handle);
KV_API void kv_free_string(const char* text);

#ifdef __cplusplus
}
#endif
