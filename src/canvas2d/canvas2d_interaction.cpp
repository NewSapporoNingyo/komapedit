/*
 * Copyright (c) 2026 Sapporo_ningyo
 *
 * Licensed under Apache License 2.0; see LICENSE and NOTICE.
 * The GUI uses Dear ImGui; see THIRD_PARTY_NOTICES.md.
 */

#ifdef _MSC_VER
#pragma execution_character_set("utf-8")
#endif

#include "canvas2d_interaction.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

using canvas2d::make_plan_transform;
using canvas2d::PlanScreenTransform;

namespace canvas2d {
namespace {

template <typename Marker>
std::optional<PlanMarkerHit> nearest_marker_hit_impl(
    const std::vector<Marker>& markers,
    const PlanScreenTransform& transform, ImVec2 mouse,
    ImVec2 origin, ImVec2 size, float canvas_margin, bool enabled) {
    if (!enabled) return std::nullopt;
    double best = static_cast<double>(canvas_margin) *
                  static_cast<double>(canvas_margin);
    std::optional<PlanMarkerHit> best_hit;
    for (const Marker& marker : markers) {
        const ImVec2 point = transform.plan_to_screen(marker.x, marker.y);
        if (!point_near_canvas(point, origin, size, canvas_margin)) continue;
        const double dx = static_cast<double>(point.x - mouse.x);
        const double dy = static_cast<double>(point.y - mouse.y);
        const double dist_sq = dx * dx + dy * dy;
        if (dist_sq <= best) {
            best = dist_sq;
            best_hit = PlanMarkerHit{marker.row_index, dist_sq};
        }
    }
    return best_hit;
}

}  // namespace

std::optional<PlanMarkerHit> nearest_marker_hit(
    const std::vector<PlanMarker>& markers,
    const PlanScreenTransform& transform, ImVec2 mouse,
    ImVec2 origin, ImVec2 size, float canvas_margin, bool enabled) {
    return nearest_marker_hit_impl(
        markers, transform, mouse, origin, size, canvas_margin, enabled);
}

std::optional<PlanMarkerHit> nearest_marker_hit(
    const std::vector<PlanStation>& markers,
    const PlanScreenTransform& transform, ImVec2 mouse,
    ImVec2 origin, ImVec2 size, float canvas_margin, bool enabled) {
    return nearest_marker_hit_impl(
        markers, transform, mouse, origin, size, canvas_margin, enabled);
}

std::optional<PlanMarkerHit> nearest_marker_hit(
    const std::vector<PlanSpeed>& markers,
    const PlanScreenTransform& transform, ImVec2 mouse,
    ImVec2 origin, ImVec2 size, float canvas_margin, bool enabled) {
    return nearest_marker_hit_impl(
        markers, transform, mouse, origin, size, canvas_margin, enabled);
}

std::optional<PlanMarkerHit> nearest_marker_hit(
    const std::vector<PlanOtherTrainStopMarker>& markers,
    const PlanScreenTransform& transform, ImVec2 mouse,
    ImVec2 origin, ImVec2 size, float canvas_margin, bool enabled) {
    return nearest_marker_hit_impl(
        markers, transform, mouse, origin, size, canvas_margin, enabled);
}

std::optional<PlanMarkerHit> nearest_marker_hit(
    const std::vector<PlanCurveParameterMarker>& markers,
    const PlanScreenTransform& transform, ImVec2 mouse,
    ImVec2 origin, ImVec2 size, float canvas_margin, bool enabled) {
    return nearest_marker_hit_impl(
        markers, transform, mouse, origin, size, canvas_margin, enabled);
}

std::optional<PlanMarkerHit> nearest_marker_hit(
    const std::vector<OwnTrackEditMarker>& markers,
    const PlanScreenTransform& transform, ImVec2 mouse,
    ImVec2 origin, ImVec2 size, float canvas_margin, bool enabled) {
    return nearest_marker_hit_impl(
        markers, transform, mouse, origin, size, canvas_margin, enabled);
}

}  // namespace canvas2d

