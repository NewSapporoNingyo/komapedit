/*
 * Copyright (c) 2026 Sapporo_ningyo
 *
 * Licensed under Apache License 2.0; see LICENSE and NOTICE.
 * The GUI uses Dear ImGui; see THIRD_PARTY_NOTICES.md.
 */

#ifdef _MSC_VER
#pragma execution_character_set("utf-8")
#endif

#include "canvas2d_background.h"

#include "../main_window/kme.h"

#include "imgui_internal.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace canvas2d {
namespace {

constexpr double k_pi = 3.14159265358979323846;
constexpr double k_radians_per_degree = k_pi / 180.0;
constexpr double k_min_dimension = 0.001;
constexpr BackgroundHandle k_move{BackgroundHandle::Kind::Move};
constexpr BackgroundHandle k_rotate{BackgroundHandle::Kind::Rotate};
constexpr std::array<BackgroundHandle, 4> k_corners{{
    {BackgroundHandle::Kind::Resize, -1, -1},
    {BackgroundHandle::Kind::Resize, 1, -1},
    {BackgroundHandle::Kind::Resize, 1, 1},
    {BackgroundHandle::Kind::Resize, -1, 1},
}};
constexpr std::array<BackgroundHandle, 4> k_edges{{
    {BackgroundHandle::Kind::Resize, 0, -1},
    {BackgroundHandle::Kind::Resize, 1, 0},
    {BackgroundHandle::Kind::Resize, 0, 1},
    {BackgroundHandle::Kind::Resize, -1, 0},
}};

double rotation_radians(double degrees) {
    return std::remainder(degrees, 360.0) * k_radians_per_degree;
}

std::pair<double, double> rotate(double x, double y, double angle) {
    const double cosine = std::cos(angle);
    const double sine = std::sin(angle);
    return {cosine * x - sine * y, sine * x + cosine * y};
}

std::optional<BackgroundQuad> project_background(const BackgroundTransform& background,
                                                 const View2D& view, ImVec2 origin, ImVec2 size) {
    constexpr double max_coordinate = std::numeric_limits<float>::max();
    const double center_distance = std::hypot(background.x - view.cx, background.y - view.cy);
    if (!(center_distance * view.scale <= max_coordinate)) return std::nullopt;
    BackgroundQuad quad{{}, view.world_to_screen(background.x, background.y, origin, size),
                       view.rotation + rotation_radians(background.rotation_deg),
                       background.width * (view.scale * 0.5),
                       background.height * (view.scale * 0.5)};
    const double cosine = std::cos(quad.rotation);
    const double sine = std::sin(quad.rotation);
    for (size_t i = 0; i < quad.corners.size(); ++i) {
        const double x = k_corners[i].x_side * quad.half_width;
        const double y = k_corners[i].y_side * quad.half_height;
        const double sx = quad.center.x + cosine * x - sine * y;
        const double sy = quad.center.y + sine * x + cosine * y;
        if (!(std::abs(sx) <= max_coordinate && std::abs(sy) <= max_coordinate)) {
            return std::nullopt;
        }
        quad.corners[i] = ImVec2(static_cast<float>(sx), static_cast<float>(sy));
    }
    return quad;
}

ImVec2 edge_midpoint(const BackgroundQuad& quad, size_t edge) {
    const ImVec2 a = quad.corners[edge];
    const ImVec2 b = quad.corners[(edge + 1) % quad.corners.size()];
    return ImVec2(a.x * 0.5f + b.x * 0.5f, a.y * 0.5f + b.y * 0.5f);
}

ImVec2 rotation_handle_point(const BackgroundQuad& quad, float scale) {
    const ImVec2 top = edge_midpoint(quad, 0);
    const auto [dx, dy] = rotate(0.0, -28.0 * scale, quad.rotation);
    return ImVec2(top.x + static_cast<float>(dx), top.y + static_cast<float>(dy));
}

double point_distance_squared(ImVec2 a, ImVec2 b) {
    const double dx = static_cast<double>(a.x) - b.x;
    const double dy = static_cast<double>(a.y) - b.y;
    return dx * dx + dy * dy;
}

double edge_distance_squared(ImVec2 pointer, ImVec2 a, ImVec2 b) {
    const double dx = static_cast<double>(b.x) - a.x;
    const double dy = static_cast<double>(b.y) - a.y;
    const double length_squared = dx * dx + dy * dy;
    if (length_squared == 0.0) return point_distance_squared(pointer, a);
    const double px = static_cast<double>(pointer.x) - a.x;
    const double py = static_cast<double>(pointer.y) - a.y;
    const double t = std::clamp((px * dx + py * dy) / length_squared, 0.0, 1.0);
    const double offset_x = px - t * dx;
    const double offset_y = py - t * dy;
    return offset_x * offset_x + offset_y * offset_y;
}

std::optional<BackgroundHandle> hit_background(const BackgroundQuad& quad,
                                               ImVec2 pointer, float scale) {
    const double rotation_radius = 9.0 * scale;
    if (point_distance_squared(pointer, rotation_handle_point(quad, scale)) <=
        rotation_radius * rotation_radius) return k_rotate;

    std::optional<BackgroundHandle> hit;
    const double corner_radius = 9.0 * scale;
    double closest = corner_radius * corner_radius;
    for (size_t i = 0; i < quad.corners.size(); ++i) {
        const double distance = point_distance_squared(pointer, quad.corners[i]);
        if (distance <= closest) {
            closest = distance;
            hit = k_corners[i];
        }
    }
    if (hit) return hit;

    const double edge_radius = 7.0 * scale;
    closest = edge_radius * edge_radius;
    for (size_t i = 0; i < quad.corners.size(); ++i) {
        const auto next = (i + 1) % quad.corners.size();
        const double distance = edge_distance_squared(pointer, quad.corners[i], quad.corners[next]);
        if (distance <= closest) {
            closest = distance;
            hit = k_edges[i];
        }
    }
    if (hit) return hit;

    const auto [x, y] = rotate(static_cast<double>(pointer.x) - quad.center.x,
                               static_cast<double>(pointer.y) - quad.center.y, -quad.rotation);
    if (std::abs(x) <= quad.half_width && std::abs(y) <= quad.half_height) return k_move;
    return std::nullopt;
}

void set_background_cursor(const BackgroundHandle& handle, double rotation) {
    switch (handle.kind) {
        case BackgroundHandle::Kind::Move:
            set_move_cursor();
            break;
        case BackgroundHandle::Kind::Rotate:
            set_crosshair_cursor();
            break;
        case BackgroundHandle::Kind::Resize: {
            constexpr ImGuiMouseCursor directions[] = {
                ImGuiMouseCursor_ResizeEW, ImGuiMouseCursor_ResizeNWSE,
                ImGuiMouseCursor_ResizeNS, ImGuiMouseCursor_ResizeNESW,
            };
            const double angle = rotation + std::atan2(handle.y_side, handle.x_side);
            const long octant = std::lround(std::remainder(angle, k_pi) / (k_pi / 4.0));
            ImGui::SetMouseCursor(directions[(octant + 4) % 4]);
            break;
        }
    }
}

}  // namespace

