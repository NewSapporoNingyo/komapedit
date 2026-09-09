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
#include "canvas2d_background.h"
#include "canvas2d_interaction.h"
#include "canvas2d_marker_cache.h"
#include "canvas2d_primitives.h"
#include "canvas3D.h"
#include "map_marker_visuals.h"
#include "route_value_sampling.h"
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

using canvas2d::dense_repeater_overview_lod;
using canvas2d::dense_repeater_segment_lod;
using canvas2d::color_u32;
using canvas2d::draw_matrix_plan_polyline;
using canvas2d::draw_plan_current_position_arrow;
using canvas2d::draw_plan_diamond_marker;
using canvas2d::draw_plan_direction_arrow;
using canvas2d::draw_plan_focus_arrow;
using canvas2d::draw_plan_pretrain_marker;
using canvas2d::draw_plan_signal_marker;
using canvas2d::draw_plan_small_text;
using canvas2d::draw_plan_triangle_marker;
using canvas2d::draw_polyline;
using canvas2d::draw_polyline_range;
using canvas2d::draw_repeater_segment_chunks;
using canvas2d::draw_scalebar;
using canvas2d::grid_step;
using canvas2d::k_dense_repeater_overview_scale;
using canvas2d::make_plan_transform;
using canvas2d::matrix_lower_bound_distance;
using canvas2d::matrix_upper_bound_distance;
using canvas2d::PlanScreenTransform;
using canvas2d::point_near_canvas;
using canvas2d::sample_matrix_track_point;

using MarkerHit = canvas2d::PlanMarkerHit;

namespace {

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
                sections.push_back({std::max(start, dmin_), std::min(end, dmax_), value,
                                    format_double(value, 0)});
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
                sections.push_back(
                    {std::max(start, dmin_), std::min(end, dmax_), 0.0, {}});
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
        p.theta = model_.own.at(r, 4);
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

    for (size_t row_index = 0; row_index < model_.station_positions.size(); ++row_index) {
        const Station& s = model_.station_positions[row_index];
        if (s.distance < dmin_ || s.distance > dmax_) continue;
        ImVec2 q = rotate_xy(s.x, s.y, angle);
        out.stations.push_back({s, q.x, q.y, row_index,
                                format_double(s.mileage, 0) + "m"});
    }

