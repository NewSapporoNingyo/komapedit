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

using kme::maploader::decode_codepage;
using kme::maploader::decode_utf16;
using kme::maploader::encode_text_for_writeback;
using kme::maploader::first_line_ascii;
using kme::maploader::has_utf8_bom;
using kme::maploader::log_info;
using kme::maploader::path_from_utf8;
using kme::maploader::path_to_utf8;
using kme::maploader::read_binary_file;
using kme::maploader::log_warn;

double elapsed_seconds_since(SteadyClock::time_point started_at) {
    return std::chrono::duration<double>(SteadyClock::now() - started_at).count();
}

thread_local LoadTiming* g_active_timing = nullptr;


std::string ascii_lower(std::string s) {
    for (char& ch : s) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return s;
}

std::string trim_copy(const std::string& s) {
    size_t begin = 0;
    while (begin < s.size() && std::isspace(static_cast<unsigned char>(s[begin]))) ++begin;
    size_t end = s.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(s[end - 1]))) --end;
    return s.substr(begin, end - begin);
}

std::string trim_field_copy(const std::string& s) {
    size_t begin = 0;
    size_t end = s.size();
    while (begin < end) {
        if (std::isspace(static_cast<unsigned char>(s[begin]))) {
            ++begin;
        } else if (begin + 2 <= end && s.compare(begin, 2, "\xC2\xA0") == 0) {
            begin += 2;
        } else if (begin + 3 <= end && s.compare(begin, 3, "\xE3\x80\x80") == 0) {
            begin += 3;
        } else {
            break;
        }
    }
    while (end > begin) {
        if (std::isspace(static_cast<unsigned char>(s[end - 1]))) {
            --end;
        } else if (end >= begin + 2 && s.compare(end - 2, 2, "\xC2\xA0") == 0) {
            end -= 2;
        } else if (end >= begin + 3 && s.compare(end - 3, 3, "\xE3\x80\x80") == 0) {
            end -= 3;
        } else {
            break;
        }
    }
    return s.substr(begin, end - begin);
}

bool ascii_ieq(const std::string& a, const std::string& b) {
    return ascii_lower(a) == ascii_lower(b);
}

void append_json_escaped(std::ostringstream& out, const std::string& s) {
    for (unsigned char c : s) {
        switch (c) {
            case '\\': out << "\\\\"; break;
            case '"': out << "\\\""; break;
            case '\b': out << "\\b"; break;
            case '\f': out << "\\f"; break;
            case '\n': out << "\\n"; break;
            case '\r': out << "\\r"; break;
            case '\t': out << "\\t"; break;
            default:
                if (c < 0x20) {
                    out << "\\u" << std::hex << std::setw(4) << std::setfill('0')
                        << static_cast<int>(c) << std::dec << std::setfill(' ');
                } else {
                    out << static_cast<char>(c);
                }
        }
    }
}

void append_json_string(std::ostringstream& out, const std::string& s) {
    out << "\"";
    append_json_escaped(out, s);
    out << "\"";
}

std::string json_escape(const std::string& s) {
    std::ostringstream out;
    append_json_escaped(out, s);
    return out.str();
}

std::string json_number(double value) {
    if (std::isnan(value)) {
        return "null";
    }
    if (std::isinf(value)) {
        return value > 0 ? "1e999" : "-1e999";
    }
    std::array<char, 64> buffer{};
    int written = std::snprintf(buffer.data(), buffer.size(), "%.17g", value);
    if (written > 0 && static_cast<size_t>(written) < buffer.size()) {
        return std::string(buffer.data(), static_cast<size_t>(written));
    }
    std::ostringstream out;
    out << std::setprecision(17) << value;
    return out.str();
}

std::uint64_t stable_hash64(const std::string& text);
std::string hex64(std::uint64_t value);

double parse_first_version(const std::string& header) {
    for (size_t i = 0; i < header.size(); ++i) {
        if (!std::isdigit(static_cast<unsigned char>(header[i]))) continue;
        size_t j = i;
        bool dot = false;
        while (j < header.size()) {
            char c = header[j];
            if (std::isdigit(static_cast<unsigned char>(c))) {
                ++j;
            } else if (c == '.' && !dot) {
                dot = true;
                ++j;
            } else {
                break;
            }
        }
        return std::stod(header.substr(i, j - i));
    }
    throw std::runtime_error("Header version not found");
}

