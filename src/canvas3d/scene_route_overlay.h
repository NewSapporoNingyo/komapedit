/*
 * Copyright (c) 2026 Sapporo_ningyo
 *
 * Licensed under Apache License 2.0; see LICENSE and NOTICE.
 */

#pragma once

#include "route_value_sampling.h"

#include <cstddef>
#include <string_view>
#include <vector>

namespace scene_route_overlay {

void format_number(char* output, std::size_t output_size, double value);

void format_curve_line(
    char* output,
    std::size_t output_size,
    const std::vector<route_value_sampling::Event>& radius_events,
    const std::vector<route_value_sampling::Event>& cant_events,
    double distance,
    std::string_view straight_label,
    std::string_view interpolate_label);

} // namespace scene_route_overlay