namespace {

constexpr size_t k_measure_spatial_index_min_point_count = 8192;

const char* plan_marker_type_label_key(PlanMarkerKind kind) {
    switch (kind) {
        case PlanMarkerKind::Station: return "chk.station_pos";
        case PlanMarkerKind::Structure: return "context.plan_marker.structure";
        case PlanMarkerKind::Repeater: return "context.plan_marker.repeater";
        case PlanMarkerKind::Signal: return "context.plan_marker.signal";
        case PlanMarkerKind::Section: return "chk.section_markers";
        case PlanMarkerKind::Beacon: return "chk.beacon_markers";
        case PlanMarkerKind::PreTrain: return "chk.pretrain_markers";
        case PlanMarkerKind::OtherTrainStop:
            return "context.plan_marker.other_train_stop";
        case PlanMarkerKind::Irregularity: return "chk.irregularity_markers";
        case PlanMarkerKind::MapSound: return "chk.map_sound_markers";
        case PlanMarkerKind::MapSound3D: return "chk.map_sound_3d_markers";
        case PlanMarkerKind::RollingNoise: return "chk.rolling_noise_markers";
        case PlanMarkerKind::FlangeNoise: return "chk.flange_noise_markers";
        case PlanMarkerKind::JointNoise: return "chk.joint_noise_markers";
        case PlanMarkerKind::Background: return "chk.background_markers";
        case PlanMarkerKind::Adhesion: return "chk.adhesion_markers";
        case PlanMarkerKind::CabIlluminance:
            return "chk.cab_illuminance_markers";
        case PlanMarkerKind::Fog: return "chk.fog_markers";
        case PlanMarkerKind::LegacyFog: return "chk.fog_markers";
        case PlanMarkerKind::DrawDistance: return "chk.draw_distance_markers";
        case PlanMarkerKind::SpeedLimit: return "chk.speedlimit";
        case PlanMarkerKind::Curve: return "chk.curve_val";
        case PlanMarkerKind::Gradient: return "chk.gradient_pos";
        case PlanMarkerKind::CurveGauge: return "chk.curve_gauge_markers";
        case PlanMarkerKind::CurveCenter: return "chk.curve_center_markers";
        case PlanMarkerKind::CurveFunction: return "chk.curve_function_markers";
        case PlanMarkerKind::OtherTrackChange:
            return "context.plan_marker.other_track_change";
        case PlanMarkerKind::None:
        default:
            return nullptr;
    }
}

}  // namespace

