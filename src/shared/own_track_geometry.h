/*
 * Copyright (c) 2026 Sapporo_ningyo
 *
 * Portions of the map parsing and track-geometry design are derived from or
 * reimplemented with reference to kobushi-trackviewer, Copyright (c) 2021-2024
 * konawasabi, licensed under Apache License 2.0.
 *
 * Licensed under Apache License 2.0; see LICENSE and NOTICE.
 */

#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace own_track_geometry {

struct Value {
    double number = 0.0;
    bool continuation = false;

    bool is_continue() const { return continuation; }
};

inline double as_number(Value value) { return value.number; }

struct Event {
    double distance = 0.0;
    std::string key;
    Value value;
    std::string flag;
};

inline constexpr std::size_t columns = 11;

struct Checkpoint {
    std::size_t rows = 0;
    double x = 0.0;
    double y = 0.0;
    double elevation = 0.0;
    double projected_distance = 0.0;
    double theta = 0.0;
    double radius = 0.0;
    double gradient = 0.0;
    double radius_anchor_theta = 0.0;
    double radius_anchor_value = 0.0;
    double cant = 0.0;
    double cant_anchor = 0.0;
    double center = 0.0;
    double gauge = 0.0;
    bool half_sine = false;
    int gradient_event = -1;
    int radius_event = -1;
    int turn_event = -1;
    int function_event = -1;
    int center_event = -1;
    int gauge_event = -1;
    int cant_event = -1;
};

struct Trace {
    std::vector<Checkpoint> checkpoints;
    std::vector<double> event_projections;
};

struct Prefix {
    const Checkpoint& state;
    const std::vector<double>& event_projections;
};

// The caller retains the regular samples preceding a supplied checkpoint.
std::vector<double> sample(const std::vector<Event>& events,
                           const std::vector<double>& controlpoints,
                           Trace* trace = nullptr, const Prefix* prefix = nullptr);

} // namespace own_track_geometry
