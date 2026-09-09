/*
 * Copyright (c) 2026 Sapporo_ningyo
 *
 * Licensed under Apache License 2.0; see LICENSE and NOTICE.
 * The GUI uses Dear ImGui; see THIRD_PARTY_NOTICES.md.
 */

#ifdef _MSC_VER
#pragma execution_character_set("utf-8")
#endif

#include "canvas2d_primitives.h"

#include "canvas2d_marker_cache.h"
#include "map_marker_visuals.h"

#include "imgui_internal.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace canvas2d {

ImU32 color_u32(const ImVec4& color) {
    return ImGui::ColorConvertFloat4ToU32(color);
}

constexpr size_t k_polyline_chunk_point_limit = 4096;
constexpr float k_polyline_min_pixel_step_sq = 0.64f;

static bool finite_screen_point(ImVec2 p) {
    return std::isfinite(p.x) && std::isfinite(p.y);
}

ImVec2 PlanScreenTransform::plan_to_screen(double x, double y) const {
    const double dx = x - cx;
    const double dy = y - cy;
    const double rx = view_c * dx - view_s * dy;
    const double ry = view_s * dx + view_c * dy;
    return ImVec2(screen_cx + static_cast<float>(rx * scale),
                  screen_cy + static_cast<float>(ry * scale));
}

std::pair<double, double> PlanScreenTransform::screen_to_plan(ImVec2 screen) const {
    const double rx = (static_cast<double>(screen.x) - screen_cx) / scale;
    const double ry = (static_cast<double>(screen.y) - screen_cy) / scale;
    const double dx = view_c * rx + view_s * ry;
    const double dy = -view_s * rx + view_c * ry;
    return {cx + dx, cy + dy};
}

std::pair<double, double> PlanScreenTransform::model_to_plan(double x, double y) const {
    return {model_c * x - model_s * y, model_s * x + model_c * y};
}

ImVec2 PlanScreenTransform::model_to_screen(double x, double y) const {
    const auto point = model_to_plan(x, y);
    return plan_to_screen(point.first, point.second);
}

PlanScreenTransform make_plan_transform(const View2D& view, double model_angle,
                                               ImVec2 origin, ImVec2 size) {
    PlanScreenTransform transform;
    transform.model_c = std::cos(model_angle);
    transform.model_s = std::sin(model_angle);
    transform.view_c = std::cos(view.rotation);
    transform.view_s = std::sin(view.rotation);
    transform.cx = view.cx;
    transform.cy = view.cy;
    transform.scale = view.scale;
    transform.screen_cx = origin.x + size.x * 0.5f;
    transform.screen_cy = origin.y + size.y * 0.5f;
    return transform;
}


class ScreenPolylineBuilder {
public:
    ScreenPolylineBuilder(ImDrawList* draw, ImVec2 origin, ImVec2 size, ImU32 color, float thickness)
        : draw_(draw), origin_(origin), size_(size), color_(color), thickness_(thickness) {
        points_.reserve(k_polyline_chunk_point_limit);
        reset_bounds();
    }

    void append(ImVec2 p) {
        if (!finite_screen_point(p)) {
            flush(false);
            has_last_ = false;
            has_pending_ = false;
            return;
        }

        if (!has_last_) {
            push_raw(p);
            last_ = p;
            has_last_ = true;
            return;
        }

        float dx = p.x - last_.x;
        float dy = p.y - last_.y;
        if (dx * dx + dy * dy >= k_polyline_min_pixel_step_sq) {
            push_raw(p);
            last_ = p;
            has_pending_ = false;
            if (points_.size() >= k_polyline_chunk_point_limit) flush(true);
        } else {
            pending_ = p;
            has_pending_ = true;
        }
    }

    void finish() {
        if (has_pending_) {
            push_raw(pending_);
            has_pending_ = false;
        }
        flush(false);
    }

    void break_line() {
        flush(false);
        has_last_ = false;
        has_pending_ = false;
    }

private:
    void push_raw(ImVec2 p) {
        points_.push_back(p);
        min_x_ = std::min(min_x_, p.x);
        min_y_ = std::min(min_y_, p.y);
        max_x_ = std::max(max_x_, p.x);
        max_y_ = std::max(max_y_, p.y);
    }

    void reset_bounds() {
        min_x_ = std::numeric_limits<float>::max();
        min_y_ = std::numeric_limits<float>::max();
        max_x_ = -std::numeric_limits<float>::max();
        max_y_ = -std::numeric_limits<float>::max();
    }

