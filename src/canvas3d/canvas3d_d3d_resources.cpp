/*
 * Copyright (c) 2026 Sapporo_ningyo
 *
 * Licensed under Apache License 2.0; see LICENSE and NOTICE.
 */

#ifdef _MSC_VER
#pragma execution_character_set("utf-8")
#endif

#include "canvas3d_impl.h"
#include "kme.h"
#include "scene_frame_profile.h"
#include "scene_shader_source.h"
#include <d3d11.h>
#include <d3dcompiler.h>
#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <string>
#include <utility>
#include <vector>

using namespace canvas3d_detail;

namespace {

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

} // namespace

namespace canvas3d_detail {

constexpr float k_scene_mileage_highlight_alpha = 0.5f;

} // namespace canvas3d_detail

namespace canvas3d_detail {

constexpr float k_scene_mileage_highlight_bottom = -2.0f;

} // namespace canvas3d_detail

namespace canvas3d_detail {

constexpr float k_scene_mileage_highlight_top = 1.0f;

} // namespace canvas3d_detail

namespace {
template <typename T>
class SceneComResource {
public:
    SceneComResource() = default;
    SceneComResource(const SceneComResource&) = delete;
    SceneComResource& operator=(const SceneComResource&) = delete;
    ~SceneComResource() { release_com(resource_); }

    T** output() { return &resource_; }
    T* get() const { return resource_; }
    T* operator->() const { return resource_; }
    explicit operator bool() const { return resource_ != nullptr; }
    T* take() { return std::exchange(resource_, nullptr); }

private:
    T* resource_ = nullptr;
};

bool scene_d3d_succeeded(HRESULT hr, const char* operation, std::string& error) {
    if (SUCCEEDED(hr)) return true;
    error = hresult_text(operation, hr);
    return false;
}

bool compile_scene_shader(const char* source, const char* entry, const char* profile,
                          const char* operation, SceneComResource<ID3DBlob>& bytecode,
                          std::string& error) {
    SceneComResource<ID3DBlob> diagnostics;
    const HRESULT hr = D3DCompile(source, std::strlen(source), nullptr, nullptr, nullptr,
        entry, profile, D3DCOMPILE_ENABLE_STRICTNESS, 0, bytecode.output(), diagnostics.output());
    if (SUCCEEDED(hr)) return true;
    error = diagnostics ? static_cast<const char*>(diagnostics->GetBufferPointer())
                                     : hresult_text(operation, hr);
    return false;
}

bool create_scene_constants(ID3D11Device& device, UINT bytes, const char* operation,
                            SceneComResource<ID3D11Buffer>& constants, std::string& error) {
    D3D11_BUFFER_DESC desc{};
    desc.ByteWidth = bytes;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    return scene_d3d_succeeded(device.CreateBuffer(&desc, nullptr, constants.output()), operation, error);
}
} // namespace

bool Canvas3D::Impl::ensure_scene_outline_pipeline(std::string& error) {
    if (scene_outline_vertex_shader) return true;

    SceneComResource<ID3DBlob> vs, ps;
    SceneComResource<ID3D11VertexShader> vertex;
    SceneComResource<ID3D11PixelShader> pixel;
    SceneComResource<ID3D11Buffer> constants;
    SceneComResource<ID3D11SamplerState> sampler;
    const char* source = k_scene_highlight_outline_shader_source;
    if (!compile_scene_shader(source, "vs_main", "vs_4_0", "D3DCompile(scene outline vertex shader)", vs, error) ||
        !compile_scene_shader(source, "ps_main", "ps_4_0", "D3DCompile(scene outline pixel shader)", ps, error) ||
        !scene_d3d_succeeded(device->CreateVertexShader(vs->GetBufferPointer(), vs->GetBufferSize(),
                                 nullptr, vertex.output()), "CreateVertexShader(scene outline)", error) ||
        !scene_d3d_succeeded(device->CreatePixelShader(ps->GetBufferPointer(), ps->GetBufferSize(),
                                 nullptr, pixel.output()), "CreatePixelShader(scene outline)", error) ||
        !create_scene_constants(*device, sizeof(SceneOutlineConstants),
                                "CreateBuffer(scene outline constants)", constants, error)) {
        return false;
    }

    D3D11_SAMPLER_DESC desc{};
    desc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
    desc.AddressU = desc.AddressV = desc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    desc.ComparisonFunc = D3D11_COMPARISON_NEVER;
    desc.MaxLOD = D3D11_FLOAT32_MAX;
    if (!scene_d3d_succeeded(device->CreateSamplerState(&desc, sampler.output()),
                             "CreateSamplerState(scene outline)", error)) return false;

    scene_outline_vertex_shader = vertex.take();
    scene_outline_pixel_shader = pixel.take();
    scene_outline_constant_buffer = constants.take();
    scene_outline_sampler_state = sampler.take();
    return true;
}

