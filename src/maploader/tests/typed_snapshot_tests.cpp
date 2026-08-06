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
#include <mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

int failures = 0;
std::mutex diagnostic_log_mutex;
std::vector<std::string> diagnostic_logs;

void diagnostic_log_callback(const char* message) {
    if (!message) return;
    std::lock_guard<std::mutex> lock(diagnostic_log_mutex);
    diagnostic_logs.emplace_back(message);
}

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

    explicit TempFixture(bool snapshot_arrays = false) {
        directory = std::filesystem::temp_directory_path() /
            ("komapedit-typed-contract-" + std::to_string(
                std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(directory);
        map_path = directory / "map.txt";
        std::ofstream map(map_path, std::ios::binary | std::ios::trunc);
        map << "BveTs Map 2.02:utf-8\n"
            << "0;\n"
            << "Structure.Load('structures.csv');\n"
            << "Station.Load('stations.csv');\n"
            << "Signal.Load('signals.csv');\n";
        if (snapshot_arrays) {
            map << "$snapshotContract=1;\n"
                << "DrawDistance.Change(600);\n";
        }
        map << "Track['1'].Position(3.8,0);\n"
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
                   << "pole,pole.csv\n"
                   << "main1,main1.csv\n"
                   << "main2,main2.csv\n"
                   << "glare1,glare1.csv\n"
                   << "glare2,glare2.csv\n"
                   << "mainB,mainB.csv\n"
                   << "glareB,glareB.csv\n";
        structures.close();
        std::ofstream stations(directory / "stations.csv",
                               std::ios::binary | std::ios::trunc);
        stations << "BveTs Station List 1.00:utf-8\n"
                 << "STA,Original,09:00,09:01,30,15,1,20,100,arr.wav,dep.wav,1,0,"
                    "legacy-a,legacy-b # keep station comment\n";
        stations.close();
        std::ofstream signals(directory / "signals.csv",
                              std::ios::binary | std::ios::trunc);
        signals << "BveTs Signal Aspects List 2.00:utf-8\r\n"
                << "aspectA,main1,main2\r\n"
                << ",glare1,glare2\r\n"
                << "aspectB,mainB\r\n"
                << ",glareB\r\n";
    }

    ~TempFixture() {
        std::error_code error;
        std::filesystem::remove_all(directory, error);
    }

    std::string path_utf8() const { return map_path.u8string(); }
};

void write_utf16_file(const std::filesystem::path& path,
                      const std::u16string& text,
                      bool little_endian,
                      bool append_incomplete_byte = false) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    const char bom[2] = {
        static_cast<char>(little_endian ? 0xff : 0xfe),
        static_cast<char>(little_endian ? 0xfe : 0xff),
    };
    output.write(bom, 2);
    for (char16_t unit : text) {
        const char bytes[2] = {
            static_cast<char>(little_endian ? unit & 0xff : unit >> 8),
            static_cast<char>(little_endian ? unit >> 8 : unit & 0xff),
        };
        output.write(bytes, 2);
    }
    if (append_incomplete_byte) output.put('\0');
}

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
    CHECK_ARRAY(draw_distances, draw_distance_count);
    CHECK_ARRAY(speed_limits, speed_limit_count);
    CHECK_ARRAY(variable_assignments, variable_assignment_count);
    CHECK_ARRAY(resource_list_loads, resource_list_load_count);
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
    TempFixture fixture(true);
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
    check(first.draw_distance_count == 1, "draw-distance fixture row present");
    check(first.variable_assignment_count == 1, "variable-assignment fixture row present");
    check(first.resource_list_load_count == 3, "resource-list Load fixture rows present");
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

    {
        TempFixture huge_distance_fixture;
        std::ofstream huge_map(huge_distance_fixture.map_path,
                               std::ios::binary | std::ios::trunc);
        huge_map << "BveTs Map 2.02:utf-8\n"
                 << "0;\n"
                 << "1000000000000;\n";
        huge_map.close();

        MapHandle huge_distance_handle(kv_load_map_ex(
            huge_distance_fixture.path_utf8().c_str(), 1.0e12, KV_LOAD_PREVIEW));
        check(huge_distance_handle.value != nullptr, "huge-distance preview load");
        if (huge_distance_handle.value) {
            check(kv_generate_scene_geometry(huge_distance_handle.value, 1.0e12,
                                             0.25, 25.0, 1.0, 0.01) == 0,
                  "huge adaptive subdivision rejected");
            const char* error = kv_get_last_error();
            check(error && std::string_view(error).find(
                               "adaptive scene subdivision count exceeds supported range") !=
                               std::string_view::npos,
                  "huge adaptive subdivision error");
        }
    }

    {
        TempFixture numeric_fixture;
        std::ofstream map(numeric_fixture.map_path, std::ios::binary | std::ios::trunc);
        map << "BveTs Map 2.02:utf-8\n"
            << "0;\n"
            << "$finiteLeft='x'+12.75;\n"
            << "$finiteRight=12.75+'x';\n"
            << "$positiveInfinity='x'+(1/0);\n"
            << "$negativeInfinity='x'+((-1)/0);\n"
            << "$notANumber='x'+((1/0)-(1/0));\n"
            << "$largeFinite='x'+1e300;\n"
            << "$smallFinite='x'+(-1e300);\n"
            << "Track[1/0].Position(3.8,0);\n";
        map.close();

        MapHandle numeric_handle(kv_load_map_ex(
            numeric_fixture.path_utf8().c_str(), 25.0, KV_LOAD_PREVIEW));
        check(numeric_handle.value != nullptr, "non-finite numeric text preview load");
        if (numeric_handle.value) {
            KvMapSnapshot numeric{};
            check(kv_get_map_snapshot(numeric_handle.value, KV_MAP_SNAPSHOT_VERSION,
                                      &numeric, sizeof(numeric)) != 0,
                  "non-finite numeric text snapshot");
            auto assignment_text = [&](std::string_view name) {
                for (std::uint64_t i = 0; i < numeric.variable_assignment_count; ++i) {
                    const KvVariableAssignmentRow& row = numeric.variable_assignments[i];
                    if (map_string(numeric, row.normalized_name) == name &&
                        row.value.kind == KV_VALUE_STRING) {
                        return map_string(numeric, row.value.string_value);
                    }
                }
                return std::string{};
            };
            check(assignment_text("finiteleft") == "x12", "finite right operand truncation bytes");
            check(assignment_text("finiteright") == "12x", "finite left operand truncation bytes");
            check(assignment_text("positiveinfinity").find("inf") != std::string::npos,
                  "positive infinity uses numeric text");
            check(assignment_text("negativeinfinity").find("-inf") != std::string::npos,
                  "negative infinity uses numeric text");
            check(assignment_text("notanumber").find("nan") != std::string::npos,
                  "NaN uses numeric text");
            check(assignment_text("largefinite").find("e+300") != std::string::npos,
                  "large finite value uses numeric text");
            check(assignment_text("smallfinite").find("e+300") != std::string::npos,
                  "small finite value uses numeric text");
            check(numeric.other_track_count == 1 &&
                      map_string(numeric, numeric.other_tracks[0].key) == "1e999",
                  "non-finite numeric key uses numeric text");
        }
    }

    {
        TempFixture nan_curve_fixture;
        std::ofstream map(nan_curve_fixture.map_path, std::ios::binary | std::ios::trunc);
        map << "BveTs Map 2.02:utf-8\n"
            << "0;\n"
            << "Curve.BeginTransition();\n"
            << "25;\n"
            << "Curve.Begin((1/0)-(1/0),0);\n"
            << "50;\n";
        map.close();
        MapHandle nan_curve_handle(kv_load_map_ex(
            nan_curve_fixture.path_utf8().c_str(), 25.0, KV_LOAD_PREVIEW));
        check(nan_curve_handle.value != nullptr, "NaN transition preview load");
        if (nan_curve_handle.value) {
            KvMapSnapshot nan_curve{};
            check(kv_get_map_snapshot(nan_curve_handle.value, KV_MAP_SNAPSHOT_VERSION,
                                      &nan_curve, sizeof(nan_curve)) != 0,
                  "NaN transition snapshot");
            bool propagated_nan = false;
            const std::uint64_t value_count = nan_curve.own_track_geometry.rows *
                nan_curve.own_track_geometry.cols;
            for (std::uint64_t i = 0; i < value_count; ++i) {
                propagated_nan = propagated_nan || std::isnan(nan_curve.own_track_geometry.data[i]);
            }
            check(propagated_nan, "NaN Fresnel input propagates into transition geometry");
        }
    }

    {
        TempFixture half_sine_limit_fixture;
        std::ofstream map(half_sine_limit_fixture.map_path,
                          std::ios::binary | std::ios::trunc);
        map << "BveTs Map 2.02:utf-8\n"
            << "0;\n"
            << "Curve.SetFunction(0);\n"
            << "Curve.BeginTransition();\n"
            << "25;\n"
            << "Curve.Begin(0.0001,0);\n"
            << "50;\n";
        map.close();
        MapHandle half_sine_handle(kv_load_map_ex(
            half_sine_limit_fixture.path_utf8().c_str(), 25.0, KV_LOAD_PREVIEW));
        check(half_sine_handle.value == nullptr,
              "pathological half-sine transition is rejected");
        const char* error = kv_get_last_error();
        check(error && std::string_view(error).find(
                           "Half-sine transition exceeds the supported integration limit") !=
                           std::string_view::npos,
              "pathological half-sine transition reports its integration limit");
    }

    {
        TempFixture utf16_fixture;
        write_utf16_file(
            utf16_fixture.map_path,
            u"BveTs Map 2.02:utf-16le\n# \U0001F642\n0;\n", true);
        MapHandle valid_le(kv_load_map_ex(
            utf16_fixture.path_utf8().c_str(), 25.0, KV_LOAD_PREVIEW));
        check(valid_le.value != nullptr, "valid UTF-16LE surrogate pair loads");

        write_utf16_file(
            utf16_fixture.map_path,
            u"BveTs Map 2.02:utf-16be\n# \U0001F642\n0;\n", false);
        MapHandle valid_be(kv_load_map_ex(
            utf16_fixture.path_utf8().c_str(), 25.0, KV_LOAD_PREVIEW));
        check(valid_be.value != nullptr, "valid UTF-16BE surrogate pair loads");

        write_utf16_file(
            utf16_fixture.map_path,
            u"BveTs Map 2.02:utf-16le\n0;\n", true, true);
        MapHandle odd_length(kv_load_map_ex(
            utf16_fixture.path_utf8().c_str(), 25.0, KV_LOAD_PREVIEW));
        check(odd_length.value == nullptr, "odd-length UTF-16 input is rejected");
        const char* odd_error = kv_get_last_error();
        check(odd_error && std::string_view(odd_error).find(
                               "incomplete trailing code unit") != std::string_view::npos,
              "odd-length UTF-16 input reports its cause");

        std::u16string high = u"BveTs Map 2.02:utf-16le\n#";
        high.push_back(static_cast<char16_t>(0xd800));
        high += u"\n0;\n";
        write_utf16_file(utf16_fixture.map_path, high, true);
        MapHandle unpaired_high(kv_load_map_ex(
            utf16_fixture.path_utf8().c_str(), 25.0, KV_LOAD_PREVIEW));
        check(unpaired_high.value == nullptr, "unpaired UTF-16 high surrogate is rejected");

        std::u16string low = u"BveTs Map 2.02:utf-16le\n#";
        low.push_back(static_cast<char16_t>(0xdc00));
        low += u"\n0;\n";
        write_utf16_file(utf16_fixture.map_path, low, true);
        MapHandle unpaired_low(kv_load_map_ex(
            utf16_fixture.path_utf8().c_str(), 25.0, KV_LOAD_PREVIEW));
        check(unpaired_low.value == nullptr, "unpaired UTF-16 low surrogate is rejected");
    }

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

    UpdateBatch(std::string id, std::string hash, std::string value,
                std::string name = "x")
        : edit_id(std::move(id)), source_hash(std::move(hash)),
          field_name(std::move(name)), field_value(std::move(value)) {
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

const KvStationListRow* find_station_list_row(const KvMapSnapshot& snapshot,
                                              std::string_view edit_id) {
    for (std::uint64_t i = 0; i < snapshot.station_list_count; ++i) {
        const KvStationListRow& row = snapshot.station_list[i];
        if (arena_view(snapshot.string_data, snapshot.string_size,
                       row.metadata.edit_id) == edit_id) {
            return &row;
        }
    }
    return nullptr;
}

const KvSignalAspectRow* find_signal_aspect(const KvMapSnapshot& snapshot,
                                            std::string_view edit_id) {
    for (std::uint64_t i = 0; i < snapshot.signal_aspect_count; ++i) {
        const KvSignalAspectRow& row = snapshot.signal_aspects[i];
        if (arena_view(snapshot.string_data, snapshot.string_size,
                       row.metadata.edit_id) == edit_id) {
            return &row;
        }
    }
    return nullptr;
}

const KvSignalAspectRow* find_signal_aspect_key(const KvMapSnapshot& snapshot,
                                                std::string_view key) {
    for (std::uint64_t i = 0; i < snapshot.signal_aspect_count; ++i) {
        const KvSignalAspectRow& row = snapshot.signal_aspects[i];
        if (arena_view(snapshot.string_data, snapshot.string_size,
                       row.signal_aspect_key) == key) {
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

    check(baseline.station_list_count == 1, "Station.List fixture row present");
    if (baseline.station_list_count == 1) {
        const KvStationListRow& station_row = baseline.station_list[0];
        const std::string station_edit_id =
            map_string(baseline, station_row.metadata.edit_id);
        check(station_row.metadata.source_file_index < baseline.source_file_count,
              "Station.List source index");
        if (station_row.metadata.source_file_index < baseline.source_file_count) {
            const KvSourceFileRow& station_source =
                baseline.source_files[station_row.metadata.source_file_index];
            const std::string station_source_hash =
                map_string(baseline, station_source.source_hash);
            const std::string station_source_path =
                map_string(baseline, station_source.file_path);
            KvEditTargetSnapshot station_target{};
            check(kv_get_edit_target_typed(handle.value, utf8_view(station_edit_id),
                                           &station_target, sizeof(station_target)) != 0 &&
                      station_target.elements_for_statement == 1,
                  "Station.List target maps to one editable element");

            UpdateBatch station_name_update(
                station_edit_id, station_source_hash, "Edited", "stationName");
            KvEditReportSnapshot station_name_report{};
            check(kv_edit_apply_to_memory_typed(
                      handle.value, &station_name_update.batch,
                      &station_name_report, sizeof(station_name_report)) != 0 &&
                      station_name_report.ok,
                  "Station.List name apply-to-memory");
            KvMapSnapshot station_name_snapshot{};
            check(kv_get_map_snapshot(handle.value, KV_MAP_SNAPSHOT_VERSION,
                                      &station_name_snapshot,
                                      sizeof(station_name_snapshot)) != 0,
                  "Station.List name snapshot");
            const KvStationListRow* edited_station =
                find_station_list_row(station_name_snapshot, station_edit_id);
            check(edited_station &&
                      map_string(station_name_snapshot, edited_station->fields[1]) == "Edited",
                  "Station.List name value");
            const char* station_source_text =
                kv_get_source_text(handle.value, station_source_path.c_str());
            check(station_source_text &&
                      std::string_view(station_source_text).find(
                          "STA,Edited,09:00,09:01,30,15,1,20,100,arr.wav,dep.wav,1,0,"
                          "legacy-a,legacy-b # keep station comment") !=
                          std::string_view::npos,
                  "Station.List preserves trailing fields and inline comment");
            kv_free_string(station_source_text);
            check(kv_edit_reset_memory(handle.value) != 0,
                  "Station.List name reset");

            UpdateBatch station_key_update(
                station_edit_id, station_source_hash, "STB", "stationKey");
            KvEditReportSnapshot station_key_report{};
            check(kv_edit_apply_to_memory_typed(
                      handle.value, &station_key_update.batch,
                      &station_key_report, sizeof(station_key_report)) != 0 &&
                      station_key_report.ok,
                  "Station.List key apply-to-memory");
            KvMapSnapshot station_key_snapshot{};
            check(kv_get_map_snapshot(handle.value, KV_MAP_SNAPSHOT_VERSION,
                                      &station_key_snapshot,
                                      sizeof(station_key_snapshot)) != 0,
                  "Station.List key snapshot");
            const KvStationListRow* renamed_station =
                find_station_list_row(station_key_snapshot, station_edit_id);
            check(renamed_station &&
                      map_string(station_key_snapshot, renamed_station->fields[0]) == "STB",
                  "Station.List key value and stable identity");
            check(kv_edit_reset_memory(handle.value) != 0,
                  "Station.List key reset");

            KvEditReportSnapshot station_save_apply_report{};
            check(kv_edit_apply_to_memory_typed(
                      handle.value, &station_name_update.batch,
                      &station_save_apply_report, sizeof(station_save_apply_report)) != 0 &&
                      station_save_apply_report.ok,
                  "Station.List apply before save");
            KvEditReportSnapshot station_save_report{};
            check(kv_edit_commit_typed(handle.value, &station_save_report,
                                       sizeof(station_save_report)) != 0 &&
                      station_save_report.ok,
                  "Station.List save");
            MapHandle station_reload(kv_load_map_ex(
                fixture.path_utf8().c_str(), 25.0,
                KV_LOAD_PREVIEW | KV_LOAD_EDIT_METADATA));
            check(station_reload.value != nullptr, "Station.List reload after save");
            if (station_reload.value) {
                KvMapSnapshot station_reload_snapshot{};
                check(kv_get_map_snapshot(station_reload.value, KV_MAP_SNAPSHOT_VERSION,
                                          &station_reload_snapshot,
                                          sizeof(station_reload_snapshot)) != 0,
                      "Station.List reload snapshot");
                const KvStationListRow* reloaded_station =
                    find_station_list_row(station_reload_snapshot, station_edit_id);
                check(reloaded_station &&
                          map_string(station_reload_snapshot,
                                     reloaded_station->fields[1]) == "Edited",
                      "Station.List saved value and stable identity");
            }
        }
    }

    KvMapSnapshot signal_baseline{};
    check(kv_get_map_snapshot(handle.value, KV_MAP_SNAPSHOT_VERSION,
                              &signal_baseline, sizeof(signal_baseline)) != 0,
          "Signal aspect baseline snapshot");
    check(signal_baseline.signal_aspect_count == 2,
          "Signal aspect fixture rows present");
    if (signal_baseline.signal_aspect_count >= 1) {
        const KvSignalAspectRow& signal_row = signal_baseline.signal_aspects[0];
        const std::string signal_edit_id =
            map_string(signal_baseline, signal_row.metadata.edit_id);
        check(signal_row.metadata.source_file_index < signal_baseline.source_file_count,
              "Signal aspect source index");
        if (signal_row.metadata.source_file_index < signal_baseline.source_file_count) {
            const KvSourceFileRow& signal_source =
                signal_baseline.source_files[signal_row.metadata.source_file_index];
            const std::string signal_source_hash =
                map_string(signal_baseline, signal_source.source_hash);
            const std::string signal_source_path =
                map_string(signal_baseline, signal_source.file_path);
            UpdateBatch delete_glare(
                signal_edit_id, signal_source_hash, "1", "deleteGlare");
            KvEditReportSnapshot delete_glare_dry_run{};
            check(kv_edit_dry_run_typed(
                      handle.value, &delete_glare.batch,
                      &delete_glare_dry_run, sizeof(delete_glare_dry_run)) != 0 &&
                      delete_glare_dry_run.ok &&
                      delete_glare_dry_run.full_reparse_ok &&
                      delete_glare_dry_run.non_target_changed_count == 0,
                  "Signal aspect glare delete dry-run");
            KvEditReportSnapshot delete_glare_applied{};
            check(kv_edit_apply_to_memory_typed(
                      handle.value, &delete_glare.batch,
                      &delete_glare_applied, sizeof(delete_glare_applied)) != 0 &&
                      delete_glare_applied.ok,
                  "Signal aspect glare delete apply-to-memory");
            KvMapSnapshot glare_applied{};
            check(kv_get_map_snapshot(handle.value, KV_MAP_SNAPSHOT_VERSION,
                                      &glare_applied, sizeof(glare_applied)) != 0,
                  "Signal aspect glare delete snapshot");
            const KvSignalAspectRow* glare_deleted =
                find_signal_aspect(glare_applied, signal_edit_id);
            check(glare_deleted &&
                      glare_deleted->metadata.reserved == 2 &&
                      glare_deleted->structure_keys.count ==
                          glare_deleted->metadata.reserved,
                  "Signal aspect glare rows removed with stable identity");
            const char* signal_source_text =
                kv_get_source_text(handle.value, signal_source_path.c_str());
            check(signal_source_text &&
                      std::string_view(signal_source_text).find(
                          "aspectA,main1,main2") != std::string_view::npos &&
                      std::string_view(signal_source_text).find(
                          "aspectB,mainB") != std::string_view::npos &&
                      std::string_view(signal_source_text).find(
                          ",glare1,glare2") == std::string_view::npos,
                  "Signal aspect glare delete preserves the next logical row");
            kv_free_string(signal_source_text);
            check(kv_edit_reset_memory(handle.value) != 0,
                  "Signal aspect glare delete reset");

            KvEditReportSnapshot delete_glare_save_apply{};
            check(kv_edit_apply_to_memory_typed(
                      handle.value, &delete_glare.batch,
                      &delete_glare_save_apply, sizeof(delete_glare_save_apply)) != 0 &&
                      delete_glare_save_apply.ok,
                  "Signal aspect glare delete apply before save");
            KvEditReportSnapshot delete_glare_save{};
            check(kv_edit_commit_typed(handle.value, &delete_glare_save,
                                       sizeof(delete_glare_save)) != 0 &&
                      delete_glare_save.ok,
                  "Signal aspect glare delete save");
            MapHandle signal_reload(kv_load_map_ex(
                fixture.path_utf8().c_str(), 25.0,
                KV_LOAD_PREVIEW | KV_LOAD_EDIT_METADATA));
            check(signal_reload.value != nullptr,
                  "Signal aspect glare delete reload after save");
            if (signal_reload.value) {
                KvMapSnapshot signal_reload_snapshot{};
                check(kv_get_map_snapshot(
                          signal_reload.value, KV_MAP_SNAPSHOT_VERSION,
                          &signal_reload_snapshot,
                          sizeof(signal_reload_snapshot)) != 0,
                      "Signal aspect glare delete reload snapshot");
                const KvSignalAspectRow* reloaded_signal =
                    find_signal_aspect_key(signal_reload_snapshot, "aspectA");
                check(reloaded_signal &&
                          reloaded_signal->structure_keys.count ==
                              reloaded_signal->metadata.reserved,
                      "Signal aspect glare delete saved value");
            }
        }
    }

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

int signal_glare_headless(const std::filesystem::path& map_path, bool commit) {
    constexpr size_t k_target_count = 3;
    struct Target {
        std::string edit_id;
        std::string aspect_key;
        std::string source_hash;
        std::string source_file;
        std::uint64_t main_structure_key_count = 0;
        int line = 0;
    };

    std::cout << "signal glare headless path=\"" << map_path.u8string()
              << "\" commit=" << (commit ? "true" : "false") << '\n';
    try {
        const std::string map_path_utf8 = map_path.u8string();
        MapHandle handle(kv_load_map_ex(map_path_utf8.c_str(), 25.0,
                                        KV_LOAD_EDIT_METADATA));
        if (!handle.value) {
            const char* error = kv_get_last_error();
            throw std::runtime_error(error ? error : "map load failed");
        }

        KvMapSnapshot baseline{};
        if (!kv_get_map_snapshot(handle.value, KV_MAP_SNAPSHOT_VERSION,
                                 &baseline, sizeof(baseline))) {
            throw std::runtime_error("failed to retrieve the baseline map snapshot");
        }
        if (baseline.signal_aspect_count != 0 && !baseline.signal_aspects) {
            throw std::runtime_error("baseline signal aspect array is null");
        }

        std::vector<Target> targets;
        targets.reserve(k_target_count);
        for (std::uint64_t i = 0;
             i < baseline.signal_aspect_count && targets.size() < k_target_count;
             ++i) {
            const KvSignalAspectRow& row = baseline.signal_aspects[i];
            if (row.metadata.reserved >= row.structure_keys.count ||
                row.metadata.source_file_index >= baseline.source_file_count) {
                continue;
            }
            const std::string aspect_key =
                map_string(baseline, row.signal_aspect_key);
            if (aspect_key.empty()) continue;
            bool key_is_unique = true;
            for (std::uint64_t j = 0; j < baseline.signal_aspect_count; ++j) {
                if (i == j) continue;
                if (map_string(baseline,
                               baseline.signal_aspects[j].signal_aspect_key) == aspect_key) {
                    key_is_unique = false;
                    break;
                }
            }
            if (!key_is_unique) continue;

            const KvSourceFileRow& source =
                baseline.source_files[row.metadata.source_file_index];
            Target target;
            target.edit_id = map_string(baseline, row.metadata.edit_id);
            target.aspect_key = aspect_key;
            target.source_hash = map_string(baseline, source.source_hash);
            target.source_file = map_string(baseline, source.file_path);
            target.main_structure_key_count = row.metadata.reserved;
            target.line = row.metadata.line;
            if (target.edit_id.empty() || target.source_hash.empty() ||
                target.source_file.empty()) {
                continue;
            }
            targets.push_back(std::move(target));
        }
        if (targets.size() != k_target_count) {
            throw std::runtime_error(
                "fewer than three uniquely addressable signal aspect glare rows were found");
        }

        const std::string field_name = "deleteGlare";
        const std::string field_value = "1";
        std::vector<std::string> change_ids;
        std::vector<KvEditField> fields;
        std::vector<KvEditChange> changes;
        change_ids.reserve(targets.size());
        fields.reserve(targets.size());
        changes.reserve(targets.size());
        for (size_t index = 0; index < targets.size(); ++index) {
            change_ids.push_back("headless-delete-glare-" +
                                 std::to_string(index + 1));
            KvEditField field{};
            field.name = utf8_view(field_name);
            field.value = utf8_view(field_value);
            fields.push_back(field);

            KvEditChange change{};
            change.change_id = utf8_view(change_ids.back());
            change.edit_id = utf8_view(targets[index].edit_id);
            change.operation = KV_EDIT_UPDATE;
            change.fields = KvSpan{static_cast<std::uint64_t>(index), 1};
            change.expected_source_hash = utf8_view(targets[index].source_hash);
            changes.push_back(change);
        }
        const KvEditBatch batch{
            changes.data(), static_cast<std::uint64_t>(changes.size()),
            fields.data(), static_cast<std::uint64_t>(fields.size()),
        };
        auto first_blocking_error = [](const KvEditReportSnapshot& report) {
            if (report.blocking_error_count == 0 || !report.blocking_errors) {
                return std::string{};
            }
            const std::string_view error = arena_view(
                report.string_data, report.string_size, report.blocking_errors[0]);
            return std::string(error.data(), error.size());
        };
        auto require_edit_report = [&](const KvEditReportSnapshot& report,
                                       const char* phase) {
            if (report.ok && report.full_reparse_ok &&
                report.update_count == static_cast<int>(targets.size()) &&
                report.non_target_changed_count == 0) {
                return;
            }
            std::string message = phase;
            const std::string error = first_blocking_error(report);
            if (!error.empty()) message += ": " + error;
            throw std::runtime_error(message);
        };
        auto all_glare_rows_removed = [&](void* current_handle,
                                          bool require_stable_edit_ids) {
            KvMapSnapshot snapshot{};
            if (!kv_get_map_snapshot(current_handle, KV_MAP_SNAPSHOT_VERSION,
                                     &snapshot, sizeof(snapshot))) {
                return false;
            }
            if (snapshot.signal_aspect_count != 0 && !snapshot.signal_aspects) {
                return false;
            }
            for (const Target& target : targets) {
                const KvSignalAspectRow* row = require_stable_edit_ids
                    ? find_signal_aspect(snapshot, target.edit_id)
                    : find_signal_aspect_key(snapshot, target.aspect_key);
                if (!row ||
                    row->metadata.reserved != target.main_structure_key_count ||
                    row->structure_keys.count != target.main_structure_key_count) {
                    return false;
                }
            }
            return true;
        };

        KvEditReportSnapshot dry_run{};
        if (!kv_edit_dry_run_typed(handle.value, &batch, &dry_run,
                                   sizeof(dry_run))) {
            const char* error = kv_get_last_error();
            throw std::runtime_error(error ? error : "glare-delete dry-run failed");
        }
        require_edit_report(dry_run, "glare-delete dry-run validation failed");

        KvEditReportSnapshot applied{};
        if (!kv_edit_apply_to_memory_typed(handle.value, &batch, &applied,
                                           sizeof(applied))) {
            const char* error = kv_get_last_error();
            throw std::runtime_error(error ? error : "glare-delete Apply failed");
        }
        require_edit_report(applied, "glare-delete Apply validation failed");
        if (!all_glare_rows_removed(handle.value, true)) {
            throw std::runtime_error(
                "glare-delete Apply did not retain target identities and main rows");
        }

        if (!kv_edit_reset_memory(handle.value)) {
            const char* error = kv_get_last_error();
            throw std::runtime_error(error ? error : "glare-delete Reset failed");
        }
        KvMapSnapshot reset{};
        if (!kv_get_map_snapshot(handle.value, KV_MAP_SNAPSHOT_VERSION,
                                 &reset, sizeof(reset))) {
            throw std::runtime_error("failed to retrieve the reset map snapshot");
        }
        for (const Target& target : targets) {
            const KvSignalAspectRow* row =
                find_signal_aspect(reset, target.edit_id);
            if (!row || row->metadata.reserved != target.main_structure_key_count ||
                row->structure_keys.count <= target.main_structure_key_count) {
                throw std::runtime_error(
                    "glare-delete Reset did not restore the original glare row");
            }
        }

        if (commit) {
            KvEditReportSnapshot reapplied{};
            if (!kv_edit_apply_to_memory_typed(handle.value, &batch, &reapplied,
                                               sizeof(reapplied))) {
                const char* error = kv_get_last_error();
                throw std::runtime_error(error ? error : "glare-delete re-Apply failed");
            }
            require_edit_report(reapplied,
                                "glare-delete pre-Save validation failed");

            KvEditReportSnapshot committed{};
            if (!kv_edit_commit_typed(handle.value, &committed, sizeof(committed))) {
                const char* error = kv_get_last_error();
                throw std::runtime_error(error ? error : "glare-delete Save failed");
            }
            if (!committed.ok || committed.committed_file_count == 0 ||
                committed.non_target_changed_count != 0) {
                std::string message = "glare-delete Save validation failed";
                const std::string error = first_blocking_error(committed);
                if (!error.empty()) message += ": " + error;
                throw std::runtime_error(message);
            }

            MapHandle reloaded(kv_load_map_ex(map_path_utf8.c_str(), 25.0,
                                               KV_LOAD_EDIT_METADATA));
            if (!reloaded.value || !all_glare_rows_removed(reloaded.value, false)) {
                const char* error = kv_get_last_error();
                throw std::runtime_error(error
                    ? error
                    : "glare-delete Save did not persist after a fresh reload");
            }
        }

        for (const Target& target : targets) {
            std::cout << "target aspect=" << target.aspect_key
                      << " file=\"" << target.source_file
                      << "\" line=" << target.line << '\n';
        }
        std::cout << "dry_run=PASS\napply_to_memory=PASS\nreset=PASS\n";
        if (commit) std::cout << "save_reload=PASS\n";
        std::cout << "result=PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cout << "error=" << error.what() << "\nresult=FAIL\n";
        return 1;
    }
}

bool diagnostics_contain(std::string_view needle) {
    std::lock_guard<std::mutex> lock(diagnostic_log_mutex);
    for (const std::string& line : diagnostic_logs) {
        if (line.find(needle) != std::string::npos) return true;
    }
    return false;
}

void clear_diagnostics() {
    std::lock_guard<std::mutex> lock(diagnostic_log_mutex);
    diagnostic_logs.clear();
}

int diagnostics_contract(const std::filesystem::path& fixture_root) {
    kv_set_log_callback(diagnostic_log_callback);
    auto fixture_path = [&](const char* name) {
        return (fixture_root / name).u8string();
    };

    TempFixture diagnostic_fixture;
    std::filesystem::copy(
        fixture_root / "map_error_assets",
        diagnostic_fixture.directory / "map_error_assets",
        std::filesystem::copy_options::recursive);
    std::ifstream diagnostic_input(fixture_root / "map_errors.txt", std::ios::binary);
    std::ofstream diagnostic_output(
        diagnostic_fixture.map_path, std::ios::binary | std::ios::trunc);
    const std::array<std::string_view, 4> resource_error_statements{{
        "Station.Load('map_error_assets/missing_stations.csv');",
        "Structure.Load('map_error_assets/not_a_list.csv');",
        "Signal.Load('map_error_assets/unopenable');",
        "Sound.Load('map_error_assets/missing_sounds.csv');",
    }};
    std::string diagnostic_line;
    while (std::getline(diagnostic_input, diagnostic_line)) {
        std::string_view comparable_line = diagnostic_line;
        if (!comparable_line.empty() && comparable_line.back() == '\r') {
            comparable_line.remove_suffix(1);
        }
        if (std::find(resource_error_statements.begin(), resource_error_statements.end(),
                      comparable_line) != resource_error_statements.end()) {
            continue;
        }
        diagnostic_output << diagnostic_line << '\n';
    }
    diagnostic_output.close();

    clear_diagnostics();
    MapHandle nonfatal(kv_load_map_ex(diagnostic_fixture.path_utf8().c_str(), 25.0,
                                      KV_LOAD_PREVIEW | KV_LOAD_EDIT_METADATA));
    check(nonfatal.value != nullptr, "nonfatal diagnostics map returns a handle");
    check(diagnostics_contain("[WARN]"), "nonfatal diagnostics use warning severity");
    check(diagnostics_contain("Parameter count error"), "parameter count warning");
    check(diagnostics_contain("Parameter content error"), "parameter content warning");
    check(diagnostics_contain("Unknown submethod"), "unknown submethod warning");
    check(diagnostics_contain("Unknown element name"), "unknown element warning");
    check(diagnostics_contain("Unknown StructureKey"), "unknown structure key warning");
    check(diagnostics_contain("Unknown SoundKey"), "unknown sound key warning");
    check(diagnostics_contain("Unknown Sound3DKey"), "unknown sound3d key warning");
    check(diagnostics_contain("Unknown StationKey"), "unknown station key warning");
    check(diagnostics_contain("Unknown TrainKey"), "unknown train key warning");
    check(diagnostics_contain("Undefined variable"), "undefined variable warning");
    check(diagnostics_contain("Missing statement terminator ';'"), "missing semicolon warning");
    check(diagnostics_contain("'BeginTransition' is required before 'Begin'."),
          "BeginTransition Begin warning");
    check(diagnostics_contain("'BeginTransition' is required before 'End'."),
          "BeginTransition End warning");
    if (nonfatal.value) {
        KvMapSnapshot snapshot{};
        check(kv_get_map_snapshot(nonfatal.value, KV_MAP_SNAPSHOT_VERSION,
                                  &snapshot, sizeof(snapshot)) != 0,
              "nonfatal diagnostics snapshot");
        check(snapshot.speed_limit_count == 1,
              "valid statement after syntax errors reaches IR");
    }

    const std::filesystem::path resource_error_path =
        diagnostic_fixture.directory / "resource_errors.txt";
    {
        std::ofstream resource_error_map(
            resource_error_path, std::ios::binary | std::ios::trunc);
        resource_error_map << "BveTs Map 2.02:utf-8\n0;\n";
        for (std::string_view statement : resource_error_statements) {
            resource_error_map << statement << '\n';
        }
    }
    clear_diagnostics();
    MapHandle resource_errors(kv_load_map_ex(
        resource_error_path.u8string().c_str(), 25.0,
        KV_LOAD_PREVIEW | KV_LOAD_EDIT_METADATA));
    check(resource_errors.value != nullptr, "resource diagnostics map returns a handle");
    check(diagnostics_contain("Resource list load failed: File not found"),
          "missing resource list warning");
    check(diagnostics_contain("file exists but cannot be opened"),
          "existing but unopenable resource list warning");
    check(diagnostics_contain("Invalid file header"),
          "invalid resource list header warning");

    clear_diagnostics();
    MapHandle clean(kv_load_map_ex(
        fixture_path("map_errors_no_false_positive.txt").c_str(), 25.0,
        KV_LOAD_PREVIEW | KV_LOAD_EDIT_METADATA));
    check(clean.value != nullptr, "diagnostics false-positive fixture loads");
    check(!diagnostics_contain("'BeginTransition' is required"),
          "one-argument Curve.Begin does not require BeginTransition");
    check(!diagnostics_contain("Unknown RepeaterKey"),
          "Repeater.End permits an undefined RepeaterKey");
    check(!diagnostics_contain("Parameter count error"),
          "legal Repeater variable arguments have no count warning");

    TempFixture unknown_signal_aspect;
    {
        std::ofstream map(unknown_signal_aspect.map_path,
                          std::ios::binary | std::ios::trunc);
        map << "BveTs Map 2.02:utf-8\n"
            << "0;\n"
            << "Signal['missing'].Put(0,'0',0,0);\n";
    }
    clear_diagnostics();
    MapHandle unknown_signal_handle(kv_load_map_ex(
        unknown_signal_aspect.path_utf8().c_str(), 25.0, KV_LOAD_PREVIEW));
    check(unknown_signal_handle.value != nullptr, "unknown signal aspect map loads");
    check(diagnostics_contain("Unknown SignalAspectKey"),
          "unknown signal aspect key warning");

    struct FatalCase {
        const char* file;
        const char* error_text;
    };
    const std::array<FatalCase, 3> fatal_cases{{
        {"map_error_invalid_header.txt", "Invalid file header"},
        {"map_error_missing_include.txt", "Include load failed"},
        {"map_error_invalid_include.txt", "Include load failed"},
    }};
    for (const FatalCase& fatal : fatal_cases) {
        clear_diagnostics();
        MapHandle handle(kv_load_map_ex(fixture_path(fatal.file).c_str(), 25.0,
                                        KV_LOAD_PREVIEW));
        check(handle.value == nullptr, "fatal diagnostics fixture returns null");
        const char* error = kv_get_last_error();
        check(error && std::string_view(error).find(fatal.error_text) != std::string_view::npos,
              "fatal diagnostics last error");
        check(diagnostics_contain("[ERROR]"), "fatal diagnostics use error severity");
    }

    TempFixture include_cycle;
    {
        std::ofstream map(include_cycle.map_path, std::ios::binary | std::ios::trunc);
        map << "BveTs Map 2.02:utf-8\n"
            << "Include 'map.txt';\n";
    }
    clear_diagnostics();
    MapHandle cycle_handle(kv_load_map_ex(
        include_cycle.path_utf8().c_str(), 25.0, KV_LOAD_PREVIEW));
    check(cycle_handle.value == nullptr, "cyclic Include returns null");
    const char* cycle_error = kv_get_last_error();
    check(cycle_error &&
              std::string_view(cycle_error).find("Include cycle detected") !=
                  std::string_view::npos,
          "cyclic Include reports its cause");
    check(diagnostics_contain("[ERROR]"), "cyclic Include uses error severity");

    auto write_include_chain = [](TempFixture& fixture, size_t depth) {
        for (size_t level = 0; level < depth; ++level) {
            const std::filesystem::path path = level == 0
                ? fixture.map_path
                : fixture.directory / ("include-" + std::to_string(level) + ".txt");
            std::ofstream map(path, std::ios::binary | std::ios::trunc);
            map << "BveTs Map 2.02:utf-8\n";
            if (level + 1 < depth) {
                map << "Include 'include-" << level + 1 << ".txt';\n";
            } else {
                map << "0;\n";
            }
        }
    };

    TempFixture include_depth_64;
    write_include_chain(include_depth_64, 64);
    clear_diagnostics();
    MapHandle depth_64_handle(kv_load_map_ex(
        include_depth_64.path_utf8().c_str(), 25.0, KV_LOAD_PREVIEW));
    check(depth_64_handle.value != nullptr, "64-level Include chain loads");

    TempFixture include_depth_65;
    write_include_chain(include_depth_65, 65);
    clear_diagnostics();
    MapHandle depth_65_handle(kv_load_map_ex(
        include_depth_65.path_utf8().c_str(), 25.0, KV_LOAD_PREVIEW));
    check(depth_65_handle.value == nullptr, "65-level Include chain is rejected");
    const char* depth_error = kv_get_last_error();
    check(depth_error && std::string_view(depth_error).find(
                             "Include depth exceeds the supported limit of 64") !=
                             std::string_view::npos,
          "65-level Include chain reports its depth limit");
    check(diagnostics_contain("[ERROR]"), "Include depth error uses error severity");

    kv_set_log_callback(nullptr);
    std::cout << "maploader diagnostics contract "
              << (failures ? "FAIL" : "PASS") << '\n';
    return failures;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: typed_snapshot_tests <snapshot|edit|diagnostics|signal-glare> "
                     "[fixture-root|map-path] [--commit]\n";
        return 2;
    }
    const std::string mode = argv[1];
    if (mode == "snapshot") return snapshot_contract() == 0 ? 0 : 1;
    if (mode == "edit") return edit_contract() == 0 ? 0 : 1;
    if (mode == "diagnostics" && argc == 3) {
        return diagnostics_contract(std::filesystem::path(argv[2])) == 0 ? 0 : 1;
    }
    if (mode == "signal-glare" && (argc == 3 || argc == 4)) {
        const bool commit = argc == 4 && std::string_view(argv[3]) == "--commit";
        if (argc == 4 && !commit) {
            std::cerr << "signal-glare accepts only --commit as its optional argument\n";
            return 2;
        }
        return signal_glare_headless(std::filesystem::path(argv[2]), commit);
    }
    std::cerr << "unknown mode: " << mode << '\n';
    return 2;
}