bool BackgroundTransform::valid() const {
    return std::isfinite(x) && std::isfinite(y) && std::isfinite(rotation_deg) &&
        std::isfinite(width) && std::isfinite(height) && width > 0.0 && height > 0.0;
}

bool BackgroundTransform::operator==(const BackgroundTransform& other) const {
    return x == other.x && y == other.y && width == other.width &&
        height == other.height && rotation_deg == other.rotation_deg;
}

bool BackgroundHandle::operator==(const BackgroundHandle& other) const {
    return kind == other.kind && x_side == other.x_side && y_side == other.y_side;
}

BackgroundTransform BackgroundDrag::update(const View2D& view, ImVec2 origin, ImVec2 size,
                                            ImVec2 pointer, bool keep_ratio) {
    BackgroundTransform next = start;
    if (handle.kind == BackgroundHandle::Kind::Rotate) {
        const ImVec2 center = view.world_to_screen(start.x, start.y, origin, size);
        const double x = static_cast<double>(pointer.x) - center.x;
        const double y = static_cast<double>(pointer.y) - center.y;
        if (x != 0.0 || y != 0.0) {
            const double previous_x = static_cast<double>(previous_pointer.x) - center.x;
            const double previous_y = static_cast<double>(previous_pointer.y) - center.y;
            rotation_delta += std::atan2(previous_x * y - previous_y * x,
                                         previous_x * x + previous_y * y);
            previous_pointer = pointer;
        }
        next.rotation_deg += rotation_delta / k_radians_per_degree;
        return next;
    }

    const auto [dx, dy] = view.screen_delta_to_world(
        static_cast<double>(pointer.x) - start_pointer.x,
        static_cast<double>(pointer.y) - start_pointer.y);
    if (handle.kind == BackgroundHandle::Kind::Move) {
        next.x += dx;
        next.y += dy;
        return next;
    }

    const double angle = rotation_radians(start.rotation_deg);
    const auto [local_x, local_y] = rotate(dx, dy, -angle);
    const double width_change = local_x * handle.x_side;
    const double height_change = local_y * handle.y_side;
    const double min_width = std::min(start.width, k_min_dimension);
    const double min_height = std::min(start.height, k_min_dimension);
    if (keep_ratio) {
        const double anchor_dx = handle.x_side * start.width;
        const double anchor_dy = handle.y_side * start.height;
        const double span = std::hypot(anchor_dx, anchor_dy);
        const double along = local_x * (anchor_dx / span) + local_y * (anchor_dy / span);
        const double min_factor = std::max(min_width / start.width, min_height / start.height);
        const double factor = std::max(min_factor, 1.0 + along / span);
        next.width *= factor;
        next.height *= factor;
    } else {
        next.width = std::max(min_width, start.width + width_change);
        next.height = std::max(min_height, start.height + height_change);
    }
    // The opposite edge midpoint or corner stays fixed in world space.
    const auto [center_dx, center_dy] = rotate(
        (next.width - start.width) * handle.x_side * 0.5,
        (next.height - start.height) * handle.y_side * 0.5, angle);
    next.x += center_dx;
    next.y += center_dy;
    return next;
}

