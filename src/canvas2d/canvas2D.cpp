/*
 * Copyright (c) 2026 Sapporo_ningyo
 *
 * Licensed under Apache License 2.0; see LICENSE and NOTICE.
 * The GUI uses Dear ImGui and ImPlot; see THIRD_PARTY_NOTICES.md.
 */

#pragma execution_character_set("utf-8")

#include "kme.h"
#include "canvas3D.h"

#include "imgui.h"
#include "implot.h"

#include <windows.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <iomanip>
#include <limits>
#include <map>
#include <optional>
#include <ostream>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

void set_crosshair_cursor() {
    ::SetCursor(::LoadCursor(nullptr, IDC_CROSS));
}

void set_move_cursor() {
    ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
    ::SetCursor(::LoadCursor(nullptr, IDC_SIZEALL));
}

double angle_lerp(double a, double b, double t) {
    double delta = std::atan2(std::sin(b - a), std::cos(b - a));
    return a + delta * t;
}

double matrix_track_tangent(const Matrix& points, size_t row) {
    if (points.rows < 2 || points.cols < 3) return 0.0;
    size_t a = row == 0 ? 0 : row - 1;
    size_t b = row + 1 < points.rows ? row + 1 : row;
    if (a == b && b + 1 < points.rows) ++b;
    if (a == b) return 0.0;
    double dx = points.at(b, 1) - points.at(a, 1);
    double dy = points.at(b, 2) - points.at(a, 2);
    if (std::abs(dx) < 1e-9 && std::abs(dy) < 1e-9) return 0.0;
    return std::atan2(dy, dx);
}

TrackPoint matrix_row_track_point(const Matrix& points, size_t row, bool has_theta_column) {
    TrackPoint p;
    p.d = points.at(row, 0);
    p.x = points.at(row, 1);
    p.y = points.at(row, 2);
    p.z = points.cols > 3 ? points.at(row, 3) : 0.0;
    p.theta = has_theta_column && points.cols > 4 ? points.at(row, 4) : matrix_track_tangent(points, row);
    if (points.cols > 5) p.radius = points.at(row, 5);
    if (points.cols > 6) p.gradient = points.at(row, 6);
    return p;
}

std::optional<TrackPoint> sample_matrix_track_point(const Matrix& points, double distance, bool has_theta_column) {
    if (points.empty() || points.cols < 3) return std::nullopt;
    double first = points.at(0, 0);
    double last = points.at(points.rows - 1, 0);
    constexpr double eps = 1e-6;
    if (distance < first - eps || distance > last + eps) return std::nullopt;
    if (distance <= first) return matrix_row_track_point(points, 0, has_theta_column);
    if (distance >= last) return matrix_row_track_point(points, points.rows - 1, has_theta_column);

    size_t lo = 0;
    size_t hi = points.rows;
    while (lo < hi) {
        size_t mid = (lo + hi) / 2;
        if (points.at(mid, 0) < distance) lo = mid + 1;
        else hi = mid;
    }
    if (lo == 0) return matrix_row_track_point(points, 0, has_theta_column);
    size_t a_row = lo - 1;
    size_t b_row = std::min(lo, points.rows - 1);
    TrackPoint a = matrix_row_track_point(points, a_row, has_theta_column);
    TrackPoint b = matrix_row_track_point(points, b_row, has_theta_column);
    double span = b.d - a.d;
    double t = std::abs(span) < eps ? 0.0 : std::clamp((distance - a.d) / span, 0.0, 1.0);

    TrackPoint p;
    p.d = distance;
    p.x = a.x + (b.x - a.x) * t;
    p.y = a.y + (b.y - a.y) * t;
    p.z = a.z + (b.z - a.z) * t;
    p.theta = has_theta_column ? angle_lerp(a.theta, b.theta, t) : std::atan2(b.y - a.y, b.x - a.x);
    p.radius = a.radius + (b.radius - a.radius) * t;
    p.gradient = a.gradient + (b.gradient - a.gradient) * t;
    return p;
}

struct TrackSource {
    const Matrix* points = nullptr;
    bool has_theta_column = false;
};

TrackPoint offset_track_point(TrackPoint base, double lateral, double forward) {
    double c = std::cos(base.theta);
    double s = std::sin(base.theta);
    base.x += c * forward - s * lateral;
    base.y += s * forward + c * lateral;
    return base;
}

size_t matrix_lower_bound_distance(const Matrix& points, double distance) {
    size_t lo = 0;
    size_t hi = points.rows;
    while (lo < hi) {
        size_t mid = (lo + hi) / 2;
        if (points.at(mid, 0) < distance) lo = mid + 1;
        else hi = mid;
    }
    return lo;
}

size_t matrix_upper_bound_distance(const Matrix& points, double distance) {
    size_t lo = 0;
    size_t hi = points.rows;
    while (lo < hi) {
        size_t mid = (lo + hi) / 2;
        if (points.at(mid, 0) <= distance) lo = mid + 1;
        else hi = mid;
    }
    return lo;
}

constexpr size_t kRepeaterSegmentChunkPointLimit = 192;

void include_repeater_chunk_bounds(PlanRepeaterSegment::Chunk& chunk, const TrackPoint& p) {
    if (!chunk.bounds_valid) {
        chunk.d_min = chunk.d_max = p.d;
        chunk.x_min = chunk.x_max = p.x;
        chunk.y_min = chunk.y_max = p.y;
        chunk.bounds_valid = true;
        return;
    }
    chunk.d_min = std::min(chunk.d_min, p.d);
    chunk.d_max = std::max(chunk.d_max, p.d);
    chunk.x_min = std::min(chunk.x_min, p.x);
    chunk.x_max = std::max(chunk.x_max, p.x);
    chunk.y_min = std::min(chunk.y_min, p.y);
    chunk.y_max = std::max(chunk.y_max, p.y);
}

void include_repeater_segment_bounds(PlanRepeaterSegment& segment, const TrackPoint& p) {
    if (!segment.bounds_valid) {
        segment.d_min = segment.d_max = p.d;
        segment.x_min = segment.x_max = p.x;
        segment.y_min = segment.y_max = p.y;
        segment.bounds_valid = true;
        return;
    }
    segment.d_min = std::min(segment.d_min, p.d);
    segment.d_max = std::max(segment.d_max, p.d);
    segment.x_min = std::min(segment.x_min, p.x);
    segment.x_max = std::max(segment.x_max, p.x);
    segment.y_min = std::min(segment.y_min, p.y);
    segment.y_max = std::max(segment.y_max, p.y);
}

void append_repeater_segment_point(PlanRepeaterSegment& segment, const TrackPoint& p) {
    if (!segment.chunks.empty()) {
        PlanRepeaterSegment::Chunk& tail_chunk = segment.chunks.back();
        if (!tail_chunk.points.empty() && std::abs(tail_chunk.points.back().d - p.d) < 1e-6) {
            tail_chunk.points.back() = p;
            include_repeater_chunk_bounds(tail_chunk, p);
            include_repeater_segment_bounds(segment, p);
            return;
        }
    }

    if (segment.chunks.empty() ||
        segment.chunks.back().points.size() >= kRepeaterSegmentChunkPointLimit) {
        std::optional<TrackPoint> tail;
        if (!segment.chunks.empty() && !segment.chunks.back().points.empty()) {
            tail = segment.chunks.back().points.back();
        }
        segment.chunks.emplace_back();
        if (tail) {
            segment.chunks.back().points.push_back(*tail);
            include_repeater_chunk_bounds(segment.chunks.back(), *tail);
        }
    }

    PlanRepeaterSegment::Chunk& chunk = segment.chunks.back();
    chunk.points.push_back(p);
    include_repeater_chunk_bounds(chunk, p);
    include_repeater_segment_bounds(segment, p);
}

#ifndef NDEBUG
void debug_plan_stage(const char* stage) {
    if (!g_debug_plan_benchmark_log) return;
    static std::chrono::steady_clock::time_point frame_start;
    static std::chrono::steady_clock::time_point previous;
    auto now = std::chrono::steady_clock::now();
    if (std::string_view(stage) == "start") {
        frame_start = now;
        previous = now;
    }
    double total_ms = std::chrono::duration<double, std::milli>(now - frame_start).count();
    double delta_ms = std::chrono::duration<double, std::milli>(now - previous).count();
    previous = now;
    *g_debug_plan_benchmark_log << "render_stage=" << stage
                                << " total_ms=" << std::fixed << std::setprecision(3) << total_ms
                                << " delta_ms=" << delta_ms << "\n";
    g_debug_plan_benchmark_log->flush();
}
#else
void debug_plan_stage(const char*) {}
#endif

} // namespace

void App::rebuild_marker_overlay_cache() {
    structure_marker_cache_.clear();
    repeater_marker_cache_.clear();
    signal_marker_cache_.clear();
    beacon_marker_cache_.clear();
    pretrain_marker_cache_.clear();
    irregularity_marker_cache_.clear();
    map_sound_marker_cache_.clear();
    map_sound_3d_marker_cache_.clear();
    rolling_noise_marker_cache_.clear();
    flange_noise_marker_cache_.clear();
    joint_noise_marker_cache_.clear();
    background_marker_cache_.clear();
    adhesion_marker_cache_.clear();
    cab_illuminance_marker_cache_.clear();
    fog_marker_cache_.clear();
    if (!has_model_ || model_.own.empty()) return;

    std::map<std::string, TrackSource> track_sources;
    TrackSource own_source{&model_.own, true};
    for (const char* key : kOwnTrackLookupAliases) {
        track_sources[normalize_track_lookup_key(key)] = own_source;
    }
    for (const auto& track : model_.other_tracks) {
        track_sources[normalize_track_lookup_key(track.key)] = TrackSource{&track.points, false};
    }

    auto find_track_source = [&](const std::string& normalized_key) -> std::optional<TrackSource> {
        auto it = track_sources.find(normalized_key);
        if (it == track_sources.end() || !it->second.points) return std::nullopt;
        return it->second;
    };

    auto sample_track_base = [&](const std::string& key, double distance) -> std::optional<TrackPoint> {
        std::string normalized_key = normalize_track_lookup_key(key);
        if (auto source = find_track_source(normalized_key)) {
            auto sampled = sample_matrix_track_point(*source->points, distance, source->has_theta_column);
            if (sampled) return sampled;
        }
        if (is_own_track_lookup_alias(normalized_key)) {
            return sample_matrix_track_point(*own_source.points, distance, own_source.has_theta_column);
        }
        return std::nullopt;
    };

    auto sample_track = [&](const std::string& key, double distance, double lateral, double forward) -> std::optional<TrackPoint> {
        auto sampled = sample_track_base(key, distance);
        if (!sampled) return std::nullopt;
        return offset_track_point(*sampled, lateral, forward);
    };

    auto make_marker = [](double distance, const TrackPoint& p, const std::string& label, size_t row_index) {
        PlanStructureMarker marker;
        marker.d = distance;
        marker.x = p.x;
        marker.y = p.y;
        marker.label = label;
        marker.row_index = row_index;
        return marker;
    };

    structure_marker_cache_.reserve(model_.structures.size() + model_.structures_between.size());
    for (const auto& row : model_.structures) {
        double distance = table_cell_number(row, "distance");
        double lateral = table_cell_number(row, "x");
        double forward = table_cell_number(row, "z");
        if (auto p = sample_track(table_cell(row, "trackKey"), distance, lateral, forward)) {
            structure_marker_cache_.push_back(make_marker(distance, *p, table_cell(row, "structureKey"),
                                                          structure_marker_cache_.size()));
        } else {
            structure_marker_cache_.push_back(std::nullopt);
        }
    }
    for (const auto& row : model_.structures_between) {
        double distance = table_cell_number(row, "distance");
        auto p1 = sample_track(table_cell(row, "trackKey1"), distance, 0.0, 0.0);
        auto p2 = sample_track(table_cell(row, "trackKey2"), distance, 0.0, 0.0);
        if (!p1 && !p2) {
            structure_marker_cache_.push_back(std::nullopt);
            continue;
        }
        TrackPoint p = p1 ? *p1 : *p2;
        if (p1 && p2) {
            p.x = (p1->x + p2->x) * 0.5;
            p.y = (p1->y + p2->y) * 0.5;
            p.z = (p1->z + p2->z) * 0.5;
            p.theta = angle_lerp(p1->theta, p2->theta, 0.5);
        }
        structure_marker_cache_.push_back(make_marker(distance, p, table_cell(row, "structureKey"),
                                                      structure_marker_cache_.size()));
    }

    signal_marker_cache_.reserve(model_.signals.size());
    for (const auto& row : model_.signals) {
        double distance = table_cell_number(row, "distance");
        double lateral = table_cell_number(row, "x");
        double forward = table_cell_number(row, "z");
        if (auto p = sample_track(table_cell(row, "trackKey"), distance, lateral, forward)) {
            PlanSignalMarker marker;
            marker.d = distance;
            marker.x = p->x;
            marker.y = p->y;
            marker.label = table_cell(row, "signalAspectKey");
            if (marker.label.empty()) marker.label = "#" + std::to_string(signal_marker_cache_.size() + 1);
            marker.row_index = signal_marker_cache_.size();
            signal_marker_cache_.push_back(std::move(marker));
        } else {
            signal_marker_cache_.push_back(std::nullopt);
        }
    }

    beacon_marker_cache_.reserve(model_.beacons.size());
    for (const auto& row : model_.beacons) {
        double distance = table_cell_number(row, "distance");
        if (auto p = sample_track("", distance, 0.0, 0.0)) {
            PlanBeaconMarker marker;
            marker.d = distance;
            marker.x = p->x;
            marker.y = p->y;
            marker.label = table_cell(row, "type");
            if (marker.label.empty()) marker.label = "#" + std::to_string(beacon_marker_cache_.size() + 1);
            marker.row_index = beacon_marker_cache_.size();
            beacon_marker_cache_.push_back(std::move(marker));
        } else {
            beacon_marker_cache_.push_back(std::nullopt);
        }
    }

    pretrain_marker_cache_.reserve(model_.pretrains.size());
    for (const auto& row : model_.pretrains) {
        double distance = table_cell_number(row, "distance");
        if (auto p = sample_track("", distance, 0.0, 0.0)) {
            PlanPreTrainMarker marker;
            marker.d = distance;
            marker.x = p->x;
            marker.y = p->y;
            marker.label = table_cell(row, "passTime");
            if (marker.label.empty()) marker.label = "#" + std::to_string(pretrain_marker_cache_.size() + 1);
            marker.row_index = pretrain_marker_cache_.size();
            pretrain_marker_cache_.push_back(std::move(marker));
        } else {
            pretrain_marker_cache_.push_back(std::nullopt);
        }
    }

    irregularity_marker_cache_.reserve(model_.irregularities.size());
    for (const auto& row : model_.irregularities) {
        double distance = table_cell_number(row, "distance");
        if (auto p = sample_track("", distance, 0.0, 0.0)) {
            PlanIrregularityMarker marker;
            marker.d = distance;
            marker.x = p->x;
            marker.y = p->y;
            marker.label = "#" + std::to_string(irregularity_marker_cache_.size() + 1);
            marker.row_index = irregularity_marker_cache_.size();
            irregularity_marker_cache_.push_back(std::move(marker));
        } else {
            irregularity_marker_cache_.push_back(std::nullopt);
        }
    }

    map_sound_marker_cache_.reserve(model_.map_sounds.size());
    for (const auto& row : model_.map_sounds) {
        double distance = table_cell_number(row, "distance");
        if (auto p = sample_track("", distance, 0.0, 0.0)) {
            PlanMapSoundMarker marker;
            marker.d = distance;
            marker.x = p->x;
            marker.y = p->y;
            marker.label = table_cell(row, "soundKey");
            if (marker.label.empty()) marker.label = "#" + std::to_string(map_sound_marker_cache_.size() + 1);
            marker.row_index = map_sound_marker_cache_.size();
            map_sound_marker_cache_.push_back(std::move(marker));
        } else {
            map_sound_marker_cache_.push_back(std::nullopt);
        }
    }

    map_sound_3d_marker_cache_.reserve(model_.map_sound_3d.size());
    for (const auto& row : model_.map_sound_3d) {
        double distance = table_cell_number(row, "distance");
        if (auto p = sample_track("", distance, 0.0, 0.0)) {
            PlanMapSound3DMarker marker;
            marker.d = distance;
            marker.x = p->x;
            marker.y = p->y;
            marker.label = table_cell(row, "soundKey");
            if (marker.label.empty()) marker.label = "#" + std::to_string(map_sound_3d_marker_cache_.size() + 1);
            marker.row_index = map_sound_3d_marker_cache_.size();
            map_sound_3d_marker_cache_.push_back(std::move(marker));
        } else {
            map_sound_3d_marker_cache_.push_back(std::nullopt);
        }
    }

    rolling_noise_marker_cache_.reserve(model_.rolling_noises.size());
    for (const auto& row : model_.rolling_noises) {
        double distance = table_cell_number(row, "distance");
        if (auto p = sample_track("", distance, 0.0, 0.0)) {
            PlanRollingNoiseMarker marker;
            marker.d = distance;
            marker.x = p->x;
            marker.y = p->y;
            marker.label = "#" + std::to_string(rolling_noise_marker_cache_.size() + 1);
            marker.row_index = rolling_noise_marker_cache_.size();
            rolling_noise_marker_cache_.push_back(std::move(marker));
        } else {
            rolling_noise_marker_cache_.push_back(std::nullopt);
        }
    }

    flange_noise_marker_cache_.reserve(model_.flange_noises.size());
    for (const auto& row : model_.flange_noises) {
        double distance = table_cell_number(row, "distance");
        if (auto p = sample_track("", distance, 0.0, 0.0)) {
            PlanFlangeNoiseMarker marker;
            marker.d = distance;
            marker.x = p->x;
            marker.y = p->y;
            marker.label = "#" + std::to_string(flange_noise_marker_cache_.size() + 1);
            marker.row_index = flange_noise_marker_cache_.size();
            flange_noise_marker_cache_.push_back(std::move(marker));
        } else {
            flange_noise_marker_cache_.push_back(std::nullopt);
        }
    }

    joint_noise_marker_cache_.reserve(model_.joint_noises.size());
    for (const auto& row : model_.joint_noises) {
        double distance = table_cell_number(row, "distance");
        if (auto p = sample_track("", distance, 0.0, 0.0)) {
            PlanJointNoiseMarker marker;
            marker.d = distance;
            marker.x = p->x;
            marker.y = p->y;
            marker.label = "#" + std::to_string(joint_noise_marker_cache_.size() + 1);
            marker.row_index = joint_noise_marker_cache_.size();
            joint_noise_marker_cache_.push_back(std::move(marker));
        } else {
            joint_noise_marker_cache_.push_back(std::nullopt);
        }
    }

    background_marker_cache_.reserve(model_.backgrounds.size());
    for (const auto& row : model_.backgrounds) {
        double distance = table_cell_number(row, "distance");
        if (auto p = sample_track("", distance, 0.0, 0.0)) {
            PlanBackgroundMarker marker;
            marker.d = distance;
            marker.x = p->x;
            marker.y = p->y;
            marker.label = table_cell(row, "structureKey");
            if (marker.label.empty()) marker.label = "#" + std::to_string(background_marker_cache_.size() + 1);
            marker.row_index = background_marker_cache_.size();
            background_marker_cache_.push_back(std::move(marker));
        } else {
            background_marker_cache_.push_back(std::nullopt);
        }
    }

    adhesion_marker_cache_.reserve(model_.adhesions.size());
    for (const auto& row : model_.adhesions) {
        double distance = table_cell_number(row, "distance");
        if (auto p = sample_track("", distance, 0.0, 0.0)) {
            PlanAdhesionMarker marker;
            marker.d = distance;
            marker.x = p->x;
            marker.y = p->y;
            marker.label = "#" + std::to_string(adhesion_marker_cache_.size() + 1);
            marker.row_index = adhesion_marker_cache_.size();
            adhesion_marker_cache_.push_back(std::move(marker));
        } else {
            adhesion_marker_cache_.push_back(std::nullopt);
        }
    }

    cab_illuminance_marker_cache_.reserve(model_.cab_illuminance.size());
    for (const auto& row : model_.cab_illuminance) {
        double distance = table_cell_number(row, "distance");
        if (auto p = sample_track("", distance, 0.0, 0.0)) {
            PlanCabIlluminanceMarker marker;
            marker.d = distance;
            marker.x = p->x;
            marker.y = p->y;
            marker.label = "#" + std::to_string(cab_illuminance_marker_cache_.size() + 1);
            marker.row_index = cab_illuminance_marker_cache_.size();
            cab_illuminance_marker_cache_.push_back(std::move(marker));
        } else {
            cab_illuminance_marker_cache_.push_back(std::nullopt);
        }
    }

    fog_marker_cache_.reserve(model_.fogs.size());
    for (const auto& row : model_.fogs) {
        double distance = table_cell_number(row, "distance");
        if (auto p = sample_track("", distance, 0.0, 0.0)) {
            PlanFogMarker marker;
            marker.d = distance;
            marker.x = p->x;
            marker.y = p->y;
            marker.label = "#" + std::to_string(fog_marker_cache_.size() + 1);
            marker.row_index = fog_marker_cache_.size();
            fog_marker_cache_.push_back(std::move(marker));
        } else {
            fog_marker_cache_.push_back(std::nullopt);
        }
    }

    auto build_repeater_segment = [&](const std::string& key, double start, double end,
                                      double lateral, double forward) -> PlanRepeaterSegment {
        PlanRepeaterSegment segment;
        if (end < start) std::swap(start, end);
        std::string normalized_key = normalize_track_lookup_key(key);
        auto source = find_track_source(normalized_key);
        if (source && is_own_track_lookup_alias(normalized_key) &&
            !sample_matrix_track_point(*source->points, start, source->has_theta_column) &&
            !sample_matrix_track_point(*source->points, end, source->has_theta_column)) {
            source = own_source;
        }
        if (!source) return segment;

        auto append_at = [&](double distance) {
            auto sampled = sample_track_base(key, distance);
            if (!sampled) return;
            TrackPoint p = offset_track_point(*sampled, lateral, forward);
            append_repeater_segment_point(segment, p);
        };

        append_at(start);
        size_t first = matrix_upper_bound_distance(*source->points, start);
        size_t last = matrix_lower_bound_distance(*source->points, end);
        for (size_t row_index = first; row_index < last; ++row_index) {
            double distance = source->points->at(row_index, 0);
            append_at(distance);
        }
        append_at(end);
        return segment;
    };

    struct RepeaterBeginState {
        size_t row_index = 0;
        double distance = 0.0;
        std::string track_key;
        double lateral = 0.0;
        double forward = 0.0;
    };
    auto make_repeater_marker = [](double distance, const TrackPoint& p, const std::string& label, size_t row_index) {
        PlanRepeaterMarker marker;
        marker.d = distance;
        marker.x = p.x;
        marker.y = p.y;
        marker.label = label;
        marker.row_index = row_index;
        return marker;
    };
    auto finish_repeater = [&](const RepeaterBeginState& begin, double end_distance, const std::string& label) {
        if (begin.row_index >= repeater_marker_cache_.size()) return;
        RepeaterOverlayRow& overlay = repeater_marker_cache_[begin.row_index];
        if (auto p = sample_track(begin.track_key, end_distance, begin.lateral, begin.forward)) {
            overlay.end_marker = make_repeater_marker(end_distance, *p, label, begin.row_index);
        }
        overlay.segment = build_repeater_segment(begin.track_key, begin.distance, end_distance,
                                                 begin.lateral, begin.forward);
    };

    std::vector<TableRow> repeater_events = model_.repeaters;
    std::stable_sort(repeater_events.begin(), repeater_events.end(), repeater_event_distance_order_less);

    std::map<std::string, RepeaterBeginState> active_repeaters;
    for (const auto& row : repeater_events) {
        std::string key = table_cell(row, "repeaterKey");
        if (key.empty()) continue;
        std::string method = table_cell(row, "method");
        double distance = table_cell_number(row, "distance");
        if (method == "Begin" || method == "Begin0") {
            auto open_it = active_repeaters.find(key);
            if (open_it != active_repeaters.end()) {
                finish_repeater(open_it->second, distance, key);
                active_repeaters.erase(open_it);
            }

            RepeaterBeginState next;
            next.row_index = repeater_marker_cache_.size();
            next.distance = distance;
            next.track_key = table_cell(row, "trackKey");
            next.lateral = table_cell_number(row, "x");
            next.forward = table_cell_number(row, "z");

            RepeaterOverlayRow overlay;
            if (auto p = sample_track(next.track_key, distance, next.lateral, next.forward)) {
                overlay.begin_marker = make_repeater_marker(distance, *p, key, next.row_index);
            }
            repeater_marker_cache_.push_back(std::move(overlay));
            active_repeaters[key] = std::move(next);
        } else if (method == "End") {
            auto open_it = active_repeaters.find(key);
            if (open_it == active_repeaters.end()) continue;
            finish_repeater(open_it->second, distance, key);
            active_repeaters.erase(open_it);
        }
    }
}

