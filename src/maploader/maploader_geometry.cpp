/*
 * Copyright (c) 2026 Sapporo_ningyo
 *
 * Portions of the map parsing and track-geometry design are derived from or
 * reimplemented with reference to kobushi-trackviewer, Copyright (c) 2021-2024
 * konawasabi, licensed under Apache License 2.0.
 *
 * Licensed under Apache License 2.0; see LICENSE and NOTICE.
 */

#include "maploader_internal.h"

namespace kme::maploader::detail {

using kme::maploader::log_info;

struct LastPos {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    double theta = 0.0;
    double radius = 0.0;
    double gradient = 0.0;
    double distance = 0.0;
    std::string interpolate_func = "line";
    double cant = 0.0;
    double center = 0.0;
    double gauge = 0.0;
};

class TrackPointer {
public:
    TrackPointer(const std::vector<OwnTrackEvent>& data, std::string target)
        : data_(&data), target_(std::move(target)) {
        ix_max_ = static_cast<int>(data.size()) - 1;
        next_ = seek(0);
    }

    TrackPointer(const std::vector<OtherTrackEvent>& data, std::string target)
        : other_data_(&data), target_(std::move(target)), other_(true) {
        ix_max_ = static_cast<int>(data.size()) - 1;
        next_ = seek(0);
    }

    int last() const { return last_; }
    int next() const { return next_; }

    bool on_nextpoint(double distance) const {
        return next_ >= 0 && event(next_).distance == distance;
    }

    bool over_nextpoint(double distance) const {
        return next_ >= 0 && event(next_).distance < distance;
    }

    void seeknext() {
        if (next_ >= 0) {
            last_ = next_;
            next_ = seek(next_ + 1);
        }
    }

    int seekoriginofcontinuous(int index) const {
        if (index < 0) return -1;
        while (index >= 0) {
            const auto& e = event(index);
            if (e.key == target_ && !e.value.is_continue()) return index;
            --index;
        }
        return -1;
    }

    const OwnTrackEvent& event(int index) const {
        if (other_) {
            temp_.distance = (*other_data_)[index].distance;
            temp_.key = (*other_data_)[index].key;
            temp_.value = (*other_data_)[index].value;
            temp_.flag = (*other_data_)[index].flag;
            return temp_;
        }
        return (*data_)[index];
    }

private:
    int seek(int ix0) const {
        int ix = ix0;
        while (ix <= ix_max_) {
            if (event(ix).key == target_) return ix;
            ++ix;
        }
        return -1;
    }

    const std::vector<OwnTrackEvent>* data_ = nullptr;
    const std::vector<OtherTrackEvent>* other_data_ = nullptr;
    std::string target_;
    bool other_ = false;
    int ix_max_ = -1;
    int last_ = -1;
    int next_ = -1;
    mutable OwnTrackEvent temp_;
};

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
    const double q = (kPi * kPi / 4.0) * x4;

    double c_term = x;
    double c = c_term;
    for (int n = 1; n < 80; ++n) {
        c_term *= -q * (4.0 * n - 3.0) /
                  ((2.0 * n - 1.0) * (2.0 * n) * (4.0 * n + 1.0));
        c += c_term;
        if (std::fabs(c_term) <= std::fabs(c) * 1e-16) break;
    }

    double s_term = (kPi / 2.0) * x * x2 / 3.0;
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
    const double phi = kPi * x * x * 0.5;
    const double px2 = kPi * x * x;
    const double px2_2 = px2 * px2;
    const double px2_4 = px2_2 * px2_2;
    const double px2_6 = px2_4 * px2_2;
    const double f = (1.0 / (kPi * x)) *
        (1.0 - 3.0 / px2_2 + 105.0 / px2_4 - 10395.0 / px2_6);
    const double g = (1.0 / (kPi * kPi * x * x * x)) *
        (1.0 - 15.0 / px2_2 + 945.0 / px2_4 - 135135.0 / px2_6);
    return {0.5 + f * std::sin(phi) - g * std::cos(phi),
            0.5 - f * std::cos(phi) - g * std::sin(phi)};
}

