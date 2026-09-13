/*
 * Copyright (c) 2026 Sapporo_ningyo
 *
 * Licensed under Apache License 2.0; see LICENSE and NOTICE.
 */

#ifdef _MSC_VER
#pragma execution_character_set("utf-8")
#endif

#include "canvas3d_scene_data.h"
#include "canvas3d_scene_geometry.h"
#include "kme.h"
#include "operation_timing.h"
#include "numeric_safety.h"
#include "repeater_linkage.h"
#include "scene_route_overlay.h"
#include "maploader.h"
#include "imgui.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <map>
#include <initializer_list>
#include <limits>
#include <optional>
#include <string>
#include <utility>
#include <vector>

using namespace canvas3d_detail;

namespace canvas3d_detail {

constexpr double k_default_scene_fog_color = 0.875;

} // namespace canvas3d_detail

namespace canvas3d_detail {

constexpr double k_default_scene_fog_density = 0.001;

} // namespace canvas3d_detail

namespace canvas3d_detail {

struct SceneTrackBufferView {
    const double* data = nullptr;
    size_t rows = 0;
    size_t cols = 0;

    double at(size_t row, size_t col) const {
        return data[row * cols + col];
    }

    bool empty() const {
        return rows == 0 || cols == 0;
    }
};

static std::optional<SceneTrackBufferView> scene_track_buffer_view(const Matrix& points) {
    if (points.rows == 0) return SceneTrackBufferView{nullptr, 0, points.cols};
    if (points.cols == 0 || points.rows > std::numeric_limits<size_t>::max() / points.cols) {
        return std::nullopt;
    }
    if (points.data.size() < points.rows * points.cols) return std::nullopt;
    return SceneTrackBufferView{points.data.data(), points.rows, points.cols};
}

static std::optional<SceneTrackBufferView> scene_track_buffer_view(KvDoubleBuffer points) {
    constexpr std::uint64_t max_size = static_cast<std::uint64_t>(std::numeric_limits<size_t>::max());
    if (points.rows > max_size || points.cols > max_size) return std::nullopt;
    const size_t rows = static_cast<size_t>(points.rows);
    const size_t cols = static_cast<size_t>(points.cols);
    if (rows == 0) return SceneTrackBufferView{nullptr, 0, cols};
    if (!points.data || cols == 0 || rows > std::numeric_limits<size_t>::max() / cols) {
        return std::nullopt;
    }
    return SceneTrackBufferView{points.data, rows, cols};
}

static Canvas3DTrackPoint scene_track_row_point(const SceneTrackBufferView& points, size_t row,
                                         bool has_theta_column) {
    constexpr double default_gauge = 1067.0;
    auto cant_angle = [default_gauge](double cant, double gauge) {
        if (!std::isfinite(cant)) return 0.0;
        double effective_gauge = std::abs(gauge) > 1e-9 && std::isfinite(gauge) ? std::abs(gauge) : default_gauge;
        return std::asin(std::clamp(cant / effective_gauge, -1.0, 1.0));
    };

    Canvas3DTrackPoint p;
    p.distance = points.at(row, 0);
    p.x = points.at(row, 2);
    p.z = -points.at(row, 1);
    p.y = points.cols > 3 ? points.at(row, 3) : 0.0;
    p.theta = has_theta_column && points.cols > 4
        ? points.at(row, 4)
        : track_buffer_tangent(points, row);
    p.gradient = has_theta_column && points.cols > 6 ? points.at(row, 6) : 0.0;
    if (has_theta_column) {
        double cant = points.cols > 8 ? points.at(row, 8) : 0.0;
        double gauge = points.cols > 10 ? points.at(row, 10) : default_gauge;
        p.cant_angle = cant_angle(cant, gauge);
    } else {
        double cant = points.cols > 5 ? points.at(row, 5) : 0.0;
        double gauge = points.cols > 7 ? points.at(row, 7) : default_gauge;
        p.cant_angle = cant_angle(cant, gauge);
    }
    return p;
}

static std::vector<std::string> scene_split_key_list(const std::string& text) {
    std::vector<std::string> keys;
    std::string current;
    for (char ch : text) {
        if (ch == ',' || ch == ';' || std::isspace(static_cast<unsigned char>(ch))) {
            std::string key = trim_ascii(current);
            if (!key.empty()) keys.push_back(key);
            current.clear();
        } else {
            current.push_back(ch);
        }
    }
    std::string key = trim_ascii(current);
    if (!key.empty()) keys.push_back(key);
    return keys;
}
static void append_scene_route_value_event(
    std::vector<route_value_sampling::Event>& events,
    const TrackEvent& source,
    double& current_value) {
    route_value_sampling::append_event(events, source.distance, source.value_number,
                                       source.number, source.flag, current_value,
                                       source.source_row_index);
}

void populate_canvas3d_scene_route_values(Canvas3DSceneRouteInfo& route_info,
                                          const MapModel& model) {
    route_info.radius_events.clear();
    route_info.cant_events.clear();
    route_info.gradient_events.clear();
    size_t radius_count = 0;
    size_t cant_count = 0;
    size_t gradient_count = 0;
    for (const TrackEvent& event : model.own_events) {
        if (event.key == "radius") ++radius_count;
        else if (event.key == "cant") ++cant_count;
        else if (event.key == "gradient") ++gradient_count;
    }
    route_info.radius_events.reserve(radius_count);
    route_info.cant_events.reserve(cant_count);
    route_info.gradient_events.reserve(gradient_count);

    double radius_value = 0.0;
    double cant_value = 0.0;
    double gradient_value = 0.0;
    // maploader emits own_events in stable distance order; filtering preserves that query order.
    for (const TrackEvent& source : model.own_events) {
        if (source.key == "radius") {
            append_scene_route_value_event(route_info.radius_events, source, radius_value);
        } else if (source.key == "cant") {
            append_scene_route_value_event(route_info.cant_events, source, cant_value);
        } else if (source.key == "gradient") {
            append_scene_route_value_event(route_info.gradient_events, source, gradient_value);
        }
    }
}

void populate_canvas3d_scene_route_stations(Canvas3DSceneRouteInfo& route_info,
                                            const MapModel& model) {
    route_info.stations.clear();
    route_info.stations.reserve(model.stations.size());
    for (const Station& station : model.stations) {
        if (!std::isfinite(station.distance)) continue;
        Canvas3DSceneRouteStation route_station;
        route_station.distance = station.distance;
        route_station.name = station.name.empty() ? station.key : station.name;
        route_info.stations.push_back(std::move(route_station));
    }
    if (!std::is_sorted(route_info.stations.begin(), route_info.stations.end(),
                        [](const Canvas3DSceneRouteStation& a,
                           const Canvas3DSceneRouteStation& b) {
                            return a.distance < b.distance;
                        })) {
        std::stable_sort(route_info.stations.begin(), route_info.stations.end(),
                         [](const Canvas3DSceneRouteStation& a,
                            const Canvas3DSceneRouteStation& b) {
                             return a.distance < b.distance;
                         });
    }
}

std::optional<size_t> canvas3d_scene_signal_speed_index(
    const std::string& value) {
    if (value.empty()) return std::nullopt;
    char* end = nullptr;
    const double parsed = std::strtod(value.c_str(), &end);
    const double index_upper = std::ldexp(1.0, std::numeric_limits<size_t>::digits);
    if (end == value.c_str() || !end || *end != '\0' ||
        !std::isfinite(parsed) || parsed < 0.0 || std::floor(parsed) != parsed ||
        parsed >= index_upper) {
        return std::nullopt;
    }
    return static_cast<size_t>(parsed);
}

static std::string canvas3d_scene_selected_signal_speeds(
    const std::vector<std::string>& signal_indices,
    const std::vector<std::string>& speed_limits) {
    if (signal_indices.empty() || speed_limits.empty()) return {};
    std::vector<std::string> selected;
    selected.reserve(signal_indices.size());
    for (const std::string& value : signal_indices) {
        const std::optional<size_t> index =
            canvas3d_scene_signal_speed_index(value);
        selected.push_back(
            index && *index < speed_limits.size() &&
                    !speed_limits[*index].empty()
                ? speed_limits[*index]
                : "null");
    }
    return join_table_values(selected, " | ");
}

void populate_canvas3d_scene_speed_limits(Canvas3DSceneRouteInfo& route_info,
                                          const MapModel& model) {
    route_info.speed_limit_events.clear();
    route_info.speed_limit_events.reserve(model.speedlimits.size());
    for (const SpeedLimit& input : model.speedlimits) {
        route_info.speed_limit_events.push_back(
            {input.distance, input.has_speed, input.speed, input.order});
    }
    std::stable_sort(
        route_info.speed_limit_events.begin(), route_info.speed_limit_events.end(),
        [](const Canvas3DSceneSpeedLimitEvent& left,
           const Canvas3DSceneSpeedLimitEvent& right) {
            if (left.distance != right.distance) return left.distance < right.distance;
            return left.order < right.order;
        });
}

void populate_canvas3d_scene_section_signals(Canvas3DSceneRouteInfo& route_info,
                                             const MapModel& model) {
    enum class SectionSignalStateKind { SpeedLimits, SectionBegin };
    struct SectionSignalStateSource {
        double distance = 0.0;
        int order = 0;
        SectionSignalStateKind kind = SectionSignalStateKind::SpeedLimits;
        const TableRow* row = nullptr;
    };
    std::vector<SectionSignalStateSource> section_signal_sources;
    section_signal_sources.reserve(
        model.section_speed_limits.size() + model.section_begins.size());
    auto append_section_signal_sources = [&](const std::vector<TableRow>& rows,
                                             SectionSignalStateKind kind) {
        for (const TableRow& row : rows) {
            section_signal_sources.push_back({
                table_cell_number(row, "distance"),
                static_cast<int>(table_cell_number(row, "order")),
                kind,
                &row});
        }
    };
    append_section_signal_sources(
        model.section_speed_limits, SectionSignalStateKind::SpeedLimits);
    append_section_signal_sources(
        model.section_begins, SectionSignalStateKind::SectionBegin);
    std::stable_sort(
        section_signal_sources.begin(), section_signal_sources.end(),
        [](const SectionSignalStateSource& left,
           const SectionSignalStateSource& right) {
            if (left.distance != right.distance) {
                return left.distance < right.distance;
            }
            return left.order < right.order;
        });
    route_info.section_signal_events.clear();
    route_info.section_signal_events.reserve(section_signal_sources.size());
    std::vector<std::string> speed_limits;
    std::vector<std::string> signal_indices;
    for (const SectionSignalStateSource& source : section_signal_sources) {
        if (source.kind == SectionSignalStateKind::SpeedLimits) {
            speed_limits = section_row_values(*source.row);
        } else {
            signal_indices = section_row_values(*source.row);
        }
        std::string selected = canvas3d_scene_selected_signal_speeds(
            signal_indices, speed_limits);
        if (!route_info.section_signal_events.empty() &&
            route_info.section_signal_events.back().values == selected) {
            continue;
        }
        route_info.section_signal_events.push_back(
            {source.distance, source.order, std::move(selected)});
    }
}

static void populate_canvas3d_scene_route_info(Canvas3DScene& scene, const MapModel& model) {
    Canvas3DSceneRouteInfo route_info;
    populate_canvas3d_scene_route_values(route_info, model);
    populate_canvas3d_scene_speed_limits(route_info, model);
    populate_canvas3d_scene_section_signals(route_info, model);
    populate_canvas3d_scene_route_stations(route_info, model);
    scene.route_info = std::move(route_info);
}

static double canvas3d_scene_route_value_at(
    const std::vector<route_value_sampling::Event>& events,
    double distance) {
    return route_value_sampling::sample(events, distance).value;
}

static std::string canvas3d_scene_curve_marker_label(
    const Canvas3DSceneRouteInfo& route_info,
    const TrackEvent& event) {
    char radius_text[64] = {};
    char cant_text[64] = {};
    scene_route_overlay::format_number(radius_text, sizeof(radius_text),
                                       std::abs(event.number));
    const double cant =
        canvas3d_scene_route_value_at(route_info.cant_events, event.distance);
    scene_route_overlay::format_number(cant_text, sizeof(cant_text), cant);
    char label[192] = {};
    std::snprintf(label, sizeof(label), "R %s m  %s %s",
                  radius_text, cant_text, event.number < 0.0 ? u8"←" : u8"→");
    return label;
}

static std::string canvas3d_scene_gradient_marker_label(double gradient) {
    char gradient_text[64] = {};
    scene_route_overlay::format_number(gradient_text, sizeof(gradient_text),
                                       std::abs(gradient));
    char label[128] = {};
    std::snprintf(label, sizeof(label), "%s %s‰",
                  gradient > 0.0 ? u8"↗" : u8"↘", gradient_text);
    return label;
}

static std::string canvas3d_scene_table_marker_label(
    const TableRow& row,
    std::initializer_list<const char*> keys,
    const char* separator = " ",
    const char* empty_value = nullptr) {
    std::string label;
    size_t key_index = 0;
    for (const char* key : keys) {
        if (key_index++ != 0) label += separator;
        const std::string& value = table_cell(row, key);
        if (value.empty() && empty_value) {
            label += empty_value;
        } else {
            label += value;
        }
    }
    return label;
}

void sort_canvas3d_scene_markers(std::vector<Canvas3DSceneMarker>& markers) {
    std::stable_sort(markers.begin(), markers.end(),
                     [](const Canvas3DSceneMarker& a,
                        const Canvas3DSceneMarker& b) {
                         if (a.track_point.distance != b.track_point.distance) {
                             return a.track_point.distance < b.track_point.distance;
                         }
                         return normalize_track_lookup_key(a.track_key) <
                             normalize_track_lookup_key(b.track_key);
                     });
}

void populate_canvas3d_scene_markers(Canvas3DScene& scene, const MapModel& model) {
    kme::timing::GuiTiming::Stage edit_timing("scene.marker_recipes");
    scene.markers.clear();
    const Canvas3DTrackPath* own_track = scene_own_track_path(scene);
    if (!own_track || own_track->points.empty()) return;

    const size_t estimated_count =
        model.station_positions.size() + model.curve_rows.size() +
        model.gradient_rows.size() + model.other_track_changes.size() +
        model.speedlimits.size() + model.section_begins.size() +
        model.beacons.size() + model.pretrains.size() +
        model.irregularities.size() + model.map_sounds.size() +
        model.map_sound_3d.size() + model.rolling_noises.size() +
        model.flange_noises.size() + model.joint_noises.size() +
        model.backgrounds.size() + model.adhesions.size() +
        model.cab_illuminance.size() + model.fogs.size() +
        model.draw_distances.size();
    scene.markers.reserve(estimated_count);

    auto append_marker = [&](MapMarkerVisualKind kind,
                             double distance,
                             std::string label = {},
                             MapMarkerIconVariant icon_variant =
                                 MapMarkerIconVariant::Default,
                             Canvas3DSceneMarkerListKind list_kind =
                                 Canvas3DSceneMarkerListKind::None,
                             std::string row_kind = {},
                             std::optional<size_t> row_index = std::nullopt,
                             std::string edit_id = {},
                             bool unpaired_transition = false,
                             double source_x = 0.0,
                             double source_y = 0.0,
                             std::string secondary_label = {}) {
        if (!std::isfinite(distance)) return;
        std::optional<Canvas3DTrackPoint> point =
            scene_sample_track_path_points(*own_track, distance);
        if (!point) return;
        Canvas3DSceneMarker marker;
        marker.kind = kind;
        marker.list_kind = list_kind;
        marker.icon_variant = icon_variant;
        marker.track_point = kind == MapMarkerVisualKind::MapSound3D
            ? scene_sound3d_source_point(*point, source_x, source_y)
            : *point;
        marker.label = std::move(label);
        marker.secondary_label = std::move(secondary_label);
        marker.row_kind = std::move(row_kind);
        marker.row_index = row_index;
        marker.edit_id = std::move(edit_id);
        marker.unpaired_transition = unpaired_transition;
        scene.markers.push_back(std::move(marker));
    };

    for (const Station& station : model.station_positions) {
        append_marker(MapMarkerVisualKind::Station, station.distance,
                      station.name.empty() ? station.key : station.name,
                      MapMarkerIconVariant::StationRailDiagram,
                      Canvas3DSceneMarkerListKind::None, "station.put", std::nullopt,
                      station.edit_id);
    }

    for (size_t row_index = 0; row_index < model.curve_rows.size(); ++row_index) {
        const TableRow& row = model.curve_rows[row_index];
        const std::string method = ascii_lower(table_cell(row, "method"));
        if (method == "curve.interpolate") continue;
        if (method == "curve.setgauge" || method == "curve.gauge" ||
            method == "curve.setcenter" || method == "curve.setfunction") {
            const MapMarkerVisualKind kind = method == "curve.setcenter"
                ? MapMarkerVisualKind::CurveCenter
                : method == "curve.setfunction"
                    ? MapMarkerVisualKind::CurveFunction
                    : MapMarkerVisualKind::CurveGauge;
            const char* code = method == "curve.setcenter" ? "CC" :
                method == "curve.setfunction" ? "CF" : "CG";
            append_marker(kind, table_cell_number(row, "distance"), code,
                          MapMarkerIconVariant::Default,
                          Canvas3DSceneMarkerListKind::None, "curve", row_index,
                          row.edit_id, false, 0.0, 0.0,
                          table_cell(row, "radius"));
            continue;
        }
        const bool transition = method == "curve.begintransition";
        const double radius = table_cell_number(row, "radius");
        const bool end = method == "curve.end" ||
            (!transition && std::isfinite(radius) &&
             std::abs(radius) <= k_scene_route_display_zero_epsilon);
        const std::string edit_id = transition
            ? table_cell(row, "_primaryEditId") : row.edit_id;
        const bool unpaired_transition = transition &&
            table_cell(row, "_transitionStatus") == "orphan";
        if (transition) {
            append_marker(MapMarkerVisualKind::CurveTransitionStart,
                          table_cell_number(row, "distance"), "Curve\nTr.",
                          MapMarkerIconVariant::Default,
                          Canvas3DSceneMarkerListKind::None, "curve", row_index,
                          edit_id, unpaired_transition);
        } else if (end) {
            append_marker(MapMarkerVisualKind::CurveEnd,
                          table_cell_number(row, "distance"), "End",
                          MapMarkerIconVariant::Default,
                          Canvas3DSceneMarkerListKind::None, "curve", row_index,
                          edit_id);
        } else {
            TrackEvent event;
            event.distance = table_cell_number(row, "distance");
            event.value_number = true;
            event.number = radius;
            append_marker(MapMarkerVisualKind::CurveCircularStart, event.distance,
                          canvas3d_scene_curve_marker_label(scene.route_info, event),
                          MapMarkerIconVariant::Default,
                          Canvas3DSceneMarkerListKind::None, "curve", row_index,
                          edit_id);
        }
    }
    for (const route_value_sampling::Event& event : scene.route_info.radius_events) {
        if (event.kind != route_value_sampling::EventKind::Interpolate) continue;
        std::optional<size_t> row_index;
        std::string edit_id;
        if (event.source_row_index < model.curve_rows.size()) {
            const TableRow& row = model.curve_rows[event.source_row_index];
            if (ascii_lower(table_cell(row, "method")) == "curve.interpolate") {
                row_index = event.source_row_index;
                edit_id = row.edit_id;
            }
        }
        std::string label;
        if (std::abs(event.value) <= k_scene_route_display_zero_epsilon) {
            label = "Intpl. 0";
        } else {
            TrackEvent display;
            display.distance = event.distance;
            display.value_number = true;
            display.number = event.value;
            label = canvas3d_scene_curve_marker_label(scene.route_info, display);
        }
        append_marker(MapMarkerVisualKind::CurveCircularStart, event.distance,
                      std::move(label), MapMarkerIconVariant::Default,
                      Canvas3DSceneMarkerListKind::None, "curve",
                      row_index, std::move(edit_id));
    }
    for (size_t row_index = 0; row_index < model.gradient_rows.size(); ++row_index) {
        const TableRow& row = model.gradient_rows[row_index];
        const std::string method = ascii_lower(table_cell(row, "method"));
        const bool transition = method == "gradient.begintransition";
        const double gradient = table_cell_number(row, "gradient");
        const bool end = method == "gradient.end" ||
            (!transition && std::isfinite(gradient) &&
             std::abs(gradient) <= k_scene_route_display_zero_epsilon);
        const double distance = table_cell_number(row, "distance");
        const std::string edit_id = transition
            ? table_cell(row, "_primaryEditId") : row.edit_id;
        const bool unpaired_transition = transition &&
            table_cell(row, "_transitionStatus") == "orphan";
        if (transition) {
            append_marker(MapMarkerVisualKind::GradientTransitionStart, distance,
                          "Gradient\nTr.", MapMarkerIconVariant::Default,
                          Canvas3DSceneMarkerListKind::None, "gradient", row_index,
                          edit_id, unpaired_transition);
        } else if (end) {
            append_marker(MapMarkerVisualKind::GradientEnd, distance, "End",
                          MapMarkerIconVariant::Default,
                          Canvas3DSceneMarkerListKind::None, "gradient", row_index,
                          edit_id);
        } else {
            append_marker(MapMarkerVisualKind::GradientStart, distance,
                          canvas3d_scene_gradient_marker_label(
                              gradient),
                          MapMarkerIconVariant::Default,
                          Canvas3DSceneMarkerListKind::None, "gradient", row_index,
                          edit_id);
        }
    }

    for (size_t row_index = 0; row_index < model.other_track_changes.size();
         ++row_index) {
        const TableRow& row = model.other_track_changes[row_index];
        if (row.edit_id.empty()) continue;
        const std::string track_key = table_cell(row, "trackKey");
        const std::string normalized_key = normalize_track_lookup_key(track_key);
        const Canvas3DTrackPath* path =
            scene_other_track_path_for_key(scene, normalized_key);
        if (!path || path->points.empty()) continue;
        const double distance = table_cell_number(row, "distance");
        if (!std::isfinite(distance) ||
            distance < path->points.front().distance ||
            distance > path->points.back().distance) continue;
        const std::optional<Canvas3DTrackPoint> point =
            scene_sample_track_path_points(*path, distance);
        if (!point) continue;
        Canvas3DSceneMarker marker;
        marker.kind = MapMarkerVisualKind::OtherTrackChange;
        marker.track_point = *point;
        const std::string& method = table_cell(row, "method");
        const std::string& parameters = table_cell(row, "parameters");
        const size_t method_offset = method.compare(0, 6, "Track.") == 0 ? 6 : 0;
        std::string label;
        label.reserve(track_key.size() + method.size() + parameters.size() + 2);
        label.append(track_key);
        label.push_back('\n');
        label.append(method, method_offset, std::string::npos);
        label.push_back('\n');
        bool comma_pending = false;
        for (const char character : parameters) {
            if (character == ',') {
                comma_pending = true;
                continue;
            }
            if (comma_pending) {
                if (std::isspace(static_cast<unsigned char>(character))) continue;
                label.push_back(' ');
                comma_pending = false;
            }
            label.push_back(character);
        }
        marker.label = std::move(label);
        marker.row_kind = "otherTrack.change";
        marker.row_index = row_index;
        marker.edit_id = row.edit_id;
        marker.track_key = path->key;
        marker.theme_color = path->color;
        marker.has_theme_color = true;
        scene.markers.push_back(std::move(marker));
    }

    for (const SpeedLimit& speed : model.speedlimits) {
        const TableRow* source = speed.row_index < model.speed_limit_rows.size()
            ? &model.speed_limit_rows[speed.row_index]
            : nullptr;
        const std::string edit_id = source ? source->edit_id : std::string{};
        char value[64] = {};
        if (speed.has_speed && std::isfinite(speed.speed)) {
            scene_route_overlay::format_number(value, sizeof(value), speed.speed);
            append_marker(
                MapMarkerVisualKind::SpeedLimit, speed.distance, value,
                MapMarkerIconVariant::SpeedLimitBegin,
                Canvas3DSceneMarkerListKind::SpeedLimit, "speedlimit",
                speed.row_index, edit_id);
        } else {
            append_marker(
                MapMarkerVisualKind::SpeedLimit, speed.distance, {},
                MapMarkerIconVariant::SpeedLimitEnd,
                Canvas3DSceneMarkerListKind::SpeedLimit, "speedlimit",
                speed.row_index, edit_id);
        }
    }

    auto append_table_markers = [&](const std::vector<TableRow>& rows,
                                    MapMarkerVisualKind kind,
                                    Canvas3DSceneMarkerListKind list_kind,
                                    const char* row_kind,
                                    const auto& label_for_row,
                                    MapMarkerIconVariant icon_variant =
                                        MapMarkerIconVariant::Default) {
        for (size_t row_index = 0; row_index < rows.size(); ++row_index) {
            const TableRow& row = rows[row_index];
            std::string label = label_for_row(row);
            const bool sound3d = kind == MapMarkerVisualKind::MapSound3D;
            append_marker(kind, table_cell_number(row, "distance"),
                          std::move(label), icon_variant, list_kind, row_kind,
                          row_index, row.edit_id, false,
                          sound3d ? table_cell_number(row, "x") : 0.0,
                          sound3d ? table_cell_number(row, "y") : 0.0);
        }
    };

    const auto field_label = [](const char* key) {
        return [key](const TableRow& row) {
            return canvas3d_scene_table_marker_label(row, {key});
        };
    };

    append_table_markers(model.section_begins, MapMarkerVisualKind::Section,
                         Canvas3DSceneMarkerListKind::Section, "section.begin",
                         [](const TableRow& row) {
                             return join_table_values(
                                 section_row_values(row), " ");
                         });

    append_table_markers(model.beacons, MapMarkerVisualKind::Beacon,
                         Canvas3DSceneMarkerListKind::Beacon, "beacon.put",
                         [](const TableRow& row) {
                             return canvas3d_scene_table_marker_label(
                                 row, {"type", "section", "sendData"});
                         });
    append_table_markers(model.pretrains, MapMarkerVisualKind::PreTrain,
                         Canvas3DSceneMarkerListKind::None, "preTrain.pass",
                         field_label("passTime"));
    append_table_markers(model.irregularities, MapMarkerVisualKind::Irregularity,
                         Canvas3DSceneMarkerListKind::Irregularity,
                         "irregularity.change",
                         [](const TableRow& row) {
                             return canvas3d_scene_table_marker_label(
                                 row, {"x", "y", "r"}, "\n") + "\n" +
                                 canvas3d_scene_table_marker_label(
                                     row, {"lx", "ly", "lr"});
                         });
    append_table_markers(model.map_sounds, MapMarkerVisualKind::MapSound,
                         Canvas3DSceneMarkerListKind::MapSound,
                         "mapSound.play",
                         field_label("soundKey"));
    append_table_markers(model.map_sound_3d, MapMarkerVisualKind::MapSound3D,
                         Canvas3DSceneMarkerListKind::MapSound3D,
                         "mapSound3D.put",
                         field_label("soundKey"));
    append_table_markers(model.rolling_noises, MapMarkerVisualKind::RollingNoise,
                         Canvas3DSceneMarkerListKind::RollingNoise,
                         "rollingNoise.change",
                         field_label("index"));
    append_table_markers(model.flange_noises, MapMarkerVisualKind::FlangeNoise,
                         Canvas3DSceneMarkerListKind::FlangeNoise,
                         "flangeNoise.change", field_label("index"));
    append_table_markers(model.joint_noises, MapMarkerVisualKind::JointNoise,
                         Canvas3DSceneMarkerListKind::JointNoise,
                         "jointNoise.play",
                         field_label("index"));
    append_table_markers(model.backgrounds, MapMarkerVisualKind::Background,
                         Canvas3DSceneMarkerListKind::Background,
                         "background.change",
                         field_label("structureKey"));
    append_table_markers(model.adhesions, MapMarkerVisualKind::Adhesion,
                         Canvas3DSceneMarkerListKind::Adhesion,
                         "adhesion.change",
                         [](const TableRow& row) {
                             return canvas3d_scene_table_marker_label(
                                 row, {"a", "b", "c"});
                         },
                         MapMarkerIconVariant::AdhesionOutlined);
    append_table_markers(model.cab_illuminance, MapMarkerVisualKind::CabIlluminance,
                         Canvas3DSceneMarkerListKind::CabIlluminance,
                         "cabIlluminance.change",
                         field_label("value"));
    append_table_markers(model.fogs, MapMarkerVisualKind::Fog,
                         Canvas3DSceneMarkerListKind::Fog, "fog.change",
                         [](const TableRow& row) {
                             std::string label = canvas3d_scene_table_marker_label(
                                 row, {"density"}, " ", "-");
                             label += '\n';
                             label += canvas3d_scene_table_marker_label(
                                 row, {"red", "green", "blue"}, " ", "-");
                             return label;
                         });
    append_table_markers(model.legacy_fogs, MapMarkerVisualKind::Fog,
                         Canvas3DSceneMarkerListKind::LegacyFog, "legacyFog.change",
                         [](const TableRow& row) {
                             std::string label = canvas3d_scene_table_marker_label(
                                 row, {"start", "end"}, " ", "-");
                             label += '\n';
                             label += canvas3d_scene_table_marker_label(
                                 row, {"red", "green", "blue"}, " ", "-");
                             return label;
                         });
    append_table_markers(model.draw_distances, MapMarkerVisualKind::DrawDistance,
                         Canvas3DSceneMarkerListKind::DrawDistance,
                         "drawDistance.change",
                         field_label("value"));

    sort_canvas3d_scene_markers(scene.markers);
}

static double resolve_canvas3d_scene_fog_component(const TableRow& row,
                                            const char* key,
                                            double previous_value) {
    if (table_cell(row, key).empty()) return previous_value;
    const double value = table_cell_number(row, key);
    if (std::isnan(value)) return previous_value;
    return std::clamp(value, 0.0, 1.0);
}

void populate_canvas3d_scene_fog(Canvas3DScene& scene, const MapModel& model) {
    scene.fog_keyframes.clear();
    if (model.fogs.empty()) return;

    std::vector<const TableRow*> rows;
    rows.reserve(model.fogs.size());
    for (const TableRow& row : model.fogs) {
        if (std::isfinite(table_cell_number(row, "distance"))) rows.push_back(&row);
    }
    std::stable_sort(rows.begin(), rows.end(), [](const TableRow* a, const TableRow* b) {
        const double a_distance = table_cell_number(*a, "distance");
        const double b_distance = table_cell_number(*b, "distance");
        if (a_distance != b_distance) return a_distance < b_distance;
        return table_cell_number(*a, "order") < table_cell_number(*b, "order");
    });

    scene.fog_keyframes.reserve(rows.size());
    double density = k_default_scene_fog_density;
    double red = k_default_scene_fog_color;
    double green = k_default_scene_fog_color;
    double blue = k_default_scene_fog_color;
    for (const TableRow* row : rows) {
        density = resolve_canvas3d_scene_fog_component(*row, "density", density);
        red = resolve_canvas3d_scene_fog_component(*row, "red", red);
        green = resolve_canvas3d_scene_fog_component(*row, "green", green);
        blue = resolve_canvas3d_scene_fog_component(*row, "blue", blue);

        Canvas3DSceneFogKeyframe keyframe;
        keyframe.distance = table_cell_number(*row, "distance");
        keyframe.density = static_cast<float>(density);
        keyframe.color = ImVec4(static_cast<float>(red), static_cast<float>(green),
                                static_cast<float>(blue), 1.0f);
        if (!scene.fog_keyframes.empty() &&
            scene.fog_keyframes.back().distance == keyframe.distance) {
            scene.fog_keyframes.back() = keyframe;
        } else {
            scene.fog_keyframes.push_back(keyframe);
        }
    }
}

void populate_canvas3d_scene_draw_distances(Canvas3DScene& scene, const MapModel& model) {
    scene.draw_distance_changes.clear();
    if (model.draw_distances.empty()) return;

    std::vector<const TableRow*> rows;
    rows.reserve(model.draw_distances.size());
    for (const TableRow& row : model.draw_distances) {
        const double distance = table_cell_number(row, "distance");
        if (std::isfinite(distance)) rows.push_back(&row);
    }
    std::stable_sort(rows.begin(), rows.end(), [](const TableRow* a, const TableRow* b) {
        const double a_distance = table_cell_number(*a, "distance");
        const double b_distance = table_cell_number(*b, "distance");
        if (a_distance != b_distance) return a_distance < b_distance;
        return table_cell_number(*a, "order") < table_cell_number(*b, "order");
    });

    scene.draw_distance_changes.reserve(rows.size());
    for (const TableRow* row : rows) {
        Canvas3DSceneDrawDistanceChange change;
        change.distance = table_cell_number(*row, "distance");
        const double raw_value = table_cell_number(*row, "value");
        change.value = !std::isfinite(raw_value) || raw_value < 0.0
            ? 0.0
            : std::round(raw_value / static_cast<double>(k_scene_draw_distance_step_m)) *
                static_cast<double>(k_scene_draw_distance_step_m);
        if (!scene.draw_distance_changes.empty() &&
            scene.draw_distance_changes.back().distance == change.distance) {
            scene.draw_distance_changes.back() = change;
        } else {
            scene.draw_distance_changes.push_back(change);
        }
    }
}
SceneFogSample sample_canvas3d_scene_fog(
    const std::vector<Canvas3DSceneFogKeyframe>& keyframes,
    double distance,
    bool enabled) {
    SceneFogSample sample;
    if (!enabled || keyframes.empty()) return sample;

    auto next = std::upper_bound(
        keyframes.begin(), keyframes.end(), distance,
        [](double value, const Canvas3DSceneFogKeyframe& keyframe) {
            return value < keyframe.distance;
        });
    if (next == keyframes.begin()) {
        sample.density = next->density;
        sample.color = next->color;
    } else if (next == keyframes.end()) {
        sample.density = keyframes.back().density;
        sample.color = keyframes.back().color;
    } else {
        const Canvas3DSceneFogKeyframe& previous = *(next - 1);
        const double interval = next->distance - previous.distance;
        const float ratio = interval > 0.0
            ? static_cast<float>(std::clamp((distance - previous.distance) / interval, 0.0, 1.0))
            : 1.0f;
        sample.density = previous.density + (next->density - previous.density) * ratio;
        sample.color = ImVec4(
            previous.color.x + (next->color.x - previous.color.x) * ratio,
            previous.color.y + (next->color.y - previous.color.y) * ratio,
            previous.color.z + (next->color.z - previous.color.z) * ratio,
            1.0f);
    }
    sample.enabled = sample.density > 0.0f;
    return sample;
}

bool populate_canvas3d_scene_dynamic_content(Canvas3DScene& scene,
                                             const MapModel& model,
                                             int station_index) {
    const Canvas3DTrackPath* own_path = scene_own_track_path(scene);
    if (!own_path || own_path->points.empty()) return false;

    scene.objects.clear();
    scene.instances.clear();
    scene.repeaters.clear();
    scene.backgrounds.clear();
    scene.min_distance = own_path->points.front().distance;
    scene.max_distance = own_path->points.back().distance;

    auto sample_placement_track = [&](const std::string& key, double distance) -> std::optional<Canvas3DTrackPoint> {
        const Canvas3DTrackPath* path = scene_placement_track_path_for_key(scene, key);
        return path ? scene_sample_track_path_points(*path, distance) : std::nullopt;
    };
    auto scene_object = [](Canvas3DSceneObjectKind kind, size_t source_row,
                           const TableRow& row, const char* label_column) {
        Canvas3DSceneObject object;
        object.kind = kind;
        object.source_row = source_row;
        object.label = table_cell(row, label_column);
        object.edit_id = row.edit_id;
        return object;
    };
    auto track_following_instance = [](std::string model_path, const TableRow& row,
                                       double distance, int object_index) {
        Canvas3DModelInstance instance;
        instance.model_path = std::move(model_path);
        instance.track_key = table_cell(row, "trackKey");
        instance.distance = distance;
        instance.object_index = object_index;
        instance.follow_track = true;
        instance.x = table_cell_number(row, "x");
        instance.y = table_cell_number(row, "y");
        instance.z = table_cell_number(row, "z");
        instance.rx = table_cell_number(row, "rx");
        instance.ry = table_cell_number(row, "ry");
        instance.rz = table_cell_number(row, "rz");
        instance.tilt = table_cell_number(row, "tilt");
        instance.span = table_cell_number(row, "span");
        return instance;
    };

    std::map<std::string, std::string> model_paths;
    for (const TableRow& row : model.structure_models) {
        std::string key = scene_model_key(table_cell(row, "structureKey"));
        std::string path = table_cell(row, "resolvedFilePath");
        if (!key.empty() && !path.empty()) model_paths[key] = path;
    }
    auto model_path_for_key = [&](const std::string& key) -> std::string {
        auto it = model_paths.find(scene_model_key(key));
        return it == model_paths.end() ? std::string{} : it->second;
    };

    std::map<std::string, std::vector<Canvas3DSceneModelOption>> signal_model_options;
    for (const TableRow& row : model.signal_aspects) {
        const std::string aspect_key = scene_model_key(table_cell(row, "signalAspectKey"));
        if (aspect_key.empty()) continue;
        const int key_count = std::max(0, static_cast<int>(table_cell_number(row, "_structureKeyCount")));
        std::vector<Canvas3DSceneModelOption> options;
        options.reserve(static_cast<size_t>(key_count));
        for (int key_index = 1; key_index <= key_count; ++key_index) {
            std::string structure_key = trim_ascii(table_cell(row, "structureKey" + std::to_string(key_index)));
            if (structure_key.empty()) continue;
            Canvas3DSceneModelOption option;
            option.structure_key_index = key_index;
            option.structure_key = std::move(structure_key);
            option.model_path = model_path_for_key(option.structure_key);
            options.push_back(std::move(option));
        }
        if (!options.empty()) signal_model_options[aspect_key] = std::move(options);
    }

    auto append_structure_instance = [&](const TableRow& row, size_t source_row) {
        std::string path = model_path_for_key(table_cell(row, "structureKey"));
        if (path.empty()) return;
        double distance = table_cell_number(row, "distance");
        auto point = sample_placement_track(table_cell(row, "trackKey"), distance);
        if (!point) return;
        const int object_index = static_cast<int>(scene.objects.size());
        scene.objects.push_back(scene_object(
            Canvas3DSceneObjectKind::Structure, source_row, row, "structureKey"));
        scene.instances.push_back(track_following_instance(
            std::move(path), row, distance, object_index));
    };
    for (size_t row_index = 0; row_index < model.structures.size(); ++row_index) {
        append_structure_instance(model.structures[row_index], row_index);
    }

    for (size_t between_index = 0; between_index < model.structures_between.size(); ++between_index) {
        const TableRow& row = model.structures_between[between_index];
        std::string path = model_path_for_key(table_cell(row, "structureKey"));
        if (path.empty()) continue;
        double distance = table_cell_number(row, "distance");
        auto p1 = sample_placement_track(table_cell(row, "trackKey1"), distance);
        auto p2 = sample_placement_track(table_cell(row, "trackKey2"), distance);
        auto own = scene_sample_track_path_points(*own_path, distance);
        if (!p1 || !p2 || !own) continue;
        Canvas3DSceneObject object = scene_object(
            Canvas3DSceneObjectKind::Structure,
            model.structures.size() + between_index, row, "structureKey");
        object.structure_put_between = true;
        const int object_index = static_cast<int>(scene.objects.size());
        scene.objects.push_back(std::move(object));

        Canvas3DModelInstance instance;
        instance.model_path = path;
        instance.distance = distance;
        instance.object_index = object_index;
        instance.put_between = true;
        instance.put_between_track_key1 = table_cell(row, "trackKey1");
        instance.put_between_track_key2 = table_cell(row, "trackKey2");
        instance.put_between_flag =
            kme::truncating_int_or_zero(table_cell_number(row, "flag")) & 1;
        instance.world[12] = own->x;
        instance.world[13] = own->y;
        instance.world[14] = own->z;
        scene.instances.push_back(std::move(instance));
    }

    auto append_signal_instance = [&](const TableRow& row, size_t row_index) {
        const std::string aspect_key = scene_model_key(table_cell(row, "signalAspectKey"));
        auto options_it = signal_model_options.find(aspect_key);
        if (options_it == signal_model_options.end()) return;

        const std::vector<Canvas3DSceneModelOption>& options = options_it->second;
        auto selected_it = std::find_if(options.begin(), options.end(), [](const Canvas3DSceneModelOption& option) {
            return !option.model_path.empty();
        });
        if (selected_it == options.end()) return;

        const double distance = table_cell_number(row, "distance");
        auto point = sample_placement_track(table_cell(row, "trackKey"), distance);
        if (!point) return;

        Canvas3DSceneObject object = scene_object(
            Canvas3DSceneObjectKind::Signal, row_index, row, "signalAspectKey");
        object.model_options = options;
        object.selected_model_option = static_cast<size_t>(selected_it - options.begin());

        const int object_index = static_cast<int>(scene.objects.size());
        scene.objects.push_back(std::move(object));
        scene.instances.push_back(track_following_instance(
            selected_it->model_path, row, distance, object_index));
    };
    for (size_t row_index = 0; row_index < model.signals.size(); ++row_index) {
        append_signal_instance(model.signals[row_index], row_index);
    }

    const repeater_linkage::Linkage linkage =
        repeater_linkage::pair_linkage(table_repeater_events(model.repeaters));
    scene.repeaters.reserve(linkage.segments.size());
    for (const repeater_linkage::Segment& linked : linkage.segments) {
        if (linked.begin_source_index >= model.repeaters.size()) continue;
        const TableRow& begin = model.repeaters[linked.begin_source_index];
        const double end_distance = linked.boundary_kind == repeater_linkage::BoundaryKind::Open
            ? scene.max_distance
            : linked.end_distance;
        if (end_distance < linked.begin_distance) continue;

        Canvas3DRepeaterSegment segment;
        segment.edit_id = begin.edit_id;
        segment.track_key = table_cell(begin, "trackKey");
        segment.chain_begin_index = linked.chain_begin_index;
        segment.chain_begin_count = linked.chain_begin_count;
        segment.begin_distance = linked.begin_distance;
        segment.end_distance = end_distance;
        segment.has_end_or_change_position =
            linked.boundary_kind != repeater_linkage::BoundaryKind::Open;
        segment.interval = table_cell_number(begin, "interval");
        segment.x = table_cell_number(begin, "x");
        segment.y = table_cell_number(begin, "y");
        segment.z = table_cell_number(begin, "z");
        segment.rx = table_cell_number(begin, "rx");
        segment.ry = table_cell_number(begin, "ry");
        segment.rz = table_cell_number(begin, "rz");
        segment.tilt = table_cell_number(begin, "tilt");
        segment.span = table_cell_number(begin, "span");
        for (const std::string& structure_key : scene_split_key_list(
                 table_cell(begin, "structureKeys"))) {
            const std::string path = model_path_for_key(structure_key);
            if (!path.empty()) segment.model_paths.push_back(path);
        }
        if (segment.model_paths.empty()) continue;

        Canvas3DSceneObject object = scene_object(
            Canvas3DSceneObjectKind::Repeater, linked.display_index - 1,
            begin, "repeaterKey");
        segment.object_index = static_cast<int>(scene.objects.size());
        scene.objects.push_back(std::move(object));
        scene.repeaters.push_back(std::move(segment));
    }

    for (const TableRow& row : model.backgrounds) {
        Canvas3DBackgroundChange change;
        change.distance = table_cell_number(row, "distance");
        change.model_path = model_path_for_key(table_cell(row, "structureKey"));
        scene.backgrounds.push_back(std::move(change));
    }

    double camera_distance = scene.min_distance;
    if (!model.stations.empty()) {
        int clamped_station = std::clamp(station_index, 0, static_cast<int>(model.stations.size()) - 1);
        camera_distance = model.stations[clamped_station].distance;
    }
    auto camera_point = scene_sample_track_path_points(*own_path, camera_distance);
    if (!camera_point) camera_point = own_path->points.front();
    scene.camera.distance = camera_distance;
    scene.camera.x = camera_point->x;
    scene.camera.y = camera_point->y + k_default_scene_camera_height;
    scene.camera.z = camera_point->z;
    scene.camera.yaw = camera_point->theta;
    scene.camera.pitch = 0.0f;
    return true;
}

} // namespace canvas3d_detail

