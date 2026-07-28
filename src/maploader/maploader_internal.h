/*
 * Copyright (c) 2026 Sapporo_ningyo
 *
 * Portions of the map parsing and track-geometry design are derived from or
 * reimplemented with reference to kobushi-trackviewer, Copyright (c) 2021-2024
 * konawasabi, licensed under Apache License 2.0.
 *
 * Licensed under Apache License 2.0; see LICENSE and NOTICE.
 */

#pragma once

#include "maploader.h"

#include "diagnostics.h"
#include "text_decoder.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <future>
#include <iomanip>
#include <iostream>
#include <initializer_list>
#include <iterator>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <random>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace kme::maploader::detail {

#if !defined(_WIN32)
constexpr unsigned int CP_UTF8 = 65001;
#endif

using SteadyClock = std::chrono::steady_clock;

constexpr double k_inf = std::numeric_limits<double>::infinity();
constexpr double k_pi = 3.141592653589793238462643383279502884;
struct LoadTiming {
    double read_decode_seconds = 0.0;
    double parse_seconds = 0.0;
    double relocate_seconds = 0.0;
    double owntrack_seconds = 0.0;
    double snapshot_seconds = 0.0;
    std::vector<std::pair<std::string, double>> othertrack_seconds;
};

double elapsed_seconds_since(SteadyClock::time_point started_at);
class ScopedTimer {
public:
    explicit ScopedTimer(double* target)
        : target_(target), started_at_(SteadyClock::now()) {}

    ~ScopedTimer() {
        if (target_) *target_ += elapsed_seconds_since(started_at_);
    }

private:
    double* target_ = nullptr;
    SteadyClock::time_point started_at_;
};

class ActiveTimingScope {
public:
    explicit ActiveTimingScope(LoadTiming& timing);
    ~ActiveTimingScope();

private:
    LoadTiming* previous_ = nullptr;
};

std::string ascii_lower(std::string s);
std::string trim_copy(const std::string& s);
std::string trim_field_copy(const std::string& s);
std::string canonical_number(double value);
std::uint64_t stable_hash64(const std::string& text);
std::string hex64(std::uint64_t value);
double parse_first_version(const std::string& header);
std::string declared_encoding_from_header(const std::string& header);
std::vector<size_t> build_line_starts(const std::string& text);
struct LoadedText {
    std::string body;
    std::filesystem::path path;
    std::filesystem::path root;
    std::string normalized_path;
    std::string normalized_key;
    std::string encoding;
    std::string newline;
    std::string source_hash;
    std::vector<size_t> line_starts;
    size_t byte_length = 0;
    size_t body_offset = 0;
    int body_start_line = 1;
};

struct MapParseOptions {
    bool collect_edit_metadata = true;
};

struct SourceTextOverride {
    std::string file_path;
    std::string source_key;
    std::string text;
    std::string encoding;
    std::string newline;
    std::string base_hash;
    std::string current_hash;
    size_t byte_length = 0;
    bool utf8_bom = false;
    bool dirty = false;
};
using SourceTextOverrides = std::map<std::string, SourceTextOverride>;

std::string detect_newline(const std::string& text);
LoadedText make_loaded_header_text(const std::filesystem::path& path,
                                   std::string text,
                                   std::string encoding,
                                   std::string newline,
                                   std::string source_hash,
                                   size_t byte_length,
                                   const std::string& head_str,
                                   double min_version,
                                   bool collect_source_metadata);
LoadedText load_header_text(const std::filesystem::path& path,
                            const std::string& head_str,
                            double min_version,
                            const SourceTextOverrides* overrides = nullptr,
                            bool collect_source_metadata = true);
std::filesystem::path join_path(const std::filesystem::path& root, const std::string& file);
enum class ValueKind {
    Null,
    Number,
    String,
    ContinueValue,
};

struct Value {
    ValueKind kind = ValueKind::Null;
    double number = 0.0;
    std::string text;

    static Value null() { return {}; }
    static Value cont() { Value v; v.kind = ValueKind::ContinueValue; return v; }
    static Value num(double d) { Value v; v.kind = ValueKind::Number; v.number = d; return v; }
    static Value str(std::string s) { Value v; v.kind = ValueKind::String; v.text = std::move(s); return v; }

    bool is_null() const { return kind == ValueKind::Null; }
    bool is_number() const { return kind == ValueKind::Number; }
    bool is_string() const { return kind == ValueKind::String; }
    bool is_continue() const { return kind == ValueKind::ContinueValue; }
};

using VariableEnvironment = std::unordered_map<std::string, Value>;
using VariableEnvironmentSnapshot = std::shared_ptr<const VariableEnvironment>;

double as_number(const Value& value, double fallback = 0.0);
std::string as_text(const Value& value);
std::string key_text(const Value& value);
std::string track_key_display_text(const Value& value);
Value track_key_from_display_text(const std::string& text);
const Value& arg_or_null(const std::vector<Value>& values, size_t index = 0);
std::vector<std::string> parse_comma_separated_fields(const std::string& line, bool stop_on_inline_hash);
void trim_trailing_empty_fields(std::vector<std::string>& fields);
std::string strip_ini_comment_copy(const std::string& line);
std::string trim_matching_quotes(std::string text);
int parse_sound_buffer_count(const std::string& text);

