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

using SteadyClock = std::chrono::steady_clock;

constexpr double kInf = std::numeric_limits<double>::infinity();
constexpr double kPi = 3.141592653589793238462643383279502884;
KvLogCallback g_log_callback = nullptr;
thread_local std::string g_last_error;

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

void emit_log(const std::string& line) {
    if (g_log_callback) {
        g_log_callback(line.c_str());
    } else {
        std::cout << line << std::endl;
    }
}

void log_info(const std::string& message) {
    emit_log("[INFO]maploader.cpp: " + message);
}

void log_warn(const std::string& message) {
    emit_log("[WARN]maploader.cpp: " + message);
}

void log_error(const std::string& message) {
    emit_log("[ERROR]maploader.cpp: " + message);
}

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

std::string path_to_utf8(const std::filesystem::path& path) {
#if defined(__cpp_char8_t)
    auto s = path.u8string();
    return std::string(reinterpret_cast<const char*>(s.data()), s.size());
#else
    return path.u8string();
#endif
}

#if defined(_WIN32)
std::wstring utf8_to_wide(const std::string& utf8) {
    if (utf8.empty()) return {};
    int wide_len = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                       utf8.data(), static_cast<int>(utf8.size()),
                                       nullptr, 0);
    if (wide_len <= 0) {
        wide_len = MultiByteToWideChar(CP_UTF8, 0,
                                       utf8.data(), static_cast<int>(utf8.size()),
                                       nullptr, 0);
    }
    if (wide_len <= 0) throw std::runtime_error("UTF-8 path decode failed");
    std::wstring wide(wide_len, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()),
                        wide.data(), wide_len);
    return wide;
}

std::filesystem::path path_from_utf8(const std::string& utf8) {
    return std::filesystem::path(utf8_to_wide(utf8));
}
#else
std::filesystem::path path_from_utf8(const std::string& utf8) {
    return std::filesystem::path(utf8);
}
#endif

std::string json_escape(const std::string& s) {
    std::ostringstream out;
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

std::string read_binary_file(const std::filesystem::path& path) {
#if defined(_WIN32)
    FILE* input = _wfopen(path.wstring().c_str(), L"rb");
    if (!input) {
        throw std::runtime_error("File open error: " + path_to_utf8(path));
    }
    std::string result;
    char buffer[8192];
    while (true) {
        size_t n = std::fread(buffer, 1, sizeof(buffer), input);
        if (n > 0) result.append(buffer, n);
        if (n < sizeof(buffer)) {
            if (std::ferror(input)) {
                std::fclose(input);
                throw std::runtime_error("File read error: " + path_to_utf8(path));
            }
            break;
        }
    }
    std::fclose(input);
    return result;
#else
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("File open error: " + path_to_utf8(path));
    }
    std::ostringstream buffer;
    buffer << input.rdbuf();
    return buffer.str();
#endif
}

#if defined(_WIN32)
std::string wide_to_utf8(const std::wstring& wide) {
    if (wide.empty()) return {};
    int bytes = WideCharToMultiByte(CP_UTF8, 0, wide.data(),
                                    static_cast<int>(wide.size()),
                                    nullptr, 0, nullptr, nullptr);
    if (bytes <= 0) throw std::runtime_error("WideCharToMultiByte failed");
    std::string out(bytes, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()),
                        out.data(), bytes, nullptr, nullptr);
    return out;
}

std::string decode_codepage(const std::string& bytes, unsigned int codepage, bool strict) {
    if (bytes.empty()) return {};
    DWORD flags = strict && codepage == CP_UTF8 ? MB_ERR_INVALID_CHARS : 0;
    int wide_len = MultiByteToWideChar(codepage, flags, bytes.data(),
                                       static_cast<int>(bytes.size()),
                                       nullptr, 0);
    if (wide_len <= 0) {
        throw std::runtime_error("text decode failed");
    }
    std::wstring wide(wide_len, L'\0');
    MultiByteToWideChar(codepage, flags, bytes.data(), static_cast<int>(bytes.size()),
                        wide.data(), wide_len);
    return wide_to_utf8(wide);
}
#else
std::string decode_codepage(const std::string& bytes, unsigned int, bool) {
    return bytes;
}
#endif

