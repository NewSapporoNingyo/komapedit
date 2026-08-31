/*
 * Copyright (c) 2026 Sapporo_ningyo
 *
 * Licensed under Apache License 2.0; see LICENSE and NOTICE.
 */

#pragma once

#include "canvas3D.h"

#include <optional>

// Pure, CPU-only track sampling helpers shared by the Canvas3D scene.
//
// The functions here deliberately depend only on the lightweight
// Canvas3DTrackPoint/Canvas3DTrackPath data in canvas3D.h: no Direct3D device,
// no ImGui context, no App/MapHandle state, and no file I/O. This keeps them
// testable from the registered canvas3d_camera_contract CTest.

namespace scene_track_sampling {

// How far the 3D scene camera may travel behind the first own-track point.
inline constexpr double k_camera_back_offset_m = 100.0;

// Camera mileage range for the scene: [min_distance - 100 m, max_distance].
// Canvas3DScene::min_distance/max_distance stay the real own-track geometry
// range; only the camera uses this widened lower bound.
double camera_min_distance(const Canvas3DScene& scene);
double camera_max_distance(const Canvas3DScene& scene);

// Clamp a requested camera mileage into the camera-only range above.
double clamp_camera_distance(const Canvas3DScene& scene, double distance);

// Ordinary track sampling shared by rendering, placement, markers, and the
// camera inside the real track range. Semantics are identical to the original
// canvas3D.cpp implementation: empty paths fail, requests before the first
// point or after the last point clamp to the endpoints, and interior requests
// interpolate x/y/z, theta (angle-lerped), gradient, and cant_angle.
std::optional<Canvas3DTrackPoint> sample_track_path_points(const Canvas3DTrackPath& path,
                                                           double distance);

// Camera-specific track sampling. For requests inside the real track range
// this is exactly sample_track_path_points. Only when the request is
// before the first point does it extrapolate the world position along the
// first point's horizontal heading (theta) and initial gradient tangent,
// carrying theta/gradient/cant_angle through unchanged and returning the
// requested distance. Placement/object/marker rendering must keep using the
// ordinary sampler.
std::optional<Canvas3DTrackPoint> camera_sample_track(const Canvas3DTrackPath& path,
                                                      double distance);

} // namespace scene_track_sampling
