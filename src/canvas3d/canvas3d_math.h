/*
 * Copyright (c) 2026 Sapporo_ningyo
 *
 * Licensed under Apache License 2.0; see LICENSE and NOTICE.
 */

#pragma once

#include "canvas3D.h"
#include <algorithm>
#include <array>
#include <cmath>

namespace canvas3d_detail {

struct Vec3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

inline Vec3 operator-(Vec3 a, Vec3 b) {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}

inline Vec3 operator+(Vec3 a, Vec3 b) {
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}

inline Vec3 operator*(Vec3 v, float s) {
    return {v.x * s, v.y * s, v.z * s};
}

inline float dot(Vec3 a, Vec3 b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

inline Vec3 cross(Vec3 a, Vec3 b) {
    return {
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x
    };
}

inline Vec3 normalize(Vec3 v) {
    float len = std::sqrt(std::max(dot(v, v), 1e-12f));
    return {v.x / len, v.y / len, v.z / len};
}

struct DVec3 {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

inline DVec3 operator-(DVec3 a, DVec3 b) {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}

inline DVec3 operator+(DVec3 a, DVec3 b) {
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}

inline DVec3 operator*(DVec3 v, double s) {
    return {v.x * s, v.y * s, v.z * s};
}

inline DVec3 dvec3_from_vec3(Vec3 v) {
    return {static_cast<double>(v.x), static_cast<double>(v.y), static_cast<double>(v.z)};
}

inline double dot(DVec3 a, DVec3 b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

inline DVec3 cross(DVec3 a, DVec3 b) {
    return {
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x
    };
}

inline DVec3 normalize(DVec3 v) {
    double len = std::sqrt(std::max(dot(v, v), 1e-12));
    return {v.x / len, v.y / len, v.z / len};
}

inline DVec3 right_from_theta_d(double theta) {
    return {std::cos(theta), 0.0, std::sin(theta)};
}

inline DVec3 forward_from_theta_d(double theta) {
    return {std::sin(theta), 0.0, -std::cos(theta)};
}

inline DVec3 rotate_axis(DVec3 v, DVec3 axis, double radians) {
    axis = normalize(axis);
    double c = std::cos(radians);
    double s = std::sin(radians);
    return v * c + cross(axis, v) * s + axis * (dot(axis, v) * (1.0 - c));
}

inline void apply_track_cant(DVec3& right, DVec3& up, const DVec3& forward,
                      double cant_angle) {
    if (std::abs(cant_angle) <= 1e-9 || !std::isfinite(cant_angle)) return;
    const DVec3 axis = forward * -1.0;
    right = rotate_axis(right, axis, -cant_angle);
    up = rotate_axis(up, axis, -cant_angle);
}

inline void scene_track_surface_frame(const Canvas3DTrackPoint& point,
                               DVec3& right,
                               DVec3& up,
                               DVec3& forward) {
    const double gradient =
        std::isfinite(point.gradient) ? point.gradient / 1000.0 : 0.0;
    forward = normalize(DVec3{
        std::sin(point.theta), gradient, -std::cos(point.theta)});
    right = normalize(cross(forward, DVec3{0.0, 1.0, 0.0}));
    up = normalize(cross(right, forward));
    apply_track_cant(right, up, forward, point.cant_angle);
}

inline Canvas3DTrackPoint scene_sound3d_source_point(Canvas3DTrackPoint point,
                                               double x,
                                               double y) {
    DVec3 right;
    DVec3 up;
    DVec3 forward;
    scene_track_surface_frame(point, right, up, forward);
    const DVec3 source = DVec3{point.x, point.y, point.z} + right * x + up * y;
    point.x = source.x;
    point.y = source.y;
    point.z = source.z;
    return point;
}

struct Mat4 {
    float m[4][4] = {};
};

inline Mat4 identity() {
    Mat4 r;
    for (int i = 0; i < 4; ++i) r.m[i][i] = 1.0f;
    return r;
}

inline Mat4 multiply(const Mat4& a, const Mat4& b) {
    Mat4 r;
    for (int row = 0; row < 4; ++row) {
        for (int col = 0; col < 4; ++col) {
            for (int k = 0; k < 4; ++k) r.m[row][col] += a.m[row][k] * b.m[k][col];
        }
    }
    return r;
}

inline Mat4 translation(float x, float y, float z) {
    Mat4 r = identity();
    r.m[3][0] = x;
    r.m[3][1] = y;
    r.m[3][2] = z;
    return r;
}

inline Mat4 rotation_x(float angle) {
    Mat4 r = identity();
    float c = std::cos(angle);
    float s = std::sin(angle);
    r.m[1][1] = c;
    r.m[1][2] = s;
    r.m[2][1] = -s;
    r.m[2][2] = c;
    return r;
}

inline Mat4 rotation_y(float angle) {
    Mat4 r = identity();
    float c = std::cos(angle);
    float s = std::sin(angle);
    r.m[0][0] = c;
    r.m[0][2] = -s;
    r.m[2][0] = s;
    r.m[2][2] = c;
    return r;
}

inline Mat4 look_to_bve(Vec3 eye, Vec3 forward, Vec3 up) {
    Vec3 zaxis = normalize(forward);
    Vec3 xaxis = normalize(cross(zaxis, up));
    Vec3 yaxis = cross(xaxis, zaxis);

    Mat4 r = identity();
    r.m[0][0] = xaxis.x;
    r.m[1][0] = xaxis.y;
    r.m[2][0] = xaxis.z;
    r.m[3][0] = -dot(xaxis, eye);
    r.m[0][1] = yaxis.x;
    r.m[1][1] = yaxis.y;
    r.m[2][1] = yaxis.z;
    r.m[3][1] = -dot(yaxis, eye);
    r.m[0][2] = zaxis.x;
    r.m[1][2] = zaxis.y;
    r.m[2][2] = zaxis.z;
    r.m[3][2] = -dot(zaxis, eye);
    return r;
}

inline Mat4 perspective_fov_lh_reverse_z(float fovy, float aspect, float zn, float zf) {
    Mat4 r;
    float y_scale = 1.0f / std::tan(fovy * 0.5f);
    float x_scale = y_scale / std::max(aspect, 0.001f);
    float span = std::max(zf - zn, 0.001f);
    r.m[0][0] = x_scale;
    r.m[1][1] = y_scale;
    r.m[2][2] = -zn / span;
    r.m[2][3] = 1.0f;
    r.m[3][2] = zn * zf / span;
    return r;
}

struct Vec4 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float w = 0.0f;
};

inline Vec4 transform_point_row(Vec3 p, const Mat4& m) {
    return {
        p.x * m.m[0][0] + p.y * m.m[1][0] + p.z * m.m[2][0] + m.m[3][0],
        p.x * m.m[0][1] + p.y * m.m[1][1] + p.z * m.m[2][1] + m.m[3][1],
        p.x * m.m[0][2] + p.y * m.m[1][2] + p.z * m.m[2][2] + m.m[3][2],
        p.x * m.m[0][3] + p.y * m.m[1][3] + p.z * m.m[2][3] + m.m[3][3],
    };
}

inline DVec3 transform_point_row(const double world[16], Vec3 p) {
    return {
        static_cast<double>(p.x) * world[0] + static_cast<double>(p.y) * world[4] +
            static_cast<double>(p.z) * world[8] + world[12],
        static_cast<double>(p.x) * world[1] + static_cast<double>(p.y) * world[5] +
            static_cast<double>(p.z) * world[9] + world[13],
        static_cast<double>(p.x) * world[2] + static_cast<double>(p.y) * world[6] +
            static_cast<double>(p.z) * world[10] + world[14],
    };
}

inline std::array<Vec3, 8> bounds_corners(Vec3 mn, Vec3 mx) {
    return {{
        {mn.x, mn.y, mn.z},
        {mx.x, mn.y, mn.z},
        {mn.x, mx.y, mn.z},
        {mx.x, mx.y, mn.z},
        {mn.x, mn.y, mx.z},
        {mx.x, mn.y, mx.z},
        {mn.x, mx.y, mx.z},
        {mx.x, mx.y, mx.z}
    }};
}

inline bool scene_bounds_valid(Vec3 mn, Vec3 mx) {
    return std::isfinite(mn.x) && std::isfinite(mn.y) && std::isfinite(mn.z) &&
        std::isfinite(mx.x) && std::isfinite(mx.y) && std::isfinite(mx.z) &&
        mx.x >= mn.x && mx.y >= mn.y && mx.z >= mn.z;
}

inline Vec3 scene_bounds_min_or_sphere(Vec3 mn, Vec3 mx, Vec3 center, float radius) {
    return scene_bounds_valid(mn, mx) ? mn : Vec3{center.x - radius, center.y - radius, center.z - radius};
}

inline Vec3 scene_bounds_max_or_sphere(Vec3 mn, Vec3 mx, Vec3 center, float radius) {
    return scene_bounds_valid(mn, mx) ? mx : Vec3{center.x + radius, center.y + radius, center.z + radius};
}
inline void store_world(double out[16], DVec3 right, DVec3 up, DVec3 forward, DVec3 origin) {
    right = normalize(right);
    up = normalize(up);
    forward = normalize(forward);
    DVec3 model_z = forward * -1.0;
    const double values[16] = {
        right.x, right.y, right.z, 0.0,
        up.x, up.y, up.z, 0.0,
        model_z.x, model_z.y, model_z.z, 0.0,
        origin.x, origin.y, origin.z, 1.0
    };
    std::copy(values, values + 16, out);
}

inline void apply_euler(DVec3& right, DVec3& up, DVec3& forward,
                 double rx_deg, double ry_deg, double rz_deg) {
    constexpr double deg_to_rad = 0.01745329251994329577;
    double rx = rx_deg * deg_to_rad;
    double ry = ry_deg * deg_to_rad;
    double rz = rz_deg * deg_to_rad;
    if (std::abs(rx) > 1e-6) {
        up = rotate_axis(up, right, -rx);
        forward = rotate_axis(forward, right, -rx);
    }
    if (std::abs(rz) > 1e-6) {
        right = rotate_axis(right, forward * -1.0, rz);
        up = rotate_axis(up, forward * -1.0, rz);
    }
    if (std::abs(ry) > 1e-6) {
        right = rotate_axis(right, up, -ry);
        forward = rotate_axis(forward, up, -ry);
    }
}

} // namespace canvas3d_detail
