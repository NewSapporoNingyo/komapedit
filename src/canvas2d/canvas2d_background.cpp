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

#include <algorithm>
#include <cmath>

namespace canvas2d {
namespace {

constexpr double k_pi = 3.14159265358979323846;

}  // namespace

std::optional<ImVec2> background_uv_from_world(
    ImVec2 world, const BackgroundTransform& background) {
    if (background.width <= 0.0 || background.height <= 0.0) {
        return std::nullopt;
    }
    const double rotation = background.rotation_deg * k_pi / 180.0;
    const double dx = world.x - background.x;
    const double dy = world.y - background.y;
    const double local_x = dx * std::cos(rotation) + dy * std::sin(rotation);
    const double local_y = -dx * std::sin(rotation) + dy * std::cos(rotation);
    return ImVec2(static_cast<float>(local_x / background.width),
                  static_cast<float>(local_y / background.height));
}

void draw_background_quad(ImDrawList* draw, ImTextureID texture,
                          const BackgroundTransform& background,
                          const View2D& view, ImVec2 origin, ImVec2 size) {
    if (!draw || !texture || background.width <= 0.0 || background.height <= 0.0) {
        return;
    }
    const double rotation = background.rotation_deg * k_pi / 180.0;
    const double cosine = std::cos(rotation);
    const double sine = std::sin(rotation);
    const double half_width = background.width * 0.5;
    const double half_height = background.height * 0.5;
    const ImVec2 local[4] = {
        ImVec2(static_cast<float>(-half_width), static_cast<float>(-half_height)),
        ImVec2(static_cast<float>(half_width), static_cast<float>(-half_height)),
        ImVec2(static_cast<float>(half_width), static_cast<float>(half_height)),
        ImVec2(static_cast<float>(-half_width), static_cast<float>(half_height)),
    };
    ImVec2 points[4];
    for (int index = 0; index < 4; ++index) {
        const double x = background.x + cosine * local[index].x - sine * local[index].y;
        const double y = background.y + sine * local[index].x + cosine * local[index].y;
        points[index] = view.world_to_screen(x, y, origin, size);
    }
    draw->AddImageQuad(texture, points[0], points[1], points[2], points[3]);
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

namespace {

canvas2d::BackgroundTransform current_background_transform(
    double x, double y, double width, double height, double rotation_deg) {
    return canvas2d::BackgroundTransform{x, y, width, height, rotation_deg};
}

}  // namespace

std::optional<ImVec2> App::background_uv_from_world(ImVec2 world) const {
    return canvas2d::background_uv_from_world(
        world, current_background_transform(
                   bg_x_, bg_y_, bg_width_, bg_height_, bg_rotation_deg_));
}

void App::draw_background(ImDrawList* draw, const View2D& view,
                          ImVec2 origin, ImVec2 size) {
    if (!bg_show_ || !bg_image_.srv) return;
    canvas2d::draw_background_quad(
        draw, reinterpret_cast<ImTextureID>(bg_image_.srv),
        current_background_transform(
            bg_x_, bg_y_, bg_width_, bg_height_, bg_rotation_deg_),
        view, origin, size);
}

void App::apply_background_alignment() {
    if (!has_model_ || model_.stations.size() < 2 || !align_pick1_ || !align_pick2_) {
        return;
    }
    if (bg_image_.path.empty() || bg_width_ <= 0.0 || bg_height_ <= 0.0) return;
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
        current_background_transform(
            bg_x_, bg_y_, bg_width_, bg_height_, bg_rotation_deg_),
        *align_pick1_, *align_pick2_, *station1, *station2);
    if (!aligned) return;

    bg_x_ = aligned->x;
    bg_y_ = aligned->y;
    bg_width_ = aligned->width;
    bg_height_ = aligned->height;
    bg_rotation_deg_ = aligned->rotation_deg;
    sync_pending_background_values();
    save_current_background_to_history();
}