std::optional<double> App::nearest_plan_measure_distance(
    const PlanData& data, ImVec2 origin, ImVec2 size, ImVec2 mouse) {
    PlanScreenTransform measure_transform = make_plan_transform(plan_view_, -data.origin_angle, origin, size);
    constexpr double hit_radius_pixels = 30.0;
    constexpr double hit_radius_squared = hit_radius_pixels * hit_radius_pixels;
    const double scale = std::abs(measure_transform.scale);
    if (!(scale > 0.0) || !std::isfinite(scale)) return std::nullopt;

    const double cell_size = hit_radius_pixels / scale;
    auto cell_for_point = [cell_size](double x, double y) -> std::optional<MeasureHitTestCell> {
        if (!std::isfinite(x) || !std::isfinite(y) ||
            !(cell_size > 0.0) || !std::isfinite(cell_size)) {
            return std::nullopt;
        }
        const long double cell_x = std::floor(static_cast<long double>(x) /
                                              static_cast<long double>(cell_size));
        const long double cell_y = std::floor(static_cast<long double>(y) /
                                              static_cast<long double>(cell_size));
        const long double cell_min = static_cast<long double>(std::numeric_limits<long long>::lowest());
        const long double cell_max = static_cast<long double>(std::numeric_limits<long long>::max());
        if (!std::isfinite(cell_x) || !std::isfinite(cell_y) ||
            cell_x <= cell_min || cell_x >= cell_max ||
            cell_y <= cell_min || cell_y >= cell_max) {
            return std::nullopt;
        }
        return MeasureHitTestCell{static_cast<long long>(cell_x),
                                  static_cast<long long>(cell_y)};
    };

#ifndef NDEBUG
    const bool use_spatial_index =
        data.own.size() >= k_measure_spatial_index_min_point_count &&
        !debug_measure_force_bruteforce_;
    debug_measure_used_spatial_index_ = use_spatial_index;
#else
    const bool use_spatial_index =
        data.own.size() >= k_measure_spatial_index_min_point_count;
#endif
    if (use_spatial_index &&
        (measure_hit_test_cache_.plan_generation != plan_data_cache_.generation ||
         measure_hit_test_cache_.scale != scale)) {
        MeasureHitTestCache rebuilt;
        rebuilt.plan_generation = plan_data_cache_.generation;
        rebuilt.scale = scale;
        rebuilt.cell_size = cell_size;
        rebuilt.cells.reserve(data.own.size() / 4 + 1);
        for (size_t index = 0; index < data.own.size(); ++index) {
            const TrackPoint& point = data.own[index];
            if (auto cell = cell_for_point(point.x, point.y)) {
                rebuilt.cells[*cell].push_back(index);
            } else {
                rebuilt.unindexed_points.push_back(index);
            }
        }
#ifndef NDEBUG
        rebuilt.rebuild_count = measure_hit_test_cache_.rebuild_count + 1;
#endif
        measure_hit_test_cache_ = std::move(rebuilt);
    }

    double best_squared = hit_radius_squared;
    size_t best_index = std::numeric_limits<size_t>::max();
    auto consider_point = [&](size_t index) {
        const TrackPoint& point = data.own[index];
        const ImVec2 screen = measure_transform.plan_to_screen(point.x, point.y);
        const double dx = static_cast<double>(screen.x) - mouse.x;
        const double dy = static_cast<double>(screen.y) - mouse.y;
        const double distance_squared = dx * dx + dy * dy;
        if (!std::isfinite(distance_squared)) return;
        if (distance_squared < best_squared ||
            (distance_squared == best_squared && index < best_index)) {
            best_squared = distance_squared;
            best_index = index;
        }
    };

    if (use_spatial_index) {
        const auto [mouse_x, mouse_y] = measure_transform.screen_to_plan(mouse);
        if (auto center = cell_for_point(mouse_x, mouse_y)) {
            for (long long offset_y = -1; offset_y <= 1; ++offset_y) {
                for (long long offset_x = -1; offset_x <= 1; ++offset_x) {
                    if ((offset_x < 0 && center->x == std::numeric_limits<long long>::lowest()) ||
                        (offset_x > 0 && center->x == std::numeric_limits<long long>::max()) ||
                        (offset_y < 0 && center->y == std::numeric_limits<long long>::lowest()) ||
                        (offset_y > 0 && center->y == std::numeric_limits<long long>::max())) {
                        continue;
                    }
                    const MeasureHitTestCell cell{center->x + offset_x, center->y + offset_y};
                    const auto found = measure_hit_test_cache_.cells.find(cell);
                    if (found == measure_hit_test_cache_.cells.end()) continue;
                    for (size_t index : found->second) consider_point(index);
                }
            }
            for (size_t index : measure_hit_test_cache_.unindexed_points) {
                consider_point(index);
            }
        } else {
            for (size_t index = 0; index < data.own.size(); ++index) {
                consider_point(index);
            }
        }
    } else {
        for (size_t index = 0; index < data.own.size(); ++index) {
            consider_point(index);
        }
    }

#ifndef NDEBUG
    if (debug_measure_validate_bruteforce_) {
        double brute_best_squared = hit_radius_squared;
        size_t brute_best_index = std::numeric_limits<size_t>::max();
        for (size_t index = 0; index < data.own.size(); ++index) {
            const TrackPoint& point = data.own[index];
            const ImVec2 screen = measure_transform.plan_to_screen(point.x, point.y);
            const double dx = static_cast<double>(screen.x) - mouse.x;
            const double dy = static_cast<double>(screen.y) - mouse.y;
            const double distance_squared = dx * dx + dy * dy;
            if (!std::isfinite(distance_squared)) continue;
            if (distance_squared < brute_best_squared ||
                (distance_squared == brute_best_squared && index < brute_best_index)) {
                brute_best_squared = distance_squared;
                brute_best_index = index;
            }
        }
        debug_measure_validation_passed_ = debug_measure_validation_passed_ &&
            brute_best_index == best_index;
        ++debug_measure_query_count_;
    }
#endif
    if (best_index == std::numeric_limits<size_t>::max()) return std::nullopt;
    return data.own[best_index].d;

}

