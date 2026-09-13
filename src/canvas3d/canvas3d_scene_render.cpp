/*
 * Copyright (c) 2026 Sapporo_ningyo
 *
 * Licensed under Apache License 2.0; see LICENSE and NOTICE.
 */

#ifdef _MSC_VER
#pragma execution_character_set("utf-8")
#endif

#include "canvas3d_impl.h"
#include "canvas3d_scene_data.h"
#include "kme.h"
#include "scene_frame_profile.h"
#include "imgui.h"
#include <d3d11.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <map>
#include <iterator>
#include <limits>
#include <string>
#include <utility>
#include <vector>

using namespace canvas3d_detail;

namespace canvas3d_detail {

constexpr float k_model_preview_fov_y = 0.78539816339f;

} // namespace canvas3d_detail

namespace canvas3d_detail {

constexpr float k_scene_highlight_outline_width_px = 5.0f;

} // namespace canvas3d_detail

namespace canvas3d_detail {

constexpr float k_scene_depth_clear = 0.0f;

} // namespace canvas3d_detail

namespace canvas3d_detail {

constexpr float k_scene_background_far_z = 5000.0f;

} // namespace canvas3d_detail

namespace canvas3d_detail {

constexpr float k_scene_background_near_z = 0.05f;

} // namespace canvas3d_detail

namespace canvas3d_detail {

static ImVec4 scene_highlight_color_for_kind(Canvas3DSceneObjectKind kind) {
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

static ImVec4 scene_marker_highlight_color() {
    return ImVec4(65.0f / 255.0f, 126.0f / 255.0f, 245.0f / 255.0f, 0.92f);
}
static bool scene_pick_id_for_object(int object_index, unsigned int& id) {
    if (object_index < 0 || object_index >= 0x00ffffff) return false;
    id = static_cast<unsigned int>(object_index) + 1u;
    return true;
}

static std::array<float, 4> scene_pick_color_for_id(unsigned int id) {
    return {{
        static_cast<float>(id & 0xffu) / 255.0f,
        static_cast<float>((id >> 8) & 0xffu) / 255.0f,
        static_cast<float>((id >> 16) & 0xffu) / 255.0f,
        1.0f
    }};
}

static unsigned int scene_pick_id_from_rgba(const unsigned char* rgba) {
    if (!rgba) return 0;
    return static_cast<unsigned int>(rgba[0]) |
        (static_cast<unsigned int>(rgba[1]) << 8) |
        (static_cast<unsigned int>(rgba[2]) << 16);
}

static ScenePickTarget scene_pick_target_from_id(unsigned int id,
                                          size_t object_count,
                                          size_t marker_count) {
    if (id == 0) return {};
    const size_t index = static_cast<size_t>(id - 1u);
    if (index < object_count) return {ScenePickTargetKind::Object, index};
    const size_t marker_index = index - object_count;
    if (marker_index < marker_count) return {ScenePickTargetKind::Marker, marker_index};
    return {};
}
static bool scene_material_is_translucent(const GpuMaterial* material) {
    return material && material->diffuse[3] < k_material_opaque_alpha_threshold;
}

static bool scene_material_uses_alpha_mask(const GpuMaterial* material) {
    return material && material->has_texture && material->texture_has_alpha && !scene_material_is_translucent(material);
}

} // namespace canvas3d_detail

void Canvas3D::Impl::bind_scene_instanced_mesh(ID3D11Buffer* vb, ID3D11Buffer* ib, ID3D11Buffer* instance_buffer) {
    UINT strides[2] = {sizeof(GpuVertex), sizeof(SceneInstanceData)};
    UINT offsets[2] = {0, 0};
    ID3D11Buffer* buffers[2] = {vb, instance_buffer};
    context->IASetInputLayout(scene_input_layout);
    context->IASetVertexBuffers(0, 2, buffers, strides, offsets);
    context->IASetIndexBuffer(ib, DXGI_FORMAT_R32_UINT, 0);
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
}

void Canvas3D::Impl::draw_scene_mesh(ID3D11Buffer* vb, ID3D11Buffer* ib, ID3D11Buffer* instance_buffer,
                     const std::vector<MeshPart>& mesh_parts,
                     const std::vector<GpuMaterial>& mesh_materials,
                     UINT instance_count,
                     const Mat4& view_proj,
                     ID3D11RasterizerState* base_rasterizer,
                     bool mask_pass,
                     const SceneFogSample* fog) {
    KME_SCENE_PROFILE(Models);
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
        scene_frame_profiler.draw();
        if (fog_active) ++debug_scene_fog_draw_part_count;
#endif
    }
    ID3D11ShaderResourceView* null_srv = nullptr;
    context->PSSetShaderResources(0, 1, &null_srv);
    context->RSSetState(base_rasterizer);
    context->OMSetDepthStencilState(scene_depth_state, 0);
    context->OMSetBlendState(nullptr, blend_factor, 0xffffffff);
}

