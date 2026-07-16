/*
 * Copyright (c) 2026 Sapporo_ningyo
 *
 * Licensed under Apache License 2.0; see LICENSE and NOTICE.
 */

#pragma execution_character_set("utf-8")

#include "canvas3D.h"

#include "kme.h"
#include "maploader.h"
#include "model_loader.h"
#include "runtime_paths.h"
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
#include <cstdio>
#include <cstring>
#include <map>
#include <filesystem>
#include <mutex>
#include <limits>
#include <optional>
#include <set>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

constexpr float kDefaultSceneCameraHeight = 2.0f;
constexpr double kSceneRepeaterDistanceEpsilon = 1e-6;
constexpr long long kSceneRepeaterInstanceLimit = 1000000;
constexpr float kSceneCameraFovY = 0.6108652382f;
constexpr float kSceneNearZ = 0.2f;
constexpr float kSceneBackgroundNearZ = 0.05f;
constexpr float kSceneBackgroundFarZ = 5000.0f;
constexpr float kSceneDepthClear = 0.0f;
constexpr float kMaterialOpaqueAlphaThreshold = 0.98f;
constexpr float kSceneTrackMarkerWidth = 0.5f;
constexpr float kSceneTrackMarkerAlpha = 0.8f;
constexpr float kSceneSelectionMinScreenRadius = 6.0f;
constexpr float kSceneSelectionHitPadding = 4.0f;
constexpr float kSceneHighlightOutlineWidthPx = 5.0f;
constexpr float kModelPreviewFovY = 0.78539816339f;
constexpr double kSceneObjectJumpBackM = 25.0;
constexpr double kSceneFocusHighlightSeconds = 3.0;
// The event-driven canvas stops repainting while idle, so preserve the last active FPS across long gaps.
constexpr double kSceneFpsIdleResetSeconds = 0.25;
constexpr float kSceneFpsSmoothing = 0.15f;
constexpr float kSceneGizmoLengthPx = 72.0f;
constexpr float kSceneGizmoHitRadiusPx = 8.0f;

template <typename T>
void release_com(T*& p) {
    if (p) {
        p->Release();
        p = nullptr;
    }
}

std::wstring utf8_to_wide_local(const std::string& text) {
    if (text.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (n <= 0) return {};
    std::wstring out(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), n);
    return out;
}

std::string wide_to_utf8_local(const std::wstring& text) {
    if (text.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    if (n <= 0) return {};
    std::string out(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), n, nullptr, nullptr);
    return out;
}

std::string path_filename_utf8(const std::string& path) {
    try {
        std::filesystem::path p = utf8_to_wide_local(path);
#if defined(__cpp_char8_t)
        auto s = p.filename().u8string();
        std::string name(reinterpret_cast<const char*>(s.data()), s.size());
#else
        std::string name = p.filename().u8string();
#endif
        return name.empty() ? path : name;
    } catch (...) {
        return path;
    }
}

float clamp_color_component(float value) {
    if (!std::isfinite(value)) return 0.0f;
    return std::clamp(value, 0.0f, 1.0f);
}

float normalize_material_alpha(float value) {
    float alpha = clamp_color_component(value);
    // BVE .x models commonly use 0.99/0.999999 for opaque alpha-tested textures.
    return alpha >= kMaterialOpaqueAlphaThreshold ? 1.0f : alpha;
}