constexpr size_t k_no_source_ref = std::numeric_limits<size_t>::max();
inline constexpr const char* k_root_include_invocation_key = "root";
struct SourceFileRecord {
    std::string file_path;
    std::string source_key;
    std::string display_path;
    std::string encoding;
    std::string newline;
    std::string source_hash;
    size_t byte_length = 0;
};

struct FileStructureRecord {
    size_t parent_index = k_no_source_ref;
    std::string include_path;
    std::string absolute_path;
};

struct SourceSpan {
    size_t source_file_index = k_no_source_ref;
    size_t include_stack_index = k_no_source_ref;
    size_t include_invocation_index = k_no_source_ref;
    size_t byte_start = 0;
    size_t byte_end = 0;
    int line = 1;
    int column = 1;
    int line_end = 1;
    int column_end = 1;
};

struct ParsedStatement {
    std::string edit_id;
    std::string statement_kind;
    SourceSpan source;
    std::string raw_text;
    std::string raw_text_preview;
    std::string raw_arguments;
    std::vector<Value> evaluated_values;
    std::shared_ptr<const VariableEnvironment> variable_environment;
    std::string distance_expression;
    double distance_value = 0.0;
    int global_order = 0;
};

struct MapElementRef {
    std::string edit_id;
    std::string row_kind;
    size_t row_index = 0;
    std::string source_file_path;
    int global_order = 0;
};

struct EditSourceRef {
    size_t statement_index = k_no_source_ref;
    int element_index = 0;

    bool valid() const {
        return statement_index != k_no_source_ref;
    }
};

enum class MapDiagnosticKind {
    Warning,
};

struct MapDiagnostic {
    MapDiagnosticKind kind = MapDiagnosticKind::Warning;
    std::string file_path;
    int line = 1;
    int column = 1;
    std::string statement_kind;
    std::string message;
};

enum class DeferredKeyKind {
    Structure,
    Sound,
    Sound3D,
    Station,
    SignalAspect,
    Train,
};

struct DeferredKeyReference {
    DeferredKeyKind kind = DeferredKeyKind::Structure;
    std::string key;
    std::string display_key;
    MapDiagnostic source;
};

enum class TransitionEventKind {
    CurveBeginTransition,
    CurveBegin,
    CurveBeginCircular,
    CurveEnd,
    GradientBeginTransition,
    GradientBegin,
    GradientEnd,
};

struct TransitionEvent {
    TransitionEventKind kind = TransitionEventKind::CurveBeginTransition;
    int argument_count = 0;
    MapDiagnostic source;
};

struct OwnTrackEvent {
    double distance = 0.0;
    std::string key;
    Value value;
    std::string flag;
    EditSourceRef edit_ref;
};

struct OtherTrackEvent {
    double distance = 0.0;
    std::string track_key;
    std::string key;
    Value value;
    std::string flag;
    EditSourceRef edit_ref;
};

struct StructureLoad {
    double distance = 0.0;
    std::string method;
    Value load_file_path;
    std::string file_path;
    int order = 0;
    EditSourceRef edit_ref;
};

struct StructureModel {
    std::string structure_key;
    std::string file_path;
    EditSourceRef edit_ref;
};

struct SoundListEntry {
    std::string sound_key;
    std::string file_path;
    int buffer_count = 1;
    bool is_3d = false;
    EditSourceRef edit_ref;
};

struct OtherTrainDefinition {
    double distance = 0.0;
    std::string method;
    Value train_key;
    Value load_file_path;
    std::string resolved_file_path;
    Value track_key;
    Value direction;
    std::string source_file_path;
    int order = 0;
    EditSourceRef edit_ref;
};

struct OtherTrainEnable {
    double distance = 0.0;
    Value train_key;
    Value time;
    std::string file_path;
    int order = 0;
    EditSourceRef edit_ref;
};

struct OtherTrainStop {
    double distance = 0.0;
    Value train_key;
    Value decelerate;
    Value stop_time;
    Value accelerate;
    Value speed;
    std::string file_path;
    int order = 0;
    EditSourceRef edit_ref;
};

struct OtherTrainReferencedKey {
    std::string key;
    std::string file_path;
    EditSourceRef edit_ref;
};

struct MapSoundPlay {
    double distance = 0.0;
    Value sound_key;
    std::string file_path;
    int order = 0;
    EditSourceRef edit_ref;
};

struct MapSound3DPut {
    double distance = 0.0;
    Value sound_key;
    double x = 0.0;
    double y = 0.0;
    std::string file_path;
    int order = 0;
    EditSourceRef edit_ref;
};

struct SectionBegin {
    double distance = 0.0;
    std::vector<Value> signal_indices;
    std::string file_path;
    int order = 0;
    EditSourceRef edit_ref;
};

struct SectionSpeedLimit {
    double distance = 0.0;
    std::vector<Value> speeds;
    std::string file_path;
    int order = 0;
    EditSourceRef edit_ref;
};

struct SignalAspect {
    std::string signal_aspect_key;
    std::vector<std::string> structure_keys;
    EditSourceRef edit_ref;
};

