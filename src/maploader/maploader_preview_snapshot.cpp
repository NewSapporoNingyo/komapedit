/*
 * Copyright (c) 2026 Sapporo_ningyo
 *
 * Licensed under Apache License 2.0; see LICENSE and NOTICE.
 */

#include "maploader_internal.h"

namespace kme::maploader::detail {
namespace {

KvDoubleBuffer matrix_view(const Matrix& matrix) {
    return {matrix.data.empty() ? nullptr : matrix.data.data(),
            matrix.rows, matrix.cols};
}

class SnapshotBuilder {
public:
    SnapshotBuilder(MapContext& context, PreviewSnapshotStorage& storage)
        : ctx_(context), storage_(storage) {
        storage_.string_arena.reserve(256 * 1024);
        storage_.tables.reserve(25);
        storage_.rows.reserve(24 * 1024);
        storage_.cells.reserve(240 * 1024);
        storage_.array_values.reserve(8 * 1024);
    }

    void build() {
        add_root_data();
        add_station_tables();
        add_structure_tables();
        add_other_train_tables();
        add_sound_table();
        add_repeater_table();
        add_signal_tables();
        add_simple_event_tables();
        finalize_view();
    }

private:
    using Cell = std::pair<const char*, KvPreviewValue>;

    KvStringRef string_ref(const std::string& text) {
        auto existing = strings_.find(text);
        if (existing != strings_.end()) return existing->second;
        KvStringRef ref{static_cast<std::uint64_t>(storage_.string_arena.size()),
                        static_cast<std::uint64_t>(text.size())};
        storage_.string_arena.append(text);
        strings_.emplace(text, ref);
        return ref;
    }

    KvStringRef string_ref(const char* text) {
        return string_ref(std::string(text ? text : ""));
    }

    KvPreviewValue null_value() const {
        KvPreviewValue out{};
        out.kind = KV_PREVIEW_VALUE_NULL;
        return out;
    }

    KvPreviewValue bool_value(bool value) const {
        KvPreviewValue out{};
        out.kind = KV_PREVIEW_VALUE_BOOL;
        out.boolean_value = value ? 1u : 0u;
        return out;
    }

    KvPreviewValue number_value(double value) const {
        if (std::isnan(value)) return null_value();
        KvPreviewValue out{};
        out.kind = KV_PREVIEW_VALUE_NUMBER;
        out.number_value = value;
        return out;
    }

    KvPreviewValue number_value(int value) const {
        return number_value(static_cast<double>(value));
    }

    KvPreviewValue string_value(const std::string& value) {
        KvPreviewValue out{};
        out.kind = KV_PREVIEW_VALUE_STRING;
        out.string_value = string_ref(value);
        return out;
    }

    KvPreviewValue value(const Value& input) {
        switch (input.kind) {
            case ValueKind::Null: return null_value();
            case ValueKind::ContinueValue: return string_value("c");
            case ValueKind::Number: return number_value(input.number);
            case ValueKind::String: return string_value(input.text);
        }
        return null_value();
    }

    KvPreviewValue array_value(const std::vector<Value>& values) {
        KvPreviewValue out{};
        out.kind = KV_PREVIEW_VALUE_ARRAY;
        out.array_values.offset = static_cast<std::uint64_t>(storage_.array_values.size());
        out.array_values.count = static_cast<std::uint64_t>(values.size());
        for (const Value& item : values) storage_.array_values.push_back(value(item));
        return out;
    }

    KvPreviewValue string_array_value(const std::vector<std::string>& values) {
        KvPreviewValue out{};
        out.kind = KV_PREVIEW_VALUE_ARRAY;
        out.array_values.offset = static_cast<std::uint64_t>(storage_.array_values.size());
        out.array_values.count = static_cast<std::uint64_t>(values.size());
        for (const std::string& item : values) storage_.array_values.push_back(string_value(item));
        return out;
    }

    void begin_table(std::uint32_t kind) {
        KvPreviewTable table{};
        table.kind = kind;
        table.rows.offset = static_cast<std::uint64_t>(storage_.rows.size());
        storage_.tables.push_back(table);
    }

    void end_table() {
        KvPreviewTable& table = storage_.tables.back();
        table.rows.count = static_cast<std::uint64_t>(storage_.rows.size()) - table.rows.offset;
    }