Canvas3DSceneBuildResult build_canvas3d_scene_preview(const Canvas3DSceneBuildOptions& options) {
    Canvas3DSceneBuildResult result;
    Canvas3DScene& scene = result.scene;
    if (!options.model || options.model->own.empty()) return result;

    const MapModel& model = *options.model;
    scene.tracks.reserve(model.other_tracks.size() + 1);
    auto append_track_path = [&](const std::string& key, const SceneTrackBufferView& points,
                                 bool has_theta, ImVec4 color, bool visible) {
        if (points.empty() || points.cols < 3) return;
        Canvas3DTrackPath path;
        path.key = key;
        path.color = color;
        path.visible = visible;
        path.points.reserve(points.rows);
        for (size_t row = 0; row < points.rows; ++row) {
            path.points.push_back(scene_track_row_point(points, row, has_theta));
        }
        scene.tracks.push_back(std::move(path));
    };

    bool used_scene_geometry = false;
    if (options.map_handle) {
        const double max_step = std::clamp(
            options.control_point_interval > 0.0 ? options.control_point_interval : 25.0,
            1.0,
            25.0);
        bool scene_geometry_ok =
            kv_generate_scene_geometry(options.map_handle, options.unit_distance, 1.0, max_step, 1.0, 0.01) != 0;
        if (scene_geometry_ok) {
            KvSceneGeometrySnapshot snapshot{};
            scene_geometry_ok = kv_get_scene_geometry_snapshot(
                options.map_handle, KV_SCENE_GEOMETRY_SNAPSHOT_VERSION,
                &snapshot, sizeof(snapshot)) != 0;
            if (scene_geometry_ok) {
                std::string validation_error;
                std::optional<SceneTrackBufferView> own_points;
                std::vector<SceneTrackBufferView> other_points;
                if (snapshot.version != KV_SCENE_GEOMETRY_SNAPSHOT_VERSION ||
                    snapshot.structure_size < sizeof(KvSceneGeometrySnapshot)) {
                    validation_error = "ABI mismatch";
                } else {
                    own_points = scene_track_buffer_view(snapshot.own_track);
                    if (!own_points || own_points->empty() || own_points->cols < 3) {
                        validation_error = "invalid own-track buffer";
                    } else if (snapshot.other_track_count != model.other_tracks.size() ||
                               (snapshot.other_track_count > 0 && !snapshot.other_tracks)) {
                        validation_error = "track count mismatch";
                    }
                }
                if (validation_error.empty()) {
                    other_points.reserve(model.other_tracks.size());
                    for (size_t i = 0; i < model.other_tracks.size(); ++i) {
                        const OtherTrack& track = model.other_tracks[i];
                        const KvSceneTrackRow& input = snapshot.other_tracks[i];
                        const bool key_bounds_valid =
                            input.key.offset <= snapshot.string_size &&
                            input.key.length <= snapshot.string_size - input.key.offset;
                        const bool key_size_valid =
                            input.key.length <= std::numeric_limits<size_t>::max() &&
                            input.key.length == track.key.size();
                        const bool key_matches = key_bounds_valid && key_size_valid &&
                            (input.key.length == 0 ||
                             (snapshot.string_data &&
                              std::memcmp(snapshot.string_data + static_cast<size_t>(input.key.offset),
                                          track.key.data(), track.key.size()) == 0));
                        if (!key_matches) {
                            validation_error = "track order mismatch";
                            break;
                        }
                        std::optional<SceneTrackBufferView> points =
                            scene_track_buffer_view(input.points);
                        if (!points || (!points->empty() && points->cols < 3)) {
                            validation_error = "invalid other-track buffer";
                            break;
                        }
                        other_points.push_back(*points);
                    }
                }
                if (validation_error.empty()) {
                    append_track_path("own", *own_points, true,
                                      ImVec4(0.78f, 0.78f, 0.76f, 1.0f),
                                      options.show_own_track_markers);
                    for (size_t i = 0; i < model.other_tracks.size(); ++i) {
                        const OtherTrack& track = model.other_tracks[i];
                        append_track_path(track.key, other_points[i], false,
                                          track.color, track.visible);
                    }
                    used_scene_geometry = true;
                } else {
                    result.log_messages.push_back(
                        "[warn]canvas3D.cpp: 3D scene snapshot " + validation_error);
                }
            }
        }
        if (!scene_geometry_ok) {
            const char* err = kv_get_last_error();
            result.log_messages.push_back(std::string("[warn]canvas3D.cpp: 3D scene preview adaptive geometry failed: ") +
                                          (err ? err : "geometry failed"));
        }
    }

    if (!used_scene_geometry) {
        if (std::optional<SceneTrackBufferView> own_points = scene_track_buffer_view(model.own)) {
            append_track_path("own", *own_points, true,
                              ImVec4(0.78f, 0.78f, 0.76f, 1.0f),
                              options.show_own_track_markers);
        }
        for (const OtherTrack& track : model.other_tracks) {
            if (std::optional<SceneTrackBufferView> points = scene_track_buffer_view(track.points)) {
                append_track_path(track.key, *points, false, track.color, track.visible);
            }
        }
    }
    populate_canvas3d_scene_route_info(scene, model);
    populate_canvas3d_scene_markers(scene, model);
    populate_canvas3d_scene_fog(scene, model);
    populate_canvas3d_scene_draw_distances(scene, model);
    if (!populate_canvas3d_scene_dynamic_content(scene, model, options.station_index)) {
        result.log_messages.push_back("[warn]canvas3D.cpp: 3D scene preview dynamic content could not be built");
    }
    return result;
}

std::vector<Canvas3DTrackVisibility> build_canvas3d_scene_track_visibility(
    const MapModel& model,
    bool show_own_track_markers) {
    std::vector<Canvas3DTrackVisibility> visibility;
    visibility.reserve(model.other_tracks.size() + 1);
    visibility.push_back(Canvas3DTrackVisibility{"own", show_own_track_markers});
    for (const OtherTrack& track : model.other_tracks) {
        visibility.push_back(Canvas3DTrackVisibility{track.key, track.visible});
    }
    return visibility;
}
