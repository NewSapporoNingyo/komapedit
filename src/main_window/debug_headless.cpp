/*
 * Copyright (c) 2026 Sapporo_ningyo
 *
 * Licensed under Apache License 2.0; see LICENSE and NOTICE.
 * The GUI uses Dear ImGui and ImPlot; see THIRD_PARTY_NOTICES.md.
 */

#pragma execution_character_set("utf-8")

#include "kme.h"
#include "canvas3D.h"
#include "debug_headless.h"
#include "maploader.h"
#include "touch_input.h"

#include "imgui.h"
#include "imgui_internal.h"
#include "implot.h"

#include <windows.h>
#include <d3d11.h>
#include <shellapi.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <initializer_list>
#include <iostream>
#include <iterator>
#include <limits>
#include <map>
#include <sstream>
#include <set>
#include <string>
#include <utility>
#include <vector>
#ifndef NDEBUG
template <typename T>
void release_com(T*& p) {
    if (p) {
        p->Release();
        p = nullptr;
    }
}

std::vector<std::string> command_line_args_utf8() {
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    std::vector<std::string> args;
    if (!argv) return args;
    args.reserve(static_cast<size_t>(argc));
    for (int i = 0; i < argc; ++i) {
        args.push_back(wide_to_utf8(argv[i]));
    }
    LocalFree(argv);
    return args;
}

bool create_headless_d3d_device(ID3D11Device*& device, ID3D11DeviceContext*& context, const char*& driver) {
    device = nullptr;
    context = nullptr;
    driver = "hardware";
    D3D_FEATURE_LEVEL feature_level = D3D_FEATURE_LEVEL_11_0;
    const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0};
    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
                                   levels, 2, D3D11_SDK_VERSION, &device,
                                   &feature_level, &context);
    if (FAILED(hr)) {
        hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0,
                               levels, 2, D3D11_SDK_VERSION, &device,
                               &feature_level, &context);
        driver = "warp";
    }
    return SUCCEEDED(hr) && device && context;
}

double angle_distance(double a, double b) {
    return std::abs(std::atan2(std::sin(a - b), std::cos(a - b)));
}

HeadlessLoadOptions parse_headless_load_options(const std::vector<std::string>& args) {
    HeadlessLoadOptions options;
    for (size_t i = 1; i < args.size(); ++i) {
        const std::string& arg = args[i];
        if (arg == "--headless-load-map" || arg == "--headless-load") {
            options.requested = true;
            if (i + 1 >= args.size()) {
                options.error = arg + " requires a map path";
                return options;
            }
            options.path = args[++i];
        } else if (arg == "--repeat") {
            if (i + 1 >= args.size()) {
                options.error = "--repeat requires a number";
                return options;
            }
            char* end = nullptr;
            long parsed = std::strtol(args[++i].c_str(), &end, 10);
            if (!end || *end != '\0' || parsed <= 0 || parsed > 10000) {
                options.error = "--repeat must be between 1 and 10000";
                return options;
            }
            options.repeat = static_cast<int>(parsed);
        } else if (arg == "--unit-distance") {
            if (i + 1 >= args.size()) {
                options.error = "--unit-distance requires a number";
                return options;
            }
            char* end = nullptr;
            double parsed = std::strtod(args[++i].c_str(), &end);
            if (!end || *end != '\0' || parsed <= 0.0 || !std::isfinite(parsed)) {
                options.error = "--unit-distance must be a positive number";
                return options;
            }
            options.unit_distance = parsed;
        } else if (arg == "--ir-json-mode") {
            if (i + 1 >= args.size()) {
                options.error = "--ir-json-mode requires compact or full";
                return options;
            }
            std::string mode = args[++i];
            if (mode == "compact") {
                options.full_ir_json = false;
            } else if (mode == "full") {
                options.full_ir_json = true;
            } else {
                options.error = "--ir-json-mode must be compact or full";
                return options;
            }
        } else if (arg == "--load-profile") {
            if (i + 1 >= args.size()) {
                options.error = "--load-profile requires preview or edit";
                return options;
            }
            std::string profile = args[++i];
            if (profile == "preview" || profile == "edit") {
                options.load_profile = profile;
            } else {
                options.error = "--load-profile must be preview or edit";
                return options;
            }
        } else if (arg == "--cache-mode") {
            if (i + 1 >= args.size()) {
                options.error = "--cache-mode requires off, read, or rebuild";
                return options;
            }
            std::string mode = args[++i];
            if (mode == "off" || mode == "read" || mode == "rebuild") {
                options.cache_mode = mode;
            } else {
                options.error = "--cache-mode must be off, read, or rebuild";
                return options;
            }
        } else if (arg == "--headless-output") {
            if (i + 1 >= args.size()) {
                options.error = "--headless-output requires a path";
                return options;
            }
            options.output_path = args[++i];
        }
    }
    if (options.requested && options.path.empty() && options.error.empty()) {
        options.error = "--headless-load-map requires a map path";
    }
    return options;
}

HeadlessPlanBenchmarkOptions parse_headless_plan_benchmark_options(const std::vector<std::string>& args) {
    HeadlessPlanBenchmarkOptions options;
    for (size_t i = 1; i < args.size(); ++i) {
        const std::string& arg = args[i];
        if (arg == "--debug-headless-plan-bench") {
            options.requested = true;
            if (i + 1 >= args.size()) {
                options.error = arg + " requires a map path";
                return options;
            }
            options.path = args[++i];
        } else if (arg == "--frames") {
            if (i + 1 >= args.size()) {
                options.error = "--frames requires a number";
                return options;
            }
            char* end = nullptr;
            long parsed = std::strtol(args[++i].c_str(), &end, 10);
            if (!end || *end != '\0' || parsed <= 0 || parsed > 100000) {
                options.error = "--frames must be between 1 and 100000";
                return options;
            }
            options.frames = static_cast<int>(parsed);
        } else if (arg == "--unit-distance") {
            if (i + 1 >= args.size()) {
                options.error = "--unit-distance requires a number";
                return options;
            }
            char* end = nullptr;
            double parsed = std::strtod(args[++i].c_str(), &end);
            if (!end || *end != '\0' || parsed <= 0.0 || !std::isfinite(parsed)) {
                options.error = "--unit-distance must be a positive number";
                return options;
            }
            options.unit_distance = parsed;
        } else if (arg == "--pan-pixels") {
            if (i + 1 >= args.size()) {
                options.error = "--pan-pixels requires a number";
                return options;
            }
            char* end = nullptr;
            double parsed = std::strtod(args[++i].c_str(), &end);
            if (!end || *end != '\0' || !std::isfinite(parsed)) {
                options.error = "--pan-pixels must be a finite number";
                return options;
            }
            options.pan_pixels = parsed;
        } else if (arg == "--max-frame-ms") {
            if (i + 1 >= args.size()) {
                options.error = "--max-frame-ms requires a number";
                return options;
            }
            char* end = nullptr;
            double parsed = std::strtod(args[++i].c_str(), &end);
            if (!end || *end != '\0' || parsed <= 0.0 || !std::isfinite(parsed)) {
                options.error = "--max-frame-ms must be a positive number";
                return options;
            }
            options.max_frame_ms = parsed;
        } else if (arg == "--headless-output") {
            if (i + 1 >= args.size()) {
                options.error = "--headless-output requires a path";
                return options;
            }
            options.output_path = args[++i];
        } else if (arg == "--profile-stages") {
            options.profile_stages = true;
        }
    }
    if (options.requested && options.path.empty() && options.error.empty()) {
        options.error = "--debug-headless-plan-bench requires a map path";
    }
    return options;
}

HeadlessScene3DBenchmarkOptions parse_headless_scene3d_benchmark_options(const std::vector<std::string>& args) {
    HeadlessScene3DBenchmarkOptions options;
    for (size_t i = 1; i < args.size(); ++i) {
        const std::string& arg = args[i];
        if (arg == "--debug-headless-scene3d-bench") {
            options.requested = true;
            if (i + 1 >= args.size()) {
                options.error = arg + " requires a map path";
                return options;
            }
            options.path = args[++i];
        } else if (arg == "--frames") {
            if (i + 1 >= args.size()) {
                options.error = "--frames requires a number";
                return options;
            }
            char* end = nullptr;
            long parsed = std::strtol(args[++i].c_str(), &end, 10);
            if (!end || *end != '\0' || parsed <= 0 || parsed > 100000) {
                options.error = "--frames must be between 1 and 100000";
                return options;
            }
            options.frames = static_cast<int>(parsed);
        } else if (arg == "--unit-distance") {
            if (i + 1 >= args.size()) {
                options.error = "--unit-distance requires a number";
                return options;
            }
            char* end = nullptr;
            double parsed = std::strtod(args[++i].c_str(), &end);
            if (!end || *end != '\0' || parsed <= 0.0 || !std::isfinite(parsed)) {
                options.error = "--unit-distance must be a positive number";
                return options;
            }
            options.unit_distance = parsed;
        } else if (arg == "--max-frame-ms") {
            if (i + 1 >= args.size()) {
                options.error = "--max-frame-ms requires a number";
                return options;
            }
            char* end = nullptr;
            double parsed = std::strtod(args[++i].c_str(), &end);
            if (!end || *end != '\0' || parsed <= 0.0 || !std::isfinite(parsed)) {
                options.error = "--max-frame-ms must be a positive number";
                return options;
            }
            options.max_frame_ms = parsed;
        } else if (arg == "--window-back-m") {
            if (i + 1 >= args.size()) {
                options.error = "--window-back-m requires a number";
                return options;
            }
            char* end = nullptr;
            double parsed = std::strtod(args[++i].c_str(), &end);
            if (!end || *end != '\0' || parsed < 0.0 || !std::isfinite(parsed)) {
                options.error = "--window-back-m must be a non-negative number";
                return options;
            }
            options.window_back_m = parsed;
        } else if (arg == "--window-forward-m") {
            if (i + 1 >= args.size()) {
                options.error = "--window-forward-m requires a number";
                return options;
            }
            char* end = nullptr;
            double parsed = std::strtod(args[++i].c_str(), &end);
            if (!end || *end != '\0' || parsed <= 0.0 || !std::isfinite(parsed)) {
                options.error = "--window-forward-m must be a positive number";
                return options;
            }
            options.window_forward_m = parsed;
        } else if (arg == "--headless-output") {
            if (i + 1 >= args.size()) {
                options.error = "--headless-output requires a path";
                return options;
            }
            options.output_path = args[++i];
        }
    }
    if (options.requested && options.path.empty() && options.error.empty()) {
        options.error = "--debug-headless-scene3d-bench requires a map path";
    }
    return options;
}

HeadlessSceneCameraTransferOptions parse_headless_scene_camera_transfer_options(const std::vector<std::string>& args) {
    HeadlessSceneCameraTransferOptions options;
    for (size_t i = 1; i < args.size(); ++i) {
        const std::string& arg = args[i];
        if (arg == "--debug-headless-scene-camera-transfer") {
            options.requested = true;
            if (i + 1 >= args.size()) {
                options.error = arg + " requires a map path";
                return options;
            }
            options.path = args[++i];
        } else if (arg == "--unit-distance") {
            if (i + 1 >= args.size()) {
                options.error = "--unit-distance requires a number";
                return options;
            }
            char* end = nullptr;
            double parsed = std::strtod(args[++i].c_str(), &end);
            if (!end || *end != '\0' || parsed <= 0.0 || !std::isfinite(parsed)) {
                options.error = "--unit-distance must be a positive number";
                return options;
            }
            options.unit_distance = parsed;
        } else if (arg == "--camera-distance") {
            if (i + 1 >= args.size()) {
                options.error = "--camera-distance requires a number";
                return options;
            }
            char* end = nullptr;
            double parsed = std::strtod(args[++i].c_str(), &end);
            if (!end || *end != '\0' || !std::isfinite(parsed)) {
                options.error = "--camera-distance must be a finite number";
                return options;
            }
            options.has_camera_distance = true;
            options.camera_distance = parsed;
        } else if (arg == "--headless-output") {
            if (i + 1 >= args.size()) {
                options.error = "--headless-output requires a path";
                return options;
            }
            options.output_path = args[++i];
        }
    }
    if (options.requested && options.path.empty() && options.error.empty()) {
        options.error = "--debug-headless-scene-camera-transfer requires a map path";
    }
    return options;
}

HeadlessSourceAnchorOptions parse_headless_source_anchor_options(const std::vector<std::string>& args) {
    HeadlessSourceAnchorOptions options;
    for (size_t i = 1; i < args.size(); ++i) {
        const std::string& arg = args[i];
        if (arg == "--debug-headless-source-anchors") {
            options.requested = true;
            if (i + 1 >= args.size()) {
                options.error = arg + " requires a map path";
                return options;
            }
            options.path = args[++i];
        } else if (arg == "--unit-distance") {
            if (i + 1 >= args.size()) {
                options.error = "--unit-distance requires a number";
                return options;
            }
            char* end = nullptr;
            double parsed = std::strtod(args[++i].c_str(), &end);
            if (!end || *end != '\0' || parsed <= 0.0 || !std::isfinite(parsed)) {
                options.error = "--unit-distance must be a positive number";
                return options;
            }
            options.unit_distance = parsed;
        } else if (arg == "--headless-output") {
            if (i + 1 >= args.size()) {
                options.error = "--headless-output requires a path";
                return options;
            }
            options.output_path = args[++i];
        }
    }
    if (options.requested && options.path.empty() && options.error.empty()) {
        options.error = "--debug-headless-source-anchors requires a map path";
    }
    return options;
}

HeadlessEditRoundtripOptions parse_headless_edit_roundtrip_options(const std::vector<std::string>& args) {
    HeadlessEditRoundtripOptions options;
    for (size_t i = 1; i < args.size(); ++i) {
        const std::string& arg = args[i];
        if (arg == "--debug-headless-edit-roundtrip") {
            options.requested = true;
            if (i + 1 >= args.size()) {
                options.error = arg + " requires a map path";
                return options;
            }
            options.path = args[++i];
        } else if (arg == "--unit-distance") {
            if (i + 1 >= args.size()) {
                options.error = "--unit-distance requires a value";
                return options;
            }
            char* end = nullptr;
            double parsed = std::strtod(args[++i].c_str(), &end);
            if (!end || *end != '\0' || parsed <= 0.0) {
                options.error = "--unit-distance must be positive";
                return options;
            }
            options.unit_distance = parsed;
        } else if (arg == "--headless-output") {
            if (i + 1 >= args.size()) {
                options.error = "--headless-output requires a path";
                return options;
            }
            options.output_path = args[++i];
        }
    }
    if (options.requested && options.path.empty()) {
        options.error = "--debug-headless-edit-roundtrip requires a map path";
    }
    return options;
}

HeadlessDistanceEditBatchOptions parse_headless_distance_edit_batch_options(
    const std::vector<std::string>& args) {
    static constexpr const char* kDefaultMapPath =
        "E:\\Railway\\BveTsWorkspace\\BVE-Gensokyo-Railway\\GSR\\Scenarios_GSR\\map\\"
        "Config_Map121M-ATSP+Ps_Ask.txt";
    HeadlessDistanceEditBatchOptions options;
    for (size_t i = 1; i < args.size(); ++i) {
        const std::string& arg = args[i];
        if (arg == "--debug-headless-distance-edit-batch") {
            options.requested = true;
            if (i + 1 < args.size() && args[i + 1].rfind("--", 0) != 0) {
                options.path = args[++i];
            }
        } else if (arg == "--unit-distance") {
            if (i + 1 >= args.size()) {
                options.error = "--unit-distance requires a value";
                return options;
            }
            char* end = nullptr;
            double parsed = std::strtod(args[++i].c_str(), &end);
            if (!end || *end != '\0' || parsed <= 0.0 || !std::isfinite(parsed)) {
                options.error = "--unit-distance must be a positive finite number";
                return options;
            }
            options.unit_distance = parsed;
        } else if (arg == "--headless-output") {
            if (i + 1 >= args.size()) {
                options.error = "--headless-output requires a path";
                return options;
            }
            options.output_path = args[++i];
        } else if (arg == "--commit") {
            options.commit = true;
        }
    }
    if (options.requested && options.path.empty()) options.path = kDefaultMapPath;
    return options;
}

HeadlessTableFindOptions parse_headless_table_find_options(const std::vector<std::string>& args) {
    HeadlessTableFindOptions options;
    for (size_t i = 1; i < args.size(); ++i) {
        const std::string& arg = args[i];
        if (arg == "--debug-headless-table-find") {
            options.requested = true;
        } else if (arg == "--headless-output") {
            if (i + 1 >= args.size()) {
                options.error = "--headless-output requires a path";
                return options;
            }
            options.output_path = args[++i];
        }
    }
    return options;
}

