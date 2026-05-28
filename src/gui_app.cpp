#pragma execution_character_set("utf-8")

#include "maploader.h"
#include "multilanguage.h"

#include "imgui.h"
#include "imgui_internal.h"
#include "imgui_impl_dx11.h"
#include "imgui_impl_win32.h"
#include "implot.h"

#include <windows.h>
#include <commdlg.h>
#include <d3d11.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <shlobj.h>
#include <wincodec.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace {

template <typename T>
void release_com(T*& p) {
    if (p) {
        p->Release();
        p = nullptr;
    }
}

void set_crosshair_cursor() {
    ::SetCursor(::LoadCursor(nullptr, IDC_CROSS));
}

void set_move_cursor() {
    ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
    ::SetCursor(::LoadCursor(nullptr, IDC_SIZEALL));
}

std::wstring utf8_to_wide(const std::string& text) {
    if (text.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (n <= 0) return {};
    std::wstring out(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), n);
    return out;
}

std::string wide_to_utf8(const std::wstring& text) {
    if (text.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    if (n <= 0) return {};
    std::string out(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), n, nullptr, nullptr);
    return out;
}

std::string narrow_path(const std::filesystem::path& path) {
#if defined(__cpp_char8_t)
    auto s = path.u8string();
    return std::string(reinterpret_cast<const char*>(s.data()), s.size());
#else
    return path.u8string();
#endif
}

std::string format_double(double value, int precision = 6) {
    if (!std::isfinite(value)) return "";
    std::ostringstream out;
    out << std::fixed << std::setprecision(precision) << value;
    std::string s = out.str();
    size_t dot = s.find('.');
    if (dot != std::string::npos) {
        while (s.size() > dot + 1 && s.back() == '0') s.pop_back();
        if (s.size() == dot + 1) s.pop_back();
    }
    return s;
}

double round_to_100(double value) {
    return std::round(value / 100.0) * 100.0;
}

std::string sanitize_filename(std::string text) {
    if (text.empty()) text = "root";
    for (char& ch : text) {
        if (ch == '\\' || ch == '/' || ch == ':' || ch == '*' || ch == '?' ||
            ch == '"' || ch == '<' || ch == '>' || ch == '|') {
            ch = '_';
        }
    }
    return text;
}

namespace mini_json {

struct Value {
    enum class Type { Null, Bool, Number, String, Array, Object };
    Type type = Type::Null;
    bool boolean = false;
    double number = 0.0;
    std::string string;
    std::vector<Value> array;
    std::map<std::string, Value> object;

    bool is_null() const { return type == Type::Null; }
    bool is_number() const { return type == Type::Number; }
    bool is_string() const { return type == Type::String; }
    bool is_array() const { return type == Type::Array; }
    bool is_object() const { return type == Type::Object; }

    const Value& at(const std::string& key) const {
        static Value empty;
        auto it = object.find(key);
        return it == object.end() ? empty : it->second;
    }

    std::string scalar_text() const {
        if (type == Type::String) return string;
        if (type == Type::Number) return format_double(number);
        if (type == Type::Bool) return boolean ? "true" : "false";
        return "";
    }
};

class Parser {
public:
    explicit Parser(const std::string& source) : s_(source) {}

    Value parse() {
        skip_ws();
        Value v = parse_value();
        skip_ws();
        return v;
    }

private:
    const std::string& s_;
    size_t p_ = 0;

    bool eof() const { return p_ >= s_.size(); }
    char peek() const { return eof() ? '\0' : s_[p_]; }
    char get() { return eof() ? '\0' : s_[p_++]; }

    void skip_ws() {
        while (!eof() && std::isspace(static_cast<unsigned char>(peek()))) ++p_;
    }

    static void append_utf8(std::string& out, unsigned codepoint) {
        if (codepoint <= 0x7f) {
            out.push_back(static_cast<char>(codepoint));
        } else if (codepoint <= 0x7ff) {
            out.push_back(static_cast<char>(0xc0 | ((codepoint >> 6) & 0x1f)));
            out.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
        } else if (codepoint <= 0xffff) {
            out.push_back(static_cast<char>(0xe0 | ((codepoint >> 12) & 0x0f)));
            out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3f)));
            out.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
        } else {
            out.push_back(static_cast<char>(0xf0 | ((codepoint >> 18) & 0x07)));
            out.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3f)));
            out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3f)));
            out.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
        }
    }

    unsigned parse_hex4() {
        unsigned value = 0;
        for (int i = 0; i < 4; ++i) {
            char c = get();
            value <<= 4;
            if (c >= '0' && c <= '9') value |= static_cast<unsigned>(c - '0');
            else if (c >= 'a' && c <= 'f') value |= static_cast<unsigned>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') value |= static_cast<unsigned>(c - 'A' + 10);
        }
        return value;
    }

    Value parse_string() {
        Value v;
        v.type = Value::Type::String;
        if (get() != '"') return v;
        while (!eof()) {
            char c = get();
            if (c == '"') break;
            if (c == '\\') {
                char esc = get();
                switch (esc) {
                    case '"': v.string.push_back('"'); break;
                    case '\\': v.string.push_back('\\'); break;
                    case '/': v.string.push_back('/'); break;
                    case 'b': v.string.push_back('\b'); break;
                    case 'f': v.string.push_back('\f'); break;
                    case 'n': v.string.push_back('\n'); break;
                    case 'r': v.string.push_back('\r'); break;
                    case 't': v.string.push_back('\t'); break;
                    case 'u': append_utf8(v.string, parse_hex4()); break;
                    default: v.string.push_back(esc); break;
                }
            } else {
                v.string.push_back(c);
            }
        }
        return v;
    }

    Value parse_number() {
        size_t begin = p_;
        if (peek() == '-') ++p_;
        while (std::isdigit(static_cast<unsigned char>(peek()))) ++p_;
        if (peek() == '.') {
            ++p_;
            while (std::isdigit(static_cast<unsigned char>(peek()))) ++p_;
        }
        if (peek() == 'e' || peek() == 'E') {
            ++p_;
            if (peek() == '+' || peek() == '-') ++p_;
            while (std::isdigit(static_cast<unsigned char>(peek()))) ++p_;
        }
        Value v;
        v.type = Value::Type::Number;
        v.number = std::strtod(s_.c_str() + begin, nullptr);
        return v;
    }

    Value parse_array() {
        Value v;
        v.type = Value::Type::Array;
        get();
        skip_ws();
        if (peek() == ']') {
            get();
            return v;
        }
        while (!eof()) {
            v.array.push_back(parse_value());
            skip_ws();
            char c = get();
            if (c == ']') break;
            if (c != ',') break;
            skip_ws();
        }
        return v;
    }

    Value parse_object() {
        Value v;
        v.type = Value::Type::Object;
        get();
        skip_ws();
        if (peek() == '}') {
            get();
            return v;
        }
        while (!eof()) {
            Value key = parse_string();
            skip_ws();
            if (get() != ':') break;
            skip_ws();
            v.object[key.string] = parse_value();
            skip_ws();
            char c = get();
            if (c == '}') break;
            if (c != ',') break;
            skip_ws();
        }
        return v;
    }

    Value parse_literal(const char* literal, Value value) {
        while (*literal) {
            if (get() != *literal++) break;
        }
        return value;
    }

    Value parse_value() {
        skip_ws();
        char c = peek();
        if (c == '"') return parse_string();
        if (c == '[') return parse_array();
        if (c == '{') return parse_object();
        if (c == '-' || std::isdigit(static_cast<unsigned char>(c))) return parse_number();
        if (s_.compare(p_, 4, "true") == 0) {
            Value v; v.type = Value::Type::Bool; v.boolean = true; return parse_literal("true", v);
        }
        if (s_.compare(p_, 5, "false") == 0) {
            Value v; v.type = Value::Type::Bool; v.boolean = false; return parse_literal("false", v);
        }
        Value nullv;
        if (s_.compare(p_, 4, "null") == 0) return parse_literal("null", nullv);
        return nullv;
    }
};

} // namespace mini_json

std::string table_cell_text(const mini_json::Value& value) {
    if (value.is_array()) {
        std::string text;
        for (size_t i = 0; i < value.array.size(); ++i) {
            if (i) text += ", ";
            text += table_cell_text(value.array[i]);
        }
        return text;
    }
    return value.scalar_text();
}

struct Matrix {
    std::vector<double> data;
    size_t rows = 0;
    size_t cols = 0;

    double at(size_t r, size_t c) const {
        return data[r * cols + c];
    }

    bool empty() const {
        return rows == 0 || cols == 0;
    }
};

Matrix copy_buffer(KvDoubleBuffer buffer) {
    Matrix m;
    m.rows = buffer.rows;
    m.cols = buffer.cols;
    if (buffer.data && buffer.rows > 0 && buffer.cols > 0) {
        m.data.assign(buffer.data, buffer.data + buffer.rows * buffer.cols);
    }
    return m;
}

struct TrackEvent {
    double distance = 0.0;
    std::string key;
    std::string flag;
    bool value_number = false;
    double number = 0.0;
    std::string text;
};

struct OtherTrack {
    std::string key;
    Matrix points;
    bool visible = false;
    double range_min = 0.0;
    double range_max = 0.0;
    ImVec4 color = ImVec4(0.2f, 0.7f, 1.0f, 1.0f);
};

struct Station {
    std::string key;
    std::string name;
    double distance = 0.0;
    double mileage = 0.0;
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

struct SpeedLimit {
    double distance = 0.0;
    bool has_speed = false;
    double speed = 0.0;
};

struct TableRow {
    std::map<std::string, std::string> cells;
};

struct TableColumnDef {
    const char* key;
    const char* header;
    float width = 0.0f;
};

struct CachedTableRow {
    std::vector<std::string> cells;
    std::string open_path;
};

struct TableUiCache {
    bool valid = false;
    float font_size = 0.0f;
    float cell_padding_x = 0.0f;
    std::vector<CachedTableRow> station_rows;
    std::vector<CachedTableRow> structure_rows;
    std::vector<CachedTableRow> repeater_rows;
    float structure_file_path_width = 200.0f;
    float repeater_distance_width = 110.0f;
    float repeater_interval_width = 70.0f;
    float repeater_file_path_width = 200.0f;
};

static const char* kStructureColumns[] = {
    "distance", "method", "structureKey", "trackKey", "x", "y", "z", "rx", "ry", "rz",
    "tilt", "span", "trackKey1", "trackKey2", "flag", "filePath"
};
constexpr int kStructureFilePathColumn = IM_ARRAYSIZE(kStructureColumns) - 1;

static const TableColumnDef kRepeaterColumns[] = {
    {"rowNumber", "#", 40.0f},
    {"distance", "distance", 110.0f},
    {"method", "method", 0.0f},
    {"repeaterKey", "repeaterKey", 0.0f},
    {"trackKey", "trackKey", 0.0f},
    {"x", "x", 0.0f},
    {"y", "y", 0.0f},
    {"z", "z", 0.0f},
    {"rx", "rx", 0.0f},
    {"ry", "ry", 0.0f},
    {"rz", "rz", 0.0f},
    {"tilt", "tilt", 0.0f},
    {"span", "span", 0.0f},
    {"interval", "interval", 0.0f},
    {"structureKeys", "structureKeys", 120.0f},
    {"filePath", "filePath", 200.0f},
};
constexpr int kRepeaterDistanceColumn = 1;
constexpr int kRepeaterIntervalColumn = 13;
constexpr int kRepeaterFilePathColumn = IM_ARRAYSIZE(kRepeaterColumns) - 1;

static const TableColumnDef kStationListColumns[] = {
    {"rowNumber", "#", 40.0f},
    {"dist", "dist", 70.0f},
    {"posKey", "key", 80.0f},
    {"door", "door", 55.0f},
    {"margin1", "back", 65.0f},
    {"margin2", "front", 65.0f},
    {"stationKey", "stKey", 80.0f},
    {"stationName", "name", 120.0f},
    {"arrivalTime", "arr", 70.0f},
    {"depertureTime", "dep", 70.0f},
    {"stoppageTime", "stop", 60.0f},
    {"defaultTime", "def", 70.0f},
    {"signalFlag", "sig", 55.0f},
    {"alightingTime", "alight", 65.0f},
    {"passengers", "pax", 60.0f},
    {"arrivalSoundKey", "arrSnd", 85.0f},
    {"depertureSoundKey", "depSnd", 85.0f},
    {"doorReopen", "reopen", 70.0f},
    {"stuckInDoor", "stuck", 65.0f},
};

std::string display_name_from_path(const std::string& path);

const std::string& table_cell(const TableRow& row, const std::string& key) {
    static const std::string empty;
    auto it = row.cells.find(key);
    return it == row.cells.end() ? empty : it->second;
}

double table_cell_number(const TableRow& row, const std::string& key) {
    const std::string& text = table_cell(row, key);
    if (text.empty()) return 0.0;
    char* end = nullptr;
    double value = std::strtod(text.c_str(), &end);
    return end == text.c_str() ? 0.0 : value;
}

std::string format_distance_range(const std::string& start, const std::string& end) {
    return start + "~" + end;
}

std::string format_changed_distance(const std::string& start, int next_display_index) {
    return format_distance_range(start, "Changed to #" + std::to_string(next_display_index));
}

std::string format_repeater_file_path(const std::string& begin_path, const std::string& end_path = {}) {
    std::string begin_name = display_name_from_path(begin_path);
    std::string end_name = display_name_from_path(end_path);
    if (!end_path.empty() && !begin_path.empty() && end_path != begin_path) {
        return "Begin:" + begin_name + ", End:" + end_name;
    }
    return begin_path.empty() ? end_name : begin_name;
}

std::vector<TableRow> merged_repeater_rows(const std::vector<TableRow>& data) {
    std::vector<TableRow> ordered_rows = data;
    std::stable_sort(ordered_rows.begin(), ordered_rows.end(), [](const TableRow& a, const TableRow& b) {
        return table_cell_number(a, "order") < table_cell_number(b, "order");
    });

    std::vector<TableRow> merged_rows;
    std::map<std::string, size_t> open_rows;
    int display_index = 1;

    for (const auto& row : ordered_rows) {
        const std::string& key = table_cell(row, "repeaterKey");
        const std::string& method = table_cell(row, "method");

        if (method == "Begin" || method == "Begin0") {
            auto open_it = open_rows.find(key);
            if (open_it != open_rows.end()) {
                TableRow& previous = merged_rows[open_it->second];
                previous.cells["distance"] = format_changed_distance(table_cell(previous, "_beginDistance"), display_index);
                previous.cells["filePath"] = format_repeater_file_path(table_cell(previous, "_beginFilePath"));
                previous.cells["_openFilePath"] = table_cell(previous, "_beginFilePath");
                open_rows.erase(open_it);
            }

            TableRow new_row = row;
            const std::string& begin_distance = table_cell(row, "distance");
            const std::string& begin_file_path = table_cell(row, "filePath");
            new_row.cells["rowNumber"] = std::to_string(display_index);
            new_row.cells["_beginDistance"] = begin_distance;
            new_row.cells["_beginFilePath"] = begin_file_path;
            new_row.cells["_openFilePath"] = begin_file_path;
            new_row.cells["distance"] = format_distance_range(begin_distance, "NO END");
            new_row.cells["filePath"] = format_repeater_file_path(begin_file_path);
            open_rows[key] = merged_rows.size();
            merged_rows.push_back(std::move(new_row));
            ++display_index;
        } else if (method == "End") {
            auto open_it = open_rows.find(key);
            if (open_it != open_rows.end()) {
                TableRow& begin_row = merged_rows[open_it->second];
                const std::string& begin_file_path = table_cell(begin_row, "_beginFilePath");
                const std::string& end_file_path = table_cell(row, "filePath");
                begin_row.cells["distance"] = format_distance_range(table_cell(begin_row, "_beginDistance"), table_cell(row, "distance"));
                begin_row.cells["filePath"] = format_repeater_file_path(begin_file_path, end_file_path);
                begin_row.cells["_openFilePath"] = begin_file_path.empty() ? end_file_path : begin_file_path;
                open_rows.erase(open_it);
            }
        }
    }

    for (auto& row : merged_rows) {
        row.cells.erase("_beginDistance");
        row.cells.erase("_beginFilePath");
    }
    return merged_rows;
}

struct MapModel {
    std::string path;
    Matrix own;
    Matrix curve;
    std::vector<OtherTrack> other_tracks;
    std::vector<Station> stations;
    std::vector<TrackEvent> own_events;
    std::vector<SpeedLimit> speedlimits;
    std::vector<double> controlpoints;
    std::vector<TableRow> station_list_rows;
    std::vector<TableRow> structures;
    std::vector<TableRow> structures_between;
    std::vector<TableRow> repeaters;
    double distance_origin = 0.0;
    double height_origin = 0.0;
    double origin_angle = 0.0;
    double default_min = 0.0;
    double default_max = 0.0;
    double cp_default_min = 0.0;
    double cp_default_max = 0.0;
    double cp_arb[3] = {0.0, 0.0, 25.0};
    bool has_cp_arb = false;
};

struct View2D {
    double cx = 0.0;
    double cy = 0.0;
    double scale = 1.0;
    double rotation = 0.0;
    bool fitted = false;
    bool dragging = false;
    bool rotating = false;
    ImVec2 last_mouse = ImVec2(0, 0);

    ImVec2 world_to_screen(double x, double y, ImVec2 origin, ImVec2 size) const {
        double dx = x - cx;
        double dy = y - cy;
        double c = std::cos(rotation);
        double s = std::sin(rotation);
        double rx = c * dx - s * dy;
        double ry = s * dx + c * dy;
        return ImVec2(origin.x + size.x * 0.5f + static_cast<float>(rx * scale),
                      origin.y + size.y * 0.5f + static_cast<float>(ry * scale));
    }

    ImVec2 screen_to_world(ImVec2 p, ImVec2 origin, ImVec2 size) const {
        double rx = (p.x - origin.x - size.x * 0.5) / scale;
        double ry = (p.y - origin.y - size.y * 0.5) / scale;
        double c = std::cos(rotation);
        double s = std::sin(rotation);
        return ImVec2(static_cast<float>(c * rx + s * ry + cx),
                      static_cast<float>(-s * rx + c * ry + cy));
    }

    void pan_by_screen_delta(ImVec2 delta) {
        double c = std::cos(rotation);
        double s = std::sin(rotation);
        double wx = -(c * delta.x / scale + s * delta.y / scale);
        double wy = (s * delta.x / scale - c * delta.y / scale);
        cx += wx;
        cy += wy;
    }

    void fit(double xmin, double ymin, double xmax, double ymax, ImVec2 size) {
        double dx = std::max(xmax - xmin, 1e-6);
        double dy = std::max(ymax - ymin, 1e-6);
        cx = (xmin + xmax) * 0.5;
        cy = (ymin + ymax) * 0.5;
        scale = std::max(0.001, std::min(size.x / dx, size.y / dy) * 0.88);
        fitted = true;
    }
};

struct TrackPoint {
    double d = 0.0;
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    double theta = 0.0;
    double radius = 0.0;
    double gradient = 0.0;
};

struct Section {
    double start = 0.0;
    double end = 0.0;
    double value = 0.0;
};

struct PlanStation {
    Station station;
    double x = 0.0;
    double y = 0.0;
};

struct PlanSpeed {
    double x = 0.0;
    double y = 0.0;
    double theta = 0.0;
    bool has_speed = false;
    double speed = 0.0;
};

struct PlanOther {
    std::string key;
    std::vector<TrackPoint> points;
    ImVec4 color;
};

struct PlanData {
    std::vector<TrackPoint> own;
    std::vector<PlanOther> other;
    std::vector<PlanStation> stations;
    std::vector<PlanSpeed> speedlimits;
    std::vector<Section> curve_sections;
    std::vector<Section> transition_sections;
    double origin_angle = 0.0;
    double xmin = -1.0;
    double ymin = -1.0;
    double xmax = 1.0;
    double ymax = 1.0;
};

struct ProfileOther {
    std::string key;
    std::vector<double> x;
    std::vector<double> y;
    ImVec4 color;
};

struct LabelPoint {
    double x = 0.0;
    double y = 0.0;
    std::string text;
};

struct ProfileData {
    std::vector<double> own_x;
    std::vector<double> own_y;
    std::vector<double> curve_x;
    std::vector<double> curve_y;
    std::vector<ProfileOther> other;
    std::vector<Station> stations;
    std::vector<LabelPoint> gradient_points;
    std::vector<LabelPoint> gradient_labels;
    std::vector<LabelPoint> radius_labels;
    double ymin = -5.0;
    double ymax = 5.0;
};

struct TextureImage {
    ID3D11ShaderResourceView* srv = nullptr;
    std::vector<unsigned char> pixels_rgba;
    int width = 0;
    int height = 0;
    double brightness = 100.0;
    std::string path;

    void release() {
        release_com(srv);
        pixels_rgba.clear();
        width = height = 0;
        path.clear();
    }
};

struct LogLine {
    std::string text;
    int severity = 0;
};

class App;
App* g_app = nullptr;
HWND g_main_hwnd = nullptr;
constexpr UINT kAppWakeMessage = WM_APP + 1;
constexpr float kDefaultFontSize = 18.0f;
constexpr float kMinFontSize = 6.0f;
constexpr float kMaxFontSize = 32.0f;
constexpr float kDefaultUiComponentSize = 100.0f;
constexpr float kMinUiComponentSize = 50.0f;
constexpr float kMaxUiComponentSize = 200.0f;
constexpr float kDefaultStationMarkerSize = 4.0f;
constexpr float kMinStationMarkerSize = 1.0f;
constexpr float kMaxStationMarkerSize = 16.0f;
constexpr size_t kMaxRecentMaps = 10;

void wake_main_window() {
    if (g_main_hwnd) PostMessageW(g_main_hwnd, kAppWakeMessage, 0, 0);
}

std::string trim_ascii(const std::string& text) {
    size_t begin = 0;
    while (begin < text.size() && std::isspace(static_cast<unsigned char>(text[begin]))) ++begin;
    size_t end = text.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1]))) --end;
    return text.substr(begin, end - begin);
}

std::string ascii_lower(std::string text) {
    for (char& ch : text) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return text;
}

std::string normalized_storage_path(const std::string& path) {
    if (trim_ascii(path).empty()) return {};
    try {
        std::filesystem::path p = utf8_to_wide(path);
        std::error_code ec;
        std::filesystem::path abs = std::filesystem::absolute(p, ec);
        if (ec) abs = p;
        return narrow_path(abs.lexically_normal());
    } catch (...) {
        return path;
    }
}

