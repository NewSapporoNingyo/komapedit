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

namespace edit_json {

struct Value {
    enum class Type { Null, Bool, Number, String, Array, Object };
    Type type = Type::Null;
    bool boolean = false;
    double number = 0.0;
    std::string string;
    std::vector<Value> array;
    std::map<std::string, Value> object;

    bool is_object() const { return type == Type::Object; }
    bool is_array() const { return type == Type::Array; }
    bool is_string() const { return type == Type::String; }
    bool is_number() const { return type == Type::Number; }
    bool is_bool() const { return type == Type::Bool; }

    const Value& at(const std::string& key) const {
        static const Value empty;
        auto it = object.find(key);
        return it == object.end() ? empty : it->second;
    }

    std::string scalar_text() const {
        if (is_string()) return string;
        if (is_number()) return json_number(number);
        if (is_bool()) return boolean ? "true" : "false";
        return {};
    }
};

class Parser {
public:
    explicit Parser(const std::string& source) : source_(source) {}

    Value parse() {
        Value value = parse_value();
        skip_ws();
        if (pos_ != source_.size()) throw std::runtime_error("trailing JSON text");
        return value;
    }

private:
    const std::string& source_;
    size_t pos_ = 0;

    void skip_ws() {
        while (pos_ < source_.size() &&
               std::isspace(static_cast<unsigned char>(source_[pos_]))) {
            ++pos_;
        }
    }

    char peek() const {
        return pos_ < source_.size() ? source_[pos_] : '\0';
    }

    char get() {
        if (pos_ >= source_.size()) throw std::runtime_error("unexpected JSON EOF");
        return source_[pos_++];
    }

    void expect(char expected) {
        if (get() != expected) throw std::runtime_error("unexpected JSON token");
    }

    Value parse_value() {
        skip_ws();
        char ch = peek();
        if (ch == '{') return parse_object();
        if (ch == '[') return parse_array();
        if (ch == '"') {
            Value value;
            value.type = Value::Type::String;
            value.string = parse_string();
            return value;
        }
        if (ch == '-' || std::isdigit(static_cast<unsigned char>(ch))) return parse_number();
        if (source_.compare(pos_, 4, "true") == 0) {
            pos_ += 4;
            Value value;
            value.type = Value::Type::Bool;
            value.boolean = true;
            return value;
        }
        if (source_.compare(pos_, 5, "false") == 0) {
            pos_ += 5;
            Value value;
            value.type = Value::Type::Bool;
            value.boolean = false;
            return value;
        }
        if (source_.compare(pos_, 4, "null") == 0) {
            pos_ += 4;
            return {};
        }
        throw std::runtime_error("invalid JSON value");
    }

    Value parse_object() {
        Value value;
        value.type = Value::Type::Object;
        expect('{');
        skip_ws();
        if (peek() == '}') {
            ++pos_;
            return value;
        }
        while (true) {
            skip_ws();
            std::string key = parse_string();
            skip_ws();
            expect(':');
            value.object[std::move(key)] = parse_value();
            skip_ws();
            char ch = get();
            if (ch == '}') break;
            if (ch != ',') throw std::runtime_error("expected JSON object comma");
        }
        return value;
    }

    Value parse_array() {
        Value value;
        value.type = Value::Type::Array;
        expect('[');
        skip_ws();
        if (peek() == ']') {
            ++pos_;
            return value;
        }
        while (true) {
            value.array.push_back(parse_value());
            skip_ws();
            char ch = get();
            if (ch == ']') break;
            if (ch != ',') throw std::runtime_error("expected JSON array comma");
        }
        return value;
    }