HeadlessTouchInputOptions parse_headless_touch_input_options(const std::vector<std::string>& args) {
    HeadlessTouchInputOptions options;
    for (size_t i = 1; i < args.size(); ++i) {
        const std::string& arg = args[i];
        if (arg == "--debug-headless-touch-input") {
            options.requested = true;
        } else if (arg == "--headless-output") {
            if (i + 1 >= args.size()) {
                options.error = "--headless-output requires a path";
                return options;
            }
            options.output_path = args[++i];
        }
    }
    return options;
}

std::uint64_t hash_double_bits(double value) {
    if (value == 0.0) value = 0.0;
    std::uint64_t bits = 0;
    static_assert(sizeof(bits) == sizeof(value), "unexpected double size");
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

struct HeadlessBufferSummary {
    size_t rows = 0;
    size_t cols = 0;
    bool finite = true;
    std::uint64_t hash = 1469598103934665603ULL;
};

HeadlessBufferSummary summarize_headless_buffer(KvDoubleBuffer buffer) {
    HeadlessBufferSummary summary;
    summary.rows = buffer.rows;
    summary.cols = buffer.cols;
    if (!buffer.data || buffer.rows == 0 || buffer.cols == 0) return summary;
    const size_t count = buffer.rows * buffer.cols;
    for (size_t i = 0; i < count; ++i) {
        const double value = buffer.data[i];
        summary.finite = summary.finite && std::isfinite(value);
        std::uint64_t bits = hash_double_bits(value);
        for (int byte = 0; byte < 8; ++byte) {
            summary.hash ^= static_cast<unsigned char>((bits >> (byte * 8)) & 0xff);
            summary.hash *= 1099511628211ULL;
        }
    }
    return summary;
}

std::string hex_u64(std::uint64_t value) {
    std::ostringstream out;
    out << std::hex << std::setw(16) << std::setfill('0') << value;
    return out.str();
}

void print_headless_buffer_summary(std::ostream& out, const char* label, const HeadlessBufferSummary& summary) {
    out << " " << label << "=" << summary.rows << "x" << summary.cols
        << ":" << hex_u64(summary.hash)
        << (summary.finite ? "" : ":nonfinite");
}

int run_headless_load_map(const HeadlessLoadOptions& options) {
    std::ofstream output_file;
    std::ostream* out = &std::cout;
    if (!options.output_path.empty()) {
        output_file.open(std::filesystem::path(utf8_to_wide(options.output_path)), std::ios::out | std::ios::trunc);
        if (!output_file) {
            std::cerr << "failed to open headless output: " << options.output_path << "\n";
            return 1;
        }
        output_file.write("\xEF\xBB\xBF", 3);
        out = &output_file;
    }

    *out << "komapedit headless-load-map path=\"" << options.path
         << "\" repeat=" << options.repeat
         << " unit_distance=" << format_double(options.unit_distance, 3)
         << " load_profile=" << options.load_profile
         << " cache_mode=" << options.cache_mode
         << " ir_json_mode=" << (options.full_ir_json ? "full" : "compact") << "\n";

    for (int run = 1; run <= options.repeat; ++run) {
        auto started_at = std::chrono::steady_clock::now();
        const bool edit_profile = options.load_profile == "edit";
        unsigned load_flags = edit_profile ? KV_LOAD_EDIT_METADATA : KV_LOAD_PREVIEW;
        if (!edit_profile && options.cache_mode != "off") load_flags |= KV_LOAD_USE_PREVIEW_CACHE;
        if (!edit_profile && options.cache_mode == "rebuild") load_flags |= KV_LOAD_REBUILD_PREVIEW_CACHE;
        void* handle = kv_load_map_ex(options.path.c_str(), options.unit_distance, load_flags);
        auto loaded_at = std::chrono::steady_clock::now();
        if (!handle) {
            const char* err = kv_get_last_error();
            std::cerr << "headless run " << run << " failed: "
                      << (err ? err : "maploader failed") << "\n";
            return 2;
        }
        const bool preview_cache_hit = kv_get_preview_cache_hit(handle) != 0;

        unsigned ir_flags = options.full_ir_json
            ? (KV_IR_JSON_FULL_EDIT | KV_IR_JSON_FULL_STATEMENT_SOURCE)
            : KV_IR_JSON_COMPACT;
        const char* json = kv_get_ir_json_ex(handle, ir_flags);
        const size_t json_bytes = json ? std::strlen(json) : 0;
        if (json) kv_free_string(json);
        auto json_at = std::chrono::steady_clock::now();

        HeadlessBufferSummary own = summarize_headless_buffer(kv_get_owntrack_buffer(handle));
        HeadlessBufferSummary curve = summarize_headless_buffer(kv_get_curveradius_buffer(handle));
        HeadlessBufferSummary structures = summarize_headless_buffer(kv_get_structure_puts(handle));
        const size_t other_count = kv_get_othertrack_count(handle);
        HeadlessBufferSummary other_total;
        other_total.rows = 0;
        other_total.cols = 8;
        for (size_t i = 0; i < other_count; ++i) {
            const char* key = kv_get_othertrack_key(handle, i);
            HeadlessBufferSummary item = summarize_headless_buffer(kv_get_othertrack_buffer(handle, key));
            other_total.rows += item.rows;
            other_total.finite = other_total.finite && item.finite;
            other_total.hash ^= item.hash + 0x9e3779b97f4a7c15ULL + (other_total.hash << 6) + (other_total.hash >> 2);
        }
        kv_free(handle);
        auto finished_at = std::chrono::steady_clock::now();

        const double load_seconds = std::chrono::duration<double>(loaded_at - started_at).count();
        const double json_seconds = std::chrono::duration<double>(json_at - loaded_at).count();
        const double total_seconds = std::chrono::duration<double>(finished_at - started_at).count();
        *out << "headless run " << run
             << " load=" << std::fixed << std::setprecision(3) << load_seconds << "s"
             << " json=" << json_seconds << "s"
             << " total=" << total_seconds << "s"
             << " preview_cache=" << (preview_cache_hit ? "hit" : "miss")
             << " json_bytes=" << json_bytes
             << " othertracks=" << other_count;
        print_headless_buffer_summary(*out, "own", own);
        print_headless_buffer_summary(*out, "curve", curve);
        print_headless_buffer_summary(*out, "structures", structures);
        print_headless_buffer_summary(*out, "other", other_total);
        *out << "\n";
    }
    return 0;
}

int App::run_debug_headless_table_find(const std::string& output_path) {
    std::ofstream output_file;
    std::ostream* out = &std::cout;
    if (!output_path.empty()) {
        output_file.open(std::filesystem::path(utf8_to_wide(output_path)), std::ios::out | std::ios::trunc);
        if (!output_file) {
            std::cerr << "failed to open headless output: " << output_path << "\n";
            return 1;
        }
        output_file.write("\xEF\xBB\xBF", 3);
        out = &output_file;
    }

    *out << "komapedit debug-headless-table-find\n";
    out->flush();

    ImGui::CreateContext();
    ImPlot::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(1280.0f, 720.0f);
    io.DeltaTime = 1.0f / 60.0f;
    io.IniFilename = nullptr;
    io.Fonts->AddFontDefault();
    io.Fonts->Build();
    ImGui::NewFrame();

    int exit_code = 0;
    try {
        auto make_row = [](std::initializer_list<std::pair<const char*, const char*>> cells) {
            TableRow row;
            for (const auto& cell : cells) row.cells[cell.first] = cell.second;
            return row;
        };

        UserSettings settings;
        settings.language = Language::En;
        App app(nullptr, settings, 1.0f, false, false);
        app.model_.signal_aspects.push_back(make_row({
            {"signalAspectKey", "aspectA"},
            {"_structureKeyCount", "1"},
            {"structureKey1", "structureOnlyKey"},
        }));
        app.model_.signal_aspects.push_back(make_row({
            {"signalAspectKey", "aspectB"},
            {"_structureKeyCount", "1"},
            {"structureKey1", "modelB"},
        }));
        app.model_.signal_aspects.push_back(make_row({
            {"signalAspectKey", "unusedAspect"},
            {"_structureKeyCount", "1"},
            {"structureKey1", "modelUnused"},
        }));
        app.model_.signals.push_back(make_row({
            {"distance", "100"},
            {"signalAspectKey", "aspectA"},
        }));
        app.model_.signals.push_back(make_row({
            {"distance", "200"},
            {"signalAspectKey", "AspectB"},
        }));
        app.model_.signals.push_back(make_row({
            {"distance", "300"},
            {"signalAspectKey", "structureOnlyKey"},
        }));
        app.has_model_ = true;

        auto check = [&](bool condition, const char* label) {
            *out << label << "=" << (condition ? "PASS" : "FAIL") << "\n";
            if (!condition) exit_code = 2;
        };

        app.find_signal_aspect_for_signal_aspect_key("aspectA");
        check(app.show_signal_aspects_window_, "opens_signal_aspect_window");
        check(app.signal_aspect_find_.exact, "signal_aspect_context_search_is_exact");
        check(app.signal_aspect_find_.matches.size() == 1 &&
              app.signal_aspect_find_.matches[0] == 0 &&
              app.signal_aspect_find_.scroll_row == 0,
              "finds_signal_aspect_key_column");

        app.find_signal_aspect_for_signal_aspect_key("ASPECTB");
        check(app.signal_aspect_find_.matches.size() == 1 &&
              app.signal_aspect_find_.matches[0] == 1,
              "finds_signal_aspect_key_case_insensitive");

        app.find_signal_aspect_for_signal_aspect_key("structureOnlyKey");
        check(app.signal_aspect_find_.matches.empty(),
              "does_not_match_signal_aspect_structure_key_columns");

        app.run_unused_signal_aspect_search();
        check(app.signal_aspect_find_.unused_has_run &&
              app.signal_aspect_find_.unused_total == 3 &&
              app.signal_aspect_find_.unused_count == 1,
              "finds_unused_signal_aspects_count");
        check(app.signal_aspect_find_.unused_row_matches.size() == 3 &&
              app.signal_aspect_find_.unused_row_matches[0] == 0 &&
              app.signal_aspect_find_.unused_row_matches[1] == 0 &&
              app.signal_aspect_find_.unused_row_matches[2] != 0 &&
              app.signal_aspect_find_.scroll_row == 2,
              "marks_only_unused_signal_aspect_row");
    } catch (const std::exception& e) {
        *out << "exception=\"" << e.what() << "\"\n";
        exit_code = 3;
    }

    ImGui::EndFrame();
    ImPlot::DestroyContext();
    ImGui::DestroyContext();
    if (exit_code == 0) *out << "result=PASS\n";
    else *out << "result=FAIL code=" << exit_code << "\n";
    out->flush();
    return exit_code;
}

int run_debug_headless_touch_input(const HeadlessTouchInputOptions& options) {
    std::ofstream output_file;
    std::ostream* out = &std::cout;
    if (!options.output_path.empty()) {
        output_file.open(std::filesystem::path(utf8_to_wide(options.output_path)), std::ios::out | std::ios::trunc);
        if (!output_file) {
            std::cerr << "failed to open headless output: " << options.output_path << "\n";
            return 1;
        }
        output_file.write("\xEF\xBB\xBF", 3);
        out = &output_file;
    }

    *out << "komapedit debug-headless-touch-input\n";
    int exit_code = 0;
    auto check = [&](bool condition, const char* label) {
        *out << label << "=" << (condition ? "PASS" : "FAIL") << "\n";
        if (!condition) exit_code = 2;
    };

    touch_input::debug_reset_for_tests(0.0);
    touch_input::debug_touch_down(1, ImVec2(100.0f, 100.0f));
    touch_input::debug_set_time_for_tests(0.08);
    touch_input::debug_touch_up(1, ImVec2(103.0f, 102.0f));
    touch_input::new_frame();
    check(touch_input::current_frame().tap, "short_touch_emits_tap");
    check(!touch_input::current_frame().long_press, "short_touch_not_long_press");

    touch_input::debug_reset_for_tests(0.0);
    touch_input::debug_touch_down(1, ImVec2(200.0f, 200.0f));
    touch_input::debug_set_time_for_tests(0.60);
    touch_input::new_frame();
    check(touch_input::current_frame().long_press, "stationary_touch_emits_long_press");
    check(!touch_input::current_frame().tap, "long_press_not_tap");
    touch_input::debug_touch_up(1, ImVec2(200.0f, 200.0f));
    touch_input::new_frame();
    check(!touch_input::current_frame().tap, "long_press_release_not_tap");

    touch_input::debug_reset_for_tests(0.0);
    touch_input::debug_touch_down(1, ImVec2(300.0f, 300.0f));
    touch_input::debug_set_time_for_tests(0.20);
    touch_input::debug_touch_move(1, ImVec2(340.0f, 300.0f));
    touch_input::debug_set_time_for_tests(0.70);
    touch_input::new_frame();
    check(!touch_input::current_frame().long_press, "moved_touch_cancels_long_press");
    check(touch_input::current_frame().single_drag, "moved_touch_emits_single_drag");

    touch_input::debug_reset_for_tests(0.0);
    touch_input::debug_touch_down(1, ImVec2(100.0f, 100.0f));
    touch_input::debug_touch_down(2, ImVec2(200.0f, 100.0f));
    touch_input::debug_set_time_for_tests(0.02);
    touch_input::debug_touch_move(2, ImVec2(220.0f, 120.0f));
    touch_input::new_frame();
    const touch_input::TouchFrame& pinch = touch_input::current_frame();
    check(pinch.pinch, "two_touch_emits_pinch");
    check(pinch.pinch_scale > 1.0f, "two_touch_scale_grows");
    check(pinch.pinch_axis == touch_input::PinchAxis::Horizontal, "two_touch_axis_horizontal");
    check(pinch.pinch_x_scale > 1.0f, "two_touch_x_scale_grows");
    check(std::abs(pinch.pinch_rotation_delta) > 0.01f, "two_touch_rotation_delta");

    touch_input::debug_reset_for_tests(0.0);
    touch_input::debug_touch_down(1, ImVec2(300.0f, 100.0f));
    touch_input::debug_touch_down(2, ImVec2(300.0f, 200.0f));
    touch_input::debug_set_time_for_tests(0.02);
    touch_input::debug_touch_move(2, ImVec2(300.0f, 240.0f));
    touch_input::new_frame();
    const touch_input::TouchFrame& vertical_pinch = touch_input::current_frame();
    check(vertical_pinch.pinch, "vertical_two_touch_emits_pinch");
    check(vertical_pinch.pinch_axis == touch_input::PinchAxis::Vertical, "two_touch_axis_vertical");
    check(vertical_pinch.pinch_y_scale > 1.0f, "two_touch_y_scale_grows");

    ImGui::CreateContext();
    GImGui->InputEventsQueue.clear();
    touch_input::debug_reset_for_tests(0.0);
    touch_input::debug_touch_down(1, ImVec2(50.0f, 60.0f));
    const int queued_after_down = GImGui->InputEventsQueue.Size;
    touch_input::debug_touch_up(1, ImVec2(50.0f, 60.0f));
    const int queued_after_up = GImGui->InputEventsQueue.Size;
    check(queued_after_down >= 2, "touch_down_queues_imgui_mouse_events");
    check(queued_after_up >= queued_after_down + 1, "touch_up_queues_imgui_mouse_events");
    ImGui::DestroyContext();

    *out << "result=" << (exit_code == 0 ? "PASS" : "FAIL") << "\n";
    out->flush();
    return exit_code;
}

int App::run_debug_headless_source_anchors(const std::string& path, double unit_distance,
                                           const std::string& output_path) {
    std::ofstream output_file;
    std::ostream* out = &std::cout;
    if (!output_path.empty()) {
        output_file.open(std::filesystem::path(utf8_to_wide(output_path)), std::ios::out | std::ios::trunc);
        if (!output_file) {
            std::cerr << "failed to open headless output: " << output_path << "\n";
            return 1;
        }
        output_file.write("\xEF\xBB\xBF", 3);
        out = &output_file;
    }

    *out << "komapedit debug-headless-source-anchors path=\"" << path
         << "\" unit_distance=" << format_double(unit_distance, 3) << "\n";
    LoadResult result = load_map_worker(path, unit_distance, false, 0.0, 0.0, 25.0,
                                        LoadModelOptions{true});
    if (!result.ok) {
        std::cerr << "debug headless source anchors load failed: " << result.error << "\n";
        return 2;
    }

    const MapModel& model = result.model;
    std::set<std::string> seen_edit_ids;
    size_t duplicate_edit_id_count = 0;
    auto note_edit_id = [&](const std::string& edit_id) {
        if (edit_id.empty()) return;
        if (!seen_edit_ids.insert(edit_id).second) ++duplicate_edit_id_count;
    };
    for (const EditStatementInfo& statement : model.edit_statements) note_edit_id(statement.edit_id);
    for (const EditElementInfo& element : model.edit_elements) note_edit_id(element.edit_id);

    size_t invalid_source_span_count = 0;
    for (const EditStatementInfo& statement : model.edit_statements) {
        if (statement.source.file_path.empty() || statement.source.line <= 0 || statement.source.column <= 0 ||
            statement.raw_text.empty()) {
            ++invalid_source_span_count;
        }
    }

    size_t missing_edit_id_count = 0;
    auto count_missing_rows = [&](const std::vector<TableRow>& rows) {
        for (const TableRow& row : rows) {
            if (row.edit_id.empty()) ++missing_edit_id_count;
        }
    };
    count_missing_rows(model.station_list_rows);
    count_missing_rows(model.structures);
    count_missing_rows(model.structure_models);
    count_missing_rows(model.other_trains);
    count_missing_rows(model.other_train_stops);
    count_missing_rows(model.other_train_structure_keys);
    count_missing_rows(model.other_train_sound_3d_keys);
    count_missing_rows(model.sound_list);
    count_missing_rows(model.structures_between);
    count_missing_rows(model.repeaters);
    count_missing_rows(model.signal_aspects);
    count_missing_rows(model.signals);
    count_missing_rows(model.beacons);
    count_missing_rows(model.pretrains);
    count_missing_rows(model.irregularities);
    count_missing_rows(model.map_sounds);
    count_missing_rows(model.map_sound_3d);
    count_missing_rows(model.rolling_noises);
    count_missing_rows(model.flange_noises);
    count_missing_rows(model.joint_noises);
    count_missing_rows(model.backgrounds);
    count_missing_rows(model.adhesions);
    count_missing_rows(model.cab_illuminance);
    count_missing_rows(model.fogs);

    std::set<std::string> map_files;
    std::set<std::string> list_files;
    for (const EditStatementInfo& statement : model.edit_statements) {
        const bool list_statement =
            statement.statement_kind.find("List.Row") != std::string::npos ||
            statement.statement_kind == "OtherTrainFile.Row";
        if (list_statement) {
            list_files.insert(statement.source.file_path);
        } else if (!statement.source.file_path.empty()) {
            map_files.insert(statement.source.file_path);
        }
    }

    *out << "file_count=" << model.edit_files.size() << "\n";
    *out << "map_file_count=" << map_files.size() << "\n";
    *out << "list_file_count=" << list_files.size() << "\n";
    *out << "statement_count=" << model.edit_statements.size() << "\n";
    *out << "element_count=" << model.edit_elements.size() << "\n";
    *out << "missing_editId_count=" << missing_edit_id_count << "\n";
    *out << "duplicate_editId_count=" << duplicate_edit_id_count << "\n";
    *out << "invalid_source_span_count=" << invalid_source_span_count << "\n";
    *out << "result=" << (duplicate_edit_id_count == 0 && invalid_source_span_count == 0 ? "PASS" : "FAIL") << "\n";
    out->flush();

    if (result.handle) kv_free(result.handle);
    return duplicate_edit_id_count == 0 && invalid_source_span_count == 0 ? 0 : 3;
}

int App::run_debug_headless_edit_roundtrip(const std::string& path, double unit_distance,
                                           const std::string& output_path) {
    std::ofstream output_file;
    std::ostream* out = &std::cout;
    if (!output_path.empty()) {
        output_file.open(std::filesystem::path(utf8_to_wide(output_path)), std::ios::out | std::ios::trunc);
        if (!output_file) {
            std::cerr << "failed to open headless output: " << output_path << "\n";
            return 1;
        }
        output_file.write("\xEF\xBB\xBF", 3);
        out = &output_file;
    }

    auto append_json_string = [](std::ostringstream& json, const std::string& text) {
        json << '"';
        for (unsigned char ch : text) {
            switch (ch) {
                case '\\': json << "\\\\"; break;
                case '"': json << "\\\""; break;
                case '\n': json << "\\n"; break;
                case '\r': json << "\\r"; break;
                case '\t': json << "\\t"; break;
                default: json << static_cast<char>(ch); break;
            }
        }
        json << '"';
    };
    auto source_hash_for_path = [](const MapModel& model, const std::string& file_path) {
        for (const EditSourceFileInfo& file : model.edit_files) {
            if (file.file_path == file_path) return file.source_hash;
        }
        return std::string{};
    };
    auto make_update_json = [&](const TableRow& row, const MapModel& model,
                                const std::map<std::string, std::string>& fields,
                                const std::string& expected_hash_override = {}) {
        std::string expected_hash = expected_hash_override.empty()
            ? source_hash_for_path(model, row.source.file_path)
            : expected_hash_override;
        std::ostringstream json;
        json << "{\"changes\":[{\"changeId\":\"headless-update\",\"editId\":";
        append_json_string(json, row.edit_id);
        json << ",\"operation\":\"update\",\"expectedSourceHash\":";
        append_json_string(json, expected_hash);
        json << ",\"fieldChanges\":{";
        bool first = true;
        for (const auto& field : fields) {
            if (!first) json << ",";
            first = false;
            append_json_string(json, field.first);
            json << ":";
            append_json_string(json, field.second);
        }
        json << "}}]}";
        return json.str();
    };
    auto report_ok = [](const std::string& report) {
        return report.find("\"ok\":true") != std::string::npos;
    };
    auto call_dry_run = [](void* handle, const std::string& changes) {
        const char* raw = kv_edit_dry_run(handle, changes.c_str());
        std::string text = raw ? raw : "";
        if (raw) kv_free_string(raw);
        return text;
    };
    auto call_apply_to_memory = [](void* handle, const std::string& changes) {
        const char* raw = kv_edit_apply_to_memory(handle, changes.c_str());
        std::string text = raw ? raw : "";
        if (raw) kv_free_string(raw);
        return text;
    };
    auto call_direct_apply = [](void* handle, const std::string& changes) {
        const char* raw = kv_edit_apply(handle, changes.c_str());
        std::string text = raw ? raw : "";
        if (raw) kv_free_string(raw);
        return text;
    };
    auto call_commit = [](void* handle) {
        const char* raw = kv_edit_commit(handle);
        std::string text = raw ? raw : "";
        if (raw) kv_free_string(raw);
        return text;
    };
    auto first_structure_values = [&](void* handle, const std::string& map_path,
                                      double& distance, double& x) {
        MapModel model = build_model_from_handle(handle, map_path, LoadModelOptions{true});
        if (model.structures.empty()) return false;
        const TableRow& row = model.structures.front();
        distance = table_cell_number(row, "distance");
        x = table_cell_number(row, "x");
        return true;
    };

    *out << "komapedit debug-headless-edit-roundtrip source_path=\"" << path
         << "\" unit_distance=" << format_double(unit_distance, 3) << "\n";

    std::filesystem::path temp_root = std::filesystem::temp_directory_path() /
        ("komapedit-edit-roundtrip-" + std::to_string(GetCurrentProcessId()) + "-" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(temp_root);
    std::filesystem::path temp_map = temp_root / "map.txt";
    std::filesystem::path temp_direct_map = temp_root / "direct_map.txt";
    std::filesystem::path temp_structures = temp_root / "structures.csv";
    auto write_map_fixture = [](const std::filesystem::path& map_path) {
        std::ofstream map_file(map_path, std::ios::out | std::ios::trunc | std::ios::binary);
        map_file << "BveTs Map 2.02:utf-8\n"
                 << "0;\n"
                 << "Structure.Load('structures.csv');\n"
                 << "100;\n"
                 << "Station['STA'].Put();\n"
                 << "Structure['pole'].Put('0',1,2,3,0,0,0,0,25);\n"
                 << "Structure['bridge'].PutBetween('0','1',0);\n"
                 << "200;\n";
    };
    write_map_fixture(temp_map);
    write_map_fixture(temp_direct_map);
    {
        std::ofstream structure_file(temp_structures, std::ios::out | std::ios::trunc | std::ios::binary);
        structure_file << "BveTs Structure List 1.00:utf-8\n"
                       << "pole,pole.csv\n"
                       << "bridge,bridge.csv\n";
    }
    *out << "temp_map=\"" << wide_to_utf8(temp_map.wstring()) << "\"\n";

    int exit_code = 0;
    LoadResult load = load_map_worker(wide_to_utf8(temp_map.wstring()), unit_distance,
                                      false, 0.0, 0.0, 25.0, LoadModelOptions{true});
    if (!load.ok) {
        *out << "load_error=" << load.error << "\nresult=FAIL\n";
        std::filesystem::remove_all(temp_root);
        return 2;
    }
    if (load.model.structures.empty()) {
        *out << "missing_structure_rows=1\nresult=FAIL\n";
        if (load.handle) kv_free(load.handle);
        std::filesystem::remove_all(temp_root);
        return 3;
    }

    const TableRow& structure_row = load.model.structures.front();
    std::string changes = make_update_json(structure_row, load.model, {
        {"distance", "125"},
        {"x", "9"},
        {"y", "2"},
        {"z", "3"}
    });
    std::string dry_report = call_dry_run(load.handle, changes);
    const bool dry_ok = report_ok(dry_report);
    *out << "dry_run_ok=" << (dry_ok ? 1 : 0) << "\n";
    if (!dry_ok) {
        *out << "dry_run_report=" << dry_report << "\n";
        exit_code = 4;
    }

    std::string stale_changes = make_update_json(structure_row, load.model, {{"x", "11"}}, "bad-hash");
    std::string stale_report = call_dry_run(load.handle, stale_changes);
    const bool stale_blocked = !report_ok(stale_report) &&
        stale_report.find("source file changed externally") != std::string::npos;
    *out << "stale_hash_blocked=" << (stale_blocked ? 1 : 0) << "\n";
    if (!stale_blocked) {
        *out << "stale_hash_report=" << stale_report << "\n";
        exit_code = 5;
    }

    std::string apply_memory_report;
    if (exit_code == 0) {
        apply_memory_report = call_apply_to_memory(load.handle, changes);
        const bool apply_memory_ok = report_ok(apply_memory_report);
        *out << "apply_to_memory_ok=" << (apply_memory_ok ? 1 : 0) << "\n";
        if (!apply_memory_ok) {
            *out << "apply_to_memory_report=" << apply_memory_report << "\n";
            exit_code = 6;
        } else {
            double memory_distance = 0.0;
            double memory_x = 0.0;
            const bool memory_matches =
                first_structure_values(load.handle, wide_to_utf8(temp_map.wstring()), memory_distance, memory_x) &&
                std::abs(memory_distance - 125.0) < 1e-6 &&
                std::abs(memory_x - 9.0) < 1e-6;
            *out << "memory_distance=" << format_double(memory_distance, 3) << "\n";
            *out << "memory_x=" << format_double(memory_x, 3) << "\n";
            *out << "apply_to_memory_match=" << (memory_matches ? 1 : 0) << "\n";
            if (!memory_matches) exit_code = 6;
        }
    }

    if (exit_code == 0) {
        const bool reset_ok = kv_edit_reset_memory(load.handle) != 0;
        double reset_distance = 0.0;
        double reset_x = 0.0;
        const bool reset_matches = reset_ok &&
            first_structure_values(load.handle, wide_to_utf8(temp_map.wstring()), reset_distance, reset_x) &&
            std::abs(reset_distance - 100.0) < 1e-6 &&
            std::abs(reset_x - 1.0) < 1e-6;
        *out << "reset_memory_ok=" << (reset_ok ? 1 : 0) << "\n";
        *out << "reset_revert_match=" << (reset_matches ? 1 : 0) << "\n";
        if (!reset_matches) exit_code = 7;
    }

    if (exit_code == 0) {
        std::string reapply_report = call_apply_to_memory(load.handle, changes);
        const bool reapply_ok = report_ok(reapply_report);
        *out << "reapply_to_memory_ok=" << (reapply_ok ? 1 : 0) << "\n";
        if (!reapply_ok) {
            *out << "reapply_report=" << reapply_report << "\n";
            exit_code = 8;
        }
    }

    if (exit_code == 0) {
        std::string commit_report = call_commit(load.handle);
        const bool commit_ok = report_ok(commit_report);
        *out << "commit_ok=" << (commit_ok ? 1 : 0) << "\n";
        if (!commit_ok) {
            *out << "commit_report=" << commit_report << "\n";
            exit_code = 9;
        }
    }
    if (load.handle) kv_free(load.handle);

    if (exit_code == 0) {
        LoadResult reload = load_map_worker(wide_to_utf8(temp_map.wstring()), unit_distance,
                                            false, 0.0, 0.0, 25.0, LoadModelOptions{true});
        if (!reload.ok || reload.model.structures.empty()) {
            *out << "reload_error=" << reload.error << "\n";
            exit_code = 10;
        } else {
            const TableRow& updated = reload.model.structures.front();
            const double distance = table_cell_number(updated, "distance");
            const double x = table_cell_number(updated, "x");
            const bool reload_matches = std::abs(distance - 125.0) < 1e-6 &&
                                        std::abs(x - 9.0) < 1e-6;
            *out << "reload_distance=" << format_double(distance, 3) << "\n";
            *out << "reload_x=" << format_double(x, 3) << "\n";
            *out << "save_reload_match=" << (reload_matches ? 1 : 0) << "\n";
            if (!reload_matches) exit_code = 11;
        }
        if (reload.handle) kv_free(reload.handle);
    }

    if (exit_code == 0) {
        LoadResult direct_load = load_map_worker(wide_to_utf8(temp_direct_map.wstring()), unit_distance,
                                                 false, 0.0, 0.0, 25.0, LoadModelOptions{true});
        if (!direct_load.ok || direct_load.model.structures.empty()) {
            *out << "direct_load_error=" << direct_load.error << "\n";
            exit_code = 12;
        } else {
            std::string direct_changes = make_update_json(direct_load.model.structures.front(),
                                                          direct_load.model, {{"x", "12"}});
            std::string direct_report = call_direct_apply(direct_load.handle, direct_changes);
            const bool direct_ok = report_ok(direct_report);
            *out << "direct_apply_ok=" << (direct_ok ? 1 : 0) << "\n";
            if (!direct_ok) {
                *out << "direct_apply_report=" << direct_report << "\n";
                exit_code = 13;
            }
        }
        if (direct_load.handle) kv_free(direct_load.handle);
    }

    if (exit_code == 0) {
        LoadResult direct_reload = load_map_worker(wide_to_utf8(temp_direct_map.wstring()), unit_distance,
                                                   false, 0.0, 0.0, 25.0, LoadModelOptions{true});
        if (!direct_reload.ok || direct_reload.model.structures.empty()) {
            *out << "direct_reload_error=" << direct_reload.error << "\n";
            exit_code = 14;
        } else {
            const double direct_x = table_cell_number(direct_reload.model.structures.front(), "x");
            const bool direct_matches = std::abs(direct_x - 12.0) < 1e-6;
            *out << "direct_reload_x=" << format_double(direct_x, 3) << "\n";
            *out << "direct_apply_reload_match=" << (direct_matches ? 1 : 0) << "\n";
            if (!direct_matches) exit_code = 15;
        }
        if (direct_reload.handle) kv_free(direct_reload.handle);
    }

    std::filesystem::remove_all(temp_root);
    *out << "result=" << (exit_code == 0 ? "PASS" : "FAIL") << "\n";
    out->flush();
    return exit_code;
}

namespace distance_batch_headless {

struct JsonValue {
    enum class Type { Null, Bool, Number, String, Array, Object };
    Type type = Type::Null;
    bool boolean = false;
    double number = 0.0;
    std::string string;
    std::vector<JsonValue> array;
    std::map<std::string, JsonValue> object;

    bool is_bool() const { return type == Type::Bool; }
    bool is_number() const { return type == Type::Number; }
    bool is_string() const { return type == Type::String; }
    bool is_array() const { return type == Type::Array; }
    bool is_object() const { return type == Type::Object; }

    const JsonValue& at(const std::string& key) const {
        static const JsonValue empty;
        auto it = object.find(key);
        return it == object.end() ? empty : it->second;
    }

    std::string scalar_text() const {
        if (is_string()) return string;
        if (is_bool()) return boolean ? "true" : "false";
        if (is_number()) {
            std::ostringstream out;
            out << std::setprecision(17) << number;
            return out.str();
        }
        return {};
    }
};

class JsonParser {
public:
    explicit JsonParser(const std::string& source) : source_(source) {}

    JsonValue parse() {
        skip_ws();
        JsonValue value = parse_value();
        skip_ws();
        if (!eof()) throw std::runtime_error("unexpected trailing JSON data");
        return value;
    }

private:
    const std::string& source_;
    size_t position_ = 0;

    bool eof() const { return position_ >= source_.size(); }
    char peek() const { return eof() ? '\0' : source_[position_]; }
    char get() { return eof() ? '\0' : source_[position_++]; }

    void skip_ws() {
        while (!eof() && std::isspace(static_cast<unsigned char>(peek()))) ++position_;
    }

    [[noreturn]] void fail(const char* message) const {
        throw std::runtime_error(std::string("invalid JSON: ") + message);
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
            const char ch = get();
            value <<= 4;
            if (ch >= '0' && ch <= '9') value |= static_cast<unsigned>(ch - '0');
            else if (ch >= 'a' && ch <= 'f') value |= static_cast<unsigned>(ch - 'a' + 10);
            else if (ch >= 'A' && ch <= 'F') value |= static_cast<unsigned>(ch - 'A' + 10);
            else fail("bad unicode escape");
        }
        return value;
    }

    JsonValue parse_string() {
        if (get() != '"') fail("expected string");
        JsonValue value;
        value.type = JsonValue::Type::String;
        while (!eof()) {
            const char ch = get();
            if (ch == '"') return value;
            if (ch != '\\') {
                value.string.push_back(ch);
                continue;
            }
            const char escaped = get();
            switch (escaped) {
                case '"': value.string.push_back('"'); break;
                case '\\': value.string.push_back('\\'); break;
                case '/': value.string.push_back('/'); break;
                case 'b': value.string.push_back('\b'); break;
                case 'f': value.string.push_back('\f'); break;
                case 'n': value.string.push_back('\n'); break;
                case 'r': value.string.push_back('\r'); break;
                case 't': value.string.push_back('\t'); break;
                case 'u': append_utf8(value.string, parse_hex4()); break;
                default: fail("bad string escape");
            }
        }
        fail("unterminated string");
    }

    JsonValue parse_number() {
        const size_t begin = position_;
        if (peek() == '-') ++position_;
        while (std::isdigit(static_cast<unsigned char>(peek()))) ++position_;
        if (peek() == '.') {
            ++position_;
            while (std::isdigit(static_cast<unsigned char>(peek()))) ++position_;
        }
        if (peek() == 'e' || peek() == 'E') {
            ++position_;
            if (peek() == '+' || peek() == '-') ++position_;
            while (std::isdigit(static_cast<unsigned char>(peek()))) ++position_;
        }
        char* end = nullptr;
        JsonValue value;
        value.type = JsonValue::Type::Number;
        value.number = std::strtod(source_.c_str() + begin, &end);
        if (!end || static_cast<size_t>(end - source_.c_str()) != position_) fail("bad number");
        return value;
    }

    JsonValue parse_array() {
        JsonValue value;
        value.type = JsonValue::Type::Array;
        get();
        skip_ws();
        if (peek() == ']') {
            get();
            return value;
        }
        while (true) {
            value.array.push_back(parse_value());
            skip_ws();
            const char separator = get();
            if (separator == ']') return value;
            if (separator != ',') fail("expected array separator");
            skip_ws();
        }
    }

    JsonValue parse_object() {
        JsonValue value;
        value.type = JsonValue::Type::Object;
        get();
        skip_ws();
        if (peek() == '}') {
            get();
            return value;
        }
        while (true) {
            if (peek() != '"') fail("expected object key");
            JsonValue key = parse_string();
            skip_ws();
            if (get() != ':') fail("expected object colon");
            skip_ws();
            value.object.emplace(std::move(key.string), parse_value());
            skip_ws();
            const char separator = get();
            if (separator == '}') return value;
            if (separator != ',') fail("expected object separator");
            skip_ws();
        }
    }

    JsonValue parse_literal(const char* literal, JsonValue value) {
        while (*literal) {
            if (get() != *literal++) fail("bad literal");
        }
        return value;
    }

    JsonValue parse_value() {
        skip_ws();
        const char ch = peek();
        if (ch == '"') return parse_string();
        if (ch == '[') return parse_array();
        if (ch == '{') return parse_object();
        if (ch == '-' || std::isdigit(static_cast<unsigned char>(ch))) return parse_number();
        if (source_.compare(position_, 4, "true") == 0) {
            JsonValue value;
            value.type = JsonValue::Type::Bool;
            value.boolean = true;
            return parse_literal("true", std::move(value));
        }
        if (source_.compare(position_, 5, "false") == 0) {
            JsonValue value;
            value.type = JsonValue::Type::Bool;
            value.boolean = false;
            return parse_literal("false", std::move(value));
        }
        if (source_.compare(position_, 4, "null") == 0) {
            return parse_literal("null", JsonValue{});
        }
        fail("unexpected value");
    }
};

struct MapHandle {
    void* value = nullptr;
    ~MapHandle() { if (value) kv_free(value); }
    MapHandle() = default;
    MapHandle(const MapHandle&) = delete;
    MapHandle& operator=(const MapHandle&) = delete;
    MapHandle(MapHandle&& other) noexcept : value(other.value) { other.value = nullptr; }
    MapHandle& operator=(MapHandle&& other) noexcept {
        if (this == &other) return *this;
        if (value) kv_free(value);
        value = other.value;
        other.value = nullptr;
        return *this;
    }
};

std::string take_owned_json(const char* raw, const char* operation) {
    if (!raw) throw std::runtime_error(std::string(operation) + " returned null");
    std::string text(raw);
    kv_free_string(raw);
    return text;
}

std::string compact_ir_json(void* handle) {
    return take_owned_json(kv_get_ir_json_ex(handle, KV_IR_JSON_COMPACT),
                           "kv_get_ir_json_ex");
}

std::string dry_run_json(void* handle, const std::string& changes) {
    return take_owned_json(kv_edit_dry_run(handle, changes.c_str()), "kv_edit_dry_run");
}

std::string apply_memory_json(void* handle, const std::string& changes) {
    return take_owned_json(kv_edit_apply_to_memory(handle, changes.c_str()),
                           "kv_edit_apply_to_memory");
}

std::string commit_json(void* handle) {
    return take_owned_json(kv_edit_commit(handle), "kv_edit_commit");
}

std::string direct_apply_json(void* handle, const std::string& changes) {
    return take_owned_json(kv_edit_apply(handle, changes.c_str()), "kv_edit_apply");
}

std::string json_escape(const std::string& text) {
    std::ostringstream out;
    out << '"';
    for (unsigned char ch : text) {
        switch (ch) {
            case '"': out << "\\\""; break;
            case '\\': out << "\\\\"; break;
            case '\b': out << "\\b"; break;
            case '\f': out << "\\f"; break;
            case '\n': out << "\\n"; break;
            case '\r': out << "\\r"; break;
            case '\t': out << "\\t"; break;
            default:
                if (ch < 0x20) {
                    out << "\\u" << std::hex << std::setw(4) << std::setfill('0')
                        << static_cast<int>(ch) << std::dec << std::setfill(' ');
                } else {
                    out << static_cast<char>(ch);
                }
                break;
        }
    }
    out << '"';
    return out.str();
}

std::string edit_number(double value) {
    std::ostringstream out;
    out << std::setprecision(15) << value;
    std::string text = out.str();
    return text == "-0" ? "0" : text;
}

std::string ascii_lower_copy(std::string text) {
    for (char& ch : text) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return text;
}

std::string trim_copy(const std::string& text) {
    size_t begin = 0;
    while (begin < text.size() && std::isspace(static_cast<unsigned char>(text[begin]))) ++begin;
    size_t end = text.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1]))) --end;
    return text.substr(begin, end - begin);
}

