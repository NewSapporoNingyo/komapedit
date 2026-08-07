/*
 * Copyright (c) 2026 Sapporo_ningyo
 *
 * Licensed under Apache License 2.0; see LICENSE and NOTICE.
 */

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define KV_MAPLOADER_API_VERSION 6u
#define KV_MAP_SNAPSHOT_VERSION 6u
#define KV_SCENE_GEOMETRY_SNAPSHOT_VERSION 1u
#define KV_EDIT_TARGET_SNAPSHOT_VERSION 1u
#define KV_EDIT_REPORT_SNAPSHOT_VERSION 1u

#define KV_INDEX_NONE UINT64_MAX
#define KV_EDIT_TARGET_FLAG_SIGNAL_SHORT_FORM (1u << 0)

typedef struct KvUtf8View {
    const char* data;
    uint64_t length;
} KvUtf8View;

typedef struct KvStringRef {
    uint64_t offset;
    uint64_t length;
} KvStringRef;

typedef struct KvSpan {
    uint64_t offset;
    uint64_t count;
} KvSpan;

typedef struct KvDoubleBuffer {
    const double* data;
    uint64_t rows;
    uint64_t cols;
} KvDoubleBuffer;

enum KvValueKind {
    KV_VALUE_NULL = 0,
    KV_VALUE_NUMBER = 1,
    KV_VALUE_STRING = 2,
    KV_VALUE_CONTINUE = 3
};

typedef struct KvValue {
    uint32_t kind;
    uint32_t reserved;
    double number_value;
    KvStringRef string_value;
} KvValue;

typedef struct KvRowMetadata {
    KvStringRef edit_id;
    KvStringRef raw_text_preview;
    uint64_t source_file_index;
    int32_t line;
    int32_t column;
    int32_t line_end;
    int32_t column_end;
    uint32_t element_index;
    uint32_t reserved;
} KvRowMetadata;

typedef struct KvFileStructureRow {
    int64_t parent_index;
    KvStringRef include_path;
    KvStringRef absolute_path;
} KvFileStructureRow;

typedef struct KvSourceFileRow {
    KvStringRef file_path;
    KvStringRef display_path;
    KvStringRef encoding;
    KvStringRef newline;
    KvStringRef source_hash;
    uint64_t byte_length;
} KvSourceFileRow;

typedef struct KvTrackEventRow {
    double distance;
    KvStringRef key;
    KvStringRef flag;
    KvValue value;
    KvRowMetadata metadata;
} KvTrackEventRow;

typedef struct KvCurveRow {
    double distance;
    KvStringRef method;
    KvValue radius;
    KvValue cant;
    uint32_t argument_count;
    uint32_t reserved;
    KvStringRef file_path;
    int32_t order;
    uint32_t reserved2;
    KvRowMetadata metadata;
} KvCurveRow;

typedef struct KvGradientRow {
    double distance;
    KvStringRef method;
    KvValue gradient;
    uint32_t argument_count;
    uint32_t reserved;
    KvStringRef file_path;
    int32_t order;
    uint32_t reserved2;
    KvRowMetadata metadata;
} KvGradientRow;

typedef struct KvOtherTrackChangeRow {
    double distance;
    KvValue track_key;
    KvStringRef method;
    KvSpan parameters;
    KvStringRef file_path;
    int32_t order;
    uint32_t reserved;
    KvRowMetadata metadata;
} KvOtherTrackChangeRow;

typedef struct KvOtherTrackRow {
    KvStringRef key;
    double range_min;
    double range_max;
    KvDoubleBuffer points;
    KvSpan events;
} KvOtherTrackRow;

typedef struct KvStationPositionRow {
    double distance;
    KvStringRef key;
} KvStationPositionRow;

typedef struct KvStationNameRow {
    KvStringRef key;
    KvStringRef name;
} KvStationNameRow;

typedef struct KvStationPutRow {
    double distance;
    KvValue station_key;
    KvValue door;
    KvValue margin1;
    KvValue margin2;
    KvStringRef file_path;
    int32_t order;
    uint32_t reserved;
    KvRowMetadata metadata;
} KvStationPutRow;

typedef struct KvStationListRow {
    KvStringRef object_key;
    KvStringRef fields[13];
    KvRowMetadata metadata;
} KvStationListRow;

typedef struct KvStructureLoadRow {
    double distance;
    KvStringRef method;
    KvValue load_file_path;
    KvStringRef file_path;
    int32_t order;
    uint32_t reserved;
    KvRowMetadata metadata;
} KvStructureLoadRow;