    std::string parse_string() {
        expect('"');
        std::string out;
        while (true) {
            char ch = get();
            if (ch == '"') break;
            if (ch != '\\') {
                out.push_back(ch);
                continue;
            }
            char escaped = get();
            switch (escaped) {
                case '"': out.push_back('"'); break;
                case '\\': out.push_back('\\'); break;
                case '/': out.push_back('/'); break;
                case 'b': out.push_back('\b'); break;
                case 'f': out.push_back('\f'); break;
                case 'n': out.push_back('\n'); break;
                case 'r': out.push_back('\r'); break;
                case 't': out.push_back('\t'); break;
                case 'u':
                    throw std::runtime_error("JSON unicode escapes are not supported in edit changes");
                default:
                    throw std::runtime_error("invalid JSON string escape");
            }
        }
        return out;
    }

    Value parse_number() {
        size_t start = pos_;
        if (peek() == '-') ++pos_;
        while (std::isdigit(static_cast<unsigned char>(peek()))) ++pos_;
        if (peek() == '.') {
            ++pos_;
            while (std::isdigit(static_cast<unsigned char>(peek()))) ++pos_;
        }
        if (peek() == 'e' || peek() == 'E') {
            ++pos_;
            if (peek() == '+' || peek() == '-') ++pos_;
            while (std::isdigit(static_cast<unsigned char>(peek()))) ++pos_;
        }
        Value value;
        value.type = Value::Type::Number;
        value.number = std::stod(source_.substr(start, pos_ - start));
        return value;
    }
};

} // namespace edit_json

struct EditableTarget {
    size_t statement_index = kNoSourceRef;
    std::string row_kind;
    size_t row_index = 0;
    const StructureModel* structure_model = nullptr;
    const StructurePut* structure_put = nullptr;
    const StationPut* station_put = nullptr;
    int elements_for_statement = 0;
};

struct TextReplacement {
    size_t begin = 0;
    size_t end = 0;
    std::string text;
    std::string edit_id;
};

struct SourcePatch {
    const SourceFileRecord* record = nullptr;
    std::string text;
    std::string current_hash;
    std::string base_hash;
    bool utf8_bom = false;
    std::vector<TextReplacement> replacements;
};

