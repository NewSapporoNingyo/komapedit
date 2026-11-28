/*
 * Copyright (c) 2026 Sapporo_ningyo
 *
 * Licensed under Apache License 2.0; see LICENSE and NOTICE.
 * The GUI uses Dear ImGui and ImPlot; see THIRD_PARTY_NOTICES.md.
 */

#pragma once

#include "kme.h"

namespace canvas2d {

inline constexpr size_t k_dense_repeater_overlay_threshold = 1200;
inline constexpr double k_dense_repeater_overview_scale = 0.04;
inline constexpr double k_dense_repeater_segment_scale = 0.05;

struct AlignmentPoseChange {
    double c;
    double s;
    double x;
    double y;
    double z;
    double theta;

    void move(double& px, double& py) const;
    void move(TrackPoint& point) const;
};

struct AlignmentReprojection {
    std::shared_ptr<const Matrix> applied;
    std::shared_ptr<const Matrix> own;
    std::vector<AlignmentPoseChange> poses;
    std::vector<Matrix> other_tracks;
    std::vector<RepeaterOverlayRow> repeaters;

    AlignmentPoseChange pose_at(double distance) const;
};

AlignmentReprojection reproject_plan_geometry(
    std::shared_ptr<const Matrix> applied, std::shared_ptr<const Matrix> own,
    const std::vector<Matrix>& other_tracks, const std::vector<RepeaterOverlayRow>& repeaters);

std::optional<TrackPoint> sample_matrix_track_point(
    const Matrix& points, double distance, bool has_theta_column);
size_t matrix_lower_bound_distance(const Matrix& points, double distance);
size_t matrix_upper_bound_distance(const Matrix& points, double distance);
bool dense_repeater_overview_lod(size_t visible_repeater_count, double scale);
bool dense_repeater_segment_lod(size_t visible_repeater_count, double scale);

} // namespace canvas2d