    bool overlaps_canvas() const {
        const float margin = 32.0f;
        return !(max_x_ < origin_.x - margin || min_x_ > origin_.x + size_.x + margin ||
                 max_y_ < origin_.y - margin || min_y_ > origin_.y + size_.y + margin);
    }

    void flush(bool keep_tail) {
        if (points_.size() >= 2 && overlaps_canvas()) {
            draw_->AddPolyline(points_.data(), static_cast<int>(points_.size()), color_, thickness_, ImDrawFlags_None);
        }
        ImVec2 tail = points_.empty() ? ImVec2(0.0f, 0.0f) : points_.back();
        points_.clear();
        reset_bounds();
        if (keep_tail) push_raw(tail);
    }

    ImDrawList* draw_ = nullptr;
    ImVec2 origin_;
    ImVec2 size_;
    ImU32 color_ = 0;
    float thickness_ = 1.0f;
    std::vector<ImVec2> points_;
    ImVec2 last_ = ImVec2(0.0f, 0.0f);
    ImVec2 pending_ = ImVec2(0.0f, 0.0f);
    bool has_last_ = false;
    bool has_pending_ = false;
    float min_x_ = 0.0f;
    float min_y_ = 0.0f;
    float max_x_ = 0.0f;
    float max_y_ = 0.0f;
};

void draw_polyline_range(ImDrawList* draw, const std::vector<TrackPoint>& points,
                                size_t first, size_t last, const PlanScreenTransform& transform,
                                ImVec2 origin, ImVec2 size, ImU32 color, float thickness) {
    if (last <= first + 1 || first >= points.size()) return;
    last = std::min(last, points.size());
    ScreenPolylineBuilder builder(draw, origin, size, color, thickness);
    double min_d_step = std::max(0.0, 0.45 / std::max(std::abs(transform.scale), 1e-9));
    double last_appended_d = -std::numeric_limits<double>::infinity();
    for (size_t i = first; i < last; ++i) {
        bool endpoint = i == first || i + 1 == last;
        if (!endpoint && points[i].d - last_appended_d < min_d_step) continue;
        builder.append(transform.plan_to_screen(points[i].x, points[i].y));
        last_appended_d = points[i].d;
    }
    builder.finish();
}

void draw_polyline(ImDrawList* draw, const std::vector<TrackPoint>& points,
                          const PlanScreenTransform& transform, ImVec2 origin, ImVec2 size,
                          ImU32 color, float thickness) {
    draw_polyline_range(draw, points, 0, points.size(), transform, origin, size, color, thickness);
}

void draw_matrix_plan_polyline(ImDrawList* draw, const Matrix& points, double rmin, double rmax,
                                      const PlanScreenTransform& transform, ImVec2 origin, ImVec2 size,
                                      ImU32 color, float thickness) {
    if (points.rows < 2 || points.cols < 3 || rmax < rmin) return;
    size_t first = matrix_lower_bound_distance(points, rmin);
    size_t last = matrix_upper_bound_distance(points, rmax);
    if (last <= first + 1) return;

    ScreenPolylineBuilder builder(draw, origin, size, color, thickness);
    double min_d_step = std::max(0.0, 0.45 / std::max(std::abs(transform.scale), 1e-9));
    double last_appended_d = -std::numeric_limits<double>::infinity();
    for (size_t row = first; row < last; ++row) {
        double d = points.at(row, 0);
        bool endpoint = row == first || row + 1 == last;
        if (!endpoint && d - last_appended_d < min_d_step) continue;
        builder.append(transform.model_to_screen(points.at(row, 1), points.at(row, 2)));
        last_appended_d = d;
    }
    builder.finish();
}

static bool distance_ranges_overlap(double a_min, double a_max, double b_min, double b_max) {
    return a_max >= b_min && a_min <= b_max;
}