std::pair<double, double> fresnel_cs(double x) {
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
            return kPi * t * t * 0.5;
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

constexpr size_t kMaxGeometryCacheEntries = 262144;
std::mutex g_geometry_cache_mutex;
std::unordered_map<CircularCurveKey, CurveResult, CircularCurveKeyHash> g_circular_curve_cache;
std::unordered_map<TransitionCurveKey, CurveResult, TransitionCurveKeyHash> g_transition_curve_cache;

double radius_from_curvature(double curvature) {
    if (curvature == 0.0) return kInf;
    double radius = 1.0 / curvature;
    return std::fabs(radius) > 1e6 ? kInf : radius;
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
        if (g_circular_curve_cache.size() >= kMaxGeometryCacheEntries) {
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
    double radius = kInf;
};

HalfSinResult halfsin_intermediate(double L, double r1, double r2, double l_intermediate, double dL = 1.0) {
    (void)dL;
    if (l_intermediate <= 0.0) {
        return {0.0, 0.0, 0.0, r1 == 0.0 ? kInf : r1};
    }
    if (L == 0.0) return {0.0, 0.0, 0.0, r2};

    const double k0 = inv_radius(r1);
    const double k1 = inv_radius(r2);
    const double dk = k1 - k0;
    auto tau_at = [=](double x) {
        return k0 * x + 0.5 * dk * (x - L / kPi * std::sin(kPi * x / L));
    };
    const double tau = tau_at(l_intermediate);
    const int panels = std::max(1, static_cast<int>(std::ceil(std::max(l_intermediate / 250.0,
                                                                         std::fabs(tau) / 0.25))));
    auto [X, Y] = integrate_unit_tangent_gauss8(0.0, l_intermediate, panels, tau_at);
    const double k = k0 + 0.5 * dk * (1.0 - std::cos(kPi * l_intermediate / L));
    double r = radius_from_curvature(k);
    return {X, Y, tau, r};
}

CurveResult linear_transition_curve_local(double L, double r1, double r2, double l_intermediate) {
    if (l_intermediate <= 0.0) return {0.0, 0.0, 0.0, std::fabs(r1) < 1e6 ? r1 : 0.0};
    if (L == 0.0) return {0.0, 0.0, 0.0, std::fabs(r2) < 1e6 ? r2 : 0.0};

    const double k0 = inv_radius(r1);
    const double k1 = inv_radius(r2);
    const double a = (k1 - k0) / L;
    const double k_at_l = k0 + a * l_intermediate;
    const double rl = radius_from_curvature(k_at_l);

    if (std::fabs(a) * std::max(1.0, L * L) < 1e-12) {
        CurveResult result = circular_curve_local(k0 != 0.0 ? 1.0 / k0 : kInf, l_intermediate);
        result.radius = rl;
        return result;
    }

    const double root = std::sqrt(std::fabs(a) / kPi);
    const double offset = k0 / a;
    const double u0 = root * offset;
    const double u1 = root * (l_intermediate + offset);
    auto [c0, s0] = fresnel_cs(u0);
    auto [c1, s1] = fresnel_cs(u1);
    double dc = c1 - c0;
    double ds = s1 - s0;
    if (a < 0.0) ds = -ds;

    const double phase = -k0 * k0 / (2.0 * a);
    const double scale = std::sqrt(kPi / std::fabs(a));
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
        if (g_transition_curve_cache.size() >= kMaxGeometryCacheEntries) {
            g_transition_curve_cache.clear();
        }
        g_transition_curve_cache.emplace(key, result);
    }
    return result;
}

CurveResult transition_curve(double L, double r1, double r2, double theta,
                             const std::string& func, double l_intermediate) {
    r1 = r1 == 0.0 ? kInf : r1;
    r2 = r2 == 0.0 ? kInf : r2;
    r1 = std::fabs(r1) > 1e6 ? kInf : r1;
    r2 = std::fabs(r2) > 1e6 ? kInf : r2;

    CurveResult local = transition_curve_local(L, r1, r2, func == "sin", l_intermediate);
    auto [x, y] = rotate_xy(local.x, local.y, theta);
    return {x, y, local.tau, std::fabs(local.radius) < 1e6 ? local.radius : 0.0};
}

std::pair<double, double> gradient_transition(double L, double gr1, double gr2, double l_intermediate) {
    double theta1 = std::atan(gr1 / 1000.0);
    double theta2 = std::atan(gr2 / 1000.0);
    double z = L / (theta2 - theta1) * std::cos(theta1) -
               L / (theta2 - theta1) * std::cos((theta2 - theta1) / L * l_intermediate + theta1);
    double gradient = 1000.0 * std::tan((theta2 - theta1) / L * l_intermediate + theta1);
    return {z, gradient};
}

class CantProcessor {
public:
    CantProcessor(TrackPointer pointer, const std::vector<OwnTrackEvent>& data, double last_cant)
        : pointer_(std::move(pointer)), own_data_(&data), cant_value_(last_cant) {}

    CantProcessor(TrackPointer pointer, const std::vector<OtherTrackEvent>& data, double last_cant)
        : pointer_(std::move(pointer)), other_data_(&data), other_(true), cant_value_(last_cant) {}

    double process(double dist, const std::string& func) {
        while (pointer_.over_nextpoint(dist)) {
            int origin = pointer_.seekoriginofcontinuous(pointer_.next());
            if (origin >= 0) {
                cant_value_ = as_number(event(origin).value);
            }
            pointer_.seeknext();
        }

        if (pointer_.last() < 0 || pointer_.next() < 0) return cant_value_;
        OwnTrackEvent next = event(pointer_.next());
        if (next.value.is_continue()) return cant_value_;
        OwnTrackEvent last = event(pointer_.last());
        if (next.flag == "i" || last.flag == "bt") {
            double next_value = as_number(next.value);
            if (cant_value_ != next_value) {
                return transition(next.distance - last.distance, cant_value_, next_value,
                                  func, dist - last.distance);
            }
        }
        return cant_value_;
    }

private:
    const OwnTrackEvent& event(int index) const {
        if (other_) {
            temp_.distance = (*other_data_)[index].distance;
            temp_.key = (*other_data_)[index].key;
            temp_.value = (*other_data_)[index].value;
            temp_.flag = (*other_data_)[index].flag;
            return temp_;
        }
        return (*own_data_)[index];
    }

    double transition(double L, double c1, double c2, const std::string& func, double l) const {
        if (L == 0.0) return c2;
        if (func == "sin") return (c2 - c1) / 2.0 * (std::sin(kPi / L * l - kPi / 2.0) + 1.0) + c1;
        return (c2 - c1) / L * l + c1;
    }

    TrackPointer pointer_;
    const std::vector<OwnTrackEvent>* own_data_ = nullptr;
    const std::vector<OtherTrackEvent>* other_data_ = nullptr;
    bool other_ = false;
    double cant_value_ = 0.0;
    mutable OwnTrackEvent temp_;
};

std::vector<double> sorted_unique(std::vector<double> values) {
    std::sort(values.begin(), values.end());
    values.erase(std::unique(values.begin(), values.end()), values.end());
    return values;
}

void append_arange(std::vector<double>& values, double start, double end, double step) {
    if (step <= 0.0) return;
    int guard = 0;
    for (double v = start; v < end && guard < 10000000; v += step, ++guard) {
        values.push_back(v);
    }
}

double round_minus2(double value) {
    return std::round(value / 100.0) * 100.0;
}

void generate_owntrack(MapContext& ctx, double unitdist,
                       bool has_arb, double arb_start, double arb_end, double arb_step,
                       const std::vector<double>* extra_controlpoints = nullptr) {
    std::vector<double> list_cp = ctx.controlpoints;
    if (list_cp.empty()) list_cp.push_back(0.0);
    if (extra_controlpoints) {
        list_cp.insert(list_cp.end(), extra_controlpoints->begin(), extra_controlpoints->end());
    }
    list_cp = sorted_unique(list_cp);
    double cp_min = list_cp.front();
    double cp_max = list_cp.back();
    double equaldist_unit = unitdist > 0.0 ? unitdist : 25.0;
    const double boundary_margin = 500.0;

    if (has_arb) {
        ctx.has_cp_arbdistribution = true;
        ctx.cp_arbdistribution = {arb_start, arb_end, arb_step};
        if (!ctx.station_position.empty()) {
            double min_station = ctx.station_position.begin()->first;
            double max_station = ctx.station_position.rbegin()->first;
            ctx.cp_arbdistribution_default = {std::max(0.0, round_minus2(min_station) - boundary_margin),
                                              round_minus2(max_station) + boundary_margin,
                                              equaldist_unit};
        } else {
            ctx.cp_arbdistribution_default = {std::max(0.0, round_minus2(cp_min) - boundary_margin),
                                              round_minus2(cp_max) + boundary_margin,
                                              equaldist_unit};
        }
        ctx.cp_defaultrange = {ctx.cp_arbdistribution_default[0],
                               ctx.cp_arbdistribution_default[1]};
        append_arange(list_cp, arb_start, arb_end, arb_step);
    } else if (!ctx.station_position.empty()) {
        double min_station = ctx.station_position.begin()->first;
        double max_station = ctx.station_position.rbegin()->first;
        double start = std::max(0.0, round_minus2(min_station) - boundary_margin);
        double end = round_minus2(max_station) + boundary_margin;
        append_arange(list_cp, start, end, equaldist_unit);
        ctx.has_cp_arbdistribution = true;
        ctx.cp_arbdistribution = {start, end, equaldist_unit};
        ctx.cp_arbdistribution_default = ctx.cp_arbdistribution;
        ctx.cp_defaultrange = {start, end};
    } else {
        double start = std::max(0.0, round_minus2(cp_min) - boundary_margin);
        double end = round_minus2(cp_max) + boundary_margin;
        append_arange(list_cp, start, end, equaldist_unit);
        ctx.has_cp_arbdistribution = true;
        ctx.cp_arbdistribution = {start, end, equaldist_unit};
        ctx.cp_arbdistribution_default = ctx.cp_arbdistribution;
        ctx.cp_defaultrange = {start, end};
    }
    list_cp = sorted_unique(list_cp);

    TrackPointer radius_p(ctx.own_track, "radius");
    TrackPointer gradient_p(ctx.own_track, "gradient");
    TrackPointer turn_p(ctx.own_track, "turn");
    TrackPointer interpolate_p(ctx.own_track, "interpolate_func");
    TrackPointer cant_p(ctx.own_track, "cant");
    TrackPointer center_p(ctx.own_track, "center");
    TrackPointer gauge_p(ctx.own_track, "gauge");

    LastPos lp;
    lp.distance = list_cp.front();
    struct RadiusLast {
        double distance = 0.0;
        double theta = 0.0;
        double radius = 0.0;
    } rlp;
    rlp.distance = lp.distance;

    CantProcessor cant_gen(std::move(cant_p), ctx.own_track, lp.cant);
    ctx.owntrack_buffer.clear(11);
    ctx.owntrack_buffer.reserve_rows(list_cp.size());

    for (double dist : list_cp) {
        while (interpolate_p.on_nextpoint(dist)) {
            lp.interpolate_func = as_text(interpolate_p.event(interpolate_p.next()).value);
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
                rlp.radius = val;
                rlp.distance = radius_p.event(origin).distance;
                rlp.theta = lp.theta;
            }
            radius_p.seeknext();
        }

        double c_theta = lp.theta;
        double c_ds = dist - lp.distance;
        double x = 0.0, y = 0.0, tau = 0.0, radius = lp.radius;

        if (radius_p.last() < 0) {
            if (radius_p.next() < 0 || lp.radius == 0.0) {
                x = std::cos(c_theta) * c_ds;
                y = std::sin(c_theta) * c_ds;
            } else {
                auto res = circular_curve(lp.radius, c_theta, c_ds);
                x = res.x; y = res.y; tau = res.tau; radius = lp.radius;
            }
        } else if (radius_p.next() < 0) {
            if (lp.radius == 0.0) {
                x = std::cos(c_theta) * c_ds;
                y = std::sin(c_theta) * c_ds;
            } else {
                auto res = circular_curve(lp.radius, c_theta, c_ds);
                x = res.x; y = res.y; tau = res.tau;
            }
            radius = lp.radius;
        } else {
            const auto& next = radius_p.event(radius_p.next());
            if (next.value.is_continue()) {
                if (lp.radius == 0.0) {
                    x = std::cos(c_theta) * c_ds;
                    y = std::sin(c_theta) * c_ds;
                } else {
                    auto res = circular_curve(lp.radius, c_theta, c_ds);
                    x = res.x; y = res.y; tau = res.tau;
                }
                radius = lp.radius;
            } else if (next.flag == "i" || radius_p.event(radius_p.last()).flag == "bt") {
                double next_radius = as_number(next.value);
                if (rlp.radius != next_radius) {
                    double total = next.distance - radius_p.event(radius_p.last()).distance;
                    double last_l = lp.distance - radius_p.event(radius_p.last()).distance;
                    double cur_l = dist - radius_p.event(radius_p.last()).distance;
                    CurveResult pos_last = transition_curve(total, rlp.radius, next_radius,
                                                            rlp.theta, lp.interpolate_func, last_l);
                    CurveResult pos_cur = transition_curve(total, rlp.radius, next_radius,
                                                           rlp.theta, lp.interpolate_func, cur_l);
                    x = pos_cur.x - pos_last.x;
                    y = pos_cur.y - pos_last.y;
                    tau = pos_cur.tau - pos_last.tau;
                    radius = pos_cur.radius;
                } else if (next_radius != 0.0) {
                    auto res = circular_curve(lp.radius, c_theta, c_ds);
                    x = res.x; y = res.y; tau = res.tau; radius = lp.radius;
                } else {
                    x = std::cos(c_theta) * c_ds;
                    y = std::sin(c_theta) * c_ds;
                    radius = lp.radius;
                }
            } else {
                if (lp.radius == 0.0) {
                    x = std::cos(c_theta) * c_ds;
                    y = std::sin(c_theta) * c_ds;
                } else {
                    auto res = circular_curve(lp.radius, c_theta, c_ds);
                    x = res.x; y = res.y; tau = res.tau;
                }
                radius = lp.radius;
            }
        }

        if (turn_p.next() >= 0 && turn_p.on_nextpoint(dist)) {
            tau += std::atan(as_number(turn_p.event(turn_p.next()).value));
            turn_p.seeknext();
        }

        while (gradient_p.over_nextpoint(dist)) {
            int origin = gradient_p.seekoriginofcontinuous(gradient_p.next());
            if (origin >= 0) {
                lp.gradient = as_number(gradient_p.event(origin).value);
            }
            gradient_p.seeknext();
        }

        double g_ds = dist - lp.distance;
        double gradient = lp.gradient;
        double z = 0.0;
        if (gradient_p.last() >= 0 && gradient_p.next() >= 0) {
            const auto& next = gradient_p.event(gradient_p.next());
            if (!next.value.is_continue() &&
                (next.flag == "i" || gradient_p.event(gradient_p.last()).flag == "bt") &&
                lp.gradient != as_number(next.value)) {
                auto gz = gradient_transition(next.distance - lp.distance,
                                              lp.gradient, as_number(next.value), g_ds);
                z = gz.first;
                gradient = gz.second;
            } else {
                z = g_ds * std::sin(std::atan(lp.gradient / 1000.0));
            }
        } else {
            z = g_ds * std::sin(std::atan(lp.gradient / 1000.0));
        }

        double cant_tmp = cant_gen.process(dist, lp.interpolate_func);

        lp.x += x;
        lp.y += y;
        lp.z += z;
        lp.theta += tau;
        lp.radius = radius;
        lp.gradient = gradient;
        lp.distance = dist;
        lp.cant = cant_tmp;
        lp.center = center_tmp;
        lp.gauge = gauge_tmp;

        ctx.owntrack_buffer.push({dist, lp.x, lp.y, lp.z, lp.theta, lp.radius, lp.gradient,
                                  lp.interpolate_func == "sin" ? 0.0 : 1.0,
                                  lp.cant, lp.center, lp.gauge});
    }
}

