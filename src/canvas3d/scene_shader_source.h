/*
 * Copyright (c) 2026 Sapporo_ningyo
 *
 * Licensed under Apache License 2.0; see LICENSE and NOTICE.
 */

#pragma once

namespace canvas3d_detail {

// Shared by the runtime compiler and the device-free CTest compile contract.
inline constexpr char k_scene_shader_source[] = R"(
cbuffer SceneViewConstants : register(b0)
{
    row_major float4x4 viewProj;
    float4 materialColor;
    float4 useTexture;
    float4 fogColorDensity;
    float4 fogLinear;
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
    float fogFactor;
    if (fogLinear.z > 0.5)
    {
        float interval = fogLinear.y - fogLinear.x;
        // A collapsed range is a step at end; never divide by zero.
        fogFactor = interval != 0.0
            ? saturate((fogLinear.y - eyeDepth) / interval)
            : (eyeDepth < fogLinear.y ? 1.0 : 0.0);
    }
    else
        fogFactor = saturate(exp2(-1.44269504089 * fogColorDensity.w * eyeDepth));
    color.rgb = lerp(fogColorDensity.rgb, color.rgb, fogFactor);
    return color;
}
)";

} // namespace canvas3d_detail
