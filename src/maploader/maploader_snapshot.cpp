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
            static_cast<std::uint64_t>(matrix.rows),
            static_cast<std::uint64_t>(matrix.cols)};
}

template <typename T>
const T* data_or_null(const std::vector<T>& values) {
    return values.empty() ? nullptr : values.data();
}

class MapSnapshotBuilder {
public:
    MapSnapshotBuilder(MapContext& context, MapSnapshotStorage& storage)
        : ctx_(context), storage_(storage) {
        const size_t row_count = ctx_.own_track.size() + ctx_.station_puts.size() +
            ctx_.station_list.size() + ctx_.structure_loads.size() +
            ctx_.structure_puts.size() + ctx_.structure_betweens.size() +
            ctx_.structure_models.size() + ctx_.other_trains.size() +
            ctx_.other_train_enables.size() + ctx_.other_train_stops.size() +
            ctx_.section_begins.size() + ctx_.section_speed_limits.size() +
            ctx_.signal_aspects.size() + ctx_.signal_puts.size() +
            ctx_.beacons.size() + ctx_.pretrains.size() + ctx_.sound_list.size() +
            ctx_.map_sounds.size() + ctx_.map_sound_3d.size() +
            ctx_.rolling_noises.size() + ctx_.flange_noises.size() +
            ctx_.joint_noises.size() + ctx_.repeaters.size() +
            ctx_.irregularities.size() + ctx_.backgrounds.size() +
            ctx_.adhesions.size() + ctx_.cab_illuminance.size() +
            ctx_.fogs.size() + ctx_.draw_distances.size() + ctx_.speedlimits.size();
        const size_t preview_row_count =
            ctx_.variable_assignments.size() + ctx_.resource_list_loads.size();
        storage_.string_arena.reserve(
            std::max<size_t>(256 * 1024, (row_count + preview_row_count) * 32));
        storage_.values.reserve(row_count * 2 + preview_row_count);
        storage_.string_refs.reserve(row_count);
        storage_.elements.reserve(row_count);
    }

    void build() {
        add_root();
        add_tracks();
        add_stations();
        add_structures();
        add_other_trains();
        add_sections_signals_and_sounds();
        add_environment();
        add_preview_rows();
        add_edit_registry();
        finalize();
    }

private:
    KvStringRef string_ref(const std::string& text) {
        auto found = strings_.find(text);
        if (found != strings_.end()) return found->second;
        KvStringRef ref{static_cast<std::uint64_t>(storage_.string_arena.size()),
                        static_cast<std::uint64_t>(text.size())};
        storage_.string_arena.append(text);
        strings_.emplace(text, ref);
        return ref;
    }

    KvStringRef string_ref(const char* text) {
        return string_ref(std::string(text ? text : ""));
    }

    KvValue value(const Value& input) {
        KvValue out{};
        switch (input.kind) {
            case ValueKind::Null:
                out.kind = KV_VALUE_NULL;
                break;
            case ValueKind::Number:
                out.kind = KV_VALUE_NUMBER;
                out.number_value = input.number;
                break;
            case ValueKind::String:
                out.kind = KV_VALUE_STRING;
                out.string_value = string_ref(input.text);
                break;
            case ValueKind::ContinueValue:
                out.kind = KV_VALUE_CONTINUE;
                break;
        }
        return out;
    }

    KvSpan append_values(const std::vector<Value>& values) {
        KvSpan span{static_cast<std::uint64_t>(storage_.values.size()),
                    static_cast<std::uint64_t>(values.size())};
        for (const Value& input : values) storage_.values.push_back(value(input));
        return span;
    }

    KvSpan append_strings(const std::vector<std::string>& values) {
        KvSpan span{static_cast<std::uint64_t>(storage_.string_refs.size()),
                    static_cast<std::uint64_t>(values.size())};
        for (const std::string& input : values) storage_.string_refs.push_back(string_ref(input));
        return span;
    }

    KvRowMetadata metadata(const EditSourceRef& ref, const std::string& row_kind,
                           size_t source_file_index = k_no_source_ref) {
        KvRowMetadata out{};
        out.source_file_index = source_file_index == k_no_source_ref
            ? KV_INDEX_NONE
            : static_cast<std::uint64_t>(source_file_index);
        if (!ref.valid() || ref.statement_index >= ctx_.parsed_statements.size()) return out;
        const ParsedStatement& statement = ctx_.parsed_statements[ref.statement_index];
        out.edit_id = string_ref(element_edit_id(ctx_, ref, row_kind));
        out.raw_text_preview = string_ref(statement.raw_text_preview);
        out.source_file_index = static_cast<std::uint64_t>(statement.source.source_file_index);
        out.line = statement.source.line;
        out.column = statement.source.column;
        out.line_end = statement.source.line_end;
        out.column_end = statement.source.column_end;
        out.element_index = static_cast<std::uint32_t>(std::max(0, ref.element_index));
        return out;
    }