std::vector<MapEditChange> parse_edit_changes_json(const char* changes_json) {
    if (!changes_json) throw std::runtime_error("changes_json is null");
    edit_json::Value root = edit_json::Parser(changes_json).parse();
    const edit_json::Value& changes_value = root.is_array() ? root : root.at("changes");
    if (!changes_value.is_array()) throw std::runtime_error("edit changes JSON must contain a changes array");

    std::vector<MapEditChange> changes;
    changes.reserve(changes_value.array.size());
    for (const edit_json::Value& item : changes_value.array) {
        if (!item.is_object()) continue;
        MapEditChange change;
        change.change_id = item.at("changeId").scalar_text();
        change.edit_id = item.at("editId").scalar_text();
        change.operation = item.at("operation").scalar_text();
        change.replacement_statement = item.at("replacementStatement").scalar_text();
        change.target_file_path = item.at("targetFilePath").scalar_text();
        change.insert_before_edit_id = item.at("insertBeforeEditId").scalar_text();
        change.expected_source_hash = item.at("expectedSourceHash").scalar_text();
        const edit_json::Value& fields = item.at("fieldChanges");
        if (fields.is_object()) {
            for (const auto& kv : fields.object) {
                change.field_changes[kv.first] = kv.second.scalar_text();
            }
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
        case ValueKind::Number: return json_number(value.number);
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
    std::ostringstream out;
    out << "Structure[" << quoted_bve_string(structure_key) << "]." << row.method << "(";
    if (between) {
        out << value_field_as_bve_arg(change, "trackKey1", row.track_key1, raw_arg_at(raw_args, 0)) << ","
            << value_field_as_bve_arg(change, "trackKey2", row.track_key2, raw_arg_at(raw_args, 1)) << ","
            << numeric_field(change, "flag", row.flag, raw_arg_at(raw_args, 2));
    } else if (ascii_lower(row.method) == "put0") {
        out << value_field_as_bve_arg(change, "trackKey", row.track_key, raw_arg_at(raw_args, 0)) << ","
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

bool source_context_equal(const SourceSpan& a, const SourceSpan& b) {
    return a.source_file_index == b.source_file_index &&
           a.include_stack_index == b.include_stack_index;
}

bool source_start_less(const SourceSpan& a, const SourceSpan& b) {
    if (a.line != b.line) return a.line < b.line;
    if (a.column != b.column) return a.column < b.column;
    if (a.line_end != b.line_end) return a.line_end < b.line_end;
    return a.column_end < b.column_end;
}

bool source_start_greater(const SourceSpan& a, const SourceSpan& b) {
    return source_start_less(b, a);
}

bool is_distance_statement(const ParsedStatement& statement) {
    return statement.statement_kind == "Distance.Set";
}

bool distance_value_matches(double a, double b) {
    const double scale = std::max({1.0, std::fabs(a), std::fabs(b)});
    return std::fabs(a - b) <= 1e-8 * scale;
}

bool distance_statement_matches(const ParsedStatement& statement,
                                const std::string& expression,
                                double value) {
    return is_distance_statement(statement) &&
           trim_field_copy(statement.distance_expression) == expression &&
           distance_value_matches(statement.distance_value, value);
}

size_t nearest_source_statement_before(const MapContext& ctx, size_t statement_index) {
    if (statement_index >= ctx.parsed_statements.size()) return kNoSourceRef;
    const ParsedStatement& target = ctx.parsed_statements[statement_index];
    size_t best = kNoSourceRef;
    for (size_t i = 0; i < ctx.parsed_statements.size(); ++i) {
        if (i == statement_index) continue;
        const ParsedStatement& candidate = ctx.parsed_statements[i];
        if (!source_context_equal(candidate.source, target.source)) continue;
        if (!source_start_less(candidate.source, target.source)) continue;
        if (best == kNoSourceRef ||
            source_start_less(ctx.parsed_statements[best].source, candidate.source)) {
            best = i;
        }
    }
    return best;
}

size_t nearest_source_statement_after(const MapContext& ctx, size_t statement_index) {
    if (statement_index >= ctx.parsed_statements.size()) return kNoSourceRef;
    const ParsedStatement& target = ctx.parsed_statements[statement_index];
    size_t best = kNoSourceRef;
    for (size_t i = 0; i < ctx.parsed_statements.size(); ++i) {
        if (i == statement_index) continue;
        const ParsedStatement& candidate = ctx.parsed_statements[i];
        if (!source_context_equal(candidate.source, target.source)) continue;
        if (!source_start_greater(candidate.source, target.source)) continue;
        if (best == kNoSourceRef ||
            source_start_less(candidate.source, ctx.parsed_statements[best].source)) {
            best = i;
        }
    }
    return best;
}

size_t nearest_distance_statement_before(const MapContext& ctx, size_t statement_index) {
    if (statement_index >= ctx.parsed_statements.size()) return kNoSourceRef;
    const ParsedStatement& target = ctx.parsed_statements[statement_index];
    size_t best = kNoSourceRef;
    for (size_t i = 0; i < ctx.parsed_statements.size(); ++i) {
        if (i == statement_index) continue;
        const ParsedStatement& candidate = ctx.parsed_statements[i];
        if (!is_distance_statement(candidate)) continue;
        if (!source_context_equal(candidate.source, target.source)) continue;
        if (!source_start_less(candidate.source, target.source)) continue;
        if (best == kNoSourceRef ||
            source_start_less(ctx.parsed_statements[best].source, candidate.source)) {
            best = i;
        }
    }
    return best;
}

size_t nearest_distance_statement_after(const MapContext& ctx, size_t statement_index) {
    if (statement_index >= ctx.parsed_statements.size()) return kNoSourceRef;
    const ParsedStatement& target = ctx.parsed_statements[statement_index];
    size_t best = kNoSourceRef;
    for (size_t i = 0; i < ctx.parsed_statements.size(); ++i) {
        if (i == statement_index) continue;
        const ParsedStatement& candidate = ctx.parsed_statements[i];
        if (!is_distance_statement(candidate)) continue;
        if (!source_context_equal(candidate.source, target.source)) continue;
        if (!source_start_greater(candidate.source, target.source)) continue;
        if (best == kNoSourceRef ||
            source_start_less(candidate.source, ctx.parsed_statements[best].source)) {
            best = i;
        }
    }
    return best;
}

struct AdjacentDistanceWrapper {
    size_t before_index = kNoSourceRef;
    size_t after_index = kNoSourceRef;

    bool valid() const {
        return before_index != kNoSourceRef && after_index != kNoSourceRef;
    }
};

AdjacentDistanceWrapper find_isolated_adjacent_distance_wrapper(const MapContext& ctx,
                                                               size_t statement_index) {
    AdjacentDistanceWrapper wrapper;
    if (statement_index >= ctx.parsed_statements.size()) return wrapper;

    size_t before = nearest_source_statement_before(ctx, statement_index);
    size_t after = nearest_source_statement_after(ctx, statement_index);
    if (before == kNoSourceRef || after == kNoSourceRef) return wrapper;
    if (!is_distance_statement(ctx.parsed_statements[before]) ||
        !is_distance_statement(ctx.parsed_statements[after])) {
        return wrapper;
    }
    if (nearest_source_statement_after(ctx, before) != statement_index ||
        nearest_source_statement_before(ctx, after) != statement_index) {
        return wrapper;
    }
    if (count_elements_for_statement(ctx, before) != 0 ||
        count_elements_for_statement(ctx, after) != 0) {
        return wrapper;
    }
    size_t previous_distance = nearest_distance_statement_before(ctx, before);
    if (previous_distance == kNoSourceRef) return wrapper;
    const ParsedStatement& previous = ctx.parsed_statements[previous_distance];
    const ParsedStatement& restore = ctx.parsed_statements[after];
    if (trim_field_copy(previous.distance_expression) != trim_field_copy(restore.distance_expression) ||
        !distance_value_matches(previous.distance_value, restore.distance_value)) {
        return wrapper;
    }

    wrapper.before_index = before;
    wrapper.after_index = after;
    return wrapper;
}

bool can_remove_restore_distance_statement(const MapContext& ctx,
                                           const AdjacentDistanceWrapper& wrapper) {
    if (!wrapper.valid()) return false;
    size_t previous_distance = nearest_distance_statement_before(ctx, wrapper.before_index);
    if (previous_distance == kNoSourceRef) return false;
    const ParsedStatement& previous = ctx.parsed_statements[previous_distance];
    const ParsedStatement& restore = ctx.parsed_statements[wrapper.after_index];
    return trim_field_copy(previous.distance_expression) == trim_field_copy(restore.distance_expression) &&
           distance_value_matches(previous.distance_value, restore.distance_value);
}

size_t find_matching_distance_anchor(const MapContext& ctx,
                                     const ParsedStatement& target,
                                     const std::string& expression,
                                     double value,
                                     const std::set<size_t>& excluded) {
    size_t best = kNoSourceRef;
    for (size_t i = 0; i < ctx.parsed_statements.size(); ++i) {
        if (excluded.find(i) != excluded.end()) continue;
        const ParsedStatement& candidate = ctx.parsed_statements[i];
        if (!source_context_equal(candidate.source, target.source)) continue;
        if (!distance_statement_matches(candidate, expression, value)) continue;
        if (best == kNoSourceRef ||
            source_start_less(candidate.source, ctx.parsed_statements[best].source)) {
            best = i;
        }
    }
    return best;
}

size_t distance_anchor_insert_offset(const MapContext& ctx,
                                     const SourcePatch& patch,
                                     size_t anchor_index) {
    if (anchor_index >= ctx.parsed_statements.size()) return std::string::npos;
    size_t next_distance = nearest_distance_statement_after(ctx, anchor_index);
    if (next_distance != kNoSourceRef) {
        return source_range_in_text(patch, ctx.parsed_statements[next_distance].source).first;
    }
    return patch.text.size();
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
    target.elements_for_statement = count_elements_for_statement(ctx, target.statement_index);
    return true;
}

EditableTarget find_editable_target(MapContext& ctx, const std::string& edit_id) {
    EditableTarget target;
    for (size_t i = 0; i < ctx.structure_models.size(); ++i) {
        const StructureModel& row = ctx.structure_models[i];
        if (match_edit_ref(ctx, row, "structure.model", i, edit_id, target)) {
            target.structure_model = &row;
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

std::string edit_target_info_json(MapContext& ctx, const std::string& edit_id) {
    auto error_json = [](const std::string& error) {
        std::ostringstream out;
        out << "{\"ok\":false,\"error\":";
        append_json_string(out, error);
        out << "}";
        return out.str();
    };

    if (edit_id.empty()) return error_json("editId is empty");

    EditableTarget target = find_editable_target(ctx, edit_id);
    if (target.statement_index == kNoSourceRef ||
        target.statement_index >= ctx.parsed_statements.size()) {
        return error_json("unsupported or unknown editId: " + edit_id);
    }

    const ParsedStatement& statement = ctx.parsed_statements[target.statement_index];
    const SourceFileRecord* file = nullptr;
    if (statement.source.source_file_index < ctx.source_files.size()) {
        file = &ctx.source_files[statement.source.source_file_index];
    }
    std::string expected_source_hash;
    if (file) {
        expected_source_hash = file->source_hash;
        auto override_it = ctx.source_overrides.find(file->source_key);
        if (override_it != ctx.source_overrides.end() &&
            !override_it->second.base_hash.empty()) {
            expected_source_hash = override_it->second.base_hash;
        }
    }

    std::ostringstream out;
    out << "{\"ok\":true,\"editId\":";
    append_json_string(out, edit_id);
    out << ",\"rowKind\":";
    append_json_string(out, target.row_kind);
    out << ",\"rowIndex\":" << target.row_index
        << ",\"elementsForStatement\":" << target.elements_for_statement
        << ",\"statementKind\":";
    append_json_string(out, statement.statement_kind);
    out << ",\"sourceHash\":";
    append_json_string(out, file ? file->source_hash : std::string{});
    out << ",\"expectedSourceHash\":";
    append_json_string(out, expected_source_hash);
    out << ",\"source\":{\"filePath\":";
    append_json_string(out, source_file_path(ctx, statement.source));
    out << ",\"line\":" << statement.source.line
        << ",\"column\":" << statement.source.column
        << ",\"rawTextPreview\":";
    append_json_string(out, statement.raw_text_preview);
    out << "},\"rawText\":";
    append_json_string(out, statement.raw_text);
    out << ",\"rawArguments\":";
    append_json_string(out, statement.raw_arguments);
    out << ",\"distanceExpression\":";
    append_json_string(out, statement.distance_expression);
    out << ",\"distanceValue\":" << json_number(statement.distance_value) << "}";
    return out.str();
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

std::string wrap_distance_edit_if_needed(const MapEditChange& change,
                                         const ParsedStatement& statement,
                                         const SourceFileRecord& file,
                                         std::string replacement,
                                         MapEditReport& report) {
    if (!has_field_change(change, "distance")) return replacement;
    const std::string new_distance_text =
        normalized_number_arg(field_text_or(change, "distance", fallback_edit_number(statement.distance_value)));
    double new_distance_value = 0.0;
    if (!parse_edit_number(new_distance_text, new_distance_value)) {
        throw std::runtime_error("invalid numeric edit value: " + new_distance_text);
    }
    const double delta = new_distance_value - statement.distance_value;
    if (delta == 0.0) return replacement;
    std::string old_distance = trim_field_copy(statement.distance_expression);
    if (old_distance.empty()) old_distance = fallback_edit_number(statement.distance_value);
    std::string adjusted_distance = adjust_distance_expression_by_delta(old_distance, delta);
    std::string nl = newline_text(file.newline);
    ++report.insert_count;
    report.warnings.push_back("distance edit preserves the original distance expression by applying a delta around the target statement");
    return adjusted_distance + ";" + nl + replacement + nl + old_distance + ";";
}

bool append_distance_anchor_move_replacements(MapContext& ctx,
                                              const MapEditChange& change,
                                              size_t statement_index,
                                              const ParsedStatement& statement,
                                              const SourceFileRecord& file,
                                              const SourcePatch& patch,
                                              const std::pair<size_t, size_t>& statement_range,
                                              const std::string& replacement_statement,
                                              MapEditReport& report,
                                              std::vector<TextReplacement>& replacements) {
    if (!has_field_change(change, "distance")) return false;

    const std::string new_distance_text =
        normalized_number_arg(field_text_or(change, "distance", fallback_edit_number(statement.distance_value)));
    double new_distance_value = 0.0;
    if (!parse_edit_number(new_distance_text, new_distance_value)) {
        throw std::runtime_error("invalid numeric edit value: " + new_distance_text);
    }
    const double delta = new_distance_value - statement.distance_value;
    if (delta == 0.0) return false;

    std::string old_distance = trim_field_copy(statement.distance_expression);
    if (old_distance.empty()) old_distance = fallback_edit_number(statement.distance_value);
    const std::string adjusted_distance = adjust_distance_expression_by_delta(old_distance, delta);

    AdjacentDistanceWrapper wrapper = find_isolated_adjacent_distance_wrapper(ctx, statement_index);
    std::set<size_t> excluded_anchors;
    if (wrapper.before_index != kNoSourceRef) excluded_anchors.insert(wrapper.before_index);
    if (wrapper.after_index != kNoSourceRef) excluded_anchors.insert(wrapper.after_index);

    size_t anchor_index = find_matching_distance_anchor(ctx, statement, adjusted_distance,
                                                        new_distance_value, excluded_anchors);
    bool anchor_is_restore_wrapper = false;
    if (anchor_index == kNoSourceRef && wrapper.after_index != kNoSourceRef) {
        const ParsedStatement& restore = ctx.parsed_statements[wrapper.after_index];
        if (distance_statement_matches(restore, adjusted_distance, new_distance_value)) {
            anchor_index = wrapper.after_index;
            anchor_is_restore_wrapper = true;
        }
    }
    if (anchor_index == kNoSourceRef) return false;

    const size_t insert_offset = distance_anchor_insert_offset(ctx, patch, anchor_index);
    if (insert_offset == std::string::npos) return false;

    TextReplacement insert;
    insert.begin = insert_offset;
    insert.end = insert_offset;
    insert.text = statement_insertion_text(patch.text, insert_offset, replacement_statement, file);
    insert.edit_id = change.edit_id;
    replacements.push_back(std::move(insert));

    TextReplacement remove_target;
    remove_target.begin = statement_range.first;
    remove_target.end = statement_range.second;
    remove_target.edit_id = change.edit_id;
    replacements.push_back(std::move(remove_target));

    if (wrapper.before_index != kNoSourceRef) {
        auto range = source_range_in_text(patch, ctx.parsed_statements[wrapper.before_index].source);
        TextReplacement remove_wrapper_begin;
        remove_wrapper_begin.begin = range.first;
        remove_wrapper_begin.end = range.second;
        remove_wrapper_begin.edit_id = change.edit_id;
        replacements.push_back(std::move(remove_wrapper_begin));
    }
    if (wrapper.after_index != kNoSourceRef && !anchor_is_restore_wrapper &&
        can_remove_restore_distance_statement(ctx, wrapper)) {
        auto range = source_range_in_text(patch, ctx.parsed_statements[wrapper.after_index].source);
        TextReplacement remove_wrapper_end;
        remove_wrapper_end.begin = range.first;
        remove_wrapper_end.end = range.second;
        remove_wrapper_end.edit_id = change.edit_id;
        replacements.push_back(std::move(remove_wrapper_end));
    }

    report.warnings.push_back("distance edit reuses an existing matching distance expression instead of inserting a new distance statement");
    return true;
}

std::string report_json(const MapEditReport& report) {
    std::ostringstream out;
    out << "{\"ok\":" << (report.ok() ? "true" : "false")
        << ",\"updateCount\":" << report.update_count
        << ",\"insertCount\":" << report.insert_count
        << ",\"deleteCount\":" << report.delete_count
        << ",\"changedFiles\":[";
    for (size_t i = 0; i < report.changed_files.size(); ++i) {
        if (i) out << ",";
        append_json_string(out, report.changed_files[i]);
    }
    out << "],\"committedFiles\":[";
    for (size_t i = 0; i < report.committed_files.size(); ++i) {
        if (i) out << ",";
        const MapEditCommittedFile& file = report.committed_files[i];
        out << "{\"filePath\":";
        append_json_string(out, file.file_path);
        out << ",\"sourceHash\":";
        append_json_string(out, file.source_hash);
        out << ",\"byteLength\":" << file.byte_length << "}";
    }
    out << "],\"committedRows\":[";
    for (size_t i = 0; i < report.committed_rows.size(); ++i) {
        if (i) out << ",";
        const MapEditCommittedRow& row = report.committed_rows[i];
        out << "{\"rowKind\":";
        append_json_string(out, row.row_kind);
        out << ",\"rowIndex\":" << row.row_index
            << ",\"editId\":";
        append_json_string(out, row.edit_id);
        out << ",\"source\":{\"filePath\":";
        append_json_string(out, row.file_path);
        out << ",\"line\":" << row.line
            << ",\"column\":" << row.column
            << ",\"rawTextPreview\":";
        append_json_string(out, row.raw_text_preview);
        out << "}}";
    }
    out << "],\"warnings\":[";
    for (size_t i = 0; i < report.warnings.size(); ++i) {
        if (i) out << ",";
        append_json_string(out, report.warnings[i]);
    }
    out << "],\"blockingErrors\":[";
    for (size_t i = 0; i < report.blocking_errors.size(); ++i) {
        if (i) out << ",";
        append_json_string(out, report.blocking_errors[i]);
    }
    out << "],\"previewSnippets\":[";
    for (size_t i = 0; i < report.previews.size(); ++i) {
        if (i) out << ",";
        out << "{\"filePath\":";
        append_json_string(out, report.previews[i].file_path);
        out << ",\"before\":";
        append_json_string(out, report.previews[i].before);
        out << ",\"after\":";
        append_json_string(out, report.previews[i].after);
        out << "}";
    }
    out << "]}";
    return out.str();
}

void write_binary_file_for_edit(const std::filesystem::path& path, const std::string& bytes) {
#if defined(_WIN32)
    FILE* output = _wfopen(path.wstring().c_str(), L"wb");
    if (!output) throw std::runtime_error("File open error: " + path_to_utf8(path));
    if (!bytes.empty() && std::fwrite(bytes.data(), 1, bytes.size(), output) != bytes.size()) {
        std::fclose(output);
        throw std::runtime_error("File write error: " + path_to_utf8(path));
    }
    std::fclose(output);
#else
    std::ofstream output(path, std::ios::binary);
    if (!output) throw std::runtime_error("File open error: " + path_to_utf8(path));
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    if (!output) throw std::runtime_error("File write error: " + path_to_utf8(path));
#endif
}

MapEditReport build_edit_report(MapContext& ctx,
                                const std::vector<MapEditChange>& changes,
                                bool write_files) {
    MapEditReport report;
    std::map<size_t, SourcePatch> patches;

    for (const MapEditChange& change : changes) {
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
        if (!expected_hash.empty() && patch.current_hash != expected_hash) {
            report.blocking_errors.push_back("source file changed externally: " + file.file_path);
            continue;
        }

        TextReplacement replacement;
        replacement.edit_id = change.edit_id;
        try {
            auto range = source_range_in_text(patch, statement.source);
            replacement.begin = range.first;
            replacement.end = range.second;
            if (operation == "delete") {
                if (target.elements_for_statement != 1) {
                    report.blocking_errors.push_back("delete is blocked because the source statement maps to multiple elements: " + change.edit_id);
                    continue;
                }
                replacement.text.clear();
                ++report.delete_count;
            } else if (operation == "update") {
                if (target.elements_for_statement != 1) {
                    report.blocking_errors.push_back("update is blocked because the source statement maps to multiple elements: " + change.edit_id);
                    continue;
                }
                std::string replacement_statement = build_replacement_statement(change, statement, target);
                if (append_distance_anchor_move_replacements(ctx, change, target.statement_index,
                                                             statement, file, patch, range,
                                                             replacement_statement, report,
                                                             patch.replacements)) {
                    ++report.update_count;
                    continue;
                }
                replacement.text = wrap_distance_edit_if_needed(change, statement, file,
                                                                std::move(replacement_statement),
                                                                report);
                ++report.update_count;
            } else if (operation == "insert") {
                report.blocking_errors.push_back("insert edits are not implemented for this target yet: " + change.edit_id);
                continue;
            } else {
                report.blocking_errors.push_back("unknown edit operation: " + change.operation);
                continue;
            }
            patch.replacements.push_back(std::move(replacement));
        } catch (const std::exception& e) {
            report.blocking_errors.push_back(std::string("edit change failed for ") + change.edit_id + ": " + e.what());
        }
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
        for (const TextReplacement& replacement : patch.replacements) {
            report.previews.push_back({
                patch.record->file_path,
                preview_fragment(patched_text, replacement.begin, replacement.end),
                preview_fragment(replacement.text, 0, replacement.text.size())
            });
            patched_text.replace(replacement.begin,
                                 replacement.end - replacement.begin,
                                 replacement.text);
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
        if (write_files) {
            write_binary_file_for_edit(path_from_utf8(patch.record->file_path), bytes);
        }
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
    std::string entry_file_path = ctx.entry_file_path;
    if (entry_file_path.empty()) {
        if (!ctx.include_stack.empty()) entry_file_path = ctx.include_stack.front();
        else if (!ctx.source_files.empty()) entry_file_path = ctx.source_files.front().file_path;
    }
    if (entry_file_path.empty()) throw std::runtime_error("map entry file is not known");
    double unit_distance = ctx.unit_distance;
    auto next = parse_map_context(path_from_utf8(entry_file_path), unit_distance, std::move(overrides),
                                  has_arbitrary_distribution, arbitrary_distribution);
    ctx = std::move(*next);
}

void apply_edit_report_to_memory(MapContext& ctx, const MapEditReport& report) {
    SourceTextOverrides overrides = ctx.source_overrides;
    apply_patched_files_to_overrides(overrides, report);
    bool has_arbitrary_distribution = ctx.has_cp_arbdistribution;
    std::array<double, 3> arbitrary_distribution = ctx.cp_arbdistribution;
    reparse_context_with_overrides(ctx, std::move(overrides),
                                   has_arbitrary_distribution,
                                   arbitrary_distribution);
}

void reset_memory_edits(MapContext& ctx) {
    bool has_arbitrary_distribution = ctx.has_cp_arbdistribution;
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
        std::string hash;
    };
    std::vector<PendingWrite> writes;

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
            writes.push_back({source.source_key, std::move(path), std::move(bytes), std::move(hash)});
            report.changed_files.push_back(source.file_path);
        } catch (const std::exception& e) {
            report.blocking_errors.push_back(e.what());
        }
    }

    if (!report.ok()) return report;

    for (const PendingWrite& write : writes) {
        write_binary_file_for_edit(write.path, write.bytes);
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
    if (!writes.empty()) populate_committed_edit_state(ctx, report);
    return report;
}


} // namespace kme::maploader::detail
