/*
 * Copyright (c) 2026 Sapporo_ningyo
 *
 * Licensed under Apache License 2.0; see LICENSE and NOTICE.
 */

// Pure CPU contract tests for the Canvas3D camera mileage range and
// camera-specific track sampling (scene_track_sampling). No Direct3D device,
// no window, no ImGui context, and no komapedit.exe involvement.

#include "canvas3D.h"
#include "scene_track_sampling.h"

#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

namespace {

int failures = 0;

void check(bool condition, const char* message) {
    if (condition) return;
    ++failures;
    std::cerr << "FAIL: " << message << '\n';
}

void check_near(double actual, double expected, double tolerance, const char* message) {
    const bool ok = std::isfinite(actual) && std::abs(actual - expected) <= tolerance;
    if (ok) return;
    ++failures;
    std::cerr << "FAIL: " << message << " (expected " << expected
              << ", got " << actual << ")\n";
}

Canvas3DTrackPoint make_point(double distance, double x, double y, double z,
                              double theta, double gradient, double cant_angle) {
    Canvas3DTrackPoint point;
    point.distance = distance;
    point.x = x;
    point.y = y;
    point.z = z;
    point.theta = theta;
    point.gradient = gradient;
    point.cant_angle = cant_angle;
    return point;
}

Canvas3DTrackPath make_own_track(std::vector<Canvas3DTrackPoint> points) {
    Canvas3DTrackPath path;
    path.key = "own";
    path.points = std::move(points);
    return path;
}

Canvas3DScene make_scene(double min_distance, double max_distance,
                         std::vector<Canvas3DTrackPoint> points) {
    Canvas3DScene scene;
    scene.tracks.push_back(make_own_track(std::move(points)));
    scene.min_distance = min_distance;
    scene.max_distance = max_distance;
    return scene;
}

void camera_bounds_contract() {
    {
        // Zero-based own track: the camera may reach -100 m.
        Canvas3DScene scene = make_scene(
            0.0, 1000.0,
            {make_point(0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0),
             make_point(1000.0, 0.0, 0.0, -1000.0, 0.0, 0.0, 0.0)});
        check_near(scene_track_sampling::camera_min_distance(scene), -100.0, 1e-9,
                   "zero-based track allows camera to -100 m");
        check_near(scene_track_sampling::camera_max_distance(scene), 1000.0, 1e-9,
                   "camera max stays at the real track end");
        check_near(scene_track_sampling::clamp_camera_distance(scene, -100.0), -100.0, 1e-9,
                   "clamp keeps the lower camera bound itself");
        check_near(scene_track_sampling::clamp_camera_distance(scene, -500.0), -100.0, 1e-9,
                   "clamp limits requests below the camera lower bound");
        check_near(scene_track_sampling::clamp_camera_distance(scene, 5000.0), 1000.0, 1e-9,
                   "clamp limits requests above the track end");
        check_near(scene_track_sampling::clamp_camera_distance(scene, 250.0), 250.0, 1e-9,
                   "clamp keeps interior requests unchanged");
        // The real scene range itself must not be widened.
        check_near(scene.min_distance, 0.0, 1e-9,
                   "scene.min_distance keeps the real track start");
        check_near(scene.max_distance, 1000.0, 1e-9,
                   "scene.max_distance keeps the real track end");
    }
    {
        // Non-zero-based own track: the bound is relative, not fixed -100 m.
        Canvas3DScene scene = make_scene(
            250.0, 1250.0,
            {make_point(250.0, 0.0, 10.0, -250.0, 0.0, 0.0, 0.0),
             make_point(1250.0, 0.0, 10.0, -1250.0, 0.0, 0.0, 0.0)});
        check_near(scene_track_sampling::camera_min_distance(scene), 150.0, 1e-9,
                   "250 m start allows camera to 150 m");
        check_near(scene_track_sampling::clamp_camera_distance(scene, 0.0), 150.0, 1e-9,
                   "clamp maps below-start requests to the relative camera bound");
        check_near(scene_track_sampling::clamp_camera_distance(scene, 150.0), 150.0, 1e-9,
                   "clamp keeps the relative camera bound itself");
        check_near(scene_track_sampling::clamp_camera_distance(scene, 1250.0), 1250.0, 1e-9,
                   "clamp keeps the real track end as the camera upper bound");
    }
}

void camera_sampling_normal_range_contract() {
    // Inside the real track range the camera sampler must behave exactly like
    // the ordinary sampler: linear interpolation of x/y/z, gradient, and
    // cant_angle, with theta angle-lerped.
    const Canvas3DTrackPath path = make_own_track(
        {make_point(0.0, 0.0, 0.0, 0.0, 0.0, 10.0, 0.0),
         make_point(100.0, 10.0, 5.0, -100.0, 0.5, 20.0, 0.1)});
    const auto sampled = scene_track_sampling::camera_sample_track(path, 50.0);
    check(sampled.has_value(), "camera sampling succeeds inside the track range");
    if (!sampled) return;
    check_near(sampled->distance, 50.0, 1e-9, "interior sample keeps the requested distance");
    check_near(sampled->x, 5.0, 1e-9, "interior sample interpolates x");
    check_near(sampled->y, 2.5, 1e-9, "interior sample interpolates y");
    check_near(sampled->z, -50.0, 1e-9, "interior sample interpolates z");
    check_near(sampled->theta, 0.25, 1e-9, "interior sample interpolates theta");
    check_near(sampled->gradient, 15.0, 1e-9, "interior sample interpolates gradient");
    check_near(sampled->cant_angle, 0.05, 1e-9, "interior sample interpolates cant_angle");

    // Ordinary sampler agreement inside the range.
    const auto ordinary = scene_track_sampling::sample_track_path_points(path, 75.0);
    const auto camera = scene_track_sampling::camera_sample_track(path, 75.0);
    check(ordinary.has_value() && camera.has_value(),
          "ordinary and camera samplers both succeed inside the range");
    if (ordinary && camera) {
        check_near(camera->x, ordinary->x, 1e-9, "camera x matches ordinary sampling");
        check_near(camera->y, ordinary->y, 1e-9, "camera y matches ordinary sampling");
        check_near(camera->z, ordinary->z, 1e-9, "camera z matches ordinary sampling");
        check_near(camera->theta, ordinary->theta, 1e-9, "camera theta matches ordinary sampling");
    }

    // At and after the track end the camera sampler clamps like the ordinary one.
    const auto at_end = scene_track_sampling::camera_sample_track(path, 100.0);
    check(at_end.has_value() && at_end->distance == 100.0 && at_end->x == 10.0,
          "camera sampling clamps at the track end");
    const auto past_end = scene_track_sampling::camera_sample_track(path, 150.0);
    check(past_end.has_value() && past_end->distance == 100.0,
          "camera sampling clamps past the track end");
}

void camera_sampling_before_start_contract() {
    {
        // Straight, level track heading north (theta = 0): -z is forward, so
        // behind the start the camera extrapolates toward +z.
        const Canvas3DTrackPath path = make_own_track(
            {make_point(0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0),
             make_point(100.0, 0.0, 0.0, -100.0, 0.0, 0.0, 0.0)});
        const auto sampled = scene_track_sampling::camera_sample_track(path, -100.0);
        check(sampled.has_value(), "camera sampling succeeds 100 m before the start");
        if (!sampled) return;
        check_near(sampled->distance, -100.0, 1e-9,
                   "before-start sample keeps the requested distance");
        check_near(sampled->x, 0.0, 1e-9, "before-start sample keeps straight x");
        check_near(sampled->y, 0.0, 1e-9, "level track keeps y before the start");
        check_near(sampled->z, 100.0, 1e-9,
                   "before-start sample extrapolates toward +z for theta 0");
        check_near(sampled->theta, 0.0, 1e-9, "before-start sample keeps first theta");
        check_near(sampled->gradient, 0.0, 1e-9, "before-start sample keeps first gradient");
        check_near(sampled->cant_angle, 0.0, 1e-9, "before-start sample keeps first cant_angle");
    }
    {
        // Curved start with a climbing gradient: verifies the non-zero theta
        // and gradient extrapolation terms.
        const double theta = 0.5; // radians, arbitrary heading
        const double gradient = 25.0; // per-mille climb
        const Canvas3DTrackPath path = make_own_track(
            {make_point(0.0, 30.0, 7.0, -5.0, theta, gradient, 0.0),
             make_point(100.0, 30.0, 7.0, -105.0, theta, gradient, 0.0)});
        const auto sampled = scene_track_sampling::camera_sample_track(path, -100.0);
        check(sampled.has_value(), "camera sampling succeeds before a curved start");
        if (!sampled) return;
        const double delta = -100.0;
        check_near(sampled->x, 30.0 + std::sin(theta) * delta, 1e-9,
                   "before-start x uses sin(theta) along -distance");
        check_near(sampled->y, 7.0 + (gradient / 1000.0) * delta, 1e-9,
                   "before-start y uses gradient/1000 per metre");
        check_near(sampled->z, -5.0 - std::cos(theta) * delta, 1e-9,
                   "before-start z uses -cos(theta) along -distance");
        check_near(sampled->theta, theta, 1e-9, "before-start sample keeps first theta");
        check_near(sampled->gradient, gradient, 1e-9, "before-start sample keeps first gradient");
    }
    {
        // Slightly before the start: must not snap back to the first point.
        const Canvas3DTrackPath path = make_own_track(
            {make_point(0.0, 0.0, 0.0, 0.0, 0.3, 10.0, 0.0),
             make_point(100.0, 29.55, 1.0, -95.52, 0.6, 10.0, 0.0)});
        const auto sampled = scene_track_sampling::camera_sample_track(path, -1.0);
        check(sampled.has_value(), "camera sampling succeeds slightly before the start");
        if (!sampled) return;
        check(sampled->distance == -1.0, "slightly-before sample keeps its requested distance");
        check(std::abs(sampled->x) > 1e-6 || std::abs(sampled->z) > 1e-6,
              "slightly-before sample moves off the first point");
        check_near(sampled->gradient, 10.0, 1e-9,
                   "slightly-before sample keeps first gradient");
    }
}

void camera_sampling_safety_contract() {
    const Canvas3DTrackPath empty_path = make_own_track({});
    check(!scene_track_sampling::camera_sample_track(empty_path, -100.0).has_value(),
          "camera sampling fails for an empty path");
    check(!scene_track_sampling::sample_track_path_points(empty_path, 0.0).has_value(),
          "ordinary sampling fails for an empty path");
    const Canvas3DTrackPath single_point = make_own_track(
        {make_point(250.0, 1.0, 2.0, 3.0, 0.0, 0.0, 0.0)});
    const auto at_point = scene_track_sampling::camera_sample_track(single_point, 250.0);
    check(at_point.has_value() && at_point->distance == 250.0,
          "camera sampling works for a single-point track at its distance");
    const auto before_point = scene_track_sampling::camera_sample_track(single_point, 150.0);
    check(before_point.has_value() && before_point->distance == 150.0,
          "camera sampling extrapolates for a single-point track before its distance");
}

} // namespace

int main() {
    camera_bounds_contract();
    camera_sampling_normal_range_contract();
    camera_sampling_before_start_contract();
    camera_sampling_safety_contract();
    std::cout << "canvas3d camera contract " << (failures ? "FAIL" : "PASS") << '\n';
    return failures == 0 ? 0 : 1;
}