    for (const auto& s : model_.speedlimits) {
        if (s.distance < dmin_ || s.distance > dmax_) continue;
        size_t idx = nearest_own_index(s.distance);
        TrackPoint p;
        p.x = model_.own.at(idx, 1);
        p.y = model_.own.at(idx, 2);
        p.theta = model_.own.at(idx, 4);
        p = rotate_point(p);
        const TableRow* source = s.row_index < model_.speed_limit_rows.size()
            ? &model_.speed_limit_rows[s.row_index]
            : nullptr;
        out.speedlimits.push_back({s.distance, p.x, p.y, p.theta,
                                   s.has_speed, s.speed,
                                   source ? source->edit_id : std::string{},
                                   s.row_index,
                                   s.has_speed ? format_double(s.speed, 0) : "x"});
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
    if (show_section_markers_) {
        append_markers(section_marker_cache_, out.section_markers);
    }
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
    if (show_fog_markers_) append_markers(legacy_fog_marker_cache_, out.legacy_fog_markers);
    if (show_draw_distance_markers_) {
        append_markers(draw_distance_marker_cache_, out.draw_distance_markers);
    }
    for (const PlanCurveParameterMarker& source : curve_parameter_marker_cache_) {
        const bool visible = source.label == "CG" ? show_curve_gauge_markers_ :
            source.label == "CC" ? show_curve_center_markers_ :
            show_curve_function_markers_;
        if (!visible || source.d < dmin_ || source.d > dmax_) continue;
        PlanCurveParameterMarker marker = source;
        const ImVec2 rotated = rotate_xy(marker.x, marker.y, angle);
        marker.x = rotated.x;
        marker.y = rotated.y;
        if (marker.label == "CG") out.curve_gauge_markers.push_back(std::move(marker));
        else if (marker.label == "CC") out.curve_center_markers.push_back(std::move(marker));
        else out.curve_function_markers.push_back(std::move(marker));
        append_marker_bounds(rotated.x, rotated.y);
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

        std::vector<route_value_sampling::Event> radius_events;
        radius_events.reserve(model_.own_events.size());
        double current_radius = 0.0;
        for (const TrackEvent& event : model_.own_events) {
            if (event.key != "radius") continue;
            route_value_sampling::append_event(
                radius_events, event.distance, event.value_number,
                event.number, event.flag, current_radius);
        }
        out.curve_interpolate_markers.reserve(radius_events.size());
        for (const route_value_sampling::Event& event : radius_events) {
            if (event.kind != route_value_sampling::EventKind::Interpolate ||
                event.distance < dmin_ || event.distance > dmax_) {
                continue;
            }
            const size_t index = nearest_own_index(event.distance);
            TrackPoint point;
            point.x = model_.own.at(index, 1);
            point.y = model_.own.at(index, 2);
            point.theta = model_.own.at(index, 4);
            point = rotate_point(point);
            out.curve_interpolate_markers.push_back({
                event.distance, point.x, point.y, point.theta,
                event.value, format_double(event.value, 0)});
        }
    }
    if (edit_mode_enabled_) {
        for (const OwnTrackEditMarker& source : own_track_edit_marker_cache_) {
            if (source.d < dmin_ || source.d > dmax_) continue;
            if ((!source.gradient && !show_curve_values_) ||
                (source.gradient && !show_gradient_pos_)) {
                continue;
            }
            OwnTrackEditMarker marker = source;
            const ImVec2 rotated = rotate_xy(marker.x, marker.y, angle);
            marker.x = rotated.x;
            marker.y = rotated.y;
            if (marker.gradient) out.gradient_edit_markers.push_back(std::move(marker));
            else out.curve_edit_markers.push_back(std::move(marker));
        }
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
    include_visibility(show_section_markers_, 13);
    include_visibility(show_curve_gauge_markers_, 14);
    include_visibility(show_curve_center_markers_, 15);
    include_visibility(show_curve_function_markers_, 16);

    const bool cache_matches = plan_data_cache_.valid &&
        plan_data_cache_.source_revision == plan_data_source_revision_ &&
        plan_data_cache_.has_model == has_model_ &&
        plan_data_cache_.distance_min == dmin_ &&
        plan_data_cache_.distance_max == dmax_ &&
        plan_data_cache_.mode == mode_ &&
        plan_data_cache_.fitted == plan_view_.fitted &&
        plan_data_cache_.scale == plan_view_.scale &&
        plan_data_cache_.show_curve_values == show_curve_values_ &&
        plan_data_cache_.show_gradient_positions == show_gradient_pos_ &&
        plan_data_cache_.edit_mode_enabled == edit_mode_enabled_ &&
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
    plan_data_cache_.show_gradient_positions = show_gradient_pos_;
    plan_data_cache_.edit_mode_enabled = edit_mode_enabled_;
    plan_data_cache_.marker_visibility_mask = marker_visibility_mask;
    plan_data_cache_.structure_row_visible = structure_row_visible_;
    plan_data_cache_.repeater_row_visible = repeater_row_visible_;
    plan_data_cache_.signal_row_visible = signal_row_visible_;
    plan_data_cache_.other_train_path_visible = other_train_path_visible_;
    plan_data_cache_.valid = true;
    ++plan_data_cache_.generation;
#ifndef NDEBUG
    ++plan_data_cache_.rebuild_count;
#endif
    return plan_data_cache_.data;
}

ProfileData App::build_profile_data() const {
    ProfileData out;
    if (!has_model_ || model_.own.empty()) return out;

    out.own_x.reserve(model_.own.rows);
    out.own_y.reserve(model_.own.rows);
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

    out.curve_x.reserve(model_.curve.rows);
    out.curve_y.reserve(model_.curve.rows);
    for (size_t r = 0; r < model_.curve.rows; ++r) {
        double d = model_.curve.at(r, 0);
        if (d < dmin_ || d > dmax_) continue;
        out.curve_x.push_back(d);
        double radius = model_.curve.at(r, 1);
        out.curve_y.push_back(radius > 0 ? 1.0 : (radius < 0 ? -1.0 : 0.0));
    }

    if (show_profile_other_) {
        out.other.reserve(model_.other_tracks.size());
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

    out.stations.reserve(model_.stations.size());
    out.station_mileage_labels.reserve(model_.stations.size());
    for (const auto& s : model_.stations) {
        if (s.distance >= dmin_ && s.distance <= dmax_) {
            out.stations.push_back(s);
            out.station_mileage_labels.push_back(format_double(s.mileage, 0) + "m");
        }
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
    for (const OwnTrackEditMarker& marker : own_track_edit_marker_cache_) {
        if (!marker.gradient || marker.d < dmin_ || marker.d > dmax_) continue;
        OwnTrackEditMarker profile_marker = marker;
        profile_marker.x = marker.d;
        profile_marker.y = interp_own_z(marker.d);
        out.gradient_edit_markers.push_back(std::move(profile_marker));
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

const ProfileData& App::current_profile_data() {
    auto key_matches = [&]() {
        if (!profile_data_cache_.key) return false;
        const ProfileDataCacheKey& key = *profile_data_cache_.key;
        if (key.source_revision != plan_data_source_revision_ || key.has_model != has_model_ ||
            key.distance_min != dmin_ || key.distance_max != dmax_ ||
            key.show_other != show_profile_other_ || key.language != lang_ ||
            key.other_tracks.size() != model_.other_tracks.size()) return false;
        return std::equal(key.other_tracks.begin(), key.other_tracks.end(),
            model_.other_tracks.begin(), [](const auto& cached, const OtherTrack& track) {
            return cached.key == track.key && cached.visible == track.visible &&
                cached.range_min == track.range_min && cached.range_max == track.range_max &&
                cached.color == std::array<float, 4>{track.color.x, track.color.y,
                                                     track.color.z, track.color.w};
        });
    };
    if (key_matches()) return profile_data_cache_.data;
    profile_data_cache_.data = build_profile_data();
    ProfileDataCacheKey key{plan_data_source_revision_, has_model_, dmin_, dmax_,
                            show_profile_other_, lang_, {}};
    key.other_tracks.reserve(model_.other_tracks.size());
    for (const OtherTrack& track : model_.other_tracks) {
        key.other_tracks.push_back({track.key, track.visible, track.range_min, track.range_max,
                                    {track.color.x, track.color.y, track.color.z, track.color.w}});
    }
    profile_data_cache_.key = std::move(key);
#ifndef NDEBUG
    ++profile_data_cache_.rebuild_count;
#endif
    return profile_data_cache_.data;
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

void App::focus_plan_at_distance(double distance) {
    if (!has_model_ || model_.own.empty()) return;
    const double clamped_distance = std::clamp(distance, dmin_, dmax_);
    const auto point = sample_matrix_track_point(
        model_.own, clamped_distance, true);
    if (!point) return;
    focus_plan_at_model_point(point->x, point->y);
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

#ifndef NDEBUG
ImVec2 App::debug_other_track_change_marker_screen_position(
    const OtherTrackChangeMarker& marker, double model_angle,
    ImVec2 origin, ImVec2 size) const {
    return canvas2d::make_plan_transform(plan_view_, model_angle, origin, size)
        .model_to_screen(marker.x, marker.y);
}

bool App::debug_repeater_overview_indices() {
    return canvas2d::debug_repeater_overview_indices();
}
#endif

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
        return nearest_plan_measure_distance(data, origin, avail, mouse);
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

    size_t selected_repeater_count = static_cast<size_t>(std::count_if(
        repeater_row_visible_.begin(), repeater_row_visible_.end(),
        [](unsigned char visible) { return visible != 0; }));
    bool dense_repeater_marker_lod = dense_repeater_overview_lod(selected_repeater_count, plan_view_.scale);
    bool overview_marker_lod = dense_repeater_marker_lod;
    const float marker_size_scale = marker_size_scale_from_percent(marker_size_percent_);
    const float marker_canvas_margin = std::max(12.0f, 12.0f * marker_size_scale);
    const double marker_hover_radius = static_cast<double>(marker_canvas_margin);
    const double marker_hover_radius_sq = marker_hover_radius * marker_hover_radius;
    const bool marker_hits_enabled =
        hovered && mode_ == Mode::Pan && !picking_background_station;
    auto nearest_marker_hit = [&](const auto& markers,
                                  const PlanScreenTransform& hit_transform,
                                  bool suppressed = false) -> std::optional<MarkerHit> {
        return canvas2d::nearest_marker_hit(
            markers, hit_transform, mouse, origin, avail, marker_canvas_margin,
            marker_hits_enabled && !suppressed);
    };
    PlanScreenTransform hit_transform = make_plan_transform(plan_view_, -data.origin_angle, origin, avail);
    std::optional<MarkerHit> hovered_station_hit =
        nearest_marker_hit(data.stations, hit_transform, !show_stations_);
    std::optional<MarkerHit> hovered_structure_hit =
        nearest_marker_hit(data.structure_markers, hit_transform);
    std::optional<MarkerHit> hovered_repeater_hit =
        nearest_marker_hit(data.repeater_markers, hit_transform, dense_repeater_marker_lod);
    std::optional<MarkerHit> hovered_signal_hit =
        nearest_marker_hit(data.signal_markers, hit_transform);
    std::optional<MarkerHit> hovered_section_hit =
        nearest_marker_hit(data.section_markers, hit_transform);
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
    std::optional<MarkerHit> hovered_legacy_fog_hit =
        nearest_marker_hit(data.legacy_fog_markers, hit_transform);
    std::optional<MarkerHit> hovered_draw_distance_hit =
        nearest_marker_hit(data.draw_distance_markers, hit_transform);
    std::optional<MarkerHit> hovered_speed_limit_hit =
        nearest_marker_hit(data.speedlimits, hit_transform, !show_speedlimits_);
    std::optional<MarkerHit> hovered_curve_edit_hit =
        nearest_marker_hit(data.curve_edit_markers, hit_transform);
    std::optional<MarkerHit> hovered_gradient_edit_hit =
        nearest_marker_hit(data.gradient_edit_markers, hit_transform);
    std::optional<MarkerHit> hovered_curve_gauge_hit =
        nearest_marker_hit(data.curve_gauge_markers, hit_transform);
    std::optional<MarkerHit> hovered_curve_center_hit =
        nearest_marker_hit(data.curve_center_markers, hit_transform);
    std::optional<MarkerHit> hovered_curve_function_hit =
        nearest_marker_hit(data.curve_function_markers, hit_transform);
    std::optional<MarkerHit> hovered_other_track_change_hit;
    if (hovered && mode_ == Mode::Pan && !picking_background_station &&
        edit_actions_available()) {
        double best = marker_hover_radius_sq;
        for (const OtherTrackChangeMarker& marker : other_track_change_marker_cache_) {
            if (marker.track_index >= model_.other_tracks.size()) continue;
            const OtherTrack& track = model_.other_tracks[marker.track_index];
            if (!track.visible || marker.d < std::max(dmin_, track.range_min) ||
                marker.d > std::min(dmax_, track.range_max)) continue;
            const ImVec2 point = hit_transform.model_to_screen(marker.x, marker.y);
            if (!point_near_canvas(point, origin, avail, marker_canvas_margin)) continue;
            const double dx = static_cast<double>(point.x - mouse.x);
            const double dy = static_cast<double>(point.y - mouse.y);
            const double dist_sq = dx * dx + dy * dy;
            if (dist_sq <= best) {
                best = dist_sq;
                hovered_other_track_change_hit =
                    MarkerHit{marker.row_index, dist_sq};
            }
        }
    }
    debug_plan_stage("hit_test");
    std::optional<size_t> hovered_station_row = hovered_station_hit
        ? std::optional<size_t>(hovered_station_hit->row_index)
        : std::nullopt;
    std::optional<size_t> hovered_structure_row = hovered_structure_hit
        ? std::optional<size_t>(hovered_structure_hit->row_index)
        : std::nullopt;
    std::optional<size_t> hovered_repeater_row = hovered_repeater_hit
        ? std::optional<size_t>(hovered_repeater_hit->row_index)
        : std::nullopt;
    std::optional<size_t> hovered_signal_row = hovered_signal_hit
        ? std::optional<size_t>(hovered_signal_hit->row_index)
        : std::nullopt;
    std::optional<size_t> hovered_section_row = hovered_section_hit
        ? std::optional<size_t>(hovered_section_hit->row_index)
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
    std::optional<size_t> hovered_legacy_fog_row = hovered_legacy_fog_hit
        ? std::optional<size_t>(hovered_legacy_fog_hit->row_index)
        : std::nullopt;
    std::optional<size_t> hovered_draw_distance_row = hovered_draw_distance_hit
        ? std::optional<size_t>(hovered_draw_distance_hit->row_index)
        : std::nullopt;
    std::optional<size_t> hovered_speed_limit_row = hovered_speed_limit_hit
        ? std::optional<size_t>(hovered_speed_limit_hit->row_index)
        : std::nullopt;

    auto select_nearest_touch_marker = [&]() {
        std::optional<PlanMarkerSelection> best;
        double best_dist_sq = std::numeric_limits<double>::max();
        auto note = [&](const std::optional<MarkerHit>& hit, PlanMarkerKind kind) {
            if (!hit || hit->dist_sq > best_dist_sq) return;
            best_dist_sq = hit->dist_sq;
            best = PlanMarkerSelection{kind, hit->row_index};
        };
        note(hovered_station_hit, PlanMarkerKind::Station);
        note(hovered_signal_hit, PlanMarkerKind::Signal);
        note(hovered_section_hit, PlanMarkerKind::Section);
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
        note(hovered_legacy_fog_hit, PlanMarkerKind::LegacyFog);
        note(hovered_draw_distance_hit, PlanMarkerKind::DrawDistance);
        note(hovered_speed_limit_hit, PlanMarkerKind::SpeedLimit);
        note(hovered_curve_edit_hit, PlanMarkerKind::Curve);
        note(hovered_gradient_edit_hit, PlanMarkerKind::Gradient);
        note(hovered_curve_gauge_hit, PlanMarkerKind::CurveGauge);
        note(hovered_curve_center_hit, PlanMarkerKind::CurveCenter);
        note(hovered_curve_function_hit, PlanMarkerKind::CurveFunction);
        note(hovered_other_track_change_hit, PlanMarkerKind::OtherTrackChange);
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
        plan_context_menu_entries_ = collect_plan_context_entries(
            data, mouse, origin, avail, marker_canvas_margin,
            marker_hover_radius_sq, marker_hits_enabled);
        if (!plan_context_menu_entries_.empty()) {
            ImGui::OpenPopup("plan_marker_context");
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
    auto draw_own_track_edit_markers = [&](const std::vector<OwnTrackEditMarker>& markers,
                                           PlanMarkerKind kind) {
        for (const OwnTrackEditMarker& marker : markers) {
            const ImVec2 point = transform.plan_to_screen(marker.x, marker.y);
            if (!point_near_canvas(point, origin, avail)) continue;
            const ImU32 color = marker.transition
                ? IM_COL32(38, 130, 66, 255)
                : IM_COL32(62, 214, 102, 255);
            const bool hovered_marker = kind == PlanMarkerKind::Curve
                ? (hovered_curve_edit_hit &&
                   hovered_curve_edit_hit->row_index == marker.row_index)
                : (hovered_gradient_edit_hit &&
                   hovered_gradient_edit_hit->row_index == marker.row_index);
            const bool active = marker_emphasized(kind, marker.row_index,
                                                  hovered_marker);
            draw_selected_marker_ring(point, kind, marker.row_index, color);
            const float scale = marker_size_scale * (active ? 1.28f : 1.0f);
            if (marker.gradient) {
                draw_plan_triangle_marker(draw, point, color, scale);
            } else {
                draw->AddCircleFilled(point, 5.0f * scale, color, 18);
            }
            if (active) draw_plan_small_text(draw, point, color, marker.label);
        }
    };
    draw_own_track_edit_markers(data.curve_edit_markers, PlanMarkerKind::Curve);
    draw_own_track_edit_markers(data.gradient_edit_markers, PlanMarkerKind::Gradient);
    auto draw_curve_parameter_markers = [&](const std::vector<PlanCurveParameterMarker>& markers,
                                            PlanMarkerKind kind,
                                            const std::optional<MarkerHit>& hovered_hit) {
        const ImU32 color = IM_COL32(255, 255, 255, 255);
        for (const PlanCurveParameterMarker& marker : markers) {
            const ImVec2 point = transform.plan_to_screen(marker.x, marker.y);
            if (!point_near_canvas(point, origin, avail)) continue;
            const bool hovered_marker = hovered_hit &&
                hovered_hit->row_index == marker.row_index;
            const bool active = marker_emphasized(kind, marker.row_index, hovered_marker);
            draw_selected_marker_ring(point, kind, marker.row_index, color);
            const float scale = marker_size_scale * (active ? 1.18f : 1.0f);
            const ImVec2 text_size = ImGui::CalcTextSize(marker.label.c_str());
            const ImVec2 half(std::max(10.0f, text_size.x * 0.5f + 3.0f) * scale,
                              std::max(8.0f, text_size.y * 0.5f + 2.0f) * scale);
            draw->AddRect(ImVec2(point.x - half.x, point.y - half.y),
                          ImVec2(point.x + half.x, point.y + half.y), color,
                          0.0f, 0, std::max(1.0f, 1.5f * scale));
            draw->AddText(ImVec2(point.x - text_size.x * 0.5f,
                                 point.y - text_size.y * 0.5f),
                          color, marker.label.c_str());
        }
    };
    draw_curve_parameter_markers(data.curve_gauge_markers,
                                 PlanMarkerKind::CurveGauge,
                                 hovered_curve_gauge_hit);
    draw_curve_parameter_markers(data.curve_center_markers,
                                 PlanMarkerKind::CurveCenter,
                                 hovered_curve_center_hit);
    draw_curve_parameter_markers(data.curve_function_markers,
                                 PlanMarkerKind::CurveFunction,
                                 hovered_curve_function_hit);
    for (const auto& t : model_.other_tracks) {
        if (!t.visible || t.points.empty()) continue;
        double rmin = std::max(dmin_, t.range_min);
        double rmax = std::min(dmax_, t.range_max);
        draw_matrix_plan_polyline(draw, t.points, rmin, rmax, transform, origin, avail, color_u32(t.color), line_widths.other_track_px);
    }
    if (edit_actions_available()) {
        for (const OtherTrackChangeMarker& marker : other_track_change_marker_cache_) {
            if (marker.track_index >= model_.other_tracks.size()) continue;
            const OtherTrack& track = model_.other_tracks[marker.track_index];
            if (!track.visible || marker.d < std::max(dmin_, track.range_min) ||
                marker.d > std::min(dmax_, track.range_max)) continue;
            const ImVec2 point = transform.model_to_screen(marker.x, marker.y);
            if (!point_near_canvas(point, origin, avail)) continue;
            const bool hovered_marker = hovered_other_track_change_hit &&
                hovered_other_track_change_hit->row_index == marker.row_index;
            const bool active = marker_emphasized(
                PlanMarkerKind::OtherTrackChange, marker.row_index,
                hovered_marker);
            const ImU32 color = color_u32(track.color);
            draw_selected_marker_ring(point, PlanMarkerKind::OtherTrackChange,
                                      marker.row_index, color);
            const float radius = 5.0f * marker_size_scale *
                (active ? 1.28f : 1.0f);
            draw_map_marker_icon(
                draw, MapMarkerVisualKind::OtherTrackChange, point,
                (radius + 1.5f) / 0.64f, 0.0f, &track.color);
            if (active) draw_plan_small_text(draw, point, color, marker.label);
        }
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
            if (!point_near_canvas(p, origin, avail)) continue;
            const bool marker_hovered =
                hovered_station_row && st.row_index == *hovered_station_row;
            const bool marker_active =
                marker_emphasized(PlanMarkerKind::Station, st.row_index, marker_hovered);
            const ImU32 marker_color =
                map_marker_theme_color_u32(MapMarkerVisualKind::Station);
            draw_selected_marker_ring(
                p, PlanMarkerKind::Station, st.row_index, marker_color);
            draw_map_marker_icon(
                draw, MapMarkerVisualKind::Station, p,
                station_marker_radius / 0.46f * (marker_active ? 1.2f : 1.0f));
            if (!overview_marker_lod && show_station_names_) {
                draw->AddText(
                    ImVec2(p.x + station_label_offset, p.y - 16),
                    marker_color,
                    st.station.name.c_str());
            }
            if (!overview_marker_lod && show_station_mileage_) {
                draw->AddText(ImVec2(p.x + station_label_offset, p.y + 4),
                              IM_COL32(255, 216, 77, 255),
                              st.mileage_label.c_str());
            }
        }
    }

    if (show_speedlimits_) {
        for (const auto& sp : data.speedlimits) {
            ImVec2 p = transform.plan_to_screen(sp.x, sp.y);
            if (!point_near_canvas(p, origin, avail)) continue;
            const bool marker_hovered = hovered_speed_limit_row &&
                sp.row_index == *hovered_speed_limit_row;
            const bool marker_active = marker_emphasized(
                PlanMarkerKind::SpeedLimit, sp.row_index, marker_hovered);
            const ImU32 marker_color =
                map_marker_theme_color_u32(MapMarkerVisualKind::SpeedLimit);
            draw_selected_marker_ring(
                p, PlanMarkerKind::SpeedLimit, sp.row_index, marker_color);
            double wx = sp.x - std::sin(sp.theta);
            double wy = sp.y + std::cos(sp.theta);
            ImVec2 q = transform.plan_to_screen(wx, wy);
            ImVec2 d(q.x - p.x, q.y - p.y);
            float len = std::max(1.0f, std::sqrt(d.x * d.x + d.y * d.y));
            const float rotation = std::atan2(d.y / len, d.x / len);
            draw_map_marker_icon(
                draw, MapMarkerVisualKind::SpeedLimit, p,
                8.0f * marker_size_scale / 0.88f *
                    (marker_active ? 1.2f : 1.0f), rotation);
            if (!overview_marker_lod) {
                draw->AddText(
                    ImVec2(p.x + std::max(10.0f, 10.0f * marker_size_scale), p.y - 15),
                    marker_color,
                    sp.label.c_str());
            }
        }
    }

    if (show_curve_values_) {
        const ImU32 curve_color = IM_COL32(136, 255, 136, 255);
        const ImVec4 curve_theme = ImGui::ColorConvertU32ToFloat4(curve_color);
        for (const PlanCurveInterpolateMarker& marker : data.curve_interpolate_markers) {
            const ImVec2 p = transform.plan_to_screen(marker.x, marker.y);
            if (!point_near_canvas(p, origin, avail)) continue;
            const double wx = marker.x - std::sin(marker.theta);
            const double wy = marker.y + std::cos(marker.theta);
            const ImVec2 q = transform.plan_to_screen(wx, wy);
            const ImVec2 direction(q.x - p.x, q.y - p.y);
            const float length = std::max(
                1.0f, std::sqrt(direction.x * direction.x + direction.y * direction.y));
            const float rotation = std::atan2(direction.y / length, direction.x / length);
            draw_map_marker_icon(
                draw, MapMarkerVisualKind::SpeedLimit, p,
                8.0f * marker_size_scale / 0.88f, rotation, &curve_theme);
            if (!overview_marker_lod) {
                draw->AddText(
                    ImVec2(p.x + std::max(10.0f, 10.0f * marker_size_scale), p.y - 15),
                    curve_color, marker.label.c_str());
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

    draw_colored_marker_set(data.section_markers, PlanMarkerKind::Section,
                            hovered_section_row,
                            MapMarkerVisualKind::Section, 14.0f);
    debug_plan_stage("section_markers");

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

    if (!data.legacy_fog_markers.empty()) {
        for (const PlanMarker& marker : data.legacy_fog_markers) {
            const ImVec2 point = transform.plan_to_screen(marker.x, marker.y);
            if (!point_near_canvas(point, origin, avail)) continue;
            const double dx = static_cast<double>(point.x - mouse.x);
            const double dy = static_cast<double>(point.y - mouse.y);
            const bool marker_hovered =
                hovered_legacy_fog_row && *hovered_legacy_fog_row == marker.row_index &&
                dx * dx + dy * dy <= marker_hover_radius_sq;
            const bool marker_active = marker_emphasized(
                PlanMarkerKind::LegacyFog, marker.row_index, marker_hovered);
            const ImU32 color =
                map_marker_theme_color_u32(MapMarkerVisualKind::Fog);
            draw_selected_marker_ring(point, PlanMarkerKind::LegacyFog, marker.row_index, color);
            draw_map_marker_icon(
                draw, MapMarkerVisualKind::Fog, point,
                7.0f * marker_size_scale *
                    (marker_active ? 1.28f : 1.0f));
            if (marker_active) draw_plan_small_text(draw, point, color, marker.label);
        }
    }
    debug_plan_stage("legacy_fog_markers");

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
                draw->AddText(ImVec2(p.x + 8, p.y - 16),
                              IM_COL32(136, 255, 136, 255), sec.label.c_str());
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
    render_plan_marker_context_menu("plan_marker_context", plan_context_menu_entries_);
    ImGui::EndChild();
    debug_plan_stage("end");
}