    void add_root() {
        storage_.view.root_path = string_ref(ctx_.rootpath_utf8);
        storage_.file_structure.reserve(ctx_.file_structure.size());
        for (const FileStructureRecord& input : ctx_.file_structure) {
            KvFileStructureRow row{};
            row.parent_index = input.parent_index == k_no_source_ref
                ? -1 : static_cast<std::int64_t>(input.parent_index);
            row.include_path = string_ref(input.include_path);
            row.absolute_path = string_ref(input.absolute_path);
            storage_.file_structure.push_back(row);
        }
        storage_.source_files.reserve(ctx_.source_files.size());
        for (const SourceFileRecord& input : ctx_.source_files) {
            KvSourceFileRow row{};
            row.file_path = string_ref(input.file_path);
            row.display_path = string_ref(input.display_path);
            row.encoding = string_ref(input.encoding);
            row.newline = string_ref(input.newline);
            row.source_hash = string_ref(input.source_hash);
            row.byte_length = static_cast<std::uint64_t>(input.byte_length);
            storage_.source_files.push_back(row);
        }
    }

    void add_tracks() {
        storage_.own_track_events.reserve(ctx_.own_track.size());
        for (const OwnTrackEvent& input : ctx_.own_track) {
            KvTrackEventRow row{};
            row.distance = input.distance;
            row.key = string_ref(input.key);
            row.flag = string_ref(input.flag);
            row.value = value(input.value);
            row.metadata = metadata(input.edit_ref, "own_track");
            storage_.own_track_events.push_back(row);
        }

        storage_.other_tracks.reserve(ctx_.othertrack_order.size());
        for (const std::string& key : ctx_.othertrack_order) {
            KvOtherTrackRow track{};
            track.key = string_ref(key);
            auto range = ctx_.othertrack_range.find(key);
            if (range != ctx_.othertrack_range.end()) {
                track.range_min = range->second.first;
                track.range_max = range->second.second;
            }
            auto geometry = ctx_.othertrack_buffers.find(key);
            if (geometry != ctx_.othertrack_buffers.end()) track.points = matrix_view(geometry->second);
            track.events.offset = static_cast<std::uint64_t>(storage_.other_track_events.size());
            auto events = ctx_.othertrack.find(key);
            if (events != ctx_.othertrack.end()) {
                storage_.other_track_events.reserve(storage_.other_track_events.size() + events->second.size());
                for (const OtherTrackEvent& input : events->second) {
                    KvTrackEventRow row{};
                    row.distance = input.distance;
                    row.key = string_ref(input.key);
                    row.flag = string_ref(input.flag);
                    row.value = value(input.value);
                    row.metadata = metadata(input.edit_ref, "othertrack");
                    storage_.other_track_events.push_back(row);
                }
            }
            track.events.count = static_cast<std::uint64_t>(storage_.other_track_events.size()) -
                                 track.events.offset;
            storage_.other_tracks.push_back(track);
        }
    }

    void add_stations() {
        storage_.station_positions.reserve(ctx_.station_position.size());
        for (const auto& input : ctx_.station_position) {
            storage_.station_positions.push_back({input.first, string_ref(input.second)});
        }
        storage_.station_names.reserve(ctx_.station_key.size());
        for (const auto& input : ctx_.station_key) {
            storage_.station_names.push_back({string_ref(input.first), string_ref(input.second)});
        }
        storage_.station_puts.reserve(ctx_.station_puts.size());
        for (const StationPut& input : ctx_.station_puts) {
            KvStationPutRow row{};
            row.distance = input.distance;
            row.station_key = value(input.station_key);
            row.door = value(input.door);
            row.margin1 = value(input.margin1);
            row.margin2 = value(input.margin2);
            row.file_path = string_ref(input.file_path);
            row.order = input.order;
            row.metadata = metadata(input.edit_ref, "station.put");
            storage_.station_puts.push_back(row);
        }
        const auto& station_list_entries = ordered_station_list_entries(ctx_);
        storage_.station_list.reserve(station_list_entries.size());
        for (const StationListEntry* input : station_list_entries) {
            KvStationListRow row{};
            row.object_key = string_ref(input->fields[0]);
            for (size_t i = 0; i < input->fields.size(); ++i) {
                row.fields[i] = string_ref(input->fields[i]);
            }
            row.metadata = metadata(input->edit_ref, "station.list");
            storage_.station_list.push_back(row);
        }
    }