static bool screen_bounds_overlap_canvas(const PlanScreenTransform& transform,
                                         double x_min, double y_min, double x_max, double y_max,
                                         ImVec2 origin, ImVec2 size, float margin) {
    ImVec2 corners[] = {
        transform.model_to_screen(x_min, y_min),
        transform.model_to_screen(x_max, y_min),
        transform.model_to_screen(x_max, y_max),
        transform.model_to_screen(x_min, y_max),
    };
    if (!finite_screen_point(corners[0])) return true;
    float min_x = corners[0].x;
    float max_x = corners[0].x;
    float min_y = corners[0].y;
    float max_y = corners[0].y;
    for (int i = 1; i < IM_ARRAYSIZE(corners); ++i) {
        if (!finite_screen_point(corners[i])) return true;
        min_x = std::min(min_x, corners[i].x);
        max_x = std::max(max_x, corners[i].x);
        min_y = std::min(min_y, corners[i].y);
        max_y = std::max(max_y, corners[i].y);
    }
    return !(max_x < origin.x - margin || min_x > origin.x + size.x + margin ||
             max_y < origin.y - margin || min_y > origin.y + size.y + margin);
}

static const TrackPoint* first_repeater_segment_point_in_range(const PlanRepeaterSegment& segment,
                                                               double dmin, double dmax) {
    if (!segment.bounds_valid || !distance_ranges_overlap(segment.d_min, segment.d_max, dmin, dmax)) {
        return nullptr;
    }
    if (segment.d_min >= dmin && segment.d_max <= dmax) {
        for (const PlanRepeaterSegment::Chunk& chunk : segment.chunks) {
            if (!chunk.points.empty()) return &chunk.points.front();
        }
        return nullptr;
    }
    for (const PlanRepeaterSegment::Chunk& chunk : segment.chunks) {
        if (!chunk.bounds_valid || !distance_ranges_overlap(chunk.d_min, chunk.d_max, dmin, dmax)) continue;
        auto it = std::lower_bound(chunk.points.begin(), chunk.points.end(), dmin,
                                   [](const TrackPoint& point, double distance) {
                                       return point.d < distance;
                                   });
        if (it != chunk.points.end() && it->d <= dmax) return &*it;
    }
    return nullptr;
}

static const TrackPoint* last_repeater_segment_point_in_range(const PlanRepeaterSegment& segment,
                                                              double dmin, double dmax) {
    if (!segment.bounds_valid || !distance_ranges_overlap(segment.d_min, segment.d_max, dmin, dmax)) {
        return nullptr;
    }
    if (segment.d_min >= dmin && segment.d_max <= dmax) {
        for (auto chunk_it = segment.chunks.rbegin(); chunk_it != segment.chunks.rend(); ++chunk_it) {
            if (!chunk_it->points.empty()) return &chunk_it->points.back();
        }
        return nullptr;
    }
    for (auto chunk_it = segment.chunks.rbegin(); chunk_it != segment.chunks.rend(); ++chunk_it) {
        const PlanRepeaterSegment::Chunk& chunk = *chunk_it;
        if (!chunk.bounds_valid || !distance_ranges_overlap(chunk.d_min, chunk.d_max, dmin, dmax)) continue;
        auto it = std::upper_bound(chunk.points.begin(), chunk.points.end(), dmax,
                                   [](double distance, const TrackPoint& point) {
                                       return distance < point.d;
                                   });
        if (it == chunk.points.begin()) continue;
        --it;
        if (it->d >= dmin) return &*it;
    }
    return nullptr;
}

static void write_overview_line_quad(ImDrawList* draw, ImVec2 a, ImVec2 b,
                                     ImU32 color, float half_thickness, const ImVec2& uv) {
    float dx = b.x - a.x;
    float dy = b.y - a.y;
    float len_sq = dx * dx + dy * dy;
    if (len_sq <= 1e-4f) return;
    float inv_len = 1.0f / std::sqrt(len_sq);
    dx *= inv_len * half_thickness;
    dy *= inv_len * half_thickness;
    ImVec2 normal(dy, -dx);

    draw->_VtxWritePtr[0].pos = ImVec2(a.x + normal.x, a.y + normal.y);
    draw->_VtxWritePtr[0].uv = uv;
    draw->_VtxWritePtr[0].col = color;
    draw->_VtxWritePtr[1].pos = ImVec2(b.x + normal.x, b.y + normal.y);
    draw->_VtxWritePtr[1].uv = uv;
    draw->_VtxWritePtr[1].col = color;
    draw->_VtxWritePtr[2].pos = ImVec2(b.x - normal.x, b.y - normal.y);
    draw->_VtxWritePtr[2].uv = uv;
    draw->_VtxWritePtr[2].col = color;
    draw->_VtxWritePtr[3].pos = ImVec2(a.x - normal.x, a.y - normal.y);
    draw->_VtxWritePtr[3].uv = uv;
    draw->_VtxWritePtr[3].col = color;
    draw->_VtxWritePtr += 4;

    draw->_IdxWritePtr[0] = static_cast<ImDrawIdx>(draw->_VtxCurrentIdx);
    draw->_IdxWritePtr[1] = static_cast<ImDrawIdx>(draw->_VtxCurrentIdx + 1);
    draw->_IdxWritePtr[2] = static_cast<ImDrawIdx>(draw->_VtxCurrentIdx + 2);
    draw->_IdxWritePtr[3] = static_cast<ImDrawIdx>(draw->_VtxCurrentIdx);
    draw->_IdxWritePtr[4] = static_cast<ImDrawIdx>(draw->_VtxCurrentIdx + 2);
    draw->_IdxWritePtr[5] = static_cast<ImDrawIdx>(draw->_VtxCurrentIdx + 3);
    draw->_IdxWritePtr += 6;
    draw->_VtxCurrentIdx += 4;
}

