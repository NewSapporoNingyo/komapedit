/*
 * Copyright (c) 2026 Sapporo_ningyo
 *
 * Licensed under Apache License 2.0; see LICENSE and NOTICE.
 * The GUI uses Dear ImGui; see THIRD_PARTY_NOTICES.md.
 */

#pragma once

#include "canvas2d_view_state.h"
#include "../main_window/kme.h"

#include "imgui.h"

#include <string>
#include <utility>
#include <vector>

namespace canvas2d {

struct PlanScreenTransform {
    double model_c = 1.0;
    double model_s = 0.0;
    double view_c = 1.0;
    double view_s = 0.0;
    double cx = 0.0;
    double cy = 0.0;
    double scale = 1.0;
    float screen_cx = 0.0f;
    float screen_cy = 0.0f;

    ImVec2 plan_to_screen(double x, double y) const;
    std::pair<double, double> screen_to_plan(ImVec2 screen) const;
    std::pair<double, double> model_to_plan(double x, double y) const;
    ImVec2 model_to_screen(double x, double y) const;
};

ImU32 color_u32(const ImVec4& color);
PlanScreenTransform make_plan_transform(const View2D& view, double model_angle,
                                        ImVec2 origin, ImVec2 size);

void draw_polyline_range(ImDrawList* draw, const std::vector<TrackPoint>& points,
                         size_t first, size_t last,
                         const PlanScreenTransform& transform,
                         ImVec2 origin, ImVec2 size, ImU32 color, float thickness);
void draw_polyline(ImDrawList* draw, const std::vector<TrackPoint>& points,
                   const PlanScreenTransform& transform,
                   ImVec2 origin, ImVec2 size, ImU32 color, float thickness);
void draw_matrix_plan_polyline(ImDrawList* draw, const Matrix& points,
                               double distance_min, double distance_max,
                               const PlanScreenTransform& transform,
                               ImVec2 origin, ImVec2 size,
                               ImU32 color, float thickness);
void draw_repeater_segment_chunks(
    ImDrawList* draw, const std::vector<RepeaterOverlayRow>& rows,
    const std::vector<unsigned char>& visible,
    double distance_min, double distance_max,
    const PlanScreenTransform& transform,
    ImVec2 origin, ImVec2 size, ImU32 color, float thickness);

double grid_step(double span);
void draw_scalebar(ImDrawList* draw, const View2D& view,
                   ImVec2 origin, ImVec2 size);
void draw_plan_triangle_marker(ImDrawList* draw, ImVec2 point,
                               ImU32 color, float scale = 1.0f);
void draw_plan_diamond_marker(ImDrawList* draw, ImVec2 point,
                              ImU32 color, float scale = 1.0f);
void draw_plan_signal_marker(ImDrawList* draw, ImVec2 point,
                             ImU32 color, float scale = 1.0f);
void draw_plan_pretrain_marker(ImDrawList* draw, ImVec2 point,
                               const std::string& label, float scale = 1.0f);
void draw_plan_focus_arrow(ImDrawList* draw, ImVec2 target);
void draw_plan_current_position_arrow(ImDrawList* draw, ImVec2 center,
                                      ImVec2 direction, float scale = 1.0f);
void draw_plan_direction_arrow(ImDrawList* draw, ImVec2 center, ImVec2 direction,
                               ImU32 fill, ImU32 outline, float scale = 1.0f);
void draw_plan_small_text(ImDrawList* draw, ImVec2 point,
                          ImU32 color, const std::string& text);
bool point_near_canvas(ImVec2 point, ImVec2 origin, ImVec2 size,
                       float margin = 48.0f);

#ifndef NDEBUG
bool debug_repeater_overview_indices();
#endif

}  // namespace canvas2d