std::vector<PlanContextMenuEntry> App::collect_plan_context_entries(
    const PlanData& data, ImVec2 mouse, ImVec2 origin, ImVec2 size,
    float marker_canvas_margin, double marker_hover_radius_sq,
    bool enabled) const {
    const PlanScreenTransform hit_transform =
        make_plan_transform(plan_view_, -data.origin_angle, origin, size);
    constexpr double k_plan_marker_overlap_eps = 1e-6;
    constexpr size_t k_plan_context_max_entries = 64;
    auto plan_context_kind_priority = [](PlanMarkerKind kind) -> int {
        switch (kind) {
            case PlanMarkerKind::OtherTrackChange: return 0;
            case PlanMarkerKind::Curve:
            case PlanMarkerKind::Gradient: return 1;
            case PlanMarkerKind::CurveGauge:
            case PlanMarkerKind::CurveCenter:
            case PlanMarkerKind::CurveFunction: return 1;
            case PlanMarkerKind::Station: return 2;
            case PlanMarkerKind::DrawDistance: return 3;
            case PlanMarkerKind::SpeedLimit: return 4;
            case PlanMarkerKind::OtherTrainStop: return 5;
            case PlanMarkerKind::Section: return 6;
            case PlanMarkerKind::Signal: return 7;
            case PlanMarkerKind::Beacon: return 8;
            case PlanMarkerKind::Adhesion: return 9;
            case PlanMarkerKind::Irregularity: return 10;
            case PlanMarkerKind::RollingNoise: return 11;
            case PlanMarkerKind::MapSound: return 12;
            case PlanMarkerKind::MapSound3D: return 13;
            case PlanMarkerKind::FlangeNoise: return 14;
            case PlanMarkerKind::JointNoise: return 15;
            case PlanMarkerKind::Background: return 16;
            case PlanMarkerKind::Repeater: return 17;
            case PlanMarkerKind::CabIlluminance: return 18;
            case PlanMarkerKind::Fog: return 19;
            case PlanMarkerKind::LegacyFog: return 19;
            case PlanMarkerKind::Structure: return 20;
            default: return 100;
        }
    };
    auto table_marker_row_kind = [&](PlanMarkerKind kind, size_t row_index) {
        switch (kind) {
            case PlanMarkerKind::Structure:
                return row_index < model_.structures.size()
                    ? std::string("structure.put") : std::string("structure.between");
            case PlanMarkerKind::Repeater: return std::string("repeater");
            case PlanMarkerKind::Signal: return std::string("signal.put");
            case PlanMarkerKind::Section: return std::string("section.begin");
            case PlanMarkerKind::Beacon: return std::string("beacon.put");
            case PlanMarkerKind::Irregularity: return std::string("irregularity.change");
            case PlanMarkerKind::MapSound: return std::string("mapSound.play");
            case PlanMarkerKind::MapSound3D: return std::string("mapSound3D.put");
            case PlanMarkerKind::RollingNoise: return std::string("rollingNoise.change");
            case PlanMarkerKind::FlangeNoise: return std::string("flangeNoise.change");
            case PlanMarkerKind::JointNoise: return std::string("jointNoise.play");
            case PlanMarkerKind::Background: return std::string("background.change");
            case PlanMarkerKind::Adhesion: return std::string("adhesion.change");
            case PlanMarkerKind::CabIlluminance: return std::string("cabIlluminance.change");
            case PlanMarkerKind::Fog: return std::string("fog.change");
            case PlanMarkerKind::LegacyFog: return std::string("legacyFog.change");
            case PlanMarkerKind::DrawDistance: return std::string("drawDistance.change");
            case PlanMarkerKind::SpeedLimit: return std::string("speedlimit");
            case PlanMarkerKind::Station: return std::string("station.put");
            case PlanMarkerKind::OtherTrackChange: return std::string("otherTrack.change");
            case PlanMarkerKind::CurveGauge:
            case PlanMarkerKind::CurveCenter:
            case PlanMarkerKind::CurveFunction: return std::string("curve");
            default: return std::string{};
        }
    };
    std::vector<PlanContextMenuEntry> entries;
    if (!enabled) return entries;
    struct ContextCandidate {
        PlanMarkerKind kind = PlanMarkerKind::None;
        size_t row_index = 0;
        double x = 0.0;
        double y = 0.0;
        double dist_sq = 0.0;
        std::string edit_id;
        std::string row_kind;
        std::optional<OwnTrackEditMarker> own_track_marker;
    };
    std::vector<ContextCandidate> candidates;
    candidates.reserve(k_plan_context_max_entries);
    auto add_candidate = [&](PlanMarkerKind kind, size_t row_index, double x, double y,
                             std::string edit_id, std::string row_kind,
                             std::optional<OwnTrackEditMarker> own_track_marker = std::nullopt) {
        if (candidates.size() >= k_plan_context_max_entries) return;
        const ImVec2 point = hit_transform.plan_to_screen(x, y);
        if (!canvas2d::point_near_canvas(
                point, origin, size, marker_canvas_margin)) {
            return;
        }
        const double dx = static_cast<double>(point.x - mouse.x);
        const double dy = static_cast<double>(point.y - mouse.y);
        const double dist_sq = dx * dx + dy * dy;
        if (dist_sq > marker_hover_radius_sq) return;
        candidates.push_back(ContextCandidate{kind, row_index, x, y, dist_sq,
                                              std::move(edit_id), std::move(row_kind),
                                              std::move(own_track_marker)});
    };
    auto add_plan_markers = [&](const auto& markers, PlanMarkerKind kind) {
        for (const auto& marker : markers) {
            add_candidate(kind, marker.row_index, marker.x, marker.y, marker.edit_id,
                          table_marker_row_kind(kind, marker.row_index));
        }
    };
    if (edit_actions_available()) {
        for (const OtherTrackChangeMarker& marker : other_track_change_marker_cache_) {
            if (marker.track_index >= model_.other_tracks.size()) continue;
            const OtherTrack& track = model_.other_tracks[marker.track_index];
            if (!track.visible || marker.d < std::max(dmin_, track.range_min) ||
                marker.d > std::min(dmax_, track.range_max)) {
                continue;
            }
            const auto point = hit_transform.model_to_plan(marker.x, marker.y);
            add_candidate(PlanMarkerKind::OtherTrackChange, marker.row_index,
                          point.first, point.second, marker.edit_id, "otherTrack.change");
        }
    }
    for (const OwnTrackEditMarker& marker : data.curve_edit_markers) {
        add_candidate(PlanMarkerKind::Curve, marker.row_index, marker.x, marker.y,
                      marker.edit_id, marker.row_kind, marker);
    }
    for (const OwnTrackEditMarker& marker : data.gradient_edit_markers) {
        add_candidate(PlanMarkerKind::Gradient, marker.row_index, marker.x, marker.y,
                      marker.edit_id, marker.row_kind, marker);
    }
    add_plan_markers(data.curve_gauge_markers, PlanMarkerKind::CurveGauge);
    add_plan_markers(data.curve_center_markers, PlanMarkerKind::CurveCenter);
    add_plan_markers(data.curve_function_markers, PlanMarkerKind::CurveFunction);
    if (show_stations_) {
        for (const PlanStation& station : data.stations) {
            add_candidate(PlanMarkerKind::Station, station.row_index, station.x, station.y,
                          station.station.edit_id, "station.put");
        }
    }
    if (show_speedlimits_) {
        for (const PlanSpeed& marker : data.speedlimits) {
            add_candidate(PlanMarkerKind::SpeedLimit, marker.row_index, marker.x, marker.y,
                          marker.edit_id, "speedlimit");
        }
    }
    add_plan_markers(data.draw_distance_markers, PlanMarkerKind::DrawDistance);
    for (const PlanOtherTrainStopMarker& marker : data.other_train_stop_markers) {
        add_candidate(PlanMarkerKind::OtherTrainStop, marker.row_index, marker.x, marker.y,
                      marker.edit_id, std::string{});
    }
    add_plan_markers(data.section_markers, PlanMarkerKind::Section);
    add_plan_markers(data.signal_markers, PlanMarkerKind::Signal);
    add_plan_markers(data.beacon_markers, PlanMarkerKind::Beacon);
    add_plan_markers(data.adhesion_markers, PlanMarkerKind::Adhesion);
    add_plan_markers(data.irregularity_markers, PlanMarkerKind::Irregularity);
    add_plan_markers(data.rolling_noise_markers, PlanMarkerKind::RollingNoise);
    add_plan_markers(data.map_sound_markers, PlanMarkerKind::MapSound);
    add_plan_markers(data.map_sound_3d_markers, PlanMarkerKind::MapSound3D);
    add_plan_markers(data.flange_noise_markers, PlanMarkerKind::FlangeNoise);
    add_plan_markers(data.joint_noise_markers, PlanMarkerKind::JointNoise);
    add_plan_markers(data.background_markers, PlanMarkerKind::Background);
    add_plan_markers(data.repeater_markers, PlanMarkerKind::Repeater);
    add_plan_markers(data.cab_illuminance_markers, PlanMarkerKind::CabIlluminance);
    add_plan_markers(data.fog_markers, PlanMarkerKind::Fog);
    add_plan_markers(data.legacy_fog_markers, PlanMarkerKind::LegacyFog);
    add_plan_markers(data.structure_markers, PlanMarkerKind::Structure);
    if (candidates.empty()) return entries;
    const ContextCandidate* anchor = &candidates.front();
    for (const ContextCandidate& candidate : candidates) {
        if (candidate.dist_sq < anchor->dist_sq) anchor = &candidate;
    }
    entries.reserve(candidates.size());
    for (const ContextCandidate& candidate : candidates) {
        if (std::abs(candidate.x - anchor->x) > k_plan_marker_overlap_eps ||
            std::abs(candidate.y - anchor->y) > k_plan_marker_overlap_eps) {
            continue;
        }
        PlanContextMenuEntry entry;
        entry.kind = candidate.kind;
        entry.row_index = candidate.row_index;
        entry.edit_id = candidate.edit_id;
        entry.row_kind = candidate.row_kind;
        entry.own_track_marker = candidate.own_track_marker;
        entries.push_back(std::move(entry));
    }
    std::stable_sort(entries.begin(), entries.end(),
        [&](const PlanContextMenuEntry& lhs, const PlanContextMenuEntry& rhs) {
            const int lhs_priority = plan_context_kind_priority(lhs.kind);
            const int rhs_priority = plan_context_kind_priority(rhs.kind);
            if (lhs_priority != rhs_priority) return lhs_priority < rhs_priority;
            return lhs.row_index < rhs.row_index;
        });
    return entries;

}