static bool screen_line_overlaps_canvas(ImVec2 a, ImVec2 b, ImVec2 origin, ImVec2 size, float margin) {
    float min_x = std::min(a.x, b.x);
    float max_x = std::max(a.x, b.x);
    float min_y = std::min(a.y, b.y);
    float max_y = std::max(a.y, b.y);
    return !(max_x < origin.x - margin || min_x > origin.x + size.x + margin ||
             max_y < origin.y - margin || min_y > origin.y + size.y + margin);
}

static void draw_repeater_segment_overview(ImDrawList* draw,
                                           const std::vector<RepeaterOverlayRow>& rows,
                                           const std::vector<unsigned char>& visible,
                                           double dmin, double dmax,
                                           const PlanScreenTransform& transform,
                                           ImVec2 origin, ImVec2 size,
                                           ImU32 color, float thickness) {
    const size_t row_count = std::min(rows.size(), visible.size());
    if (row_count == 0) return;

    constexpr float coarse_margin = 96.0f;
    const float half_thickness = std::max(0.5f, thickness * 0.5f);
    const ImVec2 uv = draw->_Data->TexUvWhitePixel;
    constexpr size_t batch_line_limit = 4096;
    int unused_lines = 0;
    for (size_t row = 0; row < row_count; ++row) {
        if (!visible[row]) continue;
        const PlanRepeaterSegment& segment = rows[row].segment;
        if (!segment.bounds_valid || !distance_ranges_overlap(segment.d_min, segment.d_max, dmin, dmax)) continue;
        const bool full_segment_visible = segment.endpoints_valid && segment.d_min >= dmin && segment.d_max <= dmax;
        const TrackPoint* first = full_segment_visible
            ? &segment.first_point
            : first_repeater_segment_point_in_range(segment, dmin, dmax);
        const TrackPoint* last = full_segment_visible
            ? &segment.last_point
            : last_repeater_segment_point_in_range(segment, dmin, dmax);
        if (!first || !last || first == last) continue;
        ImVec2 a = transform.model_to_screen(first->x, first->y);
        ImVec2 b = transform.model_to_screen(last->x, last->y);
        if (!finite_screen_point(a) || !finite_screen_point(b)) continue;
        float dx = b.x - a.x;
        float dy = b.y - a.y;
        if (dx * dx + dy * dy < 4.0f) continue;
        if (!screen_line_overlaps_canvas(a, b, origin, size, coarse_margin)) continue;
        if (unused_lines == 0) {
            unused_lines = static_cast<int>(std::min(batch_line_limit, row_count - row));
            draw->PrimReserve(unused_lines * 6, unused_lines * 4);
        }
        write_overview_line_quad(draw, a, b, color, half_thickness, uv);
        --unused_lines;
    }
    if (unused_lines > 0) draw->PrimUnreserve(unused_lines * 6, unused_lines * 4);
}

