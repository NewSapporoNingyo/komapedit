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

using kme::maploader::encode_text_for_writeback;
using kme::maploader::log_warn;
using kme::maploader::path_from_utf8;
using kme::maploader::path_to_utf8;
using kme::maploader::read_binary_file;

using SemanticSnapshot = SemanticMapSnapshot;
using SemanticElement = SemanticElementSnapshot;

SemanticSnapshot semantic_snapshot_for_context(MapContext& ctx) {
    return build_semantic_map_snapshot(ctx);
}

struct EditableTarget {
    size_t statement_index = kNoSourceRef;
    std::string row_kind;
    size_t row_index = 0;
    int element_index = 0;
    const StructurePut* structure_put = nullptr;
    const StationPut* station_put = nullptr;
    int elements_for_statement = 0;
};

struct TextReplacementIdentity {
    std::string edit_id;
    std::string row_kind;
    size_t relative_begin = 0;
    size_t relative_end = 0;
    int element_index = 0;
    int baseline_global_order = 0;
};

struct TextReplacement {
    size_t begin = 0;
    size_t end = 0;
    std::string text;
    std::vector<TextReplacementIdentity> identities;
};

struct SourcePatch {
    const SourceFileRecord* record = nullptr;
    std::string text;
    std::string current_hash;
    std::string base_hash;
    bool utf8_bom = false;
    std::vector<TextReplacement> replacements;
};

std::string copy_utf8_view(KvUtf8View view, const char* field_name) {
    if (view.length == 0) return {};
    if (!view.data) throw std::runtime_error(std::string(field_name) + " data is null");
    if (view.length > static_cast<std::uint64_t>(std::numeric_limits<size_t>::max())) {
        throw std::runtime_error(std::string(field_name) + " is too large");
    }
    return std::string(view.data, static_cast<size_t>(view.length));
}

std::vector<MapEditChange> copy_edit_batch(const KvEditBatch& batch) {
    if (batch.change_count != 0 && !batch.changes) {
        throw std::runtime_error("edit batch changes are null");
    }
    if (batch.field_count != 0 && !batch.fields) {
        throw std::runtime_error("edit batch fields are null");
    }
    if (batch.change_count > static_cast<std::uint64_t>(std::numeric_limits<size_t>::max()) ||
        batch.field_count > static_cast<std::uint64_t>(std::numeric_limits<size_t>::max())) {
        throw std::runtime_error("edit batch is too large");
    }
    std::vector<MapEditChange> changes;
    changes.reserve(static_cast<size_t>(batch.change_count));
    for (std::uint64_t i = 0; i < batch.change_count; ++i) {
        const KvEditChange& input = batch.changes[i];
        if (input.fields.offset > batch.field_count ||
            input.fields.count > batch.field_count - input.fields.offset) {
            throw std::runtime_error("edit change field span is out of bounds");
        }
        MapEditChange change;
        change.change_id = copy_utf8_view(input.change_id, "changeId");
        change.edit_id = copy_utf8_view(input.edit_id, "editId");
        switch (input.operation) {
            case KV_EDIT_UPDATE: change.operation = "update"; break;
            case KV_EDIT_INSERT: change.operation = "insert"; break;
            case KV_EDIT_DELETE: change.operation = "delete"; break;
            default: throw std::runtime_error("unsupported typed edit operation");
        }
        change.replacement_statement = copy_utf8_view(
            input.replacement_statement, "replacementStatement");
        change.target_file_path = copy_utf8_view(input.target_file_path, "targetFilePath");
        change.insert_before_edit_id = copy_utf8_view(
            input.insert_before_edit_id, "insertBeforeEditId");
        change.expected_source_hash = copy_utf8_view(
            input.expected_source_hash, "expectedSourceHash");
        change.distance_resolution_key = copy_utf8_view(
            input.distance_resolution_key, "distanceResolutionKey");
        change.distance_boundary_token = copy_utf8_view(
            input.distance_boundary_token, "distanceBoundaryToken");
        change.distance_expression = copy_utf8_view(
            input.distance_expression, "distanceExpression");
        change.confirm_environment_mismatch =
            (input.flags & KV_EDIT_CHANGE_CONFIRM_ENVIRONMENT_MISMATCH) != 0;
        for (std::uint64_t j = 0; j < input.fields.count; ++j) {
            const KvEditField& field = batch.fields[input.fields.offset + j];
            std::string name = copy_utf8_view(field.name, "field name");
            if (name.empty()) throw std::runtime_error("edit field name is empty");
            change.field_changes[std::move(name)] = copy_utf8_view(field.value, "field value");
        }
        changes.push_back(std::move(change));
    }
    return changes;
}

std::string newline_text(const std::string& newline) {
    if (newline == "crlf") return "\r\n";
    if (newline == "cr") return "\r";
    return "\n";
}

SourcePatch load_source_patch(const MapContext& ctx, const SourceFileRecord& record) {
    SourcePatch patch;
    patch.record = &record;
    auto override_it = ctx.source_overrides.find(record.source_key);
    if (override_it != ctx.source_overrides.end()) {
        const SourceTextOverride& source = override_it->second;
        patch.text = source.text;
        patch.current_hash = source.current_hash;
        patch.base_hash = source.base_hash.empty() ? source.current_hash : source.base_hash;
        patch.utf8_bom = source.utf8_bom;
        return patch;
    }

    std::filesystem::path path = path_from_utf8(record.file_path);
    std::string bytes = read_binary_file(path);
    patch.current_hash = hex64(stable_hash64(bytes));
    patch.base_hash = patch.current_hash;
    patch.utf8_bom = has_utf8_bom(bytes);
    patch.text = decode_text_bytes(bytes, record.encoding);
    return patch;
}

size_t utf8_next_offset(const std::string& text, size_t offset) {
    if (offset >= text.size()) return text.size();
    ++offset;
    while (offset < text.size() &&
           (static_cast<unsigned char>(text[offset]) & 0xc0) == 0x80) {
        ++offset;
    }
    return offset;
}

size_t offset_from_line_column(const std::string& text, int line, int column) {
    if (line <= 0 || column <= 0) return std::string::npos;
    size_t line_start = 0;
    int current_line = 1;
    while (current_line < line) {
        size_t next = text.find('\n', line_start);
        if (next == std::string::npos) return std::string::npos;
        line_start = next + 1;
        ++current_line;
    }

    size_t line_end = text.find('\n', line_start);
    if (line_end == std::string::npos) line_end = text.size();
    size_t offset = line_start;
    int current_column = 1;
    while (offset < line_end && current_column < column) {
        if (text[offset] == '\r') {
            ++offset;
            continue;
        }
        offset = utf8_next_offset(text, offset);
        ++current_column;
    }
    return offset;
}

std::pair<size_t, size_t> source_range_in_text(const SourcePatch& patch,
                                               const SourceSpan& source) {
    size_t begin = offset_from_line_column(patch.text, source.line, source.column);
    size_t end = offset_from_line_column(patch.text, source.line_end, source.column_end);
    if (begin == std::string::npos || end == std::string::npos || end < begin) {
        throw std::runtime_error("invalid source span for edit");
    }
    return {begin, end};
}

std::pair<size_t, size_t> safe_statement_removal_range(
    const SourcePatch& patch,
    const std::pair<size_t, size_t>& statement_range) {
    size_t line_start = patch.text.rfind('\n', statement_range.first);
    line_start = line_start == std::string::npos ? 0 : line_start + 1;
    size_t line_end = patch.text.find('\n', statement_range.second);
    if (line_end == std::string::npos) line_end = patch.text.size();
    auto whitespace_only = [&](size_t begin, size_t end) {
        for (size_t i = begin; i < end; ++i) {
            char ch = patch.text[i];
            if (ch != ' ' && ch != '\t' && ch != '\r') return false;
        }
        return true;
    };
    if (!whitespace_only(line_start, statement_range.first) ||
        !whitespace_only(statement_range.second, line_end)) {
        return statement_range;
    }
    if (line_end < patch.text.size()) ++line_end;
    return {line_start, line_end};
}

std::string preview_fragment(const std::string& text, size_t begin, size_t end) {
    const size_t context = 80;
    size_t preview_begin = begin > context ? begin - context : 0;
    size_t preview_end = std::min(text.size(), end + context);
    std::string out = text.substr(preview_begin, preview_end - preview_begin);
    if (preview_begin > 0) out = "..." + out;
    if (preview_end < text.size()) out += "...";
    return out;
}

bool parse_edit_number(const std::string& text, double& value) {
    std::string trimmed = trim_field_copy(text);
    if (trimmed.empty()) return false;
    const char* begin = trimmed.c_str();
    char* end = nullptr;
    errno = 0;
    value = std::strtod(begin, &end);
    return end != begin && errno != ERANGE && end && *end == '\0' && std::isfinite(value);
}

std::string fallback_edit_number(double value) {
    if (!std::isfinite(value)) {
        throw std::runtime_error("invalid numeric edit fallback");
    }
    std::ostringstream out;
    out << std::setprecision(15) << value;
    std::string text = out.str();
    if (text == "-0") return "0";
    return text;
}

std::string normalized_number_arg(const std::string& text) {
    double value = 0.0;
    std::string trimmed = trim_field_copy(text);
    if (!parse_edit_number(trimmed, value)) {
        throw std::runtime_error("invalid numeric edit value: " + text);
    }
    return trimmed == "-0" ? "0" : trimmed;
}

bool edit_expr_ident_start(unsigned char c) {
    return std::isalpha(c) || c == '_' || c >= 0x80;
}

bool edit_expr_ident_part(unsigned char c) {
    return std::isalnum(c) || c == '_' || c >= 0x80;
}

size_t previous_nonspace_pos(const std::string& text, size_t pos) {
    while (pos > 0) {
        --pos;
        if (!std::isspace(static_cast<unsigned char>(text[pos]))) return pos;
    }
    return std::string::npos;
}

bool expression_references_predefined_distance(const std::string& expression) {
    bool single_quoted = false;
    bool double_quoted = false;
    for (size_t i = 0; i < expression.size();) {
        char ch = expression[i];
        if (ch == '\'' && !double_quoted) {
            single_quoted = !single_quoted;
            ++i;
            continue;
        }
        if (ch == '"' && !single_quoted) {
            double_quoted = !double_quoted;
            ++i;
            continue;
        }
        if (single_quoted || double_quoted) {
            ++i;
            continue;
        }
        if (!edit_expr_ident_start(static_cast<unsigned char>(ch))) {
            ++i;
            continue;
        }

        const size_t begin = i;
        ++i;
        while (i < expression.size() &&
               edit_expr_ident_part(static_cast<unsigned char>(expression[i]))) {
            ++i;
        }
        if (ascii_lower(expression.substr(begin, i - begin)) != "distance") continue;

        const size_t prev = previous_nonspace_pos(expression, begin);
        if (prev == std::string::npos || expression[prev] != '$') return true;
    }
    return false;
}

bool sign_is_exponent_part(const std::string& expression, size_t pos) {
    if (pos == 0 || pos + 1 >= expression.size()) return false;
    char previous = expression[pos - 1];
    if (previous != 'e' && previous != 'E') return false;
    return std::isdigit(static_cast<unsigned char>(expression[pos + 1]));
}

bool is_unary_additive_sign(const std::string& expression, size_t pos) {
    const size_t previous = previous_nonspace_pos(expression, pos);
    if (previous == std::string::npos) return true;
    char ch = expression[previous];
    return ch == '+' || ch == '-' || ch == '*' || ch == '/' ||
           ch == '%' || ch == '(' || ch == ',';
}

std::vector<size_t> top_level_additive_separator_positions(const std::string& expression) {
    std::vector<size_t> separators;
    bool single_quoted = false;
    bool double_quoted = false;
    int paren_depth = 0;
    for (size_t i = 0; i < expression.size(); ++i) {
        char ch = expression[i];
        if (ch == '\'' && !double_quoted) {
            single_quoted = !single_quoted;
            continue;
        }
        if (ch == '"' && !single_quoted) {
            double_quoted = !double_quoted;
            continue;
        }
        if (single_quoted || double_quoted) continue;
        if (ch == '(') {
            ++paren_depth;
            continue;
        }
        if (ch == ')' && paren_depth > 0) {
            --paren_depth;
            continue;
        }
        if (paren_depth != 0 || (ch != '+' && ch != '-')) continue;
        if (sign_is_exponent_part(expression, i) || is_unary_additive_sign(expression, i)) continue;
        separators.push_back(i);
    }
    return separators;
}

struct SafeDistanceAddend {
    size_t operator_pos = std::string::npos;
    size_t unary_sign_pos = std::string::npos;
    size_t number_begin = 0;
    size_t number_end = 0;
    int contribution_sign = 1;
    double literal_value = 0.0;
};

bool find_safe_numeric_distance_addend(const std::string& expression,
                                       SafeDistanceAddend& addend) {
    std::vector<size_t> separators = top_level_additive_separator_positions(expression);
    size_t term_begin = 0;
    for (size_t term_index = 0; term_index <= separators.size(); ++term_index) {
        const size_t term_end = term_index < separators.size()
            ? separators[term_index]
            : expression.size();
        const size_t operator_pos = term_index == 0
            ? std::string::npos
            : separators[term_index - 1];
        int sign = 1;
        size_t pos = term_begin;
        size_t unary_sign_pos = std::string::npos;
        if (operator_pos != std::string::npos) {
            sign = expression[operator_pos] == '-' ? -1 : 1;
            pos = operator_pos + 1;
        }
        while (pos < term_end && std::isspace(static_cast<unsigned char>(expression[pos]))) ++pos;
        while (pos < term_end && (expression[pos] == '+' || expression[pos] == '-')) {
            if (unary_sign_pos == std::string::npos) unary_sign_pos = pos;
            if (expression[pos] == '-') sign = -sign;
            ++pos;
            while (pos < term_end && std::isspace(static_cast<unsigned char>(expression[pos]))) ++pos;
        }
        if (pos >= term_end ||
            (!std::isdigit(static_cast<unsigned char>(expression[pos])) && expression[pos] != '.')) {
            term_begin = term_end + 1;
            continue;
        }

        const char* begin = expression.c_str() + pos;
        char* end = nullptr;
        errno = 0;
        double value = std::strtod(begin, &end);
        if (end == begin || errno == ERANGE || !std::isfinite(value)) {
            term_begin = term_end + 1;
            continue;
        }
        const size_t number_end = pos + static_cast<size_t>(end - begin);
        size_t trailing = number_end;
        while (trailing < term_end &&
               std::isspace(static_cast<unsigned char>(expression[trailing]))) {
            ++trailing;
        }
        if (trailing != term_end) {
            term_begin = term_end + 1;
            continue;
        }

        addend.operator_pos = operator_pos;
        addend.unary_sign_pos = unary_sign_pos;
        addend.number_begin = pos;
        addend.number_end = number_end;
        addend.contribution_sign = sign;
        addend.literal_value = value;
        return true;
    }
    return false;
}

std::string apply_delta_to_distance_addend(std::string expression,
                                           const SafeDistanceAddend& addend,
                                           double delta) {
    const double current_contribution =
        static_cast<double>(addend.contribution_sign) * addend.literal_value;
    const double next_contribution = current_contribution + delta;
    const int next_sign = next_contribution < 0.0 ? -1 : 1;
    const std::string next_number = fallback_edit_number(std::fabs(next_contribution));

    if (next_sign == addend.contribution_sign) {
        expression.replace(addend.number_begin,
                           addend.number_end - addend.number_begin,
                           next_number);
        return expression;
    }

    if (addend.operator_pos != std::string::npos) {
        expression.replace(addend.operator_pos,
                           addend.number_end - addend.operator_pos,
                           std::string(next_sign < 0 ? "-" : "+") + next_number);
        return expression;
    }

    const size_t replace_begin = addend.unary_sign_pos == std::string::npos
        ? addend.number_begin
        : addend.unary_sign_pos;
    expression.replace(replace_begin,
                       addend.number_end - replace_begin,
                       (next_sign < 0 ? "-" : "") + next_number);
    return expression;
}

std::string append_delta_to_distance_expression(const std::string& expression, double delta) {
    if (delta < 0.0) return expression + "-" + fallback_edit_number(-delta);
    return expression + "+" + fallback_edit_number(delta);
}

std::string adjust_distance_expression_by_delta(const std::string& expression, double delta) {
    std::string trimmed = trim_field_copy(expression);
    if (trimmed.empty()) {
        throw std::runtime_error("distance edit target has no source distance expression");
    }
    if (expression_references_predefined_distance(trimmed)) {
        throw std::runtime_error("distance edit is blocked because the source distance expression references predefined distance");
    }

    SafeDistanceAddend addend;
    if (find_safe_numeric_distance_addend(trimmed, addend)) {
        return apply_delta_to_distance_addend(std::move(trimmed), addend, delta);
    }
    return append_delta_to_distance_expression(trimmed, delta);
}

std::vector<std::string> parse_bve_argument_fields(const std::string& line) {
    std::vector<std::string> fields;
    if (line.empty()) return fields;

    std::string field;
    bool single_quoted = false;
    bool double_quoted = false;
    int paren_depth = 0;
    for (char ch : line) {
        if (ch == '\'' && !double_quoted) {
            single_quoted = !single_quoted;
            field.push_back(ch);
        } else if (ch == '"' && !single_quoted) {
            double_quoted = !double_quoted;
            field.push_back(ch);
        } else if (ch == '(' && !single_quoted && !double_quoted) {
            ++paren_depth;
            field.push_back(ch);
        } else if (ch == ')' && !single_quoted && !double_quoted && paren_depth > 0) {
            --paren_depth;
            field.push_back(ch);
        } else if (ch == ',' && !single_quoted && !double_quoted && paren_depth == 0) {
            fields.push_back(trim_field_copy(field));
            field.clear();
        } else {
            field.push_back(ch);
        }
    }
    fields.push_back(trim_field_copy(field));
    return fields;
}