    void add_row(std::initializer_list<Cell> cells,
                 const EditSourceRef& edit_ref = {},
                 const char* row_kind = "",
                 const std::string& object_key = {}) {
        KvPreviewRow row{};
        row.cells.offset = static_cast<std::uint64_t>(storage_.cells.size());
        row.object_key = string_ref(object_key);
        for (const Cell& cell : cells) {
            storage_.cells.push_back(KvPreviewCell{string_ref(cell.first), cell.second});
        }
        row.cells.count = static_cast<std::uint64_t>(storage_.cells.size()) - row.cells.offset;

        if (edit_ref.valid() && edit_ref.statement_index < ctx_.parsed_statements.size()) {
            const ParsedStatement& statement = ctx_.parsed_statements[edit_ref.statement_index];
            const std::string edit_id = element_edit_id(ctx_, edit_ref, row_kind);
            row.edit_id = string_ref(edit_id);
            row.source_file_path = string_ref(source_file_path(ctx_, statement.source));
            row.source_raw_text_preview = string_ref(statement.raw_text_preview);
            row.source_line = statement.source.line;
            row.source_column = statement.source.column;
            storage_.cells.push_back(
                KvPreviewCell{string_ref("editId"), string_value(edit_id)});
            storage_.cells.push_back(
                KvPreviewCell{string_ref("source"), null_value()});
            row.cells.count += 2;
        }
        storage_.rows.push_back(row);
    }

    void add_root_data() {
        storage_.view.root_path = string_ref(ctx_.rootpath_utf8);

        storage_.file_structure.reserve(ctx_.file_structure.size());
        for (const FileStructureRecord& input : ctx_.file_structure) {
            KvPreviewFileStructure output{};
            output.parent_index = input.parent_index == kNoSourceRef
                ? -1
                : static_cast<std::int64_t>(input.parent_index);
            output.include_path = string_ref(input.include_path);
            output.absolute_path = string_ref(input.absolute_path);
            storage_.file_structure.push_back(output);
        }

        storage_.source_files.reserve(ctx_.source_files.size());
        for (const SourceFileRecord& input : ctx_.source_files) {
            KvPreviewSourceFile output{};
            output.file_path = string_ref(input.file_path);
            output.display_path = string_ref(input.display_path);
            output.encoding = string_ref(input.encoding);
            output.newline = string_ref(input.newline);
            output.source_hash = string_ref(input.source_hash);
            output.byte_length = static_cast<std::uint64_t>(input.byte_length);
            storage_.source_files.push_back(output);
        }

        storage_.other_tracks.reserve(ctx_.othertrack_order.size());
        for (const std::string& key : ctx_.othertrack_order) {
            KvPreviewOtherTrack output{};
            output.key = string_ref(key);
            auto range = ctx_.othertrack_range.find(key);
            if (range != ctx_.othertrack_range.end()) {
                output.range_min = range->second.first;
                output.range_max = range->second.second;
            }
            auto points = ctx_.othertrack_buffers.find(key);
            if (points != ctx_.othertrack_buffers.end()) output.points = matrix_view(points->second);
            storage_.other_tracks.push_back(output);
        }

        storage_.own_track_events.reserve(ctx_.own_track.size());
        for (const OwnTrackEvent& input : ctx_.own_track) {
            KvPreviewOwnTrackEvent output{};
            output.distance = input.distance;
            output.key = string_ref(input.key);
            output.flag = string_ref(input.flag);
            output.value = value(input.value);
            storage_.own_track_events.push_back(output);
        }

        storage_.speed_limits.reserve(ctx_.speedlimits.size());
        for (const SpeedLimitEvent& input : ctx_.speedlimits) {
            storage_.speed_limits.push_back(KvPreviewSpeedLimit{input.distance, value(input.speed)});
        }

        storage_.station_positions.reserve(ctx_.station_position.size());
        for (const auto& input : ctx_.station_position) {
            storage_.station_positions.push_back(
                KvPreviewStationPosition{input.first, string_ref(input.second)});
        }
        storage_.station_names.reserve(ctx_.station_key.size());
        for (const auto& input : ctx_.station_key) {
            storage_.station_names.push_back(
                KvPreviewStationName{string_ref(input.first), string_ref(input.second)});
        }
    }