std::string append_utf8_codepoint(char32_t cp) {
    std::string out;
    if (cp <= 0x7F) {
        out.push_back(static_cast<char>(cp));
    } else if (cp <= 0x7FF) {
        out.push_back(static_cast<char>(0xC0 | ((cp >> 6) & 0x1F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp <= 0xFFFF) {
        out.push_back(static_cast<char>(0xE0 | ((cp >> 12) & 0x0F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | ((cp >> 18) & 0x07)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
    return out;
}

std::string decode_utf16(const std::string& bytes, bool little_endian) {
    size_t start = 0;
    if (bytes.size() >= 2) {
        unsigned char b0 = static_cast<unsigned char>(bytes[0]);
        unsigned char b1 = static_cast<unsigned char>(bytes[1]);
        if ((b0 == 0xff && b1 == 0xfe) || (b0 == 0xfe && b1 == 0xff)) {
            start = 2;
        }
    }
    std::string out;
    for (size_t i = start; i + 1 < bytes.size(); i += 2) {
        unsigned char a = static_cast<unsigned char>(bytes[i]);
        unsigned char b = static_cast<unsigned char>(bytes[i + 1]);
        uint16_t unit = little_endian
            ? static_cast<uint16_t>(a | (b << 8))
            : static_cast<uint16_t>((a << 8) | b);
        if (unit >= 0xD800 && unit <= 0xDBFF && i + 3 < bytes.size()) {
            unsigned char c = static_cast<unsigned char>(bytes[i + 2]);
            unsigned char d = static_cast<unsigned char>(bytes[i + 3]);
            uint16_t low = little_endian
                ? static_cast<uint16_t>(c | (d << 8))
                : static_cast<uint16_t>((c << 8) | d);
            if (low >= 0xDC00 && low <= 0xDFFF) {
                char32_t cp = 0x10000 + (((unit - 0xD800) << 10) | (low - 0xDC00));
                out += append_utf8_codepoint(cp);
                i += 2;
                continue;
            }
        }
        out += append_utf8_codepoint(unit);
    }
    return out;
}

std::string first_line_ascii(const std::string& bytes) {
    size_t end = bytes.find('\n');
    if (end == std::string::npos) end = std::min<size_t>(bytes.size(), 512);
    std::string line = bytes.substr(0, end);
    for (char& ch : line) {
        unsigned char c = static_cast<unsigned char>(ch);
        if (c >= 0x80) ch = ' ';
    }
    return line;
}

bool has_utf8_bom(const std::string& bytes) {
    return bytes.size() >= 3 &&
           static_cast<unsigned char>(bytes[0]) == 0xef &&
           static_cast<unsigned char>(bytes[1]) == 0xbb &&
           static_cast<unsigned char>(bytes[2]) == 0xbf;
}

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

struct LoadedText {
    std::string body;
    std::filesystem::path path;
    std::filesystem::path root;
    std::string encoding;
};

LoadedText load_header_text(const std::filesystem::path& path,
                            const std::string& head_str,
                            double min_version) {
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

    size_t line_end = text.find('\n');
    std::string header = line_end == std::string::npos ? text : text.substr(0, line_end);
    if (ascii_lower(header).find(ascii_lower(head_str)) == std::string::npos) {
        throw std::runtime_error(path_to_utf8(path) + " is not " + head_str);
    }
    if (parse_first_version(header) < min_version) {
        throw std::runtime_error(path_to_utf8(path) + " is under Ver." + json_number(min_version));
    }
    std::string body = line_end == std::string::npos ? std::string() : text.substr(line_end + 1);
    return {body, path, std::filesystem::absolute(path).parent_path(), encoding};
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

struct OwnTrackEvent {
    double distance = 0.0;
    std::string key;
    Value value;
    std::string flag;
};

struct OtherTrackEvent {
    double distance = 0.0;
    std::string track_key;
    std::string key;
    Value value;
    std::string flag;
};

struct StructureLoad {
    double distance = 0.0;
    std::string method;
    Value load_file_path;
    std::string file_path;
    int order = 0;
};

struct StructureModel {
    std::string structure_key;
    std::string file_path;
};

struct SoundListEntry {
    std::string sound_key;
    std::string file_path;
    int buffer_count = 1;
    bool is_3d = false;
};

struct SectionBegin {
    double distance = 0.0;
    std::vector<Value> signal_indices;
    std::string file_path;
    int order = 0;
};

struct SectionSpeedLimit {
    double distance = 0.0;
    std::vector<Value> speeds;
    std::string file_path;
    int order = 0;
};

struct SignalAspect {
    std::string signal_aspect_key;
    std::vector<std::string> structure_keys;
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
};

struct BeaconPut {
    double distance = 0.0;
    Value type;
    Value section;
    Value send_data;
    std::string file_path;
    int order = 0;
};

struct PreTrainPass {
    double distance = 0.0;
    Value pass_time;
    std::string file_path;
    int order = 0;
};

struct RollingNoiseChange {
    double distance = 0.0;
    Value index;
    std::string file_path;
    int order = 0;
};

struct FlangeNoiseChange {
    double distance = 0.0;
    Value index;
    std::string file_path;
    int order = 0;
};

struct JointNoisePlay {
    double distance = 0.0;
    Value index;
    std::string file_path;
    int order = 0;
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
};

struct BackgroundChange {
    double distance = 0.0;
    Value structure_key;
    std::string file_path;
    int order = 0;
};

struct AdhesionChange {
    double distance = 0.0;
    Value a;
    Value b;
    Value c;
    std::string file_path;
    int order = 0;
};

struct CabIlluminanceChange {
    double distance = 0.0;
    Value value;
    std::string file_path;
    int order = 0;
};

struct FogChange {
    double distance = 0.0;
    Value density;
    Value red;
    Value green;
    Value blue;
    std::string file_path;
    int order = 0;
};

struct SpeedLimitEvent {
    double distance = 0.0;
    Value speed;
};

struct StationPut {
    double distance = 0.0;
    Value station_key;
    Value door;
    Value margin1;
    Value margin2;
    int order = 0;
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
    std::string current_file_path;
    double distance = 0.0;
    int parse_order = 0;
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
    std::map<std::string, std::array<std::string, 13>> station_list;
    std::map<std::string, std::vector<OtherTrackEvent>> othertrack;
    std::vector<std::string> othertrack_order;
    std::map<std::string, std::pair<double, double>> othertrack_range;
    std::vector<StructureLoad> structure_loads;
    std::vector<StructureModel> structure_models;
    std::vector<SoundListEntry> sound_list;
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
    std::string ir_json_cache;
    LoadTiming timing;
    bool load_timing_logged = false;

    int next_parse_order() {
        return ++parse_order;
    }
};

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
    ctx.own_track.push_back({ctx.distance, key, stored, flag});
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
    ctx.othertrack[key].push_back({ctx.distance, key, element_key, stored, flag});
}

struct MapObject {
    std::string label;
    Value key;
    bool has_key = false;
};

struct MapFunction {
    std::string label;
    std::vector<Value> args;
};

class Parser {
public:
    Parser(MapContext& context, std::string source, std::filesystem::path file_path)
        : ctx_(context), src_(std::move(source)), file_path_(std::move(file_path)) {}

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
            } else if (starts_with(pos_, "/*")) {
                pos_ += 2;
                while (!eof() && !starts_with(pos_, "*/")) ++pos_;
                if (!eof()) pos_ += 2;
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
            } else if (src_.compare(p, 2, "/*") == 0) {
                p += 2;
                while (p < src_.size() && src_.compare(p, 2, "*/") != 0) ++p;
                if (p < src_.size()) p += 2;
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

        if (peek() == '$' && next_is_variable_assignment()) {
            flush_pending_includes();
            ++pos_;
            std::string name = ascii_lower(parse_variable_name());
            expect('=');
            Value value = parse_expression();
            expect(';');
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
                Value path = parse_expression();
                expect(';');
                queue_include(as_text(path));
                return;
            }
            if (current_starts_map_element()) {
                flush_pending_includes();
                parse_map_element();
                expect(';');
                return;
            }
        }

        flush_pending_includes();
        Value distance = parse_expression();
        expect(';');
        set_distance(ctx_, as_number(distance));
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
        seed.current_file_path = path_to_utf8(std::filesystem::absolute(child));
        seed.distance = ctx_.distance;
        seed.variables = ctx_.variables;
        return seed;
    }

    static IncludeResult parse_include_context(MapContext seed, std::filesystem::path child) {
        IncludeResult result;
        result.context = std::move(seed);
        try {
            ActiveTimingScope active(result.context.timing);
            LoadedText loaded = load_header_text(child, "BveTs Map ", 2.0);
            result.context.current_file_path = path_to_utf8(std::filesystem::absolute(child));
            Parser nested(result.context, loaded.body, child);
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

        int order_base = ctx_.parse_order;
        auto offset_order = [order_base](int& order) {
            if (order > 0) order += order_base;
        };
        for (auto& row : child.station_puts) offset_order(row.order);
        for (auto& row : child.structure_loads) offset_order(row.order);
        for (auto& row : child.structure_puts) offset_order(row.order);
        for (auto& row : child.structure_betweens) offset_order(row.order);
        for (auto& row : child.repeaters) offset_order(row.order);
        for (auto& row : child.section_begins) offset_order(row.order);
        for (auto& row : child.section_speed_limits) offset_order(row.order);
        for (auto& row : child.signal_puts) offset_order(row.order);
        for (auto& row : child.beacons) offset_order(row.order);
        for (auto& row : child.pretrains) offset_order(row.order);
        for (auto& row : child.rolling_noises) offset_order(row.order);
        for (auto& row : child.flange_noises) offset_order(row.order);
        for (auto& row : child.joint_noises) offset_order(row.order);
        for (auto& row : child.irregularities) offset_order(row.order);
        for (auto& row : child.backgrounds) offset_order(row.order);
        for (auto& row : child.adhesions) offset_order(row.order);
        for (auto& row : child.cab_illuminance) offset_order(row.order);
        for (auto& row : child.fogs) offset_order(row.order);
        ctx_.parse_order += child.parse_order;

        if (child.has_distance_assignment) {
            ctx_.distance = child.distance;
            ctx_.has_distance_assignment = true;
        }
        for (const std::string& key : child.variable_writes) {
            auto it = child.variables.find(key);
            if (it != child.variables.end()) {
                ctx_.variables[key] = std::move(it->second);
            }
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

    void parse_map_element() {
        std::vector<MapObject> objects;
        do {
            objects.push_back(parse_map_object());
            expect('.');
        } while (!next_is_function());
        MapFunction function = parse_map_function();
        dispatch(objects, function);
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
        parse_map_args(function.args);
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
        if (label == "tan") return Value::num(std::tan(as_number(args.at(0))));
        if (label == "asin") return Value::num(std::asin(as_number(args.at(0))));
        if (label == "acos") return Value::num(std::acos(as_number(args.at(0))));
        if (label == "atan") return Value::num(std::atan(as_number(args.at(0))));
        if (label == "atan2") return Value::num(std::atan2(as_number(args.at(0)), as_number(args.at(1))));
        if (label == "sqrt") return Value::num(std::sqrt(as_number(args.at(0))));
        if (label == "exp") return Value::num(std::exp(as_number(args.at(0))));
        if (label == "log") return Value::num(std::log(as_number(args.at(0))));
        if (label == "log10") return Value::num(std::log10(as_number(args.at(0))));
        if (label == "floor") return Value::num(std::floor(as_number(args.at(0))));
        if (label == "ceil") return Value::num(std::ceil(as_number(args.at(0))));
        if (label == "round") return Value::num(std::round(as_number(args.at(0))));
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
            dispatch_sound(fn, function.args, first == "sound3d");
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
            row.order = ctx_.next_parse_order();
            ctx_.station_puts.push_back(std::move(row));
        } else if (fn == "load" && !a.empty()) {
            std::filesystem::path path = join_path(ctx_.rootpath, as_text(a.at(0)));
            LoadedText loaded = load_header_text(path, "BveTs Station List ", 0.04);
            parse_station_list(loaded.body);
        }
    }

    void parse_station_list(const std::string& body) {
        std::istringstream input(body);
        std::string line;
        while (std::getline(input, line)) {
            std::vector<std::string> fields = parse_comma_separated_fields(line, true);
            if (fields.empty() || fields[0].empty()) continue;

            std::array<std::string, 13> row{};
            for (size_t i = 0; i < row.size() && i < fields.size(); ++i) row[i] = fields[i];
            std::string key = ascii_lower(row[0]);
            ctx_.station_key[key] = row[1];
            ctx_.station_list[key] = std::move(row);
        }
    }

    void parse_structure_list(const std::string& body, const std::filesystem::path& root) {
        std::istringstream input(body);
        std::string line;
        while (std::getline(input, line)) {
            std::string trimmed = trim_field_copy(line);
            if (trimmed.empty() || trimmed[0] == '#') continue;

            std::vector<std::string> fields = parse_comma_separated_fields(line, true);
            if (fields.empty() || fields[0].empty()) continue;

            StructureModel row;
            row.structure_key = fields[0];
            if (fields.size() > 1 && !fields[1].empty()) {
                std::filesystem::path model_path = join_path(root, fields[1]);
                std::error_code ec;
                std::filesystem::path abs = std::filesystem::absolute(model_path, ec);
                if (!ec) model_path = abs;
                row.file_path = path_to_utf8(model_path.lexically_normal());
            }
            ctx_.structure_models.push_back(std::move(row));
        }
    }

    void parse_signal_aspect_list(const std::string& body) {
        std::istringstream input(body);
        std::string line;
        SignalAspect* current_aspect = nullptr;
        while (std::getline(input, line)) {
            std::string trimmed = trim_field_copy(line);
            if (trimmed.empty() || trimmed[0] == '#') continue;

            std::vector<std::string> fields = parse_comma_separated_fields(line, true);
            trim_trailing_empty_fields(fields);
            if (fields.empty()) continue;
            const bool starts_glare_row = fields[0].empty();
            if (starts_glare_row && !current_aspect) continue;

            if (!starts_glare_row) {
                SignalAspect row;
                row.signal_aspect_key = fields[0];
                ctx_.signal_aspects.push_back(std::move(row));
                current_aspect = &ctx_.signal_aspects.back();
            }
            for (size_t i = 1; i < fields.size(); ++i) {
                current_aspect->structure_keys.push_back(fields[i]);
            }
        }
    }

    void parse_sound_list(const std::string& body, const std::filesystem::path& root, bool is_3d) {
        std::istringstream input(body);
        std::string line;
        while (std::getline(input, line)) {
            std::string trimmed = trim_field_copy(line);
            if (trimmed.empty() || trimmed[0] == '#') continue;

            std::vector<std::string> fields = parse_comma_separated_fields(line, true);
            if (fields.empty() || fields[0].empty()) continue;

            SoundListEntry row;
            row.sound_key = fields[0];
            row.is_3d = is_3d;
            if (fields.size() > 1 && !fields[1].empty()) {
                std::filesystem::path sound_path = join_path(root, fields[1]);
                std::error_code ec;
                std::filesystem::path abs = std::filesystem::absolute(sound_path, ec);
                if (!ec) sound_path = abs;
                row.file_path = path_to_utf8(sound_path.lexically_normal());
            }
            if (fields.size() > 2) {
                row.buffer_count = parse_sound_buffer_count(fields[2]);
            }
            ctx_.sound_list.push_back(std::move(row));
        }
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
            ctx_.speedlimits.push_back({ctx_.distance, a.empty() ? Value::num(0.0) : a[0]});
        } else if (fn == "end") {
            note_distance_use(ctx_);
            ctx_.speedlimits.push_back({ctx_.distance, Value::null()});
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
            ctx_.section_begins.push_back(std::move(row));
        } else if (fn == "setspeedlimit") {
            note_distance_use(ctx_);
            SectionSpeedLimit row;
            row.distance = ctx_.distance;
            row.speeds = a;
            row.file_path = ctx_.current_file_path;
            row.order = ctx_.next_parse_order();
            ctx_.section_speed_limits.push_back(std::move(row));
        }
    }

    void dispatch_signal(const std::string& fn, const std::vector<Value>& a, bool has_signal_key) {
        if (fn == "load" && !has_signal_key && !a.empty()) {
            std::string list_path_text = as_text(a.at(0));
            if (list_path_text.empty()) return;
            try {
                std::filesystem::path path = join_path(ctx_.rootpath, list_path_text);
                LoadedText loaded = load_header_text(path, "BveTs Signal Aspects List ", 2.0);
                parse_signal_aspect_list(loaded.body);
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
            ctx_.structure_loads.push_back(row);
            std::string list_path_text = as_text(row.load_file_path);
            if (!list_path_text.empty()) {
                try {
                    std::filesystem::path path = join_path(ctx_.rootpath, list_path_text);
                    LoadedText loaded = load_header_text(path, "BveTs Structure List ", 1.0);
                    parse_structure_list(loaded.body, loaded.root);
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
            ctx_.structure_betweens.push_back(row);
        }
    }

    void dispatch_sound(const std::string& fn, const std::vector<Value>& a, bool is_3d) {
        if (fn != "load" || a.empty()) return;
        std::string list_path_text = as_text(a.at(0));
        if (list_path_text.empty()) return;
        try {
            std::filesystem::path path = join_path(ctx_.rootpath, list_path_text);
            LoadedText loaded = load_header_text(path, "BveTs Sound List ", 2.0);
            parse_sound_list(loaded.body, loaded.root, is_3d);
        } catch (const std::exception& e) {
            log_warn(e.what());
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
                       bool has_arb, double arb_start, double arb_end, double arb_step) {
    std::vector<double> list_cp = ctx.controlpoints;
    if (list_cp.empty()) list_cp.push_back(0.0);
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

    double min_dist = data.front().distance;
    for (const auto& e : data) min_dist = std::min(min_dist, e.distance);

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

    for (size_t r = 0; r < ctx.owntrack_buffer.rows; ++r) {
        const double* element = &ctx.owntrack_buffer.data[r * ctx.owntrack_buffer.cols];
        double dist = element[0];
        if (min_dist > dist) continue;

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
    std::stable_sort(ctx.repeaters.begin(), ctx.repeaters.end(), by_distance);
    std::stable_sort(ctx.section_begins.begin(), ctx.section_begins.end(), by_distance);
    std::stable_sort(ctx.section_speed_limits.begin(), ctx.section_speed_limits.end(), by_distance);
    std::stable_sort(ctx.signal_puts.begin(), ctx.signal_puts.end(), by_distance);
    std::stable_sort(ctx.beacons.begin(), ctx.beacons.end(), by_distance);
    std::stable_sort(ctx.pretrains.begin(), ctx.pretrains.end(), by_distance);
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

void generate_geometry(MapContext& ctx, double unitdist,
                       bool has_arb, double arb_start, double arb_end, double arb_step) {
    log_info("calculating track geometry");
    ctx.othertrack_buffers.clear();
    ctx.timing.owntrack_seconds = 0.0;
    ctx.timing.othertrack_seconds.clear();
    ctx.timing.json_seconds = 0.0;
    ctx.load_timing_logged = false;
    {
        ScopedTimer timer(&ctx.timing.owntrack_seconds);
        generate_owntrack(ctx, unitdist, has_arb, arb_start, arb_end, arb_step);
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
    ctx.ir_json_cache.clear();
}

void append_event_json(std::ostringstream& out, const OwnTrackEvent& e) {
    out << "{\"distance\":" << json_number(e.distance)
        << ",\"key\":\"" << json_escape(e.key)
        << "\",\"value\":" << json_value(e.value)
        << ",\"flag\":\"" << json_escape(e.flag) << "\"}";
}

void append_other_json(std::ostringstream& out, const OtherTrackEvent& e) {
    out << "{\"distance\":" << json_number(e.distance)
        << ",\"key\":\"" << json_escape(e.key)
        << "\",\"value\":" << json_value(e.value)
        << ",\"flag\":\"" << json_escape(e.flag) << "\"}";
}

void append_structure_put_json(std::ostringstream& out, const StructurePut& row, bool between) {
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
        << "\",\"order\":" << row.order << "}";
}

void append_value_array_json(std::ostringstream& out, const std::vector<Value>& values) {
    out << "[";
    for (size_t i = 0; i < values.size(); ++i) {
        if (i) out << ",";
        out << json_value(values[i]);
    }
    out << "]";
}

void append_section_begin_json(std::ostringstream& out, const SectionBegin& row) {
    out << "{\"distance\":" << json_number(row.distance)
        << ",\"signalIndices\":";
    append_value_array_json(out, row.signal_indices);
    out << ",\"filePath\":\"" << json_escape(row.file_path)
        << "\",\"order\":" << row.order << "}";
}

void append_section_speed_limit_json(std::ostringstream& out, const SectionSpeedLimit& row) {
    out << "{\"distance\":" << json_number(row.distance)
        << ",\"speeds\":";
    append_value_array_json(out, row.speeds);
    out << ",\"filePath\":\"" << json_escape(row.file_path)
        << "\",\"order\":" << row.order << "}";
}

void append_signal_put_json(std::ostringstream& out, const SignalPut& row) {
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
        << "\",\"order\":" << row.order << "}";
}

void append_beacon_json(std::ostringstream& out, const BeaconPut& row) {
    out << "{\"distance\":" << json_number(row.distance)
        << ",\"type\":" << json_value(row.type)
        << ",\"section\":" << json_value(row.section)
        << ",\"sendData\":" << json_value(row.send_data)
        << ",\"filePath\":\"" << json_escape(row.file_path)
        << "\",\"order\":" << row.order << "}";
}

void append_pretrain_json(std::ostringstream& out, const PreTrainPass& row) {
    out << "{\"distance\":" << json_number(row.distance)
        << ",\"passTime\":" << json_value(row.pass_time)
        << ",\"filePath\":\"" << json_escape(row.file_path)
        << "\",\"order\":" << row.order << "}";
}

void append_station_put_json(std::ostringstream& out, const StationPut& row) {
    out << "{\"distance\":" << json_number(row.distance)
        << ",\"stationKey\":" << json_value(row.station_key)
        << ",\"door\":" << json_value(row.door)
        << ",\"margin1\":" << json_value(row.margin1)
        << ",\"margin2\":" << json_value(row.margin2)
        << ",\"order\":" << row.order << "}";
}

void append_adhesion_json(std::ostringstream& out, const AdhesionChange& row) {
    out << "{\"distance\":" << json_number(row.distance)
        << ",\"a\":" << json_value(row.a)
        << ",\"b\":" << json_value(row.b)
        << ",\"c\":" << json_value(row.c)
        << ",\"filePath\":\"" << json_escape(row.file_path)
        << "\",\"order\":" << row.order << "}";
}

void append_background_json(std::ostringstream& out, const BackgroundChange& row) {
    out << "{\"distance\":" << json_number(row.distance)
        << ",\"structureKey\":" << json_value(row.structure_key)
        << ",\"filePath\":\"" << json_escape(row.file_path)
        << "\",\"order\":" << row.order << "}";
}

void append_cab_illuminance_json(std::ostringstream& out, const CabIlluminanceChange& row) {
    out << "{\"distance\":" << json_number(row.distance)
        << ",\"value\":" << json_value(row.value)
        << ",\"filePath\":\"" << json_escape(row.file_path)
        << "\",\"order\":" << row.order << "}";
}

void append_fog_json(std::ostringstream& out, const FogChange& row) {
    out << "{\"distance\":" << json_number(row.distance)
        << ",\"density\":" << json_value(row.density)
        << ",\"red\":" << json_value(row.red)
        << ",\"green\":" << json_value(row.green)
        << ",\"blue\":" << json_value(row.blue)
        << ",\"filePath\":\"" << json_escape(row.file_path)
        << "\",\"order\":" << row.order << "}";
}

std::string build_ir_json(MapContext& ctx) {
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
        append_event_json(out, ctx.own_track[i]);
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
        append_station_put_json(out, ctx.station_puts[i]);
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
        for (size_t i = 0; i < kv.second.size(); ++i) {
            if (i) out << ",";
            out << "\"" << station_list_keys[i] << "\":\"" << json_escape(kv.second[i]) << "\"";
        }
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
            append_other_json(out, rows[i]);
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
            << "\",\"order\":" << row.order << "}";
    }
    out << "],\"data\":[";
    for (size_t i = 0; i < ctx.structure_puts.size(); ++i) {
        if (i) out << ",";
        append_structure_put_json(out, ctx.structure_puts[i], false);
    }
    out << "],\"between_data\":[";
    for (size_t i = 0; i < ctx.structure_betweens.size(); ++i) {
        if (i) out << ",";
        append_structure_put_json(out, ctx.structure_betweens[i], true);
    }
    out << "],\"models\":[";
    for (size_t i = 0; i < ctx.structure_models.size(); ++i) {
        if (i) out << ",";
        const auto& row = ctx.structure_models[i];
        out << "{\"structureKey\":\"" << json_escape(row.structure_key)
            << "\",\"filePath\":\"" << json_escape(row.file_path) << "\"}";
    }
    out << "]}";

    out << ",\"section\":{\"begin\":[";
    for (size_t i = 0; i < ctx.section_begins.size(); ++i) {
        if (i) out << ",";
        append_section_begin_json(out, ctx.section_begins[i]);
    }
    out << "],\"speedLimit\":[";
    for (size_t i = 0; i < ctx.section_speed_limits.size(); ++i) {
        if (i) out << ",";
        append_section_speed_limit_json(out, ctx.section_speed_limits[i]);
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
        out << "]}";
    }
    out << "],\"data\":[";
    for (size_t i = 0; i < ctx.signal_puts.size(); ++i) {
        if (i) out << ",";
        append_signal_put_json(out, ctx.signal_puts[i]);
    }
    out << "]}";

    out << ",\"beacon\":[";
    for (size_t i = 0; i < ctx.beacons.size(); ++i) {
        if (i) out << ",";
        append_beacon_json(out, ctx.beacons[i]);
    }
    out << "]";

    out << ",\"preTrain\":[";
    for (size_t i = 0; i < ctx.pretrains.size(); ++i) {
        if (i) out << ",";
        append_pretrain_json(out, ctx.pretrains[i]);
    }
    out << "]";

    out << ",\"soundList\":[";
    for (size_t i = 0; i < ctx.sound_list.size(); ++i) {
        if (i) out << ",";
        const auto& row = ctx.sound_list[i];
        out << "{\"soundKey\":\"" << json_escape(row.sound_key)
            << "\",\"filePath\":\"" << json_escape(row.file_path)
            << "\",\"bufferCount\":" << row.buffer_count
            << ",\"is3D\":" << (row.is_3d ? "true" : "false") << "}";
    }
    out << "]";

    out << ",\"rollingNoise\":[";
    for (size_t i = 0; i < ctx.rolling_noises.size(); ++i) {
        if (i) out << ",";
        const auto& row = ctx.rolling_noises[i];
        out << "{\"distance\":" << json_number(row.distance)
            << ",\"index\":" << json_value(row.index)
            << ",\"filePath\":\"" << json_escape(row.file_path)
            << "\",\"order\":" << row.order << "}";
    }
    out << "]";

    out << ",\"flangeNoise\":[";
    for (size_t i = 0; i < ctx.flange_noises.size(); ++i) {
        if (i) out << ",";
        const auto& row = ctx.flange_noises[i];
        out << "{\"distance\":" << json_number(row.distance)
            << ",\"index\":" << json_value(row.index)
            << ",\"filePath\":\"" << json_escape(row.file_path)
            << "\",\"order\":" << row.order << "}";
    }
    out << "]";

    out << ",\"jointNoise\":[";
    for (size_t i = 0; i < ctx.joint_noises.size(); ++i) {
        if (i) out << ",";
        const auto& row = ctx.joint_noises[i];
        out << "{\"distance\":" << json_number(row.distance)
            << ",\"index\":" << json_value(row.index)
            << ",\"filePath\":\"" << json_escape(row.file_path)
            << "\",\"order\":" << row.order << "}";
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
            << "\",\"order\":" << row.order << "}";
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
            << "\",\"order\":" << row.order << "}";
    }
    out << "]";

    out << ",\"background\":[";
    for (size_t i = 0; i < ctx.backgrounds.size(); ++i) {
        if (i) out << ",";
        append_background_json(out, ctx.backgrounds[i]);
    }
    out << "]";

    out << ",\"adhesion\":[";
    for (size_t i = 0; i < ctx.adhesions.size(); ++i) {
        if (i) out << ",";
        append_adhesion_json(out, ctx.adhesions[i]);
    }
    out << "]";

    out << ",\"cabIlluminance\":[";
    for (size_t i = 0; i < ctx.cab_illuminance.size(); ++i) {
        if (i) out << ",";
        append_cab_illuminance_json(out, ctx.cab_illuminance[i]);
    }
    out << "]";

    out << ",\"fog\":[";
    for (size_t i = 0; i < ctx.fogs.size(); ++i) {
        if (i) out << ",";
        append_fog_json(out, ctx.fogs[i]);
    }
    out << "]";

    out << ",\"speedlimit\":[";
    for (size_t i = 0; i < ctx.speedlimits.size(); ++i) {
        if (i) out << ",";
        out << "{\"distance\":" << json_number(ctx.speedlimits[i].distance)
            << ",\"speed\":" << json_value(ctx.speedlimits[i].speed) << "}";
    }
    out << "]}";
    return out.str();
}

KvDoubleBuffer make_buffer(const Matrix& m) {
    return {m.data.empty() ? nullptr : m.data.data(), m.rows, m.cols};
}

char* copy_c_string(const std::string& s) {
    char* p = static_cast<char*>(std::malloc(s.size() + 1));
    if (!p) return nullptr;
    std::memcpy(p, s.c_str(), s.size() + 1);
    return p;
}

} // namespace

extern "C" {

KV_API void kv_set_log_callback(KvLogCallback callback) {
    g_log_callback = callback;
}

KV_API void* kv_load_map(const char* path, double unit_distance) {
    try {
        if (!path) throw std::runtime_error("path is null");
        auto ctx = std::make_unique<MapContext>();
        ActiveTimingScope active(ctx->timing);
        std::filesystem::path map_path = path_from_utf8(path);
        log_info("loading map " + path_to_utf8(map_path));
        LoadedText loaded = load_header_text(map_path, "BveTs Map ", 2.0);
        ctx->rootpath = loaded.root;
        ctx->rootpath_utf8 = path_to_utf8(loaded.root);
        ctx->current_file_path = path_to_utf8(std::filesystem::absolute(map_path));

        log_info("parsing syntax tree");
        {
            ScopedTimer timer(&ctx->timing.parse_seconds);
            Parser parser(*ctx, loaded.body, map_path);
            parser.parse();
        }

        log_info("sorting parsed IR");
        {
            ScopedTimer timer(&ctx->timing.relocate_seconds);
            relocate(*ctx);
        }
        generate_geometry(*ctx, unit_distance, false, 0.0, 0.0, 0.0);
        log_info(path_to_utf8(map_path.filename()) + " loaded");
        return ctx.release();
    } catch (const std::exception& e) {
        g_last_error = e.what();
        log_error(g_last_error);
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
        g_last_error = e.what();
        log_error(g_last_error);
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

KV_API const char* kv_get_ir_json(void* handle) {
    try {
        if (!handle) throw std::runtime_error("handle is null");
        auto* ctx = static_cast<MapContext*>(handle);
        if (ctx->ir_json_cache.empty()) {
            ScopedTimer timer(&ctx->timing.json_seconds);
            ctx->ir_json_cache = build_ir_json(*ctx);
        }
        if (!ctx->load_timing_logged) {
            log_load_timing(*ctx);
            ctx->load_timing_logged = true;
        }
        return copy_c_string(ctx->ir_json_cache);
    } catch (const std::exception& e) {
        g_last_error = e.what();
        return nullptr;
    }
}

KV_API const char* kv_get_last_error(void) {
    return g_last_error.c_str();
}

KV_API void kv_free(void* handle) {
    delete static_cast<MapContext*>(handle);
}

KV_API void kv_free_string(const char* text) {
    std::free(const_cast<char*>(text));
}

}
