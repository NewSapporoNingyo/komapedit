/*
 * Copyright (c) 2026 Sapporo_ningyo
 *
 * Licensed under Apache License 2.0; see LICENSE and NOTICE.
 * The GUI uses Dear ImGui and ImPlot; see THIRD_PARTY_NOTICES.md.
 */

#pragma execution_character_set("utf-8")

#include "kme.h"
#include "canvas3D.h"
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
#include <sstream>
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

struct HeadlessLoadOptions {
    bool requested = false;
    std::string path;
    std::string output_path;
    int repeat = 1;
    double unit_distance = 25.0;
    std::string error;
};

struct HeadlessPlanBenchmarkOptions {
    bool requested = false;
    std::string path;
    std::string output_path;
    int frames = 300;
    double unit_distance = 25.0;
    double pan_pixels = 8.0;
    double max_frame_ms = 16.667;
    bool profile_stages = false;
    std::string error;
};

struct HeadlessScene3DBenchmarkOptions {
    bool requested = false;
    std::string path;
    std::string output_path;
    int frames = 300;
    double unit_distance = 25.0;
    double max_frame_ms = 16.667;
    double window_back_m = 100.0;
    double window_forward_m = 1200.0;
    std::string error;
};

struct HeadlessSceneCameraTransferOptions {
    bool requested = false;
    std::string path;
    std::string output_path;
    double unit_distance = 25.0;
    bool has_camera_distance = false;
    double camera_distance = 0.0;
    std::string error;
};

struct HeadlessTableFindOptions {
    bool requested = false;
    std::string output_path;
    std::string error;
};

struct HeadlessTouchInputOptions {
    bool requested = false;
    std::string output_path;
    std::string error;
};

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
         << " unit_distance=" << format_double(options.unit_distance, 3) << "\n";

    for (int run = 1; run <= options.repeat; ++run) {
        auto started_at = std::chrono::steady_clock::now();
        void* handle = kv_load_map(options.path.c_str(), options.unit_distance);
        auto loaded_at = std::chrono::steady_clock::now();
        if (!handle) {
            const char* err = kv_get_last_error();
            std::cerr << "headless run " << run << " failed: "
                      << (err ? err : "maploader failed") << "\n";
            return 2;
        }

        const char* json = kv_get_ir_json(handle);
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
