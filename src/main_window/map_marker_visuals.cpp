/*
 * Copyright (c) 2026 Sapporo_ningyo
 *
 * Licensed under Apache License 2.0; see LICENSE and NOTICE.
 */

#include "map_marker_visuals.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace {

constexpr float k_pi = 3.14159265358979323846f;

ImVec4 rgba(unsigned int r, unsigned int g, unsigned int b, unsigned int a = 255) {
    constexpr float scale = 1.0f / 255.0f;
    return ImVec4(static_cast<float>(r) * scale,
                  static_cast<float>(g) * scale,
                  static_cast<float>(b) * scale,
                  static_cast<float>(a) * scale);
}

MapMarkerIconPrimitive& append_primitive(
    MapMarkerIconRecipe& recipe,
    MapMarkerPrimitiveKind kind,
    MapMarkerColorRole color) {
    MapMarkerIconPrimitive& primitive =
        recipe.primitives[static_cast<std::size_t>(recipe.primitive_count++)];
    primitive.kind = kind;
    primitive.color = color;
    return primitive;
}

void set_points(MapMarkerIconPrimitive& primitive,
                std::initializer_list<ImVec2> points) {
    primitive.point_count = static_cast<std::uint8_t>(
        std::min(points.size(), primitive.points.size()));
    std::copy_n(points.begin(), primitive.point_count, primitive.points.begin());
}

void append_polyline(MapMarkerIconRecipe& recipe,
                     MapMarkerColorRole color,
                     std::initializer_list<ImVec2> points,
                     float thickness,
                     bool closed = false) {
    MapMarkerIconPrimitive& primitive =
        append_primitive(recipe, MapMarkerPrimitiveKind::Polyline, color);
    set_points(primitive, points);
    primitive.thickness = thickness;
    primitive.closed = closed;
}

void append_polygon(MapMarkerIconRecipe& recipe,
                    MapMarkerColorRole color,
                    std::initializer_list<ImVec2> points) {
    MapMarkerIconPrimitive& primitive =
        append_primitive(recipe, MapMarkerPrimitiveKind::FilledPolygon, color);
    set_points(primitive, points);
}

void append_circle(MapMarkerIconRecipe& recipe,
                   MapMarkerColorRole color,
                   ImVec2 center,
                   float radius,
                   float thickness,
                   bool filled) {
    MapMarkerIconPrimitive& primitive = append_primitive(
        recipe,
        filled ? MapMarkerPrimitiveKind::FilledCircle : MapMarkerPrimitiveKind::Circle,
        color);
    primitive.points[0] = center;
    primitive.point_count = 1;
    primitive.radius = radius;
    primitive.thickness = thickness;
}

void append_glyph(MapMarkerIconRecipe& recipe,
                  MapMarkerColorRole color,
                  char glyph) {
    MapMarkerIconPrimitive& primitive =
        append_primitive(recipe, MapMarkerPrimitiveKind::Glyph, color);
    primitive.glyph = glyph;
}

void append_sampled_arc(MapMarkerIconRecipe& recipe,
                        MapMarkerColorRole color,
                        ImVec2 center,
                        float radius,
                        float angle0,
                        float angle1,
                        float thickness,
                        int segments = 12) {
    MapMarkerIconPrimitive& primitive =
        append_primitive(recipe, MapMarkerPrimitiveKind::Polyline, color);
    segments = std::clamp(segments, 2, static_cast<int>(primitive.points.size()) - 1);
    primitive.point_count = static_cast<std::uint8_t>(segments + 1);
    primitive.thickness = thickness;
    for (int i = 0; i <= segments; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(segments);
        const float angle = angle0 + (angle1 - angle0) * t;
        primitive.points[static_cast<std::size_t>(i)] =
            ImVec2(center.x + std::cos(angle) * radius,
                   center.y + std::sin(angle) * radius);
    }
}

