/*
 * Copyright (c) 2026 Sapporo_ningyo
 *
 * Licensed under Apache License 2.0; see LICENSE and NOTICE.
 * The GUI uses Dear ImGui and ImPlot; see THIRD_PARTY_NOTICES.md.
 */

#include "kme.h"
#include "touch_input.h"

#include "imgui.h"
#include "implot.h"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace {

static void plot_line_vec(const char* label, const std::vector<double>& x, const std::vector<double>& y, ImVec4 color, float weight = 1.5f) {
    if (x.size() < 2 || y.size() < 2) return;
    ImPlotSpec spec;
    spec.LineColor = color;
    spec.LineWeight = weight;
    ImPlot::PlotLine(label, x.data(), y.data(), static_cast<int>(std::min(x.size(), y.size())), spec);
}

static void draw_plot_overlay_labels(const std::string& title, const std::string& unit) {
    ImDrawList* draw = ImGui::GetWindowDrawList();
    ImVec2 pos = ImPlot::GetPlotPos();
    ImVec2 size = ImPlot::GetPlotSize();
    ImU32 color = ImGui::GetColorU32(ImGuiCol_Text);
    draw->AddText(ImVec2(pos.x + 8.0f, pos.y + 6.0f), color, title.c_str());
    ImVec2 unit_size = ImGui::CalcTextSize(unit.c_str());
    draw->AddText(ImVec2(pos.x - unit_size.x - 8.0f, pos.y + size.y + 6.0f), color, unit.c_str());
}

static void mask_plot_axis_tick_edges(ImVec2 frame_min, ImVec2 frame_max, ImVec2 plot_pos, ImVec2 plot_size) {
    ImVec2 plot_max(plot_pos.x + plot_size.x, plot_pos.y + plot_size.y);
    if (plot_size.x <= 0.0f || plot_size.y <= 0.0f) return;

    ImU32 bg = ImGui::GetColorU32(ImGuiCol_WindowBg);
    ImDrawList* draw = ImGui::GetWindowDrawList();
    float bleed = ImGui::GetFontSize() * 1.4f;
    ImVec2 outer_min(frame_min.x - bleed, frame_min.y - bleed * 0.5);
    ImVec2 outer_max(frame_max.x + bleed, frame_max.y + bleed);
    auto fill = [&](ImVec2 min, ImVec2 max) {
        if (max.x > min.x && max.y > min.y) draw->AddRectFilled(min, max, bg);
    };

    fill(outer_min, plot_pos);
    fill(ImVec2(plot_max.x, outer_min.y), ImVec2(outer_max.x, plot_pos.y));
    fill(ImVec2(outer_min.x, plot_max.y), ImVec2(plot_pos.x, outer_max.y));
    fill(plot_max, outer_max);
}

static void draw_radius_side_markers() {
    ImDrawList* draw = ImGui::GetWindowDrawList();
    ImVec2 pos = ImPlot::GetPlotPos();
    ImVec2 size = ImPlot::GetPlotSize();
    ImVec2 clip_max(pos.x + size.x, pos.y + size.y);
    ImVec2 zero = ImPlot::PlotToPixels(ImPlot::GetPlotLimits().X.Min, 0.0);
    if (!std::isfinite(zero.y)) return;

    ImU32 color = ImGui::GetColorU32(ImGuiCol_Text);
    ImVec2 r_size = ImGui::CalcTextSize("R");
    ImVec2 l_size = ImGui::CalcTextSize("L");
    float x = pos.x - std::max(r_size.x, l_size.x) - 4.0f;
    float gap = 3.0f;
    float r_y = std::clamp(zero.y - r_size.y - gap, pos.y + 2.0f, clip_max.y - r_size.y - 2.0f);
    float l_y = std::clamp(zero.y + gap, pos.y + 2.0f, clip_max.y - l_size.y - 2.0f);

    draw->AddText(ImVec2(x, r_y), color, "R");
    draw->AddText(ImVec2(x, l_y), color, "L");
}

