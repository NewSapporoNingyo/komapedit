/*
 * Copyright (c) 2026 Sapporo_ningyo
 *
 * Licensed under Apache License 2.0; see LICENSE and NOTICE.
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

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

typedef struct KvDoubleBuffer {
    /* Non-owning view into an active map handle. Do not free data directly.
       The view stays valid until kv_free(handle) or kv_generate_geometry(handle, ...)
       mutates the handle's internal buffers. */
    const double* data;
    size_t rows;
    size_t cols;
} KvDoubleBuffer;

/* Versioned, process-local typed view used by the interactive preview loader.
   Every pointer and string reference is owned by the map handle. The complete
   view becomes invalid when the handle is freed, reparsed, or its regular
   geometry is regenerated. No snapshot data is written to disk. */
#define KV_PREVIEW_SNAPSHOT_VERSION 1u

typedef struct KvStringRef {
    uint64_t offset;
    uint64_t length;
} KvStringRef;

typedef struct KvPreviewSpan {
    uint64_t offset;
    uint64_t count;
} KvPreviewSpan;

enum KvPreviewValueKind {
    KV_PREVIEW_VALUE_NULL = 0,
    KV_PREVIEW_VALUE_BOOL = 1,
    KV_PREVIEW_VALUE_NUMBER = 2,
    KV_PREVIEW_VALUE_STRING = 3,
    KV_PREVIEW_VALUE_ARRAY = 4
};

typedef struct KvPreviewValue {
    uint32_t kind;
    uint32_t boolean_value;
    double number_value;
    KvStringRef string_value;
    KvPreviewSpan array_values;
} KvPreviewValue;

typedef struct KvPreviewCell {
    KvStringRef key;
    KvPreviewValue value;
} KvPreviewCell;

typedef struct KvPreviewRow {
    KvPreviewSpan cells;
    KvStringRef object_key;
    KvStringRef edit_id;
    KvStringRef source_file_path;
    KvStringRef source_raw_text_preview;
    int32_t source_line;
    int32_t source_column;
} KvPreviewRow;

enum KvPreviewTableKind {
    KV_PREVIEW_TABLE_STATION_LIST = 1,
    KV_PREVIEW_TABLE_STATION_PUT = 2,
    KV_PREVIEW_TABLE_STRUCTURE = 3,
    KV_PREVIEW_TABLE_STRUCTURE_MODEL = 4,
    KV_PREVIEW_TABLE_OTHER_TRAIN = 5,
    KV_PREVIEW_TABLE_OTHER_TRAIN_STOP = 6,
    KV_PREVIEW_TABLE_OTHER_TRAIN_STRUCTURE_KEY = 7,
    KV_PREVIEW_TABLE_OTHER_TRAIN_SOUND_3D_KEY = 8,
    KV_PREVIEW_TABLE_SOUND_LIST = 9,
    KV_PREVIEW_TABLE_STRUCTURE_BETWEEN = 10,
    KV_PREVIEW_TABLE_REPEATER = 11,
    KV_PREVIEW_TABLE_SIGNAL_ASPECT = 12,
    KV_PREVIEW_TABLE_SIGNAL = 13,
    KV_PREVIEW_TABLE_BEACON = 14,
    KV_PREVIEW_TABLE_PRETRAIN = 15,
    KV_PREVIEW_TABLE_IRREGULARITY = 16,
    KV_PREVIEW_TABLE_MAP_SOUND = 17,
    KV_PREVIEW_TABLE_MAP_SOUND_3D = 18,
    KV_PREVIEW_TABLE_ROLLING_NOISE = 19,
    KV_PREVIEW_TABLE_FLANGE_NOISE = 20,
    KV_PREVIEW_TABLE_JOINT_NOISE = 21,
    KV_PREVIEW_TABLE_BACKGROUND = 22,
    KV_PREVIEW_TABLE_ADHESION = 23,
    KV_PREVIEW_TABLE_CAB_ILLUMINANCE = 24,
    KV_PREVIEW_TABLE_FOG = 25
};

typedef struct KvPreviewTable {
    uint32_t kind;
    uint32_t reserved;
    KvPreviewSpan rows;
} KvPreviewTable;

typedef struct KvPreviewFileStructure {
    int64_t parent_index;
    KvStringRef include_path;
    KvStringRef absolute_path;
} KvPreviewFileStructure;

typedef struct KvPreviewSourceFile {
    KvStringRef file_path;
    KvStringRef display_path;
    KvStringRef encoding;
    KvStringRef newline;
    KvStringRef source_hash;
    uint64_t byte_length;
} KvPreviewSourceFile;

typedef struct KvPreviewOtherTrack {
    KvStringRef key;
    double range_min;
    double range_max;
    KvDoubleBuffer points;
} KvPreviewOtherTrack;