void generate_curveradius(MapContext& ctx) {
    ctx.curveradius_buffer.clear(2);
    ctx.curveradius_buffer.reserve_rows(ctx.own_track.size() * 2 + 2);
    if (ctx.owntrack_buffer.rows == 0) return;
    double min_cp = ctx.owntrack_buffer.data[0];
    double max_cp = ctx.owntrack_buffer.data[(ctx.owntrack_buffer.rows - 1) * ctx.owntrack_buffer.cols];
    ctx.curveradius_buffer.push({min_cp, 0.0});
    TrackPointer radius_p(ctx.own_track, "radius");
    bool previous_bt = false;
    double previous = 0.0;
    while (radius_p.next() >= 0) {
        const auto& e = radius_p.event(radius_p.next());
        double new_radius = e.value.is_continue() ? previous : as_number(e.value);
        if (e.value.is_continue() || previous_bt || e.flag == "i") {
            ctx.curveradius_buffer.push({e.distance, new_radius});
        } else {
            ctx.curveradius_buffer.push({e.distance, previous});
            ctx.curveradius_buffer.push({e.distance, new_radius});
        }
        previous = new_radius;
        previous_bt = e.flag == "bt";
        radius_p.seeknext();
    }
    ctx.curveradius_buffer.push({max_cp, 0.0});
}