std::string hash_text(const std::string& text) {
    std::uint64_t hash = 1469598103934665603ull;
    for (unsigned char ch : text) {
        hash ^= ch;
        hash *= 1099511628211ull;
    }
    std::ostringstream out;
    out << std::hex << std::setw(16) << std::setfill('0') << hash;
    return out.str();
}

struct StructureEdit {
    std::string edit_id;
    std::string source_file;
    std::string structure_key;
    std::string raw_text_preview;
    std::string expected_source_hash;
    std::string original_distance_expression;
    int source_line = 0;
    int elements_for_statement = 0;
    double old_distance = 0.0;
    double target_distance = 0.0;
    int increment = 0;
    int selection_score = 99;
};

struct ResolutionChoice {
    std::string resolution_key;
    std::string boundary_token;
    std::string distance_expression;
    bool confirm_environment_mismatch = false;
};

struct ReportFacts {
    bool ok = false;
    bool full_reparse_ok = false;
    int target_distance_match_count = 0;
    int non_target_changed_count = 0;
    int created_distance_block_count = 0;
    int reused_distance_block_count = 0;
    int distance_group_count = 0;
    size_t blocking_error_count = 0;
    size_t resolution_request_count = 0;
    size_t changed_file_count = 0;
    size_t preview_count = 0;
    std::string validation_fingerprint;
};

