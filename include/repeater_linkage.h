/*
 * Copyright (c) 2026 Sapporo_ningyo
 *
 * Licensed under Apache License 2.0; see LICENSE and NOTICE.
 */

#pragma once

#include <algorithm>
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
    size_t chain_index = 0;
    size_t chain_begin_index = 0;
    size_t chain_begin_count = 1;
    std::optional<size_t> previous_begin_source_index;
    std::optional<size_t> boundary_source_index;
    std::optional<size_t> next_begin_display_index;
    double begin_distance = 0.0;
    double end_distance = 0.0;
    BoundaryKind boundary_kind = BoundaryKind::Open;
};

// A chain is one uninterrupted lifetime of a repeater key.  A later Begin
// after an explicit End starts a new chain even if it uses the same key.
struct Chain {
    std::vector<size_t> begin_source_indices;
    std::vector<size_t> segment_indices;
    std::optional<size_t> end_source_index;
    std::string key;
    double begin_distance = 0.0;
    std::optional<double> end_distance;
};

struct Linkage {
    std::vector<Segment> segments;
    std::vector<Chain> chains;
};

inline std::string canonical_key(std::string text) {
    const auto is_ascii_space = [](unsigned char ch) {
        return ch == ' ' || ch == '\t' || ch == '\n' ||
            ch == '\r' || ch == '\f' || ch == '\v';
    };
    size_t first = 0;
    while (first < text.size() &&
           is_ascii_space(static_cast<unsigned char>(text[first]))) ++first;
    size_t last = text.size();
    while (last > first &&
           is_ascii_space(static_cast<unsigned char>(text[last - 1]))) --last;
    text = text.substr(first, last - first);
    for (char& ch : text) {
        if (ch >= 'A' && ch <= 'Z') ch = static_cast<char>(ch - 'A' + 'a');
    }
    return text;
}

inline Linkage pair_linkage(std::vector<Event> events) {
    std::stable_sort(events.begin(), events.end(), [](const Event& a, const Event& b) {
        if (a.distance < b.distance) return true;
        if (a.distance > b.distance) return false;
        // Repeater lifetimes are half-open intervals.  At a shared distance an
        // End therefore closes the earlier lifetime before any Begin starts a
        // new one, regardless of source parse order.
        const auto kind_rank = [](EventKind kind) {
            if (kind == EventKind::End) return 0;
            if (kind == EventKind::Begin) return 1;
            return 2;
        };
        const int a_rank = kind_rank(a.kind);
        const int b_rank = kind_rank(b.kind);
        if (a_rank < b_rank) return true;
        if (a_rank > b_rank) return false;
        if (a.order < b.order) return true;
        if (a.order > b.order) return false;
        return a.source_index < b.source_index;
    });

    Linkage result;
    struct OpenSegment {
        size_t segment_index = 0;
        size_t chain_index = 0;
    };
    std::map<std::string, OpenSegment> open_segments;
    size_t display_index = 0;
    for (const Event& event : events) {
        const std::string key = canonical_key(event.key);
        if (key.empty()) continue;
        if (event.kind == EventKind::Begin) {
            ++display_index;
            std::optional<size_t> previous_begin_source_index;
            size_t chain_index = 0;
            auto open = open_segments.find(key);
            if (open != open_segments.end()) {
                Segment& previous = result.segments[open->second.segment_index];
                previous.boundary_kind = BoundaryKind::NextBegin;
                previous.boundary_source_index = event.source_index;
                previous.next_begin_display_index = display_index;
                previous_begin_source_index = previous.begin_source_index;
                previous.end_distance = event.distance;
                chain_index = open->second.chain_index;
                open_segments.erase(open);
            } else {
                chain_index = result.chains.size();
                Chain chain;
                chain.key = key;
                chain.begin_distance = event.distance;
                result.chains.push_back(std::move(chain));
            }
            Chain& chain = result.chains[chain_index];
            Segment segment;
            segment.begin_source_index = event.source_index;
            segment.display_index = display_index;
            segment.chain_index = chain_index;
            segment.chain_begin_index = chain.begin_source_indices.size();
            segment.previous_begin_source_index = previous_begin_source_index;
            segment.begin_distance = event.distance;
            result.segments.push_back(std::move(segment));
            const size_t segment_index = result.segments.size() - 1;
            chain.begin_source_indices.push_back(event.source_index);
            chain.segment_indices.push_back(segment_index);
            open_segments.emplace(key, OpenSegment{segment_index, chain_index});
        } else if (event.kind == EventKind::End) {
            auto open = open_segments.find(key);
            if (open == open_segments.end()) continue;
            Segment& segment = result.segments[open->second.segment_index];
            segment.boundary_kind = BoundaryKind::ExplicitEnd;
            segment.boundary_source_index = event.source_index;
            segment.end_distance = event.distance;
            result.chains[open->second.chain_index].end_source_index = event.source_index;
            result.chains[open->second.chain_index].end_distance = event.distance;
            open_segments.erase(open);
        }
    }
    for (size_t chain_index = 0; chain_index < result.chains.size(); ++chain_index) {
        const Chain& chain = result.chains[chain_index];
        for (size_t begin_index = 0; begin_index < chain.segment_indices.size(); ++begin_index) {
            Segment& segment = result.segments[chain.segment_indices[begin_index]];
            segment.chain_index = chain_index;
            segment.chain_begin_index = begin_index;
            segment.chain_begin_count = chain.begin_source_indices.size();
        }
    }
    return result;
}

inline bool half_open_intervals_overlap(const Chain& left, const Chain& right) {
    if (left.end_distance && *left.end_distance <= right.begin_distance) return false;
    if (right.end_distance && *right.end_distance <= left.begin_distance) return false;
    return true;
}

inline bool chain_contains_distance(const Chain& chain, double distance) {
    return distance >= chain.begin_distance &&
        (!chain.end_distance || distance < *chain.end_distance);
}

inline const Chain* chain_at_distance(const Linkage& linkage,
                                      const std::string& key,
                                      double distance,
                                      bool require_explicit_end = false) {
    const std::string canonical = canonical_key(key);
    if (canonical.empty()) return nullptr;
    for (const Chain& chain : linkage.chains) {
        if (chain.key != canonical ||
            (require_explicit_end && !chain.end_source_index) ||
            !chain_contains_distance(chain, distance)) {
            continue;
        }
        return &chain;
    }
    return nullptr;
}

inline std::vector<Segment> pair_segments(std::vector<Event> events) {
    return pair_linkage(std::move(events)).segments;
}

} // namespace repeater_linkage