std::string declared_encoding_from_header(const std::string& header) {
    size_t colon = header.find(':');
    if (colon == std::string::npos) return "utf-8";
    size_t i = colon + 1;
    if (i >= header.size() || !std::isalpha(static_cast<unsigned char>(header[i]))) {
        return "utf-8";
    }
    size_t j = i + 1;
    while (j < header.size()) {
        char c = header[j];
        if (std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_') {
            ++j;
        } else {
            break;
        }
    }
    std::string enc = ascii_lower(header.substr(i, j - i));
    if (enc == "shift_jis" || enc == "sjis") return "cp932";
    return enc;
}

std::string normalized_source_path(const std::filesystem::path& path);
std::string normalized_source_key(std::string path);

std::vector<size_t> build_line_starts(const std::string& text) {
    std::vector<size_t> starts;
    starts.reserve(std::count(text.begin(), text.end(), '\n') + 1);
    starts.push_back(0);
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '\n' && i + 1 <= text.size()) starts.push_back(i + 1);
    }
    return starts;
}


std::string detect_newline(const std::string& text) {
    if (text.find("\r\n") != std::string::npos) return "crlf";
    if (text.find('\n') != std::string::npos) return "lf";
    if (text.find('\r') != std::string::npos) return "cr";
    return "none";
}

LoadedText make_loaded_header_text(const std::filesystem::path& path,
                                   std::string text,
                                   std::string encoding,
                                   std::string newline,
                                   std::string source_hash,
                                   size_t byte_length,
                                   const std::string& head_str,
                                   double min_version,
                                   bool collect_source_metadata) {
    size_t line_end = text.find('\n');
    std::string header = line_end == std::string::npos ? text : text.substr(0, line_end);
    if (ascii_lower(header).find(ascii_lower(head_str)) == std::string::npos) {
        throw std::runtime_error(path_to_utf8(path) + " is not " + head_str);
    }
    if (parse_first_version(header) < min_version) {
        throw std::runtime_error(path_to_utf8(path) + " is under Ver." + json_number(min_version));
    }
    size_t body_offset = line_end == std::string::npos ? text.size() : line_end + 1;
    std::string body = line_end == std::string::npos ? std::string() : text.substr(body_offset);
    std::string normalized_path = normalized_source_path(path);
    std::string normalized_key = normalized_source_key(normalized_path);
    std::vector<size_t> line_starts;
    if (collect_source_metadata) line_starts = build_line_starts(body);
    return {std::move(body), path, std::filesystem::absolute(path).parent_path(), normalized_path,
            normalized_key, std::move(encoding), std::move(newline), std::move(source_hash),
            std::move(line_starts),
            byte_length, body_offset, line_end == std::string::npos ? 1 : 2};
}

LoadedText load_header_text(const std::filesystem::path& path,
                            const std::string& head_str,
                            double min_version,
                            const SourceTextOverrides* overrides,
                            bool collect_source_metadata) {
    std::string normalized_path = normalized_source_path(path);
    std::string normalized_key = normalized_source_key(normalized_path);
    if (overrides) {
        auto override_it = overrides->find(normalized_key);
        if (override_it != overrides->end()) {
            const SourceTextOverride& source = override_it->second;
            return make_loaded_header_text(path, source.text, source.encoding, source.newline,
                                           collect_source_metadata ? source.current_hash : std::string{},
                                           source.byte_length,
                                           head_str, min_version,
                                           collect_source_metadata);
        }
    }

    ScopedTimer timer(g_active_timing ? &g_active_timing->read_decode_seconds : nullptr);
    std::string bytes = read_binary_file(path);
    std::string text;
    std::string encoding;
    if (bytes.size() >= 2 &&
        static_cast<unsigned char>(bytes[0]) == 0xff &&
        static_cast<unsigned char>(bytes[1]) == 0xfe) {
        encoding = "utf-16le";
        text = decode_utf16(bytes, true);
    } else if (bytes.size() >= 2 &&
               static_cast<unsigned char>(bytes[0]) == 0xfe &&
               static_cast<unsigned char>(bytes[1]) == 0xff) {
        encoding = "utf-16be";
        text = decode_utf16(bytes, false);
    } else if (has_utf8_bom(bytes)) {
        encoding = "utf-8";
        text = decode_codepage(bytes.substr(3), CP_UTF8, true);
    } else {
        std::string ascii_header = first_line_ascii(bytes);
        encoding = declared_encoding_from_header(ascii_header);
        try {
            if (ascii_lower(encoding) == "cp932") {
                text = decode_codepage(bytes, 932, false);
            } else {
                text = decode_codepage(bytes, CP_UTF8, true);
            }
        } catch (...) {
            std::string retry = ascii_lower(encoding) == "utf-8" ? "cp932" : "utf-8";
            log_warn(path_to_utf8(path.filename()) + " cannot be decoded with " + encoding +
                     ". Kobushi tries to decode with " + retry + ".");
            encoding = retry;
            if (retry == "cp932") {
                text = decode_codepage(bytes, 932, false);
            } else {
                text = decode_codepage(bytes, CP_UTF8, false);
            }
        }
    }

    std::string newline = detect_newline(text);
    std::string source_hash;
    if (collect_source_metadata) source_hash = hex64(stable_hash64(bytes));
    return make_loaded_header_text(path, std::move(text), std::move(encoding), std::move(newline),
                                   std::move(source_hash), bytes.size(),
                                   head_str, min_version,
                                   collect_source_metadata);
}

