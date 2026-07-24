/*
 * Copyright (c) 2026 Sapporo_ningyo
 *
 * Licensed under Apache License 2.0; see LICENSE and NOTICE.
 */

#include "maploader.h"

#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <string>
#include <string_view>

namespace {

int failures = 0;

void check(bool condition, const char* message) {
    if (condition) return;
    ++failures;
    std::cerr << "FAIL: " << message << '\n';
}

template <typename T>
bool pointer_count_valid(const T* pointer, std::uint64_t count) {
    return count == 0 || pointer != nullptr;
}

bool ref_valid(const char* data, std::uint64_t size, KvStringRef ref) {
    return ref.offset <= size && ref.length <= size - ref.offset &&
        (ref.length == 0 || data != nullptr);
}

bool span_valid(KvSpan span, std::uint64_t size) {
    return span.offset <= size && span.count <= size - span.offset;
}

bool buffer_valid(KvDoubleBuffer buffer) {
    if (buffer.rows == 0 || buffer.cols == 0) return buffer.rows == 0;
    return buffer.data != nullptr &&
        buffer.rows <= std::numeric_limits<std::uint64_t>::max() / buffer.cols;
}

std::string_view arena_view(const char* data, std::uint64_t size, KvStringRef ref) {
    if (!ref_valid(data, size, ref)) return {};
    return std::string_view(data ? data + ref.offset : "", static_cast<size_t>(ref.length));
}

std::string map_string(const KvMapSnapshot& snapshot, KvStringRef ref) {
    const std::string_view value = arena_view(snapshot.string_data, snapshot.string_size, ref);
    return std::string(value.data(), value.size());
}

struct TempFixture {
    std::filesystem::path directory;
    std::filesystem::path map_path;

