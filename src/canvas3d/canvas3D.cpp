/*
 * Copyright (c) 2026 Sapporo_ningyo
 *
 * Licensed under Apache License 2.0; see LICENSE and NOTICE.
 */

#pragma execution_character_set("utf-8")

#include "canvas3D.h"

#include "model_loader.h"

#include "imgui.h"

#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wincodec.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <filesystem>
#include <mutex>
#include <limits>
#include <set>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {

constexpr float kDefaultSceneCameraHeight = 2.0f;

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

ImVec4 clamp_background_color(ImVec4 color) {
    color.x = clamp_color_component(color.x);
    color.y = clamp_color_component(color.y);
    color.z = clamp_color_component(color.z);
    color.w = 1.0f;
    return color;
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

std::filesystem::path executable_directory() {
    std::vector<wchar_t> buffer(MAX_PATH);
    while (true) {
        DWORD len = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (len == 0) break;
        if (len < buffer.size() - 1) {
            return std::filesystem::path(std::wstring(buffer.data(), len)).parent_path();
        }
        buffer.resize(buffer.size() * 2);
    }
    return std::filesystem::current_path();
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

Vec3 right_from_theta(double theta) {
    return {static_cast<float>(std::cos(theta)), 0.0f, static_cast<float>(std::sin(theta))};
}

Vec3 forward_from_theta(double theta) {
    return {static_cast<float>(std::sin(theta)), 0.0f, static_cast<float>(-std::cos(theta))};
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

struct SceneConstants {
    Mat4 mvp;
    float material_color[4];
    float use_texture[4];
};

struct SceneViewConstants {
    Mat4 view_proj;
    float material_color[4];
    float use_texture[4];
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
};

struct CpuMaterial {
    std::string texture_path;
    float diffuse[4] = {1.0f, 1.0f, 1.0f, 1.0f};
};

struct CpuModelData {
    std::string path;
    std::vector<GpuVertex> vertices;
    std::vector<unsigned int> indices;
    std::vector<MeshPart> parts;
    std::vector<CpuMaterial> materials;
    bool ok = false;
    std::string error;
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
    std::string error;
};

struct SceneTrackChunkGpu {
    double d_min = 0.0;
    double d_max = 0.0;
    ID3D11Buffer* vertex_buffer = nullptr;
    ID3D11Buffer* index_buffer = nullptr;
    ID3D11Buffer* instance_buffer = nullptr;
    UINT index_count = 0;
    std::vector<MeshPart> parts;
    std::vector<GpuMaterial> materials;
};

struct SceneInstance {
    std::string model_path;
    double distance = 0.0;
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
    std::vector<SceneInstance> instances;
    std::vector<size_t> repeater_indices;
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

int scene_tilt_flags(double tilt) {
    if (!std::isfinite(tilt)) return 0;
    return static_cast<int>(tilt);
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

const char* kShaderSource = R"(
cbuffer SceneConstants : register(b0)
{
    row_major float4x4 mvp;
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
};

struct VSOutput
{
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
};

VSOutput vs_main(VSInput input)
{
    VSOutput output;
    output.position = mul(float4(input.position, 1.0), mvp);
    output.texcoord = input.texcoord;
    return output;
}

float4 ps_main(VSOutput input) : SV_TARGET
{
    float4 color = materialColor;
    if (useTexture.x > 0.5)
        color *= diffuseTexture.Sample(diffuseSampler, input.texcoord);
    clip(color.a - 0.01);
    return color;
}
)";

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
    clip(color.a - 0.01);
    return color;
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

        std::filesystem::path dll_path = executable_directory() / L"model_loader.dll";
        library_ = LoadLibraryW(dll_path.c_str());
        if (!library_) {
            DWORD first_error = GetLastError();
            library_ = LoadLibraryW(L"model_loader.dll");
            if (!library_) {
                error = "model_loader.dll load failed: " + win32_error_text(first_error);
                return false;
            }
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
        release_com(blend_state);
        release_com(depth_state);
        release_com(track_rasterizer_state);
        release_com(rasterizer_state);
        release_com(input_layout);
        release_com(vertex_shader);
        release_com(pixel_shader);
        release_com(constant_buffer);
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
                materials[i].diffuse[3] = std::clamp(src->diffuse[3], 0.0f, 1.0f);
                if (src->texture_path && *src->texture_path) {
                    if (!load_texture(src->texture_path, &materials[i].texture, error)) {
                        release_resources();
                        return false;
                    }
                    materials[i].has_texture = true;
                }
            }
        }

        index_count = static_cast<UINT>(data.index_count);
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
        center = {};
        radius = 1.0f;
        yaw = 0.0f;
        pitch = 0.0f;
        distance_factor = 2.8f;
    }

    bool has_model() const {
        return vertex_buffer && index_buffer && index_count > 0;
    }

    bool load_scene(Canvas3DScene scene, std::string& error) {
        if (!device || !context) {
            error = "Direct3D device is not available";
            return false;
        }

        stop_scene_loader();
        release_scene_resources();
        scene_data = std::move(scene);
        std::sort(scene_data.backgrounds.begin(), scene_data.backgrounds.end(),
                  [](const Canvas3DBackgroundChange& a, const Canvas3DBackgroundChange& b) {
                      return a.distance < b.distance;
                  });
        if (scene_data.min_distance > scene_data.max_distance) {
            std::swap(scene_data.min_distance, scene_data.max_distance);
        }

        scene_camera_pos = {static_cast<float>(scene_data.camera.x),
                            static_cast<float>(scene_data.camera.y),
                            static_cast<float>(scene_data.camera.z)};
        scene_camera_yaw = static_cast<float>(scene_data.camera.yaw);
        scene_camera_pitch = static_cast<float>(scene_data.camera.pitch);
        scene_camera_distance = std::clamp(scene_data.camera.distance, scene_data.min_distance, scene_data.max_distance);
        reset_scene_camera_tracking();
        scene_active = true;

        build_scene_chunks();
        if (!build_scene_track_chunks(error)) {
            clear_scene();
            return false;
        }

        std::set<std::string> paths;
        for (const SceneChunk& chunk : scene_chunks) {
            for (const SceneInstance& instance : chunk.instances) {
                if (!instance.model_path.empty()) paths.insert(instance.model_path);
            }
        }
        for (const Canvas3DBackgroundChange& bg : scene_data.backgrounds) {
            if (!bg.model_path.empty()) paths.insert(bg.model_path);
        }
        for (const Canvas3DRepeaterSegment& repeater : scene_data.repeaters) {
            for (const std::string& path : repeater.model_paths) {
                if (!path.empty()) paths.insert(path);
            }
        }
        for (const std::string& path : paths) {
            scene_models[path] = SceneModelGpu{};
        }
        scene_stats_value.model_path_count = paths.size();
        scene_stats_value.instance_count = count_scene_instances();
        scene_stats_value.chunk_count = scene_chunks.size();
        scene_stats_value.window_back_m = scene_window_back_m;
        scene_stats_value.window_forward_m = scene_window_forward_m;
        scene_stats_value.camera_distance = scene_camera_distance;
        start_scene_model_worker(std::vector<std::string>(paths.begin(), paths.end()));
        return true;
    }

    void clear_scene() {
        stop_scene_loader();
        release_scene_resources();
        scene_data = {};
        scene_active = false;
        scene_stats_value = {};
    }

    bool has_scene() const {
        return scene_active;
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

    Canvas3DSceneStats scene_stats() const {
        Canvas3DSceneStats stats = scene_stats_value;
        stats.active = scene_active;
        stats.loading = scene_worker_running.load();
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
        return stats;
    }

    size_t count_scene_instances() const {
        size_t count = scene_data.instances.size();
        for (const Canvas3DRepeaterSegment& repeater : scene_data.repeaters) {
            if (repeater.model_paths.empty() || repeater.end_distance < repeater.begin_distance) continue;
            if (repeater.interval <= 1e-9 || !std::isfinite(repeater.interval)) {
                ++count;
                continue;
            }
            double span = repeater.end_distance - repeater.begin_distance;
            double estimated = std::floor(span / repeater.interval) + 1.0;
            if (estimated > 0.0 && std::isfinite(estimated)) {
                count += static_cast<size_t>(std::min<double>(estimated, 1000000.0));
            }
        }
        return count;
    }

    bool load_texture(const std::string& path, ID3D11ShaderResourceView** out_srv, std::string& error) {
        if (!out_srv) return false;
        *out_srv = nullptr;
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
        }
        model.materials.clear();
        model.parts.clear();
        release_com(model.vertex_buffer);
        release_com(model.index_buffer);
        release_com(model.instance_buffer);
        model.instance_capacity = 0;
        model.index_count = 0;
    }

    void release_track_chunk(SceneTrackChunkGpu& chunk) {
        for (GpuMaterial& material : chunk.materials) {
            release_com(material.texture);
            material.has_texture = false;
        }
        chunk.materials.clear();
        chunk.parts.clear();
        release_com(chunk.vertex_buffer);
        release_com(chunk.index_buffer);
        release_com(chunk.instance_buffer);
        chunk.index_count = 0;
    }

    void release_scene_track_chunks() {
        for (SceneTrackChunkGpu& chunk : scene_track_chunks) release_track_chunk(chunk);
        scene_track_chunks.clear();
    }

    void release_scene_resources() {
        for (auto& kv : scene_models) release_scene_model(kv.second);
        scene_models.clear();
        release_scene_track_chunks();
        scene_chunks.clear();
        {
            std::lock_guard<std::mutex> lock(scene_upload_mutex);
            scene_pending_uploads.clear();
        }
        scene_last_error.clear();
        scene_stats_value = {};
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
        if (data.vertex_count == 0 || data.index_count == 0 || !data.vertices || !data.indices) {
            out.error = "model contains no renderable data";
            return out;
        }
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
            out.materials[i].diffuse[3] = std::clamp(src->diffuse[3], 0.0f, 1.0f);
            if (src->texture_path && *src->texture_path) out.materials[i].texture_path = src->texture_path;
        }
        out.ok = true;
        return out;
    }

    void start_scene_model_worker(std::vector<std::string> paths) {
        if (paths.empty()) return;
        scene_worker_running.store(true);
        scene_worker = std::thread([this, paths = std::move(paths)]() {
            for (size_t path_index = 0; path_index < paths.size(); ++path_index) {
                const std::string& path = paths[path_index];
                if (scene_cancel.load()) break;
                CpuModelData cpu;
                cpu.path = path;
                MlMeshData data = {};
                std::string error;
                if (scene_loader.load(path, data, error)) {
                    cpu = copy_cpu_model(path, data);
                    scene_loader.free_model(data);
                } else {
                    cpu.error = error;
                }
                if (scene_cancel.load()) break;
                {
                    std::lock_guard<std::mutex> lock(scene_upload_mutex);
                    scene_pending_uploads.push_back(std::move(cpu));
                }
            }
            scene_worker_running.store(false);
        });
    }

    bool upload_scene_model(const CpuModelData& cpu, std::string& error) {
        auto it = scene_models.find(cpu.path);
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
            model.materials[i].diffuse[3] = src->diffuse[3];
            if (!src->texture_path.empty()) {
                std::string texture_error;
                if (load_texture(src->texture_path, &model.materials[i].texture, texture_error)) {
                    model.materials[i].has_texture = true;
                } else if (scene_last_error.empty()) {
                    scene_last_error = texture_error;
                }
            }
        }
        model.index_count = static_cast<UINT>(cpu.indices.size());
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
                auto it = scene_models.find(cpu.path);
                if (it != scene_models.end()) {
                    it->second.state = SceneModelGpu::State::Failed;
                    it->second.error = error;
                }
                if (!error.empty()) scene_last_error = error;
            }
        }
    }

    bool ensure_scene_pipeline(std::string& error) {
        if (!device || !context) {
            error = "Direct3D device is not available";
            return false;
        }
        if (scene_vertex_shader && scene_pixel_shader && scene_input_layout && scene_constant_buffer &&
            depth_state && rasterizer_state && sampler_state && blend_state) return true;

        if (!ensure_pipeline(error)) return false;

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

    static SceneInstanceData make_instance_data(const double world[16]) {
        SceneInstanceData data = {};
        for (int col = 0; col < 4; ++col) {
            data.world0[col] = static_cast<float>(world[col]);
            data.world1[col] = static_cast<float>(world[4 + col]);
            data.world2[col] = static_cast<float>(world[8 + col]);
            data.world3[col] = static_cast<float>(world[12 + col]);
        }
        return data;
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

    void draw_scene_mesh(ID3D11Buffer* vb, ID3D11Buffer* ib, ID3D11Buffer* instance_buffer,
                         const std::vector<MeshPart>& mesh_parts,
                         const std::vector<GpuMaterial>& mesh_materials,
                         UINT instance_count,
                         const Mat4& view_proj) {
        if (!vb || !ib || !instance_buffer || instance_count == 0) return;

        UINT strides[2] = {sizeof(GpuVertex), sizeof(SceneInstanceData)};
        UINT offsets[2] = {0, 0};
        ID3D11Buffer* buffers[2] = {vb, instance_buffer};
        context->IASetInputLayout(scene_input_layout);
        context->IASetVertexBuffers(0, 2, buffers, strides, offsets);
        context->IASetIndexBuffer(ib, DXGI_FORMAT_R32_UINT, 0);
        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context->VSSetShader(scene_vertex_shader, nullptr, 0);
        context->PSSetShader(scene_pixel_shader, nullptr, 0);
        context->VSSetConstantBuffers(0, 1, &scene_constant_buffer);
        context->PSSetConstantBuffers(0, 1, &scene_constant_buffer);
        context->PSSetSamplers(0, 1, &sampler_state);

        for (const MeshPart& part : mesh_parts) {
            const GpuMaterial* material = part.material_index < mesh_materials.size() ? &mesh_materials[part.material_index] : nullptr;
            SceneViewConstants constants = {};
            constants.view_proj = view_proj;
            constants.material_color[0] = material ? material->diffuse[0] : 1.0f;
            constants.material_color[1] = material ? material->diffuse[1] : 1.0f;
            constants.material_color[2] = material ? material->diffuse[2] : 1.0f;
            constants.material_color[3] = material ? material->diffuse[3] : 1.0f;
            constants.use_texture[0] = material && material->has_texture ? 1.0f : 0.0f;
            context->UpdateSubresource(scene_constant_buffer, 0, nullptr, &constants, 0, 0);
            ID3D11ShaderResourceView* texture = material && material->has_texture ? material->texture : nullptr;
            context->PSSetShaderResources(0, 1, &texture);
            context->DrawIndexedInstanced(part.index_count, instance_count, part.start_index, 0, 0);
        }
        ID3D11ShaderResourceView* null_srv = nullptr;
        context->PSSetShaderResources(0, 1, &null_srv);
    }

    void draw_scene_model(SceneModelGpu& model, const std::vector<SceneInstanceData>& instances, const Mat4& view_proj) {
        if (model.state != SceneModelGpu::State::Ready || instances.empty()) return;
        std::string error;
        if (!ensure_instance_buffer(model.instance_buffer, model.instance_capacity, instances, error)) {
            scene_last_error = error;
            return;
        }
        draw_scene_mesh(model.vertex_buffer, model.index_buffer, model.instance_buffer,
                        model.parts, model.materials, static_cast<UINT>(instances.size()), view_proj);
    }

    bool ensure_pipeline(std::string& error) {
        if (!device || !context) {
            error = "Direct3D device is not available";
            return false;
        }
        if (vertex_shader && pixel_shader && input_layout && constant_buffer && depth_state &&
            rasterizer_state && track_rasterizer_state && sampler_state && blend_state) return true;

        ID3DBlob* vs_blob = nullptr;
        ID3DBlob* ps_blob = nullptr;
        ID3DBlob* errors = nullptr;
        HRESULT hr = D3DCompile(kShaderSource, std::strlen(kShaderSource), nullptr, nullptr, nullptr,
                                "vs_main", "vs_4_0", D3DCOMPILE_ENABLE_STRICTNESS, 0, &vs_blob, &errors);
        if (FAILED(hr)) {
            error = errors ? static_cast<const char*>(errors->GetBufferPointer()) : hresult_text("D3DCompile(vertex shader)", hr);
            release_com(errors);
            return false;
        }
        release_com(errors);

        hr = D3DCompile(kShaderSource, std::strlen(kShaderSource), nullptr, nullptr, nullptr,
                        "ps_main", "ps_4_0", D3DCOMPILE_ENABLE_STRICTNESS, 0, &ps_blob, &errors);
        if (FAILED(hr)) {
            error = errors ? static_cast<const char*>(errors->GetBufferPointer()) : hresult_text("D3DCompile(pixel shader)", hr);
            release_com(errors);
            release_com(vs_blob);
            return false;
        }
        release_com(errors);

        hr = device->CreateVertexShader(vs_blob->GetBufferPointer(), vs_blob->GetBufferSize(), nullptr, &vertex_shader);
        if (FAILED(hr)) {
            error = hresult_text("CreateVertexShader", hr);
            release_com(vs_blob);
            release_com(ps_blob);
            return false;
        }
        hr = device->CreatePixelShader(ps_blob->GetBufferPointer(), ps_blob->GetBufferSize(), nullptr, &pixel_shader);
        if (FAILED(hr)) {
            error = hresult_text("CreatePixelShader", hr);
            release_com(vs_blob);
            release_com(ps_blob);
            return false;
        }

        D3D11_INPUT_ELEMENT_DESC layout[] = {
            {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0},
        };
        hr = device->CreateInputLayout(layout, 3, vs_blob->GetBufferPointer(), vs_blob->GetBufferSize(), &input_layout);
        release_com(vs_blob);
        release_com(ps_blob);
        if (FAILED(hr)) {
            error = hresult_text("CreateInputLayout", hr);
            return false;
        }

        D3D11_BUFFER_DESC cb_desc = {};
        cb_desc.ByteWidth = sizeof(SceneConstants);
        cb_desc.Usage = D3D11_USAGE_DEFAULT;
        cb_desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        hr = device->CreateBuffer(&cb_desc, nullptr, &constant_buffer);
        if (FAILED(hr)) {
            error = hresult_text("CreateBuffer(constants)", hr);
            return false;
        }

        D3D11_DEPTH_STENCIL_DESC ds_desc = {};
        ds_desc.DepthEnable = TRUE;
        ds_desc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
        ds_desc.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
        hr = device->CreateDepthStencilState(&ds_desc, &depth_state);
        if (FAILED(hr)) {
            error = hresult_text("CreateDepthStencilState", hr);
            return false;
        }

        D3D11_RASTERIZER_DESC rs_desc = {};
        rs_desc.FillMode = D3D11_FILL_SOLID;
        rs_desc.CullMode = D3D11_CULL_BACK;
        rs_desc.FrontCounterClockwise = TRUE;
        rs_desc.DepthClipEnable = TRUE;
        hr = device->CreateRasterizerState(&rs_desc, &rasterizer_state);
        if (FAILED(hr)) {
            error = hresult_text("CreateRasterizerState", hr);
            return false;
        }
        rs_desc.CullMode = D3D11_CULL_NONE;
        hr = device->CreateRasterizerState(&rs_desc, &track_rasterizer_state);
        if (FAILED(hr)) {
            error = hresult_text("CreateRasterizerState(track)", hr);
            return false;
        }

        D3D11_SAMPLER_DESC sampler_desc = {};
        sampler_desc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        sampler_desc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
        sampler_desc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
        sampler_desc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
        sampler_desc.ComparisonFunc = D3D11_COMPARISON_NEVER;
        sampler_desc.MinLOD = 0.0f;
        sampler_desc.MaxLOD = D3D11_FLOAT32_MAX;
        hr = device->CreateSamplerState(&sampler_desc, &sampler_state);
        if (FAILED(hr)) {
            error = hresult_text("CreateSamplerState", hr);
            return false;
        }

        D3D11_BLEND_DESC blend_desc = {};
        blend_desc.RenderTarget[0].BlendEnable = TRUE;
        blend_desc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
        blend_desc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
        blend_desc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
        blend_desc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
        blend_desc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
        blend_desc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
        blend_desc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        hr = device->CreateBlendState(&blend_desc, &blend_state);
        if (FAILED(hr)) {
            error = hresult_text("CreateBlendState", hr);
            return false;
        }

        return true;
    }

    void build_scene_chunks() {
        scene_chunks.clear();
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
        }
        for (const Canvas3DModelInstance& source : scene_data.instances) {
            if (source.model_path.empty()) continue;
            int index = static_cast<int>(std::floor((source.distance - first) / scene_chunk_m));
            index = std::clamp(index, 0, static_cast<int>(scene_chunks.size()) - 1);
            SceneInstance instance;
            instance.model_path = source.model_path;
            instance.distance = source.distance;
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
            scene_chunks[static_cast<size_t>(index)].instances.push_back(std::move(instance));
        }
        for (size_t repeater_index = 0; repeater_index < scene_data.repeaters.size(); ++repeater_index) {
            const Canvas3DRepeaterSegment& repeater = scene_data.repeaters[repeater_index];
            if (repeater.model_paths.empty() || repeater.end_distance < repeater.begin_distance) continue;
            int begin_index = static_cast<int>(std::floor((repeater.begin_distance - first) / scene_chunk_m));
            int end_index = static_cast<int>(std::floor((repeater.end_distance - first) / scene_chunk_m));
            begin_index = std::clamp(begin_index, 0, static_cast<int>(scene_chunks.size()) - 1);
            end_index = std::clamp(end_index, 0, static_cast<int>(scene_chunks.size()) - 1);
            for (int index = begin_index; index <= end_index; ++index) {
                scene_chunks[static_cast<size_t>(index)].repeater_indices.push_back(repeater_index);
            }
        }
    }

    static void append_track_quad(std::vector<GpuVertex>& vertices,
                                  std::vector<unsigned int>& indices,
                                  Vec3 a, Vec3 b, Vec3 side, float half_width) {
        if (vertices.size() > static_cast<size_t>(std::numeric_limits<unsigned int>::max() - 4)) return;
        Vec3 n = {0.0f, 1.0f, 0.0f};
        unsigned int base = static_cast<unsigned int>(vertices.size());
        Vec3 a0 = a - side * half_width;
        Vec3 a1 = a + side * half_width;
        Vec3 b0 = b - side * half_width;
        Vec3 b1 = b + side * half_width;
        vertices.push_back({a0.x, a0.y, a0.z, n.x, n.y, n.z, 0.0f, 0.0f});
        vertices.push_back({a1.x, a1.y, a1.z, n.x, n.y, n.z, 1.0f, 0.0f});
        vertices.push_back({b1.x, b1.y, b1.z, n.x, n.y, n.z, 1.0f, 1.0f});
        vertices.push_back({b0.x, b0.y, b0.z, n.x, n.y, n.z, 0.0f, 1.0f});
        indices.insert(indices.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
    }

    static void append_track_segment(std::vector<GpuVertex>& vertices,
                                     std::vector<unsigned int>& indices,
                                     const Canvas3DTrackPoint& p0,
                                     const Canvas3DTrackPoint& p1) {
        constexpr float rail_gauge_half = 0.5335f;
        constexpr float rail_half_width = 0.035f;
        constexpr float rail_lift = 0.035f;
        Vec3 right0 = right_from_theta(p0.theta);
        Vec3 right1 = right_from_theta(p1.theta);
        Vec3 center0{static_cast<float>(p0.x), static_cast<float>(p0.y + rail_lift), static_cast<float>(p0.z)};
        Vec3 center1{static_cast<float>(p1.x), static_cast<float>(p1.y + rail_lift), static_cast<float>(p1.z)};
        for (float rail_offset : {-rail_gauge_half, rail_gauge_half}) {
            Vec3 a = center0 + right0 * rail_offset;
            Vec3 b = center1 + right1 * rail_offset;
            Vec3 segment = b - a;
            Vec3 side = normalize(cross({0.0f, 1.0f, 0.0f}, segment));
            append_track_quad(vertices, indices, a, b, side, rail_half_width);
        }
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

        SceneInstanceData identity_instance = make_instance_data(identity());
        D3D11_BUFFER_DESC inst_desc = {};
        inst_desc.ByteWidth = sizeof(SceneInstanceData);
        inst_desc.Usage = D3D11_USAGE_DEFAULT;
        inst_desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        D3D11_SUBRESOURCE_DATA inst_data = {};
        inst_data.pSysMem = &identity_instance;
        hr = device->CreateBuffer(&inst_desc, &inst_data, &chunk.instance_buffer);
        if (FAILED(hr)) {
            error = hresult_text("CreateBuffer(track instance)", hr);
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
                material.diffuse[3] = 1.0f;
                gpu_chunk.materials.push_back(material);

                for (size_t i = 1; i < track.points.size(); ++i) {
                    const Canvas3DTrackPoint& a = track.points[i - 1];
                    const Canvas3DTrackPoint& b = track.points[i];
                    if (b.distance < scene_chunk.d_min || a.distance > scene_chunk.d_max) continue;
                    append_track_segment(vertices, indices, a, b);
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
        depth_desc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
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
        out.cant_angle = a.cant_angle + (b.cant_angle - a.cant_angle) * t;
        return true;
    }

    bool sample_own_track(double distance, Canvas3DTrackPoint& out) const {
        const Canvas3DTrackPath* path = own_track_path();
        return path && sample_track_path(*path, distance, out);
    }

    const Canvas3DTrackPath* track_path_for_key(const std::string& key) const {
        std::string normalized = normalize_scene_track_key(key);
        if (is_scene_own_track_alias(normalized)) return own_track_path();
        for (const Canvas3DTrackPath& path : scene_data.tracks) {
            if (normalize_scene_track_key(path.key) == normalized) return &path;
        }
        return nullptr;
    }

    bool sample_scene_track(const std::string& key, double distance, Canvas3DTrackPoint& out) const {
        const Canvas3DTrackPath* path = track_path_for_key(key);
        return path && sample_track_path(*path, distance, out);
    }

    static void apply_track_cant(DVec3& right, DVec3& up, const DVec3& forward, double cant_angle) {
        if (std::abs(cant_angle) <= 1e-9 || !std::isfinite(cant_angle)) return;
        DVec3 axis = forward * -1.0;
        right = rotate_axis(right, axis, -cant_angle);
        up = rotate_axis(up, axis, -cant_angle);
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
        Canvas3DTrackPoint point;
        if (!sample_scene_track(track_key, distance, point)) return false;

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
        if (sample_scene_track(track_key, distance + effective_span, span_point)) {
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
            if (sample_scene_track(track_key, distance + effective_span * 0.5, mid_point)) {
                cant_angle = mid_point.cant_angle;
            }
            apply_track_cant(right, up, forward, cant_angle);
        }

        origin = origin + forward * z;
        apply_euler(right, up, forward, rx, ry, rz);
        store_world(out_world, right, up, forward, origin);
        return true;
    }

    bool make_repeater_instance_data(const Canvas3DRepeaterSegment& repeater,
                                     double distance,
                                     SceneInstanceData& out) const {
        double world[16] = {};
        if (!make_track_world(repeater.track_key, distance,
                              repeater.x, repeater.y, repeater.z,
                              repeater.rx, repeater.ry, repeater.rz,
                              repeater.tilt, repeater.span,
                              world)) {
            return false;
        }
        out = make_instance_data(world);
        return true;
    }

    void append_visible_repeater_instances(const SceneChunk& chunk,
                                           double visible_min,
                                           double visible_max,
                                           std::map<std::string, std::vector<SceneInstanceData>>& visible_instances) const {
        constexpr double eps = 1e-6;
        double chunk_max = chunk.d_max;
        if (chunk.d_max < scene_data.max_distance) chunk_max -= eps;
        double range_min = std::max(visible_min, chunk.d_min);
        double range_max = std::min(visible_max, chunk_max);
        if (range_max < range_min) return;

        for (size_t repeater_index : chunk.repeater_indices) {
            if (repeater_index >= scene_data.repeaters.size()) continue;
            const Canvas3DRepeaterSegment& repeater = scene_data.repeaters[repeater_index];
            if (repeater.model_paths.empty() || repeater.end_distance < repeater.begin_distance) continue;

            const double begin = std::max(range_min, repeater.begin_distance);
            const double end = std::min(range_max, repeater.end_distance);
            if (end < begin - eps) continue;

            auto emit = [&](double distance, size_t model_index) {
                const std::string& path = repeater.model_paths[model_index % repeater.model_paths.size()];
                if (path.empty()) return;
                SceneInstanceData data;
                if (make_repeater_instance_data(repeater, distance, data)) {
                    visible_instances[path].push_back(data);
                }
            };

            if (repeater.interval <= 1e-9 || !std::isfinite(repeater.interval)) {
                if (repeater.begin_distance >= begin - eps && repeater.begin_distance <= end + eps) {
                    emit(repeater.begin_distance, 0);
                }
                continue;
            }

            double first_index_d = std::ceil((begin - repeater.begin_distance - eps) / repeater.interval);
            double last_index_d = std::floor((end - repeater.begin_distance + eps) / repeater.interval);
            if (!std::isfinite(first_index_d) || !std::isfinite(last_index_d)) continue;
            long long first_index = std::max<long long>(0, static_cast<long long>(first_index_d));
            long long last_index = std::max<long long>(-1, static_cast<long long>(last_index_d));
            long long emitted = 0;
            for (long long index = first_index; index <= last_index; ++index) {
                double distance = repeater.begin_distance + static_cast<double>(index) * repeater.interval;
                if (distance < begin - eps) continue;
                if (distance > end + eps) break;
                emit(distance, static_cast<size_t>(index));
                if (++emitted > 1000000) break;
            }
        }
    }

    bool update_scene_camera_from_owntrack() {
        Canvas3DTrackPoint point;
        if (!sample_own_track(scene_camera_distance, point)) return false;
        Vec3 base{static_cast<float>(point.x), static_cast<float>(point.y), static_cast<float>(point.z)};
        Vec3 right = right_from_theta(point.theta);
        scene_camera_pos = base + right * static_cast<float>(scene_camera_lateral_offset);
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

        Vec3 base{static_cast<float>(point.x), static_cast<float>(point.y), static_cast<float>(point.z)};
        Vec3 current = scene_camera_pos;
        Vec3 diff = current - base;
        scene_camera_lateral_offset = dot(diff, right_from_theta(point.theta));
        scene_camera_vertical_offset = diff.y;
        scene_camera_yaw_offset = scene_camera_yaw - point.theta;
        update_scene_camera_from_owntrack();
    }

    void handle_scene_input(bool hovered) {
        if (!hovered) {
            scene_rotating = false;
            return;
        }
        ImGuiIO& io = ImGui::GetIO();
        if (ImGui::IsKeyPressed(ImGuiKey_X, false)) {
            reset_scene_camera_pose_at_distance(scene_camera_distance);
        }

        if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
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
            Vec3 delta{};
            delta = delta + forward * distance_delta;
            delta = delta + right * lateral_delta;
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
        Mat4 world = translation(scene_camera_pos.x, scene_camera_pos.y, scene_camera_pos.z);
        std::vector<SceneInstanceData> instances{make_instance_data(world)};
        draw_scene_model(it->second, instances, view_proj);
    }

    void render_scene_preview_target(int width, int height) {
        std::string error;
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
        context->ClearDepthStencilView(depth_dsv, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);

        D3D11_VIEWPORT viewport = {};
        viewport.Width = static_cast<float>(width);
        viewport.Height = static_cast<float>(height);
        viewport.MinDepth = 0.0f;
        viewport.MaxDepth = 1.0f;
        context->RSSetViewports(1, &viewport);
        context->RSSetState(rasterizer_state);
        context->OMSetDepthStencilState(depth_state, 0);
        const float blend_factor[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        context->OMSetBlendState(blend_state, blend_factor, 0xffffffff);

        Vec3 forward = scene_forward();
        Mat4 view = look_to_bve(scene_camera_pos, forward, {0.0f, 1.0f, 0.0f});
        float aspect = static_cast<float>(width) / std::max(1.0f, static_cast<float>(height));
        Mat4 proj = perspective_fov_lh(1.0471975512f, aspect, 0.05f, 5000.0f);
        Mat4 view_proj = multiply(view, proj);

        scene_stats_value.drawn_instance_count = 0;
        scene_stats_value.drawn_track_chunk_count = 0;
        scene_stats_value.camera_distance = scene_camera_distance;

        draw_background_model(view_proj);
        context->ClearDepthStencilView(depth_dsv, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);

        double visible_min = scene_camera_distance - scene_window_back_m;
        double visible_max = scene_camera_distance + scene_window_forward_m;
        std::map<std::string, std::vector<SceneInstanceData>> visible_instances;
        for (size_t i = 0; i < scene_chunks.size(); ++i) {
            const SceneChunk& chunk = scene_chunks[i];
            if (chunk.d_max < visible_min || chunk.d_min > visible_max) continue;
            if (i < scene_track_chunks.size()) {
                SceneTrackChunkGpu& track = scene_track_chunks[i];
                if (track.vertex_buffer && track.index_buffer && track.instance_buffer && track.index_count > 0) {
                    context->RSSetState(track_rasterizer_state);
                    draw_scene_mesh(track.vertex_buffer, track.index_buffer, track.instance_buffer,
                                    track.parts, track.materials, 1, view_proj);
                    context->RSSetState(rasterizer_state);
                    ++scene_stats_value.drawn_track_chunk_count;
                }
            }
            for (const SceneInstance& instance : chunk.instances) {
                if (instance.distance < visible_min || instance.distance > visible_max) continue;
                visible_instances[instance.model_path].push_back(make_instance_data(instance.world));
            }
            append_visible_repeater_instances(chunk, visible_min, visible_max, visible_instances);
        }

        for (auto& kv : visible_instances) {
            auto model_it = scene_models.find(kv.first);
            if (model_it == scene_models.end()) continue;
            draw_scene_model(model_it->second, kv.second, view_proj);
            if (model_it->second.state == SceneModelGpu::State::Ready) {
                scene_stats_value.drawn_instance_count += kv.second.size();
            }
        }

        ID3D11ShaderResourceView* null_srv = nullptr;
        context->PSSetShaderResources(0, 1, &null_srv);
        context->OMSetBlendState(nullptr, nullptr, 0xffffffff);
        ID3D11RenderTargetView* null_rtv = nullptr;
        context->OMSetRenderTargets(1, &null_rtv, nullptr);
    }

    void draw_scene_overlay(ImDrawList* draw, ImVec2 origin, ImVec2 size) const {
        if (!draw || size.x <= 0.0f || size.y <= 0.0f || !scene_active) return;
        Canvas3DSceneStats stats = scene_stats();
        char buffer[256] = {};
        std::snprintf(buffer, sizeof(buffer), "d=%.1fm  chunks=%zu  instances=%zu  models=%zu/%zu",
                      stats.camera_distance,
                      stats.drawn_track_chunk_count,
                      stats.drawn_instance_count,
                      stats.model_ready_count,
                      stats.model_path_count);
        const float pad = std::max(4.0f, ImGui::GetStyle().FramePadding.x);
        ImVec2 text_size = ImGui::CalcTextSize(buffer);
        ImVec2 pos(origin.x + pad * 2.0f, origin.y + pad * 2.0f);
        draw->AddRectFilled(ImVec2(pos.x - pad, pos.y - pad * 0.5f),
                            ImVec2(pos.x + text_size.x + pad, pos.y + text_size.y + pad * 0.5f),
                            IM_COL32(0, 0, 0, 140), 3.0f);
        draw->AddText(pos, IM_COL32(255, 255, 255, 230), buffer);
    }

    void render_scene_preview(ImVec2 requested_size) {
        ImVec2 avail = requested_size;
        if (avail.x <= 0.0f || avail.y <= 0.0f) avail = ImGui::GetContentRegionAvail();
        avail.x = std::max(avail.x, 50.0f);
        avail.y = std::max(avail.y, 50.0f);

        ImVec2 origin = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton("ScenePreview3DCanvas", avail, ImGuiButtonFlags_MouseButtonLeft);
        bool hovered = ImGui::IsItemHovered();
        if (scene_active) handle_scene_input(hovered);

        int width = std::max(1, static_cast<int>(std::round(avail.x)));
        int height = std::max(1, static_cast<int>(std::round(avail.y)));
        if (scene_active) {
            render_scene_preview_target(width, height);
        } else {
            std::string error;
            ensure_render_target(width, height, error);
            if (render_rtv && context) {
                const ImVec4 bg = clamp_background_color(background_color_value);
                const float clear_color[4] = {bg.x, bg.y, bg.z, 1.0f};
                context->OMSetRenderTargets(1, &render_rtv, depth_dsv);
                context->ClearRenderTargetView(render_rtv, clear_color);
                if (depth_dsv) context->ClearDepthStencilView(depth_dsv, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);
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
        draw_scene_overlay(draw, origin, avail);
    }

    void render_scene(int width, int height) {
        std::string error;
        if (!ensure_render_target(width, height, error)) {
            if (last_error != error) last_error = error;
            return;
        }
        if (has_model() && !ensure_pipeline(error)) {
            if (last_error != error) last_error = error;
        }

        const ImVec4 bg = clamp_background_color(background_color_value);
        const float clear_color[4] = {bg.x, bg.y, bg.z, 1.0f};
        context->OMSetRenderTargets(1, &render_rtv, depth_dsv);
        context->ClearRenderTargetView(render_rtv, clear_color);
        context->ClearDepthStencilView(depth_dsv, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);

        if (has_model() && vertex_shader && pixel_shader && input_layout && constant_buffer && blend_state) {
            D3D11_VIEWPORT viewport = {};
            viewport.Width = static_cast<float>(width);
            viewport.Height = static_cast<float>(height);
            viewport.MinDepth = 0.0f;
            viewport.MaxDepth = 1.0f;
            context->RSSetViewports(1, &viewport);
            context->RSSetState(rasterizer_state);
            context->OMSetDepthStencilState(depth_state, 0);
            const float blend_factor[4] = {0.0f, 0.0f, 0.0f, 0.0f};
            context->OMSetBlendState(blend_state, blend_factor, 0xffffffff);

            Mat4 center_transform = translation(-center.x, -center.y, -center.z);
            Mat4 rotation = multiply(rotation_y(yaw), rotation_x(pitch));
            Mat4 world = multiply(center_transform, rotation);
            float distance = std::max(radius * distance_factor, radius + 0.1f);
            Mat4 view = look_at_lh({0.0f, 0.0f, -distance}, {0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f});
            float aspect = static_cast<float>(width) / std::max(1.0f, static_cast<float>(height));
            float near_z = std::max(0.001f, radius * 0.001f);
            float far_z = std::max(distance + radius * 4.0f, radius * 50.0f);
            Mat4 proj = perspective_fov_lh(0.78539816339f, aspect, near_z, far_z);
            SceneConstants constants = {};
            constants.mvp = multiply(multiply(world, view), proj);

            UINT stride = sizeof(GpuVertex);
            UINT offset = 0;
            context->IASetInputLayout(input_layout);
            context->IASetVertexBuffers(0, 1, &vertex_buffer, &stride, &offset);
            context->IASetIndexBuffer(index_buffer, DXGI_FORMAT_R32_UINT, 0);
            context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            context->VSSetShader(vertex_shader, nullptr, 0);
            context->PSSetShader(pixel_shader, nullptr, 0);
            context->VSSetConstantBuffers(0, 1, &constant_buffer);
            context->PSSetConstantBuffers(0, 1, &constant_buffer);
            context->PSSetSamplers(0, 1, &sampler_state);

            for (const MeshPart& part : parts) {
                const GpuMaterial* material = nullptr;
                if (part.material_index < materials.size()) material = &materials[part.material_index];
                constants.material_color[0] = material ? material->diffuse[0] : 1.0f;
                constants.material_color[1] = material ? material->diffuse[1] : 1.0f;
                constants.material_color[2] = material ? material->diffuse[2] : 1.0f;
                constants.material_color[3] = material ? material->diffuse[3] : 1.0f;
                constants.use_texture[0] = material && material->has_texture ? 1.0f : 0.0f;
                context->UpdateSubresource(constant_buffer, 0, nullptr, &constants, 0, 0);
                ID3D11ShaderResourceView* texture = material && material->has_texture ? material->texture : nullptr;
                context->PSSetShaderResources(0, 1, &texture);
                context->DrawIndexed(part.index_count, part.start_index, 0);
            }
            ID3D11ShaderResourceView* null_srv = nullptr;
            context->PSSetShaderResources(0, 1, &null_srv);
            context->OMSetBlendState(nullptr, nullptr, 0xffffffff);
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
        }
        materials.clear();
        parts.clear();
        release_com(vertex_buffer);
        release_com(index_buffer);
        index_count = 0;
    }

    void release_render_target() {
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
    ID3D11VertexShader* vertex_shader = nullptr;
    ID3D11PixelShader* pixel_shader = nullptr;
    ID3D11InputLayout* input_layout = nullptr;
    ID3D11Buffer* constant_buffer = nullptr;
    ID3D11VertexShader* scene_vertex_shader = nullptr;
    ID3D11PixelShader* scene_pixel_shader = nullptr;
    ID3D11InputLayout* scene_input_layout = nullptr;
    ID3D11Buffer* scene_constant_buffer = nullptr;
    ID3D11SamplerState* sampler_state = nullptr;
    ID3D11DepthStencilState* depth_state = nullptr;
    ID3D11RasterizerState* rasterizer_state = nullptr;
    ID3D11RasterizerState* track_rasterizer_state = nullptr;
    ID3D11BlendState* blend_state = nullptr;
    ID3D11Buffer* vertex_buffer = nullptr;
    ID3D11Buffer* index_buffer = nullptr;
    std::vector<MeshPart> parts;
    std::vector<GpuMaterial> materials;
    UINT index_count = 0;
    int render_width = 0;
    int render_height = 0;
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
    bool scene_active = false;
    std::vector<SceneChunk> scene_chunks;
    std::vector<SceneTrackChunkGpu> scene_track_chunks;
    std::map<std::string, SceneModelGpu> scene_models;
    std::mutex scene_upload_mutex;
    std::vector<CpuModelData> scene_pending_uploads;
    std::thread scene_worker;
    std::atomic<bool> scene_cancel{false};
    std::atomic<bool> scene_worker_running{false};
    Vec3 scene_camera_pos;
    float scene_camera_yaw = 0.0f;
    float scene_camera_pitch = 0.0f;
    double scene_camera_distance = 0.0;
    double scene_camera_lateral_offset = 0.0;
    float scene_camera_vertical_offset = 2.0f;
    float scene_camera_yaw_offset = 0.0f;
    bool scene_rotating = false;
    ImVec2 scene_last_mouse = ImVec2(0.0f, 0.0f);
    double scene_chunk_m = 100.0;
    double scene_window_back_m = 100.0;
    double scene_window_forward_m = 1200.0;
    float scene_slow_speed_mps = 8.0f;
    float scene_fast_multiplier = 10.0f;
    std::string scene_last_error;
    Canvas3DSceneStats scene_stats_value;
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

bool Canvas3D::load_scene(Canvas3DScene scene, std::string& error) {
    return impl_->load_scene(std::move(scene), error);
}

void Canvas3D::clear_scene() {
    impl_->clear_scene();
}

bool Canvas3D::has_scene() const {
    return impl_->has_scene();
}

bool Canvas3D::set_scene_track_visibility(const std::vector<Canvas3DTrackVisibility>& visibility, std::string& error) {
    return impl_->set_scene_track_visibility(visibility, error);
}

void Canvas3D::set_scene_window(double back_m, double forward_m) {
    impl_->set_scene_window(back_m, forward_m);
}

Canvas3DSceneStats Canvas3D::scene_stats() const {
    return impl_->scene_stats();
}

bool Canvas3D::jump_scene_camera_to_distance(double distance) {
    return impl_->reset_scene_camera_pose_at_distance(distance);
}

void Canvas3D::render_scene_preview(ImVec2 size) {
    impl_->render_scene_preview(size);
}
