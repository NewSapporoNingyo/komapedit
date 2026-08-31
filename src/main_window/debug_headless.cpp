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
#include <atomic>
#include <array>
#include <chrono>
#include <cctype>
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
#include <thread>
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
    double median_ms = 0.0;
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
    const size_t middle = sorted_ms.size() / 2;
    stats.median_ms = sorted_ms.size() % 2 == 0
        ? (sorted_ms[middle - 1] + sorted_ms[middle]) * 0.5
        : sorted_ms[middle];
    stats.p95_ms = sorted_ms[p95_index];
    stats.maximum_ms = sorted_ms.back();
    stats.p95_fps = stats.p95_ms > 0.0 ? 1000.0 / stats.p95_ms : 0.0;
    return stats;
}

std::array<size_t, 27> plan_data_size_summary(const PlanData& data) {
    return {
        data.own.size(),
        data.stations.size(),
        data.speedlimits.size(),
        data.structure_markers.size(),
        data.repeater_markers.size(),
        data.signal_markers.size(),
        data.section_markers.size(),
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
        data.legacy_fog_markers.size(),
        data.draw_distance_markers.size(),
        data.curve_edit_markers.size(),
        data.gradient_edit_markers.size(),
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

constexpr const char* k_default_edit_map_path =
    "E:\\Railway\\BveTsWorkspace\\BVE-Gensokyo-Railway\\GSR\\Scenarios_GSR\\map\\"
    "Config_Map121M-ATSP+Ps_Ask.txt";

template <typename Options, typename CommitHandler>
Options parse_headless_optional_map_edit_options(
    const std::vector<std::string>& args, std::string_view command,
    CommitHandler&& handle_commit) {
    Options options;
    for (size_t i = 1; i < args.size(); ++i) {
        const std::string& arg = args[i];
        if (std::string_view(arg) == command) {
            options.requested = true;
            if (i + 1 < args.size() && args[i + 1].rfind("--", 0) != 0) {
                options.path = args[++i];
            }
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
            handle_commit(options);
        }
    }
    if (options.requested && options.path.empty()) options.path = k_default_edit_map_path;
    return options;
}

template <typename Options, typename CommitHandler>
Options parse_headless_required_map_edit_options(
    const std::vector<std::string>& args, std::string_view command,
    CommitHandler&& handle_commit) {
    Options options;
    for (size_t i = 1; i < args.size(); ++i) {
        const std::string& arg = args[i];
        if (std::string_view(arg) == command) {
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
            handle_commit(options);
        }
    }
    if (options.requested && options.path.empty() && options.error.empty()) {
        options.error = std::string(command) + " requires a map path";
    }
    return options;
}

template <typename Options>
Options parse_headless_output_only_options(
    const std::vector<std::string>& args, std::string_view command) {
    Options options;
    for (size_t i = 1; i < args.size(); ++i) {
        const std::string& arg = args[i];
        if (std::string_view(arg) == command) {
            options.requested = true;
        } else if (arg == "--headless-output") {
            const std::string* value =
                take_option_value(args, i, arg, "a path", options.error);
            if (!value) return options;
            options.output_path = *value;
        }
    }
    return options;
}

HeadlessIncludeDeleteOptions parse_headless_include_delete_options(
    const std::vector<std::string>& args) {
    HeadlessIncludeDeleteOptions options;
    for (size_t i = 1; i < args.size(); ++i) {
        const std::string& arg = args[i];
        if (arg == "--debug-headless-include-delete") {
            options.requested = true;
            const std::string* value =
                take_option_value(args, i, arg, "a map path", options.error);
            if (!value) return options;
            options.path = *value;
        } else if (arg == "--index") {
            const std::string* value =
                take_option_value(args, i, arg, "an index", options.error);
            if (!value) return options;
            try {
                size_t consumed = 0;
                long parsed = std::stol(*value, &consumed);
                if (consumed != value->size() || parsed < 0) {
                    options.error = "--index must be a nonnegative integer";
                    return options;
                }
                options.index = static_cast<int>(parsed);
            } catch (const std::exception&) {
                options.error = "--index must be a nonnegative integer";
                return options;
            }
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
        options.error = "--debug-headless-include-delete requires a map path";
    }
    return options;
}

HeadlessIncludeReplaceOptions parse_headless_include_replace_options(
    const std::vector<std::string>& args) {
    HeadlessIncludeReplaceOptions options;
    for (size_t i = 1; i < args.size(); ++i) {
        const std::string& arg = args[i];
        if (arg == "--debug-headless-include-replace") {
            options.requested = true;
            const std::string* value =
                take_option_value(args, i, arg, "a map path", options.error);
            if (!value) return options;
            options.path = *value;
        } else if (arg == "--new-path") {
            const std::string* value =
                take_option_value(args, i, arg, "a path", options.error);
            if (!value) return options;
            options.new_path = *value;
        } else if (arg == "--index") {
            const std::string* value =
                take_option_value(args, i, arg, "an index", options.error);
            if (!value) return options;
            try {
                size_t consumed = 0;
                long parsed = std::stol(*value, &consumed);
                if (consumed != value->size() || parsed < 0) {
                    options.error = "--index must be a nonnegative integer";
                    return options;
                }
                options.index = static_cast<int>(parsed);
            } catch (const std::exception&) {
                options.error = "--index must be a nonnegative integer";
                return options;
            }
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
    if (!options.requested || !options.error.empty()) return options;
    if (options.path.empty()) {
        options.error = "--debug-headless-include-replace requires a map path";
    } else if (options.new_path.empty()) {
        options.error = "--debug-headless-include-replace requires --new-path";
    } else if (options.new_path.find('\'') != std::string::npos) {
        options.error = "--new-path must not contain a single quote";
    }
    return options;
}

HeadlessIncludeImportCreateOptions parse_headless_include_import_create_options(
    const std::vector<std::string>& args) {
    return parse_headless_required_map_edit_options<
        HeadlessIncludeImportCreateOptions>(
        args, "--debug-headless-include-import-create",
        [](HeadlessIncludeImportCreateOptions&) {});
}

HeadlessResourceListReplaceOptions parse_headless_resource_list_replace_options(
    const std::vector<std::string>& args) {
    return parse_headless_required_map_edit_options<
        HeadlessResourceListReplaceOptions>(
        args, "--debug-headless-resource-list-replace",
        [](HeadlessResourceListReplaceOptions& options) {
            options.error =
                "--debug-headless-resource-list-replace is memory-apply only";
        });
}

HeadlessResourceListInsertOptions parse_headless_resource_list_insert_options(
    const std::vector<std::string>& args) {
    HeadlessResourceListInsertOptions options;
    for (size_t i = 1; i < args.size(); ++i) {
        const std::string& arg = args[i];
        if (arg == "--debug-headless-resource-list-insert") {
            options.requested = true;
            const std::string* value =
                take_option_value(args, i, arg, "a map path", options.error);
            if (!value) return options;
            options.path = *value;
        } else if (arg == "--kind") {
            const std::string* value =
                take_option_value(args, i, arg, "structure or signal", options.error);
            if (!value) return options;
            options.kind = ascii_lower(*value);
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
            options.error =
                "--debug-headless-resource-list-insert is memory-apply only";
        }
    }
    if (!options.requested || !options.error.empty()) return options;
    if (options.path.empty()) {
        options.error = "--debug-headless-resource-list-insert requires a map path";
    } else if (options.kind != "structure" && options.kind != "signal") {
        options.error =
            "--debug-headless-resource-list-insert requires --kind structure or signal";
    }
    return options;
}

HeadlessNewFileWizardOptions parse_headless_new_file_wizard_options(
    const std::vector<std::string>& args) {
    return parse_headless_required_map_edit_options<HeadlessNewFileWizardOptions>(
        args, "--debug-headless-new-file-wizard",
        [](HeadlessNewFileWizardOptions&) {});
}

HeadlessFreshResourceListWorkflowOptions
parse_headless_fresh_resource_list_workflow_options(
    const std::vector<std::string>& args) {
    return parse_headless_required_map_edit_options<
        HeadlessFreshResourceListWorkflowOptions>(
        args, "--debug-headless-fresh-resource-list-workflow",
        [](HeadlessFreshResourceListWorkflowOptions&) {});
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

HeadlessLoadScenarioOptions parse_headless_load_scenario_options(
    const std::vector<std::string>& args) {
    HeadlessLoadScenarioOptions options;
    for (size_t i = 1; i < args.size(); ++i) {
        const std::string& arg = args[i];
        if (arg == "--headless-load-scenario") {
            options.requested = true;
            const std::string* value =
                take_option_value(args, i, arg, "a scenario path", options.error);
            if (!value) return options;
            options.path = *value;
        } else if (arg == "--scenario-index") {
            if (!parse_integer_option(args, i, arg, 0, 1000000,
                                      "--scenario-index must be between 0 and 1000000",
                                      options.scenario_index, options.error)) {
                return options;
            }
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
        } else if (arg == "--expect-no-map") {
            options.expect_no_map = true;
        } else if (arg == "--headless-output") {
            const std::string* value =
                take_option_value(args, i, arg, "a path", options.error);
            if (!value) return options;
            options.output_path = *value;
        }
    }
    if (options.requested && options.path.empty() && options.error.empty()) {
        options.error = "--headless-load-scenario requires a scenario path";
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
        } else if (arg == "--interaction") {
            const std::string* value = take_option_value(
                args, i, arg, "pan, measure-stationary, or measure-moving", options.error);
            if (!value) return options;
            if (*value != "pan" && *value != "measure-stationary" &&
                *value != "measure-moving") {
                options.error = "--interaction must be pan, measure-stationary, or measure-moving";
                return options;
            }
            options.interaction = *value;
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

HeadlessEditRoundtripOptions parse_headless_edit_roundtrip_options(
    const std::vector<std::string>& args) {
    return parse_headless_required_map_edit_options<HeadlessEditRoundtripOptions>(
        args, "--debug-headless-edit-roundtrip",
        [](HeadlessEditRoundtripOptions&) {});
}

HeadlessOwnTrackEditOptions parse_headless_own_track_edit_options(
    const std::vector<std::string>& args) {
    return parse_headless_optional_map_edit_options<HeadlessOwnTrackEditOptions>(
        args, "--debug-headless-own-track-edit",
        [](HeadlessOwnTrackEditOptions&) {});
}

HeadlessOtherTrackEditOptions parse_headless_other_track_edit_options(
    const std::vector<std::string>& args) {
    return parse_headless_optional_map_edit_options<HeadlessOtherTrackEditOptions>(
        args, "--debug-headless-other-track-edit",
        [](HeadlessOtherTrackEditOptions& options) { options.commit = true; });
}

HeadlessDistanceEditBatchOptions parse_headless_distance_edit_batch_options(
    const std::vector<std::string>& args) {
    return parse_headless_optional_map_edit_options<HeadlessDistanceEditBatchOptions>(
        args, "--debug-headless-distance-edit-batch",
        [](HeadlessDistanceEditBatchOptions& options) { options.commit = true; });
}

HeadlessStationListEditOptions parse_headless_station_list_edit_options(
    const std::vector<std::string>& args) {
    return parse_headless_required_map_edit_options<HeadlessStationListEditOptions>(
        args, "--debug-headless-station-list-edit",
        [](HeadlessStationListEditOptions& options) { options.commit = true; });
}

HeadlessRepeaterEditBatchOptions parse_headless_repeater_edit_batch_options(
    const std::vector<std::string>& args) {
    return parse_headless_optional_map_edit_options<HeadlessRepeaterEditBatchOptions>(
        args, "--debug-headless-repeater-edit-batch",
        [](HeadlessRepeaterEditBatchOptions& options) { options.commit = true; });
}

HeadlessRepeaterKeyEditOptions parse_headless_repeater_key_edit_options(
    const std::vector<std::string>& args) {
    return parse_headless_required_map_edit_options<HeadlessRepeaterKeyEditOptions>(
        args, "--debug-headless-repeater-key-edit",
        [](HeadlessRepeaterKeyEditOptions& options) { options.commit = true; });
}

HeadlessOtherTrackKeyEditOptions parse_headless_other_track_key_edit_options(
    const std::vector<std::string>& args) {
    return parse_headless_required_map_edit_options<HeadlessOtherTrackKeyEditOptions>(
        args, "--debug-headless-other-track-key-edit",
        [](HeadlessOtherTrackKeyEditOptions& options) { options.commit = true; });
}

HeadlessSectionEditBatchOptions parse_headless_section_edit_batch_options(
    const std::vector<std::string>& args) {
    return parse_headless_optional_map_edit_options<HeadlessSectionEditBatchOptions>(
        args, "--debug-headless-section-edit-batch",
        [](HeadlessSectionEditBatchOptions& options) { options.commit = true; });
}

HeadlessInsertEditOptions parse_headless_insert_edit_options(const std::vector<std::string>& args) {
    HeadlessInsertEditOptions options = parse_headless_optional_map_edit_options<HeadlessInsertEditOptions>(
        args, "--debug-headless-insert-edit",
        [](HeadlessInsertEditOptions& parsed_options) { parsed_options.commit = true; });
    if (!options.requested) return options;
    for (const std::string& arg : args) {
        if (arg == "--repeater-only") options.repeater_only = true;
    }
    return options;
}

HeadlessNewElementEditOptions parse_headless_new_element_edit_options(
    const std::vector<std::string>& args) {
    return parse_headless_required_map_edit_options<HeadlessNewElementEditOptions>(
        args, "--debug-headless-new-element-edit",
        [](HeadlessNewElementEditOptions& options) { options.commit = true; });
}

HeadlessLightEditOptions parse_headless_light_edit_options(
    const std::vector<std::string>& args) {
    return parse_headless_required_map_edit_options<HeadlessLightEditOptions>(
        args, "--debug-headless-light-edit",
        [](HeadlessLightEditOptions& options) {
            options.error = "--debug-headless-light-edit is memory-apply only";
        });
}

HeadlessCurveParameterEditOptions parse_headless_curve_parameter_edit_options(
    const std::vector<std::string>& args) {
    return parse_headless_required_map_edit_options<HeadlessCurveParameterEditOptions>(
        args, "--debug-headless-curve-parameter-edit",
        [](HeadlessCurveParameterEditOptions& options) {
            options.error =
                "--debug-headless-curve-parameter-edit is memory-apply only";
        });
}

HeadlessStationPutMarginEditOptions parse_headless_station_put_margin_edit_options(
    const std::vector<std::string>& args) {
    return parse_headless_required_map_edit_options<HeadlessStationPutMarginEditOptions>(
        args, "--debug-headless-station-put-margin-edit",
        [](HeadlessStationPutMarginEditOptions& options) {
            options.error =
                "--debug-headless-station-put-margin-edit is memory-apply only";
        });
}

HeadlessSparseNewElementOptions parse_headless_sparse_new_element_options(
    const std::vector<std::string>& args) {
    return parse_headless_required_map_edit_options<HeadlessSparseNewElementOptions>(
        args, "--debug-headless-sparse-new-element",
        [](HeadlessSparseNewElementOptions& options) {
            options.error =
                "--debug-headless-sparse-new-element is memory-apply only";
        });
}

HeadlessTableFindOptions parse_headless_table_find_options(
    const std::vector<std::string>& args) {
    return parse_headless_output_only_options<HeadlessTableFindOptions>(
        args, "--debug-headless-table-find");
}

HeadlessTouchInputOptions parse_headless_touch_input_options(
    const std::vector<std::string>& args) {
    return parse_headless_output_only_options<HeadlessTouchInputOptions>(
        args, "--debug-headless-touch-input");
}

HeadlessSettingsPersistenceOptions parse_headless_settings_persistence_options(
    const std::vector<std::string>& args) {
    return parse_headless_output_only_options<HeadlessSettingsPersistenceOptions>(
        args, "--debug-headless-settings-persistence");
}

HeadlessDiagnosticsPopupBenchOptions parse_headless_diagnostics_popup_bench_options(
    const std::vector<std::string>& args) {
    return parse_headless_output_only_options<HeadlessDiagnosticsPopupBenchOptions>(
        args, "--debug-headless-diagnostics-popup-bench");
}

HeadlessSceneLoaderContractOptions parse_headless_scene_loader_contract_options(
    const std::vector<std::string>& args) {
    return parse_headless_output_only_options<HeadlessSceneLoaderContractOptions>(
        args, "--debug-headless-scene-loader-contract");
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
    KmeByteHash64 hash;
    const size_t count = buffer.rows * buffer.cols;
    for (size_t i = 0; i < count; ++i) {
        const double value = buffer.data[i];
        summary.finite = summary.finite && std::isfinite(value);
        hash.integer(hash_double_bits(value));
    }
    summary.hash = hash.value;
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
        const uint64_t legacy_fog_count = snapshot.legacy_fog_count;
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
             << " legacyFog=" << legacy_fog_count
             << " othertracks=" << other_count;
        print_headless_buffer_summary(*out, "own", own);
        print_headless_buffer_summary(*out, "curve", curve);
        print_headless_buffer_summary(*out, "structures", structures);
        print_headless_buffer_summary(*out, "other", other_total);
        *out << "\n";
    }
    return 0;
}

int run_headless_load_scenario(const HeadlessLoadScenarioOptions& options) {
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

    *out << "komapedit headless-load-scenario path=\"" << options.path
         << "\" scenario_index=" << options.scenario_index
         << " unit_distance=" << format_double(options.unit_distance, 3)
         << " load_profile=" << options.load_profile << "\n";

    auto fail = [&](const std::string& message) {
        *out << "error=" << message << "\nresult=FAIL\n";
        out->flush();
        std::cerr << message << "\n";
        return 2;
    };

    const int probe_kind = kv_probe_file_kind(options.path.c_str());
    *out << "probe_kind="
         << (probe_kind == KV_FILE_KIND_SCENARIO ? "scenario"
             : probe_kind == KV_FILE_KIND_MAP ? "map" : "unknown")
         << "\n";
    if (probe_kind != KV_FILE_KIND_SCENARIO) {
        return fail("header probe did not identify a BVE Scenario file");
    }

    const KvScenarioSnapshot* scenario_snapshot = kv_load_scenario_snapshot(
        options.path.c_str(), KV_SCENARIO_SNAPSHOT_VERSION);
    if (!scenario_snapshot) {
        const char* error = kv_get_last_error();
        return fail(std::string("scenario preview snapshot failed: ") +
                    (error && *error ? error : "maploader failed"));
    }
    const auto snapshot_fail = [&](const std::string& message) {
        kv_free_scenario_snapshot(scenario_snapshot);
        return fail(message);
    };
    if (scenario_snapshot->version != KV_SCENARIO_SNAPSHOT_VERSION ||
        scenario_snapshot->structure_size < sizeof(KvScenarioSnapshot) ||
        scenario_snapshot->string_size >
            static_cast<uint64_t>(std::numeric_limits<size_t>::max())) {
        return snapshot_fail("scenario preview snapshot version or size is invalid");
    }

    const auto copy_string = [&](KvStringRef reference, std::string& value) {
        if (reference.offset > scenario_snapshot->string_size ||
            reference.length > scenario_snapshot->string_size - reference.offset ||
            (reference.length != 0 && !scenario_snapshot->string_data)) {
            return false;
        }
        value.assign(scenario_snapshot->string_data
                         ? scenario_snapshot->string_data + static_cast<size_t>(reference.offset)
                         : "",
                     static_cast<size_t>(reference.length));
        return true;
    };
    const auto copy_paths = [&](const KvScenarioPathWeightRow* rows, uint64_t count,
                                std::vector<ScenarioPreviewPath>& paths) {
        if (count > static_cast<uint64_t>(std::numeric_limits<size_t>::max()) ||
            (count != 0 && !rows)) {
            return false;
        }
        paths.reserve(static_cast<size_t>(count));
        for (uint64_t i = 0; i < count; ++i) {
            std::string path;
            if (!copy_string(rows[i].path, path) || !std::isfinite(rows[i].weight) ||
                rows[i].weight <= 0.0 || rows[i].has_explicit_weight > 1u) {
                return false;
            }
            paths.push_back({std::move(path), rows[i].weight,
                             rows[i].has_explicit_weight != 0});
        }
        return true;
    };

    ScenarioPreview preview;
    if (!copy_string(scenario_snapshot->title, preview.title) ||
        !copy_paths(scenario_snapshot->routes, scenario_snapshot->route_count, preview.routes) ||
        !copy_string(scenario_snapshot->route_title, preview.route_title) ||
        !copy_paths(scenario_snapshot->vehicles, scenario_snapshot->vehicle_count, preview.vehicles) ||
        !copy_string(scenario_snapshot->vehicle_title, preview.vehicle_title) ||
        !copy_string(scenario_snapshot->author, preview.author) ||
        !copy_string(scenario_snapshot->image, preview.image) ||
        !copy_string(scenario_snapshot->comment, preview.comment)) {
        return snapshot_fail("scenario preview snapshot contains invalid field data");
    }
    kv_free_scenario_snapshot(scenario_snapshot);

    const auto print_paths = [&](const char* field,
                                 const std::vector<ScenarioPreviewPath>& paths) {
        *out << "scenario." << field << "_count=" << paths.size() << "\n";
        for (size_t i = 0; i < paths.size(); ++i) {
            *out << "scenario." << field << "[" << i << "] path="
                 << '"' << paths[i].path << '"'
                 << " weight=" << format_double(paths[i].weight, 6)
                 << " explicit_weight=" << (paths[i].has_explicit_weight ? 1 : 0)
                 << "\n";
        }
    };
    *out << "scenario_preview=loaded\n"
         << "scenario_preview_field_count=8\n"
         << "scenario.Title=\"" << preview.title << "\"\n";
    print_paths("Route", preview.routes);
    *out << "scenario.RouteTitle=\"" << preview.route_title << "\"\n";
    print_paths("Vehicle", preview.vehicles);
    *out << "scenario.VehicleTitle=\"" << preview.vehicle_title << "\"\n"
         << "scenario.Author=\"" << preview.author << "\"\n"
         << "scenario.Image=\"" << preview.image << "\"\n"
         << "scenario.Comment=\"" << preview.comment << "\"\n";

    uint64_t candidate_count = 0;
    const KvScenarioRouteCandidate* candidates =
        kv_resolve_scenario_routes(options.path.c_str(), &candidate_count);
    if ((!candidates || candidate_count == 0) && options.expect_no_map) {
        // Expected degraded state: the scenario preview stays loaded and no
        // map load is attempted, mirroring the GUI document lifecycle in
        // App::perform_open_document() when route resolution is unavailable.
        const char* error = kv_get_last_error();
        kv_free_scenario_candidates(candidates);
        *out << "map_load=not_attempted\n"
             << "route_resolution=unavailable: "
             << (error && *error ? error : "maploader failed") << "\n"
             << "result=PASS\n";
        out->flush();
        return 0;
    }
    if (!candidates || candidate_count == 0) {
        const char* error = kv_get_last_error();
        return fail(std::string("scenario route resolution failed: ") +
                    (error && *error ? error : "maploader failed"));
    }
    *out << "candidate_count=" << candidate_count << "\n";
    for (uint64_t i = 0; i < candidate_count; ++i) {
        *out << "candidate[" << i << "] text=\"" << candidates[i].route_text
             << "\" resolved=\"" << candidates[i].resolved_path << "\"\n";
    }
    if (options.scenario_index < 0 ||
        static_cast<uint64_t>(options.scenario_index) >= candidate_count) {
        kv_free_scenario_candidates(candidates);
        return fail("--scenario-index is out of range");
    }
    const std::string selected_route =
        candidates[static_cast<size_t>(options.scenario_index)].resolved_path;
    kv_free_scenario_candidates(candidates);
    *out << "selected_route=\"" << selected_route << "\"\n";

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

    auto started_at = std::chrono::steady_clock::now();
    const bool edit_profile = options.load_profile == "edit";
    unsigned load_flags = edit_profile ? KV_LOAD_EDIT_METADATA : KV_LOAD_PREVIEW;
    void* handle = kv_load_map_ex(selected_route.c_str(), options.unit_distance, load_flags);
    auto loaded_at = std::chrono::steady_clock::now();
    flush_logs();
    if (!handle) {
        const char* error = kv_get_last_error();
        const std::string detail = std::string("resolved map load failed: ") +
            (error && *error ? error : "maploader failed");
        if (options.expect_no_map) {
            // Expected degraded state: the scenario preview stays loaded and
            // the resolved target is not a loadable map.
            *out << "map_load=failed\n"
                 << detail << "\n"
                 << "result=PASS\n";
            out->flush();
            return 0;
        }
        return fail(detail);
    }
    if (options.expect_no_map) {
        // --expect-no-map promises the degraded no-map state. An unexpected
        // successfully loaded map contradicts the expectation and must fail
        // instead of reporting a false-positive PASS.
        kv_free(handle);
        return fail("expected no map load, but the resolved map loaded unexpectedly");
    }

    KvMapSnapshot snapshot{};
    if (!kv_get_map_snapshot(handle, KV_MAP_SNAPSHOT_VERSION,
                             &snapshot, sizeof(snapshot))) {
        const char* error = kv_get_last_error();
        kv_free(handle);
        return fail(std::string("typed snapshot unavailable: ") +
                    (error && *error ? error : "maploader failed"));
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

    const uint64_t statement_count = snapshot.statement_count;
    const uint64_t element_count = snapshot.element_count;
    kv_free(handle);
    auto finished_at = std::chrono::steady_clock::now();

    const double load_seconds = std::chrono::duration<double>(loaded_at - started_at).count();
    const double snapshot_seconds = std::chrono::duration<double>(snapshot_at - loaded_at).count();
    const double total_seconds = std::chrono::duration<double>(finished_at - started_at).count();
    *out << "load=" << std::fixed << std::setprecision(3) << load_seconds << "s"
         << " snapshot=" << snapshot_seconds << "s"
         << " total=" << total_seconds << "s"
         << " statements=" << statement_count
         << " elements=" << element_count
         << " othertracks=" << other_count << "\n";
    print_headless_buffer_summary(*out, "own", own);
    print_headless_buffer_summary(*out, "curve", curve);
    print_headless_buffer_summary(*out, "structures", structures);
    print_headless_buffer_summary(*out, "other", other_total);
    flush_logs();
    *out << "result=PASS\n";
    out->flush();
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

        const std::string cache_source_path = "C:\\fixtures\\cache-source.map";
        auto add_cache_fixture_row = [&](std::vector<TableRow>& rows,
                                         const char* edit_id,
                                         int line,
                                         std::initializer_list<std::pair<const char*, const char*>> cells) {
            TableRow row = make_row(cells);
            row.edit_id = edit_id;
            row.source.file_path = cache_source_path;
            row.source.line = line;
            rows.push_back(std::move(row));
        };
        add_cache_fixture_row(app.model_.backgrounds, "background-cache-row", 10, {
            {"distance", "10"}, {"structureKey", "bg"},
            {"filePath", cache_source_path.c_str()},
        });
        add_cache_fixture_row(app.model_.adhesions, "adhesion-cache-row", 20, {
            {"distance", "20"}, {"a", "0.35"}, {"b", "0.1"}, {"c", "0.2"},
            {"filePath", cache_source_path.c_str()},
        });
        add_cache_fixture_row(app.model_.cab_illuminance, "cab-cache-row", 30, {
            {"distance", "30"}, {"value", "0.75"},
            {"filePath", cache_source_path.c_str()},
        });
        add_cache_fixture_row(app.model_.fogs, "fog-cache-row", 40, {
            {"distance", "40"}, {"density", "0.005"},
            {"red", "0.1"}, {"green", "0.2"}, {"blue", "0.3"},
            {"filePath", cache_source_path.c_str()},
        });
        add_cache_fixture_row(app.model_.legacy_fogs, "legacy-fog-cache-row", 50, {
            {"distance", "50"}, {"start", "100"}, {"end", "500"},
            {"red", "128"}, {"green", "129"}, {"blue", "130"},
            {"filePath", cache_source_path.c_str()},
        });
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

        app.ensure_table_cache();
        auto cached_row_matches = [&](const std::vector<CachedTableRow>& rows,
                                      const std::vector<std::string>& expected_cells,
                                      const char* edit_id,
                                      int line) {
            return rows.size() == 1 && rows[0].cells == expected_cells &&
                rows[0].open_path == cache_source_path &&
                rows[0].edit_id == edit_id &&
                rows[0].source.file_path == cache_source_path &&
                rows[0].source.line == line;
        };
        check(cached_row_matches(
                  app.table_cache_.background_rows,
                  {"1", "10", "bg", "cache-source.map"},
                  "background-cache-row", 10),
              "caches_background_change_point_row");
        check(cached_row_matches(
                  app.table_cache_.adhesion_rows,
                  {"1", "20", "0.35", "0.1", "0.2", "cache-source.map"},
                  "adhesion-cache-row", 20),
              "caches_adhesion_change_point_row");
        check(cached_row_matches(
                  app.table_cache_.cab_illuminance_rows,
                  {"1", "30", "0.75", "cache-source.map"},
                  "cab-cache-row", 30),
              "caches_cab_illuminance_change_point_row");
        check(cached_row_matches(
                  app.table_cache_.fog_rows,
                  {"1", "40", "0.005", "0.1", "0.2", "0.3", "cache-source.map"},
                  "fog-cache-row", 40),
              "caches_fog_change_point_row");
        check(cached_row_matches(
                  app.table_cache_.legacy_fog_rows,
                  {"1", "50", "100", "500", "128", "129", "130", "cache-source.map"},
                  "legacy-fog-cache-row", 50),
              "caches_legacy_fog_change_point_row");
        app.legacy_fog_list_scroll_row_ = 4;
        app.legacy_fog_list_highlight_row_ = 5;
        app.invalidate_table_cache();
        check(app.legacy_fog_list_scroll_row_ == -1 &&
                  app.legacy_fog_list_highlight_row_ == -1,
              "invalidates_legacy_fog_navigation_state");
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

int run_debug_headless_scene_loader_contract(
    const HeadlessSceneLoaderContractOptions& options) {
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

    *out << "komapedit debug-headless-scene-loader-contract\n"
         << "stage=fixture-start\n";
    struct TempDirectory {
        std::filesystem::path path;
        ~TempDirectory() {
            std::error_code error;
            std::filesystem::remove_all(path, error);
        }
    } temp;
    temp.path = std::filesystem::temp_directory_path() /
        ("komapedit-scene-loader-contract-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    std::error_code directory_error;
    std::filesystem::create_directories(temp.path, directory_error);
    if (directory_error) {
        *out << "error=failed to create scene loader contract fixture: "
             << directory_error.message() << "\nresult=FAIL\n";
        return 2;
    }
    const std::filesystem::path model_path = temp.path / "triangle.x";
    {
        std::ofstream model(model_path, std::ios::binary | std::ios::trunc);
        model << "xof 0303txt 0032\n"
                 "Mesh {\n"
                 "3;\n"
                 "0.0;0.0;0.0;,\n"
                 "1.0;0.0;0.0;,\n"
                 "0.0;1.0;0.0;;\n"
                 "1;\n"
                 "3;0,1,2;;\n"
                 "}\n";
        if (!model) {
            *out << "error=failed to write scene loader contract fixture\nresult=FAIL\n";
            return 2;
        }
    }
    const std::filesystem::path image_path = temp.path / "pixel.bmp";
    constexpr std::array<unsigned char, 58> bitmap_bytes = {
        0x42, 0x4d, 0x3a, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x36, 0x00, 0x00, 0x00, 0x28, 0x00,
        0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00,
        0x00, 0x00, 0x01, 0x00, 0x20, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x33, 0x22,
        0x11, 0xff,
    };
    {
        std::ofstream image(image_path, std::ios::binary | std::ios::trunc);
        image.write(reinterpret_cast<const char*>(bitmap_bytes.data()),
                    static_cast<std::streamsize>(bitmap_bytes.size()));
        if (!image) {
            *out << "error=failed to write image decoder contract fixture\nresult=FAIL\n";
            return 2;
        }
    }
    *out << "stage=fixture-complete\n"
         << "stage=worker-contract-start\n";
    out->flush();

    ScopedComApartment com_apartment;
    if (!com_apartment.ready()) {
        *out << "error=COM initialization failed for image decoder contract\n"
             << "result=FAIL\n";
        return 2;
    }
    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* context = nullptr;
    const char* driver = nullptr;
    if (!create_headless_d3d_device(device, context, driver)) {
        *out << "error=failed to create Direct3D device for scene loader contract\n"
             << "result=FAIL\n";
        return 2;
    }
    Canvas3DSceneLoaderContractResult contract;
    {
        Canvas3D canvas(device);
        contract = canvas.debug_run_scene_loader_contract(
            model_path.u8string(), image_path.u8string());
    }
    release_com(context);
    release_com(device);
    const HeadlessResourceSafetyContractResult resource_safety =
        run_debug_resource_safety_contract(
            image_path.u8string(), (temp.path / "missing.bmp").u8string());
    const bool passed = contract.error.empty() && contract.normal_worker &&
        contract.copy_exception && contract.put_between_exception &&
        contract.subset_requeue && contract.removal_only_cancel &&
        contract.release_balance && contract.texture_allocation_cleanup &&
        contract.texture_cache_reuse && contract.upload_failure_cleanup &&
        resource_safety.image_layout &&
        resource_safety.image_decode && resource_safety.numeric_conversion;
    *out << "stage=worker-contract-complete\n"
         << "d3d_driver=" << driver << "\n"
         << "normal_worker=" << (contract.normal_worker ? "PASS" : "FAIL") << "\n"
         << "copy_exception=" << (contract.copy_exception ? "PASS" : "FAIL") << "\n"
         << "put_between_exception="
         << (contract.put_between_exception ? "PASS" : "FAIL") << "\n"
         << "subset_requeue=" << (contract.subset_requeue ? "PASS" : "FAIL") << "\n"
         << "removal_only_cancel="
         << (contract.removal_only_cancel ? "PASS" : "FAIL") << "\n"
         << "release_balance=" << (contract.release_balance ? "PASS" : "FAIL")
         << " successful_loads=" << contract.successful_load_count
         << " frees=" << contract.free_count << "\n"
         << "texture_allocation_cleanup="
         << (contract.texture_allocation_cleanup ? "PASS" : "FAIL") << "\n"
         << "texture_cache_reuse="
         << (contract.texture_cache_reuse ? "PASS" : "FAIL") << "\n"
         << "upload_failure_cleanup="
         << (contract.upload_failure_cleanup ? "PASS" : "FAIL") << "\n"
         << "image_layout=" << (resource_safety.image_layout ? "PASS" : "FAIL") << "\n"
         << "image_decode=" << (resource_safety.image_decode ? "PASS" : "FAIL") << "\n"
         << "numeric_conversion="
         << (resource_safety.numeric_conversion ? "PASS" : "FAIL") << "\n";
    if (!contract.error.empty()) *out << "error=" << contract.error << "\n";
    *out << "result=" << (passed ? "PASS" : "FAIL") << "\n";
    out->flush();
    return passed ? 0 : 3;
}

int run_debug_headless_settings_persistence(
    const HeadlessSettingsPersistenceOptions& options) {
    std::ofstream output_file;
    std::ostream* out = &std::cout;
    if (!options.output_path.empty()) {
        output_file.open(
            std::filesystem::path(utf8_to_wide(options.output_path)),
            std::ios::out | std::ios::trunc);
        if (!output_file) {
            std::cerr << "failed to open headless output: " << options.output_path << "\n";
            return 1;
        }
        output_file.write("\xEF\xBB\xBF", 3);
        out = &output_file;
    }

    *out << "komapedit debug-headless-settings-persistence\n";
    int exit_code = 0;
    auto check = [&](bool condition, const char* label) {
        *out << label << "=" << (condition ? "PASS" : "FAIL") << "\n";
        if (!condition) exit_code = 2;
    };
    auto color_equal = [](const ImVec4& lhs, const ImVec4& rhs) {
        return std::abs(lhs.x - rhs.x) < 0.0001f &&
            std::abs(lhs.y - rhs.y) < 0.0001f &&
            std::abs(lhs.z - rhs.z) < 0.0001f &&
            std::abs(lhs.w - rhs.w) < 0.0001f;
    };
    auto write_text = [](const std::filesystem::path& path, const std::string& text) {
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        file.write(text.data(), static_cast<std::streamsize>(text.size()));
        return static_cast<bool>(file);
    };
    auto read_text = [](const std::filesystem::path& path) {
        std::ifstream file(path, std::ios::binary);
        std::ostringstream buffer;
        buffer << file.rdbuf();
        return buffer.str();
    };

    const auto unique = std::to_string(GetCurrentProcessId()) + "-" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    const std::filesystem::path temp_root =
        std::filesystem::temp_directory_path() /
        std::filesystem::path("komapedit-settings-contract-" + unique);
    struct TempDirectoryCleanup {
        std::filesystem::path path;
        ~TempDirectoryCleanup() {
            std::error_code ec;
            std::filesystem::remove_all(path, ec);
        }
    } cleanup{temp_root};

    try {
        std::filesystem::create_directories(temp_root);

        *out << "stage=canonical_roundtrip\n";
        const std::filesystem::path canonical_path = temp_root / L"settings.ini";
        UserSettings canonical;
        canonical.path = canonical_path;
        canonical.language = Language::En;
        canonical.font_size = 22.0f;
        canonical.ui_component_size = 125.0f;
        canonical.marker_size_percent = 150.0f;
        canonical.canvas_line_widths = {3.0f, 2.5f, 1.5f, 2.0f};
        canonical.theme_color = ImVec4(
            0x12 / 255.0f, 0x34 / 255.0f, 0x56 / 255.0f, 1.0f);
        canonical.edit_mode_enabled = true;
        canonical.edit_mode_warning_suppressed = true;
        canonical.window_visibility.show_station_list_window = true;
        canonical.window_visibility.show_lighting_window = true;
        canonical.window_visibility.show_console_window = false;
        canonical.view_2d.show_stations = false;
        canonical.view_2d.show_speedlimits = true;
        canonical.view_2d.show_curve_gauge_markers = true;
        canonical.view_2d.show_curve_center_markers = true;
        canonical.view_2d.show_curve_function_markers = true;
        canonical.view_2d.mode = 1;
        canonical.view_2d.grid_mode = 1;
        canonical.view_3d.show_scene_owntrack_markers = true;
        canonical.view_3d.scene_fog_enabled = false;
        canonical.view_3d.scene_map_draw_distance_enabled = false;
        canonical.view_3d.scene_auto_load_on_map_open = true;
        canonical.view_3d.scene_draw_distance_m = 2500;
        canonical.view_3d.scene_edit_component_size_percent = 180;
        canonical.view_3d.scene_camera_speed_percent = 250;
        canonical.view_3d.scene_performance_warning_enabled = false;
        canonical.view_3d.scene_instance_warning_threshold = 3600;
        canonical.view_3d.scene_instance_critical_warning_threshold = 6200;
        check(save_user_settings(canonical), "canonical_save");
        const std::string canonical_text = read_text(canonical_path);
        check(std::count(canonical_text.begin(), canonical_text.end(), '=') == 85,
              "canonical_key_count_85");
        check(canonical_text.find("show_curve_gauge_markers=true\n") !=
                  std::string::npos &&
              canonical_text.find("show_curve_center_markers=true\n") !=
                  std::string::npos &&
              canonical_text.find("show_curve_function_markers=true\n") !=
                  std::string::npos,
              "canonical_curve_parameter_marker_keys");
        UserSettings canonical_loaded = load_user_settings(canonical_path);
        check(canonical_loaded.language == canonical.language, "canonical_language");
        check(canonical_loaded.font_size == canonical.font_size, "canonical_font_size");
        check(canonical_loaded.ui_component_size == canonical.ui_component_size,
              "canonical_component_size");
        check(canonical_loaded.marker_size_percent == canonical.marker_size_percent,
              "canonical_marker_size");
        check(canonical_loaded.canvas_line_widths.own_track_px ==
                  canonical.canvas_line_widths.own_track_px &&
              canonical_loaded.canvas_line_widths.other_track_px ==
                  canonical.canvas_line_widths.other_track_px &&
              canonical_loaded.canvas_line_widths.chart_marker_px ==
                  canonical.canvas_line_widths.chart_marker_px &&
              canonical_loaded.canvas_line_widths.background_grid_px ==
                  canonical.canvas_line_widths.background_grid_px,
              "canonical_line_widths");
        check(color_equal(canonical_loaded.theme_color, canonical.theme_color),
              "canonical_theme_color");
        check(canonical_loaded.edit_mode_enabled == canonical.edit_mode_enabled &&
                  canonical_loaded.edit_mode_warning_suppressed ==
                      canonical.edit_mode_warning_suppressed,
              "canonical_edit_flags");
        check(canonical_loaded.window_visibility == canonical.window_visibility,
              "canonical_window_visibility");
        check(canonical_loaded.view_2d == canonical.view_2d, "canonical_view_2d");
        check(canonical_loaded.view_3d == canonical.view_3d, "canonical_view_3d");

        *out << "stage=legacy_alias_rejection\n";
        const std::filesystem::path alias_path = temp_root / L"settings_alias.ini";
        const std::string alias_text =
            "[General]\n"
            "lang=en\n"
            "font_size=24.0\n"
            "enable_edit=true\n"
            "[WindowVisibility]\n"
            "show_other_train_window=true\n"
            "[View2D]\n"
            "show_station_pos=false\n"
            "show_curve_gauge_marker=true\n"
            "show_cant_center_markers=true\n"
            "show_transition_function_markers=true\n"
            "view_2d_mode=measure\n"
            "[View3D]\n"
            "scene_draw_distance=2400\n";
        check(write_text(alias_path, alias_text), "legacy_alias_fixture_write");
        UserSettings alias_loaded = load_user_settings(alias_path);
        UserSettings defaults;
        check(alias_loaded.language == defaults.language, "legacy_language_alias_rejected");
        check(alias_loaded.font_size == 24.0f, "canonical_key_amid_aliases");
        check(alias_loaded.edit_mode_enabled == defaults.edit_mode_enabled,
              "legacy_edit_alias_rejected");
        check(alias_loaded.window_visibility.show_other_trains_window ==
                  defaults.window_visibility.show_other_trains_window,
              "legacy_window_alias_rejected");
        check(alias_loaded.view_2d.show_stations == defaults.view_2d.show_stations &&
                  alias_loaded.view_2d.mode == defaults.view_2d.mode &&
                  alias_loaded.view_2d.show_curve_gauge_markers ==
                      defaults.view_2d.show_curve_gauge_markers &&
                  alias_loaded.view_2d.show_curve_center_markers ==
                      defaults.view_2d.show_curve_center_markers &&
                  alias_loaded.view_2d.show_curve_function_markers ==
                      defaults.view_2d.show_curve_function_markers,
              "legacy_view_2d_aliases_rejected");
        check(alias_loaded.view_3d.scene_draw_distance_m ==
                  defaults.view_3d.scene_draw_distance_m,
              "legacy_view_3d_alias_rejected");
        check(read_text(alias_path) == alias_text, "legacy_alias_load_does_not_rewrite");
        check(save_user_settings(alias_loaded), "explicit_save_after_legacy_load");
        const std::string rewritten_alias_text = read_text(alias_path);
        check(std::count(
                  rewritten_alias_text.begin(), rewritten_alias_text.end(), '=') == 85 &&
                  rewritten_alias_text.find("lang=") == std::string::npos &&
                  rewritten_alias_text.find("enable_edit=") == std::string::npos,
              "explicit_save_writes_canonical_schema");

        *out << "stage=strict_section_and_value_rejection\n";
        const std::filesystem::path invalid_path = temp_root / L"settings_invalid.ini";
        const std::string invalid_text =
            "[General]\n"
            "language=EN\n"
            "font_size=22junk\n"
            "ui_component_size=nan\n"
            "marker_size_percent=inf\n"
            "own_track_line_width_px=0x4\n"
            "theme_color=#123456\n"
            "edit_mode_enabled=yes\n"
            "show_console_window=false\n"
            "[Malformed\n"
            "font_size=30.0\n"
            "[WindowVisibility]\n"
            "show_console_window=FALSE\n"
            "font_size=24.0\n"
            "[View2D]\n"
            "show_stations=1\n"
            "mode=1\n"
            "grid_mode=moveable\n"
            "[View3D]\n"
            "scene_fog_enabled=on\n"
            "scene_draw_distance_m=1500junk\n"
            "scene_camera_speed_percent=200x\n";
        check(write_text(invalid_path, invalid_text), "strict_fixture_write");
        UserSettings invalid_loaded = load_user_settings(invalid_path);
        check(invalid_loaded.language == defaults.language &&
                  invalid_loaded.font_size == defaults.font_size &&
                  invalid_loaded.ui_component_size == defaults.ui_component_size &&
                  invalid_loaded.marker_size_percent == defaults.marker_size_percent &&
                  invalid_loaded.canvas_line_widths.own_track_px ==
                      defaults.canvas_line_widths.own_track_px,
              "loose_general_values_rejected");
        check(color_equal(invalid_loaded.theme_color, defaults.theme_color) &&
                  invalid_loaded.edit_mode_enabled == defaults.edit_mode_enabled,
              "loose_color_and_bool_rejected");
        check(invalid_loaded.window_visibility.show_console_window ==
                  defaults.window_visibility.show_console_window,
              "wrong_section_and_case_rejected");
        check(invalid_loaded.view_2d == defaults.view_2d,
              "loose_view_2d_values_rejected");
        check(invalid_loaded.view_3d == defaults.view_3d,
              "loose_view_3d_values_rejected");

        *out << "stage=missing_keys_no_rewrite\n";
        const std::filesystem::path partial_path = temp_root / L"settings_partial.ini";
        const std::string partial_text = "[General]\nlanguage=ja\n";
        check(write_text(partial_path, partial_text), "partial_fixture_write");
        UserSettings partial_loaded = load_user_settings(partial_path);
        check(partial_loaded.language == Language::Ja &&
                  partial_loaded.font_size == defaults.font_size,
              "partial_file_uses_defaults_for_missing_keys");
        check(read_text(partial_path) == partial_text, "partial_file_not_rewritten");

        *out << "stage=history_canonical_and_legacy_boundary\n";
        const std::filesystem::path history_path = temp_root / L"history.ini";
        RecentMapEntry first_history;
        first_history.path = narrow_path(temp_root / L"route-one.txt");
        first_history.background.has_image = true;
        first_history.background.image_path = narrow_path(temp_root / L"background.png");
        first_history.background.x = 12.5;
        first_history.background.y = -3.25;
        first_history.background.width = 800.0;
        first_history.background.height = 600.0;
        first_history.background.rotation_deg = 4.5;
        first_history.background.brightness = 75.0;
        RecentMapEntry second_history;
        second_history.path = narrow_path(temp_root / L"route-two.txt");
        check(save_history_entries(history_path, {first_history, second_history}),
              "canonical_history_save");
        std::vector<RecentMapEntry> canonical_history =
            load_history_entries(history_path);
        check(canonical_history.size() == 2, "canonical_history_count");
        if (canonical_history.size() == 2) {
            check(normalized_path_key(canonical_history[0].path) ==
                      normalized_path_key(first_history.path) &&
                      normalized_path_key(canonical_history[1].path) ==
                      normalized_path_key(second_history.path),
                  "canonical_history_paths");
            check(canonical_history[0].background.has_image &&
                      canonical_history[0].background.x == 12.5 &&
                      canonical_history[0].background.y == -3.25 &&
                      canonical_history[0].background.width == 800.0 &&
                      canonical_history[0].background.height == 600.0 &&
                      canonical_history[0].background.rotation_deg == 4.5 &&
                      canonical_history[0].background.brightness == 75.0,
                  "canonical_history_background");
        }

        const std::filesystem::path legacy_history_path =
            temp_root / L"history_legacy.ini";
        const std::string current_path = narrow_path(temp_root / L"current-route.txt");
        const std::string legacy_history_text =
            "[Recent]\n"
            "count=2\n"
            "[Recent_Map0]\n"
            "path=" + narrow_path(temp_root / L"old-one.txt") + "\n"
            "[Recent0]\n"
            "path=" + narrow_path(temp_root / L"old-two.txt") + "\n"
            "[Map0junk]\n"
            "path=" + narrow_path(temp_root / L"old-three.txt") + "\n"
            "[Map00]\n"
            "path=" + narrow_path(temp_root / L"old-leading-zero.txt") + "\n"
            "[Map0]\n"
            "map_path=" + narrow_path(temp_root / L"old-four.txt") + "\n"
            "[Map1]\n"
            "path=" + current_path + "\n"
            "background_path=" + narrow_path(temp_root / L"old-bg.png") + "\n"
            "bg_x=12junk\n"
            "bg_brightness=80garbage\n"
            "[MapBroken\n"
            "path=" + narrow_path(temp_root / L"old-after-malformed.txt") + "\n";
        check(write_text(legacy_history_path, legacy_history_text),
              "legacy_history_fixture_write");
        std::vector<RecentMapEntry> legacy_history =
            load_history_entries(legacy_history_path);
        check(legacy_history.size() == 1 &&
                  normalized_path_key(legacy_history[0].path) ==
                      normalized_path_key(current_path),
              "legacy_history_sections_and_path_aliases_rejected");
        if (legacy_history.size() == 1) {
            check(!legacy_history[0].background.has_image &&
                      legacy_history[0].background.x == 0.0 &&
                      legacy_history[0].background.brightness == 100.0,
                  "legacy_history_fields_and_trailing_numbers_rejected");
        }

        const std::filesystem::path bad_count_path =
            temp_root / L"history_bad_count.ini";
        const std::string bad_count_text =
            "[Recent]\ncount=1junk\n[Map0]\npath=" + current_path + "\n";
        check(write_text(bad_count_path, bad_count_text), "bad_count_fixture_write");
        check(load_history_entries(bad_count_path).empty(),
              "history_count_trailing_text_rejected");

        const std::filesystem::path leading_zero_count_path =
            temp_root / L"history_leading_zero_count.ini";
        const std::string leading_zero_count_text =
            "[Recent]\ncount=01\n[Map0]\npath=" + current_path + "\n";
        check(write_text(leading_zero_count_path, leading_zero_count_text),
              "leading_zero_count_fixture_write");
        check(load_history_entries(leading_zero_count_path).empty(),
              "history_count_leading_zero_rejected");
    } catch (const std::exception& e) {
        *out << "exception=\"" << e.what() << "\"\n";
        exit_code = 3;
    }

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
    count_missing_rows(model.legacy_fogs);

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
    for (size_t index = 0; index < model.edit_files.size(); ++index) {
        *out << "source_file_" << index << "="
             << model.edit_files[index].file_path << "\n";
    }
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
    std::vector<std::string> previews;
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
        output.previews.push_back(text(row.after_text));
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

int App::run_debug_headless_other_track_edit(
    const HeadlessOtherTrackEditOptions& options) {
    using typed_edit_headless::Change;
    using typed_edit_headless::Field;
    using typed_edit_headless::Report;

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

    *out << "command=debug-headless-other-track-edit\n"
         << "map_path=\"" << options.path << "\"\n"
         << "commit_requested=" << (options.commit ? 1 : 0) << "\n";
    LoadResult load = load_map_worker(options.path, options.unit_distance,
                                      false, 0.0, 0.0, options.unit_distance,
                                      LoadModelOptions{true});
    if (!load.ok) {
        *out << "load_error=" << load.error << "\nresult=FAIL\n";
        if (load.handle) kv_free(load.handle);
        return 2;
    }

    int failed_cases = 0;
    auto check = [&](const char* label, bool value) {
        *out << label << '=' << (value ? 1 : 0) << "\n";
        if (!value) ++failed_cases;
    };
    auto source_hash = [&](const TableRow& row) {
        const auto found = std::find_if(
            load.model.edit_files.begin(), load.model.edit_files.end(),
            [&](const EditSourceFileInfo& file) {
                return file.file_path == row.source.file_path;
            });
        return found == load.model.edit_files.end() ? std::string{} : found->source_hash;
    };
    auto find_row = [](const MapModel& model,
                       const std::string& edit_id) -> const TableRow* {
        const auto found = std::find_if(
            model.other_track_changes.begin(), model.other_track_changes.end(),
            [&](const TableRow& row) { return row.edit_id == edit_id; });
        return found == model.other_track_changes.end() ? nullptr : &*found;
    };
    auto find_track = [](const MapModel& model,
                         const std::string& key) -> const OtherTrack* {
        const std::string normalized = normalize_track_lookup_key(key);
        const auto found = std::find_if(
            model.other_tracks.begin(), model.other_tracks.end(),
            [&](const OtherTrack& track) {
                return normalize_track_lookup_key(track.key) == normalized;
            });
        return found == model.other_tracks.end() ? nullptr : &*found;
    };
    auto geometry_fingerprint = [&](const MapModel& model, const std::string& key) {
        const OtherTrack* track = find_track(model, key);
        double fingerprint = 0.0;
        if (!track) return fingerprint;
        for (size_t row = 0; row < track->points.rows; ++row) {
            for (size_t column = 0; column < track->points.cols; ++column) {
                fingerprint += track->points.at(row, column) *
                    static_cast<double>((row + 1) * (column + 3));
            }
        }
        return fingerprint;
    };
    auto report_errors = [&](const Report& report) {
        for (const std::string& error : report.blocking_errors) {
            *out << "blocking_error=" << error << "\n";
        }
    };

    KvMapSnapshot snapshot{};
    const bool snapshot_ok = kv_get_map_snapshot(
        load.handle, KV_MAP_SNAPSHOT_VERSION, &snapshot, sizeof(snapshot)) != 0;
    check("snapshot_ok", snapshot_ok);
    check("logical_rows_present", !load.model.other_track_changes.empty());
    check("typed_row_count_match", snapshot_ok &&
          snapshot.other_track_change_count == load.model.other_track_changes.size());

    const auto target_it = std::find_if(
        load.model.other_track_changes.begin(), load.model.other_track_changes.end(),
        [](const TableRow& row) {
            return !row.edit_id.empty() &&
                ascii_lower(table_cell(row, "method")) == "track.position" &&
                table_cell_number(row, "parameterCount") >= 1.0;
        });
    const TableRow* target = target_it == load.model.other_track_changes.end()
        ? nullptr : &*target_it;
    check("position_target_found", target != nullptr);
    if (!target) {
        *out << "result=FAIL\n";
        kv_free(load.handle);
        return 3;
    }

    size_t bound_event_count = 0;
    if (snapshot_ok) {
        const size_t row_index = static_cast<size_t>(target_it -
            load.model.other_track_changes.begin());
        const KvOtherTrackChangeRow& typed = snapshot.other_track_changes[row_index];
        for (std::uint64_t i = 0; i < snapshot.other_track_event_count; ++i) {
            const KvTrackEventRow& event = snapshot.other_track_events[i];
            if (event.metadata.source_file_index == typed.metadata.source_file_index &&
                event.metadata.line == typed.metadata.line &&
                event.metadata.column == typed.metadata.column) {
                ++bound_event_count;
            }
        }
    }
    *out << "derived_event_binding_count=" << bound_event_count << "\n";
    check("derived_events_bound", bound_event_count >= 2);

    const std::string edit_id = target->edit_id;
    const std::string track_key = table_cell(*target, "trackKey");
    const std::string expected_hash = source_hash(*target);
    const std::string original_text = table_cell(*target, "parameter0");
    const double original_value = table_cell_number(*target, "parameter0");
    const double updated_value = original_value + 0.001;
    const std::string updated_text = format_double(updated_value, 9);
    const std::string source_path = target->source.file_path;
    const double baseline_geometry = geometry_fingerprint(load.model, track_key);
    const Change update = typed_edit_headless::update(
        "other-track-position-update", edit_id, expected_hash,
        std::vector<Field>{{"parameter0", updated_text}});

    Report apply_report = typed_edit_headless::apply_to_memory(load.handle, {update});
    report_errors(apply_report);
    check("update_apply_ok", apply_report.ok && apply_report.full_reparse_ok);
    check("update_non_target_clean", apply_report.non_target_changed_count == 0);
    MapModel applied = apply_report.ok
        ? build_model_from_handle(load.handle, options.path, LoadModelOptions{true})
        : MapModel{};
    const TableRow* applied_row = find_row(applied, edit_id);
    check("stable_edit_id", applied_row != nullptr);
    check("updated_parameter_match", applied_row &&
          std::abs(table_cell_number(*applied_row, "parameter0") - updated_value) < 1e-9);
    check("derived_geometry_changed", applied_row &&
          std::abs(geometry_fingerprint(applied, track_key) - baseline_geometry) > 1e-8);

    if (applied_row) {
        const std::string normalized_key = normalize_track_lookup_key(track_key);
        auto applied_track = std::find_if(
            applied.other_tracks.begin(), applied.other_tracks.end(),
            [&](const OtherTrack& track) {
                return normalize_track_lookup_key(track.key) == normalized_key;
            });
        if (applied_track != applied.other_tracks.end()) {
            applied_track->visible = true;
            applied_track->color = ImVec4(0.17f, 0.63f, 0.91f, 1.0f);
        }

        bool marker_2d_ok = false;
        bool marker_2d_label_ok = false;
        ImGui::CreateContext();
        ImPlot::CreateContext();
        ImGui::GetIO().IniFilename = nullptr;
        try {
            UserSettings settings;
            settings.language = Language::En;
            App app(nullptr, settings, 1.0f, false, false);
            app.model_ = applied;
            app.has_model_ = true;
            app.edit_mode_enabled_ = true;
            app.edit_registry_loaded_ = true;
            app.dmin_ = applied.default_min;
            app.dmax_ = applied.default_max;
            app.rebuild_marker_overlay_cache();
            const auto marker = std::find_if(
                app.other_track_change_marker_cache_.begin(),
                app.other_track_change_marker_cache_.end(),
                [&](const OtherTrackChangeMarker& candidate) {
                    return candidate.edit_id == edit_id;
                });
            marker_2d_ok = marker != app.other_track_change_marker_cache_.end() &&
                marker->track_index < app.model_.other_tracks.size() &&
                app.model_.other_tracks[marker->track_index].visible &&
                marker->d >= app.model_.other_tracks[marker->track_index].range_min &&
                marker->d <= app.model_.other_tracks[marker->track_index].range_max;
            const std::string method = table_cell(*applied_row, "method");
            const std::string method_suffix =
                method.compare(0, 6, "Track.") == 0 ? method.substr(6) : method;
            marker_2d_label_ok = marker_2d_ok &&
                marker->label.find("Track[" + track_key + "].") != std::string::npos &&
                marker->label.find(method_suffix) != std::string::npos &&
                marker->label.find(updated_text) != std::string::npos;
        } catch (...) {
            marker_2d_ok = false;
        }
        ImPlot::DestroyContext();
        ImGui::DestroyContext();
        check("marker_2d_coordinate_and_filter_ok", marker_2d_ok);
        check("marker_2d_label_ok", marker_2d_label_ok);

        Canvas3DSceneBuildOptions scene_options;
        scene_options.model = &applied;
        scene_options.map_handle = load.handle;
        scene_options.unit_distance = options.unit_distance;
        scene_options.control_point_interval = options.unit_distance;
        Canvas3DSceneBuildResult scene = build_canvas3d_scene_preview(scene_options);
        const auto marker_3d = std::find_if(
            scene.scene.markers.begin(), scene.scene.markers.end(),
            [&](const Canvas3DSceneMarker& marker) {
                return marker.row_kind == "otherTrack.change" &&
                    marker.edit_id == edit_id;
            });
        const bool marker_3d_ok = marker_3d != scene.scene.markers.end() &&
            marker_3d->has_theme_color && marker_3d->track_key ==
                (applied_track == applied.other_tracks.end()
                    ? std::string{} : applied_track->key) &&
            std::abs(marker_3d->track_point.distance -
                     table_cell_number(*applied_row, "distance")) < 1e-8;
        const bool marker_3d_color_ok = marker_3d_ok &&
            std::abs(marker_3d->theme_color.x - 0.17f) < 1e-6f &&
            std::abs(marker_3d->theme_color.y - 0.63f) < 1e-6f &&
            std::abs(marker_3d->theme_color.z - 0.91f) < 1e-6f;
        const bool marker_3d_label_ok = marker_3d_ok &&
            marker_3d->label.find(track_key) != std::string::npos &&
            marker_3d->label.find(updated_text) != std::string::npos;
        check("marker_3d_coordinate_ok", marker_3d_ok);
        check("marker_3d_color_ok", marker_3d_color_ok);
        check("marker_3d_label_ok", marker_3d_label_ok);
    }

    check("update_reset_ok", kv_edit_reset_memory(load.handle) != 0);
    Change deletion;
    deletion.change_id = "other-track-delete";
    deletion.edit_id = edit_id;
    deletion.operation = KV_EDIT_DELETE;
    deletion.expected_source_hash = expected_hash;
    const Report delete_report = typed_edit_headless::apply_to_memory(
        load.handle, {deletion});
    report_errors(delete_report);
    const MapModel deleted = delete_report.ok
        ? build_model_from_handle(load.handle, options.path, LoadModelOptions{true})
        : MapModel{};
    check("delete_apply_ok", delete_report.ok && delete_report.full_reparse_ok);
    check("delete_row_removed", delete_report.ok && find_row(deleted, edit_id) == nullptr);
    check("delete_reset_ok", kv_edit_reset_memory(load.handle) != 0);

    if (options.commit) {
        const Report commit_apply = typed_edit_headless::apply_to_memory(load.handle, {update});
        check("commit_apply_ok", commit_apply.ok && commit_apply.full_reparse_ok);
        const Report commit_report = commit_apply.ok
            ? typed_edit_headless::commit(load.handle) : Report{};
        report_errors(commit_report);
        check("commit_ok", commit_report.ok);
        LoadResult reloaded = commit_report.ok
            ? load_map_worker(options.path, options.unit_distance, false, 0.0, 0.0,
                              options.unit_distance, LoadModelOptions{true})
            : LoadResult{};
        const TableRow* reloaded_row = reloaded.ok
            ? find_row(reloaded.model, edit_id) : nullptr;
        check("commit_reload_ok", reloaded.ok && reloaded_row &&
              std::abs(table_cell_number(*reloaded_row, "parameter0") -
                       updated_value) < 1e-9);
        *out << "committed_file=\"" << source_path << "\"\n"
             << "committed_field=parameter0\n"
             << "committed_original=\"" << original_text << "\"\n"
             << "committed_new=\"" << updated_text << "\"\n";
        if (reloaded.handle) kv_free(reloaded.handle);
    }

    *out << "logical_row_count=" << load.model.other_track_changes.size() << "\n"
         << "result=" << (failed_cases == 0 ? "PASS" : "FAIL") << "\n";
    out->flush();
    kv_free(load.handle);
    return failed_cases == 0 ? 0 : 3;
}

int App::run_debug_headless_own_track_edit(
    const HeadlessOwnTrackEditOptions& options) {
    using typed_edit_headless::Change;
    using typed_edit_headless::Field;
    using typed_edit_headless::Report;

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

    *out << "command=debug-headless-own-track-edit\n"
         << "map_path=\"" << options.path << "\"\n";
    LoadResult load = load_map_worker(options.path, options.unit_distance,
                                      false, 0.0, 0.0, options.unit_distance,
                                      LoadModelOptions{true});
    if (!load.ok) {
        *out << "load_error=" << load.error << "\nresult=FAIL\n";
        if (load.handle) kv_free(load.handle);
        return 2;
    }

    auto source_hash = [&](const TableRow& row) {
        const auto file = std::find_if(
            load.model.edit_files.begin(), load.model.edit_files.end(),
            [&](const EditSourceFileInfo& candidate) {
                return candidate.file_path == row.source.file_path;
            });
        return file == load.model.edit_files.end() ? std::string{} : file->source_hash;
    };
    auto rows_for_kind = [](const MapModel& model,
                            const std::string& kind) -> const std::vector<TableRow>& {
        return kind == "curve" ? model.curve_rows : model.gradient_rows;
    };
    auto find_row = [&](const MapModel& model, const std::string& kind,
                        const std::string& edit_id) -> const TableRow* {
        const std::vector<TableRow>& rows = rows_for_kind(model, kind);
        const auto found = std::find_if(rows.begin(), rows.end(),
                                        [&](const TableRow& row) {
                                            return row.edit_id == edit_id;
                                        });
        return found == rows.end() ? nullptr : &*found;
    };
    auto update_change = [&](const TableRow& row, const std::string& field,
                             const std::string& value) {
        return typed_edit_headless::update(
            "own-track-update-" + row.edit_id, row.edit_id, source_hash(row),
            std::vector<Field>{{field, value}});
    };
    auto delete_change = [&](const TableRow& row) {
        Change change;
        change.change_id = "own-track-delete-" + row.edit_id;
        change.edit_id = row.edit_id;
        change.operation = KV_EDIT_DELETE;
        change.expected_source_hash = source_hash(row);
        return change;
    };
    auto print_report_errors = [&](const Report& report) {
        for (const std::string& error : report.blocking_errors) {
            *out << "blocking_error=" << error << "\n";
        }
    };

    int failed_cases = 0;
    auto apply_update_case = [&](const char* label, const std::string& kind,
                                 const TableRow* target, const char* field,
                                 double delta) {
        if (!target) {
            *out << label << "_target_found=0\n";
            ++failed_cases;
            return;
        }
        const double expected = table_cell_number(*target, field) + delta;
        const std::string expected_text = format_double(expected, 9);
        const Report report = typed_edit_headless::apply_to_memory(
            load.handle, {update_change(*target, field, expected_text)});
        const MapModel applied = report.ok
            ? build_model_from_handle(load.handle, options.path, LoadModelOptions{true})
            : MapModel{};
        const TableRow* applied_row = report.ok
            ? find_row(applied, kind, target->edit_id) : nullptr;
        const bool value_ok = applied_row &&
            std::abs(table_cell_number(*applied_row, field) - expected) < 1e-8;
        const bool ok = report.ok && report.full_reparse_ok && value_ok;
        *out << label << "_target_found=1\n"
             << label << "_apply_ok=" << (report.ok ? 1 : 0) << "\n"
             << label << "_full_reparse_ok=" << (report.full_reparse_ok ? 1 : 0) << "\n"
             << label << "_value_ok=" << (value_ok ? 1 : 0) << "\n";
        print_report_errors(report);
        if (!ok) ++failed_cases;
        if (!kv_edit_reset_memory(load.handle)) {
            *out << label << "_reset_ok=0\n";
            ++failed_cases;
        } else {
            *out << label << "_reset_ok=1\n";
        }
    };

    auto editable_value_row = [](const TableRow& row, const char* transition_method,
                                 const char* field) {
        return !row.edit_id.empty() &&
            ascii_lower(table_cell(row, "method")) != transition_method &&
            table_cell_number(row, "argumentCount") > 0.0 &&
            !table_cell(row, field).empty();
    };
    const auto curve_update_it = std::find_if(
        load.model.curve_rows.begin(), load.model.curve_rows.end(),
        [&](const TableRow& row) {
            return editable_value_row(row, "curve.begintransition", "radius");
        });
    const auto gradient_update_it = std::find_if(
        load.model.gradient_rows.begin(), load.model.gradient_rows.end(),
        [&](const TableRow& row) {
            return editable_value_row(row, "gradient.begintransition", "gradient");
        });
    const TableRow* curve_update = curve_update_it == load.model.curve_rows.end()
        ? nullptr : &*curve_update_it;
    const TableRow* gradient_update = gradient_update_it == load.model.gradient_rows.end()
        ? nullptr : &*gradient_update_it;

    *out << "curve_row_count=" << load.model.curve_rows.size() << "\n"
         << "gradient_row_count=" << load.model.gradient_rows.size() << "\n";
    apply_update_case("curve_update", "curve", curve_update, "radius", 1.0);
    apply_update_case("gradient_update", "gradient", gradient_update, "gradient", 0.001);

    const TableRow* delete_target = nullptr;
    std::string delete_kind;
    auto select_delete_target = [&](const std::vector<TableRow>& rows,
                                    const std::string& kind) {
        const auto standalone = std::find_if(rows.begin(), rows.end(),
                                             [&](const TableRow& row) {
            const std::string method = ascii_lower(table_cell(row, "method"));
            return !row.edit_id.empty() &&
                method != kind + ".begintransition" &&
                table_cell(row, "_transitionEditId").empty();
        });
        if (standalone != rows.end()) {
            delete_target = &*standalone;
            delete_kind = kind;
            return true;
        }
        const auto paired = std::find_if(rows.begin(), rows.end(),
                                         [&](const TableRow& row) {
            const std::string method = ascii_lower(table_cell(row, "method"));
            return !row.edit_id.empty() &&
                method != kind + ".begintransition" &&
                !table_cell(row, "_transitionEditId").empty();
        });
        if (paired == rows.end()) return false;
        delete_target = &*paired;
        delete_kind = kind;
        return true;
    };
    if (!select_delete_target(load.model.curve_rows, "curve")) {
        select_delete_target(load.model.gradient_rows, "gradient");
    }

    if (!delete_target) {
        *out << "delete_target_found=0\n";
        ++failed_cases;
    } else {
        std::vector<Change> deletes{delete_change(*delete_target)};
        std::vector<std::string> deleted_ids{delete_target->edit_id};
        const std::string transition_id = table_cell(*delete_target, "_transitionEditId");
        if (!transition_id.empty()) {
            const TableRow* transition = find_row(load.model, delete_kind, transition_id);
            if (transition) {
                deletes.push_back(delete_change(*transition));
                deleted_ids.push_back(transition_id);
            }
        }
        const Report report = typed_edit_headless::apply_to_memory(load.handle, deletes);
        const MapModel applied = report.ok
            ? build_model_from_handle(load.handle, options.path, LoadModelOptions{true})
            : MapModel{};
        const bool rows_removed = report.ok && std::all_of(
            deleted_ids.begin(), deleted_ids.end(), [&](const std::string& edit_id) {
                return find_row(applied, delete_kind, edit_id) == nullptr;
            });
        const bool ok = report.ok && report.full_reparse_ok && rows_removed;
        *out << "delete_target_found=1\n"
             << "delete_kind=" << delete_kind << "\n"
             << "delete_change_count=" << deletes.size() << "\n"
             << "delete_apply_ok=" << (report.ok ? 1 : 0) << "\n"
             << "delete_full_reparse_ok=" << (report.full_reparse_ok ? 1 : 0) << "\n"
             << "delete_rows_removed=" << (rows_removed ? 1 : 0) << "\n";
        print_report_errors(report);
        if (!ok) ++failed_cases;
        if (!kv_edit_reset_memory(load.handle)) {
            *out << "delete_reset_ok=0\n";
            ++failed_cases;
        } else {
            *out << "delete_reset_ok=1\n";
        }
    }

    if (!curve_update) {
        *out << "gui_station_sync_target_found=0\n";
        ++failed_cases;
    } else {
        const std::string target_edit_id = curve_update->edit_id;
        const std::string target_hash = source_hash(*curve_update);
        const double target_radius = table_cell_number(*curve_update, "radius") + 25.0;
        const std::vector<Station> baseline_stations = load.model.station_positions;
        bool apply_ok = false;
        bool expected_geometry_changed = false;
        bool station_coordinates_match = false;
        std::string exception_text;

        ImGui::CreateContext();
        ImPlot::CreateContext();
        ImGui::GetIO().IniFilename = nullptr;
        try {
            UserSettings settings;
            settings.language = Language::En;
            App app(nullptr, settings, 1.0f, false, false);
            app.handle_ = load.handle;
            load.handle = nullptr;
            app.model_ = std::move(load.model);
            app.file_path_ = options.path;
            app.has_model_ = true;
            app.edit_mode_enabled_ = true;
            app.edit_registry_loaded_ = true;
            app.edit_memory_matches_pending_ledger_ = true;
            app.dmin_ = app.model_.default_min;
            app.dmax_ = app.model_.default_max;

            MapElementPendingChange pending;
            pending.change_id = "own-track-gui-station-sync";
            pending.edit_id = target_edit_id;
            pending.row_kind = "curve";
            pending.operation = "update";
            pending.expected_source_hash = target_hash;
            pending.field_changes["radius"] = format_double(target_radius, 9);
            std::map<std::string, MapElementPendingChange> ledger;
            ledger.emplace(target_edit_id, std::move(pending));
            apply_ok = app.apply_edit_ledger_to_preview(
                ledger, std::nullopt, false);

            if (apply_ok) {
                const MapModel expected = build_model_from_handle(
                    app.handle_, options.path, LoadModelOptions{true});
                const size_t count = std::min(
                    baseline_stations.size(), expected.station_positions.size());
                for (size_t i = 0; i < count; ++i) {
                    const Station& before = baseline_stations[i];
                    const Station& after = expected.station_positions[i];
                    if (std::abs(before.x - after.x) > 1e-7 ||
                        std::abs(before.y - after.y) > 1e-7 ||
                        std::abs(before.z - after.z) > 1e-7) {
                        expected_geometry_changed = true;
                        break;
                    }
                }
                station_coordinates_match =
                    app.model_.station_positions.size() ==
                        expected.station_positions.size();
                for (size_t i = 0;
                     station_coordinates_match &&
                     i < expected.station_positions.size(); ++i) {
                    const Station& actual = app.model_.station_positions[i];
                    const Station& wanted = expected.station_positions[i];
                    station_coordinates_match = actual.key == wanted.key &&
                        std::abs(actual.distance - wanted.distance) < 1e-9 &&
                        std::abs(actual.x - wanted.x) < 1e-9 &&
                        std::abs(actual.y - wanted.y) < 1e-9 &&
                        std::abs(actual.z - wanted.z) < 1e-9;
                }
            }
        } catch (const std::exception& e) {
            exception_text = e.what();
        }
        ImPlot::DestroyContext();
        ImGui::DestroyContext();

        const bool station_sync_ok = apply_ok && expected_geometry_changed &&
            station_coordinates_match && exception_text.empty();
        *out << "gui_station_sync_target_found=1\n"
             << "gui_station_sync_apply_ok=" << (apply_ok ? 1 : 0) << "\n"
             << "gui_station_sync_geometry_changed="
             << (expected_geometry_changed ? 1 : 0) << "\n"
             << "gui_station_sync_coordinates_match="
             << (station_coordinates_match ? 1 : 0) << "\n";
        if (!exception_text.empty()) {
            *out << "gui_station_sync_exception=" << exception_text << "\n";
        }
        if (!station_sync_ok) ++failed_cases;
    }

    *out << "result=" << (failed_cases == 0 ? "PASS" : "FAIL") << "\n";
    out->flush();
    if (load.handle) kv_free(load.handle);
    return failed_cases == 0 ? 0 : 3;
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
    KmeByteHash64 hash;
    hash.bytes(text);
    std::ostringstream out;
    out << std::hex << std::setw(16) << std::setfill('0') << hash.value;
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
    for (const std::string& after : report.previews) {
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
    for (const std::string& after : report.previews) {
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

int run_debug_headless_include_delete(const HeadlessIncludeDeleteOptions& options) {
    using namespace distance_batch_headless;
    using typed_edit_headless::Change;

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

    int failed_cases = 0;
    auto check = [&](const char* label, bool value) {
        *out << label << '=' << (value ? 1 : 0) << "\n";
        if (!value) ++failed_cases;
    };
    auto read_file_bytes = [](const std::filesystem::path& path) {
        std::ifstream file(path, std::ios::binary);
        return std::string((std::istreambuf_iterator<char>(file)),
                           std::istreambuf_iterator<char>());
    };

    *out << "command=debug-headless-include-delete\n"
         << "map_path=\"" << options.path << "\"\n"
         << "index=" << options.index << "\n"
         << "commit_requested=" << (options.commit ? 1 : 0) << "\n";

    try {
        MapHandle handle;
        handle.value = kv_load_map_ex(options.path.c_str(), options.unit_distance,
                                      KV_LOAD_EDIT_METADATA);
        if (!handle.value) {
            const char* error = kv_get_last_error();
            throw std::runtime_error(std::string("map load failed") +
                (error ? ": " + std::string(error) : std::string{}));
        }
        check("load_ok", true);

        auto load_snapshot = [&](void* map_handle) {
            KvMapSnapshot result{};
            if (!kv_get_map_snapshot(map_handle, KV_MAP_SNAPSHOT_VERSION,
                                     &result, sizeof(result)) ||
                result.version != KV_MAP_SNAPSHOT_VERSION ||
                result.structure_size < sizeof(KvMapSnapshot)) {
                const char* error = kv_get_last_error();
                throw std::runtime_error(std::string("map snapshot failed") +
                    (error ? ": " + std::string(error) : std::string{}));
            }
            return result;
        };
        auto snapshot_text = [](const KvMapSnapshot& snap, KvStringRef ref) {
            if (ref.length == 0) return std::string{};
            if (!snap.string_data || ref.offset > snap.string_size ||
                ref.length > snap.string_size - ref.offset) {
                throw std::runtime_error(
                    "map snapshot string reference is out of range");
            }
            return std::string(snap.string_data + static_cast<size_t>(ref.offset),
                               static_cast<size_t>(ref.length));
        };
        struct IncludeTarget {
            std::string edit_id;
            std::string source_hash;
            std::string raw_arguments;
            std::string source_file;
            std::uint64_t byte_start = 0;
            std::uint64_t byte_end = 0;
            int global_order = 0;
        };
        auto collect_targets = [&](const KvMapSnapshot& snap) {
            std::vector<IncludeTarget> targets;
            for (std::uint64_t i = 0; i < snap.statement_count; ++i) {
                const KvStatementRow& row = snap.statements[i];
                if (snapshot_text(snap, row.statement_kind) != "Include") continue;
                IncludeTarget target;
                target.edit_id = snapshot_text(snap, row.edit_id);
                target.raw_arguments = snapshot_text(snap, row.raw_arguments);
                target.global_order = row.global_order;
                target.byte_start = row.source.byte_start;
                target.byte_end = row.source.byte_end;
                if (row.source.source_file_index < snap.source_file_count) {
                    target.source_file = snapshot_text(
                        snap, snap.source_files[row.source.source_file_index].file_path);
                    target.source_hash = snapshot_text(
                        snap,
                        snap.source_files[row.source.source_file_index].source_hash);
                }
                targets.push_back(std::move(target));
            }
            return targets;
        };

        const KvMapSnapshot baseline = load_snapshot(handle.value);
        const std::vector<IncludeTarget> baseline_targets =
            collect_targets(baseline);
        std::vector<std::string> route_source_paths;
        route_source_paths.reserve(baseline.source_file_count);
        for (std::uint64_t i = 0; i < baseline.source_file_count; ++i) {
            route_source_paths.push_back(
                snapshot_text(baseline, baseline.source_files[i].file_path));
        }
        auto hash_disk_files = [&]() {
            std::map<std::string, std::string> hashes;
            for (const std::string& path : route_source_paths) {
                hashes[path] = hash_text(read_file_bytes(
                    std::filesystem::path(utf8_to_wide(path))));
            }
            return hashes;
        };
        const auto disk_hashes_before = hash_disk_files();

        *out << "baseline_statement_count=" << baseline.statement_count << "\n"
             << "baseline_include_count=" << baseline_targets.size() << "\n"
             << "baseline_file_structure_count="
             << baseline.file_structure_count << "\n";
        for (size_t i = 0; i < baseline_targets.size(); ++i) {
            const IncludeTarget& item = baseline_targets[i];
            *out << "baseline_target=" << i << "|order=" << item.global_order
                 << "|file=\"" << item.source_file << "\"|args="
                 << item.raw_arguments << "\n";
        }
        check("has_includes", !baseline_targets.empty());
        if (baseline_targets.empty() || options.index < 0 ||
            static_cast<size_t>(options.index) >= baseline_targets.size()) {
            check("target_found", false);
            *out << "result=FAIL\n";
            out->flush();
            return failed_cases == 0 ? 0 : 26;
        }
        const IncludeTarget& target =
            baseline_targets[static_cast<size_t>(options.index)];
        check("target_found", true);
        *out << "target_edit_id=" << target.edit_id << "\n"
             << "target_source_file=\"" << target.source_file << "\"\n"
             << "target_raw_arguments=" << target.raw_arguments << "\n";

        Change deletion;
        deletion.change_id = "headless-include-delete";
        deletion.edit_id = target.edit_id;
        deletion.operation = KV_EDIT_DELETE;
        deletion.expected_source_hash = target.source_hash;

        Change stale = deletion;
        stale.expected_source_hash = "deadbeef00000000";
        bool stale_blocked = false;
        try {
            typed_edit_headless::Report stale_report =
                typed_edit_headless::dry_run(handle.value, {stale});
            stale_blocked = !stale_report.ok;
            for (const std::string& error : stale_report.blocking_errors) {
                *out << "stale_blocking_error=" << error << "\n";
            }
        } catch (const std::exception&) {
            stale_blocked = true;
        }
        check("stale_hash_blocked", stale_blocked);

        typed_edit_headless::Report dry =
            typed_edit_headless::dry_run(handle.value, {deletion});
        for (const std::string& error : dry.blocking_errors) {
            *out << "dry_blocking_error=" << error << "\n";
        }
        check("dry_run_ok", dry.ok && dry.delete_count == 1 &&
                                dry.full_reparse_ok);

        typed_edit_headless::Report applied =
            typed_edit_headless::apply_to_memory(handle.value, {deletion});
        for (const std::string& error : applied.blocking_errors) {
            *out << "apply_blocking_error=" << error << "\n";
        }
        check("apply_ok", applied.ok && applied.delete_count == 1 &&
                              applied.full_reparse_ok &&
                              applied.non_target_changed_count == 0);

        auto span_still_present = [&](const std::vector<IncludeTarget>& targets) {
            for (const IncludeTarget& candidate : targets) {
                if (candidate.source_file == target.source_file &&
                    candidate.byte_start == target.byte_start &&
                    candidate.byte_end == target.byte_end) {
                    return true;
                }
            }
            return false;
        };

        const KvMapSnapshot after_apply = load_snapshot(handle.value);
        const std::vector<IncludeTarget> after_targets =
            collect_targets(after_apply);
        check("include_removed_from_statements",
              !span_still_present(after_targets) &&
                  after_targets.size() < baseline_targets.size());
        check("file_structure_shrunk",
              after_apply.file_structure_count <
                  baseline.file_structure_count);
        *out << "applied_include_count=" << after_targets.size() << "\n"
             << "applied_statement_count=" << after_apply.statement_count << "\n"
             << "applied_file_structure_count="
             << after_apply.file_structure_count << "\n";

        check("reset_memory_ok", kv_edit_reset_memory(handle.value) != 0);
        const KvMapSnapshot after_reset = load_snapshot(handle.value);
        check("reset_restores_baseline",
              collect_targets(after_reset).size() == baseline_targets.size() &&
                  after_reset.statement_count == baseline.statement_count &&
                  after_reset.file_structure_count ==
                      baseline.file_structure_count);

        if (options.commit) {
            typed_edit_headless::Report reapplied = typed_edit_headless::
                apply_to_memory(handle.value, {deletion});
            for (const std::string& error : reapplied.blocking_errors) {
                *out << "reapply_blocking_error=" << error << "\n";
            }
            check("reapply_ok", reapplied.ok && reapplied.full_reparse_ok);
            typed_edit_headless::Report committed =
                typed_edit_headless::commit(handle.value);
            check("commit_ok",
                  committed.ok && !committed.committed_files.empty());
            for (const typed_edit_headless::CommittedFile& file :
                 committed.committed_files) {
                *out << "committed_file=\"" << file.file_path
                     << "\" committed_hash=" << file.source_hash << "\n";
            }
            MapHandle reloaded;
            reloaded.value = kv_load_map_ex(options.path.c_str(),
                                            options.unit_distance,
                                            KV_LOAD_EDIT_METADATA);
            check("reload_ok", reloaded.value != nullptr);
            if (reloaded.value) {
                const KvMapSnapshot reloaded_snapshot =
                    load_snapshot(reloaded.value);
                const std::vector<IncludeTarget> reloaded_targets =
                    collect_targets(reloaded_snapshot);
                check("reload_include_removed",
                      !span_still_present(reloaded_targets) &&
                          reloaded_targets.size() <
                              baseline_targets.size());
                *out << "reloaded_include_count=" << reloaded_targets.size()
                     << "\n"
                     << "reloaded_statement_count="
                     << reloaded_snapshot.statement_count << "\n"
                     << "reloaded_file_structure_count="
                     << reloaded_snapshot.file_structure_count << "\n";
            }
        } else {
            const auto disk_hashes_after = hash_disk_files();
            check("disk_untouched_without_commit",
                  disk_hashes_after == disk_hashes_before);
        }

        *out << "result=" << (failed_cases == 0 ? "PASS" : "FAIL") << "\n";
        out->flush();
        return failed_cases == 0 ? 0 : 26;
    } catch (const std::exception& e) {
        *out << "error=" << e.what() << "\nresult=FAIL\n";
        out->flush();
        return 27;
    }
}

int run_debug_headless_include_replace(const HeadlessIncludeReplaceOptions& options) {
    using namespace distance_batch_headless;
    using typed_edit_headless::Change;

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

    int failed_cases = 0;
    auto check = [&](const char* label, bool value) {
        *out << label << '=' << (value ? 1 : 0) << "\n";
        if (!value) ++failed_cases;
    };
    auto read_file_bytes = [](const std::filesystem::path& path) {
        std::ifstream file(path, std::ios::binary);
        return std::string((std::istreambuf_iterator<char>(file)),
                           std::istreambuf_iterator<char>());
    };

    const std::string new_arguments = "'" + options.new_path + "'";

    *out << "command=debug-headless-include-replace\n"
         << "map_path=\"" << options.path << "\"\n"
         << "index=" << options.index << "\n"
         << "new_path=" << options.new_path << "\n"
         << "commit_requested=" << (options.commit ? 1 : 0) << "\n";

    try {
        MapHandle handle;
        handle.value = kv_load_map_ex(options.path.c_str(), options.unit_distance,
                                      KV_LOAD_EDIT_METADATA);
        if (!handle.value) {
            const char* error = kv_get_last_error();
            throw std::runtime_error(std::string("map load failed") +
                (error ? ": " + std::string(error) : std::string{}));
        }
        check("load_ok", true);

        auto load_snapshot = [&](void* map_handle) {
            KvMapSnapshot result{};
            if (!kv_get_map_snapshot(map_handle, KV_MAP_SNAPSHOT_VERSION,
                                     &result, sizeof(result)) ||
                result.version != KV_MAP_SNAPSHOT_VERSION ||
                result.structure_size < sizeof(KvMapSnapshot)) {
                const char* error = kv_get_last_error();
                throw std::runtime_error(std::string("map snapshot failed") +
                    (error ? ": " + std::string(error) : std::string{}));
            }
            return result;
        };
        auto snapshot_text = [](const KvMapSnapshot& snap, KvStringRef ref) {
            if (ref.length == 0) return std::string{};
            if (!snap.string_data || ref.offset > snap.string_size ||
                ref.length > snap.string_size - ref.offset) {
                throw std::runtime_error(
                    "map snapshot string reference is out of range");
            }
            return std::string(snap.string_data + static_cast<size_t>(ref.offset),
                               static_cast<size_t>(ref.length));
        };
        struct IncludeTarget {
            std::string edit_id;
            std::string source_hash;
            std::string raw_arguments;
            std::string evaluated_path;
            std::string source_file;
            std::uint64_t byte_start = 0;
            int global_order = 0;
        };
        auto collect_targets = [&](const KvMapSnapshot& snap) {
            std::vector<IncludeTarget> targets;
            for (std::uint64_t i = 0; i < snap.statement_count; ++i) {
                const KvStatementRow& row = snap.statements[i];
                if (snapshot_text(snap, row.statement_kind) != "Include") continue;
                IncludeTarget target;
                target.edit_id = snapshot_text(snap, row.edit_id);
                target.raw_arguments = snapshot_text(snap, row.raw_arguments);
                target.global_order = row.global_order;
                target.byte_start = row.source.byte_start;
                if (row.evaluated_values.count > 0 &&
                    row.evaluated_values.offset < snap.value_count) {
                    const KvValue& value =
                        snap.values[row.evaluated_values.offset];
                    target.evaluated_path =
                        value.kind == KV_VALUE_STRING
                            ? snapshot_text(snap, value.string_value)
                            : std::string{};
                }
                if (row.source.source_file_index < snap.source_file_count) {
                    target.source_file = snapshot_text(
                        snap, snap.source_files[row.source.source_file_index].file_path);
                    target.source_hash = snapshot_text(
                        snap,
                        snap.source_files[row.source.source_file_index].source_hash);
                }
                targets.push_back(std::move(target));
            }
            return targets;
        };
        struct StructureNodeRef {
            size_t parent_index = static_cast<size_t>(-1);
            std::string include_path;
            std::string absolute_path;
        };
        auto collect_structure_nodes = [&](const KvMapSnapshot& snap) {
            std::vector<StructureNodeRef> nodes;
            nodes.reserve(static_cast<size_t>(snap.file_structure_count));
            for (std::uint64_t i = 0; i < snap.file_structure_count; ++i) {
                const KvFileStructureRow& row = snap.file_structure[i];
                StructureNodeRef node;
                node.parent_index = row.parent_index < 0
                    ? static_cast<size_t>(-1)
                    : static_cast<size_t>(row.parent_index);
                node.include_path = snapshot_text(snap, row.include_path);
                node.absolute_path = snapshot_text(snap, row.absolute_path);
                nodes.push_back(std::move(node));
            }
            return nodes;
        };

        const KvMapSnapshot baseline_snapshot = load_snapshot(handle.value);
        const std::vector<IncludeTarget> baseline_targets =
            collect_targets(baseline_snapshot);
        std::vector<std::string> route_source_paths;
        route_source_paths.reserve(baseline_snapshot.source_file_count);
        for (std::uint64_t i = 0; i < baseline_snapshot.source_file_count; ++i) {
            route_source_paths.push_back(snapshot_text(
                baseline_snapshot, baseline_snapshot.source_files[i].file_path));
        }
        auto hash_disk_files = [&]() {
            std::map<std::string, std::string> hashes;
            for (const std::string& path : route_source_paths) {
                hashes[path] = hash_text(read_file_bytes(
                    std::filesystem::path(utf8_to_wide(path))));
            }
            return hashes;
        };
        const auto disk_hashes_before = hash_disk_files();

        *out << "baseline_include_count=" << baseline_targets.size() << "\n";
        for (size_t i = 0; i < baseline_targets.size(); ++i) {
            const IncludeTarget& item = baseline_targets[i];
            *out << "baseline_target=" << i << "|order=" << item.global_order
                 << "|file=\"" << item.source_file << "\"|args="
                 << item.raw_arguments << "\n";
        }
        check("has_includes", !baseline_targets.empty());
        if (baseline_targets.empty() || options.index < 0 ||
            static_cast<size_t>(options.index) >= baseline_targets.size()) {
            check("target_found", false);
            *out << "result=FAIL\n";
            out->flush();
            return failed_cases == 0 ? 0 : 26;
        }
        const IncludeTarget& target =
            baseline_targets[static_cast<size_t>(options.index)];
        check("target_found", true);
        if (new_arguments == target.raw_arguments) {
            check("new_path_differs", false);
            *out << "result=FAIL\n";
            out->flush();
            return failed_cases == 0 ? 0 : 26;
        }
        check("new_path_differs", true);
        *out << "target_edit_id=" << target.edit_id << "\n"
             << "target_source_file=\"" << target.source_file << "\"\n"
             << "target_old_args=" << target.raw_arguments << "\n";

        // The expected reparsed absolute path follows the parser's own rule:
        // include arguments resolve against the entry map directory.
        std::filesystem::path expected_absolute;
        std::error_code ec;
        std::filesystem::path requested(options.new_path);
        if (requested.is_absolute()) {
            expected_absolute = requested.lexically_normal();
        } else {
            expected_absolute =
                (std::filesystem::absolute(std::filesystem::path(utf8_to_wide(
                     options.path)), ec).parent_path() / requested).lexically_normal();
            ec.clear();
        }

        Change replacement;
        replacement.change_id = "headless-include-replace";
        replacement.edit_id = target.edit_id;
        replacement.operation = KV_EDIT_UPDATE;
        replacement.fields.push_back({"includePath", options.new_path});
        replacement.expected_source_hash = target.source_hash;

        Change stale = replacement;
        stale.expected_source_hash = "deadbeef00000000";
        bool stale_blocked = false;
        try {
            typed_edit_headless::Report stale_report =
                typed_edit_headless::dry_run(handle.value, {stale});
            stale_blocked = !stale_report.ok;
            for (const std::string& error : stale_report.blocking_errors) {
                *out << "stale_blocking_error=" << error << "\n";
            }
        } catch (const std::exception&) {
            stale_blocked = true;
        }
        check("stale_hash_blocked", stale_blocked);

        typed_edit_headless::Report dry =
            typed_edit_headless::dry_run(handle.value, {replacement});
        for (const std::string& error : dry.blocking_errors) {
            *out << "dry_blocking_error=" << error << "\n";
        }
        check("dry_run_ok", dry.ok && dry.update_count == 1 &&
                                dry.full_reparse_ok);

        typed_edit_headless::Report applied =
            typed_edit_headless::apply_to_memory(handle.value, {replacement});
        for (const std::string& error : applied.blocking_errors) {
            *out << "apply_blocking_error=" << error << "\n";
        }
        check("apply_ok", applied.ok && applied.update_count == 1 &&
                              applied.full_reparse_ok &&
                              applied.non_target_changed_count == 0);

        const KvMapSnapshot after_apply = load_snapshot(handle.value);
        const std::vector<IncludeTarget> after_targets =
            collect_targets(after_apply);
        bool statement_replaced = false;
        for (const IncludeTarget& item : after_targets) {
            if (item.source_file == target.source_file &&
                item.byte_start == target.byte_start &&
                item.raw_arguments == new_arguments) {
                statement_replaced = true;
                break;
            }
        }
        check("include_statement_updated", statement_replaced);

        const std::vector<StructureNodeRef> after_nodes =
            collect_structure_nodes(after_apply);
        bool structure_updated = false;
        for (const StructureNodeRef& node : after_nodes) {
            std::error_code equivalent_error;
            const bool same_target = node.include_path == options.new_path &&
                std::filesystem::equivalent(
                    std::filesystem::path(utf8_to_wide(node.absolute_path)),
                    expected_absolute, equivalent_error);
            if (same_target) {
                structure_updated = true;
                break;
            }
        }
        check("file_structure_updated", structure_updated);
        *out << "applied_include_count=" << after_targets.size() << "\n"
             << "expected_absolute=\"" << expected_absolute.u8string() << "\"\n";

        check("reset_memory_ok", kv_edit_reset_memory(handle.value) != 0);
        const KvMapSnapshot after_reset = load_snapshot(handle.value);
        bool reset_restored = false;
        for (const IncludeTarget& item : collect_targets(after_reset)) {
            if (item.source_file == target.source_file &&
                item.byte_start == target.byte_start &&
                item.raw_arguments == target.raw_arguments) {
                reset_restored = true;
                break;
            }
        }
        check("reset_restores_baseline", reset_restored);

        if (options.commit) {
            typed_edit_headless::Report reapplied = typed_edit_headless::
                apply_to_memory(handle.value, {replacement});
            for (const std::string& error : reapplied.blocking_errors) {
                *out << "reapply_blocking_error=" << error << "\n";
            }
            check("reapply_ok", reapplied.ok && reapplied.full_reparse_ok);
            typed_edit_headless::Report committed =
                typed_edit_headless::commit(handle.value);
            check("commit_ok",
                  committed.ok && !committed.committed_files.empty());
            MapHandle reloaded;
            reloaded.value = kv_load_map_ex(options.path.c_str(),
                                            options.unit_distance,
                                            KV_LOAD_EDIT_METADATA);
            check("reload_ok", reloaded.value != nullptr);
            if (reloaded.value) {
                bool persisted = false;
                for (const IncludeTarget& item :
                     collect_targets(load_snapshot(reloaded.value))) {
                    if (item.source_file == target.source_file &&
                        item.byte_start == target.byte_start &&
                        item.raw_arguments == new_arguments) {
                        persisted = true;
                        break;
                    }
                }
                check("reload_keeps_new_path", persisted);
            }
        } else {
            const auto disk_hashes_after = hash_disk_files();
            check("disk_untouched_without_commit",
                  disk_hashes_after == disk_hashes_before);
        }

        *out << "result=" << (failed_cases == 0 ? "PASS" : "FAIL") << "\n";
        out->flush();
        return failed_cases == 0 ? 0 : 26;
    } catch (const std::exception& e) {
        *out << "error=" << e.what() << "\nresult=FAIL\n";
        out->flush();
        return 27;
    }
}

int run_debug_headless_include_import_create(
    const HeadlessIncludeImportCreateOptions& options) {
    using namespace distance_batch_headless;
    using typed_edit_headless::Change;

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

    int failed_cases = 0;
    auto check = [&](const char* label, bool value) {
        *out << label << '=' << (value ? 1 : 0) << "\n";
        if (!value) ++failed_cases;
    };
    auto read_file_bytes = [](const std::filesystem::path& path) {
        std::ifstream file(path, std::ios::binary);
        return std::string((std::istreambuf_iterator<char>(file)),
                           std::istreambuf_iterator<char>());
    };
    std::vector<std::filesystem::path> created_children;
    auto cleanup_children = [&]() {
        for (const std::filesystem::path& child : created_children) {
            std::error_code error;
            std::filesystem::remove(child, error);
        }
    };

    *out << "command=debug-headless-include-import-create\n"
         << "map_path=\"" << options.path << "\"\n";
    try {
        const std::filesystem::path entry_path(utf8_to_wide(options.path));
        std::error_code absolute_error;
        const std::filesystem::path entry_absolute =
            std::filesystem::absolute(entry_path, absolute_error);
        if (absolute_error) {
            throw std::runtime_error("failed to resolve the entry map path");
        }

        MapHandle handle;
        handle.value = kv_load_map_ex(options.path.c_str(), options.unit_distance,
                                      KV_LOAD_EDIT_METADATA);
        if (!handle.value) {
            const char* error = kv_get_last_error();
            throw std::runtime_error(std::string("map load failed") +
                (error ? ": " + std::string(error) : std::string{}));
        }
        check("load_ok", true);

        auto load_snapshot = [&](void* map_handle) {
            KvMapSnapshot result{};
            if (!kv_get_map_snapshot(map_handle, KV_MAP_SNAPSHOT_VERSION,
                                     &result, sizeof(result)) ||
                result.version != KV_MAP_SNAPSHOT_VERSION ||
                result.structure_size < sizeof(KvMapSnapshot)) {
                const char* error = kv_get_last_error();
                throw std::runtime_error(std::string("map snapshot failed") +
                    (error ? ": " + std::string(error) : std::string{}));
            }
            return result;
        };
        auto snapshot_text = [](const KvMapSnapshot& snap, KvStringRef ref) {
            if (ref.length == 0) return std::string{};
            if (!snap.string_data || ref.offset > snap.string_size ||
                ref.length > snap.string_size - ref.offset) {
                throw std::runtime_error("map snapshot string reference is out of range");
            }
            return std::string(snap.string_data + static_cast<size_t>(ref.offset),
                               static_cast<size_t>(ref.length));
        };

        const KvMapSnapshot baseline = load_snapshot(handle.value);
        const std::uint64_t baseline_statement_count = baseline.statement_count;
        const std::uint64_t baseline_file_structure_count = baseline.file_structure_count;
        std::string root_source_path;
        std::vector<std::string> route_source_paths;
        route_source_paths.reserve(static_cast<size_t>(baseline.source_file_count));
        for (std::uint64_t i = 0; i < baseline.source_file_count; ++i) {
            const std::string source_path = snapshot_text(
                baseline, baseline.source_files[i].file_path);
            route_source_paths.push_back(source_path);
            std::error_code equivalent_error;
            if (std::filesystem::equivalent(
                    entry_absolute,
                    std::filesystem::path(utf8_to_wide(source_path)),
                    equivalent_error)) {
                root_source_path = source_path;
            }
        }
        if (root_source_path.empty()) {
            throw std::runtime_error("entry map is not a physical loaded source file");
        }
        auto hash_disk_files = [&]() {
            std::map<std::string, std::string> hashes;
            for (const std::string& source_path : route_source_paths) {
                hashes[source_path] = hash_text(read_file_bytes(
                    std::filesystem::path(utf8_to_wide(source_path))));
            }
            return hashes;
        };
        const std::map<std::string, std::string> disk_hashes_before =
            hash_disk_files();

        auto create_unique_child = [&](const char* prefix) {
            const auto stamp = std::chrono::steady_clock::now()
                .time_since_epoch().count();
            for (unsigned int suffix = 1; suffix <= 100; ++suffix) {
                const std::filesystem::path child = entry_absolute.parent_path() /
                    (std::string(prefix) + "-" + std::to_string(stamp) + "-" +
                     std::to_string(suffix) + ".txt");
                std::error_code exists_error;
                if (std::filesystem::exists(child, exists_error)) continue;
                std::string create_error;
                if (!create_utf8_bve_map_file_exclusive(child, create_error)) {
                    throw std::runtime_error(create_error);
                }
                created_children.push_back(child);
                return child;
            }
            throw std::runtime_error("could not reserve a unique temporary child map path");
        };

        auto verify_insert = [&](const char* mode,
                                 const std::filesystem::path& child,
                                 const ListAssetSourcePathResult& include_path) {
            if (include_path.source_path.empty()) {
                throw std::runtime_error(std::string(mode) +
                    " could not derive an Include path");
            }
            const std::string raw_arguments = "'" + include_path.source_path + "'";
            const std::string canonical_statement =
                "include " + raw_arguments + ";";
            Change insertion;
            insertion.change_id = std::string("headless-include-") + mode;
            insertion.edit_id = insertion.change_id;
            insertion.operation = KV_EDIT_INSERT;
            insertion.target_file_path = root_source_path;
            insertion.fields = {{"rowKind", "include"},
                                {"includePath", include_path.source_path}};

            typed_edit_headless::Report dry =
                typed_edit_headless::dry_run(handle.value, {insertion});
            for (const std::string& error : dry.blocking_errors) {
                *out << mode << "_dry_blocking_error=" << error << "\n";
            }
            check((std::string(mode) + "_dry_run_ok").c_str(),
                  dry.ok && dry.insert_count == 1 && dry.full_reparse_ok);

            typed_edit_headless::Report applied =
                typed_edit_headless::apply_to_memory(handle.value, {insertion});
            for (const std::string& error : applied.blocking_errors) {
                *out << mode << "_apply_blocking_error=" << error << "\n";
            }
            check((std::string(mode) + "_apply_ok").c_str(),
                  applied.ok && applied.insert_count == 1 &&
                      applied.full_reparse_ok && applied.non_target_changed_count == 0);

            const KvMapSnapshot after_apply = load_snapshot(handle.value);
            bool inserted_statement_found = false;
            std::uint64_t inserted_start = 0;
            std::uint64_t first_distance_start =
                std::numeric_limits<std::uint64_t>::max();
            std::vector<std::uint64_t> root_include_starts;
            for (std::uint64_t i = 0; i < after_apply.statement_count; ++i) {
                const KvStatementRow& row = after_apply.statements[i];
                if (row.source.source_file_index >= after_apply.source_file_count ||
                    snapshot_text(after_apply, after_apply.source_files[
                        row.source.source_file_index].file_path) != root_source_path) {
                    continue;
                }
                const std::string kind = snapshot_text(after_apply, row.statement_kind);
                if (kind == "Distance.Set") {
                    first_distance_start = std::min(first_distance_start,
                                                    row.source.byte_start);
                } else if (kind == "Include") {
                    const std::string arguments =
                        snapshot_text(after_apply, row.raw_arguments);
                    if (arguments == raw_arguments) {
                        inserted_statement_found = true;
                        inserted_start = row.source.byte_start;
                    } else {
                        root_include_starts.push_back(row.source.byte_start);
                    }
                }
            }
            bool child_node_found = false;
            for (std::uint64_t i = 0; i < after_apply.file_structure_count; ++i) {
                const KvFileStructureRow& node = after_apply.file_structure[i];
                if (snapshot_text(after_apply, node.include_path) !=
                    include_path.source_path) {
                    continue;
                }
                std::error_code equivalent_error;
                if (std::filesystem::equivalent(
                        child, std::filesystem::path(utf8_to_wide(
                                   snapshot_text(after_apply, node.absolute_path))),
                        equivalent_error)) {
                    child_node_found = true;
                    break;
                }
            }
            check((std::string(mode) + "_statement_in_target_source").c_str(),
                  inserted_statement_found);
            check((std::string(mode) + "_file_structure_updated").c_str(),
                  child_node_found);

            bool has_eligible_include = false;
            std::uint64_t last_eligible_include_start = 0;
            for (std::uint64_t start : root_include_starts) {
                if (start >= first_distance_start) continue;
                has_eligible_include = true;
                last_eligible_include_start = std::max(last_eligible_include_start, start);
            }
            const char* source_text = kv_get_source_text(handle.value,
                                                          root_source_path.c_str());
            std::string patched_source = source_text ? source_text : std::string{};
            kv_free_string(source_text);
            bool placement_ok = false;
            if (has_eligible_include) {
                placement_ok = inserted_start > last_eligible_include_start &&
                    inserted_start < first_distance_start;
            } else if (first_distance_start != std::numeric_limits<std::uint64_t>::max()) {
                placement_ok = inserted_start < first_distance_start;
            } else {
                while (!patched_source.empty() &&
                       std::isspace(static_cast<unsigned char>(patched_source.back()))) {
                    patched_source.pop_back();
                }
                placement_ok = patched_source.size() >= canonical_statement.size() &&
                    patched_source.compare(patched_source.size() - canonical_statement.size(),
                                           canonical_statement.size(),
                                           canonical_statement) == 0;
            }
            check((std::string(mode) + "_placement_matches_zero_distance_rule").c_str(),
                  placement_ok);

            check((std::string(mode) + "_reset_memory_ok").c_str(),
                  kv_edit_reset_memory(handle.value) != 0);
            const KvMapSnapshot after_reset = load_snapshot(handle.value);
            bool reset_removed_insert = true;
            for (std::uint64_t i = 0; i < after_reset.statement_count; ++i) {
                const KvStatementRow& row = after_reset.statements[i];
                if (row.source.source_file_index < after_reset.source_file_count &&
                    snapshot_text(after_reset, after_reset.source_files[
                        row.source.source_file_index].file_path) == root_source_path &&
                    snapshot_text(after_reset, row.statement_kind) == "Include" &&
                    snapshot_text(after_reset, row.raw_arguments) == raw_arguments) {
                    reset_removed_insert = false;
                    break;
                }
            }
            check((std::string(mode) + "_reset_restores_parent").c_str(),
                  reset_removed_insert &&
                      after_reset.statement_count == baseline_statement_count &&
                      after_reset.file_structure_count == baseline_file_structure_count);
            check((std::string(mode) + "_parent_disk_untouched").c_str(),
                  hash_disk_files() == disk_hashes_before);
        };

        const std::filesystem::path imported_child =
            create_unique_child("komapedit-headless-import");
        const ListAssetSourcePathResult imported_path = make_list_asset_source_path(
            options.path, wide_to_utf8(imported_child.wstring()));
        check("import_relative_path_ready", !imported_path.source_path.empty() &&
                                           imported_path.fallback_reason.empty());
        verify_insert("import", imported_child, imported_path);

        const std::filesystem::path created_child =
            create_unique_child("komapedit-headless-create");
        check("new_child_standard_header",
              read_file_bytes(created_child) == "BveTs Map 2.02:utf-8\r\n");
        std::string overwrite_error;
        check("new_child_overwrite_rejected",
              !create_utf8_bve_map_file_exclusive(created_child, overwrite_error) &&
                  read_file_bytes(created_child) == "BveTs Map 2.02:utf-8\r\n");
        const ListAssetSourcePathResult created_path = make_list_asset_source_path(
            options.path, wide_to_utf8(created_child.wstring()));
        check("create_relative_path_ready", !created_path.source_path.empty() &&
                                           created_path.fallback_reason.empty());
        verify_insert("create", created_child, created_path);

        cleanup_children();
        check("temporary_children_cleaned", std::all_of(
            created_children.begin(), created_children.end(),
            [](const std::filesystem::path& child) {
                std::error_code error;
                return !std::filesystem::exists(child, error);
            }));
        *out << "result=" << (failed_cases == 0 ? "PASS" : "FAIL") << "\n";
        out->flush();
        return failed_cases == 0 ? 0 : 26;
    } catch (const std::exception& e) {
        cleanup_children();
        *out << "error=" << e.what() << "\nresult=FAIL\n";
        out->flush();
        return 27;
    }
}

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
    size_t selected_row_count = 0;
    int selected_first_line = 0;
    int selected_last_line = 0;
    std::array<std::string, 6> selected_edit_ids{};
    bool commit_requested = false;
    bool first_move_up_disabled = false;
    bool last_move_down_disabled = false;
    bool opposite_moves_equivalent = false;
    bool full_row_templates_swapped = false;
    bool clear_normal_cell_draft_ok = false;
    bool clear_numeric_cell_draft_ok = false;
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
            clear_normal_cell_draft_ok && clear_numeric_cell_draft_ok &&
            clear_key_draft_ok &&
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

bool same_path(const std::string& left, const std::string& right) {
    const std::filesystem::path a(utf8_to_wide(left));
    const std::filesystem::path b(utf8_to_wide(right));
    return ascii_lower(wide_to_utf8(a.lexically_normal().wstring())) ==
        ascii_lower(wide_to_utf8(b.lexically_normal().wstring()));
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

std::vector<EditableListDraftRow> make_drafts(
    const std::vector<StationRow>& rows) {
    std::vector<EditableListDraftRow> drafts;
    drafts.reserve(rows.size());
    for (const StationRow& source : rows) {
        EditableListDraftRow row;
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
    const std::vector<EditableListDraftRow>& drafts) {
    std::map<std::string, MapElementPendingChange> result;
    std::string error;
    if (!build_editable_list_pending_changes(
            k_station_definition_edit_spec, drafts, {}, result, error)) {
        throw std::runtime_error(error.empty()
            ? "shared Station.List draft builder failed" : error);
    }
    return result;
}

std::optional<size_t> select_consecutive_run(const std::vector<StationRow>& rows,
                                             size_t required_count) {
    std::map<std::string, int> key_counts;
    for (const StationRow& row : rows) {
        const std::string key = ascii_lower(row.values[0]);
        if (!key.empty()) ++key_counts[key];
    }
    for (size_t begin = 0; begin + required_count <= rows.size(); ++begin) {
        bool valid = !rows[begin + 2].values[1].empty();
        std::set<std::string> keys;
        for (size_t offset = 0; valid && offset < required_count; ++offset) {
            const StationRow& row = rows[begin + offset];
            const std::string key = ascii_lower(row.values[0]);
            valid = !key.empty() && key_counts[key] == 1 && keys.insert(key).second;
            if (offset != 0) {
                valid = valid &&
                    row.source_line == rows[begin + offset - 1].source_line + 1;
            }
        }
        if (valid) return begin;
    }
    return std::nullopt;
}

const StationRow* find_edit_id(const std::vector<StationRow>& rows,
                               const std::string& edit_id) {
    const auto found = std::find_if(rows.begin(), rows.end(),
                                    [&](const StationRow& row) {
                                        return row.edit_id == edit_id;
                                    });
    return found == rows.end() ? nullptr : &*found;
}

bool station_numeric_field_defaults_to_zero(size_t field) {
    return field == 4 || field == 6 || field == 7 || field == 8 ||
        field == 11 || field == 12;
}

std::string station_serialized_value(const std::string& baseline, size_t field) {
    return station_numeric_field_defaults_to_zero(field) && baseline.empty()
        ? "0"
        : baseline;
}

bool station_values_match_serialized(
    const std::array<std::string, 13>& actual,
    const std::array<std::string, 13>& baseline) {
    for (size_t field = 0; field < actual.size(); ++field) {
        if (actual[field] != station_serialized_value(baseline[field], field)) {
            return false;
        }
    }
    return true;
}

bool station_name_key_absent(void* handle, const std::string& key) {
    const KvMapSnapshot snapshot =
        distance_batch_headless::current_map_snapshot(handle);
    for (std::uint64_t index = 0; index < snapshot.station_name_count; ++index) {
        const std::string candidate = distance_batch_headless::snapshot_text(
            snapshot, snapshot.station_names[index].key);
        if (ascii_lower(candidate) == ascii_lower(key)) return false;
    }
    return true;
}

bool snapshot_matches_edits(
    void* handle, const std::string& target_file,
    const std::vector<StationRow>& baseline, size_t run,
    size_t baseline_global_count, bool has_sentinel, bool& empty_row_persisted,
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
    const StationRow* sentinel = has_sentinel
        ? find_edit_id(target, baseline[run + 5].edit_id)
        : nullptr;
    stable_ids = first && second && cleared_normal && cleared_key && !deleted &&
        (!has_sentinel || sentinel);
    if (!stable_ids || snapshot.station_list_count + 1 != baseline_global_count ||
        target.size() + 1 != baseline.size()) {
        return false;
    }
    const bool first_swapped = station_values_match_serialized(
        first->values, baseline[run + 1].values);
    const bool second_swapped = station_values_match_serialized(
        second->values, baseline[run].values);
    bool normal_only = cleared_normal->values[1].empty() &&
        cleared_normal->values[4] == "0";
    for (size_t field = 0; field < cleared_normal->values.size(); ++field) {
        if (field != 1 && field != 4 &&
            cleared_normal->values[field] !=
                station_serialized_value(baseline[run + 2].values[field], field)) {
            normal_only = false;
        }
    }
    bool key_only = cleared_key->values[0].empty();
    for (size_t field = 1; field < cleared_key->values.size(); ++field) {
        if (cleared_key->values[field] !=
            station_serialized_value(baseline[run + 3].values[field], field)) {
            key_only = false;
        }
    }
    empty_row_persisted = key_only;
    empty_key_not_registered =
        station_name_key_absent(handle, "") &&
        station_name_key_absent(handle, baseline[run + 3].values[0]) &&
        station_name_key_absent(handle, baseline[run + 4].values[0]);
    return first_swapped && second_swapped && normal_only && key_only &&
        (!has_sentinel || station_values_match_serialized(
            sentinel->values, baseline[run + 5].values));
}

bool persisted_snapshot_matches(
    void* handle, const std::string& target_file,
    const std::vector<StationRow>& baseline, size_t run,
    size_t baseline_global_count, bool has_sentinel, bool& empty_row_persisted,
    bool& empty_key_not_registered) {
    const KvMapSnapshot snapshot =
        distance_batch_headless::current_map_snapshot(handle);
    std::vector<StationRow> all = collect_station_rows(handle);
    std::vector<StationRow> target;
    for (StationRow& row : all) {
        if (same_path(row.source_file, target_file)) target.push_back(std::move(row));
    }
    if (snapshot.station_list_count + 1 != baseline_global_count ||
        target.size() + 1 != baseline.size() || run + 3 >= target.size() ||
        (has_sentinel && run + 4 >= target.size())) {
        return false;
    }
    bool normal_only = target[run + 2].values[1].empty() &&
        target[run + 2].values[4] == "0";
    for (size_t field = 0; field < target[run + 2].values.size(); ++field) {
        if (field != 1 && field != 4 &&
            target[run + 2].values[field] !=
                station_serialized_value(baseline[run + 2].values[field], field)) {
            normal_only = false;
        }
    }
    bool key_only = target[run + 3].values[0].empty();
    for (size_t field = 1; field < target[run + 3].values.size(); ++field) {
        if (target[run + 3].values[field] !=
            station_serialized_value(baseline[run + 3].values[field], field)) {
            key_only = false;
        }
    }
    empty_row_persisted = key_only;
    empty_key_not_registered =
        station_name_key_absent(handle, "") &&
        station_name_key_absent(handle, baseline[run + 3].values[0]) &&
        station_name_key_absent(handle, baseline[run + 4].values[0]);
    return station_values_match_serialized(target[run].values, baseline[run + 1].values) &&
        station_values_match_serialized(target[run + 1].values, baseline[run].values) &&
        normal_only && key_only &&
        (!has_sentinel || station_values_match_serialized(
            target[run + 4].values, baseline[run + 5].values));
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
    out << "selected_row_count=" << facts.selected_row_count << "\n";
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
    flag("clear_numeric_cell_draft_ok", facts.clear_numeric_cell_draft_ok);
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
                return ascii_lower(wide_to_utf8(
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
        if (selected_file == rows_by_file.end() || selected_file->second.size() < 5) {
            throw std::runtime_error(
                "no Station.List source file contains at least five definitions");
        }
        const std::vector<StationRow>& baseline = selected_file->second;
        facts.target_file = selected_file->first;
        facts.baseline_target_row_count = baseline.size();
        const std::optional<size_t> sentinel_run =
            select_consecutive_run(baseline, 6);
        const std::optional<size_t> five_row_run = sentinel_run
            ? sentinel_run
            : select_consecutive_run(baseline, 5);
        if (!five_row_run) {
            throw std::runtime_error(
                "no five consecutive unique Station.List definitions were found");
        }
        const size_t run = *five_row_run;
        facts.selected_row_count = sentinel_run ? 6 : 5;
        const bool has_sentinel = facts.selected_row_count == 6;
        facts.selected_first_line = baseline[run].source_line;
        facts.selected_last_line =
            baseline[run + facts.selected_row_count - 1].source_line;
        for (size_t index = 0; index < facts.selected_row_count; ++index) {
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

        std::vector<EditableListDraftRow> boundary_drafts =
            make_drafts(baseline);
        const std::vector<size_t> boundary_visible =
            editable_list_visible_row_indices(boundary_drafts);
        facts.first_move_up_disabled = !move_editable_list_draft_row(
            boundary_drafts, boundary_visible, 0, -1);
        facts.last_move_down_disabled = !move_editable_list_draft_row(
            boundary_drafts, boundary_visible,
            static_cast<int>(boundary_visible.size()) - 1, 1);

        std::vector<EditableListDraftRow> down_drafts = make_drafts(baseline);
        const std::vector<size_t> down_visible =
            editable_list_visible_row_indices(down_drafts);
        if (!move_editable_list_draft_row(
                down_drafts, down_visible, static_cast<int>(run), 1)) {
            throw std::runtime_error("shared first-row move-down operation failed");
        }
        const auto down_pending = build_pending(down_drafts);

        std::vector<EditableListDraftRow> up_drafts = make_drafts(baseline);
        const std::vector<size_t> up_visible =
            editable_list_visible_row_indices(up_drafts);
        if (!move_editable_list_draft_row(
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

        std::vector<EditableListDraftRow> drafts = make_drafts(baseline);
        const std::vector<size_t> visible =
            editable_list_visible_row_indices(drafts);
        if (!move_editable_list_draft_row(
                drafts, visible, static_cast<int>(run), 1) ||
            !clear_editable_list_draft_cell(
                drafts, visible, static_cast<int>(run + 2), 1) ||
            !clear_editable_list_draft_cell(
                drafts, visible, static_cast<int>(run + 2), 4) ||
            !clear_editable_list_draft_cell(
                drafts, visible, static_cast<int>(run + 3), 0) ||
            !delete_editable_list_draft_row(
                drafts, visible, static_cast<int>(run + 4))) {
            throw std::runtime_error("shared composite Station.List draft operation failed");
        }
        facts.clear_normal_cell_draft_ok =
            drafts[run + 2].values[1].empty();
        facts.clear_numeric_cell_draft_ok =
            drafts[run + 2].values[4].empty();
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
            facts.baseline_station_count, has_sentinel,
            facts.empty_key_row_persisted,
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
        for (size_t index = 0; index < facts.selected_row_count; ++index) {
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
                (!has_sentinel ||
                 committed_station_ids.count(baseline[run + 5].edit_id) != 0);
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
                facts.baseline_station_count, has_sentinel, reopened_empty_row,
                reopened_empty_lookup);
            facts.committed_snapshot_ok = facts.committed_snapshot_ok &&
                reopened_empty_row && reopened_empty_lookup;

            const size_t first_line_index =
                static_cast<size_t>(baseline[run].source_line - 1);
            const size_t delete_line_index =
                static_cast<size_t>(baseline[run + 4].source_line - 1);
            const bool physical_range_ok =
                first_line_index + facts.selected_row_count - 1 < baseline_lines.size() &&
                first_line_index + 3 < committed_lines.size();
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
                    normal_values[4] == "0" &&
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
                    (!has_sentinel ||
                     (delete_line_index < committed_lines.size() &&
                      committed_lines[delete_line_index] ==
                          baseline[run + 5].raw_statement));
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

namespace repeater_key_edit_headless {

using EditChange = typed_edit_headless::Change;
using EditReport = typed_edit_headless::Report;
using MapHandle = distance_batch_headless::MapHandle;

struct Target {
    std::string edit_id;
    std::string expected_source_hash;
    std::string source_file;
    std::string method;
    std::string original_key;
    int source_line = 0;
    double distance = 0.0;
};

struct Selection {
    std::vector<Target> targets;
    std::string original_key;
    std::string overlapping_key;
    double begin_distance = 0.0;
    std::optional<double> end_distance;
};

struct RunFacts {
    std::string path;
    bool commit_requested = false;
    Selection selection;
    std::string new_key;
    bool globally_unused_key = false;
    bool atomic_guard_ok = false;
    bool overlap_candidate_found = false;
    bool overlap_guard_ok = false;
    bool dry_run_ok = false;
    int dry_run_update_count = 0;
    bool apply_ok = false;
    bool apply_snapshot_ok = false;
    int non_target_changed_count = 0;
    bool reset_ok = false;
    bool reset_snapshot_ok = false;
    bool second_apply_ok = false;
    bool commit_attempted = false;
    bool commit_ok = false;
    bool reload_ok = false;
    std::vector<std::pair<std::string, std::string>> baseline_hashes;
    std::vector<std::pair<std::string, std::string>> reload_hashes;
    std::vector<std::string> changed_files;
    std::string error;

    bool passed() const {
        const bool overlap_result = !overlap_candidate_found || overlap_guard_ok;
        const bool commit_result = !commit_requested ||
            (commit_attempted && commit_ok && !changed_files.empty());
        return error.empty() && selection.targets.size() >= 3 && globally_unused_key &&
            atomic_guard_ok && overlap_result && dry_run_ok &&
            dry_run_update_count == static_cast<int>(selection.targets.size()) &&
            apply_ok && apply_snapshot_ok && non_target_changed_count == 0 &&
            reset_ok && reset_snapshot_ok && second_apply_ok && commit_result && reload_ok;
    }
};

repeater_linkage::Linkage linkage_from_snapshot(const KvMapSnapshot& snapshot) {
    std::vector<repeater_linkage::Event> events;
    events.reserve(static_cast<size_t>(snapshot.repeater_count));
    for (std::uint64_t index = 0; index < snapshot.repeater_count; ++index) {
        const KvRepeaterRow& row = snapshot.repeaters[index];
        repeater_linkage::Event event;
        event.source_index = static_cast<size_t>(index);
        event.distance = row.distance;
        event.order = static_cast<double>(row.order);
        event.key = distance_batch_headless::snapshot_value_text(snapshot, row.repeater_key);
        const std::string method =
            distance_batch_headless::snapshot_text(snapshot, row.method);
        if (method == "Begin" || method == "Begin0") {
            event.kind = repeater_linkage::EventKind::Begin;
        } else if (method == "End") {
            event.kind = repeater_linkage::EventKind::End;
        }
        events.push_back(std::move(event));
    }
    return repeater_linkage::pair_linkage(std::move(events));
}

Target target_from_row(const KvMapSnapshot& snapshot, size_t row_index) {
    if (row_index >= snapshot.repeater_count) {
        throw std::runtime_error("Repeater key target index is out of range");
    }
    const KvRepeaterRow& row = snapshot.repeaters[row_index];
    if (row.metadata.source_file_index == KV_INDEX_NONE ||
        row.metadata.source_file_index >= snapshot.source_file_count) {
        throw std::runtime_error("Repeater key target has no source file metadata");
    }
    const KvSourceFileRow& source = snapshot.source_files[row.metadata.source_file_index];
    Target target;
    target.edit_id = distance_batch_headless::snapshot_text(snapshot, row.metadata.edit_id);
    target.expected_source_hash =
        distance_batch_headless::snapshot_text(snapshot, source.source_hash);
    target.source_file = distance_batch_headless::snapshot_text(snapshot, source.file_path);
    target.method = distance_batch_headless::snapshot_text(snapshot, row.method);
    target.original_key =
        distance_batch_headless::snapshot_value_text(snapshot, row.repeater_key);
    target.source_line = row.metadata.line;
    target.distance = row.distance;
    if (target.edit_id.empty() || target.expected_source_hash.empty() ||
        target.source_file.empty()) {
        throw std::runtime_error("Repeater key target has incomplete edit metadata");
    }
    return target;
}

Selection select_chain(const KvMapSnapshot& snapshot,
                       const repeater_linkage::Linkage& linkage) {
    std::optional<size_t> fallback;
    std::optional<size_t> selected;
    std::string overlap_key;
    for (size_t chain_index = 0; chain_index < linkage.chains.size(); ++chain_index) {
        const repeater_linkage::Chain& chain = linkage.chains[chain_index];
        if (chain.begin_source_indices.size() < 2 || !chain.end_source_index) continue;
        bool valid = true;
        for (size_t row_index : chain.begin_source_indices) {
            if (row_index >= snapshot.repeater_count) {
                valid = false;
                break;
            }
            const std::string method = distance_batch_headless::snapshot_text(
                snapshot, snapshot.repeaters[row_index].method);
            valid = valid && (method == "Begin" || method == "Begin0");
        }
        if (!valid || *chain.end_source_index >= snapshot.repeater_count) continue;
        if (!fallback) fallback = chain_index;
        for (size_t other_index = 0; other_index < linkage.chains.size(); ++other_index) {
            if (other_index == chain_index) continue;
            const repeater_linkage::Chain& other = linkage.chains[other_index];
            if (chain.key != other.key &&
                repeater_linkage::half_open_intervals_overlap(chain, other)) {
                selected = chain_index;
                overlap_key = other.key;
                break;
            }
        }
        if (selected) break;
    }
    if (!selected) selected = fallback;
    if (!selected) {
        throw std::runtime_error(
            "map has no complete Repeater chain with at least two Begin statements");
    }

    const repeater_linkage::Chain& chain = linkage.chains[*selected];
    Selection result;
    result.begin_distance = chain.begin_distance;
    result.end_distance = chain.end_distance;
    result.overlapping_key = std::move(overlap_key);
    result.targets.reserve(chain.begin_source_indices.size() + 1);
    for (size_t row_index : chain.begin_source_indices) {
        result.targets.push_back(target_from_row(snapshot, row_index));
    }
    result.targets.push_back(target_from_row(snapshot, *chain.end_source_index));
    result.original_key = result.targets.front().original_key;
    return result;
}

std::string unused_key(const repeater_linkage::Linkage& linkage,
                       const std::string& original_key) {
    std::set<std::string> used;
    for (const repeater_linkage::Chain& chain : linkage.chains) used.insert(chain.key);
    const std::string base = original_key + "__komapedit_headless";
    for (int suffix = 0; suffix < 10000; ++suffix) {
        const std::string candidate = suffix == 0
            ? base : base + "_" + std::to_string(suffix);
        if (!used.count(repeater_linkage::canonical_key(candidate))) return candidate;
    }
    throw std::runtime_error("could not generate a globally unused Repeater key");
}

std::vector<EditChange> build_changes(const Selection& selection,
                                      const std::string& key) {
    std::vector<EditChange> changes;
    changes.reserve(selection.targets.size());
    for (size_t index = 0; index < selection.targets.size(); ++index) {
        const Target& target = selection.targets[index];
        changes.push_back(typed_edit_headless::update(
            "headless-repeater-key-" + std::to_string(index), target.edit_id,
            target.expected_source_hash, {{"repeaterKey", key}}));
    }
    return changes;
}

bool has_error_prefix(const EditReport& report, const std::string& prefix) {
    return std::any_of(report.blocking_errors.begin(), report.blocking_errors.end(),
                       [&](const std::string& error) {
                           return error.rfind(prefix, 0) == 0;
                       });
}

bool snapshot_matches(void* handle, const Selection& selection,
                      const std::string& common_key, bool original_per_target) {
    const KvMapSnapshot snapshot = distance_batch_headless::current_map_snapshot(handle);
    for (const Target& target : selection.targets) {
        bool matched = false;
        for (std::uint64_t index = 0; index < snapshot.repeater_count; ++index) {
            const KvRepeaterRow& row = snapshot.repeaters[index];
            const std::string edit_id = distance_batch_headless::snapshot_text(
                snapshot, row.metadata.edit_id);
            if (edit_id != target.edit_id) continue;
            const std::string expected = original_per_target
                ? target.original_key : common_key;
            matched = distance_batch_headless::snapshot_value_text(
                snapshot, row.repeater_key) == expected;
            break;
        }
        if (!matched) return false;
    }
    return true;
}

bool reloaded_anchors_match(void* handle, const Selection& selection,
                            const std::string& common_key, bool original_per_target) {
    const KvMapSnapshot snapshot = distance_batch_headless::current_map_snapshot(handle);
    for (const Target& target : selection.targets) {
        bool matched = false;
        for (std::uint64_t index = 0; index < snapshot.repeater_count; ++index) {
            const KvRepeaterRow& row = snapshot.repeaters[index];
            if (row.metadata.line != target.source_line ||
                std::fabs(row.distance - target.distance) > 1e-8 ||
                distance_batch_headless::snapshot_text(snapshot, row.method) != target.method ||
                distance_batch_headless::metadata_source_file(snapshot, row.metadata) !=
                    target.source_file) {
                continue;
            }
            const std::string expected = original_per_target
                ? target.original_key : common_key;
            matched = distance_batch_headless::snapshot_value_text(
                snapshot, row.repeater_key) == expected;
            break;
        }
        if (!matched) return false;
    }
    return true;
}

std::vector<std::pair<std::string, std::string>> source_hashes(
    const KvMapSnapshot& snapshot, const Selection& selection) {
    std::set<std::string> target_files;
    for (const Target& target : selection.targets) target_files.insert(target.source_file);
    std::vector<std::pair<std::string, std::string>> result;
    for (std::uint64_t index = 0; index < snapshot.source_file_count; ++index) {
        const KvSourceFileRow& source = snapshot.source_files[index];
        const std::string path =
            distance_batch_headless::snapshot_text(snapshot, source.file_path);
        if (!target_files.count(path)) continue;
        result.emplace_back(path, distance_batch_headless::snapshot_text(
            snapshot, source.source_hash));
    }
    return result;
}

void write_result(std::ostream& out, const RunFacts& facts) {
    const auto boolean = [](bool value) { return value ? 1 : 0; };
    out << "command=debug-headless-repeater-key-edit\n"
        << "path=" << facts.path << "\n"
        << "commit_requested=" << boolean(facts.commit_requested) << "\n"
        << "original_key=" << facts.selection.original_key << "\n"
        << "new_key=" << facts.new_key << "\n"
        << "chain_begin_distance="
        << distance_batch_headless::edit_number(facts.selection.begin_distance) << "\n"
        << "chain_end_distance="
        << (facts.selection.end_distance
                ? distance_batch_headless::edit_number(*facts.selection.end_distance)
                : std::string("+inf")) << "\n"
        << "target_count=" << facts.selection.targets.size() << "\n"
        << "globally_unused_key=" << boolean(facts.globally_unused_key) << "\n"
        << "atomic_guard_ok=" << boolean(facts.atomic_guard_ok) << "\n"
        << "overlap_candidate_found=" << boolean(facts.overlap_candidate_found) << "\n"
        << "overlap_candidate_key=" << facts.selection.overlapping_key << "\n"
        << "overlap_guard_ok=" << boolean(facts.overlap_guard_ok) << "\n"
        << "dry_run_ok=" << boolean(facts.dry_run_ok) << "\n"
        << "dry_run_update_count=" << facts.dry_run_update_count << "\n"
        << "apply_to_memory_ok=" << boolean(facts.apply_ok) << "\n"
        << "apply_snapshot_ok=" << boolean(facts.apply_snapshot_ok) << "\n"
        << "non_target_changed_count=" << facts.non_target_changed_count << "\n"
        << "reset_ok=" << boolean(facts.reset_ok) << "\n"
        << "reset_snapshot_ok=" << boolean(facts.reset_snapshot_ok) << "\n"
        << "second_apply_ok=" << boolean(facts.second_apply_ok) << "\n"
        << "commit_attempted=" << boolean(facts.commit_attempted) << "\n"
        << "commit_ok=" << boolean(facts.commit_ok) << "\n"
        << "reload_ok=" << boolean(facts.reload_ok) << "\n";
    for (size_t index = 0; index < facts.selection.targets.size(); ++index) {
        const Target& target = facts.selection.targets[index];
        out << "target." << index << ".edit_id=" << target.edit_id << "\n"
            << "target." << index << ".method=" << target.method << "\n"
            << "target." << index << ".distance="
            << distance_batch_headless::edit_number(target.distance) << "\n"
            << "target." << index << ".source_file=" << target.source_file << "\n"
            << "target." << index << ".source_line=" << target.source_line << "\n";
    }
    for (size_t index = 0; index < facts.baseline_hashes.size(); ++index) {
        out << "baseline_source." << index << ".file="
            << facts.baseline_hashes[index].first << "\n"
            << "baseline_source." << index << ".hash="
            << facts.baseline_hashes[index].second << "\n";
    }
    for (size_t index = 0; index < facts.reload_hashes.size(); ++index) {
        out << "reload_source." << index << ".file="
            << facts.reload_hashes[index].first << "\n"
            << "reload_source." << index << ".hash="
            << facts.reload_hashes[index].second << "\n";
    }
    for (size_t index = 0; index < facts.changed_files.size(); ++index) {
        out << "changed_file." << index << '=' << facts.changed_files[index] << "\n";
    }
    out << "error=" << facts.error << "\n"
        << "result=" << (facts.passed() ? "PASS" : "FAIL") << "\n";
}

} // namespace repeater_key_edit_headless

int run_debug_headless_repeater_key_edit(const HeadlessRepeaterKeyEditOptions& options) {
    using namespace repeater_key_edit_headless;
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

    RunFacts facts;
    facts.path = options.path;
    facts.commit_requested = options.commit;
    try {
        MapHandle handle;
        handle.value = kv_load_map_ex(options.path.c_str(), options.unit_distance,
                                      KV_LOAD_EDIT_METADATA);
        if (!handle.value) {
            const char* error = kv_get_last_error();
            throw std::runtime_error(error ? error : "map load failed");
        }

        const KvMapSnapshot baseline =
            distance_batch_headless::current_map_snapshot(handle.value);
        const repeater_linkage::Linkage linkage = linkage_from_snapshot(baseline);
        facts.selection = select_chain(baseline, linkage);
        facts.baseline_hashes = source_hashes(baseline, facts.selection);
        facts.new_key = unused_key(linkage, facts.selection.original_key);
        facts.globally_unused_key = std::none_of(
            linkage.chains.begin(), linkage.chains.end(),
            [&](const repeater_linkage::Chain& chain) {
                return chain.key == repeater_linkage::canonical_key(facts.new_key);
            });
        if (!facts.globally_unused_key) {
            throw std::runtime_error("generated Repeater key is not globally unused");
        }

        const std::vector<EditChange> changes =
            build_changes(facts.selection, facts.new_key);
        const EditReport incomplete = typed_edit_headless::dry_run(
            handle.value, {changes.front()});
        facts.atomic_guard_ok = !incomplete.ok && has_error_prefix(
            incomplete, "Repeater key rename must update every statement in the chain");
        if (!facts.atomic_guard_ok) {
            throw std::runtime_error("Repeater key atomic-chain guard assertion failed");
        }

        facts.overlap_candidate_found = !facts.selection.overlapping_key.empty();
        if (facts.overlap_candidate_found) {
            const EditReport overlap = typed_edit_headless::dry_run(
                handle.value, build_changes(facts.selection,
                                            facts.selection.overlapping_key));
            facts.overlap_guard_ok = !overlap.ok && has_error_prefix(
                overlap, "Repeater key overlaps another Repeater interval");
            if (!facts.overlap_guard_ok) {
                throw std::runtime_error("Repeater key overlap guard assertion failed");
            }
        }

        const EditReport dry = typed_edit_headless::dry_run(handle.value, changes);
        facts.dry_run_ok = dry.ok && dry.full_reparse_ok &&
            dry.non_target_changed_count == 0;
        facts.dry_run_update_count = dry.update_count;
        if (!facts.dry_run_ok) {
            throw std::runtime_error(dry.blocking_errors.empty()
                ? "Repeater key dry-run assertions did not match"
                : dry.blocking_errors.front());
        }

        const EditReport apply = typed_edit_headless::apply_to_memory(handle.value, changes);
        facts.apply_ok = apply.ok && apply.full_reparse_ok;
        facts.non_target_changed_count = apply.non_target_changed_count;
        facts.apply_snapshot_ok = snapshot_matches(
            handle.value, facts.selection, facts.new_key, false);
        if (!facts.apply_ok || !facts.apply_snapshot_ok ||
            facts.non_target_changed_count != 0) {
            throw std::runtime_error(apply.blocking_errors.empty()
                ? "Repeater key memory Apply assertions did not match"
                : apply.blocking_errors.front());
        }

        facts.reset_ok = kv_edit_reset_memory(handle.value) != 0;
        facts.reset_snapshot_ok = facts.reset_ok && snapshot_matches(
            handle.value, facts.selection, std::string{}, true);
        if (!facts.reset_snapshot_ok) {
            const char* error = kv_get_last_error();
            throw std::runtime_error(error ? error : "Repeater key Reset assertions failed");
        }

        const EditReport second_apply =
            typed_edit_headless::apply_to_memory(handle.value, changes);
        facts.second_apply_ok = second_apply.ok && second_apply.full_reparse_ok &&
            second_apply.non_target_changed_count == 0 && snapshot_matches(
                handle.value, facts.selection, facts.new_key, false);
        if (!facts.second_apply_ok) {
            throw std::runtime_error(second_apply.blocking_errors.empty()
                ? "Repeater key second Apply assertions did not match"
                : second_apply.blocking_errors.front());
        }

        if (options.commit) {
            facts.commit_attempted = true;
            const EditReport committed = typed_edit_headless::commit(handle.value);
            facts.commit_ok = committed.ok && committed.full_reparse_ok &&
                committed.non_target_changed_count == 0;
            facts.changed_files = committed.changed_files;
            if (!facts.commit_ok) {
                throw std::runtime_error(committed.blocking_errors.empty()
                    ? "Repeater key commit assertions did not match"
                    : committed.blocking_errors.front());
            }
        }

        MapHandle reload;
        reload.value = kv_load_map_ex(options.path.c_str(), options.unit_distance,
                                      KV_LOAD_EDIT_METADATA);
        if (!reload.value) {
            const char* error = kv_get_last_error();
            throw std::runtime_error(error ? error : "Repeater key reload failed");
        }
        facts.reload_ok = reloaded_anchors_match(
            reload.value, facts.selection, facts.new_key, !options.commit);
        const KvMapSnapshot reloaded =
            distance_batch_headless::current_map_snapshot(reload.value);
        facts.reload_hashes = source_hashes(reloaded, facts.selection);
        if (!facts.reload_ok) {
            throw std::runtime_error("Reloaded Repeater source anchors did not match");
        }
    } catch (const std::exception& e) {
        facts.error = e.what();
    }

    write_result(*out, facts);
    out->flush();
    return facts.passed() ? 0 : 24;
}

namespace other_track_key_edit_headless {

using EditChange = typed_edit_headless::Change;
using EditReport = typed_edit_headless::Report;
using MapHandle = distance_batch_headless::MapHandle;

struct Target {
    std::string edit_id;
    std::string expected_source_hash;
    std::string source_file;
    std::string method;
    std::string original_key;
    int source_line = 0;
    double distance = 0.0;
};

struct Facts {
    std::string path;
    bool commit_requested = false;
    std::string original_key;
    std::string new_key;
    std::string duplicate_key;
    std::vector<Target> targets;
    bool production_gui_apply_ok = false;
    bool production_gui_reset_ok = false;
    bool inspector_track_key_read_only = false;
    bool globally_unused_key = false;
    bool atomic_guard_ok = false;
    bool duplicate_guard_ok = false;
    bool dry_run_ok = false;
    int dry_run_update_count = 0;
    bool apply_ok = false;
    bool apply_snapshot_ok = false;
    int non_target_changed_count = 0;
    size_t baseline_dependency_count = 0;
    bool dependencies_unchanged = false;
    bool reset_ok = false;
    bool reset_snapshot_ok = false;
    bool second_apply_ok = false;
    bool commit_attempted = false;
    bool commit_ok = false;
    bool reload_ok = false;
    std::vector<std::pair<std::string, std::string>> baseline_hashes;
    std::vector<std::pair<std::string, std::string>> reload_hashes;
    std::vector<std::string> changed_files;
    std::string error;

    bool passed() const {
        const bool commit_result = !commit_requested ||
            (commit_attempted && commit_ok && !changed_files.empty());
        return error.empty() && targets.size() >= 2 &&
            production_gui_apply_ok && production_gui_reset_ok &&
            inspector_track_key_read_only && globally_unused_key &&
            atomic_guard_ok && duplicate_guard_ok && dry_run_ok &&
            dry_run_update_count == static_cast<int>(targets.size()) &&
            apply_ok && apply_snapshot_ok && non_target_changed_count == 0 &&
            dependencies_unchanged && reset_ok && reset_snapshot_ok &&
            second_apply_ok && commit_result && reload_ok;
    }
};

std::string canonical_key(const KvMapSnapshot& snapshot, const KvValue& key) {
    const std::string prefix = key.kind == KV_VALUE_STRING ? "s:" :
        key.kind == KV_VALUE_NUMBER ? "n:" : "o:";
    return prefix + ascii_lower(
        distance_batch_headless::snapshot_value_text(snapshot, key));
}

Target target_from_row(const KvMapSnapshot& snapshot,
                       const KvOtherTrackChangeRow& row) {
    if (row.metadata.source_file_index == KV_INDEX_NONE ||
        row.metadata.source_file_index >= snapshot.source_file_count) {
        throw std::runtime_error(
            "other-track key target has no source file metadata");
    }
    const KvSourceFileRow& source =
        snapshot.source_files[row.metadata.source_file_index];
    Target target;
    target.edit_id = distance_batch_headless::snapshot_text(
        snapshot, row.metadata.edit_id);
    target.expected_source_hash = distance_batch_headless::snapshot_text(
        snapshot, source.source_hash);
    target.source_file = distance_batch_headless::snapshot_text(
        snapshot, source.file_path);
    target.method = distance_batch_headless::snapshot_text(snapshot, row.method);
    target.original_key =
        distance_batch_headless::snapshot_value_text(snapshot, row.track_key);
    target.source_line = row.metadata.line;
    target.distance = row.distance;
    if (target.edit_id.empty() || target.expected_source_hash.empty() ||
        target.source_file.empty()) {
        throw std::runtime_error(
            "other-track key target has incomplete edit metadata");
    }
    return target;
}

void select_track(const KvMapSnapshot& snapshot, Facts& facts) {
    std::map<std::string, std::vector<std::uint64_t>> groups;
    std::map<std::string, std::string> display_keys;
    for (std::uint64_t index = 0;
         index < snapshot.other_track_change_count; ++index) {
        const KvOtherTrackChangeRow& row = snapshot.other_track_changes[index];
        if (row.track_key.kind != KV_VALUE_STRING) continue;
        const std::string canonical = canonical_key(snapshot, row.track_key);
        groups[canonical].push_back(index);
        display_keys.emplace(
            canonical, distance_batch_headless::snapshot_value_text(
                           snapshot, row.track_key));
    }
    auto selected = std::find_if(
        groups.begin(), groups.end(),
        [](const auto& group) { return group.second.size() >= 2; });
    if (selected == groups.end()) {
        throw std::runtime_error(
            "map has no string-key other track with at least two Track statements");
    }
    facts.original_key = display_keys.at(selected->first);
    for (std::uint64_t row_index : selected->second) {
        facts.targets.push_back(target_from_row(
            snapshot, snapshot.other_track_changes[row_index]));
    }
    const auto duplicate = std::find_if(
        groups.begin(), groups.end(),
        [&](const auto& group) { return group.first != selected->first; });
    if (duplicate == groups.end()) {
        throw std::runtime_error(
            "map has no second string-key other track for duplicate detection");
    }
    facts.duplicate_key = display_keys.at(duplicate->first);

    const std::string base = facts.original_key + "__komapedit_headless";
    for (int suffix = 0; suffix < 10000; ++suffix) {
        const std::string candidate = suffix == 0
            ? base : base + "_" + std::to_string(suffix);
        if (!groups.count("s:" + ascii_lower(candidate))) {
            facts.new_key = candidate;
            facts.globally_unused_key = true;
            break;
        }
    }
    if (!facts.globally_unused_key) {
        throw std::runtime_error(
            "could not generate a globally unused other-track key");
    }
}

std::vector<EditChange> build_changes(const Facts& facts,
                                      const std::string& key) {
    std::vector<EditChange> changes;
    changes.reserve(facts.targets.size());
    for (size_t index = 0; index < facts.targets.size(); ++index) {
        const Target& target = facts.targets[index];
        changes.push_back(typed_edit_headless::update(
            "headless-other-track-key-" + std::to_string(index),
            target.edit_id, target.expected_source_hash,
            {{"trackKey", key}}));
    }
    return changes;
}

bool has_error_prefix(const EditReport& report, std::string_view prefix) {
    return std::any_of(
        report.blocking_errors.begin(), report.blocking_errors.end(),
        [&](const std::string& error) { return error.rfind(prefix, 0) == 0; });
}

bool snapshot_matches(void* handle, const Facts& facts,
                      const std::string& key) {
    const KvMapSnapshot snapshot =
        distance_batch_headless::current_map_snapshot(handle);
    for (const Target& target : facts.targets) {
        bool matched = false;
        for (std::uint64_t index = 0;
             index < snapshot.other_track_change_count; ++index) {
            const KvOtherTrackChangeRow& row =
                snapshot.other_track_changes[index];
            if (distance_batch_headless::snapshot_text(
                    snapshot, row.metadata.edit_id) != target.edit_id) {
                continue;
            }
            matched = row.track_key.kind == KV_VALUE_STRING &&
                distance_batch_headless::snapshot_value_text(
                    snapshot, row.track_key) == key;
            break;
        }
        if (!matched) return false;
    }
    return true;
}

size_t dependency_count(const KvMapSnapshot& snapshot,
                        const std::string& key) {
    auto matches = [&](const KvValue& value) {
        return value.kind == KV_VALUE_STRING &&
            ascii_lower(distance_batch_headless::snapshot_value_text(
                snapshot, value)) == ascii_lower(key);
    };
    size_t count = 0;
    for (std::uint64_t i = 0; i < snapshot.structure_put_count; ++i) {
        count += matches(snapshot.structure_puts[i].track_key) ? 1 : 0;
    }
    for (std::uint64_t i = 0; i < snapshot.structure_between_count; ++i) {
        count += matches(snapshot.structure_betweens[i].track_key1) ? 1 : 0;
        count += matches(snapshot.structure_betweens[i].track_key2) ? 1 : 0;
    }
    for (std::uint64_t i = 0; i < snapshot.signal_put_count; ++i) {
        count += matches(snapshot.signal_puts[i].track_key) ? 1 : 0;
    }
    for (std::uint64_t i = 0; i < snapshot.repeater_count; ++i) {
        count += matches(snapshot.repeaters[i].track_key) ? 1 : 0;
    }
    for (std::uint64_t i = 0; i < snapshot.other_train_definition_count; ++i) {
        count += matches(snapshot.other_train_definitions[i].track_key) ? 1 : 0;
    }
    return count;
}

std::vector<std::pair<std::string, std::string>> source_hashes(
    const KvMapSnapshot& snapshot, const Facts& facts) {
    std::set<std::string> files;
    for (const Target& target : facts.targets) files.insert(target.source_file);
    std::vector<std::pair<std::string, std::string>> hashes;
    for (std::uint64_t index = 0; index < snapshot.source_file_count; ++index) {
        const KvSourceFileRow& source = snapshot.source_files[index];
        const std::string path = distance_batch_headless::snapshot_text(
            snapshot, source.file_path);
        if (!files.count(path)) continue;
        hashes.emplace_back(
            path, distance_batch_headless::snapshot_text(
                      snapshot, source.source_hash));
    }
    return hashes;
}

void write_result(std::ostream& out, const Facts& facts) {
    const auto boolean = [](bool value) { return value ? 1 : 0; };
    out << "command=debug-headless-other-track-key-edit\n"
        << "path=" << facts.path << "\n"
        << "commit_requested=" << boolean(facts.commit_requested) << "\n"
        << "original_key=" << facts.original_key << "\n"
        << "new_key=" << facts.new_key << "\n"
        << "duplicate_candidate_key=" << facts.duplicate_key << "\n"
        << "target_count=" << facts.targets.size() << "\n"
        << "production_gui_apply_ok="
        << boolean(facts.production_gui_apply_ok) << "\n"
        << "production_gui_reset_ok="
        << boolean(facts.production_gui_reset_ok) << "\n"
        << "inspector_track_key_read_only="
        << boolean(facts.inspector_track_key_read_only) << "\n"
        << "globally_unused_key=" << boolean(facts.globally_unused_key) << "\n"
        << "atomic_guard_ok=" << boolean(facts.atomic_guard_ok) << "\n"
        << "duplicate_guard_ok=" << boolean(facts.duplicate_guard_ok) << "\n"
        << "dry_run_ok=" << boolean(facts.dry_run_ok) << "\n"
        << "dry_run_update_count=" << facts.dry_run_update_count << "\n"
        << "apply_to_memory_ok=" << boolean(facts.apply_ok) << "\n"
        << "apply_snapshot_ok=" << boolean(facts.apply_snapshot_ok) << "\n"
        << "non_target_changed_count=" << facts.non_target_changed_count << "\n"
        << "baseline_dependency_count=" << facts.baseline_dependency_count << "\n"
        << "dependencies_unchanged=" << boolean(facts.dependencies_unchanged) << "\n"
        << "reset_ok=" << boolean(facts.reset_ok) << "\n"
        << "reset_snapshot_ok=" << boolean(facts.reset_snapshot_ok) << "\n"
        << "second_apply_ok=" << boolean(facts.second_apply_ok) << "\n"
        << "commit_attempted=" << boolean(facts.commit_attempted) << "\n"
        << "commit_ok=" << boolean(facts.commit_ok) << "\n"
        << "reload_ok=" << boolean(facts.reload_ok) << "\n";
    for (size_t index = 0; index < facts.targets.size(); ++index) {
        const Target& target = facts.targets[index];
        out << "target." << index << ".edit_id=" << target.edit_id << "\n"
            << "target." << index << ".method=" << target.method << "\n"
            << "target." << index << ".distance="
            << distance_batch_headless::edit_number(target.distance) << "\n"
            << "target." << index << ".source_file=" << target.source_file << "\n"
            << "target." << index << ".source_line=" << target.source_line << "\n";
    }
    for (size_t index = 0; index < facts.baseline_hashes.size(); ++index) {
        out << "baseline_source." << index << ".file="
            << facts.baseline_hashes[index].first << "\n"
            << "baseline_source." << index << ".hash="
            << facts.baseline_hashes[index].second << "\n";
    }
    for (size_t index = 0; index < facts.reload_hashes.size(); ++index) {
        out << "reload_source." << index << ".file="
            << facts.reload_hashes[index].first << "\n"
            << "reload_source." << index << ".hash="
            << facts.reload_hashes[index].second << "\n";
    }
    for (size_t index = 0; index < facts.changed_files.size(); ++index) {
        out << "changed_file." << index << '=' << facts.changed_files[index] << "\n";
    }
    out << "error=" << facts.error << "\n"
        << "result=" << (facts.passed() ? "PASS" : "FAIL") << "\n";
}

} // namespace other_track_key_edit_headless

int App::run_debug_headless_other_track_key_edit(
    const HeadlessOtherTrackKeyEditOptions& options) {
    using namespace other_track_key_edit_headless;
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
    facts.path = options.path;
    facts.commit_requested = options.commit;
    try {
        MapHandle handle;
        handle.value = kv_load_map_ex(options.path.c_str(), options.unit_distance,
                                      KV_LOAD_EDIT_METADATA);
        if (!handle.value) {
            const char* error = kv_get_last_error();
            throw std::runtime_error(error ? error : "map load failed");
        }
        const KvMapSnapshot baseline =
            distance_batch_headless::current_map_snapshot(handle.value);
        select_track(baseline, facts);
        facts.baseline_hashes = source_hashes(baseline, facts);
        facts.baseline_dependency_count =
            dependency_count(baseline, facts.original_key);

        LoadResult production_load = load_map_worker(
            options.path, options.unit_distance, false, 0.0, 0.0,
            options.unit_distance, LoadModelOptions{true});
        if (!production_load.ok) {
            if (production_load.handle) kv_free(production_load.handle);
            throw std::runtime_error(production_load.error.empty()
                ? "production GUI-path map load failed"
                : production_load.error);
        }
        ImGui::CreateContext();
        ImPlot::CreateContext();
        ImGui::GetIO().IniFilename = nullptr;
        try {
            UserSettings settings;
            settings.language = Language::En;
            App app(nullptr, settings, 1.0f, false, false);
            app.handle_ = production_load.handle;
            production_load.handle = nullptr;
            app.model_ = std::move(production_load.model);
            app.file_path_ = options.path;
            app.has_model_ = true;
            app.edit_mode_enabled_ = true;
            app.edit_registry_loaded_ = true;
            app.edit_memory_matches_pending_ledger_ = true;
            app.dmin_ = app.model_.default_min;
            app.dmax_ = app.model_.default_max;
            app.unit_distance_ = options.unit_distance;

            const auto source_row = std::find_if(
                app.model_.other_track_changes.begin(),
                app.model_.other_track_changes.end(),
                [&](const TableRow& row) {
                    return row.edit_id == facts.targets.front().edit_id;
                });
            if (source_row == app.model_.other_track_changes.end()) {
                throw std::runtime_error(
                    "production GUI path could not resolve selected other track");
            }
            app.open_element_inspector(
                MapElementInspectorRequest{
                    source_row->edit_id, "otherTrack.change"});
            const auto track_key_field = std::find_if(
                app.inspector_.fields.begin(), app.inspector_.fields.end(),
                [](const MapElementEditFieldState& field) {
                    return field.key == "trackKey";
                });
            facts.inspector_track_key_read_only =
                track_key_field != app.inspector_.fields.end() &&
                track_key_field->read_only;

            app.other_track_rename_.source_key =
                table_cell(*source_row, "trackKey");
            app.other_track_rename_.apply_key = facts.new_key;
            facts.production_gui_apply_ok = app.apply_other_track_rename() &&
                app.pending_edit_changes_.size() == facts.targets.size();
            if (facts.production_gui_apply_ok) {
                const std::string expected_key = "'" + facts.new_key + "'";
                for (const Target& target : facts.targets) {
                    const auto row = std::find_if(
                        app.model_.other_track_changes.begin(),
                        app.model_.other_track_changes.end(),
                        [&](const TableRow& candidate) {
                            return candidate.edit_id == target.edit_id;
                        });
                    facts.production_gui_apply_ok =
                        facts.production_gui_apply_ok &&
                        row != app.model_.other_track_changes.end() &&
                        normalize_track_lookup_key(
                            table_cell(*row, "trackKey")) ==
                            normalize_track_lookup_key(expected_key);
                }
            }
            facts.production_gui_reset_ok =
                facts.production_gui_apply_ok &&
                app.apply_edit_ledger_to_preview({}, std::nullopt, false) &&
                app.pending_edit_changes_.empty();
            if (!facts.production_gui_apply_ok ||
                !facts.production_gui_reset_ok ||
                !facts.inspector_track_key_read_only) {
                throw std::runtime_error(
                    "production GUI other-track rename path assertions failed");
            }
        } catch (...) {
            ImPlot::DestroyContext();
            ImGui::DestroyContext();
            if (production_load.handle) kv_free(production_load.handle);
            throw;
        }
        ImPlot::DestroyContext();
        ImGui::DestroyContext();

        const std::vector<EditChange> changes =
            build_changes(facts, facts.new_key);
        const EditReport incomplete = typed_edit_headless::dry_run(
            handle.value, {changes.front()});
        facts.atomic_guard_ok = !incomplete.ok && has_error_prefix(
            incomplete,
            "Other-track key rename must update every statement in the track");
        if (!facts.atomic_guard_ok) {
            throw std::runtime_error(
                "other-track key atomic-track guard assertion failed");
        }

        const EditReport duplicate = typed_edit_headless::dry_run(
            handle.value, build_changes(
                facts, "'" + facts.duplicate_key + "'"));
        facts.duplicate_guard_ok = !duplicate.ok && has_error_prefix(
            duplicate, "Other-track key already exists in map");
        if (!facts.duplicate_guard_ok) {
            throw std::runtime_error(
                "other-track duplicate-key guard assertion failed");
        }

        const EditReport dry =
            typed_edit_headless::dry_run(handle.value, changes);
        facts.dry_run_ok = dry.ok && dry.full_reparse_ok &&
            dry.non_target_changed_count == 0;
        facts.dry_run_update_count = dry.update_count;
        if (!facts.dry_run_ok) {
            throw std::runtime_error(dry.blocking_errors.empty()
                ? "other-track key dry-run assertions did not match"
                : dry.blocking_errors.front());
        }

        const EditReport applied =
            typed_edit_headless::apply_to_memory(handle.value, changes);
        facts.apply_ok = applied.ok && applied.full_reparse_ok;
        facts.non_target_changed_count = applied.non_target_changed_count;
        facts.apply_snapshot_ok = snapshot_matches(
            handle.value, facts, facts.new_key);
        const KvMapSnapshot applied_snapshot =
            distance_batch_headless::current_map_snapshot(handle.value);
        facts.dependencies_unchanged =
            dependency_count(applied_snapshot, facts.original_key) ==
            facts.baseline_dependency_count;
        if (!facts.apply_ok || !facts.apply_snapshot_ok ||
            facts.non_target_changed_count != 0 ||
            !facts.dependencies_unchanged) {
            throw std::runtime_error(applied.blocking_errors.empty()
                ? "other-track key memory Apply assertions did not match"
                : applied.blocking_errors.front());
        }

        facts.reset_ok = kv_edit_reset_memory(handle.value) != 0;
        facts.reset_snapshot_ok = facts.reset_ok && snapshot_matches(
            handle.value, facts, facts.original_key);
        if (!facts.reset_snapshot_ok) {
            const char* error = kv_get_last_error();
            throw std::runtime_error(error
                ? error : "other-track key Reset assertions failed");
        }

        const EditReport second_apply =
            typed_edit_headless::apply_to_memory(handle.value, changes);
        facts.second_apply_ok = second_apply.ok &&
            second_apply.full_reparse_ok &&
            second_apply.non_target_changed_count == 0 &&
            snapshot_matches(handle.value, facts, facts.new_key);
        if (!facts.second_apply_ok) {
            throw std::runtime_error(second_apply.blocking_errors.empty()
                ? "other-track key second Apply assertions did not match"
                : second_apply.blocking_errors.front());
        }

        if (options.commit) {
            facts.commit_attempted = true;
            const EditReport committed =
                typed_edit_headless::commit(handle.value);
            facts.commit_ok = committed.ok && committed.full_reparse_ok &&
                committed.non_target_changed_count == 0;
            facts.changed_files = committed.changed_files;
            if (!facts.commit_ok) {
                throw std::runtime_error(committed.blocking_errors.empty()
                    ? "other-track key commit assertions did not match"
                    : committed.blocking_errors.front());
            }
        }

        MapHandle reload;
        reload.value = kv_load_map_ex(options.path.c_str(), options.unit_distance,
                                      KV_LOAD_EDIT_METADATA);
        if (!reload.value) {
            const char* error = kv_get_last_error();
            throw std::runtime_error(error
                ? error : "other-track key reload failed");
        }
        facts.reload_ok = snapshot_matches(
            reload.value, facts,
            options.commit ? facts.new_key : facts.original_key);
        const KvMapSnapshot reload_snapshot =
            distance_batch_headless::current_map_snapshot(reload.value);
        facts.reload_hashes = source_hashes(reload_snapshot, facts);
        if (!facts.reload_ok) {
            throw std::runtime_error(
                "reloaded other-track source anchors did not match");
        }
    } catch (const std::exception& e) {
        facts.error = e.what();
    }

    write_result(*out, facts);
    out->flush();
    return facts.passed() ? 0 : 25;
}

namespace section_edit_batch_headless {

using EditChange = typed_edit_headless::Change;
using EditField = typed_edit_headless::Field;
using EditReport = typed_edit_headless::Report;
using MapHandle = distance_batch_headless::MapHandle;

struct SectionRowEdit {
    std::string edit_id;
    std::string row_kind;
    std::string source_file;
    std::string expected_source_hash;
    int source_line = 0;
    std::vector<std::string> raw_args;
    std::vector<double> values;
    double distance = 0.0;
};

struct BatchRunFacts {
    std::string path;
    bool commit_requested = false;
    size_t section_row_count = 0;
    size_t selected_value_count = 0;
    bool single_value_edit_ok = false;
    bool raw_argument_preserved = false;
    bool count_reduction_ok = false;
    bool count_increase_ok = false;
    bool delete_ok = false;
    bool reset_ok = false;
    bool snapshot_restored = false;
    bool commit_attempted = false;
    bool commit_ok = false;
    size_t commit_changed_file_count = 0;
    bool committed_value_written = false;
    bool committed_raw_preserved = false;
    std::string selected_edit_id;
    std::string selected_row_kind;
    std::string selected_source_file;
    int selected_source_line = 0;
    std::string error;

    bool passed() const {
        return error.empty() && section_row_count >= 1 && selected_value_count >= 2 &&
            single_value_edit_ok && raw_argument_preserved && count_reduction_ok &&
            count_increase_ok && delete_ok && reset_ok && snapshot_restored &&
            (!commit_requested || (commit_attempted && commit_ok &&
                                   committed_value_written && committed_raw_preserved));
    }
};

std::vector<std::string> split_arguments(const std::string& raw) {
    std::vector<std::string> args;
    size_t begin = 0;
    for (size_t index = 0; index <= raw.size(); ++index) {
        if (index != raw.size() && raw[index] != ',') continue;
        std::string field = raw.substr(begin, index - begin);
        const size_t start = field.find_first_not_of(" \t");
        const size_t end = field.find_last_not_of(" \t");
        args.push_back(start == std::string::npos
                           ? std::string{}
                           : field.substr(start, end - start + 1));
        begin = index + 1;
    }
    return args;
}

bool is_numeric_argument(const std::string& text) {
    if (text.empty()) return false;
    char* end = nullptr;
    std::strtod(text.c_str(), &end);
    return end && *end == '\0';
}

std::vector<SectionRowEdit> collect_section_rows(void* handle) {
    const KvMapSnapshot snapshot = distance_batch_headless::current_map_snapshot(handle);
    std::vector<SectionRowEdit> result;
    auto collect = [&](const KvSectionRow* rows, std::uint64_t count,
                       const char* row_kind) {
        if (count != 0 && !rows) {
            throw std::runtime_error(std::string("typed map snapshot has null ") +
                                     row_kind + " array");
        }
        for (std::uint64_t index = 0; index < count; ++index) {
            const KvSectionRow& row = rows[index];
            SectionRowEdit edit;
            edit.edit_id = distance_batch_headless::snapshot_text(
                snapshot, row.metadata.edit_id);
            edit.row_kind = row_kind;
            edit.source_line = row.metadata.line;
            edit.distance = row.distance;
            if (edit.edit_id.empty()) continue;
            if (row.values.offset > snapshot.value_count ||
                row.values.count > snapshot.value_count - row.values.offset ||
                (row.values.count != 0 && !snapshot.values)) {
                throw std::runtime_error("typed map snapshot Section value span is out of range");
            }
            for (std::uint64_t value_index = 0; value_index < row.values.count;
                 ++value_index) {
                const KvValue& value = snapshot.values[row.values.offset + value_index];
                edit.values.push_back(value.kind == KV_VALUE_NUMBER
                    ? value.number_value
                    : std::numeric_limits<double>::quiet_NaN());
            }
            std::string metadata_error;
            const std::optional<InspectorTargetMetadata> info =
                resolve_inspector_target_metadata(handle, edit.edit_id, row_kind,
                                                  &metadata_error);
            if (!info || info->expected_source_hash.empty()) continue;
            edit.expected_source_hash = info->expected_source_hash;
            edit.source_file = info->source.file_path;
            edit.raw_args = split_arguments(info->raw_arguments);
            result.push_back(std::move(edit));
        }
    };
    collect(snapshot.section_begins, snapshot.section_begin_count, "section.begin");
    collect(snapshot.section_speed_limits, snapshot.section_speed_limit_count,
            "section.speedLimit");
    std::stable_sort(result.begin(), result.end(), [](const SectionRowEdit& left,
                                                      const SectionRowEdit& right) {
        if (left.distance != right.distance) return left.distance < right.distance;
        if (left.row_kind != right.row_kind) return left.row_kind < right.row_kind;
        if (left.source_file != right.source_file) {
            return left.source_file < right.source_file;
        }
        return left.source_line < right.source_line;
    });
    return result;
}

EditChange single_value_change(const SectionRowEdit& edit, size_t index,
                               double new_value, const std::string& change_id) {
    return typed_edit_headless::update(
        change_id, edit.edit_id, edit.expected_source_hash,
        {{"values." + std::to_string(index),
          distance_batch_headless::edit_number(new_value)}});
}

EditChange count_change(const SectionRowEdit& edit,
                        const std::vector<double>& values,
                        const std::string& change_id) {
    std::vector<EditField> fields;
    fields.reserve(values.size() + 1);
    fields.push_back({"values.count", std::to_string(values.size())});
    for (size_t index = 0; index < values.size(); ++index) {
        fields.push_back({"values." + std::to_string(index),
                          distance_batch_headless::edit_number(values[index])});
    }
    return typed_edit_headless::update(change_id, edit.edit_id,
                                       edit.expected_source_hash,
                                       std::move(fields));
}

EditChange delete_change(const SectionRowEdit& edit, const std::string& change_id) {
    EditChange change;
    change.change_id = change_id;
    change.edit_id = edit.edit_id;
    change.operation = KV_EDIT_DELETE;
    change.expected_source_hash = edit.expected_source_hash;
    return change;
}

bool update_report_ok(const EditReport& report, int expected_updates) {
    return report.ok && report.full_reparse_ok && report.resolution_requests.empty() &&
        report.update_count == expected_updates && report.delete_count == 0 &&
        report.non_target_changed_count == 0 && report.target_distance_match_count == 0;
}

bool delete_report_ok(const EditReport& report) {
    return report.ok && report.full_reparse_ok && report.resolution_requests.empty() &&
        report.update_count == 0 && report.delete_count == 1 &&
        report.non_target_changed_count == 0 && report.target_distance_match_count == 0;
}

struct SnapshotSectionValues {
    bool found = false;
    size_t value_count = 0;
    std::vector<double> values;
};

SnapshotSectionValues snapshot_section_row(void* handle, const std::string& edit_id) {
    const KvMapSnapshot snapshot = distance_batch_headless::current_map_snapshot(handle);
    SnapshotSectionValues out;
    auto scan = [&](const KvSectionRow* rows, std::uint64_t count) {
        if (out.found) return;
        if (count != 0 && !rows) {
            throw std::runtime_error("typed map snapshot has null Section array");
        }
        for (std::uint64_t index = 0; index < count; ++index) {
            if (distance_batch_headless::snapshot_text(
                    snapshot, rows[index].metadata.edit_id) != edit_id) {
                continue;
            }
            if (rows[index].values.offset > snapshot.value_count ||
                rows[index].values.count > snapshot.value_count - rows[index].values.offset ||
                (rows[index].values.count != 0 && !snapshot.values)) {
                throw std::runtime_error(
                    "typed map snapshot Section value span is out of range");
            }
            out.found = true;
            out.value_count = static_cast<size_t>(rows[index].values.count);
            out.values.reserve(out.value_count);
            for (std::uint64_t value_index = 0; value_index < rows[index].values.count;
                 ++value_index) {
                const KvValue& value =
                    snapshot.values[rows[index].values.offset + value_index];
                out.values.push_back(value.kind == KV_VALUE_NUMBER
                    ? value.number_value
                    : std::numeric_limits<double>::quiet_NaN());
            }
            return;
        }
    };
    scan(snapshot.section_begins, snapshot.section_begin_count);
    scan(snapshot.section_speed_limits, snapshot.section_speed_limit_count);
    return out;
}

bool values_match(const std::vector<double>& expected,
                  const std::vector<double>& actual) {
    if (expected.size() != actual.size()) return false;
    for (size_t index = 0; index < expected.size(); ++index) {
        if (std::isnan(expected[index]) || std::isnan(actual[index])) {
            if (!std::isnan(expected[index]) || !std::isnan(actual[index])) return false;
            continue;
        }
        if (std::fabs(expected[index] - actual[index]) > 1e-9) return false;
    }
    return true;
}

void write_batch_result(std::ostream& out, const BatchRunFacts& facts) {
    auto boolean = [](bool value) { return value ? 1 : 0; };
    out << "command=debug-headless-section-edit-batch\n"
        << "path=" << facts.path << "\n"
        << "commit_requested=" << boolean(facts.commit_requested) << "\n"
        << "section_row_count=" << facts.section_row_count << "\n"
        << "selected_edit_id=" << facts.selected_edit_id << "\n"
        << "selected_row_kind=" << facts.selected_row_kind << "\n"
        << "selected_source_file=" << facts.selected_source_file << "\n"
        << "selected_source_line=" << facts.selected_source_line << "\n"
        << "selected_value_count=" << facts.selected_value_count << "\n"
        << "single_value_edit_ok=" << boolean(facts.single_value_edit_ok) << "\n"
        << "raw_argument_preserved=" << boolean(facts.raw_argument_preserved) << "\n"
        << "count_reduction_ok=" << boolean(facts.count_reduction_ok) << "\n"
        << "count_increase_ok=" << boolean(facts.count_increase_ok) << "\n"
        << "delete_ok=" << boolean(facts.delete_ok) << "\n"
        << "reset_ok=" << boolean(facts.reset_ok) << "\n"
        << "snapshot_restored=" << boolean(facts.snapshot_restored) << "\n"
        << "commit_attempted=" << boolean(facts.commit_attempted) << "\n"
        << "commit_ok=" << boolean(facts.commit_ok) << "\n"
        << "commit_changed_file_count=" << facts.commit_changed_file_count << "\n"
        << "committed_value_written=" << boolean(facts.committed_value_written) << "\n"
        << "committed_raw_preserved=" << boolean(facts.committed_raw_preserved) << "\n"
        << "error=" << facts.error << "\n"
        << "result=" << (facts.passed() ? "PASS" : "FAIL") << "\n";
}

} // namespace section_edit_batch_headless

int run_debug_headless_section_edit_batch(const HeadlessSectionEditBatchOptions& options) {
    using namespace section_edit_batch_headless;
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

        const std::vector<SectionRowEdit> rows = collect_section_rows(handle.value);
        if (rows.empty()) {
            throw std::runtime_error(
                "real map has no editable Section statements");
        }
        facts.section_row_count = rows.size();
        const std::string original_fingerprint =
            distance_batch_headless::source_snapshot_fingerprint(handle.value);
        const SectionRowEdit& edit = rows.front();
        facts.selected_edit_id = edit.edit_id;
        facts.selected_row_kind = edit.row_kind;
        facts.selected_source_file = edit.source_file;
        facts.selected_source_line = edit.source_line;
        facts.selected_value_count = edit.values.size();
        if (edit.values.size() < 2 || edit.source_file.empty()) {
            throw std::runtime_error(
                "selected Section row must have at least two parameters in a known source file");
        }
        const std::string baseline_bytes = distance_batch_headless::read_fixture_file(
            utf8_to_wide(edit.source_file));
        const std::string expression_argument = [&]() {
            for (const std::string& arg : edit.raw_args) {
                if (!is_numeric_argument(arg)) return arg;
            }
            return std::string{};
        }();

        const double original_first = std::isfinite(edit.values[0])
            ? edit.values[0] : 1.0;
        const double edited_first = original_first + 1.0;
        const std::vector<EditChange> single_change = {
            single_value_change(edit, 0, edited_first,
                                "headless-section-single-value")};
        const EditReport single_dry = typed_edit_headless::dry_run(handle.value, single_change);
        facts.single_value_edit_ok = update_report_ok(single_dry, 1);
        if (!facts.single_value_edit_ok) {
            throw std::runtime_error(single_dry.blocking_errors.empty()
                ? "Section single-value dry-run assertions failed"
                : single_dry.blocking_errors.front());
        }
        facts.raw_argument_preserved = expression_argument.empty() ||
            std::any_of(single_dry.previews.begin(), single_dry.previews.end(),
                        [&](const std::string& preview) {
                            return preview.find(expression_argument) != std::string::npos;
                        });
        const EditReport single_applied =
            typed_edit_headless::apply_to_memory(handle.value, single_change);
        if (!update_report_ok(single_applied, 1)) {
            throw std::runtime_error(single_applied.blocking_errors.empty()
                ? "Section single-value apply-to-memory assertions failed"
                : single_applied.blocking_errors.front());
        }
        {
            const SnapshotSectionValues current =
                snapshot_section_row(handle.value, edit.edit_id);
            std::vector<double> expected = edit.values;
            expected[0] = edited_first;
            if (!current.found || current.value_count != edit.values.size() ||
                !values_match(expected, current.values)) {
                throw std::runtime_error(
                    "Section single-value snapshot assertion failed");
            }
        }
        if (!kv_edit_reset_memory(handle.value)) {
            const char* error = kv_get_last_error();
            throw std::runtime_error(error ? error : "kv_edit_reset_memory failed");
        }

        const std::vector<double> reduced(edit.values.begin(),
                                          edit.values.end() - 1);
        const std::vector<EditChange> reduce_change = {
            count_change(edit, reduced, "headless-section-count-reduce")};
        const EditReport reduce_dry = typed_edit_headless::dry_run(handle.value, reduce_change);
        facts.count_reduction_ok = update_report_ok(reduce_dry, 1);
        if (!facts.count_reduction_ok) {
            throw std::runtime_error(reduce_dry.blocking_errors.empty()
                ? "Section count-reduction dry-run assertions failed"
                : reduce_dry.blocking_errors.front());
        }
        const EditReport reduce_applied =
            typed_edit_headless::apply_to_memory(handle.value, reduce_change);
        if (!update_report_ok(reduce_applied, 1)) {
            throw std::runtime_error(reduce_applied.blocking_errors.empty()
                ? "Section count-reduction apply-to-memory assertions failed"
                : reduce_applied.blocking_errors.front());
        }
        {
            const SnapshotSectionValues current =
                snapshot_section_row(handle.value, edit.edit_id);
            if (!current.found || current.value_count != reduced.size() ||
                !values_match(reduced, current.values)) {
                throw std::runtime_error(
                    "Section count-reduction snapshot assertion failed");
            }
        }
        if (!kv_edit_reset_memory(handle.value)) {
            const char* error = kv_get_last_error();
            throw std::runtime_error(error ? error : "kv_edit_reset_memory failed");
        }

        std::vector<double> increased = edit.values;
        increased.push_back(30.0);
        const std::vector<EditChange> increase_change = {
            count_change(edit, increased, "headless-section-count-increase")};
        const EditReport increase_dry = typed_edit_headless::dry_run(handle.value, increase_change);
        facts.count_increase_ok = update_report_ok(increase_dry, 1);
        if (!facts.count_increase_ok) {
            throw std::runtime_error(increase_dry.blocking_errors.empty()
                ? "Section count-increase dry-run assertions failed"
                : increase_dry.blocking_errors.front());
        }
        const EditReport increase_applied =
            typed_edit_headless::apply_to_memory(handle.value, increase_change);
        if (!update_report_ok(increase_applied, 1)) {
            throw std::runtime_error(increase_applied.blocking_errors.empty()
                ? "Section count-increase apply-to-memory assertions failed"
                : increase_applied.blocking_errors.front());
        }
        {
            const SnapshotSectionValues current =
                snapshot_section_row(handle.value, edit.edit_id);
            if (!current.found || current.value_count != increased.size() ||
                !values_match(increased, current.values)) {
                throw std::runtime_error(
                    "Section count-increase snapshot assertion failed");
            }
        }
        if (!kv_edit_reset_memory(handle.value)) {
            const char* error = kv_get_last_error();
            throw std::runtime_error(error ? error : "kv_edit_reset_memory failed");
        }

        const std::vector<EditChange> delete_changes = {
            delete_change(edit, "headless-section-delete")};
        const EditReport delete_dry = typed_edit_headless::dry_run(handle.value, delete_changes);
        facts.delete_ok = delete_report_ok(delete_dry);
        if (!facts.delete_ok) {
            throw std::runtime_error(delete_dry.blocking_errors.empty()
                ? "Section delete dry-run assertions failed"
                : delete_dry.blocking_errors.front());
        }
        const EditReport delete_applied =
            typed_edit_headless::apply_to_memory(handle.value, delete_changes);
        if (!delete_report_ok(delete_applied)) {
            throw std::runtime_error(delete_applied.blocking_errors.empty()
                ? "Section delete apply-to-memory assertions failed"
                : delete_applied.blocking_errors.front());
        }
        if (snapshot_section_row(handle.value, edit.edit_id).found) {
            throw std::runtime_error(
                "Section delete snapshot still contains the deleted row");
        }

        const std::string initial_fingerprint =
            distance_batch_headless::source_snapshot_fingerprint(handle.value);
        facts.reset_ok = kv_edit_reset_memory(handle.value) != 0;
        if (!facts.reset_ok) {
            const char* error = kv_get_last_error();
            throw std::runtime_error(error ? error : "kv_edit_reset_memory failed");
        }
        facts.snapshot_restored =
            distance_batch_headless::source_snapshot_fingerprint(handle.value) ==
            original_fingerprint && initial_fingerprint != original_fingerprint;
        if (!facts.snapshot_restored) {
            throw std::runtime_error(
                "Section reset did not restore the baseline working copy");
        }

        if (options.commit) {
            facts.commit_attempted = true;
            const EditReport commit_apply =
                typed_edit_headless::apply_to_memory(handle.value, single_change);
            if (!update_report_ok(commit_apply, 1)) {
                throw std::runtime_error(commit_apply.blocking_errors.empty()
                    ? "Section pre-commit apply-to-memory assertions failed"
                    : commit_apply.blocking_errors.front());
            }
            const EditReport commit_report = typed_edit_headless::commit(handle.value);
            facts.commit_ok = commit_report.ok && commit_report.full_reparse_ok;
            facts.commit_changed_file_count = commit_report.changed_files.size();
            if (!facts.commit_ok) {
                throw std::runtime_error(commit_report.blocking_errors.empty()
                    ? "Section commit validation failed"
                    : commit_report.blocking_errors.front());
            }
            const SnapshotSectionValues committed =
                snapshot_section_row(handle.value, edit.edit_id);
            std::vector<double> expected = edit.values;
            expected[0] = edited_first;
            const std::string committed_bytes = distance_batch_headless::read_fixture_file(
                utf8_to_wide(edit.source_file));
            facts.committed_value_written =
                committed.found && values_match(expected, committed.values) &&
                committed_bytes != baseline_bytes;
            facts.committed_raw_preserved = expression_argument.empty() ||
                committed_bytes.find(expression_argument) != std::string::npos;
            if (!facts.committed_value_written || !facts.committed_raw_preserved) {
                throw std::runtime_error(
                    "Section commit did not write the expected source change");
            }
        }
    } catch (const std::exception& e) {
        facts.error = e.what();
    }

    write_batch_result(*out, facts);
    out->flush();
    return facts.passed() ? 0 : 20;
}

int run_debug_headless_insert_edit(const HeadlessInsertEditOptions& options) {
    using typed_edit_headless::Change;
    using typed_edit_headless::Field;
    using typed_edit_headless::Report;
    using typed_edit_headless::Resolution;
    using typed_edit_headless::Boundary;

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
    *out << "command=debug-headless-insert-edit\n"
         << "path=" << options.path << "\n"
         << "commit=" << (options.commit ? 1 : 0) << "\n"
         << "scope=" << (options.repeater_only ? "repeater-only" : "all") << "\n";

    int failed_cases = 0;
    void* handle = nullptr;
    try {
        handle = kv_load_map_ex(options.path.c_str(), options.unit_distance,
                                KV_LOAD_EDIT_METADATA);
        if (!handle) {
            const char* error = kv_get_last_error();
            throw std::runtime_error(error ? error : "real map load failed");
        }
        auto get_snapshot = [&]() {
            KvMapSnapshot result{};
            if (!kv_get_map_snapshot(handle, KV_MAP_SNAPSHOT_VERSION,
                                     &result, sizeof(result)) ||
                result.version != KV_MAP_SNAPSHOT_VERSION ||
                result.structure_size < sizeof(KvMapSnapshot)) {
                const char* error = kv_get_last_error();
                throw std::runtime_error(std::string("map snapshot failed") +
                    (error ? ": " + std::string(error) : std::string{}));
            }
            return result;
        };
        auto snapshot_text = [](const KvMapSnapshot& snap, KvStringRef ref) {
            if (ref.length == 0) return std::string{};
            if (!snap.string_data || ref.offset > snap.string_size ||
                ref.length > snap.string_size - ref.offset) {
                throw std::runtime_error("map snapshot string reference is out of range");
            }
            return std::string(snap.string_data + static_cast<size_t>(ref.offset),
                               static_cast<size_t>(ref.length));
        };
        auto source_file_path = [&](const KvMapSnapshot& snap, std::uint64_t file_index) {
            if (file_index >= snap.source_file_count || !snap.source_files) return std::string{};
            return snapshot_text(snap, snap.source_files[file_index].file_path);
        };

        // The insert target is the source file that owns the most distance
        // statements: it is the map body with resolvable distance sections,
        // unlike the entry file (usually include-only) or small list files
        // whose same-distance blocks are ambiguous.
        const KvMapSnapshot baseline_snap = get_snapshot();
        std::map<std::string, std::uint64_t> distance_statement_counts;
        std::map<std::string, std::vector<const KvStatementRow*>> distance_statements_by_file;
        for (std::uint64_t i = 0; i < baseline_snap.statement_count; ++i) {
            const KvStatementRow& statement = baseline_snap.statements[i];
            if (snapshot_text(baseline_snap, statement.statement_kind) != "Distance.Set") {
                continue;
            }
            const std::string path = source_file_path(
                baseline_snap, statement.source.source_file_index);
            if (!path.empty()) {
                ++distance_statement_counts[path];
                distance_statements_by_file[path].push_back(&statement);
            }
        }
        if (distance_statement_counts.empty()) {
            throw std::runtime_error("real map has no numeric distance statements");
        }

        const auto statement_order = [](const KvStatementRow* lhs,
                                        const KvStatementRow* rhs) {
            if (lhs->global_order != rhs->global_order) {
                return lhs->global_order < rhs->global_order;
            }
            if (lhs->source.byte_start != rhs->source.byte_start) {
                return lhs->source.byte_start < rhs->source.byte_start;
            }
            return lhs->source.include_invocation_key.offset <
                rhs->source.include_invocation_key.offset;
        };
        for (auto& entry : distance_statements_by_file) {
            std::stable_sort(entry.second.begin(), entry.second.end(), statement_order);
        }

        auto safe_variable_distance_expression = [](const std::string& expression) {
            return expression.find('$') != std::string::npos &&
                ascii_lower(expression).find("distance") == std::string::npos;
        };
        auto find_variable_gap = [&](const std::vector<const KvStatementRow*>& rows,
                                     double& gap_distance,
                                     std::string& expression) {
            for (size_t i = 1; i < rows.size(); ++i) {
                const KvStatementRow& before = *rows[i - 1];
                const KvStatementRow& after = *rows[i];
                const std::string before_context = snapshot_text(
                    baseline_snap, before.source.include_invocation_key);
                const std::string after_context = snapshot_text(
                    baseline_snap, after.source.include_invocation_key);
                if (before_context != after_context) continue;
                const double delta = after.distance_value - before.distance_value;
                if (std::fabs(delta) < 2.0) continue;
                const std::string before_expression = snapshot_text(
                    baseline_snap, before.distance_expression);
                if (!safe_variable_distance_expression(before_expression)) continue;
                gap_distance = before.distance_value + (delta > 0.0 ? 1.0 : -1.0);
                expression = before_expression;
                return true;
            }
            return false;
        };

        std::string target_file;
        std::uint64_t target_count = 0;
        std::string variable_target_file;
        std::uint64_t variable_target_count = 0;
        double variable_gap_distance = 0.0;
        std::string variable_anchor_expression;
        for (const auto& entry : distance_statements_by_file) {
            double candidate_distance = 0.0;
            std::string candidate_expression;
            if (!find_variable_gap(entry.second, candidate_distance, candidate_expression)) {
                continue;
            }
            if (variable_target_file.empty() ||
                entry.second.size() > variable_target_count) {
                variable_target_file = entry.first;
                variable_target_count = static_cast<std::uint64_t>(entry.second.size());
                variable_gap_distance = candidate_distance;
                variable_anchor_expression = std::move(candidate_expression);
            }
        }
        if (!variable_target_file.empty()) {
            target_file = variable_target_file;
            target_count = variable_target_count;
        } else {
            for (const auto& entry : distance_statement_counts) {
                if (target_file.empty() || entry.second > target_count) {
                    target_file = entry.first;
                    target_count = entry.second;
                }
            }
        }
        if (target_file.empty()) {
            throw std::runtime_error("unable to select a numeric-distance source file");
        }
        *out << "target_file=" << target_file << "\n";
        std::vector<const KvStatementRow*> target_distance_statements =
            distance_statements_by_file[target_file];
        if (target_distance_statements.empty()) {
            throw std::runtime_error(
                "selected source file has no numeric distance statements");
        }

        double insert_distance = target_distance_statements.front()->distance_value;
        bool selected_gap = false;
        if (target_file == variable_target_file) {
            insert_distance = variable_gap_distance;
            selected_gap = true;
        } else {
            for (size_t i = 1; i < target_distance_statements.size(); ++i) {
                const KvStatementRow& before = *target_distance_statements[i - 1];
                const KvStatementRow& after = *target_distance_statements[i];
                const std::string before_context = snapshot_text(
                    baseline_snap, before.source.include_invocation_key);
                const std::string after_context = snapshot_text(
                    baseline_snap, after.source.include_invocation_key);
                if (before_context != after_context) continue;
                const double delta = after.distance_value - before.distance_value;
                if (std::fabs(delta) < 2.0) continue;
                insert_distance = before.distance_value + (delta > 0.0 ? 1.0 : -1.0);
                selected_gap = true;
                break;
            }
        }

        auto make_insert_change = [&](const std::string& change_id,
                                      const std::string& row_kind,
                                      const std::vector<Field>& fields) {
            Change change;
            change.change_id = change_id;
            change.edit_id = change_id;
            change.operation = KV_EDIT_INSERT;
            change.target_file_path = target_file;
            change.fields.push_back({"rowKind", row_kind});
            for (const Field& field : fields) change.fields.push_back(field);
            return change;
        };
        auto distance_field = [](const std::string& value) {
            return Field{"distance", value};
        };

        const std::string distance_text = format_double(insert_distance, 6);
        *out << "insert_distance=" << distance_text << "\n"
             << "distance_selection=" << (selected_gap
                 ? (target_file == variable_target_file
                     ? "adjacent-gap-variable"
                     : "adjacent-gap")
                 : "existing-block")
             << "\n"
             << "variable_distance_expression=" << variable_anchor_expression << "\n";

        auto snapshot_value_or = [&](const KvValue& value, const char* fallback) {
            const std::string text = distance_batch_headless::snapshot_value_text(
                baseline_snap, value);
            return text.empty() ? std::string(fallback) : text;
        };
        const std::string structure_key = baseline_snap.structure_put_count != 0 &&
                baseline_snap.structure_puts
            ? snapshot_value_or(baseline_snap.structure_puts[0].structure_key, "0")
            : "0";
        const std::string track_key = baseline_snap.structure_put_count != 0 &&
                baseline_snap.structure_puts
            ? snapshot_value_or(baseline_snap.structure_puts[0].track_key, "0")
            : "0";
        const std::string track_key1 = baseline_snap.structure_between_count != 0 &&
                baseline_snap.structure_betweens
            ? snapshot_value_or(baseline_snap.structure_betweens[0].track_key1, track_key.c_str())
            : track_key;
        const std::string track_key2 = baseline_snap.structure_between_count != 0 &&
                baseline_snap.structure_betweens
            ? snapshot_value_or(baseline_snap.structure_betweens[0].track_key2, track_key.c_str())
            : track_key;
        const std::string station_key = baseline_snap.station_put_count != 0 &&
                baseline_snap.station_puts
            ? snapshot_value_or(baseline_snap.station_puts[0].station_key, "0")
            : "0";
        const std::string signal_aspect_key = baseline_snap.signal_put_count != 0 &&
                baseline_snap.signal_puts
            ? snapshot_value_or(baseline_snap.signal_puts[0].signal_aspect_key, "0")
            : "0";
        const std::string sound_key = baseline_snap.map_sound_count != 0 &&
                baseline_snap.map_sounds
            ? snapshot_value_or(baseline_snap.map_sounds[0].sound_key, "0")
            : "0";
        const std::string sound3d_key = baseline_snap.map_sound_3d_count != 0 &&
                baseline_snap.map_sounds_3d
            ? snapshot_value_or(baseline_snap.map_sounds_3d[0].sound_key, sound_key.c_str())
            : sound_key;
        std::string repeater_track_key = track_key;
        std::string repeater_structure_key = structure_key;
        std::string repeater_begin_key;
        std::string repeater_begin0_key;
        if (options.repeater_only) {
            const repeater_linkage::Linkage linkage =
                repeater_key_edit_headless::linkage_from_snapshot(baseline_snap);
            const KvRepeaterRow* source_repeater = nullptr;
            for (std::uint64_t index = 0; index < baseline_snap.repeater_count; ++index) {
                const KvRepeaterRow& candidate = baseline_snap.repeaters[index];
                const std::string method = snapshot_text(baseline_snap, candidate.method);
                if ((method == "Begin" || method == "Begin0") &&
                    candidate.structure_keys.count != 0 && baseline_snap.values &&
                    candidate.structure_keys.offset < baseline_snap.value_count &&
                    candidate.structure_keys.count <= baseline_snap.value_count -
                        candidate.structure_keys.offset) {
                    source_repeater = &candidate;
                    break;
                }
            }
            if (!source_repeater) {
                throw std::runtime_error(
                    "--repeater-only requires a Repeater.Begin/Begin0 with a structure key");
            }
            repeater_track_key = snapshot_value_or(source_repeater->track_key, "0");
            if (source_repeater->track_key.kind == KV_VALUE_STRING) {
                repeater_track_key = "'" + repeater_track_key + "'";
            }
            repeater_structure_key = distance_batch_headless::snapshot_value_text(
                baseline_snap,
                baseline_snap.values[source_repeater->structure_keys.offset]);
            if (repeater_structure_key.empty()) {
                throw std::runtime_error(
                    "--repeater-only found an empty Repeater structure key");
            }
            repeater_begin_key = repeater_key_edit_headless::unused_key(
                linkage, "headless-repeater-begin");
            repeater_begin0_key = repeater_key_edit_headless::unused_key(
                linkage, "headless-repeater-begin0");
            *out << "repeater_begin_key=" << repeater_begin_key << "\n"
                 << "repeater_begin0_key=" << repeater_begin0_key << "\n"
                 << "repeater_structure_key=" << repeater_structure_key << "\n";
        }

        auto count_rows = [&](const KvMapSnapshot& snap, const char* kind) {
            if (std::strcmp(kind, "repeater") == 0) return snap.repeater_count;
            if (std::strcmp(kind, "structure.put") == 0) return snap.structure_put_count;
            if (std::strcmp(kind, "structure.between") == 0) {
                return snap.structure_between_count;
            }
            if (std::strcmp(kind, "station.put") == 0) return snap.station_put_count;
            if (std::strcmp(kind, "signal.put") == 0) return snap.signal_put_count;
            if (std::strcmp(kind, "speedlimit") == 0) return snap.speed_limit_count;
            if (std::strcmp(kind, "beacon.put") == 0) return snap.beacon_count;
            if (std::strcmp(kind, "section.begin") == 0) return snap.section_begin_count;
            if (std::strcmp(kind, "section.speedLimit") == 0) {
                return snap.section_speed_limit_count;
            }
            if (std::strcmp(kind, "irregularity.change") == 0) return snap.irregularity_count;
            if (std::strcmp(kind, "drawDistance.change") == 0) return snap.draw_distance_count;
            if (std::strcmp(kind, "cabIlluminance.change") == 0) return snap.cab_illuminance_count;
            if (std::strcmp(kind, "fog.change") == 0) return snap.fog_count;
            if (std::strcmp(kind, "adhesion.change") == 0) return snap.adhesion_count;
            if (std::strcmp(kind, "mapSound.play") == 0) return snap.map_sound_count;
            if (std::strcmp(kind, "mapSound3D.put") == 0) return snap.map_sound_3d_count;
            if (std::strcmp(kind, "rollingNoise.change") == 0) return snap.rolling_noise_count;
            if (std::strcmp(kind, "flangeNoise.change") == 0) return snap.flange_noise_count;
            if (std::strcmp(kind, "jointNoise.play") == 0) return snap.joint_noise_count;
            if (std::strcmp(kind, "background.change") == 0) return snap.background_count;
            return std::uint64_t{0};
        };

        std::vector<Change> batch;
        if (options.repeater_only) {
            batch = {
                make_insert_change("headless-insert-repeater-begin", "repeater",
                                   {distance_field(distance_text), {"method", "Begin"},
                                    {"repeaterKey", repeater_begin_key},
                                    {"trackKey", repeater_track_key},
                                    {"x", "1"}, {"y", "2"}, {"z", "3"},
                                    {"rx", "4"}, {"ry", "5"}, {"rz", "6"},
                                    {"tilt", "0"}, {"span", "25"}, {"interval", "5"},
                                    {"structureKeys.count", "2"},
                                    {"structureKeys.0", repeater_structure_key},
                                    {"structureKeys.1", repeater_structure_key}}),
                make_insert_change("headless-insert-repeater-begin0", "repeater",
                                   {distance_field(distance_text), {"method", "Begin0"},
                                    {"repeaterKey", repeater_begin0_key},
                                    {"trackKey", repeater_track_key},
                                    {"tilt", "0"}, {"span", "25"}, {"interval", "5"},
                                    {"structureKeys.count", "1"},
                                    {"structureKeys.0", repeater_structure_key}}),
            };
        } else {
        batch = {
            make_insert_change("headless-insert-structure-put", "structure.put",
                               {distance_field(distance_text), {"method", "Put"},
                                {"structureKey", structure_key}, {"trackKey", track_key},
                                {"x", "1"}, {"y", "2"}, {"z", "3"},
                                {"rx", "4"}, {"ry", "5"}, {"rz", "6"},
                                {"tilt", "0"}, {"span", "10"}}),
            make_insert_change("headless-insert-structure-put0", "structure.put",
                               {distance_field(distance_text), {"method", "Put0"},
                                {"structureKey", structure_key}, {"trackKey", track_key},
                                {"tilt", "0"}, {"span", "10"}}),
            make_insert_change("headless-insert-structure-between", "structure.between",
                               {distance_field(distance_text), {"structureKey", structure_key},
                                {"trackKey1", track_key1}, {"trackKey2", track_key2},
                                {"flag", "0"}}),
            make_insert_change("headless-insert-station", "station.put",
                               {distance_field(distance_text), {"stationKey", station_key},
                                {"door", "0"}, {"margin1", "-5"}, {"margin2", "5"}}),
            make_insert_change("headless-insert-signal", "signal.put",
                               {distance_field(distance_text),
                                {"signalAspectKey", signal_aspect_key}, {"section", "1"},
                                {"trackKey", track_key}, {"x", "1"}, {"y", "2"},
                                {"z", "3"}, {"rx", "4"}, {"ry", "5"}, {"rz", "6"},
                                {"tilt", "0"}, {"span", "10"}}),
            make_insert_change("headless-insert-speed-begin", "speedlimit",
                               {distance_field(distance_text), {"method", "Begin"},
                                {"speed", "60"}}),
            make_insert_change("headless-insert-speed-end", "speedlimit",
                               {distance_field(distance_text), {"method", "End"}}),
            make_insert_change("headless-insert-beacon", "beacon.put",
                               {distance_field(distance_text),
                                {"type", "1"}, {"section", "2"}, {"sendData", "3"}}),
            make_insert_change("headless-insert-section", "section.begin",
                               {distance_field(distance_text),
                                {"method", "Begin"},
                                {"values.count", "2"},
                                {"values.0", "1"}, {"values.1", "2"}}),
            make_insert_change("headless-insert-section-beginnew", "section.begin",
                               {distance_field(distance_text),
                                {"method", "BeginNew"},
                                {"values.count", "2"},
                                {"values.0", "1"}, {"values.1", "2"}}),
            make_insert_change("headless-insert-section-speed", "section.speedLimit",
                               {distance_field(distance_text),
                                {"method", "SetSpeedLimit"},
                                {"values.count", "2"},
                                {"values.0", "60"}, {"values.1", "80"}}),
            make_insert_change("headless-insert-section-signal-speed", "section.speedLimit",
                               {distance_field(distance_text),
                                {"method", "Signal.SpeedLimit"},
                                {"values.count", "2"},
                                {"values.0", "60"}, {"values.1", "80"}}),
            make_insert_change("headless-insert-irregularity", "irregularity.change",
                               {distance_field(distance_text),
                                {"x", "1"}, {"y", "2"}, {"r", "3"},
                                {"lx", "4"}, {"ly", "5"}, {"lr", "6"}}),
            make_insert_change("headless-insert-draw", "drawDistance.change",
                               {distance_field(distance_text), {"value", "500"}}),
            make_insert_change("headless-insert-cab", "cabIlluminance.change",
                               {distance_field(distance_text), {"method", "Set"},
                                {"value", "10"}}),
            make_insert_change("headless-insert-cab-interpolate", "cabIlluminance.change",
                               {distance_field(distance_text), {"method", "Interpolate"},
                                {"value", "20"}}),
            make_insert_change("headless-insert-fog", "fog.change",
                               {distance_field(distance_text), {"method", "Set"},
                                {"density", "50"}, {"red", "100"},
                                {"green", "100"}, {"blue", "100"}}),
            make_insert_change("headless-insert-fog-interpolate", "fog.change",
                               {distance_field(distance_text), {"method", "Interpolate"},
                                {"density", "50"}}),
            make_insert_change("headless-insert-adhesion", "adhesion.change",
                               {distance_field(distance_text),
                                 {"a", "1.5"}, {"b", "2.5"}, {"c", "3.5"}}),
            make_insert_change("headless-insert-map-sound", "mapSound.play",
                               {distance_field(distance_text), {"soundKey", sound_key}}),
            make_insert_change("headless-insert-map-sound3d", "mapSound3D.put",
                               {distance_field(distance_text), {"soundKey", sound3d_key},
                                {"x", "1"}, {"y", "2"}}),
            make_insert_change("headless-insert-rolling-noise", "rollingNoise.change",
                               {distance_field(distance_text), {"index", "1"}}),
            make_insert_change("headless-insert-flange-noise", "flangeNoise.change",
                               {distance_field(distance_text), {"index", "2"}}),
            make_insert_change("headless-insert-joint-noise", "jointNoise.play",
                               {distance_field(distance_text), {"index", "3"}}),
            make_insert_change("headless-insert-background", "background.change",
                               {distance_field(distance_text), {"structureKey", structure_key}}),
        };
        }
        std::vector<std::string> batch_kinds = {
            "structure.put", "structure.between", "station.put", "signal.put",
            "speedlimit", "beacon.put", "section.begin", "section.speedLimit",
            "irregularity.change", "drawDistance.change", "cabIlluminance.change",
            "fog.change", "adhesion.change", "mapSound.play", "mapSound3D.put",
            "rollingNoise.change", "flangeNoise.change", "jointNoise.play",
            "background.change",
        };
        const size_t expected_inserts = batch.size();
        std::map<std::string, std::uint64_t> expected_row_increments = {
            {"structure.put", 2},
            {"structure.between", 1},
            {"station.put", 1},
            {"signal.put", 1},
            {"speedlimit", 2},
            {"section.begin", 2},
            {"section.speedLimit", 2},
            {"cabIlluminance.change", 2},
            {"fog.change", 2},
            {"beacon.put", 1},
            {"irregularity.change", 1},
            {"drawDistance.change", 1},
            {"adhesion.change", 1},
            {"mapSound.play", 1},
            {"mapSound3D.put", 1},
            {"rollingNoise.change", 1},
            {"flangeNoise.change", 1},
            {"jointNoise.play", 1},
            {"background.change", 1},
        };
        if (options.repeater_only) {
            batch_kinds = {"repeater"};
            expected_row_increments = {{"repeater", 2}};
        }
        std::map<std::string, std::uint64_t> baseline_counts;
        for (const std::string& kind : batch_kinds) {
            baseline_counts[kind] = count_rows(baseline_snap, kind.c_str());
        }

        Report dry = typed_edit_headless::dry_run(handle, batch);
        bool manual_boundary_retry = false;
        if ((!dry.ok || !dry.full_reparse_ok) && !dry.resolution_requests.empty()) {
            const Resolution& resolution = dry.resolution_requests.front();
            if (!resolution.allowed_boundaries.empty()) {
                const Boundary& boundary = resolution.allowed_boundaries.front();
                for (Change& change : batch) {
                    if (std::find(resolution.affected_edit_ids.begin(),
                                  resolution.affected_edit_ids.end(),
                                  change.edit_id) == resolution.affected_edit_ids.end()) {
                        continue;
                    }
                    change.distance_resolution_key = resolution.resolution_key;
                    change.distance_boundary_token = boundary.token;
                    if (!resolution.suggested_expression.empty()) {
                        change.distance_expression = resolution.suggested_expression;
                    }
                }
                manual_boundary_retry = true;
                dry = typed_edit_headless::dry_run(handle, batch);
            }
        }
        auto previews_contain = [&](const std::string& text) {
            return std::any_of(dry.previews.begin(), dry.previews.end(),
                               [&](const std::string& preview) {
                                   return preview.find(text) != std::string::npos;
                               });
        };
        std::vector<std::string> expected_statements = {
            "Structure[", "].Put(", "].Put0(", "].PutBetween(",
            "Station[", "Signal[", "SpeedLimit.Begin(60);", "SpeedLimit.End();",
            "Section.Begin(1,2);", "Section.BeginNew(1,2);",
            "Section.SetSpeedLimit(60,80);", "Signal.SpeedLimit(60,80);",
            "Irregularity.Change(1,2,3,4,5,6);", "Beacon.Put(1,2,3);",
            "DrawDistance.Change(500);", "CabIlluminance.Set(10);",
            "CabIlluminance.Interpolate(20);", "Fog.Set(50,100,100,100);",
            "Fog.Interpolate(50);", "Adhesion.Change(1.5,2.5,3.5);",
            "Sound[", "Sound3D[", "RollingNoise.Change(1);",
            "FlangeNoise.Change(2);", "JointNoise.Play(3);", "Background.Change(",
        };
        if (options.repeater_only) {
            expected_statements = {
                "Repeater[", repeater_begin_key, "].Begin(",
                repeater_begin0_key, "].Begin0(",
            };
        }
        const bool syntax_preview_ok = std::all_of(
            expected_statements.begin(), expected_statements.end(), previews_contain);
        const bool variable_distance_preview_ok = variable_anchor_expression.empty() ||
            std::any_of(dry.previews.begin(), dry.previews.end(),
                        [](const std::string& preview) {
                            return preview.find('$') != std::string::npos;
                        });
        *out << "syntax_preview_ok=" << (syntax_preview_ok ? 1 : 0) << "\n"
             << "variable_distance_preview_ok="
             << (variable_distance_preview_ok ? 1 : 0) << "\n";
        if (!syntax_preview_ok || !variable_distance_preview_ok) ++failed_cases;
        *out << "manual_boundary_retry=" << (manual_boundary_retry ? 1 : 0) << "\n";
        const bool dry_ok = dry.ok && dry.insert_count == static_cast<int>(expected_inserts) &&
            dry.full_reparse_ok;
        *out << "dry_run_ok=" << (dry_ok ? 1 : 0) << "\n"
             << "dry_report_ok=" << (dry.ok ? 1 : 0) << "\n"
             << "dry_full_reparse_ok=" << (dry.full_reparse_ok ? 1 : 0) << "\n"
             << "dry_target_distance_matches=" << dry.target_distance_match_count << "\n"
             << "dry_non_target_changed=" << dry.non_target_changed_count << "\n"
             << "dry_resolution_count=" << dry.resolution_requests.size() << "\n"
             << "dry_error_count=" << dry.blocking_errors.size() << "\n"
             << "dry_run_insert_count=" << dry.insert_count << "\n"
             << "dry_run_created_distance_blocks=" << dry.created_distance_block_count << "\n";
        for (const std::string& error : dry.blocking_errors) {
            *out << "dry_run_error=" << error << "\n";
        }
        for (const Resolution& resolution : dry.resolution_requests) {
            *out << "dry_resolution_reason=" << resolution.reason
                 << " boundaries=" << resolution.allowed_boundaries.size()
                 << " distance=" << format_double(resolution.target_distance, 6) << "\n";
        }
        if (!dry_ok) ++failed_cases;

        const Report applied = typed_edit_headless::apply_to_memory(handle, batch);
        const bool apply_ok = applied.ok &&
            applied.insert_count == static_cast<int>(expected_inserts) &&
            applied.full_reparse_ok;
        *out << "apply_ok=" << (apply_ok ? 1 : 0) << "\n";
        for (const std::string& error : applied.blocking_errors) {
            *out << "apply_error=" << error << "\n";
        }
        if (!apply_ok) ++failed_cases;

        bool counts_match = false;
        if (apply_ok) {
            const KvMapSnapshot applied_snap = get_snapshot();
            counts_match = true;
            for (const std::string& kind : batch_kinds) {
                const std::uint64_t expected = baseline_counts[kind] +
                    expected_row_increments.at(kind);
                const std::uint64_t actual = count_rows(applied_snap, kind.c_str());
                *out << "row_count_" << kind << "=" << actual << " expected=" << expected << "\n";
                if (actual != expected) counts_match = false;
            }
            if (!counts_match) ++failed_cases;
        }

        bool reset_ok = false;
        if (kv_edit_reset_memory(handle)) {
            const KvMapSnapshot reset_snap = get_snapshot();
            reset_ok = true;
            for (const std::string& kind : batch_kinds) {
                if (count_rows(reset_snap, kind.c_str()) != baseline_counts[kind]) {
                    reset_ok = false;
                }
            }
        }
        *out << "reset_ok=" << (reset_ok ? 1 : 0) << "\n";
        if (!reset_ok) ++failed_cases;

        if (options.commit) {
            const Report reapplied = typed_edit_headless::apply_to_memory(handle, batch);
            if (!reapplied.ok || reapplied.insert_count != static_cast<int>(expected_inserts)) {
                throw std::runtime_error("insert re-apply before commit failed");
            }
            const Report committed = typed_edit_headless::commit(handle);
            bool commit_ok = committed.ok && !committed.changed_files.empty();
            *out << "commit_ok=" << (commit_ok ? 1 : 0) << "\n";
            for (const std::string& error : committed.blocking_errors) {
                *out << "commit_error=" << error << "\n";
            }
            if (commit_ok) {
                std::ifstream disk_file(std::filesystem::path(utf8_to_wide(target_file)),
                                        std::ios::in | std::ios::binary);
                if (disk_file) {
                    std::string text((std::istreambuf_iterator<char>(disk_file)),
                                     std::istreambuf_iterator<char>());
                    if (options.repeater_only) {
                        const bool has_begin =
                            text.find("Repeater[") != std::string::npos &&
                            text.find(repeater_begin_key) != std::string::npos &&
                            text.find(".Begin(") != std::string::npos;
                        const bool has_begin0 =
                            text.find(repeater_begin0_key) != std::string::npos &&
                            text.find(".Begin0(") != std::string::npos;
                        *out << "disk_has_repeater_begin=" << (has_begin ? 1 : 0) << "\n"
                             << "disk_has_repeater_begin0=" << (has_begin0 ? 1 : 0) << "\n";
                        if (!has_begin || !has_begin0) commit_ok = false;
                    } else {
                        const bool has_speed_limit = text.find("SpeedLimit.Begin(60);") !=
                            std::string::npos;
                        const bool has_beacon = text.find("Beacon.Put(1,2,3);") !=
                            std::string::npos;
                        *out << "disk_has_speed_limit=" << (has_speed_limit ? 1 : 0) << "\n"
                             << "disk_has_beacon=" << (has_beacon ? 1 : 0) << "\n";
                        if (!has_speed_limit || !has_beacon) {
                            commit_ok = false;
                        }
                    }
                } else {
                    commit_ok = false;
                }
                const KvMapSnapshot committed_snap = get_snapshot();
                std::uint64_t committed_target_distance_matches = 0;
                for (std::uint64_t i = 0; i < committed_snap.statement_count; ++i) {
                    const KvStatementRow& statement = committed_snap.statements[i];
                    if (snapshot_text(committed_snap, statement.statement_kind) !=
                            "Distance.Set" ||
                        source_file_path(committed_snap, statement.source.source_file_index) !=
                            target_file ||
                        std::fabs(statement.distance_value - insert_distance) > 1e-9) {
                        continue;
                    }
                    ++committed_target_distance_matches;
                }
                *out << "committed_target_distance_matches="
                     << committed_target_distance_matches << "\n";
                if (committed_target_distance_matches == 0) commit_ok = false;
                for (const std::string& kind : batch_kinds) {
                    const std::uint64_t expected = baseline_counts[kind] +
                        expected_row_increments.at(kind);
                    if (count_rows(committed_snap, kind.c_str()) != expected) commit_ok = false;
                }
            }
            bool reload_ok = false;
            if (commit_ok) {
                void* reload_handle = kv_load_map_ex(
                    options.path.c_str(), options.unit_distance, KV_LOAD_EDIT_METADATA);
                if (reload_handle) {
                    KvMapSnapshot reloaded{};
                    reload_ok = kv_get_map_snapshot(
                        reload_handle, KV_MAP_SNAPSHOT_VERSION,
                        &reloaded, sizeof(reloaded)) != 0 &&
                        reloaded.version == KV_MAP_SNAPSHOT_VERSION &&
                        reloaded.structure_size >= sizeof(KvMapSnapshot);
                    if (reload_ok) {
                        for (const std::string& kind : batch_kinds) {
                            const std::uint64_t expected = baseline_counts[kind] +
                                expected_row_increments.at(kind);
                            if (count_rows(reloaded, kind.c_str()) != expected) reload_ok = false;
                        }
                        if (options.repeater_only) {
                            bool has_begin = false;
                            bool has_begin0 = false;
                            for (std::uint64_t index = 0;
                                 index < reloaded.repeater_count; ++index) {
                                const KvRepeaterRow& row = reloaded.repeaters[index];
                                const std::string key = distance_batch_headless::snapshot_value_text(
                                    reloaded, row.repeater_key);
                                const std::string method = snapshot_text(reloaded, row.method);
                                has_begin = has_begin ||
                                    (key == repeater_begin_key && method == "Begin");
                                has_begin0 = has_begin0 ||
                                    (key == repeater_begin0_key && method == "Begin0");
                            }
                            *out << "reload_repeater_count=" << reloaded.repeater_count << "\n"
                                 << "reload_has_repeater_begin=" << (has_begin ? 1 : 0) << "\n"
                                 << "reload_has_repeater_begin0=" << (has_begin0 ? 1 : 0) << "\n";
                            reload_ok = reload_ok && has_begin && has_begin0;
                        }
                    }
                    kv_free(reload_handle);
                }
            }
            *out << "reload_ok=" << (reload_ok ? 1 : 0) << "\n";
            if (!reload_ok) commit_ok = false;
            bool post_commit_apply_ok = false;
            bool post_commit_reset_ok = false;
            if (commit_ok) {
                if (options.repeater_only) {
                    // Reapplying the same two keys after Commit is supposed to
                    // hit the overlap guard, so reload is the durable proof for
                    // this focused mode instead of a duplicate insert attempt.
                    post_commit_apply_ok = true;
                    post_commit_reset_ok = true;
                } else {
                auto committed_file = std::find_if(
                    committed.committed_files.begin(), committed.committed_files.end(),
                    [&](const typed_edit_headless::CommittedFile& file) {
                        return file.file_path == target_file;
                    });
                if (committed_file != committed.committed_files.end() &&
                    !committed_file->source_hash.empty() &&
                    kv_edit_reset_memory(handle)) {
                    std::vector<Change> post_commit_batch = batch;
                    for (Change& change : post_commit_batch) {
                        change.change_id += "-post-commit";
                        change.edit_id = change.change_id;
                        // Mirror the GUI insert path: maploader owns the
                        // authoritative baseline for a row that does not exist
                        // yet, including immediately after a commit.
                        change.expected_source_hash.clear();
                    }
                    const Report post_commit =
                        typed_edit_headless::apply_to_memory(handle, post_commit_batch);
                    post_commit_apply_ok = post_commit.ok &&
                        post_commit.insert_count == static_cast<int>(expected_inserts) &&
                        post_commit.full_reparse_ok && post_commit.blocking_errors.empty();
                    for (const std::string& error : post_commit.blocking_errors) {
                        *out << "post_commit_apply_error=" << error << "\n";
                    }
                    if (post_commit_apply_ok) {
                        const KvMapSnapshot post_commit_snap = get_snapshot();
                        for (const std::string& kind : batch_kinds) {
                            const std::uint64_t expected = baseline_counts[kind] +
                                2 * expected_row_increments.at(kind);
                            if (count_rows(post_commit_snap, kind.c_str()) != expected) {
                                post_commit_apply_ok = false;
                            }
                        }
                    }
                    if (kv_edit_reset_memory(handle)) {
                        const KvMapSnapshot reset_snap = get_snapshot();
                        post_commit_reset_ok = true;
                        for (const std::string& kind : batch_kinds) {
                            const std::uint64_t expected = baseline_counts[kind] +
                                expected_row_increments.at(kind);
                            if (count_rows(reset_snap, kind.c_str()) != expected) {
                                post_commit_reset_ok = false;
                            }
                        }
                    }
                }
                }
            }
            *out << "post_commit_apply_ok=" << (post_commit_apply_ok ? 1 : 0) << "\n"
                 << "post_commit_reset_ok=" << (post_commit_reset_ok ? 1 : 0) << "\n";
            if (!post_commit_apply_ok || !post_commit_reset_ok) commit_ok = false;
            *out << "commit_verified=" << (commit_ok ? 1 : 0) << "\n";
            if (!commit_ok) ++failed_cases;

        }
    } catch (const std::exception& e) {
        *out << "exception=" << e.what() << "\n";
        ++failed_cases;
    }
    if (handle) kv_free(handle);
    *out << "result=" << (failed_cases == 0 ? "PASS" : "FAIL") << "\n";
    out->flush();
    return failed_cases == 0 ? 0 : 21;
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
    double profile_cache_hit_seconds = 0.0;
    double profile_draw_seconds = 0.0;
    double radius_draw_seconds = 0.0;
    double first_2d_frame_seconds = 0.0;
    std::uint64_t geometry_hash = 0;
    std::uint64_t overlay_hash = 0;
};
struct DebugHash64 : KmeByteHash64 {
    void number(double input) {
        integer(hash_double_bits(input));
    }

    void text(std::string_view input) {
        integer(static_cast<std::uint64_t>(input.size()));
        bytes(input);
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
        hash_marker_cache(source.legacy_fog_marker_cache_);

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
        const ProfileData& profile = app.current_profile_data();
        sample.profile_data_seconds = std::chrono::duration<double>(
            std::chrono::steady_clock::now() - profile_data_started_at).count();
        const std::uint64_t profile_rebuilds = app.profile_data_cache_.rebuild_count;
        const auto profile_hit_started_at = std::chrono::steady_clock::now();
        const ProfileData& cached_profile = app.current_profile_data();
        sample.profile_cache_hit_seconds = std::chrono::duration<double>(
            std::chrono::steady_clock::now() - profile_hit_started_at).count();
        workflow_ok = workflow_ok && &cached_profile == &profile &&
            app.profile_data_cache_.rebuild_count == profile_rebuilds;
        const auto profile_draw_started_at = std::chrono::steady_clock::now();
        app.render_profile_plot(cached_profile, ImVec2(625.0f, 250.0f));
        sample.profile_draw_seconds = std::chrono::duration<double>(
            std::chrono::steady_clock::now() - profile_draw_started_at).count();
        ImGui::SameLine();
        const auto radius_draw_started_at = std::chrono::steady_clock::now();
        app.render_radius_plot(cached_profile, ImVec2(625.0f, 250.0f));
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
              << " profile_cache_hit=" << sample.profile_cache_hit_seconds << "s"
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

    bool profile_cache_checks_ok = false;
    if (!samples.empty()) {
        auto key = [&]() -> auto& { return *app.profile_data_cache_.key; };
        auto require_miss = [&](auto& cached, auto mismatched) {
            const std::uint64_t before = app.profile_data_cache_.rebuild_count;
            cached = std::move(mismatched);
            app.current_profile_data();
            return app.profile_data_cache_.rebuild_count == before + 1;
        };
        bool keyed_state_pass = require_miss(key().source_revision, key().source_revision + 1);
        keyed_state_pass &= require_miss(key().has_model, !app.has_model_);
        keyed_state_pass &= require_miss(key().distance_min, NAN);
        keyed_state_pass &= require_miss(key().distance_max, NAN);
        keyed_state_pass &= require_miss(key().show_other, !app.show_profile_other_);
        keyed_state_pass &= require_miss(key().language,
            app.lang_ == Language::En ? Language::Zh : Language::En);
        if (!key().other_tracks.empty()) {
            auto track = [&]() -> auto& { return key().other_tracks.front(); };
            keyed_state_pass &= require_miss(track().key, track().key + "#");
            keyed_state_pass &= require_miss(track().visible, !app.model_.other_tracks[0].visible);
            keyed_state_pass &= require_miss(track().range_min, NAN);
            keyed_state_pass &= require_miss(track().range_max, NAN);
            keyed_state_pass &= require_miss(
                track().color, std::array<float, 4>{NAN, NAN, NAN, NAN});
        }
        profile_cache_checks_ok = keyed_state_pass;
        *out << "profile_cache_checks result=" << (profile_cache_checks_ok ? "PASS" : "FAIL") << "\n";
    }
    workflow_ok = workflow_ok && profile_cache_checks_ok;

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
int App::run_debug_headless_diagnostics_popup_benchmark(
    const HeadlessDiagnosticsPopupBenchOptions& options) {
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

    *out << "komapedit debug-headless-diagnostics-popup-bench log_lines=100000\n"
         << "stage=setup-start\n";
    out->flush();
    ImGui::CreateContext();
    ImPlot::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(900.0f, 500.0f);
    io.DeltaTime = 1.0f / 60.0f;
    io.IniFilename = nullptr;
    io.Fonts->AddFontDefault();
    io.Fonts->Build();
    *out << "stage=imgui-ready\n";
    out->flush();

    int exit_code = 0;
    try {
        UserSettings settings;
        App app(nullptr, settings, 1.0f, false, false);
        *out << "stage=app-ready\n";
        out->flush();
        {
            std::lock_guard<std::mutex> lock(app.log_mutex_);
            app.logs_.clear();
            app.error_count_.store(0, std::memory_order_relaxed);
            app.warn_count_.store(0, std::memory_order_relaxed);
            ++app.log_revision_;
            app.diagnostics_snapshot_revision_ =
                std::numeric_limits<std::uint64_t>::max();
            app.diagnostics_snapshot_.clear();
        }
        *out << "stage=logs-cleared\n";
        out->flush();

        constexpr size_t log_line_count = 100000;
        for (size_t index = 0; index < log_line_count; ++index) {
            const LogSeverity severity = index % 3 == 0
                ? LogSeverity::Info
                : index % 3 == 1 ? LogSeverity::Warning : LogSeverity::Error;
            const char* prefix = severity == LogSeverity::Info
                ? "[info]" : severity == LogSeverity::Warning ? "[warn]" : "[error]";
            app.add_log_at(__FILE__, severity, std::string(prefix) +
                                      " diagnostics contract line " +
                                      std::to_string(index));
            if ((index + 1) % 10000 == 0) {
                *out << "stage=logs-progress count=" << index + 1 << "\n";
                out->flush();
            }
        }
        *out << "stage=logs-ready\n";
        out->flush();

        const auto snapshot_started = std::chrono::steady_clock::now();
        app.refresh_diagnostics_snapshot();
        const double snapshot_ms = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - snapshot_started).count();
        const size_t expected_initial = (log_line_count / 3) * 2;
        bool count_pass = app.diagnostics_snapshot_.size() == expected_initial;

        const LogLine* snapshot_data = app.diagnostics_snapshot_.data();
        const std::uint64_t snapshot_revision = app.diagnostics_snapshot_revision_;
        const auto cached_started = std::chrono::steady_clock::now();
        app.refresh_diagnostics_snapshot();
        const double cached_ms = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - cached_started).count();
        const bool cached_pass = app.diagnostics_snapshot_revision_ == snapshot_revision &&
            app.diagnostics_snapshot_.data() == snapshot_data;
        *out << "stage=snapshot-ready\n";
        out->flush();

        std::atomic<bool> writers_start{false};
        std::vector<std::thread> writers;
        for (int writer = 0; writer < 2; ++writer) {
            writers.emplace_back([&app, &writers_start, writer]() {
                while (!writers_start.load(std::memory_order_acquire)) std::this_thread::yield();
                for (int index = 0; index < 200; ++index) {
                    const LogSeverity severity = index % 2 == 0
                        ? LogSeverity::Warning : LogSeverity::Error;
                    app.add_log_at(__FILE__, severity,
                                std::string(severity == LogSeverity::Warning
                                                ? "[warn]" : "[error]") +
                                    " concurrent diagnostics writer " +
                                    std::to_string(writer) + " line " +
                                    std::to_string(index));
                }
            });
        }
        writers_start.store(true, std::memory_order_release);
        for (std::thread& writer : writers) writer.join();
        app.refresh_diagnostics_snapshot();
        *out << "stage=concurrent-writers-ready\n";
        out->flush();

        bool order_pass = true;
        size_t filtered_count = 0;
        {
            std::lock_guard<std::mutex> lock(app.log_mutex_);
            for (const LogLine& line : app.logs_) {
                if (line.severity == LogSeverity::Info) continue;
                if (filtered_count >= app.diagnostics_snapshot_.size()) {
                    order_pass = false;
                    break;
                }
                const LogLine& snapshot_line = app.diagnostics_snapshot_[filtered_count++];
                if (snapshot_line.severity != line.severity ||
                    snapshot_line.text != line.text) {
                    order_pass = false;
                    break;
                }
            }
            order_pass = order_pass &&
                filtered_count == app.diagnostics_snapshot_.size() &&
                app.diagnostics_snapshot_revision_ == app.log_revision_;
        }
        count_pass = count_pass && app.logs_.size() == k_max_console_log_lines;
        *out << "stage=order-check-ready\n";
        out->flush();

        auto render_clipped_frame = [&]() {
            size_t submitted = 0;
            ImGui::NewFrame();
            ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f), ImGuiCond_Always);
            ImGui::SetNextWindowSize(io.DisplaySize, ImGuiCond_Always);
            ImGui::Begin("DiagnosticsPopupContract", nullptr,
                         ImGuiWindowFlags_NoDecoration |
                             ImGuiWindowFlags_NoSavedSettings);
            ImGui::BeginChild("DiagnosticsPopupContractList", ImVec2(860.0f, 440.0f));
            ImGuiListClipper clipper;
            clipper.Begin(static_cast<int>(app.diagnostics_snapshot_.size()));
            while (clipper.Step()) {
                for (int index = clipper.DisplayStart; index < clipper.DisplayEnd; ++index) {
                    ImGui::TextUnformatted(
                        app.diagnostics_snapshot_[static_cast<size_t>(index)].text.c_str());
                    ++submitted;
                }
            }
            ImGui::EndChild();
            ImGui::End();
            ImGui::EndFrame();
            return submitted;
        };
        (void)render_clipped_frame();
        *out << "stage=render-warmup-ready\n";
        out->flush();
        std::vector<double> frame_ms;
        frame_ms.reserve(30);
        size_t maximum_submitted = 0;
        for (int frame = 0; frame < 30; ++frame) {
            const auto started = std::chrono::steady_clock::now();
            maximum_submitted = std::max(maximum_submitted, render_clipped_frame());
            frame_ms.push_back(std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - started).count());
        }
        const FrameTimingStats timing = calculate_frame_timing_stats(frame_ms);
        const bool clipping_pass = maximum_submitted > 0 &&
            maximum_submitted < app.diagnostics_snapshot_.size();
        const bool passed = count_pass && cached_pass && order_pass && clipping_pass;

        *out << std::fixed << std::setprecision(3)
             << "snapshot warning_error_count=" << app.diagnostics_snapshot_.size()
             << " build_ms=" << snapshot_ms
             << " cached_refresh_ms=" << cached_ms
             << " concurrent_order=" << (order_pass ? "PASS" : "FAIL")
             << " cached_revision=" << (cached_pass ? "PASS" : "FAIL") << "\n"
             << "render submitted_max=" << maximum_submitted
             << " median_ms=" << timing.median_ms
             << " p95_ms=" << timing.p95_ms
             << " clipping=" << (clipping_pass ? "PASS" : "FAIL") << "\n"
             << "result=" << (passed ? "PASS" : "FAIL") << "\n";
        if (!passed) exit_code = 3;
    } catch (const std::exception& error) {
        *out << "error=diagnostics popup benchmark failed: " << error.what() << "\n"
             << "result=FAIL\n";
        exit_code = 4;
    } catch (...) {
        *out << "error=diagnostics popup benchmark failed: unknown error\n"
             << "result=FAIL\n";
        exit_code = 4;
    }
    out->flush();
    ImPlot::DestroyContext();
    ImGui::DestroyContext();
    return exit_code;
}

int App::run_debug_headless_plan_benchmark(const std::string& path, int frames,
                                           double unit_distance, double pan_pixels,
                                           double max_frame_ms, const std::string& output_path,
                                           bool profile_stages,
                                           const std::string& interaction) {
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
         << " interaction=" << interaction
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
        const bool measure_interaction = interaction != "pan";
        if (measure_interaction) {
            app.mode_ = Mode::Measure;
            io.MousePos = ImVec2(640.0f, 360.0f);
        }
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
#ifndef NDEBUG
        app.debug_measure_validate_bruteforce_ = measure_interaction;
#endif
        // The first headless frame establishes the ImGui window hover state; the
        // second frame exercises the same measurement path used during normal input.
        for (int frame = 0; frame < 2; ++frame) {
            render_frame();
        }
#ifndef NDEBUG
        app.debug_measure_validate_bruteforce_ = false;
#endif
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
        bool pan_rotation_hit = app.plan_data_cache_.rebuild_count == hit_count_before &&
            plan_data_summary_matches(panned_data, uncached_initial);
        const std::uint64_t measure_rebuild_before_pan =
            app.measure_hit_test_cache_.rebuild_count;
        if (measure_interaction) render_frame();
        const bool measure_pan_rotation_hit = !measure_interaction ||
            app.measure_hit_test_cache_.rebuild_count == measure_rebuild_before_pan;
        pan_rotation_hit = pan_rotation_hit && measure_pan_rotation_hit;
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
                enabled_size == projection(enabled_uncached).size() &&
                plan_data_summary_matches(enabled, enabled_uncached);

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
        marker_toggles_pass &= validate_marker_toggle(
            app.show_draw_distance_markers_, 12,
            [](const PlanData& data) -> const auto& { return data.draw_distance_markers; });
        marker_toggles_pass &= validate_marker_toggle(
            app.show_section_markers_, 13,
            [](const PlanData& data) -> const auto& { return data.section_markers; });

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
            visible_curves.transition_sections.size() == visible_curves_uncached.transition_sections.size() &&
            plan_data_summary_matches(visible_curves, visible_curves_uncached);
        app.show_curve_values_ = false;
        const PlanData& hidden_curves = app.current_plan_data();
        const PlanData hidden_curves_uncached = app.build_plan_data(false);
        curve_toggle_pass = curve_toggle_pass && !app.plan_data_cache_.show_curve_values &&
            hidden_curves.curve_sections.empty() && hidden_curves.transition_sections.empty() &&
            plan_data_summary_matches(hidden_curves, hidden_curves_uncached);
        app.show_curve_values_ = original_curve_visibility;
        app.current_plan_data();

        const bool original_edit_mode = app.edit_mode_enabled_;
        const bool original_gradient_visibility = app.show_gradient_pos_;
        std::vector<OwnTrackEditMarker> original_edit_markers =
            std::move(app.own_track_edit_marker_cache_);
        OwnTrackEditMarker curve_edit_marker;
        curve_edit_marker.d = (app.dmin_ + app.dmax_) * 0.5;
        curve_edit_marker.edit_id = "headless-curve-edit-marker";
        OwnTrackEditMarker gradient_edit_marker = curve_edit_marker;
        gradient_edit_marker.gradient = true;
        gradient_edit_marker.edit_id = "headless-gradient-edit-marker";
        app.own_track_edit_marker_cache_ = {curve_edit_marker, gradient_edit_marker};
        app.edit_mode_enabled_ = true;
        app.show_curve_values_ = true;
        app.show_gradient_pos_ = true;
        app.plan_data_cache_.valid = false;
        const PlanData& visible_edit_markers = app.current_plan_data();
        const PlanData visible_edit_markers_uncached = app.build_plan_data(false);
        bool edit_marker_toggle_pass = visible_edit_markers.curve_edit_markers.size() == 1 &&
            visible_edit_markers.gradient_edit_markers.size() == 1 &&
            plan_data_summary_matches(visible_edit_markers, visible_edit_markers_uncached);
        app.show_curve_values_ = false;
        const PlanData& hidden_curve_edit_markers = app.current_plan_data();
        const PlanData hidden_curve_edit_markers_uncached = app.build_plan_data(false);
        edit_marker_toggle_pass = edit_marker_toggle_pass &&
            hidden_curve_edit_markers.curve_edit_markers.empty() &&
            hidden_curve_edit_markers.gradient_edit_markers.size() == 1 &&
            plan_data_summary_matches(hidden_curve_edit_markers,
                                      hidden_curve_edit_markers_uncached);
        app.show_gradient_pos_ = false;
        const PlanData& hidden_edit_markers = app.current_plan_data();
        const PlanData hidden_edit_markers_uncached = app.build_plan_data(false);
        edit_marker_toggle_pass = edit_marker_toggle_pass &&
            hidden_edit_markers.curve_edit_markers.empty() &&
            hidden_edit_markers.gradient_edit_markers.empty() &&
            plan_data_summary_matches(hidden_edit_markers, hidden_edit_markers_uncached);
        app.own_track_edit_marker_cache_ = std::move(original_edit_markers);
        app.edit_mode_enabled_ = original_edit_mode;
        app.show_curve_values_ = original_curve_visibility;
        app.show_gradient_pos_ = original_gradient_visibility;
        app.plan_data_cache_.valid = false;
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

        const bool measure_index_pass = !measure_interaction ||
            (app.debug_measure_validation_passed_ && app.debug_measure_query_count_ > 0 &&
             measure_pan_rotation_hit);
        const bool cache_checks_pass = uncached_match && stable_hit && pan_rotation_hit &&
            marker_toggles_pass && curve_toggle_pass && edit_marker_toggle_pass &&
            row_visibility_pass && keyed_state_pass && source_revision_pass &&
            measure_index_pass;
        *out << "plan_cache_checks uncached_match=" << (uncached_match ? "PASS" : "FAIL")
             << " stable_hit=" << (stable_hit ? "PASS" : "FAIL")
             << " pan_rotation_hit=" << (pan_rotation_hit ? "PASS" : "FAIL")
             << " marker_toggles=" << (marker_toggles_pass ? "PASS" : "FAIL")
             << " curve_toggle=" << (curve_toggle_pass ? "PASS" : "FAIL")
             << " edit_marker_toggle=" << (edit_marker_toggle_pass ? "PASS" : "FAIL")
             << " row_visibility=" << (row_visibility_pass ? "PASS" : "FAIL")
             << " keyed_state=" << (keyed_state_pass ? "PASS" : "FAIL")
             << " source_revision=" << (source_revision_pass ? "PASS" : "FAIL")
             << " measure_index=" << (measure_index_pass ? "PASS" : "FAIL")
             << " measure_strategy="
             << (app.debug_measure_used_spatial_index_ ? "grid" : "linear")
             << " measure_queries=" << app.debug_measure_query_count_
             << " measure_index_rebuilds=" << app.measure_hit_test_cache_.rebuild_count
             << " result=" << (cache_checks_pass ? "PASS" : "FAIL") << "\n";

        std::vector<double> frame_ms;
        frame_ms.reserve(static_cast<size_t>(frames));
        auto set_interaction_input = [&](int frame) {
            if (interaction == "pan") return;
            if (interaction == "measure-moving") {
                io.MousePos = ImVec2(
                    50.0f + std::fmod(static_cast<float>(frame) * 37.0f, 1180.0f),
                    70.0f + std::fmod(static_cast<float>(frame) * 23.0f, 570.0f));
            } else {
                io.MousePos = ImVec2(640.0f, 360.0f);
            }
        };
        for (int frame = 0; frame < frames; ++frame) {
            if (interaction == "pan") {
                const double step = (frame % 120 < 60) ? pan_pixels : -pan_pixels;
                app.plan_view_.pan_by_screen_delta(ImVec2(static_cast<float>(step), 0.0f));
            } else {
                set_interaction_input(frame);
            }
            auto started_at = std::chrono::steady_clock::now();
            render_frame();
            auto finished_at = std::chrono::steady_clock::now();
            frame_ms.push_back(std::chrono::duration<double, std::milli>(finished_at - started_at).count());
        }
        *out << "stage=frames-complete\n";
        out->flush();

        const FrameTimingStats timing = calculate_frame_timing_stats(frame_ms);
        const bool measured_with_spatial_index =
            measure_interaction && app.debug_measure_used_spatial_index_;
        FrameTimingStats brute_reference_timing;
        bool measure_speedup_pass = true;
        if (measured_with_spatial_index) {
            app.debug_measure_force_bruteforce_ = true;
            set_interaction_input(0);
            render_frame();
            std::vector<double> brute_frame_ms;
            brute_frame_ms.reserve(static_cast<size_t>(frames));
            for (int frame = 0; frame < frames; ++frame) {
                set_interaction_input(frame);
                const auto started_at = std::chrono::steady_clock::now();
                render_frame();
                brute_frame_ms.push_back(std::chrono::duration<double, std::milli>(
                    std::chrono::steady_clock::now() - started_at).count());
            }
            app.debug_measure_force_bruteforce_ = false;
            brute_reference_timing = calculate_frame_timing_stats(brute_frame_ms);
            if (app.model_.own.rows >= 250000) {
                measure_speedup_pass = timing.p95_ms <= brute_reference_timing.p95_ms * 0.5;
            }
        } else if (measure_interaction) {
            // The selected small-route path is already the original exhaustive
            // scan, so a second timed pass would only measure run-to-run noise.
            brute_reference_timing = timing;
        }
        bool pass = cache_checks_pass && timing.p95_ms <= max_frame_ms &&
            measure_speedup_pass;

        *out << std::fixed << std::setprecision(3)
             << "plan_bench avg_ms=" << timing.average_ms
             << " min_ms=" << timing.minimum_ms
             << " median_ms=" << timing.median_ms
             << " p95_ms=" << timing.p95_ms
             << " max_ms=" << timing.maximum_ms
             << " p95_fps=" << timing.p95_fps
             << " result=" << (pass ? "PASS" : "FAIL") << "\n";
        if (measure_interaction) {
            const double reduction_percent = brute_reference_timing.p95_ms > 0.0
                ? (1.0 - timing.p95_ms / brute_reference_timing.p95_ms) * 100.0
                : 0.0;
            *out << "measure_bruteforce_reference strategy="
                 << (measured_with_spatial_index ? "grid" : "linear")
                 << " p95_ms="
                 << brute_reference_timing.p95_ms
                 << " selected_p95_ms=" << timing.p95_ms
                 << " reduction_percent=" << reduction_percent
                 << " large_fixture_requirement="
                 << (app.model_.own.rows >= 250000
                         ? (measure_speedup_pass ? "PASS" : "FAIL")
                         : "N/A") << "\n";
        }
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

        bool inspector_cache_checks_ok = true;
        if (!app.model_.structures.empty()) {
            std::string saved_edit_id = std::move(app.model_.structures.front().edit_id);
            app.model_.structures.front().edit_id = "debug:inspector-row-cache";
            app.inspector_ = {};
            app.inspector_.open = true;
            app.inspector_.edit_id = app.model_.structures.front().edit_id;
            app.inspector_.row_kind = "structure.put";
            app.inspector_.model_row_index = 0;
            app.inspector_.model_row_source_revision = app.plan_data_source_revision_;
            app.sync_scene_placement_edit_from_inspector();
            ++app.plan_data_source_revision_;
            app.sync_scene_placement_edit_from_inspector();
            inspector_cache_checks_ok = app.inspector_.model_row_cache_scans == 1 &&
                app.inspector_.model_row_source_revision == app.plan_data_source_revision_;
            app.model_.structures.front().edit_id = std::move(saved_edit_id);
            app.inspector_ = {};
            app.clear_scene_placement_edit_target();
            *out << "inspector_row_cache_checks result=" << (inspector_cache_checks_ok ? "PASS" : "FAIL") << "\n";
        } else {
            *out << "inspector_row_cache_checks result=SKIP_NO_EDITABLE_PLACEMENT\n";
        }

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
        bool pass = load_completed && terminal_model_state && inspector_cache_checks_ok &&
            timing.p95_ms <= max_frame_ms;
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
             << " median_ms=" << timing.median_ms
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