size_t App::nearest_own_index(double distance) const {
    if (model_.own.empty()) return 0;
    size_t lo = 0, hi = model_.own.rows;
    while (lo < hi) {
        size_t mid = (lo + hi) / 2;
        if (model_.own.at(mid, 0) < distance) lo = mid + 1;
        else hi = mid;
    }
    if (lo == 0) return 0;
    if (lo >= model_.own.rows) return model_.own.rows - 1;
    double a = std::abs(model_.own.at(lo, 0) - distance);
    double b = std::abs(model_.own.at(lo - 1, 0) - distance);
    return a < b ? lo : lo - 1;
}

double App::interp_own_z(double distance) const {
    if (model_.own.empty()) return 0.0;
    size_t idx = nearest_own_index(distance);
    return model_.own.at(idx, 3) - model_.height_origin;
}

std::optional<TrackPoint> App::track_info_at(double distance) const {
    if (model_.own.empty()) return std::nullopt;
    if (distance < model_.own.at(0, 0) || distance > model_.own.at(model_.own.rows - 1, 0)) return std::nullopt;
    size_t idx = nearest_own_index(distance);
    TrackPoint p;
    p.d = distance;
    p.x = model_.own.at(idx, 1);
    p.y = model_.own.at(idx, 2);
    p.z = model_.own.at(idx, 3) - model_.height_origin;
    p.theta = model_.own.at(idx, 4);
    p.radius = model_.own.at(idx, 5);
    p.gradient = model_.own.at(idx, 6);
    return p;
}

std::optional<SpeedLimit> App::speed_at(double distance) const {
    std::optional<SpeedLimit> result;
    for (const auto& s : model_.speedlimits) {
        if (s.distance > distance) break;
        result = s;
    }
    return result;
}

std::vector<Section> App::curve_sections(bool transition) const {
    std::vector<Section> sections;
    std::vector<TrackEvent> radius;
    for (const auto& e : model_.own_events) {
        if (e.key == "radius") radius.push_back(e);
    }
    for (size_t i = 0; i < radius.size();) {
        const auto& e = radius[i];
        if (!transition && e.flag.empty() && e.value_number && e.number != 0.0) {
            double start = e.distance;
            double value = e.number;
            ++i;
            double end = model_.own.empty() ? start : model_.own.at(model_.own.rows - 1, 0);
            while (i < radius.size()) {
                if (radius[i].flag.empty()) {
                    end = radius[i].distance;
                    break;
                }
                ++i;
            }
            if (start < dmax_ && end > dmin_) {
                sections.push_back({std::max(start, dmin_), std::min(end, dmax_), value});
            }
        } else if (transition && e.flag == "bt") {
            double start = e.distance;
            ++i;
            double end = model_.own.empty() ? start : model_.own.at(model_.own.rows - 1, 0);
            while (i < radius.size()) {
                if (radius[i].flag.empty()) {
                    end = radius[i].distance;
                    break;
                }
                ++i;
            }
            if (start < dmax_ && end > dmin_) {
                sections.push_back({std::max(start, dmin_), std::min(end, dmax_), 0.0});
            }
        } else {
            ++i;
        }
    }
    return sections;
}

static ImVec2 rotate_xy(double x, double y, double angle) {
    double c = std::cos(angle);
    double s = std::sin(angle);
    return ImVec2(static_cast<float>(c * x - s * y), static_cast<float>(s * x + c * y));
}

PlanData App::build_plan_data(bool include_other_tracks) const {
    PlanData out;
    if (!has_model_ || model_.own.empty()) return out;

    size_t own_first = matrix_lower_bound_distance(model_.own, dmin_);
    size_t own_last = matrix_upper_bound_distance(model_.own, dmax_);
    out.own.reserve(own_last > own_first ? own_last - own_first : 0);
    double own_min_d_step = 0.0;
    if (mode_ == Mode::Pan && plan_view_.fitted && plan_view_.scale > 0.0) {
        own_min_d_step = std::max(0.0, 0.45 / std::max(std::abs(plan_view_.scale), 1e-9));
    }
    double last_own_d = -std::numeric_limits<double>::infinity();
    for (size_t r = own_first; r < own_last; ++r) {
        double d = model_.own.at(r, 0);
        bool endpoint = r == own_first || r + 1 == own_last;
        if (!endpoint && d - last_own_d < own_min_d_step) continue;
        TrackPoint p;
        p.d = d;
        p.x = model_.own.at(r, 1);
        p.y = model_.own.at(r, 2);
        p.z = model_.own.at(r, 3);
        p.theta = model_.own.at(r, 4);
        p.radius = model_.own.at(r, 5);
        p.gradient = model_.own.at(r, 6);
        out.own.push_back(p);
        last_own_d = d;
    }
    if (out.own.empty()) return out;
    out.origin_angle = out.own.front().theta;
    double angle = -out.origin_angle;
    auto rotate_point = [angle](TrackPoint p) {
        ImVec2 q = rotate_xy(p.x, p.y, angle);
        p.x = q.x;
        p.y = q.y;
        p.theta += angle;
        return p;
    };
    for (auto& p : out.own) p = rotate_point(p);

    auto extend_bounds = [&](double x, double y) {
        if (out.xmin > out.xmax) {
            out.xmin = out.xmax = x;
            out.ymin = out.ymax = y;
        } else {
            out.xmin = std::min(out.xmin, x);
            out.xmax = std::max(out.xmax, x);
            out.ymin = std::min(out.ymin, y);
            out.ymax = std::max(out.ymax, y);
        }
    };
    out.xmin = 1.0;
    out.xmax = -1.0;
    for (const auto& p : out.own) extend_bounds(p.x, p.y);

    if (include_other_tracks) {
        for (const auto& t : model_.other_tracks) {
            if (!t.visible || t.points.empty()) continue;
            double rmin = std::max(dmin_, t.range_min);
            double rmax = std::min(dmax_, t.range_max);
            size_t first = matrix_lower_bound_distance(t.points, rmin);
            size_t last = matrix_upper_bound_distance(t.points, rmax);
            if (last <= first) continue;
            size_t count = last - first;
            size_t step = std::max<size_t>(1, count / 4096);
            auto append_other_bounds = [&](size_t row) {
                ImVec2 q = rotate_xy(t.points.at(row, 1), t.points.at(row, 2), angle);
                extend_bounds(q.x, q.y);
            };
            for (size_t r = first; r < last; r += step) append_other_bounds(r);
            append_other_bounds(last - 1);
        }
    }

    for (const auto& s : model_.stations) {
        if (s.distance < dmin_ || s.distance > dmax_) continue;
        ImVec2 q = rotate_xy(s.x, s.y, angle);
        out.stations.push_back({s, q.x, q.y});
    }

    for (const auto& s : model_.speedlimits) {
        if (s.distance < dmin_ || s.distance > dmax_) continue;
        size_t idx = nearest_own_index(s.distance);
        TrackPoint p;
        p.x = model_.own.at(idx, 1);
        p.y = model_.own.at(idx, 2);
        p.theta = model_.own.at(idx, 4);
        p = rotate_point(p);
        out.speedlimits.push_back({p.x, p.y, p.theta, s.has_speed, s.speed});
    }

    auto append_marker_bounds = [&](double x, double y) {
        extend_bounds(x, y);
    };
    size_t visible_repeater_count = static_cast<size_t>(std::count_if(
        repeater_row_visible_.begin(), repeater_row_visible_.end(),
        [](unsigned char visible) { return visible != 0; }));
    bool skip_dense_repeater_markers = !include_other_tracks && plan_view_.fitted &&
        visible_repeater_count > 1200 && plan_view_.scale < 0.025;
    out.structure_markers.reserve(std::min(structure_marker_cache_.size(), structure_row_visible_.size()));
    if (!skip_dense_repeater_markers) {
        out.repeater_markers.reserve(std::min(repeater_marker_cache_.size(), repeater_row_visible_.size()) * 2);
    }
    for (size_t i = 0; i < structure_marker_cache_.size() && i < structure_row_visible_.size(); ++i) {
        if (!structure_row_visible_[i] || !structure_marker_cache_[i]) continue;
        const PlanStructureMarker& source = *structure_marker_cache_[i];
        if (source.d < dmin_ || source.d > dmax_) continue;
        TrackPoint p;
        p.x = source.x;
        p.y = source.y;
        p = rotate_point(p);
        PlanStructureMarker marker = source;
        marker.x = p.x;
        marker.y = p.y;
        marker.row_index = i;
        out.structure_markers.push_back(std::move(marker));
        append_marker_bounds(p.x, p.y);
    }

    out.signal_markers.reserve(std::min(signal_marker_cache_.size(), signal_row_visible_.size()));
    for (size_t i = 0; i < signal_marker_cache_.size() && i < signal_row_visible_.size(); ++i) {
        if (!signal_row_visible_[i] || !signal_marker_cache_[i]) continue;
        const PlanSignalMarker& source = *signal_marker_cache_[i];
        if (source.d < dmin_ || source.d > dmax_) continue;
        TrackPoint p;
        p.x = source.x;
        p.y = source.y;
        p = rotate_point(p);
        PlanSignalMarker marker = source;
        marker.x = p.x;
        marker.y = p.y;
        marker.row_index = i;
        out.signal_markers.push_back(std::move(marker));
        append_marker_bounds(p.x, p.y);
    }

    if (show_beacon_markers_) {
        out.beacon_markers.reserve(beacon_marker_cache_.size());
        for (size_t i = 0; i < beacon_marker_cache_.size(); ++i) {
            if (!beacon_marker_cache_[i]) continue;
            const PlanBeaconMarker& source = *beacon_marker_cache_[i];
            if (source.d < dmin_ || source.d > dmax_) continue;
            TrackPoint p;
            p.x = source.x;
            p.y = source.y;
            p = rotate_point(p);
            PlanBeaconMarker marker = source;
            marker.x = p.x;
            marker.y = p.y;
            marker.row_index = i;
            out.beacon_markers.push_back(std::move(marker));
            append_marker_bounds(p.x, p.y);
        }
    }

    if (show_pretrain_markers_) {
        out.pretrain_markers.reserve(pretrain_marker_cache_.size());
        for (size_t i = 0; i < pretrain_marker_cache_.size(); ++i) {
            if (!pretrain_marker_cache_[i]) continue;
            const PlanPreTrainMarker& source = *pretrain_marker_cache_[i];
            if (source.d < dmin_ || source.d > dmax_) continue;
            TrackPoint p;
            p.x = source.x;
            p.y = source.y;
            p = rotate_point(p);
            PlanPreTrainMarker marker = source;
            marker.x = p.x;
            marker.y = p.y;
            marker.row_index = i;
            out.pretrain_markers.push_back(std::move(marker));
            append_marker_bounds(p.x, p.y);
        }
    }

    if (show_irregularity_markers_) {
        out.irregularity_markers.reserve(irregularity_marker_cache_.size());
        for (size_t i = 0; i < irregularity_marker_cache_.size(); ++i) {
            if (!irregularity_marker_cache_[i]) continue;
            const PlanIrregularityMarker& source = *irregularity_marker_cache_[i];
            if (source.d < dmin_ || source.d > dmax_) continue;
            TrackPoint p;
            p.x = source.x;
            p.y = source.y;
            p = rotate_point(p);
            PlanIrregularityMarker marker = source;
            marker.x = p.x;
            marker.y = p.y;
            marker.row_index = i;
            out.irregularity_markers.push_back(std::move(marker));
            append_marker_bounds(p.x, p.y);
        }
    }

    if (show_map_sound_markers_) {
        out.map_sound_markers.reserve(map_sound_marker_cache_.size());
        for (size_t i = 0; i < map_sound_marker_cache_.size(); ++i) {
            if (!map_sound_marker_cache_[i]) continue;
            const PlanMapSoundMarker& source = *map_sound_marker_cache_[i];
            if (source.d < dmin_ || source.d > dmax_) continue;
            TrackPoint p;
            p.x = source.x;
            p.y = source.y;
            p = rotate_point(p);
            PlanMapSoundMarker marker = source;
            marker.x = p.x;
            marker.y = p.y;
            marker.row_index = i;
            out.map_sound_markers.push_back(std::move(marker));
            append_marker_bounds(p.x, p.y);
        }
    }

    if (show_map_sound_3d_markers_) {
        out.map_sound_3d_markers.reserve(map_sound_3d_marker_cache_.size());
        for (size_t i = 0; i < map_sound_3d_marker_cache_.size(); ++i) {
            if (!map_sound_3d_marker_cache_[i]) continue;
            const PlanMapSound3DMarker& source = *map_sound_3d_marker_cache_[i];
            if (source.d < dmin_ || source.d > dmax_) continue;
            TrackPoint p;
            p.x = source.x;
            p.y = source.y;
            p = rotate_point(p);
            PlanMapSound3DMarker marker = source;
            marker.x = p.x;
            marker.y = p.y;
            marker.row_index = i;
            out.map_sound_3d_markers.push_back(std::move(marker));
            append_marker_bounds(p.x, p.y);
        }
    }

    if (show_rolling_noise_markers_) {
        out.rolling_noise_markers.reserve(rolling_noise_marker_cache_.size());
        for (size_t i = 0; i < rolling_noise_marker_cache_.size(); ++i) {
            if (!rolling_noise_marker_cache_[i]) continue;
            const PlanRollingNoiseMarker& source = *rolling_noise_marker_cache_[i];
            if (source.d < dmin_ || source.d > dmax_) continue;
            TrackPoint p;
            p.x = source.x;
            p.y = source.y;
            p = rotate_point(p);
            PlanRollingNoiseMarker marker = source;
            marker.x = p.x;
            marker.y = p.y;
            marker.row_index = i;
            out.rolling_noise_markers.push_back(std::move(marker));
            append_marker_bounds(p.x, p.y);
        }
    }

    if (show_flange_noise_markers_) {
        out.flange_noise_markers.reserve(flange_noise_marker_cache_.size());
        for (size_t i = 0; i < flange_noise_marker_cache_.size(); ++i) {
            if (!flange_noise_marker_cache_[i]) continue;
            const PlanFlangeNoiseMarker& source = *flange_noise_marker_cache_[i];
            if (source.d < dmin_ || source.d > dmax_) continue;
            TrackPoint p;
            p.x = source.x;
            p.y = source.y;
            p = rotate_point(p);
            PlanFlangeNoiseMarker marker = source;
            marker.x = p.x;
            marker.y = p.y;
            marker.row_index = i;
            out.flange_noise_markers.push_back(std::move(marker));
            append_marker_bounds(p.x, p.y);
        }
    }

    if (show_joint_noise_markers_) {
        out.joint_noise_markers.reserve(joint_noise_marker_cache_.size());
        for (size_t i = 0; i < joint_noise_marker_cache_.size(); ++i) {
            if (!joint_noise_marker_cache_[i]) continue;
            const PlanJointNoiseMarker& source = *joint_noise_marker_cache_[i];
            if (source.d < dmin_ || source.d > dmax_) continue;
            TrackPoint p;
            p.x = source.x;
            p.y = source.y;
            p = rotate_point(p);
            PlanJointNoiseMarker marker = source;
            marker.x = p.x;
            marker.y = p.y;
            marker.row_index = i;
            out.joint_noise_markers.push_back(std::move(marker));
            append_marker_bounds(p.x, p.y);
        }
    }

    if (show_background_markers_) {
        out.background_markers.reserve(background_marker_cache_.size());
        for (size_t i = 0; i < background_marker_cache_.size(); ++i) {
            if (!background_marker_cache_[i]) continue;
            const PlanBackgroundMarker& source = *background_marker_cache_[i];
            if (source.d < dmin_ || source.d > dmax_) continue;
            TrackPoint p;
            p.x = source.x;
            p.y = source.y;
            p = rotate_point(p);
            PlanBackgroundMarker marker = source;
            marker.x = p.x;
            marker.y = p.y;
            marker.row_index = i;
            out.background_markers.push_back(std::move(marker));
            append_marker_bounds(p.x, p.y);
        }
    }

    if (show_adhesion_markers_) {
        out.adhesion_markers.reserve(adhesion_marker_cache_.size());
        for (size_t i = 0; i < adhesion_marker_cache_.size(); ++i) {
            if (!adhesion_marker_cache_[i]) continue;
            const PlanAdhesionMarker& source = *adhesion_marker_cache_[i];
            if (source.d < dmin_ || source.d > dmax_) continue;
            TrackPoint p;
            p.x = source.x;
            p.y = source.y;
            p = rotate_point(p);
            PlanAdhesionMarker marker = source;
            marker.x = p.x;
            marker.y = p.y;
            marker.row_index = i;
            out.adhesion_markers.push_back(std::move(marker));
            append_marker_bounds(p.x, p.y);
        }
    }

    if (show_cab_illuminance_markers_) {
        out.cab_illuminance_markers.reserve(cab_illuminance_marker_cache_.size());
        for (size_t i = 0; i < cab_illuminance_marker_cache_.size(); ++i) {
            if (!cab_illuminance_marker_cache_[i]) continue;
            const PlanCabIlluminanceMarker& source = *cab_illuminance_marker_cache_[i];
            if (source.d < dmin_ || source.d > dmax_) continue;
            TrackPoint p;
            p.x = source.x;
            p.y = source.y;
            p = rotate_point(p);
            PlanCabIlluminanceMarker marker = source;
            marker.x = p.x;
            marker.y = p.y;
            marker.row_index = i;
            out.cab_illuminance_markers.push_back(std::move(marker));
            append_marker_bounds(p.x, p.y);
        }
    }

    if (show_fog_markers_) {
        out.fog_markers.reserve(fog_marker_cache_.size());
        for (size_t i = 0; i < fog_marker_cache_.size(); ++i) {
            if (!fog_marker_cache_[i]) continue;
            const PlanFogMarker& source = *fog_marker_cache_[i];
            if (source.d < dmin_ || source.d > dmax_) continue;
            TrackPoint p;
            p.x = source.x;
            p.y = source.y;
            p = rotate_point(p);
            PlanFogMarker marker = source;
            marker.x = p.x;
            marker.y = p.y;
            marker.row_index = i;
            out.fog_markers.push_back(std::move(marker));
            append_marker_bounds(p.x, p.y);
        }
    }

    auto append_repeater_marker = [&](const PlanRepeaterMarker& source, size_t row_index) {
        if (source.d < dmin_ || source.d > dmax_) return;
        TrackPoint p;
        p.x = source.x;
        p.y = source.y;
        p = rotate_point(p);
        PlanRepeaterMarker marker = source;
        marker.x = p.x;
        marker.y = p.y;
        marker.row_index = row_index;
        out.repeater_markers.push_back(std::move(marker));
        append_marker_bounds(p.x, p.y);
    };
    for (size_t i = 0; i < repeater_marker_cache_.size() && i < repeater_row_visible_.size(); ++i) {
        if (!repeater_row_visible_[i]) continue;
        const RepeaterOverlayRow& source = repeater_marker_cache_[i];
        if (!skip_dense_repeater_markers) {
            if (source.begin_marker) append_repeater_marker(*source.begin_marker, i);
            if (source.end_marker) append_repeater_marker(*source.end_marker, i);
        }
        if (include_other_tracks && source.segment.bounds_valid) {
            for (const PlanRepeaterSegment::Chunk& chunk : source.segment.chunks) {
                if (!chunk.bounds_valid || chunk.d_max < dmin_ || chunk.d_min > dmax_) continue;
                ImVec2 corners[] = {
                    rotate_xy(chunk.x_min, chunk.y_min, angle),
                    rotate_xy(chunk.x_max, chunk.y_min, angle),
                    rotate_xy(chunk.x_max, chunk.y_max, angle),
                    rotate_xy(chunk.x_min, chunk.y_max, angle),
                };
                for (ImVec2 p : corners) append_marker_bounds(p.x, p.y);
            }
        }
    }

    if (show_curve_values_) {
        out.curve_sections = curve_sections(false);
        out.transition_sections = curve_sections(true);
    }

    double pad = std::max({out.xmax - out.xmin, out.ymax - out.ymin, 1.0}) * 0.05;
    out.xmin -= pad; out.xmax += pad; out.ymin -= pad; out.ymax += pad;
    return out;
}

