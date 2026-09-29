/*
 * Copyright (c) 2026 Sapporo_ningyo
 *
 * Licensed under Apache License 2.0; see LICENSE and NOTICE.
 */

#pragma once

#include "canvas3D.h"

namespace canvas3d_detail {

// Evaluated MapModel values, converted once when the scene is built/refreshed.
// Legacy colors retain their source 0..255 scale until keyframe construction.
struct SceneFogEvent {
    double distance = 0.0;
    double order = 0.0;
    Canvas3DSceneFogMode mode = Canvas3DSceneFogMode::Exponential;
    std::optional<double> density;
    std::array<std::optional<double>, 3> color;
    double start = 0.0;
    double end = 2400.0;
};

struct SceneFogSample {
    bool enabled = false;
    Canvas3DSceneFogMode mode = Canvas3DSceneFogMode::Exponential;
    float density = 0.0f;
    ImVec4 color = ImVec4(0.0f, 0.0f, 0.0f, 1.0f);
    float start = 0.0f;
    float end = 2400.0f;
};

std::vector<Canvas3DSceneFogKeyframe> build_canvas3d_scene_fog_keyframes(
    std::vector<SceneFogEvent> events);
SceneFogSample sample_canvas3d_scene_fog(
    const std::vector<Canvas3DSceneFogKeyframe>& keyframes,
    double distance, bool enabled);

} // namespace canvas3d_detail