void App::render_plan_marker_context_menu(
    const char* popup_id,
    const std::vector<PlanContextMenuEntry>& entries) {
    if (!ImGui::BeginPopup(popup_id)) return;
    const bool edit_available = edit_actions_available();
    for (size_t entry_index = 0; entry_index < entries.size(); ++entry_index) {
        const PlanContextMenuEntry& entry = entries[entry_index];
        if (entry_index > 0) ImGui::Separator();
        const bool id_depth_two = entry.edit_id.empty();
        if (id_depth_two) {
            ImGui::PushID(static_cast<int>(entry.kind));
            ImGui::PushID(static_cast<int>(entry.row_index));
        } else {
            ImGui::PushID(entry.edit_id.c_str());
        }
        bool has_source_title = false;
        if (edit_available) {
            const PlanContextSourceInfo source = plan_context_source_for(entry);
            if (!source.file_position.empty()) {
                ImGui::TextDisabled("%s", source.file_position.c_str());
                has_source_title = true;
            }
            if (!source.statement.empty()) {
                ImGui::TextDisabled("%s", source.statement.c_str());
                has_source_title = true;
            }
        }
        if (!has_source_title) {
            if (const char* label_key = plan_marker_type_label_key(entry.kind)) {
                ImGui::TextDisabled("[%s]", tr(label_key).c_str());
            }
        }
        const bool can_edit = edit_available && !entry.edit_id.empty();
        auto render_standard_menu_items = [&](const char* locate_label_key,
                                              size_t locate_limit,
                                              const std::string& edit_row_kind,
                                              auto&& locate_action) {
            ImGui::BeginDisabled(entry.row_index >= locate_limit);
            if (ImGui::MenuItem(tr(locate_label_key).c_str())) {
                locate_action(entry.row_index);
            }
            ImGui::EndDisabled();
            ImGui::BeginDisabled(!can_edit);
            if (ImGui::MenuItem(tr("dialog.element_properties").c_str())) {
                request_element_inspector(entry.edit_id, edit_row_kind);
            }
            if (ImGui::MenuItem(tr("button.delete").c_str())) {
                request_element_delete(entry.edit_id, edit_row_kind);
            }
            ImGui::EndDisabled();
        };
        switch (entry.kind) {
            case PlanMarkerKind::Station:
                ImGui::BeginDisabled(!can_edit);
                if (ImGui::MenuItem(tr("dialog.element_properties").c_str())) {
                    request_element_inspector(entry.edit_id, "station.put");
                }
                if (ImGui::MenuItem(tr("button.delete").c_str())) {
                    request_element_delete(entry.edit_id, "station.put");
                }
                ImGui::EndDisabled();
                break;
            case PlanMarkerKind::Structure: {
                const bool put_between = entry.row_index >= model_.structures.size();
                const char* locate_label_key = put_between
                    ? "menu.locate_in_structure_put_between_list"
                    : "menu.locate_in_structure_list";
                ImGui::BeginDisabled(entry.row_index >= structure_marker_cache_.size());
                if (ImGui::MenuItem(tr(locate_label_key).c_str())) {
                    locate_structure_row_in_list(entry.row_index);
                }
                ImGui::EndDisabled();
                ImGui::BeginDisabled(!can_edit);
                if (ImGui::MenuItem(tr("dialog.element_properties").c_str())) {
                    request_element_inspector(entry.edit_id, entry.row_kind);
                }
                ImGui::EndDisabled();
                break;
            }
            case PlanMarkerKind::Repeater:
                ImGui::BeginDisabled(entry.row_index >= repeater_marker_cache_.size());
                if (ImGui::MenuItem(tr("menu.locate_in_repeater_list").c_str())) {
                    locate_repeater_row_in_list(entry.row_index);
                }
                ImGui::EndDisabled();
                ImGui::BeginDisabled(!can_edit);
                if (ImGui::MenuItem(tr("dialog.element_properties").c_str())) {
                    request_element_inspector(entry.edit_id, "repeater");
                }
                ImGui::EndDisabled();
                break;
            case PlanMarkerKind::Signal:
                render_standard_menu_items("menu.locate_in_signal_list",
                                           signal_marker_cache_.size(), "signal.put",
                                           [&](size_t row) { locate_signal_row_in_list(row); });
                break;
            case PlanMarkerKind::Section:
                render_standard_menu_items("menu.locate_in_section_list",
                                           section_marker_cache_.size(), "section.begin",
                                           [&](size_t row) { locate_section_row_in_list(row); });
                break;
            case PlanMarkerKind::Beacon:
                render_standard_menu_items("menu.locate_in_beacon_list",
                                           beacon_marker_cache_.size(), "beacon.put",
                                           [&](size_t row) { locate_beacon_row_in_list(row); });
                break;
            case PlanMarkerKind::OtherTrainStop: {
                const bool can_locate = entry.row_index < other_train_stop_marker_cache_.size() &&
                    other_train_stop_marker_cache_[entry.row_index].has_value();
                ImGui::BeginDisabled(!can_locate);
                if (ImGui::MenuItem(tr("menu.locate_in_other_train_stop_list").c_str())) {
                    locate_other_train_stop_row_in_list(entry.row_index);
                }
                ImGui::EndDisabled();
                break;
            }
            case PlanMarkerKind::Irregularity:
                render_standard_menu_items("menu.locate_in_irregularity_list",
                                           irregularity_marker_cache_.size(),
                                           "irregularity.change",
                                           [&](size_t row) { locate_irregularity_row_in_list(row); });
                break;
            case PlanMarkerKind::MapSound:
                render_standard_menu_items("menu.locate_in_map_sound_list",
                                           map_sound_marker_cache_.size(), "mapSound.play",
                                           [&](size_t row) { locate_map_sound_row_in_list(row); });
                break;
            case PlanMarkerKind::MapSound3D:
                render_standard_menu_items("menu.locate_in_map_sound_3d_list",
                                           map_sound_3d_marker_cache_.size(), "mapSound3D.put",
                                           [&](size_t row) { locate_map_sound_3d_row_in_list(row); });
                break;
            case PlanMarkerKind::RollingNoise:
                render_standard_menu_items("menu.locate_in_rolling_noise_list",
                                           rolling_noise_marker_cache_.size(),
                                           "rollingNoise.change",
                                           [&](size_t row) { locate_rolling_noise_row_in_list(row); });
                break;
            case PlanMarkerKind::FlangeNoise:
                render_standard_menu_items("menu.locate_in_flange_noise_list",
                                           flange_noise_marker_cache_.size(),
                                           "flangeNoise.change",
                                           [&](size_t row) { locate_flange_noise_row_in_list(row); });
                break;
            case PlanMarkerKind::JointNoise:
                render_standard_menu_items("menu.locate_in_joint_noise_list",
                                           joint_noise_marker_cache_.size(), "jointNoise.play",
                                           [&](size_t row) { locate_joint_noise_row_in_list(row); });
                break;
            case PlanMarkerKind::Background:
                render_standard_menu_items("menu.locate_in_background_list",
                                           background_marker_cache_.size(), "background.change",
                                           [&](size_t row) { locate_background_row_in_list(row); });
                break;
            case PlanMarkerKind::Adhesion:
                render_standard_menu_items("menu.locate_in_adhesion_list",
                                           adhesion_marker_cache_.size(), "adhesion.change",
                                           [&](size_t row) { locate_adhesion_row_in_list(row); });
                break;
            case PlanMarkerKind::CabIlluminance:
                render_standard_menu_items("menu.locate_in_cab_illuminance_list",
                                           cab_illuminance_marker_cache_.size(),
                                           "cabIlluminance.change",
                                           [&](size_t row) { locate_cab_illuminance_row_in_list(row); });
                break;
            case PlanMarkerKind::Fog:
                render_standard_menu_items("menu.locate_in_fog_list",
                                           fog_marker_cache_.size(), "fog.change",
                                           [&](size_t row) { locate_fog_row_in_list(row); });
                break;
            case PlanMarkerKind::LegacyFog:
                render_standard_menu_items("menu.locate_in_legacy_fog_list",
                                           legacy_fog_marker_cache_.size(), "legacyFog.change",
                                           [&](size_t row) { locate_legacy_fog_row_in_list(row); });
                break;
            case PlanMarkerKind::DrawDistance:
                render_standard_menu_items("menu.locate_in_draw_distance_list",
                                           draw_distance_marker_cache_.size(),
                                           "drawDistance.change",
                                           [&](size_t row) { locate_draw_distance_row_in_list(row); });
                break;
            case PlanMarkerKind::SpeedLimit:
                render_standard_menu_items("menu.locate_in_speed_limit_list",
                                           speed_limit_marker_cache_.size(), "speedlimit",
                                           [&](size_t row) { locate_speed_limit_row_in_list(row); });
                break;
            case PlanMarkerKind::Curve:
            case PlanMarkerKind::Gradient: {
                const OwnTrackEditMarker* marker =
                    entry.own_track_marker ? &*entry.own_track_marker : nullptr;
                const bool can_edit_own_track = edit_available && marker &&
                    marker->paired && !marker->target_edit_id.empty();
                ImGui::BeginDisabled(!can_edit_own_track);
                if (ImGui::MenuItem(tr("dialog.element_properties").c_str())) {
                    request_element_inspector(marker->target_edit_id, marker->row_kind);
                }
                if (ImGui::MenuItem(tr("button.delete").c_str())) {
                    request_element_delete(marker->target_edit_id, marker->row_kind);
                }
                ImGui::EndDisabled();
                break;
            }
            case PlanMarkerKind::CurveGauge:
            case PlanMarkerKind::CurveCenter:
            case PlanMarkerKind::CurveFunction:
                ImGui::BeginDisabled(!can_edit);
                if (ImGui::MenuItem(tr("dialog.element_properties").c_str())) {
                    request_element_inspector(entry.edit_id, "curve");
                }
                if (ImGui::MenuItem(tr("button.delete").c_str())) {
                    request_element_delete(entry.edit_id, "curve");
                }
                ImGui::EndDisabled();
                break;
            case PlanMarkerKind::OtherTrackChange:
                ImGui::BeginDisabled(!can_edit);
                if (ImGui::MenuItem(tr("dialog.element_properties").c_str())) {
                    request_element_inspector(entry.edit_id, "otherTrack.change");
                }
                if (ImGui::MenuItem(tr("button.delete").c_str())) {
                    request_element_delete(entry.edit_id, "otherTrack.change");
                }
                ImGui::EndDisabled();
                break;
            default:
                break;
        }
        if (entry.own_track_marker && entry.own_track_marker->transition &&
            !entry.own_track_marker->paired) {
            ImGui::Separator();
            ImGui::TextWrapped("%s", tr("status.transition_unpaired").c_str());
        }
        if (id_depth_two) ImGui::PopID();
        ImGui::PopID();
    }
    ImGui::EndPopup();
}

