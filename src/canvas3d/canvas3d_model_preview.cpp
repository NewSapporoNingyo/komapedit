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
#include "model_loader.h"
#include "imgui.h"
#include <d3d11.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <string>
#include <vector>

using namespace canvas3d_detail;

namespace {
constexpr int k_reference_grid_half_cells = 6;
constexpr float k_reference_axis_radius_factor = 0.8f;
constexpr ImU32 k_reference_grid_color = IM_COL32(78, 92, 110, 196);

struct ReferenceAxis {
    const char* label;
    Vec3 direction;
    ImU32 color;
};

// BVE forward corresponds to negative Z in the imported mesh.
constexpr std::array<ReferenceAxis, 3> k_reference_axes = {{
    {"+X", {1.0f, 0.0f, 0.0f}, IM_COL32(235, 67, 67, 255)},
    {"+Y", {0.0f, 1.0f, 0.0f}, IM_COL32(73, 205, 91, 255)},
    {"+Z", {0.0f, 0.0f, -1.0f}, IM_COL32(65, 126, 245, 255)},
}};

constexpr UINT k_reference_grid_vertex_count =
    4 * (2 * k_reference_grid_half_cells + 1) + 2 * k_reference_axes.size();

double reference_grid_spacing(float radius) {
    const double target = static_cast<double>(radius) / 4.0;
    const double decade = std::pow(10.0, std::floor(std::log10(target)));
    for (double multiplier : {1.0, 2.0, 5.0}) {
        const double step = multiplier * decade;
        if (step >= target) return step;
    }
    return 10.0 * decade;
}
} // namespace

bool Canvas3D::Impl::load_model(const std::string& path, std::string& error) {
    model_load_warnings.clear();
    MlMeshData data = {};
    if (!ModelLoaderClient::load(path, data, error)) return false;
    ModelDataGuard data_guard(data);

    return upload_model(data, path, error);
}

std::vector<std::string> Canvas3D::Impl::drain_model_load_warnings() {
    std::vector<std::string> warnings;
    warnings.swap(model_load_warnings);
    return warnings;
}

