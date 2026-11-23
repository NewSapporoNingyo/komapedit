/*
 * Copyright (c) 2026 Sapporo_ningyo
 *
 * Licensed under Apache License 2.0; see LICENSE and NOTICE.
 */

#ifdef _MSC_VER
#pragma execution_character_set("utf-8")
#endif

#include "canvas3d_impl.h"
#include "kme.h"
#include "imgui.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iterator>
#include <limits>
#include <optional>
#include <utility>

using namespace canvas3d_detail;

namespace canvas3d_detail {

constexpr float k_scene_gizmo_center_radius_px = 4.0f;

} // namespace canvas3d_detail

namespace canvas3d_detail {

constexpr float k_scene_gizmo_arrow_half_width_px = 5.5f;

} // namespace canvas3d_detail

namespace canvas3d_detail {

constexpr float k_scene_gizmo_arrow_length_px = 11.0f;

} // namespace canvas3d_detail

namespace canvas3d_detail {

constexpr float k_scene_gizmo_hit_radius_px = 8.0f;

} // namespace canvas3d_detail

namespace canvas3d_detail {

constexpr float k_scene_gizmo_origin_gap_px = 9.0f;

} // namespace canvas3d_detail

namespace canvas3d_detail {

constexpr float k_scene_gizmo_length_px = 72.0f;
constexpr std::array<ImU32, 3> k_scene_gizmo_colors = {
    IM_COL32(235, 67, 67, 255),
    IM_COL32(73, 205, 91, 255),
    IM_COL32(65, 126, 245, 255)
};
constexpr std::array<ImU32, 3> k_scene_gizmo_active_colors = {
    IM_COL32(255, 142, 142, 255),
    IM_COL32(153, 255, 164, 255),
    IM_COL32(151, 190, 255, 255)
};

constexpr double k_scene_rotation_pi = 3.14159265358979323846;
constexpr float k_scene_rotation_radius_px = 90.0f;
constexpr float k_scene_rotation_line_width_px = 6.0f;
constexpr float k_scene_rotation_active_width_px = 9.0f;
constexpr float k_scene_rotation_hit_padding_px = 6.5f;
constexpr float k_scene_rotation_hover_tie_px = 1.0f;
constexpr double k_scene_rotation_rear_radius_fraction = 0.05;
constexpr std::array<double Canvas3DPlacementEditTarget::*, 3> k_scene_rotation_fields = {
    &Canvas3DPlacementEditTarget::rx, &Canvas3DPlacementEditTarget::ry, &Canvas3DPlacementEditTarget::rz
};

void SceneRotationDrag::sample_radial(DVec3 normal, std::optional<DVec3> radial) {
    if (radial) {
        if (radial_valid) {
            radians += std::atan2(dot(normal, cross(previous_radial, *radial)),
                                  dot(previous_radial, *radial));
        }
        previous_radial = *radial;
    }
    radial_valid = radial.has_value();
}

double SceneRotationDrag::angle_degrees(double start_degrees, bool snap) const {
    constexpr double snap_step_degrees = 15.0;
    const double delta = radians * (180.0 / k_scene_rotation_pi);
    const double candidate = start_degrees +
        (snap ? std::round(delta / snap_step_degrees) * snap_step_degrees : delta);
    return candidate == start_degrees ? start_degrees : truncate_gui_thousandths(candidate);
}

} // namespace canvas3d_detail

int Canvas3D::Impl::structure_drag_axis_index(Canvas3DSceneDragAxis axis) {
    switch (axis) {
        case Canvas3DSceneDragAxis::X: return 0;
        case Canvas3DSceneDragAxis::Y: return 1;
        case Canvas3DSceneDragAxis::Z: return 2;
        case Canvas3DSceneDragAxis::None: break;
    }
    return -1;
}

Canvas3DSceneDragAxis Canvas3D::Impl::structure_drag_axis_from_index(size_t index) {
    if (index == 0) return Canvas3DSceneDragAxis::X;
    if (index == 1) return Canvas3DSceneDragAxis::Y;
    if (index == 2) return Canvas3DSceneDragAxis::Z;
    return Canvas3DSceneDragAxis::None;
}

bool Canvas3D::Impl::scene_ray_triangle_intersection(DVec3 ray_origin,
                                            DVec3 ray_direction,
                                            DVec3 a,
                                            DVec3 b,
                                            DVec3 c,
                                            double& ray_parameter,
                                            double& barycentric_b,
                                            double& barycentric_c) {
    constexpr double parallel_epsilon = 1e-10;
    constexpr double forward_epsilon = 1e-6;
    const DVec3 edge_ab = b - a;
    const DVec3 edge_ac = c - a;
    const DVec3 p = cross(ray_direction, edge_ac);
    const double determinant = dot(edge_ab, p);
    if (!std::isfinite(determinant) || std::abs(determinant) <= parallel_epsilon) {
        return false;
    }

    const double inverse_determinant = 1.0 / determinant;
    const DVec3 origin_to_a = ray_origin - a;
    const double u = dot(origin_to_a, p) * inverse_determinant;
    if (!std::isfinite(u) || u < 0.0 || u > 1.0) return false;

    const DVec3 q = cross(origin_to_a, edge_ab);
    const double v = dot(ray_direction, q) * inverse_determinant;
    if (!std::isfinite(v) || v < 0.0 || u + v > 1.0) return false;

    const double t = dot(edge_ac, q) * inverse_determinant;
    if (!std::isfinite(t) || t <= forward_epsilon) return false;
    ray_parameter = t;
    barycentric_b = u;
    barycentric_c = v;
    return true;
}