MapMarkerIconRecipe station_recipe(bool rail_diagram) {
    MapMarkerIconRecipe recipe;
    if (rail_diagram) {
        append_polyline(recipe, MapMarkerColorRole::Outline,
                        {ImVec2(-1.55f, 0.0f), ImVec2(1.55f, 0.0f)}, 0.20f);
    }
    append_circle(recipe, MapMarkerColorRole::Outline, ImVec2(0.0f, 0.0f), 0.58f, 0.24f, false);
    append_circle(recipe, MapMarkerColorRole::White, ImVec2(0.0f, 0.0f), 0.46f, 0.0f, true);
    return recipe;
}

MapMarkerIconRecipe curve_transition_recipe() {
    MapMarkerIconRecipe recipe;
    append_polyline(recipe, MapMarkerColorRole::Shadow,
                    {ImVec2(-0.9f, 0.55f), ImVec2(-0.45f, 0.52f),
                     ImVec2(-0.05f, 0.35f), ImVec2(0.32f, 0.02f),
                     ImVec2(0.62f, -0.38f), ImVec2(0.82f, -0.68f)},
                    0.28f);
    append_polyline(recipe, MapMarkerColorRole::White,
                    {ImVec2(-0.9f, 0.55f), ImVec2(-0.45f, 0.52f),
                     ImVec2(-0.05f, 0.35f), ImVec2(0.32f, 0.02f),
                     ImVec2(0.62f, -0.38f), ImVec2(0.82f, -0.68f)},
                    0.13f);
    return recipe;
}

MapMarkerIconRecipe curve_circular_recipe() {
    MapMarkerIconRecipe recipe;
    // Keep the lower tangent vertical while the upper end bends to the right.
    const ImVec2 arc_center(0.72f, 0.75f);
    append_sampled_arc(recipe, MapMarkerColorRole::Shadow, arc_center,
                       1.3f, -k_pi + 0.15f, -k_pi + 1.45f, 0.28f, 14);
    append_sampled_arc(recipe, MapMarkerColorRole::White, arc_center,
                       1.3f, -k_pi + 0.15f, -k_pi + 1.45f, 0.13f, 14);
    return recipe;
}

MapMarkerIconRecipe curve_end_recipe() {
    MapMarkerIconRecipe recipe;
    append_polyline(recipe, MapMarkerColorRole::Shadow,
                    {ImVec2(-0.82f, 0.70f), ImVec2(-0.62f, 0.35f),
                     ImVec2(-0.32f, 0.08f), ImVec2(0.05f, -0.08f),
                     ImVec2(0.42f, -0.12f), ImVec2(0.88f, -0.12f)},
                    0.28f);
    append_polyline(recipe, MapMarkerColorRole::White,
                    {ImVec2(-0.82f, 0.70f), ImVec2(-0.62f, 0.35f),
                     ImVec2(-0.32f, 0.08f), ImVec2(0.05f, -0.08f),
                     ImVec2(0.42f, -0.12f), ImVec2(0.88f, -0.12f)},
                    0.13f);
    return recipe;
}

MapMarkerIconRecipe gradient_transition_recipe() {
    MapMarkerIconRecipe recipe;
    append_polyline(recipe, MapMarkerColorRole::Shadow,
                    {ImVec2(-0.9f, 0.55f), ImVec2(-0.45f, 0.52f),
                     ImVec2(-0.05f, 0.38f), ImVec2(0.30f, 0.08f),
                     ImVec2(0.62f, -0.34f), ImVec2(0.88f, -0.62f)},
                    0.28f);
    append_polyline(recipe, MapMarkerColorRole::White,
                    {ImVec2(-0.9f, 0.55f), ImVec2(-0.45f, 0.52f),
                     ImVec2(-0.05f, 0.38f), ImVec2(0.30f, 0.08f),
                     ImVec2(0.62f, -0.34f), ImVec2(0.88f, -0.62f)},
                    0.13f);
    return recipe;
}

