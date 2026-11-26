/*
 * Copyright (c) 2026 Sapporo_ningyo
 *
 * Licensed under Apache License 2.0; see LICENSE and NOTICE.
 * The GUI uses Dear ImGui; see THIRD_PARTY_NOTICES.md.
 */

#pragma once

#include "canvas2d_view_state.h"

#include "imgui.h"

#include <array>
#include <optional>

namespace canvas2d {

struct BackgroundTransform {
    double x = 0.0;
    double y = 0.0;
    double width = 0.0;
    double height = 0.0;
    double rotation_deg = 0.0;

    bool valid() const;
    bool operator==(const BackgroundTransform& other) const;
};

struct BackgroundQuad {
    std::array<ImVec2, 4> corners;
    ImVec2 center;
    double rotation;
    double half_width;
    double half_height;
};

struct BackgroundHandle {
    enum class Kind { Move, Resize, Rotate };

    Kind kind;
    int x_side = 0;
    int y_side = 0;

    bool operator==(const BackgroundHandle& other) const;
};

struct BackgroundDrag {
    BackgroundHandle handle;
    BackgroundTransform start;
    ImVec2 start_pointer;
    ImVec2 previous_pointer;
    double rotation_delta = 0.0;

    BackgroundTransform update(const View2D& view, ImVec2 origin, ImVec2 size,
                               ImVec2 pointer, bool keep_ratio);
};

std::optional<ImVec2> background_uv_from_world(
    ImVec2 world, const BackgroundTransform& background);

std::optional<BackgroundTransform> align_background_to_points(
    const BackgroundTransform& background, ImVec2 image_point1,
    ImVec2 image_point2, ImVec2 world_point1, ImVec2 world_point2);

}  // namespace canvas2d