typedef struct KvStructurePutRow {
    double distance;
    KvStringRef method;
    KvValue structure_key;
    KvValue track_key;
    double x;
    double y;
    double z;
    double rx;
    double ry;
    double rz;
    double tilt;
    double span;
    KvStringRef file_path;
    int32_t order;
    uint32_t reserved;
    KvRowMetadata metadata;
} KvStructurePutRow;

typedef struct KvStructureBetweenRow {
    double distance;
    KvStringRef method;
    KvValue structure_key;
    KvValue track_key1;
    KvValue track_key2;
    double flag;
    KvStringRef file_path;
    int32_t order;
    uint32_t reserved;
    KvRowMetadata metadata;
} KvStructureBetweenRow;

typedef struct KvStructureModelRow {
    KvStringRef structure_key;
    KvStringRef file_path;
    KvRowMetadata metadata;
} KvStructureModelRow;

typedef struct KvOtherTrainDefinitionRow {
    double distance;
    KvStringRef method;
    KvValue train_key;
    KvValue load_file_path;
    KvStringRef resolved_file_path;
    KvValue track_key;
    KvValue direction;
    KvStringRef source_file_path;
    int32_t order;
    uint32_t reserved;
    KvRowMetadata metadata;
} KvOtherTrainDefinitionRow;

typedef struct KvOtherTrainEnableRow {
    double distance;
    KvValue train_key;
    KvValue time;
    KvStringRef file_path;
    int32_t order;
    uint32_t reserved;
    KvRowMetadata metadata;
} KvOtherTrainEnableRow;

typedef struct KvOtherTrainStopRow {
    double distance;
    KvValue train_key;
    KvValue decelerate;
    KvValue stop_time;
    KvValue accelerate;
    KvValue speed;
    KvStringRef file_path;
    int32_t order;
    uint32_t reserved;
    KvRowMetadata metadata;
} KvOtherTrainStopRow;

typedef struct KvReferencedKeyRow {
    KvStringRef key;
    KvStringRef file_path;
    KvRowMetadata metadata;
} KvReferencedKeyRow;

typedef struct KvSectionRow {
    double distance;
    KvSpan values;
    KvStringRef file_path;
    int32_t order;
    uint32_t reserved;
    KvRowMetadata metadata;
    KvStringRef method;
} KvSectionRow;

enum KvResourceListLoadKind {
    KV_RESOURCE_LIST_STATION = 0,
    KV_RESOURCE_LIST_STRUCTURE = 1,
    KV_RESOURCE_LIST_SIGNAL = 2,
    KV_RESOURCE_LIST_SOUND = 3,
    KV_RESOURCE_LIST_SOUND_3D = 4
};

typedef struct KvVariableAssignmentRow {
    KvStringRef normalized_name;
    KvStringRef source_name;
    KvValue value;
    KvStringRef expression;
    KvStringRef file_path;
    int32_t order;
    uint32_t reserved;
} KvVariableAssignmentRow;

typedef struct KvResourceListLoadRow {
    uint32_t kind;
    uint32_t reserved;
    KvStringRef evaluated_path;
    KvStringRef raw_argument;
    KvStringRef resolved_path;
    KvStringRef file_path;
    int32_t order;
    uint32_t reserved2;
} KvResourceListLoadRow;

typedef struct KvSignalAspectRow {
    KvStringRef signal_aspect_key;
    KvSpan structure_keys;
    /* metadata.reserved stores the main source row's structure-key count. */
    KvRowMetadata metadata;
} KvSignalAspectRow;

typedef struct KvSignalPutRow {
    double distance;
    KvValue signal_aspect_key;
    KvValue section;
    KvValue track_key;
    double x;
    double y;
    double z;
    double rx;
    double ry;
    double rz;
    double tilt;
    double span;
    KvStringRef file_path;
    int32_t order;
    uint32_t reserved;
    KvRowMetadata metadata;
} KvSignalPutRow;

typedef struct KvBeaconRow {
    double distance;
    KvValue type;
    KvValue section;
    KvValue send_data;
    KvStringRef file_path;
    int32_t order;
    uint32_t reserved;
    KvRowMetadata metadata;
} KvBeaconRow;

typedef struct KvPreTrainRow {
    double distance;
    KvValue pass_time;
    KvStringRef file_path;
    int32_t order;
    uint32_t reserved;
    KvRowMetadata metadata;
} KvPreTrainRow;

typedef struct KvSoundListRow {
    KvStringRef sound_key;
    KvStringRef file_path;
    int32_t buffer_count;
    uint32_t is_3d;
    KvRowMetadata metadata;
} KvSoundListRow;