ProfileData App::build_profile_data() const {
    ProfileData out;
    if (!has_model_ || model_.own.empty()) return out;

    for (size_t r = 0; r < model_.own.rows; ++r) {
        double d = model_.own.at(r, 0);
        if (d < dmin_ || d > dmax_) continue;
        out.own_x.push_back(d);
        out.own_y.push_back(model_.own.at(r, 3) - model_.height_origin);
    }
    if (!out.own_y.empty()) {
        auto [mn, mx] = std::minmax_element(out.own_y.begin(), out.own_y.end());
        if (*mn != *mx) {
            out.ymin = *mn - (*mx - *mn) * 0.2;
            out.ymax = *mx + (*mx - *mn) * 0.1;
        } else {
            out.ymin = *mn - 5.0;
            out.ymax = *mx + 5.0;
        }
    }

    for (size_t r = 0; r < model_.curve.rows; ++r) {
        double d = model_.curve.at(r, 0);
        if (d < dmin_ || d > dmax_) continue;
        out.curve_x.push_back(d);
        double radius = model_.curve.at(r, 1);
        out.curve_y.push_back(radius > 0 ? 1.0 : (radius < 0 ? -1.0 : 0.0));
    }

    if (show_profile_other_) {
        for (const auto& t : model_.other_tracks) {
            if (!t.visible || t.points.empty()) continue;
            ProfileOther po;
            po.key = t.key;
            po.color = t.color;
            double rmin = std::max(dmin_, t.range_min);
            double rmax = std::min(dmax_, t.range_max);
            for (size_t r = 0; r < t.points.rows; ++r) {
                double d = t.points.at(r, 0);
                if (d < rmin || d > rmax) continue;
                po.x.push_back(d);
                po.y.push_back(t.points.at(r, 3) - model_.height_origin);
            }
            if (!po.x.empty()) out.other.push_back(std::move(po));
        }
    }

    for (const auto& s : model_.stations) {
        if (s.distance >= dmin_ && s.distance <= dmax_) out.stations.push_back(s);
    }

    std::vector<TrackEvent> gradients;
    std::vector<TrackEvent> radii;
    for (const auto& e : model_.own_events) {
        if (e.key == "gradient") gradients.push_back(e);
        if (e.key == "radius") radii.push_back(e);
    }
    for (const auto& e : gradients) {
        if (e.distance >= dmin_ && e.distance <= dmax_) {
            out.gradient_points.push_back({e.distance, interp_own_z(e.distance), ""});
        }
    }
    double last_d = model_.own.empty() ? dmin_ : model_.own.at(0, 0);
    double last_g = 0.0;
    bool in_transition = false;
    auto append_gradient_label = [&](double start, double end, double value) {
        double seg_start = std::max(start, dmin_);
        double seg_end = std::min(end, dmax_);
        if (seg_end > seg_start) {
            double mid = (seg_start + seg_end) * 0.5;
            out.gradient_labels.push_back({mid, 0.0, value == 0.0 ? tr("plot.level") : format_double(std::abs(value), 1)});
        }
    };
    for (const auto& e : gradients) {
        if (!in_transition) append_gradient_label(last_d, e.distance, last_g);
        if (e.value_number) last_g = e.number;
        if (e.flag == "bt" || e.flag == "i") in_transition = true;
        else if (e.flag.empty()) in_transition = false;
        last_d = e.distance;
    }
    if (last_d < dmax_) {
        if (!in_transition) append_gradient_label(last_d, dmax_, last_g);
    }

    for (size_t i = 0; i + 1 < radii.size(); ++i) {
        const auto& e = radii[i];
        if (!e.value_number || e.number == 0.0) continue;
        double start = std::max(e.distance, dmin_);
        double end = std::min(radii[i + 1].distance, dmax_);
        if (end > start) {
            out.radius_labels.push_back({(start + end) * 0.5, e.number > 0 ? 1.5 : -1.5, format_double(std::abs(e.number), 0)});
        }
    }
    return out;
}

void App::clear_measure() {
    measure_distance_.reset();
    measure_text_.clear();
}

void App::update_measure(double distance) {
    auto info = track_info_at(distance);
    if (!info) {
        clear_measure();
        return;
    }
    measure_distance_ = distance;
    auto sp = speed_at(distance);
    std::string speed_text = tr("info.no_limit");
    if (sp && sp->has_speed) speed_text = format_double(sp->speed, 0) + " km/h";
    std::ostringstream out;
    out << tr("info.mileage") << ": " << format_double(distance - model_.distance_origin, 0) << "m | "
        << tr("info.elevation") << ": " << format_double(info->z, 1) << "m | "
        << tr("info.gradient") << ": " << format_double(info->gradient, 1) << "‰ | "
        << tr("info.radius") << ": " << format_double(info->radius, 0) << "m | "
        << tr("info.speedlimit") << ": " << speed_text;
    measure_text_ = out.str();
}

void App::center_plan_at_distance(double distance) {
    PlanData pd = build_plan_data(false);
    if (pd.own.empty()) return;
    auto it = std::lower_bound(pd.own.begin(), pd.own.end(), distance, [](const TrackPoint& p, double d) { return p.d < d; });
    if (it == pd.own.end()) {
        --it;
    } else if (it != pd.own.begin() && std::abs((it - 1)->d - distance) < std::abs(it->d - distance)) {
        --it;
    }
    plan_view_.cx = it->x;
    plan_view_.cy = it->y;
    plan_view_.fitted = true;
    keep_plan_view_ = true;
}

std::optional<ImVec2> App::plan_point_from_model_xy(double x, double y) const {
    if (!has_model_ || model_.own.empty()) return std::nullopt;
    double origin_angle = model_.origin_angle;
    for (size_t row = 0; row < model_.own.rows; ++row) {
        double distance = model_.own.at(row, 0);
        if (distance >= dmin_ && distance <= dmax_) {
            origin_angle = model_.own.at(row, 4);
            break;
        }
    }
    return rotate_xy(x, y, -origin_angle);
}

void App::focus_plan_at_model_point(double x, double y) {
    auto p = plan_point_from_model_xy(x, y);
    if (!p) return;
    plan_view_.cx = p->x;
    plan_view_.cy = p->y;
    plan_view_.fitted = true;
    keep_plan_view_ = true;
    show_plots_window_ = true;
    focus_plots_next_ = true;
    plan_focus_arrow_ = *p;
    plan_focus_arrow_until_ = ImGui::GetTime() + 3.0;
}

void App::request_plot_focus(double distance, bool include_profile, bool include_radius) {
    if (include_profile && show_profile_graph_) {
        focus_profile_next_ = true;
        focus_profile_distance_ = distance;
    }
    if (include_radius && show_radius_graph_) {
        focus_radius_next_ = true;
        focus_radius_distance_ = distance;
    }
}

void App::handle_measure_plot_double_click(bool include_profile, bool include_radius) {
    if (mode_ != Mode::Measure || !ImPlot::IsPlotHovered() || !ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) return;
    ImPlotPoint p = ImPlot::GetPlotMousePos();
    if (p.x < dmin_ || p.x > dmax_) return;
    update_measure(p.x);
    center_plan_at_distance(p.x);
    request_plot_focus(p.x, include_profile, include_radius);
}

void App::focus_station(double distance) {
    PlanData pd = build_plan_data(false);
    for (const auto& s : pd.stations) {
        if (std::abs(s.station.distance - distance) < 1e-6) {
            plan_view_.cx = s.x;
            plan_view_.cy = s.y;
            plan_view_.fitted = true;
            keep_plan_view_ = true;
            break;
        }
    }
    request_plot_focus(distance, true, true);
}

std::optional<ImVec2> App::background_uv_from_world(ImVec2 world) const {
    if (bg_width_ <= 0.0 || bg_height_ <= 0.0) return std::nullopt;
    double rot = bg_rotation_deg_ * 3.14159265358979323846 / 180.0;
    double dx = world.x - bg_x_;
    double dy = world.y - bg_y_;
    double local_x = dx * std::cos(rot) + dy * std::sin(rot);
    double local_y = -dx * std::sin(rot) + dy * std::cos(rot);
    return ImVec2(static_cast<float>(local_x / bg_width_), static_cast<float>(local_y / bg_height_));
}

void App::draw_background(ImDrawList* draw, const View2D& view, ImVec2 origin, ImVec2 size) {
    if (!bg_show_ || !bg_image_.srv || bg_width_ <= 0 || bg_height_ <= 0) return;
    double rot = bg_rotation_deg_ * 3.14159265358979323846 / 180.0;
    double c = std::cos(rot);
    double s = std::sin(rot);
    double hw = bg_width_ * 0.5;
    double hh = bg_height_ * 0.5;
    ImVec2 local[4] = {
        ImVec2(static_cast<float>(-hw), static_cast<float>(-hh)),
        ImVec2(static_cast<float>( hw), static_cast<float>(-hh)),
        ImVec2(static_cast<float>( hw), static_cast<float>( hh)),
        ImVec2(static_cast<float>(-hw), static_cast<float>( hh))
    };
    ImVec2 p[4];
    for (int i = 0; i < 4; ++i) {
        double x = bg_x_ + c * local[i].x - s * local[i].y;
        double y = bg_y_ + s * local[i].x + c * local[i].y;
        p[i] = view.world_to_screen(x, y, origin, size);
    }
    draw->AddImageQuad((void*)bg_image_.srv, p[0], p[1], p[2], p[3]);
}

void App::apply_background_alignment() {
    if (!has_model_ || model_.stations.size() < 2 || !align_pick1_ || !align_pick2_) return;
    if (bg_image_.path.empty() || bg_width_ <= 0.0 || bg_height_ <= 0.0) return;
    align_station1_ = std::clamp(align_station1_, 0, static_cast<int>(model_.stations.size()) - 1);
    align_station2_ = std::clamp(align_station2_, 0, static_cast<int>(model_.stations.size()) - 1);
    PlanData pd = build_plan_data(false);
    auto find_station = [&](const std::string& key) -> std::optional<ImVec2> {
        for (const auto& s : pd.stations) {
            if (s.station.key == key) return ImVec2(static_cast<float>(s.x), static_cast<float>(s.y));
        }
        return std::nullopt;
    };
    auto s1 = find_station(model_.stations[align_station1_].key);
    auto s2 = find_station(model_.stations[align_station2_].key);
    if (!s1 || !s2) return;

    double dsx = s2->x - s1->x;
    double dsy = s2->y - s1->y;
    double ds_dist = std::hypot(dsx, dsy);
    if (ds_dist < 1e-6) return;

    ImVec2 q1 = *align_pick1_;
    ImVec2 q2 = *align_pick2_;
    ImVec2 u1(static_cast<float>(q1.x * bg_width_), static_cast<float>(q1.y * bg_height_));
    ImVec2 u2(static_cast<float>(q2.x * bg_width_), static_cast<float>(q2.y * bg_height_));
    double du = u2.x - u1.x;
    double dv = u2.y - u1.y;
    double duv_dist = std::hypot(du, dv);
    if (duv_dist < 1e-6) return;
    double scale = ds_dist / duv_dist;
    double angle_duv = std::atan2(dv, du);
    double angle_ds = std::atan2(dsy, dsx);
    double new_rot = angle_ds - angle_duv;
    double cosr = std::cos(new_rot);
    double sinr = std::sin(new_rot);
    double sx_u1 = scale * (u1.x * cosr - u1.y * sinr);
    double sy_u1 = scale * (u1.x * sinr + u1.y * cosr);
    bg_x_ = s1->x - sx_u1;
    bg_y_ = s1->y - sy_u1;
    bg_width_ *= scale;
    bg_height_ *= scale;
    bg_rotation_deg_ = std::fmod(new_rot * 180.0 / 3.14159265358979323846 + 360.0, 360.0);
    sync_pending_background_values();
    save_current_background_to_history();
}

void App::render_mode_grid_controls() {
    ImGui::PushID("PlanModeGridControls");
    ImGui::AlignTextToFramePadding();
    ImGui::Text("%s:", tr("frame.mode").c_str());
    ImGui::SameLine();
    Mode previous_mode = mode_;
    int mode = mode_ == Mode::Pan ? 0 : 1;
    if (ImGui::RadioButton(tr("mode.pan").c_str(), &mode, 0)) mode_ = Mode::Pan;
    ImGui::SameLine();
    if (ImGui::RadioButton(tr("mode.measure").c_str(), &mode, 1)) mode_ = Mode::Measure;
    if (previous_mode != mode_ && mode_ == Mode::Pan) clear_measure();

    ImGui::SameLine(0.0f, ImGui::GetStyle().ItemSpacing.x * 2.0f);
    ImGui::AlignTextToFramePadding();
    ImGui::Text("%s:", tr("frame.grid").c_str());
    ImGui::SameLine();
    int grid = grid_mode_ == GridMode::Fixed ? 0 : (grid_mode_ == GridMode::Movable ? 1 : 2);
    if (ImGui::RadioButton(tr("grid.fixed").c_str(), &grid, 0)) grid_mode_ = GridMode::Fixed;
    ImGui::SameLine();
    if (ImGui::RadioButton(tr("grid.movable").c_str(), &grid, 1)) grid_mode_ = GridMode::Movable;
    ImGui::SameLine();
    if (ImGui::RadioButton(tr("grid.none").c_str(), &grid, 2)) grid_mode_ = GridMode::None;
    ImGui::PopID();
}

static ImU32 color_u32(const ImVec4& color) {
    return ImGui::ColorConvertFloat4ToU32(color);
}

constexpr size_t kPolylineChunkPointLimit = 4096;
constexpr float kPolylineMinPixelStepSq = 0.64f;

static bool finite_screen_point(ImVec2 p) {
    return std::isfinite(p.x) && std::isfinite(p.y);
}

struct PlanScreenTransform {
    double model_c = 1.0;
    double model_s = 0.0;
    double view_c = 1.0;
    double view_s = 0.0;
    double cx = 0.0;
    double cy = 0.0;
    double scale = 1.0;
    float screen_cx = 0.0f;
    float screen_cy = 0.0f;

    ImVec2 plan_to_screen(double x, double y) const {
        double dx = x - cx;
        double dy = y - cy;
        double rx = view_c * dx - view_s * dy;
        double ry = view_s * dx + view_c * dy;
        return ImVec2(screen_cx + static_cast<float>(rx * scale),
                      screen_cy + static_cast<float>(ry * scale));
    }

    ImVec2 model_to_screen(double x, double y) const {
        double px = model_c * x - model_s * y;
        double py = model_s * x + model_c * y;
        return plan_to_screen(px, py);
    }
};

static PlanScreenTransform make_plan_transform(const View2D& view, double model_angle,
                                               ImVec2 origin, ImVec2 size) {
    PlanScreenTransform transform;
    transform.model_c = std::cos(model_angle);
    transform.model_s = std::sin(model_angle);
    transform.view_c = std::cos(view.rotation);
    transform.view_s = std::sin(view.rotation);
    transform.cx = view.cx;
    transform.cy = view.cy;
    transform.scale = view.scale;
    transform.screen_cx = origin.x + size.x * 0.5f;
    transform.screen_cy = origin.y + size.y * 0.5f;
    return transform;
}

class ScreenPolylineBuilder {
public:
    ScreenPolylineBuilder(ImDrawList* draw, ImVec2 origin, ImVec2 size, ImU32 color, float thickness)
        : draw_(draw), origin_(origin), size_(size), color_(color), thickness_(thickness) {
        points_.reserve(kPolylineChunkPointLimit);
        reset_bounds();
    }

    void append(ImVec2 p) {
        if (!finite_screen_point(p)) {
            flush(false);
            has_last_ = false;
            has_pending_ = false;
            return;
        }

        if (!has_last_) {
            push_raw(p);
            last_ = p;
            has_last_ = true;
            return;
        }

        float dx = p.x - last_.x;
        float dy = p.y - last_.y;
        if (dx * dx + dy * dy >= kPolylineMinPixelStepSq) {
            push_raw(p);
            last_ = p;
            has_pending_ = false;
            if (points_.size() >= kPolylineChunkPointLimit) flush(true);
        } else {
            pending_ = p;
            has_pending_ = true;
        }
    }

    void finish() {
        if (has_pending_) {
            push_raw(pending_);
            has_pending_ = false;
        }
        flush(false);
    }

    void break_line() {
        flush(false);
        has_last_ = false;
        has_pending_ = false;
    }

private:
    void push_raw(ImVec2 p) {
        points_.push_back(p);
        min_x_ = std::min(min_x_, p.x);
        min_y_ = std::min(min_y_, p.y);
        max_x_ = std::max(max_x_, p.x);
        max_y_ = std::max(max_y_, p.y);
    }