static std::optional<std::pair<double, double>> plot_x_wheel_zoom_limits(const ImPlotRect& limits) {
    ImGuiIO& io = ImGui::GetIO();
    if (io.MouseWheel == 0.0f || !ImPlot::IsPlotHovered()) return std::nullopt;
    double min_x = limits.X.Min;
    double max_x = limits.X.Max;
    double span = max_x - min_x;
    if (!std::isfinite(span) || span <= 1e-9) return std::nullopt;

    ImVec2 pos = ImPlot::GetPlotPos();
    ImVec2 size = ImPlot::GetPlotSize();
    if (size.x <= 1.0f) return std::nullopt;
    float tx = std::clamp((io.MousePos.x - pos.x) / size.x, 0.0f, 1.0f);

    float zoom_rate = 0.1f;
    if (io.MouseWheel > 0.0f) {
        zoom_rate = (-zoom_rate) / (1.0f + (2.0f * zoom_rate));
    }
    double new_min = min_x - span * tx * zoom_rate;
    double new_max = max_x + span * (1.0f - tx) * zoom_rate;
    if (!std::isfinite(new_min) || !std::isfinite(new_max) || new_max <= new_min) return std::nullopt;
    return std::make_pair(new_min, new_max);
}

static void draw_bottom_locked_plot_labels(const std::vector<LabelPoint>& labels) {
    ImDrawList* draw = ImPlot::GetPlotDrawList();
    ImVec2 pos = ImPlot::GetPlotPos();
    ImVec2 size = ImPlot::GetPlotSize();
    ImVec2 clip_max(pos.x + size.x, pos.y + size.y);
    ImU32 color = ImGui::GetColorU32(ImGuiCol_Text);
    draw->PushClipRect(pos, clip_max, true);
    for (const auto& label : labels) {
        ImVec2 p = ImPlot::PlotToPixels(label.x, 0.0);
        if (p.x < pos.x || p.x > clip_max.x) continue;
        ImVec2 text_size = ImGui::CalcTextSize(label.text.c_str());
        draw->AddText(ImVec2(p.x + 6.0f - text_size.x, clip_max.y - text_size.y - 6.0f), color, label.text.c_str());
    }
    draw->PopClipRect();
}

enum class FixedPlotY {
    Top,
    Bottom
};

static void draw_fixed_y_plot_text(double x, const std::string& text, ImU32 color, FixedPlotY fixed_y) {
    if (text.empty()) return;
    ImDrawList* draw = ImPlot::GetPlotDrawList();
    ImVec2 pos = ImPlot::GetPlotPos();
    ImVec2 size = ImPlot::GetPlotSize();
    ImVec2 clip_max(pos.x + size.x, pos.y + size.y);
    ImVec2 p = ImPlot::PlotToPixels(x, 0.0);
    if (!std::isfinite(p.x) || p.x < pos.x || p.x > clip_max.x) return;

    ImVec2 text_size = ImGui::CalcTextSize(text.c_str());
    float y = fixed_y == FixedPlotY::Top ? pos.y + 8.0f : clip_max.y - text_size.y - 8.0f;
    draw->PushClipRect(pos, clip_max, true);
    draw->AddText(ImVec2(p.x + 8.0f, y), color, text.c_str());
    draw->PopClipRect();
}

static void draw_plot_point_right_text(double x, double y, const std::string& text, ImU32 color) {
    if (text.empty()) return;
    ImDrawList* draw = ImPlot::GetPlotDrawList();
    ImVec2 pos = ImPlot::GetPlotPos();
    ImVec2 size = ImPlot::GetPlotSize();
    ImVec2 clip_max(pos.x + size.x, pos.y + size.y);
    ImVec2 p = ImPlot::PlotToPixels(x, y);
    if (!std::isfinite(p.x) || !std::isfinite(p.y) || p.x < pos.x || p.x > clip_max.x) return;

    draw->PushClipRect(pos, clip_max, true);
    draw->AddText(ImVec2(p.x + 8.0f, p.y - 26.0f), color, text.c_str());
    draw->PopClipRect();
}

enum class ProfileMarkerDirection {
    Up,
    Down
};

