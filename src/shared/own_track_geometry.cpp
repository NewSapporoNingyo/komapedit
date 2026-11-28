/*
 * Copyright (c) 2026 Sapporo_ningyo
 *
 * Portions of the map parsing and track-geometry design are derived from or
 * reimplemented with reference to kobushi-trackviewer, Copyright (c) 2021-2024
 * konawasabi, licensed under Apache License 2.0.
 *
 * Licensed under Apache License 2.0; see LICENSE and NOTICE.
 */

#include "own_track_geometry.h"
#include "track_event_cursor.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <unordered_map>
#include <utility>

namespace own_track_geometry {
namespace {

constexpr double k_inf = std::numeric_limits<double>::infinity();
constexpr double k_pi = 3.141592653589793238462643383279502884;
constexpr double k_straight_radius_threshold = 1e6;

std::pair<double, double> rotate_xy(double x, double y, double theta) {
    return {std::cos(theta) * x - std::sin(theta) * y,
            std::sin(theta) * x + std::cos(theta) * y};
}

std::uint64_t double_cache_bits(double value) {
    if (value == 0.0) value = 0.0;
    std::uint64_t bits = 0;
    static_assert(sizeof(bits) == sizeof(value), "unexpected double size");
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

void hash_combine_bits(std::size_t& seed, std::uint64_t value) {
    seed ^= static_cast<std::size_t>(value + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2));
}

template <typename PhaseFunc>
std::pair<double, double> integrate_unit_tangent_gauss8(double a, double b, int panels, PhaseFunc&& phase) {
    static constexpr std::array<double, 4> nodes{
        0.18343464249564980494,
        0.52553240991632898582,
        0.79666647741362673959,
        0.96028985649753623168
    };
    static constexpr std::array<double, 4> weights{
        0.36268378337836198297,
        0.31370664587788728734,
        0.22238103445337447054,
        0.10122853629037625915
    };

    if (b <= a) return {0.0, 0.0};
    panels = std::max(1, panels);
    const double width = (b - a) / panels;
    double x = 0.0;
    double y = 0.0;
    for (int panel = 0; panel < panels; ++panel) {
        const double lo = a + width * panel;
        const double hi = panel + 1 == panels ? b : lo + width;
        const double mid = (lo + hi) * 0.5;
        const double half = (hi - lo) * 0.5;
        double panel_x = 0.0;
        double panel_y = 0.0;
        for (size_t i = 0; i < nodes.size(); ++i) {
            const double delta = half * nodes[i];
            const double p0 = phase(mid - delta);
            const double p1 = phase(mid + delta);
            panel_x += weights[i] * (std::cos(p0) + std::cos(p1));
            panel_y += weights[i] * (std::sin(p0) + std::sin(p1));
        }
        x += half * panel_x;
        y += half * panel_y;
    }
    return {x, y};
}

std::pair<double, double> fresnel_cs_series(double x) {
    const double x2 = x * x;
    const double x4 = x2 * x2;
    const double q = (k_pi * k_pi / 4.0) * x4;

    double c_term = x;
    double c = c_term;
    for (int n = 1; n < 80; ++n) {
        c_term *= -q * (4.0 * n - 3.0) /
                  ((2.0 * n - 1.0) * (2.0 * n) * (4.0 * n + 1.0));
        c += c_term;
        if (std::fabs(c_term) <= std::fabs(c) * 1e-16) break;
    }

    double s_term = (k_pi / 2.0) * x * x2 / 3.0;
    double s = s_term;
    for (int n = 1; n < 80; ++n) {
        s_term *= -q * (4.0 * n - 1.0) /
                  ((2.0 * n) * (2.0 * n + 1.0) * (4.0 * n + 3.0));
        s += s_term;
        if (std::fabs(s_term) <= std::fabs(s) * 1e-16) break;
    }
    return {c, s};
}

std::pair<double, double> fresnel_cs_asymptotic(double x) {
    const double phi = k_pi * x * x * 0.5;
    const double px2 = k_pi * x * x;
    const double px2_2 = px2 * px2;
    const double px2_4 = px2_2 * px2_2;
    const double px2_6 = px2_4 * px2_2;
    const double f = (1.0 / (k_pi * x)) *
        (1.0 - 3.0 / px2_2 + 105.0 / px2_4 - 10395.0 / px2_6);
    const double g = (1.0 / (k_pi * k_pi * x * x * x)) *
        (1.0 - 15.0 / px2_2 + 945.0 / px2_4 - 135135.0 / px2_6);
    return {0.5 + f * std::sin(phi) - g * std::cos(phi),
            0.5 - f * std::cos(phi) - g * std::sin(phi)};
}

std::pair<double, double> fresnel_cs(double x) {
    if (std::isnan(x)) return {x, x};
    if (x == 0.0) return {0.0, 0.0};
    const double sign = x < 0.0 ? -1.0 : 1.0;
    const double ax = std::fabs(x);
    std::pair<double, double> out;
    if (ax <= 1.6) {
        out = fresnel_cs_series(ax);
    } else if (ax >= 8.0) {
        out = fresnel_cs_asymptotic(ax);
    } else {
        const int panels = std::max(1, static_cast<int>(std::ceil(ax * ax * 2.0)));
        out = integrate_unit_tangent_gauss8(0.0, ax, panels, [](double t) {
            return k_pi * t * t * 0.5;
        });
    }
    return {sign * out.first, sign * out.second};
}

struct CurveResult {
    double x = 0.0;
    double y = 0.0;
    double tau = 0.0;
    double radius = 0.0;
};

struct CircularCurveKey {
    std::uint64_t radius = 0;
    std::uint64_t length = 0;