#ifndef NDEBUG
bool debug_repeater_overview_indices() {
    constexpr size_t row_count = 17004;
    std::vector<RepeaterOverlayRow> rows(row_count);
    std::vector<unsigned char> visible(row_count, 1);
    for (size_t i = 0; i < rows.size(); ++i) {
        PlanRepeaterSegment& segment = rows[i].segment;
        segment.bounds_valid = true;
        segment.endpoints_valid = true;
        segment.d_min = 0.0;
        segment.d_max = 1.0;
        segment.first_point.d = 0.0;
        segment.first_point.x = 10.0;
        segment.first_point.y = 10.0 + static_cast<double>(i % 50);
        segment.last_point = segment.first_point;
        segment.last_point.d = 1.0;
        segment.last_point.x = 30.0;
    }
    visible.front() = 0;
    visible.back() = 0;
    rows[1].segment.last_point.x = rows[1].segment.first_point.x;
    rows[2].segment.first_point.x = 1000.0;
    rows[2].segment.last_point.x = 1020.0;
    ImDrawList& draw = *ImGui::GetBackgroundDrawList();
    draw.Flags |= ImDrawListFlags_AllowVtxOffset;
    draw.PushClipRect(ImVec2(0.0f, 0.0f), ImVec2(200.0f, 200.0f));
    draw_repeater_segment_overview(
        &draw, rows, visible, 0.0, 1.0, PlanScreenTransform{},
        ImVec2(0.0f, 0.0f), ImVec2(200.0f, 200.0f), IM_COL32_WHITE, 1.0f);
    draw.PopClipRect();
    // Exercise the buffer assertions also used by ImGui::Render().
    ImDrawData draw_data;
    draw_data.AddDrawList(&draw);
    constexpr size_t emitted_lines = row_count - 4;
    if (draw.VtxBuffer.Size != static_cast<int>(emitted_lines * 4) ||
        draw.IdxBuffer.Size != static_cast<int>(emitted_lines * 6)) return false;
    constexpr size_t quad_indices[] = {0, 1, 2, 0, 2, 3};
    size_t covered_indices = 0;
    bool used_vertex_offset = false;
    for (const ImDrawCmd& command : draw.CmdBuffer) {
        used_vertex_offset = used_vertex_offset || command.VtxOffset != 0;
        for (unsigned int i = 0; i < command.ElemCount; ++i) {
            const size_t index = static_cast<size_t>(command.IdxOffset) + i;
            if (index >= static_cast<size_t>(draw.IdxBuffer.Size)) return false;
            const size_t vertex = static_cast<size_t>(command.VtxOffset) +
                draw.IdxBuffer[static_cast<int>(index)];
            if (vertex != (index / 6) * 4 + quad_indices[index % 6]) return false;
        }
        covered_indices += command.ElemCount;
    }
    return covered_indices == emitted_lines * 6 &&
        (sizeof(ImDrawIdx) != 2 || used_vertex_offset);
}
#endif

