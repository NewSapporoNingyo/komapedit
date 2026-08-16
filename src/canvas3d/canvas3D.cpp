/*
 * Copyright (c) 2026 Sapporo_ningyo
 *
 * Licensed under Apache License 2.0; see LICENSE and NOTICE.
 */

#ifdef _MSC_VER
#pragma execution_character_set("utf-8")
#endif

#include "canvas3D.h"
#include "numeric_safety.h"

#include "repeater_linkage.h"

#include "kme.h"
#include "maploader.h"
#include "model_loader.h"
#include "runtime_paths.h"
#include "text_decoder.h"
#include "touch_input.h"

#include "imgui.h"

#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wincodec.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <map>
#include <filesystem>
#include <initializer_list>
#include <iterator>
#include <mutex>
#include <limits>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace {

constexpr float k_default_scene_camera_height = 2.0f;
constexpr double k_scene_repeater_distance_epsilon = 1e-6;
constexpr long long k_scene_repeater_instance_limit = 1000000;
constexpr float k_scene_camera_fov_y = 0.6108652382f;
constexpr float k_scene_near_z = 0.2f;
constexpr float k_scene_background_near_z = 0.05f;
constexpr float k_scene_background_far_z = 5000.0f;
constexpr float k_scene_depth_clear = 0.0f;
constexpr float k_material_opaque_alpha_threshold = 0.98f;
constexpr float k_scene_track_marker_width = 0.5f;
constexpr float k_scene_track_marker_alpha = 0.8f;
constexpr double k_scene_mileage_pick_half_width = 100.0;
constexpr float k_scene_mileage_highlight_top = 1.0f;
constexpr float k_scene_mileage_highlight_bottom = -2.0f;
constexpr float k_scene_mileage_highlight_alpha = 0.5f;
constexpr float k_scene_highlight_outline_width_px = 5.0f;
constexpr float k_model_preview_fov_y = 0.78539816339f;
constexpr double k_scene_object_jump_back_m = 25.0;
constexpr double k_scene_focus_highlight_seconds = 3.0;
// The event-driven canvas stops repainting while idle, so preserve the last active FPS across long gaps.
constexpr double k_scene_fps_idle_reset_seconds = 0.25;
constexpr float k_scene_fps_smoothing = 0.15f;
constexpr float k_scene_gizmo_length_px = 72.0f;
constexpr float k_scene_gizmo_origin_gap_px = 9.0f;
constexpr float k_scene_gizmo_hit_radius_px = 8.0f;
constexpr float k_scene_gizmo_arrow_length_px = 11.0f;
constexpr float k_scene_gizmo_arrow_half_width_px = 5.5f;
constexpr float k_scene_gizmo_center_radius_px = 4.0f;
constexpr double k_scene_route_display_zero_epsilon = 0.0000005;
constexpr size_t k_scene_model_max_workers = 8;
constexpr size_t k_scene_chunk_count_limit = 100000;
constexpr double k_default_scene_fog_density = 0.001;
constexpr double k_default_scene_fog_color = 0.875;
constexpr float k_scene_marker_board_width = 1.0f;
constexpr float k_scene_marker_board_alpha = 0.70f;
constexpr float k_scene_marker_lateral_gap = 0.10f;
constexpr float k_scene_marker_sound3d_tag_tip_height = 0.30f;
constexpr float k_scene_marker_face_offset = 0.003f;
constexpr float k_scene_marker_icon_half_extent = 0.18f;
constexpr float k_scene_marker_label_height = 0.18f;
constexpr float k_scene_marker_label_center_y = 0.22f;
constexpr float k_scene_marker_other_track_label_center_y = 0.50f;
constexpr float k_scene_marker_label_max_width = 0.88f;
constexpr float k_scene_marker_outline_width = 0.012f;
constexpr float k_scene_marker_irregularity_content_offset = 0.16f;
constexpr int k_scene_marker_circle_segments = 48;
constexpr size_t k_scene_marker_target_missing = std::numeric_limits<size_t>::max();
constexpr size_t k_scene_marker_list_kind_count =
    static_cast<size_t>(Canvas3DSceneMarkerListKind::Count);

#ifndef NDEBUG
std::atomic<int> g_debug_put_between_derive_throw_countdown{0};
#endif

bool scene_marker_list_kind_is_navigable(Canvas3DSceneMarkerListKind kind) {
    const size_t slot = static_cast<size_t>(kind);
    return slot > static_cast<size_t>(Canvas3DSceneMarkerListKind::None) &&
        slot < k_scene_marker_list_kind_count;
}

size_t scene_marker_list_kind_slot(Canvas3DSceneMarkerListKind kind) {
    return static_cast<size_t>(kind);
}

enum class SceneOverlayCorner {
    TopLeft,
    TopRight,
    BottomRight,
};

struct SceneOverlayLabelLayout {
    ImVec2 pos{};
    float pad = 0.0f;
};

float normalize_material_alpha(float value) {
    float alpha = clamp_color_component(value);
    // BVE .x models commonly use 0.99/0.999999 for opaque alpha-tested textures.
    return alpha >= k_material_opaque_alpha_threshold ? 1.0f : alpha;
}

ImVec4 scene_highlight_color_for_kind(Canvas3DSceneObjectKind kind) {
    switch (kind) {
    case Canvas3DSceneObjectKind::Structure:
        return ImVec4(1.0f, 216.0f / 255.0f, 48.0f / 255.0f, 0.92f);
    case Canvas3DSceneObjectKind::Repeater:
        return ImVec4(1.0f, 105.0f / 255.0f, 190.0f / 255.0f, 0.92f);
    case Canvas3DSceneObjectKind::Signal:
        return ImVec4(0.62f, 1.0f, 0.72f, 0.92f);
    default:
        return ImVec4(0.62f, 1.0f, 0.72f, 0.92f);
    }
}

ImVec4 scene_marker_highlight_color() {
    return ImVec4(65.0f / 255.0f, 126.0f / 255.0f, 245.0f / 255.0f, 0.92f);
}

enum class ScenePickTargetKind {
    None,
    Object,
    Marker,
};

struct ScenePickTarget {
    ScenePickTargetKind kind = ScenePickTargetKind::None;
    size_t index = 0;
};

bool scene_pick_id_for_object(int object_index, unsigned int& id) {
    if (object_index < 0 || object_index >= 0x00ffffff) return false;
    id = static_cast<unsigned int>(object_index) + 1u;
    return true;
}

std::array<float, 4> scene_pick_color_for_id(unsigned int id) {
    return {{
        static_cast<float>(id & 0xffu) / 255.0f,
        static_cast<float>((id >> 8) & 0xffu) / 255.0f,
        static_cast<float>((id >> 16) & 0xffu) / 255.0f,
        1.0f
    }};
}

unsigned int scene_pick_id_from_rgba(const unsigned char* rgba) {
    if (!rgba) return 0;
    return static_cast<unsigned int>(rgba[0]) |
        (static_cast<unsigned int>(rgba[1]) << 8) |
        (static_cast<unsigned int>(rgba[2]) << 16);
}

ScenePickTarget scene_pick_target_from_id(unsigned int id,
                                          size_t object_count,
                                          size_t marker_count) {
    if (id == 0) return {};
    const size_t index = static_cast<size_t>(id - 1u);
    if (index < object_count) return {ScenePickTargetKind::Object, index};
    const size_t marker_index = index - object_count;
    if (marker_index < marker_count) return {ScenePickTargetKind::Marker, marker_index};
    return {};
}

bool texture_pixels_have_alpha(const std::vector<unsigned char>& pixels) {
    for (size_t i = 3; i < pixels.size(); i += 4) {
        if (pixels[i] < 255) return true;
    }
    return false;
}

std::string win32_error_text(DWORD code) {
    if (code == 0) return {};
    wchar_t* buffer = nullptr;
    DWORD flags = FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS;
    DWORD n = FormatMessageW(flags, nullptr, code, 0, reinterpret_cast<LPWSTR>(&buffer), 0, nullptr);
    if (n == 0 || !buffer) return "Win32 error " + std::to_string(code);
    std::wstring text(buffer, n);
    LocalFree(buffer);
    while (!text.empty() && (text.back() == L'\n' || text.back() == L'\r' || text.back() == L' ')) text.pop_back();
    return wide_to_utf8(text);
}

std::string hresult_text(const char* action, HRESULT hr) {
    std::string message = action;
    message += " failed: 0x";
    char hex[16] = {};
    std::snprintf(hex, sizeof(hex), "%08lx", static_cast<unsigned long>(hr));
    message += hex;
    return message;
}

struct Vec3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

Vec3 operator-(Vec3 a, Vec3 b) {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}

Vec3 operator+(Vec3 a, Vec3 b) {
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}

Vec3 operator*(Vec3 v, float s) {
    return {v.x * s, v.y * s, v.z * s};
}

float dot(Vec3 a, Vec3 b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

Vec3 cross(Vec3 a, Vec3 b) {
    return {
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x
    };
}

Vec3 normalize(Vec3 v) {
    float len = std::sqrt(std::max(dot(v, v), 1e-12f));
    return {v.x / len, v.y / len, v.z / len};
}

struct DVec3 {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

DVec3 operator-(DVec3 a, DVec3 b) {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}

DVec3 operator+(DVec3 a, DVec3 b) {
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}

DVec3 operator*(DVec3 v, double s) {
    return {v.x * s, v.y * s, v.z * s};
}

DVec3 dvec3_from_vec3(Vec3 v) {
    return {static_cast<double>(v.x), static_cast<double>(v.y), static_cast<double>(v.z)};
}

double dot(DVec3 a, DVec3 b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

DVec3 cross(DVec3 a, DVec3 b) {
    return {
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x
    };
}

DVec3 normalize(DVec3 v) {
    double len = std::sqrt(std::max(dot(v, v), 1e-12));
    return {v.x / len, v.y / len, v.z / len};
}

DVec3 right_from_theta_d(double theta) {
    return {std::cos(theta), 0.0, std::sin(theta)};
}

DVec3 forward_from_theta_d(double theta) {
    return {std::sin(theta), 0.0, -std::cos(theta)};
}

DVec3 rotate_axis(DVec3 v, DVec3 axis, double radians) {
    axis = normalize(axis);
    double c = std::cos(radians);
    double s = std::sin(radians);
    return v * c + cross(axis, v) * s + axis * (dot(axis, v) * (1.0 - c));
}

void apply_track_cant(DVec3& right, DVec3& up, const DVec3& forward,
                      double cant_angle) {
    if (std::abs(cant_angle) <= 1e-9 || !std::isfinite(cant_angle)) return;
    const DVec3 axis = forward * -1.0;
    right = rotate_axis(right, axis, -cant_angle);
    up = rotate_axis(up, axis, -cant_angle);
}

void scene_track_surface_frame(const Canvas3DTrackPoint& point,
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

Canvas3DTrackPoint scene_sound3d_source_point(Canvas3DTrackPoint point,
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

Mat4 identity() {
    Mat4 r;
    for (int i = 0; i < 4; ++i) r.m[i][i] = 1.0f;
    return r;
}

Mat4 multiply(const Mat4& a, const Mat4& b) {
    Mat4 r;
    for (int row = 0; row < 4; ++row) {
        for (int col = 0; col < 4; ++col) {
            for (int k = 0; k < 4; ++k) r.m[row][col] += a.m[row][k] * b.m[k][col];
        }
    }
    return r;
}

Mat4 translation(float x, float y, float z) {
    Mat4 r = identity();
    r.m[3][0] = x;
    r.m[3][1] = y;
    r.m[3][2] = z;
    return r;
}

Mat4 rotation_x(float angle) {
    Mat4 r = identity();
    float c = std::cos(angle);
    float s = std::sin(angle);
    r.m[1][1] = c;
    r.m[1][2] = s;
    r.m[2][1] = -s;
    r.m[2][2] = c;
    return r;
}

Mat4 rotation_y(float angle) {
    Mat4 r = identity();
    float c = std::cos(angle);
    float s = std::sin(angle);
    r.m[0][0] = c;
    r.m[0][2] = -s;
    r.m[2][0] = s;
    r.m[2][2] = c;
    return r;
}

Mat4 look_to_bve(Vec3 eye, Vec3 forward, Vec3 up) {
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

Mat4 perspective_fov_lh_reverse_z(float fovy, float aspect, float zn, float zf) {
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

Vec4 transform_point_row(Vec3 p, const Mat4& m) {
    return {
        p.x * m.m[0][0] + p.y * m.m[1][0] + p.z * m.m[2][0] + m.m[3][0],
        p.x * m.m[0][1] + p.y * m.m[1][1] + p.z * m.m[2][1] + m.m[3][1],
        p.x * m.m[0][2] + p.y * m.m[1][2] + p.z * m.m[2][2] + m.m[3][2],
        p.x * m.m[0][3] + p.y * m.m[1][3] + p.z * m.m[2][3] + m.m[3][3],
    };
}

DVec3 transform_point_row(const double world[16], Vec3 p) {
    return {
        static_cast<double>(p.x) * world[0] + static_cast<double>(p.y) * world[4] +
            static_cast<double>(p.z) * world[8] + world[12],
        static_cast<double>(p.x) * world[1] + static_cast<double>(p.y) * world[5] +
            static_cast<double>(p.z) * world[9] + world[13],
        static_cast<double>(p.x) * world[2] + static_cast<double>(p.y) * world[6] +
            static_cast<double>(p.z) * world[10] + world[14],
    };
}

std::array<Vec3, 8> bounds_corners(Vec3 mn, Vec3 mx) {
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

bool scene_bounds_valid(Vec3 mn, Vec3 mx) {
    return std::isfinite(mn.x) && std::isfinite(mn.y) && std::isfinite(mn.z) &&
        std::isfinite(mx.x) && std::isfinite(mx.y) && std::isfinite(mx.z) &&
        mx.x >= mn.x && mx.y >= mn.y && mx.z >= mn.z;
}

Vec3 scene_bounds_min_or_sphere(Vec3 mn, Vec3 mx, Vec3 center, float radius) {
    return scene_bounds_valid(mn, mx) ? mn : Vec3{center.x - radius, center.y - radius, center.z - radius};
}

Vec3 scene_bounds_max_or_sphere(Vec3 mn, Vec3 mx, Vec3 center, float radius) {
    return scene_bounds_valid(mn, mx) ? mx : Vec3{center.x + radius, center.y + radius, center.z + radius};
}

struct GpuVertex {
    float px;
    float py;
    float pz;
    float nx;
    float ny;
    float nz;
    float u;
    float v;
};

struct SceneMarkerVertex {
    float px = 0.0f;
    float py = 0.0f;
    float pz = 0.0f;
    float u = 0.0f;
    float v = 0.0f;
    std::uint32_t color = 0xffffffffu;
    float use_texture = 0.0f;
    std::uint32_t marker_index = 0;
};

struct SceneMarkerConstants {
    Mat4 view_proj;
    float chunk_offset[4] = {};
};

struct SceneMarkerPickConstants {
    std::uint32_t marker_pick_id_base = 0;
    std::uint32_t padding[3] = {};
};

struct SceneMarkerIndexRange {
    MapMarkerVisualKind kind = MapMarkerVisualKind::Station;
    size_t marker_index = 0;
    std::uint32_t first = 0;
    std::uint32_t count = 0;
    std::uint32_t visible_first = 0;
    bool visible = false;
    bool label = false;
    DVec3 center;
    DVec3 right;
    DVec3 up;
};

struct SceneMarkerGeometrySpan {
    size_t vertex_first = 0;
    size_t vertex_count = 0;
    size_t range_first = 0;
    size_t range_count = 0;
};

struct SceneMarkerGpuLocation {
    size_t chunk_index = 0;
    size_t range_index = 0;
    size_t vertex_first = 0;
    size_t vertex_count = 0;
    size_t range_count = 0;
    bool valid = false;
};

struct SceneMarkerChunkGpu {
    ID3D11Buffer* vertex_buffer = nullptr;
    ID3D11Buffer* index_buffer = nullptr;
    ID3D11Buffer* pick_index_buffer = nullptr;
    std::vector<unsigned int> source_indices;
    std::vector<SceneMarkerIndexRange> ranges;
    DVec3 origin;
    double d_min = 0.0;
    double d_max = 0.0;
    UINT visible_index_count = 0;
    UINT visible_pick_index_count = 0;
};

struct SceneViewConstants {
    Mat4 view_proj;
    float material_color[4];
    float use_texture[4];
    float fog_color_density[4];
};

struct SceneOutlineConstants {
    float texel_radius[4];
    float color[4];
};

struct ScenePickConstants {
    float pick_color[4];
    float alpha_controls[4];
};

struct SceneInstanceData {
    float world0[4];
    float world1[4];
    float world2[4];
    float world3[4];
};

struct MeshPart {
    UINT start_index = 0;
    UINT index_count = 0;
    UINT material_index = 0;
};

struct GpuMaterial {
    ID3D11ShaderResourceView* texture = nullptr;
    float diffuse[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    bool has_texture = false;
    bool texture_has_alpha = false;
};

struct SceneTextureCacheEntry {
    ID3D11ShaderResourceView* texture = nullptr;
    bool has_alpha = false;
    bool failed = false;
    std::string error;
};

struct CpuMaterial {
    std::string texture_path;
    float diffuse[4] = {1.0f, 1.0f, 1.0f, 1.0f};
};

struct CpuModelData {
    std::string path;
    std::string scene_key;
    std::string shared_model_key;
    std::vector<GpuVertex> vertices;
    std::vector<unsigned int> indices;
    std::vector<MeshPart> parts;
    std::vector<CpuMaterial> materials;
    Vec3 bounds_min;
    Vec3 bounds_max;
    Vec3 center;
    float radius = 1.0f;
    bool ok = false;
    std::string error;
};

struct PutBetweenSourceTemplate {
    std::vector<size_t> vertex_slice_indices;
    std::vector<double> vertex_residual_x;
    std::vector<std::uint8_t> vertex_track1_side;
    std::vector<double> slice_source_z;
    bool ok = false;
    std::string error;
};

struct ScenePutBetweenDeformation {
    bool enabled = false;
    double distance = 0.0;
    int flag = 0;
    DVec3 origin;
    const Canvas3DTrackPath* own_track = nullptr;
    const Canvas3DTrackPath* track1 = nullptr;
    const Canvas3DTrackPath* track2 = nullptr;
};

struct SceneModelLoadRequest {
    std::string key;
    std::string source_path;
    ScenePutBetweenDeformation put_between;
};

struct ScenePutBetweenPreviewJob {
    std::uint64_t sequence = 0;
    size_t geometry_generation = 0;
    Canvas3DPlacementEditTarget target;
    SceneModelLoadRequest request;
};

struct ScenePutBetweenPreviewResult {
    std::uint64_t sequence = 0;
    size_t geometry_generation = 0;
    Canvas3DPlacementEditTarget target;
    std::shared_ptr<const CpuModelData> source;
    CpuModelData derived;
};

struct ScenePutBetweenPreparedSource {
    std::shared_ptr<CpuModelData> source;
    PutBetweenSourceTemplate source_template;
};

struct SceneModelGpu {
    enum class State { Pending, Ready, Failed };

    State state = State::Pending;
    ID3D11Buffer* vertex_buffer = nullptr;
    UINT vertex_capacity = 0;
    bool dynamic_vertices = false;
    std::string shared_model_key;
    ID3D11Buffer* index_buffer = nullptr;
    ID3D11Buffer* instance_buffer = nullptr;
    UINT instance_capacity = 0;
    UINT index_count = 0;
    std::vector<MeshPart> parts;
    std::vector<GpuMaterial> materials;
    Vec3 bounds_min;
    Vec3 bounds_max;
    Vec3 center;
    float radius = 1.0f;
    std::string error;
};

struct SceneCameraState {
    bool valid = false;
    DVec3 pos;
    float yaw = 0.0f;
    float pitch = 0.0f;
    double distance = 0.0;
};

struct SceneTrackChunkGpu {
    double d_min = 0.0;
    double d_max = 0.0;
    DVec3 origin;
    ID3D11Buffer* vertex_buffer = nullptr;
    ID3D11Buffer* index_buffer = nullptr;
    ID3D11Buffer* instance_buffer = nullptr;
    UINT instance_capacity = 0;
    UINT index_count = 0;
    std::vector<MeshPart> parts;
    std::vector<GpuMaterial> materials;
};

struct SceneMileagePickPoint {
    double distance = 0.0;
    DVec3 left;
    DVec3 right;
};

struct SceneInstance {
    std::string model_path;
    double distance = 0.0;
    int object_index = -1;
    double world[16] = {
        1.0, 0.0, 0.0, 0.0,
        0.0, 1.0, 0.0, 0.0,
        0.0, 0.0, 1.0, 0.0,
        0.0, 0.0, 0.0, 1.0
    };
};

struct SceneChunk {
    double d_min = 0.0;
    double d_max = 0.0;
    DVec3 origin;
    std::vector<SceneInstance> instances;
    std::vector<size_t> repeater_indices;
};

struct ScenePlacementInstanceLocation {
    size_t source_index = 0;
    size_t chunk_index = 0;
    size_t chunk_instance_index = 0;
};

struct SceneGizmoAxisProjection {
    bool valid = false;
    bool ray_drag_reliable = false;
    ImVec2 begin;
    ImVec2 end;
    ImVec2 direction;
    DVec3 world_direction;
    double parameter_sign = 1.0;
    double parameter_units_per_pixel = 0.0;
};

struct SceneGizmoHandle {
    DVec3 origin;
    std::array<DVec3, 3> axes{};
    std::array<bool, 3> enabled{{true, true, true}};
    std::array<SceneGizmoAxisProjection, 3> projection{};
};

enum class SceneGizmoTarget {
    None,
    Placement,
    RepeaterEndDistance,
};

struct SceneStructureEditState {
    bool active = false;
    bool show_gizmo = false;
    Canvas3DSceneEditKind kind = Canvas3DSceneEditKind::Structure;
    std::string edit_id;
    Canvas3DModelInstance baseline_instance;
    Canvas3DRepeaterSegment baseline_repeater;
    Canvas3DPlacementEditTarget current;
    Canvas3DPlacementEditTarget completed;
    std::string preview_model_key;
    std::uint64_t preview_sequence = 0;
    SceneGizmoHandle placement_gizmo;
    SceneGizmoHandle repeater_end_gizmo;
    SceneGizmoTarget hovered_gizmo = SceneGizmoTarget::None;
    SceneGizmoTarget dragging_gizmo = SceneGizmoTarget::None;
    Canvas3DSceneDragAxis hovered_axis = Canvas3DSceneDragAxis::None;
    Canvas3DSceneDragAxis dragging_axis = Canvas3DSceneDragAxis::None;
    DVec3 drag_axis_origin;
    DVec3 drag_axis_direction;
    ImVec2 drag_start_mouse;
    ImVec2 drag_screen_direction;
    double drag_start_value = 0.0;
    double drag_start_axis_parameter = 0.0;
    double drag_parameter_units_per_pixel = 0.0;
    double drag_parameter_sign = 1.0;
    bool drag_uses_ray = false;
};

struct StructurePlacementFrame {
    DVec3 origin;
    DVec3 model_right;
    DVec3 model_up;
    DVec3 model_forward;
    std::array<DVec3, 3> parameter_axes{};
};

struct SceneScreenBounds {
    ImVec2 screen_min = ImVec2(0.0f, 0.0f);
    ImVec2 screen_max = ImVec2(0.0f, 0.0f);
};

struct SceneVisibleInstanceRef {
    const std::string* model_path = nullptr;
    size_t instance_index = 0;
    ImVec2 screen_min = ImVec2(0.0f, 0.0f);
    ImVec2 screen_max = ImVec2(0.0f, 0.0f);
};

struct SceneHighlightInstance {
    std::string model_path;
    SceneInstanceData instance_data = {};
    ImVec2 screen_min = ImVec2(0.0f, 0.0f);
    ImVec2 screen_max = ImVec2(0.0f, 0.0f);
};

struct SceneHighlightBatch {
    int object_index = -1;
    std::vector<SceneHighlightInstance> instances;
    ImVec2 screen_min = ImVec2(0.0f, 0.0f);
    ImVec2 screen_max = ImVec2(0.0f, 0.0f);

    void clear() {
        object_index = -1;
        instances.clear();
        screen_min = ImVec2(0.0f, 0.0f);
        screen_max = ImVec2(0.0f, 0.0f);
    }

    bool valid() const {
        return object_index >= 0 && !instances.empty();
    }
};

struct SceneRepeaterIndexRange {
    long long first = 0;
    long long last = -1;
};

using MlApiVersionFn = unsigned int (*)();
using MlLoadModelFn = int (*)(const char*, MlMeshData*);
using MlFreeModelFn = void (*)(MlMeshData*);
using MlGetLastErrorFn = const char* (*)();

int scene_tilt_flags(double tilt) {
    return kme::truncating_int_or_zero(tilt);
}

bool scene_material_is_translucent(const GpuMaterial* material) {
    return material && material->diffuse[3] < k_material_opaque_alpha_threshold;
}

bool scene_material_uses_alpha_mask(const GpuMaterial* material) {
    return material && material->has_texture && material->texture_has_alpha && !scene_material_is_translucent(material);
}

bool scene_repeater_has_interval(const Canvas3DRepeaterSegment& repeater) {
    return repeater.interval > 1e-9 && std::isfinite(repeater.interval);
}

double scene_repeater_index_epsilon(const Canvas3DRepeaterSegment& repeater) {
    if (!scene_repeater_has_interval(repeater)) return k_scene_repeater_distance_epsilon;
    return std::min(k_scene_repeater_distance_epsilon, repeater.interval * 0.25);
}

bool scene_repeater_index_range(const Canvas3DRepeaterSegment& repeater,
                                double range_min,
                                double range_max,
                                SceneRepeaterIndexRange& out) {
    out = {};
    if (!scene_repeater_has_interval(repeater) || repeater.end_distance < repeater.begin_distance) return false;
    const double begin = std::max(range_min, repeater.begin_distance);
    const double end = std::min(range_max, repeater.end_distance);
    if (end < begin - k_scene_repeater_distance_epsilon) return false;

    const double eps = scene_repeater_index_epsilon(repeater);
    const double first_index_d = std::ceil((begin - repeater.begin_distance - eps) / repeater.interval);
    const double last_visible_index_d = std::floor((end - repeater.begin_distance + eps) / repeater.interval);
    const double last_before_end_index_d =
        std::ceil((repeater.end_distance - repeater.begin_distance - eps) / repeater.interval) - 1.0;
    if (!std::isfinite(first_index_d) ||
        !std::isfinite(last_visible_index_d) ||
        !std::isfinite(last_before_end_index_d)) {
        return false;
    }

    long long first_index = 0;
    if (first_index_d > 0.0) {
        if (first_index_d > static_cast<double>(std::numeric_limits<long long>::max())) return false;
        first_index = static_cast<long long>(first_index_d);
    }

    const double last_index_d = std::min(last_visible_index_d, last_before_end_index_d);
    if (last_index_d < 0.0) return false;
    if (last_index_d > static_cast<double>(std::numeric_limits<long long>::max())) {
        out.first = first_index;
        out.last = std::numeric_limits<long long>::max();
        return out.last >= out.first;
    }

    out.first = first_index;
    out.last = static_cast<long long>(last_index_d);
    return out.last >= out.first;
}

size_t scene_repeater_index_count(const SceneRepeaterIndexRange& range) {
    if (range.last < range.first) return 0;
    double count = static_cast<double>(range.last - range.first) + 1.0;
    return static_cast<size_t>(std::min<double>(count, static_cast<double>(k_scene_repeater_instance_limit)));
}

size_t scene_repeater_instance_count(const Canvas3DRepeaterSegment& repeater) {
    if (repeater.model_paths.empty() || repeater.end_distance < repeater.begin_distance) return 0;
    if (!scene_repeater_has_interval(repeater)) return 1;

    SceneRepeaterIndexRange range;
    if (!scene_repeater_index_range(repeater, repeater.begin_distance, repeater.end_distance, range)) return 0;
    return scene_repeater_index_count(range);
}

bool scene_repeater_last_instance(const Canvas3DRepeaterSegment& repeater,
                                  double& distance,
                                  size_t& model_index) {
    if (repeater.model_paths.empty() || repeater.end_distance < repeater.begin_distance) return false;
    if (!scene_repeater_has_interval(repeater)) {
        distance = repeater.begin_distance;
        model_index = 0;
        return true;
    }

    SceneRepeaterIndexRange range;
    if (!scene_repeater_index_range(repeater, repeater.begin_distance, repeater.end_distance, range)) {
        return false;
    }
    distance = repeater.begin_distance + static_cast<double>(range.last) * repeater.interval;
    model_index = static_cast<size_t>(range.last) % repeater.model_paths.size();
    return true;
}

bool scene_repeater_render_distance_span(const Canvas3DRepeaterSegment& repeater,
                                         double& first_distance,
                                         double& last_distance) {
    size_t last_model_index = 0;
    if (!scene_repeater_last_instance(repeater, last_distance, last_model_index)) return false;
    first_distance = repeater.begin_distance;
    return true;
}

void store_world(double out[16], DVec3 right, DVec3 up, DVec3 forward, DVec3 origin) {
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

void apply_euler(DVec3& right, DVec3& up, DVec3& forward,
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

std::string scene_model_key(std::string key) {
    return ascii_lower(trim_ascii(key));
}

struct SceneTrackBufferView {
    const double* data = nullptr;
    size_t rows = 0;
    size_t cols = 0;

    double at(size_t row, size_t col) const {
        return data[row * cols + col];
    }

    bool empty() const {
        return rows == 0 || cols == 0;
    }
};

std::optional<SceneTrackBufferView> scene_track_buffer_view(const Matrix& points) {
    if (points.rows == 0) return SceneTrackBufferView{nullptr, 0, points.cols};
    if (points.cols == 0 || points.rows > std::numeric_limits<size_t>::max() / points.cols) {
        return std::nullopt;
    }
    if (points.data.size() < points.rows * points.cols) return std::nullopt;
    return SceneTrackBufferView{points.data.data(), points.rows, points.cols};
}

std::optional<SceneTrackBufferView> scene_track_buffer_view(KvDoubleBuffer points) {
    constexpr std::uint64_t max_size = static_cast<std::uint64_t>(std::numeric_limits<size_t>::max());
    if (points.rows > max_size || points.cols > max_size) return std::nullopt;
    const size_t rows = static_cast<size_t>(points.rows);
    const size_t cols = static_cast<size_t>(points.cols);
    if (rows == 0) return SceneTrackBufferView{nullptr, 0, cols};
    if (!points.data || cols == 0 || rows > std::numeric_limits<size_t>::max() / cols) {
        return std::nullopt;
    }
    return SceneTrackBufferView{points.data, rows, cols};
}

Canvas3DTrackPoint scene_track_row_point(const SceneTrackBufferView& points, size_t row,
                                         bool has_theta_column) {
    constexpr double default_gauge = 1067.0;
    auto cant_angle = [default_gauge](double cant, double gauge) {
        if (!std::isfinite(cant)) return 0.0;
        double effective_gauge = std::abs(gauge) > 1e-9 && std::isfinite(gauge) ? std::abs(gauge) : default_gauge;
        return std::asin(std::clamp(cant / effective_gauge, -1.0, 1.0));
    };

    Canvas3DTrackPoint p;
    p.distance = points.at(row, 0);
    p.x = points.at(row, 2);
    p.z = -points.at(row, 1);
    p.y = points.cols > 3 ? points.at(row, 3) : 0.0;
    p.theta = has_theta_column && points.cols > 4
        ? points.at(row, 4)
        : track_buffer_tangent(points, row);
    p.gradient = has_theta_column && points.cols > 6 ? points.at(row, 6) : 0.0;
    if (has_theta_column) {
        double cant = points.cols > 8 ? points.at(row, 8) : 0.0;
        double gauge = points.cols > 10 ? points.at(row, 10) : default_gauge;
        p.cant_angle = cant_angle(cant, gauge);
    } else {
        double cant = points.cols > 5 ? points.at(row, 5) : 0.0;
        double gauge = points.cols > 7 ? points.at(row, 7) : default_gauge;
        p.cant_angle = cant_angle(cant, gauge);
    }
    return p;
}

std::vector<std::string> scene_split_key_list(const std::string& text) {
    std::vector<std::string> keys;
    std::string current;
    for (char ch : text) {
        if (ch == ',' || ch == ';' || std::isspace(static_cast<unsigned char>(ch))) {
            std::string key = trim_ascii(current);
            if (!key.empty()) keys.push_back(key);
            current.clear();
        } else {
            current.push_back(ch);
        }
    }
    std::string key = trim_ascii(current);
    if (!key.empty()) keys.push_back(key);
    return keys;
}

std::optional<Canvas3DTrackPoint> scene_sample_track_path_points(const Canvas3DTrackPath& path,
                                                                 double distance) {
    if (path.points.empty()) return std::nullopt;
    if (distance <= path.points.front().distance) return path.points.front();
    if (distance >= path.points.back().distance) return path.points.back();
    size_t lo = 0;
    size_t hi = path.points.size();
    while (lo < hi) {
        size_t mid = (lo + hi) / 2;
        if (path.points[mid].distance < distance) lo = mid + 1;
        else hi = mid;
    }
    size_t a_index = lo == 0 ? 0 : lo - 1;
    size_t b_index = std::min(lo, path.points.size() - 1);
    const Canvas3DTrackPoint& a = path.points[a_index];
    const Canvas3DTrackPoint& b = path.points[b_index];
    double span = b.distance - a.distance;
    double t = std::abs(span) < 1e-9 ? 0.0 : std::clamp((distance - a.distance) / span, 0.0, 1.0);
    Canvas3DTrackPoint out;
    out.distance = distance;
    out.x = a.x + (b.x - a.x) * t;
    out.y = a.y + (b.y - a.y) * t;
    out.z = a.z + (b.z - a.z) * t;
    out.theta = angle_lerp(a.theta, b.theta, t);
    out.gradient = a.gradient + (b.gradient - a.gradient) * t;
    out.cant_angle = a.cant_angle + (b.cant_angle - a.cant_angle) * t;
    return out;
}

std::string normalized_texture_cache_key(const std::string& path) {
    try {
        std::filesystem::path normalized = utf8_to_wide(path);
        std::error_code ec;
        if (normalized.is_relative()) {
            std::filesystem::path absolute = std::filesystem::absolute(normalized, ec);
            if (!ec) normalized = std::move(absolute);
        }
        std::string key = wide_to_utf8(normalized.lexically_normal().wstring());
        return key.empty() ? path : key;
    } catch (...) {
        return path;
    }
}

size_t scene_model_worker_count_for(size_t source_count) {
    if (source_count == 0) return 0;
    size_t available = std::thread::hardware_concurrency();
    if (available == 0) available = 4;
    if (available > 2) --available;
    return std::max<size_t>(1, std::min({source_count, available, k_scene_model_max_workers}));
}

const Canvas3DTrackPath* scene_own_track_path(const Canvas3DScene& scene) {
    for (const Canvas3DTrackPath& path : scene.tracks) {
        if (path.key == "own" || path.key.empty() || path.key == "0") return &path;
    }
    return scene.tracks.empty() ? nullptr : &scene.tracks.front();
}

const Canvas3DTrackPath* scene_other_track_path_for_key(const Canvas3DScene& scene,
                                                        const std::string& normalized_key) {
    const Canvas3DTrackPath* own = scene_own_track_path(scene);
    for (const Canvas3DTrackPath& path : scene.tracks) {
        if (&path == own) continue;
        if (normalize_track_lookup_key(path.key) == normalized_key) return &path;
    }
    return nullptr;
}

const Canvas3DTrackPath* scene_placement_track_path_for_key(const Canvas3DScene& scene,
                                                            const std::string& key) {
    const std::string normalized_key = normalize_track_lookup_key(key);
    if (is_own_track_placement_key(normalized_key)) return scene_own_track_path(scene);
    if (const Canvas3DTrackPath* other =
            scene_other_track_path_for_key(scene, normalized_key)) {
        return other;
    }
    return scene_own_track_path(scene);
}

void append_scene_model_key_field(std::string& key, const std::string& value) {
    const size_t size = value.size();
    key.append(reinterpret_cast<const char*>(&size), sizeof(size));
    key.append(value);
}

std::string scene_model_key_for_instance(const Canvas3DModelInstance& instance,
                                         size_t geometry_generation) {
    if (!instance.put_between) return instance.model_path;

    std::string key(1, '\x1f');
    key.append("putbetween");
    append_scene_model_key_field(key, instance.model_path);
    append_scene_model_key_field(key, normalize_track_lookup_key(instance.put_between_track_key1));
    append_scene_model_key_field(key, normalize_track_lookup_key(instance.put_between_track_key2));
    key.append(reinterpret_cast<const char*>(&instance.distance), sizeof(instance.distance));
    const int flag = instance.put_between_flag & 1;
    key.append(reinterpret_cast<const char*>(&flag), sizeof(flag));
    key.append(reinterpret_cast<const char*>(&geometry_generation), sizeof(geometry_generation));
    return key;
}

std::string scene_put_between_preview_model_key(const std::string& edit_id,
                                                size_t geometry_generation) {
    std::string key(1, '\x1f');
    key.append("putbetween-preview");
    append_scene_model_key_field(key, edit_id);
    key.append(reinterpret_cast<const char*>(&geometry_generation),
               sizeof(geometry_generation));
    return key;
}

bool update_cpu_model_bounds(CpuModelData& model) {
    if (model.vertices.empty()) return false;

    DVec3 bounds_min{
        std::numeric_limits<double>::infinity(),
        std::numeric_limits<double>::infinity(),
        std::numeric_limits<double>::infinity()
    };
    DVec3 bounds_max{
        -std::numeric_limits<double>::infinity(),
        -std::numeric_limits<double>::infinity(),
        -std::numeric_limits<double>::infinity()
    };
    for (const GpuVertex& vertex : model.vertices) {
        bounds_min.x = std::min(bounds_min.x, static_cast<double>(vertex.px));
        bounds_min.y = std::min(bounds_min.y, static_cast<double>(vertex.py));
        bounds_min.z = std::min(bounds_min.z, static_cast<double>(vertex.pz));
        bounds_max.x = std::max(bounds_max.x, static_cast<double>(vertex.px));
        bounds_max.y = std::max(bounds_max.y, static_cast<double>(vertex.py));
        bounds_max.z = std::max(bounds_max.z, static_cast<double>(vertex.pz));
    }
    const DVec3 center = (bounds_min + bounds_max) * 0.5;
    double radius_squared = 0.0;
    for (const GpuVertex& vertex : model.vertices) {
        const DVec3 offset{
            static_cast<double>(vertex.px) - center.x,
            static_cast<double>(vertex.py) - center.y,
            static_cast<double>(vertex.pz) - center.z
        };
        radius_squared = std::max(radius_squared, dot(offset, offset));
    }
    if (!std::isfinite(radius_squared)) return false;

    model.bounds_min = {
        static_cast<float>(bounds_min.x),
        static_cast<float>(bounds_min.y),
        static_cast<float>(bounds_min.z)
    };
    model.bounds_max = {
        static_cast<float>(bounds_max.x),
        static_cast<float>(bounds_max.y),
        static_cast<float>(bounds_max.z)
    };
    model.center = {
        static_cast<float>(center.x),
        static_cast<float>(center.y),
        static_cast<float>(center.z)
    };
    model.radius = std::max(static_cast<float>(std::sqrt(radius_squared)), 0.001f);
    return true;
}

PutBetweenSourceTemplate prepare_put_between_source(const CpuModelData& source) {
    PutBetweenSourceTemplate result;
    if (!source.ok) {
        result.error = source.error;
        return result;
    }
    if (source.vertices.empty()) {
        result.error = "PutBetween model contains no vertices";
        return result;
    }

    double average_x = 0.0;
    for (const GpuVertex& vertex : source.vertices) {
        if (!std::isfinite(vertex.px) || !std::isfinite(vertex.py) ||
            !std::isfinite(vertex.pz)) {
            result.error = "PutBetween model contains invalid vertex coordinates";
            return result;
        }
        average_x += static_cast<double>(vertex.px);
    }
    average_x /= static_cast<double>(source.vertices.size());
    if (!std::isfinite(average_x)) {
        result.error = "PutBetween model contains invalid vertex coordinates";
        return result;
    }

    // BVE PutBetween models extend from x=0 in either the positive or negative x direction.
    // The sign determines which end of the mesh is anchored to trackKey1.
    const double orientation = average_x < 0.0 ? -1.0 : 1.0;
    double min_oriented_x = std::numeric_limits<double>::infinity();
    double max_oriented_x = -std::numeric_limits<double>::infinity();
    for (const GpuVertex& vertex : source.vertices) {
        const double oriented_x = static_cast<double>(vertex.px) * orientation;
        min_oriented_x = std::min(min_oriented_x, oriented_x);
        max_oriented_x = std::max(max_oriented_x, oriented_x);
    }
    const double split_x = average_x * orientation;
    const double opposite_edge_offset = (min_oriented_x + max_oriented_x) * orientation;

    result.vertex_slice_indices.resize(source.vertices.size());
    result.vertex_residual_x.resize(source.vertices.size());
    result.vertex_track1_side.resize(source.vertices.size());
    std::map<float, size_t> slice_indices;
    for (size_t index = 0; index < source.vertices.size(); ++index) {
        const GpuVertex& vertex = source.vertices[index];
        auto [slice, inserted] = slice_indices.emplace(vertex.pz, slice_indices.size());
        if (inserted) result.slice_source_z.push_back(static_cast<double>(vertex.pz));
        const bool track1_side =
            static_cast<double>(vertex.px) * orientation < split_x;
        result.vertex_slice_indices[index] = slice->second;
        result.vertex_track1_side[index] = track1_side ? 1u : 0u;
        result.vertex_residual_x[index] = track1_side
            ? static_cast<double>(vertex.px)
            : static_cast<double>(vertex.px) - opposite_edge_offset;
    }
    result.ok = true;
    return result;
}

CpuModelData derive_put_between_model(const CpuModelData& source,
                                      const PutBetweenSourceTemplate& source_template,
                                      const SceneModelLoadRequest& request) {
#ifndef NDEBUG
    int remaining = g_debug_put_between_derive_throw_countdown.load(std::memory_order_relaxed);
    while (remaining > 0 &&
           !g_debug_put_between_derive_throw_countdown.compare_exchange_weak(
               remaining, remaining - 1, std::memory_order_relaxed)) {
    }
    if (remaining == 1) {
        throw std::runtime_error("debug injected PutBetween derivation failure");
    }
#endif
    CpuModelData result;
    result.path = request.source_path;
    result.scene_key = request.key;
    result.shared_model_key = request.source_path;
    if (!source.ok || !source_template.ok) {
        result.error = source.ok ? source_template.error : source.error;
        return result;
    }
    if (!request.put_between.own_track || !request.put_between.track1 ||
        !request.put_between.track2) {
        result.error = "PutBetween references an unavailable track";
        return result;
    }
    if (source_template.vertex_slice_indices.size() != source.vertices.size() ||
        source_template.vertex_residual_x.size() != source.vertices.size() ||
        source_template.vertex_track1_side.size() != source.vertices.size()) {
        result.error = "PutBetween source template does not match the model";
        return result;
    }

    struct SliceFrame {
        DVec3 track1_anchor;
        DVec3 track2_anchor;
        DVec3 right;
        DVec3 up;
        DVec3 forward;
    };
    std::vector<SliceFrame> frames(source_template.slice_source_z.size());
    for (size_t index = 0; index < source_template.slice_source_z.size(); ++index) {
        const double vertex_distance =
            request.put_between.distance - source_template.slice_source_z[index];
        auto own_point = scene_sample_track_path_points(
            *request.put_between.own_track, vertex_distance);
        auto track1_point = scene_sample_track_path_points(
            *request.put_between.track1, vertex_distance);
        auto track2_point = scene_sample_track_path_points(
            *request.put_between.track2, vertex_distance);
        if (!own_point || !track1_point || !track2_point) {
            result.error = "PutBetween could not sample track geometry";
            return result;
        }

        SliceFrame& frame = frames[index];
        frame.track1_anchor = {track1_point->x, track1_point->y, track1_point->z};
        frame.track2_anchor = {track2_point->x, track2_point->y, track2_point->z};
        if ((request.put_between.flag & 1) != 0) {
            frame.track1_anchor.y = own_point->y;
            frame.track2_anchor.y = own_point->y;
        }
        const double gradient = std::isfinite(own_point->gradient)
            ? own_point->gradient / 1000.0 : 0.0;
        frame.right = right_from_theta_d(own_point->theta);
        frame.forward = normalize(DVec3{
            std::sin(own_point->theta), gradient, -std::cos(own_point->theta)});
        frame.up = normalize(cross(frame.right, frame.forward));
    }

    const double max_float = static_cast<double>(std::numeric_limits<float>::max());
    result.vertices = source.vertices;
    for (size_t index = 0; index < result.vertices.size(); ++index) {
        GpuVertex& vertex = result.vertices[index];
        const GpuVertex& source_vertex = source.vertices[index];
        const SliceFrame& frame = frames[source_template.vertex_slice_indices[index]];
        const DVec3& anchor = source_template.vertex_track1_side[index]
            ? frame.track1_anchor : frame.track2_anchor;
        const DVec3 world_position = anchor +
            frame.right * source_template.vertex_residual_x[index] +
            frame.up * static_cast<double>(source_vertex.py);
        const DVec3 local_position = world_position - request.put_between.origin;
        if (!std::isfinite(local_position.x) || !std::isfinite(local_position.y) ||
            !std::isfinite(local_position.z) || std::abs(local_position.x) > max_float ||
            std::abs(local_position.y) > max_float || std::abs(local_position.z) > max_float) {
            result.vertices.clear();
            result.error = "PutBetween produced invalid vertex coordinates";
            return result;
        }
        vertex.px = static_cast<float>(local_position.x);
        vertex.py = static_cast<float>(local_position.y);
        vertex.pz = static_cast<float>(local_position.z);

        const DVec3 source_normal{source_vertex.nx, source_vertex.ny, source_vertex.nz};
        if (dot(source_normal, source_normal) > 1e-12) {
            const DVec3 world_normal = normalize(
                frame.right * source_normal.x + frame.up * source_normal.y +
                frame.forward * -source_normal.z);
            vertex.nx = static_cast<float>(world_normal.x);
            vertex.ny = static_cast<float>(world_normal.y);
            vertex.nz = static_cast<float>(world_normal.z);
        }
    }

    if (!update_cpu_model_bounds(result)) {
        result.vertices.clear();
        result.error = "PutBetween could not calculate deformed model bounds";
        return result;
    }
    result.ok = true;
    return result;
}

void append_scene_route_value_event(
    std::vector<Canvas3DSceneRouteValueEvent>& events,
    const TrackEvent& source,
    double& current_value) {
    if (!std::isfinite(source.distance) ||
        (!source.value_number && source.flag != "bt" && source.flag != "i")) {
        return;
    }

    Canvas3DSceneRouteValueEvent event;
    event.distance = source.distance;
    event.previous_value = current_value;
    if (source.value_number && std::isfinite(source.number)) current_value = source.number;
    event.value = current_value;
    if (source.flag == "bt") {
        event.kind = Canvas3DSceneRouteEventKind::BeginTransition;
    } else if (source.flag == "i") {
        event.kind = Canvas3DSceneRouteEventKind::Interpolate;
    }
    events.push_back(event);
}

void populate_canvas3d_scene_route_values(Canvas3DSceneRouteInfo& route_info,
                                          const MapModel& model) {
    size_t radius_count = 0;
    size_t cant_count = 0;
    size_t gradient_count = 0;
    for (const TrackEvent& event : model.own_events) {
        if (event.key == "radius") ++radius_count;
        else if (event.key == "cant") ++cant_count;
        else if (event.key == "gradient") ++gradient_count;
    }
    route_info.radius_events.reserve(radius_count);
    route_info.cant_events.reserve(cant_count);
    route_info.gradient_events.reserve(gradient_count);

    double radius_value = 0.0;
    double cant_value = 0.0;
    double gradient_value = 0.0;
    // maploader emits own_events in stable distance order; filtering preserves that query order.
    for (const TrackEvent& source : model.own_events) {
        if (source.key == "radius") {
            append_scene_route_value_event(route_info.radius_events, source, radius_value);
        } else if (source.key == "cant") {
            append_scene_route_value_event(route_info.cant_events, source, cant_value);
        } else if (source.key == "gradient") {
            append_scene_route_value_event(route_info.gradient_events, source, gradient_value);
        }
    }
}

void populate_canvas3d_scene_route_stations(Canvas3DSceneRouteInfo& route_info,
                                            const MapModel& model) {
    route_info.stations.clear();
    route_info.stations.reserve(model.stations.size());
    for (const Station& station : model.stations) {
        if (!std::isfinite(station.distance)) continue;
        Canvas3DSceneRouteStation route_station;
        route_station.distance = station.distance;
        route_station.name = station.name.empty() ? station.key : station.name;
        route_info.stations.push_back(std::move(route_station));
    }
    if (!std::is_sorted(route_info.stations.begin(), route_info.stations.end(),
                        [](const Canvas3DSceneRouteStation& a,
                           const Canvas3DSceneRouteStation& b) {
                            return a.distance < b.distance;
                        })) {
        std::stable_sort(route_info.stations.begin(), route_info.stations.end(),
                         [](const Canvas3DSceneRouteStation& a,
                            const Canvas3DSceneRouteStation& b) {
                             return a.distance < b.distance;
                         });
    }
}

std::optional<size_t> canvas3d_scene_signal_speed_index(
    const std::string& value) {
    if (value.empty()) return std::nullopt;
    char* end = nullptr;
    const double parsed = std::strtod(value.c_str(), &end);
    if (end == value.c_str() || !end || *end != '\0' ||
        !std::isfinite(parsed) || parsed < 0.0 || std::floor(parsed) != parsed ||
        parsed > static_cast<double>(std::numeric_limits<size_t>::max())) {
        return std::nullopt;
    }
    return static_cast<size_t>(parsed);
}

std::string canvas3d_scene_selected_signal_speeds(
    const std::vector<std::string>& signal_indices,
    const std::vector<std::string>& speed_limits) {
    if (signal_indices.empty() || speed_limits.empty()) return {};
    std::vector<std::string> selected;
    selected.reserve(signal_indices.size());
    for (const std::string& value : signal_indices) {
        const std::optional<size_t> index =
            canvas3d_scene_signal_speed_index(value);
        selected.push_back(
            index && *index < speed_limits.size() &&
                    !speed_limits[*index].empty()
                ? speed_limits[*index]
                : "null");
    }
    return join_table_values(selected, " | ");
}

void populate_canvas3d_scene_speed_limits(Canvas3DSceneRouteInfo& route_info,
                                          const MapModel& model) {
    route_info.speed_limit_events.clear();
    route_info.speed_limit_events.reserve(model.speedlimits.size());
    for (const SpeedLimit& input : model.speedlimits) {
        route_info.speed_limit_events.push_back(
            {input.distance, input.has_speed, input.speed, input.order});
    }
    std::stable_sort(
        route_info.speed_limit_events.begin(), route_info.speed_limit_events.end(),
        [](const Canvas3DSceneSpeedLimitEvent& left,
           const Canvas3DSceneSpeedLimitEvent& right) {
            if (left.distance != right.distance) return left.distance < right.distance;
            return left.order < right.order;
        });
}

void populate_canvas3d_scene_section_signals(Canvas3DSceneRouteInfo& route_info,
                                             const MapModel& model) {
    enum class SectionSignalStateKind { SpeedLimits, SectionBegin };
    struct SectionSignalStateSource {
        double distance = 0.0;
        int order = 0;
        SectionSignalStateKind kind = SectionSignalStateKind::SpeedLimits;
        const TableRow* row = nullptr;
    };
    std::vector<SectionSignalStateSource> section_signal_sources;
    section_signal_sources.reserve(
        model.section_speed_limits.size() + model.section_begins.size());
    auto append_section_signal_sources = [&](const std::vector<TableRow>& rows,
                                             SectionSignalStateKind kind) {
        for (const TableRow& row : rows) {
            section_signal_sources.push_back({
                table_cell_number(row, "distance"),
                static_cast<int>(table_cell_number(row, "order")),
                kind,
                &row});
        }
    };
    append_section_signal_sources(
        model.section_speed_limits, SectionSignalStateKind::SpeedLimits);
    append_section_signal_sources(
        model.section_begins, SectionSignalStateKind::SectionBegin);
    std::stable_sort(
        section_signal_sources.begin(), section_signal_sources.end(),
        [](const SectionSignalStateSource& left,
           const SectionSignalStateSource& right) {
            if (left.distance != right.distance) {
                return left.distance < right.distance;
            }
            return left.order < right.order;
        });
    route_info.section_signal_events.clear();
    route_info.section_signal_events.reserve(section_signal_sources.size());
    std::vector<std::string> speed_limits;
    std::vector<std::string> signal_indices;
    for (const SectionSignalStateSource& source : section_signal_sources) {
        if (source.kind == SectionSignalStateKind::SpeedLimits) {
            speed_limits = section_row_values(*source.row);
        } else {
            signal_indices = section_row_values(*source.row);
        }
        std::string selected = canvas3d_scene_selected_signal_speeds(
            signal_indices, speed_limits);
        if (!route_info.section_signal_events.empty() &&
            route_info.section_signal_events.back().values == selected) {
            continue;
        }
        route_info.section_signal_events.push_back(
            {source.distance, source.order, std::move(selected)});
    }
}

void populate_canvas3d_scene_route_info(Canvas3DScene& scene, const MapModel& model) {
    Canvas3DSceneRouteInfo route_info;
    populate_canvas3d_scene_route_values(route_info, model);
    populate_canvas3d_scene_speed_limits(route_info, model);
    populate_canvas3d_scene_section_signals(route_info, model);
    populate_canvas3d_scene_route_stations(route_info, model);
    scene.route_info = std::move(route_info);
}

void format_scene_route_number(char* output, size_t output_size, double value);

double canvas3d_scene_route_value_at(
    const std::vector<Canvas3DSceneRouteValueEvent>& events,
    double distance) {
    const auto next = std::upper_bound(
        events.begin(), events.end(), distance,
        [](double value, const Canvas3DSceneRouteValueEvent& event) {
            return value < event.distance;
        });
    return next == events.begin() ? 0.0 : (next - 1)->value;
}

std::string canvas3d_scene_curve_marker_label(
    const Canvas3DSceneRouteInfo& route_info,
    const TrackEvent& event) {
    char radius_text[64] = {};
    char cant_text[64] = {};
    format_scene_route_number(radius_text, sizeof(radius_text), std::abs(event.number));
    const double cant =
        canvas3d_scene_route_value_at(route_info.cant_events, event.distance);
    format_scene_route_number(cant_text, sizeof(cant_text), cant);
    char label[192] = {};
    std::snprintf(label, sizeof(label), "R %s m  %s %s",
                  radius_text, cant_text, event.number < 0.0 ? u8"←" : u8"→");
    return label;
}

std::string canvas3d_scene_gradient_marker_label(double gradient) {
    char gradient_text[64] = {};
    format_scene_route_number(gradient_text, sizeof(gradient_text), std::abs(gradient));
    char label[128] = {};
    std::snprintf(label, sizeof(label), "%s %s‰",
                  gradient > 0.0 ? u8"↗" : u8"↘", gradient_text);
    return label;
}

std::string canvas3d_scene_table_marker_label(
    const TableRow& row,
    std::initializer_list<const char*> keys,
    const char* separator = " ",
    const char* empty_value = nullptr) {
    std::string label;
    size_t key_index = 0;
    for (const char* key : keys) {
        if (key_index++ != 0) label += separator;
        const std::string& value = table_cell(row, key);
        if (value.empty() && empty_value) {
            label += empty_value;
        } else {
            label += value;
        }
    }
    return label;
}

void sort_canvas3d_scene_markers(std::vector<Canvas3DSceneMarker>& markers) {
    std::stable_sort(markers.begin(), markers.end(),
                     [](const Canvas3DSceneMarker& a,
                        const Canvas3DSceneMarker& b) {
                         if (a.track_point.distance != b.track_point.distance) {
                             return a.track_point.distance < b.track_point.distance;
                         }
                         return normalize_track_lookup_key(a.track_key) <
                             normalize_track_lookup_key(b.track_key);
                     });
}

void populate_canvas3d_scene_markers(Canvas3DScene& scene, const MapModel& model) {
    scene.markers.clear();
    const Canvas3DTrackPath* own_track = scene_own_track_path(scene);
    if (!own_track || own_track->points.empty()) return;

    const size_t estimated_count =
        model.station_positions.size() + model.curve_rows.size() +
        model.gradient_rows.size() + model.other_track_changes.size() +
        model.speedlimits.size() + model.section_begins.size() +
        model.beacons.size() + model.pretrains.size() +
        model.irregularities.size() + model.map_sounds.size() +
        model.map_sound_3d.size() + model.rolling_noises.size() +
        model.flange_noises.size() + model.joint_noises.size() +
        model.backgrounds.size() + model.adhesions.size() +
        model.cab_illuminance.size() + model.fogs.size() +
        model.draw_distances.size();
    scene.markers.reserve(estimated_count);

    auto append_marker = [&](MapMarkerVisualKind kind,
                             double distance,
                             std::string label = {},
                             MapMarkerIconVariant icon_variant =
                                 MapMarkerIconVariant::Default,
                             Canvas3DSceneMarkerListKind list_kind =
                                 Canvas3DSceneMarkerListKind::None,
                             std::string row_kind = {},
                             std::optional<size_t> row_index = std::nullopt,
                             std::string edit_id = {},
                             bool unpaired_transition = false,
                             double source_x = 0.0,
                             double source_y = 0.0) {
        if (!std::isfinite(distance)) return;
        std::optional<Canvas3DTrackPoint> point =
            scene_sample_track_path_points(*own_track, distance);
        if (!point) return;
        Canvas3DSceneMarker marker;
        marker.kind = kind;
        marker.list_kind = list_kind;
        marker.icon_variant = icon_variant;
        marker.track_point = kind == MapMarkerVisualKind::MapSound3D
            ? scene_sound3d_source_point(*point, source_x, source_y)
            : *point;
        marker.label = std::move(label);
        marker.row_kind = std::move(row_kind);
        marker.row_index = row_index;
        marker.edit_id = std::move(edit_id);
        marker.unpaired_transition = unpaired_transition;
        scene.markers.push_back(std::move(marker));
    };

    for (const Station& station : model.station_positions) {
        append_marker(MapMarkerVisualKind::Station, station.distance,
                      station.name.empty() ? station.key : station.name,
                      MapMarkerIconVariant::StationRailDiagram,
                      Canvas3DSceneMarkerListKind::None, "station.put", std::nullopt,
                      station.edit_id);
    }

    for (size_t row_index = 0; row_index < model.curve_rows.size(); ++row_index) {
        const TableRow& row = model.curve_rows[row_index];
        const std::string method = ascii_lower(table_cell(row, "method"));
        const bool transition = method == "curve.begintransition";
        const double radius = table_cell_number(row, "radius");
        const bool end = method == "curve.end" ||
            (!transition && std::isfinite(radius) &&
             std::abs(radius) <= k_scene_route_display_zero_epsilon);
        const std::string edit_id = transition
            ? table_cell(row, "_primaryEditId") : row.edit_id;
        const bool unpaired_transition = transition &&
            table_cell(row, "_transitionStatus") == "orphan";
        if (transition) {
            append_marker(MapMarkerVisualKind::CurveTransitionStart,
                          table_cell_number(row, "distance"), "Curve\nTr.",
                          MapMarkerIconVariant::Default,
                          Canvas3DSceneMarkerListKind::None, "curve", row_index,
                          edit_id, unpaired_transition);
        } else if (end) {
            append_marker(MapMarkerVisualKind::CurveEnd,
                          table_cell_number(row, "distance"), "End",
                          MapMarkerIconVariant::Default,
                          Canvas3DSceneMarkerListKind::None, "curve", row_index,
                          edit_id);
        } else {
            TrackEvent event;
            event.distance = table_cell_number(row, "distance");
            event.value_number = true;
            event.number = radius;
            append_marker(MapMarkerVisualKind::CurveCircularStart, event.distance,
                          canvas3d_scene_curve_marker_label(scene.route_info, event),
                          MapMarkerIconVariant::Default,
                          Canvas3DSceneMarkerListKind::None, "curve", row_index,
                          edit_id);
        }
    }
    for (size_t row_index = 0; row_index < model.gradient_rows.size(); ++row_index) {
        const TableRow& row = model.gradient_rows[row_index];
        const std::string method = ascii_lower(table_cell(row, "method"));
        const bool transition = method == "gradient.begintransition";
        const double gradient = table_cell_number(row, "gradient");
        const bool end = method == "gradient.end" ||
            (!transition && std::isfinite(gradient) &&
             std::abs(gradient) <= k_scene_route_display_zero_epsilon);
        const double distance = table_cell_number(row, "distance");
        const std::string edit_id = transition
            ? table_cell(row, "_primaryEditId") : row.edit_id;
        const bool unpaired_transition = transition &&
            table_cell(row, "_transitionStatus") == "orphan";
        if (transition) {
            append_marker(MapMarkerVisualKind::GradientTransitionStart, distance,
                          "Gradient\nTr.", MapMarkerIconVariant::Default,
                          Canvas3DSceneMarkerListKind::None, "gradient", row_index,
                          edit_id, unpaired_transition);
        } else if (end) {
            append_marker(MapMarkerVisualKind::GradientEnd, distance, "End",
                          MapMarkerIconVariant::Default,
                          Canvas3DSceneMarkerListKind::None, "gradient", row_index,
                          edit_id);
        } else {
            append_marker(MapMarkerVisualKind::GradientStart, distance,
                          canvas3d_scene_gradient_marker_label(
                              gradient),
                          MapMarkerIconVariant::Default,
                          Canvas3DSceneMarkerListKind::None, "gradient", row_index,
                          edit_id);
        }
    }

    for (size_t row_index = 0; row_index < model.other_track_changes.size();
         ++row_index) {
        const TableRow& row = model.other_track_changes[row_index];
        if (row.edit_id.empty()) continue;
        const std::string track_key = table_cell(row, "trackKey");
        const std::string normalized_key = normalize_track_lookup_key(track_key);
        const Canvas3DTrackPath* path =
            scene_other_track_path_for_key(scene, normalized_key);
        if (!path || path->points.empty()) continue;
        const double distance = table_cell_number(row, "distance");
        if (!std::isfinite(distance) ||
            distance < path->points.front().distance ||
            distance > path->points.back().distance) continue;
        const std::optional<Canvas3DTrackPoint> point =
            scene_sample_track_path_points(*path, distance);
        if (!point) continue;
        Canvas3DSceneMarker marker;
        marker.kind = MapMarkerVisualKind::OtherTrackChange;
        marker.track_point = *point;
        const std::string& method = table_cell(row, "method");
        const std::string& parameters = table_cell(row, "parameters");
        const size_t method_offset = method.compare(0, 6, "Track.") == 0 ? 6 : 0;
        std::string label;
        label.reserve(track_key.size() + method.size() + parameters.size() + 2);
        label.append(track_key);
        label.push_back('\n');
        label.append(method, method_offset, std::string::npos);
        label.push_back('\n');
        bool comma_pending = false;
        for (const char character : parameters) {
            if (character == ',') {
                comma_pending = true;
                continue;
            }
            if (comma_pending) {
                if (std::isspace(static_cast<unsigned char>(character))) continue;
                label.push_back(' ');
                comma_pending = false;
            }
            label.push_back(character);
        }
        marker.label = std::move(label);
        marker.row_kind = "otherTrack.change";
        marker.row_index = row_index;
        marker.edit_id = row.edit_id;
        marker.track_key = path->key;
        marker.theme_color = path->color;
        marker.has_theme_color = true;
        scene.markers.push_back(std::move(marker));
    }

    for (const SpeedLimit& speed : model.speedlimits) {
        const TableRow* source = speed.row_index < model.speed_limit_rows.size()
            ? &model.speed_limit_rows[speed.row_index]
            : nullptr;
        const std::string edit_id = source ? source->edit_id : std::string{};
        char value[64] = {};
        if (speed.has_speed && std::isfinite(speed.speed)) {
            format_scene_route_number(value, sizeof(value), speed.speed);
            append_marker(
                MapMarkerVisualKind::SpeedLimit, speed.distance, value,
                MapMarkerIconVariant::SpeedLimitBegin,
                Canvas3DSceneMarkerListKind::SpeedLimit, "speedlimit",
                speed.row_index, edit_id);
        } else {
            append_marker(
                MapMarkerVisualKind::SpeedLimit, speed.distance, {},
                MapMarkerIconVariant::SpeedLimitEnd,
                Canvas3DSceneMarkerListKind::SpeedLimit, "speedlimit",
                speed.row_index, edit_id);
        }
    }

    auto append_table_markers = [&](const std::vector<TableRow>& rows,
                                    MapMarkerVisualKind kind,
                                    Canvas3DSceneMarkerListKind list_kind,
                                    const char* row_kind,
                                    const auto& label_for_row,
                                    MapMarkerIconVariant icon_variant =
                                        MapMarkerIconVariant::Default) {
        for (size_t row_index = 0; row_index < rows.size(); ++row_index) {
            const TableRow& row = rows[row_index];
            std::string label = label_for_row(row);
            const bool sound3d = kind == MapMarkerVisualKind::MapSound3D;
            append_marker(kind, table_cell_number(row, "distance"),
                          std::move(label), icon_variant, list_kind, row_kind,
                          row_index, row.edit_id, false,
                          sound3d ? table_cell_number(row, "x") : 0.0,
                          sound3d ? table_cell_number(row, "y") : 0.0);
        }
    };

    const auto field_label = [](const char* key) {
        return [key](const TableRow& row) {
            return canvas3d_scene_table_marker_label(row, {key});
        };
    };

    append_table_markers(model.section_begins, MapMarkerVisualKind::Section,
                         Canvas3DSceneMarkerListKind::Section, "section.begin",
                         [](const TableRow& row) {
                             return join_table_values(
                                 section_row_values(row), " ");
                         });

    append_table_markers(model.beacons, MapMarkerVisualKind::Beacon,
                         Canvas3DSceneMarkerListKind::Beacon, "beacon.put",
                         [](const TableRow& row) {
                             return canvas3d_scene_table_marker_label(
                                 row, {"type", "section", "sendData"});
                         });
    append_table_markers(model.pretrains, MapMarkerVisualKind::PreTrain,
                         Canvas3DSceneMarkerListKind::None, "preTrain.pass",
                         field_label("passTime"));
    append_table_markers(model.irregularities, MapMarkerVisualKind::Irregularity,
                         Canvas3DSceneMarkerListKind::Irregularity,
                         "irregularity.change",
                         [](const TableRow& row) {
                             return canvas3d_scene_table_marker_label(
                                 row, {"x", "y", "r"}, "\n") + "\n" +
                                 canvas3d_scene_table_marker_label(
                                     row, {"lx", "ly", "lr"});
                         });
    append_table_markers(model.map_sounds, MapMarkerVisualKind::MapSound,
                         Canvas3DSceneMarkerListKind::MapSound,
                         "mapSound.play",
                         field_label("soundKey"));
    append_table_markers(model.map_sound_3d, MapMarkerVisualKind::MapSound3D,
                         Canvas3DSceneMarkerListKind::MapSound3D,
                         "mapSound3D.put",
                         field_label("soundKey"));
    append_table_markers(model.rolling_noises, MapMarkerVisualKind::RollingNoise,
                         Canvas3DSceneMarkerListKind::RollingNoise,
                         "rollingNoise.change",
                         field_label("index"));
    append_table_markers(model.flange_noises, MapMarkerVisualKind::FlangeNoise,
                         Canvas3DSceneMarkerListKind::FlangeNoise,
                         "flangeNoise.change", field_label("index"));
    append_table_markers(model.joint_noises, MapMarkerVisualKind::JointNoise,
                         Canvas3DSceneMarkerListKind::JointNoise,
                         "jointNoise.play",
                         field_label("index"));
    append_table_markers(model.backgrounds, MapMarkerVisualKind::Background,
                         Canvas3DSceneMarkerListKind::Background,
                         "background.change",
                         field_label("structureKey"));
    append_table_markers(model.adhesions, MapMarkerVisualKind::Adhesion,
                         Canvas3DSceneMarkerListKind::Adhesion,
                         "adhesion.change",
                         [](const TableRow& row) {
                             return canvas3d_scene_table_marker_label(
                                 row, {"a", "b", "c"});
                         },
                         MapMarkerIconVariant::AdhesionOutlined);
    append_table_markers(model.cab_illuminance, MapMarkerVisualKind::CabIlluminance,
                         Canvas3DSceneMarkerListKind::CabIlluminance,
                         "cabIlluminance.change",
                         field_label("value"));
    append_table_markers(model.fogs, MapMarkerVisualKind::Fog,
                         Canvas3DSceneMarkerListKind::Fog, "fog.change",
                         [](const TableRow& row) {
                             std::string label = canvas3d_scene_table_marker_label(
                                 row, {"density"}, " ", "-");
                             label += '\n';
                             label += canvas3d_scene_table_marker_label(
                                 row, {"red", "green", "blue"}, " ", "-");
                             return label;
                         });
    append_table_markers(model.legacy_fogs, MapMarkerVisualKind::Fog,
                         Canvas3DSceneMarkerListKind::LegacyFog, "legacyFog.change",
                         [](const TableRow& row) {
                             std::string label = canvas3d_scene_table_marker_label(
                                 row, {"start", "end"}, " ", "-");
                             label += '\n';
                             label += canvas3d_scene_table_marker_label(
                                 row, {"red", "green", "blue"}, " ", "-");
                             return label;
                         });
    append_table_markers(model.draw_distances, MapMarkerVisualKind::DrawDistance,
                         Canvas3DSceneMarkerListKind::DrawDistance,
                         "drawDistance.change",
                         field_label("value"));

    sort_canvas3d_scene_markers(scene.markers);
}

double resolve_canvas3d_scene_fog_component(const TableRow& row,
                                            const char* key,
                                            double previous_value) {
    if (table_cell(row, key).empty()) return previous_value;
    const double value = table_cell_number(row, key);
    if (std::isnan(value)) return previous_value;
    return std::clamp(value, 0.0, 1.0);
}

void populate_canvas3d_scene_fog(Canvas3DScene& scene, const MapModel& model) {
    scene.fog_keyframes.clear();
    if (model.fogs.empty()) return;

    std::vector<const TableRow*> rows;
    rows.reserve(model.fogs.size());
    for (const TableRow& row : model.fogs) {
        if (std::isfinite(table_cell_number(row, "distance"))) rows.push_back(&row);
    }
    std::stable_sort(rows.begin(), rows.end(), [](const TableRow* a, const TableRow* b) {
        const double a_distance = table_cell_number(*a, "distance");
        const double b_distance = table_cell_number(*b, "distance");
        if (a_distance != b_distance) return a_distance < b_distance;
        return table_cell_number(*a, "order") < table_cell_number(*b, "order");
    });

    scene.fog_keyframes.reserve(rows.size());
    double density = k_default_scene_fog_density;
    double red = k_default_scene_fog_color;
    double green = k_default_scene_fog_color;
    double blue = k_default_scene_fog_color;
    for (const TableRow* row : rows) {
        density = resolve_canvas3d_scene_fog_component(*row, "density", density);
        red = resolve_canvas3d_scene_fog_component(*row, "red", red);
        green = resolve_canvas3d_scene_fog_component(*row, "green", green);
        blue = resolve_canvas3d_scene_fog_component(*row, "blue", blue);

        Canvas3DSceneFogKeyframe keyframe;
        keyframe.distance = table_cell_number(*row, "distance");
        keyframe.density = static_cast<float>(density);
        keyframe.color = ImVec4(static_cast<float>(red), static_cast<float>(green),
                                static_cast<float>(blue), 1.0f);
        if (!scene.fog_keyframes.empty() &&
            scene.fog_keyframes.back().distance == keyframe.distance) {
            scene.fog_keyframes.back() = keyframe;
        } else {
            scene.fog_keyframes.push_back(keyframe);
        }
    }
}

void populate_canvas3d_scene_draw_distances(Canvas3DScene& scene, const MapModel& model) {
    scene.draw_distance_changes.clear();
    if (model.draw_distances.empty()) return;

    std::vector<const TableRow*> rows;
    rows.reserve(model.draw_distances.size());
    for (const TableRow& row : model.draw_distances) {
        const double distance = table_cell_number(row, "distance");
        if (std::isfinite(distance)) rows.push_back(&row);
    }
    std::stable_sort(rows.begin(), rows.end(), [](const TableRow* a, const TableRow* b) {
        const double a_distance = table_cell_number(*a, "distance");
        const double b_distance = table_cell_number(*b, "distance");
        if (a_distance != b_distance) return a_distance < b_distance;
        return table_cell_number(*a, "order") < table_cell_number(*b, "order");
    });

    scene.draw_distance_changes.reserve(rows.size());
    for (const TableRow* row : rows) {
        Canvas3DSceneDrawDistanceChange change;
        change.distance = table_cell_number(*row, "distance");
        const double raw_value = table_cell_number(*row, "value");
        change.value = !std::isfinite(raw_value) || raw_value < 0.0
            ? 0.0
            : std::round(raw_value / static_cast<double>(k_scene_draw_distance_step_m)) *
                static_cast<double>(k_scene_draw_distance_step_m);
        if (!scene.draw_distance_changes.empty() &&
            scene.draw_distance_changes.back().distance == change.distance) {
            scene.draw_distance_changes.back() = change;
        } else {
            scene.draw_distance_changes.push_back(change);
        }
    }
}

struct SceneFogSample {
    bool enabled = false;
    float density = 0.0f;
    ImVec4 color = ImVec4(0.0f, 0.0f, 0.0f, 1.0f);
};

SceneFogSample sample_canvas3d_scene_fog(
    const std::vector<Canvas3DSceneFogKeyframe>& keyframes,
    double distance,
    bool enabled) {
    SceneFogSample sample;
    if (!enabled || keyframes.empty()) return sample;

    auto next = std::upper_bound(
        keyframes.begin(), keyframes.end(), distance,
        [](double value, const Canvas3DSceneFogKeyframe& keyframe) {
            return value < keyframe.distance;
        });
    if (next == keyframes.begin()) {
        sample.density = next->density;
        sample.color = next->color;
    } else if (next == keyframes.end()) {
        sample.density = keyframes.back().density;
        sample.color = keyframes.back().color;
    } else {
        const Canvas3DSceneFogKeyframe& previous = *(next - 1);
        const double interval = next->distance - previous.distance;
        const float ratio = interval > 0.0
            ? static_cast<float>(std::clamp((distance - previous.distance) / interval, 0.0, 1.0))
            : 1.0f;
        sample.density = previous.density + (next->density - previous.density) * ratio;
        sample.color = ImVec4(
            previous.color.x + (next->color.x - previous.color.x) * ratio,
            previous.color.y + (next->color.y - previous.color.y) * ratio,
            previous.color.z + (next->color.z - previous.color.z) * ratio,
            1.0f);
    }
    sample.enabled = sample.density > 0.0f;
    return sample;
}

enum class SceneRouteValueMode {
    Constant,
    Transition,
    Interpolate,
};

struct SceneRouteValueSample {
    SceneRouteValueMode mode = SceneRouteValueMode::Constant;
    double value = 0.0;
    double from_value = 0.0;
    double to_value = 0.0;
};

SceneRouteValueSample sample_scene_route_value(
    const std::vector<Canvas3DSceneRouteValueEvent>& events,
    double distance) {
    auto next = std::upper_bound(
        events.begin(), events.end(), distance,
        [](double value, const Canvas3DSceneRouteValueEvent& event) {
            return value < event.distance;
        });
    const Canvas3DSceneRouteValueEvent* previous =
        next == events.begin() ? nullptr : &*(next - 1);

    SceneRouteValueSample result;
    result.value = previous ? previous->value : 0.0;
    result.from_value = result.value;
    result.to_value = result.value;
    if (previous && next != events.end() &&
        next->kind == Canvas3DSceneRouteEventKind::Interpolate) {
        result.mode = SceneRouteValueMode::Interpolate;
        result.from_value = previous->value;
        result.to_value = next->value;
    } else if (previous && previous->kind == Canvas3DSceneRouteEventKind::BeginTransition &&
               next != events.end() &&
               next->kind != Canvas3DSceneRouteEventKind::BeginTransition) {
        result.mode = SceneRouteValueMode::Transition;
        result.from_value = previous->previous_value;
        result.to_value = next->value;
    }
    return result;
}

void format_scene_route_number(char* output, size_t output_size, double value) {
    if (!output || output_size == 0) return;
    if (!std::isfinite(value)) value = 0.0;
    if (std::abs(value) <= k_scene_route_display_zero_epsilon) value = 0.0;
    std::snprintf(output, output_size, "%.6f", value);
    char* end = output + std::strlen(output);
    char* decimal = std::strchr(output, '.');
    if (!decimal) return;
    while (end > decimal + 1 && end[-1] == '0') --end;
    if (end > decimal && end[-1] == '.') --end;
    *end = '\0';
}

bool populate_canvas3d_scene_dynamic_content(Canvas3DScene& scene,
                                             const MapModel& model,
                                             int station_index) {
    const Canvas3DTrackPath* own_path = scene_own_track_path(scene);
    if (!own_path || own_path->points.empty()) return false;

    scene.objects.clear();
    scene.instances.clear();
    scene.repeaters.clear();
    scene.backgrounds.clear();
    scene.min_distance = own_path->points.front().distance;
    scene.max_distance = own_path->points.back().distance;

    auto sample_placement_track = [&](const std::string& key, double distance) -> std::optional<Canvas3DTrackPoint> {
        const Canvas3DTrackPath* path = scene_placement_track_path_for_key(scene, key);
        return path ? scene_sample_track_path_points(*path, distance) : std::nullopt;
    };
    auto scene_object = [](Canvas3DSceneObjectKind kind, size_t source_row,
                           const TableRow& row, const char* label_column) {
        Canvas3DSceneObject object;
        object.kind = kind;
        object.source_row = source_row;
        object.label = table_cell(row, label_column);
        object.edit_id = row.edit_id;
        return object;
    };
    auto track_following_instance = [](std::string model_path, const TableRow& row,
                                       double distance, int object_index) {
        Canvas3DModelInstance instance;
        instance.model_path = std::move(model_path);
        instance.track_key = table_cell(row, "trackKey");
        instance.distance = distance;
        instance.object_index = object_index;
        instance.follow_track = true;
        instance.x = table_cell_number(row, "x");
        instance.y = table_cell_number(row, "y");
        instance.z = table_cell_number(row, "z");
        instance.rx = table_cell_number(row, "rx");
        instance.ry = table_cell_number(row, "ry");
        instance.rz = table_cell_number(row, "rz");
        instance.tilt = table_cell_number(row, "tilt");
        instance.span = table_cell_number(row, "span");
        return instance;
    };

    std::map<std::string, std::string> model_paths;
    for (const TableRow& row : model.structure_models) {
        std::string key = scene_model_key(table_cell(row, "structureKey"));
        std::string path = table_cell(row, "resolvedFilePath");
        if (!key.empty() && !path.empty()) model_paths[key] = path;
    }
    auto model_path_for_key = [&](const std::string& key) -> std::string {
        auto it = model_paths.find(scene_model_key(key));
        return it == model_paths.end() ? std::string{} : it->second;
    };

    std::map<std::string, std::vector<Canvas3DSceneModelOption>> signal_model_options;
    for (const TableRow& row : model.signal_aspects) {
        const std::string aspect_key = scene_model_key(table_cell(row, "signalAspectKey"));
        if (aspect_key.empty()) continue;
        const int key_count = std::max(0, static_cast<int>(table_cell_number(row, "_structureKeyCount")));
        std::vector<Canvas3DSceneModelOption> options;
        options.reserve(static_cast<size_t>(key_count));
        for (int key_index = 1; key_index <= key_count; ++key_index) {
            std::string structure_key = trim_ascii(table_cell(row, "structureKey" + std::to_string(key_index)));
            if (structure_key.empty()) continue;
            Canvas3DSceneModelOption option;
            option.structure_key_index = key_index;
            option.structure_key = std::move(structure_key);
            option.model_path = model_path_for_key(option.structure_key);
            options.push_back(std::move(option));
        }
        if (!options.empty()) signal_model_options[aspect_key] = std::move(options);
    }

    auto append_structure_instance = [&](const TableRow& row, size_t source_row) {
        std::string path = model_path_for_key(table_cell(row, "structureKey"));
        if (path.empty()) return;
        double distance = table_cell_number(row, "distance");
        auto point = sample_placement_track(table_cell(row, "trackKey"), distance);
        if (!point) return;
        const int object_index = static_cast<int>(scene.objects.size());
        scene.objects.push_back(scene_object(
            Canvas3DSceneObjectKind::Structure, source_row, row, "structureKey"));
        scene.instances.push_back(track_following_instance(
            std::move(path), row, distance, object_index));
    };
    for (size_t row_index = 0; row_index < model.structures.size(); ++row_index) {
        append_structure_instance(model.structures[row_index], row_index);
    }

    for (size_t between_index = 0; between_index < model.structures_between.size(); ++between_index) {
        const TableRow& row = model.structures_between[between_index];
        std::string path = model_path_for_key(table_cell(row, "structureKey"));
        if (path.empty()) continue;
        double distance = table_cell_number(row, "distance");
        auto p1 = sample_placement_track(table_cell(row, "trackKey1"), distance);
        auto p2 = sample_placement_track(table_cell(row, "trackKey2"), distance);
        auto own = scene_sample_track_path_points(*own_path, distance);
        if (!p1 || !p2 || !own) continue;
        Canvas3DSceneObject object = scene_object(
            Canvas3DSceneObjectKind::Structure,
            model.structures.size() + between_index, row, "structureKey");
        object.structure_put_between = true;
        const int object_index = static_cast<int>(scene.objects.size());
        scene.objects.push_back(std::move(object));

        Canvas3DModelInstance instance;
        instance.model_path = path;
        instance.distance = distance;
        instance.object_index = object_index;
        instance.put_between = true;
        instance.put_between_track_key1 = table_cell(row, "trackKey1");
        instance.put_between_track_key2 = table_cell(row, "trackKey2");
        instance.put_between_flag =
            kme::truncating_int_or_zero(table_cell_number(row, "flag")) & 1;
        instance.world[12] = own->x;
        instance.world[13] = own->y;
        instance.world[14] = own->z;
        scene.instances.push_back(std::move(instance));
    }

    auto append_signal_instance = [&](const TableRow& row, size_t row_index) {
        const std::string aspect_key = scene_model_key(table_cell(row, "signalAspectKey"));
        auto options_it = signal_model_options.find(aspect_key);
        if (options_it == signal_model_options.end()) return;

        const std::vector<Canvas3DSceneModelOption>& options = options_it->second;
        auto selected_it = std::find_if(options.begin(), options.end(), [](const Canvas3DSceneModelOption& option) {
            return !option.model_path.empty();
        });
        if (selected_it == options.end()) return;

        const double distance = table_cell_number(row, "distance");
        auto point = sample_placement_track(table_cell(row, "trackKey"), distance);
        if (!point) return;

        Canvas3DSceneObject object = scene_object(
            Canvas3DSceneObjectKind::Signal, row_index, row, "signalAspectKey");
        object.model_options = options;
        object.selected_model_option = static_cast<size_t>(selected_it - options.begin());

        const int object_index = static_cast<int>(scene.objects.size());
        scene.objects.push_back(std::move(object));
        scene.instances.push_back(track_following_instance(
            selected_it->model_path, row, distance, object_index));
    };
    for (size_t row_index = 0; row_index < model.signals.size(); ++row_index) {
        append_signal_instance(model.signals[row_index], row_index);
    }

    const repeater_linkage::Linkage linkage =
        repeater_linkage::pair_linkage(table_repeater_events(model.repeaters));
    scene.repeaters.reserve(linkage.segments.size());
    for (const repeater_linkage::Segment& linked : linkage.segments) {
        if (linked.begin_source_index >= model.repeaters.size()) continue;
        const TableRow& begin = model.repeaters[linked.begin_source_index];
        const double end_distance = linked.boundary_kind == repeater_linkage::BoundaryKind::Open
            ? scene.max_distance
            : linked.end_distance;
        if (end_distance < linked.begin_distance) continue;

        Canvas3DRepeaterSegment segment;
        segment.edit_id = begin.edit_id;
        segment.track_key = table_cell(begin, "trackKey");
        segment.chain_begin_index = linked.chain_begin_index;
        segment.chain_begin_count = linked.chain_begin_count;
        segment.begin_distance = linked.begin_distance;
        segment.end_distance = end_distance;
        segment.has_end_or_change_position =
            linked.boundary_kind != repeater_linkage::BoundaryKind::Open;
        segment.interval = table_cell_number(begin, "interval");
        segment.x = table_cell_number(begin, "x");
        segment.y = table_cell_number(begin, "y");
        segment.z = table_cell_number(begin, "z");
        segment.rx = table_cell_number(begin, "rx");
        segment.ry = table_cell_number(begin, "ry");
        segment.rz = table_cell_number(begin, "rz");
        segment.tilt = table_cell_number(begin, "tilt");
        segment.span = table_cell_number(begin, "span");
        for (const std::string& structure_key : scene_split_key_list(
                 table_cell(begin, "structureKeys"))) {
            const std::string path = model_path_for_key(structure_key);
            if (!path.empty()) segment.model_paths.push_back(path);
        }
        if (segment.model_paths.empty()) continue;

        Canvas3DSceneObject object = scene_object(
            Canvas3DSceneObjectKind::Repeater, linked.display_index - 1,
            begin, "repeaterKey");
        segment.object_index = static_cast<int>(scene.objects.size());
        scene.objects.push_back(std::move(object));
        scene.repeaters.push_back(std::move(segment));
    }

    for (const TableRow& row : model.backgrounds) {
        Canvas3DBackgroundChange change;
        change.distance = table_cell_number(row, "distance");
        change.model_path = model_path_for_key(table_cell(row, "structureKey"));
        scene.backgrounds.push_back(std::move(change));
    }

    double camera_distance = scene.min_distance;
    if (!model.stations.empty()) {
        int clamped_station = std::clamp(station_index, 0, static_cast<int>(model.stations.size()) - 1);
        camera_distance = model.stations[clamped_station].distance;
    }
    auto camera_point = scene_sample_track_path_points(*own_path, camera_distance);
    if (!camera_point) camera_point = own_path->points.front();
    scene.camera.distance = camera_distance;
    scene.camera.x = camera_point->x;
    scene.camera.y = camera_point->y + k_default_scene_camera_height;
    scene.camera.z = camera_point->z;
    scene.camera.yaw = camera_point->theta;
    scene.camera.pitch = 0.0f;
    return true;
}

const char* k_scene_shader_source = R"(
cbuffer SceneViewConstants : register(b0)
{
    row_major float4x4 viewProj;
    float4 materialColor;
    float4 useTexture;
    float4 fogColorDensity;
};

Texture2D diffuseTexture : register(t0);
SamplerState diffuseSampler : register(s0);

struct VSInput
{
    float3 position : POSITION;
    float3 normal : NORMAL;
    float2 texcoord : TEXCOORD0;
    float4 world0 : WORLD0;
    float4 world1 : WORLD1;
    float4 world2 : WORLD2;
    float4 world3 : WORLD3;
};

struct VSOutput
{
    float4 position : SV_POSITION;
    float3 texcoord_eye_depth : TEXCOORD0;
};

VSOutput vs_main(VSInput input)
{
    float4x4 world = float4x4(input.world0, input.world1, input.world2, input.world3);
    VSOutput output;
    float4 clipPosition = mul(mul(float4(input.position, 1.0), world), viewProj);
    output.position = clipPosition;
    output.texcoord_eye_depth = float3(input.texcoord, clipPosition.w);
    return output;
}

float4 sample_material(VSOutput input)
{
    float4 color = materialColor;
    if (useTexture.x > 0.5)
        color *= diffuseTexture.Sample(diffuseSampler, input.texcoord_eye_depth.xy);
    clip(color.a - 0.1);
    if (useTexture.y > 0.5)
        color.a = 1.0;
    return color;
}

float4 ps_main(VSOutput input) : SV_TARGET
{
    return sample_material(input);
}

float4 ps_fog_main(VSOutput input) : SV_TARGET
{
    float4 color = sample_material(input);
    float eyeDepth = max(input.texcoord_eye_depth.z, 0.0);
    float fogFactor = saturate(exp2(-1.44269504089 * fogColorDensity.w * eyeDepth));
    color.rgb = lerp(fogColorDensity.rgb, color.rgb, fogFactor);
    return color;
}
)";

const char* k_scene_marker_shader_source = R"(
cbuffer SceneMarkerConstants : register(b0)
{
    row_major float4x4 viewProj;
    float4 chunkOffset;
};

cbuffer SceneMarkerPickConstants : register(b1)
{
    uint markerPickIdBase;
    float3 markerPickPadding;
};

Texture2D fontTexture : register(t0);
SamplerState fontSampler : register(s0);

struct VSInput
{
    float3 position : POSITION;
    float2 texcoord : TEXCOORD0;
    float4 color : COLOR0;
    float useTexture : TEXCOORD1;
    uint markerIndex : TEXCOORD2;
};

struct VSOutput
{
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
    float4 color : COLOR0;
    float useTexture : TEXCOORD1;
    nointerpolation uint markerIndex : TEXCOORD2;
};

VSOutput vs_main(VSInput input)
{
    VSOutput output;
    float3 worldPosition = input.position + chunkOffset.xyz;
    output.position = mul(float4(worldPosition, 1.0), viewProj);
    output.texcoord = input.texcoord;
    output.color = input.color;
    output.useTexture = input.useTexture;
    output.markerIndex = input.markerIndex;
    return output;
}

float marker_alpha(VSOutput input)
{
    float alpha = input.color.a;
    if (input.useTexture > 0.5)
        alpha *= fontTexture.Sample(fontSampler, input.texcoord).a;
    clip(alpha - 0.01);
    return alpha;
}

float4 ps_main(VSOutput input) : SV_TARGET
{
    float alpha = marker_alpha(input);
    return float4(input.color.rgb, alpha);
}

float4 ps_pick_main(VSOutput input) : SV_TARGET
{
    marker_alpha(input);
    uint id = markerPickIdBase + input.markerIndex;
    return float4(
        float(id & 0xffu) / 255.0,
        float((id >> 8) & 0xffu) / 255.0,
        float((id >> 16) & 0xffu) / 255.0,
        1.0);
}

float4 ps_mask_main(VSOutput input) : SV_TARGET
{
    marker_alpha(input);
    return float4(1.0, 1.0, 1.0, 1.0);
}
)";

const char* k_scene_pick_shader_source = R"(
cbuffer ScenePickConstants : register(b1)
{
    float4 pickColor;
    float4 alphaControls;
};

Texture2D diffuseTexture : register(t0);
SamplerState diffuseSampler : register(s0);

struct VSOutput
{
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
};

float4 ps_main(VSOutput input) : SV_TARGET
{
    float alpha = alphaControls.x;
    if (alphaControls.y > 0.5)
        alpha *= diffuseTexture.Sample(diffuseSampler, input.texcoord).a;
    clip(alpha - 0.1);
    return pickColor;
}
)";

const char* k_scene_highlight_outline_shader_source = R"(
cbuffer SceneOutlineConstants : register(b0)
{
    float4 texelRadius;
    float4 outlineColor;
};

Texture2D highlightMask : register(t0);
SamplerState maskSampler : register(s0);

struct VSOutput
{
    float4 position : SV_POSITION;
};

VSOutput vs_main(uint vertexId : SV_VertexID)
{
    float2 positions[3] = {
        float2(-1.0, -1.0),
        float2(-1.0,  3.0),
        float2( 3.0, -1.0)
    };

    VSOutput output;
    output.position = float4(positions[vertexId], 0.0, 1.0);
    return output;
}

float mask_at(float2 uv)
{
    return highlightMask.SampleLevel(maskSampler, uv, 0).a;
}

float4 ps_main(VSOutput input) : SV_TARGET
{
    float2 uv = input.position.xy * texelRadius.xy;
    const float threshold = 0.1;
    const int sampleRadius = 3;
    bool centerInside = mask_at(uv) > threshold;
    bool border = false;

    [unroll]
    for (int y = -sampleRadius; y <= sampleRadius; ++y) {
        [unroll]
        for (int x = -sampleRadius; x <= sampleRadius; ++x) {
            float2 offsetPixels = float2((float)x, (float)y);
            if (dot(offsetPixels, offsetPixels) > texelRadius.z * texelRadius.z) continue;

            bool sampleInside = mask_at(uv + offsetPixels * texelRadius.xy) > threshold;
            if (sampleInside != centerInside) {
                border = true;
            }
        }
    }

    clip(border ? 1.0 : -1.0);
    return outlineColor;
}
)";

class ModelLoaderClient {
public:
    // Keep Assimp/model_loader loaded for the process lifetime; unloading it before
    // CRT/DLL teardown can leave stale cleanup callbacks in some dependency builds.
    ~ModelLoaderClient() = default;

    bool prepare(std::string& error) {
        return ensure_loaded(error);
    }

    bool load(const std::string& path, MlMeshData& data, std::string& error) {
        if (!ensure_loaded(error)) return false;
        if (!load_model_(path.c_str(), &data)) {
            const char* loader_error = get_last_error_ ? get_last_error_() : nullptr;
            error = loader_error && *loader_error ? loader_error : "model_loader.dll could not load the model";
            return false;
        }
#ifndef NDEBUG
        debug_successful_load_count_.fetch_add(1, std::memory_order_relaxed);
#endif
        return true;
    }

    void free_model(MlMeshData& data) {
#ifndef NDEBUG
        debug_free_count_.fetch_add(1, std::memory_order_relaxed);
#endif
        if (free_model_) free_model_(&data);
        else data = {};
    }

#ifndef NDEBUG
    static void debug_reset_counts() noexcept {
        debug_successful_load_count_.store(0, std::memory_order_relaxed);
        debug_free_count_.store(0, std::memory_order_relaxed);
    }

    static size_t debug_successful_load_count() noexcept {
        return debug_successful_load_count_.load(std::memory_order_relaxed);
    }

    static size_t debug_free_count() noexcept {
        return debug_free_count_.load(std::memory_order_relaxed);
    }
#endif

private:
    bool ensure_loaded(std::string& error) {
        if (library_) return true;

        DWORD first_error = ERROR_SUCCESS;
        library_ = runtime_paths::load_dll(L"model_loader.dll", &first_error);
        if (!library_) {
            error = "bin/model_loader.dll load failed: " + win32_error_text(first_error);
            return false;
        }

        api_version_ = runtime_paths::resolve_dll_function<MlApiVersionFn>(library_, "ml_api_version");
        load_model_ = runtime_paths::resolve_dll_function<MlLoadModelFn>(library_, "ml_load_model");
        free_model_ = runtime_paths::resolve_dll_function<MlFreeModelFn>(library_, "ml_free_model");
        get_last_error_ = runtime_paths::resolve_dll_function<MlGetLastErrorFn>(library_, "ml_get_last_error");
        if (!api_version_ || !load_model_ || !free_model_ || !get_last_error_) {
            error = "model_loader.dll is missing required entry points";
            FreeLibrary(library_);
            library_ = nullptr;
            return false;
        }
        if (api_version_() != 2) {
            error = "model_loader.dll API version is not supported";
            FreeLibrary(library_);
            library_ = nullptr;
            return false;
        }
        return true;
    }

    HMODULE library_ = nullptr;
    MlApiVersionFn api_version_ = nullptr;
    MlLoadModelFn load_model_ = nullptr;
    MlFreeModelFn free_model_ = nullptr;
    MlGetLastErrorFn get_last_error_ = nullptr;
#ifndef NDEBUG
    static inline std::atomic<size_t> debug_successful_load_count_{0};
    static inline std::atomic<size_t> debug_free_count_{0};
#endif
};

class ModelDataGuard {
public:
    ModelDataGuard(ModelLoaderClient& loader, MlMeshData& data) noexcept
        : loader_(&loader), data_(&data) {}

    ModelDataGuard(const ModelDataGuard&) = delete;
    ModelDataGuard& operator=(const ModelDataGuard&) = delete;

    ~ModelDataGuard() noexcept {
        if (!loaded_) return;
        try {
            loader_->free_model(*data_);
        } catch (...) {
            *data_ = {};
        }
    }

    void mark_loaded() noexcept { loaded_ = true; }

private:
    ModelLoaderClient* loader_ = nullptr;
    MlMeshData* data_ = nullptr;
    bool loaded_ = false;
};

} // namespace

Canvas3DSceneBuildResult build_canvas3d_scene_preview(const Canvas3DSceneBuildOptions& options) {
    Canvas3DSceneBuildResult result;
    Canvas3DScene& scene = result.scene;
    if (!options.model || options.model->own.empty()) return result;

    const MapModel& model = *options.model;
    scene.tracks.reserve(model.other_tracks.size() + 1);
    auto append_track_path = [&](const std::string& key, const SceneTrackBufferView& points,
                                 bool has_theta, ImVec4 color, bool visible) {
        if (points.empty() || points.cols < 3) return;
        Canvas3DTrackPath path;
        path.key = key;
        path.color = color;
        path.visible = visible;
        path.points.reserve(points.rows);
        for (size_t row = 0; row < points.rows; ++row) {
            path.points.push_back(scene_track_row_point(points, row, has_theta));
        }
        scene.tracks.push_back(std::move(path));
    };

    bool used_scene_geometry = false;
    if (options.map_handle) {
        const double max_step = std::clamp(
            options.control_point_interval > 0.0 ? options.control_point_interval : 25.0,
            1.0,
            25.0);
        bool scene_geometry_ok =
            kv_generate_scene_geometry(options.map_handle, options.unit_distance, 1.0, max_step, 1.0, 0.01) != 0;
        if (scene_geometry_ok) {
            KvSceneGeometrySnapshot snapshot{};
            scene_geometry_ok = kv_get_scene_geometry_snapshot(
                options.map_handle, KV_SCENE_GEOMETRY_SNAPSHOT_VERSION,
                &snapshot, sizeof(snapshot)) != 0;
            if (scene_geometry_ok) {
                std::string validation_error;
                std::optional<SceneTrackBufferView> own_points;
                std::vector<SceneTrackBufferView> other_points;
                if (snapshot.version != KV_SCENE_GEOMETRY_SNAPSHOT_VERSION ||
                    snapshot.structure_size < sizeof(KvSceneGeometrySnapshot)) {
                    validation_error = "ABI mismatch";
                } else {
                    own_points = scene_track_buffer_view(snapshot.own_track);
                    if (!own_points || own_points->empty() || own_points->cols < 3) {
                        validation_error = "invalid own-track buffer";
                    } else if (snapshot.other_track_count != model.other_tracks.size() ||
                               (snapshot.other_track_count > 0 && !snapshot.other_tracks)) {
                        validation_error = "track count mismatch";
                    }
                }
                if (validation_error.empty()) {
                    other_points.reserve(model.other_tracks.size());
                    for (size_t i = 0; i < model.other_tracks.size(); ++i) {
                        const OtherTrack& track = model.other_tracks[i];
                        const KvSceneTrackRow& input = snapshot.other_tracks[i];
                        const bool key_bounds_valid =
                            input.key.offset <= snapshot.string_size &&
                            input.key.length <= snapshot.string_size - input.key.offset;
                        const bool key_size_valid =
                            input.key.length <= std::numeric_limits<size_t>::max() &&
                            input.key.length == track.key.size();
                        const bool key_matches = key_bounds_valid && key_size_valid &&
                            (input.key.length == 0 ||
                             (snapshot.string_data &&
                              std::memcmp(snapshot.string_data + static_cast<size_t>(input.key.offset),
                                          track.key.data(), track.key.size()) == 0));
                        if (!key_matches) {
                            validation_error = "track order mismatch";
                            break;
                        }
                        std::optional<SceneTrackBufferView> points =
                            scene_track_buffer_view(input.points);
                        if (!points || (!points->empty() && points->cols < 3)) {
                            validation_error = "invalid other-track buffer";
                            break;
                        }
                        other_points.push_back(*points);
                    }
                }
                if (validation_error.empty()) {
                    append_track_path("own", *own_points, true,
                                      ImVec4(0.78f, 0.78f, 0.76f, 1.0f),
                                      options.show_own_track_markers);
                    for (size_t i = 0; i < model.other_tracks.size(); ++i) {
                        const OtherTrack& track = model.other_tracks[i];
                        append_track_path(track.key, other_points[i], false,
                                          track.color, track.visible);
                    }
                    used_scene_geometry = true;
                } else {
                    result.log_messages.push_back(
                        "[warn]canvas3D.cpp: 3D scene snapshot " + validation_error);
                }
            }
        }
        if (!scene_geometry_ok) {
            const char* err = kv_get_last_error();
            result.log_messages.push_back(std::string("[warn]canvas3D.cpp: 3D scene preview adaptive geometry failed: ") +
                                          (err ? err : "geometry failed"));
        }
    }

    if (!used_scene_geometry) {
        if (std::optional<SceneTrackBufferView> own_points = scene_track_buffer_view(model.own)) {
            append_track_path("own", *own_points, true,
                              ImVec4(0.78f, 0.78f, 0.76f, 1.0f),
                              options.show_own_track_markers);
        }
        for (const OtherTrack& track : model.other_tracks) {
            if (std::optional<SceneTrackBufferView> points = scene_track_buffer_view(track.points)) {
                append_track_path(track.key, *points, false, track.color, track.visible);
            }
        }
    }
    populate_canvas3d_scene_route_info(scene, model);
    populate_canvas3d_scene_markers(scene, model);
    populate_canvas3d_scene_fog(scene, model);
    populate_canvas3d_scene_draw_distances(scene, model);
    if (!populate_canvas3d_scene_dynamic_content(scene, model, options.station_index)) {
        result.log_messages.push_back("[warn]canvas3D.cpp: 3D scene preview dynamic content could not be built");
    }
    return result;
}

std::vector<Canvas3DTrackVisibility> build_canvas3d_scene_track_visibility(
    const MapModel& model,
    bool show_own_track_markers) {
    std::vector<Canvas3DTrackVisibility> visibility;
    visibility.reserve(model.other_tracks.size() + 1);
    visibility.push_back(Canvas3DTrackVisibility{"own", show_own_track_markers});
    for (const OtherTrack& track : model.other_tracks) {
        visibility.push_back(Canvas3DTrackVisibility{track.key, track.visible});
    }
    return visibility;
}

struct Canvas3D::Impl {
    explicit Impl(ID3D11Device* device, Canvas3DWakeCallback wake_callback)
        : wake_callback(wake_callback), device(device) {
        if (device) {
            device->AddRef();
            device->GetImmediateContext(&context);
        }
    }

    ~Impl() {
        stop_scene_put_between_preview_worker();
        stop_scene_loader();
        release_scene_resources();
        release_scene_mileage_highlight_resources();
        release_resources();
        release_render_target();
        release_com(scene_input_layout);
        release_com(scene_vertex_shader);
        release_com(scene_pixel_shader);
        release_com(scene_fog_pixel_shader);
        release_com(scene_constant_buffer);
        release_com(scene_marker_input_layout);
        release_com(scene_marker_vertex_shader);
        release_com(scene_marker_pixel_shader);
        release_com(scene_marker_pick_pixel_shader);
        release_com(scene_marker_mask_pixel_shader);
        release_com(scene_marker_constant_buffer);
        release_com(scene_marker_pick_constant_buffer);
        release_com(scene_depth_state);
        release_com(scene_depth_read_state);
        release_com(blend_state);
        release_com(alpha_mask_rasterizer_state);
        release_com(track_rasterizer_state);
        release_com(rasterizer_state);
        release_com(scene_outline_sampler_state);
        release_com(scene_outline_constant_buffer);
        release_com(scene_outline_pixel_shader);
        release_com(scene_outline_vertex_shader);
        release_com(scene_pick_constant_buffer);
        release_com(scene_pick_pixel_shader);
        release_com(sampler_state);
        release_com(context);
        release_com(device);
    }

    bool load_model(const std::string& path, std::string& error) {
        model_load_warnings.clear();
        MlMeshData data = {};
        ModelDataGuard data_guard(loader, data);
        if (!loader.load(path, data, error)) return false;
        data_guard.mark_loaded();

        return upload_model(data, path, error);
    }

    std::vector<std::string> drain_model_load_warnings() {
        std::vector<std::string> warnings;
        warnings.swap(model_load_warnings);
        return warnings;
    }

    bool reload_model(std::string& error) {
        std::string path = model_path_value;
        if (path.empty()) {
            error = "model preview has no model to reload";
            return false;
        }
        const float old_yaw = yaw;
        const float old_pitch = pitch;
        const float old_distance_factor = distance_factor;
        if (!load_model(path, error)) return false;
        yaw = old_yaw;
        pitch = old_pitch;
        distance_factor = old_distance_factor;
        return true;
    }

    bool upload_model(const MlMeshData& data, const std::string& path, std::string& error) {
        if (!device || !context) {
            error = "Direct3D device is not available";
            return false;
        }
        if (data.vertex_count == 0 || data.index_count == 0 || !data.vertices || !data.indices) {
            error = "model contains no renderable data";
            return false;
        }
        if (data.vertex_count > static_cast<size_t>(std::numeric_limits<UINT>::max() / sizeof(GpuVertex)) ||
            data.index_count > static_cast<size_t>(std::numeric_limits<UINT>::max() / sizeof(unsigned int))) {
            error = "model is too large for a Direct3D 11 buffer";
            return false;
        }

        release_resources();
        std::vector<GpuVertex> vertices(data.vertex_count);
        for (size_t i = 0; i < data.vertex_count; ++i) {
            vertices[i] = {
                data.vertices[i].px, data.vertices[i].py, data.vertices[i].pz,
                data.vertices[i].nx, data.vertices[i].ny, data.vertices[i].nz,
                data.vertices[i].u, data.vertices[i].v
            };
        }

        D3D11_BUFFER_DESC vb_desc = {};
        vb_desc.ByteWidth = static_cast<UINT>(vertices.size() * sizeof(GpuVertex));
        vb_desc.Usage = D3D11_USAGE_DEFAULT;
        vb_desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        D3D11_SUBRESOURCE_DATA vb_data = {};
        vb_data.pSysMem = vertices.data();
        HRESULT hr = device->CreateBuffer(&vb_desc, &vb_data, &vertex_buffer);
        if (FAILED(hr)) {
            error = hresult_text("CreateBuffer(vertex)", hr);
            release_resources();
            return false;
        }

        D3D11_BUFFER_DESC ib_desc = {};
        ib_desc.ByteWidth = static_cast<UINT>(data.index_count * sizeof(unsigned int));
        ib_desc.Usage = D3D11_USAGE_DEFAULT;
        ib_desc.BindFlags = D3D11_BIND_INDEX_BUFFER;
        D3D11_SUBRESOURCE_DATA ib_data = {};
        ib_data.pSysMem = data.indices;
        hr = device->CreateBuffer(&ib_desc, &ib_data, &index_buffer);
        if (FAILED(hr)) {
            error = hresult_text("CreateBuffer(index)", hr);
            release_resources();
            return false;
        }

        parts.clear();
        if (data.parts && data.part_count > 0) {
            parts.reserve(data.part_count);
            for (size_t i = 0; i < data.part_count; ++i) {
                parts.push_back({data.parts[i].start_index, data.parts[i].index_count, data.parts[i].material_index});
            }
        } else {
            parts.push_back({0, static_cast<UINT>(data.index_count), 0});
        }

        materials.clear();
        const size_t material_count = std::max<size_t>(data.material_count, 1);
        materials.resize(material_count);
        for (size_t i = 0; i < material_count; ++i) {
            const MlMaterial* src = data.materials && i < data.material_count ? &data.materials[i] : nullptr;
            if (src) {
                materials[i].diffuse[0] = src->diffuse[0];
                materials[i].diffuse[1] = src->diffuse[1];
                materials[i].diffuse[2] = src->diffuse[2];
                materials[i].diffuse[3] = normalize_material_alpha(src->diffuse[3]);
                if (src->texture_path && *src->texture_path) {
                    bool texture_has_alpha = false;
                    std::string texture_error;
                    if (!load_texture(src->texture_path, &materials[i].texture,
                                      texture_error, &texture_has_alpha)) {
                        model_load_warnings.push_back(
                            "[WARN]canvas3D.cpp: model preview texture warning: model=" +
                            path + "; " + texture_error);
                    } else {
                        materials[i].has_texture = true;
                        materials[i].texture_has_alpha = texture_has_alpha;
                    }
                }
            }
        }

        index_count = static_cast<UINT>(data.index_count);
        bounds_min = {data.bounds_min[0], data.bounds_min[1], data.bounds_min[2]};
        bounds_max = {data.bounds_max[0], data.bounds_max[1], data.bounds_max[2]};
        center = {data.center[0], data.center[1], data.center[2]};
        radius = std::max(data.radius, 0.001f);
        model_path_value = path;
        yaw = 0.0f;
        pitch = 0.0f;
        distance_factor = 2.8f;
        return true;
    }

    void clear_model() {
        release_resources();
        model_path_value.clear();
        bounds_min = {};
        bounds_max = {};
        center = {};
        radius = 1.0f;
        yaw = 0.0f;
        pitch = 0.0f;
        distance_factor = 2.8f;
    }

    bool has_model() const {
        return vertex_buffer && index_buffer && index_count > 0;
    }

    bool load_scene(Canvas3DScene scene, std::string& error, bool preserve_loaded_models, bool preserve_camera) {
        if (!device || !context) {
            error = "Direct3D device is not available";
            return false;
        }

        SceneCameraState camera_state;
        if (preserve_camera && scene_active) {
            camera_state.valid = true;
            camera_state.pos = scene_camera_pos;
            camera_state.yaw = scene_camera_yaw;
            camera_state.pitch = scene_camera_pitch;
            camera_state.distance = scene_camera_distance;
        }

        stop_scene_put_between_preview_worker();
        stop_scene_loader();
        if (preserve_loaded_models) {
            release_scene_track_chunks();
            release_scene_marker_chunks();
            scene_chunks.clear();
            clear_pending_scene_model_uploads();
            scene_last_error.clear();
            scene_stats_value = {};
            scene_model_worker_count_value.store(0);
            scene_load_summary_pending = false;
        } else {
            release_scene_resources();
        }
        scene_structure_edit = SceneStructureEditState{};
        scene_placement_locations.clear();
        scene_repeater_locations.clear();
        scene_data = std::move(scene);
        if (++scene_geometry_generation == 0) ++scene_geometry_generation;
        clear_scene_focus_highlight();
        std::sort(scene_data.backgrounds.begin(), scene_data.backgrounds.end(),
                  [](const Canvas3DBackgroundChange& a, const Canvas3DBackgroundChange& b) {
                      return a.distance < b.distance;
                  });
        if (scene_data.min_distance > scene_data.max_distance) {
            std::swap(scene_data.min_distance, scene_data.max_distance);
        }

        if (camera_state.valid) {
            scene_camera_pos = camera_state.pos;
            scene_camera_yaw = camera_state.yaw;
            scene_camera_pitch = camera_state.pitch;
            scene_camera_distance = std::clamp(camera_state.distance, scene_data.min_distance, scene_data.max_distance);
            reset_scene_camera_tracking();
            scene_camera_pitch = camera_state.pitch;
        } else {
            scene_camera_pos = {scene_data.camera.x, scene_data.camera.y, scene_data.camera.z};
            scene_camera_yaw = static_cast<float>(scene_data.camera.yaw);
            scene_camera_pitch = static_cast<float>(scene_data.camera.pitch);
            scene_camera_distance = std::clamp(scene_data.camera.distance, scene_data.min_distance, scene_data.max_distance);
            reset_scene_camera_tracking();
        }
        scene_active = true;
        rebuild_scene_mileage_pick_cache();

        const auto track_gpu_setup_started_at = std::chrono::steady_clock::now();
        if (!build_scene_chunks(error)) {
            clear_scene();
            return false;
        }
        if (!build_scene_track_chunks(error)) {
            clear_scene();
            return false;
        }
        if (!build_scene_marker_chunks(error)) {
            clear_scene();
            return false;
        }
        scene_stats_value.track_gpu_setup_seconds = std::chrono::duration<double>(
            std::chrono::steady_clock::now() - track_gpu_setup_started_at).count();

        const auto model_queue_started_at = std::chrono::steady_clock::now();
        std::map<std::string, SceneModelLoadRequest> requests = collect_scene_model_load_requests();
        std::vector<SceneModelLoadRequest> requests_to_load;
        requests_to_load.reserve(requests.size());
        if (preserve_loaded_models) {
            for (auto it = scene_models.begin(); it != scene_models.end();) {
                if (requests.find(it->first) == requests.end()) {
                    release_scene_model(it->second);
                    it = scene_models.erase(it);
                } else {
                    ++it;
                }
            }
            for (const auto& entry : requests) {
                auto [it, inserted] = scene_models.try_emplace(entry.first);
                if (inserted || it->second.state == SceneModelGpu::State::Pending) {
                    release_scene_model(it->second);
                    it->second = SceneModelGpu{};
                    requests_to_load.push_back(entry.second);
                }
            }
        } else {
            for (const auto& entry : requests) {
                scene_models[entry.first] = SceneModelGpu{};
                requests_to_load.push_back(entry.second);
            }
        }
        scene_stats_value.model_path_count = requests.size();
        scene_stats_value.instance_count = count_scene_instances();
        scene_stats_value.chunk_count = scene_chunks.size();
        scene_stats_value.window_back_m = scene_window_back_m;
        scene_stats_value.window_forward_m = scene_window_forward_m;
        scene_stats_value.camera_distance = scene_camera_distance;
        reset_scene_fps_counter();
        start_scene_model_worker(std::move(requests_to_load));
        scene_stats_value.model_queue_seconds = std::chrono::duration<double>(
            std::chrono::steady_clock::now() - model_queue_started_at).count();
        return true;
    }

    std::vector<SceneModelLoadRequest> reconcile_dynamic_scene_model_requests(
        const std::map<std::string, SceneModelLoadRequest>& requests) {
        const bool request_set_changed = requests.size() != scene_models.size() ||
            std::any_of(scene_models.begin(), scene_models.end(),
                        [&](const auto& entry) {
                            return requests.find(entry.first) == requests.end();
                        });
        if (request_set_changed && scene_worker.joinable()) {
            stop_scene_loader();
            clear_pending_scene_model_uploads();
        }

        std::vector<SceneModelLoadRequest> requests_to_load;
        requests_to_load.reserve(requests.size());
        for (auto it = scene_models.begin(); it != scene_models.end();) {
            if (requests.find(it->first) == requests.end()) {
                release_scene_model(it->second);
                it = scene_models.erase(it);
            } else {
                ++it;
            }
        }
        for (const auto& entry : requests) {
            auto [it, inserted] = scene_models.try_emplace(entry.first);
            if (inserted || it->second.state == SceneModelGpu::State::Pending) {
                release_scene_model(it->second);
                it->second = SceneModelGpu{};
                requests_to_load.push_back(entry.second);
            }
        }
        return requests_to_load;
    }

    bool refresh_scene_dynamic_content(const MapModel& model, int station_index, std::string& error) {
        error.clear();
        if (!scene_active) return true;
        if (!device || !context) {
            error = "Direct3D device is not available";
            return false;
        }
        if (scene_data.tracks.empty() || scene_chunks.empty() || scene_track_chunks.empty()) {
            error = "3D scene preview has no reusable track geometry";
            return false;
        }

        stop_scene_put_between_preview_worker();
        clear_scene_placement_edit_target();

        std::vector<Canvas3DSceneObject> old_objects = std::move(scene_data.objects);
        std::vector<Canvas3DModelInstance> old_instances = std::move(scene_data.instances);
        std::vector<Canvas3DRepeaterSegment> old_repeaters = std::move(scene_data.repeaters);
        std::vector<Canvas3DBackgroundChange> old_backgrounds = std::move(scene_data.backgrounds);
        Canvas3DCameraStart old_camera = scene_data.camera;
        const double old_min_distance = scene_data.min_distance;
        const double old_max_distance = scene_data.max_distance;
        std::vector<SceneChunk> old_chunks = std::move(scene_chunks);
        auto old_structure_locations = scene_placement_locations;
        auto old_repeater_locations = scene_repeater_locations;

        auto restore_dynamic_content = [&]() {
            scene_data.objects = std::move(old_objects);
            scene_data.instances = std::move(old_instances);
            scene_data.repeaters = std::move(old_repeaters);
            scene_data.backgrounds = std::move(old_backgrounds);
            scene_data.camera = old_camera;
            scene_data.min_distance = old_min_distance;
            scene_data.max_distance = old_max_distance;
            scene_chunks = std::move(old_chunks);
            scene_placement_locations = std::move(old_structure_locations);
            scene_repeater_locations = std::move(old_repeater_locations);
        };

        if (!populate_canvas3d_scene_dynamic_content(scene_data, model, station_index)) {
            restore_dynamic_content();
            error = "failed to rebuild 3D scene dynamic content";
            return false;
        }
        scene_data.camera = old_camera;
        std::sort(scene_data.backgrounds.begin(), scene_data.backgrounds.end(),
                  [](const Canvas3DBackgroundChange& a, const Canvas3DBackgroundChange& b) {
                      return a.distance < b.distance;
                  });
        if (scene_data.min_distance > scene_data.max_distance) {
            std::swap(scene_data.min_distance, scene_data.max_distance);
        }

        if (!build_scene_chunks(error)) {
            restore_dynamic_content();
            return false;
        }
        auto same_chunk_signature = [](const std::vector<SceneChunk>& a,
                                       const std::vector<SceneChunk>& b) {
            if (a.size() != b.size()) return false;
            for (size_t i = 0; i < a.size(); ++i) {
                if (std::abs(a[i].d_min - b[i].d_min) > 1e-6 ||
                    std::abs(a[i].d_max - b[i].d_max) > 1e-6 ||
                    std::abs(a[i].origin.x - b[i].origin.x) > 1e-6 ||
                    std::abs(a[i].origin.y - b[i].origin.y) > 1e-6 ||
                    std::abs(a[i].origin.z - b[i].origin.z) > 1e-6) {
                    return false;
                }
            }
            return true;
        };
        if (!same_chunk_signature(scene_chunks, old_chunks) ||
            scene_track_chunks.size() != scene_chunks.size()) {
            restore_dynamic_content();
            error = "3D scene preview chunk layout changed";
            return false;
        }

        clear_scene_focus_highlight();
        scene_hovered_object_index = -1;
        scene_hovered_marker_index = -1;
        scene_context_object_index = -1;
        scene_context_marker_index = -1;
        scene_hover_highlight_batch.clear();

        std::map<std::string, SceneModelLoadRequest> requests = collect_scene_model_load_requests();
        std::vector<SceneModelLoadRequest> requests_to_load =
            reconcile_dynamic_scene_model_requests(requests);

        scene_stats_value.model_path_count = requests.size();
        scene_stats_value.instance_count = count_scene_instances();
        scene_stats_value.chunk_count = scene_chunks.size();
        scene_stats_value.window_back_m = scene_window_back_m;
        scene_stats_value.window_forward_m = scene_window_forward_m;
        scene_stats_value.camera_distance = scene_camera_distance;
        reset_scene_fps_counter();
        start_scene_model_worker(std::move(requests_to_load));
        return true;
    }

    bool refresh_scene_map_content(const MapModel& model,
                                   const Canvas3DSceneMapRefreshOptions& options,
                                   std::string& error) {
        error.clear();
        if (!scene_active) return true;
        if (options.route_stations) {
            populate_canvas3d_scene_route_stations(scene_data.route_info, model);
        }
        if (options.fog) populate_canvas3d_scene_fog(scene_data, model);
        if (options.draw_distances) {
            populate_canvas3d_scene_draw_distances(scene_data, model);
        }
        if (options.speed_limits) {
            populate_canvas3d_scene_speed_limits(scene_data.route_info, model);
        }
        if (options.section_signals) {
            populate_canvas3d_scene_section_signals(scene_data.route_info, model);
        }
        if (options.markers) {
            populate_canvas3d_scene_markers(scene_data, model);
            release_scene_marker_chunks();
            if (!build_scene_marker_chunks(error)) {
                if (!error.empty()) scene_last_error = error;
                release_scene_marker_chunks();
                return false;
            }
            if (scene_structure_edit.active &&
                scene_structure_edit.kind == Canvas3DSceneEditKind::Sound3D) {
                scene_structure_edit = SceneStructureEditState{};
            }
        }
        return true;
    }

    bool refresh_scene_route_stations(const MapModel& model, std::string& error) {
        Canvas3DSceneMapRefreshOptions options;
        options.route_stations = true;
        options.markers = true;
        return refresh_scene_map_content(model, options, error);
    }

    void clear_scene() {
        stop_scene_put_between_preview_worker();
        stop_scene_loader();
        release_scene_resources();
        scene_data = {};
        scene_active = false;
        scene_structure_edit = SceneStructureEditState{};
        scene_placement_locations.clear();
        scene_repeater_locations.clear();
        scene_rotating = false;
        scene_hovered_object_index = -1;
        scene_hovered_marker_index = -1;
        scene_hovered_mileage.reset();
        scene_context_mileage.reset();
        scene_context_object_index = -1;
        scene_context_marker_index = -1;
        scene_hover_highlight_batch.clear();
        clear_scene_focus_highlight();
        scene_stats_value = {};
        scene_model_worker_count_value.store(0);
        reset_scene_fps_counter();
    }

    bool has_scene() const {
        return scene_active;
    }

    bool reload_scene_models(std::string& error) {
        if (!scene_active) {
            error = "3D scene preview is not started";
            return false;
        }
        stop_scene_put_between_preview_worker();
        stop_scene_loader();
        clear_pending_scene_model_uploads();
        std::map<std::string, SceneModelLoadRequest> requests = collect_scene_model_load_requests();
        std::vector<SceneModelLoadRequest> requests_to_load;
        requests_to_load.reserve(requests.size());
        for (auto& kv : scene_models) {
            release_scene_model(kv.second);
            kv.second = SceneModelGpu{};
            auto request_it = requests.find(kv.first);
            if (request_it != requests.end()) requests_to_load.push_back(request_it->second);
        }
        release_scene_texture_cache();
        scene_last_error.clear();
        scene_stats_value.model_path_count = scene_models.size();
        scene_load_summary_pending = false;
        start_scene_model_worker(std::move(requests_to_load));
        return true;
    }

    bool set_scene_track_visibility(const std::vector<Canvas3DTrackVisibility>& visibility, std::string& error) {
        if (!scene_active) return true;

        std::map<std::string, bool> visible_by_key;
        for (const Canvas3DTrackVisibility& item : visibility) {
            visible_by_key[normalize_track_lookup_key(item.key)] = item.visible;
        }

        bool changed = false;
        for (Canvas3DTrackPath& path : scene_data.tracks) {
            auto it = visible_by_key.find(normalize_track_lookup_key(path.key));
            if (it == visible_by_key.end() || path.visible == it->second) continue;
            path.visible = it->second;
            changed = true;
        }
        if (!changed) return true;

        release_scene_track_chunks();
        if (!build_scene_track_chunks(error)) {
            if (!error.empty()) scene_last_error = error;
            release_scene_track_chunks();
            return false;
        }
        if (!rebuild_scene_marker_visible_indices(error)) {
            if (!error.empty()) scene_last_error = error;
            return false;
        }
        return true;
    }

    void set_scene_window(double back_m, double forward_m) {
        if (std::isfinite(back_m) && back_m >= 0.0) scene_window_back_m = back_m;
        if (std::isfinite(forward_m) && forward_m > 0.0) scene_window_forward_m = forward_m;
        scene_stats_value.window_back_m = scene_window_back_m;
        scene_stats_value.window_forward_m = scene_window_forward_m;
    }

    void set_scene_edit_component_scale(float scale) {
        if (!std::isfinite(scale)) scale = 1.0f;
        scene_edit_component_scale = std::clamp(scale, 0.5f, 5.0f);
    }

    void set_scene_interaction_mode(Canvas3DSceneInteractionMode mode) {
        if (scene_interaction_mode == mode) return;
        scene_interaction_mode = mode;
        scene_rotating = false;
        scene_hovered_object_index = -1;
        scene_hovered_marker_index = -1;
        scene_hovered_mileage.reset();
        scene_context_mileage.reset();
        scene_context_object_index = -1;
        scene_context_marker_index = -1;
        scene_hover_highlight_batch.clear();
    }

    void set_scene_fog_enabled(bool enabled) {
        scene_fog_enabled = enabled;
    }

    void set_scene_map_draw_distance_enabled(bool enabled) {
        scene_map_draw_distance_enabled = enabled;
    }

    void set_scene_camera_speed_percent(int percent) {
        scene_camera_speed_percent = std::clamp(percent, 50, 400);
    }

    void set_scene_performance_warning(bool enabled,
                                       size_t warning_threshold,
                                       size_t critical_warning_threshold) {
        scene_performance_warning_enabled = enabled;
        scene_instance_warning_threshold = warning_threshold;
        scene_instance_critical_warning_threshold =
            std::max(warning_threshold, critical_warning_threshold);
    }

#ifndef NDEBUG
    Canvas3DSceneFogDebugState debug_scene_fog_state() const {
        Canvas3DSceneFogDebugState state;
        state.keyframe_count = scene_data.fog_keyframes.size();
        state.fog_draw_part_count = debug_scene_fog_draw_part_count;
        state.setting_enabled = scene_fog_enabled;
        state.shader_ready = scene_fog_pixel_shader != nullptr;
        state.camera_distance = scene_camera_distance;
        const SceneFogSample sample = sample_canvas3d_scene_fog(
            scene_data.fog_keyframes, scene_camera_distance, scene_fog_enabled);
        state.sampled_enabled = sample.enabled;
        state.density = sample.density;
        state.color = sample.color;
        for (const Canvas3DSceneFogKeyframe& keyframe : scene_data.fog_keyframes) {
            if (keyframe.density > state.max_density) {
                state.max_density = keyframe.density;
                state.max_density_distance = keyframe.distance;
            }
        }
        return state;
    }

    bool debug_read_scene_render_pixels(std::vector<std::uint8_t>& rgba,
                                        int& width, int& height,
                                        std::string& error) {
        rgba.clear();
        width = 0;
        height = 0;
        if (!device || !context || !render_texture) {
            error = "3D scene render target is not available";
            return false;
        }

        D3D11_TEXTURE2D_DESC desc = {};
        render_texture->GetDesc(&desc);
        D3D11_TEXTURE2D_DESC staging_desc = desc;
        staging_desc.Usage = D3D11_USAGE_STAGING;
        staging_desc.BindFlags = 0;
        staging_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        staging_desc.MiscFlags = 0;
        ID3D11Texture2D* staging = nullptr;
        HRESULT hr = device->CreateTexture2D(&staging_desc, nullptr, &staging);
        if (FAILED(hr)) {
            error = hresult_text("CreateTexture2D(scene debug readback)", hr);
            return false;
        }

        context->CopyResource(staging, render_texture);
        D3D11_MAPPED_SUBRESOURCE mapped = {};
        hr = context->Map(staging, 0, D3D11_MAP_READ, 0, &mapped);
        if (FAILED(hr)) {
            error = hresult_text("Map(scene debug readback)", hr);
            release_com(staging);
            return false;
        }

        width = static_cast<int>(desc.Width);
        height = static_cast<int>(desc.Height);
        const size_t row_bytes = static_cast<size_t>(desc.Width) * 4;
        rgba.resize(row_bytes * static_cast<size_t>(desc.Height));
        for (UINT y = 0; y < desc.Height; ++y) {
            std::memcpy(rgba.data() + static_cast<size_t>(y) * row_bytes,
                        static_cast<const std::uint8_t*>(mapped.pData) +
                            static_cast<size_t>(y) * mapped.RowPitch,
                        row_bytes);
        }
        context->Unmap(staging, 0);
        release_com(staging);
        return true;
    }
#endif

    Canvas3DSceneInteractionMode scene_interaction_mode_value() const {
        return scene_interaction_mode;
    }

    Canvas3DSceneStats scene_stats() const {
        Canvas3DSceneStats stats = scene_stats_value;
        stats.active = scene_active;
        stats.camera_distance = scene_camera_distance;
        stats.chunk_count = scene_chunks.size();
        stats.model_path_count = scene_models.size();
        stats.instance_count = scene_stats_value.instance_count;
        stats.model_worker_count = scene_model_worker_count_value.load();
        stats.window_back_m = scene_window_back_m;
        stats.window_forward_m = scene_window_forward_m;
        stats.model_ready_count = 0;
        stats.model_failed_count = 0;
        for (const auto& kv : scene_models) {
            if (kv.second.state == SceneModelGpu::State::Ready) ++stats.model_ready_count;
            if (kv.second.state == SceneModelGpu::State::Failed) ++stats.model_failed_count;
        }
        const size_t completed_count = stats.model_ready_count + stats.model_failed_count;
        stats.loading = scene_active &&
            (scene_worker_running.load() || completed_count < stats.model_path_count);
        return stats;
    }

    std::vector<std::string> drain_scene_load_messages() {
        std::vector<std::string> messages;
        std::lock_guard<std::mutex> lock(scene_log_mutex);
        messages.swap(scene_pending_logs);
        return messages;
    }

    Canvas3DSceneCameraPose scene_camera_pose() const {
        Canvas3DSceneCameraPose pose;
        if (!scene_active) return pose;
        pose.valid = true;
        pose.distance = scene_camera_distance;
        pose.x = -scene_camera_pos.z;
        pose.y = scene_camera_pos.x;
        pose.z = scene_camera_pos.y;
        pose.theta = scene_camera_yaw;
        pose.pitch = scene_camera_pitch;
        return pose;
    }

    size_t count_scene_instances() const {
        size_t count = scene_data.instances.size();
        for (const Canvas3DRepeaterSegment& repeater : scene_data.repeaters) {
            count += scene_repeater_instance_count(repeater);
        }
        return count;
    }

    std::map<std::string, SceneModelLoadRequest> collect_scene_model_load_requests() const {
        std::map<std::string, SceneModelLoadRequest> requests;
        auto note_regular_model = [&](const std::string& path) {
            if (path.empty() || requests.find(path) != requests.end()) return;
            SceneModelLoadRequest request;
            request.key = path;
            request.source_path = path;
            requests.emplace(path, std::move(request));
        };

        for (const Canvas3DModelInstance& instance : scene_data.instances) {
            note_regular_model(instance.model_path);
            if (!instance.put_between || instance.model_path.empty()) continue;

            SceneModelLoadRequest request;
            request.key = scene_model_key_for_instance(instance, scene_geometry_generation);
            request.source_path = instance.model_path;
            request.put_between.enabled = true;
            request.put_between.distance = instance.distance;
            request.put_between.flag = instance.put_between_flag & 1;
            request.put_between.origin = {instance.world[12], instance.world[13], instance.world[14]};
            request.put_between.own_track = own_track_path();
            request.put_between.track1 = placement_track_path_for_key(instance.put_between_track_key1);
            request.put_between.track2 = placement_track_path_for_key(instance.put_between_track_key2);
            const std::string request_key = request.key;
            requests.emplace(request_key, std::move(request));
        }
        for (const Canvas3DSceneObject& object : scene_data.objects) {
            for (const Canvas3DSceneModelOption& option : object.model_options) {
                note_regular_model(option.model_path);
            }
        }
        for (const Canvas3DBackgroundChange& bg : scene_data.backgrounds) {
            note_regular_model(bg.model_path);
        }
        for (const Canvas3DRepeaterSegment& repeater : scene_data.repeaters) {
            for (const std::string& path : repeater.model_paths) {
                note_regular_model(path);
            }
        }
        return requests;
    }

    bool set_scene_object_model_option(int object_index, size_t option_index) {
        if (!scene_object_index_valid(object_index)) return false;
        Canvas3DSceneObject& object = scene_data.objects[static_cast<size_t>(object_index)];
        if (option_index >= object.model_options.size()) return false;
        const Canvas3DSceneModelOption& option = object.model_options[option_index];
        if (option.model_path.empty()) return false;

        object.selected_model_option = option_index;
        for (Canvas3DModelInstance& source : scene_data.instances) {
            if (source.object_index == object_index) source.model_path = option.model_path;
        }
        for (SceneChunk& chunk : scene_chunks) {
            for (SceneInstance& instance : chunk.instances) {
                if (instance.object_index == object_index) instance.model_path = option.model_path;
            }
        }
        if (scene_models.find(option.model_path) == scene_models.end()) {
            scene_models[option.model_path] = SceneModelGpu{};
            scene_stats_value.model_path_count = scene_models.size();
            SceneModelLoadRequest request;
            request.key = option.model_path;
            request.source_path = option.model_path;
            start_scene_model_worker({std::move(request)});
        }
        scene_hover_highlight_batch.clear();
        return true;
    }

    static bool same_placement_edit_target(const Canvas3DPlacementEditTarget& a,
                                           const Canvas3DPlacementEditTarget& b) {
        return a.kind == b.kind && a.edit_id == b.edit_id &&
            a.model_path == b.model_path && a.track_key == b.track_key &&
            a.put_between_track_key1 == b.put_between_track_key1 &&
            a.put_between_track_key2 == b.put_between_track_key2 &&
            a.put_between_flag == b.put_between_flag &&
            a.distance == b.distance &&
            a.placement_distance_gizmo == b.placement_distance_gizmo &&
            a.has_repeater_end_distance == b.has_repeater_end_distance &&
            a.repeater_end_distance == b.repeater_end_distance &&
            a.x == b.x && a.y == b.y && a.z == b.z &&
            a.rx == b.rx && a.ry == b.ry && a.rz == b.rz &&
            a.tilt == b.tilt && a.span == b.span;
    }

    const std::string* placement_edit_id_for_object(int object_index) const {
        if (!scene_object_index_valid(object_index)) return nullptr;
        const Canvas3DSceneObject& object = scene_data.objects[static_cast<size_t>(object_index)];
        const bool editable_placement =
            object.kind == Canvas3DSceneObjectKind::Structure ||
            object.kind == Canvas3DSceneObjectKind::Signal;
        if (!editable_placement || object.edit_id.empty()) {
            return nullptr;
        }
        return &object.edit_id;
    }

    size_t scene_chunk_index_for_distance(double distance) const {
        if (scene_chunks.empty()) return 0;
        const double first = scene_chunks.front().d_min;
        const double relative = (distance - first) / scene_chunk_m;
        if (!std::isfinite(relative)) {
            return relative > 0.0 ? scene_chunks.size() - 1 : 0;
        }
        if (relative <= 0.0) return 0;
        if (relative >= static_cast<double>(scene_chunks.size())) {
            return scene_chunks.size() - 1;
        }
        return static_cast<size_t>(std::floor(relative));
    }

    Canvas3DModelInstance placement_instance_from_target(
        const Canvas3DPlacementEditTarget& target,
        const Canvas3DModelInstance& base) const {
        Canvas3DModelInstance desired = base;
        desired.track_key = target.track_key;
        desired.distance = target.distance;
        desired.follow_track = true;
        desired.put_between = false;
        desired.x = target.x;
        desired.y = target.y;
        desired.z = target.z;
        desired.rx = target.rx;
        desired.ry = target.ry;
        desired.rz = target.rz;
        desired.tilt = target.tilt;
        desired.span = target.span;
        return desired;
    }

    Canvas3DModelInstance put_between_instance_from_target(
        const Canvas3DPlacementEditTarget& target,
        const Canvas3DModelInstance& base) const {
        Canvas3DModelInstance desired = base;
        desired.model_path = target.model_path;
        desired.distance = target.distance;
        desired.follow_track = false;
        desired.put_between = true;
        desired.put_between_track_key1 = target.put_between_track_key1;
        desired.put_between_track_key2 = target.put_between_track_key2;
        desired.put_between_flag = target.put_between_flag & 1;
        return desired;
    }

    Canvas3DPlacementEditTarget put_between_target_from_instance(
        const std::string& edit_id,
        const Canvas3DModelInstance& instance) const {
        Canvas3DPlacementEditTarget target;
        target.kind = Canvas3DSceneEditKind::StructurePutBetween;
        target.edit_id = edit_id;
        target.model_path = instance.model_path;
        target.put_between_track_key1 = instance.put_between_track_key1;
        target.put_between_track_key2 = instance.put_between_track_key2;
        target.put_between_flag = instance.put_between_flag & 1;
        target.distance = instance.distance;
        return target;
    }

    Canvas3DRepeaterSegment repeater_segment_from_target(
        const Canvas3DPlacementEditTarget& target,
        const Canvas3DRepeaterSegment& base) const {
        Canvas3DRepeaterSegment desired = base;
        desired.track_key = target.track_key;
        desired.begin_distance = target.distance;
        if (target.has_repeater_end_distance) {
            desired.end_distance = target.repeater_end_distance;
            desired.has_end_or_change_position = true;
        }
        desired.x = target.x;
        desired.y = target.y;
        desired.z = target.z;
        desired.rx = target.rx;
        desired.ry = target.ry;
        desired.rz = target.rz;
        desired.tilt = target.tilt;
        desired.span = target.span;
        return desired;
    }

    bool replace_scene_placement_instance(const std::string& edit_id,
                                          Canvas3DModelInstance desired,
                                          const std::string& render_model_path) {
        auto location_it = scene_placement_locations.find(edit_id);
        if (location_it == scene_placement_locations.end() || scene_chunks.empty()) return false;
        ScenePlacementInstanceLocation location = location_it->second;
        if (location.source_index >= scene_data.instances.size() ||
            location.chunk_index >= scene_chunks.size() ||
            location.chunk_instance_index >= scene_chunks[location.chunk_index].instances.size()) {
            return false;
        }

        SceneInstance replacement;
        replacement.model_path = render_model_path;
        replacement.distance = desired.distance;
        replacement.object_index = desired.object_index;
        std::copy(desired.world, desired.world + 16, replacement.world);

        const size_t destination_chunk_index = scene_chunk_index_for_distance(desired.distance);
        if (destination_chunk_index == location.chunk_index) {
            scene_chunks[location.chunk_index].instances[location.chunk_instance_index] =
                std::move(replacement);
        } else {
            SceneChunk& old_chunk = scene_chunks[location.chunk_index];
            const size_t last_index = old_chunk.instances.size() - 1;
            if (location.chunk_instance_index != last_index) {
                old_chunk.instances[location.chunk_instance_index] =
                    std::move(old_chunk.instances[last_index]);
                const SceneInstance& moved = old_chunk.instances[location.chunk_instance_index];
                if (const std::string* moved_edit_id = placement_edit_id_for_object(moved.object_index)) {
                    auto moved_location = scene_placement_locations.find(*moved_edit_id);
                    if (moved_location != scene_placement_locations.end()) {
                        moved_location->second.chunk_index = location.chunk_index;
                        moved_location->second.chunk_instance_index = location.chunk_instance_index;
                    }
                }
            }
            old_chunk.instances.pop_back();

            SceneChunk& destination = scene_chunks[destination_chunk_index];
            location.chunk_index = destination_chunk_index;
            location.chunk_instance_index = destination.instances.size();
            destination.instances.push_back(std::move(replacement));
            scene_placement_locations[edit_id] = location;
        }

        scene_data.instances[location.source_index] = desired;
        if (scene_focus_highlight_object_index == desired.object_index) {
            scene_focus_highlight_model_path = render_model_path.empty()
                ? desired.model_path : render_model_path;
            std::copy(desired.world, desired.world + 16, scene_focus_highlight_world);
        }
        return true;
    }

    bool write_scene_placement_instance(const std::string& edit_id,
                                        Canvas3DModelInstance desired) {
        const std::string placement_track_key = desired.track_key;
        const double placement_distance = desired.distance;
        StructurePlacementFrame frame;
        if (!make_track_placement_frame(desired.track_key, desired.distance,
                                        desired.x, desired.y, desired.z,
                                        desired.rx, desired.ry, desired.rz,
                                        desired.tilt, desired.span, frame)) {
            return false;
        }
        store_world(desired.world, frame.model_right, frame.model_up,
                    frame.model_forward, frame.origin);
        const std::string render_model_path =
            scene_model_key_for_instance(desired, scene_geometry_generation);
        if (!replace_scene_placement_instance(
                edit_id, std::move(desired), render_model_path)) {
            return false;
        }
        if (scene_structure_edit.active && scene_structure_edit.edit_id == edit_id) {
            if (scene_structure_edit.current.placement_distance_gizmo) {
                track_distance_gizmo_frame(
                    placement_track_key, placement_distance,
                    scene_structure_edit.placement_gizmo);
            } else {
                scene_structure_edit.placement_gizmo.origin = frame.origin;
                scene_structure_edit.placement_gizmo.axes = frame.parameter_axes;
            }
        }
        return true;
    }

    bool put_between_edit_frame(double distance, DVec3& origin,
                                std::array<DVec3, 3>& axes) const {
        Canvas3DTrackPoint point;
        if (!sample_own_track(distance, point)) return false;
        origin = {point.x, point.y, point.z};
        const double gradient = std::isfinite(point.gradient)
            ? point.gradient / 1000.0 : 0.0;
        axes = {};
        axes[2] = normalize(DVec3{
            std::sin(point.theta), gradient, -std::cos(point.theta)});
        return true;
    }

    bool write_scene_put_between_instance(const std::string& edit_id,
                                          Canvas3DModelInstance desired,
                                          const std::string& render_model_path) {
        DVec3 origin;
        std::array<DVec3, 3> axes{};
        if (!put_between_edit_frame(desired.distance, origin, axes)) return false;
        std::fill(std::begin(desired.world), std::end(desired.world), 0.0);
        desired.world[0] = 1.0;
        desired.world[5] = 1.0;
        desired.world[10] = 1.0;
        desired.world[15] = 1.0;
        desired.world[12] = origin.x;
        desired.world[13] = origin.y;
        desired.world[14] = origin.z;
        if (!replace_scene_placement_instance(
                edit_id, std::move(desired), render_model_path)) {
            return false;
        }
        if (scene_structure_edit.active && scene_structure_edit.edit_id == edit_id) {
            scene_structure_edit.placement_gizmo.origin = origin;
            scene_structure_edit.placement_gizmo.axes = axes;
        }
        return true;
    }

    bool track_distance_gizmo_frame(const std::string& track_key,
                                    double distance,
                                    SceneGizmoHandle& gizmo) const {
        gizmo.enabled = {{false, false, true}};
        gizmo.axes = {};
        const Canvas3DTrackPath* path = placement_track_path_for_key(track_key);
        if (!path || path->points.empty()) return false;

        const double path_begin = path->points.front().distance;
        const double path_end = path->points.back().distance;
        if (!std::isfinite(path_begin) || !std::isfinite(path_end)) return false;
        const double range_min = std::min(path_begin, path_end);
        const double range_max = std::max(path_begin, path_end);
        const double sample_distance = std::clamp(
            distance, range_min, range_max);
        const std::optional<Canvas3DTrackPoint> center =
            scene_sample_track_path_points(*path, sample_distance);
        if (!center) return false;
        gizmo.origin = {center->x, center->y, center->z};

        double low_distance = std::max(range_min, sample_distance - 0.5);
        double high_distance = std::min(range_max, sample_distance + 0.5);
        if (high_distance - low_distance < k_scene_repeater_distance_epsilon) {
            low_distance = std::max(range_min, sample_distance - 1.0);
            high_distance = std::min(range_max, sample_distance + 1.0);
        }
        const double distance_span = high_distance - low_distance;
        if (!std::isfinite(distance_span) ||
            distance_span < k_scene_repeater_distance_epsilon) {
            return false;
        }
        const std::optional<Canvas3DTrackPoint> low =
            scene_sample_track_path_points(*path, low_distance);
        const std::optional<Canvas3DTrackPoint> high =
            scene_sample_track_path_points(*path, high_distance);
        if (!low || !high) return false;
        const DVec3 distance_axis = DVec3{
            high->x - low->x,
            high->y - low->y,
            high->z - low->z,
        } * (1.0 / distance_span);
        if (!std::isfinite(distance_axis.x) ||
            !std::isfinite(distance_axis.y) ||
            !std::isfinite(distance_axis.z) ||
            dot(distance_axis, distance_axis) <= 1e-12) {
            return false;
        }
        gizmo.axes[2] = distance_axis;
        return true;
    }

    bool sound3d_gizmo_frame(const Canvas3DPlacementEditTarget& target,
                             SceneGizmoHandle& gizmo) const {
        if (!track_distance_gizmo_frame({}, target.distance, gizmo)) return false;
        Canvas3DTrackPoint track_point;
        if (!sample_own_track(target.distance, track_point)) return false;
        DVec3 right;
        DVec3 up;
        DVec3 forward;
        scene_track_surface_frame(track_point, right, up, forward);
        const Canvas3DTrackPoint source = scene_sound3d_source_point(
            track_point, target.x, target.y);
        gizmo.origin = {source.x, source.y, source.z};
        gizmo.axes[0] = right;
        gizmo.axes[1] = up;
        gizmo.enabled = {{true, true, true}};
        return true;
    }

    bool sound3d_marker_baseline_target(
        const Canvas3DPlacementEditTarget& target,
        Canvas3DPlacementEditTarget& baseline) const {
        const auto index_it = scene_sound3d_marker_indices.find(target.edit_id);
        if (index_it == scene_sound3d_marker_indices.end() ||
            index_it->second >= scene_data.markers.size()) {
            return false;
        }
        const Canvas3DSceneMarker& marker = scene_data.markers[index_it->second];
        if (marker.kind != MapMarkerVisualKind::MapSound3D) return false;
        Canvas3DTrackPoint track_point;
        if (!sample_own_track(marker.track_point.distance, track_point)) return false;
        DVec3 right;
        DVec3 up;
        DVec3 forward;
        scene_track_surface_frame(track_point, right, up, forward);
        const DVec3 offset =
            DVec3{marker.track_point.x, marker.track_point.y, marker.track_point.z} -
            DVec3{track_point.x, track_point.y, track_point.z};
        baseline = target;
        baseline.distance = track_point.distance;
        baseline.x = dot(offset, right);
        baseline.y = dot(offset, up);
        return true;
    }

    bool update_scene_sound3d_marker(const std::string& edit_id,
                                     double distance,
                                     double x,
                                     double y) {
        const auto index_it = scene_sound3d_marker_indices.find(edit_id);
        if (index_it == scene_sound3d_marker_indices.end() ||
            index_it->second >= scene_data.markers.size()) {
            return false;
        }
        const size_t marker_index = index_it->second;
        Canvas3DTrackPoint track_point;
        if (!sample_own_track(distance, track_point)) return false;
        Canvas3DSceneMarker replacement = scene_data.markers[marker_index];
        if (replacement.kind != MapMarkerVisualKind::MapSound3D) return false;
        replacement.track_point = scene_sound3d_source_point(track_point, x, y);

        if (marker_index >= scene_marker_locations.size()) {
            scene_data.markers[marker_index] = std::move(replacement);
            return true;
        }
        const SceneMarkerGpuLocation location =
            scene_marker_locations[marker_index];
        if (!location.valid || location.chunk_index >= scene_marker_chunks.size()) {
            scene_data.markers[marker_index] = std::move(replacement);
            return true;
        }

        const size_t destination_chunk = scene_chunk_index_for_distance(
            replacement.track_point.distance);
        if (destination_chunk != location.chunk_index ||
            !scene_marker_font_cache_current()) {
            scene_data.markers[marker_index] = std::move(replacement);
            sort_canvas3d_scene_markers(scene_data.markers);
            std::string error;
            if (build_scene_marker_chunks(error)) return true;
            if (!error.empty()) scene_last_error = error;
            return false;
        }

        SceneMarkerChunkGpu& chunk = scene_marker_chunks[location.chunk_index];
        if (!context || !chunk.vertex_buffer || !scene_marker_font ||
            location.range_index + location.range_count > chunk.ranges.size()) {
            return false;
        }
        ImFontBaked* baked = scene_marker_font->GetFontBaked(scene_marker_font_size);
        if (!baked) return false;

        std::vector<SceneMarkerVertex> vertices;
        std::vector<unsigned int> indices;
        std::vector<SceneMarkerIndexRange> ranges;
        const SceneMarkerGeometrySpan span = append_scene_marker_geometry(
            replacement, marker_index, chunk.origin, 0.0f,
            *scene_marker_font, *baked, scene_marker_font_size,
            vertices, indices, ranges);
        if (span.vertex_count != location.vertex_count ||
            span.range_count != location.range_count ||
            vertices.empty()) {
            return false;
        }
        for (size_t range_index = 0; range_index < ranges.size(); ++range_index) {
            const SceneMarkerIndexRange& previous =
                chunk.ranges[location.range_index + range_index];
            const SceneMarkerIndexRange& updated = ranges[range_index];
            if (previous.kind != updated.kind ||
                previous.marker_index != updated.marker_index ||
                previous.label != updated.label ||
                previous.count != updated.count) {
                return false;
            }
        }
        const size_t byte_first = location.vertex_first * sizeof(SceneMarkerVertex);
        const size_t byte_end =
            (location.vertex_first + location.vertex_count) * sizeof(SceneMarkerVertex);
        if (byte_end > static_cast<size_t>(std::numeric_limits<UINT>::max())) {
            return false;
        }
        D3D11_BOX box = {};
        box.left = static_cast<UINT>(byte_first);
        box.right = static_cast<UINT>(byte_end);
        box.top = 0;
        box.bottom = 1;
        box.front = 0;
        box.back = 1;
        context->UpdateSubresource(
            chunk.vertex_buffer, 0, &box, vertices.data(), 0, 0);
        for (size_t range_index = 0; range_index < ranges.size(); ++range_index) {
            SceneMarkerIndexRange& destination =
                chunk.ranges[location.range_index + range_index];
            const SceneMarkerIndexRange& source = ranges[range_index];
            destination.center = source.center;
            destination.right = source.right;
            destination.up = source.up;
        }
        scene_data.markers[marker_index] = std::move(replacement);
        return true;
    }

    std::optional<std::pair<size_t, size_t>> scene_repeater_chunk_range(
        const Canvas3DRepeaterSegment& repeater) const {
        if (scene_chunks.empty()) return std::nullopt;
        double first_distance = 0.0;
        double last_distance = 0.0;
        if (!scene_repeater_render_distance_span(
                repeater, first_distance, last_distance)) {
            return std::nullopt;
        }
        const size_t first_index = scene_chunk_index_for_distance(first_distance);
        const size_t last_index = scene_chunk_index_for_distance(last_distance);
        return std::pair<size_t, size_t>{
            std::min(first_index, last_index),
            std::max(first_index, last_index),
        };
    }

    void update_scene_repeater_chunk_membership(
        size_t repeater_index,
        const std::optional<std::pair<size_t, size_t>>& old_range,
        const std::optional<std::pair<size_t, size_t>>& new_range) {
        const auto contains = [](const std::optional<std::pair<size_t, size_t>>& range,
                                 size_t index) {
            return range && index >= range->first && index <= range->second;
        };
        const auto visit_range = [&](const std::pair<size_t, size_t>& range,
                                     const auto& visitor) {
            for (size_t index = range.first;; ++index) {
                visitor(index);
                if (index == range.second) break;
            }
        };
        if (old_range) {
            visit_range(*old_range, [&](size_t chunk_index) {
                if (contains(new_range, chunk_index) ||
                    chunk_index >= scene_chunks.size()) {
                    return;
                }
                std::vector<size_t>& indices = scene_chunks[chunk_index].repeater_indices;
                const auto found = std::lower_bound(
                    indices.begin(), indices.end(), repeater_index);
                if (found != indices.end() && *found == repeater_index) {
                    indices.erase(found);
                }
            });
        }
        if (new_range) {
            visit_range(*new_range, [&](size_t chunk_index) {
                if (contains(old_range, chunk_index) ||
                    chunk_index >= scene_chunks.size()) {
                    return;
                }
                std::vector<size_t>& indices = scene_chunks[chunk_index].repeater_indices;
                const auto insertion = std::lower_bound(
                    indices.begin(), indices.end(), repeater_index);
                if (insertion == indices.end() || *insertion != repeater_index) {
                    indices.insert(insertion, repeater_index);
                }
            });
        }
    }

    bool write_scene_repeater_segment(const std::string& edit_id,
                                      Canvas3DRepeaterSegment desired) {
        const auto location_it = scene_repeater_locations.find(edit_id);
        if (location_it == scene_repeater_locations.end() ||
            location_it->second >= scene_data.repeaters.size() ||
            desired.model_paths.empty() || desired.model_paths.front().empty()) {
            return false;
        }
        const size_t repeater_index = location_it->second;
        const Canvas3DRepeaterSegment& current = scene_data.repeaters[repeater_index];
        StructurePlacementFrame frame;
        if (!make_track_placement_frame(desired.track_key, desired.begin_distance,
                                        desired.x, desired.y, desired.z,
                                        desired.rx, desired.ry, desired.rz,
                                        desired.tilt, desired.span, frame)) {
            return false;
        }
        const std::optional<std::pair<size_t, size_t>> old_range =
            scene_repeater_chunk_range(current);
        const std::optional<std::pair<size_t, size_t>> new_range =
            scene_repeater_chunk_range(desired);
        const size_t old_instance_count = scene_repeater_instance_count(current);
        const size_t new_instance_count = scene_repeater_instance_count(desired);
        update_scene_repeater_chunk_membership(repeater_index, old_range, new_range);
        scene_data.repeaters[repeater_index] = std::move(desired);
        if (scene_stats_value.instance_count >= old_instance_count) {
            const size_t non_repeater_count =
                scene_stats_value.instance_count - old_instance_count;
            if (new_instance_count <=
                std::numeric_limits<size_t>::max() - non_repeater_count) {
                scene_stats_value.instance_count = non_repeater_count + new_instance_count;
            } else {
                scene_stats_value.instance_count = std::numeric_limits<size_t>::max();
            }
        } else {
            scene_stats_value.instance_count = count_scene_instances();
        }
        if (scene_structure_edit.active &&
            scene_structure_edit.kind == Canvas3DSceneEditKind::Repeater &&
            scene_structure_edit.edit_id == edit_id) {
            const Canvas3DRepeaterSegment& updated =
                scene_data.repeaters[repeater_index];
            if (scene_structure_edit.current.placement_distance_gizmo) {
                track_distance_gizmo_frame(
                    updated.track_key, updated.begin_distance,
                    scene_structure_edit.placement_gizmo);
            } else {
                scene_structure_edit.placement_gizmo.origin = frame.origin;
                scene_structure_edit.placement_gizmo.axes = frame.parameter_axes;
            }
            track_distance_gizmo_frame(
                updated.track_key, updated.end_distance,
                scene_structure_edit.repeater_end_gizmo);
        }
        return true;
    }

    bool scene_source_model_path_in_use(const std::string& path) const {
        if (path.empty()) return false;
        for (const Canvas3DModelInstance& instance : scene_data.instances) {
            if (instance.model_path == path) return true;
        }
        for (const Canvas3DSceneObject& object : scene_data.objects) {
            for (const Canvas3DSceneModelOption& option : object.model_options) {
                if (option.model_path == path) return true;
            }
        }
        for (const Canvas3DBackgroundChange& background : scene_data.backgrounds) {
            if (background.model_path == path) return true;
        }
        for (const Canvas3DRepeaterSegment& repeater : scene_data.repeaters) {
            if (std::find(repeater.model_paths.begin(), repeater.model_paths.end(), path) !=
                repeater.model_paths.end()) {
                return true;
            }
        }
        return false;
    }

    void release_unused_put_between_preview_base_models() {
        for (auto it = scene_put_between_preview_base_model_keys.begin();
             it != scene_put_between_preview_base_model_keys.end();) {
            if (scene_source_model_path_in_use(*it)) {
                ++it;
                continue;
            }
            auto model = scene_models.find(*it);
            if (model != scene_models.end()) {
                release_scene_model(model->second);
                scene_models.erase(model);
            }
            it = scene_put_between_preview_base_model_keys.erase(it);
        }
    }

    void clear_scene_placement_edit_target() {
        if (!scene_structure_edit.active) return;
        const std::string edit_id = scene_structure_edit.edit_id;
        Canvas3DModelInstance baseline = scene_structure_edit.baseline_instance;
        Canvas3DRepeaterSegment repeater_baseline = scene_structure_edit.baseline_repeater;
        const Canvas3DSceneEditKind kind = scene_structure_edit.kind;
        const Canvas3DPlacementEditTarget completed = scene_structure_edit.completed;
        const std::string preview_model_key = scene_structure_edit.preview_model_key;
        if (kind == Canvas3DSceneEditKind::StructurePutBetween) {
            stop_scene_put_between_preview_worker();
        }
        scene_structure_edit = SceneStructureEditState{};
        if (kind == Canvas3DSceneEditKind::Repeater) {
            write_scene_repeater_segment(edit_id, std::move(repeater_baseline));
        } else if (kind == Canvas3DSceneEditKind::Sound3D) {
            update_scene_sound3d_marker(
                edit_id, completed.distance, completed.x, completed.y);
        } else if (kind == Canvas3DSceneEditKind::StructurePutBetween) {
            std::string render_model_key =
                scene_model_key_for_instance(baseline, scene_geometry_generation);
            const Canvas3DPlacementEditTarget baseline_target =
                put_between_target_from_instance(edit_id, baseline);
            if (!preview_model_key.empty() &&
                same_placement_edit_target(completed, baseline_target)) {
                auto preview = scene_models.find(preview_model_key);
                if (preview != scene_models.end() && preview_model_key != render_model_key) {
                    auto existing = scene_models.find(render_model_key);
                    if (existing != scene_models.end()) {
                        release_scene_model(existing->second);
                        scene_models.erase(existing);
                    }
                    auto node = scene_models.extract(preview);
                    node.key() = render_model_key;
                    scene_models.insert(std::move(node));
                }
            }
            write_scene_put_between_instance(
                edit_id, std::move(baseline), render_model_key);
            if (!preview_model_key.empty() && preview_model_key != render_model_key) {
                auto preview = scene_models.find(preview_model_key);
                if (preview != scene_models.end()) {
                    release_scene_model(preview->second);
                    scene_models.erase(preview);
                }
            }
            release_unused_put_between_preview_base_models();
        } else {
            write_scene_placement_instance(edit_id, std::move(baseline));
        }
    }

    void cancel_scene_gizmo_interaction(SceneGizmoTarget target) {
        if (scene_structure_edit.hovered_gizmo == target) {
            scene_structure_edit.hovered_gizmo = SceneGizmoTarget::None;
            scene_structure_edit.hovered_axis = Canvas3DSceneDragAxis::None;
        }
        if (scene_structure_edit.dragging_gizmo == target) {
            scene_structure_edit.dragging_gizmo = SceneGizmoTarget::None;
            scene_structure_edit.dragging_axis = Canvas3DSceneDragAxis::None;
        }
    }

    bool set_scene_placement_edit_target(const Canvas3DPlacementEditTarget& target,
                                         bool show_gizmo) {
        if (!scene_active || target.edit_id.empty()) return false;
        if (target.kind != Canvas3DSceneEditKind::Structure &&
            target.kind != Canvas3DSceneEditKind::StructurePutBetween &&
            target.kind != Canvas3DSceneEditKind::Signal &&
            target.kind != Canvas3DSceneEditKind::Sound3D) {
            return false;
        }
        if (scene_structure_edit.active &&
            (scene_structure_edit.kind != target.kind ||
             scene_structure_edit.edit_id != target.edit_id)) {
            clear_scene_placement_edit_target();
        }
        if (target.kind == Canvas3DSceneEditKind::Sound3D) {
            if (!scene_structure_edit.active) {
                Canvas3DPlacementEditTarget baseline;
                if (!sound3d_marker_baseline_target(target, baseline)) return false;
                scene_structure_edit.active = true;
                scene_structure_edit.kind = Canvas3DSceneEditKind::Sound3D;
                scene_structure_edit.edit_id = target.edit_id;
                scene_structure_edit.completed = std::move(baseline);
            } else if (same_placement_edit_target(scene_structure_edit.current, target) &&
                       scene_structure_edit.show_gizmo == show_gizmo) {
                return true;
            }
            SceneGizmoHandle gizmo;
            if (!sound3d_gizmo_frame(target, gizmo) ||
                !update_scene_sound3d_marker(
                    target.edit_id, target.distance, target.x, target.y)) {
                if (scene_structure_edit.current.edit_id.empty()) {
                    scene_structure_edit = SceneStructureEditState{};
                }
                return false;
            }
            scene_structure_edit.current = target;
            scene_structure_edit.show_gizmo = show_gizmo;
            scene_structure_edit.placement_gizmo = gizmo;
            if (!show_gizmo) {
                cancel_scene_gizmo_interaction(SceneGizmoTarget::Placement);
            }
            return true;
        }
        scene_structure_edit.placement_gizmo.enabled =
            target.kind == Canvas3DSceneEditKind::StructurePutBetween ||
            target.placement_distance_gizmo
                ? std::array<bool, 3>{{false, false, true}}
                : std::array<bool, 3>{{true, true, true}};

        auto location_it = scene_placement_locations.find(target.edit_id);
        if (location_it == scene_placement_locations.end() ||
            location_it->second.source_index >= scene_data.instances.size()) {
            return false;
        }

        if (!scene_structure_edit.active) {
            scene_structure_edit.active = true;
            scene_structure_edit.kind = target.kind;
            scene_structure_edit.edit_id = target.edit_id;
            scene_structure_edit.baseline_instance =
                scene_data.instances[location_it->second.source_index];
            if (target.kind == Canvas3DSceneEditKind::StructurePutBetween) {
                if (!scene_structure_edit.baseline_instance.put_between) {
                    scene_structure_edit = SceneStructureEditState{};
                    return false;
                }
                scene_structure_edit.current = put_between_target_from_instance(
                    target.edit_id, scene_structure_edit.baseline_instance);
                scene_structure_edit.completed = scene_structure_edit.current;
                if (!put_between_edit_frame(
                        scene_structure_edit.baseline_instance.distance,
                        scene_structure_edit.placement_gizmo.origin,
                        scene_structure_edit.placement_gizmo.axes)) {
                    scene_structure_edit = SceneStructureEditState{};
                    return false;
                }
            }
        } else if (same_placement_edit_target(scene_structure_edit.current, target) &&
                   scene_structure_edit.show_gizmo == show_gizmo) {
            return true;
        }

        if (target.kind == Canvas3DSceneEditKind::StructurePutBetween) {
            if (target.model_path.empty()) return false;
            scene_structure_edit.show_gizmo = show_gizmo;
            if (same_placement_edit_target(scene_structure_edit.current, target)) {
                return true;
            }
            if (same_placement_edit_target(scene_structure_edit.completed, target)) {
                std::lock_guard<std::mutex> lock(scene_put_between_preview_mutex);
                const std::uint64_t sequence =
                    ++scene_put_between_preview_next_sequence;
                scene_put_between_preview_latest_sequence = sequence;
                scene_put_between_preview_pending.reset();
                scene_put_between_preview_completed.reset();
                scene_structure_edit.current = target;
                scene_structure_edit.preview_sequence = sequence;
                return true;
            }
            const std::uint64_t sequence = queue_scene_put_between_preview(target);
            if (sequence == 0) return false;
            scene_structure_edit.current = target;
            scene_structure_edit.preview_sequence = sequence;
            if (!show_gizmo) {
                cancel_scene_gizmo_interaction(SceneGizmoTarget::Placement);
            }
            return true;
        }

        Canvas3DModelInstance desired = placement_instance_from_target(
            target, scene_structure_edit.baseline_instance);
        if (!write_scene_placement_instance(target.edit_id, std::move(desired))) {
            if (scene_structure_edit.current.edit_id.empty()) {
                scene_structure_edit = SceneStructureEditState{};
            }
            return false;
        }
        scene_structure_edit.current = target;
        scene_structure_edit.show_gizmo = show_gizmo;
        if (target.placement_distance_gizmo) {
            track_distance_gizmo_frame(
                target.track_key, target.distance,
                scene_structure_edit.placement_gizmo);
        }
        if (!show_gizmo) {
            cancel_scene_gizmo_interaction(SceneGizmoTarget::Placement);
        }
        return true;
    }

    bool set_scene_repeater_edit_target(const Canvas3DPlacementEditTarget& input,
                                        bool show_gizmo) {
        if (!scene_active || input.edit_id.empty()) return false;
        Canvas3DPlacementEditTarget target = input;
        target.kind = Canvas3DSceneEditKind::Repeater;
        if (scene_structure_edit.active &&
            (scene_structure_edit.kind != Canvas3DSceneEditKind::Repeater ||
             scene_structure_edit.edit_id != target.edit_id)) {
            clear_scene_placement_edit_target();
        }
        scene_structure_edit.placement_gizmo.enabled = target.placement_distance_gizmo
            ? std::array<bool, 3>{{false, false, true}}
            : std::array<bool, 3>{{true, true, true}};
        scene_structure_edit.repeater_end_gizmo.enabled = {{false, false, true}};

        const auto location_it = scene_repeater_locations.find(target.edit_id);
        if (location_it == scene_repeater_locations.end() ||
            location_it->second >= scene_data.repeaters.size()) {
            return false;
        }
        const Canvas3DRepeaterSegment& current = scene_data.repeaters[location_it->second];
        if (current.model_paths.empty() || current.model_paths.front().empty()) return false;

        if (!scene_structure_edit.active) {
            scene_structure_edit.active = true;
            scene_structure_edit.kind = Canvas3DSceneEditKind::Repeater;
            scene_structure_edit.edit_id = target.edit_id;
            scene_structure_edit.baseline_repeater = current;
        } else if (same_placement_edit_target(scene_structure_edit.current, target) &&
                   scene_structure_edit.show_gizmo == show_gizmo) {
            return true;
        }

        const Canvas3DRepeaterSegment desired = repeater_segment_from_target(
            target, scene_structure_edit.baseline_repeater);
        if (desired.track_key != scene_structure_edit.baseline_repeater.track_key ||
            (!target.placement_distance_gizmo &&
             desired.begin_distance != scene_structure_edit.baseline_repeater.begin_distance)) {
            clear_scene_placement_edit_target();
            return false;
        }
        if (!write_scene_repeater_segment(target.edit_id, desired)) {
            if (scene_structure_edit.current.edit_id.empty()) {
                scene_structure_edit = SceneStructureEditState{};
            }
            return false;
        }
        scene_structure_edit.current = target;
        scene_structure_edit.show_gizmo = show_gizmo;
        if (target.placement_distance_gizmo) {
            track_distance_gizmo_frame(
                target.track_key, target.distance,
                scene_structure_edit.placement_gizmo);
        }
        if (!show_gizmo) {
            cancel_scene_gizmo_interaction(SceneGizmoTarget::Placement);
        }
        if (!target.has_repeater_end_distance) {
            cancel_scene_gizmo_interaction(SceneGizmoTarget::RepeaterEndDistance);
        }
        return true;
    }

    bool update_scene_placement_instance(const Canvas3DPlacementEditTarget& target) {
        if (!scene_active) return true;
        if (target.kind != Canvas3DSceneEditKind::Structure &&
            target.kind != Canvas3DSceneEditKind::StructurePutBetween &&
            target.kind != Canvas3DSceneEditKind::Signal) {
            return false;
        }
        auto location_it = scene_placement_locations.find(target.edit_id);
        if (location_it == scene_placement_locations.end() ||
            location_it->second.source_index >= scene_data.instances.size()) {
            return false;
        }
        if (target.kind == Canvas3DSceneEditKind::StructurePutBetween) {
            if (!scene_structure_edit.active ||
                scene_structure_edit.kind != Canvas3DSceneEditKind::StructurePutBetween ||
                scene_structure_edit.edit_id != target.edit_id ||
                !same_placement_edit_target(scene_structure_edit.completed, target)) {
                return false;
            }
            scene_structure_edit.baseline_instance =
                scene_data.instances[location_it->second.source_index];
            scene_structure_edit.current = target;
            return true;
        }
        const Canvas3DModelInstance& base = scene_structure_edit.active &&
            scene_structure_edit.edit_id == target.edit_id
            ? scene_structure_edit.baseline_instance
            : scene_data.instances[location_it->second.source_index];
        Canvas3DModelInstance desired = placement_instance_from_target(target, base);
        if (!write_scene_placement_instance(target.edit_id, std::move(desired))) return false;

        if (scene_structure_edit.active && scene_structure_edit.edit_id == target.edit_id) {
            auto updated_location = scene_placement_locations.find(target.edit_id);
            if (updated_location != scene_placement_locations.end() &&
                updated_location->second.source_index < scene_data.instances.size()) {
                scene_structure_edit.baseline_instance =
                    scene_data.instances[updated_location->second.source_index];
            }
            scene_structure_edit.current = target;
        }
        return true;
    }

    bool update_scene_repeater_segment(const Canvas3DPlacementEditTarget& input) {
        if (!scene_active) return true;
        Canvas3DPlacementEditTarget target = input;
        target.kind = Canvas3DSceneEditKind::Repeater;
        const auto location_it = scene_repeater_locations.find(target.edit_id);
        if (location_it == scene_repeater_locations.end() ||
            location_it->second >= scene_data.repeaters.size()) {
            return false;
        }
        const Canvas3DRepeaterSegment& base = scene_structure_edit.active &&
            scene_structure_edit.kind == Canvas3DSceneEditKind::Repeater &&
            scene_structure_edit.edit_id == target.edit_id
            ? scene_structure_edit.baseline_repeater
            : scene_data.repeaters[location_it->second];
        const Canvas3DRepeaterSegment desired = repeater_segment_from_target(target, base);
        if (desired.track_key != base.track_key || desired.begin_distance != base.begin_distance ||
            !write_scene_repeater_segment(target.edit_id, desired)) {
            return false;
        }
        if (scene_structure_edit.active &&
            scene_structure_edit.kind == Canvas3DSceneEditKind::Repeater &&
            scene_structure_edit.edit_id == target.edit_id) {
            const auto updated = scene_repeater_locations.find(target.edit_id);
            if (updated != scene_repeater_locations.end() &&
                updated->second < scene_data.repeaters.size()) {
                scene_structure_edit.baseline_repeater = scene_data.repeaters[updated->second];
            }
            scene_structure_edit.current = target;
        }
        return true;
    }

    struct SceneObjectJumpTarget {
        int object_index = -1;
        double distance = 0.0;
        std::string model_path;
        double world[16] = {
            1.0, 0.0, 0.0, 0.0,
            0.0, 1.0, 0.0, 0.0,
            0.0, 0.0, 1.0, 0.0,
            0.0, 0.0, 0.0, 1.0
        };
        DVec3 center;
    };

    bool scene_model_center_world(const std::string& model_path, const double world[16], DVec3& out) const {
        auto model_it = scene_models.find(model_path);
        if (model_it == scene_models.end() || model_it->second.state != SceneModelGpu::State::Ready) return false;
        out = transform_point_row(world, model_it->second.center);
        return true;
    }

    bool find_placed_object_jump_target(Canvas3DSceneObjectKind kind,
                                        size_t source_row,
                                        SceneObjectJumpTarget& target) const {
        for (const SceneChunk& chunk : scene_chunks) {
            for (const SceneInstance& instance : chunk.instances) {
                if (!scene_object_index_valid(instance.object_index)) continue;
                const Canvas3DSceneObject& object = scene_data.objects[static_cast<size_t>(instance.object_index)];
                if (object.kind != kind || object.source_row != source_row) continue;
                if (!scene_model_center_world(instance.model_path, instance.world, target.center)) return false;
                target.object_index = instance.object_index;
                target.distance = instance.distance;
                target.model_path = instance.model_path;
                std::copy(instance.world, instance.world + 16, target.world);
                return true;
            }
        }
        return false;
    }

    const Canvas3DRepeaterSegment* find_repeater_segment(size_t source_row) const {
        for (const Canvas3DRepeaterSegment& repeater : scene_data.repeaters) {
            if (!scene_object_index_valid(repeater.object_index)) continue;
            const Canvas3DSceneObject& object = scene_data.objects[static_cast<size_t>(repeater.object_index)];
            if (object.kind == Canvas3DSceneObjectKind::Repeater && object.source_row == source_row) {
                return &repeater;
            }
        }
        return nullptr;
    }

    bool find_repeater_jump_target(size_t source_row, SceneObjectJumpTarget& target) const {
        const Canvas3DRepeaterSegment* repeater = find_repeater_segment(source_row);
        if (!repeater || repeater->model_paths.empty()) return false;
        const std::string& model_path = repeater->model_paths.front();
        if (model_path.empty()) return false;
        double world[16] = {};
        if (!make_repeater_instance_world(*repeater, repeater->begin_distance, world)) return false;
        target.object_index = repeater->object_index;
        target.distance = repeater->begin_distance;
        target.model_path = model_path;
        std::copy(world, world + 16, target.world);
        if (!scene_model_center_world(model_path, world, target.center)) {
            target.center = {world[12], world[13], world[14]};
        }
        return true;
    }

    bool find_repeater_end_or_change_jump_target(size_t source_row,
                                                 SceneObjectJumpTarget& target) const {
        const Canvas3DRepeaterSegment* repeater = find_repeater_segment(source_row);
        if (!repeater || !repeater->has_end_or_change_position) return false;

        double last_instance_distance = 0.0;
        size_t last_model_index = 0;
        if (!scene_repeater_last_instance(*repeater, last_instance_distance, last_model_index)) return false;

        const std::string& model_path = repeater->model_paths[last_model_index];
        double last_instance_world[16] = {};
        if (model_path.empty() ||
            !make_repeater_instance_world(*repeater, last_instance_distance, last_instance_world)) {
            return false;
        }

        double boundary_world[16] = {};
        if (!make_repeater_instance_world(*repeater, repeater->end_distance, boundary_world)) return false;

        target.object_index = repeater->object_index;
        target.distance = repeater->end_distance;
        target.model_path = model_path;
        std::copy(last_instance_world, last_instance_world + 16, target.world);
        if (!scene_model_center_world(model_path, last_instance_world, target.center)) {
            target.center = {boundary_world[12], boundary_world[13], boundary_world[14]};
        }
        return true;
    }

    bool find_scene_object_jump_target(Canvas3DSceneObjectKind kind,
                                       size_t source_row,
                                       SceneObjectJumpTarget& target) const {
        if (kind == Canvas3DSceneObjectKind::Structure ||
            kind == Canvas3DSceneObjectKind::Signal) {
            return find_placed_object_jump_target(kind, source_row, target);
        }
        if (kind == Canvas3DSceneObjectKind::Repeater) return find_repeater_jump_target(source_row, target);
        return false;
    }

    void clear_scene_focus_highlight() {
        scene_focus_highlight_object_index = -1;
        scene_focus_highlight_marker_index = -1;
        scene_focus_highlight_model_path.clear();
        scene_focus_highlight_until = {};
        scene_focus_highlight_batch.clear();
    }

    void start_scene_focus_highlight(int object_index, const std::string& model_path, const double world[16]) {
        scene_focus_highlight_batch.clear();
        scene_focus_highlight_object_index = object_index;
        scene_focus_highlight_marker_index = -1;
        scene_focus_highlight_model_path = model_path;
        std::copy(world, world + 16, scene_focus_highlight_world);
        scene_focus_highlight_until =
            std::chrono::steady_clock::now() +
            std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                std::chrono::duration<double>(k_scene_focus_highlight_seconds));
    }

    bool scene_focus_highlight_active_now() {
        if (scene_focus_highlight_object_index < 0 && scene_focus_highlight_marker_index < 0) return false;
        if (std::chrono::steady_clock::now() < scene_focus_highlight_until) return true;
        clear_scene_focus_highlight();
        return false;
    }

    void start_scene_marker_focus_highlight(size_t marker_index) {
        scene_focus_highlight_batch.clear();
        scene_focus_highlight_object_index = -1;
        scene_focus_highlight_marker_index = static_cast<int>(marker_index);
        scene_focus_highlight_model_path.clear();
        scene_focus_highlight_until =
            std::chrono::steady_clock::now() +
            std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                std::chrono::duration<double>(k_scene_focus_highlight_seconds));
    }

    bool update_scene_focus_highlight_batch(DVec3 render_origin,
                                            const Mat4& view_proj,
                                            int width,
                                            int height) {
        scene_focus_highlight_batch.clear();
        if (!scene_focus_highlight_active_now() ||
            !scene_object_index_valid(scene_focus_highlight_object_index)) {
            return false;
        }
        auto model_it = scene_models.find(scene_focus_highlight_model_path);
        if (model_it == scene_models.end() || model_it->second.state != SceneModelGpu::State::Ready) return false;

        SceneScreenBounds bounds;
        if (!compute_scene_instance_screen_bounds(scene_focus_highlight_world,
                                                  model_it->second,
                                                  render_origin,
                                                  view_proj,
                                                  width,
                                                  height,
                                                  bounds)) {
            return false;
        }

        scene_focus_highlight_batch.instances.push_back(SceneHighlightInstance{
            scene_focus_highlight_model_path,
            make_instance_data_relative(scene_focus_highlight_world, render_origin),
            bounds.screen_min,
            bounds.screen_max
        });
        scene_focus_highlight_batch.screen_min = bounds.screen_min;
        scene_focus_highlight_batch.screen_max = bounds.screen_max;
        scene_focus_highlight_batch.object_index = scene_focus_highlight_object_index;
        return true;
    }

    bool set_scene_camera_for_target(double distance, DVec3 target_center) {
        scene_camera_distance = std::clamp(distance - k_scene_object_jump_back_m,
                                           scene_data.min_distance,
                                           scene_data.max_distance);
        scene_camera_lateral_offset = 0.0;
        scene_camera_vertical_offset = k_default_scene_camera_height;
        scene_camera_yaw_offset = 0.0f;
        scene_camera_pitch = 0.0f;
        scene_rotating = false;
        if (!update_scene_camera_from_owntrack()) return false;

        DVec3 to_target = target_center - scene_camera_pos;
        double len_sq = dot(to_target, to_target);
        if (len_sq > 1e-12) {
            double len = std::sqrt(len_sq);
            scene_camera_yaw = static_cast<float>(std::atan2(to_target.x, -to_target.z));
            scene_camera_pitch = static_cast<float>(
                std::clamp(std::asin(std::clamp(to_target.y / len, -1.0, 1.0)), -1.45, 1.45));
            Canvas3DTrackPoint point;
            if (sample_own_track(scene_camera_distance, point)) {
                scene_camera_yaw_offset = scene_camera_yaw - static_cast<float>(point.theta);
            }
        }

        return true;
    }

    bool jump_scene_camera_to_object(Canvas3DSceneObjectKind kind, size_t source_row) {
        if (!scene_active) return false;

        SceneObjectJumpTarget target;
        if (!find_scene_object_jump_target(kind, source_row, target) ||
            !set_scene_camera_for_target(target.distance, target.center)) {
            return false;
        }

        start_scene_focus_highlight(target.object_index, target.model_path, target.world);
        return true;
    }

    bool jump_scene_camera_to_marker(Canvas3DSceneMarkerListKind list_kind,
                                     size_t row_index) {
        if (!scene_active || !scene_marker_list_kind_is_navigable(list_kind)) return false;
        const size_t list_slot = scene_marker_list_kind_slot(list_kind);
        if (list_slot >= scene_marker_target_indices.size()) return false;
        const std::vector<size_t>& target_indices = scene_marker_target_indices[list_slot];
        if (row_index >= target_indices.size()) return false;
        const size_t marker_index = target_indices[row_index];
        if (marker_index == k_scene_marker_target_missing ||
            marker_index >= scene_data.markers.size()) {
            return false;
        }
        const Canvas3DSceneMarker& marker = scene_data.markers[marker_index];
        if (marker.list_kind != list_kind || !marker.row_index ||
            *marker.row_index != row_index ||
            !set_scene_camera_for_target(
                marker.track_point.distance,
                DVec3{marker.track_point.x, marker.track_point.y, marker.track_point.z})) {
            return false;
        }
        start_scene_marker_focus_highlight(marker_index);
        return true;
    }

    bool jump_scene_camera_to_repeater_end_or_change(size_t source_row) {
        if (!scene_active) return false;
        SceneObjectJumpTarget target;
        if (!find_repeater_end_or_change_jump_target(source_row, target) ||
            !set_scene_camera_for_target(target.distance, target.center)) {
            return false;
        }

        start_scene_focus_highlight(target.object_index, target.model_path, target.world);
        return true;
    }

    bool load_texture(const std::string& path,
                      ID3D11ShaderResourceView** out_srv,
                      std::string& error,
                      bool* out_has_alpha = nullptr) {
        if (!out_srv) return false;
        *out_srv = nullptr;
        if (out_has_alpha) *out_has_alpha = false;
        IWICImagingFactory* factory = nullptr;
        IWICBitmapDecoder* decoder = nullptr;
        IWICBitmapFrameDecode* frame = nullptr;
        IWICFormatConverter* converter = nullptr;
        UINT width = 0;
        UINT height = 0;
        UINT row_stride = 0;
        size_t pixel_bytes = 0;
        std::vector<unsigned char> pixels;

        HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
        if (FAILED(hr)) {
            error = hresult_text("CoCreateInstance(WIC)", hr);
            goto fail;
        }
        hr = factory->CreateDecoderFromFilename(utf8_to_wide(path).c_str(), nullptr, GENERIC_READ,
                                                WICDecodeMetadataCacheOnLoad, &decoder);
        if (FAILED(hr)) {
            const auto failure =
                kme::maploader::classify_file_open_failure(
                    kme::maploader::path_from_utf8(path));
            if (failure == kme::maploader::FileOpenFailureKind::Missing) {
                error = "Texture file not found at specified path: " + path;
            } else if (failure ==
                       kme::maploader::FileOpenFailureKind::ExistsButCannotOpen) {
                error = "Texture file exists but cannot be opened or decoded: " + path;
            } else {
                error = "Texture file status could not be determined: " + path;
            }
            goto fail;
        }
        hr = decoder->GetFrame(0, &frame);
        if (FAILED(hr)) {
            error = "Texture file exists but cannot be opened or decoded: " + path;
            goto fail;
        }
        hr = factory->CreateFormatConverter(&converter);
        if (FAILED(hr)) {
            error = hresult_text("CreateFormatConverter", hr);
            goto fail;
        }
        hr = converter->Initialize(frame, GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone,
                                   nullptr, 0.0, WICBitmapPaletteTypeCustom);
        if (FAILED(hr)) {
            error = "Texture file exists but cannot be opened or decoded: " + path;
            goto fail;
        }
        hr = converter->GetSize(&width, &height);
        if (FAILED(hr)) {
            error = "failed to read texture dimensions: " + path;
            goto fail;
        }
        if (width == 0 || height == 0) {
            error = "texture has invalid size: " + path;
            goto fail;
        }
        if (width > D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION ||
            height > D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION ||
            width > std::numeric_limits<UINT>::max() / 4) {
            error = "texture dimensions exceed the Direct3D 11 limit: " + path;
            goto fail;
        }
        row_stride = width * 4;
        if (height > std::numeric_limits<size_t>::max() / row_stride) {
            error = "texture byte size overflows the host address space: " + path;
            goto fail;
        }
        pixel_bytes = static_cast<size_t>(row_stride) * height;
        if (pixel_bytes > std::numeric_limits<UINT>::max()) {
            error = "texture byte size exceeds the WIC copy limit: " + path;
            goto fail;
        }
        pixels.resize(pixel_bytes);
        hr = converter->CopyPixels(
            nullptr, row_stride, static_cast<UINT>(pixel_bytes), pixels.data());
        if (FAILED(hr)) {
            error = "failed to copy texture pixels: " + path;
            goto fail;
        }
        if (out_has_alpha) *out_has_alpha = texture_pixels_have_alpha(pixels);

        {
            D3D11_TEXTURE2D_DESC desc = {};
            desc.Width = width;
            desc.Height = height;
            desc.MipLevels = 1;
            desc.ArraySize = 1;
            desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            desc.SampleDesc.Count = 1;
            desc.Usage = D3D11_USAGE_DEFAULT;
            desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            D3D11_SUBRESOURCE_DATA sub = {};
            sub.pSysMem = pixels.data();
            sub.SysMemPitch = row_stride;
            ID3D11Texture2D* texture = nullptr;
            hr = device->CreateTexture2D(&desc, &sub, &texture);
            if (FAILED(hr)) {
                error = hresult_text("CreateTexture2D(texture)", hr);
                goto fail;
            }
            D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
            srv_desc.Format = desc.Format;
            srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
            srv_desc.Texture2D.MipLevels = 1;
            hr = device->CreateShaderResourceView(texture, &srv_desc, out_srv);
            texture->Release();
            if (FAILED(hr)) {
                error = hresult_text("CreateShaderResourceView(texture)", hr);
                goto fail;
            }
        }

        release_com(converter);
        release_com(frame);
        release_com(decoder);
        release_com(factory);
        return true;

fail:
        release_com(*out_srv);
        if (out_has_alpha) *out_has_alpha = false;
        release_com(converter);
        release_com(frame);
        release_com(decoder);
        release_com(factory);
        return false;
    }

    bool load_scene_texture(const std::string& path,
                            ID3D11ShaderResourceView** out_srv,
                            std::string& error,
                            bool* out_has_alpha = nullptr) {
        if (!scene_texture_cache_enabled) return load_texture(path, out_srv, error, out_has_alpha);
        if (!out_srv) return false;
        *out_srv = nullptr;
        if (out_has_alpha) *out_has_alpha = false;

        const std::string key = normalized_texture_cache_key(path);
        auto cached = scene_texture_cache.find(key);
        if (cached != scene_texture_cache.end()) {
            ++scene_stats_value.texture_cache_hit_count;
            if (cached->second.failed) {
                error = cached->second.error;
                return false;
            }
            if (cached->second.texture) {
                cached->second.texture->AddRef();
                *out_srv = cached->second.texture;
                if (out_has_alpha) *out_has_alpha = cached->second.has_alpha;
                return true;
            }
        }

        ++scene_stats_value.texture_cache_miss_count;
        bool has_alpha = false;
        if (!load_texture(path, out_srv, error, &has_alpha)) {
            SceneTextureCacheEntry entry;
            entry.failed = true;
            entry.error = error;
            scene_texture_cache.emplace(key, std::move(entry));
            return false;
        }

        SceneTextureCacheEntry entry;
        entry.texture = *out_srv;
        entry.has_alpha = has_alpha;
        entry.texture->AddRef();
        scene_texture_cache.emplace(key, entry);
        if (out_has_alpha) *out_has_alpha = has_alpha;
        return true;
    }

    void release_scene_texture_cache() {
        for (auto& entry : scene_texture_cache) release_com(entry.second.texture);
        scene_texture_cache.clear();
        scene_texture_warning_keys.clear();
    }

    void release_scene_model(SceneModelGpu& model) {
        for (GpuMaterial& material : model.materials) {
            release_com(material.texture);
            material.has_texture = false;
            material.texture_has_alpha = false;
        }
        model.materials.clear();
        model.parts.clear();
        release_com(model.vertex_buffer);
        release_com(model.index_buffer);
        release_com(model.instance_buffer);
        model.vertex_capacity = 0;
        model.dynamic_vertices = false;
        model.shared_model_key.clear();
        model.instance_capacity = 0;
        model.index_count = 0;
        model.center = {};
        model.radius = 1.0f;
    }

    void release_track_chunk(SceneTrackChunkGpu& chunk) {
        for (GpuMaterial& material : chunk.materials) {
            release_com(material.texture);
            material.has_texture = false;
            material.texture_has_alpha = false;
        }
        chunk.materials.clear();
        chunk.parts.clear();
        release_com(chunk.vertex_buffer);
        release_com(chunk.index_buffer);
        release_com(chunk.instance_buffer);
        chunk.instance_capacity = 0;
        chunk.index_count = 0;
    }

    void release_scene_track_chunks() {
        for (SceneTrackChunkGpu& chunk : scene_track_chunks) release_track_chunk(chunk);
        scene_track_chunks.clear();
    }

    static void release_marker_chunk(SceneMarkerChunkGpu& chunk) {
        release_com(chunk.vertex_buffer);
        release_com(chunk.index_buffer);
        release_com(chunk.pick_index_buffer);
        chunk.source_indices.clear();
        chunk.ranges.clear();
        chunk.visible_index_count = 0;
        chunk.visible_pick_index_count = 0;
    }

    void release_scene_marker_chunks() {
        for (SceneMarkerChunkGpu& chunk : scene_marker_chunks) release_marker_chunk(chunk);
        scene_marker_chunks.clear();
        scene_marker_locations.clear();
        scene_sound3d_marker_indices.clear();
        for (std::vector<size_t>& target_indices : scene_marker_target_indices) {
            target_indices.clear();
        }
        scene_marker_font = nullptr;
        scene_marker_font_size = 0.0f;
        scene_marker_font_texture_id = ImTextureID_Invalid;
        scene_marker_font_texture_unique_id = -1;
        scene_marker_font_texture_width = 0;
        scene_marker_font_texture_height = 0;
    }

    void notify_scene_loading_progress() {
        if (!wake_callback) return;
        bool expected = false;
        if (scene_wake_pending.compare_exchange_strong(expected, true)) wake_callback();
    }

    void queue_scene_model_uploads(std::vector<CpuModelData> outputs) {
        if (outputs.empty()) return;
        {
            std::lock_guard<std::mutex> lock(scene_upload_mutex);
            for (CpuModelData& output : outputs) {
                scene_pending_uploads.push_back(std::move(output));
            }
        }
        notify_scene_loading_progress();
    }

    void clear_pending_scene_model_uploads() {
        std::lock_guard<std::mutex> lock(scene_upload_mutex);
        scene_pending_uploads.clear();
        scene_wake_pending.store(false);
    }

    void release_scene_resources() {
        for (auto& kv : scene_models) release_scene_model(kv.second);
        scene_models.clear();
        scene_put_between_preview_base_model_keys.clear();
        release_scene_texture_cache();
        release_scene_track_chunks();
        release_scene_marker_chunks();
        scene_chunks.clear();
        scene_mileage_pick_points.clear();
        scene_hovered_mileage.reset();
        scene_context_mileage.reset();
        scene_placement_locations.clear();
        scene_repeater_locations.clear();
        scene_structure_edit = SceneStructureEditState{};
        clear_pending_scene_model_uploads();
        scene_last_error.clear();
        scene_stats_value = {};
        scene_model_worker_count_value.store(0);
        scene_load_summary_pending = false;
        scene_model_load_timer_active = false;
    }

    void stop_scene_loader() {
        scene_cancel.store(true);
        if (scene_worker.joinable()) scene_worker.join();
        scene_worker_running.store(false);
        scene_cancel.store(false);
    }

    CpuModelData copy_cpu_model(const std::string& path, const MlMeshData& data) {
#ifndef NDEBUG
        int remaining = debug_copy_cpu_model_throw_countdown.load(std::memory_order_relaxed);
        while (remaining > 0 &&
               !debug_copy_cpu_model_throw_countdown.compare_exchange_weak(
                   remaining, remaining - 1, std::memory_order_relaxed)) {
        }
        if (remaining == 1) {
            throw std::runtime_error("debug injected CPU model copy failure");
        }
#endif
        CpuModelData out;
        out.path = path;
        out.scene_key = path;
        if (data.vertex_count == 0 || data.index_count == 0 || !data.vertices || !data.indices) {
            out.error = "model contains no renderable data";
            return out;
        }
        out.bounds_min = {data.bounds_min[0], data.bounds_min[1], data.bounds_min[2]};
        out.bounds_max = {data.bounds_max[0], data.bounds_max[1], data.bounds_max[2]};
        out.center = {data.center[0], data.center[1], data.center[2]};
        out.radius = std::max(data.radius, 0.001f);
        out.vertices.resize(data.vertex_count);
        for (size_t i = 0; i < data.vertex_count; ++i) {
            out.vertices[i] = {
                data.vertices[i].px, data.vertices[i].py, data.vertices[i].pz,
                data.vertices[i].nx, data.vertices[i].ny, data.vertices[i].nz,
                data.vertices[i].u, data.vertices[i].v
            };
        }
        out.indices.assign(data.indices, data.indices + data.index_count);
        if (data.parts && data.part_count > 0) {
            out.parts.reserve(data.part_count);
            for (size_t i = 0; i < data.part_count; ++i) {
                out.parts.push_back({data.parts[i].start_index, data.parts[i].index_count, data.parts[i].material_index});
            }
        } else {
            out.parts.push_back({0, static_cast<UINT>(data.index_count), 0});
        }
        size_t material_count = std::max<size_t>(data.material_count, 1);
        out.materials.resize(material_count);
        for (size_t i = 0; i < material_count; ++i) {
            const MlMaterial* src = data.materials && i < data.material_count ? &data.materials[i] : nullptr;
            if (!src) continue;
            out.materials[i].diffuse[0] = src->diffuse[0];
            out.materials[i].diffuse[1] = src->diffuse[1];
            out.materials[i].diffuse[2] = src->diffuse[2];
            out.materials[i].diffuse[3] = normalize_material_alpha(src->diffuse[3]);
            if (src->texture_path && *src->texture_path) out.materials[i].texture_path = src->texture_path;
        }
        out.ok = true;
        return out;
    }

    void start_scene_put_between_preview_worker() {
        if (scene_put_between_preview_worker.joinable()) return;
        {
            std::lock_guard<std::mutex> lock(scene_put_between_preview_mutex);
            scene_put_between_preview_stop = false;
        }
        try {
            scene_put_between_preview_worker = std::thread([this]() noexcept {
                auto report_error = [this](const std::string& error) noexcept {
                    try {
                        push_scene_load_log(
                            "[warn]canvas3D.cpp: PutBetween preview worker failed: " + error);
                    } catch (...) {
                    }
                };
                try {
                    ModelLoaderClient preview_loader;
                    std::unordered_map<std::string, ScenePutBetweenPreparedSource> source_cache;
                    for (;;) {
                        ScenePutBetweenPreviewJob job;
                        {
                            std::unique_lock<std::mutex> lock(scene_put_between_preview_mutex);
                            scene_put_between_preview_cv.wait(lock, [this]() {
                                return scene_put_between_preview_stop ||
                                    scene_put_between_preview_pending.has_value();
                            });
                            if (scene_put_between_preview_stop) return;
                            job = std::move(*scene_put_between_preview_pending);
                            scene_put_between_preview_pending.reset();
                        }

                        ScenePutBetweenPreviewResult result;
                        result.sequence = job.sequence;
                        result.geometry_generation = job.geometry_generation;
                        result.target = std::move(job.target);
                        try {
                            auto source_it = source_cache.find(job.request.source_path);
                            if (source_it == source_cache.end()) {
                                if (source_cache.size() >= 4) source_cache.clear();
                                ScenePutBetweenPreparedSource prepared;
                                prepared.source = std::make_shared<CpuModelData>();
                                prepared.source->path = job.request.source_path;
                                prepared.source->scene_key = job.request.source_path;
                                MlMeshData data = {};
                                ModelDataGuard data_guard(preview_loader, data);
                                std::string load_error;
                                if (preview_loader.load(
                                        job.request.source_path, data, load_error)) {
                                    data_guard.mark_loaded();
                                    *prepared.source =
                                        copy_cpu_model(job.request.source_path, data);
                                } else {
                                    prepared.source->error = load_error;
                                }
                                prepared.source_template =
                                    prepare_put_between_source(*prepared.source);
                                source_it = source_cache.emplace(
                                    job.request.source_path, std::move(prepared)).first;
                            }

                            result.source = source_it->second.source;
                            result.derived = derive_put_between_model(
                                *source_it->second.source,
                                source_it->second.source_template,
                                job.request);
                        } catch (const std::exception& error) {
                            auto failed = std::make_shared<CpuModelData>();
                            failed->path = job.request.source_path;
                            failed->scene_key = job.request.source_path;
                            failed->error = error.what();
                            result.source = std::move(failed);
                            result.derived.error = error.what();
                            report_error(error.what());
                        } catch (...) {
                            auto failed = std::make_shared<CpuModelData>();
                            failed->path = job.request.source_path;
                            failed->scene_key = job.request.source_path;
                            failed->error = "unknown worker error";
                            result.source = std::move(failed);
                            result.derived.error = "unknown worker error";
                            report_error("unknown worker error");
                        }
                        {
                            std::lock_guard<std::mutex> lock(scene_put_between_preview_mutex);
                            if (!scene_put_between_preview_stop &&
                                result.sequence == scene_put_between_preview_latest_sequence) {
                                scene_put_between_preview_completed = std::move(result);
                            }
                        }
                        notify_scene_loading_progress();
                    }
                } catch (const std::exception& error) {
                    report_error(error.what());
                } catch (...) {
                    report_error("unknown worker error");
                }
                try {
                    notify_scene_loading_progress();
                } catch (...) {
                }
            });
        } catch (const std::exception& error) {
            push_scene_load_log(
                "[warn]canvas3D.cpp: failed to start PutBetween preview worker: " +
                std::string(error.what()));
        }
    }

    void stop_scene_put_between_preview_worker() {
        {
            std::lock_guard<std::mutex> lock(scene_put_between_preview_mutex);
            scene_put_between_preview_stop = true;
            scene_put_between_preview_pending.reset();
            scene_put_between_preview_completed.reset();
        }
        scene_put_between_preview_cv.notify_all();
        if (scene_put_between_preview_worker.joinable()) {
            scene_put_between_preview_worker.join();
        }
        std::lock_guard<std::mutex> lock(scene_put_between_preview_mutex);
        scene_put_between_preview_stop = false;
    }

    std::uint64_t queue_scene_put_between_preview(
        const Canvas3DPlacementEditTarget& target) {
        DVec3 origin;
        std::array<DVec3, 3> axes{};
        if (target.model_path.empty() ||
            !put_between_edit_frame(target.distance, origin, axes)) {
            return 0;
        }
        const Canvas3DTrackPath* own = own_track_path();
        const Canvas3DTrackPath* track1 =
            placement_track_path_for_key(target.put_between_track_key1);
        const Canvas3DTrackPath* track2 =
            placement_track_path_for_key(target.put_between_track_key2);
        if (!own || !track1 || !track2) return 0;

        start_scene_put_between_preview_worker();
        if (!scene_put_between_preview_worker.joinable()) return 0;
        ScenePutBetweenPreviewJob job;
        job.geometry_generation = scene_geometry_generation;
        job.target = target;
        job.request.key = scene_put_between_preview_model_key(
            target.edit_id, scene_geometry_generation);
        job.request.source_path = target.model_path;
        job.request.put_between.enabled = true;
        job.request.put_between.distance = target.distance;
        job.request.put_between.flag = target.put_between_flag & 1;
        job.request.put_between.origin = origin;
        job.request.put_between.own_track = own;
        job.request.put_between.track1 = track1;
        job.request.put_between.track2 = track2;
        {
            std::lock_guard<std::mutex> lock(scene_put_between_preview_mutex);
            job.sequence = ++scene_put_between_preview_next_sequence;
            scene_put_between_preview_latest_sequence = job.sequence;
            scene_put_between_preview_pending = std::move(job);
            scene_put_between_preview_completed.reset();
        }
        scene_put_between_preview_cv.notify_one();
        return scene_put_between_preview_latest_sequence;
    }

    std::optional<ScenePutBetweenPreviewResult>
    take_scene_put_between_preview_result() {
        std::lock_guard<std::mutex> lock(scene_put_between_preview_mutex);
        std::optional<ScenePutBetweenPreviewResult> result =
            std::move(scene_put_between_preview_completed);
        scene_put_between_preview_completed.reset();
        return result;
    }

    void push_scene_load_log(std::string message) {
        std::lock_guard<std::mutex> lock(scene_log_mutex);
        scene_pending_logs.push_back(std::move(message));
    }

    void start_scene_model_worker(std::vector<SceneModelLoadRequest> requests) {
        if (requests.empty()) return;
        if (scene_worker.joinable()) {
            if (scene_worker_running.load()) return;
            scene_worker.join();
        }
        std::map<std::string, std::vector<SceneModelLoadRequest>> requests_by_source;
        for (SceneModelLoadRequest& request : requests) {
            if (request.key.empty() || request.source_path.empty()) continue;
            requests_by_source[request.source_path].push_back(std::move(request));
        }
        if (requests_by_source.empty()) return;

        using SceneModelRequestGroup = std::pair<std::string, std::vector<SceneModelLoadRequest>>;
        std::vector<SceneModelRequestGroup> request_groups;
        request_groups.reserve(requests_by_source.size());
        for (auto& entry : requests_by_source) {
            request_groups.emplace_back(entry.first, std::move(entry.second));
        }

        scene_load_summary_pending = true;
        scene_model_load_started_at = std::chrono::steady_clock::now();
        scene_model_load_timer_active = true;
        scene_stats_value.model_load_seconds = 0.0;
        scene_stats_value.model_worker_count = scene_model_worker_count_for(request_groups.size());
        if (scene_model_worker_limit > 0) {
            scene_stats_value.model_worker_count = std::min(
                scene_stats_value.model_worker_count,
                std::max<size_t>(1, scene_model_worker_limit));
        }
        scene_model_worker_count_value.store(scene_stats_value.model_worker_count);
        scene_stats_value.texture_cache_hit_count = 0;
        scene_stats_value.texture_cache_miss_count = 0;
        scene_worker_running.store(true);
        try {
            const size_t requested_worker_count = scene_stats_value.model_worker_count;
            scene_worker = std::thread(
                [this, request_groups = std::move(request_groups),
                 requested_worker_count]() mutable noexcept {
                    auto safe_log = [this](std::string message) noexcept {
                        try {
                            push_scene_load_log(std::move(message));
                        } catch (...) {
                        }
                    };
                    auto finish = [this]() noexcept {
                        scene_worker_running.store(false);
                        try {
                            notify_scene_loading_progress();
                        } catch (...) {
                        }
                    };
                    auto queue_group_failure =
                        [this, &safe_log](const SceneModelRequestGroup& group,
                                         const std::string& error) noexcept {
                            try {
                                std::vector<CpuModelData> outputs;
                                outputs.reserve(group.second.size());
                                for (const SceneModelLoadRequest& request : group.second) {
                                    CpuModelData failed;
                                    failed.path = group.first;
                                    failed.scene_key = request.key;
                                    failed.error = error;
                                    outputs.push_back(std::move(failed));
                                }
                                queue_scene_model_uploads(std::move(outputs));
                            } catch (const std::exception& failure_error) {
                                safe_log(
                                    "[warn]canvas3D.cpp: failed to queue scene model failure: " +
                                    std::string(failure_error.what()));
                            } catch (...) {
                                safe_log(
                                    "[warn]canvas3D.cpp: failed to queue scene model failure: "
                                    "unknown worker error");
                            }
                        };

                    try {
                        std::string loader_error;
                        if (!scene_loader.prepare(loader_error)) {
                            safe_log(
                                "[warn]canvas3D.cpp: scene model loader initialization failed: " +
                                loader_error);
                            if (!scene_cancel.load()) {
                                for (const SceneModelRequestGroup& group : request_groups) {
                                    queue_group_failure(group, loader_error);
                                }
                            }
                            scene_model_worker_count_value.store(1);
                            finish();
                            return;
                        }

                        std::atomic<size_t> next_group{0};
                        auto process_groups = [this, &request_groups, &next_group,
                                               &queue_group_failure,
                                               &safe_log]() noexcept {
                            while (!scene_cancel.load()) {
                                const size_t group_index = next_group.fetch_add(1);
                                if (group_index >= request_groups.size()) return;

                                SceneModelRequestGroup& group = request_groups[group_index];
                                const std::string& path = group.first;
                                std::vector<SceneModelLoadRequest>& source_requests = group.second;
                                try {
                                    const std::string progress =
                                        std::to_string(group_index + 1) + "/" +
                                        std::to_string(request_groups.size());
                                    CpuModelData source_cpu;
                                    source_cpu.path = path;
                                    source_cpu.scene_key = path;
                                    MlMeshData data = {};
                                    ModelDataGuard data_guard(scene_loader, data);
                                    std::string error;
                                    if (scene_loader.load(path, data, error)) {
                                        data_guard.mark_loaded();
                                        source_cpu = copy_cpu_model(path, data);
                                        if (!source_cpu.ok) {
                                            safe_log(
                                                "[warn]canvas3D.cpp: failed to read scene model " +
                                                progress + ": " + path + ": " +
                                                source_cpu.error);
                                        }
                                    } else {
                                        source_cpu.error = error;
                                        safe_log(
                                            "[warn]canvas3D.cpp: failed to read scene model " +
                                            progress + ": " + path + ": " + error);
                                    }
                                    if (scene_cancel.load()) return;

                                    std::vector<CpuModelData> outputs;
                                    outputs.reserve(source_requests.size());
                                    if (!source_cpu.ok) {
                                        for (const SceneModelLoadRequest& request : source_requests) {
                                            CpuModelData failed;
                                            failed.path = path;
                                            failed.scene_key = request.key;
                                            failed.error = source_cpu.error;
                                            outputs.push_back(std::move(failed));
                                        }
                                    } else {
                                        const SceneModelLoadRequest* regular_request = nullptr;
                                        const PutBetweenSourceTemplate put_between_source =
                                            prepare_put_between_source(source_cpu);
                                        std::vector<CpuModelData> derived_models;
                                        derived_models.reserve(source_requests.size());
                                        for (const SceneModelLoadRequest& request : source_requests) {
                                            if (!request.put_between.enabled) {
                                                regular_request = &request;
                                                continue;
                                            }
                                            CpuModelData derived = derive_put_between_model(
                                                source_cpu, put_between_source, request);
                                            if (!derived.ok) {
                                                safe_log(
                                                    "[warn]canvas3D.cpp: failed to deform "
                                                    "PutBetween model: " + path + ": " +
                                                    derived.error);
                                            }
                                            derived_models.push_back(std::move(derived));
                                            if (scene_cancel.load()) return;
                                        }
                                        if (regular_request) {
                                            source_cpu.scene_key = regular_request->key;
                                            outputs.push_back(std::move(source_cpu));
                                        }
                                        for (CpuModelData& derived : derived_models) {
                                            outputs.push_back(std::move(derived));
                                        }
                                    }
                                    queue_scene_model_uploads(std::move(outputs));
                                } catch (const std::exception& error) {
                                    const std::string detail = error.what();
                                    safe_log(
                                        "[warn]canvas3D.cpp: scene model worker failed: " +
                                        path + ": " + detail);
                                    queue_group_failure(group, detail);
                                } catch (...) {
                                    safe_log(
                                        "[warn]canvas3D.cpp: scene model worker failed: " +
                                        path + ": unknown worker error");
                                    queue_group_failure(group, "unknown worker error");
                                }
                            }
                        };

                        std::vector<std::thread> helpers;
                        helpers.reserve(
                            requested_worker_count > 0 ? requested_worker_count - 1 : 0);
                        for (size_t worker = 1; worker < requested_worker_count; ++worker) {
                            try {
                                helpers.emplace_back(process_groups);
                            } catch (const std::exception& error) {
                                safe_log(
                                    "[warn]canvas3D.cpp: scene model worker count reduced: " +
                                    std::string(error.what()));
                                break;
                            }
                        }
                        scene_model_worker_count_value.store(helpers.size() + 1);
                        process_groups();
                        for (std::thread& helper : helpers) helper.join();
                    } catch (const std::exception& error) {
                        safe_log(
                            "[warn]canvas3D.cpp: scene model worker failed: " +
                            std::string(error.what()));
                        if (!scene_cancel.load()) {
                            for (const SceneModelRequestGroup& group : request_groups) {
                                queue_group_failure(group, error.what());
                            }
                        }
                    } catch (...) {
                        safe_log(
                            "[warn]canvas3D.cpp: scene model worker failed: "
                            "unknown worker error");
                        if (!scene_cancel.load()) {
                            for (const SceneModelRequestGroup& group : request_groups) {
                                queue_group_failure(group, "unknown worker error");
                            }
                        }
                    }
                    finish();
                });
        } catch (const std::exception& e) {
            const std::string error = "failed to start scene model worker: " + std::string(e.what());
            for (auto& entry : scene_models) {
                if (entry.second.state != SceneModelGpu::State::Pending) continue;
                entry.second.state = SceneModelGpu::State::Failed;
                entry.second.error = error;
            }
            scene_model_worker_count_value.store(0);
            scene_worker_running.store(false);
            push_scene_load_log("[warn]canvas3D.cpp: " + error);
            notify_scene_loading_progress();
        } catch (...) {
            const std::string error = "failed to start scene model worker: unknown error";
            for (auto& entry : scene_models) {
                if (entry.second.state != SceneModelGpu::State::Pending) continue;
                entry.second.state = SceneModelGpu::State::Failed;
                entry.second.error = error;
            }
            scene_model_worker_count_value.store(0);
            scene_worker_running.store(false);
            push_scene_load_log("[warn]canvas3D.cpp: " + error);
            notify_scene_loading_progress();
        }
    }

#ifndef NDEBUG
    Canvas3DSceneLoaderContractResult debug_run_scene_loader_contract(
        const std::string& valid_model_path) {
        Canvas3DSceneLoaderContractResult result;
        result.release_balance = true;
        auto record_release_counts = [&]() {
            const size_t loaded = ModelLoaderClient::debug_successful_load_count();
            const size_t freed = ModelLoaderClient::debug_free_count();
            result.successful_load_count += loaded;
            result.free_count += freed;
            result.release_balance = result.release_balance && loaded == freed;
        };
        auto reset_scene_worker_state = [&]() {
            stop_scene_loader();
            clear_pending_scene_model_uploads();
            for (auto& entry : scene_models) release_scene_model(entry.second);
            scene_models.clear();
            scene_cancel.store(false);
            scene_worker_running.store(false);
            scene_model_worker_limit = 1;
            debug_copy_cpu_model_throw_countdown.store(0);
            g_debug_put_between_derive_throw_countdown.store(0);
        };
        auto pending_uploads = [&]() {
            std::lock_guard<std::mutex> lock(scene_upload_mutex);
            return scene_pending_uploads;
        };

        try {
            reset_scene_worker_state();
            ModelLoaderClient::debug_reset_counts();
            scene_models.try_emplace("normal");
            SceneModelLoadRequest normal;
            normal.key = "normal";
            normal.source_path = valid_model_path;
            start_scene_model_worker({normal});
            if (scene_worker.joinable()) scene_worker.join();
            const std::vector<CpuModelData> normal_outputs = pending_uploads();
            result.normal_worker = !scene_worker_running.load() &&
                normal_outputs.size() == 1 && normal_outputs[0].ok &&
                normal_outputs[0].scene_key == "normal";
            record_release_counts();

            reset_scene_worker_state();
            ModelLoaderClient::debug_reset_counts();
            debug_copy_cpu_model_throw_countdown.store(1);
            scene_models.try_emplace("copy-failure");
            SceneModelLoadRequest copy_failure;
            copy_failure.key = "copy-failure";
            copy_failure.source_path = valid_model_path;
            start_scene_model_worker({copy_failure});
            if (scene_worker.joinable()) scene_worker.join();
            const std::vector<CpuModelData> failure_outputs = pending_uploads();
            result.copy_exception = !scene_worker_running.load() &&
                failure_outputs.size() == 1 && !failure_outputs[0].ok &&
                failure_outputs[0].error.find("debug injected CPU model copy failure") !=
                    std::string::npos;
            record_release_counts();

            reset_scene_worker_state();
            stop_scene_put_between_preview_worker();
            ModelLoaderClient::debug_reset_counts();
            g_debug_put_between_derive_throw_countdown.store(1);
            start_scene_put_between_preview_worker();
            if (scene_put_between_preview_worker.joinable()) {
                ScenePutBetweenPreviewJob job;
                job.sequence = 1;
                job.geometry_generation = 1;
                job.request.key = "put-between-failure";
                job.request.source_path = valid_model_path;
                job.request.put_between.enabled = true;
                {
                    std::lock_guard<std::mutex> lock(scene_put_between_preview_mutex);
                    scene_put_between_preview_latest_sequence = job.sequence;
                    scene_put_between_preview_pending = std::move(job);
                    scene_put_between_preview_completed.reset();
                }
                scene_put_between_preview_cv.notify_one();
                for (int attempt = 0; attempt < 5000; ++attempt) {
                    {
                        std::lock_guard<std::mutex> lock(scene_put_between_preview_mutex);
                        if (scene_put_between_preview_completed) {
                            const ScenePutBetweenPreviewResult& completed =
                                *scene_put_between_preview_completed;
                            result.put_between_exception = completed.source &&
                                !completed.source->ok && !completed.derived.ok &&
                                completed.derived.error.find(
                                    "debug injected PutBetween derivation failure") !=
                                    std::string::npos;
                            break;
                        }
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }
            }
            stop_scene_put_between_preview_worker();
            record_release_counts();

            auto start_artificial_worker = [&]() {
                scene_cancel.store(false);
                scene_worker_running.store(true);
                scene_worker = std::thread([this]() noexcept {
                    while (!scene_cancel.load(std::memory_order_relaxed)) {
                        std::this_thread::sleep_for(std::chrono::milliseconds(1));
                    }
                    scene_worker_running.store(false);
                });
            };
            auto queue_dummy_upload = [&]() {
                std::lock_guard<std::mutex> lock(scene_upload_mutex);
                CpuModelData dummy;
                dummy.scene_key = "stale";
                scene_pending_uploads.push_back(std::move(dummy));
            };
            auto pending_upload_count = [&]() {
                std::lock_guard<std::mutex> lock(scene_upload_mutex);
                return scene_pending_uploads.size();
            };

            reset_scene_worker_state();
            scene_models.try_emplace("keep");
            scene_models.try_emplace("remove");
            start_artificial_worker();
            queue_dummy_upload();
            SceneModelLoadRequest keep_request;
            keep_request.key = "keep";
            keep_request.source_path = valid_model_path;
            std::map<std::string, SceneModelLoadRequest> subset_requests;
            subset_requests.emplace(keep_request.key, keep_request);
            const std::vector<SceneModelLoadRequest> subset_to_load =
                reconcile_dynamic_scene_model_requests(subset_requests);
            result.subset_requeue = !scene_worker.joinable() &&
                !scene_worker_running.load() && pending_upload_count() == 0 &&
                scene_models.size() == 1 && scene_models.count("keep") == 1 &&
                subset_to_load.size() == 1 && subset_to_load[0].key == "keep";

            reset_scene_worker_state();
            scene_models.try_emplace("remove-only");
            start_artificial_worker();
            queue_dummy_upload();
            const std::map<std::string, SceneModelLoadRequest> empty_requests;
            const std::vector<SceneModelLoadRequest> removal_to_load =
                reconcile_dynamic_scene_model_requests(empty_requests);
            result.removal_only_cancel = !scene_worker.joinable() &&
                !scene_worker_running.load() && pending_upload_count() == 0 &&
                scene_models.empty() && removal_to_load.empty();
            reset_scene_worker_state();
        } catch (const std::exception& error) {
            result.error = error.what();
            stop_scene_put_between_preview_worker();
            reset_scene_worker_state();
        } catch (...) {
            result.error = "unknown scene loader contract error";
            stop_scene_put_between_preview_worker();
            reset_scene_worker_state();
        }
        return result;
    }
#endif

    void maybe_log_scene_model_load_summary() {
        if (!scene_load_summary_pending || scene_worker_running.load()) return;
        Canvas3DSceneStats stats = scene_stats();
        if (stats.model_ready_count + stats.model_failed_count < stats.model_path_count) return;
        if (scene_model_load_timer_active) {
            scene_stats_value.model_load_seconds = std::chrono::duration<double>(
                std::chrono::steady_clock::now() - scene_model_load_started_at).count();
            scene_model_load_timer_active = false;
            stats.model_load_seconds = scene_stats_value.model_load_seconds;
        }
        char elapsed[64] = {};
        std::snprintf(elapsed, sizeof(elapsed), "%.3f", stats.model_load_seconds);
        push_scene_load_log("[info]canvas3D.cpp: scene model loading finished in " +
                            std::string(elapsed) + " s: workers=" +
                            std::to_string(stats.model_worker_count) +
                            " loaded=" +
                            std::to_string(stats.model_ready_count) +
                            " failed=" + std::to_string(stats.model_failed_count) +
                            " total=" + std::to_string(stats.model_path_count) +
                            " texture_cache_hits=" + std::to_string(stats.texture_cache_hit_count) +
                            " texture_cache_misses=" + std::to_string(stats.texture_cache_miss_count));
        scene_load_summary_pending = false;
    }

    bool upload_scene_model(const CpuModelData& cpu, std::string& error) {
        const std::string& scene_key = cpu.scene_key.empty() ? cpu.path : cpu.scene_key;
        auto it = scene_models.find(scene_key);
        if (it == scene_models.end()) return true;
        SceneModelGpu& model = it->second;
        release_scene_model(model);
        if (!cpu.ok) {
            model.state = SceneModelGpu::State::Failed;
            model.error = cpu.error.empty() ? "model load failed" : cpu.error;
            return true;
        }
        if (cpu.vertices.size() > static_cast<size_t>(std::numeric_limits<UINT>::max() / sizeof(GpuVertex)) ||
            cpu.indices.size() > static_cast<size_t>(std::numeric_limits<UINT>::max() / sizeof(unsigned int))) {
            model.state = SceneModelGpu::State::Failed;
            model.error = "model is too large for a Direct3D 11 buffer";
            return true;
        }

        const SceneModelGpu* shared_model = nullptr;
        if (!cpu.shared_model_key.empty()) {
            auto shared_it = scene_models.find(cpu.shared_model_key);
            if (shared_it == scene_models.end() ||
                shared_it->second.state != SceneModelGpu::State::Ready ||
                !shared_it->second.index_buffer) {
                model.state = SceneModelGpu::State::Failed;
                model.error = "PutBetween base model is not ready";
                return true;
            }
            shared_model = &shared_it->second;
        }

        D3D11_BUFFER_DESC vb_desc = {};
        vb_desc.ByteWidth = static_cast<UINT>(cpu.vertices.size() * sizeof(GpuVertex));
        vb_desc.Usage = D3D11_USAGE_DEFAULT;
        vb_desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        D3D11_SUBRESOURCE_DATA vb_data = {};
        vb_data.pSysMem = cpu.vertices.data();
        HRESULT hr = device->CreateBuffer(&vb_desc, &vb_data, &model.vertex_buffer);
        if (FAILED(hr)) {
            error = hresult_text("CreateBuffer(scene vertex)", hr);
            return false;
        }

        if (shared_model) {
            model.index_buffer = shared_model->index_buffer;
            model.index_buffer->AddRef();
            model.parts = shared_model->parts;
            model.materials = shared_model->materials;
            for (GpuMaterial& material : model.materials) {
                if (material.texture) material.texture->AddRef();
            }
            model.index_count = shared_model->index_count;
        } else {
            D3D11_BUFFER_DESC ib_desc = {};
            ib_desc.ByteWidth = static_cast<UINT>(cpu.indices.size() * sizeof(unsigned int));
            ib_desc.Usage = D3D11_USAGE_DEFAULT;
            ib_desc.BindFlags = D3D11_BIND_INDEX_BUFFER;
            D3D11_SUBRESOURCE_DATA ib_data = {};
            ib_data.pSysMem = cpu.indices.data();
            hr = device->CreateBuffer(&ib_desc, &ib_data, &model.index_buffer);
            if (FAILED(hr)) {
                error = hresult_text("CreateBuffer(scene index)", hr);
                return false;
            }

            model.parts = cpu.parts;
            model.materials.resize(std::max<size_t>(cpu.materials.size(), 1));
            for (size_t i = 0; i < model.materials.size(); ++i) {
                const CpuMaterial* src = i < cpu.materials.size() ? &cpu.materials[i] : nullptr;
                if (!src) continue;
                model.materials[i].diffuse[0] = src->diffuse[0];
                model.materials[i].diffuse[1] = src->diffuse[1];
                model.materials[i].diffuse[2] = src->diffuse[2];
                model.materials[i].diffuse[3] = normalize_material_alpha(src->diffuse[3]);
                if (!src->texture_path.empty()) {
                    std::string texture_error;
                    bool texture_has_alpha = false;
                    if (load_scene_texture(src->texture_path, &model.materials[i].texture,
                                           texture_error, &texture_has_alpha)) {
                        model.materials[i].has_texture = true;
                        model.materials[i].texture_has_alpha = texture_has_alpha;
                    } else {
                        if (scene_last_error.empty()) scene_last_error = texture_error;
                        const std::string texture_key =
                            normalized_texture_cache_key(src->texture_path);
                        if (scene_texture_warning_keys.insert(texture_key).second) {
                            push_scene_load_log(
                                "[WARN]canvas3D.cpp: scene texture warning: model=" +
                                cpu.path + "; " + texture_error);
                        }
                    }
                }
            }
            model.index_count = static_cast<UINT>(cpu.indices.size());
        }
        model.bounds_min = cpu.bounds_min;
        model.bounds_max = cpu.bounds_max;
        model.center = cpu.center;
        model.radius = std::max(cpu.radius, 0.001f);
        model.state = SceneModelGpu::State::Ready;
        model.error.clear();
        return true;
    }

    bool upload_scene_put_between_preview_model(
        const CpuModelData& cpu,
        const std::string& preview_key,
        const SceneModelGpu& shared_model,
        std::string& error) {
        if (!cpu.ok || cpu.vertices.empty() || !shared_model.index_buffer ||
            shared_model.state != SceneModelGpu::State::Ready) {
            error = cpu.error.empty()
                ? "PutBetween preview source model is not ready" : cpu.error;
            return false;
        }
        if (cpu.vertices.size() >
            static_cast<size_t>(std::numeric_limits<UINT>::max() /
                                sizeof(GpuVertex))) {
            error = "PutBetween preview model is too large for a Direct3D 11 buffer";
            return false;
        }

        SceneModelGpu& model = scene_models[preview_key];
        const UINT vertex_count = static_cast<UINT>(cpu.vertices.size());
        const bool rebuild = !model.dynamic_vertices ||
            model.shared_model_key != cpu.shared_model_key ||
            !model.vertex_buffer || model.vertex_capacity < vertex_count;
        if (rebuild) {
            release_scene_model(model);
            D3D11_BUFFER_DESC vb_desc = {};
            vb_desc.ByteWidth = vertex_count * sizeof(GpuVertex);
            vb_desc.Usage = D3D11_USAGE_DYNAMIC;
            vb_desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
            vb_desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
            HRESULT hr = device->CreateBuffer(&vb_desc, nullptr, &model.vertex_buffer);
            if (FAILED(hr)) {
                error = hresult_text("CreateBuffer(PutBetween preview vertex)", hr);
                return false;
            }
            model.vertex_capacity = vertex_count;
            model.dynamic_vertices = true;
            model.shared_model_key = cpu.shared_model_key;
            model.index_buffer = shared_model.index_buffer;
            model.index_buffer->AddRef();
            model.parts = shared_model.parts;
            model.materials = shared_model.materials;
            for (GpuMaterial& material : model.materials) {
                if (material.texture) material.texture->AddRef();
            }
            model.index_count = shared_model.index_count;
        }

        D3D11_MAPPED_SUBRESOURCE mapped = {};
        HRESULT hr = context->Map(
            model.vertex_buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
        if (FAILED(hr)) {
            error = hresult_text("Map(PutBetween preview vertex)", hr);
            return false;
        }
        std::memcpy(mapped.pData, cpu.vertices.data(),
                    cpu.vertices.size() * sizeof(GpuVertex));
        context->Unmap(model.vertex_buffer, 0);
        model.bounds_min = cpu.bounds_min;
        model.bounds_max = cpu.bounds_max;
        model.center = cpu.center;
        model.radius = std::max(cpu.radius, 0.001f);
        model.state = SceneModelGpu::State::Ready;
        model.error.clear();
        return true;
    }

    void apply_scene_put_between_preview_result() {
        std::optional<ScenePutBetweenPreviewResult> pending =
            take_scene_put_between_preview_result();
        if (!pending) return;
        ScenePutBetweenPreviewResult& result = *pending;
        if (!scene_active || !scene_structure_edit.active ||
            scene_structure_edit.kind != Canvas3DSceneEditKind::StructurePutBetween ||
            result.geometry_generation != scene_geometry_generation ||
            result.sequence != scene_structure_edit.preview_sequence ||
            !same_placement_edit_target(result.target, scene_structure_edit.current)) {
            return;
        }
        if (!result.source || !result.source->ok || !result.derived.ok) {
            const std::string detail = !result.derived.error.empty()
                ? result.derived.error
                : result.source ? result.source->error : std::string("source model unavailable");
            push_scene_load_log(
                "[warn]canvas3D.cpp: PutBetween live preview failed: " + detail);
            return;
        }

        auto [shared_it, inserted] = scene_models.try_emplace(result.source->path);
        if (inserted || shared_it->second.state != SceneModelGpu::State::Ready) {
            CpuModelData source = *result.source;
            source.scene_key = source.path;
            source.shared_model_key.clear();
            std::string source_error;
            if (!upload_scene_model(source, source_error) ||
                shared_it->second.state != SceneModelGpu::State::Ready) {
                push_scene_load_log(
                    "[warn]canvas3D.cpp: PutBetween live preview base upload failed: " +
                    (source_error.empty() ? shared_it->second.error : source_error));
                return;
            }
            scene_put_between_preview_base_model_keys.insert(source.path);
        }

        const std::string preview_key = scene_put_between_preview_model_key(
            result.target.edit_id, scene_geometry_generation);
        std::string upload_error;
        if (!upload_scene_put_between_preview_model(
                result.derived, preview_key, shared_it->second, upload_error)) {
            push_scene_load_log(
                "[warn]canvas3D.cpp: PutBetween live preview upload failed: " +
                upload_error);
            return;
        }
        Canvas3DModelInstance desired = put_between_instance_from_target(
            result.target, scene_structure_edit.baseline_instance);
        if (!write_scene_put_between_instance(
                result.target.edit_id, std::move(desired), preview_key)) {
            push_scene_load_log(
                "[warn]canvas3D.cpp: PutBetween live preview instance update failed");
            return;
        }
        scene_structure_edit.completed = result.target;
        scene_structure_edit.preview_model_key = preview_key;
        release_unused_put_between_preview_base_models();
    }

    void upload_pending_scene_models() {
        scene_wake_pending.store(false);
        std::vector<CpuModelData> pending;
        {
            std::lock_guard<std::mutex> lock(scene_upload_mutex);
            pending.swap(scene_pending_uploads);
        }
        for (const CpuModelData& cpu : pending) {
            std::string error;
            if (!upload_scene_model(cpu, error)) {
                const std::string& scene_key = cpu.scene_key.empty() ? cpu.path : cpu.scene_key;
                auto it = scene_models.find(scene_key);
                if (it != scene_models.end()) {
                    release_scene_model(it->second);
                    it->second.state = SceneModelGpu::State::Failed;
                    it->second.error = error;
                }
                if (!error.empty()) {
                    scene_last_error = error;
                    push_scene_load_log("[warn]canvas3D.cpp: failed to upload scene model: " +
                                        cpu.path + ": " + error);
                }
            }
        }
        maybe_log_scene_model_load_summary();
    }

    bool ensure_scene_outline_pipeline(std::string& error) {
        if (scene_outline_vertex_shader && scene_outline_pixel_shader &&
            scene_outline_constant_buffer && scene_outline_sampler_state) {
            return true;
        }

        ID3DBlob* vs_blob = nullptr;
        ID3DBlob* ps_blob = nullptr;
        ID3DBlob* errors = nullptr;
        HRESULT hr = D3DCompile(k_scene_highlight_outline_shader_source, std::strlen(k_scene_highlight_outline_shader_source),
                                nullptr, nullptr, nullptr, "vs_main", "vs_4_0",
                                D3DCOMPILE_ENABLE_STRICTNESS, 0, &vs_blob, &errors);
        if (FAILED(hr)) {
            error = errors ? static_cast<const char*>(errors->GetBufferPointer()) :
                hresult_text("D3DCompile(scene outline vertex shader)", hr);
            release_com(errors);
            return false;
        }
        release_com(errors);

        hr = D3DCompile(k_scene_highlight_outline_shader_source, std::strlen(k_scene_highlight_outline_shader_source),
                        nullptr, nullptr, nullptr, "ps_main", "ps_4_0",
                        D3DCOMPILE_ENABLE_STRICTNESS, 0, &ps_blob, &errors);
        if (FAILED(hr)) {
            error = errors ? static_cast<const char*>(errors->GetBufferPointer()) :
                hresult_text("D3DCompile(scene outline pixel shader)", hr);
            release_com(errors);
            release_com(vs_blob);
            return false;
        }
        release_com(errors);

        hr = device->CreateVertexShader(vs_blob->GetBufferPointer(), vs_blob->GetBufferSize(), nullptr,
                                        &scene_outline_vertex_shader);
        if (FAILED(hr)) {
            error = hresult_text("CreateVertexShader(scene outline)", hr);
            release_com(vs_blob);
            release_com(ps_blob);
            return false;
        }
        hr = device->CreatePixelShader(ps_blob->GetBufferPointer(), ps_blob->GetBufferSize(), nullptr,
                                       &scene_outline_pixel_shader);
        release_com(vs_blob);
        release_com(ps_blob);
        if (FAILED(hr)) {
            error = hresult_text("CreatePixelShader(scene outline)", hr);
            release_com(scene_outline_vertex_shader);
            return false;
        }

        D3D11_BUFFER_DESC cb_desc = {};
        cb_desc.ByteWidth = sizeof(SceneOutlineConstants);
        cb_desc.Usage = D3D11_USAGE_DEFAULT;
        cb_desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        hr = device->CreateBuffer(&cb_desc, nullptr, &scene_outline_constant_buffer);
        if (FAILED(hr)) {
            error = hresult_text("CreateBuffer(scene outline constants)", hr);
            release_com(scene_outline_pixel_shader);
            release_com(scene_outline_vertex_shader);
            return false;
        }

        D3D11_SAMPLER_DESC sampler_desc = {};
        sampler_desc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
        sampler_desc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
        sampler_desc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
        sampler_desc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        sampler_desc.ComparisonFunc = D3D11_COMPARISON_NEVER;
        sampler_desc.MinLOD = 0.0f;
        sampler_desc.MaxLOD = D3D11_FLOAT32_MAX;
        hr = device->CreateSamplerState(&sampler_desc, &scene_outline_sampler_state);
        if (FAILED(hr)) {
            error = hresult_text("CreateSamplerState(scene outline)", hr);
            release_com(scene_outline_constant_buffer);
            release_com(scene_outline_pixel_shader);
            release_com(scene_outline_vertex_shader);
            return false;
        }

        return true;
    }

    bool ensure_scene_pick_pipeline(std::string& error) {
        if (scene_pick_pixel_shader && scene_pick_constant_buffer) return true;

        ID3DBlob* ps_blob = nullptr;
        ID3DBlob* errors = nullptr;
        HRESULT hr = D3DCompile(k_scene_pick_shader_source, std::strlen(k_scene_pick_shader_source),
                                nullptr, nullptr, nullptr, "ps_main", "ps_4_0",
                                D3DCOMPILE_ENABLE_STRICTNESS, 0, &ps_blob, &errors);
        if (FAILED(hr)) {
            error = errors ? static_cast<const char*>(errors->GetBufferPointer()) :
                hresult_text("D3DCompile(scene pick pixel shader)", hr);
            release_com(errors);
            return false;
        }
        release_com(errors);

        hr = device->CreatePixelShader(ps_blob->GetBufferPointer(), ps_blob->GetBufferSize(), nullptr,
                                       &scene_pick_pixel_shader);
        release_com(ps_blob);
        if (FAILED(hr)) {
            error = hresult_text("CreatePixelShader(scene pick)", hr);
            return false;
        }

        D3D11_BUFFER_DESC cb_desc = {};
        cb_desc.ByteWidth = sizeof(ScenePickConstants);
        cb_desc.Usage = D3D11_USAGE_DEFAULT;
        cb_desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        hr = device->CreateBuffer(&cb_desc, nullptr, &scene_pick_constant_buffer);
        if (FAILED(hr)) {
            error = hresult_text("CreateBuffer(scene pick constants)", hr);
            release_com(scene_pick_pixel_shader);
            return false;
        }

        return true;
    }

    bool ensure_scene_pipeline(std::string& error) {
        if (!device || !context) {
            error = "Direct3D device is not available";
            return false;
        }
        if (scene_vertex_shader && scene_pixel_shader && scene_fog_pixel_shader &&
            scene_input_layout && scene_constant_buffer &&
            scene_depth_state && scene_depth_read_state && rasterizer_state && alpha_mask_rasterizer_state &&
            track_rasterizer_state && sampler_state && blend_state &&
            scene_outline_vertex_shader && scene_outline_pixel_shader &&
            scene_outline_constant_buffer && scene_outline_sampler_state &&
            scene_pick_pixel_shader && scene_pick_constant_buffer &&
            scene_marker_vertex_shader && scene_marker_pixel_shader &&
            scene_marker_pick_pixel_shader && scene_marker_mask_pixel_shader &&
            scene_marker_input_layout && scene_marker_constant_buffer &&
            scene_marker_pick_constant_buffer) return true;

        if (!ensure_pipeline(error)) return false;
        if (!ensure_scene_depth_states(error)) return false;
        if (scene_vertex_shader && scene_pixel_shader && scene_fog_pixel_shader &&
            scene_input_layout && scene_constant_buffer &&
            track_rasterizer_state && sampler_state && blend_state) {
            return ensure_scene_outline_pipeline(error) &&
                ensure_scene_pick_pipeline(error) &&
                ensure_scene_marker_pipeline(error);
        }

        release_com(scene_input_layout);
        release_com(scene_vertex_shader);
        release_com(scene_pixel_shader);
        release_com(scene_fog_pixel_shader);
        release_com(scene_constant_buffer);

        ID3DBlob* vs_blob = nullptr;
        ID3DBlob* ps_blob = nullptr;
        ID3DBlob* fog_ps_blob = nullptr;
        ID3DBlob* errors = nullptr;
        HRESULT hr = D3DCompile(k_scene_shader_source, std::strlen(k_scene_shader_source), nullptr, nullptr, nullptr,
                                "vs_main", "vs_4_0", D3DCOMPILE_ENABLE_STRICTNESS, 0, &vs_blob, &errors);
        if (FAILED(hr)) {
            error = errors ? static_cast<const char*>(errors->GetBufferPointer()) : hresult_text("D3DCompile(scene vertex shader)", hr);
            release_com(errors);
            return false;
        }
        release_com(errors);

        hr = D3DCompile(k_scene_shader_source, std::strlen(k_scene_shader_source), nullptr, nullptr, nullptr,
                        "ps_main", "ps_4_0", D3DCOMPILE_ENABLE_STRICTNESS, 0, &ps_blob, &errors);
        if (FAILED(hr)) {
            error = errors ? static_cast<const char*>(errors->GetBufferPointer()) : hresult_text("D3DCompile(scene pixel shader)", hr);
            release_com(errors);
            release_com(vs_blob);
            return false;
        }
        release_com(errors);

        hr = D3DCompile(k_scene_shader_source, std::strlen(k_scene_shader_source), nullptr, nullptr, nullptr,
                        "ps_fog_main", "ps_4_0", D3DCOMPILE_ENABLE_STRICTNESS, 0, &fog_ps_blob, &errors);
        if (FAILED(hr)) {
            error = errors ? static_cast<const char*>(errors->GetBufferPointer()) : hresult_text("D3DCompile(scene fog pixel shader)", hr);
            release_com(errors);
            release_com(vs_blob);
            release_com(ps_blob);
            return false;
        }
        release_com(errors);

        hr = device->CreateVertexShader(vs_blob->GetBufferPointer(), vs_blob->GetBufferSize(), nullptr, &scene_vertex_shader);
        if (FAILED(hr)) {
            error = hresult_text("CreateVertexShader(scene)", hr);
            release_com(vs_blob);
            release_com(ps_blob);
            release_com(fog_ps_blob);
            return false;
        }
        hr = device->CreatePixelShader(ps_blob->GetBufferPointer(), ps_blob->GetBufferSize(), nullptr, &scene_pixel_shader);
        if (FAILED(hr)) {
            error = hresult_text("CreatePixelShader(scene)", hr);
            release_com(vs_blob);
            release_com(ps_blob);
            release_com(fog_ps_blob);
            release_com(scene_vertex_shader);
            return false;
        }
        hr = device->CreatePixelShader(fog_ps_blob->GetBufferPointer(), fog_ps_blob->GetBufferSize(), nullptr,
                                       &scene_fog_pixel_shader);
        if (FAILED(hr)) {
            error = hresult_text("CreatePixelShader(scene fog)", hr);
            release_com(vs_blob);
            release_com(ps_blob);
            release_com(fog_ps_blob);
            release_com(scene_vertex_shader);
            release_com(scene_pixel_shader);
            return false;
        }

        D3D11_INPUT_ELEMENT_DESC layout[] = {
            {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"WORLD", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 0, D3D11_INPUT_PER_INSTANCE_DATA, 1},
            {"WORLD", 1, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 16, D3D11_INPUT_PER_INSTANCE_DATA, 1},
            {"WORLD", 2, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 32, D3D11_INPUT_PER_INSTANCE_DATA, 1},
            {"WORLD", 3, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 48, D3D11_INPUT_PER_INSTANCE_DATA, 1},
        };
        hr = device->CreateInputLayout(layout, 7, vs_blob->GetBufferPointer(), vs_blob->GetBufferSize(), &scene_input_layout);
        release_com(vs_blob);
        release_com(ps_blob);
        release_com(fog_ps_blob);
        if (FAILED(hr)) {
            error = hresult_text("CreateInputLayout(scene)", hr);
            release_com(scene_vertex_shader);
            release_com(scene_pixel_shader);
            release_com(scene_fog_pixel_shader);
            return false;
        }

        D3D11_BUFFER_DESC cb_desc = {};
        cb_desc.ByteWidth = sizeof(SceneViewConstants);
        cb_desc.Usage = D3D11_USAGE_DEFAULT;
        cb_desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        hr = device->CreateBuffer(&cb_desc, nullptr, &scene_constant_buffer);
        if (FAILED(hr)) {
            error = hresult_text("CreateBuffer(scene constants)", hr);
            release_com(scene_input_layout);
            release_com(scene_vertex_shader);
            release_com(scene_pixel_shader);
            release_com(scene_fog_pixel_shader);
            return false;
        }
        return ensure_scene_outline_pipeline(error) &&
            ensure_scene_pick_pipeline(error) &&
            ensure_scene_marker_pipeline(error);
    }

    bool ensure_scene_marker_pipeline(std::string& error) {
        if (scene_marker_vertex_shader && scene_marker_pixel_shader &&
            scene_marker_pick_pixel_shader && scene_marker_mask_pixel_shader &&
            scene_marker_input_layout && scene_marker_constant_buffer &&
            scene_marker_pick_constant_buffer) {
            return true;
        }
        release_com(scene_marker_input_layout);
        release_com(scene_marker_vertex_shader);
        release_com(scene_marker_pixel_shader);
        release_com(scene_marker_pick_pixel_shader);
        release_com(scene_marker_mask_pixel_shader);
        release_com(scene_marker_constant_buffer);
        release_com(scene_marker_pick_constant_buffer);

        ID3DBlob* vs_blob = nullptr;
        ID3DBlob* ps_blob = nullptr;
        ID3DBlob* pick_ps_blob = nullptr;
        ID3DBlob* mask_ps_blob = nullptr;
        ID3DBlob* errors = nullptr;
        HRESULT hr = D3DCompile(
            k_scene_marker_shader_source, std::strlen(k_scene_marker_shader_source),
            nullptr, nullptr, nullptr, "vs_main", "vs_4_0",
            D3DCOMPILE_ENABLE_STRICTNESS, 0, &vs_blob, &errors);
        if (FAILED(hr)) {
            error = errors
                ? static_cast<const char*>(errors->GetBufferPointer())
                : hresult_text("D3DCompile(scene marker vertex shader)", hr);
            release_com(errors);
            return false;
        }
        release_com(errors);

        hr = D3DCompile(
            k_scene_marker_shader_source, std::strlen(k_scene_marker_shader_source),
            nullptr, nullptr, nullptr, "ps_main", "ps_4_0",
            D3DCOMPILE_ENABLE_STRICTNESS, 0, &ps_blob, &errors);
        if (FAILED(hr)) {
            error = errors
                ? static_cast<const char*>(errors->GetBufferPointer())
                : hresult_text("D3DCompile(scene marker pixel shader)", hr);
            release_com(errors);
            release_com(vs_blob);
            return false;
        }
        release_com(errors);

        hr = D3DCompile(
            k_scene_marker_shader_source, std::strlen(k_scene_marker_shader_source),
            nullptr, nullptr, nullptr, "ps_pick_main", "ps_4_0",
            D3DCOMPILE_ENABLE_STRICTNESS, 0, &pick_ps_blob, &errors);
        if (FAILED(hr)) {
            error = errors
                ? static_cast<const char*>(errors->GetBufferPointer())
                : hresult_text("D3DCompile(scene marker pick shader)", hr);
            release_com(errors);
            release_com(vs_blob);
            release_com(ps_blob);
            return false;
        }
        release_com(errors);

        hr = D3DCompile(
            k_scene_marker_shader_source, std::strlen(k_scene_marker_shader_source),
            nullptr, nullptr, nullptr, "ps_mask_main", "ps_4_0",
            D3DCOMPILE_ENABLE_STRICTNESS, 0, &mask_ps_blob, &errors);
        if (FAILED(hr)) {
            error = errors
                ? static_cast<const char*>(errors->GetBufferPointer())
                : hresult_text("D3DCompile(scene marker mask shader)", hr);
            release_com(errors);
            release_com(vs_blob);
            release_com(ps_blob);
            release_com(pick_ps_blob);
            return false;
        }
        release_com(errors);

        hr = device->CreateVertexShader(vs_blob->GetBufferPointer(),
                                        vs_blob->GetBufferSize(), nullptr,
                                        &scene_marker_vertex_shader);
        if (SUCCEEDED(hr)) {
            hr = device->CreatePixelShader(ps_blob->GetBufferPointer(),
                                           ps_blob->GetBufferSize(), nullptr,
                                           &scene_marker_pixel_shader);
        }
        if (SUCCEEDED(hr)) {
            hr = device->CreatePixelShader(pick_ps_blob->GetBufferPointer(),
                                           pick_ps_blob->GetBufferSize(), nullptr,
                                           &scene_marker_pick_pixel_shader);
        }
        if (SUCCEEDED(hr)) {
            hr = device->CreatePixelShader(mask_ps_blob->GetBufferPointer(),
                                           mask_ps_blob->GetBufferSize(), nullptr,
                                           &scene_marker_mask_pixel_shader);
        }
        if (FAILED(hr)) {
            error = hresult_text("CreateShader(scene marker)", hr);
            release_com(vs_blob);
            release_com(ps_blob);
            release_com(pick_ps_blob);
            release_com(mask_ps_blob);
            release_com(scene_marker_vertex_shader);
            release_com(scene_marker_pixel_shader);
            release_com(scene_marker_pick_pixel_shader);
            release_com(scene_marker_mask_pixel_shader);
            return false;
        }

        D3D11_INPUT_ELEMENT_DESC layout[] = {
            {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,
             D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12,
             D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, 20,
             D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"TEXCOORD", 1, DXGI_FORMAT_R32_FLOAT, 0, 24,
             D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"TEXCOORD", 2, DXGI_FORMAT_R32_UINT, 0, 28,
             D3D11_INPUT_PER_VERTEX_DATA, 0},
        };
        hr = device->CreateInputLayout(
            layout, IM_ARRAYSIZE(layout),
            vs_blob->GetBufferPointer(), vs_blob->GetBufferSize(),
            &scene_marker_input_layout);
        release_com(vs_blob);
        release_com(ps_blob);
        release_com(pick_ps_blob);
        release_com(mask_ps_blob);
        if (FAILED(hr)) {
            error = hresult_text("CreateInputLayout(scene marker)", hr);
            release_com(scene_marker_vertex_shader);
            release_com(scene_marker_pixel_shader);
            release_com(scene_marker_pick_pixel_shader);
            release_com(scene_marker_mask_pixel_shader);
            return false;
        }

        D3D11_BUFFER_DESC cb_desc = {};
        cb_desc.ByteWidth = sizeof(SceneMarkerConstants);
        cb_desc.Usage = D3D11_USAGE_DEFAULT;
        cb_desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        hr = device->CreateBuffer(&cb_desc, nullptr,
                                  &scene_marker_constant_buffer);
        if (FAILED(hr)) {
            error = hresult_text("CreateBuffer(scene marker constants)", hr);
            release_com(scene_marker_input_layout);
            release_com(scene_marker_vertex_shader);
            release_com(scene_marker_pixel_shader);
            release_com(scene_marker_pick_pixel_shader);
            release_com(scene_marker_mask_pixel_shader);
            return false;
        }
        cb_desc.ByteWidth = sizeof(SceneMarkerPickConstants);
        hr = device->CreateBuffer(&cb_desc, nullptr,
                                  &scene_marker_pick_constant_buffer);
        if (FAILED(hr)) {
            error = hresult_text("CreateBuffer(scene marker pick constants)", hr);
            release_com(scene_marker_constant_buffer);
            release_com(scene_marker_input_layout);
            release_com(scene_marker_vertex_shader);
            release_com(scene_marker_pixel_shader);
            release_com(scene_marker_pick_pixel_shader);
            release_com(scene_marker_mask_pixel_shader);
            return false;
        }
        return true;
    }

    bool ensure_scene_depth_states(std::string& error) {
        if (scene_depth_state && scene_depth_read_state) return true;
        release_com(scene_depth_state);
        release_com(scene_depth_read_state);

        D3D11_DEPTH_STENCIL_DESC ds_desc = {};
        ds_desc.DepthEnable = TRUE;
        ds_desc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
        ds_desc.DepthFunc = D3D11_COMPARISON_GREATER_EQUAL;
        HRESULT hr = device->CreateDepthStencilState(&ds_desc, &scene_depth_state);
        if (FAILED(hr)) {
            error = hresult_text("CreateDepthStencilState(scene reverse)", hr);
            return false;
        }

        D3D11_DEPTH_STENCIL_DESC ds_read_desc = ds_desc;
        ds_read_desc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
        hr = device->CreateDepthStencilState(&ds_read_desc, &scene_depth_read_state);
        if (FAILED(hr)) {
            error = hresult_text("CreateDepthStencilState(scene reverse read-only)", hr);
            return false;
        }
        return true;
    }

    static SceneInstanceData make_instance_data(const Mat4& world) {
        SceneInstanceData data = {};
        for (int col = 0; col < 4; ++col) {
            data.world0[col] = world.m[0][col];
            data.world1[col] = world.m[1][col];
            data.world2[col] = world.m[2][col];
            data.world3[col] = world.m[3][col];
        }
        return data;
    }

    static SceneInstanceData make_instance_data_relative(const double world[16], DVec3 origin) {
        SceneInstanceData data = {};
        for (int col = 0; col < 4; ++col) {
            data.world0[col] = static_cast<float>(world[col]);
            data.world1[col] = static_cast<float>(world[4 + col]);
            data.world2[col] = static_cast<float>(world[8 + col]);
            data.world3[col] = static_cast<float>(world[12 + col]);
        }
        data.world3[0] = static_cast<float>(world[12] - origin.x);
        data.world3[1] = static_cast<float>(world[13] - origin.y);
        data.world3[2] = static_cast<float>(world[14] - origin.z);
        return data;
    }

    static SceneInstanceData make_chunk_instance_data(DVec3 chunk_origin, DVec3 render_origin) {
        Mat4 world = identity();
        world.m[3][0] = static_cast<float>(chunk_origin.x - render_origin.x);
        world.m[3][1] = static_cast<float>(chunk_origin.y - render_origin.y);
        world.m[3][2] = static_cast<float>(chunk_origin.z - render_origin.z);
        return make_instance_data(world);
    }

    bool ensure_instance_buffer(ID3D11Buffer*& buffer, UINT& capacity,
                                const std::vector<SceneInstanceData>& instances,
                                std::string& error) {
        if (instances.empty()) return true;
        if (instances.size() > static_cast<size_t>(std::numeric_limits<UINT>::max() / sizeof(SceneInstanceData))) {
            error = "too many scene instances for a Direct3D 11 buffer";
            return false;
        }
        UINT needed = static_cast<UINT>(instances.size());
        if (!buffer || capacity < needed) {
            release_com(buffer);
            capacity = std::max<UINT>(needed, 64);
            D3D11_BUFFER_DESC desc = {};
            desc.ByteWidth = capacity * sizeof(SceneInstanceData);
            desc.Usage = D3D11_USAGE_DYNAMIC;
            desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
            desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
            HRESULT hr = device->CreateBuffer(&desc, nullptr, &buffer);
            if (FAILED(hr)) {
                error = hresult_text("CreateBuffer(scene instances)", hr);
                capacity = 0;
                return false;
            }
        }

        D3D11_MAPPED_SUBRESOURCE mapped = {};
        HRESULT hr = context->Map(buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
        if (FAILED(hr)) {
            error = hresult_text("Map(scene instances)", hr);
            return false;
        }
        std::memcpy(mapped.pData, instances.data(), instances.size() * sizeof(SceneInstanceData));
        context->Unmap(buffer, 0);
        return true;
    }

    void release_scene_mileage_highlight_resources() {
        release_com(scene_mileage_highlight_instance_buffer);
        release_com(scene_mileage_highlight_index_buffer);
        release_com(scene_mileage_highlight_vertex_buffer);
        scene_mileage_highlight_instance_capacity = 0;
        scene_mileage_highlight_instances.clear();
        scene_mileage_highlight_parts.clear();
        scene_mileage_highlight_materials.clear();
    }

    bool ensure_scene_mileage_highlight_resources(std::string& error) {
        if (scene_mileage_highlight_vertex_buffer && scene_mileage_highlight_index_buffer &&
            !scene_mileage_highlight_parts.empty() &&
            !scene_mileage_highlight_materials.empty()) {
            return true;
        }
        release_scene_mileage_highlight_resources();

        const std::array<GpuVertex, 4> vertices = {{
            {-static_cast<float>(k_scene_mileage_pick_half_width),
             k_scene_mileage_highlight_bottom, 0.0f, 0.0f, 0.0f, -1.0f, 0.0f, 1.0f},
            { static_cast<float>(k_scene_mileage_pick_half_width),
             k_scene_mileage_highlight_bottom, 0.0f, 0.0f, 0.0f, -1.0f, 1.0f, 1.0f},
            { static_cast<float>(k_scene_mileage_pick_half_width),
             k_scene_mileage_highlight_top, 0.0f, 0.0f, 0.0f, -1.0f, 1.0f, 0.0f},
            {-static_cast<float>(k_scene_mileage_pick_half_width),
             k_scene_mileage_highlight_top, 0.0f, 0.0f, 0.0f, -1.0f, 0.0f, 0.0f},
        }};
        const std::array<unsigned int, 6> indices = {{0, 1, 2, 0, 2, 3}};

        D3D11_BUFFER_DESC vertex_desc = {};
        vertex_desc.ByteWidth = static_cast<UINT>(vertices.size() * sizeof(GpuVertex));
        vertex_desc.Usage = D3D11_USAGE_IMMUTABLE;
        vertex_desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        D3D11_SUBRESOURCE_DATA vertex_data = {};
        vertex_data.pSysMem = vertices.data();
        HRESULT hr = device->CreateBuffer(
            &vertex_desc, &vertex_data, &scene_mileage_highlight_vertex_buffer);
        if (FAILED(hr)) {
            error = hresult_text("CreateBuffer(scene mileage highlight vertex)", hr);
            release_scene_mileage_highlight_resources();
            return false;
        }

        D3D11_BUFFER_DESC index_desc = {};
        index_desc.ByteWidth = static_cast<UINT>(indices.size() * sizeof(unsigned int));
        index_desc.Usage = D3D11_USAGE_IMMUTABLE;
        index_desc.BindFlags = D3D11_BIND_INDEX_BUFFER;
        D3D11_SUBRESOURCE_DATA index_data = {};
        index_data.pSysMem = indices.data();
        hr = device->CreateBuffer(
            &index_desc, &index_data, &scene_mileage_highlight_index_buffer);
        if (FAILED(hr)) {
            error = hresult_text("CreateBuffer(scene mileage highlight index)", hr);
            release_scene_mileage_highlight_resources();
            return false;
        }

        scene_mileage_highlight_parts.push_back(MeshPart{0, 6, 0});
        scene_mileage_highlight_materials.resize(1);
        GpuMaterial& material = scene_mileage_highlight_materials.front();
        material.diffuse[0] = 83.0f / 255.0f;
        material.diffuse[1] = 175.0f / 255.0f;
        material.diffuse[2] = 1.0f;
        material.diffuse[3] = k_scene_mileage_highlight_alpha;
        scene_mileage_highlight_instances.resize(1);
        return true;
    }

    void bind_scene_instanced_mesh(ID3D11Buffer* vb, ID3D11Buffer* ib, ID3D11Buffer* instance_buffer) {
        UINT strides[2] = {sizeof(GpuVertex), sizeof(SceneInstanceData)};
        UINT offsets[2] = {0, 0};
        ID3D11Buffer* buffers[2] = {vb, instance_buffer};
        context->IASetInputLayout(scene_input_layout);
        context->IASetVertexBuffers(0, 2, buffers, strides, offsets);
        context->IASetIndexBuffer(ib, DXGI_FORMAT_R32_UINT, 0);
        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    }

    void draw_scene_mesh(ID3D11Buffer* vb, ID3D11Buffer* ib, ID3D11Buffer* instance_buffer,
                         const std::vector<MeshPart>& mesh_parts,
                         const std::vector<GpuMaterial>& mesh_materials,
                         UINT instance_count,
                         const Mat4& view_proj,
                         ID3D11RasterizerState* base_rasterizer,
                         bool mask_pass = false,
                         const SceneFogSample* fog = nullptr) {
        if (!vb || !ib || !instance_buffer || instance_count == 0) return;

        bind_scene_instanced_mesh(vb, ib, instance_buffer);
        context->VSSetShader(scene_vertex_shader, nullptr, 0);
        const bool fog_active = !mask_pass && fog && fog->enabled && scene_fog_pixel_shader;
        context->PSSetShader(fog_active ? scene_fog_pixel_shader : scene_pixel_shader, nullptr, 0);
        context->VSSetConstantBuffers(0, 1, &scene_constant_buffer);
        context->PSSetConstantBuffers(0, 1, &scene_constant_buffer);
        context->PSSetSamplers(0, 1, &sampler_state);

        const float blend_factor[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        if (!base_rasterizer) base_rasterizer = rasterizer_state;
        for (const MeshPart& part : mesh_parts) {
            const GpuMaterial* material = part.material_index < mesh_materials.size() ? &mesh_materials[part.material_index] : nullptr;
            const bool alpha_masked = scene_material_uses_alpha_mask(material);
            const bool translucent = scene_material_is_translucent(material);
            context->RSSetState(alpha_masked && alpha_mask_rasterizer_state ? alpha_mask_rasterizer_state : base_rasterizer);
            context->OMSetDepthStencilState(
                (mask_pass || translucent) && scene_depth_read_state ? scene_depth_read_state : scene_depth_state, 0);
            context->OMSetBlendState(mask_pass ? nullptr : (translucent ? blend_state : nullptr),
                                     blend_factor, 0xffffffff);

            SceneViewConstants constants = {};
            constants.view_proj = view_proj;
            constants.material_color[0] = mask_pass ? 1.0f : (material ? material->diffuse[0] : 1.0f);
            constants.material_color[1] = mask_pass ? 1.0f : (material ? material->diffuse[1] : 1.0f);
            constants.material_color[2] = mask_pass ? 1.0f : (material ? material->diffuse[2] : 1.0f);
            constants.material_color[3] = material ? material->diffuse[3] : 1.0f;
            constants.use_texture[0] = material && material->has_texture ? 1.0f : 0.0f;
            constants.use_texture[1] = translucent ? 0.0f : 1.0f;
            if (fog_active) {
                constants.fog_color_density[0] = fog->color.x;
                constants.fog_color_density[1] = fog->color.y;
                constants.fog_color_density[2] = fog->color.z;
                constants.fog_color_density[3] = fog->density;
            }
            context->UpdateSubresource(scene_constant_buffer, 0, nullptr, &constants, 0, 0);
            ID3D11ShaderResourceView* texture = material && material->has_texture ? material->texture : nullptr;
            context->PSSetShaderResources(0, 1, &texture);
            context->DrawIndexedInstanced(part.index_count, instance_count, part.start_index, 0, 0);
#ifndef NDEBUG
            if (fog_active) ++debug_scene_fog_draw_part_count;
#endif
        }
        ID3D11ShaderResourceView* null_srv = nullptr;
        context->PSSetShaderResources(0, 1, &null_srv);
        context->RSSetState(base_rasterizer);
        context->OMSetDepthStencilState(scene_depth_state, 0);
        context->OMSetBlendState(nullptr, blend_factor, 0xffffffff);
    }

    void draw_scene_model(SceneModelGpu& model,
                          const std::vector<SceneInstanceData>& instances,
                          const Mat4& view_proj,
                          const SceneFogSample* fog = nullptr) {
        if (model.state != SceneModelGpu::State::Ready || instances.empty()) return;
        std::string error;
        if (!ensure_instance_buffer(model.instance_buffer, model.instance_capacity, instances, error)) {
            scene_last_error = error;
            return;
        }
        draw_scene_mesh(model.vertex_buffer, model.index_buffer, model.instance_buffer,
                        model.parts, model.materials, static_cast<UINT>(instances.size()),
                        view_proj, rasterizer_state, false, fog);
    }

    void draw_scene_mileage_highlight(DVec3 render_origin,
                                      const Mat4& view_proj,
                                      std::string& error) {
        if (!scene_hovered_mileage) return;
        Canvas3DTrackPoint point;
        if (!sample_own_track(*scene_hovered_mileage, point) ||
            !ensure_scene_mileage_highlight_resources(error)) {
            return;
        }

        DVec3 right;
        DVec3 up;
        DVec3 forward;
        scene_track_surface_frame(point, right, up, forward);
        double world[16] = {};
        store_world(world, right, up, forward, DVec3{point.x, point.y, point.z});
        scene_mileage_highlight_instances.front() =
            make_instance_data_relative(world, render_origin);
        if (!ensure_instance_buffer(scene_mileage_highlight_instance_buffer,
                                    scene_mileage_highlight_instance_capacity,
                                    scene_mileage_highlight_instances, error)) {
            return;
        }

        draw_scene_mesh(scene_mileage_highlight_vertex_buffer,
                        scene_mileage_highlight_index_buffer,
                        scene_mileage_highlight_instance_buffer,
                        scene_mileage_highlight_parts,
                        scene_mileage_highlight_materials,
                        1, view_proj, track_rasterizer_state, false, nullptr);
    }

    bool draw_scene_pick_model(SceneModelGpu& model,
                               const std::vector<SceneInstanceData>& instances,
                               const Mat4& view_proj,
                               int object_index,
                               std::string& error) {
        if (model.state != SceneModelGpu::State::Ready || instances.empty()) return true;
        unsigned int pick_id = 0;
        if (!scene_pick_id_for_object(object_index, pick_id)) return true;
        if (!ensure_instance_buffer(model.instance_buffer, model.instance_capacity, instances, error)) return false;

        bind_scene_instanced_mesh(model.vertex_buffer, model.index_buffer, model.instance_buffer);
        context->VSSetShader(scene_vertex_shader, nullptr, 0);
        context->PSSetShader(scene_pick_pixel_shader, nullptr, 0);
        context->VSSetConstantBuffers(0, 1, &scene_constant_buffer);
        context->PSSetConstantBuffers(1, 1, &scene_pick_constant_buffer);
        context->PSSetSamplers(0, 1, &sampler_state);
        context->OMSetDepthStencilState(scene_depth_read_state, 0);

        SceneViewConstants view_constants = {};
        view_constants.view_proj = view_proj;
        context->UpdateSubresource(scene_constant_buffer, 0, nullptr, &view_constants, 0, 0);

        const std::array<float, 4> pick_color = scene_pick_color_for_id(pick_id);
        const float blend_factor[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        context->OMSetBlendState(nullptr, blend_factor, 0xffffffff);
        for (const MeshPart& part : model.parts) {
            const GpuMaterial* material = part.material_index < model.materials.size() ? &model.materials[part.material_index] : nullptr;
            const bool alpha_masked = scene_material_uses_alpha_mask(material);
            context->RSSetState(alpha_masked && alpha_mask_rasterizer_state ? alpha_mask_rasterizer_state : rasterizer_state);

            ScenePickConstants constants = {};
            constants.pick_color[0] = pick_color[0];
            constants.pick_color[1] = pick_color[1];
            constants.pick_color[2] = pick_color[2];
            constants.pick_color[3] = pick_color[3];
            constants.alpha_controls[0] = material ? material->diffuse[3] : 1.0f;
            constants.alpha_controls[1] = material && material->has_texture ? 1.0f : 0.0f;
            context->UpdateSubresource(scene_pick_constant_buffer, 0, nullptr, &constants, 0, 0);
            ID3D11ShaderResourceView* texture = material && material->has_texture ? material->texture : nullptr;
            context->PSSetShaderResources(0, 1, &texture);
            context->DrawIndexedInstanced(part.index_count, static_cast<UINT>(instances.size()), part.start_index, 0, 0);
        }

        ID3D11ShaderResourceView* null_srv = nullptr;
        context->PSSetShaderResources(0, 1, &null_srv);
        ID3D11Buffer* null_buffer = nullptr;
        context->PSSetConstantBuffers(1, 1, &null_buffer);
        context->RSSetState(rasterizer_state);
        context->OMSetDepthStencilState(scene_depth_state, 0);
        context->OMSetBlendState(nullptr, blend_factor, 0xffffffff);
        return true;
    }

    bool begin_scene_pick_at_mouse(
        const std::map<std::string, std::vector<SceneInstanceData>>& visible_instances,
        const std::map<int, std::vector<SceneVisibleInstanceRef>>& object_refs,
        const Mat4& view_proj,
        int width,
        int height,
        ImVec2 mouse_local,
        std::string& error) {
        const int pixel_x = static_cast<int>(std::floor(mouse_local.x));
        const int pixel_y = static_cast<int>(std::floor(mouse_local.y));
        if (pixel_x < 0 || pixel_y < 0 || pixel_x >= width || pixel_y >= height) return false;
        if (!ensure_scene_pick_target(width, height, error)) return false;

        using PickBatchKey = std::pair<int, std::string>;
        std::map<PickBatchKey, std::vector<SceneInstanceData>> pick_batches;
        for (const auto& object_kv : object_refs) {
            const int object_index = object_kv.first;
            if (!scene_object_index_valid(object_index)) continue;
            unsigned int pick_id = 0;
            if (!scene_pick_id_for_object(object_index, pick_id)) continue;
            for (const SceneVisibleInstanceRef& ref : object_kv.second) {
                if (!ref.model_path) continue;
                auto visible_it = visible_instances.find(*ref.model_path);
                if (visible_it == visible_instances.end() || ref.instance_index >= visible_it->second.size()) continue;
                pick_batches[PickBatchKey{object_index, *ref.model_path}].push_back(visible_it->second[ref.instance_index]);
            }
        }
        const float clear_id[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        context->ClearRenderTargetView(scene_pick_rtv, clear_id);
        ID3D11RenderTargetView* pick_target = scene_pick_rtv;
        context->OMSetRenderTargets(1, &pick_target, depth_dsv);

        D3D11_VIEWPORT viewport = {};
        viewport.Width = static_cast<float>(width);
        viewport.Height = static_cast<float>(height);
        viewport.MinDepth = 0.0f;
        viewport.MaxDepth = 1.0f;
        context->RSSetViewports(1, &viewport);

        for (auto& kv : pick_batches) {
            const int object_index = kv.first.first;
            const std::string& model_path = kv.first.second;
            auto model_it = scene_models.find(model_path);
            if (model_it == scene_models.end()) continue;
            if (!draw_scene_pick_model(model_it->second, kv.second, view_proj, object_index, error)) {
                if (!error.empty()) scene_last_error = error;
            }
        }

        return true;
    }

    ScenePickTarget finish_scene_pick_at_mouse(int width,
                                                int height,
                                                ImVec2 mouse_local,
                                                std::string& error) {
        const int pixel_x = static_cast<int>(std::floor(mouse_local.x));
        const int pixel_y = static_cast<int>(std::floor(mouse_local.y));
        if (pixel_x < 0 || pixel_y < 0 || pixel_x >= width || pixel_y >= height) return {};

        ID3D11RenderTargetView* null_rtv = nullptr;
        context->OMSetRenderTargets(1, &null_rtv, nullptr);

        D3D11_BOX box = {};
        box.left = static_cast<UINT>(pixel_x);
        box.top = static_cast<UINT>(pixel_y);
        box.front = 0;
        box.right = static_cast<UINT>(pixel_x + 1);
        box.bottom = static_cast<UINT>(pixel_y + 1);
        box.back = 1;
        context->CopySubresourceRegion(scene_pick_readback_texture, 0, 0, 0, 0, scene_pick_texture, 0, &box);

        D3D11_MAPPED_SUBRESOURCE mapped = {};
        HRESULT hr = context->Map(scene_pick_readback_texture, 0, D3D11_MAP_READ, 0, &mapped);
        if (FAILED(hr)) {
            error = hresult_text("Map(scene pick readback)", hr);
            return {};
        }
        unsigned char rgba[4] = {};
        if (mapped.pData) std::memcpy(rgba, mapped.pData, sizeof(rgba));
        context->Unmap(scene_pick_readback_texture, 0);

        return scene_pick_target_from_id(
            scene_pick_id_from_rgba(rgba), scene_data.objects.size(),
            scene_data.markers.size());
    }

    void composite_scene_highlight_outline(int width, int height,
                                           ImVec2 screen_min,
                                           ImVec2 screen_max,
                                           ImVec4 color) {
        if (!render_rtv || !scene_highlight_mask_srv ||
            !scene_outline_vertex_shader || !scene_outline_pixel_shader ||
            !scene_outline_constant_buffer || !scene_outline_sampler_state ||
            width <= 0 || height <= 0) {
            return;
        }

        const float left = std::floor(std::clamp(screen_min.x, 0.0f, static_cast<float>(width)));
        const float top = std::floor(std::clamp(screen_min.y, 0.0f, static_cast<float>(height)));
        const float right = std::ceil(std::clamp(screen_max.x, 0.0f, static_cast<float>(width)));
        const float bottom = std::ceil(std::clamp(screen_max.y, 0.0f, static_cast<float>(height)));
        if (right <= left || bottom <= top) return;

        SceneOutlineConstants constants = {};
        constants.texel_radius[0] = 1.0f / static_cast<float>(width);
        constants.texel_radius[1] = 1.0f / static_cast<float>(height);
        constants.texel_radius[2] = k_scene_highlight_outline_width_px * 0.5f;
        constants.texel_radius[3] = 0.0f;
        constants.color[0] = clamp_color_component(color.x);
        constants.color[1] = clamp_color_component(color.y);
        constants.color[2] = clamp_color_component(color.z);
        constants.color[3] = clamp_color_component(color.w);
        context->UpdateSubresource(scene_outline_constant_buffer, 0, nullptr, &constants, 0, 0);

        D3D11_VIEWPORT outline_viewport = {};
        outline_viewport.TopLeftX = left;
        outline_viewport.TopLeftY = top;
        outline_viewport.Width = right - left;
        outline_viewport.Height = bottom - top;
        outline_viewport.MinDepth = 0.0f;
        outline_viewport.MaxDepth = 1.0f;
        context->RSSetViewports(1, &outline_viewport);

        ID3D11RenderTargetView* target = render_rtv;
        context->OMSetRenderTargets(1, &target, nullptr);
        context->IASetInputLayout(nullptr);
        ID3D11Buffer* null_buffer = nullptr;
        UINT zero = 0;
        context->IASetVertexBuffers(0, 1, &null_buffer, &zero, &zero);
        context->IASetIndexBuffer(nullptr, DXGI_FORMAT_UNKNOWN, 0);
        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context->VSSetShader(scene_outline_vertex_shader, nullptr, 0);
        context->PSSetShader(scene_outline_pixel_shader, nullptr, 0);
        context->PSSetConstantBuffers(0, 1, &scene_outline_constant_buffer);
        context->PSSetShaderResources(0, 1, &scene_highlight_mask_srv);
        context->PSSetSamplers(0, 1, &scene_outline_sampler_state);
        context->RSSetState(track_rasterizer_state ? track_rasterizer_state : rasterizer_state);
        context->OMSetDepthStencilState(nullptr, 0);
        const float blend_factor[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        context->OMSetBlendState(blend_state, blend_factor, 0xffffffff);
        context->Draw(3, 0);

        ID3D11ShaderResourceView* null_srv = nullptr;
        context->PSSetShaderResources(0, 1, &null_srv);
        context->OMSetBlendState(nullptr, blend_factor, 0xffffffff);
        context->OMSetDepthStencilState(scene_depth_state, 0);
        context->RSSetState(rasterizer_state);

        D3D11_VIEWPORT full_viewport = {};
        full_viewport.Width = static_cast<float>(width);
        full_viewport.Height = static_cast<float>(height);
        full_viewport.MinDepth = 0.0f;
        full_viewport.MaxDepth = 1.0f;
        context->RSSetViewports(1, &full_viewport);
    }

    void draw_scene_highlight_mask_batches(const std::map<std::string, std::vector<SceneInstanceData>>& batches,
                                           const Mat4& view_proj,
                                           std::string& error) {
        ID3D11RenderTargetView* mask_target = scene_highlight_mask_rtv;
        context->OMSetRenderTargets(1, &mask_target, nullptr);

        for (const auto& kv : batches) {
            auto model_it = scene_models.find(kv.first);
            if (model_it == scene_models.end()) continue;
            SceneModelGpu& model = model_it->second;
            if (model.state != SceneModelGpu::State::Ready || kv.second.empty()) continue;
            if (!ensure_instance_buffer(model.instance_buffer, model.instance_capacity, kv.second, error)) {
                scene_last_error = error;
                continue;
            }
            draw_scene_mesh(model.vertex_buffer, model.index_buffer, model.instance_buffer,
                            model.parts, model.materials,
                            static_cast<UINT>(kv.second.size()),
                            view_proj, rasterizer_state, true);
        }
    }

    void draw_scene_model_highlight_outlines(const std::vector<SceneHighlightInstance>& instances,
                                             const Mat4& view_proj,
                                             int width,
                                             int height,
                                             ImVec2 screen_min,
                                             ImVec2 screen_max,
                                             ImVec4 color,
                                             bool separate_instances) {
        if (instances.empty() || !render_rtv) return;
        std::string error;
        if (!ensure_scene_highlight_mask_target(width, height, error)) {
            if (!error.empty()) scene_last_error = error;
            return;
        }

        ID3D11ShaderResourceView* null_srv = nullptr;
        context->PSSetShaderResources(0, 1, &null_srv);
        const float clear_mask[4] = {0.0f, 0.0f, 0.0f, 0.0f};

        if (separate_instances) {
            std::map<std::string, std::vector<SceneInstanceData>> batch;
            for (const SceneHighlightInstance& instance : instances) {
                batch.clear();
                batch[instance.model_path].push_back(instance.instance_data);
                context->ClearRenderTargetView(scene_highlight_mask_rtv, clear_mask);
                draw_scene_highlight_mask_batches(batch, view_proj, error);
                composite_scene_highlight_outline(width, height, instance.screen_min, instance.screen_max, color);
            }
            return;
        }

        std::map<std::string, std::vector<SceneInstanceData>> batches;
        for (const SceneHighlightInstance& instance : instances) {
            batches[instance.model_path].push_back(instance.instance_data);
        }
        context->ClearRenderTargetView(scene_highlight_mask_rtv, clear_mask);
        draw_scene_highlight_mask_batches(batches, view_proj, error);
        composite_scene_highlight_outline(width, height, screen_min, screen_max, color);
    }

    void draw_scene_highlight_batch(const SceneHighlightBatch& batch,
                                    const Mat4& view_proj,
                                    int width,
                                    int height) {
        if (!batch.valid() || !scene_object_index_valid(batch.object_index)) return;
        const Canvas3DSceneObject& object = scene_data.objects[static_cast<size_t>(batch.object_index)];
        draw_scene_model_highlight_outlines(batch.instances,
                                            view_proj, width, height,
                                            batch.screen_min,
                                            batch.screen_max,
                                            scene_highlight_color_for_kind(object.kind),
                                            object.kind == Canvas3DSceneObjectKind::Repeater);
    }


    static bool scene_chunk_visible(const SceneChunk& chunk, double visible_min, double visible_max) {
        return chunk.d_max >= visible_min && chunk.d_min <= visible_max;
    }

    void draw_scene_track_chunk(SceneTrackChunkGpu& track,
                                DVec3 render_origin,
                                const Mat4& view_proj,
                                std::vector<SceneInstanceData>& track_instance,
                                std::string& error,
                                const SceneFogSample* fog = nullptr) {
        if (!track.vertex_buffer || !track.index_buffer || track.index_count == 0) return;

        track_instance[0] = make_chunk_instance_data(track.origin, render_origin);
        if (!ensure_instance_buffer(track.instance_buffer, track.instance_capacity, track_instance, error)) {
            if (!error.empty()) scene_last_error = error;
            return;
        }
        draw_scene_mesh(track.vertex_buffer, track.index_buffer, track.instance_buffer,
                        track.parts, track.materials, 1, view_proj,
                        track_rasterizer_state, false, fog);
        ++scene_stats_value.drawn_track_chunk_count;
    }

    bool bind_scene_marker_chunk(const SceneMarkerChunkGpu& chunk,
                                 ID3D11Buffer* index_buffer,
                                 DVec3 render_origin,
                                 const Mat4& view_proj) {
        if (!chunk.vertex_buffer || !index_buffer ||
            scene_marker_font_texture_id == ImTextureID_Invalid) {
            return false;
        }

        SceneMarkerConstants constants = {};
        constants.view_proj = view_proj;
        constants.chunk_offset[0] =
            static_cast<float>(chunk.origin.x - render_origin.x);
        constants.chunk_offset[1] =
            static_cast<float>(chunk.origin.y - render_origin.y);
        constants.chunk_offset[2] =
            static_cast<float>(chunk.origin.z - render_origin.z);
        context->UpdateSubresource(
            scene_marker_constant_buffer, 0, nullptr, &constants, 0, 0);

        UINT stride = sizeof(SceneMarkerVertex);
        UINT offset = 0;
        ID3D11Buffer* vertex_buffer = chunk.vertex_buffer;
        context->IASetInputLayout(scene_marker_input_layout);
        context->IASetVertexBuffers(
            0, 1, &vertex_buffer, &stride, &offset);
        context->IASetIndexBuffer(
            index_buffer, DXGI_FORMAT_R32_UINT, 0);
        context->IASetPrimitiveTopology(
            D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context->VSSetShader(scene_marker_vertex_shader, nullptr, 0);
        context->VSSetConstantBuffers(
            0, 1, &scene_marker_constant_buffer);
        context->PSSetSamplers(0, 1, &sampler_state);
        ID3D11ShaderResourceView* font_srv =
            reinterpret_cast<ID3D11ShaderResourceView*>(
                static_cast<std::uintptr_t>(
                    scene_marker_font_texture_id));
        context->PSSetShaderResources(0, 1, &font_srv);
        context->RSSetState(rasterizer_state);
        return true;
    }

    void draw_scene_marker_chunk(const SceneMarkerChunkGpu& chunk,
                                 DVec3 render_origin,
                                 const Mat4& view_proj) {
        if (chunk.visible_index_count == 0 ||
            !bind_scene_marker_chunk(
                chunk, chunk.index_buffer, render_origin, view_proj)) {
            return;
        }

        context->PSSetShader(scene_marker_pixel_shader, nullptr, 0);
        context->OMSetDepthStencilState(scene_depth_read_state, 0);
        const float blend_factor[4] = {
            0.0f, 0.0f, 0.0f, 0.0f};
        context->OMSetBlendState(
            blend_state, blend_factor, 0xffffffff);
        context->DrawIndexed(chunk.visible_index_count, 0, 0);
    }

    bool scene_marker_pick_ids_valid() const {
        constexpr size_t max_pick_id = 0x00ffffffu;
        return scene_data.objects.size() <= max_pick_id &&
            scene_data.markers.size() <= max_pick_id - scene_data.objects.size();
    }

    bool draw_scene_marker_pick_chunk(const SceneMarkerChunkGpu& chunk,
                                      DVec3 render_origin,
                                      const Mat4& view_proj) {
        if (chunk.visible_pick_index_count == 0 ||
            !scene_marker_pick_ids_valid() ||
            !scene_marker_pick_pixel_shader ||
            !scene_marker_pick_constant_buffer ||
            !bind_scene_marker_chunk(
                chunk, chunk.pick_index_buffer, render_origin, view_proj)) {
            return false;
        }

        SceneMarkerPickConstants constants = {};
        constants.marker_pick_id_base = static_cast<std::uint32_t>(
            scene_data.objects.size() + 1u);
        context->UpdateSubresource(
            scene_marker_pick_constant_buffer, 0, nullptr, &constants, 0, 0);
        context->PSSetShader(scene_marker_pick_pixel_shader, nullptr, 0);
        context->PSSetConstantBuffers(
            1, 1, &scene_marker_pick_constant_buffer);
        context->OMSetDepthStencilState(scene_depth_read_state, 0);
        const float blend_factor[4] = {
            0.0f, 0.0f, 0.0f, 0.0f};
        context->OMSetBlendState(nullptr, blend_factor, 0xffffffff);
        context->DrawIndexed(chunk.visible_pick_index_count, 0, 0);
        ID3D11Buffer* null_buffer = nullptr;
        context->PSSetConstantBuffers(1, 1, &null_buffer);
        return true;
    }

    bool scene_marker_font_cache_current() const {
        ImFontAtlas* atlas = ImGui::GetIO().Fonts;
        if (!atlas || !atlas->TexData) return false;
        return scene_marker_font == ImGui::GetFont() &&
            scene_marker_font_size == ImGui::GetFontSize() &&
            scene_marker_font_texture_id == atlas->TexRef.GetTexID() &&
            scene_marker_font_texture_unique_id ==
                atlas->TexData->UniqueID &&
            scene_marker_font_texture_width == atlas->TexData->Width &&
            scene_marker_font_texture_height == atlas->TexData->Height;
    }

    void draw_visible_scene_markers(double visible_min,
                                    double visible_max,
                                    DVec3 render_origin,
                                    const Mat4& view_proj) {
        if (scene_marker_chunks.empty()) return;
        size_t camera_chunk = 0;
        while (camera_chunk + 1 < scene_marker_chunks.size() &&
               scene_marker_chunks[camera_chunk].d_max <
                   scene_camera_distance) {
            ++camera_chunk;
        }

        for (size_t i = scene_marker_chunks.size();
             i-- > camera_chunk;) {
            const SceneMarkerChunkGpu& chunk = scene_marker_chunks[i];
            if (chunk.d_max < visible_min || chunk.d_min > visible_max) {
                continue;
            }
            draw_scene_marker_chunk(chunk, render_origin, view_proj);
        }
        for (size_t i = 0; i < camera_chunk; ++i) {
            const SceneMarkerChunkGpu& chunk = scene_marker_chunks[i];
            if (chunk.d_max < visible_min || chunk.d_min > visible_max) {
                continue;
            }
            draw_scene_marker_chunk(chunk, render_origin, view_proj);
        }
    }

    bool draw_visible_scene_marker_picks(double visible_min,
                                         double visible_max,
                                         DVec3 render_origin,
                                         const Mat4& view_proj) {
        if (scene_marker_chunks.empty() || !scene_marker_pick_ids_valid()) return false;
        bool drew = false;
        size_t camera_chunk = 0;
        while (camera_chunk + 1 < scene_marker_chunks.size() &&
               scene_marker_chunks[camera_chunk].d_max <
                   scene_camera_distance) {
            ++camera_chunk;
        }

        const auto draw_chunk = [&](const SceneMarkerChunkGpu& chunk) {
            if (chunk.d_max < visible_min || chunk.d_min > visible_max) return;
            drew = draw_scene_marker_pick_chunk(
                chunk, render_origin, view_proj) || drew;
        };
        for (size_t i = scene_marker_chunks.size(); i-- > camera_chunk;) {
            draw_chunk(scene_marker_chunks[i]);
        }
        for (size_t i = 0; i < camera_chunk; ++i) {
            draw_chunk(scene_marker_chunks[i]);
        }
        return drew;
    }

    bool has_visible_scene_marker_picks(double visible_min,
                                        double visible_max) const {
        if (!scene_marker_pick_ids_valid()) return false;
        for (const SceneMarkerChunkGpu& chunk : scene_marker_chunks) {
            if (chunk.d_max < visible_min || chunk.d_min > visible_max) {
                continue;
            }
            if (chunk.visible_pick_index_count > 0) return true;
        }
        return false;
    }

    bool scene_object_index_valid(int object_index) const {
        return object_index >= 0 && static_cast<size_t>(object_index) < scene_data.objects.size();
    }

    bool project_scene_point(DVec3 relative_point,
                             const Mat4& view_proj,
                             int width,
                             int height,
                             ImVec2& screen) const {
        if (width <= 0 || height <= 0) return false;
        Vec4 clip = transform_point_row({
            static_cast<float>(relative_point.x),
            static_cast<float>(relative_point.y),
            static_cast<float>(relative_point.z)
        }, view_proj);
        if (!std::isfinite(clip.w) || clip.w <= 1e-5f) return false;
        const float inv_w = 1.0f / clip.w;
        const float ndc_x = clip.x * inv_w;
        const float ndc_y = clip.y * inv_w;
        if (!std::isfinite(ndc_x) || !std::isfinite(ndc_y)) return false;
        screen.x = (ndc_x * 0.5f + 0.5f) * static_cast<float>(width);
        screen.y = (-ndc_y * 0.5f + 0.5f) * static_cast<float>(height);
        return true;
    }

    void draw_scene_marker_highlight(size_t marker_index,
                                     DVec3 render_origin,
                                     const Mat4& view_proj,
                                     int width,
                                     int height) {
        if (marker_index >= scene_marker_locations.size() || !render_rtv) return;
        const SceneMarkerGpuLocation& location =
            scene_marker_locations[marker_index];
        if (!location.valid || location.chunk_index >= scene_marker_chunks.size()) return;
        const SceneMarkerChunkGpu& chunk =
            scene_marker_chunks[location.chunk_index];
        if (location.range_index >= chunk.ranges.size()) return;
        const SceneMarkerIndexRange& range = chunk.ranges[location.range_index];
        if (range.label || !range.visible || range.count == 0) return;

        std::string error;
        if (!ensure_scene_highlight_mask_target(width, height, error)) {
            if (!error.empty()) scene_last_error = error;
            return;
        }

        const float clear_mask[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        context->ClearRenderTargetView(scene_highlight_mask_rtv, clear_mask);
        ID3D11RenderTargetView* mask_target = scene_highlight_mask_rtv;
        context->OMSetRenderTargets(1, &mask_target, nullptr);
        if (!scene_marker_mask_pixel_shader ||
            !bind_scene_marker_chunk(
                chunk, chunk.index_buffer, render_origin, view_proj)) {
            return;
        }
        context->PSSetShader(scene_marker_mask_pixel_shader, nullptr, 0);
        context->OMSetDepthStencilState(nullptr, 0);
        const float blend_factor[4] = {
            0.0f, 0.0f, 0.0f, 0.0f};
        context->OMSetBlendState(nullptr, blend_factor, 0xffffffff);
        context->DrawIndexed(range.count, range.visible_first, 0);

        ImVec2 screen_min(
            static_cast<float>(width), static_cast<float>(height));
        ImVec2 screen_max(0.0f, 0.0f);
        bool projected = false;
        const float marker_bottom = range.kind == MapMarkerVisualKind::MapSound3D
            ? -k_scene_marker_sound3d_tag_tip_height : 0.0f;
        for (float x : {-0.5f, 0.5f}) {
            for (float y : {marker_bottom, 1.0f}) {
                const DVec3 world_point =
                    range.center +
                    range.right * static_cast<double>(
                        x * k_scene_marker_board_width) +
                    range.up * static_cast<double>(y);
                ImVec2 screen;
                if (!project_scene_point(
                        world_point - render_origin, view_proj,
                        width, height, screen)) {
                    continue;
                }
                projected = true;
                screen_min.x = std::min(screen_min.x, screen.x);
                screen_min.y = std::min(screen_min.y, screen.y);
                screen_max.x = std::max(screen_max.x, screen.x);
                screen_max.y = std::max(screen_max.y, screen.y);
            }
        }
        if (!projected) return;
        const float outline_padding = k_scene_highlight_outline_width_px + 2.0f;
        screen_min.x -= outline_padding;
        screen_min.y -= outline_padding;
        screen_max.x += outline_padding;
        screen_max.y += outline_padding;
        composite_scene_highlight_outline(
            width, height, screen_min, screen_max,
            scene_marker_highlight_color());
    }

    bool compute_scene_instance_screen_bounds(const double world[16],
                                              const SceneModelGpu& model,
                                              DVec3 render_origin,
                                              const Mat4& view_proj,
                                              int width,
                                              int height,
                                              SceneScreenBounds& out) const {
        if (model.state != SceneModelGpu::State::Ready) return false;

        const Vec3 bounds_min = scene_bounds_min_or_sphere(model.bounds_min, model.bounds_max,
                                                           model.center, model.radius);
        const Vec3 bounds_max = scene_bounds_max_or_sphere(model.bounds_min, model.bounds_max,
                                                           model.center, model.radius);
        const DVec3 forward = dvec3_from_vec3(scene_forward());
        bool projected = false;
        ImVec2 raw_min(std::numeric_limits<float>::max(), std::numeric_limits<float>::max());
        ImVec2 raw_max(-std::numeric_limits<float>::max(), -std::numeric_limits<float>::max());
        for (Vec3 corner : bounds_corners(bounds_min, bounds_max)) {
            DVec3 world_point = transform_point_row(world, corner);
            DVec3 relative_point = world_point - render_origin;
            const double view_depth = dot(relative_point, forward);
            if (!std::isfinite(view_depth) || view_depth <= static_cast<double>(k_scene_near_z)) continue;
            ImVec2 screen;
            if (!project_scene_point(relative_point, view_proj, width, height, screen)) continue;
            raw_min.x = std::min(raw_min.x, screen.x);
            raw_min.y = std::min(raw_min.y, screen.y);
            raw_max.x = std::max(raw_max.x, screen.x);
            raw_max.y = std::max(raw_max.y, screen.y);
            projected = true;
        }
        if (!projected) return false;

        const float outline_padding = k_scene_highlight_outline_width_px + 2.0f;
        out.screen_min = ImVec2(std::max(0.0f, raw_min.x - outline_padding),
                                std::max(0.0f, raw_min.y - outline_padding));
        out.screen_max = ImVec2(std::min(static_cast<float>(width), raw_max.x + outline_padding),
                                std::min(static_cast<float>(height), raw_max.y + outline_padding));
        return true;
    }

    bool ensure_pipeline(std::string& error) {
        if (!device || !context) {
            error = "Direct3D device is not available";
            return false;
        }
        if (rasterizer_state && alpha_mask_rasterizer_state &&
            track_rasterizer_state && sampler_state && blend_state) return true;

        D3D11_RASTERIZER_DESC rs_desc = {};
        rs_desc.FillMode = D3D11_FILL_SOLID;
        rs_desc.CullMode = D3D11_CULL_BACK;
        rs_desc.FrontCounterClockwise = TRUE;
        rs_desc.DepthClipEnable = TRUE;
        if (!rasterizer_state) {
            HRESULT hr = device->CreateRasterizerState(&rs_desc, &rasterizer_state);
            if (FAILED(hr)) {
                error = hresult_text("CreateRasterizerState", hr);
                return false;
            }
        }
        if (!alpha_mask_rasterizer_state) {
            D3D11_RASTERIZER_DESC alpha_mask_rs_desc = rs_desc;
            alpha_mask_rs_desc.DepthBias = 8;
            HRESULT hr = device->CreateRasterizerState(&alpha_mask_rs_desc, &alpha_mask_rasterizer_state);
            if (FAILED(hr)) {
                error = hresult_text("CreateRasterizerState(alpha mask)", hr);
                return false;
            }
        }
        if (!track_rasterizer_state) {
            D3D11_RASTERIZER_DESC track_rs_desc = rs_desc;
            track_rs_desc.CullMode = D3D11_CULL_NONE;
            HRESULT hr = device->CreateRasterizerState(&track_rs_desc, &track_rasterizer_state);
            if (FAILED(hr)) {
                error = hresult_text("CreateRasterizerState(track)", hr);
                return false;
            }
        }
        if (!sampler_state) {
            D3D11_SAMPLER_DESC sampler_desc = {};
            sampler_desc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
            sampler_desc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
            sampler_desc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
            sampler_desc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
            sampler_desc.ComparisonFunc = D3D11_COMPARISON_NEVER;
            sampler_desc.MinLOD = 0.0f;
            sampler_desc.MaxLOD = D3D11_FLOAT32_MAX;
            HRESULT hr = device->CreateSamplerState(&sampler_desc, &sampler_state);
            if (FAILED(hr)) {
                error = hresult_text("CreateSamplerState", hr);
                return false;
            }
        }

        if (!blend_state) {
            D3D11_BLEND_DESC blend_desc = {};
            blend_desc.RenderTarget[0].BlendEnable = TRUE;
            blend_desc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
            blend_desc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
            blend_desc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
            blend_desc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
            blend_desc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
            blend_desc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
            blend_desc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
            HRESULT hr = device->CreateBlendState(&blend_desc, &blend_state);
            if (FAILED(hr)) {
                error = hresult_text("CreateBlendState", hr);
                return false;
            }
        }

        return true;
    }

    bool build_scene_chunks(std::string& error) {
        scene_chunks.clear();
        scene_placement_locations.clear();
        scene_repeater_locations.clear();
        if (!std::isfinite(scene_chunk_m) || scene_chunk_m <= 0.0) {
            error = "3D scene chunk size is invalid";
            return false;
        }
        double min_d = scene_data.min_distance;
        double max_d = scene_data.max_distance;
        if (max_d <= min_d) {
            min_d = scene_camera_distance - scene_window_back_m;
            max_d = scene_camera_distance + scene_window_forward_m;
        }
        if (!std::isfinite(min_d) || !std::isfinite(max_d)) {
            error = "3D scene distance range is not finite";
            return false;
        }
        const double first = std::floor(min_d / scene_chunk_m) * scene_chunk_m;
        const double last = std::ceil(max_d / scene_chunk_m) * scene_chunk_m;
        const double count_value = std::max(1.0, (last - first) / scene_chunk_m);
        if (!std::isfinite(first) || !std::isfinite(last) ||
            !std::isfinite(count_value) ||
            count_value > static_cast<double>(k_scene_chunk_count_limit)) {
            error = "3D scene chunk count exceeds the supported limit of " +
                std::to_string(k_scene_chunk_count_limit);
            return false;
        }
        const size_t count = static_cast<size_t>(count_value);
        scene_chunks.resize(count);
        for (size_t i = 0; i < count; ++i) {
            scene_chunks[i].d_min = first + static_cast<double>(i) * scene_chunk_m;
            scene_chunks[i].d_max = scene_chunks[i].d_min + scene_chunk_m;
            Canvas3DTrackPoint origin_point;
            if (sample_own_track(scene_chunks[i].d_min, origin_point)) {
                scene_chunks[i].origin = {origin_point.x, origin_point.y, origin_point.z};
            }
        }
        for (size_t source_index = 0; source_index < scene_data.instances.size(); ++source_index) {
            const Canvas3DModelInstance& source = scene_data.instances[source_index];
            if (source.model_path.empty()) continue;
            const size_t index = scene_chunk_index_for_distance(source.distance);
            SceneInstance instance;
            instance.model_path = scene_model_key_for_instance(source, scene_geometry_generation);
            instance.distance = source.distance;
            instance.object_index = source.object_index;
            if (source.follow_track) {
                if (!make_track_world(source.track_key, source.distance,
                                      source.x, source.y, source.z,
                                      source.rx, source.ry, source.rz,
                                      source.tilt, source.span,
                                      instance.world)) {
                    continue;
                }
            } else {
                std::copy(source.world, source.world + 16, instance.world);
            }
            SceneChunk& chunk = scene_chunks[index];
            const size_t chunk_instance_index = chunk.instances.size();
            chunk.instances.push_back(std::move(instance));
            if (scene_object_index_valid(source.object_index)) {
                const Canvas3DSceneObject& object =
                    scene_data.objects[static_cast<size_t>(source.object_index)];
                const bool editable_placement =
                    object.kind == Canvas3DSceneObjectKind::Structure ||
                    object.kind == Canvas3DSceneObjectKind::Signal;
                if (editable_placement && !object.edit_id.empty()) {
                    scene_placement_locations[object.edit_id] = ScenePlacementInstanceLocation{
                        source_index,
                        index,
                        chunk_instance_index
                    };
                }
            }
        }
        for (size_t repeater_index = 0; repeater_index < scene_data.repeaters.size(); ++repeater_index) {
            const Canvas3DRepeaterSegment& repeater = scene_data.repeaters[repeater_index];
            if (!repeater.edit_id.empty()) {
                scene_repeater_locations[repeater.edit_id] = repeater_index;
            }
            update_scene_repeater_chunk_membership(
                repeater_index, std::nullopt,
                scene_repeater_chunk_range(repeater));
        }
        return true;
    }

    static void append_track_quad(std::vector<GpuVertex>& vertices,
                                  std::vector<unsigned int>& indices,
                                  Vec3 a, Vec3 b, Vec3 side0, Vec3 side1, float half_width) {
        if (vertices.size() > static_cast<size_t>(std::numeric_limits<unsigned int>::max() - 4)) return;
        Vec3 n = {0.0f, 1.0f, 0.0f};
        unsigned int base = static_cast<unsigned int>(vertices.size());
        Vec3 a0 = a - side0 * half_width;
        Vec3 a1 = a + side0 * half_width;
        Vec3 b0 = b - side1 * half_width;
        Vec3 b1 = b + side1 * half_width;
        vertices.push_back({a0.x, a0.y, a0.z, n.x, n.y, n.z, 0.0f, 0.0f});
        vertices.push_back({a1.x, a1.y, a1.z, n.x, n.y, n.z, 1.0f, 0.0f});
        vertices.push_back({b1.x, b1.y, b1.z, n.x, n.y, n.z, 1.0f, 1.0f});
        vertices.push_back({b0.x, b0.y, b0.z, n.x, n.y, n.z, 0.0f, 1.0f});
        indices.insert(indices.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
    }

    static Vec3 vec3_from_dvec3(DVec3 v) {
        return {static_cast<float>(v.x), static_cast<float>(v.y), static_cast<float>(v.z)};
    }

    static void track_marker_frame(const Canvas3DTrackPoint& point, Vec3& right, Vec3& up) {
        DVec3 right_d = right_from_theta_d(point.theta);
        DVec3 forward_d = forward_from_theta_d(point.theta);
        DVec3 up_d = cross(right_d, forward_d);
        apply_track_cant(right_d, up_d, forward_d, point.cant_angle);
        right = vec3_from_dvec3(normalize(right_d));
        up = vec3_from_dvec3(normalize(up_d));
    }

    static void append_track_segment(std::vector<GpuVertex>& vertices,
                                     std::vector<unsigned int>& indices,
                                     DVec3 origin,
                                     const Canvas3DTrackPoint& p0,
                                     const Canvas3DTrackPoint& p1) {
        constexpr float marker_lift = 0.035f;
        constexpr float marker_half_width = k_scene_track_marker_width * 0.5f;
        Vec3 right0;
        Vec3 right1;
        Vec3 up0;
        Vec3 up1;
        track_marker_frame(p0, right0, up0);
        track_marker_frame(p1, right1, up1);
        Vec3 center0{static_cast<float>(p0.x - origin.x),
                     static_cast<float>(p0.y - origin.y),
                     static_cast<float>(p0.z - origin.z)};
        Vec3 center1{static_cast<float>(p1.x - origin.x),
                     static_cast<float>(p1.y - origin.y),
                     static_cast<float>(p1.z - origin.z)};
        center0 = center0 + up0 * marker_lift;
        center1 = center1 + up1 * marker_lift;
        append_track_quad(vertices, indices, center0, center1, right0, right1, marker_half_width);
    }

    bool upload_track_chunk(SceneTrackChunkGpu& chunk,
                            const std::vector<GpuVertex>& vertices,
                            const std::vector<unsigned int>& indices,
                            std::string& error) {
        if (vertices.empty() || indices.empty()) return true;
        if (vertices.size() > static_cast<size_t>(std::numeric_limits<UINT>::max() / sizeof(GpuVertex)) ||
            indices.size() > static_cast<size_t>(std::numeric_limits<UINT>::max() / sizeof(unsigned int))) {
            error = "track chunk is too large for a Direct3D 11 buffer";
            return false;
        }
        D3D11_BUFFER_DESC vb_desc = {};
        vb_desc.ByteWidth = static_cast<UINT>(vertices.size() * sizeof(GpuVertex));
        vb_desc.Usage = D3D11_USAGE_DEFAULT;
        vb_desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        D3D11_SUBRESOURCE_DATA vb_data = {};
        vb_data.pSysMem = vertices.data();
        HRESULT hr = device->CreateBuffer(&vb_desc, &vb_data, &chunk.vertex_buffer);
        if (FAILED(hr)) {
            error = hresult_text("CreateBuffer(track vertex)", hr);
            return false;
        }

        D3D11_BUFFER_DESC ib_desc = {};
        ib_desc.ByteWidth = static_cast<UINT>(indices.size() * sizeof(unsigned int));
        ib_desc.Usage = D3D11_USAGE_DEFAULT;
        ib_desc.BindFlags = D3D11_BIND_INDEX_BUFFER;
        D3D11_SUBRESOURCE_DATA ib_data = {};
        ib_data.pSysMem = indices.data();
        hr = device->CreateBuffer(&ib_desc, &ib_data, &chunk.index_buffer);
        if (FAILED(hr)) {
            error = hresult_text("CreateBuffer(track index)", hr);
            return false;
        }

        chunk.index_count = static_cast<UINT>(indices.size());
        return true;
    }

    bool build_scene_track_chunks(std::string& error) {
        scene_track_chunks.clear();
        scene_track_chunks.resize(scene_chunks.size());
        for (size_t chunk_index = 0; chunk_index < scene_chunks.size(); ++chunk_index) {
            const SceneChunk& scene_chunk = scene_chunks[chunk_index];
            SceneTrackChunkGpu& gpu_chunk = scene_track_chunks[chunk_index];
            gpu_chunk.d_min = scene_chunk.d_min;
            gpu_chunk.d_max = scene_chunk.d_max;
            gpu_chunk.origin = scene_chunk.origin;
            std::vector<GpuVertex> vertices;
            std::vector<unsigned int> indices;

            for (const Canvas3DTrackPath& track : scene_data.tracks) {
                if (!track.visible || track.points.size() < 2) continue;
                size_t part_start = indices.size();
                UINT material_index = static_cast<UINT>(gpu_chunk.materials.size());
                GpuMaterial material;
                material.diffuse[0] = clamp_color_component(track.color.x);
                material.diffuse[1] = clamp_color_component(track.color.y);
                material.diffuse[2] = clamp_color_component(track.color.z);
                material.diffuse[3] = k_scene_track_marker_alpha;
                gpu_chunk.materials.push_back(material);

                for (size_t i = 1; i < track.points.size(); ++i) {
                    const Canvas3DTrackPoint& a = track.points[i - 1];
                    const Canvas3DTrackPoint& b = track.points[i];
                    if (b.distance < scene_chunk.d_min || a.distance > scene_chunk.d_max) continue;
                    append_track_segment(vertices, indices, gpu_chunk.origin, a, b);
                }

                size_t part_count = indices.size() - part_start;
                if (part_count > 0) {
                    gpu_chunk.parts.push_back({static_cast<UINT>(part_start), static_cast<UINT>(part_count), material_index});
                } else {
                    gpu_chunk.materials.pop_back();
                }
            }

            if (!upload_track_chunk(gpu_chunk, vertices, indices, error)) return false;
        }
        return true;
    }

    static unsigned int append_scene_marker_vertex(
        std::vector<SceneMarkerVertex>& vertices,
        DVec3 origin,
        DVec3 center,
        DVec3 right,
        DVec3 up,
        DVec3 forward,
        float local_x,
        float local_y,
        float face_sign,
        float u,
        float v,
        ImU32 color,
        bool textured) {
        DVec3 position = center +
            right * static_cast<double>(local_x) +
            up * static_cast<double>(local_y) +
            forward * static_cast<double>(k_scene_marker_face_offset * face_sign);
        const unsigned int index = static_cast<unsigned int>(vertices.size());
        vertices.push_back({
            static_cast<float>(position.x - origin.x),
            static_cast<float>(position.y - origin.y),
            static_cast<float>(position.z - origin.z),
            u,
            v,
            static_cast<std::uint32_t>(color),
            textured ? 1.0f : 0.0f
        });
        return index;
    }

    static void append_scene_marker_quad(
        std::vector<SceneMarkerVertex>& vertices,
        std::vector<unsigned int>& indices,
        DVec3 origin,
        DVec3 center,
        DVec3 right,
        DVec3 up,
        DVec3 forward,
        float x0,
        float y0,
        float x1,
        float y1,
        float face_sign,
        ImU32 color,
        bool textured = false,
        ImVec2 uv0 = ImVec2(0.0f, 0.0f),
        ImVec2 uv1 = ImVec2(0.0f, 0.0f)) {
        if (vertices.size() >
            static_cast<size_t>(std::numeric_limits<unsigned int>::max() - 4)) {
            return;
        }
        const unsigned int a = append_scene_marker_vertex(
            vertices, origin, center, right, up, forward,
            x0, y0, face_sign, uv0.x, uv0.y, color, textured);
        const unsigned int b = append_scene_marker_vertex(
            vertices, origin, center, right, up, forward,
            x1, y0, face_sign, uv1.x, uv0.y, color, textured);
        const unsigned int c = append_scene_marker_vertex(
            vertices, origin, center, right, up, forward,
            x1, y1, face_sign, uv1.x, uv1.y, color, textured);
        const unsigned int d = append_scene_marker_vertex(
            vertices, origin, center, right, up, forward,
            x0, y1, face_sign, uv0.x, uv1.y, color, textured);
        indices.insert(indices.end(), {a, c, b, a, d, c});
    }

    static void append_scene_marker_triangle(
        std::vector<SceneMarkerVertex>& vertices,
        std::vector<unsigned int>& indices,
        DVec3 origin,
        DVec3 center,
        DVec3 right,
        DVec3 up,
        DVec3 forward,
        ImVec2 p0,
        ImVec2 p1,
        ImVec2 p2,
        float face_sign,
        ImU32 color) {
        if (vertices.size() >
            static_cast<size_t>(std::numeric_limits<unsigned int>::max() - 3)) {
            return;
        }
        const float signed_area =
            (p1.x - p0.x) * (p2.y - p0.y) -
            (p1.y - p0.y) * (p2.x - p0.x);
        if (signed_area < 0.0f) std::swap(p1, p2);
        const unsigned int a = append_scene_marker_vertex(
            vertices, origin, center, right, up, forward,
            p0.x, p0.y, face_sign, 0.0f, 0.0f, color, false);
        const unsigned int b = append_scene_marker_vertex(
            vertices, origin, center, right, up, forward,
            p1.x, p1.y, face_sign, 0.0f, 0.0f, color, false);
        const unsigned int c = append_scene_marker_vertex(
            vertices, origin, center, right, up, forward,
            p2.x, p2.y, face_sign, 0.0f, 0.0f, color, false);
        indices.insert(indices.end(), {a, b, c});
    }

    static ImVec2 scene_marker_icon_point(ImVec2 point) {
        return ImVec2(
            point.x * k_scene_marker_icon_half_extent,
            0.72f - point.y * k_scene_marker_icon_half_extent);
    }

    static void append_scene_marker_line(
        std::vector<SceneMarkerVertex>& vertices,
        std::vector<unsigned int>& indices,
        DVec3 origin,
        DVec3 center,
        DVec3 right,
        DVec3 up,
        DVec3 forward,
        ImVec2 a,
        ImVec2 b,
        float thickness,
        float face_sign,
        ImU32 color) {
        const float dx = b.x - a.x;
        const float dy = b.y - a.y;
        const float length = std::sqrt(dx * dx + dy * dy);
        if (length <= 1e-6f) return;
        const float half = thickness * 0.5f;
        const ImVec2 side(-dy / length * half, dx / length * half);
        append_scene_marker_triangle(
            vertices, indices, origin, center, right, up, forward,
            ImVec2(a.x - side.x, a.y - side.y),
            ImVec2(a.x + side.x, a.y + side.y),
            ImVec2(b.x + side.x, b.y + side.y),
            face_sign, color);
        append_scene_marker_triangle(
            vertices, indices, origin, center, right, up, forward,
            ImVec2(a.x - side.x, a.y - side.y),
            ImVec2(b.x + side.x, b.y + side.y),
            ImVec2(b.x - side.x, b.y - side.y),
            face_sign, color);
    }

    static void append_scene_marker_glyph(
        std::vector<SceneMarkerVertex>& vertices,
        std::vector<unsigned int>& indices,
        DVec3 origin,
        DVec3 center,
        DVec3 right,
        DVec3 up,
        DVec3 forward,
        ImFontBaked& baked,
        ImWchar codepoint,
        float cursor_x,
        float top_y,
        float scale,
        float face_sign,
        ImU32 color) {
        const ImFontGlyph* glyph = baked.FindGlyph(codepoint);
        if (!glyph || !glyph->Visible) return;
        append_scene_marker_quad(
            vertices, indices, origin, center, right, up, forward,
            cursor_x + glyph->X0 * scale,
            top_y - glyph->Y0 * scale,
            cursor_x + glyph->X1 * scale,
            top_y - glyph->Y1 * scale,
            face_sign, color, true,
            ImVec2(glyph->U0, glyph->V0),
            ImVec2(glyph->U1, glyph->V1));
    }

    static unsigned int decode_scene_marker_utf8(const char*& cursor,
                                                  const char* end) {
        if (cursor >= end) return 0;
        const unsigned char c0 = static_cast<unsigned char>(*cursor++);
        if (c0 < 0x80) return c0;
        int continuation_count = 0;
        unsigned int codepoint = 0;
        if ((c0 & 0xe0u) == 0xc0u) {
            continuation_count = 1;
            codepoint = c0 & 0x1fu;
        } else if ((c0 & 0xf0u) == 0xe0u) {
            continuation_count = 2;
            codepoint = c0 & 0x0fu;
        } else if ((c0 & 0xf8u) == 0xf0u) {
            continuation_count = 3;
            codepoint = c0 & 0x07u;
        } else {
            return 0xfffdu;
        }
        for (int i = 0; i < continuation_count; ++i) {
            if (cursor >= end) return 0xfffdu;
            const unsigned char continuation =
                static_cast<unsigned char>(*cursor);
            if ((continuation & 0xc0u) != 0x80u) return 0xfffdu;
            ++cursor;
            codepoint = (codepoint << 6) | (continuation & 0x3fu);
        }
        return codepoint <= IM_UNICODE_CODEPOINT_MAX ? codepoint : 0xfffdu;
    }

    static void append_scene_marker_text(
        std::vector<SceneMarkerVertex>& vertices,
        std::vector<unsigned int>& indices,
        DVec3 origin,
        DVec3 center,
        DVec3 right,
        DVec3 up,
        DVec3 forward,
        ImFont& font,
        ImFontBaked& baked,
        float font_size,
        const std::string& text,
        float face_sign,
        ImU32 color,
        float text_height,
        float center_y,
        float max_width) {
        if (text.empty() || font_size <= 0.0f || baked.Size <= 0.0f) return;
        const char* text_begin = text.c_str();
        const char* text_end = text_begin + text.size();
        float max_line_width = 0.0f;
        size_t line_count = 0;
        for (const char* line_begin = text_begin;;) {
            const char* line_end = std::find(line_begin, text_end, '\n');
            const char* display_end = line_end;
            if (display_end > line_begin && *(display_end - 1) == '\r') {
                --display_end;
            }
            const ImVec2 measured = font.CalcTextSizeA(
                font_size, std::numeric_limits<float>::max(), 0.0f,
                line_begin, display_end);
            max_line_width = std::max(max_line_width, measured.x);
            ++line_count;
            if (line_end == text_end) break;
            line_begin = line_end + 1;
        }
        float scale = text_height / baked.Size;
        if (max_line_width > 0.0f) {
            scale = std::min(scale, max_width / max_line_width);
        }
        const float line_height = font_size * scale;
        float top_y = center_y +
            line_height * static_cast<float>(line_count) * 0.5f;
        for (const char* line_begin = text_begin;;) {
            const char* line_end = std::find(line_begin, text_end, '\n');
            const char* display_end = line_end;
            if (display_end > line_begin && *(display_end - 1) == '\r') {
                --display_end;
            }
            const ImVec2 measured = font.CalcTextSizeA(
                font_size, std::numeric_limits<float>::max(), 0.0f,
                line_begin, display_end);
            float cursor_x = measured.x * scale * -0.5f;
            const char* cursor = line_begin;
            while (cursor < display_end) {
                const unsigned int codepoint = decode_scene_marker_utf8(cursor, display_end);
                const ImFontGlyph* glyph = baked.FindGlyph(
                    static_cast<ImWchar>(codepoint));
                if (!glyph) continue;
                const float advance = glyph->AdvanceX * scale;
                if (glyph->Visible) {
                    append_scene_marker_glyph(
                        vertices, indices, origin, center, right, up, forward,
                        baked, static_cast<ImWchar>(codepoint),
                        cursor_x, top_y, scale, face_sign, color);
                }
                cursor_x += advance;
            }
            if (line_end == text_end) break;
            top_y -= line_height;
            line_begin = line_end + 1;
        }
    }

    static void append_scene_marker_icon_glyph(
        std::vector<SceneMarkerVertex>& vertices,
        std::vector<unsigned int>& indices,
        DVec3 origin,
        DVec3 center,
        DVec3 right,
        DVec3 up,
        DVec3 forward,
        ImFontBaked& baked,
        char glyph,
        float face_sign,
        ImU32 color,
        float offset_x,
        float offset_y) {
        const ImFontGlyph* font_glyph =
            baked.FindGlyph(static_cast<ImWchar>(glyph));
        if (!font_glyph || baked.Size <= 0.0f) return;
        constexpr float glyph_height = 0.28f;
        const float scale = glyph_height / baked.Size;
        const float cursor_x =
            -font_glyph->AdvanceX * scale * 0.5f + offset_x;
        const float top_y = 0.72f + glyph_height * 0.5f + offset_y;
        append_scene_marker_glyph(
            vertices, indices, origin, center, right, up, forward,
            baked, static_cast<ImWchar>(glyph),
            cursor_x, top_y, scale, face_sign, color);
    }

    static void append_scene_marker_icon(
        std::vector<SceneMarkerVertex>& vertices,
        std::vector<unsigned int>& indices,
        DVec3 origin,
        DVec3 center,
        DVec3 right,
        DVec3 up,
        DVec3 forward,
        ImFontBaked& baked,
        MapMarkerVisualKind kind,
        MapMarkerIconVariant variant,
        float face_sign,
        const ImVec4* theme_override) {
        const MapMarkerIconRecipe recipe =
            map_marker_icon_recipe(kind, variant);
        for (size_t primitive_index = 0;
             primitive_index < recipe.primitive_count;
             ++primitive_index) {
            const MapMarkerIconPrimitive& primitive =
                recipe.primitives[primitive_index];
            const ImU32 color = ImGui::ColorConvertFloat4ToU32(
                map_marker_role_color(kind, primitive.color, theme_override));
            switch (primitive.kind) {
                case MapMarkerPrimitiveKind::Polyline: {
                    if (primitive.point_count < 2) break;
                    const float thickness =
                        primitive.thickness * k_scene_marker_icon_half_extent;
                    const size_t segment_count = primitive.closed
                        ? primitive.point_count
                        : primitive.point_count - 1;
                    for (size_t segment = 0; segment < segment_count; ++segment) {
                        const size_t next =
                            (segment + 1) % primitive.point_count;
                        append_scene_marker_line(
                            vertices, indices, origin, center, right, up, forward,
                            scene_marker_icon_point(primitive.points[segment]),
                            scene_marker_icon_point(primitive.points[next]),
                            thickness, face_sign, color);
                    }
                    break;
                }
                case MapMarkerPrimitiveKind::FilledPolygon: {
                    if (primitive.point_count < 3) break;
                    const ImVec2 first =
                        scene_marker_icon_point(primitive.points[0]);
                    for (size_t point = 2;
                         point < primitive.point_count;
                         ++point) {
                        append_scene_marker_triangle(
                            vertices, indices, origin, center, right, up, forward,
                            first,
                            scene_marker_icon_point(primitive.points[point - 1]),
                            scene_marker_icon_point(primitive.points[point]),
                            face_sign, color);
                    }
                    break;
                }
                case MapMarkerPrimitiveKind::Circle:
                case MapMarkerPrimitiveKind::FilledCircle: {
                    const ImVec2 circle_center =
                        scene_marker_icon_point(primitive.points[0]);
                    const float radius =
                        primitive.radius * k_scene_marker_icon_half_extent;
                    if (primitive.kind == MapMarkerPrimitiveKind::FilledCircle) {
                        for (int segment = 0;
                             segment < k_scene_marker_circle_segments;
                             ++segment) {
                            const float a0 =
                                static_cast<float>(segment) * 6.28318530718f /
                                static_cast<float>(k_scene_marker_circle_segments);
                            const float a1 =
                                static_cast<float>(segment + 1) * 6.28318530718f /
                                static_cast<float>(k_scene_marker_circle_segments);
                            append_scene_marker_triangle(
                                vertices, indices, origin, center, right, up, forward,
                                circle_center,
                                ImVec2(circle_center.x + std::cos(a0) * radius,
                                       circle_center.y + std::sin(a0) * radius),
                                ImVec2(circle_center.x + std::cos(a1) * radius,
                                       circle_center.y + std::sin(a1) * radius),
                                face_sign, color);
                        }
                    } else {
                        const float thickness =
                            primitive.thickness *
                            k_scene_marker_icon_half_extent;
                        const float outer_radius = radius + thickness * 0.5f;
                        const float inner_radius = std::max(
                            0.0f, radius - thickness * 0.5f);
                        for (int segment = 0;
                             segment < k_scene_marker_circle_segments;
                             ++segment) {
                            const float a0 =
                                static_cast<float>(segment) * 6.28318530718f /
                                static_cast<float>(k_scene_marker_circle_segments);
                            const float a1 =
                                static_cast<float>(segment + 1) * 6.28318530718f /
                                static_cast<float>(k_scene_marker_circle_segments);
                            const ImVec2 outer0(
                                circle_center.x + std::cos(a0) * outer_radius,
                                circle_center.y + std::sin(a0) * outer_radius);
                            const ImVec2 outer1(
                                circle_center.x + std::cos(a1) * outer_radius,
                                circle_center.y + std::sin(a1) * outer_radius);
                            const ImVec2 inner0(
                                circle_center.x + std::cos(a0) * inner_radius,
                                circle_center.y + std::sin(a0) * inner_radius);
                            const ImVec2 inner1(
                                circle_center.x + std::cos(a1) * inner_radius,
                                circle_center.y + std::sin(a1) * inner_radius);
                            append_scene_marker_triangle(
                                vertices, indices, origin, center, right, up, forward,
                                outer0, inner0, inner1, face_sign, color);
                            append_scene_marker_triangle(
                                vertices, indices, origin, center, right, up, forward,
                                outer0, inner1, outer1, face_sign, color);
                        }
                    }
                    break;
                }
                case MapMarkerPrimitiveKind::Glyph: {
                    const bool outline =
                        primitive.color == MapMarkerColorRole::Outline ||
                        primitive.color == MapMarkerColorRole::Shadow ||
                        primitive.color == MapMarkerColorRole::Black;
                    if (outline) {
                        for (const ImVec2 offset : {
                                 ImVec2(-k_scene_marker_outline_width, 0.0f),
                                 ImVec2(k_scene_marker_outline_width, 0.0f),
                                 ImVec2(0.0f, -k_scene_marker_outline_width),
                                 ImVec2(0.0f, k_scene_marker_outline_width)}) {
                            append_scene_marker_icon_glyph(
                                vertices, indices, origin, center, right, up, forward,
                                baked, primitive.glyph, face_sign, color,
                                offset.x, offset.y);
                        }
                    } else {
                        append_scene_marker_icon_glyph(
                            vertices, indices, origin, center, right, up, forward,
                            baked, primitive.glyph, face_sign, color, 0.0f, 0.0f);
                    }
                    break;
                }
            }
        }
    }

    static void scene_marker_frame(const Canvas3DTrackPoint& point,
                                   DVec3& right,
                                   DVec3& up,
                                   DVec3& forward) {
        scene_track_surface_frame(point, right, up, forward);
    }

    static SceneMarkerGeometrySpan append_scene_marker_geometry(
        const Canvas3DSceneMarker& marker,
        size_t marker_index,
        DVec3 origin,
        float lateral_offset,
        ImFont& font,
        ImFontBaked& baked,
        float font_size,
        std::vector<SceneMarkerVertex>& vertices,
        std::vector<unsigned int>& indices,
        std::vector<SceneMarkerIndexRange>& ranges) {
        SceneMarkerGeometrySpan span;
        span.vertex_first = vertices.size();
        span.range_first = ranges.size();
        if (marker_index >
            static_cast<size_t>(std::numeric_limits<std::uint32_t>::max())) {
            return span;
        }

        const Canvas3DTrackPoint& point = marker.track_point;
        DVec3 right;
        DVec3 up;
        DVec3 forward;
        scene_marker_frame(point, right, up, forward);
        const bool sound3d = marker.kind == MapMarkerVisualKind::MapSound3D;
        const DVec3 center =
            DVec3{point.x, point.y, point.z} +
            right * static_cast<double>(lateral_offset) +
            up * static_cast<double>(sound3d
                ? k_scene_marker_sound3d_tag_tip_height : 0.0f);
        const float content_vertical_offset =
            marker.kind == MapMarkerVisualKind::Irregularity
            ? k_scene_marker_irregularity_content_offset
            : 0.0f;
        const DVec3 content_center =
            center + up * static_cast<double>(content_vertical_offset);
        const bool is_other_track_change =
            marker.kind == MapMarkerVisualKind::OtherTrackChange;

        const std::uint32_t marker_first =
            static_cast<std::uint32_t>(indices.size());
        ImVec4 board_color = marker.has_theme_color
            ? marker.theme_color : map_marker_theme_color(marker.kind);
        board_color.w = k_scene_marker_board_alpha;
        const ImU32 board_color_u32 =
            ImGui::ColorConvertFloat4ToU32(board_color);
        constexpr float face_sign = -1.0f;
        append_scene_marker_quad(
            vertices, indices, origin, center,
            right, up, forward,
            k_scene_marker_board_width * -0.5f, 1.0f,
            k_scene_marker_board_width * 0.5f, 0.0f,
            face_sign, board_color_u32);
        if (sound3d) {
            append_scene_marker_triangle(
                vertices, indices, origin, center,
                right, up, forward,
                ImVec2(k_scene_marker_board_width * -0.5f, 0.0f),
                ImVec2(k_scene_marker_board_width * 0.5f, 0.0f),
                ImVec2(0.0f, -k_scene_marker_sound3d_tag_tip_height),
                face_sign, board_color_u32);
        }
        if (!is_other_track_change) {
            append_scene_marker_icon(
                vertices, indices, origin, content_center,
                right, up, forward, baked, marker.kind,
                marker.icon_variant, face_sign,
                marker.has_theme_color ? &marker.theme_color : nullptr);
        }
        const bool label_in_icon =
            marker.icon_variant ==
            MapMarkerIconVariant::SpeedLimitBegin;
        const float label_center_y = is_other_track_change
            ? k_scene_marker_other_track_label_center_y
            : k_scene_marker_label_center_y;
        if (label_in_icon && !marker.label.empty()) {
            const ImU32 text_color =
                ImGui::ColorConvertFloat4ToU32(
                    map_marker_role_color(
                        marker.kind, MapMarkerColorRole::Black));
            append_scene_marker_text(
                vertices, indices, origin, content_center,
                right, up, forward, font, baked, font_size,
                marker.label, face_sign, text_color,
                0.22f, 0.72f, 0.25f);
        }
        ranges.push_back({
            marker.kind,
            marker_index,
            marker_first,
            static_cast<std::uint32_t>(
                indices.size() - marker_first),
            0,
            false,
            false,
            center,
            right,
            up
        });

        if (!label_in_icon && !marker.label.empty()) {
            const std::uint32_t label_first =
                static_cast<std::uint32_t>(indices.size());
            const ImU32 outline_color =
                ImGui::ColorConvertFloat4ToU32(
                    map_marker_role_color(
                        marker.kind, MapMarkerColorRole::Shadow));
            const ImU32 text_color =
                ImGui::ColorConvertFloat4ToU32(
                    marker.has_theme_color ? marker.theme_color
                                           : map_marker_theme_color(marker.kind));
            for (const ImVec2 offset : {
                     ImVec2(-k_scene_marker_outline_width, 0.0f),
                     ImVec2(k_scene_marker_outline_width, 0.0f),
                     ImVec2(0.0f, -k_scene_marker_outline_width),
                     ImVec2(0.0f, k_scene_marker_outline_width)}) {
                const DVec3 shifted_center =
                    content_center +
                    right * static_cast<double>(offset.x) +
                    up * static_cast<double>(offset.y);
                append_scene_marker_text(
                    vertices, indices, origin,
                    shifted_center, right, up, forward,
                    font, baked, font_size, marker.label,
                    face_sign, outline_color,
                    k_scene_marker_label_height,
                    label_center_y,
                    k_scene_marker_label_max_width);
            }
            append_scene_marker_text(
                vertices, indices, origin, content_center,
                right, up, forward, font, baked, font_size,
                marker.label, face_sign, text_color,
                k_scene_marker_label_height,
                label_center_y,
                k_scene_marker_label_max_width);
            ranges.push_back({
                marker.kind,
                marker_index,
                label_first,
                static_cast<std::uint32_t>(
                    indices.size() - label_first),
                0,
                false,
                true,
                content_center,
                right,
                up
            });
        }

        const std::uint32_t marker_index_u32 =
            static_cast<std::uint32_t>(marker_index);
        for (size_t vertex_index = span.vertex_first;
             vertex_index < vertices.size(); ++vertex_index) {
            vertices[vertex_index].marker_index = marker_index_u32;
        }
        span.vertex_count = vertices.size() - span.vertex_first;
        span.range_count = ranges.size() - span.range_first;
        return span;
    }

    void rebuild_scene_mileage_pick_cache() {
        scene_mileage_pick_points.clear();
        const Canvas3DTrackPath* path = own_track_path();
        if (!path || path->points.size() < 2) return;

        scene_mileage_pick_points.reserve(path->points.size());
        for (const Canvas3DTrackPoint& point : path->points) {
            if (!std::isfinite(point.distance) || !std::isfinite(point.x) ||
                !std::isfinite(point.y) || !std::isfinite(point.z) ||
                !std::isfinite(point.theta)) {
                scene_mileage_pick_points.clear();
                return;
            }
            DVec3 right;
            DVec3 up;
            DVec3 forward;
            scene_track_surface_frame(point, right, up, forward);
            const DVec3 center{point.x, point.y, point.z};
            scene_mileage_pick_points.push_back(SceneMileagePickPoint{
                point.distance,
                center - right * k_scene_mileage_pick_half_width,
                center + right * k_scene_mileage_pick_half_width
            });
        }
    }

    bool rebuild_scene_marker_visible_indices(std::string& error) {
        error.clear();
        std::map<std::string, bool> track_visible;
        for (const Canvas3DTrackPath& path : scene_data.tracks) {
            track_visible[normalize_track_lookup_key(path.key)] = path.visible;
        }
        for (SceneMarkerChunkGpu& chunk : scene_marker_chunks) {
            chunk.visible_index_count = 0;
            chunk.visible_pick_index_count = 0;
            if (!chunk.index_buffer || !chunk.pick_index_buffer ||
                chunk.source_indices.empty()) continue;

            D3D11_MAPPED_SUBRESOURCE display_mapped = {};
            HRESULT hr = context->Map(
                chunk.index_buffer, 0, D3D11_MAP_WRITE_DISCARD, 0,
                &display_mapped);
            if (FAILED(hr)) {
                error = hresult_text("Map(scene marker index)", hr);
                return false;
            }
            D3D11_MAPPED_SUBRESOURCE pick_mapped = {};
            hr = context->Map(
                chunk.pick_index_buffer, 0, D3D11_MAP_WRITE_DISCARD, 0,
                &pick_mapped);
            if (FAILED(hr)) {
                context->Unmap(chunk.index_buffer, 0);
                error = hresult_text("Map(scene marker pick index)", hr);
                return false;
            }
            unsigned int* display_destination =
                static_cast<unsigned int*>(display_mapped.pData);
            unsigned int* pick_destination =
                static_cast<unsigned int*>(pick_mapped.pData);
            for (SceneMarkerIndexRange& range : chunk.ranges) {
                bool visible = range.label
                    ? scene_marker_visibility.label_visible(range.kind)
                    : scene_marker_visibility.marker_visible(range.kind);
                if (visible && range.marker_index < scene_data.markers.size()) {
                    const std::string& marker_track_key =
                        scene_data.markers[range.marker_index].track_key;
                    if (!marker_track_key.empty()) {
                        const auto found = track_visible.find(
                            normalize_track_lookup_key(marker_track_key));
                        visible = found != track_visible.end() && found->second;
                    }
                }
                range.visible = visible && range.count > 0;
                range.visible_first = 0;
                if (!range.visible) continue;
                const size_t end =
                    static_cast<size_t>(range.first) + range.count;
                if (end > chunk.source_indices.size()) {
                    range.visible = false;
                    continue;
                }
                std::copy_n(
                    chunk.source_indices.data() + range.first,
                    range.count,
                    display_destination + chunk.visible_index_count);
                range.visible_first = chunk.visible_index_count;
                chunk.visible_index_count += range.count;
                if (!range.label) {
                    std::copy_n(
                        chunk.source_indices.data() + range.first,
                        range.count,
                        pick_destination + chunk.visible_pick_index_count);
                    chunk.visible_pick_index_count += range.count;
                }
            }
            context->Unmap(chunk.index_buffer, 0);
            context->Unmap(chunk.pick_index_buffer, 0);
        }
        return true;
    }

    bool build_scene_marker_chunks(std::string& error) {
        error.clear();
        release_scene_marker_chunks();
        scene_marker_chunks.resize(scene_chunks.size());
        for (size_t marker_index = 0;
             marker_index < scene_data.markers.size();
             ++marker_index) {
            const Canvas3DSceneMarker& marker = scene_data.markers[marker_index];
            if (marker.kind == MapMarkerVisualKind::MapSound3D &&
                !marker.edit_id.empty()) {
                scene_sound3d_marker_indices[marker.edit_id] = marker_index;
            }
            if (!scene_marker_list_kind_is_navigable(marker.list_kind) || !marker.row_index) {
                continue;
            }
            std::vector<size_t>& target_indices =
                scene_marker_target_indices[scene_marker_list_kind_slot(marker.list_kind)];
            if (*marker.row_index >= target_indices.size()) {
                target_indices.resize(*marker.row_index + 1, k_scene_marker_target_missing);
            }
            target_indices[*marker.row_index] = marker_index;
        }
        if (scene_chunks.empty() || scene_data.markers.empty()) return true;
        scene_marker_locations.resize(scene_data.markers.size());

        ImFont* font = ImGui::GetFont();
        const float font_size = ImGui::GetFontSize();
        ImFontAtlas* atlas = ImGui::GetIO().Fonts;
        if (!font || !atlas || font_size <= 0.0f) {
            error = "ImGui font atlas is not available for 3D scene markers";
            return false;
        }
        ImFontBaked* baked = font->GetFontBaked(font_size);
        if (!baked) {
            error = "ImGui font glyphs are not available for 3D scene markers";
            return false;
        }

        // Use the complete semantic marker set so visibility-only updates keep
        // their dynamic-index-buffer fast path and do not move nearby signs.
        std::vector<float> marker_lateral_offsets(
            scene_data.markers.size(), 0.0f);
        for (size_t group_begin = 0;
             group_begin < scene_data.markers.size();) {
            size_t group_end = group_begin + 1;
            const double group_distance =
                scene_data.markers[group_begin].track_point.distance;
            const std::string group_track_key = normalize_track_lookup_key(
                scene_data.markers[group_begin].track_key);
            while (group_end < scene_data.markers.size() &&
                   scene_data.markers[group_end].track_point.distance ==
                       group_distance &&
                   normalize_track_lookup_key(
                       scene_data.markers[group_end].track_key) ==
                       group_track_key) {
                ++group_end;
            }
            std::vector<size_t> offset_markers;
            offset_markers.reserve(group_end - group_begin);
            for (size_t i = group_begin; i < group_end; ++i) {
                if (scene_data.markers[i].kind != MapMarkerVisualKind::MapSound3D) {
                    offset_markers.push_back(i);
                }
            }
            const size_t group_size = offset_markers.size();
            if (group_size == 0) {
                group_begin = group_end;
                continue;
            }
            const float step =
                k_scene_marker_board_width + k_scene_marker_lateral_gap;
            const float first_offset =
                -0.5f * static_cast<float>(group_size - 1) * step;
            for (size_t i = 0; i < group_size; ++i) {
                marker_lateral_offsets[offset_markers[i]] =
                    first_offset + static_cast<float>(i) * step;
            }
            group_begin = group_end;
        }

        std::vector<std::vector<size_t>> marker_indices(scene_chunks.size());
        for (size_t marker_index = 0;
             marker_index < scene_data.markers.size();
             ++marker_index) {
            const double distance =
                scene_data.markers[marker_index].track_point.distance;
            marker_indices[scene_chunk_index_for_distance(distance)].push_back(marker_index);
        }

        for (size_t chunk_index = 0;
             chunk_index < scene_chunks.size();
             ++chunk_index) {
            const SceneChunk& scene_chunk = scene_chunks[chunk_index];
            SceneMarkerChunkGpu& gpu_chunk =
                scene_marker_chunks[chunk_index];
            gpu_chunk.origin = scene_chunk.origin;
            gpu_chunk.d_min = scene_chunk.d_min;
            gpu_chunk.d_max = scene_chunk.d_max;
            std::vector<SceneMarkerVertex> vertices;
            std::vector<unsigned int>& indices = gpu_chunk.source_indices;

            for (const size_t marker_index : marker_indices[chunk_index]) {
                const Canvas3DSceneMarker& marker =
                    scene_data.markers[marker_index];
                if (marker_index >
                    static_cast<size_t>(std::numeric_limits<std::uint32_t>::max())) {
                    error = "too many scene markers for a Direct3D 11 vertex attribute";
                    release_scene_marker_chunks();
                    return false;
                }
                const SceneMarkerGeometrySpan span = append_scene_marker_geometry(
                    marker, marker_index, gpu_chunk.origin,
                    marker_lateral_offsets[marker_index], *font, *baked, font_size,
                    vertices, indices, gpu_chunk.ranges);
                scene_marker_locations[marker_index] = {
                    chunk_index,
                    span.range_first,
                    span.vertex_first,
                    span.vertex_count,
                    span.range_count,
                    true};
            }

            if (vertices.empty() || indices.empty()) continue;
            if (vertices.size() >
                    static_cast<size_t>(
                        std::numeric_limits<UINT>::max() /
                        sizeof(SceneMarkerVertex)) ||
                indices.size() >
                    static_cast<size_t>(
                        std::numeric_limits<UINT>::max() /
                        sizeof(unsigned int))) {
                error = "scene marker chunk is too large for a Direct3D 11 buffer";
                release_scene_marker_chunks();
                return false;
            }

            D3D11_BUFFER_DESC vb_desc = {};
            vb_desc.ByteWidth = static_cast<UINT>(
                vertices.size() * sizeof(SceneMarkerVertex));
            vb_desc.Usage = D3D11_USAGE_DEFAULT;
            vb_desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
            D3D11_SUBRESOURCE_DATA vb_data = {};
            vb_data.pSysMem = vertices.data();
            HRESULT hr = device->CreateBuffer(
                &vb_desc, &vb_data, &gpu_chunk.vertex_buffer);
            if (FAILED(hr)) {
                error = hresult_text("CreateBuffer(scene marker vertex)", hr);
                release_scene_marker_chunks();
                return false;
            }

            D3D11_BUFFER_DESC ib_desc = {};
            ib_desc.ByteWidth = static_cast<UINT>(
                indices.size() * sizeof(unsigned int));
            ib_desc.Usage = D3D11_USAGE_DYNAMIC;
            ib_desc.BindFlags = D3D11_BIND_INDEX_BUFFER;
            ib_desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
            hr = device->CreateBuffer(
                &ib_desc, nullptr, &gpu_chunk.index_buffer);
            if (FAILED(hr)) {
                error = hresult_text("CreateBuffer(scene marker index)", hr);
                release_scene_marker_chunks();
                return false;
            }
            hr = device->CreateBuffer(
                &ib_desc, nullptr, &gpu_chunk.pick_index_buffer);
            if (FAILED(hr)) {
                error = hresult_text("CreateBuffer(scene marker pick index)", hr);
                release_scene_marker_chunks();
                return false;
            }
        }

        scene_marker_font = font;
        scene_marker_font_size = font_size;
        scene_marker_font_texture_id = atlas->TexRef.GetTexID();
        if (atlas->TexData) {
            scene_marker_font_texture_unique_id = atlas->TexData->UniqueID;
            scene_marker_font_texture_width = atlas->TexData->Width;
            scene_marker_font_texture_height = atlas->TexData->Height;
        }
        if (!rebuild_scene_marker_visible_indices(error)) {
            release_scene_marker_chunks();
            return false;
        }
        return true;
    }

    bool set_scene_marker_visibility(
        const Canvas3DSceneMarkerVisibility& visibility,
        std::string& error) {
        error.clear();
        if (scene_marker_visibility == visibility) return true;
        scene_marker_visibility = visibility;
        if (!scene_active || scene_marker_chunks.empty()) return true;
        if (!rebuild_scene_marker_visible_indices(error)) {
            if (!error.empty()) scene_last_error = error;
            return false;
        }
        return true;
    }

    bool ensure_render_target(int width, int height, std::string& error) {
        if (!device || width <= 0 || height <= 0) return false;
        if (render_srv && render_width == width && render_height == height) return true;

        release_render_target();
        render_width = width;
        render_height = height;

        D3D11_TEXTURE2D_DESC tex_desc = {};
        tex_desc.Width = static_cast<UINT>(width);
        tex_desc.Height = static_cast<UINT>(height);
        tex_desc.MipLevels = 1;
        tex_desc.ArraySize = 1;
        tex_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        tex_desc.SampleDesc.Count = 1;
        tex_desc.Usage = D3D11_USAGE_DEFAULT;
        tex_desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        HRESULT hr = device->CreateTexture2D(&tex_desc, nullptr, &render_texture);
        if (FAILED(hr)) {
            error = hresult_text("CreateTexture2D(render target)", hr);
            release_render_target();
            return false;
        }
        hr = device->CreateRenderTargetView(render_texture, nullptr, &render_rtv);
        if (FAILED(hr)) {
            error = hresult_text("CreateRenderTargetView", hr);
            release_render_target();
            return false;
        }
        hr = device->CreateShaderResourceView(render_texture, nullptr, &render_srv);
        if (FAILED(hr)) {
            error = hresult_text("CreateShaderResourceView", hr);
            release_render_target();
            return false;
        }

        D3D11_TEXTURE2D_DESC depth_desc = {};
        depth_desc.Width = static_cast<UINT>(width);
        depth_desc.Height = static_cast<UINT>(height);
        depth_desc.MipLevels = 1;
        depth_desc.ArraySize = 1;
        depth_desc.Format = DXGI_FORMAT_D32_FLOAT;
        depth_desc.SampleDesc.Count = 1;
        depth_desc.Usage = D3D11_USAGE_DEFAULT;
        depth_desc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
        hr = device->CreateTexture2D(&depth_desc, nullptr, &depth_texture);
        if (FAILED(hr)) {
            error = hresult_text("CreateTexture2D(depth)", hr);
            release_render_target();
            return false;
        }
        hr = device->CreateDepthStencilView(depth_texture, nullptr, &depth_dsv);
        if (FAILED(hr)) {
            error = hresult_text("CreateDepthStencilView", hr);
            release_render_target();
            return false;
        }
        return true;
    }

    bool ensure_scene_highlight_mask_target(int width, int height, std::string& error) {
        if (!device || width <= 0 || height <= 0) return false;
        if (scene_highlight_mask_texture && scene_highlight_mask_rtv && scene_highlight_mask_srv &&
            render_width == width && render_height == height) {
            return true;
        }

        release_com(scene_highlight_mask_srv);
        release_com(scene_highlight_mask_rtv);
        release_com(scene_highlight_mask_texture);

        D3D11_TEXTURE2D_DESC tex_desc = {};
        tex_desc.Width = static_cast<UINT>(width);
        tex_desc.Height = static_cast<UINT>(height);
        tex_desc.MipLevels = 1;
        tex_desc.ArraySize = 1;
        tex_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        tex_desc.SampleDesc.Count = 1;
        tex_desc.Usage = D3D11_USAGE_DEFAULT;
        tex_desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        HRESULT hr = device->CreateTexture2D(&tex_desc, nullptr, &scene_highlight_mask_texture);
        if (FAILED(hr)) {
            error = hresult_text("CreateTexture2D(scene highlight mask)", hr);
            release_com(scene_highlight_mask_texture);
            return false;
        }
        hr = device->CreateRenderTargetView(scene_highlight_mask_texture, nullptr, &scene_highlight_mask_rtv);
        if (FAILED(hr)) {
            error = hresult_text("CreateRenderTargetView(scene highlight mask)", hr);
            release_com(scene_highlight_mask_texture);
            return false;
        }
        hr = device->CreateShaderResourceView(scene_highlight_mask_texture, nullptr, &scene_highlight_mask_srv);
        if (FAILED(hr)) {
            error = hresult_text("CreateShaderResourceView(scene highlight mask)", hr);
            release_com(scene_highlight_mask_rtv);
            release_com(scene_highlight_mask_texture);
            return false;
        }
        return true;
    }

    bool ensure_scene_pick_target(int width, int height, std::string& error) {
        if (!device || width <= 0 || height <= 0) return false;
        if (scene_pick_texture && scene_pick_rtv && scene_pick_readback_texture &&
            render_width == width && render_height == height) {
            return true;
        }

        release_com(scene_pick_readback_texture);
        release_com(scene_pick_rtv);
        release_com(scene_pick_texture);

        D3D11_TEXTURE2D_DESC tex_desc = {};
        tex_desc.Width = static_cast<UINT>(width);
        tex_desc.Height = static_cast<UINT>(height);
        tex_desc.MipLevels = 1;
        tex_desc.ArraySize = 1;
        tex_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        tex_desc.SampleDesc.Count = 1;
        tex_desc.Usage = D3D11_USAGE_DEFAULT;
        tex_desc.BindFlags = D3D11_BIND_RENDER_TARGET;
        HRESULT hr = device->CreateTexture2D(&tex_desc, nullptr, &scene_pick_texture);
        if (FAILED(hr)) {
            error = hresult_text("CreateTexture2D(scene pick)", hr);
            release_com(scene_pick_texture);
            return false;
        }
        hr = device->CreateRenderTargetView(scene_pick_texture, nullptr, &scene_pick_rtv);
        if (FAILED(hr)) {
            error = hresult_text("CreateRenderTargetView(scene pick)", hr);
            release_com(scene_pick_texture);
            return false;
        }

        D3D11_TEXTURE2D_DESC read_desc = tex_desc;
        read_desc.Width = 1;
        read_desc.Height = 1;
        read_desc.Usage = D3D11_USAGE_STAGING;
        read_desc.BindFlags = 0;
        read_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        hr = device->CreateTexture2D(&read_desc, nullptr, &scene_pick_readback_texture);
        if (FAILED(hr)) {
            error = hresult_text("CreateTexture2D(scene pick readback)", hr);
            release_com(scene_pick_rtv);
            release_com(scene_pick_texture);
            return false;
        }

        return true;
    }

    Vec3 scene_forward() const {
        float cp = std::cos(scene_camera_pitch);
        Vec3 forward{cp * std::sin(scene_camera_yaw),
                     std::sin(scene_camera_pitch),
                     -cp * std::cos(scene_camera_yaw)};
        return normalize(forward);
    }

    Vec3 scene_right() const {
        return normalize(cross(scene_forward(), {0.0f, 1.0f, 0.0f}));
    }

    const Canvas3DTrackPath* own_track_path() const {
        return scene_own_track_path(scene_data);
    }

    bool sample_track_path(const Canvas3DTrackPath& path, double distance, Canvas3DTrackPoint& out) const {
        std::optional<Canvas3DTrackPoint> sample =
            scene_sample_track_path_points(path, distance);
        if (!sample) return false;
        out = *sample;
        return true;
    }

    bool sample_own_track(double distance, Canvas3DTrackPoint& out) const {
        const Canvas3DTrackPath* path = own_track_path();
        return path && sample_track_path(*path, distance, out);
    }

    const Canvas3DTrackPath* placement_track_path_for_key(const std::string& key) const {
        return scene_placement_track_path_for_key(scene_data, key);
    }

    bool sample_scene_placement_track(const std::string& key, double distance, Canvas3DTrackPoint& out) const {
        const Canvas3DTrackPath* path = placement_track_path_for_key(key);
        return path && sample_track_path(*path, distance, out);
    }

    bool make_track_placement_frame(const std::string& track_key,
                                    double distance,
                                    double x,
                                    double y,
                                    double z,
                                    double rx,
                                    double ry,
                                    double rz,
                                    double tilt,
                                    double span,
                                    StructurePlacementFrame& frame) const {
        Canvas3DTrackPoint point;
        if (!sample_scene_placement_track(track_key, distance, point)) return false;

        const int flags = scene_tilt_flags(tilt);
        const bool follow_gradient = (flags & 1) != 0;
        const bool follow_cant = (flags & 2) != 0;

        DVec3 right = right_from_theta_d(point.theta);
        DVec3 forward = forward_from_theta_d(point.theta);
        DVec3 up = cross(right, forward);
        DVec3 offset_right = right;
        DVec3 offset_up = up;
        if (follow_cant) apply_track_cant(offset_right, offset_up, forward, point.cant_angle);

        DVec3 origin{point.x, point.y, point.z};
        origin = origin + offset_right * x + offset_up * y;

        double effective_span = std::isfinite(span) && span >= 1.0 ? span : 1.0;
        Canvas3DTrackPoint span_point;
        if (sample_scene_placement_track(track_key, distance + effective_span, span_point)) {
            DVec3 span_right = right_from_theta_d(span_point.theta);
            DVec3 span_forward = forward_from_theta_d(span_point.theta);
            DVec3 span_up = cross(span_right, span_forward);
            DVec3 span_offset_right = span_right;
            DVec3 span_offset_up = span_up;
            if (follow_cant) apply_track_cant(span_offset_right, span_offset_up, span_forward, span_point.cant_angle);

            DVec3 next{span_point.x, span_point.y, span_point.z};
            next = next + span_offset_right * x + span_offset_up * y;
            if (!follow_gradient) next.y = origin.y;
            DVec3 span_forward_vec = next - origin;
            if (dot(span_forward_vec, span_forward_vec) > 1e-12) {
                forward = normalize(span_forward_vec);
                right = normalize(cross(forward, {0.0, 1.0, 0.0}));
                up = cross(right, forward);
            }
        }

        if (follow_cant) {
            double cant_angle = point.cant_angle;
            Canvas3DTrackPoint mid_point;
            if (sample_scene_placement_track(track_key, distance + effective_span * 0.5, mid_point)) {
                cant_angle = mid_point.cant_angle;
            }
            apply_track_cant(right, up, forward, cant_angle);
        }

        origin = origin + forward * z;
        frame.origin = origin;
        frame.parameter_axes[0] = normalize(offset_right);
        frame.parameter_axes[1] = normalize(offset_up);
        frame.parameter_axes[2] = normalize(forward);
        apply_euler(right, up, forward, rx, ry, rz);
        frame.model_right = right;
        frame.model_up = up;
        frame.model_forward = forward;
        return true;
    }

    bool make_track_world(const std::string& track_key,
                          double distance,
                          double x,
                          double y,
                          double z,
                          double rx,
                          double ry,
                          double rz,
                          double tilt,
                          double span,
                          double out_world[16]) const {
        StructurePlacementFrame frame;
        if (!make_track_placement_frame(track_key, distance, x, y, z,
                                        rx, ry, rz, tilt, span, frame)) {
            return false;
        }
        store_world(out_world, frame.model_right, frame.model_up,
                    frame.model_forward, frame.origin);
        return true;
    }

    bool make_repeater_instance_world(const Canvas3DRepeaterSegment& repeater,
                                      double distance,
                                      double out_world[16]) const {
        return make_track_world(repeater.track_key, distance,
                                repeater.x, repeater.y, repeater.z,
                                repeater.rx, repeater.ry, repeater.rz,
                                repeater.tilt, repeater.span,
                                out_world);
    }

    void append_visible_model_instance(const std::string& model_path,
                                       const SceneInstanceData& data,
                                       int object_index,
                                       const SceneScreenBounds* bounds,
                                       std::map<std::string, std::vector<SceneInstanceData>>& visible_instances,
                                       std::map<int, std::vector<SceneVisibleInstanceRef>>* object_refs) const {
        auto instance_it = visible_instances.try_emplace(model_path).first;
        std::vector<SceneInstanceData>& instances = instance_it->second;
        const size_t instance_index = instances.size();
        instances.push_back(data);
        if (object_refs && object_index >= 0 && bounds) {
            (*object_refs)[object_index].push_back(SceneVisibleInstanceRef{
                &instance_it->first,
                instance_index,
                bounds->screen_min,
                bounds->screen_max
            });
        }
    }

    static void include_scene_screen_bounds(ImVec2& screen_min,
                                            ImVec2& screen_max,
                                            const SceneVisibleInstanceRef& ref) {
        screen_min.x = std::min(screen_min.x, ref.screen_min.x);
        screen_min.y = std::min(screen_min.y, ref.screen_min.y);
        screen_max.x = std::max(screen_max.x, ref.screen_max.x);
        screen_max.y = std::max(screen_max.y, ref.screen_max.y);
    }

    void append_visible_repeater_instances(const SceneChunk& chunk,
                                           double visible_min,
                                           double visible_max,
                                           DVec3 render_origin,
                                           const Mat4& view_proj,
                                           int width,
                                           int height,
                                           bool can_pick,
                                           std::map<std::string, std::vector<SceneInstanceData>>& visible_instances,
                                           std::map<int, std::vector<SceneVisibleInstanceRef>>* object_refs) const {
        double chunk_max = chunk.d_max;
        if (chunk.d_max < scene_data.max_distance) chunk_max -= k_scene_repeater_distance_epsilon;
        double range_min = std::max(visible_min, chunk.d_min);
        double range_max = std::min(visible_max, chunk_max);
        if (range_max < range_min) return;

        for (size_t repeater_index : chunk.repeater_indices) {
            if (repeater_index >= scene_data.repeaters.size()) continue;
            const Canvas3DRepeaterSegment& repeater = scene_data.repeaters[repeater_index];
            if (repeater.model_paths.empty() || repeater.end_distance < repeater.begin_distance) continue;

            const double begin = std::max(range_min, repeater.begin_distance);
            const double end = std::min(range_max, repeater.end_distance);
            if (end < begin - k_scene_repeater_distance_epsilon) continue;

            auto emit = [&](double distance, size_t model_index) {
                const std::string& path = repeater.model_paths[model_index % repeater.model_paths.size()];
                if (path.empty()) return;
                double world[16] = {};
                if (!make_repeater_instance_world(repeater, distance, world)) return;
                SceneInstanceData data = make_instance_data_relative(world, render_origin);

                SceneScreenBounds bounds;
                SceneScreenBounds* bounds_ptr = nullptr;
                if (can_pick && scene_object_index_valid(repeater.object_index)) {
                    auto model_it = scene_models.find(path);
                    if (model_it != scene_models.end() &&
                        compute_scene_instance_screen_bounds(world, model_it->second, render_origin, view_proj,
                                                             width, height, bounds)) {
                        bounds_ptr = &bounds;
                    }
                }
                append_visible_model_instance(path, data, repeater.object_index, bounds_ptr,
                                              visible_instances, object_refs);
            };

            if (!scene_repeater_has_interval(repeater)) {
                if (repeater.begin_distance >= begin - k_scene_repeater_distance_epsilon &&
                    repeater.begin_distance <= end + k_scene_repeater_distance_epsilon) {
                    emit(repeater.begin_distance, 0);
                }
                continue;
            }

            SceneRepeaterIndexRange range;
            if (!scene_repeater_index_range(repeater, begin, end, range)) continue;
            long long emitted = 0;
            const double end_epsilon = scene_repeater_index_epsilon(repeater);
            for (long long index = range.first; index <= range.last; ++index) {
                double distance = repeater.begin_distance + static_cast<double>(index) * repeater.interval;
                if (distance < begin - k_scene_repeater_distance_epsilon) continue;
                if (distance > end + k_scene_repeater_distance_epsilon) break;
                if (distance >= repeater.end_distance - end_epsilon) break;
                emit(distance, static_cast<size_t>(index));
                if (++emitted >= k_scene_repeater_instance_limit) break;
            }
        }
    }

    bool update_scene_camera_from_owntrack() {
        Canvas3DTrackPoint point;
        if (!sample_own_track(scene_camera_distance, point)) return false;
        DVec3 base{point.x, point.y, point.z};
        DVec3 right = right_from_theta_d(point.theta);
        scene_camera_pos = base + right * scene_camera_lateral_offset;
        scene_camera_pos.y += scene_camera_vertical_offset;
        scene_camera_yaw = point.theta + scene_camera_yaw_offset;
        return true;
    }

    bool reset_scene_camera_pose_at_distance(double distance) {
        if (!scene_active) return false;
        scene_camera_distance = std::clamp(distance, scene_data.min_distance, scene_data.max_distance);
        scene_camera_lateral_offset = 0.0;
        scene_camera_vertical_offset = k_default_scene_camera_height;
        scene_camera_yaw_offset = 0.0f;
        scene_camera_pitch = 0.0f;
        scene_rotating = false;
        return update_scene_camera_from_owntrack();
    }

    void reset_scene_camera_tracking() {
        Canvas3DTrackPoint point;
        scene_camera_lateral_offset = 0.0;
        scene_camera_vertical_offset = k_default_scene_camera_height;
        scene_camera_yaw_offset = 0.0f;
        if (!sample_own_track(scene_camera_distance, point)) return;

        DVec3 base{point.x, point.y, point.z};
        DVec3 current = scene_camera_pos;
        DVec3 diff = current - base;
        scene_camera_lateral_offset = dot(diff, right_from_theta_d(point.theta));
        scene_camera_vertical_offset = static_cast<float>(diff.y);
        scene_camera_yaw_offset = scene_camera_yaw - point.theta;
        update_scene_camera_from_owntrack();
    }

    static int structure_drag_axis_index(Canvas3DSceneDragAxis axis) {
        switch (axis) {
            case Canvas3DSceneDragAxis::X: return 0;
            case Canvas3DSceneDragAxis::Y: return 1;
            case Canvas3DSceneDragAxis::Z: return 2;
            case Canvas3DSceneDragAxis::None: break;
        }
        return -1;
    }

    static Canvas3DSceneDragAxis structure_drag_axis_from_index(size_t index) {
        if (index == 0) return Canvas3DSceneDragAxis::X;
        if (index == 1) return Canvas3DSceneDragAxis::Y;
        if (index == 2) return Canvas3DSceneDragAxis::Z;
        return Canvas3DSceneDragAxis::None;
    }

    static double truncate_scene_millimeter(double value) {
        if (!std::isfinite(value)) return value;
        double scaled = value * 1000.0;
        const double nearest = std::round(scaled);
        if (std::abs(scaled - nearest) < 1e-9) scaled = nearest;
        double result = std::trunc(scaled) / 1000.0;
        return result == 0.0 ? 0.0 : result;
    }

    static bool scene_ray_triangle_intersection(DVec3 ray_origin,
                                                DVec3 ray_direction,
                                                DVec3 a,
                                                DVec3 b,
                                                DVec3 c,
                                                double& ray_parameter,
                                                double& barycentric_b,
                                                double& barycentric_c) {
        constexpr double parallel_epsilon = 1e-10;
        constexpr double forward_epsilon = 1e-6;
        const DVec3 edge_ab = b - a;
        const DVec3 edge_ac = c - a;
        const DVec3 p = cross(ray_direction, edge_ac);
        const double determinant = dot(edge_ab, p);
        if (!std::isfinite(determinant) || std::abs(determinant) <= parallel_epsilon) {
            return false;
        }

        const double inverse_determinant = 1.0 / determinant;
        const DVec3 origin_to_a = ray_origin - a;
        const double u = dot(origin_to_a, p) * inverse_determinant;
        if (!std::isfinite(u) || u < 0.0 || u > 1.0) return false;

        const DVec3 q = cross(origin_to_a, edge_ab);
        const double v = dot(ray_direction, q) * inverse_determinant;
        if (!std::isfinite(v) || v < 0.0 || u + v > 1.0) return false;

        const double t = dot(edge_ac, q) * inverse_determinant;
        if (!std::isfinite(t) || t <= forward_epsilon) return false;
        ray_parameter = t;
        barycentric_b = u;
        barycentric_c = v;
        return true;
    }

    std::optional<double> pick_scene_mileage(ImVec2 mouse_local,
                                             int width,
                                             int height,
                                             double visible_min,
                                             double visible_max) const {
        if (scene_mileage_pick_points.size() < 2) return std::nullopt;
        visible_min = std::max(visible_min, scene_data.min_distance);
        visible_max = std::min(visible_max, scene_data.max_distance);
        if (!std::isfinite(visible_min) || !std::isfinite(visible_max) ||
            visible_min > visible_max) {
            return std::nullopt;
        }

        DVec3 ray_origin;
        DVec3 ray_direction;
        if (!scene_camera_ray(mouse_local, width, height, ray_origin, ray_direction)) {
            return std::nullopt;
        }

        const auto begin_it = std::lower_bound(
            scene_mileage_pick_points.begin(), scene_mileage_pick_points.end(), visible_min,
            [](const SceneMileagePickPoint& point, double distance) {
                return point.distance < distance;
            });
        size_t first_segment = static_cast<size_t>(
            std::distance(scene_mileage_pick_points.begin(), begin_it));
        if (first_segment > 0) --first_segment;
        const auto end_it = std::upper_bound(
            scene_mileage_pick_points.begin(), scene_mileage_pick_points.end(), visible_max,
            [](double distance, const SceneMileagePickPoint& point) {
                return distance < point.distance;
            });
        const size_t segment_end = static_cast<size_t>(
            std::distance(scene_mileage_pick_points.begin(), end_it));

        double nearest_ray_parameter = std::numeric_limits<double>::infinity();
        double nearest_distance = 0.0;
        auto consider_triangle = [&](DVec3 a, DVec3 b, DVec3 c,
                                     double distance_a,
                                     double distance_b,
                                     double distance_c) {
            double ray_parameter = 0.0;
            double barycentric_b = 0.0;
            double barycentric_c = 0.0;
            if (!scene_ray_triangle_intersection(
                    ray_origin, ray_direction, a, b, c,
                    ray_parameter, barycentric_b, barycentric_c) ||
                ray_parameter >= nearest_ray_parameter) {
                return;
            }
            const double barycentric_a = 1.0 - barycentric_b - barycentric_c;
            const double distance = distance_a * barycentric_a +
                distance_b * barycentric_b + distance_c * barycentric_c;
            if (!std::isfinite(distance) || distance < visible_min || distance > visible_max) {
                return;
            }
            nearest_ray_parameter = ray_parameter;
            nearest_distance = distance;
        };

        for (size_t i = first_segment;
             i + 1 < scene_mileage_pick_points.size() && i < segment_end; ++i) {
            const SceneMileagePickPoint& a = scene_mileage_pick_points[i];
            const SceneMileagePickPoint& b = scene_mileage_pick_points[i + 1];
            if (b.distance < visible_min || a.distance > visible_max) continue;
            consider_triangle(a.left, a.right, b.right,
                              a.distance, a.distance, b.distance);
            consider_triangle(a.left, b.right, b.left,
                              a.distance, b.distance, b.distance);
        }
        if (!std::isfinite(nearest_ray_parameter)) return std::nullopt;

        const double first_integer_distance = std::ceil(scene_data.min_distance);
        const double last_integer_distance = std::floor(scene_data.max_distance);
        if (first_integer_distance > last_integer_distance) return std::nullopt;
        return std::clamp(std::round(nearest_distance),
                          first_integer_distance, last_integer_distance);
    }

    bool scene_camera_ray(ImVec2 mouse_local, int width, int height,
                          DVec3& ray_origin, DVec3& ray_direction) const {
        if (width <= 0 || height <= 0) return false;
        const double ndc_x = 2.0 * static_cast<double>(mouse_local.x) /
            static_cast<double>(width) - 1.0;
        const double ndc_y = 1.0 - 2.0 * static_cast<double>(mouse_local.y) /
            static_cast<double>(height);
        const double aspect = static_cast<double>(width) / static_cast<double>(height);
        const double tan_half_fov = std::tan(static_cast<double>(k_scene_camera_fov_y) * 0.5);
        DVec3 forward = dvec3_from_vec3(scene_forward());
        DVec3 right = dvec3_from_vec3(scene_right());
        DVec3 up = normalize(cross(right, forward));
        ray_origin = scene_camera_pos;
        ray_direction = normalize(forward + right * (ndc_x * aspect * tan_half_fov) +
                                  up * (ndc_y * tan_half_fov));
        return std::isfinite(ray_direction.x) && std::isfinite(ray_direction.y) &&
            std::isfinite(ray_direction.z);
    }

    static bool closest_axis_parameter(DVec3 axis_origin, DVec3 axis_direction,
                                       DVec3 ray_origin, DVec3 ray_direction,
                                       double& parameter) {
        const double axis_length_squared = dot(axis_direction, axis_direction);
        if (!std::isfinite(axis_length_squared) || axis_length_squared <= 1e-12) {
            return false;
        }
        const double axis_length = std::sqrt(axis_length_squared);
        axis_direction = normalize(axis_direction);
        ray_direction = normalize(ray_direction);
        DVec3 w = axis_origin - ray_origin;
        const double a = dot(axis_direction, axis_direction);
        const double b = dot(axis_direction, ray_direction);
        const double c = dot(ray_direction, ray_direction);
        const double d = dot(axis_direction, w);
        const double e = dot(ray_direction, w);
        const double denominator = a * c - b * b;
        if (!std::isfinite(denominator) || denominator < 1e-4) return false;
        parameter = (b * e - c * d) / denominator / axis_length;
        return std::isfinite(parameter);
    }

    static float point_segment_distance_sq(ImVec2 point, ImVec2 a, ImVec2 b) {
        const float vx = b.x - a.x;
        const float vy = b.y - a.y;
        const float length_sq = vx * vx + vy * vy;
        float t = 0.0f;
        if (length_sq > 1e-6f) {
            t = std::clamp(((point.x - a.x) * vx + (point.y - a.y) * vy) / length_sq,
                           0.0f, 1.0f);
        }
        const float dx = point.x - (a.x + vx * t);
        const float dy = point.y - (a.y + vy * t);
        return dx * dx + dy * dy;
    }

    bool update_scene_gizmo_projection(SceneGizmoHandle& gizmo,
                                       bool visible,
                                       int width,
                                       int height) {
        for (SceneGizmoAxisProjection& projection : gizmo.projection) {
            projection = SceneGizmoAxisProjection{};
        }
        if (!visible || width <= 0 || height <= 0) {
            return false;
        }

        const DVec3 relative_origin = gizmo.origin - scene_camera_pos;
        const DVec3 camera_forward = dvec3_from_vec3(scene_forward());
        const double depth = dot(relative_origin, camera_forward);
        if (!std::isfinite(depth) || depth <= static_cast<double>(k_scene_near_z)) return false;

        Vec3 forward = scene_forward();
        Mat4 view = look_to_bve({0.0f, 0.0f, 0.0f}, forward, {0.0f, 1.0f, 0.0f});
        const float aspect = static_cast<float>(width) /
            std::max(1.0f, static_cast<float>(height));
        Mat4 proj = perspective_fov_lh_reverse_z(k_scene_camera_fov_y, aspect,
                                                 k_scene_near_z,
                                                 scene_far_z(effective_scene_window_forward_m()));
        Mat4 view_proj = multiply(view, proj);
        ImVec2 origin_screen;
        if (!project_scene_point(relative_origin, view_proj, width, height, origin_screen) ||
            origin_screen.x < 0.0f || origin_screen.y < 0.0f ||
            origin_screen.x > static_cast<float>(width) ||
            origin_screen.y > static_cast<float>(height)) {
            return false;
        }

        const double generic_world_units_per_pixel =
            2.0 * depth * std::tan(static_cast<double>(k_scene_camera_fov_y) * 0.5) /
            static_cast<double>(height);
        static constexpr std::array<ImVec2, 3> fallback_directions = {
            ImVec2(0.8660254f, 0.5f),
            ImVec2(0.0f, -1.0f),
            ImVec2(-0.8660254f, 0.5f)
        };
        const DVec3 to_camera = scene_camera_pos - gizmo.origin;
        const float gizmo_scale = scene_edit_component_scale;
        bool any = false;
        for (size_t i = 0; i < gizmo.axes.size(); ++i) {
            if (!gizmo.enabled[i]) continue;
            const DVec3 parameter_axis = gizmo.axes[i];
            const double axis_length_squared = dot(parameter_axis, parameter_axis);
            if (!std::isfinite(axis_length_squared) || axis_length_squared <= 1e-12) {
                continue;
            }
            const double axis_length = std::sqrt(axis_length_squared);
            const DVec3 parameter_direction = parameter_axis * (1.0 / axis_length);
            const double parameter_sign =
                dot(parameter_direction, to_camera) < 0.0 ? -1.0 : 1.0;
            const DVec3 visual_axis = parameter_axis * parameter_sign;
            ImVec2 one_parameter_screen;
            const bool projected = project_scene_point(relative_origin + visual_axis, view_proj,
                                                        width, height,
                                                        one_parameter_screen);
            float dx = projected ? one_parameter_screen.x - origin_screen.x : 0.0f;
            float dy = projected ? one_parameter_screen.y - origin_screen.y : 0.0f;
            const float projected_length = std::sqrt(dx * dx + dy * dy);
            ImVec2 direction(fallback_directions[i].x * static_cast<float>(parameter_sign),
                             fallback_directions[i].y * static_cast<float>(parameter_sign));
            if (projected_length >= 1.0f) {
                direction = ImVec2(dx / projected_length, dy / projected_length);
            }
            SceneGizmoAxisProjection& axis_projection = gizmo.projection[i];
            axis_projection.valid = true;
            axis_projection.ray_drag_reliable = projected_length >= 4.0f;
            axis_projection.direction = direction;
            axis_projection.world_direction = visual_axis;
            axis_projection.parameter_sign = parameter_sign;
            axis_projection.begin = ImVec2(
                origin_screen.x + direction.x * k_scene_gizmo_origin_gap_px * gizmo_scale,
                origin_screen.y + direction.y * k_scene_gizmo_origin_gap_px * gizmo_scale);
            axis_projection.end = ImVec2(
                origin_screen.x + direction.x * k_scene_gizmo_length_px * gizmo_scale,
                origin_screen.y + direction.y * k_scene_gizmo_length_px * gizmo_scale);
            axis_projection.parameter_units_per_pixel = projected_length >= 1.0f
                ? 1.0 / static_cast<double>(projected_length)
                : generic_world_units_per_pixel / axis_length;
            any = true;
        }
        return any;
    }

    bool update_scene_structure_gizmo_projections(int width, int height) {
        const bool placement_visible =
            scene_structure_edit.active && scene_structure_edit.show_gizmo;
        const bool repeater_end_visible =
            scene_structure_edit.active &&
            scene_structure_edit.kind == Canvas3DSceneEditKind::Repeater &&
            scene_structure_edit.current.has_repeater_end_distance;
        const bool placement = update_scene_gizmo_projection(
            scene_structure_edit.placement_gizmo,
            placement_visible, width, height);
        const bool repeater_end = update_scene_gizmo_projection(
            scene_structure_edit.repeater_end_gizmo,
            repeater_end_visible, width, height);
        return placement || repeater_end;
    }

    SceneGizmoHandle* scene_gizmo_handle(SceneGizmoTarget target) {
        if (target == SceneGizmoTarget::Placement) {
            return &scene_structure_edit.placement_gizmo;
        }
        if (target == SceneGizmoTarget::RepeaterEndDistance) {
            return &scene_structure_edit.repeater_end_gizmo;
        }
        return nullptr;
    }

    std::optional<Canvas3DPlacementDragUpdate> handle_scene_structure_gizmo_input(
        bool canvas_hovered, int width, int height, ImVec2 mouse_local) {
        if (!update_scene_structure_gizmo_projections(width, height)) {
            scene_structure_edit.hovered_gizmo = SceneGizmoTarget::None;
            scene_structure_edit.hovered_axis = Canvas3DSceneDragAxis::None;
            if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                scene_structure_edit.dragging_gizmo = SceneGizmoTarget::None;
                scene_structure_edit.dragging_axis = Canvas3DSceneDragAxis::None;
            }
            return std::nullopt;
        }

        if (scene_structure_edit.dragging_gizmo == SceneGizmoTarget::None) {
            scene_structure_edit.hovered_gizmo = SceneGizmoTarget::None;
            scene_structure_edit.hovered_axis = Canvas3DSceneDragAxis::None;
            if (canvas_hovered) {
                const float hit_radius = k_scene_gizmo_hit_radius_px * scene_edit_component_scale;
                float best_distance_sq = hit_radius * hit_radius;
                const auto consider = [&](SceneGizmoTarget target,
                                          const SceneGizmoHandle& gizmo) {
                    for (size_t i = 0; i < gizmo.projection.size(); ++i) {
                        const SceneGizmoAxisProjection& projection = gizmo.projection[i];
                        if (!projection.valid) continue;
                        const float distance_sq = point_segment_distance_sq(
                            mouse_local, projection.begin, projection.end);
                        if (distance_sq <= best_distance_sq) {
                            best_distance_sq = distance_sq;
                            scene_structure_edit.hovered_gizmo = target;
                            scene_structure_edit.hovered_axis =
                                structure_drag_axis_from_index(i);
                        }
                    }
                };
                consider(SceneGizmoTarget::Placement,
                         scene_structure_edit.placement_gizmo);
                // Check the EndDistance handle last so an exact overlap selects
                // the endpoint and a zero-length Repeater can still be extended.
                consider(SceneGizmoTarget::RepeaterEndDistance,
                         scene_structure_edit.repeater_end_gizmo);
            }

            if (scene_structure_edit.hovered_gizmo != SceneGizmoTarget::None &&
                scene_structure_edit.hovered_axis != Canvas3DSceneDragAxis::None &&
                ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                const int axis_index = structure_drag_axis_index(scene_structure_edit.hovered_axis);
                SceneGizmoHandle* gizmo =
                    scene_gizmo_handle(scene_structure_edit.hovered_gizmo);
                if (!gizmo || axis_index < 0) return std::nullopt;
                const SceneGizmoAxisProjection& projection =
                    gizmo->projection[static_cast<size_t>(axis_index)];
                scene_structure_edit.dragging_gizmo =
                    scene_structure_edit.hovered_gizmo;
                scene_structure_edit.dragging_axis = scene_structure_edit.hovered_axis;
                scene_structure_edit.drag_axis_origin = gizmo->origin;
                scene_structure_edit.drag_axis_direction = projection.world_direction;
                scene_structure_edit.drag_start_mouse = mouse_local;
                scene_structure_edit.drag_screen_direction = projection.direction;
                scene_structure_edit.drag_parameter_units_per_pixel =
                    projection.parameter_units_per_pixel;
                scene_structure_edit.drag_parameter_sign = projection.parameter_sign;
                if (scene_structure_edit.dragging_gizmo ==
                    SceneGizmoTarget::RepeaterEndDistance) {
                    scene_structure_edit.drag_start_value =
                        scene_structure_edit.current.repeater_end_distance;
                } else if (scene_structure_edit.kind ==
                           Canvas3DSceneEditKind::StructurePutBetween) {
                    scene_structure_edit.drag_start_value =
                        scene_structure_edit.current.distance;
                } else if (scene_structure_edit.kind ==
                           Canvas3DSceneEditKind::Sound3D &&
                           scene_structure_edit.dragging_axis ==
                           Canvas3DSceneDragAxis::Z) {
                    scene_structure_edit.drag_start_value =
                        scene_structure_edit.current.distance;
                } else if (scene_structure_edit.current.placement_distance_gizmo) {
                    scene_structure_edit.drag_start_value =
                        scene_structure_edit.current.distance;
                } else {
                    scene_structure_edit.drag_start_value = axis_index == 0
                        ? scene_structure_edit.current.x
                        : axis_index == 1 ? scene_structure_edit.current.y
                                          : scene_structure_edit.current.z;
                }
                DVec3 ray_origin;
                DVec3 ray_direction;
                scene_structure_edit.drag_uses_ray =
                    projection.ray_drag_reliable &&
                    scene_camera_ray(mouse_local, width, height, ray_origin, ray_direction) &&
                    closest_axis_parameter(scene_structure_edit.drag_axis_origin,
                                           scene_structure_edit.drag_axis_direction,
                                           ray_origin, ray_direction,
                                           scene_structure_edit.drag_start_axis_parameter);
            }
        }

        if (scene_structure_edit.dragging_gizmo == SceneGizmoTarget::None ||
            scene_structure_edit.dragging_axis == Canvas3DSceneDragAxis::None) {
            return std::nullopt;
        }
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            scene_structure_edit.dragging_gizmo = SceneGizmoTarget::None;
            scene_structure_edit.dragging_axis = Canvas3DSceneDragAxis::None;
            return std::nullopt;
        }

        double delta = 0.0;
        bool used_ray = false;
        if (scene_structure_edit.drag_uses_ray) {
            DVec3 ray_origin;
            DVec3 ray_direction;
            double current_parameter = 0.0;
            if (scene_camera_ray(mouse_local, width, height, ray_origin, ray_direction) &&
                closest_axis_parameter(scene_structure_edit.drag_axis_origin,
                                       scene_structure_edit.drag_axis_direction,
                                       ray_origin, ray_direction, current_parameter)) {
                delta = current_parameter - scene_structure_edit.drag_start_axis_parameter;
                used_ray = true;
            }
        }
        if (!used_ray) {
            const double screen_delta_x =
                static_cast<double>(mouse_local.x - scene_structure_edit.drag_start_mouse.x);
            const double screen_delta_y =
                static_cast<double>(mouse_local.y - scene_structure_edit.drag_start_mouse.y);
            delta = (screen_delta_x * scene_structure_edit.drag_screen_direction.x +
                     screen_delta_y * scene_structure_edit.drag_screen_direction.y) *
                scene_structure_edit.drag_parameter_units_per_pixel;
        }
        delta *= scene_structure_edit.drag_parameter_sign;

        const bool put_between_distance_drag =
            scene_structure_edit.dragging_gizmo == SceneGizmoTarget::Placement &&
            scene_structure_edit.kind ==
            Canvas3DSceneEditKind::StructurePutBetween;
        const bool repeater_end_distance_drag =
            scene_structure_edit.dragging_gizmo ==
            SceneGizmoTarget::RepeaterEndDistance;
        const bool placement_distance_drag =
            scene_structure_edit.dragging_gizmo == SceneGizmoTarget::Placement &&
            scene_structure_edit.current.placement_distance_gizmo;
        const bool sound3d_distance_drag =
            scene_structure_edit.dragging_gizmo == SceneGizmoTarget::Placement &&
            scene_structure_edit.kind == Canvas3DSceneEditKind::Sound3D &&
            scene_structure_edit.dragging_axis == Canvas3DSceneDragAxis::Z;
        const bool distance_drag =
            put_between_distance_drag || repeater_end_distance_drag ||
            placement_distance_drag || sound3d_distance_drag;
        const double candidate = distance_drag
            ? std::round(scene_structure_edit.drag_start_value + delta)
            : truncate_scene_millimeter(scene_structure_edit.drag_start_value + delta);
        const int axis_index = structure_drag_axis_index(scene_structure_edit.dragging_axis);
        double* current_value = repeater_end_distance_drag
            ? &scene_structure_edit.current.repeater_end_distance
            : put_between_distance_drag || placement_distance_drag ||
            sound3d_distance_drag
            ? &scene_structure_edit.current.distance
            : axis_index == 0 ? &scene_structure_edit.current.x
            : axis_index == 1 ? &scene_structure_edit.current.y
                              : &scene_structure_edit.current.z;
        const double change_threshold = distance_drag
            ? 0.5 : 0.000999999;
        if (std::abs(candidate - *current_value) < change_threshold) return std::nullopt;
        const double previous_value = *current_value;
        *current_value = candidate;
        const Canvas3DSceneDragAxis changed_axis = scene_structure_edit.dragging_axis;
        bool wrote = false;
        if (put_between_distance_drag) {
            const std::uint64_t sequence = queue_scene_put_between_preview(
                scene_structure_edit.current);
            wrote = sequence != 0;
            if (wrote) scene_structure_edit.preview_sequence = sequence;
        } else if (scene_structure_edit.kind == Canvas3DSceneEditKind::Repeater) {
            Canvas3DRepeaterSegment desired = repeater_segment_from_target(
                scene_structure_edit.current, scene_structure_edit.baseline_repeater);
            wrote = write_scene_repeater_segment(scene_structure_edit.edit_id, std::move(desired));
        } else if (scene_structure_edit.kind == Canvas3DSceneEditKind::Sound3D) {
            SceneGizmoHandle gizmo;
            wrote = sound3d_gizmo_frame(scene_structure_edit.current, gizmo) &&
                update_scene_sound3d_marker(
                    scene_structure_edit.edit_id,
                    scene_structure_edit.current.distance,
                    scene_structure_edit.current.x,
                    scene_structure_edit.current.y);
            if (wrote) scene_structure_edit.placement_gizmo = gizmo;
        } else {
            Canvas3DModelInstance desired = placement_instance_from_target(
                scene_structure_edit.current, scene_structure_edit.baseline_instance);
            wrote = write_scene_placement_instance(scene_structure_edit.edit_id, std::move(desired));
        }
        if (!wrote) {
            *current_value = previous_value;
            return std::nullopt;
        }

        Canvas3DPlacementDragUpdate result;
        result.kind = scene_structure_edit.kind;
        result.edit_id = scene_structure_edit.edit_id;
        result.target = repeater_end_distance_drag
            ? Canvas3DSceneDragTarget::RepeaterEndDistance
                : put_between_distance_drag
                ? Canvas3DSceneDragTarget::PutBetweenDistance
                : placement_distance_drag || sound3d_distance_drag
                    ? Canvas3DSceneDragTarget::PlacementDistance
                    : Canvas3DSceneDragTarget::Placement;
        result.axis = changed_axis;
        result.distance = scene_structure_edit.current.distance;
        result.repeater_end_distance =
            scene_structure_edit.current.repeater_end_distance;
        result.x = scene_structure_edit.current.x;
        result.y = scene_structure_edit.current.y;
        result.z = scene_structure_edit.current.z;
        return result;
    }

    bool scene_structure_gizmo_consumes_left_input() const {
        return scene_structure_edit.dragging_gizmo != SceneGizmoTarget::None ||
            scene_structure_edit.hovered_gizmo != SceneGizmoTarget::None;
    }

    void draw_scene_gizmo_handle(ImDrawList* draw,
                                 ImVec2 canvas_origin,
                                 const SceneGizmoHandle& gizmo,
                                 SceneGizmoTarget target) {
        static constexpr std::array<ImU32, 3> colors = {
            IM_COL32(235, 67, 67, 255),
            IM_COL32(73, 205, 91, 255),
            IM_COL32(65, 126, 245, 255)
        };
        static constexpr std::array<ImU32, 3> active_colors = {
            IM_COL32(255, 142, 142, 255),
            IM_COL32(153, 255, 164, 255),
            IM_COL32(151, 190, 255, 255)
        };
        const float gizmo_scale = scene_edit_component_scale;
        for (size_t i = 0; i < gizmo.projection.size(); ++i) {
            const SceneGizmoAxisProjection& projection = gizmo.projection[i];
            if (!projection.valid) continue;
            const Canvas3DSceneDragAxis axis = structure_drag_axis_from_index(i);
            const bool active =
                (scene_structure_edit.dragging_gizmo == target &&
                 scene_structure_edit.dragging_axis == axis) ||
                (scene_structure_edit.hovered_gizmo == target &&
                 scene_structure_edit.hovered_axis == axis);
            const ImU32 color = active ? active_colors[i] : colors[i];
            ImVec2 begin(canvas_origin.x + projection.begin.x,
                         canvas_origin.y + projection.begin.y);
            ImVec2 end(canvas_origin.x + projection.end.x,
                       canvas_origin.y + projection.end.y);
            ImVec2 direction = projection.direction;
            ImVec2 perpendicular(-direction.y, direction.x);
            const float arrow_length = k_scene_gizmo_arrow_length_px * gizmo_scale;
            const float arrow_half_width = k_scene_gizmo_arrow_half_width_px * gizmo_scale;
            ImVec2 arrow_base(end.x - direction.x * arrow_length,
                              end.y - direction.y * arrow_length);
            draw->AddLine(begin, end, color, (active ? 4.5f : 3.0f) * gizmo_scale);
            draw->AddTriangleFilled(
                end,
                ImVec2(arrow_base.x + perpendicular.x * arrow_half_width,
                       arrow_base.y + perpendicular.y * arrow_half_width),
                ImVec2(arrow_base.x - perpendicular.x * arrow_half_width,
                       arrow_base.y - perpendicular.y * arrow_half_width),
                color);
        }
        const SceneGizmoAxisProjection* first = nullptr;
        for (const SceneGizmoAxisProjection& projection : gizmo.projection) {
            if (projection.valid) {
                first = &projection;
                break;
            }
        }
        if (first) {
            ImVec2 center(
                canvas_origin.x + first->begin.x -
                    first->direction.x * k_scene_gizmo_origin_gap_px * gizmo_scale,
                canvas_origin.y + first->begin.y -
                    first->direction.y * k_scene_gizmo_origin_gap_px * gizmo_scale);
            const float center_radius = k_scene_gizmo_center_radius_px * gizmo_scale;
            draw->AddCircleFilled(center, center_radius, IM_COL32(245, 245, 245, 235));
            draw->AddCircle(center, center_radius, IM_COL32(30, 30, 30, 220), 0,
                            gizmo_scale);
        }
    }

    void draw_scene_structure_gizmo(ImDrawList* draw, ImVec2 canvas_origin,
                                    int width, int height) {
        if (!draw) return;
        const bool placement = update_scene_gizmo_projection(
            scene_structure_edit.placement_gizmo,
            scene_structure_edit.active && scene_structure_edit.show_gizmo,
            width, height);
        const bool repeater_end = update_scene_gizmo_projection(
            scene_structure_edit.repeater_end_gizmo,
            scene_structure_edit.active &&
                scene_structure_edit.kind == Canvas3DSceneEditKind::Repeater &&
                scene_structure_edit.current.has_repeater_end_distance,
            width, height);
        if (placement) {
            draw_scene_gizmo_handle(draw, canvas_origin,
                                    scene_structure_edit.placement_gizmo,
                                    SceneGizmoTarget::Placement);
        }
        if (repeater_end) {
            draw_scene_gizmo_handle(draw, canvas_origin,
                                    scene_structure_edit.repeater_end_gizmo,
                                    SceneGizmoTarget::RepeaterEndDistance);
        }
    }

    void handle_scene_input(bool hovered, bool block_left_drag) {
        if (!hovered) {
            scene_rotating = false;
            return;
        }
        ImGuiIO& io = ImGui::GetIO();
        if (ImGui::IsKeyPressed(ImGuiKey_X, false)) {
            reset_scene_camera_pose_at_distance(scene_camera_distance);
        }

        const bool rotation_enabled = scene_interaction_mode == Canvas3DSceneInteractionMode::Move;
        if (rotation_enabled && !block_left_drag && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            if (!scene_rotating) {
                scene_rotating = true;
                scene_last_mouse = io.MousePos;
            } else {
                ImVec2 delta(io.MousePos.x - scene_last_mouse.x, io.MousePos.y - scene_last_mouse.y);
                const float yaw_delta = -delta.x * 0.005f;
                scene_camera_yaw += yaw_delta;
                scene_camera_yaw_offset += yaw_delta;
                scene_camera_pitch = std::clamp(scene_camera_pitch + delta.y * 0.005f, -1.45f, 1.45f);
                scene_last_mouse = io.MousePos;
            }
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
        } else {
            scene_rotating = false;
        }

        float dt = std::clamp(io.DeltaTime, 1.0f / 240.0f, 0.1f);
        bool fast = ImGui::IsKeyDown(ImGuiKey_LeftCtrl) || ImGui::IsKeyDown(ImGuiKey_RightCtrl);
        float speed_factor = static_cast<float>(scene_camera_speed_percent) / 100.0f;
        float step = scene_slow_speed_mps * speed_factor * (fast ? scene_fast_multiplier : 1.0f) * dt;
        float distance_delta = 0.0f;
        float lateral_delta = 0.0f;
        float vertical_delta = 0.0f;
        if (ImGui::IsKeyDown(ImGuiKey_W)) distance_delta += step;
        if (ImGui::IsKeyDown(ImGuiKey_S)) distance_delta -= step;
        if (ImGui::IsKeyDown(ImGuiKey_D)) lateral_delta += step;
        if (ImGui::IsKeyDown(ImGuiKey_A)) lateral_delta -= step;
        if (ImGui::IsKeyDown(ImGuiKey_R)) vertical_delta += step;
        if (ImGui::IsKeyDown(ImGuiKey_F)) vertical_delta -= step;

        Canvas3DTrackPoint own_point;
        if (sample_own_track(scene_camera_distance, own_point)) {
            scene_camera_distance = std::clamp(scene_camera_distance + static_cast<double>(distance_delta),
                                               scene_data.min_distance, scene_data.max_distance);
            scene_camera_lateral_offset += lateral_delta;
            scene_camera_vertical_offset += vertical_delta;
            update_scene_camera_from_owntrack();
        } else {
            Vec3 forward = scene_forward();
            Vec3 right = scene_right();
            DVec3 delta{};
            delta = delta + dvec3_from_vec3(forward) * static_cast<double>(distance_delta);
            delta = delta + dvec3_from_vec3(right) * static_cast<double>(lateral_delta);
            delta.y += vertical_delta;
            scene_camera_pos = scene_camera_pos + delta;
            scene_camera_distance = std::clamp(scene_camera_distance + static_cast<double>(distance_delta),
                                               scene_data.min_distance, scene_data.max_distance);
        }
    }

    std::string current_background_path() const {
        std::string path;
        for (const Canvas3DBackgroundChange& bg : scene_data.backgrounds) {
            if (bg.distance > scene_camera_distance) break;
            path = bg.model_path;
        }
        return path;
    }

    void draw_background_model(const Mat4& view_proj, const SceneFogSample* fog) {
        std::string path = current_background_path();
        if (path.empty()) return;
        auto it = scene_models.find(path);
        if (it == scene_models.end() || it->second.state != SceneModelGpu::State::Ready) return;
        Mat4 world = identity();
        std::vector<SceneInstanceData> instances{make_instance_data(world)};
        draw_scene_model(it->second, instances, view_proj, fog);
    }

    double effective_scene_window_forward_m() const {
        if (!scene_map_draw_distance_enabled || scene_data.draw_distance_changes.empty()) {
            return scene_window_forward_m;
        }
        const auto next = std::upper_bound(
            scene_data.draw_distance_changes.begin(), scene_data.draw_distance_changes.end(),
            scene_camera_distance,
            [](double distance, const Canvas3DSceneDrawDistanceChange& change) {
                return distance < change.distance;
            });
        if (next == scene_data.draw_distance_changes.begin()) return scene_window_forward_m;
        const double map_value = std::prev(next)->value;
        if (!std::isfinite(map_value) || map_value <= 0.0) return scene_window_forward_m;
        return std::min(scene_window_forward_m, map_value);
    }

    float scene_far_z(double window_forward_m) const {
        double far_z = scene_window_back_m + window_forward_m + scene_chunk_m * 2.0;
        if (!std::isfinite(far_z)) far_z = k_scene_background_far_z;
        return static_cast<float>(std::clamp(far_z, 256.0, static_cast<double>(k_scene_background_far_z)));
    }

    void render_scene_preview_target(int width, int height, ImVec2 mouse_local,
                                     bool pick_enabled,
                                     bool mileage_pick_enabled) {
        std::string error;
        scene_hovered_object_index = -1;
        scene_hovered_marker_index = -1;
        scene_hovered_mileage.reset();
        scene_hover_highlight_batch.clear();
        if (!ensure_render_target(width, height, error)) {
            if (scene_last_error != error) scene_last_error = error;
            return;
        }
        if (!ensure_scene_pipeline(error)) {
            if (scene_last_error != error) scene_last_error = error;
            return;
        }
        upload_pending_scene_models();
        apply_scene_put_between_preview_result();
        if (!scene_data.markers.empty() &&
            !scene_marker_font_cache_current() &&
            !build_scene_marker_chunks(error)) {
            if (scene_last_error != error) scene_last_error = error;
        }

#ifndef NDEBUG
        debug_scene_fog_draw_part_count = 0;
#endif

        const SceneFogSample fog = sample_canvas3d_scene_fog(
            scene_data.fog_keyframes, scene_camera_distance, scene_fog_enabled);
        const SceneFogSample* fog_ptr = fog.enabled ? &fog : nullptr;

        const ImVec4 bg = clamp_theme_color(background_color_value);
        const float clear_color[4] = {bg.x, bg.y, bg.z, 1.0f};
        context->OMSetRenderTargets(1, &render_rtv, depth_dsv);
        context->ClearRenderTargetView(render_rtv, clear_color);
        context->ClearDepthStencilView(depth_dsv, D3D11_CLEAR_DEPTH, k_scene_depth_clear, 0);

        D3D11_VIEWPORT viewport = {};
        viewport.Width = static_cast<float>(width);
        viewport.Height = static_cast<float>(height);
        viewport.MinDepth = 0.0f;
        viewport.MaxDepth = 1.0f;
        context->RSSetViewports(1, &viewport);
        context->RSSetState(rasterizer_state);
        context->OMSetDepthStencilState(scene_depth_state, 0);
        const float blend_factor[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        context->OMSetBlendState(nullptr, blend_factor, 0xffffffff);

        const DVec3 render_origin = scene_camera_pos;
        Vec3 forward = scene_forward();
        Mat4 view = look_to_bve({0.0f, 0.0f, 0.0f}, forward, {0.0f, 1.0f, 0.0f});
        float aspect = static_cast<float>(width) / std::max(1.0f, static_cast<float>(height));
        Mat4 background_proj = perspective_fov_lh_reverse_z(k_scene_camera_fov_y, aspect, k_scene_background_near_z, k_scene_background_far_z);
        Mat4 background_view_proj = multiply(view, background_proj);
        draw_background_model(background_view_proj, fog_ptr);
        context->ClearDepthStencilView(depth_dsv, D3D11_CLEAR_DEPTH, k_scene_depth_clear, 0);

        const double effective_window_forward_m = effective_scene_window_forward_m();
        Mat4 proj = perspective_fov_lh_reverse_z(
            k_scene_camera_fov_y, aspect, k_scene_near_z,
            scene_far_z(effective_window_forward_m));
        Mat4 view_proj = multiply(view, proj);

        scene_stats_value.drawn_instance_count = 0;
        scene_stats_value.drawn_track_chunk_count = 0;
        scene_stats_value.camera_distance = scene_camera_distance;

        double visible_min = scene_camera_distance - scene_window_back_m;
        double visible_max = scene_camera_distance + effective_window_forward_m;
        if (scene_interaction_mode == Canvas3DSceneInteractionMode::MileageSelect) {
            if (mileage_pick_enabled) {
                scene_hovered_mileage = pick_scene_mileage(
                    mouse_local, width, height, visible_min, visible_max);
            } else if (scene_context_mileage) {
                scene_hovered_mileage = scene_context_mileage;
            }
        }
        std::map<std::string, std::vector<SceneInstanceData>> visible_instances;
        std::map<int, std::vector<SceneVisibleInstanceRef>> visible_object_instances;
        std::vector<SceneInstanceData> track_instance(1);
        const bool can_pick = pick_enabled && scene_interaction_mode == Canvas3DSceneInteractionMode::Select;
        for (size_t i = 0; i < scene_chunks.size(); ++i) {
            const SceneChunk& chunk = scene_chunks[i];
            if (!scene_chunk_visible(chunk, visible_min, visible_max)) continue;
            for (const SceneInstance& instance : chunk.instances) {
                if (instance.distance < visible_min || instance.distance > visible_max) continue;
                SceneInstanceData data = make_instance_data_relative(instance.world, render_origin);
                SceneScreenBounds bounds;
                SceneScreenBounds* bounds_ptr = nullptr;
                if (can_pick && scene_object_index_valid(instance.object_index)) {
                    auto model_it = scene_models.find(instance.model_path);
                    if (model_it != scene_models.end() &&
                        compute_scene_instance_screen_bounds(instance.world, model_it->second, render_origin, view_proj,
                                                             width, height, bounds)) {
                        bounds_ptr = &bounds;
                    }
                }
                append_visible_model_instance(instance.model_path, data, instance.object_index, bounds_ptr,
                                              visible_instances,
                                              can_pick ? &visible_object_instances : nullptr);
            }
            append_visible_repeater_instances(chunk, visible_min, visible_max, render_origin,
                                              view_proj, width, height, can_pick,
                                              visible_instances,
                                              can_pick ? &visible_object_instances : nullptr);
        }

        for (auto& kv : visible_instances) {
            auto model_it = scene_models.find(kv.first);
            if (model_it == scene_models.end()) continue;
            draw_scene_model(model_it->second, kv.second, view_proj, fog_ptr);
            if (model_it->second.state == SceneModelGpu::State::Ready) {
                scene_stats_value.drawn_instance_count += kv.second.size();
            }
        }
        const bool marker_pick_possible =
            can_pick && has_visible_scene_marker_picks(
                visible_min, visible_max);
        const bool scene_pick_active =
            can_pick && (!visible_object_instances.empty() || marker_pick_possible) &&
            begin_scene_pick_at_mouse(
                visible_instances, visible_object_instances, view_proj,
                width, height, mouse_local, error);
        if (!error.empty() && scene_last_error != error) scene_last_error = error;
        ID3D11RenderTargetView* scene_target = render_rtv;
        context->OMSetRenderTargets(1, &scene_target, depth_dsv);
        context->RSSetViewports(1, &viewport);
        context->OMSetDepthStencilState(scene_depth_state, 0);
        context->OMSetBlendState(nullptr, blend_factor, 0xffffffff);
        for (size_t i = 0; i < scene_chunks.size() && i < scene_track_chunks.size(); ++i) {
            if (!scene_chunk_visible(scene_chunks[i], visible_min, visible_max)) continue;
            draw_scene_track_chunk(scene_track_chunks[i], render_origin, view_proj,
                                   track_instance, error, fog_ptr);
        }
        draw_scene_mileage_highlight(render_origin, view_proj, error);
        if (!error.empty() && scene_last_error != error) scene_last_error = error;
        ScenePickTarget picked_target;
        if (scene_pick_active) {
            ID3D11RenderTargetView* pick_target = scene_pick_rtv;
            context->OMSetRenderTargets(1, &pick_target, depth_dsv);
            context->RSSetViewports(1, &viewport);
            draw_visible_scene_marker_picks(
                visible_min, visible_max, render_origin, view_proj);
            picked_target = finish_scene_pick_at_mouse(
                width, height, mouse_local, error);
            if (!error.empty() && scene_last_error != error) {
                scene_last_error = error;
            }
            context->OMSetRenderTargets(1, &scene_target, depth_dsv);
            context->RSSetViewports(1, &viewport);
            context->OMSetDepthStencilState(scene_depth_state, 0);
            context->OMSetBlendState(nullptr, blend_factor, 0xffffffff);
        }
        draw_visible_scene_markers(
            visible_min, visible_max, render_origin, view_proj);
        if (picked_target.kind == ScenePickTargetKind::Object &&
            picked_target.index < scene_data.objects.size()) {
            const int picked_object_index =
                static_cast<int>(picked_target.index);
            scene_hovered_object_index = picked_object_index;
            scene_hover_highlight_batch.object_index = picked_object_index;
            scene_hover_highlight_batch.screen_min = ImVec2(
                static_cast<float>(width), static_cast<float>(height));
            scene_hover_highlight_batch.screen_max = ImVec2(0.0f, 0.0f);
            auto refs_it = visible_object_instances.find(picked_object_index);
            if (refs_it != visible_object_instances.end()) {
                for (const SceneVisibleInstanceRef& ref : refs_it->second) {
                    if (!ref.model_path) continue;
                    auto visible_it = visible_instances.find(*ref.model_path);
                    if (visible_it == visible_instances.end() ||
                        ref.instance_index >= visible_it->second.size()) {
                        continue;
                    }
                    scene_hover_highlight_batch.instances.push_back(
                        SceneHighlightInstance{
                            *ref.model_path,
                            visible_it->second[ref.instance_index],
                            ref.screen_min,
                            ref.screen_max
                        });
                    include_scene_screen_bounds(
                        scene_hover_highlight_batch.screen_min,
                        scene_hover_highlight_batch.screen_max, ref);
                }
            }
            if (picked_object_index == scene_focus_highlight_object_index) {
                clear_scene_focus_highlight();
            }
        } else if (picked_target.kind == ScenePickTargetKind::Marker &&
                   picked_target.index < scene_data.markers.size()) {
            scene_hovered_marker_index = static_cast<int>(picked_target.index);
        }
        const bool focus_highlight_active = scene_focus_highlight_active_now();
        if (focus_highlight_active && scene_focus_highlight_marker_index >= 0 &&
            static_cast<size_t>(scene_focus_highlight_marker_index) < scene_data.markers.size()) {
            draw_scene_marker_highlight(
                static_cast<size_t>(scene_focus_highlight_marker_index), render_origin,
                view_proj, width, height);
        }
        update_scene_focus_highlight_batch(render_origin, view_proj, width, height);
        if (scene_hovered_marker_index >= 0 &&
            scene_hovered_marker_index != scene_focus_highlight_marker_index) {
            draw_scene_marker_highlight(
                static_cast<size_t>(scene_hovered_marker_index), render_origin,
                view_proj, width, height);
        }
        draw_scene_highlight_batch(scene_focus_highlight_batch, view_proj, width, height);
        draw_scene_highlight_batch(scene_hover_highlight_batch, view_proj, width, height);

        ID3D11ShaderResourceView* null_srv = nullptr;
        context->PSSetShaderResources(0, 1, &null_srv);
        context->OMSetBlendState(nullptr, nullptr, 0xffffffff);
        ID3D11RenderTargetView* null_rtv = nullptr;
        context->OMSetRenderTargets(1, &null_rtv, nullptr);
        update_scene_fps_counter();
    }

    void reset_scene_fps_counter() {
        scene_fps_last_frame_valid = false;
        scene_fps_value = 0.0f;
    }

    void update_scene_fps_counter() {
        using Clock = std::chrono::steady_clock;
        const Clock::time_point now = Clock::now();
        if (scene_fps_last_frame_valid) {
            const double elapsed_seconds = std::chrono::duration<double>(now - scene_fps_last_frame_at).count();
            if (elapsed_seconds > 0.0 && elapsed_seconds <= k_scene_fps_idle_reset_seconds) {
                const float sample = static_cast<float>(1.0 / elapsed_seconds);
                scene_fps_value = scene_fps_value > 0.0f
                    ? scene_fps_value + (sample - scene_fps_value) * k_scene_fps_smoothing
                    : sample;
            }
        }
        scene_fps_last_frame_at = now;
        scene_fps_last_frame_valid = true;
    }

    SceneOverlayLabelLayout scene_overlay_label_layout(
        ImVec2 origin, ImVec2 size, ImVec2 text_size, SceneOverlayCorner corner) const {
        const float pad = std::max(4.0f, ImGui::GetStyle().FramePadding.x);
        ImVec2 pos(origin.x + pad * 2.0f, origin.y + pad * 2.0f);
        if (corner == SceneOverlayCorner::TopRight) {
            pos.x = origin.x + size.x - text_size.x - pad * 2.0f;
        } else if (corner == SceneOverlayCorner::BottomRight) {
            pos.x = origin.x + size.x - text_size.x - pad * 2.0f;
            pos.y = origin.y + size.y - text_size.y - pad * 2.0f;
        }
        pos.x = std::max(origin.x + pad, pos.x);
        pos.y = std::max(origin.y + pad, pos.y);
        return SceneOverlayLabelLayout{pos, pad};
    }

    void draw_scene_overlay_label_background(ImDrawList* draw,
                                             const SceneOverlayLabelLayout& layout,
                                             ImVec2 text_size) const {
        if (!draw) return;
        draw->AddRectFilled(ImVec2(layout.pos.x - layout.pad, layout.pos.y - layout.pad * 0.5f),
                            ImVec2(layout.pos.x + text_size.x + layout.pad,
                                   layout.pos.y + text_size.y + layout.pad * 0.5f),
                            IM_COL32(0, 0, 0, 140), 3.0f);
    }

    void draw_scene_overlay_label(ImDrawList* draw, ImVec2 origin, ImVec2 size,
                                  const char* text, SceneOverlayCorner corner) const {
        if (!draw || !text || size.x <= 0.0f || size.y <= 0.0f) return;
        const ImVec2 text_size = ImGui::CalcTextSize(text);
        const SceneOverlayLabelLayout layout =
            scene_overlay_label_layout(origin, size, text_size, corner);
        draw_scene_overlay_label_background(draw, layout, text_size);
        draw->AddText(layout.pos, IM_COL32(255, 255, 255, 230), text);
    }

    void draw_scene_overlay(ImDrawList* draw, ImVec2 origin, ImVec2 size) const {
        if (!draw || size.x <= 0.0f || size.y <= 0.0f || !scene_active) return;
        char buffer[256] = {};
        std::snprintf(buffer, sizeof(buffer), "x=%.1fm  y=%.1fm  d=%.1fm",
                      scene_camera_lateral_offset,
                      static_cast<double>(scene_camera_vertical_offset),
                      scene_camera_distance);
        draw_scene_overlay_label(draw, origin, size, buffer, SceneOverlayCorner::TopLeft);
    }

    void draw_scene_route_overlay(ImDrawList* draw, ImVec2 origin, ImVec2 size,
                                  const Canvas3DSceneUiText& ui_text) const {
        if (!draw || size.x <= 0.0f || size.y <= 0.0f || !scene_active) return;

        const SceneRouteValueSample radius =
            sample_scene_route_value(scene_data.route_info.radius_events, scene_camera_distance);
        const SceneRouteValueSample cant =
            sample_scene_route_value(scene_data.route_info.cant_events, scene_camera_distance);
        const SceneRouteValueSample gradient =
            sample_scene_route_value(scene_data.route_info.gradient_events, scene_camera_distance);

        char curve_line[192] = {};
        if (radius.mode == SceneRouteValueMode::Interpolate) {
            std::snprintf(curve_line, sizeof(curve_line), "%s",
                          ui_text.interpolate_unsupported);
        } else {
            const bool transition = radius.mode == SceneRouteValueMode::Transition;
            const bool use_transition_target =
                !transition || std::abs(radius.to_value) > k_scene_route_display_zero_epsilon ||
                std::abs(radius.from_value) <= k_scene_route_display_zero_epsilon;
            const double displayed_radius = transition
                ? (use_transition_target ? radius.to_value : radius.from_value)
                : radius.value;
            const double displayed_cant = transition
                ? (use_transition_target ? cant.to_value : cant.from_value)
                : cant.value;
            const char* prefix = transition ? "[Tr.] " : "";
            if (std::abs(displayed_radius) <= k_scene_route_display_zero_epsilon) {
                std::snprintf(curve_line, sizeof(curve_line), "%s%s",
                              prefix, ui_text.straight);
            } else {
                char radius_text[64] = {};
                char cant_text[64] = {};
                format_scene_route_number(radius_text, sizeof(radius_text),
                                          std::abs(displayed_radius));
                format_scene_route_number(cant_text, sizeof(cant_text), displayed_cant);
                std::snprintf(curve_line, sizeof(curve_line), "%sR %s m %s %s",
                              prefix, radius_text, cant_text,
                              displayed_radius < 0.0 ? u8"←" : u8"→");
            }
        }

        const bool gradient_transition = gradient.mode != SceneRouteValueMode::Constant;
        const bool use_gradient_target =
            !gradient_transition || std::abs(gradient.to_value) > k_scene_route_display_zero_epsilon ||
            std::abs(gradient.from_value) <= k_scene_route_display_zero_epsilon;
        const double displayed_gradient = gradient_transition
            ? (use_gradient_target ? gradient.to_value : gradient.from_value)
            : gradient.value;
        char gradient_text[64] = {};
        format_scene_route_number(gradient_text, sizeof(gradient_text),
                                  std::abs(displayed_gradient));
        char gradient_line[160] = {};
        const char* gradient_prefix = gradient_transition ? "[Tr.] " : "";
        if (std::abs(displayed_gradient) <= k_scene_route_display_zero_epsilon) {
            std::snprintf(gradient_line, sizeof(gradient_line), "%s0‰", gradient_prefix);
        } else {
            std::snprintf(gradient_line, sizeof(gradient_line), "%s%s %s‰",
                          gradient_prefix,
                          displayed_gradient > 0.0 ? u8"↗" : u8"↘",
                          gradient_text);
        }

        char speed_limit_line[160] = {};
        const auto speed_limit_next = std::upper_bound(
            scene_data.route_info.speed_limit_events.begin(),
            scene_data.route_info.speed_limit_events.end(), scene_camera_distance,
            [](double distance, const Canvas3DSceneSpeedLimitEvent& event) {
                return distance < event.distance;
            });
        if (speed_limit_next == scene_data.route_info.speed_limit_events.begin() ||
            !(speed_limit_next - 1)->has_speed) {
            std::snprintf(speed_limit_line, sizeof(speed_limit_line), "%s -",
                          ui_text.speed_limit);
        } else {
            char speed_text[64] = {};
            format_scene_route_number(speed_text, sizeof(speed_text),
                                      (speed_limit_next - 1)->speed);
            std::snprintf(speed_limit_line, sizeof(speed_limit_line),
                          "%s %s km/h", ui_text.speed_limit, speed_text);
        }

        char signal_line[768] = {};
        const auto section_signal_next = std::upper_bound(
            scene_data.route_info.section_signal_events.begin(),
            scene_data.route_info.section_signal_events.end(), scene_camera_distance,
            [](double distance, const Canvas3DSceneSectionSignalEvent& event) {
                return distance < event.distance;
            });
        if (section_signal_next ==
                scene_data.route_info.section_signal_events.begin() ||
            (section_signal_next - 1)->values.empty()) {
            std::snprintf(signal_line, sizeof(signal_line), "%s -", ui_text.signal);
        } else {
            std::snprintf(signal_line, sizeof(signal_line), "%s %s", ui_text.signal,
                          (section_signal_next - 1)->values.c_str());
        }

        char station_line[768] = {};
        const auto next_station = std::upper_bound(
            scene_data.route_info.stations.begin(), scene_data.route_info.stations.end(),
            scene_camera_distance,
            [](double distance, const Canvas3DSceneRouteStation& station) {
                return distance < station.distance;
            });
        if (next_station == scene_data.route_info.stations.end()) {
            std::snprintf(station_line, sizeof(station_line), "%s",
                          ui_text.no_station_ahead);
        } else {
            const double remaining_m = std::max(0.0, next_station->distance - scene_camera_distance);
            std::snprintf(station_line, sizeof(station_line), "%s %s  %.0fm",
                          ui_text.next_station, next_station->name.c_str(), remaining_m);
        }

        char buffer[2048] = {};
        std::snprintf(buffer, sizeof(buffer), "%s\n%s\n%s\n%s\n%s",
                      curve_line, gradient_line, speed_limit_line,
                      signal_line, station_line);
        draw_scene_overlay_label(draw, origin, size, buffer, SceneOverlayCorner::TopRight);
    }

    ImU32 scene_instance_metric_color(size_t instance_count) const {
        if (!scene_performance_warning_enabled) return IM_COL32(255, 255, 255, 230);
        if (instance_count > scene_instance_critical_warning_threshold) {
            return IM_COL32(255, 96, 96, 230);
        }
        if (instance_count > scene_instance_warning_threshold) {
            return IM_COL32(255, 220, 0, 230);
        }
        return IM_COL32(255, 255, 255, 230);
    }

    void draw_scene_metrics_overlay(ImDrawList* draw, ImVec2 origin, ImVec2 size,
                                    const Canvas3DSceneStats& stats) const {
        if (!draw || size.x <= 0.0f || size.y <= 0.0f || !scene_active) return;
        char prefix[64] = {};
        char instance[64] = {};
        char suffix[96] = {};
        std::snprintf(prefix, sizeof(prefix), "chunks=%zu  ", stats.chunk_count);
        std::snprintf(instance, sizeof(instance), "instances=%zu", stats.drawn_instance_count);
        std::snprintf(suffix, sizeof(suffix), "  models=%zu/%zu  %.1f fps",
                      stats.model_ready_count,
                      stats.model_path_count,
                      static_cast<double>(scene_fps_value));
        const ImVec2 prefix_size = ImGui::CalcTextSize(prefix);
        const ImVec2 instance_size = ImGui::CalcTextSize(instance);
        const ImVec2 suffix_size = ImGui::CalcTextSize(suffix);
        const ImVec2 text_size(
            prefix_size.x + instance_size.x + suffix_size.x,
            std::max(prefix_size.y, std::max(instance_size.y, suffix_size.y)));
        const SceneOverlayLabelLayout layout = scene_overlay_label_layout(
            origin, size, text_size, SceneOverlayCorner::BottomRight);
        draw_scene_overlay_label_background(draw, layout, text_size);
        const ImU32 normal_color = IM_COL32(255, 255, 255, 230);
        draw->AddText(layout.pos, normal_color, prefix);
        ImVec2 instance_pos(layout.pos.x + prefix_size.x, layout.pos.y);
        draw->AddText(instance_pos,
                      scene_instance_metric_color(stats.drawn_instance_count), instance);
        ImVec2 suffix_pos(instance_pos.x + instance_size.x, layout.pos.y);
        draw->AddText(suffix_pos, normal_color, suffix);
    }

    void draw_scene_loading_overlay(ImDrawList* draw, ImVec2 origin, ImVec2 size,
                                    const char* text) const {
        if (!draw || size.x <= 0.0f || size.y <= 0.0f) return;
        ImVec2 end(origin.x + size.x, origin.y + size.y);
        ImVec4 bg = clamp_theme_color(background_color_value);
        bg.w = 1.0f;
        draw->AddRectFilled(origin, end, ImGui::ColorConvertFloat4ToU32(bg));

        const char* label = text && text[0] ? text : "Loading...";
        ImVec2 text_size = ImGui::CalcTextSize(label);
        ImVec2 pos(origin.x + std::max(0.0f, (size.x - text_size.x) * 0.5f),
                   origin.y + std::max(0.0f, (size.y - text_size.y) * 0.5f));
        const float luminance = bg.x * 0.2126f + bg.y * 0.7152f + bg.z * 0.0722f;
        const ImU32 text_color = luminance > 0.55f ? IM_COL32(24, 24, 24, 235)
                                                   : IM_COL32(255, 255, 255, 235);
        const ImU32 shadow_color = luminance > 0.55f ? IM_COL32(255, 255, 255, 90)
                                                     : IM_COL32(0, 0, 0, 110);
        draw->AddText(ImVec2(pos.x + 1.0f, pos.y + 1.0f), shadow_color, label);
        draw->AddText(pos, text_color, label);
    }

    static const char* scene_object_edit_row_kind(const Canvas3DSceneObject& object) {
        if (object.kind == Canvas3DSceneObjectKind::Structure) {
            return object.structure_put_between ? "structure.between" : "structure.put";
        }
        if (object.kind == Canvas3DSceneObjectKind::Signal) return "signal.put";
        return object.kind == Canvas3DSceneObjectKind::Repeater ? "repeater" : nullptr;
    }

    Canvas3DSceneContextAction render_scene_context_popup(
        const Canvas3DSceneUiText& ui_text,
        const Canvas3DSceneContextMenuOptions& context_menu_options) {
        Canvas3DSceneContextAction action;
        if (!ImGui::BeginPopup("ScenePreviewObjectContext")) return action;

        if (scene_object_index_valid(scene_context_object_index)) {
            Canvas3DSceneObject& object = scene_data.objects[static_cast<size_t>(scene_context_object_index)];
            if (object.kind == Canvas3DSceneObjectKind::Structure) {
                const char* locate_label = object.structure_put_between
                    ? ui_text.locate_structure_put_between_list
                    : ui_text.locate_structure_list;
                if (ImGui::MenuItem(locate_label)) {
                    action.kind = Canvas3DSceneContextActionKind::LocateStructure;
                    action.row_index = object.source_row;
                }
                const char* edit_row_kind = scene_object_edit_row_kind(object);
                ImGui::BeginDisabled(!context_menu_options.element_properties_enabled ||
                                     object.edit_id.empty() || !edit_row_kind);
                if (ImGui::MenuItem(ui_text.element_properties)) {
                    action.kind = Canvas3DSceneContextActionKind::EditElement;
                    action.edit_id = object.edit_id;
                    action.row_kind = edit_row_kind;
                }
                if (ImGui::MenuItem(ui_text.delete_element)) {
                    action.kind = Canvas3DSceneContextActionKind::DeleteElement;
                    action.edit_id = object.edit_id;
                    action.row_kind = edit_row_kind;
                }
                ImGui::EndDisabled();
            } else if (object.kind == Canvas3DSceneObjectKind::Repeater) {
                if (ImGui::MenuItem(ui_text.locate_repeater_list)) {
                    action.kind = Canvas3DSceneContextActionKind::LocateRepeater;
                    action.row_index = object.source_row;
                }
                const char* edit_row_kind = scene_object_edit_row_kind(object);
                ImGui::BeginDisabled(!context_menu_options.element_properties_enabled ||
                                     object.edit_id.empty() || !edit_row_kind);
                if (ImGui::MenuItem(ui_text.element_properties)) {
                    action.kind = Canvas3DSceneContextActionKind::EditElement;
                    action.edit_id = object.edit_id;
                    action.row_kind = edit_row_kind;
                }
                ImGui::EndDisabled();
                const Canvas3DRepeaterSegment* repeater = find_repeater_segment(object.source_row);
                const bool delete_enabled = context_menu_options.element_properties_enabled &&
                    !object.edit_id.empty() && edit_row_kind && repeater;
                if (repeater && repeater->chain_begin_count > 1) {
                    if (ImGui::BeginMenu(ui_text.delete_element, delete_enabled)) {
                        if (ImGui::MenuItem(ui_text.delete_repeater_all)) {
                            action.kind = Canvas3DSceneContextActionKind::DeleteRepeaterAll;
                        } else if (ImGui::MenuItem(ui_text.delete_repeater_change_point)) {
                            action.kind = Canvas3DSceneContextActionKind::DeleteRepeaterChangePoint;
                        } else if (repeater->chain_begin_index != 0 &&
                                   ImGui::MenuItem(ui_text.trim_repeater_to_change_point)) {
                            action.kind = Canvas3DSceneContextActionKind::TrimRepeaterToChangePoint;
                        } else if (repeater->chain_begin_index != 0 &&
                                   ImGui::MenuItem(ui_text.start_repeater_from_change_point)) {
                            action.kind = Canvas3DSceneContextActionKind::StartRepeaterFromChangePoint;
                        }
                        if (action.kind != Canvas3DSceneContextActionKind::None) {
                            action.edit_id = object.edit_id;
                            action.row_kind = edit_row_kind;
                        }
                        ImGui::EndMenu();
                    }
                } else {
                    ImGui::BeginDisabled(!delete_enabled);
                    if (ImGui::MenuItem(ui_text.delete_element)) {
                        action.kind = Canvas3DSceneContextActionKind::DeleteRepeaterAll;
                        action.edit_id = object.edit_id;
                        action.row_kind = edit_row_kind;
                    }
                    ImGui::EndDisabled();
                }
                ImGui::Separator();
                if (ImGui::MenuItem(ui_text.jump_to_repeater_start_position)) {
                    jump_scene_camera_to_object(Canvas3DSceneObjectKind::Repeater, object.source_row);
                }
                ImGui::BeginDisabled(!repeater || !repeater->has_end_or_change_position);
                if (ImGui::MenuItem(ui_text.jump_to_repeater_end_or_change_position)) {
                    jump_scene_camera_to_repeater_end_or_change(object.source_row);
                }
                ImGui::EndDisabled();
            } else if (object.kind == Canvas3DSceneObjectKind::Signal) {
                if (ImGui::MenuItem(ui_text.locate_signal_list)) {
                    action.kind = Canvas3DSceneContextActionKind::LocateSignal;
                    action.row_index = object.source_row;
                }
                const char* edit_row_kind = scene_object_edit_row_kind(object);
                ImGui::BeginDisabled(!context_menu_options.element_properties_enabled ||
                                     object.edit_id.empty() || !edit_row_kind);
                if (ImGui::MenuItem(ui_text.element_properties)) {
                    action.kind = Canvas3DSceneContextActionKind::EditElement;
                    action.edit_id = object.edit_id;
                    action.row_kind = edit_row_kind;
                }
                if (ImGui::MenuItem(ui_text.delete_element)) {
                    action.kind = Canvas3DSceneContextActionKind::DeleteElement;
                    action.edit_id = object.edit_id;
                    action.row_kind = edit_row_kind;
                }
                ImGui::EndDisabled();
                if (ImGui::BeginMenu(ui_text.switch_signal_aspect, !object.model_options.empty())) {
                    for (size_t i = 0; i < object.model_options.size(); ++i) {
                        const Canvas3DSceneModelOption& option = object.model_options[i];
                        std::string label = std::to_string(option.structure_key_index) + " - " + option.structure_key;
                        const bool selected = i == object.selected_model_option;
                        ImGui::BeginDisabled(option.model_path.empty());
                        if (ImGui::MenuItem(label.c_str(), nullptr, selected)) {
                            set_scene_object_model_option(scene_context_object_index, i);
                        }
                        ImGui::EndDisabled();
                    }
                    ImGui::EndMenu();
                }
            }
        }

        ImGui::EndPopup();
        return action;
    }

    static bool scene_marker_has_list_target(const Canvas3DSceneMarker& marker) {
        return scene_marker_list_kind_is_navigable(marker.list_kind) &&
            marker.row_index.has_value();
    }

    static bool scene_marker_has_edit_target(const Canvas3DSceneMarker& marker) {
        return (marker.kind == MapMarkerVisualKind::Station &&
                 marker.row_kind == "station.put") ||
               marker.row_kind == "curve" || marker.row_kind == "gradient" ||
               marker.row_kind == "otherTrack.change" ||
               scene_marker_has_list_target(marker);
    }

    Canvas3DSceneContextAction render_scene_marker_context_popup(
        const Canvas3DSceneUiText& ui_text,
        const Canvas3DSceneContextMenuOptions& context_menu_options) {
        Canvas3DSceneContextAction action;
        if (!ImGui::BeginPopup("ScenePreviewMarkerContext")) return action;

        if (scene_context_marker_index >= 0 &&
            static_cast<size_t>(scene_context_marker_index) < scene_data.markers.size()) {
            const Canvas3DSceneMarker& marker =
                scene_data.markers[static_cast<size_t>(scene_context_marker_index)];
            const char* locate_label = scene_marker_has_list_target(marker)
                ? ui_text.locate_marker_list_labels[
                    scene_marker_list_kind_slot(marker.list_kind)]
                : nullptr;
            if (locate_label && ImGui::MenuItem(locate_label)) {
                action.kind = Canvas3DSceneContextActionKind::LocateMarkerList;
                action.marker_list_kind = marker.list_kind;
                action.row_index = *marker.row_index;
            }

            const bool edit_target = scene_marker_has_edit_target(marker);
            if (edit_target) {
                ImGui::BeginDisabled(!context_menu_options.element_properties_enabled ||
                                     marker.edit_id.empty());
                if (ImGui::MenuItem(ui_text.element_properties)) {
                    action.kind = Canvas3DSceneContextActionKind::EditElement;
                    action.edit_id = marker.edit_id;
                    action.row_kind = marker.row_kind;
                }
                ImGui::EndDisabled();
                ImGui::BeginDisabled(!context_menu_options.element_properties_enabled ||
                                     marker.edit_id.empty());
                if (ImGui::MenuItem(ui_text.delete_element)) {
                    action.kind = Canvas3DSceneContextActionKind::DeleteElement;
                    action.edit_id = marker.edit_id;
                    action.row_kind = marker.row_kind;
                }
                ImGui::EndDisabled();
            }
            if (context_menu_options.element_properties_enabled &&
                marker.unpaired_transition) {
                ImGui::Separator();
                ImGui::TextWrapped("%s", ui_text.unpaired_transition);
            }
        }

        ImGui::EndPopup();
        return action;
    }

    std::optional<double> render_scene_mileage_context_popup(
        const Canvas3DSceneUiText& ui_text,
        const Canvas3DSceneContextMenuOptions& context_menu_options) {
        std::optional<double> requested_mileage;
        if (ImGui::BeginPopup("ScenePreviewMileageContext")) {
            if (scene_interaction_mode != Canvas3DSceneInteractionMode::MileageSelect ||
                !scene_context_mileage) {
                ImGui::CloseCurrentPopup();
            } else if (ImGui::MenuItem(ui_text.add_map_element_at_mileage, nullptr, false,
                                      context_menu_options.new_element_enabled)) {
                requested_mileage = scene_context_mileage;
            }
            ImGui::EndPopup();
        }
        if (!ImGui::IsPopupOpen("ScenePreviewMileageContext")) {
            scene_context_mileage.reset();
        }
        return requested_mileage;
    }

    static void draw_scene_mileage_label(ImVec2 origin,
                                         ImVec2 end,
                                         ImVec2 pointer_pos,
                                         double mileage,
                                         const Canvas3DSceneUiText& ui_text) {
        std::array<char, 128> label{};
        std::snprintf(label.data(), label.size(), "%s: %.0f %s",
                      ui_text.mileage, mileage, ui_text.unit_m);

        const ImGuiStyle& style = ImGui::GetStyle();
        const ImVec2 text_size = ImGui::CalcTextSize(label.data());
        const ImVec2 label_size(text_size.x + style.WindowPadding.x * 2.0f,
                                text_size.y + style.WindowPadding.y * 2.0f);
        const float pointer_gap = style.ItemSpacing.x;
        float label_x = pointer_pos.x + pointer_gap;
        if (label_x + label_size.x > end.x) {
            label_x = pointer_pos.x - pointer_gap - label_size.x;
        }
        label_x = std::clamp(label_x, origin.x,
                             std::max(origin.x, end.x - label_size.x));
        const float label_y = std::clamp(pointer_pos.y - label_size.y * 0.5f,
                                         origin.y,
                                         std::max(origin.y, end.y - label_size.y));

        ImGui::SetNextWindowPos(ImVec2(label_x, label_y), ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.9f);
        constexpr ImGuiWindowFlags label_flags =
            ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
            ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
            ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoMove;
        if (ImGui::Begin("##ScenePreviewMileageLabel", nullptr, label_flags)) {
            ImGui::TextUnformatted(label.data());
        }
        ImGui::End();
    }

    Canvas3DSceneFrameResult render_scene_preview(
        ImVec2 requested_size,
        const Canvas3DSceneUiText& ui_text,
        const Canvas3DSceneContextMenuOptions& context_menu_options) {
        Canvas3DSceneFrameResult result;
        ImVec2 avail = requested_size;
        if (avail.x <= 0.0f || avail.y <= 0.0f) avail = ImGui::GetContentRegionAvail();
        avail.x = std::max(avail.x, 50.0f);
        avail.y = std::max(avail.y, 50.0f);

        ImVec2 origin = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton("ScenePreview3DCanvas", avail,
                               ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
        bool hovered = ImGui::IsItemHovered();
        const bool context_popup_open =
            ImGui::IsPopupOpen("ScenePreviewObjectContext") ||
            ImGui::IsPopupOpen("ScenePreviewMarkerContext") ||
            ImGui::IsPopupOpen("ScenePreviewMileageContext");
        const bool loading_before_render = scene_stats().loading;
        ImGuiIO& io = ImGui::GetIO();
        ImVec2 pointer_pos = io.MousePos;
        const touch_input::TouchFrame& touch = touch_input::current_frame();
        if (touch.long_press) pointer_pos = touch.long_press_pos;
        else if (touch.tap) pointer_pos = touch.tap_pos;
        ImVec2 mouse_local(pointer_pos.x - origin.x, pointer_pos.y - origin.y);

        int width = std::max(1, static_cast<int>(std::round(avail.x)));
        int height = std::max(1, static_cast<int>(std::round(avail.y)));
        if (scene_active && !loading_before_render) {
            result.placement_drag = handle_scene_structure_gizmo_input(
                hovered, width, height, mouse_local);
            handle_scene_input(hovered, scene_structure_gizmo_consumes_left_input());
        }
        if (scene_active) {
            render_scene_preview_target(
                width, height, mouse_local,
                (hovered || context_popup_open) && !scene_structure_gizmo_consumes_left_input(),
                hovered && !loading_before_render);
        } else {
            std::string error;
            ensure_render_target(width, height, error);
            if (render_rtv && context) {
                const ImVec4 bg = clamp_theme_color(background_color_value);
                const float clear_color[4] = {bg.x, bg.y, bg.z, 1.0f};
                context->OMSetRenderTargets(1, &render_rtv, depth_dsv);
                context->ClearRenderTargetView(render_rtv, clear_color);
                if (depth_dsv) context->ClearDepthStencilView(depth_dsv, D3D11_CLEAR_DEPTH, 1.0f, 0);
                ID3D11RenderTargetView* null_rtv = nullptr;
                context->OMSetRenderTargets(1, &null_rtv, nullptr);
            }
        }

        ImDrawList* draw = ImGui::GetWindowDrawList();
        ImVec2 end(origin.x + avail.x, origin.y + avail.y);
        if (render_srv) {
            draw->AddImage(reinterpret_cast<void*>(render_srv), origin, end);
        } else {
            draw->AddRectFilled(origin, end, IM_COL32(0, 0, 0, 255));
        }
        const Canvas3DSceneStats stats = scene_stats();
        if (stats.loading) {
            draw_scene_loading_overlay(draw, origin, avail, ui_text.loading);
        } else {
            draw_scene_overlay(draw, origin, avail);
            draw_scene_route_overlay(draw, origin, avail, ui_text);
            draw_scene_metrics_overlay(draw, origin, avail, stats);
            draw_scene_structure_gizmo(draw, origin, width, height);
        }

        const bool select_mode = scene_interaction_mode == Canvas3DSceneInteractionMode::Select;
        const bool mileage_select_mode =
            scene_interaction_mode == Canvas3DSceneInteractionMode::MileageSelect;
        if (mileage_select_mode) {
            result.hovered_mileage = scene_hovered_mileage;
            if (!stats.loading && hovered && scene_hovered_mileage) {
                draw_scene_mileage_label(origin, end, pointer_pos,
                                         *scene_hovered_mileage, ui_text);
            }
        }
        if (select_mode && scene_hovered_marker_index >= 0 &&
            static_cast<size_t>(scene_hovered_marker_index) <
                scene_data.markers.size()) {
            const Canvas3DSceneMarker& marker = scene_data.markers[
                static_cast<size_t>(scene_hovered_marker_index)];
            result.hovered_marker = Canvas3DSceneMarkerTarget{
                marker.kind,
                marker.list_kind,
                static_cast<size_t>(scene_hovered_marker_index),
                marker.row_kind,
                marker.row_index,
                marker.edit_id
            };
        }
        const bool marker_context_available =
            scene_hovered_marker_index >= 0 &&
            static_cast<size_t>(scene_hovered_marker_index) < scene_data.markers.size() &&
            (scene_marker_has_list_target(
                 scene_data.markers[static_cast<size_t>(scene_hovered_marker_index)]) ||
             scene_marker_has_edit_target(
                 scene_data.markers[static_cast<size_t>(scene_hovered_marker_index)]));
        if (!stats.loading && scene_structure_gizmo_consumes_left_input()) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
        } else if (!stats.loading && select_mode && hovered &&
                   (scene_hovered_object_index >= 0 ||
                    scene_hovered_marker_index >= 0)) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        }
        ImVec2 long_press_pos;
        const bool touch_context =
            select_mode && !stats.loading &&
            (scene_hovered_object_index >= 0 || marker_context_available) &&
            touch_input::consume_long_press_in_rect(origin, end, &long_press_pos);
        if (!stats.loading && select_mode &&
            (scene_hovered_object_index >= 0 || marker_context_available) &&
            ((hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) || touch_context)) {
            if (scene_hovered_object_index >= 0) {
                scene_context_object_index = scene_hovered_object_index;
                ImGui::OpenPopup("ScenePreviewObjectContext");
            } else {
                scene_context_marker_index = scene_hovered_marker_index;
                ImGui::OpenPopup("ScenePreviewMarkerContext");
            }
        }
        if (!stats.loading && mileage_select_mode && hovered && scene_hovered_mileage &&
            ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
            scene_context_mileage = scene_hovered_mileage;
            ImGui::OpenPopup("ScenePreviewMileageContext");
        }
        result.context_action = render_scene_context_popup(ui_text, context_menu_options);
        if (result.context_action.kind == Canvas3DSceneContextActionKind::None) {
            result.context_action = render_scene_marker_context_popup(
                ui_text, context_menu_options);
        }
        result.new_element_mileage =
            render_scene_mileage_context_popup(ui_text, context_menu_options);
        return result;
    }

    void render_scene(int width, int height) {
        std::string error;
        if (!ensure_render_target(width, height, error)) {
            if (last_error != error) last_error = error;
            return;
        }
        if (has_model() && !ensure_scene_pipeline(error)) {
            if (last_error != error) last_error = error;
        }

        const ImVec4 bg = clamp_theme_color(background_color_value);
        const float clear_color[4] = {bg.x, bg.y, bg.z, 1.0f};
        context->OMSetRenderTargets(1, &render_rtv, depth_dsv);
        context->ClearRenderTargetView(render_rtv, clear_color);
        context->ClearDepthStencilView(depth_dsv, D3D11_CLEAR_DEPTH, k_scene_depth_clear, 0);

        if (has_model() && scene_vertex_shader && scene_pixel_shader &&
            scene_input_layout && scene_constant_buffer &&
            scene_depth_state && rasterizer_state && sampler_state && blend_state) {
            D3D11_VIEWPORT viewport = {};
            viewport.Width = static_cast<float>(width);
            viewport.Height = static_cast<float>(height);
            viewport.MinDepth = 0.0f;
            viewport.MaxDepth = 1.0f;
            context->RSSetViewports(1, &viewport);
            context->RSSetState(rasterizer_state);
            context->OMSetDepthStencilState(scene_depth_state, 0);
            const float blend_factor[4] = {0.0f, 0.0f, 0.0f, 0.0f};
            context->OMSetBlendState(nullptr, blend_factor, 0xffffffff);

            Mat4 center_transform = translation(-center.x, -center.y, -center.z);
            Mat4 rotation = multiply(rotation_y(yaw), rotation_x(pitch));
            float distance = std::max(radius * distance_factor, radius + 0.1f);
            Mat4 model_transform = multiply(center_transform, rotation);
            Mat4 world = multiply(model_transform, translation(0.0f, 0.0f, -distance));
            Mat4 view = look_to_bve({0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, -1.0f}, {0.0f, 1.0f, 0.0f});
            float aspect = static_cast<float>(width) / std::max(1.0f, static_cast<float>(height));
            float near_z = std::max(0.001f, radius * 0.001f);
            float far_z = std::max(distance + radius * 4.0f, radius * 50.0f);
            Mat4 proj = perspective_fov_lh_reverse_z(k_model_preview_fov_y, aspect, near_z, far_z);
            Mat4 view_proj = multiply(view, proj);

            model_preview_instances.resize(1);
            model_preview_instances[0] = make_instance_data(world);
            if (!ensure_instance_buffer(model_preview_instance_buffer,
                                        model_preview_instance_capacity,
                                        model_preview_instances,
                                        error)) {
                if (last_error != error) last_error = error;
            } else {
                draw_scene_mesh(vertex_buffer, index_buffer, model_preview_instance_buffer,
                                parts, materials, 1, view_proj, rasterizer_state);
            }
        }

        ID3D11RenderTargetView* null_rtv = nullptr;
        context->OMSetRenderTargets(1, &null_rtv, nullptr);
    }

    void draw_overlay(ImDrawList* draw, ImVec2 origin, ImVec2 size) const {
        if (!draw || size.x <= 0.0f || size.y <= 0.0f) return;
        if (!has_model() || model_path_value.empty()) return;

        const float pad = std::max(4.0f, ImGui::GetStyle().FramePadding.x);
        const float rounding = 3.0f;
        const ImU32 text_color = IM_COL32(255, 255, 255, 240);
        const ImU32 bg_color = IM_COL32(0, 0, 0, 150);
        ImVec2 end(origin.x + size.x, origin.y + size.y);

        std::string file_name = display_name_from_path(model_path_value);
        ImVec2 name_size = ImGui::CalcTextSize(file_name.c_str());
        float max_name_width = std::max(0.0f, size.x - pad * 4.0f);
        if (max_name_width <= 1.0f) return;

        float visible_name_width = std::min(name_size.x, max_name_width);
        ImVec2 name_pos(origin.x + pad * 2.0f, end.y - pad * 2.0f - name_size.y);
        ImVec2 clip_min(name_pos.x, name_pos.y);
        ImVec2 clip_max(name_pos.x + visible_name_width, name_pos.y + name_size.y);
        draw->AddRectFilled(ImVec2(name_pos.x - pad, name_pos.y - pad * 0.5f),
                            ImVec2(name_pos.x + visible_name_width + pad, name_pos.y + name_size.y + pad * 0.5f),
                            bg_color, rounding);
        draw->PushClipRect(clip_min, clip_max, true);
        draw->AddText(name_pos, text_color, file_name.c_str());
        draw->PopClipRect();
    }

    void render(ImVec2 requested_size) {
        ImVec2 avail = requested_size;
        if (avail.x <= 0.0f || avail.y <= 0.0f) avail = ImGui::GetContentRegionAvail();
        avail.x = std::max(avail.x, 50.0f);
        avail.y = std::max(avail.y, 50.0f);

        ImVec2 origin = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton("ModelPreview3DCanvas", avail, ImGuiButtonFlags_MouseButtonLeft);
        bool hovered = ImGui::IsItemHovered();
        ImGuiIO& io = ImGui::GetIO();
        if (hovered && io.MouseWheel != 0.0f) {
            float factor = io.MouseWheel > 0.0f ? 0.88f : (1.0f / 0.88f);
            distance_factor = std::clamp(distance_factor * factor, 0.25f, 40.0f);
        }
        if (hovered && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            if (!rotating) {
                rotating = true;
                last_mouse = io.MousePos;
            } else {
                ImVec2 delta(io.MousePos.x - last_mouse.x, io.MousePos.y - last_mouse.y);
                yaw += delta.x * 0.01f;
                pitch = std::clamp(pitch + delta.y * 0.01f, -1.55334f, 1.55334f);
                last_mouse = io.MousePos;
            }
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
        } else {
            rotating = false;
        }

        int width = std::max(1, static_cast<int>(std::round(avail.x)));
        int height = std::max(1, static_cast<int>(std::round(avail.y)));
        render_scene(width, height);

        ImDrawList* draw = ImGui::GetWindowDrawList();
        ImVec2 end(origin.x + avail.x, origin.y + avail.y);
        if (render_srv) {
            draw->AddImage(reinterpret_cast<void*>(render_srv), origin, end);
        } else {
            draw->AddRectFilled(origin, end, IM_COL32(0, 0, 0, 255));
        }
        draw_overlay(draw, origin, avail);
    }

    void release_resources() {
        for (GpuMaterial& material : materials) {
            release_com(material.texture);
            material.has_texture = false;
            material.texture_has_alpha = false;
        }
        materials.clear();
        parts.clear();
        model_preview_instances.clear();
        release_com(model_preview_instance_buffer);
        model_preview_instance_capacity = 0;
        release_com(vertex_buffer);
        release_com(index_buffer);
        index_count = 0;
    }

    void release_render_target() {
        release_com(scene_pick_readback_texture);
        release_com(scene_pick_rtv);
        release_com(scene_pick_texture);
        release_com(scene_highlight_mask_srv);
        release_com(scene_highlight_mask_rtv);
        release_com(scene_highlight_mask_texture);
        release_com(render_srv);
        release_com(render_rtv);
        release_com(render_texture);
        release_com(depth_dsv);
        release_com(depth_texture);
        render_width = 0;
        render_height = 0;
    }

    Canvas3DWakeCallback wake_callback = nullptr;
    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* context = nullptr;
    ID3D11Texture2D* render_texture = nullptr;
    ID3D11RenderTargetView* render_rtv = nullptr;
    ID3D11ShaderResourceView* render_srv = nullptr;
    ID3D11Texture2D* depth_texture = nullptr;
    ID3D11DepthStencilView* depth_dsv = nullptr;
    ID3D11Texture2D* scene_pick_texture = nullptr;
    ID3D11RenderTargetView* scene_pick_rtv = nullptr;
    ID3D11Texture2D* scene_pick_readback_texture = nullptr;
    ID3D11Texture2D* scene_highlight_mask_texture = nullptr;
    ID3D11RenderTargetView* scene_highlight_mask_rtv = nullptr;
    ID3D11ShaderResourceView* scene_highlight_mask_srv = nullptr;
    ID3D11VertexShader* scene_vertex_shader = nullptr;
    ID3D11PixelShader* scene_pixel_shader = nullptr;
    ID3D11PixelShader* scene_fog_pixel_shader = nullptr;
    ID3D11InputLayout* scene_input_layout = nullptr;
    ID3D11Buffer* scene_constant_buffer = nullptr;
    ID3D11VertexShader* scene_marker_vertex_shader = nullptr;
    ID3D11PixelShader* scene_marker_pixel_shader = nullptr;
    ID3D11PixelShader* scene_marker_pick_pixel_shader = nullptr;
    ID3D11PixelShader* scene_marker_mask_pixel_shader = nullptr;
    ID3D11InputLayout* scene_marker_input_layout = nullptr;
    ID3D11Buffer* scene_marker_constant_buffer = nullptr;
    ID3D11Buffer* scene_marker_pick_constant_buffer = nullptr;
    ID3D11VertexShader* scene_outline_vertex_shader = nullptr;
    ID3D11PixelShader* scene_outline_pixel_shader = nullptr;
    ID3D11Buffer* scene_outline_constant_buffer = nullptr;
    ID3D11PixelShader* scene_pick_pixel_shader = nullptr;
    ID3D11Buffer* scene_pick_constant_buffer = nullptr;
    ID3D11SamplerState* sampler_state = nullptr;
    ID3D11SamplerState* scene_outline_sampler_state = nullptr;
    ID3D11DepthStencilState* scene_depth_state = nullptr;
    ID3D11DepthStencilState* scene_depth_read_state = nullptr;
    ID3D11RasterizerState* rasterizer_state = nullptr;
    ID3D11RasterizerState* alpha_mask_rasterizer_state = nullptr;
    ID3D11RasterizerState* track_rasterizer_state = nullptr;
    ID3D11BlendState* blend_state = nullptr;
    ID3D11Buffer* vertex_buffer = nullptr;
    ID3D11Buffer* index_buffer = nullptr;
    ID3D11Buffer* model_preview_instance_buffer = nullptr;
    UINT model_preview_instance_capacity = 0;
    std::vector<MeshPart> parts;
    std::vector<GpuMaterial> materials;
    std::vector<SceneInstanceData> model_preview_instances;
    UINT index_count = 0;
    int render_width = 0;
    int render_height = 0;
    Vec3 bounds_min;
    Vec3 bounds_max;
    Vec3 center;
    float radius = 1.0f;
    float yaw = 0.0f;
    float pitch = 0.0f;
    float distance_factor = 2.8f;
    bool rotating = false;
    ImVec2 last_mouse = ImVec2(0.0f, 0.0f);
    std::string model_path_value;
    std::vector<std::string> model_load_warnings;
    ImVec4 background_color_value = ImVec4(0.0f, 0.0f, 0.0f, 1.0f);
    std::string last_error;
    Canvas3DScene scene_data;
    size_t scene_geometry_generation = 0;
    bool scene_active = false;
    bool scene_fog_enabled = true;
    bool scene_map_draw_distance_enabled = true;
#ifndef NDEBUG
    size_t debug_scene_fog_draw_part_count = 0;
#endif
    std::vector<SceneChunk> scene_chunks;
    std::unordered_map<std::string, ScenePlacementInstanceLocation> scene_placement_locations;
    std::unordered_map<std::string, size_t> scene_repeater_locations;
    SceneStructureEditState scene_structure_edit;
    float scene_edit_component_scale = 1.0f;
    std::vector<SceneTrackChunkGpu> scene_track_chunks;
    std::vector<SceneMileagePickPoint> scene_mileage_pick_points;
    ID3D11Buffer* scene_mileage_highlight_vertex_buffer = nullptr;
    ID3D11Buffer* scene_mileage_highlight_index_buffer = nullptr;
    ID3D11Buffer* scene_mileage_highlight_instance_buffer = nullptr;
    UINT scene_mileage_highlight_instance_capacity = 0;
    std::vector<MeshPart> scene_mileage_highlight_parts;
    std::vector<GpuMaterial> scene_mileage_highlight_materials;
    std::vector<SceneInstanceData> scene_mileage_highlight_instances;
    // Static marker vertices/UVs are keyed by scene_data.markers, the 100 m
    // scene chunk layout, and the active ImGui font/atlas identity below.
    // Visibility is view-only state and rewrites only each chunk's dynamic IBs.
    std::vector<SceneMarkerChunkGpu> scene_marker_chunks;
    std::vector<SceneMarkerGpuLocation> scene_marker_locations;
    std::unordered_map<std::string, size_t> scene_sound3d_marker_indices;
    std::array<std::vector<size_t>, k_scene_marker_list_kind_count> scene_marker_target_indices;
    Canvas3DSceneMarkerVisibility scene_marker_visibility;
    ImFont* scene_marker_font = nullptr;
    float scene_marker_font_size = 0.0f;
    ImTextureID scene_marker_font_texture_id = ImTextureID_Invalid;
    int scene_marker_font_texture_unique_id = -1;
    int scene_marker_font_texture_width = 0;
    int scene_marker_font_texture_height = 0;
    std::map<std::string, SceneModelGpu> scene_models;
    std::unordered_set<std::string> scene_put_between_preview_base_model_keys;
    std::unordered_map<std::string, SceneTextureCacheEntry> scene_texture_cache;
    std::unordered_set<std::string> scene_texture_warning_keys;
    size_t scene_model_worker_limit = 0;
    bool scene_texture_cache_enabled = true;
    std::mutex scene_upload_mutex;
    std::vector<CpuModelData> scene_pending_uploads;
    std::atomic<bool> scene_wake_pending{false};
    std::mutex scene_log_mutex;
    std::vector<std::string> scene_pending_logs;
    bool scene_load_summary_pending = false;
    std::chrono::steady_clock::time_point scene_model_load_started_at{};
    bool scene_model_load_timer_active = false;
    std::thread scene_worker;
    std::atomic<bool> scene_cancel{false};
    std::atomic<bool> scene_worker_running{false};
    std::atomic<size_t> scene_model_worker_count_value{0};
    std::thread scene_put_between_preview_worker;
    std::mutex scene_put_between_preview_mutex;
    std::condition_variable scene_put_between_preview_cv;
    bool scene_put_between_preview_stop = false;
    std::optional<ScenePutBetweenPreviewJob> scene_put_between_preview_pending;
    std::optional<ScenePutBetweenPreviewResult> scene_put_between_preview_completed;
    std::uint64_t scene_put_between_preview_next_sequence = 0;
    std::uint64_t scene_put_between_preview_latest_sequence = 0;
    DVec3 scene_camera_pos;
    float scene_camera_yaw = 0.0f;
    float scene_camera_pitch = 0.0f;
    double scene_camera_distance = 0.0;
    double scene_camera_lateral_offset = 0.0;
    float scene_camera_vertical_offset = 2.0f;
    float scene_camera_yaw_offset = 0.0f;
    bool scene_rotating = false;
    ImVec2 scene_last_mouse = ImVec2(0.0f, 0.0f);
    Canvas3DSceneInteractionMode scene_interaction_mode = Canvas3DSceneInteractionMode::Move;
    int scene_hovered_object_index = -1;
    int scene_hovered_marker_index = -1;
    std::optional<double> scene_hovered_mileage;
    std::optional<double> scene_context_mileage;
    int scene_context_object_index = -1;
    int scene_context_marker_index = -1;
    SceneHighlightBatch scene_hover_highlight_batch;
    SceneHighlightBatch scene_focus_highlight_batch;
    int scene_focus_highlight_object_index = -1;
    int scene_focus_highlight_marker_index = -1;
    std::string scene_focus_highlight_model_path;
    double scene_focus_highlight_world[16] = {
        1.0, 0.0, 0.0, 0.0,
        0.0, 1.0, 0.0, 0.0,
        0.0, 0.0, 1.0, 0.0,
        0.0, 0.0, 0.0, 1.0
    };
    std::chrono::steady_clock::time_point scene_focus_highlight_until{};
    double scene_chunk_m = 100.0;
    double scene_window_back_m = 100.0;
    double scene_window_forward_m = 1200.0;
    float scene_slow_speed_mps = 8.0f;
    float scene_fast_multiplier = 10.0f;
    int scene_camera_speed_percent = 100;
    bool scene_performance_warning_enabled = true;
    size_t scene_instance_warning_threshold = 3000;
    size_t scene_instance_critical_warning_threshold = 5000;
    std::string scene_last_error;
    Canvas3DSceneStats scene_stats_value;
    std::chrono::steady_clock::time_point scene_fps_last_frame_at{};
    bool scene_fps_last_frame_valid = false;
    float scene_fps_value = 0.0f;
#ifndef NDEBUG
    std::atomic<int> debug_copy_cpu_model_throw_countdown{0};
#endif
    ModelLoaderClient loader;
    ModelLoaderClient scene_loader;
};

Canvas3D::Canvas3D(ID3D11Device* device, Canvas3DWakeCallback wake_callback)
    : impl_(std::make_unique<Impl>(device, wake_callback)) {
}

Canvas3D::~Canvas3D() = default;

bool Canvas3D::load_model(const std::string& path, std::string& error) {
    return impl_->load_model(path, error);
}

bool Canvas3D::reload_model(std::string& error) {
    return impl_->reload_model(error);
}

std::vector<std::string> Canvas3D::drain_model_load_warnings() {
    return impl_->drain_model_load_warnings();
}

void Canvas3D::clear_model() {
    impl_->clear_model();
}

bool Canvas3D::has_model() const {
    return impl_->has_model();
}

const std::string& Canvas3D::model_path() const {
    return impl_->model_path_value;
}

void Canvas3D::set_background_color(ImVec4 color) {
    impl_->background_color_value = clamp_theme_color(color);
}

void Canvas3D::render(ImVec2 size) {
    impl_->render(size);
}

bool Canvas3D::load_scene(Canvas3DScene scene, std::string& error,
                          bool preserve_loaded_models,
                          bool preserve_camera) {
    return impl_->load_scene(std::move(scene), error, preserve_loaded_models, preserve_camera);
}

bool Canvas3D::refresh_scene_dynamic_content(const MapModel& model, int station_index, std::string& error) {
    return impl_->refresh_scene_dynamic_content(model, station_index, error);
}

bool Canvas3D::set_scene_marker_visibility(
    const Canvas3DSceneMarkerVisibility& visibility,
    std::string& error) {
    return impl_->set_scene_marker_visibility(visibility, error);
}

bool Canvas3D::refresh_scene_route_stations(
    const MapModel& model,
    std::string& error) {
    return impl_->refresh_scene_route_stations(model, error);
}

bool Canvas3D::refresh_scene_map_content(
    const MapModel& model,
    const Canvas3DSceneMapRefreshOptions& options,
    std::string& error) {
    return impl_->refresh_scene_map_content(model, options, error);
}

void Canvas3D::clear_scene() {
    impl_->clear_scene();
}

bool Canvas3D::has_scene() const {
    return impl_->has_scene();
}

bool Canvas3D::reload_scene_models(std::string& error) {
    return impl_->reload_scene_models(error);
}

bool Canvas3D::set_scene_track_visibility(const std::vector<Canvas3DTrackVisibility>& visibility, std::string& error) {
    return impl_->set_scene_track_visibility(visibility, error);
}

void Canvas3D::set_scene_window(double back_m, double forward_m) {
    impl_->set_scene_window(back_m, forward_m);
}

void Canvas3D::set_scene_edit_component_scale(float scale) {
    impl_->set_scene_edit_component_scale(scale);
}

void Canvas3D::set_scene_interaction_mode(Canvas3DSceneInteractionMode mode) {
    impl_->set_scene_interaction_mode(mode);
}

void Canvas3D::set_scene_fog_enabled(bool enabled) {
    impl_->set_scene_fog_enabled(enabled);
}

void Canvas3D::set_scene_map_draw_distance_enabled(bool enabled) {
    impl_->set_scene_map_draw_distance_enabled(enabled);
}

void Canvas3D::set_scene_camera_speed_percent(int percent) {
    impl_->set_scene_camera_speed_percent(percent);
}

void Canvas3D::set_scene_performance_warning(bool enabled,
                                             size_t warning_threshold,
                                             size_t critical_warning_threshold) {
    impl_->set_scene_performance_warning(
        enabled, warning_threshold, critical_warning_threshold);
}

Canvas3DSceneInteractionMode Canvas3D::scene_interaction_mode() const {
    return impl_->scene_interaction_mode_value();
}

Canvas3DSceneStats Canvas3D::scene_stats() const {
    return impl_->scene_stats();
}

void Canvas3D::process_scene_loading() {
    impl_->upload_pending_scene_models();
}

#ifndef NDEBUG
void Canvas3D::set_debug_scene_loading_tuning(size_t worker_limit, bool texture_cache_enabled) {
    impl_->scene_model_worker_limit = worker_limit;
    impl_->scene_texture_cache_enabled = texture_cache_enabled;
}

Canvas3DSceneLoaderContractResult Canvas3D::debug_run_scene_loader_contract(
    const std::string& valid_model_path) {
    return impl_->debug_run_scene_loader_contract(valid_model_path);
}

Canvas3DSceneFogDebugState Canvas3D::debug_scene_fog_state() const {
    return impl_->debug_scene_fog_state();
}

bool Canvas3D::debug_read_scene_render_pixels(std::vector<std::uint8_t>& rgba,
                                              int& width, int& height,
                                              std::string& error) {
    return impl_->debug_read_scene_render_pixels(rgba, width, height, error);
}
#endif

std::vector<std::string> Canvas3D::drain_scene_load_messages() {
    return impl_->drain_scene_load_messages();
}

Canvas3DSceneCameraPose Canvas3D::scene_camera_pose() const {
    return impl_->scene_camera_pose();
}

bool Canvas3D::jump_scene_camera_to_distance(double distance) {
    return impl_->reset_scene_camera_pose_at_distance(distance);
}

bool Canvas3D::jump_scene_camera_to_object(Canvas3DSceneObjectKind kind, size_t source_row) {
    return impl_->jump_scene_camera_to_object(kind, source_row);
}

bool Canvas3D::jump_scene_camera_to_marker(Canvas3DSceneMarkerListKind list_kind,
                                            size_t row_index) {
    return impl_->jump_scene_camera_to_marker(list_kind, row_index);
}

bool Canvas3D::set_scene_placement_edit_target(const Canvas3DPlacementEditTarget& target,
                                               bool show_gizmo) {
    return impl_->set_scene_placement_edit_target(target, show_gizmo);
}

bool Canvas3D::update_scene_placement_instance(const Canvas3DPlacementEditTarget& target) {
    return impl_->update_scene_placement_instance(target);
}

bool Canvas3D::set_scene_repeater_edit_target(const Canvas3DPlacementEditTarget& target,
                                               bool show_gizmo) {
    return impl_->set_scene_repeater_edit_target(target, show_gizmo);
}

bool Canvas3D::update_scene_repeater_segment(const Canvas3DPlacementEditTarget& target) {
    return impl_->update_scene_repeater_segment(target);
}

void Canvas3D::clear_scene_placement_edit_target() {
    impl_->clear_scene_placement_edit_target();
}

Canvas3DSceneFrameResult Canvas3D::render_scene_preview(
    ImVec2 size,
    const Canvas3DSceneUiText& ui_text,
    const Canvas3DSceneContextMenuOptions& context_menu_options) {
    return impl_->render_scene_preview(size, ui_text, context_menu_options);
}