std::string quoted_bve_string(const std::string& text) {
    if (text.find_first_of("'\r\n") != std::string::npos) {
        throw std::runtime_error("single quotes and line breaks are not supported in edited string values");
    }
    return "'" + text + "'";
}

std::string value_to_edit_text(const Value& value) {
    if (value.is_null()) return {};
    return as_text(value);
}

std::string value_to_bve_arg(const Value& value) {
    switch (value.kind) {
        case ValueKind::Null: return {};
        case ValueKind::ContinueValue: return "c";
        case ValueKind::Number: return canonical_number(value.number);
        case ValueKind::String: return quoted_bve_string(value.text);
    }
    return {};
}

bool has_field_change(const MapEditChange& change, const std::string& key) {
    return change.field_changes.find(key) != change.field_changes.end();
}

std::string field_text_or(const MapEditChange& change, const std::string& key,
                          const std::string& fallback) {
    auto it = change.field_changes.find(key);
    return it == change.field_changes.end() ? fallback : it->second;
}

const std::string* raw_arg_at(const std::vector<std::string>& raw_args, size_t index) {
    return index < raw_args.size() ? &raw_args[index] : nullptr;
}

std::string required_string_field(const MapEditChange& change, const std::string& key,
                                  const std::string& fallback) {
    std::string value = trim_field_copy(field_text_or(change, key, fallback));
    if (value.empty()) throw std::runtime_error("required edit field is empty: " + key);
    return value;
}

std::string numeric_field(const MapEditChange& change, const std::string& key,
                          double fallback, const std::string* raw_fallback = nullptr) {
    auto it = change.field_changes.find(key);
    if (it != change.field_changes.end()) return normalized_number_arg(it->second);
    if (raw_fallback) return trim_field_copy(*raw_fallback);
    return fallback_edit_number(fallback);
}

std::string value_field_as_bve_arg(const MapEditChange& change, const std::string& key,
                                   const Value& fallback, const std::string* raw_fallback = nullptr) {
    auto it = change.field_changes.find(key);
    if (it == change.field_changes.end()) {
        if (raw_fallback) return trim_field_copy(*raw_fallback);
    }
    if (it == change.field_changes.end()) return value_to_bve_arg(fallback);
    return quoted_bve_string(trim_field_copy(it->second));
}

std::string csv_field(const std::string& text) {
    if (text.find_first_of(",#\"\r\n") == std::string::npos) return text;
    std::string out = "\"";
    for (char ch : text) {
        if (ch == '"') out += "\"\"";
        else out.push_back(ch);
    }
    out += "\"";
    return out;
}

std::string build_structure_model_statement(const MapEditChange& change,
                                            const ParsedStatement& statement) {
    std::vector<std::string> fields = parse_comma_separated_fields(statement.raw_arguments, false);
    if (fields.size() < 2) fields.resize(2);
    std::string structure_key = required_string_field(change, "structureKey", fields[0]);
    std::string file_path = required_string_field(change, "filePath", fields.size() > 1 ? fields[1] : "");
    return csv_field(structure_key) + "," + csv_field(file_path);
}

std::string build_station_put_statement(const MapEditChange& change,
                                        const ParsedStatement& statement,
                                        const StationPut& row) {
    std::string station_key = required_string_field(change, "stationKey", value_to_edit_text(row.station_key));
    std::string raw_args = trim_field_copy(statement.raw_arguments);
    std::ostringstream out;
    out << "Station[" << quoted_bve_string(station_key) << "].Put("
        << raw_args << ");";
    return out.str();
}

std::string build_structure_put_statement(const MapEditChange& change,
                                          const ParsedStatement& statement,
                                          const StructurePut& row,
                                          bool between) {
    std::string structure_key = required_string_field(change, "structureKey", value_to_edit_text(row.structure_key));
    std::vector<std::string> raw_args = parse_bve_argument_fields(statement.raw_arguments);
    const bool source_put0 = ascii_lower(row.method) == "put0";
    std::string output_method = row.method;
    auto method_change = change.field_changes.find("method");
    if (method_change != change.field_changes.end()) {
        const std::string requested_method = trim_field_copy(method_change->second);
        if (between || !source_put0 || ascii_lower(requested_method) != "put") {
            throw std::runtime_error("unsupported Structure placement method conversion");
        }
        output_method = "Put";
    }
    const bool converted_put0 = source_put0 && ascii_lower(output_method) == "put";
    std::ostringstream out;
    out << "Structure[" << quoted_bve_string(structure_key) << "]." << output_method << "(";
    if (between) {
        out << value_field_as_bve_arg(change, "trackKey1", row.track_key1, raw_arg_at(raw_args, 0)) << ","
            << value_field_as_bve_arg(change, "trackKey2", row.track_key2, raw_arg_at(raw_args, 1)) << ","
            << numeric_field(change, "flag", row.flag, raw_arg_at(raw_args, 2));
    } else if (source_put0 && !converted_put0) {
        out << value_field_as_bve_arg(change, "trackKey", row.track_key, raw_arg_at(raw_args, 0)) << ","
            << numeric_field(change, "tilt", row.tilt, raw_arg_at(raw_args, 1)) << ","
            << numeric_field(change, "span", row.span, raw_arg_at(raw_args, 2));
    } else if (converted_put0) {
        out << value_field_as_bve_arg(change, "trackKey", row.track_key, raw_arg_at(raw_args, 0)) << ","
            << numeric_field(change, "x", 0.0) << ","
            << numeric_field(change, "y", 0.0) << ","
            << numeric_field(change, "z", 0.0) << ","
            << numeric_field(change, "rx", 0.0) << ","
            << numeric_field(change, "ry", 0.0) << ","
            << numeric_field(change, "rz", 0.0) << ","
            << numeric_field(change, "tilt", row.tilt, raw_arg_at(raw_args, 1)) << ","
            << numeric_field(change, "span", row.span, raw_arg_at(raw_args, 2));
    } else {
        out << value_field_as_bve_arg(change, "trackKey", row.track_key, raw_arg_at(raw_args, 0)) << ","
            << numeric_field(change, "x", row.x, raw_arg_at(raw_args, 1)) << ","
            << numeric_field(change, "y", row.y, raw_arg_at(raw_args, 2)) << ","
            << numeric_field(change, "z", row.z, raw_arg_at(raw_args, 3)) << ","
            << numeric_field(change, "rx", row.rx, raw_arg_at(raw_args, 4)) << ","
            << numeric_field(change, "ry", row.ry, raw_arg_at(raw_args, 5)) << ","
            << numeric_field(change, "rz", row.rz, raw_arg_at(raw_args, 6)) << ","
            << numeric_field(change, "tilt", row.tilt, raw_arg_at(raw_args, 7)) << ","
            << numeric_field(change, "span", row.span, raw_arg_at(raw_args, 8));
    }
    out << ");";
    return out.str();
}

void count_statement_ref(const EditSourceRef& ref, size_t statement_index, int& count) {
    if (ref.valid() && ref.statement_index == statement_index) ++count;
}

int count_elements_for_statement(const MapContext& ctx, size_t statement_index) {
    int count = 0;
    for (const auto& row : ctx.station_puts) count_statement_ref(row.edit_ref, statement_index, count);
    for (const auto& row : ctx.structure_models) count_statement_ref(row.edit_ref, statement_index, count);
    for (const auto& row : ctx.structure_puts) count_statement_ref(row.edit_ref, statement_index, count);
    for (const auto& row : ctx.structure_betweens) count_statement_ref(row.edit_ref, statement_index, count);
    return count;
}

bool source_start_less(const SourceSpan& a, const SourceSpan& b) {
    if (a.line != b.line) return a.line < b.line;
    if (a.column != b.column) return a.column < b.column;
    if (a.line_end != b.line_end) return a.line_end < b.line_end;
    return a.column_end < b.column_end;
}

bool is_distance_statement(const ParsedStatement& statement) {
    return statement.statement_kind == "Distance.Set";
}

std::string statement_insertion_text(const std::string& source,
                                     size_t offset,
                                     const std::string& statement,
                                     const SourceFileRecord& file) {
    std::string nl = newline_text(file.newline);
    std::string text;
    if (offset > 0 && offset <= source.size()) {
        char previous = source[offset - 1];
        if (previous != '\n' && previous != '\r') text += nl;
    }
    text += statement;
    text += nl;
    return text;
}

template <typename Row>
bool match_edit_ref(MapContext& ctx, const Row& row, const std::string& row_kind,
                    size_t row_index, const std::string& edit_id, EditableTarget& target) {
    if (!row.edit_ref.valid() || row.edit_ref.statement_index >= ctx.parsed_statements.size()) return false;
    if (element_edit_id(ctx, row.edit_ref, row_kind) != edit_id) return false;
    target.statement_index = row.edit_ref.statement_index;
    target.row_kind = row_kind;
    target.row_index = row_index;
    target.element_index = row.edit_ref.element_index;
    target.elements_for_statement = count_elements_for_statement(ctx, target.statement_index);
    return true;
}

EditableTarget find_editable_target(MapContext& ctx, const std::string& edit_id) {
    EditableTarget target;
    for (size_t i = 0; i < ctx.structure_models.size(); ++i) {
        const StructureModel& row = ctx.structure_models[i];
        if (match_edit_ref(ctx, row, "structure.model", i, edit_id, target)) {
            return target;
        }
    }
    for (size_t i = 0; i < ctx.structure_puts.size(); ++i) {
        const StructurePut& row = ctx.structure_puts[i];
        if (match_edit_ref(ctx, row, "structure.put", i, edit_id, target)) {
            target.structure_put = &row;
            return target;
        }
    }
    for (size_t i = 0; i < ctx.structure_betweens.size(); ++i) {
        const StructurePut& row = ctx.structure_betweens[i];
        if (match_edit_ref(ctx, row, "structure.between", i, edit_id, target)) {
            target.structure_put = &row;
            return target;
        }
    }
    for (size_t i = 0; i < ctx.station_puts.size(); ++i) {
        const StationPut& row = ctx.station_puts[i];
        if (match_edit_ref(ctx, row, "station.put", i, edit_id, target)) {
            target.station_put = &row;
            return target;
        }
    }
    return target;
}

const KvEditTargetSnapshot& build_edit_target_snapshot(MapContext& ctx,
                                                       const std::string& edit_id) {
    if (edit_id.empty()) throw std::runtime_error("editId is empty");
    EditableTarget target = find_editable_target(ctx, edit_id);
    if (target.statement_index == kNoSourceRef ||
        target.statement_index >= ctx.parsed_statements.size()) {
        throw std::runtime_error("unsupported or unknown editId: " + edit_id);
    }

    const ParsedStatement& statement = ctx.parsed_statements[target.statement_index];
    const SourceFileRecord* file = statement.source.source_file_index < ctx.source_files.size()
        ? &ctx.source_files[statement.source.source_file_index]
        : nullptr;
    std::string expected_source_hash = file ? file->source_hash : std::string{};
    if (file) {
        auto override_it = ctx.source_overrides.find(file->source_key);
        if (override_it != ctx.source_overrides.end() &&
            !override_it->second.base_hash.empty()) {
            expected_source_hash = override_it->second.base_hash;
        }
    }

    auto storage = std::make_unique<EditTargetSnapshotStorage>();
    storage->string_arena.reserve(4096);
    auto string_ref = [&](const std::string& text) {
        KvStringRef ref{static_cast<std::uint64_t>(storage->string_arena.size()),
                        static_cast<std::uint64_t>(text.size())};
        storage->string_arena.append(text);
        return ref;
    };
    KvEditTargetSnapshot& view = storage->view;
    view.version = KV_EDIT_TARGET_SNAPSHOT_VERSION;
    view.structure_size = sizeof(KvEditTargetSnapshot);
    view.edit_id = string_ref(edit_id);
    view.row_kind = string_ref(target.row_kind);
    view.row_index = static_cast<std::uint64_t>(target.row_index);
    view.elements_for_statement = static_cast<std::uint64_t>(target.elements_for_statement);
    view.statement_kind = string_ref(statement.statement_kind);
    view.source_hash = string_ref(file ? file->source_hash : std::string{});
    view.expected_source_hash = string_ref(expected_source_hash);
    view.source_file_path = string_ref(source_file_path(ctx, statement.source));
    view.source.source_file_index = static_cast<std::uint64_t>(statement.source.source_file_index);
    view.source.byte_start = static_cast<std::uint64_t>(statement.source.byte_start);
    view.source.byte_end = static_cast<std::uint64_t>(statement.source.byte_end);
    view.source.line = statement.source.line;
    view.source.column = statement.source.column;
    view.source.line_end = statement.source.line_end;
    view.source.column_end = statement.source.column_end;
    const std::vector<std::string>& include_stack = source_include_stack(ctx, statement.source);
    view.source.include_stack.offset = 0;
    view.source.include_stack.count = static_cast<std::uint64_t>(include_stack.size());
    storage->string_refs.reserve(include_stack.size());
    for (const std::string& item : include_stack) storage->string_refs.push_back(string_ref(item));
    view.source.include_invocation_key = string_ref(
        source_include_invocation_key(ctx, statement.source));
    view.raw_text = string_ref(statement.raw_text);
    view.raw_text_preview = string_ref(statement.raw_text_preview);
    view.raw_arguments = string_ref(statement.raw_arguments);
    view.distance_expression = string_ref(statement.distance_expression);
    view.distance_value = statement.distance_value;
    view.string_data = storage->string_arena.empty() ? nullptr : storage->string_arena.data();
    view.string_size = static_cast<std::uint64_t>(storage->string_arena.size());
    view.string_refs = storage->string_refs.empty() ? nullptr : storage->string_refs.data();
    view.string_ref_count = static_cast<std::uint64_t>(storage->string_refs.size());
    ctx.edit_target_snapshot = std::move(storage);
    return ctx.edit_target_snapshot->view;
}

std::string build_replacement_statement(const MapEditChange& change,
                                        const ParsedStatement& statement,
                                        const EditableTarget& target) {
    if (!change.replacement_statement.empty()) return change.replacement_statement;
    if (target.row_kind == "structure.model") {
        return build_structure_model_statement(change, statement);
    }
    if (target.row_kind == "structure.put" && target.structure_put) {
        return build_structure_put_statement(change, statement, *target.structure_put, false);
    }
    if (target.row_kind == "structure.between" && target.structure_put) {
        return build_structure_put_statement(change, statement, *target.structure_put, true);
    }
    if (target.row_kind == "station.put" && target.station_put) {
        return build_station_put_statement(change, statement, *target.station_put);
    }
    throw std::runtime_error("unsupported editable target: " + target.row_kind);
}

bool exact_distance_value(double a, double b) {
    return a == b;
}

std::string source_context_identity(const MapContext& ctx, const SourceSpan& source) {
    std::string invocation = source_include_invocation_key(ctx, source);
    if (!invocation.empty()) return invocation;
    return include_stack_key(source_include_stack(ctx, source));
}

bool same_statement_context(const MapContext& ctx,
                            const SourceSpan& a,
                            const SourceSpan& b) {
    return a.source_file_index == b.source_file_index &&
           source_context_identity(ctx, a) == source_context_identity(ctx, b);
}

struct DistanceSectionAnalysis {
    std::vector<size_t> anchors;
    size_t origin_position = kNoSourceRef;
    size_t first_position = 0;
    size_t last_position = 0;
    std::string direction = "ambiguous";
    bool resolved = false;
};

struct DistancePlanningIndex {
    using PhysicalKey = std::tuple<std::string, size_t, size_t, size_t>;

    std::map<std::string, std::vector<size_t>> anchors_by_context;
    std::map<std::string, std::vector<size_t>> statements_by_context;
    std::map<PhysicalKey, std::vector<size_t>> statements_by_physical_source;
    std::unordered_map<size_t, DistanceSectionAnalysis> sections_by_statement;

    explicit DistancePlanningIndex(const MapContext& ctx) {
        for (size_t i = 0; i < ctx.parsed_statements.size(); ++i) {
            const ParsedStatement& statement = ctx.parsed_statements[i];
            std::string context_key = key_for_context(ctx, statement.source);
            statements_by_context[context_key].push_back(i);
            if (is_distance_statement(statement)) {
                anchors_by_context[context_key].push_back(i);
            }
            statements_by_physical_source[physical_key(statement)].push_back(i);
        }
        for (auto& entry : anchors_by_context) {
            std::stable_sort(entry.second.begin(), entry.second.end(), [&](size_t lhs, size_t rhs) {
                const SourceSpan& a = ctx.parsed_statements[lhs].source;
                const SourceSpan& b = ctx.parsed_statements[rhs].source;
                if (a.byte_start != b.byte_start) return a.byte_start < b.byte_start;
                return source_start_less(a, b);
            });
        }
        for (auto& entry : statements_by_context) {
            std::stable_sort(entry.second.begin(), entry.second.end(), [&](size_t lhs, size_t rhs) {
                const SourceSpan& a = ctx.parsed_statements[lhs].source;
                const SourceSpan& b = ctx.parsed_statements[rhs].source;
                if (a.byte_start != b.byte_start) return a.byte_start < b.byte_start;
                return source_start_less(a, b);
            });
        }
    }

    static std::string key_for_context(const MapContext& ctx, const SourceSpan& source) {
        return std::to_string(source.source_file_index) + "\n" +
            source_context_identity(ctx, source);
    }

    static PhysicalKey physical_key(const ParsedStatement& statement) {
        return {statement.statement_kind,
                statement.source.source_file_index,
                statement.source.byte_start,
                statement.source.byte_end};
    }

    const std::vector<size_t>& anchors_for(const MapContext& ctx,
                                           const SourceSpan& source) const {
        static const std::vector<size_t> empty;
        auto it = anchors_by_context.find(key_for_context(ctx, source));
        return it == anchors_by_context.end() ? empty : it->second;
    }

    const std::vector<size_t>& physical_counterparts(
        const ParsedStatement& statement) const {
        static const std::vector<size_t> empty;
        auto it = statements_by_physical_source.find(physical_key(statement));
        return it == statements_by_physical_source.end() ? empty : it->second;
    }