void draw_repeater_segment_chunks(ImDrawList* draw,
                                         const std::vector<RepeaterOverlayRow>& rows,
                                         const std::vector<unsigned char>& visible,
                                         double dmin, double dmax,
                                         const PlanScreenTransform& transform,
                                         ImVec2 origin, ImVec2 size,
                                         ImU32 color, float thickness) {
    const size_t row_count = std::min(rows.size(), visible.size());
    if (row_count == 0) return;
    size_t visible_row_count = 0;
    for (size_t row = 0; row < row_count; ++row) {
        if (visible[row]) ++visible_row_count;
    }
    if (visible_row_count == 0) return;

    constexpr float coarse_margin = 96.0f;
    if (dense_repeater_overview_lod(visible_row_count, transform.scale)) {
        draw_repeater_segment_overview(draw, rows, visible, dmin, dmax, transform,
                                       origin, size, color, thickness);
        return;
    }

    bool dense_overlay = dense_repeater_segment_lod(visible_row_count, transform.scale);
    double detail_pixel_step = dense_overlay ? 4.0 : 0.45;
    ImDrawListFlags old_flags = draw->Flags;
    if (dense_overlay) draw->Flags &= ~ImDrawListFlags_AntiAliasedLines;
    ScreenPolylineBuilder builder(draw, origin, size, color, thickness);
    double min_d_step = std::max(0.0, detail_pixel_step / std::max(std::abs(transform.scale), 1e-9));
    for (size_t row = 0; row < row_count; ++row) {
        if (!visible[row]) continue;
        const PlanRepeaterSegment& segment = rows[row].segment;
        if (!segment.bounds_valid || !distance_ranges_overlap(segment.d_min, segment.d_max, dmin, dmax)) continue;
        if (!screen_bounds_overlap_canvas(transform, segment.x_min, segment.y_min, segment.x_max, segment.y_max,
                                          origin, size, coarse_margin)) {
            continue;
        }

        bool segment_open = false;
        double last_appended_d = -std::numeric_limits<double>::infinity();
        for (const PlanRepeaterSegment::Chunk& chunk : segment.chunks) {
            bool chunk_visible = chunk.bounds_valid &&
                distance_ranges_overlap(chunk.d_min, chunk.d_max, dmin, dmax) &&
                screen_bounds_overlap_canvas(transform, chunk.x_min, chunk.y_min, chunk.x_max, chunk.y_max,
                                             origin, size, coarse_margin);
            if (!chunk_visible) {
                if (segment_open) {
                    builder.break_line();
                    segment_open = false;
                }
                continue;
            }

            bool contains_segment_end = std::abs(chunk.d_max - segment.d_max) < 1e-6;
            if (dense_overlay && std::isfinite(last_appended_d) && !contains_segment_end &&
                chunk.d_max - last_appended_d < min_d_step) {
                continue;
            }

            bool appended = false;
            for (size_t point_index = 0; point_index < chunk.points.size();) {
                const TrackPoint& point = chunk.points[point_index];
                if (point.d < dmin) {
                    ++point_index;
                    continue;
                }
                if (point.d > dmax) break;
                bool endpoint = dense_overlay
                    ? (std::abs(point.d - segment.d_min) < 1e-6 || std::abs(point.d - segment.d_max) < 1e-6)
                    : (point_index == 0 || point_index + 1 == chunk.points.size());
                if (!endpoint && point.d - last_appended_d < min_d_step) {
                    if (dense_overlay && std::isfinite(last_appended_d)) {
                        double target_d = last_appended_d + min_d_step;
                        auto next_it = std::lower_bound(chunk.points.begin() + static_cast<std::ptrdiff_t>(point_index + 1),
                                                        chunk.points.end(), target_d,
                                                        [](const TrackPoint& candidate, double target) {
                                                            return candidate.d < target;
                                                        });
                        if (next_it == chunk.points.end() &&
                            std::abs(chunk.d_max - segment.d_max) < 1e-6 &&
                            !chunk.points.empty()) {
                            point_index = chunk.points.size() - 1;
                        } else {
                            point_index = static_cast<size_t>(next_it - chunk.points.begin());
                        }
                        continue;
                    }
                    ++point_index;
                    continue;
                }
                builder.append(transform.model_to_screen(point.x, point.y));
                last_appended_d = point.d;
                appended = true;
                ++point_index;
            }
            if (appended) {
                segment_open = true;
            } else if (segment_open) {
                builder.break_line();
                segment_open = false;
            }
        }
        if (segment_open) builder.break_line();
    }
    builder.finish();
    draw->Flags = old_flags;
}

double grid_step(double span) {
    double raw = std::max(span / 8.0, 1e-9);
    double mag = std::pow(10.0, std::floor(std::log10(raw)));
    for (double f : {1.0, 2.0, 5.0, 10.0}) {
        if (raw <= f * mag) return f * mag;
    }
    return 10.0 * mag;
}

static double friendly_scalebar_length(double raw_length) {
    if (raw_length <= 0.0 || !std::isfinite(raw_length)) return 1.0;
    double magnitude = std::pow(10.0, std::floor(std::log10(raw_length)));
    double best = magnitude;
    double best_diff = std::numeric_limits<double>::max();
    for (int exp_offset : {-1, 0, 1, 2}) {
        double base = magnitude * std::pow(10.0, exp_offset);
        for (double factor : {1.0, 2.0, 3.0, 5.0}) {
            double candidate = factor * base;
            if (candidate <= 0.0) continue;
            double diff = std::abs(candidate - raw_length);
            if (diff < best_diff) {
                best = candidate;
                best_diff = diff;
            }
        }
    }
    return best;
}

static std::string format_scalebar_label(double length) {
    if (length >= 1000.0) {
        double km = length / 1000.0;
        return format_double(km, std::abs(km - std::round(km)) < 1e-9 ? 0 : 1) + "km";
    }
    return format_double(length, std::abs(length - std::round(length)) < 1e-9 ? 0 : 1) + "m";
}