    void add_station_tables() {
        static const char* keys[] = {
            "stationKey", "stationName", "arrivalTime", "depertureTime", "stoppageTime",
            "defaultTime", "signalFlag", "alightingTime", "passengers", "arrivalSoundKey",
            "depertureSoundKey", "doorReopen", "stuckInDoor"
        };
        begin_table(KV_PREVIEW_TABLE_STATION_LIST);
        for (const auto& item : ctx_.station_list) {
            const StationListEntry& row = item.second;
            add_row({
                {keys[0], string_value(row.fields[0])}, {keys[1], string_value(row.fields[1])},
                {keys[2], string_value(row.fields[2])}, {keys[3], string_value(row.fields[3])},
                {keys[4], string_value(row.fields[4])}, {keys[5], string_value(row.fields[5])},
                {keys[6], string_value(row.fields[6])}, {keys[7], string_value(row.fields[7])},
                {keys[8], string_value(row.fields[8])}, {keys[9], string_value(row.fields[9])},
                {keys[10], string_value(row.fields[10])}, {keys[11], string_value(row.fields[11])},
                {keys[12], string_value(row.fields[12])}},
                row.edit_ref, "station.list", item.first);
        }
        end_table();

        begin_table(KV_PREVIEW_TABLE_STATION_PUT);
        for (const StationPut& row : ctx_.station_puts) {
            add_row({
                {"distance", number_value(row.distance)}, {"stationKey", value(row.station_key)},
                {"door", value(row.door)}, {"margin1", value(row.margin1)},
                {"margin2", value(row.margin2)}, {"filePath", string_value(row.file_path)},
                {"order", number_value(row.order)}}, row.edit_ref, "station.put");
        }
        end_table();
    }

    void add_structure_tables() {
        begin_table(KV_PREVIEW_TABLE_STRUCTURE);
        for (const StructurePut& row : ctx_.structure_puts) {
            add_row({
                {"distance", number_value(row.distance)}, {"method", string_value(row.method)},
                {"structureKey", value(row.structure_key)}, {"trackKey", value(row.track_key)},
                {"x", number_value(row.x)}, {"y", number_value(row.y)},
                {"z", number_value(row.z)}, {"rx", number_value(row.rx)},
                {"ry", number_value(row.ry)}, {"rz", number_value(row.rz)},
                {"tilt", number_value(row.tilt)}, {"span", number_value(row.span)},
                {"filePath", string_value(row.file_path)}, {"order", number_value(row.order)}},
                row.edit_ref, "structure.put");
        }
        end_table();

        begin_table(KV_PREVIEW_TABLE_STRUCTURE_BETWEEN);
        for (const StructurePut& row : ctx_.structure_betweens) {
            add_row({
                {"distance", number_value(row.distance)}, {"method", string_value(row.method)},
                {"structureKey", value(row.structure_key)}, {"trackKey1", value(row.track_key1)},
                {"trackKey2", value(row.track_key2)}, {"flag", number_value(row.flag)},
                {"filePath", string_value(row.file_path)}, {"order", number_value(row.order)}},
                row.edit_ref, "structure.between");
        }
        end_table();

        begin_table(KV_PREVIEW_TABLE_STRUCTURE_MODEL);
        for (const StructureModel& row : ctx_.structure_models) {
            add_row({{"structureKey", string_value(row.structure_key)},
                     {"filePath", string_value(row.file_path)}},
                    row.edit_ref, "structure.model");
        }
        end_table();
    }

    void add_other_train_tables() {
        begin_table(KV_PREVIEW_TABLE_OTHER_TRAIN);
        for (const OtherTrainDefinition& row : ctx_.other_trains) {
            add_row({
                {"distance", number_value(row.distance)}, {"method", string_value(row.method)},
                {"trainKey", value(row.train_key)}, {"filePath", value(row.load_file_path)},
                {"resolvedFilePath", string_value(row.resolved_file_path)},
                {"trackKey", value(row.track_key)}, {"direction", value(row.direction)},
                {"sourceFilePath", string_value(row.source_file_path)},
                {"order", number_value(row.order)}}, row.edit_ref, "otherTrain.definition");
        }
        end_table();

        begin_table(KV_PREVIEW_TABLE_OTHER_TRAIN_STOP);
        for (const OtherTrainStop& row : ctx_.other_train_stops) {
            add_row({
                {"distance", number_value(row.distance)}, {"trainKey", value(row.train_key)},
                {"decelerate", value(row.decelerate)}, {"stopTime", value(row.stop_time)},
                {"accelerate", value(row.accelerate)}, {"speed", value(row.speed)},
                {"filePath", string_value(row.file_path)}, {"order", number_value(row.order)}},
                row.edit_ref, "otherTrain.stop");
        }
        end_table();

        begin_table(KV_PREVIEW_TABLE_OTHER_TRAIN_STRUCTURE_KEY);
        for (const OtherTrainReferencedKey& row : ctx_.other_train_structure_keys) {
            add_row({{"key", string_value(row.key)}, {"filePath", string_value(row.file_path)}},
                    row.edit_ref, "otherTrain.structureKey");
        }
        end_table();

        begin_table(KV_PREVIEW_TABLE_OTHER_TRAIN_SOUND_3D_KEY);
        for (const OtherTrainReferencedKey& row : ctx_.other_train_sound_3d_keys) {
            add_row({{"key", string_value(row.key)}, {"filePath", string_value(row.file_path)}},
                    row.edit_ref, "otherTrain.sound3DKey");
        }
        end_table();
    }

