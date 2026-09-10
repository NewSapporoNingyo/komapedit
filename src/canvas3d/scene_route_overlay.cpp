/*
 * Copyright (c) 2026 Sapporo_ningyo
 *
 * Licensed under Apache License 2.0; see LICENSE and NOTICE.
 */

#include "scene_route_overlay.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>

namespace scene_route_overlay {
namespace {

constexpr double k_display_zero_epsilon = 0.0000005;

int format_view_length(std::string_view value) {
    return static_cast<int>(std::min(
        value.size(), static_cast<std::size_t>(std::numeric_limits<int>::max())));
}

const char* format_view_data(std::string_view value) {
    return value.empty() ? "" : value.data();
}

void format_curve_endpoint(char* output,
                           std::size_t output_size,
                           double radius,
                           double cant) {
    if (!output || output_size == 0) return;
    char radius_text[64] = {};
    char cant_text[64] = {};
    format_number(radius_text, sizeof(radius_text), std::abs(radius));
    format_number(cant_text, sizeof(cant_text), cant);

    if (std::abs(radius) > k_display_zero_epsilon) {
        std::snprintf(output, output_size, "R %s m %s %s",
                      radius_text, cant_text, radius < 0.0 ? u8"←" : u8"→");
    } else {
        std::snprintf(output, output_size, "R %s m %s",
                      radius_text, cant_text);
    }
}

} // namespace

void format_number(char* output, std::size_t output_size, double value) {
    if (!output || output_size == 0) return;
    if (!std::isfinite(value)) value = 0.0;
    if (std::abs(value) <= k_display_zero_epsilon) value = 0.0;
    std::snprintf(output, output_size, "%.6f", value);
    char* end = output + std::strlen(output);
    char* decimal = std::strchr(output, '.');
    if (!decimal) return;
    while (end > decimal + 1 && end[-1] == '0') --end;
    if (end > decimal && end[-1] == '.') --end;
    *end = '\0';
}

void format_curve_line(
    char* output,
    std::size_t output_size,
    const std::vector<route_value_sampling::Event>& radius_events,
    const std::vector<route_value_sampling::Event>& cant_events,
    double distance,
    std::string_view straight_label,
    std::string_view interpolate_label) {
    if (!output || output_size == 0) return;
    output[0] = '\0';

    const route_value_sampling::Sample radius =
        route_value_sampling::sample(radius_events, distance);
    const route_value_sampling::Sample cant =
        route_value_sampling::sample(cant_events, distance);

    const bool zero_radius_interpolation =
        radius.mode == route_value_sampling::Mode::Interpolate &&
        std::abs(radius.from_value) <= k_display_zero_epsilon &&
        std::abs(radius.to_value) <= k_display_zero_epsilon;
    if (radius.mode == route_value_sampling::Mode::Interpolate &&
        !zero_radius_interpolation) {
        const double from_cant =
            route_value_sampling::sample(cant_events, radius.from_distance).value;
        const double to_cant =
            route_value_sampling::sample(cant_events, radius.to_distance).value;
        char from_endpoint[160] = {};
        char to_endpoint[160] = {};
        format_curve_endpoint(from_endpoint, sizeof(from_endpoint),
                              radius.from_value, from_cant);
        format_curve_endpoint(to_endpoint, sizeof(to_endpoint),
                              radius.to_value, to_cant);
        if (interpolate_label.empty()) {
            std::snprintf(output, output_size, u8"%s ➤ %s",
                          from_endpoint, to_endpoint);
        } else {
            std::snprintf(output, output_size, u8"%.*s %s ➤ %s",
                          format_view_length(interpolate_label),
                          format_view_data(interpolate_label),
                          from_endpoint, to_endpoint);
        }
        return;
    }

    const bool transition = radius.mode == route_value_sampling::Mode::Transition;
    const bool use_transition_target =
        !transition || std::abs(radius.to_value) > k_display_zero_epsilon ||
        std::abs(radius.from_value) <= k_display_zero_epsilon;
    const double displayed_radius = transition
        ? (use_transition_target ? radius.to_value : radius.from_value)
        : radius.value;
    const double displayed_cant = transition
        ? (use_transition_target ? cant.to_value : cant.from_value)
        : cant.value;
    const char* prefix = transition ? "[Tr.] " : "";
    if (std::abs(displayed_radius) <= k_display_zero_epsilon) {
        std::snprintf(output, output_size, "%s%.*s", prefix,
                      format_view_length(straight_label),
                      format_view_data(straight_label));
        return;
    }
    char endpoint[160] = {};
    format_curve_endpoint(endpoint, sizeof(endpoint),
                          displayed_radius, displayed_cant);
    std::snprintf(output, output_size, "%s%s", prefix, endpoint);
}

} // namespace scene_route_overlay
