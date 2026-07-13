/*
 * Copyright (c) 2026 Sapporo_ningyo
 *
 * Portions of the map parsing and track-geometry design are derived from or
 * reimplemented with reference to kobushi-trackviewer, Copyright (c) 2021-2024
 * konawasabi, licensed under Apache License 2.0.
 *
 * Licensed under Apache License 2.0; see LICENSE and NOTICE.
 */

#include "maploader_internal.h"

namespace kme::maploader::detail {

std::uint64_t stable_hash64(const std::string& text) {
    std::uint64_t hash = 14695981039346656037ull;
    for (unsigned char ch : text) {
        hash ^= static_cast<std::uint64_t>(ch);
        hash *= 1099511628211ull;
    }
    return hash;
}

std::string hex64(std::uint64_t value) {
    std::ostringstream out;
    out << std::hex << std::setw(16) << std::setfill('0') << value;
    return out.str();
}

std::string edit_kind_token(std::string kind) {
    for (char& ch : kind) {
        unsigned char c = static_cast<unsigned char>(ch);
        if (std::isalnum(c)) {
            ch = static_cast<char>(std::tolower(c));
        } else {
            ch = '_';
        }
    }
    while (!kind.empty() && kind.back() == '_') kind.pop_back();
    return kind.empty() ? "row" : kind;
}

std::string make_edit_id(const std::string& source_key, int global_order,
                         const std::string& kind, int element_index) {
    std::string key = source_key + "|" +
                      std::to_string(global_order) + "|" +
                      ascii_lower(kind) + "|" +
                      std::to_string(element_index);
    return "edit-" + hex64(stable_hash64(key)) + "-" +
           std::to_string(global_order) + "-" +
           edit_kind_token(kind) + "-" + std::to_string(element_index);
}

std::string statement_edit_id(MapContext& ctx, ParsedStatement& statement) {
    if (statement.edit_id.empty()) {
        statement.edit_id = make_edit_id(source_file_key(ctx, statement.source),
                                         statement.global_order,
                                         "statement." + statement.statement_kind,
                                         0);
    }
    return statement.edit_id;
}

std::string native_element_edit_id(const MapContext& ctx, const EditSourceRef& ref,
                                   const std::string& row_kind) {
    if (!ref.valid() || ref.statement_index >= ctx.parsed_statements.size()) return {};
    const ParsedStatement& statement = ctx.parsed_statements[ref.statement_index];
    return make_edit_id(source_file_key(ctx, statement.source),
                        statement.global_order, row_kind, ref.element_index);
}

std::string element_edit_id(const MapContext& ctx, const EditSourceRef& ref,
                            const std::string& row_kind) {
    if (!ref.valid() || ref.statement_index >= ctx.parsed_statements.size()) return {};
    std::string cache_key = std::to_string(ref.statement_index) + "|" +
                            std::to_string(ref.element_index) + "|" + row_kind;
    auto cached = ctx.element_edit_id_cache.find(cache_key);
    if (cached != ctx.element_edit_id_cache.end()) return cached->second;
    std::string edit_id = native_element_edit_id(ctx, ref, row_kind);
    auto stable = ctx.native_element_edit_id_to_stable.find(edit_id);
    if (stable != ctx.native_element_edit_id_to_stable.end()) {
        edit_id = stable->second;
    }
    auto inserted = ctx.element_edit_id_cache.emplace(std::move(cache_key), std::move(edit_id));
    return inserted.first->second;
}

void append_source_span_json(std::ostringstream& out, const MapContext& ctx,
                             const SourceSpan& span, bool include_full) {
    out << "{\"filePath\":";
    append_json_string(out, source_file_path(ctx, span));
    out << ",\"line\":" << span.line
        << ",\"column\":" << span.column;
    if (include_full) {
        const std::vector<std::string>& include_stack = source_include_stack(ctx, span);
        out << ",\"includeStack\":[";
        for (size_t i = 0; i < include_stack.size(); ++i) {
            if (i) out << ",";
            append_json_string(out, include_stack[i]);
        }
        out << "],\"encoding\":";
        append_json_string(out, source_file_encoding(ctx, span));
        out << ",\"newline\":";
        append_json_string(out, source_file_newline(ctx, span));
        out << ",\"byteStart\":" << span.byte_start
            << ",\"byteEnd\":" << span.byte_end
            << ",\"lineEnd\":" << span.line_end
            << ",\"columnEnd\":" << span.column_end;
    }
    out << "}";
}

void append_edit_fields(std::ostringstream& out, const MapContext& ctx,
                        const EditSourceRef& ref, const std::string& row_kind) {
    if (!ref.valid() || ref.statement_index >= ctx.parsed_statements.size()) return;
    const ParsedStatement& statement = ctx.parsed_statements[ref.statement_index];
    out << ",\"editId\":";
    append_json_string(out, element_edit_id(ctx, ref, row_kind));
    out << ",\"source\":{\"filePath\":";
    append_json_string(out, source_file_path(ctx, statement.source));
    out << ",\"line\":" << statement.source.line
        << ",\"column\":" << statement.source.column
        << ",\"rawTextPreview\":";
    append_json_string(out, statement.raw_text_preview);
    out << "}";
}

void append_event_json(std::ostringstream& out, const MapContext& ctx, const OwnTrackEvent& e) {
    out << "{\"distance\":" << json_number(e.distance)
        << ",\"key\":\"" << json_escape(e.key)
        << "\",\"value\":" << json_value(e.value)
        << ",\"flag\":\"" << json_escape(e.flag) << "\"";
    append_edit_fields(out, ctx, e.edit_ref, "own_track");
    out << "}";
}

void append_other_json(std::ostringstream& out, const MapContext& ctx, const OtherTrackEvent& e) {
    out << "{\"distance\":" << json_number(e.distance)
        << ",\"key\":\"" << json_escape(e.key)
        << "\",\"value\":" << json_value(e.value)
        << ",\"flag\":\"" << json_escape(e.flag) << "\"";
    append_edit_fields(out, ctx, e.edit_ref, "othertrack");
    out << "}";
}

void append_structure_put_json(std::ostringstream& out, const MapContext& ctx,
                               const StructurePut& row, bool between) {
    out << "{\"distance\":" << json_number(row.distance)
        << ",\"method\":\"" << json_escape(row.method) << "\""
        << ",\"structureKey\":" << json_value(row.structure_key);
    if (between) {
        out << ",\"trackKey1\":" << json_value(row.track_key1)
            << ",\"trackKey2\":" << json_value(row.track_key2)
            << ",\"flag\":" << json_number(row.flag);
    } else {
        out << ",\"trackKey\":" << json_value(row.track_key)
            << ",\"x\":" << json_number(row.x)
            << ",\"y\":" << json_number(row.y)
            << ",\"z\":" << json_number(row.z)
            << ",\"rx\":" << json_number(row.rx)
            << ",\"ry\":" << json_number(row.ry)
            << ",\"rz\":" << json_number(row.rz)
            << ",\"tilt\":" << json_number(row.tilt)
            << ",\"span\":" << json_number(row.span);
    }
    out << ",\"filePath\":\"" << json_escape(row.file_path)
        << "\",\"order\":" << row.order;
    append_edit_fields(out, ctx, row.edit_ref, between ? "structure.between" : "structure.put");
    out << "}";
}

void append_value_array_json(std::ostringstream& out, const std::vector<Value>& values) {
    out << "[";
    for (size_t i = 0; i < values.size(); ++i) {
        if (i) out << ",";
        out << json_value(values[i]);
    }
    out << "]";
}

void append_section_begin_json(std::ostringstream& out, const MapContext& ctx, const SectionBegin& row) {
    out << "{\"distance\":" << json_number(row.distance)
        << ",\"signalIndices\":";
    append_value_array_json(out, row.signal_indices);
    out << ",\"filePath\":\"" << json_escape(row.file_path)
        << "\",\"order\":" << row.order;
    append_edit_fields(out, ctx, row.edit_ref, "section.begin");
    out << "}";
}

void append_section_speed_limit_json(std::ostringstream& out, const MapContext& ctx, const SectionSpeedLimit& row) {
    out << "{\"distance\":" << json_number(row.distance)
        << ",\"speeds\":";
    append_value_array_json(out, row.speeds);
    out << ",\"filePath\":\"" << json_escape(row.file_path)
        << "\",\"order\":" << row.order;
    append_edit_fields(out, ctx, row.edit_ref, "section.speedLimit");
    out << "}";
}

void append_signal_put_json(std::ostringstream& out, const MapContext& ctx, const SignalPut& row) {
    out << "{\"distance\":" << json_number(row.distance)
        << ",\"signalAspectKey\":" << json_value(row.signal_aspect_key)
        << ",\"section\":" << json_value(row.section)
        << ",\"trackKey\":" << json_value(row.track_key)
        << ",\"x\":" << json_number(row.x)
        << ",\"y\":" << json_number(row.y)
        << ",\"z\":" << json_number(row.z)
        << ",\"rx\":" << json_number(row.rx)
        << ",\"ry\":" << json_number(row.ry)
        << ",\"rz\":" << json_number(row.rz)
        << ",\"tilt\":" << json_number(row.tilt)
        << ",\"span\":" << json_number(row.span)
        << ",\"filePath\":\"" << json_escape(row.file_path)
        << "\",\"order\":" << row.order;
    append_edit_fields(out, ctx, row.edit_ref, "signal.put");
    out << "}";
}

void append_beacon_json(std::ostringstream& out, const MapContext& ctx, const BeaconPut& row) {
    out << "{\"distance\":" << json_number(row.distance)
        << ",\"type\":" << json_value(row.type)
        << ",\"section\":" << json_value(row.section)
        << ",\"sendData\":" << json_value(row.send_data)
        << ",\"filePath\":\"" << json_escape(row.file_path)
        << "\",\"order\":" << row.order;
    append_edit_fields(out, ctx, row.edit_ref, "beacon.put");
    out << "}";
}

void append_pretrain_json(std::ostringstream& out, const MapContext& ctx, const PreTrainPass& row) {
    out << "{\"distance\":" << json_number(row.distance)
        << ",\"passTime\":" << json_value(row.pass_time)
        << ",\"filePath\":\"" << json_escape(row.file_path)
        << "\",\"order\":" << row.order;
    append_edit_fields(out, ctx, row.edit_ref, "preTrain.pass");
    out << "}";
}

void append_station_put_json(std::ostringstream& out, const MapContext& ctx, const StationPut& row) {
    out << "{\"distance\":" << json_number(row.distance)
        << ",\"stationKey\":" << json_value(row.station_key)
        << ",\"door\":" << json_value(row.door)
        << ",\"margin1\":" << json_value(row.margin1)
        << ",\"margin2\":" << json_value(row.margin2)
        << ",\"filePath\":\"" << json_escape(row.file_path)
        << "\",\"order\":" << row.order;
    append_edit_fields(out, ctx, row.edit_ref, "station.put");
    out << "}";
}

void append_adhesion_json(std::ostringstream& out, const MapContext& ctx, const AdhesionChange& row) {
    out << "{\"distance\":" << json_number(row.distance)
        << ",\"a\":" << json_value(row.a)
        << ",\"b\":" << json_value(row.b)
        << ",\"c\":" << json_value(row.c)
        << ",\"filePath\":\"" << json_escape(row.file_path)
        << "\",\"order\":" << row.order;
    append_edit_fields(out, ctx, row.edit_ref, "adhesion.change");
    out << "}";
}

void append_background_json(std::ostringstream& out, const MapContext& ctx, const BackgroundChange& row) {
    out << "{\"distance\":" << json_number(row.distance)
        << ",\"structureKey\":" << json_value(row.structure_key)
        << ",\"filePath\":\"" << json_escape(row.file_path)
        << "\",\"order\":" << row.order;
    append_edit_fields(out, ctx, row.edit_ref, "background.change");
    out << "}";
}

void append_cab_illuminance_json(std::ostringstream& out, const MapContext& ctx, const CabIlluminanceChange& row) {
    out << "{\"distance\":" << json_number(row.distance)
        << ",\"value\":" << json_value(row.value)
        << ",\"filePath\":\"" << json_escape(row.file_path)
        << "\",\"order\":" << row.order;
    append_edit_fields(out, ctx, row.edit_ref, "cabIlluminance.change");
    out << "}";
}

void append_fog_json(std::ostringstream& out, const MapContext& ctx, const FogChange& row) {
    out << "{\"distance\":" << json_number(row.distance)
        << ",\"density\":" << json_value(row.density)
        << ",\"red\":" << json_value(row.red)
        << ",\"green\":" << json_value(row.green)
        << ",\"blue\":" << json_value(row.blue)
        << ",\"filePath\":\"" << json_escape(row.file_path)
        << "\",\"order\":" << row.order;
    append_edit_fields(out, ctx, row.edit_ref, "fog.change");
    out << "}";
}

void append_element_ref_json(std::ostringstream& out, const MapContext& ctx, bool& first,
                             const std::string& row_kind, size_t row_index,
                             const EditSourceRef& ref) {
    if (!ref.valid() || ref.statement_index >= ctx.parsed_statements.size()) return;
    const ParsedStatement& statement = ctx.parsed_statements[ref.statement_index];
    MapElementRef element;
    element.edit_id = element_edit_id(ctx, ref, row_kind);
    element.row_kind = row_kind;
    element.row_index = row_index;
    element.source_file_path = source_file_path(ctx, statement.source);
    element.global_order = statement.global_order;
    if (element.edit_id.empty()) return;
    if (!first) out << ",";
    first = false;
    out << "{\"editId\":";
    append_json_string(out, element.edit_id);
    out << ",\"rowKind\":";
    append_json_string(out, element.row_kind);
    out << ",\"rowIndex\":" << element.row_index
        << ",\"sourceFilePath\":";
    append_json_string(out, element.source_file_path);
    out << ",\"globalOrder\":" << element.global_order << "}";
}

template <typename Rows>
void append_row_element_refs(std::ostringstream& out, const MapContext& ctx, bool& first,
                             const std::string& row_kind, const Rows& rows) {
    for (size_t i = 0; i < rows.size(); ++i) {
        append_element_ref_json(out, ctx, first, row_kind, i, rows[i].edit_ref);
    }
}

unsigned normalize_ir_json_flags(unsigned flags) {
    flags &= (KV_IR_JSON_FULL_EDIT | KV_IR_JSON_FULL_STATEMENT_SOURCE);
    if (flags & KV_IR_JSON_FULL_STATEMENT_SOURCE) flags |= KV_IR_JSON_FULL_EDIT;
    return flags;
}

void append_edit_registry_json(std::ostringstream& out, MapContext& ctx, unsigned flags) {
    out << ",\"edit\":{\"files\":[";
    for (size_t i = 0; i < ctx.source_files.size(); ++i) {
        if (i) out << ",";
        const SourceFileRecord& file = ctx.source_files[i];
        out << "{\"filePath\":";
        append_json_string(out, file.file_path);
        out << ",\"displayPath\":";
        append_json_string(out, file.display_path);
        out << ",\"encoding\":";
        append_json_string(out, file.encoding);
        out << ",\"newline\":";
        append_json_string(out, file.newline);
        out << ",\"sourceHash\":";
        append_json_string(out, file.source_hash);
        out << ",\"byteLength\":" << file.byte_length << "}";
    }
    out << "]";
    if (!(flags & KV_IR_JSON_FULL_EDIT)) {
        out << "}";
        return;
    }

    const bool include_full_statement_source = (flags & KV_IR_JSON_FULL_STATEMENT_SOURCE) != 0;
    out << ",\"statements\":[";
    for (size_t i = 0; i < ctx.parsed_statements.size(); ++i) {
        if (i) out << ",";
        ParsedStatement& statement = ctx.parsed_statements[i];
        out << "{\"editId\":";
        append_json_string(out, statement_edit_id(ctx, statement));
        out << ",\"statementKind\":";
        append_json_string(out, statement.statement_kind);
        out << ",\"source\":";
        append_source_span_json(out, ctx, statement.source, include_full_statement_source);
        if (include_full_statement_source) {
            out << ",\"rawText\":";
            append_json_string(out, statement.raw_text);
            out << ",\"rawArguments\":";
            append_json_string(out, statement.raw_arguments);
            out << ",\"evaluatedValues\":[";
            for (size_t j = 0; j < statement.evaluated_values.size(); ++j) {
                if (j) out << ",";
                out << json_value(statement.evaluated_values[j]);
            }
            out << "],\"distanceExpression\":";
            append_json_string(out, statement.distance_expression);
            out << ",\"distanceValue\":" << json_number(statement.distance_value);
        }
        out << ",\"globalOrder\":" << statement.global_order << "}";
    }
    out << "],\"elements\":[";
    bool first = true;
    append_row_element_refs(out, ctx, first, "own_track", ctx.own_track);
    for (const std::string& key : ctx.othertrack_order) {
        auto it = ctx.othertrack.find(key);
        if (it != ctx.othertrack.end()) {
            append_row_element_refs(out, ctx, first, "othertrack." + key, it->second);
        }
    }
    append_row_element_refs(out, ctx, first, "station.put", ctx.station_puts);
    size_t station_list_index = 0;
    for (const auto& kv : ctx.station_list) {
        append_element_ref_json(out, ctx, first, "station.list", station_list_index++, kv.second.edit_ref);
    }
    append_row_element_refs(out, ctx, first, "structure.load", ctx.structure_loads);
    append_row_element_refs(out, ctx, first, "structure.put", ctx.structure_puts);
    append_row_element_refs(out, ctx, first, "structure.between", ctx.structure_betweens);
    append_row_element_refs(out, ctx, first, "structure.model", ctx.structure_models);
    append_row_element_refs(out, ctx, first, "otherTrain.definition", ctx.other_trains);
    append_row_element_refs(out, ctx, first, "otherTrain.structureKey", ctx.other_train_structure_keys);
    append_row_element_refs(out, ctx, first, "otherTrain.sound3DKey", ctx.other_train_sound_3d_keys);
    append_row_element_refs(out, ctx, first, "otherTrain.enable", ctx.other_train_enables);
    append_row_element_refs(out, ctx, first, "otherTrain.stop", ctx.other_train_stops);
    append_row_element_refs(out, ctx, first, "section.begin", ctx.section_begins);
    append_row_element_refs(out, ctx, first, "section.speedLimit", ctx.section_speed_limits);
    append_row_element_refs(out, ctx, first, "signal.aspect", ctx.signal_aspects);
    append_row_element_refs(out, ctx, first, "signal.put", ctx.signal_puts);
    append_row_element_refs(out, ctx, first, "beacon.put", ctx.beacons);
    append_row_element_refs(out, ctx, first, "preTrain.pass", ctx.pretrains);
    for (size_t i = 0; i < ctx.sound_list.size(); ++i) {
        append_element_ref_json(out, ctx, first,
                                ctx.sound_list[i].is_3d ? "sound3D.list" : "sound.list",
                                i, ctx.sound_list[i].edit_ref);
    }
    append_row_element_refs(out, ctx, first, "mapSound.play", ctx.map_sounds);
    append_row_element_refs(out, ctx, first, "mapSound3D.put", ctx.map_sound_3d);
    append_row_element_refs(out, ctx, first, "rollingNoise.change", ctx.rolling_noises);
    append_row_element_refs(out, ctx, first, "flangeNoise.change", ctx.flange_noises);
    append_row_element_refs(out, ctx, first, "jointNoise.play", ctx.joint_noises);
    append_row_element_refs(out, ctx, first, "repeater", ctx.repeaters);
    append_row_element_refs(out, ctx, first, "irregularity.change", ctx.irregularities);
    append_row_element_refs(out, ctx, first, "background.change", ctx.backgrounds);
    append_row_element_refs(out, ctx, first, "adhesion.change", ctx.adhesions);
    append_row_element_refs(out, ctx, first, "cabIlluminance.change", ctx.cab_illuminance);
    append_row_element_refs(out, ctx, first, "fog.change", ctx.fogs);
    append_row_element_refs(out, ctx, first, "speedlimit", ctx.speedlimits);
    out << "]}";
}

void append_file_structure_json(std::ostringstream& out, const MapContext& ctx) {
    out << ",\"fileStructure\":[";
    for (size_t i = 0; i < ctx.file_structure.size(); ++i) {
        if (i) out << ",";
        const FileStructureRecord& file = ctx.file_structure[i];
        out << "{\"parentIndex\":";
        if (file.parent_index == kNoSourceRef) out << -1;
        else out << file.parent_index;
        out << ",\"includePath\":";
        append_json_string(out, file.include_path);
        out << ",\"absolutePath\":";
        append_json_string(out, file.absolute_path);
        out << "}";
    }
    out << "]";
}


std::string build_ir_json(MapContext& ctx, unsigned flags) {
    flags = normalize_ir_json_flags(flags);
    std::ostringstream out;
    out << "{\"rootpath\":\"" << json_escape(ctx.rootpath_utf8) << "\"";
    append_file_structure_json(out, ctx);
    out << ",\"controlpoints\":[";
    for (size_t i = 0; i < ctx.controlpoints.size(); ++i) {
        if (i) out << ",";
        out << json_number(ctx.controlpoints[i]);
    }
    out << "]";
    out << ",\"cp_arbdistribution\":[" << json_number(ctx.cp_arbdistribution[0]) << ","
        << json_number(ctx.cp_arbdistribution[1]) << "," << json_number(ctx.cp_arbdistribution[2]) << "]";
    out << ",\"cp_arbdistribution_default\":[" << json_number(ctx.cp_arbdistribution_default[0]) << ","
        << json_number(ctx.cp_arbdistribution_default[1]) << "," << json_number(ctx.cp_arbdistribution_default[2]) << "]";
    out << ",\"cp_defaultrange\":[" << json_number(ctx.cp_defaultrange[0]) << ","
        << json_number(ctx.cp_defaultrange[1]) << "]";

    out << ",\"own_track\":[";
    for (size_t i = 0; i < ctx.own_track.size(); ++i) {
        if (i) out << ",";
        append_event_json(out, ctx, ctx.own_track[i]);
    }
    out << "]";

    out << ",\"station\":{\"position\":[";
    bool first = true;
    for (const auto& kv : ctx.station_position) {
        if (!first) out << ",";
        first = false;
        out << "[" << json_number(kv.first) << ",\"" << json_escape(kv.second) << "\"]";
    }
    out << "],\"put\":[";
    for (size_t i = 0; i < ctx.station_puts.size(); ++i) {
        if (i) out << ",";
        append_station_put_json(out, ctx, ctx.station_puts[i]);
    }
    out << "],\"stationkey\":{";
    first = true;
    for (const auto& kv : ctx.station_key) {
        if (!first) out << ",";
        first = false;
        out << "\"" << json_escape(kv.first) << "\":\"" << json_escape(kv.second) << "\"";
    }
    out << "},\"list\":{";
    static const char* station_list_keys[] = {
        "stationKey", "stationName", "arrivalTime", "depertureTime", "stoppageTime",
        "defaultTime", "signalFlag", "alightingTime", "passengers", "arrivalSoundKey",
        "depertureSoundKey", "doorReopen", "stuckInDoor"
    };
    first = true;
    for (const auto& kv : ctx.station_list) {
        if (!first) out << ",";
        first = false;
        out << "\"" << json_escape(kv.first) << "\":{";
        for (size_t i = 0; i < kv.second.fields.size(); ++i) {
            if (i) out << ",";
            out << "\"" << station_list_keys[i] << "\":\"" << json_escape(kv.second.fields[i]) << "\"";
        }
        append_edit_fields(out, ctx, kv.second.edit_ref, "station.list");
        out << "}";
    }
    out << "}}";

    out << ",\"othertrack\":{\"order\":[";
    for (size_t i = 0; i < ctx.othertrack_order.size(); ++i) {
        if (i) out << ",";
        out << "\"" << json_escape(ctx.othertrack_order[i]) << "\"";
    }
    out << "],\"data\":{";
    first = true;
    for (const auto& key : ctx.othertrack_order) {
        if (!first) out << ",";
        first = false;
        out << "\"" << json_escape(key) << "\":[";
        const auto& rows = ctx.othertrack[key];
        for (size_t i = 0; i < rows.size(); ++i) {
            if (i) out << ",";
            append_other_json(out, ctx, rows[i]);
        }
        out << "]";
    }
    out << "},\"cp_range\":{";
    first = true;
    for (const auto& kv : ctx.othertrack_range) {
        if (!first) out << ",";
        first = false;
        out << "\"" << json_escape(kv.first) << "\":{\"min\":" << json_number(kv.second.first)
            << ",\"max\":" << json_number(kv.second.second) << "}";
    }
    out << "}}";

    out << ",\"structure\":{\"loads\":[";
    for (size_t i = 0; i < ctx.structure_loads.size(); ++i) {
        if (i) out << ",";
        const auto& row = ctx.structure_loads[i];
        out << "{\"distance\":" << json_number(row.distance)
            << ",\"method\":\"Load\",\"loadFilePath\":" << json_value(row.load_file_path)
            << ",\"filePath\":\"" << json_escape(row.file_path)
            << "\",\"order\":" << row.order;
        append_edit_fields(out, ctx, row.edit_ref, "structure.load");
        out << "}";
    }
    out << "],\"data\":[";
    for (size_t i = 0; i < ctx.structure_puts.size(); ++i) {
        if (i) out << ",";
        append_structure_put_json(out, ctx, ctx.structure_puts[i], false);
    }
    out << "],\"between_data\":[";
    for (size_t i = 0; i < ctx.structure_betweens.size(); ++i) {
        if (i) out << ",";
        append_structure_put_json(out, ctx, ctx.structure_betweens[i], true);
    }
    out << "],\"models\":[";
    for (size_t i = 0; i < ctx.structure_models.size(); ++i) {
        if (i) out << ",";
        const auto& row = ctx.structure_models[i];
        out << "{\"structureKey\":\"" << json_escape(row.structure_key)
            << "\",\"filePath\":\"" << json_escape(row.file_path) << "\"";
        append_edit_fields(out, ctx, row.edit_ref, "structure.model");
        out << "}";
    }
    out << "]}";

    out << ",\"otherTrain\":{\"definitions\":[";
    for (size_t i = 0; i < ctx.other_trains.size(); ++i) {
        if (i) out << ",";
        const auto& row = ctx.other_trains[i];
        out << "{\"distance\":" << json_number(row.distance)
            << ",\"method\":\"" << json_escape(row.method)
            << "\",\"trainKey\":" << json_value(row.train_key)
            << ",\"filePath\":" << json_value(row.load_file_path)
            << ",\"resolvedFilePath\":\"" << json_escape(row.resolved_file_path)
            << "\",\"trackKey\":" << json_value(row.track_key)
            << ",\"direction\":" << json_value(row.direction)
            << ",\"sourceFilePath\":\"" << json_escape(row.source_file_path)
            << "\",\"order\":" << row.order;
        append_edit_fields(out, ctx, row.edit_ref, "otherTrain.definition");
        out << "}";
    }
    out << "],\"structureKeys\":[";
    for (size_t i = 0; i < ctx.other_train_structure_keys.size(); ++i) {
        if (i) out << ",";
        const auto& row = ctx.other_train_structure_keys[i];
        out << "{\"key\":\"" << json_escape(row.key)
            << "\",\"filePath\":\"" << json_escape(row.file_path) << "\"";
        append_edit_fields(out, ctx, row.edit_ref, "otherTrain.structureKey");
        out << "}";
    }
    out << "],\"sound3DKeys\":[";
    for (size_t i = 0; i < ctx.other_train_sound_3d_keys.size(); ++i) {
        if (i) out << ",";
        const auto& row = ctx.other_train_sound_3d_keys[i];
        out << "{\"key\":\"" << json_escape(row.key)
            << "\",\"filePath\":\"" << json_escape(row.file_path) << "\"";
        append_edit_fields(out, ctx, row.edit_ref, "otherTrain.sound3DKey");
        out << "}";
    }
    out << "],\"enable\":[";
    for (size_t i = 0; i < ctx.other_train_enables.size(); ++i) {
        if (i) out << ",";
        const auto& row = ctx.other_train_enables[i];
        out << "{\"distance\":" << json_number(row.distance)
            << ",\"trainKey\":" << json_value(row.train_key)
            << ",\"time\":" << json_value(row.time)
            << ",\"filePath\":\"" << json_escape(row.file_path)
            << "\",\"order\":" << row.order;
        append_edit_fields(out, ctx, row.edit_ref, "otherTrain.enable");
        out << "}";
    }
    out << "],\"stop\":[";
    for (size_t i = 0; i < ctx.other_train_stops.size(); ++i) {
        if (i) out << ",";
        const auto& row = ctx.other_train_stops[i];
        out << "{\"distance\":" << json_number(row.distance)
            << ",\"trainKey\":" << json_value(row.train_key)
            << ",\"decelerate\":" << json_value(row.decelerate)
            << ",\"stopTime\":" << json_value(row.stop_time)
            << ",\"accelerate\":" << json_value(row.accelerate)
            << ",\"speed\":" << json_value(row.speed)
            << ",\"filePath\":\"" << json_escape(row.file_path)
            << "\",\"order\":" << row.order;
        append_edit_fields(out, ctx, row.edit_ref, "otherTrain.stop");
        out << "}";
    }
    out << "]}";

    out << ",\"section\":{\"begin\":[";
    for (size_t i = 0; i < ctx.section_begins.size(); ++i) {
        if (i) out << ",";
        append_section_begin_json(out, ctx, ctx.section_begins[i]);
    }
    out << "],\"speedLimit\":[";
    for (size_t i = 0; i < ctx.section_speed_limits.size(); ++i) {
        if (i) out << ",";
        append_section_speed_limit_json(out, ctx, ctx.section_speed_limits[i]);
    }
    out << "]}";

    out << ",\"signal\":{\"aspects\":[";
    for (size_t i = 0; i < ctx.signal_aspects.size(); ++i) {
        if (i) out << ",";
        const auto& row = ctx.signal_aspects[i];
        out << "{\"signalAspectKey\":\"" << json_escape(row.signal_aspect_key)
            << "\",\"structureKeys\":[";
        for (size_t j = 0; j < row.structure_keys.size(); ++j) {
            if (j) out << ",";
            out << "\"" << json_escape(row.structure_keys[j]) << "\"";
        }
        out << "]";
        append_edit_fields(out, ctx, row.edit_ref, "signal.aspect");
        out << "}";
    }
    out << "],\"data\":[";
    for (size_t i = 0; i < ctx.signal_puts.size(); ++i) {
        if (i) out << ",";
        append_signal_put_json(out, ctx, ctx.signal_puts[i]);
    }
    out << "]}";

    out << ",\"beacon\":[";
    for (size_t i = 0; i < ctx.beacons.size(); ++i) {
        if (i) out << ",";
        append_beacon_json(out, ctx, ctx.beacons[i]);
    }
    out << "]";

    out << ",\"preTrain\":[";
    for (size_t i = 0; i < ctx.pretrains.size(); ++i) {
        if (i) out << ",";
        append_pretrain_json(out, ctx, ctx.pretrains[i]);
    }
    out << "]";

    out << ",\"soundList\":[";
    for (size_t i = 0; i < ctx.sound_list.size(); ++i) {
        if (i) out << ",";
        const auto& row = ctx.sound_list[i];
        out << "{\"soundKey\":\"" << json_escape(row.sound_key)
            << "\",\"filePath\":\"" << json_escape(row.file_path)
            << "\",\"bufferCount\":" << row.buffer_count
            << ",\"is3D\":" << (row.is_3d ? "true" : "false");
        append_edit_fields(out, ctx, row.edit_ref, row.is_3d ? "sound3D.list" : "sound.list");
        out << "}";
    }
    out << "]";

    out << ",\"mapSound\":[";
    for (size_t i = 0; i < ctx.map_sounds.size(); ++i) {
        if (i) out << ",";
        const auto& row = ctx.map_sounds[i];
        out << "{\"distance\":" << json_number(row.distance)
            << ",\"soundKey\":" << json_value(row.sound_key)
            << ",\"filePath\":\"" << json_escape(row.file_path)
            << "\",\"order\":" << row.order;
        append_edit_fields(out, ctx, row.edit_ref, "mapSound.play");
        out << "}";
    }
    out << "]";

    out << ",\"mapSound3D\":[";
    for (size_t i = 0; i < ctx.map_sound_3d.size(); ++i) {
        if (i) out << ",";
        const auto& row = ctx.map_sound_3d[i];
        out << "{\"distance\":" << json_number(row.distance)
            << ",\"soundKey\":" << json_value(row.sound_key)
            << ",\"x\":" << json_number(row.x)
            << ",\"y\":" << json_number(row.y)
            << ",\"filePath\":\"" << json_escape(row.file_path)
            << "\",\"order\":" << row.order;
        append_edit_fields(out, ctx, row.edit_ref, "mapSound3D.put");
        out << "}";
    }
    out << "]";

    out << ",\"rollingNoise\":[";
    for (size_t i = 0; i < ctx.rolling_noises.size(); ++i) {
        if (i) out << ",";
        const auto& row = ctx.rolling_noises[i];
        out << "{\"distance\":" << json_number(row.distance)
            << ",\"index\":" << json_value(row.index)
            << ",\"filePath\":\"" << json_escape(row.file_path)
            << "\",\"order\":" << row.order;
        append_edit_fields(out, ctx, row.edit_ref, "rollingNoise.change");
        out << "}";
    }
    out << "]";

    out << ",\"flangeNoise\":[";
    for (size_t i = 0; i < ctx.flange_noises.size(); ++i) {
        if (i) out << ",";
        const auto& row = ctx.flange_noises[i];
        out << "{\"distance\":" << json_number(row.distance)
            << ",\"index\":" << json_value(row.index)
            << ",\"filePath\":\"" << json_escape(row.file_path)
            << "\",\"order\":" << row.order;
        append_edit_fields(out, ctx, row.edit_ref, "flangeNoise.change");
        out << "}";
    }
    out << "]";

    out << ",\"jointNoise\":[";
    for (size_t i = 0; i < ctx.joint_noises.size(); ++i) {
        if (i) out << ",";
        const auto& row = ctx.joint_noises[i];
        out << "{\"distance\":" << json_number(row.distance)
            << ",\"index\":" << json_value(row.index)
            << ",\"filePath\":\"" << json_escape(row.file_path)
            << "\",\"order\":" << row.order;
        append_edit_fields(out, ctx, row.edit_ref, "jointNoise.play");
        out << "}";
    }
    out << "]";

    out << ",\"repeater\":[";
    for (size_t i = 0; i < ctx.repeaters.size(); ++i) {
        if (i) out << ",";
        const auto& row = ctx.repeaters[i];
        out << "{\"distance\":" << json_number(row.distance)
            << ",\"method\":\"" << json_escape(row.method)
            << "\",\"repeaterKey\":" << json_value(row.repeater_key)
            << ",\"trackKey\":" << json_value(row.track_key)
            << ",\"x\":" << json_number(row.x)
            << ",\"y\":" << json_number(row.y)
            << ",\"z\":" << json_number(row.z)
            << ",\"rx\":" << json_number(row.rx)
            << ",\"ry\":" << json_number(row.ry)
            << ",\"rz\":" << json_number(row.rz)
            << ",\"tilt\":" << json_number(row.tilt)
            << ",\"span\":" << json_number(row.span)
            << ",\"interval\":" << json_number(row.interval)
            << ",\"structureKeys\":[";
        for (size_t j = 0; j < row.structure_keys.size(); ++j) {
            if (j) out << ",";
            out << json_value(row.structure_keys[j]);
        }
        out << "],\"filePath\":\"" << json_escape(row.file_path)
            << "\",\"order\":" << row.order;
        append_edit_fields(out, ctx, row.edit_ref, "repeater");
        out << "}";
    }
    out << "]";

    out << ",\"irregularity\":[";
    for (size_t i = 0; i < ctx.irregularities.size(); ++i) {
        if (i) out << ",";
        const auto& row = ctx.irregularities[i];
        out << "{\"distance\":" << json_number(row.distance)
            << ",\"x\":" << json_number(row.x)
            << ",\"y\":" << json_number(row.y)
            << ",\"r\":" << json_number(row.r)
            << ",\"lx\":" << json_number(row.lx)
            << ",\"ly\":" << json_number(row.ly)
            << ",\"lr\":" << json_number(row.lr)
            << ",\"filePath\":\"" << json_escape(row.file_path)
            << "\",\"order\":" << row.order;
        append_edit_fields(out, ctx, row.edit_ref, "irregularity.change");
        out << "}";
    }
    out << "]";

    out << ",\"background\":[";
    for (size_t i = 0; i < ctx.backgrounds.size(); ++i) {
        if (i) out << ",";
        append_background_json(out, ctx, ctx.backgrounds[i]);
    }
    out << "]";

    out << ",\"adhesion\":[";
    for (size_t i = 0; i < ctx.adhesions.size(); ++i) {
        if (i) out << ",";
        append_adhesion_json(out, ctx, ctx.adhesions[i]);
    }
    out << "]";

    out << ",\"cabIlluminance\":[";
    for (size_t i = 0; i < ctx.cab_illuminance.size(); ++i) {
        if (i) out << ",";
        append_cab_illuminance_json(out, ctx, ctx.cab_illuminance[i]);
    }
    out << "]";

    out << ",\"fog\":[";
    for (size_t i = 0; i < ctx.fogs.size(); ++i) {
        if (i) out << ",";
        append_fog_json(out, ctx, ctx.fogs[i]);
    }
    out << "]";

    out << ",\"speedlimit\":[";
    for (size_t i = 0; i < ctx.speedlimits.size(); ++i) {
        if (i) out << ",";
        const auto& row = ctx.speedlimits[i];
        out << "{\"distance\":" << json_number(row.distance)
            << ",\"speed\":" << json_value(row.speed)
            << ",\"filePath\":\"" << json_escape(row.file_path)
            << "\",\"order\":" << row.order;
        append_edit_fields(out, ctx, row.edit_ref, "speedlimit");
        out << "}";
    }
    out << "]";
    append_edit_registry_json(out, ctx, flags);
    out << "}";
    return out.str();
}

} // namespace kme::maploader::detail
