/*
 * Copyright (c) 2026 Sapporo_ningyo
 *
 * Licensed under Apache License 2.0; see LICENSE and NOTICE.
 * The GUI uses Dear ImGui and ImPlot; see THIRD_PARTY_NOTICES.md.
 */

#pragma once

#include "imgui.h"

// Mutable camera-like state for the 2D plan canvas. Model-space route data is
// kept outside this type; it owns only view transforms and pointer-drag state.
struct View2D {
    double cx = 0.0;
    double cy = 0.0;
    double scale = 1.0;
    double rotation = 0.0;
    bool fitted = false;
    bool dragging = false;
    bool rotating = false;
    ImVec2 last_mouse = ImVec2(0, 0);

    ImVec2 world_to_screen(double x, double y, ImVec2 origin, ImVec2 size) const;
    ImVec2 screen_to_world(ImVec2 point, ImVec2 origin, ImVec2 size) const;
    void pan_by_screen_delta(ImVec2 delta);
    void fit(double xmin, double ymin, double xmax, double ymax, ImVec2 size);
};