    void reset_bounds() {
        min_x_ = std::numeric_limits<float>::max();
        min_y_ = std::numeric_limits<float>::max();
        max_x_ = -std::numeric_limits<float>::max();
        max_y_ = -std::numeric_limits<float>::max();
    }

    bool overlaps_canvas() const {
        const float margin = 32.0f;
        return !(max_x_ < origin_.x - margin || min_x_ > origin_.x + size_.x + margin ||
                 max_y_ < origin_.y - margin || min_y_ > origin_.y + size_.y + margin);
    }

    void flush(bool keep_tail) {
        if (points_.size() >= 2 && overlaps_canvas()) {
            draw_->AddPolyline(points_.data(), static_cast<int>(points_.size()), color_, thickness_, ImDrawFlags_None);
        }
        ImVec2 tail = points_.empty() ? ImVec2(0.0f, 0.0f) : points_.back();
        points_.clear();
        reset_bounds();
        if (keep_tail) push_raw(tail);
    }

    ImDrawList* draw_ = nullptr;
    ImVec2 origin_;
    ImVec2 size_;
    ImU32 color_ = 0;
    float thickness_ = 1.0f;
    std::vector<ImVec2> points_;
    ImVec2 last_ = ImVec2(0.0f, 0.0f);
    ImVec2 pending_ = ImVec2(0.0f, 0.0f);
    bool has_last_ = false;
    bool has_pending_ = false;
    float min_x_ = 0.0f;
    float min_y_ = 0.0f;
    float max_x_ = 0.0f;
    float max_y_ = 0.0f;
};

static void draw_polyline_range(ImDrawList* draw, const std::vector<TrackPoint>& points,
                                size_t first, size_t last, const PlanScreenTransform& transform,
                                ImVec2 origin, ImVec2 size, ImU32 color, float thickness) {
    if (last <= first + 1 || first >= points.size()) return;
    last = std::min(last, points.size());
    ScreenPolylineBuilder builder(draw, origin, size, color, thickness);
    double min_d_step = std::max(0.0, 0.45 / std::max(std::abs(transform.scale), 1e-9));
    double last_appended_d = -std::numeric_limits<double>::infinity();
    for (size_t i = first; i < last; ++i) {
        bool endpoint = i == first || i + 1 == last;
        if (!endpoint && points[i].d - last_appended_d < min_d_step) continue;
        builder.append(transform.plan_to_screen(points[i].x, points[i].y));
        last_appended_d = points[i].d;
    }
    builder.finish();
}

static void draw_polyline(ImDrawList* draw, const std::vector<TrackPoint>& points,
                          const PlanScreenTransform& transform, ImVec2 origin, ImVec2 size,
                          ImU32 color, float thickness) {
    draw_polyline_range(draw, points, 0, points.size(), transform, origin, size, color, thickness);
}

static void draw_matrix_plan_polyline(ImDrawList* draw, const Matrix& points, double rmin, double rmax,
                                      const PlanScreenTransform& transform, ImVec2 origin, ImVec2 size,
                                      ImU32 color, float thickness) {
    if (points.rows < 2 || points.cols < 3 || rmax < rmin) return;
    size_t first = matrix_lower_bound_distance(points, rmin);
    size_t last = matrix_upper_bound_distance(points, rmax);
    if (last <= first + 1) return;

    ScreenPolylineBuilder builder(draw, origin, size, color, thickness);
    double min_d_step = std::max(0.0, 0.45 / std::max(std::abs(transform.scale), 1e-9));
    double last_appended_d = -std::numeric_limits<double>::infinity();
    for (size_t row = first; row < last; ++row) {
        double d = points.at(row, 0);
        bool endpoint = row == first || row + 1 == last;
        if (!endpoint && d - last_appended_d < min_d_step) continue;
        builder.append(transform.model_to_screen(points.at(row, 1), points.at(row, 2)));
        last_appended_d = d;
    }
    builder.finish();
}

static bool distance_ranges_overlap(double a_min, double a_max, double b_min, double b_max) {
    return a_max >= b_min && a_min <= b_max;
}

static bool screen_bounds_overlap_canvas(const PlanScreenTransform& transform,
                                         double x_min, double y_min, double x_max, double y_max,
                                         ImVec2 origin, ImVec2 size, float margin) {
    ImVec2 corners[] = {
        transform.model_to_screen(x_min, y_min),
        transform.model_to_screen(x_max, y_min),
        transform.model_to_screen(x_max, y_max),
        transform.model_to_screen(x_min, y_max),
    };
    if (!finite_screen_point(corners[0])) return true;
    float min_x = corners[0].x;
    float max_x = corners[0].x;
    float min_y = corners[0].y;
    float max_y = corners[0].y;
    for (int i = 1; i < IM_ARRAYSIZE(corners); ++i) {
        if (!finite_screen_point(corners[i])) return true;
        min_x = std::min(min_x, corners[i].x);
        max_x = std::max(max_x, corners[i].x);
        min_y = std::min(min_y, corners[i].y);
        max_y = std::max(max_y, corners[i].y);
    }
    return !(max_x < origin.x - margin || min_x > origin.x + size.x + margin ||
             max_y < origin.y - margin || min_y > origin.y + size.y + margin);
}

static void draw_repeater_segment_chunks(ImDrawList* draw,
                                         const std::vector<RepeaterOverlayRow>& rows,
                                         const std::vector<unsigned char>& visible,
                                         double dmin, double dmax,
                                         const PlanScreenTransform& transform,
                                         ImVec2 origin, ImVec2 size,
                                         ImU32 color, float thickness) {
    const size_t row_count = std::min(rows.size(), visible.size());
    if (row_count == 0) return;
    size_t visible_row_count = 0;
    for (size_t row = 0; row < row_count; ++row) {
        if (visible[row]) ++visible_row_count;
    }
    if (visible_row_count == 0) return;

    constexpr float coarse_margin = 96.0f;
    bool dense_overlay = visible_row_count > 1200 && transform.scale < 0.05;
    double detail_pixel_step = 0.45;
    if (visible_row_count > 1200 && transform.scale < 0.025) {
        detail_pixel_step = 32.0;
    } else if (dense_overlay) {
        detail_pixel_step = 4.0;
    }
    ImDrawListFlags old_flags = draw->Flags;
    if (dense_overlay) draw->Flags &= ~ImDrawListFlags_AntiAliasedLines;
    ScreenPolylineBuilder builder(draw, origin, size, color, thickness);
    double min_d_step = std::max(0.0, detail_pixel_step / std::max(std::abs(transform.scale), 1e-9));
    for (size_t row = 0; row < row_count; ++row) {
        if (!visible[row]) continue;
        const PlanRepeaterSegment& segment = rows[row].segment;
        if (!segment.bounds_valid || !distance_ranges_overlap(segment.d_min, segment.d_max, dmin, dmax)) continue;
        if (!screen_bounds_overlap_canvas(transform, segment.x_min, segment.y_min, segment.x_max, segment.y_max,
                                          origin, size, coarse_margin)) {
            continue;
        }

        bool segment_open = false;
        double last_appended_d = -std::numeric_limits<double>::infinity();
        for (const PlanRepeaterSegment::Chunk& chunk : segment.chunks) {
            bool chunk_visible = chunk.bounds_valid &&
                distance_ranges_overlap(chunk.d_min, chunk.d_max, dmin, dmax) &&
                screen_bounds_overlap_canvas(transform, chunk.x_min, chunk.y_min, chunk.x_max, chunk.y_max,
                                             origin, size, coarse_margin);
            if (!chunk_visible) {
                if (segment_open) {
                    builder.break_line();
                    segment_open = false;
                }
                continue;
            }

            bool contains_segment_end = std::abs(chunk.d_max - segment.d_max) < 1e-6;
            if (dense_overlay && std::isfinite(last_appended_d) && !contains_segment_end &&
                chunk.d_max - last_appended_d < min_d_step) {
                continue;
            }

            bool appended = false;
            for (size_t point_index = 0; point_index < chunk.points.size();) {
                const TrackPoint& point = chunk.points[point_index];
                if (point.d < dmin) {
                    ++point_index;
                    continue;
                }
                if (point.d > dmax) break;
                bool endpoint = dense_overlay
                    ? (std::abs(point.d - segment.d_min) < 1e-6 || std::abs(point.d - segment.d_max) < 1e-6)
                    : (point_index == 0 || point_index + 1 == chunk.points.size());
                if (!endpoint && point.d - last_appended_d < min_d_step) {
                    if (dense_overlay && std::isfinite(last_appended_d)) {
                        double target_d = last_appended_d + min_d_step;
                        auto next_it = std::lower_bound(chunk.points.begin() + static_cast<std::ptrdiff_t>(point_index + 1),
                                                        chunk.points.end(), target_d,
                                                        [](const TrackPoint& candidate, double target) {
                                                            return candidate.d < target;
                                                        });
                        if (next_it == chunk.points.end() &&
                            std::abs(chunk.d_max - segment.d_max) < 1e-6 &&
                            !chunk.points.empty()) {
                            point_index = chunk.points.size() - 1;
                        } else {
                            point_index = static_cast<size_t>(next_it - chunk.points.begin());
                        }
                        continue;
                    }
                    ++point_index;
                    continue;
                }
                builder.append(transform.model_to_screen(point.x, point.y));
                last_appended_d = point.d;
                appended = true;
                ++point_index;
            }
            if (appended) {
                segment_open = true;
            } else if (segment_open) {
                builder.break_line();
                segment_open = false;
            }
        }
        if (segment_open) builder.break_line();
    }
    builder.finish();
    draw->Flags = old_flags;
}

static double grid_step(double span) {
    double raw = std::max(span / 8.0, 1e-9);
    double mag = std::pow(10.0, std::floor(std::log10(raw)));
    for (double f : {1.0, 2.0, 5.0, 10.0}) {
        if (raw <= f * mag) return f * mag;
    }
    return 10.0 * mag;
}

static double friendly_scalebar_length(double raw_length) {
    if (raw_length <= 0.0 || !std::isfinite(raw_length)) return 1.0;
    double magnitude = std::pow(10.0, std::floor(std::log10(raw_length)));
    double best = magnitude;
    double best_diff = std::numeric_limits<double>::max();
    for (int exp_offset : {-1, 0, 1, 2}) {
        double base = magnitude * std::pow(10.0, exp_offset);
        for (double factor : {1.0, 2.0, 3.0, 5.0}) {
            double candidate = factor * base;
            if (candidate <= 0.0) continue;
            double diff = std::abs(candidate - raw_length);
            if (diff < best_diff) {
                best = candidate;
                best_diff = diff;
            }
        }
    }
    return best;
}

static std::string format_scalebar_label(double length) {
    if (length >= 1000.0) {
        double km = length / 1000.0;
        return format_double(km, std::abs(km - std::round(km)) < 1e-9 ? 0 : 1) + "km";
    }
    return format_double(length, std::abs(length - std::round(length)) < 1e-9 ? 0 : 1) + "m";
}

static void draw_scalebar(ImDrawList* draw, const View2D& view, ImVec2 origin, ImVec2 size) {
    if (view.scale <= 0.0 || !std::isfinite(view.scale)) return;
    float target_px = std::clamp(size.x * 0.18f, 90.0f, 180.0f);
    double length = friendly_scalebar_length(static_cast<double>(target_px) / view.scale);
    float bar_px = static_cast<float>(length * view.scale);
    if (!std::isfinite(bar_px) || bar_px <= 0.0f) return;

    float margin = 24.0f;
    float tick = 10.0f;
    ImVec2 p2(origin.x + size.x - margin, origin.y + size.y - margin);
    ImVec2 p1(p2.x - bar_px, p2.y);
    if (p1.x < origin.x + margin) return;

    ImU32 color = IM_COL32(255, 255, 255, 255);
    ImVec2 points[] = {ImVec2(p1.x, p1.y - tick), p1, p2, ImVec2(p2.x, p2.y - tick)};
    draw->AddPolyline(points, IM_ARRAYSIZE(points), color, ImDrawFlags_None, 2.0f);
    std::string label = format_scalebar_label(length);
    ImVec2 text_size = ImGui::CalcTextSize(label.c_str());
    draw->AddText(ImVec2((p1.x + p2.x - text_size.x) * 0.5f, p1.y - tick - 4.0f - text_size.y),
                  color, label.c_str());
}

static void draw_plan_triangle_marker(ImDrawList* draw, ImVec2 p, ImU32 color, float scale = 1.0f) {
    const float r = 6.0f * scale;
    ImVec2 pts[3] = {
        ImVec2(p.x, p.y - r),
        ImVec2(p.x - r * 0.9f, p.y + r * 0.72f),
        ImVec2(p.x + r * 0.9f, p.y + r * 0.72f),
    };
    draw->AddConvexPolyFilled(pts, IM_ARRAYSIZE(pts), color);
    draw->AddPolyline(pts, IM_ARRAYSIZE(pts), IM_COL32(64, 48, 0, 255), ImDrawFlags_Closed, 1.0f);
}

static void draw_plan_diamond_marker(ImDrawList* draw, ImVec2 p, ImU32 color, float scale = 1.0f) {
    const float r = 5.5f * scale;
    ImVec2 pts[4] = {
        ImVec2(p.x, p.y - r),
        ImVec2(p.x + r, p.y),
        ImVec2(p.x, p.y + r),
        ImVec2(p.x - r, p.y),
    };
    draw->AddConvexPolyFilled(pts, IM_ARRAYSIZE(pts), color);
    draw->AddPolyline(pts, IM_ARRAYSIZE(pts), IM_COL32(80, 0, 48, 255), ImDrawFlags_Closed, 1.0f);
}

static void draw_plan_signal_marker(ImDrawList* draw, ImVec2 p, ImU32 color, float scale = 1.0f) {
    const float circle_r = 4.6f * scale;
    const float line_weight = 1.8f * scale;
    const ImU32 shadow = IM_COL32(8, 42, 30, 230);
    ImVec2 circle_center(p.x, p.y - 3.2f * scale);
    draw->AddCircle(circle_center, circle_r, shadow, 20, line_weight + 1.8f * scale);
    draw->AddCircle(circle_center, circle_r, color, 20, line_weight);
    draw->AddLine(ImVec2(p.x, p.y + 1.8f * scale), ImVec2(p.x, p.y + 7.2f * scale),
                  shadow, line_weight + 1.8f * scale);
    draw->AddLine(ImVec2(p.x - 5.0f * scale, p.y + 7.2f * scale),
                  ImVec2(p.x + 5.0f * scale, p.y + 7.2f * scale),
                  shadow, line_weight + 1.8f * scale);
    draw->AddLine(ImVec2(p.x, p.y + 1.8f * scale), ImVec2(p.x, p.y + 7.2f * scale),
                  color, line_weight);
    draw->AddLine(ImVec2(p.x - 5.0f * scale, p.y + 7.2f * scale),
                  ImVec2(p.x + 5.0f * scale, p.y + 7.2f * scale),
                  color, line_weight);
}

static void draw_plan_beacon_marker(ImDrawList* draw, ImVec2 p, ImU32 color, float scale = 1.0f) {
    const float half_top = 4.5f * scale;
    const float half_bottom = 7.0f * scale;
    const float half_height = 5.5f * scale;
    const float line_weight = 1.8f * scale;
    const ImU32 shadow = IM_COL32(8, 42, 30, 230);
    ImVec2 pts[4] = {
        ImVec2(p.x - half_top, p.y - half_height),
        ImVec2(p.x + half_top, p.y - half_height),
        ImVec2(p.x + half_bottom, p.y + half_height),
        ImVec2(p.x - half_bottom, p.y + half_height),
    };
    draw->AddPolyline(pts, IM_ARRAYSIZE(pts), shadow, ImDrawFlags_Closed, line_weight + 1.8f * scale);
    draw->AddPolyline(pts, IM_ARRAYSIZE(pts), color, ImDrawFlags_Closed, line_weight);
}

static void draw_plan_pretrain_marker(ImDrawList* draw, ImVec2 p, const std::string& label, float scale = 1.0f) {
    const float half = 7.0f * scale;
    const ImU32 white = IM_COL32(255, 255, 255, 255);
    const ImU32 shadow = IM_COL32(0, 0, 0, 220);
    ImVec2 min(p.x - half, p.y - half);
    ImVec2 max(p.x + half, p.y + half);
    draw->AddRect(min, max, shadow, 0.0f, 0, 3.8f * scale);
    draw->AddRect(min, max, white, 0.0f, 0, 1.8f * scale);
    ImVec2 text_size = ImGui::CalcTextSize("P");
    ImVec2 text_pos(p.x - text_size.x * 0.5f, p.y - text_size.y * 0.5f);
    draw->AddText(ImVec2(text_pos.x + 1.0f, text_pos.y + 1.0f), shadow, "P");
    draw->AddText(text_pos, white, "P");
    if (!label.empty()) {
        ImVec2 label_pos(max.x + 5.0f * scale, p.y - ImGui::GetTextLineHeight() * 0.5f);
        draw->AddText(ImVec2(label_pos.x + 1.0f, label_pos.y + 1.0f), shadow, label.c_str());
        draw->AddText(label_pos, white, label.c_str());
    }
}

static void draw_plan_wave_marker(ImDrawList* draw, ImVec2 p, ImU32 color, float scale = 1.0f) {
    const float half_width = 7.0f * scale;
    const float amplitude = 3.0f * scale;
    ImVec2 pts[9];
    for (int i = 0; i < IM_ARRAYSIZE(pts); ++i) {
        float t = static_cast<float>(i) / static_cast<float>(IM_ARRAYSIZE(pts) - 1);
        float x = p.x - half_width + half_width * 2.0f * t;
        float y = p.y + std::sin(t * 3.14159265358979323846f * 4.0f) * amplitude;
        pts[i] = ImVec2(x, y);
    }
    draw->AddPolyline(pts, IM_ARRAYSIZE(pts), IM_COL32(72, 48, 112, 255), ImDrawFlags_None, 3.0f * scale);
    draw->AddPolyline(pts, IM_ARRAYSIZE(pts), color, ImDrawFlags_None, 1.6f * scale);
}

static void draw_plan_speaker_marker(ImDrawList* draw, ImVec2 p, ImU32 color, float scale = 1.0f) {
    const float body_w = 4.4f * scale;
    const float body_h = 6.2f * scale;
    const float cone_w = 5.0f * scale;
    const float cone_left_h = 4.0f * scale;
    const float cone_right_h = 9.2f * scale;
    const float line_weight = 1.8f * scale;
    const ImU32 outline = IM_COL32(58, 32, 92, 255);
    ImVec2 body_min(p.x - 8.0f * scale, p.y - body_h * 0.5f);
    ImVec2 body_max(body_min.x + body_w, p.y + body_h * 0.5f);
    ImVec2 cone[4] = {
        ImVec2(body_max.x, p.y - cone_left_h * 0.5f),
        ImVec2(body_max.x + cone_w, p.y - cone_right_h * 0.5f),
        ImVec2(body_max.x + cone_w, p.y + cone_right_h * 0.5f),
        ImVec2(body_max.x, p.y + cone_left_h * 0.5f),
    };
    draw->AddRectFilled(body_min, body_max, color, 1.0f * scale);
    draw->AddRect(body_min, body_max, outline, 1.0f * scale, 0, 1.0f * scale);
    draw->AddConvexPolyFilled(cone, IM_ARRAYSIZE(cone), color);
    draw->AddPolyline(cone, IM_ARRAYSIZE(cone), outline, ImDrawFlags_Closed, 1.0f * scale);
    for (int i = 0; i < 2; ++i) {
        float r = (5.4f + static_cast<float>(i) * 3.2f) * scale;
        draw->PathArcTo(ImVec2(p.x - 1.0f * scale, p.y), r, -0.72f, 0.72f, 12);
        draw->PathStroke(outline, ImDrawFlags_None, line_weight + 1.4f * scale);
        draw->PathArcTo(ImVec2(p.x - 1.0f * scale, p.y), r, -0.72f, 0.72f, 12);
        draw->PathStroke(color, ImDrawFlags_None, line_weight);
    }
}