static void draw_profile_vertical_marker(double x, double track_y, ProfileMarkerDirection direction,
                                         ImU32 line_color, float line_weight, bool draw_station_marker,
                                         float station_marker_radius = k_default_station_marker_size) {
    ImDrawList* draw = ImPlot::GetPlotDrawList();
    ImVec2 pos = ImPlot::GetPlotPos();
    ImVec2 size = ImPlot::GetPlotSize();
    ImVec2 clip_min = pos;
    ImVec2 clip_max(pos.x + size.x, pos.y + size.y);
    ImVec2 p = ImPlot::PlotToPixels(x, track_y);
    if (!std::isfinite(p.x) || !std::isfinite(p.y) || p.x < clip_min.x || p.x > clip_max.x) return;

    float unclipped_a = direction == ProfileMarkerDirection::Up ? clip_min.y : p.y;
    float unclipped_b = direction == ProfileMarkerDirection::Up ? p.y : clip_max.y;
    float line_min = std::max(std::min(unclipped_a, unclipped_b), clip_min.y);
    float line_max = std::min(std::max(unclipped_a, unclipped_b), clip_max.y);

    draw->PushClipRect(clip_min, clip_max, true);
    if (line_max > line_min) {
        draw->AddLine(ImVec2(p.x, line_min), ImVec2(p.x, line_max), line_color, line_weight);
    }
    if (draw_station_marker) {
        float radius = std::max(0.0f, station_marker_radius);
        float outline_weight = std::max(1.0f, radius * 0.375f);
        draw->AddCircleFilled(p, radius, IM_COL32(0, 0, 0, 255));
        draw->AddCircle(p, radius, IM_COL32(255, 255, 255, 255), 0, outline_weight);
    }
    draw->PopClipRect();
}

class ScopedImPlotFitButton {
public:
    explicit ScopedImPlotFitButton(bool disabled) : active_(disabled) {
        if (!active_) return;
        ImPlotInputMap& input = ImPlot::GetInputMap();
        old_fit_ = input.Fit;
        // Extra mouse slot 3 is not enabled by ImPlot's plot button flags.
        input.Fit = 3;
    }

    ~ScopedImPlotFitButton() {
        if (active_) ImPlot::GetInputMap().Fit = old_fit_;
    }

private:
    bool active_ = false;
    ImGuiMouseButton old_fit_ = ImGuiMouseButton_Left;
};

class ScopedImPlotWheelZoomDisabled {
public:
    explicit ScopedImPlotWheelZoomDisabled(bool active) : active_(active) {
        if (!active_) return;
        ImPlotInputMap& input = ImPlot::GetInputMap();
        old_zoom_rate_ = input.ZoomRate;
        input.ZoomRate = 0.0f;
    }

    ~ScopedImPlotWheelZoomDisabled() {
        if (active_) ImPlot::GetInputMap().ZoomRate = old_zoom_rate_;
    }

private:
    bool active_ = false;
    float old_zoom_rate_ = 0.0f;
};

static bool point_in_rect(ImVec2 p, ImVec2 pos, ImVec2 size) {
    return p.x >= pos.x && p.x <= pos.x + size.x && p.y >= pos.y && p.y <= pos.y + size.y;
}

struct PlotTouchZoom {
    bool x = false;
    double x_min = 0.0;
    double x_max = 0.0;
    bool y = false;
    double y_min = 0.0;
    double y_max = 0.0;
};

static std::optional<std::pair<double, double>> zoom_axis_limits(double min_value, double max_value,
                                                                  double anchor, float scale) {
    if (!std::isfinite(min_value) || !std::isfinite(max_value) || max_value <= min_value) return std::nullopt;
    if (!std::isfinite(anchor) || !std::isfinite(scale) || scale <= 0.0f) return std::nullopt;
    if (std::abs(scale - 1.0f) < 0.004f) return std::nullopt;

    anchor = std::clamp(anchor, min_value, max_value);
    double new_min = anchor - (anchor - min_value) / static_cast<double>(scale);
    double new_max = anchor + (max_value - anchor) / static_cast<double>(scale);
    if (!std::isfinite(new_min) || !std::isfinite(new_max) || new_max <= new_min) return std::nullopt;
    return std::make_pair(new_min, new_max);
}

