/*
 * Copyright (c) 2026 Sapporo_ningyo
 *
 * Licensed under Apache License 2.0; see LICENSE and NOTICE.
 */

#pragma once

#include <string_view>
#include <vector>

namespace route_value_sampling {

enum class EventKind {
    Value,
    BeginTransition,
    Interpolate,
};

struct Event {
    double distance = 0.0;
    double previous_value = 0.0;
    double value = 0.0;
    EventKind kind = EventKind::Value;
};

enum class Mode {
    Constant,
    Transition,
    Interpolate,
};

struct Sample {
    Mode mode = Mode::Constant;
    double value = 0.0;
    double from_distance = 0.0;
    double to_distance = 0.0;
    double from_value = 0.0;
    double to_value = 0.0;
};

// Appends one already-evaluated own-track value event. Missing values are
// accepted only for BeginTransition/Interpolate and inherit current_value.
bool append_event(std::vector<Event>& events,
                  double distance,
                  bool has_value,
                  double value,
                  std::string_view flag,
                  double& current_value);

// Events must retain maploader's stable, nondecreasing distance order.
Sample sample(const std::vector<Event>& events, double distance);

} // namespace route_value_sampling