double relative_position(double L, double radius, double ya, double yb, double l_intermediate) {
    if (L == 0.0) return yb;
    if (radius != 0.0) {
        double sintheta = std::sqrt(L * L + (yb - ya) * (yb - ya)) / (2.0 * radius);
        if (std::fabs(sintheta) <= 1.0) {
            double tau = std::atan((yb - ya) / L);
            double theta = 2.0 * std::asin(sintheta);
            double phiA = theta / 2.0 - tau;
            double x0 = radius * std::sin(phiA);
            double y0 = ya + radius * std::cos(phiA);
            return y0 - radius * std::cos(std::asin((l_intermediate - x0) / radius));
        }
    }
    return (yb - ya) / L * l_intermediate + ya;
}

struct OtherTrackBuildResult {
    std::string key;
    Matrix buffer;
    double seconds = 0.0;
    bool has_buffer = false;
};

Matrix build_othertrack_buffer(const MapContext& ctx, const std::string& trackkey, bool& has_buffer) {
    const auto& data = ctx.othertrack.at(trackkey);
    has_buffer = false;
    if (data.empty() || ctx.owntrack_buffer.rows == 0) return {};

    TrackPointer x_position_p(data, "x.position");
    TrackPointer x_radius_p(data, "x.radius");
    TrackPointer y_position_p(data, "y.position");
    TrackPointer y_radius_p(data, "y.radius");
    TrackPointer func_p(data, "interpolate_func");
    TrackPointer center_p(data, "center");
    TrackPointer gauge_p(data, "gauge");
    TrackPointer cant_p(data, "cant");

    double x_position_last = 0.0, x_position_next = 0.0;
    double x_radius_last = 0.0, x_radius_next = 0.0;
    double y_position_last = 0.0, y_position_next = 0.0;
    double y_radius_last = 0.0, y_radius_next = 0.0;
    double center_last = 0.0, center_next = 0.0;
    double gauge_last = 0.0, gauge_next = 0.0;
    double cant_initial = 0.0;
    std::string func_last = "line";
    std::string func_next = "line";

    auto init_numeric = [](TrackPointer& p, double& last, double& next) {
        if (p.next() < 0) return;
        const auto& e = p.event(p.next());
        double v = e.value.is_continue() ? 0.0 : as_number(e.value);
        last = v;
        next = v;
    };
    init_numeric(x_position_p, x_position_last, x_position_next);
    init_numeric(x_radius_p, x_radius_last, x_radius_next);
    init_numeric(y_position_p, y_position_last, y_position_next);
    init_numeric(y_radius_p, y_radius_last, y_radius_next);
    init_numeric(center_p, center_last, center_next);
    init_numeric(gauge_p, gauge_last, gauge_next);
    if (cant_p.next() >= 0) {
        const auto& e = cant_p.event(cant_p.next());
        cant_initial = e.value.is_continue() ? 0.0 : as_number(e.value);
    }
    if (func_p.next() >= 0) {
        const auto& e = func_p.event(func_p.next());
        func_last = func_next = e.value.is_continue() ? "line" : as_text(e.value);
    }

    CantProcessor cant_gen(std::move(cant_p), data, cant_initial);
    Matrix result;
    result.clear(8);
    result.reserve_rows(ctx.owntrack_buffer.rows);

    auto advance_over = [](TrackPointer& p, double& last, double& next, double dist) {
        while (p.over_nextpoint(dist)) {
            p.seeknext();
            last = next;
            if (p.next() >= 0) {
                const auto& e = p.event(p.next());
                next = e.value.is_continue() ? last : as_number(e.value);
            }
        }
    };
    auto advance_on = [](TrackPointer& p, double& last, double& next, double dist) {
        while (p.on_nextpoint(dist)) {
            p.seeknext();
            last = next;
            if (p.next() >= 0) {
                const auto& e = p.event(p.next());
                next = e.value.is_continue() ? last : as_number(e.value);
            }
        }
    };

    // Child tracks keep their first defined value before the first explicit point.
    // The initial values above already hold that first point, so emit the full own-track range.
    for (size_t r = 0; r < ctx.owntrack_buffer.rows; ++r) {
        const double* element = &ctx.owntrack_buffer.data[r * ctx.owntrack_buffer.cols];
        double dist = element[0];

        advance_over(x_position_p, x_position_last, x_position_next, dist);
        advance_over(x_radius_p, x_radius_last, x_radius_next, dist);
        advance_over(y_position_p, y_position_last, y_position_next, dist);
        advance_over(y_radius_p, y_radius_last, y_radius_next, dist);

        while (func_p.on_nextpoint(dist)) {
            func_p.seeknext();
            func_last = func_next;
            if (func_p.next() >= 0) {
                const auto& e = func_p.event(func_p.next());
                func_next = e.value.is_continue() ? func_last : as_text(e.value);
            }
        }
        advance_on(center_p, center_last, center_next, dist);
        advance_on(gauge_p, gauge_last, gauge_next, dist);

        double out_x = 0.0, out_y = 0.0;
        double sin_theta = std::sin(element[4]);
        double cos_theta = std::cos(element[4]);
        if (x_position_p.last() >= 0 && x_position_p.next() >= 0) {
            double x_distance_last = x_position_p.event(x_position_p.last()).distance;
            double x_distance_next = x_position_p.event(x_position_p.next()).distance;
            double rel = relative_position(x_distance_next - x_distance_last,
                                           x_radius_last, x_position_last,
                                           x_position_next, dist - x_distance_last);
            out_x = element[1] - sin_theta * rel;
            out_y = element[2] + cos_theta * rel;
        } else {
            double rel = x_position_last;
            out_x = element[1] - sin_theta * rel;
            out_y = element[2] + cos_theta * rel;
        }

        double out_z = 0.0;
        if (y_position_p.last() >= 0 && y_position_p.next() >= 0) {
            double y_distance_last = y_position_p.event(y_position_p.last()).distance;
            double y_distance_next = y_position_p.event(y_position_p.next()).distance;
            double rel = relative_position(y_distance_next - y_distance_last,
                                           y_radius_last, y_position_last,
                                           y_position_next, dist - y_distance_last);
            out_z = rel + element[3];
        } else {
            out_z = y_position_last + element[3];
        }

        double cant = cant_gen.process(dist, func_last);
        result.push({dist, out_x, out_y, out_z, func_last == "sin" ? 0.0 : 1.0,
                     cant, center_last, gauge_last});
    }
    has_buffer = true;
    return result;
}

