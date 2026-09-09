/*
 * Copyright (c) 2026 Sapporo_ningyo
 *
 * Licensed under Apache License 2.0; see LICENSE and NOTICE.
 */

// Pure CPU contracts for the shared route-value event semantics and the 3D
// route-overlay curve text. No parser, Direct3D device, window, or ImGui state.

#include "route_value_sampling.h"
#include "scene_route_overlay.h"

#include <cmath>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

namespace {

int failures = 0;

void check(bool condition, const char* message) {
    if (condition) return;
    ++failures;
    std::cerr << "FAIL: " << message << '\n';
}

void check_near(double actual, double expected, const char* message) {
    if (std::isfinite(actual) && std::abs(actual - expected) <= 1e-9) return;
    ++failures;
    std::cerr << "FAIL: " << message << " (expected " << expected
              << ", got " << actual << ")\n";
}

void append(std::vector<route_value_sampling::Event>& events,
            double distance,
            bool has_value,
            double value,
            const char* flag,
            double& current_value) {
    check(route_value_sampling::append_event(
              events, distance, has_value, value, flag, current_value),
          "valid route value event is appended");
}

std::string formatted_curve_line(
    const std::vector<route_value_sampling::Event>& radii,
    const std::vector<route_value_sampling::Event>& cants,
    double distance) {
    char output[512] = {};
    scene_route_overlay::format_curve_line(
        output, sizeof(output), radii, cants, distance,
        "Straight", "(Interpolate)");
    return output;
}

void interpolation_contract() {
    std::vector<route_value_sampling::Event> events;
    double current = 0.0;
    append(events, 0.0, true, 400.0, "", current);
    append(events, 100.0, true, 800.0, "i", current);
    append(events, 200.0, true, 1200.0, "i", current);

    const auto before = route_value_sampling::sample(events, -1.0);
    check(before.mode == route_value_sampling::Mode::Constant,
          "before first event is constant");
    check_near(before.value, 0.0, "before first event uses default zero");

    const auto middle = route_value_sampling::sample(events, 50.0);
    check(middle.mode == route_value_sampling::Mode::Interpolate,
          "interval ending at Interpolate is classified as interpolation");
    check_near(middle.from_distance, 0.0, "interpolation exposes previous distance");
    check_near(middle.to_distance, 100.0, "interpolation exposes next distance");
    check_near(middle.from_value, 400.0, "interpolation exposes previous value");
    check_near(middle.to_value, 800.0, "interpolation exposes next value");

    const auto endpoint = route_value_sampling::sample(events, 100.0);
    check(endpoint.mode == route_value_sampling::Mode::Interpolate,
          "an exact Interpolate point owns the following interval");
    check_near(endpoint.value, 800.0, "exact endpoint uses its evaluated value");
    check_near(endpoint.from_distance, 100.0,
               "following interpolation starts at the exact endpoint");
    check_near(endpoint.to_distance, 200.0,
               "following interpolation exposes its next endpoint");
    check_near(endpoint.from_value, 800.0,
               "following interpolation starts with the endpoint value");
    check_near(endpoint.to_value, 1200.0,
               "following interpolation exposes the next value");

    const auto final_endpoint = route_value_sampling::sample(events, 200.0);
    check(final_endpoint.mode == route_value_sampling::Mode::Constant,
          "the final Interpolate point is constant without a following endpoint");
    check_near(final_endpoint.value, 1200.0,
               "the final endpoint keeps its evaluated value");
}

void omitted_value_contract() {
    std::vector<route_value_sampling::Event> events;
    double current = 0.0;
    append(events, 0.0, true, -600.0, "i", current);
    append(events, 50.0, false, 0.0, "i", current);
    check_near(current, -600.0, "omitted Interpolate value inherits the previous value");
    check_near(events.back().previous_value, -600.0,
               "omitted event records its inherited previous value");
    check_near(events.back().value, -600.0,
               "omitted event keeps the inherited current value");
    check(events.back().kind == route_value_sampling::EventKind::Interpolate,
          "omitted Interpolate retains its endpoint kind for plan markers");

    const auto sample = route_value_sampling::sample(events, 25.0);
    check(sample.mode == route_value_sampling::Mode::Interpolate,
          "omitted Interpolate still owns the preceding interval");
    check_near(sample.from_value, -600.0, "omitted interval keeps its start value");
    check_near(sample.to_value, -600.0, "omitted interval keeps its end value");
}

void transition_contract() {
    std::vector<route_value_sampling::Event> events;
    double current = 0.0;
    append(events, 0.0, true, 0.0, "", current);
    append(events, 100.0, false, 0.0, "bt", current);
    append(events, 200.0, true, 500.0, "", current);

    const auto sample = route_value_sampling::sample(events, 150.0);
    check(sample.mode == route_value_sampling::Mode::Transition,
          "BeginTransition owns the interval to the next value event");
    check_near(sample.from_distance, 100.0, "transition exposes its start distance");
    check_near(sample.to_distance, 200.0, "transition exposes its end distance");
    check_near(sample.from_value, 0.0, "transition exposes pre-transition value");
    check_near(sample.to_value, 500.0, "transition exposes target value");
}

void invalid_event_contract() {
    std::vector<route_value_sampling::Event> events;
    double current = 12.0;
    check(!route_value_sampling::append_event(
              events, std::numeric_limits<double>::infinity(), true, 20.0, "i", current),
          "non-finite distance is rejected");
    check(!route_value_sampling::append_event(
              events, 10.0, true, std::numeric_limits<double>::quiet_NaN(), "i", current),
          "non-finite explicit value is rejected");
    check(!route_value_sampling::append_event(
              events, 10.0, false, 0.0, "", current),
          "missing ordinary value is rejected");
    check(events.empty(), "invalid events do not mutate the output series");
    check_near(current, 12.0, "invalid events do not mutate current value");
}

void curve_overlay_contract() {
    std::vector<route_value_sampling::Event> radii;
    std::vector<route_value_sampling::Event> cants;
    double radius = 0.0;
    double cant = 0.0;
    append(radii, 0.0, true, 400.0, "i", radius);
    append(cants, 0.0, true, 0.05, "i", cant);
    append(radii, 100.0, true, 800.0, "i", radius);
    append(cants, 100.0, true, 0.025, "i", cant);
    check(formatted_curve_line(radii, cants, 50.0) ==
              u8"(Interpolate) R 400 m 0.05 → ➤ R 800 m 0.025 →",
          "overlay shows both positive-radius interpolation endpoints");

    radii.clear();
    cants.clear();
    radius = 0.0;
    cant = 0.0;
    append(radii, 0.0, true, 0.0, "i", radius);
    append(cants, 0.0, true, 0.0, "i", cant);
    append(radii, 100.0, true, -1200.0, "i", radius);
    append(cants, 100.0, true, -0.04, "i", cant);
    check(formatted_curve_line(radii, cants, 25.0) ==
              u8"(Interpolate) R 0 m 0 ➤ R 1200 m -0.04 ←",
          "overlay preserves straight endpoint values and negative-radius direction");

    check(formatted_curve_line(radii, cants, 100.0) ==
              u8"R 1200 m -0.04 ←",
          "overlay leaves the exact endpoint in constant mode");
}

} // namespace

int main() {
    interpolation_contract();
    omitted_value_contract();
    transition_contract();
    invalid_event_contract();
    curve_overlay_contract();
    std::cout << "route value sampling contract " << (failures ? "FAIL" : "PASS") << '\n';
    return failures == 0 ? 0 : 1;
}