    TempFixture() {
        directory = std::filesystem::temp_directory_path() /
            ("komapedit-typed-contract-" + std::to_string(
                std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(directory);
        map_path = directory / "map.txt";
        std::ofstream map(map_path, std::ios::binary | std::ios::trunc);
        map << "BveTs Map 2.02:utf-8\n"
            << "0;\n"
            << "Structure.Load('structures.csv');\n"
            << "Track['1'].Position(3.8,0);\n"
            << "100;\n"
            << "Station['STA'].Put();\n"
            << "Structure['pole'].Put('0',1,2,3,0,0,0,0,25);\n"
            << "SpeedLimit.Begin(80);\n"
            << "125;\n"
            << "Structure['pole'].Put('1',9,2,3,0,0,0,0,25);\n"
            << "150;\n"
            << "Repeater['rail'].Begin0('1',1,25,5,'rail-a');\n"
            << "175;\n"
            << "Repeater['rail'].Begin0('1',1,25,5,'rail-b');\n"
            << "190;\n"
            << "Repeater['rail'].End();\n"
            << "200;\n"
            << "Track['1'].Position(4.0,0);\n"
            << "SpeedLimit.End();\n";
        map.close();
        std::ofstream structures(directory / "structures.csv",
                                 std::ios::binary | std::ios::trunc);
        structures << "BveTs Structure List 1.00:utf-8\n"
                   << "pole,pole.csv\n";
    }

    ~TempFixture() {
        std::error_code error;
        std::filesystem::remove_all(directory, error);
    }

    std::string path_utf8() const { return map_path.u8string(); }
};

struct MapHandle {
    void* value = nullptr;
    explicit MapHandle(void* input) : value(input) {}
    ~MapHandle() { kv_free(value); }
    MapHandle(const MapHandle&) = delete;
    MapHandle& operator=(const MapHandle&) = delete;
};

void validate_arrays(const KvMapSnapshot& snapshot) {
#define CHECK_ARRAY(field, count_field) \
    check(pointer_count_valid(snapshot.field, snapshot.count_field), #field " pointer/count")
    CHECK_ARRAY(values, value_count);
    CHECK_ARRAY(string_refs, string_ref_count);
    CHECK_ARRAY(file_structure, file_structure_count);
    CHECK_ARRAY(source_files, source_file_count);
    CHECK_ARRAY(controlpoints, controlpoint_count);
    CHECK_ARRAY(other_tracks, other_track_count);
    CHECK_ARRAY(own_track_events, own_track_event_count);
    CHECK_ARRAY(other_track_events, other_track_event_count);
    CHECK_ARRAY(station_positions, station_position_count);
    CHECK_ARRAY(station_names, station_name_count);
    CHECK_ARRAY(station_puts, station_put_count);
    CHECK_ARRAY(station_list, station_list_count);
    CHECK_ARRAY(structure_loads, structure_load_count);
    CHECK_ARRAY(structure_puts, structure_put_count);
    CHECK_ARRAY(structure_betweens, structure_between_count);
    CHECK_ARRAY(structure_models, structure_model_count);
    CHECK_ARRAY(other_train_definitions, other_train_definition_count);
    CHECK_ARRAY(other_train_structure_keys, other_train_structure_key_count);
    CHECK_ARRAY(other_train_sound_3d_keys, other_train_sound_3d_key_count);
    CHECK_ARRAY(other_train_enables, other_train_enable_count);
    CHECK_ARRAY(other_train_stops, other_train_stop_count);
    CHECK_ARRAY(section_begins, section_begin_count);
    CHECK_ARRAY(section_speed_limits, section_speed_limit_count);
    CHECK_ARRAY(signal_aspects, signal_aspect_count);
    CHECK_ARRAY(signal_puts, signal_put_count);
    CHECK_ARRAY(beacons, beacon_count);
    CHECK_ARRAY(pretrains, pretrain_count);
    CHECK_ARRAY(sound_list, sound_list_count);
    CHECK_ARRAY(map_sounds, map_sound_count);
    CHECK_ARRAY(map_sounds_3d, map_sound_3d_count);
    CHECK_ARRAY(rolling_noises, rolling_noise_count);
    CHECK_ARRAY(flange_noises, flange_noise_count);
    CHECK_ARRAY(joint_noises, joint_noise_count);
    CHECK_ARRAY(repeaters, repeater_count);
    CHECK_ARRAY(irregularities, irregularity_count);
    CHECK_ARRAY(backgrounds, background_count);
    CHECK_ARRAY(adhesions, adhesion_count);
    CHECK_ARRAY(cab_illuminance, cab_illuminance_count);
    CHECK_ARRAY(fogs, fog_count);
    CHECK_ARRAY(speed_limits, speed_limit_count);
    CHECK_ARRAY(statements, statement_count);
    CHECK_ARRAY(elements, element_count);
#undef CHECK_ARRAY
}

void validate_map(const KvMapSnapshot& snapshot, bool edit_metadata) {
    check(snapshot.version == KV_MAP_SNAPSHOT_VERSION, "map snapshot version");
    check(snapshot.structure_size == sizeof(KvMapSnapshot), "map structure size");
    check((snapshot.capabilities & KV_MAP_CAP_PREVIEW_DATA) != 0, "preview capability");
    check((snapshot.capabilities & KV_MAP_CAP_REGULAR_GEOMETRY) != 0,
          "geometry capability");
    check(((snapshot.capabilities & KV_MAP_CAP_EDIT_METADATA) != 0) == edit_metadata,
          "edit capability");
    check(ref_valid(snapshot.string_data, snapshot.string_size, snapshot.root_path),
          "root path bounds");
    validate_arrays(snapshot);
    check(buffer_valid(snapshot.own_track_geometry), "own geometry buffer");
    check(buffer_valid(snapshot.curve_radius_geometry), "curve geometry buffer");
    check(buffer_valid(snapshot.structure_put_geometry), "structure geometry buffer");
    for (std::uint64_t i = 0; i < snapshot.string_ref_count; ++i) {
        check(ref_valid(snapshot.string_data, snapshot.string_size, snapshot.string_refs[i]),
              "string-ref bounds");
    }
    for (std::uint64_t i = 0; i < snapshot.value_count; ++i) {
        check(snapshot.values[i].kind <= KV_VALUE_CONTINUE, "value tag");
        if (snapshot.values[i].kind == KV_VALUE_STRING) {
            check(ref_valid(snapshot.string_data, snapshot.string_size,
                            snapshot.values[i].string_value),
                  "value string bounds");
        }
    }
    for (std::uint64_t i = 0; i < snapshot.other_track_count; ++i) {
        check(ref_valid(snapshot.string_data, snapshot.string_size,
                        snapshot.other_tracks[i].key),
              "other-track key bounds");
        check(buffer_valid(snapshot.other_tracks[i].points), "other-track geometry buffer");
        check(span_valid(snapshot.other_tracks[i].events,
                         snapshot.other_track_event_count),
              "other-track event span");
    }
    if (!edit_metadata) return;
    check(snapshot.source_file_count != 0 && snapshot.statement_count != 0 &&
              snapshot.element_count != 0,
          "edit metadata arrays present");
    check((snapshot.capabilities & KV_MAP_CAP_FULL_STATEMENT_SOURCE) != 0,
          "full statement source capability");
    for (std::uint64_t i = 0; i < snapshot.statement_count; ++i) {
        const KvStatementRow& row = snapshot.statements[i];
        check(row.source.source_file_index < snapshot.source_file_count,
              "statement source index");
        check(span_valid(row.evaluated_values, snapshot.value_count),
              "evaluated-value span");
        check(span_valid(row.source.include_stack, snapshot.string_ref_count),
              "include-stack span");
        check(ref_valid(snapshot.string_data, snapshot.string_size, row.edit_id),
              "statement edit id bounds");
    }
    for (std::uint64_t i = 0; i < snapshot.element_count; ++i) {
        const KvElementRow& row = snapshot.elements[i];
        check(row.source_file_index < snapshot.source_file_count, "element source index");
        check(ref_valid(snapshot.string_data, snapshot.string_size, row.edit_id),
              "element edit id bounds");
        check(ref_valid(snapshot.string_data, snapshot.string_size, row.row_kind),
              "element row kind bounds");
    }
}

int snapshot_contract() {
    TempFixture fixture;
    check(kv_api_version() == KV_MAPLOADER_API_VERSION, "API version");
    MapHandle handle(kv_load_map_ex(fixture.path_utf8().c_str(), 25.0, KV_LOAD_PREVIEW));
    check(handle.value != nullptr, "preview load");
    if (!handle.value) return failures;

    KvMapSnapshot first{};
    KvMapSnapshot cached{};
    check(!kv_get_map_snapshot(nullptr, KV_MAP_SNAPSHOT_VERSION, &first, sizeof(first)),
          "null handle rejected");
    check(!kv_get_map_snapshot(handle.value, KV_MAP_SNAPSHOT_VERSION, nullptr, sizeof(first)),
          "null output rejected");
    check(!kv_get_map_snapshot(handle.value, KV_MAP_SNAPSHOT_VERSION + 1,
                               &first, sizeof(first)),
          "wrong version rejected");
    check(!kv_get_map_snapshot(handle.value, KV_MAP_SNAPSHOT_VERSION,
                               &first, sizeof(first) - 1),
          "short output rejected");
    check(kv_get_map_snapshot(handle.value, KV_MAP_SNAPSHOT_VERSION,
                              &first, sizeof(first)) != 0,
          "first map snapshot");
    validate_map(first, false);
    check(kv_get_map_snapshot(handle.value, KV_MAP_SNAPSHOT_VERSION,
                              &cached, sizeof(cached)) != 0,
          "cached map snapshot");
    check(first.content_revision == cached.content_revision &&
              first.geometry_revision == cached.geometry_revision &&
              first.string_data == cached.string_data && first.values == cached.values &&
              first.other_tracks == cached.other_tracks &&
              first.own_track_geometry.data == cached.own_track_geometry.data,
          "map snapshot reused");

    KvSceneGeometrySnapshot scene{};
    check(!kv_get_scene_geometry_snapshot(handle.value, KV_SCENE_GEOMETRY_SNAPSHOT_VERSION,
                                           &scene, sizeof(scene)),
          "scene requires generation");
    check(kv_generate_scene_geometry(handle.value, 25.0, 1.0, 25.0, 1.0, 0.01) != 0,
          "scene generation");
    check(kv_get_scene_geometry_snapshot(handle.value, KV_SCENE_GEOMETRY_SNAPSHOT_VERSION,
                                         &scene, sizeof(scene)) != 0,
          "scene snapshot");
    check(scene.version == KV_SCENE_GEOMETRY_SNAPSHOT_VERSION &&
              scene.structure_size == sizeof(KvSceneGeometrySnapshot),
          "scene ABI contract");
    check(scene.content_revision == first.content_revision &&
              scene.other_track_count == first.other_track_count,
          "scene content and order count");
    check(buffer_valid(scene.own_track) &&
              pointer_count_valid(scene.other_tracks, scene.other_track_count),
          "scene buffers");
    for (std::uint64_t i = 0; i < scene.other_track_count; ++i) {
        check(buffer_valid(scene.other_tracks[i].points), "scene other-track buffer");
        check(arena_view(scene.string_data, scene.string_size, scene.other_tracks[i].key) ==
                  arena_view(first.string_data, first.string_size, first.other_tracks[i].key),
              "scene track order");
    }
    KvSceneGeometrySnapshot scene_cached{};
    check(kv_get_scene_geometry_snapshot(handle.value, KV_SCENE_GEOMETRY_SNAPSHOT_VERSION,
                                         &scene_cached, sizeof(scene_cached)) != 0,
          "cached scene snapshot");
    check(scene.scene_revision == scene_cached.scene_revision &&
              scene.string_data == scene_cached.string_data &&
              scene.other_tracks == scene_cached.other_tracks &&
              scene.own_track.data == scene_cached.own_track.data,
          "scene snapshot reused");
    check(kv_generate_scene_geometry(handle.value, 25.0, 1.0, 25.0, 1.0, 0.01) != 0,
          "same-parameter scene cache hit");
    KvSceneGeometrySnapshot scene_parameter_cached{};
    check(kv_get_scene_geometry_snapshot(handle.value, KV_SCENE_GEOMETRY_SNAPSHOT_VERSION,
                                         &scene_parameter_cached, sizeof(scene_parameter_cached)) != 0 &&
              scene.scene_revision == scene_parameter_cached.scene_revision &&
              scene.string_data == scene_parameter_cached.string_data &&
              scene.other_tracks == scene_parameter_cached.other_tracks &&
              scene.own_track.data == scene_parameter_cached.own_track.data,
          "same-parameter scene geometry reused");
    KvMapSnapshot after_scene{};
    check(kv_get_map_snapshot(handle.value, KV_MAP_SNAPSHOT_VERSION,
                              &after_scene, sizeof(after_scene)) != 0 &&
              after_scene.content_revision == first.content_revision &&
              after_scene.geometry_revision == first.geometry_revision &&
              after_scene.string_data == first.string_data,
          "scene generation preserves map snapshot");

    const std::uint64_t scene_revision = scene.scene_revision;
    check(kv_generate_scene_geometry(handle.value, 25.0, 1.0, 20.0, 1.0, 0.005) != 0,
          "scene regeneration");
    KvSceneGeometrySnapshot rebuilt_scene{};
    check(kv_get_scene_geometry_snapshot(handle.value, KV_SCENE_GEOMETRY_SNAPSHOT_VERSION,
                                         &rebuilt_scene, sizeof(rebuilt_scene)) != 0 &&
              rebuilt_scene.content_revision == first.content_revision &&
              rebuilt_scene.scene_revision > scene_revision,
          "scene-only invalidation");
    KvMapSnapshot after_scene_rebuild{};
    check(kv_get_map_snapshot(handle.value, KV_MAP_SNAPSHOT_VERSION,
                              &after_scene_rebuild, sizeof(after_scene_rebuild)) != 0 &&
              after_scene_rebuild.geometry_revision == first.geometry_revision,
          "scene rebuild preserves map revision");

    check(kv_generate_geometry(handle.value, 25.0, 0, 0.0, 0.0, 0.0) != 0,
          "regular geometry regeneration");
    KvMapSnapshot regenerated{};
    check(kv_get_map_snapshot(handle.value, KV_MAP_SNAPSHOT_VERSION,
                              &regenerated, sizeof(regenerated)) != 0 &&
              regenerated.content_revision == first.content_revision &&
              regenerated.geometry_revision > first.geometry_revision,
          "regular geometry invalidation");
    check(!kv_get_scene_geometry_snapshot(handle.value, KV_SCENE_GEOMETRY_SNAPSHOT_VERSION,
                                           &rebuilt_scene, sizeof(rebuilt_scene)),
          "regular geometry invalidates scene");
    std::cout << "typed snapshot contract " << (failures ? "FAIL" : "PASS") << '\n';
    return failures;
}

KvUtf8View utf8_view(const std::string& value) {
    return KvUtf8View{value.data(), static_cast<std::uint64_t>(value.size())};
}

struct UpdateBatch {
    std::string change_id = "typed-contract-update";
    std::string edit_id;
    std::string source_hash;
    std::string field_name = "x";
    std::string field_value;
    KvEditField field{};
    KvEditChange change{};
    KvEditBatch batch{};

    UpdateBatch(std::string id, std::string hash, std::string value)
        : edit_id(std::move(id)), source_hash(std::move(hash)),
          field_value(std::move(value)) {
        field = KvEditField{utf8_view(field_name), utf8_view(field_value)};
        change.change_id = utf8_view(change_id);
        change.edit_id = utf8_view(edit_id);
        change.operation = KV_EDIT_UPDATE;
        change.fields = KvSpan{0, 1};
        change.expected_source_hash = utf8_view(source_hash);
        batch = KvEditBatch{&change, 1, &field, 1};
    }
};

struct RepeaterTrimBatch {
    std::string change_update_id = "typed-contract-repeater-trim";
    std::string change_delete_id = "typed-contract-repeater-delete-end";
    std::string update_edit_id;
    std::string delete_edit_id;
    std::string source_hash;
    std::string field_name = "method";
    std::string field_value = "End";
    KvEditField field{};
    std::array<KvEditChange, 2> changes{};
    KvEditBatch batch{};

    RepeaterTrimBatch(std::string update_id, std::string delete_id, std::string hash)
        : update_edit_id(std::move(update_id)), delete_edit_id(std::move(delete_id)),
          source_hash(std::move(hash)) {
        field = KvEditField{utf8_view(field_name), utf8_view(field_value)};
        changes[0].change_id = utf8_view(change_update_id);
        changes[0].edit_id = utf8_view(update_edit_id);
        changes[0].operation = KV_EDIT_UPDATE;
        changes[0].fields = KvSpan{0, 1};
        changes[0].expected_source_hash = utf8_view(source_hash);
        changes[1].change_id = utf8_view(change_delete_id);
        changes[1].edit_id = utf8_view(delete_edit_id);
        changes[1].operation = KV_EDIT_DELETE;
        changes[1].expected_source_hash = utf8_view(source_hash);
        batch = KvEditBatch{changes.data(), changes.size(), &field, 1};
    }
};

void validate_report(const KvEditReportSnapshot& report) {
    check(report.version == KV_EDIT_REPORT_SNAPSHOT_VERSION, "report version");
    check(report.structure_size == sizeof(KvEditReportSnapshot), "report structure size");
    check(ref_valid(report.string_data, report.string_size,
                    report.validation_fingerprint),
          "report fingerprint bounds");
    check(pointer_count_valid(report.string_refs, report.string_ref_count), "report refs");
    check(pointer_count_valid(report.boundaries, report.boundary_count), "report boundaries");
    check(pointer_count_valid(report.changed_files, report.changed_file_count), "changed files");
    check(pointer_count_valid(report.committed_files, report.committed_file_count),
          "committed files");
    check(pointer_count_valid(report.committed_rows, report.committed_row_count),
          "committed rows");
    check(pointer_count_valid(report.warnings, report.warning_count), "warnings");
    check(pointer_count_valid(report.blocking_errors, report.blocking_error_count),
          "blocking errors");
    check(pointer_count_valid(report.resolution_requests, report.resolution_request_count),
          "resolution requests");
    check(pointer_count_valid(report.preview_snippets, report.preview_snippet_count),
          "preview snippets");
    for (std::uint64_t i = 0; i < report.resolution_request_count; ++i) {
        check(span_valid(report.resolution_requests[i].include_stack,
                         report.string_ref_count),
              "resolution include stack");
        check(span_valid(report.resolution_requests[i].affected_edit_ids,
                         report.string_ref_count),
              "resolution affected ids");
        check(span_valid(report.resolution_requests[i].allowed_boundaries,
                         report.boundary_count),
              "resolution boundaries");
    }
}

const KvStructurePutRow* find_structure(const KvMapSnapshot& snapshot,
                                        std::string_view edit_id) {
    for (std::uint64_t i = 0; i < snapshot.structure_put_count; ++i) {
        const KvStructurePutRow& row = snapshot.structure_puts[i];
        if (arena_view(snapshot.string_data, snapshot.string_size,
                       row.metadata.edit_id) == edit_id) return &row;
    }
    return nullptr;
}

const KvRepeaterRow* find_repeater(const KvMapSnapshot& snapshot,
                                   std::string_view edit_id) {
    for (std::uint64_t i = 0; i < snapshot.repeater_count; ++i) {
        const KvRepeaterRow& row = snapshot.repeaters[i];
        if (arena_view(snapshot.string_data, snapshot.string_size,
                       row.metadata.edit_id) == edit_id) {
            return &row;
        }
    }
    return nullptr;
}

int edit_contract() {
    TempFixture fixture;
    MapHandle handle(kv_load_map_ex(fixture.path_utf8().c_str(), 25.0,
                                    KV_LOAD_PREVIEW | KV_LOAD_EDIT_METADATA));
    check(handle.value != nullptr, "edit load");
    if (!handle.value) return failures;
    KvMapSnapshot baseline{};
    check(kv_get_map_snapshot(handle.value, KV_MAP_SNAPSHOT_VERSION,
                              &baseline, sizeof(baseline)) != 0,
          "edit baseline snapshot");
    validate_map(baseline, true);
    check(baseline.structure_put_count >= 2, "editable rows present");
    if (baseline.structure_put_count == 0) return failures;
    const KvStructurePutRow& row = baseline.structure_puts[0];
    const std::string edit_id = map_string(baseline, row.metadata.edit_id);
    check(!edit_id.empty(), "edit id present");
    check(row.metadata.source_file_index < baseline.source_file_count, "source index");
    if (row.metadata.source_file_index >= baseline.source_file_count) return failures;
    const KvSourceFileRow& source = baseline.source_files[row.metadata.source_file_index];
    const std::string source_hash = map_string(baseline, source.source_hash);
    const std::string source_path = map_string(baseline, source.file_path);
    check(baseline.repeater_count == 3, "Repeater trim fixture rows present");
    if (baseline.repeater_count != 3) return failures;
    const std::string trim_edit_id =
        map_string(baseline, baseline.repeaters[1].metadata.edit_id);
    const std::string trim_end_edit_id =
        map_string(baseline, baseline.repeaters[2].metadata.edit_id);

    KvEditTargetSnapshot target{};
    check(kv_get_edit_target_typed(handle.value, utf8_view(edit_id),
                                   &target, sizeof(target)) != 0,
          "typed target");
    check(target.version == KV_EDIT_TARGET_SNAPSHOT_VERSION &&
              target.structure_size == sizeof(KvEditTargetSnapshot),
          "target ABI contract");
    check(arena_view(target.string_data, target.string_size, target.edit_id) == edit_id &&
              arena_view(target.string_data, target.string_size, target.row_kind) ==
                  "structure.put",
          "target identity");
    check(span_valid(target.source.include_stack, target.string_ref_count),
          "target include stack");

    KvEditReportSnapshot report{};
    KvEditBatch missing{};
    missing.change_count = 1;
    check(!kv_edit_dry_run_typed(handle.value, &missing, &report, sizeof(report)),
          "missing change array rejected");
    UpdateBatch update(edit_id, source_hash, "7");
    update.change.fields = KvSpan{1, 1};
    check(!kv_edit_dry_run_typed(handle.value, &update.batch, &report, sizeof(report)),
          "out-of-bounds field span rejected");
    update.change.fields = KvSpan{0, 1};

    RepeaterTrimBatch trim(trim_edit_id, trim_end_edit_id, source_hash);
    check(kv_edit_dry_run_typed(handle.value, &trim.batch,
                                &report, sizeof(report)) != 0,
          "Repeater trim dry-run call");
    validate_report(report);
    check(report.ok && report.update_count == 1 && report.delete_count == 1 &&
              report.blocking_error_count == 0,
          "Repeater trim dry-run report");
    KvEditReportSnapshot trim_applied_report{};
    check(kv_edit_apply_to_memory_typed(handle.value, &trim.batch,
                                        &trim_applied_report,
                                        sizeof(trim_applied_report)) != 0,
          "Repeater trim apply-to-memory call");
    validate_report(trim_applied_report);
    KvMapSnapshot trim_applied{};
    check(kv_get_map_snapshot(handle.value, KV_MAP_SNAPSHOT_VERSION,
                              &trim_applied, sizeof(trim_applied)) != 0,
          "Repeater trim applied snapshot");
    const KvRepeaterRow* trimmed = find_repeater(trim_applied, trim_edit_id);
    check(trim_applied.repeater_count == 2 && trimmed &&
              map_string(trim_applied, trimmed->method) == "End" &&
              find_repeater(trim_applied, trim_end_edit_id) == nullptr,
          "Repeater trim converted Begin0 and removed original End");
    check(kv_edit_reset_memory(handle.value) != 0, "Repeater trim reset call");
    KvMapSnapshot trim_reset{};
    check(kv_get_map_snapshot(handle.value, KV_MAP_SNAPSHOT_VERSION,
                              &trim_reset, sizeof(trim_reset)) != 0,
          "Repeater trim reset snapshot");
    const KvRepeaterRow* reset_trimmed = find_repeater(trim_reset, trim_edit_id);
    check(trim_reset.repeater_count == 3 && reset_trimmed &&
              map_string(trim_reset, reset_trimmed->method) == "Begin0" &&
              find_repeater(trim_reset, trim_end_edit_id) != nullptr,
          "Repeater trim reset restored source rows");

    check(kv_edit_dry_run_typed(handle.value, &update.batch, &report, sizeof(report)) != 0,
          "dry-run call");
    validate_report(report);
    check(report.ok && report.update_count == 1 && report.blocking_error_count == 0 &&
              report.changed_file_count == 1 && report.preview_snippet_count == 1,
          "dry-run report");
    check(report.report_revision != 0, "dry-run report revision");

    KvEditReportSnapshot applied_report{};
    check(kv_edit_apply_to_memory_typed(handle.value, &update.batch,
                                        &applied_report, sizeof(applied_report)) != 0,
          "apply-to-memory call");
    validate_report(applied_report);
    check(applied_report.ok && applied_report.report_revision != 0,
          "apply-to-memory report");
    KvMapSnapshot applied{};
    check(kv_get_map_snapshot(handle.value, KV_MAP_SNAPSHOT_VERSION,
                              &applied, sizeof(applied)) != 0 &&
              applied.content_revision > baseline.content_revision &&
              applied.geometry_revision > baseline.geometry_revision,
          "apply-to-memory revisions");
    const KvStructurePutRow* applied_row = find_structure(applied, edit_id);
    check(applied_row && std::abs(applied_row->x - 7.0) < 1e-9,
          "apply-to-memory value");
    const char* source_text = kv_get_source_text(handle.value, source_path.c_str());
    check(source_text && std::string_view(source_text).find("'0',7,2,3") !=
              std::string_view::npos,
          "in-memory source text");
    kv_free_string(source_text);

    check(kv_edit_reset_memory(handle.value) != 0, "reset call");
    KvMapSnapshot reset{};
    check(kv_get_map_snapshot(handle.value, KV_MAP_SNAPSHOT_VERSION,
                              &reset, sizeof(reset)) != 0 &&
              reset.content_revision > applied.content_revision &&
              reset.geometry_revision > applied.geometry_revision,
          "reset revisions");
    const KvStructurePutRow* reset_row = find_structure(reset, edit_id);
    check(reset_row && std::abs(reset_row->x - 1.0) < 1e-9, "reset value");

    KvEditReportSnapshot reapply{};
    check(kv_edit_apply_to_memory_typed(handle.value, &update.batch,
                                        &reapply, sizeof(reapply)) != 0 && reapply.ok,
          "reapply before commit");
    KvEditReportSnapshot committed_report{};
    check(kv_edit_commit_typed(handle.value, &committed_report,
                               sizeof(committed_report)) != 0,
          "commit call");
    validate_report(committed_report);
    check(committed_report.ok && committed_report.committed_file_count >= 1 &&
              committed_report.committed_row_count >= 1,
          "commit report");
    KvMapSnapshot committed{};
    check(kv_get_map_snapshot(handle.value, KV_MAP_SNAPSHOT_VERSION,
                              &committed, sizeof(committed)) != 0,
          "committed snapshot");
    const KvStructurePutRow* committed_row = find_structure(committed, edit_id);
    check(committed_row && std::abs(committed_row->x - 7.0) < 1e-9,
          "commit value and stable id");
    if (!committed_row) return failures;
    const KvSourceFileRow& committed_source =
        committed.source_files[committed_row->metadata.source_file_index];
    UpdateBatch direct(edit_id, map_string(committed, committed_source.source_hash), "8");
    KvEditReportSnapshot direct_report{};
    check(kv_edit_apply_typed(handle.value, &direct.batch,
                              &direct_report, sizeof(direct_report)) != 0,
          "direct apply call");
    validate_report(direct_report);
    check(direct_report.ok && direct_report.changed_file_count >= 1 &&
              direct_report.blocking_error_count == 0,
          "direct apply report");
    KvMapSnapshot direct_snapshot{};
    check(kv_get_map_snapshot(handle.value, KV_MAP_SNAPSHOT_VERSION,
                              &direct_snapshot, sizeof(direct_snapshot)) != 0,
          "direct snapshot");
    const KvStructurePutRow* direct_row = find_structure(direct_snapshot, edit_id);
    check(direct_row && std::abs(direct_row->x - 8.0) < 1e-9,
          "direct apply value and stable id");

    MapHandle reload(kv_load_map_ex(fixture.path_utf8().c_str(), 25.0,
                                    KV_LOAD_PREVIEW | KV_LOAD_EDIT_METADATA));
    check(reload.value != nullptr, "reload after commit");
    if (reload.value) {
        KvMapSnapshot snapshot{};
        check(kv_get_map_snapshot(reload.value, KV_MAP_SNAPSHOT_VERSION,
                                  &snapshot, sizeof(snapshot)) != 0,
              "reload snapshot");
        const KvStructurePutRow* reload_row = find_structure(snapshot, edit_id);
        check(reload_row && std::abs(reload_row->x - 8.0) < 1e-9,
              "save/reload identity and value");
    }
    std::cout << "typed edit contract " << (failures ? "FAIL" : "PASS") << '\n';
    return failures;
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "usage: typed_snapshot_tests <snapshot|edit>\n";
        return 2;
    }
    const std::string mode = argv[1];
    if (mode == "snapshot") return snapshot_contract() == 0 ? 0 : 1;
    if (mode == "edit") return edit_contract() == 0 ? 0 : 1;
    std::cerr << "unknown mode: " << mode << '\n';
    return 2;
}