MapMarkerIconRecipe gradient_start_recipe() {
    MapMarkerIconRecipe recipe;
    append_polyline(recipe, MapMarkerColorRole::Shadow,
                    {ImVec2(-0.9f, 0.55f), ImVec2(-0.25f, 0.55f), ImVec2(0.9f, -0.55f)},
                    0.28f);
    append_polyline(recipe, MapMarkerColorRole::White,
                    {ImVec2(-0.9f, 0.55f), ImVec2(-0.25f, 0.55f), ImVec2(0.9f, -0.55f)},
                    0.13f);
    return recipe;
}

MapMarkerIconRecipe gradient_end_recipe() {
    MapMarkerIconRecipe recipe;
    append_polyline(recipe, MapMarkerColorRole::Shadow,
                    {ImVec2(-0.9f, 0.55f), ImVec2(0.25f, -0.55f), ImVec2(0.9f, -0.55f)},
                    0.28f);
    append_polyline(recipe, MapMarkerColorRole::White,
                    {ImVec2(-0.9f, 0.55f), ImVec2(0.25f, -0.55f), ImVec2(0.9f, -0.55f)},
                    0.13f);
    return recipe;
}

MapMarkerIconRecipe speed_limit_begin_recipe() {
    MapMarkerIconRecipe recipe;
    const std::initializer_list<ImVec2> points = {
        ImVec2(-0.78f, -0.78f), ImVec2(0.78f, -0.78f),
        ImVec2(0.78f, 0.78f), ImVec2(-0.78f, 0.78f)};
    append_polygon(recipe, MapMarkerColorRole::White, points);
    append_polyline(recipe, MapMarkerColorRole::Black, points, 0.12f, true);
    return recipe;
}

MapMarkerIconRecipe speed_limit_end_recipe() {
    MapMarkerIconRecipe recipe;
    const std::initializer_list<ImVec2> outline = {
        ImVec2(-0.92f, -0.72f), ImVec2(0.92f, -0.72f),
        ImVec2(0.92f, 0.72f), ImVec2(-0.92f, 0.72f)};
    append_polygon(recipe, MapMarkerColorRole::Black, outline);
    append_polygon(recipe, MapMarkerColorRole::White,
                   {ImVec2(-0.84f, -0.64f), ImVec2(0.0f, 0.0f),
                    ImVec2(-0.84f, 0.64f)});
    append_polygon(recipe, MapMarkerColorRole::White,
                   {ImVec2(0.84f, -0.64f), ImVec2(0.84f, 0.64f),
                    ImVec2(0.0f, 0.0f)});
    append_polyline(recipe, MapMarkerColorRole::Black, outline, 0.10f, true);
    return recipe;
}

MapMarkerIconRecipe beacon_recipe() {
    MapMarkerIconRecipe recipe;
    const std::initializer_list<ImVec2> points = {
        ImVec2(-0.55f, -0.72f), ImVec2(0.55f, -0.72f),
        ImVec2(0.88f, 0.72f), ImVec2(-0.88f, 0.72f)};
    append_polyline(recipe, MapMarkerColorRole::Shadow, points, 0.34f, true);
    append_polyline(recipe, MapMarkerColorRole::Theme, points, 0.16f, true);
    return recipe;
}

MapMarkerIconRecipe pretrain_recipe() {
    MapMarkerIconRecipe recipe;
    const std::initializer_list<ImVec2> points = {
        ImVec2(-0.82f, -0.82f), ImVec2(0.82f, -0.82f),
        ImVec2(0.82f, 0.82f), ImVec2(-0.82f, 0.82f)};
    append_polyline(recipe, MapMarkerColorRole::Shadow, points, 0.34f, true);
    append_polyline(recipe, MapMarkerColorRole::White, points, 0.16f, true);
    append_glyph(recipe, MapMarkerColorRole::White, 'P');
    return recipe;
}