typedef struct KvMapSoundRow {
    double distance;
    KvValue sound_key;
    KvStringRef file_path;
    int32_t order;
    uint32_t reserved;
    KvRowMetadata metadata;
} KvMapSoundRow;

typedef struct KvMapSound3DRow {
    double distance;
    KvValue sound_key;
    double x;
    double y;
    KvStringRef file_path;
    int32_t order;
    uint32_t reserved;
    KvRowMetadata metadata;
} KvMapSound3DRow;

typedef struct KvNoiseRow {
    double distance;
    KvValue index;
    KvStringRef file_path;
    int32_t order;
    uint32_t reserved;
    KvRowMetadata metadata;
} KvNoiseRow;

typedef struct KvRepeaterRow {
    double distance;
    KvStringRef method;
    KvValue repeater_key;
    KvValue track_key;
    double x;
    double y;
    double z;
    double rx;
    double ry;
    double rz;
    double tilt;
    double span;
    double interval;
    KvSpan structure_keys;
    KvStringRef file_path;
    int32_t order;
    uint32_t reserved;
    KvRowMetadata metadata;
} KvRepeaterRow;

typedef struct KvIrregularityRow {
    double distance;
    double x;
    double y;
    double r;
    double lx;
    double ly;
    double lr;
    KvStringRef file_path;
    int32_t order;
    uint32_t reserved;
    KvRowMetadata metadata;
} KvIrregularityRow;

typedef struct KvBackgroundRow {
    double distance;
    KvValue structure_key;
    KvStringRef file_path;
    int32_t order;
    uint32_t reserved;
    KvRowMetadata metadata;
} KvBackgroundRow;

typedef struct KvAdhesionRow {
    double distance;
    KvValue a;
    KvValue b;
    KvValue c;
    KvStringRef file_path;
    int32_t order;
    uint32_t reserved;
    KvRowMetadata metadata;
} KvAdhesionRow;

typedef struct KvCabIlluminanceRow {
    double distance;
    KvValue value;
    KvStringRef file_path;
    int32_t order;
    uint32_t reserved;
    KvRowMetadata metadata;
} KvCabIlluminanceRow;

typedef struct KvFogRow {
    double distance;
    KvValue density;
    KvValue red;
    KvValue green;
    KvValue blue;
    KvStringRef file_path;
    int32_t order;
    uint32_t reserved;
    KvRowMetadata metadata;
} KvFogRow;

typedef struct KvDrawDistanceRow {
    double distance;
    double value;
    KvStringRef file_path;
    int32_t order;
    uint32_t reserved;
    KvRowMetadata metadata;
} KvDrawDistanceRow;

typedef struct KvSpeedLimitRow {
    double distance;
    KvValue speed;
    KvStringRef file_path;
    int32_t order;
    uint32_t reserved;
    KvRowMetadata metadata;
} KvSpeedLimitRow;

typedef struct KvSourceSpanRow {
    uint64_t source_file_index;
    uint64_t byte_start;
    uint64_t byte_end;
    int32_t line;
    int32_t column;
    int32_t line_end;
    int32_t column_end;
    KvSpan include_stack;
    KvStringRef include_invocation_key;
} KvSourceSpanRow;

typedef struct KvStatementRow {
    KvStringRef edit_id;
    KvStringRef statement_kind;
    KvSourceSpanRow source;
    KvStringRef raw_text;
    KvStringRef raw_text_preview;
    KvStringRef raw_arguments;
    KvSpan evaluated_values;
    KvStringRef distance_expression;
    double distance_value;
    int32_t global_order;
    uint32_t reserved;
} KvStatementRow;

typedef struct KvElementRow {
    KvStringRef edit_id;
    KvStringRef row_kind;
    uint64_t row_index;
    uint64_t source_file_index;
    int32_t global_order;
    uint32_t reserved;
} KvElementRow;

#define KV_MAP_CAP_PREVIEW_DATA (1u << 0)
#define KV_MAP_CAP_EDIT_METADATA (1u << 1)
#define KV_MAP_CAP_FULL_STATEMENT_SOURCE (1u << 2)
#define KV_MAP_CAP_REGULAR_GEOMETRY (1u << 3)