void relocate(MapContext& ctx) {
    ctx.controlpoints = sorted_unique(ctx.controlpoints);
    std::stable_sort(ctx.own_track.begin(), ctx.own_track.end(),
                     [](const auto& a, const auto& b) { return a.distance < b.distance; });
    for (auto& kv : ctx.othertrack) {
        auto& rows = kv.second;
        std::stable_sort(rows.begin(), rows.end(),
                         [](const auto& a, const auto& b) { return a.distance < b.distance; });
        if (!rows.empty()) {
            ctx.othertrack_range[kv.first] = {rows.front().distance, rows.front().distance};
            for (const auto& e : rows) {
                ctx.othertrack_range[kv.first].first = std::min(ctx.othertrack_range[kv.first].first, e.distance);
                ctx.othertrack_range[kv.first].second = std::max(ctx.othertrack_range[kv.first].second, e.distance);
            }
        }
    }
    auto by_distance = [](const auto& a, const auto& b) { return a.distance < b.distance; };
    std::stable_sort(ctx.structure_loads.begin(), ctx.structure_loads.end(), by_distance);
    std::stable_sort(ctx.structure_puts.begin(), ctx.structure_puts.end(), by_distance);
    std::stable_sort(ctx.structure_betweens.begin(), ctx.structure_betweens.end(), by_distance);
    std::stable_sort(ctx.other_trains.begin(), ctx.other_trains.end(), by_distance);
    std::stable_sort(ctx.other_train_enables.begin(), ctx.other_train_enables.end(), by_distance);
    std::stable_sort(ctx.other_train_stops.begin(), ctx.other_train_stops.end(), by_distance);
    std::stable_sort(ctx.repeaters.begin(), ctx.repeaters.end(), by_distance);
    std::stable_sort(ctx.section_begins.begin(), ctx.section_begins.end(), by_distance);
    std::stable_sort(ctx.section_speed_limits.begin(), ctx.section_speed_limits.end(), by_distance);
    std::stable_sort(ctx.signal_puts.begin(), ctx.signal_puts.end(), by_distance);
    std::stable_sort(ctx.beacons.begin(), ctx.beacons.end(), by_distance);
    std::stable_sort(ctx.pretrains.begin(), ctx.pretrains.end(), by_distance);
    std::stable_sort(ctx.map_sounds.begin(), ctx.map_sounds.end(), by_distance);
    std::stable_sort(ctx.map_sound_3d.begin(), ctx.map_sound_3d.end(), by_distance);
    std::stable_sort(ctx.rolling_noises.begin(), ctx.rolling_noises.end(), by_distance);
    std::stable_sort(ctx.flange_noises.begin(), ctx.flange_noises.end(), by_distance);
    std::stable_sort(ctx.joint_noises.begin(), ctx.joint_noises.end(), by_distance);
    std::stable_sort(ctx.irregularities.begin(), ctx.irregularities.end(), by_distance);
    std::stable_sort(ctx.backgrounds.begin(), ctx.backgrounds.end(), by_distance);
    std::stable_sort(ctx.adhesions.begin(), ctx.adhesions.end(), by_distance);
    std::stable_sort(ctx.cab_illuminance.begin(), ctx.cab_illuminance.end(), by_distance);
    std::stable_sort(ctx.fogs.begin(), ctx.fogs.end(), by_distance);
    std::stable_sort(ctx.speedlimits.begin(), ctx.speedlimits.end(), by_distance);
    std::stable_sort(ctx.station_puts.begin(), ctx.station_puts.end(), by_distance);
}