static PlotTouchZoom plot_touch_zoom_limits(const ImPlotRect& limits, bool allow_y_axis) {
    PlotTouchZoom zoom;
    const touch_input::TouchFrame& touch = touch_input::current_frame();
    if (!touch.pinch || touch.active_count < 2 || touch.pinch_axis == touch_input::PinchAxis::None) return zoom;

    ImVec2 pos = ImPlot::GetPlotPos();
    ImVec2 size = ImPlot::GetPlotSize();
    if (size.x <= 1.0f || size.y <= 1.0f) return zoom;
    if (!point_in_rect(touch.pinch_center, pos, size) && !point_in_rect(touch.pinch_previous_center, pos, size)) {
        return zoom;
    }

    ImPlotPoint anchor = ImPlot::PixelsToPlot(touch.pinch_center);
    if (touch.pinch_axis == touch_input::PinchAxis::Horizontal) {
        if (auto range = zoom_axis_limits(limits.X.Min, limits.X.Max, anchor.x, touch.pinch_x_scale)) {
            zoom.x = true;
            zoom.x_min = range->first;
            zoom.x_max = range->second;
        }
    } else if (allow_y_axis) {
        if (auto range = zoom_axis_limits(limits.Y.Min, limits.Y.Max, anchor.y, touch.pinch_y_scale)) {
            zoom.y = true;
            zoom.y_min = range->first;
            zoom.y_max = range->second;
        }
    }
    return zoom;
}

static double preserved_plot_span(double current_span, double fallback_min, double fallback_max) {
    if (std::isfinite(current_span) && current_span > 1e-6) return current_span;
    double fallback = fallback_max - fallback_min;
    if (std::isfinite(fallback) && fallback > 1e-6) return fallback;
    return 1000.0;
}

} // namespace