std::optional<double> Canvas3D::Impl::pick_scene_mileage(ImVec2 mouse_local,
                                         int width,
                                         int height,
                                         double visible_min,
                                         double visible_max) const {
    if (scene_mileage_pick_points.size() < 2) return std::nullopt;
    visible_min = std::max(visible_min, scene_data.min_distance);
    visible_max = std::min(visible_max, scene_data.max_distance);
    if (!std::isfinite(visible_min) || !std::isfinite(visible_max) ||
        visible_min > visible_max) {
        return std::nullopt;
    }

    DVec3 ray_origin;
    DVec3 ray_direction;
    if (!scene_camera_ray(mouse_local, width, height, ray_origin, ray_direction)) {
        return std::nullopt;
    }

    const auto begin_it = std::lower_bound(
        scene_mileage_pick_points.begin(), scene_mileage_pick_points.end(), visible_min,
        [](const SceneMileagePickPoint& point, double distance) {
            return point.distance < distance;
        });
    size_t first_segment = static_cast<size_t>(
        std::distance(scene_mileage_pick_points.begin(), begin_it));
    if (first_segment > 0) --first_segment;
    const auto end_it = std::upper_bound(
        scene_mileage_pick_points.begin(), scene_mileage_pick_points.end(), visible_max,
        [](double distance, const SceneMileagePickPoint& point) {
            return distance < point.distance;
        });
    const size_t segment_end = static_cast<size_t>(
        std::distance(scene_mileage_pick_points.begin(), end_it));

    double nearest_ray_parameter = std::numeric_limits<double>::infinity();
    double nearest_distance = 0.0;
    auto consider_triangle = [&](DVec3 a, DVec3 b, DVec3 c,
                                 double distance_a,
                                 double distance_b,
                                 double distance_c) {
        double ray_parameter = 0.0;
        double barycentric_b = 0.0;
        double barycentric_c = 0.0;
        if (!scene_ray_triangle_intersection(
                ray_origin, ray_direction, a, b, c,
                ray_parameter, barycentric_b, barycentric_c) ||
            ray_parameter >= nearest_ray_parameter) {
            return;
        }
        const double barycentric_a = 1.0 - barycentric_b - barycentric_c;
        const double distance = distance_a * barycentric_a +
            distance_b * barycentric_b + distance_c * barycentric_c;
        if (!std::isfinite(distance) || distance < visible_min || distance > visible_max) {
            return;
        }
        nearest_ray_parameter = ray_parameter;
        nearest_distance = distance;
    };

    for (size_t i = first_segment;
         i + 1 < scene_mileage_pick_points.size() && i < segment_end; ++i) {
        const SceneMileagePickPoint& a = scene_mileage_pick_points[i];
        const SceneMileagePickPoint& b = scene_mileage_pick_points[i + 1];
        if (b.distance < visible_min || a.distance > visible_max) continue;
        consider_triangle(a.left, a.right, b.right,
                          a.distance, a.distance, b.distance);
        consider_triangle(a.left, b.right, b.left,
                          a.distance, b.distance, b.distance);
    }
    if (!std::isfinite(nearest_ray_parameter)) return std::nullopt;

    const double first_integer_distance = std::ceil(scene_data.min_distance);
    const double last_integer_distance = std::floor(scene_data.max_distance);
    if (first_integer_distance > last_integer_distance) return std::nullopt;
    return std::clamp(std::round(nearest_distance),
                      first_integer_distance, last_integer_distance);
}

bool Canvas3D::Impl::scene_camera_ray(ImVec2 mouse_local, int width, int height,
                      DVec3& ray_origin, DVec3& ray_direction) const {
    if (width <= 0 || height <= 0) return false;
    const double ndc_x = 2.0 * static_cast<double>(mouse_local.x) /
        static_cast<double>(width) - 1.0;
    const double ndc_y = 1.0 - 2.0 * static_cast<double>(mouse_local.y) /
        static_cast<double>(height);
    const double aspect = static_cast<double>(width) / static_cast<double>(height);
    const double tan_half_fov = std::tan(static_cast<double>(k_scene_camera_fov_y) * 0.5);
    DVec3 forward = dvec3_from_vec3(scene_forward());
    DVec3 right = dvec3_from_vec3(scene_right());
    DVec3 up = normalize(cross(right, forward));
    ray_origin = scene_camera_pos;
    ray_direction = normalize(forward + right * (ndc_x * aspect * tan_half_fov) +
                              up * (ndc_y * tan_half_fov));
    return std::isfinite(ray_direction.x) && std::isfinite(ray_direction.y) &&
        std::isfinite(ray_direction.z);
}

bool Canvas3D::Impl::closest_axis_parameter(DVec3 axis_origin, DVec3 axis_direction,
                                   DVec3 ray_origin, DVec3 ray_direction,
                                   double& parameter) {
    const double axis_length_squared = dot(axis_direction, axis_direction);
    if (!std::isfinite(axis_length_squared) || axis_length_squared <= 1e-12) {
        return false;
    }
    const double axis_length = std::sqrt(axis_length_squared);
    axis_direction = normalize(axis_direction);
    ray_direction = normalize(ray_direction);
    DVec3 w = axis_origin - ray_origin;
    const double a = dot(axis_direction, axis_direction);
    const double b = dot(axis_direction, ray_direction);
    const double c = dot(ray_direction, ray_direction);
    const double d = dot(axis_direction, w);
    const double e = dot(ray_direction, w);
    const double denominator = a * c - b * b;
    if (!std::isfinite(denominator) || denominator < 1e-4) return false;
    parameter = (b * e - c * d) / denominator / axis_length;
    return std::isfinite(parameter);
}

float Canvas3D::Impl::point_segment_distance_sq(ImVec2 point, ImVec2 a, ImVec2 b,
                                               float* segment_fraction) {
    const float vx = b.x - a.x;
    const float vy = b.y - a.y;
    const float length_sq = vx * vx + vy * vy;
    float t = 0.0f;
    if (length_sq > 1e-6f) {
        t = std::clamp(((point.x - a.x) * vx + (point.y - a.y) * vy) / length_sq,
                       0.0f, 1.0f);
    }
    if (segment_fraction) *segment_fraction = t;
    const float dx = point.x - (a.x + vx * t);
    const float dy = point.y - (a.y + vy * t);
    return dx * dx + dy * dy;
}

Mat4 Canvas3D::Impl::scene_gizmo_view_projection(int width, int height) const {
    const Mat4 view = look_to_bve({0.0f, 0.0f, 0.0f}, scene_forward(), {0.0f, 1.0f, 0.0f});
    const float aspect = static_cast<float>(width) / static_cast<float>(height);
    const Mat4 projection = perspective_fov_lh_reverse_z(
        k_scene_camera_fov_y, aspect, k_scene_near_z,
        scene_far_z(effective_scene_window_forward_m()));
    return multiply(view, projection);
}