std::optional<ImVec2> background_uv_from_world(
    ImVec2 world, const BackgroundTransform& background) {
    if (background.width <= 0.0 || background.height <= 0.0) {
        return std::nullopt;
    }
    const auto [local_x, local_y] = rotate(world.x - background.x, world.y - background.y,
                                           -rotation_radians(background.rotation_deg));
    return ImVec2(static_cast<float>(local_x / background.width),
                  static_cast<float>(local_y / background.height));
}

std::optional<BackgroundTransform> align_background_to_points(
    const BackgroundTransform& background, ImVec2 image_point1,
    ImVec2 image_point2, ImVec2 world_point1, ImVec2 world_point2) {
    const double world_dx = world_point2.x - world_point1.x;
    const double world_dy = world_point2.y - world_point1.y;
    const double world_distance = std::hypot(world_dx, world_dy);
    if (world_distance < 1e-6) return std::nullopt;

    const ImVec2 local_point1(
        static_cast<float>(image_point1.x * background.width),
        static_cast<float>(image_point1.y * background.height));
    const ImVec2 local_point2(
        static_cast<float>(image_point2.x * background.width),
        static_cast<float>(image_point2.y * background.height));
    const double local_dx = local_point2.x - local_point1.x;
    const double local_dy = local_point2.y - local_point1.y;
    const double local_distance = std::hypot(local_dx, local_dy);
    if (local_distance < 1e-6) return std::nullopt;

    const double scale = world_distance / local_distance;
    const double local_angle = std::atan2(local_dy, local_dx);
    const double world_angle = std::atan2(world_dy, world_dx);
    const double rotation = world_angle - local_angle;
    const double cosine = std::cos(rotation);
    const double sine = std::sin(rotation);
    const double scaled_x =
        scale * (local_point1.x * cosine - local_point1.y * sine);
    const double scaled_y =
        scale * (local_point1.x * sine + local_point1.y * cosine);

    BackgroundTransform aligned = background;
    aligned.x = world_point1.x - scaled_x;
    aligned.y = world_point1.y - scaled_y;
    aligned.width *= scale;
    aligned.height *= scale;
    aligned.rotation_deg = std::fmod(rotation * 180.0 / k_pi + 360.0, 360.0);
    return aligned;
}

}  // namespace canvas2d

