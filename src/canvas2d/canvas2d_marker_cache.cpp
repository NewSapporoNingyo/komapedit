/*
 * Copyright (c) 2026 Sapporo_ningyo
 *
 * Licensed under Apache License 2.0; see LICENSE and NOTICE.
 * The GUI uses Dear ImGui and ImPlot; see THIRD_PARTY_NOTICES.md.
 */

#ifdef _MSC_VER
#pragma execution_character_set("utf-8")
#endif

#include "canvas2d_marker_cache.h"

#include "repeater_linkage.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace canvas2d {
namespace {

TrackPoint matrix_row_track_point(
    const Matrix& points, size_t row, bool has_theta_column) {
    TrackPoint point;
    point.d = points.at(row, 0);
    point.x = points.at(row, 1);
    point.y = points.at(row, 2);
    point.z = points.cols > 3 ? points.at(row, 3) : 0.0;
    point.theta = has_theta_column && points.cols > 4
        ? points.at(row, 4)
        : track_buffer_tangent(points, row);
    if (points.cols > 5) point.radius = points.at(row, 5);
    if (points.cols > 6) point.gradient = points.at(row, 6);
    return point;
}

std::optional<TrackPoint> sample_matrix_track_point_at_lower_bound(
    const Matrix& points, double distance, bool has_theta_column,
    size_t lower_bound_row) {
    if (points.empty() || points.cols < 3) return std::nullopt;
    const double first = points.at(0, 0);
    const double last = points.at(points.rows - 1, 0);
    constexpr double eps = 1e-6;
    if (distance < first - eps || distance > last + eps) return std::nullopt;
    if (distance <= first) return matrix_row_track_point(points, 0, has_theta_column);
    if (distance >= last) {
        return matrix_row_track_point(points, points.rows - 1, has_theta_column);
    }

    const size_t b_row = std::min(lower_bound_row, points.rows - 1);
    if (b_row == 0) return matrix_row_track_point(points, 0, has_theta_column);
    const size_t a_row = b_row - 1;
    const TrackPoint a = matrix_row_track_point(points, a_row, has_theta_column);
    const TrackPoint b = matrix_row_track_point(points, b_row, has_theta_column);
    const double span = b.d - a.d;
    const double t = std::abs(span) < eps
        ? 0.0
        : std::clamp((distance - a.d) / span, 0.0, 1.0);

    TrackPoint point;
    point.d = distance;
    point.x = a.x + (b.x - a.x) * t;
    point.y = a.y + (b.y - a.y) * t;
    point.z = a.z + (b.z - a.z) * t;
    point.theta = has_theta_column
        ? angle_lerp(a.theta, b.theta, t)
        : std::atan2(b.y - a.y, b.x - a.x);
    point.radius = a.radius + (b.radius - a.radius) * t;
    point.gradient = a.gradient + (b.gradient - a.gradient) * t;
    return point;
}

struct TrackSource {
    const Matrix* points = nullptr;
    bool has_theta_column = false;
    size_t track_index = std::numeric_limits<size_t>::max();
};

TrackPoint offset_track_point(TrackPoint base, double lateral, double forward) {
    const double c = std::cos(base.theta);
    const double s = std::sin(base.theta);
    base.x += c * forward - s * lateral;
    base.y += s * forward + c * lateral;
    return base;
}

constexpr size_t k_repeater_segment_chunk_point_limit = 192;

template <typename Bounds>
void include_repeater_bounds(Bounds& bounds, const TrackPoint& point) {
    if (!bounds.bounds_valid) {
        bounds.d_min = bounds.d_max = point.d;
        bounds.x_min = bounds.x_max = point.x;
        bounds.y_min = bounds.y_max = point.y;
        bounds.bounds_valid = true;
        return;
    }
    bounds.d_min = std::min(bounds.d_min, point.d);
    bounds.d_max = std::max(bounds.d_max, point.d);
    bounds.x_min = std::min(bounds.x_min, point.x);
    bounds.x_max = std::max(bounds.x_max, point.x);
    bounds.y_min = std::min(bounds.y_min, point.y);
    bounds.y_max = std::max(bounds.y_max, point.y);
}

void include_repeater_segment_endpoint(
    PlanRepeaterSegment& segment, const TrackPoint& point) {
    if (!segment.endpoints_valid) {
        segment.first_point = point;
        segment.last_point = point;
        segment.endpoints_valid = true;
        return;
    }
    if (std::abs(segment.first_point.d - point.d) < 1e-6 &&
        std::abs(segment.last_point.d - point.d) < 1e-6) {
        segment.first_point = point;
    }
    segment.last_point = point;
}

void append_repeater_segment_point(
    PlanRepeaterSegment& segment, const TrackPoint& point) {
    if (!segment.chunks.empty()) {
        PlanRepeaterSegment::Chunk& tail_chunk = segment.chunks.back();
        if (!tail_chunk.points.empty() &&
            std::abs(tail_chunk.points.back().d - point.d) < 1e-6) {
            tail_chunk.points.back() = point;
            include_repeater_bounds(tail_chunk, point);
            include_repeater_bounds(segment, point);
            include_repeater_segment_endpoint(segment, point);
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
            include_repeater_bounds(segment.chunks.back(), *tail);
        }
    }

    PlanRepeaterSegment::Chunk& chunk = segment.chunks.back();
    chunk.points.push_back(point);
    include_repeater_bounds(chunk, point);
    include_repeater_bounds(segment, point);
    include_repeater_segment_endpoint(segment, point);
}

void populate_speed_limit_marker_cache(
    const MapModel& model,
    std::vector<std::optional<PlanMarker>>& cache) {
    cache.assign(model.speed_limit_rows.size(), std::nullopt);
    if (model.own.empty()) return;
    for (size_t row_index = 0; row_index < model.speed_limit_rows.size();
         ++row_index) {
        const TableRow& row = model.speed_limit_rows[row_index];
        const double distance = table_cell_number(row, "distance");
        const std::optional<TrackPoint> point =
            sample_matrix_track_point(model.own, distance, true);
        if (!point) continue;
        PlanMarker marker;
        marker.d = distance;
        marker.x = point->x;
        marker.y = point->y;
        marker.label = table_cell(row, "speed");
        marker.edit_id = row.edit_id;
        marker.row_index = row_index;
        cache[row_index] = std::move(marker);
    }
}

} // namespace

