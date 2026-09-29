/*
 * Copyright (c) 2026 Sapporo_ningyo
 *
 * Licensed under Apache License 2.0; see LICENSE and NOTICE.
 */

// Part of route_value_sampling_contract: no window, rendering device, app
// launch, headless entry point, or UI automation is involved.
#include "scene_fog.h"
#include "scene_shader_source.h"

#include <d3dcompiler.h>
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>

namespace {

using namespace canvas3d_detail;
using Mode = Canvas3DSceneFogMode;
int failures = 0;

void check(bool condition, const char* message) {
    if (condition) return;
    ++failures;
    std::cerr << "FAIL: fog " << message << '\n';
}

void check_near(double actual, double expected, const char* message) {
    check(std::isfinite(actual) && std::abs(actual - expected) <= 1e-5, message);
}

SceneFogEvent legacy(double distance, double start, double end,
                     double red = 255, double green = 255, double blue = 255,
                     double order = 0) {
    SceneFogEvent event;
    event.distance = distance;
    event.order = order;
    event.mode = Mode::Linear;
    event.start = start;
    event.end = end;
    event.color = {red, green, blue};
    return event;
}

SceneFogEvent exponential(double distance, double density,
                          double red = 0.875, double green = 0.9375, double blue = 1,
                          double order = 0) {
    SceneFogEvent event;
    event.distance = distance;
    event.order = order;
    event.density = density;
    event.color = {red, green, blue};
    return event;
}

SceneFogSample at(const std::vector<Canvas3DSceneFogKeyframe>& keys, double distance) {
    return sample_canvas3d_scene_fog(keys, distance, true);
}

void legacy_contract() {
    auto keys = build_canvas3d_scene_fog_keyframes({legacy(0, 50, 600, 128, 64, 255)});
    check(keys.size() == 1, "zero mileage creates only the target node");
    for (double distance : {-100.0, 0.0, 10000.0}) {
        const auto sample = at(keys, distance);
        check(sample.enabled && sample.mode == Mode::Linear, "linear mode is enabled before/after endpoints");
        check_near(sample.start, 50, "zero-mileage start");
        check_near(sample.end, 600, "zero-mileage end");
        check_near(sample.color.x, 128.0 / 255.0, "legacy red is normalized from 255");
        check_near(sample.color.y, 64.0 / 255.0, "legacy green is normalized from 255");
    }
    keys = build_canvas3d_scene_fog_keyframes({legacy(100, 100, 400), legacy(500, 200, 800, 0, 0, 255)});
    check(keys.size() == 4, "nonzero mileage expands to snapshot and target");
    check_near(at(keys, -1).start, 10000, "initial snapshot starts at 10000m");
    check_near(at(keys, 100).end, 10025, "initial snapshot ends at 10025m");
    check_near(at(keys, 112.5).start, 5050, "first 25m transition midpoint");
    check_near(at(keys, 112.5).end, 5212.5, "first 25m end midpoint");
    check_near(at(keys, 125).start, 100, "first target reached");
    check_near(at(keys, 499).start, 100, "legacy value remains constant until next snapshot");
    check_near(at(keys, 512.5).start, 150, "second legacy transition start midpoint");
    check_near(at(keys, 512.5).end, 600, "second legacy transition end midpoint");
    check_near(at(keys, 512.5).color.x, 0.5, "legacy colors interpolate with mileage");
    check_near(at(keys, 525).end, 800, "second target reached");
}

void mixed_contract() {
    const auto keys = build_canvas3d_scene_fog_keyframes({
        exponential(0, 0.0005), legacy(500, 200, 800, 200, 210, 235),
        exponential(1200, 0.002, 0.5, 0.6, 0.7)});
    for (double distance : {250.0, 500.0, 524.999}) {
        const auto sample = at(keys, distance);
        check(sample.mode == Mode::Exponential, "EXP remains active until legacy target");
        check_near(sample.density, 0.0005, "snapshot freezes preceding EXP interpolation");
        check_near(sample.color.x, 0.875, "mode boundary does not blend colors");
    }
    for (double distance : {525.0, 900.0, 1199.999}) {
        const auto sample = at(keys, distance);
        check(sample.mode == Mode::Linear, "linear persists until next EXP node");
        check_near(sample.start, 200, "mixed linear start");
        check_near(sample.end, 800, "mixed linear end");
    }
    check(at(keys, 1200).mode == Mode::Exponential, "switch occurs exactly at EXP node");
    check_near(at(keys, 1200).density, 0.002, "EXP target density");
}

void ordering_contract() {
    auto keys = build_canvas3d_scene_fog_keyframes({
        exponential(0, 0.003, 0, 1, 0, 2), legacy(0, 100, 400, 255, 0, 0, 1)});
    check(at(keys, 0).mode == Mode::Exponential, "global order wins across separate fog families");
    keys = build_canvas3d_scene_fog_keyframes({
        exponential(0, 0.003, 0, 1, 0, 1), legacy(0, 100, 400, 255, 0, 0, 2)});
    check(at(keys, 0).mode == Mode::Linear, "reverse same-mileage order selects legacy");
    keys = build_canvas3d_scene_fog_keyframes({
        exponential(100, 0.004, 1, 1, 1, 2), exponential(0, 0, 0, 0, 0),
        exponential(100, 0.002, 1, 1, 1, 1)});
    check_near(at(keys, 50).density, 0.001, "first equal-distance node is incoming interpolation target");
    check_near(at(keys, 100).density, 0.004, "last equal-distance node is active at exact distance");

    keys = build_canvas3d_scene_fog_keyframes({legacy(110, 200, 800), legacy(100, 100, 400)});
    check_near(at(keys, 105).start, 5050, "nearby legacy snapshot shortens first transition");
    check_near(at(keys, 120).start, 100, "overlapping transition retains intervening constant state");
    check_near(at(keys, 130).start, 150, "nearby legacy final transition");
    keys = build_canvas3d_scene_fog_keyframes({legacy(100, 100, 400), exponential(110, 0.002)});
    check_near(at(keys, 105).start, 10000, "EXP inside legacy window blocks interpolation");
    check(at(keys, 110).mode == Mode::Exponential, "intervening EXP activates");
    check(at(keys, 125).mode == Mode::Linear, "delayed legacy target still activates");
}

void inheritance_and_safety_contract() {
    SceneFogEvent omitted;
    omitted.distance = 115;
    auto keys = build_canvas3d_scene_fog_keyframes({legacy(100, 100, 400, 255, 0, 0),
        exponential(110, 0.02, 0, 1, 0), omitted});
    const auto inherited = at(keys, 115);
    check_near(inherited.density, 0.001, "omitted density uses furthest legacy node");
    check_near(inherited.color.x, 1, "omitted color uses furthest legacy node, not last statement");
    check_near(inherited.color.y, 0, "omitted color does not inherit nearer EXP");
    omitted.distance = 0;
    keys = build_canvas3d_scene_fog_keyframes({omitted});
    check_near(at(keys, 0).density, 0.001, "first omission keeps existing safe density default");
    check_near(at(keys, 0).color.x, 0.875, "first omission keeps existing safe color default");
    check(!at({}, 0).enabled, "empty route retains existing no-fog behavior");
    check(!sample_canvas3d_scene_fog(keys, 0, false).enabled, "existing fog toggle disables sampling");
    keys = build_canvas3d_scene_fog_keyframes({exponential(0, 0)});
    check(!at(keys, 0).enabled, "zero-density EXP remains disabled");
    keys = build_canvas3d_scene_fog_keyframes({legacy(0, 100, 100, -1, 300, 128)});
    check(at(keys, 0).enabled, "equal endpoints reach guarded linear shader path");
    check_near(at(keys, 0).color.x, 0, "negative color clamps safely");
    check_near(at(keys, 0).color.y, 1, "oversized color clamps safely");
    check(!sample_canvas3d_scene_fog(keys, 0, false).enabled, "toggle disables linear fog too");

    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double inf = std::numeric_limits<double>::infinity();
    keys = build_canvas3d_scene_fog_keyframes({legacy(nan, 0, 100), legacy(0, 0, inf),
        exponential(inf, 0.01), legacy(0, -1e300, 1e300)});
    check(keys.size() == 1, "invalid event distances/endpoints are filtered before sorting");
    const auto large = at(keys, 0);
    check(std::isfinite(large.start) && std::isfinite(large.end) &&
          std::isfinite(large.end - large.start), "shader range arithmetic stays finite");
    check(!at(keys, nan).enabled, "invalid camera mileage disables sample");
}

void shader_compile_contract() {
    for (const char* entry : {"vs_main", "ps_main", "ps_fog_main"}) {
        ID3DBlob* bytecode = nullptr;
        ID3DBlob* errors = nullptr;
        const char* profile = entry[0] == 'v' ? "vs_4_0" : "ps_4_0";
        const HRESULT result = D3DCompile(k_scene_shader_source, std::strlen(k_scene_shader_source),
            "scene_shader_source", nullptr, nullptr, entry, profile,
            D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_WARNINGS_ARE_ERRORS, 0, &bytecode, &errors);
        check(SUCCEEDED(result) && bytecode != nullptr, entry);
        if (FAILED(result) && errors) {
            std::cerr << static_cast<const char*>(errors->GetBufferPointer()) << '\n';
        }
        if (errors) errors->Release();
        if (bytecode) bytecode->Release();
    }
}

} // namespace

int canvas3d_scene_fog_contract() {
    legacy_contract();
    mixed_contract();
    ordering_contract();
    inheritance_and_safety_contract();
    shader_compile_contract();
    std::cout << "scene fog contract " << (failures ? "FAIL" : "PASS") << '\n';
    return failures;
}