    void add_structures() {
        storage_.structure_loads.reserve(ctx_.structure_loads.size());
        for (const StructureLoad& input : ctx_.structure_loads) {
            KvStructureLoadRow row{};
            row.distance = input.distance;
            row.method = string_ref(input.method);
            row.load_file_path = value(input.load_file_path);
            row.file_path = string_ref(input.file_path);
            row.order = input.order;
            row.metadata = metadata(input.edit_ref, "structure.load");
            storage_.structure_loads.push_back(row);
        }
        storage_.structure_puts.reserve(ctx_.structure_puts.size());
        for (const StructurePut& input : ctx_.structure_puts) {
            KvStructurePutRow row{};
            row.distance = input.distance;
            row.method = string_ref(input.method);
            row.structure_key = value(input.structure_key);
            row.track_key = value(input.track_key);
            row.x = input.x; row.y = input.y; row.z = input.z;
            row.rx = input.rx; row.ry = input.ry; row.rz = input.rz;
            row.tilt = input.tilt; row.span = input.span;
            row.file_path = string_ref(input.file_path);
            row.order = input.order;
            row.metadata = metadata(input.edit_ref, "structure.put");
            storage_.structure_puts.push_back(row);
        }
        storage_.structure_betweens.reserve(ctx_.structure_betweens.size());
        for (const StructurePut& input : ctx_.structure_betweens) {
            KvStructureBetweenRow row{};
            row.distance = input.distance;
            row.method = string_ref(input.method);
            row.structure_key = value(input.structure_key);
            row.track_key1 = value(input.track_key1);
            row.track_key2 = value(input.track_key2);
            row.flag = input.flag;
            row.file_path = string_ref(input.file_path);
            row.order = input.order;
            row.metadata = metadata(input.edit_ref, "structure.between");
            storage_.structure_betweens.push_back(row);
        }
        storage_.structure_models.reserve(ctx_.structure_models.size());
        for (const StructureModel& input : ctx_.structure_models) {
            KvStructureModelRow row{};
            row.structure_key = string_ref(input.structure_key);
            row.file_path = string_ref(input.file_path);
            row.metadata = metadata(input.edit_ref, "structure.model",
                                    input.source_file_index);
            storage_.structure_models.push_back(row);
        }
    }

    void add_other_trains() {
        storage_.other_train_definitions.reserve(ctx_.other_trains.size());
        for (const OtherTrainDefinition& input : ctx_.other_trains) {
            KvOtherTrainDefinitionRow row{};
            row.distance = input.distance;
            row.method = string_ref(input.method);
            row.train_key = value(input.train_key);
            row.load_file_path = value(input.load_file_path);
            row.resolved_file_path = string_ref(input.resolved_file_path);
            row.track_key = value(input.track_key);
            row.direction = value(input.direction);
            row.source_file_path = string_ref(input.source_file_path);
            row.order = input.order;
            row.metadata = metadata(input.edit_ref, "otherTrain.definition");
            storage_.other_train_definitions.push_back(row);
        }
        auto add_keys = [&](const std::vector<OtherTrainReferencedKey>& inputs,
                            std::vector<KvReferencedKeyRow>& outputs,
                            const char* kind) {
            outputs.reserve(inputs.size());
            for (const OtherTrainReferencedKey& input : inputs) {
                KvReferencedKeyRow row{};
                row.key = string_ref(input.key);
                row.file_path = string_ref(input.file_path);
                row.metadata = metadata(input.edit_ref, kind);
                outputs.push_back(row);
            }
        };
        add_keys(ctx_.other_train_structure_keys, storage_.other_train_structure_keys,
                 "otherTrain.structureKey");
        add_keys(ctx_.other_train_sound_3d_keys, storage_.other_train_sound_3d_keys,
                 "otherTrain.sound3DKey");
        storage_.other_train_enables.reserve(ctx_.other_train_enables.size());
        for (const OtherTrainEnable& input : ctx_.other_train_enables) {
            KvOtherTrainEnableRow row{};
            row.distance = input.distance;
            row.train_key = value(input.train_key);
            row.time = value(input.time);
            row.file_path = string_ref(input.file_path);
            row.order = input.order;
            row.metadata = metadata(input.edit_ref, "otherTrain.enable");
            storage_.other_train_enables.push_back(row);
        }
        storage_.other_train_stops.reserve(ctx_.other_train_stops.size());
        for (const OtherTrainStop& input : ctx_.other_train_stops) {
            KvOtherTrainStopRow row{};
            row.distance = input.distance;
            row.train_key = value(input.train_key);
            row.decelerate = value(input.decelerate);
            row.stop_time = value(input.stop_time);
            row.accelerate = value(input.accelerate);
            row.speed = value(input.speed);
            row.file_path = string_ref(input.file_path);
            row.order = input.order;
            row.metadata = metadata(input.edit_ref, "otherTrain.stop");
            storage_.other_train_stops.push_back(row);
        }
    }

