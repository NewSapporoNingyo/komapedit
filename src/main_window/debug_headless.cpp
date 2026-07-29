/*
 * Copyright (c) 2026 Sapporo_ningyo
 *
 * Licensed under Apache License 2.0; see LICENSE and NOTICE.
 * The GUI uses Dear ImGui and ImPlot; see THIRD_PARTY_NOTICES.md.
 */

#ifdef _MSC_VER
#pragma execution_character_set("utf-8")
#endif

#include "kme.h"
#include "app_settings.h"
#include "canvas3D.h"
#include "debug_headless.h"
#include "maploader.h"
#include "repeater_linkage.h"
#include "touch_input.h"

#include "imgui.h"
#include "imgui_internal.h"
#include "implot.h"

#include <windows.h>
#include <d3d11.h>
#include <shellapi.h>

#include <algorithm>
#include <array>
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
#include <mutex>
#include <sstream>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#ifndef NDEBUG
class ScopedComApartment {
public:
    ScopedComApartment() : result_(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)) {
        should_uninitialize_ = SUCCEEDED(result_);
    }

    ~ScopedComApartment() {
        if (should_uninitialize_) CoUninitialize();
    }

    bool ready() const {
        return SUCCEEDED(result_) || result_ == RPC_E_CHANGED_MODE;
    }

private:
    HRESULT result_ = E_FAIL;
    bool should_uninitialize_ = false;
};

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

const std::string* take_option_value(const std::vector<std::string>& args, size_t& index,
                                     std::string_view option, const char* requirement,
                                     std::string& error) {
    if (index + 1 >= args.size()) {
        error.assign(option.data(), option.size());
        error += " requires ";
        error += requirement;
        return nullptr;
    }
    return &args[++index];
}

bool parse_integer_option(const std::vector<std::string>& args, size_t& index,
                          std::string_view option, long minimum, long maximum,
                          const char* invalid_message, int& destination,
                          std::string& error) {
    const std::string* text = take_option_value(args, index, option, "a number", error);
    if (!text) return false;
    char* end = nullptr;
    long parsed = std::strtol(text->c_str(), &end, 10);
    if (!end || *end != '\0' || parsed < minimum || parsed > maximum) {
        error = invalid_message;
        return false;
    }
    destination = static_cast<int>(parsed);
    return true;
}

template <typename Validator>
bool parse_double_option(const std::vector<std::string>& args, size_t& index,
                         std::string_view option, const char* requirement,
                         const char* invalid_message, double& destination,
                         std::string& error, Validator&& valid) {
    const std::string* text = take_option_value(args, index, option, requirement, error);
    if (!text) return false;
    char* end = nullptr;
    double parsed = std::strtod(text->c_str(), &end);
    if (!end || *end != '\0' || !valid(parsed)) {
        error = invalid_message;
        return false;
    }
    destination = parsed;
    return true;
}

struct FrameTimingStats {
    double average_ms = 0.0;
    double minimum_ms = 0.0;
    double p95_ms = 0.0;
    double maximum_ms = 0.0;
    double p95_fps = 0.0;
};

FrameTimingStats calculate_frame_timing_stats(const std::vector<double>& frame_ms) {
    FrameTimingStats stats;
    if (frame_ms.empty()) return stats;
    std::vector<double> sorted_ms = frame_ms;
    std::sort(sorted_ms.begin(), sorted_ms.end());
    double sum_ms = 0.0;
    for (double value : frame_ms) sum_ms += value;
    const size_t p95_index = std::min(
        static_cast<size_t>(std::ceil(0.95 * static_cast<double>(sorted_ms.size()))) - 1,
        sorted_ms.size() - 1);
    stats.average_ms = sum_ms / static_cast<double>(frame_ms.size());
    stats.minimum_ms = sorted_ms.front();
    stats.p95_ms = sorted_ms[p95_index];
    stats.maximum_ms = sorted_ms.back();
    stats.p95_fps = stats.p95_ms > 0.0 ? 1000.0 / stats.p95_ms : 0.0;
    return stats;
}

std::array<size_t, 22> plan_data_size_summary(const PlanData& data) {
    return {
        data.own.size(),
        data.stations.size(),
        data.speedlimits.size(),
        data.structure_markers.size(),
        data.repeater_markers.size(),
        data.signal_markers.size(),
        data.beacon_markers.size(),
        data.pretrain_markers.size(),
        data.other_train_stop_markers.size(),
        data.other_train_paths.size(),
        data.irregularity_markers.size(),
        data.map_sound_markers.size(),
        data.map_sound_3d_markers.size(),
        data.rolling_noise_markers.size(),
        data.flange_noise_markers.size(),
        data.joint_noise_markers.size(),
        data.background_markers.size(),
        data.adhesion_markers.size(),
        data.cab_illuminance_markers.size(),
        data.fog_markers.size(),
        data.curve_sections.size(),
        data.transition_sections.size(),
    };
}

bool plan_data_summary_matches(const PlanData& left, const PlanData& right) {
    return plan_data_size_summary(left) == plan_data_size_summary(right) &&
        left.origin_angle == right.origin_angle &&
        left.xmin == right.xmin && left.ymin == right.ymin &&
        left.xmax == right.xmax && left.ymax == right.ymax;
}

HeadlessLoadOptions parse_headless_load_options(const std::vector<std::string>& args) {
    HeadlessLoadOptions options;
    for (size_t i = 1; i < args.size(); ++i) {
        const std::string& arg = args[i];
        if (arg == "--headless-load-map" || arg == "--headless-load") {
            options.requested = true;
            const std::string* value = take_option_value(args, i, arg, "a map path", options.error);
            if (!value) return options;
            options.path = *value;
        } else if (arg == "--repeat") {
            if (!parse_integer_option(args, i, arg, 1, 10000,
                                      "--repeat must be between 1 and 10000",
                                      options.repeat, options.error)) return options;
        } else if (arg == "--unit-distance") {
            if (!parse_double_option(args, i, arg, "a number",
                                     "--unit-distance must be a positive number",
                                     options.unit_distance, options.error,
                                     [](double value) { return value > 0.0 && std::isfinite(value); })) {
                return options;
            }
        } else if (arg == "--load-profile") {
            const std::string* value = take_option_value(
                args, i, arg, "preview or edit", options.error);
            if (!value) return options;
            const std::string& profile = *value;
            if (profile == "preview" || profile == "edit") {
                options.load_profile = profile;
            } else {
                options.error = "--load-profile must be preview or edit";
                return options;
            }
        } else if (arg == "--headless-output") {
            const std::string* value = take_option_value(args, i, arg, "a path", options.error);
            if (!value) return options;
            options.output_path = *value;
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
            const std::string* value = take_option_value(args, i, arg, "a map path", options.error);
            if (!value) return options;
            options.path = *value;
        } else if (arg == "--frames") {
            if (!parse_integer_option(args, i, arg, 1, 100000,
                                      "--frames must be between 1 and 100000",
                                      options.frames, options.error)) return options;
        } else if (arg == "--unit-distance") {
            if (!parse_double_option(args, i, arg, "a number",
                                     "--unit-distance must be a positive number",
                                     options.unit_distance, options.error,
                                     [](double value) { return value > 0.0 && std::isfinite(value); })) {
                return options;
            }
        } else if (arg == "--pan-pixels") {
            if (!parse_double_option(args, i, arg, "a number",
                                     "--pan-pixels must be a finite number",
                                     options.pan_pixels, options.error,
                                     [](double value) { return std::isfinite(value); })) {
                return options;
            }
        } else if (arg == "--max-frame-ms") {
            if (!parse_double_option(args, i, arg, "a number",
                                     "--max-frame-ms must be a positive number",
                                     options.max_frame_ms, options.error,
                                     [](double value) { return value > 0.0 && std::isfinite(value); })) {
                return options;
            }
        } else if (arg == "--headless-output") {
            const std::string* value = take_option_value(args, i, arg, "a path", options.error);
            if (!value) return options;
            options.output_path = *value;
        } else if (arg == "--profile-stages") {
            options.profile_stages = true;
        }
    }
    if (options.requested && options.path.empty() && options.error.empty()) {
        options.error = "--debug-headless-plan-bench requires a map path";
    }
    return options;
}

HeadlessOpenBenchmarkOptions parse_headless_open_benchmark_options(
    const std::vector<std::string>& args) {
    HeadlessOpenBenchmarkOptions options;
    for (size_t i = 1; i < args.size(); ++i) {
        const std::string& arg = args[i];
        if (arg == "--debug-headless-open-bench") {
            options.requested = true;
            const std::string* value = take_option_value(args, i, arg, "a map path", options.error);
            if (!value) return options;
            options.path = *value;
        } else if (arg == "--repeat") {
            if (!parse_integer_option(args, i, arg, 1, 10000,
                                      "--repeat must be between 1 and 10000",
                                      options.repeat, options.error)) return options;
        } else if (arg == "--unit-distance") {
            if (!parse_double_option(args, i, arg, "a number",
                                     "--unit-distance must be a positive number",
                                     options.unit_distance, options.error,
                                     [](double value) { return value > 0.0 && std::isfinite(value); })) {
                return options;
            }
        } else if (arg == "--headless-output") {
            const std::string* value = take_option_value(args, i, arg, "a path", options.error);
            if (!value) return options;
            options.output_path = *value;
        }
    }
    if (options.requested && options.path.empty() && options.error.empty()) {
        options.error = "--debug-headless-open-bench requires a map path";
    }
    return options;
}

HeadlessScene3DBenchmarkOptions parse_headless_scene3d_benchmark_options(const std::vector<std::string>& args) {
    HeadlessScene3DBenchmarkOptions options;
    for (size_t i = 1; i < args.size(); ++i) {
        const std::string& arg = args[i];
        if (arg == "--debug-headless-scene3d-bench") {
            options.requested = true;
            const std::string* value = take_option_value(args, i, arg, "a map path", options.error);
            if (!value) return options;
            options.path = *value;
        } else if (arg == "--frames") {
            if (!parse_integer_option(args, i, arg, 1, 100000,
                                      "--frames must be between 1 and 100000",
                                      options.frames, options.error)) return options;
        } else if (arg == "--unit-distance") {
            if (!parse_double_option(args, i, arg, "a number",
                                     "--unit-distance must be a positive number",
                                     options.unit_distance, options.error,
                                     [](double value) { return value > 0.0 && std::isfinite(value); })) {
                return options;
            }
        } else if (arg == "--max-frame-ms") {
            if (!parse_double_option(args, i, arg, "a number",
                                     "--max-frame-ms must be a positive number",
                                     options.max_frame_ms, options.error,
                                     [](double value) { return value > 0.0 && std::isfinite(value); })) {
                return options;
            }
        } else if (arg == "--window-back-m") {
            if (!parse_double_option(args, i, arg, "a number",
                                     "--window-back-m must be a non-negative number",
                                     options.window_back_m, options.error,
                                     [](double value) { return value >= 0.0 && std::isfinite(value); })) {
                return options;
            }
        } else if (arg == "--window-forward-m") {
            if (!parse_double_option(args, i, arg, "a number",
                                     "--window-forward-m must be a positive number",
                                     options.window_forward_m, options.error,
                                     [](double value) { return value > 0.0 && std::isfinite(value); })) {
                return options;
            }
        } else if (arg == "--scene-model-workers") {
            if (!parse_integer_option(args, i, arg, 1, 8,
                                      "--scene-model-workers must be between 1 and 8",
                                      options.scene_model_workers, options.error)) return options;
        } else if (arg == "--disable-scene-texture-cache") {
            options.disable_scene_texture_cache = true;
        } else if (arg == "--headless-output") {
            const std::string* value = take_option_value(args, i, arg, "a path", options.error);
            if (!value) return options;
            options.output_path = *value;
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
            const std::string* value = take_option_value(args, i, arg, "a map path", options.error);
            if (!value) return options;
            options.path = *value;
        } else if (arg == "--unit-distance") {
            if (!parse_double_option(args, i, arg, "a number",
                                     "--unit-distance must be a positive number",
                                     options.unit_distance, options.error,
                                     [](double value) { return value > 0.0 && std::isfinite(value); })) {
                return options;
            }
        } else if (arg == "--camera-distance") {
            double parsed = 0.0;
            if (!parse_double_option(args, i, arg, "a number",
                                     "--camera-distance must be a finite number",
                                     parsed, options.error,
                                     [](double value) { return std::isfinite(value); })) {
                return options;
            }
            options.has_camera_distance = true;
            options.camera_distance = parsed;
        } else if (arg == "--headless-output") {
            const std::string* value = take_option_value(args, i, arg, "a path", options.error);
            if (!value) return options;
            options.output_path = *value;
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
            const std::string* value = take_option_value(args, i, arg, "a map path", options.error);
            if (!value) return options;
            options.path = *value;
        } else if (arg == "--unit-distance") {
            if (!parse_double_option(args, i, arg, "a number",
                                     "--unit-distance must be a positive number",
                                     options.unit_distance, options.error,
                                     [](double value) { return value > 0.0 && std::isfinite(value); })) {
                return options;
            }
        } else if (arg == "--headless-output") {
            const std::string* value = take_option_value(args, i, arg, "a path", options.error);
            if (!value) return options;
            options.output_path = *value;
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
            const std::string* value = take_option_value(args, i, arg, "a map path", options.error);
            if (!value) return options;
            options.path = *value;
        } else if (arg == "--unit-distance") {
            if (!parse_double_option(args, i, arg, "a value",
                                     "--unit-distance must be positive",
                                     options.unit_distance, options.error,
                                     [](double value) { return value > 0.0; })) {
                return options;
            }
        } else if (arg == "--headless-output") {
            const std::string* value = take_option_value(args, i, arg, "a path", options.error);
            if (!value) return options;
            options.output_path = *value;
        }
    }
    if (options.requested && options.path.empty()) {
        options.error = "--debug-headless-edit-roundtrip requires a map path";
    }
    return options;
}

HeadlessDistanceEditBatchOptions parse_headless_distance_edit_batch_options(
    const std::vector<std::string>& args) {
    static constexpr const char* k_default_map_path =
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
            if (!parse_double_option(args, i, arg, "a value",
                                     "--unit-distance must be a positive finite number",
                                     options.unit_distance, options.error,
                                     [](double value) { return value > 0.0 && std::isfinite(value); })) {
                return options;
            }
        } else if (arg == "--headless-output") {
            const std::string* value = take_option_value(args, i, arg, "a path", options.error);
            if (!value) return options;
            options.output_path = *value;
        } else if (arg == "--commit") {
            options.commit = true;
        }
    }
    if (options.requested && options.path.empty()) options.path = k_default_map_path;
    return options;
}

HeadlessStationListEditOptions parse_headless_station_list_edit_options(
    const std::vector<std::string>& args) {
    HeadlessStationListEditOptions options;
    for (size_t i = 1; i < args.size(); ++i) {
        const std::string& arg = args[i];
        if (arg == "--debug-headless-station-list-edit") {
            options.requested = true;
            const std::string* value =
                take_option_value(args, i, arg, "a map path", options.error);
            if (!value) return options;
            options.path = *value;
        } else if (arg == "--unit-distance") {
            if (!parse_double_option(args, i, arg, "a value",
                                     "--unit-distance must be a positive finite number",
                                     options.unit_distance, options.error,
                                     [](double value) {
                                         return value > 0.0 && std::isfinite(value);
                                     })) {
                return options;
            }
        } else if (arg == "--headless-output") {
            const std::string* value =
                take_option_value(args, i, arg, "a path", options.error);
            if (!value) return options;
            options.output_path = *value;
        } else if (arg == "--commit") {
            options.commit = true;
        }
    }
    if (options.requested && options.path.empty() && options.error.empty()) {
        options.error = "--debug-headless-station-list-edit requires a map path";
    }
    return options;
}

HeadlessRepeaterEditBatchOptions parse_headless_repeater_edit_batch_options(
    const std::vector<std::string>& args) {
    static constexpr const char* k_default_map_path =
        "E:\\Railway\\BveTsWorkspace\\BVE-Gensokyo-Railway\\GSR\\Scenarios_GSR\\map\\"
        "Config_Map121M-ATSP+Ps_Ask.txt";
    HeadlessRepeaterEditBatchOptions options;
    for (size_t i = 1; i < args.size(); ++i) {
        const std::string& arg = args[i];
        if (arg == "--debug-headless-repeater-edit-batch") {
            options.requested = true;
            if (i + 1 < args.size() && args[i + 1].rfind("--", 0) != 0) {
                options.path = args[++i];
            }
        } else if (arg == "--unit-distance") {
            if (!parse_double_option(args, i, arg, "a value",
                                     "--unit-distance must be a positive finite number",
                                     options.unit_distance, options.error,
                                     [](double value) { return value > 0.0 && std::isfinite(value); })) {
                return options;
            }
        } else if (arg == "--headless-output") {
            const std::string* value = take_option_value(args, i, arg, "a path", options.error);
            if (!value) return options;
            options.output_path = *value;
        } else if (arg == "--commit") {
            options.commit = true;
        }
    }
    if (options.requested && options.path.empty()) options.path = k_default_map_path;
    return options;
}

