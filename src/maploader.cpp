#define MAPLOADER_EXPORTS
#include "maploader.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cmath>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
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

constexpr double kInf = std::numeric_limits<double>::infinity();
constexpr double kPi = 3.141592653589793238462643383279502884;
KvLogCallback g_log_callback = nullptr;
thread_local std::string g_last_error;

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

    void push(const std::vector<double>& row) {
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
    std::vector<StructurePut> structure_puts;
    std::vector<StructurePut> structure_betweens;
    std::vector<RepeaterEvent> repeaters;
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

    int next_parse_order() {
        return ++parse_order;
    }
};

void add_controlpoint(MapContext& ctx, double value) {
    ctx.controlpoints.push_back(value);
}

void set_distance(MapContext& ctx, double value) {
    ctx.distance = value;
    add_controlpoint(ctx, value);
}

void put_own(MapContext& ctx, const std::string& key, const Value& value, const std::string& flag = "") {
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
    }

private:
    MapContext& ctx_;
    std::string src_;
    std::filesystem::path file_path_;
    size_t pos_ = 0;

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
            ++pos_;
            std::string name = ascii_lower(parse_variable_name());
            expect('=');
            Value value = parse_expression();
            expect(';');
            ctx_.variables[name] = value;
            return;
        }

        size_t save = pos_;
        if (!eof() && is_ident_start(static_cast<unsigned char>(peek()))) {
            std::string first = parse_label();
            std::string first_l = ascii_lower(first);
            pos_ = save;
            if (first_l == "include" && !current_starts_map_element()) {
                parse_label();
                Value path = parse_expression();
                expect(';');
                include_file(as_text(path));
                return;
            }
            if (current_starts_map_element()) {
                parse_map_element();
                expect(';');
                return;
            }
        }

        Value distance = parse_expression();
        expect(';');
        set_distance(ctx_, as_number(distance));
    }

    void include_file(const std::string& path_text) {
        std::filesystem::path child = join_path(ctx_.rootpath, path_text);
        log_info("including " + path_to_utf8(child));
        try {
            LoadedText loaded = load_header_text(child, "BveTs Map ", 2.0);
            std::string previous = ctx_.current_file_path;
            ctx_.current_file_path = path_to_utf8(std::filesystem::absolute(child));
            Parser nested(ctx_, loaded.body, child);
            nested.parse();
            ctx_.current_file_path = previous;
        } catch (const std::exception& e) {
            log_warn(e.what());
        }
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
            if (lower == "distance") return Value::num(ctx_.distance);
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
        } else if (first == "structure") {
            std::vector<Value> args = function.args;
            if (objects.front().has_key) args.insert(args.begin(), objects.front().key);
            dispatch_structure(fn, args);
        } else if (first == "repeater") {
            std::vector<Value> args = function.args;
            if (objects.front().has_key) args.insert(args.begin(), objects.front().key);
            dispatch_repeater(fn, args);
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
        if (fn == "begin") ctx_.speedlimits.push_back({ctx_.distance, a.empty() ? Value::num(0.0) : a[0]});
        else if (fn == "end") ctx_.speedlimits.push_back({ctx_.distance, Value::null()});
    }

    void dispatch_structure(const std::string& fn, const std::vector<Value>& a) {
        if (fn == "load") {
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

    void dispatch_repeater(const std::string& fn, const std::vector<Value>& a) {
        if (fn == "begin" && a.size() >= 11) {
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

double clothoid_dist(double A, double l, char elem) {
    if (A == 0.0 || std::isinf(A)) return elem == 'X' ? l : 0.0;
    double la = l / A;
    if (elem == 'X') {
        return l * (1 - std::pow(la, 4) / 40 + std::pow(la, 8) / 3456 - std::pow(la, 12) / 599040);
    }
    return l * (std::pow(la, 2) / 6 - std::pow(la, 6) / 336 + std::pow(la, 10) / 42240 - std::pow(la, 14) / 9676800);
}

struct CurveResult {
    double x = 0.0;
    double y = 0.0;
    double tau = 0.0;
    double radius = 0.0;
};

CurveResult circular_curve(double R, double theta, double l_intermediate) {
    if (R == 0.0 || std::isinf(R)) {
        auto [x, y] = rotate_xy(l_intermediate, 0.0, theta);
        return {x, y, 0.0, 0.0};
    }
    double tau = l_intermediate / R;
    double x0 = std::fabs(R) * std::sin(l_intermediate / std::fabs(R));
    double y0 = R * (1 - std::cos(l_intermediate / std::fabs(R)));
    auto [x, y] = rotate_xy(x0, y0, theta);
    return {x, y, tau, R};
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
    auto K = [](double x, double R1, double R2, double L0) {
        return (inv_radius(R2) - inv_radius(R1)) / 2.0 *
               (std::sin(kPi / L0 * x - kPi / 2.0) + 1.0) + inv_radius(R1);
    };
    if (l_intermediate <= 0.0) {
        return {0.0, 0.0, 0.0, r1 == 0.0 ? kInf : r1};
    }
    if (L == 0.0) return {0.0, 0.0, 0.0, r2};
    if (l_intermediate / 5.0 <= dL) dL = l_intermediate / 5.0;
    int n = static_cast<int>(l_intermediate / dL) + 1;
    if (n < 2) n = 2;
    std::vector<double> xs(n);
    std::vector<double> tau(n, 0.0);
    std::vector<double> X(n, 0.0);
    std::vector<double> Y(n, 0.0);
    for (int i = 0; i < n; ++i) xs[i] = l_intermediate * i / (n - 1);
    for (int i = 1; i < n; ++i) {
        double dx = xs[i] - xs[i - 1];
        tau[i] = tau[i - 1] + (K(xs[i - 1], r1, r2, L) + K(xs[i], r1, r2, L)) * dx / 2.0;
        X[i] = X[i - 1] + (std::cos(tau[i - 1]) + std::cos(tau[i])) * dx / 2.0;
        Y[i] = Y[i - 1] + (std::sin(tau[i - 1]) + std::sin(tau[i])) * dx / 2.0;
    }
    double k = K(l_intermediate, r1, r2, L);
    double r = k != 0.0 ? 1.0 / k : kInf;
    return {X.back(), Y.back(), tau.back(), r};
}

CurveResult transition_curve(double L, double r1, double r2, double theta,
                             const std::string& func, double l_intermediate) {
    r1 = r1 == 0.0 ? kInf : r1;
    r2 = r2 == 0.0 ? kInf : r2;
    r1 = std::fabs(r1) > 1e6 ? kInf : r1;
    r2 = std::fabs(r2) > 1e6 ? kInf : r2;
    double tau1 = 0.0;
    double turn = 0.0;
    double rl = 0.0;
    double x0 = 0.0, y0 = 0.0, x1 = 0.0, y1 = 0.0;

    if (func == "sin") {
        HalfSinResult out0 = halfsin_intermediate(L, r1, r2, 0.0);
        HalfSinResult out1 = halfsin_intermediate(L, r1, r2, l_intermediate);
        turn = out1.tau;
        rl = out1.radius;
        x0 = out0.x; y0 = out0.y; x1 = out1.x; y1 = out1.y;
    } else {
        double ratio = 0.0;
        if (std::isinf(r1) && !std::isinf(r2)) ratio = 0.0;
        else if (!std::isinf(r1) && std::isinf(r2)) ratio = std::copysign(kInf, r2 / r1);
        else if (std::isinf(r1) && std::isinf(r2)) ratio = 1.0;
        else ratio = r2 / r1;
        double denom = 1.0 - ratio;
        double L0 = denom == 0.0 ? 0.0 : L * (1.0 - (1.0 / denom));
        double denom_r = inv_radius(r1) + (inv_radius(r2) - inv_radius(r1)) / L * l_intermediate;
        rl = denom_r != 0.0 ? 1.0 / denom_r : kInf;
        double A = !std::isinf(r1) ? std::sqrt(std::fabs(L0) * std::fabs(r1))
                                   : std::sqrt(std::fabs(L - L0) * std::fabs(r2));
        if (A == 0.0 || std::isnan(A)) {
            auto [x, y] = rotate_xy(l_intermediate, 0.0, theta);
            return {x, y, 0.0, 0.0};
        }
        if (inv_radius(r1) < inv_radius(r2)) {
            tau1 = std::pow(A / r1, 2) / 2.0;
            double d0 = A * A / r1;
            double d1 = l_intermediate + A * A / r1;
            turn = (std::pow(l_intermediate - L0, 2) - std::pow(L0, 2)) / (2 * A * A);
            x0 = clothoid_dist(A, d0, 'X');
            y0 = clothoid_dist(A, d0, 'Y');
            x1 = clothoid_dist(A, d1, 'X');
            y1 = clothoid_dist(A, d1, 'Y');
        } else {
            tau1 = -std::pow(A / r1, 2) / 2.0;
            double d0 = -A * A / r1;
            double d1 = l_intermediate + (-A * A / r1);
            turn = -(std::pow(l_intermediate - L0, 2) - std::pow(L0, 2)) / (2 * A * A);
            x0 = clothoid_dist(A, d0, 'X');
            y0 = -clothoid_dist(A, d0, 'Y');
            x1 = clothoid_dist(A, d1, 'X');
            y1 = -clothoid_dist(A, d1, 'Y');
        }
    }

    auto [rx, ry] = rotate_xy(x1 - x0, y1 - y0, -tau1);
    auto [fx, fy] = rotate_xy(rx, ry, theta);
    return {fx, fy, turn, std::fabs(rl) < 1e6 ? rl : 0.0};
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

void generate_othertrack(MapContext& ctx, const std::string& trackkey) {
    const auto& data = ctx.othertrack.at(trackkey);
    if (data.empty() || ctx.owntrack_buffer.rows == 0) return;

    std::map<std::string, TrackPointer> ptrs;
    for (const std::string& key : {"x.position", "x.radius", "y.position", "y.radius",
                                   "interpolate_func", "cant", "center", "gauge"}) {
        ptrs.emplace(key, TrackPointer(data, key));
    }

    struct PosSet {
        std::map<std::string, double> last;
        std::map<std::string, double> next;
        std::string func_last = "line";
        std::string func_next = "line";
    } pos;
    for (const std::string& key : {"x.position", "x.radius", "x.distance", "y.position",
                                   "y.radius", "y.distance", "cant", "center", "gauge"}) {
        pos.last[key] = 0.0;
        pos.next[key] = 0.0;
    }
    for (auto& kv : ptrs) {
        if (kv.second.next() >= 0) {
            const auto& e = kv.second.event(kv.second.next());
            if (kv.first == "interpolate_func") {
                pos.func_last = pos.func_next = e.value.is_continue() ? "line" : as_text(e.value);
            } else {
                double v = e.value.is_continue() ? 0.0 : as_number(e.value);
                pos.last[kv.first] = v;
                pos.next[kv.first] = v;
            }
        }
    }

    CantProcessor cant_gen(TrackPointer(data, "cant"), data, pos.last["cant"]);
    Matrix result;
    result.clear(8);

    double min_dist = data.front().distance;
    for (const auto& e : data) min_dist = std::min(min_dist, e.distance);

    for (size_t r = 0; r < ctx.owntrack_buffer.rows; ++r) {
        const double* element = &ctx.owntrack_buffer.data[r * ctx.owntrack_buffer.cols];
        double dist = element[0];
        if (min_dist > dist) continue;

        for (const std::string& key : {"x.position", "x.radius", "y.position", "y.radius"}) {
            auto& p = ptrs.at(key);
            while (p.over_nextpoint(dist)) {
                p.seeknext();
                pos.last[key] = pos.next[key];
                if (p.next() >= 0) {
                    const auto& e = p.event(p.next());
                    pos.next[key] = e.value.is_continue() ? pos.last[key] : as_number(e.value);
                }
            }
        }

        for (const std::string& key : {"interpolate_func", "center", "gauge"}) {
            auto& p = ptrs.at(key);
            while (p.on_nextpoint(dist)) {
                p.seeknext();
                if (key == "interpolate_func") {
                    pos.func_last = pos.func_next;
                    if (p.next() >= 0) {
                        const auto& e = p.event(p.next());
                        pos.func_next = e.value.is_continue() ? pos.func_last : as_text(e.value);
                    }
                } else {
                    pos.last[key] = pos.next[key];
                    if (p.next() >= 0) {
                        const auto& e = p.event(p.next());
                        pos.next[key] = e.value.is_continue() ? pos.last[key] : as_number(e.value);
                    }
                }
            }
        }

        double out_x = 0.0, out_y = 0.0;
        auto& xptr = ptrs.at("x.position");
        if (xptr.last() >= 0 && xptr.next() >= 0) {
            pos.last["x.distance"] = xptr.event(xptr.last()).distance;
            pos.next["x.distance"] = xptr.event(xptr.next()).distance;
            double rel = relative_position(pos.next["x.distance"] - pos.last["x.distance"],
                                           pos.last["x.radius"], pos.last["x.position"],
                                           pos.next["x.position"], dist - pos.last["x.distance"]);
            auto [rx, ry] = rotate_xy(0.0, rel, element[4]);
            out_x = rx + element[1];
            out_y = ry + element[2];
        } else {
            double rel = pos.last["x.position"];
            out_x = -std::sin(element[4]) * rel + element[1];
            out_y = std::cos(element[4]) * rel + element[2];
        }

        double out_z = 0.0;
        auto& yptr = ptrs.at("y.position");
        if (yptr.last() >= 0 && yptr.next() >= 0) {
            pos.last["y.distance"] = yptr.event(yptr.last()).distance;
            pos.next["y.distance"] = yptr.event(yptr.next()).distance;
            double rel = relative_position(pos.next["y.distance"] - pos.last["y.distance"],
                                           pos.last["y.radius"], pos.last["y.position"],
                                           pos.next["y.position"], dist - pos.last["y.distance"]);
            out_z = rel + element[3];
        } else {
            out_z = pos.last["y.position"] + element[3];
        }

        double cant = cant_gen.process(dist, pos.func_last);
        result.push({dist, out_x, out_y, out_z, pos.func_last == "sin" ? 0.0 : 1.0,
                     cant, pos.last["center"], pos.last["gauge"]});
    }
    ctx.othertrack_buffers[trackkey] = std::move(result);
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
    std::stable_sort(ctx.speedlimits.begin(), ctx.speedlimits.end(), by_distance);
    std::stable_sort(ctx.station_puts.begin(), ctx.station_puts.end(), by_distance);
}

void build_structure_put_buffer(MapContext& ctx) {
    ctx.structure_put_buffer.clear(10);
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
    generate_owntrack(ctx, unitdist, has_arb, arb_start, arb_end, arb_step);
    generate_curveradius(ctx);
    for (const auto& key : ctx.othertrack_order) {
        generate_othertrack(ctx, key);
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

void append_station_put_json(std::ostringstream& out, const StationPut& row) {
    out << "{\"distance\":" << json_number(row.distance)
        << ",\"stationKey\":" << json_value(row.station_key)
        << ",\"door\":" << json_value(row.door)
        << ",\"margin1\":" << json_value(row.margin1)
        << ",\"margin2\":" << json_value(row.margin2)
        << ",\"order\":" << row.order << "}";
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
        std::filesystem::path map_path = path_from_utf8(path);
        log_info("loading map " + path_to_utf8(map_path));
        LoadedText loaded = load_header_text(map_path, "BveTs Map ", 2.0);
        ctx->rootpath = loaded.root;
        ctx->rootpath_utf8 = path_to_utf8(loaded.root);
        ctx->current_file_path = path_to_utf8(std::filesystem::absolute(map_path));

        log_info("parsing syntax tree");
        Parser parser(*ctx, loaded.body, map_path);
        parser.parse();

        log_info("sorting parsed IR");
        relocate(*ctx);
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
        if (ctx->ir_json_cache.empty()) ctx->ir_json_cache = build_ir_json(*ctx);
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