ImVec4 clamp_background_color(ImVec4 color) {
    color.x = clamp_color_component(color.x);
    color.y = clamp_color_component(color.y);
    color.z = clamp_color_component(color.z);
    color.w = 1.0f;
    return color;
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

int scene_object_index_from_pick_rgba(const unsigned char* rgba) {
    if (!rgba) return -1;
    const unsigned int id = static_cast<unsigned int>(rgba[0]) |
        (static_cast<unsigned int>(rgba[1]) << 8) |
        (static_cast<unsigned int>(rgba[2]) << 16);
    if (id == 0) return -1;
    return static_cast<int>(id - 1u);
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
    return wide_to_utf8_local(text);
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

struct Mat4 {
    float m[4][4] = {};
};

Mat4 identity() {
    Mat4 r;
    for (int i = 0; i < 4; ++i) r.m[i][i] = 1.0f;
    return r;
}

Mat4 mat4_from_array(const float values[16]) {
    Mat4 r;
    for (int row = 0; row < 4; ++row) {
        for (int col = 0; col < 4; ++col) {
            r.m[row][col] = values[row * 4 + col];
        }
    }
    return r;
}

void mat4_to_array(const Mat4& m, float values[16]) {
    for (int row = 0; row < 4; ++row) {
        for (int col = 0; col < 4; ++col) {
            values[row * 4 + col] = m.m[row][col];
        }
    }
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

Mat4 rotation_z(float angle) {
    Mat4 r = identity();
    float c = std::cos(angle);
    float s = std::sin(angle);
    r.m[0][0] = c;
    r.m[0][1] = s;
    r.m[1][0] = -s;
    r.m[1][1] = c;
    return r;
}

Mat4 structure_basis(Vec3 right, Vec3 up, Vec3 forward, Vec3 origin) {
    Mat4 r = identity();
    r.m[0][0] = right.x;
    r.m[0][1] = right.y;
    r.m[0][2] = right.z;
    r.m[1][0] = up.x;
    r.m[1][1] = up.y;
    r.m[1][2] = up.z;
    r.m[2][0] = forward.x;
    r.m[2][1] = forward.y;
    r.m[2][2] = forward.z;
    r.m[3][0] = origin.x;
    r.m[3][1] = origin.y;
    r.m[3][2] = origin.z;
    return r;
}

Mat4 look_at_lh(Vec3 eye, Vec3 target, Vec3 up) {
    Vec3 zaxis = normalize(target - eye);
    Vec3 xaxis = normalize(cross(up, zaxis));
    Vec3 yaxis = cross(zaxis, xaxis);

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

Mat4 perspective_fov_lh(float fovy, float aspect, float zn, float zf) {
    Mat4 r;
    float y_scale = 1.0f / std::tan(fovy * 0.5f);
    float x_scale = y_scale / std::max(aspect, 0.001f);
    r.m[0][0] = x_scale;
    r.m[1][1] = y_scale;
    r.m[2][2] = zf / (zf - zn);
    r.m[2][3] = 1.0f;
    r.m[3][2] = -zn * zf / (zf - zn);
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

struct SceneViewConstants {
    Mat4 view_proj;
    float material_color[4];
    float use_texture[4];
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

struct SceneModelGpu {
    enum class State { Pending, Ready, Failed };

    State state = State::Pending;
    ID3D11Buffer* vertex_buffer = nullptr;
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

struct SceneStructureInstanceLocation {
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
    double world_units_per_pixel = 0.0;
};

struct SceneStructureEditState {
    bool active = false;
    bool show_gizmo = false;
    std::string edit_id;
    Canvas3DModelInstance baseline_instance;
    Canvas3DStructureEditTarget current;
    DVec3 origin;
    std::array<DVec3, 3> axes{};
    std::array<SceneGizmoAxisProjection, 3> projection{};
    Canvas3DSceneDragAxis hovered_axis = Canvas3DSceneDragAxis::None;
    Canvas3DSceneDragAxis dragging_axis = Canvas3DSceneDragAxis::None;
    DVec3 drag_axis_origin;
    DVec3 drag_axis_direction;
    ImVec2 drag_start_mouse;
    ImVec2 drag_screen_direction;
    double drag_start_value = 0.0;
    double drag_start_axis_parameter = 0.0;
    double drag_world_units_per_pixel = 0.0;
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

double angle_lerp(double a, double b, double t) {
    double delta = std::atan2(std::sin(static_cast<double>(b) - static_cast<double>(a)),
                              std::cos(static_cast<double>(b) - static_cast<double>(a)));
    return a + delta * t;
}

std::string normalize_scene_track_key(std::string key) {
    key.erase(std::remove_if(key.begin(), key.end(), [](unsigned char ch) {
        return std::isspace(ch) != 0;
    }), key.end());
    for (char& ch : key) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return key;
}

bool is_scene_own_track_alias(const std::string& normalized_key) {
    return normalized_key.empty() || normalized_key == "0" || normalized_key == "1" ||
        normalized_key == "\\" || normalized_key == "own" || normalized_key == "main";
}

bool is_scene_own_track_placement_key(const std::string& normalized_key) {
    // BVE Structure/Repeater placement uses empty trackKey or trackKey 0 for the own track.
    return normalized_key.empty() || normalized_key == "0";
}

int scene_tilt_flags(double tilt) {
    if (!std::isfinite(tilt)) return 0;
    return static_cast<int>(tilt);
}

bool scene_material_is_translucent(const GpuMaterial* material) {
    return material && material->diffuse[3] < kMaterialOpaqueAlphaThreshold;
}

bool scene_material_uses_alpha_mask(const GpuMaterial* material) {
    return material && material->has_texture && material->texture_has_alpha && !scene_material_is_translucent(material);
}

bool scene_repeater_has_interval(const Canvas3DRepeaterSegment& repeater) {
    return repeater.interval > 1e-9 && std::isfinite(repeater.interval);
}

double scene_repeater_index_epsilon(const Canvas3DRepeaterSegment& repeater) {
    if (!scene_repeater_has_interval(repeater)) return kSceneRepeaterDistanceEpsilon;
    return std::min(kSceneRepeaterDistanceEpsilon, repeater.interval * 0.25);
}

bool scene_repeater_index_range(const Canvas3DRepeaterSegment& repeater,
                                double range_min,
                                double range_max,
                                SceneRepeaterIndexRange& out) {
    out = {};
    if (!scene_repeater_has_interval(repeater) || repeater.end_distance < repeater.begin_distance) return false;
    const double begin = std::max(range_min, repeater.begin_distance);
    const double end = std::min(range_max, repeater.end_distance);
    if (end < begin - kSceneRepeaterDistanceEpsilon) return false;

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
    return static_cast<size_t>(std::min<double>(count, static_cast<double>(kSceneRepeaterInstanceLimit)));
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

std::string trim_scene_ascii(const std::string& text) {
    size_t first = 0;
    while (first < text.size() && std::isspace(static_cast<unsigned char>(text[first]))) ++first;
    size_t last = text.size();
    while (last > first && std::isspace(static_cast<unsigned char>(text[last - 1]))) --last;
    return text.substr(first, last - first);
}

std::string scene_model_key(std::string key) {
    key = trim_scene_ascii(key);
    for (char& ch : key) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return key;
}

Matrix copy_scene_buffer(KvDoubleBuffer buffer) {
    Matrix m;
    m.rows = buffer.rows;
    m.cols = buffer.cols;
    if (buffer.data && buffer.rows > 0 && buffer.cols > 0) {
        m.data.assign(buffer.data, buffer.data + buffer.rows * buffer.cols);
    }
    return m;
}

double scene_matrix_track_tangent(const Matrix& points, size_t row) {
    if (points.rows < 2 || points.cols < 3) return 0.0;
    size_t a = row == 0 ? 0 : row - 1;
    size_t b = row + 1 < points.rows ? row + 1 : row;
    if (a == b && b + 1 < points.rows) ++b;
    if (a == b) return 0.0;
    double dx = points.at(b, 1) - points.at(a, 1);
    double dy = points.at(b, 2) - points.at(a, 2);
    if (std::abs(dx) < 1e-9 && std::abs(dy) < 1e-9) return 0.0;
    return std::atan2(dy, dx);
}

Canvas3DTrackPoint scene_matrix_row_point(const Matrix& points, size_t row, bool has_theta_column) {
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
    p.theta = has_theta_column && points.cols > 4 ? points.at(row, 4) : scene_matrix_track_tangent(points, row);
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
            std::string key = trim_scene_ascii(current);
            if (!key.empty()) keys.push_back(key);
            current.clear();
        } else {
            current.push_back(ch);
        }
    }
    std::string key = trim_scene_ascii(current);
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
        if (normalize_scene_track_key(path.key) == normalized_key) return &path;
    }
    return nullptr;
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
    append_scene_model_key_field(key, normalize_scene_track_key(instance.put_between_track_key1));
    append_scene_model_key_field(key, normalize_scene_track_key(instance.put_between_track_key2));
    key.append(reinterpret_cast<const char*>(&instance.distance), sizeof(instance.distance));
    const int flag = instance.put_between_flag & 1;
    key.append(reinterpret_cast<const char*>(&flag), sizeof(flag));
    key.append(reinterpret_cast<const char*>(&geometry_generation), sizeof(geometry_generation));
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

CpuModelData derive_put_between_model(const CpuModelData& source,
                                      const SceneModelLoadRequest& request) {
    CpuModelData result;
    result.path = request.source_path;
    result.scene_key = request.key;
    result.shared_model_key = request.source_path;
    if (!source.ok) {
        result.error = source.error;
        return result;
    }
    if (!request.put_between.own_track || !request.put_between.track1 || !request.put_between.track2) {
        result.error = "PutBetween references an unavailable track";
        return result;
    }
    if (source.vertices.empty()) {
        result.error = "PutBetween model contains no vertices";
        return result;
    }

    double average_x = 0.0;
    for (const GpuVertex& vertex : source.vertices) average_x += static_cast<double>(vertex.px);
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
    const double max_float = static_cast<double>(std::numeric_limits<float>::max());

    result.vertices = source.vertices;
    for (GpuVertex& vertex : result.vertices) {
        const double original_x = static_cast<double>(vertex.px);
        const double vertex_distance = request.put_between.distance - static_cast<double>(vertex.pz);
        const bool track1_side = original_x * orientation < split_x;
        const Canvas3DTrackPath& selected_track =
            track1_side ? *request.put_between.track1 : *request.put_between.track2;
        auto own_point = scene_sample_track_path_points(*request.put_between.own_track, vertex_distance);
        auto selected_point = scene_sample_track_path_points(selected_track, vertex_distance);
        if (!own_point || !selected_point) {
            result.vertices.clear();
            result.error = "PutBetween could not sample track geometry";
            return result;
        }

        const double residual_x = track1_side ? original_x : original_x - opposite_edge_offset;
        DVec3 anchor{selected_point->x, selected_point->y, selected_point->z};
        if ((request.put_between.flag & 1) != 0) anchor.y = own_point->y;

        const double gradient = std::isfinite(own_point->gradient) ? own_point->gradient / 1000.0 : 0.0;
        const DVec3 right = right_from_theta_d(own_point->theta);
        const DVec3 forward = normalize(DVec3{
            std::sin(own_point->theta),
            gradient,
            -std::cos(own_point->theta)
        });
        const DVec3 up = normalize(cross(right, forward));
        const DVec3 world_position =
            anchor + right * residual_x + up * static_cast<double>(vertex.py);
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

        const DVec3 source_normal{vertex.nx, vertex.ny, vertex.nz};
        if (dot(source_normal, source_normal) > 1e-12) {
            const DVec3 world_normal = normalize(
                right * source_normal.x + up * source_normal.y + forward * -source_normal.z);
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
        std::string normalized_key = normalize_scene_track_key(key);
        if (is_scene_own_track_placement_key(normalized_key)) {
            return scene_sample_track_path_points(*own_path, distance);
        }
        if (const Canvas3DTrackPath* other = scene_other_track_path_for_key(scene, normalized_key)) {
            return scene_sample_track_path_points(*other, distance);
        }
        return std::nullopt;
    };

    std::map<std::string, std::string> model_paths;
    for (const TableRow& row : model.structure_models) {
        std::string key = scene_model_key(table_cell(row, "structureKey"));
        std::string path = table_cell(row, "filePath");
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
            std::string structure_key = trim_scene_ascii(table_cell(row, "structureKey" + std::to_string(key_index)));
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
        Canvas3DSceneObject object;
        object.kind = Canvas3DSceneObjectKind::Structure;
        object.source_row = source_row;
        object.label = table_cell(row, "structureKey");
        object.edit_id = row.edit_id;
        const int object_index = static_cast<int>(scene.objects.size());
        scene.objects.push_back(std::move(object));

        Canvas3DModelInstance instance;
        instance.model_path = path;
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
        scene.instances.push_back(std::move(instance));
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
        Canvas3DSceneObject object;
        object.kind = Canvas3DSceneObjectKind::Structure;
        object.source_row = model.structures.size() + between_index;
        object.structure_put_between = true;
        object.label = table_cell(row, "structureKey");
        object.edit_id = row.edit_id;
        const int object_index = static_cast<int>(scene.objects.size());
        scene.objects.push_back(std::move(object));

        Canvas3DModelInstance instance;
        instance.model_path = path;
        instance.distance = distance;
        instance.object_index = object_index;
        instance.put_between = true;
        instance.put_between_track_key1 = table_cell(row, "trackKey1");
        instance.put_between_track_key2 = table_cell(row, "trackKey2");
        instance.put_between_flag = static_cast<int>(table_cell_number(row, "flag")) & 1;
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

        Canvas3DSceneObject object;
        object.kind = Canvas3DSceneObjectKind::Signal;
        object.source_row = row_index;
        object.label = table_cell(row, "signalAspectKey");
        object.edit_id = row.edit_id;
        object.model_options = options;
        object.selected_model_option = static_cast<size_t>(selected_it - options.begin());

        const int object_index = static_cast<int>(scene.objects.size());
        scene.objects.push_back(std::move(object));

        Canvas3DModelInstance instance;
        instance.model_path = scene.objects.back().model_options[scene.objects.back().selected_model_option].model_path;
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
        scene.instances.push_back(std::move(instance));
    };
    for (size_t row_index = 0; row_index < model.signals.size(); ++row_index) {
        append_signal_instance(model.signals[row_index], row_index);
    }

    struct RepeaterBegin {
        size_t row_index = 0;
        double distance = 0.0;
        std::string key;
        std::string track_key;
        std::string edit_id;
        double x = 0.0;
        double y = 0.0;
        double z = 0.0;
        double rx = 0.0;
        double ry = 0.0;
        double rz = 0.0;
        double tilt = 0.0;
        double span = 0.0;
        double interval = 0.0;
        std::vector<std::string> model_paths;
    };
    std::vector<TableRow> repeater_events = model.repeaters;
    std::stable_sort(repeater_events.begin(), repeater_events.end(), repeater_event_distance_order_less);
    std::map<std::string, RepeaterBegin> active_repeaters;
    size_t repeater_row_index = 0;
    auto emit_repeater = [&](const RepeaterBegin& begin,
                             double end_distance,
                             bool has_end_or_change_position) {
        if (end_distance < begin.distance) return;
        Canvas3DRepeaterSegment segment;
        segment.track_key = begin.track_key;
        segment.begin_distance = begin.distance;
        segment.end_distance = end_distance;
        segment.has_end_or_change_position = has_end_or_change_position;
        segment.interval = begin.interval;
        segment.x = begin.x;
        segment.y = begin.y;
        segment.z = begin.z;
        segment.rx = begin.rx;
        segment.ry = begin.ry;
        segment.rz = begin.rz;
        segment.tilt = begin.tilt;
        segment.span = begin.span;
        for (const std::string& path : begin.model_paths) {
            if (!path.empty()) segment.model_paths.push_back(path);
        }
        if (segment.model_paths.empty()) return;

        Canvas3DSceneObject object;
        object.kind = Canvas3DSceneObjectKind::Repeater;
        object.source_row = begin.row_index;
        object.label = begin.key;
        object.edit_id = begin.edit_id;
        segment.object_index = static_cast<int>(scene.objects.size());
        scene.objects.push_back(std::move(object));
        scene.repeaters.push_back(std::move(segment));
    };
    for (const TableRow& row : repeater_events) {
        std::string key = scene_model_key(table_cell(row, "repeaterKey"));
        if (key.empty()) continue;
        std::string method = table_cell(row, "method");
        double distance = table_cell_number(row, "distance");
        if (method == "Begin" || method == "Begin0") {
            auto existing = active_repeaters.find(key);
            if (existing != active_repeaters.end()) {
                emit_repeater(existing->second, distance, true);
                active_repeaters.erase(existing);
            }
            RepeaterBegin begin;
            begin.row_index = repeater_row_index++;
            begin.distance = distance;
            begin.key = key;
            begin.track_key = table_cell(row, "trackKey");
            begin.edit_id = row.edit_id;
            begin.x = table_cell_number(row, "x");
            begin.y = table_cell_number(row, "y");
            begin.z = table_cell_number(row, "z");
            begin.rx = table_cell_number(row, "rx");
            begin.ry = table_cell_number(row, "ry");
            begin.rz = table_cell_number(row, "rz");
            begin.tilt = table_cell_number(row, "tilt");
            begin.span = table_cell_number(row, "span");
            begin.interval = table_cell_number(row, "interval");
            for (const std::string& structure_key : scene_split_key_list(table_cell(row, "structureKeys"))) {
                begin.model_paths.push_back(model_path_for_key(structure_key));
            }
            active_repeaters[key] = std::move(begin);
        } else if (method == "End") {
            auto existing = active_repeaters.find(key);
            if (existing == active_repeaters.end()) continue;
            emit_repeater(existing->second, distance, true);
            active_repeaters.erase(existing);
        }
    }
    for (const auto& kv : active_repeaters) {
        emit_repeater(kv.second, scene.max_distance, false);
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
    scene.camera.y = camera_point->y + kDefaultSceneCameraHeight;
    scene.camera.z = camera_point->z;
    scene.camera.yaw = camera_point->theta;
    scene.camera.pitch = 0.0f;
    return true;
}

const char* kSceneShaderSource = R"(
cbuffer SceneViewConstants : register(b0)
{
    row_major float4x4 viewProj;
    float4 materialColor;
    float4 useTexture;
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
    float2 texcoord : TEXCOORD0;
};

VSOutput vs_main(VSInput input)
{
    float4x4 world = float4x4(input.world0, input.world1, input.world2, input.world3);
    VSOutput output;
    output.position = mul(mul(float4(input.position, 1.0), world), viewProj);
    output.texcoord = input.texcoord;
    return output;
}

float4 ps_main(VSOutput input) : SV_TARGET
{
    float4 color = materialColor;
    if (useTexture.x > 0.5)
        color *= diffuseTexture.Sample(diffuseSampler, input.texcoord);
    clip(color.a - 0.1);
    if (useTexture.y > 0.5)
        color.a = 1.0;
    return color;
}
)";

const char* kScenePickShaderSource = R"(
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

const char* kSceneHighlightOutlineShaderSource = R"(
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

    bool load(const std::string& path, MlMeshData& data, std::string& error) {
        if (!ensure_loaded(error)) return false;
        if (!load_model_(path.c_str(), &data)) {
            const char* loader_error = get_last_error_ ? get_last_error_() : nullptr;
            error = loader_error && *loader_error ? loader_error : "model_loader.dll could not load the model";
            return false;
        }
        return true;
    }

    void free_model(MlMeshData& data) {
        if (free_model_) free_model_(&data);
        else data = {};
    }

private:
    bool ensure_loaded(std::string& error) {
        if (library_) return true;

        DWORD first_error = ERROR_SUCCESS;
        library_ = runtime_paths::load_dll(L"model_loader.dll", &first_error);
        if (!library_) {
            error = "bin/model_loader.dll load failed: " + win32_error_text(first_error);
            return false;
        }

        api_version_ = reinterpret_cast<MlApiVersionFn>(GetProcAddress(library_, "ml_api_version"));
        load_model_ = reinterpret_cast<MlLoadModelFn>(GetProcAddress(library_, "ml_load_model"));
        free_model_ = reinterpret_cast<MlFreeModelFn>(GetProcAddress(library_, "ml_free_model"));
        get_last_error_ = reinterpret_cast<MlGetLastErrorFn>(GetProcAddress(library_, "ml_get_last_error"));
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
};

} // namespace

Canvas3DSceneBuildResult build_canvas3d_scene_preview(const Canvas3DSceneBuildOptions& options) {
    Canvas3DSceneBuildResult result;
    Canvas3DScene& scene = result.scene;
    if (!options.model || options.model->own.empty()) return result;

    const MapModel& model = *options.model;
    Matrix scene_own = model.own;
    std::vector<OtherTrack> scene_other_tracks = model.other_tracks;
    if (options.map_handle) {
        const double max_step = std::clamp(
            options.control_point_interval > 0.0 ? options.control_point_interval : 25.0,
            1.0,
            25.0);
        bool scene_geometry_ok =
            kv_generate_scene_geometry(options.map_handle, options.unit_distance, 1.0, max_step, 1.0, 0.01) != 0;
        if (scene_geometry_ok) {
            Matrix dense_own = copy_scene_buffer(kv_get_owntrack_buffer(options.map_handle));
            std::vector<OtherTrack> dense_other_tracks;
            dense_other_tracks.reserve(model.other_tracks.size());
            for (const OtherTrack& track : model.other_tracks) {
                OtherTrack dense = track;
                dense.points = copy_scene_buffer(kv_get_othertrack_buffer(options.map_handle, track.key.c_str()));
                dense_other_tracks.push_back(std::move(dense));
            }
            if (!dense_own.empty()) {
                scene_own = std::move(dense_own);
                scene_other_tracks = std::move(dense_other_tracks);
            }
        } else {
            const char* err = kv_get_last_error();
            result.log_messages.push_back(std::string("[warn]canvas3D.cpp: 3D scene preview adaptive geometry failed: ") +
                                          (err ? err : "geometry failed"));
        }

        if (!kv_generate_geometry(options.map_handle, options.unit_distance, model.has_cp_arb ? 1 : 0,
                                  options.control_point_start, options.control_point_end,
                                  options.control_point_interval)) {
            const char* err = kv_get_last_error();
            result.log_messages.push_back(std::string("[error]canvas3D.cpp: 3D scene preview failed to restore 2D geometry: ") +
                                          (err ? err : "geometry failed"));
        }
    }

    auto append_track_path = [&](const std::string& key, const Matrix& points, bool has_theta, ImVec4 color,
                                 bool visible) {
        if (points.empty() || points.cols < 3) return;
        Canvas3DTrackPath path;
        path.key = key;
        path.color = color;
        path.visible = visible;
        path.points.reserve(points.rows);
        for (size_t row = 0; row < points.rows; ++row) {
            path.points.push_back(scene_matrix_row_point(points, row, has_theta));
        }
        scene.tracks.push_back(std::move(path));
    };
    append_track_path("own", scene_own, true, ImVec4(0.78f, 0.78f, 0.76f, 1.0f),
                      options.show_own_track_markers);
    for (const OtherTrack& track : scene_other_tracks) {
        append_track_path(track.key, track.points, false, track.color, track.visible);
    }
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
    explicit Impl(ID3D11Device* device) : device(device) {
        if (device) {
            device->AddRef();
            device->GetImmediateContext(&context);
        }
    }

    ~Impl() {
        stop_scene_loader();
        release_scene_resources();
        release_resources();
        release_render_target();
        release_com(scene_input_layout);
        release_com(scene_vertex_shader);
        release_com(scene_pixel_shader);
        release_com(scene_constant_buffer);
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
        MlMeshData data = {};
        if (!loader.load(path, data, error)) return false;

        bool ok = upload_model(data, path, error);
        loader.free_model(data);
        return ok;
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
                    if (!load_texture(src->texture_path, &materials[i].texture, error, &texture_has_alpha)) {
                        release_resources();
                        return false;
                    }
                    materials[i].has_texture = true;
                    materials[i].texture_has_alpha = texture_has_alpha;
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

        stop_scene_loader();
        if (preserve_loaded_models) {
            release_scene_track_chunks();
            scene_chunks.clear();
            clear_pending_scene_model_uploads();
            scene_last_error.clear();
            scene_stats_value = {};
            scene_load_summary_pending = false;
        } else {
            release_scene_resources();
        }
        scene_structure_edit = SceneStructureEditState{};
        scene_structure_locations.clear();
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

        build_scene_chunks();
        if (!build_scene_track_chunks(error)) {
            clear_scene();
            return false;
        }

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
        return true;
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

        clear_scene_structure_edit_target();

        std::vector<Canvas3DSceneObject> old_objects = std::move(scene_data.objects);
        std::vector<Canvas3DModelInstance> old_instances = std::move(scene_data.instances);
        std::vector<Canvas3DRepeaterSegment> old_repeaters = std::move(scene_data.repeaters);
        std::vector<Canvas3DBackgroundChange> old_backgrounds = std::move(scene_data.backgrounds);
        Canvas3DCameraStart old_camera = scene_data.camera;
        const double old_min_distance = scene_data.min_distance;
        const double old_max_distance = scene_data.max_distance;
        std::vector<SceneChunk> old_chunks = std::move(scene_chunks);
        auto old_structure_locations = scene_structure_locations;

        auto restore_dynamic_content = [&]() {
            scene_data.objects = std::move(old_objects);
            scene_data.instances = std::move(old_instances);
            scene_data.repeaters = std::move(old_repeaters);
            scene_data.backgrounds = std::move(old_backgrounds);
            scene_data.camera = old_camera;
            scene_data.min_distance = old_min_distance;
            scene_data.max_distance = old_max_distance;
            scene_chunks = std::move(old_chunks);
            scene_structure_locations = std::move(old_structure_locations);
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

        build_scene_chunks();
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
        scene_context_object_index = -1;
        scene_hover_highlight_batch.clear();

        std::map<std::string, SceneModelLoadRequest> requests = collect_scene_model_load_requests();
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
        if (!requests_to_load.empty() && scene_worker_running.load()) {
            stop_scene_loader();
            clear_pending_scene_model_uploads();
            requests_to_load.clear();
            requests_to_load.reserve(scene_models.size());
            for (auto& kv : scene_models) {
                if (kv.second.state != SceneModelGpu::State::Pending) continue;
                release_scene_model(kv.second);
                kv.second = SceneModelGpu{};
                auto request_it = requests.find(kv.first);
                if (request_it != requests.end()) requests_to_load.push_back(request_it->second);
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
        return true;
    }

    void clear_scene() {
        stop_scene_loader();
        release_scene_resources();
        scene_data = {};
        scene_active = false;
        scene_structure_edit = SceneStructureEditState{};
        scene_structure_locations.clear();
        scene_rotating = false;
        scene_hovered_object_index = -1;
        scene_context_object_index = -1;
        scene_hover_highlight_batch.clear();
        clear_scene_focus_highlight();
        scene_stats_value = {};
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
            visible_by_key[normalize_scene_track_key(item.key)] = item.visible;
        }

        bool changed = false;
        for (Canvas3DTrackPath& path : scene_data.tracks) {
            auto it = visible_by_key.find(normalize_scene_track_key(path.key));
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
        return true;
    }

    void set_scene_window(double back_m, double forward_m) {
        if (std::isfinite(back_m) && back_m >= 0.0) scene_window_back_m = back_m;
        if (std::isfinite(forward_m) && forward_m > 0.0) scene_window_forward_m = forward_m;
        scene_stats_value.window_back_m = scene_window_back_m;
        scene_stats_value.window_forward_m = scene_window_forward_m;
    }

    void set_scene_interaction_mode(Canvas3DSceneInteractionMode mode) {
        if (scene_interaction_mode == mode) return;
        scene_interaction_mode = mode;
        scene_rotating = false;
        scene_hovered_object_index = -1;
        scene_context_object_index = -1;
        scene_hover_highlight_batch.clear();
    }

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

    static bool same_structure_edit_target(const Canvas3DStructureEditTarget& a,
                                           const Canvas3DStructureEditTarget& b) {
        return a.edit_id == b.edit_id && a.track_key == b.track_key &&
            a.distance == b.distance && a.x == b.x && a.y == b.y && a.z == b.z &&
            a.rx == b.rx && a.ry == b.ry && a.rz == b.rz &&
            a.tilt == b.tilt && a.span == b.span;
    }

    const std::string* structure_edit_id_for_object(int object_index) const {
        if (!scene_object_index_valid(object_index)) return nullptr;
        const Canvas3DSceneObject& object = scene_data.objects[static_cast<size_t>(object_index)];
        if (object.kind != Canvas3DSceneObjectKind::Structure ||
            object.structure_put_between || object.edit_id.empty()) {
            return nullptr;
        }
        return &object.edit_id;
    }

    size_t scene_chunk_index_for_distance(double distance) const {
        if (scene_chunks.empty()) return 0;
        const double first = scene_chunks.front().d_min;
        int index = static_cast<int>(std::floor((distance - first) / scene_chunk_m));
        index = std::clamp(index, 0, static_cast<int>(scene_chunks.size()) - 1);
        return static_cast<size_t>(index);
    }

    Canvas3DModelInstance structure_instance_from_target(
        const Canvas3DStructureEditTarget& target,
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

    bool write_scene_structure_instance(const std::string& edit_id,
                                        Canvas3DModelInstance desired) {
        auto location_it = scene_structure_locations.find(edit_id);
        if (location_it == scene_structure_locations.end() || scene_chunks.empty()) return false;
        SceneStructureInstanceLocation location = location_it->second;
        if (location.source_index >= scene_data.instances.size() ||
            location.chunk_index >= scene_chunks.size() ||
            location.chunk_instance_index >= scene_chunks[location.chunk_index].instances.size()) {
            return false;
        }

        StructurePlacementFrame frame;
        if (!make_track_placement_frame(desired.track_key, desired.distance,
                                        desired.x, desired.y, desired.z,
                                        desired.rx, desired.ry, desired.rz,
                                        desired.tilt, desired.span, frame)) {
            return false;
        }
        store_world(desired.world, frame.model_right, frame.model_up,
                    frame.model_forward, frame.origin);

        SceneInstance replacement;
        replacement.model_path = scene_model_key_for_instance(desired, scene_geometry_generation);
        replacement.distance = desired.distance;
        replacement.object_index = desired.object_index;
        std::copy(desired.world, desired.world + 16, replacement.world);
        const std::string render_model_path = replacement.model_path;

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
                if (const std::string* moved_edit_id = structure_edit_id_for_object(moved.object_index)) {
                    auto moved_location = scene_structure_locations.find(*moved_edit_id);
                    if (moved_location != scene_structure_locations.end()) {
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
            scene_structure_locations[edit_id] = location;
        }

        scene_data.instances[location.source_index] = desired;
        if (scene_focus_highlight_object_index == desired.object_index) {
            scene_focus_highlight_model_path = render_model_path.empty()
                ? desired.model_path : render_model_path;
            std::copy(desired.world, desired.world + 16, scene_focus_highlight_world);
        }
        if (scene_structure_edit.active && scene_structure_edit.edit_id == edit_id) {
            scene_structure_edit.origin = frame.origin;
            scene_structure_edit.axes = frame.parameter_axes;
        }
        return true;
    }

    void clear_scene_structure_edit_target() {
        if (!scene_structure_edit.active) return;
        const std::string edit_id = scene_structure_edit.edit_id;
        Canvas3DModelInstance baseline = scene_structure_edit.baseline_instance;
        scene_structure_edit = SceneStructureEditState{};
        write_scene_structure_instance(edit_id, std::move(baseline));
    }

    bool set_scene_structure_edit_target(const Canvas3DStructureEditTarget& target,
                                         bool show_gizmo) {
        if (!scene_active || target.edit_id.empty()) return false;
        if (scene_structure_edit.active && scene_structure_edit.edit_id != target.edit_id) {
            clear_scene_structure_edit_target();
        }

        auto location_it = scene_structure_locations.find(target.edit_id);
        if (location_it == scene_structure_locations.end() ||
            location_it->second.source_index >= scene_data.instances.size()) {
            return false;
        }

        if (!scene_structure_edit.active) {
            scene_structure_edit.active = true;
            scene_structure_edit.edit_id = target.edit_id;
            scene_structure_edit.baseline_instance =
                scene_data.instances[location_it->second.source_index];
        } else if (same_structure_edit_target(scene_structure_edit.current, target) &&
                   scene_structure_edit.show_gizmo == show_gizmo) {
            return true;
        }

        Canvas3DModelInstance desired = structure_instance_from_target(
            target, scene_structure_edit.baseline_instance);
        if (!write_scene_structure_instance(target.edit_id, std::move(desired))) {
            if (scene_structure_edit.current.edit_id.empty()) {
                scene_structure_edit = SceneStructureEditState{};
            }
            return false;
        }
        scene_structure_edit.current = target;
        scene_structure_edit.show_gizmo = show_gizmo;
        if (!show_gizmo) {
            scene_structure_edit.hovered_axis = Canvas3DSceneDragAxis::None;
            scene_structure_edit.dragging_axis = Canvas3DSceneDragAxis::None;
        }
        return true;
    }

    bool update_scene_structure_instance(const Canvas3DStructureEditTarget& target) {
        if (!scene_active) return true;
        auto location_it = scene_structure_locations.find(target.edit_id);
        if (location_it == scene_structure_locations.end() ||
            location_it->second.source_index >= scene_data.instances.size()) {
            return false;
        }
        const Canvas3DModelInstance& base = scene_structure_edit.active &&
            scene_structure_edit.edit_id == target.edit_id
            ? scene_structure_edit.baseline_instance
            : scene_data.instances[location_it->second.source_index];
        Canvas3DModelInstance desired = structure_instance_from_target(target, base);
        if (!write_scene_structure_instance(target.edit_id, std::move(desired))) return false;

        if (scene_structure_edit.active && scene_structure_edit.edit_id == target.edit_id) {
            auto updated_location = scene_structure_locations.find(target.edit_id);
            if (updated_location != scene_structure_locations.end() &&
                updated_location->second.source_index < scene_data.instances.size()) {
                scene_structure_edit.baseline_instance =
                    scene_data.instances[updated_location->second.source_index];
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

    bool find_structure_jump_target(size_t source_row, SceneObjectJumpTarget& target) const {
        for (const SceneChunk& chunk : scene_chunks) {
            for (const SceneInstance& instance : chunk.instances) {
                if (!scene_object_index_valid(instance.object_index)) continue;
                const Canvas3DSceneObject& object = scene_data.objects[static_cast<size_t>(instance.object_index)];
                if (object.kind != Canvas3DSceneObjectKind::Structure || object.source_row != source_row) continue;
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
        if (!scene_model_center_world(model_path, world, target.center)) return false;
        target.object_index = repeater->object_index;
        target.distance = repeater->begin_distance;
        target.model_path = model_path;
        std::copy(world, world + 16, target.world);
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
        if (kind == Canvas3DSceneObjectKind::Structure) return find_structure_jump_target(source_row, target);
        if (kind == Canvas3DSceneObjectKind::Repeater) return find_repeater_jump_target(source_row, target);
        return false;
    }

    void clear_scene_focus_highlight() {
        scene_focus_highlight_object_index = -1;
        scene_focus_highlight_model_path.clear();
        scene_focus_highlight_until = {};
        scene_focus_highlight_batch.clear();
    }

    void start_scene_focus_highlight(int object_index, const std::string& model_path, const double world[16]) {
        scene_focus_highlight_batch.clear();
        scene_focus_highlight_object_index = object_index;
        scene_focus_highlight_model_path = model_path;
        std::copy(world, world + 16, scene_focus_highlight_world);
        scene_focus_highlight_until =
            std::chrono::steady_clock::now() +
            std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                std::chrono::duration<double>(kSceneFocusHighlightSeconds));
    }

    bool scene_focus_highlight_active_now() {
        if (scene_focus_highlight_object_index < 0 || scene_focus_highlight_model_path.empty()) return false;
        if (std::chrono::steady_clock::now() < scene_focus_highlight_until) return true;
        clear_scene_focus_highlight();
        return false;
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
        scene_camera_distance = std::clamp(distance - kSceneObjectJumpBackM,
                                           scene_data.min_distance,
                                           scene_data.max_distance);
        scene_camera_lateral_offset = 0.0;
        scene_camera_vertical_offset = kDefaultSceneCameraHeight;
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
        std::vector<unsigned char> pixels;

        HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
        if (FAILED(hr)) {
            error = hresult_text("CoCreateInstance(WIC)", hr);
            goto fail;
        }
        hr = factory->CreateDecoderFromFilename(utf8_to_wide_local(path).c_str(), nullptr, GENERIC_READ,
                                                WICDecodeMetadataCacheOnLoad, &decoder);
        if (FAILED(hr)) {
            error = "failed to open texture: " + path;
            goto fail;
        }
        hr = decoder->GetFrame(0, &frame);
        if (FAILED(hr)) {
            error = "failed to decode texture frame: " + path;
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
            error = "failed to convert texture to RGBA: " + path;
            goto fail;
        }
        converter->GetSize(&width, &height);
        if (width == 0 || height == 0) {
            error = "texture has invalid size: " + path;
            goto fail;
        }
        pixels.resize(static_cast<size_t>(width) * height * 4);
        hr = converter->CopyPixels(nullptr, width * 4, static_cast<UINT>(pixels.size()), pixels.data());
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
            sub.SysMemPitch = width * 4;
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

    void clear_pending_scene_model_uploads() {
        std::lock_guard<std::mutex> lock(scene_upload_mutex);
        scene_pending_uploads.clear();
    }

    void release_scene_resources() {
        for (auto& kv : scene_models) release_scene_model(kv.second);
        scene_models.clear();
        release_scene_track_chunks();
        scene_chunks.clear();
        scene_structure_locations.clear();
        scene_structure_edit = SceneStructureEditState{};
        clear_pending_scene_model_uploads();
        scene_last_error.clear();
        scene_stats_value = {};
        scene_load_summary_pending = false;
    }

    void stop_scene_loader() {
        scene_cancel.store(true);
        if (scene_worker.joinable()) scene_worker.join();
        scene_worker_running.store(false);
        scene_cancel.store(false);
    }

    CpuModelData copy_cpu_model(const std::string& path, const MlMeshData& data) {
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
        scene_load_summary_pending = true;
        scene_worker_running.store(true);
        scene_worker = std::thread([this, requests_by_source = std::move(requests_by_source)]() mutable {
            size_t source_index = 0;
            for (auto& source_entry : requests_by_source) {
                const std::string& path = source_entry.first;
                std::vector<SceneModelLoadRequest>& source_requests = source_entry.second;
                if (scene_cancel.load()) break;
                const std::string progress = std::to_string(++source_index) + "/" +
                    std::to_string(requests_by_source.size());
                CpuModelData source_cpu;
                source_cpu.path = path;
                source_cpu.scene_key = path;
                MlMeshData data = {};
                std::string error;
                if (scene_loader.load(path, data, error)) {
                    source_cpu = copy_cpu_model(path, data);
                    scene_loader.free_model(data);
                    if (!source_cpu.ok) {
                        push_scene_load_log("[warn]canvas3D.cpp: failed to read scene model " + progress + ": " +
                                            path + ": " + source_cpu.error);
                    }
                } else {
                    source_cpu.error = error;
                    push_scene_load_log("[warn]canvas3D.cpp: failed to read scene model " + progress + ": " +
                                        path + ": " + error);
                }
                if (scene_cancel.load()) break;

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
                    std::vector<CpuModelData> derived_models;
                    derived_models.reserve(source_requests.size());
                    for (const SceneModelLoadRequest& request : source_requests) {
                        if (!request.put_between.enabled) {
                            regular_request = &request;
                            continue;
                        }
                        CpuModelData derived = derive_put_between_model(source_cpu, request);
                        if (!derived.ok) {
                            push_scene_load_log("[warn]canvas3D.cpp: failed to deform PutBetween model: " +
                                                path + ": " + derived.error);
                        }
                        derived_models.push_back(std::move(derived));
                        if (scene_cancel.load()) break;
                    }
                    if (scene_cancel.load()) break;
                    if (regular_request) {
                        source_cpu.scene_key = regular_request->key;
                        outputs.push_back(std::move(source_cpu));
                    }
                    for (CpuModelData& derived : derived_models) {
                        outputs.push_back(std::move(derived));
                    }
                }
                {
                    std::lock_guard<std::mutex> lock(scene_upload_mutex);
                    for (CpuModelData& output : outputs) {
                        scene_pending_uploads.push_back(std::move(output));
                    }
                }
            }
            scene_worker_running.store(false);
        });
    }

    void maybe_log_scene_model_load_summary() {
        if (!scene_load_summary_pending || scene_worker_running.load()) return;
        Canvas3DSceneStats stats = scene_stats();
        if (stats.model_ready_count + stats.model_failed_count < stats.model_path_count) return;
        push_scene_load_log("[info]canvas3D.cpp: scene model loading finished: loaded=" +
                            std::to_string(stats.model_ready_count) +
                            " failed=" + std::to_string(stats.model_failed_count) +
                            " total=" + std::to_string(stats.model_path_count));
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
                    if (load_texture(src->texture_path, &model.materials[i].texture, texture_error, &texture_has_alpha)) {
                        model.materials[i].has_texture = true;
                        model.materials[i].texture_has_alpha = texture_has_alpha;
                    } else if (scene_last_error.empty()) {
                        scene_last_error = texture_error;
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

    void upload_pending_scene_models() {
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
        HRESULT hr = D3DCompile(kSceneHighlightOutlineShaderSource, std::strlen(kSceneHighlightOutlineShaderSource),
                                nullptr, nullptr, nullptr, "vs_main", "vs_4_0",
                                D3DCOMPILE_ENABLE_STRICTNESS, 0, &vs_blob, &errors);
        if (FAILED(hr)) {
            error = errors ? static_cast<const char*>(errors->GetBufferPointer()) :
                hresult_text("D3DCompile(scene outline vertex shader)", hr);
            release_com(errors);
            return false;
        }
        release_com(errors);

        hr = D3DCompile(kSceneHighlightOutlineShaderSource, std::strlen(kSceneHighlightOutlineShaderSource),
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
        HRESULT hr = D3DCompile(kScenePickShaderSource, std::strlen(kScenePickShaderSource),
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
        if (scene_vertex_shader && scene_pixel_shader && scene_input_layout && scene_constant_buffer &&
            scene_depth_state && scene_depth_read_state && rasterizer_state && alpha_mask_rasterizer_state &&
            track_rasterizer_state && sampler_state && blend_state &&
            scene_outline_vertex_shader && scene_outline_pixel_shader &&
            scene_outline_constant_buffer && scene_outline_sampler_state &&
            scene_pick_pixel_shader && scene_pick_constant_buffer) return true;

        if (!ensure_pipeline(error)) return false;
        if (!ensure_scene_depth_states(error)) return false;
        if (scene_vertex_shader && scene_pixel_shader && scene_input_layout && scene_constant_buffer &&
            track_rasterizer_state && sampler_state && blend_state) {
            return ensure_scene_outline_pipeline(error) && ensure_scene_pick_pipeline(error);
        }

        ID3DBlob* vs_blob = nullptr;
        ID3DBlob* ps_blob = nullptr;
        ID3DBlob* errors = nullptr;
        HRESULT hr = D3DCompile(kSceneShaderSource, std::strlen(kSceneShaderSource), nullptr, nullptr, nullptr,
                                "vs_main", "vs_4_0", D3DCOMPILE_ENABLE_STRICTNESS, 0, &vs_blob, &errors);
        if (FAILED(hr)) {
            error = errors ? static_cast<const char*>(errors->GetBufferPointer()) : hresult_text("D3DCompile(scene vertex shader)", hr);
            release_com(errors);
            return false;
        }
        release_com(errors);

        hr = D3DCompile(kSceneShaderSource, std::strlen(kSceneShaderSource), nullptr, nullptr, nullptr,
                        "ps_main", "ps_4_0", D3DCOMPILE_ENABLE_STRICTNESS, 0, &ps_blob, &errors);
        if (FAILED(hr)) {
            error = errors ? static_cast<const char*>(errors->GetBufferPointer()) : hresult_text("D3DCompile(scene pixel shader)", hr);
            release_com(errors);
            release_com(vs_blob);
            return false;
        }
        release_com(errors);

        hr = device->CreateVertexShader(vs_blob->GetBufferPointer(), vs_blob->GetBufferSize(), nullptr, &scene_vertex_shader);
        if (FAILED(hr)) {
            error = hresult_text("CreateVertexShader(scene)", hr);
            release_com(vs_blob);
            release_com(ps_blob);
            return false;
        }
        hr = device->CreatePixelShader(ps_blob->GetBufferPointer(), ps_blob->GetBufferSize(), nullptr, &scene_pixel_shader);
        if (FAILED(hr)) {
            error = hresult_text("CreatePixelShader(scene)", hr);
            release_com(vs_blob);
            release_com(ps_blob);
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
        if (FAILED(hr)) {
            error = hresult_text("CreateInputLayout(scene)", hr);
            return false;
        }

        D3D11_BUFFER_DESC cb_desc = {};
        cb_desc.ByteWidth = sizeof(SceneViewConstants);
        cb_desc.Usage = D3D11_USAGE_DEFAULT;
        cb_desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        hr = device->CreateBuffer(&cb_desc, nullptr, &scene_constant_buffer);
        if (FAILED(hr)) {
            error = hresult_text("CreateBuffer(scene constants)", hr);
            return false;
        }
        return ensure_scene_outline_pipeline(error) && ensure_scene_pick_pipeline(error);
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
                         bool mask_pass = false) {
        if (!vb || !ib || !instance_buffer || instance_count == 0) return;

        bind_scene_instanced_mesh(vb, ib, instance_buffer);
        context->VSSetShader(scene_vertex_shader, nullptr, 0);
        context->PSSetShader(scene_pixel_shader, nullptr, 0);
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
            context->UpdateSubresource(scene_constant_buffer, 0, nullptr, &constants, 0, 0);
            ID3D11ShaderResourceView* texture = material && material->has_texture ? material->texture : nullptr;
            context->PSSetShaderResources(0, 1, &texture);
            context->DrawIndexedInstanced(part.index_count, instance_count, part.start_index, 0, 0);
        }
        ID3D11ShaderResourceView* null_srv = nullptr;
        context->PSSetShaderResources(0, 1, &null_srv);
        context->RSSetState(base_rasterizer);
        context->OMSetDepthStencilState(scene_depth_state, 0);
        context->OMSetBlendState(nullptr, blend_factor, 0xffffffff);
    }

    void draw_scene_model(SceneModelGpu& model, const std::vector<SceneInstanceData>& instances, const Mat4& view_proj) {
        if (model.state != SceneModelGpu::State::Ready || instances.empty()) return;
        std::string error;
        if (!ensure_instance_buffer(model.instance_buffer, model.instance_capacity, instances, error)) {
            scene_last_error = error;
            return;
        }
        draw_scene_mesh(model.vertex_buffer, model.index_buffer, model.instance_buffer,
                        model.parts, model.materials, static_cast<UINT>(instances.size()), view_proj, rasterizer_state);
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

    int pick_scene_object_at_mouse(const std::map<std::string, std::vector<SceneInstanceData>>& visible_instances,
                                   const std::map<int, std::vector<SceneVisibleInstanceRef>>& object_refs,
                                   const Mat4& view_proj,
                                   int width,
                                   int height,
                                   ImVec2 mouse_local,
                                   std::string& error) {
        const int pixel_x = static_cast<int>(std::floor(mouse_local.x));
        const int pixel_y = static_cast<int>(std::floor(mouse_local.y));
        if (pixel_x < 0 || pixel_y < 0 || pixel_x >= width || pixel_y >= height) return -1;
        if (object_refs.empty()) return -1;
        if (!ensure_scene_pick_target(width, height, error)) return -1;

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
        if (pick_batches.empty()) return -1;

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
            return -1;
        }
        unsigned char rgba[4] = {};
        if (mapped.pData) std::memcpy(rgba, mapped.pData, sizeof(rgba));
        context->Unmap(scene_pick_readback_texture, 0);

        const int object_index = scene_object_index_from_pick_rgba(rgba);
        return scene_object_index_valid(object_index) ? object_index : -1;
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
        constants.texel_radius[2] = kSceneHighlightOutlineWidthPx * 0.5f;
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
                                std::string& error) {
        if (!track.vertex_buffer || !track.index_buffer || track.index_count == 0) return;

        track_instance[0] = make_chunk_instance_data(track.origin, render_origin);
        if (!ensure_instance_buffer(track.instance_buffer, track.instance_capacity, track_instance, error)) {
            if (!error.empty()) scene_last_error = error;
            return;
        }
        draw_scene_mesh(track.vertex_buffer, track.index_buffer, track.instance_buffer,
                        track.parts, track.materials, 1, view_proj, track_rasterizer_state);
        ++scene_stats_value.drawn_track_chunk_count;
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
            if (!std::isfinite(view_depth) || view_depth <= static_cast<double>(kSceneNearZ)) continue;
            ImVec2 screen;
            if (!project_scene_point(relative_point, view_proj, width, height, screen)) continue;
            raw_min.x = std::min(raw_min.x, screen.x);
            raw_min.y = std::min(raw_min.y, screen.y);
            raw_max.x = std::max(raw_max.x, screen.x);
            raw_max.y = std::max(raw_max.y, screen.y);
            projected = true;
        }
        if (!projected) return false;

        const float outline_padding = kSceneHighlightOutlineWidthPx + 2.0f;
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

    void build_scene_chunks() {
        scene_chunks.clear();
        scene_structure_locations.clear();
        double min_d = scene_data.min_distance;
        double max_d = scene_data.max_distance;
        if (max_d <= min_d) {
            min_d = scene_camera_distance - scene_window_back_m;
            max_d = scene_camera_distance + scene_window_forward_m;
        }
        double first = std::floor(min_d / scene_chunk_m) * scene_chunk_m;
        double last = std::ceil(max_d / scene_chunk_m) * scene_chunk_m;
        size_t count = static_cast<size_t>(std::max(1.0, (last - first) / scene_chunk_m));
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
            int index = static_cast<int>(std::floor((source.distance - first) / scene_chunk_m));
            index = std::clamp(index, 0, static_cast<int>(scene_chunks.size()) - 1);
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
            SceneChunk& chunk = scene_chunks[static_cast<size_t>(index)];
            const size_t chunk_instance_index = chunk.instances.size();
            chunk.instances.push_back(std::move(instance));
            if (scene_object_index_valid(source.object_index)) {
                const Canvas3DSceneObject& object =
                    scene_data.objects[static_cast<size_t>(source.object_index)];
                if (object.kind == Canvas3DSceneObjectKind::Structure &&
                    !object.structure_put_between && !object.edit_id.empty()) {
                    scene_structure_locations[object.edit_id] = SceneStructureInstanceLocation{
                        source_index,
                        static_cast<size_t>(index),
                        chunk_instance_index
                    };
                }
            }
        }
        for (size_t repeater_index = 0; repeater_index < scene_data.repeaters.size(); ++repeater_index) {
            const Canvas3DRepeaterSegment& repeater = scene_data.repeaters[repeater_index];
            double repeater_first = 0.0;
            double repeater_last = 0.0;
            if (!scene_repeater_render_distance_span(repeater, repeater_first, repeater_last)) continue;
            int begin_index = static_cast<int>(std::floor((repeater_first - first) / scene_chunk_m));
            int end_index = static_cast<int>(std::floor((repeater_last - first) / scene_chunk_m));
            begin_index = std::clamp(begin_index, 0, static_cast<int>(scene_chunks.size()) - 1);
            end_index = std::clamp(end_index, 0, static_cast<int>(scene_chunks.size()) - 1);
            for (int index = begin_index; index <= end_index; ++index) {
                scene_chunks[static_cast<size_t>(index)].repeater_indices.push_back(repeater_index);
            }
        }
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
        constexpr float marker_half_width = kSceneTrackMarkerWidth * 0.5f;
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
                material.diffuse[3] = kSceneTrackMarkerAlpha;
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
        for (const Canvas3DTrackPath& path : scene_data.tracks) {
            if (path.key == "own" || path.key.empty() || path.key == "0") return &path;
        }
        return scene_data.tracks.empty() ? nullptr : &scene_data.tracks.front();
    }

    bool sample_track_path(const Canvas3DTrackPath& path, double distance, Canvas3DTrackPoint& out) const {
        if (path.points.empty()) return false;
        if (distance <= path.points.front().distance) {
            out = path.points.front();
            return true;
        }
        if (distance >= path.points.back().distance) {
            out = path.points.back();
            return true;
        }
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
        out.distance = distance;
        out.x = a.x + (b.x - a.x) * t;
        out.y = a.y + (b.y - a.y) * t;
        out.z = a.z + (b.z - a.z) * t;
        out.theta = angle_lerp(a.theta, b.theta, t);
        out.gradient = a.gradient + (b.gradient - a.gradient) * t;
        out.cant_angle = a.cant_angle + (b.cant_angle - a.cant_angle) * t;
        return true;
    }

    bool sample_own_track(double distance, Canvas3DTrackPoint& out) const {
        const Canvas3DTrackPath* path = own_track_path();
        return path && sample_track_path(*path, distance, out);
    }

    const Canvas3DTrackPath* other_track_path_for_normalized_key(const std::string& normalized) const {
        const Canvas3DTrackPath* own = own_track_path();
        for (const Canvas3DTrackPath& path : scene_data.tracks) {
            if (&path == own) continue;
            if (normalize_scene_track_key(path.key) == normalized) return &path;
        }
        return nullptr;
    }

    const Canvas3DTrackPath* track_path_for_key(const std::string& key) const {
        std::string normalized = normalize_scene_track_key(key);
        if (const Canvas3DTrackPath* other = other_track_path_for_normalized_key(normalized)) return other;
        if (is_scene_own_track_alias(normalized)) return own_track_path();
        return nullptr;
    }

    const Canvas3DTrackPath* placement_track_path_for_key(const std::string& key) const {
        std::string normalized = normalize_scene_track_key(key);
        if (is_scene_own_track_placement_key(normalized)) return own_track_path();
        return other_track_path_for_normalized_key(normalized);
    }

    bool sample_scene_track(const std::string& key, double distance, Canvas3DTrackPoint& out) const {
        const Canvas3DTrackPath* path = track_path_for_key(key);
        return path && sample_track_path(*path, distance, out);
    }

    bool sample_scene_placement_track(const std::string& key, double distance, Canvas3DTrackPoint& out) const {
        const Canvas3DTrackPath* path = placement_track_path_for_key(key);
        return path && sample_track_path(*path, distance, out);
    }

    static void apply_track_cant(DVec3& right, DVec3& up, const DVec3& forward, double cant_angle) {
        if (std::abs(cant_angle) <= 1e-9 || !std::isfinite(cant_angle)) return;
        DVec3 axis = forward * -1.0;
        right = rotate_axis(right, axis, -cant_angle);
        up = rotate_axis(up, axis, -cant_angle);
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
        if (chunk.d_max < scene_data.max_distance) chunk_max -= kSceneRepeaterDistanceEpsilon;
        double range_min = std::max(visible_min, chunk.d_min);
        double range_max = std::min(visible_max, chunk_max);
        if (range_max < range_min) return;

        for (size_t repeater_index : chunk.repeater_indices) {
            if (repeater_index >= scene_data.repeaters.size()) continue;
            const Canvas3DRepeaterSegment& repeater = scene_data.repeaters[repeater_index];
            if (repeater.model_paths.empty() || repeater.end_distance < repeater.begin_distance) continue;

            const double begin = std::max(range_min, repeater.begin_distance);
            const double end = std::min(range_max, repeater.end_distance);
            if (end < begin - kSceneRepeaterDistanceEpsilon) continue;

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
                if (repeater.begin_distance >= begin - kSceneRepeaterDistanceEpsilon &&
                    repeater.begin_distance <= end + kSceneRepeaterDistanceEpsilon) {
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
                if (distance < begin - kSceneRepeaterDistanceEpsilon) continue;
                if (distance > end + kSceneRepeaterDistanceEpsilon) break;
                if (distance >= repeater.end_distance - end_epsilon) break;
                emit(distance, static_cast<size_t>(index));
                if (++emitted >= kSceneRepeaterInstanceLimit) break;
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
        scene_camera_vertical_offset = kDefaultSceneCameraHeight;
        scene_camera_yaw_offset = 0.0f;
        scene_camera_pitch = 0.0f;
        scene_rotating = false;
        return update_scene_camera_from_owntrack();
    }

    void reset_scene_camera_tracking() {
        Canvas3DTrackPoint point;
        scene_camera_lateral_offset = 0.0;
        scene_camera_vertical_offset = kDefaultSceneCameraHeight;
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

    bool scene_camera_ray(ImVec2 mouse_local, int width, int height,
                          DVec3& ray_origin, DVec3& ray_direction) const {
        if (width <= 0 || height <= 0) return false;
        const double ndc_x = 2.0 * static_cast<double>(mouse_local.x) /
            static_cast<double>(width) - 1.0;
        const double ndc_y = 1.0 - 2.0 * static_cast<double>(mouse_local.y) /
            static_cast<double>(height);
        const double aspect = static_cast<double>(width) / static_cast<double>(height);
        const double tan_half_fov = std::tan(static_cast<double>(kSceneCameraFovY) * 0.5);
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
        parameter = (b * e - c * d) / denominator;
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

    bool update_scene_structure_gizmo_projection(int width, int height) {
        for (SceneGizmoAxisProjection& projection : scene_structure_edit.projection) {
            projection = SceneGizmoAxisProjection{};
        }
        if (!scene_structure_edit.active || !scene_structure_edit.show_gizmo ||
            width <= 0 || height <= 0) {
            return false;
        }

        const DVec3 relative_origin = scene_structure_edit.origin - scene_camera_pos;
        const DVec3 camera_forward = dvec3_from_vec3(scene_forward());
        const double depth = dot(relative_origin, camera_forward);
        if (!std::isfinite(depth) || depth <= static_cast<double>(kSceneNearZ)) return false;

        Vec3 forward = scene_forward();
        Mat4 view = look_to_bve({0.0f, 0.0f, 0.0f}, forward, {0.0f, 1.0f, 0.0f});
        const float aspect = static_cast<float>(width) /
            std::max(1.0f, static_cast<float>(height));
        Mat4 proj = perspective_fov_lh_reverse_z(kSceneCameraFovY, aspect,
                                                 kSceneNearZ, scene_far_z());
        Mat4 view_proj = multiply(view, proj);
        ImVec2 origin_screen;
        if (!project_scene_point(relative_origin, view_proj, width, height, origin_screen) ||
            origin_screen.x < 0.0f || origin_screen.y < 0.0f ||
            origin_screen.x > static_cast<float>(width) ||
            origin_screen.y > static_cast<float>(height)) {
            return false;
        }

        const double generic_world_units_per_pixel =
            2.0 * depth * std::tan(static_cast<double>(kSceneCameraFovY) * 0.5) /
            static_cast<double>(height);
        static constexpr std::array<ImVec2, 3> fallback_directions = {
            ImVec2(0.8660254f, 0.5f),
            ImVec2(0.0f, -1.0f),
            ImVec2(-0.8660254f, 0.5f)
        };
        bool any = false;
        for (size_t i = 0; i < scene_structure_edit.axes.size(); ++i) {
            const DVec3 axis = normalize(scene_structure_edit.axes[i]);
            ImVec2 one_meter_screen;
            const bool projected = project_scene_point(relative_origin + axis, view_proj,
                                                        width, height, one_meter_screen);
            float dx = projected ? one_meter_screen.x - origin_screen.x : 0.0f;
            float dy = projected ? one_meter_screen.y - origin_screen.y : 0.0f;
            const float projected_length = std::sqrt(dx * dx + dy * dy);
            ImVec2 direction = fallback_directions[i];
            if (projected_length >= 1.0f) {
                direction = ImVec2(dx / projected_length, dy / projected_length);
            }
            SceneGizmoAxisProjection& axis_projection = scene_structure_edit.projection[i];
            axis_projection.valid = true;
            axis_projection.ray_drag_reliable = projected_length >= 4.0f;
            axis_projection.direction = direction;
            axis_projection.begin = ImVec2(origin_screen.x + direction.x * 9.0f,
                                           origin_screen.y + direction.y * 9.0f);
            axis_projection.end = ImVec2(origin_screen.x + direction.x * kSceneGizmoLengthPx,
                                         origin_screen.y + direction.y * kSceneGizmoLengthPx);
            axis_projection.world_units_per_pixel = projected_length >= 1.0f
                ? 1.0 / static_cast<double>(projected_length)
                : generic_world_units_per_pixel;
            any = true;
        }
        return any;
    }

    std::optional<Canvas3DStructureDragUpdate> handle_scene_structure_gizmo_input(
        bool canvas_hovered, int width, int height, ImVec2 mouse_local) {
        if (!update_scene_structure_gizmo_projection(width, height)) {
            scene_structure_edit.hovered_axis = Canvas3DSceneDragAxis::None;
            if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                scene_structure_edit.dragging_axis = Canvas3DSceneDragAxis::None;
            }
            return std::nullopt;
        }

        if (scene_structure_edit.dragging_axis == Canvas3DSceneDragAxis::None) {
            scene_structure_edit.hovered_axis = Canvas3DSceneDragAxis::None;
            if (canvas_hovered) {
                float best_distance_sq = kSceneGizmoHitRadiusPx * kSceneGizmoHitRadiusPx;
                for (size_t i = 0; i < scene_structure_edit.projection.size(); ++i) {
                    const SceneGizmoAxisProjection& projection = scene_structure_edit.projection[i];
                    if (!projection.valid) continue;
                    const float distance_sq = point_segment_distance_sq(
                        mouse_local, projection.begin, projection.end);
                    if (distance_sq <= best_distance_sq) {
                        best_distance_sq = distance_sq;
                        scene_structure_edit.hovered_axis = structure_drag_axis_from_index(i);
                    }
                }
            }

            if (scene_structure_edit.hovered_axis != Canvas3DSceneDragAxis::None &&
                ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                const int axis_index = structure_drag_axis_index(scene_structure_edit.hovered_axis);
                const SceneGizmoAxisProjection& projection =
                    scene_structure_edit.projection[static_cast<size_t>(axis_index)];
                scene_structure_edit.dragging_axis = scene_structure_edit.hovered_axis;
                scene_structure_edit.drag_axis_origin = scene_structure_edit.origin;
                scene_structure_edit.drag_axis_direction =
                    normalize(scene_structure_edit.axes[static_cast<size_t>(axis_index)]);
                scene_structure_edit.drag_start_mouse = mouse_local;
                scene_structure_edit.drag_screen_direction = projection.direction;
                scene_structure_edit.drag_world_units_per_pixel =
                    projection.world_units_per_pixel;
                scene_structure_edit.drag_start_value = axis_index == 0
                    ? scene_structure_edit.current.x
                    : axis_index == 1 ? scene_structure_edit.current.y
                                      : scene_structure_edit.current.z;
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

        if (scene_structure_edit.dragging_axis == Canvas3DSceneDragAxis::None) return std::nullopt;
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
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
                scene_structure_edit.drag_world_units_per_pixel;
        }

        const double candidate = truncate_scene_millimeter(
            scene_structure_edit.drag_start_value + delta);
        const int axis_index = structure_drag_axis_index(scene_structure_edit.dragging_axis);
        double* current_value = axis_index == 0 ? &scene_structure_edit.current.x
            : axis_index == 1 ? &scene_structure_edit.current.y
                              : &scene_structure_edit.current.z;
        if (std::abs(candidate - *current_value) < 0.000999999) return std::nullopt;
        const double previous_value = *current_value;
        *current_value = candidate;
        const Canvas3DSceneDragAxis changed_axis = scene_structure_edit.dragging_axis;
        Canvas3DModelInstance desired = structure_instance_from_target(
            scene_structure_edit.current, scene_structure_edit.baseline_instance);
        if (!write_scene_structure_instance(scene_structure_edit.edit_id, std::move(desired))) {
            *current_value = previous_value;
            return std::nullopt;
        }

        Canvas3DStructureDragUpdate result;
        result.edit_id = scene_structure_edit.edit_id;
        result.axis = changed_axis;
        result.x = scene_structure_edit.current.x;
        result.y = scene_structure_edit.current.y;
        result.z = scene_structure_edit.current.z;
        return result;
    }

    bool scene_structure_gizmo_consumes_left_input() const {
        return scene_structure_edit.dragging_axis != Canvas3DSceneDragAxis::None ||
            scene_structure_edit.hovered_axis != Canvas3DSceneDragAxis::None;
    }

    void draw_scene_structure_gizmo(ImDrawList* draw, ImVec2 canvas_origin,
                                    int width, int height) {
        if (!draw || !update_scene_structure_gizmo_projection(width, height)) return;
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
        static constexpr std::array<const char*, 3> labels = {"X", "Y", "Z"};
        for (size_t i = 0; i < scene_structure_edit.projection.size(); ++i) {
            const SceneGizmoAxisProjection& projection = scene_structure_edit.projection[i];
            if (!projection.valid) continue;
            const Canvas3DSceneDragAxis axis = structure_drag_axis_from_index(i);
            const bool active = scene_structure_edit.dragging_axis == axis ||
                scene_structure_edit.hovered_axis == axis;
            const ImU32 color = active ? active_colors[i] : colors[i];
            ImVec2 begin(canvas_origin.x + projection.begin.x,
                         canvas_origin.y + projection.begin.y);
            ImVec2 end(canvas_origin.x + projection.end.x,
                       canvas_origin.y + projection.end.y);
            ImVec2 direction = projection.direction;
            ImVec2 perpendicular(-direction.y, direction.x);
            ImVec2 arrow_base(end.x - direction.x * 11.0f,
                              end.y - direction.y * 11.0f);
            draw->AddLine(begin, end, color, active ? 4.5f : 3.0f);
            draw->AddTriangleFilled(
                end,
                ImVec2(arrow_base.x + perpendicular.x * 5.5f,
                       arrow_base.y + perpendicular.y * 5.5f),
                ImVec2(arrow_base.x - perpendicular.x * 5.5f,
                       arrow_base.y - perpendicular.y * 5.5f),
                color);
            draw->AddText(ImVec2(end.x + direction.x * 5.0f - 3.0f,
                                 end.y + direction.y * 5.0f - 7.0f), color, labels[i]);
        }
        const SceneGizmoAxisProjection& first = scene_structure_edit.projection[0];
        if (first.valid) {
            ImVec2 center(canvas_origin.x + first.begin.x - first.direction.x * 9.0f,
                          canvas_origin.y + first.begin.y - first.direction.y * 9.0f);
            draw->AddCircleFilled(center, 4.0f, IM_COL32(245, 245, 245, 235));
            draw->AddCircle(center, 4.0f, IM_COL32(30, 30, 30, 220), 0, 1.0f);
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
        float step = scene_slow_speed_mps * (fast ? scene_fast_multiplier : 1.0f) * dt;
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

    void draw_background_model(const Mat4& view_proj) {
        std::string path = current_background_path();
        if (path.empty()) return;
        auto it = scene_models.find(path);
        if (it == scene_models.end() || it->second.state != SceneModelGpu::State::Ready) return;
        Mat4 world = identity();
        std::vector<SceneInstanceData> instances{make_instance_data(world)};
        draw_scene_model(it->second, instances, view_proj);
    }

    float scene_far_z() const {
        double far_z = scene_window_back_m + scene_window_forward_m + scene_chunk_m * 2.0;
        if (!std::isfinite(far_z)) far_z = kSceneBackgroundFarZ;
        return static_cast<float>(std::clamp(far_z, 256.0, static_cast<double>(kSceneBackgroundFarZ)));
    }

    void render_scene_preview_target(int width, int height, ImVec2 mouse_local, bool pick_enabled) {
        std::string error;
        scene_hovered_object_index = -1;
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

        const ImVec4 bg = clamp_background_color(background_color_value);
        const float clear_color[4] = {bg.x, bg.y, bg.z, 1.0f};
        context->OMSetRenderTargets(1, &render_rtv, depth_dsv);
        context->ClearRenderTargetView(render_rtv, clear_color);
        context->ClearDepthStencilView(depth_dsv, D3D11_CLEAR_DEPTH, kSceneDepthClear, 0);

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
        Mat4 background_proj = perspective_fov_lh_reverse_z(kSceneCameraFovY, aspect, kSceneBackgroundNearZ, kSceneBackgroundFarZ);
        Mat4 background_view_proj = multiply(view, background_proj);
        draw_background_model(background_view_proj);
        context->ClearDepthStencilView(depth_dsv, D3D11_CLEAR_DEPTH, kSceneDepthClear, 0);

        Mat4 proj = perspective_fov_lh_reverse_z(kSceneCameraFovY, aspect, kSceneNearZ, scene_far_z());
        Mat4 view_proj = multiply(view, proj);

        scene_stats_value.drawn_instance_count = 0;
        scene_stats_value.drawn_track_chunk_count = 0;
        scene_stats_value.camera_distance = scene_camera_distance;

        double visible_min = scene_camera_distance - scene_window_back_m;
        double visible_max = scene_camera_distance + scene_window_forward_m;
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
            draw_scene_model(model_it->second, kv.second, view_proj);
            if (model_it->second.state == SceneModelGpu::State::Ready) {
                scene_stats_value.drawn_instance_count += kv.second.size();
            }
        }
        const int picked_object_index = can_pick
            ? pick_scene_object_at_mouse(visible_instances, visible_object_instances, view_proj,
                                         width, height, mouse_local, error)
            : -1;
        if (!error.empty() && scene_last_error != error) scene_last_error = error;
        ID3D11RenderTargetView* scene_target = render_rtv;
        context->OMSetRenderTargets(1, &scene_target, depth_dsv);
        context->RSSetViewports(1, &viewport);
        context->OMSetDepthStencilState(scene_depth_state, 0);
        context->OMSetBlendState(nullptr, blend_factor, 0xffffffff);
        if (picked_object_index >= 0) {
            scene_hovered_object_index = picked_object_index;
            scene_hover_highlight_batch.object_index = picked_object_index;
            scene_hover_highlight_batch.screen_min = ImVec2(static_cast<float>(width), static_cast<float>(height));
            scene_hover_highlight_batch.screen_max = ImVec2(0.0f, 0.0f);
            auto refs_it = visible_object_instances.find(picked_object_index);
            if (refs_it != visible_object_instances.end()) {
                for (const SceneVisibleInstanceRef& ref : refs_it->second) {
                    if (!ref.model_path) continue;
                    auto visible_it = visible_instances.find(*ref.model_path);
                    if (visible_it == visible_instances.end() || ref.instance_index >= visible_it->second.size()) continue;
                    scene_hover_highlight_batch.instances.push_back(SceneHighlightInstance{
                        *ref.model_path,
                        visible_it->second[ref.instance_index],
                        ref.screen_min,
                        ref.screen_max
                    });
                    include_scene_screen_bounds(scene_hover_highlight_batch.screen_min,
                                                scene_hover_highlight_batch.screen_max,
                                                ref);
                }
            }
        }
        if (picked_object_index >= 0 &&
            picked_object_index == scene_focus_highlight_object_index) {
            clear_scene_focus_highlight();
        }
        update_scene_focus_highlight_batch(render_origin, view_proj, width, height);
        for (size_t i = 0; i < scene_chunks.size() && i < scene_track_chunks.size(); ++i) {
            if (!scene_chunk_visible(scene_chunks[i], visible_min, visible_max)) continue;
            draw_scene_track_chunk(scene_track_chunks[i], render_origin, view_proj, track_instance, error);
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
            if (elapsed_seconds > 0.0 && elapsed_seconds <= kSceneFpsIdleResetSeconds) {
                const float sample = static_cast<float>(1.0 / elapsed_seconds);
                scene_fps_value = scene_fps_value > 0.0f
                    ? scene_fps_value + (sample - scene_fps_value) * kSceneFpsSmoothing
                    : sample;
            }
        }
        scene_fps_last_frame_at = now;
        scene_fps_last_frame_valid = true;
    }

    void draw_scene_overlay_label(ImDrawList* draw, ImVec2 pos, ImVec2 text_size,
                                  const char* text, float pad) const {
        draw->AddRectFilled(ImVec2(pos.x - pad, pos.y - pad * 0.5f),
                            ImVec2(pos.x + text_size.x + pad, pos.y + text_size.y + pad * 0.5f),
                            IM_COL32(0, 0, 0, 140), 3.0f);
        draw->AddText(pos, IM_COL32(255, 255, 255, 230), text);
    }

    void draw_scene_overlay(ImDrawList* draw, ImVec2 origin, ImVec2 size) const {
        if (!draw || size.x <= 0.0f || size.y <= 0.0f || !scene_active) return;
        char buffer[256] = {};
        std::snprintf(buffer, sizeof(buffer), "x=%.1fm  y=%.1fm  d=%.1fm",
                      scene_camera_lateral_offset,
                      static_cast<double>(scene_camera_vertical_offset),
                      scene_camera_distance);
        const float pad = std::max(4.0f, ImGui::GetStyle().FramePadding.x);
        ImVec2 text_size = ImGui::CalcTextSize(buffer);
        ImVec2 pos(origin.x + pad * 2.0f, origin.y + pad * 2.0f);
        draw_scene_overlay_label(draw, pos, text_size, buffer, pad);
    }

    void draw_scene_metrics_overlay(ImDrawList* draw, ImVec2 origin, ImVec2 size,
                                    const Canvas3DSceneStats& stats) const {
        if (!draw || size.x <= 0.0f || size.y <= 0.0f || !scene_active) return;
        char buffer[128] = {};
        std::snprintf(buffer, sizeof(buffer), "chunks=%zu  instances=%zu  models=%zu/%zu  %.1f fps",
                      stats.chunk_count,
                      stats.drawn_instance_count,
                      stats.model_ready_count,
                      stats.model_path_count,
                      static_cast<double>(scene_fps_value));
        const float pad = std::max(4.0f, ImGui::GetStyle().FramePadding.x);
        ImVec2 text_size = ImGui::CalcTextSize(buffer);
        ImVec2 pos(origin.x + size.x - text_size.x - pad * 2.0f,
                   origin.y + size.y - text_size.y - pad * 2.0f);
        pos.x = std::max(origin.x + pad, pos.x);
        pos.y = std::max(origin.y + pad, pos.y);
        draw_scene_overlay_label(draw, pos, text_size, buffer, pad);
    }

    void draw_scene_loading_overlay(ImDrawList* draw, ImVec2 origin, ImVec2 size,
                                    const std::string& text) const {
        if (!draw || size.x <= 0.0f || size.y <= 0.0f) return;
        ImVec2 end(origin.x + size.x, origin.y + size.y);
        ImVec4 bg = clamp_background_color(background_color_value);
        bg.w = 1.0f;
        draw->AddRectFilled(origin, end, ImGui::ColorConvertFloat4ToU32(bg));

        const char* label = text.empty() ? "Loading..." : text.c_str();
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
        if (object.kind != Canvas3DSceneObjectKind::Structure) return nullptr;
        return object.structure_put_between ? "structure.between" : "structure.put";
    }

    Canvas3DSceneContextAction render_scene_context_popup(
        const Canvas3DSceneUiText& ui_text,
        const Canvas3DSceneContextMenuOptions& context_menu_options) {
        Canvas3DSceneContextAction action;
        if (!ImGui::BeginPopup("ScenePreviewObjectContext")) return action;

        if (scene_object_index_valid(scene_context_object_index)) {
            Canvas3DSceneObject& object = scene_data.objects[static_cast<size_t>(scene_context_object_index)];
            if (object.kind == Canvas3DSceneObjectKind::Structure) {
                const std::string& locate_label = object.structure_put_between
                    ? ui_text.locate_structure_put_between_list
                    : ui_text.locate_structure_list;
                if (ImGui::MenuItem(locate_label.c_str())) {
                    action.kind = Canvas3DSceneContextActionKind::LocateStructure;
                    action.row_index = object.source_row;
                }
                const char* edit_row_kind = scene_object_edit_row_kind(object);
                ImGui::BeginDisabled(!context_menu_options.element_properties_enabled ||
                                     object.edit_id.empty() || !edit_row_kind);
                if (ImGui::MenuItem(ui_text.element_properties.c_str())) {
                    action.kind = Canvas3DSceneContextActionKind::EditElement;
                    action.edit_id = object.edit_id;
                    action.row_kind = edit_row_kind;
                }
                ImGui::EndDisabled();
            } else if (object.kind == Canvas3DSceneObjectKind::Repeater) {
                if (ImGui::MenuItem(ui_text.locate_repeater_list.c_str())) {
                    action.kind = Canvas3DSceneContextActionKind::LocateRepeater;
                    action.row_index = object.source_row;
                }
                ImGui::Separator();
                if (ImGui::MenuItem(ui_text.jump_to_repeater_start_position.c_str())) {
                    jump_scene_camera_to_object(Canvas3DSceneObjectKind::Repeater, object.source_row);
                }
                const Canvas3DRepeaterSegment* repeater = find_repeater_segment(object.source_row);
                ImGui::BeginDisabled(!repeater || !repeater->has_end_or_change_position);
                if (ImGui::MenuItem(ui_text.jump_to_repeater_end_or_change_position.c_str())) {
                    jump_scene_camera_to_repeater_end_or_change(object.source_row);
                }
                ImGui::EndDisabled();
            } else if (object.kind == Canvas3DSceneObjectKind::Signal &&
                       ImGui::BeginMenu(ui_text.switch_signal_aspect.c_str(), !object.model_options.empty())) {
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

        ImGui::EndPopup();
        return action;
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
        const bool context_popup_open = ImGui::IsPopupOpen("ScenePreviewObjectContext");
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
            result.structure_drag = handle_scene_structure_gizmo_input(
                hovered, width, height, mouse_local);
            handle_scene_input(hovered, scene_structure_gizmo_consumes_left_input());
        }
        if (scene_active) {
            render_scene_preview_target(
                width, height, mouse_local,
                (hovered || context_popup_open) && !scene_structure_gizmo_consumes_left_input());
        } else {
            std::string error;
            ensure_render_target(width, height, error);
            if (render_rtv && context) {
                const ImVec4 bg = clamp_background_color(background_color_value);
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
            draw_scene_metrics_overlay(draw, origin, avail, stats);
            draw_scene_structure_gizmo(draw, origin, width, height);
        }

        const bool select_mode = scene_interaction_mode == Canvas3DSceneInteractionMode::Select;
        if (!stats.loading && scene_structure_gizmo_consumes_left_input()) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
        } else if (!stats.loading && select_mode && hovered && scene_hovered_object_index >= 0) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        }
        ImVec2 long_press_pos;
        const bool touch_context =
            select_mode && !stats.loading && scene_hovered_object_index >= 0 &&
            touch_input::consume_long_press_in_rect(origin, end, &long_press_pos);
        if (!stats.loading && select_mode && scene_hovered_object_index >= 0 &&
            ((hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) || touch_context)) {
            scene_context_object_index = scene_hovered_object_index;
            ImGui::OpenPopup("ScenePreviewObjectContext");
        }
        result.context_action = render_scene_context_popup(ui_text, context_menu_options);
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

        const ImVec4 bg = clamp_background_color(background_color_value);
        const float clear_color[4] = {bg.x, bg.y, bg.z, 1.0f};
        context->OMSetRenderTargets(1, &render_rtv, depth_dsv);
        context->ClearRenderTargetView(render_rtv, clear_color);
        context->ClearDepthStencilView(depth_dsv, D3D11_CLEAR_DEPTH, kSceneDepthClear, 0);

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
            Mat4 proj = perspective_fov_lh_reverse_z(kModelPreviewFovY, aspect, near_z, far_z);
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

        std::string file_name = path_filename_utf8(model_path_value);
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
    ID3D11InputLayout* scene_input_layout = nullptr;
    ID3D11Buffer* scene_constant_buffer = nullptr;
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
    ImVec4 background_color_value = ImVec4(0.0f, 0.0f, 0.0f, 1.0f);
    std::string last_error;
    Canvas3DScene scene_data;
    size_t scene_geometry_generation = 0;
    bool scene_active = false;
    std::vector<SceneChunk> scene_chunks;
    std::unordered_map<std::string, SceneStructureInstanceLocation> scene_structure_locations;
    SceneStructureEditState scene_structure_edit;
    std::vector<SceneTrackChunkGpu> scene_track_chunks;
    std::map<std::string, SceneModelGpu> scene_models;
    std::mutex scene_upload_mutex;
    std::vector<CpuModelData> scene_pending_uploads;
    std::mutex scene_log_mutex;
    std::vector<std::string> scene_pending_logs;
    bool scene_load_summary_pending = false;
    std::thread scene_worker;
    std::atomic<bool> scene_cancel{false};
    std::atomic<bool> scene_worker_running{false};
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
    int scene_context_object_index = -1;
    SceneHighlightBatch scene_hover_highlight_batch;
    SceneHighlightBatch scene_focus_highlight_batch;
    int scene_focus_highlight_object_index = -1;
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
    std::string scene_last_error;
    Canvas3DSceneStats scene_stats_value;
    std::chrono::steady_clock::time_point scene_fps_last_frame_at{};
    bool scene_fps_last_frame_valid = false;
    float scene_fps_value = 0.0f;
    ModelLoaderClient loader;
    ModelLoaderClient scene_loader;
};

Canvas3D::Canvas3D(ID3D11Device* device) : impl_(std::make_unique<Impl>(device)) {
}

Canvas3D::~Canvas3D() = default;

bool Canvas3D::load_model(const std::string& path, std::string& error) {
    return impl_->load_model(path, error);
}

bool Canvas3D::reload_model(std::string& error) {
    return impl_->reload_model(error);
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
    impl_->background_color_value = clamp_background_color(color);
}

ImVec4 Canvas3D::background_color() const {
    return impl_->background_color_value;
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

void Canvas3D::set_scene_interaction_mode(Canvas3DSceneInteractionMode mode) {
    impl_->set_scene_interaction_mode(mode);
}

Canvas3DSceneInteractionMode Canvas3D::scene_interaction_mode() const {
    return impl_->scene_interaction_mode_value();
}

Canvas3DSceneStats Canvas3D::scene_stats() const {
    return impl_->scene_stats();
}

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

bool Canvas3D::set_scene_structure_edit_target(const Canvas3DStructureEditTarget& target,
                                               bool show_gizmo) {
    return impl_->set_scene_structure_edit_target(target, show_gizmo);
}

bool Canvas3D::update_scene_structure_instance(const Canvas3DStructureEditTarget& target) {
    return impl_->update_scene_structure_instance(target);
}

void Canvas3D::clear_scene_structure_edit_target() {
    impl_->clear_scene_structure_edit_target();
}

Canvas3DSceneFrameResult Canvas3D::render_scene_preview(
    ImVec2 size,
    const Canvas3DSceneUiText& ui_text,
    const Canvas3DSceneContextMenuOptions& context_menu_options) {
    return impl_->render_scene_preview(size, ui_text, context_menu_options);
}