ReportFacts report_facts(const JsonValue& report) {
    ReportFacts facts;
    const JsonValue& ok = report.at("ok");
    const JsonValue& full = report.at("fullReparseOk");
    facts.ok = ok.is_bool() && ok.boolean;
    facts.full_reparse_ok = full.is_bool() && full.boolean;
    facts.target_distance_match_count = static_cast<int>(report.at("targetDistanceMatchCount").number);
    facts.non_target_changed_count = static_cast<int>(report.at("nonTargetChangedCount").number);
    facts.created_distance_block_count = static_cast<int>(report.at("createdDistanceBlockCount").number);
    facts.reused_distance_block_count = static_cast<int>(report.at("reusedDistanceBlockCount").number);
    facts.distance_group_count = static_cast<int>(report.at("distanceGroupCount").number);
    facts.blocking_error_count = report.at("blockingErrors").array.size();
    facts.resolution_request_count = report.at("resolutionRequests").array.size();
    facts.changed_file_count = report.at("changedFiles").array.size();
    facts.preview_count = report.at("previewSnippets").array.size();
    facts.validation_fingerprint = report.at("validationFingerprint").scalar_text();
    return facts;
}

bool request_affects(const JsonValue& request, const std::string& edit_id) {
    for (const JsonValue& value : request.at("affectedEditIds").array) {
        if (value.scalar_text() == edit_id) return true;
    }
    return false;
}

std::string recommended_boundary_token(const JsonValue& request) {
    for (const JsonValue& boundary : request.at("allowedBoundaries").array) {
        const JsonValue& recommended = boundary.at("recommended");
        if (recommended.is_bool() && recommended.boolean) {
            return boundary.at("token").scalar_text();
        }
    }
    return {};
}

std::string first_blocking_error(const JsonValue& report) {
    const JsonValue& errors = report.at("blockingErrors");
    return errors.array.empty() ? std::string{} : errors.array.front().scalar_text();
}

std::string build_changes_json(
    const std::vector<StructureEdit>& edits,
    const std::map<std::string, ResolutionChoice>& resolutions = {}) {
    std::ostringstream json;
    json << "{\"changes\":[";
    for (size_t i = 0; i < edits.size(); ++i) {
        if (i) json << ",";
        const StructureEdit& edit = edits[i];
        json << "{\"changeId\":" << json_escape("headless-distance-" + std::to_string(i))
             << ",\"editId\":" << json_escape(edit.edit_id)
             << ",\"operation\":\"update\",\"expectedSourceHash\":"
             << json_escape(edit.expected_source_hash)
             << ",\"fieldChanges\":{\"distance\":"
             << json_escape(edit_number(edit.target_distance)) << "}";
        auto resolution = resolutions.find(edit.edit_id);
        if (resolution != resolutions.end()) {
            const ResolutionChoice& choice = resolution->second;
            if (!choice.resolution_key.empty()) {
                json << ",\"distanceResolutionKey\":" << json_escape(choice.resolution_key);
            }
            if (!choice.boundary_token.empty()) {
                json << ",\"distanceBoundaryToken\":" << json_escape(choice.boundary_token);
            }
            if (!choice.distance_expression.empty()) {
                json << ",\"distanceExpression\":" << json_escape(choice.distance_expression);
            }
            if (choice.confirm_environment_mismatch) {
                json << ",\"confirmEnvironmentMismatch\":true";
            }
        }
        json << "}";
    }
    json << "]}";
    return json.str();
}

std::string build_field_updates_json(const std::vector<StructureEdit>& edits,
                                     const std::string& field,
                                     const std::vector<std::string>& values) {
    if (edits.size() != values.size()) {
        throw std::runtime_error("field-update fixture input sizes differ");
    }
    std::ostringstream json;
    json << "{\"changes\":[";
    for (size_t i = 0; i < edits.size(); ++i) {
        if (i) json << ",";
        json << "{\"changeId\":"
             << json_escape("headless-field-update-" + std::to_string(i))
             << ",\"editId\":" << json_escape(edits[i].edit_id)
             << ",\"operation\":\"update\",\"expectedSourceHash\":"
             << json_escape(edits[i].expected_source_hash)
             << ",\"fieldChanges\":{" << json_escape(field) << ":"
             << json_escape(values[i]) << "}}";
    }
    json << "]}";
    return json.str();
}

std::string build_single_field_update_json(const StructureEdit& edit,
                                           const std::string& field,
                                           const std::string& value) {
    return build_field_updates_json({edit}, field, {value});
}

std::vector<StructureEdit> structure_rows_from_ir(const std::string& ir_json) {
    JsonValue root = JsonParser(ir_json).parse();
    const JsonValue& rows = root.at("structure").at("data");
    if (!rows.is_array()) throw std::runtime_error("IR has no structure.data array");
    std::vector<StructureEdit> result;
    result.reserve(rows.array.size());
    for (const JsonValue& row : rows.array) {
        if (!row.is_object() || !row.at("distance").is_number()) continue;
        const JsonValue& source = row.at("source");
        StructureEdit edit;
        edit.edit_id = row.at("editId").scalar_text();
        edit.source_file = source.at("filePath").scalar_text();
        edit.source_line = static_cast<int>(source.at("line").number);
        edit.raw_text_preview = source.at("rawTextPreview").scalar_text();
        edit.structure_key = row.at("structureKey").scalar_text();
        edit.old_distance = row.at("distance").number;
        if (!edit.edit_id.empty() && !edit.source_file.empty()) result.push_back(std::move(edit));
    }
    std::stable_sort(result.begin(), result.end(), [](const StructureEdit& a,
                                                       const StructureEdit& b) {
        if (a.old_distance != b.old_distance) return a.old_distance < b.old_distance;
        if (a.source_file != b.source_file) return a.source_file < b.source_file;
        return a.source_line < b.source_line;
    });
    return result;
}

std::vector<StructureEdit> station_put_rows_from_ir(const std::string& ir_json) {
    JsonValue root = JsonParser(ir_json).parse();
    const JsonValue& rows = root.at("station").at("put");
    if (!rows.is_array()) throw std::runtime_error("IR has no station.put array");
    std::vector<StructureEdit> result;
    result.reserve(rows.array.size());
    for (const JsonValue& row : rows.array) {
        if (!row.is_object() || !row.at("distance").is_number()) continue;
        const JsonValue& source = row.at("source");
        StructureEdit edit;
        edit.edit_id = row.at("editId").scalar_text();
        edit.source_file = source.at("filePath").scalar_text();
        edit.source_line = static_cast<int>(source.at("line").number);
        edit.raw_text_preview = source.at("rawTextPreview").scalar_text();
        edit.structure_key = row.at("stationKey").scalar_text();
        edit.old_distance = row.at("distance").number;
        if (!edit.edit_id.empty() && !edit.source_file.empty()) result.push_back(std::move(edit));
    }
    std::stable_sort(result.begin(), result.end(), [](const StructureEdit& a,
                                                       const StructureEdit& b) {
        if (a.old_distance != b.old_distance) return a.old_distance < b.old_distance;
        return a.source_line < b.source_line;
    });
    return result;
}

bool populate_target_info(void* handle, StructureEdit& edit, std::string& error) {
    try {
        std::string text = take_owned_json(
            kv_get_edit_target_info(handle, edit.edit_id.c_str()),
            "kv_get_edit_target_info");
        JsonValue root = JsonParser(text).parse();
        if (!root.at("ok").is_bool() || !root.at("ok").boolean) {
            error = root.at("error").scalar_text();
            return false;
        }
        edit.expected_source_hash = root.at("expectedSourceHash").scalar_text();
        edit.original_distance_expression = root.at("distanceExpression").scalar_text();
        edit.elements_for_statement = static_cast<int>(root.at("elementsForStatement").number);
        if (edit.expected_source_hash.empty() || edit.elements_for_statement != 1) {
            error = "target is not a unique source-backed editable statement";
            return false;
        }
        return true;
    } catch (const std::exception& e) {
        error = e.what();
        return false;
    }
}

bool candidate_has_recommended_resolution(const JsonValue& report,
                                          const StructureEdit& candidate,
                                          int& score) {
    score = 99;
    if (!report.at("blockingErrors").array.empty()) return false;
    ReportFacts facts = report_facts(report);
    if (facts.ok && facts.full_reparse_ok && facts.target_distance_match_count == 1 &&
        facts.non_target_changed_count == 0) {
        score = 0;
        return true;
    }
    for (const JsonValue& request : report.at("resolutionRequests").array) {
        if (!request_affects(request, candidate.edit_id) ||
            recommended_boundary_token(request).empty()) {
            continue;
        }
        const std::string reason = request.at("reason").scalar_text();
        score = reason == "variableHasMultipleContextValues" ? 1 : 2;
        return true;
    }
    return false;
}