std::filesystem::path join_path(const std::filesystem::path& root, const std::string& file) {
    std::string normalized = file;
    std::replace(normalized.begin(), normalized.end(), '\\', '/');
    std::filesystem::path p = path_from_utf8(normalized);
    if (p.is_absolute()) return p;
    return root / p;
}

double as_number(const Value& value, double fallback) {
    if (value.kind == ValueKind::Number) return value.number;
    if (value.kind == ValueKind::Null || value.kind == ValueKind::ContinueValue) return fallback;
    char* end = nullptr;
    errno = 0;
    double result = std::strtod(value.text.c_str(), &end);
    if (end != value.text.c_str() && errno == 0) return result;
    throw std::runtime_error("Expected numeric value: " + value.text);
}

std::string as_text(const Value& value) {
    if (value.kind == ValueKind::String) return value.text;
    if (value.kind == ValueKind::Number) {
        std::ostringstream out;
        out << std::setprecision(17) << value.number;
        return out.str();
    }
    if (value.kind == ValueKind::ContinueValue) return "c";
    return "";
}

std::string key_text(const Value& value) {
    if (value.kind == ValueKind::Number) {
        long long i = static_cast<long long>(value.number);
        return ascii_lower(std::to_string(i));
    }
    return ascii_lower(as_text(value));
}

const Value& arg_or_null(const std::vector<Value>& values, size_t index) {
    static const Value null_value = Value::null();
    return index < values.size() ? values[index] : null_value;
}

std::string json_value(const Value& value) {
    switch (value.kind) {
        case ValueKind::Null: return "null";
        case ValueKind::ContinueValue: return "\"c\"";
        case ValueKind::Number: return json_number(value.number);
        case ValueKind::String: return "\"" + json_escape(value.text) + "\"";
    }
    return "null";
}

std::vector<std::string> parse_comma_separated_fields(const std::string& line, bool stop_on_inline_hash) {
    std::vector<std::string> fields;
    std::string field;
    bool quoted = false;
    for (size_t i = 0; i < line.size(); ++i) {
        char ch = line[i];
        if (ch == '"') {
            if (quoted && i + 1 < line.size() && line[i + 1] == '"') {
                field.push_back('"');
                ++i;
            } else {
                quoted = !quoted;
            }
        } else if (ch == ',' && !quoted) {
            fields.push_back(trim_field_copy(field));
            field.clear();
        } else if (ch == '#' && !quoted && stop_on_inline_hash) {
            break;
        } else {
            field.push_back(ch);
        }
    }
    fields.push_back(trim_field_copy(field));
    return fields;
}

void trim_trailing_empty_fields(std::vector<std::string>& fields) {
    while (!fields.empty() && fields.back().empty()) {
        fields.pop_back();
    }
}

std::string strip_ini_comment_copy(const std::string& line) {
    bool single_quoted = false;
    bool double_quoted = false;
    for (size_t i = 0; i < line.size(); ++i) {
        char ch = line[i];
        if (ch == '\'' && !double_quoted) {
            single_quoted = !single_quoted;
        } else if (ch == '"' && !single_quoted) {
            double_quoted = !double_quoted;
        } else if ((ch == '#' || ch == ';') && !single_quoted && !double_quoted) {
            return line.substr(0, i);
        }
    }
    return line;
}