typedef struct KvMapSnapshot {
    uint32_t version;
    uint32_t capabilities;
    uint64_t structure_size;
    uint64_t content_revision;
    uint64_t geometry_revision;
    double build_seconds;

    const char* string_data;
    uint64_t string_size;
    const KvValue* values;
    uint64_t value_count;
    const KvStringRef* string_refs;
    uint64_t string_ref_count;

    KvStringRef root_path;
    const KvFileStructureRow* file_structure;
    uint64_t file_structure_count;
    const KvSourceFileRow* source_files;
    uint64_t source_file_count;

    const double* controlpoints;
    uint64_t controlpoint_count;
    double cp_arbdistribution[3];
    double cp_arbdistribution_default[3];
    double cp_default_range[2];

    KvDoubleBuffer own_track_geometry;
    KvDoubleBuffer curve_radius_geometry;
    KvDoubleBuffer structure_put_geometry;
    const KvOtherTrackRow* other_tracks;
    uint64_t other_track_count;
    const KvTrackEventRow* own_track_events;
    uint64_t own_track_event_count;
    const KvTrackEventRow* other_track_events;
    uint64_t other_track_event_count;
    const KvCurveRow* curves;
    uint64_t curve_count;
    const KvGradientRow* gradients;
    uint64_t gradient_count;
    const KvOtherTrackChangeRow* other_track_changes;
    uint64_t other_track_change_count;

    const KvStationPositionRow* station_positions;
    uint64_t station_position_count;
    const KvStationNameRow* station_names;
    uint64_t station_name_count;
    const KvStationPutRow* station_puts;
    uint64_t station_put_count;
    const KvStationListRow* station_list;
    uint64_t station_list_count;

    const KvStructureLoadRow* structure_loads;
    uint64_t structure_load_count;
    const KvStructurePutRow* structure_puts;
    uint64_t structure_put_count;
    const KvStructureBetweenRow* structure_betweens;
    uint64_t structure_between_count;
    const KvStructureModelRow* structure_models;
    uint64_t structure_model_count;

    const KvOtherTrainDefinitionRow* other_train_definitions;
    uint64_t other_train_definition_count;
    const KvReferencedKeyRow* other_train_structure_keys;
    uint64_t other_train_structure_key_count;
    const KvReferencedKeyRow* other_train_sound_3d_keys;
    uint64_t other_train_sound_3d_key_count;
    const KvOtherTrainEnableRow* other_train_enables;
    uint64_t other_train_enable_count;
    const KvOtherTrainStopRow* other_train_stops;
    uint64_t other_train_stop_count;

    const KvSectionRow* section_begins;
    uint64_t section_begin_count;
    const KvSectionRow* section_speed_limits;
    uint64_t section_speed_limit_count;
    const KvSignalAspectRow* signal_aspects;
    uint64_t signal_aspect_count;
    const KvSignalPutRow* signal_puts;
    uint64_t signal_put_count;
    const KvBeaconRow* beacons;
    uint64_t beacon_count;
    const KvPreTrainRow* pretrains;
    uint64_t pretrain_count;
    const KvSoundListRow* sound_list;
    uint64_t sound_list_count;
    const KvMapSoundRow* map_sounds;
    uint64_t map_sound_count;
    const KvMapSound3DRow* map_sounds_3d;
    uint64_t map_sound_3d_count;
    const KvNoiseRow* rolling_noises;
    uint64_t rolling_noise_count;
    const KvNoiseRow* flange_noises;
    uint64_t flange_noise_count;
    const KvNoiseRow* joint_noises;
    uint64_t joint_noise_count;
    const KvRepeaterRow* repeaters;
    uint64_t repeater_count;
    const KvIrregularityRow* irregularities;
    uint64_t irregularity_count;
    const KvBackgroundRow* backgrounds;
    uint64_t background_count;
    const KvAdhesionRow* adhesions;
    uint64_t adhesion_count;
    const KvCabIlluminanceRow* cab_illuminance;
    uint64_t cab_illuminance_count;
    const KvFogRow* fogs;
    uint64_t fog_count;
    const KvDrawDistanceRow* draw_distances;
    uint64_t draw_distance_count;
    const KvSpeedLimitRow* speed_limits;
    uint64_t speed_limit_count;

    const KvVariableAssignmentRow* variable_assignments;
    uint64_t variable_assignment_count;
    const KvResourceListLoadRow* resource_list_loads;
    uint64_t resource_list_load_count;

    const KvStatementRow* statements;
    uint64_t statement_count;
    const KvElementRow* elements;
    uint64_t element_count;
} KvMapSnapshot;

typedef struct KvSceneTrackRow {
    KvStringRef key;
    KvDoubleBuffer points;
} KvSceneTrackRow;

typedef struct KvSceneGeometrySnapshot {
    uint32_t version;
    uint32_t reserved;
    uint64_t structure_size;
    uint64_t content_revision;
    uint64_t scene_revision;
    double build_seconds;
    const char* string_data;
    uint64_t string_size;
    KvDoubleBuffer own_track;
    const KvSceneTrackRow* other_tracks;
    uint64_t other_track_count;
} KvSceneGeometrySnapshot;