void build_structure_put_buffer(MapContext& ctx) {
    ctx.structure_put_buffer.clear(10);
    ctx.structure_put_buffer.reserve_rows(ctx.structure_puts.size());
    for (const auto& row : ctx.structure_puts) {
        ctx.structure_put_buffer.push({row.distance, row.x, row.y, row.z,
                                       row.rx, row.ry, row.rz, row.tilt,
                                       row.span, static_cast<double>(row.order)});
    }
}

double matrix_value(const Matrix& matrix, size_t row, size_t col) {
    return matrix.data[row * matrix.cols + col];
}

double angle_delta_abs(double a, double b) {
    return std::abs(std::atan2(std::sin(b - a), std::cos(b - a)));
}

void append_controlpoint_if_in_range(std::vector<double>& values, double distance,
                                     double min_distance, double max_distance) {
    if (!std::isfinite(distance)) return;
    constexpr double eps = 1e-6;
    if (distance < min_distance - eps || distance > max_distance + eps) return;
    values.push_back(std::clamp(distance, min_distance, max_distance));
}

void append_scene_model_distance(std::vector<double>& values, double distance, double span,
                                 double min_distance, double max_distance) {
    append_controlpoint_if_in_range(values, distance, min_distance, max_distance);
    if (span > 1.0) append_controlpoint_if_in_range(values, distance + span, min_distance, max_distance);
}