static void draw_plan_broadcast_marker(ImDrawList* draw, ImVec2 p, ImU32 color, float scale = 1.0f) {
    const ImU32 outline = IM_COL32(58, 32, 92, 255);
    const float stem = 7.4f * scale;
    const float line_weight = 1.8f * scale;
    ImVec2 base(p.x, p.y + 4.8f * scale);
    ImVec2 top(p.x, p.y + 4.8f * scale - stem);
    draw->AddCircleFilled(base, 2.8f * scale, color, 16);
    draw->AddCircle(base, 2.8f * scale, outline, 16, 1.0f * scale);
    draw->AddLine(base, top, outline, line_weight + 1.6f * scale);
    draw->AddLine(base, top, color, line_weight);
    draw->AddCircleFilled(top, 2.0f * scale, color, 16);
    draw->AddCircle(top, 2.0f * scale, outline, 16, 1.0f * scale);
    for (int i = 0; i < 2; ++i) {
        float r = (5.6f + static_cast<float>(i) * 3.8f) * scale;
        draw->PathArcTo(top, r, -0.72f, 0.72f, 14);
        draw->PathStroke(outline, ImDrawFlags_None, line_weight + 1.4f * scale);
        draw->PathArcTo(top, r, -0.72f, 0.72f, 14);
        draw->PathStroke(color, ImDrawFlags_None, line_weight);
        draw->PathArcTo(top, r, 2.42f, 3.86f, 14);
        draw->PathStroke(outline, ImDrawFlags_None, line_weight + 1.4f * scale);
        draw->PathArcTo(top, r, 2.42f, 3.86f, 14);
        draw->PathStroke(color, ImDrawFlags_None, line_weight);
    }
}

static void draw_plan_axle_marker(ImDrawList* draw, ImVec2 p, ImU32 color, float scale = 1.0f) {
    const float half_width = 7.2f * scale;
    const float half_height = 4.4f * scale;
    const float outline_weight = 3.6f * scale;
    const float line_weight = 2.0f * scale;
    const ImU32 outline = IM_COL32(18, 52, 78, 255);
    ImVec2 left(p.x - half_width, p.y);
    ImVec2 right(p.x + half_width, p.y);
    draw->AddLine(left, right, outline, outline_weight);
    draw->AddLine(ImVec2(left.x, left.y - half_height), ImVec2(left.x, left.y + half_height), outline, outline_weight);
    draw->AddLine(ImVec2(right.x, right.y - half_height), ImVec2(right.x, right.y + half_height), outline, outline_weight);
    draw->AddLine(left, right, color, line_weight);
    draw->AddLine(ImVec2(left.x, left.y - half_height), ImVec2(left.x, left.y + half_height), color, line_weight);
    draw->AddLine(ImVec2(right.x, right.y - half_height), ImVec2(right.x, right.y + half_height), color, line_weight);
}

static void draw_plan_flange_noise_marker(ImDrawList* draw, ImVec2 p, ImU32 color, float scale = 1.0f) {
    const char* label = "F";
    ImFont* font = ImGui::GetFont();
    const float font_size = ImGui::GetFontSize() * 0.92f * scale;
    const ImVec2 text_size = font->CalcTextSizeA(font_size, std::numeric_limits<float>::max(), 0.0f, label);
    const ImVec2 text_pos(p.x - text_size.x * 0.5f, p.y - text_size.y * 0.5f);
    const ImU32 outline = IM_COL32(68, 34, 112, 255);
    const float outline_px = std::max(1.0f, 1.45f * scale);
    const ImVec2 outline_offsets[] = {
        ImVec2(-outline_px, -outline_px), ImVec2(0.0f, -outline_px), ImVec2(outline_px, -outline_px),
        ImVec2(-outline_px, 0.0f),                                      ImVec2(outline_px, 0.0f),
        ImVec2(-outline_px, outline_px),  ImVec2(0.0f, outline_px),  ImVec2(outline_px, outline_px),
    };
    for (const ImVec2& offset : outline_offsets) {
        draw->AddText(font, font_size, ImVec2(text_pos.x + offset.x, text_pos.y + offset.y), outline, label);
    }

    const float bold_px = std::max(0.45f, 0.75f * scale);
    draw->AddText(font, font_size, ImVec2(text_pos.x - bold_px, text_pos.y), color, label);
    draw->AddText(font, font_size, text_pos, color, label);
    draw->AddText(font, font_size, ImVec2(text_pos.x + bold_px, text_pos.y), color, label);
}

static void draw_plan_joint_noise_marker(ImDrawList* draw, ImVec2 p, ImU32 color, float scale = 1.0f) {
    const float left = 6.0f * scale;
    const float right = 6.5f * scale;
    const float up = 6.5f * scale;
    const float down = 5.0f * scale;
    const float outline_weight = 4.0f * scale;
    const float line_weight = 2.1f * scale;
    const ImU32 outline = IM_COL32(18, 54, 72, 255);
    ImVec2 vertex(p.x - left, p.y + down);
    ImVec2 upper(p.x + right, p.y - up);
    ImVec2 lower(p.x + right, p.y + down);
    draw->AddLine(upper, vertex, outline, outline_weight);
    draw->AddLine(vertex, lower, outline, outline_weight);
    draw->AddLine(upper, vertex, color, line_weight);
    draw->AddLine(vertex, lower, color, line_weight);
}

static void draw_plan_square_marker(ImDrawList* draw, ImVec2 p, ImU32 color, float scale = 1.0f) {
    const float r = 5.0f * scale;
    ImVec2 min(p.x - r, p.y - r);
    ImVec2 max(p.x + r, p.y + r);
    draw->AddRectFilled(min, max, color, 0.0f);
    draw->AddRect(min, max, IM_COL32(82, 68, 0, 255), 0.0f, 0, 1.0f * scale);
}

static void draw_plan_adhesion_marker(ImDrawList* draw, ImVec2 p, ImU32 color, float scale = 1.0f) {
    const float circle_r = 3.2f * scale;
    const float arc_r = 7.2f * scale;
    const float start_angle = -2.45f;
    const float end_angle = 0.82f;
    draw->AddCircleFilled(p, circle_r, color, 16);
    draw->AddCircle(p, circle_r, IM_COL32(62, 28, 96, 255), 16, 1.0f * scale);
    draw->PathArcTo(p, arc_r, start_angle, end_angle, 18);
    draw->PathStroke(color, ImDrawFlags_None, 1.8f * scale);

    ImVec2 tip(p.x + std::cos(end_angle) * arc_r, p.y + std::sin(end_angle) * arc_r);
    ImVec2 tangent(-std::sin(end_angle), std::cos(end_angle));
    ImVec2 normal(-tangent.y, tangent.x);
    const float head_len = 4.0f * scale;
    const float head_half = 2.5f * scale;
    ImVec2 pts[3] = {
        tip,
        ImVec2(tip.x - tangent.x * head_len + normal.x * head_half,
               tip.y - tangent.y * head_len + normal.y * head_half),
        ImVec2(tip.x - tangent.x * head_len - normal.x * head_half,
               tip.y - tangent.y * head_len - normal.y * head_half),
    };
    draw->AddConvexPolyFilled(pts, IM_ARRAYSIZE(pts), color);
}

static void draw_plan_sun_marker(ImDrawList* draw, ImVec2 p, ImU32 color, float scale = 1.0f) {
    const float inner = 3.4f * scale;
    const float ray_inner = 5.3f * scale;
    const float ray_outer = 8.0f * scale;
    const ImU32 outline = IM_COL32(96, 68, 0, 255);
    for (int i = 0; i < 8; ++i) {
        float angle = static_cast<float>(i) * 3.14159265358979323846f / 4.0f;
        ImVec2 a(p.x + std::cos(angle) * ray_inner, p.y + std::sin(angle) * ray_inner);
        ImVec2 b(p.x + std::cos(angle) * ray_outer, p.y + std::sin(angle) * ray_outer);
        draw->AddLine(a, b, outline, 2.8f * scale);
        draw->AddLine(a, b, color, 1.6f * scale);
    }
    draw->AddCircleFilled(p, inner, color, 16);
    draw->AddCircle(p, inner, outline, 16, 1.0f * scale);
}

static void draw_plan_fog_marker(ImDrawList* draw, ImVec2 p, float scale = 1.0f) {
    const float half_width = 5.5f * scale;
    const float gap = 3.2f * scale;
    const float shadow_weight = 3.0f * scale;
    const float line_weight = 1.5f * scale;
    const ImU32 shadow = IM_COL32(0, 0, 0, 190);
    const ImU32 white = IM_COL32(255, 255, 255, 255);
    for (int i = -1; i <= 1; ++i) {
        float y = p.y + static_cast<float>(i) * gap;
        ImVec2 a(p.x - half_width, y);
        ImVec2 b(p.x + half_width, y);
        draw->AddLine(a, b, shadow, shadow_weight);
        draw->AddLine(a, b, white, line_weight);
    }
}

static void draw_plan_focus_arrow(ImDrawList* draw, ImVec2 target) {
    const float length = 30.0f;
    const float head = 11.0f;
    const float half_height = 7.0f;
    const float target_gap = 10.0f;
    ImU32 fill = IM_COL32(255, 32, 32, 255);
    ImVec2 tip(target.x - target_gap, target.y);
    ImVec2 tail(tip.x - length, tip.y);
    ImVec2 neck(tip.x - head, tip.y);
    draw->AddLine(tail, neck, fill, 3.0f);
    ImVec2 pts[3] = {
        tip,
        ImVec2(tip.x - head, tip.y - half_height),
        ImVec2(tip.x - head, tip.y + half_height),
    };
    draw->AddConvexPolyFilled(pts, IM_ARRAYSIZE(pts), fill);
}

static void draw_plan_current_position_arrow(ImDrawList* draw, ImVec2 center, ImVec2 direction, float scale = 1.0f) {
    float len = std::sqrt(direction.x * direction.x + direction.y * direction.y);
    if (len < 1e-3f) return;
    ImVec2 forward(direction.x / len, direction.y / len);
    ImVec2 normal(-forward.y, forward.x);
    const float tip_len = 18.0f * scale;
    const float tail_back = 10.0f * scale;
    const float notch_back = 3.0f * scale;
    const float half_width = 8.0f * scale;
    ImVec2 pts[4] = {
        ImVec2(center.x + forward.x * tip_len,
               center.y + forward.y * tip_len),
        ImVec2(center.x - forward.x * tail_back + normal.x * half_width,
               center.y - forward.y * tail_back + normal.y * half_width),
        ImVec2(center.x - forward.x * notch_back,
               center.y - forward.y * notch_back),
        ImVec2(center.x - forward.x * tail_back - normal.x * half_width,
               center.y - forward.y * tail_back - normal.y * half_width),
    };
    const ImU32 fill = IM_COL32(0, 122, 255, 255);
    draw->AddTriangleFilled(pts[0], pts[1], pts[2], fill);
    draw->AddTriangleFilled(pts[0], pts[2], pts[3], fill);
    draw->AddPolyline(pts, IM_ARRAYSIZE(pts), IM_COL32(255, 255, 255, 255), ImDrawFlags_Closed, 3.0f * scale);
}

static void draw_plan_small_text(ImDrawList* draw, ImVec2 p, ImU32 color, const std::string& text) {
    if (text.empty()) return;
    draw->AddText(nullptr, ImGui::GetFontSize() * 0.78f, ImVec2(p.x + 8.0f, p.y - 9.0f), color, text.c_str());
}

static bool point_near_canvas(ImVec2 p, ImVec2 origin, ImVec2 size, float margin = 48.0f) {
    return p.x >= origin.x - margin && p.x <= origin.x + size.x + margin &&
           p.y >= origin.y - margin && p.y <= origin.y + size.y + margin;
}