std::optional<TrackPoint> sample_matrix_track_point(
    const Matrix& points, double distance, bool has_theta_column) {
    if (points.empty() || points.cols < 3) return std::nullopt;
    const double first = points.at(0, 0);
    const double last = points.at(points.rows - 1, 0);
    constexpr double eps = 1e-6;
    if (distance < first - eps || distance > last + eps) return std::nullopt;
    if (distance <= first) return matrix_row_track_point(points, 0, has_theta_column);
    if (distance >= last) {
        return matrix_row_track_point(points, points.rows - 1, has_theta_column);
    }

    return sample_matrix_track_point_at_lower_bound(
        points, distance, has_theta_column,
        matrix_lower_bound_distance(points, distance));
}

size_t matrix_lower_bound_distance(const Matrix& points, double distance) {
    size_t lo = 0;
    size_t hi = points.rows;
    while (lo < hi) {
        const size_t mid = (lo + hi) / 2;
        if (points.at(mid, 0) < distance) lo = mid + 1;
        else hi = mid;
    }
    return lo;
}

size_t matrix_upper_bound_distance(const Matrix& points, double distance) {
    size_t lo = 0;
    size_t hi = points.rows;
    while (lo < hi) {
        const size_t mid = (lo + hi) / 2;
        if (points.at(mid, 0) <= distance) lo = mid + 1;
        else hi = mid;
    }
    return lo;
}

bool dense_repeater_overview_lod(size_t visible_repeater_count, double scale) {
    return visible_repeater_count > k_dense_repeater_overlay_threshold &&
        scale < k_dense_repeater_overview_scale;
}

