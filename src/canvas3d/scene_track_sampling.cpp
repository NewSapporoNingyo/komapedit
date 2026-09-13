/*
 * Copyright (c) 2026 Sapporo_ningyo
 *
 * Licensed under Apache License 2.0; see LICENSE and NOTICE.
 */

#include "scene_track_sampling.h"

#include "kme.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace scene_track_sampling {

void PlacementTrackLookup::rebuild(const Canvas3DScene& scene) {
    own_index_ = scene.tracks.empty() ? static_cast<size_t>(-1) : 0;
    other_indices_.clear();
    other_indices_.reserve(scene.tracks.size());
    for (size_t index = 0; index < scene.tracks.size(); ++index) {
        const std::string& key = scene.tracks[index].key;
        if (key == "own" || key.empty() || key == "0") {
            own_index_ = index;
            break;
        }
    }
    for (size_t index = 0; index < scene.tracks.size(); ++index) {
        if (index != own_index_) {
            // Preserve the original first-match rule for normalized duplicates.
            other_indices_.try_emplace(normalize_track_lookup_key(scene.tracks[index].key), index);
        }
    }
}

const Canvas3DTrackPath* PlacementTrackLookup::own(const Canvas3DScene& scene) const {
    return own_index_ < scene.tracks.size() ? &scene.tracks[own_index_] : nullptr;
}

const Canvas3DTrackPath* PlacementTrackLookup::find(const Canvas3DScene& scene, const std::string& key) const {
    const std::string normalized = normalize_track_lookup_key(key);
    if (!is_own_track_placement_key(normalized)) {
        const auto found = other_indices_.find(normalized);
        if (found != other_indices_.end() && found->second < scene.tracks.size()) {
            return &scene.tracks[found->second];
        }
    }
    return own(scene);
}

bool has_ordered_finite_distances(const Canvas3DTrackPath& path) {
    double previous = -std::numeric_limits<double>::infinity();
    for (const Canvas3DTrackPoint& point : path.points) {
        if (!std::isfinite(point.distance) || point.distance < previous) return false;
        previous = point.distance;
    }
    return true;
}

SegmentRange chunk_segment_range(const Canvas3DTrackPath& path, double minimum,
                                double maximum, bool ordered_finite) {
    if (path.points.size() < 2) return {};
    if (!ordered_finite || !std::isfinite(minimum) || !std::isfinite(maximum)) {
        return {1, path.points.size()};
    }
    const auto first = std::lower_bound(path.points.begin(), path.points.end(), minimum,
        [](const Canvas3DTrackPoint& point, double distance) { return point.distance < distance; });
    const auto last = std::upper_bound(path.points.begin(), path.points.end(), maximum,
        [](double distance, const Canvas3DTrackPoint& point) { return distance < point.distance; });
    return {std::max<size_t>(1, static_cast<size_t>(first - path.points.begin())),
            last == path.points.end() ? path.points.size()
                : static_cast<size_t>(last - path.points.begin()) + 1};
}

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