std::string trim_matching_quotes(std::string text) {
    text = trim_field_copy(text);
    if (text.size() >= 2) {
        char first = text.front();
        char last = text.back();
        if ((first == '\'' && last == '\'') || (first == '"' && last == '"')) {
            return text.substr(1, text.size() - 2);
        }
    }
    return text;
}

int parse_sound_buffer_count(const std::string& text) {
    std::string trimmed = trim_field_copy(text);
    if (trimmed.empty()) return 1;

    char* end = nullptr;
    errno = 0;
    long value = std::strtol(trimmed.c_str(), &end, 10);
    while (end && *end && std::isspace(static_cast<unsigned char>(*end))) ++end;
    if (end == trimmed.c_str() || errno == ERANGE || (end && *end != '\0') || value < 1) {
        return 1;
    }
    return static_cast<int>(std::min<long>(value, std::numeric_limits<int>::max()));
}

LoadedText load_header_text(const MapContext& ctx,
                            const std::filesystem::path& path,
                            const std::string& head_str,
                            double min_version) {
    return load_header_text(path, head_str, min_version, &ctx.source_overrides,
                            ctx.parse_options.collect_edit_metadata);
}

std::string normalized_source_path(const std::filesystem::path& path) {
    std::error_code ec;
    std::filesystem::path abs = std::filesystem::absolute(path, ec);
    if (ec) abs = path;
    return path_to_utf8(abs.lexically_normal());
}

std::string normalized_source_key(std::string path) {
    std::replace(path.begin(), path.end(), '\\', '/');
    return ascii_lower(std::move(path));
}

size_t register_source_file_index(MapContext& ctx, const LoadedText& loaded) {
    auto existing = ctx.source_file_indices.find(loaded.normalized_key);
    if (existing != ctx.source_file_indices.end()) return existing->second;

    SourceFileRecord record;
    record.file_path = loaded.normalized_path;
    record.source_key = loaded.normalized_key;
    record.display_path = record.file_path;
    record.encoding = loaded.encoding;
    record.newline = loaded.newline;
    record.source_hash = loaded.source_hash;
    record.byte_length = loaded.byte_length;
    size_t index = ctx.source_files.size();
    ctx.source_file_indices[record.source_key] = index;
    ctx.source_files.push_back(std::move(record));
    return index;
}

void register_source_file(MapContext& ctx, const LoadedText& loaded) {
    register_source_file_index(ctx, loaded);
}

std::string include_stack_key(const std::vector<std::string>& stack) {
    std::string key;
    for (const std::string& item : stack) {
        key += item;
        key.push_back('\n');
    }
    return key;
}

size_t intern_include_stack(MapContext& ctx, const std::vector<std::string>& stack) {
    std::string key = include_stack_key(stack);
    auto existing = ctx.include_stack_indices.find(key);
    if (existing != ctx.include_stack_indices.end()) return existing->second;
    size_t index = ctx.include_stacks.size();
    ctx.include_stack_indices[std::move(key)] = index;
    ctx.include_stacks.push_back(stack);
    return index;
}

std::string make_include_invocation_key(const std::string& parent_key,
                                        const std::string& source_key,
                                        size_t byte_start,
                                        size_t byte_end,
                                        size_t occurrence) {
    // Length-prefix the strings so paths and parent keys cannot make two
    // different invocation chains serialize to the same key.
    std::string key;
    key.reserve(parent_key.size() + source_key.size() + 80);
    key += std::to_string(parent_key.size());
    key.push_back(':');
    key += parent_key;
    key.push_back('|');
    key += std::to_string(source_key.size());
    key.push_back(':');
    key += source_key;
    key.push_back('|');
    key += std::to_string(byte_start);
    key.push_back(':');
    key += std::to_string(byte_end);
    key.push_back(':');
    key += std::to_string(occurrence);
    return key;
}

size_t intern_include_invocation_key(MapContext& ctx, const std::string& key) {
    auto existing = ctx.include_invocation_indices.find(key);
    if (existing != ctx.include_invocation_indices.end()) return existing->second;
    size_t index = ctx.include_invocation_keys.size();
    ctx.include_invocation_indices[key] = index;
    ctx.include_invocation_keys.push_back(key);
    return index;
}

