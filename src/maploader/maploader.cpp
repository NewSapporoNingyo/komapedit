/*
 * Copyright (c) 2026 Sapporo_ningyo
 *
 * Portions of the map parsing and track-geometry design are derived from or
 * reimplemented with reference to kobushi-trackviewer, Copyright (c) 2021-2024
 * konawasabi, licensed under Apache License 2.0.
 *
 * Licensed under Apache License 2.0; see LICENSE and NOTICE.
 */

#define MAPLOADER_EXPORTS
#include "maploader.h"

#include "c_api.h"
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

#if !defined(_WIN32)
constexpr unsigned int CP_UTF8 = 65001;
#endif

namespace {

using kme::maploader::copy_c_string;
using kme::maploader::decode_codepage;
using kme::maploader::decode_utf16;
using kme::maploader::encode_text_for_writeback;
using kme::maploader::first_line_ascii;
using kme::maploader::has_utf8_bom;
using kme::maploader::last_error_c_str;
using kme::maploader::log_error;
using kme::maploader::log_info;
using kme::maploader::log_warn;
using kme::maploader::path_from_utf8;
using kme::maploader::path_to_utf8;
using kme::maploader::read_binary_file;
using kme::maploader::set_last_error;
using kme::maploader::set_log_callback;
using SteadyClock = std::chrono::steady_clock;

constexpr double kInf = std::numeric_limits<double>::infinity();
constexpr double kPi = 3.141592653589793238462643383279502884;

struct LoadTiming {
    double read_decode_seconds = 0.0;
    double parse_seconds = 0.0;
    double relocate_seconds = 0.0;
    double owntrack_seconds = 0.0;
    double json_seconds = 0.0;
    std::vector<std::pair<std::string, double>> othertrack_seconds;
};

thread_local LoadTiming* g_active_timing = nullptr;

double elapsed_seconds_since(SteadyClock::time_point started_at) {
    return std::chrono::duration<double>(SteadyClock::now() - started_at).count();
}

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
    explicit ActiveTimingScope(LoadTiming& timing)
        : previous_(g_active_timing) {
        g_active_timing = &timing;
    }

    ~ActiveTimingScope() {
        g_active_timing = previous_;
    }

private:
    LoadTiming* previous_ = nullptr;
};

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
                                   double min_version) {
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
    std::vector<size_t> line_starts = build_line_starts(body);
    return {std::move(body), path, std::filesystem::absolute(path).parent_path(), normalized_path,
            normalized_key, std::move(encoding), std::move(newline), std::move(source_hash),
            std::move(line_starts),
            byte_length, body_offset, line_end == std::string::npos ? 1 : 2};
}