bool choose_candidate_target(void* handle, StructureEdit& candidate,
                             int& dry_run_attempts, std::string& error) {
    if (!populate_target_info(handle, candidate, error)) return false;
    bool accepted = false;
    StructureEdit best = candidate;
    int best_score = 99;
    for (int increment : {1, 2}) {
        StructureEdit trial = candidate;
        trial.increment = increment;
        trial.target_distance = trial.old_distance + static_cast<double>(increment);
        std::string report_text;
        try {
            ++dry_run_attempts;
            report_text = dry_run_json(handle, build_changes_json({trial}));
            JsonValue report = JsonParser(report_text).parse();
            int score = 99;
            if (candidate_has_recommended_resolution(report, trial, score) && score < best_score) {
                best = std::move(trial);
                best_score = score;
                accepted = true;
                if (score == 0) break;
            }
        } catch (const std::exception& e) {
            error = e.what();
        }
    }
    if (!accepted) return false;
    best.selection_score = best_score;
    candidate = std::move(best);
    return true;
}

bool inject_report_resolutions(
    const JsonValue& report,
    const std::vector<StructureEdit>& edits,
    std::map<std::string, ResolutionChoice>& resolutions,
    int& variable_expression_resolution_count,
    std::string& error) {
    std::map<std::string, const StructureEdit*> by_id;
    for (const StructureEdit& edit : edits) by_id[edit.edit_id] = &edit;
    bool added = false;
    for (const JsonValue& request : report.at("resolutionRequests").array) {
        const std::string key = request.at("resolutionKey").scalar_text();
        const std::string reason = request.at("reason").scalar_text();
        const std::string boundary = recommended_boundary_token(request);
        if (key.empty() || boundary.empty()) {
            error = "resolution request has no recommended parser boundary: " + reason;
            return false;
        }
        for (const JsonValue& affected : request.at("affectedEditIds").array) {
            const std::string edit_id = affected.scalar_text();
            auto target = by_id.find(edit_id);
            if (target == by_id.end()) {
                error = "resolution request references an unselected editId";
                return false;
            }
            ResolutionChoice& choice = resolutions[edit_id];
            choice.resolution_key = key;
            choice.boundary_token = boundary;
            const bool manual_numeric_expression =
                reason == "variableHasMultipleContextValues" ||
                reason == "distanceExpressionRequiresManualEdit";
            if (manual_numeric_expression) {
                choice.distance_expression = edit_number(target->second->target_distance);
                ++variable_expression_resolution_count;
            }
            const JsonValue& can_confirm = request.at("canConfirmReuse");
            choice.confirm_environment_mismatch = can_confirm.is_bool() && can_confirm.boolean;
            added = true;
        }
    }
    return added;
}

bool preview_has_local_wrapper(const JsonValue& report,
                               const std::vector<StructureEdit>& edits) {
    for (const JsonValue& warning : report.at("warnings").array) {
        if (warning.scalar_text().find("preserves the original distance expression") !=
            std::string::npos) {
            return true;
        }
    }
    for (const JsonValue& preview : report.at("previewSnippets").array) {
        std::string after = preview.at("after").scalar_text();
        std::vector<std::string> lines;
        size_t start = 0;
        while (start <= after.size()) {
            size_t end = after.find('\n', start);
            std::string line = trim_copy(after.substr(
                start, end == std::string::npos ? std::string::npos : end - start));
            if (!line.empty()) lines.push_back(std::move(line));
            if (end == std::string::npos) break;
            start = end + 1;
        }
        if (lines.size() < 3) continue;
        bool has_structure = std::any_of(lines.begin() + 1, lines.end() - 1,
                                         [](const std::string& line) {
                                             return line.find("Structure[") != std::string::npos;
                                         });
        if (!has_structure) continue;
        for (const StructureEdit& edit : edits) {
            if (!edit.original_distance_expression.empty() &&
                lines.back() == trim_copy(edit.original_distance_expression) + ";") {
                return true;
            }
        }
    }
    return false;
}

size_t count_preview_occurrences(const JsonValue& report, const std::string& needle) {
    size_t count = 0;
    for (const JsonValue& preview : report.at("previewSnippets").array) {
        const std::string after = preview.at("after").scalar_text();
        size_t position = 0;
        while ((position = after.find(needle, position)) != std::string::npos) {
            ++count;
            position += needle.size();
        }
    }
    return count;
}

bool distances_are_separated(const std::vector<StructureEdit>& selected,
                             double distance, double minimum_spacing) {
    for (const StructureEdit& edit : selected) {
        if (std::fabs(edit.old_distance - distance) < minimum_spacing) return false;
    }
    return true;
}

std::vector<StructureEdit> select_real_map_edits(
    void* handle, const std::vector<StructureEdit>& rows,
    int& dry_run_attempts, int& sig_context_count) {
    if (rows.empty()) throw std::runtime_error("real map has no editable structure.data rows");
    std::vector<StructureEdit> selected;
    std::set<std::string> selected_files;
    std::set<std::string> attempted_ids;
    std::string last_error;

    auto try_row = [&](const StructureEdit& source, bool require_sig,
                       double minimum_spacing) {
        if (selected.size() >= 5 || !attempted_ids.insert(source.edit_id).second) return false;
        const std::string file_key = ascii_lower_copy(source.source_file);
        if (selected_files.find(file_key) != selected_files.end()) return false;
        if (!distances_are_separated(selected, source.old_distance, minimum_spacing)) return false;
        StructureEdit candidate = source;
        if (!choose_candidate_target(handle, candidate, dry_run_attempts, last_error)) return false;
        const bool has_sig = candidate.original_distance_expression.find("$sig") != std::string::npos;
        if (require_sig && !has_sig) return false;
        selected_files.insert(file_key);
        if (has_sig) ++sig_context_count;
        selected.push_back(std::move(candidate));
        return true;
    };

    for (const char* file_tail : {"beacons_common1.txt", "beacons_common2.txt"}) {
        int attempts_for_file = 0;
        const std::string wanted = ascii_lower_copy(file_tail);
        for (const StructureEdit& row : rows) {
            if (ascii_lower_copy(row.source_file).find(wanted) == std::string::npos) continue;
            if (++attempts_for_file > 16) break;
            if (try_row(row, true, 100.0)) break;
        }
    }

    std::vector<size_t> pool;
    std::set<size_t> pooled;
    const double minimum = rows.front().old_distance;
    const double maximum = rows.back().old_distance;
    for (double fraction : {0.10, 0.30, 0.50, 0.70, 0.90}) {
        const double wanted = minimum + (maximum - minimum) * fraction;
        std::vector<size_t> indices(rows.size());
        for (size_t i = 0; i < rows.size(); ++i) indices[i] = i;
        std::stable_sort(indices.begin(), indices.end(), [&](size_t lhs, size_t rhs) {
            return std::fabs(rows[lhs].old_distance - wanted) <
                   std::fabs(rows[rhs].old_distance - wanted);
        });
        const size_t limit = std::min<size_t>(indices.size(), 16);
        for (size_t i = 0; i < limit; ++i) {
            if (pooled.insert(indices[i]).second) pool.push_back(indices[i]);
        }
    }
    const size_t stride = std::max<size_t>(1, rows.size() / 64);
    for (size_t i = 0; i < rows.size(); i += stride) {
        if (pooled.insert(i).second) pool.push_back(i);
    }

    const double preferred_spacing = std::max(500.0, (maximum - minimum) / 30.0);
    int general_attempts = 0;
    for (size_t index : pool) {
        if (selected.size() >= 5 || general_attempts >= 40) break;
        ++general_attempts;
        try_row(rows[index], false, preferred_spacing);
    }
    for (size_t index : pool) {
        if (selected.size() >= 5 || general_attempts >= 80) break;
        ++general_attempts;
        try_row(rows[index], false, 100.0);
    }
    if (selected.size() != 5) {
        throw std::runtime_error("could not select five resolvable cross-file Structure distance edits" +
                                 (last_error.empty() ? std::string{} : ": " + last_error));
    }
    return selected;
}

struct FixtureFacts {
    bool increasing_same_target_one_block = false;
    bool canonical_identical_same_target_one_block = false;
    bool terminal_unique_target_reused = false;
    bool repeated_include_partial_blocked = false;
    bool repeated_include_all_targets_coalesced = false;
    bool unordered_requires_resolution_without_patch = false;
    bool variable_environment_change_blocked = false;
    bool derived_station_collision_blocked = false;
    bool staged_variable_resolution_succeeded = false;
    bool direct_apply_handle_advanced = false;
    bool direct_apply_second_update_succeeded = false;
    bool transactional_apply_failure_preserved_files = false;
    bool transactional_two_file_apply_succeeded = false;
    int increasing_target_match_count = 0;
    int unordered_resolution_count = 0;
    int environment_blocking_error_count = 0;
    int staged_variable_resolution_count = 0;
    int derived_state_blocking_error_count = 0;
    std::string error;

    bool passed() const {
        return increasing_same_target_one_block &&
               canonical_identical_same_target_one_block &&
               terminal_unique_target_reused &&
               repeated_include_partial_blocked &&
               repeated_include_all_targets_coalesced &&
               unordered_requires_resolution_without_patch &&
               variable_environment_change_blocked &&
               derived_station_collision_blocked &&
               staged_variable_resolution_succeeded &&
               direct_apply_handle_advanced &&
               direct_apply_second_update_succeeded &&
               transactional_apply_failure_preserved_files &&
               transactional_two_file_apply_succeeded && error.empty();
    }
};

struct TempFixtureDirectory {
    std::filesystem::path path;
    ~TempFixtureDirectory() {
        std::error_code error;
        if (!path.empty()) std::filesystem::remove_all(path, error);
    }
};

void write_fixture_file(const std::filesystem::path& path, const std::string& text) {
    std::ofstream file(path, std::ios::out | std::ios::trunc | std::ios::binary);
    if (!file) throw std::runtime_error("failed to create fixture: " + wide_to_utf8(path.wstring()));
    file.write(text.data(), static_cast<std::streamsize>(text.size()));
    if (!file) throw std::runtime_error("failed to write fixture: " + wide_to_utf8(path.wstring()));
}

std::string read_fixture_file(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::in | std::ios::binary);
    if (!file) throw std::runtime_error("failed to read fixture: " + wide_to_utf8(path.wstring()));
    return std::string(std::istreambuf_iterator<char>(file),
                       std::istreambuf_iterator<char>());
}

MapHandle load_edit_map(const std::filesystem::path& path, double unit_distance) {
    MapHandle handle;
    const std::string utf8_path = wide_to_utf8(path.wstring());
    handle.value = kv_load_map_ex(utf8_path.c_str(), unit_distance, KV_LOAD_EDIT_METADATA);
    if (!handle.value) {
        const char* error = kv_get_last_error();
        throw std::runtime_error(error ? error : "fixture map load failed");
    }
    return handle;
}

bool report_has_reason(const JsonValue& report, const std::string& reason) {
    for (const JsonValue& request : report.at("resolutionRequests").array) {
        if (request.at("reason").scalar_text() == reason) return true;
    }
    return false;
}