void draw_scalebar(ImDrawList* draw, const View2D& view, ImVec2 origin, ImVec2 size) {
    if (view.scale <= 0.0 || !std::isfinite(view.scale)) return;
    float target_px = std::clamp(size.x * 0.18f, 90.0f, 180.0f);
    double length = friendly_scalebar_length(static_cast<double>(target_px) / view.scale);
    float bar_px = static_cast<float>(length * view.scale);
    if (!std::isfinite(bar_px) || bar_px <= 0.0f) return;

    float margin = 24.0f;
    float tick = 10.0f;
    ImVec2 p2(origin.x + size.x - margin, origin.y + size.y - margin);
    ImVec2 p1(p2.x - bar_px, p2.y);
    if (p1.x < origin.x + margin) return;

    ImU32 color = IM_COL32(255, 255, 255, 255);
    ImVec2 points[] = {ImVec2(p1.x, p1.y - tick), p1, p2, ImVec2(p2.x, p2.y - tick)};
    draw->AddPolyline(points, IM_ARRAYSIZE(points), color, ImDrawFlags_None, 2.0f);
    std::string label = format_scalebar_label(length);
    ImVec2 text_size = ImGui::CalcTextSize(label.c_str());
    draw->AddText(ImVec2((p1.x + p2.x - text_size.x) * 0.5f, p1.y - tick - 4.0f - text_size.y),
                  color, label.c_str());
}

void draw_plan_triangle_marker(ImDrawList* draw, ImVec2 p, ImU32 color, float scale) {
    const float r = 6.0f * scale;
    ImVec2 pts[3] = {
        ImVec2(p.x, p.y - r),
        ImVec2(p.x - r * 0.9f, p.y + r * 0.72f),
        ImVec2(p.x + r * 0.9f, p.y + r * 0.72f),
    };
    draw->AddConvexPolyFilled(pts, IM_ARRAYSIZE(pts), color);
    draw->AddPolyline(pts, IM_ARRAYSIZE(pts), IM_COL32(64, 48, 0, 255), ImDrawFlags_Closed, 1.0f);
}

void draw_plan_diamond_marker(ImDrawList* draw, ImVec2 p, ImU32 color, float scale) {
    const float r = 5.5f * scale;
    ImVec2 pts[4] = {
        ImVec2(p.x, p.y - r),
        ImVec2(p.x + r, p.y),
        ImVec2(p.x, p.y + r),
        ImVec2(p.x - r, p.y),
    };
    draw->AddConvexPolyFilled(pts, IM_ARRAYSIZE(pts), color);
    draw->AddPolyline(pts, IM_ARRAYSIZE(pts), IM_COL32(80, 0, 48, 255), ImDrawFlags_Closed, 1.0f);
}

void draw_plan_signal_marker(ImDrawList* draw, ImVec2 p, ImU32 color, float scale) {
    const float circle_r = 4.6f * scale;
    const float line_weight = 1.8f * scale;
    const ImU32 shadow = IM_COL32(8, 42, 30, 230);
    ImVec2 circle_center(p.x, p.y - 3.2f * scale);
    draw->AddCircle(circle_center, circle_r, shadow, 20, line_weight + 1.8f * scale);
    draw->AddCircle(circle_center, circle_r, color, 20, line_weight);
    draw->AddLine(ImVec2(p.x, p.y + 1.8f * scale), ImVec2(p.x, p.y + 7.2f * scale),
                  shadow, line_weight + 1.8f * scale);
    draw->AddLine(ImVec2(p.x - 5.0f * scale, p.y + 7.2f * scale),
                  ImVec2(p.x + 5.0f * scale, p.y + 7.2f * scale),
                  shadow, line_weight + 1.8f * scale);
    draw->AddLine(ImVec2(p.x, p.y + 1.8f * scale), ImVec2(p.x, p.y + 7.2f * scale),
                  color, line_weight);
    draw->AddLine(ImVec2(p.x - 5.0f * scale, p.y + 7.2f * scale),
                  ImVec2(p.x + 5.0f * scale, p.y + 7.2f * scale),
                  color, line_weight);
}

void draw_plan_pretrain_marker(ImDrawList* draw, ImVec2 p, const std::string& label, float scale) {
    const float half = 7.0f * scale;
    const ImU32 white = map_marker_theme_color_u32(MapMarkerVisualKind::PreTrain);
    const ImU32 shadow = IM_COL32(0, 0, 0, 220);
    draw_map_marker_icon(draw, MapMarkerVisualKind::PreTrain, p, half);
    if (!label.empty()) {
        ImVec2 label_pos(p.x + half + 5.0f * scale,
                         p.y - ImGui::GetTextLineHeight() * 0.5f);
        draw->AddText(ImVec2(label_pos.x + 1.0f, label_pos.y + 1.0f), shadow, label.c_str());
        draw->AddText(label_pos, white, label.c_str());
    }
}