enum KvEditOperation {
    KV_EDIT_UPDATE = 1,
    KV_EDIT_INSERT = 2,
    KV_EDIT_DELETE = 3
};

#define KV_EDIT_CHANGE_CONFIRM_ENVIRONMENT_MISMATCH (1u << 0)

typedef struct KvEditField {
    KvUtf8View name;
    KvUtf8View value;
} KvEditField;

typedef struct KvEditChange {
    KvUtf8View change_id;
    KvUtf8View edit_id;
    uint32_t operation;
    uint32_t flags;
    KvSpan fields;
    KvUtf8View replacement_statement;
    KvUtf8View target_file_path;
    KvUtf8View insert_before_edit_id;
    KvUtf8View expected_source_hash;
    KvUtf8View distance_resolution_key;
    KvUtf8View distance_boundary_token;
    KvUtf8View distance_expression;
} KvEditChange;

typedef struct KvEditBatch {
    const KvEditChange* changes;
    uint64_t change_count;
    const KvEditField* fields;
    uint64_t field_count;
} KvEditBatch;

typedef struct KvEditTargetSnapshot {
    uint32_t version;
    uint32_t flags;
    uint64_t structure_size;
    const char* string_data;
    uint64_t string_size;
    const KvStringRef* string_refs;
    uint64_t string_ref_count;
    KvStringRef edit_id;
    KvStringRef row_kind;
    uint64_t row_index;
    uint64_t elements_for_statement;
    KvStringRef statement_kind;
    KvStringRef source_hash;
    KvStringRef expected_source_hash;
    KvStringRef source_file_path;
    KvSourceSpanRow source;
    KvStringRef raw_text;
    KvStringRef raw_text_preview;
    KvStringRef raw_arguments;
    KvStringRef distance_expression;
    double distance_value;
} KvEditTargetSnapshot;

typedef struct KvEditCommittedFileRow {
    KvStringRef file_path;
    KvStringRef source_hash;
    uint64_t byte_length;
} KvEditCommittedFileRow;

typedef struct KvEditCommittedRow {
    KvStringRef row_kind;
    uint64_t row_index;
    KvStringRef edit_id;
    KvStringRef file_path;
    int32_t line;
    int32_t column;
    KvStringRef raw_text_preview;
} KvEditCommittedRow;

typedef struct KvEditPreviewRow {
    KvStringRef file_path;
    KvStringRef before_text;
    KvStringRef after_text;
} KvEditPreviewRow;

typedef struct KvDistanceBoundaryRow {
    KvStringRef token;
    int32_t line;
    int32_t column;
    uint32_t recommended;
    uint32_t reserved;
} KvDistanceBoundaryRow;

typedef struct KvDistanceResolutionRow {
    KvStringRef resolution_key;
    KvStringRef reason;
    KvStringRef source_file;
    KvSpan include_stack;
    double target_distance;
    KvStringRef variable_name;
    KvSpan affected_edit_ids;
    KvStringRef suggested_expression;
    KvStringRef insertion_preview;
    uint32_t can_confirm_reuse;
    uint32_t reserved;
    int32_t source_section_first_line;
    int32_t source_section_last_line;
    KvStringRef source_section_direction;
    KvSpan allowed_boundaries;
} KvDistanceResolutionRow;

typedef struct KvEditReportSnapshot {
    uint32_t version;
    uint32_t ok;
    uint64_t structure_size;
    uint64_t report_revision;
    const char* string_data;
    uint64_t string_size;
    const KvStringRef* string_refs;
    uint64_t string_ref_count;
    const KvDistanceBoundaryRow* boundaries;
    uint64_t boundary_count;

    int32_t update_count;
    int32_t insert_count;
    int32_t delete_count;
    int32_t full_reparse_ok;
    int32_t target_distance_match_count;
    int32_t non_target_changed_count;
    int32_t created_distance_block_count;
    int32_t reused_distance_block_count;
    int32_t distance_group_count;
    uint32_t reserved;
    KvStringRef validation_fingerprint;

    const KvStringRef* changed_files;
    uint64_t changed_file_count;
    const KvEditCommittedFileRow* committed_files;
    uint64_t committed_file_count;
    const KvEditCommittedRow* committed_rows;
    uint64_t committed_row_count;
    const KvStringRef* warnings;
    uint64_t warning_count;
    const KvStringRef* blocking_errors;
    uint64_t blocking_error_count;
    const KvDistanceResolutionRow* resolution_requests;
    uint64_t resolution_request_count;
    const KvEditPreviewRow* preview_snippets;
    uint64_t preview_snippet_count;
} KvEditReportSnapshot;

#ifdef __cplusplus
}
#endif
