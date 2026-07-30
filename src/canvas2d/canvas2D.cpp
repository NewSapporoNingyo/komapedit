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
#include "canvas3D.h"
#include "map_marker_visuals.h"
#include "touch_input.h"
#include "repeater_linkage.h"

#include "imgui.h"
#include "imgui_internal.h"
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

std::optional<TrackPoint> sample_matrix_track_point_at_lower_bound(
    const Matrix& points, double distance, bool has_theta_column, size_t lower_bound_row) {
    if (points.empty() || points.cols < 3) return std::nullopt;
    const double first = points.at(0, 0);
    const double last = points.at(points.rows - 1, 0);
    constexpr double eps = 1e-6;
    if (distance < first - eps || distance > last + eps) return std::nullopt;
    if (distance <= first) return matrix_row_track_point(points, 0, has_theta_column);
    if (distance >= last) return matrix_row_track_point(points, points.rows - 1, has_theta_column);

    const size_t b_row = std::min(lower_bound_row, points.rows - 1);
    if (b_row == 0) return matrix_row_track_point(points, 0, has_theta_column);
    const size_t a_row = b_row - 1;
    TrackPoint a = matrix_row_track_point(points, a_row, has_theta_column);
    TrackPoint b = matrix_row_track_point(points, b_row, has_theta_column);
    const double span = b.d - a.d;
    const double t = std::abs(span) < eps
        ? 0.0
        : std::clamp((distance - a.d) / span, 0.0, 1.0);

    TrackPoint p;
    p.d = distance;
    p.x = a.x + (b.x - a.x) * t;
    p.y = a.y + (b.y - a.y) * t;
    p.z = a.z + (b.z - a.z) * t;
    p.theta = has_theta_column
        ? angle_lerp(a.theta, b.theta, t)
        : std::atan2(b.y - a.y, b.x - a.x);
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

constexpr size_t k_repeater_segment_chunk_point_limit = 192;
constexpr size_t k_dense_repeater_overlay_threshold = 1200;
constexpr double k_dense_repeater_overview_scale = 0.04;
constexpr double k_dense_repeater_segment_scale = 0.05;

bool dense_repeater_overview_lod(size_t visible_repeater_count, double scale) {
    return visible_repeater_count > k_dense_repeater_overlay_threshold &&
        scale < k_dense_repeater_overview_scale;
}

bool dense_repeater_segment_lod(size_t visible_repeater_count, double scale) {
    return visible_repeater_count > k_dense_repeater_overlay_threshold &&
        scale < k_dense_repeater_segment_scale;
}

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

void include_repeater_segment_endpoint(PlanRepeaterSegment& segment, const TrackPoint& p) {
    if (!segment.endpoints_valid) {
        segment.first_point = p;
        segment.last_point = p;
        segment.endpoints_valid = true;
        return;
    }
    if (std::abs(segment.first_point.d - p.d) < 1e-6 &&
        std::abs(segment.last_point.d - p.d) < 1e-6) {
        segment.first_point = p;
    }
    segment.last_point = p;
}

void append_repeater_segment_point(PlanRepeaterSegment& segment, const TrackPoint& p) {
    if (!segment.chunks.empty()) {
        PlanRepeaterSegment::Chunk& tail_chunk = segment.chunks.back();
        if (!tail_chunk.points.empty() && std::abs(tail_chunk.points.back().d - p.d) < 1e-6) {
            tail_chunk.points.back() = p;
            include_repeater_chunk_bounds(tail_chunk, p);
            include_repeater_segment_bounds(segment, p);
            include_repeater_segment_endpoint(segment, p);
            return;
        }
    }

    if (segment.chunks.empty() ||
        segment.chunks.back().points.size() >= k_repeater_segment_chunk_point_limit) {
        std::optional<TrackPoint> tail;
        if (!segment.chunks.empty() && !segment.chunks.back().points.empty()) {
            tail = segment.chunks.back().points.back();
        }
        segment.chunks.emplace_back();
        segment.chunks.back().points.reserve(k_repeater_segment_chunk_point_limit);
        if (tail) {
            segment.chunks.back().points.push_back(*tail);
            include_repeater_chunk_bounds(segment.chunks.back(), *tail);
        }
    }

    PlanRepeaterSegment::Chunk& chunk = segment.chunks.back();
    chunk.points.push_back(p);
    include_repeater_chunk_bounds(chunk, p);
    include_repeater_segment_bounds(segment, p);
    include_repeater_segment_endpoint(segment, p);
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
    plan_data_cache_.valid = false;
    ++plan_data_source_revision_;
    structure_marker_cache_.clear();
    repeater_marker_cache_.clear();
    signal_marker_cache_.clear();
    beacon_marker_cache_.clear();
    pretrain_marker_cache_.clear();
    other_train_stop_marker_cache_.clear();
    other_train_path_cache_.clear();
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
    draw_distance_marker_cache_.clear();
    if (!has_model_ || model_.own.empty()) return;

    std::map<std::string, TrackSource> track_sources;
    TrackSource own_source{&model_.own, true};
    for (const char* key : k_own_track_lookup_aliases) {
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

    auto sample_placement_track = [&](const std::string& key, double distance,
                                      double lateral, double forward) -> std::optional<TrackPoint> {
        const std::string normalized_key = normalize_track_lookup_key(key);
        std::optional<TrackSource> source = is_own_track_placement_key(normalized_key)
            ? std::optional<TrackSource>{own_source}
            : find_track_source(normalized_key);
        if (!source) source = own_source;
        auto sampled = sample_matrix_track_point(*source->points, distance, source->has_theta_column);
        if (!sampled) return std::nullopt;
        return offset_track_point(*sampled, lateral, forward);
    };

    auto make_marker = [](double distance, const TrackPoint& p, const std::string& label,
                          const std::string& edit_id, size_t row_index) {
        PlanStructureMarker marker;
        marker.d = distance;
        marker.x = p.x;
        marker.y = p.y;
        marker.label = label;
        marker.edit_id = edit_id;
        marker.row_index = row_index;
        return marker;
    };

    structure_marker_cache_.reserve(model_.structures.size() + model_.structures_between.size());
    for (const auto& row : model_.structures) {
        double distance = table_cell_number(row, "distance");
        double lateral = table_cell_number(row, "x");
        double forward = table_cell_number(row, "z");
        if (auto p = sample_placement_track(table_cell(row, "trackKey"), distance,
                                            lateral, forward)) {
            structure_marker_cache_.push_back(make_marker(distance, *p, table_cell(row, "structureKey"),
                                                          row.edit_id,
                                                          structure_marker_cache_.size()));
        } else {
            structure_marker_cache_.push_back(std::nullopt);
        }
    }
    for (const auto& row : model_.structures_between) {
        double distance = table_cell_number(row, "distance");
        auto p1 = sample_placement_track(table_cell(row, "trackKey1"), distance, 0.0, 0.0);
        auto p2 = sample_placement_track(table_cell(row, "trackKey2"), distance, 0.0, 0.0);
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
                                                      row.edit_id,
                                                      structure_marker_cache_.size()));
    }

    auto build_standard_markers = [&](const std::vector<TableRow>& rows, auto& cache,
                                      const std::string& label_key,
                                      bool use_row_placement = false) {
        cache.reserve(rows.size());
        for (const TableRow& row : rows) {
            const double distance = table_cell_number(row, "distance");
            std::optional<TrackPoint> point = use_row_placement
                ? sample_placement_track(table_cell(row, "trackKey"), distance,
                                         table_cell_number(row, "x"), table_cell_number(row, "z"))
                : sample_track("", distance, 0.0, 0.0);
            if (!point) {
                cache.push_back(std::nullopt);
                continue;
            }

            PlanMarker marker;
            marker.d = distance;
            marker.x = point->x;
            marker.y = point->y;
            marker.label = label_key.empty() ? std::string{} : table_cell(row, label_key);
            if (marker.label.empty()) marker.label = "#" + std::to_string(cache.size() + 1);
            marker.edit_id = row.edit_id;
            marker.row_index = cache.size();
            cache.push_back(std::move(marker));
        }
    };

    build_standard_markers(model_.signals, signal_marker_cache_, "signalAspectKey", true);
    build_standard_markers(model_.beacons, beacon_marker_cache_, "type");
    build_standard_markers(model_.pretrains, pretrain_marker_cache_, "passTime");
    auto build_other_train_path_points = [&](const std::string& key, double start, double end) {
        std::vector<TrackPoint> points;
        if (end < start) std::swap(start, end);
        std::string normalized_key = normalize_track_lookup_key(key);
        auto source = find_track_source(normalized_key);
        if (!source && is_own_track_lookup_alias(normalized_key)) source = own_source;
        if (!source || !source->points || source->points->empty() || source->points->cols < 3) return points;

        auto append_sample = [&](double distance) {
            auto sampled = sample_matrix_track_point(*source->points, distance, source->has_theta_column);
            if (!sampled) return;
            if (!points.empty() && std::abs(points.back().d - sampled->d) < 1e-6) {
                points.back() = *sampled;
                return;
            }
            points.push_back(*sampled);
        };

        append_sample(start);
        size_t first = matrix_upper_bound_distance(*source->points, start);
        size_t last = matrix_lower_bound_distance(*source->points, end);
        for (size_t row_index = first; row_index < last; ++row_index) {
            append_sample(source->points->at(row_index, 0));
        }
        append_sample(end);
        return points;
    };

    struct OtherTrainDefinitionState {
        size_t row_index = 0;
        double distance = 0.0;
        double order = 0.0;
        std::string train_key;
        std::string normalized_train_key;
        std::string track_key;
        bool reverse_direction = false;
        std::vector<size_t> stop_rows;
    };

    std::vector<OtherTrainDefinitionState> other_train_definitions;
    other_train_definitions.reserve(model_.other_trains.size());
    std::map<std::string, std::vector<size_t>> definitions_by_train_key;
    for (size_t row_index = 0; row_index < model_.other_trains.size(); ++row_index) {
        const TableRow& row = model_.other_trains[row_index];
        OtherTrainDefinitionState state;
        state.row_index = row_index;
        state.distance = table_cell_number(row, "distance");
        state.order = table_cell_number(row, "order");
        state.train_key = table_cell(row, "trainKey");
        state.normalized_train_key = normalize_track_lookup_key(state.train_key);
        state.track_key = table_cell(row, "trackKey");
        state.reverse_direction = table_cell_number(row, "direction") < 0.0;
        definitions_by_train_key[state.normalized_train_key].push_back(other_train_definitions.size());
        other_train_definitions.push_back(std::move(state));
    }
    auto by_distance_order = [&](size_t lhs, size_t rhs) {
        const auto& a = other_train_definitions[lhs];
        const auto& b = other_train_definitions[rhs];
        if (a.distance < b.distance) return true;
        if (a.distance > b.distance) return false;
        if (a.order < b.order) return true;
        if (a.order > b.order) return false;
        return a.row_index < b.row_index;
    };
    for (auto& kv : definitions_by_train_key) {
        std::stable_sort(kv.second.begin(), kv.second.end(), by_distance_order);
    }

    for (size_t stop_row_index = 0; stop_row_index < model_.other_train_stops.size(); ++stop_row_index) {
        const TableRow& row = model_.other_train_stops[stop_row_index];
        std::string normalized_train_key = normalize_track_lookup_key(table_cell(row, "trainKey"));
        auto definitions_it = definitions_by_train_key.find(normalized_train_key);
        if (definitions_it == definitions_by_train_key.end() || definitions_it->second.empty()) continue;
        const double stop_distance = table_cell_number(row, "distance");
        const double stop_order = table_cell_number(row, "order");
        size_t chosen_definition = definitions_it->second.front();
        for (size_t definition_index : definitions_it->second) {
            const auto& definition = other_train_definitions[definition_index];
            if (definition.distance < stop_distance - 1e-6 ||
                (std::abs(definition.distance - stop_distance) <= 1e-6 && definition.order <= stop_order)) {
                chosen_definition = definition_index;
                continue;
            }
            break;
        }
        other_train_definitions[chosen_definition].stop_rows.push_back(stop_row_index);
    }

    other_train_stop_marker_cache_.assign(model_.other_train_stops.size(), std::nullopt);
    for (auto& definition : other_train_definitions) {
        std::stable_sort(definition.stop_rows.begin(), definition.stop_rows.end(),
            [&](size_t lhs, size_t rhs) {
                const TableRow& a = model_.other_train_stops[lhs];
                const TableRow& b = model_.other_train_stops[rhs];
                double ad = table_cell_number(a, "distance");
                double bd = table_cell_number(b, "distance");
                if (ad < bd) return true;
                if (ad > bd) return false;
                double ao = table_cell_number(a, "order");
                double bo = table_cell_number(b, "order");
                if (ao < bo) return true;
                if (ao > bo) return false;
                return lhs < rhs;
            });

        std::vector<size_t> valid_stop_rows;
        for (size_t stop_row_index : definition.stop_rows) {
            const TableRow& stop_row = model_.other_train_stops[stop_row_index];
            double distance = table_cell_number(stop_row, "distance");
            auto p = sample_track_base(definition.track_key, distance);
            if (!p) continue;

            PlanOtherTrainStopMarker marker;
            marker.d = distance;
            marker.x = p->x;
            marker.y = p->y;
            marker.theta = p->theta;
            marker.label = definition.train_key.empty()
                ? "#" + std::to_string(definition.row_index + 1)
                : definition.train_key;
            marker.edit_id = stop_row.edit_id;
            marker.row_index = stop_row_index;
            marker.definition_row_index = definition.row_index;
            marker.reverse_direction = definition.reverse_direction;
            other_train_stop_marker_cache_[stop_row_index] = marker;
            valid_stop_rows.push_back(stop_row_index);
        }

        if (valid_stop_rows.size() < 2) continue;
        double start = table_cell_number(model_.other_train_stops[valid_stop_rows.front()], "distance");
        double end = table_cell_number(model_.other_train_stops[valid_stop_rows.back()], "distance");
        std::vector<TrackPoint> points = build_other_train_path_points(definition.track_key, start, end);
        if (points.size() < 2) continue;

        OtherTrainPathOverlay path;
        path.points = std::move(points);
        path.label = definition.train_key.empty()
            ? "#" + std::to_string(definition.row_index + 1)
            : definition.train_key;
        path.definition_row_index = definition.row_index;
        path.d_min = std::min(start, end);
        path.d_max = std::max(start, end);
        path.reverse_direction = definition.reverse_direction;
        other_train_path_cache_.push_back(std::move(path));
    }

    build_standard_markers(model_.irregularities, irregularity_marker_cache_, "");
    build_standard_markers(model_.map_sounds, map_sound_marker_cache_, "soundKey");
    build_standard_markers(model_.map_sound_3d, map_sound_3d_marker_cache_, "soundKey");
    build_standard_markers(model_.rolling_noises, rolling_noise_marker_cache_, "");
    build_standard_markers(model_.flange_noises, flange_noise_marker_cache_, "");
    build_standard_markers(model_.joint_noises, joint_noise_marker_cache_, "");
    build_standard_markers(model_.backgrounds, background_marker_cache_, "structureKey");
    build_standard_markers(model_.adhesions, adhesion_marker_cache_, "");
    build_standard_markers(model_.cab_illuminance, cab_illuminance_marker_cache_, "");
    build_standard_markers(model_.fogs, fog_marker_cache_, "");
    build_standard_markers(model_.draw_distances, draw_distance_marker_cache_, "value");
    auto build_repeater_segment = [&](TrackSource source, double start, double end,
                                      double lateral, double forward) -> PlanRepeaterSegment {
        PlanRepeaterSegment segment;
        if (end < start) std::swap(start, end);
        if (!source.points || source.points->empty() || source.points->cols < 3) return segment;

        const size_t first = matrix_upper_bound_distance(*source.points, start);
        const size_t last = matrix_lower_bound_distance(*source.points, end);
        const size_t candidate_points = (last > first ? last - first : 0) + 2;
        const size_t points_per_following_chunk = k_repeater_segment_chunk_point_limit - 1;
        const size_t chunk_count = candidate_points <= k_repeater_segment_chunk_point_limit
            ? 1
            : 1 + (candidate_points - k_repeater_segment_chunk_point_limit +
                   points_per_following_chunk - 1) / points_per_following_chunk;
        segment.chunks.reserve(chunk_count);

        auto append_sample = [&](const std::optional<TrackPoint>& sampled) {
            if (!sampled) return;
            TrackPoint p = offset_track_point(*sampled, lateral, forward);
            append_repeater_segment_point(segment, p);
        };

        append_sample(sample_matrix_track_point(*source.points, start, source.has_theta_column));
        double previous_distance = std::numeric_limits<double>::quiet_NaN();
        size_t lower_bound_row = first;
        for (size_t row_index = first; row_index < last; ++row_index) {
            const double distance = source.points->at(row_index, 0);
            if (row_index == first || distance != previous_distance) lower_bound_row = row_index;
            append_sample(sample_matrix_track_point_at_lower_bound(
                *source.points, distance, source.has_theta_column, lower_bound_row));
            previous_distance = distance;
        }
        append_sample(sample_matrix_track_point(*source.points, end, source.has_theta_column));
        return segment;
    };

    struct RepeaterBeginState {
        size_t row_index = 0;
        double distance = 0.0;
        std::string track_key;
        std::string edit_id;
        std::optional<TrackSource> track_source;
        bool own_track_alias = false;
        double lateral = 0.0;
        double forward = 0.0;
    };
    auto make_repeater_marker = [](double distance, const TrackPoint& p, const std::string& label,
                                   const std::string& edit_id, size_t row_index) {
        PlanRepeaterMarker marker;
        marker.d = distance;
        marker.x = p.x;
        marker.y = p.y;
        marker.label = label;
        marker.edit_id = edit_id;
        marker.row_index = row_index;
        return marker;
    };
    auto finish_repeater = [&](const RepeaterBeginState& begin, double end_distance, const std::string& label) {
        if (begin.row_index >= repeater_marker_cache_.size()) return;
        RepeaterOverlayRow& overlay = repeater_marker_cache_[begin.row_index];
        if (begin.track_source) {
            TrackSource segment_source = *begin.track_source;
            auto source_begin = sample_matrix_track_point(
                *segment_source.points, begin.distance, segment_source.has_theta_column);
            auto source_end = sample_matrix_track_point(
                *segment_source.points, end_distance, segment_source.has_theta_column);
            if (begin.own_track_alias && !source_begin && !source_end) {
                segment_source = own_source;
            }
            overlay.segment = build_repeater_segment(segment_source, begin.distance, end_distance,
                                                     begin.lateral, begin.forward);
            auto base = std::move(source_end);
            if (!base && begin.own_track_alias) {
                base = sample_matrix_track_point(
                    *own_source.points, end_distance, own_source.has_theta_column);
            }
            if (base) {
                TrackPoint point = offset_track_point(*base, begin.lateral, begin.forward);
                overlay.end_marker = make_repeater_marker(
                    end_distance, point, label, begin.edit_id, begin.row_index);
            }
        }
    };

    std::vector<repeater_linkage::Event> repeater_events;
    repeater_events.reserve(model_.repeaters.size());
    for (size_t index = 0; index < model_.repeaters.size(); ++index) {
        const TableRow& row = model_.repeaters[index];
        repeater_linkage::Event event;
        event.source_index = index;
        event.distance = table_cell_number(row, "distance");
        event.order = table_cell_number(row, "order");
        event.key = table_cell(row, "repeaterKey");
        const std::string& method = table_cell(row, "method");
        if (method == "Begin" || method == "Begin0") {
            event.kind = repeater_linkage::EventKind::Begin;
        } else if (method == "End") {
            event.kind = repeater_linkage::EventKind::End;
        }
        repeater_events.push_back(std::move(event));
    }

    repeater_marker_cache_.reserve(model_.repeaters.size());
    for (const repeater_linkage::Segment& segment :
         repeater_linkage::pair_segments(std::move(repeater_events))) {
        if (segment.begin_source_index >= model_.repeaters.size()) continue;
        const TableRow& row = model_.repeaters[segment.begin_source_index];
        const std::string key = table_cell(row, "repeaterKey");
        RepeaterBeginState begin;
        begin.row_index = repeater_marker_cache_.size();
        begin.distance = table_cell_number(row, "distance");
        begin.track_key = table_cell(row, "trackKey");
        begin.edit_id = row.edit_id;
        begin.lateral = table_cell_number(row, "x");
        begin.forward = table_cell_number(row, "z");
        const std::string normalized_track_key = normalize_track_lookup_key(begin.track_key);
        const bool own_placement_key = is_own_track_placement_key(normalized_track_key);
        begin.own_track_alias = own_placement_key ||
            is_own_track_lookup_alias(normalized_track_key);
        begin.track_source = own_placement_key
            ? std::optional<TrackSource>{own_source}
            : find_track_source(normalized_track_key);
        if (!begin.track_source) {
            begin.track_source = own_source;
            begin.own_track_alias = true;
        }

        RepeaterOverlayRow overlay;
        if (begin.track_source) {
            auto base = sample_matrix_track_point(
                *begin.track_source->points, begin.distance, begin.track_source->has_theta_column);
            if (!base && begin.own_track_alias) {
                base = sample_matrix_track_point(
                    *own_source.points, begin.distance, own_source.has_theta_column);
            }
            if (base) {
                TrackPoint p = offset_track_point(*base, begin.lateral, begin.forward);
                overlay.begin_marker = make_repeater_marker(
                    begin.distance, p, key, begin.edit_id, begin.row_index);
            }
        }
        repeater_marker_cache_.push_back(std::move(overlay));
        if (segment.boundary_kind != repeater_linkage::BoundaryKind::Open) {
            finish_repeater(begin, segment.end_distance, key);
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

double App::current_plan_origin_angle() const {
    if (!has_model_ || model_.own.empty()) return model_.origin_angle;
    size_t row = matrix_lower_bound_distance(model_.own, dmin_);
    if (row < model_.own.rows && model_.own.at(row, 0) <= dmax_) {
        return model_.own.at(row, 4);
    }
    return model_.origin_angle;
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
    out.origin_angle = current_plan_origin_angle();
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
        dense_repeater_overview_lod(visible_repeater_count, plan_view_.scale);
    if (!skip_dense_repeater_markers) {
        out.repeater_markers.reserve(
            std::min(repeater_marker_cache_.size(), repeater_row_visible_.size()) * 2);
    }
    auto append_markers = [&](const std::vector<std::optional<PlanMarker>>& cache,
                              std::vector<PlanMarker>& markers,
                              const std::vector<unsigned char>* row_visibility = nullptr) {
        markers.reserve(row_visibility ? std::min(cache.size(), row_visibility->size())
                                       : cache.size());
        for (size_t row_index = 0; row_index < cache.size(); ++row_index) {
            if (row_visibility &&
                (row_index >= row_visibility->size() || !(*row_visibility)[row_index])) {
                continue;
            }
            if (!cache[row_index]) continue;
            const PlanMarker& source = *cache[row_index];
            if (source.d < dmin_ || source.d > dmax_) continue;
            TrackPoint point;
            point.x = source.x;
            point.y = source.y;
            point = rotate_point(point);
            PlanMarker marker = source;
            marker.x = point.x;
            marker.y = point.y;
            marker.row_index = row_index;
            markers.push_back(std::move(marker));
            append_marker_bounds(point.x, point.y);
        }
    };

    append_markers(structure_marker_cache_, out.structure_markers, &structure_row_visible_);
    append_markers(signal_marker_cache_, out.signal_markers, &signal_row_visible_);
    if (show_beacon_markers_) append_markers(beacon_marker_cache_, out.beacon_markers);
    if (show_pretrain_markers_) append_markers(pretrain_marker_cache_, out.pretrain_markers);
    auto is_other_train_path_visible = [this](size_t definition_row_index) {
        return definition_row_index < other_train_path_visible_.size() &&
            other_train_path_visible_[definition_row_index] != 0;
    };
    if (!other_train_path_visible_.empty()) {
        out.other_train_paths.reserve(other_train_path_cache_.size());
        for (const OtherTrainPathOverlay& source : other_train_path_cache_) {
            if (!is_other_train_path_visible(source.definition_row_index)) continue;
            if (source.d_max < dmin_ || source.d_min > dmax_ || source.points.empty()) continue;
            OtherTrainPathOverlay path = source;
            path.points.clear();
            path.points.reserve(source.points.size());
            for (TrackPoint p : source.points) {
                p = rotate_point(p);
                path.points.push_back(p);
                append_marker_bounds(p.x, p.y);
            }
            out.other_train_paths.push_back(std::move(path));
        }

        out.other_train_stop_markers.reserve(other_train_stop_marker_cache_.size());
        for (size_t i = 0; i < other_train_stop_marker_cache_.size(); ++i) {
            if (!other_train_stop_marker_cache_[i]) continue;
            const PlanOtherTrainStopMarker& source = *other_train_stop_marker_cache_[i];
            if (!is_other_train_path_visible(source.definition_row_index)) continue;
            if (source.d < dmin_ || source.d > dmax_) continue;
            TrackPoint p;
            p.x = source.x;
            p.y = source.y;
            p.theta = source.theta;
            p = rotate_point(p);
            PlanOtherTrainStopMarker marker = source;
            marker.x = p.x;
            marker.y = p.y;
            marker.theta = p.theta;
            marker.row_index = i;
            out.other_train_stop_markers.push_back(std::move(marker));
            append_marker_bounds(p.x, p.y);
        }
    }

    if (show_irregularity_markers_) {
        append_markers(irregularity_marker_cache_, out.irregularity_markers);
    }
    if (show_map_sound_markers_) append_markers(map_sound_marker_cache_, out.map_sound_markers);
    if (show_map_sound_3d_markers_) {
        append_markers(map_sound_3d_marker_cache_, out.map_sound_3d_markers);
    }
    if (show_rolling_noise_markers_) {
        append_markers(rolling_noise_marker_cache_, out.rolling_noise_markers);
    }
    if (show_flange_noise_markers_) {
        append_markers(flange_noise_marker_cache_, out.flange_noise_markers);
    }
    if (show_joint_noise_markers_) {
        append_markers(joint_noise_marker_cache_, out.joint_noise_markers);
    }
    if (show_background_markers_) {
        append_markers(background_marker_cache_, out.background_markers);
    }
    if (show_adhesion_markers_) append_markers(adhesion_marker_cache_, out.adhesion_markers);
    if (show_cab_illuminance_markers_) {
        append_markers(cab_illuminance_marker_cache_, out.cab_illuminance_markers);
    }
    if (show_fog_markers_) append_markers(fog_marker_cache_, out.fog_markers);
    if (show_draw_distance_markers_) {
        append_markers(draw_distance_marker_cache_, out.draw_distance_markers);
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

const PlanData& App::current_plan_data() {
    std::uint32_t marker_visibility_mask = 0;
    auto include_visibility = [&](bool visible, unsigned bit) {
        if (visible) marker_visibility_mask |= std::uint32_t{1} << bit;
    };
    include_visibility(show_beacon_markers_, 0);
    include_visibility(show_pretrain_markers_, 1);
    include_visibility(show_irregularity_markers_, 2);
    include_visibility(show_map_sound_markers_, 3);
    include_visibility(show_map_sound_3d_markers_, 4);
    include_visibility(show_rolling_noise_markers_, 5);
    include_visibility(show_flange_noise_markers_, 6);
    include_visibility(show_joint_noise_markers_, 7);
    include_visibility(show_background_markers_, 8);
    include_visibility(show_adhesion_markers_, 9);
    include_visibility(show_cab_illuminance_markers_, 10);
    include_visibility(show_fog_markers_, 11);
    include_visibility(show_draw_distance_markers_, 12);

    const bool cache_matches = plan_data_cache_.valid &&
        plan_data_cache_.source_revision == plan_data_source_revision_ &&
        plan_data_cache_.has_model == has_model_ &&
        plan_data_cache_.distance_min == dmin_ &&
        plan_data_cache_.distance_max == dmax_ &&
        plan_data_cache_.mode == mode_ &&
        plan_data_cache_.fitted == plan_view_.fitted &&
        plan_data_cache_.scale == plan_view_.scale &&
        plan_data_cache_.show_curve_values == show_curve_values_ &&
        plan_data_cache_.marker_visibility_mask == marker_visibility_mask &&
        plan_data_cache_.structure_row_visible == structure_row_visible_ &&
        plan_data_cache_.repeater_row_visible == repeater_row_visible_ &&
        plan_data_cache_.signal_row_visible == signal_row_visible_ &&
        plan_data_cache_.other_train_path_visible == other_train_path_visible_;
    if (cache_matches) return plan_data_cache_.data;

    plan_data_cache_.data = build_plan_data(false);
    plan_data_cache_.source_revision = plan_data_source_revision_;
    plan_data_cache_.has_model = has_model_;
    plan_data_cache_.distance_min = dmin_;
    plan_data_cache_.distance_max = dmax_;
    plan_data_cache_.mode = mode_;
    plan_data_cache_.fitted = plan_view_.fitted;
    plan_data_cache_.scale = plan_view_.scale;
    plan_data_cache_.show_curve_values = show_curve_values_;
    plan_data_cache_.marker_visibility_mask = marker_visibility_mask;
    plan_data_cache_.structure_row_visible = structure_row_visible_;
    plan_data_cache_.repeater_row_visible = repeater_row_visible_;
    plan_data_cache_.signal_row_visible = signal_row_visible_;
    plan_data_cache_.other_train_path_visible = other_train_path_visible_;
    plan_data_cache_.valid = true;
#ifndef NDEBUG
    ++plan_data_cache_.rebuild_count;
#endif
    return plan_data_cache_.data;
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
    if (!has_model_ || model_.own.empty()) return;
    const double first = model_.own.at(0, 0);
    const double last = model_.own.at(model_.own.rows - 1, 0);
    const double clamped_distance = std::clamp(distance, first, last);
    auto point = sample_matrix_track_point(model_.own, clamped_distance, true);
    if (!point) return;

    ImVec2 rotated = rotate_xy(point->x, point->y, -current_plan_origin_angle());
    plan_view_.cx = rotated.x;
    plan_view_.cy = rotated.y;
    plan_view_.fitted = true;
    keep_plan_view_ = true;
}

std::optional<ImVec2> App::plan_point_from_model_xy(double x, double y) const {
    if (!has_model_ || model_.own.empty()) return std::nullopt;
    return rotate_xy(x, y, -current_plan_origin_angle());
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

void App::jump_to_distance(double distance) {
    center_plan_at_distance(distance);
    request_plot_focus(distance, true, true);
    if (scene_preview_canvas_) {
        scene_preview_canvas_->jump_scene_camera_to_distance(distance);
    }
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
    const PlanData& pd = current_plan_data();
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

constexpr size_t k_polyline_chunk_point_limit = 4096;
constexpr float k_polyline_min_pixel_step_sq = 0.64f;

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
        points_.reserve(k_polyline_chunk_point_limit);
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
        if (dx * dx + dy * dy >= k_polyline_min_pixel_step_sq) {
            push_raw(p);
            last_ = p;
            has_pending_ = false;
            if (points_.size() >= k_polyline_chunk_point_limit) flush(true);
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

static const TrackPoint* first_repeater_segment_point_in_range(const PlanRepeaterSegment& segment,
                                                               double dmin, double dmax) {
    if (!segment.bounds_valid || !distance_ranges_overlap(segment.d_min, segment.d_max, dmin, dmax)) {
        return nullptr;
    }
    if (segment.d_min >= dmin && segment.d_max <= dmax) {
        for (const PlanRepeaterSegment::Chunk& chunk : segment.chunks) {
            if (!chunk.points.empty()) return &chunk.points.front();
        }
        return nullptr;
    }
    for (const PlanRepeaterSegment::Chunk& chunk : segment.chunks) {
        if (!chunk.bounds_valid || !distance_ranges_overlap(chunk.d_min, chunk.d_max, dmin, dmax)) continue;
        auto it = std::lower_bound(chunk.points.begin(), chunk.points.end(), dmin,
                                   [](const TrackPoint& point, double distance) {
                                       return point.d < distance;
                                   });
        if (it != chunk.points.end() && it->d <= dmax) return &*it;
    }
    return nullptr;
}

static const TrackPoint* last_repeater_segment_point_in_range(const PlanRepeaterSegment& segment,
                                                              double dmin, double dmax) {
    if (!segment.bounds_valid || !distance_ranges_overlap(segment.d_min, segment.d_max, dmin, dmax)) {
        return nullptr;
    }
    if (segment.d_min >= dmin && segment.d_max <= dmax) {
        for (auto chunk_it = segment.chunks.rbegin(); chunk_it != segment.chunks.rend(); ++chunk_it) {
            if (!chunk_it->points.empty()) return &chunk_it->points.back();
        }
        return nullptr;
    }
    for (auto chunk_it = segment.chunks.rbegin(); chunk_it != segment.chunks.rend(); ++chunk_it) {
        const PlanRepeaterSegment::Chunk& chunk = *chunk_it;
        if (!chunk.bounds_valid || !distance_ranges_overlap(chunk.d_min, chunk.d_max, dmin, dmax)) continue;
        auto it = std::upper_bound(chunk.points.begin(), chunk.points.end(), dmax,
                                   [](double distance, const TrackPoint& point) {
                                       return distance < point.d;
                                   });
        if (it == chunk.points.begin()) continue;
        --it;
        if (it->d >= dmin) return &*it;
    }
    return nullptr;
}

static void write_overview_line_quad(ImDrawList* draw, ImVec2 a, ImVec2 b,
                                     ImU32 color, float half_thickness, const ImVec2& uv) {
    float dx = b.x - a.x;
    float dy = b.y - a.y;
    float len_sq = dx * dx + dy * dy;
    if (len_sq <= 1e-4f) return;
    float inv_len = 1.0f / std::sqrt(len_sq);
    dx *= inv_len * half_thickness;
    dy *= inv_len * half_thickness;
    ImVec2 normal(dy, -dx);

    draw->_VtxWritePtr[0].pos = ImVec2(a.x + normal.x, a.y + normal.y);
    draw->_VtxWritePtr[0].uv = uv;
    draw->_VtxWritePtr[0].col = color;
    draw->_VtxWritePtr[1].pos = ImVec2(b.x + normal.x, b.y + normal.y);
    draw->_VtxWritePtr[1].uv = uv;
    draw->_VtxWritePtr[1].col = color;
    draw->_VtxWritePtr[2].pos = ImVec2(b.x - normal.x, b.y - normal.y);
    draw->_VtxWritePtr[2].uv = uv;
    draw->_VtxWritePtr[2].col = color;
    draw->_VtxWritePtr[3].pos = ImVec2(a.x - normal.x, a.y - normal.y);
    draw->_VtxWritePtr[3].uv = uv;
    draw->_VtxWritePtr[3].col = color;
    draw->_VtxWritePtr += 4;

    draw->_IdxWritePtr[0] = static_cast<ImDrawIdx>(draw->_VtxCurrentIdx);
    draw->_IdxWritePtr[1] = static_cast<ImDrawIdx>(draw->_VtxCurrentIdx + 1);
    draw->_IdxWritePtr[2] = static_cast<ImDrawIdx>(draw->_VtxCurrentIdx + 2);
    draw->_IdxWritePtr[3] = static_cast<ImDrawIdx>(draw->_VtxCurrentIdx);
    draw->_IdxWritePtr[4] = static_cast<ImDrawIdx>(draw->_VtxCurrentIdx + 2);
    draw->_IdxWritePtr[5] = static_cast<ImDrawIdx>(draw->_VtxCurrentIdx + 3);
    draw->_IdxWritePtr += 6;
    draw->_VtxCurrentIdx += 4;
}

static bool screen_line_overlaps_canvas(ImVec2 a, ImVec2 b, ImVec2 origin, ImVec2 size, float margin) {
    float min_x = std::min(a.x, b.x);
    float max_x = std::max(a.x, b.x);
    float min_y = std::min(a.y, b.y);
    float max_y = std::max(a.y, b.y);
    return !(max_x < origin.x - margin || min_x > origin.x + size.x + margin ||
             max_y < origin.y - margin || min_y > origin.y + size.y + margin);
}

static void draw_repeater_segment_overview(ImDrawList* draw,
                                           const std::vector<RepeaterOverlayRow>& rows,
                                           const std::vector<unsigned char>& visible,
                                           double dmin, double dmax,
                                           const PlanScreenTransform& transform,
                                           ImVec2 origin, ImVec2 size,
                                           ImU32 color, float thickness) {
    const size_t row_count = std::min(rows.size(), visible.size());
    if (row_count == 0) return;

    constexpr float coarse_margin = 96.0f;
    const float half_thickness = std::max(0.5f, thickness * 0.5f);
    const ImVec2 uv = draw->_Data->TexUvWhitePixel;
    const int reserved_lines = static_cast<int>(std::min<size_t>(row_count, static_cast<size_t>(std::numeric_limits<int>::max() / 6)));
    if (reserved_lines <= 0) return;
    draw->PrimReserve(reserved_lines * 6, reserved_lines * 4);
    int emitted_lines = 0;
    for (size_t row = 0; row < row_count; ++row) {
        if (!visible[row]) continue;
        const PlanRepeaterSegment& segment = rows[row].segment;
        if (!segment.bounds_valid || !distance_ranges_overlap(segment.d_min, segment.d_max, dmin, dmax)) continue;
        const bool full_segment_visible = segment.endpoints_valid && segment.d_min >= dmin && segment.d_max <= dmax;
        const TrackPoint* first = full_segment_visible
            ? &segment.first_point
            : first_repeater_segment_point_in_range(segment, dmin, dmax);
        const TrackPoint* last = full_segment_visible
            ? &segment.last_point
            : last_repeater_segment_point_in_range(segment, dmin, dmax);
        if (!first || !last || first == last) continue;
        ImVec2 a = transform.model_to_screen(first->x, first->y);
        ImVec2 b = transform.model_to_screen(last->x, last->y);
        if (!finite_screen_point(a) || !finite_screen_point(b)) continue;
        float dx = b.x - a.x;
        float dy = b.y - a.y;
        if (dx * dx + dy * dy < 4.0f) continue;
        if (!screen_line_overlaps_canvas(a, b, origin, size, coarse_margin)) continue;
        write_overview_line_quad(draw, a, b, color, half_thickness, uv);
        ++emitted_lines;
    }
    int unused_lines = reserved_lines - emitted_lines;
    if (unused_lines > 0) draw->PrimUnreserve(unused_lines * 6, unused_lines * 4);
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
    if (dense_repeater_overview_lod(visible_row_count, transform.scale)) {
        draw_repeater_segment_overview(draw, rows, visible, dmin, dmax, transform,
                                       origin, size, color, thickness);
        return;
    }

    bool dense_overlay = dense_repeater_segment_lod(visible_row_count, transform.scale);
    double detail_pixel_step = 0.45;
    if (dense_repeater_overview_lod(visible_row_count, transform.scale)) {
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

static void draw_plan_pretrain_marker(ImDrawList* draw, ImVec2 p, const std::string& label, float scale = 1.0f) {
    const float half = 7.0f * scale;
    const ImU32 white = map_marker_theme_color_u32(MapMarkerVisualKind::PreTrain);
    const ImU32 shadow = IM_COL32(0, 0, 0, 220);
    draw_map_marker_icon(draw, MapMarkerVisualKind::PreTrain, p, half);
    if (!label.empty()) {
        ImVec2 label_pos(p.x + half + 5.0f * scale,
                         p.y - ImGui::GetTextLineHeight() * 0.5f);
        draw->AddText(ImVec2(label_pos.x + 1.0f, label_pos.y + 1.0f), shadow, label.c_str());
        draw->AddText(label_pos, white, label.c_str());
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

static void draw_plan_direction_arrow(ImDrawList* draw, ImVec2 center, ImVec2 direction,
                                      ImU32 fill, ImU32 outline, float scale = 1.0f) {
    float len = std::sqrt(direction.x * direction.x + direction.y * direction.y);
    if (len < 1e-3f) return;
    ImVec2 forward(direction.x / len, direction.y / len);
    ImVec2 normal(-forward.y, forward.x);
    const float tip_len = 14.0f * scale;
    const float tail_back = 8.0f * scale;
    const float half_width = 6.0f * scale;
    ImVec2 pts[3] = {
        ImVec2(center.x + forward.x * tip_len,
               center.y + forward.y * tip_len),
        ImVec2(center.x - forward.x * tail_back + normal.x * half_width,
               center.y - forward.y * tail_back + normal.y * half_width),
        ImVec2(center.x - forward.x * tail_back - normal.x * half_width,
               center.y - forward.y * tail_back - normal.y * half_width),
    };
    draw->AddTriangleFilled(pts[0], pts[1], pts[2], fill);
    draw->AddPolyline(pts, IM_ARRAYSIZE(pts), outline, ImDrawFlags_Closed, 2.0f * scale);
}

static void draw_plan_small_text(ImDrawList* draw, ImVec2 p, ImU32 color, const std::string& text) {
    if (text.empty()) return;
    draw->AddText(nullptr, ImGui::GetFontSize() * 0.78f, ImVec2(p.x + 8.0f, p.y - 9.0f), color, text.c_str());
}

static bool point_near_canvas(ImVec2 p, ImVec2 origin, ImVec2 size, float margin = 48.0f) {
    return p.x >= origin.x - margin && p.x <= origin.x + size.x + margin &&
           p.y >= origin.y - margin && p.y <= origin.y + size.y + margin;
}

void App::finish_pending_load_timing_after_plan_data_ready() {
    if (!load_state_.pending_started_at) return;
    finish_pending_load_timing(std::chrono::steady_clock::now());
}

void App::render_plan_canvas(ImVec2 size) {
    debug_plan_stage("start");
    const PlanData& data = current_plan_data();
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
    const touch_input::TouchFrame& touch = touch_input::current_frame();
    ImVec2 mouse = io.MousePos;
    if (touch.long_press) mouse = touch.long_press_pos;
    else if (touch.tap) mouse = touch.tap_pos;
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
    if (hovered && mode_ == Mode::Pan && pick_slot_ == 0 && touch.pinch && touch.active_count >= 2 &&
        point_near_canvas(touch.pinch_center, origin, avail, 0.0f)) {
        ImVec2 anchor_before = plan_view_.screen_to_world(touch.pinch_center, origin, avail);
        const double scale_delta = std::abs(static_cast<double>(touch.pinch_scale) - 1.0) < 0.004
            ? 1.0
            : static_cast<double>(touch.pinch_scale);
        plan_view_.scale = std::clamp(plan_view_.scale * scale_delta, 0.001, 10000.0);
        if (std::abs(touch.pinch_rotation_delta) >= 0.003f) {
            plan_view_.rotation += static_cast<double>(touch.pinch_rotation_delta);
        }
        ImVec2 anchor_after = plan_view_.screen_to_world(touch.pinch_center, origin, avail);
        plan_view_.cx += static_cast<double>(anchor_before.x - anchor_after.x);
        plan_view_.cy += static_cast<double>(anchor_before.y - anchor_after.y);
        plan_view_.pan_by_screen_delta(touch.pinch_center_delta);
        plan_view_.dragging = false;
        plan_view_.rotating = false;
    }

    struct MarkerHit {
        size_t row_index = 0;
        double dist_sq = 0.0;
    };
    size_t selected_repeater_count = static_cast<size_t>(std::count_if(
        repeater_row_visible_.begin(), repeater_row_visible_.end(),
        [](unsigned char visible) { return visible != 0; }));
    bool dense_repeater_marker_lod = dense_repeater_overview_lod(selected_repeater_count, plan_view_.scale);
    bool overview_marker_lod = dense_repeater_marker_lod;
    const float marker_size_scale = marker_size_scale_from_percent(marker_size_percent_);
    const float marker_canvas_margin = std::max(12.0f, 12.0f * marker_size_scale);
    const double marker_hover_radius = static_cast<double>(marker_canvas_margin);
    const double marker_hover_radius_sq = marker_hover_radius * marker_hover_radius;
    auto nearest_marker_hit = [&](const auto& markers,
                                  const PlanScreenTransform& hit_transform,
                                  bool suppressed = false) -> std::optional<MarkerHit> {
        if (!hovered || mode_ != Mode::Pan || picking_background_station || suppressed) {
            return std::nullopt;
        }
        double best = marker_hover_radius_sq;
        std::optional<MarkerHit> best_hit;
        for (const auto& marker : markers) {
            const ImVec2 point = hit_transform.plan_to_screen(marker.x, marker.y);
            if (!point_near_canvas(point, origin, avail, marker_canvas_margin)) continue;
            const double dx = static_cast<double>(point.x - mouse.x);
            const double dy = static_cast<double>(point.y - mouse.y);
            const double dist_sq = dx * dx + dy * dy;
            if (dist_sq <= best) {
                best = dist_sq;
                best_hit = MarkerHit{marker.row_index, dist_sq};
            }
        }
        return best_hit;
    };
    PlanScreenTransform hit_transform = make_plan_transform(plan_view_, -data.origin_angle, origin, avail);
    std::optional<MarkerHit> hovered_structure_hit =
        nearest_marker_hit(data.structure_markers, hit_transform);
    std::optional<MarkerHit> hovered_repeater_hit =
        nearest_marker_hit(data.repeater_markers, hit_transform, dense_repeater_marker_lod);
    std::optional<MarkerHit> hovered_signal_hit =
        nearest_marker_hit(data.signal_markers, hit_transform);
    std::optional<MarkerHit> hovered_beacon_hit =
        nearest_marker_hit(data.beacon_markers, hit_transform);
    std::optional<MarkerHit> hovered_pretrain_hit =
        nearest_marker_hit(data.pretrain_markers, hit_transform);
    std::optional<MarkerHit> hovered_other_train_stop_hit =
        nearest_marker_hit(data.other_train_stop_markers, hit_transform);
    std::optional<MarkerHit> hovered_irregularity_hit =
        nearest_marker_hit(data.irregularity_markers, hit_transform);
    std::optional<MarkerHit> hovered_rolling_noise_hit =
        nearest_marker_hit(data.rolling_noise_markers, hit_transform);
    std::optional<MarkerHit> hovered_map_sound_hit =
        nearest_marker_hit(data.map_sound_markers, hit_transform);
    std::optional<MarkerHit> hovered_map_sound_3d_hit =
        nearest_marker_hit(data.map_sound_3d_markers, hit_transform);
    std::optional<MarkerHit> hovered_flange_noise_hit =
        nearest_marker_hit(data.flange_noise_markers, hit_transform);
    std::optional<MarkerHit> hovered_joint_noise_hit =
        nearest_marker_hit(data.joint_noise_markers, hit_transform);
    std::optional<MarkerHit> hovered_background_hit =
        nearest_marker_hit(data.background_markers, hit_transform);
    std::optional<MarkerHit> hovered_adhesion_hit =
        nearest_marker_hit(data.adhesion_markers, hit_transform);
    std::optional<MarkerHit> hovered_cab_illuminance_hit =
        nearest_marker_hit(data.cab_illuminance_markers, hit_transform);
    std::optional<MarkerHit> hovered_fog_hit =
        nearest_marker_hit(data.fog_markers, hit_transform);
    std::optional<MarkerHit> hovered_draw_distance_hit =
        nearest_marker_hit(data.draw_distance_markers, hit_transform);
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
    std::optional<size_t> hovered_other_train_stop_row = hovered_other_train_stop_hit
        ? std::optional<size_t>(hovered_other_train_stop_hit->row_index)
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
    std::optional<size_t> hovered_draw_distance_row = hovered_draw_distance_hit
        ? std::optional<size_t>(hovered_draw_distance_hit->row_index)
        : std::nullopt;

    auto closer_or_equal = [](const std::optional<MarkerHit>& hit, const std::optional<MarkerHit>& other) {
        return hit && (!other || hit->dist_sq <= other->dist_sq);
    };
    auto closer_than_all_marker_context_hits = [&](const std::optional<MarkerHit>& hit) {
        if (!hit) return false;
        auto closer_than = [&](const std::optional<MarkerHit>& other) {
            return !other || hit->dist_sq < other->dist_sq;
        };
        return closer_than(hovered_signal_hit) &&
               closer_than(hovered_beacon_hit) &&
               closer_than(hovered_adhesion_hit) &&
               closer_than(hovered_irregularity_hit) &&
               closer_than(hovered_background_hit) &&
               closer_than(hovered_repeater_hit) &&
               closer_than(hovered_structure_hit) &&
               closer_than(hovered_cab_illuminance_hit) &&
               closer_than(hovered_rolling_noise_hit) &&
               closer_than(hovered_map_sound_hit) &&
               closer_than(hovered_map_sound_3d_hit) &&
               closer_than(hovered_flange_noise_hit) &&
               closer_than(hovered_joint_noise_hit) &&
               closer_than(hovered_fog_hit);
    };
    auto select_nearest_touch_marker = [&]() {
        std::optional<PlanMarkerSelection> best;
        double best_dist_sq = std::numeric_limits<double>::max();
        auto note = [&](const std::optional<MarkerHit>& hit, PlanMarkerKind kind) {
            if (!hit || hit->dist_sq > best_dist_sq) return;
            best_dist_sq = hit->dist_sq;
            best = PlanMarkerSelection{kind, hit->row_index};
        };
        note(hovered_signal_hit, PlanMarkerKind::Signal);
        note(hovered_beacon_hit, PlanMarkerKind::Beacon);
        note(hovered_pretrain_hit, PlanMarkerKind::PreTrain);
        note(hovered_other_train_stop_hit, PlanMarkerKind::OtherTrainStop);
        note(hovered_adhesion_hit, PlanMarkerKind::Adhesion);
        note(hovered_irregularity_hit, PlanMarkerKind::Irregularity);
        note(hovered_background_hit, PlanMarkerKind::Background);
        note(hovered_repeater_hit, PlanMarkerKind::Repeater);
        note(hovered_structure_hit, PlanMarkerKind::Structure);
        note(hovered_cab_illuminance_hit, PlanMarkerKind::CabIlluminance);
        note(hovered_rolling_noise_hit, PlanMarkerKind::RollingNoise);
        note(hovered_map_sound_hit, PlanMarkerKind::MapSound);
        note(hovered_map_sound_3d_hit, PlanMarkerKind::MapSound3D);
        note(hovered_flange_noise_hit, PlanMarkerKind::FlangeNoise);
        note(hovered_joint_noise_hit, PlanMarkerKind::JointNoise);
        note(hovered_fog_hit, PlanMarkerKind::Fog);
        note(hovered_draw_distance_hit, PlanMarkerKind::DrawDistance);
        if (best) plan_marker_selection_ = *best;
    };
    ImVec2 touch_tap_pos;
    if (touch_input::consume_tap_in_rect(origin, ImVec2(origin.x + avail.x, origin.y + avail.y), &touch_tap_pos)) {
        (void)touch_tap_pos;
        select_nearest_touch_marker();
    }

    ImVec2 touch_long_press_pos;
    bool touch_marker_context_requested =
        touch_input::consume_long_press_in_rect(origin, ImVec2(origin.x + avail.x, origin.y + avail.y),
                                                &touch_long_press_pos);
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Right) || touch_marker_context_requested) {
        if (touch_marker_context_requested) plan_marker_selection_.clear();
        if (hovered_draw_distance_hit &&
            closer_than_all_marker_context_hits(hovered_draw_distance_hit) &&
            closer_or_equal(hovered_draw_distance_hit, hovered_other_train_stop_hit)) {
            plan_draw_distance_popup_row_ =
                static_cast<int>(hovered_draw_distance_hit->row_index);
            ImGui::OpenPopup("plan_draw_distance_marker_context");
        } else if (hovered_other_train_stop_hit &&
            closer_than_all_marker_context_hits(hovered_other_train_stop_hit)) {
            plan_other_train_stop_popup_row_ = static_cast<int>(hovered_other_train_stop_hit->row_index);
            ImGui::OpenPopup("plan_other_train_stop_marker_context");
        } else if (hovered_signal_hit &&
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

    const bool touch_multi_input = touch.active_count >= 2 || touch.pinch;
    bool rotate_plan = hovered && !touch_multi_input && io.KeyCtrl && ImGui::IsMouseDown(ImGuiMouseButton_Left);
    if (hovered && mode_ == Mode::Pan && !touch_multi_input &&
        ImGui::IsMouseDown(ImGuiMouseButton_Left) && !rotate_plan) {
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
    auto marker_emphasized = [&](PlanMarkerKind kind, size_t row_index, bool hovered_marker) {
        return hovered_marker || plan_marker_selection_.matches(kind, row_index);
    };
    auto draw_selected_marker_ring = [&](ImVec2 p, PlanMarkerKind kind, size_t row_index, ImU32 color) {
        if (!plan_marker_selection_.matches(kind, row_index)) return;
        const float radius = std::max(12.0f, 14.0f * marker_size_scale);
        draw->AddCircle(p, radius, color, 24, std::max(2.0f, 2.0f * marker_size_scale));
    };
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

    {
        const ImU32 other_train_line_color = IM_COL32(255, 64, 64, 235);
        const ImU32 other_train_stop_color = IM_COL32(255, 51, 51, 255);
        const ImU32 other_train_outline_color = IM_COL32(96, 0, 0, 255);
        const float path_width = std::max(2.0f, line_widths.other_track_px + 0.75f);
        for (const OtherTrainPathOverlay& path : data.other_train_paths) {
            draw_polyline(draw, path.points, transform, origin, avail, other_train_line_color, path_width);
            if (path.points.size() >= 2) {
                size_t segment_index = std::min(path.points.size() / 2, path.points.size() - 2);
                ImVec2 a = transform.plan_to_screen(path.points[segment_index].x, path.points[segment_index].y);
                ImVec2 b = transform.plan_to_screen(path.points[segment_index + 1].x, path.points[segment_index + 1].y);
                ImVec2 center((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f);
                ImVec2 direction(b.x - a.x, b.y - a.y);
                if (path.reverse_direction) {
                    direction.x = -direction.x;
                    direction.y = -direction.y;
                }
                if (point_near_canvas(center, origin, avail)) {
                    draw_plan_direction_arrow(draw, center, direction, other_train_stop_color,
                                              other_train_outline_color, marker_size_scale);
                }
            }
        }
        for (const PlanOtherTrainStopMarker& marker : data.other_train_stop_markers) {
            ImVec2 p = transform.plan_to_screen(marker.x, marker.y);
            if (!point_near_canvas(p, origin, avail)) continue;
            bool marker_hovered = hovered_other_train_stop_row && marker.row_index == *hovered_other_train_stop_row;
            bool marker_active = marker_emphasized(PlanMarkerKind::OtherTrainStop, marker.row_index, marker_hovered);
            draw_selected_marker_ring(p, PlanMarkerKind::OtherTrainStop, marker.row_index, other_train_stop_color);
            float radius = 4.4f * marker_size_scale * (marker_active ? 1.25f : 1.0f);
            draw->AddCircleFilled(p, radius, other_train_stop_color, 16);
            draw->AddCircle(p, radius + 1.5f * marker_size_scale, other_train_outline_color, 16, 1.4f * marker_size_scale);
            ImVec2 direction(std::cos(marker.theta), std::sin(marker.theta));
            if (marker.reverse_direction) {
                direction.x = -direction.x;
                direction.y = -direction.y;
            }
            ImVec2 q = transform.plan_to_screen(marker.x + direction.x, marker.y + direction.y);
            if (marker_active) {
                draw_plan_direction_arrow(draw, p, ImVec2(q.x - p.x, q.y - p.y),
                                          other_train_stop_color, other_train_outline_color,
                                          marker_size_scale * 0.9f);
                draw_plan_small_text(draw, p, other_train_stop_color, marker.label);
            }
        }
    }
    debug_plan_stage("other_train_paths");

    if (show_stations_) {
        const float station_marker_radius = k_default_station_marker_size * marker_size_scale;
        const float station_label_offset = std::max(8.0f, station_marker_radius + 4.0f);
        for (const auto& st : data.stations) {
            ImVec2 p = transform.plan_to_screen(st.x, st.y);
            draw_map_marker_icon(
                draw, MapMarkerVisualKind::Station, p,
                station_marker_radius / 0.46f);
            if (!overview_marker_lod && show_station_names_) {
                draw->AddText(
                    ImVec2(p.x + station_label_offset, p.y - 16),
                    map_marker_theme_color_u32(MapMarkerVisualKind::Station),
                    st.station.name.c_str());
            }
            if (!overview_marker_lod && show_station_mileage_) {
                draw->AddText(ImVec2(p.x + station_label_offset, p.y + 4), IM_COL32(255, 216, 77, 255), (format_double(st.station.mileage, 0) + "m").c_str());
            }
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
            const float rotation = std::atan2(d.y / len, d.x / len);
            draw_map_marker_icon(
                draw, MapMarkerVisualKind::SpeedLimit, p,
                8.0f * marker_size_scale / 0.88f, rotation);
            if (!overview_marker_lod) {
                std::string label = sp.has_speed ? format_double(sp.speed, 0) : "x";
                draw->AddText(
                    ImVec2(p.x + std::max(10.0f, 10.0f * marker_size_scale), p.y - 15),
                    map_marker_theme_color_u32(MapMarkerVisualKind::SpeedLimit),
                    label.c_str());
            }
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
            bool marker_active = marker_emphasized(PlanMarkerKind::Signal, marker.row_index, marker_hovered);
            if (overview_marker_lod && !marker_active) {
                float half = std::max(2.0f, 2.8f * marker_size_scale);
                draw->AddRectFilled(ImVec2(p.x - half, p.y - half), ImVec2(p.x + half, p.y + half), signal_color);
                continue;
            }
            draw_selected_marker_ring(p, PlanMarkerKind::Signal, marker.row_index, signal_color);
            draw_plan_signal_marker(draw, p, signal_color, marker_size_scale * (marker_active ? 1.28f : 1.0f));
            if (marker_active) draw_plan_small_text(draw, p, signal_color, marker.label);
        }
    }
    debug_plan_stage("signal_markers");

    auto draw_colored_marker_set = [&](const std::vector<PlanMarker>& markers,
                                       PlanMarkerKind kind,
                                       const std::optional<size_t>& hovered_row,
                                       MapMarkerVisualKind visual_kind,
                                       float icon_half_extent) {
        const ImU32 color = map_marker_theme_color_u32(visual_kind);
        for (const PlanMarker& marker : markers) {
            const ImVec2 point = transform.plan_to_screen(marker.x, marker.y);
            if (!point_near_canvas(point, origin, avail)) continue;
            const double dx = static_cast<double>(point.x - mouse.x);
            const double dy = static_cast<double>(point.y - mouse.y);
            const bool marker_hovered = hovered_row && *hovered_row == marker.row_index &&
                dx * dx + dy * dy <= marker_hover_radius_sq;
            const bool marker_active = marker_emphasized(kind, marker.row_index, marker_hovered);
            draw_selected_marker_ring(point, kind, marker.row_index, color);
            draw_map_marker_icon(
                draw, visual_kind, point,
                icon_half_extent * marker_size_scale *
                    (marker_active ? 1.28f : 1.0f));
            if (marker_active) draw_plan_small_text(draw, point, color, marker.label);
        }
    };

    draw_colored_marker_set(data.beacon_markers, PlanMarkerKind::Beacon,
                            hovered_beacon_row, MapMarkerVisualKind::Beacon, 7.0f);
    debug_plan_stage("beacon_markers");

    if (!data.pretrain_markers.empty()) {
        for (const PlanMarker& marker : data.pretrain_markers) {
            const ImVec2 point = transform.plan_to_screen(marker.x, marker.y);
            if (!point_near_canvas(point, origin, avail)) continue;
            const double dx = static_cast<double>(point.x - mouse.x);
            const double dy = static_cast<double>(point.y - mouse.y);
            const bool marker_hovered = hovered_pretrain_row &&
                *hovered_pretrain_row == marker.row_index &&
                dx * dx + dy * dy <= marker_hover_radius_sq;
            const bool marker_active = marker_emphasized(
                PlanMarkerKind::PreTrain, marker.row_index, marker_hovered);
            const ImU32 color =
                map_marker_theme_color_u32(MapMarkerVisualKind::PreTrain);
            draw_selected_marker_ring(point, PlanMarkerKind::PreTrain, marker.row_index, color);
            draw_plan_pretrain_marker(
                draw, point, marker.label, marker_size_scale * (marker_active ? 1.22f : 1.0f));
        }
    }
    debug_plan_stage("pretrain_markers");

    draw_colored_marker_set(data.irregularity_markers, PlanMarkerKind::Irregularity,
                            hovered_irregularity_row,
                            MapMarkerVisualKind::Irregularity, 7.0f);
    debug_plan_stage("irregularity_markers");

    draw_colored_marker_set(data.map_sound_markers, PlanMarkerKind::MapSound,
                            hovered_map_sound_row,
                            MapMarkerVisualKind::MapSound, 8.0f);
    debug_plan_stage("map_sound_markers");

    draw_colored_marker_set(data.map_sound_3d_markers, PlanMarkerKind::MapSound3D,
                            hovered_map_sound_3d_row,
                            MapMarkerVisualKind::MapSound3D, 8.0f);
    debug_plan_stage("map_sound_3d_markers");

    draw_colored_marker_set(data.rolling_noise_markers, PlanMarkerKind::RollingNoise,
                            hovered_rolling_noise_row,
                            MapMarkerVisualKind::RollingNoise, 7.2f);
    debug_plan_stage("rolling_noise_markers");

    draw_colored_marker_set(data.flange_noise_markers, PlanMarkerKind::FlangeNoise,
                            hovered_flange_noise_row,
                            MapMarkerVisualKind::FlangeNoise, 7.0f);
    debug_plan_stage("flange_noise_markers");

    draw_colored_marker_set(data.joint_noise_markers, PlanMarkerKind::JointNoise,
                            hovered_joint_noise_row,
                            MapMarkerVisualKind::JointNoise, 7.0f);
    debug_plan_stage("joint_noise_markers");

    draw_colored_marker_set(data.background_markers, PlanMarkerKind::Background,
                            hovered_background_row,
                            MapMarkerVisualKind::Background, 6.0f);
    debug_plan_stage("background_change_markers");

    draw_colored_marker_set(data.adhesion_markers, PlanMarkerKind::Adhesion,
                            hovered_adhesion_row,
                            MapMarkerVisualKind::Adhesion, 7.2f);
    debug_plan_stage("adhesion_markers");

    draw_colored_marker_set(data.cab_illuminance_markers, PlanMarkerKind::CabIlluminance,
                            hovered_cab_illuminance_row,
                            MapMarkerVisualKind::CabIlluminance, 8.0f);
    debug_plan_stage("cab_illuminance_markers");

    if (!data.fog_markers.empty()) {
        for (const PlanMarker& marker : data.fog_markers) {
            const ImVec2 point = transform.plan_to_screen(marker.x, marker.y);
            if (!point_near_canvas(point, origin, avail)) continue;
            const double dx = static_cast<double>(point.x - mouse.x);
            const double dy = static_cast<double>(point.y - mouse.y);
            const bool marker_hovered = hovered_fog_row && *hovered_fog_row == marker.row_index &&
                dx * dx + dy * dy <= marker_hover_radius_sq;
            const bool marker_active = marker_emphasized(
                PlanMarkerKind::Fog, marker.row_index, marker_hovered);
            const ImU32 color =
                map_marker_theme_color_u32(MapMarkerVisualKind::Fog);
            draw_selected_marker_ring(point, PlanMarkerKind::Fog, marker.row_index, color);
            draw_map_marker_icon(
                draw, MapMarkerVisualKind::Fog, point,
                7.0f * marker_size_scale *
                    (marker_active ? 1.28f : 1.0f));
            if (marker_active) draw_plan_small_text(draw, point, color, marker.label);
        }
    }
    debug_plan_stage("fog_markers");

    draw_colored_marker_set(data.draw_distance_markers, PlanMarkerKind::DrawDistance,
                            hovered_draw_distance_row,
                            MapMarkerVisualKind::DrawDistance, 7.0f);
    debug_plan_stage("draw_distance_markers");

    if (!repeater_marker_cache_.empty() || !data.repeater_markers.empty()) {
        ImU32 repeater_color = IM_COL32(255, 105, 190, 255);
        draw_repeater_segment_chunks(draw, repeater_marker_cache_, repeater_row_visible_,
                                     dmin_, dmax_, transform, origin, avail, repeater_color, 1.25f * marker_size_scale);
        debug_plan_stage("repeater_segments");
        if (!dense_repeater_marker_lod) {
            bool draw_repeater_labels = plan_view_.scale >= k_dense_repeater_overview_scale ||
                data.repeater_markers.size() <= 600;
            for (const auto& marker : data.repeater_markers) {
                ImVec2 p = transform.plan_to_screen(marker.x, marker.y);
                if (!point_near_canvas(p, origin, avail)) continue;
                double dx = static_cast<double>(p.x - mouse.x);
                double dy = static_cast<double>(p.y - mouse.y);
                bool marker_hovered = hovered_repeater_row && *hovered_repeater_row == marker.row_index &&
                    dx * dx + dy * dy <= marker_hover_radius_sq;
                bool marker_active = marker_emphasized(PlanMarkerKind::Repeater, marker.row_index, marker_hovered);
                draw_selected_marker_ring(p, PlanMarkerKind::Repeater, marker.row_index, repeater_color);
                float marker_scale = marker_size_scale * (marker_active ? 1.28f : 1.0f);
                draw_plan_diamond_marker(draw, p, repeater_color, marker_scale);
                if (draw_repeater_labels || marker_active) draw_plan_small_text(draw, p, repeater_color, marker.label);
            }
        }
        debug_plan_stage("repeater_markers");
    }

    if (!data.structure_markers.empty()) {
        ImU32 structure_color = IM_COL32(255, 216, 48, 255);
        for (const auto& marker : data.structure_markers) {
            ImVec2 p = transform.plan_to_screen(marker.x, marker.y);
            if (!point_near_canvas(p, origin, avail)) continue;
            bool marker_hovered = hovered_structure_row && *hovered_structure_row == marker.row_index;
            bool marker_active = marker_emphasized(PlanMarkerKind::Structure, marker.row_index, marker_hovered);
            draw_selected_marker_ring(p, PlanMarkerKind::Structure, marker.row_index, structure_color);
            float marker_scale = marker_size_scale *
                (marker_active ? 1.28f : 1.0f);
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
    draw->PopClipRect();
    debug_plan_stage("overlays_done");
    if (ImGui::BeginPopup("plan_structure_marker_context")) {
        bool can_locate = plan_structure_popup_row_ >= 0 &&
            static_cast<size_t>(plan_structure_popup_row_) < structure_marker_cache_.size();
        const bool put_between = can_locate &&
            static_cast<size_t>(plan_structure_popup_row_) >= model_.structures.size();
        const char* locate_label_key =
            put_between ? "menu.locate_in_structure_put_between_list" : "menu.locate_in_structure_list";
        ImGui::BeginDisabled(!can_locate);
        if (ImGui::MenuItem(tr(locate_label_key).c_str()) && can_locate) {
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
        const PlanRepeaterMarker* marker = nullptr;
        if (can_locate) {
            marker = repeater_marker_cache_[static_cast<size_t>(plan_repeater_popup_row_)].begin_marker
                ? &*repeater_marker_cache_[static_cast<size_t>(plan_repeater_popup_row_)].begin_marker
                : nullptr;
        }
        ImGui::BeginDisabled(!edit_actions_available() || !marker || marker->edit_id.empty());
        if (ImGui::MenuItem(tr("dialog.element_properties").c_str()) && marker) {
            request_element_inspector(marker->edit_id, "repeater");
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
    if (ImGui::BeginPopup("plan_other_train_stop_marker_context")) {
        bool can_locate = plan_other_train_stop_popup_row_ >= 0 &&
            static_cast<size_t>(plan_other_train_stop_popup_row_) < other_train_stop_marker_cache_.size() &&
            other_train_stop_marker_cache_[static_cast<size_t>(plan_other_train_stop_popup_row_)].has_value();
        ImGui::BeginDisabled(!can_locate);
        if (ImGui::MenuItem(tr("menu.locate_in_other_train_stop_list").c_str()) && can_locate) {
            locate_other_train_stop_row_in_list(static_cast<size_t>(plan_other_train_stop_popup_row_));
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
        const PlanIrregularityMarker* marker = nullptr;
        if (can_locate && irregularity_marker_cache_[static_cast<size_t>(plan_irregularity_popup_row_)].has_value()) {
            marker = &*irregularity_marker_cache_[static_cast<size_t>(plan_irregularity_popup_row_)];
        }
        ImGui::BeginDisabled(!edit_actions_available() || !marker || marker->edit_id.empty());
        if (ImGui::MenuItem(tr("dialog.element_properties").c_str()) && marker) {
            request_element_inspector(marker->edit_id, "irregularity.change");
        }
        ImGui::EndDisabled();
        ImGui::BeginDisabled(!edit_actions_available() || !marker || marker->edit_id.empty());
        if (ImGui::MenuItem(tr("button.delete").c_str()) && marker) {
            request_element_delete(marker->edit_id, "irregularity.change");
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
    if (ImGui::BeginPopup("plan_draw_distance_marker_context")) {
        bool can_locate = plan_draw_distance_popup_row_ >= 0 &&
            static_cast<size_t>(plan_draw_distance_popup_row_) <
                draw_distance_marker_cache_.size();
        ImGui::BeginDisabled(!can_locate);
        if (ImGui::MenuItem(tr("menu.locate_in_draw_distance_list").c_str()) &&
            can_locate) {
            locate_draw_distance_row_in_list(
                static_cast<size_t>(plan_draw_distance_popup_row_));
        }
        ImGui::EndDisabled();
        ImGui::EndPopup();
    }
    ImGui::EndChild();
    debug_plan_stage("end");
}
