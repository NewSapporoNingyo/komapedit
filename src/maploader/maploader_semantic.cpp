/*
 * Copyright (c) 2026 Sapporo_ningyo
 *
 * Licensed under Apache License 2.0; see LICENSE and NOTICE.
 */

#include "maploader_internal.h"

namespace kme::maploader::detail {
namespace {

class SemanticWriter {
public:
    void label(const char* value) { string(value ? value : ""); }

    void string(std::string_view value) {
        byte('s');
        integer(static_cast<std::uint64_t>(value.size()));
        data_.append(value.data(), value.size());
    }

    void number(double value) {
        byte('d');
        if (value == 0.0) value = 0.0;
        std::uint64_t bits = 0;
        if (std::isnan(value)) {
            bits = 0x7ff8000000000000ull;
        } else {
            static_assert(sizeof(bits) == sizeof(value), "unexpected double size");
            std::memcpy(&bits, &value, sizeof(bits));
        }
        integer(bits);
    }

    void signed_integer(std::int64_t value) {
        byte('i');
        integer(static_cast<std::uint64_t>(value));
    }

    void boolean(bool value) {
        byte(value ? 't' : 'f');
    }

    void value(const KvMapSnapshot& snapshot, const KvValue& input) {
        byte('v');
        byte(static_cast<unsigned char>(input.kind));
        switch (input.kind) {
            case KV_VALUE_NUMBER:
                number(input.number_value);
                break;
            case KV_VALUE_STRING:
                string(snapshot_text(snapshot, input.string_value));
                break;
            case KV_VALUE_NULL:
            case KV_VALUE_CONTINUE:
                break;
            default:
                throw std::runtime_error("typed snapshot contains an invalid value kind");
        }
    }

    void value(const Value& input) {
        byte('v');
        switch (input.kind) {
            case ValueKind::Null:
                byte(static_cast<unsigned char>(KV_VALUE_NULL));
                break;
            case ValueKind::Number:
                byte(static_cast<unsigned char>(KV_VALUE_NUMBER));
                number(input.number);
                break;
            case ValueKind::String:
                byte(static_cast<unsigned char>(KV_VALUE_STRING));
                string(input.text);
                break;
            case ValueKind::ContinueValue:
                byte(static_cast<unsigned char>(KV_VALUE_CONTINUE));
                break;
        }
    }

    void string_value(std::string_view value) {
        byte('v');
        byte(static_cast<unsigned char>(KV_VALUE_STRING));
        string(value);
    }

    const std::string& data() const { return data_; }
    std::string take() { return std::move(data_); }

    static std::string_view snapshot_text(const KvMapSnapshot& snapshot, KvStringRef ref) {
        if (ref.offset > snapshot.string_size || ref.length > snapshot.string_size - ref.offset ||
            (ref.length != 0 && !snapshot.string_data)) {
            throw std::runtime_error("typed snapshot string reference is out of bounds");
        }
        return ref.length == 0
            ? std::string_view{}
            : std::string_view(snapshot.string_data + static_cast<size_t>(ref.offset),
                               static_cast<size_t>(ref.length));
    }

private:
    void byte(unsigned char value) { data_.push_back(static_cast<char>(value)); }

    void integer(std::uint64_t value) {
        for (unsigned shift = 0; shift < 64; shift += 8) {
            byte(static_cast<unsigned char>((value >> shift) & 0xffu));
        }
    }