PlanContextSourceInfo App::plan_context_source_for(
    const PlanContextMenuEntry& entry) const {
    PlanContextSourceInfo info;
    const TableRow* row = nullptr;
    std::string edit_id = entry.edit_id;
    if (entry.own_track_marker && !entry.own_track_marker->target_edit_id.empty()) {
        edit_id = entry.own_track_marker->target_edit_id;
    }
    const std::vector<TableRow>* rows = nullptr;
    if (entry.own_track_marker) {
        rows = entry.own_track_marker->gradient
            ? &model_.gradient_rows : &model_.curve_rows;
    } else if (entry.kind == PlanMarkerKind::Station) {
        rows = &model_.station_list_rows;
    } else if (entry.kind == PlanMarkerKind::Structure) {
        rows = entry.row_index < model_.structures.size()
            ? &model_.structures : &model_.structures_between;
    } else if (entry.kind == PlanMarkerKind::OtherTrainStop) {
        rows = &model_.other_train_stops;
    } else if (!entry.row_kind.empty()) {
        rows = inspector_rows_for_kind(model_, entry.row_kind);
    }
    if (rows) {
        size_t row_index = 0;
        if (!edit_id.empty() && find_row_index_by_edit_id(*rows, edit_id, row_index)) {
            row = &(*rows)[row_index];
        } else if (entry.row_index < rows->size()) {
            row = &(*rows)[entry.row_index];
        }
    }
    if (!row) return info;
    const EditSourceInfo& source = row->source;
    if (!source.file_path.empty()) {
        std::string file_position = display_name_from_path(source.file_path);
        if (source.line > 0) {
            file_position += "  ";
            file_position += std::to_string(source.line);
            if (source.column > 0) {
                file_position += ":";
                file_position += std::to_string(source.column);
            }
        }
        info.file_position = std::move(file_position);
    }
    if (!edit_id.empty()) {
        const auto statement = std::find_if(
            model_.edit_statements.begin(), model_.edit_statements.end(),
            [&](const EditStatementInfo& candidate) {
                return candidate.edit_id == edit_id;
            });
        if (statement != model_.edit_statements.end() && !statement->raw_text.empty()) {
            info.statement = statement->raw_text;
            return info;
        }
    }
    info.statement = source.raw_text_preview;
    return info;
}