bool Canvas3D::Impl::update_scene_gizmo_projection(SceneGizmoHandle& gizmo,
                                   bool visible,
                                   int width,
                                   int height) {
    for (SceneGizmoAxisProjection& projection : gizmo.projection) {
        projection = SceneGizmoAxisProjection{};
    }
    if (!visible || width <= 0 || height <= 0) {
        return false;
    }

    const DVec3 relative_origin = gizmo.origin - scene_camera_pos;
    const DVec3 camera_forward = dvec3_from_vec3(scene_forward());
    const double depth = dot(relative_origin, camera_forward);
    if (!std::isfinite(depth) || depth <= static_cast<double>(k_scene_near_z)) return false;

    const Mat4 view_proj = scene_gizmo_view_projection(width, height);
    ImVec2 origin_screen;
    if (!project_scene_point(relative_origin, view_proj, width, height, origin_screen) ||
        origin_screen.x < 0.0f || origin_screen.y < 0.0f ||
        origin_screen.x > static_cast<float>(width) ||
        origin_screen.y > static_cast<float>(height)) {
        return false;
    }

    const double generic_world_units_per_pixel =
        2.0 * depth * std::tan(static_cast<double>(k_scene_camera_fov_y) * 0.5) /
        static_cast<double>(height);
    static constexpr std::array<ImVec2, 3> fallback_directions = {
        ImVec2(0.8660254f, 0.5f),
        ImVec2(0.0f, -1.0f),
        ImVec2(-0.8660254f, 0.5f)
    };
    const DVec3 to_camera = scene_camera_pos - gizmo.origin;
    const float gizmo_scale = scene_edit_component_scale;
    bool any = false;
    for (size_t i = 0; i < gizmo.axes.size(); ++i) {
        if (!gizmo.enabled[i]) continue;
        const DVec3 parameter_axis = gizmo.axes[i];
        const double axis_length_squared = dot(parameter_axis, parameter_axis);
        if (!std::isfinite(axis_length_squared) || axis_length_squared <= 1e-12) {
            continue;
        }
        const double axis_length = std::sqrt(axis_length_squared);
        const DVec3 parameter_direction = parameter_axis * (1.0 / axis_length);
        const double parameter_sign =
            dot(parameter_direction, to_camera) < 0.0 ? -1.0 : 1.0;
        const DVec3 visual_axis = parameter_axis * parameter_sign;
        ImVec2 one_parameter_screen;
        const bool projected = project_scene_point(relative_origin + visual_axis, view_proj,
                                                    width, height,
                                                    one_parameter_screen);
        float dx = projected ? one_parameter_screen.x - origin_screen.x : 0.0f;
        float dy = projected ? one_parameter_screen.y - origin_screen.y : 0.0f;
        const float projected_length = std::sqrt(dx * dx + dy * dy);
        ImVec2 direction(fallback_directions[i].x * static_cast<float>(parameter_sign),
                         fallback_directions[i].y * static_cast<float>(parameter_sign));
        if (projected_length >= 1.0f) {
            direction = ImVec2(dx / projected_length, dy / projected_length);
        }
        SceneGizmoAxisProjection& axis_projection = gizmo.projection[i];
        axis_projection.valid = true;
        axis_projection.ray_drag_reliable = projected_length >= 4.0f;
        axis_projection.direction = direction;
        axis_projection.world_direction = visual_axis;
        axis_projection.parameter_sign = parameter_sign;
        axis_projection.begin = ImVec2(
            origin_screen.x + direction.x * k_scene_gizmo_origin_gap_px * gizmo_scale,
            origin_screen.y + direction.y * k_scene_gizmo_origin_gap_px * gizmo_scale);
        axis_projection.end = ImVec2(
            origin_screen.x + direction.x * k_scene_gizmo_length_px * gizmo_scale,
            origin_screen.y + direction.y * k_scene_gizmo_length_px * gizmo_scale);
        axis_projection.parameter_units_per_pixel = projected_length >= 1.0f
            ? 1.0 / static_cast<double>(projected_length)
            : generic_world_units_per_pixel / axis_length;
        any = true;
    }
    return any;
}

bool Canvas3D::Impl::scene_placement_rotation_available() const {
    return scene_structure_edit.active && scene_structure_edit.show_gizmo &&
        scene_structure_edit.current.placement_rotation_gizmo;
}

std::optional<Canvas3DPlacementGizmoMode> Canvas3D::Impl::scene_placement_gizmo_mode() const {
    if (!scene_placement_rotation_available()) return std::nullopt;
    return scene_structure_edit.rotation_mode ? Canvas3DPlacementGizmoMode::Rotate
                                              : Canvas3DPlacementGizmoMode::Translate;
}

bool Canvas3D::Impl::set_scene_placement_gizmo_mode(Canvas3DPlacementGizmoMode mode) {
    if (!scene_placement_rotation_available() ||
        scene_structure_edit.dragging_gizmo != SceneGizmoTarget::None ||
        scene_structure_edit.rotation_waiting_for_release) {
        return false;
    }
    scene_structure_edit.rotation_mode = mode == Canvas3DPlacementGizmoMode::Rotate;
    return true;
}