    bool operator==(const CircularCurveKey& other) const {
        return radius == other.radius && length == other.length;
    }
};

struct CircularCurveKeyHash {
    std::size_t operator()(const CircularCurveKey& key) const {
        std::size_t seed = 0;
        hash_combine_bits(seed, key.radius);
        hash_combine_bits(seed, key.length);
        return seed;
    }
};

struct TransitionCurveKey {
    bool half_sine = false;
    std::uint64_t length = 0;
    std::uint64_t radius0 = 0;
    std::uint64_t radius1 = 0;
    std::uint64_t position = 0;

    bool operator==(const TransitionCurveKey& other) const {
        return half_sine == other.half_sine &&
               length == other.length &&
               radius0 == other.radius0 &&
               radius1 == other.radius1 &&
               position == other.position;
    }
};

struct TransitionCurveKeyHash {
    std::size_t operator()(const TransitionCurveKey& key) const {
        std::size_t seed = key.half_sine ? 0x9e3779b97f4a7c15ULL : 0;
        hash_combine_bits(seed, key.length);
        hash_combine_bits(seed, key.radius0);
        hash_combine_bits(seed, key.radius1);
        hash_combine_bits(seed, key.position);
        return seed;
    }
};

constexpr size_t k_max_geometry_cache_entries = 262144;
std::mutex g_geometry_cache_mutex;
std::unordered_map<CircularCurveKey, CurveResult, CircularCurveKeyHash> g_circular_curve_cache;
std::unordered_map<TransitionCurveKey, CurveResult, TransitionCurveKeyHash> g_transition_curve_cache;

double radius_from_curvature(double curvature) {
    if (curvature == 0.0) return k_inf;
    double radius = 1.0 / curvature;
    return std::fabs(radius) > k_straight_radius_threshold ? k_inf : radius;
}

CurveResult circular_curve_local_uncached(double R, double l_intermediate) {
    if (R == 0.0 || std::isinf(R)) {
        return {l_intermediate, 0.0, 0.0, 0.0};
    }
    double tau = l_intermediate / R;
    double x0 = std::fabs(R) * std::sin(l_intermediate / std::fabs(R));
    double y0 = R * (1 - std::cos(l_intermediate / std::fabs(R)));
    return {x0, y0, tau, R};
}

CurveResult circular_curve_local(double R, double l_intermediate) {
    CircularCurveKey key{double_cache_bits(R), double_cache_bits(l_intermediate)};
    {
        std::lock_guard<std::mutex> lock(g_geometry_cache_mutex);
        auto it = g_circular_curve_cache.find(key);
        if (it != g_circular_curve_cache.end()) return it->second;
    }

    CurveResult result = circular_curve_local_uncached(R, l_intermediate);
    {
        std::lock_guard<std::mutex> lock(g_geometry_cache_mutex);
        if (g_circular_curve_cache.size() >= k_max_geometry_cache_entries) {
            g_circular_curve_cache.clear();
        }
        g_circular_curve_cache.emplace(key, result);
    }
    return result;
}

CurveResult circular_curve(double R, double theta, double l_intermediate) {
    CurveResult local = circular_curve_local(R, l_intermediate);
    auto [x, y] = rotate_xy(local.x, local.y, theta);
    return {x, y, local.tau, local.radius};
}

double inv_radius(double r) {
    return std::isinf(r) ? 0.0 : 1.0 / r;
}

struct HalfSinResult {
    double x = 0.0;
    double y = 0.0;
    double tau = 0.0;
    double radius = k_inf;
};

constexpr int k_max_half_sine_integration_panels = 100000;

HalfSinResult halfsin_intermediate(double L, double r1, double r2, double l_intermediate) {
    if (l_intermediate <= 0.0) {
        return {0.0, 0.0, 0.0, r1 == 0.0 ? k_inf : r1};
    }
    if (L == 0.0) return {0.0, 0.0, 0.0, r2};

    const double k0 = inv_radius(r1);
    const double k1 = inv_radius(r2);
    const double dk = k1 - k0;
    auto tau_at = [=](double x) {
        return k0 * x + 0.5 * dk * (x - L / k_pi * std::sin(k_pi * x / L));
    };
    const double tau = tau_at(l_intermediate);
    const double required_panels = std::ceil(std::max(
        l_intermediate / 250.0, std::fabs(tau) / 0.25));
    if (std::isinf(required_panels) ||
        required_panels > k_max_half_sine_integration_panels) {
        throw std::runtime_error(
            "Half-sine transition exceeds the supported integration limit of " +
            std::to_string(k_max_half_sine_integration_panels) + " panels");
    }
    const int panels = std::isnan(required_panels)
        ? 1
        : std::max(1, static_cast<int>(required_panels));
    auto [X, Y] = integrate_unit_tangent_gauss8(0.0, l_intermediate, panels, tau_at);
    const double k = k0 + 0.5 * dk * (1.0 - std::cos(k_pi * l_intermediate / L));
    double r = radius_from_curvature(k);
    return {X, Y, tau, r};
}

CurveResult linear_transition_curve_local(double L, double r1, double r2, double l_intermediate) {
    if (l_intermediate <= 0.0) return {0.0, 0.0, 0.0, std::fabs(r1) < k_straight_radius_threshold ? r1 : 0.0};
    if (L == 0.0) return {0.0, 0.0, 0.0, std::fabs(r2) < k_straight_radius_threshold ? r2 : 0.0};

    const double k0 = inv_radius(r1);
    const double k1 = inv_radius(r2);
    const double a = (k1 - k0) / L;
    const double k_at_l = k0 + a * l_intermediate;
    const double rl = radius_from_curvature(k_at_l);

    if (std::fabs(a) * std::max(1.0, L * L) < 1e-12) {
        CurveResult result = circular_curve_local(k0 != 0.0 ? 1.0 / k0 : k_inf, l_intermediate);
        result.radius = rl;
        return result;
    }

    const double root = std::sqrt(std::fabs(a) / k_pi);
    const double offset = k0 / a;
    const double u0 = root * offset;
    const double u1 = root * (l_intermediate + offset);
    auto [c0, s0] = fresnel_cs(u0);
    auto [c1, s1] = fresnel_cs(u1);
    double dc = c1 - c0;
    double ds = s1 - s0;
    if (a < 0.0) ds = -ds;

    const double phase = -k0 * k0 / (2.0 * a);
    const double scale = std::sqrt(k_pi / std::fabs(a));
    const double x = scale * (std::cos(phase) * dc - std::sin(phase) * ds);
    const double y = scale * (std::sin(phase) * dc + std::cos(phase) * ds);
    const double turn = k0 * l_intermediate + 0.5 * a * l_intermediate * l_intermediate;
    return {x, y, turn, rl};
}

CurveResult transition_curve_local(double L, double r1, double r2,
                                   bool half_sine, double l_intermediate) {
    TransitionCurveKey key{half_sine,
                           double_cache_bits(L),
                           double_cache_bits(r1),
                           double_cache_bits(r2),
                           double_cache_bits(l_intermediate)};
    {
        std::lock_guard<std::mutex> lock(g_geometry_cache_mutex);
        auto it = g_transition_curve_cache.find(key);
        if (it != g_transition_curve_cache.end()) return it->second;
    }

    CurveResult result;
    if (half_sine) {
        HalfSinResult half = halfsin_intermediate(L, r1, r2, l_intermediate);
        result = {half.x, half.y, half.tau, half.radius};
    } else {
        result = linear_transition_curve_local(L, r1, r2, l_intermediate);
    }

    {
        std::lock_guard<std::mutex> lock(g_geometry_cache_mutex);
        if (g_transition_curve_cache.size() >= k_max_geometry_cache_entries) {
            g_transition_curve_cache.clear();
        }
        g_transition_curve_cache.emplace(key, result);
    }
    return result;
}

CurveResult transition_curve(double L, double r1, double r2, double theta,
                             bool half_sine, double l_intermediate) {
    r1 = r1 == 0.0 ? k_inf : r1;
    r2 = r2 == 0.0 ? k_inf : r2;
    r1 = std::fabs(r1) > k_straight_radius_threshold ? k_inf : r1;
    r2 = std::fabs(r2) > k_straight_radius_threshold ? k_inf : r2;

    CurveResult local = transition_curve_local(L, r1, r2, half_sine, l_intermediate);
    auto [x, y] = rotate_xy(local.x, local.y, theta);
    return {x, y, local.tau, std::fabs(local.radius) < k_straight_radius_threshold ? local.radius : 0.0};
}

struct GradientProjection {
    double horizontal = 0.0;
    double vertical = 0.0;
    double gradient = 0.0;
};

GradientProjection constant_gradient_projection(double length, double gradient) {
    if (gradient == 0.0) return {length, 0.0, gradient};
    if (std::isfinite(gradient)) {
        const double norm = std::hypot(1000.0, gradient);
        return {length * (1000.0 / norm),
                length * (gradient / norm),
                gradient};
    }
    const double theta = std::atan(gradient / 1000.0);
    return {length * std::cos(theta), length * std::sin(theta), gradient};
}

double sinc(double value) {
    const double absolute = std::fabs(value);
    if (absolute < 1e-4) {
        const double squared = value * value;
        return 1.0 - squared / 6.0 + squared * squared / 120.0;
    }
    return std::sin(value) / value;
}

GradientProjection gradient_transition(double length, double gradient0, double gradient1,
                                       double intermediate_length) {
    if (length == 0.0 || gradient0 == gradient1) {
        return constant_gradient_projection(intermediate_length,
                                            length == 0.0 ? gradient1 : gradient0);
    }
    const double theta0 = std::atan(gradient0 / 1000.0);
    const double theta1 = std::atan(gradient1 / 1000.0);
    const double delta = (theta1 - theta0) * intermediate_length / length;
    const double half_delta = delta * 0.5;
    // Integrate cos(theta) and sin(theta) exactly while the slope angle changes
    // linearly. The sinc form avoids cancellation when the angle change is tiny.
    const double scale = sinc(half_delta);
    const double midpoint = theta0 + half_delta;
    const double end_theta = theta0 + delta;
    return {intermediate_length * std::cos(midpoint) * scale,
            intermediate_length * std::sin(midpoint) * scale,
            1000.0 * std::tan(end_theta)};
}

struct GradientProjectionSample {
    double projected_distance = 0.0;
    double elevation = 0.0;
    double gradient = 0.0;
    int last_event = -1;
};

std::vector<GradientProjectionSample> build_gradient_projection_samples(
    const std::vector<Event>& own_track, const std::vector<double>& controlpoints,
    const Checkpoint& state) {
    std::vector<GradientProjectionSample> result;
    result.reserve(controlpoints.size() - state.rows);

    TrackPointer gradient_pointer(own_track, "gradient", state.gradient_event);
    double last_distance = controlpoints[state.rows == 0 ? 0 : state.rows - 1];
    double last_gradient = state.gradient;
    double projected_distance = state.projected_distance;
    double elevation = state.elevation;

    for (std::size_t index = state.rows; index < controlpoints.size(); ++index) {
        const double distance = controlpoints[index];
        while (gradient_pointer.over_nextpoint(distance)) {
            const int origin = gradient_pointer.seekoriginofcontinuous(
                gradient_pointer.next());
            if (origin >= 0) {
                last_gradient = as_number(gradient_pointer.event(origin).value);
            }
            gradient_pointer.seeknext();
        }

        const double segment_length = distance - last_distance;
        GradientProjection segment =
            constant_gradient_projection(segment_length, last_gradient);
        if (gradient_pointer.last() >= 0 && gradient_pointer.next() >= 0) {
            const Event& next = gradient_pointer.event(gradient_pointer.next());
            if (!next.value.is_continue() &&
                interpolates_to(gradient_pointer.event(gradient_pointer.last()).flag, next.flag) &&
                last_gradient != as_number(next.value)) {
                segment = gradient_transition(next.distance - last_distance,
                                              last_gradient,
                                              as_number(next.value),
                                              segment_length);
            }
        }

        projected_distance += segment.horizontal;
        elevation += segment.vertical;
        last_gradient = segment.gradient;
        last_distance = distance;
        result.push_back({projected_distance, elevation, last_gradient, gradient_pointer.last()});
    }
    return result;
}

std::vector<double> build_event_projected_distances(
    const std::vector<Event>& own_track,
    const std::vector<double>& controlpoints,
    const std::vector<GradientProjectionSample>& projection,
    const Prefix* prefix) {
    std::vector<double> result(own_track.size());
    const std::size_t first = prefix ? prefix->state.rows : 0;
    std::size_t event_index = 0;
    if (prefix) {
        const double last_distance = controlpoints[first - 1];
        while (event_index < own_track.size() && own_track[event_index].distance <= last_distance) {
            result[event_index] = prefix->event_projections[event_index];
            ++event_index;
        }
    }
    std::size_t controlpoint_index = first;
    for (; event_index < own_track.size(); ++event_index) {
        const double distance = own_track[event_index].distance;
        while (controlpoint_index < controlpoints.size() &&
               controlpoints[controlpoint_index] < distance) {
            ++controlpoint_index;
        }
        if (controlpoint_index == controlpoints.size() ||
            controlpoints[controlpoint_index] != distance) {
            throw std::runtime_error("own-track event is missing its geometry control point");
        }
        result[event_index] = projection[controlpoint_index - first].projected_distance;
    }
    return result;
}

} // namespace

std::vector<double> sample(const std::vector<Event>& events,
                           const std::vector<double>& controlpoints,
                           Trace* trace, const Prefix* prefix) {
    if (controlpoints.empty()) return {};
    Checkpoint lp = prefix ? prefix->state : Checkpoint{};
    const std::size_t first = lp.rows;
    const auto gradient_projection = build_gradient_projection_samples(events, controlpoints, lp);
    auto event_projected_distances =
        build_event_projected_distances(events, controlpoints, gradient_projection, prefix);

    TrackPointer radius_p(events, "radius", lp.radius_event);
    TrackPointer turn_p(events, "turn", lp.turn_event);
    TrackPointer interpolate_p(events, "interpolate_func", lp.function_event);
    TrackPointer center_p(events, "center", lp.center_event);
    TrackPointer gauge_p(events, "gauge", lp.gauge_event);
    CantProcessor cant_gen(TrackPointer(events, "cant", lp.cant_event), lp.cant_anchor);
    std::vector<double> result;
    result.reserve((controlpoints.size() - first) * columns);

    for (std::size_t controlpoint_index = first; controlpoint_index < controlpoints.size();
         ++controlpoint_index) {
        const double dist = controlpoints[controlpoint_index];
        const GradientProjectionSample& projection = gradient_projection[controlpoint_index - first];
        while (interpolate_p.on_nextpoint(dist)) {
            lp.half_sine = as_number(interpolate_p.event(interpolate_p.next()).value) == 0.0;
            interpolate_p.seeknext();
        }

        double center_tmp = lp.center;
        while (center_p.on_nextpoint(dist)) {
            center_tmp = as_number(center_p.event(center_p.next()).value);
            center_p.seeknext();
        }

        double gauge_tmp = lp.gauge;
        while (gauge_p.on_nextpoint(dist)) {
            gauge_tmp = as_number(gauge_p.event(gauge_p.next()).value);
            gauge_p.seeknext();
        }

        while (radius_p.over_nextpoint(dist)) {
            int origin = radius_p.seekoriginofcontinuous(radius_p.next());
            if (origin >= 0) {
                double val = as_number(radius_p.event(origin).value);
                lp.radius = val;
                lp.radius_anchor_value = val;
                lp.radius_anchor_theta = lp.theta;
            }
            radius_p.seeknext();
        }

        const double step_length = projection.projected_distance - lp.projected_distance;
        auto [x, y, tau, radius] = [&]() -> CurveResult {
            bool straight = lp.radius == 0.0;
            if (radius_p.last() >= 0 && radius_p.next() >= 0) {
                const auto& next = radius_p.event(radius_p.next());
                if (!next.value.is_continue() &&
                    interpolates_to(radius_p.event(radius_p.last()).flag, next.flag)) {
                    const double next_radius = as_number(next.value);
                    if (lp.radius_anchor_value != next_radius) {
                        const double transition_start =
                            event_projected_distances[static_cast<size_t>(radius_p.last())];
                        const double transition_end =
                            event_projected_distances[static_cast<size_t>(radius_p.next())];
                        const double total = transition_end - transition_start;
                        if (total == 0.0) {
                            return {0.0, 0.0, 0.0, dist >= next.distance ? next_radius : lp.radius_anchor_value};
                        }
                        const auto before = transition_curve(total, lp.radius_anchor_value, next_radius,
                            lp.radius_anchor_theta, lp.half_sine, lp.projected_distance - transition_start);
                        const auto after = transition_curve(total, lp.radius_anchor_value, next_radius,
                            lp.radius_anchor_theta, lp.half_sine, projection.projected_distance - transition_start);
                        return {after.x - before.x, after.y - before.y, after.tau - before.tau,
                                after.radius};
                    }
                    straight = next_radius == 0.0;
                }
            }
            if (straight) {
                return {std::cos(lp.theta) * step_length, std::sin(lp.theta) * step_length,
                        0.0, lp.radius};
            }
            const auto step = circular_curve(lp.radius, lp.theta, step_length);
            return {step.x, step.y, step.tau, lp.radius};
        }();

        if (turn_p.on_nextpoint(dist)) {
            tau += std::atan(as_number(turn_p.event(turn_p.next()).value));
            turn_p.seeknext();
        }

        double cant_tmp = cant_gen.process(dist, lp.half_sine);

        lp.x += x;
        lp.y += y;
        lp.elevation = projection.elevation;
        lp.projected_distance = projection.projected_distance;
        lp.theta += tau;
        lp.radius = radius;
        lp.gradient = projection.gradient;
        lp.cant = cant_tmp;
        lp.center = center_tmp;
        lp.gauge = gauge_tmp;

        result.insert(result.end(), {dist, lp.x, lp.y, lp.elevation, lp.theta, lp.radius,
                                    lp.gradient, lp.half_sine ? 0.0 : 1.0,
                                    lp.cant, lp.center, lp.gauge});
        constexpr std::size_t checkpoint_stride = 128;
        if (trace && (controlpoint_index + 1) % checkpoint_stride == 0) {
            lp.rows = controlpoint_index + 1;
            lp.gradient_event = projection.last_event;
            lp.radius_event = radius_p.last();
            lp.turn_event = turn_p.last();
            lp.function_event = interpolate_p.last();
            lp.center_event = center_p.last();
            lp.gauge_event = gauge_p.last();
            lp.cant_event = cant_gen.last_event();
            lp.cant_anchor = cant_gen.current_value();
            trace->checkpoints.push_back(lp);
        }
    }
    if (trace) trace->event_projections = std::move(event_projected_distances);
    return result;
}

} // namespace own_track_geometry
