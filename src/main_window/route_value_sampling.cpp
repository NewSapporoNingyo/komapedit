/*
 * Copyright (c) 2026 Sapporo_ningyo
 *
 * Licensed under Apache License 2.0; see LICENSE and NOTICE.
 */

#include "route_value_sampling.h"

#include <algorithm>
#include <cmath>

namespace route_value_sampling {

bool append_event(std::vector<Event>& events,
                  double distance,
                  bool has_value,
                  double value,
                  std::string_view flag,
                  double& current_value,
                  size_t source_row_index) {
    if (!std::isfinite(distance) || (has_value && !std::isfinite(value)) ||
        (!has_value && flag != "bt" && flag != "i")) {
        return false;
    }

    Event event;
    event.distance = distance;
    event.previous_value = current_value;
    event.source_row_index = source_row_index;
    if (has_value) current_value = value;
    event.value = current_value;
    if (flag == "bt") {
        event.kind = EventKind::BeginTransition;
    } else if (flag == "i") {
        event.kind = EventKind::Interpolate;
    }
    events.push_back(event);
    return true;
}

Sample sample(const std::vector<Event>& events, double distance) {
    const auto next = std::upper_bound(
        events.begin(), events.end(), distance,
        [](double value, const Event& event) { return value < event.distance; });
    const Event* previous = next == events.begin() ? nullptr : &*(next - 1);

    Sample result;
    result.value = previous ? previous->value : 0.0;
    result.from_distance = previous ? previous->distance : 0.0;
    result.to_distance = result.from_distance;
    result.from_value = result.value;
    result.to_value = result.value;
    if (previous && next != events.end() && next->kind == EventKind::Interpolate) {
        result.mode = Mode::Interpolate;
        result.to_distance = next->distance;
        result.from_value = previous->value;
        result.to_value = next->value;
    } else if (previous && previous->kind == EventKind::BeginTransition &&
               next != events.end() && next->kind != EventKind::BeginTransition) {
        result.mode = Mode::Transition;
        result.to_distance = next->distance;
        result.from_value = previous->previous_value;
        result.to_value = next->value;
    }
    return result;
}

} // namespace route_value_sampling