MapMarkerIconRecipe wave_recipe() {
    MapMarkerIconRecipe recipe;
    MapMarkerIconPrimitive& shadow =
        append_primitive(recipe, MapMarkerPrimitiveKind::Polyline, MapMarkerColorRole::Outline);
    MapMarkerIconPrimitive& line =
        append_primitive(recipe, MapMarkerPrimitiveKind::Polyline, MapMarkerColorRole::Theme);
    shadow.point_count = line.point_count = 17;
    shadow.thickness = 0.30f;
    line.thickness = 0.15f;
    for (std::size_t i = 0; i < 17; ++i) {
        const float t = static_cast<float>(i) / 16.0f;
        const ImVec2 point(-0.92f + 1.84f * t, std::sin(t * k_pi * 4.0f) * 0.38f);
        shadow.points[i] = line.points[i] = point;
    }
    return recipe;
}

MapMarkerIconRecipe speaker_recipe(bool broadcast) {
    MapMarkerIconRecipe recipe;
    if (!broadcast) {
        append_polygon(recipe, MapMarkerColorRole::Theme,
                       {ImVec2(-0.85f, -0.35f), ImVec2(-0.42f, -0.35f),
                        ImVec2(0.30f, -0.75f), ImVec2(0.30f, 0.75f),
                        ImVec2(-0.42f, 0.35f), ImVec2(-0.85f, 0.35f)});
        append_sampled_arc(recipe, MapMarkerColorRole::Outline, ImVec2(0.15f, 0.0f),
                           0.58f, -0.75f, 0.75f, 0.24f, 10);
        append_sampled_arc(recipe, MapMarkerColorRole::Theme, ImVec2(0.15f, 0.0f),
                           0.58f, -0.75f, 0.75f, 0.12f, 10);
        append_sampled_arc(recipe, MapMarkerColorRole::Outline, ImVec2(0.15f, 0.0f),
                           0.90f, -0.75f, 0.75f, 0.24f, 12);
        append_sampled_arc(recipe, MapMarkerColorRole::Theme, ImVec2(0.15f, 0.0f),
                           0.90f, -0.75f, 0.75f, 0.12f, 12);
    } else {
        append_circle(recipe, MapMarkerColorRole::Outline, ImVec2(0.0f, 0.55f), 0.22f, 0.0f, true);
        append_circle(recipe, MapMarkerColorRole::Theme, ImVec2(0.0f, 0.55f), 0.15f, 0.0f, true);
        append_polyline(recipe, MapMarkerColorRole::Outline,
                        {ImVec2(0.0f, 0.55f), ImVec2(0.0f, -0.55f)}, 0.28f);
        append_polyline(recipe, MapMarkerColorRole::Theme,
                        {ImVec2(0.0f, 0.55f), ImVec2(0.0f, -0.55f)}, 0.13f);
        append_sampled_arc(recipe, MapMarkerColorRole::Theme, ImVec2(0.0f, 0.55f),
                           0.58f, -0.7f, 0.7f, 0.13f, 10);
        append_sampled_arc(recipe, MapMarkerColorRole::Theme, ImVec2(0.0f, 0.55f),
                           0.58f, 2.44f, 3.84f, 0.13f, 10);
    }
    return recipe;
}

MapMarkerIconRecipe axle_recipe() {
    MapMarkerIconRecipe recipe;
    append_polyline(recipe, MapMarkerColorRole::Outline,
                    {ImVec2(-0.85f, 0.0f), ImVec2(0.85f, 0.0f)}, 0.34f);
    append_polyline(recipe, MapMarkerColorRole::Outline,
                    {ImVec2(-0.85f, -0.55f), ImVec2(-0.85f, 0.55f)}, 0.34f);
    append_polyline(recipe, MapMarkerColorRole::Outline,
                    {ImVec2(0.85f, -0.55f), ImVec2(0.85f, 0.55f)}, 0.34f);
    append_polyline(recipe, MapMarkerColorRole::Theme,
                    {ImVec2(-0.85f, 0.0f), ImVec2(0.85f, 0.0f)}, 0.17f);
    append_polyline(recipe, MapMarkerColorRole::Theme,
                    {ImVec2(-0.85f, -0.55f), ImVec2(-0.85f, 0.55f)}, 0.17f);
    append_polyline(recipe, MapMarkerColorRole::Theme,
                    {ImVec2(0.85f, -0.55f), ImVec2(0.85f, 0.55f)}, 0.17f);
    return recipe;
}