void Canvas3D::Impl::draw_scene_model(SceneModelGpu& model,
                      const std::vector<SceneInstanceData>& instances,
                      const Mat4& view_proj,
                      const SceneFogSample* fog) {
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

void Canvas3D::Impl::draw_scene_mileage_highlight(DVec3 render_origin,
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

bool Canvas3D::Impl::draw_scene_pick_model(SceneModelGpu& model,
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
#ifndef NDEBUG
        scene_frame_profiler.draw();
#endif
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

bool Canvas3D::Impl::begin_scene_pick_at_mouse(
    const std::map<std::string, std::vector<SceneInstanceData>>& visible_instances,
    const std::map<int, std::vector<SceneVisibleInstanceRef>>& object_refs,
    const Mat4& view_proj,
    int width,
    int height,
    ImVec2 mouse_local,
    std::string& error) {
    KME_SCENE_PROFILE(Picking);
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

ScenePickTarget Canvas3D::Impl::finish_scene_pick_at_mouse(int width,
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

void Canvas3D::Impl::composite_scene_highlight_outline(int width, int height,
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
#ifndef NDEBUG
    scene_frame_profiler.draw();
#endif

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

void Canvas3D::Impl::draw_scene_highlight_mask_batches(const std::map<std::string, std::vector<SceneInstanceData>>& batches,
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

void Canvas3D::Impl::draw_scene_model_highlight_outlines(const std::vector<SceneHighlightInstance>& instances,
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

void Canvas3D::Impl::draw_scene_highlight_batch(const SceneHighlightBatch& batch,
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

bool Canvas3D::Impl::scene_chunk_visible(const SceneChunk& chunk, double visible_min, double visible_max) {
    return chunk.d_max >= visible_min && chunk.d_min <= visible_max;
}

void Canvas3D::Impl::draw_scene_track_chunk(SceneTrackChunkGpu& track,
                            DVec3 render_origin,
                            const Mat4& view_proj,
                            std::vector<SceneInstanceData>& track_instance,
                            std::string& error,
                            const SceneFogSample* fog) {
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

bool Canvas3D::Impl::scene_object_index_valid(int object_index) const {
    return object_index >= 0 && static_cast<size_t>(object_index) < scene_data.objects.size();
}

bool Canvas3D::Impl::project_scene_point(DVec3 relative_point,
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

void Canvas3D::Impl::draw_scene_marker_highlight(size_t marker_index,
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
#ifndef NDEBUG
    scene_frame_profiler.draw();
#endif

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

bool Canvas3D::Impl::compute_scene_instance_screen_bounds(const double world[16],
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

std::string Canvas3D::Impl::current_background_path() const {
    std::string path;
    for (const Canvas3DBackgroundChange& bg : scene_data.backgrounds) {
        if (bg.distance > scene_camera_distance) break;
        path = bg.model_path;
    }
    return path;
}

void Canvas3D::Impl::draw_background_model(const Mat4& view_proj, const SceneFogSample* fog) {
    std::string path = current_background_path();
    if (path.empty()) return;
    auto it = scene_models.find(path);
    if (it == scene_models.end() || it->second.state != SceneModelGpu::State::Ready) return;
    Mat4 world = identity();
    std::vector<SceneInstanceData> instances{make_instance_data(world)};
    draw_scene_model(it->second, instances, view_proj, fog);
}

double Canvas3D::Impl::effective_scene_window_forward_m() const {
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

float Canvas3D::Impl::scene_far_z(double window_forward_m) const {
    double far_z = scene_window_back_m + window_forward_m + scene_chunk_m * 2.0;
    if (!std::isfinite(far_z)) far_z = k_scene_background_far_z;
    return static_cast<float>(std::clamp(far_z, 256.0, static_cast<double>(k_scene_background_far_z)));
}

void Canvas3D::Impl::render_scene_preview_target(int width, int height, ImVec2 mouse_local,
                                 bool pick_enabled,
                                 bool mileage_pick_enabled) {
#ifndef NDEBUG
    debug_scene_signature = 14695981039346656037ULL;
#endif
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
    // Evict the complete old window before preparing new chunks, so a
    // backwards jump can use the same bounded budget as forward movement.
    for (SceneChunk& chunk : scene_chunks) {
        if (!scene_chunk_visible(chunk, visible_min, visible_max) && chunk.repeater_cache_prepared) {
            invalidate_scene_repeater_cache(chunk);
        }
    }
    for (size_t i = 0; i < scene_chunks.size(); ++i) {
        SceneChunk& chunk = scene_chunks[i];
        if (!scene_chunk_visible(chunk, visible_min, visible_max)) continue;
        {
        KME_SCENE_PROFILE(Instances);
        for (const SceneInstance& instance : chunk.instances) {
            if (instance.distance < visible_min || instance.distance > visible_max) continue;
#ifndef NDEBUG
            debug_record_scene_world(instance.model_path, instance.object_index, instance.distance, instance.world);
#endif
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
#ifndef NDEBUG
    scene_frame_profiler.result.model_groups = visible_instances.size();
#endif
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
        KME_SCENE_PROFILE(Tracks);
        if (!scene_chunk_visible(scene_chunks[i], visible_min, visible_max)) continue;
        draw_scene_track_chunk(scene_track_chunks[i], render_origin, view_proj,
                               track_instance, error, fog_ptr);
    }
    draw_scene_mileage_highlight(render_origin, view_proj, error);
    if (!error.empty() && scene_last_error != error) scene_last_error = error;
    ScenePickTarget picked_target;
    if (scene_pick_active) {
        KME_SCENE_PROFILE(Picking);
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
    {
    KME_SCENE_PROFILE(Highlight);
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
    }

    ID3D11ShaderResourceView* null_srv = nullptr;
    context->PSSetShaderResources(0, 1, &null_srv);
    context->OMSetBlendState(nullptr, nullptr, 0xffffffff);
    ID3D11RenderTargetView* null_rtv = nullptr;
    context->OMSetRenderTargets(1, &null_rtv, nullptr);
    update_scene_fps_counter();
}

void Canvas3D::Impl::render_scene(int width, int height) {
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