    void add_sections_signals_and_sounds() {
        auto add_sections = [&](const std::vector<SectionBegin>& inputs) {
            storage_.section_begins.reserve(inputs.size());
            for (const SectionBegin& input : inputs) {
                KvSectionRow row{};
                row.distance = input.distance;
                row.method = string_ref(input.method);
                row.values = append_values(input.signal_indices);
                row.file_path = string_ref(input.file_path);
                row.order = input.order;
                row.metadata = metadata(input.edit_ref, "section.begin");
                storage_.section_begins.push_back(row);
            }
        };
        add_sections(ctx_.section_begins);
        storage_.section_speed_limits.reserve(ctx_.section_speed_limits.size());
        for (const SectionSpeedLimit& input : ctx_.section_speed_limits) {
            KvSectionRow row{};
            row.distance = input.distance;
            row.method = string_ref(input.method);
            row.values = append_values(input.speeds);
            row.file_path = string_ref(input.file_path);
            row.order = input.order;
            row.metadata = metadata(input.edit_ref, "section.speedLimit");
            storage_.section_speed_limits.push_back(row);
        }
        storage_.signal_aspects.reserve(ctx_.signal_aspects.size());
        for (const SignalAspect& input : ctx_.signal_aspects) {
            KvSignalAspectRow row{};
            row.signal_aspect_key = string_ref(input.signal_aspect_key);
            row.structure_keys = append_strings(input.structure_keys);
            row.metadata = metadata(input.edit_ref, "signal.aspect");
            if (input.main_structure_key_count >
                    input.structure_keys.size() ||
                input.main_structure_key_count >
                std::numeric_limits<std::uint32_t>::max()) {
                throw std::runtime_error(
                    "Signal aspect main-row structure-key count is invalid");
            }
            row.metadata.reserved = static_cast<std::uint32_t>(
                input.main_structure_key_count);
            storage_.signal_aspects.push_back(row);
        }
        storage_.signal_puts.reserve(ctx_.signal_puts.size());
        for (const SignalPut& input : ctx_.signal_puts) {
            KvSignalPutRow row{};
            row.distance = input.distance;
            row.signal_aspect_key = value(input.signal_aspect_key);
            row.section = value(input.section);
            row.track_key = value(input.track_key);
            row.x = input.x; row.y = input.y; row.z = input.z;
            row.rx = input.rx; row.ry = input.ry; row.rz = input.rz;
            row.tilt = input.tilt; row.span = input.span;
            row.file_path = string_ref(input.file_path);
            row.order = input.order;
            row.metadata = metadata(input.edit_ref, "signal.put");
            storage_.signal_puts.push_back(row);
        }
        storage_.beacons.reserve(ctx_.beacons.size());
        for (const BeaconPut& input : ctx_.beacons) {
            KvBeaconRow row{};
            row.distance = input.distance;
            row.type = value(input.type);
            row.section = value(input.section);
            row.send_data = value(input.send_data);
            row.file_path = string_ref(input.file_path);
            row.order = input.order;
            row.metadata = metadata(input.edit_ref, "beacon.put");
            storage_.beacons.push_back(row);
        }
        storage_.pretrains.reserve(ctx_.pretrains.size());
        for (const PreTrainPass& input : ctx_.pretrains) {
            KvPreTrainRow row{};
            row.distance = input.distance;
            row.pass_time = value(input.pass_time);
            row.file_path = string_ref(input.file_path);
            row.order = input.order;
            row.metadata = metadata(input.edit_ref, "preTrain.pass");
            storage_.pretrains.push_back(row);
        }
        storage_.sound_list.reserve(ctx_.sound_list.size());
        for (const SoundListEntry& input : ctx_.sound_list) {
            KvSoundListRow row{};
            row.sound_key = string_ref(input.sound_key);
            row.file_path = string_ref(input.file_path);
            row.buffer_count = input.buffer_count;
            row.is_3d = input.is_3d ? 1u : 0u;
            row.metadata = metadata(input.edit_ref,
                input.is_3d ? "sound3D.list" : "sound.list",
                input.source_file_index);
            storage_.sound_list.push_back(row);
        }
        storage_.map_sounds.reserve(ctx_.map_sounds.size());
        for (const MapSoundPlay& input : ctx_.map_sounds) {
            KvMapSoundRow row{};
            row.distance = input.distance;
            row.sound_key = value(input.sound_key);
            row.file_path = string_ref(input.file_path);
            row.order = input.order;
            row.metadata = metadata(input.edit_ref, "mapSound.play");
            storage_.map_sounds.push_back(row);
        }
        storage_.map_sounds_3d.reserve(ctx_.map_sound_3d.size());
        for (const MapSound3DPut& input : ctx_.map_sound_3d) {
            KvMapSound3DRow row{};
            row.distance = input.distance;
            row.sound_key = value(input.sound_key);
            row.x = input.x; row.y = input.y;
            row.file_path = string_ref(input.file_path);
            row.order = input.order;
            row.metadata = metadata(input.edit_ref, "mapSound3D.put");
            storage_.map_sounds_3d.push_back(row);
        }
    }

    template <typename InputRow>
    void add_noise_rows(const std::vector<InputRow>& inputs,
                        std::vector<KvNoiseRow>& outputs,
                        const char* kind) {
        outputs.reserve(inputs.size());
        for (const InputRow& input : inputs) {
            KvNoiseRow row{};
            row.distance = input.distance;
            row.index = value(input.index);
            row.file_path = string_ref(input.file_path);
            row.order = input.order;
            row.metadata = metadata(input.edit_ref, kind);
            outputs.push_back(row);
        }
    }