FixtureFacts run_fixture_checks(double unit_distance) {
    FixtureFacts facts;
    try {
        TempFixtureDirectory temp;
        temp.path = std::filesystem::temp_directory_path() /
            ("komapedit-distance-batch-" + std::to_string(GetCurrentProcessId()) + "-" +
             std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(temp.path);
        write_fixture_file(temp.path / "structures.csv",
                           "BveTs Structure List 1.00:utf-8\n"
                           "pole,pole.csv\n");

        const std::filesystem::path increasing_map = temp.path / "increasing.txt";
        write_fixture_file(increasing_map,
            "BveTs Map 2.02:utf-8\n"
            "0;\n"
            "Structure.Load('structures.csv');\n"
            "50;\n"
            "Structure['pole'].Put('',1,0,0,,,,,);\n"
            "100;\n"
            "Structure['pole'].Put('',1,0,0,,,,,);\n"
            "200;\n");
        {
            MapHandle handle = load_edit_map(increasing_map, unit_distance);
            std::vector<StructureEdit> edits = structure_rows_from_ir(compact_ir_json(handle.value));
            if (edits.size() != 2) throw std::runtime_error("increasing fixture did not yield two structures");
            for (StructureEdit& edit : edits) {
                std::string error;
                if (!populate_target_info(handle.value, edit, error)) throw std::runtime_error(error);
                edit.target_distance = 150.0;
                edit.increment = static_cast<int>(edit.target_distance - edit.old_distance);
            }
            JsonValue report = JsonParser(dry_run_json(handle.value, build_changes_json(edits))).parse();
            ReportFacts summary = report_facts(report);
            facts.increasing_target_match_count = summary.target_distance_match_count;
            facts.increasing_same_target_one_block =
                summary.ok && summary.full_reparse_ok &&
                summary.target_distance_match_count == 2 &&
                summary.non_target_changed_count == 0 &&
                summary.distance_group_count == 1 &&
                summary.created_distance_block_count == 1 &&
                count_preview_occurrences(report, "150;") == 1 &&
                !preview_has_local_wrapper(report, edits);
            facts.canonical_identical_same_target_one_block =
                facts.increasing_same_target_one_block;
        }

        const std::filesystem::path terminal_reuse_map =
            temp.path / "terminal_reuse.txt";
        write_fixture_file(terminal_reuse_map,
            "BveTs Map 2.02:utf-8\n"
            "0;\n"
            "Structure.Load('structures.csv');\n"
            "50;\n"
            "Structure['pole'].Put('',1,0,0,,,,,);\n"
            "100;\n"
            "Structure['pole'].Put('',2,0,0,,,,,);\n");
        {
            MapHandle handle = load_edit_map(terminal_reuse_map, unit_distance);
            std::vector<StructureEdit> edits =
                structure_rows_from_ir(compact_ir_json(handle.value));
            if (edits.size() != 2) {
                throw std::runtime_error(
                    "terminal reuse fixture did not yield two structures");
            }
            StructureEdit target = edits.front();
            std::string error;
            if (!populate_target_info(handle.value, target, error)) {
                throw std::runtime_error(error);
            }
            target.target_distance = 100.0;
            JsonValue report = JsonParser(
                dry_run_json(handle.value, build_changes_json({target}))).parse();
            ReportFacts summary = report_facts(report);
            facts.terminal_unique_target_reused =
                summary.ok && summary.full_reparse_ok &&
                summary.target_distance_match_count == 1 &&
                summary.non_target_changed_count == 0 &&
                summary.distance_group_count == 1 &&
                summary.created_distance_block_count == 0 &&
                summary.reused_distance_block_count == 1 &&
                !preview_has_local_wrapper(report, {target});
        }

        const std::filesystem::path repeated_include_map =
            temp.path / "repeated_include.txt";
        const std::filesystem::path repeated_include_child =
            temp.path / "repeated_include_child.txt";
        write_fixture_file(repeated_include_map,
            "BveTs Map 2.02:utf-8\n"
            "0;\n"
            "Structure.Load('structures.csv');\n"
            "include 'repeated_include_child.txt';\n"
            "100;\n"
            "include 'repeated_include_child.txt';\n"
            "200;\n");
        write_fixture_file(repeated_include_child,
            "BveTs Map 2.02:utf-8\n"
            "$base=distance;\n"
            "$base+10;\n"
            "Structure['pole'].Put('',1,0,0,,,,,);\n"
            "$base+30;\n");
        {
            MapHandle handle = load_edit_map(repeated_include_map, unit_distance);
            std::vector<StructureEdit> edits =
                structure_rows_from_ir(compact_ir_json(handle.value));
            if (edits.size() != 2) {
                throw std::runtime_error(
                    "repeated Include fixture did not yield two structures");
            }
            for (StructureEdit& edit : edits) {
                std::string error;
                if (!populate_target_info(handle.value, edit, error)) {
                    throw std::runtime_error(error);
                }
                edit.target_distance = edit.old_distance + 5.0;
            }

            JsonValue partial = JsonParser(dry_run_json(
                handle.value, build_changes_json({edits.front()}))).parse();
            ReportFacts partial_summary = report_facts(partial);
            facts.repeated_include_partial_blocked =
                !partial_summary.ok && partial_summary.resolution_request_count > 0 &&
                partial_summary.changed_file_count == 0 &&
                report_has_reason(
                    partial, "physicalSourceHasIncompatibleIncludeContexts");

            JsonValue complete = JsonParser(
                dry_run_json(handle.value, build_changes_json(edits))).parse();
            ReportFacts complete_summary = report_facts(complete);
            facts.repeated_include_all_targets_coalesced =
                complete_summary.ok && complete_summary.full_reparse_ok &&
                complete_summary.target_distance_match_count == 2 &&
                complete_summary.non_target_changed_count == 0 &&
                complete_summary.distance_group_count == 2 &&
                complete_summary.created_distance_block_count == 1 &&
                !preview_has_local_wrapper(complete, edits);
        }

        const std::filesystem::path unordered_map = temp.path / "unordered.txt";
        write_fixture_file(unordered_map,
            "BveTs Map 2.02:utf-8\n"
            "0;\n"
            "Structure.Load('structures.csv');\n"
            "100;\n"
            "Structure['pole'].Put('',1,0,0,,,,,);\n"
            "50;\n"
            "200;\n");
        {
            MapHandle handle = load_edit_map(unordered_map, unit_distance);
            std::vector<StructureEdit> edits = structure_rows_from_ir(compact_ir_json(handle.value));
            if (edits.size() != 1) throw std::runtime_error("unordered fixture did not yield one structure");
            std::string error;
            if (!populate_target_info(handle.value, edits.front(), error)) throw std::runtime_error(error);
            edits.front().target_distance = 75.0;
            JsonValue report = JsonParser(dry_run_json(handle.value, build_changes_json(edits))).parse();
            ReportFacts summary = report_facts(report);
            facts.unordered_resolution_count = static_cast<int>(summary.resolution_request_count);
            facts.unordered_requires_resolution_without_patch =
                !summary.ok && summary.resolution_request_count > 0 &&
                summary.changed_file_count == 0 && summary.preview_count == 0 &&
                report_has_reason(report, "ambiguousSourceSection");
        }

        const std::filesystem::path environment_map = temp.path / "environment.txt";
        write_fixture_file(environment_map,
            "BveTs Map 2.02:utf-8\n"
            "0;\n"
            "Structure.Load('structures.csv');\n"
            "$x=1;\n"
            "50;\n"
            "Structure['pole'].Put('',$x,0,0,,,,,);\n"
            "100;\n"
            "$x=2;\n"
            "200;\n");
        {
            MapHandle handle = load_edit_map(environment_map, unit_distance);
            std::vector<StructureEdit> edits = structure_rows_from_ir(compact_ir_json(handle.value));
            if (edits.size() != 1) throw std::runtime_error("environment fixture did not yield one structure");
            std::string error;
            if (!populate_target_info(handle.value, edits.front(), error)) throw std::runtime_error(error);
            edits.front().target_distance = 150.0;
            JsonValue initial = JsonParser(dry_run_json(handle.value, build_changes_json(edits))).parse();
            std::map<std::string, ResolutionChoice> resolutions;
            int expression_count = 0;
            if (!report_has_reason(initial, "variableHasMultipleContextValues") ||
                !inject_report_resolutions(initial, edits, resolutions, expression_count, error)) {
                throw std::runtime_error(error.empty()
                    ? "environment fixture did not request the expected variable resolution"
                    : error);
            }
            JsonValue forced = JsonParser(
                dry_run_json(handle.value, build_changes_json(edits, resolutions))).parse();
            ReportFacts summary = report_facts(forced);
            facts.environment_blocking_error_count = static_cast<int>(summary.blocking_error_count);
            facts.variable_environment_change_blocked =
                !summary.ok && !summary.full_reparse_ok &&
                (summary.blocking_error_count > 0 ||
                 (summary.resolution_request_count > 0 &&
                  (report_has_reason(forced, "variableHasMultipleContextValues") ||
                   report_has_reason(forced, "incompatibleEvaluationEnvironment"))));
        }

        const std::filesystem::path station_collision_map =
            temp.path / "station_collision.txt";
        write_fixture_file(station_collision_map,
            "BveTs Map 2.02:utf-8\n"
            "0;\n"
            "Station['A'].Put();\n"
            "100;\n"
            "Station['B'].Put();\n"
            "200;\n");
        {
            MapHandle handle = load_edit_map(station_collision_map, unit_distance);
            std::vector<StructureEdit> edits =
                station_put_rows_from_ir(compact_ir_json(handle.value));
            if (edits.size() != 2) {
                throw std::runtime_error(
                    "station collision fixture did not yield two station puts");
            }
            StructureEdit target = edits.front();
            std::string error;
            if (!populate_target_info(handle.value, target, error)) {
                throw std::runtime_error(error);
            }
            target.target_distance = 100.0;
            JsonValue report = JsonParser(
                dry_run_json(handle.value, build_changes_json({target}))).parse();
            ReportFacts summary = report_facts(report);
            facts.derived_state_blocking_error_count =
                static_cast<int>(summary.blocking_error_count);
            facts.derived_station_collision_blocked =
                !summary.ok && !summary.full_reparse_ok &&
                summary.blocking_error_count > 0 && summary.resolution_request_count == 0;
        }

        const std::filesystem::path transaction_entry_map =
            temp.path / "transaction_a_entry.txt";
        const std::filesystem::path transaction_child_map =
            temp.path / "transaction_z_child.txt";
        write_fixture_file(transaction_entry_map,
            "BveTs Map 2.02:utf-8\n"
            "0;\n"
            "Structure.Load('structures.csv');\n"
            "50;\n"
            "Structure['pole'].Put('',1,0,0,,,,,);\n"
            "include 'transaction_z_child.txt';\n"
            "200;\n");
        write_fixture_file(transaction_child_map,
            "BveTs Map 2.02:utf-8\n"
            "100;\n"
            "Structure['pole'].Put('',2,0,0,,,,,);\n");
        {
            const std::string entry_before = read_fixture_file(transaction_entry_map);
            const std::string child_before = read_fixture_file(transaction_child_map);
            MapHandle handle = load_edit_map(transaction_entry_map, unit_distance);
            std::vector<StructureEdit> edits =
                structure_rows_from_ir(compact_ir_json(handle.value));
            if (edits.size() != 2) {
                throw std::runtime_error(
                    "transaction fixture did not yield two cross-file structures");
            }
            for (StructureEdit& edit : edits) {
                std::string error;
                if (!populate_target_info(handle.value, edit, error)) {
                    throw std::runtime_error(error);
                }
            }
            HANDLE locked_child = CreateFileW(
                transaction_child_map.wstring().c_str(), GENERIC_READ, FILE_SHARE_READ,
                nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (locked_child == INVALID_HANDLE_VALUE) {
                throw std::runtime_error(
                    "transaction fixture could not lock the second source file");
            }
            JsonValue report;
            try {
                report = JsonParser(direct_apply_json(
                    handle.value,
                    build_field_updates_json(edits, "x", {"11", "22"}))).parse();
            } catch (...) {
                CloseHandle(locked_child);
                throw;
            }
            CloseHandle(locked_child);
            ReportFacts summary = report_facts(report);
            facts.transactional_apply_failure_preserved_files =
                !summary.ok && summary.blocking_error_count > 0 &&
                read_fixture_file(transaction_entry_map) == entry_before &&
                read_fixture_file(transaction_child_map) == child_before;

            JsonValue success_report = JsonParser(direct_apply_json(
                handle.value,
                build_field_updates_json(edits, "x", {"11", "22"}))).parse();
            ReportFacts success_summary = report_facts(success_report);
            bool transaction_artifacts_remain = false;
            for (const auto& directory_entry :
                 std::filesystem::directory_iterator(temp.path)) {
                if (wide_to_utf8(directory_entry.path().filename().wstring())
                        .find(".komapedit.") != std::string::npos) {
                    transaction_artifacts_remain = true;
                    break;
                }
            }
            facts.transactional_two_file_apply_succeeded =
                success_summary.ok && success_summary.full_reparse_ok &&
                read_fixture_file(transaction_entry_map) != entry_before &&
                read_fixture_file(transaction_child_map) != child_before &&
                !transaction_artifacts_remain;
        }

        const std::filesystem::path staged_variable_map =
            temp.path / "staged_variable.txt";
        write_fixture_file(staged_variable_map,
            "BveTs Map 2.02:utf-8\n"
            "0;\n"
            "Structure.Load('structures.csv');\n"
            "100;\n"
            "$sig=distance;\n"
            "$sig-30;\n"
            "Structure['pole'].Put('',1,0,0,,,,,);\n"
            "200;\n"
            "$sig=distance;\n"
            "$sig-30;\n"
            "Structure['pole'].Put('',2,0,0,,,,,);\n"
            "300;\n");
        {
            MapHandle handle = load_edit_map(staged_variable_map, unit_distance);
            std::vector<StructureEdit> rows =
                structure_rows_from_ir(compact_ir_json(handle.value));
            auto selected = std::find_if(rows.begin(), rows.end(), [](const StructureEdit& row) {
                return std::fabs(row.old_distance - 70.0) < 1e-9;
            });
            if (selected == rows.end()) {
                throw std::runtime_error(
                    "staged variable fixture did not yield the expected source row");
            }
            std::vector<StructureEdit> edits{*selected};
            std::string error;
            if (!populate_target_info(handle.value, edits.front(), error)) {
                throw std::runtime_error(error);
            }
            edits.front().target_distance = 75.0;

            JsonValue section_request = JsonParser(
                dry_run_json(handle.value, build_changes_json(edits))).parse();
            if (!report_has_reason(section_request, "ambiguousSourceSection")) {
                throw std::runtime_error(
                    "staged variable fixture did not first request a source boundary");
            }
            std::map<std::string, ResolutionChoice> resolutions;
            int expression_count = 0;
            if (!inject_report_resolutions(section_request, edits, resolutions,
                                           expression_count, error)) {
                throw std::runtime_error(error.empty()
                    ? "staged variable fixture could not select a parser boundary"
                    : error);
            }
            JsonValue expression_request = JsonParser(
                dry_run_json(handle.value, build_changes_json(edits, resolutions))).parse();
            const bool requested_manual_expression =
                report_has_reason(expression_request, "variableHasMultipleContextValues") ||
                report_has_reason(expression_request, "distanceExpressionRequiresManualEdit");
            facts.staged_variable_resolution_count = static_cast<int>(
                expression_request.at("resolutionRequests").array.size());
            if (!requested_manual_expression ||
                !inject_report_resolutions(expression_request, edits, resolutions,
                                           expression_count, error)) {
                throw std::runtime_error(error.empty()
                    ? "staged variable fixture skipped the manual expression request"
                    : error);
            }
            const auto choice = resolutions.find(edits.front().edit_id);
            if (choice == resolutions.end() ||
                choice->second.distance_expression != edit_number(edits.front().target_distance)) {
                throw std::runtime_error(
                    "staged variable fixture did not inject the numeric distance expression");
            }
            JsonValue resolved = JsonParser(
                dry_run_json(handle.value, build_changes_json(edits, resolutions))).parse();
            ReportFacts summary = report_facts(resolved);
            facts.staged_variable_resolution_succeeded =
                summary.ok && summary.full_reparse_ok &&
                summary.target_distance_match_count == 1 &&
                summary.non_target_changed_count == 0 &&
                summary.resolution_request_count == 0 && expression_count > 0 &&
                !preview_has_local_wrapper(resolved, edits);
        }

        const std::filesystem::path direct_state_map = temp.path / "direct_state.txt";
        write_fixture_file(direct_state_map,
            "BveTs Map 2.02:utf-8\n"
            "0;\n"
            "Structure.Load('structures.csv');\n"
            "100;\n"
            "Structure['pole'].Put('',1,0,0,,,,,);\n"
            "200;\n");
        {
            MapHandle handle = load_edit_map(direct_state_map, unit_distance);
            std::vector<StructureEdit> edits =
                structure_rows_from_ir(compact_ir_json(handle.value));
            if (edits.size() != 1) {
                throw std::runtime_error(
                    "direct apply fixture did not yield one structure");
            }
            std::string error;
            if (!populate_target_info(handle.value, edits.front(), error)) {
                throw std::runtime_error(error);
            }
            JsonValue first_report = JsonParser(direct_apply_json(
                handle.value,
                build_single_field_update_json(edits.front(), "x", "12"))).parse();
            ReportFacts first_summary = report_facts(first_report);
            if (first_summary.ok && first_summary.full_reparse_ok) {
                const std::string first_ir = compact_ir_json(handle.value);
                JsonValue first_root = JsonParser(first_ir).parse();
                std::vector<StructureEdit> first_rows = structure_rows_from_ir(first_ir);
                const JsonValue& first_data = first_root.at("structure").at("data");
                const double first_x = first_data.array.empty()
                    ? std::numeric_limits<double>::quiet_NaN()
                    : first_data.array.front().at("x").number;
                facts.direct_apply_handle_advanced =
                    first_rows.size() == 1 && std::fabs(first_x - 12.0) < 1e-9;

                if (first_rows.size() == 1 &&
                    populate_target_info(handle.value, first_rows.front(), error)) {
                    JsonValue second_report = JsonParser(direct_apply_json(
                        handle.value,
                        build_single_field_update_json(first_rows.front(), "x", "13"))).parse();
                    ReportFacts second_summary = report_facts(second_report);
                    const std::string second_ir = compact_ir_json(handle.value);
                    JsonValue second_root = JsonParser(second_ir).parse();
                    const JsonValue& second_data = second_root.at("structure").at("data");
                    const double second_x = second_data.array.empty()
                        ? std::numeric_limits<double>::quiet_NaN()
                        : second_data.array.front().at("x").number;
                    facts.direct_apply_second_update_succeeded =
                        second_summary.ok && second_summary.full_reparse_ok &&
                        std::fabs(second_x - 13.0) < 1e-9;
                }
            }
        }
    } catch (const std::exception& e) {
        facts.error = e.what();
    }
    return facts;
}

struct BatchRunFacts {
    std::string path;
    bool commit_requested = false;
    int candidate_dry_run_attempts = 0;
    int selected_sig_context_count = 0;
    int initial_resolution_request_count = 0;
    std::vector<std::string> initial_resolution_reasons;
    int resolution_round_count = 0;
    int variable_expression_resolution_count = 0;
    bool final_dry_run_ok = false;
    bool apply_to_memory_ok = false;
    bool full_reparse_ok = false;
    int target_distance_match_count = 0;
    int non_target_changed_count = 0;
    int distance_group_count = 0;
    int created_distance_block_count = 0;
    int reused_distance_block_count = 0;
    bool no_local_wrapper = false;
    bool reset_ok = false;
    bool reset_ir_fingerprint_restored = false;
    std::string initial_ir_fingerprint;
    std::string reset_ir_fingerprint;
    bool commit_attempted = false;
    bool commit_ok = false;
    std::vector<StructureEdit> selected;
    FixtureFacts fixtures;
    std::string error;

    bool passed() const {
        return error.empty() && selected.size() == 5 && selected_sig_context_count >= 2 &&
               final_dry_run_ok && apply_to_memory_ok && full_reparse_ok &&
               target_distance_match_count == 5 && non_target_changed_count == 0 &&
               distance_group_count == 5 &&
               created_distance_block_count + reused_distance_block_count == distance_group_count &&
               no_local_wrapper && reset_ok && reset_ir_fingerprint_restored &&
               fixtures.passed() && (!commit_requested || (commit_attempted && commit_ok));
    }
};

void write_batch_result(std::ostream& out, const BatchRunFacts& facts) {
    auto boolean = [](bool value) { return value ? "true" : "false"; };
    out << "{\n"
        << "  \"command\": \"debug-headless-distance-edit-batch\",\n"
        << "  \"path\": " << json_escape(facts.path) << ",\n"
        << "  \"commitRequested\": " << boolean(facts.commit_requested) << ",\n"
        << "  \"candidateDryRunAttempts\": " << facts.candidate_dry_run_attempts << ",\n"
        << "  \"selectedCount\": " << facts.selected.size() << ",\n"
        << "  \"selectedSigContextCount\": " << facts.selected_sig_context_count << ",\n"
        << "  \"initialResolutionRequestCount\": " << facts.initial_resolution_request_count << ",\n"
        << "  \"initialResolutionReasons\": [";
    for (size_t i = 0; i < facts.initial_resolution_reasons.size(); ++i) {
        if (i) out << ",";
        out << json_escape(facts.initial_resolution_reasons[i]);
    }
    out << "],\n"
        << "  \"resolutionRoundCount\": " << facts.resolution_round_count << ",\n"
        << "  \"variableExpressionResolutionCount\": "
        << facts.variable_expression_resolution_count << ",\n"
        << "  \"finalDryRunOk\": " << boolean(facts.final_dry_run_ok) << ",\n"
        << "  \"applyToMemoryOk\": " << boolean(facts.apply_to_memory_ok) << ",\n"
        << "  \"fullReparseOk\": " << boolean(facts.full_reparse_ok) << ",\n"
        << "  \"targetDistanceMatchCount\": " << facts.target_distance_match_count << ",\n"
        << "  \"nonTargetChangedCount\": " << facts.non_target_changed_count << ",\n"
        << "  \"distanceGroupCount\": " << facts.distance_group_count << ",\n"
        << "  \"createdDistanceBlockCount\": " << facts.created_distance_block_count << ",\n"
        << "  \"reusedDistanceBlockCount\": " << facts.reused_distance_block_count << ",\n"
        << "  \"noLocalWrapper\": " << boolean(facts.no_local_wrapper) << ",\n"
        << "  \"resetOk\": " << boolean(facts.reset_ok) << ",\n"
        << "  \"resetIrFingerprintRestored\": "
        << boolean(facts.reset_ir_fingerprint_restored) << ",\n"
        << "  \"initialIrFingerprint\": " << json_escape(facts.initial_ir_fingerprint) << ",\n"
        << "  \"resetIrFingerprint\": " << json_escape(facts.reset_ir_fingerprint) << ",\n"
        << "  \"commitAttempted\": " << boolean(facts.commit_attempted) << ",\n"
        << "  \"commitOk\": " << boolean(facts.commit_ok) << ",\n"
        << "  \"selected\": [";
    for (size_t i = 0; i < facts.selected.size(); ++i) {
        if (i) out << ",";
        const StructureEdit& edit = facts.selected[i];
        out << "\n    {\"editId\":" << json_escape(edit.edit_id)
            << ",\"sourceFile\":" << json_escape(edit.source_file)
            << ",\"sourceLine\":" << edit.source_line
            << ",\"structureKey\":" << json_escape(edit.structure_key)
            << ",\"oldDistance\":" << edit_number(edit.old_distance)
            << ",\"targetDistance\":" << edit_number(edit.target_distance)
            << ",\"increment\":" << edit.increment
            << ",\"selectionScore\":" << edit.selection_score
            << ",\"distanceExpression\":"
            << json_escape(edit.original_distance_expression) << "}";
    }
    if (!facts.selected.empty()) out << "\n  ";
    out << "],\n"
        << "  \"fixtures\": {\n"
        << "    \"increasingSameTargetOneBlock\": "
        << boolean(facts.fixtures.increasing_same_target_one_block) << ",\n"
        << "    \"canonicalIdenticalSameTargetOneBlock\": "
        << boolean(facts.fixtures.canonical_identical_same_target_one_block) << ",\n"
        << "    \"terminalUniqueTargetReused\": "
        << boolean(facts.fixtures.terminal_unique_target_reused) << ",\n"
        << "    \"repeatedIncludePartialBlocked\": "
        << boolean(facts.fixtures.repeated_include_partial_blocked) << ",\n"
        << "    \"repeatedIncludeAllTargetsCoalesced\": "
        << boolean(facts.fixtures.repeated_include_all_targets_coalesced) << ",\n"
        << "    \"increasingTargetMatchCount\": "
        << facts.fixtures.increasing_target_match_count << ",\n"
        << "    \"unorderedRequiresResolutionWithoutPatch\": "
        << boolean(facts.fixtures.unordered_requires_resolution_without_patch) << ",\n"
        << "    \"unorderedResolutionCount\": "
        << facts.fixtures.unordered_resolution_count << ",\n"
        << "    \"variableEnvironmentChangeBlocked\": "
        << boolean(facts.fixtures.variable_environment_change_blocked) << ",\n"
        << "    \"environmentBlockingErrorCount\": "
        << facts.fixtures.environment_blocking_error_count << ",\n"
        << "    \"derivedStationCollisionBlocked\": "
        << boolean(facts.fixtures.derived_station_collision_blocked) << ",\n"
        << "    \"derivedStateBlockingErrorCount\": "
        << facts.fixtures.derived_state_blocking_error_count << ",\n"
        << "    \"stagedVariableResolutionSucceeded\": "
        << boolean(facts.fixtures.staged_variable_resolution_succeeded) << ",\n"
        << "    \"stagedVariableResolutionCount\": "
        << facts.fixtures.staged_variable_resolution_count << ",\n"
        << "    \"directApplyHandleAdvanced\": "
        << boolean(facts.fixtures.direct_apply_handle_advanced) << ",\n"
        << "    \"directApplySecondUpdateSucceeded\": "
        << boolean(facts.fixtures.direct_apply_second_update_succeeded) << ",\n"
        << "    \"transactionalApplyFailurePreservedFiles\": "
        << boolean(facts.fixtures.transactional_apply_failure_preserved_files) << ",\n"
        << "    \"transactionalTwoFileApplySucceeded\": "
        << boolean(facts.fixtures.transactional_two_file_apply_succeeded) << ",\n"
        << "    \"error\": " << json_escape(facts.fixtures.error) << "\n"
        << "  },\n"
        << "  \"error\": " << json_escape(facts.error) << ",\n"
        << "  \"result\": " << json_escape(facts.passed() ? "PASS" : "FAIL") << "\n"
        << "}\n";
}

} // namespace distance_batch_headless

int run_debug_headless_distance_edit_batch(const HeadlessDistanceEditBatchOptions& options) {
    using namespace distance_batch_headless;
    std::ofstream output_file;
    std::ostream* out = &std::cout;
    if (!options.output_path.empty()) {
        output_file.open(std::filesystem::path(utf8_to_wide(options.output_path)),
                         std::ios::out | std::ios::trunc | std::ios::binary);
        if (!output_file) {
            std::cerr << "failed to open headless output: " << options.output_path << "\n";
            return 1;
        }
        out = &output_file;
    }

    BatchRunFacts facts;
    facts.path = options.path;
    facts.commit_requested = options.commit;
    try {
        MapHandle handle;
        handle.value = kv_load_map_ex(options.path.c_str(), options.unit_distance,
                                      KV_LOAD_EDIT_METADATA);
        if (!handle.value) {
            const char* error = kv_get_last_error();
            throw std::runtime_error(error ? error : "real map load failed");
        }

        const std::string initial_ir = compact_ir_json(handle.value);
        facts.initial_ir_fingerprint = hash_text(initial_ir);
        std::vector<StructureEdit> rows = structure_rows_from_ir(initial_ir);
        facts.selected = select_real_map_edits(
            handle.value, rows, facts.candidate_dry_run_attempts,
            facts.selected_sig_context_count);

        std::map<std::string, ResolutionChoice> resolutions;
        std::string final_changes = build_changes_json(facts.selected, resolutions);
        JsonValue final_report = JsonParser(dry_run_json(handle.value, final_changes)).parse();
        facts.initial_resolution_request_count = static_cast<int>(
            final_report.at("resolutionRequests").array.size());
        for (const JsonValue& request : final_report.at("resolutionRequests").array) {
            std::string summary = request.at("reason").scalar_text();
            const std::string variable = request.at("variableName").scalar_text();
            if (!variable.empty()) summary += ":" + variable;
            facts.initial_resolution_reasons.push_back(std::move(summary));
        }
        while (!final_report.at("resolutionRequests").array.empty()) {
            if (facts.resolution_round_count >= 6) {
                throw std::runtime_error("distance resolution did not converge after six rounds");
            }
            std::string resolution_error;
            if (!inject_report_resolutions(
                    final_report, facts.selected, resolutions,
                    facts.variable_expression_resolution_count, resolution_error)) {
                throw std::runtime_error(resolution_error.empty()
                    ? "distance resolution request could not be applied"
                    : resolution_error);
            }
            ++facts.resolution_round_count;
            final_changes = build_changes_json(facts.selected, resolutions);
            final_report = JsonParser(dry_run_json(handle.value, final_changes)).parse();
        }

        ReportFacts dry = report_facts(final_report);
        facts.final_dry_run_ok =
            dry.ok && dry.full_reparse_ok && dry.target_distance_match_count == 5 &&
            dry.non_target_changed_count == 0 && dry.distance_group_count == 5;
        if (!facts.final_dry_run_ok) {
            std::string detail = first_blocking_error(final_report);
            if (detail.empty()) detail = "final dry-run assertions did not match";
            throw std::runtime_error(detail);
        }
        const bool dry_without_wrapper = !preview_has_local_wrapper(final_report, facts.selected);

        JsonValue apply_report = JsonParser(
            apply_memory_json(handle.value, final_changes)).parse();
        ReportFacts applied = report_facts(apply_report);
        facts.apply_to_memory_ok = applied.ok;
        facts.full_reparse_ok = applied.full_reparse_ok;
        facts.target_distance_match_count = applied.target_distance_match_count;
        facts.non_target_changed_count = applied.non_target_changed_count;
        facts.distance_group_count = applied.distance_group_count;
        facts.created_distance_block_count = applied.created_distance_block_count;
        facts.reused_distance_block_count = applied.reused_distance_block_count;
        facts.no_local_wrapper =
            dry_without_wrapper && !preview_has_local_wrapper(apply_report, facts.selected);
        if (!facts.apply_to_memory_ok || !facts.full_reparse_ok ||
            facts.target_distance_match_count != 5 || facts.non_target_changed_count != 0 ||
            facts.distance_group_count != 5 ||
            facts.created_distance_block_count + facts.reused_distance_block_count !=
                facts.distance_group_count ||
            !facts.no_local_wrapper) {
            std::string detail = first_blocking_error(apply_report);
            if (detail.empty()) detail = "apply-to-memory assertions did not match";
            throw std::runtime_error(detail);
        }

        facts.reset_ok = kv_edit_reset_memory(handle.value) != 0;
        if (!facts.reset_ok) {
            const char* error = kv_get_last_error();
            throw std::runtime_error(error ? error : "kv_edit_reset_memory failed");
        }
        const std::string reset_ir = compact_ir_json(handle.value);
        facts.reset_ir_fingerprint = hash_text(reset_ir);
        facts.reset_ir_fingerprint_restored =
            facts.reset_ir_fingerprint == facts.initial_ir_fingerprint && reset_ir == initial_ir;

        facts.fixtures = run_fixture_checks(options.unit_distance);
        if (!facts.reset_ir_fingerprint_restored) {
            size_t first_difference = 0;
            const size_t common_size = std::min(initial_ir.size(), reset_ir.size());
            while (first_difference < common_size &&
                   initial_ir[first_difference] == reset_ir[first_difference]) {
                ++first_difference;
            }
            const size_t context_begin = first_difference > 80 ? first_difference - 80 : 0;
            throw std::runtime_error(
                "reset compact-IR differs at byte " + std::to_string(first_difference) +
                " (initial=" + json_escape(initial_ir.substr(context_begin, 160)) +
                ", reset=" + json_escape(reset_ir.substr(context_begin, 160)) + ")");
        }

        if (!facts.fixtures.passed()) {
            throw std::runtime_error(facts.fixtures.error.empty()
                ? "one or more distance planner fixture checks failed"
                : facts.fixtures.error);
        }

        if (options.commit) {
            facts.commit_attempted = true;
            JsonValue commit_apply = JsonParser(
                apply_memory_json(handle.value, final_changes)).parse();
            ReportFacts reapplied = report_facts(commit_apply);
            if (!reapplied.ok || !reapplied.full_reparse_ok ||
                reapplied.target_distance_match_count != 5 ||
                reapplied.non_target_changed_count != 0) {
                std::string detail = first_blocking_error(commit_apply);
                throw std::runtime_error(detail.empty()
                    ? "pre-commit apply-to-memory validation failed"
                    : detail);
            }
            JsonValue commit_report = JsonParser(commit_json(handle.value)).parse();
            ReportFacts committed = report_facts(commit_report);
            facts.commit_ok = committed.ok && committed.full_reparse_ok;
            if (!facts.commit_ok) {
                std::string detail = first_blocking_error(commit_report);
                throw std::runtime_error(detail.empty() ? "commit validation failed" : detail);
            }
        }
    } catch (const std::exception& e) {
        facts.error = e.what();
    }

    write_batch_result(*out, facts);
    out->flush();
    return facts.passed() ? 0 : 20;
}

int App::run_debug_headless_plan_benchmark(const std::string& path, int frames,
                                           double unit_distance, double pan_pixels,
                                           double max_frame_ms, const std::string& output_path,
                                           bool profile_stages) {
    std::ofstream output_file;
    std::ostream* out = &std::cout;
    if (!output_path.empty()) {
        output_file.open(std::filesystem::path(utf8_to_wide(output_path)), std::ios::out | std::ios::trunc);
        if (!output_file) {
            std::cerr << "failed to open headless output: " << output_path << "\n";
            return 1;
        }
        output_file.write("\xEF\xBB\xBF", 3);
        out = &output_file;
    }

    *out << "komapedit debug-headless-plan-bench path=\"" << path
         << "\" frames=" << frames
         << " unit_distance=" << format_double(unit_distance, 3)
         << " pan_pixels=" << format_double(pan_pixels, 3)
         << " max_frame_ms=" << format_double(max_frame_ms, 3) << "\n";
    *out << "stage=load-start\n";
    out->flush();

    LoadResult result = load_map_worker(path, unit_distance, false, 0.0, 0.0, 25.0);
    if (!result.ok) {
        std::cerr << "debug headless plan benchmark load failed: " << result.error << "\n";
        return 2;
    }
    *out << "stage=load-complete\n";
    out->flush();

    ImGui::CreateContext();
    ImPlot::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(1280.0f, 720.0f);
    io.DeltaTime = 1.0f / 60.0f;
    io.IniFilename = nullptr;
    io.Fonts->AddFontDefault();
    io.Fonts->Build();
    *out << "stage=imgui-ready\n";
    out->flush();

    int exit_code = 0;
    try {
        g_debug_plan_benchmark_log = profile_stages ? out : nullptr;
        UserSettings settings;
        App app(nullptr, settings, 1.0f, false, false);
        app.handle_ = result.handle;
        result.handle = nullptr;
        app.model_ = std::move(result.model);
        app.file_path_ = path;
        app.has_model_ = true;
        app.dmin_ = app.model_.default_min;
        app.dmax_ = app.model_.default_max;
        app.plot_min_ = app.dmin_;
        app.plot_max_ = app.dmax_;
        app.rebuild_marker_overlay_cache();
        app.reset_marker_visibility();
        std::fill(app.other_train_path_visible_.begin(), app.other_train_path_visible_.end(), 1);
        std::fill(app.signal_row_visible_.begin(), app.signal_row_visible_.end(), 1);
        std::fill(app.repeater_row_visible_.begin(), app.repeater_row_visible_.end(), 1);
        *out << "stage=overlay-cache-ready\n";
        out->flush();

        size_t chunk_count = 0;
        size_t segment_point_count = 0;
        size_t repeater_begin_marker_count = 0;
        size_t repeater_end_marker_count = 0;
        size_t repeater_segment_count = 0;
        size_t other_train_stop_marker_count = 0;
        size_t other_train_path_point_count = 0;
        for (const RepeaterOverlayRow& row : app.repeater_marker_cache_) {
            if (row.begin_marker) ++repeater_begin_marker_count;
            if (row.end_marker) ++repeater_end_marker_count;
            if (row.segment.bounds_valid) ++repeater_segment_count;
            for (const PlanRepeaterSegment::Chunk& chunk : row.segment.chunks) {
                ++chunk_count;
                segment_point_count += chunk.points.size();
            }
        }
        for (const auto& marker : app.other_train_stop_marker_cache_) {
            if (marker) ++other_train_stop_marker_count;
        }
        for (const OtherTrainPathOverlay& path : app.other_train_path_cache_) {
            other_train_path_point_count += path.points.size();
        }
        size_t visible_other_count = 0;
        size_t visible_other_rows = 0;
        for (const OtherTrack& track : app.model_.other_tracks) {
            if (!track.visible) continue;
            ++visible_other_count;
            visible_other_rows += track.points.rows;
        }
        *out << "loaded own_rows=" << app.model_.own.rows
             << " visible_othertracks=" << visible_other_count
             << " visible_other_rows=" << visible_other_rows
             << " other_trains=" << app.model_.other_trains.size()
             << " other_train_stops=" << app.model_.other_train_stops.size()
             << " other_train_stop_markers=" << other_train_stop_marker_count
             << " other_train_paths=" << app.other_train_path_cache_.size()
             << " other_train_path_points=" << other_train_path_point_count
             << " signal_aspects=" << app.model_.signal_aspects.size()
             << " signals=" << app.model_.signals.size()
             << " signal_markers=" << app.signal_marker_cache_.size()
             << " beacons=" << app.model_.beacons.size()
             << " beacon_markers=" << app.beacon_marker_cache_.size()
             << " pretrains=" << app.model_.pretrains.size()
             << " pretrain_markers=" << app.pretrain_marker_cache_.size()
             << " map_sounds=" << app.model_.map_sounds.size()
             << " map_sound_markers=" << app.map_sound_marker_cache_.size()
             << " map_sound_3d=" << app.model_.map_sound_3d.size()
             << " map_sound_3d_markers=" << app.map_sound_3d_marker_cache_.size()
             << " rolling_noises=" << app.model_.rolling_noises.size()
             << " rolling_noise_markers=" << app.rolling_noise_marker_cache_.size()
             << " flange_noises=" << app.model_.flange_noises.size()
             << " flange_noise_markers=" << app.flange_noise_marker_cache_.size()
             << " joint_noises=" << app.model_.joint_noises.size()
             << " joint_noise_markers=" << app.joint_noise_marker_cache_.size()
             << " backgrounds=" << app.model_.backgrounds.size()
             << " background_markers=" << app.background_marker_cache_.size()
             << " repeaters=" << app.repeater_marker_cache_.size()
             << " repeater_begin_markers=" << repeater_begin_marker_count
             << " repeater_end_markers=" << repeater_end_marker_count
             << " repeater_segments=" << repeater_segment_count
             << " selected_repeaters=" << app.repeater_row_visible_.size()
             << " repeater_chunks=" << chunk_count
             << " repeater_chunk_points=" << segment_point_count << "\n";
        out->flush();

        auto render_frame = [&]() {
            io.DisplaySize = ImVec2(1280.0f, 720.0f);
            io.DeltaTime = 1.0f / 60.0f;
            ImGui::NewFrame();
            ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f), ImGuiCond_Always);
            ImGui::SetNextWindowSize(io.DisplaySize, ImGuiCond_Always);
            ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings |
                ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoBringToFrontOnFocus;
            ImGui::Begin("DebugHeadlessPlanBenchmark", nullptr, flags);
            app.render_plan_canvas(ImVec2(1260.0f, 680.0f));
            if (g_debug_plan_benchmark_log) {
                int total_vtx = 0;
                int total_idx = 0;
                ImGuiContext& context = *GImGui;
                for (ImGuiWindow* window : context.Windows) {
                    if (!window || !window->WasActive || !window->DrawList) continue;
                    total_vtx += window->DrawList->VtxBuffer.Size;
                    total_idx += window->DrawList->IdxBuffer.Size;
                }
                *g_debug_plan_benchmark_log << "render_stage=before_imgui_render"
                                            << " scale=" << std::fixed << std::setprecision(6) << app.plan_view_.scale
                                            << " vtx=" << total_vtx
                                            << " idx=" << total_idx << "\n";
                g_debug_plan_benchmark_log->flush();
            }
            ImGui::End();
            ImGui::EndFrame();
        };

        *out << "stage=warmup-start\n";
        out->flush();
        for (int frame = 0; frame < 1; ++frame) {
            render_frame();
        }
        *out << "stage=warmup-complete\n";
        out->flush();

        std::vector<double> frame_ms;
        frame_ms.reserve(static_cast<size_t>(frames));
        for (int frame = 0; frame < frames; ++frame) {
            double step = (frame % 120 < 60) ? pan_pixels : -pan_pixels;
            app.plan_view_.pan_by_screen_delta(ImVec2(static_cast<float>(step), 0.0f));
            auto started_at = std::chrono::steady_clock::now();
            render_frame();
            auto finished_at = std::chrono::steady_clock::now();
            frame_ms.push_back(std::chrono::duration<double, std::milli>(finished_at - started_at).count());
        }
        *out << "stage=frames-complete\n";
        out->flush();

        std::vector<double> sorted_ms = frame_ms;
        std::sort(sorted_ms.begin(), sorted_ms.end());
        double sum_ms = 0.0;
        for (double value : frame_ms) sum_ms += value;
        auto percentile = [&](double p) {
            if (sorted_ms.empty()) return 0.0;
            size_t index = static_cast<size_t>(std::ceil(p * static_cast<double>(sorted_ms.size()))) - 1;
            index = std::min(index, sorted_ms.size() - 1);
            return sorted_ms[index];
        };
        double avg_ms = frame_ms.empty() ? 0.0 : sum_ms / static_cast<double>(frame_ms.size());
        double min_ms = sorted_ms.empty() ? 0.0 : sorted_ms.front();
        double p95_ms = percentile(0.95);
        double max_ms = sorted_ms.empty() ? 0.0 : sorted_ms.back();
        double p95_fps = p95_ms > 0.0 ? 1000.0 / p95_ms : 0.0;
        bool pass = p95_ms <= max_frame_ms;

        *out << std::fixed << std::setprecision(3)
             << "plan_bench avg_ms=" << avg_ms
             << " min_ms=" << min_ms
             << " p95_ms=" << p95_ms
             << " max_ms=" << max_ms
             << " p95_fps=" << p95_fps
             << " result=" << (pass ? "PASS" : "FAIL") << "\n";
        if (!pass) exit_code = 3;
    } catch (const std::exception& e) {
        std::cerr << "debug headless plan benchmark failed: " << e.what() << "\n";
        exit_code = 4;
    }

    g_debug_plan_benchmark_log = nullptr;
    if (result.handle) kv_free(result.handle);
    ImPlot::DestroyContext();
    ImGui::DestroyContext();
    return exit_code;
}