void App::render_profile_plot(const ProfileData& data, ImVec2 size) {
    if (!show_profile_graph_) return;
    ScopedImPlotFitButton disable_fit(mode_ == Mode::Measure);
    ImGuiIO& io = ImGui::GetIO();
    bool mouse_in_profile_plot = profile_plot_rect_valid_ && point_in_rect(io.MousePos, profile_plot_pos_, profile_plot_size_);
    ScopedImPlotWheelZoomDisabled disable_default_wheel_zoom(mouse_in_profile_plot);
    const CanvasLineWidthSettings line_widths = clamp_canvas_line_widths(canvas_line_widths_);
    const ImVec2 grid_line_size(line_widths.background_grid_px, line_widths.background_grid_px);
    ImPlot::PushStyleVar(ImPlotStyleVar_PlotPadding, ImVec2(4.0f, 4.0f));
    ImPlot::PushStyleVar(ImPlotStyleVar_LabelPadding, ImVec2(2.0f, 2.0f));
    ImPlot::PushStyleVar(ImPlotStyleVar_MajorGridSize, grid_line_size);
    ImPlot::PushStyleVar(ImPlotStyleVar_MinorGridSize, grid_line_size);
    bool consumed_profile_x_zoom = false;
    bool consumed_profile_y_zoom = false;
    if (ImPlot::BeginPlot("##ProfilePlot", size, ImPlotFlags_NoTitle | ImPlotFlags_NoLegend)) {
        ImVec2 frame_min = ImGui::GetItemRectMin();
        ImVec2 frame_max = ImGui::GetItemRectMax();
        ImPlot::SetupAxes(nullptr, nullptr, ImPlotAxisFlags_NoLabel, ImPlotAxisFlags_NoLabel);
        ImPlotCond reset_cond = reset_profile_axes_next_ ? ImPlotCond_Always : ImPlotCond_Once;
        if (focus_profile_next_) {
            double span = preserved_plot_span(profile_x_span_, dmin_, dmax_);
            ImPlot::SetupAxisLimits(ImAxis_X1, focus_profile_distance_ - span * 0.5, focus_profile_distance_ + span * 0.5, ImPlotCond_Always);
            profile_x_zoom_pending_ = false;
        } else if (!reset_profile_axes_next_ && profile_x_zoom_pending_) {
            ImPlot::SetupAxisLimits(ImAxis_X1, profile_x_zoom_min_, profile_x_zoom_max_, ImPlotCond_Always);
            consumed_profile_x_zoom = true;
        } else {
            ImPlot::SetupAxisLimits(ImAxis_X1, dmin_, dmax_, reset_cond);
            if (reset_profile_axes_next_) profile_x_zoom_pending_ = false;
        }
        if (!reset_profile_axes_next_ && profile_y_zoom_pending_) {
            ImPlot::SetupAxisLimits(ImAxis_Y1, profile_y_zoom_min_, profile_y_zoom_max_, ImPlotCond_Always);
            consumed_profile_y_zoom = true;
        } else {
            ImPlot::SetupAxisLimits(ImAxis_Y1, data.ymin, data.ymax, reset_cond);
            if (reset_profile_axes_next_) profile_y_zoom_pending_ = false;
        }
        plot_line_vec("Own", data.own_x, data.own_y, ImVec4(1, 1, 1, 1), line_widths.own_track_px);
        for (const auto& t : data.other) plot_line_vec(t.key.c_str(), t.x, t.y, t.color, line_widths.other_track_px);
        if (show_gradient_pos_) {
            for (const auto& p : data.gradient_points) {
                draw_profile_vertical_marker(p.x, p.y, ProfileMarkerDirection::Down,
                                             IM_COL32(255, 255, 255, 140), line_widths.chart_marker_px, false);
            }
            if (show_gradient_values_) {
                draw_bottom_locked_plot_labels(data.gradient_labels);
            }
        }
        if (show_stations_) {
            const float station_marker_radius =
                k_default_station_marker_size * marker_size_scale_from_percent(marker_size_percent_);
            for (size_t station_index = 0;
                 station_index < data.stations.size(); ++station_index) {
                const Station& s = data.stations[station_index];
                double x = s.distance;
                double y = s.z - model_.height_origin;
                draw_profile_vertical_marker(x, y, ProfileMarkerDirection::Up,
                                             IM_COL32(255, 255, 255, 191), line_widths.chart_marker_px, true, station_marker_radius);
                if (show_station_names_) draw_plot_point_right_text(x, y, s.name, IM_COL32(255, 255, 255, 255));
                if (show_station_mileage_) {
                    draw_fixed_y_plot_text(
                        x, data.station_mileage_labels[station_index],
                        IM_COL32(255, 216, 77, 255), FixedPlotY::Top);
                }
            }
        }
        if (mode_ == Mode::Measure && ImPlot::IsPlotHovered()) {
            set_crosshair_cursor();
            ImPlotPoint p = ImPlot::GetPlotMousePos();
            if (p.x >= dmin_ && p.x <= dmax_) update_measure(p.x);
        }
        handle_measure_plot_double_click(false, true);
        if (mode_ == Mode::Measure && measure_distance_) {
            double x = *measure_distance_;
            ImPlot::PlotInfLines("##measure_profile", &x, 1, {ImPlotProp_LineColor, ImVec4(1, 0.2f, 0.2f, 1), ImPlotProp_LineWeight, 2.0f, ImPlotProp_Flags, ImPlotItemFlags_NoLegend});
        }
        ImPlotRect limits = ImPlot::GetPlotLimits();
        profile_x_span_ = std::abs(limits.X.Size());
        profile_plot_pos_ = ImPlot::GetPlotPos();
        profile_plot_size_ = ImPlot::GetPlotSize();
        profile_plot_rect_valid_ = profile_plot_size_.x > 0.0f && profile_plot_size_.y > 0.0f;
        bool queued_profile_x_zoom = false;
        if (auto zoom_limits = plot_x_wheel_zoom_limits(limits)) {
            profile_x_zoom_min_ = zoom_limits->first;
            profile_x_zoom_max_ = zoom_limits->second;
            profile_x_zoom_pending_ = true;
            queued_profile_x_zoom = true;
        }
        PlotTouchZoom touch_zoom = plot_touch_zoom_limits(limits, true);
        if (touch_zoom.x) {
            profile_x_zoom_min_ = touch_zoom.x_min;
            profile_x_zoom_max_ = touch_zoom.x_max;
            profile_x_zoom_pending_ = true;
            queued_profile_x_zoom = true;
        }
        if (touch_zoom.y) {
            profile_y_zoom_min_ = touch_zoom.y_min;
            profile_y_zoom_max_ = touch_zoom.y_max;
            profile_y_zoom_pending_ = true;
        }
        if (!queued_profile_x_zoom && consumed_profile_x_zoom) {
            profile_x_zoom_pending_ = false;
        }
        if (!touch_zoom.y && consumed_profile_y_zoom) {
            profile_y_zoom_pending_ = false;
        }
        ImVec2 plot_pos = profile_plot_pos_;
        ImVec2 plot_size = profile_plot_size_;
        mask_plot_axis_tick_edges(frame_min, frame_max, plot_pos, plot_size);
        draw_plot_overlay_labels(tr("plot.profile"), tr("unit.m"));
        focus_profile_next_ = false;
        reset_profile_axes_next_ = false;
        ImPlot::EndPlot();
    }
    ImPlot::PopStyleVar(4);
}