bool Canvas3D::Impl::update_scene_rotation_projection(int width, int height) {
    SceneRotationGizmo& gizmo = scene_structure_edit.rotation_gizmo;
    for (SceneRotationRing& ring : gizmo.rings) {
        ring.point_count = 0;
        ring.closed = false;
    }
    if (width <= 0 || height <= 0) return false;

    const DVec3 relative_origin = gizmo.origin - scene_camera_pos;
    const DVec3 camera_forward = dvec3_from_vec3(scene_forward());
    const double depth = dot(relative_origin, camera_forward);
    if (depth <= k_scene_near_z) return false;

    const Mat4 view_proj = scene_gizmo_view_projection(width, height);
    ImVec2 center;
    if (!project_scene_point(relative_origin, view_proj, width, height, center) ||
        center.x < 0.0f || center.y < 0.0f ||
        center.x > static_cast<float>(width) || center.y > static_cast<float>(height)) {
        return false;
    }

    const double units_per_pixel = 2.0 * depth * std::tan(k_scene_camera_fov_y * 0.5) / height;
    gizmo.radius_px = k_scene_rotation_radius_px * scene_edit_component_scale;
    const double radius = units_per_pixel * gizmo.radius_px;
    const DVec3 view_direction = normalize(relative_origin);
    const DVec3 xy_cross = cross(gizmo.axes[0], gizmo.axes[1]);
    const bool split_xy = dot(xy_cross, xy_cross) <= 1e-12;

    bool any = false;
    for (size_t axis = 0; axis < gizmo.rings.size(); ++axis) {
        SceneRotationRing& ring = gizmo.rings[axis];
        const DVec3 normal = gizmo.axes[axis];
        const bool split = split_xy && axis < 2;
        const DVec3 circle_normal = split ? gizmo.axes[0] : normal;
        const DVec3 reference = std::abs(circle_normal.y) < 0.9 ? DVec3{0.0, 1.0, 0.0}
                                                               : DVec3{1.0, 0.0, 0.0};
        const DVec3 u = normalize(cross(reference, circle_normal));
        const DVec3 v = cross(circle_normal, u);
        ring.edge_on = std::abs(dot(normal, view_direction)) < 0.15;

        // View depth around the ring is sinusoidal, with its minimum at the arc midpoint.
        const double depth_u = dot(u, camera_forward);
        const double depth_v = dot(v, camera_forward);
        const double depth_amplitude = std::hypot(depth_u, depth_v);
        const bool full_ring = depth_amplitude <= k_scene_rotation_rear_radius_fraction;
        const double middle = depth_amplitude > 1e-12 ? std::atan2(-depth_v, -depth_u) : 0.0;
        const double half_arc = full_ring ? k_scene_rotation_pi
            : std::acos(-k_scene_rotation_rear_radius_fraction / depth_amplitude);
        double start = split && axis == 1 ? middle : middle - half_arc;
        double end = split && axis == 0 ? middle : middle + half_arc;
        if (split && dot(circle_normal, normal) < 0.0) std::swap(start, end);

        const size_t segment_count = (ring.points.size() - 1) / (split ? 2 : 1);
        ring.point_count = segment_count + 1;
        ring.closed = full_ring && !split;
        const double step = (end - start) / static_cast<double>(segment_count);
        const double step_cos = std::cos(step);
        const double step_sin = std::sin(step);
        double angle_cos = std::cos(start);
        double angle_sin = std::sin(start);
        bool previous_visible = false;
        for (size_t i = 0; i < ring.point_count; ++i) {
            SceneRotationRing::Point& projected = ring.points[i];
            const DVec3 point = relative_origin + (u * angle_cos + v * angle_sin) * radius;
            const double point_depth = dot(point, camera_forward);
            const bool visible = point_depth > k_scene_near_z &&
                project_scene_point(point, view_proj, width, height, projected.screen);
            projected.inverse_depth = visible ? 1.0 / point_depth : 0.0;
            any = any || (previous_visible && visible);
            previous_visible = visible;
            ring.closed = ring.closed && visible;
            const double next_cos = angle_cos * step_cos - angle_sin * step_sin;
            angle_sin = angle_sin * step_cos + angle_cos * step_sin;
            angle_cos = next_cos;
        }
        if (ring.closed) ring.points[segment_count] = ring.points[0];
    }
    return any;
}

std::optional<Canvas3D::Impl::SceneRotationHit> Canvas3D::Impl::pick_scene_rotation_ring(
    ImVec2 mouse_local, Canvas3DSceneDragAxis previous_axis) const {
    const auto& rings = scene_structure_edit.rotation_gizmo.rings;
    const float hit_radius = (k_scene_rotation_line_width_px * 0.5f +
                              k_scene_rotation_hit_padding_px) * scene_edit_component_scale;
    const float hit_radius_sq = hit_radius * hit_radius;
    struct Candidate {
        float distance_sq = std::numeric_limits<float>::infinity();
        double inverse_depth = 0.0;
        ImVec2 tangent;
    };
    std::array<Candidate, 3> candidates;
    float nearest_distance_sq = std::numeric_limits<float>::infinity();
    for (size_t axis = 0; axis < rings.size(); ++axis) {
        const SceneRotationRing& ring = rings[axis];
        Candidate& nearest = candidates[axis];
        for (size_t i = 1; i < ring.point_count; ++i) {
            const auto& begin = ring.points[i - 1];
            const auto& end = ring.points[i];
            if (begin.inverse_depth == 0.0 || end.inverse_depth == 0.0) continue;
            const ImVec2 tangent{end.screen.x - begin.screen.x, end.screen.y - begin.screen.y};
            if (tangent.x * tangent.x + tangent.y * tangent.y <= 1e-6f) continue;
            float fraction;
            const float distance_sq = point_segment_distance_sq(mouse_local, begin.screen, end.screen,
                                                                &fraction);
            if (distance_sq > hit_radius_sq || distance_sq > nearest.distance_sq) continue;
            // Reciprocal depth interpolates linearly along the projected segment.
            const double inverse_depth = begin.inverse_depth +
                (end.inverse_depth - begin.inverse_depth) * fraction;
            if (distance_sq < nearest.distance_sq || inverse_depth > nearest.inverse_depth) {
                nearest = {distance_sq, inverse_depth, tangent};
            }
        }
        nearest_distance_sq = std::min(nearest_distance_sq, nearest.distance_sq);
    }
    if (!std::isfinite(nearest_distance_sq)) return std::nullopt;

    const float tie_distance = std::sqrt(nearest_distance_sq) + k_scene_rotation_hover_tie_px;
    const float tie_distance_sq = tie_distance * tie_distance;
    const int previous_index = structure_drag_axis_index(previous_axis);
    if (previous_index >= 0 && candidates[static_cast<size_t>(previous_index)].distance_sq <= tie_distance_sq) {
        return SceneRotationHit{previous_axis, nearest_distance_sq,
                                candidates[static_cast<size_t>(previous_index)].tangent};
    }

    size_t winner = 0;
    double nearest_inverse_depth = 0.0;
    for (size_t axis = 0; axis < candidates.size(); ++axis) {
        const Candidate& candidate = candidates[axis];
        if (candidate.distance_sq <= tie_distance_sq && candidate.inverse_depth > nearest_inverse_depth) {
            winner = axis;
            nearest_inverse_depth = candidate.inverse_depth;
        }
    }
    return SceneRotationHit{structure_drag_axis_from_index(winner), nearest_distance_sq,
                            candidates[winner].tangent};
}

bool Canvas3D::Impl::update_scene_structure_gizmo_projections(int width, int height) {
    const bool placement_visible =
        scene_structure_edit.active && scene_structure_edit.show_gizmo;
    const bool rotation_visible = scene_structure_edit.rotation_mode &&
        scene_placement_rotation_available();
    const bool repeater_end_visible =
        scene_structure_edit.active &&
        scene_structure_edit.kind == Canvas3DSceneEditKind::Repeater &&
        scene_structure_edit.current.has_repeater_end_distance;
    const bool placement = update_scene_gizmo_projection(
        scene_structure_edit.placement_gizmo,
        placement_visible && !rotation_visible, width, height);
    const bool repeater_end = update_scene_gizmo_projection(
        scene_structure_edit.repeater_end_gizmo,
        repeater_end_visible, width, height);
    const bool rotation = rotation_visible && update_scene_rotation_projection(width, height);
    return placement || repeater_end || rotation;
}