struct SignalPut {
    double distance = 0.0;
    Value signal_aspect_key;
    Value section;
    Value track_key;
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    double rx = 0.0;
    double ry = 0.0;
    double rz = 0.0;
    double tilt = 0.0;
    double span = 0.0;
    std::string file_path;
    int order = 0;
    EditSourceRef edit_ref;
};

struct BeaconPut {
    double distance = 0.0;
    Value type;
    Value section;
    Value send_data;
    std::string file_path;
    int order = 0;
    EditSourceRef edit_ref;
};

struct PreTrainPass {
    double distance = 0.0;
    Value pass_time;
    std::string file_path;
    int order = 0;
    EditSourceRef edit_ref;
};

struct RollingNoiseChange {
    double distance = 0.0;
    Value index;
    std::string file_path;
    int order = 0;
    EditSourceRef edit_ref;
};

struct FlangeNoiseChange {
    double distance = 0.0;
    Value index;
    std::string file_path;
    int order = 0;
    EditSourceRef edit_ref;
};

struct JointNoisePlay {
    double distance = 0.0;
    Value index;
    std::string file_path;
    int order = 0;
    EditSourceRef edit_ref;
};

struct StructurePut {
    double distance = 0.0;
    std::string method;
    Value structure_key;
    Value track_key;
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    double rx = 0.0;
    double ry = 0.0;
    double rz = 0.0;
    double tilt = 0.0;
    double span = 0.0;
    Value track_key1;
    Value track_key2;
    double flag = 0.0;
    std::string file_path;
    int order = 0;
    EditSourceRef edit_ref;
};

struct RepeaterEvent {
    double distance = 0.0;
    std::string method;
    Value repeater_key;
    Value track_key;
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    double rx = 0.0;
    double ry = 0.0;
    double rz = 0.0;
    double tilt = 0.0;
    double span = 0.0;
    double interval = 0.0;
    std::vector<Value> structure_keys;
    std::string file_path;
    int order = 0;
    EditSourceRef edit_ref;
};

struct IrregularityChange {
    double distance = 0.0;
    double x = 0.0;
    double y = 0.0;
    double r = 0.0;
    double lx = 0.0;
    double ly = 0.0;
    double lr = 0.0;
    std::string file_path;
    int order = 0;
    EditSourceRef edit_ref;
};

struct BackgroundChange {
    double distance = 0.0;
    Value structure_key;
    std::string file_path;
    int order = 0;
    EditSourceRef edit_ref;
};

struct AdhesionChange {
    double distance = 0.0;
    Value a;
    Value b;
    Value c;
    std::string file_path;
    int order = 0;
    EditSourceRef edit_ref;
};

struct CabIlluminanceChange {
    double distance = 0.0;
    Value value;
    std::string file_path;
    int order = 0;
    EditSourceRef edit_ref;
};

struct FogChange {
    double distance = 0.0;
    Value density;
    Value red;
    Value green;
    Value blue;
    std::string file_path;
    int order = 0;
    EditSourceRef edit_ref;
};

struct DrawDistanceChange {
    double distance = 0.0;
    double value = 0.0;
    std::string file_path;
    int order = 0;
    EditSourceRef edit_ref;
};

struct SpeedLimitEvent {
    double distance = 0.0;
    Value speed;
    std::string file_path;
    int order = 0;
    EditSourceRef edit_ref;
};

struct StationPut {
    double distance = 0.0;
    Value station_key;
    Value door;
    Value margin1;
    Value margin2;
    std::string file_path;
    int order = 0;
    EditSourceRef edit_ref;
};

struct StationListEntry {
    std::array<std::string, 13> fields{};
    int order = 0;
    EditSourceRef edit_ref;
};

inline constexpr std::array<const char*, 13> k_station_list_field_names = {
    "stationKey", "stationName", "arrivalTime", "depertureTime", "stoppageTime",
    "defaultTime", "signalFlag", "alightingTime", "passengers", "arrivalSoundKey",
    "depertureSoundKey", "doorReopen", "stuckInDoor"
};

inline std::string normalized_station_list_edit_value(const std::string& input,
                                                      size_t field_index) {
    std::string value = trim_field_copy(input);
    if (field_index == 0 && value.empty()) {
        throw std::runtime_error("required edit field is empty: stationKey");
    }
    if (value.find_first_of("\r\n") != std::string::npos) {
        throw std::runtime_error(
            std::string("Station.List field cannot contain a line break: ") +
            k_station_list_field_names[field_index]);
    }
    return value;
}

struct Matrix {
    std::vector<double> data;
    size_t rows = 0;
    size_t cols = 0;

    void clear(size_t c) {
        data.clear();
        rows = 0;
        cols = c;
    }

    void reserve_rows(size_t row_count) {
        if (cols != 0) data.reserve(row_count * cols);
    }

    void push(const std::vector<double>& row) {
        if (cols == 0) cols = row.size();
        data.insert(data.end(), row.begin(), row.end());
        ++rows;
    }

    void push(std::initializer_list<double> row) {
        if (cols == 0) cols = row.size();
        data.insert(data.end(), row.begin(), row.end());
        ++rows;
    }
};

