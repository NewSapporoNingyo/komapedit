/*
 * Copyright (c) 2026 Sapporo_ningyo
 *
 * Licensed under Apache License 2.0; see LICENSE and NOTICE.
 */

#pragma once

#include <algorithm>
#include <cstddef>
#include <optional>
#include <vector>

// Curve/Gradient BeginTransition belongs to the next source-order statement
// that consumes it. Keep this model-independent so parser validation, editing,
// and every canvas use exactly the same pairing semantics.
namespace own_track_transition_linkage {

enum class EventKind {
    CurveBeginTransition,
    CurveBegin,
    CurveBeginCircular,
    CurveEnd,
    CurveOther,
    GradientBeginTransition,
    GradientBegin,
    GradientEnd,
};

struct Event {
    size_t source_index = 0;
    int order = 0;
    int argument_count = 0;
    EventKind kind = EventKind::CurveOther;
};

struct Pair {
    size_t transition_source_index = 0;
    size_t primary_source_index = 0;
};

struct Linkage {
    std::vector<Pair> pairs;
    std::vector<size_t> orphan_transition_source_indices;
};

inline bool consumes_curve_transition(const Event& event) {
    return (event.kind == EventKind::CurveBegin && event.argument_count == 2) ||
        event.kind == EventKind::CurveBeginCircular ||
        event.kind == EventKind::CurveEnd;
}

inline bool consumes_gradient_transition(const Event& event) {
    return event.kind == EventKind::GradientBegin ||
        event.kind == EventKind::GradientEnd;
}

inline Linkage pair_transitions(std::vector<Event> events) {
    std::stable_sort(events.begin(), events.end(), [](const Event& left, const Event& right) {
        if (left.order != right.order) return left.order < right.order;
        return left.source_index < right.source_index;
    });

    Linkage result;
    std::optional<size_t> curve_pending;
    std::optional<size_t> gradient_pending;
    auto replace_pending = [&](std::optional<size_t>& pending, size_t source_index) {
        if (pending) result.orphan_transition_source_indices.push_back(*pending);
        pending = source_index;
    };
    auto consume = [&](std::optional<size_t>& pending, size_t primary_source_index) {
        if (!pending) return;
        result.pairs.push_back(Pair{*pending, primary_source_index});
        pending.reset();
    };

    for (const Event& event : events) {
        if (event.kind == EventKind::CurveBeginTransition) {
            replace_pending(curve_pending, event.source_index);
        } else if (event.kind == EventKind::GradientBeginTransition) {
            replace_pending(gradient_pending, event.source_index);
        } else if (consumes_curve_transition(event)) {
            consume(curve_pending, event.source_index);
        } else if (consumes_gradient_transition(event)) {
            consume(gradient_pending, event.source_index);
        }
    }
    if (curve_pending) result.orphan_transition_source_indices.push_back(*curve_pending);
    if (gradient_pending) result.orphan_transition_source_indices.push_back(*gradient_pending);
    return result;
}

} // namespace own_track_transition_linkage