SceneGizmoHandle* Canvas3D::Impl::scene_gizmo_handle(SceneGizmoTarget target) {
    if (target == SceneGizmoTarget::Placement) {
        return &scene_structure_edit.placement_gizmo;
    }
    if (target == SceneGizmoTarget::RepeaterEndDistance) {
        return &scene_structure_edit.repeater_end_gizmo;
    }
    return nullptr;
}

std::optional<DVec3> Canvas3D::Impl::scene_rotation_radial(ImVec2 mouse_local,
                                                        int width, int height) const {
    DVec3 ray_origin;
    DVec3 ray_direction;
    if (!scene_camera_ray(mouse_local, width, height, ray_origin, ray_direction)) return std::nullopt;
    const DVec3 normal = scene_structure_edit.drag_axis_direction;
    const DVec3 center_from_eye = scene_structure_edit.drag_axis_origin - ray_origin;
    const double denominator = dot(normal, ray_direction);
    if (std::abs(denominator) < 1e-5) return std::nullopt;
    const double distance = dot(normal, center_from_eye) / denominator;
    if (distance <= k_scene_near_z) return std::nullopt;
    const DVec3 radial = ray_direction * distance - center_from_eye;
    const double center_dead_zone_sq = dot(center_from_eye, center_from_eye) * 1e-6;
    if (dot(radial, radial) <= center_dead_zone_sq) return std::nullopt;
    return normalize(radial);
}

std::optional<Canvas3DPlacementDragUpdate> Canvas3D::Impl::begin_scene_rotation_drag(
    ImVec2 mouse_local, int width, int height, ImVec2 screen_tangent) {
    SceneStructureEditState& edit = scene_structure_edit;
    const size_t index = static_cast<size_t>(structure_drag_axis_index(edit.hovered_axis));
    const SceneRotationRing& ring = edit.rotation_gizmo.rings[index];
    edit.drag_axis_origin = edit.rotation_gizmo.origin;
    edit.drag_axis_direction = edit.rotation_gizmo.axes[index];
    edit.drag_start_mouse = mouse_local;
    edit.drag_start_value = edit.current.*k_scene_rotation_fields[index];
    edit.rotation_drag = {};
    if (ring.edge_on) {
        const float length = std::hypot(screen_tangent.x, screen_tangent.y);
        edit.rotation_drag.screen_tangent = {screen_tangent.x / length, screen_tangent.y / length};
        edit.rotation_drag.screen_radius_px = edit.rotation_gizmo.radius_px;
    } else {
        edit.rotation_drag.sample_radial(edit.drag_axis_direction,
                                         scene_rotation_radial(mouse_local, width, height));
        if (!edit.rotation_drag.radial_valid) return std::nullopt;
    }
    edit.dragging_gizmo = SceneGizmoTarget::Rotation;
    edit.dragging_axis = edit.hovered_axis;
    return scene_rotation_drag_result(Canvas3DSceneDragPhase::Begin);
}

bool Canvas3D::Impl::write_scene_placement_gizmo_preview() {
    const SceneStructureEditState& edit = scene_structure_edit;
    if (edit.kind == Canvas3DSceneEditKind::Repeater) {
        return write_scene_repeater_segment(edit.edit_id,
            repeater_segment_from_target(edit.current, edit.baseline_repeater));
    }
    return write_scene_placement_instance(edit.edit_id,
        placement_instance_from_target(edit.current, edit.baseline_instance));
}

bool Canvas3D::Impl::write_scene_rotation_angle(double angle) {
    SceneStructureEditState& edit = scene_structure_edit;
    const size_t index = static_cast<size_t>(structure_drag_axis_index(edit.dragging_axis));
    double& current = edit.current.*k_scene_rotation_fields[index];
    if (current == angle) return false;
    const double previous = current;
    current = angle;
    if (write_scene_placement_gizmo_preview()) return true;
    current = previous;
    return false;
}

Canvas3DPlacementDragUpdate Canvas3D::Impl::scene_rotation_drag_result(
    Canvas3DSceneDragPhase phase) const {
    const SceneStructureEditState& edit = scene_structure_edit;
    const size_t index = static_cast<size_t>(structure_drag_axis_index(edit.dragging_axis));
    Canvas3DPlacementDragUpdate result;
    result.kind = edit.kind;
    result.edit_id = edit.edit_id;
    result.target = Canvas3DSceneDragTarget::PlacementRotation;
    result.axis = edit.dragging_axis;
    result.angle_degrees = edit.current.*k_scene_rotation_fields[index];
    result.phase = phase;
    return result;
}

Canvas3DPlacementDragUpdate Canvas3D::Impl::cancel_scene_rotation_drag() {
    SceneStructureEditState& edit = scene_structure_edit;
    if (scene_placement_rotation_available()) write_scene_rotation_angle(edit.drag_start_value);
    auto result = scene_rotation_drag_result(Canvas3DSceneDragPhase::Cancel);
    cancel_scene_gizmo_interaction(SceneGizmoTarget::Rotation);
    edit.rotation_waiting_for_release = ImGui::IsMouseDown(ImGuiMouseButton_Left);
    return result;
}

std::optional<Canvas3DPlacementDragUpdate> Canvas3D::Impl::update_scene_rotation_drag(
    ImVec2 mouse_local, int width, int height) {
    SceneStructureEditState& edit = scene_structure_edit;
    const ImGuiIO& io = ImGui::GetIO();
    const bool released = ImGui::IsMouseReleased(ImGuiMouseButton_Left);
    if (!scene_placement_rotation_available() || io.AppFocusLost ||
        !ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) ||
        ImGui::IsKeyPressed(ImGuiKey_Escape, false) ||
        (!released && !ImGui::IsMouseDown(ImGuiMouseButton_Left))) {
        return cancel_scene_rotation_drag();
    }

    SceneRotationDrag& drag = edit.rotation_drag;
    if (drag.screen_radius_px == 0.0) {
        drag.sample_radial(edit.drag_axis_direction, ImGui::IsMousePosValid()
            ? scene_rotation_radial(mouse_local, width, height) : std::nullopt);
    } else if (ImGui::IsMousePosValid()) {
        const double dx = static_cast<double>(mouse_local.x) - edit.drag_start_mouse.x;
        const double dy = static_cast<double>(mouse_local.y) - edit.drag_start_mouse.y;
        drag.radians = (dx * drag.screen_tangent.x + dy * drag.screen_tangent.y) / drag.screen_radius_px;
    }
    const bool changed = write_scene_rotation_angle(drag.angle_degrees(edit.drag_start_value, io.KeyShift));
    if (released) {
        auto result = scene_rotation_drag_result(Canvas3DSceneDragPhase::Finish);
        cancel_scene_gizmo_interaction(SceneGizmoTarget::Rotation);
        return result;
    }
    if (changed) return scene_rotation_drag_result(Canvas3DSceneDragPhase::Update);
    return std::nullopt;
}