const canvas2d::BackgroundTransform& App::displayed_background_transform() const {
    return show_background_adjust_ && pending_bg_transform_.valid()
        ? pending_bg_transform_ : bg_transform_;
}

std::optional<ImVec2> App::background_uv_from_world(ImVec2 world) const {
    return canvas2d::background_uv_from_world(world, displayed_background_transform());
}

std::optional<canvas2d::BackgroundQuad> App::draw_background(ImDrawList* draw, ImVec2 origin,
                                                            ImVec2 size) const {
    if (!bg_show_ || !bg_image_.srv) return std::nullopt;
    const auto quad = canvas2d::project_background(
        displayed_background_transform(), plan_view_, origin, size);
    if (quad) {
        const auto& p = quad->corners;
        draw->AddImageQuad(reinterpret_cast<ImTextureID>(bg_image_.srv), p[0], p[1], p[2], p[3]);
    }
    return quad;
}

bool App::background_handles_available() const {
    return show_background_adjust_ && bg_show_ && bg_image_.srv && pick_slot_ == 0 &&
        !popups_.background_align && pending_bg_transform_.valid();
}

void App::cancel_background_drag() {
    if (!bg_drag_) return;
    pending_bg_transform_ = bg_drag_->start;
    bg_drag_.reset();
    wake_main_window();
}

bool App::process_background_drag(ImVec2 origin, ImVec2 size, float handle_scale,
                                  bool hovered, bool mouse_input) {
    const ImGuiIO& io = ImGui::GetIO();
    const bool pressed = ImGui::IsMouseClicked(ImGuiMouseButton_Left);
    const bool held = ImGui::IsMouseDown(ImGuiMouseButton_Left);
    const bool released = ImGui::IsMouseReleased(ImGuiMouseButton_Left);
    const bool available = mouse_input && background_handles_available() && !io.AppFocusLost;
    if (pressed && bg_left_consumed_) {
        cancel_background_drag();
        bg_left_consumed_ = false;
    }
    if (bg_drag_) {
        if (!available || !ImGui::IsMousePosValid() || (!held && !released) ||
            ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
            cancel_background_drag();
        } else {
            if (ImGui::IsMouseDragPastThreshold(ImGuiMouseButton_Left)) {
                const auto next = bg_drag_->update(plan_view_, origin, size, io.MousePos, io.KeyShift);
                if (next.valid() && !(next == pending_bg_transform_)) {
                    pending_bg_transform_ = next;
                    wake_main_window();
                }
            }
            if (released) {
                const bool changed = !(pending_bg_transform_ == bg_drag_->start);
                bg_drag_.reset();
                wake_main_window();
                if (changed) {
                    bg_transform_ = pending_bg_transform_;
                    save_current_background_to_history();
                }
            }
        }
    } else if (available && hovered && pressed && !io.KeyCtrl) {
        const auto quad = canvas2d::project_background(pending_bg_transform_, plan_view_, origin, size);
        if (quad) {
            const auto hit = canvas2d::hit_background(*quad, io.MousePos, handle_scale);
            if (hit) {
                bg_drag_ = canvas2d::BackgroundDrag{*hit, pending_bg_transform_, io.MousePos, io.MousePos};
                bg_left_consumed_ = true;
            }
        }
    }
    const bool consumed = bg_left_consumed_;
    bg_left_consumed_ = consumed && held;
    return consumed;
}

