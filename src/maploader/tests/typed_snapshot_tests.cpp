/*
 * Copyright (c) 2026 Sapporo_ningyo
 *
 * Licensed under Apache License 2.0; see LICENSE and NOTICE.
 */

#include "maploader.h"
#include "repeater_linkage.h"

#if defined(_WIN32)
#include <windows.h>
#endif

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <mutex>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
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

const double* buffer_row_at_distance(KvDoubleBuffer buffer, double distance) {
    if (!buffer.data || buffer.cols == 0) return nullptr;
    for (std::uint64_t row = 0; row < buffer.rows; ++row) {
        const double* values = buffer.data + row * buffer.cols;
        if (values[0] == distance) return values;
    }
    return nullptr;
}

bool nearly_equal(double left, double right, double tolerance = 1e-9) {
    return std::fabs(left - right) <=
        tolerance * std::max({1.0, std::fabs(left), std::fabs(right)});
}

std::string_view arena_view(const char* data, std::uint64_t size, KvStringRef ref) {
    if (!ref_valid(data, size, ref)) return {};
    return std::string_view(data ? data + ref.offset : "", static_cast<size_t>(ref.length));
}

std::string map_string(const KvMapSnapshot& snapshot, KvStringRef ref) {
    const std::string_view value = arena_view(snapshot.string_data, snapshot.string_size, ref);
    return std::string(value.data(), value.size());
}

std::string scenario_string(const KvScenarioSnapshot& snapshot, KvStringRef ref) {
    const std::string_view value = arena_view(snapshot.string_data, snapshot.string_size, ref);
    return std::string(value.data(), value.size());
}

struct TempFixture {
    std::filesystem::path directory;
    std::filesystem::path map_path;

    explicit TempFixture(bool snapshot_arrays = false, bool sound3d = false) {
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
        if (sound3d) map << "Sound3D.Load('sounds3d.csv');\n";
        if (snapshot_arrays) {
            map << "$snapshotContract=1;\n"
                << "Curve.Change(800);\n"
                << "Gradient.Begin(10);\n"
                << "DrawDistance.Change(600);\n"
                << "include 'legacy.txt';\n";
        }
        map << "Track['1'].Position(3.8, 0); # preserve other-track layout\n"
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
        if (sound3d) {
            map << "225;\n"
                << "Sound3D['ambient'].Put(1.25,2.5);\n"
                << "250;\n"
                << "Track['1'].Position(4.2,0);\n";
        }
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
                    "legacy-a,legacy-b // keep station comment\n";
        stations.close();
        std::ofstream signals(directory / "signals.csv",
                              std::ios::binary | std::ios::trunc);
        signals << "BveTs Signal Aspects List 2.00:utf-8\r\n"
                << "aspectA,main1,main2\r\n"
                << ",glare1,glare2\r\n"
                << "aspectB,mainB\r\n"
                << ",glareB\r\n";
        if (sound3d) {
            std::ofstream sounds3d(directory / "sounds3d.csv",
                                   std::ios::binary | std::ios::trunc);
            sounds3d << "BveTs Sound List 2.00:utf-8\n"
                     << "ambient,ambient.wav\n";
        }
        if (snapshot_arrays) {
            std::ofstream legacy_fog_map(directory / "legacy.txt",
                                         std::ios::binary | std::ios::trunc);
            legacy_fog_map << "BveTs Map 2.02:utf-8\n"
                           << "0;\n"
                           << "Legacy.Fog(50, 600, 128, 128, 128);\n";
        }
    }

    ~TempFixture() {
        std::error_code error;
        std::filesystem::remove_all(directory, error);
    }

    std::string path_utf8() const { return map_path.u8string(); }
};

struct RepeaterRenameFixture {
    std::filesystem::path directory;
    std::filesystem::path map_path;

    RepeaterRenameFixture() {
        directory = std::filesystem::temp_directory_path() /
            ("komapedit-repeater-rename-contract-" + std::to_string(
                std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(directory);
        map_path = directory / "map.txt";
        std::ofstream map(map_path, std::ios::binary | std::ios::trunc);
        map << "BveTs Map 2.02:utf-8\n"
            << "0;\n"
            << "Repeater['shared'].Begin0('0',0,0,25,'shared-a');\n"
            << "100;\n"
            << "Repeater['shared'].End();\n"
            << "Repeater['target'].Begin0('0',0,0,25, 'target-a');\n"
            << "110;\n"
            << "Repeater['blocker'].Begin0('0',0,0,25,'blocker-a');\n"
            << "150;\n"
            << "Repeater['target'].Begin0('0',0,0,25, 'target-b');\n"
            << "180;\n"
            << "Repeater['blocker'].End();\n"
            << "200;\n"
            << "Repeater['target'].End();\n"
            << "300;\n"
            << "Repeater['distant'].Begin0('0',0,0,25,'distant-a');\n"
            << "350;\n"
            << "Repeater['distant'].End();\n";
    }

    ~RepeaterRenameFixture() {
        std::error_code error;
        std::filesystem::remove_all(directory, error);
    }

    std::string path_utf8() const { return map_path.u8string(); }
};

struct OtherTrackRenameFixture {
    std::filesystem::path directory;
    std::filesystem::path map_path;
    std::filesystem::path include_path;

    OtherTrackRenameFixture() {
        directory = std::filesystem::temp_directory_path() /
            ("komapedit-other-track-rename-contract-" + std::to_string(
                std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(directory);
        map_path = directory / "map.txt";
        include_path = directory / "other-track.txt";
        std::ofstream map(map_path, std::ios::binary | std::ios::trunc);
        map << "BveTs Map 2.02:utf-8\n"
            << "0;\n"
            << "Structure.Load('structures.csv');\n"
            << "Signal.Load('signals.csv');\n"
            << "Track['target'].Position(3.8, 0); # preserve root layout\n"
            << "25;\n"
            << "Track['target'].X.Interpolate(4.0, 20);\n"
            << "Structure['pole'].Put('target',1,2,3,0,0,0,0,25);\n"
            << "Signal['aspect'].Put(0,'target',0,1);\n"
            << "Repeater['reference'].Begin0('target',0,25,5,'pole');\n"
            << "50;\n"
            << "Include('other-track.txt');\n"
            << "1000;\n"
            << "Track['blocker'].Position(8,0);\n"
            << "Track[7].Position(9,0);\n";
        std::ofstream included(include_path, std::ios::binary | std::ios::trunc);
        included << "BveTs Map 2.02:utf-8\n"
                 << "50;\n"
                 << "Track['target'].Y.Interpolate(0.1); # preserve include layout\n"
                 << "75;\n"
                 << "Track['target'].Gauge(1.067);\n"
                 << "100;\n"
                 << "Track['target'].Cant.SetGauge(1.067);\n";
        std::ofstream structures(directory / "structures.csv",
                                 std::ios::binary | std::ios::trunc);
        structures << "BveTs Structure List 1.00:utf-8\n"
                   << "pole,pole.csv\n";
        std::ofstream signals(directory / "signals.csv",
                              std::ios::binary | std::ios::trunc);
        signals << "BveTs Signal Aspects List 2.00:utf-8\n"
                << "aspect,pole\n";
    }

    ~OtherTrackRenameFixture() {
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
    CHECK_ARRAY(curves, curve_count);
    CHECK_ARRAY(gradients, gradient_count);
    CHECK_ARRAY(other_tracks, other_track_count);
    CHECK_ARRAY(own_track_events, own_track_event_count);
    CHECK_ARRAY(other_track_events, other_track_event_count);
    CHECK_ARRAY(other_track_changes, other_track_change_count);
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
    CHECK_ARRAY(legacy_fogs, legacy_fog_count);
    CHECK_ARRAY(light_ambient, light_ambient_count);
    CHECK_ARRAY(light_diffuse, light_diffuse_count);
    CHECK_ARRAY(light_direction, light_direction_count);
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
    for (std::uint64_t i = 0; i < snapshot.other_track_change_count; ++i) {
        const KvOtherTrackChangeRow& row = snapshot.other_track_changes[i];
        check(ref_valid(snapshot.string_data, snapshot.string_size, row.method),
              "other-track change method bounds");
        check(ref_valid(snapshot.string_data, snapshot.string_size, row.file_path),
              "other-track change file path bounds");
        check(span_valid(row.parameters, snapshot.value_count),
              "other-track change parameter span");
        check(row.track_key.kind == KV_VALUE_NUMBER ||
                  row.track_key.kind == KV_VALUE_STRING,
              "other-track change key value kind");
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

int scenario_route_contract();
void light_contract();

int snapshot_contract() {
    static_assert(KV_MAPLOADER_API_VERSION == 10u, "maploader API contract version");
    TempFixture fixture(true);
    check(kv_api_version() == KV_MAPLOADER_API_VERSION, "API version");
#if defined(_WIN32)
    HMODULE module = GetModuleHandleW(L"maploader.dll");
    check(module != nullptr, "maploader module handle");
    if (module) {
        check(GetProcAddress(module, "kv_load_map") == nullptr,
              "obsolete kv_load_map export removed");
        check(GetProcAddress(module, "kv_load_map_ex") != nullptr,
              "kv_load_map_ex export present");
    }
#endif
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
    check(first.other_track_change_count == 2,
          "one logical other-track row per source statement");
    if (first.other_track_change_count == 2) {
        check(map_string(first, first.other_track_changes[0].method) ==
                  "Track.Position" &&
              first.other_track_changes[0].parameters.count == 2,
              "other-track Position typed row");
    }
    check(first.draw_distance_count == 1, "draw-distance fixture row present");
    check(first.legacy_fog_count == 1, "legacy fog fixture row present");
    if (first.legacy_fog_count == 1) {
        const KvLegacyFogRow& legacy_fog = first.legacy_fogs[0];
        check(legacy_fog.distance == 0.0, "legacy fog distance");
        check(legacy_fog.start == 50.0 && legacy_fog.end == 600.0, "legacy fog start/end");
        check(legacy_fog.red == 128.0 && legacy_fog.green == 128.0 &&
                  legacy_fog.blue == 128.0,
              "legacy fog rgb");
    }
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

    KvMapSnapshot geometry_before_invalid_distribution{};
    check(kv_get_map_snapshot(handle.value, KV_MAP_SNAPSHOT_VERSION,
                              &geometry_before_invalid_distribution,
                              sizeof(geometry_before_invalid_distribution)) != 0,
          "geometry baseline before invalid distribution");
    check(kv_generate_geometry(handle.value, 25.0, 1, 0.0, 10000001.0, 1.0) == 0,
          "over-limit control-point distribution rejected before allocation");
    const char* distribution_error = kv_get_last_error();
    check(distribution_error && std::string_view(distribution_error).find(
                                    "control-point distribution count exceeds supported limit") !=
                                    std::string_view::npos,
          "over-limit control-point distribution error");
    check(kv_generate_geometry(handle.value, 25.0, 1, 0.0, 100.0, 0.0) == 0,
          "zero control-point step rejected");
    check(kv_generate_geometry(handle.value, 25.0, 1, 0.0, 100.0, -1.0) == 0,
          "negative control-point step rejected");
    check(kv_generate_geometry(handle.value, 25.0, 1,
                               std::numeric_limits<double>::quiet_NaN(),
                               100.0, 1.0) == 0,
          "non-finite control-point bound rejected");
    check(kv_generate_geometry(handle.value, 25.0, 1, 1.0e16, 1.0e16 + 4.0, 1.0) == 0,
          "non-advancing floating-point step rejected");
    KvMapSnapshot geometry_after_invalid_distribution{};
    check(kv_get_map_snapshot(handle.value, KV_MAP_SNAPSHOT_VERSION,
                              &geometry_after_invalid_distribution,
                              sizeof(geometry_after_invalid_distribution)) != 0 &&
              geometry_after_invalid_distribution.geometry_revision ==
                  geometry_before_invalid_distribution.geometry_revision &&
              geometry_after_invalid_distribution.own_track_geometry.data ==
                  geometry_before_invalid_distribution.own_track_geometry.data,
          "invalid control-point distribution preserves the cached geometry");

    {
        TempFixture repeater_limit_fixture;
        std::ofstream repeater_map(repeater_limit_fixture.map_path,
                                   std::ios::binary | std::ios::trunc);
        repeater_map << "BveTs Map 2.02:utf-8\n"
                     << "0;\n"
                     << "Repeater['dense'].Begin0('0',0,0,1,'missing');\n"
                     << "1000001;\n"
                     << "Repeater['dense'].End();\n";
        repeater_map.close();
        MapHandle repeater_limit_handle(kv_load_map_ex(
            repeater_limit_fixture.path_utf8().c_str(), 1000001.0, KV_LOAD_PREVIEW));
        check(repeater_limit_handle.value != nullptr, "repeater sample limit fixture loads");
        if (repeater_limit_handle.value) {
            check(kv_generate_scene_geometry(repeater_limit_handle.value, 1000001.0,
                                             1.0, 1000001.0, 1.0, 0.01) == 0,
                  "over-limit repeater scene sampling rejected before allocation");
            const char* error = kv_get_last_error();
            check(error && std::string_view(error).find(
                               "repeater scene sample count exceeds supported limit") !=
                               std::string_view::npos,
                  "over-limit repeater scene sampling error");
        }
    }

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
        TempFixture huge_version_fixture;
        std::ofstream huge_version_map(
            huge_version_fixture.map_path, std::ios::binary | std::ios::trunc);
        huge_version_map << "BveTs Map " << std::string(400, '9')
                         << ":utf-8\n0;\n";
        huge_version_map.close();
        MapHandle huge_version_handle(kv_load_map_ex(
            huge_version_fixture.path_utf8().c_str(), 25.0, KV_LOAD_PREVIEW));
        check(huge_version_handle.value == nullptr,
              "over-range header version is rejected");
        const char* error = kv_get_last_error();
        check(error && std::string_view(error).find(
                           "Header version is invalid") != std::string_view::npos,
              "over-range header version has a deterministic error");
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

    {
        TempFixture other_track_fixture;
        std::ofstream map(other_track_fixture.map_path,
                          std::ios::binary | std::ios::trunc);
        map << "BveTs Map 2.02:utf-8\n"
            << "0;\n"
            << "Track['alpha'].Position(null, 0, 100, 200);\n"
            << "Track[2].X.Interpolate(1, 50);\n"
            << "Track['alpha'].Y.Interpolate();\n"
            << "Track['alpha'].Gauge(1.067);\n"
            << "Track['alpha'].Cant(0.1);\n"
            << "Track['alpha'].Cant.SetGauge(1.067);\n"
            << "Track['alpha'].Cant.SetCenter(0.2);\n"
            << "Track['alpha'].Cant.SetFunction(1);\n"
            << "Track['alpha'].Cant.BeginTransition();\n"
            << "Track['alpha'].Cant.Begin(0.12);\n"
            << "Track['alpha'].Cant.End();\n"
            << "Track['alpha'].Cant.Interpolate(null);\n"
            << "Include('other-track.txt');\n";
        map.close();
        std::ofstream included(other_track_fixture.directory / "other-track.txt",
                               std::ios::binary | std::ios::trunc);
        included << "BveTs Map 2.02:utf-8\n"
                 << "10;\n"
                 << "Track['included'].Gauge(1.435);\n";
        included.close();
        MapHandle other_track_handle(kv_load_map_ex(
            other_track_fixture.path_utf8().c_str(), 25.0,
            KV_LOAD_PREVIEW | KV_LOAD_EDIT_METADATA));
        check(other_track_handle.value != nullptr,
              "all supported other-track methods load");
        if (other_track_handle.value) {
            KvMapSnapshot typed{};
            check(kv_get_map_snapshot(other_track_handle.value,
                                      KV_MAP_SNAPSHOT_VERSION, &typed,
                                      sizeof(typed)) != 0,
                  "all supported other-track methods snapshot");
            static constexpr std::array<std::string_view, 13> k_methods = {
                "Track.Position", "Track.X.Interpolate", "Track.Y.Interpolate",
                "Track.Gauge", "Track.Cant", "Track.Cant.SetGauge",
                "Track.Cant.SetCenter", "Track.Cant.SetFunction",
                "Track.Cant.BeginTransition", "Track.Cant.Begin",
                "Track.Cant.End", "Track.Cant.Interpolate", "Track.Gauge",
            };
            static constexpr std::array<std::uint64_t, 13> k_counts = {
                4, 2, 0, 1, 1, 1, 1, 1, 0, 1, 0, 1, 1,
            };
            check(typed.other_track_change_count == k_methods.size(),
                  "all supported methods produce one logical row");
            if (typed.other_track_change_count == k_methods.size()) {
                bool rows_match = true;
                for (size_t index = 0; index < k_methods.size(); ++index) {
                    const KvOtherTrackChangeRow& row =
                        typed.other_track_changes[index];
                    rows_match = rows_match &&
                        map_string(typed, row.method) == k_methods[index] &&
                        row.parameters.count == k_counts[index] &&
                        !map_string(typed, row.metadata.edit_id).empty();
                }
                check(rows_match, "other-track methods, shapes, and edit ids");
                check(typed.other_track_changes[0].track_key.kind == KV_VALUE_STRING &&
                          typed.other_track_changes[1].track_key.kind == KV_VALUE_NUMBER,
                      "string and numeric other-track keys retain value type");
                const KvOtherTrackChangeRow& position = typed.other_track_changes[0];
                const KvOtherTrackChangeRow& interpolate =
                    typed.other_track_changes[11];
                check(typed.values[position.parameters.offset].kind == KV_VALUE_NULL &&
                          typed.values[interpolate.parameters.offset].kind == KV_VALUE_NULL,
                      "explicit null other-track parameters are retained");
                check(map_string(typed, typed.other_track_changes[12].file_path).find(
                          "other-track.txt") != std::string::npos,
                      "Include-owned other-track statement retains its source file");
            }
        }
    }

    {
        MapHandle editable_handle(kv_load_map_ex(
            fixture.path_utf8().c_str(), 25.0,
            KV_LOAD_PREVIEW | KV_LOAD_EDIT_METADATA));
        check(editable_handle.value != nullptr, "editable array fixture load");
        if (editable_handle.value) {
            KvMapSnapshot editable{};
            check(kv_get_map_snapshot(editable_handle.value,
                                      KV_MAP_SNAPSHOT_VERSION, &editable,
                                      sizeof(editable)) != 0,
                  "editable array fixture snapshot");
            validate_map(editable, true);
            check(editable.curve_count != 0, "nonempty curve array contract");
            check(editable.gradient_count != 0, "nonempty gradient array contract");
        }
    }

    scenario_route_contract();
    std::cout << "typed snapshot contract " << (failures ? "FAIL" : "PASS") << '\n';
    return failures;
}

int scenario_route_contract() {
#if defined(_WIN32)
    HMODULE module = GetModuleHandleW(L"maploader.dll");
    check(module != nullptr, "scenario module handle");
    if (module) {
        check(GetProcAddress(module, "kv_probe_file_kind") != nullptr,
              "kv_probe_file_kind export present");
        check(GetProcAddress(module, "kv_resolve_scenario_routes") != nullptr,
              "kv_resolve_scenario_routes export present");
        check(GetProcAddress(module, "kv_free_scenario_candidates") != nullptr,
              "kv_free_scenario_candidates export present");
        check(GetProcAddress(module, "kv_load_scenario_snapshot") != nullptr,
              "kv_load_scenario_snapshot export present");
        check(GetProcAddress(module, "kv_free_scenario_snapshot") != nullptr,
              "kv_free_scenario_snapshot export present");
    }
#endif

    const std::filesystem::path directory = std::filesystem::temp_directory_path() /
        ("komapedit-scenario-contract-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(directory);
    struct Cleanup {
        const std::filesystem::path& dir;
        ~Cleanup() {
            std::error_code ec;
            std::filesystem::remove_all(dir, ec);
        }
    } cleanup{directory};

    auto write_bytes = [&](const std::filesystem::path& path,
                           const std::string& bytes) {
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        check(static_cast<bool>(file), "scenario fixture open");
        file << bytes;
    };
    auto scenario_path_string = [&](const char* name) {
        return (directory / name).u8string();
    };
    auto resolve = [&](const std::filesystem::path& path) {
        uint64_t count = 0;
        const KvScenarioRouteCandidate* candidates =
            kv_resolve_scenario_routes(path.u8string().c_str(), &count);
        return std::pair<const KvScenarioRouteCandidate*, uint64_t>(candidates,
                                                                    count);
    };
    auto load_snapshot = [&](const std::filesystem::path& path) {
        return kv_load_scenario_snapshot(path.u8string().c_str(),
                                         KV_SCENARIO_SNAPSHOT_VERSION);
    };

    std::filesystem::create_directories(directory / "maps");
    write_bytes(directory / "maps" / "map-a.txt", "BveTs Map 2.02:utf-8\n0;\n");
    write_bytes(directory / "maps" / "map-b.txt", "BveTs Map 2.02:utf-8\n0;\n");

    check(kv_probe_file_kind(
              (directory / "maps" / "map-a.txt").u8string().c_str()) ==
              KV_FILE_KIND_MAP,
          "probe identifies map header");
    write_bytes(directory / "plain.txt", "hello world\n");
    check(kv_probe_file_kind(scenario_path_string("plain.txt").c_str()) ==
              KV_FILE_KIND_UNKNOWN,
          "probe rejects plain text");

    // Official single-candidate form with comments, an unrelated key, CRLF
    // endings, and a trailing comment after the value.
    write_bytes(directory / "utf8.txt",
                "BveTs Scenario 2.00:utf-8\r\n"
                "; leading comment\r\n"
                "Title = Scenario Contract\r\n"
                "# hash comment\r\n"
                "Route = maps\\map-a.txt ; trailing comment\r\n"
                "Vehicle = train.txt\r\n");
    check(kv_probe_file_kind(scenario_path_string("utf8.txt").c_str()) ==
              KV_FILE_KIND_SCENARIO,
          "probe identifies scenario header");
    {
        auto [candidates, count] = resolve(directory / "utf8.txt");
        check(candidates != nullptr && count == 1,
              "utf-8 single candidate resolves");
        if (candidates && count == 1) {
            check(std::string(candidates[0].route_text) == "maps\\map-a.txt",
                  "route text preserved verbatim");
            check(std::string_view(candidates[0].resolved_path).find(
                      "maps\\map-a.txt") != std::string_view::npos,
                  "resolved path targets the route file");
        }
        kv_free_scenario_candidates(candidates);
    }
    kv_free_scenario_candidates(nullptr);

    // The full snapshot preserves every official field, source-relative
    // paths, source order, explicit weights, and implicit default weights.
    write_bytes(directory / "preview.txt",
                "BveTs Scenario 2.00:utf-8\n"
                "Title = Scenario Preview\n"
                "Route = maps\\missing.txt * 2.5 | maps\\map-a.txt\n"
                "RouteTitle = Preview Route\n"
                "Vehicle = trains\\train-a.txt * 3 | trains\\train-b.txt\n"
                "VehicleTitle = Preview Vehicle\n"
                "Author = Scenario Author\n"
                "Image = images\\cover.png\n"
                "Comment = Scenario Comment\n");
    {
        const KvScenarioSnapshot* snapshot = load_snapshot(directory / "preview.txt");
        check(snapshot != nullptr, "scenario snapshot loads without existing Route target");
        if (snapshot) {
            check(snapshot->version == KV_SCENARIO_SNAPSHOT_VERSION &&
                      snapshot->structure_size == sizeof(KvScenarioSnapshot),
                  "scenario snapshot version and size");
            check(scenario_string(*snapshot, snapshot->title) == "Scenario Preview" &&
                      scenario_string(*snapshot, snapshot->route_title) == "Preview Route" &&
                      scenario_string(*snapshot, snapshot->vehicle_title) == "Preview Vehicle" &&
                      scenario_string(*snapshot, snapshot->author) == "Scenario Author" &&
                      scenario_string(*snapshot, snapshot->image) == "images\\cover.png" &&
                      scenario_string(*snapshot, snapshot->comment) == "Scenario Comment",
                  "scenario snapshot stores scalar official fields");
            check(snapshot->route_count == 2 && snapshot->routes &&
                      scenario_string(*snapshot, snapshot->routes[0].path) == "maps\\missing.txt" &&
                      nearly_equal(snapshot->routes[0].weight, 2.5) &&
                      snapshot->routes[0].has_explicit_weight != 0 &&
                      scenario_string(*snapshot, snapshot->routes[1].path) == "maps\\map-a.txt" &&
                      nearly_equal(snapshot->routes[1].weight, 1.0) &&
                      snapshot->routes[1].has_explicit_weight == 0,
                  "scenario snapshot keeps Route relative paths and weights");
            check(snapshot->vehicle_count == 2 && snapshot->vehicles &&
                      scenario_string(*snapshot, snapshot->vehicles[0].path) == "trains\\train-a.txt" &&
                      nearly_equal(snapshot->vehicles[0].weight, 3.0) &&
                      snapshot->vehicles[0].has_explicit_weight != 0 &&
                      scenario_string(*snapshot, snapshot->vehicles[1].path) == "trains\\train-b.txt" &&
                      nearly_equal(snapshot->vehicles[1].weight, 1.0) &&
                      snapshot->vehicles[1].has_explicit_weight == 0,
                  "scenario snapshot keeps Vehicle relative paths and weights");
        }
        kv_free_scenario_snapshot(snapshot);
    }
    kv_free_scenario_snapshot(nullptr);
    {
        auto [candidates, count] = resolve(directory / "preview.txt");
        check(candidates == nullptr && count == 0,
              "route resolver still rejects nonexistent Route target");
        kv_free_scenario_candidates(candidates);
    }

    // Omitted encoding defaults to UTF-8; weighted multi-candidate syntax.
    write_bytes(directory / "multi.txt",
                "BveTs Scenario 2.00\n"
                "Route = maps\\map-b.txt * 2.5 | maps\\map-a.txt\n");
    {
        auto [candidates, count] = resolve(directory / "multi.txt");
        check(candidates != nullptr && count == 2,
              "weighted candidates resolve in source order");
        if (candidates && count == 2) {
            check(std::string(candidates[0].route_text) == "maps\\map-b.txt" &&
                      std::string(candidates[1].route_text) == "maps\\map-a.txt",
                  "candidate texts preserved in order");
        }
        kv_free_scenario_candidates(candidates);
    }

    // Duplicate Route entries: the last one wins.
    write_bytes(directory / "duplicate.txt",
                "BveTs Scenario 2.00\n"
                "Route = maps\\map-a.txt\n"
                "Route = maps\\map-b.txt\n");
    {
        auto [candidates, count] = resolve(directory / "duplicate.txt");
        check(candidates != nullptr && count == 1 &&
                  std::string(candidates[0].route_text) == "maps\\map-b.txt",
              "duplicate Route keeps the last entry");
        kv_free_scenario_candidates(candidates);
    }

    // shift_jis declaration decodes CP932 relative paths. The target lives in
    // a directory whose name only round-trips through the declared encoding.
    const std::wstring japanese_dir_name = L"\x5730\x56f3";
    std::filesystem::create_directories(directory / japanese_dir_name);
    write_bytes(directory / japanese_dir_name / "map.txt",
                "BveTs Map 2.02:utf-8\n0;\n");
    std::filesystem::create_directories(directory / "timetables");
    write_bytes(directory / "timetables" / "cp932.txt",
                std::string("BveTs Scenario 2.00:shift_jis\nRoute = ..\\") +
                    "\x92\x6e\x90\x7d" + "\\map.txt\n");
    {
        auto [candidates, count] = resolve(directory / "timetables" / "cp932.txt");
        check(candidates != nullptr && count == 1,
              "shift_jis route decodes and resolves");
        if (candidates && count == 1) {
            check(std::string(candidates[0].route_text) ==
                      (std::string("..\\") + "\xe5\x9c\xb0\xe5\x9b\xb3" +
                       "\\map.txt"),
                  "shift_jis route text decodes to UTF-8");
        }
        kv_free_scenario_candidates(candidates);
    }

    // Negative cases expose deterministic diagnostics.
    auto expect_failure = [&](const char* name, const std::string& body,
                              const char* message_part, const char* label) {
        write_bytes(directory / name, body);
        auto [candidates, count] = resolve(directory / name);
        check(candidates == nullptr, label);
        if (!candidates) {
            const char* error = kv_get_last_error();
            check(error && std::string_view(error).find(message_part) !=
                               std::string_view::npos,
                  (std::string(label) + " diagnostic").c_str());
        }
    };
    write_bytes(directory / "noroute.txt",
                "BveTs Scenario 2.00\nTitle = No Route Here\n");
    {
        const KvScenarioSnapshot* snapshot = load_snapshot(directory / "noroute.txt");
        check(snapshot != nullptr && snapshot->route_count == 0 &&
                  scenario_string(*snapshot, snapshot->title) == "No Route Here",
              "scenario snapshot permits a missing Route");
        kv_free_scenario_snapshot(snapshot);
    }
    expect_failure("noroute.txt",
                   "BveTs Scenario 2.00\nTitle = No Route Here\n",
                   "no Route entry", "missing Route rejected");
    expect_failure("emptyroute.txt",
                   "BveTs Scenario 2.00\nRoute =    \n",
                   "Route entry is empty", "empty Route rejected");
    expect_failure("badweight.txt",
                   "BveTs Scenario 2.00\nRoute = maps\\map-a.txt * abc\n",
                   "weight", "invalid weight rejected");
    expect_failure("zeroweight.txt",
                   "BveTs Scenario 2.00\nRoute = maps\\map-a.txt * 0\n",
                   "weight", "non-positive weight rejected");
    expect_failure("missingtarget.txt",
                   "BveTs Scenario 2.00\nRoute = maps\\missing.txt\n",
                   "does not exist", "missing target rejected");
    expect_failure("wrongheader.txt",
                   "BveTs Map 2.02:utf-8\n0;\n",
                   "Invalid file header", "map header rejected by resolver");

    std::cout << "scenario route contract " << (failures ? "FAIL" : "PASS")
              << '\n';
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

struct MultiFieldUpdateBatch {
    std::string change_id;
    std::string edit_id;
    std::string source_hash;
    std::vector<std::string> field_names;
    std::vector<std::string> field_values;
    std::vector<KvEditField> fields;
    KvEditChange change{};
    KvEditBatch batch{};

    MultiFieldUpdateBatch(
        std::string id, std::string target_edit_id, std::string hash,
        std::vector<std::pair<std::string, std::string>> updates)
        : change_id(std::move(id)), edit_id(std::move(target_edit_id)),
          source_hash(std::move(hash)) {
        field_names.reserve(updates.size());
        field_values.reserve(updates.size());
        for (auto& update : updates) {
            field_names.push_back(std::move(update.first));
            field_values.push_back(std::move(update.second));
        }
        fields.reserve(updates.size());
        for (size_t index = 0; index < updates.size(); ++index) {
            fields.push_back(KvEditField{
                utf8_view(field_names[index]), utf8_view(field_values[index])});
        }
        change.change_id = utf8_view(change_id);
        change.edit_id = utf8_view(edit_id);
        change.operation = KV_EDIT_UPDATE;
        change.fields = KvSpan{0, static_cast<std::uint64_t>(fields.size())};
        change.expected_source_hash = utf8_view(source_hash);
        batch = KvEditBatch{&change, 1, fields.data(),
                            static_cast<std::uint64_t>(fields.size())};
    }
};

struct SimpleInsertBatch {
    std::string target_file;
    std::string change_id;
    std::vector<std::string> field_names;
    std::vector<std::string> field_values;
    std::vector<KvEditField> fields;
    KvEditChange change{};
    KvEditBatch batch{};

    SimpleInsertBatch(std::string target, std::string id,
                      std::vector<std::pair<std::string, std::string>> values)
        : target_file(std::move(target)), change_id(std::move(id)) {
        field_names.reserve(values.size());
        field_values.reserve(values.size());
        for (auto& value : values) {
            field_names.push_back(std::move(value.first));
            field_values.push_back(std::move(value.second));
        }
        fields.reserve(values.size());
        for (size_t index = 0; index < values.size(); ++index) {
            fields.push_back({utf8_view(field_names[index]), utf8_view(field_values[index])});
        }
        change.change_id = utf8_view(change_id);
        change.edit_id = utf8_view(change_id);
        change.operation = KV_EDIT_INSERT;
        change.fields = KvSpan{0, static_cast<std::uint64_t>(fields.size())};
        change.target_file_path = utf8_view(target_file);
        batch = {&change, 1, fields.data(), static_cast<std::uint64_t>(fields.size())};
    }
};

struct RepeaterKeyBatch {
    std::string field_name = "repeaterKey";
    std::string field_value;
    std::vector<std::string> change_ids;
    std::vector<std::string> edit_ids;
    std::vector<std::string> source_hashes;
    std::vector<KvEditField> fields;
    std::vector<KvEditChange> changes;
    KvEditBatch batch{};

    RepeaterKeyBatch(
        const std::vector<std::pair<std::string, std::string>>& targets,
        std::string value)
        : field_value(std::move(value)) {
        change_ids.reserve(targets.size());
        edit_ids.reserve(targets.size());
        source_hashes.reserve(targets.size());
        fields.reserve(targets.size());
        changes.resize(targets.size());
        for (size_t index = 0; index < targets.size(); ++index) {
            change_ids.push_back(
                "typed-contract-repeater-key-" + std::to_string(index));
            edit_ids.push_back(targets[index].first);
            source_hashes.push_back(targets[index].second);
        }
        for (size_t index = 0; index < targets.size(); ++index) {
            fields.push_back(KvEditField{
                utf8_view(field_name), utf8_view(field_value)});
            KvEditChange& change = changes[index];
            change.change_id = utf8_view(change_ids[index]);
            change.edit_id = utf8_view(edit_ids[index]);
            change.operation = KV_EDIT_UPDATE;
            change.fields = KvSpan{static_cast<std::uint64_t>(index), 1};
            change.expected_source_hash = utf8_view(source_hashes[index]);
        }
        batch = KvEditBatch{
            changes.data(), static_cast<std::uint64_t>(changes.size()),
            fields.data(), static_cast<std::uint64_t>(fields.size())};
    }
};

struct OtherTrackKeyBatch {
    std::string field_name = "trackKey";
    std::string field_value;
    std::vector<std::string> change_ids;
    std::vector<std::string> edit_ids;
    std::vector<std::string> source_hashes;
    std::vector<KvEditField> fields;
    std::vector<KvEditChange> changes;
    KvEditBatch batch{};

    OtherTrackKeyBatch(
        const std::vector<std::pair<std::string, std::string>>& targets,
        std::string value)
        : field_value(std::move(value)) {
        change_ids.reserve(targets.size());
        edit_ids.reserve(targets.size());
        source_hashes.reserve(targets.size());
        fields.reserve(targets.size());
        changes.resize(targets.size());
        for (size_t index = 0; index < targets.size(); ++index) {
            change_ids.push_back(
                "typed-contract-other-track-key-" + std::to_string(index));
            edit_ids.push_back(targets[index].first);
            source_hashes.push_back(targets[index].second);
        }
        for (size_t index = 0; index < targets.size(); ++index) {
            fields.push_back(KvEditField{
                utf8_view(field_name), utf8_view(field_value)});
            KvEditChange& change = changes[index];
            change.change_id = utf8_view(change_ids[index]);
            change.edit_id = utf8_view(edit_ids[index]);
            change.operation = KV_EDIT_UPDATE;
            change.fields = KvSpan{static_cast<std::uint64_t>(index), 1};
            change.expected_source_hash = utf8_view(source_hashes[index]);
        }
        batch = KvEditBatch{
            changes.data(), static_cast<std::uint64_t>(changes.size()),
            fields.data(), static_cast<std::uint64_t>(fields.size())};
    }
};

struct RepeaterInsertSpec {
    std::string change_id;
    std::string method;
    std::string repeater_key;
    std::string distance;
    std::string track_key;
    bool full_coordinates = false;
    std::vector<std::string> structure_keys;
    std::string pair_id;
    bool confirm_change_point = false;
};

struct RepeaterInsertBatch {
    std::string target_file;
    std::vector<std::string> change_ids;
    std::vector<std::string> field_names;
    std::vector<std::string> field_values;
    std::vector<KvEditField> fields;
    std::vector<KvEditChange> changes;
    KvEditBatch batch{};

    RepeaterInsertBatch(std::string target, const std::vector<RepeaterInsertSpec>& specs)
        : target_file(std::move(target)) {
        size_t field_count = 0;
        for (const RepeaterInsertSpec& spec : specs) {
            const bool is_end = spec.method == "End";
            field_count += 4 + (spec.pair_id.empty() ? 0 : 1);
            if (!is_end) {
                field_count += 5 + spec.structure_keys.size() +
                    (spec.full_coordinates ? 6 : 0);
            }
        }
        change_ids.reserve(specs.size());
        field_names.reserve(field_count);
        field_values.reserve(field_count);
        std::vector<KvSpan> field_ranges;
        field_ranges.reserve(specs.size());
        for (const RepeaterInsertSpec& spec : specs) {
            const std::uint64_t offset = static_cast<std::uint64_t>(field_names.size());
            auto append = [&](const char* name, const std::string& value) {
                field_names.emplace_back(name);
                field_values.push_back(value);
            };
            append("rowKind", "repeater");
            append("distance", spec.distance);
            append("method", spec.method);
            append("repeaterKey", spec.repeater_key);
            if (!spec.pair_id.empty()) append("repeaterPairId", spec.pair_id);
            if (spec.method != "End") {
                append("trackKey", spec.track_key);
                if (spec.full_coordinates) {
                    append("x", "1"); append("y", "2"); append("z", "3");
                    append("rx", "4"); append("ry", "5"); append("rz", "6");
                }
                append("tilt", "0");
                append("span", "25");
                append("interval", "5");
                append("structureKeys.count", std::to_string(spec.structure_keys.size()));
                for (size_t index = 0; index < spec.structure_keys.size(); ++index) {
                    append(("structureKeys." + std::to_string(index)).c_str(),
                           spec.structure_keys[index]);
                }
            }
            field_ranges.push_back({
                offset,
                static_cast<std::uint64_t>(field_names.size()) - offset,
            });
            change_ids.push_back(spec.change_id);
        }
        fields.reserve(field_names.size());
        for (size_t index = 0; index < field_names.size(); ++index) {
            fields.push_back({utf8_view(field_names[index]), utf8_view(field_values[index])});
        }
        changes.resize(specs.size());
        for (size_t index = 0; index < specs.size(); ++index) {
            KvEditChange& change = changes[index];
            change.change_id = utf8_view(change_ids[index]);
            change.edit_id = utf8_view(change_ids[index]);
            change.operation = KV_EDIT_INSERT;
            if (specs[index].confirm_change_point) {
                change.flags |= KV_EDIT_CHANGE_CONFIRM_REPEATER_CHANGE_POINT;
            }
            change.fields = field_ranges[index];
            change.target_file_path = utf8_view(target_file);
        }
        batch = {
            changes.empty() ? nullptr : changes.data(),
            static_cast<std::uint64_t>(changes.size()),
            fields.empty() ? nullptr : fields.data(),
            static_cast<std::uint64_t>(fields.size()),
        };
    }
};

struct OtherTrackInsertSpec {
    std::string change_id;
    std::string method;
    std::string distance;
    std::string track_key;
    std::vector<std::string> parameters;
};

struct OtherTrackInsertBatch {
    std::string target_file;
    std::vector<std::string> change_ids;
    std::vector<std::string> field_names;
    std::vector<std::string> field_values;
    std::vector<KvEditField> fields;
    std::vector<KvEditChange> changes;
    KvEditBatch batch{};

    OtherTrackInsertBatch(std::string target,
                          const std::vector<OtherTrackInsertSpec>& specs)
        : target_file(std::move(target)) {
        size_t field_count = 0;
        for (const OtherTrackInsertSpec& spec : specs) {
            field_count += 4 + spec.parameters.size();
        }
        change_ids.reserve(specs.size());
        field_names.reserve(field_count);
        field_values.reserve(field_count);
        std::vector<KvSpan> field_ranges;
        field_ranges.reserve(specs.size());
        for (const OtherTrackInsertSpec& spec : specs) {
            const std::uint64_t offset = static_cast<std::uint64_t>(field_names.size());
            auto append = [&](const std::string& name, const std::string& value) {
                field_names.push_back(name);
                field_values.push_back(value);
            };
            append("rowKind", "otherTrack.change");
            append("distance", spec.distance);
            append("method", spec.method);
            append("trackKey", spec.track_key);
            for (size_t index = 0; index < spec.parameters.size(); ++index) {
                append("parameter" + std::to_string(index), spec.parameters[index]);
            }
            field_ranges.push_back({
                offset,
                static_cast<std::uint64_t>(field_names.size()) - offset,
            });
            change_ids.push_back(spec.change_id);
        }
        fields.reserve(field_names.size());
        for (size_t index = 0; index < field_names.size(); ++index) {
            fields.push_back({utf8_view(field_names[index]), utf8_view(field_values[index])});
        }
        changes.resize(specs.size());
        for (size_t index = 0; index < specs.size(); ++index) {
            KvEditChange& change = changes[index];
            change.change_id = utf8_view(change_ids[index]);
            change.edit_id = utf8_view(change_ids[index]);
            change.operation = KV_EDIT_INSERT;
            change.fields = field_ranges[index];
            change.target_file_path = utf8_view(target_file);
        }
        batch = {
            changes.empty() ? nullptr : changes.data(),
            static_cast<std::uint64_t>(changes.size()),
            fields.empty() ? nullptr : fields.data(),
            static_cast<std::uint64_t>(fields.size()),
        };
    }
};

struct OwnTrackInsertSpec {
    std::string change_id;
    std::string row_kind;
    std::string method;
    std::string distance;
    std::string radius;
    std::string cant;
    std::string gradient;
};

struct OwnTrackInsertBatch {
    std::string target_file;
    std::vector<std::string> change_ids;
    std::vector<std::string> field_names;
    std::vector<std::string> field_values;
    std::vector<KvEditField> fields;
    std::vector<KvEditChange> changes;
    KvEditBatch batch{};

    OwnTrackInsertBatch(std::string target,
                        const std::vector<OwnTrackInsertSpec>& specs)
        : target_file(std::move(target)) {
        size_t field_count = 0;
        for (const OwnTrackInsertSpec& spec : specs) {
            field_count += 3 + (!spec.radius.empty()) + (!spec.cant.empty()) +
                (!spec.gradient.empty());
        }
        change_ids.reserve(specs.size());
        field_names.reserve(field_count);
        field_values.reserve(field_count);
        std::vector<KvSpan> field_ranges;
        field_ranges.reserve(specs.size());
        for (const OwnTrackInsertSpec& spec : specs) {
            const std::uint64_t offset = static_cast<std::uint64_t>(field_names.size());
            auto append = [&](const char* name, const std::string& value) {
                field_names.emplace_back(name);
                field_values.push_back(value);
            };
            append("rowKind", spec.row_kind);
            append("distance", spec.distance);
            append("method", spec.method);
            if (!spec.radius.empty()) append("radius", spec.radius);
            if (!spec.cant.empty()) append("cant", spec.cant);
            if (!spec.gradient.empty()) append("gradient", spec.gradient);
            field_ranges.push_back({
                offset,
                static_cast<std::uint64_t>(field_names.size()) - offset,
            });
            change_ids.push_back(spec.change_id);
        }
        fields.reserve(field_names.size());
        for (size_t index = 0; index < field_names.size(); ++index) {
            fields.push_back({utf8_view(field_names[index]), utf8_view(field_values[index])});
        }
        changes.resize(specs.size());
        for (size_t index = 0; index < specs.size(); ++index) {
            KvEditChange& change = changes[index];
            change.change_id = utf8_view(change_ids[index]);
            change.edit_id = utf8_view(change_ids[index]);
            change.operation = KV_EDIT_INSERT;
            change.fields = field_ranges[index];
            change.target_file_path = utf8_view(target_file);
        }
        batch = {
            changes.empty() ? nullptr : changes.data(),
            static_cast<std::uint64_t>(changes.size()),
            fields.empty() ? nullptr : fields.data(),
            static_cast<std::uint64_t>(fields.size()),
        };
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

const KvMapSound3DRow* find_map_sound3d(const KvMapSnapshot& snapshot,
                                        std::string_view edit_id) {
    for (std::uint64_t i = 0; i < snapshot.map_sound_3d_count; ++i) {
        const KvMapSound3DRow& row = snapshot.map_sounds_3d[i];
        if (arena_view(snapshot.string_data, snapshot.string_size,
                       row.metadata.edit_id) == edit_id) {
            return &row;
        }
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

const KvOtherTrackChangeRow* find_other_track_change(
    const KvMapSnapshot& snapshot, std::string_view edit_id) {
    for (std::uint64_t i = 0; i < snapshot.other_track_change_count; ++i) {
        const KvOtherTrackChangeRow& row = snapshot.other_track_changes[i];
        if (arena_view(snapshot.string_data, snapshot.string_size,
                       row.metadata.edit_id) == edit_id) {
            return &row;
        }
    }
    return nullptr;
}

bool string_track_key_equals(const KvMapSnapshot& snapshot,
                             const KvValue& key, std::string_view expected) {
    return key.kind == KV_VALUE_STRING &&
        arena_view(snapshot.string_data, snapshot.string_size,
                   key.string_value) == expected;
}

void check_coordinate_offset_method_conversions(const std::string& map_path) {
    const std::vector<std::pair<std::string, std::string>> zero_coordinates = {
        {"x", "0"}, {"y", "0"}, {"z", "0"},
        {"rx", "0"}, {"ry", "0"}, {"rz", "0"}
    };

    {
        MapHandle handle(kv_load_map_ex(
            map_path.c_str(), 25.0, KV_LOAD_PREVIEW | KV_LOAD_EDIT_METADATA));
        check(handle.value != nullptr, "Structure offset conversion load");
        if (!handle.value) return;

        KvMapSnapshot baseline{};
        check(kv_get_map_snapshot(handle.value, KV_MAP_SNAPSHOT_VERSION,
                                  &baseline, sizeof(baseline)) != 0 &&
                  baseline.structure_put_count > 0,
              "Structure offset conversion baseline");
        if (baseline.structure_put_count == 0) return;
        const KvStructurePutRow& source_row = baseline.structure_puts[0];
        const std::string edit_id = map_string(baseline, source_row.metadata.edit_id);
        const KvSourceFileRow& source =
            baseline.source_files[source_row.metadata.source_file_index];
        const std::string source_hash = map_string(baseline, source.source_hash);
        const std::string source_path = map_string(baseline, source.file_path);

        auto remove_fields = zero_coordinates;
        remove_fields.insert(remove_fields.begin(), {"method", "Put0"});
        MultiFieldUpdateBatch remove_offsets(
            "typed-contract-structure-remove-offsets", edit_id, source_hash,
            std::move(remove_fields));
        KvEditReportSnapshot remove_report{};
        check(kv_edit_apply_to_memory_typed(
                  handle.value, &remove_offsets.batch, &remove_report,
                  sizeof(remove_report)) != 0 && remove_report.ok &&
                  remove_report.full_reparse_ok,
              "Structure Put to Put0 conversion");
        KvMapSnapshot compact{};
        check(kv_get_map_snapshot(handle.value, KV_MAP_SNAPSHOT_VERSION,
                                  &compact, sizeof(compact)) != 0,
              "Structure Put0 conversion snapshot");
        const KvStructurePutRow* compact_row = find_structure(compact, edit_id);
        check(compact_row && map_string(compact, compact_row->method) == "Put0" &&
                  nearly_equal(compact_row->x, 0.0) &&
                  nearly_equal(compact_row->y, 0.0) &&
                  nearly_equal(compact_row->z, 0.0) &&
                  nearly_equal(compact_row->tilt, 0.0) &&
                  nearly_equal(compact_row->span, 25.0),
              "Structure Put0 conversion semantics");

        MultiFieldUpdateBatch add_offsets(
            "typed-contract-structure-add-offsets", edit_id, source_hash,
            {{"method", "Put"}});
        KvEditReportSnapshot add_report{};
        check(kv_edit_apply_to_memory_typed(
                  handle.value, &add_offsets.batch, &add_report,
                  sizeof(add_report)) != 0 && add_report.ok &&
                  add_report.full_reparse_ok,
              "Structure Put0 to Put conversion");
        KvMapSnapshot expanded{};
        check(kv_get_map_snapshot(handle.value, KV_MAP_SNAPSHOT_VERSION,
                                  &expanded, sizeof(expanded)) != 0,
              "Structure Put conversion snapshot");
        const KvStructurePutRow* expanded_row = find_structure(expanded, edit_id);
        check(expanded_row && map_string(expanded, expanded_row->method) == "Put" &&
                  nearly_equal(expanded_row->x, 0.0) &&
                  nearly_equal(expanded_row->y, 0.0) &&
                  nearly_equal(expanded_row->z, 0.0) &&
                  nearly_equal(expanded_row->tilt, 0.0) &&
                  nearly_equal(expanded_row->span, 25.0),
              "Structure Put conversion semantics");
        const char* source_text = kv_get_source_text(handle.value, source_path.c_str());
        check(source_text && std::string_view(source_text).find(
                  "Structure['pole'].Put('0',0,0,0,0,0,0,0,25);") !=
                  std::string_view::npos,
              "Structure conversion preserves compact-source arguments");
        kv_free_string(source_text);
    }

    {
        MapHandle handle(kv_load_map_ex(
            map_path.c_str(), 25.0, KV_LOAD_PREVIEW | KV_LOAD_EDIT_METADATA));
        check(handle.value != nullptr, "Repeater offset conversion load");
        if (!handle.value) return;

        KvMapSnapshot baseline{};
        check(kv_get_map_snapshot(handle.value, KV_MAP_SNAPSHOT_VERSION,
                                  &baseline, sizeof(baseline)) != 0 &&
                  baseline.repeater_count > 0,
              "Repeater offset conversion baseline");
        if (baseline.repeater_count == 0) return;
        const KvRepeaterRow& source_row = baseline.repeaters[0];
        const std::string edit_id = map_string(baseline, source_row.metadata.edit_id);
        const KvSourceFileRow& source =
            baseline.source_files[source_row.metadata.source_file_index];
        const std::string source_hash = map_string(baseline, source.source_hash);
        const std::string source_path = map_string(baseline, source.file_path);

        MultiFieldUpdateBatch add_offsets(
            "typed-contract-repeater-add-offsets", edit_id, source_hash,
            {{"method", "Begin"}, {"x", "4"}, {"y", "5"}, {"z", "6"},
             {"rx", "1"}, {"ry", "2"}, {"rz", "3"}});
        KvEditReportSnapshot add_report{};
        check(kv_edit_apply_to_memory_typed(
                  handle.value, &add_offsets.batch, &add_report,
                  sizeof(add_report)) != 0 && add_report.ok &&
                  add_report.full_reparse_ok,
              "Repeater Begin0 to Begin conversion");
        KvMapSnapshot expanded{};
        check(kv_get_map_snapshot(handle.value, KV_MAP_SNAPSHOT_VERSION,
                                  &expanded, sizeof(expanded)) != 0,
              "Repeater Begin conversion snapshot");
        const KvRepeaterRow* expanded_row = find_repeater(expanded, edit_id);
        check(expanded_row && map_string(expanded, expanded_row->method) == "Begin" &&
                  nearly_equal(expanded_row->x, 4.0) &&
                  nearly_equal(expanded_row->y, 5.0) &&
                  nearly_equal(expanded_row->z, 6.0) &&
                  nearly_equal(expanded_row->rx, 1.0) &&
                  nearly_equal(expanded_row->ry, 2.0) &&
                  nearly_equal(expanded_row->rz, 3.0) &&
                  nearly_equal(expanded_row->tilt, 1.0) &&
                  nearly_equal(expanded_row->span, 25.0) &&
                  nearly_equal(expanded_row->interval, 5.0) &&
                  expanded_row->structure_keys.count == 1,
              "Repeater Begin conversion semantics");

        auto remove_fields = zero_coordinates;
        remove_fields.insert(remove_fields.begin(), {"method", "Begin0"});
        MultiFieldUpdateBatch remove_offsets(
            "typed-contract-repeater-remove-offsets", edit_id, source_hash,
            std::move(remove_fields));
        KvEditReportSnapshot remove_report{};
        check(kv_edit_apply_to_memory_typed(
                  handle.value, &remove_offsets.batch, &remove_report,
                  sizeof(remove_report)) != 0 && remove_report.ok &&
                  remove_report.full_reparse_ok,
              "Repeater Begin to Begin0 conversion");
        KvMapSnapshot compact{};
        check(kv_get_map_snapshot(handle.value, KV_MAP_SNAPSHOT_VERSION,
                                  &compact, sizeof(compact)) != 0,
              "Repeater Begin0 conversion snapshot");
        const KvRepeaterRow* compact_row = find_repeater(compact, edit_id);
        check(compact_row && map_string(compact, compact_row->method) == "Begin0" &&
                  nearly_equal(compact_row->x, 0.0) &&
                  nearly_equal(compact_row->y, 0.0) &&
                  nearly_equal(compact_row->z, 0.0) &&
                  nearly_equal(compact_row->tilt, 1.0) &&
                  nearly_equal(compact_row->span, 25.0) &&
                  nearly_equal(compact_row->interval, 5.0) &&
                  compact_row->structure_keys.count == 1,
              "Repeater Begin0 conversion semantics");
        const char* source_text = kv_get_source_text(handle.value, source_path.c_str());
        check(source_text && std::string_view(source_text).find(
                  "Repeater['rail'].Begin0('1',1,25,5,'rail-a');") !=
                  std::string_view::npos,
              "Repeater conversion preserves compact-source arguments");
        kv_free_string(source_text);
    }
}

bool edit_report_has_error_prefix(const KvEditReportSnapshot& report,
                                  std::string_view prefix) {
    if (!report.blocking_errors) return false;
    for (std::uint64_t index = 0; index < report.blocking_error_count; ++index) {
        const std::string_view error = arena_view(
            report.string_data, report.string_size,
            report.blocking_errors[index]);
        if (error.size() >= prefix.size() &&
            error.substr(0, prefix.size()) == prefix) {
            return true;
        }
    }
    return false;
}

bool edit_report_has_error_containing(const KvEditReportSnapshot& report,
                                      std::string_view text) {
    if (!report.blocking_errors) return false;
    for (std::uint64_t index = 0; index < report.blocking_error_count; ++index) {
        const std::string_view error = arena_view(
            report.string_data, report.string_size,
            report.blocking_errors[index]);
        if (error.find(text) != std::string_view::npos) return true;
    }
    return false;
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

int geometry_projection_contract() {
    auto write_map = [](TempFixture& fixture, std::string_view body) {
        std::ofstream map(fixture.map_path, std::ios::binary | std::ios::trunc);
        map << "BveTs Map 2.02:utf-8\n" << body;
    };
    auto load_snapshot = [](MapHandle& handle, KvMapSnapshot& snapshot) {
        return handle.value && kv_get_map_snapshot(
            handle.value, KV_MAP_SNAPSHOT_VERSION, &snapshot, sizeof(snapshot));
    };

    TempFixture level_fixture;
    write_map(level_fixture, "0;\n100;\n");
    MapHandle level_handle(kv_load_map_ex(
        level_fixture.path_utf8().c_str(), 25.0, KV_LOAD_PREVIEW));
    KvMapSnapshot level{};
    check(load_snapshot(level_handle, level), "level projection map loads");
    const double* level_end = buffer_row_at_distance(level.own_track_geometry, 100.0);
    check(level_end != nullptr, "level projection endpoint exists");
    if (level_end) {
        check(level.own_track_geometry.cols == 11, "level geometry column contract");
        check(level_end[1] == 100.0 && level_end[2] == 0.0 &&
                  level_end[3] == 0.0 && level_end[4] == 0.0,
              "zero gradient preserves exact plan geometry");
    }

    TempFixture signed_fixture;
    write_map(signed_fixture,
              "0;\n"
              "Gradient.Begin(1000);\n"
              "100;\n"
              "Gradient.Begin(-1000);\n"
              "200;\n");
    MapHandle signed_handle(kv_load_map_ex(
        signed_fixture.path_utf8().c_str(), 25.0, KV_LOAD_PREVIEW));
    KvMapSnapshot signed_snapshot{};
    check(load_snapshot(signed_handle, signed_snapshot),
          "signed gradient projection map loads");
    const double* positive_end = buffer_row_at_distance(
        signed_snapshot.own_track_geometry, 100.0);
    const double* negative_end = buffer_row_at_distance(
        signed_snapshot.own_track_geometry, 200.0);
    check(positive_end && negative_end, "signed gradient endpoints exist");
    const double diagonal = 100.0 / std::sqrt(2.0);
    if (positive_end && negative_end) {
        check(nearly_equal(positive_end[1], diagonal),
              "positive gradient shortens plan projection");
        check(nearly_equal(positive_end[3], diagonal),
              "positive gradient raises elevation");
        check(nearly_equal(negative_end[1] - positive_end[1], diagonal),
              "negative gradient has the same plan shortening");
        check(nearly_equal(negative_end[3], 0.0),
              "negative gradient reverses the elevation component");
    }

    TempFixture curve_fixture;
    write_map(curve_fixture,
              "0;\n"
              "Gradient.Begin(1000);\n"
              "Curve.Begin(100);\n"
              "Track['offset'].Position(4,0);\n"
              "100;\n");
    MapHandle curve_handle(kv_load_map_ex(
        curve_fixture.path_utf8().c_str(), 25.0, KV_LOAD_PREVIEW));
    KvMapSnapshot curve{};
    check(load_snapshot(curve_handle, curve), "sloped curve projection map loads");
    const double* curve_end = buffer_row_at_distance(curve.own_track_geometry, 100.0);
    check(curve_end != nullptr, "sloped curve endpoint exists");
    const double curve_angle = diagonal / 100.0;
    if (curve_end) {
        check(nearly_equal(curve_end[1], 100.0 * std::sin(curve_angle)),
              "sloped circular curve uses projected arc length for x");
        check(nearly_equal(curve_end[2], 100.0 * (1.0 - std::cos(curve_angle))),
              "sloped circular curve uses projected arc length for y");
        check(nearly_equal(curve_end[3], diagonal),
              "sloped circular curve preserves elevation");
        check(nearly_equal(curve_end[4], curve_angle),
              "sloped circular curve direction follows projected arc length");
        check(nearly_equal(curve_end[5], 100.0),
              "sloped circular curve preserves radius");
    }
    check(curve.other_track_count == 1, "sloped curve other track exists");
    const double* other_end = curve.other_track_count == 1
        ? buffer_row_at_distance(curve.other_tracks[0].points, 100.0)
        : nullptr;
    check(other_end != nullptr, "sloped curve other-track endpoint exists");
    if (curve_end && other_end) {
        check(nearly_equal(other_end[1], curve_end[1] - 4.0 * std::sin(curve_angle)) &&
                  nearly_equal(other_end[2], curve_end[2] + 4.0 * std::cos(curve_angle)),
              "other track derives its plan position from projected own track");
        check(nearly_equal(other_end[3], curve_end[3]),
              "other track derives its elevation from projected own track");
    }

    check(curve_handle.value && kv_generate_scene_geometry(
              curve_handle.value, 25.0, 1.0, 7.0, 1.0, 0.01),
          "sloped curve scene geometry generates");
    KvSceneGeometrySnapshot curve_scene{};
    check(curve_handle.value && kv_get_scene_geometry_snapshot(
              curve_handle.value, KV_SCENE_GEOMETRY_SNAPSHOT_VERSION,
              &curve_scene, sizeof(curve_scene)),
          "sloped curve scene snapshot is available");
    const double* scene_curve_end = buffer_row_at_distance(curve_scene.own_track, 100.0);
    check(scene_curve_end != nullptr, "scene sloped-curve endpoint exists");
    if (curve_end && scene_curve_end) {
        check(nearly_equal(scene_curve_end[1], curve_end[1]) &&
                  nearly_equal(scene_curve_end[2], curve_end[2]) &&
                  nearly_equal(scene_curve_end[3], curve_end[3]) &&
                  nearly_equal(scene_curve_end[4], curve_end[4]),
              "regular and scene own-track projections agree");
    }
    check(curve_scene.other_track_count == 1,
          "scene sloped-curve other track exists");
    const double* scene_other_end = curve_scene.other_track_count == 1
        ? buffer_row_at_distance(curve_scene.other_tracks[0].points, 100.0)
        : nullptr;
    if (other_end && scene_other_end) {
        check(nearly_equal(scene_other_end[1], other_end[1]) &&
                  nearly_equal(scene_other_end[2], other_end[2]) &&
                  nearly_equal(scene_other_end[3], other_end[3]),
              "regular and scene other-track projections agree");
    } else {
        check(false, "scene sloped-curve other-track endpoint exists");
    }

    TempFixture transition_fixture;
    write_map(transition_fixture,
              "0;\n"
              "Gradient.BeginTransition();\n"
              "Curve.BeginTransition();\n"
              "100;\n"
              "Gradient.Begin(1000);\n"
              "Curve.Begin(100);\n");
    MapHandle transition_25_handle(kv_load_map_ex(
        transition_fixture.path_utf8().c_str(), 25.0, KV_LOAD_PREVIEW));
    MapHandle transition_7_handle(kv_load_map_ex(
        transition_fixture.path_utf8().c_str(), 7.0, KV_LOAD_PREVIEW));
    KvMapSnapshot transition_25{};
    KvMapSnapshot transition_7{};
    check(load_snapshot(transition_25_handle, transition_25) &&
              load_snapshot(transition_7_handle, transition_7),
          "combined transition maps load at both control-point intervals");
    const double* transition_25_end = buffer_row_at_distance(
        transition_25.own_track_geometry, 100.0);
    const double* transition_7_end = buffer_row_at_distance(
        transition_7.own_track_geometry, 100.0);
    check(transition_25_end && transition_7_end,
          "combined transition endpoints exist");
    const double half_angle = std::atan(1.0) * 0.5;
    const double transition_scale = std::sin(half_angle) / half_angle;
    const double transition_horizontal =
        100.0 * std::cos(half_angle) * transition_scale;
    const double transition_vertical =
        100.0 * std::sin(half_angle) * transition_scale;
    if (transition_25_end && transition_7_end) {
        check(nearly_equal(transition_25_end[3], transition_vertical),
              "vertical transition elevation matches analytic integral");
        check(nearly_equal(transition_25_end[4], transition_horizontal / 200.0),
              "lateral transition direction uses projected transition length");
        check(nearly_equal(transition_25_end[5], 100.0) &&
                  nearly_equal(transition_25_end[6], 1000.0),
              "combined transition reaches requested radius and gradient");
        bool interval_independent = true;
        for (size_t column = 1; column <= 6; ++column) {
            interval_independent = interval_independent && nearly_equal(
                transition_25_end[column], transition_7_end[column], 1e-8);
        }
        check(interval_independent,
              "combined projection endpoint is independent of control-point interval");
    }

    std::cout << "gradient projection contract "
              << (failures ? "FAIL" : "PASS") << '\n';
    return failures;
}

void repeater_linkage_boundary_contract() {
    using repeater_linkage::Event;
    using repeater_linkage::EventKind;
    const repeater_linkage::Linkage linkage = repeater_linkage::pair_linkage({
        Event{0, 0.0, 0.0, "rail", EventKind::Begin},
        // Deliberately put Begin before End in source order at the shared
        // distance. Half-open linkage must still close the earlier chain first.
        Event{1, 100.0, 1.0, "rail", EventKind::Begin},
        Event{2, 100.0, 2.0, "rail", EventKind::End},
    });
    check(linkage.chains.size() == 2 && linkage.segments.size() == 2,
          "Repeater same-distance End/Begin split chains");
    if (linkage.chains.size() == 2) {
        check(linkage.chains[0].end_source_index &&
                  *linkage.chains[0].end_source_index == 2 &&
                  linkage.chains[1].begin_source_indices.size() == 1 &&
                  linkage.chains[1].begin_source_indices[0] == 1,
              "Repeater same-distance chain ownership");
        check(!repeater_linkage::half_open_intervals_overlap(
                  linkage.chains[0], linkage.chains[1]),
              "Repeater touching half-open intervals do not overlap");
    }
}

void other_track_key_edit_contract() {
    OtherTrackRenameFixture fixture;
    MapHandle handle(kv_load_map_ex(
        fixture.path_utf8().c_str(), 25.0,
        KV_LOAD_PREVIEW | KV_LOAD_EDIT_METADATA));
    check(handle.value != nullptr, "other-track key rename fixture load");
    if (!handle.value) return;

    KvMapSnapshot baseline{};
    check(kv_get_map_snapshot(handle.value, KV_MAP_SNAPSHOT_VERSION,
                              &baseline, sizeof(baseline)) != 0,
          "other-track key rename baseline snapshot");
    std::vector<std::pair<std::string, std::string>> targets;
    for (std::uint64_t index = 0;
         index < baseline.other_track_change_count; ++index) {
        const KvOtherTrackChangeRow& row = baseline.other_track_changes[index];
        if (!string_track_key_equals(baseline, row.track_key, "target")) continue;
        check(row.metadata.source_file_index < baseline.source_file_count,
              "other-track key rename source index");
        if (row.metadata.source_file_index >= baseline.source_file_count) continue;
        targets.emplace_back(
            map_string(baseline, row.metadata.edit_id),
            map_string(
                baseline,
                baseline.source_files[row.metadata.source_file_index].source_hash));
    }
    check(targets.size() == 5,
          "other-track key rename fixture has all root and Include targets");
    if (targets.size() != 5) return;

    OtherTrackKeyBatch incomplete({targets.front()}, "renamed");
    KvEditReportSnapshot incomplete_report{};
    check(kv_edit_dry_run_typed(
              handle.value, &incomplete.batch, &incomplete_report,
              sizeof(incomplete_report)) != 0,
          "other-track incomplete key rename dry-run call");
    validate_report(incomplete_report);
    check(!incomplete_report.ok && edit_report_has_error_prefix(
              incomplete_report,
              "Other-track key rename must update every statement in the track"),
          "other-track incomplete key rename rejected");

    OtherTrackKeyBatch conflict(targets, "BLOCKER");
    KvEditReportSnapshot conflict_report{};
    check(kv_edit_dry_run_typed(
              handle.value, &conflict.batch, &conflict_report,
              sizeof(conflict_report)) != 0,
          "other-track conflicting key rename dry-run call");
    validate_report(conflict_report);
    check(!conflict_report.ok && edit_report_has_error_prefix(
              conflict_report, "Other-track key already exists in map"),
          "other-track distant case-insensitive duplicate rename rejected");

    OtherTrackKeyBatch string_numeric_distinct(targets, "'7'");
    KvEditReportSnapshot distinct_report{};
    check(kv_edit_dry_run_typed(
              handle.value, &string_numeric_distinct.batch, &distinct_report,
              sizeof(distinct_report)) != 0 && distinct_report.ok &&
              distinct_report.full_reparse_ok &&
              distinct_report.non_target_changed_count == 0,
          "other-track string and numeric keys remain distinct");

    OtherTrackKeyBatch rename(targets, "renamed");
    KvEditReportSnapshot dry_report{};
    check(kv_edit_dry_run_typed(
              handle.value, &rename.batch, &dry_report,
              sizeof(dry_report)) != 0 && dry_report.ok &&
              dry_report.full_reparse_ok && dry_report.update_count == 5 &&
              dry_report.changed_file_count == 2 &&
              dry_report.non_target_changed_count == 0,
          "other-track whole-key rename dry run");

    KvEditReportSnapshot applied_report{};
    check(kv_edit_apply_to_memory_typed(
              handle.value, &rename.batch, &applied_report,
              sizeof(applied_report)) != 0 && applied_report.ok &&
              applied_report.full_reparse_ok &&
              applied_report.non_target_changed_count == 0,
          "other-track whole-key apply-to-memory");
    KvMapSnapshot applied{};
    check(kv_get_map_snapshot(handle.value, KV_MAP_SNAPSHOT_VERSION,
                              &applied, sizeof(applied)) != 0,
          "other-track renamed snapshot");
    for (const auto& target : targets) {
        const KvOtherTrackChangeRow* row =
            find_other_track_change(applied, target.first);
        check(row && string_track_key_equals(applied, row->track_key, "renamed"),
              "other-track target has renamed key and stable edit id");
    }
    check(applied.structure_put_count == 1 &&
              string_track_key_equals(
                  applied, applied.structure_puts[0].track_key, "target") &&
              applied.signal_put_count == 1 &&
              string_track_key_equals(
                  applied, applied.signal_puts[0].track_key, "target") &&
              applied.repeater_count == 1 &&
              string_track_key_equals(
                  applied, applied.repeaters[0].track_key, "target"),
          "other-track dependent map elements retain the old track key");

    check(kv_edit_reset_memory(handle.value) != 0,
          "other-track whole-key reset");
    KvMapSnapshot reset{};
    check(kv_get_map_snapshot(handle.value, KV_MAP_SNAPSHOT_VERSION,
                              &reset, sizeof(reset)) != 0,
          "other-track whole-key reset snapshot");
    for (const auto& target : targets) {
        const KvOtherTrackChangeRow* row =
            find_other_track_change(reset, target.first);
        check(row && string_track_key_equals(reset, row->track_key, "target"),
              "other-track reset restores original key and edit id");
    }

    KvEditReportSnapshot reapplied_report{};
    check(kv_edit_apply_to_memory_typed(
              handle.value, &rename.batch, &reapplied_report,
              sizeof(reapplied_report)) != 0 && reapplied_report.ok,
          "other-track whole-key second apply before commit");
    KvEditReportSnapshot commit_report{};
    check(kv_edit_commit_typed(
              handle.value, &commit_report, sizeof(commit_report)) != 0 &&
              commit_report.ok && commit_report.full_reparse_ok &&
              commit_report.changed_file_count == 2,
          "other-track whole-key commit across root and Include");

    MapHandle reloaded(kv_load_map_ex(
        fixture.path_utf8().c_str(), 25.0,
        KV_LOAD_PREVIEW | KV_LOAD_EDIT_METADATA));
    check(reloaded.value != nullptr, "other-track whole-key reload");
    if (!reloaded.value) return;
    KvMapSnapshot reloaded_snapshot{};
    check(kv_get_map_snapshot(
              reloaded.value, KV_MAP_SNAPSHOT_VERSION,
              &reloaded_snapshot, sizeof(reloaded_snapshot)) != 0,
          "other-track whole-key reload snapshot");
    for (const auto& target : targets) {
        const KvOtherTrackChangeRow* row =
            find_other_track_change(reloaded_snapshot, target.first);
        check(row && string_track_key_equals(
                  reloaded_snapshot, row->track_key, "renamed"),
              "other-track committed key and stable edit id reload");
    }
    check(reloaded_snapshot.structure_put_count == 1 &&
              string_track_key_equals(
                  reloaded_snapshot,
                  reloaded_snapshot.structure_puts[0].track_key, "target") &&
              reloaded_snapshot.signal_put_count == 1 &&
              string_track_key_equals(
                  reloaded_snapshot,
                  reloaded_snapshot.signal_puts[0].track_key, "target") &&
              reloaded_snapshot.repeater_count == 1 &&
              string_track_key_equals(
                  reloaded_snapshot,
                  reloaded_snapshot.repeaters[0].track_key, "target"),
          "other-track dependency keys remain unchanged after commit/reload");

    std::ifstream root_source(fixture.map_path, std::ios::binary);
    const std::string root_text{
        std::istreambuf_iterator<char>(root_source),
        std::istreambuf_iterator<char>()};
    std::ifstream include_source(fixture.include_path, std::ios::binary);
    const std::string include_text{
        std::istreambuf_iterator<char>(include_source),
        std::istreambuf_iterator<char>()};
    check(root_text.find(
              "Track['renamed'].Position(3.8, 0); # preserve root layout") !=
              std::string::npos &&
          root_text.find(
              "Structure['pole'].Put('target',1,2,3,0,0,0,0,25);") !=
              std::string::npos &&
          include_text.find(
              "Track['renamed'].Y.Interpolate(0.1); # preserve include layout") !=
              std::string::npos &&
          include_text.find("Track['renamed'].Cant.SetGauge(1.067);") !=
              std::string::npos,
          "other-track key-only writeback preserves source text and dependencies");
}

void repeater_key_edit_contract() {
    RepeaterRenameFixture fixture;
    MapHandle handle(kv_load_map_ex(
        fixture.path_utf8().c_str(), 25.0,
        KV_LOAD_PREVIEW | KV_LOAD_EDIT_METADATA));
    check(handle.value != nullptr, "Repeater key rename fixture load");
    if (!handle.value) return;

    KvMapSnapshot baseline{};
    check(kv_get_map_snapshot(handle.value, KV_MAP_SNAPSHOT_VERSION,
                              &baseline, sizeof(baseline)) != 0,
          "Repeater key rename baseline snapshot");
    std::vector<std::pair<std::string, std::string>> targets;
    for (std::uint64_t index = 0; index < baseline.repeater_count; ++index) {
        const KvRepeaterRow& row = baseline.repeaters[index];
        if (map_string(baseline, row.repeater_key.string_value) != "target") continue;
        check(row.metadata.source_file_index < baseline.source_file_count,
              "Repeater key rename source index");
        if (row.metadata.source_file_index >= baseline.source_file_count) continue;
        targets.emplace_back(
            map_string(baseline, row.metadata.edit_id),
            map_string(
                baseline,
                baseline.source_files[row.metadata.source_file_index].source_hash));
    }
    check(targets.size() == 3,
          "Repeater key rename fixture has first/middle/End targets");
    if (targets.size() != 3) return;

    RepeaterKeyBatch incomplete({targets.front()}, "renamed");
    KvEditReportSnapshot incomplete_report{};
    check(kv_edit_dry_run_typed(
              handle.value, &incomplete.batch, &incomplete_report,
              sizeof(incomplete_report)) != 0,
          "Repeater incomplete key rename dry-run call");
    validate_report(incomplete_report);
    check(!incomplete_report.ok && edit_report_has_error_prefix(
              incomplete_report,
              "Repeater key rename must update every statement in the chain"),
          "Repeater incomplete key rename rejected");

    RepeaterKeyBatch conflict(targets, "BLOCKER");
    KvEditReportSnapshot conflict_report{};
    check(kv_edit_dry_run_typed(
              handle.value, &conflict.batch, &conflict_report,
              sizeof(conflict_report)) != 0,
          "Repeater overlapping key rename dry-run call");
    validate_report(conflict_report);
    check(!conflict_report.ok && edit_report_has_error_prefix(
              conflict_report,
              "Repeater key overlaps another Repeater interval"),
          "Repeater case-insensitive overlapping rename rejected");

    RepeaterKeyBatch disjoint(targets, "distant");
    KvEditReportSnapshot disjoint_report{};
    check(kv_edit_dry_run_typed(
              handle.value, &disjoint.batch, &disjoint_report,
              sizeof(disjoint_report)) != 0 && disjoint_report.ok &&
              disjoint_report.full_reparse_ok &&
              disjoint_report.non_target_changed_count == 0,
          "Repeater disjoint same-name key rename allowed");

    RepeaterKeyBatch touching(targets, "shared");
    KvEditReportSnapshot dry_report{};
    check(kv_edit_dry_run_typed(
              handle.value, &touching.batch, &dry_report,
              sizeof(dry_report)) != 0 && dry_report.ok &&
              dry_report.full_reparse_ok && dry_report.update_count == 3 &&
              dry_report.non_target_changed_count == 0,
          "Repeater touching-interval key rename dry run");

    KvEditReportSnapshot applied_report{};
    check(kv_edit_apply_to_memory_typed(
              handle.value, &touching.batch, &applied_report,
              sizeof(applied_report)) != 0 && applied_report.ok &&
              applied_report.full_reparse_ok &&
              applied_report.non_target_changed_count == 0,
          "Repeater chain key apply-to-memory");
    KvMapSnapshot applied{};
    check(kv_get_map_snapshot(handle.value, KV_MAP_SNAPSHOT_VERSION,
                              &applied, sizeof(applied)) != 0,
          "Repeater chain key applied snapshot");
    for (const auto& target : targets) {
        const KvRepeaterRow* row = find_repeater(applied, target.first);
        check(row && map_string(applied, row->repeater_key.string_value) == "shared",
              "Repeater chain member has renamed key");
    }

    check(kv_edit_reset_memory(handle.value) != 0,
          "Repeater chain key reset");
    KvMapSnapshot reset{};
    check(kv_get_map_snapshot(handle.value, KV_MAP_SNAPSHOT_VERSION,
                              &reset, sizeof(reset)) != 0,
          "Repeater chain key reset snapshot");
    for (const auto& target : targets) {
        const KvRepeaterRow* row = find_repeater(reset, target.first);
        check(row && map_string(reset, row->repeater_key.string_value) == "target",
              "Repeater chain key reset restored original");
    }

    KvEditReportSnapshot commit_apply_report{};
    check(kv_edit_apply_to_memory_typed(
              handle.value, &touching.batch, &commit_apply_report,
              sizeof(commit_apply_report)) != 0 && commit_apply_report.ok,
          "Repeater chain key apply before commit");
    KvEditReportSnapshot commit_report{};
    check(kv_edit_commit_typed(
              handle.value, &commit_report, sizeof(commit_report)) != 0 &&
              commit_report.ok && commit_report.full_reparse_ok &&
              commit_report.committed_file_count == 1,
          "Repeater chain key commit");

    MapHandle reloaded(kv_load_map_ex(
        fixture.path_utf8().c_str(), 25.0,
        KV_LOAD_PREVIEW | KV_LOAD_EDIT_METADATA));
    check(reloaded.value != nullptr, "Repeater chain key reload");
    if (!reloaded.value) return;
    KvMapSnapshot reloaded_snapshot{};
    check(kv_get_map_snapshot(
              reloaded.value, KV_MAP_SNAPSHOT_VERSION,
              &reloaded_snapshot, sizeof(reloaded_snapshot)) != 0,
          "Repeater chain key reloaded snapshot");
    int renamed_rows = 0;
    bool blocker_preserved = false;
    bool distant_preserved = false;
    for (std::uint64_t index = 0;
         index < reloaded_snapshot.repeater_count; ++index) {
        const KvRepeaterRow& row = reloaded_snapshot.repeaters[index];
        const std::string key = map_string(
            reloaded_snapshot, row.repeater_key.string_value);
        const std::string method = map_string(reloaded_snapshot, row.method);
        if (key == "shared" &&
            ((method == "Begin0" &&
              (nearly_equal(row.distance, 100.0) ||
               nearly_equal(row.distance, 150.0))) ||
             (method == "End" && nearly_equal(row.distance, 200.0)))) {
            ++renamed_rows;
        }
        blocker_preserved = blocker_preserved || key == "blocker";
        distant_preserved = distant_preserved || key == "distant";
    }
    check(renamed_rows == 3 && blocker_preserved && distant_preserved,
          "Repeater chain key save/reload and non-target preservation");
    std::ifstream committed_source(fixture.map_path, std::ios::binary);
    const std::string committed_text{
        std::istreambuf_iterator<char>(committed_source),
        std::istreambuf_iterator<char>()};
    check(committed_text.find(
              "Repeater['shared'].Begin0('0',0,0,25, 'target-a');") !=
              std::string::npos &&
          committed_text.find(
              "Repeater['shared'].Begin0('0',0,0,25, 'target-b');") !=
              std::string::npos &&
          committed_text.find("Repeater['shared'].End();") != std::string::npos,
          "Repeater key-only rename preserves non-key source text");
}

void repeater_insert_contract() {
    TempFixture fixture;
    MapHandle handle(kv_load_map_ex(
        fixture.path_utf8().c_str(), 25.0,
        KV_LOAD_PREVIEW | KV_LOAD_EDIT_METADATA));
    check(handle.value != nullptr, "Repeater insert fixture load");
    if (!handle.value) return;

    KvMapSnapshot baseline{};
    check(kv_get_map_snapshot(handle.value, KV_MAP_SNAPSHOT_VERSION,
                              &baseline, sizeof(baseline)) != 0 &&
              baseline.repeater_count >= 3,
          "Repeater insert baseline snapshot");
    if (baseline.repeater_count == 0 || !baseline.repeaters) return;
    const KvRepeaterRow& source_row = baseline.repeaters[0];
    check(source_row.metadata.source_file_index < baseline.source_file_count,
          "Repeater insert source index");
    if (source_row.metadata.source_file_index >= baseline.source_file_count) return;
    const std::string source_path = map_string(
        baseline, baseline.source_files[source_row.metadata.source_file_index].file_path);
    const std::uint64_t baseline_repeater_count = baseline.repeater_count;

    auto dry_run = [&](RepeaterInsertBatch& input, KvEditReportSnapshot& report,
                       const char* label) {
        check(kv_edit_dry_run_typed(handle.value, &input.batch, &report,
                                    sizeof(report)) != 0, label);
        validate_report(report);
    };
    RepeaterInsertBatch overlapping(source_path, {{
        "typed-contract-repeater-overlap", "Begin0", "  RAIL  ", "175", "'1'", false,
        {"pole"}, "",
    }});
    KvEditReportSnapshot overlap_report{};
    dry_run(overlapping, overlap_report, "Repeater overlapping insert dry-run call");
    check(!overlap_report.ok && edit_report_has_error_prefix(
              overlap_report, "Repeater key overlaps another Repeater interval"),
          "Repeater case-insensitive overlapping insert rejected");

    RepeaterInsertBatch confirmed_change_point(source_path, {{
        "typed-contract-repeater-change-point", "Begin0", "  RAIL  ", "175", "'1'", false,
        {"pole"}, "", true,
    }});
    KvEditReportSnapshot confirmed_report{};
    dry_run(confirmed_change_point, confirmed_report,
            "Confirmed Repeater change-point dry-run call");
    check(confirmed_report.ok && confirmed_report.full_reparse_ok &&
              confirmed_report.insert_count == 1,
          "Confirmed same-name Begin insert is accepted as a change point");
    KvEditReportSnapshot confirmed_apply_report{};
    check(kv_edit_apply_to_memory_typed(
              handle.value, &confirmed_change_point.batch, &confirmed_apply_report,
              sizeof(confirmed_apply_report)) != 0 && confirmed_apply_report.ok,
          "Confirmed Repeater change-point apply-to-memory");
    KvMapSnapshot change_point_snapshot{};
    check(kv_get_map_snapshot(handle.value, KV_MAP_SNAPSHOT_VERSION,
                              &change_point_snapshot,
                              sizeof(change_point_snapshot)) != 0 &&
              change_point_snapshot.repeater_count == baseline_repeater_count + 1 &&
              find_repeater(change_point_snapshot,
                            "typed-contract-repeater-change-point"),
          "Confirmed Repeater change point appears in the working copy");
    check(kv_edit_reset_memory(handle.value) != 0,
          "Confirmed Repeater change-point reset");

    RepeaterInsertBatch overlapping_pair(source_path, {
        {"typed-contract-repeater-overlap-pair-begin", "Begin0", "rail", "175",
         "'1'", false, {"pole"}, "overlap-pair"},
        {"typed-contract-repeater-overlap-pair-end", "End", "rail", "180",
         "", false, {}, "overlap-pair"},
    });
    KvEditReportSnapshot overlap_pair_report{};
    dry_run(overlapping_pair, overlap_pair_report,
            "Overlapping paired Repeater insert dry-run call");
    check(!overlap_pair_report.ok && edit_report_has_error_prefix(
              overlap_pair_report, "Repeater key overlaps another Repeater interval"),
          "Paired Repeater insert retains ordinary overlap protection");

    RepeaterInsertBatch reversed_pair(source_path, {
        {"typed-contract-repeater-reversed-begin", "Begin0", "reverse-pair", "180",
         "'1'", false, {"pole"}, "reverse-pair"},
        {"typed-contract-repeater-reversed-end", "End", "reverse-pair", "170",
         "", false, {}, "reverse-pair"},
    });
    KvEditReportSnapshot reversed_pair_report{};
    dry_run(reversed_pair, reversed_pair_report,
            "Reversed paired Repeater insert dry-run call");
    check(!reversed_pair_report.ok && edit_report_has_error_prefix(
              reversed_pair_report,
              "Repeater End distance cannot be less than Begin distance"),
          "Repeater End before paired Begin is rejected");

    RepeaterInsertBatch equal_pair(source_path, {
        {"typed-contract-repeater-equal-begin", "Begin0", "equal-pair", "175",
         "'1'", false, {"pole"}, "equal-pair"},
        {"typed-contract-repeater-equal-end", "End", "equal-pair", "175",
         "", false, {}, "equal-pair"},
    });
    KvEditReportSnapshot equal_pair_report{};
    dry_run(equal_pair, equal_pair_report,
            "Equal-distance paired Repeater insert dry-run call");
    check(equal_pair_report.ok && equal_pair_report.full_reparse_ok &&
              equal_pair_report.insert_count == 2,
          "Equal-distance Repeater Begin and End pair is allowed");

    RepeaterInsertBatch incomplete_pair(source_path, {{
        "typed-contract-repeater-incomplete-begin", "Begin0", "incomplete-pair", "175",
        "'1'", false, {"pole"}, "incomplete-pair",
    }});
    KvEditReportSnapshot incomplete_pair_report{};
    dry_run(incomplete_pair, incomplete_pair_report,
            "Incomplete paired Repeater insert dry-run call");
    check(!incomplete_pair_report.ok && edit_report_has_error_prefix(
              incomplete_pair_report,
              "Repeater paired insert must contain exactly one Begin and one End"),
          "Incomplete paired Repeater insert remains rejected");

    RepeaterInsertBatch extra_end(source_path, {{
        "typed-contract-repeater-extra-end", "End", "rail", "175", "", false, {}, "",
    }});
    KvEditReportSnapshot extra_end_report{};
    dry_run(extra_end, extra_end_report, "Extra Repeater End dry-run call");
    check(!extra_end_report.ok && edit_report_has_error_prefix(
              extra_end_report,
              "Repeater End falls inside a same-name interval that already has an explicit End"),
          "Repeater End inside an already closed interval is rejected");

    RepeaterInsertBatch end_at_old_end(source_path, {{
        "typed-contract-repeater-at-old-end", "End", "rail", "190", "", false, {}, "",
    }});
    KvEditReportSnapshot end_at_old_end_report{};
    dry_run(end_at_old_end, end_at_old_end_report,
            "Repeater End at existing End dry-run call");
    check(end_at_old_end_report.ok && end_at_old_end_report.full_reparse_ok &&
              end_at_old_end_report.insert_count == 1,
          "Orphan Repeater End at an existing End boundary is allowed");

    RepeaterInsertBatch isolated_end(source_path, {{
        "typed-contract-repeater-isolated-end", "End", "isolated-end", "200", "", false, {}, "",
    }});
    KvEditReportSnapshot isolated_end_report{};
    dry_run(isolated_end, isolated_end_report,
            "Isolated Repeater End dry-run call");
    check(isolated_end_report.ok && isolated_end_report.full_reparse_ok &&
              isolated_end_report.insert_count == 1,
          "Truly isolated Repeater End is allowed");

    RepeaterInsertBatch touching(source_path, {{
        "typed-contract-repeater-touching", "Begin0", "rail", "190", "'1'", false,
        {"pole"}, "",
    }});
    KvEditReportSnapshot touching_report{};
    dry_run(touching, touching_report, "Repeater touching insert dry-run call");
    check(touching_report.ok && touching_report.full_reparse_ok &&
              touching_report.insert_count == 1 &&
              touching_report.non_target_changed_count == 0,
          "Repeater touching insert allowed");

    RepeaterInsertBatch disjoint(source_path, {{
        "typed-contract-repeater-disjoint", "Begin0", "rail", "200", "'1'", false,
        {"pole"}, "",
    }});
    KvEditReportSnapshot disjoint_report{};
    dry_run(disjoint, disjoint_report, "Repeater disjoint insert dry-run call");
    check(disjoint_report.ok && disjoint_report.full_reparse_ok &&
              disjoint_report.insert_count == 1 &&
              disjoint_report.non_target_changed_count == 0,
          "Repeater disjoint insert allowed");

    RepeaterInsertBatch valid(source_path, {
        {"typed-contract-repeater-begin", "Begin", "insert-full", "160", "'1'", true,
         {"pole", "main1"}, "full-pair"},
        {"typed-contract-repeater-full-end", "End", "insert-full", "165", "", false,
         {}, "full-pair"},
        {"typed-contract-repeater-begin0", "Begin0", "insert-zero", "180", "'1'", false,
         {"pole"}, "zero-pair"},
        {"typed-contract-repeater-zero-end", "End", "insert-zero", "185", "", false,
         {}, "zero-pair"},
        {"typed-contract-repeater-orphan-end", "End", "insert-orphan", "200", "", false,
         {}, ""},
    });
    KvEditReportSnapshot dry_report{};
    dry_run(valid, dry_report, "Repeater insert dry-run call");
    check(dry_report.ok && dry_report.full_reparse_ok && dry_report.insert_count == 5 &&
              dry_report.non_target_changed_count == 0,
          "Repeater Begin/Begin0 pairs and orphan End inserts validate");

    KvEditReportSnapshot applied_report{};
    check(kv_edit_apply_to_memory_typed(
              handle.value, &valid.batch, &applied_report,
              sizeof(applied_report)) != 0 && applied_report.ok &&
              applied_report.full_reparse_ok && applied_report.insert_count == 5,
          "Repeater insert apply-to-memory");
    KvMapSnapshot applied{};
    check(kv_get_map_snapshot(handle.value, KV_MAP_SNAPSHOT_VERSION,
                              &applied, sizeof(applied)) != 0,
          "Repeater insert applied snapshot");
    const KvRepeaterRow* inserted_full = find_repeater(
        applied, "typed-contract-repeater-begin");
    const KvRepeaterRow* inserted_zero = find_repeater(
        applied, "typed-contract-repeater-begin0");
    const KvRepeaterRow* inserted_full_end = find_repeater(
        applied, "typed-contract-repeater-full-end");
    const KvRepeaterRow* inserted_zero_end = find_repeater(
        applied, "typed-contract-repeater-zero-end");
    const KvRepeaterRow* inserted_orphan_end = find_repeater(
        applied, "typed-contract-repeater-orphan-end");
    const auto structure_key_at = [](const KvMapSnapshot& snapshot,
                                     const KvRepeaterRow& row, std::uint64_t index) {
        if (!span_valid(row.structure_keys, snapshot.value_count) ||
            index >= row.structure_keys.count || !snapshot.values) {
            return std::string{};
        }
        const KvValue& value = snapshot.values[row.structure_keys.offset + index];
        return value.kind == KV_VALUE_STRING
            ? map_string(snapshot, value.string_value)
            : std::string{};
    };
    check(applied.repeater_count == baseline_repeater_count + 5 && inserted_full && inserted_zero &&
              inserted_full_end && inserted_zero_end && inserted_orphan_end &&
              map_string(applied, inserted_full->method) == "Begin" &&
              map_string(applied, inserted_full->repeater_key.string_value) == "insert-full" &&
              nearly_equal(inserted_full->x, 1.0) && nearly_equal(inserted_full->rz, 6.0) &&
              inserted_full->structure_keys.count == 2 &&
              structure_key_at(applied, *inserted_full, 0) == "pole" &&
              structure_key_at(applied, *inserted_full, 1) == "main1" &&
              map_string(applied, inserted_zero->method) == "Begin0" &&
              map_string(applied, inserted_zero->repeater_key.string_value) == "insert-zero" &&
              nearly_equal(inserted_zero->x, 0.0) && inserted_zero->structure_keys.count == 1 &&
              structure_key_at(applied, *inserted_zero, 0) == "pole" &&
              map_string(applied, inserted_full_end->method) == "End" &&
              nearly_equal(inserted_full_end->distance, 165.0) &&
              map_string(applied, inserted_zero_end->method) == "End" &&
              nearly_equal(inserted_zero_end->distance, 185.0) &&
              map_string(applied, inserted_orphan_end->method) == "End" &&
              nearly_equal(inserted_orphan_end->distance, 200.0),
          "Repeater inserts have stable ids, methods, fields, pairs, and orphan End data");

    check(kv_edit_reset_memory(handle.value) != 0, "Repeater insert reset call");
    KvMapSnapshot reset{};
    check(kv_get_map_snapshot(handle.value, KV_MAP_SNAPSHOT_VERSION,
                              &reset, sizeof(reset)) != 0 &&
              reset.repeater_count == baseline_repeater_count &&
              !find_repeater(reset, "typed-contract-repeater-begin") &&
              !find_repeater(reset, "typed-contract-repeater-begin0") &&
              !find_repeater(reset, "typed-contract-repeater-full-end") &&
              !find_repeater(reset, "typed-contract-repeater-zero-end") &&
              !find_repeater(reset, "typed-contract-repeater-orphan-end"),
          "Repeater insert reset restores disk snapshot");

    RepeaterInsertBatch modified_pair(source_path, {
        {"typed-contract-repeater-begin", "Begin", "insert-full-edited", "160", "'1'",
         true, {"pole"}, "full-pair"},
        {"typed-contract-repeater-full-end", "End", "insert-full-edited", "166", "",
         false, {}, "full-pair"},
    });
    KvEditReportSnapshot modified_pair_report{};
    check(kv_edit_apply_to_memory_typed(
              handle.value, &modified_pair.batch, &modified_pair_report,
              sizeof(modified_pair_report)) != 0 && modified_pair_report.ok &&
              modified_pair_report.full_reparse_ok &&
              modified_pair_report.insert_count == 2,
          "Modified paired Repeater insert ledger replays from disk baseline");
    KvMapSnapshot modified_pair_snapshot{};
    check(kv_get_map_snapshot(handle.value, KV_MAP_SNAPSHOT_VERSION,
                              &modified_pair_snapshot,
                              sizeof(modified_pair_snapshot)) != 0,
          "Modified paired Repeater snapshot");
    const KvRepeaterRow* modified_begin = find_repeater(
        modified_pair_snapshot, "typed-contract-repeater-begin");
    const KvRepeaterRow* modified_end = find_repeater(
        modified_pair_snapshot, "typed-contract-repeater-full-end");
    check(modified_pair_snapshot.repeater_count == baseline_repeater_count + 2 &&
              modified_begin && modified_end &&
              map_string(modified_pair_snapshot,
                         modified_begin->repeater_key.string_value) ==
                  "insert-full-edited" &&
              map_string(modified_pair_snapshot,
                         modified_end->repeater_key.string_value) ==
                  "insert-full-edited" &&
              nearly_equal(modified_end->distance, 166.0),
          "Modified paired Repeater replay preserves stable ids and both members");
    check(kv_edit_reset_memory(handle.value) != 0,
          "Modified paired Repeater replay reset");

    KvEditReportSnapshot reapply_report{};
    check(kv_edit_apply_to_memory_typed(
              handle.value, &valid.batch, &reapply_report,
              sizeof(reapply_report)) != 0 && reapply_report.ok,
          "Repeater insert reapply before commit");
    KvEditReportSnapshot commit_report{};
    check(kv_edit_commit_typed(handle.value, &commit_report, sizeof(commit_report)) != 0,
          "Repeater insert commit call");
    validate_report(commit_report);
    check(commit_report.ok && commit_report.full_reparse_ok,
          "Repeater insert commit validation");
    check(commit_report.changed_file_count == 1 && commit_report.changed_files &&
              arena_view(commit_report.string_data, commit_report.string_size,
                         commit_report.changed_files[0]) == source_path &&
              commit_report.committed_file_count >= 1,
          "Repeater insert commit writes only the target map source");

    MapHandle reloaded(kv_load_map_ex(
        fixture.path_utf8().c_str(), 25.0,
        KV_LOAD_PREVIEW | KV_LOAD_EDIT_METADATA));
    check(reloaded.value != nullptr, "Repeater insert reload");
    if (!reloaded.value) return;
    KvMapSnapshot reloaded_snapshot{};
    check(kv_get_map_snapshot(reloaded.value, KV_MAP_SNAPSHOT_VERSION,
                              &reloaded_snapshot, sizeof(reloaded_snapshot)) != 0,
          "Repeater insert reloaded snapshot");
    bool reloaded_full = false;
    bool reloaded_zero = false;
    bool reloaded_full_end = false;
    bool reloaded_zero_end = false;
    bool reloaded_orphan_end = false;
    for (std::uint64_t index = 0; index < reloaded_snapshot.repeater_count; ++index) {
        const KvRepeaterRow& row = reloaded_snapshot.repeaters[index];
        const std::string key = map_string(reloaded_snapshot, row.repeater_key.string_value);
        const std::string method = map_string(reloaded_snapshot, row.method);
        reloaded_full = reloaded_full ||
            (key == "insert-full" && method == "Begin" && nearly_equal(row.distance, 160.0) &&
             row.structure_keys.count == 2);
        reloaded_zero = reloaded_zero ||
            (key == "insert-zero" && method == "Begin0" && nearly_equal(row.distance, 180.0) &&
             row.structure_keys.count == 1);
        reloaded_full_end = reloaded_full_end ||
            (key == "insert-full" && method == "End" && nearly_equal(row.distance, 165.0));
        reloaded_zero_end = reloaded_zero_end ||
            (key == "insert-zero" && method == "End" && nearly_equal(row.distance, 185.0));
        reloaded_orphan_end = reloaded_orphan_end ||
            (key == "insert-orphan" && method == "End" && nearly_equal(row.distance, 200.0));
    }
    check(reloaded_snapshot.repeater_count == baseline_repeater_count + 5 &&
              reloaded_full && reloaded_zero && reloaded_full_end &&
              reloaded_zero_end && reloaded_orphan_end,
          "Repeater insert commit/reload retains paired forms and orphan End");
    std::ifstream committed_source(fixture.map_path, std::ios::binary);
    const std::string committed_text{
        std::istreambuf_iterator<char>(committed_source),
        std::istreambuf_iterator<char>()};
    check(committed_text.find(
              "Repeater['insert-full'].Begin('1',1,2,3,4,5,6,0,25,5,'pole','main1');") !=
              std::string::npos &&
          committed_text.find(
              "Repeater['insert-zero'].Begin0('1',0,25,5,'pole');") !=
              std::string::npos &&
          committed_text.find("Repeater['insert-full'].End();") != std::string::npos &&
          committed_text.find("Repeater['insert-zero'].End();") != std::string::npos &&
          committed_text.find("Repeater['insert-orphan'].End();") != std::string::npos,
          "Repeater insert source text uses official Begin, Begin0, and End syntax");
}

const KvCurveRow* find_curve(const KvMapSnapshot& snapshot,
                             const std::string& edit_id) {
    for (std::uint64_t index = 0; index < snapshot.curve_count; ++index) {
        if (map_string(snapshot, snapshot.curves[index].metadata.edit_id) == edit_id) {
            return &snapshot.curves[index];
        }
    }
    return nullptr;
}

const KvGradientRow* find_gradient(const KvMapSnapshot& snapshot,
                                   const std::string& edit_id) {
    for (std::uint64_t index = 0; index < snapshot.gradient_count; ++index) {
        if (map_string(snapshot, snapshot.gradients[index].metadata.edit_id) == edit_id) {
            return &snapshot.gradients[index];
        }
    }
    return nullptr;
}

void own_track_insert_contract() {
    TempFixture fixture;
    MapHandle handle(kv_load_map_ex(
        fixture.path_utf8().c_str(), 25.0,
        KV_LOAD_PREVIEW | KV_LOAD_EDIT_METADATA));
    check(handle.value != nullptr, "own-track insert fixture load");
    if (!handle.value) return;

    KvMapSnapshot baseline{};
    check(kv_get_map_snapshot(handle.value, KV_MAP_SNAPSHOT_VERSION,
                              &baseline, sizeof(baseline)) != 0 &&
              baseline.other_track_change_count != 0 &&
              baseline.other_track_changes != nullptr,
          "own-track insert baseline snapshot");
    if (baseline.other_track_change_count == 0 || !baseline.other_track_changes) return;
    const KvOtherTrackChangeRow& source_row = baseline.other_track_changes[0];
    check(source_row.metadata.source_file_index < baseline.source_file_count,
          "own-track insert source index");
    if (source_row.metadata.source_file_index >= baseline.source_file_count) return;
    const std::string source_path = map_string(
        baseline, baseline.source_files[source_row.metadata.source_file_index].file_path);
    const std::uint64_t baseline_curve_count = baseline.curve_count;
    const std::uint64_t baseline_gradient_count = baseline.gradient_count;

    const std::vector<OwnTrackInsertSpec> valid_specs = {
        {"typed-contract-own-curve-transition-a", "curve", "Curve.BeginTransition", "1", "", "", ""},
        {"typed-contract-own-curve-begin-cant-b", "curve", "Curve.Begin", "2", "300", "0.15", ""},
        {"typed-contract-own-curve-begin", "curve", "Curve.Begin", "3", "350", "", ""},
        {"typed-contract-own-curve-change", "curve", "Curve.Change", "4", "-400", "", ""},
        {"typed-contract-own-curve-end-transition-a", "curve", "Curve.BeginTransition", "5", "", "", ""},
        {"typed-contract-own-curve-end-b", "curve", "Curve.End", "6", "", "", ""},
        {"typed-contract-own-curve-end", "curve", "Curve.End", "7", "", "", ""},
        {"typed-contract-own-gradient-begin-transition-a", "gradient", "Gradient.BeginTransition", "8", "", "", ""},
        {"typed-contract-own-gradient-begin-b", "gradient", "Gradient.Begin", "9", "", "", "12.5"},
        {"typed-contract-own-gradient-end-transition-a", "gradient", "Gradient.BeginTransition", "10", "", "", ""},
        {"typed-contract-own-gradient-end-b", "gradient", "Gradient.End", "11", "", "", ""},
        {"typed-contract-own-gradient-begin", "gradient", "Gradient.Begin", "12", "", "", "-8"},
        {"typed-contract-own-gradient-end", "gradient", "Gradient.End", "13", "", "", ""},
    };
    const size_t curve_insert_count = static_cast<size_t>(std::count_if(
        valid_specs.begin(), valid_specs.end(), [](const OwnTrackInsertSpec& spec) {
            return spec.row_kind == "curve";
        }));
    const size_t gradient_insert_count = valid_specs.size() - curve_insert_count;
    OwnTrackInsertBatch valid(source_path, valid_specs);
    KvEditReportSnapshot dry_report{};
    check(kv_edit_dry_run_typed(handle.value, &valid.batch, &dry_report,
                                sizeof(dry_report)) != 0,
          "own-track insert dry-run call");
    validate_report(dry_report);
    check(dry_report.ok && dry_report.full_reparse_ok &&
              dry_report.insert_count == static_cast<int>(valid_specs.size()) &&
              dry_report.non_target_changed_count == 0,
          "all official own-track insert forms validate without non-target changes");

    auto rejected_insert = [&](const char* label, const char* expected_error,
                               const OwnTrackInsertSpec& spec) {
        OwnTrackInsertBatch invalid(source_path, {spec});
        KvEditReportSnapshot report{};
        check(kv_edit_dry_run_typed(handle.value, &invalid.batch, &report,
                                    sizeof(report)) != 0, label);
        validate_report(report);
        check(!report.ok && edit_report_has_error_containing(report, expected_error), label);
    };
    rejected_insert("two-argument Curve.Begin requires paired transition",
                    "requires a preceding Curve.BeginTransition", {
                        "typed-contract-own-unpaired-curve-begin", "curve", "Curve.Begin",
                        "10", "300", "0.15", "",
                    });
    rejected_insert("Curve.Change rejects a cant argument",
                    "Curve.Change insert accepts exactly one radius argument", {
                        "typed-contract-own-bad-curve-change", "curve", "Curve.Change",
                        "10", "300", "0.15", "",
                    });
    rejected_insert("Gradient.End rejects a gradient argument",
                    "Gradient.End insert does not accept a gradient argument", {
                        "typed-contract-own-bad-gradient-end", "gradient", "Gradient.End",
                        "10", "", "", "5",
                    });
    {
        OwnTrackInsertBatch reverse_order(source_path, {
            {"typed-contract-own-reverse-transition", "curve", "Curve.BeginTransition",
             "15", "", "", ""},
            {"typed-contract-own-reverse-primary", "curve", "Curve.Begin",
             "14", "300", "0.15", ""},
        });
        KvEditReportSnapshot report{};
        check(kv_edit_dry_run_typed(handle.value, &reverse_order.batch, &report,
                                    sizeof(report)) != 0,
              "reverse-order transition dry-run call");
        validate_report(report);
        check(!report.ok && edit_report_has_error_containing(report, "BeginTransition"),
              "reverse-order transition insert is rejected");
    }

    KvEditReportSnapshot applied_report{};
    check(kv_edit_apply_to_memory_typed(
              handle.value, &valid.batch, &applied_report,
              sizeof(applied_report)) != 0 && applied_report.ok &&
              applied_report.full_reparse_ok &&
              applied_report.insert_count == static_cast<int>(valid_specs.size()),
          "own-track insert apply-to-memory");
    KvMapSnapshot applied{};
    check(kv_get_map_snapshot(handle.value, KV_MAP_SNAPSHOT_VERSION,
                              &applied, sizeof(applied)) != 0,
          "own-track insert applied snapshot");
    bool rows_match = applied.curve_count == baseline_curve_count + curve_insert_count &&
        applied.gradient_count == baseline_gradient_count + gradient_insert_count;
    for (const OwnTrackInsertSpec& spec : valid_specs) {
        if (spec.row_kind == "curve") {
            const KvCurveRow* row = find_curve(applied, spec.change_id);
            const std::uint32_t argument_count = spec.method == "Curve.Begin"
                ? (spec.cant.empty() ? 1U : 2U)
                : spec.method == "Curve.Change" ? 1U : 0U;
            rows_match = rows_match && row &&
                map_string(applied, row->method) == spec.method &&
                nearly_equal(row->distance, std::stod(spec.distance)) &&
                row->argument_count == argument_count &&
                map_string(applied, row->file_path) == source_path;
        } else {
            const KvGradientRow* row = find_gradient(applied, spec.change_id);
            const std::uint32_t argument_count = spec.method == "Gradient.Begin" ? 1U : 0U;
            rows_match = rows_match && row &&
                map_string(applied, row->method) == spec.method &&
                nearly_equal(row->distance, std::stod(spec.distance)) &&
                row->argument_count == argument_count &&
                map_string(applied, row->file_path) == source_path;
        }
    }
    const KvCurveRow* curve_transition = find_curve(
        applied, "typed-contract-own-curve-transition-a");
    const KvCurveRow* curve_begin_cant = find_curve(
        applied, "typed-contract-own-curve-begin-cant-b");
    const KvCurveRow* curve_end_transition = find_curve(
        applied, "typed-contract-own-curve-end-transition-a");
    const KvCurveRow* curve_end = find_curve(
        applied, "typed-contract-own-curve-end-b");
    const KvGradientRow* gradient_transition = find_gradient(
        applied, "typed-contract-own-gradient-begin-transition-a");
    const KvGradientRow* gradient_begin = find_gradient(
        applied, "typed-contract-own-gradient-begin-b");
    const KvGradientRow* gradient_end_transition = find_gradient(
        applied, "typed-contract-own-gradient-end-transition-a");
    const KvGradientRow* gradient_end = find_gradient(
        applied, "typed-contract-own-gradient-end-b");
    check(rows_match && curve_transition && curve_begin_cant && curve_end_transition &&
              curve_end && gradient_transition && gradient_begin && gradient_end_transition &&
              gradient_end && curve_transition->order < curve_begin_cant->order &&
              !nearly_equal(curve_transition->distance, curve_begin_cant->distance) &&
              curve_end_transition->order < curve_end->order &&
              !nearly_equal(curve_end_transition->distance, curve_end->distance) &&
              gradient_transition->order < gradient_begin->order &&
              !nearly_equal(gradient_transition->distance, gradient_begin->distance) &&
              gradient_end_transition->order < gradient_end->order &&
              !nearly_equal(gradient_end_transition->distance, gradient_end->distance),
          "own-track inserts retain stable ids, distinct paired distances, and source order");

    check(kv_edit_reset_memory(handle.value) != 0,
          "own-track insert reset call");
    KvMapSnapshot reset{};
    check(kv_get_map_snapshot(handle.value, KV_MAP_SNAPSHOT_VERSION,
                              &reset, sizeof(reset)) != 0 &&
              reset.curve_count == baseline_curve_count &&
              reset.gradient_count == baseline_gradient_count &&
              !find_curve(reset, "typed-contract-own-curve-begin-cant-b") &&
              !find_gradient(reset, "typed-contract-own-gradient-begin-b"),
          "own-track insert reset restores the disk baseline");

    KvEditReportSnapshot reapply_report{};
    check(kv_edit_apply_to_memory_typed(
              handle.value, &valid.batch, &reapply_report,
              sizeof(reapply_report)) != 0 && reapply_report.ok &&
              reapply_report.full_reparse_ok,
          "own-track insert reapply before commit");
    KvEditReportSnapshot commit_report{};
    check(kv_edit_commit_typed(handle.value, &commit_report, sizeof(commit_report)) != 0,
          "own-track insert commit call");
    validate_report(commit_report);
    check(commit_report.ok && commit_report.full_reparse_ok &&
              commit_report.changed_file_count == 1 && commit_report.changed_files &&
              arena_view(commit_report.string_data, commit_report.string_size,
                         commit_report.changed_files[0]) == source_path,
          "own-track insert commit writes only the requested target source");
    bool committed_ids_match = commit_report.committed_rows != nullptr;
    for (const OwnTrackInsertSpec& spec : valid_specs) {
        const bool found = commit_report.committed_rows && std::any_of(
            commit_report.committed_rows,
            commit_report.committed_rows + commit_report.committed_row_count,
            [&](const KvEditCommittedRow& row) {
                return arena_view(commit_report.string_data, commit_report.string_size,
                                  row.row_kind) == spec.row_kind &&
                    arena_view(commit_report.string_data, commit_report.string_size,
                               row.edit_id) == spec.change_id;
            });
        committed_ids_match = committed_ids_match && found;
    }
    check(committed_ids_match,
          "own-track insert commit retains stable identities for every inserted row");

    MapHandle reloaded(kv_load_map_ex(
        fixture.path_utf8().c_str(), 25.0,
        KV_LOAD_PREVIEW | KV_LOAD_EDIT_METADATA));
    check(reloaded.value != nullptr, "own-track insert reload");
    if (!reloaded.value) return;
    KvMapSnapshot reloaded_snapshot{};
    check(kv_get_map_snapshot(reloaded.value, KV_MAP_SNAPSHOT_VERSION,
                              &reloaded_snapshot, sizeof(reloaded_snapshot)) != 0,
          "own-track insert reloaded snapshot");
    auto has_curve = [&](const OwnTrackInsertSpec& spec) {
        const std::uint32_t argument_count = spec.method == "Curve.Begin"
            ? (spec.cant.empty() ? 1U : 2U)
            : spec.method == "Curve.Change" ? 1U : 0U;
        return std::any_of(
            reloaded_snapshot.curves,
            reloaded_snapshot.curves + reloaded_snapshot.curve_count,
            [&](const KvCurveRow& row) {
                return map_string(reloaded_snapshot, row.method) == spec.method &&
                    nearly_equal(row.distance, std::stod(spec.distance)) &&
                    row.argument_count == argument_count &&
                    map_string(reloaded_snapshot, row.file_path) == source_path &&
                    !map_string(reloaded_snapshot, row.metadata.edit_id).empty();
            });
    };
    auto has_gradient = [&](const OwnTrackInsertSpec& spec) {
        const std::uint32_t argument_count = spec.method == "Gradient.Begin" ? 1U : 0U;
        return std::any_of(
            reloaded_snapshot.gradients,
            reloaded_snapshot.gradients + reloaded_snapshot.gradient_count,
            [&](const KvGradientRow& row) {
                return map_string(reloaded_snapshot, row.method) == spec.method &&
                    nearly_equal(row.distance, std::stod(spec.distance)) &&
                    row.argument_count == argument_count &&
                    map_string(reloaded_snapshot, row.file_path) == source_path &&
                    !map_string(reloaded_snapshot, row.metadata.edit_id).empty();
            });
    };
    bool reloaded_rows_match = reloaded_snapshot.curve_count ==
        baseline_curve_count + curve_insert_count &&
        reloaded_snapshot.gradient_count == baseline_gradient_count + gradient_insert_count;
    for (const OwnTrackInsertSpec& spec : valid_specs) {
        reloaded_rows_match = reloaded_rows_match &&
            (spec.row_kind == "curve" ? has_curve(spec) : has_gradient(spec));
    }
    check(reloaded_rows_match,
          "own-track insert save/reload retains source and typed snapshots");
    std::ifstream committed_source(fixture.map_path, std::ios::binary);
    const std::string committed_text{
        std::istreambuf_iterator<char>(committed_source),
        std::istreambuf_iterator<char>()};
    const std::vector<std::string> official_source_forms = {
        "Curve.BeginTransition();",
        "Curve.Begin(300,0.15);",
        "Curve.Begin(350);",
        "Curve.Change(-400);",
        "Curve.BeginTransition();",
        "Curve.End();",
        "Curve.End();",
        "Gradient.BeginTransition();",
        "Gradient.Begin(12.5);",
        "Gradient.BeginTransition();",
        "Gradient.End();",
        "Gradient.Begin(-8);",
        "Gradient.End();",
    };
    bool source_forms_ordered = true;
    size_t source_position = 0;
    for (const std::string& statement : official_source_forms) {
        const size_t found = committed_text.find(statement, source_position);
        if (found == std::string::npos) {
            source_forms_ordered = false;
            break;
        }
        source_position = found + statement.size();
    }
    check(source_forms_ordered &&
              committed_text.find("Curve.BeginCircular(") == std::string::npos,
          "own-track insert source uses only current official forms in paired order");
}

void other_track_insert_contract() {
    TempFixture fixture;
    MapHandle handle(kv_load_map_ex(
        fixture.path_utf8().c_str(), 25.0,
        KV_LOAD_PREVIEW | KV_LOAD_EDIT_METADATA));
    check(handle.value != nullptr, "other-track insert fixture load");
    if (!handle.value) return;

    KvMapSnapshot baseline{};
    check(kv_get_map_snapshot(handle.value, KV_MAP_SNAPSHOT_VERSION,
                              &baseline, sizeof(baseline)) != 0 &&
              baseline.other_track_change_count == 2 &&
              baseline.other_track_count == 1,
          "other-track insert baseline snapshot");
    if (baseline.other_track_change_count == 0 || !baseline.other_track_changes) return;
    const KvOtherTrackChangeRow& source_row = baseline.other_track_changes[0];
    check(source_row.metadata.source_file_index < baseline.source_file_count,
          "other-track insert source index");
    if (source_row.metadata.source_file_index >= baseline.source_file_count) return;
    const std::string source_path = map_string(
        baseline, baseline.source_files[source_row.metadata.source_file_index].file_path);
    const std::uint64_t baseline_change_count = baseline.other_track_change_count;
    const std::uint64_t baseline_track_count = baseline.other_track_count;

    const std::vector<OtherTrackInsertSpec> valid_specs = {
        {"typed-contract-other-position-xy", "Track.Position", "1", "'Insert-Other'", {"1", "2"}},
        {"typed-contract-other-position-xyh", "Track.Position", "2", "'insert-other'", {"3", "4", "5"}},
        {"typed-contract-other-position-xyhv", "Track.Position", "3", "'INSERT-OTHER'", {"6", "7", "8", "9"}},
        {"typed-contract-other-x-none", "Track.X.Interpolate", "4", "'insert-other'", {}},
        {"typed-contract-other-x-x", "Track.X.Interpolate", "5", "'insert-other'", {"10"}},
        {"typed-contract-other-x-radius", "Track.X.Interpolate", "6", "'insert-other'", {"11", "12"}},
        {"typed-contract-other-y-none", "Track.Y.Interpolate", "7", "'insert-other'", {}},
        {"typed-contract-other-y-y", "Track.Y.Interpolate", "8", "'insert-other'", {"13"}},
        {"typed-contract-other-y-radius", "Track.Y.Interpolate", "9", "'insert-other'", {"14", "15"}},
        {"typed-contract-other-cant-gauge", "Track.Cant.SetGauge", "10", "'insert-other'", {"1.435"}},
        {"typed-contract-other-cant-center", "Track.Cant.SetCenter", "11", "'insert-other'", {"0.1"}},
        {"typed-contract-other-cant-function", "Track.Cant.SetFunction", "12", "'insert-other'", {"1"}},
        {"typed-contract-other-cant-transition", "Track.Cant.BeginTransition", "13", "'insert-other'", {}},
        {"typed-contract-other-cant-begin", "Track.Cant.Begin", "14", "'insert-other'", {"0.2"}},
        {"typed-contract-other-cant-end", "Track.Cant.End", "15", "'insert-other'", {}},
        {"typed-contract-other-cant-interpolate-none", "Track.Cant.Interpolate", "16", "'insert-other'", {}},
        {"typed-contract-other-numeric", "Track.Cant.Interpolate", "17", "1", {"0.3"}},
    };
    OtherTrackInsertBatch valid(source_path, valid_specs);
    KvEditReportSnapshot dry_report{};
    check(kv_edit_dry_run_typed(handle.value, &valid.batch, &dry_report,
                                sizeof(dry_report)) != 0,
          "other-track insert dry-run call");
    validate_report(dry_report);
    check(dry_report.ok && dry_report.full_reparse_ok &&
              dry_report.insert_count == static_cast<int>(valid_specs.size()) &&
              dry_report.non_target_changed_count == 0,
          "all official other-track insert forms validate without non-target changes");

    KvEditReportSnapshot applied_report{};
    check(kv_edit_apply_to_memory_typed(
              handle.value, &valid.batch, &applied_report,
              sizeof(applied_report)) != 0 && applied_report.ok &&
              applied_report.full_reparse_ok &&
              applied_report.insert_count == static_cast<int>(valid_specs.size()),
          "other-track insert apply-to-memory");
    KvMapSnapshot applied{};
    check(kv_get_map_snapshot(handle.value, KV_MAP_SNAPSHOT_VERSION,
                              &applied, sizeof(applied)) != 0,
          "other-track insert applied snapshot");
    bool rows_match = applied.other_track_change_count ==
        baseline_change_count + valid_specs.size();
    for (const OtherTrackInsertSpec& spec : valid_specs) {
        const KvOtherTrackChangeRow* row = find_other_track_change(applied, spec.change_id);
        rows_match = rows_match && row &&
            map_string(applied, row->method) == spec.method &&
            nearly_equal(row->distance, std::stod(spec.distance)) &&
            row->parameters.count == spec.parameters.size() &&
            map_string(applied, row->file_path) == source_path;
        if (!row || row->parameters.count != spec.parameters.size() ||
            !span_valid(row->parameters, applied.value_count)) {
            rows_match = false;
            continue;
        }
        for (size_t index = 0; index < spec.parameters.size(); ++index) {
            const KvValue& parameter = applied.values[row->parameters.offset + index];
            rows_match = rows_match && parameter.kind == KV_VALUE_NUMBER &&
                nearly_equal(parameter.number_value, std::stod(spec.parameters[index]));
        }
    }
    check(rows_match,
          "other-track inserts preserve all official methods, parameters, target file, and stable ids");
    const KvOtherTrackChangeRow* string_key_row = find_other_track_change(
        applied, "typed-contract-other-position-xy");
    const KvOtherTrackChangeRow* numeric_key_row = find_other_track_change(
        applied, "typed-contract-other-numeric");
    size_t normalized_string_track_count = 0;
    size_t numeric_track_count = 0;
    size_t quoted_numeric_track_count = 0;
    for (std::uint64_t index = 0; index < applied.other_track_count; ++index) {
        const std::string key = map_string(applied, applied.other_tracks[index].key);
        normalized_string_track_count += key == "'insert-other'" ? 1u : 0u;
        numeric_track_count += key == "1" ? 1u : 0u;
        quoted_numeric_track_count += key == "'1'" ? 1u : 0u;
    }
    check(applied.other_track_count == baseline_track_count + 2 &&
              normalized_string_track_count == 1 && numeric_track_count == 1 &&
              quoted_numeric_track_count == 1 && string_key_row &&
              string_track_key_equals(applied, string_key_row->track_key, "Insert-Other") &&
              numeric_key_row && numeric_key_row->track_key.kind == KV_VALUE_NUMBER &&
              nearly_equal(numeric_key_row->track_key.number_value, 1.0),
          "new normalized key creates one track, case variants reuse it, and numeric/string keys remain distinct");

    check(kv_edit_reset_memory(handle.value) != 0,
          "other-track insert reset call");
    KvMapSnapshot reset{};
    check(kv_get_map_snapshot(handle.value, KV_MAP_SNAPSHOT_VERSION,
                              &reset, sizeof(reset)) != 0 &&
              reset.other_track_change_count == baseline_change_count &&
              reset.other_track_count == baseline_track_count &&
              !find_other_track_change(reset, "typed-contract-other-position-xy") &&
              !find_other_track_change(reset, "typed-contract-other-numeric"),
          "other-track insert reset restores the disk baseline");

    auto rejected_insert = [&](const char* label, const char* expected_error,
                               const OtherTrackInsertSpec& spec) {
        OtherTrackInsertBatch invalid(source_path, {spec});
        KvEditReportSnapshot report{};
        check(kv_edit_dry_run_typed(handle.value, &invalid.batch, &report,
                                    sizeof(report)) != 0, label);
        validate_report(report);
        check(!report.ok && edit_report_has_error_containing(report, expected_error), label);
    };
    rejected_insert("other-track legacy Gauge insert rejected",
                    "unsupported insert method Track.Gauge", {
                        "typed-contract-other-legacy-gauge", "Track.Gauge", "20", "'legacy'", {"1.435"},
                    });
    rejected_insert("other-track legacy Cant insert rejected",
                    "unsupported insert method Track.Cant", {
                        "typed-contract-other-legacy-cant", "Track.Cant", "20", "'legacy'", {"0.1"},
                    });
    rejected_insert("other-track wrong Position arity rejected",
                    "invalid parameter count for other-track insert method Track.Position", {
                        "typed-contract-other-bad-position", "Track.Position", "20", "'bad'", {"1"},
                    });
    rejected_insert("other-track SetFunction id rejected",
                    "Track.Cant.SetFunction insert requires id 0 or 1", {
                        "typed-contract-other-bad-function", "Track.Cant.SetFunction", "20", "'bad'", {"2"},
                    });

    KvEditReportSnapshot reapply_report{};
    check(kv_edit_apply_to_memory_typed(
              handle.value, &valid.batch, &reapply_report,
              sizeof(reapply_report)) != 0 && reapply_report.ok &&
              reapply_report.full_reparse_ok,
          "other-track insert reapply before commit");
    KvEditReportSnapshot commit_report{};
    check(kv_edit_commit_typed(handle.value, &commit_report, sizeof(commit_report)) != 0,
          "other-track insert commit call");
    validate_report(commit_report);
    check(commit_report.ok && commit_report.full_reparse_ok &&
              commit_report.changed_file_count == 1 && commit_report.changed_files &&
              arena_view(commit_report.string_data, commit_report.string_size,
                         commit_report.changed_files[0]) == source_path,
          "other-track insert commit writes only the requested target source");
    bool committed_ids_match = commit_report.committed_rows != nullptr;
    if (committed_ids_match) {
        for (const OtherTrackInsertSpec& spec : valid_specs) {
            const bool found = std::any_of(
                commit_report.committed_rows,
                commit_report.committed_rows + commit_report.committed_row_count,
                [&](const KvEditCommittedRow& row) {
                    return arena_view(commit_report.string_data, commit_report.string_size,
                                      row.row_kind) == "otherTrack.change" &&
                        arena_view(commit_report.string_data, commit_report.string_size,
                                   row.edit_id) == spec.change_id;
                });
            committed_ids_match = committed_ids_match && found;
        }
    }
    check(committed_ids_match,
          "other-track insert commit retains stable identities for every inserted row");

    MapHandle reloaded(kv_load_map_ex(
        fixture.path_utf8().c_str(), 25.0,
        KV_LOAD_PREVIEW | KV_LOAD_EDIT_METADATA));
    check(reloaded.value != nullptr, "other-track insert reload");
    if (!reloaded.value) return;
    KvMapSnapshot reloaded_snapshot{};
    check(kv_get_map_snapshot(reloaded.value, KV_MAP_SNAPSHOT_VERSION,
                              &reloaded_snapshot, sizeof(reloaded_snapshot)) != 0,
          "other-track insert reloaded snapshot");
    bool reloaded_rows_match = reloaded_snapshot.other_track_change_count ==
        baseline_change_count + valid_specs.size() &&
        reloaded_snapshot.other_track_count == baseline_track_count + 2;
    for (const OtherTrackInsertSpec& spec : valid_specs) {
        bool found = false;
        for (std::uint64_t index = 0;
             index < reloaded_snapshot.other_track_change_count; ++index) {
            const KvOtherTrackChangeRow& row = reloaded_snapshot.other_track_changes[index];
            if (map_string(reloaded_snapshot, row.method) != spec.method ||
                !nearly_equal(row.distance, std::stod(spec.distance)) ||
                row.parameters.count != spec.parameters.size() ||
                map_string(reloaded_snapshot, row.file_path) != source_path) {
                continue;
            }
            found = !map_string(reloaded_snapshot, row.metadata.edit_id).empty();
            for (size_t parameter = 0; found && parameter < spec.parameters.size(); ++parameter) {
                const KvValue& value = reloaded_snapshot.values[
                    row.parameters.offset + parameter];
                found = value.kind == KV_VALUE_NUMBER &&
                    nearly_equal(value.number_value, std::stod(spec.parameters[parameter]));
            }
            if (found) break;
        }
        reloaded_rows_match = reloaded_rows_match && found;
    }
    check(reloaded_rows_match,
          "other-track insert save/reload retains source, typed snapshots, and edit identities");
    std::ifstream committed_source(fixture.map_path, std::ios::binary);
    const std::string committed_text{
        std::istreambuf_iterator<char>(committed_source),
        std::istreambuf_iterator<char>()};
    const std::vector<std::string> official_source_forms = {
        "Track['Insert-Other'].Position(1,2);",
        "Track['insert-other'].Position(3,4,5);",
        "Track['INSERT-OTHER'].Position(6,7,8,9);",
        "Track['insert-other'].X.Interpolate();",
        "Track['insert-other'].X.Interpolate(10);",
        "Track['insert-other'].X.Interpolate(11,12);",
        "Track['insert-other'].Y.Interpolate();",
        "Track['insert-other'].Y.Interpolate(13);",
        "Track['insert-other'].Y.Interpolate(14,15);",
        "Track['insert-other'].Cant.SetGauge(1.435);",
        "Track['insert-other'].Cant.SetCenter(0.1);",
        "Track['insert-other'].Cant.SetFunction(1);",
        "Track['insert-other'].Cant.BeginTransition();",
        "Track['insert-other'].Cant.Begin(0.2);",
        "Track['insert-other'].Cant.End();",
        "Track['insert-other'].Cant.Interpolate();",
        "Track[1].Cant.Interpolate(0.3);",
    };
    bool official_source_forms_present = true;
    for (const std::string& statement : official_source_forms) {
        official_source_forms_present = official_source_forms_present &&
            committed_text.find(statement) != std::string::npos;
    }
    check(official_source_forms_present &&
              committed_text.find("Track['legacy'].Gauge(") == std::string::npos &&
              committed_text.find("Track['legacy'].Cant(") == std::string::npos,
          "other-track insert source uses only the official current forms");
}

void line_ending_edit_contract() {
    TempFixture fixture;
    const std::filesystem::path include_path = fixture.directory / "include.txt";
    auto convert_newlines = [](std::string_view text, std::string_view newline) {
        std::string output;
        for (char ch : text) {
            if (ch == '\n') output.append(newline);
            else output.push_back(ch);
        }
        return output;
    };
    auto write_fixture_file = [&](const std::filesystem::path& path,
                                  std::string_view text,
                                  std::string_view newline,
                                  bool utf8_bom) {
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        if (utf8_bom) output.write("\xEF\xBB\xBF", 3);
        const std::string bytes = convert_newlines(text, newline);
        output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    };
    auto read_bytes = [](const std::filesystem::path& path) {
        std::ifstream input(path, std::ios::binary);
        return std::vector<char>(std::istreambuf_iterator<char>(input),
                                 std::istreambuf_iterator<char>());
    };
    auto newline_is_preserved = [](const std::vector<char>& bytes,
                                   std::string_view newline) {
        size_t terminators = 0;
        for (size_t index = 0; index < bytes.size(); ++index) {
            if (bytes[index] == '\r') {
                if (newline == "\r\n") {
                    if (index + 1 >= bytes.size() || bytes[index + 1] != '\n') return false;
                    ++index;
                } else if (newline != "\r") {
                    return false;
                }
                ++terminators;
            } else if (bytes[index] == '\n') {
                if (newline != "\n") return false;
                ++terminators;
            }
        }
        return terminators != 0;
    };
    auto has_utf8_bom = [](const std::vector<char>& bytes) {
        return bytes.size() >= 3 &&
            static_cast<unsigned char>(bytes[0]) == 0xef &&
            static_cast<unsigned char>(bytes[1]) == 0xbb &&
            static_cast<unsigned char>(bytes[2]) == 0xbf;
    };

    struct LogicalBaseline {
        bool initialized = false;
        std::uint64_t statement_count = 0;
        std::string structure_edit_id;
        std::string signal_edit_id;
        int structure_line = 0;
        int structure_line_end = 0;
        int signal_line = 0;
        int signal_line_end = 0;
    };
    const std::array<std::pair<std::string_view, std::string_view>, 3> newline_cases{{
        {"lf", "\n"}, {"crlf", "\r\n"}, {"cr", "\r"},
    }};
    const std::array<std::pair<std::string_view, bool>, 2> encoding_cases{{
        {"utf-8", true}, {"cp932", false},
    }};

    for (const auto& [encoding, utf8_bom] : encoding_cases) {
        LogicalBaseline logical_baseline;
        for (const auto& [newline_name, newline] : newline_cases) {
            const std::string encoding_name(encoding);
            write_fixture_file(
                fixture.map_path,
                "BveTs Map 2.02:" + encoding_name + "\n"
                "# root comment\n0;\nStructure.Load('structures.csv');\n"
                "Signal.Load('signals.csv');\nInclude 'include.txt';\n",
                newline, utf8_bom);
            write_fixture_file(
                include_path,
                "BveTs Map 2.02:" + encoding_name + "\n"
                "// include comment\n100;\n"
                "Structure['pole'].Put('0',1,2,3,0,0,0,0,25);\n",
                newline, utf8_bom);
            write_fixture_file(
                fixture.directory / "structures.csv",
                "BveTs Structure List 1.00:" + encoding_name + "\n"
                "pole,pole.x\nglare,glare.x\n",
                newline, utf8_bom);
            write_fixture_file(
                fixture.directory / "signals.csv",
                "BveTs Signal Aspects List 2.00:" + encoding_name + "\n"
                "aspect,pole\n,glare\n",
                newline, utf8_bom);
            const std::vector<char> root_before = read_bytes(fixture.map_path);
            std::string structure_edit_id;
            std::string signal_edit_id;

            {
                MapHandle handle(kv_load_map_ex(
                    fixture.path_utf8().c_str(), 25.0,
                    KV_LOAD_PREVIEW | KV_LOAD_EDIT_METADATA));
                check(handle.value != nullptr, "line-ending fixture loads");
                if (!handle.value) continue;
                KvMapSnapshot snapshot{};
                check(kv_get_map_snapshot(handle.value, KV_MAP_SNAPSHOT_VERSION,
                                          &snapshot, sizeof(snapshot)) != 0,
                      "line-ending fixture snapshot");
                check(snapshot.structure_put_count == 1 && snapshot.signal_aspect_count == 1,
                      "line-ending fixture typed rows");
                if (snapshot.structure_put_count != 1 || snapshot.signal_aspect_count != 1) continue;
                const KvStructurePutRow& structure = snapshot.structure_puts[0];
                const KvSignalAspectRow& signal = snapshot.signal_aspects[0];
                structure_edit_id = map_string(snapshot, structure.metadata.edit_id);
                signal_edit_id = map_string(snapshot, signal.metadata.edit_id);
                const KvSourceFileRow& structure_source =
                    snapshot.source_files[structure.metadata.source_file_index];
                const KvSourceFileRow& signal_source =
                    snapshot.source_files[signal.metadata.source_file_index];
                check(map_string(snapshot, structure_source.newline) == newline_name &&
                          map_string(snapshot, signal_source.newline) == newline_name,
                      "line-ending source kind");
                check(map_string(snapshot, structure_source.encoding) == encoding &&
                          map_string(snapshot, signal_source.encoding) == encoding,
                      "line-ending source encoding");
                check(structure.metadata.line == 4 && structure.metadata.line_end == 4 &&
                          signal.metadata.line == 2 && signal.metadata.line_end == 3,
                      "line-ending logical line spans");
                if (!logical_baseline.initialized) {
                    logical_baseline = {true, snapshot.statement_count,
                                        structure_edit_id, signal_edit_id,
                                        structure.metadata.line, structure.metadata.line_end,
                                        signal.metadata.line, signal.metadata.line_end};
                } else {
                    check(snapshot.statement_count == logical_baseline.statement_count &&
                              structure_edit_id == logical_baseline.structure_edit_id &&
                              signal_edit_id == logical_baseline.signal_edit_id,
                          "line-ending semantics and edit identity parity");
                    check(structure.metadata.line == logical_baseline.structure_line &&
                              structure.metadata.line_end == logical_baseline.structure_line_end &&
                              signal.metadata.line == logical_baseline.signal_line &&
                              signal.metadata.line_end == logical_baseline.signal_line_end,
                          "line-ending source span parity");
                }

                UpdateBatch update(
                    structure_edit_id, map_string(snapshot, structure_source.source_hash),
                    "7", "x");
                KvEditReportSnapshot apply_report{};
                check(kv_edit_apply_to_memory_typed(
                          handle.value, &update.batch, &apply_report,
                          sizeof(apply_report)) != 0 && apply_report.ok,
                      "line-ending map apply-to-memory");
                KvEditReportSnapshot commit_report{};
                check(kv_edit_commit_typed(handle.value, &commit_report,
                                           sizeof(commit_report)) != 0 && commit_report.ok,
                      "line-ending map commit");
            }

            check(read_bytes(fixture.map_path) == root_before,
                  "line-ending root Include file remains unchanged");
            const std::vector<char> include_after = read_bytes(include_path);
            check(newline_is_preserved(include_after, newline) &&
                      has_utf8_bom(include_after) == utf8_bom,
                  "line-ending map encoding and terminators preserved");

            {
                MapHandle reloaded(kv_load_map_ex(
                    fixture.path_utf8().c_str(), 25.0,
                    KV_LOAD_PREVIEW | KV_LOAD_EDIT_METADATA));
                KvMapSnapshot snapshot{};
                check(reloaded.value && kv_get_map_snapshot(
                          reloaded.value, KV_MAP_SNAPSHOT_VERSION,
                          &snapshot, sizeof(snapshot)) != 0,
                      "line-ending map reload");
                if (!reloaded.value || snapshot.structure_put_count != 1 ||
                    snapshot.signal_aspect_count != 1) continue;
                check(map_string(snapshot, snapshot.structure_puts[0].metadata.edit_id) ==
                          structure_edit_id &&
                          std::abs(snapshot.structure_puts[0].x - 7.0) < 1e-9,
                      "line-ending map reload identity and value");
                const KvSignalAspectRow& signal = snapshot.signal_aspects[0];
                const KvSourceFileRow& signal_source =
                    snapshot.source_files[signal.metadata.source_file_index];
                UpdateBatch delete_glare(
                    signal_edit_id, map_string(snapshot, signal_source.source_hash),
                    "1", "deleteGlare");
                KvEditReportSnapshot apply_report{};
                check(kv_edit_apply_to_memory_typed(
                          reloaded.value, &delete_glare.batch, &apply_report,
                          sizeof(apply_report)) != 0 && apply_report.ok,
                      "line-ending Signal Aspects apply-to-memory");
                KvEditReportSnapshot commit_report{};
                check(kv_edit_commit_typed(reloaded.value, &commit_report,
                                           sizeof(commit_report)) != 0 && commit_report.ok,
                      "line-ending Signal Aspects commit");
            }

            const std::vector<char> signal_after =
                read_bytes(fixture.directory / "signals.csv");
            check(newline_is_preserved(signal_after, newline) &&
                      has_utf8_bom(signal_after) == utf8_bom,
                  "line-ending Signal Aspects encoding and terminators preserved");
            MapHandle final_reload(kv_load_map_ex(
                fixture.path_utf8().c_str(), 25.0,
                KV_LOAD_PREVIEW | KV_LOAD_EDIT_METADATA));
            KvMapSnapshot final_snapshot{};
            check(final_reload.value && kv_get_map_snapshot(
                      final_reload.value, KV_MAP_SNAPSHOT_VERSION,
                      &final_snapshot, sizeof(final_snapshot)) != 0 &&
                      final_snapshot.signal_aspect_count == 1,
                  "line-ending Signal Aspects reload");
            if (final_reload.value && final_snapshot.signal_aspect_count == 1) {
                const KvSignalAspectRow& signal = final_snapshot.signal_aspects[0];
                check(map_string(final_snapshot, signal.metadata.edit_id) == signal_edit_id &&
                          signal.structure_keys.count == signal.metadata.reserved,
                      "line-ending Signal Aspects row reconnection");
            }
        }
    }

    auto check_decoded_source_position = [&](TempFixture& position_fixture,
                                             const char* expected_statement,
                                             const char* label) {
        MapHandle handle(kv_load_map_ex(
            position_fixture.path_utf8().c_str(), 25.0,
            KV_LOAD_PREVIEW | KV_LOAD_EDIT_METADATA));
        check(handle.value != nullptr, label);
        if (!handle.value) return;
        KvMapSnapshot snapshot{};
        check(kv_get_map_snapshot(handle.value, KV_MAP_SNAPSHOT_VERSION,
                                  &snapshot, sizeof(snapshot)) != 0 &&
                  snapshot.structure_put_count == 1,
              "decoded source-position snapshot");
        if (snapshot.structure_put_count != 1) return;
        const KvStructurePutRow& row = snapshot.structure_puts[0];
        const KvSourceFileRow& source =
            snapshot.source_files[row.metadata.source_file_index];
        UpdateBatch update(
            map_string(snapshot, row.metadata.edit_id),
            map_string(snapshot, source.source_hash), "7", "x");
        KvEditReportSnapshot report{};
        check(kv_edit_apply_to_memory_typed(
                  handle.value, &update.batch, &report, sizeof(report)) != 0 &&
                  report.ok,
              "decoded source-position apply");
        const char* source_text = kv_get_source_text(
            handle.value, position_fixture.path_utf8().c_str());
        const bool statement_preserved = source_text &&
            std::string_view(source_text).find(expected_statement) !=
                std::string_view::npos;
        kv_free_string(source_text);
        check(statement_preserved,
              "decoded source-position preserves multibyte text and indentation");
        check(kv_edit_reset_memory(handle.value) != 0,
              "decoded source-position reset");
    };

    {
        TempFixture utf8_position_fixture;
        std::ofstream map(
            utf8_position_fixture.map_path, std::ios::binary | std::ios::trunc);
        map << "BveTs Map 2.02:utf-8\n"
            << "Structure.Load('structures.csv');\n"
            << "0;\n"
            << "    Structure['電柱'].Put('0',1,2,3,0,0,0,0,25);\n";
        map.close();
        std::ofstream structures(
            utf8_position_fixture.directory / "structures.csv",
            std::ios::binary | std::ios::trunc);
        structures << "BveTs Structure List 1.00:utf-8\n電柱,pole.x\n";
        structures.close();
        check_decoded_source_position(
            utf8_position_fixture,
            "    Structure['電柱'].Put('0',7,2,3,0,0,0,0,25);",
            "UTF-8 multibyte source-position fixture loads");
    }

    {
        TempFixture utf16_position_fixture;
        write_utf16_file(
            utf16_position_fixture.map_path,
            u"BveTs Map 2.02:utf-16le\n"
            u"Structure.Load('structures.csv');\n"
            u"0;\n"
            u"    Structure['pole'].Put('0',1,2,3,0,0,0,0,25);\n",
            true);
        std::ofstream structures(
            utf16_position_fixture.directory / "structures.csv",
            std::ios::binary | std::ios::trunc);
        structures << "BveTs Structure List 1.00:utf-8\npole,pole.x\n";
        structures.close();
        check_decoded_source_position(
            utf16_position_fixture,
            "    Structure['pole'].Put('0',7,2,3,0,0,0,0,25);",
            "UTF-16 decoded source-position fixture loads");
    }
}

void sound3d_edit_contract() {
    TempFixture fixture(false, true);
    MapHandle handle(kv_load_map_ex(
        fixture.path_utf8().c_str(), 25.0,
        KV_LOAD_PREVIEW | KV_LOAD_EDIT_METADATA));
    check(handle.value != nullptr, "Sound3D edit fixture load");
    if (!handle.value) return;

    KvMapSnapshot baseline{};
    check(kv_get_map_snapshot(handle.value, KV_MAP_SNAPSHOT_VERSION,
                              &baseline, sizeof(baseline)) != 0,
          "Sound3D baseline snapshot");
    check(baseline.map_sound_3d_count == 1,
          "Sound3D fixture row present");
    if (baseline.map_sound_3d_count != 1) return;
    const KvMapSound3DRow& row = baseline.map_sounds_3d[0];
    const std::string edit_id = map_string(baseline, row.metadata.edit_id);
    check(!edit_id.empty() && nearly_equal(row.distance, 225.0) &&
              nearly_equal(row.x, 1.25) && nearly_equal(row.y, 2.5),
          "Sound3D baseline values and stable edit id");
    check(row.metadata.source_file_index < baseline.source_file_count,
          "Sound3D source index");
    if (edit_id.empty() || row.metadata.source_file_index >= baseline.source_file_count) {
        return;
    }
    const KvSourceFileRow& source =
        baseline.source_files[row.metadata.source_file_index];
    const std::string source_hash = map_string(baseline, source.source_hash);
    const std::string source_path = map_string(baseline, source.file_path);
    const auto read_source = [](const std::filesystem::path& path) {
        std::ifstream input(path, std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(input),
                           std::istreambuf_iterator<char>());
    };
    const std::string disk_before_apply = read_source(fixture.map_path);

    MultiFieldUpdateBatch xy_update(
        "typed-contract-sound3d-xy", edit_id, source_hash,
        {{"x", "3.125"}, {"y", "-0.75"}});
    KvEditReportSnapshot xy_dry_run{};
    check(kv_edit_dry_run_typed(handle.value, &xy_update.batch,
                                &xy_dry_run, sizeof(xy_dry_run)) != 0,
          "Sound3D x/y dry-run call");
    validate_report(xy_dry_run);
    check(xy_dry_run.ok && xy_dry_run.full_reparse_ok &&
              xy_dry_run.update_count == 1 &&
              xy_dry_run.non_target_changed_count == 0,
          "Sound3D x/y dry-run validation");

    KvEditReportSnapshot xy_applied_report{};
    check(kv_edit_apply_to_memory_typed(
              handle.value, &xy_update.batch,
              &xy_applied_report, sizeof(xy_applied_report)) != 0,
          "Sound3D x/y apply-to-memory call");
    validate_report(xy_applied_report);
    check(xy_applied_report.ok && xy_applied_report.full_reparse_ok &&
              xy_applied_report.non_target_changed_count == 0,
          "Sound3D x/y apply-to-memory validation");
    KvMapSnapshot xy_applied{};
    check(kv_get_map_snapshot(handle.value, KV_MAP_SNAPSHOT_VERSION,
                              &xy_applied, sizeof(xy_applied)) != 0,
          "Sound3D x/y applied snapshot");
    const KvMapSound3DRow* xy_row = find_map_sound3d(xy_applied, edit_id);
    check(xy_row && nearly_equal(xy_row->distance, 225.0) &&
              nearly_equal(xy_row->x, 3.125) && nearly_equal(xy_row->y, -0.75) &&
              map_string(xy_applied, xy_row->metadata.edit_id) == edit_id,
          "Sound3D x/y in-memory values and stable edit id");
    const char* in_memory_source = kv_get_source_text(handle.value, source_path.c_str());
    check(in_memory_source && std::string_view(in_memory_source).find(
              "Sound3D['ambient'].Put(3.125,-0.75);") !=
              std::string_view::npos,
          "Sound3D x/y in-memory source keeps two arguments");
    if (in_memory_source) kv_free_string(in_memory_source);
    check(read_source(fixture.map_path) == disk_before_apply,
          "Sound3D Apply does not write the map file");
    MultiFieldUpdateBatch distance_update(
        "typed-contract-sound3d-distance", edit_id, source_hash,
        {{"distance", "240"}});
    KvEditReportSnapshot distance_dry_run{};
    check(kv_edit_dry_run_typed(handle.value, &distance_update.batch,
                                &distance_dry_run, sizeof(distance_dry_run)) != 0,
          "Sound3D distance dry-run call");
    validate_report(distance_dry_run);
    check(distance_dry_run.ok && distance_dry_run.full_reparse_ok &&
              distance_dry_run.update_count == 1 &&
              distance_dry_run.non_target_changed_count == 0,
          "Sound3D distance dry-run validation");

    KvEditReportSnapshot distance_applied_report{};
    check(kv_edit_apply_to_memory_typed(
              handle.value, &distance_update.batch,
              &distance_applied_report, sizeof(distance_applied_report)) != 0,
          "Sound3D distance apply-to-memory call");
    validate_report(distance_applied_report);
    check(distance_applied_report.ok && distance_applied_report.full_reparse_ok &&
              distance_applied_report.non_target_changed_count == 0,
          "Sound3D distance apply-to-memory validation");
    KvMapSnapshot distance_applied{};
    check(kv_get_map_snapshot(handle.value, KV_MAP_SNAPSHOT_VERSION,
                              &distance_applied, sizeof(distance_applied)) != 0,
          "Sound3D distance applied snapshot");
    const KvMapSound3DRow* distance_row =
        find_map_sound3d(distance_applied, edit_id);
    check(distance_row && nearly_equal(distance_row->distance, 240.0) &&
              nearly_equal(distance_row->x, 3.125) &&
              nearly_equal(distance_row->y, -0.75) &&
              map_string(distance_applied, distance_row->metadata.edit_id) == edit_id,
          "Sound3D distance in-memory value and stable edit id");

    KvEditReportSnapshot commit_report{};
    check(kv_edit_commit_typed(handle.value, &commit_report,
                               sizeof(commit_report)) != 0,
          "Sound3D Save call");
    validate_report(commit_report);
    check(commit_report.ok && commit_report.full_reparse_ok &&
              commit_report.non_target_changed_count == 0,
          "Sound3D Save validation");
    KvMapSnapshot committed{};
    check(kv_get_map_snapshot(handle.value, KV_MAP_SNAPSHOT_VERSION,
                              &committed, sizeof(committed)) != 0,
          "Sound3D Save snapshot");
    const KvMapSound3DRow* committed_row = find_map_sound3d(committed, edit_id);
    check(committed_row && nearly_equal(committed_row->distance, 240.0) &&
              nearly_equal(committed_row->x, 3.125) &&
              nearly_equal(committed_row->y, -0.75) &&
              map_string(committed, committed_row->metadata.edit_id) == edit_id,
          "Sound3D Save preserves the current-session edit id");
    const std::string committed_source = read_source(fixture.map_path);
    check(committed_source.find("240;\nSound3D['ambient'].Put(3.125,-0.75);") !=
              std::string::npos &&
              committed_source.find("Sound3D['ambient'].Put(3.125,-0.75,") ==
              std::string::npos,
          "Sound3D Save keeps Put(x, y) syntax");

    MapHandle reload(kv_load_map_ex(
        fixture.path_utf8().c_str(), 25.0,
        KV_LOAD_PREVIEW | KV_LOAD_EDIT_METADATA));
    check(reload.value != nullptr, "Sound3D reload after Save");
    if (!reload.value) return;
    KvMapSnapshot reloaded{};
    check(kv_get_map_snapshot(reload.value, KV_MAP_SNAPSHOT_VERSION,
                              &reloaded, sizeof(reloaded)) != 0,
          "Sound3D reload snapshot");
    const KvMapSound3DRow* reloaded_row = reloaded.map_sound_3d_count == 1
        ? &reloaded.map_sounds_3d[0] : nullptr;
    check(reloaded_row && nearly_equal(reloaded_row->distance, 240.0) &&
              nearly_equal(reloaded_row->x, 3.125) &&
              nearly_equal(reloaded_row->y, -0.75) &&
              !map_string(reloaded, reloaded_row->metadata.edit_id).empty(),
          "Sound3D Save/reload values");
}

void environment_argument_shape_edit_contract() {
    TempFixture fixture;
    {
        std::ofstream map(fixture.map_path, std::ios::binary | std::ios::trunc);
        map << "BveTs Map 2.02:utf-8\n"
            << "0;\nAdhesion.Change(0.35);\n"
            << "10;\nAdhesion.Change(0.35,0.1,0.2);\n"
            << "20;\nFog.Interpolate();\n"
            << "30;\nFog.Interpolate(0.01);\n"
            << "40;\nFog.Interpolate(0.02,0.1,0.2,0.3);\n"
            << "50;\nFog.Set(0.03,0.4,0.5,0.6);\n"
            << "60;\nCabIlluminance.Interpolate();\n"
            << "70;\nCabIlluminance.Interpolate(0.25);\n"
            << "80;\nCabIlluminance.Interpolate();\n"
            << "90;\nCabIlluminance.Set(0.5);\n"
            << "100;\nCabIlluminance.Interpolate();\n"
            << "110;\n120;\n130;\n140;\n150;\n160;\n170;\n180;\n190;\n200;\n210;\n";
    }

    MapHandle handle(kv_load_map_ex(
        fixture.path_utf8().c_str(), 25.0,
        KV_LOAD_PREVIEW | KV_LOAD_EDIT_METADATA));
    check(handle.value != nullptr, "environment argument-shape fixture load");
    if (!handle.value) return;
    KvMapSnapshot baseline{};
    check(kv_get_map_snapshot(handle.value, KV_MAP_SNAPSHOT_VERSION,
                              &baseline, sizeof(baseline)) != 0,
          "environment argument-shape fixture snapshot");
    check(baseline.adhesion_count == 2 && baseline.fog_count == 4 &&
              baseline.cab_illuminance_count == 5,
          "environment argument-shape fixture rows");
    if (baseline.adhesion_count != 2 || baseline.fog_count != 4 ||
        baseline.cab_illuminance_count != 5) return;

    struct TargetIdentity {
        std::string edit_id;
        std::string source_hash;
    };
    auto target_identity = [&](const auto& row) {
        TargetIdentity target;
        target.edit_id = map_string(baseline, row.metadata.edit_id);
        if (row.metadata.source_file_index < baseline.source_file_count) {
            target.source_hash = map_string(
                baseline,
                baseline.source_files[row.metadata.source_file_index].source_hash);
        }
        return target;
    };
    const TargetIdentity adhesion_one = target_identity(baseline.adhesions[0]);
    const TargetIdentity adhesion_three = target_identity(baseline.adhesions[1]);
    const TargetIdentity fog_empty = target_identity(baseline.fogs[0]);
    const TargetIdentity fog_density = target_identity(baseline.fogs[1]);
    const TargetIdentity fog_color = target_identity(baseline.fogs[2]);
    const TargetIdentity fog_set = target_identity(baseline.fogs[3]);
    const TargetIdentity cab_empty_first = target_identity(baseline.cab_illuminance[0]);
    const TargetIdentity cab_interpolate = target_identity(baseline.cab_illuminance[1]);
    const TargetIdentity cab_empty_after_interpolate = target_identity(baseline.cab_illuminance[2]);
    const TargetIdentity cab_set = target_identity(baseline.cab_illuminance[3]);
    const TargetIdentity cab_empty_after_set = target_identity(baseline.cab_illuminance[4]);
    check(baseline.cab_illuminance[0].value.kind == KV_VALUE_CONTINUE &&
              baseline.cab_illuminance[1].value.kind == KV_VALUE_NUMBER &&
              nearly_equal(baseline.cab_illuminance[1].value.number_value, 0.25) &&
              baseline.cab_illuminance[2].value.kind == KV_VALUE_CONTINUE &&
              baseline.cab_illuminance[3].value.kind == KV_VALUE_NUMBER &&
              nearly_equal(baseline.cab_illuminance[3].value.number_value, 0.5) &&
              baseline.cab_illuminance[4].value.kind == KV_VALUE_CONTINUE,
          "CabIlluminance no-argument rows retain CONTINUE values");
    check(!cab_empty_first.edit_id.empty() && !cab_interpolate.edit_id.empty() &&
              !cab_empty_after_interpolate.edit_id.empty() && !cab_set.edit_id.empty() &&
              !cab_empty_after_set.edit_id.empty(),
          "CabIlluminance rows retain edit identities");
    const std::string source_path = fixture.path_utf8();

    const auto source_occurrences = [](std::string_view source, std::string_view text) {
        size_t count = 0;
        for (size_t offset = 0; !text.empty();) {
            offset = source.find(text, offset);
            if (offset == std::string_view::npos) break;
            ++count;
            offset += text.size();
        }
        return count;
    };

    auto apply_and_check_source = [&](auto& update, std::string_view expected,
                                      const char* label, size_t expected_count = 0,
                                      std::string_view unexpected = {}) {
        KvEditReportSnapshot report{};
        const bool applied = kv_edit_apply_to_memory_typed(
            handle.value, &update.batch, &report, sizeof(report)) != 0 &&
            report.ok && report.full_reparse_ok &&
            report.non_target_changed_count == 0;
        const char* source = applied
            ? kv_get_source_text(handle.value, source_path.c_str()) : nullptr;
        const bool source_matches = source &&
            std::string_view(source).find(expected) != std::string_view::npos &&
            (expected_count == 0 ||
             source_occurrences(source, expected) == expected_count) &&
            (unexpected.empty() ||
             std::string_view(source).find(unexpected) == std::string_view::npos);
        check(applied && source_matches, label);
        kv_free_string(source);
        check(kv_edit_reset_memory(handle.value) != 0,
              "environment argument-shape update reset");
    };
    MultiFieldUpdateBatch adhesion_update(
        "environment-adhesion-three-update", adhesion_three.edit_id,
        adhesion_three.source_hash,
        {{"a", "0.4"}, {"b", "0.11"}, {"c", "0.22"}});
    apply_and_check_source(
        adhesion_update, "Adhesion.Change(0.4,0.11,0.22);",
        "Adhesion three-parameter update preserves shape");
    UpdateBatch fog_empty_update(
        fog_empty.edit_id, fog_empty.source_hash, "0.007", "density");
    apply_and_check_source(
        fog_empty_update, "Fog.Interpolate(0.007);",
        "Fog zero-to-one-parameter update uses official shape");
    UpdateBatch fog_color_update(
        fog_color.edit_id, fog_color.source_hash, "0.25", "green");
    apply_and_check_source(
        fog_color_update, "Fog.Interpolate(0.02,0.1,0.25,0.3);",
        "Fog four-parameter Interpolate update preserves shape");
    UpdateBatch fog_set_update(
        fog_set.edit_id, fog_set.source_hash, "0.45", "red");
    apply_and_check_source(
        fog_set_update, "Fog.Set(0.03,0.45,0.5,0.6);",
        "Fog legacy Set update preserves official legacy shape");
    UpdateBatch cab_empty_to_value(
        cab_empty_first.edit_id, cab_empty_first.source_hash, "0.33", "value");
    apply_and_check_source(
        cab_empty_to_value, "CabIlluminance.Interpolate(0.33);",
        "CabIlluminance no-argument update writes a value");
    UpdateBatch cab_interpolate_to_empty(
        cab_interpolate.edit_id, cab_interpolate.source_hash, "", "value");
    apply_and_check_source(
        cab_interpolate_to_empty, "CabIlluminance.Interpolate();",
        "CabIlluminance clearing writes the no-argument form", 4);
    UpdateBatch cab_set_to_empty(
        cab_set.edit_id, cab_set.source_hash, "", "value");
    apply_and_check_source(
        cab_set_to_empty, "CabIlluminance.Interpolate();",
        "CabIlluminance legacy Set clearing writes Interpolate", 4,
        "CabIlluminance.Set();");

    auto check_update_error = [&](auto& update, std::string_view expected_error,
                                  const char* label) {
        KvEditReportSnapshot report{};
        const bool called = kv_edit_dry_run_typed(
            handle.value, &update.batch, &report, sizeof(report)) != 0;
        check(called && !report.ok &&
                  edit_report_has_error_containing(report, expected_error),
              label);
    };
    UpdateBatch incomplete_adhesion(
        adhesion_one.edit_id, adhesion_one.source_hash, "0.1", "b");
    check_update_error(
        incomplete_adhesion,
        "Adhesion.Change requires either 1 or 3 parameters",
        "Adhesion partial optional update is rejected");
    UpdateBatch incomplete_fog_color(
        fog_density.edit_id, fog_density.source_hash, "0.2", "red");
    check_update_error(
        incomplete_fog_color,
        "Fog.Interpolate requires 0, 1, or 4 parameters",
        "Fog partial Interpolate color update is rejected");
    UpdateBatch incomplete_fog_set(
        fog_set.edit_id, fog_set.source_hash, "", "blue");
    check_update_error(
        incomplete_fog_set, "Fog.Set requires 4 parameters",
        "Fog incomplete Set update is rejected");

    auto check_insert = [&](std::string change_id,
                            std::vector<std::pair<std::string, std::string>> fields,
                            std::string_view expected,
                            const char* label, size_t expected_count = 0,
                            std::string_view unexpected = {}) {
        SimpleInsertBatch insert(source_path, std::move(change_id), std::move(fields));
        KvEditReportSnapshot report{};
        const bool applied = kv_edit_apply_to_memory_typed(
            handle.value, &insert.batch, &report, sizeof(report)) != 0 &&
            report.ok && report.full_reparse_ok &&
            report.non_target_changed_count == 0;
        const char* source = applied
            ? kv_get_source_text(handle.value, source_path.c_str()) : nullptr;
        const bool source_matches = source &&
            std::string_view(source).find(expected) != std::string_view::npos &&
            (expected_count == 0 ||
             source_occurrences(source, expected) == expected_count) &&
            (unexpected.empty() ||
             std::string_view(source).find(unexpected) == std::string_view::npos);
        check(applied && source_matches, label);
        kv_free_string(source);
        check(kv_edit_reset_memory(handle.value) != 0,
              "environment argument-shape insert reset");
    };
    check_insert(
        "insert-adhesion-one",
        {{"rowKind", "adhesion.change"}, {"distance", "100"}, {"a", "0.5"}},
        "Adhesion.Change(0.5);",
        "Adhesion one-parameter insert uses official shape");
    check_insert(
        "insert-adhesion-three",
        {{"rowKind", "adhesion.change"}, {"distance", "110"},
         {"a", "0.5"}, {"b", "0.1"}, {"c", "0.2"}},
        "Adhesion.Change(0.5,0.1,0.2);",
        "Adhesion three-parameter insert uses official shape");
    check_insert(
        "insert-fog-empty",
        {{"rowKind", "fog.change"}, {"distance", "120"},
         {"method", "Interpolate"}},
        "Fog.Interpolate();",
        "Fog zero-parameter insert uses official shape", 2);
    check_insert(
        "insert-fog-density",
        {{"rowKind", "fog.change"}, {"distance", "130"},
         {"method", "Interpolate"}, {"density", "0.04"}},
        "Fog.Interpolate(0.04);",
        "Fog one-parameter insert uses official shape");
    check_insert(
        "insert-fog-color",
        {{"rowKind", "fog.change"}, {"distance", "140"},
         {"method", "Interpolate"}, {"density", "0.05"},
         {"red", "0.1"}, {"green", "0.2"}, {"blue", "0.3"}},
        "Fog.Interpolate(0.05,0.1,0.2,0.3);",
        "Fog four-parameter insert uses official shape");
    check_insert(
        "insert-fog-set",
        {{"rowKind", "fog.change"}, {"distance", "150"},
         {"method", "Set"}, {"density", "0.06"},
         {"red", "0.4"}, {"green", "0.5"}, {"blue", "0.6"}},
        "Fog.Set(0.06,0.4,0.5,0.6);",
        "Fog legacy Set insert preserves supported official legacy shape");
    check_insert(
        "insert-cab-empty-interpolate",
        {{"rowKind", "cabIlluminance.change"}, {"distance", "200"},
         {"method", "Interpolate"}},
        "CabIlluminance.Interpolate();",
        "CabIlluminance blank Interpolate insert uses official shape", 4);
    check_insert(
        "insert-cab-empty-set",
        {{"rowKind", "cabIlluminance.change"}, {"distance", "210"},
         {"method", "Set"}, {"value", ""}},
        "CabIlluminance.Interpolate();",
        "CabIlluminance blank legacy Set insert uses Interpolate", 4,
        "CabIlluminance.Set();");

    auto check_insert_error = [&](
        std::string change_id,
        std::vector<std::pair<std::string, std::string>> fields,
        std::string_view expected_error,
        const char* label) {
        SimpleInsertBatch insert(source_path, std::move(change_id), std::move(fields));
        KvEditReportSnapshot report{};
        const bool called = kv_edit_dry_run_typed(
            handle.value, &insert.batch, &report, sizeof(report)) != 0;
        check(called && !report.ok &&
                  edit_report_has_error_containing(report, expected_error),
              label);
    };
    check_insert_error(
        "insert-incomplete-adhesion",
        {{"rowKind", "adhesion.change"}, {"distance", "170"},
         {"a", "0.5"}, {"b", "0.1"}},
        "Adhesion.Change requires either 1 or 3 parameters",
        "Adhesion partial optional insert is rejected");
    check_insert_error(
        "insert-incomplete-fog-interpolate",
        {{"rowKind", "fog.change"}, {"distance", "180"},
         {"method", "Interpolate"}, {"density", "0.05"}, {"red", "0.1"}},
        "Fog.Interpolate requires 0, 1, or 4 parameters",
        "Fog partial Interpolate insert is rejected");
    check_insert_error(
        "insert-incomplete-fog-set",
        {{"rowKind", "fog.change"}, {"distance", "190"},
         {"method", "Set"}, {"density", "0.05"},
         {"red", "0.1"}, {"green", "0.2"}},
        "Fog.Set requires 4 parameters",
        "Fog incomplete Set insert is rejected");
}

void include_insert_contract() {
    auto read_text = [](const std::filesystem::path& path) {
        std::ifstream input(path, std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(input),
                           std::istreambuf_iterator<char>());
    };
    auto run_case = [&](const char* label, const std::string& parent_text,
                        const std::string& expected_source, bool verify_commit = false) {
        const std::filesystem::path directory =
            std::filesystem::temp_directory_path() /
            ("komapedit-include-insert-contract-" + std::to_string(
                std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(directory);
        const auto check_case = [&](bool condition, const char* suffix) {
            const std::string message = std::string(label) + " " + suffix;
            check(condition, message.c_str());
        };
        const std::filesystem::path map_path = directory / "map.txt";
        {
            std::ofstream map(map_path, std::ios::binary | std::ios::trunc);
            map << parent_text;
        }
        for (const char* name : {"child.txt", "first.txt", "second.txt", "late.txt"}) {
            std::ofstream child(directory / name, std::ios::binary | std::ios::trunc);
            child << "BveTs Map 2.02:utf-8\r\n";
        }
        const std::string disk_before = read_text(map_path);
        {
            MapHandle handle(kv_load_map_ex(map_path.u8string().c_str(), 25.0,
                                            KV_LOAD_EDIT_METADATA));
            check_case(handle.value != nullptr, "load");
            if (handle.value) {
                SimpleInsertBatch insert(map_path.u8string(),
                                         std::string("include-insert-") + label,
                                         {{"rowKind", "include"},
                                          {"includePath", "child.txt"}});
                KvEditReportSnapshot report{};
                const bool applied = kv_edit_apply_to_memory_typed(
                    handle.value, &insert.batch, &report, sizeof(report)) != 0;
                check_case(applied && report.ok && report.insert_count == 1 &&
                               report.full_reparse_ok && report.non_target_changed_count == 0,
                           "applies and reparses");
                const char* source = applied
                    ? kv_get_source_text(handle.value, map_path.u8string().c_str()) : nullptr;
                check_case(source && std::string_view(source) == expected_source,
                           "preserves the expected CRLF source placement");
                kv_free_string(source);

                KvMapSnapshot snapshot{};
                check_case(kv_get_map_snapshot(handle.value, KV_MAP_SNAPSHOT_VERSION,
                                               &snapshot, sizeof(snapshot)) != 0,
                           "snapshot after include insert");
                bool inserted_statement_found = false;
                bool child_node_found = false;
                for (std::uint64_t i = 0; i < snapshot.statement_count; ++i) {
                    const KvStatementRow& statement = snapshot.statements[i];
                    if (map_string(snapshot, statement.statement_kind) != "Include" ||
                        statement.source.source_file_index >= snapshot.source_file_count ||
                        map_string(snapshot, snapshot.source_files[
                            statement.source.source_file_index].file_path) != map_path.u8string()) {
                        continue;
                    }
                    inserted_statement_found = inserted_statement_found ||
                        arena_view(snapshot.string_data, snapshot.string_size,
                                   statement.raw_arguments) == "'child.txt'";
                }
                for (std::uint64_t i = 0; i < snapshot.file_structure_count; ++i) {
                    const KvFileStructureRow& node = snapshot.file_structure[i];
                    child_node_found = child_node_found ||
                        map_string(snapshot, node.include_path) == "child.txt";
                }
                check_case(inserted_statement_found && child_node_found,
                           "reconnects the inserted physical Include subtree");
                check_case(kv_edit_reset_memory(handle.value) != 0, "reset");
            }
        }
        check_case(read_text(map_path) == disk_before,
                   "reset leaves the parent file uncommitted");
        if (verify_commit) {
            MapHandle committed(kv_load_map_ex(map_path.u8string().c_str(), 25.0,
                                               KV_LOAD_EDIT_METADATA));
            check_case(committed.value != nullptr, "commit load");
            if (committed.value) {
                SimpleInsertBatch insert(map_path.u8string(),
                                         std::string("include-insert-commit-") + label,
                                         {{"rowKind", "include"},
                                          {"includePath", "child.txt"}});
                KvEditReportSnapshot apply_report{};
                KvEditReportSnapshot commit_report{};
                const bool committed_ok =
                    kv_edit_apply_to_memory_typed(
                        committed.value, &insert.batch, &apply_report,
                        sizeof(apply_report)) != 0 && apply_report.ok &&
                    kv_edit_commit_typed(committed.value, &commit_report,
                                         sizeof(commit_report)) != 0 && commit_report.ok;
                check_case(committed_ok && read_text(map_path) == expected_source,
                           "commit preserves UTF-8 without BOM and CRLF");
            }
        }
        std::error_code cleanup;
        std::filesystem::remove_all(directory, cleanup);
    };

    const std::string header = "BveTs Map 2.02:utf-8\r\n";
    run_case("after-last-zero-include",
             header + "include 'first.txt';\r\n"
                      "include 'second.txt'; // last zero\r\n"
                      "0;\r\n",
             header + "include 'first.txt';\r\n"
                      "include 'second.txt'; // last zero\r\n"
                      "include 'child.txt';\r\n"
                      "0;\r\n",
             true);
    run_case("before-first-distance-without-include",
             header + "# preamble\r\n0;\r\n",
             header + "# preamble\r\ninclude 'child.txt';\r\n0;\r\n");
    run_case("before-first-distance-when-only-late-include",
             header + "0;\r\ninclude 'late.txt';\r\n",
             header + "include 'child.txt';\r\n0;\r\ninclude 'late.txt';\r\n");
    run_case("append-without-include-or-distance", header,
             header + "include 'child.txt';\r\n");
}

void empty_submap_insert_contract() {
    auto read_text = [](const std::filesystem::path& path) {
        std::ifstream input(path, std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(input),
                           std::istreambuf_iterator<char>());
    };
    const std::filesystem::path directory =
        std::filesystem::temp_directory_path() /
        ("komapedit-empty-submap-insert-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(directory);
    const std::filesystem::path map_path = directory / "map.txt";
    const std::filesystem::path submap_path = directory / "submap.txt";
    const std::string submap_before =
        "BveTs Map 2.02:utf-8\r\n"
        "// awaiting first element\r\n";
    {
        std::ofstream map(map_path, std::ios::binary | std::ios::trunc);
        map << "BveTs Map 2.02:utf-8\r\n"
            << "include 'submap.txt';\r\n"
            << "0;\r\n";
    }
    {
        std::ofstream submap(submap_path, std::ios::binary | std::ios::trunc);
        submap << submap_before;
    }
    const std::string disk_before = read_text(submap_path);
    const std::string target_file = submap_path.u8string();
    {
        MapHandle handle(kv_load_map_ex(map_path.u8string().c_str(), 25.0,
                                        KV_LOAD_EDIT_METADATA));
        check(handle.value != nullptr, "empty submap insert load");
        if (handle.value) {
            KvMapSnapshot baseline{};
            check(kv_get_map_snapshot(handle.value, KV_MAP_SNAPSHOT_VERSION,
                                      &baseline, sizeof(baseline)) != 0,
                  "empty submap insert baseline snapshot");
            std::uint64_t submap_source_index = baseline.source_file_count;
            for (std::uint64_t index = 0; index < baseline.source_file_count; ++index) {
                if (map_string(baseline, baseline.source_files[index].file_path) == target_file) {
                    submap_source_index = index;
                    break;
                }
            }
            bool submap_has_statement = false;
            for (std::uint64_t index = 0; index < baseline.statement_count; ++index) {
                submap_has_statement = submap_has_statement ||
                    baseline.statements[index].source.source_file_index == submap_source_index;
            }
            check(submap_source_index < baseline.source_file_count && !submap_has_statement,
                  "header-only submap remains in source metadata without map statements");

            const std::array<std::string, 3> ids = {
                "empty-submap-200", "empty-submap-100-a", "empty-submap-100-b"};
            const std::array<std::array<std::string, 4>, 3> values = {{
                {{"cabIlluminance.change", "200", "Set", "0.2"}},
                {{"cabIlluminance.change", "100", "Set", "0.1"}},
                {{"cabIlluminance.change", "100", "Set", "0.3"}},
            }};
            const std::array<std::string, 4> field_names = {
                "rowKind", "distance", "method", "value"};
            std::vector<KvEditField> fields;
            fields.reserve(ids.size() * field_names.size());
            std::array<KvEditChange, 3> changes{};
            for (size_t index = 0; index < changes.size(); ++index) {
                const size_t field_offset = fields.size();
                for (size_t field = 0; field < field_names.size(); ++field) {
                    fields.push_back({utf8_view(field_names[field]),
                                      utf8_view(values[index][field])});
                }
                changes[index].change_id = utf8_view(ids[index]);
                changes[index].edit_id = utf8_view(ids[index]);
                changes[index].operation = KV_EDIT_INSERT;
                changes[index].target_file_path = utf8_view(target_file);
                changes[index].fields = {
                    field_offset, static_cast<std::uint64_t>(field_names.size())};
            }
            const KvEditBatch batch = {
                changes.data(), static_cast<std::uint64_t>(changes.size()), fields.data(),
                static_cast<std::uint64_t>(fields.size())};
            KvEditReportSnapshot report{};
            const bool applied = kv_edit_apply_to_memory_typed(
                handle.value, &batch, &report, sizeof(report)) != 0;
            check(applied && report.ok && report.insert_count == 3 &&
                      report.created_distance_block_count == 2 && report.full_reparse_ok &&
                      report.non_target_changed_count == 0,
                  "empty submap first inserts group distances and fully reparse");
            const char* source = applied
                ? kv_get_source_text(handle.value, target_file.c_str()) : nullptr;
            const std::string expected_source = submap_before +
                "100;\r\n"
                "CabIlluminance.Set(0.1);\r\n"
                "CabIlluminance.Set(0.3);\r\n"
                "200;\r\n"
                "CabIlluminance.Set(0.2);\r\n";
            check(source && std::string_view(source) == expected_source,
                  "empty submap first inserts preserve comments and canonical distance ordering");
            kv_free_string(source);

            KvMapSnapshot applied_snapshot{};
            check(kv_get_map_snapshot(handle.value, KV_MAP_SNAPSHOT_VERSION,
                                      &applied_snapshot, sizeof(applied_snapshot)) != 0,
                  "empty submap insert applied snapshot");
            auto has_insert = [&](std::string_view id, double distance, double value) {
                return std::any_of(
                    applied_snapshot.cab_illuminance,
                    applied_snapshot.cab_illuminance + applied_snapshot.cab_illuminance_count,
                    [&](const KvCabIlluminanceRow& row) {
                        return map_string(applied_snapshot, row.metadata.edit_id) == id &&
                            map_string(applied_snapshot, row.file_path) == target_file &&
                            nearly_equal(row.distance, distance) &&
                            row.value.kind == KV_VALUE_NUMBER &&
                            nearly_equal(row.value.number_value, value);
                    });
            };
            check(applied_snapshot.cab_illuminance_count == 3 &&
                      has_insert(ids[0], 200.0, 0.2) &&
                      has_insert(ids[1], 100.0, 0.1) &&
                      has_insert(ids[2], 100.0, 0.3),
                  "empty submap inserts retain stable target identities");
            check(kv_edit_reset_memory(handle.value) != 0,
                  "empty submap insert reset");
        }
    }
    check(read_text(submap_path) == disk_before,
          "empty submap insert reset leaves disk unchanged");

    const std::filesystem::path nonempty_map_path = directory / "nonempty-map.txt";
    const std::filesystem::path nonempty_submap_path = directory / "nonempty-submap.txt";
    const std::string nonempty_submap_before =
        "BveTs Map 2.02:utf-8\r\n"
        "$already=1;\r\n";
    {
        std::ofstream map(nonempty_map_path, std::ios::binary | std::ios::trunc);
        map << "BveTs Map 2.02:utf-8\r\n"
            << "include 'nonempty-submap.txt';\r\n"
            << "0;\r\n";
    }
    {
        std::ofstream submap(nonempty_submap_path, std::ios::binary | std::ios::trunc);
        submap << nonempty_submap_before;
    }
    const std::string nonempty_disk_before = read_text(nonempty_submap_path);
    {
        MapHandle handle(kv_load_map_ex(nonempty_map_path.u8string().c_str(), 25.0,
                                        KV_LOAD_EDIT_METADATA));
        check(handle.value != nullptr, "nonempty submap insert load");
        if (handle.value) {
            SimpleInsertBatch insert(
                nonempty_submap_path.u8string(), "nonempty-submap-no-anchor",
                 {{"rowKind", "cabIlluminance.change"}, {"distance", "100"},
                 {"method", "Set"}, {"value", "0.1"}});
            KvEditReportSnapshot dry_report{};
            const bool dry_run = kv_edit_dry_run_typed(
                handle.value, &insert.batch, &dry_report, sizeof(dry_report)) != 0;
            check(dry_run && dry_report.ok && dry_report.insert_count == 1 &&
                      dry_report.created_distance_block_count == 1 &&
                      dry_report.full_reparse_ok &&
                      dry_report.non_target_changed_count == 0,
                  "nonempty submap without a numeric distance dry-runs through a tail block");

            KvEditReportSnapshot apply_report{};
            const bool applied = kv_edit_apply_to_memory_typed(
                handle.value, &insert.batch, &apply_report, sizeof(apply_report)) != 0;
            check(applied && apply_report.ok && apply_report.insert_count == 1 &&
                      apply_report.created_distance_block_count == 1 &&
                      apply_report.full_reparse_ok &&
                      apply_report.non_target_changed_count == 0,
                  "nonempty submap without a numeric distance applies through a tail block");
            const char* source = applied
                ? kv_get_source_text(handle.value, nonempty_submap_path.u8string().c_str())
                : nullptr;
            const std::string expected_source = nonempty_submap_before +
                "100;\r\n"
                "CabIlluminance.Set(0.1);\r\n";
            check(source && std::string_view(source) == expected_source,
                  "nonempty submap tail insert preserves prior statements and CRLF");
            kv_free_string(source);

            KvMapSnapshot applied_snapshot{};
            check(kv_get_map_snapshot(handle.value, KV_MAP_SNAPSHOT_VERSION,
                                      &applied_snapshot, sizeof(applied_snapshot)) != 0,
                  "nonempty submap tail insert applied snapshot");
            const bool stable_insert = applied_snapshot.cab_illuminance && std::any_of(
                applied_snapshot.cab_illuminance,
                applied_snapshot.cab_illuminance + applied_snapshot.cab_illuminance_count,
                [&](const KvCabIlluminanceRow& row) {
                    return map_string(applied_snapshot, row.metadata.edit_id) ==
                               "nonempty-submap-no-anchor" &&
                        map_string(applied_snapshot, row.file_path) ==
                            nonempty_submap_path.u8string() &&
                        nearly_equal(row.distance, 100.0) &&
                        row.value.kind == KV_VALUE_NUMBER &&
                        nearly_equal(row.value.number_value, 0.1);
                });
            check(stable_insert,
                  "nonempty submap tail insert retains a stable target identity");
            check(kv_edit_reset_memory(handle.value) != 0,
                  "nonempty submap tail insert reset");
        }
    }
    check(read_text(nonempty_submap_path) == nonempty_disk_before,
          "nonempty submap tail insert reset leaves disk unchanged");

    const std::filesystem::path single_anchor_map_path = directory / "single-anchor-map.txt";
    const std::string single_anchor_before =
        "BveTs Map 2.02:utf-8\r\n"
        "$already=1;\r\n"
        "0;\r\n";
    {
        std::ofstream map(single_anchor_map_path, std::ios::binary | std::ios::trunc);
        map << single_anchor_before;
    }
    {
        MapHandle handle(kv_load_map_ex(single_anchor_map_path.u8string().c_str(), 25.0,
                                        KV_LOAD_EDIT_METADATA));
        check(handle.value != nullptr, "single-anchor map insert load");
        if (handle.value) {
            SimpleInsertBatch insert(
                single_anchor_map_path.u8string(), "single-anchor-map-tail-insert",
                {{"rowKind", "cabIlluminance.change"}, {"distance", "100"},
                 {"method", "Set"}, {"value", "0.1"}});
            KvEditReportSnapshot dry_report{};
            const bool dry_run = kv_edit_dry_run_typed(
                handle.value, &insert.batch, &dry_report, sizeof(dry_report)) != 0;
            check(dry_run && dry_report.ok && dry_report.insert_count == 1 &&
                      dry_report.created_distance_block_count == 1 &&
                      dry_report.full_reparse_ok &&
                      dry_report.non_target_changed_count == 0,
                  "single-anchor map dry-runs through a tail block");

            KvEditReportSnapshot apply_report{};
            const bool applied = kv_edit_apply_to_memory_typed(
                handle.value, &insert.batch, &apply_report, sizeof(apply_report)) != 0;
            check(applied && apply_report.ok && apply_report.insert_count == 1 &&
                      apply_report.created_distance_block_count == 1 &&
                      apply_report.full_reparse_ok &&
                      apply_report.non_target_changed_count == 0,
                  "single-anchor map applies through a tail block");
            const char* source = applied
                ? kv_get_source_text(handle.value, single_anchor_map_path.u8string().c_str())
                : nullptr;
            const std::string expected_source = single_anchor_before +
                "100;\r\n"
                "CabIlluminance.Set(0.1);\r\n";
            check(source && std::string_view(source) == expected_source,
                  "single-anchor root tail insert preserves monotonic source order");
            kv_free_string(source);

            KvMapSnapshot applied_snapshot{};
            check(kv_get_map_snapshot(handle.value, KV_MAP_SNAPSHOT_VERSION,
                                      &applied_snapshot, sizeof(applied_snapshot)) != 0,
                  "single-anchor map tail insert applied snapshot");
            const bool stable_insert = applied_snapshot.cab_illuminance && std::any_of(
                applied_snapshot.cab_illuminance,
                applied_snapshot.cab_illuminance + applied_snapshot.cab_illuminance_count,
                [&](const KvCabIlluminanceRow& row) {
                    return map_string(applied_snapshot, row.metadata.edit_id) ==
                               "single-anchor-map-tail-insert" &&
                        map_string(applied_snapshot, row.file_path) ==
                            single_anchor_map_path.u8string() &&
                        nearly_equal(row.distance, 100.0) &&
                        row.value.kind == KV_VALUE_NUMBER &&
                        nearly_equal(row.value.number_value, 0.1);
                });
            check(stable_insert,
                  "single-anchor map tail insert retains a stable target identity");
            check(kv_edit_reset_memory(handle.value) != 0,
                  "single-anchor map tail insert reset");
        }
    }
    check(read_text(single_anchor_map_path) == single_anchor_before,
          "single-anchor map tail insert reset leaves disk unchanged");

    const std::filesystem::path monotonic_tail_map_path =
        directory / "monotonic-tail-map.txt";
    const std::string monotonic_tail_before =
        "BveTs Map 2.02:utf-8\r\n"
        "0;\r\n"
        "45;\r\n"
        "541;\r\n"
        "718;\r\n";
    {
        std::ofstream map(monotonic_tail_map_path,
                          std::ios::binary | std::ios::trunc);
        map << monotonic_tail_before;
    }
    {
        MapHandle handle(kv_load_map_ex(monotonic_tail_map_path.u8string().c_str(),
                                        25.0, KV_LOAD_EDIT_METADATA));
        check(handle.value != nullptr, "monotonic-tail map insert load");
        if (handle.value) {
            SimpleInsertBatch insert(
                monotonic_tail_map_path.u8string(), "monotonic-tail-map-insert",
                {{"rowKind", "drawDistance.change"}, {"distance", "866"},
                 {"value", "500"}});
            KvEditReportSnapshot dry_report{};
            const bool dry_run = kv_edit_dry_run_typed(
                handle.value, &insert.batch, &dry_report, sizeof(dry_report)) != 0;
            check(dry_run && dry_report.ok && dry_report.insert_count == 1 &&
                      dry_report.created_distance_block_count == 1 &&
                      dry_report.resolution_request_count == 0 &&
                      dry_report.full_reparse_ok &&
                      dry_report.non_target_changed_count == 0,
                  "monotonic-tail map dry-runs through an EOF distance block");

            KvEditReportSnapshot apply_report{};
            const bool applied = kv_edit_apply_to_memory_typed(
                handle.value, &insert.batch, &apply_report, sizeof(apply_report)) != 0;
            check(applied && apply_report.ok && apply_report.insert_count == 1 &&
                      apply_report.created_distance_block_count == 1 &&
                      apply_report.resolution_request_count == 0 &&
                      apply_report.full_reparse_ok &&
                      apply_report.non_target_changed_count == 0,
                  "monotonic-tail map applies through an EOF distance block");
            const char* source = applied
                ? kv_get_source_text(handle.value, monotonic_tail_map_path.u8string().c_str())
                : nullptr;
            const std::string expected_source = monotonic_tail_before +
                "866;\r\n"
                "DrawDistance.Change(500);\r\n";
            check(source && std::string_view(source) == expected_source,
                  "monotonic-tail map appends a canonical block without restoring 718");
            kv_free_string(source);

            KvMapSnapshot applied_snapshot{};
            check(kv_get_map_snapshot(handle.value, KV_MAP_SNAPSHOT_VERSION,
                                      &applied_snapshot, sizeof(applied_snapshot)) != 0,
                  "monotonic-tail map applied snapshot");
            const bool stable_insert = applied_snapshot.draw_distances &&
                applied_snapshot.draw_distance_count == 1 &&
                std::any_of(applied_snapshot.draw_distances,
                            applied_snapshot.draw_distances +
                                applied_snapshot.draw_distance_count,
                            [&](const KvDrawDistanceRow& row) {
                                return map_string(applied_snapshot, row.metadata.edit_id) ==
                                           "monotonic-tail-map-insert" &&
                                    map_string(applied_snapshot, row.file_path) ==
                                        monotonic_tail_map_path.u8string() &&
                                    nearly_equal(row.distance, 866.0) &&
                                    nearly_equal(row.value, 500.0);
                            });
            check(stable_insert,
                  "monotonic-tail map retains the inserted draw-distance identity");
            check(kv_edit_reset_memory(handle.value) != 0,
                  "monotonic-tail map insert reset");
        }
    }
    check(read_text(monotonic_tail_map_path) == monotonic_tail_before,
          "monotonic-tail map insert reset leaves disk unchanged");
    std::error_code cleanup;
    std::filesystem::remove_all(directory, cleanup);
}

void include_insert_repeated_source_contract() {
    const std::filesystem::path directory =
        std::filesystem::temp_directory_path() /
        ("komapedit-include-insert-repeated-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(directory);
    const std::filesystem::path map_path = directory / "map.txt";
    const std::filesystem::path shared_path = directory / "shared.txt";
    {
        std::ofstream map(map_path, std::ios::binary | std::ios::trunc);
        map << "BveTs Map 2.02:utf-8\r\n"
            << "Structure.Load('structures.csv');\r\n"
            << "include 'shared.txt';\r\n"
            << "include 'shared.txt';\r\n";
    }
    {
        std::ofstream shared(shared_path, std::ios::binary | std::ios::trunc);
        shared << "BveTs Map 2.02:utf-8\r\n"
               << "0;\r\n"
               << "Structure['pole'].Put('',1,0,0,,,,,);\r\n";
    }
    {
        std::ofstream child(directory / "child.txt", std::ios::binary | std::ios::trunc);
        child << "BveTs Map 2.02:utf-8\r\n"
              << "Structure['pole'].Put('',2,0,0,,,,,);\r\n";
    }
    {
        std::ofstream structures(directory / "structures.csv",
                                 std::ios::binary | std::ios::trunc);
        structures << "BveTs Structure List 1.00:utf-8\r\n"
                   << "pole,pole.csv\r\n";
    }
    {
        MapHandle handle(kv_load_map_ex(map_path.u8string().c_str(), 25.0,
                                        KV_LOAD_EDIT_METADATA));
        check(handle.value != nullptr, "repeated-source include insert load");
        if (handle.value) {
            KvMapSnapshot baseline{};
            check(kv_get_map_snapshot(handle.value, KV_MAP_SNAPSHOT_VERSION,
                                      &baseline, sizeof(baseline)) != 0,
                  "repeated-source include insert baseline snapshot");
            const std::uint64_t baseline_puts = baseline.structure_put_count;
            SimpleInsertBatch insert(shared_path.u8string(), "include-insert-repeated",
                                     {{"rowKind", "include"},
                                      {"includePath", "child.txt"}});
            KvEditReportSnapshot report{};
            const bool applied = kv_edit_apply_to_memory_typed(
                handle.value, &insert.batch, &report, sizeof(report)) != 0;
            check(applied && report.ok && report.insert_count == 1 &&
                      report.full_reparse_ok && report.non_target_changed_count == 0,
                  "repeated-source include insert validates every Include invocation");
            KvMapSnapshot after{};
            check(kv_get_map_snapshot(handle.value, KV_MAP_SNAPSHOT_VERSION,
                                      &after, sizeof(after)) != 0,
                  "repeated-source include insert applied snapshot");
            check(after.structure_put_count == baseline_puts + 2,
                  "repeated-source include insert adds one child subtree per invocation");
            const char* source = applied
                ? kv_get_source_text(handle.value, shared_path.u8string().c_str()) : nullptr;
            check(source && std::string_view(source).find(
                              "include 'child.txt';\r\n0;") != std::string_view::npos,
                  "repeated-source include insert stays before the local first distance");
            kv_free_string(source);
            check(kv_edit_reset_memory(handle.value) != 0,
                  "repeated-source include insert reset");
        }
    }
    std::error_code cleanup;
    std::filesystem::remove_all(directory, cleanup);
}

struct IncludeDeleteFixture {
    std::filesystem::path directory;
    std::filesystem::path map_path;

    IncludeDeleteFixture() {
        directory = std::filesystem::temp_directory_path() /
            ("komapedit-include-delete-contract-" + std::to_string(
                std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(directory);
        map_path = directory / "map.txt";
        std::ofstream map(map_path, std::ios::binary | std::ios::trunc);
        map << "BveTs Map 2.02:utf-8\r\n"
            << "0;\r\n"
            << "Structure.Load('structures.csv');\r\n"
            << "$root=1;\r\n"
            << "25;\r\n"
            << "include 'child.txt';\r\n"
            << "$shared=3;\r\n"
            << "100;\r\n"
            << "Structure['pole'].Put('0',1,2,3,0,0,0,0,25);\r\n"
            << "200;\r\n";
        map.close();
        std::ofstream child(directory / "child.txt",
                            std::ios::binary | std::ios::trunc);
        child << "BveTs Map 2.02:utf-8\r\n"
              << "50;\r\n"
              << "$childOnly=7;\r\n"
              << "$shared=2;\r\n"
              << "Structure['pole'].Put('1',9,2,3,0,0,0,0,25);\r\n"
              << "75;\r\n"
              << "include 'grandchild.txt';\r\n"
              << "125;\r\n"
              << "Repeater['rail'].Begin0('0',1,25,5,'pole');\r\n"
              << "150;\r\n"
              << "Repeater['rail'].End();\r\n";
        child.close();
        std::ofstream grandchild(directory / "grandchild.txt",
                                 std::ios::binary | std::ios::trunc);
        grandchild << "BveTs Map 2.02:utf-8\r\n"
                   << "80;\r\n"
                   << "Curve.Begin(300, 0);\r\n"
                   << "105;\r\n"
                   << "Curve.End();\r\n";
        grandchild.close();
        std::ofstream structures(directory / "structures.csv",
                                 std::ios::binary | std::ios::trunc);
        structures << "BveTs Structure List 1.00:utf-8\r\n"
                   << "pole,pole.csv\r\n";
    }

    ~IncludeDeleteFixture() {
        std::error_code error;
        std::filesystem::remove_all(directory, error);
    }

    std::string path_utf8() const { return map_path.u8string(); }
};

struct SimpleEditBatch {
    std::vector<KvEditChange> changes;
    std::vector<KvEditField> fields;
    KvEditBatch batch{};

    SimpleEditBatch(const std::string& edit_id, std::uint32_t operation,
                    const std::string& source_hash, bool with_field = false) {
        static const std::string field_name = "distance";
        static const std::string field_value = "300";
        static const std::string change_id = "typed-contract-simple-edit";
        if (with_field) {
            fields.push_back({utf8_view(field_name), utf8_view(field_value)});
        }
        changes.resize(1);
        changes[0].change_id = utf8_view(change_id);
        changes[0].edit_id = utf8_view(edit_id);
        changes[0].operation = operation;
        if (with_field) {
            changes[0].fields = KvSpan{0, 1};
        }
        changes[0].expected_source_hash = utf8_view(source_hash);
        batch = {changes.data(), changes.size(),
                 fields.empty() ? nullptr : fields.data(),
                 static_cast<std::uint64_t>(fields.size())};
    }
};

void light_edit_contract() {
    TempFixture fixture;
    const std::string source_before =
        "BveTs Map 2.02:utf-8\n"
        "$ambientRed=0.1;\n"
        "$diffuseBlue=0.6;\n"
        "0;\n"
        "Light.Ambient($ambientRed, 0.2, 0.3);\n"
        "Light.Diffuse(0.4, 0.5, $diffuseBlue);\n"
        "Light.Direction(1.25, -0.5);\n";
    {
        std::ofstream map(fixture.map_path, std::ios::binary | std::ios::trunc);
        map << source_before;
    }

    const auto read_disk = [&]() {
        std::ifstream input(fixture.map_path, std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(input),
                           std::istreambuf_iterator<char>());
    };
    const auto source_hash = [](const KvMapSnapshot& snapshot,
                                const KvRowMetadata& metadata) {
        return metadata.source_file_index < snapshot.source_file_count
            ? map_string(snapshot,
                         snapshot.source_files[metadata.source_file_index].source_hash)
            : std::string{};
    };

    MapHandle handle(kv_load_map_ex(
        fixture.path_utf8().c_str(), 25.0,
        KV_LOAD_PREVIEW | KV_LOAD_EDIT_METADATA));
    check(handle.value != nullptr, "light edit fixture load");
    if (!handle.value) return;

    KvMapSnapshot baseline{};
    check(kv_get_map_snapshot(handle.value, KV_MAP_SNAPSHOT_VERSION,
                              &baseline, sizeof(baseline)) != 0,
          "light edit baseline snapshot");
    if (baseline.light_ambient_count != 1 ||
        baseline.light_diffuse_count != 1 ||
        baseline.light_direction_count != 1) {
        check(false, "light edit baseline rows");
        return;
    }
    const std::string ambient_id =
        map_string(baseline, baseline.light_ambient[0].metadata.edit_id);
    const std::string diffuse_id =
        map_string(baseline, baseline.light_diffuse[0].metadata.edit_id);
    const std::string direction_id =
        map_string(baseline, baseline.light_direction[0].metadata.edit_id);
    const std::string baseline_hash =
        source_hash(baseline, baseline.light_ambient[0].metadata);
    check(!ambient_id.empty() && !diffuse_id.empty() && !direction_id.empty() &&
              !baseline_hash.empty(),
          "light edit targets expose source identity");
    if (ambient_id.empty() || diffuse_id.empty() || direction_id.empty() ||
        baseline_hash.empty()) {
        return;
    }

    const auto apply_update = [&](const char* label,
                                  const std::string& edit_id,
                                  std::vector<std::pair<std::string, std::string>> fields) {
        MultiFieldUpdateBatch update(
            std::string("typed-light-update-") + label,
            edit_id, baseline_hash, std::move(fields));
        KvEditReportSnapshot report{};
        const bool applied = kv_edit_apply_to_memory_typed(
            handle.value, &update.batch, &report, sizeof(report)) != 0 &&
            report.ok && report.full_reparse_ok &&
            report.non_target_changed_count == 0;
        check(applied, (std::string("light ") + label + " update applies").c_str());
        return applied;
    };
    apply_update("ambient", ambient_id, {{"green", "0.25"}});
    apply_update("diffuse", diffuse_id, {{"red", "0.45"}});
    apply_update("direction", direction_id, {{"yaw", "-0.75"}});

    const auto memory_source = [&]() {
        const char* raw = kv_get_source_text(handle.value, fixture.path_utf8().c_str());
        std::string text = raw ? raw : "";
        if (raw) kv_free_string(raw);
        return text;
    };
    const std::string updated_memory = memory_source();
    check(updated_memory.find("Light.Ambient($ambientRed,0.25,0.3);") !=
              std::string::npos &&
              updated_memory.find("Light.Diffuse(0.45,0.5,$diffuseBlue);") !=
                  std::string::npos &&
              updated_memory.find("Light.Direction(1.25,-0.75);") !=
                  std::string::npos,
          "light updates preserve untouched source expressions");
    check(read_disk() == source_before,
          "light Apply keeps the map file unchanged before Save");

    KvEditReportSnapshot save_report{};
    check(kv_edit_commit_typed(handle.value, &save_report, sizeof(save_report)) != 0 &&
              save_report.ok && save_report.full_reparse_ok,
          "light Save commits typed updates");
    const std::string committed_source = read_disk();
    check(committed_source == updated_memory,
          "light Save preserves the validated source writeback");

    KvMapSnapshot committed{};
    check(kv_get_map_snapshot(handle.value, KV_MAP_SNAPSHOT_VERSION,
                              &committed, sizeof(committed)) != 0 &&
              committed.light_ambient_count == 1 &&
              committed.light_diffuse_count == 1 &&
              committed.light_direction_count == 1 &&
              nearly_equal(committed.light_ambient[0].green, 0.25) &&
              nearly_equal(committed.light_diffuse[0].red, 0.45) &&
              nearly_equal(committed.light_direction[0].yaw, -0.75),
          "light Save reloads semantic values");
    if (committed.light_ambient_count != 1 ||
        committed.light_diffuse_count != 1 ||
        committed.light_direction_count != 1) {
        return;
    }
    const std::string committed_hash =
        source_hash(committed, committed.light_ambient[0].metadata);
    check(!committed_hash.empty(), "light committed source hash");
    if (committed_hash.empty()) return;

    const auto delete_light = [&](const char* label, const std::string& edit_id) {
        SimpleEditBatch remove(edit_id, KV_EDIT_DELETE, committed_hash);
        KvEditReportSnapshot report{};
        const bool deleted = kv_edit_apply_to_memory_typed(
            handle.value, &remove.batch, &report, sizeof(report)) != 0 &&
            report.ok && report.full_reparse_ok && report.delete_count == 1 &&
            report.non_target_changed_count == 0;
        check(deleted, (std::string("light ") + label + " delete applies").c_str());
        return deleted;
    };
    delete_light("ambient", ambient_id);
    delete_light("diffuse", diffuse_id);
    delete_light("direction", direction_id);

    KvMapSnapshot deleted{};
    check(kv_get_map_snapshot(handle.value, KV_MAP_SNAPSHOT_VERSION,
                              &deleted, sizeof(deleted)) != 0 &&
              deleted.light_ambient_count == 0 &&
              deleted.light_diffuse_count == 0 &&
              deleted.light_direction_count == 0,
          "light deletes refresh the preview snapshot");
    check(read_disk() == committed_source,
          "light Delete keeps the map file unchanged before Save");

    const auto reject_insert = [&](const char* label,
                                   std::string id,
                                   std::vector<std::pair<std::string, std::string>> fields) {
        SimpleInsertBatch insert(fixture.path_utf8(), std::move(id), std::move(fields));
        KvEditReportSnapshot report{};
        const bool rejected = kv_edit_dry_run_typed(
            handle.value, &insert.batch, &report, sizeof(report)) != 0 && !report.ok;
        check(rejected, (std::string("light ") + label + " insert is rejected").c_str());
        return rejected;
    };
    reject_insert("out-of-range RGB", "typed-light-invalid-rgb",
                  {{"rowKind", "light.ambient"}, {"distance", "0"},
                   {"red", "1.1"}, {"green", "0"}, {"blue", "0"}});
    reject_insert("nonzero direction distance", "typed-light-invalid-direction-distance",
                  {{"rowKind", "light.direction"}, {"distance", "1"},
                   {"pitch", "1"}, {"yaw", "2"}});

    const auto insert_light = [&](const char* label,
                                  std::string id,
                                  std::vector<std::pair<std::string, std::string>> fields) {
        SimpleInsertBatch insert(fixture.path_utf8(), std::move(id), std::move(fields));
        KvEditReportSnapshot report{};
        const bool inserted = kv_edit_apply_to_memory_typed(
            handle.value, &insert.batch, &report, sizeof(report)) != 0 &&
            report.ok && report.full_reparse_ok && report.insert_count == 1 &&
            report.non_target_changed_count == 0;
        check(inserted, (std::string("light ") + label + " insert applies").c_str());
        return inserted;
    };
    insert_light("ambient", "typed-light-insert-ambient",
                 {{"rowKind", "light.ambient"}, {"distance", "0"},
                  {"red", "0.11"}, {"green", "0.22"}, {"blue", "0.33"}});
    insert_light("diffuse", "typed-light-insert-diffuse",
                 {{"rowKind", "light.diffuse"}, {"distance", "0"},
                  {"red", "0.44"}, {"green", "0.55"}, {"blue", "0.66"}});
    insert_light("direction", "typed-light-insert-direction",
                 {{"rowKind", "light.direction"}, {"distance", "0"},
                  {"pitch", "1.5"}, {"yaw", "-0.25"}});
    reject_insert("duplicate", "typed-light-insert-duplicate",
                  {{"rowKind", "light.ambient"}, {"distance", "0"},
                   {"red", "0"}, {"green", "0"}, {"blue", "0"}});

    KvMapSnapshot inserted{};
    check(kv_get_map_snapshot(handle.value, KV_MAP_SNAPSHOT_VERSION,
                              &inserted, sizeof(inserted)) != 0 &&
              inserted.light_ambient_count == 1 &&
              inserted.light_diffuse_count == 1 &&
              inserted.light_direction_count == 1 &&
              nearly_equal(inserted.light_ambient[0].red, 0.11) &&
              nearly_equal(inserted.light_diffuse[0].blue, 0.66) &&
              nearly_equal(inserted.light_direction[0].pitch, 1.5),
          "light inserts use the typed semantic rows");
    const std::string inserted_source = memory_source();
    check(inserted_source.find("Light.Ambient(0.11,0.22,0.33);") !=
              std::string::npos &&
              inserted_source.find("Light.Diffuse(0.44,0.55,0.66);") !=
                  std::string::npos &&
              inserted_source.find("Light.Direction(1.5,-0.25);") !=
                  std::string::npos,
          "light inserts emit only the official statement forms");
    check(kv_edit_reset_memory(handle.value) != 0,
          "light reset discards delete and insert preview changes");
    check(read_disk() == committed_source,
          "light reset leaves the saved baseline unchanged");
}

void include_delete_contract() {
    IncludeDeleteFixture fixture;
    MapHandle handle(kv_load_map_ex(fixture.path_utf8().c_str(), 25.0,
                                    KV_LOAD_EDIT_METADATA));
    check(handle.value != nullptr, "include-delete load");
    if (!handle.value) return;
    KvMapSnapshot baseline{};
    check(kv_get_map_snapshot(handle.value, KV_MAP_SNAPSHOT_VERSION,
                              &baseline, sizeof(baseline)) != 0,
          "include-delete baseline snapshot");
    validate_map(baseline, true);
    check(baseline.file_structure_count == 3, "include tree fixture depth");
    const KvStatementRow* child_include = nullptr;
    std::uint64_t include_count = 0;
    for (std::uint64_t i = 0; i < baseline.statement_count; ++i) {
        const KvStatementRow& statement = baseline.statements[i];
        if (map_string(baseline, statement.statement_kind) != "Include") continue;
        ++include_count;
        if (arena_view(baseline.string_data, baseline.string_size,
                       statement.raw_arguments).find("'child.txt'") !=
            std::string_view::npos) {
            child_include = &statement;
        }
    }
    check(include_count == 2 && child_include != nullptr,
          "nested include statements located");
    check(baseline.curve_count == 2 && baseline.repeater_count == 2 &&
              baseline.structure_put_count == 2,
          "baseline subtree element rows");
    if (!child_include) return;
    const std::string include_edit_id =
        map_string(baseline, child_include->edit_id);
    const std::string source_hash = map_string(
        baseline,
        baseline.source_files[child_include->source.source_file_index]
            .source_hash);

    {
        KvEditTargetSnapshot target{};
        check(kv_get_edit_target_typed(handle.value, utf8_view(include_edit_id),
                                       &target, sizeof(target)) != 0,
              "include typed target resolves");
        check(target.version == KV_EDIT_TARGET_SNAPSHOT_VERSION &&
                  arena_view(target.string_data, target.string_size,
                             target.row_kind) == "include",
              "include target row kind");
    }

    auto count_includes = [](const KvMapSnapshot& snapshot) {
        std::uint64_t total = 0;
        for (std::uint64_t i = 0; i < snapshot.statement_count; ++i) {
            if (map_string(snapshot, snapshot.statements[i].statement_kind) ==
                "Include") {
                ++total;
            }
        }
        return total;
    };

    {
        KvEditReportSnapshot report{};
        SimpleEditBatch batch(include_edit_id, KV_EDIT_DELETE, source_hash);
        check(kv_edit_dry_run_typed(
                  handle.value, &batch.batch, &report, sizeof(report)) != 0 &&
                  report.ok && report.delete_count == 1 &&
                  report.full_reparse_ok && report.update_count == 0 &&
                  report.insert_count == 0,
              "include delete dry run");
    }

    {
        KvEditReportSnapshot report{};
        SimpleEditBatch stale_batch(include_edit_id, KV_EDIT_DELETE, "deadbeef");
        check(!(kv_edit_dry_run_typed(
                    handle.value, &stale_batch.batch, &report,
                    sizeof(report)) != 0 && report.ok),
              "include delete rejects a stale source hash");
    }
    {
        KvEditReportSnapshot report{};
        SimpleEditBatch update_batch(include_edit_id, KV_EDIT_UPDATE,
                                     source_hash, true);
        const bool ran = kv_edit_apply_to_memory_typed(
                             handle.value, &update_batch.batch, &report,
                             sizeof(report)) != 0;
        check(!ran || !report.ok,
              "include updates without an includePath field are rejected");
        bool mentions_include_path = false;
        for (std::uint64_t i = 0; i < report.blocking_error_count; ++i) {
            if (arena_view(report.string_data, report.string_size,
                           report.blocking_errors[i]).find(
                               "includePath") != std::string_view::npos) {
                mentions_include_path = true;
            }
        }
        check(mentions_include_path,
              "include update rejection names the required includePath field");
    }

    {
        KvEditReportSnapshot report{};
        SimpleEditBatch batch(include_edit_id, KV_EDIT_DELETE, source_hash);
        const bool apply_ok = kv_edit_apply_to_memory_typed(
            handle.value, &batch.batch, &report, sizeof(report)) != 0;
        check(apply_ok && report.ok && report.full_reparse_ok &&
                  report.non_target_changed_count == 0 &&
                  report.delete_count == 1,
              "include delete apply to memory");
        KvMapSnapshot applied{};
        check(kv_get_map_snapshot(handle.value, KV_MAP_SNAPSHOT_VERSION,
                                  &applied, sizeof(applied)) != 0,
              "include delete applied snapshot");
        validate_map(applied, true);
        std::uint64_t remaining_includes = count_includes(applied);
        check(remaining_includes == 0,
              "deleted include removes the whole nested subtree");
        check(applied.file_structure_count == 1 &&
                  applied.curve_count == 0 && applied.repeater_count == 0 &&
                  applied.structure_put_count == 1,
              "applied snapshot drops only the removed subtree rows");
        check(applied.variable_assignment_count == 2,
              "removed-subtree variable assignments disappear");
        bool root_assignment_kept = false;
        for (std::uint64_t i = 0; i < applied.variable_assignment_count; ++i) {
            if (map_string(applied,
                           applied.variable_assignments[i].normalized_name) ==
                "root") {
                root_assignment_kept = true;
            }
        }
        check(root_assignment_kept, "surviving assignments remain");
    }
    check(kv_edit_reset_memory(handle.value) != 0, "include delete reset");
    baseline = {};
    check(kv_get_map_snapshot(handle.value, KV_MAP_SNAPSHOT_VERSION,
                              &baseline, sizeof(baseline)) != 0,
          "include delete reset refreshes snapshot view");
    check(count_includes(baseline) == 2, "reset restores the include statements");

    {
        KvEditReportSnapshot report{};
        SimpleEditBatch reapply(include_edit_id, KV_EDIT_DELETE, source_hash);
        check(kv_edit_apply_to_memory_typed(
                  handle.value, &reapply.batch, &report, sizeof(report)) != 0 &&
                  report.ok && report.full_reparse_ok,
              "include delete reapplies after reset");
        check(kv_edit_commit_typed(
                  handle.value, &report, sizeof(report)) != 0 && report.ok,
              "include delete commit");
        std::ifstream committed(fixture.map_path, std::ios::binary);
        std::string text((std::istreambuf_iterator<char>(committed)),
                         std::istreambuf_iterator<char>());
        committed.close();
        check(text.find("include 'child.txt';") == std::string::npos,
              "commit removes the include statement from disk");
        check(text.find("$shared=3;") != std::string::npos,
              "commit preserves unrelated statements and CRLF text");
        check(std::filesystem::exists(fixture.directory / "child.txt") &&
                  std::filesystem::exists(fixture.directory / "grandchild.txt"),
              "removing a reference keeps the included source files");
    }
}

void include_diamond_delete_contract() {
    std::filesystem::path directory = std::filesystem::temp_directory_path() /
        ("komapedit-include-diamond-contract-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(directory);
    const std::filesystem::path map_path = directory / "map.txt";
    {
        std::ofstream map(map_path, std::ios::binary | std::ios::trunc);
        map << "BveTs Map 2.02:utf-8\n"
            << "0;\n"
            << "Structure.Load('structures.csv');\n"
            << "include 'twin.txt';\n"
            << "50;\n"
            << "include 'twin.txt';\n"
            << "100;\n"
            << "Structure['pole'].Put('0',1,2,3,0,0,0,0,25);\n";
    }
    {
        std::ofstream twin(directory / "twin.txt",
                           std::ios::binary | std::ios::trunc);
        twin << "BveTs Map 2.02:utf-8\n"
             << "10;\n"
             << "Structure['pole'].Put('1',9,2,3,0,0,0,0,25);\n"
             << "20;\n";
    }
    {
        std::ofstream structures(directory / "structures.csv",
                                 std::ios::binary | std::ios::trunc);
        structures << "BveTs Structure List 1.00:utf-8\n"
                   << "pole,pole.csv\n";
    }
    MapHandle handle(kv_load_map_ex(map_path.u8string().c_str(), 25.0,
                                    KV_LOAD_EDIT_METADATA));
    check(handle.value != nullptr, "diamond include load");
    if (handle.value) {
        KvMapSnapshot baseline{};
        check(kv_get_map_snapshot(handle.value, KV_MAP_SNAPSHOT_VERSION,
                                  &baseline, sizeof(baseline)) != 0,
              "diamond include baseline snapshot");
        check(baseline.file_structure_count == 3 &&
                  baseline.structure_put_count == 3,
              "duplicated include rows exist twice");
        std::vector<std::pair<std::string, std::string>> includes;
        for (std::uint64_t i = 0; i < baseline.statement_count; ++i) {
            const KvStatementRow& statement = baseline.statements[i];
            if (map_string(baseline, statement.statement_kind) != "Include") {
                continue;
            }
            includes.push_back({
                map_string(baseline, statement.edit_id),
                map_string(baseline,
                    baseline.source_files[statement.source.source_file_index]
                        .source_hash),
            });
        }
        check(includes.size() == 2, "two twin include statements");
        if (includes.size() == 2) {
            KvEditReportSnapshot report{};
            SimpleEditBatch first(includes.front().first, KV_EDIT_DELETE,
                                  includes.front().second);
            check(kv_edit_apply_to_memory_typed(
                      handle.value, &first.batch, &report,
                      sizeof(report)) != 0 && report.ok &&
                      report.non_target_changed_count == 0,
                  "one of two identical includes can be deleted");
            KvMapSnapshot applied{};
            check(kv_get_map_snapshot(handle.value, KV_MAP_SNAPSHOT_VERSION,
                                      &applied, sizeof(applied)) != 0,
                  "diamond applied snapshot");
            check(applied.file_structure_count == 2 &&
                      applied.structure_put_count == 2,
                  "deleting one instance keeps the other alive");
            check(kv_edit_reset_memory(handle.value) != 0,
                  "diamond reset");
        }
    }
    std::error_code cleanup;
    std::filesystem::remove_all(directory, cleanup);
}

void include_variable_dependency_blocks_deletion_contract() {
    std::filesystem::path directory = std::filesystem::temp_directory_path() /
        ("komapedit-include-dependency-contract-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(directory);
    const std::filesystem::path map_path = directory / "map.txt";
    {
        std::ofstream map(map_path, std::ios::binary | std::ios::trunc);
        map << "BveTs Map 2.02:utf-8\n"
            << "include 'dep.txt';\n"
            << "$derived=$base+1;\n"
            << "0;\n"
            << "Structure['pole'].Put('0',1,2,3,0,0,0,0,25);\n";
    }
    {
        std::ofstream dep(directory / "dep.txt",
                          std::ios::binary | std::ios::trunc);
        dep << "BveTs Map 2.02:utf-8\n"
            << "$base=4;\n";
    }
    {
        std::ofstream structures(directory / "structures.csv",
                                 std::ios::binary | std::ios::trunc);
        structures << "BveTs Structure List 1.00:utf-8\n"
                   << "pole,pole.csv\n";
    }
    MapHandle handle(kv_load_map_ex(map_path.u8string().c_str(), 25.0,
                                    KV_LOAD_EDIT_METADATA));
    check(handle.value != nullptr, "variable dependency load");
    if (handle.value) {
        KvMapSnapshot baseline{};
        check(kv_get_map_snapshot(handle.value, KV_MAP_SNAPSHOT_VERSION,
                                  &baseline, sizeof(baseline)) != 0,
              "variable dependency baseline snapshot");
        const KvStatementRow* dep_include = nullptr;
        for (std::uint64_t i = 0; i < baseline.statement_count; ++i) {
            const KvStatementRow& statement = baseline.statements[i];
            if (map_string(baseline, statement.statement_kind) == "Include") {
                dep_include = &statement;
            }
        }
        check(dep_include != nullptr, "dependency include located");
        if (dep_include) {
            KvEditReportSnapshot report{};
            SimpleEditBatch batch(
                map_string(baseline, dep_include->edit_id), KV_EDIT_DELETE,
                map_string(baseline,
                    baseline.source_files[dep_include->source.source_file_index]
                        .source_hash));
            const bool ran = kv_edit_apply_to_memory_typed(
                handle.value, &batch.batch, &report, sizeof(report));
            check(!ran || !report.ok ||
                      report.blocking_error_count > 0,
                  "deleting an include that feeds surviving variables is blocked");
        }
    }
    std::error_code cleanup;
    std::filesystem::remove_all(directory, cleanup);
}

struct IncludeReplaceFixture {
    std::filesystem::path directory;
    std::filesystem::path map_path;

    IncludeReplaceFixture() {
        directory = std::filesystem::temp_directory_path() /
            ("komapedit-include-replace-contract-" + std::to_string(
                std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(directory);
        map_path = directory / "map.txt";
        std::ofstream map(map_path, std::ios::binary | std::ios::trunc);
        map << "BveTs Map 2.02:utf-8\r\n"
            << "0;\r\n"
            << "Structure.Load('structures.csv');\r\n"
            << "$root=1;\r\n"
            << "25;\r\n"
            << "include 'child.txt';\r\n"
            << "$after=2;\r\n"
            << "100;\r\n"
            << "Structure['pole'].Put('0',1,2,3,0,0,0,0,25);\r\n"
            << "200;\r\n";
        map.close();
        std::ofstream child(directory / "child.txt",
                            std::ios::binary | std::ios::trunc);
        child << "BveTs Map 2.02:utf-8\r\n"
              << "50;\r\n"
              << "$childOnly=7;\r\n"
              << "Curve.Begin(300, 0);\r\n"
              << "75;\r\n"
              << "Curve.End();\r\n";
        child.close();
        std::ofstream other(directory / "other.txt",
                            std::ios::binary | std::ios::trunc);
        other << "BveTs Map 2.02:utf-8\r\n"
              << "60;\r\n"
              << "$replacementOnly=9;\r\n"
              << "Repeater['rail'].Begin0('0',1,25,5,'pole');\r\n"
              << "120;\r\n"
              << "Repeater['rail'].End();\r\n";
        other.close();
        std::ofstream structures(directory / "structures.csv",
                                 std::ios::binary | std::ios::trunc);
        structures << "BveTs Structure List 1.00:utf-8\r\n"
                   << "pole,pole.csv\r\n";
    }

    ~IncludeReplaceFixture() {
        std::error_code error;
        std::filesystem::remove_all(directory, error);
    }
};

void include_replace_contract() {
    IncludeReplaceFixture fixture;
    MapHandle handle(kv_load_map_ex(fixture.map_path.u8string().c_str(), 25.0,
                                    KV_LOAD_EDIT_METADATA));
    check(handle.value != nullptr, "include-replace load");
    if (!handle.value) return;
    KvMapSnapshot baseline{};
    check(kv_get_map_snapshot(handle.value, KV_MAP_SNAPSHOT_VERSION,
                              &baseline, sizeof(baseline)) != 0,
          "include-replace baseline snapshot");
    validate_map(baseline, true);
    const KvStatementRow* child_include = nullptr;
    for (std::uint64_t i = 0; i < baseline.statement_count; ++i) {
        const KvStatementRow& statement = baseline.statements[i];
        if (map_string(baseline, statement.statement_kind) != "Include") continue;
        if (arena_view(baseline.string_data, baseline.string_size,
                       statement.raw_arguments).find("'child.txt'") !=
            std::string_view::npos) {
            child_include = &statement;
        }
    }
    check(child_include != nullptr, "include-replace target located");
    if (!child_include) return;
    const std::string include_edit_id =
        map_string(baseline, child_include->edit_id);
    const std::string source_hash = map_string(
        baseline,
        baseline.source_files[child_include->source.source_file_index]
            .source_hash);
    const std::uint64_t target_byte_start = child_include->source.byte_start;

    static const std::string replacement_text = "other.txt";
    static const std::string unrepresentable_text = "with'quote.txt";
    static const std::string field_name = "includePath";
    auto build_batch = [&](const std::string& new_text,
                           const std::string& hash) {
        SimpleEditBatch batch(include_edit_id, KV_EDIT_UPDATE, hash);
        batch.fields.assign(1, KvEditField{utf8_view(field_name),
                                           utf8_view(new_text)});
        batch.changes[0].fields = KvSpan{0, 1};
        batch.batch.field_count = 1;
        batch.batch.fields = batch.fields.data();
        return batch;
    };

    {
        KvEditReportSnapshot report{};
        SimpleEditBatch stale_batch =
            build_batch(replacement_text, "deadbeef");
        check(!(kv_edit_dry_run_typed(
                    handle.value, &stale_batch.batch, &report,
                    sizeof(report)) != 0 && report.ok),
              "include replace rejects a stale source hash");
    }
    {
        KvEditReportSnapshot report{};
        SimpleEditBatch quoted = build_batch(unrepresentable_text, source_hash);
        const bool ran = kv_edit_dry_run_typed(handle.value, &quoted.batch,
                                               &report, sizeof(report)) != 0;
        check(!ran || !report.ok,
              "include replace rejects unrepresentable path text");
    }

    {
        KvEditReportSnapshot report{};
        SimpleEditBatch batch = build_batch(replacement_text, source_hash);
        const bool dry_ok = kv_edit_dry_run_typed(
                                handle.value, &batch.batch, &report,
                                sizeof(report)) != 0 &&
                            report.ok && report.update_count == 1 &&
                            report.full_reparse_ok;
        check(dry_ok, "include replace dry run");
    }

    {
        KvEditReportSnapshot report{};
        SimpleEditBatch batch = build_batch(replacement_text, source_hash);
        const bool apply_ok = kv_edit_apply_to_memory_typed(
            handle.value, &batch.batch, &report, sizeof(report)) != 0;
        check(apply_ok && report.ok && report.full_reparse_ok &&
                  report.non_target_changed_count == 0 &&
                  report.update_count == 1,
              "include replace apply to memory");
        KvMapSnapshot applied{};
        check(kv_get_map_snapshot(handle.value, KV_MAP_SNAPSHOT_VERSION,
                                  &applied, sizeof(applied)) != 0,
              "include replace applied snapshot");
        validate_map(applied, true);
        bool statement_updated = false;
        for (std::uint64_t i = 0; i < applied.statement_count; ++i) {
            const KvStatementRow& statement = applied.statements[i];
            if (map_string(applied, statement.statement_kind) != "Include") continue;
            if (statement.source.byte_start == target_byte_start &&
                arena_view(applied.string_data, applied.string_size,
                           statement.raw_arguments) == "'other.txt'") {
                statement_updated = true;
            }
        }
        check(statement_updated, "applied include statement uses the new path");
        bool structure_updated = false;
        for (std::uint64_t i = 0; i < applied.file_structure_count; ++i) {
            const KvFileStructureRow& node = applied.file_structure[i];
            if (map_string(applied, node.include_path) != "other.txt") continue;
            const std::filesystem::path node_absolute =
                std::filesystem::u8path(map_string(applied, node.absolute_path));
            std::error_code equivalent_error;
            if (node.parent_index >= 0 &&
                std::filesystem::equivalent(
                    node_absolute, fixture.directory / "other.txt",
                    equivalent_error)) {
                structure_updated = true;
            }
        }
        check(structure_updated,
              "applied file structure points at the replacement file");
        check(applied.curve_count == 0,
              "old subtree elements disappear after the swap");
        check(applied.repeater_count == 2,
              "replacement subtree elements appear after the swap");
        check(applied.variable_assignment_count == 3,
              "variable assignments follow the swapped subtree");
        std::set<std::string> assignment_names;
        for (std::uint64_t i = 0; i < applied.variable_assignment_count; ++i) {
            assignment_names.insert(map_string(
                applied, applied.variable_assignments[i].normalized_name));
        }
        check(assignment_names.count("childonly") == 0 &&
                  assignment_names.count("replacementonly") == 1 &&
                  assignment_names.count("root") == 1 &&
                  assignment_names.count("after") == 1,
              "swapped variables are replaced while parent ones survive");
    }
    check(kv_edit_reset_memory(handle.value) != 0, "include replace reset");
    KvMapSnapshot restored{};
    check(kv_get_map_snapshot(handle.value, KV_MAP_SNAPSHOT_VERSION,
                              &restored, sizeof(restored)) != 0,
          "include replace reset snapshot");
    bool restore_found = false;
    for (std::uint64_t i = 0; i < restored.statement_count; ++i) {
        const KvStatementRow& statement = restored.statements[i];
        if (statement.source.byte_start == target_byte_start &&
            arena_view(restored.string_data, restored.string_size,
                       statement.raw_arguments).find("'child.txt'") !=
                std::string_view::npos) {
            restore_found = true;
        }
    }
    check(restore_found, "reset restores the original include argument");

    {
        KvEditReportSnapshot report{};
        SimpleEditBatch reapply = build_batch(replacement_text, source_hash);
        check(kv_edit_apply_to_memory_typed(
                  handle.value, &reapply.batch, &report,
                  sizeof(report)) != 0 &&
                  report.ok && report.full_reparse_ok,
              "include replace reapplies after reset");
        check(kv_edit_commit_typed(handle.value, &report, sizeof(report)) != 0 &&
                  report.ok,
              "include replace commit");
        std::ifstream committed(fixture.map_path, std::ios::binary);
        std::string text((std::istreambuf_iterator<char>(committed)),
                         std::istreambuf_iterator<char>());
        committed.close();
        check(text.find("include 'other.txt';") != std::string::npos,
              "commit writes the new include path to disk");
        check(text.find("include 'child.txt';") == std::string::npos,
              "commit removes the old include path from disk");
        check(text.find("$after=2;\r\n") != std::string::npos,
              "commit preserves unrelated statements and CRLF endings");
    }
}

void resource_list_replace_contract() {
    struct Fixture {
        std::filesystem::path directory;
        std::filesystem::path map_path;

        Fixture() {
            directory = std::filesystem::temp_directory_path() /
                ("komapedit-resource-list-replace-contract-" + std::to_string(
                    std::chrono::steady_clock::now().time_since_epoch().count()));
            std::filesystem::create_directories(directory);
            map_path = directory / "map.txt";
            std::ofstream map(map_path, std::ios::binary | std::ios::trunc);
            map << "BveTs Map 2.02:utf-8\r\n"
                << "0;\r\n"
                << "Station.Load('stations-old.csv');\r\n"
                << "Structure.Load('structures-old.csv');\r\n"
                << "Signal.Load('signals-old.csv');\r\n"
                << "Sound.Load('sounds-old.csv');\r\n"
                << "Sound3D.Load('sounds3d-old.csv');\r\n"
                << "$unrelated=1;\r\n";
            const auto write = [&](const char* name, const char* text) {
                std::ofstream file(directory / name, std::ios::binary | std::ios::trunc);
                file << text;
            };
            write("stations-old.csv", "BveTs Station List 2.00:utf-8\r\nold,Old\r\n");
            write("stations-new.csv", "BveTs Station List 0.04:utf-8\r\nnew,New\r\n");
            write("stations-v003.csv", "BveTs Station List 0.03:utf-8\r\nold,Old\r\n");
            write("structures-old.csv", "BveTs Structure List 1.00:utf-8\r\nold,old.csv\r\n");
            write("structures-new.csv", "BveTs Structure List 1.00:utf-8\r\nnew,new.csv\r\n");
            write("signals-old.csv", "BveTs Signal Aspects List 2.00:utf-8\r\nold,old\r\n");
            write("signals-new.csv", "BveTs Signal Aspects List 2.00:utf-8\r\nnew,new\r\n");
            write("signals-v1.csv", "BveTs Signal Aspects List 1.00:utf-8\r\nold,old\r\n");
            write("sounds-old.csv", "BveTs Sound List 2.00:utf-8\r\nold,old.wav\r\n");
            write("sounds-new.csv", "BveTs Sound List 2.00:utf-8\r\nnew,new.wav\r\n");
            write("sounds3d-old.csv", "BveTs Sound List 2.00:utf-8\r\nold3d,old3d.wav\r\n");
            write("sounds3d-new.csv", "BveTs Sound List 2.00:utf-8\r\nnew3d,new3d.wav\r\n");
            write("sounds-v1.csv", "BveTs Sound List 1.00:utf-8\r\nold,old.wav\r\n");
        }

        ~Fixture() {
            std::error_code error;
            std::filesystem::remove_all(directory, error);
        }
    } fixture;

    struct PathBatch {
        std::vector<std::string> change_ids;
        std::vector<std::string> edit_ids;
        std::vector<std::string> values;
        std::vector<KvEditField> fields;
        std::vector<KvEditChange> changes;
        KvEditBatch batch{};

        PathBatch(const std::vector<std::pair<std::string, std::string>>& updates,
                  const std::string& source_hash) {
            static const std::string field_name = "resourceListPath";
            change_ids.reserve(updates.size());
            edit_ids.reserve(updates.size());
            values.reserve(updates.size());
            for (size_t i = 0; i < updates.size(); ++i) {
                change_ids.push_back("resource-list-path-" + std::to_string(i));
                edit_ids.push_back(updates[i].first);
                values.push_back(updates[i].second);
            }
            fields.reserve(updates.size());
            changes.resize(updates.size());
            for (size_t i = 0; i < updates.size(); ++i) {
                fields.push_back({utf8_view(field_name), utf8_view(values[i])});
                KvEditChange& change = changes[i];
                change.change_id = utf8_view(change_ids[i]);
                change.edit_id = utf8_view(edit_ids[i]);
                change.operation = KV_EDIT_UPDATE;
                change.fields = {static_cast<std::uint64_t>(i), 1};
                change.expected_source_hash = utf8_view(source_hash);
            }
            batch = {changes.data(), static_cast<std::uint64_t>(changes.size()),
                     fields.data(), static_cast<std::uint64_t>(fields.size())};
        }
    };

    MapHandle handle(kv_load_map_ex(fixture.map_path.u8string().c_str(), 25.0,
                                    KV_LOAD_EDIT_METADATA));
    check(handle.value != nullptr, "resource-list replace load");
    if (!handle.value) return;
    KvMapSnapshot baseline{};
    check(kv_get_map_snapshot(handle.value, KV_MAP_SNAPSHOT_VERSION,
                              &baseline, sizeof(baseline)) != 0,
          "resource-list replace baseline snapshot");
    validate_map(baseline, true);

    std::map<std::string, std::string> edit_ids;
    std::string source_hash;
    for (std::uint64_t i = 0; i < baseline.statement_count; ++i) {
        const KvStatementRow& statement = baseline.statements[i];
        const std::string kind = map_string(baseline, statement.statement_kind);
        const char* edit_kind = nullptr;
        if (kind == "Station.Load") edit_kind = "station.load";
        else if (kind == "Structure.Load") edit_kind = "structure.load";
        else if (kind == "Signal.Load") edit_kind = "signal.load";
        else if (kind == "Sound.Load") edit_kind = "sound.load";
        else if (kind == "Sound3D.Load") edit_kind = "sound3d.load";
        if (edit_kind) {
            edit_ids[edit_kind] = map_string(baseline, statement.edit_id);
            if (statement.source.source_file_index < baseline.source_file_count) {
                source_hash = map_string(
                    baseline,
                    baseline.source_files[statement.source.source_file_index].source_hash);
            }
        }
    }
    check(edit_ids.size() == 5 && !source_hash.empty(),
          "resource-list replace source targets located");
    if (edit_ids.size() != 5 || source_hash.empty()) return;

    const auto update_one = [&](const std::string& kind, const std::string& path,
                                const char* marker, const char* label) {
        PathBatch batch({{edit_ids[kind], path}}, source_hash);
        KvEditReportSnapshot report{};
        const bool ran = kv_edit_dry_run_typed(
            handle.value, &batch.batch, &report, sizeof(report)) != 0;
        check(ran && !report.ok &&
                  edit_report_has_error_containing(report, marker),
              label);
    };
    update_one("structure.load", "signals-new.csv",
               "resource-list-header-mismatch:structure",
               "resource-list replacement rejects a mismatched list type");
    update_one("station.load", "signals-new.csv",
               "resource-list-header-mismatch:station",
               "resource-list replacement rejects a mismatched Station List type");
    update_one("station.load", "stations-v003.csv",
               "resource-list-header-mismatch:station",
               "resource-list replacement rejects unsupported Station List version");
    update_one("signal.load", "signals-v1.csv",
               "resource-list-header-mismatch:signal",
               "resource-list replacement rejects unsupported Signal List version");
    update_one("sound.load", "sounds-v1.csv",
               "resource-list-header-mismatch:sound",
               "resource-list replacement rejects unsupported Sound List version");

    const std::vector<std::pair<std::string, std::string>> replacements = {
        {edit_ids["station.load"], "stations-new.csv"},
        {edit_ids["structure.load"], "structures-new.csv"},
        {edit_ids["signal.load"], "signals-new.csv"},
        {edit_ids["sound.load"], "sounds-new.csv"},
        {edit_ids["sound3d.load"], "sounds3d-new.csv"},
    };
    {
        PathBatch batch(replacements, source_hash);
        KvEditReportSnapshot report{};
        const bool dry_ok = kv_edit_dry_run_typed(
                                handle.value, &batch.batch, &report,
                                sizeof(report)) != 0 &&
            report.ok && report.full_reparse_ok && report.update_count == 5;
        check(dry_ok,
              "resource-list replacement accepts Station List 0.04 compatibility and other supported headers");
    }
    KvEditReportSnapshot applied_report{};
    PathBatch apply_batch(replacements, source_hash);
    check(kv_edit_apply_to_memory_typed(
              handle.value, &apply_batch.batch, &applied_report,
              sizeof(applied_report)) != 0 &&
              applied_report.ok && applied_report.full_reparse_ok &&
              applied_report.non_target_changed_count == 0 &&
              applied_report.update_count == 5,
          "resource-list replacement applies all lists to memory");
    KvMapSnapshot applied{};
    check(kv_get_map_snapshot(handle.value, KV_MAP_SNAPSHOT_VERSION,
                              &applied, sizeof(applied)) != 0,
          "resource-list replacement applied snapshot");
    const auto list_path = [](const KvMapSnapshot& snapshot, std::uint32_t kind) {
        for (std::uint64_t i = 0; i < snapshot.resource_list_load_count; ++i) {
            const KvResourceListLoadRow& row = snapshot.resource_list_loads[i];
            if (row.kind == kind) return map_string(snapshot, row.evaluated_path);
        }
        return std::string{};
    };
    check(list_path(applied, KV_RESOURCE_LIST_STATION) == "stations-new.csv" &&
              list_path(applied, KV_RESOURCE_LIST_STRUCTURE) == "structures-new.csv" &&
              list_path(applied, KV_RESOURCE_LIST_SIGNAL) == "signals-new.csv" &&
              list_path(applied, KV_RESOURCE_LIST_SOUND) == "sounds-new.csv" &&
              list_path(applied, KV_RESOURCE_LIST_SOUND_3D) == "sounds3d-new.csv" &&
              applied.station_list_count == 1 &&
              applied.structure_model_count == 1 &&
              applied.signal_aspect_count == 1 && applied.sound_list_count == 2,
          "resource-list replacement refreshes the typed list rows");

    check(kv_edit_reset_memory(handle.value) != 0,
          "resource-list replacement reset");
    KvMapSnapshot restored{};
    check(kv_get_map_snapshot(handle.value, KV_MAP_SNAPSHOT_VERSION,
                              &restored, sizeof(restored)) != 0,
          "resource-list replacement restored snapshot");
    bool restored_old_station = false;
    bool restored_old_structure = false;
    for (std::uint64_t i = 0; i < restored.resource_list_load_count; ++i) {
        const KvResourceListLoadRow& row = restored.resource_list_loads[i];
        restored_old_station = restored_old_station ||
            (row.kind == KV_RESOURCE_LIST_STATION &&
             map_string(restored, row.evaluated_path) == "stations-old.csv");
        restored_old_structure = restored_old_structure ||
            (row.kind == KV_RESOURCE_LIST_STRUCTURE &&
             map_string(restored, row.evaluated_path) == "structures-old.csv");
    }
    check(restored_old_station && restored_old_structure,
          "resource-list replacement reset restores old paths");

    PathBatch commit_batch(replacements, source_hash);
    KvEditReportSnapshot commit_report{};
    check(kv_edit_apply_to_memory_typed(
              handle.value, &commit_batch.batch, &commit_report,
              sizeof(commit_report)) != 0 && commit_report.ok &&
              kv_edit_commit_typed(handle.value, &commit_report,
                                   sizeof(commit_report)) != 0 &&
              commit_report.ok,
          "resource-list replacement commits the rewritten map paths");
    std::ifstream committed(fixture.map_path, std::ios::binary);
    std::string text((std::istreambuf_iterator<char>(committed)),
                     std::istreambuf_iterator<char>());
    check(text.find("Station.Load('stations-new.csv');\r\n") != std::string::npos &&
              text.find("Structure.Load('structures-new.csv');\r\n") != std::string::npos &&
              text.find("Signal.Load('signals-new.csv');\r\n") != std::string::npos &&
              text.find("Sound.Load('sounds-new.csv');\r\n") != std::string::npos &&
              text.find("Sound3D.Load('sounds3d-new.csv');\r\n") != std::string::npos &&
              text.find("$unrelated=1;\r\n") != std::string::npos,
          "resource-list replacement preserves CRLF and unrelated map source");
    MapHandle reloaded(kv_load_map_ex(fixture.map_path.u8string().c_str(), 25.0,
                                      KV_LOAD_EDIT_METADATA));
    KvMapSnapshot reloaded_snapshot{};
    check(reloaded.value != nullptr &&
              kv_get_map_snapshot(reloaded.value, KV_MAP_SNAPSHOT_VERSION,
                                  &reloaded_snapshot, sizeof(reloaded_snapshot)) != 0 &&
              list_path(reloaded_snapshot, KV_RESOURCE_LIST_STATION) ==
                  "stations-new.csv" &&
              reloaded_snapshot.station_list_count == 1 &&
              map_string(reloaded_snapshot,
                         reloaded_snapshot.station_list[0].fields[0]) == "new",
          "resource-list replacement reloads Station List path and rows");
}

void resource_list_insert_contract() {
    const std::filesystem::path directory = std::filesystem::temp_directory_path() /
        ("komapedit-resource-list-insert-contract-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(directory);
    const std::filesystem::path map_path = directory / "map.txt";
    {
        std::ofstream map(map_path, std::ios::binary | std::ios::trunc);
        map << "BveTs Map 2.02:utf-8\r\n"
            << "# keep this comment\r\n"
            << "0;\r\n"
            << "$unrelated=1;\r\n";
    }
    struct ResourceFile {
        const char* kind;
        const char* path;
        const char* header;
        std::uint32_t snapshot_kind;
        const char* statement;
    };
    const std::array<ResourceFile, 5> resources = {{
        {"structure", "structures.csv", "BveTs Structure List 2.00:utf-8\r\n",
         KV_RESOURCE_LIST_STRUCTURE, "Structure.Load('structures.csv');\r\n"},
        {"signal", "signals.csv", "BveTs Signal Aspects List 2.00:utf-8\r\n",
         KV_RESOURCE_LIST_SIGNAL, "Signal.Load('signals.csv');\r\n"},
        {"sound", "sounds.csv", "BveTs Sound List 2.00:utf-8\r\n",
         KV_RESOURCE_LIST_SOUND, "Sound.Load('sounds.csv');\r\n"},
        {"sound3d", "sounds3d.csv", "BveTs Sound List 2.00:utf-8\r\n",
         KV_RESOURCE_LIST_SOUND_3D, "Sound3D.Load('sounds3d.csv');\r\n"},
        {"station", "stations.csv", "BveTs Station List 2.00:utf-8\r\n",
         KV_RESOURCE_LIST_STATION, "Station.Load('stations.csv');\r\n"},
    }};
    for (const ResourceFile& resource : resources) {
        std::ofstream file(directory / resource.path, std::ios::binary | std::ios::trunc);
        file << resource.header;
    }

    const auto list_path = [](const KvMapSnapshot& snapshot, std::uint32_t kind) {
        for (std::uint64_t i = 0; i < snapshot.resource_list_load_count; ++i) {
            const KvResourceListLoadRow& row = snapshot.resource_list_loads[i];
            if (row.kind == kind) return map_string(snapshot, row.evaluated_path);
        }
        return std::string{};
    };
    struct ResourceListInsertBatch {
        std::string target_file;
        std::vector<std::string> change_ids;
        std::vector<std::string> field_names;
        std::vector<std::string> field_values;
        std::vector<KvEditField> fields;
        std::vector<KvEditChange> changes;
        KvEditBatch batch{};

        ResourceListInsertBatch(std::string target,
                                const std::array<ResourceFile, 5>& resources)
            : target_file(std::move(target)) {
            change_ids.reserve(resources.size());
            field_names.reserve(resources.size() * 3);
            field_values.reserve(resources.size() * 3);
            changes.resize(resources.size());
            for (const ResourceFile& resource : resources) {
                change_ids.push_back(std::string("resource-list-batch-") + resource.kind);
                field_names.insert(field_names.end(),
                                   {"rowKind", "resourceListKind", "resourceListPath"});
                field_values.insert(field_values.end(),
                                    {"resourceList.load", resource.kind, resource.path});
            }
            fields.reserve(field_names.size());
            for (size_t i = 0; i < field_names.size(); ++i) {
                fields.push_back({utf8_view(field_names[i]), utf8_view(field_values[i])});
            }
            for (size_t i = 0; i < changes.size(); ++i) {
                KvEditChange& change = changes[i];
                change.change_id = utf8_view(change_ids[i]);
                change.edit_id = utf8_view(change_ids[i]);
                change.operation = KV_EDIT_INSERT;
                change.fields = {static_cast<std::uint64_t>(i * 3), 3};
                change.target_file_path = utf8_view(target_file);
            }
            batch = {changes.data(), static_cast<std::uint64_t>(changes.size()),
                     fields.data(), static_cast<std::uint64_t>(fields.size())};
        }
    };

    const std::filesystem::path blank_map_path = directory / "blank-map.txt";
    {
        std::ofstream blank_map(blank_map_path, std::ios::binary | std::ios::trunc);
        blank_map << "BveTs Map 2.02:utf-8\r\n";
    }
    MapHandle blank_handle(kv_load_map_ex(blank_map_path.u8string().c_str(), 25.0,
                                          KV_LOAD_EDIT_METADATA));
    check(blank_handle.value != nullptr, "resource-list insert blank map load");
    if (blank_handle.value) {
        ResourceListInsertBatch batch(blank_map_path.u8string(), resources);
        KvEditReportSnapshot dry_report{};
        check(kv_edit_dry_run_typed(blank_handle.value, &batch.batch, &dry_report,
                                    sizeof(dry_report)) != 0 &&
                  dry_report.ok && dry_report.full_reparse_ok &&
                  dry_report.insert_count == resources.size() &&
                  dry_report.non_target_changed_count == 0,
              "resource-list insert batch dry run on a blank standard map");
        KvEditReportSnapshot apply_report{};
        check(kv_edit_apply_to_memory_typed(blank_handle.value, &batch.batch, &apply_report,
                                            sizeof(apply_report)) != 0 &&
                  apply_report.ok && apply_report.full_reparse_ok &&
                  apply_report.insert_count == resources.size() &&
                  apply_report.non_target_changed_count == 0,
              "resource-list insert batch applies to a blank standard map");
        std::string expected_source = "BveTs Map 2.02:utf-8\r\n";
        for (const ResourceFile& resource : resources) expected_source += resource.statement;
        const char* blank_source = kv_get_source_text(
            blank_handle.value, blank_map_path.u8string().c_str());
        check(blank_source && std::string_view(blank_source) == expected_source,
              "resource-list insert batch preserves blank-map CRLF reference order");
        kv_free_string(blank_source);
        KvEditReportSnapshot commit_report{};
        check(kv_edit_commit_typed(blank_handle.value, &commit_report, sizeof(commit_report)) != 0 &&
                  commit_report.ok,
              "resource-list insert batch commits blank map references");
        MapHandle blank_reloaded(kv_load_map_ex(blank_map_path.u8string().c_str(), 25.0,
                                                KV_LOAD_EDIT_METADATA));
        KvMapSnapshot blank_snapshot{};
        bool blank_paths_match = blank_reloaded.value != nullptr &&
            kv_get_map_snapshot(blank_reloaded.value, KV_MAP_SNAPSHOT_VERSION,
                                &blank_snapshot, sizeof(blank_snapshot)) != 0 &&
            blank_snapshot.resource_list_load_count == resources.size();
        for (const ResourceFile& resource : resources) {
            blank_paths_match = blank_paths_match &&
                list_path(blank_snapshot, resource.snapshot_kind) == resource.path;
        }
        check(blank_paths_match,
              "resource-list insert batch reloads all empty blank-map lists");
    }

    MapHandle handle(kv_load_map_ex(map_path.u8string().c_str(), 25.0,
                                    KV_LOAD_EDIT_METADATA));
    check(handle.value != nullptr, "resource-list insert load");
    if (!handle.value) return;
    for (const ResourceFile& resource : resources) {
        SimpleInsertBatch insert(
            map_path.u8string(), std::string("resource-list-insert-") + resource.kind,
            {{"rowKind", "resourceList.load"},
             {"resourceListKind", resource.kind},
             {"resourceListPath", resource.path}});
        KvEditReportSnapshot dry_report{};
        check(kv_edit_dry_run_typed(handle.value, &insert.batch, &dry_report,
                                    sizeof(dry_report)) != 0 &&
                  dry_report.ok && dry_report.full_reparse_ok &&
                  dry_report.insert_count == 1 &&
                  dry_report.non_target_changed_count == 0,
              (std::string("resource-list insert dry run ") + resource.kind).c_str());
        KvEditReportSnapshot apply_report{};
        check(kv_edit_apply_to_memory_typed(handle.value, &insert.batch, &apply_report,
                                            sizeof(apply_report)) != 0 &&
                  apply_report.ok && apply_report.full_reparse_ok &&
                  apply_report.insert_count == 1 &&
                  apply_report.non_target_changed_count == 0,
              (std::string("resource-list insert applies ") + resource.kind).c_str());
    }

    KvMapSnapshot applied{};
    check(kv_get_map_snapshot(handle.value, KV_MAP_SNAPSHOT_VERSION,
                              &applied, sizeof(applied)) != 0,
          "resource-list insert applied snapshot");
    bool paths_match = applied.resource_list_load_count == resources.size();
    for (const ResourceFile& resource : resources) {
        paths_match = paths_match &&
            list_path(applied, resource.snapshot_kind) == resource.path;
    }
    check(paths_match, "resource-list insert refreshes all typed load paths");

    const char* source = kv_get_source_text(handle.value, map_path.u8string().c_str());
    const std::string source_text = source ? source : "";
    kv_free_string(source);
    size_t prior = 0;
    bool source_order_matches = true;
    for (const ResourceFile& resource : resources) {
        const size_t position = source_text.find(resource.statement);
        source_order_matches = source_order_matches && position != std::string::npos &&
            position >= prior;
        prior = position == std::string::npos
            ? prior
            : position + std::string_view(resource.statement).size();
    }
    const size_t distance = source_text.find("0;\r\n");
    check(source_order_matches && distance != std::string::npos && prior <= distance &&
              source_text.find("# keep this comment\r\n") != std::string::npos &&
              source_text.find("$unrelated=1;\r\n") != std::string::npos,
          "resource-list insert stays ordered before the first distance and preserves source");

    const std::filesystem::path duplicate_path = directory / "structures-duplicate.csv";
    {
        std::ofstream duplicate(duplicate_path, std::ios::binary | std::ios::trunc);
        duplicate << "BveTs Structure List 2.00:utf-8\r\n";
    }
    SimpleInsertBatch duplicate(
        map_path.u8string(), "resource-list-insert-duplicate",
        {{"rowKind", "resourceList.load"},
         {"resourceListKind", "structure"},
         {"resourceListPath", "structures-duplicate.csv"}});
    KvEditReportSnapshot duplicate_report{};
    const bool duplicate_ran = kv_edit_dry_run_typed(
        handle.value, &duplicate.batch, &duplicate_report, sizeof(duplicate_report)) != 0;
    check(!duplicate_ran || !duplicate_report.ok || duplicate_report.blocking_error_count > 0,
          "resource-list insert rejects a duplicate list type");

    KvEditReportSnapshot commit_report{};
    check(kv_edit_commit_typed(handle.value, &commit_report, sizeof(commit_report)) != 0 &&
              commit_report.ok,
          "resource-list insert commits all paths");
    std::ifstream committed(map_path, std::ios::binary);
    const std::string committed_text((std::istreambuf_iterator<char>(committed)),
                                     std::istreambuf_iterator<char>());
    bool committed_paths_match = true;
    for (const ResourceFile& resource : resources) {
        committed_paths_match = committed_paths_match &&
            committed_text.find(resource.statement) != std::string::npos;
    }
    check(committed_paths_match && committed_text.find("$unrelated=1;\r\n") != std::string::npos,
          "resource-list insert commit preserves exact source references");

    MapHandle reloaded(kv_load_map_ex(map_path.u8string().c_str(), 25.0,
                                      KV_LOAD_EDIT_METADATA));
    KvMapSnapshot reloaded_snapshot{};
    bool reload_paths_match = reloaded.value != nullptr &&
        kv_get_map_snapshot(reloaded.value, KV_MAP_SNAPSHOT_VERSION,
                            &reloaded_snapshot, sizeof(reloaded_snapshot)) != 0;
    for (const ResourceFile& resource : resources) {
        reload_paths_match = reload_paths_match &&
            list_path(reloaded_snapshot, resource.snapshot_kind) == resource.path;
    }
    check(reload_paths_match, "resource-list insert reloads all empty lists");

    std::error_code cleanup;
    std::filesystem::remove_all(directory, cleanup);
}

void resource_list_content_insert_contract() {
    struct Fixture {
        std::filesystem::path directory;
        std::filesystem::path map_path;

        Fixture() {
            directory = std::filesystem::temp_directory_path() /
                ("komapedit-resource-list-content-insert-" + std::to_string(
                    std::chrono::steady_clock::now().time_since_epoch().count()));
            std::filesystem::create_directories(directory);
            map_path = directory / "map.txt";
            std::ofstream map(map_path, std::ios::binary | std::ios::trunc);
            map << "BveTs Map 2.02:utf-8\r\n"
                << "Structure.Load('structures.csv');\r\n"
                << "Sound.Load('sounds.csv');\r\n"
                << "Sound3D.Load('sounds3d.csv');\r\n"
                << "Station.Load('stations.csv');\r\n"
                << "Signal.Load('signals.csv');\r\n"
                << "0;\r\n";
            std::ofstream structures(directory / "structures.csv",
                                     std::ios::binary | std::ios::trunc);
            structures << "BveTs Structure List 1.00:utf-8\r\n"
                       << "\t// keep structure comment, ignored\r\n"
                       << "s1,s1.csv // structure inline comment\r\n"
                       << "s2,s2.csv\r\n"
                       << "g1,g1.csv\r\n"
                       << "g2,g2.csv\r\n";
            std::ofstream sounds(directory / "sounds.csv",
                                 std::ios::binary | std::ios::trunc);
            sounds << "BveTs Sound List 2.00:utf-8\r\n"
                   << "\t// keep sound comment, ignored\r\n"
                   << "snd1,snd1.wav,1 // sound inline comment\r\n"
                   << "snd2,snd2.wav,1\r\n";
            std::ofstream sounds3d(directory / "sounds3d.csv",
                                   std::ios::binary | std::ios::trunc);
            sounds3d << "BveTs Sound List 2.00:utf-8\r\n"
                     << "\t// keep sound3d comment, ignored\r\n"
                     << "snd3a,snd3a.wav,1 // sound3d inline comment\r\n"
                     << "snd3b,snd3b.wav,1\r\n";
            std::ofstream stations(directory / "stations.csv",
                                   std::ios::binary | std::ios::trunc);
            stations << "BveTs Station List 1.00:utf-8\r\n"
                     << "\t// keep station comment, ignored\r\n"
                     << "sta1,Station 1,09:00,09:01,30,15,1,20,100,arr.wav,dep.wav,1,0 // station inline comment\r\n"
                     << "sta2,Station 2,10:00,10:01,30,15,1,20,100,arr.wav,dep.wav,1,0\r\n";
            std::ofstream signals(directory / "signals.csv",
                                  std::ios::binary | std::ios::trunc);
            signals << "BveTs Signal Aspects List 2.00:utf-8\r\n"
                    << "\t// keep signal comment, ignored\r\n"
                    << "sigA,s1,s2 // signal inline comment\r\n"
                    << ",g1,g2\r\n"
                    << "sigB,s1\r\n";
        }

        ~Fixture() {
            std::error_code error;
            std::filesystem::remove_all(directory, error);
        }
    } fixture;

    const auto read_bytes = [](const std::filesystem::path& path) {
        std::ifstream input(path, std::ios::binary);
        return std::string((std::istreambuf_iterator<char>(input)),
                           std::istreambuf_iterator<char>());
    };
    const auto source_text = [](void* handle, const std::string& path) {
        const char* text = kv_get_source_text(handle, path.c_str());
        std::string value = text ? text : "";
        kv_free_string(text);
        return value;
    };
    const auto crlf_only = [](const std::string& text) {
        for (size_t index = 0; index < text.size(); ++index) {
            if (text[index] == '\n' && (index == 0 || text[index - 1] != '\r')) {
                return false;
            }
        }
        return true;
    };
    const auto csv_count_at = [](const std::string& text, size_t begin) {
        const size_t end = text.find_first_of("\r\n", begin);
        return static_cast<size_t>(std::count(
            text.begin() + static_cast<std::ptrdiff_t>(begin),
            text.begin() + static_cast<std::ptrdiff_t>(
                end == std::string::npos ? text.size() : end), ',')) + 1;
    };

    MapHandle handle(kv_load_map_ex(
        fixture.map_path.u8string().c_str(), 25.0, KV_LOAD_EDIT_METADATA));
    check(handle.value != nullptr, "resource-list content insert load");
    if (!handle.value) return;
    KvMapSnapshot baseline{};
    check(kv_get_map_snapshot(handle.value, KV_MAP_SNAPSHOT_VERSION,
                              &baseline, sizeof(baseline)) != 0,
          "resource-list content insert baseline snapshot");
    if (baseline.structure_model_count != 4 || baseline.station_list_count != 2 ||
        baseline.signal_aspect_count != 2 || baseline.sound_list_count != 4) {
        check(false, "resource-list // comments do not create typed rows");
        return;
    }

    struct ListTarget {
        std::string file;
        std::string hash;
        std::string first_edit_id;
        std::string second_edit_id;
    };
    const auto target_for = [&](const KvRowMetadata& first,
                                const KvRowMetadata& second) {
        ListTarget target;
        if (first.source_file_index >= baseline.source_file_count ||
            second.source_file_index != first.source_file_index) {
            return target;
        }
        const KvSourceFileRow& source =
            baseline.source_files[first.source_file_index];
        target.file = map_string(baseline, source.file_path);
        target.hash = map_string(baseline, source.source_hash);
        target.first_edit_id = map_string(baseline, first.edit_id);
        target.second_edit_id = map_string(baseline, second.edit_id);
        return target;
    };
    std::vector<const KvSoundListRow*> sounds;
    std::vector<const KvSoundListRow*> sounds3d;
    for (std::uint64_t index = 0; index < baseline.sound_list_count; ++index) {
        const KvSoundListRow& row = baseline.sound_list[index];
        (row.is_3d ? sounds3d : sounds).push_back(&row);
    }
    check(sounds.size() >= 2 && sounds3d.size() >= 2,
          "resource-list content insert sound rows present");
    if (sounds.size() < 2 || sounds3d.size() < 2) return;
    const KvSignalAspectRow& signal_a = baseline.signal_aspects[0];
    const bool signal_fields_are_clean =
        baseline.string_refs && signal_a.structure_keys.count == 4 &&
        span_valid(signal_a.structure_keys, baseline.string_ref_count) &&
        map_string(baseline, baseline.string_refs[signal_a.structure_keys.offset]) == "s1" &&
        map_string(baseline, baseline.string_refs[signal_a.structure_keys.offset + 1]) == "s2" &&
        map_string(baseline, baseline.string_refs[signal_a.structure_keys.offset + 2]) == "g1" &&
        map_string(baseline, baseline.string_refs[signal_a.structure_keys.offset + 3]) == "g2";
    check(map_string(baseline, baseline.structure_models[0].file_path) == "s1.csv" &&
              map_string(baseline, sounds[0]->file_path) == "snd1.wav" &&
              sounds[0]->buffer_count == 1 &&
              map_string(baseline, sounds3d[0]->file_path) == "snd3a.wav" &&
              sounds3d[0]->buffer_count == 1 &&
              map_string(baseline, baseline.station_list[0].fields[12]) == "0" &&
              signal_fields_are_clean,
          "resource-list inline // comments are excluded from fields");

    const ListTarget structure_target = target_for(
        baseline.structure_models[0].metadata, baseline.structure_models[1].metadata);
    const ListTarget station_target = target_for(
        baseline.station_list[0].metadata, baseline.station_list[1].metadata);
    const ListTarget signal_target = target_for(
        baseline.signal_aspects[0].metadata, baseline.signal_aspects[1].metadata);
    const ListTarget sound_target = target_for(
        sounds[0]->metadata, sounds[1]->metadata);
    const ListTarget sound3d_target = target_for(
        sounds3d[0]->metadata, sounds3d[1]->metadata);
    check(!structure_target.file.empty() && !station_target.file.empty() &&
              !signal_target.file.empty() && !sound_target.file.empty() &&
              !sound3d_target.file.empty(),
          "resource-list content insert source targets present");
    if (structure_target.file.empty() || station_target.file.empty() ||
        signal_target.file.empty() || sound_target.file.empty() ||
        sound3d_target.file.empty()) {
        return;
    }

    struct InsertSpec {
        std::string id;
        std::string row_kind;
        std::string target_file;
        std::string source_hash;
        std::string insert_before_edit_id;
        std::vector<std::pair<std::string, std::string>> fields;
    };
    struct InsertBatch {
        std::vector<InsertSpec> specs;
        std::vector<std::string> change_ids;
        std::vector<std::string> field_names;
        std::vector<std::string> field_values;
        std::vector<KvEditField> fields;
        std::vector<KvEditChange> changes;
        KvEditBatch batch{};

        explicit InsertBatch(std::vector<InsertSpec> input)
            : specs(std::move(input)) {
            size_t field_count = 0;
            for (const InsertSpec& spec : specs) field_count += 1 + spec.fields.size();
            change_ids.reserve(specs.size());
            field_names.reserve(field_count);
            field_values.reserve(field_count);
            changes.resize(specs.size());
            std::vector<std::pair<size_t, size_t>> ranges;
            ranges.reserve(specs.size());
            for (const InsertSpec& spec : specs) {
                const size_t begin = field_names.size();
                field_names.push_back("rowKind");
                field_values.push_back(spec.row_kind);
                for (const auto& field : spec.fields) {
                    field_names.push_back(field.first);
                    field_values.push_back(field.second);
                }
                ranges.push_back({begin, field_names.size() - begin});
                change_ids.push_back(spec.id);
            }
            fields.reserve(field_names.size());
            for (size_t index = 0; index < field_names.size(); ++index) {
                fields.push_back({utf8_view(field_names[index]),
                                  utf8_view(field_values[index])});
            }
            for (size_t index = 0; index < specs.size(); ++index) {
                const InsertSpec& spec = specs[index];
                KvEditChange& change = changes[index];
                change.change_id = utf8_view(change_ids[index]);
                change.edit_id = utf8_view(change_ids[index]);
                change.operation = KV_EDIT_INSERT;
                change.fields = {static_cast<std::uint64_t>(ranges[index].first),
                                 static_cast<std::uint64_t>(ranges[index].second)};
                change.target_file_path = utf8_view(spec.target_file);
                change.expected_source_hash = utf8_view(spec.source_hash);
                change.insert_before_edit_id = utf8_view(spec.insert_before_edit_id);
            }
            batch = {changes.data(), static_cast<std::uint64_t>(changes.size()),
                     fields.data(), static_cast<std::uint64_t>(fields.size())};
        }
    };
    const auto structure_fields = [](const std::string& key) {
        return std::vector<std::pair<std::string, std::string>>{
            {"structureKey", key}, {"filePath", key + ".csv"}};
    };
    const auto sound_fields = [](const std::string& key) {
        return std::vector<std::pair<std::string, std::string>>{
            {"soundKey", key}, {"filePath", key + ".wav"}, {"bufferCount", "1"}};
    };
    const auto station_fields = [](const std::string& key) {
        static constexpr std::array<const char*, 13> names = {{
            "stationKey", "stationName", "arrivalTime", "depertureTime",
            "stoppageTime", "defaultTime", "signalFlag", "alightingTime",
            "passengers", "arrivalSoundKey", "depertureSoundKey", "doorReopen",
            "stuckInDoor",
        }};
        std::vector<std::pair<std::string, std::string>> result;
        result.reserve(names.size());
        for (size_t index = 0; index < names.size(); ++index) {
            result.emplace_back(names[index], index == 0 ? key : std::string{});
        }
        return result;
    };
    const auto signal_fields = [](const std::string& key, bool add_glare) {
        std::vector<std::pair<std::string, std::string>> result;
        result.emplace_back("signalAspectKey", key);
        for (size_t index = 1; index <= 5; ++index) {
            result.emplace_back("structureKey" + std::to_string(index),
                                index == 1 ? "s1" : std::string{});
        }
        if (add_glare) {
            result.emplace_back("addGlare", "5");
            for (size_t index = 6; index <= 10; ++index) {
                result.emplace_back("structureKey" + std::to_string(index),
                                    index == 6 ? "g1" : std::string{});
            }
        }
        return result;
    };
    const auto append_three = [](std::vector<InsertSpec>& specs,
                                 const ListTarget& target,
                                 const char* row_kind,
                                 const char* prefix,
                                 const auto& fields) {
        specs.push_back({std::string(prefix) + "-above-a", row_kind,
                         target.file, target.hash, target.first_edit_id,
                         fields(std::string(prefix) + "AboveA")});
        specs.push_back({std::string(prefix) + "-above-b", row_kind,
                         target.file, target.hash, target.first_edit_id,
                         fields(std::string(prefix) + "AboveB")});
        specs.push_back({std::string(prefix) + "-below", row_kind,
                         target.file, target.hash, target.second_edit_id,
                         fields(std::string(prefix) + "Below")});
    };

    std::vector<InsertSpec> inserts;
    append_three(inserts, structure_target, "structure.model", "ctStructure",
                 structure_fields);
    append_three(inserts, sound_target, "sound.list", "ctSound", sound_fields);
    append_three(inserts, sound3d_target, "sound3D.list", "ctSound3D", sound_fields);
    append_three(inserts, station_target, "station.list", "ctStation", station_fields);
    inserts.push_back({"ct-signal-above-a", "signal.aspect", signal_target.file,
                       signal_target.hash, signal_target.first_edit_id,
                       signal_fields("ctSignalAboveA", false)});
    inserts.push_back({"ct-signal-above-b", "signal.aspect", signal_target.file,
                       signal_target.hash, signal_target.first_edit_id,
                       signal_fields("ctSignalAboveB", true)});
    inserts.push_back({"ct-signal-below", "signal.aspect", signal_target.file,
                       signal_target.hash, signal_target.second_edit_id,
                       signal_fields("ctSignalBelow", false)});
    InsertBatch batch(std::move(inserts));
    KvEditReportSnapshot dry_report{};
    check(kv_edit_dry_run_typed(handle.value, &batch.batch, &dry_report,
                                sizeof(dry_report)) != 0 && dry_report.ok &&
              dry_report.full_reparse_ok && dry_report.insert_count == 15 &&
              dry_report.non_target_changed_count == 0,
          "resource-list content insert dry-run");
    KvEditReportSnapshot apply_report{};
    check(kv_edit_apply_to_memory_typed(handle.value, &batch.batch, &apply_report,
                                        sizeof(apply_report)) != 0 && apply_report.ok &&
              apply_report.full_reparse_ok && apply_report.insert_count == 15,
          "resource-list content insert apply-to-memory");

    const auto ordered = [&](const ListTarget& target,
                             std::initializer_list<std::string_view> keys) {
        const std::string text = source_text(handle.value, target.file);
        size_t previous = 0;
        bool result = crlf_only(text);
        for (std::string_view key : keys) {
            const size_t position = text.find(key, previous);
            result = result && position != std::string::npos;
            if (position == std::string::npos) break;
            previous = position + key.size();
        }
        return result;
    };
    const std::string structure_source = source_text(handle.value, structure_target.file);
    const std::string sound_source = source_text(handle.value, sound_target.file);
    const std::string sound3d_source = source_text(handle.value, sound3d_target.file);
    const std::string station_source = source_text(handle.value, station_target.file);
    const std::string signal_source = source_text(handle.value, signal_target.file);
    check(ordered(structure_target, {"ctStructureAboveA,", "ctStructureAboveB,", "s1,",
                                     "ctStructureBelow,", "s2,"}) &&
              ordered(sound_target, {"ctSoundAboveA,", "ctSoundAboveB,", "snd1,",
                                     "ctSoundBelow,", "snd2,"}) &&
              ordered(sound3d_target, {"ctSound3DAboveA,", "ctSound3DAboveB,", "snd3a,",
                                       "ctSound3DBelow,", "snd3b,"}) &&
              ordered(station_target, {"ctStationAboveA,", "ctStationAboveB,", "sta1,",
                                       "ctStationBelow,", "sta2,"}) &&
              ordered(signal_target, {"ctSignalAboveA,", "ctSignalAboveB,", "sigA,",
                                      ",g1,g2", "ctSignalBelow,", "sigB,"}),
          "resource-list content insert keeps same-anchor order and Signal glare blocks");
    const size_t structure_above = structure_source.find("ctStructureAboveA,");
    const size_t sound_above = sound_source.find("ctSoundAboveA,");
    const size_t sound3d_above = sound3d_source.find("ctSound3DAboveA,");
    const size_t station_above = station_source.find("ctStationAboveA,");
    const size_t signal_above = signal_source.find("ctSignalAboveA,");
    const size_t signal_glare = signal_source.find("\n,g1,,,,", signal_source.find("ctSignalAboveB,"));
    check(structure_source.find("\t// keep structure comment, ignored\r\n") != std::string::npos &&
              sound_source.find("\t// keep sound comment, ignored\r\n") != std::string::npos &&
              sound3d_source.find("\t// keep sound3d comment, ignored\r\n") != std::string::npos &&
              station_source.find("\t// keep station comment, ignored\r\n") != std::string::npos &&
              signal_source.find("\t// keep signal comment, ignored\r\n") != std::string::npos &&
              structure_above != std::string::npos && csv_count_at(structure_source, structure_above) == 2 &&
              sound_above != std::string::npos && csv_count_at(sound_source, sound_above) == 3 &&
              sound3d_above != std::string::npos && csv_count_at(sound3d_source, sound3d_above) == 3 &&
              station_above != std::string::npos && csv_count_at(station_source, station_above) == 13 &&
              signal_above != std::string::npos && csv_count_at(signal_source, signal_above) == 6 &&
              signal_glare != std::string::npos && csv_count_at(signal_source, signal_glare + 1) == 6,
          "resource-list content insert emits exact CSV fields and preserves CRLF/comments");

    KvMapSnapshot applied{};
    check(kv_get_map_snapshot(handle.value, KV_MAP_SNAPSHOT_VERSION,
                              &applied, sizeof(applied)) != 0 &&
              applied.structure_model_count == baseline.structure_model_count + 3 &&
              applied.station_list_count == baseline.station_list_count + 3 &&
              applied.signal_aspect_count == baseline.signal_aspect_count + 3 &&
              applied.sound_list_count == baseline.sound_list_count + 6,
          "resource-list content insert hydrates all five typed row families");
    check(kv_edit_reset_memory(handle.value) != 0,
          "resource-list content insert reset");
    KvMapSnapshot reset{};
    check(kv_get_map_snapshot(handle.value, KV_MAP_SNAPSHOT_VERSION,
                              &reset, sizeof(reset)) != 0 &&
              reset.structure_model_count == baseline.structure_model_count &&
              reset.station_list_count == baseline.station_list_count &&
              reset.signal_aspect_count == baseline.signal_aspect_count &&
              reset.sound_list_count == baseline.sound_list_count,
          "resource-list content insert reset restores baseline rows");
    check(source_text(handle.value, structure_target.file) ==
              read_bytes(fixture.directory / "structures.csv") &&
              source_text(handle.value, signal_target.file) ==
              read_bytes(fixture.directory / "signals.csv"),
          "resource-list content insert reset leaves disk source unchanged");

    const std::string signal_b_id = signal_target.second_edit_id;
    MultiFieldUpdateBatch add_glare(
        "ct-signal-add-glare", signal_b_id, signal_target.hash,
        {{"addGlare", "1"}, {"structureKey2", "g1"}});
    KvEditReportSnapshot glare_dry{};
    check(kv_edit_dry_run_typed(handle.value, &add_glare.batch, &glare_dry,
                                sizeof(glare_dry)) != 0 && glare_dry.ok,
          "resource-list content insert validates existing Signal glare addition");
    KvEditReportSnapshot glare_apply{};
    check(kv_edit_apply_to_memory_typed(handle.value, &add_glare.batch, &glare_apply,
                                        sizeof(glare_apply)) != 0 && glare_apply.ok &&
              source_text(handle.value, signal_target.file).find("sigB,s1\r\n,g1") !=
                  std::string::npos,
          "resource-list content insert adds Signal glare with source newline");
    check(kv_edit_reset_memory(handle.value) != 0,
          "resource-list content insert glare reset");

    InsertBatch invalid_main({{
        "ct-signal-invalid-main", "signal.aspect", signal_target.file,
        signal_target.hash, signal_target.first_edit_id,
        {{"signalAspectKey", "ctSignalInvalidMain"},
         {"structureKey1", ""}, {"structureKey2", ""},
         {"structureKey3", ""}, {"structureKey4", ""},
         {"structureKey5", ""}},
    }});
    InsertBatch invalid_glare({{
        "ct-signal-invalid-glare", "signal.aspect", signal_target.file,
        signal_target.hash, signal_target.first_edit_id,
        {{"signalAspectKey", "ctSignalInvalidGlare"},
         {"structureKey1", "s1"}, {"structureKey2", ""},
         {"structureKey3", ""}, {"structureKey4", ""},
         {"structureKey5", ""}, {"addGlare", "5"},
         {"structureKey6", ""}, {"structureKey7", ""},
         {"structureKey8", ""}, {"structureKey9", ""},
         {"structureKey10", ""}},
    }});
    KvEditReportSnapshot invalid_main_report{};
    KvEditReportSnapshot invalid_glare_report{};
    const bool invalid_main_ran = kv_edit_dry_run_typed(
        handle.value, &invalid_main.batch, &invalid_main_report,
        sizeof(invalid_main_report)) != 0;
    const bool invalid_glare_ran = kv_edit_dry_run_typed(
        handle.value, &invalid_glare.batch, &invalid_glare_report,
        sizeof(invalid_glare_report)) != 0;
    check((!invalid_main_ran || !invalid_main_report.ok ||
           invalid_main_report.blocking_error_count != 0) &&
              (!invalid_glare_ran || !invalid_glare_report.ok ||
               invalid_glare_report.blocking_error_count != 0),
          "resource-list content insert rejects empty Signal main and glare rows");
}

void include_replace_variable_dependency_blocks_contract() {
    std::filesystem::path directory = std::filesystem::temp_directory_path() /
        ("komapedit-include-replace-dependency-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(directory);
    const std::filesystem::path map_path = directory / "map.txt";
    {
        std::ofstream map(map_path, std::ios::binary | std::ios::trunc);
        map << "BveTs Map 2.02:utf-8\r\n"
            << "include 'dep.txt';\r\n"
            << "$derived=$base+1;\r\n"
            << "0;\r\n"
            << "Structure['pole'].Put('0',1,2,3,0,0,0,0,25);\r\n";
    }
    {
        std::ofstream dep(directory / "dep.txt",
                          std::ios::binary | std::ios::trunc);
        dep << "BveTs Map 2.02:utf-8\r\n"
            << "$base=4;\r\n";
    }
    {
        std::ofstream other(directory / "other.txt",
                            std::ios::binary | std::ios::trunc);
        other << "BveTs Map 2.02:utf-8\r\n"
              << "$unrelated=5;\r\n";
    }
    {
        std::ofstream structures(directory / "structures.csv",
                                 std::ios::binary | std::ios::trunc);
        structures << "BveTs Structure List 1.00:utf-8\r\n"
                   << "pole,pole.csv\r\n";
    }
    MapHandle handle(kv_load_map_ex(map_path.u8string().c_str(), 25.0,
                                    KV_LOAD_EDIT_METADATA));
    check(handle.value != nullptr, "replace dependency load");
    if (handle.value) {
        KvMapSnapshot baseline{};
        check(kv_get_map_snapshot(handle.value, KV_MAP_SNAPSHOT_VERSION,
                                  &baseline, sizeof(baseline)) != 0,
              "replace dependency baseline snapshot");
        const KvStatementRow* dep_include = nullptr;
        for (std::uint64_t i = 0; i < baseline.statement_count; ++i) {
            const KvStatementRow& statement = baseline.statements[i];
            if (map_string(baseline, statement.statement_kind) == "Include") {
                dep_include = &statement;
            }
        }
        check(dep_include != nullptr, "dependency include located");
        if (dep_include) {
            const std::string source_hash = map_string(
                baseline,
                baseline.source_files[dep_include->source.source_file_index]
                    .source_hash);
            static const std::string field_name = "includePath";
            static const std::string replacement_text = "other.txt";
            SimpleEditBatch batch(map_string(baseline, dep_include->edit_id),
                                  KV_EDIT_UPDATE, source_hash);
            batch.fields.clear();
            batch.fields.push_back(
                {utf8_view(field_name), utf8_view(replacement_text)});
            batch.changes[0].fields = KvSpan{0, 1};
            batch.batch.field_count = 1;
            KvEditReportSnapshot report{};
            const bool ran = kv_edit_apply_to_memory_typed(
                handle.value, &batch.batch, &report,
                sizeof(report)) != 0;
            check(!ran || !report.ok || report.blocking_error_count > 0,
                  "replacing an include that feeds surviving variables is blocked");
        }
    }
    std::error_code cleanup;
    std::filesystem::remove_all(directory, cleanup);
}

// One batch that both inserts new *.Load references and edits resource-list
// rows must plan in two stages: the row editIds of a newly connected list only
// exist once the Load statements have been reparsed into the working copy.
// Also proves that a header-only Structure List can take its first logical
// row, that Apply leaves disk untouched, and that commit writes every patched
// file with its original encoding and newline style.
namespace {
void staged_dump_blocking_errors(const char* label,
                                 const KvEditReportSnapshot& report) {
    if (report.ok) return;
    for (std::uint64_t index = 0; index < report.blocking_error_count; ++index) {
        std::string_view error = arena_view(
            report.string_data, report.string_size,
            report.blocking_errors[index]);
        std::cerr << "  [" << label << "] blocking: " << error << "\n";
    }
}
} // namespace

void staged_resource_list_workflow_contract() {
    struct Fixture {
        std::filesystem::path directory;
        std::filesystem::path map_path;
        std::filesystem::path structures_path;
        std::filesystem::path stations_path;
        std::string map_bytes;
        std::string structures_bytes;
        std::string stations_bytes;

        Fixture() {
            directory = std::filesystem::temp_directory_path() /
                ("komapedit-staged-resource-list-" + std::to_string(
                    std::chrono::steady_clock::now().time_since_epoch().count()));
            std::filesystem::create_directories(directory);
            map_path = directory / "map.txt";
            structures_path = directory / "structures.csv";
            stations_path = directory / "stations.csv";
            // A distance-free header-only map: official *.Load statements do
            // not require any preceding distance statement.
            map_bytes = "BveTs Map 2.02:utf-8\r\n";
            // A header-only Structure List is a valid official starting point.
            structures_bytes = "BveTs Structure List 2.00:utf-8\r\n";
            stations_bytes =
                "BveTs Station List 2.00:utf-8\r\n"
                "sta1,Station 1,,,,,,,,,,,,\r\n";
            write_file(map_path, map_bytes);
            write_file(structures_path, structures_bytes);
            write_file(stations_path, stations_bytes);
        }

        static void write_file(const std::filesystem::path& path,
                               const std::string& bytes) {
            std::ofstream output(path, std::ios::binary | std::ios::trunc);
            output << bytes;
        }

        ~Fixture() {
            std::error_code error;
            std::filesystem::remove_all(directory, error);
        }
    } fixture;

    const auto read_bytes = [](const std::filesystem::path& path) {
        std::ifstream input(path, std::ios::binary);
        return std::string((std::istreambuf_iterator<char>(input)),
                           std::istreambuf_iterator<char>());
    };

    MapHandle handle(kv_load_map_ex(
        fixture.map_path.u8string().c_str(), 25.0, KV_LOAD_EDIT_METADATA));
    check(handle.value != nullptr, "staged workflow loads a distance-free map");
    if (!handle.value) return;

    struct BatchStorage {
        // utf8_view keeps raw pointers into these vectors, so every backing
        // string must be stable for the lifetime of the batch: reserve once,
        // never grow, and only store owned copies.
        std::vector<std::string> strings;
        std::vector<KvEditField> fields;
        std::vector<KvEditChange> changes;
        KvEditBatch batch{};

        void init() {
            strings.reserve(128);
            fields.reserve(128);
            changes.reserve(4);
        }
    };
    const auto keep_stable = [&](BatchStorage& storage,
                                 const std::string& value) {
        storage.strings.push_back(value);
        return utf8_view(storage.strings.back());
    };
    const auto add_load_insert = [&](BatchStorage& storage,
                                     const std::string& id,
                                     const char* kind,
                                     const char* relative) {
        if (storage.changes.empty()) storage.init();
        KvEditChange change{};
        change.change_id = keep_stable(storage, id);
        change.edit_id = keep_stable(storage, id);
        change.operation = KV_EDIT_INSERT;
        change.target_file_path =
            keep_stable(storage, fixture.map_path.u8string());
        const size_t field_begin = storage.fields.size();
        for (const auto& [name, value] :
             {std::pair<const char*, const char*>{"rowKind", "resourceList.load"},
              {"resourceListKind", kind},
              {"resourceListPath", relative}}) {
            storage.fields.push_back({keep_stable(storage, name),
                                      keep_stable(storage, value)});
        }
        change.fields = KvSpan{static_cast<std::uint64_t>(field_begin), 3};
        storage.changes.push_back(change);
    };

    // Stage A: two references on a distance-free blank map. Applying them to
    // memory must expose the station rows and the (still empty) structure list
    // exactly like the GUI preview does.
    {
        BatchStorage refs;
        add_load_insert(refs, "stage-a-structure-ref", "structure", "structures.csv");
        add_load_insert(refs, "stage-a-station-ref", "station", "stations.csv");
        refs.batch = {refs.changes.data(),
                      static_cast<std::uint64_t>(refs.changes.size()),
                      refs.fields.data(),
                      static_cast<std::uint64_t>(refs.fields.size())};
        KvEditReportSnapshot dry_report{};
        const bool dry_ok = kv_edit_dry_run_typed(
            handle.value, &refs.batch, &dry_report, sizeof(dry_report)) != 0;
        staged_dump_blocking_errors("stage-a-dry", dry_report);
        check(dry_ok && dry_report.ok && dry_report.full_reparse_ok &&
                  dry_report.insert_count == 2,
              "staged workflow dry run adds two references without distances");
        KvEditReportSnapshot apply_report{};
        const bool apply_ok = kv_edit_apply_to_memory_typed(
            handle.value, &refs.batch, &apply_report, sizeof(apply_report)) != 0;
        staged_dump_blocking_errors("stage-a-apply", apply_report);
        check(apply_ok && apply_report.ok && apply_report.full_reparse_ok &&
                  apply_report.insert_count == 2,
              "staged workflow applies reference batch to memory");
    }

    KvMapSnapshot connected{};
    check(kv_get_map_snapshot(handle.value, KV_MAP_SNAPSHOT_VERSION, &connected,
                              sizeof(connected)) != 0,
          "staged workflow connected snapshot");
    check(connected.station_list_count == 1 && connected.structure_model_count == 0,
          "staged workflow exposes existing rows after the memory apply");
    if (connected.station_list_count != 1) return;
    const std::string station_edit_id =
        map_string(connected, connected.station_list[0].metadata.edit_id);
    check(!station_edit_id.empty(), "staged workflow station editId present");
    std::string structures_hash;
    std::string stations_hash;
    for (std::uint64_t index = 0; index < connected.source_file_count; ++index) {
        const KvSourceFileRow& file = connected.source_files[index];
        const std::string path = map_string(connected, file.file_path);
        if (path == fixture.structures_path.u8string()) {
            structures_hash = map_string(connected, file.source_hash);
        } else if (path == fixture.stations_path.u8string()) {
            stations_hash = map_string(connected, file.source_hash);
        }
    }
    check(!structures_hash.empty() && !stations_hash.empty(),
          "staged workflow collected disk baseline hashes");
    if (structures_hash.empty() || stations_hash.empty()) return;

    check(kv_edit_reset_memory(handle.value) != 0,
          "staged workflow reset restores disk baseline");
    KvMapSnapshot reset_snapshot{};
    check(kv_get_map_snapshot(handle.value, KV_MAP_SNAPSHOT_VERSION,
                              &reset_snapshot, sizeof(reset_snapshot)) != 0 &&
              reset_snapshot.resource_list_load_count == 0 &&
              reset_snapshot.station_list_count == 0,
          "staged workflow reset drops unsaved references");

    // Stage B: one ledger batch that mixes new Load references with list-row
    // work. The station update intentionally precedes the inserts in the batch
    // so planning cannot rely on array order: only the staged replay finds the
    // row editIds. All strings live in one stable backing vector because
    // utf8_view keeps raw pointers.
    BatchStorage staged;
    staged.init();
    const auto keep = [&](const std::string& value) {
        return keep_stable(staged, value);
    };
    const auto add_field = [&](const char* name, const char* value) {
        staged.fields.push_back({keep(name), keep(value)});
    };
    staged.changes.resize(4);
    // 0: update an existing station row loaded by a reference in this batch.
    {
        KvEditChange& change = staged.changes[0];
        change.change_id = keep("stage-b-station-update");
        change.edit_id = keep(station_edit_id);
        change.operation = KV_EDIT_UPDATE;
        change.expected_source_hash = keep(stations_hash);
        const size_t field_begin = staged.fields.size();
        add_field("stationName", "Updated Name");
        change.fields = KvSpan{static_cast<std::uint64_t>(field_begin), 1};
    }
    // 1: first logical row of a header-only Structure List.
    {
        KvEditChange& change = staged.changes[1];
        change.change_id = keep("stage-b-structure-first-row");
        change.edit_id = keep("stage-b-structure-first-row");
        change.operation = KV_EDIT_INSERT;
        change.target_file_path = keep(fixture.structures_path.u8string());
        change.expected_source_hash = keep(structures_hash);
        const size_t field_begin = staged.fields.size();
        add_field("rowKind", "structure.model");
        add_field("structureKey", "stNew");
        add_field("filePath", "stNew.x");
        change.fields = KvSpan{static_cast<std::uint64_t>(field_begin), 3};
    }
    // 2 + 3: the two new Load references themselves.
    {
        KvEditChange& first = staged.changes[2];
        first.change_id = keep("stage-b-structure-ref");
        first.edit_id = keep("stage-b-structure-ref");
        first.operation = KV_EDIT_INSERT;
        first.target_file_path = keep(fixture.map_path.u8string());
        size_t field_begin = staged.fields.size();
        add_field("rowKind", "resourceList.load");
        add_field("resourceListKind", "structure");
        add_field("resourceListPath", "structures.csv");
        first.fields = KvSpan{static_cast<std::uint64_t>(field_begin), 3};
        KvEditChange& second = staged.changes[3];
        second.change_id = keep("stage-b-station-ref");
        second.edit_id = keep("stage-b-station-ref");
        second.operation = KV_EDIT_INSERT;
        second.target_file_path = keep(fixture.map_path.u8string());
        field_begin = staged.fields.size();
        add_field("rowKind", "resourceList.load");
        add_field("resourceListKind", "station");
        add_field("resourceListPath", "stations.csv");
        second.fields = KvSpan{static_cast<std::uint64_t>(field_begin), 3};
    }
    KvEditBatch staged_batch{staged.changes.data(),
                             static_cast<std::uint64_t>(staged.changes.size()),
                             staged.fields.data(),
                             static_cast<std::uint64_t>(staged.fields.size())};

    KvEditReportSnapshot staged_dry{};
    const bool staged_dry_ok = kv_edit_dry_run_typed(
        handle.value, &staged_batch, &staged_dry, sizeof(staged_dry)) != 0;
    staged_dump_blocking_errors("stage-b-dry", staged_dry);
    check(staged_dry_ok && staged_dry.ok && staged_dry.full_reparse_ok &&
              staged_dry.insert_count == 3 && staged_dry.update_count == 1 &&
              staged_dry.non_target_changed_count == 0,
          "staged workflow plans mixed Load-and-row batch in one dry run");

    KvEditReportSnapshot staged_apply{};
    check(kv_edit_apply_to_memory_typed(handle.value, &staged_batch,
                                        &staged_apply,
                                        sizeof(staged_apply)) != 0 &&
              staged_apply.ok && staged_apply.full_reparse_ok &&
              staged_apply.insert_count == 3 && staged_apply.update_count == 1,
          "staged workflow applies mixed Load-and-row batch to memory");

    KvMapSnapshot applied{};
    check(kv_get_map_snapshot(handle.value, KV_MAP_SNAPSHOT_VERSION, &applied,
                              sizeof(applied)) != 0,
          "staged workflow applied snapshot");
    bool applied_state_matches =
        applied.structure_model_count == 1 &&
        applied.station_list_count == 1 &&
        applied.resource_list_load_count == 2;
    if (applied.structure_model_count == 1) {
        applied_state_matches = applied_state_matches &&
            map_string(applied, applied.structure_models[0].file_path) == "stNew.x";
    }
    if (applied.station_list_count == 1) {
        const KvStationListRow& row = applied.station_list[0];
        applied_state_matches = applied_state_matches &&
            map_string(applied, row.fields[0]) == "sta1" &&
            map_string(applied, row.fields[1]) == "Updated Name" &&
            map_string(applied, row.metadata.edit_id) == station_edit_id;
    }
    check(applied_state_matches,
          "staged workflow proves target rows and stable editIds after apply");

    // Apply must stay in memory: every touched file keeps its exact bytes.
    check(read_bytes(fixture.map_path) == fixture.map_bytes &&
              read_bytes(fixture.structures_path) == fixture.structures_bytes &&
              read_bytes(fixture.stations_path) == fixture.stations_bytes,
          "staged workflow apply leaves all disk files untouched");

    KvEditReportSnapshot commit_report{};
    check(kv_edit_commit_typed(handle.value, &commit_report,
                               sizeof(commit_report)) != 0 &&
              commit_report.ok,
          "staged workflow commits the merged multi-file patch");
    const std::string committed_map = read_bytes(fixture.map_path);
    const std::string committed_structures = read_bytes(fixture.structures_path);
    const std::string committed_stations = read_bytes(fixture.stations_path);
    check(committed_map ==
              std::string("BveTs Map 2.02:utf-8\r\n") +
              "Structure.Load('structures.csv');\r\n" +
              "Station.Load('stations.csv');\r\n",
          "staged workflow commits both references in source order");
    check(committed_structures ==
              std::string("BveTs Structure List 2.00:utf-8\r\nstNew,stNew.x\r\n"),
          "staged workflow appends the first structure row with fixed columns");
    check(committed_stations ==
              std::string("BveTs Station List 2.00:utf-8\r\n"
                          "sta1,Updated Name,,,,,,,,,,,,\r\n"),
          "staged workflow updates the station row preserving its field count");

    MapHandle reloaded(kv_load_map_ex(fixture.map_path.u8string().c_str(), 25.0,
                                      KV_LOAD_EDIT_METADATA));
    KvMapSnapshot reloaded_snapshot{};
    bool reload_matches = reloaded.value != nullptr &&
        kv_get_map_snapshot(reloaded.value, KV_MAP_SNAPSHOT_VERSION,
                            &reloaded_snapshot, sizeof(reloaded_snapshot)) != 0 &&
        reloaded_snapshot.structure_model_count == 1 &&
        reloaded_snapshot.station_list_count == 1 &&
        reloaded_snapshot.resource_list_load_count == 2;
    if (reload_matches) {
        reload_matches =
            map_string(reloaded_snapshot,
                       reloaded_snapshot.structure_models[0].file_path) == "stNew.x" &&
            map_string(reloaded_snapshot,
                       reloaded_snapshot.station_list[0].fields[1]) == "Updated Name";
    }
    check(reload_matches, "staged workflow reloads the committed state");
}

int edit_contract() {
    include_insert_contract();
    empty_submap_insert_contract();
    include_insert_repeated_source_contract();
    include_delete_contract();
    include_diamond_delete_contract();
    include_variable_dependency_blocks_deletion_contract();
    include_replace_contract();
    resource_list_replace_contract();
    resource_list_insert_contract();
    resource_list_content_insert_contract();
    staged_resource_list_workflow_contract();
    include_replace_variable_dependency_blocks_contract();
    line_ending_edit_contract();
    repeater_linkage_boundary_contract();
    other_track_key_edit_contract();
    repeater_key_edit_contract();
    repeater_insert_contract();
    own_track_insert_contract();
    other_track_insert_contract();
    environment_argument_shape_edit_contract();
    sound3d_edit_contract();
    light_edit_contract();
    TempFixture fixture;
    check_coordinate_offset_method_conversions(fixture.path_utf8());
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

    check(baseline.other_track_change_count == 2,
          "other-track editable rows present");
    if (baseline.other_track_change_count >= 1) {
        const KvOtherTrackChangeRow& other = baseline.other_track_changes[0];
        const std::string other_edit_id =
            map_string(baseline, other.metadata.edit_id);
        const KvSourceFileRow& other_source =
            baseline.source_files[other.metadata.source_file_index];
        UpdateBatch other_update(
            other_edit_id, map_string(baseline, other_source.source_hash),
            "4.25", "parameter0");
        KvEditReportSnapshot other_report{};
        check(kv_edit_apply_to_memory_typed(
                  handle.value, &other_update.batch, &other_report,
                  sizeof(other_report)) != 0 && other_report.ok &&
                  other_report.non_target_changed_count == 0,
              "other-track parameter apply-to-memory");
        bool layout_preserved = false;
        for (std::uint64_t i = 0; i < other_report.preview_snippet_count; ++i) {
            const std::string_view after = arena_view(
                other_report.string_data, other_report.string_size,
                other_report.preview_snippets[i].after_text);
            layout_preserved = layout_preserved ||
                after.find("Position(4.25, 0);") !=
                    std::string_view::npos;
        }
        check(layout_preserved,
              "other-track writeback preserves untouched argument layout");
        KvMapSnapshot other_applied{};
        check(kv_get_map_snapshot(handle.value, KV_MAP_SNAPSHOT_VERSION,
                                  &other_applied, sizeof(other_applied)) != 0,
              "other-track applied snapshot");
        const KvOtherTrackChangeRow* applied_other = nullptr;
        for (std::uint64_t i = 0; i < other_applied.other_track_change_count; ++i) {
            if (map_string(other_applied,
                           other_applied.other_track_changes[i].metadata.edit_id) ==
                other_edit_id) {
                applied_other = &other_applied.other_track_changes[i];
                break;
            }
        }
        check(applied_other && applied_other->parameters.count == 2 &&
                  other_applied.values[
                      applied_other->parameters.offset].kind == KV_VALUE_NUMBER &&
                  nearly_equal(other_applied.values[
                      applied_other->parameters.offset].number_value, 4.25),
              "other-track value and stable identity");
        check(kv_edit_reset_memory(handle.value) != 0,
              "other-track parameter reset");
        baseline = {};
        check(kv_get_map_snapshot(handle.value, KV_MAP_SNAPSHOT_VERSION,
                                  &baseline, sizeof(baseline)) != 0,
              "other-track reset refreshes borrowed snapshot view");
        UpdateBatch other_distance_update(
            other_edit_id, map_string(baseline,
                baseline.source_files[
                    baseline.other_track_changes[0].metadata.source_file_index]
                    .source_hash),
            "25", "distance");
        KvEditReportSnapshot distance_report{};
        check(kv_edit_apply_to_memory_typed(
                  handle.value, &other_distance_update.batch, &distance_report,
                  sizeof(distance_report)) != 0 && distance_report.ok &&
                  distance_report.full_reparse_ok,
              "other-track distance apply-to-memory");
        KvMapSnapshot moved{};
        check(kv_get_map_snapshot(handle.value, KV_MAP_SNAPSHOT_VERSION,
                                  &moved, sizeof(moved)) != 0 &&
                  moved.other_track_change_count == 2 &&
                  nearly_equal(moved.other_track_changes[0].distance, 25.0),
              "other-track distance movement and committed-row mapping");
        check(kv_edit_reset_memory(handle.value) != 0,
              "other-track distance reset");
        baseline = {};
        check(kv_get_map_snapshot(handle.value, KV_MAP_SNAPSHOT_VERSION,
                                  &baseline, sizeof(baseline)) != 0,
              "other-track distance reset refreshes borrowed snapshot view");
    }

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
                station_edit_id, station_source_hash, "Edited//Literal", "stationName");
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
                      map_string(station_name_snapshot, edited_station->fields[1]) == "Edited//Literal",
                  "Station.List name value");
            const char* station_source_text =
                kv_get_source_text(handle.value, station_source_path.c_str());
            check(station_source_text &&
                      std::string_view(station_source_text).find(
                          "STA,\"Edited//Literal\",09:00,09:01,30,15,1,20,100,arr.wav,dep.wav,1,0,"
                          "legacy-a,legacy-b // keep station comment") !=
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
                                     reloaded_station->fields[1]) == "Edited//Literal",
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

    // Station.Put margin tolerance edit blocking and apply contract
    {
        TempFixture station_edit_fixture;
        {
            std::ofstream map(station_edit_fixture.map_path, std::ios::binary | std::ios::trunc);
            map << "BveTs Map 2.02:utf-8\n"
                << "0;\n"
                << "Station.Load('stations.csv');\n"
                << "100;\n"
                << "Station['STA'].Put(1, -2.5, 2.5);\n";
        }
        std::ofstream stations(station_edit_fixture.directory / "stations.csv",
                               std::ios::binary | std::ios::trunc);
        stations << "BveTs Station List 1.00:utf-8\n"
                 << "STA,Alpha,09:00,09:01,30,15,1,20,100,arr.wav,dep.wav,1,0\n";
        stations.close();

        MapHandle st_handle(kv_load_map_ex(
            station_edit_fixture.path_utf8().c_str(), 25.0,
            KV_LOAD_PREVIEW | KV_LOAD_EDIT_METADATA));
        check(st_handle.value != nullptr, "station edit map load");
        if (st_handle.value) {
            KvMapSnapshot st_snapshot{};
            check(kv_get_map_snapshot(st_handle.value, KV_MAP_SNAPSHOT_VERSION,
                                      &st_snapshot, sizeof(st_snapshot)) != 0,
                  "station edit map snapshot");
            check(st_snapshot.station_put_count == 1, "station put count 1");
            if (st_snapshot.station_put_count >= 1) {
                const KvStationPutRow& st_row = st_snapshot.station_puts[0];
                const std::string st_edit_id = map_string(st_snapshot, st_row.metadata.edit_id);
                const std::string st_hash = map_string(
                    st_snapshot,
                    st_snapshot.source_files[st_row.metadata.source_file_index].source_hash);

                // Update with invalid margin1 (>= 0)
                {
                    const std::string m_id = st_edit_id;
                    const std::string m_hash = st_hash;
                    const std::string m_name = "margin1";
                    const std::string m_val = "0";
                    const KvEditField fields[] = {
                        {utf8_view(m_name), utf8_view(m_val)},
                    };
                    const KvEditChange changes[] = {
                        {utf8_view(m_id), utf8_view(m_id), KV_EDIT_UPDATE, 0,
                         {0, 1},
                         utf8_view(""), utf8_view(""), utf8_view(""),
                         utf8_view(m_hash), utf8_view(""), utf8_view(""), utf8_view("")},
                    };
                    const KvEditBatch batch = {changes, 1, fields, 1};
                    KvEditReportSnapshot report{};
                    check(kv_edit_dry_run_typed(st_handle.value, &batch, &report, sizeof(report)) != 0,
                          "station margin1=0 update dry-run call");
                    check(!report.ok && report.blocking_error_count >= 1,
                          "station margin1=0 update dry-run blocked");
                    check(edit_report_has_error_containing(report, "margin1 must be a negative value"),
                          "station margin1=0 update reports margin1 error");
                }

                // Update with invalid margin2 (<= 0)
                {
                    const std::string m_id = st_edit_id;
                    const std::string m_hash = st_hash;
                    const std::string m_name = "margin2";
                    const std::string m_val = "-1";
                    const KvEditField fields[] = {
                        {utf8_view(m_name), utf8_view(m_val)},
                    };
                    const KvEditChange changes[] = {
                        {utf8_view(m_id), utf8_view(m_id), KV_EDIT_UPDATE, 0,
                         {0, 1},
                         utf8_view(""), utf8_view(""), utf8_view(""),
                         utf8_view(m_hash), utf8_view(""), utf8_view(""), utf8_view("")},
                    };
                    const KvEditBatch batch = {changes, 1, fields, 1};
                    KvEditReportSnapshot report{};
                    check(kv_edit_dry_run_typed(st_handle.value, &batch, &report, sizeof(report)) != 0,
                          "station margin2=-1 update dry-run call");
                    check(!report.ok && report.blocking_error_count >= 1,
                          "station margin2=-1 update dry-run blocked");
                    check(edit_report_has_error_containing(report, "margin2 must be a positive value"),
                          "station margin2=-1 update reports margin2 error");
                }

                // Valid update to margin1 = -3.5, margin2 = 4.0
                {
                    const std::string m_id = st_edit_id;
                    const std::string m_hash = st_hash;
                    const std::string m1_name = "margin1";
                    const std::string m1_val = "-3.5";
                    const std::string m2_name = "margin2";
                    const std::string m2_val = "4.0";
                    const KvEditField fields[] = {
                        {utf8_view(m1_name), utf8_view(m1_val)},
                        {utf8_view(m2_name), utf8_view(m2_val)},
                    };
                    const KvEditChange changes[] = {
                        {utf8_view(m_id), utf8_view(m_id), KV_EDIT_UPDATE, 0,
                         {0, 2},
                         utf8_view(""), utf8_view(""), utf8_view(""),
                         utf8_view(m_hash), utf8_view(""), utf8_view(""), utf8_view("")},
                    };
                    const KvEditBatch batch = {changes, 1, fields, 2};
                    KvEditReportSnapshot report{};
                    check(kv_edit_dry_run_typed(st_handle.value, &batch, &report, sizeof(report)) != 0,
                          "station valid margin update dry-run call");
                    check(report.ok && report.blocking_error_count == 0,
                          "station valid margin update dry-run succeeds");
                    KvEditReportSnapshot applied{};
                    check(kv_edit_apply_to_memory_typed(st_handle.value, &batch, &applied, sizeof(applied)) != 0,
                          "station valid margin apply-to-memory call");
                    check(applied.ok, "station valid margin apply succeeds");
                    check(kv_edit_reset_memory(st_handle.value) != 0,
                          "station valid margin update reset");
                }

                // Inserts must receive the same backend validation as updates.
                {
                    SimpleInsertBatch invalid_insert(
                        station_edit_fixture.path_utf8(), "station-margin-invalid-insert",
                        {{"rowKind", "station.put"}, {"distance", "50"},
                         {"stationKey", "STA"}, {"door", "1"},
                         {"margin1", "0"}, {"margin2", "4"}});
                    KvEditReportSnapshot report{};
                    check(kv_edit_dry_run_typed(
                              st_handle.value, &invalid_insert.batch, &report,
                              sizeof(report)) != 0,
                          "station invalid margin insert dry-run call");
                    check(!report.ok && report.blocking_error_count >= 1,
                          "station margin1=0 insert dry-run blocked");
                    check(edit_report_has_error_containing(
                              report, "margin1 must be a negative value"),
                          "station margin1=0 insert reports margin1 error");
                }
                {
                    constexpr const char* insert_id = "station-margin-valid-insert";
                    SimpleInsertBatch valid_insert(
                        station_edit_fixture.path_utf8(), insert_id,
                        {{"rowKind", "station.put"}, {"distance", "50"},
                         {"stationKey", "STA"}, {"door", "1"},
                         {"margin1", "-4"}, {"margin2", "4"}});
                    KvEditReportSnapshot dry_report{};
                    check(kv_edit_dry_run_typed(
                              st_handle.value, &valid_insert.batch, &dry_report,
                              sizeof(dry_report)) != 0,
                          "station valid margin insert dry-run call");
                    check(dry_report.ok && dry_report.insert_count == 1 &&
                              dry_report.full_reparse_ok &&
                              dry_report.non_target_changed_count == 0,
                          "station valid margin insert dry-run succeeds");
                    KvEditReportSnapshot applied{};
                    check(kv_edit_apply_to_memory_typed(
                              st_handle.value, &valid_insert.batch, &applied,
                              sizeof(applied)) != 0,
                          "station valid margin insert apply-to-memory call");
                    check(applied.ok && applied.insert_count == 1,
                          "station valid margin insert apply succeeds");
                    KvMapSnapshot inserted_snapshot{};
                    check(kv_get_map_snapshot(
                              st_handle.value, KV_MAP_SNAPSHOT_VERSION,
                              &inserted_snapshot, sizeof(inserted_snapshot)) != 0,
                          "station valid margin insert snapshot");
                    const bool inserted_row = inserted_snapshot.station_puts &&
                        std::any_of(
                            inserted_snapshot.station_puts,
                            inserted_snapshot.station_puts +
                                inserted_snapshot.station_put_count,
                            [&](const KvStationPutRow& row) {
                                return map_string(inserted_snapshot, row.metadata.edit_id) ==
                                           insert_id &&
                                    row.margin1.kind == KV_VALUE_NUMBER &&
                                    nearly_equal(row.margin1.number_value, -4.0) &&
                                    row.margin2.kind == KV_VALUE_NUMBER &&
                                    nearly_equal(row.margin2.number_value, 4.0);
                            });
                    check(inserted_row,
                          "station valid margin insert retains semantic values");
                    check(kv_edit_reset_memory(st_handle.value) != 0,
                          "station valid margin insert reset");
                }
            }
        }
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

void light_contract() {
    kv_set_log_callback(diagnostic_log_callback);
    TempFixture fixture;
    const std::filesystem::path child_path = fixture.directory / "light-child.txt";
    const auto write_map = [](const std::filesystem::path& path,
                              const std::string& text) {
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        output << text;
        return static_cast<bool>(output);
    };

    check(write_map(
              fixture.map_path,
              "BveTs Map 2.02:utf-8\n"
              "0;\n"
              "Light.Ambient(0.1, 0.2, 0.3);\n"
              "Include 'light-child.txt';\n"
              "Light.Direction(1.25, -0.5);\n"),
          "light valid root fixture write");
    check(write_map(
              child_path,
              "BveTs Map 2.02:utf-8\n"
              "Light.Diffuse(0.4, 0.5, 0.6);\n"),
          "light valid include fixture write");
    clear_diagnostics();
    {
        MapHandle handle(kv_load_map_ex(
            fixture.path_utf8().c_str(), 25.0, KV_LOAD_PREVIEW));
        check(handle.value != nullptr, "light valid preview load");
        if (handle.value) {
            KvMapSnapshot snapshot{};
            check(kv_get_map_snapshot(handle.value, KV_MAP_SNAPSHOT_VERSION,
                                      &snapshot, sizeof(snapshot)) != 0,
                  "light valid preview snapshot");
            validate_map(snapshot, false);
            check(snapshot.light_ambient_count == 1 &&
                      snapshot.light_diffuse_count == 1 &&
                      snapshot.light_direction_count == 1,
                  "light valid preview rows");
            if (snapshot.light_ambient_count == 1 &&
                snapshot.light_diffuse_count == 1 &&
                snapshot.light_direction_count == 1) {
                check(nearly_equal(snapshot.light_ambient[0].red, 0.1) &&
                          nearly_equal(snapshot.light_ambient[0].green, 0.2) &&
                          nearly_equal(snapshot.light_ambient[0].blue, 0.3),
                      "light ambient values");
                check(nearly_equal(snapshot.light_diffuse[0].red, 0.4) &&
                          nearly_equal(snapshot.light_diffuse[0].green, 0.5) &&
                          nearly_equal(snapshot.light_diffuse[0].blue, 0.6),
                      "light diffuse values");
                check(nearly_equal(snapshot.light_direction[0].pitch, 1.25) &&
                          nearly_equal(snapshot.light_direction[0].yaw, -0.5),
                      "light direction values");
            }
        }
    }
    check(!diagnostics_contain("Light.Ambient declarations are invalid"),
          "light valid emits no duplicate warning");
    {
        MapHandle handle(kv_load_map_ex(
            fixture.path_utf8().c_str(), 25.0,
            KV_LOAD_PREVIEW | KV_LOAD_EDIT_METADATA));
        check(handle.value != nullptr, "light valid edit-profile load");
        if (handle.value) {
            KvMapSnapshot snapshot{};
            check(kv_get_map_snapshot(handle.value, KV_MAP_SNAPSHOT_VERSION,
                                      &snapshot, sizeof(snapshot)) != 0,
                  "light valid edit-profile snapshot");
            validate_arrays(snapshot);
            check(snapshot.version == KV_MAP_SNAPSHOT_VERSION &&
                      (snapshot.capabilities & KV_MAP_CAP_EDIT_METADATA) != 0 &&
                      snapshot.light_ambient_count == 1 &&
                      snapshot.light_ambient[0].metadata.line == 3 &&
                      snapshot.light_ambient[0].metadata.source_file_index <
                          snapshot.source_file_count,
                  "light row source metadata");
            std::set<std::string> light_edit_kinds;
            for (std::uint64_t index = 0; index < snapshot.element_count; ++index) {
                const KvElementRow& element = snapshot.elements[index];
                const std::string kind = map_string(snapshot, element.row_kind);
                if (kind != "light.ambient" && kind != "light.diffuse" &&
                    kind != "light.direction") {
                    continue;
                }
                const std::string edit_id = map_string(snapshot, element.edit_id);
                KvEditTargetSnapshot target{};
                check(!edit_id.empty() &&
                          kv_get_edit_target_typed(handle.value, utf8_view(edit_id),
                                                   &target, sizeof(target)) != 0 &&
                          arena_view(target.string_data, target.string_size,
                                     target.row_kind) == kind &&
                          target.elements_for_statement == 1,
                      "light rows have editable targets");
                light_edit_kinds.insert(kind);
            }
            check(light_edit_kinds ==
                      std::set<std::string>{"light.ambient", "light.diffuse",
                                            "light.direction"},
                  "light rows have stable typed edit identities");
        }
    }

    check(write_map(
              fixture.map_path,
              "BveTs Map 2.02:utf-8\n"
              "0;\n"
              "Light.Ambient(0.1, 0.2, 0.3);\n"
              "Light.Ambient(0.4, 0.5, 0.6);\n"
              "Light.Diffuse(0.4, 0.5, 0.6);\n"
              "Light.Direction(1, 2);\n"),
          "light root duplicate fixture write");
    clear_diagnostics();
    {
        MapHandle handle(kv_load_map_ex(
            fixture.path_utf8().c_str(), 25.0, KV_LOAD_PREVIEW));
        check(handle.value != nullptr, "light root duplicate load");
        if (handle.value) {
            KvMapSnapshot snapshot{};
            check(kv_get_map_snapshot(handle.value, KV_MAP_SNAPSHOT_VERSION,
                                      &snapshot, sizeof(snapshot)) != 0,
                  "light root duplicate snapshot");
            check(snapshot.light_ambient_count == 0 &&
                      snapshot.light_diffuse_count == 1 &&
                      snapshot.light_direction_count == 1,
                  "light root duplicate invalidates only its statement family");
        }
    }
    check(diagnostics_contain(
              "Multiple Light.Ambient declarations are invalid in one logical map:"),
          "light root duplicate warning");
    check(diagnostics_contain("map.txt:3:1") && diagnostics_contain("map.txt:4:1"),
          "light root duplicate locations");

    check(write_map(
              fixture.map_path,
              "BveTs Map 2.02:utf-8\n"
              "0;\n"
              "Light.Diffuse(0.1, 0.2, 0.3);\n"
              "Include 'light-child.txt';\n"),
          "light include duplicate root fixture write");
    check(write_map(
              child_path,
              "BveTs Map 2.02:utf-8\n"
              "Light.Diffuse(0.4, 0.5, 0.6);\n"),
          "light include duplicate child fixture write");
    clear_diagnostics();
    {
        MapHandle handle(kv_load_map_ex(
            fixture.path_utf8().c_str(), 25.0, KV_LOAD_PREVIEW));
        check(handle.value != nullptr, "light include duplicate load");
        if (handle.value) {
            KvMapSnapshot snapshot{};
            check(kv_get_map_snapshot(handle.value, KV_MAP_SNAPSHOT_VERSION,
                                      &snapshot, sizeof(snapshot)) != 0,
                  "light include duplicate snapshot");
            check(snapshot.light_diffuse_count == 0,
                  "light include duplicate invalidates diffuse");
        }
    }
    check(diagnostics_contain(
              "Multiple Light.Diffuse declarations are invalid in one logical map:"),
          "light include duplicate warning");
    check(diagnostics_contain("map.txt:3:1") &&
              diagnostics_contain("light-child.txt:2:1"),
          "light include duplicate locations");

    check(write_map(
              fixture.map_path,
              "BveTs Map 2.02:utf-8\n"
              "0;\n"
              "Light.Ambient(-0.1, 0.2, 0.3);\n"),
          "light RGB range fixture write");
    clear_diagnostics();
    {
        MapHandle handle(kv_load_map_ex(
            fixture.path_utf8().c_str(), 25.0, KV_LOAD_PREVIEW));
        check(handle.value != nullptr, "light RGB range load");
        if (handle.value) {
            KvMapSnapshot snapshot{};
            check(kv_get_map_snapshot(handle.value, KV_MAP_SNAPSHOT_VERSION,
                                      &snapshot, sizeof(snapshot)) != 0,
                  "light RGB range snapshot");
            check(snapshot.light_ambient_count == 0,
                  "light RGB range invalidates ambient");
        }
    }
    check(diagnostics_contain("Light.Ambient RGB values must be between 0 and 1."),
          "light RGB range warning");

    check(write_map(
              fixture.map_path,
              "BveTs Map 2.02:utf-8\n"
              "0;\n"
              "10;\n"
              "Light.Direction(1, 2);\n"),
          "light direction distance fixture write");
    clear_diagnostics();
    {
        MapHandle handle(kv_load_map_ex(
            fixture.path_utf8().c_str(), 25.0, KV_LOAD_PREVIEW));
        check(handle.value != nullptr, "light direction distance load");
        if (handle.value) {
            KvMapSnapshot snapshot{};
            check(kv_get_map_snapshot(handle.value, KV_MAP_SNAPSHOT_VERSION,
                                      &snapshot, sizeof(snapshot)) != 0,
                  "light direction distance snapshot");
            check(snapshot.light_direction_count == 0,
                  "light direction distance invalidates direction");
        }
    }
    check(diagnostics_contain("Light.Direction must be declared at route distance 0."),
          "light direction distance warning");

    check(write_map(
              fixture.map_path,
              "BveTs Map 2.02:utf-8\n"
              "0;\n"
              "Light.Ambient(0.1, 0.2);\n"
              "Light.Ambient(0.1, 0.2, 0.3);\n"),
          "light basic syntax fixture write");
    clear_diagnostics();
    {
        MapHandle handle(kv_load_map_ex(
            fixture.path_utf8().c_str(), 25.0, KV_LOAD_PREVIEW));
        check(handle.value != nullptr, "light basic syntax load");
        if (handle.value) {
            KvMapSnapshot snapshot{};
            check(kv_get_map_snapshot(handle.value, KV_MAP_SNAPSHOT_VERSION,
                                      &snapshot, sizeof(snapshot)) != 0,
                  "light basic syntax snapshot");
            check(snapshot.light_ambient_count == 1,
                  "light invalid syntax does not join duplicate grouping");
        }
    }
    check(diagnostics_contain("Parameter count error"), "light basic syntax warning");
    check(!diagnostics_contain("Multiple Light.Ambient declarations"),
          "light invalid syntax avoids duplicate warning");

    // Station.Put margin tolerance diagnostics contract
    {
        TempFixture station_margin_fixture;
        const auto write_station_map = [](const std::filesystem::path& path,
                                          const std::string& text) {
            std::ofstream output(path, std::ios::binary | std::ios::trunc);
            output << text;
            return static_cast<bool>(output);
        };
        check(write_station_map(
                  station_margin_fixture.map_path,
                  "BveTs Map 2.02:utf-8\n"
                  "0;\n"
                  "Station.Load('stations.csv');\n"
                  "Station['STA'].Put(1, -2.5, 2.5);\n"
                  "50;\n"
                  "Station['STA'].Put(1);\n"),
              "station margin valid fixture write");
        std::ofstream stations(station_margin_fixture.directory / "stations.csv",
                               std::ios::binary | std::ios::trunc);
        stations << "BveTs Station List 1.00:utf-8\n"
                 << "STA,Alpha,09:00,09:01,30,15,1,20,100,arr.wav,dep.wav,1,0\n";
        stations.close();

        clear_diagnostics();
        {
            MapHandle handle(kv_load_map_ex(
                station_margin_fixture.path_utf8().c_str(), 25.0, KV_LOAD_PREVIEW));
            check(handle.value != nullptr, "station margin valid load");
        }
        check(!diagnostics_contain("Station.Put margin1 (rearward tolerance) must be a negative value"),
              "station margin valid emits no margin1 warning");
        check(!diagnostics_contain("Station.Put margin2 (forward tolerance) must be a positive value"),
              "station margin valid emits no margin2 warning");

        // Invalid margin1 >= 0 and invalid margin2 <= 0
        check(write_station_map(
                  station_margin_fixture.map_path,
                  "BveTs Map 2.02:utf-8\n"
                  "0;\n"
                  "Station.Load('stations.csv');\n"
                  "Station['STA'].Put(1, 0, 0);\n"
                  "100;\n"
                  "Station['STA'].Put(1, 2.5, -1.5);\n"),
              "station margin invalid fixture write");
        clear_diagnostics();
        {
            MapHandle handle(kv_load_map_ex(
                station_margin_fixture.path_utf8().c_str(), 25.0, KV_LOAD_PREVIEW));
            check(handle.value != nullptr, "station margin invalid load succeeds (nonfatal)");
        }
        check(diagnostics_contain("Station.Put margin1 (rearward tolerance) must be a negative value (got 0)."),
              "station margin1=0 emits warning");
        check(diagnostics_contain("Station.Put margin2 (forward tolerance) must be a positive value (got 0)."),
              "station margin2=0 emits warning");
        check(diagnostics_contain("Station.Put margin1 (rearward tolerance) must be a negative value (got 2.5)."),
              "station margin1=2.5 emits warning");
        check(diagnostics_contain("Station.Put margin2 (forward tolerance) must be a positive value (got -1.5)."),
              "station margin2=-1.5 emits warning");
        check(diagnostics_contain("map.txt:4:") && diagnostics_contain("map.txt:6:"),
              "station margin warnings report file and line locations");
    }

    kv_set_log_callback(nullptr);
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
    check(diagnostics_contain("[WARN]maploader_parser.cpp:"),
          "map diagnostics identify maploader_parser.cpp");
    check(diagnostics_contain("[INFO]maploader_parser.cpp: loading map "),
          "map parser progress identifies maploader_parser.cpp");
    check(diagnostics_contain("[INFO]maploader_geometry.cpp: calculating track geometry"),
          "geometry progress identifies maploader_geometry.cpp");
    check(diagnostics_contain("[INFO]maploader_core.cpp: load timing:"),
          "load timing identifies maploader_core.cpp");
    check(diagnostics_contain("[INFO]maploader.cpp: "),
          "C ABI load completion identifies maploader.cpp");

    TempFixture invalid_cant_cant;
    {
        std::ofstream map(invalid_cant_cant.map_path,
                          std::ios::binary | std::ios::trunc);
        map << "BveTs Map 2.02:utf-8\n"
            << "0;\n"
            << "Track['1'].Cant.Cant(10);\n";
    }
    clear_diagnostics();
    MapHandle invalid_cant_handle(kv_load_map_ex(
        invalid_cant_cant.path_utf8().c_str(), 25.0, KV_LOAD_PREVIEW));
    check(invalid_cant_handle.value != nullptr,
          "invalid Cant.Cant remains a nonfatal unsupported statement");
    check(diagnostics_contain("Unknown submethod") &&
              diagnostics_contain("track.cant.cant"),
          "invalid Track[].Cant.Cant is diagnosed instead of ignored");

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

    TempFixture skipped_includes;
    {
        std::ofstream invalid_child(skipped_includes.directory / "invalid-include.txt",
                                    std::ios::binary | std::ios::trunc);
        invalid_child << "This is not a BVE map.\n0;\n";
        std::ofstream map(skipped_includes.map_path, std::ios::binary | std::ios::trunc);
        map << "BveTs Map 2.02:utf-8\n"
            << "Include 'missing-include.txt';\n"
            << "Include 'invalid-include.txt';\n"
            << "0;\n"
            << "SpeedLimit.Begin(80);\n";
    }
    clear_diagnostics();
    MapHandle skipped_include_handle(kv_load_map_ex(
        skipped_includes.path_utf8().c_str(), 25.0,
        KV_LOAD_PREVIEW | KV_LOAD_EDIT_METADATA));
    check(skipped_include_handle.value != nullptr,
          "missing and invalid Includes do not reject the entry map");
    check(diagnostics_contain("[WARN]"), "skipped Includes use warning severity");
    check(!diagnostics_contain("[ERROR]"), "skipped Includes do not use error severity");
    check(diagnostics_contain("Include load skipped: missing-include.txt"),
          "missing Include warning identifies the skipped reference");
    check(diagnostics_contain("Include load skipped: invalid-include.txt"),
          "invalid Include warning identifies the skipped reference");
    if (skipped_include_handle.value) {
        KvMapSnapshot snapshot{};
        check(kv_get_map_snapshot(skipped_include_handle.value, KV_MAP_SNAPSHOT_VERSION,
                                  &snapshot, sizeof(snapshot)) != 0,
              "skipped Include snapshot");
        check(snapshot.speed_limit_count == 1,
              "valid statement after skipped Includes reaches IR");
        check(snapshot.file_structure_count == 3,
              "file structure retains skipped Include nodes");
        check(snapshot.source_file_count == 1,
              "skipped Include files are not loaded source files");
        std::uint64_t skipped_node_count = 0;
        for (std::uint64_t i = 0; i < snapshot.file_structure_count; ++i) {
            const KvFileStructureRow& node = snapshot.file_structure[i];
            const std::string include_path = map_string(snapshot, node.include_path);
            if (include_path != "missing-include.txt" &&
                include_path != "invalid-include.txt") {
                continue;
            }
            ++skipped_node_count;
            const std::string node_path = map_string(snapshot, node.absolute_path);
            bool source_file_found = false;
            for (std::uint64_t source_index = 0;
                 source_index < snapshot.source_file_count; ++source_index) {
                source_file_found = source_file_found ||
                    map_string(snapshot, snapshot.source_files[source_index].file_path) ==
                        node_path;
            }
            check(!source_file_found,
                  "skipped Include node is absent from loaded source files");
        }
        check(skipped_node_count == 2,
              "file structure retains both skipped Include nodes");
    }

    struct FatalCase {
        const char* file;
        const char* error_text;
    };
    const std::array<FatalCase, 1> fatal_cases{{
        {"map_error_invalid_header.txt", "Invalid file header"},
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
        std::cerr << "usage: typed_snapshot_tests "
                     "<snapshot|geometry|edit|diagnostics|signal-glare> "
                     "[fixture-root|map-path] [--commit]\n";
        return 2;
    }
    const std::string mode = argv[1];
    if (mode == "snapshot") {
        snapshot_contract();
        light_contract();
        return failures == 0 ? 0 : 1;
    }
    if (mode == "geometry") return geometry_projection_contract() == 0 ? 0 : 1;
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