std::vector<double> build_scene_adaptive_controlpoints(const MapContext& ctx,
                                                       const Matrix& baseline,
                                                       double min_step,
                                                       double max_step,
                                                       double max_angle_degrees,
                                                       double max_chord_error) {
    std::vector<double> values;
    if (baseline.rows < 2 || baseline.cols < 5) return values;

    min_step = std::clamp(std::isfinite(min_step) ? min_step : 1.0, 0.25, 100.0);
    max_step = std::clamp(std::isfinite(max_step) ? max_step : 25.0, min_step, 200.0);
    const double max_angle = std::max(0.001, (std::isfinite(max_angle_degrees) ? max_angle_degrees : 1.0) * kPi / 180.0);
    const double max_error = std::max(0.001, std::isfinite(max_chord_error) ? max_chord_error : 0.01);
    const double min_distance = matrix_value(baseline, 0, 0);
    const double max_distance = matrix_value(baseline, baseline.rows - 1, 0);

    values.reserve(baseline.rows);
    for (size_t row = 1; row < baseline.rows; ++row) {
        const double a_distance = matrix_value(baseline, row - 1, 0);
        const double b_distance = matrix_value(baseline, row, 0);
        const double span = b_distance - a_distance;
        if (!(span > 0.0) || !std::isfinite(span)) continue;

        double desired_step = max_step;
        if (baseline.cols > 5) {
            const double r0 = std::abs(matrix_value(baseline, row - 1, 5));
            const double r1 = std::abs(matrix_value(baseline, row, 5));
            double radius = 0.0;
            if (r0 > 1e-6 && r1 > 1e-6) radius = std::min(r0, r1);
            else if (r0 > 1e-6) radius = r0;
            else if (r1 > 1e-6) radius = r1;
            if (radius > 1e-6 && std::isfinite(radius)) {
                desired_step = std::min(desired_step, std::sqrt(std::max(min_step * min_step,
                                                                         8.0 * radius * max_error)));
            }
        }

        const double theta_delta = angle_delta_abs(matrix_value(baseline, row - 1, 4),
                                                   matrix_value(baseline, row, 4));
        if (theta_delta > max_angle) {
            const double angle_step = span / std::ceil(theta_delta / max_angle);
            desired_step = std::min(desired_step, angle_step);
        }

        desired_step = std::clamp(desired_step, min_step, max_step);
        const int divisions = std::max(1, static_cast<int>(std::ceil(span / desired_step)));
        for (int i = 1; i < divisions; ++i) {
            values.push_back(a_distance + span * (static_cast<double>(i) / static_cast<double>(divisions)));
        }
    }

    for (const StructurePut& row : ctx.structure_puts) {
        append_scene_model_distance(values, row.distance, row.span, min_distance, max_distance);
    }
    for (const StructurePut& row : ctx.structure_betweens) {
        append_controlpoint_if_in_range(values, row.distance, min_distance, max_distance);
    }

    struct ActiveRepeater {
        double begin = 0.0;
        double interval = 0.0;
        double span = 0.0;
    };
    auto append_repeater_range = [&](const ActiveRepeater& repeater, double end_distance) {
        if (end_distance < repeater.begin) return;
        append_scene_model_distance(values, repeater.begin, repeater.span, min_distance, max_distance);
        append_scene_model_distance(values, end_distance, repeater.span, min_distance, max_distance);
        if (repeater.interval <= 1e-9 || !std::isfinite(repeater.interval)) return;

        size_t guard = 0;
        for (double distance = repeater.begin; distance < end_distance + 1e-6; distance += repeater.interval) {
            append_scene_model_distance(values, distance, repeater.span, min_distance, max_distance);
            if (++guard > 1000000) break;
        }
    };

    std::map<std::string, ActiveRepeater> active_repeaters;
    for (const RepeaterEvent& row : ctx.repeaters) {
        std::string key = key_text(row.repeater_key);
        if (key.empty()) continue;
        if (row.method == "Begin" || row.method == "Begin0") {
            auto existing = active_repeaters.find(key);
            if (existing != active_repeaters.end()) {
                append_repeater_range(existing->second, row.distance);
                active_repeaters.erase(existing);
            }
            active_repeaters[key] = ActiveRepeater{row.distance, row.interval, row.span};
        } else if (row.method == "End") {
            auto existing = active_repeaters.find(key);
            if (existing == active_repeaters.end()) continue;
            append_repeater_range(existing->second, row.distance);
            active_repeaters.erase(existing);
        }
    }
    for (const auto& kv : active_repeaters) {
        append_repeater_range(kv.second, max_distance);
    }

    return values;
}