bool Canvas3D::Impl::reload_model(std::string& error) {
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

bool Canvas3D::Impl::upload_model(const MlMeshData& data, const std::string& path, std::string& error) {
    if (!device || !context) {
        error = "Direct3D device is not available";
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
    if (!upload_model_preview_reference_grid(data, error)) {
        release_resources();
        return false;
    }

    parts.reserve(data.part_count);
    for (size_t i = 0; i < data.part_count; ++i) {
        const MlMeshPart& part = data.parts[i];
        parts.push_back({part.start_index, part.index_count, part.material_index});
    }

    materials.resize(data.material_count);
    for (size_t i = 0; i < data.material_count; ++i) {
        const MlMaterial& src = data.materials[i];
        auto& dest = materials[i];
        std::copy_n(src.diffuse, 3, dest.diffuse);
        dest.diffuse[3] = normalize_material_alpha(src.diffuse[3]);
        if (src.texture_path && *src.texture_path) {
            bool texture_has_alpha = false;
            std::string texture_error;
            if (!load_texture(src.texture_path, &dest.texture, texture_error, &texture_has_alpha)) {
                model_load_warnings.push_back(
                    "[WARN]canvas3D.cpp: model preview texture warning: model=" +
                    path + "; " + texture_error);
            } else {
                dest.has_texture = true;
                dest.texture_has_alpha = texture_has_alpha;
            }
        }
    }

    index_count = static_cast<UINT>(data.index_count);
    bounds_min = {data.bounds_min[0], data.bounds_min[1], data.bounds_min[2]};
    bounds_max = {data.bounds_max[0], data.bounds_max[1], data.bounds_max[2]};
    center = {data.center[0], data.center[1], data.center[2]};
    radius = data.radius;
    model_path_value = path;
    yaw = 0.0f;
    pitch = 0.0f;
    distance_factor = 2.8f;
    return true;
}

bool Canvas3D::Impl::upload_model_preview_reference_grid(const MlMeshData& data,
                                                        std::string& error) {
    const double step = reference_grid_spacing(data.radius);
    const double grid_x = std::round(data.center[0] / step) * step;
    const double grid_z = std::round(data.center[2] / step) * step;
    const double extent = k_reference_grid_half_cells * step;
    const auto coordinate = [](double value) {
        constexpr double limit = std::numeric_limits<float>::max();
        return static_cast<float>(std::clamp(value, -limit, limit));
    };
    const float left = coordinate(grid_x - extent);
    const float right = coordinate(grid_x + extent);
    const float back = coordinate(grid_z - extent);
    const float front = coordinate(grid_z + extent);

    std::array<SceneMarkerVertex, k_reference_grid_vertex_count> vertices;
    auto vertex = vertices.begin();
    const auto append_line = [&](Vec3 a, Vec3 b, ImU32 color) {
        *vertex++ = {a.x, a.y, a.z, 0.0f, 0.0f, color};
        *vertex++ = {b.x, b.y, b.z, 0.0f, 0.0f, color};
    };
    for (int i = -k_reference_grid_half_cells; i <= k_reference_grid_half_cells; ++i) {
        const float x = coordinate(grid_x + i * step);
        const float z = coordinate(grid_z + i * step);
        append_line({x, 0.0f, back}, {x, 0.0f, front}, k_reference_grid_color);
        append_line({left, 0.0f, z}, {right, 0.0f, z}, k_reference_grid_color);
    }
    const float axis_length = data.radius * k_reference_axis_radius_factor;
    for (const ReferenceAxis& axis : k_reference_axes) {
        append_line({}, axis.direction * axis_length, axis.color);
    }

    D3D11_BUFFER_DESC desc = {};
    desc.ByteWidth = sizeof(vertices);
    desc.Usage = D3D11_USAGE_IMMUTABLE;
    desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    D3D11_SUBRESOURCE_DATA initial_data = {};
    initial_data.pSysMem = vertices.data();
    const HRESULT hr = device->CreateBuffer(&desc, &initial_data, &model_preview_reference_grid_buffer);
    if (FAILED(hr)) {
        error = hresult_text("CreateBuffer(model preview reference grid)", hr);
        return false;
    }
    return true;
}

void Canvas3D::Impl::draw_model_preview_reference_grid(const Mat4& model_to_clip) {
    SceneMarkerConstants constants = {};
    constants.view_proj = model_to_clip;
    context->UpdateSubresource(scene_marker_constant_buffer, 0, nullptr, &constants, 0, 0);

    const UINT stride = sizeof(SceneMarkerVertex);
    const UINT offset = 0;
    context->IASetInputLayout(scene_marker_input_layout);
    context->IASetVertexBuffers(0, 1, &model_preview_reference_grid_buffer, &stride, &offset);
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINELIST);
    context->VSSetShader(scene_marker_vertex_shader, nullptr, 0);
    context->VSSetConstantBuffers(0, 1, &scene_marker_constant_buffer);
    context->PSSetShader(scene_marker_pixel_shader, nullptr, 0);
    context->OMSetDepthStencilState(scene_depth_read_state, 0);
    context->OMSetBlendState(blend_state, nullptr, 0xffffffff);
    context->Draw(k_reference_grid_vertex_count, 0);
}

void Canvas3D::Impl::draw_model_preview_axis_labels(ImDrawList& draw, ImVec2 origin, ImVec2 size,
                                                   const Mat4& model_to_clip) const {
    draw.PushClipRect(origin, ImVec2(origin.x + size.x, origin.y + size.y), true);
    const float axis_length = radius * k_reference_axis_radius_factor;
    for (const ReferenceAxis& axis : k_reference_axes) {
        const Vec4 clip = transform_point_row(axis.direction * axis_length, model_to_clip);
        if (!(clip.w > 0.0f && clip.z >= 0.0f && clip.z <= clip.w &&
              std::abs(clip.x) <= clip.w && std::abs(clip.y) <= clip.w)) continue;

        const ImVec2 tip(origin.x + (0.5f + 0.5f * clip.x / clip.w) * size.x,
                         origin.y + (0.5f - 0.5f * clip.y / clip.w) * size.y);
        const ImVec2 text_size = ImGui::CalcTextSize(axis.label);
        const ImVec2 label(tip.x + 4.0f, tip.y - text_size.y * 0.5f);
        draw.AddRectFilled(ImVec2(label.x - 2.0f, label.y - 1.0f),
                           ImVec2(label.x + text_size.x + 2.0f, label.y + text_size.y + 1.0f),
                           IM_COL32(18, 21, 27, 225), 3.0f);
        draw.AddText(label, axis.color, axis.label);
    }
    draw.PopClipRect();
}

void Canvas3D::Impl::clear_model() {
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

bool Canvas3D::Impl::has_model() const {
    return vertex_buffer && index_buffer && index_count > 0;
}

void Canvas3D::Impl::draw_overlay(ImDrawList* draw, ImVec2 origin, ImVec2 size) const {
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

void Canvas3D::Impl::render(ImVec2 requested_size, bool show_reference_grid) {
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
    const auto reference_projection = render_model_preview_target(width, height, show_reference_grid);

    ImDrawList* draw = ImGui::GetWindowDrawList();
    ImVec2 end(origin.x + avail.x, origin.y + avail.y);
    if (render_srv) {
        draw->AddImage(reinterpret_cast<void*>(render_srv), origin, end);
    } else {
        draw->AddRectFilled(origin, end, IM_COL32(0, 0, 0, 255));
    }
    if (reference_projection) {
        draw_model_preview_axis_labels(*draw, origin, avail, *reference_projection);
    }
    draw_overlay(draw, origin, avail);
}

void Canvas3D::Impl::release_resources() {
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
    release_com(model_preview_reference_grid_buffer);
    release_com(vertex_buffer);
    release_com(index_buffer);
    index_count = 0;
}
