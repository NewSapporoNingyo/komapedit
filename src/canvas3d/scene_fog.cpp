/*
 * Copyright (c) 2026 Sapporo_ningyo
 *
 * Licensed under Apache License 2.0; see LICENSE and NOTICE.
 */

#include "scene_fog.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace canvas3d_detail {
namespace {

double component(const std::optional<double>& value, double fallback, double scale = 1.0) {
    if (!value || std::isnan(*value)) return fallback;
    return std::clamp(*value / scale, 0.0, 1.0);
}

float shader_distance(double value) {
    // Leave room for end-start and end-eyeDepth in the float shader arithmetic.
    constexpr double limit = static_cast<double>(std::numeric_limits<float>::max()) / 4.0;
    return static_cast<float>(std::clamp(value, -limit, limit));
}

} // namespace

std::vector<Canvas3DSceneFogKeyframe> build_canvas3d_scene_fog_keyframes(
    std::vector<SceneFogEvent> events) {
    events.erase(std::remove_if(events.begin(), events.end(), [](const SceneFogEvent& event) {
        return !std::isfinite(event.distance) || !std::isfinite(event.order) ||
            (event.mode == Canvas3DSceneFogMode::Linear &&
             (!std::isfinite(event.start) || !std::isfinite(event.end)));
    }), events.end());
    std::stable_sort(events.begin(), events.end(), [](const SceneFogEvent& a, const SceneFogEvent& b) {
        return a.distance != b.distance ? a.distance < b.distance : a.order < b.order;
    });

    std::vector<Canvas3DSceneFogKeyframe> keyframes;
    keyframes.reserve(events.size());
    Canvas3DSceneFogKeyframe previous_statement;
    previous_statement.mode = Canvas3DSceneFogMode::Linear;
    previous_statement.start = 10000.0;
    previous_statement.end = 10025.0;
    // Fog omissions inherit the furthest inserted node, which can be a future
    // Legacy target at D+25, rather than the most recently processed statement.
    Canvas3DSceneFogKeyframe furthest;
    bool has_furthest = false;
    const auto append = [&](const Canvas3DSceneFogKeyframe& keyframe) {
        keyframes.push_back(keyframe);
        if (!has_furthest || keyframe.distance >= furthest.distance) {
            furthest = keyframe;
            has_furthest = true;
        }
    };
    for (const SceneFogEvent& event : events) {
        Canvas3DSceneFogKeyframe keyframe;
        keyframe.distance = event.distance;
        keyframe.mode = event.mode;
        if (event.mode == Canvas3DSceneFogMode::Linear) {
            if (event.distance != 0.0) {
                Canvas3DSceneFogKeyframe snapshot = previous_statement;
                snapshot.distance = event.distance;
                append(snapshot);
                keyframe.distance += 25.0;
            }
            keyframe.start = event.start;
            keyframe.end = event.end;
            for (size_t i = 0; i < keyframe.color.size(); ++i) {
                keyframe.color[i] = component(event.color[i], keyframe.color[i], 255.0);
            }
        } else {
            // Preserve komapedit's safe defaults for a first omitted Fog value.
            keyframe.density = component(event.density, furthest.density);
            for (size_t i = 0; i < keyframe.color.size(); ++i) {
                keyframe.color[i] = component(event.color[i], furthest.color[i]);
            }
        }
        append(keyframe);
        previous_statement = keyframe;
    }
    // Sort once after expansion: repeated sorted insertion would be quadratic.
    // Retain equal-distance nodes: the first is the incoming interpolation
    // target, while upper_bound selects the last at/after that exact distance.
    std::stable_sort(keyframes.begin(), keyframes.end(),
        [](const Canvas3DSceneFogKeyframe& a, const Canvas3DSceneFogKeyframe& b) {
            return a.distance < b.distance;
        });
    return keyframes;
}

SceneFogSample sample_canvas3d_scene_fog(
    const std::vector<Canvas3DSceneFogKeyframe>& keyframes,
    double distance, bool enabled) {
    SceneFogSample sample;
    if (!enabled || keyframes.empty() || !std::isfinite(distance)) return sample;

    const auto next = std::upper_bound(keyframes.begin(), keyframes.end(), distance,
        [](double value, const Canvas3DSceneFogKeyframe& keyframe) {
            return value < keyframe.distance;
        });
    Canvas3DSceneFogKeyframe value = next == keyframes.begin() ? *next : *(next - 1);
    if (next != keyframes.begin() && next != keyframes.end() && value.mode == next->mode) {
        const double ratio = std::clamp(
            (distance - value.distance) / (next->distance - value.distance), 0.0, 1.0);
        const auto lerp = [ratio](double a, double b) { return a * (1.0 - ratio) + b * ratio; };
        value.density = lerp(value.density, next->density);
        value.start = lerp(value.start, next->start);
        value.end = lerp(value.end, next->end);
        for (size_t i = 0; i < value.color.size(); ++i) {
            value.color[i] = lerp(value.color[i], next->color[i]);
        }
    }
    sample.mode = value.mode;
    sample.density = static_cast<float>(value.density);
    sample.color = ImVec4(static_cast<float>(value.color[0]), static_cast<float>(value.color[1]),
                         static_cast<float>(value.color[2]), 1.0f);
    sample.start = shader_distance(value.start);
    sample.end = shader_distance(value.end);
    sample.enabled = sample.mode == Canvas3DSceneFogMode::Linear || sample.density > 0.0f;
    return sample;
}

} // namespace canvas3d_detail