struct MapSnapshotStorage {
    KvMapSnapshot view{};
    std::string string_arena;
    std::vector<KvValue> values;
    std::vector<KvStringRef> string_refs;
    std::vector<KvFileStructureRow> file_structure;
    std::vector<KvSourceFileRow> source_files;
    std::vector<KvOtherTrackRow> other_tracks;
    std::vector<KvTrackEventRow> own_track_events;
    std::vector<KvTrackEventRow> other_track_events;
    std::vector<KvStationPositionRow> station_positions;
    std::vector<KvStationNameRow> station_names;
    std::vector<KvStationPutRow> station_puts;
    std::vector<KvStationListRow> station_list;
    std::vector<KvStructureLoadRow> structure_loads;
    std::vector<KvStructurePutRow> structure_puts;
    std::vector<KvStructureBetweenRow> structure_betweens;
    std::vector<KvStructureModelRow> structure_models;
    std::vector<KvOtherTrainDefinitionRow> other_train_definitions;
    std::vector<KvReferencedKeyRow> other_train_structure_keys;
    std::vector<KvReferencedKeyRow> other_train_sound_3d_keys;
    std::vector<KvOtherTrainEnableRow> other_train_enables;
    std::vector<KvOtherTrainStopRow> other_train_stops;
    std::vector<KvSectionRow> section_begins;
    std::vector<KvSectionRow> section_speed_limits;
    std::vector<KvSignalAspectRow> signal_aspects;
    std::vector<KvSignalPutRow> signal_puts;
    std::vector<KvBeaconRow> beacons;
    std::vector<KvPreTrainRow> pretrains;
    std::vector<KvSoundListRow> sound_list;
    std::vector<KvMapSoundRow> map_sounds;
    std::vector<KvMapSound3DRow> map_sounds_3d;
    std::vector<KvNoiseRow> rolling_noises;
    std::vector<KvNoiseRow> flange_noises;
    std::vector<KvNoiseRow> joint_noises;
    std::vector<KvRepeaterRow> repeaters;
    std::vector<KvIrregularityRow> irregularities;
    std::vector<KvBackgroundRow> backgrounds;
    std::vector<KvAdhesionRow> adhesions;
    std::vector<KvCabIlluminanceRow> cab_illuminance;
    std::vector<KvFogRow> fogs;
    std::vector<KvDrawDistanceRow> draw_distances;
    std::vector<KvSpeedLimitRow> speed_limits;
    std::vector<KvStatementRow> statements;
    std::vector<KvElementRow> elements;
};

struct SceneGeometrySnapshotStorage {
    KvSceneGeometrySnapshot view{};
    std::string string_arena;
    std::vector<KvSceneTrackRow> other_tracks;
};

struct EditTargetSnapshotStorage {
    KvEditTargetSnapshot view{};
    std::string string_arena;
    std::vector<KvStringRef> string_refs;
};

struct EditReportSnapshotStorage {
    KvEditReportSnapshot view{};
    std::string string_arena;
    std::vector<KvStringRef> string_refs;
    std::vector<KvDistanceBoundaryRow> boundaries;
    std::vector<KvStringRef> changed_files;
    std::vector<KvEditCommittedFileRow> committed_files;
    std::vector<KvEditCommittedRow> committed_rows;
    std::vector<KvStringRef> warnings;
    std::vector<KvStringRef> blocking_errors;
    std::vector<KvDistanceResolutionRow> resolution_requests;
    std::vector<KvEditPreviewRow> previews;
};