void App::draw_background_handles(ImDrawList* draw, const canvas2d::BackgroundQuad& quad,
                                  float handle_scale, bool hovered) const {
    if (!background_handles_available()) return;
    const ImGuiIO& io = ImGui::GetIO();
    std::optional<canvas2d::BackgroundHandle> selected;
    if (bg_drag_) {
        selected = bg_drag_->handle;
    } else if (hovered && !io.KeyCtrl && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        selected = canvas2d::hit_background(quad, io.MousePos, handle_scale);
    }
    if (selected) canvas2d::set_background_cursor(*selected, quad.rotation);

    constexpr ImU32 outline = IM_COL32(17, 20, 26, 255);
    constexpr ImU32 normal = IM_COL32(244, 249, 255, 255);
    constexpr ImU32 active = IM_COL32(255, 206, 83, 255);
    const auto& p = quad.corners;
    draw->AddQuad(p[0], p[1], p[2], p[3], outline, 4.0f * handle_scale);
    draw->AddQuad(p[0], p[1], p[2], p[3], selected == canvas2d::k_move ? active : normal,
                  1.5f * handle_scale);
    for (size_t i = 0; i < p.size(); ++i) {
        if (selected == canvas2d::k_edges[i]) {
            draw->AddLine(p[i], p[(i + 1) % p.size()], active, 2.5f * handle_scale);
        }
    }
    const auto square = [&](ImVec2 point, float half_size, ImU32 color) {
        const float inner = half_size * handle_scale;
        const float outer = inner + 1.5f * handle_scale;
        draw->AddRectFilled(ImVec2(point.x - outer, point.y - outer),
                            ImVec2(point.x + outer, point.y + outer), outline);
        draw->AddRectFilled(ImVec2(point.x - inner, point.y - inner),
                            ImVec2(point.x + inner, point.y + inner), color);
    };
    for (size_t i = 0; i < p.size(); ++i) {
        square(canvas2d::edge_midpoint(quad, i), 3.5f,
               selected == canvas2d::k_edges[i] ? active : normal);
        square(p[i], 4.5f, selected == canvas2d::k_corners[i] ? active : normal);
    }
    const ImVec2 top = canvas2d::edge_midpoint(quad, 0);
    const ImVec2 rotate = canvas2d::rotation_handle_point(quad, handle_scale);
    const ImU32 rotation_color = selected == canvas2d::k_rotate ? active : normal;
    draw->AddLine(top, rotate, outline, 4.0f * handle_scale);
    draw->AddLine(top, rotate, rotation_color, 1.5f * handle_scale);
    draw->AddCircleFilled(rotate, 7.0f * handle_scale, outline);
    draw->AddCircleFilled(rotate, 5.5f * handle_scale, rotation_color);
}

void App::apply_background_alignment() {
    if (!has_model_ || model_.stations.size() < 2 || !align_pick1_ || !align_pick2_) {
        return;
    }
    if (bg_image_.path.empty()) return;
    align_station1_ =
        std::clamp(align_station1_, 0, static_cast<int>(model_.stations.size()) - 1);
    align_station2_ =
        std::clamp(align_station2_, 0, static_cast<int>(model_.stations.size()) - 1);
    const PlanData& plan = current_plan_data();
    const auto find_station = [&](const std::string& key) -> std::optional<ImVec2> {
        for (const auto& station : plan.stations) {
            if (station.station.key == key) {
                return ImVec2(static_cast<float>(station.x),
                              static_cast<float>(station.y));
            }
        }
        return std::nullopt;
    };
    const auto station1 = find_station(model_.stations[align_station1_].key);
    const auto station2 = find_station(model_.stations[align_station2_].key);
    if (!station1 || !station2) return;

    const auto aligned = canvas2d::align_background_to_points(
        displayed_background_transform(), *align_pick1_, *align_pick2_, *station1, *station2);
    if (!aligned || !aligned->valid()) return;

    bg_transform_ = *aligned;
    pending_bg_transform_ = *aligned;
    save_current_background_to_history();
}