int App::run_debug_headless_scene3d_benchmark(const std::string& path, int frames,
                                              double unit_distance, double max_frame_ms,
                                              double window_back_m, double window_forward_m,
                                              const std::string& output_path) {
    std::ofstream output_file;
    std::ostream* out = &std::cout;
    if (!output_path.empty()) {
        output_file.open(std::filesystem::path(utf8_to_wide(output_path)), std::ios::out | std::ios::trunc);
        if (!output_file) {
            std::cerr << "failed to open headless output: " << output_path << "\n";
            return 1;
        }
        output_file.write("\xEF\xBB\xBF", 3);
        out = &output_file;
    }

    *out << "komapedit debug-headless-scene3d-bench path=\"" << path
         << "\" frames=" << frames
         << " unit_distance=" << format_double(unit_distance, 3)
         << " max_frame_ms=" << format_double(max_frame_ms, 3)
         << " window_back_m=" << format_double(window_back_m, 3)
         << " window_forward_m=" << format_double(window_forward_m, 3) << "\n";
    *out << "stage=d3d-create-start\n";
    out->flush();

    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* context = nullptr;
    const char* driver = "hardware";
    if (!create_headless_d3d_device(device, context, driver)) {
        std::cerr << "debug headless scene3d benchmark failed: D3D11CreateDevice\n";
        release_com(context);
        release_com(device);
        return 2;
    }
    *out << "stage=d3d-ready driver=" << driver << "\n";
    out->flush();

    *out << "stage=load-start\n";
    out->flush();
    LoadResult result = load_map_worker(path, unit_distance, false, 0.0, 0.0, 25.0);
    if (!result.ok) {
        std::cerr << "debug headless scene3d benchmark load failed: " << result.error << "\n";
        release_com(context);
        release_com(device);
        return 3;
    }
    *out << "stage=load-complete\n";
    out->flush();

    ImGui::CreateContext();
    ImPlot::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(1280.0f, 720.0f);
    io.DeltaTime = 1.0f / 60.0f;
    io.IniFilename = nullptr;
    io.Fonts->AddFontDefault();
    io.Fonts->Build();

    int exit_code = 0;
    try {
        UserSettings settings;
        App app(device, settings, 1.0f, false, false);
        app.handle_ = result.handle;
        result.handle = nullptr;
        app.model_ = std::move(result.model);
        app.file_path_ = path;
        app.has_model_ = true;
        app.dmin_ = app.model_.default_min;
        app.dmax_ = app.model_.default_max;
        app.plot_min_ = app.dmin_;
        app.plot_max_ = app.dmax_;
        app.rebuild_marker_overlay_cache();
        app.reset_marker_visibility();
        app.scene_preview_started_ = true;
        app.scene_preview_canvas_->set_scene_window(window_back_m, window_forward_m);
        app.rebuild_scene_preview();
        Canvas3DSceneStats initial_stats = app.scene_preview_canvas_->scene_stats();
        *out << "stage=scene-ready"
             << " chunks=" << initial_stats.chunk_count
             << " instances=" << initial_stats.instance_count
             << " models=" << initial_stats.model_path_count
             << " camera_distance=" << format_double(initial_stats.camera_distance, 3)
             << " window_back_m=" << format_double(initial_stats.window_back_m, 3)
             << " window_forward_m=" << format_double(initial_stats.window_forward_m, 3) << "\n";
        out->flush();

        auto render_frame = [&]() {
            io.DisplaySize = ImVec2(1280.0f, 720.0f);
            io.DeltaTime = 1.0f / 60.0f;
            ImGui::NewFrame();
            ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f), ImGuiCond_Always);
            ImGui::SetNextWindowSize(io.DisplaySize, ImGuiCond_Always);
            ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings |
                ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoBringToFrontOnFocus;
            ImGui::Begin("DebugHeadlessScene3DBenchmark", nullptr, flags);
            app.scene_preview_canvas_->render_scene_preview(ImVec2(1260.0f, 680.0f));
            ImGui::End();
            ImGui::EndFrame();
        };

        *out << "stage=warmup-start\n";
        out->flush();
        int warmup_frames = 0;
        for (; warmup_frames < 900; ++warmup_frames) {
            render_frame();
            Canvas3DSceneStats stats = app.scene_preview_canvas_->scene_stats();
            if (!stats.loading && stats.model_ready_count + stats.model_failed_count >= stats.model_path_count) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        for (int i = 0; i < 5; ++i) render_frame();
        Canvas3DSceneStats warmed_stats = app.scene_preview_canvas_->scene_stats();
        *out << "stage=warmup-complete"
             << " warmup_frames=" << warmup_frames
             << " model_ready=" << warmed_stats.model_ready_count
             << " model_failed=" << warmed_stats.model_failed_count
             << " model_total=" << warmed_stats.model_path_count
             << " drawn_instances=" << warmed_stats.drawn_instance_count
             << " drawn_track_chunks=" << warmed_stats.drawn_track_chunk_count << "\n";
        out->flush();

        std::vector<double> frame_ms;
        frame_ms.reserve(static_cast<size_t>(frames));
        for (int frame = 0; frame < frames; ++frame) {
            auto started_at = std::chrono::steady_clock::now();
            render_frame();
            auto finished_at = std::chrono::steady_clock::now();
            frame_ms.push_back(std::chrono::duration<double, std::milli>(finished_at - started_at).count());
        }
        *out << "stage=frames-complete\n";
        out->flush();

        std::vector<double> sorted_ms = frame_ms;
        std::sort(sorted_ms.begin(), sorted_ms.end());
        double sum_ms = 0.0;
        for (double value : frame_ms) sum_ms += value;
        auto percentile = [&](double p) {
            if (sorted_ms.empty()) return 0.0;
            size_t index = static_cast<size_t>(std::ceil(p * static_cast<double>(sorted_ms.size()))) - 1;
            index = std::min(index, sorted_ms.size() - 1);
            return sorted_ms[index];
        };
        double avg_ms = frame_ms.empty() ? 0.0 : sum_ms / static_cast<double>(frame_ms.size());
        double min_ms = sorted_ms.empty() ? 0.0 : sorted_ms.front();
        double p95_ms = percentile(0.95);
        double max_ms = sorted_ms.empty() ? 0.0 : sorted_ms.back();
        double p95_fps = p95_ms > 0.0 ? 1000.0 / p95_ms : 0.0;
        bool pass = p95_ms <= max_frame_ms;
        Canvas3DSceneStats final_stats = app.scene_preview_canvas_->scene_stats();

        *out << std::fixed << std::setprecision(3)
             << "scene3d_bench avg_ms=" << avg_ms
             << " min_ms=" << min_ms
             << " p95_ms=" << p95_ms
             << " max_ms=" << max_ms
             << " p95_fps=" << p95_fps
             << " drawn_instances=" << final_stats.drawn_instance_count
             << " drawn_track_chunks=" << final_stats.drawn_track_chunk_count
             << " result=" << (pass ? "PASS" : "FAIL") << "\n";
        if (!pass) exit_code = 4;
    } catch (const std::exception& e) {
        std::cerr << "debug headless scene3d benchmark failed: " << e.what() << "\n";
        exit_code = 5;
    }

    if (result.handle) kv_free(result.handle);
    ImPlot::DestroyContext();
    ImGui::DestroyContext();
    context->ClearState();
    context->Flush();
    release_com(context);
    release_com(device);
    return exit_code;
}