    void add_environment() {
        add_noise_rows(ctx_.rolling_noises, storage_.rolling_noises, "rollingNoise.change");
        add_noise_rows(ctx_.flange_noises, storage_.flange_noises, "flangeNoise.change");
        add_noise_rows(ctx_.joint_noises, storage_.joint_noises, "jointNoise.play");
        storage_.repeaters.reserve(ctx_.repeaters.size());
        for (const RepeaterEvent& input : ctx_.repeaters) {
            KvRepeaterRow row{};
            row.distance = input.distance;
            row.method = string_ref(input.method);
            row.repeater_key = value(input.repeater_key);
            row.track_key = value(input.track_key);
            row.x = input.x; row.y = input.y; row.z = input.z;
            row.rx = input.rx; row.ry = input.ry; row.rz = input.rz;
            row.tilt = input.tilt; row.span = input.span; row.interval = input.interval;
            row.structure_keys = append_values(input.structure_keys);
            row.file_path = string_ref(input.file_path);
            row.order = input.order;
            row.metadata = metadata(input.edit_ref, "repeater");
            storage_.repeaters.push_back(row);
        }
        storage_.irregularities.reserve(ctx_.irregularities.size());
        for (const IrregularityChange& input : ctx_.irregularities) {
            KvIrregularityRow row{};
            row.distance = input.distance;
            row.x = input.x; row.y = input.y; row.r = input.r;
            row.lx = input.lx; row.ly = input.ly; row.lr = input.lr;
            row.file_path = string_ref(input.file_path);
            row.order = input.order;
            row.metadata = metadata(input.edit_ref, "irregularity.change");
            storage_.irregularities.push_back(row);
        }
        storage_.backgrounds.reserve(ctx_.backgrounds.size());
        for (const BackgroundChange& input : ctx_.backgrounds) {
            KvBackgroundRow row{};
            row.distance = input.distance;
            row.structure_key = value(input.structure_key);
            row.file_path = string_ref(input.file_path);
            row.order = input.order;
            row.metadata = metadata(input.edit_ref, "background.change");
            storage_.backgrounds.push_back(row);
        }
        storage_.adhesions.reserve(ctx_.adhesions.size());
        for (const AdhesionChange& input : ctx_.adhesions) {
            KvAdhesionRow row{};
            row.distance = input.distance;
            row.a = value(input.a); row.b = value(input.b); row.c = value(input.c);
            row.file_path = string_ref(input.file_path);
            row.order = input.order;
            row.metadata = metadata(input.edit_ref, "adhesion.change");
            storage_.adhesions.push_back(row);
        }
        storage_.cab_illuminance.reserve(ctx_.cab_illuminance.size());
        for (const CabIlluminanceChange& input : ctx_.cab_illuminance) {
            KvCabIlluminanceRow row{};
            row.distance = input.distance;
            row.value = value(input.value);
            row.file_path = string_ref(input.file_path);
            row.order = input.order;
            row.metadata = metadata(input.edit_ref, "cabIlluminance.change");
            storage_.cab_illuminance.push_back(row);
        }
        storage_.fogs.reserve(ctx_.fogs.size());
        for (const FogChange& input : ctx_.fogs) {
            KvFogRow row{};
            row.distance = input.distance;
            row.density = value(input.density); row.red = value(input.red);
            row.green = value(input.green); row.blue = value(input.blue);
            row.file_path = string_ref(input.file_path);
            row.order = input.order;
            row.metadata = metadata(input.edit_ref, "fog.change");
            storage_.fogs.push_back(row);
        }
        storage_.draw_distances.reserve(ctx_.draw_distances.size());
        for (const DrawDistanceChange& input : ctx_.draw_distances) {
            KvDrawDistanceRow row{};
            row.distance = input.distance;
            row.value = input.value;
            row.file_path = string_ref(input.file_path);
            row.order = input.order;
            row.metadata = metadata(input.edit_ref, "drawDistance.change");
            storage_.draw_distances.push_back(row);
        }
        storage_.speed_limits.reserve(ctx_.speedlimits.size());
        for (const SpeedLimitEvent& input : ctx_.speedlimits) {
            KvSpeedLimitRow row{};
            row.distance = input.distance;
            row.speed = value(input.speed);
            row.file_path = string_ref(input.file_path);
            row.order = input.order;
            row.metadata = metadata(input.edit_ref, "speedlimit");
            storage_.speed_limits.push_back(row);
        }
    }

    void add_preview_rows() {
        storage_.variable_assignments.reserve(ctx_.variable_assignments.size());
        for (const VariableAssignment& input : ctx_.variable_assignments) {
            KvVariableAssignmentRow row{};
            row.normalized_name = string_ref(input.normalized_name);
            row.source_name = string_ref(input.source_name);
            row.value = value(input.value);
            row.expression = string_ref(input.expression);
            row.file_path = string_ref(input.file_path);
            row.order = input.order;
            storage_.variable_assignments.push_back(row);
        }
        storage_.resource_list_loads.reserve(ctx_.resource_list_loads.size());
        for (const ResourceListLoad& input : ctx_.resource_list_loads) {
            KvResourceListLoadRow row{};
            row.kind = static_cast<std::uint32_t>(input.kind);
            row.evaluated_path = string_ref(input.evaluated_path);
            row.raw_argument = string_ref(input.raw_argument);
            row.resolved_path = string_ref(input.resolved_path);
            row.file_path = string_ref(input.file_path);
            row.order = input.order;
            storage_.resource_list_loads.push_back(row);
        }
    }

