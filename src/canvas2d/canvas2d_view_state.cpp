/*
 * Copyright (c) 2026 Sapporo_ningyo
 *
 * Licensed under Apache License 2.0; see LICENSE and NOTICE.
 * The GUI uses Dear ImGui and ImPlot; see THIRD_PARTY_NOTICES.md.
 */

#ifdef _MSC_VER
#pragma execution_character_set("utf-8")
#endif

#include "canvas2d_view_state.h"

#include <algorithm>
#include <cmath>

ImVec2 View2D::world_to_screen(double x, double y, ImVec2 origin, ImVec2 size) const {
    const double dx = x - cx;
    const double dy = y - cy;
    const double c = std::cos(rotation);
    const double s = std::sin(rotation);
    const double rx = c * dx - s * dy;
    const double ry = s * dx + c * dy;
    return ImVec2(origin.x + size.x * 0.5f + static_cast<float>(rx * scale),
                  origin.y + size.y * 0.5f + static_cast<float>(ry * scale));
}

ImVec2 View2D::screen_to_world(ImVec2 point, ImVec2 origin, ImVec2 size) const {
    const double rx = (point.x - origin.x - size.x * 0.5) / scale;
    const double ry = (point.y - origin.y - size.y * 0.5) / scale;
    const double c = std::cos(rotation);
    const double s = std::sin(rotation);
    return ImVec2(static_cast<float>(c * rx + s * ry + cx),
                  static_cast<float>(-s * rx + c * ry + cy));
}

void View2D::pan_by_screen_delta(ImVec2 delta) {
    const double c = std::cos(rotation);
    const double s = std::sin(rotation);
    const double wx = -(c * delta.x / scale + s * delta.y / scale);
    const double wy = s * delta.x / scale - c * delta.y / scale;
    cx += wx;
    cy += wy;
}

void View2D::fit(double xmin, double ymin, double xmax, double ymax, ImVec2 size) {
    const double dx = std::max(xmax - xmin, 1e-6);
    const double dy = std::max(ymax - ymin, 1e-6);
    cx = (xmin + xmax) * 0.5;
    cy = (ymin + ymax) * 0.5;
    scale = std::clamp(
        std::min(size.x / dx, size.y / dy) * 0.88, 0.001, 10000.0);
    fitted = true;
}