struct MapContext {
    std::filesystem::path rootpath;
    std::string rootpath_utf8;
    std::string entry_file_path;
    std::string current_file_path;
    std::vector<std::string> include_stack;
    std::string current_include_invocation_key;
    size_t current_include_invocation_index = k_no_source_ref;
    SourceTextOverrides source_overrides;
    double unit_distance = 25.0;
    double distance = 0.0;
    std::string distance_expression;
    int parse_order = 0;
    int edit_order = 0;
    VariableEnvironment variables;
    VariableEnvironmentSnapshot variable_environment_snapshot;
    std::set<std::string> external_variable_reads;
    std::set<std::string> variable_writes;
    bool depends_on_initial_distance = false;
    bool has_distance_assignment = false;
    std::vector<double> controlpoints;
    std::vector<OwnTrackEvent> own_track;
    std::map<double, std::string> station_position;
    std::map<std::string, std::string> station_key;
    std::vector<StationPut> station_puts;
    std::map<std::string, StationListEntry> station_list;
    std::map<std::string, std::vector<OtherTrackEvent>> othertrack;
    std::vector<std::string> othertrack_order;
    std::map<std::string, std::pair<double, double>> othertrack_range;
    std::vector<StructureLoad> structure_loads;
    std::vector<StructureModel> structure_models;
    std::vector<SoundListEntry> sound_list;
    std::vector<OtherTrainDefinition> other_trains;
    std::vector<OtherTrainEnable> other_train_enables;
    std::vector<OtherTrainStop> other_train_stops;
    std::vector<OtherTrainReferencedKey> other_train_structure_keys;
    std::vector<OtherTrainReferencedKey> other_train_sound_3d_keys;
    std::set<std::string> parsed_other_train_files;
    std::vector<MapSoundPlay> map_sounds;
    std::vector<MapSound3DPut> map_sound_3d;
    std::vector<RollingNoiseChange> rolling_noises;
    std::vector<FlangeNoiseChange> flange_noises;
    std::vector<JointNoisePlay> joint_noises;
    std::vector<StructurePut> structure_puts;
    std::vector<StructurePut> structure_betweens;
    std::vector<RepeaterEvent> repeaters;
    std::vector<SectionBegin> section_begins;
    std::vector<SectionSpeedLimit> section_speed_limits;
    std::vector<SignalAspect> signal_aspects;
    std::vector<SignalPut> signal_puts;
    std::vector<BeaconPut> beacons;
    std::vector<PreTrainPass> pretrains;
    std::vector<IrregularityChange> irregularities;
    std::vector<BackgroundChange> backgrounds;
    std::vector<AdhesionChange> adhesions;
    std::vector<CabIlluminanceChange> cab_illuminance;
    std::vector<FogChange> fogs;
    std::vector<DrawDistanceChange> draw_distances;
    std::vector<SpeedLimitEvent> speedlimits;
    Matrix owntrack_buffer;
    Matrix curveradius_buffer;
    Matrix structure_put_buffer;
    std::map<std::string, Matrix> othertrack_buffers;
    Matrix scene_owntrack_buffer;
    std::map<std::string, Matrix> scene_othertrack_buffers;
    std::array<double, 5> scene_geometry_parameters{0.0, 0.0, 0.0, 0.0, 0.0};
    bool scene_geometry_valid = false;
    std::array<double, 3> cp_arbdistribution{0.0, 0.0, 0.0};
    std::array<double, 3> cp_arbdistribution_default{0.0, 0.0, 0.0};
    std::array<double, 2> cp_defaultrange{0.0, 0.0};
    bool has_cp_arbdistribution = false;
    bool cp_arbdistribution_explicit = false;
    std::unique_ptr<MapSnapshotStorage> map_snapshot;
    std::unique_ptr<SceneGeometrySnapshotStorage> scene_snapshot;
    std::unique_ptr<EditTargetSnapshotStorage> edit_target_snapshot;
    std::unique_ptr<EditReportSnapshotStorage> edit_report_snapshot;
    std::uint64_t content_revision = 1;
    std::uint64_t geometry_revision = 1;
    std::uint64_t scene_revision = 1;
    std::uint64_t edit_report_revision = 0;
    LoadTiming timing;
    bool load_timing_logged = false;
    MapParseOptions parse_options;
    std::vector<FileStructureRecord> file_structure;
    std::vector<SourceFileRecord> source_files;
    std::unordered_map<std::string, size_t> source_file_indices;
    std::vector<std::vector<std::string>> include_stacks;
    std::unordered_map<std::string, size_t> include_stack_indices;
    std::vector<std::string> include_invocation_keys;
    std::unordered_map<std::string, size_t> include_invocation_indices;
    // editIds are derived from global_order, which changes when statements move.
    // These maps keep element identities stable for the lifetime of one loaded
    // editing session while retaining the native IDs needed after a disk reset.
    std::unordered_map<std::string, std::string> native_element_edit_id_to_stable;
    std::unordered_map<std::string, std::string> disk_native_element_edit_id_to_stable;
    std::unordered_map<std::string, std::string> disk_source_hashes_for_stable_ids;
    mutable std::unordered_map<std::string, std::string> element_edit_id_cache;
    std::vector<ParsedStatement> parsed_statements;
    std::vector<MapDiagnostic> diagnostics;
    std::vector<DeferredKeyReference> deferred_key_references;
    std::vector<TransitionEvent> transition_events;
    size_t active_statement_index = k_no_source_ref;
    int active_statement_next_element_index = 0;
    std::string edit_validation_fingerprint;
    bool edit_validation_current = false;

    int next_parse_order() {
        return ++parse_order;
    }

    int next_edit_order() {
        return ++edit_order;
    }
};

VariableEnvironmentSnapshot current_variable_environment_snapshot(MapContext& ctx);
void rebuild_variable_environment_snapshot(MapContext& ctx);

LoadedText load_header_text(const MapContext& ctx,
                            const std::filesystem::path& path,
                            const std::string& head_str,
                            double min_version);
std::string normalized_source_path(const std::filesystem::path& path);
std::string normalized_source_key(std::string path);
std::string current_source_text(const MapContext& ctx, const std::string& file_path);
size_t register_source_file_index(MapContext& ctx, const LoadedText& loaded);
void register_source_file(MapContext& ctx, const LoadedText& loaded);
std::string include_stack_key(const std::vector<std::string>& stack);
size_t intern_include_stack(MapContext& ctx, const std::vector<std::string>& stack);
std::string make_include_invocation_key(const std::string& parent_key,
                                        const std::string& source_key,
                                        size_t byte_start,
                                        size_t byte_end,
                                        size_t occurrence);