    void add_element(const std::string& row_kind, size_t row_index,
                     const EditSourceRef& ref) {
        if (!ref.valid() || ref.statement_index >= ctx_.parsed_statements.size()) return;
        const ParsedStatement& statement = ctx_.parsed_statements[ref.statement_index];
        KvElementRow row{};
        row.edit_id = string_ref(element_edit_id(ctx_, ref, row_kind));
        if (row.edit_id.length == 0) return;
        row.row_kind = string_ref(row_kind);
        row.row_index = static_cast<std::uint64_t>(row_index);
        row.source_file_index = static_cast<std::uint64_t>(statement.source.source_file_index);
        row.global_order = statement.global_order;
        storage_.elements.push_back(row);
    }

    template <typename Rows>
    void add_elements(const char* row_kind, const Rows& rows) {
        for (size_t i = 0; i < rows.size(); ++i) add_element(row_kind, i, rows[i].edit_ref);
    }

    void add_edit_registry() {
        storage_.statements.reserve(ctx_.parsed_statements.size());
        for (ParsedStatement& input : ctx_.parsed_statements) {
            KvStatementRow row{};
            row.edit_id = string_ref(statement_edit_id(ctx_, input));
            row.statement_kind = string_ref(input.statement_kind);
            row.source.source_file_index = static_cast<std::uint64_t>(input.source.source_file_index);
            row.source.byte_start = static_cast<std::uint64_t>(input.source.byte_start);
            row.source.byte_end = static_cast<std::uint64_t>(input.source.byte_end);
            row.source.line = input.source.line;
            row.source.column = input.source.column;
            row.source.line_end = input.source.line_end;
            row.source.column_end = input.source.column_end;
            row.source.include_stack = append_strings(source_include_stack(ctx_, input.source));
            row.source.include_invocation_key = string_ref(
                source_include_invocation_key(ctx_, input.source));
            row.raw_text = string_ref(input.raw_text);
            row.raw_text_preview = string_ref(input.raw_text_preview);
            row.raw_arguments = string_ref(input.raw_arguments);
            row.evaluated_values = append_values(input.evaluated_values);
            row.distance_expression = string_ref(input.distance_expression);
            row.distance_value = input.distance_value;
            row.global_order = input.global_order;
            storage_.statements.push_back(row);
        }

        add_elements("own_track", ctx_.own_track);
        for (const std::string& key : ctx_.othertrack_order) {
            auto found = ctx_.othertrack.find(key);
            if (found == ctx_.othertrack.end()) continue;
            const std::string row_kind = "othertrack." + key;
            for (size_t i = 0; i < found->second.size(); ++i) {
                add_element(row_kind, i, found->second[i].edit_ref);
            }
        }
        add_elements("station.put", ctx_.station_puts);
        const auto& station_list_entries = ordered_station_list_entries(ctx_);
        for (size_t station_index = 0; station_index < station_list_entries.size(); ++station_index) {
            add_element("station.list", station_index,
                        station_list_entries[station_index]->edit_ref);
        }
        add_elements("structure.load", ctx_.structure_loads);
        add_elements("structure.put", ctx_.structure_puts);
        add_elements("structure.between", ctx_.structure_betweens);
        add_elements("structure.model", ctx_.structure_models);
        add_elements("otherTrain.definition", ctx_.other_trains);
        add_elements("otherTrain.structureKey", ctx_.other_train_structure_keys);
        add_elements("otherTrain.sound3DKey", ctx_.other_train_sound_3d_keys);
        add_elements("otherTrain.enable", ctx_.other_train_enables);
        add_elements("otherTrain.stop", ctx_.other_train_stops);
        add_elements("section.begin", ctx_.section_begins);
        add_elements("section.speedLimit", ctx_.section_speed_limits);
        add_elements("signal.aspect", ctx_.signal_aspects);
        add_elements("signal.put", ctx_.signal_puts);
        add_elements("beacon.put", ctx_.beacons);
        add_elements("preTrain.pass", ctx_.pretrains);
        size_t sound_index = 0;
        size_t sound_3d_index = 0;
        for (const SoundListEntry& row : ctx_.sound_list) {
            const bool is_3d = row.is_3d;
            add_element(is_3d ? "sound3D.list" : "sound.list",
                        is_3d ? sound_3d_index++ : sound_index++, row.edit_ref);
        }
        add_elements("mapSound.play", ctx_.map_sounds);
        add_elements("mapSound3D.put", ctx_.map_sound_3d);
        add_elements("rollingNoise.change", ctx_.rolling_noises);
        add_elements("flangeNoise.change", ctx_.flange_noises);
        add_elements("jointNoise.play", ctx_.joint_noises);
        add_elements("repeater", ctx_.repeaters);
        add_elements("irregularity.change", ctx_.irregularities);
        add_elements("background.change", ctx_.backgrounds);
        add_elements("adhesion.change", ctx_.adhesions);
        add_elements("cabIlluminance.change", ctx_.cab_illuminance);
        add_elements("fog.change", ctx_.fogs);
        add_elements("drawDistance.change", ctx_.draw_distances);
        add_elements("speedlimit", ctx_.speedlimits);
    }