int App::run_debug_headless_scene_camera_transfer(const std::string& path, double unit_distance,
                                                  bool has_camera_distance, double camera_distance,
                                                  const std::string& output_path) {
    std::ofstream output_file;
    std::ostream* out = &std::cout;
    if (!output_path.empty()) {
        output_file.open(std::filesystem::path(utf8_to_wide(output_path)), std::ios::out | std::ios::trunc);
        if (!output_file) {
            std::cerr << "failed to open headless output: " << output_path << "\n";
            return 1;
        }
        output_file.write("\xEF\xBB\xBF", 3);
        out = &output_file;
    }

    *out << "komapedit debug-headless-scene-camera-transfer path=\"" << path
         << "\" unit_distance=" << format_double(unit_distance, 3);
    if (has_camera_distance) *out << " camera_distance=" << format_double(camera_distance, 3);
    *out << "\n";
    *out << "stage=d3d-create-start\n";
    out->flush();

    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* context = nullptr;
    const char* driver = "hardware";
    if (!create_headless_d3d_device(device, context, driver)) {
        std::cerr << "debug headless scene camera transfer failed: D3D11CreateDevice\n";
        release_com(context);
        release_com(device);
        return 2;
    }
    *out << "stage=d3d-ready driver=" << driver << "\n";
    out->flush();

    *out << "stage=load-start\n";
    out->flush();
    LoadResult load_result = load_map_worker(path, unit_distance, false, 0.0, 0.0, 25.0);
    if (!load_result.ok) {
        std::cerr << "debug headless scene camera transfer load failed: " << load_result.error << "\n";
        release_com(context);
        release_com(device);
        return 3;
    }
    *out << "stage=load-complete\n";

    int exit_code = 0;
    try {
        Canvas3DSceneBuildOptions options;
        options.model = &load_result.model;
        options.map_handle = load_result.handle;
        options.unit_distance = unit_distance;
        options.control_point_start = load_result.model.cp_arb[0];
        options.control_point_end = load_result.model.cp_arb[1];
        options.control_point_interval = load_result.model.cp_arb[2];
        options.show_own_track_markers = true;
        Canvas3DSceneBuildResult build_result = build_canvas3d_scene_preview(options);
        for (const std::string& message : build_result.log_messages) *out << message << "\n";
        Canvas3DScene scene = std::move(build_result.scene);
        if (scene.tracks.empty()) {
            *out << "stage=scene-build-empty result=FAIL\n";
            exit_code = 4;
        } else {
            Canvas3DCameraStart expected_start = scene.camera;
            Canvas3D canvas(device);
            std::string error;
            if (!canvas.load_scene(std::move(scene), error)) {
                *out << "stage=scene-load-failed error=\"" << error << "\" result=FAIL\n";
                exit_code = 5;
            } else {
                Canvas3DSceneCameraPose start_pose = canvas.scene_camera_pose();
                const double expected_x = -expected_start.z;
                const double expected_y = expected_start.x;
                bool start_ok = start_pose.valid &&
                    std::abs(start_pose.distance - expected_start.distance) <= 1e-6 &&
                    std::abs(start_pose.x - expected_x) <= 1e-6 &&
                    std::abs(start_pose.y - expected_y) <= 1e-6 &&
                    angle_distance(start_pose.theta, expected_start.yaw) <= 1e-6;
                *out << "stage=start-pose"
                     << " valid=" << (start_pose.valid ? "true" : "false")
                     << " x=" << format_double(start_pose.x, 6)
                     << " y=" << format_double(start_pose.y, 6)
                     << " z=" << format_double(start_pose.z, 6)
                     << " theta=" << format_double(start_pose.theta, 6)
                     << " distance=" << format_double(start_pose.distance, 6)
                     << " expected_x=" << format_double(expected_x, 6)
                     << " expected_y=" << format_double(expected_y, 6)
                     << " expected_theta=" << format_double(expected_start.yaw, 6)
                     << " transfer=" << (start_ok ? "PASS" : "FAIL") << "\n";

                double target_distance = has_camera_distance
                    ? camera_distance
                    : std::clamp(expected_start.distance + unit_distance,
                                 load_result.model.default_min,
                                 load_result.model.default_max);
                bool jumped = canvas.jump_scene_camera_to_distance(target_distance);
                Canvas3DSceneCameraPose jump_pose = canvas.scene_camera_pose();
                bool jump_ok = jumped && jump_pose.valid &&
                    std::isfinite(jump_pose.x) && std::isfinite(jump_pose.y) &&
                    std::isfinite(jump_pose.theta);
                *out << "stage=jump-pose"
                     << " requested_distance=" << format_double(target_distance, 6)
                     << " valid=" << (jump_pose.valid ? "true" : "false")
                     << " x=" << format_double(jump_pose.x, 6)
                     << " y=" << format_double(jump_pose.y, 6)
                     << " theta=" << format_double(jump_pose.theta, 6)
                     << " distance=" << format_double(jump_pose.distance, 6)
                     << " transfer=" << (jump_ok ? "PASS" : "FAIL") << "\n";
                if (!start_ok || !jump_ok) exit_code = 6;
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "debug headless scene camera transfer failed: " << e.what() << "\n";
        exit_code = 7;
    }

    if (load_result.handle) kv_free(load_result.handle);
    context->ClearState();
    context->Flush();
    release_com(context);
    release_com(device);
    if (exit_code == 0) *out << "result=PASS\n";
    out->flush();
    return exit_code;
}
#endif