LoadedText load_header_text(const std::filesystem::path& path,
                            const std::string& head_str,
                            double min_version,
                            const SourceTextOverrides* overrides = nullptr) {
    std::string normalized_path = normalized_source_path(path);
    std::string normalized_key = normalized_source_key(normalized_path);
    if (overrides) {
        auto override_it = overrides->find(normalized_key);
        if (override_it != overrides->end()) {
            const SourceTextOverride& source = override_it->second;
            return make_loaded_header_text(path, source.text, source.encoding, source.newline,
                                           source.current_hash, source.byte_length,
                                           head_str, min_version);
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
    return make_loaded_header_text(path, std::move(text), std::move(encoding), std::move(newline),
                                   hex64(stable_hash64(bytes)), bytes.size(),
                                   head_str, min_version);
}

std::filesystem::path join_path(const std::filesystem::path& root, const std::string& file) {
    std::string normalized = file;
    std::replace(normalized.begin(), normalized.end(), '\\', '/');
    std::filesystem::path p = path_from_utf8(normalized);
    if (p.is_absolute()) return p;
    return root / p;
}

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

double as_number(const Value& value, double fallback = 0.0) {
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

const Value& arg_or_null(const std::vector<Value>& values, size_t index = 0) {
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

constexpr size_t kNoSourceRef = std::numeric_limits<size_t>::max();

struct SourceFileRecord {
    std::string file_path;
    std::string source_key;
    std::string display_path;
    std::string encoding;
    std::string newline;
    std::string source_hash;
    size_t byte_length = 0;
};

struct SourceSpan {
    size_t source_file_index = kNoSourceRef;
    size_t include_stack_index = kNoSourceRef;
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
    size_t statement_index = kNoSourceRef;
    int element_index = 0;

    bool valid() const {
        return statement_index != kNoSourceRef;
    }
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
    EditSourceRef edit_ref;
};

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

struct MapContext {
    std::filesystem::path rootpath;
    std::string rootpath_utf8;
    std::string entry_file_path;
    std::string current_file_path;
    std::vector<std::string> include_stack;
    SourceTextOverrides source_overrides;
    double unit_distance = 25.0;
    double distance = 0.0;
    std::string distance_expression;
    int parse_order = 0;
    int edit_order = 0;
    std::unordered_map<std::string, Value> variables;
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
    std::vector<SpeedLimitEvent> speedlimits;
    Matrix owntrack_buffer;
    Matrix curveradius_buffer;
    Matrix structure_put_buffer;
    std::map<std::string, Matrix> othertrack_buffers;
    std::array<double, 3> cp_arbdistribution{0.0, 0.0, 0.0};
    std::array<double, 3> cp_arbdistribution_default{0.0, 0.0, 0.0};
    std::array<double, 2> cp_defaultrange{0.0, 0.0};
    bool has_cp_arbdistribution = false;
    std::map<unsigned, std::string> ir_json_cache_by_flags;
    LoadTiming timing;
    bool load_timing_logged = false;
    std::vector<SourceFileRecord> source_files;
    std::unordered_map<std::string, size_t> source_file_indices;
    std::vector<std::vector<std::string>> include_stacks;
    std::unordered_map<std::string, size_t> include_stack_indices;
    mutable std::unordered_map<std::string, std::string> element_edit_id_cache;
    std::vector<ParsedStatement> parsed_statements;
    size_t active_statement_index = kNoSourceRef;
    int active_statement_next_element_index = 0;

    int next_parse_order() {
        return ++parse_order;
    }

    int next_edit_order() {
        return ++edit_order;
    }
};

LoadedText load_header_text(const MapContext& ctx,
                            const std::filesystem::path& path,
                            const std::string& head_str,
                            double min_version) {
    return load_header_text(path, head_str, min_version, &ctx.source_overrides);
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
    span.source_file_index = register_source_file_index(ctx, loaded);
    span.include_stack_index = intern_include_stack(ctx, include_stack);
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
    ParsedStatement statement;
    statement.statement_kind = std::move(kind);
    statement.source = std::move(source);
    statement.raw_text = std::move(raw_text);
    statement.raw_text_preview = raw_text_preview(statement.raw_text);
    statement.raw_arguments = std::move(raw_arguments);
    statement.evaluated_values = std::move(evaluated_values);
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
    if (ctx.active_statement_index == kNoSourceRef) return {};
    EditSourceRef ref;
    ref.statement_index = ctx.active_statement_index;
    ref.element_index = ctx.active_statement_next_element_index++;
    return ref;
}

template <typename Row>
void attach_active_edit_ref(MapContext& ctx, Row& row) {
    row.edit_ref = next_active_edit_ref(ctx);
}

struct ActiveStatementScope {
    MapContext& ctx;
    size_t old_index = kNoSourceRef;
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

void offset_edit_ref(EditSourceRef& ref, size_t statement_index_base) {
    if (ref.valid()) ref.statement_index += statement_index_base;
}

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

void put_own(MapContext& ctx, const std::string& key, const Value& value, const std::string& flag = "") {
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
               const Value& value, const std::string& flag = "") {
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

struct MapObject {
    std::string label;
    Value key;
    bool has_key = false;
};

struct MapFunction {
    std::string label;
    std::vector<Value> args;
    std::string raw_arguments;
};

struct ParsedMapElement {
    std::vector<MapObject> objects;
    MapFunction function;
};

class Parser {
public:
    Parser(MapContext& context, LoadedText loaded)
        : ctx_(context), loaded_(std::move(loaded)), src_(loaded_.body), file_path_(loaded_.path) {
        register_source_file(ctx_, loaded_);
        ctx_.current_file_path = loaded_.normalized_path;
        if (ctx_.include_stack.empty()) {
            ctx_.include_stack.push_back(ctx_.current_file_path);
        }
    }

    void parse() {
        while (true) {
            skip();
            if (eof()) break;
            parse_statement();
        }
        flush_pending_includes();
    }

private:
    struct IncludeResult {
        MapContext context;
        std::string error;
    };

    struct PendingInclude {
        std::filesystem::path path;
        double seed_distance = 0.0;
        std::unordered_map<std::string, Value> seed_variables;
        std::future<IncludeResult> future;
    };

    MapContext& ctx_;
    LoadedText loaded_;
    std::string src_;
    std::filesystem::path file_path_;
    size_t pos_ = 0;
    std::vector<PendingInclude> pending_includes_;

    bool eof() const { return pos_ >= src_.size(); }
    char peek() const { return eof() ? '\0' : src_[pos_]; }

    bool starts_with(size_t pos, const std::string& token) const {
        return src_.compare(pos, token.size(), token) == 0;
    }

    void skip() {
        while (!eof()) {
            unsigned char c = static_cast<unsigned char>(src_[pos_]);
            if (std::isspace(c) || c == '\\' || c == '"') {
                ++pos_;
            } else if (c == '#') {
                while (!eof() && src_[pos_] != '\n') ++pos_;
            } else if (starts_with(pos_, "//")) {
                pos_ += 2;
                while (!eof() && src_[pos_] != '\n') ++pos_;
            } else if (starts_with(pos_, "\xC2\xA0")) {
                pos_ += 2;
            } else if (starts_with(pos_, "\xE3\x80\x80")) {
                pos_ += 3;
            } else {
                break;
            }
        }
    }

    bool accept(char ch) {
        skip();
        if (peek() == ch) {
            ++pos_;
            return true;
        }
        return false;
    }

    void expect(char ch) {
        if (!accept(ch)) {
            std::ostringstream out;
            out << "Expected '" << ch << "' at byte " << pos_;
            throw std::runtime_error(out.str());
        }
    }

    bool is_ident_start(unsigned char c) const {
        return std::isalpha(c) || c == '_' || c >= 0x80;
    }

    bool is_ident_part(unsigned char c) const {
        return std::isalnum(c) || c == '_' || c >= 0x80;
    }

    std::string parse_label() {
        skip();
        if (eof() || !is_ident_start(static_cast<unsigned char>(peek()))) {
            throw std::runtime_error("Expected label at byte " + std::to_string(pos_));
        }
        size_t start = pos_;
        ++pos_;
        while (!eof() && is_ident_part(static_cast<unsigned char>(peek()))) ++pos_;
        return src_.substr(start, pos_ - start);
    }

    std::string parse_variable_name() {
        skip();
        if (eof() || !is_ident_part(static_cast<unsigned char>(peek()))) {
            throw std::runtime_error("Expected variable name at byte " + std::to_string(pos_));
        }
        size_t start = pos_;
        ++pos_;
        while (!eof() && is_ident_part(static_cast<unsigned char>(peek()))) ++pos_;
        return src_.substr(start, pos_ - start);
    }

    bool current_starts_map_element() {
        size_t p = pos_;
        skip_at(p);
        if (p >= src_.size() || !is_ident_start(static_cast<unsigned char>(src_[p]))) return false;
        skip_label_at(p);
        skip_at(p);
        if (p < src_.size() && src_[p] == '[') {
            int depth = 1;
            ++p;
            while (p < src_.size() && depth > 0) {
                if (src_[p] == '\'') {
                    ++p;
                    while (p < src_.size() && src_[p] != '\'') ++p;
                } else if (src_[p] == '[') {
                    ++depth;
                } else if (src_[p] == ']') {
                    --depth;
                }
                ++p;
            }
        }
        skip_at(p);
        return p < src_.size() && src_[p] == '.';
    }

    void skip_at(size_t& p) const {
        while (p < src_.size()) {
            unsigned char c = static_cast<unsigned char>(src_[p]);
            if (std::isspace(c) || c == '\\' || c == '"') {
                ++p;
            } else if (c == '#') {
                while (p < src_.size() && src_[p] != '\n') ++p;
            } else if (src_.compare(p, 2, "//") == 0) {
                p += 2;
                while (p < src_.size() && src_[p] != '\n') ++p;
            } else {
                break;
            }
        }
    }

    void skip_label_at(size_t& p) const {
        if (p < src_.size() && is_ident_start(static_cast<unsigned char>(src_[p]))) {
            ++p;
            while (p < src_.size() && is_ident_part(static_cast<unsigned char>(src_[p]))) ++p;
        }
    }

    void parse_statement() {
        skip();
        if (accept(';')) return;
        size_t statement_start = pos_;

        if (peek() == '$' && next_is_variable_assignment()) {
            flush_pending_includes();
            ++pos_;
            std::string name = ascii_lower(parse_variable_name());
            expect('=');
            size_t args_start = pos_;
            Value value = parse_expression();
            size_t args_end = pos_;
            expect(';');
            add_parsed_statement(ctx_, "Variable.Assign",
                                 make_source_span(ctx_, loaded_, statement_start, pos_, ctx_.include_stack),
                                 src_.substr(statement_start, pos_ - statement_start),
                                 trim_field_copy(src_.substr(args_start, args_end - args_start)),
                                 {value}, ctx_.distance_expression, ctx_.distance);
            ctx_.variables[name] = value;
            note_variable_write(ctx_, name);
            return;
        }

        size_t save = pos_;
        if (!eof() && is_ident_start(static_cast<unsigned char>(peek()))) {
            std::string first = parse_label();
            std::string first_l = ascii_lower(first);
            pos_ = save;
            if (first_l == "include" && !current_starts_map_element()) {
                if (!include_path_is_simple_string(save)) flush_pending_includes();
                parse_label();
                size_t args_start = pos_;
                Value path = parse_expression();
                size_t args_end = pos_;
                expect(';');
                add_parsed_statement(ctx_, "Include",
                                     make_source_span(ctx_, loaded_, statement_start, pos_, ctx_.include_stack),
                                     src_.substr(statement_start, pos_ - statement_start),
                                     trim_field_copy(src_.substr(args_start, args_end - args_start)),
                                     {path}, ctx_.distance_expression, ctx_.distance);
                queue_include(as_text(path));
                return;
            }
            if (current_starts_map_element()) {
                flush_pending_includes();
                ParsedMapElement element = parse_map_element();
                expect(';');
                size_t statement_index = add_parsed_statement(
                    ctx_, map_statement_kind(element.objects, element.function),
                    make_source_span(ctx_, loaded_, statement_start, pos_, ctx_.include_stack),
                    src_.substr(statement_start, pos_ - statement_start),
                    element.function.raw_arguments,
                    element.function.args,
                    ctx_.distance_expression,
                    ctx_.distance);
                ActiveStatementScope active(ctx_, statement_index);
                dispatch(element.objects, element.function);
                return;
            }
        }

        flush_pending_includes();
        size_t args_start = pos_;
        Value distance = parse_expression();
        size_t args_end = pos_;
        expect(';');
        std::string raw_distance = trim_field_copy(src_.substr(args_start, args_end - args_start));
        double distance_value = as_number(distance);
        add_parsed_statement(ctx_, "Distance.Set",
                             make_source_span(ctx_, loaded_, statement_start, pos_, ctx_.include_stack),
                             src_.substr(statement_start, pos_ - statement_start),
                             raw_distance,
                             {distance}, raw_distance, distance_value);
        ctx_.distance_expression = raw_distance;
        set_distance(ctx_, distance_value);
    }

    bool include_path_is_simple_string(size_t include_pos) const {
        size_t p = include_pos;
        skip_label_at(p);
        skip_at(p);
        if (p >= src_.size() || src_[p] != '\'') return false;
        ++p;
        while (p < src_.size() && src_[p] != '\'') ++p;
        if (p < src_.size()) ++p;
        skip_at(p);
        return p < src_.size() && src_[p] == ';';
    }

    MapContext make_child_seed(const std::filesystem::path& child) const {
        MapContext seed;
        seed.rootpath = ctx_.rootpath;
        seed.rootpath_utf8 = ctx_.rootpath_utf8;
        seed.entry_file_path = ctx_.entry_file_path;
        seed.current_file_path = normalized_source_path(child);
        seed.include_stack = ctx_.include_stack;
        seed.source_overrides = ctx_.source_overrides;
        seed.unit_distance = ctx_.unit_distance;
        std::string child_path = normalized_source_path(child);
        if (seed.include_stack.empty() ||
            normalized_source_key(seed.include_stack.back()) != normalized_source_key(child_path)) {
            seed.include_stack.push_back(std::move(child_path));
        }
        seed.distance = ctx_.distance;
        seed.distance_expression = ctx_.distance_expression;
        seed.variables = ctx_.variables;
        return seed;
    }

    static IncludeResult parse_include_context(MapContext seed, std::filesystem::path child) {
        IncludeResult result;
        result.context = std::move(seed);
        try {
            ActiveTimingScope active(result.context.timing);
            LoadedText loaded = load_header_text(result.context, child, "BveTs Map ", 2.0);
            result.context.current_file_path = loaded.normalized_path;
            Parser nested(result.context, std::move(loaded));
            nested.parse();
        } catch (const std::exception& e) {
            result.error = e.what();
        }
        return result;
    }

    void queue_include(const std::string& path_text) {
        std::filesystem::path child = join_path(ctx_.rootpath, path_text);
        log_info("including " + path_to_utf8(child));
        MapContext seed = make_child_seed(child);
        PendingInclude pending;
        pending.path = child;
        pending.seed_distance = seed.distance;
        pending.seed_variables = seed.variables;
        pending.future = std::async(std::launch::async,
                                    [seed = std::move(seed), child]() mutable {
                                        return parse_include_context(std::move(seed), child);
                                    });
        pending_includes_.push_back(std::move(pending));
    }

    bool include_result_is_stale(const PendingInclude& pending, const MapContext& parsed) const {
        if (parsed.depends_on_initial_distance && ctx_.distance != pending.seed_distance) {
            return true;
        }
        for (const std::string& key : parsed.external_variable_reads) {
            if (!variable_value_matches(ctx_.variables, pending.seed_variables, key)) {
                return true;
            }
        }
        return false;
    }

    void merge_include_context(MapContext& child) {
        ctx_.timing.read_decode_seconds += child.timing.read_decode_seconds;
        if (child.depends_on_initial_distance && !ctx_.has_distance_assignment) {
            ctx_.depends_on_initial_distance = true;
        }
        for (const std::string& key : child.external_variable_reads) {
            if (ctx_.variable_writes.find(key) == ctx_.variable_writes.end()) {
                ctx_.external_variable_reads.insert(key);
            }
        }
        ctx_.variable_writes.insert(child.variable_writes.begin(), child.variable_writes.end());

        std::vector<size_t> source_file_index_map = merge_source_file_records(ctx_, child);
        std::vector<size_t> include_stack_index_map = merge_include_stacks(ctx_, child);
        size_t statement_index_base = ctx_.parsed_statements.size();
        int edit_order_base = ctx_.edit_order;
        for (auto& statement : child.parsed_statements) {
            if (statement.global_order > 0) {
                statement.global_order += edit_order_base;
                statement.edit_id.clear();
            }
            if (statement.source.source_file_index < source_file_index_map.size()) {
                statement.source.source_file_index = source_file_index_map[statement.source.source_file_index];
            }
            if (statement.source.include_stack_index < include_stack_index_map.size()) {
                statement.source.include_stack_index = include_stack_index_map[statement.source.include_stack_index];
            }
        }
        ctx_.edit_order += child.edit_order;
        offset_row_edit_refs(child.own_track, statement_index_base);
        for (auto& kv : child.station_list) offset_edit_ref(kv.second.edit_ref, statement_index_base);
        for (auto& row : child.station_puts) offset_row_edit_ref(row, statement_index_base);
        for (auto& kv : child.othertrack) offset_row_edit_refs(kv.second, statement_index_base);
        offset_row_edit_refs(child.structure_loads, statement_index_base);
        offset_row_edit_refs(child.structure_models, statement_index_base);
        offset_row_edit_refs(child.sound_list, statement_index_base);
        offset_row_edit_refs(child.other_trains, statement_index_base);
        offset_row_edit_refs(child.other_train_enables, statement_index_base);
        offset_row_edit_refs(child.other_train_stops, statement_index_base);
        offset_row_edit_refs(child.other_train_structure_keys, statement_index_base);
        offset_row_edit_refs(child.other_train_sound_3d_keys, statement_index_base);
        offset_row_edit_refs(child.map_sounds, statement_index_base);
        offset_row_edit_refs(child.map_sound_3d, statement_index_base);
        offset_row_edit_refs(child.rolling_noises, statement_index_base);
        offset_row_edit_refs(child.flange_noises, statement_index_base);
        offset_row_edit_refs(child.joint_noises, statement_index_base);
        offset_row_edit_refs(child.structure_puts, statement_index_base);
        offset_row_edit_refs(child.structure_betweens, statement_index_base);
        offset_row_edit_refs(child.repeaters, statement_index_base);
        offset_row_edit_refs(child.section_begins, statement_index_base);
        offset_row_edit_refs(child.section_speed_limits, statement_index_base);
        offset_row_edit_refs(child.signal_aspects, statement_index_base);
        offset_row_edit_refs(child.signal_puts, statement_index_base);
        offset_row_edit_refs(child.beacons, statement_index_base);
        offset_row_edit_refs(child.pretrains, statement_index_base);
        offset_row_edit_refs(child.irregularities, statement_index_base);
        offset_row_edit_refs(child.backgrounds, statement_index_base);
        offset_row_edit_refs(child.adhesions, statement_index_base);
        offset_row_edit_refs(child.cab_illuminance, statement_index_base);
        offset_row_edit_refs(child.fogs, statement_index_base);
        offset_row_edit_refs(child.speedlimits, statement_index_base);

        int order_base = ctx_.parse_order;
        auto offset_order = [order_base](int& order) {
            if (order > 0) order += order_base;
        };
        for (auto& row : child.station_puts) offset_order(row.order);
        for (auto& row : child.structure_loads) offset_order(row.order);
        for (auto& row : child.structure_puts) offset_order(row.order);
        for (auto& row : child.structure_betweens) offset_order(row.order);
        for (auto& row : child.other_trains) offset_order(row.order);
        for (auto& row : child.other_train_enables) offset_order(row.order);
        for (auto& row : child.other_train_stops) offset_order(row.order);
        for (auto& row : child.repeaters) offset_order(row.order);
        for (auto& row : child.section_begins) offset_order(row.order);
        for (auto& row : child.section_speed_limits) offset_order(row.order);
        for (auto& row : child.signal_puts) offset_order(row.order);
        for (auto& row : child.beacons) offset_order(row.order);
        for (auto& row : child.pretrains) offset_order(row.order);
        for (auto& row : child.map_sounds) offset_order(row.order);
        for (auto& row : child.map_sound_3d) offset_order(row.order);
        for (auto& row : child.rolling_noises) offset_order(row.order);
        for (auto& row : child.flange_noises) offset_order(row.order);
        for (auto& row : child.joint_noises) offset_order(row.order);
        for (auto& row : child.irregularities) offset_order(row.order);
        for (auto& row : child.backgrounds) offset_order(row.order);
        for (auto& row : child.adhesions) offset_order(row.order);
        for (auto& row : child.cab_illuminance) offset_order(row.order);
        for (auto& row : child.fogs) offset_order(row.order);
        for (auto& row : child.speedlimits) offset_order(row.order);
        ctx_.parse_order += child.parse_order;

        if (child.has_distance_assignment) {
            ctx_.distance = child.distance;
            ctx_.distance_expression = child.distance_expression;
            ctx_.has_distance_assignment = true;
        }
        for (const std::string& key : child.variable_writes) {
            auto it = child.variables.find(key);
            if (it != child.variables.end()) {
                ctx_.variables[key] = std::move(it->second);
            }
        }

        for (auto& statement : child.parsed_statements) {
            ctx_.parsed_statements.push_back(std::move(statement));
        }
        ctx_.controlpoints.insert(ctx_.controlpoints.end(), child.controlpoints.begin(), child.controlpoints.end());
        for (auto& row : child.own_track) ctx_.own_track.push_back(std::move(row));
        for (auto& kv : child.station_position) ctx_.station_position[kv.first] = std::move(kv.second);
        for (auto& kv : child.station_key) ctx_.station_key[kv.first] = std::move(kv.second);
        for (auto& kv : child.station_list) ctx_.station_list[kv.first] = std::move(kv.second);
        for (auto& row : child.station_puts) ctx_.station_puts.push_back(std::move(row));
        for (const std::string& key : child.othertrack_order) {
            ensure_othertrack(ctx_, key);
            auto& dest = ctx_.othertrack[key];
            auto& src = child.othertrack[key];
            for (auto& row : src) dest.push_back(std::move(row));
        }
        for (auto& row : child.structure_loads) ctx_.structure_loads.push_back(std::move(row));
        for (auto& row : child.structure_models) ctx_.structure_models.push_back(std::move(row));
        for (auto& row : child.sound_list) ctx_.sound_list.push_back(std::move(row));
        for (auto& row : child.other_trains) ctx_.other_trains.push_back(std::move(row));
        for (auto& row : child.other_train_enables) ctx_.other_train_enables.push_back(std::move(row));
        for (auto& row : child.other_train_stops) ctx_.other_train_stops.push_back(std::move(row));
        for (auto& row : child.other_train_structure_keys) ctx_.other_train_structure_keys.push_back(std::move(row));
        for (auto& row : child.other_train_sound_3d_keys) ctx_.other_train_sound_3d_keys.push_back(std::move(row));
        ctx_.parsed_other_train_files.insert(child.parsed_other_train_files.begin(), child.parsed_other_train_files.end());
        for (auto& row : child.map_sounds) ctx_.map_sounds.push_back(std::move(row));
        for (auto& row : child.map_sound_3d) ctx_.map_sound_3d.push_back(std::move(row));
        for (auto& row : child.rolling_noises) ctx_.rolling_noises.push_back(std::move(row));
        for (auto& row : child.flange_noises) ctx_.flange_noises.push_back(std::move(row));
        for (auto& row : child.joint_noises) ctx_.joint_noises.push_back(std::move(row));
        for (auto& row : child.structure_puts) ctx_.structure_puts.push_back(std::move(row));
        for (auto& row : child.structure_betweens) ctx_.structure_betweens.push_back(std::move(row));
        for (auto& row : child.repeaters) ctx_.repeaters.push_back(std::move(row));
        for (auto& row : child.section_begins) ctx_.section_begins.push_back(std::move(row));
        for (auto& row : child.section_speed_limits) ctx_.section_speed_limits.push_back(std::move(row));
        for (auto& row : child.signal_aspects) ctx_.signal_aspects.push_back(std::move(row));
        for (auto& row : child.signal_puts) ctx_.signal_puts.push_back(std::move(row));
        for (auto& row : child.beacons) ctx_.beacons.push_back(std::move(row));
        for (auto& row : child.pretrains) ctx_.pretrains.push_back(std::move(row));
        for (auto& row : child.irregularities) ctx_.irregularities.push_back(std::move(row));
        for (auto& row : child.backgrounds) ctx_.backgrounds.push_back(std::move(row));
        for (auto& row : child.adhesions) ctx_.adhesions.push_back(std::move(row));
        for (auto& row : child.cab_illuminance) ctx_.cab_illuminance.push_back(std::move(row));
        for (auto& row : child.fogs) ctx_.fogs.push_back(std::move(row));
        for (auto& row : child.speedlimits) ctx_.speedlimits.push_back(std::move(row));
    }

    void flush_pending_includes() {
        if (pending_includes_.empty()) return;

        for (auto& pending : pending_includes_) {
            IncludeResult result;
            try {
                result = pending.future.get();
            } catch (const std::exception& e) {
                result.error = e.what();
            }
            if (!result.error.empty()) {
                result = parse_include_context(make_child_seed(pending.path), pending.path);
                if (!result.error.empty()) {
                    log_warn(result.error);
                    continue;
                }
            } else if (include_result_is_stale(pending, result.context)) {
                result = parse_include_context(make_child_seed(pending.path), pending.path);
                if (!result.error.empty()) {
                    log_warn(result.error);
                    continue;
                }
            }
            merge_include_context(result.context);
        }
        pending_includes_.clear();
    }

    std::string map_statement_kind(const std::vector<MapObject>& objects,
                                   const MapFunction& function) const {
        std::string kind;
        for (const MapObject& object : objects) {
            if (!kind.empty()) kind += ".";
            kind += object.label;
        }
        if (!kind.empty()) kind += ".";
        kind += function.label;
        return kind;
    }

    ParsedMapElement parse_map_element() {
        ParsedMapElement element;
        do {
            element.objects.push_back(parse_map_object());
            expect('.');
        } while (!next_is_function());
        element.function = parse_map_function();
        return element;
    }

    bool next_is_function() {
        size_t p = pos_;
        skip_at(p);
        if (p >= src_.size() || !is_ident_start(static_cast<unsigned char>(src_[p]))) return false;
        skip_label_at(p);
        skip_at(p);
        return p < src_.size() && src_[p] == '(';
    }

    MapObject parse_map_object() {
        MapObject object;
        object.label = parse_label();
        if (accept('[')) {
            object.has_key = true;
            object.key = parse_expression();
            expect(']');
        }
        return object;
    }

    MapFunction parse_map_function() {
        MapFunction function;
        function.label = parse_label();
        expect('(');
        size_t args_start = pos_;
        parse_map_args(function.args);
        size_t args_end = pos_ > args_start ? pos_ - 1 : args_start;
        function.raw_arguments = src_.substr(args_start, args_end - args_start);
        return function;
    }

    void parse_map_args(std::vector<Value>& args) {
        skip();
        if (accept(')')) {
            args.push_back(Value::null());
            return;
        }
        while (true) {
            skip();
            if (peek() == ',' || peek() == ')') {
                args.push_back(Value::null());
            } else {
                args.push_back(parse_expression());
            }
            skip();
            if (accept(',')) continue;
            expect(')');
            break;
        }
    }

    Value parse_expression(int min_prec = 0) {
        Value lhs = parse_prefix();
        while (true) {
            skip();
            char op = peek();
            int prec = precedence(op);
            if (prec < min_prec) break;
            ++pos_;
            Value rhs = parse_expression(prec + 1);
            lhs = apply_binary(op, lhs, rhs);
        }
        return lhs;
    }

    int precedence(char op) const {
        if (op == '+' || op == '-') return 10;
        if (op == '*' || op == '/' || op == '%') return 20;
        return -1;
    }

    Value parse_prefix() {
        skip();
        if (accept('+')) return parse_prefix();
        if (accept('-')) {
            Value v = parse_prefix();
            return Value::num(-as_number(v));
        }
        return parse_primary();
    }

    Value parse_primary() {
        skip();
        if (accept('(')) {
            Value v = parse_expression();
            expect(')');
            return v;
        }
        if (peek() == '\'') return parse_string();
        if (peek() == '$') {
            ++pos_;
            std::string name = ascii_lower(parse_variable_name());
            note_variable_read(ctx_, name);
            auto it = ctx_.variables.find(name);
            if (it == ctx_.variables.end()) throw std::runtime_error("Undefined variable: " + name);
            return it->second;
        }
        if (std::isdigit(static_cast<unsigned char>(peek())) || peek() == '.') {
            return parse_number();
        }
        if (is_ident_start(static_cast<unsigned char>(peek()))) {
            std::string label = parse_label();
            std::string lower = ascii_lower(label);
            skip();
            if (accept('(')) {
                std::vector<Value> args;
                skip();
                if (!accept(')')) {
                    while (true) {
                        args.push_back(parse_expression());
                        if (accept(',')) continue;
                        expect(')');
                        break;
                    }
                }
                return call_function(lower, args);
            }
            if (lower == "null") return Value::null();
            if (lower == "distance") {
                note_distance_use(ctx_);
                return Value::num(ctx_.distance);
            }
            throw std::runtime_error("Unknown predefined variable: " + label);
        }
        throw std::runtime_error("Expected expression at byte " + std::to_string(pos_));
    }

    bool next_is_variable_assignment() const {
        size_t p = pos_;
        skip_at(p);
        if (p >= src_.size() || src_[p] != '$') return false;
        ++p;
        skip_at(p);
        if (p >= src_.size() || !is_ident_part(static_cast<unsigned char>(src_[p]))) return false;
        ++p;
        while (p < src_.size() && is_ident_part(static_cast<unsigned char>(src_[p]))) ++p;
        skip_at(p);
        return p < src_.size() && src_[p] == '=';
    }

    Value parse_string() {
        expect('\'');
        size_t start = pos_;
        while (!eof() && peek() != '\'') ++pos_;
        std::string s = src_.substr(start, pos_ - start);
        expect('\'');
        return Value::str(s);
    }

    Value parse_number() {
        skip();
        const char* begin = src_.c_str() + pos_;
        char* end = nullptr;
        errno = 0;
        double value = std::strtod(begin, &end);
        if (end == begin || errno == ERANGE) {
            throw std::runtime_error("Invalid number at byte " + std::to_string(pos_));
        }
        pos_ += static_cast<size_t>(end - begin);
        return Value::num(value);
    }

    Value apply_binary(char op, const Value& lhs, const Value& rhs) {
        if (op == '+') {
            if ((lhs.is_string() && rhs.is_string()) || (!lhs.is_string() && !rhs.is_string())) {
                if (lhs.is_string()) return Value::str(lhs.text + rhs.text);
                return Value::num(as_number(lhs) + as_number(rhs));
            }
            if (lhs.is_string()) {
                return Value::str(lhs.text + std::to_string(static_cast<long long>(as_number(rhs))));
            }
            return Value::str(std::to_string(static_cast<long long>(as_number(lhs))) + rhs.text);
        }
        if (op == '-') return Value::num(as_number(lhs) - as_number(rhs));
        if (op == '*') return Value::num(as_number(lhs) * as_number(rhs));
        if (op == '/') {
            double a = as_number(lhs);
            double b = as_number(rhs);
            return Value::num(b != 0.0 ? a / b : std::copysign(kInf, a));
        }
        if (op == '%') return Value::num(std::fmod(as_number(lhs), as_number(rhs)));
        throw std::runtime_error("Unknown operator");
    }

    Value call_function(const std::string& label, const std::vector<Value>& args) {
        static thread_local std::mt19937 rng{std::random_device{}()};
        if (label == "rand") {
            if (args.empty()) {
                return Value::num(std::uniform_real_distribution<double>(0.0, 1.0)(rng));
            }
            return Value::num(std::uniform_real_distribution<double>(0.0, as_number(args[0]))(rng));
        }
        if (label == "abs") return Value::num(std::fabs(as_number(args.at(0))));
        if (label == "sin") return Value::num(std::sin(as_number(args.at(0))));
        if (label == "cos") return Value::num(std::cos(as_number(args.at(0))));
        if (label == "atan2") return Value::num(std::atan2(as_number(args.at(0)), as_number(args.at(1))));
        if (label == "sqrt") return Value::num(std::sqrt(as_number(args.at(0))));
        if (label == "exp") return Value::num(std::exp(as_number(args.at(0))));
        if (label == "log") return Value::num(std::log(as_number(args.at(0))));
        if (label == "floor") return Value::num(std::floor(as_number(args.at(0))));
        if (label == "ceil") return Value::num(std::ceil(as_number(args.at(0))));
        if (label == "pow") return Value::num(std::pow(as_number(args.at(0)), as_number(args.at(1))));
        throw std::runtime_error("Unknown function: " + label);
    }

    void dispatch(const std::vector<MapObject>& objects, const MapFunction& function) {
        std::string first = ascii_lower(objects.front().label);
        std::vector<std::string> labels;
        labels.reserve(objects.size());
        for (const auto& object : objects) labels.push_back(ascii_lower(object.label));
        std::string fn = ascii_lower(function.label);

        if (first == "curve") {
            dispatch_curve(fn, function.args);
        } else if (first == "gradient") {
            dispatch_gradient(fn, function.args);
        } else if (first == "legacy") {
            dispatch_legacy(fn, function.args);
        } else if (first == "station") {
            std::vector<Value> args = function.args;
            if (objects.front().has_key) args.insert(args.begin(), objects.front().key);
            dispatch_station(fn, args);
        } else if (first == "track") {
            if (!objects.front().has_key) throw std::runtime_error("Track key is required");
            dispatch_track(objects.front().key, labels, fn, function.args);
        } else if (first == "speedlimit") {
            dispatch_speedlimit(fn, function.args);
        } else if (first == "section") {
            dispatch_section(fn, function.args);
        } else if (first == "signal") {
            std::vector<Value> args = function.args;
            if (objects.front().has_key) args.insert(args.begin(), objects.front().key);
            dispatch_signal(fn, args, objects.front().has_key);
        } else if (first == "structure") {
            std::vector<Value> args = function.args;
            if (objects.front().has_key) args.insert(args.begin(), objects.front().key);
            dispatch_structure(fn, args);
        } else if (first == "beacon") {
            dispatch_beacon(fn, function.args);
        } else if (first == "pretrain") {
            dispatch_pretrain(fn, function.args);
        } else if (first == "sound" || first == "sound3d") {
            dispatch_sound(fn, function.args, first == "sound3d",
                           objects.front().has_key, objects.front().key);
        } else if (first == "train") {
            std::vector<Value> args = function.args;
            if (objects.front().has_key) args.insert(args.begin(), objects.front().key);
            dispatch_train(fn, args, objects.front().has_key);
        } else if (first == "rollingnoise") {
            dispatch_rolling_noise(fn, function.args);
        } else if (first == "flangenoise") {
            dispatch_flange_noise(fn, function.args);
        } else if (first == "jointnoise") {
            dispatch_joint_noise(fn, function.args);
        } else if (first == "repeater") {
            std::vector<Value> args = function.args;
            if (objects.front().has_key) args.insert(args.begin(), objects.front().key);
            dispatch_repeater(fn, args);
        } else if (first == "irregularity") {
            dispatch_irregularity(fn, function.args);
        } else if (first == "background") {
            dispatch_background(fn, function.args);
        } else if (first == "adhesion") {
            dispatch_adhesion(fn, function.args);
        } else if (first == "cabilluminance") {
            dispatch_cab_illuminance(fn, function.args);
        } else if (first == "fog") {
            dispatch_fog(fn, function.args);
        }
    }

    void dispatch_curve(const std::string& fn, const std::vector<Value>& a) {
        if (fn == "setgauge" || fn == "gauge") put_own(ctx_, "gauge", arg_or_null(a));
        else if (fn == "setcenter") put_own(ctx_, "center", arg_or_null(a));
        else if (fn == "setfunction") put_own(ctx_, "interpolate_func", Value::str(as_number(arg_or_null(a)) == 0.0 ? "sin" : "line"));
        else if (fn == "begintransition") {
            put_own(ctx_, "radius", Value::null(), "bt");
            put_own(ctx_, "cant", Value::null(), "bt");
        } else if (fn == "begincircular" || fn == "begin" || fn == "change") {
            put_own(ctx_, "radius", arg_or_null(a));
            put_own(ctx_, "cant", a.size() > 1 ? a.at(1) : Value::num(0.0));
        } else if (fn == "end") {
            put_own(ctx_, "radius", Value::num(0.0));
            put_own(ctx_, "cant", Value::num(0.0));
        } else if (fn == "interpolate") {
            put_own(ctx_, "radius", arg_or_null(a), "i");
            put_own(ctx_, "cant", a.size() > 1 ? a.at(1) : Value::null(), "i");
        }
    }

    void dispatch_gradient(const std::string& fn, const std::vector<Value>& a) {
        if (fn == "begintransition") put_own(ctx_, "gradient", Value::null(), "bt");
        else if (fn == "begin" || fn == "beginconst") put_own(ctx_, "gradient", arg_or_null(a));
        else if (fn == "end") put_own(ctx_, "gradient", Value::num(0.0));
        else if (fn == "interpolate") put_own(ctx_, "gradient", arg_or_null(a), "i");
    }

    void dispatch_legacy(const std::string& fn, const std::vector<Value>& a) {
        if (fn == "turn") put_own(ctx_, "turn", arg_or_null(a));
        else if (fn == "curve") {
            put_own(ctx_, "radius", arg_or_null(a));
            put_own(ctx_, "cant", a.size() > 1 ? a.at(1) : Value::num(0.0));
        } else if (fn == "pitch") {
            put_own(ctx_, "gradient", arg_or_null(a));
        }
    }

    void dispatch_station(const std::string& fn, const std::vector<Value>& a) {
        if (fn == "put" && !a.empty()) {
            note_distance_use(ctx_);
            std::string key = key_text(a.at(0));
            ctx_.station_position[ctx_.distance] = key;
            StationPut row;
            row.distance = ctx_.distance;
            row.station_key = Value::str(key);
            row.door = arg_or_null(a, 1);
            row.margin1 = arg_or_null(a, 2);
            row.margin2 = arg_or_null(a, 3);
            row.file_path = ctx_.current_file_path;
            row.order = ctx_.next_parse_order();
            attach_active_edit_ref(ctx_, row);
            ctx_.station_puts.push_back(std::move(row));
        } else if (fn == "load" && !a.empty()) {
            std::filesystem::path path = join_path(ctx_.rootpath, as_text(a.at(0)));
            LoadedText loaded = load_header_text(ctx_, path, "BveTs Station List ", 0.04);
            parse_station_list(loaded);
        }
    }

    void parse_station_list(const LoadedText& loaded) {
        register_source_file(ctx_, loaded);
        std::vector<std::string> stack = include_stack_for_file(ctx_, loaded.path);
        for_each_loaded_body_line(loaded, [&](const std::string& line, size_t line_start,
                                              size_t line_end, int) {
            std::vector<std::string> fields = parse_comma_separated_fields(line, true);
            if (fields.empty() || fields[0].empty()) return;

            StationListEntry row;
            for (size_t i = 0; i < row.fields.size() && i < fields.size(); ++i) row.fields[i] = fields[i];
            row.edit_ref = add_loaded_line_statement(ctx_, loaded, stack, "StationList.Row",
                                                     line_start, line_end, line, fields);
            std::string key = ascii_lower(row.fields[0]);
            ctx_.station_key[key] = row.fields[1];
            ctx_.station_list[key] = std::move(row);
        });
    }

    void parse_structure_list(const LoadedText& loaded) {
        register_source_file(ctx_, loaded);
        std::vector<std::string> stack = include_stack_for_file(ctx_, loaded.path);
        for_each_loaded_body_line(loaded, [&](const std::string& line, size_t line_start,
                                              size_t line_end, int) {
            std::string trimmed = trim_field_copy(line);
            if (trimmed.empty() || trimmed[0] == '#') return;

            std::vector<std::string> fields = parse_comma_separated_fields(line, true);
            if (fields.empty() || fields[0].empty()) return;

            StructureModel row;
            row.structure_key = fields[0];
            if (fields.size() > 1 && !fields[1].empty()) {
                std::filesystem::path model_path = join_path(loaded.root, fields[1]);
                std::error_code ec;
                std::filesystem::path abs = std::filesystem::absolute(model_path, ec);
                if (!ec) model_path = abs;
                row.file_path = path_to_utf8(model_path.lexically_normal());
            }
            row.edit_ref = add_loaded_line_statement(ctx_, loaded, stack, "StructureList.Row",
                                                     line_start, line_end, line, fields);
            ctx_.structure_models.push_back(std::move(row));
        });
    }

    void parse_signal_aspect_list(const LoadedText& loaded) {
        register_source_file(ctx_, loaded);
        std::vector<std::string> stack = include_stack_for_file(ctx_, loaded.path);
        SignalAspect* current_aspect = nullptr;
        for_each_loaded_body_line(loaded, [&](const std::string& line, size_t line_start,
                                              size_t line_end, int) {
            std::string trimmed = trim_field_copy(line);
            if (trimmed.empty() || trimmed[0] == '#') return;

            std::vector<std::string> fields = parse_comma_separated_fields(line, true);
            trim_trailing_empty_fields(fields);
            if (fields.empty()) return;
            const bool starts_glare_row = fields[0].empty();
            if (starts_glare_row && !current_aspect) return;

            if (!starts_glare_row) {
                SignalAspect row;
                row.signal_aspect_key = fields[0];
                row.edit_ref = add_loaded_line_statement(ctx_, loaded, stack, "SignalList.Row",
                                                         line_start, line_end, line, fields);
                ctx_.signal_aspects.push_back(std::move(row));
                current_aspect = &ctx_.signal_aspects.back();
            } else if (current_aspect && !current_aspect->edit_ref.valid()) {
                current_aspect->edit_ref = add_loaded_line_statement(ctx_, loaded, stack, "SignalList.Row",
                                                                     line_start, line_end, line, fields);
            }
            for (size_t i = 1; i < fields.size(); ++i) {
                current_aspect->structure_keys.push_back(fields[i]);
            }
        });
    }

    void parse_sound_list(const LoadedText& loaded, bool is_3d) {
        register_source_file(ctx_, loaded);
        std::vector<std::string> stack = include_stack_for_file(ctx_, loaded.path);
        for_each_loaded_body_line(loaded, [&](const std::string& line, size_t line_start,
                                              size_t line_end, int) {
            std::string trimmed = trim_field_copy(line);
            if (trimmed.empty() || trimmed[0] == '#') return;

            std::vector<std::string> fields = parse_comma_separated_fields(line, true);
            if (fields.empty() || fields[0].empty()) return;

            SoundListEntry row;
            row.sound_key = fields[0];
            row.is_3d = is_3d;
            if (fields.size() > 1 && !fields[1].empty()) {
                std::filesystem::path sound_path = join_path(loaded.root, fields[1]);
                std::error_code ec;
                std::filesystem::path abs = std::filesystem::absolute(sound_path, ec);
                if (!ec) sound_path = abs;
                row.file_path = path_to_utf8(sound_path.lexically_normal());
            }
            if (fields.size() > 2) {
                row.buffer_count = parse_sound_buffer_count(fields[2]);
            }
            row.edit_ref = add_loaded_line_statement(ctx_, loaded, stack,
                                                     is_3d ? "Sound3DList.Row" : "SoundList.Row",
                                                     line_start, line_end, line, fields);
            ctx_.sound_list.push_back(std::move(row));
        });
    }

    void parse_other_train_file(const std::filesystem::path& path) {
        std::error_code ec;
        std::filesystem::path abs = std::filesystem::absolute(path, ec);
        if (ec) abs = path;
        abs = abs.lexically_normal();
        std::string path_key = path_to_utf8(abs);
        if (!ctx_.parsed_other_train_files.insert(path_key).second) return;

        LoadedText loaded = load_header_text(ctx_, abs, "BveTs Train ", 0.0);
        register_source_file(ctx_, loaded);
        std::vector<std::string> stack = include_stack_for_file(ctx_, loaded.path);
        std::string section;
        for_each_loaded_body_line(loaded, [&](const std::string& line, size_t line_start,
                                              size_t line_end, int) {
            std::string trimmed = trim_field_copy(strip_ini_comment_copy(line));
            if (trimmed.empty()) return;
            if (trimmed.front() == '[' && trimmed.back() == ']' && trimmed.size() >= 2) {
                section = ascii_lower(trim_field_copy(trimmed.substr(1, trimmed.size() - 2)));
                return;
            }

            size_t eq = trimmed.find('=');
            if (eq == std::string::npos) return;
            std::string key = ascii_lower(trim_field_copy(trimmed.substr(0, eq)));
            if (key != "key") return;

            std::string value = trim_matching_quotes(trimmed.substr(eq + 1));
            if (value.empty()) return;
            std::vector<std::string> fields{key, value};
            EditSourceRef ref = add_loaded_line_statement(ctx_, loaded, stack, "OtherTrainFile.Row",
                                                          line_start, line_end, line, fields);
            if (section == "structure") {
                ctx_.other_train_structure_keys.push_back({value, path_key, ref});
            } else if (section == "sound3d") {
                ctx_.other_train_sound_3d_keys.push_back({value, path_key, ref});
            }
        });
    }

    void dispatch_track(const Value& track_key, const std::vector<std::string>& labels,
                        const std::string& fn, const std::vector<Value>& a) {
        if (labels.size() >= 2 && labels[1] == "cant" && fn == "cant") {
            return;
        }
        if (labels.size() == 1 && fn == "position") {
            track_position(track_key, a);
            return;
        }
        if (labels.size() == 1 && fn == "cant") {
            put_other(ctx_, track_key, "cant", arg_or_null(a), "i");
            return;
        }
        if (labels.size() == 1 && fn == "gauge") {
            put_other(ctx_, track_key, "gauge", arg_or_null(a));
            return;
        }
        if (labels.size() >= 2 && (labels[1] == "x" || labels[1] == "y") && fn == "interpolate") {
            setposition_interpolate(track_key, labels[1], a);
            return;
        }
        if (labels.size() >= 2 && labels[1] == "cant") {
            if (fn == "setgauge") put_other(ctx_, track_key, "gauge", arg_or_null(a));
            else if (fn == "setcenter") put_other(ctx_, track_key, "center", arg_or_null(a));
            else if (fn == "setfunction") put_other(ctx_, track_key, "interpolate_func", Value::str(as_number(arg_or_null(a)) == 0.0 ? "sin" : "line"));
            else if (fn == "begintransition") put_other(ctx_, track_key, "cant", Value::null(), "bt");
            else if (fn == "begin") put_other(ctx_, track_key, "cant", arg_or_null(a), "i");
            else if (fn == "end") put_other(ctx_, track_key, "cant", Value::num(0.0), "i");
            else if (fn == "interpolate" || fn == "cant") put_other(ctx_, track_key, "cant", arg_or_null(a), "i");
        }
    }

    void setposition_interpolate(const Value& track_key, const std::string& dim, const std::vector<Value>& a) {
        if (a.empty()) {
            put_other(ctx_, track_key, dim + ".position", Value::null());
            put_other(ctx_, track_key, dim + ".radius", Value::null());
        } else if (a.size() == 1) {
            put_other(ctx_, track_key, dim + ".position", a.at(0));
            put_other(ctx_, track_key, dim + ".radius", Value::null());
        } else {
            put_other(ctx_, track_key, dim + ".position", a.at(0));
            put_other(ctx_, track_key, dim + ".radius", a.at(1));
        }
    }

    void track_position(const Value& track_key, const std::vector<Value>& a) {
        if (a.size() == 2) {
            setposition_interpolate(track_key, "x", {a[0].is_null() ? Value::num(0.0) : a[0], Value::num(0.0)});
            setposition_interpolate(track_key, "y", {a[1].is_null() ? Value::num(0.0) : a[1], Value::num(0.0)});
        } else if (a.size() == 3) {
            setposition_interpolate(track_key, "x", {a[0].is_null() ? Value::num(0.0) : a[0], a[2].is_null() ? Value::num(0.0) : a[2]});
            setposition_interpolate(track_key, "y", {a[1].is_null() ? Value::num(0.0) : a[1], Value::num(0.0)});
        } else if (a.size() >= 4) {
            setposition_interpolate(track_key, "x", {a[0].is_null() ? Value::num(0.0) : a[0], a[2].is_null() ? Value::num(0.0) : a[2]});
            setposition_interpolate(track_key, "y", {a[1].is_null() ? Value::num(0.0) : a[1], a[3].is_null() ? Value::num(0.0) : a[3]});
        }
    }

    void dispatch_speedlimit(const std::string& fn, const std::vector<Value>& a) {
        if (fn == "begin") {
            note_distance_use(ctx_);
            SpeedLimitEvent row;
            row.distance = ctx_.distance;
            row.speed = a.empty() ? Value::num(0.0) : a[0];
            row.file_path = ctx_.current_file_path;
            row.order = ctx_.next_parse_order();
            attach_active_edit_ref(ctx_, row);
            ctx_.speedlimits.push_back(std::move(row));
        } else if (fn == "end") {
            note_distance_use(ctx_);
            SpeedLimitEvent row;
            row.distance = ctx_.distance;
            row.speed = Value::null();
            row.file_path = ctx_.current_file_path;
            row.order = ctx_.next_parse_order();
            attach_active_edit_ref(ctx_, row);
            ctx_.speedlimits.push_back(std::move(row));
        }
    }

    void dispatch_section(const std::string& fn, const std::vector<Value>& a) {
        if (fn == "begin" || fn == "beginnew") {
            note_distance_use(ctx_);
            SectionBegin row;
            row.distance = ctx_.distance;
            row.signal_indices = a;
            row.file_path = ctx_.current_file_path;
            row.order = ctx_.next_parse_order();
            attach_active_edit_ref(ctx_, row);
            ctx_.section_begins.push_back(std::move(row));
        } else if (fn == "setspeedlimit") {
            note_distance_use(ctx_);
            SectionSpeedLimit row;
            row.distance = ctx_.distance;
            row.speeds = a;
            row.file_path = ctx_.current_file_path;
            row.order = ctx_.next_parse_order();
            attach_active_edit_ref(ctx_, row);
            ctx_.section_speed_limits.push_back(std::move(row));
        }
    }

    void dispatch_signal(const std::string& fn, const std::vector<Value>& a, bool has_signal_key) {
        if (fn == "load" && !has_signal_key && !a.empty()) {
            std::string list_path_text = as_text(a.at(0));
            if (list_path_text.empty()) return;
            try {
                std::filesystem::path path = join_path(ctx_.rootpath, list_path_text);
                LoadedText loaded = load_header_text(ctx_, path, "BveTs Signal Aspects List ", 2.0);
                parse_signal_aspect_list(loaded);
            } catch (const std::exception& e) {
                log_warn(e.what());
            }
        } else if (fn == "speedlimit" && !has_signal_key) {
            note_distance_use(ctx_);
            SectionSpeedLimit row;
            row.distance = ctx_.distance;
            row.speeds = a;
            row.file_path = ctx_.current_file_path;
            row.order = ctx_.next_parse_order();
            attach_active_edit_ref(ctx_, row);
            ctx_.section_speed_limits.push_back(std::move(row));
        } else if (fn == "put" && has_signal_key && a.size() >= 4) {
            note_distance_use(ctx_);
            SignalPut row;
            row.distance = ctx_.distance;
            row.signal_aspect_key = a[0];
            row.section = a[1];
            row.track_key = a[2];
            row.x = as_number(a[3]);
            row.y = a.size() > 4 ? as_number(a[4]) : 0.0;
            if (a.size() >= 11) {
                row.z = as_number(a[5]);
                row.rx = as_number(a[6]);
                row.ry = as_number(a[7]);
                row.rz = as_number(a[8]);
                row.tilt = as_number(a[9]);
                row.span = as_number(a[10]);
            }
            row.file_path = ctx_.current_file_path;
            row.order = ctx_.next_parse_order();
            attach_active_edit_ref(ctx_, row);
            ctx_.signal_puts.push_back(std::move(row));
        }
    }

    void dispatch_beacon(const std::string& fn, const std::vector<Value>& a) {
        if (fn != "put" || a.size() < 3) return;
        note_distance_use(ctx_);
        BeaconPut row;
        row.distance = ctx_.distance;
        row.type = a[0];
        row.section = a[1];
        row.send_data = a[2];
        row.file_path = ctx_.current_file_path;
        row.order = ctx_.next_parse_order();
        attach_active_edit_ref(ctx_, row);
        ctx_.beacons.push_back(std::move(row));
    }

    void dispatch_pretrain(const std::string& fn, const std::vector<Value>& a) {
        if (fn != "pass" || a.empty()) return;
        note_distance_use(ctx_);
        PreTrainPass row;
        row.distance = ctx_.distance;
        row.pass_time = a[0];
        row.file_path = ctx_.current_file_path;
        row.order = ctx_.next_parse_order();
        attach_active_edit_ref(ctx_, row);
        ctx_.pretrains.push_back(std::move(row));
    }

    void dispatch_structure(const std::string& fn, const std::vector<Value>& a) {
        if (fn == "load") {
            note_distance_use(ctx_);
            StructureLoad row;
            row.distance = ctx_.distance;
            row.method = "Load";
            row.load_file_path = a.empty() ? Value::str("") : a[0];
            row.file_path = ctx_.current_file_path;
            row.order = ctx_.next_parse_order();
            attach_active_edit_ref(ctx_, row);
            ctx_.structure_loads.push_back(row);
            std::string list_path_text = as_text(row.load_file_path);
            if (!list_path_text.empty()) {
                try {
                    std::filesystem::path path = join_path(ctx_.rootpath, list_path_text);
                    LoadedText loaded = load_header_text(ctx_, path, "BveTs Structure List ", 1.0);
                    parse_structure_list(loaded);
                } catch (const std::exception& e) {
                    log_warn(e.what());
                }
            }
        } else if (fn == "put" && a.size() >= 10) {
            note_distance_use(ctx_);
            StructurePut row;
            row.distance = ctx_.distance;
            row.method = "Put";
            row.structure_key = a[0];
            row.track_key = a[1];
            row.x = as_number(a[2]); row.y = as_number(a[3]); row.z = as_number(a[4]);
            row.rx = as_number(a[5]); row.ry = as_number(a[6]); row.rz = as_number(a[7]);
            row.tilt = as_number(a[8]); row.span = as_number(a[9]);
            row.file_path = ctx_.current_file_path;
            row.order = ctx_.next_parse_order();
            attach_active_edit_ref(ctx_, row);
            ctx_.structure_puts.push_back(row);
        } else if (fn == "put0" && a.size() >= 4) {
            note_distance_use(ctx_);
            StructurePut row;
            row.distance = ctx_.distance;
            row.method = "Put0";
            row.structure_key = a[0];
            row.track_key = a[1];
            row.tilt = as_number(a[2]); row.span = as_number(a[3]);
            row.file_path = ctx_.current_file_path;
            row.order = ctx_.next_parse_order();
            attach_active_edit_ref(ctx_, row);
            ctx_.structure_puts.push_back(row);
        } else if (fn == "putbetween" && a.size() >= 3) {
            note_distance_use(ctx_);
            StructurePut row;
            row.distance = ctx_.distance;
            row.method = "PutBetween";
            row.structure_key = a[0];
            row.track_key1 = a[1];
            row.track_key2 = a[2];
            row.flag = a.size() > 3 ? as_number(a[3]) : 0.0;
            row.file_path = ctx_.current_file_path;
            row.order = ctx_.next_parse_order();
            attach_active_edit_ref(ctx_, row);
            ctx_.structure_betweens.push_back(row);
        }
    }

    void dispatch_sound(const std::string& fn, const std::vector<Value>& a,
                        bool is_3d, bool has_key, const Value& sound_key) {
        if (fn == "load" && !has_key && !a.empty()) {
            std::string list_path_text = as_text(a.at(0));
            if (list_path_text.empty()) return;
            try {
                std::filesystem::path path = join_path(ctx_.rootpath, list_path_text);
                LoadedText loaded = load_header_text(ctx_, path, "BveTs Sound List ", 2.0);
                parse_sound_list(loaded, is_3d);
            } catch (const std::exception& e) {
                log_warn(e.what());
            }
        } else if (!is_3d && fn == "play" && has_key) {
            note_distance_use(ctx_);
            MapSoundPlay row;
            row.distance = ctx_.distance;
            row.sound_key = sound_key;
            row.file_path = ctx_.current_file_path;
            row.order = ctx_.next_parse_order();
            attach_active_edit_ref(ctx_, row);
            ctx_.map_sounds.push_back(std::move(row));
        } else if (is_3d && fn == "put" && has_key && a.size() >= 2) {
            note_distance_use(ctx_);
            MapSound3DPut row;
            row.distance = ctx_.distance;
            row.sound_key = sound_key;
            row.x = as_number(a[0]);
            row.y = as_number(a[1]);
            row.file_path = ctx_.current_file_path;
            row.order = ctx_.next_parse_order();
            attach_active_edit_ref(ctx_, row);
            ctx_.map_sound_3d.push_back(std::move(row));
        }
    }

    void add_other_train_definition(const std::string& method,
                                    const Value& train_key,
                                    const Value& file_path,
                                    const Value& track_key,
                                    const Value& direction) {
        note_distance_use(ctx_);
        OtherTrainDefinition row;
        row.distance = ctx_.distance;
        row.method = method;
        row.train_key = train_key;
        row.load_file_path = file_path;
        row.track_key = track_key;
        row.direction = direction;
        row.source_file_path = ctx_.current_file_path;
        row.order = ctx_.next_parse_order();
        attach_active_edit_ref(ctx_, row);

        std::string path_text = as_text(file_path);
        if (!path_text.empty()) {
            std::filesystem::path path = join_path(ctx_.rootpath, path_text);
            std::error_code ec;
            std::filesystem::path abs = std::filesystem::absolute(path, ec);
            if (!ec) path = abs;
            path = path.lexically_normal();
            row.resolved_file_path = path_to_utf8(path);
            try {
                parse_other_train_file(path);
            } catch (const std::exception& e) {
                log_warn(e.what());
            }
        }

        ctx_.other_trains.push_back(std::move(row));
    }

    void dispatch_train(const std::string& fn, const std::vector<Value>& a, bool has_train_key) {
        if (fn == "add" && !has_train_key && a.size() >= 4) {
            add_other_train_definition("Add", a[0], a[1], a[2], a[3]);
        } else if (fn == "load" && has_train_key && a.size() >= 4) {
            add_other_train_definition("Load", a[0], a[1], a[2], a[3]);
        } else if (fn == "enable" && has_train_key && a.size() >= 2) {
            note_distance_use(ctx_);
            OtherTrainEnable row;
            row.distance = ctx_.distance;
            row.train_key = a[0];
            row.time = a[1];
            row.file_path = ctx_.current_file_path;
            row.order = ctx_.next_parse_order();
            attach_active_edit_ref(ctx_, row);
            ctx_.other_train_enables.push_back(std::move(row));
        } else if (fn == "stop" && has_train_key && a.size() >= 5) {
            note_distance_use(ctx_);
            OtherTrainStop row;
            row.distance = ctx_.distance;
            row.train_key = a[0];
            row.decelerate = a[1];
            row.stop_time = a[2];
            row.accelerate = a[3];
            row.speed = a[4];
            row.file_path = ctx_.current_file_path;
            row.order = ctx_.next_parse_order();
            attach_active_edit_ref(ctx_, row);
            ctx_.other_train_stops.push_back(std::move(row));
        }
    }

    void dispatch_rolling_noise(const std::string& fn, const std::vector<Value>& a) {
        if (fn != "change" || a.empty()) return;
        note_distance_use(ctx_);
        RollingNoiseChange row;
        row.distance = ctx_.distance;
        row.index = a[0];
        row.file_path = ctx_.current_file_path;
        row.order = ctx_.next_parse_order();
        attach_active_edit_ref(ctx_, row);
        ctx_.rolling_noises.push_back(std::move(row));
    }

    void dispatch_flange_noise(const std::string& fn, const std::vector<Value>& a) {
        if (fn != "change" || a.empty()) return;
        note_distance_use(ctx_);
        FlangeNoiseChange row;
        row.distance = ctx_.distance;
        row.index = a[0];
        row.file_path = ctx_.current_file_path;
        row.order = ctx_.next_parse_order();
        attach_active_edit_ref(ctx_, row);
        ctx_.flange_noises.push_back(std::move(row));
    }

    void dispatch_joint_noise(const std::string& fn, const std::vector<Value>& a) {
        if (fn != "play" || a.empty()) return;
        note_distance_use(ctx_);
        JointNoisePlay row;
        row.distance = ctx_.distance;
        row.index = a[0];
        row.file_path = ctx_.current_file_path;
        row.order = ctx_.next_parse_order();
        attach_active_edit_ref(ctx_, row);
        ctx_.joint_noises.push_back(std::move(row));
    }

    void dispatch_repeater(const std::string& fn, const std::vector<Value>& a) {
        if (fn == "begin" && a.size() >= 11) {
            note_distance_use(ctx_);
            RepeaterEvent row;
            row.distance = ctx_.distance;
            row.method = "Begin";
            row.repeater_key = a[0]; row.track_key = a[1];
            row.x = as_number(a[2]); row.y = as_number(a[3]); row.z = as_number(a[4]);
            row.rx = as_number(a[5]); row.ry = as_number(a[6]); row.rz = as_number(a[7]);
            row.tilt = as_number(a[8]); row.span = as_number(a[9]); row.interval = as_number(a[10]);
            row.structure_keys.assign(a.begin() + 11, a.end());
            row.file_path = ctx_.current_file_path;
            row.order = ctx_.next_parse_order();
            attach_active_edit_ref(ctx_, row);
            ctx_.repeaters.push_back(row);
        } else if (fn == "begin0" && a.size() >= 5) {
            note_distance_use(ctx_);
            RepeaterEvent row;
            row.distance = ctx_.distance;
            row.method = "Begin0";
            row.repeater_key = a[0]; row.track_key = a[1];
            row.tilt = as_number(a[2]); row.span = as_number(a[3]); row.interval = as_number(a[4]);
            row.structure_keys.assign(a.begin() + 5, a.end());
            row.file_path = ctx_.current_file_path;
            row.order = ctx_.next_parse_order();
            attach_active_edit_ref(ctx_, row);
            ctx_.repeaters.push_back(row);
        } else if (fn == "end" && !a.empty()) {
            note_distance_use(ctx_);
            RepeaterEvent row;
            row.distance = ctx_.distance;
            row.method = "End";
            row.repeater_key = a[0];
            row.track_key = Value::str("");
            row.file_path = ctx_.current_file_path;
            row.order = ctx_.next_parse_order();
            attach_active_edit_ref(ctx_, row);
            ctx_.repeaters.push_back(row);
        }
    }

    void dispatch_irregularity(const std::string& fn, const std::vector<Value>& a) {
        if (fn != "change" || a.size() < 6) return;
        note_distance_use(ctx_);
        IrregularityChange row;
        row.distance = ctx_.distance;
        row.x = as_number(a[0]);
        row.y = as_number(a[1]);
        row.r = as_number(a[2]);
        row.lx = as_number(a[3]);
        row.ly = as_number(a[4]);
        row.lr = as_number(a[5]);
        row.file_path = ctx_.current_file_path;
        row.order = ctx_.next_parse_order();
        attach_active_edit_ref(ctx_, row);
        ctx_.irregularities.push_back(row);
    }

    void dispatch_background(const std::string& fn, const std::vector<Value>& a) {
        if (fn != "change" || a.empty() || a[0].is_null()) return;
        note_distance_use(ctx_);
        BackgroundChange row;
        row.distance = ctx_.distance;
        row.structure_key = a[0];
        row.file_path = ctx_.current_file_path;
        row.order = ctx_.next_parse_order();
        attach_active_edit_ref(ctx_, row);
        ctx_.backgrounds.push_back(std::move(row));
    }

    void dispatch_adhesion(const std::string& fn, const std::vector<Value>& a) {
        if (fn != "change" || a.empty()) return;
        note_distance_use(ctx_);
        AdhesionChange row;
        row.distance = ctx_.distance;
        row.a = a[0];
        if (a.size() >= 3) {
            row.b = a[1];
            row.c = a[2];
        }
        row.file_path = ctx_.current_file_path;
        row.order = ctx_.next_parse_order();
        attach_active_edit_ref(ctx_, row);
        ctx_.adhesions.push_back(std::move(row));
    }

    void dispatch_cab_illuminance(const std::string& fn, const std::vector<Value>& a) {
        if ((fn != "interpolate" && fn != "set") || a.empty()) return;
        note_distance_use(ctx_);
        CabIlluminanceChange row;
        row.distance = ctx_.distance;
        row.value = a[0];
        row.file_path = ctx_.current_file_path;
        row.order = ctx_.next_parse_order();
        attach_active_edit_ref(ctx_, row);
        ctx_.cab_illuminance.push_back(std::move(row));
    }

    void dispatch_fog(const std::string& fn, const std::vector<Value>& a) {
        if ((fn != "interpolate" && fn != "set") || a.empty()) return;
        note_distance_use(ctx_);
        FogChange row;
        row.distance = ctx_.distance;
        row.density = a[0];
        if (a.size() > 1) row.red = a[1];
        if (a.size() > 2) row.green = a[2];
        if (a.size() > 3) row.blue = a[3];
        row.file_path = ctx_.current_file_path;
        row.order = ctx_.next_parse_order();
        attach_active_edit_ref(ctx_, row);
        ctx_.fogs.push_back(std::move(row));
    }
};

struct LastPos {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    double theta = 0.0;
    double radius = 0.0;
    double gradient = 0.0;
    double distance = 0.0;
    std::string interpolate_func = "line";
    double cant = 0.0;
    double center = 0.0;
    double gauge = 0.0;
};

class TrackPointer {
public:
    TrackPointer(const std::vector<OwnTrackEvent>& data, std::string target)
        : data_(&data), target_(std::move(target)) {
        ix_max_ = static_cast<int>(data.size()) - 1;
        next_ = seek(0);
    }

    TrackPointer(const std::vector<OtherTrackEvent>& data, std::string target)
        : other_data_(&data), target_(std::move(target)), other_(true) {
        ix_max_ = static_cast<int>(data.size()) - 1;
        next_ = seek(0);
    }

    int last() const { return last_; }
    int next() const { return next_; }

    bool on_nextpoint(double distance) const {
        return next_ >= 0 && event(next_).distance == distance;
    }

    bool over_nextpoint(double distance) const {
        return next_ >= 0 && event(next_).distance < distance;
    }

    void seeknext() {
        if (next_ >= 0) {
            last_ = next_;
            next_ = seek(next_ + 1);
        }
    }

    int seekoriginofcontinuous(int index) const {
        if (index < 0) return -1;
        while (index >= 0) {
            const auto& e = event(index);
            if (e.key == target_ && !e.value.is_continue()) return index;
            --index;
        }
        return -1;
    }

    const OwnTrackEvent& event(int index) const {
        if (other_) {
            temp_.distance = (*other_data_)[index].distance;
            temp_.key = (*other_data_)[index].key;
            temp_.value = (*other_data_)[index].value;
            temp_.flag = (*other_data_)[index].flag;
            return temp_;
        }
        return (*data_)[index];
    }

private:
    int seek(int ix0) const {
        int ix = ix0;
        while (ix <= ix_max_) {
            if (event(ix).key == target_) return ix;
            ++ix;
        }
        return -1;
    }

    const std::vector<OwnTrackEvent>* data_ = nullptr;
    const std::vector<OtherTrackEvent>* other_data_ = nullptr;
    std::string target_;
    bool other_ = false;
    int ix_max_ = -1;
    int last_ = -1;
    int next_ = -1;
    mutable OwnTrackEvent temp_;
};

std::pair<double, double> rotate_xy(double x, double y, double theta) {
    return {std::cos(theta) * x - std::sin(theta) * y,
            std::sin(theta) * x + std::cos(theta) * y};
}

std::uint64_t double_cache_bits(double value) {
    if (value == 0.0) value = 0.0;
    std::uint64_t bits = 0;
    static_assert(sizeof(bits) == sizeof(value), "unexpected double size");
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

void hash_combine_bits(std::size_t& seed, std::uint64_t value) {
    seed ^= static_cast<std::size_t>(value + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2));
}

template <typename PhaseFunc>
std::pair<double, double> integrate_unit_tangent_gauss8(double a, double b, int panels, PhaseFunc&& phase) {
    static constexpr std::array<double, 4> nodes{
        0.18343464249564980494,
        0.52553240991632898582,
        0.79666647741362673959,
        0.96028985649753623168
    };
    static constexpr std::array<double, 4> weights{
        0.36268378337836198297,
        0.31370664587788728734,
        0.22238103445337447054,
        0.10122853629037625915
    };

    if (b <= a) return {0.0, 0.0};
    panels = std::max(1, panels);
    const double width = (b - a) / panels;
    double x = 0.0;
    double y = 0.0;
    for (int panel = 0; panel < panels; ++panel) {
        const double lo = a + width * panel;
        const double hi = panel + 1 == panels ? b : lo + width;
        const double mid = (lo + hi) * 0.5;
        const double half = (hi - lo) * 0.5;
        double panel_x = 0.0;
        double panel_y = 0.0;
        for (size_t i = 0; i < nodes.size(); ++i) {
            const double delta = half * nodes[i];
            const double p0 = phase(mid - delta);
            const double p1 = phase(mid + delta);
            panel_x += weights[i] * (std::cos(p0) + std::cos(p1));
            panel_y += weights[i] * (std::sin(p0) + std::sin(p1));
        }
        x += half * panel_x;
        y += half * panel_y;
    }
    return {x, y};
}

std::pair<double, double> fresnel_cs_series(double x) {
    const double x2 = x * x;
    const double x4 = x2 * x2;
    const double q = (kPi * kPi / 4.0) * x4;

    double c_term = x;
    double c = c_term;
    for (int n = 1; n < 80; ++n) {
        c_term *= -q * (4.0 * n - 3.0) /
                  ((2.0 * n - 1.0) * (2.0 * n) * (4.0 * n + 1.0));
        c += c_term;
        if (std::fabs(c_term) <= std::fabs(c) * 1e-16) break;
    }

    double s_term = (kPi / 2.0) * x * x2 / 3.0;
    double s = s_term;
    for (int n = 1; n < 80; ++n) {
        s_term *= -q * (4.0 * n - 1.0) /
                  ((2.0 * n) * (2.0 * n + 1.0) * (4.0 * n + 3.0));
        s += s_term;
        if (std::fabs(s_term) <= std::fabs(s) * 1e-16) break;
    }
    return {c, s};
}

std::pair<double, double> fresnel_cs_asymptotic(double x) {
    const double phi = kPi * x * x * 0.5;
    const double px2 = kPi * x * x;
    const double px2_2 = px2 * px2;
    const double px2_4 = px2_2 * px2_2;
    const double px2_6 = px2_4 * px2_2;
    const double f = (1.0 / (kPi * x)) *
        (1.0 - 3.0 / px2_2 + 105.0 / px2_4 - 10395.0 / px2_6);
    const double g = (1.0 / (kPi * kPi * x * x * x)) *
        (1.0 - 15.0 / px2_2 + 945.0 / px2_4 - 135135.0 / px2_6);
    return {0.5 + f * std::sin(phi) - g * std::cos(phi),
            0.5 - f * std::cos(phi) - g * std::sin(phi)};
}

std::pair<double, double> fresnel_cs(double x) {
    if (x == 0.0) return {0.0, 0.0};
    const double sign = x < 0.0 ? -1.0 : 1.0;
    const double ax = std::fabs(x);
    std::pair<double, double> out;
    if (ax <= 1.6) {
        out = fresnel_cs_series(ax);
    } else if (ax >= 8.0) {
        out = fresnel_cs_asymptotic(ax);
    } else {
        const int panels = std::max(1, static_cast<int>(std::ceil(ax * ax * 2.0)));
        out = integrate_unit_tangent_gauss8(0.0, ax, panels, [](double t) {
            return kPi * t * t * 0.5;
        });
    }
    return {sign * out.first, sign * out.second};
}

struct CurveResult {
    double x = 0.0;
    double y = 0.0;
    double tau = 0.0;
    double radius = 0.0;
};

struct CircularCurveKey {
    std::uint64_t radius = 0;
    std::uint64_t length = 0;

    bool operator==(const CircularCurveKey& other) const {
        return radius == other.radius && length == other.length;
    }
};

struct CircularCurveKeyHash {
    std::size_t operator()(const CircularCurveKey& key) const {
        std::size_t seed = 0;
        hash_combine_bits(seed, key.radius);
        hash_combine_bits(seed, key.length);
        return seed;
    }
};

struct TransitionCurveKey {
    bool half_sine = false;
    std::uint64_t length = 0;
    std::uint64_t radius0 = 0;
    std::uint64_t radius1 = 0;
    std::uint64_t position = 0;

    bool operator==(const TransitionCurveKey& other) const {
        return half_sine == other.half_sine &&
               length == other.length &&
               radius0 == other.radius0 &&
               radius1 == other.radius1 &&
               position == other.position;
    }
};

struct TransitionCurveKeyHash {
    std::size_t operator()(const TransitionCurveKey& key) const {
        std::size_t seed = key.half_sine ? 0x9e3779b97f4a7c15ULL : 0;
        hash_combine_bits(seed, key.length);
        hash_combine_bits(seed, key.radius0);
        hash_combine_bits(seed, key.radius1);
        hash_combine_bits(seed, key.position);
        return seed;
    }
};

constexpr size_t kMaxGeometryCacheEntries = 262144;
std::mutex g_geometry_cache_mutex;
std::unordered_map<CircularCurveKey, CurveResult, CircularCurveKeyHash> g_circular_curve_cache;
std::unordered_map<TransitionCurveKey, CurveResult, TransitionCurveKeyHash> g_transition_curve_cache;

double radius_from_curvature(double curvature) {
    if (curvature == 0.0) return kInf;
    double radius = 1.0 / curvature;
    return std::fabs(radius) > 1e6 ? kInf : radius;
}

CurveResult circular_curve_local_uncached(double R, double l_intermediate) {
    if (R == 0.0 || std::isinf(R)) {
        return {l_intermediate, 0.0, 0.0, 0.0};
    }
    double tau = l_intermediate / R;
    double x0 = std::fabs(R) * std::sin(l_intermediate / std::fabs(R));
    double y0 = R * (1 - std::cos(l_intermediate / std::fabs(R)));
    return {x0, y0, tau, R};
}

CurveResult circular_curve_local(double R, double l_intermediate) {
    CircularCurveKey key{double_cache_bits(R), double_cache_bits(l_intermediate)};
    {
        std::lock_guard<std::mutex> lock(g_geometry_cache_mutex);
        auto it = g_circular_curve_cache.find(key);
        if (it != g_circular_curve_cache.end()) return it->second;
    }

    CurveResult result = circular_curve_local_uncached(R, l_intermediate);
    {
        std::lock_guard<std::mutex> lock(g_geometry_cache_mutex);
        if (g_circular_curve_cache.size() >= kMaxGeometryCacheEntries) {
            g_circular_curve_cache.clear();
        }
        g_circular_curve_cache.emplace(key, result);
    }
    return result;
}

CurveResult circular_curve(double R, double theta, double l_intermediate) {
    CurveResult local = circular_curve_local(R, l_intermediate);
    auto [x, y] = rotate_xy(local.x, local.y, theta);
    return {x, y, local.tau, local.radius};
}

double inv_radius(double r) {
    return std::isinf(r) ? 0.0 : 1.0 / r;
}

struct HalfSinResult {
    double x = 0.0;
    double y = 0.0;
    double tau = 0.0;
    double radius = kInf;
};

HalfSinResult halfsin_intermediate(double L, double r1, double r2, double l_intermediate, double dL = 1.0) {
    (void)dL;
    if (l_intermediate <= 0.0) {
        return {0.0, 0.0, 0.0, r1 == 0.0 ? kInf : r1};
    }
    if (L == 0.0) return {0.0, 0.0, 0.0, r2};

    const double k0 = inv_radius(r1);
    const double k1 = inv_radius(r2);
    const double dk = k1 - k0;
    auto tau_at = [=](double x) {
        return k0 * x + 0.5 * dk * (x - L / kPi * std::sin(kPi * x / L));
    };
    const double tau = tau_at(l_intermediate);
    const int panels = std::max(1, static_cast<int>(std::ceil(std::max(l_intermediate / 250.0,
                                                                         std::fabs(tau) / 0.25))));
    auto [X, Y] = integrate_unit_tangent_gauss8(0.0, l_intermediate, panels, tau_at);
    const double k = k0 + 0.5 * dk * (1.0 - std::cos(kPi * l_intermediate / L));
    double r = radius_from_curvature(k);
    return {X, Y, tau, r};
}

CurveResult linear_transition_curve_local(double L, double r1, double r2, double l_intermediate) {
    if (l_intermediate <= 0.0) return {0.0, 0.0, 0.0, std::fabs(r1) < 1e6 ? r1 : 0.0};
    if (L == 0.0) return {0.0, 0.0, 0.0, std::fabs(r2) < 1e6 ? r2 : 0.0};

    const double k0 = inv_radius(r1);
    const double k1 = inv_radius(r2);
    const double a = (k1 - k0) / L;
    const double k_at_l = k0 + a * l_intermediate;
    const double rl = radius_from_curvature(k_at_l);

    if (std::fabs(a) * std::max(1.0, L * L) < 1e-12) {
        CurveResult result = circular_curve_local(k0 != 0.0 ? 1.0 / k0 : kInf, l_intermediate);
        result.radius = rl;
        return result;
    }

    const double root = std::sqrt(std::fabs(a) / kPi);
    const double offset = k0 / a;
    const double u0 = root * offset;
    const double u1 = root * (l_intermediate + offset);
    auto [c0, s0] = fresnel_cs(u0);
    auto [c1, s1] = fresnel_cs(u1);
    double dc = c1 - c0;
    double ds = s1 - s0;
    if (a < 0.0) ds = -ds;

    const double phase = -k0 * k0 / (2.0 * a);
    const double scale = std::sqrt(kPi / std::fabs(a));
    const double x = scale * (std::cos(phase) * dc - std::sin(phase) * ds);
    const double y = scale * (std::sin(phase) * dc + std::cos(phase) * ds);
    const double turn = k0 * l_intermediate + 0.5 * a * l_intermediate * l_intermediate;
    return {x, y, turn, rl};
}

CurveResult transition_curve_local(double L, double r1, double r2,
                                   bool half_sine, double l_intermediate) {
    TransitionCurveKey key{half_sine,
                           double_cache_bits(L),
                           double_cache_bits(r1),
                           double_cache_bits(r2),
                           double_cache_bits(l_intermediate)};
    {
        std::lock_guard<std::mutex> lock(g_geometry_cache_mutex);
        auto it = g_transition_curve_cache.find(key);
        if (it != g_transition_curve_cache.end()) return it->second;
    }

    CurveResult result;
    if (half_sine) {
        HalfSinResult half = halfsin_intermediate(L, r1, r2, l_intermediate);
        result = {half.x, half.y, half.tau, half.radius};
    } else {
        result = linear_transition_curve_local(L, r1, r2, l_intermediate);
    }

    {
        std::lock_guard<std::mutex> lock(g_geometry_cache_mutex);
        if (g_transition_curve_cache.size() >= kMaxGeometryCacheEntries) {
            g_transition_curve_cache.clear();
        }
        g_transition_curve_cache.emplace(key, result);
    }
    return result;
}

CurveResult transition_curve(double L, double r1, double r2, double theta,
                             const std::string& func, double l_intermediate) {
    r1 = r1 == 0.0 ? kInf : r1;
    r2 = r2 == 0.0 ? kInf : r2;
    r1 = std::fabs(r1) > 1e6 ? kInf : r1;
    r2 = std::fabs(r2) > 1e6 ? kInf : r2;

    CurveResult local = transition_curve_local(L, r1, r2, func == "sin", l_intermediate);
    auto [x, y] = rotate_xy(local.x, local.y, theta);
    return {x, y, local.tau, std::fabs(local.radius) < 1e6 ? local.radius : 0.0};
}

std::pair<double, double> gradient_transition(double L, double gr1, double gr2, double l_intermediate) {
    double theta1 = std::atan(gr1 / 1000.0);
    double theta2 = std::atan(gr2 / 1000.0);
    double z = L / (theta2 - theta1) * std::cos(theta1) -
               L / (theta2 - theta1) * std::cos((theta2 - theta1) / L * l_intermediate + theta1);
    double gradient = 1000.0 * std::tan((theta2 - theta1) / L * l_intermediate + theta1);
    return {z, gradient};
}

class CantProcessor {
public:
    CantProcessor(TrackPointer pointer, const std::vector<OwnTrackEvent>& data, double last_cant)
        : pointer_(std::move(pointer)), own_data_(&data), cant_value_(last_cant) {}

    CantProcessor(TrackPointer pointer, const std::vector<OtherTrackEvent>& data, double last_cant)
        : pointer_(std::move(pointer)), other_data_(&data), other_(true), cant_value_(last_cant) {}

    double process(double dist, const std::string& func) {
        while (pointer_.over_nextpoint(dist)) {
            int origin = pointer_.seekoriginofcontinuous(pointer_.next());
            if (origin >= 0) {
                cant_distance_ = event(origin).distance;
                cant_value_ = as_number(event(origin).value);
            }
            pointer_.seeknext();
        }

        if (pointer_.last() < 0 || pointer_.next() < 0) return cant_value_;
        OwnTrackEvent next = event(pointer_.next());
        if (next.value.is_continue()) return cant_value_;
        OwnTrackEvent last = event(pointer_.last());
        if (next.flag == "i" || last.flag == "bt") {
            double next_value = as_number(next.value);
            if (cant_value_ != next_value) {
                return transition(next.distance - last.distance, cant_value_, next_value,
                                  func, dist - last.distance);
            }
        }
        return cant_value_;
    }

private:
    const OwnTrackEvent& event(int index) const {
        if (other_) {
            temp_.distance = (*other_data_)[index].distance;
            temp_.key = (*other_data_)[index].key;
            temp_.value = (*other_data_)[index].value;
            temp_.flag = (*other_data_)[index].flag;
            return temp_;
        }
        return (*own_data_)[index];
    }

    double transition(double L, double c1, double c2, const std::string& func, double l) const {
        if (L == 0.0) return c2;
        if (func == "sin") return (c2 - c1) / 2.0 * (std::sin(kPi / L * l - kPi / 2.0) + 1.0) + c1;
        return (c2 - c1) / L * l + c1;
    }

    TrackPointer pointer_;
    const std::vector<OwnTrackEvent>* own_data_ = nullptr;
    const std::vector<OtherTrackEvent>* other_data_ = nullptr;
    bool other_ = false;
    double cant_distance_ = 0.0;
    double cant_value_ = 0.0;
    mutable OwnTrackEvent temp_;
};

std::vector<double> sorted_unique(std::vector<double> values) {
    std::sort(values.begin(), values.end());
    values.erase(std::unique(values.begin(), values.end()), values.end());
    return values;
}

void append_arange(std::vector<double>& values, double start, double end, double step) {
    if (step <= 0.0) return;
    int guard = 0;
    for (double v = start; v < end && guard < 10000000; v += step, ++guard) {
        values.push_back(v);
    }
}

double round_minus2(double value) {
    return std::round(value / 100.0) * 100.0;
}

void generate_owntrack(MapContext& ctx, double unitdist,
                       bool has_arb, double arb_start, double arb_end, double arb_step,
                       const std::vector<double>* extra_controlpoints = nullptr) {
    std::vector<double> list_cp = ctx.controlpoints;
    if (list_cp.empty()) list_cp.push_back(0.0);
    if (extra_controlpoints) {
        list_cp.insert(list_cp.end(), extra_controlpoints->begin(), extra_controlpoints->end());
    }
    list_cp = sorted_unique(list_cp);
    double cp_min = list_cp.front();
    double cp_max = list_cp.back();
    double equaldist_unit = unitdist > 0.0 ? unitdist : 25.0;
    const double boundary_margin = 500.0;

    if (has_arb) {
        ctx.has_cp_arbdistribution = true;
        ctx.cp_arbdistribution = {arb_start, arb_end, arb_step};
        if (!ctx.station_position.empty()) {
            double min_station = ctx.station_position.begin()->first;
            double max_station = ctx.station_position.rbegin()->first;
            ctx.cp_arbdistribution_default = {std::max(0.0, round_minus2(min_station) - boundary_margin),
                                              round_minus2(max_station) + boundary_margin,
                                              equaldist_unit};
        } else {
            ctx.cp_arbdistribution_default = {std::max(0.0, round_minus2(cp_min) - boundary_margin),
                                              round_minus2(cp_max) + boundary_margin,
                                              equaldist_unit};
        }
        append_arange(list_cp, arb_start, arb_end, arb_step);
    } else if (!ctx.station_position.empty()) {
        double min_station = ctx.station_position.begin()->first;
        double max_station = ctx.station_position.rbegin()->first;
        double start = std::max(0.0, round_minus2(min_station) - boundary_margin);
        double end = round_minus2(max_station) + boundary_margin;
        append_arange(list_cp, start, end, equaldist_unit);
        ctx.has_cp_arbdistribution = true;
        ctx.cp_arbdistribution = {start, end, equaldist_unit};
        ctx.cp_arbdistribution_default = ctx.cp_arbdistribution;
        ctx.cp_defaultrange = {start, end};
    } else {
        double start = std::max(0.0, round_minus2(cp_min) - boundary_margin);
        double end = round_minus2(cp_max) + boundary_margin;
        append_arange(list_cp, start, end, equaldist_unit);
        ctx.has_cp_arbdistribution = true;
        ctx.cp_arbdistribution = {start, end, equaldist_unit};
        ctx.cp_arbdistribution_default = ctx.cp_arbdistribution;
        ctx.cp_defaultrange = {start, end};
    }
    list_cp = sorted_unique(list_cp);

    TrackPointer radius_p(ctx.own_track, "radius");
    TrackPointer gradient_p(ctx.own_track, "gradient");
    TrackPointer turn_p(ctx.own_track, "turn");
    TrackPointer interpolate_p(ctx.own_track, "interpolate_func");
    TrackPointer cant_p(ctx.own_track, "cant");
    TrackPointer center_p(ctx.own_track, "center");
    TrackPointer gauge_p(ctx.own_track, "gauge");

    LastPos lp;
    lp.distance = list_cp.front();
    struct RadiusLast {
        double distance = 0.0;
        double theta = 0.0;
        double radius = 0.0;
    } rlp;
    rlp.distance = lp.distance;

    CantProcessor cant_gen(std::move(cant_p), ctx.own_track, lp.cant);
    ctx.owntrack_buffer.clear(11);
    ctx.owntrack_buffer.reserve_rows(list_cp.size());

    for (double dist : list_cp) {
        while (interpolate_p.on_nextpoint(dist)) {
            lp.interpolate_func = as_text(interpolate_p.event(interpolate_p.next()).value);
            interpolate_p.seeknext();
        }

        double center_tmp = lp.center;
        while (center_p.on_nextpoint(dist)) {
            center_tmp = as_number(center_p.event(center_p.next()).value);
            center_p.seeknext();
        }

        double gauge_tmp = lp.gauge;
        while (gauge_p.on_nextpoint(dist)) {
            gauge_tmp = as_number(gauge_p.event(gauge_p.next()).value);
            gauge_p.seeknext();
        }

        while (radius_p.over_nextpoint(dist)) {
            int origin = radius_p.seekoriginofcontinuous(radius_p.next());
            if (origin >= 0) {
                double val = as_number(radius_p.event(origin).value);
                lp.radius = val;
                rlp.radius = val;
                rlp.distance = radius_p.event(origin).distance;
                rlp.theta = lp.theta;
            }
            radius_p.seeknext();
        }

        double c_theta = lp.theta;
        double c_ds = dist - lp.distance;
        double x = 0.0, y = 0.0, tau = 0.0, radius = lp.radius;

        if (radius_p.last() < 0) {
            if (radius_p.next() < 0 || lp.radius == 0.0) {
                x = std::cos(c_theta) * c_ds;
                y = std::sin(c_theta) * c_ds;
            } else {
                auto res = circular_curve(lp.radius, c_theta, c_ds);
                x = res.x; y = res.y; tau = res.tau; radius = lp.radius;
            }
        } else if (radius_p.next() < 0) {
            if (lp.radius == 0.0) {
                x = std::cos(c_theta) * c_ds;
                y = std::sin(c_theta) * c_ds;
            } else {
                auto res = circular_curve(lp.radius, c_theta, c_ds);
                x = res.x; y = res.y; tau = res.tau;
            }
            radius = lp.radius;
        } else {
            const auto& next = radius_p.event(radius_p.next());
            if (next.value.is_continue()) {
                if (lp.radius == 0.0) {
                    x = std::cos(c_theta) * c_ds;
                    y = std::sin(c_theta) * c_ds;
                } else {
                    auto res = circular_curve(lp.radius, c_theta, c_ds);
                    x = res.x; y = res.y; tau = res.tau;
                }
                radius = lp.radius;
            } else if (next.flag == "i" || radius_p.event(radius_p.last()).flag == "bt") {
                double next_radius = as_number(next.value);
                if (rlp.radius != next_radius) {
                    double total = next.distance - radius_p.event(radius_p.last()).distance;
                    double last_l = lp.distance - radius_p.event(radius_p.last()).distance;
                    double cur_l = dist - radius_p.event(radius_p.last()).distance;
                    CurveResult pos_last = transition_curve(total, rlp.radius, next_radius,
                                                            rlp.theta, lp.interpolate_func, last_l);
                    CurveResult pos_cur = transition_curve(total, rlp.radius, next_radius,
                                                           rlp.theta, lp.interpolate_func, cur_l);
                    x = pos_cur.x - pos_last.x;
                    y = pos_cur.y - pos_last.y;
                    tau = pos_cur.tau - pos_last.tau;
                    radius = pos_cur.radius;
                } else if (next_radius != 0.0) {
                    auto res = circular_curve(lp.radius, c_theta, c_ds);
                    x = res.x; y = res.y; tau = res.tau; radius = lp.radius;
                } else {
                    x = std::cos(c_theta) * c_ds;
                    y = std::sin(c_theta) * c_ds;
                    radius = lp.radius;
                }
            } else {
                if (lp.radius == 0.0) {
                    x = std::cos(c_theta) * c_ds;
                    y = std::sin(c_theta) * c_ds;
                } else {
                    auto res = circular_curve(lp.radius, c_theta, c_ds);
                    x = res.x; y = res.y; tau = res.tau;
                }
                radius = lp.radius;
            }
        }

        if (turn_p.next() >= 0 && turn_p.on_nextpoint(dist)) {
            tau += std::atan(as_number(turn_p.event(turn_p.next()).value));
            turn_p.seeknext();
        }

        while (gradient_p.over_nextpoint(dist)) {
            int origin = gradient_p.seekoriginofcontinuous(gradient_p.next());
            if (origin >= 0) {
                lp.gradient = as_number(gradient_p.event(origin).value);
            }
            gradient_p.seeknext();
        }

        double g_ds = dist - lp.distance;
        double gradient = lp.gradient;
        double z = 0.0;
        if (gradient_p.last() >= 0 && gradient_p.next() >= 0) {
            const auto& next = gradient_p.event(gradient_p.next());
            if (!next.value.is_continue() &&
                (next.flag == "i" || gradient_p.event(gradient_p.last()).flag == "bt") &&
                lp.gradient != as_number(next.value)) {
                auto gz = gradient_transition(next.distance - lp.distance,
                                              lp.gradient, as_number(next.value), g_ds);
                z = gz.first;
                gradient = gz.second;
            } else {
                z = g_ds * std::sin(std::atan(lp.gradient / 1000.0));
            }
        } else {
            z = g_ds * std::sin(std::atan(lp.gradient / 1000.0));
        }

        double cant_tmp = cant_gen.process(dist, lp.interpolate_func);

        lp.x += x;
        lp.y += y;
        lp.z += z;
        lp.theta += tau;
        lp.radius = radius;
        lp.gradient = gradient;
        lp.distance = dist;
        lp.cant = cant_tmp;
        lp.center = center_tmp;
        lp.gauge = gauge_tmp;

        ctx.owntrack_buffer.push({dist, lp.x, lp.y, lp.z, lp.theta, lp.radius, lp.gradient,
                                  lp.interpolate_func == "sin" ? 0.0 : 1.0,
                                  lp.cant, lp.center, lp.gauge});
    }
}

void generate_curveradius(MapContext& ctx) {
    ctx.curveradius_buffer.clear(2);
    ctx.curveradius_buffer.reserve_rows(ctx.own_track.size() * 2 + 2);
    if (ctx.owntrack_buffer.rows == 0) return;
    double min_cp = ctx.owntrack_buffer.data[0];
    double max_cp = ctx.owntrack_buffer.data[(ctx.owntrack_buffer.rows - 1) * ctx.owntrack_buffer.cols];
    ctx.curveradius_buffer.push({min_cp, 0.0});
    TrackPointer radius_p(ctx.own_track, "radius");
    bool previous_bt = false;
    double previous = 0.0;
    while (radius_p.next() >= 0) {
        const auto& e = radius_p.event(radius_p.next());
        double new_radius = e.value.is_continue() ? previous : as_number(e.value);
        if (e.value.is_continue() || previous_bt || e.flag == "i") {
            ctx.curveradius_buffer.push({e.distance, new_radius});
        } else {
            ctx.curveradius_buffer.push({e.distance, previous});
            ctx.curveradius_buffer.push({e.distance, new_radius});
        }
        previous = new_radius;
        previous_bt = e.flag == "bt";
        radius_p.seeknext();
    }
    ctx.curveradius_buffer.push({max_cp, 0.0});
}

double relative_position(double L, double radius, double ya, double yb, double l_intermediate) {
    if (L == 0.0) return yb;
    if (radius != 0.0) {
        double sintheta = std::sqrt(L * L + (yb - ya) * (yb - ya)) / (2.0 * radius);
        if (std::fabs(sintheta) <= 1.0) {
            double tau = std::atan((yb - ya) / L);
            double theta = 2.0 * std::asin(sintheta);
            double phiA = theta / 2.0 - tau;
            double x0 = radius * std::sin(phiA);
            double y0 = ya + radius * std::cos(phiA);
            return y0 - radius * std::cos(std::asin((l_intermediate - x0) / radius));
        }
    }
    return (yb - ya) / L * l_intermediate + ya;
}

struct OtherTrackBuildResult {
    std::string key;
    Matrix buffer;
    double seconds = 0.0;
    bool has_buffer = false;
};

Matrix build_othertrack_buffer(const MapContext& ctx, const std::string& trackkey, bool& has_buffer) {
    const auto& data = ctx.othertrack.at(trackkey);
    has_buffer = false;
    if (data.empty() || ctx.owntrack_buffer.rows == 0) return {};

    TrackPointer x_position_p(data, "x.position");
    TrackPointer x_radius_p(data, "x.radius");
    TrackPointer y_position_p(data, "y.position");
    TrackPointer y_radius_p(data, "y.radius");
    TrackPointer func_p(data, "interpolate_func");
    TrackPointer center_p(data, "center");
    TrackPointer gauge_p(data, "gauge");
    TrackPointer cant_p(data, "cant");

    double x_position_last = 0.0, x_position_next = 0.0;
    double x_radius_last = 0.0, x_radius_next = 0.0;
    double y_position_last = 0.0, y_position_next = 0.0;
    double y_radius_last = 0.0, y_radius_next = 0.0;
    double center_last = 0.0, center_next = 0.0;
    double gauge_last = 0.0, gauge_next = 0.0;
    double cant_initial = 0.0;
    std::string func_last = "line";
    std::string func_next = "line";

    auto init_numeric = [](TrackPointer& p, double& last, double& next) {
        if (p.next() < 0) return;
        const auto& e = p.event(p.next());
        double v = e.value.is_continue() ? 0.0 : as_number(e.value);
        last = v;
        next = v;
    };
    init_numeric(x_position_p, x_position_last, x_position_next);
    init_numeric(x_radius_p, x_radius_last, x_radius_next);
    init_numeric(y_position_p, y_position_last, y_position_next);
    init_numeric(y_radius_p, y_radius_last, y_radius_next);
    init_numeric(center_p, center_last, center_next);
    init_numeric(gauge_p, gauge_last, gauge_next);
    if (cant_p.next() >= 0) {
        const auto& e = cant_p.event(cant_p.next());
        cant_initial = e.value.is_continue() ? 0.0 : as_number(e.value);
    }
    if (func_p.next() >= 0) {
        const auto& e = func_p.event(func_p.next());
        func_last = func_next = e.value.is_continue() ? "line" : as_text(e.value);
    }

    CantProcessor cant_gen(std::move(cant_p), data, cant_initial);
    Matrix result;
    result.clear(8);
    result.reserve_rows(ctx.owntrack_buffer.rows);

    auto advance_over = [](TrackPointer& p, double& last, double& next, double dist) {
        while (p.over_nextpoint(dist)) {
            p.seeknext();
            last = next;
            if (p.next() >= 0) {
                const auto& e = p.event(p.next());
                next = e.value.is_continue() ? last : as_number(e.value);
            }
        }
    };
    auto advance_on = [](TrackPointer& p, double& last, double& next, double dist) {
        while (p.on_nextpoint(dist)) {
            p.seeknext();
            last = next;
            if (p.next() >= 0) {
                const auto& e = p.event(p.next());
                next = e.value.is_continue() ? last : as_number(e.value);
            }
        }
    };

    // Child tracks keep their first defined value before the first explicit point.
    // The initial values above already hold that first point, so emit the full own-track range.
    for (size_t r = 0; r < ctx.owntrack_buffer.rows; ++r) {
        const double* element = &ctx.owntrack_buffer.data[r * ctx.owntrack_buffer.cols];
        double dist = element[0];

        advance_over(x_position_p, x_position_last, x_position_next, dist);
        advance_over(x_radius_p, x_radius_last, x_radius_next, dist);
        advance_over(y_position_p, y_position_last, y_position_next, dist);
        advance_over(y_radius_p, y_radius_last, y_radius_next, dist);

        while (func_p.on_nextpoint(dist)) {
            func_p.seeknext();
            func_last = func_next;
            if (func_p.next() >= 0) {
                const auto& e = func_p.event(func_p.next());
                func_next = e.value.is_continue() ? func_last : as_text(e.value);
            }
        }
        advance_on(center_p, center_last, center_next, dist);
        advance_on(gauge_p, gauge_last, gauge_next, dist);

        double out_x = 0.0, out_y = 0.0;
        double sin_theta = std::sin(element[4]);
        double cos_theta = std::cos(element[4]);
        if (x_position_p.last() >= 0 && x_position_p.next() >= 0) {
            double x_distance_last = x_position_p.event(x_position_p.last()).distance;
            double x_distance_next = x_position_p.event(x_position_p.next()).distance;
            double rel = relative_position(x_distance_next - x_distance_last,
                                           x_radius_last, x_position_last,
                                           x_position_next, dist - x_distance_last);
            out_x = element[1] - sin_theta * rel;
            out_y = element[2] + cos_theta * rel;
        } else {
            double rel = x_position_last;
            out_x = element[1] - sin_theta * rel;
            out_y = element[2] + cos_theta * rel;
        }

        double out_z = 0.0;
        if (y_position_p.last() >= 0 && y_position_p.next() >= 0) {
            double y_distance_last = y_position_p.event(y_position_p.last()).distance;
            double y_distance_next = y_position_p.event(y_position_p.next()).distance;
            double rel = relative_position(y_distance_next - y_distance_last,
                                           y_radius_last, y_position_last,
                                           y_position_next, dist - y_distance_last);
            out_z = rel + element[3];
        } else {
            out_z = y_position_last + element[3];
        }

        double cant = cant_gen.process(dist, func_last);
        result.push({dist, out_x, out_y, out_z, func_last == "sin" ? 0.0 : 1.0,
                     cant, center_last, gauge_last});
    }
    has_buffer = true;
    return result;
}

void relocate(MapContext& ctx) {
    ctx.controlpoints = sorted_unique(ctx.controlpoints);
    std::stable_sort(ctx.own_track.begin(), ctx.own_track.end(),
                     [](const auto& a, const auto& b) { return a.distance < b.distance; });
    for (auto& kv : ctx.othertrack) {
        auto& rows = kv.second;
        std::stable_sort(rows.begin(), rows.end(),
                         [](const auto& a, const auto& b) { return a.distance < b.distance; });
        if (!rows.empty()) {
            ctx.othertrack_range[kv.first] = {rows.front().distance, rows.front().distance};
            for (const auto& e : rows) {
                ctx.othertrack_range[kv.first].first = std::min(ctx.othertrack_range[kv.first].first, e.distance);
                ctx.othertrack_range[kv.first].second = std::max(ctx.othertrack_range[kv.first].second, e.distance);
            }
        }
    }
    auto by_distance = [](const auto& a, const auto& b) { return a.distance < b.distance; };
    std::stable_sort(ctx.structure_loads.begin(), ctx.structure_loads.end(), by_distance);
    std::stable_sort(ctx.structure_puts.begin(), ctx.structure_puts.end(), by_distance);
    std::stable_sort(ctx.structure_betweens.begin(), ctx.structure_betweens.end(), by_distance);
    std::stable_sort(ctx.other_trains.begin(), ctx.other_trains.end(), by_distance);
    std::stable_sort(ctx.other_train_enables.begin(), ctx.other_train_enables.end(), by_distance);
    std::stable_sort(ctx.other_train_stops.begin(), ctx.other_train_stops.end(), by_distance);
    std::stable_sort(ctx.repeaters.begin(), ctx.repeaters.end(), by_distance);
    std::stable_sort(ctx.section_begins.begin(), ctx.section_begins.end(), by_distance);
    std::stable_sort(ctx.section_speed_limits.begin(), ctx.section_speed_limits.end(), by_distance);
    std::stable_sort(ctx.signal_puts.begin(), ctx.signal_puts.end(), by_distance);
    std::stable_sort(ctx.beacons.begin(), ctx.beacons.end(), by_distance);
    std::stable_sort(ctx.pretrains.begin(), ctx.pretrains.end(), by_distance);
    std::stable_sort(ctx.map_sounds.begin(), ctx.map_sounds.end(), by_distance);
    std::stable_sort(ctx.map_sound_3d.begin(), ctx.map_sound_3d.end(), by_distance);
    std::stable_sort(ctx.rolling_noises.begin(), ctx.rolling_noises.end(), by_distance);
    std::stable_sort(ctx.flange_noises.begin(), ctx.flange_noises.end(), by_distance);
    std::stable_sort(ctx.joint_noises.begin(), ctx.joint_noises.end(), by_distance);
    std::stable_sort(ctx.irregularities.begin(), ctx.irregularities.end(), by_distance);
    std::stable_sort(ctx.backgrounds.begin(), ctx.backgrounds.end(), by_distance);
    std::stable_sort(ctx.adhesions.begin(), ctx.adhesions.end(), by_distance);
    std::stable_sort(ctx.cab_illuminance.begin(), ctx.cab_illuminance.end(), by_distance);
    std::stable_sort(ctx.fogs.begin(), ctx.fogs.end(), by_distance);
    std::stable_sort(ctx.speedlimits.begin(), ctx.speedlimits.end(), by_distance);
    std::stable_sort(ctx.station_puts.begin(), ctx.station_puts.end(), by_distance);
}

void build_structure_put_buffer(MapContext& ctx) {
    ctx.structure_put_buffer.clear(10);
    ctx.structure_put_buffer.reserve_rows(ctx.structure_puts.size());
    for (const auto& row : ctx.structure_puts) {
        ctx.structure_put_buffer.push({row.distance, row.x, row.y, row.z,
                                       row.rx, row.ry, row.rz, row.tilt,
                                       row.span, static_cast<double>(row.order)});
    }
}

double matrix_value(const Matrix& matrix, size_t row, size_t col) {
    return matrix.data[row * matrix.cols + col];
}

double angle_delta_abs(double a, double b) {
    return std::abs(std::atan2(std::sin(b - a), std::cos(b - a)));
}

void append_controlpoint_if_in_range(std::vector<double>& values, double distance,
                                     double min_distance, double max_distance) {
    if (!std::isfinite(distance)) return;
    constexpr double eps = 1e-6;
    if (distance < min_distance - eps || distance > max_distance + eps) return;
    values.push_back(std::clamp(distance, min_distance, max_distance));
}

void append_scene_model_distance(std::vector<double>& values, double distance, double span,
                                 double min_distance, double max_distance) {
    append_controlpoint_if_in_range(values, distance, min_distance, max_distance);
    if (span > 1.0) append_controlpoint_if_in_range(values, distance + span, min_distance, max_distance);
}

std::vector<double> build_scene_adaptive_controlpoints(const MapContext& ctx,
                                                       const Matrix& baseline,
                                                       double min_step,
                                                       double max_step,
                                                       double max_angle_degrees,
                                                       double max_chord_error) {
    std::vector<double> values;
    if (baseline.rows < 2 || baseline.cols < 5) return values;

    min_step = std::clamp(std::isfinite(min_step) ? min_step : 1.0, 0.25, 100.0);
    max_step = std::clamp(std::isfinite(max_step) ? max_step : 25.0, min_step, 200.0);
    const double max_angle = std::max(0.001, (std::isfinite(max_angle_degrees) ? max_angle_degrees : 1.0) * kPi / 180.0);
    const double max_error = std::max(0.001, std::isfinite(max_chord_error) ? max_chord_error : 0.01);
    const double min_distance = matrix_value(baseline, 0, 0);
    const double max_distance = matrix_value(baseline, baseline.rows - 1, 0);

    values.reserve(baseline.rows);
    for (size_t row = 1; row < baseline.rows; ++row) {
        const double a_distance = matrix_value(baseline, row - 1, 0);
        const double b_distance = matrix_value(baseline, row, 0);
        const double span = b_distance - a_distance;
        if (!(span > 0.0) || !std::isfinite(span)) continue;

        double desired_step = max_step;
        if (baseline.cols > 5) {
            const double r0 = std::abs(matrix_value(baseline, row - 1, 5));
            const double r1 = std::abs(matrix_value(baseline, row, 5));
            double radius = 0.0;
            if (r0 > 1e-6 && r1 > 1e-6) radius = std::min(r0, r1);
            else if (r0 > 1e-6) radius = r0;
            else if (r1 > 1e-6) radius = r1;
            if (radius > 1e-6 && std::isfinite(radius)) {
                desired_step = std::min(desired_step, std::sqrt(std::max(min_step * min_step,
                                                                         8.0 * radius * max_error)));
            }
        }

        const double theta_delta = angle_delta_abs(matrix_value(baseline, row - 1, 4),
                                                   matrix_value(baseline, row, 4));
        if (theta_delta > max_angle) {
            const double angle_step = span / std::ceil(theta_delta / max_angle);
            desired_step = std::min(desired_step, angle_step);
        }

        desired_step = std::clamp(desired_step, min_step, max_step);
        const int divisions = std::max(1, static_cast<int>(std::ceil(span / desired_step)));
        for (int i = 1; i < divisions; ++i) {
            values.push_back(a_distance + span * (static_cast<double>(i) / static_cast<double>(divisions)));
        }
    }

    for (const StructurePut& row : ctx.structure_puts) {
        append_scene_model_distance(values, row.distance, row.span, min_distance, max_distance);
    }
    for (const StructurePut& row : ctx.structure_betweens) {
        append_controlpoint_if_in_range(values, row.distance, min_distance, max_distance);
    }

    struct ActiveRepeater {
        double begin = 0.0;
        double interval = 0.0;
        double span = 0.0;
    };
    auto append_repeater_range = [&](const ActiveRepeater& repeater, double end_distance) {
        if (end_distance < repeater.begin) return;
        append_scene_model_distance(values, repeater.begin, repeater.span, min_distance, max_distance);
        append_scene_model_distance(values, end_distance, repeater.span, min_distance, max_distance);
        if (repeater.interval <= 1e-9 || !std::isfinite(repeater.interval)) return;

        size_t guard = 0;
        for (double distance = repeater.begin; distance < end_distance + 1e-6; distance += repeater.interval) {
            append_scene_model_distance(values, distance, repeater.span, min_distance, max_distance);
            if (++guard > 1000000) break;
        }
    };

    std::map<std::string, ActiveRepeater> active_repeaters;
    for (const RepeaterEvent& row : ctx.repeaters) {
        std::string key = key_text(row.repeater_key);
        if (key.empty()) continue;
        if (row.method == "Begin" || row.method == "Begin0") {
            auto existing = active_repeaters.find(key);
            if (existing != active_repeaters.end()) {
                append_repeater_range(existing->second, row.distance);
                active_repeaters.erase(existing);
            }
            active_repeaters[key] = ActiveRepeater{row.distance, row.interval, row.span};
        } else if (row.method == "End") {
            auto existing = active_repeaters.find(key);
            if (existing == active_repeaters.end()) continue;
            append_repeater_range(existing->second, row.distance);
            active_repeaters.erase(existing);
        }
    }
    for (const auto& kv : active_repeaters) {
        append_repeater_range(kv.second, max_distance);
    }

    return values;
}

void generate_geometry(MapContext& ctx, double unitdist,
                       bool has_arb, double arb_start, double arb_end, double arb_step,
                       const std::vector<double>* extra_controlpoints = nullptr) {
    log_info("calculating track geometry");
    ctx.unit_distance = unitdist;
    ctx.othertrack_buffers.clear();
    ctx.timing.owntrack_seconds = 0.0;
    ctx.timing.othertrack_seconds.clear();
    ctx.timing.json_seconds = 0.0;
    ctx.load_timing_logged = false;
    {
        ScopedTimer timer(&ctx.timing.owntrack_seconds);
        generate_owntrack(ctx, unitdist, has_arb, arb_start, arb_end, arb_step, extra_controlpoints);
    }
    generate_curveradius(ctx);
    std::vector<std::future<OtherTrackBuildResult>> futures;
    futures.reserve(ctx.othertrack_order.size());
    for (const auto& key : ctx.othertrack_order) {
        futures.push_back(std::async(std::launch::async, [&ctx, key]() {
            auto started_at = SteadyClock::now();
            OtherTrackBuildResult out;
            out.key = key;
            out.buffer = build_othertrack_buffer(ctx, key, out.has_buffer);
            out.seconds = elapsed_seconds_since(started_at);
            return out;
        }));
    }
    for (auto& future : futures) {
        OtherTrackBuildResult result = future.get();
        ctx.timing.othertrack_seconds.push_back({result.key, result.seconds});
        if (result.has_buffer) {
            ctx.othertrack_buffers[result.key] = std::move(result.buffer);
        }
    }
    build_structure_put_buffer(ctx);
    ctx.ir_json_cache_by_flags.clear();
}

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

std::string element_edit_id(const MapContext& ctx, const EditSourceRef& ref,
                            const std::string& row_kind) {
    if (!ref.valid() || ref.statement_index >= ctx.parsed_statements.size()) return {};
    std::string cache_key = std::to_string(ref.statement_index) + "|" +
                            std::to_string(ref.element_index) + "|" + row_kind;
    auto cached = ctx.element_edit_id_cache.find(cache_key);
    if (cached != ctx.element_edit_id_cache.end()) return cached->second;
    const ParsedStatement& statement = ctx.parsed_statements[ref.statement_index];
    std::string edit_id = make_edit_id(source_file_key(ctx, statement.source),
                                       statement.global_order,
                                       row_kind,
                                       ref.element_index);
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

struct MapEditChange {
    std::string change_id;
    std::string edit_id;
    std::string operation;
    std::map<std::string, std::string> field_changes;
    std::string replacement_statement;
    std::string target_file_path;
    std::string insert_before_edit_id;
    std::string expected_source_hash;
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

struct MapEditReport {
    std::vector<std::string> changed_files;
    std::vector<std::string> warnings;
    std::vector<std::string> blocking_errors;
    std::vector<MapEditPreview> previews;
    std::vector<MapEditPatchedFile> patched_files;
    int update_count = 0;
    int insert_count = 0;
    int delete_count = 0;

    bool ok() const {
        return blocking_errors.empty();
    }
};

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

std::string decode_source_text_for_edit(const std::string& bytes,
                                        const std::string& encoding) {
    std::string lower = ascii_lower(encoding);
    if (lower == "utf-16le") return decode_utf16(bytes, true);
    if (lower == "utf-16be") return decode_utf16(bytes, false);
    if (lower == "cp932" || lower == "shift_jis" || lower == "sjis") {
        return decode_codepage(bytes, 932, false);
    }
    if (has_utf8_bom(bytes)) return decode_codepage(bytes.substr(3), CP_UTF8, true);
    return decode_codepage(bytes, CP_UTF8, true);
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
    patch.text = decode_source_text_for_edit(bytes, record.encoding);
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

std::string build_ir_json(MapContext& ctx, unsigned flags) {
    flags = normalize_ir_json_flags(flags);
    std::ostringstream out;
    out << "{\"rootpath\":\"" << json_escape(ctx.rootpath_utf8) << "\"";
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

std::unique_ptr<MapContext> parse_map_context(std::filesystem::path map_path,
                                              double unit_distance,
                                              SourceTextOverrides overrides,
                                              bool has_arbitrary_distribution,
                                              const std::array<double, 3>& arbitrary_distribution) {
    auto ctx = std::make_unique<MapContext>();
    ctx->source_overrides = std::move(overrides);
    ActiveTimingScope active(ctx->timing);
    log_info("loading map " + path_to_utf8(map_path));
    LoadedText loaded = load_header_text(*ctx, map_path, "BveTs Map ", 2.0);
    ctx->rootpath = loaded.root;
    ctx->rootpath_utf8 = path_to_utf8(loaded.root);
    ctx->entry_file_path = loaded.normalized_path;
    ctx->current_file_path = loaded.normalized_path;
    ctx->include_stack.push_back(ctx->current_file_path);

    log_info("parsing syntax tree");
    {
        ScopedTimer timer(&ctx->timing.parse_seconds);
        Parser parser(*ctx, std::move(loaded));
        parser.parse();
    }

    log_info("sorting parsed IR");
    {
        ScopedTimer timer(&ctx->timing.relocate_seconds);
        relocate(*ctx);
    }
    generate_geometry(*ctx, unit_distance, has_arbitrary_distribution,
                      arbitrary_distribution[0], arbitrary_distribution[1],
                      arbitrary_distribution[2]);
    return ctx;
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
    return report;
}

KvDoubleBuffer make_buffer(const Matrix& m) {
    return {m.data.empty() ? nullptr : m.data.data(), m.rows, m.cols};
}

} // namespace

extern "C" {

KV_API void kv_set_log_callback(KvLogCallback callback) {
    set_log_callback(callback);
}

KV_API void* kv_load_map(const char* path, double unit_distance) {
    try {
        if (!path) throw std::runtime_error("path is null");
        std::filesystem::path map_path = path_from_utf8(path);
        auto ctx = parse_map_context(map_path, unit_distance, SourceTextOverrides{}, false, {0.0, 0.0, 0.0});
        log_info(path_to_utf8(map_path.filename()) + " loaded");
        return ctx.release();
    } catch (const std::exception& e) {
        set_last_error(e.what());
        log_error(e.what());
        return nullptr;
    }
}

KV_API int kv_generate_geometry(void* handle, double unit_distance,
                                int has_arbitrary_distribution,
                                double arbitrary_start,
                                double arbitrary_end,
                                double arbitrary_step) {
    try {
        if (!handle) throw std::runtime_error("handle is null");
        auto* ctx = static_cast<MapContext*>(handle);
        generate_geometry(*ctx, unit_distance, has_arbitrary_distribution != 0,
                          arbitrary_start, arbitrary_end, arbitrary_step);
        return 1;
    } catch (const std::exception& e) {
        set_last_error(e.what());
        log_error(e.what());
        return 0;
    }
}

KV_API int kv_generate_scene_geometry(void* handle, double unit_distance,
                                      double min_step, double max_step,
                                      double max_angle_degrees,
                                      double max_chord_error) {
    try {
        if (!handle) throw std::runtime_error("handle is null");
        auto* ctx = static_cast<MapContext*>(handle);
        if (ctx->owntrack_buffer.rows == 0) {
            generate_geometry(*ctx, unit_distance, false, 0.0, 0.0, 0.0);
        }

        const bool has_arb = ctx->has_cp_arbdistribution;
        const std::array<double, 3> arb = ctx->cp_arbdistribution;
        Matrix baseline = ctx->owntrack_buffer;
        std::vector<double> extra = build_scene_adaptive_controlpoints(
            *ctx, baseline, min_step, max_step, max_angle_degrees, max_chord_error);
        generate_geometry(*ctx, unit_distance, has_arb, arb[0], arb[1], arb[2], &extra);
        return 1;
    } catch (const std::exception& e) {
        set_last_error(e.what());
        log_error(e.what());
        return 0;
    }
}

KV_API KvDoubleBuffer kv_get_owntrack_buffer(void* handle) {
    if (!handle) return {nullptr, 0, 0};
    return make_buffer(static_cast<MapContext*>(handle)->owntrack_buffer);
}

KV_API KvDoubleBuffer kv_get_curveradius_buffer(void* handle) {
    if (!handle) return {nullptr, 0, 0};
    return make_buffer(static_cast<MapContext*>(handle)->curveradius_buffer);
}

KV_API size_t kv_get_othertrack_count(void* handle) {
    if (!handle) return 0;
    return static_cast<MapContext*>(handle)->othertrack_order.size();
}

KV_API const char* kv_get_othertrack_key(void* handle, size_t index) {
    if (!handle) return nullptr;
    auto* ctx = static_cast<MapContext*>(handle);
    if (index >= ctx->othertrack_order.size()) return nullptr;
    return ctx->othertrack_order[index].c_str();
}

KV_API KvDoubleBuffer kv_get_othertrack_buffer(void* handle, const char* key) {
    if (!handle || !key) return {nullptr, 0, 0};
    auto* ctx = static_cast<MapContext*>(handle);
    std::string k = ascii_lower(key);
    auto it = ctx->othertrack_buffers.find(k);
    if (it == ctx->othertrack_buffers.end()) return {nullptr, 0, 0};
    return make_buffer(it->second);
}

KV_API KvDoubleBuffer kv_get_structure_puts(void* handle) {
    if (!handle) return {nullptr, 0, 0};
    return make_buffer(static_cast<MapContext*>(handle)->structure_put_buffer);
}

KV_API const char* kv_get_ir_json_ex(void* handle, unsigned flags) {
    try {
        if (!handle) throw std::runtime_error("handle is null");
        auto* ctx = static_cast<MapContext*>(handle);
        flags = normalize_ir_json_flags(flags);
        auto& cache = ctx->ir_json_cache_by_flags[flags];
        if (cache.empty()) {
            ScopedTimer timer(&ctx->timing.json_seconds);
            cache = build_ir_json(*ctx, flags);
        }
        if (!ctx->load_timing_logged) {
            log_load_timing(*ctx);
            ctx->load_timing_logged = true;
        }
        return copy_c_string(cache);
    } catch (const std::exception& e) {
        set_last_error(e.what());
        return nullptr;
    }
}

KV_API const char* kv_get_ir_json(void* handle) {
    return kv_get_ir_json_ex(handle, KV_IR_JSON_FULL_EDIT | KV_IR_JSON_FULL_STATEMENT_SOURCE);
}

KV_API const char* kv_get_edit_target_info(void* handle, const char* edit_id) {
    try {
        if (!handle) throw std::runtime_error("handle is null");
        auto* ctx = static_cast<MapContext*>(handle);
        return copy_c_string(edit_target_info_json(*ctx, edit_id ? edit_id : ""));
    } catch (const std::exception& e) {
        set_last_error(e.what());
        return nullptr;
    }
}

KV_API const char* kv_edit_dry_run(void* handle, const char* changes_json) {
    try {
        if (!handle) throw std::runtime_error("handle is null");
        auto* ctx = static_cast<MapContext*>(handle);
        std::vector<MapEditChange> changes = parse_edit_changes_json(changes_json);
        MapEditReport report = build_edit_report(*ctx, changes, false);
        return copy_c_string(report_json(report));
    } catch (const std::exception& e) {
        set_last_error(e.what());
        return nullptr;
    }
}

KV_API const char* kv_edit_apply_to_memory(void* handle, const char* changes_json) {
    try {
        if (!handle) throw std::runtime_error("handle is null");
        auto* ctx = static_cast<MapContext*>(handle);
        std::vector<MapEditChange> changes = parse_edit_changes_json(changes_json);
        MapEditReport report = build_edit_report(*ctx, changes, false);
        if (report.ok()) {
            try {
                apply_edit_report_to_memory(*ctx, report);
            } catch (const std::exception& e) {
                report.blocking_errors.push_back(std::string("edited cache reload failed: ") + e.what());
            }
        }
        return copy_c_string(report_json(report));
    } catch (const std::exception& e) {
        set_last_error(e.what());
        return nullptr;
    }
}

KV_API const char* kv_edit_apply(void* handle, const char* changes_json) {
    try {
        if (!handle) throw std::runtime_error("handle is null");
        auto* ctx = static_cast<MapContext*>(handle);
        std::vector<MapEditChange> changes = parse_edit_changes_json(changes_json);
        MapEditReport report = build_edit_report(*ctx, changes, true);
        return copy_c_string(report_json(report));
    } catch (const std::exception& e) {
        set_last_error(e.what());
        return nullptr;
    }
}

KV_API const char* kv_edit_commit(void* handle) {
    try {
        if (!handle) throw std::runtime_error("handle is null");
        auto* ctx = static_cast<MapContext*>(handle);
        MapEditReport report = commit_memory_edits(*ctx);
        return copy_c_string(report_json(report));
    } catch (const std::exception& e) {
        set_last_error(e.what());
        return nullptr;
    }
}

KV_API const char* kv_get_last_error(void) {
    return last_error_c_str();
}

KV_API void kv_free(void* handle) {
    delete static_cast<MapContext*>(handle);
}

KV_API void kv_free_string(const char* text) {
    std::free(const_cast<char*>(text));
}

}