std::optional<Canvas3DPlacementDragUpdate> Canvas3D::Impl::handle_scene_structure_gizmo_input(
    bool canvas_hovered, int width, int height, ImVec2 mouse_local) {
    if (scene_structure_edit.dragging_gizmo == SceneGizmoTarget::Rotation) {
        return update_scene_rotation_drag(mouse_local, width, height);
    }
    if (scene_structure_edit.rotation_waiting_for_release) {
        scene_structure_edit.rotation_waiting_for_release = ImGui::IsMouseDown(ImGuiMouseButton_Left);
        if (scene_structure_edit.rotation_waiting_for_release) return std::nullopt;
    }
    if (!scene_placement_rotation_available()) scene_structure_edit.rotation_mode = false;
    if (!update_scene_structure_gizmo_projections(width, height)) {
        scene_structure_edit.hovered_gizmo = SceneGizmoTarget::None;
        scene_structure_edit.hovered_axis = Canvas3DSceneDragAxis::None;
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            scene_structure_edit.dragging_gizmo = SceneGizmoTarget::None;
            scene_structure_edit.dragging_axis = Canvas3DSceneDragAxis::None;
        }
        return std::nullopt;
    }

    if (scene_structure_edit.dragging_gizmo == SceneGizmoTarget::None) {
        const Canvas3DSceneDragAxis previous_rotation_axis =
            scene_structure_edit.hovered_gizmo == SceneGizmoTarget::Rotation
                ? scene_structure_edit.hovered_axis : Canvas3DSceneDragAxis::None;
        scene_structure_edit.hovered_gizmo = SceneGizmoTarget::None;
        scene_structure_edit.hovered_axis = Canvas3DSceneDragAxis::None;
        ImVec2 rotation_tangent;
        if (canvas_hovered) {
            const float hit_radius = k_scene_gizmo_hit_radius_px * scene_edit_component_scale;
            const float translation_hit_distance_sq = hit_radius * hit_radius;
            float best_distance_sq = translation_hit_distance_sq;
            const auto consider = [&](SceneGizmoTarget target,
                                      const SceneGizmoHandle& gizmo) {
                for (size_t i = 0; i < gizmo.projection.size(); ++i) {
                    const SceneGizmoAxisProjection& projection = gizmo.projection[i];
                    if (!projection.valid) continue;
                    const float distance_sq = point_segment_distance_sq(
                        mouse_local, projection.begin, projection.end);
                    if (distance_sq <= std::min(best_distance_sq, translation_hit_distance_sq)) {
                        best_distance_sq = distance_sq;
                        scene_structure_edit.hovered_gizmo = target;
                        scene_structure_edit.hovered_axis =
                            structure_drag_axis_from_index(i);
                    }
                }
            };
            consider(SceneGizmoTarget::Placement,
                     scene_structure_edit.placement_gizmo);
            if (scene_structure_edit.rotation_mode) {
                if (const auto hit = pick_scene_rotation_ring(mouse_local, previous_rotation_axis)) {
                    best_distance_sq = hit->nearest_distance_sq;
                    scene_structure_edit.hovered_gizmo = SceneGizmoTarget::Rotation;
                    scene_structure_edit.hovered_axis = hit->axis;
                    rotation_tangent = hit->tangent;
                }
            }
            // Check the EndDistance handle last so an exact overlap selects
            // the endpoint and a zero-length Repeater can still be extended.
            consider(SceneGizmoTarget::RepeaterEndDistance,
                     scene_structure_edit.repeater_end_gizmo);
        }

        if (scene_structure_edit.hovered_gizmo != SceneGizmoTarget::None &&
            scene_structure_edit.hovered_axis != Canvas3DSceneDragAxis::None &&
            ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            if (scene_structure_edit.hovered_gizmo == SceneGizmoTarget::Rotation) {
                return begin_scene_rotation_drag(mouse_local, width, height, rotation_tangent);
            }
            const int axis_index = structure_drag_axis_index(scene_structure_edit.hovered_axis);
            SceneGizmoHandle* gizmo =
                scene_gizmo_handle(scene_structure_edit.hovered_gizmo);
            if (!gizmo || axis_index < 0) return std::nullopt;
            const SceneGizmoAxisProjection& projection =
                gizmo->projection[static_cast<size_t>(axis_index)];
            scene_structure_edit.dragging_gizmo =
                scene_structure_edit.hovered_gizmo;
            scene_structure_edit.dragging_axis = scene_structure_edit.hovered_axis;
            scene_structure_edit.drag_axis_origin = gizmo->origin;
            scene_structure_edit.drag_axis_direction = projection.world_direction;
            scene_structure_edit.drag_start_mouse = mouse_local;
            scene_structure_edit.drag_screen_direction = projection.direction;
            scene_structure_edit.drag_parameter_units_per_pixel =
                projection.parameter_units_per_pixel;
            scene_structure_edit.drag_parameter_sign = projection.parameter_sign;
            if (scene_structure_edit.dragging_gizmo ==
                SceneGizmoTarget::RepeaterEndDistance) {
                scene_structure_edit.drag_start_value =
                    scene_structure_edit.current.repeater_end_distance;
            } else if (scene_structure_edit.kind ==
                       Canvas3DSceneEditKind::StructurePutBetween) {
                scene_structure_edit.drag_start_value =
                    scene_structure_edit.current.distance;
            } else if (scene_structure_edit.kind ==
                       Canvas3DSceneEditKind::Sound3D &&
                       scene_structure_edit.dragging_axis ==
                       Canvas3DSceneDragAxis::Z) {
                scene_structure_edit.drag_start_value =
                    scene_structure_edit.current.distance;
            } else if (scene_structure_edit.current.placement_distance_gizmo) {
                scene_structure_edit.drag_start_value =
                    scene_structure_edit.current.distance;
            } else {
                scene_structure_edit.drag_start_value = axis_index == 0
                    ? scene_structure_edit.current.x
                    : axis_index == 1 ? scene_structure_edit.current.y
                                      : scene_structure_edit.current.z;
            }
            DVec3 ray_origin;
            DVec3 ray_direction;
            scene_structure_edit.drag_uses_ray =
                projection.ray_drag_reliable &&
                scene_camera_ray(mouse_local, width, height, ray_origin, ray_direction) &&
                closest_axis_parameter(scene_structure_edit.drag_axis_origin,
                                       scene_structure_edit.drag_axis_direction,
                                       ray_origin, ray_direction,
                                       scene_structure_edit.drag_start_axis_parameter);
        }
    }

    if (scene_structure_edit.dragging_gizmo == SceneGizmoTarget::None ||
        scene_structure_edit.dragging_axis == Canvas3DSceneDragAxis::None) {
        return std::nullopt;
    }
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        scene_structure_edit.dragging_gizmo = SceneGizmoTarget::None;
        scene_structure_edit.dragging_axis = Canvas3DSceneDragAxis::None;
        return std::nullopt;
    }

    double delta = 0.0;
    bool used_ray = false;
    if (scene_structure_edit.drag_uses_ray) {
        DVec3 ray_origin;
        DVec3 ray_direction;
        double current_parameter = 0.0;
        if (scene_camera_ray(mouse_local, width, height, ray_origin, ray_direction) &&
            closest_axis_parameter(scene_structure_edit.drag_axis_origin,
                                   scene_structure_edit.drag_axis_direction,
                                   ray_origin, ray_direction, current_parameter)) {
            delta = current_parameter - scene_structure_edit.drag_start_axis_parameter;
            used_ray = true;
        }
    }
    if (!used_ray) {
        const double screen_delta_x =
            static_cast<double>(mouse_local.x - scene_structure_edit.drag_start_mouse.x);
        const double screen_delta_y =
            static_cast<double>(mouse_local.y - scene_structure_edit.drag_start_mouse.y);
        delta = (screen_delta_x * scene_structure_edit.drag_screen_direction.x +
                 screen_delta_y * scene_structure_edit.drag_screen_direction.y) *
            scene_structure_edit.drag_parameter_units_per_pixel;
    }
    delta *= scene_structure_edit.drag_parameter_sign;

    const bool put_between_distance_drag =
        scene_structure_edit.dragging_gizmo == SceneGizmoTarget::Placement &&
        scene_structure_edit.kind ==
        Canvas3DSceneEditKind::StructurePutBetween;
    const bool repeater_end_distance_drag =
        scene_structure_edit.dragging_gizmo ==
        SceneGizmoTarget::RepeaterEndDistance;
    const bool placement_distance_drag =
        scene_structure_edit.dragging_gizmo == SceneGizmoTarget::Placement &&
        scene_structure_edit.current.placement_distance_gizmo;
    const bool sound3d_distance_drag =
        scene_structure_edit.dragging_gizmo == SceneGizmoTarget::Placement &&
        scene_structure_edit.kind == Canvas3DSceneEditKind::Sound3D &&
        scene_structure_edit.dragging_axis == Canvas3DSceneDragAxis::Z;
    const bool distance_drag =
        put_between_distance_drag || repeater_end_distance_drag ||
        placement_distance_drag || sound3d_distance_drag;
    const double candidate = distance_drag
        ? std::round(scene_structure_edit.drag_start_value + delta)
        : truncate_gui_thousandths(scene_structure_edit.drag_start_value + delta);
    const int axis_index = structure_drag_axis_index(scene_structure_edit.dragging_axis);
    double* current_value = repeater_end_distance_drag
        ? &scene_structure_edit.current.repeater_end_distance
        : put_between_distance_drag || placement_distance_drag ||
        sound3d_distance_drag
        ? &scene_structure_edit.current.distance
        : axis_index == 0 ? &scene_structure_edit.current.x
        : axis_index == 1 ? &scene_structure_edit.current.y
                          : &scene_structure_edit.current.z;
    const double change_threshold = distance_drag
        ? 0.5 : 0.000999999;
    if (std::abs(candidate - *current_value) < change_threshold) return std::nullopt;
    const double previous_value = *current_value;
    *current_value = candidate;
    const Canvas3DSceneDragAxis changed_axis = scene_structure_edit.dragging_axis;
    bool wrote = false;
    if (put_between_distance_drag) {
        const std::uint64_t sequence = queue_scene_put_between_preview(
            scene_structure_edit.current);
        wrote = sequence != 0;
        if (wrote) scene_structure_edit.preview_sequence = sequence;
    } else if (scene_structure_edit.kind == Canvas3DSceneEditKind::Sound3D) {
        SceneGizmoHandle gizmo;
        wrote = sound3d_gizmo_frame(scene_structure_edit.current, gizmo) &&
            update_scene_sound3d_marker(
                scene_structure_edit.edit_id,
                scene_structure_edit.current.distance,
                scene_structure_edit.current.x,
                scene_structure_edit.current.y);
        if (wrote) scene_structure_edit.placement_gizmo = gizmo;
    } else {
        wrote = write_scene_placement_gizmo_preview();
    }
    if (!wrote) {
        *current_value = previous_value;
        return std::nullopt;
    }

    Canvas3DPlacementDragUpdate result;
    result.kind = scene_structure_edit.kind;
    result.edit_id = scene_structure_edit.edit_id;
    result.target = repeater_end_distance_drag
        ? Canvas3DSceneDragTarget::RepeaterEndDistance
            : put_between_distance_drag
            ? Canvas3DSceneDragTarget::PutBetweenDistance
            : placement_distance_drag || sound3d_distance_drag
                ? Canvas3DSceneDragTarget::PlacementDistance
                : Canvas3DSceneDragTarget::Placement;
    result.axis = changed_axis;
    result.distance = scene_structure_edit.current.distance;
    result.repeater_end_distance =
        scene_structure_edit.current.repeater_end_distance;
    result.x = scene_structure_edit.current.x;
    result.y = scene_structure_edit.current.y;
    result.z = scene_structure_edit.current.z;
    return result;
}