size_t intern_include_invocation_key(MapContext& ctx, const std::string& key);
const SourceFileRecord* source_file_record(const MapContext& ctx, const SourceSpan& span);
const std::string& source_file_path(const MapContext& ctx, const SourceSpan& span);
const std::string& source_file_key(const MapContext& ctx, const SourceSpan& span);
const std::string& source_file_encoding(const MapContext& ctx, const SourceSpan& span);
const std::string& source_file_newline(const MapContext& ctx, const SourceSpan& span);
const std::vector<std::string>& source_include_stack(const MapContext& ctx, const SourceSpan& span);
const std::string& source_include_invocation_key(const MapContext& ctx, const SourceSpan& span);
int utf8_column_count(const std::string& text, size_t begin, size_t end);
std::pair<int, int> line_column_for_body_pos(const LoadedText& loaded, size_t body_pos);
SourceSpan make_source_span(MapContext& ctx,
                            const LoadedText& loaded,
                            size_t body_start,
                            size_t body_end,
                            const std::vector<std::string>& include_stack);
std::vector<std::string> include_stack_for_file(const MapContext& ctx, const std::filesystem::path& path);
std::string raw_text_preview(std::string text);
std::string make_edit_id(const std::string& source_key, int global_order,
                         const std::string& kind, int element_index);
size_t add_parsed_statement(MapContext& ctx,
                            std::string kind,
                            SourceSpan source,
                            std::string raw_text,
                            std::string raw_arguments,
                            std::vector<Value> evaluated_values,
                            std::string distance_expression,
                            double distance_value);
EditSourceRef next_active_edit_ref(MapContext& ctx);
template <typename Row>
void attach_active_edit_ref(MapContext& ctx, Row& row) {
    row.edit_ref = next_active_edit_ref(ctx);
}

struct ActiveStatementScope {
    MapContext& ctx;
    size_t old_index = k_no_source_ref;
    int old_next_element = 0;

    ActiveStatementScope(MapContext& context, size_t statement_index)
        : ctx(context), old_index(context.active_statement_index),
          old_next_element(context.active_statement_next_element_index) {
        ctx.active_statement_index = statement_index;
        ctx.active_statement_next_element_index = 0;
    }

    ~ActiveStatementScope() {
        ctx.active_statement_index = old_index;
        ctx.active_statement_next_element_index = old_next_element;
    }
};


std::vector<size_t> merge_source_file_records(MapContext& dest, const MapContext& child);
std::vector<size_t> merge_include_stacks(MapContext& dest, const MapContext& child);
std::vector<size_t> merge_include_invocation_keys(MapContext& dest, const MapContext& child);
void offset_edit_ref(EditSourceRef& ref, size_t statement_index_base);
template <typename Row>
void offset_row_edit_ref(Row& row, size_t statement_index_base) {
    offset_edit_ref(row.edit_ref, statement_index_base);
}

template <typename Rows>
void offset_row_edit_refs(Rows& rows, size_t statement_index_base) {
    for (auto& row : rows) offset_row_edit_ref(row, statement_index_base);
}

template <typename Fn>
void for_each_loaded_body_line(const LoadedText& loaded, Fn&& fn) {
    size_t pos = 0;
    int line_number = loaded.body_start_line;
    while (pos <= loaded.body.size()) {
        size_t line_end = loaded.body.find('\n', pos);
        size_t content_end = line_end == std::string::npos ? loaded.body.size() : line_end;
        if (content_end > pos && loaded.body[content_end - 1] == '\r') --content_end;
        fn(loaded.body.substr(pos, content_end - pos), pos, content_end, line_number);
        if (line_end == std::string::npos) break;
        pos = line_end + 1;
        ++line_number;
    }
}

std::vector<Value> values_from_fields(const std::vector<std::string>& fields);
EditSourceRef add_loaded_line_statement(MapContext& ctx,
                                        const LoadedText& loaded,
                                        const std::vector<std::string>& include_stack,
                                        const std::string& kind,
                                        size_t line_start,
                                        size_t line_end,
                                        const std::string& line,
                                        const std::vector<std::string>& fields);
bool value_equal(const Value& a, const Value& b);
bool variable_value_matches(const std::unordered_map<std::string, Value>& current,
                            const std::unordered_map<std::string, Value>& seed,
                            const std::string& key);
void note_distance_use(MapContext& ctx);
void note_variable_read(MapContext& ctx, const std::string& key);
void note_variable_write(MapContext& ctx, const std::string& key);
std::string format_seconds(double seconds);
void log_load_timing(const MapContext& ctx);
void add_controlpoint(MapContext& ctx, double value);
void set_distance(MapContext& ctx, double value);
void put_own(MapContext& ctx, const std::string& key, const Value& value, const std::string& flag = "");
void ensure_othertrack(MapContext& ctx, const std::string& key);
void put_other(MapContext& ctx, const Value& track_key, const std::string& element_key,
               const Value& value, const std::string& flag = "");

void relocate(MapContext& ctx);
void generate_geometry(MapContext& ctx, double unitdist,
                       bool has_arb, double arb_start, double arb_end, double arb_step,
                       const std::vector<double>* extra_controlpoints = nullptr,
                       bool generate_auxiliary_buffers = true);
std::vector<double> build_scene_adaptive_controlpoints(const MapContext& ctx,
                                                       const Matrix& baseline,
                                                       double min_step,
                                                       double max_step,
                                                       double max_angle_degrees,
                                                       double max_chord_error);