MapMarkerIconRecipe glyph_recipe(char glyph, MapMarkerColorRole outline) {
    MapMarkerIconRecipe recipe;
    append_glyph(recipe, outline, glyph);
    append_glyph(recipe, MapMarkerColorRole::Theme, glyph);
    return recipe;
}

MapMarkerIconRecipe joint_recipe() {
    MapMarkerIconRecipe recipe;
    const std::initializer_list<ImVec2> points = {
        ImVec2(0.72f, -0.68f), ImVec2(-0.72f, 0.60f),
        ImVec2(0.82f, 0.60f)};
    append_polyline(recipe, MapMarkerColorRole::Outline,
                    points, 0.34f);
    append_polyline(recipe, MapMarkerColorRole::Theme,
                    points, 0.17f);
    return recipe;
}

MapMarkerIconRecipe background_recipe() {
    MapMarkerIconRecipe recipe;
    append_polygon(recipe, MapMarkerColorRole::Theme,
                   {ImVec2(-0.68f, -0.68f), ImVec2(0.68f, -0.68f),
                    ImVec2(0.68f, 0.68f), ImVec2(-0.68f, 0.68f)});
    append_polyline(recipe, MapMarkerColorRole::Outline,
                    {ImVec2(-0.68f, -0.68f), ImVec2(0.68f, -0.68f),
                     ImVec2(0.68f, 0.68f), ImVec2(-0.68f, 0.68f)},
                    0.12f, true);
    return recipe;
}

MapMarkerIconRecipe adhesion_recipe(bool outlined) {
    MapMarkerIconRecipe recipe;
    if (outlined) {
        append_circle(recipe, MapMarkerColorRole::Black,
                      ImVec2(-0.16f, 0.12f), 0.36f, 0.0f, true);
        append_sampled_arc(recipe, MapMarkerColorRole::Black,
                           ImVec2(-0.16f, 0.12f),
                           0.72f, -2.45f, 0.82f, 0.30f, 16);
        append_polygon(recipe, MapMarkerColorRole::Black,
                       {ImVec2(0.29f, 0.70f), ImVec2(0.79f, 0.52f),
                        ImVec2(0.50f, 0.13f)});
    }
    append_circle(recipe, MapMarkerColorRole::Theme, ImVec2(-0.16f, 0.12f), 0.28f, 0.0f, true);
    append_sampled_arc(recipe, MapMarkerColorRole::Theme, ImVec2(-0.16f, 0.12f),
                       0.72f, -2.45f, 0.82f, 0.16f, 16);
    append_polygon(recipe, MapMarkerColorRole::Theme,
                   {ImVec2(0.34f, 0.64f), ImVec2(0.72f, 0.50f), ImVec2(0.51f, 0.20f)});
    return recipe;
}

MapMarkerIconRecipe sun_recipe() {
    MapMarkerIconRecipe recipe;
    append_circle(recipe, MapMarkerColorRole::Outline, ImVec2(0.0f, 0.0f), 0.33f, 0.0f, true);
    append_circle(recipe, MapMarkerColorRole::Theme, ImVec2(0.0f, 0.0f), 0.24f, 0.0f, true);
    for (int i = 0; i < 8; ++i) {
        const float angle = static_cast<float>(i) * k_pi / 4.0f;
        append_polyline(recipe, MapMarkerColorRole::Outline,
                        {ImVec2(std::cos(angle) * 0.48f, std::sin(angle) * 0.48f),
                         ImVec2(std::cos(angle) * 0.90f, std::sin(angle) * 0.90f)},
                        0.25f);
        append_polyline(recipe, MapMarkerColorRole::Theme,
                        {ImVec2(std::cos(angle) * 0.48f, std::sin(angle) * 0.48f),
                         ImVec2(std::cos(angle) * 0.90f, std::sin(angle) * 0.90f)},
                        0.12f);
    }
    return recipe;
}