    const std::vector<size_t>& statements_for(const MapContext& ctx,
                                              const SourceSpan& source) const {
        static const std::vector<size_t> empty;
        auto it = statements_by_context.find(key_for_context(ctx, source));
        return it == statements_by_context.end() ? empty : it->second;
    }
};

DistanceSectionAnalysis analyze_distance_section(const MapContext& ctx,
                                                 size_t statement_index,
                                                 DistancePlanningIndex& index) {
    auto cached = index.sections_by_statement.find(statement_index);
    if (cached != index.sections_by_statement.end()) return cached->second;
    DistanceSectionAnalysis result;
    if (statement_index >= ctx.parsed_statements.size()) return result;
    const ParsedStatement& origin = ctx.parsed_statements[statement_index];
    result.anchors = index.anchors_for(ctx, origin.source);
    if (result.anchors.empty()) {
        index.sections_by_statement.emplace(statement_index, result);
        return result;
    }

    for (size_t pos = 0; pos < result.anchors.size(); ++pos) {
        const SourceSpan& source = ctx.parsed_statements[result.anchors[pos]].source;
        if (source.byte_start < origin.source.byte_start) result.origin_position = pos;
        else break;
    }
    if (result.origin_position == kNoSourceRef) {
        index.sections_by_statement.emplace(statement_index, result);
        return result;
    }

    size_t plateau_first = result.origin_position;
    size_t plateau_last = result.origin_position;
    const double center = ctx.parsed_statements[result.anchors[result.origin_position]].distance_value;
    while (plateau_first > 0 &&
           exact_distance_value(ctx.parsed_statements[result.anchors[plateau_first - 1]].distance_value,
                                center)) {
        --plateau_first;
    }
    while (plateau_last + 1 < result.anchors.size() &&
           exact_distance_value(ctx.parsed_statements[result.anchors[plateau_last + 1]].distance_value,
                                center)) {
        ++plateau_last;
    }

    size_t inc_first = plateau_first;
    size_t inc_last = plateau_last;
    while (inc_first > 0) {
        double previous = ctx.parsed_statements[result.anchors[inc_first - 1]].distance_value;
        double current = ctx.parsed_statements[result.anchors[inc_first]].distance_value;
        if (!(previous <= current)) break;
        --inc_first;
    }
    while (inc_last + 1 < result.anchors.size()) {
        double current = ctx.parsed_statements[result.anchors[inc_last]].distance_value;
        double next = ctx.parsed_statements[result.anchors[inc_last + 1]].distance_value;
        if (!(current <= next)) break;
        ++inc_last;
    }

    size_t dec_first = plateau_first;
    size_t dec_last = plateau_last;
    while (dec_first > 0) {
        double previous = ctx.parsed_statements[result.anchors[dec_first - 1]].distance_value;
        double current = ctx.parsed_statements[result.anchors[dec_first]].distance_value;
        if (!(previous >= current)) break;
        --dec_first;
    }
    while (dec_last + 1 < result.anchors.size()) {
        double current = ctx.parsed_statements[result.anchors[dec_last]].distance_value;
        double next = ctx.parsed_statements[result.anchors[dec_last + 1]].distance_value;
        if (!(current >= next)) break;
        ++dec_last;
    }

    const bool inc_distinct =
        ctx.parsed_statements[result.anchors[inc_first]].distance_value <
        ctx.parsed_statements[result.anchors[inc_last]].distance_value;
    const bool dec_distinct =
        ctx.parsed_statements[result.anchors[dec_first]].distance_value >
        ctx.parsed_statements[result.anchors[dec_last]].distance_value;
    if (inc_distinct == dec_distinct) {
        result.first_position = 0;
        result.last_position = result.anchors.size() - 1;
        index.sections_by_statement.emplace(statement_index, result);
        return result;
    }
    result.resolved = true;
    if (inc_distinct) {
        result.first_position = inc_first;
        result.last_position = inc_last;
        result.direction = "increasing";
    } else {
        result.first_position = dec_first;
        result.last_position = dec_last;
        result.direction = "decreasing";
    }
    index.sections_by_statement.emplace(statement_index, result);
    return result;
}

struct DistanceBoundaryPlan {
    size_t before_anchor_position = kNoSourceRef;
    size_t after_anchor_position = kNoSourceRef;
    size_t insert_offset = std::string::npos;
    std::string token;
    int line = 0;
    int column = 0;
    VariableEnvironmentSnapshot variable_environment;
    bool terminal_context_boundary = false;

    bool valid() const {
        return before_anchor_position != kNoSourceRef &&
               (after_anchor_position != kNoSourceRef || terminal_context_boundary) &&
               insert_offset != std::string::npos;
    }
};

std::string distance_boundary_token(const MapContext& ctx,
                                    const ParsedStatement& before,
                                    const ParsedStatement& after) {
    std::ostringstream key;
    key << source_file_key(ctx, before.source) << "\n"
        << source_context_identity(ctx, before.source) << "\n"
        << before.source.byte_start << ":" << before.source.byte_end << "\n"
        << after.source.byte_start << ":" << after.source.byte_end;
    return "distance-gap-" + hex64(stable_hash64(key.str()));
}

DistanceBoundaryPlan boundary_after_anchor(const MapContext& ctx,
                                           const SourcePatch& patch,
                                           const DistanceSectionAnalysis& section,
                                           size_t before_position) {
    DistanceBoundaryPlan boundary;
    if (before_position >= section.anchors.size() ||
        before_position + 1 >= section.anchors.size()) {
        return boundary;
    }
    const ParsedStatement& before = ctx.parsed_statements[section.anchors[before_position]];
    const ParsedStatement& after = ctx.parsed_statements[section.anchors[before_position + 1]];
    boundary.before_anchor_position = before_position;
    boundary.after_anchor_position = before_position + 1;
    const auto after_range = source_range_in_text(patch, after.source);
    boundary.insert_offset = after_range.first;
    size_t line_start = offset_from_line_column(patch.text, after.source.line, 1);
    if (line_start != std::string::npos && line_start <= after_range.first &&
        std::all_of(patch.text.begin() + static_cast<std::ptrdiff_t>(line_start),
                    patch.text.begin() + static_cast<std::ptrdiff_t>(after_range.first),
                    [](char ch) { return ch == ' ' || ch == '\t' || ch == '\r'; })) {
        boundary.insert_offset = line_start;
        boundary.column = 1;
    } else {
        boundary.column = after.source.column;
    }
    boundary.token = distance_boundary_token(ctx, before, after);
    boundary.line = after.source.line;
    boundary.variable_environment = after.variable_environment;
    return boundary;
}

DistanceBoundaryPlan terminal_boundary_for_last_anchor(
    const MapContext& ctx,
    const SourcePatch& patch,
    const DistanceSectionAnalysis& section,
    size_t before_position,
    const DistancePlanningIndex& index) {
    DistanceBoundaryPlan boundary;
    if (before_position >= section.anchors.size() ||
        before_position + 1 != section.anchors.size()) {
        return boundary;
    }
    const ParsedStatement& before = ctx.parsed_statements[section.anchors[before_position]];
    boundary.before_anchor_position = before_position;
    boundary.terminal_context_boundary = true;
    boundary.variable_environment = before.variable_environment;

    size_t terminal_statement_index = kNoSourceRef;
    for (size_t statement_index : index.statements_for(ctx, before.source)) {
        const ParsedStatement& statement = ctx.parsed_statements[statement_index];
        if (statement.source.byte_start > before.source.byte_end &&
            exact_distance_value(statement.distance_value, before.distance_value)) {
            terminal_statement_index = statement_index;
        }
    }
    if (terminal_statement_index != kNoSourceRef) {
        const ParsedStatement& terminal = ctx.parsed_statements[terminal_statement_index];
        auto range = source_range_in_text(patch, terminal.source);
        size_t line_start = offset_from_line_column(patch.text, terminal.source.line, 1);
        boundary.insert_offset = range.first;
        if (line_start != std::string::npos && line_start <= range.first &&
            std::all_of(patch.text.begin() + static_cast<std::ptrdiff_t>(line_start),
                        patch.text.begin() + static_cast<std::ptrdiff_t>(range.first),
                        [](char ch) { return ch == ' ' || ch == '\t' || ch == '\r'; })) {
            boundary.insert_offset = line_start;
        }
        boundary.line = terminal.source.line;
        boundary.column = terminal.source.column;
        boundary.variable_environment = terminal.variable_environment;
    } else {
        auto range = source_range_in_text(patch, before.source);
        boundary.insert_offset = range.second;
        boundary.line = before.source.line_end;
        boundary.column = before.source.column_end;
    }
    std::ostringstream token;
    token << source_file_key(ctx, before.source) << "\n"
          << source_context_identity(ctx, before.source) << "\nterminal\n"
          << before.source.byte_start << ":" << before.source.byte_end << "\n"
          << boundary.insert_offset;
    boundary.token = "distance-terminal-" + hex64(stable_hash64(token.str()));
    return boundary;
}

std::set<std::string> referenced_variables(const std::string& expression) {
    std::set<std::string> names;
    bool single_quoted = false;
    bool double_quoted = false;
    for (size_t i = 0; i < expression.size();) {
        char ch = expression[i];
        if (ch == '\'' && !double_quoted) {
            single_quoted = !single_quoted;
            ++i;
            continue;
        }
        if (ch == '"' && !single_quoted) {
            double_quoted = !double_quoted;
            ++i;
            continue;
        }
        if (single_quoted || double_quoted || ch != '$') {
            ++i;
            continue;
        }
        ++i;
        const size_t begin = i;
        while (i < expression.size() &&
               edit_expr_ident_part(static_cast<unsigned char>(expression[i]))) {
            ++i;
        }
        if (i > begin) names.insert(ascii_lower(expression.substr(begin, i - begin)));
    }
    return names;
}

const Value* environment_value(const VariableEnvironmentSnapshot& environment,
                               const std::string& name) {
    if (!environment) return nullptr;
    auto it = environment->find(name);
    return it == environment->end() ? nullptr : &it->second;
}

bool environment_binding_equal(const VariableEnvironmentSnapshot& a,
                               const VariableEnvironmentSnapshot& b,
                               const std::string& name) {
    const Value* av = environment_value(a, name);
    const Value* bv = environment_value(b, name);
    if (!av || !bv) return av == bv;
    return value_equal(*av, *bv);
}

std::string first_environment_mismatch(const VariableEnvironmentSnapshot& origin,
                                       const VariableEnvironmentSnapshot& destination,
                                       const std::set<std::string>& variables) {
    for (const std::string& variable : variables) {
        if (!environment_binding_equal(origin, destination, variable)) return variable;
    }
    return {};
}

std::string first_multivalued_variable(const MapContext& ctx,
                                       const DistanceSectionAnalysis& section,
                                       const DistancePlanningIndex& distance_index,
                                       const std::set<std::string>& variables) {
    if (section.anchors.empty()) return {};
    const ParsedStatement& first_anchor = ctx.parsed_statements[
        section.anchors[std::min(section.first_position, section.anchors.size() - 1)]];
    const size_t begin_offset = first_anchor.source.byte_start;
    size_t end_offset = std::numeric_limits<size_t>::max();
    if (section.last_position + 1 < section.anchors.size()) {
        end_offset = ctx.parsed_statements[
            section.anchors[section.last_position + 1]].source.byte_start;
    }

    auto assigned_variable_name = [](const ParsedStatement& statement) {
        if (statement.statement_kind != "Variable.Assign") return std::string{};
        size_t pos = 0;
        while (pos < statement.raw_text.size() &&
               std::isspace(static_cast<unsigned char>(statement.raw_text[pos]))) {
            ++pos;
        }
        if (pos >= statement.raw_text.size() || statement.raw_text[pos] != '$') return std::string{};
        const size_t begin = ++pos;
        while (pos < statement.raw_text.size() &&
               edit_expr_ident_part(static_cast<unsigned char>(statement.raw_text[pos]))) {
            ++pos;
        }
        return pos > begin
            ? ascii_lower(statement.raw_text.substr(begin, pos - begin))
            : std::string{};
    };

    for (const std::string& variable : variables) {
        const Value* first = nullptr;
        bool first_missing = false;
        bool initialized = false;
        auto observe = [&](const Value* value) {
            if (!initialized) {
                first = value;
                first_missing = value == nullptr;
                initialized = true;
                return false;
            }
            return (value == nullptr) != first_missing ||
                (value && first && !value_equal(*value, *first));
        };

        for (size_t statement_index :
             distance_index.statements_for(ctx, first_anchor.source)) {
            const ParsedStatement& statement = ctx.parsed_statements[statement_index];
            if (statement.source.byte_start < begin_offset ||
                statement.source.byte_start > end_offset) {
                continue;
            }
            if (observe(environment_value(statement.variable_environment, variable))) {
                return variable;
            }
            if (assigned_variable_name(statement) == variable &&
                !statement.evaluated_values.empty() &&
                observe(&statement.evaluated_values.front())) {
                return variable;
            }
        }
    }
    return {};
}

struct PreparedEdit {
    const MapEditChange* change = nullptr;
    size_t input_ordinal = 0;
    EditableTarget target;
    size_t source_file_index = kNoSourceRef;
    std::pair<size_t, size_t> source_range{};
    std::pair<size_t, size_t> removal_range{};
    std::string source_indent;
    std::string replacement_statement;
    std::string operation;
    bool moves_distance = false;
    double target_distance = 0.0;
    std::string suggested_distance_expression;
    DistanceSectionAnalysis section;
};

struct DistanceEditGroup {
    std::string key;
    size_t source_file_index = kNoSourceRef;
    double target_distance = 0.0;
    DistanceSectionAnalysis section;
    std::vector<size_t> member_indices;
};

struct ResolvedDistanceGroup {
    const DistanceEditGroup* group = nullptr;
    DistanceBoundaryPlan boundary;
    bool create_distance_block = false;
    std::string distance_expression;
    std::string direction;
};

std::string source_section_key(const MapContext& ctx,
                               const DistanceSectionAnalysis& section,
                               const ParsedStatement& origin,
                               double target_distance) {
    std::ostringstream key;
    key << source_file_key(ctx, origin.source) << "\n"
        << source_context_identity(ctx, origin.source) << "\n"
        << section.direction << "\n" << canonical_number(target_distance) << "\n";
    if (!section.anchors.empty()) {
        const ParsedStatement& first = ctx.parsed_statements[
            section.anchors[std::min(section.first_position, section.anchors.size() - 1)]];
        const ParsedStatement& last = ctx.parsed_statements[
            section.anchors[std::min(section.last_position, section.anchors.size() - 1)]];
        key << trim_field_copy(first.distance_expression) << "="
            << canonical_number(first.distance_value) << "@"
            << first.source.byte_start << ":" << first.source.byte_end << "\n"
            << trim_field_copy(last.distance_expression) << "="
            << canonical_number(last.distance_value) << "@"
            << last.source.byte_start << ":" << last.source.byte_end;
    }
    return "distance-resolution-" + hex64(stable_hash64(key.str()));
}

std::vector<DistanceResolutionBoundary> resolution_boundaries(
    const MapContext& ctx,
    const SourcePatch& patch,
    const DistanceSectionAnalysis& section,
    const std::string& recommended_token) {
    std::vector<DistanceResolutionBoundary> boundaries;
    if (section.anchors.size() < 2) return boundaries;
    const size_t first = 0;
    const size_t last = section.anchors.size() - 1;
    for (size_t pos = first;
         pos <= last && pos + 1 < section.anchors.size(); ++pos) {
        DistanceBoundaryPlan boundary = boundary_after_anchor(ctx, patch, section, pos);
        if (!boundary.valid()) continue;
        boundaries.push_back({boundary.token, boundary.line, boundary.column,
                              boundary.token == recommended_token});
    }
    return boundaries;
}

void append_resolution_request(MapContext& ctx,
                               const SourcePatch& patch,
                               const DistanceEditGroup& group,
                               const std::vector<PreparedEdit>& prepared,
                               const std::string& reason,
                               const std::string& variable_name,
                               bool can_confirm_reuse,
                               const std::string& recommended_token,
                               MapEditReport& report) {
    if (group.member_indices.empty()) return;
    const PreparedEdit& first_edit = prepared[group.member_indices.front()];
    const ParsedStatement& origin = ctx.parsed_statements[first_edit.target.statement_index];
    DistanceResolutionRequest request;
    request.resolution_key = group.key;
    request.reason = reason;
    request.source_file = source_file_path(ctx, origin.source);
    request.include_stack = source_include_stack(ctx, origin.source);
    request.target_distance = group.target_distance;
    request.variable_name = variable_name;
    request.suggested_expression = first_edit.suggested_distance_expression;
    request.can_confirm_reuse = can_confirm_reuse;
    for (size_t index : group.member_indices) {
        request.affected_edit_ids.push_back(prepared[index].change->edit_id);
    }
    if (!group.section.anchors.empty()) {
        const ParsedStatement& first = ctx.parsed_statements[
            group.section.anchors[group.section.first_position]];
        const ParsedStatement& last = ctx.parsed_statements[
            group.section.anchors[group.section.last_position]];
        request.source_section.first_line = first.source.line;
        request.source_section.last_line = last.source.line_end;
    }
    request.source_section.direction = group.section.direction;
    std::string effective_recommended_token = recommended_token;
    if (effective_recommended_token.empty() && group.section.anchors.size() >= 2) {
        double best_score = std::numeric_limits<double>::infinity();
        for (size_t pos = 0; pos + 1 < group.section.anchors.size(); ++pos) {
            const double before =
                ctx.parsed_statements[group.section.anchors[pos]].distance_value;
            const double after =
                ctx.parsed_statements[group.section.anchors[pos + 1]].distance_value;
            double score = std::min(std::fabs(group.target_distance - before),
                                    std::fabs(group.target_distance - after));
            if (exact_distance_value(before, group.target_distance)) score = -1.0;
            else if ((before < group.target_distance && group.target_distance < after) ||
                     (before > group.target_distance && group.target_distance > after)) {
                score = 0.0;
            }
            if (score >= best_score) continue;
            DistanceBoundaryPlan candidate = boundary_after_anchor(
                ctx, patch, group.section, pos);
            if (!candidate.valid()) continue;
            best_score = score;
            effective_recommended_token = candidate.token;
        }
    }
    request.allowed_boundaries = resolution_boundaries(
        ctx, patch, group.section, effective_recommended_token);

    std::string nl = patch.record ? newline_text(patch.record->newline) : "\n";
    request.insertion_preview = request.suggested_expression + ";";
    for (size_t index : group.member_indices) {
        request.insertion_preview += nl + prepared[index].replacement_statement;
    }
    report.resolution_requests.push_back(std::move(request));
}

