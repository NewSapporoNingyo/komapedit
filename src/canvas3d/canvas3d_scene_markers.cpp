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
#include "operation_timing.h"
#include "scene_frame_profile.h"
#include "imgui.h"
#include <d3d11.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <limits>
#include <string>
#include <utility>
#include <vector>

using namespace canvas3d_detail;

namespace canvas3d_detail {

constexpr int k_scene_marker_circle_segments = 48;

} // namespace canvas3d_detail

namespace canvas3d_detail {

constexpr float k_scene_marker_irregularity_content_offset = 0.16f;

} // namespace canvas3d_detail

namespace canvas3d_detail {

constexpr float k_scene_marker_outline_width = 0.012f;

} // namespace canvas3d_detail

namespace canvas3d_detail {

constexpr float k_scene_marker_label_max_width = 0.88f;

} // namespace canvas3d_detail

namespace canvas3d_detail {

constexpr float k_scene_marker_other_track_label_center_y = 0.50f;

} // namespace canvas3d_detail

namespace canvas3d_detail {

constexpr float k_scene_marker_label_center_y = 0.22f;

} // namespace canvas3d_detail

namespace canvas3d_detail {

constexpr float k_scene_marker_label_height = 0.18f;

} // namespace canvas3d_detail

namespace canvas3d_detail {

constexpr float k_scene_marker_icon_half_extent = 0.18f;

} // namespace canvas3d_detail

namespace canvas3d_detail {

constexpr float k_scene_marker_face_offset = 0.003f;

} // namespace canvas3d_detail

namespace canvas3d_detail {

constexpr float k_scene_marker_lateral_gap = 0.10f;

} // namespace canvas3d_detail

namespace canvas3d_detail {

constexpr float k_scene_marker_board_alpha = 0.70f;

} // namespace canvas3d_detail

namespace canvas3d_detail {

bool scene_marker_list_kind_is_navigable(Canvas3DSceneMarkerListKind kind) {
    const size_t slot = static_cast<size_t>(kind);
    return slot > static_cast<size_t>(Canvas3DSceneMarkerListKind::None) &&
        slot < k_scene_marker_list_kind_count;
}

size_t scene_marker_list_kind_slot(Canvas3DSceneMarkerListKind kind) {
    return static_cast<size_t>(kind);
}

} // namespace canvas3d_detail

void Canvas3D::Impl::release_marker_chunk(SceneMarkerChunkGpu& chunk) {
    release_com(chunk.vertex_buffer);
    release_com(chunk.index_buffer);
    release_com(chunk.pick_index_buffer);
    chunk.source_indices.clear();
    chunk.ranges.clear();
    chunk.visible_index_count = 0;
    chunk.visible_pick_index_count = 0;
}

void Canvas3D::Impl::release_scene_marker_chunks() {
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

bool Canvas3D::Impl::bind_scene_marker_chunk(const SceneMarkerChunkGpu& chunk,
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

void Canvas3D::Impl::draw_scene_marker_chunk(const SceneMarkerChunkGpu& chunk,
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
#ifndef NDEBUG
    scene_frame_profiler.draw();
#endif
}

bool Canvas3D::Impl::scene_marker_pick_ids_valid() const {
    constexpr size_t max_pick_id = 0x00ffffffu;
    return scene_data.objects.size() <= max_pick_id &&
        scene_data.markers.size() <= max_pick_id - scene_data.objects.size();
}

bool Canvas3D::Impl::draw_scene_marker_pick_chunk(const SceneMarkerChunkGpu& chunk,
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
#ifndef NDEBUG
    scene_frame_profiler.draw();
#endif
    ID3D11Buffer* null_buffer = nullptr;
    context->PSSetConstantBuffers(1, 1, &null_buffer);
    return true;
}

bool Canvas3D::Impl::scene_marker_font_cache_current() const {
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

void Canvas3D::Impl::draw_visible_scene_markers(double visible_min,
                                double visible_max,
                                DVec3 render_origin,
                                const Mat4& view_proj) {
    KME_SCENE_PROFILE(Markers);
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

bool Canvas3D::Impl::draw_visible_scene_marker_picks(double visible_min,
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

bool Canvas3D::Impl::has_visible_scene_marker_picks(double visible_min,
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

unsigned int Canvas3D::Impl::append_scene_marker_vertex(
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

void Canvas3D::Impl::append_scene_marker_quad(
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
    bool textured,
    ImVec2 uv0,
    ImVec2 uv1) {
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

void Canvas3D::Impl::append_scene_marker_triangle(
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

ImVec2 Canvas3D::Impl::scene_marker_icon_point(ImVec2 point) {
    return ImVec2(
        point.x * k_scene_marker_icon_half_extent,
        0.72f - point.y * k_scene_marker_icon_half_extent);
}

void Canvas3D::Impl::append_scene_marker_line(
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

void Canvas3D::Impl::append_scene_marker_glyph(
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

unsigned int Canvas3D::Impl::decode_scene_marker_utf8(const char*& cursor,
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

void Canvas3D::Impl::append_scene_marker_text(
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

void Canvas3D::Impl::append_scene_marker_icon_glyph(
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

void Canvas3D::Impl::append_scene_marker_icon(
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

void Canvas3D::Impl::scene_marker_frame(const Canvas3DTrackPoint& point,
                               DVec3& right,
                               DVec3& up,
                               DVec3& forward) {
    scene_track_surface_frame(point, right, up, forward);
}

SceneMarkerGeometrySpan Canvas3D::Impl::append_scene_marker_geometry(
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
    const bool is_curve_parameter =
        marker.kind == MapMarkerVisualKind::CurveGauge ||
        marker.kind == MapMarkerVisualKind::CurveCenter ||
        marker.kind == MapMarkerVisualKind::CurveFunction;

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
    if (!is_other_track_change && !is_curve_parameter) {
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
        auto append_outlined_label = [&](const std::string& text,
                                         float center_y,
                                         float height) {
            if (text.empty()) return;
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
                    font, baked, font_size, text,
                    face_sign, outline_color, height, center_y,
                    k_scene_marker_label_max_width);
            }
            append_scene_marker_text(
                vertices, indices, origin, content_center,
                right, up, forward, font, baked, font_size,
                text, face_sign, text_color, height, center_y,
                k_scene_marker_label_max_width);
        };
        if (is_curve_parameter) {
            append_outlined_label(marker.label, 0.72f, 0.22f);
            append_outlined_label(marker.secondary_label, 0.28f, 0.18f);
        } else {
            append_outlined_label(marker.label, label_center_y,
                                  k_scene_marker_label_height);
        }
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

void Canvas3D::Impl::rebuild_scene_mileage_pick_cache() {
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

bool Canvas3D::Impl::rebuild_scene_marker_visible_indices(std::string& error) {
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

bool Canvas3D::Impl::build_scene_marker_chunks(std::string& error) {
    kme::timing::GuiTiming::Stage edit_timing("scene.marker_gpu");
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

bool Canvas3D::Impl::set_scene_marker_visibility(
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