MapMarkerIconRecipe fog_recipe() {
    MapMarkerIconRecipe recipe;
    for (float y : {-0.42f, 0.0f, 0.42f}) {
        append_polyline(recipe, MapMarkerColorRole::Shadow,
                        {ImVec2(-0.78f, y), ImVec2(0.78f, y)}, 0.30f);
        append_polyline(recipe, MapMarkerColorRole::White,
                        {ImVec2(-0.78f, y), ImVec2(0.78f, y)}, 0.14f);
    }
    return recipe;
}

MapMarkerIconRecipe other_track_change_recipe() {
    MapMarkerIconRecipe recipe;
    append_circle(recipe, MapMarkerColorRole::Black,
                  ImVec2(0.0f, 0.0f), 0.64f, 0.0f, true);
    append_circle(recipe, MapMarkerColorRole::Theme,
                  ImVec2(0.0f, 0.0f), 0.48f, 0.0f, true);
    return recipe;
}

ImVec2 transform_icon_point(ImVec2 point,
                            ImVec2 center,
                            float half_extent,
                            float rotation) {
    const float c = std::cos(rotation);
    const float s = std::sin(rotation);
    return ImVec2(center.x + (point.x * c - point.y * s) * half_extent,
                  center.y + (point.x * s + point.y * c) * half_extent);
}

} // namespace

ImVec4 map_marker_theme_color(MapMarkerVisualKind kind) {
    switch (kind) {
        case MapMarkerVisualKind::Station:
        case MapMarkerVisualKind::CurveTransitionStart:
        case MapMarkerVisualKind::CurveCircularStart:
        case MapMarkerVisualKind::CurveEnd:
        case MapMarkerVisualKind::CurveGauge:
        case MapMarkerVisualKind::CurveCenter:
        case MapMarkerVisualKind::CurveFunction:
        case MapMarkerVisualKind::GradientTransitionStart:
        case MapMarkerVisualKind::GradientStart:
        case MapMarkerVisualKind::GradientEnd:
        case MapMarkerVisualKind::PreTrain:
        case MapMarkerVisualKind::Fog:
            return rgba(255, 255, 255);
        case MapMarkerVisualKind::SpeedLimit:
            return rgba(136, 204, 255);
        case MapMarkerVisualKind::Section:
            return rgba(158, 255, 184);
        case MapMarkerVisualKind::Beacon:
            return rgba(148, 242, 178);
        case MapMarkerVisualKind::Irregularity:
            return rgba(204, 170, 255);
        case MapMarkerVisualKind::MapSound:
        case MapMarkerVisualKind::MapSound3D:
            return rgba(222, 190, 255);
        case MapMarkerVisualKind::RollingNoise:
            return rgba(126, 214, 255);
        case MapMarkerVisualKind::FlangeNoise:
            return rgba(214, 176, 255);
        case MapMarkerVisualKind::JointNoise:
            return rgba(158, 224, 255);
        case MapMarkerVisualKind::Background:
            return rgba(255, 230, 72);
        case MapMarkerVisualKind::Adhesion:
            return rgba(178, 102, 255);
        case MapMarkerVisualKind::CabIlluminance:
            return rgba(255, 226, 64);
        case MapMarkerVisualKind::DrawDistance:
            return rgba(255, 224, 48);
        case MapMarkerVisualKind::OtherTrackChange:
            return rgba(90, 180, 255);
        case MapMarkerVisualKind::Count:
            break;
    }
    return rgba(255, 255, 255);
}

ImU32 map_marker_theme_color_u32(MapMarkerVisualKind kind, float alpha) {
    ImVec4 color = map_marker_theme_color(kind);
    color.w = std::clamp(alpha, 0.0f, 1.0f);
    return ImGui::ColorConvertFloat4ToU32(color);
}