const KvEditReportSnapshot& build_edit_report_snapshot(MapContext& ctx,
                                                       const MapEditReport& report) {
    auto storage = std::make_unique<EditReportSnapshotStorage>();
    const size_t string_hint = report.changed_files.size() + report.warnings.size() +
        report.blocking_errors.size() + report.committed_files.size() * 2 +
        report.committed_rows.size() * 4 + report.previews.size() * 3 + 16;
    storage->string_arena.reserve(std::max<size_t>(4096, string_hint * 48));
    std::unordered_map<std::string, KvStringRef> strings;
    strings.reserve(string_hint);
    auto string_ref = [&](const std::string& text) {
        auto found = strings.find(text);
        if (found != strings.end()) return found->second;
        KvStringRef ref{static_cast<std::uint64_t>(storage->string_arena.size()),
                        static_cast<std::uint64_t>(text.size())};
        storage->string_arena.append(text);
        strings.emplace(text, ref);
        return ref;
    };
    auto append_string_span = [&](const std::vector<std::string>& inputs) {
        KvSpan span{static_cast<std::uint64_t>(storage->string_refs.size()),
                    static_cast<std::uint64_t>(inputs.size())};
        for (const std::string& input : inputs) storage->string_refs.push_back(string_ref(input));
        return span;
    };

    storage->changed_files.reserve(report.changed_files.size());
    for (const std::string& input : report.changed_files) {
        storage->changed_files.push_back(string_ref(input));
    }
    storage->committed_files.reserve(report.committed_files.size());
    for (const MapEditCommittedFile& input : report.committed_files) {
        storage->committed_files.push_back({string_ref(input.file_path),
                                            string_ref(input.source_hash),
                                            static_cast<std::uint64_t>(input.byte_length)});
    }
    storage->committed_rows.reserve(report.committed_rows.size());
    for (const MapEditCommittedRow& input : report.committed_rows) {
        KvEditCommittedRow row{};
        row.row_kind = string_ref(input.row_kind);
        row.row_index = static_cast<std::uint64_t>(input.row_index);
        row.edit_id = string_ref(input.edit_id);
        row.file_path = string_ref(input.file_path);
        row.line = input.line;
        row.column = input.column;
        row.raw_text_preview = string_ref(input.raw_text_preview);
        storage->committed_rows.push_back(row);
    }
    storage->warnings.reserve(report.warnings.size());
    for (const std::string& input : report.warnings) storage->warnings.push_back(string_ref(input));
    storage->blocking_errors.reserve(report.blocking_errors.size());
    for (const std::string& input : report.blocking_errors) {
        storage->blocking_errors.push_back(string_ref(input));
    }
    storage->resolution_requests.reserve(report.resolution_requests.size());
    for (const DistanceResolutionRequest& input : report.resolution_requests) {
        KvDistanceResolutionRow row{};
        row.resolution_key = string_ref(input.resolution_key);
        row.reason = string_ref(input.reason);
        row.source_file = string_ref(input.source_file);
        row.include_stack = append_string_span(input.include_stack);
        row.target_distance = input.target_distance;
        row.variable_name = string_ref(input.variable_name);
        row.affected_edit_ids = append_string_span(input.affected_edit_ids);
        row.suggested_expression = string_ref(input.suggested_expression);
        row.insertion_preview = string_ref(input.insertion_preview);
        row.can_confirm_reuse = input.can_confirm_reuse ? 1u : 0u;
        row.source_section_first_line = input.source_section.first_line;
        row.source_section_last_line = input.source_section.last_line;
        row.source_section_direction = string_ref(input.source_section.direction);
        row.allowed_boundaries.offset = static_cast<std::uint64_t>(storage->boundaries.size());
        row.allowed_boundaries.count = static_cast<std::uint64_t>(input.allowed_boundaries.size());
        storage->boundaries.reserve(storage->boundaries.size() + input.allowed_boundaries.size());
        for (const DistanceResolutionBoundary& boundary : input.allowed_boundaries) {
            KvDistanceBoundaryRow output{};
            output.token = string_ref(boundary.token);
            output.line = boundary.line;
            output.column = boundary.column;
            output.recommended = boundary.recommended ? 1u : 0u;
            storage->boundaries.push_back(output);
        }
        storage->resolution_requests.push_back(row);
    }
    storage->previews.reserve(report.previews.size());
    for (const MapEditPreview& input : report.previews) {
        storage->previews.push_back({string_ref(input.file_path),
                                     string_ref(input.before),
                                     string_ref(input.after)});
    }

    KvEditReportSnapshot& view = storage->view;
    view.version = KV_EDIT_REPORT_SNAPSHOT_VERSION;
    view.ok = report.ok() ? 1u : 0u;
    view.structure_size = sizeof(KvEditReportSnapshot);
    view.report_revision = ++ctx.edit_report_revision;
    view.update_count = report.update_count;
    view.insert_count = report.insert_count;
    view.delete_count = report.delete_count;
    view.full_reparse_ok = report.full_reparse_ok ? 1 : 0;
    view.target_distance_match_count = report.target_distance_match_count;
    view.non_target_changed_count = report.non_target_changed_count;
    view.created_distance_block_count = report.created_distance_block_count;
    view.reused_distance_block_count = report.reused_distance_block_count;
    view.distance_group_count = report.distance_group_count;
    view.validation_fingerprint = string_ref(report.validation_fingerprint);
    view.string_data = storage->string_arena.empty() ? nullptr : storage->string_arena.data();
    view.string_size = static_cast<std::uint64_t>(storage->string_arena.size());
    view.string_refs = storage->string_refs.empty() ? nullptr : storage->string_refs.data();
    view.string_ref_count = static_cast<std::uint64_t>(storage->string_refs.size());
    view.boundaries = storage->boundaries.empty() ? nullptr : storage->boundaries.data();
    view.boundary_count = static_cast<std::uint64_t>(storage->boundaries.size());
    view.changed_files = storage->changed_files.empty() ? nullptr : storage->changed_files.data();
    view.changed_file_count = static_cast<std::uint64_t>(storage->changed_files.size());
    view.committed_files = storage->committed_files.empty() ? nullptr : storage->committed_files.data();
    view.committed_file_count = static_cast<std::uint64_t>(storage->committed_files.size());
    view.committed_rows = storage->committed_rows.empty() ? nullptr : storage->committed_rows.data();
    view.committed_row_count = static_cast<std::uint64_t>(storage->committed_rows.size());
    view.warnings = storage->warnings.empty() ? nullptr : storage->warnings.data();
    view.warning_count = static_cast<std::uint64_t>(storage->warnings.size());
    view.blocking_errors = storage->blocking_errors.empty() ? nullptr : storage->blocking_errors.data();
    view.blocking_error_count = static_cast<std::uint64_t>(storage->blocking_errors.size());
    view.resolution_requests = storage->resolution_requests.empty()
        ? nullptr : storage->resolution_requests.data();
    view.resolution_request_count = static_cast<std::uint64_t>(storage->resolution_requests.size());
    view.preview_snippets = storage->previews.empty() ? nullptr : storage->previews.data();
    view.preview_snippet_count = static_cast<std::uint64_t>(storage->previews.size());
    ctx.edit_report_snapshot = std::move(storage);
    return ctx.edit_report_snapshot->view;
}

struct TransactionalWriteRequest {
    std::filesystem::path path;
    std::string bytes;
    std::string expected_hash;
    std::string new_hash;
};

struct TransactionalWriteOutcome {
    std::vector<std::string> warnings;
};

class TransactionalWriteError : public std::runtime_error {
public:
    TransactionalWriteError(std::string message, bool rollback_complete)
        : std::runtime_error(std::move(message)),
          rollback_complete_(rollback_complete) {}

    bool rollback_complete() const noexcept { return rollback_complete_; }

private:
    bool rollback_complete_ = true;
};

std::string source_file_hash(const std::filesystem::path& path) {
    return hex64(stable_hash64(read_binary_file(path)));
}

std::string native_file_error(const std::string& operation,
                              const std::filesystem::path& path,
                              unsigned long code) {
    return operation + " failed for " + path_to_utf8(path) +
        " (system error " + std::to_string(code) + ")";
}

void remove_transaction_artifact(const std::filesystem::path& path) noexcept {
    if (path.empty()) return;
    std::error_code error;
    std::filesystem::remove(path, error);
}