bool Canvas3D::Impl::scene_structure_gizmo_consumes_left_input() const {
    return scene_structure_edit.dragging_gizmo != SceneGizmoTarget::None ||
        scene_structure_edit.hovered_gizmo != SceneGizmoTarget::None ||
        scene_structure_edit.rotation_waiting_for_release;
}

void Canvas3D::Impl::draw_scene_gizmo_handle(ImDrawList* draw,
                             ImVec2 canvas_origin,
                             const SceneGizmoHandle& gizmo,
                             SceneGizmoTarget target) {
    const float gizmo_scale = scene_edit_component_scale;
    for (size_t i = 0; i < gizmo.projection.size(); ++i) {
        const SceneGizmoAxisProjection& projection = gizmo.projection[i];
        if (!projection.valid) continue;
        const Canvas3DSceneDragAxis axis = structure_drag_axis_from_index(i);
        const bool active =
            (scene_structure_edit.dragging_gizmo == target &&
             scene_structure_edit.dragging_axis == axis) ||
            (scene_structure_edit.hovered_gizmo == target &&
             scene_structure_edit.hovered_axis == axis);
        const ImU32 color = active ? k_scene_gizmo_active_colors[i] : k_scene_gizmo_colors[i];
        ImVec2 begin(canvas_origin.x + projection.begin.x,
                     canvas_origin.y + projection.begin.y);
        ImVec2 end(canvas_origin.x + projection.end.x,
                   canvas_origin.y + projection.end.y);
        ImVec2 direction = projection.direction;
        ImVec2 perpendicular(-direction.y, direction.x);
        const float arrow_length = k_scene_gizmo_arrow_length_px * gizmo_scale;
        const float arrow_half_width = k_scene_gizmo_arrow_half_width_px * gizmo_scale;
        ImVec2 arrow_base(end.x - direction.x * arrow_length,
                          end.y - direction.y * arrow_length);
        draw->AddLine(begin, end, color, (active ? 4.5f : 3.0f) * gizmo_scale);
        draw->AddTriangleFilled(
            end,
            ImVec2(arrow_base.x + perpendicular.x * arrow_half_width,
                   arrow_base.y + perpendicular.y * arrow_half_width),
            ImVec2(arrow_base.x - perpendicular.x * arrow_half_width,
                   arrow_base.y - perpendicular.y * arrow_half_width),
            color);
    }
    const SceneGizmoAxisProjection* first = nullptr;
    for (const SceneGizmoAxisProjection& projection : gizmo.projection) {
        if (projection.valid) {
            first = &projection;
            break;
        }
    }
    if (first) {
        ImVec2 center(
            canvas_origin.x + first->begin.x -
                first->direction.x * k_scene_gizmo_origin_gap_px * gizmo_scale,
            canvas_origin.y + first->begin.y -
                first->direction.y * k_scene_gizmo_origin_gap_px * gizmo_scale);
        const float center_radius = k_scene_gizmo_center_radius_px * gizmo_scale;
        draw->AddCircleFilled(center, center_radius, IM_COL32(245, 245, 245, 235));
        draw->AddCircle(center, center_radius, IM_COL32(30, 30, 30, 220), 0,
                        gizmo_scale);
    }
}