ImVec4 map_marker_role_color(MapMarkerVisualKind kind, MapMarkerColorRole role,
                             const ImVec4* theme_override) {
    switch (role) {
        case MapMarkerColorRole::Theme:
            return theme_override ? *theme_override : map_marker_theme_color(kind);
        case MapMarkerColorRole::Outline:
            return rgba(48, 34, 66);
        case MapMarkerColorRole::White:
            return rgba(255, 255, 255);
        case MapMarkerColorRole::Shadow:
            return rgba(0, 0, 0, 220);
        case MapMarkerColorRole::Black:
            return rgba(0, 0, 0);
    }
    return map_marker_theme_color(kind);
}

MapMarkerIconRecipe map_marker_icon_recipe(MapMarkerVisualKind kind,
                                           MapMarkerIconVariant variant) {
    switch (kind) {
        case MapMarkerVisualKind::Station:
            return station_recipe(
                variant == MapMarkerIconVariant::StationRailDiagram);
        case MapMarkerVisualKind::CurveTransitionStart:
            return curve_transition_recipe();
        case MapMarkerVisualKind::CurveCircularStart:
            return curve_circular_recipe();
        case MapMarkerVisualKind::CurveEnd:
            return curve_end_recipe();
        case MapMarkerVisualKind::CurveGauge:
        case MapMarkerVisualKind::CurveCenter:
        case MapMarkerVisualKind::CurveFunction:
            return {};
        case MapMarkerVisualKind::GradientTransitionStart:
            return gradient_transition_recipe();
        case MapMarkerVisualKind::GradientStart:
            return gradient_start_recipe();
        case MapMarkerVisualKind::GradientEnd:
            return gradient_end_recipe();
        case MapMarkerVisualKind::SpeedLimit: {
            if (variant == MapMarkerIconVariant::SpeedLimitBegin) {
                return speed_limit_begin_recipe();
            }
            if (variant == MapMarkerIconVariant::SpeedLimitEnd) {
                return speed_limit_end_recipe();
            }
            MapMarkerIconRecipe recipe;
            append_polyline(recipe, MapMarkerColorRole::Shadow,
                            {ImVec2(-0.88f, 0.0f), ImVec2(0.88f, 0.0f)}, 0.28f);
            append_polyline(recipe, MapMarkerColorRole::Theme,
                            {ImVec2(-0.88f, 0.0f), ImVec2(0.88f, 0.0f)}, 0.13f);
            return recipe;
        }
        case MapMarkerVisualKind::Section:
            return glyph_recipe('S', MapMarkerColorRole::Black);
        case MapMarkerVisualKind::Beacon:
            return beacon_recipe();
        case MapMarkerVisualKind::PreTrain:
            return pretrain_recipe();
        case MapMarkerVisualKind::Irregularity:
            return wave_recipe();
        case MapMarkerVisualKind::MapSound:
            return speaker_recipe(false);
        case MapMarkerVisualKind::MapSound3D:
            return speaker_recipe(true);
        case MapMarkerVisualKind::RollingNoise:
            return axle_recipe();
        case MapMarkerVisualKind::FlangeNoise:
            return glyph_recipe('F', MapMarkerColorRole::Outline);
        case MapMarkerVisualKind::JointNoise:
            return joint_recipe();
        case MapMarkerVisualKind::Background:
            return background_recipe();
        case MapMarkerVisualKind::Adhesion:
            return adhesion_recipe(
                variant == MapMarkerIconVariant::AdhesionOutlined);
        case MapMarkerVisualKind::CabIlluminance:
            return sun_recipe();
        case MapMarkerVisualKind::Fog:
            return fog_recipe();
        case MapMarkerVisualKind::DrawDistance:
            return glyph_recipe('D', MapMarkerColorRole::Outline);
        case MapMarkerVisualKind::OtherTrackChange:
            return other_track_change_recipe();
        case MapMarkerVisualKind::Count:
            break;
    }
    return {};
}