void App::render_radius_plot(const ProfileData& data, ImVec2 size) {
    if (!show_radius_graph_) return;
    ScopedImPlotFitButton disable_fit(mode_ == Mode::Measure);
    const CanvasLineWidthSettings line_widths = clamp_canvas_line_widths(canvas_line_widths_);
    const ImVec2 grid_line_size(line_widths.background_grid_px, line_widths.background_grid_px);
    ImPlot::PushStyleVar(ImPlotStyleVar_PlotPadding, ImVec2(4.0f, 4.0f));
    ImPlot::PushStyleVar(ImPlotStyleVar_LabelPadding, ImVec2(2.0f, 2.0f));
    ImPlot::PushStyleVar(ImPlotStyleVar_MajorGridSize, grid_line_size);
    ImPlot::PushStyleVar(ImPlotStyleVar_MinorGridSize, grid_line_size);
    bool consumed_radius_x_zoom = false;
    if (ImPlot::BeginPlot("##RadiusPlot", size, ImPlotFlags_NoTitle | ImPlotFlags_NoLegend)) {
        ImVec2 frame_min = ImGui::GetItemRectMin();
        ImVec2 frame_max = ImGui::GetItemRectMax();
        ImPlot::SetupAxis(ImAxis_X1, nullptr, ImPlotAxisFlags_NoLabel);
        ImPlot::SetupAxis(ImAxis_Y1, nullptr, ImPlotAxisFlags_NoLabel | ImPlotAxisFlags_NoTickMarks | ImPlotAxisFlags_NoTickLabels | ImPlotAxisFlags_NoHighlight | ImPlotAxisFlags_Lock);
        ImPlotCond reset_cond = reset_radius_axes_next_ ? ImPlotCond_Always : ImPlotCond_Once;
        if (focus_radius_next_) {
            double span = preserved_plot_span(radius_x_span_, dmin_, dmax_);
            ImPlot::SetupAxisLimits(ImAxis_X1, focus_radius_distance_ - span * 0.5, focus_radius_distance_ + span * 0.5, ImPlotCond_Always);
            radius_x_zoom_pending_ = false;
        } else if (!reset_radius_axes_next_ && radius_x_zoom_pending_) {
            ImPlot::SetupAxisLimits(ImAxis_X1, radius_x_zoom_min_, radius_x_zoom_max_, ImPlotCond_Always);
            consumed_radius_x_zoom = true;
        } else {
            ImPlot::SetupAxisLimits(ImAxis_X1, dmin_, dmax_, reset_cond);
            if (reset_radius_axes_next_) radius_x_zoom_pending_ = false;
        }
        ImPlot::SetupAxisLimits(ImAxis_Y1, -2.2, 2.2, ImPlotCond_Always);
        plot_line_vec("RadiusSign", data.curve_x, data.curve_y, ImVec4(1, 1, 1, 1), line_widths.own_track_px);
        for (const auto& label : data.radius_labels) ImPlot::PlotText(label.text.c_str(), label.x, label.y, ImVec2(-6, 0), {ImPlotProp_Flags, ImPlotTextFlags_Vertical});
        if (show_stations_) {
            for (size_t station_index = 0;
                 station_index < data.stations.size(); ++station_index) {
                const Station& s = data.stations[station_index];
                double x = s.distance;
                ImPlot::PlotInfLines(("##rst" + s.key).c_str(), &x, 1, {ImPlotProp_LineColor, ImVec4(1, 1, 1, 0.55f), ImPlotProp_LineWeight, line_widths.chart_marker_px, ImPlotProp_Flags, ImPlotItemFlags_NoLegend});
                if (show_station_names_) draw_fixed_y_plot_text(x, s.name, IM_COL32(255, 255, 255, 255), FixedPlotY::Top);
                if (show_station_mileage_) {
                    draw_fixed_y_plot_text(
                        x, data.station_mileage_labels[station_index],
                        IM_COL32(255, 216, 77, 255), FixedPlotY::Bottom);
                }
            }
        }
        if (mode_ == Mode::Measure && ImPlot::IsPlotHovered()) {
            set_crosshair_cursor();
            ImPlotPoint p = ImPlot::GetPlotMousePos();
            if (p.x >= dmin_ && p.x <= dmax_) update_measure(p.x);
        }
        handle_measure_plot_double_click(true, false);
        if (mode_ == Mode::Measure && measure_distance_) {
            double x = *measure_distance_;
            ImPlot::PlotInfLines("##measure_radius", &x, 1, {ImPlotProp_LineColor, ImVec4(1, 0.2f, 0.2f, 1), ImPlotProp_LineWeight, 2.0f, ImPlotProp_Flags, ImPlotItemFlags_NoLegend});
        }
        ImPlotRect limits = ImPlot::GetPlotLimits();
        radius_x_span_ = std::abs(limits.X.Size());
        ImVec2 plot_pos = ImPlot::GetPlotPos();
        ImVec2 plot_size = ImPlot::GetPlotSize();
        PlotTouchZoom touch_zoom = plot_touch_zoom_limits(limits, false);
        if (touch_zoom.x) {
            radius_x_zoom_min_ = touch_zoom.x_min;
            radius_x_zoom_max_ = touch_zoom.x_max;
            radius_x_zoom_pending_ = true;
        } else if (consumed_radius_x_zoom) {
            radius_x_zoom_pending_ = false;
        }
        mask_plot_axis_tick_edges(frame_min, frame_max, plot_pos, plot_size);
        draw_radius_side_markers();
        draw_plot_overlay_labels(tr("plot.radius"), tr("unit.m"));
        focus_radius_next_ = false;
        reset_radius_axes_next_ = false;
        ImPlot::EndPlot();
    }
    ImPlot::PopStyleVar(4);
}

