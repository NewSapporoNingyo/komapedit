/*
 * Copyright (c) 2026 Sapporo_ningyo
 *
 * Licensed under Apache License 2.0; see LICENSE and NOTICE.
 */

#pragma once

#include "imgui.h"

#include <array>
#include <cstddef>
#include <cstdint>

enum class MapMarkerVisualKind : std::uint8_t {
    Station,
    CurveTransitionStart,
    CurveCircularStart,
    CurveEnd,
    GradientTransitionStart,
    GradientStart,
    GradientEnd,
    SpeedLimit,
    Section,
    Beacon,
    PreTrain,
    Irregularity,
    MapSound,
    MapSound3D,
    RollingNoise,
    FlangeNoise,
    JointNoise,
    Background,
    Adhesion,
    CabIlluminance,
    Fog,
    DrawDistance,
    Count
};

inline constexpr std::uint64_t map_marker_visual_bit(MapMarkerVisualKind kind) {
    return std::uint64_t{1} << static_cast<unsigned int>(kind);
}

enum class MapMarkerPrimitiveKind : std::uint8_t {
    Polyline,
    FilledPolygon,
    Circle,
    FilledCircle,
    Glyph
};

enum class MapMarkerColorRole : std::uint8_t {
    Theme,
    Outline,
    White,
    Shadow,
    Black
};

enum class MapMarkerIconVariant : std::uint8_t {
    Default,
    SpeedLimitBegin,
    SpeedLimitEnd,
    StationRailDiagram,
    AdhesionOutlined
};

struct MapMarkerIconPrimitive {
    MapMarkerPrimitiveKind kind = MapMarkerPrimitiveKind::Polyline;
    MapMarkerColorRole color = MapMarkerColorRole::Theme;
    std::array<ImVec2, 24> points{};
    std::uint8_t point_count = 0;
    bool closed = false;
    float thickness = 0.16f;
    float radius = 0.0f;
    char glyph = '\0';
};

struct MapMarkerIconRecipe {
    std::array<MapMarkerIconPrimitive, 20> primitives{};
    std::uint8_t primitive_count = 0;
};

ImVec4 map_marker_theme_color(MapMarkerVisualKind kind);
ImU32 map_marker_theme_color_u32(MapMarkerVisualKind kind, float alpha = 1.0f);
ImVec4 map_marker_role_color(MapMarkerVisualKind kind, MapMarkerColorRole role);
MapMarkerIconRecipe map_marker_icon_recipe(
    MapMarkerVisualKind kind,
    MapMarkerIconVariant variant = MapMarkerIconVariant::Default);

void draw_map_marker_icon(ImDrawList* draw,
                          MapMarkerVisualKind kind,
                          ImVec2 center,
                          float half_extent,
                          float rotation_radians = 0.0f);