void write_new_binary_file_for_edit(const std::filesystem::path& path,
                                    const std::string& bytes) {
#if defined(_WIN32)
    HANDLE output = CreateFileW(path.wstring().c_str(), GENERIC_WRITE, 0, nullptr,
                                CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (output == INVALID_HANDLE_VALUE) {
        throw std::runtime_error(native_file_error(
            "temporary file creation", path, GetLastError()));
    }
    bool closed = false;
    try {
        size_t offset = 0;
        while (offset < bytes.size()) {
            const size_t remaining = bytes.size() - offset;
            const DWORD chunk = static_cast<DWORD>(std::min<size_t>(
                remaining, static_cast<size_t>(std::numeric_limits<DWORD>::max())));
            DWORD written = 0;
            if (!WriteFile(output, bytes.data() + offset, chunk, &written, nullptr) ||
                written == 0) {
                throw std::runtime_error(native_file_error(
                    "temporary file write", path, GetLastError()));
            }
            offset += written;
        }
        if (!FlushFileBuffers(output)) {
            throw std::runtime_error(native_file_error(
                "temporary file flush", path, GetLastError()));
        }
        if (!CloseHandle(output)) {
            throw std::runtime_error(native_file_error(
                "temporary file close", path, GetLastError()));
        }
        closed = true;
    } catch (...) {
        if (!closed) CloseHandle(output);
        throw;
    }
#else
    if (std::filesystem::exists(path)) {
        throw std::runtime_error("temporary file already exists: " + path_to_utf8(path));
    }
    std::ofstream output(path, std::ios::binary | std::ios::out | std::ios::trunc);
    if (!output) throw std::runtime_error("File open error: " + path_to_utf8(path));
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    output.flush();
    if (!output) throw std::runtime_error("File write error: " + path_to_utf8(path));
    output.close();
    if (output.fail()) throw std::runtime_error("File close error: " + path_to_utf8(path));
#endif
}

std::filesystem::path transaction_artifact_path(
    const std::filesystem::path& target,
    const std::string& token,
    size_t index,
    const char* suffix) {
    std::string file_name = "." + path_to_utf8(target.filename()) +
        ".komapedit." + token + "." + std::to_string(index) + suffix;
    return target.parent_path() / path_from_utf8(file_name);
}

struct TransactionalWriteEntry {
    TransactionalWriteRequest request;
    std::filesystem::path temporary;
    std::filesystem::path backup;
    std::filesystem::path rollback_discard;
    bool replaced = false;
    bool replacement_failure_uncertain = false;
};

bool replace_staged_file(TransactionalWriteEntry& entry, std::string& error) {
#if defined(_WIN32)
    if (!ReplaceFileW(entry.request.path.wstring().c_str(),
                      entry.temporary.wstring().c_str(),
                      entry.backup.wstring().c_str(), 0, nullptr, nullptr)) {
        const DWORD replace_error = GetLastError();
        error = native_file_error("source file replacement", entry.request.path,
                                  replace_error);
        try {
            const bool target_exists = std::filesystem::exists(entry.request.path);
            const bool backup_exists = std::filesystem::exists(entry.backup);
            if (target_exists) {
                const std::string target_hash = source_file_hash(entry.request.path);
                if ((!entry.request.expected_hash.empty() &&
                     target_hash == entry.request.expected_hash) ||
                    (entry.request.expected_hash.empty() && !backup_exists)) {
                    return false;
                }
                if (backup_exists) {
                    // ReplaceFileW can report a failure after it has already moved
                    // one or both paths. Mark this entry so the transaction's common
                    // rollback path restores the actual backup.
                    entry.replaced = true;
                    return false;
                }
            } else if (backup_exists) {
                if (MoveFileExW(entry.backup.wstring().c_str(),
                                entry.request.path.wstring().c_str(),
                                MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
                    if (entry.request.expected_hash.empty() ||
                        source_file_hash(entry.request.path) == entry.request.expected_hash) {
                        return false;
                    }
                    error += "; restored path has an unexpected hash";
                } else {
                    error += "; immediate backup restore failed with system error " +
                        std::to_string(GetLastError());
                }
            }
        } catch (const std::exception& inspection_error) {
            error += "; replacement-state inspection failed: ";
            error += inspection_error.what();
        }
        entry.replacement_failure_uncertain = true;
        error += "; recovery artifacts: backup=" + path_to_utf8(entry.backup) +
            ", staged=" + path_to_utf8(entry.temporary);
        return false;
    }
#else
    std::error_code filesystem_error;
    const auto permissions = std::filesystem::status(entry.request.path, filesystem_error).permissions();
    if (filesystem_error) {
        error = "source file status failed for " + path_to_utf8(entry.request.path) +
            ": " + filesystem_error.message();
        return false;
    }
    std::filesystem::permissions(entry.temporary, permissions, filesystem_error);
    if (filesystem_error) {
        error = "temporary file permission update failed for " +
            path_to_utf8(entry.temporary) + ": " + filesystem_error.message();
        return false;
    }
    std::filesystem::rename(entry.request.path, entry.backup, filesystem_error);
    if (filesystem_error) {
        error = "source file backup failed for " + path_to_utf8(entry.request.path) +
            ": " + filesystem_error.message();
        return false;
    }
    std::filesystem::rename(entry.temporary, entry.request.path, filesystem_error);
    if (filesystem_error) {
        std::error_code restore_error;
        std::filesystem::rename(entry.backup, entry.request.path, restore_error);
        error = "source file replacement failed for " + path_to_utf8(entry.request.path) +
            ": " + filesystem_error.message();
        if (restore_error) {
            entry.replacement_failure_uncertain = true;
            error += "; immediate restore failed: " + restore_error.message() +
                "; recovery artifacts: backup=" + path_to_utf8(entry.backup) +
                ", staged=" + path_to_utf8(entry.temporary);
        }
        return false;
    }
#endif
    entry.replaced = true;
    return true;
}

bool rollback_staged_file(TransactionalWriteEntry& entry, std::string& error) {
    if (!entry.replaced) return true;
#if defined(_WIN32)
    if (!ReplaceFileW(entry.request.path.wstring().c_str(),
                      entry.backup.wstring().c_str(),
                      entry.rollback_discard.wstring().c_str(), 0, nullptr, nullptr)) {
        error = native_file_error("source file rollback", entry.request.path,
                                  GetLastError());
        return false;
    }
#else
    std::error_code filesystem_error;
    std::filesystem::rename(entry.request.path, entry.rollback_discard, filesystem_error);
    if (filesystem_error) {
        error = "source file rollback staging failed for " +
            path_to_utf8(entry.request.path) + ": " + filesystem_error.message();
        return false;
    }
    std::filesystem::rename(entry.backup, entry.request.path, filesystem_error);
    if (filesystem_error) {
        error = "source file rollback failed for " + path_to_utf8(entry.request.path) +
            ": " + filesystem_error.message();
        return false;
    }
#endif
    entry.replaced = false;
    try {
        if (!entry.request.expected_hash.empty() &&
            source_file_hash(entry.request.path) != entry.request.expected_hash) {
            error = "source file rollback hash mismatch for " +
                path_to_utf8(entry.request.path);
            return false;
        }
    } catch (const std::exception& e) {
        error = e.what();
        return false;
    }
    return true;
}

TransactionalWriteOutcome replace_files_transactionally(
    std::vector<TransactionalWriteRequest> requests) {
    TransactionalWriteOutcome outcome;
    if (requests.empty()) return outcome;

    std::stable_sort(requests.begin(), requests.end(), [](const auto& lhs, const auto& rhs) {
        return ascii_lower(path_to_utf8(lhs.path.lexically_normal())) <
            ascii_lower(path_to_utf8(rhs.path.lexically_normal()));
    });
    for (size_t i = 1; i < requests.size(); ++i) {
        if (ascii_lower(path_to_utf8(requests[i - 1].path.lexically_normal())) ==
            ascii_lower(path_to_utf8(requests[i].path.lexically_normal()))) {
            throw std::runtime_error("duplicate source file in write transaction: " +
                                     path_to_utf8(requests[i].path));
        }
    }

    const std::string token = hex64(stable_hash64(
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "\n" +
#if defined(_WIN32)
        std::to_string(GetCurrentProcessId()) + "\n" +
#else
        std::to_string(std::hash<std::thread::id>{}(std::this_thread::get_id())) + "\n" +
#endif
        path_to_utf8(requests.front().path)));
    std::vector<TransactionalWriteEntry> entries;
    entries.reserve(requests.size());
    for (size_t i = 0; i < requests.size(); ++i) {
        TransactionalWriteEntry entry;
        entry.request = std::move(requests[i]);
        entry.temporary = transaction_artifact_path(entry.request.path, token, i, ".tmp");
        entry.backup = transaction_artifact_path(entry.request.path, token, i, ".bak");
        entry.rollback_discard = transaction_artifact_path(
            entry.request.path, token, i, ".rollback-new");
        if (std::filesystem::exists(entry.temporary) ||
            std::filesystem::exists(entry.backup) ||
            std::filesystem::exists(entry.rollback_discard)) {
            throw std::runtime_error("transaction artifact collision near: " +
                                     path_to_utf8(entry.request.path));
        }
        entries.push_back(std::move(entry));
    }

    try {
        for (TransactionalWriteEntry& entry : entries) {
            write_new_binary_file_for_edit(entry.temporary, entry.request.bytes);
            if (source_file_hash(entry.temporary) != entry.request.new_hash) {
                throw std::runtime_error("staged source hash mismatch for: " +
                                         path_to_utf8(entry.request.path));
            }
        }
        for (const TransactionalWriteEntry& entry : entries) {
            if (!entry.request.expected_hash.empty() &&
                source_file_hash(entry.request.path) != entry.request.expected_hash) {
                throw std::runtime_error("source file changed externally: " +
                                         path_to_utf8(entry.request.path));
            }
        }

        for (TransactionalWriteEntry& entry : entries) {
            if (!entry.request.expected_hash.empty() &&
                source_file_hash(entry.request.path) != entry.request.expected_hash) {
                throw std::runtime_error("source file changed during save: " +
                                         path_to_utf8(entry.request.path));
            }
            std::string replace_error;
            if (!replace_staged_file(entry, replace_error)) {
                throw std::runtime_error(replace_error);
            }
            const std::string target_hash = source_file_hash(entry.request.path);
            const std::string backup_hash = source_file_hash(entry.backup);
            if (target_hash != entry.request.new_hash ||
                (!entry.request.expected_hash.empty() &&
                 backup_hash != entry.request.expected_hash)) {
                throw std::runtime_error("post-replacement hash verification failed for: " +
                                         path_to_utf8(entry.request.path));
            }
        }
    } catch (const std::exception& e) {
        const std::string cause = e.what();
        bool rollback_ok = std::none_of(
            entries.begin(), entries.end(), [](const TransactionalWriteEntry& entry) {
                return entry.replacement_failure_uncertain;
            });
        std::ostringstream rollback_errors;
        for (const TransactionalWriteEntry& entry : entries) {
            if (!entry.replacement_failure_uncertain) continue;
            rollback_errors << " [replacement state uncertain for "
                            << path_to_utf8(entry.request.path) << "; backup="
                            << path_to_utf8(entry.backup) << "; staged="
                            << path_to_utf8(entry.temporary) << "]";
        }
        for (auto it = entries.rbegin(); it != entries.rend(); ++it) {
            if (!it->replaced) continue;
            std::string rollback_error;
            if (!rollback_staged_file(*it, rollback_error)) {
                rollback_ok = false;
                rollback_errors << " [" << rollback_error << "; backup="
                                << path_to_utf8(it->backup) << "; staged="
                                << path_to_utf8(it->temporary) << "]";
            }
        }
        for (TransactionalWriteEntry& entry : entries) {
            if (!entry.replacement_failure_uncertain) {
                remove_transaction_artifact(entry.temporary);
            }
            if (!entry.replacement_failure_uncertain &&
                (rollback_ok || !entry.replaced)) {
                remove_transaction_artifact(entry.backup);
                remove_transaction_artifact(entry.rollback_discard);
            }
        }
        if (rollback_ok) {
            throw TransactionalWriteError(
                "transactional source write failed; all files restored: " + cause,
                true);
        }
        throw TransactionalWriteError(
            "transactional source write failed and rollback was incomplete: " + cause +
            rollback_errors.str(), false);
    }

    for (TransactionalWriteEntry& entry : entries) {
        std::error_code cleanup_error;
        std::filesystem::remove(entry.backup, cleanup_error);
        if (cleanup_error) {
            outcome.warnings.push_back("committed source backup could not be removed: " +
                                       path_to_utf8(entry.backup) + ": " +
                                       cleanup_error.message());
        }
        remove_transaction_artifact(entry.temporary);
        remove_transaction_artifact(entry.rollback_discard);
    }
    return outcome;
}

bool variable_environment_equal(const VariableEnvironment& a, double a_distance,
                                const VariableEnvironment& b, double b_distance) {
    if (a_distance != b_distance || a.size() != b.size()) return false;
    for (const auto& entry : a) {
        auto found = b.find(entry.first);
        if (found == b.end() || !value_equal(entry.second, found->second)) return false;
    }
    return true;
}

std::map<double, std::string> effective_station_position_owners(MapContext& ctx) {
    struct OrderedOwner {
        int order = std::numeric_limits<int>::min();
        std::string edit_id;
    };
    std::map<double, OrderedOwner> ordered;
    for (const StationPut& row : ctx.station_puts) {
        OrderedOwner& owner = ordered[row.distance];
        if (row.order < owner.order) continue;
        owner.order = row.order;
        owner.edit_id = element_edit_id(ctx, row.edit_ref, "station.put");
    }
    std::map<double, std::string> owners;
    for (auto& entry : ordered) owners.emplace(entry.first, std::move(entry.second.edit_id));
    return owners;
}

bool validate_non_target_derived_state(MapContext& baseline,
                                       const MapContext& candidate,
                                       const std::set<std::string>& target_edit_ids,
                                       std::string& error) {
    const std::map<double, std::string> owners = effective_station_position_owners(baseline);
    for (const auto& entry : baseline.station_position) {
        auto owner = owners.find(entry.first);
        if (owner != owners.end() &&
            target_edit_ids.find(owner->second) != target_edit_ids.end()) {
            continue;
        }
        auto after = candidate.station_position.find(entry.first);
        if (after == candidate.station_position.end() || after->second != entry.second) {
            error = "full reparse changed a non-target effective station position at distance " +
                canonical_number(entry.first);
            return false;
        }
    }
    return true;
}

std::unique_ptr<MapContext> parse_report_candidate(MapContext& ctx,
                                                   const MapEditReport& report) {
    SourceTextOverrides overrides = ctx.source_overrides;
    apply_patched_files_to_overrides(overrides, report);
    std::string entry_file_path = ctx.entry_file_path;
    if (entry_file_path.empty()) {
        if (!ctx.include_stack.empty()) entry_file_path = ctx.include_stack.front();
        else if (!ctx.source_files.empty()) entry_file_path = ctx.source_files.front().file_path;
    }
    if (entry_file_path.empty()) throw std::runtime_error("map entry file is not known");
    MapParseOptions options = ctx.parse_options;
    options.collect_edit_metadata = true;
    return parse_map_context(path_from_utf8(entry_file_path), ctx.unit_distance,
                             std::move(overrides), ctx.cp_arbdistribution_explicit,
                             ctx.cp_arbdistribution, options);
}

void validate_edit_report(MapContext& baseline,
                          const std::vector<MapEditChange>& changes,
                          MapEditReport& report) {
    std::unique_ptr<MapContext> candidate;
    SemanticSnapshot before_snapshot;
    SemanticSnapshot after_snapshot;
    try {
        candidate = parse_report_candidate(baseline, report);
        before_snapshot = semantic_snapshot_for_context(baseline);
        after_snapshot = semantic_snapshot_for_context(*candidate);
    } catch (const std::exception& e) {
        report.blocking_errors.push_back(std::string("full edited-map reparse failed: ") + e.what());
        return;
    }

    const std::vector<SemanticElement>& before_elements = before_snapshot.elements;
    const std::vector<SemanticElement>& after_elements = after_snapshot.elements;
    std::map<std::string, const SemanticElement*> before_by_id;
    std::map<std::string, const SemanticElement*> after_by_native_id;
    for (const SemanticElement& element : before_elements) {
        if (!before_by_id.emplace(element.edit_id, &element).second) {
            report.blocking_errors.push_back("duplicate baseline editId during validation: " +
                                             element.edit_id);
            return;
        }
    }
    for (const SemanticElement& element : after_elements) {
        if (!after_by_native_id.emplace(element.edit_id, &element).second) {
            report.blocking_errors.push_back("duplicate candidate editId during validation: " +
                                             element.edit_id);
            return;
        }
    }

    std::set<std::string> excluded_before;
    std::map<std::string, std::string> expected_target_canonical;
    int expected_distance_target_count = 0;
    for (const MapEditChange& change : changes) {
        if (change.edit_id.empty()) continue;
        const std::string operation =
            ascii_lower(change.operation.empty() ? "update" : change.operation);
        auto before_it = before_by_id.find(change.edit_id);
        if (before_it == before_by_id.end()) {
            report.blocking_errors.push_back("validation lost baseline target: " + change.edit_id);
            return;
        }
        excluded_before.insert(change.edit_id);
        if (operation == "delete") continue;
        try {
            expected_target_canonical.emplace(
                change.edit_id,
                expected_target_semantic(baseline, *before_it->second, change));
        } catch (const std::exception& e) {
            report.blocking_errors.push_back(std::string("target validation failed for ") +
                                             change.edit_id + ": " + e.what());
            return;
        }
        if (has_field_change(change, "distance")) ++expected_distance_target_count;
    }

    using IdentityLocation =
        std::tuple<std::string, size_t, size_t, std::string, int>;
    struct CandidateIdentityElement {
        std::string native_edit_id;
        int global_order = 0;
    };
    std::map<IdentityLocation, std::vector<const MapEditIdentityOrigin*>> origins_by_location;
    std::set<std::string> origin_edit_ids;
    for (const MapEditIdentityOrigin& origin : report.identity_origins) {
        if (expected_target_canonical.find(origin.edit_id) ==
            expected_target_canonical.end()) {
            report.blocking_errors.push_back(
                "generated edit identity does not belong to a surviving target: " +
                origin.edit_id);
            return;
        }
        if (!origin_edit_ids.insert(origin.edit_id).second) {
            report.blocking_errors.push_back(
                "generated more than one source identity for edit target: " + origin.edit_id);
            return;
        }
        origins_by_location[IdentityLocation{
            origin.source_key, origin.text_start, origin.text_end,
            origin.row_kind, origin.element_index,
        }].push_back(&origin);
    }
    if (origin_edit_ids.size() != expected_target_canonical.size()) {
        report.blocking_errors.push_back(
            "full reparse validation is missing source provenance for an edited target");
        return;
    }

    std::map<std::string, const std::string*> patched_text_by_source_key;
    for (const MapEditPatchedFile& file : report.patched_files) {
        patched_text_by_source_key[file.source_key] = &file.text;
    }
    std::map<IdentityLocation, std::vector<CandidateIdentityElement>> candidates_by_location;
    auto collect_candidate_rows = [&](const auto& rows, const std::string& row_kind) {
        for (const auto& row : rows) {
            const EditSourceRef& ref = row.edit_ref;
            if (!ref.valid() || ref.statement_index >= candidate->parsed_statements.size()) continue;
            const ParsedStatement& statement = candidate->parsed_statements[ref.statement_index];
            const std::string& source_key = source_file_key(*candidate, statement.source);
            auto patched_text = patched_text_by_source_key.find(source_key);
            if (patched_text == patched_text_by_source_key.end()) continue;
            SourcePatch source;
            source.text = *patched_text->second;
            const auto range = source_range_in_text(source, statement.source);
            candidates_by_location[IdentityLocation{
                source_key, range.first, range.second, row_kind, ref.element_index,
            }].push_back({
                native_element_edit_id(*candidate, ref, row_kind),
                statement.global_order,
            });
        }
    };
    try {
        collect_candidate_rows(candidate->structure_models, "structure.model");
        collect_candidate_rows(candidate->structure_puts, "structure.put");
        collect_candidate_rows(candidate->structure_betweens, "structure.between");
        collect_candidate_rows(candidate->station_puts, "station.put");
    } catch (const std::exception& e) {
        report.blocking_errors.push_back(
            std::string("failed to resolve edited target source provenance: ") + e.what());
        return;
    }

    std::map<std::string, std::string> candidate_to_stable_edit_ids;
    std::map<std::string, std::string> stable_to_candidate_edit_ids;
    auto preserve_edit_identity = [&](const std::string& stable_id,
                                      const std::string& candidate_id) {
        if (stable_id.empty() || candidate_id.empty()) {
            report.blocking_errors.push_back(
                "validated edit identity contains an empty editId");
            return;
        }
        auto by_candidate = candidate_to_stable_edit_ids.emplace(candidate_id, stable_id);
        if (!by_candidate.second && by_candidate.first->second != stable_id) {
            report.blocking_errors.push_back(
                "validated candidate editId matched multiple baseline elements: " +
                candidate_id);
        }
        auto by_stable = stable_to_candidate_edit_ids.emplace(stable_id, candidate_id);
        if (!by_stable.second && by_stable.first->second != candidate_id) {
            report.blocking_errors.push_back(
                "validated baseline editId matched multiple candidate elements: " + stable_id);
        }
    };

    std::set<std::string> candidate_target_ids;
    for (auto& origin_group : origins_by_location) {
        auto candidates = candidates_by_location.find(origin_group.first);
        if (candidates == candidates_by_location.end() ||
            candidates->second.size() != origin_group.second.size()) {
            report.blocking_errors.push_back(
                "edited target could not be uniquely reconnected to its generated source statement");
            return;
        }
        std::stable_sort(origin_group.second.begin(), origin_group.second.end(),
                         [](const auto* lhs, const auto* rhs) {
                             return lhs->baseline_global_order < rhs->baseline_global_order;
                         });
        std::stable_sort(candidates->second.begin(), candidates->second.end(),
                         [](const auto& lhs, const auto& rhs) {
                             return lhs.global_order < rhs.global_order;
                         });
        for (size_t i = 0; i < origin_group.second.size(); ++i) {
            const MapEditIdentityOrigin& origin = *origin_group.second[i];
            const CandidateIdentityElement& candidate_element = candidates->second[i];
            auto after = after_by_native_id.find(candidate_element.native_edit_id);
            auto expected = expected_target_canonical.find(origin.edit_id);
            if (after == after_by_native_id.end() ||
                expected == expected_target_canonical.end() ||
                after->second->canonical != expected->second) {
                report.blocking_errors.push_back(
                    "edited target did not reparse to its expected semantic value: " +
                    origin.edit_id);
                return;
            }
            preserve_edit_identity(origin.edit_id, candidate_element.native_edit_id);
            candidate_target_ids.insert(candidate_element.native_edit_id);
        }
    }
    if (!report.blocking_errors.empty()) return;

    std::vector<const SemanticElement*> before_non_targets;
    before_non_targets.reserve(before_elements.size() - excluded_before.size());
    for (const SemanticElement& element : before_elements) {
        if (excluded_before.find(element.edit_id) == excluded_before.end()) {
            before_non_targets.push_back(&element);
        }
    }
    size_t before_position = 0;
    for (const SemanticElement& element : after_elements) {
        if (candidate_target_ids.find(element.edit_id) != candidate_target_ids.end()) continue;
        if (before_position >= before_non_targets.size() ||
            element.canonical != before_non_targets[before_position]->canonical) {
            report.non_target_changed_count = 1;
            report.blocking_errors.push_back(
                "full reparse changed one or more non-target evaluated elements");
            return;
        }
        preserve_edit_identity(before_non_targets[before_position]->edit_id,
                               element.edit_id);
        ++before_position;
    }
    if (before_position != before_non_targets.size() ||
        candidate_target_ids.size() != expected_target_canonical.size()) {
        report.non_target_changed_count =
            before_position == before_non_targets.size() ? 0 : 1;
        report.blocking_errors.push_back(
            candidate_target_ids.size() != expected_target_canonical.size()
                ? "edited targets did not reparse to the expected semantic set"
                : "full reparse changed one or more non-target evaluated elements");
        return;
    }
    if (!report.blocking_errors.empty()) return;

    // editId contains global_order, so moving a statement can regenerate IDs for
    // both the target and every crossed element. The GUI intentionally keeps the
    // disk-baseline IDs while a batch is pending so it can reset and replay the
    // complete ledger. Preserve those validated identities in the working-copy
    // context instead of forcing the GUI to guess an old-to-new row mapping.
    candidate->native_element_edit_id_to_stable.clear();
    candidate->native_element_edit_id_to_stable.reserve(
        candidate_to_stable_edit_ids.size());
    for (const auto& identity : candidate_to_stable_edit_ids) {
        candidate->native_element_edit_id_to_stable.emplace(
            identity.first, identity.second);
    }
    candidate->element_edit_id_cache.clear();

    candidate->disk_native_element_edit_id_to_stable =
        baseline.disk_native_element_edit_id_to_stable;
    candidate->disk_source_hashes_for_stable_ids =
        baseline.disk_source_hashes_for_stable_ids;
    if (candidate->disk_native_element_edit_id_to_stable.empty()) {
        if (!baseline.native_element_edit_id_to_stable.empty()) {
            candidate->disk_native_element_edit_id_to_stable =
                baseline.native_element_edit_id_to_stable;
        } else {
            candidate->disk_native_element_edit_id_to_stable.reserve(
                before_elements.size());
            for (const SemanticElement& element : before_elements) {
                candidate->disk_native_element_edit_id_to_stable.emplace(
                    element.edit_id, element.edit_id);
            }
        }
    }
    if (candidate->disk_source_hashes_for_stable_ids.empty()) {
        candidate->disk_source_hashes_for_stable_ids.reserve(
            baseline.source_files.size());
        for (const SourceFileRecord& file : baseline.source_files) {
            candidate->disk_source_hashes_for_stable_ids.emplace(
                file.source_key, file.source_hash);
        }
    }

    std::string derived_error;
    if (!validate_non_target_derived_state(
            baseline, *candidate, excluded_before, derived_error)) {
        report.non_target_changed_count = 1;
        report.blocking_errors.push_back(std::move(derived_error));
        return;
    }

    if (!variable_environment_equal(baseline.variables, baseline.distance,
                                    candidate->variables, candidate->distance)) {
        report.non_target_changed_count = 1;
        report.blocking_errors.push_back(
            "full reparse changed the final variable or distance environment");
        return;
    }

    report.target_distance_match_count = expected_distance_target_count;
    report.validation_fingerprint = after_snapshot.full_fingerprint;
    candidate->edit_validation_fingerprint = report.validation_fingerprint;
    candidate->edit_validation_current = true;
    report.validated_context = std::shared_ptr<MapContext>(candidate.release());
    report.full_reparse_ok = true;
}

DistanceBoundaryPlan find_boundary_by_token(const MapContext& ctx,
                                            const SourcePatch& patch,
                                            const DistanceSectionAnalysis& section,
                                            const std::string& token) {
    if (token.empty() || section.anchors.size() < 2) return {};
    for (size_t pos = 0; pos + 1 < section.anchors.size(); ++pos) {
        DistanceBoundaryPlan boundary = boundary_after_anchor(ctx, patch, section, pos);
        if (boundary.valid() && boundary.token == token) return boundary;
    }
    return {};
}

std::string common_group_expression(const DistanceEditGroup& group,
                                    const std::vector<PreparedEdit>& prepared,
                                    bool manual_only,
                                    bool& conflict) {
    conflict = false;
    std::string expression;
    for (size_t index : group.member_indices) {
        const PreparedEdit& edit = prepared[index];
        const std::string candidate = manual_only
            ? trim_field_copy(edit.change->distance_expression)
            : trim_field_copy(edit.suggested_distance_expression);
        if (candidate.empty()) continue;
        if (expression.empty()) expression = candidate;
        else if (expression != candidate) conflict = true;
    }
    return expression;
}

std::set<std::string> group_statement_variables(const DistanceEditGroup& group,
                                                const std::vector<PreparedEdit>& prepared,
                                                const std::string& distance_expression,
                                                bool include_distance_expression) {
    std::set<std::string> variables;
    for (size_t index : group.member_indices) {
        std::set<std::string> statement_variables =
            referenced_variables(prepared[index].replacement_statement);
        variables.insert(statement_variables.begin(), statement_variables.end());
    }
    if (include_distance_expression) {
        std::set<std::string> expression_variables = referenced_variables(distance_expression);
        variables.insert(expression_variables.begin(), expression_variables.end());
    }
    return variables;
}

bool validate_distance_group_environment(
    MapContext& ctx,
    const SourcePatch& patch,
    const DistanceEditGroup& group,
    const std::vector<PreparedEdit>& prepared,
    const DistanceBoundaryPlan& boundary,
    const std::string& distance_expression,
    bool create_distance_block,
    bool force_distance_expression_check,
    bool has_manual_distance_expression,
    bool confirm_environment_mismatch,
    const DistancePlanningIndex& distance_index,
    MapEditReport& report) {
    const ParsedStatement& destination_anchor = ctx.parsed_statements[
        group.section.anchors[boundary.before_anchor_position]];
    const bool expression_context_matters = create_distance_block ||
        force_distance_expression_check ||
        trim_field_copy(destination_anchor.distance_expression) !=
            trim_field_copy(distance_expression);
    const std::set<std::string> statement_variables =
        group_statement_variables(group, prepared, {}, false);
    const std::set<std::string> expression_variables = expression_context_matters
        ? referenced_variables(distance_expression)
        : std::set<std::string>{};

    if (expression_context_matters && !has_manual_distance_expression) {
        const std::string multi_value = first_multivalued_variable(
            ctx, group.section, distance_index, expression_variables);
        if (!multi_value.empty()) {
            append_resolution_request(ctx, patch, group, prepared,
                                      "variableHasMultipleContextValues",
                                      multi_value, false, boundary.token, report);
            return false;
        }
    }

    std::string statement_mismatch;
    std::string expression_mismatch;
    for (size_t index : group.member_indices) {
        const PreparedEdit& edit = prepared[index];
        const ParsedStatement& origin = ctx.parsed_statements[edit.target.statement_index];
        if (statement_mismatch.empty()) {
            statement_mismatch = first_environment_mismatch(
                origin.variable_environment, boundary.variable_environment,
                statement_variables);
        }
        if (expression_mismatch.empty() && expression_context_matters) {
            expression_mismatch = first_environment_mismatch(
                origin.variable_environment, boundary.variable_environment,
                expression_variables);
        }
    }
    if (create_distance_block &&
        std::any_of(group.member_indices.begin(), group.member_indices.end(),
                    [&](size_t index) {
                        return expression_references_predefined_distance(
                            prepared[index].replacement_statement);
                    })) {
        statement_mismatch = statement_mismatch.empty() ? "distance" : statement_mismatch;
    }
    if (create_distance_block &&
        expression_references_predefined_distance(distance_expression)) {
        expression_mismatch = expression_mismatch.empty() ? "distance" : expression_mismatch;
    }

    if (!statement_mismatch.empty() && !confirm_environment_mismatch) {
        const std::string multi_value = first_multivalued_variable(
            ctx, group.section, distance_index, statement_variables);
        append_resolution_request(ctx, patch, group, prepared,
                                  multi_value.empty()
                                      ? "incompatibleEvaluationEnvironment"
                                      : "variableHasMultipleContextValues",
                                  multi_value.empty() ? statement_mismatch : multi_value,
                                  !create_distance_block, boundary.token, report);
        return false;
    }
    if (!expression_mismatch.empty() &&
        !confirm_environment_mismatch && !has_manual_distance_expression) {
        append_resolution_request(ctx, patch, group, prepared,
                                  "incompatibleEvaluationEnvironment",
                                  expression_mismatch, !create_distance_block,
                                  boundary.token, report);
        return false;
    }
    return true;
}

bool resolve_distance_group(MapContext& ctx,
                            const SourcePatch& patch,
                            const DistanceEditGroup& group,
                            const std::vector<PreparedEdit>& prepared,
                            DistancePlanningIndex& distance_index,
                            ResolvedDistanceGroup& resolved,
                            MapEditReport& report) {
    resolved.group = &group;
    resolved.direction = group.section.direction;
    if (group.member_indices.empty()) return false;

    for (size_t index : group.member_indices) {
        const MapEditChange& change = *prepared[index].change;
        if (!change.distance_resolution_key.empty() &&
            change.distance_resolution_key != group.key) {
            append_resolution_request(ctx, patch, group, prepared,
                                      "staleDistanceResolution", {}, false, {}, report);
            return false;
        }
    }

    std::string selected_boundary_token;
    bool boundary_token_conflict = false;
    bool confirm_environment_mismatch = false;
    for (size_t index : group.member_indices) {
        const MapEditChange& change = *prepared[index].change;
        if (!change.distance_boundary_token.empty()) {
            if (selected_boundary_token.empty()) {
                selected_boundary_token = change.distance_boundary_token;
            } else if (selected_boundary_token != change.distance_boundary_token) {
                boundary_token_conflict = true;
            }
        }
        confirm_environment_mismatch =
            confirm_environment_mismatch || change.confirm_environment_mismatch;
    }
    if (boundary_token_conflict) {
        append_resolution_request(ctx, patch, group, prepared,
                                  "conflictingManualBoundaries", {}, false, {}, report);
        return false;
    }

    bool manual_expression_conflict = false;
    std::string manual_expression =
        common_group_expression(group, prepared, true, manual_expression_conflict);
    if (manual_expression_conflict) {
        append_resolution_request(ctx, patch, group, prepared,
                                  "conflictingManualDistanceExpressions", {}, false, {}, report);
        return false;
    }

    if (!selected_boundary_token.empty()) {
        DistanceBoundaryPlan boundary = find_boundary_by_token(
            ctx, patch, group.section, selected_boundary_token);
        if (!boundary.valid()) {
            append_resolution_request(ctx, patch, group, prepared,
                                      "staleDistanceBoundary", {}, false, {}, report);
            return false;
        }
        const ParsedStatement& before =
            ctx.parsed_statements[group.section.anchors[boundary.before_anchor_position]];
        bool expression_conflict = false;
        std::string expression = manual_expression;
        if (expression.empty()) {
            expression = common_group_expression(group, prepared, false, expression_conflict);
        }
        if (expression_conflict || expression.empty()) {
            append_resolution_request(ctx, patch, group, prepared,
                                      "distanceExpressionRequiresManualEdit", {}, false,
                                      selected_boundary_token, report);
            return false;
        }
        resolved.boundary = std::move(boundary);
        resolved.create_distance_block =
            !exact_distance_value(before.distance_value, group.target_distance) ||
            (!manual_expression.empty() &&
             trim_field_copy(before.distance_expression) != manual_expression);
        resolved.distance_expression = std::move(expression);
        if (!validate_distance_group_environment(
                ctx, patch, group, prepared, resolved.boundary,
                resolved.distance_expression, resolved.create_distance_block,
                true, !manual_expression.empty(),
                confirm_environment_mismatch, distance_index, report)) {
            return false;
        }
        return true;
    }

    if (!group.section.resolved) {
        append_resolution_request(ctx, patch, group, prepared,
                                  "ambiguousSourceSection", {}, false, {}, report);
        return false;
    }

    std::vector<size_t> numeric_positions;
    for (size_t pos = group.section.first_position;
         pos <= group.section.last_position && pos < group.section.anchors.size(); ++pos) {
        const ParsedStatement& anchor = ctx.parsed_statements[group.section.anchors[pos]];
        if (exact_distance_value(anchor.distance_value, group.target_distance)) {
            numeric_positions.push_back(pos);
        }
    }
    if (numeric_positions.size() > 1) {
        append_resolution_request(ctx, patch, group, prepared,
                                  "multipleEquivalentDistanceBlocks", {}, false, {}, report);
        return false;
    }

    bool suggested_expression_conflict = false;
    std::string suggested_expression = common_group_expression(
        group, prepared, false, suggested_expression_conflict);
    if (suggested_expression.empty()) {
        append_resolution_request(ctx, patch, group, prepared,
                                  "distanceExpressionRequiresManualEdit", {}, false, {}, report);
        return false;
    }

    size_t destination_before_position = kNoSourceRef;
    bool create_distance_block = false;
    if (numeric_positions.size() == 1) {
        destination_before_position = numeric_positions.front();
    } else {
        std::vector<size_t> bracket_positions;
        for (size_t pos = group.section.first_position;
             pos < group.section.last_position && pos + 1 < group.section.anchors.size(); ++pos) {
            double before = ctx.parsed_statements[group.section.anchors[pos]].distance_value;
            double after = ctx.parsed_statements[group.section.anchors[pos + 1]].distance_value;
            bool bracketed = group.section.direction == "increasing"
                ? before < group.target_distance && group.target_distance < after
                : before > group.target_distance && group.target_distance > after;
            if (bracketed) bracket_positions.push_back(pos);
        }
        if (bracket_positions.size() != 1) {
            append_resolution_request(ctx, patch, group, prepared,
                                      bracket_positions.empty()
                                          ? "noUniqueDistanceBracket"
                                          : "multipleDistanceBrackets",
                                      {}, false, {}, report);
            return false;
        }
        destination_before_position = bracket_positions.front();
        create_distance_block = true;
    }

    DistanceBoundaryPlan boundary = boundary_after_anchor(
        ctx, patch, group.section, destination_before_position);
    if (!boundary.valid() && !create_distance_block &&
        destination_before_position + 1 == group.section.anchors.size()) {
        boundary = terminal_boundary_for_last_anchor(
            ctx, patch, group.section, destination_before_position, distance_index);
    }
    if (!boundary.valid()) {
        append_resolution_request(ctx, patch, group, prepared,
                                  "destinationBoundaryUnavailable", {}, false, {}, report);
        return false;
    }

    bool has_manual_distance_expression = false;
    for (size_t index : group.member_indices) {
        has_manual_distance_expression = has_manual_distance_expression ||
            !trim_field_copy(prepared[index].change->distance_expression).empty();
    }
    if (suggested_expression_conflict && create_distance_block) {
        append_resolution_request(ctx, patch, group, prepared,
                                  "distanceExpressionRequiresManualEdit", {}, false,
                                  boundary.token, report);
        return false;
    }

    if (!validate_distance_group_environment(
            ctx, patch, group, prepared, boundary, suggested_expression,
            create_distance_block, false, has_manual_distance_expression,
            confirm_environment_mismatch, distance_index, report)) {
        return false;
    }

    resolved.boundary = std::move(boundary);
    resolved.create_distance_block = create_distance_block;
    resolved.distance_expression = suggested_expression;
    return true;
}

bool physical_include_instances_are_compatible(
    MapContext& ctx,
    const SourcePatch& patch,
    const DistanceEditGroup& group,
    const std::vector<PreparedEdit>& prepared,
    const std::unordered_set<size_t>& targeted_statement_indices,
    const ResolvedDistanceGroup& resolved,
    DistancePlanningIndex& distance_index,
    std::string& mismatch_variable) {
    const bool user_selected_boundary = std::any_of(
        group.member_indices.begin(), group.member_indices.end(),
        [&](size_t index) {
            return !prepared[index].change->distance_boundary_token.empty();
        });
    const bool primary_terminal = resolved.boundary.terminal_context_boundary;
    size_t primary_after_byte = 0;
    if (!primary_terminal) {
        const size_t primary_after_index =
            group.section.anchors[resolved.boundary.after_anchor_position];
        primary_after_byte = ctx.parsed_statements[primary_after_index].source.byte_start;
    }
    for (size_t member_index : group.member_indices) {
        const PreparedEdit& member = prepared[member_index];
        const ParsedStatement& primary = ctx.parsed_statements[member.target.statement_index];
        for (size_t statement_index : distance_index.physical_counterparts(primary)) {
            if (statement_index == member.target.statement_index) continue;
            const ParsedStatement& counterpart = ctx.parsed_statements[statement_index];
            if (same_statement_context(ctx, counterpart.source, primary.source)) {
                continue;
            }
            if (targeted_statement_indices.find(statement_index) !=
                targeted_statement_indices.end()) {
                // The physical source rewrite necessarily affects every Include
                // invocation. A separately planned counterpart is validated by its
                // own group and by the final whole-map semantic comparison.
                continue;
            }

            DistanceSectionAnalysis section =
                analyze_distance_section(ctx, statement_index, distance_index);
            if (!section.resolved) return false;
            size_t before_position = kNoSourceRef;
            bool create_block = false;
            if (user_selected_boundary) {
                for (size_t pos = 0; pos + 1 < section.anchors.size(); ++pos) {
                    const ParsedStatement& after =
                        ctx.parsed_statements[section.anchors[pos + 1]];
                    if (after.source.byte_start != primary_after_byte) continue;
                    if (before_position != kNoSourceRef) return false;
                    before_position = pos;
                }
                create_block = resolved.create_distance_block;
            } else {
                std::vector<size_t> numeric_positions;
                for (size_t pos = section.first_position; pos <= section.last_position; ++pos) {
                    if (exact_distance_value(
                            ctx.parsed_statements[section.anchors[pos]].distance_value,
                            group.target_distance)) {
                        numeric_positions.push_back(pos);
                    }
                }
                if (numeric_positions.size() == 1) {
                    before_position = numeric_positions.front();
                } else if (numeric_positions.empty()) {
                    for (size_t pos = section.first_position; pos < section.last_position; ++pos) {
                        const double before =
                            ctx.parsed_statements[section.anchors[pos]].distance_value;
                        const double after =
                            ctx.parsed_statements[section.anchors[pos + 1]].distance_value;
                        const bool bracketed = section.direction == "increasing"
                            ? before < group.target_distance && group.target_distance < after
                            : before > group.target_distance && group.target_distance > after;
                        if (!bracketed) continue;
                        if (before_position != kNoSourceRef) return false;
                        before_position = pos;
                    }
                    create_block = true;
                } else {
                    return false;
                }
            }
            if (before_position == kNoSourceRef) return false;
            DistanceBoundaryPlan boundary = boundary_after_anchor(
                ctx, patch, section, before_position);
            if (!boundary.valid() && !create_block &&
                before_position + 1 == section.anchors.size()) {
                boundary = terminal_boundary_for_last_anchor(
                    ctx, patch, section, before_position, distance_index);
            }
            const bool same_physical_boundary = boundary.valid() &&
                boundary.terminal_context_boundary == primary_terminal &&
                (primary_terminal
                    ? boundary.insert_offset == resolved.boundary.insert_offset
                    : ctx.parsed_statements[section.anchors[boundary.after_anchor_position]]
                          .source.byte_start == primary_after_byte);
            if (!same_physical_boundary ||
                create_block != resolved.create_distance_block) {
                return false;
            }

            std::set<std::string> statement_variables =
                referenced_variables(member.replacement_statement);
            mismatch_variable = first_environment_mismatch(
                counterpart.variable_environment, boundary.variable_environment,
                statement_variables);
            if (!mismatch_variable.empty()) return false;
            if (create_block || user_selected_boundary) {
                std::set<std::string> expression_variables =
                    referenced_variables(resolved.distance_expression);
                mismatch_variable = first_environment_mismatch(
                    counterpart.variable_environment, boundary.variable_environment,
                    expression_variables);
                if (!mismatch_variable.empty()) return false;
            }
        }
    }
    return true;
}

MapEditReport build_edit_report(MapContext& ctx,
                                const std::vector<MapEditChange>& changes,
                                bool write_files) {
    MapEditReport report;
    std::map<size_t, SourcePatch> patches;
    DistancePlanningIndex distance_index(ctx);

    auto change_signature = [](const MapEditChange& change) {
        std::ostringstream out;
        out << change.operation << "\n" << change.replacement_statement << "\n"
            << change.target_file_path << "\n" << change.insert_before_edit_id << "\n"
            << change.expected_source_hash << "\n" << change.distance_resolution_key << "\n"
            << change.distance_boundary_token << "\n" << change.distance_expression << "\n"
            << change.confirm_environment_mismatch << "\n";
        for (const auto& field : change.field_changes) {
            out << field.first.size() << ":" << field.first << "="
                << field.second.size() << ":" << field.second << "\n";
        }
        return out.str();
    };

    std::map<std::string, std::string> edit_signatures;
    std::vector<const MapEditChange*> effective_changes;
    effective_changes.reserve(changes.size());
    for (const MapEditChange& change : changes) {
        if (change.edit_id.empty()) {
            report.blocking_errors.push_back("edit change is missing editId");
            continue;
        }
        std::string signature = change_signature(change);
        auto existing = edit_signatures.find(change.edit_id);
        if (existing != edit_signatures.end()) {
            if (existing->second != signature) {
                report.blocking_errors.push_back(
                    "conflicting duplicate editId in one edit batch: " + change.edit_id);
            }
            continue;
        }
        edit_signatures.emplace(change.edit_id, std::move(signature));
        effective_changes.push_back(&change);
    }
    if (!report.blocking_errors.empty()) return report;

    std::vector<PreparedEdit> prepared;
    prepared.reserve(effective_changes.size());
    for (size_t input_ordinal = 0; input_ordinal < effective_changes.size(); ++input_ordinal) {
        const MapEditChange& change = *effective_changes[input_ordinal];
        if (change.edit_id.empty()) {
            report.blocking_errors.push_back("edit change is missing editId");
            continue;
        }
        std::string operation = ascii_lower(change.operation.empty() ? "update" : change.operation);
        EditableTarget target = find_editable_target(ctx, change.edit_id);
        if (target.statement_index == kNoSourceRef ||
            target.statement_index >= ctx.parsed_statements.size()) {
            report.blocking_errors.push_back("unsupported or unknown editId: " + change.edit_id);
            continue;
        }

        const ParsedStatement& statement = ctx.parsed_statements[target.statement_index];
        if (statement.source.source_file_index >= ctx.source_files.size()) {
            report.blocking_errors.push_back("edit target has no source file: " + change.edit_id);
            continue;
        }
        const SourceFileRecord& file = ctx.source_files[statement.source.source_file_index];
        SourcePatch& patch = patches[statement.source.source_file_index];
        if (!patch.record) {
            try {
                patch = load_source_patch(ctx, file);
            } catch (const std::exception& e) {
                report.blocking_errors.push_back(e.what());
                continue;
            }
        }
        const std::string& expected_hash = change.expected_source_hash.empty()
            ? file.source_hash
            : change.expected_source_hash;
        if (!expected_hash.empty() && patch.current_hash != expected_hash &&
            patch.base_hash != expected_hash) {
            report.blocking_errors.push_back("source file changed externally: " + file.file_path);
            continue;
        }

        try {
            auto range = source_range_in_text(patch, statement.source);
            PreparedEdit edit;
            edit.change = &change;
            edit.input_ordinal = input_ordinal;
            edit.target = target;
            edit.source_file_index = statement.source.source_file_index;
            edit.source_range = range;
            edit.removal_range = safe_statement_removal_range(patch, range);
            const size_t source_line_start = offset_from_line_column(
                patch.text, statement.source.line, 1);
            if (source_line_start != std::string::npos &&
                source_line_start <= range.first) {
                std::string indent = patch.text.substr(
                    source_line_start, range.first - source_line_start);
                if (std::all_of(indent.begin(), indent.end(), [](char ch) {
                        return ch == ' ' || ch == '\t';
                    })) {
                    edit.source_indent = std::move(indent);
                }
            }
            edit.operation = operation;
            if (operation == "delete") {
                if (target.elements_for_statement != 1) {
                    report.blocking_errors.push_back("delete is blocked because the source statement maps to multiple elements: " + change.edit_id);
                    continue;
                }
                ++report.delete_count;
            } else if (operation == "update") {
                if (target.elements_for_statement != 1) {
                    report.blocking_errors.push_back("update is blocked because the source statement maps to multiple elements: " + change.edit_id);
                    continue;
                }
                edit.replacement_statement = build_replacement_statement(change, statement, target);
                if (has_field_change(change, "distance")) {
                    const std::string target_text = normalized_number_arg(
                        field_text_or(change, "distance",
                                      fallback_edit_number(statement.distance_value)));
                    if (!parse_edit_number(target_text, edit.target_distance)) {
                        throw std::runtime_error("invalid numeric edit value: " + target_text);
                    }
                    edit.moves_distance =
                        !exact_distance_value(edit.target_distance, statement.distance_value);
                    if (edit.moves_distance) {
                        std::string old_expression = trim_field_copy(statement.distance_expression);
                        if (old_expression.empty()) {
                            old_expression = fallback_edit_number(statement.distance_value);
                        }
                        try {
                            edit.suggested_distance_expression = adjust_distance_expression_by_delta(
                                old_expression, edit.target_distance - statement.distance_value);
                        } catch (const std::exception&) {
                            edit.suggested_distance_expression.clear();
                        }
                        if (!change.distance_expression.empty()) {
                            edit.suggested_distance_expression =
                                trim_field_copy(change.distance_expression);
                        }
                        edit.section = analyze_distance_section(
                            ctx, target.statement_index, distance_index);
                    }
                }
                ++report.update_count;
            } else if (operation == "insert") {
                report.blocking_errors.push_back("insert edits are not implemented for this target yet: " + change.edit_id);
                continue;
            } else {
                report.blocking_errors.push_back("unknown edit operation: " + change.operation);
                continue;
            }
            prepared.push_back(std::move(edit));
        } catch (const std::exception& e) {
            report.blocking_errors.push_back(std::string("edit change failed for ") + change.edit_id + ": " + e.what());
        }
    }

    if (!report.blocking_errors.empty()) return report;

    std::map<std::string, DistanceEditGroup> distance_groups;
    for (size_t index = 0; index < prepared.size(); ++index) {
        PreparedEdit& edit = prepared[index];
        if (!edit.moves_distance) continue;
        const ParsedStatement& statement = ctx.parsed_statements[edit.target.statement_index];
        const std::string key = source_section_key(
            ctx, edit.section, statement, edit.target_distance);
        DistanceEditGroup& group = distance_groups[key];
        if (group.member_indices.empty()) {
            group.key = key;
            group.source_file_index = edit.source_file_index;
            group.target_distance = edit.target_distance;
            group.section = edit.section;
        }
        group.member_indices.push_back(index);
    }
    report.distance_group_count = static_cast<int>(distance_groups.size());

    std::vector<ResolvedDistanceGroup> resolved_groups;
    resolved_groups.reserve(distance_groups.size());
    std::unordered_set<size_t> targeted_distance_statement_indices;
    targeted_distance_statement_indices.reserve(prepared.size());
    for (const PreparedEdit& edit : prepared) {
        if (edit.moves_distance) {
            targeted_distance_statement_indices.insert(edit.target.statement_index);
        }
    }
    for (auto& group_entry : distance_groups) {
        DistanceEditGroup& group = group_entry.second;
        SourcePatch& patch = patches[group.source_file_index];
        ResolvedDistanceGroup resolved;
        if (resolve_distance_group(
                ctx, patch, group, prepared, distance_index, resolved, report)) {
            std::string mismatch_variable;
            if (!physical_include_instances_are_compatible(
                    ctx, patch, group, prepared,
                    targeted_distance_statement_indices, resolved,
                    distance_index, mismatch_variable)) {
                append_resolution_request(ctx, patch, group, prepared,
                                          "physicalSourceHasIncompatibleIncludeContexts",
                                          mismatch_variable, false,
                                          resolved.boundary.token, report);
            } else {
                resolved_groups.push_back(std::move(resolved));
            }
        }
    }
    if (!report.ok()) {
        report.previews.clear();
        report.patched_files.clear();
        report.changed_files.clear();
        return report;
    }

    struct InsertionStatement {
        size_t source_offset = 0;
        size_t input_ordinal = 0;
        std::string text;
        size_t statement_begin = 0;
        size_t statement_length = 0;
        std::vector<TextReplacementIdentity> identities;
    };
    struct InsertionPart {
        double target_distance = 0.0;
        std::string direction;
        bool create_distance_block = false;
        std::string distance_expression;
        std::string distance_indent;
        std::vector<InsertionStatement> statements;
        std::string identity;
        size_t first_source_offset = std::numeric_limits<size_t>::max();
    };
    std::map<std::pair<size_t, size_t>, std::vector<InsertionPart>> insertion_parts;
    std::map<std::tuple<size_t, size_t, size_t>, std::string> replacement_by_range;

    auto append_replacement_identity = [&](TextReplacement& replacement,
                                           const PreparedEdit* identity_edit) {
        if (!identity_edit || identity_edit->operation == "delete") return;
        const ParsedStatement& baseline_statement =
            ctx.parsed_statements[identity_edit->target.statement_index];
        replacement.identities.push_back({
            identity_edit->change->edit_id,
            identity_edit->target.row_kind,
            0,
            replacement.text.size(),
            identity_edit->target.element_index,
            baseline_statement.global_order,
        });
    };

    auto add_source_replacement = [&](size_t file_index,
                                      size_t begin,
                                      size_t end,
                                      const std::string& text,
                                      const PreparedEdit* identity_edit) {
        auto key = std::make_tuple(file_index, begin, end);
        auto existing = replacement_by_range.find(key);
        if (existing != replacement_by_range.end()) {
            if (existing->second != text) {
                report.blocking_errors.push_back(
                    "conflicting edits target the same physical source statement");
            } else if (identity_edit) {
                auto replacement = std::find_if(
                    patches[file_index].replacements.begin(),
                    patches[file_index].replacements.end(),
                    [&](const TextReplacement& candidate) {
                        return candidate.begin == begin && candidate.end == end;
                    });
                if (replacement != patches[file_index].replacements.end()) {
                    append_replacement_identity(*replacement, identity_edit);
                }
            }
            return;
        }
        replacement_by_range.emplace(key, text);
        TextReplacement replacement;
        replacement.begin = begin;
        replacement.end = end;
        replacement.text = text;
        append_replacement_identity(replacement, identity_edit);
        patches[file_index].replacements.push_back(std::move(replacement));
    };

    for (const PreparedEdit& edit : prepared) {
        if (edit.moves_distance) {
            add_source_replacement(edit.source_file_index,
                                   edit.removal_range.first,
                                   edit.removal_range.second,
                                   {}, nullptr);
        } else {
            add_source_replacement(edit.source_file_index,
                                   edit.operation == "delete" ? edit.removal_range.first
                                                              : edit.source_range.first,
                                   edit.operation == "delete" ? edit.removal_range.second
                                                              : edit.source_range.second,
                                   edit.operation == "delete" ? std::string{}
                                                              : edit.replacement_statement,
                                   edit.operation == "delete" ? nullptr : &edit);
        }
    }

    for (const ResolvedDistanceGroup& resolved : resolved_groups) {
        const DistanceEditGroup& group = *resolved.group;
        std::vector<size_t> members = group.member_indices;
        std::stable_sort(members.begin(), members.end(), [&](size_t lhs, size_t rhs) {
            const PreparedEdit& a = prepared[lhs];
            const PreparedEdit& b = prepared[rhs];
            const SourceSpan& as = ctx.parsed_statements[a.target.statement_index].source;
            const SourceSpan& bs = ctx.parsed_statements[b.target.statement_index].source;
            if (as.byte_start != bs.byte_start) return as.byte_start < bs.byte_start;
            return a.input_ordinal < b.input_ordinal;
        });

        InsertionPart part;
        part.target_distance = group.target_distance;
        part.direction = resolved.direction;
        part.create_distance_block = resolved.create_distance_block;
        part.distance_expression = resolved.distance_expression;
        if (!resolved.boundary.terminal_context_boundary) {
            const ParsedStatement& boundary_after = ctx.parsed_statements[
                group.section.anchors[resolved.boundary.after_anchor_position]];
            auto boundary_after_range = source_range_in_text(
                patches[group.source_file_index], boundary_after.source);
            if (resolved.boundary.insert_offset <= boundary_after_range.first) {
                part.distance_indent = patches[group.source_file_index].text.substr(
                    resolved.boundary.insert_offset,
                    boundary_after_range.first - resolved.boundary.insert_offset);
            }
        }
        std::map<std::pair<size_t, size_t>, size_t> physical_statements;
        std::ostringstream identity;
        // Absolute target values may differ between executions of the same
        // physical Include file. The physical patch is identical when its
        // expression and moved source statements are identical, so deduplicate
        // on those facts rather than the per-invocation evaluated distance.
        identity << part.create_distance_block << "\n"
                 << part.distance_expression << "\n";
        for (size_t index : members) {
            const PreparedEdit& edit = prepared[index];
            TextReplacementIdentity statement_identity{
                edit.change->edit_id,
                edit.target.row_kind,
                0,
                0,
                edit.target.element_index,
                ctx.parsed_statements[edit.target.statement_index].global_order,
            };
            auto inserted = physical_statements.emplace(
                edit.source_range, part.statements.size());
            if (inserted.second) {
                InsertionStatement statement;
                statement.source_offset = edit.source_range.first;
                statement.input_ordinal = edit.input_ordinal;
                statement.text = edit.source_indent + edit.replacement_statement;
                statement.statement_begin = edit.source_indent.size();
                statement.statement_length = edit.replacement_statement.size();
                statement.identities.push_back(std::move(statement_identity));
                part.statements.push_back(std::move(statement));
            } else {
                part.statements[inserted.first->second].identities.push_back(
                    std::move(statement_identity));
            }
            part.first_source_offset = std::min(part.first_source_offset,
                                                edit.source_range.first);
            identity << edit.source_range.first << ":" << edit.source_range.second << "="
                     << edit.replacement_statement << "\n";
        }
        part.identity = hex64(stable_hash64(identity.str()));
        auto insertion_key = std::make_pair(group.source_file_index,
                                            resolved.boundary.insert_offset);
        auto& parts = insertion_parts[insertion_key];
        auto duplicate = std::find_if(parts.begin(), parts.end(),
                                      [&](const InsertionPart& existing) {
                                          return existing.identity == part.identity;
                                      });
        if (duplicate == parts.end()) {
            parts.push_back(std::move(part));
        } else {
            // Repeated Include invocations can resolve to one identical physical
            // patch. Coalesce the source text once, but retain every logical
            // element identity that the shared statement represents.
            for (InsertionStatement& statement : part.statements) {
                auto existing_statement = std::find_if(
                    duplicate->statements.begin(), duplicate->statements.end(),
                    [&](const InsertionStatement& candidate) {
                        return candidate.source_offset == statement.source_offset &&
                               candidate.text == statement.text;
                    });
                if (existing_statement == duplicate->statements.end()) {
                    duplicate->statements.push_back(std::move(statement));
                } else {
                    existing_statement->identities.insert(
                        existing_statement->identities.end(),
                        std::make_move_iterator(statement.identities.begin()),
                        std::make_move_iterator(statement.identities.end()));
                }
            }
        }
    }

    for (auto& insertion_entry : insertion_parts) {
        const size_t file_index = insertion_entry.first.first;
        const size_t offset = insertion_entry.first.second;
        std::vector<InsertionPart>& parts = insertion_entry.second;
        std::string direction;
        for (const InsertionPart& part : parts) {
            if (direction.empty()) direction = part.direction;
            else if (direction != part.direction) {
                report.blocking_errors.push_back(
                    "incompatible source-section directions share one physical insertion gap");
            }
        }
        std::stable_sort(parts.begin(), parts.end(), [&](const InsertionPart& a,
                                                         const InsertionPart& b) {
            if (a.target_distance == b.target_distance) {
                if (a.first_source_offset != b.first_source_offset) {
                    return a.first_source_offset < b.first_source_offset;
                }
                return a.identity < b.identity;
            }
            return direction == "decreasing"
                ? a.target_distance > b.target_distance
                : a.target_distance < b.target_distance;
        });
        SourcePatch& patch = patches[file_index];
        const std::string nl = newline_text(patch.record->newline);
        std::string insertion_body;
        std::vector<TextReplacementIdentity> insertion_identities;
        for (size_t part_begin = 0; part_begin < parts.size();) {
            size_t part_end = part_begin + 1;
            while (part_end < parts.size() &&
                   exact_distance_value(parts[part_begin].target_distance,
                                        parts[part_end].target_distance)) {
                ++part_end;
            }
            const InsertionPart& first_part = parts[part_begin];
            bool compatible = true;
            std::vector<InsertionStatement> statements;
            for (size_t part_index = part_begin; part_index < part_end; ++part_index) {
                const InsertionPart& part = parts[part_index];
                if (part.create_distance_block != first_part.create_distance_block ||
                    (part.create_distance_block &&
                     part.distance_expression != first_part.distance_expression)) {
                    compatible = false;
                }
                statements.insert(statements.end(), part.statements.begin(),
                                  part.statements.end());
            }
            if (!compatible) {
                report.blocking_errors.push_back(
                    "incompatible plans for one target distance share a physical insertion gap");
                part_begin = part_end;
                continue;
            }
            std::stable_sort(statements.begin(), statements.end(),
                             [](const InsertionStatement& a,
                                const InsertionStatement& b) {
                if (a.source_offset != b.source_offset) {
                    return a.source_offset < b.source_offset;
                }
                return a.input_ordinal < b.input_ordinal;
            });
            if (first_part.create_distance_block) {
                if (!insertion_body.empty()) insertion_body += nl;
                insertion_body += first_part.distance_indent +
                    first_part.distance_expression + ";";
                ++report.created_distance_block_count;
                ++report.insert_count;
            } else {
                ++report.reused_distance_block_count;
            }
            std::map<std::pair<size_t, std::string>, std::pair<size_t, size_t>>
                emitted_statements;
            for (const InsertionStatement& statement : statements) {
                const auto statement_key =
                    std::make_pair(statement.source_offset, statement.text);
                auto emitted = emitted_statements.find(statement_key);
                std::pair<size_t, size_t> statement_range;
                if (emitted == emitted_statements.end()) {
                    if (!insertion_body.empty()) insertion_body += nl;
                    const size_t text_begin = insertion_body.size();
                    insertion_body += statement.text;
                    statement_range = {
                        text_begin + statement.statement_begin,
                        text_begin + statement.statement_begin + statement.statement_length,
                    };
                    emitted_statements.emplace(statement_key, statement_range);
                } else {
                    statement_range = emitted->second;
                }
                for (const TextReplacementIdentity& source_identity : statement.identities) {
                    TextReplacementIdentity identity = source_identity;
                    identity.relative_begin = statement_range.first;
                    identity.relative_end = statement_range.second;
                    insertion_identities.push_back(std::move(identity));
                }
            }
            part_begin = part_end;
        }
        TextReplacement insertion;
        insertion.begin = offset;
        insertion.end = offset;
        insertion.text = statement_insertion_text(
            patch.text, offset, insertion_body, *patch.record);
        const size_t body_offset = insertion.text.find(insertion_body);
        if (body_offset == std::string::npos) {
            report.blocking_errors.push_back(
                "failed to retain edit identity in a generated distance insertion");
        } else {
            for (TextReplacementIdentity& identity : insertion_identities) {
                identity.relative_begin += body_offset;
                identity.relative_end += body_offset;
            }
            insertion.identities = std::move(insertion_identities);
        }
        patch.replacements.push_back(std::move(insertion));
    }

    for (auto& patch_entry : patches) {
        SourcePatch& patch = patch_entry.second;
        if (!patch.record || patch.replacements.empty()) continue;
        std::sort(patch.replacements.begin(), patch.replacements.end(),
                  [](const TextReplacement& a, const TextReplacement& b) {
                      if (a.begin != b.begin) return a.begin > b.begin;
                      return a.end > b.end;
                  });
        for (size_t i = 1; i < patch.replacements.size(); ++i) {
            const TextReplacement& previous = patch.replacements[i - 1];
            const TextReplacement& current = patch.replacements[i];
            if (current.end > previous.begin) {
                report.blocking_errors.push_back("overlapping edits in source file: " + patch.record->file_path);
                break;
            }
        }
    }

    if (!report.ok()) return report;

    for (auto& patch_entry : patches) {
        SourcePatch& patch = patch_entry.second;
        if (!patch.record || patch.replacements.empty()) continue;
        std::string patched_text = patch.text;
        std::vector<MapEditIdentityOrigin> resolved_identities;
        for (const TextReplacement& replacement : patch.replacements) {
            report.previews.push_back({
                patch.record->file_path,
                preview_fragment(patched_text, replacement.begin, replacement.end),
                preview_fragment(replacement.text, 0, replacement.text.size())
            });
            const std::ptrdiff_t delta =
                static_cast<std::ptrdiff_t>(replacement.text.size()) -
                static_cast<std::ptrdiff_t>(replacement.end - replacement.begin);
            for (MapEditIdentityOrigin& identity : resolved_identities) {
                if (identity.text_start < replacement.end) continue;
                identity.text_start = static_cast<size_t>(
                    static_cast<std::ptrdiff_t>(identity.text_start) + delta);
                identity.text_end = static_cast<size_t>(
                    static_cast<std::ptrdiff_t>(identity.text_end) + delta);
            }
            patched_text.replace(replacement.begin,
                                 replacement.end - replacement.begin,
                                 replacement.text);
            for (const TextReplacementIdentity& source_identity : replacement.identities) {
                if (source_identity.relative_end < source_identity.relative_begin ||
                    source_identity.relative_end > replacement.text.size()) {
                    report.blocking_errors.push_back(
                        "generated edit identity range is outside its source replacement");
                    continue;
                }
                resolved_identities.push_back({
                    source_identity.edit_id,
                    source_identity.row_kind,
                    patch.record->source_key,
                    replacement.begin + source_identity.relative_begin,
                    replacement.begin + source_identity.relative_end,
                    source_identity.element_index,
                    source_identity.baseline_global_order,
                });
            }
        }
        std::string bytes;
        try {
            bytes = encode_text_for_writeback(patched_text, patch.record->encoding, patch.utf8_bom);
        } catch (const std::exception& e) {
            report.blocking_errors.push_back(e.what());
            continue;
        }
        std::string current_hash = hex64(stable_hash64(bytes));
        report.changed_files.push_back(patch.record->file_path);
        report.patched_files.push_back({
            patch.record->file_path,
            patch.record->source_key,
            std::move(patched_text),
            bytes,
            patch.record->encoding,
            patch.record->newline,
            patch.base_hash.empty() ? patch.current_hash : patch.base_hash,
            current_hash,
            bytes.size(),
            patch.utf8_bom
        });
        report.identity_origins.insert(
            report.identity_origins.end(),
            std::make_move_iterator(resolved_identities.begin()),
            std::make_move_iterator(resolved_identities.end()));
    }

    if (!report.ok()) return report;
    std::vector<MapEditChange> validation_changes;
    validation_changes.reserve(effective_changes.size());
    for (const MapEditChange* change : effective_changes) validation_changes.push_back(*change);
    validate_edit_report(ctx, validation_changes, report);
    if (!report.ok()) return report;

    if (write_files) {
        if (std::any_of(ctx.source_overrides.begin(), ctx.source_overrides.end(),
                        [](const auto& entry) { return entry.second.dirty; })) {
            report.blocking_errors.push_back(
                "direct disk apply is blocked while working-copy edits are pending");
            return report;
        }
        if (!report.validated_context) {
            report.blocking_errors.push_back(
                "direct disk apply has no validated full-reparse context");
            return report;
        }
        std::vector<TransactionalWriteRequest> writes;
        writes.reserve(report.patched_files.size());
        for (const MapEditPatchedFile& file : report.patched_files) {
            writes.push_back({path_from_utf8(file.file_path), file.bytes,
                              file.base_hash, file.current_hash});
        }
        try {
            TransactionalWriteOutcome outcome =
                replace_files_transactionally(std::move(writes));
            report.warnings.insert(report.warnings.end(),
                                   outcome.warnings.begin(), outcome.warnings.end());
        } catch (const std::exception& e) {
            report.blocking_errors.push_back(e.what());
            return report;
        }

        const std::uint64_t next_content_revision = ctx.content_revision + 1;
        const std::uint64_t next_geometry_revision = ctx.geometry_revision + 1;
        const std::uint64_t next_scene_revision = ctx.scene_revision + 1;
        ctx = std::move(*report.validated_context);
        ctx.content_revision = next_content_revision;
        ctx.geometry_revision = next_geometry_revision;
        ctx.scene_revision = next_scene_revision;
        ctx.map_snapshot.reset();
        ctx.scene_snapshot.reset();
        ctx.edit_target_snapshot.reset();
        ctx.edit_report_snapshot.reset();
        ctx.disk_native_element_edit_id_to_stable =
            ctx.native_element_edit_id_to_stable;
        ctx.disk_source_hashes_for_stable_ids.clear();
        ctx.disk_source_hashes_for_stable_ids.reserve(ctx.source_files.size());
        for (const SourceFileRecord& file : ctx.source_files) {
            ctx.disk_source_hashes_for_stable_ids.emplace(
                file.source_key, file.source_hash);
        }
        ctx.source_overrides.clear();
        ctx.edit_validation_current = false;
        ctx.edit_validation_fingerprint.clear();
    }
    return report;
}


void apply_patched_files_to_overrides(SourceTextOverrides& overrides,
                                      const MapEditReport& report) {
    for (const MapEditPatchedFile& file : report.patched_files) {
        SourceTextOverride source;
        source.file_path = file.file_path;
        source.source_key = file.source_key;
        source.text = file.text;
        source.encoding = file.encoding;
        source.newline = file.newline;
        source.base_hash = file.base_hash;
        source.current_hash = file.current_hash;
        source.byte_length = file.byte_length;
        source.utf8_bom = file.utf8_bom;
        source.dirty = true;
        overrides[source.source_key] = std::move(source);
    }
}

void reparse_context_with_overrides(MapContext& ctx,
                                    SourceTextOverrides overrides,
                                    bool has_arbitrary_distribution,
                                    const std::array<double, 3>& arbitrary_distribution) {
    const std::uint64_t next_content_revision = ctx.content_revision + 1;
    const std::uint64_t next_geometry_revision = ctx.geometry_revision + 1;
    const std::uint64_t next_scene_revision = ctx.scene_revision + 1;
    const auto disk_identities = ctx.disk_native_element_edit_id_to_stable;
    const auto disk_source_hashes = ctx.disk_source_hashes_for_stable_ids;
    std::string entry_file_path = ctx.entry_file_path;
    if (entry_file_path.empty()) {
        if (!ctx.include_stack.empty()) entry_file_path = ctx.include_stack.front();
        else if (!ctx.source_files.empty()) entry_file_path = ctx.source_files.front().file_path;
    }
    if (entry_file_path.empty()) throw std::runtime_error("map entry file is not known");
    double unit_distance = ctx.unit_distance;
    MapParseOptions options = ctx.parse_options;
    options.collect_edit_metadata = true;
    auto next = parse_map_context(path_from_utf8(entry_file_path), unit_distance, std::move(overrides),
                                  has_arbitrary_distribution, arbitrary_distribution, options);
    if (!disk_identities.empty() && !disk_source_hashes.empty()) {
        bool disk_baseline_matches = next->source_files.size() == disk_source_hashes.size();
        for (const SourceFileRecord& file : next->source_files) {
            auto expected = disk_source_hashes.find(file.source_key);
            if (expected == disk_source_hashes.end() ||
                expected->second != file.source_hash) {
                disk_baseline_matches = false;
                break;
            }
        }
        if (!disk_baseline_matches) {
            throw std::runtime_error(
                "source files changed externally; full Reload is required before editing");
        }
    }
    next->native_element_edit_id_to_stable = disk_identities;
    next->disk_native_element_edit_id_to_stable = disk_identities;
    next->disk_source_hashes_for_stable_ids = disk_source_hashes;
    next->element_edit_id_cache.clear();
    ctx = std::move(*next);
    ctx.content_revision = next_content_revision;
    ctx.geometry_revision = next_geometry_revision;
    ctx.scene_revision = next_scene_revision;
    ctx.map_snapshot.reset();
    ctx.scene_snapshot.reset();
    ctx.edit_target_snapshot.reset();
    ctx.edit_report_snapshot.reset();
}

void apply_edit_report_to_memory(MapContext& ctx, const MapEditReport& report) {
    if (!report.ok() || !report.full_reparse_ok || !report.validated_context) {
        throw std::runtime_error("edit report has no validated full-reparse result");
    }
    const std::uint64_t next_content_revision = ctx.content_revision + 1;
    const std::uint64_t next_geometry_revision = ctx.geometry_revision + 1;
    const std::uint64_t next_scene_revision = ctx.scene_revision + 1;
    ctx = std::move(*report.validated_context);
    ctx.content_revision = next_content_revision;
    ctx.geometry_revision = next_geometry_revision;
    ctx.scene_revision = next_scene_revision;
    ctx.map_snapshot.reset();
    ctx.scene_snapshot.reset();
    ctx.edit_target_snapshot.reset();
    ctx.edit_report_snapshot.reset();
    ctx.edit_validation_fingerprint = report.validation_fingerprint;
    ctx.edit_validation_current = true;
}

void reset_memory_edits(MapContext& ctx) {
    if (ctx.source_overrides.empty()) {
        // The current context already represents the disk baseline. Re-parsing
        // here would regenerate global-order-based editIds after a committed
        // distance move and break the session-stable identities held by the GUI.
        return;
    }
    bool has_arbitrary_distribution = ctx.cp_arbdistribution_explicit;
    std::array<double, 3> arbitrary_distribution = ctx.cp_arbdistribution;
    reparse_context_with_overrides(ctx, SourceTextOverrides{},
                                   has_arbitrary_distribution,
                                   arbitrary_distribution);
}

void append_committed_row(MapContext& ctx, MapEditReport& report,
                          const std::string& row_kind, size_t row_index,
                          const EditSourceRef& ref) {
    MapEditCommittedRow row;
    row.row_kind = row_kind;
    row.row_index = row_index;
    if (ref.valid() && ref.statement_index < ctx.parsed_statements.size()) {
        const ParsedStatement& statement = ctx.parsed_statements[ref.statement_index];
        row.edit_id = element_edit_id(ctx, ref, row_kind);
        row.file_path = source_file_path(ctx, statement.source);
        row.line = statement.source.line;
        row.column = statement.source.column;
        row.raw_text_preview = statement.raw_text_preview;
    }
    report.committed_rows.push_back(std::move(row));
}

template <typename Rows>
void append_committed_rows(MapContext& ctx, MapEditReport& report,
                           const std::string& row_kind, const Rows& rows) {
    for (size_t i = 0; i < rows.size(); ++i) {
        append_committed_row(ctx, report, row_kind, i, rows[i].edit_ref);
    }
}

void populate_committed_edit_state(MapContext& ctx, MapEditReport& report) {
    report.committed_files.reserve(ctx.source_files.size());
    for (const SourceFileRecord& file : ctx.source_files) {
        report.committed_files.push_back({file.file_path, file.source_hash, file.byte_length});
    }

    append_committed_rows(ctx, report, "structure.model", ctx.structure_models);
    append_committed_rows(ctx, report, "structure.put", ctx.structure_puts);
    append_committed_rows(ctx, report, "structure.between", ctx.structure_betweens);

    std::vector<size_t> station_order;
    station_order.reserve(ctx.station_puts.size());
    for (size_t i = 0; i < ctx.station_puts.size(); ++i) station_order.push_back(i);
    std::stable_sort(station_order.begin(), station_order.end(), [&](size_t lhs, size_t rhs) {
        const StationPut& a = ctx.station_puts[lhs];
        const StationPut& b = ctx.station_puts[rhs];
        if (a.distance != b.distance) return a.distance < b.distance;
        return a.order < b.order;
    });
    for (size_t row_index = 0; row_index < station_order.size(); ++row_index) {
        append_committed_row(ctx, report, "station.put", row_index,
                             ctx.station_puts[station_order[row_index]].edit_ref);
    }
}

MapEditReport commit_memory_edits(MapContext& ctx) {
    MapEditReport report;
    struct PendingWrite {
        std::string source_key;
        std::filesystem::path path;
        std::string bytes;
        std::string expected_hash;
        std::string hash;
    };
    std::vector<PendingWrite> writes;

    bool has_dirty_override = false;
    for (const auto& entry : ctx.source_overrides) {
        if (entry.second.dirty) {
            has_dirty_override = true;
            break;
        }
    }
    if (has_dirty_override && !ctx.edit_validation_current) {
        report.blocking_errors.push_back(
            "working-copy edits have not passed full temporary-result validation");
        return report;
    }
    if (has_dirty_override) {
        try {
            MapEditReport empty_report;
            std::unique_ptr<MapContext> verified = parse_report_candidate(ctx, empty_report);
            const std::string fingerprint =
                semantic_snapshot_for_context(*verified).full_fingerprint;
            report.validation_fingerprint = fingerprint;
            report.full_reparse_ok = true;
            if (fingerprint != ctx.edit_validation_fingerprint) {
                report.blocking_errors.push_back(
                    "working-copy semantics changed after the last validated Apply");
                return report;
            }
        } catch (const std::exception& e) {
            report.blocking_errors.push_back(
                std::string("pre-save full edited-map reparse failed: ") + e.what());
            return report;
        }
    }

    for (const auto& entry : ctx.source_overrides) {
        const SourceTextOverride& source = entry.second;
        if (!source.dirty) continue;
        std::filesystem::path path = path_from_utf8(source.file_path);
        std::string disk_bytes;
        try {
            disk_bytes = read_binary_file(path);
        } catch (const std::exception& e) {
            report.blocking_errors.push_back(e.what());
            continue;
        }
        std::string disk_hash = hex64(stable_hash64(disk_bytes));
        if (!source.base_hash.empty() && disk_hash != source.base_hash) {
            report.blocking_errors.push_back("source file changed externally: " + source.file_path);
            continue;
        }

        try {
            std::string bytes = encode_text_for_writeback(source.text, source.encoding, source.utf8_bom);
            std::string hash = hex64(stable_hash64(bytes));
            writes.push_back({source.source_key, std::move(path), std::move(bytes),
                              source.base_hash, std::move(hash)});
            report.changed_files.push_back(source.file_path);
        } catch (const std::exception& e) {
            report.blocking_errors.push_back(e.what());
        }
    }

    if (!report.ok()) return report;

    std::vector<TransactionalWriteRequest> transaction_writes;
    transaction_writes.reserve(writes.size());
    for (const PendingWrite& write : writes) {
        transaction_writes.push_back({write.path, write.bytes,
                                      write.expected_hash, write.hash});
    }
    try {
        TransactionalWriteOutcome outcome =
            replace_files_transactionally(std::move(transaction_writes));
        report.warnings.insert(report.warnings.end(),
                               outcome.warnings.begin(), outcome.warnings.end());
    } catch (const TransactionalWriteError& e) {
        if (!e.rollback_complete()) {
            ctx.edit_validation_current = false;
            ctx.edit_validation_fingerprint.clear();
        }
        report.blocking_errors.push_back(e.what());
        return report;
    } catch (const std::exception& e) {
        report.blocking_errors.push_back(e.what());
        return report;
    }
    for (const PendingWrite& write : writes) {
        for (SourceFileRecord& source_file : ctx.source_files) {
            if (source_file.source_key == write.source_key) {
                source_file.source_hash = write.hash;
                source_file.byte_length = write.bytes.size();
                break;
            }
        }
        ctx.source_overrides.erase(write.source_key);
    }
    ctx.edit_validation_current = false;
    ctx.edit_validation_fingerprint.clear();
    if (!writes.empty()) {
        ctx.disk_native_element_edit_id_to_stable =
            ctx.native_element_edit_id_to_stable;
        ctx.disk_source_hashes_for_stable_ids.clear();
        ctx.disk_source_hashes_for_stable_ids.reserve(ctx.source_files.size());
        for (const SourceFileRecord& file : ctx.source_files) {
            ctx.disk_source_hashes_for_stable_ids.emplace(
                file.source_key, file.source_hash);
        }
        populate_committed_edit_state(ctx, report);
        invalidate_map_snapshot(ctx, true, false);
        invalidate_scene_geometry_snapshot(ctx, false);
    }
    return report;
}


} // namespace kme::maploader::detail