    void add_sound_table() {
        begin_table(KV_PREVIEW_TABLE_SOUND_LIST);
        for (const SoundListEntry& row : ctx_.sound_list) {
            add_row({
                {"soundKey", string_value(row.sound_key)}, {"filePath", string_value(row.file_path)},
                {"bufferCount", number_value(row.buffer_count)}, {"is3D", bool_value(row.is_3d)}},
                row.edit_ref, row.is_3d ? "sound3D.list" : "sound.list");
        }
        end_table();
    }

    void add_repeater_table() {
        begin_table(KV_PREVIEW_TABLE_REPEATER);
        for (const RepeaterEvent& row : ctx_.repeaters) {
            add_row({
                {"distance", number_value(row.distance)}, {"method", string_value(row.method)},
                {"repeaterKey", value(row.repeater_key)}, {"trackKey", value(row.track_key)},
                {"x", number_value(row.x)}, {"y", number_value(row.y)},
                {"z", number_value(row.z)}, {"rx", number_value(row.rx)},
                {"ry", number_value(row.ry)}, {"rz", number_value(row.rz)},
                {"tilt", number_value(row.tilt)}, {"span", number_value(row.span)},
                {"interval", number_value(row.interval)},
                {"structureKeys", array_value(row.structure_keys)},
                {"filePath", string_value(row.file_path)}, {"order", number_value(row.order)}},
                row.edit_ref, "repeater");
        }
        end_table();
    }

    void add_signal_tables() {
        begin_table(KV_PREVIEW_TABLE_SIGNAL_ASPECT);
        for (const SignalAspect& row : ctx_.signal_aspects) {
            add_row({{"signalAspectKey", string_value(row.signal_aspect_key)},
                     {"structureKeys", string_array_value(row.structure_keys)}},
                    row.edit_ref, "signal.aspect");
        }
        end_table();

        begin_table(KV_PREVIEW_TABLE_SIGNAL);
        for (const SignalPut& row : ctx_.signal_puts) {
            add_row({
                {"distance", number_value(row.distance)},
                {"signalAspectKey", value(row.signal_aspect_key)}, {"section", value(row.section)},
                {"trackKey", value(row.track_key)}, {"x", number_value(row.x)},
                {"y", number_value(row.y)}, {"z", number_value(row.z)},
                {"rx", number_value(row.rx)}, {"ry", number_value(row.ry)},
                {"rz", number_value(row.rz)}, {"tilt", number_value(row.tilt)},
                {"span", number_value(row.span)}, {"filePath", string_value(row.file_path)},
                {"order", number_value(row.order)}}, row.edit_ref, "signal.put");
        }
        end_table();
    }