bool Canvas3D::Impl::ensure_scene_pick_pipeline(std::string& error) {
    if (scene_pick_pixel_shader) return true;

    SceneComResource<ID3DBlob> ps;
    SceneComResource<ID3D11PixelShader> pixel;
    SceneComResource<ID3D11Buffer> constants;
    if (!compile_scene_shader(k_scene_pick_shader_source, "ps_main", "ps_4_0",
                              "D3DCompile(scene pick pixel shader)", ps, error) ||
        !scene_d3d_succeeded(device->CreatePixelShader(ps->GetBufferPointer(), ps->GetBufferSize(),
                                 nullptr, pixel.output()), "CreatePixelShader(scene pick)", error) ||
        !create_scene_constants(*device, sizeof(ScenePickConstants),
                                "CreateBuffer(scene pick constants)", constants, error)) {
        return false;
    }

    scene_pick_pixel_shader = pixel.take();
    scene_pick_constant_buffer = constants.take();
    return true;
}

bool Canvas3D::Impl::ensure_scene_mesh_pipeline(std::string& error) {
    if (scene_vertex_shader) return true;
    if (!ensure_pipeline(error) || !ensure_scene_depth_states(error)) return false;

    SceneComResource<ID3DBlob> vs, ps, fog_ps;
    SceneComResource<ID3D11VertexShader> vertex;
    SceneComResource<ID3D11PixelShader> pixel, fog_pixel;
    SceneComResource<ID3D11InputLayout> input_layout;
    SceneComResource<ID3D11Buffer> constants;
    const char* source = k_scene_shader_source;
    if (!compile_scene_shader(source, "vs_main", "vs_4_0", "D3DCompile(scene vertex shader)", vs, error) ||
        !compile_scene_shader(source, "ps_main", "ps_4_0", "D3DCompile(scene pixel shader)", ps, error) ||
        !compile_scene_shader(source, "ps_fog_main", "ps_4_0", "D3DCompile(scene fog pixel shader)", fog_ps, error)) {
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
    if (!scene_d3d_succeeded(device->CreateVertexShader(vs->GetBufferPointer(), vs->GetBufferSize(),
                                 nullptr, vertex.output()), "CreateVertexShader(scene)", error) ||
        !scene_d3d_succeeded(device->CreatePixelShader(ps->GetBufferPointer(), ps->GetBufferSize(),
                                 nullptr, pixel.output()), "CreatePixelShader(scene)", error) ||
        !scene_d3d_succeeded(device->CreatePixelShader(fog_ps->GetBufferPointer(), fog_ps->GetBufferSize(),
                                 nullptr, fog_pixel.output()), "CreatePixelShader(scene fog)", error) ||
        !scene_d3d_succeeded(device->CreateInputLayout(layout, IM_ARRAYSIZE(layout),
                                 vs->GetBufferPointer(), vs->GetBufferSize(), input_layout.output()),
                             "CreateInputLayout(scene)", error) ||
        !create_scene_constants(*device, sizeof(SceneViewConstants),
                                "CreateBuffer(scene constants)", constants, error)) {
        return false;
    }

    scene_vertex_shader = vertex.take();
    scene_pixel_shader = pixel.take();
    scene_fog_pixel_shader = fog_pixel.take();
    scene_input_layout = input_layout.take();
    scene_constant_buffer = constants.take();
    return true;
}

bool Canvas3D::Impl::ensure_scene_pipeline(std::string& error) {
    return ensure_scene_mesh_pipeline(error) && ensure_scene_outline_pipeline(error) &&
        ensure_scene_pick_pipeline(error) && ensure_scene_marker_pipeline(error);
}

bool Canvas3D::Impl::ensure_scene_marker_pipeline(std::string& error) {
    if (scene_marker_vertex_shader) return true;

    SceneComResource<ID3DBlob> vs, ps, pick_ps, mask_ps;
    SceneComResource<ID3D11VertexShader> vertex;
    SceneComResource<ID3D11PixelShader> pixel, pick_pixel, mask_pixel;
    SceneComResource<ID3D11InputLayout> input_layout;
    SceneComResource<ID3D11Buffer> constants, pick_constants;
    const char* source = k_scene_marker_shader_source;
    const char* mask_entry = "ps_mask_main";
#ifndef NDEBUG
    if (std::exchange(debug_faults.marker_compile_failure, false)) mask_entry = "missing_mask_entry";
#endif
    if (!compile_scene_shader(source, "vs_main", "vs_4_0", "D3DCompile(scene marker vertex shader)", vs, error) ||
        !compile_scene_shader(source, "ps_main", "ps_4_0", "D3DCompile(scene marker pixel shader)", ps, error) ||
        !compile_scene_shader(source, "ps_pick_main", "ps_4_0", "D3DCompile(scene marker pick shader)", pick_ps, error) ||
        !compile_scene_shader(source, mask_entry, "ps_4_0", "D3DCompile(scene marker mask shader)", mask_ps, error)) {
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
    if (!scene_d3d_succeeded(device->CreateVertexShader(vs->GetBufferPointer(), vs->GetBufferSize(),
                                 nullptr, vertex.output()), "CreateVertexShader(scene marker)", error) ||
        !scene_d3d_succeeded(device->CreatePixelShader(ps->GetBufferPointer(), ps->GetBufferSize(),
                                 nullptr, pixel.output()), "CreatePixelShader(scene marker)", error) ||
        !scene_d3d_succeeded(device->CreatePixelShader(pick_ps->GetBufferPointer(), pick_ps->GetBufferSize(),
                                 nullptr, pick_pixel.output()), "CreatePixelShader(scene marker pick)", error) ||
        !scene_d3d_succeeded(device->CreatePixelShader(mask_ps->GetBufferPointer(), mask_ps->GetBufferSize(),
                                 nullptr, mask_pixel.output()), "CreatePixelShader(scene marker mask)", error) ||
        !scene_d3d_succeeded(device->CreateInputLayout(layout, IM_ARRAYSIZE(layout),
                                 vs->GetBufferPointer(), vs->GetBufferSize(), input_layout.output()),
                             "CreateInputLayout(scene marker)", error) ||
        !create_scene_constants(*device, sizeof(SceneMarkerConstants),
                                "CreateBuffer(scene marker constants)", constants, error) ||
        !create_scene_constants(*device, sizeof(SceneMarkerPickConstants),
                                "CreateBuffer(scene marker pick constants)", pick_constants, error)) {
        return false;
    }

    scene_marker_vertex_shader = vertex.take();
    scene_marker_pixel_shader = pixel.take();
    scene_marker_pick_pixel_shader = pick_pixel.take();
    scene_marker_mask_pixel_shader = mask_pixel.take();
    scene_marker_input_layout = input_layout.take();
    scene_marker_constant_buffer = constants.take();
    scene_marker_pick_constant_buffer = pick_constants.take();
    return true;
}

bool Canvas3D::Impl::ensure_scene_depth_states(std::string& error) {
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

SceneInstanceData Canvas3D::Impl::make_instance_data(const Mat4& world) {
    SceneInstanceData data = {};
    for (int col = 0; col < 4; ++col) {
        data.world0[col] = world.m[0][col];
        data.world1[col] = world.m[1][col];
        data.world2[col] = world.m[2][col];
        data.world3[col] = world.m[3][col];
    }
    return data;
}

SceneInstanceData Canvas3D::Impl::make_instance_data_relative(const double world[16], DVec3 origin) {
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

SceneInstanceData Canvas3D::Impl::make_chunk_instance_data(DVec3 chunk_origin, DVec3 render_origin) {
    Mat4 world = identity();
    world.m[3][0] = static_cast<float>(chunk_origin.x - render_origin.x);
    world.m[3][1] = static_cast<float>(chunk_origin.y - render_origin.y);
    world.m[3][2] = static_cast<float>(chunk_origin.z - render_origin.z);
    return make_instance_data(world);
}

bool Canvas3D::Impl::ensure_instance_buffer(ID3D11Buffer*& buffer, UINT& capacity,
                            const std::vector<SceneInstanceData>& instances,
                            std::string& error) {
    KME_SCENE_PROFILE(Upload);
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
#ifndef NDEBUG
    scene_frame_profiler.upload(instances.size() * sizeof(SceneInstanceData));
#endif
    context->Unmap(buffer, 0);
    return true;
}

void Canvas3D::Impl::release_scene_mileage_highlight_resources() {
    release_com(scene_mileage_highlight_instance_buffer);
    release_com(scene_mileage_highlight_index_buffer);
    release_com(scene_mileage_highlight_vertex_buffer);
    scene_mileage_highlight_instance_capacity = 0;
    scene_mileage_highlight_instances.clear();
    scene_mileage_highlight_parts.clear();
    scene_mileage_highlight_materials.clear();
}

bool Canvas3D::Impl::ensure_scene_mileage_highlight_resources(std::string& error) {
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

bool Canvas3D::Impl::ensure_pipeline(std::string& error) {
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

static D3D11_TEXTURE2D_DESC render_color_description(int width, int height, UINT bind_flags) {
    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = static_cast<UINT>(width);
    desc.Height = static_cast<UINT>(height);
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = bind_flags;
    return desc;
}

bool Canvas3D::Impl::ensure_render_target(int width, int height, std::string& error) {
    if (!device || width <= 0 || height <= 0) return false;
    if (render_srv && render_width == width && render_height == height) return true;

    release_render_target();
    render_width = width;
    render_height = height;

    const auto tex_desc = render_color_description(
        width, height, D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE);
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

bool Canvas3D::Impl::ensure_scene_highlight_mask_target(std::string& error) {
    if (scene_highlight_mask_srv) return true;

    SceneComResource<ID3D11Texture2D> texture;
    SceneComResource<ID3D11RenderTargetView> target;
    SceneComResource<ID3D11ShaderResourceView> resource;
    const auto desc = render_color_description(
        render_width, render_height, D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE);
    if (!scene_d3d_succeeded(device->CreateTexture2D(&desc, nullptr, texture.output()),
                             "CreateTexture2D(scene highlight mask)", error) ||
        !scene_d3d_succeeded(device->CreateRenderTargetView(texture.get(), nullptr, target.output()),
                             "CreateRenderTargetView(scene highlight mask)", error) ||
        !scene_d3d_succeeded(device->CreateShaderResourceView(texture.get(), nullptr, resource.output()),
                             "CreateShaderResourceView(scene highlight mask)", error)) {
        return false;
    }
    scene_highlight_mask_texture = texture.take();
    scene_highlight_mask_rtv = target.take();
    scene_highlight_mask_srv = resource.take();
    return true;
}

bool Canvas3D::Impl::ensure_scene_pick_target(std::string& error) {
    if (scene_pick_readback_texture) return true;

    SceneComResource<ID3D11Texture2D> texture, readback;
    SceneComResource<ID3D11RenderTargetView> target;
    const auto desc = render_color_description(render_width, render_height, D3D11_BIND_RENDER_TARGET);
    auto read_desc = render_color_description(1, 1, 0);
    read_desc.Usage = D3D11_USAGE_STAGING;
    read_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    if (!scene_d3d_succeeded(device->CreateTexture2D(&desc, nullptr, texture.output()),
                             "CreateTexture2D(scene pick)", error) ||
        !scene_d3d_succeeded(device->CreateRenderTargetView(texture.get(), nullptr, target.output()),
                             "CreateRenderTargetView(scene pick)", error) ||
        !scene_d3d_succeeded(device->CreateTexture2D(&read_desc, nullptr, readback.output()),
                             "CreateTexture2D(scene pick readback)", error)) {
        return false;
    }
    scene_pick_texture = texture.take();
    scene_pick_rtv = target.take();
    scene_pick_readback_texture = readback.take();
    return true;
}

void Canvas3D::Impl::release_render_target() {
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