const SourceFileRecord* source_file_record(const MapContext& ctx, const SourceSpan& span) {
    if (span.source_file_index >= ctx.source_files.size()) return nullptr;
    return &ctx.source_files[span.source_file_index];
}

const std::string& source_file_path(const MapContext& ctx, const SourceSpan& span) {
    static const std::string empty;
    const SourceFileRecord* record = source_file_record(ctx, span);
    return record ? record->file_path : empty;
}

const std::string& source_file_key(const MapContext& ctx, const SourceSpan& span) {
    static const std::string empty;
    const SourceFileRecord* record = source_file_record(ctx, span);
    return record ? record->source_key : empty;
}

const std::string& source_file_encoding(const MapContext& ctx, const SourceSpan& span) {
    static const std::string empty;
    const SourceFileRecord* record = source_file_record(ctx, span);
    return record ? record->encoding : empty;
}

const std::string& source_file_newline(const MapContext& ctx, const SourceSpan& span) {
    static const std::string empty;
    const SourceFileRecord* record = source_file_record(ctx, span);
    return record ? record->newline : empty;
}

const std::vector<std::string>& source_include_stack(const MapContext& ctx, const SourceSpan& span) {
    static const std::vector<std::string> empty;
    if (span.include_stack_index >= ctx.include_stacks.size()) return empty;
    return ctx.include_stacks[span.include_stack_index];
}

const std::string& source_include_invocation_key(const MapContext& ctx, const SourceSpan& span) {
    static const std::string empty;
    if (span.include_invocation_index >= ctx.include_invocation_keys.size()) return empty;
    return ctx.include_invocation_keys[span.include_invocation_index];
}

int utf8_column_count(const std::string& text, size_t begin, size_t end) {
    int count = 0;
    for (size_t i = begin; i < end && i < text.size(); ++i) {
        unsigned char c = static_cast<unsigned char>(text[i]);
        if (c == '\r') continue;
        if ((c & 0xc0) != 0x80) ++count;
    }
    return count;
}

std::pair<int, int> line_column_for_body_pos(const LoadedText& loaded, size_t body_pos) {
    size_t limit = std::min(body_pos, loaded.body.size());
    auto it = std::upper_bound(loaded.line_starts.begin(), loaded.line_starts.end(), limit);
    size_t line_index = it == loaded.line_starts.begin()
        ? 0
        : static_cast<size_t>(std::distance(loaded.line_starts.begin(), it) - 1);
    size_t line_start = loaded.line_starts.empty() ? 0 : loaded.line_starts[line_index];
    int line = loaded.body_start_line + static_cast<int>(line_index);
    int column = utf8_column_count(loaded.body, line_start, limit) + 1;
    return {line, column};
}

SourceSpan make_source_span(MapContext& ctx,
                            const LoadedText& loaded,
                            size_t body_start,
                            size_t body_end,
                            const std::vector<std::string>& include_stack) {
    SourceSpan span;
    if (!ctx.parse_options.collect_edit_metadata) return span;
    span.source_file_index = register_source_file_index(ctx, loaded);
    span.include_stack_index = intern_include_stack(ctx, include_stack);
    if (ctx.current_include_invocation_key.empty()) {
        ctx.current_include_invocation_key = kRootIncludeInvocationKey;
        ctx.current_include_invocation_index = kNoSourceRef;
    }
    if (ctx.current_include_invocation_index >= ctx.include_invocation_keys.size() ||
        ctx.include_invocation_keys[ctx.current_include_invocation_index] !=
            ctx.current_include_invocation_key) {
        ctx.current_include_invocation_index =
            intern_include_invocation_key(ctx, ctx.current_include_invocation_key);
    }
    span.include_invocation_index = ctx.current_include_invocation_index;
    span.byte_start = loaded.body_offset + body_start;
    span.byte_end = loaded.body_offset + body_end;
    auto start = line_column_for_body_pos(loaded, body_start);
    auto end = line_column_for_body_pos(loaded, body_end);
    span.line = start.first;
    span.column = start.second;
    span.line_end = end.first;
    span.column_end = end.second;
    return span;
}

