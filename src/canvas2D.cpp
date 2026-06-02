/*
 * Copyright (c) 2026 Sapporo_ningyo
 *
 * Licensed under Apache License 2.0; see LICENSE and NOTICE.
 * The GUI uses Dear ImGui and ImPlot; see THIRD_PARTY_NOTICES.md.
 */

#pragma execution_character_set("utf-8")

#include "kme.h"

#include "imgui.h"
#include "implot.h"

#include <windows.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>
#include <map>
#include <optional>
#include <sstream>
#include <string>
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

std::string normalize_track_key(std::string key) {
    key.erase(std::remove_if(key.begin(), key.end(), [](unsigned char ch) {
        return std::isspace(ch) != 0;
    }), key.end());
    for (char& ch : key) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return key;
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

int table_row_order(const TableRow& row) {
    return static_cast<int>(std::round(table_cell_number(row, "order")));
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

} // namespace

void App::rebuild_marker_overlay_cache() {
    structure_marker_cache_.clear();
    repeater_marker_cache_.clear();
    if (!has_model_ || model_.own.empty()) return;

    std::map<std::string, TrackSource> track_sources;
    TrackSource own_source{&model_.own, true};
    for (const char* key : {"", "0", "\\", "own", "main"}) {
        track_sources[normalize_track_key(key)] = own_source;
    }
    for (const auto& track : model_.other_tracks) {
        track_sources[normalize_track_key(track.key)] = TrackSource{&track.points, false};
    }

    auto find_track_source = [&](const std::string& key) -> std::optional<TrackSource> {
        auto it = track_sources.find(normalize_track_key(key));
        if (it == track_sources.end() || !it->second.points) return std::nullopt;
        return it->second;
    };

    auto sample_track = [&](const std::string& key, double distance, double lateral, double forward) -> std::optional<TrackPoint> {
        auto source = find_track_source(key);
        if (!source) return std::nullopt;
        auto sampled = sample_matrix_track_point(*source->points, distance, source->has_theta_column);
        if (!sampled) return std::nullopt;
        return offset_track_point(*sampled, lateral, forward);
    };

    auto make_marker = [](double distance, const TrackPoint& p, const std::string& label) {
        PlanStructureMarker marker;
        marker.d = distance;
        marker.x = p.x;
        marker.y = p.y;
        marker.label = label;
        return marker;
    };

    structure_marker_cache_.reserve(model_.structures.size() + model_.structures_between.size());
    for (const auto& row : model_.structures) {
        double distance = table_cell_number(row, "distance");
        double lateral = table_cell_number(row, "x");
        double forward = table_cell_number(row, "z");
        if (auto p = sample_track(table_cell(row, "trackKey"), distance, lateral, forward)) {
            structure_marker_cache_.push_back(make_marker(distance, *p, table_cell(row, "structureKey")));
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
        structure_marker_cache_.push_back(make_marker(distance, p, table_cell(row, "structureKey")));
    }

    auto build_repeater_segment = [&](const std::string& key, double start, double end,
                                      double lateral, double forward) -> PlanRepeaterSegment {
        PlanRepeaterSegment segment;
        if (end < start) std::swap(start, end);
        auto source = find_track_source(key);
        if (!source) return segment;

        auto append_at = [&](double distance) {
            auto sampled = sample_matrix_track_point(*source->points, distance, source->has_theta_column);
            if (!sampled) return;
            TrackPoint p = offset_track_point(*sampled, lateral, forward);
            if (!segment.points.empty() && std::abs(segment.points.back().d - p.d) < 1e-6) {
                segment.points.back() = p;
            } else {
                segment.points.push_back(p);
            }
        };

        append_at(start);
        for (size_t row_index = 0; row_index < source->points->rows; ++row_index) {
            double distance = source->points->at(row_index, 0);
            if (distance > start && distance < end) append_at(distance);
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
    auto make_repeater_marker = [](double distance, const TrackPoint& p, const std::string& label) {
        PlanRepeaterMarker marker;
        marker.d = distance;
        marker.x = p.x;
        marker.y = p.y;
        marker.label = label;
        return marker;
    };
    auto finish_repeater = [&](const RepeaterBeginState& begin, double end_distance, const std::string& label) {
        if (begin.row_index >= repeater_marker_cache_.size()) return;
        RepeaterOverlayRow& overlay = repeater_marker_cache_[begin.row_index];
        if (auto p = sample_track(begin.track_key, end_distance, begin.lateral, begin.forward)) {
            overlay.end_marker = make_repeater_marker(end_distance, *p, label);
        }
        overlay.segment = build_repeater_segment(begin.track_key, begin.distance, end_distance,
                                                 begin.lateral, begin.forward);
    };

    std::vector<TableRow> repeater_events = model_.repeaters;
    std::stable_sort(repeater_events.begin(), repeater_events.end(), [](const TableRow& a, const TableRow& b) {
        int ao = table_row_order(a);
        int bo = table_row_order(b);
        if (ao != bo) return ao < bo;
        return table_cell_number(a, "distance") < table_cell_number(b, "distance");
    });

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
                overlay.begin_marker = make_repeater_marker(distance, *p, key);
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
    for (size_t r = own_first; r < own_last; ++r) {
        double d = model_.own.at(r, 0);
        TrackPoint p;
        p.d = d;
        p.x = model_.own.at(r, 1);
        p.y = model_.own.at(r, 2);
        p.z = model_.own.at(r, 3);
        p.theta = model_.own.at(r, 4);
        p.radius = model_.own.at(r, 5);
        p.gradient = model_.own.at(r, 6);
        out.own.push_back(p);
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
            PlanOther po;
            po.key = t.key;
            po.color = t.color;
            double rmin = std::max(dmin_, t.range_min);
            double rmax = std::min(dmax_, t.range_max);
            size_t first = matrix_lower_bound_distance(t.points, rmin);
            size_t last = matrix_upper_bound_distance(t.points, rmax);
            po.points.reserve(last > first ? last - first : 0);
            for (size_t r = first; r < last; ++r) {
                TrackPoint p;
                p.d = t.points.at(r, 0);
                p.x = t.points.at(r, 1);
                p.y = t.points.at(r, 2);
                p.z = t.points.at(r, 3);
                ImVec2 q = rotate_xy(p.x, p.y, angle);
                p.x = q.x;
                p.y = q.y;
                po.points.push_back(p);
                extend_bounds(p.x, p.y);
            }
            if (!po.points.empty()) out.other.push_back(std::move(po));
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
    for (size_t i = 0; i < structure_marker_cache_.size() && i < structure_row_visible_.size(); ++i) {
        if (!structure_row_visible_[i] || !structure_marker_cache_[i]) continue;
        const PlanStructureMarker& source = *structure_marker_cache_[i];
        if (source.d < dmin_ || source.d > dmax_) continue;
        TrackPoint p;
        p.x = source.x;
        p.y = source.y;
        p = rotate_point(p);
        out.structure_markers.push_back({source.d, p.x, p.y, source.label});
        append_marker_bounds(p.x, p.y);
    }

    auto append_repeater_marker = [&](const PlanRepeaterMarker& source) {
        if (source.d < dmin_ || source.d > dmax_) return;
        TrackPoint p;
        p.x = source.x;
        p.y = source.y;
        p = rotate_point(p);
        out.repeater_markers.push_back({source.d, p.x, p.y, source.label});
        append_marker_bounds(p.x, p.y);
    };
    for (size_t i = 0; i < repeater_marker_cache_.size() && i < repeater_row_visible_.size(); ++i) {
        if (!repeater_row_visible_[i]) continue;
        const RepeaterOverlayRow& source = repeater_marker_cache_[i];
        if (source.begin_marker) append_repeater_marker(*source.begin_marker);
        if (source.end_marker) append_repeater_marker(*source.end_marker);

        PlanRepeaterSegment segment;
        for (const auto& point : source.segment.points) {
            if (point.d < dmin_ || point.d > dmax_) continue;
            TrackPoint p = rotate_point(point);
            segment.points.push_back(p);
            append_marker_bounds(p.x, p.y);
        }
        if (segment.points.size() >= 2) out.repeater_segments.push_back(std::move(segment));
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
    for (size_t i = first; i < last; ++i) {
        builder.append(transform.plan_to_screen(points[i].x, points[i].y));
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
    for (size_t row = first; row < last; ++row) {
        builder.append(transform.model_to_screen(points.at(row, 1), points.at(row, 2)));
    }
    builder.finish();
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

static void draw_plan_triangle_marker(ImDrawList* draw, ImVec2 p, ImU32 color) {
    const float r = 6.0f;
    ImVec2 pts[3] = {
        ImVec2(p.x, p.y - r),
        ImVec2(p.x - r * 0.9f, p.y + r * 0.72f),
        ImVec2(p.x + r * 0.9f, p.y + r * 0.72f),
    };
    draw->AddConvexPolyFilled(pts, IM_ARRAYSIZE(pts), color);
    draw->AddPolyline(pts, IM_ARRAYSIZE(pts), IM_COL32(64, 48, 0, 255), ImDrawFlags_Closed, 1.0f);
}

static void draw_plan_diamond_marker(ImDrawList* draw, ImVec2 p, ImU32 color) {
    const float r = 5.5f;
    ImVec2 pts[4] = {
        ImVec2(p.x, p.y - r),
        ImVec2(p.x + r, p.y),
        ImVec2(p.x, p.y + r),
        ImVec2(p.x - r, p.y),
    };
    draw->AddConvexPolyFilled(pts, IM_ARRAYSIZE(pts), color);
    draw->AddPolyline(pts, IM_ARRAYSIZE(pts), IM_COL32(80, 0, 48, 255), ImDrawFlags_Closed, 1.0f);
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

static void draw_plan_small_text(ImDrawList* draw, ImVec2 p, ImU32 color, const std::string& text) {
    if (text.empty()) return;
    draw->AddText(nullptr, ImGui::GetFontSize() * 0.78f, ImVec2(p.x + 8.0f, p.y - 9.0f), color, text.c_str());
}

static bool point_near_canvas(ImVec2 p, ImVec2 origin, ImVec2 size, float margin = 48.0f) {
    return p.x >= origin.x - margin && p.x <= origin.x + size.x + margin &&
           p.y >= origin.y - margin && p.y <= origin.y + size.y + margin;
}

void App::render_plan_canvas(ImVec2 size) {
    PlanData data = build_plan_data(false);
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

    bool rotate_plan = hovered && (ImGui::IsMouseDown(ImGuiMouseButton_Right) ||
                                   (io.KeyCtrl && ImGui::IsMouseDown(ImGuiMouseButton_Left)));
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
            show_align_popup_ = true;
        } else if (pick_slot_ == 2) {
            if (auto uv = background_uv_from_world(world)) align_pick2_ = *uv;
            pick_slot_ = 0;
            show_align_popup_ = true;
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

    if (grid_mode_ == GridMode::Fixed) {
        for (float x = origin.x; x <= origin.x + avail.x; x += 80.0f) draw->AddLine(ImVec2(x, origin.y), ImVec2(x, origin.y + avail.y), IM_COL32(48, 52, 58, 255));
        for (float y = origin.y; y <= origin.y + avail.y; y += 80.0f) draw->AddLine(ImVec2(origin.x, y), ImVec2(origin.x + avail.x, y), IM_COL32(48, 52, 58, 255));
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
            draw->AddLine(a, b, IM_COL32(48, 52, 58, 255));
        }
        for (double y = ymin; y <= ymax; y += step) {
            ImVec2 a = plan_view_.world_to_screen(xmin, y, origin, avail);
            ImVec2 b = plan_view_.world_to_screen(xmax, y, origin, avail);
            draw->AddLine(a, b, IM_COL32(48, 52, 58, 255));
        }
    }

    draw_background(draw, plan_view_, origin, avail);

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
        for (const auto& s : data.curve_sections) draw_section(s, IM_COL32(130, 130, 130, 220), 10.0f);
        for (const auto& s : data.transition_sections) draw_section(s, IM_COL32(84, 84, 84, 220), 8.0f);
    }
    draw_polyline(draw, data.own, transform, origin, avail, IM_COL32(245, 245, 245, 255), 2.0f);
    for (const auto& t : model_.other_tracks) {
        if (!t.visible || t.points.empty()) continue;
        double rmin = std::max(dmin_, t.range_min);
        double rmax = std::min(dmax_, t.range_max);
        draw_matrix_plan_polyline(draw, t.points, rmin, rmax, transform, origin, avail, color_u32(t.color), 1.5f);
    }

    if (show_stations_) {
        for (const auto& st : data.stations) {
            ImVec2 p = transform.plan_to_screen(st.x, st.y);
            draw->AddCircleFilled(p, station_marker_size_, IM_COL32(255, 255, 255, 255));
            if (show_station_names_) draw->AddText(ImVec2(p.x + 8, p.y - 16), IM_COL32(255, 255, 255, 255), st.station.name.c_str());
            if (show_station_mileage_) draw->AddText(ImVec2(p.x + 8, p.y + 4), IM_COL32(255, 216, 77, 255), (format_double(st.station.mileage, 0) + "m").c_str());
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
            d.x = d.x / len * 8.0f;
            d.y = d.y / len * 8.0f;
            draw->AddLine(ImVec2(p.x - d.x, p.y - d.y), ImVec2(p.x + d.x, p.y + d.y), IM_COL32(136, 204, 255, 255), 1.0f);
            std::string label = sp.has_speed ? format_double(sp.speed, 0) : "x";
            draw->AddText(ImVec2(p.x + 10, p.y - 15), IM_COL32(136, 204, 255, 255), label.c_str());
        }
    }

    if (!data.repeater_segments.empty() || !data.repeater_markers.empty()) {
        ImU32 repeater_color = IM_COL32(255, 105, 190, 255);
        for (const auto& segment : data.repeater_segments) {
            draw_polyline(draw, segment.points, transform, origin, avail, repeater_color, 1.25f);
        }
        for (const auto& marker : data.repeater_markers) {
            ImVec2 p = transform.plan_to_screen(marker.x, marker.y);
            if (!point_near_canvas(p, origin, avail)) continue;
            draw_plan_diamond_marker(draw, p, repeater_color);
            draw_plan_small_text(draw, p, repeater_color, marker.label);
        }
    }

    if (!data.structure_markers.empty()) {
        ImU32 structure_color = IM_COL32(255, 216, 48, 255);
        for (const auto& marker : data.structure_markers) {
            ImVec2 p = transform.plan_to_screen(marker.x, marker.y);
            if (!point_near_canvas(p, origin, avail)) continue;
            draw_plan_triangle_marker(draw, p, structure_color);
            draw_plan_small_text(draw, p, structure_color, marker.label);
        }
    }

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

    draw->AddText(ImVec2(origin.x + 8, origin.y + 8), IM_COL32(255, 255, 255, 255), tr("canvas.plan").c_str());
    draw_scalebar(draw, plan_view_, origin, avail);
    if (!data.own.empty()) plan_canvas_rendered_this_frame_ = true;
    draw->PopClipRect();
    ImGui::EndChild();
}

static void plot_line_vec(const char* label, const std::vector<double>& x, const std::vector<double>& y, ImVec4 color, float weight = 1.5f) {
    if (x.size() < 2 || y.size() < 2) return;
    ImPlotSpec spec;
    spec.LineColor = color;
    spec.LineWeight = weight;
    ImPlot::PlotLine(label, x.data(), y.data(), static_cast<int>(std::min(x.size(), y.size())), spec);
}

static void draw_plot_overlay_labels(const std::string& title, const std::string& unit) {
    ImDrawList* draw = ImGui::GetWindowDrawList();
    ImVec2 pos = ImPlot::GetPlotPos();
    ImVec2 size = ImPlot::GetPlotSize();
    ImU32 color = ImGui::GetColorU32(ImGuiCol_Text);
    draw->AddText(ImVec2(pos.x + 8.0f, pos.y + 6.0f), color, title.c_str());
    ImVec2 unit_size = ImGui::CalcTextSize(unit.c_str());
    draw->AddText(ImVec2(pos.x - unit_size.x - 8.0f, pos.y + size.y + 6.0f), color, unit.c_str());
}

static void mask_plot_axis_tick_edges(ImVec2 frame_min, ImVec2 frame_max, ImVec2 plot_pos, ImVec2 plot_size) {
    ImVec2 plot_max(plot_pos.x + plot_size.x, plot_pos.y + plot_size.y);
    if (plot_size.x <= 0.0f || plot_size.y <= 0.0f) return;

    ImU32 bg = ImGui::GetColorU32(ImGuiCol_WindowBg);
    ImDrawList* draw = ImGui::GetWindowDrawList();
    float bleed = ImGui::GetFontSize() * 1.6f;
    ImVec2 outer_min(frame_min.x - bleed, frame_min.y - bleed);
    ImVec2 outer_max(frame_max.x + bleed, frame_max.y + bleed);
    auto fill = [&](ImVec2 min, ImVec2 max) {
        if (max.x > min.x && max.y > min.y) draw->AddRectFilled(min, max, bg);
    };

    fill(outer_min, plot_pos);
    fill(ImVec2(plot_max.x, outer_min.y), ImVec2(outer_max.x, plot_pos.y));
    fill(ImVec2(outer_min.x, plot_max.y), ImVec2(plot_pos.x, outer_max.y));
    fill(plot_max, outer_max);
}

static void draw_radius_side_markers() {
    ImDrawList* draw = ImGui::GetWindowDrawList();
    ImVec2 pos = ImPlot::GetPlotPos();
    ImVec2 size = ImPlot::GetPlotSize();
    ImVec2 clip_max(pos.x + size.x, pos.y + size.y);
    ImVec2 zero = ImPlot::PlotToPixels(ImPlot::GetPlotLimits().X.Min, 0.0);
    if (!std::isfinite(zero.y)) return;

    ImU32 color = ImGui::GetColorU32(ImGuiCol_Text);
    ImVec2 r_size = ImGui::CalcTextSize("R");
    ImVec2 l_size = ImGui::CalcTextSize("L");
    float x = pos.x - std::max(r_size.x, l_size.x) - 4.0f;
    float gap = 3.0f;
    float r_y = std::clamp(zero.y - r_size.y - gap, pos.y + 2.0f, clip_max.y - r_size.y - 2.0f);
    float l_y = std::clamp(zero.y + gap, pos.y + 2.0f, clip_max.y - l_size.y - 2.0f);

    draw->AddText(ImVec2(x, r_y), color, "R");
    draw->AddText(ImVec2(x, l_y), color, "L");
}

static std::optional<std::pair<double, double>> plot_x_wheel_zoom_limits(const ImPlotRect& limits) {
    ImGuiIO& io = ImGui::GetIO();
    if (io.MouseWheel == 0.0f || !ImPlot::IsPlotHovered()) return std::nullopt;
    double min_x = limits.X.Min;
    double max_x = limits.X.Max;
    double span = max_x - min_x;
    if (!std::isfinite(span) || span <= 1e-9) return std::nullopt;

    ImVec2 pos = ImPlot::GetPlotPos();
    ImVec2 size = ImPlot::GetPlotSize();
    if (size.x <= 1.0f) return std::nullopt;
    float tx = std::clamp((io.MousePos.x - pos.x) / size.x, 0.0f, 1.0f);

    float zoom_rate = 0.1f;
    if (io.MouseWheel > 0.0f) {
        zoom_rate = (-zoom_rate) / (1.0f + (2.0f * zoom_rate));
    }
    double new_min = min_x - span * tx * zoom_rate;
    double new_max = max_x + span * (1.0f - tx) * zoom_rate;
    if (!std::isfinite(new_min) || !std::isfinite(new_max) || new_max <= new_min) return std::nullopt;
    return std::make_pair(new_min, new_max);
}

static void draw_bottom_locked_plot_labels(const std::vector<LabelPoint>& labels) {
    ImDrawList* draw = ImPlot::GetPlotDrawList();
    ImVec2 pos = ImPlot::GetPlotPos();
    ImVec2 size = ImPlot::GetPlotSize();
    ImVec2 clip_max(pos.x + size.x, pos.y + size.y);
    ImU32 color = ImGui::GetColorU32(ImGuiCol_Text);
    draw->PushClipRect(pos, clip_max, true);
    for (const auto& label : labels) {
        ImVec2 p = ImPlot::PlotToPixels(label.x, 0.0);
        if (p.x < pos.x || p.x > clip_max.x) continue;
        ImVec2 text_size = ImGui::CalcTextSize(label.text.c_str());
        draw->AddText(ImVec2(p.x + 6.0f - text_size.x, clip_max.y - text_size.y - 6.0f), color, label.text.c_str());
    }
    draw->PopClipRect();
}

enum class FixedPlotY {
    Top,
    Bottom
};

static std::string station_mileage_text(const Station& station) {
    return format_double(station.mileage, 0) + "m";
}

static void draw_fixed_y_plot_text(double x, const std::string& text, ImU32 color, FixedPlotY fixed_y) {
    if (text.empty()) return;
    ImDrawList* draw = ImPlot::GetPlotDrawList();
    ImVec2 pos = ImPlot::GetPlotPos();
    ImVec2 size = ImPlot::GetPlotSize();
    ImVec2 clip_max(pos.x + size.x, pos.y + size.y);
    ImVec2 p = ImPlot::PlotToPixels(x, 0.0);
    if (!std::isfinite(p.x) || p.x < pos.x || p.x > clip_max.x) return;

    ImVec2 text_size = ImGui::CalcTextSize(text.c_str());
    float y = fixed_y == FixedPlotY::Top ? pos.y + 8.0f : clip_max.y - text_size.y - 8.0f;
    draw->PushClipRect(pos, clip_max, true);
    draw->AddText(ImVec2(p.x + 8.0f, y), color, text.c_str());
    draw->PopClipRect();
}

static void draw_plot_point_right_text(double x, double y, const std::string& text, ImU32 color) {
    if (text.empty()) return;
    ImDrawList* draw = ImPlot::GetPlotDrawList();
    ImVec2 pos = ImPlot::GetPlotPos();
    ImVec2 size = ImPlot::GetPlotSize();
    ImVec2 clip_max(pos.x + size.x, pos.y + size.y);
    ImVec2 p = ImPlot::PlotToPixels(x, y);
    if (!std::isfinite(p.x) || !std::isfinite(p.y) || p.x < pos.x || p.x > clip_max.x) return;

    draw->PushClipRect(pos, clip_max, true);
    draw->AddText(ImVec2(p.x + 8.0f, p.y - 26.0f), color, text.c_str());
    draw->PopClipRect();
}

enum class ProfileMarkerDirection {
    Up,
    Down
};

static void draw_profile_vertical_marker(double x, double track_y, ProfileMarkerDirection direction,
                                         ImU32 line_color, float line_weight, bool draw_station_marker,
                                         float station_marker_size = kDefaultStationMarkerSize) {
    ImDrawList* draw = ImPlot::GetPlotDrawList();
    ImVec2 pos = ImPlot::GetPlotPos();
    ImVec2 size = ImPlot::GetPlotSize();
    ImVec2 clip_min = pos;
    ImVec2 clip_max(pos.x + size.x, pos.y + size.y);
    ImVec2 p = ImPlot::PlotToPixels(x, track_y);
    if (!std::isfinite(p.x) || !std::isfinite(p.y) || p.x < clip_min.x || p.x > clip_max.x) return;

    float unclipped_a = direction == ProfileMarkerDirection::Up ? clip_min.y : p.y;
    float unclipped_b = direction == ProfileMarkerDirection::Up ? p.y : clip_max.y;
    float line_min = std::max(std::min(unclipped_a, unclipped_b), clip_min.y);
    float line_max = std::min(std::max(unclipped_a, unclipped_b), clip_max.y);

    draw->PushClipRect(clip_min, clip_max, true);
    if (line_max > line_min) {
        draw->AddLine(ImVec2(p.x, line_min), ImVec2(p.x, line_max), line_color, line_weight);
    }
    if (draw_station_marker) {
        float radius = clamp_station_marker_size(station_marker_size);
        float outline_weight = std::max(1.0f, radius * 0.375f);
        draw->AddCircleFilled(p, radius, IM_COL32(0, 0, 0, 255));
        draw->AddCircle(p, radius, IM_COL32(255, 255, 255, 255), 0, outline_weight);
    }
    draw->PopClipRect();
}

class ScopedImPlotFitButton {
public:
    explicit ScopedImPlotFitButton(bool disabled) : active_(disabled) {
        if (!active_) return;
        ImPlotInputMap& input = ImPlot::GetInputMap();
        old_fit_ = input.Fit;
        // Extra mouse slot 3 is not enabled by ImPlot's plot button flags.
        input.Fit = 3;
    }

    ~ScopedImPlotFitButton() {
        if (active_) ImPlot::GetInputMap().Fit = old_fit_;
    }

private:
    bool active_ = false;
    ImGuiMouseButton old_fit_ = ImGuiMouseButton_Left;
};

class ScopedImPlotWheelZoomDisabled {
public:
    explicit ScopedImPlotWheelZoomDisabled(bool active) : active_(active) {
        if (!active_) return;
        ImPlotInputMap& input = ImPlot::GetInputMap();
        old_zoom_rate_ = input.ZoomRate;
        input.ZoomRate = 0.0f;
    }

    ~ScopedImPlotWheelZoomDisabled() {
        if (active_) ImPlot::GetInputMap().ZoomRate = old_zoom_rate_;
    }

private:
    bool active_ = false;
    float old_zoom_rate_ = 0.0f;
};

static bool point_in_rect(ImVec2 p, ImVec2 pos, ImVec2 size) {
    return p.x >= pos.x && p.x <= pos.x + size.x && p.y >= pos.y && p.y <= pos.y + size.y;
}

static double preserved_plot_span(double current_span, double fallback_min, double fallback_max) {
    if (std::isfinite(current_span) && current_span > 1e-6) return current_span;
    double fallback = fallback_max - fallback_min;
    if (std::isfinite(fallback) && fallback > 1e-6) return fallback;
    return 1000.0;
}

void App::render_profile_plot(const ProfileData& data, ImVec2 size) {
    if (!show_profile_graph_) return;
    ScopedImPlotFitButton disable_fit(mode_ == Mode::Measure);
    ImGuiIO& io = ImGui::GetIO();
    bool mouse_in_profile_plot = profile_plot_rect_valid_ && point_in_rect(io.MousePos, profile_plot_pos_, profile_plot_size_);
    ScopedImPlotWheelZoomDisabled disable_default_wheel_zoom(mouse_in_profile_plot);
    ImPlot::PushStyleVar(ImPlotStyleVar_PlotPadding, ImVec2(4.0f, 4.0f));
    ImPlot::PushStyleVar(ImPlotStyleVar_LabelPadding, ImVec2(2.0f, 2.0f));
    bool consumed_profile_x_zoom = false;
    if (ImPlot::BeginPlot("##ProfilePlot", size, ImPlotFlags_NoTitle | ImPlotFlags_NoLegend)) {
        ImVec2 frame_min = ImGui::GetItemRectMin();
        ImVec2 frame_max = ImGui::GetItemRectMax();
        ImPlot::SetupAxes(nullptr, nullptr, ImPlotAxisFlags_NoLabel, ImPlotAxisFlags_NoLabel);
        ImPlotCond reset_cond = reset_profile_axes_next_ ? ImPlotCond_Always : ImPlotCond_Once;
        if (focus_profile_next_) {
            double span = preserved_plot_span(profile_x_span_, dmin_, dmax_);
            ImPlot::SetupAxisLimits(ImAxis_X1, focus_profile_distance_ - span * 0.5, focus_profile_distance_ + span * 0.5, ImPlotCond_Always);
            profile_x_zoom_pending_ = false;
        } else if (!reset_profile_axes_next_ && profile_x_zoom_pending_) {
            ImPlot::SetupAxisLimits(ImAxis_X1, profile_x_zoom_min_, profile_x_zoom_max_, ImPlotCond_Always);
            consumed_profile_x_zoom = true;
        } else {
            ImPlot::SetupAxisLimits(ImAxis_X1, dmin_, dmax_, reset_cond);
            if (reset_profile_axes_next_) profile_x_zoom_pending_ = false;
        }
        ImPlot::SetupAxisLimits(ImAxis_Y1, data.ymin, data.ymax, reset_cond);
        plot_line_vec("Own", data.own_x, data.own_y, ImVec4(1, 1, 1, 1), 2.0f);
        for (const auto& t : data.other) plot_line_vec(t.key.c_str(), t.x, t.y, t.color, 1.2f);
        if (show_gradient_pos_) {
            for (const auto& p : data.gradient_points) {
                draw_profile_vertical_marker(p.x, p.y, ProfileMarkerDirection::Down,
                                             IM_COL32(255, 255, 255, 140), 1.0f, false);
            }
            if (show_gradient_values_) {
                draw_bottom_locked_plot_labels(data.gradient_labels);
            }
        }
        if (show_stations_) {
            for (const auto& s : data.stations) {
                double x = s.distance;
                double y = s.z - model_.height_origin;
                draw_profile_vertical_marker(x, y, ProfileMarkerDirection::Up,
                                             IM_COL32(255, 255, 255, 191), 1.0f, true, station_marker_size_);
                if (show_station_names_) draw_plot_point_right_text(x, y, s.name, IM_COL32(255, 255, 255, 255));
                if (show_station_mileage_) draw_fixed_y_plot_text(x, station_mileage_text(s), IM_COL32(255, 216, 77, 255), FixedPlotY::Top);
            }
        }
        if (mode_ == Mode::Measure && ImPlot::IsPlotHovered()) {
            set_crosshair_cursor();
            ImPlotPoint p = ImPlot::GetPlotMousePos();
            if (p.x >= dmin_ && p.x <= dmax_) update_measure(p.x);
        }
        handle_measure_plot_double_click(false, true);
        if (mode_ == Mode::Measure && measure_distance_) {
            double x = *measure_distance_;
            ImPlot::PlotInfLines("##measure_profile", &x, 1, {ImPlotProp_LineColor, ImVec4(1, 0.2f, 0.2f, 1), ImPlotProp_LineWeight, 2.0f, ImPlotProp_Flags, ImPlotItemFlags_NoLegend});
        }
        ImPlotRect limits = ImPlot::GetPlotLimits();
        profile_x_span_ = std::abs(limits.X.Size());
        profile_plot_pos_ = ImPlot::GetPlotPos();
        profile_plot_size_ = ImPlot::GetPlotSize();
        profile_plot_rect_valid_ = profile_plot_size_.x > 0.0f && profile_plot_size_.y > 0.0f;
        if (auto zoom_limits = plot_x_wheel_zoom_limits(limits)) {
            profile_x_zoom_min_ = zoom_limits->first;
            profile_x_zoom_max_ = zoom_limits->second;
            profile_x_zoom_pending_ = true;
        } else if (consumed_profile_x_zoom) {
            profile_x_zoom_pending_ = false;
        }
        ImVec2 plot_pos = profile_plot_pos_;
        ImVec2 plot_size = profile_plot_size_;
        mask_plot_axis_tick_edges(frame_min, frame_max, plot_pos, plot_size);
        draw_plot_overlay_labels(tr("plot.profile"), tr("unit.m"));
        focus_profile_next_ = false;
        reset_profile_axes_next_ = false;
        ImPlot::EndPlot();
    }
    ImPlot::PopStyleVar(2);
}

void App::render_radius_plot(const ProfileData& data, ImVec2 size) {
    if (!show_radius_graph_) return;
    ScopedImPlotFitButton disable_fit(mode_ == Mode::Measure);
    ImPlot::PushStyleVar(ImPlotStyleVar_PlotPadding, ImVec2(4.0f, 4.0f));
    ImPlot::PushStyleVar(ImPlotStyleVar_LabelPadding, ImVec2(2.0f, 2.0f));
    if (ImPlot::BeginPlot("##RadiusPlot", size, ImPlotFlags_NoTitle | ImPlotFlags_NoLegend)) {
        ImVec2 frame_min = ImGui::GetItemRectMin();
        ImVec2 frame_max = ImGui::GetItemRectMax();
        ImPlot::SetupAxis(ImAxis_X1, nullptr, ImPlotAxisFlags_NoLabel);
        ImPlot::SetupAxis(ImAxis_Y1, nullptr, ImPlotAxisFlags_NoLabel | ImPlotAxisFlags_NoTickMarks | ImPlotAxisFlags_NoTickLabels | ImPlotAxisFlags_NoHighlight | ImPlotAxisFlags_Lock);
        ImPlotCond reset_cond = reset_radius_axes_next_ ? ImPlotCond_Always : ImPlotCond_Once;
        if (focus_radius_next_) {
            double span = preserved_plot_span(radius_x_span_, dmin_, dmax_);
            ImPlot::SetupAxisLimits(ImAxis_X1, focus_radius_distance_ - span * 0.5, focus_radius_distance_ + span * 0.5, ImPlotCond_Always);
        } else {
            ImPlot::SetupAxisLimits(ImAxis_X1, dmin_, dmax_, reset_cond);
        }
        ImPlot::SetupAxisLimits(ImAxis_Y1, -2.2, 2.2, ImPlotCond_Always);
        plot_line_vec("RadiusSign", data.curve_x, data.curve_y, ImVec4(1, 1, 1, 1), 2.0f);
        for (const auto& label : data.radius_labels) ImPlot::PlotText(label.text.c_str(), label.x, label.y, ImVec2(-6, 0), {ImPlotProp_Flags, ImPlotTextFlags_Vertical});
        if (show_stations_) {
            for (const auto& s : data.stations) {
                double x = s.distance;
                ImPlot::PlotInfLines(("##rst" + s.key).c_str(), &x, 1, {ImPlotProp_LineColor, ImVec4(1, 1, 1, 0.55f), ImPlotProp_Flags, ImPlotItemFlags_NoLegend});
                if (show_station_names_) draw_fixed_y_plot_text(x, s.name, IM_COL32(255, 255, 255, 255), FixedPlotY::Top);
                if (show_station_mileage_) draw_fixed_y_plot_text(x, station_mileage_text(s), IM_COL32(255, 216, 77, 255), FixedPlotY::Bottom);
            }
        }
        if (mode_ == Mode::Measure && ImPlot::IsPlotHovered()) {
            set_crosshair_cursor();
            ImPlotPoint p = ImPlot::GetPlotMousePos();
            if (p.x >= dmin_ && p.x <= dmax_) update_measure(p.x);
        }
        handle_measure_plot_double_click(true, false);
        if (mode_ == Mode::Measure && measure_distance_) {
            double x = *measure_distance_;
            ImPlot::PlotInfLines("##measure_radius", &x, 1, {ImPlotProp_LineColor, ImVec4(1, 0.2f, 0.2f, 1), ImPlotProp_LineWeight, 2.0f, ImPlotProp_Flags, ImPlotItemFlags_NoLegend});
        }
        ImPlotRect limits = ImPlot::GetPlotLimits();
        radius_x_span_ = std::abs(limits.X.Size());
        ImVec2 plot_pos = ImPlot::GetPlotPos();
        ImVec2 plot_size = ImPlot::GetPlotSize();
        mask_plot_axis_tick_edges(frame_min, frame_max, plot_pos, plot_size);
        draw_radius_side_markers();
        draw_plot_overlay_labels(tr("plot.radius"), tr("unit.m"));
        focus_radius_next_ = false;
        reset_radius_axes_next_ = false;
        ImPlot::EndPlot();
    }
    ImPlot::PopStyleVar(2);
}

void App::render_plots() {
    if (!show_plots_window_) return;
    std::string title = tr("frame.plots") + "###Plots";
    if (dock_main_id_) ImGui::SetNextWindowDockID(dock_main_id_, ImGuiCond_FirstUseEver);
    if (focus_plots_next_) ImGui::SetNextWindowFocus();
    if (!ImGui::Begin(title.c_str(), &show_plots_window_)) {
        focus_plots_next_ = false;
        ImGui::End();
        return;
    }
    render_mode_grid_controls();
    if (pick_slot_ != 0) ImGui::TextUnformatted(tr("hint.pick_bg_station").c_str());
    else if (mode_ == Mode::Measure && !measure_text_.empty()) ImGui::TextUnformatted(measure_text_.c_str());
    else ImGui::TextDisabled("%s", has_model_ ? "" : tr("status.no_map").c_str());

    ImVec2 avail = ImGui::GetContentRegionAvail();
    float splitter_h = 6.0f;
    float plan_h = avail.y;
    bool any_graph = show_profile_graph_ || show_radius_graph_;
    if (any_graph) plan_h = std::max(80.0f, avail.y * static_cast<float>(plan_height_) - splitter_h);
    render_plan_canvas(ImVec2(avail.x, plan_h));

    if (any_graph) {
        ImGui::InvisibleButton("##vsplit", ImVec2(avail.x, splitter_h));
        if (ImGui::IsItemActive()) {
            plan_height_ += ImGui::GetIO().MouseDelta.y / std::max(1.0f, avail.y);
            plan_height_ = std::clamp(plan_height_, 0.2, 0.86);
        }
        ProfileData profile = build_profile_data();
        ImVec2 graph_avail = ImGui::GetContentRegionAvail();
        if (show_profile_graph_ && show_radius_graph_) {
            float left_w = graph_avail.x * static_cast<float>(graph_split_);
            render_profile_plot(profile, ImVec2(left_w - 3.0f, graph_avail.y));
            ImGui::SameLine();
            ImGui::InvisibleButton("##hsplit", ImVec2(6.0f, graph_avail.y));
            if (ImGui::IsItemActive()) {
                graph_split_ += ImGui::GetIO().MouseDelta.x / std::max(1.0f, graph_avail.x);
                graph_split_ = std::clamp(graph_split_, 0.2, 0.8);
            }
            ImGui::SameLine();
            render_radius_plot(profile, ImVec2(-1.0f, graph_avail.y));
        } else if (show_profile_graph_) {
            render_profile_plot(profile, graph_avail);
        } else if (show_radius_graph_) {
            render_radius_plot(profile, graph_avail);
        }
    }
    focus_plots_next_ = false;
    ImGui::End();
}
