/*
 * Copyright (c) 2026 Sapporo_ningyo
 *
 * Licensed under Apache License 2.0; see LICENSE and NOTICE.
 */

#pragma once

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

// Repeater.Begin/Begin0 starts a segment.  A following Begin for the same key
// closes the previous segment (the UI calls that boundary a "change"); an End
// closes it explicitly.  Keep this tiny, model-independent pairing routine in
// one place so maploader, the table, and both canvases agree on the boundary.
namespace repeater_linkage {

enum class EventKind {
    Begin,
    End,
    Other,
};

enum class BoundaryKind {
    ExplicitEnd,
    NextBegin,
    Open,
};

struct Event {
    size_t source_index = 0;
    double distance = 0.0;
    double order = 0.0;
    std::string key;
    EventKind kind = EventKind::Other;
};

struct Segment {
    size_t begin_source_index = 0;
    size_t display_index = 0; // One-based index in Begin order.
    std::optional<size_t> previous_begin_source_index;
    std::optional<size_t> boundary_source_index;
    std::optional<size_t> next_begin_display_index;
    double begin_distance = 0.0;
    double end_distance = 0.0;
    BoundaryKind boundary_kind = BoundaryKind::Open;
};

inline std::string canonical_key(std::string text) {
    size_t first = 0;
    while (first < text.size() && std::isspace(static_cast<unsigned char>(text[first]))) ++first;
    size_t last = text.size();
    while (last > first && std::isspace(static_cast<unsigned char>(text[last - 1]))) --last;
    text = text.substr(first, last - first);
    for (char& ch : text) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return text;
}

inline std::vector<Segment> pair_segments(std::vector<Event> events) {
    std::stable_sort(events.begin(), events.end(), [](const Event& a, const Event& b) {
        if (a.distance < b.distance) return true;
        if (a.distance > b.distance) return false;
        if (a.order < b.order) return true;
        if (a.order > b.order) return false;
        return a.source_index < b.source_index;
    });

    std::vector<Segment> result;
    std::map<std::string, size_t> open_segments;
    size_t display_index = 0;
    for (const Event& event : events) {
        const std::string key = canonical_key(event.key);
        if (key.empty()) continue;
        if (event.kind == EventKind::Begin) {
            ++display_index;
            std::optional<size_t> previous_begin_source_index;
            auto open = open_segments.find(key);
            if (open != open_segments.end()) {
                Segment& previous = result[open->second];
                previous.boundary_kind = BoundaryKind::NextBegin;
                previous.boundary_source_index = event.source_index;
                previous.next_begin_display_index = display_index;
                previous_begin_source_index = previous.begin_source_index;
                previous.end_distance = event.distance;
                open_segments.erase(open);
            }
            Segment segment;
            segment.begin_source_index = event.source_index;
            segment.display_index = display_index;
            segment.previous_begin_source_index = previous_begin_source_index;
            segment.begin_distance = event.distance;
            result.push_back(std::move(segment));
            open_segments.emplace(key, result.size() - 1);
        } else if (event.kind == EventKind::End) {
            auto open = open_segments.find(key);
            if (open == open_segments.end()) continue;
            Segment& segment = result[open->second];
            segment.boundary_kind = BoundaryKind::ExplicitEnd;
            segment.boundary_source_index = event.source_index;
            segment.end_distance = event.distance;
            open_segments.erase(open);
        }
    }
    return result;
}

} // namespace repeater_linkage