    std::string data_;
};

std::string text(const KvMapSnapshot& snapshot, KvStringRef ref) {
    const std::string_view view = SemanticWriter::snapshot_text(snapshot, ref);
    return std::string(view.data(), view.size());
}

bool span_valid(KvSpan span, std::uint64_t count) {
    return span.offset <= count && span.count <= count - span.offset;
}

void field(SemanticWriter& out, const char* name, std::string_view value) {
    out.label(name);
    out.string(value);
}

void field(SemanticWriter& out, const char* name, double value) {
    out.label(name);
    out.number(value);
}

void field(SemanticWriter& out, const char* name, std::int64_t value) {
    out.label(name);
    out.signed_integer(value);
}

void field(SemanticWriter& out, const char* name, bool value) {
    out.label(name);
    out.boolean(value);
}

void field(SemanticWriter& out, const KvMapSnapshot& snapshot,
           const char* name, const KvValue& value) {
    out.label(name);
    out.value(snapshot, value);
}

void value_span(SemanticWriter& out, const KvMapSnapshot& snapshot,
                const char* name, KvSpan span) {
    if (!span_valid(span, snapshot.value_count) || (span.count != 0 && !snapshot.values)) {
        throw std::runtime_error("typed snapshot value span is out of bounds");
    }
    out.label(name);
    out.signed_integer(static_cast<std::int64_t>(span.count));
    for (std::uint64_t i = 0; i < span.count; ++i) {
        out.value(snapshot, snapshot.values[span.offset + i]);
    }
}

void string_span(SemanticWriter& out, const KvMapSnapshot& snapshot,
                 const char* name, KvSpan span) {
    if (!span_valid(span, snapshot.string_ref_count) ||
        (span.count != 0 && !snapshot.string_refs)) {
        throw std::runtime_error("typed snapshot string span is out of bounds");
    }
    out.label(name);
    out.signed_integer(static_cast<std::int64_t>(span.count));
    for (std::uint64_t i = 0; i < span.count; ++i) {
        out.string(SemanticWriter::snapshot_text(
            snapshot, snapshot.string_refs[span.offset + i]));
    }
}

std::string source_path(const KvMapSnapshot& snapshot, const KvRowMetadata& metadata) {
    if (metadata.source_file_index >= snapshot.source_file_count || !snapshot.source_files) return {};
    return text(snapshot, snapshot.source_files[metadata.source_file_index].file_path);
}

void begin_element(SemanticWriter& out, const std::string& source,
                   const std::string& container) {
    field(out, "source", source);
    field(out, "container", container);
}

template <typename Fn>
void emit_element(SemanticMapSnapshot& output, SemanticWriter& full,
                  const KvMapSnapshot& snapshot, const KvRowMetadata& metadata,
                  std::string row_kind, std::string container, size_t row_index,
                  Fn&& append_fields) {
    SemanticWriter canonical;
    const std::string source = source_path(snapshot, metadata);
    begin_element(canonical, source, container);
    append_fields(canonical);
    const std::string canonical_data = canonical.take();
    full.label("element");
    full.string(canonical_data);
    const std::string edit_id = text(snapshot, metadata.edit_id);
    if (edit_id.empty()) return;
    output.elements.push_back({edit_id, std::move(row_kind), std::move(container),
                               row_index, source, canonical_data});
}

const std::string* changed_field(const MapEditChange* change, const char* key) {
    if (!change) return nullptr;
    auto found = change->field_changes.find(key);
    return found == change->field_changes.end() ? nullptr : &found->second;
}

double parse_changed_number(const std::string& input, const char* key) {
    const std::string trimmed = trim_field_copy(input);
    if (trimmed.empty()) throw std::runtime_error(std::string("empty numeric edit field: ") + key);
    const char* begin = trimmed.c_str();
    char* end = nullptr;
    errno = 0;
    const double value = std::strtod(begin, &end);
    if (end == begin || !end || *end != '\0' || errno == ERANGE || !std::isfinite(value)) {
        throw std::runtime_error(std::string("invalid numeric edit field ") + key + ": " + input);
    }
    return value;
}

double changed_number(const MapEditChange* change, const char* key, double fallback) {
    const std::string* input = changed_field(change, key);
    return input ? parse_changed_number(*input, key) : fallback;
}

void changed_optional_number(SemanticWriter& out, const KvMapSnapshot& snapshot,
                             const MapEditChange* change, const char* key,
                             const KvValue& fallback) {
    out.label(key);
    const std::string* input = changed_field(change, key);
    if (!input) {
        out.value(snapshot, fallback);
        return;
    }
    if (trim_field_copy(*input).empty()) {
        out.value(Value::null());
        return;
    }
    out.value(Value::num(parse_changed_number(*input, key)));
}

std::string changed_string(const KvMapSnapshot& snapshot, const MapEditChange* change,
                           const char* key, KvStringRef fallback) {
    const std::string* input = changed_field(change, key);
    return input ? trim_field_copy(*input) : text(snapshot, fallback);
}

void changed_value(SemanticWriter& out, const KvMapSnapshot& snapshot,
                   const MapEditChange* change, const char* key,
                   const KvValue& fallback) {
    out.label(key);
    const std::string* input = changed_field(change, key);
    if (input) {
        out.string_value(trim_field_copy(*input));
    } else {
        out.value(snapshot, fallback);
    }
}

void changed_track_key(SemanticWriter& out, const KvMapSnapshot& snapshot,
                       const MapEditChange* change, const char* key,
                       const KvValue& fallback) {
    out.label(key);
    const std::string* input = changed_field(change, key);
    if (input) {
        out.value(track_key_from_display_text(*input));
    } else {
        out.value(snapshot, fallback);
    }
}

void changed_required_number_value(SemanticWriter& out,
                                   const KvMapSnapshot& snapshot,
                                   const MapEditChange* change,
                                   const char* key,
                                   const KvValue& fallback) {
    out.label(key);
    const std::string* input = changed_field(change, key);
    if (input) {
        out.value(Value::num(parse_changed_number(*input, key)));
    } else {
        out.value(snapshot, fallback);
    }
}

void write_structure_model(SemanticWriter& out, const KvMapSnapshot& snapshot,
                           const KvStructureModelRow& row,
                           const MapEditChange* change = nullptr) {
    field(out, "structureKey", changed_string(snapshot, change, "structureKey", row.structure_key));
    field(out, "filePath", changed_string(snapshot, change, "filePath", row.file_path));
}

void write_sound_list(SemanticWriter& out, const KvMapSnapshot& snapshot,
                      const KvSoundListRow& row,
                      const MapEditChange* change = nullptr) {
    const std::string* buffer_change = changed_field(change, "bufferCount");
    int buffer_count = row.buffer_count;
    if (buffer_change) {
        const std::string normalized =
            normalized_sound_buffer_count_edit_value(*buffer_change);
        buffer_count = normalized.empty() ? 1 : std::stoi(normalized);
    }
    field(out, "soundKey", changed_string(snapshot, change, "soundKey", row.sound_key));
    field(out, "filePath", changed_string(snapshot, change, "filePath", row.file_path));
    field(out, "bufferCount", static_cast<std::int64_t>(buffer_count));
    field(out, "is3D", row.is_3d != 0);
}

void write_structure_put(SemanticWriter& out, const KvMapSnapshot& snapshot,
                         const KvStructurePutRow& row,
                         const MapEditChange* change = nullptr) {
    field(out, "distance", changed_number(change, "distance", row.distance));
    field(out, "method", changed_string(snapshot, change, "method", row.method));
    changed_value(out, snapshot, change, "structureKey", row.structure_key);
    changed_track_key(out, snapshot, change, "trackKey", row.track_key);
    field(out, "x", changed_number(change, "x", row.x));
    field(out, "y", changed_number(change, "y", row.y));
    field(out, "z", changed_number(change, "z", row.z));
    field(out, "rx", changed_number(change, "rx", row.rx));
    field(out, "ry", changed_number(change, "ry", row.ry));
    field(out, "rz", changed_number(change, "rz", row.rz));
    field(out, "tilt", changed_number(change, "tilt", row.tilt));
    field(out, "span", changed_number(change, "span", row.span));
    field(out, "filePath", text(snapshot, row.file_path));
}

void write_structure_between(SemanticWriter& out, const KvMapSnapshot& snapshot,
                             const KvStructureBetweenRow& row,
                             const MapEditChange* change = nullptr) {
    field(out, "distance", changed_number(change, "distance", row.distance));
    field(out, "method", changed_string(snapshot, change, "method", row.method));
    changed_value(out, snapshot, change, "structureKey", row.structure_key);
    changed_track_key(out, snapshot, change, "trackKey1", row.track_key1);
    changed_track_key(out, snapshot, change, "trackKey2", row.track_key2);
    field(out, "flag", changed_number(change, "flag", row.flag));
    field(out, "filePath", text(snapshot, row.file_path));
}

void write_station_put(SemanticWriter& out, const KvMapSnapshot& snapshot,
                       const KvStationPutRow& row,
                       const MapEditChange* change = nullptr) {
    field(out, "distance", changed_number(change, "distance", row.distance));
    changed_value(out, snapshot, change, "stationKey", row.station_key);
    changed_optional_number(out, snapshot, change, "door", row.door);
    changed_optional_number(out, snapshot, change, "margin1", row.margin1);
    changed_optional_number(out, snapshot, change, "margin2", row.margin2);
    field(out, "filePath", text(snapshot, row.file_path));
}

void write_irregularity(SemanticWriter& out, const KvMapSnapshot& snapshot,
                        const KvIrregularityRow& row,
                        const MapEditChange* change = nullptr) {
    field(out, "distance", changed_number(change, "distance", row.distance));
    field(out, "x", changed_number(change, "x", row.x));
    field(out, "y", changed_number(change, "y", row.y));
    field(out, "r", changed_number(change, "r", row.r));
    field(out, "lx", changed_number(change, "lx", row.lx));
    field(out, "ly", changed_number(change, "ly", row.ly));
    field(out, "lr", changed_number(change, "lr", row.lr));
    field(out, "filePath", text(snapshot, row.file_path));
}

bool optional_number_present(const MapEditChange* change, const char* key,
                             const KvValue& fallback) {
    const std::string* input = changed_field(change, key);
    if (input) return !trim_field_copy(*input).empty();
    return fallback.kind != KV_VALUE_NULL;
}

void write_beacon(SemanticWriter& out, const KvMapSnapshot& snapshot,
                  const KvBeaconRow& row, const MapEditChange* change = nullptr) {
    field(out, "distance", changed_number(change, "distance", row.distance));
    changed_required_number_value(out, snapshot, change, "type", row.type);
    changed_required_number_value(out, snapshot, change, "section", row.section);
    changed_required_number_value(out, snapshot, change, "sendData", row.send_data);
    field(out, "filePath", text(snapshot, row.file_path));
}

void write_map_sound(SemanticWriter& out, const KvMapSnapshot& snapshot,
                     const KvMapSoundRow& row, const MapEditChange* change = nullptr) {
    field(out, "distance", changed_number(change, "distance", row.distance));
    changed_value(out, snapshot, change, "soundKey", row.sound_key);
    field(out, "filePath", text(snapshot, row.file_path));
}

void write_map_sound_3d(SemanticWriter& out, const KvMapSnapshot& snapshot,
                        const KvMapSound3DRow& row,
                        const MapEditChange* change = nullptr) {
    field(out, "distance", changed_number(change, "distance", row.distance));
    changed_value(out, snapshot, change, "soundKey", row.sound_key);
    field(out, "x", changed_number(change, "x", row.x));
    field(out, "y", changed_number(change, "y", row.y));
    field(out, "filePath", text(snapshot, row.file_path));
}

void write_noise(SemanticWriter& out, const KvMapSnapshot& snapshot,
                 const KvNoiseRow& row, const MapEditChange* change = nullptr) {
    field(out, "distance", changed_number(change, "distance", row.distance));
    changed_required_number_value(out, snapshot, change, "index", row.index);
    field(out, "filePath", text(snapshot, row.file_path));
}

void write_background(SemanticWriter& out, const KvMapSnapshot& snapshot,
                      const KvBackgroundRow& row,
                      const MapEditChange* change = nullptr) {
    field(out, "distance", changed_number(change, "distance", row.distance));
    changed_value(out, snapshot, change, "structureKey", row.structure_key);
    field(out, "filePath", text(snapshot, row.file_path));
}

void write_adhesion(SemanticWriter& out, const KvMapSnapshot& snapshot,
                    const KvAdhesionRow& row,
                    const MapEditChange* change = nullptr) {
    const bool has_b = optional_number_present(change, "b", row.b);
    const bool has_c = optional_number_present(change, "c", row.c);
    if (has_b != has_c) {
        throw std::runtime_error("Adhesion.Change requires either 1 or 3 parameters");
    }
    field(out, "distance", changed_number(change, "distance", row.distance));
    changed_required_number_value(out, snapshot, change, "a", row.a);
    changed_optional_number(out, snapshot, change, "b", row.b);
    changed_optional_number(out, snapshot, change, "c", row.c);
    field(out, "filePath", text(snapshot, row.file_path));
}

void write_cab_illuminance(SemanticWriter& out, const KvMapSnapshot& snapshot,
                           const KvCabIlluminanceRow& row,
                           const MapEditChange* change = nullptr) {
    field(out, "distance", changed_number(change, "distance", row.distance));
    changed_required_number_value(out, snapshot, change, "value", row.value);
    field(out, "filePath", text(snapshot, row.file_path));
}

void write_fog(SemanticWriter& out, const KvMapSnapshot& snapshot,
               const KvFogRow& row, const MapEditChange* change = nullptr) {
    const bool density = optional_number_present(change, "density", row.density);
    const bool red = optional_number_present(change, "red", row.red);
    const bool green = optional_number_present(change, "green", row.green);
    const bool blue = optional_number_present(change, "blue", row.blue);
    const bool all_colors = red && green && blue;
    if ((red || green || blue) != all_colors || (all_colors && !density)) {
        throw std::runtime_error("Fog requires 0, 1, or 4 parameters");
    }
    field(out, "distance", changed_number(change, "distance", row.distance));
    changed_optional_number(out, snapshot, change, "density", row.density);
    changed_optional_number(out, snapshot, change, "red", row.red);
    changed_optional_number(out, snapshot, change, "green", row.green);
    changed_optional_number(out, snapshot, change, "blue", row.blue);
    field(out, "filePath", text(snapshot, row.file_path));
}

void write_draw_distance(SemanticWriter& out, const KvMapSnapshot& snapshot,
                         const KvDrawDistanceRow& row,
                         const MapEditChange* change = nullptr) {
    field(out, "distance", changed_number(change, "distance", row.distance));
    field(out, "value", changed_number(change, "value", row.value));
    field(out, "filePath", text(snapshot, row.file_path));
}

void write_station_list(SemanticWriter& out, const KvMapSnapshot& snapshot,
                        const KvStationListRow& row,
                        const MapEditChange* change = nullptr) {
    for (size_t i = 0; i < k_station_list_field_names.size(); ++i) {
        const std::string* edited = changed_field(change, k_station_list_field_names[i]);
        const std::string value = edited
            ? normalized_station_list_edit_value(*edited, i)
            : text(snapshot, row.fields[i]);
        out.label("field");
        out.string(value);
    }
}

void write_signal_aspect_values(
    SemanticWriter& out,
    SignalAspectSourceValues values) {
    values.signal_aspect_key =
        normalized_signal_aspect_edit_value(
            values.signal_aspect_key, "signalAspectKey", true);
    field(out, "signalAspectKey", values.signal_aspect_key);
    out.label("structureKeys");
    out.signed_integer(
        static_cast<std::int64_t>(values.structure_keys.size()));
    for (size_t index = 0;
         index < values.structure_keys.size(); ++index) {
        const std::string field_name =
            signal_aspect_structure_key_field_name(index);
        out.string(normalized_signal_aspect_edit_value(
            values.structure_keys[index], field_name, false));
    }
}

void write_signal_aspect(SemanticWriter& out,
                         const KvMapSnapshot& snapshot,
                         const KvSignalAspectRow& row) {
    SignalAspectSourceValues values;
    values.signal_aspect_key = text(snapshot, row.signal_aspect_key);
    if (!span_valid(row.structure_keys, snapshot.string_ref_count) ||
        (row.structure_keys.count != 0 && !snapshot.string_refs)) {
        throw std::runtime_error(
            "typed snapshot Signal aspect structure-key span is out of bounds");
    }
    values.structure_keys.reserve(
        static_cast<size_t>(row.structure_keys.count));
    for (std::uint64_t index = 0;
         index < row.structure_keys.count; ++index) {
        values.structure_keys.push_back(text(
            snapshot,
            snapshot.string_refs[
                row.structure_keys.offset + index]));
    }
    write_signal_aspect_values(out, std::move(values));
}

void write_signal_put(SemanticWriter& out, const KvMapSnapshot& snapshot,
                      const KvSignalPutRow& row,
                      const MapEditChange* change = nullptr) {
    field(out, "distance", changed_number(change, "distance", row.distance));
    changed_value(out, snapshot, change, "signalAspectKey", row.signal_aspect_key);
    changed_required_number_value(out, snapshot, change, "section", row.section);
    changed_track_key(out, snapshot, change, "trackKey", row.track_key);
    field(out, "x", changed_number(change, "x", row.x));
    field(out, "y", changed_number(change, "y", row.y));
    field(out, "z", changed_number(change, "z", row.z));
    field(out, "rx", changed_number(change, "rx", row.rx));
    field(out, "ry", changed_number(change, "ry", row.ry));
    field(out, "rz", changed_number(change, "rz", row.rz));
    field(out, "tilt", changed_number(change, "tilt", row.tilt));
    field(out, "span", changed_number(change, "span", row.span));
    field(out, "filePath", text(snapshot, row.file_path));
}

void write_repeater(SemanticWriter& out, const KvMapSnapshot& snapshot,
                    const KvRepeaterRow& row,
                    const MapEditChange* change = nullptr) {
    if (change) validate_repeater_edit_fields(*change);
    const std::string source_method = ascii_lower(text(snapshot, row.method));
    const std::string effective_method = ascii_lower(
        changed_string(snapshot, change, "method", row.method));
    if ((source_method == "begin" || source_method == "begin0") &&
        effective_method == "end") {
        for (const auto& field_change : change->field_changes) {
            if (field_change.first != "method") {
                throw std::runtime_error(
                    "Repeater Begin to End conversion only supports the method field");
            }
        }
        field(out, "distance", row.distance);
        field(out, "method", std::string_view{"End"});
        field(out, snapshot, "repeaterKey", row.repeater_key);
        out.label("trackKey");
        out.value(Value::str(""));
        for (const char* field_name : {"x", "y", "z", "rx", "ry", "rz", "tilt", "span", "interval"}) {
            field(out, field_name, 0.0);
        }
        out.label("structureKeys");
        out.signed_integer(0);
        field(out, "filePath", text(snapshot, row.file_path));
        return;
    }
    const RepeaterStructureKeyEdit structure_keys = change
        ? parse_repeater_structure_key_edit(*change)
        : RepeaterStructureKeyEdit{};
    field(out, "distance", changed_number(change, "distance", row.distance));
    field(out, "method", changed_string(snapshot, change, "method", row.method));
    field(out, snapshot, "repeaterKey", row.repeater_key);
    changed_track_key(out, snapshot, change, "trackKey", row.track_key);
    field(out, "x", changed_number(change, "x", row.x));
    field(out, "y", changed_number(change, "y", row.y));
    field(out, "z", changed_number(change, "z", row.z));
    field(out, "rx", changed_number(change, "rx", row.rx));
    field(out, "ry", changed_number(change, "ry", row.ry));
    field(out, "rz", changed_number(change, "rz", row.rz));
    field(out, "tilt", changed_number(change, "tilt", row.tilt));
    field(out, "span", changed_number(change, "span", row.span));
    field(out, "interval", changed_number(change, "interval", row.interval));
    if (!structure_keys.changed) {
        value_span(out, snapshot, "structureKeys", row.structure_keys);
    } else {
        out.label("structureKeys");
        out.signed_integer(static_cast<std::int64_t>(structure_keys.values.size()));
        for (const std::string& value : structure_keys.values) out.string_value(value);
    }
    field(out, "filePath", text(snapshot, row.file_path));
}

void reject_unknown_target_fields(const SemanticElementSnapshot& target,
                                  const MapEditChange& change) {
    std::set<std::string> allowed;
    if (target.row_kind == "structure.model") {
        allowed = {"structureKey", "filePath"};
    } else if (target.row_kind == "structure.put") {
        allowed = {"distance", "method", "structureKey", "trackKey", "x", "y", "z",
                   "rx", "ry", "rz", "tilt", "span"};
    } else if (target.row_kind == "structure.between") {
        allowed = {"distance", "method", "structureKey", "trackKey1", "trackKey2", "flag"};
    } else if (target.row_kind == "station.put") {
        allowed = {"distance", "stationKey", "door", "margin1", "margin2"};
    } else if (target.row_kind == "irregularity.change") {
        allowed = {"distance", "x", "y", "r", "lx", "ly", "lr"};
    } else if (target.row_kind == "beacon.put") {
        allowed = {"distance", "type", "section", "sendData"};
    } else if (target.row_kind == "mapSound.play") {
        allowed = {"distance", "soundKey"};
    } else if (target.row_kind == "mapSound3D.put") {
        allowed = {"distance", "soundKey", "x", "y"};
    } else if (target.row_kind == "rollingNoise.change" ||
               target.row_kind == "flangeNoise.change" ||
               target.row_kind == "jointNoise.play") {
        allowed = {"distance", "index"};
    } else if (target.row_kind == "background.change") {
        allowed = {"distance", "structureKey"};
    } else if (target.row_kind == "adhesion.change") {
        allowed = {"distance", "a", "b", "c"};
    } else if (target.row_kind == "cabIlluminance.change") {
        allowed = {"distance", "value"};
    } else if (target.row_kind == "fog.change") {
        allowed = {"distance", "density", "red", "green", "blue"};
    } else if (target.row_kind == "drawDistance.change") {
        allowed = {"distance", "value"};
    } else if (target.row_kind == "station.list") {
        allowed = {"stationKey", "stationName", "arrivalTime", "depertureTime",
                   "stoppageTime", "defaultTime", "signalFlag", "alightingTime",
                   "passengers", "arrivalSoundKey", "depertureSoundKey",
                   "doorReopen", "stuckInDoor"};
    } else if (target.row_kind == "sound.list" ||
               target.row_kind == "sound3D.list") {
        allowed = {"soundKey", "filePath", "bufferCount"};
    } else if (target.row_kind == "signal.aspect") {
        for (const auto& input : change.field_changes) {
            if (input.first == "signalAspectKey" ||
                input.first == "deleteGlare") {
                continue;
            }
            size_t key_index = 0;
            if (!parse_signal_aspect_structure_key_field_name(
                    input.first, key_index)) {
                throw std::runtime_error(
                    "unsupported semantic edit field " + input.first +
                    " for " + target.row_kind);
            }
        }
        return;
    } else if (target.row_kind == "signal.put") {
        allowed = {"distance", "form", "signalAspectKey", "section", "trackKey",
                   "x", "y", "z", "rx", "ry", "rz", "tilt", "span"};
    }
    if (target.row_kind == "repeater") {
        validate_repeater_edit_fields(change);
        return;
    }
    for (const auto& input : change.field_changes) {
        if (allowed.find(input.first) == allowed.end()) {
            throw std::runtime_error("unsupported semantic edit field " + input.first +
                                     " for " + target.row_kind);
        }
    }
}

} // namespace

SemanticMapSnapshot build_semantic_map_snapshot(MapContext& ctx) {
    const KvMapSnapshot& snapshot = build_map_snapshot(ctx);
    SemanticMapSnapshot output;
    output.elements.reserve(static_cast<size_t>(snapshot.element_count));
    SemanticWriter full;

    field(full, "rootPath", SemanticWriter::snapshot_text(snapshot, snapshot.root_path));
    full.label("fileStructureCount");
    full.signed_integer(static_cast<std::int64_t>(snapshot.file_structure_count));
    for (std::uint64_t i = 0; i < snapshot.file_structure_count; ++i) {
        const KvFileStructureRow& row = snapshot.file_structure[i];
        field(full, "parent", row.parent_index);
        field(full, "include", SemanticWriter::snapshot_text(snapshot, row.include_path));
        field(full, "absolute", SemanticWriter::snapshot_text(snapshot, row.absolute_path));
    }
    full.label("sourceFileCount");
    full.signed_integer(static_cast<std::int64_t>(snapshot.source_file_count));
    for (std::uint64_t i = 0; i < snapshot.source_file_count; ++i) {
        const KvSourceFileRow& row = snapshot.source_files[i];
        field(full, "filePath", SemanticWriter::snapshot_text(snapshot, row.file_path));
        field(full, "displayPath", SemanticWriter::snapshot_text(snapshot, row.display_path));
        field(full, "encoding", SemanticWriter::snapshot_text(snapshot, row.encoding));
        field(full, "newline", SemanticWriter::snapshot_text(snapshot, row.newline));
        field(full, "sourceHash", SemanticWriter::snapshot_text(snapshot, row.source_hash));
        field(full, "byteLength", static_cast<std::int64_t>(row.byte_length));
    }
    full.label("controlpoints");
    full.signed_integer(static_cast<std::int64_t>(snapshot.controlpoint_count));
    for (std::uint64_t i = 0; i < snapshot.controlpoint_count; ++i) {
        full.number(snapshot.controlpoints[i]);
    }
    for (double value : snapshot.cp_arbdistribution) field(full, "cpArb", value);
    for (double value : snapshot.cp_arbdistribution_default) field(full, "cpArbDefault", value);
    for (double value : snapshot.cp_default_range) field(full, "cpDefaultRange", value);
    for (std::uint64_t i = 0; i < snapshot.station_position_count; ++i) {
        field(full, "stationPosition", snapshot.station_positions[i].distance);
        field(full, "stationPositionKey",
              SemanticWriter::snapshot_text(snapshot, snapshot.station_positions[i].key));
    }
    for (std::uint64_t i = 0; i < snapshot.station_name_count; ++i) {
        field(full, "stationNameKey",
              SemanticWriter::snapshot_text(snapshot, snapshot.station_names[i].key));
        field(full, "stationName",
              SemanticWriter::snapshot_text(snapshot, snapshot.station_names[i].name));
    }

    for (std::uint64_t i = 0; i < snapshot.own_track_event_count; ++i) {
        const KvTrackEventRow& row = snapshot.own_track_events[i];
        emit_element(output, full, snapshot, row.metadata, "own_track", "own_track",
                     static_cast<size_t>(i), [&](SemanticWriter& out) {
            field(out, "distance", row.distance);
            field(out, "key", SemanticWriter::snapshot_text(snapshot, row.key));
            field(out, snapshot, "value", row.value);
            field(out, "flag", SemanticWriter::snapshot_text(snapshot, row.flag));
        });
    }
    for (std::uint64_t track_index = 0; track_index < snapshot.other_track_count; ++track_index) {
        const KvOtherTrackRow& track = snapshot.other_tracks[track_index];
        const std::string key = text(snapshot, track.key);
        field(full, "otherTrackOrder", key);
        field(full, "otherTrackRangeMin", track.range_min);
        field(full, "otherTrackRangeMax", track.range_max);
        if (!span_valid(track.events, snapshot.other_track_event_count) ||
            (track.events.count != 0 && !snapshot.other_track_events)) {
            throw std::runtime_error("typed snapshot other-track event span is out of bounds");
        }
        for (std::uint64_t i = 0; i < track.events.count; ++i) {
            const KvTrackEventRow& row = snapshot.other_track_events[track.events.offset + i];
            emit_element(output, full, snapshot, row.metadata, "othertrack",
                         "othertrack." + key, static_cast<size_t>(i),
                         [&](SemanticWriter& out) {
                field(out, "distance", row.distance);
                field(out, "key", SemanticWriter::snapshot_text(snapshot, row.key));
                field(out, snapshot, "value", row.value);
                field(out, "flag", SemanticWriter::snapshot_text(snapshot, row.flag));
            });
        }
    }
    for (std::uint64_t i = 0; i < snapshot.station_put_count; ++i) {
        const KvStationPutRow& row = snapshot.station_puts[i];
        emit_element(output, full, snapshot, row.metadata, "station.put", "station.put",
                     static_cast<size_t>(i), [&](SemanticWriter& out) {
            write_station_put(out, snapshot, row);
        });
    }
    for (std::uint64_t i = 0; i < snapshot.station_list_count; ++i) {
        const KvStationListRow& row = snapshot.station_list[i];
        const std::string object_key = text(snapshot, row.object_key);
        emit_element(output, full, snapshot, row.metadata, "station.list",
                     "station.list." + object_key, static_cast<size_t>(i),
                     [&](SemanticWriter& out) {
            write_station_list(out, snapshot, row);
        });
    }
    for (std::uint64_t i = 0; i < snapshot.structure_load_count; ++i) {
        const KvStructureLoadRow& row = snapshot.structure_loads[i];
        emit_element(output, full, snapshot, row.metadata, "structure.load", "structure.loads",
                     static_cast<size_t>(i), [&](SemanticWriter& out) {
            field(out, "distance", row.distance);
            field(out, "method", SemanticWriter::snapshot_text(snapshot, row.method));
            field(out, snapshot, "loadFilePath", row.load_file_path);
            field(out, "filePath", SemanticWriter::snapshot_text(snapshot, row.file_path));
        });
    }
    for (std::uint64_t i = 0; i < snapshot.structure_put_count; ++i) {
        const KvStructurePutRow& row = snapshot.structure_puts[i];
        emit_element(output, full, snapshot, row.metadata, "structure.put", "structure.data",
                     static_cast<size_t>(i), [&](SemanticWriter& out) {
            write_structure_put(out, snapshot, row);
        });
    }
    for (std::uint64_t i = 0; i < snapshot.structure_between_count; ++i) {
        const KvStructureBetweenRow& row = snapshot.structure_betweens[i];
        emit_element(output, full, snapshot, row.metadata, "structure.between",
                     "structure.between_data", static_cast<size_t>(i),
                     [&](SemanticWriter& out) { write_structure_between(out, snapshot, row); });
    }
    for (std::uint64_t i = 0; i < snapshot.structure_model_count; ++i) {
        const KvStructureModelRow& row = snapshot.structure_models[i];
        emit_element(output, full, snapshot, row.metadata, "structure.model", "structure.models",
                     static_cast<size_t>(i), [&](SemanticWriter& out) {
            write_structure_model(out, snapshot, row);
        });
    }

    for (std::uint64_t i = 0; i < snapshot.other_train_definition_count; ++i) {
        const KvOtherTrainDefinitionRow& row = snapshot.other_train_definitions[i];
        emit_element(output, full, snapshot, row.metadata, "otherTrain.definition",
                     "otherTrain.definitions", static_cast<size_t>(i),
                     [&](SemanticWriter& out) {
            field(out, "distance", row.distance);
            field(out, "method", SemanticWriter::snapshot_text(snapshot, row.method));
            field(out, snapshot, "trainKey", row.train_key);
            field(out, snapshot, "filePath", row.load_file_path);
            field(out, "resolvedFilePath",
                  SemanticWriter::snapshot_text(snapshot, row.resolved_file_path));
            field(out, snapshot, "trackKey", row.track_key);
            field(out, snapshot, "direction", row.direction);
            field(out, "sourceFilePath",
                  SemanticWriter::snapshot_text(snapshot, row.source_file_path));
        });
    }
    auto emit_referenced_keys = [&](const KvReferencedKeyRow* rows, std::uint64_t count,
                                    const char* kind, const char* container) {
        for (std::uint64_t i = 0; i < count; ++i) {
            const KvReferencedKeyRow& row = rows[i];
            emit_element(output, full, snapshot, row.metadata, kind, container,
                         static_cast<size_t>(i), [&](SemanticWriter& out) {
                field(out, "key", SemanticWriter::snapshot_text(snapshot, row.key));
                field(out, "filePath", SemanticWriter::snapshot_text(snapshot, row.file_path));
            });
        }
    };
    emit_referenced_keys(snapshot.other_train_structure_keys,
                         snapshot.other_train_structure_key_count,
                         "otherTrain.structureKey", "otherTrain.structureKeys");
    emit_referenced_keys(snapshot.other_train_sound_3d_keys,
                         snapshot.other_train_sound_3d_key_count,
                         "otherTrain.sound3DKey", "otherTrain.sound3DKeys");
    for (std::uint64_t i = 0; i < snapshot.other_train_enable_count; ++i) {
        const KvOtherTrainEnableRow& row = snapshot.other_train_enables[i];
        emit_element(output, full, snapshot, row.metadata, "otherTrain.enable", "otherTrain.enable",
                     static_cast<size_t>(i), [&](SemanticWriter& out) {
            field(out, "distance", row.distance);
            field(out, snapshot, "trainKey", row.train_key);
            field(out, snapshot, "time", row.time);
            field(out, "filePath", SemanticWriter::snapshot_text(snapshot, row.file_path));
        });
    }
    for (std::uint64_t i = 0; i < snapshot.other_train_stop_count; ++i) {
        const KvOtherTrainStopRow& row = snapshot.other_train_stops[i];
        emit_element(output, full, snapshot, row.metadata, "otherTrain.stop", "otherTrain.stop",
                     static_cast<size_t>(i), [&](SemanticWriter& out) {
            field(out, "distance", row.distance);
            field(out, snapshot, "trainKey", row.train_key);
            field(out, snapshot, "decelerate", row.decelerate);
            field(out, snapshot, "stopTime", row.stop_time);
            field(out, snapshot, "accelerate", row.accelerate);
            field(out, snapshot, "speed", row.speed);
            field(out, "filePath", SemanticWriter::snapshot_text(snapshot, row.file_path));
        });
    }
    auto emit_sections = [&](const KvSectionRow* rows, std::uint64_t count,
                             const char* kind, const char* container, const char* values_name) {
        for (std::uint64_t i = 0; i < count; ++i) {
            const KvSectionRow& row = rows[i];
            emit_element(output, full, snapshot, row.metadata, kind, container,
                         static_cast<size_t>(i), [&](SemanticWriter& out) {
                field(out, "distance", row.distance);
                value_span(out, snapshot, values_name, row.values);
                field(out, "filePath", SemanticWriter::snapshot_text(snapshot, row.file_path));
            });
        }
    };
    emit_sections(snapshot.section_begins, snapshot.section_begin_count,
                  "section.begin", "section.begin", "signalIndices");
    emit_sections(snapshot.section_speed_limits, snapshot.section_speed_limit_count,
                  "section.speedLimit", "section.speedLimit", "speeds");
    for (std::uint64_t i = 0; i < snapshot.signal_aspect_count; ++i) {
        const KvSignalAspectRow& row = snapshot.signal_aspects[i];
        emit_element(output, full, snapshot, row.metadata, "signal.aspect", "signal.aspects",
                     static_cast<size_t>(i), [&](SemanticWriter& out) {
            write_signal_aspect(out, snapshot, row);
        });
    }
    for (std::uint64_t i = 0; i < snapshot.signal_put_count; ++i) {
        const KvSignalPutRow& row = snapshot.signal_puts[i];
        emit_element(output, full, snapshot, row.metadata, "signal.put", "signal.data",
                     static_cast<size_t>(i), [&](SemanticWriter& out) {
            write_signal_put(out, snapshot, row);
        });
    }
    for (std::uint64_t i = 0; i < snapshot.beacon_count; ++i) {
        const KvBeaconRow& row = snapshot.beacons[i];
        emit_element(output, full, snapshot, row.metadata, "beacon.put", "beacon",
                     static_cast<size_t>(i), [&](SemanticWriter& out) {
            write_beacon(out, snapshot, row);
        });
    }
    for (std::uint64_t i = 0; i < snapshot.pretrain_count; ++i) {
        const KvPreTrainRow& row = snapshot.pretrains[i];
        emit_element(output, full, snapshot, row.metadata, "preTrain.pass", "preTrain",
                     static_cast<size_t>(i), [&](SemanticWriter& out) {
            field(out, "distance", row.distance);
            field(out, snapshot, "passTime", row.pass_time);
            field(out, "filePath", SemanticWriter::snapshot_text(snapshot, row.file_path));
        });
    }
    size_t sound_index = 0;
    size_t sound_3d_index = 0;
    for (std::uint64_t i = 0; i < snapshot.sound_list_count; ++i) {
        const KvSoundListRow& row = snapshot.sound_list[i];
        const std::string kind = row.is_3d ? "sound3D.list" : "sound.list";
        const size_t row_index = row.is_3d ? sound_3d_index++ : sound_index++;
        emit_element(output, full, snapshot, row.metadata, kind, "soundList",
                     row_index, [&](SemanticWriter& out) {
            write_sound_list(out, snapshot, row);
        });
    }
    for (std::uint64_t i = 0; i < snapshot.map_sound_count; ++i) {
        const KvMapSoundRow& row = snapshot.map_sounds[i];
        emit_element(output, full, snapshot, row.metadata, "mapSound.play", "mapSound",
                     static_cast<size_t>(i), [&](SemanticWriter& out) {
            write_map_sound(out, snapshot, row);
        });
    }
    for (std::uint64_t i = 0; i < snapshot.map_sound_3d_count; ++i) {
        const KvMapSound3DRow& row = snapshot.map_sounds_3d[i];
        emit_element(output, full, snapshot, row.metadata, "mapSound3D.put", "mapSound3D",
                     static_cast<size_t>(i), [&](SemanticWriter& out) {
            write_map_sound_3d(out, snapshot, row);
        });
    }
    auto emit_noises = [&](const KvNoiseRow* rows, std::uint64_t count,
                           const char* kind, const char* container) {
        for (std::uint64_t i = 0; i < count; ++i) {
            const KvNoiseRow& row = rows[i];
            emit_element(output, full, snapshot, row.metadata, kind, container,
                         static_cast<size_t>(i), [&](SemanticWriter& out) {
                write_noise(out, snapshot, row);
            });
        }
    };
    emit_noises(snapshot.rolling_noises, snapshot.rolling_noise_count,
                "rollingNoise.change", "rollingNoise");
    emit_noises(snapshot.flange_noises, snapshot.flange_noise_count,
                "flangeNoise.change", "flangeNoise");
    emit_noises(snapshot.joint_noises, snapshot.joint_noise_count,
                "jointNoise.play", "jointNoise");
    for (std::uint64_t i = 0; i < snapshot.repeater_count; ++i) {
        const KvRepeaterRow& row = snapshot.repeaters[i];
        emit_element(output, full, snapshot, row.metadata, "repeater", "repeater",
                     static_cast<size_t>(i), [&](SemanticWriter& out) {
            field(out, "distance", row.distance);
            field(out, "method", SemanticWriter::snapshot_text(snapshot, row.method));
            field(out, snapshot, "repeaterKey", row.repeater_key);
            field(out, snapshot, "trackKey", row.track_key);
            field(out, "x", row.x); field(out, "y", row.y); field(out, "z", row.z);
            field(out, "rx", row.rx); field(out, "ry", row.ry); field(out, "rz", row.rz);
            field(out, "tilt", row.tilt); field(out, "span", row.span);
            field(out, "interval", row.interval);
            value_span(out, snapshot, "structureKeys", row.structure_keys);
            field(out, "filePath", SemanticWriter::snapshot_text(snapshot, row.file_path));
        });
    }
    for (std::uint64_t i = 0; i < snapshot.irregularity_count; ++i) {
        const KvIrregularityRow& row = snapshot.irregularities[i];
        emit_element(output, full, snapshot, row.metadata, "irregularity.change", "irregularity",
                     static_cast<size_t>(i), [&](SemanticWriter& out) {
            field(out, "distance", row.distance);
            field(out, "x", row.x); field(out, "y", row.y); field(out, "r", row.r);
            field(out, "lx", row.lx); field(out, "ly", row.ly); field(out, "lr", row.lr);
            field(out, "filePath", SemanticWriter::snapshot_text(snapshot, row.file_path));
        });
    }
    for (std::uint64_t i = 0; i < snapshot.background_count; ++i) {
        const KvBackgroundRow& row = snapshot.backgrounds[i];
        emit_element(output, full, snapshot, row.metadata, "background.change", "background",
                     static_cast<size_t>(i), [&](SemanticWriter& out) {
            write_background(out, snapshot, row);
        });
    }
    for (std::uint64_t i = 0; i < snapshot.adhesion_count; ++i) {
        const KvAdhesionRow& row = snapshot.adhesions[i];
        emit_element(output, full, snapshot, row.metadata, "adhesion.change", "adhesion",
                     static_cast<size_t>(i), [&](SemanticWriter& out) {
            write_adhesion(out, snapshot, row);
        });
    }
    for (std::uint64_t i = 0; i < snapshot.cab_illuminance_count; ++i) {
        const KvCabIlluminanceRow& row = snapshot.cab_illuminance[i];
        emit_element(output, full, snapshot, row.metadata, "cabIlluminance.change", "cabIlluminance",
                     static_cast<size_t>(i), [&](SemanticWriter& out) {
            write_cab_illuminance(out, snapshot, row);
        });
    }
    for (std::uint64_t i = 0; i < snapshot.fog_count; ++i) {
        const KvFogRow& row = snapshot.fogs[i];
        emit_element(output, full, snapshot, row.metadata, "fog.change", "fog",
                     static_cast<size_t>(i), [&](SemanticWriter& out) {
            write_fog(out, snapshot, row);
        });
    }
    for (std::uint64_t i = 0; i < snapshot.draw_distance_count; ++i) {
        const KvDrawDistanceRow& row = snapshot.draw_distances[i];
        emit_element(output, full, snapshot, row.metadata,
                     "drawDistance.change", "drawDistance",
                     static_cast<size_t>(i), [&](SemanticWriter& out) {
            write_draw_distance(out, snapshot, row);
        });
    }
    for (std::uint64_t i = 0; i < snapshot.speed_limit_count; ++i) {
        const KvSpeedLimitRow& row = snapshot.speed_limits[i];
        emit_element(output, full, snapshot, row.metadata, "speedlimit", "speedlimit",
                     static_cast<size_t>(i), [&](SemanticWriter& out) {
            field(out, "distance", row.distance);
            field(out, snapshot, "speed", row.speed);
            field(out, "filePath", SemanticWriter::snapshot_text(snapshot, row.file_path));
        });
    }

    std::vector<std::string> variable_names;
    variable_names.reserve(ctx.variables.size());
    for (const auto& variable : ctx.variables) variable_names.push_back(variable.first);
    std::sort(variable_names.begin(), variable_names.end());
    field(full, "finalDistance", ctx.distance);
    for (const std::string& name : variable_names) {
        const Value& value = ctx.variables.at(name);
        field(full, "variableName", name);
        field(full, "variableKind", static_cast<std::int64_t>(value.kind));
        if (value.kind == ValueKind::Number) field(full, "variableNumber", value.number);
        if (value.kind == ValueKind::String) field(full, "variableString", value.text);
    }
    output.full_fingerprint = hex64(stable_hash64(full.data()));
    return output;
}

std::string expected_target_semantic(MapContext& ctx,
                                     const SemanticElementSnapshot& target,
                                     const MapEditChange& change) {
    reject_unknown_target_fields(target, change);
    const KvMapSnapshot& snapshot = build_map_snapshot(ctx);
    SemanticWriter out;
    std::string expected_container = target.container_path;
    if (target.row_kind == "station.list") {
        const auto station_key = change.field_changes.find("stationKey");
        if (station_key != change.field_changes.end()) {
            expected_container = "station.list." +
                ascii_lower(normalized_station_list_edit_value(station_key->second, 0));
        }
    }
    begin_element(out, target.source_file, expected_container);
    if (target.row_kind == "structure.model") {
        if (target.row_index >= snapshot.structure_model_count || !snapshot.structure_models) {
            throw std::runtime_error("structure.model target row is out of bounds");
        }
        write_structure_model(out, snapshot, snapshot.structure_models[target.row_index], &change);
    } else if (target.row_kind == "sound.list" ||
               target.row_kind == "sound3D.list") {
        const bool want_3d = target.row_kind == "sound3D.list";
        const KvSoundListRow* selected = nullptr;
        size_t matching_index = 0;
        for (std::uint64_t i = 0; i < snapshot.sound_list_count; ++i) {
            const KvSoundListRow& row = snapshot.sound_list[i];
            if ((row.is_3d != 0) != want_3d) continue;
            if (matching_index++ == target.row_index) {
                selected = &row;
                break;
            }
        }
        if (!selected) {
            throw std::runtime_error(target.row_kind +
                                     " target row is out of bounds");
        }
        write_sound_list(out, snapshot, *selected, &change);
    } else if (target.row_kind == "structure.put") {
        if (target.row_index >= snapshot.structure_put_count || !snapshot.structure_puts) {
            throw std::runtime_error("structure.put target row is out of bounds");
        }
        write_structure_put(out, snapshot, snapshot.structure_puts[target.row_index], &change);
    } else if (target.row_kind == "structure.between") {
        if (target.row_index >= snapshot.structure_between_count || !snapshot.structure_betweens) {
            throw std::runtime_error("structure.between target row is out of bounds");
        }
        write_structure_between(out, snapshot, snapshot.structure_betweens[target.row_index], &change);
    } else if (target.row_kind == "station.put") {
        if (target.row_index >= snapshot.station_put_count || !snapshot.station_puts) {
            throw std::runtime_error("station.put target row is out of bounds");
        }
        write_station_put(out, snapshot, snapshot.station_puts[target.row_index], &change);
    } else if (target.row_kind == "irregularity.change") {
        if (target.row_index >= snapshot.irregularity_count || !snapshot.irregularities) {
            throw std::runtime_error("irregularity.change target row is out of bounds");
        }
        write_irregularity(out, snapshot, snapshot.irregularities[target.row_index], &change);
    } else if (target.row_kind == "beacon.put") {
        if (target.row_index >= snapshot.beacon_count || !snapshot.beacons) {
            throw std::runtime_error("beacon.put target row is out of bounds");
        }
        write_beacon(out, snapshot, snapshot.beacons[target.row_index], &change);
    } else if (target.row_kind == "mapSound.play") {
        if (target.row_index >= snapshot.map_sound_count || !snapshot.map_sounds) {
            throw std::runtime_error("mapSound.play target row is out of bounds");
        }
        write_map_sound(out, snapshot, snapshot.map_sounds[target.row_index], &change);
    } else if (target.row_kind == "mapSound3D.put") {
        if (target.row_index >= snapshot.map_sound_3d_count || !snapshot.map_sounds_3d) {
            throw std::runtime_error("mapSound3D.put target row is out of bounds");
        }
        write_map_sound_3d(out, snapshot, snapshot.map_sounds_3d[target.row_index], &change);
    } else if (target.row_kind == "rollingNoise.change" ||
               target.row_kind == "flangeNoise.change" ||
               target.row_kind == "jointNoise.play") {
        const KvNoiseRow* rows = target.row_kind == "rollingNoise.change"
            ? snapshot.rolling_noises
            : target.row_kind == "flangeNoise.change" ? snapshot.flange_noises
                                                        : snapshot.joint_noises;
        const std::uint64_t count = target.row_kind == "rollingNoise.change"
            ? snapshot.rolling_noise_count
            : target.row_kind == "flangeNoise.change" ? snapshot.flange_noise_count
                                                        : snapshot.joint_noise_count;
        if (target.row_index >= count || !rows) {
            throw std::runtime_error(target.row_kind + " target row is out of bounds");
        }
        write_noise(out, snapshot, rows[target.row_index], &change);
    } else if (target.row_kind == "background.change") {
        if (target.row_index >= snapshot.background_count || !snapshot.backgrounds) {
            throw std::runtime_error("background.change target row is out of bounds");
        }
        write_background(out, snapshot, snapshot.backgrounds[target.row_index], &change);
    } else if (target.row_kind == "adhesion.change") {
        if (target.row_index >= snapshot.adhesion_count || !snapshot.adhesions) {
            throw std::runtime_error("adhesion.change target row is out of bounds");
        }
        write_adhesion(out, snapshot, snapshot.adhesions[target.row_index], &change);
    } else if (target.row_kind == "cabIlluminance.change") {
        if (target.row_index >= snapshot.cab_illuminance_count || !snapshot.cab_illuminance) {
            throw std::runtime_error("cabIlluminance.change target row is out of bounds");
        }
        write_cab_illuminance(out, snapshot, snapshot.cab_illuminance[target.row_index], &change);
    } else if (target.row_kind == "fog.change") {
        if (target.row_index >= snapshot.fog_count || !snapshot.fogs) {
            throw std::runtime_error("fog.change target row is out of bounds");
        }
        write_fog(out, snapshot, snapshot.fogs[target.row_index], &change);
    } else if (target.row_kind == "drawDistance.change") {
        if (target.row_index >= snapshot.draw_distance_count || !snapshot.draw_distances) {
            throw std::runtime_error("drawDistance.change target row is out of bounds");
        }
        write_draw_distance(out, snapshot, snapshot.draw_distances[target.row_index], &change);
    } else if (target.row_kind == "station.list") {
        if (target.row_index >= snapshot.station_list_count || !snapshot.station_list) {
            throw std::runtime_error("station.list target row is out of bounds");
        }
        write_station_list(out, snapshot, snapshot.station_list[target.row_index], &change);
    } else if (target.row_kind == "signal.aspect") {
        if (target.row_index >= snapshot.signal_aspect_count ||
            !snapshot.signal_aspects ||
            target.row_index >= ctx.signal_aspects.size()) {
            throw std::runtime_error(
                "signal.aspect target row is out of bounds");
        }
        const EditSourceRef ref =
            ctx.signal_aspects[target.row_index].edit_ref;
        if (!ref.valid() ||
            ref.statement_index >= ctx.parsed_statements.size()) {
            throw std::runtime_error(
                "signal.aspect target has no source statement");
        }
        const std::string rebuilt_statement =
            build_signal_aspect_statement(
                change, ctx.parsed_statements[ref.statement_index]);
        write_signal_aspect_values(
            out, parse_signal_aspect_source_values(
                     rebuilt_statement));
    } else if (target.row_kind == "signal.put") {
        if (target.row_index >= snapshot.signal_put_count || !snapshot.signal_puts) {
            throw std::runtime_error("signal.put target row is out of bounds");
        }
        write_signal_put(out, snapshot, snapshot.signal_puts[target.row_index], &change);
    } else if (target.row_kind == "repeater") {
        if (target.row_index >= snapshot.repeater_count || !snapshot.repeaters) {
            throw std::runtime_error("repeater target row is out of bounds");
        }
        write_repeater(out, snapshot, snapshot.repeaters[target.row_index], &change);
    } else {
        throw std::runtime_error("unsupported semantic edit target: " + target.row_kind);
    }
    return out.take();
}

} // namespace kme::maploader::detail
