/*
 * Copyright (c) 2026 Sapporo_ningyo
 *
 * Licensed under Apache License 2.0; see LICENSE and NOTICE.
 */

#pragma once

#include <stddef.h>

#if defined(_WIN32)
#  if defined(MODEL_LOADER_EXPORTS)
#    define ML_API __declspec(dllexport)
#  else
#    define ML_API __declspec(dllimport)
#  endif
#else
#  define ML_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct MlVertex {
    float px;
    float py;
    float pz;
    float nx;
    float ny;
    float nz;
    float u;
    float v;
} MlVertex;

typedef struct MlMeshPart {
    unsigned int start_index;
    unsigned int index_count;
    unsigned int material_index;
} MlMeshPart;

typedef struct MlMaterial {
    /* Owned by MlMeshData after a successful ml_load_model() call.
       Do not free this field directly; ml_free_model() releases it. */
    char* texture_path;
    float diffuse[4];
} MlMaterial;

typedef struct MlMeshData {
    /* All pointer fields are owned by the struct after ml_load_model() succeeds.
       Release them together with ml_free_model(); do not mix CRT free/delete
       across the DLL boundary. */
    MlVertex* vertices;
    size_t vertex_count;
    unsigned int* indices;
    size_t index_count;
    MlMeshPart* parts;
    size_t part_count;
    MlMaterial* materials;
    size_t material_count;
    float bounds_min[3];
    float bounds_max[3];
    float center[3];
    float radius;
} MlMeshData;

ML_API unsigned int ml_api_version(void);

/* On success, fills out_model with DLL-owned allocations that the caller must
   release with ml_free_model(). On failure, out_model is reset to empty. */
ML_API int ml_load_model(const char* path, MlMeshData* out_model);

/* Releases all allocations in model and resets it to an empty state.
   Passing NULL is allowed. */
ML_API void ml_free_model(MlMeshData* model);

/* Returns a thread-local error string owned by model_loader.dll. Do not free it. */
ML_API const char* ml_get_last_error(void);

#ifdef __cplusplus
}
#endif