bool dense_repeater_segment_lod(size_t visible_repeater_count, double scale) {
    return visible_repeater_count > k_dense_repeater_overlay_threshold &&
        scale < k_dense_repeater_segment_scale;
}

} // namespace canvas2d

void App::rebuild_speed_limit_marker_overlay_cache() {
    plan_data_cache_.valid = false;
    profile_data_cache_.key.reset();
    ++plan_data_source_revision_;
    if (!has_model_) {
        speed_limit_marker_cache_.clear();
        return;
    }
    canvas2d::populate_speed_limit_marker_cache(model_, speed_limit_marker_cache_);
}

void App::rebuild_marker_overlay_cache() {
    using namespace canvas2d;

    plan_data_cache_.valid = false;
    profile_data_cache_.key.reset();
    ++plan_data_source_revision_;
    structure_marker_cache_.clear();
    repeater_marker_cache_.clear();
    signal_marker_cache_.clear();
    section_marker_cache_.clear();
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
    legacy_fog_marker_cache_.clear();
    draw_distance_marker_cache_.clear();
    speed_limit_marker_cache_.clear();
    curve_parameter_marker_cache_.clear();
    own_track_edit_marker_cache_.clear();
    other_track_change_marker_cache_.clear();
    if (!has_model_ || model_.own.empty()) return;

    std::map<std::string, TrackSource> track_sources;
    TrackSource own_source{&model_.own, true};
    for (const char* key : k_own_track_lookup_aliases) {
        track_sources[normalize_track_lookup_key(key)] = own_source;
    }

    auto append_own_track_edit_markers = [&](const std::vector<TableRow>& rows,
                                             bool gradient) {
        for (size_t row_index = 0; row_index < rows.size(); ++row_index) {
            const TableRow& row = rows[row_index];
            const double distance = table_cell_number(row, "distance");
            const std::optional<TrackPoint> point =
                sample_matrix_track_point(model_.own, distance, true);
            if (!point) continue;
            const std::string method = ascii_lower(table_cell(row, "method"));
            if (!gradient &&
                (method == "curve.setgauge" || method == "curve.gauge" ||
                 method == "curve.setcenter" || method == "curve.setfunction")) {
                PlanCurveParameterMarker marker;
                marker.d = distance;
                marker.x = point->x;
                marker.y = point->y;
                marker.label = method == "curve.setcenter" ? "CC" :
                    method == "curve.setfunction" ? "CF" : "CG";
                marker.value_label = table_cell(row, "radius");
                marker.edit_id = row.edit_id;
                marker.row_index = row_index;
                curve_parameter_marker_cache_.push_back(std::move(marker));
                continue;
            }
            if (!gradient && method == "curve.interpolate") continue;
            const bool transition = method == (gradient
                ? "gradient.begintransition" : "curve.begintransition");
            std::string label;
            if (transition) {
                label = "Tr.";
            } else if (method == (gradient ? "gradient.end" : "curve.end")) {
                label = "End";
            } else if (gradient) {
                label = table_cell(row, "gradient");
            } else {
                label = table_cell(row, "radius");
                const std::string cant = table_cell(row, "cant");
                if (!cant.empty()) label += ", " + cant;
            }
            OwnTrackEditMarker marker;
            marker.d = distance;
            marker.x = point->x;
            marker.y = point->y;
            marker.label = std::move(label);
            marker.edit_id = row.edit_id;
            marker.target_edit_id = transition
                ? table_cell(row, "_primaryEditId") : row.edit_id;
            marker.row_kind = gradient ? "gradient" : "curve";
            marker.row_index = row_index;
            marker.gradient = gradient;
            marker.transition = transition;
            marker.paired = !transition ||
                table_cell(row, "_transitionStatus") == "paired";
            own_track_edit_marker_cache_.push_back(std::move(marker));
        }
    };
    append_own_track_edit_markers(model_.curve_rows, false);
    append_own_track_edit_markers(model_.gradient_rows, true);
    for (size_t track_index = 0; track_index < model_.other_tracks.size(); ++track_index) {
        const OtherTrack& track = model_.other_tracks[track_index];
        TrackSource& source = track_sources[normalize_track_lookup_key(track.key)];
        source.points = &track.points;
        source.has_theta_column = false;
        if (source.track_index == std::numeric_limits<size_t>::max()) {
            source.track_index = track_index;
        }
    }

    other_track_change_marker_cache_.reserve(model_.other_track_changes.size());
    for (size_t row_index = 0; row_index < model_.other_track_changes.size(); ++row_index) {
        const TableRow& row = model_.other_track_changes[row_index];
        if (row.edit_id.empty()) continue;
        const std::string normalized_key =
            normalize_track_lookup_key(table_cell(row, "trackKey"));
        const auto source_it = track_sources.find(normalized_key);
        if (source_it == track_sources.end() ||
            source_it->second.track_index >= model_.other_tracks.size()) {
            continue;
        }
        const size_t track_index = source_it->second.track_index;
        const OtherTrack& track = model_.other_tracks[track_index];
        const double distance = table_cell_number(row, "distance");
        if (distance < track.range_min || distance > track.range_max) continue;
        const std::optional<TrackPoint> point =
            sample_matrix_track_point(track.points, distance, false);
        if (!point) continue;
        OtherTrackChangeMarker marker;
        marker.d = distance;
        marker.x = point->x;
        marker.y = point->y;
        const std::string method = table_cell(row, "method");
        const std::string method_suffix =
            method.compare(0, 6, "Track.") == 0 ? method.substr(6) : method;
        marker.label = "Track[" + table_cell(row, "trackKey") + "]." +
            method_suffix + "(" + table_cell(row, "parameters") + ")";
        marker.edit_id = row.edit_id;
        marker.row_index = row_index;
        marker.track_index = track_index;
        other_track_change_marker_cache_.push_back(std::move(marker));
    }

    auto find_track_source = [&](const std::string& normalized_key)
        -> std::optional<TrackSource> {
        const auto it = track_sources.find(normalized_key);
        if (it == track_sources.end() || !it->second.points) return std::nullopt;
        return it->second;
    };

    auto sample_track_base = [&](const std::string& key, double distance)
        -> std::optional<TrackPoint> {
        const std::string normalized_key = normalize_track_lookup_key(key);
        if (auto source = find_track_source(normalized_key)) {
            auto sampled = sample_matrix_track_point(
                *source->points, distance, source->has_theta_column);
            if (sampled) return sampled;
        }
        if (is_own_track_lookup_alias(normalized_key)) {
            return sample_matrix_track_point(
                *own_source.points, distance, own_source.has_theta_column);
        }
        return std::nullopt;
    };

    auto sample_track = [&](const std::string& key, double distance,
                            double lateral, double forward)
        -> std::optional<TrackPoint> {
        auto sampled = sample_track_base(key, distance);
        if (!sampled) return std::nullopt;
        return offset_track_point(*sampled, lateral, forward);
    };

    auto sample_placement_track = [&](const std::string& key, double distance,
                                      double lateral, double forward)
        -> std::optional<TrackPoint> {
        const std::string normalized_key = normalize_track_lookup_key(key);
        std::optional<TrackSource> source = is_own_track_placement_key(normalized_key)
            ? std::optional<TrackSource>{own_source}
            : find_track_source(normalized_key);
        if (!source) source = own_source;
        auto sampled = sample_matrix_track_point(
            *source->points, distance, source->has_theta_column);
        if (!sampled) return std::nullopt;
        return offset_track_point(*sampled, lateral, forward);
    };

    auto make_marker = [](double distance, const TrackPoint& point,
                          const std::string& label, const std::string& edit_id,
                          size_t row_index) {
        PlanStructureMarker marker;
        marker.d = distance;
        marker.x = point.x;
        marker.y = point.y;
        marker.label = label;
        marker.edit_id = edit_id;
        marker.row_index = row_index;
        return marker;
    };

    structure_marker_cache_.reserve(
        model_.structures.size() + model_.structures_between.size());
    for (const auto& row : model_.structures) {
        const double distance = table_cell_number(row, "distance");
        const double lateral = table_cell_number(row, "x");
        const double forward = table_cell_number(row, "z");
        if (auto point = sample_placement_track(
                table_cell(row, "trackKey"), distance, lateral, forward)) {
            structure_marker_cache_.push_back(make_marker(
                distance, *point, table_cell(row, "structureKey"), row.edit_id,
                structure_marker_cache_.size()));
        } else {
            structure_marker_cache_.push_back(std::nullopt);
        }
    }
    for (const auto& row : model_.structures_between) {
        const double distance = table_cell_number(row, "distance");
        auto point1 = sample_placement_track(
            table_cell(row, "trackKey1"), distance, 0.0, 0.0);
        auto point2 = sample_placement_track(
            table_cell(row, "trackKey2"), distance, 0.0, 0.0);
        if (!point1 && !point2) {
            structure_marker_cache_.push_back(std::nullopt);
            continue;
        }
        TrackPoint point = point1 ? *point1 : *point2;
        if (point1 && point2) {
            point.x = (point1->x + point2->x) * 0.5;
            point.y = (point1->y + point2->y) * 0.5;
            point.z = (point1->z + point2->z) * 0.5;
            point.theta = angle_lerp(point1->theta, point2->theta, 0.5);
        }
        structure_marker_cache_.push_back(make_marker(
            distance, point, table_cell(row, "structureKey"), row.edit_id,
            structure_marker_cache_.size()));
    }

    auto build_standard_markers = [&](const std::vector<TableRow>& rows,
                                      auto& cache,
                                      const std::string& label_key,
                                      bool use_row_placement = false) {
        cache.reserve(rows.size());
        for (const TableRow& row : rows) {
            const double distance = table_cell_number(row, "distance");
            std::optional<TrackPoint> point = use_row_placement
                ? sample_placement_track(
                      table_cell(row, "trackKey"), distance,
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
            marker.label = label_key.empty()
                ? std::string{} : table_cell(row, label_key);
            if (marker.label.empty()) {
                marker.label = "#" + std::to_string(cache.size() + 1);
            }
            marker.edit_id = row.edit_id;
            marker.row_index = cache.size();
            cache.push_back(std::move(marker));
        }
    };

    build_standard_markers(
        model_.signals, signal_marker_cache_, "signalAspectKey", true);
    build_standard_markers(model_.section_begins, section_marker_cache_, "");
    for (size_t row_index = 0; row_index < section_marker_cache_.size(); ++row_index) {
        if (section_marker_cache_[row_index]) {
            section_marker_cache_[row_index]->label = join_table_values(
                section_row_values(model_.section_begins[row_index]), ",");
        }
    }
    build_standard_markers(model_.beacons, beacon_marker_cache_, "type");
    build_standard_markers(model_.pretrains, pretrain_marker_cache_, "passTime");

    auto build_other_train_path_points = [&](const std::string& key,
                                             double start, double end) {
        std::vector<TrackPoint> points;
        if (end < start) std::swap(start, end);
        const std::string normalized_key = normalize_track_lookup_key(key);
        auto source = find_track_source(normalized_key);
        if (!source && is_own_track_lookup_alias(normalized_key)) source = own_source;
        if (!source || !source->points || source->points->empty() ||
            source->points->cols < 3) {
            return points;
        }

        auto append_sample = [&](double distance) {
            auto sampled = sample_matrix_track_point(
                *source->points, distance, source->has_theta_column);
            if (!sampled) return;
            if (!points.empty() &&
                std::abs(points.back().d - sampled->d) < 1e-6) {
                points.back() = *sampled;
                return;
            }
            points.push_back(*sampled);
        };

        append_sample(start);
        const size_t first = matrix_upper_bound_distance(*source->points, start);
        const size_t last = matrix_lower_bound_distance(*source->points, end);
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
        definitions_by_train_key[state.normalized_train_key].push_back(
            other_train_definitions.size());
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
    for (auto& entry : definitions_by_train_key) {
        std::stable_sort(entry.second.begin(), entry.second.end(), by_distance_order);
    }

    for (size_t stop_row_index = 0;
         stop_row_index < model_.other_train_stops.size(); ++stop_row_index) {
        const TableRow& row = model_.other_train_stops[stop_row_index];
        const std::string normalized_train_key =
            normalize_track_lookup_key(table_cell(row, "trainKey"));
        const auto definitions_it = definitions_by_train_key.find(normalized_train_key);
        if (definitions_it == definitions_by_train_key.end() ||
            definitions_it->second.empty()) {
            continue;
        }
        const double stop_distance = table_cell_number(row, "distance");
        const double stop_order = table_cell_number(row, "order");
        size_t chosen_definition = definitions_it->second.front();
        for (size_t definition_index : definitions_it->second) {
            const auto& definition = other_train_definitions[definition_index];
            if (definition.distance < stop_distance - 1e-6 ||
                (std::abs(definition.distance - stop_distance) <= 1e-6 &&
                 definition.order <= stop_order)) {
                chosen_definition = definition_index;
                continue;
            }
            break;
        }
        other_train_definitions[chosen_definition].stop_rows.push_back(stop_row_index);
    }

    other_train_stop_marker_cache_.assign(
        model_.other_train_stops.size(), std::nullopt);
    for (auto& definition : other_train_definitions) {
        std::stable_sort(definition.stop_rows.begin(), definition.stop_rows.end(),
            [&](size_t lhs, size_t rhs) {
                const TableRow& a = model_.other_train_stops[lhs];
                const TableRow& b = model_.other_train_stops[rhs];
                const double a_distance = table_cell_number(a, "distance");
                const double b_distance = table_cell_number(b, "distance");
                if (a_distance < b_distance) return true;
                if (a_distance > b_distance) return false;
                const double a_order = table_cell_number(a, "order");
                const double b_order = table_cell_number(b, "order");
                if (a_order < b_order) return true;
                if (a_order > b_order) return false;
                return lhs < rhs;
            });

        std::vector<size_t> valid_stop_rows;
        for (size_t stop_row_index : definition.stop_rows) {
            const TableRow& stop_row = model_.other_train_stops[stop_row_index];
            const double distance = table_cell_number(stop_row, "distance");
            auto point = sample_track_base(definition.track_key, distance);
            if (!point) continue;

            PlanOtherTrainStopMarker marker;
            marker.d = distance;
            marker.x = point->x;
            marker.y = point->y;
            marker.theta = point->theta;
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
        const double start = table_cell_number(
            model_.other_train_stops[valid_stop_rows.front()], "distance");
        const double end = table_cell_number(
            model_.other_train_stops[valid_stop_rows.back()], "distance");
        std::vector<TrackPoint> points =
            build_other_train_path_points(definition.track_key, start, end);
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
    build_standard_markers(model_.legacy_fogs, legacy_fog_marker_cache_, "");
    build_standard_markers(model_.draw_distances, draw_distance_marker_cache_, "value");
    populate_speed_limit_marker_cache(model_, speed_limit_marker_cache_);

    auto build_repeater_segment = [&](TrackSource source, double start, double end,
                                      double lateral, double forward) {
        PlanRepeaterSegment segment;
        if (end < start) std::swap(start, end);
        if (!source.points || source.points->empty() || source.points->cols < 3) {
            return segment;
        }

        const size_t first = matrix_upper_bound_distance(*source.points, start);
        const size_t last = matrix_lower_bound_distance(*source.points, end);
        const size_t candidate_points = (last > first ? last - first : 0) + 2;
        const size_t points_per_following_chunk =
            k_repeater_segment_chunk_point_limit - 1;
        const size_t chunk_count = candidate_points <= k_repeater_segment_chunk_point_limit
            ? 1
            : 1 + (candidate_points - k_repeater_segment_chunk_point_limit +
                   points_per_following_chunk - 1) / points_per_following_chunk;
        segment.chunks.reserve(chunk_count);

        auto append_sample = [&](const std::optional<TrackPoint>& sampled) {
            if (!sampled) return;
            const TrackPoint point = offset_track_point(*sampled, lateral, forward);
            append_repeater_segment_point(segment, point);
        };

        append_sample(sample_matrix_track_point(
            *source.points, start, source.has_theta_column));
        double previous_distance = std::numeric_limits<double>::quiet_NaN();
        size_t lower_bound_row = first;
        for (size_t row_index = first; row_index < last; ++row_index) {
            const double distance = source.points->at(row_index, 0);
            if (row_index == first || distance != previous_distance) {
                lower_bound_row = row_index;
            }
            append_sample(sample_matrix_track_point_at_lower_bound(
                *source.points, distance, source.has_theta_column, lower_bound_row));
            previous_distance = distance;
        }
        append_sample(sample_matrix_track_point(
            *source.points, end, source.has_theta_column));
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

    auto make_repeater_marker = [](
        double distance, const TrackPoint& point, const std::string& label,
        const std::string& edit_id, size_t row_index) {
        PlanRepeaterMarker marker;
        marker.d = distance;
        marker.x = point.x;
        marker.y = point.y;
        marker.label = label;
        marker.edit_id = edit_id;
        marker.row_index = row_index;
        return marker;
    };

    auto finish_repeater = [&](const RepeaterBeginState& begin,
                               double end_distance, const std::string& label) {
        if (begin.row_index >= repeater_marker_cache_.size()) return;
        RepeaterOverlayRow& overlay = repeater_marker_cache_[begin.row_index];
        if (!begin.track_source) return;

        TrackSource segment_source = *begin.track_source;
        auto source_begin = sample_matrix_track_point(
            *segment_source.points, begin.distance, segment_source.has_theta_column);
        auto source_end = sample_matrix_track_point(
            *segment_source.points, end_distance, segment_source.has_theta_column);
        if (begin.own_track_alias && !source_begin && !source_end) {
            segment_source = own_source;
        }
        overlay.segment = build_repeater_segment(
            segment_source, begin.distance, end_distance,
            begin.lateral, begin.forward);
        auto base = std::move(source_end);
        if (!base && begin.own_track_alias) {
            base = sample_matrix_track_point(
                *own_source.points, end_distance, own_source.has_theta_column);
        }
        if (base) {
            const TrackPoint point =
                offset_track_point(*base, begin.lateral, begin.forward);
            overlay.end_marker = make_repeater_marker(
                end_distance, point, label, begin.edit_id, begin.row_index);
        }
    };

    repeater_marker_cache_.reserve(model_.repeaters.size());
    for (const repeater_linkage::Segment& segment :
         repeater_linkage::pair_segments(table_repeater_events(model_.repeaters))) {
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
        const std::string normalized_track_key =
            normalize_track_lookup_key(begin.track_key);
        const bool own_placement_key =
            is_own_track_placement_key(normalized_track_key);
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
                *begin.track_source->points, begin.distance,
                begin.track_source->has_theta_column);
            if (!base && begin.own_track_alias) {
                base = sample_matrix_track_point(
                    *own_source.points, begin.distance, own_source.has_theta_column);
            }
            if (base) {
                const TrackPoint point =
                    offset_track_point(*base, begin.lateral, begin.forward);
                overlay.begin_marker = make_repeater_marker(
                    begin.distance, point, key, begin.edit_id, begin.row_index);
            }
        }
        repeater_marker_cache_.push_back(std::move(overlay));
        if (segment.boundary_kind != repeater_linkage::BoundaryKind::Open) {
            finish_repeater(begin, segment.end_distance, key);
        }
    }
}