std::string normalized_path_key(const std::string& path) {
    if (trim_ascii(path).empty()) return {};
    try {
        std::filesystem::path p = utf8_to_wide(path);
        std::error_code ec;
        std::filesystem::path abs = std::filesystem::absolute(p, ec);
        if (ec) abs = p;
        std::filesystem::path canonical = std::filesystem::weakly_canonical(abs, ec);
        if (!ec) abs = canonical;
        return ascii_lower(narrow_path(abs.lexically_normal()));
    } catch (...) {
        return ascii_lower(trim_ascii(path));
    }
}

std::string display_name_from_path(const std::string& path) {
    try {
        std::filesystem::path p = utf8_to_wide(path);
        std::string name = narrow_path(p.filename());
        return name.empty() ? path : name;
    } catch (...) {
        return path;
    }
}

void open_parent_directory_in_explorer(const std::string& file_path) {
    if (trim_ascii(file_path).empty()) return;
    try {
        std::filesystem::path path = utf8_to_wide(file_path);
        std::error_code ec;
        std::filesystem::path abs = std::filesystem::absolute(path, ec);
        if (!ec) path = abs;
        std::filesystem::path dir = path.parent_path();
        if (dir.empty()) return;
        ShellExecuteW(nullptr, L"open", dir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    } catch (...) {
    }
}

void render_file_path_cell_with_context(const std::string& display_text, const std::string& open_path, const std::string& menu_label) {
    if (display_text.empty()) return;

    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImVec2 text_size = ImGui::CalcTextSize(display_text.c_str());
    ImVec2 item_size(
        std::max(1.0f, ImGui::GetContentRegionAvail().x),
        std::max(ImGui::GetTextLineHeight(), text_size.y));
    ImGui::InvisibleButton("file_path_cell", item_size);
    if (ImGui::IsItemHovered()) {
        ImGui::GetWindowDrawList()->AddRectFilled(pos, ImVec2(pos.x + item_size.x, pos.y + item_size.y), ImGui::GetColorU32(ImGuiCol_HeaderHovered));
    }
    ImGui::GetWindowDrawList()->AddText(pos, ImGui::GetColorU32(ImGuiCol_Text), display_text.c_str());

    if (ImGui::BeginPopupContextItem("file_path_context", ImGuiPopupFlags_MouseButtonRight)) {
        bool can_open = !trim_ascii(open_path).empty();
        ImGui::BeginDisabled(!can_open);
        if (ImGui::MenuItem(menu_label.c_str())) {
            open_parent_directory_in_explorer(open_path);
        }
        ImGui::EndDisabled();
        ImGui::EndPopup();
    }
}

void expand_width_for_text(float& width, const std::string& text) {
    if (text.empty()) return;
    float text_width = ImGui::CalcTextSize(text.c_str()).x + ImGui::GetStyle().CellPadding.x * 2.0f + 12.0f;
    width = std::max(width, text_width);
}

float clamp_font_size(float value) {
    if (!std::isfinite(value)) return kDefaultFontSize;
    return std::clamp(value, kMinFontSize, kMaxFontSize);
}

float clamp_ui_component_size(float value) {
    if (!std::isfinite(value)) return kDefaultUiComponentSize;
    return std::clamp(value, kMinUiComponentSize, kMaxUiComponentSize);
}

float clamp_station_marker_size(float value) {
    if (!std::isfinite(value)) return kDefaultStationMarkerSize;
    return std::clamp(value, kMinStationMarkerSize, kMaxStationMarkerSize);
}

ImVec4 default_theme_color() {
    return ImVec4(0.26f, 0.59f, 0.98f, 1.0f);
}

float clamp_color_component(float value) {
    if (!std::isfinite(value)) return 0.0f;
    return std::clamp(value, 0.0f, 1.0f);
}

ImVec4 clamp_theme_color(ImVec4 color) {
    color.x = clamp_color_component(color.x);
    color.y = clamp_color_component(color.y);
    color.z = clamp_color_component(color.z);
    color.w = 1.0f;
    return color;
}

int color_component_to_byte(float value) {
    return std::clamp(static_cast<int>(std::round(clamp_color_component(value) * 255.0f)), 0, 255);
}

std::string theme_color_to_string(const ImVec4& color) {
    ImVec4 c = clamp_theme_color(color);
    std::ostringstream out;
    out << std::uppercase << std::hex << std::setfill('0')
        << std::setw(2) << color_component_to_byte(c.x)
        << std::setw(2) << color_component_to_byte(c.y)
        << std::setw(2) << color_component_to_byte(c.z);
    return out.str();
}

int hex_digit(char ch) {
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
    return -1;
}

std::optional<ImVec4> parse_theme_color(const std::string& text) {
    std::string value = trim_ascii(text);
    if (value.empty()) return std::nullopt;
    bool had_hash_prefix = value.front() == '#';
    if (had_hash_prefix) {
        value.erase(value.begin());
        size_t trailing = value.find_first_of(" \t\r\n");
        if (trailing != std::string::npos) value.erase(trailing);
    }
    if (value.size() > 2 && value[0] == '0' && (value[1] == 'x' || value[1] == 'X')) value.erase(0, 2);

    if (value.size() == 6 || value.size() == 8) {
        bool is_hex = true;
        for (char ch : value) {
            if (hex_digit(ch) < 0) {
                is_hex = false;
                break;
            }
        }
        if (is_hex) {
            auto byte_at = [&](size_t pos) {
                return hex_digit(value[pos]) * 16 + hex_digit(value[pos + 1]);
            };
            return clamp_theme_color(ImVec4(
                byte_at(0) / 255.0f,
                byte_at(2) / 255.0f,
                byte_at(4) / 255.0f,
                1.0f));
        }
    }

    std::array<float, 3> components{};
    std::istringstream in(value);
    std::string part;
    int count = 0;
    while (count < static_cast<int>(components.size()) && std::getline(in, part, ',')) {
        try {
            components[count++] = std::stof(trim_ascii(part));
        } catch (...) {
            return std::nullopt;
        }
    }
    if (count == static_cast<int>(components.size())) {
        bool byte_range = components[0] > 1.0f || components[1] > 1.0f || components[2] > 1.0f;
        if (byte_range) {
            for (float& component : components) component /= 255.0f;
        }
        return clamp_theme_color(ImVec4(components[0], components[1], components[2], 1.0f));
    }

    return std::nullopt;
}

ImVec4 with_alpha(ImVec4 color, float alpha) {
    color = clamp_theme_color(color);
    color.w = clamp_color_component(alpha);
    return color;
}

ImVec4 mix_color(ImVec4 a, ImVec4 b, float t, float alpha = 1.0f) {
    a = clamp_theme_color(a);
    b = clamp_theme_color(b);
    t = clamp_color_component(t);
    return ImVec4(
        a.x + (b.x - a.x) * t,
        a.y + (b.y - a.y) * t,
        a.z + (b.z - a.z) * t,
        clamp_color_component(alpha));
}

std::string language_to_string(Language lang) {
    switch (lang) {
        case Language::Ja: return "ja";
        case Language::En: return "en";
        case Language::Zh: return "zh";
    }
    return "zh";
}

Language language_from_string(const std::string& text, Language fallback) {
    std::string value = ascii_lower(trim_ascii(text));
    if (value == "ja" || value == "jp" || value == "japanese") return Language::Ja;
    if (value == "en" || value == "english") return Language::En;
    if (value == "zh" || value == "cn" || value == "chinese" || value == "simplified_chinese") return Language::Zh;
    return fallback;
}

struct UserSettings {
    Language language = Language::Zh;
    float font_size = kDefaultFontSize;
    float ui_component_size = kDefaultUiComponentSize;
    float station_marker_size = kDefaultStationMarkerSize;
    ImVec4 theme_color = default_theme_color();
    std::filesystem::path path;
};

struct BackgroundHistory {
    bool has_image = false;
    std::string image_path;
    double x = 0.0;
    double y = 0.0;
    double width = 0.0;
    double height = 0.0;
    double rotation_deg = 0.0;
    double brightness = 100.0;
};

struct RecentMapEntry {
    std::string path;
    BackgroundHistory background;
};

std::filesystem::path executable_directory() {
    std::vector<wchar_t> buffer(MAX_PATH);
    while (true) {
        DWORD len = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (len == 0) break;
        if (len < buffer.size() - 1) {
            return std::filesystem::path(std::wstring(buffer.data(), len)).parent_path();
        }
        buffer.resize(buffer.size() * 2);
    }
    std::error_code ec;
    std::filesystem::path cwd = std::filesystem::current_path(ec);
    return ec ? std::filesystem::path(L".") : cwd;
}

std::filesystem::path default_settings_path() {
    return executable_directory() / L"settings.ini";
}

std::filesystem::path default_history_path() {
    return executable_directory() / L"history.ini";
}

bool save_user_settings(const UserSettings& settings) {
    std::ofstream out(settings.path, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out << "[General]\n";
    out << "language=" << language_to_string(settings.language) << "\n";
    out << "font_size=" << std::fixed << std::setprecision(1) << clamp_font_size(settings.font_size) << "\n";
    out << "ui_component_size=" << std::fixed << std::setprecision(1) << clamp_ui_component_size(settings.ui_component_size) << "\n";
    out << "station_marker_size=" << std::fixed << std::setprecision(1) << clamp_station_marker_size(settings.station_marker_size) << "\n";
    out << "theme_color=" << theme_color_to_string(settings.theme_color) << "\n";
    return true;
}

UserSettings load_user_settings() {
    UserSettings settings;
    settings.path = default_settings_path();

    std::error_code ec;
    bool exists = std::filesystem::exists(settings.path, ec);
    if (ec || !exists) {
        save_user_settings(settings);
        return settings;
    }

    std::ifstream in(settings.path, std::ios::binary);
    if (!in) return settings;

    std::string line;
    while (std::getline(in, line)) {
        std::string trimmed_line = trim_ascii(line);
        if (trimmed_line.empty() || trimmed_line.front() == ';' || trimmed_line.front() == '#') continue;
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = ascii_lower(trim_ascii(line.substr(0, eq)));
        std::string value = trim_ascii(line.substr(eq + 1));
        bool is_theme_color_key = key == "theme_color" || key == "ui_theme_color" || key == "interface_theme_color" || key == "accent_color";
        size_t semicolon_comment = value.find(';');
        if (semicolon_comment != std::string::npos) value.erase(semicolon_comment);
        size_t hash_comment = value.find('#');
        if (hash_comment != std::string::npos && !(is_theme_color_key && hash_comment == 0)) value.erase(hash_comment);
        value = trim_ascii(value);
        if (key == "language" || key == "lang") {
            settings.language = language_from_string(value, settings.language);
        } else if (key == "font_size" || key == "fontsize" || key == "text_size") {
            try {
                settings.font_size = clamp_font_size(std::stof(value));
            } catch (...) {
                settings.font_size = kDefaultFontSize;
            }
        } else if (key == "ui_component_size" || key == "component_size" || key == "ui_scale" || key == "ui_component_scale") {
            try {
                settings.ui_component_size = clamp_ui_component_size(std::stof(value));
            } catch (...) {
                settings.ui_component_size = kDefaultUiComponentSize;
            }
        } else if (key == "station_marker_size" || key == "station_marker_radius" || key == "station_size") {
            try {
                settings.station_marker_size = clamp_station_marker_size(std::stof(value));
            } catch (...) {
                settings.station_marker_size = kDefaultStationMarkerSize;
            }
        } else if (is_theme_color_key) {
            if (auto color = parse_theme_color(value)) {
                settings.theme_color = *color;
            } else {
                settings.theme_color = default_theme_color();
            }
        }
    }
    settings.font_size = clamp_font_size(settings.font_size);
    settings.ui_component_size = clamp_ui_component_size(settings.ui_component_size);
    settings.station_marker_size = clamp_station_marker_size(settings.station_marker_size);
    settings.theme_color = clamp_theme_color(settings.theme_color);
    return settings;
}

bool ensure_history_file(const std::filesystem::path& path) {
    std::error_code ec;
    if (std::filesystem::exists(path, ec) && !ec) return true;
    std::ofstream out(path, std::ios::binary);
    return static_cast<bool>(out);
}

double parse_history_double(const std::string& value, double fallback) {
    try {
        size_t used = 0;
        double parsed = std::stod(value, &used);
        return used == 0 || !std::isfinite(parsed) ? fallback : parsed;
    } catch (...) {
        return fallback;
    }
}

std::string history_number(double value) {
    std::ostringstream out;
    out << std::setprecision(17) << value;
    return out.str();
}

std::optional<int> parse_history_section_index(std::string section) {
    section = ascii_lower(trim_ascii(section));
    size_t pos = std::string::npos;
    if (section.rfind("map", 0) == 0) {
        pos = 3;
    } else if (section.rfind("recent_map", 0) == 0) {
        pos = 10;
    } else if (section.rfind("recent", 0) == 0) {
        pos = 6;
    }
    if (pos == std::string::npos) return std::nullopt;
    while (pos < section.size() && !std::isdigit(static_cast<unsigned char>(section[pos]))) ++pos;
    if (pos >= section.size()) return std::nullopt;
    try {
        return std::stoi(section.substr(pos));
    } catch (...) {
        return std::nullopt;
    }
}

std::vector<RecentMapEntry> load_history_entries(const std::filesystem::path& path) {
    ensure_history_file(path);
    std::ifstream in(path, std::ios::binary);
    if (!in) return {};

    std::map<int, RecentMapEntry> parsed;
    int current_index = -1;
    std::string line;
    while (std::getline(in, line)) {
        std::string trimmed_line = trim_ascii(line);
        if (trimmed_line.empty() || trimmed_line.front() == ';' || trimmed_line.front() == '#') continue;
        if (trimmed_line.front() == '[' && trimmed_line.back() == ']') {
            std::string section = trimmed_line.substr(1, trimmed_line.size() - 2);
            auto index = parse_history_section_index(section);
            current_index = index ? *index : -1;
            if (current_index >= 0 && parsed.find(current_index) == parsed.end()) parsed[current_index] = {};
            continue;
        }
        if (current_index < 0) continue;
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = ascii_lower(trim_ascii(line.substr(0, eq)));
        std::string value = trim_ascii(line.substr(eq + 1));
        RecentMapEntry& entry = parsed[current_index];
        if (key == "path" || key == "map_path" || key == "map") {
            entry.path = normalized_storage_path(value);
        } else if (key == "bg_path" || key == "background_path" || key == "image_path") {
            entry.background.has_image = !value.empty();
            entry.background.image_path = normalized_storage_path(value);
        } else if (key == "bg_x" || key == "background_x") {
            entry.background.x = parse_history_double(value, entry.background.x);
        } else if (key == "bg_y" || key == "background_y") {
            entry.background.y = parse_history_double(value, entry.background.y);
        } else if (key == "bg_width" || key == "background_width") {
            entry.background.width = parse_history_double(value, entry.background.width);
        } else if (key == "bg_height" || key == "background_height") {
            entry.background.height = parse_history_double(value, entry.background.height);
        } else if (key == "bg_rotation" || key == "bg_rotation_deg" || key == "background_rotation") {
            entry.background.rotation_deg = parse_history_double(value, entry.background.rotation_deg);
        } else if (key == "bg_brightness" || key == "background_brightness") {
            entry.background.brightness = parse_history_double(value, entry.background.brightness);
        }
    }

    std::vector<RecentMapEntry> entries;
    std::set<std::string> seen;
    for (auto& kv : parsed) {
        RecentMapEntry entry = std::move(kv.second);
        if (entry.path.empty()) continue;
        std::string key = normalized_path_key(entry.path);
        if (!seen.insert(key).second) continue;
        entries.push_back(std::move(entry));
        if (entries.size() >= kMaxRecentMaps) break;
    }
    return entries;
}

bool save_history_entries(const std::filesystem::path& path, const std::vector<RecentMapEntry>& entries) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    if (entries.empty()) return true;
    size_t count = std::min(entries.size(), kMaxRecentMaps);
    out << "[Recent]\n";
    out << "count=" << count << "\n\n";
    for (size_t i = 0; i < count; ++i) {
        const RecentMapEntry& entry = entries[i];
        out << "[Map" << i << "]\n";
        out << "path=" << normalized_storage_path(entry.path) << "\n";
        if (entry.background.has_image && !entry.background.image_path.empty()) {
            const BackgroundHistory& bg = entry.background;
            out << "bg_path=" << normalized_storage_path(bg.image_path) << "\n";
            out << "bg_x=" << history_number(bg.x) << "\n";
            out << "bg_y=" << history_number(bg.y) << "\n";
            out << "bg_width=" << history_number(bg.width) << "\n";
            out << "bg_height=" << history_number(bg.height) << "\n";
            out << "bg_rotation=" << history_number(bg.rotation_deg) << "\n";
            out << "bg_brightness=" << history_number(bg.brightness) << "\n";
        }
        out << "\n";
    }
    return true;
}

void apply_ui_font_size(float font_size) {
    ImGui::GetStyle().FontScaleMain = clamp_font_size(font_size) / kDefaultFontSize;
}

void apply_ui_theme_color(ImVec4 theme_color) {
    ImGuiStyle& style = ImGui::GetStyle();
    ImVec4 accent = clamp_theme_color(theme_color);
    ImVec4 dark_bg(0.06f, 0.07f, 0.08f, 1.0f);
    ImVec4 panel_bg(0.12f, 0.13f, 0.15f, 1.0f);

    style.Colors[ImGuiCol_FrameBg] = with_alpha(mix_color(panel_bg, accent, 0.16f), 0.88f);
    style.Colors[ImGuiCol_Button] = with_alpha(accent, 0.62f);
    style.Colors[ImGuiCol_ButtonHovered] = with_alpha(mix_color(accent, ImVec4(1.0f, 1.0f, 1.0f, 1.0f), 0.16f), 0.86f);
    style.Colors[ImGuiCol_ButtonActive] = with_alpha(mix_color(accent, dark_bg, 0.18f), 1.0f);
    style.Colors[ImGuiCol_Header] = with_alpha(accent, 0.34f);
    style.Colors[ImGuiCol_HeaderHovered] = with_alpha(accent, 0.72f);
    style.Colors[ImGuiCol_HeaderActive] = with_alpha(accent, 0.92f);
    style.Colors[ImGuiCol_CheckMark] = with_alpha(accent, 1.0f);
    style.Colors[ImGuiCol_CheckboxSelectedBg] = with_alpha(accent, 0.58f);
    style.Colors[ImGuiCol_SliderGrab] = with_alpha(accent, 0.86f);
    style.Colors[ImGuiCol_SliderGrabActive] = with_alpha(mix_color(accent, ImVec4(1.0f, 1.0f, 1.0f, 1.0f), 0.18f), 1.0f);
    style.Colors[ImGuiCol_FrameBgHovered] = with_alpha(accent, 0.24f);
    style.Colors[ImGuiCol_FrameBgActive] = with_alpha(accent, 0.42f);
    style.Colors[ImGuiCol_SeparatorHovered] = with_alpha(accent, 0.78f);
    style.Colors[ImGuiCol_SeparatorActive] = with_alpha(accent, 1.0f);
    style.Colors[ImGuiCol_ResizeGrip] = with_alpha(accent, 0.30f);
    style.Colors[ImGuiCol_ResizeGripHovered] = with_alpha(accent, 0.70f);
    style.Colors[ImGuiCol_ResizeGripActive] = with_alpha(accent, 0.95f);
    style.Colors[ImGuiCol_Tab] = with_alpha(mix_color(panel_bg, accent, 0.28f), 0.86f);
    style.Colors[ImGuiCol_TabHovered] = with_alpha(accent, 0.82f);
    style.Colors[ImGuiCol_TabSelected] = with_alpha(mix_color(panel_bg, accent, 0.58f), 1.0f);
    style.Colors[ImGuiCol_TabSelectedOverline] = with_alpha(accent, 1.0f);
    style.Colors[ImGuiCol_TabDimmedSelected] = with_alpha(mix_color(panel_bg, accent, 0.36f), 1.0f);
    style.Colors[ImGuiCol_TabDimmedSelectedOverline] = with_alpha(accent, 0.72f);
    style.Colors[ImGuiCol_TitleBgActive] = with_alpha(mix_color(dark_bg, accent, 0.35f), 1.0f);
    style.Colors[ImGuiCol_DockingPreview] = with_alpha(accent, 0.70f);
    style.Colors[ImGuiCol_TextLink] = with_alpha(mix_color(accent, ImVec4(1.0f, 1.0f, 1.0f, 1.0f), 0.12f), 1.0f);
    style.Colors[ImGuiCol_TextSelectedBg] = with_alpha(accent, 0.42f);
    style.Colors[ImGuiCol_NavCursor] = with_alpha(accent, 1.0f);

    ImPlotStyle& plot_style = ImPlot::GetStyle();
    ImVec4 canvas_bg(12.0f / 255.0f, 13.0f / 255.0f, 15.0f / 255.0f, 1.0f);
    plot_style.Colors[ImPlotCol_FrameBg] = canvas_bg;
    plot_style.Colors[ImPlotCol_PlotBg] = canvas_bg;
    plot_style.Colors[ImPlotCol_PlotBorder] = ImVec4(0.43f, 0.43f, 0.50f, 0.50f);
    plot_style.Colors[ImPlotCol_LegendBg] = ImVec4(0.08f, 0.08f, 0.08f, 0.94f);
    plot_style.Colors[ImPlotCol_LegendBorder] = ImVec4(0.43f, 0.43f, 0.50f, 0.50f);
    plot_style.Colors[ImPlotCol_AxisText] = with_alpha(mix_color(ImVec4(1.0f, 1.0f, 1.0f, 1.0f), accent, 0.20f), 1.0f);
    plot_style.Colors[ImPlotCol_AxisGrid] = ImVec4(48.0f / 255.0f, 52.0f / 255.0f, 58.0f / 255.0f, 1.0f);
    plot_style.Colors[ImPlotCol_AxisTick] = with_alpha(accent, 0.55f);
    plot_style.Colors[ImPlotCol_AxisBg] = with_alpha(accent, 0.08f);
    plot_style.Colors[ImPlotCol_AxisBgHovered] = with_alpha(accent, 0.22f);
    plot_style.Colors[ImPlotCol_AxisBgActive] = with_alpha(accent, 0.34f);
    plot_style.Colors[ImPlotCol_Selection] = ImVec4(1.00f, 0.60f, 0.00f, 1.00f);
    plot_style.Colors[ImPlotCol_Crosshairs] = ImVec4(1.00f, 1.00f, 1.00f, 0.50f);
}

void apply_ui_component_size(float component_size, float dpi_scale, bool viewports_enabled) {
    ImGuiStyle& style = ImGui::GetStyle();
    float font_size_base = style.FontSizeBase;
    float font_scale_main = style.FontScaleMain;
    style = ImGuiStyle();
    style.FontSizeBase = font_size_base;
    style.FontScaleMain = font_scale_main;
    ImGui::StyleColorsDark(&style);
    style.ScaleAllSizes(dpi_scale * clamp_ui_component_size(component_size) / 100.0f);
    style.FontScaleDpi = dpi_scale;
    if (viewports_enabled) {
        style.WindowRounding = 0.0f;
        style.Colors[ImGuiCol_WindowBg].w = 1.0f;
    }
}

void apply_ui_settings(float font_size, float component_size, ImVec4 theme_color, float dpi_scale, bool viewports_enabled) {
    apply_ui_component_size(component_size, dpi_scale, viewports_enabled);
    apply_ui_theme_color(theme_color);
    apply_ui_font_size(font_size);
}

class App {
public:
    explicit App(ID3D11Device* device, UserSettings settings, float dpi_scale, bool viewports_enabled)
        : device_(device), settings_(std::move(settings)), dpi_scale_(dpi_scale), viewports_enabled_(viewports_enabled) {
        g_app = this;
        kv_set_log_callback(&App::log_callback);
        lang_ = settings_.language;
        font_size_ = clamp_font_size(settings_.font_size);
        ui_component_size_ = clamp_ui_component_size(settings_.ui_component_size);
        station_marker_size_ = clamp_station_marker_size(settings_.station_marker_size);
        theme_color_ = clamp_theme_color(settings_.theme_color);
        settings_.language = lang_;
        settings_.font_size = font_size_;
        settings_.ui_component_size = ui_component_size_;
        settings_.station_marker_size = station_marker_size_;
        settings_.theme_color = theme_color_;
        pending_font_size_ = font_size_;
        font_size_before_dialog_ = font_size_;
        pending_ui_component_size_ = ui_component_size_;
        ui_component_size_before_dialog_ = ui_component_size_;
        pending_station_marker_size_ = station_marker_size_;
        station_marker_size_before_dialog_ = station_marker_size_;
        pending_theme_color_ = theme_color_;
        theme_color_before_dialog_ = theme_color_;
        apply_ui_settings(font_size_, ui_component_size_, theme_color_, dpi_scale_, viewports_enabled_);
        history_path_ = default_history_path();
        recent_maps_ = load_history_entries(history_path_);
        sync_pending_background_values();
    }

    ~App() {
        stop_loader();
        if (handle_) kv_free(handle_);
        bg_image_.release();
        g_app = nullptr;
    }

    void render();
    void add_log(std::string text);

private:
    ID3D11Device* device_ = nullptr;
    float dpi_scale_ = 1.0f;
    bool viewports_enabled_ = false;
    Translation i18n_;
    UserSettings settings_;
    Language lang_ = Language::Zh;
    float font_size_ = kDefaultFontSize;
    float pending_font_size_ = kDefaultFontSize;
    float font_size_before_dialog_ = kDefaultFontSize;
    float ui_component_size_ = kDefaultUiComponentSize;
    float pending_ui_component_size_ = kDefaultUiComponentSize;
    float ui_component_size_before_dialog_ = kDefaultUiComponentSize;
    float station_marker_size_ = kDefaultStationMarkerSize;
    float pending_station_marker_size_ = kDefaultStationMarkerSize;
    float station_marker_size_before_dialog_ = kDefaultStationMarkerSize;
    ImVec4 theme_color_ = default_theme_color();
    ImVec4 pending_theme_color_ = default_theme_color();
    ImVec4 theme_color_before_dialog_ = default_theme_color();
    std::filesystem::path history_path_;
    std::vector<RecentMapEntry> recent_maps_;

    void* handle_ = nullptr;
    MapModel model_;
    bool has_model_ = false;
    std::string file_path_;

    std::vector<LogLine> logs_;
    std::mutex log_mutex_;
    std::string last_log_;
    int error_count_ = 0;
    int warn_count_ = 0;

    std::thread loader_;
    std::atomic<bool> loading_{false};
    struct LoadResult {
        bool ok = false;
        bool preserve_settings = false;
        bool record_history = false;
        std::optional<BackgroundHistory> background_to_restore;
        void* handle = nullptr;
        MapModel model;
        std::string path;
        std::string error;
        double elapsed_seconds = 0.0;
    };
    std::mutex result_mutex_;
    std::optional<LoadResult> pending_result_;

    double dmin_ = 0.0;
    double dmax_ = 0.0;
    double plot_min_ = 0.0;
    double plot_max_ = 0.0;
    double cp_start_ = 0.0;
    double cp_end_ = 0.0;
    double cp_interval_ = 25.0;
    double unit_distance_ = 25.0;

    bool show_stations_ = true;
    bool show_station_names_ = true;
    bool show_station_mileage_ = true;
    bool show_gradient_pos_ = true;
    bool show_gradient_values_ = true;
    bool show_curve_values_ = true;
    bool show_profile_other_ = false;
    bool show_speedlimits_ = true;
    bool show_profile_graph_ = true;
    bool show_radius_graph_ = true;
    bool show_othertracks_window_ = true;

    enum class GridMode { Fixed, Movable, None };
    enum class Mode { Pan, Measure };
    GridMode grid_mode_ = GridMode::Fixed;
    Mode mode_ = Mode::Pan;

    View2D plan_view_;
    bool keep_plan_view_ = false;
    double plan_height_ = 0.68;
    double graph_split_ = 0.52;

    std::optional<double> measure_distance_;
    std::string measure_text_;
    bool focus_profile_next_ = false;
    double focus_profile_distance_ = 0.0;
    bool focus_radius_next_ = false;
    double focus_radius_distance_ = 0.0;
    double profile_x_span_ = 0.0;
    double radius_x_span_ = 0.0;
    bool profile_x_zoom_pending_ = false;
    double profile_x_zoom_min_ = 0.0;
    double profile_x_zoom_max_ = 0.0;
    bool profile_plot_rect_valid_ = false;
    ImVec2 profile_plot_pos_ = ImVec2(0.0f, 0.0f);
    ImVec2 profile_plot_size_ = ImVec2(0.0f, 0.0f);
    bool reset_profile_axes_next_ = true;
    bool reset_radius_axes_next_ = true;

    bool show_station_list_window_ = false;
    bool show_structures_window_ = false;
    bool show_repeaters_window_ = false;
    bool show_range_popup_ = false;
    bool show_cp_popup_ = false;
    bool show_bg_adjust_popup_ = false;
    bool show_align_popup_ = false;
    bool show_about_popup_ = false;
    bool show_font_size_popup_ = false;
    ImGuiID dock_right_id_ = 0;
    TableUiCache table_cache_;

    TextureImage bg_image_;
    bool bg_show_ = true;
    double bg_x_ = 0.0;
    double bg_y_ = 0.0;
    double bg_width_ = 5000.0;
    double bg_height_ = 5000.0;
    double bg_rotation_deg_ = 0.0;
    double bg_brightness_ = 100.0;
    double pending_bg_x_ = 0.0;
    double pending_bg_y_ = 0.0;
    double pending_bg_width_ = 5000.0;
    double pending_bg_height_ = 5000.0;
    double pending_bg_rotation_deg_ = 0.0;
    double pending_bg_brightness_ = 100.0;
    int station_jump_index_ = 0;
    int align_station1_ = 0;
    int align_station2_ = 1;
    std::optional<ImVec2> align_pick1_;
    std::optional<ImVec2> align_pick2_;
    int pick_slot_ = 0;

    const std::string& tr(const std::string& key) const { return i18n_.get(lang_, key); }

    static void log_callback(const char* message) {
        if (g_app && message) g_app->add_log(message);
    }

    void stop_loader();
    void poll_loader();
    void begin_load(std::string path, bool preserve_settings, bool record_history = false,
                    std::optional<BackgroundHistory> background_to_restore = std::nullopt);
    void apply_load_result(LoadResult result);
    void regenerate_geometry();
    static LoadResult load_map_worker(std::string path, double unit_distance, bool has_cp, double cp_start, double cp_end, double cp_step);
    static MapModel build_model_from_handle(void* handle, const std::string& path);

    void handle_shortcuts();
    void render_menu();
    void render_toolbar();
    void render_mode_grid_controls();
    void render_station_jump_combo();
    void render_console();
    void render_plots();
    void render_plan_canvas(ImVec2 size);
    void render_profile_plot(const ProfileData& data, ImVec2 size);
    void render_radius_plot(const ProfileData& data, ImVec2 size);
    void render_othertracks_window();
    void render_station_list_window();
    void render_structures_window();
    void render_repeaters_window();
    void render_popups();
    void setup_initial_dockspace(ImGuiID dockspace_id);
    void invalidate_table_cache();
    void ensure_table_cache();

    PlanData build_plan_data() const;
    ProfileData build_profile_data() const;
    std::vector<Section> curve_sections(bool transition) const;
    size_t nearest_own_index(double distance) const;
    double interp_own_z(double distance) const;
    std::optional<TrackPoint> track_info_at(double distance) const;
    std::optional<SpeedLimit> speed_at(double distance) const;
    void clear_measure();
    void update_measure(double distance);
    void center_plan_at_distance(double distance);
    void request_plot_focus(double distance, bool include_profile, bool include_radius);
    void handle_measure_plot_double_click(bool include_profile, bool include_radius);
    void focus_station(double distance);
    void export_csv();
    void save_history();
    void touch_recent_map(const std::string& path);
    void save_current_background_to_history();
    BackgroundHistory current_background_history() const;
    void sync_pending_background_values();
    void apply_pending_background_values(bool save_history_entry);
    bool apply_background_history(const BackgroundHistory& background);
    void clear_background_image();
    bool load_background_image(const std::string& path, bool reset_parameters = true);
    bool rebuild_background_texture();
    std::optional<ImVec2> background_uv_from_world(ImVec2 world) const;
    void draw_background(ImDrawList* draw, const View2D& view, ImVec2 origin, ImVec2 size);
    void apply_background_alignment();
    std::string open_map_dialog();
    std::string open_image_dialog();
    std::string choose_folder_dialog();
};

void App::add_log(std::string text) {
    int sev = 0;
    if (text.find("[ERROR]") != std::string::npos || text.find("Error") != std::string::npos) sev = 2;
    else if (text.find("[WARN]") != std::string::npos || text.find("Warning") != std::string::npos) sev = 1;
    {
        std::lock_guard<std::mutex> lock(log_mutex_);
        logs_.push_back({text, sev});
        last_log_ = text;
        if (sev == 2) ++error_count_;
        if (sev == 1) ++warn_count_;
    }
    wake_main_window();
}

void App::invalidate_table_cache() {
    table_cache_ = TableUiCache{};
}

void App::ensure_table_cache() {
    const float font_size = ImGui::GetFontSize();
    const float cell_padding_x = ImGui::GetStyle().CellPadding.x;
    if (table_cache_.valid &&
        std::abs(table_cache_.font_size - font_size) < 0.01f &&
        std::abs(table_cache_.cell_padding_x - cell_padding_x) < 0.01f) {
        return;
    }

    TableUiCache cache;
    cache.valid = true;
    cache.font_size = font_size;
    cache.cell_padding_x = cell_padding_x;

    cache.station_rows.reserve(model_.station_list_rows.size());
    for (const auto& row : model_.station_list_rows) {
        CachedTableRow cached;
        cached.cells.resize(IM_ARRAYSIZE(kStationListColumns));
        for (int i = 0; i < IM_ARRAYSIZE(kStationListColumns); ++i) {
            cached.cells[i] = table_cell(row, kStationListColumns[i].key);
        }
        cache.station_rows.push_back(std::move(cached));
    }

    auto append_structure_rows = [&](const std::vector<TableRow>& rows) {
        cache.structure_rows.reserve(cache.structure_rows.size() + rows.size());
        for (const auto& row : rows) {
            CachedTableRow cached;
            cached.cells.resize(IM_ARRAYSIZE(kStructureColumns));
            for (int i = 0; i < IM_ARRAYSIZE(kStructureColumns); ++i) {
                const std::string& value = table_cell(row, kStructureColumns[i]);
                if (i == kStructureFilePathColumn) {
                    cached.open_path = value;
                    cached.cells[i] = display_name_from_path(value);
                    expand_width_for_text(cache.structure_file_path_width, cached.cells[i]);
                } else {
                    cached.cells[i] = value;
                }
            }
            cache.structure_rows.push_back(std::move(cached));
        }
    };
    append_structure_rows(model_.structures);
    append_structure_rows(model_.structures_between);

    cache.repeater_interval_width = 0.0f;
    expand_width_for_text(cache.repeater_interval_width, kRepeaterColumns[kRepeaterIntervalColumn].header);
    std::vector<TableRow> repeater_rows = merged_repeater_rows(model_.repeaters);
    cache.repeater_rows.reserve(repeater_rows.size());
    for (const auto& row : repeater_rows) {
        CachedTableRow cached;
        cached.cells.resize(IM_ARRAYSIZE(kRepeaterColumns));
        cached.open_path = table_cell(row, "_openFilePath");
        for (int i = 0; i < IM_ARRAYSIZE(kRepeaterColumns); ++i) {
            cached.cells[i] = table_cell(row, kRepeaterColumns[i].key);
        }
        expand_width_for_text(cache.repeater_distance_width, cached.cells[kRepeaterDistanceColumn]);
        expand_width_for_text(cache.repeater_file_path_width, cached.cells[kRepeaterFilePathColumn]);
        cache.repeater_rows.push_back(std::move(cached));
    }

    table_cache_ = std::move(cache);
}

void App::stop_loader() {
    if (loader_.joinable()) loader_.join();
}

void App::poll_loader() {
    std::optional<LoadResult> result;
    {
        std::lock_guard<std::mutex> lock(result_mutex_);
        if (pending_result_) {
            result = std::move(pending_result_);
            pending_result_.reset();
        }
    }
    if (result) apply_load_result(std::move(*result));
}

void App::begin_load(std::string path, bool preserve_settings, bool record_history,
                     std::optional<BackgroundHistory> background_to_restore) {
    if (path.empty() || loading_) return;

    std::map<std::string, OtherTrack> old_other;
    if (preserve_settings) {
        for (const auto& t : model_.other_tracks) old_other[t.key] = t;
    }

    stop_loader();
    {
        std::lock_guard<std::mutex> lock(log_mutex_);
        logs_.clear();
        last_log_.clear();
        error_count_ = warn_count_ = 0;
    }
    loading_ = true;
    add_log(std::string("Start loading file: ") + path);

    bool has_cp = preserve_settings && has_model_ && model_.has_cp_arb;
    double cp0 = has_cp ? model_.cp_arb[0] : 0.0;
    double cp1 = has_cp ? model_.cp_arb[1] : 0.0;
    double cp2 = has_cp ? model_.cp_arb[2] : 25.0;

    loader_ = std::thread([this, path, has_cp, cp0, cp1, cp2, old_other, preserve_settings,
                           record_history, background_to_restore]() mutable {
        LoadResult result = load_map_worker(path, unit_distance_, has_cp, cp0, cp1, cp2);
        result.preserve_settings = preserve_settings;
        result.record_history = record_history;
        result.background_to_restore = background_to_restore;
        if (result.ok && preserve_settings) {
            for (auto& t : result.model.other_tracks) {
                auto it = old_other.find(t.key);
                if (it != old_other.end()) {
                    t.visible = it->second.visible;
                    t.color = it->second.color;
                    t.range_min = it->second.range_min;
                    t.range_max = it->second.range_max;
                }
            }
        }
        {
            std::lock_guard<std::mutex> lock(result_mutex_);
            pending_result_ = std::move(result);
        }
        loading_ = false;
        wake_main_window();
    });
}

void App::apply_load_result(LoadResult result) {
    if (!result.ok) {
        add_log("Error during loading: " + result.error);
        if (result.handle) kv_free(result.handle);
        return;
    }
    if (handle_) kv_free(handle_);
    handle_ = result.handle;
    model_ = std::move(result.model);
    invalidate_table_cache();
    has_model_ = true;
    file_path_ = result.path;
    dmin_ = model_.default_min;
    dmax_ = model_.default_max;
    plot_min_ = dmin_;
    plot_max_ = dmax_;
    cp_start_ = model_.cp_arb[0];
    cp_end_ = model_.cp_arb[1];
    cp_interval_ = model_.cp_arb[2];
    if (!result.preserve_settings) {
        plan_view_.fitted = false;
        clear_measure();
    }
    reset_profile_axes_next_ = true;
    reset_radius_axes_next_ = true;
    profile_x_span_ = 0.0;
    radius_x_span_ = 0.0;
    std::ostringstream elapsed;
    elapsed << std::fixed << std::setprecision(2) << result.elapsed_seconds;
    add_log("Map loaded in " + elapsed.str() + "s");
    add_log("Map loaded: " + result.path);
    if (result.background_to_restore) {
        apply_background_history(*result.background_to_restore);
    } else if (!result.preserve_settings) {
        clear_background_image();
    }
    if (result.record_history) touch_recent_map(result.path);
}

void App::regenerate_geometry() {
    if (!handle_ || loading_) return;
    if (!kv_generate_geometry(handle_, unit_distance_, 1, cp_start_, cp_end_, cp_interval_)) {
        const char* err = kv_get_last_error();
        add_log(std::string("[ERROR]") + (err ? err : "geometry failed"));
        return;
    }
    std::map<std::string, OtherTrack> old_other;
    for (const auto& t : model_.other_tracks) old_other[t.key] = t;
    try {
        MapModel updated = build_model_from_handle(handle_, file_path_);
        for (auto& t : updated.other_tracks) {
            auto it = old_other.find(t.key);
            if (it != old_other.end()) {
                t.visible = it->second.visible;
                t.color = it->second.color;
                t.range_min = it->second.range_min;
                t.range_max = it->second.range_max;
            }
        }
        model_ = std::move(updated);
        invalidate_table_cache();
        model_.has_cp_arb = true;
        model_.cp_arb[0] = cp_start_;
        model_.cp_arb[1] = cp_end_;
        model_.cp_arb[2] = cp_interval_;
        dmin_ = plot_min_;
        dmax_ = plot_max_;
        reset_profile_axes_next_ = true;
        reset_radius_axes_next_ = true;
        profile_x_span_ = 0.0;
        radius_x_span_ = 0.0;
        add_log("Geometry regenerated by maploader");
    } catch (const std::exception& e) {
        add_log(std::string("[ERROR]") + e.what());
    }
}

App::LoadResult App::load_map_worker(std::string path, double unit_distance, bool has_cp, double cp_start, double cp_end, double cp_step) {
    auto started_at = std::chrono::steady_clock::now();
    auto elapsed_seconds = [&]() {
        return std::chrono::duration<double>(std::chrono::steady_clock::now() - started_at).count();
    };
    LoadResult out;
    out.path = path;
    void* handle = kv_load_map(path.c_str(), unit_distance);
    if (!handle) {
        const char* err = kv_get_last_error();
        out.error = err ? err : "maploader failed";
        out.elapsed_seconds = elapsed_seconds();
        return out;
    }
    if (has_cp) {
        if (!kv_generate_geometry(handle, unit_distance, 1, cp_start, cp_end, cp_step)) {
            const char* err = kv_get_last_error();
            out.error = err ? err : "geometry failed";
            out.elapsed_seconds = elapsed_seconds();
            kv_free(handle);
            return out;
        }
    }
    try {
        out.model = build_model_from_handle(handle, path);
        if (has_cp) {
            out.model.has_cp_arb = true;
            out.model.cp_arb[0] = cp_start;
            out.model.cp_arb[1] = cp_end;
            out.model.cp_arb[2] = cp_step;
        }
        out.handle = handle;
        out.ok = true;
        out.elapsed_seconds = elapsed_seconds();
    } catch (const std::exception& e) {
        out.error = e.what();
        out.elapsed_seconds = elapsed_seconds();
        kv_free(handle);
    }
    return out;
}

MapModel App::build_model_from_handle(void* handle, const std::string& path) {
    const char* raw = kv_get_ir_json(handle);
    if (!raw) throw std::runtime_error("kv_get_ir_json failed");
    std::string json(raw);
    kv_free_string(raw);
    auto root = mini_json::Parser(json).parse();

    MapModel model;
    model.path = path;
    model.own = copy_buffer(kv_get_owntrack_buffer(handle));
    model.curve = copy_buffer(kv_get_curveradius_buffer(handle));

    const auto& cps = root.at("controlpoints");
    if (cps.is_array()) {
        for (const auto& v : cps.array) if (v.is_number()) model.controlpoints.push_back(v.number);
    }

    const auto& cp_default = root.at("cp_defaultrange");
    if (cp_default.is_array() && cp_default.array.size() >= 2) {
        model.cp_default_min = cp_default.array[0].number;
        model.cp_default_max = cp_default.array[1].number;
    }
    const auto& cp_arb = root.at("cp_arbdistribution");
    if (cp_arb.is_array() && cp_arb.array.size() >= 3) {
        model.cp_arb[0] = cp_arb.array[0].number;
        model.cp_arb[1] = cp_arb.array[1].number;
        model.cp_arb[2] = cp_arb.array[2].number;
    }

    if (!model.own.empty()) {
        model.distance_origin = model.own.at(0, 0);
        model.height_origin = model.own.at(0, 3);
        model.origin_angle = model.own.at(0, 4);
    }

    static const ImVec4 palette[] = {
        ImVec4(0.12f, 0.47f, 0.71f, 1.0f), ImVec4(1.00f, 0.50f, 0.05f, 1.0f),
        ImVec4(0.17f, 0.63f, 0.17f, 1.0f), ImVec4(0.84f, 0.15f, 0.16f, 1.0f),
        ImVec4(0.58f, 0.40f, 0.74f, 1.0f), ImVec4(0.55f, 0.34f, 0.29f, 1.0f),
        ImVec4(0.89f, 0.47f, 0.76f, 1.0f), ImVec4(0.50f, 0.50f, 0.50f, 1.0f),
        ImVec4(0.74f, 0.74f, 0.13f, 1.0f), ImVec4(0.09f, 0.75f, 0.81f, 1.0f)
    };

    const auto& other = root.at("othertrack");
    const auto& order = other.at("order");
    const auto& ranges = other.at("cp_range");
    if (order.is_array()) {
        for (size_t i = 0; i < order.array.size(); ++i) {
            std::string key = order.array[i].scalar_text();
            OtherTrack t;
            t.key = key;
            t.color = palette[i % (sizeof(palette) / sizeof(palette[0]))];
            t.range_min = 0.0;
            t.range_max = 0.0;
            const auto& range = ranges.at(key);
            if (range.is_object()) {
                t.range_min = range.at("min").number;
                t.range_max = range.at("max").number;
            }
            Matrix points;
            KvDoubleBuffer buf = kv_get_othertrack_buffer(handle, key.c_str());
            points = copy_buffer(buf);
            t.points = std::move(points);
            if (!t.points.empty() && (t.range_min == t.range_max)) {
                t.range_min = t.points.at(0, 0);
                t.range_max = t.points.at(t.points.rows - 1, 0);
            }
            model.other_tracks.push_back(std::move(t));
        }
    }

    const auto& own_track = root.at("own_track");
    if (own_track.is_array()) {
        for (const auto& row : own_track.array) {
            TrackEvent e;
            e.distance = row.at("distance").number;
            e.key = row.at("key").scalar_text();
            e.flag = row.at("flag").scalar_text();
            const auto& val = row.at("value");
            if (val.is_number()) {
                e.value_number = true;
                e.number = val.number;
            } else {
                e.text = val.scalar_text();
            }
            model.own_events.push_back(std::move(e));
        }
    }

    const auto& speed = root.at("speedlimit");
    if (speed.is_array()) {
        for (const auto& row : speed.array) {
            SpeedLimit s;
            s.distance = row.at("distance").number;
            const auto& v = row.at("speed");
            if (v.is_number()) {
                s.has_speed = true;
                s.speed = v.number;
            }
            model.speedlimits.push_back(s);
        }
    }

    const auto& station = root.at("station");
    const auto& positions = station.at("position");
    const auto& names = station.at("stationkey");
    if (positions.is_array()) {
        std::set<std::string> seen;
        for (const auto& item : positions.array) {
            if (!item.is_array() || item.array.size() < 2) continue;
            Station s;
            s.distance = item.array[0].number;
            s.key = item.array[1].scalar_text();
            s.name = names.at(s.key).scalar_text();
            if (s.name.empty()) s.name = s.key;
            s.mileage = s.distance - model.distance_origin;
            if (!model.own.empty()) {
                size_t idx = 0;
                while (idx + 1 < model.own.rows && model.own.at(idx, 0) < s.distance) ++idx;
                if (idx >= model.own.rows) idx = model.own.rows - 1;
                s.x = model.own.at(idx, 1);
                s.y = model.own.at(idx, 2);
                s.z = model.own.at(idx, 3);
            }
            if (seen.insert(s.key).second) model.stations.push_back(std::move(s));
        }
    }

    auto make_table_rows = [](const mini_json::Value& array) {
        std::vector<TableRow> rows;
        if (!array.is_array()) return rows;
        for (const auto& v : array.array) {
            TableRow r;
            if (v.is_object()) {
                for (const auto& kv : v.object) r.cells[kv.first] = table_cell_text(kv.second);
            }
            rows.push_back(std::move(r));
        }
        return rows;
    };
    const auto& structure = root.at("structure");
    model.structures = make_table_rows(structure.at("data"));
    model.structures_between = make_table_rows(structure.at("between_data"));
    model.repeaters = make_table_rows(root.at("repeater"));

    std::map<std::string, TableRow> station_rows_by_key;
    const auto& station_list = station.at("list");
    if (station_list.is_object()) {
        for (const auto& kv : station_list.object) {
            TableRow row;
            if (kv.second.is_object()) {
                for (const auto& cell : kv.second.object) row.cells[cell.first] = table_cell_text(cell.second);
            }
            if (table_cell(row, "stationKey").empty()) row.cells["stationKey"] = kv.first;
            station_rows_by_key[ascii_lower(kv.first)] = std::move(row);
        }
    }
    auto append_station_table_row = [&](TableRow row, const std::string& key) {
        auto it = station_rows_by_key.find(ascii_lower(key));
        if (it != station_rows_by_key.end()) {
            for (const auto& cell : it->second.cells) row.cells[cell.first] = cell.second;
        }
        model.station_list_rows.push_back(std::move(row));
    };
    const auto& station_puts = station.at("put");
    if (station_puts.is_array()) {
        for (const auto& item : station_puts.array) {
            if (!item.is_object()) continue;
            std::string key = item.at("stationKey").scalar_text();
            double distance = item.at("distance").number;
            TableRow row;
            row.cells["_distance"] = format_double(distance);
            row.cells["_order"] = item.at("order").scalar_text();
            row.cells["dist"] = format_double(distance - model.distance_origin, 0);
            row.cells["posKey"] = key;
            row.cells["door"] = item.at("door").scalar_text();
            row.cells["margin1"] = item.at("margin1").scalar_text();
            row.cells["margin2"] = item.at("margin2").scalar_text();
            append_station_table_row(std::move(row), key);
        }
    } else if (positions.is_array()) {
        int order_index = 0;
        for (const auto& item : positions.array) {
            if (!item.is_array() || item.array.size() < 2) continue;
            std::string key = item.array[1].scalar_text();
            double distance = item.array[0].number;
            TableRow row;
            row.cells["_distance"] = format_double(distance);
            row.cells["_order"] = std::to_string(++order_index);
            row.cells["dist"] = format_double(distance - model.distance_origin, 0);
            row.cells["posKey"] = key;
            append_station_table_row(std::move(row), key);
        }
    }
    std::stable_sort(model.station_list_rows.begin(), model.station_list_rows.end(), [](const TableRow& a, const TableRow& b) {
        double da = table_cell_number(a, "_distance");
        double db = table_cell_number(b, "_distance");
        if (da != db) return da < db;
        return table_cell_number(a, "_order") < table_cell_number(b, "_order");
    });
    for (size_t i = 0; i < model.station_list_rows.size(); ++i) {
        model.station_list_rows[i].cells["rowNumber"] = std::to_string(i + 1);
    }

    if (!model.stations.empty()) {
        double mn = model.stations.front().distance;
        double mx = model.stations.front().distance;
        for (const auto& s : model.stations) {
            mn = std::min(mn, s.distance);
            mx = std::max(mx, s.distance);
        }
        model.default_min = round_to_100(mn) - 500.0;
        model.default_max = round_to_100(mx) + 500.0;
    } else if (!model.controlpoints.empty()) {
        auto [mn, mx] = std::minmax_element(model.controlpoints.begin(), model.controlpoints.end());
        model.default_min = round_to_100(*mn) - 500.0;
        model.default_max = round_to_100(*mx) + 500.0;
    } else if (!model.own.empty()) {
        model.default_min = model.own.at(0, 0);
        model.default_max = model.own.at(model.own.rows - 1, 0);
    }
    return model;
}

size_t App::nearest_own_index(double distance) const {
    if (model_.own.empty()) return 0;
    size_t lo = 0, hi = model_.own.rows;
    while (lo < hi) {
        size_t mid = (lo + hi) / 2;
        if (model_.own.at(mid, 0) < distance) lo = mid + 1;
        else hi = mid;
    }
    if (lo == 0) return 0;
    if (lo >= model_.own.rows) return model_.own.rows - 1;
    double a = std::abs(model_.own.at(lo, 0) - distance);
    double b = std::abs(model_.own.at(lo - 1, 0) - distance);
    return a < b ? lo : lo - 1;
}

double App::interp_own_z(double distance) const {
    if (model_.own.empty()) return 0.0;
    size_t idx = nearest_own_index(distance);
    return model_.own.at(idx, 3) - model_.height_origin;
}

std::optional<TrackPoint> App::track_info_at(double distance) const {
    if (model_.own.empty()) return std::nullopt;
    if (distance < model_.own.at(0, 0) || distance > model_.own.at(model_.own.rows - 1, 0)) return std::nullopt;
    size_t idx = nearest_own_index(distance);
    TrackPoint p;
    p.d = distance;
    p.x = model_.own.at(idx, 1);
    p.y = model_.own.at(idx, 2);
    p.z = model_.own.at(idx, 3) - model_.height_origin;
    p.theta = model_.own.at(idx, 4);
    p.radius = model_.own.at(idx, 5);
    p.gradient = model_.own.at(idx, 6);
    return p;
}

std::optional<SpeedLimit> App::speed_at(double distance) const {
    std::optional<SpeedLimit> result;
    for (const auto& s : model_.speedlimits) {
        if (s.distance > distance) break;
        result = s;
    }
    return result;
}

std::vector<Section> App::curve_sections(bool transition) const {
    std::vector<Section> sections;
    std::vector<TrackEvent> radius;
    for (const auto& e : model_.own_events) {
        if (e.key == "radius") radius.push_back(e);
    }
    for (size_t i = 0; i < radius.size();) {
        const auto& e = radius[i];
        if (!transition && e.flag.empty() && e.value_number && e.number != 0.0) {
            double start = e.distance;
            double value = e.number;
            ++i;
            double end = model_.own.empty() ? start : model_.own.at(model_.own.rows - 1, 0);
            while (i < radius.size()) {
                if (radius[i].flag.empty()) {
                    end = radius[i].distance;
                    break;
                }
                ++i;
            }
            if (start < dmax_ && end > dmin_) {
                sections.push_back({std::max(start, dmin_), std::min(end, dmax_), value});
            }
        } else if (transition && e.flag == "bt") {
            double start = e.distance;
            ++i;
            double end = model_.own.empty() ? start : model_.own.at(model_.own.rows - 1, 0);
            while (i < radius.size()) {
                if (radius[i].flag.empty()) {
                    end = radius[i].distance;
                    break;
                }
                ++i;
            }
            if (start < dmax_ && end > dmin_) {
                sections.push_back({std::max(start, dmin_), std::min(end, dmax_), 0.0});
            }
        } else {
            ++i;
        }
    }
    return sections;
}

static ImVec2 rotate_xy(double x, double y, double angle) {
    double c = std::cos(angle);
    double s = std::sin(angle);
    return ImVec2(static_cast<float>(c * x - s * y), static_cast<float>(s * x + c * y));
}

PlanData App::build_plan_data() const {
    PlanData out;
    if (!has_model_ || model_.own.empty()) return out;

    for (size_t r = 0; r < model_.own.rows; ++r) {
        double d = model_.own.at(r, 0);
        if (d < dmin_ || d > dmax_) continue;
        TrackPoint p;
        p.d = d;
        p.x = model_.own.at(r, 1);
        p.y = model_.own.at(r, 2);
        p.z = model_.own.at(r, 3);
        p.theta = model_.own.at(r, 4);
        p.radius = model_.own.at(r, 5);
        p.gradient = model_.own.at(r, 6);
        out.own.push_back(p);
    }
    if (out.own.empty()) return out;
    out.origin_angle = out.own.front().theta;
    double angle = -out.origin_angle;
    auto rotate_point = [angle](TrackPoint p) {
        ImVec2 q = rotate_xy(p.x, p.y, angle);
        p.x = q.x;
        p.y = q.y;
        p.theta += angle;
        return p;
    };
    for (auto& p : out.own) p = rotate_point(p);

    auto extend_bounds = [&](double x, double y) {
        if (out.xmin > out.xmax) {
            out.xmin = out.xmax = x;
            out.ymin = out.ymax = y;
        } else {
            out.xmin = std::min(out.xmin, x);
            out.xmax = std::max(out.xmax, x);
            out.ymin = std::min(out.ymin, y);
            out.ymax = std::max(out.ymax, y);
        }
    };
    out.xmin = 1.0;
    out.xmax = -1.0;
    for (const auto& p : out.own) extend_bounds(p.x, p.y);

    for (const auto& t : model_.other_tracks) {
        if (!t.visible || t.points.empty()) continue;
        PlanOther po;
        po.key = t.key;
        po.color = t.color;
        double rmin = std::max(dmin_, t.range_min);
        double rmax = std::min(dmax_, t.range_max);
        for (size_t r = 0; r < t.points.rows; ++r) {
            double d = t.points.at(r, 0);
            if (d < rmin || d > rmax) continue;
            TrackPoint p;
            p.d = d;
            p.x = t.points.at(r, 1);
            p.y = t.points.at(r, 2);
            p.z = t.points.at(r, 3);
            ImVec2 q = rotate_xy(p.x, p.y, angle);
            p.x = q.x;
            p.y = q.y;
            po.points.push_back(p);
            extend_bounds(p.x, p.y);
        }
        if (!po.points.empty()) out.other.push_back(std::move(po));
    }

    for (const auto& s : model_.stations) {
        if (s.distance < dmin_ || s.distance > dmax_) continue;
        ImVec2 q = rotate_xy(s.x, s.y, angle);
        out.stations.push_back({s, q.x, q.y});
    }

    for (const auto& s : model_.speedlimits) {
        if (s.distance < dmin_ || s.distance > dmax_) continue;
        size_t idx = nearest_own_index(s.distance);
        TrackPoint p;
        p.x = model_.own.at(idx, 1);
        p.y = model_.own.at(idx, 2);
        p.theta = model_.own.at(idx, 4);
        p = rotate_point(p);
        out.speedlimits.push_back({p.x, p.y, p.theta, s.has_speed, s.speed});
    }

    out.curve_sections = curve_sections(false);
    out.transition_sections = curve_sections(true);

    double pad = std::max({out.xmax - out.xmin, out.ymax - out.ymin, 1.0}) * 0.05;
    out.xmin -= pad; out.xmax += pad; out.ymin -= pad; out.ymax += pad;
    return out;
}

ProfileData App::build_profile_data() const {
    ProfileData out;
    if (!has_model_ || model_.own.empty()) return out;

    for (size_t r = 0; r < model_.own.rows; ++r) {
        double d = model_.own.at(r, 0);
        if (d < dmin_ || d > dmax_) continue;
        out.own_x.push_back(d);
        out.own_y.push_back(model_.own.at(r, 3) - model_.height_origin);
    }
    if (!out.own_y.empty()) {
        auto [mn, mx] = std::minmax_element(out.own_y.begin(), out.own_y.end());
        if (*mn != *mx) {
            out.ymin = *mn - (*mx - *mn) * 0.2;
            out.ymax = *mx + (*mx - *mn) * 0.1;
        } else {
            out.ymin = *mn - 5.0;
            out.ymax = *mx + 5.0;
        }
    }

    for (size_t r = 0; r < model_.curve.rows; ++r) {
        double d = model_.curve.at(r, 0);
        if (d < dmin_ || d > dmax_) continue;
        out.curve_x.push_back(d);
        double radius = model_.curve.at(r, 1);
        out.curve_y.push_back(radius > 0 ? 1.0 : (radius < 0 ? -1.0 : 0.0));
    }

    if (show_profile_other_) {
        for (const auto& t : model_.other_tracks) {
            if (!t.visible || t.points.empty()) continue;
            ProfileOther po;
            po.key = t.key;
            po.color = t.color;
            double rmin = std::max(dmin_, t.range_min);
            double rmax = std::min(dmax_, t.range_max);
            for (size_t r = 0; r < t.points.rows; ++r) {
                double d = t.points.at(r, 0);
                if (d < rmin || d > rmax) continue;
                po.x.push_back(d);
                po.y.push_back(t.points.at(r, 3) - model_.height_origin);
            }
            if (!po.x.empty()) out.other.push_back(std::move(po));
        }
    }

    for (const auto& s : model_.stations) {
        if (s.distance >= dmin_ && s.distance <= dmax_) out.stations.push_back(s);
    }

    std::vector<TrackEvent> gradients;
    std::vector<TrackEvent> radii;
    for (const auto& e : model_.own_events) {
        if (e.key == "gradient") gradients.push_back(e);
        if (e.key == "radius") radii.push_back(e);
    }
    for (const auto& e : gradients) {
        if (e.distance >= dmin_ && e.distance <= dmax_) {
            out.gradient_points.push_back({e.distance, interp_own_z(e.distance), ""});
        }
    }
    double last_d = model_.own.empty() ? dmin_ : model_.own.at(0, 0);
    double last_g = 0.0;
    bool in_transition = false;
    auto append_gradient_label = [&](double start, double end, double value) {
        double seg_start = std::max(start, dmin_);
        double seg_end = std::min(end, dmax_);
        if (seg_end > seg_start) {
            double mid = (seg_start + seg_end) * 0.5;
            out.gradient_labels.push_back({mid, 0.0, value == 0.0 ? tr("plot.level") : format_double(std::abs(value), 1)});
        }
    };
    for (const auto& e : gradients) {
        if (!in_transition) append_gradient_label(last_d, e.distance, last_g);
        if (e.value_number) last_g = e.number;
        if (e.flag == "bt" || e.flag == "i") in_transition = true;
        else if (e.flag.empty()) in_transition = false;
        last_d = e.distance;
    }
    if (last_d < dmax_) {
        if (!in_transition) append_gradient_label(last_d, dmax_, last_g);
    }

    for (size_t i = 0; i + 1 < radii.size(); ++i) {
        const auto& e = radii[i];
        if (!e.value_number || e.number == 0.0) continue;
        double start = std::max(e.distance, dmin_);
        double end = std::min(radii[i + 1].distance, dmax_);
        if (end > start) {
            out.radius_labels.push_back({(start + end) * 0.5, e.number > 0 ? 1.5 : -1.5, format_double(std::abs(e.number), 0)});
        }
    }
    return out;
}

void App::clear_measure() {
    measure_distance_.reset();
    measure_text_.clear();
}

void App::update_measure(double distance) {
    auto info = track_info_at(distance);
    if (!info) {
        clear_measure();
        return;
    }
    measure_distance_ = distance;
    auto sp = speed_at(distance);
    std::string speed_text = tr("info.no_limit");
    if (sp && sp->has_speed) speed_text = format_double(sp->speed, 0) + " km/h";
    std::ostringstream out;
    out << tr("info.mileage") << ": " << format_double(distance - model_.distance_origin, 0) << "m | "
        << tr("info.elevation") << ": " << format_double(info->z, 1) << "m | "
        << tr("info.gradient") << ": " << format_double(info->gradient, 1) << "‰ | "
        << tr("info.radius") << ": " << format_double(info->radius, 0) << "m | "
        << tr("info.speedlimit") << ": " << speed_text;
    measure_text_ = out.str();
}

void App::center_plan_at_distance(double distance) {
    PlanData pd = build_plan_data();
    if (pd.own.empty()) return;
    auto it = std::lower_bound(pd.own.begin(), pd.own.end(), distance, [](const TrackPoint& p, double d) { return p.d < d; });
    if (it == pd.own.end()) {
        --it;
    } else if (it != pd.own.begin() && std::abs((it - 1)->d - distance) < std::abs(it->d - distance)) {
        --it;
    }
    plan_view_.cx = it->x;
    plan_view_.cy = it->y;
    plan_view_.fitted = true;
    keep_plan_view_ = true;
}

void App::request_plot_focus(double distance, bool include_profile, bool include_radius) {
    if (include_profile && show_profile_graph_) {
        focus_profile_next_ = true;
        focus_profile_distance_ = distance;
    }
    if (include_radius && show_radius_graph_) {
        focus_radius_next_ = true;
        focus_radius_distance_ = distance;
    }
}

void App::handle_measure_plot_double_click(bool include_profile, bool include_radius) {
    if (mode_ != Mode::Measure || !ImPlot::IsPlotHovered() || !ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) return;
    ImPlotPoint p = ImPlot::GetPlotMousePos();
    if (p.x < dmin_ || p.x > dmax_) return;
    update_measure(p.x);
    center_plan_at_distance(p.x);
    request_plot_focus(p.x, include_profile, include_radius);
}

void App::focus_station(double distance) {
    PlanData pd = build_plan_data();
    for (const auto& s : pd.stations) {
        if (std::abs(s.station.distance - distance) < 1e-6) {
            plan_view_.cx = s.x;
            plan_view_.cy = s.y;
            plan_view_.fitted = true;
            keep_plan_view_ = true;
            break;
        }
    }
    request_plot_focus(distance, true, true);
}

std::string App::open_map_dialog() {
    wchar_t file[MAX_PATH] = {};
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = nullptr;
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrFilter = L"BVE map files\0*.map;*.txt\0All files\0*.*\0";
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
    if (GetOpenFileNameW(&ofn)) return wide_to_utf8(file);
    return {};
}

std::string App::open_image_dialog() {
    wchar_t file[MAX_PATH] = {};
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = nullptr;
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrFilter = L"Images\0*.png;*.jpg;*.jpeg;*.bmp;*.tif;*.tiff\0All files\0*.*\0";
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
    if (GetOpenFileNameW(&ofn)) return wide_to_utf8(file);
    return {};
}

std::string App::choose_folder_dialog() {
    BROWSEINFOW bi = {};
    bi.lpszTitle = L"Select export folder";
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    PIDLIST_ABSOLUTE pidl = SHBrowseForFolderW(&bi);
    if (!pidl) return {};
    wchar_t path[MAX_PATH] = {};
    SHGetPathFromIDListW(pidl, path);
    CoTaskMemFree(pidl);
    return wide_to_utf8(path);
}

void App::save_history() {
    if (!save_history_entries(history_path_, recent_maps_)) {
        add_log("[WARN] Failed to save history.ini");
    }
}

void App::touch_recent_map(const std::string& path) {
    std::string stored_path = normalized_storage_path(path);
    std::string key = normalized_path_key(stored_path);
    if (key.empty()) return;

    RecentMapEntry selected;
    selected.path = stored_path;
    std::vector<RecentMapEntry> kept;
    bool found = false;
    for (const auto& entry : recent_maps_) {
        if (normalized_path_key(entry.path) == key) {
            if (!found) {
                selected = entry;
                selected.path = stored_path;
                found = true;
            }
        } else {
            kept.push_back(entry);
        }
    }
    kept.insert(kept.begin(), std::move(selected));
    if (kept.size() > kMaxRecentMaps) kept.resize(kMaxRecentMaps);
    recent_maps_ = std::move(kept);
    save_history();
}

BackgroundHistory App::current_background_history() const {
    BackgroundHistory bg;
    if (bg_image_.path.empty()) return bg;
    bg.has_image = true;
    bg.image_path = normalized_storage_path(bg_image_.path);
    bg.x = bg_x_;
    bg.y = bg_y_;
    bg.width = bg_width_;
    bg.height = bg_height_;
    bg.rotation_deg = bg_rotation_deg_;
    bg.brightness = bg_brightness_;
    return bg;
}

void App::save_current_background_to_history() {
    if (file_path_.empty()) return;
    if (bg_image_.path.empty()) return;
    std::string stored_path = normalized_storage_path(file_path_);
    std::string key = normalized_path_key(stored_path);
    if (key.empty()) return;

    RecentMapEntry selected;
    selected.path = stored_path;
    std::vector<RecentMapEntry> kept;
    bool found = false;
    for (const auto& entry : recent_maps_) {
        if (normalized_path_key(entry.path) == key) {
            if (!found) {
                selected = entry;
                selected.path = stored_path;
                found = true;
            }
        } else {
            kept.push_back(entry);
        }
    }
    selected.background = current_background_history();
    kept.insert(kept.begin(), std::move(selected));
    if (kept.size() > kMaxRecentMaps) kept.resize(kMaxRecentMaps);
    recent_maps_ = std::move(kept);
    save_history();
}

void App::sync_pending_background_values() {
    pending_bg_x_ = bg_x_;
    pending_bg_y_ = bg_y_;
    pending_bg_width_ = bg_width_;
    pending_bg_height_ = bg_height_;
    pending_bg_rotation_deg_ = bg_rotation_deg_;
    pending_bg_brightness_ = bg_brightness_;
}

void App::apply_pending_background_values(bool save_history_entry) {
    bg_x_ = pending_bg_x_;
    bg_y_ = pending_bg_y_;
    bg_width_ = pending_bg_width_;
    bg_height_ = pending_bg_height_;
    bg_rotation_deg_ = pending_bg_rotation_deg_;
    bg_brightness_ = std::clamp(pending_bg_brightness_, 1.0, 200.0);
    pending_bg_brightness_ = bg_brightness_;
    if (!bg_image_.pixels_rgba.empty()) rebuild_background_texture();
    if (save_history_entry) save_current_background_to_history();
}

void App::clear_background_image() {
    bg_image_.release();
    bg_show_ = false;
    sync_pending_background_values();
}

bool App::apply_background_history(const BackgroundHistory& background) {
    if (!background.has_image || background.image_path.empty()) {
        clear_background_image();
        return true;
    }

    std::error_code ec;
    bool exists = std::filesystem::exists(std::filesystem::path(utf8_to_wide(background.image_path)), ec);
    if (ec || !exists) {
        clear_background_image();
        std::string message = "[WARN] Background image not found: " + background.image_path;
        add_log(message);
        std::cerr << message << std::endl;
        return false;
    }

    bg_x_ = background.x;
    bg_y_ = background.y;
    bg_width_ = background.width;
    bg_height_ = background.height;
    bg_rotation_deg_ = background.rotation_deg;
    bg_brightness_ = std::clamp(background.brightness, 1.0, 200.0);
    bg_show_ = true;
    if (!load_background_image(background.image_path, false)) {
        bg_show_ = false;
        sync_pending_background_values();
        return false;
    }
    if (bg_width_ <= 0.0) bg_width_ = static_cast<double>(bg_image_.width);
    if (bg_height_ <= 0.0) bg_height_ = static_cast<double>(bg_image_.height);
    sync_pending_background_values();
    return true;
}

bool App::load_background_image(const std::string& path, bool reset_parameters) {
    bg_image_.release();
    bg_image_.path = path;
    IWICImagingFactory* factory = nullptr;
    IWICBitmapDecoder* decoder = nullptr;
    IWICBitmapFrameDecode* frame = nullptr;
    IWICFormatConverter* converter = nullptr;
    UINT w = 0;
    UINT h = 0;

    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
    if (FAILED(hr)) goto fail;
    hr = factory->CreateDecoderFromFilename(utf8_to_wide(path).c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnLoad, &decoder);
    if (FAILED(hr)) goto fail;
    hr = decoder->GetFrame(0, &frame);
    if (FAILED(hr)) goto fail;
    hr = factory->CreateFormatConverter(&converter);
    if (FAILED(hr)) goto fail;
    hr = converter->Initialize(frame, GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom);
    if (FAILED(hr)) goto fail;
    converter->GetSize(&w, &h);
    bg_image_.width = static_cast<int>(w);
    bg_image_.height = static_cast<int>(h);
    bg_image_.pixels_rgba.resize(static_cast<size_t>(w) * h * 4);
    hr = converter->CopyPixels(nullptr, w * 4, static_cast<UINT>(bg_image_.pixels_rgba.size()), bg_image_.pixels_rgba.data());
    if (FAILED(hr)) goto fail;

    release_com(converter);
    release_com(frame);
    release_com(decoder);
    release_com(factory);
    if (reset_parameters) {
        bg_width_ = static_cast<double>(w);
        bg_height_ = static_cast<double>(h);
        bg_brightness_ = 100.0;
    }
    return rebuild_background_texture();

fail:
    release_com(converter);
    release_com(frame);
    release_com(decoder);
    release_com(factory);
    bg_image_.release();
    add_log("[ERROR]Failed to load background image: " + path);
    return false;
}

bool App::rebuild_background_texture() {
    if (!device_ || bg_image_.pixels_rgba.empty()) return false;
    release_com(bg_image_.srv);
    std::vector<unsigned char> adjusted = bg_image_.pixels_rgba;
    double mul = std::clamp(bg_brightness_, 1.0, 200.0) / 100.0;
    for (size_t i = 0; i + 3 < adjusted.size(); i += 4) {
        adjusted[i + 0] = static_cast<unsigned char>(std::clamp(adjusted[i + 0] * mul, 0.0, 255.0));
        adjusted[i + 1] = static_cast<unsigned char>(std::clamp(adjusted[i + 1] * mul, 0.0, 255.0));
        adjusted[i + 2] = static_cast<unsigned char>(std::clamp(adjusted[i + 2] * mul, 0.0, 255.0));
    }

    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = static_cast<UINT>(bg_image_.width);
    desc.Height = static_cast<UINT>(bg_image_.height);
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA sub = {};
    sub.pSysMem = adjusted.data();
    sub.SysMemPitch = static_cast<UINT>(bg_image_.width * 4);
    ID3D11Texture2D* texture = nullptr;
    HRESULT hr = device_->CreateTexture2D(&desc, &sub, &texture);
    if (FAILED(hr)) return false;

    D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
    srv_desc.Format = desc.Format;
    srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srv_desc.Texture2D.MipLevels = 1;
    hr = device_->CreateShaderResourceView(texture, &srv_desc, &bg_image_.srv);
    texture->Release();
    if (FAILED(hr)) return false;
    bg_image_.brightness = bg_brightness_;
    return true;
}

std::optional<ImVec2> App::background_uv_from_world(ImVec2 world) const {
    if (bg_width_ <= 0.0 || bg_height_ <= 0.0) return std::nullopt;
    double rot = bg_rotation_deg_ * 3.14159265358979323846 / 180.0;
    double dx = world.x - bg_x_;
    double dy = world.y - bg_y_;
    double local_x = dx * std::cos(rot) + dy * std::sin(rot);
    double local_y = -dx * std::sin(rot) + dy * std::cos(rot);
    return ImVec2(static_cast<float>(local_x / bg_width_), static_cast<float>(local_y / bg_height_));
}

void App::draw_background(ImDrawList* draw, const View2D& view, ImVec2 origin, ImVec2 size) {
    if (!bg_show_ || !bg_image_.srv || bg_width_ <= 0 || bg_height_ <= 0) return;
    double rot = bg_rotation_deg_ * 3.14159265358979323846 / 180.0;
    double c = std::cos(rot);
    double s = std::sin(rot);
    double hw = bg_width_ * 0.5;
    double hh = bg_height_ * 0.5;
    ImVec2 local[4] = {
        ImVec2(static_cast<float>(-hw), static_cast<float>(-hh)),
        ImVec2(static_cast<float>( hw), static_cast<float>(-hh)),
        ImVec2(static_cast<float>( hw), static_cast<float>( hh)),
        ImVec2(static_cast<float>(-hw), static_cast<float>( hh))
    };
    ImVec2 p[4];
    for (int i = 0; i < 4; ++i) {
        double x = bg_x_ + c * local[i].x - s * local[i].y;
        double y = bg_y_ + s * local[i].x + c * local[i].y;
        p[i] = view.world_to_screen(x, y, origin, size);
    }
    draw->AddImageQuad((void*)bg_image_.srv, p[0], p[1], p[2], p[3]);
}

void App::apply_background_alignment() {
    if (!has_model_ || model_.stations.size() < 2 || !align_pick1_ || !align_pick2_) return;
    if (bg_image_.path.empty() || bg_width_ <= 0.0 || bg_height_ <= 0.0) return;
    align_station1_ = std::clamp(align_station1_, 0, static_cast<int>(model_.stations.size()) - 1);
    align_station2_ = std::clamp(align_station2_, 0, static_cast<int>(model_.stations.size()) - 1);
    PlanData pd = build_plan_data();
    auto find_station = [&](const std::string& key) -> std::optional<ImVec2> {
        for (const auto& s : pd.stations) {
            if (s.station.key == key) return ImVec2(static_cast<float>(s.x), static_cast<float>(s.y));
        }
        return std::nullopt;
    };
    auto s1 = find_station(model_.stations[align_station1_].key);
    auto s2 = find_station(model_.stations[align_station2_].key);
    if (!s1 || !s2) return;

    double dsx = s2->x - s1->x;
    double dsy = s2->y - s1->y;
    double ds_dist = std::hypot(dsx, dsy);
    if (ds_dist < 1e-6) return;

    ImVec2 q1 = *align_pick1_;
    ImVec2 q2 = *align_pick2_;
    ImVec2 u1(static_cast<float>(q1.x * bg_width_), static_cast<float>(q1.y * bg_height_));
    ImVec2 u2(static_cast<float>(q2.x * bg_width_), static_cast<float>(q2.y * bg_height_));
    double du = u2.x - u1.x;
    double dv = u2.y - u1.y;
    double duv_dist = std::hypot(du, dv);
    if (duv_dist < 1e-6) return;
    double scale = ds_dist / duv_dist;
    double angle_duv = std::atan2(dv, du);
    double angle_ds = std::atan2(dsy, dsx);
    double new_rot = angle_ds - angle_duv;
    double cosr = std::cos(new_rot);
    double sinr = std::sin(new_rot);
    double sx_u1 = scale * (u1.x * cosr - u1.y * sinr);
    double sy_u1 = scale * (u1.x * sinr + u1.y * cosr);
    bg_x_ = s1->x - sx_u1;
    bg_y_ = s1->y - sy_u1;
    bg_width_ *= scale;
    bg_height_ *= scale;
    bg_rotation_deg_ = std::fmod(new_rot * 180.0 / 3.14159265358979323846 + 360.0, 360.0);
    sync_pending_background_values();
    save_current_background_to_history();
}

void App::export_csv() {
    if (!has_model_) return;
    std::string folder = choose_folder_dialog();
    if (folder.empty()) return;
    std::filesystem::path dir = utf8_to_wide(folder);
    std::string base = narrow_path(dir.filename());
    if (base.empty()) base = "kobushi";

    auto write_matrix = [&](const std::filesystem::path& path, const Matrix& m, const std::string& header) {
        std::ofstream out(path, std::ios::binary);
        out << "#" << header << "\n";
        for (size_t r = 0; r < m.rows; ++r) {
            for (size_t c = 0; c < m.cols; ++c) {
                if (c) out << ",";
                out << std::fixed << std::setprecision(6) << m.at(r, c);
            }
            out << "\n";
        }
    };
    write_matrix(dir / utf8_to_wide(base + "_owntrack.csv"), model_.own,
                 "distance,x,y,z,direction,radius,gradient,interpolate_func,cant,center,gauge");
    for (const auto& t : model_.other_tracks) {
        write_matrix(dir / utf8_to_wide(base + "_" + sanitize_filename(t.key) + ".csv"), t.points,
                     "distance,x,y,z,interpolate_func,cant,center,gauge");
    }
    add_log("CSV exported: " + folder);
}

void App::setup_initial_dockspace(ImGuiID dockspace_id) {
    static bool docked = false;
    if (docked) return;
    docked = true;
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::DockBuilderRemoveNode(dockspace_id);
    ImGui::DockBuilderAddNode(dockspace_id, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(dockspace_id, viewport->Size);

    ImGuiID dock_main = dockspace_id;
    ImGuiID dock_right = ImGui::DockBuilderSplitNode(dock_main, ImGuiDir_Right, 0.23f, nullptr, &dock_main);
    ImGuiID dock_console = ImGui::DockBuilderSplitNode(dock_right, ImGuiDir_Down, 0.32f, nullptr, &dock_right);
    dock_right_id_ = dock_right;
    ImGui::DockBuilderDockWindow("OtherTracks", dock_right);
    ImGui::DockBuilderDockWindow("StationList", dock_right);
    ImGui::DockBuilderDockWindow("Structures", dock_right);
    ImGui::DockBuilderDockWindow("Repeaters", dock_right);
    ImGui::DockBuilderDockWindow("Console", dock_console);
    ImGui::DockBuilderDockWindow("Plots", dock_main);
    ImGui::DockBuilderFinish(dockspace_id);
}

void App::render_menu() {
    if (!ImGui::BeginMainMenuBar()) return;
    if (ImGui::BeginMenu(tr("menu.file").c_str())) {
        if (ImGui::MenuItem(tr("menu.open").c_str(), "Ctrl+O")) {
            std::string p = open_map_dialog();
            if (!p.empty()) begin_load(p, false, true);
        }
        if (ImGui::BeginMenu(tr("menu.recent_maps").c_str())) {
            if (recent_maps_.empty()) {
                ImGui::MenuItem(tr("menu.none").c_str(), nullptr, false, false);
            } else {
                for (size_t i = 0; i < recent_maps_.size(); ++i) {
                    const RecentMapEntry& entry = recent_maps_[i];
                    std::string label = display_name_from_path(entry.path) + "###recent_map_" + std::to_string(i);
                    if (ImGui::MenuItem(label.c_str(), nullptr, false, !loading_)) {
                        begin_load(entry.path, false, true, entry.background);
                    }
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", entry.path.c_str());
                }
            }
            ImGui::Separator();
            if (ImGui::MenuItem(tr("menu.clear_recent_maps").c_str())) {
                recent_maps_.clear();
                save_history();
            }
            ImGui::EndMenu();
        }
        if (ImGui::MenuItem(tr("menu.reload").c_str(), "F5", false, has_model_ && !loading_)) {
            begin_load(file_path_, true);
        }
        if (ImGui::MenuItem(tr("menu.export_csv").c_str(), nullptr, false, has_model_)) export_csv();
        if (ImGui::MenuItem(tr("menu.exit").c_str())) PostQuitMessage(0);
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu(tr("menu.options").c_str())) {
        if (ImGui::MenuItem(tr("menu.ui_settings").c_str())) {
            pending_font_size_ = font_size_;
            font_size_before_dialog_ = font_size_;
            pending_ui_component_size_ = ui_component_size_;
            ui_component_size_before_dialog_ = ui_component_size_;
            pending_station_marker_size_ = station_marker_size_;
            station_marker_size_before_dialog_ = station_marker_size_;
            pending_theme_color_ = theme_color_;
            theme_color_before_dialog_ = theme_color_;
            show_font_size_popup_ = true;
        }
        if (ImGui::MenuItem(tr("menu.plotlimit").c_str(), nullptr, false, has_model_)) {
            plot_min_ = dmin_;
            plot_max_ = dmax_;
            show_range_popup_ = true;
        }
        if (ImGui::MenuItem(tr("menu.controlpoints").c_str(), nullptr, false, has_model_)) {
            cp_start_ = model_.cp_arb[0];
            cp_end_ = model_.cp_arb[1];
            cp_interval_ = model_.cp_arb[2];
            show_cp_popup_ = true;
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu(tr("menu.map_info").c_str())) {
        if (ImGui::MenuItem(tr("frame.othertracks").c_str(), nullptr, false, !show_othertracks_window_)) {
            show_othertracks_window_ = true;
        }
        if (ImGui::MenuItem(tr("frame.station_list").c_str(), nullptr, false, !show_station_list_window_)) {
            show_station_list_window_ = true;
        }
        if (ImGui::MenuItem(tr("button.structure_list").c_str(), nullptr, false, !show_structures_window_)) {
            show_structures_window_ = true;
        }
        if (ImGui::MenuItem(tr("button.repeater_list").c_str(), nullptr, false, !show_repeaters_window_)) {
            show_repeaters_window_ = true;
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu(tr("menu.view_2d").c_str())) {
        ImGui::MenuItem(tr("frame.aux_info").c_str(), nullptr, false, false);
        ImGui::MenuItem(tr("chk.station_pos").c_str(), nullptr, &show_stations_);
        ImGui::MenuItem(tr("chk.station_name").c_str(), nullptr, &show_station_names_);
        ImGui::MenuItem(tr("chk.station_mileage").c_str(), nullptr, &show_station_mileage_);
        ImGui::MenuItem(tr("chk.curve_val").c_str(), nullptr, &show_curve_values_);
        ImGui::MenuItem(tr("chk.speedlimit").c_str(), nullptr, &show_speedlimits_);
        ImGui::Separator();
        ImGui::MenuItem(tr("frame.chart_visibility").c_str(), nullptr, false, false);
        if (ImGui::MenuItem(tr("chk.gradient_graph").c_str(), nullptr, &show_profile_graph_) && show_profile_graph_) reset_profile_axes_next_ = true;
        if (ImGui::MenuItem(tr("chk.curve_graph").c_str(), nullptr, &show_radius_graph_) && show_radius_graph_) reset_radius_axes_next_ = true;
        ImGui::MenuItem(tr("chk.gradient_pos").c_str(), nullptr, &show_gradient_pos_);
        ImGui::MenuItem(tr("chk.gradient_val").c_str(), nullptr, &show_gradient_values_);
        ImGui::MenuItem(tr("chk.prof_othert").c_str(), nullptr, &show_profile_other_);
        ImGui::Separator();
        ImGui::MenuItem(tr("frame.bgimage").c_str(), nullptr, false, false);
        if (ImGui::MenuItem(tr("button.import_bg").c_str())) {
            std::string p = open_image_dialog();
            if (!p.empty() && load_background_image(p)) {
                bg_show_ = true;
                sync_pending_background_values();
                save_current_background_to_history();
            }
        }
        ImGui::MenuItem(tr("chk.bgimg_show").c_str(), nullptr, &bg_show_);
        if (ImGui::MenuItem(tr("button.adjust_bg").c_str())) {
            sync_pending_background_values();
            show_bg_adjust_popup_ = true;
        }
        if (ImGui::MenuItem(tr("button.align_to_station").c_str(), nullptr, false,
                            has_model_ && model_.stations.size() >= 2 && !bg_image_.path.empty())) {
            align_pick1_.reset();
            align_pick2_.reset();
            pick_slot_ = 0;
            show_align_popup_ = true;
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu(tr("menu.lang").c_str())) {
        auto set_language = [&](Language lang) {
            if (lang_ == lang) return;
            lang_ = lang;
            settings_.language = lang_;
            save_user_settings(settings_);
        };
        if (ImGui::MenuItem("简体中文", nullptr, lang_ == Language::Zh)) set_language(Language::Zh);
        if (ImGui::MenuItem("English", nullptr, lang_ == Language::En)) set_language(Language::En);
        if (ImGui::MenuItem("日本語", nullptr, lang_ == Language::Ja)) set_language(Language::Ja);
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu(tr("menu.help").c_str())) {
        if (ImGui::MenuItem(tr("menu.online_docs").c_str())) {
            ShellExecuteW(nullptr, L"open", L"https://github.com/NewSapporoNingyo/komapedit", nullptr, nullptr, SW_SHOWNORMAL);
        }
        if (ImGui::MenuItem(tr("menu.about").c_str())) show_about_popup_ = true;
        ImGui::EndMenu();
    }
    ImGui::EndMainMenuBar();
}

void App::render_toolbar() {
    ImGuiStyle& style = ImGui::GetStyle();
    ImVec4 toolbar_bg = style.Colors[ImGuiCol_WindowBg];
    toolbar_bg.x = std::min(toolbar_bg.x + 0.035f, 1.0f);
    toolbar_bg.y = std::min(toolbar_bg.y + 0.035f, 1.0f);
    toolbar_bg.z = std::min(toolbar_bg.z + 0.035f, 1.0f);

    const float button_height = ImGui::GetFrameHeight();
    const float toolbar_padding_y = button_height * 0.25f;
    const float toolbar_height = button_height + toolbar_padding_y * 2.0f;
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings;
    ImGui::PushStyleColor(ImGuiCol_WindowBg, toolbar_bg);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(style.WindowPadding.x, toolbar_padding_y));
    bool visible = ImGui::BeginViewportSideBar("##MainToolbar", ImGui::GetMainViewport(), ImGuiDir_Up, toolbar_height, flags);
    if (visible) {
        if (ImGui::Button(tr("button.open").c_str())) {
            std::string p = open_map_dialog();
            if (!p.empty()) begin_load(p, false, true);
        }
        ImGui::SameLine();

        const bool can_reload = has_model_ && !loading_ && !file_path_.empty();
        ImGui::BeginDisabled(!can_reload);
        if (ImGui::Button(tr("button.reload").c_str())) begin_load(file_path_, true);
        ImGui::EndDisabled();

        ImGui::SameLine(0.0f, style.ItemSpacing.x);
        ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
        ImGui::SameLine(0.0f, style.ItemSpacing.x);
        render_station_jump_combo();
    }
    ImGui::End();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
}

void App::render_mode_grid_controls() {
    ImGui::PushID("PlanModeGridControls");
    ImGui::AlignTextToFramePadding();
    ImGui::Text("%s:", tr("frame.mode").c_str());
    ImGui::SameLine();
    Mode previous_mode = mode_;
    int mode = mode_ == Mode::Pan ? 0 : 1;
    if (ImGui::RadioButton(tr("mode.pan").c_str(), &mode, 0)) mode_ = Mode::Pan;
    ImGui::SameLine();
    if (ImGui::RadioButton(tr("mode.measure").c_str(), &mode, 1)) mode_ = Mode::Measure;
    if (previous_mode != mode_ && mode_ == Mode::Pan) clear_measure();

    ImGui::SameLine(0.0f, ImGui::GetStyle().ItemSpacing.x * 2.0f);
    ImGui::AlignTextToFramePadding();
    ImGui::Text("%s:", tr("frame.grid").c_str());
    ImGui::SameLine();
    int grid = grid_mode_ == GridMode::Fixed ? 0 : (grid_mode_ == GridMode::Movable ? 1 : 2);
    if (ImGui::RadioButton(tr("grid.fixed").c_str(), &grid, 0)) grid_mode_ = GridMode::Fixed;
    ImGui::SameLine();
    if (ImGui::RadioButton(tr("grid.movable").c_str(), &grid, 1)) grid_mode_ = GridMode::Movable;
    ImGui::SameLine();
    if (ImGui::RadioButton(tr("grid.none").c_str(), &grid, 2)) grid_mode_ = GridMode::None;
    ImGui::PopID();
}

void App::render_station_jump_combo() {
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(tr("label.station_jump").c_str());
    ImGui::SameLine();

    const bool can_jump = has_model_ && !model_.stations.empty();
    std::string preview;
    const char* preview_text = "-";
    if (can_jump) {
        station_jump_index_ = std::clamp(station_jump_index_, 0, static_cast<int>(model_.stations.size()) - 1);
        preview = model_.stations[station_jump_index_].key + ", " + model_.stations[station_jump_index_].name;
        preview_text = preview.c_str();
    }

    ImGui::BeginDisabled(!can_jump);
    ImGui::SetNextItemWidth(std::max(1.0f, std::min(360.0f, ImGui::GetContentRegionAvail().x)));
    if (ImGui::BeginCombo("##toolbar_station", preview_text)) {
        for (int i = 0; i < static_cast<int>(model_.stations.size()); ++i) {
            std::string label = model_.stations[i].key + ", " + model_.stations[i].name;
            const bool selected = i == station_jump_index_;
            if (ImGui::Selectable(label.c_str(), selected)) {
                station_jump_index_ = i;
                focus_station(model_.stations[i].distance);
            }
            if (selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    ImGui::EndDisabled();
}

void App::render_console() {
    std::string title = tr("frame.console") + "###Console";
    ImGui::Begin(title.c_str());
    if (ImGui::Button(tr("button.clear").c_str())) {
        std::lock_guard<std::mutex> lock(log_mutex_);
        logs_.clear();
        last_log_.clear();
        error_count_ = warn_count_ = 0;
    }
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "E %d", error_count_);
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(1.0f, 0.78f, 0.25f, 1.0f), "W %d", warn_count_);
    ImGui::Separator();
    ImGui::BeginChild("console_scroll", ImVec2(0, 0), true, ImGuiWindowFlags_HorizontalScrollbar);
    std::lock_guard<std::mutex> lock(log_mutex_);
    for (const auto& line : logs_) {
        ImVec4 color = line.severity == 2 ? ImVec4(1.0f, 0.35f, 0.35f, 1.0f)
                     : line.severity == 1 ? ImVec4(1.0f, 0.78f, 0.25f, 1.0f)
                                          : ImVec4(0.88f, 0.88f, 0.88f, 1.0f);
        ImGui::TextColored(color, "%s", line.text.c_str());
    }
    if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 4.0f) ImGui::SetScrollHereY(1.0f);
    ImGui::EndChild();
    ImGui::End();
}

static ImU32 color_u32(const ImVec4& color) {
    return ImGui::ColorConvertFloat4ToU32(color);
}

static void draw_polyline(ImDrawList* draw, const std::vector<TrackPoint>& points, const View2D& view,
                          ImVec2 origin, ImVec2 size, ImU32 color, float thickness) {
    if (points.size() < 2) return;
    std::vector<ImVec2> screen;
    screen.reserve(points.size());
    for (const auto& p : points) screen.push_back(view.world_to_screen(p.x, p.y, origin, size));
    draw->AddPolyline(screen.data(), static_cast<int>(screen.size()), color, ImDrawFlags_None, thickness);
}

static double grid_step(double span) {
    double raw = std::max(span / 8.0, 1e-9);
    double mag = std::pow(10.0, std::floor(std::log10(raw)));
    for (double f : {1.0, 2.0, 5.0, 10.0}) {
        if (raw <= f * mag) return f * mag;
    }
    return 10.0 * mag;
}

static double friendly_scalebar_length(double raw_length) {
    if (raw_length <= 0.0 || !std::isfinite(raw_length)) return 1.0;
    double magnitude = std::pow(10.0, std::floor(std::log10(raw_length)));
    double best = magnitude;
    double best_diff = std::numeric_limits<double>::max();
    for (int exp_offset : {-1, 0, 1, 2}) {
        double base = magnitude * std::pow(10.0, exp_offset);
        for (double factor : {1.0, 2.0, 3.0, 5.0}) {
            double candidate = factor * base;
            if (candidate <= 0.0) continue;
            double diff = std::abs(candidate - raw_length);
            if (diff < best_diff) {
                best = candidate;
                best_diff = diff;
            }
        }
    }
    return best;
}

static std::string format_scalebar_label(double length) {
    if (length >= 1000.0) {
        double km = length / 1000.0;
        return format_double(km, std::abs(km - std::round(km)) < 1e-9 ? 0 : 1) + "km";
    }
    return format_double(length, std::abs(length - std::round(length)) < 1e-9 ? 0 : 1) + "m";
}

static void draw_scalebar(ImDrawList* draw, const View2D& view, ImVec2 origin, ImVec2 size) {
    if (view.scale <= 0.0 || !std::isfinite(view.scale)) return;
    float target_px = std::clamp(size.x * 0.18f, 90.0f, 180.0f);
    double length = friendly_scalebar_length(static_cast<double>(target_px) / view.scale);
    float bar_px = static_cast<float>(length * view.scale);
    if (!std::isfinite(bar_px) || bar_px <= 0.0f) return;

    float margin = 24.0f;
    float tick = 10.0f;
    ImVec2 p2(origin.x + size.x - margin, origin.y + size.y - margin);
    ImVec2 p1(p2.x - bar_px, p2.y);
    if (p1.x < origin.x + margin) return;

    ImU32 color = IM_COL32(255, 255, 255, 255);
    ImVec2 points[] = {ImVec2(p1.x, p1.y - tick), p1, p2, ImVec2(p2.x, p2.y - tick)};
    draw->AddPolyline(points, IM_ARRAYSIZE(points), color, ImDrawFlags_None, 2.0f);
    std::string label = format_scalebar_label(length);
    ImVec2 text_size = ImGui::CalcTextSize(label.c_str());
    draw->AddText(ImVec2((p1.x + p2.x - text_size.x) * 0.5f, p1.y - tick - 4.0f - text_size.y),
                  color, label.c_str());
}

void App::render_plan_canvas(ImVec2 size) {
    PlanData data = build_plan_data();
    ImGui::BeginChild("PlanCanvasChild", size, true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImVec2 origin = ImGui::GetCursorScreenPos();
    ImVec2 avail = ImGui::GetContentRegionAvail();
    avail.x = std::max(avail.x, 50.0f);
    avail.y = std::max(avail.y, 50.0f);
    ImGui::InvisibleButton("PlanCanvasButton", avail, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
    bool hovered = ImGui::IsItemHovered();
    bool picking_background_station = pick_slot_ != 0;
    if (hovered && picking_background_station) {
        set_crosshair_cursor();
    }
    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(origin, ImVec2(origin.x + avail.x, origin.y + avail.y), IM_COL32(12, 13, 15, 255));
    draw->PushClipRect(origin, ImVec2(origin.x + avail.x, origin.y + avail.y), true);

    if (!data.own.empty() && (!plan_view_.fitted || !keep_plan_view_)) {
        plan_view_.fit(data.xmin, data.ymin, data.xmax, data.ymax, avail);
        keep_plan_view_ = true;
    }

    ImGuiIO& io = ImGui::GetIO();
    ImVec2 mouse = io.MousePos;
    auto nearest_measure_distance = [&]() -> std::optional<double> {
        double best = std::numeric_limits<double>::max();
        const TrackPoint* best_p = nullptr;
        for (const auto& p : data.own) {
            ImVec2 sp = plan_view_.world_to_screen(p.x, p.y, origin, avail);
            double dist = std::hypot(sp.x - mouse.x, sp.y - mouse.y);
            if (dist < best) {
                best = dist;
                best_p = &p;
            }
        }
        if (best_p && best <= 30.0) return best_p->d;
        return std::nullopt;
    };
    std::optional<double> hovered_measure_distance;
    if (hovered && mode_ == Mode::Measure && !data.own.empty()) {
        hovered_measure_distance = nearest_measure_distance();
        if (hovered_measure_distance) set_crosshair_cursor();
    }
    if (hovered && io.MouseWheel != 0.0f) {
        if (io.KeyShift) {
            plan_view_.rotation += io.MouseWheel * 5.0 * 3.14159265358979323846 / 180.0;
        } else {
            double factor = io.MouseWheel > 0 ? 1.15 : 1.0 / 1.15;
            plan_view_.scale = std::clamp(plan_view_.scale * factor, 0.001, 10000.0);
        }
    }

    bool rotate_plan = hovered && (ImGui::IsMouseDown(ImGuiMouseButton_Right) ||
                                   (io.KeyCtrl && ImGui::IsMouseDown(ImGuiMouseButton_Left)));
    if (hovered && mode_ == Mode::Pan && ImGui::IsMouseDown(ImGuiMouseButton_Left) && !rotate_plan) {
        if (!plan_view_.dragging) {
            plan_view_.dragging = true;
            plan_view_.last_mouse = mouse;
        } else {
            ImVec2 delta(mouse.x - plan_view_.last_mouse.x, mouse.y - plan_view_.last_mouse.y);
            plan_view_.pan_by_screen_delta(delta);
            plan_view_.last_mouse = mouse;
        }
        set_move_cursor();
    } else {
        plan_view_.dragging = false;
    }

    if (rotate_plan) {
        if (!plan_view_.rotating) {
            plan_view_.rotating = true;
            plan_view_.last_mouse = mouse;
        } else {
            ImVec2 center(origin.x + avail.x * 0.5f, origin.y + avail.y * 0.5f);
            double a0 = std::atan2(plan_view_.last_mouse.y - center.y, plan_view_.last_mouse.x - center.x);
            double a1 = std::atan2(mouse.y - center.y, mouse.x - center.x);
            plan_view_.rotation += a1 - a0;
            plan_view_.last_mouse = mouse;
        }
    } else {
        plan_view_.rotating = false;
    }

    if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        ImVec2 world = plan_view_.screen_to_world(mouse, origin, avail);
        if (pick_slot_ == 1) {
            if (auto uv = background_uv_from_world(world)) align_pick1_ = *uv;
            pick_slot_ = 0;
            show_align_popup_ = true;
        } else if (pick_slot_ == 2) {
            if (auto uv = background_uv_from_world(world)) align_pick2_ = *uv;
            pick_slot_ = 0;
            show_align_popup_ = true;
        } else if (mode_ == Mode::Measure) {
            if (auto clicked_distance = nearest_measure_distance()) {
                update_measure(*clicked_distance);
                request_plot_focus(*clicked_distance, true, true);
            }
        } else if (!data.own.empty()) {
            plan_view_.fit(data.xmin, data.ymin, data.xmax, data.ymax, avail);
        }
    }

    if (grid_mode_ == GridMode::Fixed) {
        for (float x = origin.x; x <= origin.x + avail.x; x += 80.0f) draw->AddLine(ImVec2(x, origin.y), ImVec2(x, origin.y + avail.y), IM_COL32(48, 52, 58, 255));
        for (float y = origin.y; y <= origin.y + avail.y; y += 80.0f) draw->AddLine(ImVec2(origin.x, y), ImVec2(origin.x + avail.x, y), IM_COL32(48, 52, 58, 255));
    } else if (grid_mode_ == GridMode::Movable) {
        ImVec2 screen_corners[] = {
            origin,
            ImVec2(origin.x + avail.x, origin.y),
            ImVec2(origin.x, origin.y + avail.y),
            ImVec2(origin.x + avail.x, origin.y + avail.y)
        };
        ImVec2 first = plan_view_.screen_to_world(screen_corners[0], origin, avail);
        double xmin = first.x, xmax = first.x;
        double ymin = first.y, ymax = first.y;
        for (int i = 1; i < IM_ARRAYSIZE(screen_corners); ++i) {
            ImVec2 c = plan_view_.screen_to_world(screen_corners[i], origin, avail);
            xmin = std::min(xmin, static_cast<double>(c.x));
            xmax = std::max(xmax, static_cast<double>(c.x));
            ymin = std::min(ymin, static_cast<double>(c.y));
            ymax = std::max(ymax, static_cast<double>(c.y));
        }
        double step = grid_step(std::max(xmax - xmin, ymax - ymin));
        xmin = std::floor(xmin / step) * step - step;
        xmax = std::ceil(xmax / step) * step + step;
        ymin = std::floor(ymin / step) * step - step;
        ymax = std::ceil(ymax / step) * step + step;
        for (double x = xmin; x <= xmax; x += step) {
            ImVec2 a = plan_view_.world_to_screen(x, ymin, origin, avail);
            ImVec2 b = plan_view_.world_to_screen(x, ymax, origin, avail);
            draw->AddLine(a, b, IM_COL32(48, 52, 58, 255));
        }
        for (double y = ymin; y <= ymax; y += step) {
            ImVec2 a = plan_view_.world_to_screen(xmin, y, origin, avail);
            ImVec2 b = plan_view_.world_to_screen(xmax, y, origin, avail);
            draw->AddLine(a, b, IM_COL32(48, 52, 58, 255));
        }
    }

    draw_background(draw, plan_view_, origin, avail);

    auto draw_section = [&](const Section& sec, ImU32 color, float width) {
        std::vector<TrackPoint> pts;
        for (const auto& p : data.own) if (p.d >= sec.start && p.d <= sec.end) pts.push_back(p);
        draw_polyline(draw, pts, plan_view_, origin, avail, color, width);
    };
    if (show_curve_values_) {
        for (const auto& s : data.curve_sections) draw_section(s, IM_COL32(130, 130, 130, 220), 10.0f);
        for (const auto& s : data.transition_sections) draw_section(s, IM_COL32(84, 84, 84, 220), 8.0f);
    }
    draw_polyline(draw, data.own, plan_view_, origin, avail, IM_COL32(245, 245, 245, 255), 2.0f);
    for (const auto& t : data.other) draw_polyline(draw, t.points, plan_view_, origin, avail, color_u32(t.color), 1.5f);

    if (show_stations_) {
        for (const auto& st : data.stations) {
            ImVec2 p = plan_view_.world_to_screen(st.x, st.y, origin, avail);
            draw->AddCircleFilled(p, station_marker_size_, IM_COL32(255, 255, 255, 255));
            if (show_station_names_) draw->AddText(ImVec2(p.x + 8, p.y - 16), IM_COL32(255, 255, 255, 255), st.station.name.c_str());
            if (show_station_mileage_) draw->AddText(ImVec2(p.x + 8, p.y + 4), IM_COL32(255, 216, 77, 255), (format_double(st.station.mileage, 0) + "m").c_str());
        }
    }

    if (show_speedlimits_) {
        for (const auto& sp : data.speedlimits) {
            ImVec2 p = plan_view_.world_to_screen(sp.x, sp.y, origin, avail);
            double wx = sp.x - std::sin(sp.theta);
            double wy = sp.y + std::cos(sp.theta);
            ImVec2 q = plan_view_.world_to_screen(wx, wy, origin, avail);
            ImVec2 d(q.x - p.x, q.y - p.y);
            float len = std::max(1.0f, std::sqrt(d.x * d.x + d.y * d.y));
            d.x = d.x / len * 8.0f;
            d.y = d.y / len * 8.0f;
            draw->AddLine(ImVec2(p.x - d.x, p.y - d.y), ImVec2(p.x + d.x, p.y + d.y), IM_COL32(136, 204, 255, 255), 1.0f);
            std::string label = sp.has_speed ? format_double(sp.speed, 0) : "x";
            draw->AddText(ImVec2(p.x + 10, p.y - 15), IM_COL32(136, 204, 255, 255), label.c_str());
        }
    }

    if (show_curve_values_) {
        for (const auto& sec : data.curve_sections) {
            double mid = (sec.start + sec.end) * 0.5;
            auto it = std::lower_bound(data.own.begin(), data.own.end(), mid, [](const TrackPoint& p, double d) { return p.d < d; });
            if (it != data.own.end()) {
                ImVec2 p = plan_view_.world_to_screen(it->x, it->y, origin, avail);
                draw->AddText(ImVec2(p.x + 8, p.y - 16), IM_COL32(136, 255, 136, 255), format_double(sec.value, 0).c_str());
            }
        }
    }

    if (hovered_measure_distance) {
        update_measure(*hovered_measure_distance);
    }

    if (mode_ == Mode::Measure && measure_distance_ && !data.own.empty()) {
        auto it = std::lower_bound(data.own.begin(), data.own.end(), *measure_distance_, [](const TrackPoint& p, double d) { return p.d < d; });
        if (it == data.own.end()) {
            --it;
        } else if (it != data.own.begin() && std::abs((it - 1)->d - *measure_distance_) < std::abs(it->d - *measure_distance_)) {
            --it;
        }
        ImVec2 p = plan_view_.world_to_screen(it->x, it->y, origin, avail);
        draw->AddLine(ImVec2(p.x - 12, p.y - 12), ImVec2(p.x + 12, p.y + 12), IM_COL32(255, 51, 51, 255), 2.0f);
        draw->AddLine(ImVec2(p.x - 12, p.y + 12), ImVec2(p.x + 12, p.y - 12), IM_COL32(255, 51, 51, 255), 2.0f);
    }

    draw->AddText(ImVec2(origin.x + 8, origin.y + 8), IM_COL32(255, 255, 255, 255), tr("canvas.plan").c_str());
    draw_scalebar(draw, plan_view_, origin, avail);
    draw->PopClipRect();
    ImGui::EndChild();
}

static void plot_line_vec(const char* label, const std::vector<double>& x, const std::vector<double>& y, ImVec4 color, float weight = 1.5f) {
    if (x.size() < 2 || y.size() < 2) return;
    ImPlotSpec spec;
    spec.LineColor = color;
    spec.LineWeight = weight;
    ImPlot::PlotLine(label, x.data(), y.data(), static_cast<int>(std::min(x.size(), y.size())), spec);
}

static void draw_plot_overlay_labels(const std::string& title, const std::string& unit) {
    ImDrawList* draw = ImGui::GetWindowDrawList();
    ImVec2 pos = ImPlot::GetPlotPos();
    ImVec2 size = ImPlot::GetPlotSize();
    ImU32 color = ImGui::GetColorU32(ImGuiCol_Text);
    draw->AddText(ImVec2(pos.x + 8.0f, pos.y + 6.0f), color, title.c_str());
    ImVec2 unit_size = ImGui::CalcTextSize(unit.c_str());
    draw->AddText(ImVec2(pos.x - unit_size.x - 8.0f, pos.y + size.y + 6.0f), color, unit.c_str());
}

static void mask_plot_axis_tick_edges(ImVec2 frame_min, ImVec2 frame_max, ImVec2 plot_pos, ImVec2 plot_size) {
    ImVec2 plot_max(plot_pos.x + plot_size.x, plot_pos.y + plot_size.y);
    if (plot_size.x <= 0.0f || plot_size.y <= 0.0f) return;

    ImU32 bg = ImGui::GetColorU32(ImGuiCol_WindowBg);
    ImDrawList* draw = ImGui::GetWindowDrawList();
    float bleed = ImGui::GetFontSize() * 1.6f;
    ImVec2 outer_min(frame_min.x - bleed, frame_min.y - bleed);
    ImVec2 outer_max(frame_max.x + bleed, frame_max.y + bleed);
    auto fill = [&](ImVec2 min, ImVec2 max) {
        if (max.x > min.x && max.y > min.y) draw->AddRectFilled(min, max, bg);
    };

    fill(outer_min, plot_pos);
    fill(ImVec2(plot_max.x, outer_min.y), ImVec2(outer_max.x, plot_pos.y));
    fill(ImVec2(outer_min.x, plot_max.y), ImVec2(plot_pos.x, outer_max.y));
    fill(plot_max, outer_max);
}

static void draw_radius_side_markers() {
    ImDrawList* draw = ImGui::GetWindowDrawList();
    ImVec2 pos = ImPlot::GetPlotPos();
    ImVec2 size = ImPlot::GetPlotSize();
    ImVec2 clip_max(pos.x + size.x, pos.y + size.y);
    ImVec2 zero = ImPlot::PlotToPixels(ImPlot::GetPlotLimits().X.Min, 0.0);
    if (!std::isfinite(zero.y)) return;

    ImU32 color = ImGui::GetColorU32(ImGuiCol_Text);
    ImVec2 r_size = ImGui::CalcTextSize("R");
    ImVec2 l_size = ImGui::CalcTextSize("L");
    float x = pos.x - std::max(r_size.x, l_size.x) - 4.0f;
    float gap = 3.0f;
    float r_y = std::clamp(zero.y - r_size.y - gap, pos.y + 2.0f, clip_max.y - r_size.y - 2.0f);
    float l_y = std::clamp(zero.y + gap, pos.y + 2.0f, clip_max.y - l_size.y - 2.0f);

    draw->AddText(ImVec2(x, r_y), color, "R");
    draw->AddText(ImVec2(x, l_y), color, "L");
}

static std::optional<std::pair<double, double>> plot_x_wheel_zoom_limits(const ImPlotRect& limits) {
    ImGuiIO& io = ImGui::GetIO();
    if (io.MouseWheel == 0.0f || !ImPlot::IsPlotHovered()) return std::nullopt;
    double min_x = limits.X.Min;
    double max_x = limits.X.Max;
    double span = max_x - min_x;
    if (!std::isfinite(span) || span <= 1e-9) return std::nullopt;

    ImVec2 pos = ImPlot::GetPlotPos();
    ImVec2 size = ImPlot::GetPlotSize();
    if (size.x <= 1.0f) return std::nullopt;
    float tx = std::clamp((io.MousePos.x - pos.x) / size.x, 0.0f, 1.0f);

    float zoom_rate = 0.1f;
    if (io.MouseWheel > 0.0f) {
        zoom_rate = (-zoom_rate) / (1.0f + (2.0f * zoom_rate));
    }
    double new_min = min_x - span * tx * zoom_rate;
    double new_max = max_x + span * (1.0f - tx) * zoom_rate;
    if (!std::isfinite(new_min) || !std::isfinite(new_max) || new_max <= new_min) return std::nullopt;
    return std::make_pair(new_min, new_max);
}

static void draw_bottom_locked_plot_labels(const std::vector<LabelPoint>& labels) {
    ImDrawList* draw = ImPlot::GetPlotDrawList();
    ImVec2 pos = ImPlot::GetPlotPos();
    ImVec2 size = ImPlot::GetPlotSize();
    ImVec2 clip_max(pos.x + size.x, pos.y + size.y);
    ImU32 color = ImGui::GetColorU32(ImGuiCol_Text);
    draw->PushClipRect(pos, clip_max, true);
    for (const auto& label : labels) {
        ImVec2 p = ImPlot::PlotToPixels(label.x, 0.0);
        if (p.x < pos.x || p.x > clip_max.x) continue;
        ImVec2 text_size = ImGui::CalcTextSize(label.text.c_str());
        draw->AddText(ImVec2(p.x + 6.0f - text_size.x, clip_max.y - text_size.y - 6.0f), color, label.text.c_str());
    }
    draw->PopClipRect();
}

enum class FixedPlotY {
    Top,
    Bottom
};

static std::string station_mileage_text(const Station& station) {
    return format_double(station.mileage, 0) + "m";
}

static void draw_fixed_y_plot_text(double x, const std::string& text, ImU32 color, FixedPlotY fixed_y) {
    if (text.empty()) return;
    ImDrawList* draw = ImPlot::GetPlotDrawList();
    ImVec2 pos = ImPlot::GetPlotPos();
    ImVec2 size = ImPlot::GetPlotSize();
    ImVec2 clip_max(pos.x + size.x, pos.y + size.y);
    ImVec2 p = ImPlot::PlotToPixels(x, 0.0);
    if (!std::isfinite(p.x) || p.x < pos.x || p.x > clip_max.x) return;

    ImVec2 text_size = ImGui::CalcTextSize(text.c_str());
    float y = fixed_y == FixedPlotY::Top ? pos.y + 8.0f : clip_max.y - text_size.y - 8.0f;
    draw->PushClipRect(pos, clip_max, true);
    draw->AddText(ImVec2(p.x + 8.0f, y), color, text.c_str());
    draw->PopClipRect();
}

static void draw_plot_point_right_text(double x, double y, const std::string& text, ImU32 color) {
    if (text.empty()) return;
    ImDrawList* draw = ImPlot::GetPlotDrawList();
    ImVec2 pos = ImPlot::GetPlotPos();
    ImVec2 size = ImPlot::GetPlotSize();
    ImVec2 clip_max(pos.x + size.x, pos.y + size.y);
    ImVec2 p = ImPlot::PlotToPixels(x, y);
    if (!std::isfinite(p.x) || !std::isfinite(p.y) || p.x < pos.x || p.x > clip_max.x) return;

    draw->PushClipRect(pos, clip_max, true);
    draw->AddText(ImVec2(p.x + 8.0f, p.y - 26.0f), color, text.c_str());
    draw->PopClipRect();
}

enum class ProfileMarkerDirection {
    Up,
    Down
};

static void draw_profile_vertical_marker(double x, double track_y, ProfileMarkerDirection direction,
                                         ImU32 line_color, float line_weight, bool draw_station_marker,
                                         float station_marker_size = kDefaultStationMarkerSize) {
    ImDrawList* draw = ImPlot::GetPlotDrawList();
    ImVec2 pos = ImPlot::GetPlotPos();
    ImVec2 size = ImPlot::GetPlotSize();
    ImVec2 clip_min = pos;
    ImVec2 clip_max(pos.x + size.x, pos.y + size.y);
    ImVec2 p = ImPlot::PlotToPixels(x, track_y);
    if (!std::isfinite(p.x) || !std::isfinite(p.y) || p.x < clip_min.x || p.x > clip_max.x) return;

    float unclipped_a = direction == ProfileMarkerDirection::Up ? clip_min.y : p.y;
    float unclipped_b = direction == ProfileMarkerDirection::Up ? p.y : clip_max.y;
    float line_min = std::max(std::min(unclipped_a, unclipped_b), clip_min.y);
    float line_max = std::min(std::max(unclipped_a, unclipped_b), clip_max.y);

    draw->PushClipRect(clip_min, clip_max, true);
    if (line_max > line_min) {
        draw->AddLine(ImVec2(p.x, line_min), ImVec2(p.x, line_max), line_color, line_weight);
    }
    if (draw_station_marker) {
        float radius = clamp_station_marker_size(station_marker_size);
        float outline_weight = std::max(1.0f, radius * 0.375f);
        draw->AddCircleFilled(p, radius, IM_COL32(0, 0, 0, 255));
        draw->AddCircle(p, radius, IM_COL32(255, 255, 255, 255), 0, outline_weight);
    }
    draw->PopClipRect();
}

class ScopedImPlotFitButton {
public:
    explicit ScopedImPlotFitButton(bool disabled) : active_(disabled) {
        if (!active_) return;
        ImPlotInputMap& input = ImPlot::GetInputMap();
        old_fit_ = input.Fit;
        // Extra mouse slot 3 is not enabled by ImPlot's plot button flags.
        input.Fit = 3;
    }

    ~ScopedImPlotFitButton() {
        if (active_) ImPlot::GetInputMap().Fit = old_fit_;
    }

private:
    bool active_ = false;
    ImGuiMouseButton old_fit_ = ImGuiMouseButton_Left;
};

class ScopedImPlotWheelZoomDisabled {
public:
    explicit ScopedImPlotWheelZoomDisabled(bool active) : active_(active) {
        if (!active_) return;
        ImPlotInputMap& input = ImPlot::GetInputMap();
        old_zoom_rate_ = input.ZoomRate;
        input.ZoomRate = 0.0f;
    }

    ~ScopedImPlotWheelZoomDisabled() {
        if (active_) ImPlot::GetInputMap().ZoomRate = old_zoom_rate_;
    }

private:
    bool active_ = false;
    float old_zoom_rate_ = 0.0f;
};

static bool point_in_rect(ImVec2 p, ImVec2 pos, ImVec2 size) {
    return p.x >= pos.x && p.x <= pos.x + size.x && p.y >= pos.y && p.y <= pos.y + size.y;
}

static double preserved_plot_span(double current_span, double fallback_min, double fallback_max) {
    if (std::isfinite(current_span) && current_span > 1e-6) return current_span;
    double fallback = fallback_max - fallback_min;
    if (std::isfinite(fallback) && fallback > 1e-6) return fallback;
    return 1000.0;
}

void App::render_profile_plot(const ProfileData& data, ImVec2 size) {
    if (!show_profile_graph_) return;
    ScopedImPlotFitButton disable_fit(mode_ == Mode::Measure);
    ImGuiIO& io = ImGui::GetIO();
    bool mouse_in_profile_plot = profile_plot_rect_valid_ && point_in_rect(io.MousePos, profile_plot_pos_, profile_plot_size_);
    ScopedImPlotWheelZoomDisabled disable_default_wheel_zoom(mouse_in_profile_plot);
    ImPlot::PushStyleVar(ImPlotStyleVar_PlotPadding, ImVec2(4.0f, 4.0f));
    ImPlot::PushStyleVar(ImPlotStyleVar_LabelPadding, ImVec2(2.0f, 2.0f));
    bool consumed_profile_x_zoom = false;
    if (ImPlot::BeginPlot("##ProfilePlot", size, ImPlotFlags_NoTitle | ImPlotFlags_NoLegend)) {
        ImVec2 frame_min = ImGui::GetItemRectMin();
        ImVec2 frame_max = ImGui::GetItemRectMax();
        ImPlot::SetupAxes(nullptr, nullptr, ImPlotAxisFlags_NoLabel, ImPlotAxisFlags_NoLabel);
        ImPlotCond reset_cond = reset_profile_axes_next_ ? ImPlotCond_Always : ImPlotCond_Once;
        if (focus_profile_next_) {
            double span = preserved_plot_span(profile_x_span_, dmin_, dmax_);
            ImPlot::SetupAxisLimits(ImAxis_X1, focus_profile_distance_ - span * 0.5, focus_profile_distance_ + span * 0.5, ImPlotCond_Always);
            profile_x_zoom_pending_ = false;
        } else if (!reset_profile_axes_next_ && profile_x_zoom_pending_) {
            ImPlot::SetupAxisLimits(ImAxis_X1, profile_x_zoom_min_, profile_x_zoom_max_, ImPlotCond_Always);
            consumed_profile_x_zoom = true;
        } else {
            ImPlot::SetupAxisLimits(ImAxis_X1, dmin_, dmax_, reset_cond);
            if (reset_profile_axes_next_) profile_x_zoom_pending_ = false;
        }
        ImPlot::SetupAxisLimits(ImAxis_Y1, data.ymin, data.ymax, reset_cond);
        plot_line_vec("Own", data.own_x, data.own_y, ImVec4(1, 1, 1, 1), 2.0f);
        for (const auto& t : data.other) plot_line_vec(t.key.c_str(), t.x, t.y, t.color, 1.2f);
        if (show_gradient_pos_) {
            for (const auto& p : data.gradient_points) {
                draw_profile_vertical_marker(p.x, p.y, ProfileMarkerDirection::Down,
                                             IM_COL32(255, 255, 255, 140), 1.0f, false);
            }
            if (show_gradient_values_) {
                draw_bottom_locked_plot_labels(data.gradient_labels);
            }
        }
        if (show_stations_) {
            for (const auto& s : data.stations) {
                double x = s.distance;
                double y = s.z - model_.height_origin;
                draw_profile_vertical_marker(x, y, ProfileMarkerDirection::Up,
                                             IM_COL32(255, 255, 255, 191), 1.0f, true, station_marker_size_);
                if (show_station_names_) draw_plot_point_right_text(x, y, s.name, IM_COL32(255, 255, 255, 255));
                if (show_station_mileage_) draw_fixed_y_plot_text(x, station_mileage_text(s), IM_COL32(255, 216, 77, 255), FixedPlotY::Top);
            }
        }
        if (mode_ == Mode::Measure && ImPlot::IsPlotHovered()) {
            set_crosshair_cursor();
            ImPlotPoint p = ImPlot::GetPlotMousePos();
            if (p.x >= dmin_ && p.x <= dmax_) update_measure(p.x);
        }
        handle_measure_plot_double_click(false, true);
        if (mode_ == Mode::Measure && measure_distance_) {
            double x = *measure_distance_;
            ImPlot::PlotInfLines("##measure_profile", &x, 1, {ImPlotProp_LineColor, ImVec4(1, 0.2f, 0.2f, 1), ImPlotProp_LineWeight, 2.0f, ImPlotProp_Flags, ImPlotItemFlags_NoLegend});
        }
        ImPlotRect limits = ImPlot::GetPlotLimits();
        profile_x_span_ = std::abs(limits.X.Size());
        profile_plot_pos_ = ImPlot::GetPlotPos();
        profile_plot_size_ = ImPlot::GetPlotSize();
        profile_plot_rect_valid_ = profile_plot_size_.x > 0.0f && profile_plot_size_.y > 0.0f;
        if (auto zoom_limits = plot_x_wheel_zoom_limits(limits)) {
            profile_x_zoom_min_ = zoom_limits->first;
            profile_x_zoom_max_ = zoom_limits->second;
            profile_x_zoom_pending_ = true;
        } else if (consumed_profile_x_zoom) {
            profile_x_zoom_pending_ = false;
        }
        ImVec2 plot_pos = profile_plot_pos_;
        ImVec2 plot_size = profile_plot_size_;
        mask_plot_axis_tick_edges(frame_min, frame_max, plot_pos, plot_size);
        draw_plot_overlay_labels(tr("plot.profile"), tr("unit.m"));
        focus_profile_next_ = false;
        reset_profile_axes_next_ = false;
        ImPlot::EndPlot();
    }
    ImPlot::PopStyleVar(2);
}

void App::render_radius_plot(const ProfileData& data, ImVec2 size) {
    if (!show_radius_graph_) return;
    ScopedImPlotFitButton disable_fit(mode_ == Mode::Measure);
    ImPlot::PushStyleVar(ImPlotStyleVar_PlotPadding, ImVec2(4.0f, 4.0f));
    ImPlot::PushStyleVar(ImPlotStyleVar_LabelPadding, ImVec2(2.0f, 2.0f));
    if (ImPlot::BeginPlot("##RadiusPlot", size, ImPlotFlags_NoTitle | ImPlotFlags_NoLegend)) {
        ImVec2 frame_min = ImGui::GetItemRectMin();
        ImVec2 frame_max = ImGui::GetItemRectMax();
        ImPlot::SetupAxis(ImAxis_X1, nullptr, ImPlotAxisFlags_NoLabel);
        ImPlot::SetupAxis(ImAxis_Y1, nullptr, ImPlotAxisFlags_NoLabel | ImPlotAxisFlags_NoTickMarks | ImPlotAxisFlags_NoTickLabels | ImPlotAxisFlags_NoHighlight | ImPlotAxisFlags_Lock);
        ImPlotCond reset_cond = reset_radius_axes_next_ ? ImPlotCond_Always : ImPlotCond_Once;
        if (focus_radius_next_) {
            double span = preserved_plot_span(radius_x_span_, dmin_, dmax_);
            ImPlot::SetupAxisLimits(ImAxis_X1, focus_radius_distance_ - span * 0.5, focus_radius_distance_ + span * 0.5, ImPlotCond_Always);
        } else {
            ImPlot::SetupAxisLimits(ImAxis_X1, dmin_, dmax_, reset_cond);
        }
        ImPlot::SetupAxisLimits(ImAxis_Y1, -2.2, 2.2, ImPlotCond_Always);
        plot_line_vec("RadiusSign", data.curve_x, data.curve_y, ImVec4(1, 1, 1, 1), 2.0f);
        for (const auto& label : data.radius_labels) ImPlot::PlotText(label.text.c_str(), label.x, label.y, ImVec2(-6, 0), {ImPlotProp_Flags, ImPlotTextFlags_Vertical});
        if (show_stations_) {
            for (const auto& s : data.stations) {
                double x = s.distance;
                ImPlot::PlotInfLines(("##rst" + s.key).c_str(), &x, 1, {ImPlotProp_LineColor, ImVec4(1, 1, 1, 0.55f), ImPlotProp_Flags, ImPlotItemFlags_NoLegend});
                if (show_station_names_) draw_fixed_y_plot_text(x, s.name, IM_COL32(255, 255, 255, 255), FixedPlotY::Top);
                if (show_station_mileage_) draw_fixed_y_plot_text(x, station_mileage_text(s), IM_COL32(255, 216, 77, 255), FixedPlotY::Bottom);
            }
        }
        if (mode_ == Mode::Measure && ImPlot::IsPlotHovered()) {
            set_crosshair_cursor();
            ImPlotPoint p = ImPlot::GetPlotMousePos();
            if (p.x >= dmin_ && p.x <= dmax_) update_measure(p.x);
        }
        handle_measure_plot_double_click(true, false);
        if (mode_ == Mode::Measure && measure_distance_) {
            double x = *measure_distance_;
            ImPlot::PlotInfLines("##measure_radius", &x, 1, {ImPlotProp_LineColor, ImVec4(1, 0.2f, 0.2f, 1), ImPlotProp_LineWeight, 2.0f, ImPlotProp_Flags, ImPlotItemFlags_NoLegend});
        }
        ImPlotRect limits = ImPlot::GetPlotLimits();
        radius_x_span_ = std::abs(limits.X.Size());
        ImVec2 plot_pos = ImPlot::GetPlotPos();
        ImVec2 plot_size = ImPlot::GetPlotSize();
        mask_plot_axis_tick_edges(frame_min, frame_max, plot_pos, plot_size);
        draw_radius_side_markers();
        draw_plot_overlay_labels(tr("plot.radius"), tr("unit.m"));
        focus_radius_next_ = false;
        reset_radius_axes_next_ = false;
        ImPlot::EndPlot();
    }
    ImPlot::PopStyleVar(2);
}

void App::render_plots() {
    std::string title = tr("frame.plots") + "###Plots";
    ImGui::Begin(title.c_str());
    render_mode_grid_controls();
    if (pick_slot_ != 0) ImGui::TextUnformatted(tr("hint.pick_bg_station").c_str());
    else if (mode_ == Mode::Measure && !measure_text_.empty()) ImGui::TextUnformatted(measure_text_.c_str());
    else ImGui::TextDisabled("%s", has_model_ ? "" : tr("status.no_map").c_str());

    ImVec2 avail = ImGui::GetContentRegionAvail();
    float splitter_h = 6.0f;
    float plan_h = avail.y;
    bool any_graph = show_profile_graph_ || show_radius_graph_;
    if (any_graph) plan_h = std::max(80.0f, avail.y * static_cast<float>(plan_height_) - splitter_h);
    render_plan_canvas(ImVec2(avail.x, plan_h));

    if (any_graph) {
        ImGui::InvisibleButton("##vsplit", ImVec2(avail.x, splitter_h));
        if (ImGui::IsItemActive()) {
            plan_height_ += ImGui::GetIO().MouseDelta.y / std::max(1.0f, avail.y);
            plan_height_ = std::clamp(plan_height_, 0.2, 0.86);
        }
        ProfileData profile = build_profile_data();
        ImVec2 graph_avail = ImGui::GetContentRegionAvail();
        if (show_profile_graph_ && show_radius_graph_) {
            float left_w = graph_avail.x * static_cast<float>(graph_split_);
            render_profile_plot(profile, ImVec2(left_w - 3.0f, graph_avail.y));
            ImGui::SameLine();
            ImGui::InvisibleButton("##hsplit", ImVec2(6.0f, graph_avail.y));
            if (ImGui::IsItemActive()) {
                graph_split_ += ImGui::GetIO().MouseDelta.x / std::max(1.0f, graph_avail.x);
                graph_split_ = std::clamp(graph_split_, 0.2, 0.8);
            }
            ImGui::SameLine();
            render_radius_plot(profile, ImVec2(-1.0f, graph_avail.y));
        } else if (show_profile_graph_) {
            render_profile_plot(profile, graph_avail);
        } else if (show_radius_graph_) {
            render_radius_plot(profile, graph_avail);
        }
    }
    ImGui::End();
}

void App::render_othertracks_window() {
    if (!show_othertracks_window_) return;
    if (dock_right_id_) ImGui::SetNextWindowDockID(dock_right_id_, ImGuiCond_FirstUseEver);
    std::string title = tr("frame.othertracks") + "###OtherTracks";
    if (ImGui::Begin(title.c_str(), &show_othertracks_window_)) {
        if (!has_model_) {
            ImGui::TextDisabled("-");
        } else if (ImGui::BeginTable("othertracks", 5, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY)) {
            ImGui::TableSetupColumn("Show");
            ImGui::TableSetupColumn("Key");
            ImGui::TableSetupColumn("From");
            ImGui::TableSetupColumn("To");
            ImGui::TableSetupColumn("Color");
            ImGui::TableHeadersRow();
            for (auto& t : model_.other_tracks) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::PushID(t.key.c_str());
                ImGui::Checkbox("##show", &t.visible);
                ImGui::TableSetColumnIndex(1);
                ImGui::TextUnformatted(t.key.empty() ? "\\" : t.key.c_str());
                ImGui::TableSetColumnIndex(2);
                ImGui::SetNextItemWidth(-1);
                ImGui::InputDouble("##min", &t.range_min, 0, 0, "%.1f");
                ImGui::TableSetColumnIndex(3);
                ImGui::SetNextItemWidth(-1);
                ImGui::InputDouble("##max", &t.range_max, 0, 0, "%.1f");
                ImGui::TableSetColumnIndex(4);
                ImGui::ColorEdit3("##color", &t.color.x, ImGuiColorEditFlags_NoInputs);
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
    }
    ImGui::End();
}

void App::render_station_list_window() {
    if (!show_station_list_window_) return;
    if (dock_right_id_) ImGui::SetNextWindowDockID(dock_right_id_, ImGuiCond_FirstUseEver);
    std::string title = tr("frame.station_list") + "###StationList";
    if (!ImGui::Begin(title.c_str(), &show_station_list_window_)) {
        ImGui::End();
        return;
    }
    if (!has_model_) {
        ImGui::TextDisabled("-");
        ImGui::End();
        return;
    }
    ensure_table_cache();
    if (ImGui::BeginTable("station_list", IM_ARRAYSIZE(kStationListColumns), ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollX | ImGuiTableFlags_ScrollY)) {
        for (int i = 0; i < IM_ARRAYSIZE(kStationListColumns); ++i) {
            ImGui::TableSetupColumn(kStationListColumns[i].header, ImGuiTableColumnFlags_WidthFixed, kStationListColumns[i].width);
        }
        ImGui::TableHeadersRow();
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(table_cache_.station_rows.size()));
        while (clipper.Step()) {
            for (int row_index = clipper.DisplayStart; row_index < clipper.DisplayEnd; ++row_index) {
                const CachedTableRow& row = table_cache_.station_rows[static_cast<size_t>(row_index)];
                ImGui::TableNextRow();
                for (int i = 0; i < IM_ARRAYSIZE(kStationListColumns); ++i) {
                    ImGui::TableSetColumnIndex(i);
                    const std::string& value = row.cells[static_cast<size_t>(i)];
                    if (!value.empty()) ImGui::TextUnformatted(value.c_str());
                }
            }
        }
        ImGui::EndTable();
    }
    ImGui::End();
}

void App::render_structures_window() {
    if (!show_structures_window_) return;
    if (dock_right_id_) ImGui::SetNextWindowDockID(dock_right_id_, ImGuiCond_FirstUseEver);
    std::string title = tr("frame.structures") + "###Structures";
    if (!ImGui::Begin(title.c_str(), &show_structures_window_)) {
        ImGui::End();
        return;
    }
    ensure_table_cache();
    if (ImGui::BeginTable("structures", IM_ARRAYSIZE(kStructureColumns), ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollX | ImGuiTableFlags_ScrollY)) {
        for (int i = 0; i < IM_ARRAYSIZE(kStructureColumns); ++i) {
            ImGui::TableSetupColumn(kStructureColumns[i],
                                    i == kStructureFilePathColumn ? ImGuiTableColumnFlags_WidthFixed : 0,
                                    i == kStructureFilePathColumn ? table_cache_.structure_file_path_width : 0.0f);
        }
        ImGui::TableHeadersRow();
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(table_cache_.structure_rows.size()));
        while (clipper.Step()) {
            for (int row_index = clipper.DisplayStart; row_index < clipper.DisplayEnd; ++row_index) {
                const CachedTableRow& row = table_cache_.structure_rows[static_cast<size_t>(row_index)];
                ImGui::TableNextRow();
                ImGui::PushID(row_index);
                for (int i = 0; i < IM_ARRAYSIZE(kStructureColumns); ++i) {
                    ImGui::TableSetColumnIndex(i);
                    const std::string& value = row.cells[static_cast<size_t>(i)];
                    if (value.empty()) continue;
                    if (i == kStructureFilePathColumn) {
                        render_file_path_cell_with_context(value, row.open_path, tr("menu.open_in_explorer"));
                    } else {
                        ImGui::TextUnformatted(value.c_str());
                    }
                }
                ImGui::PopID();
            }
        }
        ImGui::EndTable();
    }
    ImGui::End();
}

void App::render_repeaters_window() {
    if (!show_repeaters_window_) return;
    if (dock_right_id_) ImGui::SetNextWindowDockID(dock_right_id_, ImGuiCond_FirstUseEver);
    std::string title = tr("frame.repeaters") + "###Repeaters";
    if (!ImGui::Begin(title.c_str(), &show_repeaters_window_)) {
        ImGui::End();
        return;
    }
    ensure_table_cache();
    if (ImGui::BeginTable("repeaters", IM_ARRAYSIZE(kRepeaterColumns), ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollX | ImGuiTableFlags_ScrollY)) {
        for (int i = 0; i < IM_ARRAYSIZE(kRepeaterColumns); ++i) {
            float width = kRepeaterColumns[i].width;
            if (i == kRepeaterDistanceColumn) width = table_cache_.repeater_distance_width;
            if (i == kRepeaterIntervalColumn) width = table_cache_.repeater_interval_width;
            if (i == kRepeaterFilePathColumn) width = table_cache_.repeater_file_path_width;
            ImGui::TableSetupColumn(kRepeaterColumns[i].header, width > 0.0f ? ImGuiTableColumnFlags_WidthFixed : 0, width);
        }
        ImGui::TableHeadersRow();
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(table_cache_.repeater_rows.size()));
        while (clipper.Step()) {
            for (int row_index = clipper.DisplayStart; row_index < clipper.DisplayEnd; ++row_index) {
                const CachedTableRow& row = table_cache_.repeater_rows[static_cast<size_t>(row_index)];
                ImGui::TableNextRow();
                ImGui::PushID(row_index);
                for (int i = 0; i < IM_ARRAYSIZE(kRepeaterColumns); ++i) {
                    ImGui::TableSetColumnIndex(i);
                    const std::string& value = row.cells[static_cast<size_t>(i)];
                    if (value.empty()) continue;
                    if (i == kRepeaterFilePathColumn) {
                        render_file_path_cell_with_context(value, row.open_path, tr("menu.open_in_explorer"));
                    } else {
                        ImGui::TextUnformatted(value.c_str());
                    }
                }
                ImGui::PopID();
            }
        }
        ImGui::EndTable();
    }
    ImGui::End();
}

void App::render_popups() {
    if (show_font_size_popup_) {
        ImGui::OpenPopup(tr("dialog.ui_settings").c_str());
        show_font_size_popup_ = false;
    }
    bool ui_settings_popup_open = true;
    if (ImGui::BeginPopupModal(tr("dialog.ui_settings").c_str(), &ui_settings_popup_open, ImGuiWindowFlags_AlwaysAutoResize)) {
        auto apply_pending_ui_settings = [&]() {
            apply_ui_settings(pending_font_size_, pending_ui_component_size_, pending_theme_color_, dpi_scale_, viewports_enabled_);
        };
        ImGui::SetNextItemWidth(260.0f);
        if (ImGui::SliderFloat(tr("label.font_size").c_str(), &pending_font_size_, kMinFontSize, kMaxFontSize, "%.0f px", ImGuiSliderFlags_AlwaysClamp)) {
            pending_font_size_ = clamp_font_size(pending_font_size_);
            apply_pending_ui_settings();
        }
        ImGui::SetNextItemWidth(260.0f);
        if (ImGui::SliderFloat(tr("label.ui_component_size").c_str(), &pending_ui_component_size_, kMinUiComponentSize, kMaxUiComponentSize, "%.0f%%", ImGuiSliderFlags_AlwaysClamp)) {
            pending_ui_component_size_ = clamp_ui_component_size(pending_ui_component_size_);
            apply_pending_ui_settings();
        }
        ImGui::SetNextItemWidth(260.0f);
        if (ImGui::SliderFloat(tr("label.station_marker_size").c_str(), &pending_station_marker_size_, kMinStationMarkerSize, kMaxStationMarkerSize, "%.0f px", ImGuiSliderFlags_AlwaysClamp)) {
            pending_station_marker_size_ = clamp_station_marker_size(pending_station_marker_size_);
            station_marker_size_ = pending_station_marker_size_;
        }
        ImGui::Separator();
        const ImGuiColorEditFlags color_flags = ImGuiColorEditFlags_NoAlpha
            | ImGuiColorEditFlags_DisplayRGB
            | ImGuiColorEditFlags_InputRGB
            | ImGuiColorEditFlags_Uint8
            | ImGuiColorEditFlags_PickerHueBar;
        std::string theme_hex = theme_color_to_string(pending_theme_color_);
        ImGui::TextUnformatted(tr("label.ui_theme_color").c_str());
        ImGui::SameLine();
        const float preview_size = ImGui::GetFrameHeight();
        if (ImGui::ColorButton("##theme_color_preview", pending_theme_color_, ImGuiColorEditFlags_NoAlpha, ImVec2(preview_size, preview_size))) {
            ImGui::OpenPopup("theme_color_popup");
        }
        ImGui::SameLine();
        ImGui::Text("#%s", theme_hex.c_str());
        const std::array<ImVec4, 12> palette = {
            ImVec4(0.26f, 0.59f, 0.98f, 1.0f),
            ImVec4(0.00f, 0.74f, 0.83f, 1.0f),
            ImVec4(0.26f, 0.75f, 0.48f, 1.0f),
            ImVec4(0.60f, 0.78f, 0.20f, 1.0f),
            ImVec4(0.95f, 0.67f, 0.13f, 1.0f),
            ImVec4(0.95f, 0.42f, 0.18f, 1.0f),
            ImVec4(0.90f, 0.27f, 0.33f, 1.0f),
            ImVec4(0.88f, 0.31f, 0.55f, 1.0f),
            ImVec4(0.70f, 0.38f, 0.94f, 1.0f),
            ImVec4(0.46f, 0.45f, 0.95f, 1.0f),
            ImVec4(0.40f, 0.58f, 0.71f, 1.0f),
            ImVec4(0.58f, 0.63f, 0.68f, 1.0f),
        };
        if (ImGui::BeginPopup("theme_color_popup")) {
            ImGui::SetNextItemWidth(260.0f);
            if (ImGui::ColorPicker3("##theme_color_picker", &pending_theme_color_.x, color_flags)) {
                pending_theme_color_ = clamp_theme_color(pending_theme_color_);
                apply_pending_ui_settings();
            }
            ImGui::Separator();
            const float swatch_size = ImGui::GetFrameHeight();
            for (size_t i = 0; i < palette.size(); ++i) {
                if (i > 0 && i % 6 != 0) ImGui::SameLine();
                std::string id = "##theme_palette_" + std::to_string(i);
                if (ImGui::ColorButton(id.c_str(), palette[i], ImGuiColorEditFlags_NoAlpha, ImVec2(swatch_size, swatch_size))) {
                    pending_theme_color_ = clamp_theme_color(palette[i]);
                    apply_pending_ui_settings();
                }
            }
            ImGui::EndPopup();
        }
        if (ImGui::Button(tr("button.ok").c_str())) {
            font_size_ = clamp_font_size(pending_font_size_);
            ui_component_size_ = clamp_ui_component_size(pending_ui_component_size_);
            station_marker_size_ = clamp_station_marker_size(pending_station_marker_size_);
            theme_color_ = clamp_theme_color(pending_theme_color_);
            station_marker_size_before_dialog_ = station_marker_size_;
            settings_.font_size = font_size_;
            settings_.ui_component_size = ui_component_size_;
            settings_.station_marker_size = station_marker_size_;
            settings_.theme_color = theme_color_;
            save_user_settings(settings_);
            apply_ui_settings(font_size_, ui_component_size_, theme_color_, dpi_scale_, viewports_enabled_);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button(tr("button.cancel").c_str())) {
            pending_font_size_ = font_size_before_dialog_;
            pending_ui_component_size_ = ui_component_size_before_dialog_;
            pending_station_marker_size_ = station_marker_size_before_dialog_;
            station_marker_size_ = station_marker_size_before_dialog_;
            pending_theme_color_ = theme_color_before_dialog_;
            apply_ui_settings(font_size_, ui_component_size_, theme_color_, dpi_scale_, viewports_enabled_);
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    if (!ui_settings_popup_open) {
        pending_font_size_ = font_size_;
        pending_ui_component_size_ = ui_component_size_;
        pending_station_marker_size_ = station_marker_size_before_dialog_;
        station_marker_size_ = station_marker_size_before_dialog_;
        pending_theme_color_ = theme_color_;
        apply_ui_settings(font_size_, ui_component_size_, theme_color_, dpi_scale_, viewports_enabled_);
    }

    if (show_range_popup_) {
        ImGui::OpenPopup(tr("menu.plotlimit").c_str());
        show_range_popup_ = false;
    }
    if (ImGui::BeginPopupModal(tr("menu.plotlimit").c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::InputDouble("Min", &plot_min_);
        ImGui::InputDouble("Max", &plot_max_);
        if (ImGui::Button(tr("button.apply").c_str())) {
            dmin_ = plot_min_;
            dmax_ = plot_max_;
            keep_plan_view_ = false;
            reset_profile_axes_next_ = true;
            reset_radius_axes_next_ = true;
            profile_x_span_ = 0.0;
            radius_x_span_ = 0.0;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button(tr("button.reset").c_str())) {
            plot_min_ = model_.default_min;
            plot_max_ = model_.default_max;
        }
        ImGui::EndPopup();
    }

    if (show_cp_popup_) {
        ImGui::OpenPopup(tr("menu.controlpoints").c_str());
        show_cp_popup_ = false;
    }
    if (ImGui::BeginPopupModal(tr("menu.controlpoints").c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::InputDouble("Min", &cp_start_);
        ImGui::InputDouble("Max", &cp_end_);
        ImGui::InputDouble("Interval", &cp_interval_);
        if (ImGui::Button(tr("button.apply").c_str())) {
            regenerate_geometry();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button(tr("button.reset").c_str())) {
            cp_start_ = std::max(0.0, round_to_100(model_.own.empty() ? 0.0 : model_.own.at(0, 0)) - 500.0);
            cp_end_ = round_to_100(model_.own.empty() ? 0.0 : model_.own.at(model_.own.rows - 1, 0)) + 500.0;
            cp_interval_ = 25.0;
        }
        ImGui::EndPopup();
    }

    if (show_bg_adjust_popup_) {
        ImGui::OpenPopup(tr("dialog.bgimage_adjust").c_str());
        show_bg_adjust_popup_ = false;
    }
    if (ImGui::BeginPopupModal(tr("dialog.bgimage_adjust").c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        if (ImGui::BeginTable("bg_adjust_params", 2, ImGuiTableFlags_SizingStretchProp)) {
            auto input_row = [](const char* id, const std::string& label, double& value, const char* format) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextUnformatted(label.c_str());
                ImGui::TableSetColumnIndex(1);
                ImGui::SetNextItemWidth(220.0f);
                ImGui::InputDouble(id, &value, 0.0, 0.0, format);
            };
            auto slider_row = [](const char* id, const std::string& label, double& value, double min_value, double max_value, const char* format) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextUnformatted(label.c_str());
                ImGui::TableSetColumnIndex(1);
                ImGui::SetNextItemWidth(220.0f);
                float slider_value = static_cast<float>(std::clamp(value, min_value, max_value));
                if (ImGui::SliderFloat(id, &slider_value, static_cast<float>(min_value), static_cast<float>(max_value), format,
                                       ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_NoInput)) {
                    value = slider_value;
                }
            };
            input_row("##bg_adjust_x", tr("label.bgimg_x"), pending_bg_x_, "%.3f");
            input_row("##bg_adjust_y", tr("label.bgimg_y"), pending_bg_y_, "%.3f");
            input_row("##bg_adjust_width", tr("label.bgimg_width"), pending_bg_width_, "%.3f");
            input_row("##bg_adjust_height", tr("label.bgimg_height"), pending_bg_height_, "%.3f");
            input_row("##bg_adjust_rotation", tr("label.bgimg_rotation"), pending_bg_rotation_deg_, "%.3f");
            slider_row("##bg_adjust_brightness", tr("label.bgimg_brightness"), pending_bg_brightness_, 1.0, 200.0, "%.0f%%");
            ImGui::EndTable();
        }
        ImGui::Separator();
        if (ImGui::Button(tr("button.ok").c_str())) {
            apply_pending_background_values(true);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button(tr("button.cancel").c_str())) {
            sync_pending_background_values();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    if (show_align_popup_) {
        ImGui::OpenPopup(tr("dialog.align_to_station").c_str());
        show_align_popup_ = false;
    }
    if (ImGui::BeginPopupModal(tr("dialog.align_to_station").c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        auto combo_station = [&](const char* label, int& index) {
            index = std::clamp(index, 0, static_cast<int>(model_.stations.size()) - 1);
            std::string preview = model_.stations[index].key + ", " + model_.stations[index].name;
            if (ImGui::BeginCombo(label, preview.c_str())) {
                for (int i = 0; i < static_cast<int>(model_.stations.size()); ++i) {
                    std::string item = model_.stations[i].key + ", " + model_.stations[i].name;
                    if (ImGui::Selectable(item.c_str(), i == index)) index = i;
                }
                ImGui::EndCombo();
            }
        };
        if (has_model_ && model_.stations.size() >= 2) {
            combo_station("Station 1", align_station1_);
            std::string pick1_label = (align_pick1_ ? tr("button.pick_on_bg_ok") : tr("button.pick_on_bg")) + "##align_pick1";
            if (ImGui::Button(pick1_label.c_str())) {
                pick_slot_ = 1;
                ImGui::CloseCurrentPopup();
            }
            combo_station("Station 2", align_station2_);
            std::string pick2_label = (align_pick2_ ? tr("button.pick_on_bg_ok") : tr("button.pick_on_bg")) + "##align_pick2";
            if (ImGui::Button(pick2_label.c_str())) {
                pick_slot_ = 2;
                ImGui::CloseCurrentPopup();
            }
            if (ImGui::Button(tr("button.apply").c_str())) apply_background_alignment();
            ImGui::SameLine();
        }
        if (ImGui::Button(tr("button.ok").c_str())) {
            pick_slot_ = 0;
            apply_background_alignment();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    if (show_about_popup_) {
        ImGui::OpenPopup(tr("menu.about").c_str());
        show_about_popup_ = false;
    }
    ImGui::SetNextWindowSize(ImVec2(560.0f, 0.0f), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal(tr("menu.about").c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 520.0f);
        ImGui::TextUnformatted(tr("about.text").c_str());
        ImGui::PopTextWrapPos();
        if (ImGui::Button(tr("button.ok").c_str())) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

void App::handle_shortcuts() {
    if (ImGui::IsKeyPressed(ImGuiKey_F5, false) && has_model_ && !loading_ && !file_path_.empty()) {
        begin_load(file_path_, true);
    }
}

void App::render() {
    poll_loader();
    handle_shortcuts();
    render_menu();
    render_toolbar();
    ImGuiID dockspace_id = ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport());
    setup_initial_dockspace(dockspace_id);
    render_othertracks_window();
    render_station_list_window();
    render_console();
    render_plots();
    render_structures_window();
    render_repeaters_window();
    render_popups();
}

ID3D11Device* g_pd3dDevice = nullptr;
ID3D11DeviceContext* g_pd3dDeviceContext = nullptr;
IDXGISwapChain* g_pSwapChain = nullptr;
bool g_SwapChainOccluded = false;
UINT g_ResizeWidth = 0, g_ResizeHeight = 0;
ID3D11RenderTargetView* g_mainRenderTargetView = nullptr;

void CreateRenderTarget() {
    ID3D11Texture2D* back_buffer = nullptr;
    g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&back_buffer));
    g_pd3dDevice->CreateRenderTargetView(back_buffer, nullptr, &g_mainRenderTargetView);
    back_buffer->Release();
}

void CleanupRenderTarget() {
    release_com(g_mainRenderTargetView);
}

bool CreateDeviceD3D(HWND hWnd) {
    DXGI_SWAP_CHAIN_DESC sd = {};
    sd.BufferCount = 2;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hWnd;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    UINT flags = 0;
    D3D_FEATURE_LEVEL feature_level;
    const D3D_FEATURE_LEVEL levels[2] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0};
    HRESULT hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, levels, 2,
                                               D3D11_SDK_VERSION, &sd, &g_pSwapChain, &g_pd3dDevice,
                                               &feature_level, &g_pd3dDeviceContext);
    if (hr == DXGI_ERROR_UNSUPPORTED) {
        hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, flags, levels, 2,
                                           D3D11_SDK_VERSION, &sd, &g_pSwapChain, &g_pd3dDevice,
                                           &feature_level, &g_pd3dDeviceContext);
    }
    if (FAILED(hr)) return false;
    IDXGIFactory* factory = nullptr;
    if (SUCCEEDED(g_pSwapChain->GetParent(IID_PPV_ARGS(&factory)))) {
        factory->MakeWindowAssociation(hWnd, DXGI_MWA_NO_ALT_ENTER);
        factory->Release();
    }
    CreateRenderTarget();
    return true;
}

void CleanupDeviceD3D() {
    CleanupRenderTarget();
    release_com(g_pSwapChain);
    release_com(g_pd3dDeviceContext);
    release_com(g_pd3dDevice);
}

LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (::ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam)) return true;
    switch (msg) {
        case kAppWakeMessage:
            return 0;
        case WM_SIZE:
            if (wParam == SIZE_MINIMIZED) return 0;
            g_ResizeWidth = LOWORD(lParam);
            g_ResizeHeight = HIWORD(lParam);
            return 0;
        case WM_SYSCOMMAND:
            if ((wParam & 0xfff0) == SC_KEYMENU) return 0;
            break;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

} // namespace

int main(int, char**) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    UserSettings settings = load_user_settings();
    ImGui_ImplWin32_EnableDpiAwareness();
    float scale = ImGui_ImplWin32_GetDpiScaleForMonitor(MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY));

    WNDCLASSEXW wc = {sizeof(wc), CS_CLASSDC, WndProc, 0L, 0L, GetModuleHandle(nullptr), nullptr, nullptr, nullptr, nullptr, L"komapedit", nullptr};
    RegisterClassExW(&wc);
    HWND hwnd = CreateWindowW(wc.lpszClassName, L"komapedit", WS_OVERLAPPEDWINDOW,
                              100, 100, static_cast<int>(1440 * scale), static_cast<int>(900 * scale),
                              nullptr, nullptr, wc.hInstance, nullptr);
    g_main_hwnd = hwnd;
    if (!CreateDeviceD3D(hwnd)) {
        CleanupDeviceD3D();
        UnregisterClassW(wc.lpszClassName, wc.hInstance);
        g_main_hwnd = nullptr;
        CoUninitialize();
        return 1;
    }

    ShowWindow(hwnd, SW_SHOWDEFAULT);
    UpdateWindow(hwnd);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImPlot::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;

    bool viewports_enabled = (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) != 0;
    apply_ui_settings(settings.font_size, settings.ui_component_size, settings.theme_color, scale, viewports_enabled);
    io.ConfigDpiScaleFonts = true;
    io.ConfigDpiScaleViewports = true;

    const char* font_candidates[] = {
        "C:/Windows/Fonts/msyh.ttc",
        "C:/Windows/Fonts/YuGothM.ttc",
        "C:/Windows/Fonts/meiryo.ttc",
        "C:/Windows/Fonts/segoeui.ttf"
    };
    bool font_loaded = false;
    for (const char* font : font_candidates) {
        if (std::filesystem::exists(font)) {
            io.Fonts->AddFontFromFileTTF(font, kDefaultFontSize * scale, nullptr, io.Fonts->GetGlyphRangesChineseFull());
            font_loaded = true;
            break;
        }
    }
    if (!font_loaded) io.Fonts->AddFontDefault();

    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);

    App app(g_pd3dDevice, std::move(settings), scale, viewports_enabled);

    bool done = false;
    bool needs_render = true;
    int warmup_frames = 2;
    while (!done) {
        bool received_message = false;
        MSG msg;
        while (PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE)) {
            received_message = true;
            TranslateMessage(&msg);
            DispatchMessage(&msg);
            if (msg.message == WM_QUIT) done = true;
        }
        if (done) break;

        if (!needs_render && !received_message) {
            MsgWaitForMultipleObjectsEx(0, nullptr, INFINITE, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
            continue;
        }
        needs_render = false;

        if (g_SwapChainOccluded && g_pSwapChain->Present(0, DXGI_PRESENT_TEST) == DXGI_STATUS_OCCLUDED) {
            continue;
        }
        g_SwapChainOccluded = false;

        if (g_ResizeWidth != 0 && g_ResizeHeight != 0) {
            CleanupRenderTarget();
            g_pSwapChain->ResizeBuffers(0, g_ResizeWidth, g_ResizeHeight, DXGI_FORMAT_UNKNOWN, 0);
            g_ResizeWidth = g_ResizeHeight = 0;
            CreateRenderTarget();
        }

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();
        app.render();

        ImGui::Render();
        const float clear_color[4] = {0.06f, 0.07f, 0.08f, 1.0f};
        g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, nullptr);
        g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView, clear_color);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

        if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
            ImGui::UpdatePlatformWindows();
            ImGui::RenderPlatformWindowsDefault();
        }

        HRESULT hr = g_pSwapChain->Present(1, 0);
        g_SwapChainOccluded = (hr == DXGI_STATUS_OCCLUDED);
        if (warmup_frames > 0) {
            --warmup_frames;
            needs_render = true;
        }
    }

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImPlot::DestroyContext();
    ImGui::DestroyContext();

    CleanupDeviceD3D();
    g_main_hwnd = nullptr;
    DestroyWindow(hwnd);
    UnregisterClassW(wc.lpszClassName, wc.hInstance);
    CoUninitialize();
    return 0;
}