std::vector<std::string> include_stack_for_file(const MapContext& ctx, const std::filesystem::path& path) {
    std::vector<std::string> stack = ctx.include_stack;
    std::string file_path = normalized_source_path(path);
    if (stack.empty() || normalized_source_key(stack.back()) != normalized_source_key(file_path)) {
        stack.push_back(std::move(file_path));
    }
    return stack;
}

std::string raw_text_preview(std::string text) {
    for (char& ch : text) {
        if (ch == '\r' || ch == '\n' || ch == '\t') ch = ' ';
    }
    if (text.size() > 120) text = text.substr(0, 117) + "...";
    return text;
}

VariableEnvironmentSnapshot current_variable_environment_snapshot(MapContext& ctx) {
    if (!ctx.variable_environment_snapshot) {
        rebuild_variable_environment_snapshot(ctx);
    }
    return ctx.variable_environment_snapshot;
}

void rebuild_variable_environment_snapshot(MapContext& ctx) {
    ctx.variable_environment_snapshot =
        std::make_shared<const VariableEnvironment>(ctx.variables);
}

std::string make_edit_id(const std::string& source_key, int global_order,
                         const std::string& kind, int element_index);

size_t add_parsed_statement(MapContext& ctx,
                            std::string kind,
                            SourceSpan source,
                            std::string raw_text,
                            std::string raw_arguments,
                            std::vector<Value> evaluated_values,
                            std::string distance_expression,
                            double distance_value) {
    if (!ctx.parse_options.collect_edit_metadata) return kNoSourceRef;
    ParsedStatement statement;
    statement.statement_kind = std::move(kind);
    statement.source = std::move(source);
    statement.raw_text = std::move(raw_text);
    statement.raw_text_preview = raw_text_preview(statement.raw_text);
    statement.raw_arguments = std::move(raw_arguments);
    statement.evaluated_values = std::move(evaluated_values);
    statement.variable_environment = current_variable_environment_snapshot(ctx);
    statement.distance_expression = std::move(distance_expression);
    statement.distance_value = distance_value;
    statement.global_order = ctx.next_edit_order();
    statement.edit_id = make_edit_id(source_file_key(ctx, statement.source),
                                     statement.global_order,
                                     "statement." + statement.statement_kind,
                                     0);
    ctx.parsed_statements.push_back(std::move(statement));
    return ctx.parsed_statements.size() - 1;
}

EditSourceRef next_active_edit_ref(MapContext& ctx) {
    if (!ctx.parse_options.collect_edit_metadata) return {};
    if (ctx.active_statement_index == kNoSourceRef) return {};
    EditSourceRef ref;
    ref.statement_index = ctx.active_statement_index;
    ref.element_index = ctx.active_statement_next_element_index++;
    return ref;
}

std::vector<size_t> merge_source_file_records(MapContext& dest, const MapContext& child) {
    std::vector<size_t> index_map(child.source_files.size(), kNoSourceRef);
    for (size_t i = 0; i < child.source_files.size(); ++i) {
        const SourceFileRecord& record = child.source_files[i];
        auto existing = dest.source_file_indices.find(record.source_key);
        if (existing != dest.source_file_indices.end()) {
            index_map[i] = existing->second;
            continue;
        }
        size_t dest_index = dest.source_files.size();
        dest.source_file_indices[record.source_key] = dest_index;
        dest.source_files.push_back(record);
        index_map[i] = dest_index;
    }
    return index_map;
}

std::vector<size_t> merge_include_stacks(MapContext& dest, const MapContext& child) {
    std::vector<size_t> index_map(child.include_stacks.size(), kNoSourceRef);
    for (size_t i = 0; i < child.include_stacks.size(); ++i) {
        index_map[i] = intern_include_stack(dest, child.include_stacks[i]);
    }
    return index_map;
}

std::vector<size_t> merge_include_invocation_keys(MapContext& dest, const MapContext& child) {
    std::vector<size_t> index_map(child.include_invocation_keys.size(), kNoSourceRef);
    for (size_t i = 0; i < child.include_invocation_keys.size(); ++i) {
        index_map[i] = intern_include_invocation_key(dest, child.include_invocation_keys[i]);
    }
    return index_map;
}

void offset_edit_ref(EditSourceRef& ref, size_t statement_index_base) {
    if (ref.valid()) ref.statement_index += statement_index_base;
}


