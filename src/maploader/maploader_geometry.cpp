/*
 * Copyright (c) 2026 Sapporo_ningyo
 *
 * Portions of the map parsing and track-geometry design are derived from or
 * reimplemented with reference to kobushi-trackviewer, Copyright (c) 2021-2024
 * konawasabi, licensed under Apache License 2.0.
 *
 * Licensed under Apache License 2.0; see LICENSE and NOTICE.
 */

#include "maploader_internal.h"
#include "own_track_geometry.h"
#include "repeater_linkage.h"
#include "track_event_cursor.h"

namespace kme::maploader::detail {

using own_track_geometry::CantProcessor;
using own_track_geometry::TrackPointer;


std::vector<double> sorted_unique(std::vector<double> values) {
    std::sort(values.begin(), values.end());
    values.erase(std::unique(values.begin(), values.end()), values.end());
    return values;
}

namespace {

constexpr size_t k_max_control_point_distribution_count = 10000000;
constexpr size_t k_max_repeater_scene_sample_count = 1000000;

size_t checked_arange_count(double start,
                            double end,
                            double step,
                            long double end_margin,
                            size_t supported_limit,
                            const char* description) {
    if (!std::isfinite(start) || !std::isfinite(end) || !std::isfinite(step)) {
        throw std::runtime_error(std::string(description) +
                                 " start, end, and step must be finite");
    }
    if (!(step > 0.0)) {
        throw std::runtime_error(std::string(description) +
                                 " step must be greater than zero");
    }
    if (!(start + step > start)) {
        throw std::runtime_error(std::string(description) +
                                 " step does not advance the start value");
    }

    const long double exclusive_end = static_cast<long double>(end) + end_margin;
    const long double begin = static_cast<long double>(start);
    if (!(exclusive_end > begin)) return 0;
    const long double requested = std::ceil(
        (exclusive_end - begin) / static_cast<long double>(step));
    if (!std::isfinite(requested) ||
        requested > static_cast<long double>(supported_limit)) {
        throw std::runtime_error(
            std::string(description) + " count exceeds supported limit of " +
            std::to_string(supported_limit));
    }
    return static_cast<size_t>(requested);
}

} // namespace

size_t validate_control_point_distribution(double start, double end, double step) {
    return checked_arange_count(
        start, end, step, 0.0L, k_max_control_point_distribution_count,
        "control-point distribution");
}

void append_arange(std::vector<double>& values, double start, double end, double step) {
    const size_t count = validate_control_point_distribution(start, end, step);
    if (count > values.max_size() - values.size()) {
        throw std::runtime_error("control-point distribution exceeds vector capacity");
    }
    values.reserve(values.size() + count);
    for (size_t index = 0; index < count; ++index) {
        const long double candidate = static_cast<long double>(start) +
            static_cast<long double>(index) * static_cast<long double>(step);
        if (!(candidate < static_cast<long double>(end))) break;
        values.push_back(static_cast<double>(candidate));
    }
}

double round_minus2(double value) {
    return std::round(value / 100.0) * 100.0;
}

struct TrackGeometry {
    Matrix owntrack;
    std::map<std::string, Matrix> othertracks;
    std::array<double, 3> distribution;
    std::array<double, 3> default_distribution;
    double owntrack_seconds = 0.0;
    std::vector<std::pair<std::string, double>> othertrack_seconds;
};

static void generate_owntrack(const MapContext& ctx, TrackGeometry& geometry, double unitdist,
                              bool has_arb, const std::array<double, 3>& arbitrary_distribution,
                              const std::vector<double>& extra_controlpoints) {
    std::vector<double> list_cp = ctx.controlpoints;
    if (list_cp.empty()) list_cp.push_back(0.0);
    list_cp.insert(list_cp.end(), extra_controlpoints.begin(), extra_controlpoints.end());
    const auto bounds = std::minmax_element(list_cp.begin(), list_cp.end());
    const double start = ctx.station_position.empty() ? *bounds.first : ctx.station_position.begin()->first;
    const double end = ctx.station_position.empty() ? *bounds.second : ctx.station_position.rbegin()->first;
    constexpr double boundary_margin = 500.0;
    geometry.default_distribution = {std::max(0.0, round_minus2(start) - boundary_margin),
                                     round_minus2(end) + boundary_margin,
                                     unitdist > 0.0 ? unitdist : 25.0};
    geometry.distribution = has_arb ? arbitrary_distribution : geometry.default_distribution;
    const auto& [distribution_start, distribution_end, distribution_step] = geometry.distribution;
    append_arange(list_cp, distribution_start, distribution_end, distribution_step);

    list_cp = sorted_unique(list_cp);

    std::vector<own_track_geometry::Event> events;
    events.reserve(ctx.own_track.size());
    for (const OwnTrackEvent& event : ctx.own_track) {
        const double value = event.key == "interpolate_func"
            ? (as_text(event.value) == "sin" ? 0.0 : 1.0)
            : as_number(event.value);
        events.push_back({event.distance, event.key, {value, event.value.is_continue()}, event.flag});
    }
    geometry.owntrack.data = own_track_geometry::sample(events, list_cp);
    geometry.owntrack.rows = list_cp.size();
    geometry.owntrack.cols = own_track_geometry::columns;
}

void generate_curveradius(MapContext& ctx) {
    ctx.curveradius_buffer.clear(2);
    ctx.curveradius_buffer.reserve_rows(ctx.own_track.size() * 2 + 2);
    if (ctx.owntrack_buffer.rows == 0) return;
    double min_cp = ctx.owntrack_buffer.data[0];
    double max_cp = ctx.owntrack_buffer.data[(ctx.owntrack_buffer.rows - 1) * ctx.owntrack_buffer.cols];
    ctx.curveradius_buffer.push({min_cp, 0.0});
    TrackPointer radius_p(ctx.own_track, "radius");
    bool previous_bt = false;
    double previous = 0.0;
    while (radius_p.next() >= 0) {
        const auto& e = radius_p.event(radius_p.next());
        double new_radius = e.value.is_continue() ? previous : as_number(e.value);
        if (e.value.is_continue() || previous_bt || e.flag == "i") {
            ctx.curveradius_buffer.push({e.distance, new_radius});
        } else {
            ctx.curveradius_buffer.push({e.distance, previous});
            ctx.curveradius_buffer.push({e.distance, new_radius});
        }
        previous = new_radius;
        previous_bt = e.flag == "bt";
        radius_p.seeknext();
    }
    ctx.curveradius_buffer.push({max_cp, 0.0});
}

double relative_position(double L, double radius, double ya, double yb, double l_intermediate) {
    if (L == 0.0) return yb;
    if (radius != 0.0) {
        double sintheta = std::sqrt(L * L + (yb - ya) * (yb - ya)) / (2.0 * radius);
        if (std::fabs(sintheta) <= 1.0) {
            double tau = std::atan((yb - ya) / L);
            double theta = 2.0 * std::asin(sintheta);
            double phiA = theta / 2.0 - tau;
            double x0 = radius * std::sin(phiA);
            double y0 = ya + radius * std::cos(phiA);
            return y0 - radius * std::cos(std::asin((l_intermediate - x0) / radius));
        }
    }
    return (yb - ya) / L * l_intermediate + ya;
}

struct OtherTrackBuildResult {
    std::string key;
    Matrix buffer;
    double seconds = 0.0;
    bool has_buffer = false;
};

Matrix build_othertrack_buffer(const MapContext& ctx, const Matrix& owntrack_buffer,
                               const std::string& trackkey, bool& has_buffer) {
    const auto& data = ctx.othertrack.at(trackkey);
    has_buffer = false;
    if (data.empty() || owntrack_buffer.rows == 0) return {};

    TrackPointer x_position_p(data, "x.position");
    TrackPointer x_radius_p(data, "x.radius");
    TrackPointer y_position_p(data, "y.position");
    TrackPointer y_radius_p(data, "y.radius");
    TrackPointer func_p(data, "interpolate_func");
    TrackPointer center_p(data, "center");
    TrackPointer gauge_p(data, "gauge");
    TrackPointer cant_p(data, "cant");

    double x_position_last = 0.0, x_position_next = 0.0;
    double x_radius_last = 0.0, x_radius_next = 0.0;
    double y_position_last = 0.0, y_position_next = 0.0;
    double y_radius_last = 0.0, y_radius_next = 0.0;
    double center_last = 0.0, center_next = 0.0;
    double gauge_last = 0.0, gauge_next = 0.0;
    double cant_initial = 0.0;
    std::string func_last = "line";
    std::string func_next = "line";

    auto init_numeric = [](const TrackPointer<OtherTrackEvent>& p, double& last, double& next) {
        if (p.next() < 0) return;
        const auto& e = p.event(p.next());
        double v = e.value.is_continue() ? 0.0 : as_number(e.value);
        last = v;
        next = v;
    };
    init_numeric(x_position_p, x_position_last, x_position_next);
    init_numeric(x_radius_p, x_radius_last, x_radius_next);
    init_numeric(y_position_p, y_position_last, y_position_next);
    init_numeric(y_radius_p, y_radius_last, y_radius_next);
    init_numeric(center_p, center_last, center_next);
    init_numeric(gauge_p, gauge_last, gauge_next);
    if (cant_p.next() >= 0) {
        const auto& e = cant_p.event(cant_p.next());
        cant_initial = e.value.is_continue() ? 0.0 : as_number(e.value);
    }
    if (func_p.next() >= 0) {
        const auto& e = func_p.event(func_p.next());
        func_last = func_next = e.value.is_continue() ? "line" : as_text(e.value);
    }

    CantProcessor cant_gen(cant_p, cant_initial);
    Matrix result;
    result.clear(8);
    result.reserve_rows(owntrack_buffer.rows);

    auto advance_over = [](TrackPointer<OtherTrackEvent>& p, double& last, double& next, double dist) {
        while (p.over_nextpoint(dist)) {
            p.seeknext();
            last = next;
            if (p.next() >= 0) {
                const auto& e = p.event(p.next());
                next = e.value.is_continue() ? last : as_number(e.value);
            }
        }
    };
    auto advance_on = [](TrackPointer<OtherTrackEvent>& p, double& last, double& next, double dist) {
        while (p.on_nextpoint(dist)) {
            p.seeknext();
            last = next;
            if (p.next() >= 0) {
                const auto& e = p.event(p.next());
                next = e.value.is_continue() ? last : as_number(e.value);
            }
        }
    };

    // Child tracks keep their first defined value before the first explicit point.
    // The initial values above already hold that first point, so emit the full own-track range.
    for (size_t r = 0; r < owntrack_buffer.rows; ++r) {
        const double* element = &owntrack_buffer.data[r * owntrack_buffer.cols];
        double dist = element[0];

        advance_over(x_position_p, x_position_last, x_position_next, dist);
        advance_over(x_radius_p, x_radius_last, x_radius_next, dist);
        advance_over(y_position_p, y_position_last, y_position_next, dist);
        advance_over(y_radius_p, y_radius_last, y_radius_next, dist);

        while (func_p.on_nextpoint(dist)) {
            func_p.seeknext();
            func_last = func_next;
            if (func_p.next() >= 0) {
                const auto& e = func_p.event(func_p.next());
                func_next = e.value.is_continue() ? func_last : as_text(e.value);
            }
        }
        advance_on(center_p, center_last, center_next, dist);
        advance_on(gauge_p, gauge_last, gauge_next, dist);

        double out_x = 0.0, out_y = 0.0;
        double sin_theta = std::sin(element[4]);
        double cos_theta = std::cos(element[4]);
        if (x_position_p.last() >= 0 && x_position_p.next() >= 0) {
            double x_distance_last = x_position_p.event(x_position_p.last()).distance;
            double x_distance_next = x_position_p.event(x_position_p.next()).distance;
            double rel = relative_position(x_distance_next - x_distance_last,
                                           x_radius_last, x_position_last,
                                           x_position_next, dist - x_distance_last);
            out_x = element[1] - sin_theta * rel;
            out_y = element[2] + cos_theta * rel;
        } else {
            double rel = x_position_last;
            out_x = element[1] - sin_theta * rel;
            out_y = element[2] + cos_theta * rel;
        }

        double out_z = 0.0;
        if (y_position_p.last() >= 0 && y_position_p.next() >= 0) {
            double y_distance_last = y_position_p.event(y_position_p.last()).distance;
            double y_distance_next = y_position_p.event(y_position_p.next()).distance;
            double rel = relative_position(y_distance_next - y_distance_last,
                                           y_radius_last, y_position_last,
                                           y_position_next, dist - y_distance_last);
            out_z = rel + element[3];
        } else {
            out_z = y_position_last + element[3];
        }

        double cant = cant_gen.process(dist, func_last == "sin");
        result.push({dist, out_x, out_y, out_z, func_last == "sin" ? 0.0 : 1.0,
                     cant, center_last, gauge_last});
    }
    has_buffer = true;
    return result;
}

void relocate(MapContext& ctx) {
    ctx.controlpoints = sorted_unique(ctx.controlpoints);
    std::stable_sort(ctx.own_track.begin(), ctx.own_track.end(),
                     [](const auto& a, const auto& b) { return a.distance < b.distance; });
    for (auto& kv : ctx.othertrack) {
        auto& rows = kv.second;
        std::stable_sort(rows.begin(), rows.end(),
                         [](const auto& a, const auto& b) { return a.distance < b.distance; });
        if (!rows.empty()) {
            ctx.othertrack_range[kv.first] = {rows.front().distance, rows.back().distance};
        }
    }
    auto by_distance = [](const auto& a, const auto& b) { return a.distance < b.distance; };
    std::stable_sort(ctx.structure_loads.begin(), ctx.structure_loads.end(), by_distance);
    std::stable_sort(ctx.structure_puts.begin(), ctx.structure_puts.end(), by_distance);
    std::stable_sort(ctx.structure_betweens.begin(), ctx.structure_betweens.end(), by_distance);
    std::stable_sort(ctx.other_trains.begin(), ctx.other_trains.end(), by_distance);
    std::stable_sort(ctx.other_train_enables.begin(), ctx.other_train_enables.end(), by_distance);
    std::stable_sort(ctx.other_train_stops.begin(), ctx.other_train_stops.end(), by_distance);
    std::stable_sort(ctx.repeaters.begin(), ctx.repeaters.end(), by_distance);
    std::stable_sort(ctx.section_begins.begin(), ctx.section_begins.end(), by_distance);
    std::stable_sort(ctx.section_speed_limits.begin(), ctx.section_speed_limits.end(), by_distance);
    std::stable_sort(ctx.signal_puts.begin(), ctx.signal_puts.end(), by_distance);
    std::stable_sort(ctx.beacons.begin(), ctx.beacons.end(), by_distance);
    std::stable_sort(ctx.pretrains.begin(), ctx.pretrains.end(), by_distance);
    std::stable_sort(ctx.map_sounds.begin(), ctx.map_sounds.end(), by_distance);
    std::stable_sort(ctx.map_sound_3d.begin(), ctx.map_sound_3d.end(), by_distance);
    std::stable_sort(ctx.rolling_noises.begin(), ctx.rolling_noises.end(), by_distance);
    std::stable_sort(ctx.flange_noises.begin(), ctx.flange_noises.end(), by_distance);
    std::stable_sort(ctx.joint_noises.begin(), ctx.joint_noises.end(), by_distance);
    std::stable_sort(ctx.irregularities.begin(), ctx.irregularities.end(), by_distance);
    std::stable_sort(ctx.backgrounds.begin(), ctx.backgrounds.end(), by_distance);
    std::stable_sort(ctx.adhesions.begin(), ctx.adhesions.end(), by_distance);
    std::stable_sort(ctx.cab_illuminance.begin(), ctx.cab_illuminance.end(), by_distance);
    std::stable_sort(ctx.fogs.begin(), ctx.fogs.end(), by_distance);
    std::stable_sort(ctx.draw_distances.begin(), ctx.draw_distances.end(), by_distance);
    std::stable_sort(ctx.speedlimits.begin(), ctx.speedlimits.end(), by_distance);
    std::stable_sort(ctx.station_puts.begin(), ctx.station_puts.end(), by_distance);
}

void build_structure_put_buffer(MapContext& ctx) {
    ctx.structure_put_buffer.clear(10);
    ctx.structure_put_buffer.reserve_rows(ctx.structure_puts.size());
    for (const auto& row : ctx.structure_puts) {
        ctx.structure_put_buffer.push({row.distance, row.x, row.y, row.z,
                                       row.rx, row.ry, row.rz, row.tilt,
                                       row.span, static_cast<double>(row.order)});
    }
}

double matrix_value(const Matrix& matrix, size_t row, size_t col) {
    return matrix.data[row * matrix.cols + col];
}

double angle_delta_abs(double a, double b) {
    return std::abs(std::atan2(std::sin(b - a), std::cos(b - a)));
}

void append_controlpoint_if_in_range(std::vector<double>& values, double distance,
                                     double min_distance, double max_distance) {
    if (!std::isfinite(distance)) return;
    constexpr double eps = 1e-6;
    if (distance < min_distance - eps || distance > max_distance + eps) return;
    values.push_back(std::clamp(distance, min_distance, max_distance));
}

void append_scene_model_distance(std::vector<double>& values, double distance, double span,
                                 double min_distance, double max_distance) {
    append_controlpoint_if_in_range(values, distance, min_distance, max_distance);
    if (span > 1.0) append_controlpoint_if_in_range(values, distance + span, min_distance, max_distance);
}

static std::vector<double> build_scene_adaptive_controlpoints(const MapContext& ctx,
                                                       const Matrix& baseline,
                                                       double min_step,
                                                       double max_step,
                                                       double max_angle_degrees,
                                                       double max_chord_error) {
    std::vector<double> values;
    if (baseline.rows < 2 || baseline.cols < 5) return values;

    min_step = std::clamp(std::isfinite(min_step) ? min_step : 1.0, 0.25, 100.0);
    max_step = std::clamp(std::isfinite(max_step) ? max_step : 25.0, min_step, 200.0);
    const double max_angle = std::max(0.001, (std::isfinite(max_angle_degrees) ? max_angle_degrees : 1.0) * k_pi / 180.0);
    const double max_error = std::max(0.001, std::isfinite(max_chord_error) ? max_chord_error : 0.01);
    const double min_distance = matrix_value(baseline, 0, 0);
    const double max_distance = matrix_value(baseline, baseline.rows - 1, 0);

    values.reserve(baseline.rows);
    for (size_t row = 1; row < baseline.rows; ++row) {
        const double a_distance = matrix_value(baseline, row - 1, 0);
        const double b_distance = matrix_value(baseline, row, 0);
        const double span = b_distance - a_distance;
        if (!(span > 0.0) || !std::isfinite(span)) continue;

        double desired_step = max_step;
        if (baseline.cols > 5) {
            const double r0 = std::abs(matrix_value(baseline, row - 1, 5));
            const double r1 = std::abs(matrix_value(baseline, row, 5));
            double radius = 0.0;
            if (r0 > 1e-6 && r1 > 1e-6) radius = std::min(r0, r1);
            else if (r0 > 1e-6) radius = r0;
            else if (r1 > 1e-6) radius = r1;
            if (radius > 1e-6 && std::isfinite(radius)) {
                desired_step = std::min(desired_step, std::sqrt(std::max(min_step * min_step,
                                                                         8.0 * radius * max_error)));
            }
        }

        const double theta_delta = angle_delta_abs(matrix_value(baseline, row - 1, 4),
                                                   matrix_value(baseline, row, 4));
        if (theta_delta > max_angle) {
            const double angle_step = span / std::ceil(theta_delta / max_angle);
            desired_step = std::min(desired_step, angle_step);
        }

        desired_step = std::clamp(desired_step, min_step, max_step);
        const double requested_divisions = std::ceil(span / desired_step);
        if (!std::isfinite(requested_divisions) ||
            requested_divisions > static_cast<double>(std::numeric_limits<int>::max())) {
            throw std::runtime_error("adaptive scene subdivision count exceeds supported range");
        }
        const int divisions = std::max(1, static_cast<int>(requested_divisions));
        for (int i = 1; i < divisions; ++i) {
            values.push_back(a_distance + span * (static_cast<double>(i) / static_cast<double>(divisions)));
        }
    }

    for (const StructurePut& row : ctx.structure_puts) {
        append_scene_model_distance(values, row.distance, row.span, min_distance, max_distance);
    }
    for (const StructurePut& row : ctx.structure_betweens) {
        append_controlpoint_if_in_range(values, row.distance, min_distance, max_distance);
    }

    auto append_repeater_range = [&](const RepeaterEvent& repeater, double end_distance) {
        if (end_distance < repeater.distance) return;
        append_scene_model_distance(values, repeater.distance, repeater.span, min_distance, max_distance);
        append_scene_model_distance(values, end_distance, repeater.span, min_distance, max_distance);
        const size_t sample_count = checked_arange_count(
            repeater.distance, end_distance, repeater.interval, 1.0e-6L,
            k_max_repeater_scene_sample_count, "repeater scene sample");
        for (size_t index = 0; index < sample_count; ++index) {
            const long double candidate = static_cast<long double>(repeater.distance) +
                static_cast<long double>(index) *
                    static_cast<long double>(repeater.interval);
            if (!(candidate < static_cast<long double>(end_distance) + 1.0e-6L)) break;
            const double distance = static_cast<double>(candidate);
            append_scene_model_distance(values, distance, repeater.span, min_distance, max_distance);
        }
    };

    std::vector<repeater_linkage::Event> repeater_events;
    repeater_events.reserve(ctx.repeaters.size());
    for (size_t index = 0; index < ctx.repeaters.size(); ++index) {
        const RepeaterEvent& row = ctx.repeaters[index];
        repeater_linkage::Event event;
        event.source_index = index;
        event.distance = row.distance;
        event.order = static_cast<double>(row.order);
        event.key = key_text(row.repeater_key);
        if (row.method == "Begin" || row.method == "Begin0") {
            event.kind = repeater_linkage::EventKind::Begin;
        } else if (row.method == "End") {
            event.kind = repeater_linkage::EventKind::End;
        }
        repeater_events.push_back(std::move(event));
    }
    for (const repeater_linkage::Segment& segment :
         repeater_linkage::pair_segments(std::move(repeater_events))) {
        if (segment.begin_source_index >= ctx.repeaters.size()) continue;
        const double end_distance = segment.boundary_kind == repeater_linkage::BoundaryKind::Open
            ? max_distance : segment.end_distance;
        append_repeater_range(ctx.repeaters[segment.begin_source_index], end_distance);
    }

    return values;
}

static TrackGeometry build_track_geometry(const MapContext& ctx, double unitdist,
                                          bool has_arb, const std::array<double, 3>& distribution,
                                          const std::vector<double>& extra_controlpoints) {
    KME_MAPLOADER_LOG_INFO("calculating track geometry");
    TrackGeometry geometry;
    {
        ScopedTimer timer(&geometry.owntrack_seconds);
        generate_owntrack(ctx, geometry, unitdist, has_arb, distribution, extra_controlpoints);
    }
    std::vector<std::future<OtherTrackBuildResult>> futures;
    futures.reserve(std::min(ctx.othertrack_order.size(),
                             k_max_parallel_maploader_tasks));
    for (size_t first = 0; first < ctx.othertrack_order.size();
         first += k_max_parallel_maploader_tasks) {
        const size_t last = std::min(
            first + k_max_parallel_maploader_tasks, ctx.othertrack_order.size());
        futures.clear();
        for (size_t index = first; index < last; ++index) {
            const std::string& key = ctx.othertrack_order[index];
            futures.push_back(launch_bounded_maploader_task([&ctx, &geometry, key]() {
                auto started_at = SteadyClock::now();
                OtherTrackBuildResult out;
                out.key = key;
                out.buffer = build_othertrack_buffer(ctx, geometry.owntrack, key, out.has_buffer);
                out.seconds = elapsed_seconds_since(started_at);
                return out;
            }));
        }
        for (auto& future : futures) {
            OtherTrackBuildResult result = future.get();
            geometry.othertrack_seconds.push_back({result.key, result.seconds});
            if (result.has_buffer) {
                geometry.othertracks.emplace(std::move(result.key), std::move(result.buffer));
            }
        }
    }
    return geometry;
}

void generate_geometry(MapContext& ctx, double unitdist,
                       bool has_arb, double arb_start, double arb_end, double arb_step) {
    TrackGeometry geometry = build_track_geometry(ctx, unitdist, has_arb, {arb_start, arb_end, arb_step}, {});
    ctx.scene_geometry_valid = false;
    ctx.unit_distance = unitdist;
    ctx.cp_arbdistribution_explicit = has_arb;
    ctx.cp_arbdistribution = geometry.distribution;
    ctx.cp_arbdistribution_default = geometry.default_distribution;
    ctx.cp_defaultrange = {geometry.default_distribution[0], geometry.default_distribution[1]};
    ctx.owntrack_buffer = std::move(geometry.owntrack);
    ctx.othertrack_buffers = std::move(geometry.othertracks);
    ctx.timing.owntrack_seconds = geometry.owntrack_seconds;
    ctx.timing.othertrack_seconds = std::move(geometry.othertrack_seconds);
    ctx.timing.snapshot_seconds = 0.0;
    ctx.load_timing_logged = false;
    generate_curveradius(ctx);
    build_structure_put_buffer(ctx);
}

void generate_scene_geometry(MapContext& ctx, double unitdist, double min_step, double max_step,
                             double max_angle_degrees, double max_chord_error) {
    const std::array<double, 5> parameters{unitdist, min_step, max_step, max_angle_degrees, max_chord_error};
    if (ctx.scene_geometry_valid && ctx.scene_geometry_parameters == parameters) return;
    invalidate_scene_geometry_snapshot(ctx, true);
    const auto extra = build_scene_adaptive_controlpoints(
        ctx, ctx.owntrack_buffer, min_step, max_step, max_angle_degrees, max_chord_error);
    TrackGeometry geometry = build_track_geometry(ctx, unitdist, true, ctx.cp_arbdistribution, extra);
    ctx.scene_owntrack_buffer = std::move(geometry.owntrack);
    ctx.scene_othertrack_buffers = std::move(geometry.othertracks);
    ctx.scene_geometry_parameters = parameters;
    ctx.scene_geometry_valid = true;
}


} // namespace kme::maploader::detail
