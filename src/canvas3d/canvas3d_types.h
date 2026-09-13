/*
 * Copyright (c) 2026 Sapporo_ningyo
 *
 * Licensed under Apache License 2.0; see LICENSE and NOTICE.
 */

#pragma once

#include "canvas3d_math.h"
#include <d3d11.h>
#include <array>
#include <chrono>
#include <cstdint>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace canvas3d_detail {

inline constexpr float k_default_scene_camera_height = 2.0f;
inline constexpr double k_scene_repeater_distance_epsilon = 1e-6;
inline constexpr long long k_scene_repeater_instance_limit = 1000000;
inline constexpr float k_scene_camera_fov_y = 0.6108652382f;
inline constexpr float k_scene_near_z = 0.2f;
inline constexpr float k_material_opaque_alpha_threshold = 0.98f;
inline constexpr double k_scene_mileage_pick_half_width = 100.0;
inline constexpr double k_scene_route_display_zero_epsilon = 0.0000005;
// Optional CPU reuse only. Exceeding this budget keeps the exact uncached
// placement path, without changing the draw window or instance limits.
inline constexpr size_t k_scene_repeater_cache_instance_limit = 65536;
inline constexpr float k_scene_marker_board_width = 1.0f;
inline constexpr float k_scene_marker_sound3d_tag_tip_height = 0.30f;
inline constexpr size_t k_scene_marker_target_missing = std::numeric_limits<size_t>::max();
inline constexpr size_t k_scene_marker_list_kind_count =
    static_cast<size_t>(Canvas3DSceneMarkerListKind::Count);

enum class SceneOverlayCorner {
    TopLeft,
    TopRight,
    BottomRight,
};

struct SceneOverlayLabelLayout {
    ImVec2 pos{};
    float pad = 0.0f;
};
enum class ScenePickTargetKind {
    None,
    Object,
    Marker,
};

struct ScenePickTarget {
    ScenePickTargetKind kind = ScenePickTargetKind::None;
    size_t index = 0;
};
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
    struct RepeaterWorld {
        std::array<double, 16> world{};
        bool valid = false;
    };
    struct RepeaterCache {
        long long first_index = 0;
        std::vector<RepeaterWorld> worlds;
    };
    bool repeater_cache_prepared = false;
    size_t cached_world_count = 0;
    std::vector<RepeaterCache> repeater_cache;
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
struct SceneFogSample {
    bool enabled = false;
    float density = 0.0f;
    ImVec4 color = ImVec4(0.0f, 0.0f, 0.0f, 1.0f);
};

} // namespace canvas3d_detail

namespace canvas3d_detail {


bool scene_marker_list_kind_is_navigable(Canvas3DSceneMarkerListKind kind);
size_t scene_marker_list_kind_slot(Canvas3DSceneMarkerListKind kind);

} // namespace canvas3d_detail