    void add_simple_event_tables() {
        begin_table(KV_PREVIEW_TABLE_BEACON);
        for (const BeaconPut& row : ctx_.beacons) {
            add_row({
                {"distance", number_value(row.distance)}, {"type", value(row.type)},
                {"section", value(row.section)}, {"sendData", value(row.send_data)},
                {"filePath", string_value(row.file_path)}, {"order", number_value(row.order)}},
                row.edit_ref, "beacon.put");
        }
        end_table();

        begin_table(KV_PREVIEW_TABLE_PRETRAIN);
        for (const PreTrainPass& row : ctx_.pretrains) {
            add_row({
                {"distance", number_value(row.distance)}, {"passTime", value(row.pass_time)},
                {"filePath", string_value(row.file_path)}, {"order", number_value(row.order)}},
                row.edit_ref, "preTrain.pass");
        }
        end_table();

        begin_table(KV_PREVIEW_TABLE_IRREGULARITY);
        for (const IrregularityChange& row : ctx_.irregularities) {
            add_row({
                {"distance", number_value(row.distance)}, {"x", number_value(row.x)},
                {"y", number_value(row.y)}, {"r", number_value(row.r)},
                {"lx", number_value(row.lx)}, {"ly", number_value(row.ly)},
                {"lr", number_value(row.lr)}, {"filePath", string_value(row.file_path)},
                {"order", number_value(row.order)}}, row.edit_ref, "irregularity.change");
        }
        end_table();

        begin_table(KV_PREVIEW_TABLE_MAP_SOUND);
        for (const MapSoundPlay& row : ctx_.map_sounds) {
            add_row({
                {"distance", number_value(row.distance)}, {"soundKey", value(row.sound_key)},
                {"filePath", string_value(row.file_path)}, {"order", number_value(row.order)}},
                row.edit_ref, "mapSound.play");
        }
        end_table();

        begin_table(KV_PREVIEW_TABLE_MAP_SOUND_3D);
        for (const MapSound3DPut& row : ctx_.map_sound_3d) {
            add_row({
                {"distance", number_value(row.distance)}, {"soundKey", value(row.sound_key)},
                {"x", number_value(row.x)}, {"y", number_value(row.y)},
                {"filePath", string_value(row.file_path)}, {"order", number_value(row.order)}},
                row.edit_ref, "mapSound3D.put");
        }
        end_table();

        add_noise_table(KV_PREVIEW_TABLE_ROLLING_NOISE, ctx_.rolling_noises,
                        "rollingNoise.change");
        add_noise_table(KV_PREVIEW_TABLE_FLANGE_NOISE, ctx_.flange_noises,
                        "flangeNoise.change");
        add_noise_table(KV_PREVIEW_TABLE_JOINT_NOISE, ctx_.joint_noises,
                        "jointNoise.play");

        begin_table(KV_PREVIEW_TABLE_BACKGROUND);
        for (const BackgroundChange& row : ctx_.backgrounds) {
            add_row({
                {"distance", number_value(row.distance)}, {"structureKey", value(row.structure_key)},
                {"filePath", string_value(row.file_path)}, {"order", number_value(row.order)}},
                row.edit_ref, "background.change");
        }
        end_table();

        begin_table(KV_PREVIEW_TABLE_ADHESION);
        for (const AdhesionChange& row : ctx_.adhesions) {
            add_row({
                {"distance", number_value(row.distance)}, {"a", value(row.a)},
                {"b", value(row.b)}, {"c", value(row.c)},
                {"filePath", string_value(row.file_path)}, {"order", number_value(row.order)}},
                row.edit_ref, "adhesion.change");
        }
        end_table();

        begin_table(KV_PREVIEW_TABLE_CAB_ILLUMINANCE);
        for (const CabIlluminanceChange& row : ctx_.cab_illuminance) {
            add_row({
                {"distance", number_value(row.distance)}, {"value", value(row.value)},
                {"filePath", string_value(row.file_path)}, {"order", number_value(row.order)}},
                row.edit_ref, "cabIlluminance.change");
        }
        end_table();

        begin_table(KV_PREVIEW_TABLE_FOG);
        for (const FogChange& row : ctx_.fogs) {
            add_row({
                {"distance", number_value(row.distance)}, {"density", value(row.density)},
                {"red", value(row.red)}, {"green", value(row.green)},
                {"blue", value(row.blue)}, {"filePath", string_value(row.file_path)},
                {"order", number_value(row.order)}}, row.edit_ref, "fog.change");
        }
        end_table();
    }

    template <typename Rows>
    void add_noise_table(std::uint32_t kind, const Rows& rows, const char* row_kind) {
        begin_table(kind);
        for (const auto& row : rows) {
            add_row({
                {"distance", number_value(row.distance)}, {"index", value(row.index)},
                {"filePath", string_value(row.file_path)}, {"order", number_value(row.order)}},
                row.edit_ref, row_kind);
        }
        end_table();
    }