void App::render_plots() {
    if (!show_plots_window_) {
        finish_pending_load_timing_after_plan_data_ready();
        return;
    }
    std::string title = tr("frame.plots") + "###Plots";
    if (dock_main_id_) ImGui::SetNextWindowDockID(dock_main_id_, ImGuiCond_FirstUseEver);
    if (focus_plots_next_) ImGui::SetNextWindowFocus();
    if (!ImGui::Begin(title.c_str(), &show_plots_window_)) {
        focus_plots_next_ = false;
        finish_pending_load_timing_after_plan_data_ready();
        ImGui::End();
        return;
    }
    render_mode_grid_controls();
    if (pick_slot_ != 0) ImGui::TextUnformatted(tr("hint.pick_bg_station").c_str());
    else if (mode_ == Mode::Measure && !measure_text_.empty()) ImGui::TextUnformatted(measure_text_.c_str());
    else ImGui::TextDisabled("%s", has_model_ ? "" : tr("status.no_map").c_str());

    ImVec2 avail = ImGui::GetContentRegionAvail();
    float splitter_h = 6.0f;
    float plan_h = avail.y;
    bool any_graph = show_profile_graph_ || show_radius_graph_;
    if (any_graph) plan_h = std::max(80.0f, avail.y * static_cast<float>(plan_height_) - splitter_h);
    render_plan_canvas(ImVec2(avail.x, plan_h));

    if (any_graph) {
        ImGui::InvisibleButton("##vsplit", ImVec2(avail.x, splitter_h));
        if (ImGui::IsItemActive()) {
            plan_height_ += ImGui::GetIO().MouseDelta.y / std::max(1.0f, avail.y);
            plan_height_ = std::clamp(plan_height_, 0.2, 0.86);
        }
        const ProfileData& profile = current_profile_data();
        ImVec2 graph_avail = ImGui::GetContentRegionAvail();
        if (show_profile_graph_ && show_radius_graph_) {
            float left_w = graph_avail.x * static_cast<float>(graph_split_);
            render_profile_plot(profile, ImVec2(left_w - 3.0f, graph_avail.y));
            ImGui::SameLine();
            ImGui::InvisibleButton("##hsplit", ImVec2(6.0f, graph_avail.y));
            if (ImGui::IsItemActive()) {
                graph_split_ += ImGui::GetIO().MouseDelta.x / std::max(1.0f, graph_avail.x);
                graph_split_ = std::clamp(graph_split_, 0.2, 0.8);
            }
            ImGui::SameLine();
            render_radius_plot(profile, ImVec2(-1.0f, graph_avail.y));
        } else if (show_profile_graph_) {
            render_profile_plot(profile, graph_avail);
        } else if (show_radius_graph_) {
            render_radius_plot(profile, graph_avail);
        }
    }
    if (has_model_) finish_pending_load_timing(std::chrono::steady_clock::now());
    focus_plots_next_ = false;
    ImGui::End();
}
