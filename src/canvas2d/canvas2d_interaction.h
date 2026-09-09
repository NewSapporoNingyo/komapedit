/*
 * Copyright (c) 2026 Sapporo_ningyo
 *
 * Licensed under Apache License 2.0; see LICENSE and NOTICE.
 * The GUI uses Dear ImGui; see THIRD_PARTY_NOTICES.md.
 */

#pragma once

#include "canvas2d_primitives.h"

#include <optional>
#include <vector>

namespace canvas2d {

struct PlanMarkerHit {
    size_t row_index = 0;
    double dist_sq = 0.0;
};

std::optional<PlanMarkerHit> nearest_marker_hit(
    const std::vector<PlanMarker>& markers,
    const PlanScreenTransform& transform, ImVec2 mouse,
    ImVec2 origin, ImVec2 size, float canvas_margin, bool enabled);
std::optional<PlanMarkerHit> nearest_marker_hit(
    const std::vector<PlanStation>& markers,
    const PlanScreenTransform& transform, ImVec2 mouse,
    ImVec2 origin, ImVec2 size, float canvas_margin, bool enabled);
std::optional<PlanMarkerHit> nearest_marker_hit(
    const std::vector<PlanSpeed>& markers,
    const PlanScreenTransform& transform, ImVec2 mouse,
    ImVec2 origin, ImVec2 size, float canvas_margin, bool enabled);
std::optional<PlanMarkerHit> nearest_marker_hit(
    const std::vector<PlanOtherTrainStopMarker>& markers,
    const PlanScreenTransform& transform, ImVec2 mouse,
    ImVec2 origin, ImVec2 size, float canvas_margin, bool enabled);
std::optional<PlanMarkerHit> nearest_marker_hit(
    const std::vector<PlanCurveParameterMarker>& markers,
    const PlanScreenTransform& transform, ImVec2 mouse,
    ImVec2 origin, ImVec2 size, float canvas_margin, bool enabled);
std::optional<PlanMarkerHit> nearest_marker_hit(
    const std::vector<OwnTrackEditMarker>& markers,
    const PlanScreenTransform& transform, ImVec2 mouse,
    ImVec2 origin, ImVec2 size, float canvas_margin, bool enabled);

}  // namespace canvas2d