void generate_geometry(MapContext& ctx, double unitdist,
                       bool has_arb, double arb_start, double arb_end, double arb_step,
                       const std::vector<double>* extra_controlpoints,
                       bool generate_auxiliary_buffers) {
    log_info("calculating track geometry");
    ctx.scene_geometry_valid = false;
    ctx.unit_distance = unitdist;
    ctx.cp_arbdistribution_explicit = has_arb;
    ctx.othertrack_buffers.clear();
    ctx.timing.owntrack_seconds = 0.0;
    ctx.timing.othertrack_seconds.clear();
    ctx.timing.json_seconds = 0.0;
    ctx.load_timing_logged = false;
    {
        ScopedTimer timer(&ctx.timing.owntrack_seconds);
        generate_owntrack(ctx, unitdist, has_arb, arb_start, arb_end, arb_step, extra_controlpoints);
    }
    if (generate_auxiliary_buffers) generate_curveradius(ctx);
    std::vector<std::future<OtherTrackBuildResult>> futures;
    futures.reserve(ctx.othertrack_order.size());
    for (const auto& key : ctx.othertrack_order) {
        futures.push_back(std::async(std::launch::async, [&ctx, key]() {
            auto started_at = SteadyClock::now();
            OtherTrackBuildResult out;
            out.key = key;
            out.buffer = build_othertrack_buffer(ctx, key, out.has_buffer);
            out.seconds = elapsed_seconds_since(started_at);
            return out;
        }));
    }
    for (auto& future : futures) {
        OtherTrackBuildResult result = future.get();
        ctx.timing.othertrack_seconds.push_back({result.key, result.seconds});
        if (result.has_buffer) {
            ctx.othertrack_buffers[result.key] = std::move(result.buffer);
        }
    }
    if (generate_auxiliary_buffers) build_structure_put_buffer(ctx);
    ctx.ir_json_cache_by_flags.clear();
}


} // namespace kme::maploader::detail