const KvMapSnapshot& build_map_snapshot(MapContext& ctx);
const KvSceneGeometrySnapshot& build_scene_geometry_snapshot(MapContext& ctx);
void invalidate_map_snapshot(MapContext& ctx, bool content_changed,
                             bool geometry_changed);
void invalidate_scene_geometry_snapshot(MapContext& ctx, bool scene_changed);
std::string statement_edit_id(MapContext& ctx, ParsedStatement& statement);
std::string native_element_edit_id(const MapContext& ctx, const EditSourceRef& ref,
                                   const std::string& row_kind);
std::string element_edit_id(const MapContext& ctx, const EditSourceRef& ref,
                            const std::string& row_kind);

// Returns station-list entries in the canonical parse-order used by the
// snapshot builder. The same ordering must be used by edit-target lookup and
// committed-row population so that station.list row indices stay stable
// across map snapshots, edit reports, and the GUI table cache.
std::vector<std::map<std::string, StationListEntry>::const_iterator>
ordered_station_list_entries(const MapContext& ctx);
struct MapEditChange {
    std::string change_id;
    std::string edit_id;
    std::string operation;
    std::map<std::string, std::string> field_changes;
    std::string replacement_statement;
    std::string target_file_path;
    std::string insert_before_edit_id;
    std::string expected_source_hash;
    std::string distance_resolution_key;
    std::string distance_boundary_token;
    std::string distance_expression;
    bool confirm_environment_mismatch = false;
};

// Repeater structure keys are represented as a list in the snapshot but as
// individual typed fields in the edit ABI. Keeping the validation and decoding
// here prevents the source writer and semantic validator from drifting apart.
struct RepeaterStructureKeyEdit {
    bool changed = false;
    std::vector<std::string> values;
};

inline bool is_repeater_structure_key_edit_field(const std::string& field) {
    return field == "structureKeys.count" ||
           (field.size() > 14 && field.compare(0, 14, "structureKeys.") == 0);
}

inline bool is_repeater_edit_field(const std::string& field) {
    return field == "distance" || field == "method" || field == "trackKey" ||
           field == "x" || field == "y" || field == "z" ||
           field == "rx" || field == "ry" || field == "rz" ||
           field == "tilt" || field == "span" || field == "interval" ||
           is_repeater_structure_key_edit_field(field);
}

inline size_t repeater_structure_key_index(const std::string& field) {
    constexpr size_t k_prefix_size = 14;
    if (field.size() <= k_prefix_size ||
        field.compare(0, k_prefix_size, "structureKeys.") != 0) {
        throw std::runtime_error("invalid Repeater structure key field: " + field);
    }
    size_t result = 0;
    for (size_t pos = k_prefix_size; pos < field.size(); ++pos) {
        const unsigned char ch = static_cast<unsigned char>(field[pos]);
        if (!std::isdigit(ch) ||
            result > (std::numeric_limits<size_t>::max() - 9) / 10) {
            throw std::runtime_error("invalid Repeater structure key field: " + field);
        }
        result = result * 10 + static_cast<size_t>(ch - '0');
    }
    return result;
}

inline RepeaterStructureKeyEdit parse_repeater_structure_key_edit(
    const MapEditChange& change) {
    RepeaterStructureKeyEdit result;
    const auto count_it = change.field_changes.find("structureKeys.count");
    bool has_indexed_field = false;
    for (const auto& entry : change.field_changes) {
        if (entry.first == "structureKeys.count") continue;
        if (!is_repeater_structure_key_edit_field(entry.first)) continue;
        has_indexed_field = true;
        (void)repeater_structure_key_index(entry.first);
    }
    if (count_it == change.field_changes.end() && !has_indexed_field) return result;
    if (count_it == change.field_changes.end()) {
        throw std::runtime_error(
            "Repeater structure key edits require structureKeys.count");
    }

    const std::string count_text = trim_field_copy(count_it->second);
    if (count_text.empty()) {
        throw std::runtime_error("Repeater structureKeys.count is empty");
    }
    size_t count = 0;
    for (char raw : count_text) {
        const unsigned char ch = static_cast<unsigned char>(raw);
        if (!std::isdigit(ch) ||
            count > (std::numeric_limits<size_t>::max() - 9) / 10) {
            throw std::runtime_error("invalid Repeater structureKeys.count: " + count_text);
        }
        count = count * 10 + static_cast<size_t>(ch - '0');
    }
    if (count == 0) {
        throw std::runtime_error("Repeater requires at least one structure key");
    }

    result.changed = true;
    result.values.reserve(count);
    for (size_t index = 0; index < count; ++index) {
        const std::string field = "structureKeys." + std::to_string(index);
        const auto value_it = change.field_changes.find(field);
        if (value_it == change.field_changes.end()) {
            throw std::runtime_error("missing Repeater structure key field: " + field);
        }
        const std::string value = trim_field_copy(value_it->second);
        if (value.empty()) {
            throw std::runtime_error("Repeater structure key is empty: " + field);
        }
        result.values.push_back(value);
    }
    for (const auto& entry : change.field_changes) {
        if (entry.first == "structureKeys.count") continue;
        if (!is_repeater_structure_key_edit_field(entry.first)) continue;
        if (repeater_structure_key_index(entry.first) >= count) {
            throw std::runtime_error("Repeater structure key index is out of range: " +
                                     entry.first);
        }
    }
    return result;
}

