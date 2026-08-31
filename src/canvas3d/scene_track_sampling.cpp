/*
 * Copyright (c) 2026 Sapporo_ningyo
 *
 * Licensed under Apache License 2.0; see LICENSE and NOTICE.
 */

#include "scene_track_sampling.h"

#include "kme.h"

#include <algorithm>
#include <cmath>

namespace scene_track_sampling {

// Ordinary track sampling shared by rendering, placement, markers, and the
// camera inside the real track range. Semantics must stay identical to the
// original canvas3D.cpp implementation: empty paths fail, requests before the
// first point or after the last point clamp to the endpoints, and interior
// requests interpolate x/y/z, theta (angle-lerped), gradient, and cant_angle.
std::optional<Canvas3DTrackPoint> sample_track_path_points(const Canvas3DTrackPath& path,
                                                           double distance) {
    if (path.points.empty()) return std::nullopt;
    if (distance <= path.points.front().distance) return path.points.front();
    if (distance >= path.points.back().distance) return path.points.back();
    size_t lo = 0;
    size_t hi = path.points.size();
    while (lo < hi) {
        size_t mid = (lo + hi) / 2;
        if (path.points[mid].distance < distance) lo = mid + 1;
        else hi = mid;
    }
    size_t a_index = lo == 0 ? 0 : lo - 1;
    size_t b_index = std::min(lo, path.points.size() - 1);
    const Canvas3DTrackPoint& a = path.points[a_index];
    const Canvas3DTrackPoint& b = path.points[b_index];
    double span = b.distance - a.distance;
    double t = std::abs(span) < 1e-9 ? 0.0 : std::clamp((distance - a.distance) / span, 0.0, 1.0);
    Canvas3DTrackPoint out;
    out.distance = distance;
    out.x = a.x + (b.x - a.x) * t;
    out.y = a.y + (b.y - a.y) * t;
    out.z = a.z + (b.z - a.z) * t;
    out.theta = angle_lerp(a.theta, b.theta, t);
    out.gradient = a.gradient + (b.gradient - a.gradient) * t;
    out.cant_angle = a.cant_angle + (b.cant_angle - a.cant_angle) * t;
    return out;
}

double camera_min_distance(const Canvas3DScene& scene) {
    return scene.min_distance - k_camera_back_offset_m;
}

double camera_max_distance(const Canvas3DScene& scene) {
    return scene.max_distance;
}

double clamp_camera_distance(const Canvas3DScene& scene, double distance) {
    return std::clamp(distance, camera_min_distance(scene), camera_max_distance(scene));
}

std::optional<Canvas3DTrackPoint> camera_sample_track(const Canvas3DTrackPath& path,
                                                      double distance) {
    if (path.points.empty()) return std::nullopt;
    const double first_distance = path.points.front().distance;
    if (distance >= first_distance) {
        return sample_track_path_points(path, distance);
    }

    // Before the first track point: extrapolate the world position along the
    // first point's horizontal heading and initial gradient tangent. With the
    // current coordinate convention the horizontal forward direction for a
    // heading theta is {sin(theta), 0, -cos(theta)} and height changes by
    // gradient/1000 per metre of distance.
    const Canvas3DTrackPoint& first = path.points.front();
    const double delta = distance - first_distance;
    Canvas3DTrackPoint out = first;
    out.distance = distance;
    out.x = first.x + std::sin(first.theta) * delta;
    out.y = first.y + (first.gradient / 1000.0) * delta;
    out.z = first.z + (-std::cos(first.theta)) * delta;
    return out;
}

} // namespace scene_track_sampling