static const MapMarkerIconRecipe& default_map_marker_icon_recipe(MapMarkerVisualKind kind) {
    constexpr std::size_t k_kind_count = static_cast<std::size_t>(MapMarkerVisualKind::Count);
    static const std::array<MapMarkerIconRecipe, k_kind_count> recipes = [] {
        std::array<MapMarkerIconRecipe, k_kind_count> values{};
        for (std::size_t i = 0; i < values.size(); ++i)
            values[i] = map_marker_icon_recipe(static_cast<MapMarkerVisualKind>(i));
        return values;
    }();
    return recipes[static_cast<std::size_t>(kind)];
}

void draw_map_marker_icon(ImDrawList* draw,
                          MapMarkerVisualKind kind,
                          ImVec2 center,
                          float half_extent,
                          float rotation_radians,
                          const ImVec4* theme_override) {
    if (!draw || half_extent <= 0.0f) return;
    if (static_cast<std::size_t>(kind) >= static_cast<std::size_t>(MapMarkerVisualKind::Count)) return;
    const MapMarkerIconRecipe& recipe = default_map_marker_icon_recipe(kind);
    for (std::size_t primitive_index = 0;
         primitive_index < recipe.primitive_count;
         ++primitive_index) {
        const MapMarkerIconPrimitive& primitive = recipe.primitives[primitive_index];
        ImU32 color = ImGui::ColorConvertFloat4ToU32(
            map_marker_role_color(kind, primitive.color, theme_override));
        switch (primitive.kind) {
            case MapMarkerPrimitiveKind::Polyline: {
                std::array<ImVec2, 24> points{};
                for (std::size_t i = 0; i < primitive.point_count; ++i) {
                    points[i] = transform_icon_point(
                        primitive.points[i], center, half_extent, rotation_radians);
                }
                draw->AddPolyline(points.data(), primitive.point_count, color,
                                  primitive.closed ? ImDrawFlags_Closed : ImDrawFlags_None,
                                  std::max(1.0f, primitive.thickness * half_extent));
                break;
            }
            case MapMarkerPrimitiveKind::FilledPolygon: {
                std::array<ImVec2, 24> points{};
                for (std::size_t i = 0; i < primitive.point_count; ++i) {
                    points[i] = transform_icon_point(
                        primitive.points[i], center, half_extent, rotation_radians);
                }
                draw->AddConvexPolyFilled(points.data(), primitive.point_count, color);
                break;
            }
            case MapMarkerPrimitiveKind::Circle:
            case MapMarkerPrimitiveKind::FilledCircle: {
                const ImVec2 point = transform_icon_point(
                    primitive.points[0], center, half_extent, rotation_radians);
                const float radius = primitive.radius * half_extent;
                if (primitive.kind == MapMarkerPrimitiveKind::FilledCircle) {
                    draw->AddCircleFilled(point, radius, color, 20);
                } else {
                    draw->AddCircle(point, radius, color, 20,
                                    std::max(1.0f, primitive.thickness * half_extent));
                }
                break;
            }
            case MapMarkerPrimitiveKind::Glyph: {
                char text[2] = {primitive.glyph, '\0'};
                const float font_size = half_extent * 1.55f;
                ImFont* font = ImGui::GetFont();
                const ImVec2 size = font->CalcTextSizeA(
                    font_size, std::numeric_limits<float>::max(), 0.0f, text);
                const ImVec2 position(
                    center.x - size.x * 0.5f,
                    center.y - size.y * 0.5f);
                const bool outline =
                    primitive.color == MapMarkerColorRole::Outline ||
                    primitive.color == MapMarkerColorRole::Shadow ||
                    primitive.color == MapMarkerColorRole::Black;
                if (outline) {
                    const float offset = std::max(1.0f, half_extent * 0.11f);
                    for (const ImVec2 delta : {
                             ImVec2(-offset, 0.0f), ImVec2(offset, 0.0f),
                             ImVec2(0.0f, -offset), ImVec2(0.0f, offset)}) {
                        draw->AddText(
                            font, font_size,
                            ImVec2(position.x + delta.x, position.y + delta.y),
                            color, text);
                    }
                } else {
                    draw->AddText(font, font_size, position, color, text);
                }
                break;
            }
        }
    }
}