std::vector<Value> values_from_fields(const std::vector<std::string>& fields) {
    std::vector<Value> values;
    values.reserve(fields.size());
    for (const std::string& field : fields) values.push_back(Value::str(field));
    return values;
}

EditSourceRef add_loaded_line_statement(MapContext& ctx,
                                        const LoadedText& loaded,
                                        const std::vector<std::string>& include_stack,
                                        const std::string& kind,
                                        size_t line_start,
                                        size_t line_end,
                                        const std::string& line,
                                        const std::vector<std::string>& fields) {
    if (!ctx.parse_options.collect_edit_metadata) return {};
    size_t statement_index = add_parsed_statement(
        ctx, kind,
        make_source_span(ctx, loaded, line_start, line_end, include_stack),
        line,
        line,
        values_from_fields(fields),
        ctx.distance_expression,
        ctx.distance);
    return EditSourceRef{statement_index, 0};
}

bool value_equal(const Value& a, const Value& b) {
    if (a.kind != b.kind) return false;
    if (a.kind == ValueKind::Number) {
        if (std::isnan(a.number) && std::isnan(b.number)) return true;
        return a.number == b.number;
    }
    return a.text == b.text;
}

bool variable_value_matches(const std::unordered_map<std::string, Value>& current,
                            const std::unordered_map<std::string, Value>& seed,
                            const std::string& key) {
    auto current_it = current.find(key);
    auto seed_it = seed.find(key);
    if (current_it == current.end() || seed_it == seed.end()) {
        return current_it == current.end() && seed_it == seed.end();
    }
    return value_equal(current_it->second, seed_it->second);
}

void note_distance_use(MapContext& ctx) {
    if (!ctx.has_distance_assignment) ctx.depends_on_initial_distance = true;
}

void note_variable_read(MapContext& ctx, const std::string& key) {
    if (ctx.variable_writes.find(key) == ctx.variable_writes.end()) {
        ctx.external_variable_reads.insert(key);
    }
}

void note_variable_write(MapContext& ctx, const std::string& key) {
    ctx.variable_writes.insert(key);
}

std::string format_seconds(double seconds) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(3) << seconds << "s";
    return out.str();
}

void log_load_timing(const MapContext& ctx) {
    log_info("load timing: read/decode=" + format_seconds(ctx.timing.read_decode_seconds) +
             ", parse=" + format_seconds(ctx.timing.parse_seconds) +
             ", relocate=" + format_seconds(ctx.timing.relocate_seconds) +
             ", owntrack=" + format_seconds(ctx.timing.owntrack_seconds) +
             ", JSON=" + format_seconds(ctx.timing.json_seconds));
    for (const auto& item : ctx.timing.othertrack_seconds) {
        log_info("load timing: othertrack[" + item.first + "]=" + format_seconds(item.second));
    }
}

void add_controlpoint(MapContext& ctx, double value) {
    ctx.controlpoints.push_back(value);
}

void set_distance(MapContext& ctx, double value) {
    ctx.has_distance_assignment = true;
    ctx.distance = value;
    add_controlpoint(ctx, value);
}

void put_own(MapContext& ctx, const std::string& key, const Value& value, const std::string& flag) {
    note_distance_use(ctx);
    Value stored = value.is_null() ? Value::cont() : value;
    OwnTrackEvent row;
    row.distance = ctx.distance;
    row.key = key;
    row.value = stored;
    row.flag = flag;
    attach_active_edit_ref(ctx, row);
    ctx.own_track.push_back(std::move(row));
}

void ensure_othertrack(MapContext& ctx, const std::string& key) {
    if (ctx.othertrack.find(key) == ctx.othertrack.end()) {
        ctx.othertrack[key] = {};
        ctx.othertrack_order.push_back(key);
    }
}

void put_other(MapContext& ctx, const Value& track_key, const std::string& element_key,
               const Value& value, const std::string& flag) {
    note_distance_use(ctx);
    std::string key = key_text(track_key);
    ensure_othertrack(ctx, key);
    Value stored = value.is_null() ? Value::cont() : value;
    OtherTrackEvent row;
    row.distance = ctx.distance;
    row.track_key = key;
    row.key = element_key;
    row.value = stored;
    row.flag = flag;
    attach_active_edit_ref(ctx, row);
    ctx.othertrack[key].push_back(std::move(row));
}


} // namespace kme::maploader::detail