    template <typename T>
    static void bind(const std::vector<T>& input, const T*& pointer, std::uint64_t& count) {
        pointer = data_or_null(input);
        count = static_cast<std::uint64_t>(input.size());
    }

    void finalize() {
        KvMapSnapshot& view = storage_.view;
        view.version = KV_MAP_SNAPSHOT_VERSION;
        view.capabilities = KV_MAP_CAP_PREVIEW_DATA | KV_MAP_CAP_REGULAR_GEOMETRY;
        if (ctx_.parse_options.collect_edit_metadata) {
            view.capabilities |= KV_MAP_CAP_EDIT_METADATA | KV_MAP_CAP_FULL_STATEMENT_SOURCE;
        }
        view.structure_size = sizeof(KvMapSnapshot);
        view.content_revision = ctx_.content_revision;
        view.geometry_revision = ctx_.geometry_revision;
        view.string_data = storage_.string_arena.empty() ? nullptr : storage_.string_arena.data();
        view.string_size = static_cast<std::uint64_t>(storage_.string_arena.size());
        view.values = data_or_null(storage_.values);
        view.value_count = static_cast<std::uint64_t>(storage_.values.size());
        view.string_refs = data_or_null(storage_.string_refs);
        view.string_ref_count = static_cast<std::uint64_t>(storage_.string_refs.size());
        bind(storage_.file_structure, view.file_structure, view.file_structure_count);
        bind(storage_.source_files, view.source_files, view.source_file_count);
        view.controlpoints = ctx_.controlpoints.empty() ? nullptr : ctx_.controlpoints.data();
        view.controlpoint_count = static_cast<std::uint64_t>(ctx_.controlpoints.size());
        std::copy(ctx_.cp_arbdistribution.begin(), ctx_.cp_arbdistribution.end(), view.cp_arbdistribution);
        std::copy(ctx_.cp_arbdistribution_default.begin(), ctx_.cp_arbdistribution_default.end(),
                  view.cp_arbdistribution_default);
        std::copy(ctx_.cp_defaultrange.begin(), ctx_.cp_defaultrange.end(), view.cp_default_range);
        view.own_track_geometry = matrix_view(ctx_.owntrack_buffer);
        view.curve_radius_geometry = matrix_view(ctx_.curveradius_buffer);
        view.structure_put_geometry = matrix_view(ctx_.structure_put_buffer);
        bind(storage_.other_tracks, view.other_tracks, view.other_track_count);
        bind(storage_.own_track_events, view.own_track_events, view.own_track_event_count);
        bind(storage_.other_track_events, view.other_track_events, view.other_track_event_count);
        bind(storage_.station_positions, view.station_positions, view.station_position_count);
        bind(storage_.station_names, view.station_names, view.station_name_count);
        bind(storage_.station_puts, view.station_puts, view.station_put_count);
        bind(storage_.station_list, view.station_list, view.station_list_count);
        bind(storage_.structure_loads, view.structure_loads, view.structure_load_count);
        bind(storage_.structure_puts, view.structure_puts, view.structure_put_count);
        bind(storage_.structure_betweens, view.structure_betweens, view.structure_between_count);
        bind(storage_.structure_models, view.structure_models, view.structure_model_count);
        bind(storage_.other_train_definitions, view.other_train_definitions,
             view.other_train_definition_count);
        bind(storage_.other_train_structure_keys, view.other_train_structure_keys,
             view.other_train_structure_key_count);
        bind(storage_.other_train_sound_3d_keys, view.other_train_sound_3d_keys,
             view.other_train_sound_3d_key_count);
        bind(storage_.other_train_enables, view.other_train_enables, view.other_train_enable_count);
        bind(storage_.other_train_stops, view.other_train_stops, view.other_train_stop_count);
        bind(storage_.section_begins, view.section_begins, view.section_begin_count);
        bind(storage_.section_speed_limits, view.section_speed_limits, view.section_speed_limit_count);
        bind(storage_.signal_aspects, view.signal_aspects, view.signal_aspect_count);
        bind(storage_.signal_puts, view.signal_puts, view.signal_put_count);
        bind(storage_.beacons, view.beacons, view.beacon_count);
        bind(storage_.pretrains, view.pretrains, view.pretrain_count);
        bind(storage_.sound_list, view.sound_list, view.sound_list_count);
        bind(storage_.map_sounds, view.map_sounds, view.map_sound_count);
        bind(storage_.map_sounds_3d, view.map_sounds_3d, view.map_sound_3d_count);
        bind(storage_.rolling_noises, view.rolling_noises, view.rolling_noise_count);
        bind(storage_.flange_noises, view.flange_noises, view.flange_noise_count);
        bind(storage_.joint_noises, view.joint_noises, view.joint_noise_count);
        bind(storage_.repeaters, view.repeaters, view.repeater_count);
        bind(storage_.irregularities, view.irregularities, view.irregularity_count);
        bind(storage_.backgrounds, view.backgrounds, view.background_count);
        bind(storage_.adhesions, view.adhesions, view.adhesion_count);
        bind(storage_.cab_illuminance, view.cab_illuminance, view.cab_illuminance_count);
        bind(storage_.fogs, view.fogs, view.fog_count);
        bind(storage_.draw_distances, view.draw_distances, view.draw_distance_count);
        bind(storage_.speed_limits, view.speed_limits, view.speed_limit_count);
        bind(storage_.variable_assignments, view.variable_assignments,
             view.variable_assignment_count);
        bind(storage_.resource_list_loads, view.resource_list_loads,
             view.resource_list_load_count);
        bind(storage_.statements, view.statements, view.statement_count);
        bind(storage_.elements, view.elements, view.element_count);
    }