inline void validate_repeater_edit_fields(const MapEditChange& change) {
    for (const auto& entry : change.field_changes) {
        if (!is_repeater_edit_field(entry.first)) {
            throw std::runtime_error("unsupported Repeater edit field: " + entry.first);
        }
    }
    (void)parse_repeater_structure_key_edit(change);
}

struct SemanticElementSnapshot {
    std::string edit_id;
    std::string row_kind;
    std::string container_path;
    size_t row_index = 0;
    std::string source_file;
    std::string canonical;
};

struct SemanticMapSnapshot {
    std::vector<SemanticElementSnapshot> elements;
    std::string full_fingerprint;
};

SemanticMapSnapshot build_semantic_map_snapshot(MapContext& ctx);
std::string expected_target_semantic(MapContext& ctx,
                                     const SemanticElementSnapshot& target,
                                     const MapEditChange& change);

struct DistanceResolutionBoundary {
    std::string token;
    int line = 0;
    int column = 0;
    bool recommended = false;
};

struct DistanceResolutionSection {
    int first_line = 0;
    int last_line = 0;
    std::string direction;
};

struct DistanceResolutionRequest {
    std::string resolution_key;
    std::string reason;
    std::string source_file;
    std::vector<std::string> include_stack;
    double target_distance = 0.0;
    std::string variable_name;
    std::vector<std::string> affected_edit_ids;
    std::string suggested_expression;
    std::string insertion_preview;
    bool can_confirm_reuse = false;
    DistanceResolutionSection source_section;
    std::vector<DistanceResolutionBoundary> allowed_boundaries;
};

struct MapEditPreview {
    std::string file_path;
    std::string before;
    std::string after;
};

struct MapEditPatchedFile {
    std::string file_path;
    std::string source_key;
    std::string text;
    std::string bytes;
    std::string encoding;
    std::string newline;
    std::string base_hash;
    std::string current_hash;
    size_t byte_length = 0;
    bool utf8_bom = false;
};

struct MapEditCommittedFile {
    std::string file_path;
    std::string source_hash;
    size_t byte_length = 0;
};

struct MapEditCommittedRow {
    std::string row_kind;
    size_t row_index = 0;
    std::string edit_id;
    std::string file_path;
    int line = 0;
    int column = 0;
    std::string raw_text_preview;
};

// Internal-only provenance for reconnecting one validated reparse element to
// the stable editId that selected its source statement. Offsets refer to the
// final decoded UTF-8 text in MapEditPatchedFile and are never serialized.
struct MapEditIdentityOrigin {
    std::string edit_id;
    std::string row_kind;
    std::string source_key;
    size_t text_start = 0;
    size_t text_end = 0;
    int element_index = 0;
    int baseline_global_order = 0;
};

struct MapEditReport {
    std::vector<std::string> changed_files;
    std::vector<MapEditCommittedFile> committed_files;
    std::vector<MapEditCommittedRow> committed_rows;
    std::vector<std::string> warnings;
    std::vector<std::string> blocking_errors;
    std::vector<MapEditPreview> previews;
    std::vector<MapEditPatchedFile> patched_files;
    std::vector<MapEditIdentityOrigin> identity_origins;
    std::vector<DistanceResolutionRequest> resolution_requests;
    std::shared_ptr<MapContext> validated_context;
    std::string validation_fingerprint;
    bool full_reparse_ok = false;
    int target_distance_match_count = 0;
    int non_target_changed_count = 0;
    int created_distance_block_count = 0;
    int reused_distance_block_count = 0;
    int distance_group_count = 0;
    int update_count = 0;
    int insert_count = 0;
    int delete_count = 0;

    bool ok() const {
        return blocking_errors.empty() && resolution_requests.empty();
    }
};

std::vector<MapEditChange> copy_edit_batch(const KvEditBatch& batch);
const KvEditTargetSnapshot& build_edit_target_snapshot(MapContext& ctx,
                                                       const std::string& edit_id);
MapEditReport build_edit_report(MapContext& ctx,
                                const std::vector<MapEditChange>& changes,
                                bool write_files);
const KvEditReportSnapshot& build_edit_report_snapshot(MapContext& ctx,
                                                       const MapEditReport& report);
void apply_patched_files_to_overrides(SourceTextOverrides& overrides,
                                      const MapEditReport& report);
void reparse_context_with_overrides(MapContext& ctx,
                                    SourceTextOverrides overrides,
                                    bool has_arbitrary_distribution,
                                    const std::array<double, 3>& arbitrary_distribution);
void apply_edit_report_to_memory(MapContext& ctx, const MapEditReport& report);
void reset_memory_edits(MapContext& ctx);
MapEditReport commit_memory_edits(MapContext& ctx);

std::unique_ptr<MapContext> parse_map_context(std::filesystem::path map_path,
                                              double unit_distance,
                                              SourceTextOverrides overrides,
                                              bool has_arbitrary_distribution,
                                              const std::array<double, 3>& arbitrary_distribution,
                                              MapParseOptions options = {});

} // namespace kme::maploader::detail