typedef struct KvPreviewOwnTrackEvent {
    double distance;
    KvStringRef key;
    KvStringRef flag;
    KvPreviewValue value;
} KvPreviewOwnTrackEvent;

typedef struct KvPreviewSpeedLimit {
    double distance;
    KvPreviewValue speed;
} KvPreviewSpeedLimit;

typedef struct KvPreviewStationPosition {
    double distance;
    KvStringRef key;
} KvPreviewStationPosition;

typedef struct KvPreviewStationName {
    KvStringRef key;
    KvStringRef name;
} KvPreviewStationName;

typedef struct KvPreviewSnapshot {
    uint32_t version;
    uint32_t reserved;
    uint64_t content_revision;
    uint64_t geometry_revision;
    double build_seconds;

    const char* string_data;
    uint64_t string_size;
    KvStringRef root_path;

    const KvPreviewFileStructure* file_structure;
    uint64_t file_structure_count;
    const KvPreviewSourceFile* source_files;
    uint64_t source_file_count;

    const double* controlpoints;
    uint64_t controlpoint_count;
    double cp_arbdistribution[3];
    double cp_arbdistribution_default[3];
    double cp_default_range[2];

    KvDoubleBuffer owntrack;
    KvDoubleBuffer curve_radius;
    const KvPreviewOtherTrack* other_tracks;
    uint64_t other_track_count;
    const KvPreviewOwnTrackEvent* own_track_events;
    uint64_t own_track_event_count;
    const KvPreviewSpeedLimit* speed_limits;
    uint64_t speed_limit_count;
    const KvPreviewStationPosition* station_positions;
    uint64_t station_position_count;
    const KvPreviewStationName* station_names;
    uint64_t station_name_count;

    const KvPreviewTable* tables;
    uint64_t table_count;
    const KvPreviewRow* rows;
    uint64_t row_count;
    const KvPreviewCell* cells;
    uint64_t cell_count;
    const KvPreviewValue* array_values;
    uint64_t array_value_count;
} KvPreviewSnapshot;

typedef void (*KvLogCallback)(const char* message);

KV_API void kv_set_log_callback(KvLogCallback callback);

/* Returns an opaque map handle owned by the caller. Release it exactly once
   with kv_free(), even after retrieving buffers or IR JSON from it. */
KV_API void* kv_load_map(const char* path, double unit_distance);

#define KV_LOAD_PREVIEW (1u << 0)
#define KV_LOAD_EDIT_METADATA (1u << 1)

/* Extended loader entry point. KV_LOAD_PREVIEW skips source/edit metadata when
   KV_LOAD_EDIT_METADATA is not also set. */
KV_API void* kv_load_map_ex(const char* path, double unit_distance, unsigned flags);

/* Regenerates geometry in-place for an existing handle and invalidates any
   KvDoubleBuffer views previously obtained from that handle. */
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
KV_API KvDoubleBuffer kv_get_scene_owntrack_buffer(void* handle);
KV_API KvDoubleBuffer kv_get_scene_othertrack_buffer(void* handle, const char* key);
KV_API KvDoubleBuffer kv_get_owntrack_buffer(void* handle);
KV_API KvDoubleBuffer kv_get_curveradius_buffer(void* handle);
KV_API size_t kv_get_othertrack_count(void* handle);
KV_API const char* kv_get_othertrack_key(void* handle, size_t index);
KV_API KvDoubleBuffer kv_get_othertrack_buffer(void* handle, const char* key);
KV_API KvDoubleBuffer kv_get_structure_puts(void* handle);

/* Copies a versioned POD view into out_snapshot. out_size must be at least
   sizeof(KvPreviewSnapshot). Returns nonzero on success. The view is read-only
   and remains owned by handle; callers must not free any nested pointer. */
KV_API int kv_get_preview_snapshot(void* handle, unsigned version,
                                   KvPreviewSnapshot* out_snapshot, size_t out_size);

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

/* Returns the complete decoded UTF-8 text for one parsed source file, including
   the active in-memory edit override when present. The source file must belong
   to the loaded map. Release the returned string with kv_free_string(). */
KV_API const char* kv_get_source_text(void* handle, const char* file_path);

/* Validates edit changes and returns a newly allocated UTF-8 JSON report.
   Does not write source files. Release it with kv_free_string(). */
KV_API const char* kv_edit_dry_run(void* handle, const char* changes_json);

/* Validates edit changes, reparses the active handle from an in-memory patched
   source cache, and returns a newly allocated UTF-8 JSON report. Does not write
   source files. Release it with kv_free_string(). */
KV_API const char* kv_edit_apply_to_memory(void* handle, const char* changes_json);

/* Discards in-memory edited source overrides and reparses the active handle from
   the current source files. Does not write source files. Returns nonzero on
   success. */
KV_API int kv_edit_reset_memory(void* handle);

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