    MapContext& ctx_;
    MapSnapshotStorage& storage_;
    std::unordered_map<std::string, KvStringRef> strings_;
};

} // namespace

void invalidate_map_snapshot(MapContext& ctx, bool content_changed,
                             bool geometry_changed) {
    if (content_changed) ++ctx.content_revision;
    if (geometry_changed) ++ctx.geometry_revision;
    ctx.map_snapshot.reset();
    ctx.edit_target_snapshot.reset();
}

void invalidate_scene_geometry_snapshot(MapContext& ctx, bool scene_changed) {
    ctx.scene_snapshot.reset();
    if (!scene_changed) return;

    ++ctx.scene_revision;
    ctx.scene_geometry_valid = false;
    ctx.scene_geometry_parameters = {};
    ctx.scene_owntrack_buffer = {};
    ctx.scene_othertrack_buffers.clear();
}

const KvMapSnapshot& build_map_snapshot(MapContext& ctx) {
    if (ctx.map_snapshot &&
        ctx.map_snapshot->view.content_revision == ctx.content_revision &&
        ctx.map_snapshot->view.geometry_revision == ctx.geometry_revision) {
        return ctx.map_snapshot->view;
    }
    const auto started_at = SteadyClock::now();
    auto storage = std::make_unique<MapSnapshotStorage>();
    MapSnapshotBuilder(ctx, *storage).build();
    const double elapsed = elapsed_seconds_since(started_at);
    storage->view.build_seconds = elapsed;
    ctx.timing.snapshot_seconds += elapsed;
    ctx.map_snapshot = std::move(storage);
    return ctx.map_snapshot->view;
}

const KvSceneGeometrySnapshot& build_scene_geometry_snapshot(MapContext& ctx) {
    if (ctx.scene_snapshot &&
        ctx.scene_snapshot->view.content_revision == ctx.content_revision &&
        ctx.scene_snapshot->view.scene_revision == ctx.scene_revision) {
        return ctx.scene_snapshot->view;
    }
    const auto started_at = SteadyClock::now();
    auto storage = std::make_unique<SceneGeometrySnapshotStorage>();
    storage->string_arena.reserve(ctx.othertrack_order.size() * 16);
    storage->other_tracks.reserve(ctx.othertrack_order.size());
    for (const std::string& key : ctx.othertrack_order) {
        KvSceneTrackRow row{};
        row.key.offset = static_cast<std::uint64_t>(storage->string_arena.size());
        row.key.length = static_cast<std::uint64_t>(key.size());
        storage->string_arena.append(key);
        auto geometry = ctx.scene_othertrack_buffers.find(key);
        if (geometry != ctx.scene_othertrack_buffers.end()) row.points = matrix_view(geometry->second);
        storage->other_tracks.push_back(row);
    }
    KvSceneGeometrySnapshot& view = storage->view;
    view.version = KV_SCENE_GEOMETRY_SNAPSHOT_VERSION;
    view.structure_size = sizeof(KvSceneGeometrySnapshot);
    view.content_revision = ctx.content_revision;
    view.scene_revision = ctx.scene_revision;
    view.string_data = storage->string_arena.empty() ? nullptr : storage->string_arena.data();
    view.string_size = static_cast<std::uint64_t>(storage->string_arena.size());
    view.own_track = matrix_view(ctx.scene_owntrack_buffer);
    view.other_tracks = data_or_null(storage->other_tracks);
    view.other_track_count = static_cast<std::uint64_t>(storage->other_tracks.size());
    view.build_seconds = elapsed_seconds_since(started_at);
    ctx.scene_snapshot = std::move(storage);
    return ctx.scene_snapshot->view;
}

std::vector<const StationListEntry*> ordered_station_list_entries(const MapContext& ctx) {
    std::vector<const StationListEntry*> entries;
    entries.reserve(ctx.station_list.size());
    for (const StationListEntry& row : ctx.station_list) {
        entries.push_back(&row);
    }
    std::stable_sort(entries.begin(), entries.end(),
                     [](const StationListEntry* left, const StationListEntry* right) {
                         return left->order < right->order;
                     });
    return entries;
}

} // namespace kme::maploader::detail