void draw_plan_focus_arrow(ImDrawList* draw, ImVec2 target) {
    const float length = 30.0f;
    const float head = 11.0f;
    const float half_height = 7.0f;
    const float target_gap = 10.0f;
    ImU32 fill = IM_COL32(255, 32, 32, 255);
    ImVec2 tip(target.x - target_gap, target.y);
    ImVec2 tail(tip.x - length, tip.y);
    ImVec2 neck(tip.x - head, tip.y);
    draw->AddLine(tail, neck, fill, 3.0f);
    ImVec2 pts[3] = {
        tip,
        ImVec2(tip.x - head, tip.y - half_height),
        ImVec2(tip.x - head, tip.y + half_height),
    };
    draw->AddConvexPolyFilled(pts, IM_ARRAYSIZE(pts), fill);
}

void draw_plan_current_position_arrow(ImDrawList* draw, ImVec2 center, ImVec2 direction, float scale) {
    float len = std::sqrt(direction.x * direction.x + direction.y * direction.y);
    if (len < 1e-3f) return;
    ImVec2 forward(direction.x / len, direction.y / len);
    ImVec2 normal(-forward.y, forward.x);
    const float tip_len = 18.0f * scale;
    const float tail_back = 10.0f * scale;
    const float notch_back = 3.0f * scale;
    const float half_width = 8.0f * scale;
    ImVec2 pts[4] = {
        ImVec2(center.x + forward.x * tip_len,
               center.y + forward.y * tip_len),
        ImVec2(center.x - forward.x * tail_back + normal.x * half_width,
               center.y - forward.y * tail_back + normal.y * half_width),
        ImVec2(center.x - forward.x * notch_back,
               center.y - forward.y * notch_back),
        ImVec2(center.x - forward.x * tail_back - normal.x * half_width,
               center.y - forward.y * tail_back - normal.y * half_width),
    };
    const ImU32 fill = IM_COL32(0, 122, 255, 255);
    draw->AddTriangleFilled(pts[0], pts[1], pts[2], fill);
    draw->AddTriangleFilled(pts[0], pts[2], pts[3], fill);
    draw->AddPolyline(pts, IM_ARRAYSIZE(pts), IM_COL32(255, 255, 255, 255), ImDrawFlags_Closed, 3.0f * scale);
}

void draw_plan_direction_arrow(ImDrawList* draw, ImVec2 center, ImVec2 direction,
                                      ImU32 fill, ImU32 outline, float scale) {
    float len = std::sqrt(direction.x * direction.x + direction.y * direction.y);
    if (len < 1e-3f) return;
    ImVec2 forward(direction.x / len, direction.y / len);
    ImVec2 normal(-forward.y, forward.x);
    const float tip_len = 14.0f * scale;
    const float tail_back = 8.0f * scale;
    const float half_width = 6.0f * scale;
    ImVec2 pts[3] = {
        ImVec2(center.x + forward.x * tip_len,
               center.y + forward.y * tip_len),
        ImVec2(center.x - forward.x * tail_back + normal.x * half_width,
               center.y - forward.y * tail_back + normal.y * half_width),
        ImVec2(center.x - forward.x * tail_back - normal.x * half_width,
               center.y - forward.y * tail_back - normal.y * half_width),
    };
    draw->AddTriangleFilled(pts[0], pts[1], pts[2], fill);
    draw->AddPolyline(pts, IM_ARRAYSIZE(pts), outline, ImDrawFlags_Closed, 2.0f * scale);
}

void draw_plan_small_text(ImDrawList* draw, ImVec2 p, ImU32 color, const std::string& text) {
    if (text.empty()) return;
    draw->AddText(nullptr, ImGui::GetFontSize() * 0.78f, ImVec2(p.x + 8.0f, p.y - 9.0f), color, text.c_str());
}

bool point_near_canvas(ImVec2 p, ImVec2 origin, ImVec2 size, float margin) {
    return p.x >= origin.x - margin && p.x <= origin.x + size.x + margin &&
           p.y >= origin.y - margin && p.y <= origin.y + size.y + margin;
}

}  // namespace canvas2d