HeadlessTableFindOptions parse_headless_table_find_options(const std::vector<std::string>& args) {
    HeadlessTableFindOptions options;
    for (size_t i = 1; i < args.size(); ++i) {
        const std::string& arg = args[i];
        if (arg == "--debug-headless-table-find") {
            options.requested = true;
        } else if (arg == "--headless-output") {
            const std::string* value = take_option_value(args, i, arg, "a path", options.error);
            if (!value) return options;
            options.output_path = *value;
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
            const std::string* value = take_option_value(args, i, arg, "a path", options.error);
            if (!value) return options;
            options.output_path = *value;
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

std::mutex g_headless_map_log_mutex;
std::vector<std::string>* g_headless_map_logs = nullptr;

void headless_map_log_callback(const char* message) {
    if (!message) return;
    std::lock_guard<std::mutex> lock(g_headless_map_log_mutex);
    if (g_headless_map_logs) g_headless_map_logs->emplace_back(message);
}

class ScopedHeadlessMapLogCapture {
public:
    explicit ScopedHeadlessMapLogCapture(std::vector<std::string>& logs) {
        std::lock_guard<std::mutex> lock(g_headless_map_log_mutex);
        g_headless_map_logs = &logs;
        kv_set_log_callback(headless_map_log_callback);
    }

    ~ScopedHeadlessMapLogCapture() {
        kv_set_log_callback(nullptr);
        std::lock_guard<std::mutex> lock(g_headless_map_log_mutex);
        g_headless_map_logs = nullptr;
    }
};

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
         << " load_profile=" << options.load_profile << "\n";

    std::vector<std::string> captured_logs;
    ScopedHeadlessMapLogCapture log_capture(captured_logs);
    auto flush_logs = [&]() {
        std::vector<std::string> logs;
        {
            std::lock_guard<std::mutex> lock(g_headless_map_log_mutex);
            logs.swap(captured_logs);
        }
        for (const std::string& line : logs) *out << line << "\n";
        out->flush();
    };

    for (int run = 1; run <= options.repeat; ++run) {
        auto started_at = std::chrono::steady_clock::now();
        const bool edit_profile = options.load_profile == "edit";
        unsigned load_flags = edit_profile ? KV_LOAD_EDIT_METADATA : KV_LOAD_PREVIEW;
        void* handle = kv_load_map_ex(options.path.c_str(), options.unit_distance, load_flags);
        auto loaded_at = std::chrono::steady_clock::now();
        flush_logs();
        if (!handle) {
            const char* err = kv_get_last_error();
            const std::string failure =
                "headless run " + std::to_string(run) + " failed: " +
                (err ? err : "maploader failed");
            *out << failure << "\n";
            out->flush();
            std::cerr << failure << "\n";
            return 2;
        }

        KvMapSnapshot snapshot{};
        if (!kv_get_map_snapshot(handle, KV_MAP_SNAPSHOT_VERSION,
                                 &snapshot, sizeof(snapshot))) {
            const char* err = kv_get_last_error();
            const std::string failure =
                "headless run " + std::to_string(run) + " snapshot failed: " +
                (err ? err : "typed snapshot unavailable");
            *out << failure << "\n";
            out->flush();
            std::cerr << failure << "\n";
            kv_free(handle);
            return 2;
        }
        auto snapshot_at = std::chrono::steady_clock::now();

        HeadlessBufferSummary own = summarize_headless_buffer(snapshot.own_track_geometry);
        HeadlessBufferSummary curve = summarize_headless_buffer(snapshot.curve_radius_geometry);
        HeadlessBufferSummary structures = summarize_headless_buffer(snapshot.structure_put_geometry);
        const size_t other_count = static_cast<size_t>(snapshot.other_track_count);
        HeadlessBufferSummary other_total;
        other_total.cols = 8;
        for (size_t i = 0; i < other_count; ++i) {
            HeadlessBufferSummary item = summarize_headless_buffer(snapshot.other_tracks[i].points);
            other_total.rows += item.rows;
            other_total.finite = other_total.finite && item.finite;
            other_total.hash ^= item.hash + 0x9e3779b97f4a7c15ULL +
                (other_total.hash << 6) + (other_total.hash >> 2);
        }

        const uint64_t string_bytes = snapshot.string_size;
        const uint64_t content_revision = snapshot.content_revision;
        const uint64_t geometry_revision = snapshot.geometry_revision;
        const uint64_t statement_count = snapshot.statement_count;
        const uint64_t element_count = snapshot.element_count;
        const double snapshot_build_seconds = snapshot.build_seconds;
        kv_free(handle);
        auto finished_at = std::chrono::steady_clock::now();

        const double load_seconds = std::chrono::duration<double>(loaded_at - started_at).count();
        const double snapshot_seconds = std::chrono::duration<double>(snapshot_at - loaded_at).count();
        const double total_seconds = std::chrono::duration<double>(finished_at - started_at).count();
        *out << "headless run " << run
             << " load=" << std::fixed << std::setprecision(3) << load_seconds << "s"
             << " snapshot=" << snapshot_seconds << "s"
             << " total=" << total_seconds << "s"
             << " snapshot_build=" << snapshot_build_seconds << "s"
             << " snapshot_string_bytes=" << string_bytes
             << " content_revision=" << content_revision
             << " geometry_revision=" << geometry_revision
             << " statements=" << statement_count
             << " elements=" << element_count
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
            {"_signalMainStructureKeyCount", "1"},
            {"_signalGlareStructureKeyCount", "0"},
            {"structureKey1", "structureOnlyKey"},
        }));
        app.model_.signal_aspects.push_back(make_row({
            {"signalAspectKey", "aspectB"},
            {"_structureKeyCount", "1"},
            {"_signalMainStructureKeyCount", "1"},
            {"_signalGlareStructureKeyCount", "0"},
            {"structureKey1", "modelB"},
        }));
        app.model_.signal_aspects.push_back(make_row({
            {"signalAspectKey", "unusedAspect"},
            {"_structureKeyCount", "1"},
            {"_signalMainStructureKeyCount", "1"},
            {"_signalGlareStructureKeyCount", "0"},
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
    count_missing_rows(model.sound_3d_list);
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

namespace typed_edit_headless {

struct Field {
    std::string name;
    std::string value;
};

struct Change {
    std::string change_id;
    std::string edit_id;
    std::uint32_t operation = KV_EDIT_UPDATE;
    std::uint32_t flags = 0;
    std::vector<Field> fields;
    std::string replacement_statement;
    std::string target_file_path;
    std::string insert_before_edit_id;
    std::string expected_source_hash;
    std::string distance_resolution_key;
    std::string distance_boundary_token;
    std::string distance_expression;
};

struct Boundary {
    std::string token;
    int line = 0;
    int column = 0;
    bool recommended = false;
};

struct Resolution {
    std::string resolution_key;
    std::string reason;
    std::string source_file;
    std::vector<std::string> include_stack;
    double target_distance = 0.0;
    std::string variable_name;
    std::vector<std::string> affected_edit_ids;
    std::string suggested_expression;
    std::string insertion_preview;
    bool can_confirm_reuse = false;
    int source_section_first_line = 0;
    int source_section_last_line = 0;
    std::string source_section_direction;
    std::vector<Boundary> allowed_boundaries;
};

struct Preview {
    std::string file_path;
    std::string before;
    std::string after;
};

struct CommittedFile {
    std::string file_path;
    std::string source_hash;
    std::uint64_t byte_length = 0;
};

struct CommittedRow {
    std::string row_kind;
    std::uint64_t row_index = 0;
    std::string edit_id;
    std::string file_path;
    int line = 0;
    int column = 0;
    std::string raw_text_preview;
};

struct Report {
    bool ok = false;
    int update_count = 0;
    int insert_count = 0;
    int delete_count = 0;
    bool full_reparse_ok = false;
    int target_distance_match_count = 0;
    int non_target_changed_count = 0;
    int created_distance_block_count = 0;
    int reused_distance_block_count = 0;
    int distance_group_count = 0;
    std::string validation_fingerprint;
    std::vector<std::string> changed_files;
    std::vector<CommittedFile> committed_files;
    std::vector<CommittedRow> committed_rows;
    std::vector<std::string> warnings;
    std::vector<std::string> blocking_errors;
    std::vector<Resolution> resolution_requests;
    std::vector<Preview> previews;
};

KvUtf8View view(const std::string& text) {
    return {text.empty() ? nullptr : text.data(), static_cast<std::uint64_t>(text.size())};
}

class Batch {
public:
    explicit Batch(const std::vector<Change>& input) {
        size_t field_count = 0;
        for (const Change& change : input) field_count += change.fields.size();
        changes_.reserve(input.size());
        fields_.reserve(field_count);
        for (const Change& change : input) {
            KvEditChange output{};
            output.change_id = view(change.change_id);
            output.edit_id = view(change.edit_id);
            output.operation = change.operation;
            output.flags = change.flags;
            output.fields.offset = static_cast<std::uint64_t>(fields_.size());
            output.fields.count = static_cast<std::uint64_t>(change.fields.size());
            output.replacement_statement = view(change.replacement_statement);
            output.target_file_path = view(change.target_file_path);
            output.insert_before_edit_id = view(change.insert_before_edit_id);
            output.expected_source_hash = view(change.expected_source_hash);
            output.distance_resolution_key = view(change.distance_resolution_key);
            output.distance_boundary_token = view(change.distance_boundary_token);
            output.distance_expression = view(change.distance_expression);
            for (const Field& field : change.fields) {
                fields_.push_back({view(field.name), view(field.value)});
            }
            changes_.push_back(output);
        }
        batch_.changes = changes_.empty() ? nullptr : changes_.data();
        batch_.change_count = static_cast<std::uint64_t>(changes_.size());
        batch_.fields = fields_.empty() ? nullptr : fields_.data();
        batch_.field_count = static_cast<std::uint64_t>(fields_.size());
    }

    const KvEditBatch* get() const { return &batch_; }

private:
    std::vector<KvEditChange> changes_;
    std::vector<KvEditField> fields_;
    KvEditBatch batch_{};
};

Report copy_report(const KvEditReportSnapshot& input) {
    if (input.version != KV_EDIT_REPORT_SNAPSHOT_VERSION ||
        input.structure_size < sizeof(KvEditReportSnapshot)) {
        throw std::runtime_error("typed edit report version or size mismatch");
    }
    auto text = [&](KvStringRef ref) {
        if (ref.length == 0) return std::string{};
        if (!input.string_data || ref.offset > input.string_size ||
            ref.length > input.string_size - ref.offset) {
            throw std::runtime_error("typed edit report string reference is out of range");
        }
        return std::string(input.string_data + static_cast<size_t>(ref.offset),
                           static_cast<size_t>(ref.length));
    };
    auto strings = [&](KvSpan span) {
        if (span.offset > input.string_ref_count ||
            span.count > input.string_ref_count - span.offset ||
            (span.count != 0 && !input.string_refs)) {
            throw std::runtime_error("typed edit report string span is out of range");
        }
        std::vector<std::string> output;
        output.reserve(static_cast<size_t>(span.count));
        for (std::uint64_t i = 0; i < span.count; ++i) {
            output.push_back(text(input.string_refs[span.offset + i]));
        }
        return output;
    };
    auto check_pointer = [](const void* pointer, std::uint64_t count, const char* label) {
        if (count != 0 && !pointer) {
            throw std::runtime_error(std::string("typed edit report has null ") + label);
        }
    };
    check_pointer(input.changed_files, input.changed_file_count, "changed-files array");
    check_pointer(input.committed_files, input.committed_file_count, "committed-files array");
    check_pointer(input.committed_rows, input.committed_row_count, "committed-rows array");
    check_pointer(input.warnings, input.warning_count, "warnings array");
    check_pointer(input.blocking_errors, input.blocking_error_count, "errors array");
    check_pointer(input.resolution_requests, input.resolution_request_count,
                  "resolution array");
    check_pointer(input.preview_snippets, input.preview_snippet_count, "preview array");

    Report output;
    output.ok = input.ok != 0;
    output.update_count = input.update_count;
    output.insert_count = input.insert_count;
    output.delete_count = input.delete_count;
    output.full_reparse_ok = input.full_reparse_ok != 0;
    output.target_distance_match_count = input.target_distance_match_count;
    output.non_target_changed_count = input.non_target_changed_count;
    output.created_distance_block_count = input.created_distance_block_count;
    output.reused_distance_block_count = input.reused_distance_block_count;
    output.distance_group_count = input.distance_group_count;
    output.validation_fingerprint = text(input.validation_fingerprint);
    output.changed_files.reserve(static_cast<size_t>(input.changed_file_count));
    for (std::uint64_t i = 0; i < input.changed_file_count; ++i) {
        output.changed_files.push_back(text(input.changed_files[i]));
    }
    output.committed_files.reserve(static_cast<size_t>(input.committed_file_count));
    for (std::uint64_t i = 0; i < input.committed_file_count; ++i) {
        const KvEditCommittedFileRow& row = input.committed_files[i];
        output.committed_files.push_back({text(row.file_path), text(row.source_hash),
                                          row.byte_length});
    }
    output.committed_rows.reserve(static_cast<size_t>(input.committed_row_count));
    for (std::uint64_t i = 0; i < input.committed_row_count; ++i) {
        const KvEditCommittedRow& row = input.committed_rows[i];
        output.committed_rows.push_back({text(row.row_kind), row.row_index, text(row.edit_id),
                                         text(row.file_path), row.line, row.column,
                                         text(row.raw_text_preview)});
    }
    output.warnings.reserve(static_cast<size_t>(input.warning_count));
    for (std::uint64_t i = 0; i < input.warning_count; ++i) {
        output.warnings.push_back(text(input.warnings[i]));
    }
    output.blocking_errors.reserve(static_cast<size_t>(input.blocking_error_count));
    for (std::uint64_t i = 0; i < input.blocking_error_count; ++i) {
        output.blocking_errors.push_back(text(input.blocking_errors[i]));
    }
    output.resolution_requests.reserve(static_cast<size_t>(input.resolution_request_count));
    for (std::uint64_t i = 0; i < input.resolution_request_count; ++i) {
        const KvDistanceResolutionRow& row = input.resolution_requests[i];
        if (row.allowed_boundaries.offset > input.boundary_count ||
            row.allowed_boundaries.count > input.boundary_count - row.allowed_boundaries.offset ||
            (row.allowed_boundaries.count != 0 && !input.boundaries)) {
            throw std::runtime_error("typed edit report boundary span is out of range");
        }
        Resolution resolution;
        resolution.resolution_key = text(row.resolution_key);
        resolution.reason = text(row.reason);
        resolution.source_file = text(row.source_file);
        resolution.include_stack = strings(row.include_stack);
        resolution.target_distance = row.target_distance;
        resolution.variable_name = text(row.variable_name);
        resolution.affected_edit_ids = strings(row.affected_edit_ids);
        resolution.suggested_expression = text(row.suggested_expression);
        resolution.insertion_preview = text(row.insertion_preview);
        resolution.can_confirm_reuse = row.can_confirm_reuse != 0;
        resolution.source_section_first_line = row.source_section_first_line;
        resolution.source_section_last_line = row.source_section_last_line;
        resolution.source_section_direction = text(row.source_section_direction);
        resolution.allowed_boundaries.reserve(
            static_cast<size_t>(row.allowed_boundaries.count));
        for (std::uint64_t j = 0; j < row.allowed_boundaries.count; ++j) {
            const KvDistanceBoundaryRow& boundary =
                input.boundaries[row.allowed_boundaries.offset + j];
            resolution.allowed_boundaries.push_back({text(boundary.token), boundary.line,
                                                      boundary.column,
                                                      boundary.recommended != 0});
        }
        output.resolution_requests.push_back(std::move(resolution));
    }
    output.previews.reserve(static_cast<size_t>(input.preview_snippet_count));
    for (std::uint64_t i = 0; i < input.preview_snippet_count; ++i) {
        const KvEditPreviewRow& row = input.preview_snippets[i];
        output.previews.push_back({text(row.file_path), text(row.before_text),
                                   text(row.after_text)});
    }
    return output;
}

Report dry_run(void* handle, const std::vector<Change>& changes) {
    Batch batch(changes);
    KvEditReportSnapshot output{};
    if (!kv_edit_dry_run_typed(handle, batch.get(), &output, sizeof(output))) {
        const char* error = kv_get_last_error();
        throw std::runtime_error(error ? error : "typed edit dry-run failed");
    }
    return copy_report(output);
}

Report apply_to_memory(void* handle, const std::vector<Change>& changes) {
    Batch batch(changes);
    KvEditReportSnapshot output{};
    if (!kv_edit_apply_to_memory_typed(handle, batch.get(), &output, sizeof(output))) {
        const char* error = kv_get_last_error();
        throw std::runtime_error(error ? error : "typed memory apply failed");
    }
    return copy_report(output);
}

Report direct_apply(void* handle, const std::vector<Change>& changes) {
    Batch batch(changes);
    KvEditReportSnapshot output{};
    if (!kv_edit_apply_typed(handle, batch.get(), &output, sizeof(output))) {
        const char* error = kv_get_last_error();
        throw std::runtime_error(error ? error : "typed direct apply failed");
    }
    return copy_report(output);
}

Report commit(void* handle) {
    KvEditReportSnapshot output{};
    if (!kv_edit_commit_typed(handle, &output, sizeof(output))) {
        const char* error = kv_get_last_error();
        throw std::runtime_error(error ? error : "typed edit commit failed");
    }
    return copy_report(output);
}

Change update(std::string change_id, std::string edit_id,
              std::string expected_source_hash,
              std::vector<Field> fields) {
    Change change;
    change.change_id = std::move(change_id);
    change.edit_id = std::move(edit_id);
    change.expected_source_hash = std::move(expected_source_hash);
    change.fields = std::move(fields);
    return change;
}

} // namespace typed_edit_headless

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

    using typed_edit_headless::Change;
    using typed_edit_headless::Field;
    using typed_edit_headless::Report;
    auto source_hash_for_path = [](const MapModel& model, const std::string& file_path) {
        for (const EditSourceFileInfo& file : model.edit_files) {
            if (file.file_path == file_path) return file.source_hash;
        }
        return std::string{};
    };
    auto make_update = [&](const TableRow& row, const MapModel& model,
                           const std::map<std::string, std::string>& fields,
                           const std::string& expected_hash_override = {}) {
        std::string expected_hash = expected_hash_override.empty()
            ? source_hash_for_path(model, row.source.file_path)
            : expected_hash_override;
        std::vector<Field> typed_fields;
        typed_fields.reserve(fields.size());
        for (const auto& field : fields) {
            typed_fields.push_back({field.first, field.second});
        }
        return std::vector<Change>{typed_edit_headless::update(
            "headless-update", row.edit_id, expected_hash, std::move(typed_fields))};
    };
    auto find_structure_by_edit_id = [](const MapModel& model,
                                        const std::string& edit_id) -> const TableRow* {
        auto row = std::find_if(model.structures.begin(), model.structures.end(),
                                [&](const TableRow& candidate) {
                                    return candidate.edit_id == edit_id;
                                });
        return row == model.structures.end() ? nullptr : &*row;
    };
    auto find_structure_by_values = [](const MapModel& model, double distance,
                                       double x) -> const TableRow* {
        auto row = std::find_if(model.structures.begin(), model.structures.end(),
                                [&](const TableRow& candidate) {
                                    return std::abs(table_cell_number(candidate, "distance") -
                                                    distance) < 1e-6 &&
                                           std::abs(table_cell_number(candidate, "x") - x) < 1e-6;
                                });
        return row == model.structures.end() ? nullptr : &*row;
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
                 << "125;\n"
                 << "Structure['pole'].Put('0',9,2,3,0,0,0,0,25);\n"
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
    KvMapSnapshot baseline_snapshot{};
    KvMapSnapshot applied_snapshot{};
    const bool baseline_snapshot_ok = kv_get_map_snapshot(
        load.handle, KV_MAP_SNAPSHOT_VERSION,
        &baseline_snapshot, sizeof(baseline_snapshot)) != 0;
    *out << "snapshot_baseline_ok=" << (baseline_snapshot_ok ? 1 : 0) << "\n";
    if (!baseline_snapshot_ok) exit_code = 4;
    const TableRow* initial_target = find_structure_by_values(load.model, 100.0, 1.0);
    const TableRow* initial_non_target = find_structure_by_values(load.model, 125.0, 9.0);
    if (!initial_target || !initial_non_target) {
        *out << "missing_structure_identity_fixture_rows=1\nresult=FAIL\n";
        if (load.handle) kv_free(load.handle);
        std::filesystem::remove_all(temp_root);
        return 3;
    }

    const TableRow& structure_row = *initial_target;
    MapModel local_commit_model = load.model;
    const std::string baseline_structure_edit_id = structure_row.edit_id;
    const std::string baseline_non_target_edit_id = initial_non_target->edit_id;
    const std::string baseline_source_hash =
        source_hash_for_path(load.model, structure_row.source.file_path);
    std::string baseline_metadata_error;
    const std::optional<InspectorTargetMetadata> baseline_target_info =
        resolve_inspector_target_metadata(load.handle, baseline_structure_edit_id,
                                           "structure.put", &baseline_metadata_error);
    std::string baseline_non_target_metadata_error;
    const std::optional<InspectorTargetMetadata> baseline_non_target_info =
        resolve_inspector_target_metadata(load.handle, baseline_non_target_edit_id,
                                           "structure.put",
                                           &baseline_non_target_metadata_error);
    const bool baseline_metadata_ok = baseline_target_info && baseline_non_target_info &&
        baseline_target_info->source_distance_string == "100" &&
        std::abs(baseline_target_info->distance_value - 100.0) < 1e-9 &&
        baseline_non_target_info->source_distance_string == "125" &&
        std::abs(baseline_non_target_info->distance_value - 125.0) < 1e-9 &&
        !baseline_source_hash.empty() &&
        baseline_target_info->expected_source_hash == baseline_source_hash &&
        baseline_non_target_info->expected_source_hash == baseline_source_hash &&
        baseline_target_info->source.file_path == structure_row.source.file_path &&
        baseline_non_target_info->source.file_path == structure_row.source.file_path &&
        baseline_target_info->source.line != baseline_non_target_info->source.line &&
        baseline_target_info->raw_statement.find("Structure['pole'].Put") !=
            std::string::npos;
    *out << "baseline_source_distance_metadata_ok=" << (baseline_metadata_ok ? 1 : 0) << "\n";
    if (!baseline_metadata_ok) {
        *out << "baseline_target_metadata_error=" << baseline_metadata_error << "\n"
             << "baseline_non_target_metadata_error=" <<
                baseline_non_target_metadata_error << "\n";
        exit_code = 4;
    }
    std::vector<Change> changes = make_update(structure_row, load.model, {
        {"distance", "125"},
        {"x", "9"},
        {"y", "2"},
        {"z", "3"}
    });
    Report dry_report = typed_edit_headless::dry_run(load.handle, changes);
    const bool dry_ok = dry_report.ok;
    *out << "dry_run_ok=" << (dry_ok ? 1 : 0) << "\n";
    if (!dry_ok) {
        *out << "dry_run_error_count=" << dry_report.blocking_errors.size() << "\n";
        exit_code = 4;
    }

    std::vector<Change> stale_changes = make_update(
        structure_row, load.model, {{"x", "11"}}, "bad-hash");
    Report stale_report = typed_edit_headless::dry_run(load.handle, stale_changes);
    const bool stale_blocked = !stale_report.ok && std::any_of(
        stale_report.blocking_errors.begin(), stale_report.blocking_errors.end(),
        [](const std::string& error) {
            return error.find("source file changed externally") != std::string::npos;
        });
    *out << "stale_hash_blocked=" << (stale_blocked ? 1 : 0) << "\n";
    if (!stale_blocked) {
        *out << "stale_hash_error_count=" << stale_report.blocking_errors.size() << "\n";
        exit_code = 5;
    }

    Report apply_memory_report;
    if (exit_code == 0) {
        apply_memory_report = typed_edit_headless::apply_to_memory(load.handle, changes);
        const bool apply_memory_ok = apply_memory_report.ok;
        *out << "apply_to_memory_ok=" << (apply_memory_ok ? 1 : 0) << "\n";
        if (!apply_memory_ok) {
            *out << "apply_to_memory_error_count="
                 << apply_memory_report.blocking_errors.size() << "\n";
            exit_code = 6;
        } else {
            const bool applied_snapshot_ok = kv_get_map_snapshot(
                load.handle, KV_MAP_SNAPSHOT_VERSION,
                &applied_snapshot, sizeof(applied_snapshot)) != 0;
            const bool apply_snapshot_invalidated = baseline_snapshot_ok && applied_snapshot_ok &&
                applied_snapshot.content_revision > baseline_snapshot.content_revision &&
                applied_snapshot.geometry_revision > baseline_snapshot.geometry_revision;
            *out << "snapshot_apply_invalidation_ok="
                 << (apply_snapshot_invalidated ? 1 : 0) << "\n";
            if (!apply_snapshot_invalidated) exit_code = 6;
            MapModel memory_model = build_model_from_handle(
                load.handle, wide_to_utf8(temp_map.wstring()), LoadModelOptions{true});
            const TableRow* memory_target =
                find_structure_by_edit_id(memory_model, baseline_structure_edit_id);
            const TableRow* memory_non_target =
                find_structure_by_edit_id(memory_model, baseline_non_target_edit_id);
            const double memory_distance = memory_target
                ? table_cell_number(*memory_target, "distance") : 0.0;
            const double memory_x = memory_target
                ? table_cell_number(*memory_target, "x") : 0.0;
            const bool memory_matches = memory_target && memory_non_target &&
                std::abs(memory_distance - 125.0) < 1e-6 &&
                std::abs(memory_x - 9.0) < 1e-6 &&
                std::abs(table_cell_number(*memory_non_target, "distance") - 125.0) < 1e-6 &&
                std::abs(table_cell_number(*memory_non_target, "x") - 9.0) < 1e-6;
            *out << "memory_distance=" << format_double(memory_distance, 3) << "\n";
            *out << "memory_x=" << format_double(memory_x, 3) << "\n";
            *out << "apply_to_memory_match=" << (memory_matches ? 1 : 0) << "\n";
            const bool target_id_stable = memory_target != nullptr;
            const bool non_target_id_stable = memory_non_target != nullptr;
            std::string post_apply_target_error;
            const std::optional<InspectorTargetMetadata> post_apply_target_info =
                resolve_inspector_target_metadata(load.handle, baseline_structure_edit_id,
                                                   "structure.put",
                                                   &post_apply_target_error);
            std::string post_apply_non_target_error;
            const std::optional<InspectorTargetMetadata> post_apply_non_target_info =
                resolve_inspector_target_metadata(load.handle, baseline_non_target_edit_id,
                                                   "structure.put",
                                                   &post_apply_non_target_error);
            const bool post_apply_metadata_ok = post_apply_target_info &&
                post_apply_target_info->source_distance_string == "125" &&
                std::abs(post_apply_target_info->distance_value - 125.0) < 1e-9 &&
                post_apply_target_info->expected_source_hash == baseline_source_hash &&
                post_apply_target_info->source.file_path == structure_row.source.file_path &&
                post_apply_target_info->raw_statement.find("Structure['pole'].Put") !=
                    std::string::npos;
            const bool post_apply_non_target_metadata_ok = post_apply_non_target_info &&
                post_apply_non_target_info->source_distance_string == "125" &&
                std::abs(post_apply_non_target_info->distance_value - 125.0) < 1e-9 &&
                post_apply_non_target_info->expected_source_hash == baseline_source_hash &&
                post_apply_non_target_info->source.file_path == structure_row.source.file_path &&
                (!post_apply_target_info ||
                 post_apply_non_target_info->source.line != post_apply_target_info->source.line);
            *out << "working_target_edit_id_stable=" << (target_id_stable ? 1 : 0) << "\n";
            *out << "working_non_target_edit_id_stable=" << (non_target_id_stable ? 1 : 0) << "\n";
            *out << "post_apply_baseline_id_metadata_ok=" <<
                (post_apply_metadata_ok ? 1 : 0) << "\n";
            *out << "post_apply_non_target_metadata_ok=" <<
                (post_apply_non_target_metadata_ok ? 1 : 0) << "\n";
            if (!memory_matches || !target_id_stable || !non_target_id_stable ||
                !post_apply_metadata_ok || !post_apply_non_target_metadata_ok) {
                if (!post_apply_metadata_ok) {
                    *out << "post_apply_target_error=" << post_apply_target_error << "\n";
                }
                if (!post_apply_non_target_metadata_ok) {
                    *out << "post_apply_non_target_error=" <<
                        post_apply_non_target_error << "\n";
                }
                exit_code = 6;
            }
        }
    }

    if (exit_code == 0) {
        const bool reset_ok = kv_edit_reset_memory(load.handle) != 0;
        KvMapSnapshot reset_snapshot{};
        const bool reset_snapshot_ok = kv_get_map_snapshot(
            load.handle, KV_MAP_SNAPSHOT_VERSION,
            &reset_snapshot, sizeof(reset_snapshot)) != 0;
        const bool reset_snapshot_invalidated = reset_ok && reset_snapshot_ok &&
            reset_snapshot.content_revision > applied_snapshot.content_revision &&
            reset_snapshot.geometry_revision > applied_snapshot.geometry_revision;
        MapModel reset_model = build_model_from_handle(
            load.handle, wide_to_utf8(temp_map.wstring()), LoadModelOptions{true});
        const TableRow* reset_target =
            find_structure_by_edit_id(reset_model, baseline_structure_edit_id);
        const TableRow* reset_non_target =
            find_structure_by_edit_id(reset_model, baseline_non_target_edit_id);
        const bool reset_matches = reset_ok && reset_target && reset_non_target &&
            std::abs(table_cell_number(*reset_target, "distance") - 100.0) < 1e-6 &&
            std::abs(table_cell_number(*reset_target, "x") - 1.0) < 1e-6 &&
            std::abs(table_cell_number(*reset_non_target, "distance") - 125.0) < 1e-6 &&
            std::abs(table_cell_number(*reset_non_target, "x") - 9.0) < 1e-6;
        *out << "reset_memory_ok=" << (reset_ok ? 1 : 0) << "\n";
        *out << "reset_revert_match=" << (reset_matches ? 1 : 0) << "\n";
        *out << "snapshot_reset_invalidation_ok="
             << (reset_snapshot_invalidated ? 1 : 0) << "\n";
        if (!reset_matches || !reset_snapshot_invalidated) exit_code = 7;
    }

    if (exit_code == 0) {
        Report reapply_report = typed_edit_headless::apply_to_memory(load.handle, changes);
        const bool reapply_ok = reapply_report.ok;
        *out << "reapply_to_memory_ok=" << (reapply_ok ? 1 : 0) << "\n";
        if (!reapply_ok) {
            *out << "reapply_error_count=" << reapply_report.blocking_errors.size() << "\n";
            exit_code = 8;
        }
    }

    if (exit_code == 0) {
        KvEditReportSnapshot commit_report{};
        const bool commit_call_ok = kv_edit_commit_typed(
            load.handle, &commit_report, sizeof(commit_report)) != 0;
        const bool commit_ok = commit_call_ok && commit_report.ok != 0;
        std::string commit_metadata_error;
        bool commit_metadata_merge_ok = commit_ok &&
            apply_committed_edit_report_to_model(
                local_commit_model, commit_report, commit_metadata_error);
        std::string committed_target_metadata_error;
        const std::optional<InspectorTargetMetadata> committed_target_metadata =
            resolve_inspector_target_metadata(load.handle, baseline_structure_edit_id,
                                               "structure.put",
                                               &committed_target_metadata_error);
        std::string committed_non_target_metadata_error;
        const std::optional<InspectorTargetMetadata> committed_non_target_metadata =
            resolve_inspector_target_metadata(load.handle, baseline_non_target_edit_id,
                                               "structure.put",
                                               &committed_non_target_metadata_error);
        const TableRow* local_committed_target =
            find_structure_by_edit_id(local_commit_model, baseline_structure_edit_id);
        const TableRow* local_committed_non_target =
            find_structure_by_edit_id(local_commit_model, baseline_non_target_edit_id);
        commit_metadata_merge_ok = commit_metadata_merge_ok && committed_target_metadata &&
            committed_non_target_metadata && local_committed_target &&
            local_committed_non_target &&
            local_committed_target->source.line == committed_target_metadata->source.line &&
            local_committed_non_target->source.line ==
                committed_non_target_metadata->source.line &&
            local_committed_target->source.line != local_committed_non_target->source.line;
        *out << "commit_ok=" << (commit_ok ? 1 : 0) << "\n";
        *out << "commit_metadata_merge_ok=" <<
            (commit_metadata_merge_ok ? 1 : 0) << "\n";
        if (!commit_ok || !commit_metadata_merge_ok) {
            *out << "commit_report_ok=" << (commit_report.ok ? 1 : 0)
                 << " committed_files=" << commit_report.committed_file_count
                 << " blocking_errors=" << commit_report.blocking_error_count << "\n";
            *out << "commit_metadata_error=" << commit_metadata_error << "\n"
                 << "committed_target_metadata_error=" <<
                    committed_target_metadata_error << "\n"
                 << "committed_non_target_metadata_error=" <<
                    committed_non_target_metadata_error << "\n";
            exit_code = 9;
        }
    }

    if (exit_code == 0) {
        const bool post_commit_reset_ok = kv_edit_reset_memory(load.handle) != 0;
        MapModel committed_model = build_model_from_handle(
            load.handle, wide_to_utf8(temp_map.wstring()), LoadModelOptions{true});
        const TableRow* committed_target =
            find_structure_by_edit_id(committed_model, baseline_structure_edit_id);
        const TableRow* committed_non_target =
            find_structure_by_edit_id(committed_model, baseline_non_target_edit_id);
        const bool committed_identity_stable = committed_target && committed_non_target;
        const std::string committed_source_hash = committed_target
            ? source_hash_for_path(committed_model, committed_target->source.file_path)
            : std::string{};
        std::vector<Change> second_batch_changes;
        if (committed_non_target) {
            second_batch_changes = make_update(
                *committed_non_target, committed_model, {{"x", "10"}});
        }
        const Report second_batch_report = second_batch_changes.empty()
            ? Report{}
            : typed_edit_headless::apply_to_memory(load.handle, second_batch_changes);
        const bool second_batch_ok = second_batch_report.ok;
        MapModel second_batch_model = build_model_from_handle(
            load.handle, wide_to_utf8(temp_map.wstring()), LoadModelOptions{true});
        const TableRow* second_batch_target =
            find_structure_by_edit_id(second_batch_model, baseline_structure_edit_id);
        const TableRow* second_batch_non_target =
            find_structure_by_edit_id(second_batch_model, baseline_non_target_edit_id);
        const bool second_batch_identity_stable =
            second_batch_target && second_batch_non_target;
        const bool second_batch_value_ok = second_batch_target && second_batch_non_target &&
            std::abs(table_cell_number(*second_batch_target, "x") - 9.0) < 1e-6 &&
            std::abs(table_cell_number(*second_batch_non_target, "x") - 10.0) < 1e-6;
        std::string second_batch_target_error;
        const std::optional<InspectorTargetMetadata> second_batch_target_info =
            resolve_inspector_target_metadata(load.handle, baseline_structure_edit_id,
                                               "structure.put", &second_batch_target_error);
        std::string second_batch_non_target_error;
        const std::optional<InspectorTargetMetadata> second_batch_non_target_info =
            resolve_inspector_target_metadata(load.handle, baseline_non_target_edit_id,
                                               "structure.put",
                                               &second_batch_non_target_error);
        const bool second_batch_metadata_ok = second_batch_target_info &&
            second_batch_non_target_info &&
            second_batch_target_info->source_distance_string == "125" &&
            second_batch_non_target_info->source_distance_string == "125" &&
            std::abs(second_batch_target_info->distance_value - 125.0) < 1e-9 &&
            std::abs(second_batch_non_target_info->distance_value - 125.0) < 1e-9 &&
            second_batch_target_info->raw_statement.find(",9,2,3,") != std::string::npos &&
            second_batch_non_target_info->raw_statement.find(",10,2,3,") !=
                std::string::npos;

        const bool second_batch_discard_ok = kv_edit_reset_memory(load.handle) != 0;
        MapModel discarded_model = build_model_from_handle(
            load.handle, wide_to_utf8(temp_map.wstring()), LoadModelOptions{true});
        const TableRow* discarded_target =
            find_structure_by_edit_id(discarded_model, baseline_structure_edit_id);
        const TableRow* discarded_non_target =
            find_structure_by_edit_id(discarded_model, baseline_non_target_edit_id);
        const bool discarded_identity_stable = discarded_target && discarded_non_target;
        const bool discarded_value_ok = discarded_target && discarded_non_target &&
            std::abs(table_cell_number(*discarded_target, "distance") - 125.0) < 1e-6 &&
            std::abs(table_cell_number(*discarded_target, "x") - 9.0) < 1e-6 &&
            std::abs(table_cell_number(*discarded_non_target, "distance") - 125.0) < 1e-6 &&
            std::abs(table_cell_number(*discarded_non_target, "x") - 9.0) < 1e-6;
        std::string discarded_metadata_error;
        const std::optional<InspectorTargetMetadata> discarded_metadata =
            resolve_inspector_target_metadata(load.handle, baseline_structure_edit_id,
                                               "structure.put", &discarded_metadata_error);
        const bool discarded_metadata_ok = discarded_metadata &&
            discarded_metadata->source_distance_string == "125" &&
            std::abs(discarded_metadata->distance_value - 125.0) < 1e-9 &&
            !committed_source_hash.empty() &&
            discarded_metadata->expected_source_hash == committed_source_hash;

        *out << "post_commit_reset_ok=" << (post_commit_reset_ok ? 1 : 0) << "\n";
        *out << "post_commit_identity_stable=" << (committed_identity_stable ? 1 : 0) << "\n";
        *out << "second_batch_apply_ok=" << (second_batch_ok ? 1 : 0) << "\n";
        *out << "second_batch_identity_stable=" <<
            (second_batch_identity_stable ? 1 : 0) << "\n";
        *out << "second_batch_value_ok=" << (second_batch_value_ok ? 1 : 0) << "\n";
        *out << "second_batch_metadata_ok=" << (second_batch_metadata_ok ? 1 : 0) << "\n";
        *out << "second_batch_discard_ok=" << (second_batch_discard_ok ? 1 : 0) << "\n";
        *out << "discarded_identity_stable=" << (discarded_identity_stable ? 1 : 0) << "\n";
        *out << "discarded_value_ok=" << (discarded_value_ok ? 1 : 0) << "\n";
        *out << "discarded_metadata_ok=" << (discarded_metadata_ok ? 1 : 0) << "\n";
        if (!post_commit_reset_ok || !committed_identity_stable || !second_batch_ok ||
            !second_batch_identity_stable || !second_batch_value_ok ||
            !second_batch_metadata_ok || !second_batch_discard_ok ||
            !discarded_identity_stable || !discarded_value_ok ||
            !discarded_metadata_ok) {
            if (!second_batch_ok) {
                *out << "second_batch_error_count="
                     << second_batch_report.blocking_errors.size() << "\n";
            }
            if (!second_batch_metadata_ok) {
                *out << "second_batch_target_error=" << second_batch_target_error << "\n"
                     << "second_batch_non_target_error=" <<
                        second_batch_non_target_error << "\n";
            }
            if (!discarded_metadata_ok) {
                *out << "discarded_metadata_error=" << discarded_metadata_error << "\n";
            }
            exit_code = 16;
        }
    }
    if (load.handle) kv_free(load.handle);

    if (exit_code == 0) {
        LoadResult reload = load_map_worker(wide_to_utf8(temp_map.wstring()), unit_distance,
                                            false, 0.0, 0.0, 25.0, LoadModelOptions{true});
        if (!reload.ok || reload.model.structures.size() < 2) {
            *out << "reload_error=" << reload.error << "\n";
            exit_code = 10;
        } else {
            const int matching_rows = static_cast<int>(std::count_if(
                reload.model.structures.begin(), reload.model.structures.end(),
                [](const TableRow& row) {
                    return std::abs(table_cell_number(row, "distance") - 125.0) < 1e-6 &&
                           std::abs(table_cell_number(row, "x") - 9.0) < 1e-6;
                }));
            const double distance = table_cell_number(reload.model.structures.front(), "distance");
            const double x = table_cell_number(reload.model.structures.front(), "x");
            const bool reload_matches = matching_rows == 2;
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
        const TableRow* direct_target = direct_load.ok
            ? find_structure_by_values(direct_load.model, 100.0, 1.0)
            : nullptr;
        if (!direct_load.ok || !direct_target) {
            *out << "direct_load_error=" << direct_load.error << "\n";
            exit_code = 12;
        } else {
            std::vector<Change> direct_changes = make_update(
                *direct_target, direct_load.model, {{"x", "12"}});
            Report direct_report = typed_edit_headless::direct_apply(
                direct_load.handle, direct_changes);
            const bool direct_ok = direct_report.ok;
            *out << "direct_apply_ok=" << (direct_ok ? 1 : 0) << "\n";
            if (!direct_ok) {
                *out << "direct_apply_error_count="
                     << direct_report.blocking_errors.size() << "\n";
                exit_code = 13;
            }
        }
        if (direct_load.handle) kv_free(direct_load.handle);
    }

    if (exit_code == 0) {
        LoadResult direct_reload = load_map_worker(wide_to_utf8(temp_direct_map.wstring()), unit_distance,
                                                   false, 0.0, 0.0, 25.0, LoadModelOptions{true});
        const TableRow* direct_updated = direct_reload.ok
            ? find_structure_by_values(direct_reload.model, 100.0, 12.0)
            : nullptr;
        if (!direct_reload.ok || !direct_updated) {
            *out << "direct_reload_error=" << direct_reload.error << "\n";
            exit_code = 14;
        } else {
            const double direct_x = table_cell_number(*direct_updated, "x");
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

using EditChange = typed_edit_headless::Change;
using EditField = typed_edit_headless::Field;
using EditReport = typed_edit_headless::Report;
using EditResolution = typed_edit_headless::Resolution;

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

std::string edit_number(double value) {
    std::ostringstream out;
    out << std::setprecision(15) << value;
    std::string text = out.str();
    return text == "-0" ? "0" : text;
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
    std::string row_kind;
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

ReportFacts report_facts(const EditReport& report) {
    ReportFacts facts;
    facts.ok = report.ok;
    facts.full_reparse_ok = report.full_reparse_ok;
    facts.target_distance_match_count = report.target_distance_match_count;
    facts.non_target_changed_count = report.non_target_changed_count;
    facts.created_distance_block_count = report.created_distance_block_count;
    facts.reused_distance_block_count = report.reused_distance_block_count;
    facts.distance_group_count = report.distance_group_count;
    facts.blocking_error_count = report.blocking_errors.size();
    facts.resolution_request_count = report.resolution_requests.size();
    facts.changed_file_count = report.changed_files.size();
    facts.preview_count = report.previews.size();
    facts.validation_fingerprint = report.validation_fingerprint;
    return facts;
}

bool request_affects(const EditResolution& request, const std::string& edit_id) {
    return std::find(request.affected_edit_ids.begin(), request.affected_edit_ids.end(),
                     edit_id) != request.affected_edit_ids.end();
}

std::string recommended_boundary_token(const EditResolution& request) {
    for (const typed_edit_headless::Boundary& boundary : request.allowed_boundaries) {
        if (boundary.recommended) return boundary.token;
    }
    return {};
}

std::string first_blocking_error(const EditReport& report) {
    return report.blocking_errors.empty() ? std::string{} : report.blocking_errors.front();
}

std::vector<EditChange> build_changes(
    const std::vector<StructureEdit>& edits,
    const std::map<std::string, ResolutionChoice>& resolutions = {}) {
    std::vector<EditChange> output;
    output.reserve(edits.size());
    for (size_t i = 0; i < edits.size(); ++i) {
        const StructureEdit& edit = edits[i];
        EditChange change = typed_edit_headless::update(
            "headless-distance-" + std::to_string(i), edit.edit_id,
            edit.expected_source_hash, {{"distance", edit_number(edit.target_distance)}});
        auto resolution = resolutions.find(edit.edit_id);
        if (resolution != resolutions.end()) {
            const ResolutionChoice& choice = resolution->second;
            change.distance_resolution_key = choice.resolution_key;
            change.distance_boundary_token = choice.boundary_token;
            change.distance_expression = choice.distance_expression;
            if (choice.confirm_environment_mismatch) {
                change.flags |= KV_EDIT_CHANGE_CONFIRM_ENVIRONMENT_MISMATCH;
            }
        }
        output.push_back(std::move(change));
    }
    return output;
}

std::vector<EditChange> build_field_updates(
    const std::vector<StructureEdit>& edits,
    const std::string& field,
    const std::vector<std::string>& values) {
    if (edits.size() != values.size()) {
        throw std::runtime_error("field-update fixture input sizes differ");
    }
    std::vector<EditChange> output;
    output.reserve(edits.size());
    for (size_t i = 0; i < edits.size(); ++i) {
        output.push_back(typed_edit_headless::update(
            "headless-field-update-" + std::to_string(i), edits[i].edit_id,
            edits[i].expected_source_hash, {{field, values[i]}}));
    }
    return output;
}

std::vector<EditChange> build_single_field_update(
    const StructureEdit& edit, const std::string& field, const std::string& value) {
    return build_field_updates({edit}, field, {value});
}

KvMapSnapshot current_map_snapshot(void* handle) {
    KvMapSnapshot snapshot{};
    if (!kv_get_map_snapshot(handle, KV_MAP_SNAPSHOT_VERSION,
                             &snapshot, sizeof(snapshot))) {
        const char* error = kv_get_last_error();
        throw std::runtime_error(error ? error : "typed map snapshot failed");
    }
    if (snapshot.version != KV_MAP_SNAPSHOT_VERSION ||
        snapshot.structure_size < sizeof(KvMapSnapshot)) {
        throw std::runtime_error("typed map snapshot version or size mismatch");
    }
    return snapshot;
}

std::string snapshot_text(const KvMapSnapshot& snapshot, KvStringRef ref) {
    if (ref.length == 0) return {};
    if (!snapshot.string_data || ref.offset > snapshot.string_size ||
        ref.length > snapshot.string_size - ref.offset) {
        throw std::runtime_error("typed map snapshot string reference is out of range");
    }
    return std::string(snapshot.string_data + static_cast<size_t>(ref.offset),
                       static_cast<size_t>(ref.length));
}

std::string snapshot_value_text(const KvMapSnapshot& snapshot, const KvValue& value) {
    switch (value.kind) {
        case KV_VALUE_NUMBER: return edit_number(value.number_value);
        case KV_VALUE_STRING: return snapshot_text(snapshot, value.string_value);
        case KV_VALUE_CONTINUE: return "c";
        default: return {};
    }
}

std::string metadata_source_file(const KvMapSnapshot& snapshot,
                                 const KvRowMetadata& metadata) {
    if (metadata.source_file_index == KV_INDEX_NONE) return {};
    if (!snapshot.source_files ||
        metadata.source_file_index >= snapshot.source_file_count) {
        throw std::runtime_error("typed map snapshot source index is out of range");
    }
    return snapshot_text(
        snapshot, snapshot.source_files[metadata.source_file_index].file_path);
}

std::vector<StructureEdit> structure_rows_from_snapshot(void* handle) {
    const KvMapSnapshot snapshot = current_map_snapshot(handle);
    if (snapshot.structure_put_count != 0 && !snapshot.structure_puts) {
        throw std::runtime_error("typed map snapshot has null structure array");
    }
    std::vector<StructureEdit> result;
    result.reserve(static_cast<size_t>(snapshot.structure_put_count));
    for (std::uint64_t i = 0; i < snapshot.structure_put_count; ++i) {
        const KvStructurePutRow& row = snapshot.structure_puts[i];
        StructureEdit edit;
        edit.edit_id = snapshot_text(snapshot, row.metadata.edit_id);
        edit.row_kind = "structure.put";
        edit.source_file = metadata_source_file(snapshot, row.metadata);
        edit.source_line = row.metadata.line;
        edit.raw_text_preview = snapshot_text(snapshot, row.metadata.raw_text_preview);
        edit.structure_key = snapshot_value_text(snapshot, row.structure_key);
        edit.old_distance = row.distance;
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

std::vector<StructureEdit> station_put_rows_from_snapshot(void* handle) {
    const KvMapSnapshot snapshot = current_map_snapshot(handle);
    if (snapshot.station_put_count != 0 && !snapshot.station_puts) {
        throw std::runtime_error("typed map snapshot has null station-put array");
    }
    std::vector<StructureEdit> result;
    result.reserve(static_cast<size_t>(snapshot.station_put_count));
    for (std::uint64_t i = 0; i < snapshot.station_put_count; ++i) {
        const KvStationPutRow& row = snapshot.station_puts[i];
        StructureEdit edit;
        edit.edit_id = snapshot_text(snapshot, row.metadata.edit_id);
        edit.row_kind = "station.put";
        edit.source_file = metadata_source_file(snapshot, row.metadata);
        edit.source_line = row.metadata.line;
        edit.raw_text_preview = snapshot_text(snapshot, row.metadata.raw_text_preview);
        edit.structure_key = snapshot_value_text(snapshot, row.station_key);
        edit.old_distance = row.distance;
        if (!edit.edit_id.empty() && !edit.source_file.empty()) result.push_back(std::move(edit));
    }
    std::stable_sort(result.begin(), result.end(), [](const StructureEdit& a,
                                                       const StructureEdit& b) {
        if (a.old_distance != b.old_distance) return a.old_distance < b.old_distance;
        return a.source_line < b.source_line;
    });
    return result;
}

std::string source_snapshot_fingerprint(void* handle) {
    const KvMapSnapshot snapshot = current_map_snapshot(handle);
    if (snapshot.source_file_count != 0 && !snapshot.source_files) {
        throw std::runtime_error("typed map snapshot has null source-file array");
    }
    std::string content;
    for (std::uint64_t i = 0; i < snapshot.source_file_count; ++i) {
        const std::string path = snapshot_text(snapshot, snapshot.source_files[i].file_path);
        const char* raw = kv_get_source_text(handle, path.c_str());
        if (!raw) throw std::runtime_error("source text lookup failed while fingerprinting");
        const std::string text(raw);
        kv_free_string(raw);
        content.append(std::to_string(path.size())).push_back(':');
        content.append(path);
        content.append(std::to_string(text.size())).push_back(':');
        content.append(text);
    }
    return hash_text(content);
}

bool populate_target_info(void* handle, StructureEdit& edit, std::string& error) {
    const std::optional<InspectorTargetMetadata> info =
        resolve_inspector_target_metadata(handle, edit.edit_id, edit.row_kind, &error);
    if (!info) return false;
    edit.expected_source_hash = info->expected_source_hash;
    edit.original_distance_expression = info->source_distance_string;
    edit.elements_for_statement = info->elements_for_statement;
    if (edit.expected_source_hash.empty() || edit.elements_for_statement != 1) {
        error = "target is not a unique source-backed editable statement";
        return false;
    }
    return true;
}

bool candidate_has_recommended_resolution(const EditReport& report,
                                          const StructureEdit& candidate,
                                          int& score) {
    score = 99;
    if (!report.blocking_errors.empty()) return false;
    ReportFacts facts = report_facts(report);
    if (facts.ok && facts.full_reparse_ok && facts.target_distance_match_count == 1 &&
        facts.non_target_changed_count == 0) {
        score = 0;
        return true;
    }
    for (const EditResolution& request : report.resolution_requests) {
        if (!request_affects(request, candidate.edit_id) ||
            recommended_boundary_token(request).empty()) {
            continue;
        }
        score = request.reason == "variableHasMultipleContextValues" ? 1 : 2;
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
        try {
            ++dry_run_attempts;
            EditReport report = typed_edit_headless::dry_run(
                handle, build_changes({trial}));
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
    const EditReport& report,
    const std::vector<StructureEdit>& edits,
    std::map<std::string, ResolutionChoice>& resolutions,
    int& variable_expression_resolution_count,
    std::string& error) {
    std::map<std::string, const StructureEdit*> by_id;
    for (const StructureEdit& edit : edits) by_id[edit.edit_id] = &edit;
    bool added = false;
    for (const EditResolution& request : report.resolution_requests) {
        const std::string& key = request.resolution_key;
        const std::string& reason = request.reason;
        const std::string boundary = recommended_boundary_token(request);
        if (key.empty() || boundary.empty()) {
            error = "resolution request has no recommended parser boundary: " + reason;
            return false;
        }
        for (const std::string& edit_id : request.affected_edit_ids) {
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
            choice.confirm_environment_mismatch = request.can_confirm_reuse;
            added = true;
        }
    }
    return added;
}

bool preview_has_local_wrapper(const EditReport& report,
                               const std::vector<StructureEdit>& edits) {
    for (const std::string& warning : report.warnings) {
        if (warning.find("preserves the original distance expression") !=
            std::string::npos) {
            return true;
        }
    }
    for (const typed_edit_headless::Preview& preview : report.previews) {
        const std::string& after = preview.after;
        std::vector<std::string> lines;
        size_t start = 0;
        while (start <= after.size()) {
            size_t end = after.find('\n', start);
            std::string line = trim_ascii(after.substr(
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
                lines.back() == trim_ascii(edit.original_distance_expression) + ";") {
                return true;
            }
        }
    }
    return false;
}

size_t count_preview_occurrences(const EditReport& report, const std::string& needle) {
    size_t count = 0;
    for (const typed_edit_headless::Preview& preview : report.previews) {
        const std::string& after = preview.after;
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
        const std::string file_key = ascii_lower(source.source_file);
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
        const std::string wanted = ascii_lower(file_tail);
        for (const StructureEdit& row : rows) {
            if (ascii_lower(row.source_file).find(wanted) == std::string::npos) continue;
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

bool report_has_reason(const EditReport& report, const std::string& reason) {
    for (const EditResolution& request : report.resolution_requests) {
        if (request.reason == reason) return true;
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
            std::vector<StructureEdit> edits = structure_rows_from_snapshot(handle.value);
            if (edits.size() != 2) throw std::runtime_error("increasing fixture did not yield two structures");
            for (StructureEdit& edit : edits) {
                std::string error;
                if (!populate_target_info(handle.value, edit, error)) throw std::runtime_error(error);
                edit.target_distance = 150.0;
                edit.increment = static_cast<int>(edit.target_distance - edit.old_distance);
            }
            EditReport report = typed_edit_headless::dry_run(
                handle.value, build_changes(edits));
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
            std::vector<StructureEdit> edits = structure_rows_from_snapshot(handle.value);
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
            EditReport report = typed_edit_headless::dry_run(
                handle.value, build_changes({target}));
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
            std::vector<StructureEdit> edits = structure_rows_from_snapshot(handle.value);
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

            EditReport partial = typed_edit_headless::dry_run(
                handle.value, build_changes({edits.front()}));
            ReportFacts partial_summary = report_facts(partial);
            facts.repeated_include_partial_blocked =
                !partial_summary.ok && partial_summary.resolution_request_count > 0 &&
                partial_summary.changed_file_count == 0 &&
                report_has_reason(
                    partial, "physicalSourceHasIncompatibleIncludeContexts");

            EditReport complete = typed_edit_headless::dry_run(
                handle.value, build_changes(edits));
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
            std::vector<StructureEdit> edits = structure_rows_from_snapshot(handle.value);
            if (edits.size() != 1) throw std::runtime_error("unordered fixture did not yield one structure");
            std::string error;
            if (!populate_target_info(handle.value, edits.front(), error)) throw std::runtime_error(error);
            edits.front().target_distance = 75.0;
            EditReport report = typed_edit_headless::dry_run(
                handle.value, build_changes(edits));
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
            std::vector<StructureEdit> edits = structure_rows_from_snapshot(handle.value);
            if (edits.size() != 1) throw std::runtime_error("environment fixture did not yield one structure");
            std::string error;
            if (!populate_target_info(handle.value, edits.front(), error)) throw std::runtime_error(error);
            edits.front().target_distance = 150.0;
            EditReport initial = typed_edit_headless::dry_run(
                handle.value, build_changes(edits));
            std::map<std::string, ResolutionChoice> resolutions;
            int expression_count = 0;
            if (!report_has_reason(initial, "variableHasMultipleContextValues") ||
                !inject_report_resolutions(initial, edits, resolutions, expression_count, error)) {
                throw std::runtime_error(error.empty()
                    ? "environment fixture did not request the expected variable resolution"
                    : error);
            }
            EditReport forced = typed_edit_headless::dry_run(
                handle.value, build_changes(edits, resolutions));
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
            std::vector<StructureEdit> edits = station_put_rows_from_snapshot(handle.value);
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
            EditReport report = typed_edit_headless::dry_run(
                handle.value, build_changes({target}));
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
            std::vector<StructureEdit> edits = structure_rows_from_snapshot(handle.value);
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
            EditReport report;
            try {
                report = typed_edit_headless::direct_apply(
                    handle.value, build_field_updates(edits, "x", {"11", "22"}));
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

            EditReport success_report = typed_edit_headless::direct_apply(
                handle.value, build_field_updates(edits, "x", {"11", "22"}));
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
            std::vector<StructureEdit> rows = structure_rows_from_snapshot(handle.value);
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

            EditReport section_request = typed_edit_headless::dry_run(
                handle.value, build_changes(edits));
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
            EditReport expression_request = typed_edit_headless::dry_run(
                handle.value, build_changes(edits, resolutions));
            const bool requested_manual_expression =
                report_has_reason(expression_request, "variableHasMultipleContextValues") ||
                report_has_reason(expression_request, "distanceExpressionRequiresManualEdit");
            facts.staged_variable_resolution_count = static_cast<int>(
                expression_request.resolution_requests.size());
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
            EditReport resolved = typed_edit_headless::dry_run(
                handle.value, build_changes(edits, resolutions));
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
            std::vector<StructureEdit> edits = structure_rows_from_snapshot(handle.value);
            if (edits.size() != 1) {
                throw std::runtime_error(
                    "direct apply fixture did not yield one structure");
            }
            std::string error;
            if (!populate_target_info(handle.value, edits.front(), error)) {
                throw std::runtime_error(error);
            }
            EditReport first_report = typed_edit_headless::direct_apply(
                handle.value, build_single_field_update(edits.front(), "x", "12"));
            ReportFacts first_summary = report_facts(first_report);
            if (first_summary.ok && first_summary.full_reparse_ok) {
                const KvMapSnapshot first_snapshot = current_map_snapshot(handle.value);
                std::vector<StructureEdit> first_rows =
                    structure_rows_from_snapshot(handle.value);
                const double first_x = first_snapshot.structure_put_count == 0
                    ? std::numeric_limits<double>::quiet_NaN()
                    : first_snapshot.structure_puts[0].x;
                facts.direct_apply_handle_advanced =
                    first_rows.size() == 1 && std::fabs(first_x - 12.0) < 1e-9;

                if (first_rows.size() == 1 &&
                    populate_target_info(handle.value, first_rows.front(), error)) {
                    EditReport second_report = typed_edit_headless::direct_apply(
                        handle.value,
                        build_single_field_update(first_rows.front(), "x", "13"));
                    ReportFacts second_summary = report_facts(second_report);
                    const KvMapSnapshot second_snapshot = current_map_snapshot(handle.value);
                    const double second_x = second_snapshot.structure_put_count == 0
                        ? std::numeric_limits<double>::quiet_NaN()
                        : second_snapshot.structure_puts[0].x;
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
    int post_apply_metadata_resolved_count = 0;
    int post_apply_source_distance_string_count = 0;
    int post_apply_metadata_distance_match_count = 0;
    int post_apply_metadata_identity_match_count = 0;
    int post_apply_metadata_fallback_required_count = 0;
    bool no_local_wrapper = false;
    bool reset_ok = false;
    bool reset_snapshot_fingerprint_restored = false;
    std::string initial_source_fingerprint;
    std::string reset_source_fingerprint;
    bool commit_attempted = false;
    bool commit_ok = false;
    std::vector<StructureEdit> selected;
    FixtureFacts fixtures;
    std::string error;

    bool passed() const {
        return error.empty() && selected.size() == 5 && selected_sig_context_count >= 2 &&
               final_dry_run_ok && apply_to_memory_ok && full_reparse_ok &&
               target_distance_match_count == 5 && non_target_changed_count == 0 &&
               post_apply_metadata_resolved_count == 5 &&
               post_apply_source_distance_string_count == 5 &&
               post_apply_metadata_distance_match_count == 5 &&
               post_apply_metadata_identity_match_count == 5 &&
               post_apply_metadata_fallback_required_count == 0 &&
               distance_group_count == 5 &&
               created_distance_block_count + reused_distance_block_count == distance_group_count &&
               no_local_wrapper && reset_ok && reset_snapshot_fingerprint_restored &&
               fixtures.passed() && (!commit_requested || (commit_attempted && commit_ok));
    }
};

void write_batch_result(std::ostream& out, const BatchRunFacts& facts) {
    auto boolean = [](bool value) { return value ? 1 : 0; };
    out << "command=debug-headless-distance-edit-batch\n"
        << "path=" << facts.path << "\n"
        << "commit_requested=" << boolean(facts.commit_requested) << "\n"
        << "candidate_dry_run_attempts=" << facts.candidate_dry_run_attempts << "\n"
        << "selected_count=" << facts.selected.size() << "\n"
        << "selected_sig_context_count=" << facts.selected_sig_context_count << "\n"
        << "initial_resolution_request_count="
        << facts.initial_resolution_request_count << "\n";
    for (size_t i = 0; i < facts.initial_resolution_reasons.size(); ++i) {
        out << "initial_resolution_reason." << i << "="
            << facts.initial_resolution_reasons[i] << "\n";
    }
    out << "resolution_round_count=" << facts.resolution_round_count << "\n"
        << "variable_expression_resolution_count="
        << facts.variable_expression_resolution_count << "\n"
        << "final_dry_run_ok=" << boolean(facts.final_dry_run_ok) << "\n"
        << "apply_to_memory_ok=" << boolean(facts.apply_to_memory_ok) << "\n"
        << "full_reparse_ok=" << boolean(facts.full_reparse_ok) << "\n"
        << "target_distance_match_count=" << facts.target_distance_match_count << "\n"
        << "non_target_changed_count=" << facts.non_target_changed_count << "\n"
        << "distance_group_count=" << facts.distance_group_count << "\n"
        << "created_distance_block_count=" << facts.created_distance_block_count << "\n"
        << "reused_distance_block_count=" << facts.reused_distance_block_count << "\n"
        << "post_apply_metadata_resolved_count="
        << facts.post_apply_metadata_resolved_count << "\n"
        << "post_apply_source_distance_string_count="
        << facts.post_apply_source_distance_string_count << "\n"
        << "post_apply_metadata_distance_match_count="
        << facts.post_apply_metadata_distance_match_count << "\n"
        << "post_apply_metadata_identity_match_count="
        << facts.post_apply_metadata_identity_match_count << "\n"
        << "post_apply_metadata_fallback_required_count="
        << facts.post_apply_metadata_fallback_required_count << "\n"
        << "no_local_wrapper=" << boolean(facts.no_local_wrapper) << "\n"
        << "reset_ok=" << boolean(facts.reset_ok) << "\n"
        << "reset_snapshot_fingerprint_restored="
        << boolean(facts.reset_snapshot_fingerprint_restored) << "\n"
        << "initial_source_fingerprint=" << facts.initial_source_fingerprint << "\n"
        << "reset_source_fingerprint=" << facts.reset_source_fingerprint << "\n"
        << "commit_attempted=" << boolean(facts.commit_attempted) << "\n"
        << "commit_ok=" << boolean(facts.commit_ok) << "\n";
    for (size_t i = 0; i < facts.selected.size(); ++i) {
        const StructureEdit& edit = facts.selected[i];
        out << "selected." << i << ".edit_id=" << edit.edit_id << "\n"
            << "selected." << i << ".source_file=" << edit.source_file << "\n"
            << "selected." << i << ".source_line=" << edit.source_line << "\n"
            << "selected." << i << ".structure_key=" << edit.structure_key << "\n"
            << "selected." << i << ".old_distance="
            << edit_number(edit.old_distance) << "\n"
            << "selected." << i << ".target_distance="
            << edit_number(edit.target_distance) << "\n"
            << "selected." << i << ".increment=" << edit.increment << "\n"
            << "selected." << i << ".selection_score=" << edit.selection_score << "\n"
            << "selected." << i << ".distance_expression="
            << edit.original_distance_expression << "\n";
    }
    out << "fixture.increasing_same_target_one_block="
        << boolean(facts.fixtures.increasing_same_target_one_block) << "\n"
        << "fixture.canonical_identical_same_target_one_block="
        << boolean(facts.fixtures.canonical_identical_same_target_one_block) << "\n"
        << "fixture.terminal_unique_target_reused="
        << boolean(facts.fixtures.terminal_unique_target_reused) << "\n"
        << "fixture.repeated_include_partial_blocked="
        << boolean(facts.fixtures.repeated_include_partial_blocked) << "\n"
        << "fixture.repeated_include_all_targets_coalesced="
        << boolean(facts.fixtures.repeated_include_all_targets_coalesced) << "\n"
        << "fixture.increasing_target_match_count="
        << facts.fixtures.increasing_target_match_count << "\n"
        << "fixture.unordered_requires_resolution_without_patch="
        << boolean(facts.fixtures.unordered_requires_resolution_without_patch) << "\n"
        << "fixture.unordered_resolution_count="
        << facts.fixtures.unordered_resolution_count << "\n"
        << "fixture.variable_environment_change_blocked="
        << boolean(facts.fixtures.variable_environment_change_blocked) << "\n"
        << "fixture.environment_blocking_error_count="
        << facts.fixtures.environment_blocking_error_count << "\n"
        << "fixture.derived_station_collision_blocked="
        << boolean(facts.fixtures.derived_station_collision_blocked) << "\n"
        << "fixture.derived_state_blocking_error_count="
        << facts.fixtures.derived_state_blocking_error_count << "\n"
        << "fixture.staged_variable_resolution_succeeded="
        << boolean(facts.fixtures.staged_variable_resolution_succeeded) << "\n"
        << "fixture.staged_variable_resolution_count="
        << facts.fixtures.staged_variable_resolution_count << "\n"
        << "fixture.direct_apply_handle_advanced="
        << boolean(facts.fixtures.direct_apply_handle_advanced) << "\n"
        << "fixture.direct_apply_second_update_succeeded="
        << boolean(facts.fixtures.direct_apply_second_update_succeeded) << "\n"
        << "fixture.transactional_apply_failure_preserved_files="
        << boolean(facts.fixtures.transactional_apply_failure_preserved_files) << "\n"
        << "fixture.transactional_two_file_apply_succeeded="
        << boolean(facts.fixtures.transactional_two_file_apply_succeeded) << "\n"
        << "fixture.error=" << facts.fixtures.error << "\n"
        << "error=" << facts.error << "\n"
        << "result=" << (facts.passed() ? "PASS" : "FAIL") << "\n";
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

        facts.initial_source_fingerprint = source_snapshot_fingerprint(handle.value);
        std::vector<StructureEdit> rows = structure_rows_from_snapshot(handle.value);
        facts.selected = select_real_map_edits(
            handle.value, rows, facts.candidate_dry_run_attempts,
            facts.selected_sig_context_count);

        std::map<std::string, ResolutionChoice> resolutions;
        std::vector<EditChange> final_changes = build_changes(facts.selected, resolutions);
        EditReport final_report = typed_edit_headless::dry_run(handle.value, final_changes);
        facts.initial_resolution_request_count = static_cast<int>(
            final_report.resolution_requests.size());
        for (const EditResolution& request : final_report.resolution_requests) {
            std::string summary = request.reason;
            if (!request.variable_name.empty()) summary += ":" + request.variable_name;
            facts.initial_resolution_reasons.push_back(std::move(summary));
        }
        while (!final_report.resolution_requests.empty()) {
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
            final_changes = build_changes(facts.selected, resolutions);
            final_report = typed_edit_headless::dry_run(handle.value, final_changes);
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

        EditReport apply_report = typed_edit_headless::apply_to_memory(
            handle.value, final_changes);
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

        for (const StructureEdit& edit : facts.selected) {
            std::string metadata_error;
            const std::optional<InspectorTargetMetadata> metadata =
                resolve_inspector_target_metadata(
                    handle.value, edit.edit_id, "structure.put", &metadata_error);
            if (!metadata) {
                ++facts.post_apply_metadata_fallback_required_count;
                continue;
            }
            ++facts.post_apply_metadata_resolved_count;
            if (!metadata->source_distance_string.empty()) {
                ++facts.post_apply_source_distance_string_count;
            }
            const double distance_tolerance =
                1e-9 * std::max(1.0, std::abs(edit.target_distance));
            if (std::isfinite(metadata->distance_value) &&
                std::abs(metadata->distance_value - edit.target_distance) <=
                    distance_tolerance) {
                ++facts.post_apply_metadata_distance_match_count;
            }
            if (metadata->row_kind == "structure.put" &&
                metadata->source.file_path == edit.source_file &&
                metadata->expected_source_hash == edit.expected_source_hash &&
                metadata->raw_statement.find("Structure[") != std::string::npos &&
                metadata->raw_statement.find(edit.structure_key) != std::string::npos) {
                ++facts.post_apply_metadata_identity_match_count;
            }
        }
        if (facts.post_apply_metadata_resolved_count != 5 ||
            facts.post_apply_source_distance_string_count != 5 ||
            facts.post_apply_metadata_distance_match_count != 5 ||
            facts.post_apply_metadata_identity_match_count != 5 ||
            facts.post_apply_metadata_fallback_required_count != 0) {
            throw std::runtime_error(
                "post-Apply baseline editId metadata did not remain resolvable");
        }

        facts.reset_ok = kv_edit_reset_memory(handle.value) != 0;
        if (!facts.reset_ok) {
            const char* error = kv_get_last_error();
            throw std::runtime_error(error ? error : "kv_edit_reset_memory failed");
        }
        facts.reset_source_fingerprint = source_snapshot_fingerprint(handle.value);
        facts.reset_snapshot_fingerprint_restored =
            facts.reset_source_fingerprint == facts.initial_source_fingerprint;

        facts.fixtures = run_fixture_checks(options.unit_distance);
        if (!facts.reset_snapshot_fingerprint_restored) {
            throw std::runtime_error("reset source snapshot fingerprint differs from baseline");
        }

        if (!facts.fixtures.passed()) {
            throw std::runtime_error(facts.fixtures.error.empty()
                ? "one or more distance planner fixture checks failed"
                : facts.fixtures.error);
        }

        if (options.commit) {
            facts.commit_attempted = true;
            EditReport commit_apply = typed_edit_headless::apply_to_memory(
                handle.value, final_changes);
            ReportFacts reapplied = report_facts(commit_apply);
            if (!reapplied.ok || !reapplied.full_reparse_ok ||
                reapplied.target_distance_match_count != 5 ||
                reapplied.non_target_changed_count != 0) {
                std::string detail = first_blocking_error(commit_apply);
                throw std::runtime_error(detail.empty()
                    ? "pre-commit apply-to-memory validation failed"
                    : detail);
            }
            EditReport commit_report = typed_edit_headless::commit(handle.value);
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

namespace station_list_edit_headless {

using EditChange = typed_edit_headless::Change;
using EditField = typed_edit_headless::Field;
using EditReport = typed_edit_headless::Report;
using MapHandle = distance_batch_headless::MapHandle;

struct StationRow {
    std::string edit_id;
    std::string source_file;
    int source_line = 0;
    std::array<std::string, 13> values{};
    std::string raw_statement;
    std::string expected_source_hash;
};

struct SourceFacts {
    std::string path;
    std::string encoding;
    std::string newline;
    std::string source_hash;
    std::uint64_t byte_length = 0;
};

struct Facts {
    std::string map_path;
    std::string target_file;
    std::string original_source_hash;
    std::string committed_source_hash;
    std::string original_snapshot_fingerprint;
    std::string reset_snapshot_fingerprint;
    std::string encoding;
    std::string newline;
    size_t baseline_station_count = 0;
    size_t baseline_target_row_count = 0;
    size_t baseline_physical_line_count = 0;
    size_t committed_physical_line_count = 0;
    int selected_first_line = 0;
    int selected_last_line = 0;
    std::array<std::string, 6> selected_edit_ids{};
    bool commit_requested = false;
    bool first_move_up_disabled = false;
    bool last_move_down_disabled = false;
    bool opposite_moves_equivalent = false;
    bool full_row_templates_swapped = false;
    bool clear_normal_cell_draft_ok = false;
    bool clear_key_draft_ok = false;
    bool delete_row_draft_ok = false;
    bool dry_run_ok = false;
    bool dry_run_disk_unchanged = false;
    bool apply_memory_ok = false;
    bool apply_memory_snapshot_ok = false;
    bool apply_memory_disk_unchanged = false;
    bool empty_key_row_persisted = false;
    bool empty_key_not_registered = false;
    bool reset_ok = false;
    bool reset_snapshot_restored = false;
    bool reset_disk_unchanged = false;
    bool commit_attempted = false;
    bool commit_ok = false;
    bool committed_snapshot_ok = false;
    bool committed_stable_edit_ids = false;
    bool committed_disk_changed = false;
    bool committed_header_encoding_newline_preserved = false;
    bool committed_full_rows_swapped = false;
    bool committed_clear_cells_persisted = false;
    bool committed_delete_removed_physical_line = false;
    bool committed_sentinel_and_suffix_preserved = false;
    bool committed_non_target_semantics_ok = false;
    std::string error;

    bool passed() const {
        const bool memory = first_move_up_disabled && last_move_down_disabled &&
            opposite_moves_equivalent && full_row_templates_swapped &&
            clear_normal_cell_draft_ok && clear_key_draft_ok &&
            delete_row_draft_ok && dry_run_ok && dry_run_disk_unchanged &&
            apply_memory_ok && apply_memory_snapshot_ok &&
            apply_memory_disk_unchanged && empty_key_row_persisted &&
            empty_key_not_registered && reset_ok && reset_snapshot_restored &&
            reset_disk_unchanged;
        const bool committed = !commit_requested ||
            (commit_attempted && commit_ok && committed_snapshot_ok &&
             committed_stable_edit_ids && committed_disk_changed &&
             committed_header_encoding_newline_preserved &&
             committed_full_rows_swapped && committed_clear_cells_persisted &&
             committed_delete_removed_physical_line &&
             committed_sentinel_and_suffix_preserved &&
             committed_non_target_semantics_ok);
        return error.empty() && memory && committed;
    }
};

std::string lower_ascii_copy(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

bool same_path(const std::string& left, const std::string& right) {
    const std::filesystem::path a(utf8_to_wide(left));
    const std::filesystem::path b(utf8_to_wide(right));
    return lower_ascii_copy(wide_to_utf8(a.lexically_normal().wstring())) ==
        lower_ascii_copy(wide_to_utf8(b.lexically_normal().wstring()));
}

std::vector<StationRow> collect_station_rows(void* handle) {
    const KvMapSnapshot snapshot =
        distance_batch_headless::current_map_snapshot(handle);
    if (snapshot.station_list_count != 0 && !snapshot.station_list) {
        throw std::runtime_error("typed map snapshot has null Station.List array");
    }
    std::vector<StationRow> rows;
    rows.reserve(static_cast<size_t>(snapshot.station_list_count));
    for (std::uint64_t index = 0; index < snapshot.station_list_count; ++index) {
        const KvStationListRow& source = snapshot.station_list[index];
        StationRow row;
        row.edit_id =
            distance_batch_headless::snapshot_text(snapshot, source.metadata.edit_id);
        row.source_file =
            distance_batch_headless::metadata_source_file(snapshot, source.metadata);
        row.source_line = source.metadata.line;
        for (size_t field = 0; field < row.values.size(); ++field) {
            row.values[field] =
                distance_batch_headless::snapshot_text(snapshot, source.fields[field]);
        }
        rows.push_back(std::move(row));
    }
    for (StationRow& row : rows) {
        std::string metadata_error;
        const std::optional<InspectorTargetMetadata> metadata =
            resolve_inspector_target_metadata(
                handle, row.edit_id, "station.list", &metadata_error);
        if (!metadata || metadata->elements_for_statement != 1) {
            throw std::runtime_error(metadata_error.empty()
                ? "Station.List row does not have unique editable metadata"
                : metadata_error);
        }
        row.raw_statement = metadata->raw_statement;
        row.expected_source_hash = metadata->expected_source_hash;
    }
    std::stable_sort(rows.begin(), rows.end(), [](const StationRow& left,
                                                  const StationRow& right) {
        if (left.source_file != right.source_file) {
            return left.source_file < right.source_file;
        }
        return left.source_line < right.source_line;
    });
    return rows;
}

SourceFacts source_facts(void* handle, const std::string& path) {
    const KvMapSnapshot snapshot =
        distance_batch_headless::current_map_snapshot(handle);
    for (std::uint64_t index = 0; index < snapshot.source_file_count; ++index) {
        const KvSourceFileRow& source = snapshot.source_files[index];
        const std::string candidate =
            distance_batch_headless::snapshot_text(snapshot, source.file_path);
        if (!same_path(candidate, path)) continue;
        return {
            candidate,
            distance_batch_headless::snapshot_text(snapshot, source.encoding),
            distance_batch_headless::snapshot_text(snapshot, source.newline),
            distance_batch_headless::snapshot_text(snapshot, source.source_hash),
            source.byte_length,
        };
    }
    throw std::runtime_error("Station.List source file is absent from the snapshot");
}

std::vector<std::string> physical_lines(const std::string& bytes) {
    std::vector<std::string> lines;
    size_t begin = 0;
    for (size_t index = 0; index < bytes.size(); ++index) {
        if (bytes[index] != '\n' && bytes[index] != '\r') continue;
        lines.push_back(bytes.substr(begin, index - begin));
        if (bytes[index] == '\r' && index + 1 < bytes.size() &&
            bytes[index + 1] == '\n') {
            ++index;
        }
        begin = index + 1;
    }
    if (begin < bytes.size()) lines.push_back(bytes.substr(begin));
    return lines;
}

std::pair<size_t, size_t> newline_counts(const std::string& bytes) {
    size_t crlf = 0;
    size_t other = 0;
    for (size_t index = 0; index < bytes.size(); ++index) {
        if (bytes[index] == '\r' && index + 1 < bytes.size() &&
            bytes[index + 1] == '\n') {
            ++crlf;
            ++index;
        } else if (bytes[index] == '\r' || bytes[index] == '\n') {
            ++other;
        }
    }
    return {crlf, other};
}

std::vector<std::string> csv_values(const std::string& line) {
    std::vector<std::string> result;
    std::string field;
    bool quoted = false;
    for (size_t index = 0; index < line.size(); ++index) {
        const char ch = line[index];
        if (ch == '"') {
            if (quoted && index + 1 < line.size() && line[index + 1] == '"') {
                field.push_back('"');
                ++index;
            } else {
                quoted = !quoted;
            }
        } else if (ch == ',' && !quoted) {
            result.push_back(field);
            field.clear();
        } else if (ch == '#' && !quoted) {
            break;
        } else {
            field.push_back(ch);
        }
    }
    result.push_back(field);
    for (std::string& value : result) {
        const size_t begin = value.find_first_not_of(" \t");
        const size_t end = value.find_last_not_of(" \t");
        value = begin == std::string::npos
            ? std::string{}
            : value.substr(begin, end - begin + 1);
    }
    return result;
}

std::vector<StationDefinitionDraftRow> make_drafts(
    const std::vector<StationRow>& rows) {
    std::vector<StationDefinitionDraftRow> drafts;
    drafts.reserve(rows.size());
    for (const StationRow& source : rows) {
        StationDefinitionDraftRow row;
        row.target_edit_id = source.edit_id;
        row.target_source_file = source.source_file;
        row.target_expected_source_hash = source.expected_source_hash;
        row.original_values.assign(
            source.values.begin(), source.values.end());
        row.payload_edit_id = source.edit_id;
        row.payload_source_file = source.source_file;
        row.payload_raw_statement = source.raw_statement;
        row.values.assign(source.values.begin(), source.values.end());
        drafts.push_back(std::move(row));
    }
    return drafts;
}

std::vector<EditChange> typed_changes(
    const std::map<std::string, MapElementPendingChange>& pending) {
    std::vector<EditChange> result;
    result.reserve(pending.size());
    for (const auto& entry : pending) {
        const MapElementPendingChange& source = entry.second;
        EditChange change;
        change.change_id = source.change_id;
        change.edit_id = source.edit_id;
        if (source.operation == "update") change.operation = KV_EDIT_UPDATE;
        else if (source.operation == "delete") change.operation = KV_EDIT_DELETE;
        else throw std::runtime_error("unexpected Station.List pending operation");
        change.replacement_statement = source.replacement_statement;
        change.expected_source_hash = source.expected_source_hash;
        for (const auto& field : source.field_changes) {
            change.fields.push_back({field.first, field.second});
        }
        result.push_back(std::move(change));
    }
    return result;
}

std::string pending_signature(
    const std::map<std::string, MapElementPendingChange>& changes) {
    std::ostringstream out;
    for (const auto& entry : changes) {
        const MapElementPendingChange& change = entry.second;
        out << entry.first << "|" << change.operation << "|"
            << change.replacement_statement << "\n";
        for (const auto& field : change.field_changes) {
            out << field.first << "=" << field.second << "\n";
        }
    }
    return out.str();
}

std::map<std::string, MapElementPendingChange> build_pending(
    const std::vector<StationDefinitionDraftRow>& drafts) {
    std::map<std::string, MapElementPendingChange> result;
    std::string error;
    if (!build_station_definition_pending_changes(drafts, {}, result, error)) {
        throw std::runtime_error(error.empty()
            ? "shared Station.List draft builder failed" : error);
    }
    return result;
}

size_t select_consecutive_run(const std::vector<StationRow>& rows) {
    std::map<std::string, int> key_counts;
    for (const StationRow& row : rows) {
        const std::string key = lower_ascii_copy(row.values[0]);
        if (!key.empty()) ++key_counts[key];
    }
    for (size_t begin = 0; begin + 6 <= rows.size(); ++begin) {
        bool valid = !rows[begin + 2].values[1].empty();
        std::set<std::string> keys;
        for (size_t offset = 0; valid && offset < 6; ++offset) {
            const StationRow& row = rows[begin + offset];
            const std::string key = lower_ascii_copy(row.values[0]);
            valid = !key.empty() && key_counts[key] == 1 && keys.insert(key).second;
            if (offset != 0) {
                valid = valid &&
                    row.source_line == rows[begin + offset - 1].source_line + 1;
            }
        }
        if (valid) return begin;
    }
    throw std::runtime_error(
        "no six consecutive unique Station.List definitions were found");
}

const StationRow* find_edit_id(const std::vector<StationRow>& rows,
                               const std::string& edit_id) {
    const auto found = std::find_if(rows.begin(), rows.end(),
                                    [&](const StationRow& row) {
                                        return row.edit_id == edit_id;
                                    });
    return found == rows.end() ? nullptr : &*found;
}

bool station_name_key_absent(void* handle, const std::string& key) {
    const KvMapSnapshot snapshot =
        distance_batch_headless::current_map_snapshot(handle);
    for (std::uint64_t index = 0; index < snapshot.station_name_count; ++index) {
        const std::string candidate = distance_batch_headless::snapshot_text(
            snapshot, snapshot.station_names[index].key);
        if (lower_ascii_copy(candidate) == lower_ascii_copy(key)) return false;
    }
    return true;
}

bool snapshot_matches_edits(
    void* handle, const std::string& target_file,
    const std::vector<StationRow>& baseline, size_t run,
    size_t baseline_global_count, bool& empty_row_persisted,
    bool& empty_key_not_registered, bool& stable_ids) {
    const KvMapSnapshot snapshot =
        distance_batch_headless::current_map_snapshot(handle);
    std::vector<StationRow> all = collect_station_rows(handle);
    std::vector<StationRow> target;
    for (StationRow& row : all) {
        if (same_path(row.source_file, target_file)) target.push_back(std::move(row));
    }
    const StationRow* first = find_edit_id(target, baseline[run].edit_id);
    const StationRow* second = find_edit_id(target, baseline[run + 1].edit_id);
    const StationRow* cleared_normal = find_edit_id(target, baseline[run + 2].edit_id);
    const StationRow* cleared_key = find_edit_id(target, baseline[run + 3].edit_id);
    const StationRow* deleted = find_edit_id(target, baseline[run + 4].edit_id);
    const StationRow* sentinel = find_edit_id(target, baseline[run + 5].edit_id);
    stable_ids = first && second && cleared_normal && cleared_key && !deleted && sentinel;
    if (!stable_ids || snapshot.station_list_count + 1 != baseline_global_count ||
        target.size() + 1 != baseline.size()) {
        return false;
    }
    const bool first_swapped = first->values == baseline[run + 1].values;
    const bool second_swapped = second->values == baseline[run].values;
    bool normal_only = cleared_normal->values[1].empty();
    for (size_t field = 0; field < cleared_normal->values.size(); ++field) {
        if (field != 1 && cleared_normal->values[field] != baseline[run + 2].values[field]) {
            normal_only = false;
        }
    }
    bool key_only = cleared_key->values[0].empty();
    for (size_t field = 1; field < cleared_key->values.size(); ++field) {
        if (cleared_key->values[field] != baseline[run + 3].values[field]) {
            key_only = false;
        }
    }
    empty_row_persisted = key_only;
    empty_key_not_registered =
        station_name_key_absent(handle, "") &&
        station_name_key_absent(handle, baseline[run + 3].values[0]) &&
        station_name_key_absent(handle, baseline[run + 4].values[0]);
    return first_swapped && second_swapped && normal_only && key_only &&
        sentinel->values == baseline[run + 5].values;
}

bool persisted_snapshot_matches(
    void* handle, const std::string& target_file,
    const std::vector<StationRow>& baseline, size_t run,
    size_t baseline_global_count, bool& empty_row_persisted,
    bool& empty_key_not_registered) {
    const KvMapSnapshot snapshot =
        distance_batch_headless::current_map_snapshot(handle);
    std::vector<StationRow> all = collect_station_rows(handle);
    std::vector<StationRow> target;
    for (StationRow& row : all) {
        if (same_path(row.source_file, target_file)) target.push_back(std::move(row));
    }
    if (snapshot.station_list_count + 1 != baseline_global_count ||
        target.size() + 1 != baseline.size() || run + 4 >= target.size()) {
        return false;
    }
    bool normal_only = target[run + 2].values[1].empty();
    for (size_t field = 0; field < target[run + 2].values.size(); ++field) {
        if (field != 1 &&
            target[run + 2].values[field] != baseline[run + 2].values[field]) {
            normal_only = false;
        }
    }
    bool key_only = target[run + 3].values[0].empty();
    for (size_t field = 1; field < target[run + 3].values.size(); ++field) {
        if (target[run + 3].values[field] != baseline[run + 3].values[field]) {
            key_only = false;
        }
    }
    empty_row_persisted = key_only;
    empty_key_not_registered =
        station_name_key_absent(handle, "") &&
        station_name_key_absent(handle, baseline[run + 3].values[0]) &&
        station_name_key_absent(handle, baseline[run + 4].values[0]);
    return target[run].values == baseline[run + 1].values &&
        target[run + 1].values == baseline[run].values &&
        normal_only && key_only &&
        target[run + 4].values == baseline[run + 5].values;
}

bool apply_report_ok(const EditReport& report) {
    return report.ok && report.full_reparse_ok &&
        report.update_count == 4 && report.delete_count == 1 &&
        report.non_target_changed_count == 0 &&
        report.blocking_errors.empty();
}

bool commit_report_ok(const EditReport& report) {
    return report.ok && report.full_reparse_ok &&
        report.non_target_changed_count == 0 &&
        !report.committed_files.empty() && report.blocking_errors.empty();
}

void write_facts(std::ostream& out, const Facts& facts) {
    auto flag = [&](const char* name, bool value) {
        out << name << "=" << (value ? 1 : 0) << "\n";
    };
    out << "command=debug-headless-station-list-edit\n";
    out << "map_path=" << facts.map_path << "\n";
    out << "target_file=" << facts.target_file << "\n";
    out << "commit_requested=" << (facts.commit_requested ? 1 : 0) << "\n";
    out << "original_source_hash=" << facts.original_source_hash << "\n";
    out << "committed_source_hash=" << facts.committed_source_hash << "\n";
    out << "original_snapshot_fingerprint=" << facts.original_snapshot_fingerprint << "\n";
    out << "reset_snapshot_fingerprint=" << facts.reset_snapshot_fingerprint << "\n";
    out << "encoding=" << facts.encoding << "\n";
    out << "newline=" << facts.newline << "\n";
    out << "baseline_station_count=" << facts.baseline_station_count << "\n";
    out << "baseline_target_row_count=" << facts.baseline_target_row_count << "\n";
    out << "baseline_physical_line_count=" << facts.baseline_physical_line_count << "\n";
    out << "committed_physical_line_count=" << facts.committed_physical_line_count << "\n";
    out << "selected_first_line=" << facts.selected_first_line << "\n";
    out << "selected_last_line=" << facts.selected_last_line << "\n";
    for (size_t index = 0; index < facts.selected_edit_ids.size(); ++index) {
        out << "selected_edit_id_" << index << "=" << facts.selected_edit_ids[index] << "\n";
    }
    flag("first_move_up_disabled", facts.first_move_up_disabled);
    flag("last_move_down_disabled", facts.last_move_down_disabled);
    flag("opposite_moves_equivalent", facts.opposite_moves_equivalent);
    flag("full_row_templates_swapped", facts.full_row_templates_swapped);
    flag("clear_normal_cell_draft_ok", facts.clear_normal_cell_draft_ok);
    flag("clear_key_draft_ok", facts.clear_key_draft_ok);
    flag("delete_row_draft_ok", facts.delete_row_draft_ok);
    flag("dry_run_ok", facts.dry_run_ok);
    flag("dry_run_disk_unchanged", facts.dry_run_disk_unchanged);
    flag("apply_memory_ok", facts.apply_memory_ok);
    flag("apply_memory_snapshot_ok", facts.apply_memory_snapshot_ok);
    flag("apply_memory_disk_unchanged", facts.apply_memory_disk_unchanged);
    flag("empty_key_row_persisted", facts.empty_key_row_persisted);
    flag("empty_key_not_registered", facts.empty_key_not_registered);
    flag("reset_ok", facts.reset_ok);
    flag("reset_snapshot_restored", facts.reset_snapshot_restored);
    flag("reset_disk_unchanged", facts.reset_disk_unchanged);
    flag("commit_attempted", facts.commit_attempted);
    flag("commit_ok", facts.commit_ok);
    flag("committed_snapshot_ok", facts.committed_snapshot_ok);
    flag("committed_stable_edit_ids", facts.committed_stable_edit_ids);
    flag("committed_disk_changed", facts.committed_disk_changed);
    flag("committed_header_encoding_newline_preserved",
         facts.committed_header_encoding_newline_preserved);
    flag("committed_full_rows_swapped", facts.committed_full_rows_swapped);
    flag("committed_clear_cells_persisted", facts.committed_clear_cells_persisted);
    flag("committed_delete_removed_physical_line",
         facts.committed_delete_removed_physical_line);
    flag("committed_sentinel_and_suffix_preserved",
         facts.committed_sentinel_and_suffix_preserved);
    flag("committed_non_target_semantics_ok",
         facts.committed_non_target_semantics_ok);
    if (!facts.error.empty()) out << "error=" << facts.error << "\n";
    out << "result=" << (facts.passed() ? "PASS" : "FAIL") << "\n";
}

} // namespace station_list_edit_headless

int run_debug_headless_station_list_edit(
    const HeadlessStationListEditOptions& options) {
    using namespace station_list_edit_headless;
    std::ofstream output_file;
    std::ostream* out = &std::cout;
    if (!options.output_path.empty()) {
        output_file.open(std::filesystem::path(utf8_to_wide(options.output_path)),
                         std::ios::out | std::ios::trunc | std::ios::binary);
        if (!output_file) {
            std::cerr << "failed to open headless output: "
                      << options.output_path << "\n";
            return 1;
        }
        out = &output_file;
    }

    Facts facts;
    facts.map_path = options.path;
    facts.commit_requested = options.commit;
    try {
        MapHandle handle;
        handle.value = kv_load_map_ex(options.path.c_str(), options.unit_distance,
                                      KV_LOAD_EDIT_METADATA);
        if (!handle.value) {
            const char* error = kv_get_last_error();
            throw std::runtime_error(error ? error : "real map load failed");
        }
        const KvMapSnapshot baseline_snapshot =
            distance_batch_headless::current_map_snapshot(handle.value);
        facts.baseline_station_count =
            static_cast<size_t>(baseline_snapshot.station_list_count);
        facts.original_snapshot_fingerprint =
            distance_batch_headless::source_snapshot_fingerprint(handle.value);

        const std::vector<StationRow> all_rows = collect_station_rows(handle.value);
        std::map<std::string, std::vector<StationRow>> rows_by_file;
        for (const StationRow& row : all_rows) rows_by_file[row.source_file].push_back(row);
        const auto preferred = std::find_if(
            rows_by_file.begin(), rows_by_file.end(), [](const auto& entry) {
                return lower_ascii_copy(wide_to_utf8(
                    std::filesystem::path(utf8_to_wide(entry.first)).filename().wstring())) ==
                    "stations121m.txt";
            });
        const auto selected_file = preferred != rows_by_file.end()
            ? preferred
            : std::max_element(
                  rows_by_file.begin(), rows_by_file.end(),
                  [](const auto& left, const auto& right) {
                      return left.second.size() < right.second.size();
                  });
        if (selected_file == rows_by_file.end() || selected_file->second.size() < 6) {
            throw std::runtime_error(
                "no Station.List source file contains at least six definitions");
        }
        const std::vector<StationRow>& baseline = selected_file->second;
        facts.target_file = selected_file->first;
        facts.baseline_target_row_count = baseline.size();
        const size_t run = select_consecutive_run(baseline);
        facts.selected_first_line = baseline[run].source_line;
        facts.selected_last_line = baseline[run + 5].source_line;
        for (size_t index = 0; index < 6; ++index) {
            facts.selected_edit_ids[index] = baseline[run + index].edit_id;
        }

        const SourceFacts original_source = source_facts(handle.value, facts.target_file);
        facts.original_source_hash = original_source.source_hash;
        facts.encoding = original_source.encoding;
        facts.newline = original_source.newline;
        const std::filesystem::path target_path(utf8_to_wide(facts.target_file));
        const std::filesystem::path entry_path(utf8_to_wide(options.path));
        const std::string baseline_bytes =
            distance_batch_headless::read_fixture_file(target_path);
        const std::string baseline_entry_bytes =
            distance_batch_headless::read_fixture_file(entry_path);
        const std::vector<std::string> baseline_lines = physical_lines(baseline_bytes);
        facts.baseline_physical_line_count = baseline_lines.size();

        std::vector<StationDefinitionDraftRow> boundary_drafts =
            make_drafts(baseline);
        const std::vector<size_t> boundary_visible =
            station_definition_visible_row_indices(boundary_drafts);
        facts.first_move_up_disabled = !move_station_definition_draft_row(
            boundary_drafts, boundary_visible, 0, -1);
        facts.last_move_down_disabled = !move_station_definition_draft_row(
            boundary_drafts, boundary_visible,
            static_cast<int>(boundary_visible.size()) - 1, 1);

        std::vector<StationDefinitionDraftRow> down_drafts = make_drafts(baseline);
        const std::vector<size_t> down_visible =
            station_definition_visible_row_indices(down_drafts);
        if (!move_station_definition_draft_row(
                down_drafts, down_visible, static_cast<int>(run), 1)) {
            throw std::runtime_error("shared first-row move-down operation failed");
        }
        const auto down_pending = build_pending(down_drafts);

        std::vector<StationDefinitionDraftRow> up_drafts = make_drafts(baseline);
        const std::vector<size_t> up_visible =
            station_definition_visible_row_indices(up_drafts);
        if (!move_station_definition_draft_row(
                up_drafts, up_visible, static_cast<int>(run + 1), -1)) {
            throw std::runtime_error("shared second-row move-up operation failed");
        }
        const auto up_pending = build_pending(up_drafts);
        facts.opposite_moves_equivalent =
            pending_signature(down_pending) == pending_signature(up_pending);
        const auto first_change = down_pending.find(baseline[run].edit_id);
        const auto second_change = down_pending.find(baseline[run + 1].edit_id);
        facts.full_row_templates_swapped =
            first_change != down_pending.end() &&
            second_change != down_pending.end() &&
            first_change->second.field_changes.size() == 13 &&
            second_change->second.field_changes.size() == 13 &&
            first_change->second.replacement_statement ==
                baseline[run + 1].raw_statement &&
            second_change->second.replacement_statement ==
                baseline[run].raw_statement;

        std::vector<StationDefinitionDraftRow> drafts = make_drafts(baseline);
        const std::vector<size_t> visible =
            station_definition_visible_row_indices(drafts);
        if (!move_station_definition_draft_row(
                drafts, visible, static_cast<int>(run), 1) ||
            !clear_station_definition_draft_cell(
                drafts, visible, static_cast<int>(run + 2), 1) ||
            !clear_station_definition_draft_cell(
                drafts, visible, static_cast<int>(run + 3), 0) ||
            !delete_station_definition_draft_row(
                drafts, visible, static_cast<int>(run + 4))) {
            throw std::runtime_error("shared composite Station.List draft operation failed");
        }
        facts.clear_normal_cell_draft_ok =
            drafts[run + 2].values[1].empty();
        facts.clear_key_draft_ok = drafts[run + 3].values[0].empty();
        facts.delete_row_draft_ok = drafts[run + 4].deleted;

        const auto pending = build_pending(drafts);
        const std::vector<EditChange> changes = typed_changes(pending);
        const EditReport dry = typed_edit_headless::dry_run(handle.value, changes);
        facts.dry_run_ok = apply_report_ok(dry);
        facts.dry_run_disk_unchanged =
            distance_batch_headless::read_fixture_file(target_path) == baseline_bytes &&
            distance_batch_headless::read_fixture_file(entry_path) == baseline_entry_bytes;
        if (!facts.dry_run_ok) {
            throw std::runtime_error(dry.blocking_errors.empty()
                ? "Station.List dry-run assertions failed"
                : dry.blocking_errors.front());
        }

        const EditReport applied =
            typed_edit_headless::apply_to_memory(handle.value, changes);
        facts.apply_memory_ok = apply_report_ok(applied);
        bool applied_stable_ids = false;
        facts.apply_memory_snapshot_ok = snapshot_matches_edits(
            handle.value, facts.target_file, baseline, run,
            facts.baseline_station_count, facts.empty_key_row_persisted,
            facts.empty_key_not_registered, applied_stable_ids);
        facts.apply_memory_disk_unchanged =
            distance_batch_headless::read_fixture_file(target_path) == baseline_bytes &&
            distance_batch_headless::read_fixture_file(entry_path) == baseline_entry_bytes;
        if (!facts.apply_memory_ok || !facts.apply_memory_snapshot_ok ||
            !applied_stable_ids) {
            throw std::runtime_error("Station.List Apply-to-memory assertions failed");
        }

        facts.reset_ok = kv_edit_reset_memory(handle.value) != 0;
        facts.reset_snapshot_fingerprint =
            distance_batch_headless::source_snapshot_fingerprint(handle.value);
        const std::vector<StationRow> reset_all = collect_station_rows(handle.value);
        bool reset_rows_match = true;
        for (size_t index = 0; index < 6; ++index) {
            const StationRow* row =
                find_edit_id(reset_all, baseline[run + index].edit_id);
            reset_rows_match = reset_rows_match && row &&
                row->values == baseline[run + index].values;
        }
        facts.reset_snapshot_restored =
            facts.reset_ok && reset_rows_match &&
            facts.reset_snapshot_fingerprint == facts.original_snapshot_fingerprint;
        facts.reset_disk_unchanged =
            distance_batch_headless::read_fixture_file(target_path) == baseline_bytes &&
            distance_batch_headless::read_fixture_file(entry_path) == baseline_entry_bytes;
        if (!facts.reset_snapshot_restored || !facts.reset_disk_unchanged) {
            throw std::runtime_error("Station.List Reset did not restore the baseline");
        }

        if (options.commit) {
            facts.commit_attempted = true;
            const EditReport reapplied =
                typed_edit_headless::apply_to_memory(handle.value, changes);
            if (!apply_report_ok(reapplied)) {
                throw std::runtime_error("pre-commit Station.List Apply failed");
            }
            const EditReport committed = typed_edit_headless::commit(handle.value);
            facts.commit_ok = commit_report_ok(committed);
            facts.committed_non_target_semantics_ok =
                committed.non_target_changed_count == 0;
            std::set<std::string> committed_station_ids;
            for (const typed_edit_headless::CommittedRow& row :
                 committed.committed_rows) {
                if (row.row_kind == "station.list") {
                    committed_station_ids.insert(row.edit_id);
                }
            }
            facts.committed_stable_edit_ids =
                committed_station_ids.count(baseline[run].edit_id) != 0 &&
                committed_station_ids.count(baseline[run + 1].edit_id) != 0 &&
                committed_station_ids.count(baseline[run + 2].edit_id) != 0 &&
                committed_station_ids.count(baseline[run + 3].edit_id) != 0 &&
                committed_station_ids.count(baseline[run + 4].edit_id) == 0 &&
                committed_station_ids.count(baseline[run + 5].edit_id) != 0;
            if (!facts.commit_ok) {
                throw std::runtime_error(committed.blocking_errors.empty()
                    ? "Station.List commit assertions failed"
                    : committed.blocking_errors.front());
            }

            const std::string committed_bytes =
                distance_batch_headless::read_fixture_file(target_path);
            const std::string committed_entry_bytes =
                distance_batch_headless::read_fixture_file(entry_path);
            facts.committed_disk_changed =
                committed_bytes != baseline_bytes &&
                committed_entry_bytes == baseline_entry_bytes;
            const std::vector<std::string> committed_lines =
                physical_lines(committed_bytes);
            facts.committed_physical_line_count = committed_lines.size();

            MapHandle reopened;
            reopened.value = kv_load_map_ex(options.path.c_str(), options.unit_distance,
                                            KV_LOAD_EDIT_METADATA);
            if (!reopened.value) {
                const char* error = kv_get_last_error();
                throw std::runtime_error(error
                    ? error : "committed entry map reopen failed");
            }
            const SourceFacts committed_source =
                source_facts(reopened.value, facts.target_file);
            facts.committed_source_hash = committed_source.source_hash;
            bool reopened_empty_row = false;
            bool reopened_empty_lookup = false;
            facts.committed_snapshot_ok = persisted_snapshot_matches(
                reopened.value, facts.target_file, baseline, run,
                facts.baseline_station_count, reopened_empty_row,
                reopened_empty_lookup);
            facts.committed_snapshot_ok = facts.committed_snapshot_ok &&
                reopened_empty_row && reopened_empty_lookup;

            const size_t first_line_index =
                static_cast<size_t>(baseline[run].source_line - 1);
            const size_t delete_line_index =
                static_cast<size_t>(baseline[run + 4].source_line - 1);
            const bool physical_range_ok =
                first_line_index + 5 < baseline_lines.size() &&
                delete_line_index < committed_lines.size();
            if (physical_range_ok) {
                facts.committed_full_rows_swapped =
                    committed_lines[first_line_index] ==
                        baseline[run + 1].raw_statement &&
                    committed_lines[first_line_index + 1] ==
                        baseline[run].raw_statement;
                const std::vector<std::string> normal_values =
                    csv_values(committed_lines[first_line_index + 2]);
                const std::vector<std::string> key_values =
                    csv_values(committed_lines[first_line_index + 3]);
                facts.committed_clear_cells_persisted =
                    normal_values.size() >= 13 && normal_values[1].empty() &&
                    key_values.size() >= 13 && key_values[0].empty() &&
                    !committed_lines[first_line_index + 3].empty() &&
                    committed_lines[first_line_index + 3].front() == ',';
                facts.committed_delete_removed_physical_line =
                    committed_lines.size() + 1 == baseline_lines.size();
                bool prefix_ok = true;
                for (size_t index = 0; index < first_line_index; ++index) {
                    prefix_ok = prefix_ok &&
                        committed_lines[index] == baseline_lines[index];
                }
                bool suffix_ok = true;
                for (size_t index = delete_line_index;
                     index < committed_lines.size(); ++index) {
                    suffix_ok = suffix_ok && index + 1 < baseline_lines.size() &&
                        committed_lines[index] == baseline_lines[index + 1];
                }
                facts.committed_sentinel_and_suffix_preserved =
                    prefix_ok && suffix_ok &&
                    committed_lines[delete_line_index] ==
                        baseline[run + 5].raw_statement;
            }
            const auto baseline_newlines = newline_counts(baseline_bytes);
            const auto committed_newlines = newline_counts(committed_bytes);
            const bool newline_preserved =
                (baseline_newlines.second == 0 &&
                 committed_newlines.second == 0 &&
                 committed_newlines.first + 1 == baseline_newlines.first) ||
                (baseline_newlines.first == 0 &&
                 committed_newlines.first == 0 &&
                 committed_newlines.second + 1 == baseline_newlines.second);
            facts.committed_header_encoding_newline_preserved =
                committed_source.encoding == original_source.encoding &&
                committed_source.newline == original_source.newline &&
                newline_preserved;
        }
    } catch (const std::exception& e) {
        facts.error = e.what();
    }

    write_facts(*out, facts);
    out->flush();
    return facts.passed() ? 0 : 22;
}

namespace repeater_batch_headless {

using EditChange = typed_edit_headless::Change;
using EditReport = typed_edit_headless::Report;
using MapHandle = distance_batch_headless::MapHandle;

struct RepeaterSegmentEdit {
    std::string repeater_key;
    std::string begin_edit_id;
    std::string end_edit_id;
    std::string begin_expected_source_hash;
    std::string end_expected_source_hash;
    std::string begin_source_file;
    std::string end_source_file;
    int begin_line = 0;
    int end_line = 0;
    double begin_distance = 0.0;
    double end_distance = 0.0;
    double x = 0.0;
    double target_begin_distance = 0.0;
    double target_end_distance = 0.0;
    double target_x = 0.0;
};

struct BatchRunFacts {
    std::string path;
    bool commit_requested = false;
    bool dry_run_ok = false;
    bool apply_to_memory_ok = false;
    bool full_reparse_ok = false;
    bool snapshot_after_apply_ok = false;
    int metadata_after_apply_count = 0;
    int target_distance_match_count = 0;
    int non_target_changed_count = 0;
    bool reset_ok = false;
    bool commit_attempted = false;
    bool commit_ok = false;
    size_t commit_changed_file_count = 0;
    std::vector<RepeaterSegmentEdit> selected;
    std::string error;

    bool passed() const {
        return error.empty() && selected.size() == 5 && dry_run_ok &&
            apply_to_memory_ok && full_reparse_ok && snapshot_after_apply_ok &&
            metadata_after_apply_count == 10 && target_distance_match_count == 10 &&
            non_target_changed_count == 0 && reset_ok &&
            (!commit_requested || (commit_attempted && commit_ok));
    }
};

std::vector<RepeaterSegmentEdit> collect_repeater_segments(void* handle) {
    const KvMapSnapshot snapshot = distance_batch_headless::current_map_snapshot(handle);
    if (snapshot.repeater_count != 0 && !snapshot.repeaters) {
        throw std::runtime_error("typed map snapshot has null repeater array");
    }

    std::vector<repeater_linkage::Event> events;
    events.reserve(static_cast<size_t>(snapshot.repeater_count));
    for (std::uint64_t i = 0; i < snapshot.repeater_count; ++i) {
        const KvRepeaterRow& row = snapshot.repeaters[i];
        repeater_linkage::Event event;
        event.source_index = static_cast<size_t>(i);
        event.distance = row.distance;
        event.order = static_cast<double>(row.order);
        event.key = distance_batch_headless::snapshot_value_text(snapshot, row.repeater_key);
        const std::string method = distance_batch_headless::snapshot_text(snapshot, row.method);
        if (method == "Begin" || method == "Begin0") {
            event.kind = repeater_linkage::EventKind::Begin;
        } else if (method == "End") {
            event.kind = repeater_linkage::EventKind::End;
        }
        events.push_back(std::move(event));
    }

    std::vector<RepeaterSegmentEdit> result;
    for (const repeater_linkage::Segment& segment : repeater_linkage::pair_segments(std::move(events))) {
        if (segment.boundary_kind != repeater_linkage::BoundaryKind::ExplicitEnd ||
            !segment.boundary_source_index ||
            segment.begin_source_index >= snapshot.repeater_count ||
            *segment.boundary_source_index >= snapshot.repeater_count) {
            continue;
        }
        const KvRepeaterRow& begin = snapshot.repeaters[segment.begin_source_index];
        const KvRepeaterRow& end = snapshot.repeaters[*segment.boundary_source_index];
        const std::string method = distance_batch_headless::snapshot_text(snapshot, begin.method);
        if (method != "Begin" || segment.end_distance - segment.begin_distance <= 0.1) {
            continue;
        }

        const std::string begin_edit_id =
            distance_batch_headless::snapshot_text(snapshot, begin.metadata.edit_id);
        const std::string end_edit_id =
            distance_batch_headless::snapshot_text(snapshot, end.metadata.edit_id);
        if (begin_edit_id.empty() || end_edit_id.empty()) continue;

        std::string begin_error;
        std::string end_error;
        const std::optional<InspectorTargetMetadata> begin_info =
            resolve_inspector_target_metadata(handle, begin_edit_id, "repeater", &begin_error);
        const std::optional<InspectorTargetMetadata> end_info =
            resolve_inspector_target_metadata(handle, end_edit_id, "repeater", &end_error);
        if (!begin_info || !end_info || begin_info->expected_source_hash.empty() ||
            end_info->expected_source_hash.empty()) {
            continue;
        }

        RepeaterSegmentEdit edit;
        edit.repeater_key = distance_batch_headless::snapshot_value_text(
            snapshot, begin.repeater_key);
        edit.begin_edit_id = begin_edit_id;
        edit.end_edit_id = end_edit_id;
        edit.begin_expected_source_hash = begin_info->expected_source_hash;
        edit.end_expected_source_hash = end_info->expected_source_hash;
        edit.begin_source_file = begin_info->source.file_path;
        edit.end_source_file = end_info->source.file_path;
        edit.begin_line = begin_info->source.line;
        edit.end_line = end_info->source.line;
        edit.begin_distance = begin.distance;
        edit.end_distance = end.distance;
        edit.x = begin.x;
        edit.target_begin_distance = begin.distance + 0.001;
        edit.target_end_distance = end.distance + 0.002;
        edit.target_x = begin.x + 0.001;
        result.push_back(std::move(edit));
    }
    std::stable_sort(result.begin(), result.end(), [](const RepeaterSegmentEdit& left,
                                                       const RepeaterSegmentEdit& right) {
        if (left.begin_distance != right.begin_distance) {
            return left.begin_distance < right.begin_distance;
        }
        if (left.begin_source_file != right.begin_source_file) {
            return left.begin_source_file < right.begin_source_file;
        }
        return left.begin_line < right.begin_line;
    });
    return result;
}

std::vector<EditChange> build_changes(const std::vector<RepeaterSegmentEdit>& selected) {
    std::vector<EditChange> changes;
    changes.reserve(selected.size() * 2);
    for (size_t index = 0; index < selected.size(); ++index) {
        const RepeaterSegmentEdit& edit = selected[index];
        changes.push_back(typed_edit_headless::update(
            "headless-repeater-begin-" + std::to_string(index), edit.begin_edit_id,
            edit.begin_expected_source_hash,
            {{"distance", distance_batch_headless::edit_number(edit.target_begin_distance)},
             {"x", distance_batch_headless::edit_number(edit.target_x)}}));
        changes.push_back(typed_edit_headless::update(
            "headless-repeater-end-" + std::to_string(index), edit.end_edit_id,
            edit.end_expected_source_hash,
            {{"distance", distance_batch_headless::edit_number(edit.target_end_distance)}}));
    }
    return changes;
}

bool has_minimum_spacing(const std::vector<RepeaterSegmentEdit>& selected,
                         double candidate_distance, double minimum_spacing) {
    return std::all_of(selected.begin(), selected.end(), [&](const RepeaterSegmentEdit& edit) {
        return std::fabs(edit.begin_distance - candidate_distance) >= minimum_spacing;
    });
}

bool report_matches_selection(const EditReport& report, size_t selected_count) {
    return report.ok && report.full_reparse_ok && report.resolution_requests.empty() &&
        report.target_distance_match_count == static_cast<int>(selected_count * 2) &&
        report.non_target_changed_count == 0;
}

std::vector<RepeaterSegmentEdit> select_real_map_repeater_edits(
    void* handle, const std::vector<RepeaterSegmentEdit>& candidates) {
    if (candidates.empty()) {
        throw std::runtime_error("real map has no editable Repeater.Begin/End segments");
    }
    std::vector<RepeaterSegmentEdit> selected;
    std::string last_error;
    for (double minimum_spacing : {500.0, 50.0, 1.0, 0.0}) {
        for (const RepeaterSegmentEdit& candidate : candidates) {
            if (selected.size() == 5) break;
            if (!has_minimum_spacing(selected, candidate.begin_distance, minimum_spacing)) continue;
            selected.push_back(candidate);
            try {
                const EditReport report = typed_edit_headless::dry_run(
                    handle, build_changes(selected));
                if (report_matches_selection(report, selected.size())) continue;
                if (!report.blocking_errors.empty()) last_error = report.blocking_errors.front();
            } catch (const std::exception& e) {
                last_error = e.what();
            }
            selected.pop_back();
        }
        if (selected.size() == 5) break;
    }
    if (selected.size() != 5) {
        throw std::runtime_error(
            "could not select five resolvable Repeater Begin/End edit pairs" +
            (last_error.empty() ? std::string{} : ": " + last_error));
    }
    return selected;
}

bool snapshot_matches_selected(void* handle, const std::vector<RepeaterSegmentEdit>& selected) {
    const KvMapSnapshot snapshot = distance_batch_headless::current_map_snapshot(handle);
    for (const RepeaterSegmentEdit& edit : selected) {
        bool begin_found = false;
        bool end_found = false;
        for (std::uint64_t index = 0; index < snapshot.repeater_count; ++index) {
            const KvRepeaterRow& row = snapshot.repeaters[index];
            const std::string edit_id =
                distance_batch_headless::snapshot_text(snapshot, row.metadata.edit_id);
            if (edit_id == edit.begin_edit_id) {
                begin_found = std::fabs(row.distance - edit.target_begin_distance) <= 1e-8 &&
                    std::fabs(row.x - edit.target_x) <= 1e-8;
            } else if (edit_id == edit.end_edit_id) {
                end_found = std::fabs(row.distance - edit.target_end_distance) <= 1e-8;
            }
        }
        if (!begin_found || !end_found) return false;
    }
    return true;
}

int metadata_matches_selected(void* handle, const std::vector<RepeaterSegmentEdit>& selected) {
    int matched = 0;
    auto count_match = [&](const std::string& edit_id, double target_distance) {
        std::string error;
        const std::optional<InspectorTargetMetadata> info =
            resolve_inspector_target_metadata(handle, edit_id, "repeater", &error);
        if (info && !info->source_distance_string.empty() &&
            std::isfinite(info->distance_value) &&
            std::fabs(info->distance_value - target_distance) <= 1e-8) {
            ++matched;
        }
    };
    for (const RepeaterSegmentEdit& edit : selected) {
        count_match(edit.begin_edit_id, edit.target_begin_distance);
        count_match(edit.end_edit_id, edit.target_end_distance);
    }
    return matched;
}

void write_batch_result(std::ostream& out, const BatchRunFacts& facts) {
    auto boolean = [](bool value) { return value ? 1 : 0; };
    out << "command=debug-headless-repeater-edit-batch\n"
        << "path=" << facts.path << "\n"
        << "commit_requested=" << boolean(facts.commit_requested) << "\n"
        << "selected_count=" << facts.selected.size() << "\n"
        << "dry_run_ok=" << boolean(facts.dry_run_ok) << "\n"
        << "apply_to_memory_ok=" << boolean(facts.apply_to_memory_ok) << "\n"
        << "full_reparse_ok=" << boolean(facts.full_reparse_ok) << "\n"
        << "snapshot_after_apply_ok=" << boolean(facts.snapshot_after_apply_ok) << "\n"
        << "metadata_after_apply_count=" << facts.metadata_after_apply_count << "\n"
        << "target_distance_match_count=" << facts.target_distance_match_count << "\n"
        << "non_target_changed_count=" << facts.non_target_changed_count << "\n"
        << "reset_ok=" << boolean(facts.reset_ok) << "\n"
        << "commit_attempted=" << boolean(facts.commit_attempted) << "\n"
        << "commit_ok=" << boolean(facts.commit_ok) << "\n"
        << "commit_changed_file_count=" << facts.commit_changed_file_count << "\n";
    for (size_t index = 0; index < facts.selected.size(); ++index) {
        const RepeaterSegmentEdit& edit = facts.selected[index];
        out << "selected." << index << ".repeater_key=" << edit.repeater_key << "\n"
            << "selected." << index << ".begin_edit_id=" << edit.begin_edit_id << "\n"
            << "selected." << index << ".end_edit_id=" << edit.end_edit_id << "\n"
            << "selected." << index << ".begin_source_file=" << edit.begin_source_file << "\n"
            << "selected." << index << ".begin_line=" << edit.begin_line << "\n"
            << "selected." << index << ".end_source_file=" << edit.end_source_file << "\n"
            << "selected." << index << ".end_line=" << edit.end_line << "\n"
            << "selected." << index << ".begin_distance="
            << distance_batch_headless::edit_number(edit.begin_distance) << "\n"
            << "selected." << index << ".target_begin_distance="
            << distance_batch_headless::edit_number(edit.target_begin_distance) << "\n"
            << "selected." << index << ".end_distance="
            << distance_batch_headless::edit_number(edit.end_distance) << "\n"
            << "selected." << index << ".target_end_distance="
            << distance_batch_headless::edit_number(edit.target_end_distance) << "\n"
            << "selected." << index << ".x="
            << distance_batch_headless::edit_number(edit.x) << "\n"
            << "selected." << index << ".target_x="
            << distance_batch_headless::edit_number(edit.target_x) << "\n";
    }
    out << "error=" << facts.error << "\n"
        << "result=" << (facts.passed() ? "PASS" : "FAIL") << "\n";
}

} // namespace repeater_batch_headless

int run_debug_headless_repeater_edit_batch(const HeadlessRepeaterEditBatchOptions& options) {
    using namespace repeater_batch_headless;
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

        facts.selected = select_real_map_repeater_edits(
            handle.value, collect_repeater_segments(handle.value));
        const std::vector<EditChange> changes = build_changes(facts.selected);
        const EditReport dry_report = typed_edit_headless::dry_run(handle.value, changes);
        facts.dry_run_ok = report_matches_selection(dry_report, facts.selected.size());
        if (!facts.dry_run_ok) {
            throw std::runtime_error(dry_report.blocking_errors.empty()
                ? "Repeater dry-run assertions did not match"
                : dry_report.blocking_errors.front());
        }

        const EditReport apply_report = typed_edit_headless::apply_to_memory(handle.value, changes);
        facts.apply_to_memory_ok = apply_report.ok;
        facts.full_reparse_ok = apply_report.full_reparse_ok;
        facts.target_distance_match_count = apply_report.target_distance_match_count;
        facts.non_target_changed_count = apply_report.non_target_changed_count;
        facts.snapshot_after_apply_ok = snapshot_matches_selected(handle.value, facts.selected);
        facts.metadata_after_apply_count = metadata_matches_selected(handle.value, facts.selected);
        if (!facts.apply_to_memory_ok || !facts.full_reparse_ok ||
            facts.target_distance_match_count != 10 || facts.non_target_changed_count != 0 ||
            !facts.snapshot_after_apply_ok || facts.metadata_after_apply_count != 10) {
            throw std::runtime_error(apply_report.blocking_errors.empty()
                ? "Repeater apply-to-memory assertions did not match"
                : apply_report.blocking_errors.front());
        }

        facts.reset_ok = kv_edit_reset_memory(handle.value) != 0;
        if (!facts.reset_ok) {
            const char* error = kv_get_last_error();
            throw std::runtime_error(error ? error : "kv_edit_reset_memory failed");
        }

        if (options.commit) {
            facts.commit_attempted = true;
            const EditReport commit_apply = typed_edit_headless::apply_to_memory(
                handle.value, changes);
            if (!report_matches_selection(commit_apply, facts.selected.size())) {
                throw std::runtime_error(commit_apply.blocking_errors.empty()
                    ? "Repeater pre-commit apply-to-memory assertions did not match"
                    : commit_apply.blocking_errors.front());
            }
            const EditReport commit_report = typed_edit_headless::commit(handle.value);
            facts.commit_ok = commit_report.ok && commit_report.full_reparse_ok;
            facts.commit_changed_file_count = commit_report.changed_files.size();
            if (!facts.commit_ok) {
                throw std::runtime_error(commit_report.blocking_errors.empty()
                    ? "Repeater commit validation failed"
                    : commit_report.blocking_errors.front());
            }
        }
    } catch (const std::exception& e) {
        facts.error = e.what();
    }

    write_batch_result(*out, facts);
    out->flush();
    return facts.passed() ? 0 : 20;
}

namespace {

struct OpenBenchSample {
    double ready_seconds = 0.0;
    double load_worker_seconds = 0.0;
    double maploader_seconds = 0.0;
    double model_seconds = 0.0;
    double snapshot_build_seconds = 0.0;
    double snapshot_hydrate_seconds = 0.0;
    double hydrate_seconds = 0.0;
    double buffer_copy_seconds = 0.0;
    double overlay_seconds = 0.0;
    double plan_data_seconds = 0.0;
    double plan_draw_seconds = 0.0;
    double profile_data_seconds = 0.0;
    double profile_draw_seconds = 0.0;
    double radius_draw_seconds = 0.0;
    double first_2d_frame_seconds = 0.0;
    std::uint64_t geometry_hash = 0;
    std::uint64_t overlay_hash = 0;
};
struct DebugHash64 {
    std::uint64_t value = 1469598103934665603ULL;

    void byte(unsigned char input) {
        value ^= input;
        value *= 1099511628211ULL;
    }

    void integer(std::uint64_t input) {
        for (unsigned shift = 0; shift < 64; shift += 8) {
            byte(static_cast<unsigned char>((input >> shift) & 0xffu));
        }
    }

    void number(double input) {
        integer(hash_double_bits(input));
    }

    void text(std::string_view input) {
        integer(static_cast<std::uint64_t>(input.size()));
        for (unsigned char ch : input) byte(ch);
    }
};

void hash_matrix(DebugHash64& hash, const Matrix& matrix) {
    hash.integer(static_cast<std::uint64_t>(matrix.rows));
    hash.integer(static_cast<std::uint64_t>(matrix.cols));
    for (double value : matrix.data) hash.number(value);
}

std::uint64_t model_geometry_hash(const MapModel& model) {
    DebugHash64 hash;
    hash_matrix(hash, model.own);
    hash_matrix(hash, model.curve);
    hash.integer(static_cast<std::uint64_t>(model.other_tracks.size()));
    for (const OtherTrack& track : model.other_tracks) {
        hash.text(track.key);
        hash.number(track.range_min);
        hash.number(track.range_max);
        hash_matrix(hash, track.points);
    }
    return hash.value;
}

struct MetricSummary {
    double median = 0.0;
    double p95 = 0.0;
};

MetricSummary summarize_metric(std::vector<double> values) {
    MetricSummary summary;
    if (values.empty()) return summary;
    std::sort(values.begin(), values.end());
    const size_t count = values.size();
    summary.median = count % 2 == 0
        ? (values[count / 2 - 1] + values[count / 2]) * 0.5
        : values[count / 2];
    const size_t p95_index = std::min(
        static_cast<size_t>(std::ceil(0.95 * static_cast<double>(count))) - 1,
        count - 1);
    summary.p95 = values[p95_index];
    return summary;
}

template <typename Projection>
MetricSummary summarize_samples(const std::vector<OpenBenchSample>& samples,
                                size_t first_index, Projection projection) {
    std::vector<double> values;
    if (first_index < samples.size()) values.reserve(samples.size() - first_index);
    for (size_t i = first_index; i < samples.size(); ++i) values.push_back(projection(samples[i]));
    return summarize_metric(std::move(values));
}

} // namespace

int App::run_debug_headless_open_benchmark(const HeadlessOpenBenchmarkOptions& options) {
    std::ofstream output_file;
    std::ostream* out = &std::cout;
    if (!options.output_path.empty()) {
        output_file.open(std::filesystem::path(utf8_to_wide(options.output_path)),
                         std::ios::out | std::ios::trunc);
        if (!output_file) {
            std::cerr << "failed to open headless output: " << options.output_path << "\n";
            return 1;
        }
        output_file.write("\xEF\xBB\xBF", 3);
        out = &output_file;
    }

    *out << "komapedit debug-headless-open-bench path=\"" << options.path
         << "\" repeat=" << options.repeat
         << " unit_distance=" << format_double(options.unit_distance, 3) << "\n";

    ImGui::CreateContext();
    ImPlot::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(1280.0f, 720.0f);
    io.DeltaTime = 1.0f / 60.0f;
    io.IniFilename = nullptr;
    io.Fonts->AddFontDefault();
    io.Fonts->Build();

    UserSettings settings;
    App app(nullptr, settings, 1.0f, false, false);
    app.show_profile_graph_ = true;
    app.show_radius_graph_ = true;

    auto overlay_hash = [](const App& source) {
        DebugHash64 hash;
        auto hash_track_point = [&](const TrackPoint& point) {
            hash.number(point.d); hash.number(point.x); hash.number(point.y);
            hash.number(point.z); hash.number(point.theta); hash.number(point.radius);
            hash.number(point.gradient);
        };
        auto hash_marker = [&](const auto& marker) {
            hash.number(marker.d); hash.number(marker.x); hash.number(marker.y);
            hash.text(marker.label); hash.text(marker.edit_id);
            hash.integer(static_cast<std::uint64_t>(marker.row_index));
        };
        auto hash_marker_cache = [&](const auto& cache) {
            hash.integer(static_cast<std::uint64_t>(cache.size()));
            for (const auto& marker : cache) {
                hash.integer(marker ? 1u : 0u);
                if (marker) hash_marker(*marker);
            }
        };
        hash_marker_cache(source.structure_marker_cache_);
        hash_marker_cache(source.signal_marker_cache_);
        hash_marker_cache(source.beacon_marker_cache_);
        hash_marker_cache(source.pretrain_marker_cache_);
        hash_marker_cache(source.irregularity_marker_cache_);
        hash_marker_cache(source.map_sound_marker_cache_);
        hash_marker_cache(source.map_sound_3d_marker_cache_);
        hash_marker_cache(source.rolling_noise_marker_cache_);
        hash_marker_cache(source.flange_noise_marker_cache_);
        hash_marker_cache(source.joint_noise_marker_cache_);
        hash_marker_cache(source.background_marker_cache_);
        hash_marker_cache(source.adhesion_marker_cache_);
        hash_marker_cache(source.cab_illuminance_marker_cache_);
        hash_marker_cache(source.fog_marker_cache_);

        hash.integer(static_cast<std::uint64_t>(source.other_train_stop_marker_cache_.size()));
        for (const auto& marker : source.other_train_stop_marker_cache_) {
            hash.integer(marker ? 1u : 0u);
            if (!marker) continue;
            hash_marker(*marker);
            hash.number(marker->theta);
            hash.integer(static_cast<std::uint64_t>(marker->definition_row_index));
            hash.integer(marker->reverse_direction ? 1u : 0u);
        }
        hash.integer(static_cast<std::uint64_t>(source.other_train_path_cache_.size()));
        for (const OtherTrainPathOverlay& path : source.other_train_path_cache_) {
            hash.text(path.label);
            hash.integer(static_cast<std::uint64_t>(path.definition_row_index));
            hash.number(path.d_min); hash.number(path.d_max);
            hash.integer(path.reverse_direction ? 1u : 0u);
            hash.integer(static_cast<std::uint64_t>(path.points.size()));
            for (const TrackPoint& point : path.points) hash_track_point(point);
        }

        hash.integer(static_cast<std::uint64_t>(source.repeater_marker_cache_.size()));
        for (const RepeaterOverlayRow& row : source.repeater_marker_cache_) {
            hash.integer(row.begin_marker ? 1u : 0u);
            if (row.begin_marker) hash_marker(*row.begin_marker);
            hash.integer(row.end_marker ? 1u : 0u);
            if (row.end_marker) hash_marker(*row.end_marker);
            const PlanRepeaterSegment& segment = row.segment;
            hash.integer(segment.endpoints_valid ? 1u : 0u);
            hash.integer(segment.bounds_valid ? 1u : 0u);
            hash_track_point(segment.first_point);
            hash_track_point(segment.last_point);
            hash.number(segment.d_min); hash.number(segment.d_max);
            hash.number(segment.x_min); hash.number(segment.y_min);
            hash.number(segment.x_max); hash.number(segment.y_max);
            hash.integer(static_cast<std::uint64_t>(segment.chunks.size()));
            for (const PlanRepeaterSegment::Chunk& chunk : segment.chunks) {
                hash.integer(chunk.bounds_valid ? 1u : 0u);
                hash.number(chunk.d_min); hash.number(chunk.d_max);
                hash.number(chunk.x_min); hash.number(chunk.y_min);
                hash.number(chunk.x_max); hash.number(chunk.y_max);
                hash.integer(static_cast<std::uint64_t>(chunk.points.size()));
                for (const TrackPoint& point : chunk.points) hash_track_point(point);
            }
        }
        return hash.value;
    };

    std::vector<OpenBenchSample> samples;
    bool benchmark_ok = true;
    bool workflow_ok = true;

    auto run_one = [&](int run) -> std::optional<OpenBenchSample> {
        const auto ready_started_at = std::chrono::steady_clock::now();
        LoadModelOptions load_options;
        load_options.full_edit_registry = false;
        load_options.load_profile = "preview";
        LoadResult result = load_map_worker(options.path, options.unit_distance,
                                            false, 0.0, 0.0, 25.0, load_options);
        if (!result.ok) {
            *out << "open_bench_error run=" << run
                 << " message=\"" << result.error << "\"\n";
            benchmark_ok = false;
            return std::nullopt;
        }

        OpenBenchSample sample;
        sample.load_worker_seconds = result.elapsed_seconds;
        sample.maploader_seconds = result.maploader_seconds;
        sample.model_seconds = result.model_build_seconds;
        sample.snapshot_build_seconds = result.model.snapshot_build_seconds;
        sample.snapshot_hydrate_seconds = result.model.snapshot_hydrate_seconds;
        sample.hydrate_seconds = result.model.snapshot_hydrate_seconds;
        sample.buffer_copy_seconds = result.model.buffer_copy_seconds;

        app.model_ = std::move(result.model);
        app.file_path_ = options.path;
        app.has_model_ = true;
        app.dmin_ = app.model_.default_min;
        app.dmax_ = app.model_.default_max;
        app.plot_min_ = app.dmin_;
        app.plot_max_ = app.dmax_;
        app.plan_view_ = View2D{};
        app.plan_data_cache_.valid = false;
        const auto overlay_started_at = std::chrono::steady_clock::now();
        app.rebuild_marker_overlay_cache();
        app.reset_marker_visibility();
        sample.overlay_seconds = std::chrono::duration<double>(
            std::chrono::steady_clock::now() - overlay_started_at).count();

        const auto first_frame_started_at = std::chrono::steady_clock::now();
        const auto plan_data_started_at = std::chrono::steady_clock::now();
        (void)app.current_plan_data();
        sample.plan_data_seconds = std::chrono::duration<double>(
            std::chrono::steady_clock::now() - plan_data_started_at).count();

        io.DisplaySize = ImVec2(1280.0f, 720.0f);
        io.DeltaTime = 1.0f / 60.0f;
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f), ImGuiCond_Always);
        ImGui::SetNextWindowSize(io.DisplaySize, ImGuiCond_Always);
        const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration |
            ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoBringToFrontOnFocus;
        ImGui::Begin("DebugHeadlessOpenBenchmark", nullptr, flags);
        const auto plan_draw_started_at = std::chrono::steady_clock::now();
        app.render_plan_canvas(ImVec2(1260.0f, 400.0f));
        sample.plan_draw_seconds = std::chrono::duration<double>(
            std::chrono::steady_clock::now() - plan_draw_started_at).count();

        const auto profile_data_started_at = std::chrono::steady_clock::now();
        ProfileData profile = app.build_profile_data();
        sample.profile_data_seconds = std::chrono::duration<double>(
            std::chrono::steady_clock::now() - profile_data_started_at).count();
        const auto profile_draw_started_at = std::chrono::steady_clock::now();
        app.render_profile_plot(profile, ImVec2(625.0f, 250.0f));
        sample.profile_draw_seconds = std::chrono::duration<double>(
            std::chrono::steady_clock::now() - profile_draw_started_at).count();
        ImGui::SameLine();
        const auto radius_draw_started_at = std::chrono::steady_clock::now();
        app.render_radius_plot(profile, ImVec2(625.0f, 250.0f));
        sample.radius_draw_seconds = std::chrono::duration<double>(
            std::chrono::steady_clock::now() - radius_draw_started_at).count();
        ImGui::End();
        ImGui::Render();
        sample.first_2d_frame_seconds = std::chrono::duration<double>(
            std::chrono::steady_clock::now() - first_frame_started_at).count();
        sample.ready_seconds = std::chrono::duration<double>(
            std::chrono::steady_clock::now() - ready_started_at).count();
        sample.geometry_hash = model_geometry_hash(app.model_);
        sample.overlay_hash = overlay_hash(app);

        if (result.handle) kv_free(result.handle);

        *out << std::fixed << std::setprecision(6)
             << "open_bench_run run=" << run
             << " transport=typed_snapshot"
             << " maploader=" << sample.maploader_seconds << "s"
             << " model=" << sample.model_seconds << "s"
             << " snapshot_build=" << sample.snapshot_build_seconds << "s"
             << " snapshot_hydrate=" << sample.snapshot_hydrate_seconds << "s"
             << " hydrate=" << sample.hydrate_seconds << "s"
             << " buffer_copy=" << sample.buffer_copy_seconds << "s"
             << " overlay=" << sample.overlay_seconds << "s"
             << " plan_data=" << sample.plan_data_seconds << "s"
             << " plan_draw=" << sample.plan_draw_seconds << "s"
             << " profile_data=" << sample.profile_data_seconds << "s"
             << " profile_draw=" << sample.profile_draw_seconds << "s"
             << " radius_draw=" << sample.radius_draw_seconds << "s"
             << " first_2d_frame=" << sample.first_2d_frame_seconds << "s"
             << " ready_total=" << sample.ready_seconds << "s"
             << " geometry_hash=" << hex_u64(sample.geometry_hash)
             << " overlay_hash=" << hex_u64(sample.overlay_hash) << "\n";
        out->flush();
        return sample;
    };

    for (int run = 1; run <= options.repeat && benchmark_ok; ++run) {
        if (auto sample = run_one(run)) samples.push_back(*sample);
    }

    auto ref_valid = [](const char* data, std::uint64_t size, KvStringRef ref) {
        return ref.offset <= size && ref.length <= size - ref.offset &&
            (ref.length == 0 || data != nullptr);
    };
    auto span_valid = [](KvSpan span, std::uint64_t size) {
        return span.offset <= size && span.count <= size - span.offset;
    };
    auto pointer_valid = [](const void* data, std::uint64_t count) {
        return count == 0 || data != nullptr;
    };
    auto string_view = [&](const char* data, std::uint64_t size, KvStringRef ref) {
        if (!ref_valid(data, size, ref)) return std::string_view{};
        return std::string_view(data ? data + ref.offset : "", static_cast<size_t>(ref.length));
    };

    bool metadata_ok = false;
    void* metadata_handle = kv_load_map_ex(
        options.path.c_str(), options.unit_distance,
        KV_LOAD_PREVIEW | KV_LOAD_EDIT_METADATA);
    if (metadata_handle) {
        KvMapSnapshot metadata{};
        metadata_ok = kv_get_map_snapshot(metadata_handle, KV_MAP_SNAPSHOT_VERSION,
                                           &metadata, sizeof(metadata)) != 0 &&
            metadata.version == KV_MAP_SNAPSHOT_VERSION &&
            metadata.structure_size == sizeof(KvMapSnapshot) &&
            (metadata.capabilities & KV_MAP_CAP_EDIT_METADATA) != 0 &&
            (metadata.capabilities & KV_MAP_CAP_FULL_STATEMENT_SOURCE) != 0 &&
            metadata.source_file_count != 0 && metadata.statement_count != 0 &&
            metadata.element_count != 0 &&
            pointer_valid(metadata.source_files, metadata.source_file_count) &&
            pointer_valid(metadata.statements, metadata.statement_count) &&
            pointer_valid(metadata.elements, metadata.element_count);
        for (std::uint64_t i = 0; metadata_ok && i < metadata.statement_count; ++i) {
            const KvStatementRow& row = metadata.statements[i];
            metadata_ok = row.source.source_file_index < metadata.source_file_count &&
                ref_valid(metadata.string_data, metadata.string_size, row.edit_id) &&
                ref_valid(metadata.string_data, metadata.string_size, row.statement_kind) &&
                ref_valid(metadata.string_data, metadata.string_size, row.raw_text) &&
                ref_valid(metadata.string_data, metadata.string_size, row.raw_text_preview) &&
                ref_valid(metadata.string_data, metadata.string_size, row.raw_arguments) &&
                ref_valid(metadata.string_data, metadata.string_size, row.distance_expression) &&
                span_valid(row.evaluated_values, metadata.value_count) &&
                span_valid(row.source.include_stack, metadata.string_ref_count);
        }
        for (std::uint64_t i = 0; metadata_ok && i < metadata.element_count; ++i) {
            const KvElementRow& row = metadata.elements[i];
            metadata_ok = row.source_file_index < metadata.source_file_count &&
                ref_valid(metadata.string_data, metadata.string_size, row.edit_id) &&
                ref_valid(metadata.string_data, metadata.string_size, row.row_kind);
        }
    }
    *out << "snapshot_metadata_contract result=" << (metadata_ok ? "PASS" : "FAIL") << "\n";
    if (metadata_handle) kv_free(metadata_handle);

    void* contract_handle = kv_load_map_ex(
        options.path.c_str(), options.unit_distance, KV_LOAD_PREVIEW);
    KvMapSnapshot first_map{};
    KvMapSnapshot cached_map{};
    KvMapSnapshot map_after_scene{};
    KvMapSnapshot map_after_scene_rebuild{};
    KvMapSnapshot regenerated_map{};
    KvSceneGeometrySnapshot scene_before_generation{};
    KvSceneGeometrySnapshot first_scene{};
    KvSceneGeometrySnapshot cached_scene{};
    KvSceneGeometrySnapshot rebuilt_scene{};

    const bool null_handle_rejected = !kv_get_map_snapshot(
        nullptr, KV_MAP_SNAPSHOT_VERSION, &first_map, sizeof(first_map));
    const bool null_output_rejected = contract_handle && !kv_get_map_snapshot(
        contract_handle, KV_MAP_SNAPSHOT_VERSION, nullptr, sizeof(first_map));
    const bool wrong_version_rejected = contract_handle && !kv_get_map_snapshot(
        contract_handle, KV_MAP_SNAPSHOT_VERSION + 1u, &first_map, sizeof(first_map));
    const bool short_output_rejected = contract_handle && !kv_get_map_snapshot(
        contract_handle, KV_MAP_SNAPSHOT_VERSION, &first_map, sizeof(first_map) - 1u);
    const bool first_map_ok = contract_handle && kv_get_map_snapshot(
        contract_handle, KV_MAP_SNAPSHOT_VERSION, &first_map, sizeof(first_map)) &&
        first_map.version == KV_MAP_SNAPSHOT_VERSION &&
        first_map.structure_size == sizeof(KvMapSnapshot) &&
        (first_map.capabilities & KV_MAP_CAP_PREVIEW_DATA) != 0 &&
        (first_map.capabilities & KV_MAP_CAP_REGULAR_GEOMETRY) != 0 &&
        (first_map.capabilities & KV_MAP_CAP_EDIT_METADATA) == 0 &&
        ref_valid(first_map.string_data, first_map.string_size, first_map.root_path) &&
        pointer_valid(first_map.other_tracks, first_map.other_track_count) &&
        pointer_valid(first_map.own_track_events, first_map.own_track_event_count) &&
        pointer_valid(first_map.speed_limits, first_map.speed_limit_count) &&
        pointer_valid(first_map.own_track_geometry.data,
                      first_map.own_track_geometry.rows * first_map.own_track_geometry.cols);
    const bool cached_map_ok = first_map_ok && kv_get_map_snapshot(
        contract_handle, KV_MAP_SNAPSHOT_VERSION, &cached_map, sizeof(cached_map));
    const bool map_reused = cached_map_ok &&
        cached_map.content_revision == first_map.content_revision &&
        cached_map.geometry_revision == first_map.geometry_revision &&
        cached_map.string_data == first_map.string_data &&
        cached_map.values == first_map.values &&
        cached_map.string_refs == first_map.string_refs &&
        cached_map.other_tracks == first_map.other_tracks &&
        cached_map.own_track_geometry.data == first_map.own_track_geometry.data;

    const bool scene_requires_generation = contract_handle &&
        !kv_get_scene_geometry_snapshot(
            contract_handle, KV_SCENE_GEOMETRY_SNAPSHOT_VERSION,
            &scene_before_generation, sizeof(scene_before_generation));
    const bool scene_generate_ok = contract_handle && kv_generate_scene_geometry(
        contract_handle, options.unit_distance, 1.0, 25.0, 1.0, 0.01);
    const bool first_scene_ok = scene_generate_ok && kv_get_scene_geometry_snapshot(
        contract_handle, KV_SCENE_GEOMETRY_SNAPSHOT_VERSION,
        &first_scene, sizeof(first_scene)) &&
        first_scene.version == KV_SCENE_GEOMETRY_SNAPSHOT_VERSION &&
        first_scene.structure_size == sizeof(KvSceneGeometrySnapshot) &&
        first_scene.content_revision == first_map.content_revision &&
        first_scene.other_track_count == first_map.other_track_count &&
        pointer_valid(first_scene.other_tracks, first_scene.other_track_count) &&
        pointer_valid(first_scene.own_track.data,
                      first_scene.own_track.rows * first_scene.own_track.cols);
    bool scene_order_ok = first_scene_ok;
    for (std::uint64_t i = 0; scene_order_ok && i < first_scene.other_track_count; ++i) {
        scene_order_ok = string_view(first_scene.string_data, first_scene.string_size,
                                     first_scene.other_tracks[i].key) ==
            string_view(first_map.string_data, first_map.string_size,
                        first_map.other_tracks[i].key);
    }
    const bool cached_scene_ok = first_scene_ok && kv_get_scene_geometry_snapshot(
        contract_handle, KV_SCENE_GEOMETRY_SNAPSHOT_VERSION,
        &cached_scene, sizeof(cached_scene));
    const bool scene_reused = cached_scene_ok &&
        cached_scene.content_revision == first_scene.content_revision &&
        cached_scene.scene_revision == first_scene.scene_revision &&
        cached_scene.string_data == first_scene.string_data &&
        cached_scene.other_tracks == first_scene.other_tracks &&
        cached_scene.own_track.data == first_scene.own_track.data;
    const bool map_unchanged_by_scene = first_scene_ok && kv_get_map_snapshot(
        contract_handle, KV_MAP_SNAPSHOT_VERSION,
        &map_after_scene, sizeof(map_after_scene)) &&
        map_after_scene.content_revision == first_map.content_revision &&
        map_after_scene.geometry_revision == first_map.geometry_revision &&
        map_after_scene.string_data == first_map.string_data;

    const bool scene_rebuild_ok = first_scene_ok && kv_generate_scene_geometry(
        contract_handle, options.unit_distance, 1.0, 20.0, 1.0, 0.005) &&
        kv_get_scene_geometry_snapshot(
            contract_handle, KV_SCENE_GEOMETRY_SNAPSHOT_VERSION,
            &rebuilt_scene, sizeof(rebuilt_scene));
    const bool scene_revision_changed = scene_rebuild_ok &&
        rebuilt_scene.content_revision == first_scene.content_revision &&
        rebuilt_scene.scene_revision > first_scene.scene_revision;
    const bool map_unchanged_by_scene_rebuild = scene_rebuild_ok && kv_get_map_snapshot(
        contract_handle, KV_MAP_SNAPSHOT_VERSION,
        &map_after_scene_rebuild, sizeof(map_after_scene_rebuild)) &&
        map_after_scene_rebuild.content_revision == first_map.content_revision &&
        map_after_scene_rebuild.geometry_revision == first_map.geometry_revision;

    const bool regular_generate_ok = contract_handle && kv_generate_geometry(
        contract_handle, options.unit_distance, 0, 0.0, 0.0, 0.0) &&
        kv_get_map_snapshot(contract_handle, KV_MAP_SNAPSHOT_VERSION,
                            &regenerated_map, sizeof(regenerated_map));
    const bool regular_revision_changed = regular_generate_ok &&
        regenerated_map.content_revision == first_map.content_revision &&
        regenerated_map.geometry_revision > first_map.geometry_revision;
    const bool regular_invalidates_scene = regular_generate_ok &&
        !kv_get_scene_geometry_snapshot(
            contract_handle, KV_SCENE_GEOMETRY_SNAPSHOT_VERSION,
            &rebuilt_scene, sizeof(rebuilt_scene));

    const bool snapshot_contract_ok = null_handle_rejected && null_output_rejected &&
        wrong_version_rejected && short_output_rejected && first_map_ok && map_reused &&
        scene_requires_generation && first_scene_ok && scene_order_ok && scene_reused &&
        map_unchanged_by_scene && scene_revision_changed &&
        map_unchanged_by_scene_rebuild && regular_revision_changed &&
        regular_invalidates_scene;
    *out << "snapshot_contract"
         << " null_handle=" << (null_handle_rejected ? "PASS" : "FAIL")
         << " null_output=" << (null_output_rejected ? "PASS" : "FAIL")
         << " wrong_version=" << (wrong_version_rejected ? "PASS" : "FAIL")
         << " short_output=" << (short_output_rejected ? "PASS" : "FAIL")
         << " map_v2=" << (first_map_ok ? "PASS" : "FAIL")
         << " map_reuse=" << (map_reused ? "PASS" : "FAIL")
         << " scene_requires_generate=" << (scene_requires_generation ? "PASS" : "FAIL")
         << " scene_v1=" << (first_scene_ok ? "PASS" : "FAIL")
         << " scene_order=" << (scene_order_ok ? "PASS" : "FAIL")
         << " scene_reuse=" << (scene_reused ? "PASS" : "FAIL")
         << " scene_independent=" << (map_unchanged_by_scene ? "PASS" : "FAIL")
         << " scene_rebuild=" << (scene_revision_changed ? "PASS" : "FAIL")
         << " scene_rebuild_independent="
         << (map_unchanged_by_scene_rebuild ? "PASS" : "FAIL")
         << " regular_invalidation=" << (regular_revision_changed ? "PASS" : "FAIL")
         << " regular_invalidates_scene=" << (regular_invalidates_scene ? "PASS" : "FAIL")
         << " result=" << (snapshot_contract_ok ? "PASS" : "FAIL") << "\n";
    if (contract_handle) kv_free(contract_handle);

    const auto hashes_consistent = [](const std::vector<OpenBenchSample>& values) {
        if (values.empty()) return false;
        return std::all_of(values.begin() + 1, values.end(), [&](const OpenBenchSample& sample) {
            return sample.geometry_hash == values.front().geometry_hash &&
                sample.overlay_hash == values.front().overlay_hash;
        });
    };
    const bool repeated_load_hash_ok = hashes_consistent(samples);
        bool station_jump_ok = false;
        bool measure_ok = false;
        if (!app.model_.stations.empty()) {
            const double station_distance = app.model_.stations.front().distance;
            app.plan_view_ = View2D{};
            app.jump_to_distance(station_distance);
            station_jump_ok = app.plan_view_.fitted && std::isfinite(app.plan_view_.cx) &&
                std::isfinite(app.plan_view_.cy) && app.focus_profile_next_ && app.focus_radius_next_;
            app.update_measure(station_distance);
            measure_ok = app.measure_distance_ &&
                *app.measure_distance_ == station_distance && !app.measure_text_.empty();
        }

        bool csv_export_ok = false;
        bool csv_cleanup_ok = false;
        bool temp_created = false;
        std::error_code temp_error;
        const std::string temp_name = "komapedit_csv_smoke_" +
            std::to_string(static_cast<unsigned long>(GetCurrentProcessId())) + "_" +
            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
        const std::filesystem::path temp_root = std::filesystem::temp_directory_path(temp_error);
        const std::filesystem::path temp_dir = temp_root / temp_name;
        if (!temp_error && std::filesystem::create_directories(temp_dir, temp_error) && !temp_error) {
            temp_created = true;
            app.export_csv_to_directory(temp_dir);
            const std::filesystem::path own_csv = temp_dir / (temp_name + "_owntrack.csv");
            std::ifstream input(own_csv, std::ios::binary);
            std::string header;
            size_t data_rows = 0;
            if (std::getline(input, header)) {
                std::string row;
                while (std::getline(input, row)) ++data_rows;
            }
            size_t csv_files = 0;
            std::error_code scan_error;
            for (std::filesystem::directory_iterator it(temp_dir, scan_error), end;
                 !scan_error && it != end; it.increment(scan_error)) {
                if (it->is_regular_file() && it->path().extension() == ".csv") ++csv_files;
            }
            csv_export_ok = input.eof() &&
                header == "#distance,x,y,z,direction,radius,gradient,interpolate_func,cant,center,gauge" &&
                data_rows == app.model_.own.rows &&
                csv_files == app.model_.other_tracks.size() + 1 && !scan_error;
        }
        std::error_code cleanup_error;
        if (temp_created) {
            std::filesystem::remove_all(temp_dir, cleanup_error);
            csv_cleanup_ok = !cleanup_error;
        }
        csv_export_ok = csv_export_ok && csv_cleanup_ok;

        MapModel alias_fallback_model;
        alias_fallback_model.own.rows = 2;
        alias_fallback_model.own.cols = 11;
        alias_fallback_model.own.data.assign(22, 0.0);
        alias_fallback_model.own.data[0] = 0.0;
        alias_fallback_model.own.data[11] = 100.0;
        alias_fallback_model.own.data[12] = 100.0;
        OtherTrack colliding_alias_track;
        colliding_alias_track.key = "0";
        colliding_alias_track.points.rows = 2;
        colliding_alias_track.points.cols = 8;
        colliding_alias_track.points.data.assign(16, 0.0);
        colliding_alias_track.points.data[0] = 200.0;
        colliding_alias_track.points.data[8] = 300.0;
        colliding_alias_track.points.data[9] = 100.0;
        alias_fallback_model.other_tracks.push_back(std::move(colliding_alias_track));
        TableRow repeater_begin;
        repeater_begin.cells = {{"distance", "10"}, {"method", "Begin"},
                                {"repeaterKey", "fallback"}, {"trackKey", "0"},
                                {"x", "0"}, {"z", "0"}, {"order", "0"}};
        TableRow repeater_end;
        repeater_end.cells = {{"distance", "20"}, {"method", "End"},
                              {"repeaterKey", "fallback"}, {"order", "1"}};
        alias_fallback_model.repeaters.push_back(std::move(repeater_begin));
        alias_fallback_model.repeaters.push_back(std::move(repeater_end));
        app.model_ = std::move(alias_fallback_model);
        app.has_model_ = true;
        app.rebuild_marker_overlay_cache();
        const bool repeater_alias_fallback_ok = app.repeater_marker_cache_.size() == 1 &&
            app.repeater_marker_cache_.front().begin_marker &&
            app.repeater_marker_cache_.front().end_marker &&
            app.repeater_marker_cache_.front().segment.endpoints_valid &&
            std::abs(app.repeater_marker_cache_.front().segment.first_point.x - 10.0) < 1e-9 &&
            std::abs(app.repeater_marker_cache_.front().segment.last_point.x - 20.0) < 1e-9;

    workflow_ok = repeated_load_hash_ok && station_jump_ok && measure_ok && csv_export_ok &&
        repeater_alias_fallback_ok;
    *out << "headless_ui_workflow repeated_load_hash="
         << (repeated_load_hash_ok ? "PASS" : "FAIL")
         << " station_jump=" << (station_jump_ok ? "PASS" : "FAIL")
         << " measure=" << (measure_ok ? "PASS" : "FAIL")
         << " csv_export=" << (csv_export_ok ? "PASS" : "FAIL")
         << " csv_temp_cleanup=" << (csv_cleanup_ok ? "PASS" : "FAIL")
         << " repeater_alias_fallback="
         << (repeater_alias_fallback_ok ? "PASS" : "FAIL")
         << " result=" << (workflow_ok ? "PASS" : "FAIL") << "\n";

    if (!samples.empty()) {
        const OpenBenchSample& first = samples.front();
        const MetricSummary ready_all = summarize_samples(samples, 0,
            [](const OpenBenchSample& sample) { return sample.ready_seconds; });
        const MetricSummary overlay_all = summarize_samples(samples, 0,
            [](const OpenBenchSample& sample) { return sample.overlay_seconds; });
        const size_t steady_first = samples.size() > 1 ? 1 : 0;
        const MetricSummary ready_steady = summarize_samples(samples, steady_first,
            [](const OpenBenchSample& sample) { return sample.ready_seconds; });
        const MetricSummary overlay_steady = summarize_samples(samples, steady_first,
            [](const OpenBenchSample& sample) { return sample.overlay_seconds; });
        const MetricSummary frame_steady = summarize_samples(samples, steady_first,
            [](const OpenBenchSample& sample) { return sample.first_2d_frame_seconds; });
        *out << std::fixed << std::setprecision(6)
             << "open_bench_summary transport=typed_snapshot"
             << " runs=" << samples.size()
             << " first_ready=" << first.ready_seconds << "s"
             << " first_overlay=" << first.overlay_seconds << "s"
             << " all_ready_median=" << ready_all.median << "s"
             << " all_ready_p95=" << ready_all.p95 << "s"
             << " all_overlay_median=" << overlay_all.median << "s"
             << " all_overlay_p95=" << overlay_all.p95 << "s"
             << " steady_runs=" << (samples.size() - steady_first)
             << " steady_ready_median=" << ready_steady.median << "s"
             << " steady_ready_p95=" << ready_steady.p95 << "s"
             << " steady_overlay_median=" << overlay_steady.median << "s"
             << " steady_overlay_p95=" << overlay_steady.p95 << "s"
             << " steady_first_2d_frame_median=" << frame_steady.median << "s"
             << " steady_first_2d_frame_p95=" << frame_steady.p95 << "s\n";
    }

    const bool performance_ok = samples.size() == static_cast<size_t>(options.repeat);
    const bool passed = benchmark_ok && metadata_ok && snapshot_contract_ok &&
        workflow_ok && performance_ok;
    *out << "open_bench result=" << (passed ? "PASS" : "FAIL") << "\n";
    out->flush();
    ImPlot::DestroyContext();
    ImGui::DestroyContext();
    return passed ? 0 : 3;
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
        for (const OtherTrainPathOverlay& overlay : app.other_train_path_cache_) {
            other_train_path_point_count += overlay.points.size();
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

        const PlanData& cached_initial = app.current_plan_data();
        const PlanData uncached_initial = app.build_plan_data(false);
        const bool uncached_match = plan_data_summary_matches(cached_initial, uncached_initial);
        const std::uint64_t hit_count_before = app.plan_data_cache_.rebuild_count;
        app.current_plan_data();
        const bool stable_hit = app.plan_data_cache_.rebuild_count == hit_count_before;

        const double saved_center_x = app.plan_view_.cx;
        const double saved_center_y = app.plan_view_.cy;
        const double saved_rotation = app.plan_view_.rotation;
        app.plan_view_.pan_by_screen_delta(ImVec2(23.0f, -17.0f));
        app.plan_view_.rotation += 0.125;
        const PlanData& panned_data = app.current_plan_data();
        const bool pan_rotation_hit = app.plan_data_cache_.rebuild_count == hit_count_before &&
            plan_data_summary_matches(panned_data, uncached_initial);
        app.plan_view_.cx = saved_center_x;
        app.plan_view_.cy = saved_center_y;
        app.plan_view_.rotation = saved_rotation;

        auto validate_marker_toggle = [&](bool& toggle, unsigned bit, auto projection) {
            const bool original = toggle;
            toggle = false;
            app.current_plan_data();
            const std::uint64_t enabled_count_before = app.plan_data_cache_.rebuild_count;
            toggle = true;
            const PlanData& enabled = app.current_plan_data();
            const size_t enabled_size = projection(enabled).size();
            const PlanData enabled_uncached = app.build_plan_data(false);
            bool passed = app.plan_data_cache_.rebuild_count == enabled_count_before + 1 &&
                (app.plan_data_cache_.marker_visibility_mask & (std::uint32_t{1} << bit)) != 0 &&
                enabled_size == projection(enabled_uncached).size();

            const std::uint64_t disabled_count_before = app.plan_data_cache_.rebuild_count;
            toggle = false;
            const PlanData& disabled = app.current_plan_data();
            const PlanData disabled_uncached = app.build_plan_data(false);
            passed = passed &&
                app.plan_data_cache_.rebuild_count == disabled_count_before + 1 &&
                (app.plan_data_cache_.marker_visibility_mask & (std::uint32_t{1} << bit)) == 0 &&
                projection(disabled).empty() &&
                plan_data_summary_matches(disabled, disabled_uncached);
            toggle = original;
            app.current_plan_data();
            return passed;
        };

        bool marker_toggles_pass = true;
        marker_toggles_pass &= validate_marker_toggle(
            app.show_beacon_markers_, 0,
            [](const PlanData& data) -> const auto& { return data.beacon_markers; });
        marker_toggles_pass &= validate_marker_toggle(
            app.show_pretrain_markers_, 1,
            [](const PlanData& data) -> const auto& { return data.pretrain_markers; });
        marker_toggles_pass &= validate_marker_toggle(
            app.show_irregularity_markers_, 2,
            [](const PlanData& data) -> const auto& { return data.irregularity_markers; });
        marker_toggles_pass &= validate_marker_toggle(
            app.show_map_sound_markers_, 3,
            [](const PlanData& data) -> const auto& { return data.map_sound_markers; });
        marker_toggles_pass &= validate_marker_toggle(
            app.show_map_sound_3d_markers_, 4,
            [](const PlanData& data) -> const auto& { return data.map_sound_3d_markers; });
        marker_toggles_pass &= validate_marker_toggle(
            app.show_rolling_noise_markers_, 5,
            [](const PlanData& data) -> const auto& { return data.rolling_noise_markers; });
        marker_toggles_pass &= validate_marker_toggle(
            app.show_flange_noise_markers_, 6,
            [](const PlanData& data) -> const auto& { return data.flange_noise_markers; });
        marker_toggles_pass &= validate_marker_toggle(
            app.show_joint_noise_markers_, 7,
            [](const PlanData& data) -> const auto& { return data.joint_noise_markers; });
        marker_toggles_pass &= validate_marker_toggle(
            app.show_background_markers_, 8,
            [](const PlanData& data) -> const auto& { return data.background_markers; });
        marker_toggles_pass &= validate_marker_toggle(
            app.show_adhesion_markers_, 9,
            [](const PlanData& data) -> const auto& { return data.adhesion_markers; });
        marker_toggles_pass &= validate_marker_toggle(
            app.show_cab_illuminance_markers_, 10,
            [](const PlanData& data) -> const auto& { return data.cab_illuminance_markers; });
        marker_toggles_pass &= validate_marker_toggle(
            app.show_fog_markers_, 11,
            [](const PlanData& data) -> const auto& { return data.fog_markers; });

        const bool original_curve_visibility = app.show_curve_values_;
        app.show_curve_values_ = false;
        app.current_plan_data();
        const std::uint64_t curve_count_before = app.plan_data_cache_.rebuild_count;
        app.show_curve_values_ = true;
        const PlanData& visible_curves = app.current_plan_data();
        const PlanData visible_curves_uncached = app.build_plan_data(false);
        bool curve_toggle_pass = app.plan_data_cache_.rebuild_count == curve_count_before + 1 &&
            app.plan_data_cache_.show_curve_values &&
            visible_curves.curve_sections.size() == visible_curves_uncached.curve_sections.size() &&
            visible_curves.transition_sections.size() == visible_curves_uncached.transition_sections.size();
        app.show_curve_values_ = false;
        const PlanData& hidden_curves = app.current_plan_data();
        curve_toggle_pass = curve_toggle_pass && !app.plan_data_cache_.show_curve_values &&
            hidden_curves.curve_sections.empty() && hidden_curves.transition_sections.empty();
        app.show_curve_values_ = original_curve_visibility;
        app.current_plan_data();

        auto validate_row_visibility = [&](auto& visibility, auto cache_snapshot) {
            if (visibility.empty()) return true;
            const unsigned char original = visibility.front();
            const std::uint64_t count_before = app.plan_data_cache_.rebuild_count;
            visibility.front() = original == 0 ? 1 : 0;
            const PlanData& cached = app.current_plan_data();
            const PlanData uncached = app.build_plan_data(false);
            bool passed = app.plan_data_cache_.rebuild_count == count_before + 1 &&
                cache_snapshot() == visibility && plan_data_summary_matches(cached, uncached);
            visibility.front() = original;
            app.current_plan_data();
            return passed;
        };
        bool row_visibility_pass = true;
        row_visibility_pass &= validate_row_visibility(
            app.structure_row_visible_,
            [&]() -> const auto& { return app.plan_data_cache_.structure_row_visible; });
        row_visibility_pass &= validate_row_visibility(
            app.repeater_row_visible_,
            [&]() -> const auto& { return app.plan_data_cache_.repeater_row_visible; });
        row_visibility_pass &= validate_row_visibility(
            app.signal_row_visible_,
            [&]() -> const auto& { return app.plan_data_cache_.signal_row_visible; });
        row_visibility_pass &= validate_row_visibility(
            app.other_train_path_visible_,
            [&]() -> const auto& { return app.plan_data_cache_.other_train_path_visible; });

        const Mode original_mode = app.mode_;
        const std::uint64_t mode_count_before = app.plan_data_cache_.rebuild_count;
        app.mode_ = original_mode == Mode::Pan ? Mode::Measure : Mode::Pan;
        const PlanData& changed_mode = app.current_plan_data();
        const PlanData changed_mode_uncached = app.build_plan_data(false);
        bool keyed_state_pass = app.plan_data_cache_.rebuild_count == mode_count_before + 1 &&
            plan_data_summary_matches(changed_mode, changed_mode_uncached);
        app.mode_ = original_mode;
        app.current_plan_data();

        const double original_scale = app.plan_view_.scale;
        const std::uint64_t scale_count_before = app.plan_data_cache_.rebuild_count;
        app.plan_view_.scale = original_scale + 0.125;
        const PlanData& changed_scale = app.current_plan_data();
        const PlanData changed_scale_uncached = app.build_plan_data(false);
        keyed_state_pass = keyed_state_pass &&
            app.plan_data_cache_.rebuild_count == scale_count_before + 1 &&
            plan_data_summary_matches(changed_scale, changed_scale_uncached);
        app.plan_view_.scale = original_scale;
        app.current_plan_data();

        const std::uint64_t source_revision_before = app.plan_data_source_revision_;
        app.rebuild_marker_overlay_cache();
        bool source_revision_pass = !app.plan_data_cache_.valid &&
            app.plan_data_source_revision_ == source_revision_before + 1;
        const PlanData& rebuilt_source_data = app.current_plan_data();
        const PlanData rebuilt_source_uncached = app.build_plan_data(false);
        source_revision_pass = source_revision_pass &&
            plan_data_summary_matches(rebuilt_source_data, rebuilt_source_uncached);

        const bool cache_checks_pass = uncached_match && stable_hit && pan_rotation_hit &&
            marker_toggles_pass && curve_toggle_pass && row_visibility_pass &&
            keyed_state_pass && source_revision_pass;
        *out << "plan_cache_checks uncached_match=" << (uncached_match ? "PASS" : "FAIL")
             << " stable_hit=" << (stable_hit ? "PASS" : "FAIL")
             << " pan_rotation_hit=" << (pan_rotation_hit ? "PASS" : "FAIL")
             << " marker_toggles=" << (marker_toggles_pass ? "PASS" : "FAIL")
             << " curve_toggle=" << (curve_toggle_pass ? "PASS" : "FAIL")
             << " row_visibility=" << (row_visibility_pass ? "PASS" : "FAIL")
             << " keyed_state=" << (keyed_state_pass ? "PASS" : "FAIL")
             << " source_revision=" << (source_revision_pass ? "PASS" : "FAIL")
             << " result=" << (cache_checks_pass ? "PASS" : "FAIL") << "\n";

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

        const FrameTimingStats timing = calculate_frame_timing_stats(frame_ms);
        bool pass = cache_checks_pass && timing.p95_ms <= max_frame_ms;

        *out << std::fixed << std::setprecision(3)
             << "plan_bench avg_ms=" << timing.average_ms
             << " min_ms=" << timing.minimum_ms
             << " p95_ms=" << timing.p95_ms
             << " max_ms=" << timing.maximum_ms
             << " p95_fps=" << timing.p95_fps
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
                                              int scene_model_workers,
                                              bool disable_scene_texture_cache,
                                              const std::string& output_path) {
    ScopedComApartment com_apartment;
    if (!com_apartment.ready()) {
        std::cerr << "debug headless scene3d benchmark failed: COM initialization\n";
        return 6;
    }
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
         << " window_forward_m=" << format_double(window_forward_m, 3)
         << " scene_model_workers=" << scene_model_workers
         << " texture_cache=" << (disable_scene_texture_cache ? "disabled" : "enabled") << "\n";
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
        app.cp_start_ = app.model_.cp_arb[0];
        app.cp_end_ = app.model_.cp_arb[1];
        app.cp_interval_ = app.model_.cp_arb[2];
        app.unit_distance_ = unit_distance;
        app.rebuild_marker_overlay_cache();
        app.reset_marker_visibility();
        app.scene_preview_started_ = true;
        app.scene_preview_canvas_->set_scene_window(window_back_m, window_forward_m);
        app.scene_preview_canvas_->set_debug_scene_loading_tuning(
            static_cast<size_t>(std::max(scene_model_workers, 0)),
            !disable_scene_texture_cache);
        const auto preview_load_started_at = std::chrono::steady_clock::now();
        ImGui::NewFrame();
        const double scene_build_seconds = app.rebuild_scene_preview();
        ImGui::EndFrame();
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
        constexpr auto k_scene_load_timeout = std::chrono::seconds(120);
        int warmup_frames = 0;
        bool load_completed = false;
        auto preview_load_finished_at = std::chrono::steady_clock::now();
        for (;; ++warmup_frames) {
            render_frame();
            Canvas3DSceneStats stats = app.scene_preview_canvas_->scene_stats();
            if (!stats.loading && stats.model_ready_count + stats.model_failed_count >= stats.model_path_count) {
                load_completed = true;
                preview_load_finished_at = std::chrono::steady_clock::now();
                break;
            }
            if (std::chrono::steady_clock::now() - preview_load_started_at >= k_scene_load_timeout) {
                preview_load_finished_at = std::chrono::steady_clock::now();
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        for (int i = 0; i < 5; ++i) render_frame();
        for (const std::string& message :
             app.scene_preview_canvas_->drain_scene_load_messages()) {
            *out << message << "\n";
        }
        Canvas3DSceneStats warmed_stats = app.scene_preview_canvas_->scene_stats();
        const double scene_build_ms = scene_build_seconds * 1000.0;
        const double preview_load_ms = std::chrono::duration<double, std::milli>(
            preview_load_finished_at - preview_load_started_at).count();
        const bool terminal_model_state =
            warmed_stats.model_ready_count + warmed_stats.model_failed_count >= warmed_stats.model_path_count;
        *out << "stage=warmup-complete"
             << " warmup_frames=" << warmup_frames
             << " model_ready=" << warmed_stats.model_ready_count
             << " model_failed=" << warmed_stats.model_failed_count
             << " model_total=" << warmed_stats.model_path_count
             << " drawn_instances=" << warmed_stats.drawn_instance_count
             << " drawn_track_chunks=" << warmed_stats.drawn_track_chunk_count << "\n";
        *out << std::fixed << std::setprecision(3)
             << "scene3d_load scene_build_ms=" << scene_build_ms
             << " track_gpu_ms=" << warmed_stats.track_gpu_setup_seconds * 1000.0
             << " model_queue_ms=" << warmed_stats.model_queue_seconds * 1000.0
             << " model_load_ms=" << warmed_stats.model_load_seconds * 1000.0
             << " preview_load_ms=" << preview_load_ms
             << " wait_frames=" << warmup_frames
             << " worker_count=" << warmed_stats.model_worker_count
             << " texture_cache_hits=" << warmed_stats.texture_cache_hit_count
             << " texture_cache_misses=" << warmed_stats.texture_cache_miss_count
             << " model_ready=" << warmed_stats.model_ready_count
             << " model_failed=" << warmed_stats.model_failed_count
             << " model_total=" << warmed_stats.model_path_count
             << " result=" << (load_completed && terminal_model_state ? "PASS" : "TIMEOUT") << "\n";
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

        const FrameTimingStats timing = calculate_frame_timing_stats(frame_ms);
        bool pass = load_completed && terminal_model_state && timing.p95_ms <= max_frame_ms;
        Canvas3DSceneStats final_stats = app.scene_preview_canvas_->scene_stats();

        const size_t fog_row_count = app.model_.fogs.size();
        const Canvas3DSceneFogDebugState initial_fog_state =
            app.scene_preview_canvas_->debug_scene_fog_state();
        Canvas3DSceneFogDebugState probed_fog_state = initial_fog_state;
        size_t fog_changed_pixel_count = 0;
        std::uint64_t fog_total_abs_rgb_difference = 0;
        unsigned fog_max_channel_difference = 0;
        std::string fog_probe_result = "SKIP_NO_ROWS";
        std::string fog_probe_error;
        if (fog_row_count > 0 && initial_fog_state.keyframe_count == 0) {
            fog_probe_result = "FAIL_NO_KEYFRAMES";
            pass = false;
        } else if (initial_fog_state.keyframe_count > 0 && initial_fog_state.max_density <= 0.0f) {
            fog_probe_result = "SKIP_ZERO_DENSITY";
        } else if (initial_fog_state.max_density > 0.0f) {
            const bool previous_fog_enabled = initial_fog_state.setting_enabled;
            const bool jumped = app.scene_preview_canvas_->jump_scene_camera_to_distance(
                initial_fog_state.max_density_distance);
            std::vector<std::uint8_t> fog_off_pixels;
            std::vector<std::uint8_t> fog_on_pixels;
            int fog_off_width = 0;
            int fog_off_height = 0;
            int fog_on_width = 0;
            int fog_on_height = 0;
            bool captured_off = false;
            bool captured_on = false;
            if (jumped) {
                app.scene_preview_canvas_->set_scene_fog_enabled(false);
                render_frame();
                captured_off = app.scene_preview_canvas_->debug_read_scene_render_pixels(
                    fog_off_pixels, fog_off_width, fog_off_height, fog_probe_error);
                app.scene_preview_canvas_->set_scene_fog_enabled(true);
                render_frame();
                probed_fog_state = app.scene_preview_canvas_->debug_scene_fog_state();
                if (captured_off) {
                    captured_on = app.scene_preview_canvas_->debug_read_scene_render_pixels(
                        fog_on_pixels, fog_on_width, fog_on_height, fog_probe_error);
                }
            } else {
                fog_probe_error = "failed to jump scene camera to maximum-density fog keyframe";
            }
            app.scene_preview_canvas_->set_scene_fog_enabled(previous_fog_enabled);

            const bool matching_pixels = captured_off && captured_on &&
                fog_off_width == fog_on_width && fog_off_height == fog_on_height &&
                fog_off_pixels.size() == fog_on_pixels.size();
            if (matching_pixels) {
                for (size_t offset = 0; offset + 3 < fog_off_pixels.size(); offset += 4) {
                    bool pixel_changed = false;
                    for (size_t channel = 0; channel < 3; ++channel) {
                        const unsigned off = fog_off_pixels[offset + channel];
                        const unsigned on = fog_on_pixels[offset + channel];
                        const unsigned difference = off > on ? off - on : on - off;
                        fog_total_abs_rgb_difference += difference;
                        fog_max_channel_difference = std::max(fog_max_channel_difference, difference);
                        pixel_changed = pixel_changed || difference != 0;
                    }
                    if (pixel_changed) ++fog_changed_pixel_count;
                }
            }

            constexpr unsigned k_minimum_visible_fog_channel_difference = 8;
            const bool fog_probe_pass = matching_pixels && probed_fog_state.sampled_enabled &&
                probed_fog_state.shader_ready && probed_fog_state.fog_draw_part_count > 0 &&
                fog_changed_pixel_count > 0 &&
                fog_max_channel_difference >= k_minimum_visible_fog_channel_difference;
            fog_probe_result = fog_probe_pass ? "PASS" : "FAIL";
            if (!fog_probe_pass) pass = false;
        }

        *out << std::fixed << std::setprecision(6)
             << "scene3d_fog rows=" << fog_row_count
             << " keyframes=" << initial_fog_state.keyframe_count
             << " initial_distance=" << initial_fog_state.camera_distance
             << " initial_density=" << initial_fog_state.density
             << " probe_distance=" << probed_fog_state.camera_distance
             << " density=" << probed_fog_state.density
             << " color=" << probed_fog_state.color.x << ","
             << probed_fog_state.color.y << "," << probed_fog_state.color.z
             << " max_density=" << initial_fog_state.max_density
             << " max_density_distance=" << initial_fog_state.max_density_distance
             << " shader_ready=" << (probed_fog_state.shader_ready ? "true" : "false")
             << " fog_draw_parts=" << probed_fog_state.fog_draw_part_count
             << " changed_pixels=" << fog_changed_pixel_count
             << " total_abs_rgb_diff=" << fog_total_abs_rgb_difference
             << " max_channel_diff=" << fog_max_channel_difference
             << " min_visible_channel_diff=8"
             << " result=" << fog_probe_result;
        if (!fog_probe_error.empty()) *out << " error=\"" << fog_probe_error << "\"";
        *out << "\n";

        *out << std::fixed << std::setprecision(3)
             << "scene3d_bench avg_ms=" << timing.average_ms
             << " min_ms=" << timing.minimum_ms
             << " p95_ms=" << timing.p95_ms
             << " max_ms=" << timing.maximum_ms
             << " p95_fps=" << timing.p95_fps
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

    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(1280.0f, 720.0f);
    io.DeltaTime = 1.0f / 60.0f;
    io.IniFilename = nullptr;
    io.Fonts->AddFontDefault();
    io.Fonts->Build();
    ImGui::NewFrame();

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
    ImGui::EndFrame();
    ImGui::DestroyContext();
    context->ClearState();
    context->Flush();
    release_com(context);
    release_com(device);
    if (exit_code == 0) *out << "result=PASS\n";
    out->flush();
    return exit_code;
}
#endif
