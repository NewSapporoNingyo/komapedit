/*
 * Copyright (c) 2026 Sapporo_ningyo
 *
 * Licensed under Apache License 2.0; see LICENSE and NOTICE.
 * The GUI uses Dear ImGui; see THIRD_PARTY_NOTICES.md.
 */

#pragma once

#include "canvas2d_view_state.h"

#include "imgui.h"

#include <optional>

namespace canvas2d {

struct BackgroundTransform {
    double x = 0.0;
    double y = 0.0;
    double width = 0.0;
    double height = 0.0;
    double rotation_deg = 0.0;
};

std::optional<ImVec2> background_uv_from_world(
    ImVec2 world, const BackgroundTransform& background);

void draw_background_quad(ImDrawList* draw, ImTextureID texture,
                          const BackgroundTransform& background,
                          const View2D& view, ImVec2 origin, ImVec2 size);

std::optional<BackgroundTransform> align_background_to_points(
    const BackgroundTransform& background, ImVec2 image_point1,
    ImVec2 image_point2, ImVec2 world_point1, ImVec2 world_point2);

}  // namespace canvas2d