void Canvas3D::Impl::draw_scene_rotation_gizmo(ImDrawList* draw, ImVec2 canvas_origin) const {
    const SceneStructureEditState& edit = scene_structure_edit;
    const auto& rings = edit.rotation_gizmo.rings;
    const Canvas3DSceneDragAxis active_axis = edit.dragging_gizmo == SceneGizmoTarget::Rotation
        ? edit.dragging_axis : edit.hovered_gizmo == SceneGizmoTarget::Rotation
        ? edit.hovered_axis : Canvas3DSceneDragAxis::None;
    const int active_index = structure_drag_axis_index(active_axis);
    const auto draw_ring = [&](size_t i, bool active) {
        const SceneRotationRing& ring = rings[i];
        const ImU32 color = active ? k_scene_gizmo_active_colors[i] : k_scene_gizmo_colors[i];
        const float thickness = (active ? k_scene_rotation_active_width_px :
            k_scene_rotation_line_width_px) * scene_edit_component_scale;
        const size_t point_count = ring.point_count - (ring.closed ? 1 : 0);
        for (size_t j = 0; j < point_count; ++j) {
            const auto& point = ring.points[j];
            if (point.inverse_depth == 0.0) {
                draw->PathStroke(color, thickness);
                continue;
            }
            if (ring.edge_on && j >= 2) {
                const auto& before = ring.points[j - 2];
                const auto& join = ring.points[j - 1];
                const float turn = (join.screen.x - before.screen.x) * (point.screen.x - join.screen.x) +
                    (join.screen.y - before.screen.y) * (point.screen.y - join.screen.y);
                if (before.inverse_depth != 0.0 && join.inverse_depth != 0.0 && turn < 0.0f) {
                    // A round join keeps the stroke width at a projected fold.
                    const ImVec2 center{canvas_origin.x + join.screen.x, canvas_origin.y + join.screen.y};
                    draw->PathStroke(color, thickness);
                    draw->AddCircleFilled(center, thickness * 0.5f, color);
                    draw->PathLineTo(center);
                }
            }
            draw->PathLineTo({canvas_origin.x + point.screen.x, canvas_origin.y + point.screen.y});
        }
        draw->PathStroke(color, thickness, ring.closed ? ImDrawFlags_Closed : ImDrawFlags_None);
    };
    for (size_t i = rings.size(); i-- > 0;) {
        if (static_cast<int>(i) != active_index) draw_ring(i, false);
    }
    if (active_index >= 0) draw_ring(static_cast<size_t>(active_index), true);
}

void Canvas3D::Impl::draw_scene_structure_gizmo(ImDrawList* draw, ImVec2 canvas_origin,
                                int width, int height) {
    if (!draw || !update_scene_structure_gizmo_projections(width, height)) return;
    if (scene_structure_edit.rotation_mode && scene_placement_rotation_available()) {
        draw->PushClipRect(canvas_origin, {canvas_origin.x + static_cast<float>(width),
                                         canvas_origin.y + static_cast<float>(height)}, true);
        draw_scene_rotation_gizmo(draw, canvas_origin);
        draw->PopClipRect();
    } else {
        draw_scene_gizmo_handle(draw, canvas_origin,
                                scene_structure_edit.placement_gizmo,
                                SceneGizmoTarget::Placement);
    }
    draw_scene_gizmo_handle(draw, canvas_origin, scene_structure_edit.repeater_end_gizmo,
                            SceneGizmoTarget::RepeaterEndDistance);
}
