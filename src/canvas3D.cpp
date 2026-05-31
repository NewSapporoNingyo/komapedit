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
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <limits>
#include <string>
#include <vector>

namespace {

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

using MlApiVersionFn = unsigned int (*)();
using MlLoadModelFn = int (*)(const char*, MlMeshData*);
using MlFreeModelFn = void (*)(MlMeshData*);
using MlGetLastErrorFn = const char* (*)();

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

class ModelLoaderClient {
public:
    ~ModelLoaderClient() {
        if (library_) FreeLibrary(library_);
    }

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
        release_resources();
        release_render_target();
        release_com(blend_state);
        release_com(depth_state);
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

    bool ensure_pipeline(std::string& error) {
        if (!device || !context) {
            error = "Direct3D device is not available";
            return false;
        }
        if (vertex_shader && pixel_shader && input_layout && constant_buffer && depth_state &&
            rasterizer_state && sampler_state && blend_state) return true;

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
        rs_desc.CullMode = D3D11_CULL_NONE;
        rs_desc.DepthClipEnable = TRUE;
        hr = device->CreateRasterizerState(&rs_desc, &rasterizer_state);
        if (FAILED(hr)) {
            error = hresult_text("CreateRasterizerState", hr);
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
    ID3D11SamplerState* sampler_state = nullptr;
    ID3D11DepthStencilState* depth_state = nullptr;
    ID3D11RasterizerState* rasterizer_state = nullptr;
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
    ModelLoaderClient loader;
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