void App::render_plan_canvas(ImVec2 size) {
    debug_plan_stage("start");
    PlanData data = build_plan_data(false);
    debug_plan_stage("build_plan_data");
    ImGui::BeginChild("PlanCanvasChild", size, true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImVec2 origin = ImGui::GetCursorScreenPos();
    ImVec2 avail = ImGui::GetContentRegionAvail();
    avail.x = std::max(avail.x, 50.0f);
    avail.y = std::max(avail.y, 50.0f);
    ImGui::InvisibleButton("PlanCanvasButton", avail, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
    bool hovered = ImGui::IsItemHovered();
    bool picking_background_station = pick_slot_ != 0;
    if (hovered && picking_background_station) {
        set_crosshair_cursor();
    }
    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(origin, ImVec2(origin.x + avail.x, origin.y + avail.y), IM_COL32(12, 13, 15, 255));
    draw->PushClipRect(origin, ImVec2(origin.x + avail.x, origin.y + avail.y), true);

    if (!data.own.empty() && (!plan_view_.fitted || !keep_plan_view_)) {
        PlanData fit_data = build_plan_data(true);
        const PlanData& bounds = fit_data.own.empty() ? data : fit_data;
        plan_view_.fit(bounds.xmin, bounds.ymin, bounds.xmax, bounds.ymax, avail);
        keep_plan_view_ = true;
    }
    debug_plan_stage("fit");

    ImGuiIO& io = ImGui::GetIO();
    ImVec2 mouse = io.MousePos;
    auto nearest_measure_distance = [&]() -> std::optional<double> {
        PlanScreenTransform measure_transform = make_plan_transform(plan_view_, -data.origin_angle, origin, avail);
        double best = std::numeric_limits<double>::max();
        const TrackPoint* best_p = nullptr;
        for (const auto& p : data.own) {
            ImVec2 sp = measure_transform.plan_to_screen(p.x, p.y);
            double dist = std::hypot(sp.x - mouse.x, sp.y - mouse.y);
            if (dist < best) {
                best = dist;
                best_p = &p;
            }
        }
        if (best_p && best <= 30.0) return best_p->d;
        return std::nullopt;
    };
    std::optional<double> hovered_measure_distance;
    if (hovered && mode_ == Mode::Measure && !data.own.empty()) {
        hovered_measure_distance = nearest_measure_distance();
        if (hovered_measure_distance) set_crosshair_cursor();
    }
    if (hovered && io.MouseWheel != 0.0f) {
        if (io.KeyShift) {
            plan_view_.rotation += io.MouseWheel * 5.0 * 3.14159265358979323846 / 180.0;
        } else {
            double factor = io.MouseWheel > 0 ? 1.15 : 1.0 / 1.15;
            plan_view_.scale = std::clamp(plan_view_.scale * factor, 0.001, 10000.0);
        }
    }

    struct MarkerHit {
        size_t row_index = 0;
        double dist_sq = 0.0;
    };
    size_t selected_repeater_count = static_cast<size_t>(std::count_if(
        repeater_row_visible_.begin(), repeater_row_visible_.end(),
        [](unsigned char visible) { return visible != 0; }));
    bool dense_repeater_marker_lod = selected_repeater_count > 1200 && plan_view_.scale < 0.025;
    const float marker_size_scale = marker_size_scale_from_percent(marker_size_percent_);
    const float marker_canvas_margin = std::max(12.0f, 12.0f * marker_size_scale);
    const double marker_hover_radius = static_cast<double>(marker_canvas_margin);
    const double marker_hover_radius_sq = marker_hover_radius * marker_hover_radius;
    auto nearest_structure_marker_hit = [&](const PlanScreenTransform& hit_transform) -> std::optional<MarkerHit> {
        if (!hovered || mode_ != Mode::Pan || picking_background_station) return std::nullopt;
        double best = marker_hover_radius_sq;
        std::optional<MarkerHit> best_hit;
        for (const auto& marker : data.structure_markers) {
            ImVec2 p = hit_transform.plan_to_screen(marker.x, marker.y);
            if (!point_near_canvas(p, origin, avail, marker_canvas_margin)) continue;
            double dx = static_cast<double>(p.x - mouse.x);
            double dy = static_cast<double>(p.y - mouse.y);
            double dist_sq = dx * dx + dy * dy;
            if (dist_sq <= best) {
                best = dist_sq;
                best_hit = MarkerHit{marker.row_index, dist_sq};
            }
        }
        return best_hit;
    };
    auto nearest_repeater_marker_hit = [&](const PlanScreenTransform& hit_transform) -> std::optional<MarkerHit> {
        if (!hovered || mode_ != Mode::Pan || picking_background_station || dense_repeater_marker_lod) return std::nullopt;
        double best = marker_hover_radius_sq;
        std::optional<MarkerHit> best_hit;
        for (const auto& marker : data.repeater_markers) {
            ImVec2 p = hit_transform.plan_to_screen(marker.x, marker.y);
            if (!point_near_canvas(p, origin, avail, marker_canvas_margin)) continue;
            double dx = static_cast<double>(p.x - mouse.x);
            double dy = static_cast<double>(p.y - mouse.y);
            double dist_sq = dx * dx + dy * dy;
            if (dist_sq <= best) {
                best = dist_sq;
                best_hit = MarkerHit{marker.row_index, dist_sq};
            }
        }
        return best_hit;
    };
    auto nearest_signal_marker_hit = [&](const PlanScreenTransform& hit_transform) -> std::optional<MarkerHit> {
        if (!hovered || mode_ != Mode::Pan || picking_background_station) return std::nullopt;
        double best = marker_hover_radius_sq;
        std::optional<MarkerHit> best_hit;
        for (const auto& marker : data.signal_markers) {
            ImVec2 p = hit_transform.plan_to_screen(marker.x, marker.y);
            if (!point_near_canvas(p, origin, avail, marker_canvas_margin)) continue;
            double dx = static_cast<double>(p.x - mouse.x);
            double dy = static_cast<double>(p.y - mouse.y);
            double dist_sq = dx * dx + dy * dy;
            if (dist_sq <= best) {
                best = dist_sq;
                best_hit = MarkerHit{marker.row_index, dist_sq};
            }
        }
        return best_hit;
    };
    auto nearest_beacon_marker_hit = [&](const PlanScreenTransform& hit_transform) -> std::optional<MarkerHit> {
        if (!hovered || mode_ != Mode::Pan || picking_background_station) return std::nullopt;
        double best = marker_hover_radius_sq;
        std::optional<MarkerHit> best_hit;
        for (const auto& marker : data.beacon_markers) {
            ImVec2 p = hit_transform.plan_to_screen(marker.x, marker.y);
            if (!point_near_canvas(p, origin, avail, marker_canvas_margin)) continue;
            double dx = static_cast<double>(p.x - mouse.x);
            double dy = static_cast<double>(p.y - mouse.y);
            double dist_sq = dx * dx + dy * dy;
            if (dist_sq <= best) {
                best = dist_sq;
                best_hit = MarkerHit{marker.row_index, dist_sq};
            }
        }
        return best_hit;
    };
    auto nearest_pretrain_marker_hit = [&](const PlanScreenTransform& hit_transform) -> std::optional<MarkerHit> {
        if (!hovered || mode_ != Mode::Pan || picking_background_station) return std::nullopt;
        double best = marker_hover_radius_sq;
        std::optional<MarkerHit> best_hit;
        for (const auto& marker : data.pretrain_markers) {
            ImVec2 p = hit_transform.plan_to_screen(marker.x, marker.y);
            if (!point_near_canvas(p, origin, avail, marker_canvas_margin)) continue;
            double dx = static_cast<double>(p.x - mouse.x);
            double dy = static_cast<double>(p.y - mouse.y);
            double dist_sq = dx * dx + dy * dy;
            if (dist_sq <= best) {
                best = dist_sq;
                best_hit = MarkerHit{marker.row_index, dist_sq};
            }
        }
        return best_hit;
    };
    auto nearest_irregularity_marker_hit = [&](const PlanScreenTransform& hit_transform) -> std::optional<MarkerHit> {
        if (!hovered || mode_ != Mode::Pan || picking_background_station) return std::nullopt;
        double best = marker_hover_radius_sq;
        std::optional<MarkerHit> best_hit;
        for (const auto& marker : data.irregularity_markers) {
            ImVec2 p = hit_transform.plan_to_screen(marker.x, marker.y);
            if (!point_near_canvas(p, origin, avail, marker_canvas_margin)) continue;
            double dx = static_cast<double>(p.x - mouse.x);
            double dy = static_cast<double>(p.y - mouse.y);
            double dist_sq = dx * dx + dy * dy;
            if (dist_sq <= best) {
                best = dist_sq;
                best_hit = MarkerHit{marker.row_index, dist_sq};
            }
        }
        return best_hit;
    };
    auto nearest_rolling_noise_marker_hit = [&](const PlanScreenTransform& hit_transform) -> std::optional<MarkerHit> {
        if (!hovered || mode_ != Mode::Pan || picking_background_station) return std::nullopt;
        double best = marker_hover_radius_sq;
        std::optional<MarkerHit> best_hit;
        for (const auto& marker : data.rolling_noise_markers) {
            ImVec2 p = hit_transform.plan_to_screen(marker.x, marker.y);
            if (!point_near_canvas(p, origin, avail, marker_canvas_margin)) continue;
            double dx = static_cast<double>(p.x - mouse.x);
            double dy = static_cast<double>(p.y - mouse.y);
            double dist_sq = dx * dx + dy * dy;
            if (dist_sq <= best) {
                best = dist_sq;
                best_hit = MarkerHit{marker.row_index, dist_sq};
            }
        }
        return best_hit;
    };
    auto nearest_map_sound_marker_hit = [&](const PlanScreenTransform& hit_transform) -> std::optional<MarkerHit> {
        if (!hovered || mode_ != Mode::Pan || picking_background_station) return std::nullopt;
        double best = marker_hover_radius_sq;
        std::optional<MarkerHit> best_hit;
        for (const auto& marker : data.map_sound_markers) {
            ImVec2 p = hit_transform.plan_to_screen(marker.x, marker.y);
            if (!point_near_canvas(p, origin, avail, marker_canvas_margin)) continue;
            double dx = static_cast<double>(p.x - mouse.x);
            double dy = static_cast<double>(p.y - mouse.y);
            double dist_sq = dx * dx + dy * dy;
            if (dist_sq <= best) {
                best = dist_sq;
                best_hit = MarkerHit{marker.row_index, dist_sq};
            }
        }
        return best_hit;
    };
    auto nearest_map_sound_3d_marker_hit = [&](const PlanScreenTransform& hit_transform) -> std::optional<MarkerHit> {
        if (!hovered || mode_ != Mode::Pan || picking_background_station) return std::nullopt;
        double best = marker_hover_radius_sq;
        std::optional<MarkerHit> best_hit;
        for (const auto& marker : data.map_sound_3d_markers) {
            ImVec2 p = hit_transform.plan_to_screen(marker.x, marker.y);
            if (!point_near_canvas(p, origin, avail, marker_canvas_margin)) continue;
            double dx = static_cast<double>(p.x - mouse.x);
            double dy = static_cast<double>(p.y - mouse.y);
            double dist_sq = dx * dx + dy * dy;
            if (dist_sq <= best) {
                best = dist_sq;
                best_hit = MarkerHit{marker.row_index, dist_sq};
            }
        }
        return best_hit;
    };
    auto nearest_flange_noise_marker_hit = [&](const PlanScreenTransform& hit_transform) -> std::optional<MarkerHit> {
        if (!hovered || mode_ != Mode::Pan || picking_background_station) return std::nullopt;
        double best = marker_hover_radius_sq;
        std::optional<MarkerHit> best_hit;
        for (const auto& marker : data.flange_noise_markers) {
            ImVec2 p = hit_transform.plan_to_screen(marker.x, marker.y);
            if (!point_near_canvas(p, origin, avail, marker_canvas_margin)) continue;
            double dx = static_cast<double>(p.x - mouse.x);
            double dy = static_cast<double>(p.y - mouse.y);
            double dist_sq = dx * dx + dy * dy;
            if (dist_sq <= best) {
                best = dist_sq;
                best_hit = MarkerHit{marker.row_index, dist_sq};
            }
        }
        return best_hit;
    };
    auto nearest_joint_noise_marker_hit = [&](const PlanScreenTransform& hit_transform) -> std::optional<MarkerHit> {
        if (!hovered || mode_ != Mode::Pan || picking_background_station) return std::nullopt;
        double best = marker_hover_radius_sq;
        std::optional<MarkerHit> best_hit;
        for (const auto& marker : data.joint_noise_markers) {
            ImVec2 p = hit_transform.plan_to_screen(marker.x, marker.y);
            if (!point_near_canvas(p, origin, avail, marker_canvas_margin)) continue;
            double dx = static_cast<double>(p.x - mouse.x);
            double dy = static_cast<double>(p.y - mouse.y);
            double dist_sq = dx * dx + dy * dy;
            if (dist_sq <= best) {
                best = dist_sq;
                best_hit = MarkerHit{marker.row_index, dist_sq};
            }
        }
        return best_hit;
    };
    auto nearest_background_marker_hit = [&](const PlanScreenTransform& hit_transform) -> std::optional<MarkerHit> {
        if (!hovered || mode_ != Mode::Pan || picking_background_station) return std::nullopt;
        double best = marker_hover_radius_sq;
        std::optional<MarkerHit> best_hit;
        for (const auto& marker : data.background_markers) {
            ImVec2 p = hit_transform.plan_to_screen(marker.x, marker.y);
            if (!point_near_canvas(p, origin, avail, marker_canvas_margin)) continue;
            double dx = static_cast<double>(p.x - mouse.x);
            double dy = static_cast<double>(p.y - mouse.y);
            double dist_sq = dx * dx + dy * dy;
            if (dist_sq <= best) {
                best = dist_sq;
                best_hit = MarkerHit{marker.row_index, dist_sq};
            }
        }
        return best_hit;
    };
    auto nearest_adhesion_marker_hit = [&](const PlanScreenTransform& hit_transform) -> std::optional<MarkerHit> {
        if (!hovered || mode_ != Mode::Pan || picking_background_station) return std::nullopt;
        double best = marker_hover_radius_sq;
        std::optional<MarkerHit> best_hit;
        for (const auto& marker : data.adhesion_markers) {
            ImVec2 p = hit_transform.plan_to_screen(marker.x, marker.y);
            if (!point_near_canvas(p, origin, avail, marker_canvas_margin)) continue;
            double dx = static_cast<double>(p.x - mouse.x);
            double dy = static_cast<double>(p.y - mouse.y);
            double dist_sq = dx * dx + dy * dy;
            if (dist_sq <= best) {
                best = dist_sq;
                best_hit = MarkerHit{marker.row_index, dist_sq};
            }
        }
        return best_hit;
    };
    auto nearest_cab_illuminance_marker_hit = [&](const PlanScreenTransform& hit_transform) -> std::optional<MarkerHit> {
        if (!hovered || mode_ != Mode::Pan || picking_background_station) return std::nullopt;
        double best = marker_hover_radius_sq;
        std::optional<MarkerHit> best_hit;
        for (const auto& marker : data.cab_illuminance_markers) {
            ImVec2 p = hit_transform.plan_to_screen(marker.x, marker.y);
            if (!point_near_canvas(p, origin, avail, marker_canvas_margin)) continue;
            double dx = static_cast<double>(p.x - mouse.x);
            double dy = static_cast<double>(p.y - mouse.y);
            double dist_sq = dx * dx + dy * dy;
            if (dist_sq <= best) {
                best = dist_sq;
                best_hit = MarkerHit{marker.row_index, dist_sq};
            }
        }
        return best_hit;
    };
    auto nearest_fog_marker_hit = [&](const PlanScreenTransform& hit_transform) -> std::optional<MarkerHit> {
        if (!hovered || mode_ != Mode::Pan || picking_background_station) return std::nullopt;
        double best = marker_hover_radius_sq;
        std::optional<MarkerHit> best_hit;
        for (const auto& marker : data.fog_markers) {
            ImVec2 p = hit_transform.plan_to_screen(marker.x, marker.y);
            if (!point_near_canvas(p, origin, avail, marker_canvas_margin)) continue;
            double dx = static_cast<double>(p.x - mouse.x);
            double dy = static_cast<double>(p.y - mouse.y);
            double dist_sq = dx * dx + dy * dy;
            if (dist_sq <= best) {
                best = dist_sq;
                best_hit = MarkerHit{marker.row_index, dist_sq};
            }
        }
        return best_hit;
    };
    PlanScreenTransform hit_transform = make_plan_transform(plan_view_, -data.origin_angle, origin, avail);
    std::optional<MarkerHit> hovered_structure_hit = nearest_structure_marker_hit(hit_transform);
    std::optional<MarkerHit> hovered_repeater_hit = nearest_repeater_marker_hit(hit_transform);
    std::optional<MarkerHit> hovered_signal_hit = nearest_signal_marker_hit(hit_transform);
    std::optional<MarkerHit> hovered_beacon_hit = nearest_beacon_marker_hit(hit_transform);
    std::optional<MarkerHit> hovered_pretrain_hit = nearest_pretrain_marker_hit(hit_transform);
    std::optional<MarkerHit> hovered_irregularity_hit = nearest_irregularity_marker_hit(hit_transform);
    std::optional<MarkerHit> hovered_rolling_noise_hit = nearest_rolling_noise_marker_hit(hit_transform);
    std::optional<MarkerHit> hovered_map_sound_hit = nearest_map_sound_marker_hit(hit_transform);
    std::optional<MarkerHit> hovered_map_sound_3d_hit = nearest_map_sound_3d_marker_hit(hit_transform);
    std::optional<MarkerHit> hovered_flange_noise_hit = nearest_flange_noise_marker_hit(hit_transform);
    std::optional<MarkerHit> hovered_joint_noise_hit = nearest_joint_noise_marker_hit(hit_transform);
    std::optional<MarkerHit> hovered_background_hit = nearest_background_marker_hit(hit_transform);
    std::optional<MarkerHit> hovered_adhesion_hit = nearest_adhesion_marker_hit(hit_transform);
    std::optional<MarkerHit> hovered_cab_illuminance_hit = nearest_cab_illuminance_marker_hit(hit_transform);
    std::optional<MarkerHit> hovered_fog_hit = nearest_fog_marker_hit(hit_transform);
    debug_plan_stage("hit_test");
    std::optional<size_t> hovered_structure_row = hovered_structure_hit
        ? std::optional<size_t>(hovered_structure_hit->row_index)
        : std::nullopt;
    std::optional<size_t> hovered_repeater_row = hovered_repeater_hit
        ? std::optional<size_t>(hovered_repeater_hit->row_index)
        : std::nullopt;
    std::optional<size_t> hovered_signal_row = hovered_signal_hit
        ? std::optional<size_t>(hovered_signal_hit->row_index)
        : std::nullopt;
    std::optional<size_t> hovered_beacon_row = hovered_beacon_hit
        ? std::optional<size_t>(hovered_beacon_hit->row_index)
        : std::nullopt;
    std::optional<size_t> hovered_pretrain_row = hovered_pretrain_hit
        ? std::optional<size_t>(hovered_pretrain_hit->row_index)
        : std::nullopt;
    std::optional<size_t> hovered_irregularity_row = hovered_irregularity_hit
        ? std::optional<size_t>(hovered_irregularity_hit->row_index)
        : std::nullopt;
    std::optional<size_t> hovered_rolling_noise_row = hovered_rolling_noise_hit
        ? std::optional<size_t>(hovered_rolling_noise_hit->row_index)
        : std::nullopt;
    std::optional<size_t> hovered_map_sound_row = hovered_map_sound_hit
        ? std::optional<size_t>(hovered_map_sound_hit->row_index)
        : std::nullopt;
    std::optional<size_t> hovered_map_sound_3d_row = hovered_map_sound_3d_hit
        ? std::optional<size_t>(hovered_map_sound_3d_hit->row_index)
        : std::nullopt;
    std::optional<size_t> hovered_flange_noise_row = hovered_flange_noise_hit
        ? std::optional<size_t>(hovered_flange_noise_hit->row_index)
        : std::nullopt;
    std::optional<size_t> hovered_joint_noise_row = hovered_joint_noise_hit
        ? std::optional<size_t>(hovered_joint_noise_hit->row_index)
        : std::nullopt;
    std::optional<size_t> hovered_background_row = hovered_background_hit
        ? std::optional<size_t>(hovered_background_hit->row_index)
        : std::nullopt;
    std::optional<size_t> hovered_adhesion_row = hovered_adhesion_hit
        ? std::optional<size_t>(hovered_adhesion_hit->row_index)
        : std::nullopt;
    std::optional<size_t> hovered_cab_illuminance_row = hovered_cab_illuminance_hit
        ? std::optional<size_t>(hovered_cab_illuminance_hit->row_index)
        : std::nullopt;
    std::optional<size_t> hovered_fog_row = hovered_fog_hit
        ? std::optional<size_t>(hovered_fog_hit->row_index)
        : std::nullopt;
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
        auto closer_or_equal = [](const std::optional<MarkerHit>& hit, const std::optional<MarkerHit>& other) {
            return hit && (!other || hit->dist_sq <= other->dist_sq);
        };
        if (hovered_signal_hit &&
            closer_or_equal(hovered_signal_hit, hovered_beacon_hit) &&
            closer_or_equal(hovered_signal_hit, hovered_adhesion_hit) &&
            closer_or_equal(hovered_signal_hit, hovered_irregularity_hit) &&
            closer_or_equal(hovered_signal_hit, hovered_background_hit) &&
            closer_or_equal(hovered_signal_hit, hovered_repeater_hit) &&
            closer_or_equal(hovered_signal_hit, hovered_structure_hit) &&
            closer_or_equal(hovered_signal_hit, hovered_cab_illuminance_hit) &&
            closer_or_equal(hovered_signal_hit, hovered_rolling_noise_hit) &&
            closer_or_equal(hovered_signal_hit, hovered_map_sound_hit) &&
            closer_or_equal(hovered_signal_hit, hovered_map_sound_3d_hit) &&
            closer_or_equal(hovered_signal_hit, hovered_flange_noise_hit) &&
            closer_or_equal(hovered_signal_hit, hovered_joint_noise_hit) &&
            closer_or_equal(hovered_signal_hit, hovered_fog_hit)) {
            plan_signal_popup_row_ = static_cast<int>(hovered_signal_hit->row_index);
            ImGui::OpenPopup("plan_signal_marker_context");
        } else if (hovered_beacon_hit &&
            closer_or_equal(hovered_beacon_hit, hovered_signal_hit) &&
            closer_or_equal(hovered_beacon_hit, hovered_adhesion_hit) &&
            closer_or_equal(hovered_beacon_hit, hovered_irregularity_hit) &&
            closer_or_equal(hovered_beacon_hit, hovered_background_hit) &&
            closer_or_equal(hovered_beacon_hit, hovered_repeater_hit) &&
            closer_or_equal(hovered_beacon_hit, hovered_structure_hit) &&
            closer_or_equal(hovered_beacon_hit, hovered_cab_illuminance_hit) &&
            closer_or_equal(hovered_beacon_hit, hovered_rolling_noise_hit) &&
            closer_or_equal(hovered_beacon_hit, hovered_map_sound_hit) &&
            closer_or_equal(hovered_beacon_hit, hovered_map_sound_3d_hit) &&
            closer_or_equal(hovered_beacon_hit, hovered_flange_noise_hit) &&
            closer_or_equal(hovered_beacon_hit, hovered_joint_noise_hit) &&
            closer_or_equal(hovered_beacon_hit, hovered_fog_hit)) {
            plan_beacon_popup_row_ = static_cast<int>(hovered_beacon_hit->row_index);
            ImGui::OpenPopup("plan_beacon_marker_context");
        } else if (hovered_adhesion_hit &&
            closer_or_equal(hovered_adhesion_hit, hovered_irregularity_hit) &&
            closer_or_equal(hovered_adhesion_hit, hovered_background_hit) &&
            closer_or_equal(hovered_adhesion_hit, hovered_repeater_hit) &&
            closer_or_equal(hovered_adhesion_hit, hovered_structure_hit) &&
            closer_or_equal(hovered_adhesion_hit, hovered_cab_illuminance_hit) &&
            closer_or_equal(hovered_adhesion_hit, hovered_rolling_noise_hit) &&
            closer_or_equal(hovered_adhesion_hit, hovered_map_sound_hit) &&
            closer_or_equal(hovered_adhesion_hit, hovered_map_sound_3d_hit) &&
            closer_or_equal(hovered_adhesion_hit, hovered_flange_noise_hit) &&
            closer_or_equal(hovered_adhesion_hit, hovered_joint_noise_hit) &&
            closer_or_equal(hovered_adhesion_hit, hovered_fog_hit)) {
            plan_adhesion_popup_row_ = static_cast<int>(hovered_adhesion_hit->row_index);
            ImGui::OpenPopup("plan_adhesion_marker_context");
        } else if (hovered_irregularity_hit &&
            closer_or_equal(hovered_irregularity_hit, hovered_adhesion_hit) &&
            closer_or_equal(hovered_irregularity_hit, hovered_background_hit) &&
            closer_or_equal(hovered_irregularity_hit, hovered_cab_illuminance_hit) &&
            closer_or_equal(hovered_irregularity_hit, hovered_fog_hit) &&
            closer_or_equal(hovered_irregularity_hit, hovered_rolling_noise_hit) &&
            closer_or_equal(hovered_irregularity_hit, hovered_map_sound_hit) &&
            closer_or_equal(hovered_irregularity_hit, hovered_map_sound_3d_hit) &&
            closer_or_equal(hovered_irregularity_hit, hovered_flange_noise_hit) &&
            closer_or_equal(hovered_irregularity_hit, hovered_joint_noise_hit) &&
            (!hovered_repeater_hit || hovered_irregularity_hit->dist_sq <= hovered_repeater_hit->dist_sq) &&
            (!hovered_structure_hit || hovered_irregularity_hit->dist_sq <= hovered_structure_hit->dist_sq)) {
            plan_irregularity_popup_row_ = static_cast<int>(hovered_irregularity_hit->row_index);
            ImGui::OpenPopup("plan_irregularity_marker_context");
        } else if (hovered_rolling_noise_hit &&
                   closer_or_equal(hovered_rolling_noise_hit, hovered_adhesion_hit) &&
                   closer_or_equal(hovered_rolling_noise_hit, hovered_irregularity_hit) &&
                   closer_or_equal(hovered_rolling_noise_hit, hovered_background_hit) &&
                   closer_or_equal(hovered_rolling_noise_hit, hovered_cab_illuminance_hit) &&
                   closer_or_equal(hovered_rolling_noise_hit, hovered_fog_hit) &&
                   closer_or_equal(hovered_rolling_noise_hit, hovered_map_sound_hit) &&
                   closer_or_equal(hovered_rolling_noise_hit, hovered_map_sound_3d_hit) &&
                   closer_or_equal(hovered_rolling_noise_hit, hovered_flange_noise_hit) &&
                   closer_or_equal(hovered_rolling_noise_hit, hovered_joint_noise_hit) &&
                   (!hovered_repeater_hit || hovered_rolling_noise_hit->dist_sq <= hovered_repeater_hit->dist_sq) &&
                   (!hovered_structure_hit || hovered_rolling_noise_hit->dist_sq <= hovered_structure_hit->dist_sq)) {
            plan_rolling_noise_popup_row_ = static_cast<int>(hovered_rolling_noise_hit->row_index);
            ImGui::OpenPopup("plan_rolling_noise_marker_context");
        } else if (hovered_map_sound_hit &&
                   closer_or_equal(hovered_map_sound_hit, hovered_signal_hit) &&
                   closer_or_equal(hovered_map_sound_hit, hovered_beacon_hit) &&
                   closer_or_equal(hovered_map_sound_hit, hovered_adhesion_hit) &&
                   closer_or_equal(hovered_map_sound_hit, hovered_irregularity_hit) &&
                   closer_or_equal(hovered_map_sound_hit, hovered_background_hit) &&
                   closer_or_equal(hovered_map_sound_hit, hovered_cab_illuminance_hit) &&
                   closer_or_equal(hovered_map_sound_hit, hovered_fog_hit) &&
                   closer_or_equal(hovered_map_sound_hit, hovered_rolling_noise_hit) &&
                   closer_or_equal(hovered_map_sound_hit, hovered_map_sound_3d_hit) &&
                   closer_or_equal(hovered_map_sound_hit, hovered_flange_noise_hit) &&
                   closer_or_equal(hovered_map_sound_hit, hovered_joint_noise_hit) &&
                   (!hovered_repeater_hit || hovered_map_sound_hit->dist_sq <= hovered_repeater_hit->dist_sq) &&
                   (!hovered_structure_hit || hovered_map_sound_hit->dist_sq <= hovered_structure_hit->dist_sq)) {
            plan_map_sound_popup_row_ = static_cast<int>(hovered_map_sound_hit->row_index);
            ImGui::OpenPopup("plan_map_sound_marker_context");
        } else if (hovered_map_sound_3d_hit &&
                   closer_or_equal(hovered_map_sound_3d_hit, hovered_signal_hit) &&
                   closer_or_equal(hovered_map_sound_3d_hit, hovered_beacon_hit) &&
                   closer_or_equal(hovered_map_sound_3d_hit, hovered_adhesion_hit) &&
                   closer_or_equal(hovered_map_sound_3d_hit, hovered_irregularity_hit) &&
                   closer_or_equal(hovered_map_sound_3d_hit, hovered_background_hit) &&
                   closer_or_equal(hovered_map_sound_3d_hit, hovered_cab_illuminance_hit) &&
                   closer_or_equal(hovered_map_sound_3d_hit, hovered_fog_hit) &&
                   closer_or_equal(hovered_map_sound_3d_hit, hovered_rolling_noise_hit) &&
                   closer_or_equal(hovered_map_sound_3d_hit, hovered_map_sound_hit) &&
                   closer_or_equal(hovered_map_sound_3d_hit, hovered_flange_noise_hit) &&
                   closer_or_equal(hovered_map_sound_3d_hit, hovered_joint_noise_hit) &&
                   (!hovered_repeater_hit || hovered_map_sound_3d_hit->dist_sq <= hovered_repeater_hit->dist_sq) &&
                   (!hovered_structure_hit || hovered_map_sound_3d_hit->dist_sq <= hovered_structure_hit->dist_sq)) {
            plan_map_sound_3d_popup_row_ = static_cast<int>(hovered_map_sound_3d_hit->row_index);
            ImGui::OpenPopup("plan_map_sound_3d_marker_context");
        } else if (hovered_flange_noise_hit &&
                   closer_or_equal(hovered_flange_noise_hit, hovered_adhesion_hit) &&
                   closer_or_equal(hovered_flange_noise_hit, hovered_irregularity_hit) &&
                   closer_or_equal(hovered_flange_noise_hit, hovered_background_hit) &&
                   closer_or_equal(hovered_flange_noise_hit, hovered_cab_illuminance_hit) &&
                   closer_or_equal(hovered_flange_noise_hit, hovered_fog_hit) &&
                   closer_or_equal(hovered_flange_noise_hit, hovered_rolling_noise_hit) &&
                   closer_or_equal(hovered_flange_noise_hit, hovered_map_sound_hit) &&
                   closer_or_equal(hovered_flange_noise_hit, hovered_map_sound_3d_hit) &&
                   closer_or_equal(hovered_flange_noise_hit, hovered_joint_noise_hit) &&
                   (!hovered_repeater_hit || hovered_flange_noise_hit->dist_sq <= hovered_repeater_hit->dist_sq) &&
                   (!hovered_structure_hit || hovered_flange_noise_hit->dist_sq <= hovered_structure_hit->dist_sq)) {
            plan_flange_noise_popup_row_ = static_cast<int>(hovered_flange_noise_hit->row_index);
            ImGui::OpenPopup("plan_flange_noise_marker_context");
        } else if (hovered_joint_noise_hit &&
                   closer_or_equal(hovered_joint_noise_hit, hovered_adhesion_hit) &&
                   closer_or_equal(hovered_joint_noise_hit, hovered_irregularity_hit) &&
                   closer_or_equal(hovered_joint_noise_hit, hovered_background_hit) &&
                   closer_or_equal(hovered_joint_noise_hit, hovered_cab_illuminance_hit) &&
                   closer_or_equal(hovered_joint_noise_hit, hovered_fog_hit) &&
                   closer_or_equal(hovered_joint_noise_hit, hovered_rolling_noise_hit) &&
                   closer_or_equal(hovered_joint_noise_hit, hovered_map_sound_hit) &&
                   closer_or_equal(hovered_joint_noise_hit, hovered_map_sound_3d_hit) &&
                   closer_or_equal(hovered_joint_noise_hit, hovered_flange_noise_hit) &&
                   (!hovered_repeater_hit || hovered_joint_noise_hit->dist_sq <= hovered_repeater_hit->dist_sq) &&
                   (!hovered_structure_hit || hovered_joint_noise_hit->dist_sq <= hovered_structure_hit->dist_sq)) {
            plan_joint_noise_popup_row_ = static_cast<int>(hovered_joint_noise_hit->row_index);
            ImGui::OpenPopup("plan_joint_noise_marker_context");
        } else if (hovered_background_hit &&
                   closer_or_equal(hovered_background_hit, hovered_adhesion_hit) &&
                   closer_or_equal(hovered_background_hit, hovered_irregularity_hit) &&
                   closer_or_equal(hovered_background_hit, hovered_cab_illuminance_hit) &&
                   closer_or_equal(hovered_background_hit, hovered_fog_hit) &&
                   closer_or_equal(hovered_background_hit, hovered_rolling_noise_hit) &&
                   closer_or_equal(hovered_background_hit, hovered_map_sound_hit) &&
                   closer_or_equal(hovered_background_hit, hovered_map_sound_3d_hit) &&
                   closer_or_equal(hovered_background_hit, hovered_flange_noise_hit) &&
                   closer_or_equal(hovered_background_hit, hovered_joint_noise_hit) &&
                   (!hovered_repeater_hit || hovered_background_hit->dist_sq <= hovered_repeater_hit->dist_sq) &&
                   (!hovered_structure_hit || hovered_background_hit->dist_sq <= hovered_structure_hit->dist_sq)) {
            plan_background_popup_row_ = static_cast<int>(hovered_background_hit->row_index);
            ImGui::OpenPopup("plan_background_marker_context");
        } else if (hovered_repeater_hit &&
                   closer_or_equal(hovered_repeater_hit, hovered_adhesion_hit) &&
                   closer_or_equal(hovered_repeater_hit, hovered_background_hit) &&
                   closer_or_equal(hovered_repeater_hit, hovered_cab_illuminance_hit) &&
                   closer_or_equal(hovered_repeater_hit, hovered_fog_hit) &&
                   closer_or_equal(hovered_repeater_hit, hovered_rolling_noise_hit) &&
                   closer_or_equal(hovered_repeater_hit, hovered_map_sound_hit) &&
                   closer_or_equal(hovered_repeater_hit, hovered_map_sound_3d_hit) &&
                   closer_or_equal(hovered_repeater_hit, hovered_flange_noise_hit) &&
                   closer_or_equal(hovered_repeater_hit, hovered_joint_noise_hit) &&
                   (!hovered_structure_hit || hovered_repeater_hit->dist_sq <= hovered_structure_hit->dist_sq)) {
            plan_repeater_popup_row_ = static_cast<int>(hovered_repeater_hit->row_index);
            ImGui::OpenPopup("plan_repeater_marker_context");
        } else if (hovered_cab_illuminance_hit &&
                   closer_or_equal(hovered_cab_illuminance_hit, hovered_background_hit) &&
                   closer_or_equal(hovered_cab_illuminance_hit, hovered_fog_hit) &&
                   closer_or_equal(hovered_cab_illuminance_hit, hovered_rolling_noise_hit) &&
                   closer_or_equal(hovered_cab_illuminance_hit, hovered_map_sound_hit) &&
                   closer_or_equal(hovered_cab_illuminance_hit, hovered_map_sound_3d_hit) &&
                   closer_or_equal(hovered_cab_illuminance_hit, hovered_flange_noise_hit) &&
                   closer_or_equal(hovered_cab_illuminance_hit, hovered_joint_noise_hit) &&
                   (!hovered_structure_hit || hovered_cab_illuminance_hit->dist_sq <= hovered_structure_hit->dist_sq)) {
            plan_cab_illuminance_popup_row_ = static_cast<int>(hovered_cab_illuminance_hit->row_index);
            ImGui::OpenPopup("plan_cab_illuminance_marker_context");
        } else if (hovered_fog_hit &&
                   closer_or_equal(hovered_fog_hit, hovered_background_hit) &&
                   closer_or_equal(hovered_fog_hit, hovered_rolling_noise_hit) &&
                   closer_or_equal(hovered_fog_hit, hovered_map_sound_hit) &&
                   closer_or_equal(hovered_fog_hit, hovered_map_sound_3d_hit) &&
                   closer_or_equal(hovered_fog_hit, hovered_flange_noise_hit) &&
                   closer_or_equal(hovered_fog_hit, hovered_joint_noise_hit) &&
                   (!hovered_structure_hit || hovered_fog_hit->dist_sq <= hovered_structure_hit->dist_sq)) {
            plan_fog_popup_row_ = static_cast<int>(hovered_fog_hit->row_index);
            ImGui::OpenPopup("plan_fog_marker_context");
        } else if (hovered_structure_hit) {
            plan_structure_popup_row_ = static_cast<int>(hovered_structure_hit->row_index);
            ImGui::OpenPopup("plan_structure_marker_context");
        }
    }

    bool rotate_plan = hovered && io.KeyCtrl && ImGui::IsMouseDown(ImGuiMouseButton_Left);
    if (hovered && mode_ == Mode::Pan && ImGui::IsMouseDown(ImGuiMouseButton_Left) && !rotate_plan) {
        if (!plan_view_.dragging) {
            plan_view_.dragging = true;
            plan_view_.last_mouse = mouse;
        } else {
            ImVec2 delta(mouse.x - plan_view_.last_mouse.x, mouse.y - plan_view_.last_mouse.y);
            plan_view_.pan_by_screen_delta(delta);
            plan_view_.last_mouse = mouse;
        }
        set_move_cursor();
    } else {
        plan_view_.dragging = false;
    }

    if (rotate_plan) {
        if (!plan_view_.rotating) {
            plan_view_.rotating = true;
            plan_view_.last_mouse = mouse;
        } else {
            ImVec2 center(origin.x + avail.x * 0.5f, origin.y + avail.y * 0.5f);
            double a0 = std::atan2(plan_view_.last_mouse.y - center.y, plan_view_.last_mouse.x - center.x);
            double a1 = std::atan2(mouse.y - center.y, mouse.x - center.x);
            plan_view_.rotation += a1 - a0;
            plan_view_.last_mouse = mouse;
        }
    } else {
        plan_view_.rotating = false;
    }

    if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        ImVec2 world = plan_view_.screen_to_world(mouse, origin, avail);
        if (pick_slot_ == 1) {
            if (auto uv = background_uv_from_world(world)) align_pick1_ = *uv;
            pick_slot_ = 0;
            popups_.background_align = true;
        } else if (pick_slot_ == 2) {
            if (auto uv = background_uv_from_world(world)) align_pick2_ = *uv;
            pick_slot_ = 0;
            popups_.background_align = true;
        } else if (mode_ == Mode::Measure) {
            if (auto clicked_distance = nearest_measure_distance()) {
                update_measure(*clicked_distance);
                request_plot_focus(*clicked_distance, true, true);
            }
        } else if (!data.own.empty()) {
            PlanData fit_data = build_plan_data(true);
            const PlanData& bounds = fit_data.own.empty() ? data : fit_data;
            plan_view_.fit(bounds.xmin, bounds.ymin, bounds.xmax, bounds.ymax, avail);
        }
    }

    PlanScreenTransform transform = make_plan_transform(plan_view_, -data.origin_angle, origin, avail);
    const CanvasLineWidthSettings line_widths = clamp_canvas_line_widths(canvas_line_widths_);
    const float background_grid_line_width = line_widths.background_grid_px;

    if (grid_mode_ == GridMode::Fixed) {
        for (float x = origin.x; x <= origin.x + avail.x; x += 80.0f) draw->AddLine(ImVec2(x, origin.y), ImVec2(x, origin.y + avail.y), IM_COL32(48, 52, 58, 255), background_grid_line_width);
        for (float y = origin.y; y <= origin.y + avail.y; y += 80.0f) draw->AddLine(ImVec2(origin.x, y), ImVec2(origin.x + avail.x, y), IM_COL32(48, 52, 58, 255), background_grid_line_width);
    } else if (grid_mode_ == GridMode::Movable) {
        ImVec2 screen_corners[] = {
            origin,
            ImVec2(origin.x + avail.x, origin.y),
            ImVec2(origin.x, origin.y + avail.y),
            ImVec2(origin.x + avail.x, origin.y + avail.y)
        };
        ImVec2 first = plan_view_.screen_to_world(screen_corners[0], origin, avail);
        double xmin = first.x, xmax = first.x;
        double ymin = first.y, ymax = first.y;
        for (int i = 1; i < IM_ARRAYSIZE(screen_corners); ++i) {
            ImVec2 c = plan_view_.screen_to_world(screen_corners[i], origin, avail);
            xmin = std::min(xmin, static_cast<double>(c.x));
            xmax = std::max(xmax, static_cast<double>(c.x));
            ymin = std::min(ymin, static_cast<double>(c.y));
            ymax = std::max(ymax, static_cast<double>(c.y));
        }
        double step = grid_step(std::max(xmax - xmin, ymax - ymin));
        xmin = std::floor(xmin / step) * step - step;
        xmax = std::ceil(xmax / step) * step + step;
        ymin = std::floor(ymin / step) * step - step;
        ymax = std::ceil(ymax / step) * step + step;
        for (double x = xmin; x <= xmax; x += step) {
            ImVec2 a = plan_view_.world_to_screen(x, ymin, origin, avail);
            ImVec2 b = plan_view_.world_to_screen(x, ymax, origin, avail);
            draw->AddLine(a, b, IM_COL32(48, 52, 58, 255), background_grid_line_width);
        }
        for (double y = ymin; y <= ymax; y += step) {
            ImVec2 a = plan_view_.world_to_screen(xmin, y, origin, avail);
            ImVec2 b = plan_view_.world_to_screen(xmax, y, origin, avail);
            draw->AddLine(a, b, IM_COL32(48, 52, 58, 255), background_grid_line_width);
        }
    }

    draw_background(draw, plan_view_, origin, avail);
    debug_plan_stage("background_grid");

    auto draw_section = [&](const Section& sec, ImU32 color, float width) {
        auto first = std::lower_bound(data.own.begin(), data.own.end(), sec.start,
                                      [](const TrackPoint& p, double d) { return p.d < d; });
        auto last = std::upper_bound(data.own.begin(), data.own.end(), sec.end,
                                     [](double d, const TrackPoint& p) { return d < p.d; });
        draw_polyline_range(draw, data.own,
                            static_cast<size_t>(first - data.own.begin()),
                            static_cast<size_t>(last - data.own.begin()),
                            transform, origin, avail, color, width);
    };
    if (show_curve_values_) {
        for (const auto& s : data.curve_sections) draw_section(s, IM_COL32(130, 130, 130, 220), 10.0f * marker_size_scale);
        for (const auto& s : data.transition_sections) draw_section(s, IM_COL32(84, 84, 84, 220), 8.0f * marker_size_scale);
    }
    draw_polyline(draw, data.own, transform, origin, avail, IM_COL32(245, 245, 245, 255), line_widths.own_track_px);
    for (const auto& t : model_.other_tracks) {
        if (!t.visible || t.points.empty()) continue;
        double rmin = std::max(dmin_, t.range_min);
        double rmax = std::min(dmax_, t.range_max);
        draw_matrix_plan_polyline(draw, t.points, rmin, rmax, transform, origin, avail, color_u32(t.color), line_widths.other_track_px);
    }
    debug_plan_stage("tracks");

    if (show_stations_) {
        const float station_marker_radius = kDefaultStationMarkerSize * marker_size_scale;
        const float station_label_offset = std::max(8.0f, station_marker_radius + 4.0f);
        for (const auto& st : data.stations) {
            ImVec2 p = transform.plan_to_screen(st.x, st.y);
            draw->AddCircleFilled(p, station_marker_radius, IM_COL32(255, 255, 255, 255));
            if (show_station_names_) draw->AddText(ImVec2(p.x + station_label_offset, p.y - 16), IM_COL32(255, 255, 255, 255), st.station.name.c_str());
            if (show_station_mileage_) draw->AddText(ImVec2(p.x + station_label_offset, p.y + 4), IM_COL32(255, 216, 77, 255), (format_double(st.station.mileage, 0) + "m").c_str());
        }
    }

    if (show_speedlimits_) {
        for (const auto& sp : data.speedlimits) {
            ImVec2 p = transform.plan_to_screen(sp.x, sp.y);
            double wx = sp.x - std::sin(sp.theta);
            double wy = sp.y + std::cos(sp.theta);
            ImVec2 q = transform.plan_to_screen(wx, wy);
            ImVec2 d(q.x - p.x, q.y - p.y);
            float len = std::max(1.0f, std::sqrt(d.x * d.x + d.y * d.y));
            d.x = d.x / len * (8.0f * marker_size_scale);
            d.y = d.y / len * (8.0f * marker_size_scale);
            draw->AddLine(ImVec2(p.x - d.x, p.y - d.y), ImVec2(p.x + d.x, p.y + d.y), IM_COL32(136, 204, 255, 255), std::max(1.0f, marker_size_scale));
            std::string label = sp.has_speed ? format_double(sp.speed, 0) : "x";
            draw->AddText(ImVec2(p.x + std::max(10.0f, 10.0f * marker_size_scale), p.y - 15), IM_COL32(136, 204, 255, 255), label.c_str());
        }
    }

    if (!data.signal_markers.empty()) {
        ImU32 signal_color = IM_COL32(148, 242, 178, 255);
        for (const auto& marker : data.signal_markers) {
            ImVec2 p = transform.plan_to_screen(marker.x, marker.y);
            if (!point_near_canvas(p, origin, avail)) continue;
            double dx = static_cast<double>(p.x - mouse.x);
            double dy = static_cast<double>(p.y - mouse.y);
            bool marker_hovered = hovered_signal_row && *hovered_signal_row == marker.row_index &&
                dx * dx + dy * dy <= marker_hover_radius_sq;
            draw_plan_signal_marker(draw, p, signal_color, marker_size_scale * (marker_hovered ? 1.28f : 1.0f));
            if (marker_hovered) draw_plan_small_text(draw, p, signal_color, marker.label);
        }
    }
    debug_plan_stage("signal_markers");

    if (!data.beacon_markers.empty()) {
        ImU32 beacon_color = IM_COL32(148, 242, 178, 255);
        for (const auto& marker : data.beacon_markers) {
            ImVec2 p = transform.plan_to_screen(marker.x, marker.y);
            if (!point_near_canvas(p, origin, avail)) continue;
            double dx = static_cast<double>(p.x - mouse.x);
            double dy = static_cast<double>(p.y - mouse.y);
            bool marker_hovered = hovered_beacon_row && *hovered_beacon_row == marker.row_index &&
                dx * dx + dy * dy <= marker_hover_radius_sq;
            draw_plan_beacon_marker(draw, p, beacon_color, marker_size_scale * (marker_hovered ? 1.28f : 1.0f));
            if (marker_hovered) draw_plan_small_text(draw, p, beacon_color, marker.label);
        }
    }
    debug_plan_stage("beacon_markers");

    if (!data.pretrain_markers.empty()) {
        for (const auto& marker : data.pretrain_markers) {
            ImVec2 p = transform.plan_to_screen(marker.x, marker.y);
            if (!point_near_canvas(p, origin, avail)) continue;
            double dx = static_cast<double>(p.x - mouse.x);
            double dy = static_cast<double>(p.y - mouse.y);
            bool marker_hovered = hovered_pretrain_row && *hovered_pretrain_row == marker.row_index &&
                dx * dx + dy * dy <= marker_hover_radius_sq;
            draw_plan_pretrain_marker(draw, p, marker.label, marker_size_scale * (marker_hovered ? 1.22f : 1.0f));
        }
    }
    debug_plan_stage("pretrain_markers");

    if (!data.irregularity_markers.empty()) {
        ImU32 irregularity_color = IM_COL32(204, 170, 255, 255);
        for (const auto& marker : data.irregularity_markers) {
            ImVec2 p = transform.plan_to_screen(marker.x, marker.y);
            if (!point_near_canvas(p, origin, avail)) continue;
            double dx = static_cast<double>(p.x - mouse.x);
            double dy = static_cast<double>(p.y - mouse.y);
            bool marker_hovered = hovered_irregularity_row && *hovered_irregularity_row == marker.row_index &&
                dx * dx + dy * dy <= marker_hover_radius_sq;
            draw_plan_wave_marker(draw, p, irregularity_color, marker_size_scale * (marker_hovered ? 1.28f : 1.0f));
            if (marker_hovered) draw_plan_small_text(draw, p, irregularity_color, marker.label);
        }
    }
    debug_plan_stage("irregularity_markers");

    if (!data.map_sound_markers.empty()) {
        ImU32 map_sound_color = IM_COL32(222, 190, 255, 255);
        for (const auto& marker : data.map_sound_markers) {
            ImVec2 p = transform.plan_to_screen(marker.x, marker.y);
            if (!point_near_canvas(p, origin, avail)) continue;
            double dx = static_cast<double>(p.x - mouse.x);
            double dy = static_cast<double>(p.y - mouse.y);
            bool marker_hovered = hovered_map_sound_row && *hovered_map_sound_row == marker.row_index &&
                dx * dx + dy * dy <= marker_hover_radius_sq;
            draw_plan_speaker_marker(draw, p, map_sound_color, marker_size_scale * (marker_hovered ? 1.28f : 1.0f));
            if (marker_hovered) draw_plan_small_text(draw, p, map_sound_color, marker.label);
        }
    }
    debug_plan_stage("map_sound_markers");

    if (!data.map_sound_3d_markers.empty()) {
        ImU32 map_sound_3d_color = IM_COL32(222, 190, 255, 255);
        for (const auto& marker : data.map_sound_3d_markers) {
            ImVec2 p = transform.plan_to_screen(marker.x, marker.y);
            if (!point_near_canvas(p, origin, avail)) continue;
            double dx = static_cast<double>(p.x - mouse.x);
            double dy = static_cast<double>(p.y - mouse.y);
            bool marker_hovered = hovered_map_sound_3d_row && *hovered_map_sound_3d_row == marker.row_index &&
                dx * dx + dy * dy <= marker_hover_radius_sq;
            draw_plan_broadcast_marker(draw, p, map_sound_3d_color, marker_size_scale * (marker_hovered ? 1.28f : 1.0f));
            if (marker_hovered) draw_plan_small_text(draw, p, map_sound_3d_color, marker.label);
        }
    }
    debug_plan_stage("map_sound_3d_markers");

    if (!data.rolling_noise_markers.empty()) {
        ImU32 rolling_noise_color = IM_COL32(126, 214, 255, 255);
        for (const auto& marker : data.rolling_noise_markers) {
            ImVec2 p = transform.plan_to_screen(marker.x, marker.y);
            if (!point_near_canvas(p, origin, avail)) continue;
            double dx = static_cast<double>(p.x - mouse.x);
            double dy = static_cast<double>(p.y - mouse.y);
            bool marker_hovered = hovered_rolling_noise_row && *hovered_rolling_noise_row == marker.row_index &&
                dx * dx + dy * dy <= marker_hover_radius_sq;
            draw_plan_axle_marker(draw, p, rolling_noise_color, marker_size_scale * (marker_hovered ? 1.28f : 1.0f));
            if (marker_hovered) draw_plan_small_text(draw, p, rolling_noise_color, marker.label);
        }
    }
    debug_plan_stage("rolling_noise_markers");

    if (!data.flange_noise_markers.empty()) {
        ImU32 flange_noise_color = IM_COL32(214, 176, 255, 255);
        for (const auto& marker : data.flange_noise_markers) {
            ImVec2 p = transform.plan_to_screen(marker.x, marker.y);
            if (!point_near_canvas(p, origin, avail)) continue;
            double dx = static_cast<double>(p.x - mouse.x);
            double dy = static_cast<double>(p.y - mouse.y);
            bool marker_hovered = hovered_flange_noise_row && *hovered_flange_noise_row == marker.row_index &&
                dx * dx + dy * dy <= marker_hover_radius_sq;
            draw_plan_flange_noise_marker(draw, p, flange_noise_color, marker_size_scale * (marker_hovered ? 1.28f : 1.0f));
            if (marker_hovered) draw_plan_small_text(draw, p, flange_noise_color, marker.label);
        }
    }
    debug_plan_stage("flange_noise_markers");

    if (!data.joint_noise_markers.empty()) {
        ImU32 joint_noise_color = IM_COL32(158, 224, 255, 255);
        for (const auto& marker : data.joint_noise_markers) {
            ImVec2 p = transform.plan_to_screen(marker.x, marker.y);
            if (!point_near_canvas(p, origin, avail)) continue;
            double dx = static_cast<double>(p.x - mouse.x);
            double dy = static_cast<double>(p.y - mouse.y);
            bool marker_hovered = hovered_joint_noise_row && *hovered_joint_noise_row == marker.row_index &&
                dx * dx + dy * dy <= marker_hover_radius_sq;
            draw_plan_joint_noise_marker(draw, p, joint_noise_color, marker_size_scale * (marker_hovered ? 1.28f : 1.0f));
            if (marker_hovered) draw_plan_small_text(draw, p, joint_noise_color, marker.label);
        }
    }
    debug_plan_stage("joint_noise_markers");

    if (!data.background_markers.empty()) {
        ImU32 background_color = IM_COL32(255, 230, 72, 255);
        for (const auto& marker : data.background_markers) {
            ImVec2 p = transform.plan_to_screen(marker.x, marker.y);
            if (!point_near_canvas(p, origin, avail)) continue;
            double dx = static_cast<double>(p.x - mouse.x);
            double dy = static_cast<double>(p.y - mouse.y);
            bool marker_hovered = hovered_background_row && *hovered_background_row == marker.row_index &&
                dx * dx + dy * dy <= marker_hover_radius_sq;
            draw_plan_square_marker(draw, p, background_color, marker_size_scale * (marker_hovered ? 1.28f : 1.0f));
            if (marker_hovered) draw_plan_small_text(draw, p, background_color, marker.label);
        }
    }
    debug_plan_stage("background_change_markers");

    if (!data.adhesion_markers.empty()) {
        ImU32 adhesion_color = IM_COL32(178, 102, 255, 255);
        for (const auto& marker : data.adhesion_markers) {
            ImVec2 p = transform.plan_to_screen(marker.x, marker.y);
            if (!point_near_canvas(p, origin, avail)) continue;
            double dx = static_cast<double>(p.x - mouse.x);
            double dy = static_cast<double>(p.y - mouse.y);
            bool marker_hovered = hovered_adhesion_row && *hovered_adhesion_row == marker.row_index &&
                dx * dx + dy * dy <= marker_hover_radius_sq;
            draw_plan_adhesion_marker(draw, p, adhesion_color, marker_size_scale * (marker_hovered ? 1.28f : 1.0f));
            if (marker_hovered) draw_plan_small_text(draw, p, adhesion_color, marker.label);
        }
    }
    debug_plan_stage("adhesion_markers");

    if (!data.cab_illuminance_markers.empty()) {
        ImU32 cab_color = IM_COL32(255, 226, 64, 255);
        for (const auto& marker : data.cab_illuminance_markers) {
            ImVec2 p = transform.plan_to_screen(marker.x, marker.y);
            if (!point_near_canvas(p, origin, avail)) continue;
            double dx = static_cast<double>(p.x - mouse.x);
            double dy = static_cast<double>(p.y - mouse.y);
            bool marker_hovered = hovered_cab_illuminance_row && *hovered_cab_illuminance_row == marker.row_index &&
                dx * dx + dy * dy <= marker_hover_radius_sq;
            draw_plan_sun_marker(draw, p, cab_color, marker_size_scale * (marker_hovered ? 1.28f : 1.0f));
            if (marker_hovered) draw_plan_small_text(draw, p, cab_color, marker.label);
        }
    }
    debug_plan_stage("cab_illuminance_markers");

    if (!data.fog_markers.empty()) {
        for (const auto& marker : data.fog_markers) {
            ImVec2 p = transform.plan_to_screen(marker.x, marker.y);
            if (!point_near_canvas(p, origin, avail)) continue;
            double dx = static_cast<double>(p.x - mouse.x);
            double dy = static_cast<double>(p.y - mouse.y);
            bool marker_hovered = hovered_fog_row && *hovered_fog_row == marker.row_index &&
                dx * dx + dy * dy <= marker_hover_radius_sq;
            draw_plan_fog_marker(draw, p, marker_size_scale * (marker_hovered ? 1.28f : 1.0f));
            if (marker_hovered) draw_plan_small_text(draw, p, IM_COL32(255, 255, 255, 255), marker.label);
        }
    }
    debug_plan_stage("fog_markers");

    if (!repeater_marker_cache_.empty() || !data.repeater_markers.empty()) {
        ImU32 repeater_color = IM_COL32(255, 105, 190, 255);
        draw_repeater_segment_chunks(draw, repeater_marker_cache_, repeater_row_visible_,
                                     dmin_, dmax_, transform, origin, avail, repeater_color, 1.25f * marker_size_scale);
        debug_plan_stage("repeater_segments");
        if (!dense_repeater_marker_lod) {
            bool draw_repeater_labels = plan_view_.scale >= 0.025 || data.repeater_markers.size() <= 600;
            for (const auto& marker : data.repeater_markers) {
                ImVec2 p = transform.plan_to_screen(marker.x, marker.y);
                if (!point_near_canvas(p, origin, avail)) continue;
                double dx = static_cast<double>(p.x - mouse.x);
                double dy = static_cast<double>(p.y - mouse.y);
                bool marker_hovered = hovered_repeater_row && *hovered_repeater_row == marker.row_index &&
                    dx * dx + dy * dy <= marker_hover_radius_sq;
                float marker_scale = marker_size_scale * (marker_hovered ? 1.28f : 1.0f);
                draw_plan_diamond_marker(draw, p, repeater_color, marker_scale);
                if (draw_repeater_labels || marker_hovered) draw_plan_small_text(draw, p, repeater_color, marker.label);
            }
        }
        debug_plan_stage("repeater_markers");
    }

    if (!data.structure_markers.empty()) {
        ImU32 structure_color = IM_COL32(255, 216, 48, 255);
        for (const auto& marker : data.structure_markers) {
            ImVec2 p = transform.plan_to_screen(marker.x, marker.y);
            if (!point_near_canvas(p, origin, avail)) continue;
            float marker_scale = marker_size_scale *
                (hovered_structure_row && *hovered_structure_row == marker.row_index ? 1.28f : 1.0f);
            draw_plan_triangle_marker(draw, p, structure_color, marker_scale);
            draw_plan_small_text(draw, p, structure_color, marker.label);
        }
    }
    debug_plan_stage("structure_markers");

    if (show_curve_values_) {
        for (const auto& sec : data.curve_sections) {
            double mid = (sec.start + sec.end) * 0.5;
            auto it = std::lower_bound(data.own.begin(), data.own.end(), mid, [](const TrackPoint& p, double d) { return p.d < d; });
            if (it != data.own.end()) {
                ImVec2 p = transform.plan_to_screen(it->x, it->y);
                draw->AddText(ImVec2(p.x + 8, p.y - 16), IM_COL32(136, 255, 136, 255), format_double(sec.value, 0).c_str());
            }
        }
    }

    if (hovered_measure_distance) {
        update_measure(*hovered_measure_distance);
    }

    if (mode_ == Mode::Measure && measure_distance_ && !data.own.empty()) {
        auto it = std::lower_bound(data.own.begin(), data.own.end(), *measure_distance_, [](const TrackPoint& p, double d) { return p.d < d; });
        if (it == data.own.end()) {
            --it;
        } else if (it != data.own.begin() && std::abs((it - 1)->d - *measure_distance_) < std::abs(it->d - *measure_distance_)) {
            --it;
        }
        ImVec2 p = transform.plan_to_screen(it->x, it->y);
        draw->AddLine(ImVec2(p.x - 12, p.y - 12), ImVec2(p.x + 12, p.y + 12), IM_COL32(255, 51, 51, 255), 2.0f);
        draw->AddLine(ImVec2(p.x - 12, p.y + 12), ImVec2(p.x + 12, p.y - 12), IM_COL32(255, 51, 51, 255), 2.0f);
    }

    if (plan_focus_arrow_) {
        if (ImGui::GetTime() <= plan_focus_arrow_until_) {
            ImVec2 p = transform.plan_to_screen(plan_focus_arrow_->x, plan_focus_arrow_->y);
            if (point_near_canvas(p, origin, avail)) draw_plan_focus_arrow(draw, p);
        } else {
            plan_focus_arrow_.reset();
        }
    }

    if (show_scene_current_position_on_plan_ && scene_preview_started_ && scene_preview_canvas_) {
        Canvas3DSceneCameraPose pose = scene_preview_canvas_->scene_camera_pose();
        if (pose.valid && std::isfinite(pose.x) && std::isfinite(pose.y) && std::isfinite(pose.theta)) {
            ImVec2 p = transform.model_to_screen(pose.x, pose.y);
            ImVec2 q = transform.model_to_screen(pose.x + std::cos(pose.theta),
                                                 pose.y + std::sin(pose.theta));
            if (point_near_canvas(p, origin, avail)) {
                draw_plan_current_position_arrow(draw, p, ImVec2(q.x - p.x, q.y - p.y), marker_size_scale);
            }
        }
    }
    debug_plan_stage("scene_camera_marker");

    draw->AddText(ImVec2(origin.x + 8, origin.y + 8), IM_COL32(255, 255, 255, 255), tr("canvas.plan").c_str());
    draw_scalebar(draw, plan_view_, origin, avail);
    if (!data.own.empty()) plan_canvas_rendered_this_frame_ = true;
    draw->PopClipRect();
    debug_plan_stage("overlays_done");
    if (ImGui::BeginPopup("plan_structure_marker_context")) {
        bool can_locate = plan_structure_popup_row_ >= 0 &&
            static_cast<size_t>(plan_structure_popup_row_) < structure_marker_cache_.size();
        ImGui::BeginDisabled(!can_locate);
        if (ImGui::MenuItem(tr("menu.locate_in_structure_list").c_str()) && can_locate) {
            locate_structure_row_in_list(static_cast<size_t>(plan_structure_popup_row_));
        }
        ImGui::EndDisabled();
        ImGui::EndPopup();
    }
    if (ImGui::BeginPopup("plan_repeater_marker_context")) {
        bool can_locate = plan_repeater_popup_row_ >= 0 &&
            static_cast<size_t>(plan_repeater_popup_row_) < repeater_marker_cache_.size();
        ImGui::BeginDisabled(!can_locate);
        if (ImGui::MenuItem(tr("menu.locate_in_repeater_list").c_str()) && can_locate) {
            locate_repeater_row_in_list(static_cast<size_t>(plan_repeater_popup_row_));
        }
        ImGui::EndDisabled();
        ImGui::EndPopup();
    }
    if (ImGui::BeginPopup("plan_signal_marker_context")) {
        bool can_locate = plan_signal_popup_row_ >= 0 &&
            static_cast<size_t>(plan_signal_popup_row_) < signal_marker_cache_.size();
        ImGui::BeginDisabled(!can_locate);
        if (ImGui::MenuItem(tr("menu.locate_in_signal_list").c_str()) && can_locate) {
            locate_signal_row_in_list(static_cast<size_t>(plan_signal_popup_row_));
        }
        ImGui::EndDisabled();
        ImGui::EndPopup();
    }
    if (ImGui::BeginPopup("plan_beacon_marker_context")) {
        bool can_locate = plan_beacon_popup_row_ >= 0 &&
            static_cast<size_t>(plan_beacon_popup_row_) < beacon_marker_cache_.size();
        ImGui::BeginDisabled(!can_locate);
        if (ImGui::MenuItem(tr("menu.locate_in_beacon_list").c_str()) && can_locate) {
            locate_beacon_row_in_list(static_cast<size_t>(plan_beacon_popup_row_));
        }
        ImGui::EndDisabled();
        ImGui::EndPopup();
    }
    if (ImGui::BeginPopup("plan_irregularity_marker_context")) {
        bool can_locate = plan_irregularity_popup_row_ >= 0 &&
            static_cast<size_t>(plan_irregularity_popup_row_) < irregularity_marker_cache_.size();
        ImGui::BeginDisabled(!can_locate);
        if (ImGui::MenuItem(tr("menu.locate_in_irregularity_list").c_str()) && can_locate) {
            locate_irregularity_row_in_list(static_cast<size_t>(plan_irregularity_popup_row_));
        }
        ImGui::EndDisabled();
        ImGui::EndPopup();
    }
    if (ImGui::BeginPopup("plan_rolling_noise_marker_context")) {
        bool can_locate = plan_rolling_noise_popup_row_ >= 0 &&
            static_cast<size_t>(plan_rolling_noise_popup_row_) < rolling_noise_marker_cache_.size();
        ImGui::BeginDisabled(!can_locate);
        if (ImGui::MenuItem(tr("menu.locate_in_rolling_noise_list").c_str()) && can_locate) {
            locate_rolling_noise_row_in_list(static_cast<size_t>(plan_rolling_noise_popup_row_));
        }
        ImGui::EndDisabled();
        ImGui::EndPopup();
    }
    if (ImGui::BeginPopup("plan_map_sound_marker_context")) {
        bool can_locate = plan_map_sound_popup_row_ >= 0 &&
            static_cast<size_t>(plan_map_sound_popup_row_) < map_sound_marker_cache_.size();
        ImGui::BeginDisabled(!can_locate);
        if (ImGui::MenuItem(tr("menu.locate_in_map_sound_list").c_str()) && can_locate) {
            locate_map_sound_row_in_list(static_cast<size_t>(plan_map_sound_popup_row_));
        }
        ImGui::EndDisabled();
        ImGui::EndPopup();
    }
    if (ImGui::BeginPopup("plan_map_sound_3d_marker_context")) {
        bool can_locate = plan_map_sound_3d_popup_row_ >= 0 &&
            static_cast<size_t>(plan_map_sound_3d_popup_row_) < map_sound_3d_marker_cache_.size();
        ImGui::BeginDisabled(!can_locate);
        if (ImGui::MenuItem(tr("menu.locate_in_map_sound_3d_list").c_str()) && can_locate) {
            locate_map_sound_3d_row_in_list(static_cast<size_t>(plan_map_sound_3d_popup_row_));
        }
        ImGui::EndDisabled();
        ImGui::EndPopup();
    }
    if (ImGui::BeginPopup("plan_flange_noise_marker_context")) {
        bool can_locate = plan_flange_noise_popup_row_ >= 0 &&
            static_cast<size_t>(plan_flange_noise_popup_row_) < flange_noise_marker_cache_.size();
        ImGui::BeginDisabled(!can_locate);
        if (ImGui::MenuItem(tr("menu.locate_in_flange_noise_list").c_str()) && can_locate) {
            locate_flange_noise_row_in_list(static_cast<size_t>(plan_flange_noise_popup_row_));
        }
        ImGui::EndDisabled();
        ImGui::EndPopup();
    }
    if (ImGui::BeginPopup("plan_joint_noise_marker_context")) {
        bool can_locate = plan_joint_noise_popup_row_ >= 0 &&
            static_cast<size_t>(plan_joint_noise_popup_row_) < joint_noise_marker_cache_.size();
        ImGui::BeginDisabled(!can_locate);
        if (ImGui::MenuItem(tr("menu.locate_in_joint_noise_list").c_str()) && can_locate) {
            locate_joint_noise_row_in_list(static_cast<size_t>(plan_joint_noise_popup_row_));
        }
        ImGui::EndDisabled();
        ImGui::EndPopup();
    }
    if (ImGui::BeginPopup("plan_background_marker_context")) {
        bool can_locate = plan_background_popup_row_ >= 0 &&
            static_cast<size_t>(plan_background_popup_row_) < background_marker_cache_.size();
        ImGui::BeginDisabled(!can_locate);
        if (ImGui::MenuItem(tr("menu.locate_in_background_list").c_str()) && can_locate) {
            locate_background_row_in_list(static_cast<size_t>(plan_background_popup_row_));
        }
        ImGui::EndDisabled();
        ImGui::EndPopup();
    }
    if (ImGui::BeginPopup("plan_adhesion_marker_context")) {
        bool can_locate = plan_adhesion_popup_row_ >= 0 &&
            static_cast<size_t>(plan_adhesion_popup_row_) < adhesion_marker_cache_.size();
        ImGui::BeginDisabled(!can_locate);
        if (ImGui::MenuItem(tr("menu.locate_in_adhesion_list").c_str()) && can_locate) {
            locate_adhesion_row_in_list(static_cast<size_t>(plan_adhesion_popup_row_));
        }
        ImGui::EndDisabled();
        ImGui::EndPopup();
    }
    if (ImGui::BeginPopup("plan_cab_illuminance_marker_context")) {
        bool can_locate = plan_cab_illuminance_popup_row_ >= 0 &&
            static_cast<size_t>(plan_cab_illuminance_popup_row_) < cab_illuminance_marker_cache_.size();
        ImGui::BeginDisabled(!can_locate);
        if (ImGui::MenuItem(tr("menu.locate_in_cab_illuminance_list").c_str()) && can_locate) {
            locate_cab_illuminance_row_in_list(static_cast<size_t>(plan_cab_illuminance_popup_row_));
        }
        ImGui::EndDisabled();
        ImGui::EndPopup();
    }
    if (ImGui::BeginPopup("plan_fog_marker_context")) {
        bool can_locate = plan_fog_popup_row_ >= 0 &&
            static_cast<size_t>(plan_fog_popup_row_) < fog_marker_cache_.size();
        ImGui::BeginDisabled(!can_locate);
        if (ImGui::MenuItem(tr("menu.locate_in_fog_list").c_str()) && can_locate) {
            locate_fog_row_in_list(static_cast<size_t>(plan_fog_popup_row_));
        }
        ImGui::EndDisabled();
        ImGui::EndPopup();
    }
    ImGui::EndChild();
    debug_plan_stage("end");
}