    void finalize_view() {
        KvPreviewSnapshot& view = storage_.view;
        view.version = KV_PREVIEW_SNAPSHOT_VERSION;
        view.content_revision = ctx_.content_revision;
        view.geometry_revision = ctx_.geometry_revision;
        view.string_data = storage_.string_arena.empty() ? nullptr : storage_.string_arena.data();
        view.string_size = static_cast<std::uint64_t>(storage_.string_arena.size());
        view.file_structure = storage_.file_structure.empty() ? nullptr : storage_.file_structure.data();
        view.file_structure_count = static_cast<std::uint64_t>(storage_.file_structure.size());
        view.source_files = storage_.source_files.empty() ? nullptr : storage_.source_files.data();
        view.source_file_count = static_cast<std::uint64_t>(storage_.source_files.size());
        view.controlpoints = ctx_.controlpoints.empty() ? nullptr : ctx_.controlpoints.data();
        view.controlpoint_count = static_cast<std::uint64_t>(ctx_.controlpoints.size());
        for (size_t i = 0; i < 3; ++i) {
            view.cp_arbdistribution[i] = ctx_.cp_arbdistribution[i];
            view.cp_arbdistribution_default[i] = ctx_.cp_arbdistribution_default[i];
        }
        for (size_t i = 0; i < 2; ++i) view.cp_default_range[i] = ctx_.cp_defaultrange[i];
        view.owntrack = matrix_view(ctx_.owntrack_buffer);
        view.curve_radius = matrix_view(ctx_.curveradius_buffer);
        view.other_tracks = storage_.other_tracks.empty() ? nullptr : storage_.other_tracks.data();
        view.other_track_count = static_cast<std::uint64_t>(storage_.other_tracks.size());
        view.own_track_events = storage_.own_track_events.empty() ? nullptr : storage_.own_track_events.data();
        view.own_track_event_count = static_cast<std::uint64_t>(storage_.own_track_events.size());
        view.speed_limits = storage_.speed_limits.empty() ? nullptr : storage_.speed_limits.data();
        view.speed_limit_count = static_cast<std::uint64_t>(storage_.speed_limits.size());
        view.station_positions = storage_.station_positions.empty() ? nullptr : storage_.station_positions.data();
        view.station_position_count = static_cast<std::uint64_t>(storage_.station_positions.size());
        view.station_names = storage_.station_names.empty() ? nullptr : storage_.station_names.data();
        view.station_name_count = static_cast<std::uint64_t>(storage_.station_names.size());
        view.tables = storage_.tables.empty() ? nullptr : storage_.tables.data();
        view.table_count = static_cast<std::uint64_t>(storage_.tables.size());
        view.rows = storage_.rows.empty() ? nullptr : storage_.rows.data();
        view.row_count = static_cast<std::uint64_t>(storage_.rows.size());
        view.cells = storage_.cells.empty() ? nullptr : storage_.cells.data();
        view.cell_count = static_cast<std::uint64_t>(storage_.cells.size());
        view.array_values = storage_.array_values.empty() ? nullptr : storage_.array_values.data();
        view.array_value_count = static_cast<std::uint64_t>(storage_.array_values.size());
    }

    MapContext& ctx_;
    PreviewSnapshotStorage& storage_;
    std::unordered_map<std::string, KvStringRef> strings_;
};

} // namespace

void invalidate_preview_snapshot(MapContext& ctx, bool content_changed,
                                 bool geometry_changed) {
    if (content_changed) ++ctx.content_revision;
    if (geometry_changed) ++ctx.geometry_revision;
    ctx.preview_snapshot.reset();
}

const KvPreviewSnapshot& build_preview_snapshot(MapContext& ctx) {
    if (ctx.preview_snapshot &&
        ctx.preview_snapshot->view.content_revision == ctx.content_revision &&
        ctx.preview_snapshot->view.geometry_revision == ctx.geometry_revision) {
        return ctx.preview_snapshot->view;
    }

    auto started_at = SteadyClock::now();
    auto storage = std::make_unique<PreviewSnapshotStorage>();
    SnapshotBuilder builder(ctx, *storage);
    builder.build();
    const double build_seconds = elapsed_seconds_since(started_at);
    storage->view.build_seconds = build_seconds;
    ctx.timing.snapshot_seconds += build_seconds;
    ctx.preview_snapshot = std::move(storage);
    return ctx.preview_snapshot->view;
}

} // namespace kme::maploader::detail
