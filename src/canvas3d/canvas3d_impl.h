/*
 * Copyright (c) 2026 Sapporo_ningyo
 *
 * Licensed under Apache License 2.0; see LICENSE and NOTICE.
 */

#pragma once

#include "canvas3D.h"
#include "canvas3d_types.h"
#include "canvas3d_model_loader.h"
#include "scene_track_sampling.h"
#include "scene_frame_profile.h"
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <unordered_set>

// Shared state owner. Definitions live in the functional canvas3d_*.cpp modules.
struct Canvas3D::Impl {
    explicit Impl(ID3D11Device* device, Canvas3DWakeCallback wake_callback);

    ~Impl();

    bool load_model(const std::string& path, std::string& error);

    std::vector<std::string> drain_model_load_warnings();

    bool reload_model(std::string& error);

    bool upload_model(const MlMeshData& data, const std::string& path, std::string& error);

    void clear_model();

    bool has_model() const;

    bool load_scene(Canvas3DScene scene, std::string& error, bool preserve_loaded_models, bool preserve_camera);

    std::vector<canvas3d_detail::SceneModelLoadRequest> reconcile_dynamic_scene_model_requests(
        const std::map<std::string, canvas3d_detail::SceneModelLoadRequest>& requests);

    bool refresh_scene_dynamic_content(const MapModel& model, int station_index, std::string& error);

    bool refresh_scene_map_content(const MapModel& model,
                                   const Canvas3DSceneMapRefreshOptions& options,
                                   std::string& error);

    bool refresh_scene_route_stations(const MapModel& model, std::string& error);

    void clear_scene();

    bool has_scene() const;

    bool reload_scene_models(std::string& error);

    bool set_scene_track_visibility(const std::vector<Canvas3DTrackVisibility>& visibility, std::string& error);

    void set_scene_window(double back_m, double forward_m);

    void set_scene_edit_component_scale(float scale);

    void set_scene_interaction_mode(Canvas3DSceneInteractionMode mode);

    void set_scene_fog_enabled(bool enabled);

    void set_scene_map_draw_distance_enabled(bool enabled);

    void set_scene_camera_speed_percent(int percent);

    void set_scene_performance_warning(bool enabled,
                                       size_t warning_threshold,
                                       size_t critical_warning_threshold);

#ifndef NDEBUG
    Canvas3DSceneFogDebugState debug_scene_fog_state() const;

    bool debug_read_scene_render_pixels(std::vector<std::uint8_t>& rgba,
                                        int& width, int& height,
                                        std::string& error);
#endif

    Canvas3DSceneInteractionMode scene_interaction_mode_value() const;

    Canvas3DSceneStats scene_stats() const;

    std::vector<std::string> drain_scene_load_messages();

    Canvas3DSceneCameraPose scene_camera_pose() const;

    size_t count_scene_instances() const;

    std::map<std::string, canvas3d_detail::SceneModelLoadRequest> collect_scene_model_load_requests() const;

    bool set_scene_object_model_option(int object_index, size_t option_index);

    static bool same_placement_edit_target(const Canvas3DPlacementEditTarget& a,
                                           const Canvas3DPlacementEditTarget& b);

    const std::string* placement_edit_id_for_object(int object_index) const;

    size_t scene_chunk_index_for_distance(double distance) const;

    Canvas3DModelInstance placement_instance_from_target(
        const Canvas3DPlacementEditTarget& target,
        const Canvas3DModelInstance& base) const;

    Canvas3DModelInstance put_between_instance_from_target(
        const Canvas3DPlacementEditTarget& target,
        const Canvas3DModelInstance& base) const;

    Canvas3DPlacementEditTarget put_between_target_from_instance(
        const std::string& edit_id,
        const Canvas3DModelInstance& instance) const;

    Canvas3DRepeaterSegment repeater_segment_from_target(
        const Canvas3DPlacementEditTarget& target,
        const Canvas3DRepeaterSegment& base) const;

    bool replace_scene_placement_instance(const std::string& edit_id,
                                          Canvas3DModelInstance desired,
                                          const std::string& render_model_path);

    bool write_scene_placement_instance(const std::string& edit_id,
                                        Canvas3DModelInstance desired);

    bool put_between_edit_frame(double distance, canvas3d_detail::DVec3& origin,
                                std::array<canvas3d_detail::DVec3, 3>& axes) const;

    bool write_scene_put_between_instance(const std::string& edit_id,
                                          Canvas3DModelInstance desired,
                                          const std::string& render_model_path);

    bool track_distance_gizmo_frame(const std::string& track_key,
                                    double distance,
                                    canvas3d_detail::SceneGizmoHandle& gizmo) const;

    bool sound3d_gizmo_frame(const Canvas3DPlacementEditTarget& target,
                             canvas3d_detail::SceneGizmoHandle& gizmo) const;

    bool sound3d_marker_baseline_target(
        const Canvas3DPlacementEditTarget& target,
        Canvas3DPlacementEditTarget& baseline) const;

    bool update_scene_sound3d_marker(const std::string& edit_id,
                                     double distance,
                                     double x,
                                     double y);

    std::optional<std::pair<size_t, size_t>> scene_repeater_chunk_range(
        const Canvas3DRepeaterSegment& repeater) const;

    void update_scene_repeater_chunk_membership(
        size_t repeater_index,
        const std::optional<std::pair<size_t, size_t>>& old_range,
        const std::optional<std::pair<size_t, size_t>>& new_range);

    bool write_scene_repeater_segment(const std::string& edit_id,
                                      Canvas3DRepeaterSegment desired);

    bool scene_source_model_path_in_use(const std::string& path) const;

    void release_unused_put_between_preview_base_models();

    void clear_scene_placement_edit_target();

    void cancel_scene_gizmo_interaction(canvas3d_detail::SceneGizmoTarget target);

    bool set_scene_placement_edit_target(const Canvas3DPlacementEditTarget& target,
                                         bool show_gizmo);

    bool set_scene_repeater_edit_target(const Canvas3DPlacementEditTarget& input,
                                        bool show_gizmo);

#ifndef NDEBUG
    bool debug_check_scene_edit_target(const Canvas3DPlacementEditTarget& target) const;
#endif

    bool update_scene_placement_instance(const Canvas3DPlacementEditTarget& target);

    bool update_scene_repeater_segment(const Canvas3DPlacementEditTarget& input);

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
        canvas3d_detail::DVec3 center;
    };

    bool scene_model_center_world(const std::string& model_path, const double world[16], canvas3d_detail::DVec3& out) const;

    bool find_placed_object_jump_target(Canvas3DSceneObjectKind kind,
                                        size_t source_row,
                                        SceneObjectJumpTarget& target) const;

    const Canvas3DRepeaterSegment* find_repeater_segment(size_t source_row) const;

    bool find_repeater_jump_target(size_t source_row, SceneObjectJumpTarget& target) const;

    bool find_repeater_end_or_change_jump_target(size_t source_row,
                                                 SceneObjectJumpTarget& target) const;

    bool find_scene_object_jump_target(Canvas3DSceneObjectKind kind,
                                       size_t source_row,
                                       SceneObjectJumpTarget& target) const;

    void clear_scene_focus_highlight();

    void start_scene_focus_highlight(int object_index, const std::string& model_path, const double world[16]);

    bool scene_focus_highlight_active_now();

    void start_scene_marker_focus_highlight(size_t marker_index);

    bool update_scene_focus_highlight_batch(canvas3d_detail::DVec3 render_origin,
                                            const canvas3d_detail::Mat4& view_proj,
                                            int width,
                                            int height);

    bool set_scene_camera_for_target(double distance, canvas3d_detail::DVec3 target_center);

    bool jump_scene_camera_to_object(Canvas3DSceneObjectKind kind, size_t source_row);

    bool jump_scene_camera_to_marker(Canvas3DSceneMarkerListKind list_kind,
                                     size_t row_index);

    bool jump_scene_camera_to_repeater_end_or_change(size_t source_row);

    bool load_texture(const std::string& path,
                      ID3D11ShaderResourceView** out_srv,
                      std::string& error,
                      bool* out_has_alpha = nullptr);

    bool load_scene_texture(const std::string& path,
                            ID3D11ShaderResourceView** out_srv,
                            std::string& error,
                            bool* out_has_alpha = nullptr);

    void release_scene_texture_cache();

    void release_scene_model(canvas3d_detail::SceneModelGpu& model);

    void release_track_chunk(canvas3d_detail::SceneTrackChunkGpu& chunk);

    void release_scene_track_chunks();

    static void release_marker_chunk(canvas3d_detail::SceneMarkerChunkGpu& chunk);

    void release_scene_marker_chunks();

    void notify_scene_loading_progress();

    void queue_scene_model_uploads(std::vector<canvas3d_detail::CpuModelData> outputs);

    void clear_pending_scene_model_uploads();

    void release_scene_resources();

    void stop_scene_loader();

    canvas3d_detail::CpuModelData copy_cpu_model(const std::string& path, const MlMeshData& data);

    void start_scene_put_between_preview_worker();

    void stop_scene_put_between_preview_worker();

    std::uint64_t queue_scene_put_between_preview(
        const Canvas3DPlacementEditTarget& target);

    std::optional<canvas3d_detail::ScenePutBetweenPreviewResult>
    take_scene_put_between_preview_result();

    void push_scene_load_log(std::string message);

    void start_scene_model_worker(std::vector<canvas3d_detail::SceneModelLoadRequest> requests);

#ifndef NDEBUG
    void debug_record_scene_world(const std::string& path, int object, double distance, const double* world);
    bool debug_check_repeater_cache();
    Canvas3DSceneRenderContractResult debug_check_scene_render();

    Canvas3DSceneLoaderContractResult debug_run_scene_loader_contract(
        const std::string& valid_model_path,
        const std::string& valid_texture_path);
#endif

    void maybe_log_scene_model_load_summary();

    bool upload_scene_model(const canvas3d_detail::CpuModelData& cpu, std::string& error);

    bool upload_scene_put_between_preview_model(
        const canvas3d_detail::CpuModelData& cpu,
        const std::string& preview_key,
        const canvas3d_detail::SceneModelGpu& shared_model,
        std::string& error);

    void apply_scene_put_between_preview_result();

    void upload_pending_scene_models();

    bool ensure_scene_outline_pipeline(std::string& error);

    bool ensure_scene_pick_pipeline(std::string& error);

    bool ensure_scene_pipeline(std::string& error);

    bool ensure_scene_marker_pipeline(std::string& error);

    bool ensure_scene_depth_states(std::string& error);

    static canvas3d_detail::SceneInstanceData make_instance_data(const canvas3d_detail::Mat4& world);

    static canvas3d_detail::SceneInstanceData make_instance_data_relative(const double world[16], canvas3d_detail::DVec3 origin);

    static canvas3d_detail::SceneInstanceData make_chunk_instance_data(canvas3d_detail::DVec3 chunk_origin, canvas3d_detail::DVec3 render_origin);

    bool ensure_instance_buffer(ID3D11Buffer*& buffer, UINT& capacity,
                                const std::vector<canvas3d_detail::SceneInstanceData>& instances,
                                std::string& error);

    void release_scene_mileage_highlight_resources();

    bool ensure_scene_mileage_highlight_resources(std::string& error);

    void bind_scene_instanced_mesh(ID3D11Buffer* vb, ID3D11Buffer* ib, ID3D11Buffer* instance_buffer);

    void draw_scene_mesh(ID3D11Buffer* vb, ID3D11Buffer* ib, ID3D11Buffer* instance_buffer,
                         const std::vector<canvas3d_detail::MeshPart>& mesh_parts,
                         const std::vector<canvas3d_detail::GpuMaterial>& mesh_materials,
                         UINT instance_count,
                         const canvas3d_detail::Mat4& view_proj,
                         ID3D11RasterizerState* base_rasterizer,
                         bool mask_pass = false,
                         const canvas3d_detail::SceneFogSample* fog = nullptr);

    void draw_scene_model(canvas3d_detail::SceneModelGpu& model,
                          const std::vector<canvas3d_detail::SceneInstanceData>& instances,
                          const canvas3d_detail::Mat4& view_proj,
                          const canvas3d_detail::SceneFogSample* fog = nullptr);

    void draw_scene_mileage_highlight(canvas3d_detail::DVec3 render_origin,
                                      const canvas3d_detail::Mat4& view_proj,
                                      std::string& error);

    bool draw_scene_pick_model(canvas3d_detail::SceneModelGpu& model,
                               const std::vector<canvas3d_detail::SceneInstanceData>& instances,
                               const canvas3d_detail::Mat4& view_proj,
                               int object_index,
                               std::string& error);

    bool begin_scene_pick_at_mouse(
        const std::map<std::string, std::vector<canvas3d_detail::SceneInstanceData>>& visible_instances,
        const std::map<int, std::vector<canvas3d_detail::SceneVisibleInstanceRef>>& object_refs,
        const canvas3d_detail::Mat4& view_proj,
        int width,
        int height,
        ImVec2 mouse_local,
        std::string& error);

    canvas3d_detail::ScenePickTarget finish_scene_pick_at_mouse(int width,
                                                int height,
                                                ImVec2 mouse_local,
                                                std::string& error);

    void composite_scene_highlight_outline(int width, int height,
                                           ImVec2 screen_min,
                                           ImVec2 screen_max,
                                           ImVec4 color);

    void draw_scene_highlight_mask_batches(const std::map<std::string, std::vector<canvas3d_detail::SceneInstanceData>>& batches,
                                           const canvas3d_detail::Mat4& view_proj,
                                           std::string& error);

    void draw_scene_model_highlight_outlines(const std::vector<canvas3d_detail::SceneHighlightInstance>& instances,
                                             const canvas3d_detail::Mat4& view_proj,
                                             int width,
                                             int height,
                                             ImVec2 screen_min,
                                             ImVec2 screen_max,
                                             ImVec4 color,
                                             bool separate_instances);

    void draw_scene_highlight_batch(const canvas3d_detail::SceneHighlightBatch& batch,
                                    const canvas3d_detail::Mat4& view_proj,
                                    int width,
                                    int height);


    static bool scene_chunk_visible(const canvas3d_detail::SceneChunk& chunk, double visible_min, double visible_max);

    void draw_scene_track_chunk(canvas3d_detail::SceneTrackChunkGpu& track,
                                canvas3d_detail::DVec3 render_origin,
                                const canvas3d_detail::Mat4& view_proj,
                                std::vector<canvas3d_detail::SceneInstanceData>& track_instance,
                                std::string& error,
                                const canvas3d_detail::SceneFogSample* fog = nullptr);

    bool bind_scene_marker_chunk(const canvas3d_detail::SceneMarkerChunkGpu& chunk,
                                 ID3D11Buffer* index_buffer,
                                 canvas3d_detail::DVec3 render_origin,
                                 const canvas3d_detail::Mat4& view_proj);

    void draw_scene_marker_chunk(const canvas3d_detail::SceneMarkerChunkGpu& chunk,
                                 canvas3d_detail::DVec3 render_origin,
                                 const canvas3d_detail::Mat4& view_proj);

    bool scene_marker_pick_ids_valid() const;

    bool draw_scene_marker_pick_chunk(const canvas3d_detail::SceneMarkerChunkGpu& chunk,
                                      canvas3d_detail::DVec3 render_origin,
                                      const canvas3d_detail::Mat4& view_proj);

    bool scene_marker_font_cache_current() const;

    void draw_visible_scene_markers(double visible_min,
                                    double visible_max,
                                    canvas3d_detail::DVec3 render_origin,
                                    const canvas3d_detail::Mat4& view_proj);

    bool draw_visible_scene_marker_picks(double visible_min,
                                         double visible_max,
                                         canvas3d_detail::DVec3 render_origin,
                                         const canvas3d_detail::Mat4& view_proj);

    bool has_visible_scene_marker_picks(double visible_min,
                                        double visible_max) const;

    bool scene_object_index_valid(int object_index) const;

    bool project_scene_point(canvas3d_detail::DVec3 relative_point,
                             const canvas3d_detail::Mat4& view_proj,
                             int width,
                             int height,
                             ImVec2& screen) const;

    void draw_scene_marker_highlight(size_t marker_index,
                                     canvas3d_detail::DVec3 render_origin,
                                     const canvas3d_detail::Mat4& view_proj,
                                     int width,
                                     int height);

    bool compute_scene_instance_screen_bounds(const double world[16],
                                              const canvas3d_detail::SceneModelGpu& model,
                                              canvas3d_detail::DVec3 render_origin,
                                              const canvas3d_detail::Mat4& view_proj,
                                              int width,
                                              int height,
                                              canvas3d_detail::SceneScreenBounds& out) const;

    bool ensure_pipeline(std::string& error);

    bool build_scene_chunks(std::string& error);

    static void append_track_quad(std::vector<canvas3d_detail::GpuVertex>& vertices,
                                  std::vector<unsigned int>& indices,
                                  canvas3d_detail::Vec3 a, canvas3d_detail::Vec3 b, canvas3d_detail::Vec3 side0, canvas3d_detail::Vec3 side1, float half_width);

    static canvas3d_detail::Vec3 vec3_from_dvec3(canvas3d_detail::DVec3 v);

    static void track_marker_frame(const Canvas3DTrackPoint& point, canvas3d_detail::Vec3& right, canvas3d_detail::Vec3& up);

    static void append_track_segment(std::vector<canvas3d_detail::GpuVertex>& vertices,
                                     std::vector<unsigned int>& indices,
                                     canvas3d_detail::DVec3 origin,
                                     const Canvas3DTrackPoint& p0,
                                     const Canvas3DTrackPoint& p1);

    bool upload_track_chunk(canvas3d_detail::SceneTrackChunkGpu& chunk,
                            const std::vector<canvas3d_detail::GpuVertex>& vertices,
                            const std::vector<unsigned int>& indices,
                            std::string& error);

    bool build_scene_track_chunks(std::string& error);

    static unsigned int append_scene_marker_vertex(
        std::vector<canvas3d_detail::SceneMarkerVertex>& vertices,
        canvas3d_detail::DVec3 origin,
        canvas3d_detail::DVec3 center,
        canvas3d_detail::DVec3 right,
        canvas3d_detail::DVec3 up,
        canvas3d_detail::DVec3 forward,
        float local_x,
        float local_y,
        float face_sign,
        float u,
        float v,
        ImU32 color,
        bool textured);

    static void append_scene_marker_quad(
        std::vector<canvas3d_detail::SceneMarkerVertex>& vertices,
        std::vector<unsigned int>& indices,
        canvas3d_detail::DVec3 origin,
        canvas3d_detail::DVec3 center,
        canvas3d_detail::DVec3 right,
        canvas3d_detail::DVec3 up,
        canvas3d_detail::DVec3 forward,
        float x0,
        float y0,
        float x1,
        float y1,
        float face_sign,
        ImU32 color,
        bool textured = false,
        ImVec2 uv0 = ImVec2(0.0f, 0.0f),
        ImVec2 uv1 = ImVec2(0.0f, 0.0f));

    static void append_scene_marker_triangle(
        std::vector<canvas3d_detail::SceneMarkerVertex>& vertices,
        std::vector<unsigned int>& indices,
        canvas3d_detail::DVec3 origin,
        canvas3d_detail::DVec3 center,
        canvas3d_detail::DVec3 right,
        canvas3d_detail::DVec3 up,
        canvas3d_detail::DVec3 forward,
        ImVec2 p0,
        ImVec2 p1,
        ImVec2 p2,
        float face_sign,
        ImU32 color);

    static ImVec2 scene_marker_icon_point(ImVec2 point);

    static void append_scene_marker_line(
        std::vector<canvas3d_detail::SceneMarkerVertex>& vertices,
        std::vector<unsigned int>& indices,
        canvas3d_detail::DVec3 origin,
        canvas3d_detail::DVec3 center,
        canvas3d_detail::DVec3 right,
        canvas3d_detail::DVec3 up,
        canvas3d_detail::DVec3 forward,
        ImVec2 a,
        ImVec2 b,
        float thickness,
        float face_sign,
        ImU32 color);

    static void append_scene_marker_glyph(
        std::vector<canvas3d_detail::SceneMarkerVertex>& vertices,
        std::vector<unsigned int>& indices,
        canvas3d_detail::DVec3 origin,
        canvas3d_detail::DVec3 center,
        canvas3d_detail::DVec3 right,
        canvas3d_detail::DVec3 up,
        canvas3d_detail::DVec3 forward,
        ImFontBaked& baked,
        ImWchar codepoint,
        float cursor_x,
        float top_y,
        float scale,
        float face_sign,
        ImU32 color);

    static unsigned int decode_scene_marker_utf8(const char*& cursor,
                                                  const char* end);

    static void append_scene_marker_text(
        std::vector<canvas3d_detail::SceneMarkerVertex>& vertices,
        std::vector<unsigned int>& indices,
        canvas3d_detail::DVec3 origin,
        canvas3d_detail::DVec3 center,
        canvas3d_detail::DVec3 right,
        canvas3d_detail::DVec3 up,
        canvas3d_detail::DVec3 forward,
        ImFont& font,
        ImFontBaked& baked,
        float font_size,
        const std::string& text,
        float face_sign,
        ImU32 color,
        float text_height,
        float center_y,
        float max_width);

    static void append_scene_marker_icon_glyph(
        std::vector<canvas3d_detail::SceneMarkerVertex>& vertices,
        std::vector<unsigned int>& indices,
        canvas3d_detail::DVec3 origin,
        canvas3d_detail::DVec3 center,
        canvas3d_detail::DVec3 right,
        canvas3d_detail::DVec3 up,
        canvas3d_detail::DVec3 forward,
        ImFontBaked& baked,
        char glyph,
        float face_sign,
        ImU32 color,
        float offset_x,
        float offset_y);

    static void append_scene_marker_icon(
        std::vector<canvas3d_detail::SceneMarkerVertex>& vertices,
        std::vector<unsigned int>& indices,
        canvas3d_detail::DVec3 origin,
        canvas3d_detail::DVec3 center,
        canvas3d_detail::DVec3 right,
        canvas3d_detail::DVec3 up,
        canvas3d_detail::DVec3 forward,
        ImFontBaked& baked,
        MapMarkerVisualKind kind,
        MapMarkerIconVariant variant,
        float face_sign,
        const ImVec4* theme_override);

    static void scene_marker_frame(const Canvas3DTrackPoint& point,
                                   canvas3d_detail::DVec3& right,
                                   canvas3d_detail::DVec3& up,
                                   canvas3d_detail::DVec3& forward);

    static canvas3d_detail::SceneMarkerGeometrySpan append_scene_marker_geometry(
        const Canvas3DSceneMarker& marker,
        size_t marker_index,
        canvas3d_detail::DVec3 origin,
        float lateral_offset,
        ImFont& font,
        ImFontBaked& baked,
        float font_size,
        std::vector<canvas3d_detail::SceneMarkerVertex>& vertices,
        std::vector<unsigned int>& indices,
        std::vector<canvas3d_detail::SceneMarkerIndexRange>& ranges);

    void rebuild_scene_mileage_pick_cache();

    bool rebuild_scene_marker_visible_indices(std::string& error);

    bool build_scene_marker_chunks(std::string& error);

    bool set_scene_marker_visibility(
        const Canvas3DSceneMarkerVisibility& visibility,
        std::string& error);

    bool ensure_render_target(int width, int height, std::string& error);

    bool ensure_scene_highlight_mask_target(int width, int height, std::string& error);

    bool ensure_scene_pick_target(int width, int height, std::string& error);

    canvas3d_detail::Vec3 scene_forward() const;

    canvas3d_detail::Vec3 scene_right() const;

    const Canvas3DTrackPath* own_track_path() const;

    bool sample_track_path(const Canvas3DTrackPath& path, double distance, Canvas3DTrackPoint& out) const;

    bool sample_own_track(double distance, Canvas3DTrackPoint& out) const;

    // Camera-specific own-track sampling: identical to sample_own_track inside
    // the real track range, but extrapolates the world position along the
    // first point's heading/gradient for camera mileages before the track
    // start. Ordinary placement/marker/rendering sampling must not use this.
    bool sample_own_track_for_camera(double distance, Canvas3DTrackPoint& out) const;

    const Canvas3DTrackPath* placement_track_path_for_key(const std::string& key) const;

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
                                    canvas3d_detail::StructurePlacementFrame& frame) const;

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
                          double out_world[16]) const;

    bool make_repeater_instance_world(const Canvas3DRepeaterSegment& repeater,
                                      double distance,
                                      double out_world[16]) const;

    void append_visible_model_instance(const std::string& model_path,
                                       const canvas3d_detail::SceneInstanceData& data,
                                       int object_index,
                                       const canvas3d_detail::SceneScreenBounds* bounds,
                                       std::map<std::string, std::vector<canvas3d_detail::SceneInstanceData>>& visible_instances,
                                       std::map<int, std::vector<canvas3d_detail::SceneVisibleInstanceRef>>* object_refs) const;

    static void include_scene_screen_bounds(ImVec2& screen_min,
                                            ImVec2& screen_max,
                                            const canvas3d_detail::SceneVisibleInstanceRef& ref);

    void invalidate_scene_repeater_cache(canvas3d_detail::SceneChunk& chunk);

    void prepare_scene_repeater_cache(canvas3d_detail::SceneChunk& chunk);

    void append_visible_repeater_instances(canvas3d_detail::SceneChunk& chunk,
                                           double visible_min,
                                           double visible_max,
                                           canvas3d_detail::DVec3 render_origin,
                                           const canvas3d_detail::Mat4& view_proj,
                                           int width,
                                           int height,
                                           bool can_pick,
                                           std::map<std::string, std::vector<canvas3d_detail::SceneInstanceData>>& visible_instances,
                                           std::map<int, std::vector<canvas3d_detail::SceneVisibleInstanceRef>>* object_refs);

    bool update_scene_camera_from_owntrack();

    bool reset_scene_camera_pose_at_distance(double distance);

    void reset_scene_camera_tracking();

    static int structure_drag_axis_index(Canvas3DSceneDragAxis axis);

    static Canvas3DSceneDragAxis structure_drag_axis_from_index(size_t index);

    static bool scene_ray_triangle_intersection(canvas3d_detail::DVec3 ray_origin,
                                                canvas3d_detail::DVec3 ray_direction,
                                                canvas3d_detail::DVec3 a,
                                                canvas3d_detail::DVec3 b,
                                                canvas3d_detail::DVec3 c,
                                                double& ray_parameter,
                                                double& barycentric_b,
                                                double& barycentric_c);

    std::optional<double> pick_scene_mileage(ImVec2 mouse_local,
                                             int width,
                                             int height,
                                             double visible_min,
                                             double visible_max) const;

    bool scene_camera_ray(ImVec2 mouse_local, int width, int height,
                          canvas3d_detail::DVec3& ray_origin, canvas3d_detail::DVec3& ray_direction) const;

    static bool closest_axis_parameter(canvas3d_detail::DVec3 axis_origin, canvas3d_detail::DVec3 axis_direction,
                                       canvas3d_detail::DVec3 ray_origin, canvas3d_detail::DVec3 ray_direction,
                                       double& parameter);

    static float point_segment_distance_sq(ImVec2 point, ImVec2 a, ImVec2 b);

    bool update_scene_gizmo_projection(canvas3d_detail::SceneGizmoHandle& gizmo,
                                       bool visible,
                                       int width,
                                       int height);

    bool update_scene_structure_gizmo_projections(int width, int height);

    canvas3d_detail::SceneGizmoHandle* scene_gizmo_handle(canvas3d_detail::SceneGizmoTarget target);

    std::optional<Canvas3DPlacementDragUpdate> handle_scene_structure_gizmo_input(
        bool canvas_hovered, int width, int height, ImVec2 mouse_local);

    bool scene_structure_gizmo_consumes_left_input() const;

    void draw_scene_gizmo_handle(ImDrawList* draw,
                                 ImVec2 canvas_origin,
                                 const canvas3d_detail::SceneGizmoHandle& gizmo,
                                 canvas3d_detail::SceneGizmoTarget target);

    void draw_scene_structure_gizmo(ImDrawList* draw, ImVec2 canvas_origin,
                                    int width, int height);

    void handle_scene_input(bool hovered, bool block_left_drag);

    std::string current_background_path() const;

    void draw_background_model(const canvas3d_detail::Mat4& view_proj, const canvas3d_detail::SceneFogSample* fog);

    double effective_scene_window_forward_m() const;

    float scene_far_z(double window_forward_m) const;

    void render_scene_preview_target(int width, int height, ImVec2 mouse_local,
                                     bool pick_enabled,
                                     bool mileage_pick_enabled);

    void reset_scene_fps_counter();

    void update_scene_fps_counter();

    canvas3d_detail::SceneOverlayLabelLayout scene_overlay_label_layout(
        ImVec2 origin, ImVec2 size, ImVec2 text_size, canvas3d_detail::SceneOverlayCorner corner) const;

    void draw_scene_overlay_label_background(ImDrawList* draw,
                                             const canvas3d_detail::SceneOverlayLabelLayout& layout,
                                             ImVec2 text_size) const;

    void draw_scene_overlay_label(ImDrawList* draw, ImVec2 origin, ImVec2 size,
                                  const char* text, canvas3d_detail::SceneOverlayCorner corner) const;

    void draw_scene_overlay(ImDrawList* draw, ImVec2 origin, ImVec2 size) const;

    void draw_scene_route_overlay(ImDrawList* draw, ImVec2 origin, ImVec2 size,
                                  const Canvas3DSceneUiText& ui_text) const;

    ImU32 scene_instance_metric_color(size_t instance_count) const;

    void draw_scene_metrics_overlay(ImDrawList* draw, ImVec2 origin, ImVec2 size,
                                    const Canvas3DSceneStats& stats) const;

    void draw_scene_loading_overlay(ImDrawList* draw, ImVec2 origin, ImVec2 size,
                                    const char* text) const;

    static const char* scene_object_edit_row_kind(const Canvas3DSceneObject& object);

    Canvas3DSceneContextAction render_scene_context_popup(
        const Canvas3DSceneUiText& ui_text,
        const Canvas3DSceneContextMenuOptions& context_menu_options);

    static bool scene_marker_has_list_target(const Canvas3DSceneMarker& marker);

    static bool scene_marker_has_edit_target(const Canvas3DSceneMarker& marker);

    Canvas3DSceneContextAction render_scene_marker_context_popup(
        const Canvas3DSceneUiText& ui_text,
        const Canvas3DSceneContextMenuOptions& context_menu_options);

    std::optional<double> render_scene_mileage_context_popup(
        const Canvas3DSceneUiText& ui_text,
        const Canvas3DSceneContextMenuOptions& context_menu_options);

    static void draw_scene_mileage_label(ImVec2 origin,
                                         ImVec2 end,
                                         ImVec2 pointer_pos,
                                         double mileage,
                                         const Canvas3DSceneUiText& ui_text);

    Canvas3DSceneFrameResult render_scene_preview(
        ImVec2 requested_size,
        const Canvas3DSceneUiText& ui_text,
        const Canvas3DSceneContextMenuOptions& context_menu_options);

    void render_scene(int width, int height);

    void draw_overlay(ImDrawList* draw, ImVec2 origin, ImVec2 size) const;

    void render(ImVec2 requested_size);

    void release_resources();

    void release_render_target();

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
    std::vector<canvas3d_detail::MeshPart> parts;
    std::vector<canvas3d_detail::GpuMaterial> materials;
    std::vector<canvas3d_detail::SceneInstanceData> model_preview_instances;
    UINT index_count = 0;
    int render_width = 0;
    int render_height = 0;
    canvas3d_detail::Vec3 bounds_min;
    canvas3d_detail::Vec3 bounds_max;
    canvas3d_detail::Vec3 center;
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
    scene_track_sampling::PlacementTrackLookup scene_placement_tracks;
    size_t scene_geometry_generation = 0;
    bool scene_active = false;
    bool scene_fog_enabled = true;
    bool scene_map_draw_distance_enabled = true;
#ifndef NDEBUG
    size_t debug_scene_fog_draw_part_count = 0;
    mutable SceneFrameProfiler scene_frame_profiler;
    bool debug_scene_reference = false;
    bool debug_scene_capture = false;
    std::uint64_t debug_scene_signature = 0;
#endif
    std::vector<canvas3d_detail::SceneChunk> scene_chunks;
    // Cleared with chunks, invalidated by Repeater writes (both old/new ranges)
    // and scene replacement. Tracks are replaced only through load_scene.
    size_t scene_cached_repeater_world_count = 0;
    std::unordered_map<std::string, canvas3d_detail::ScenePlacementInstanceLocation> scene_placement_locations;
    std::unordered_map<std::string, size_t> scene_repeater_locations;
    canvas3d_detail::SceneStructureEditState scene_structure_edit;
    float scene_edit_component_scale = 1.0f;
    std::vector<canvas3d_detail::SceneTrackChunkGpu> scene_track_chunks;
    std::vector<canvas3d_detail::SceneMileagePickPoint> scene_mileage_pick_points;
    ID3D11Buffer* scene_mileage_highlight_vertex_buffer = nullptr;
    ID3D11Buffer* scene_mileage_highlight_index_buffer = nullptr;
    ID3D11Buffer* scene_mileage_highlight_instance_buffer = nullptr;
    UINT scene_mileage_highlight_instance_capacity = 0;
    std::vector<canvas3d_detail::MeshPart> scene_mileage_highlight_parts;
    std::vector<canvas3d_detail::GpuMaterial> scene_mileage_highlight_materials;
    std::vector<canvas3d_detail::SceneInstanceData> scene_mileage_highlight_instances;
    // Static marker vertices/UVs are keyed by scene_data.markers, the 100 m
    // scene chunk layout, and the active ImGui font/atlas identity below.
    // Visibility is view-only state and rewrites only each chunk's dynamic IBs.
    std::vector<canvas3d_detail::SceneMarkerChunkGpu> scene_marker_chunks;
    std::vector<canvas3d_detail::SceneMarkerGpuLocation> scene_marker_locations;
    std::unordered_map<std::string, size_t> scene_sound3d_marker_indices;
    std::array<std::vector<size_t>, canvas3d_detail::k_scene_marker_list_kind_count> scene_marker_target_indices;
    Canvas3DSceneMarkerVisibility scene_marker_visibility;
    ImFont* scene_marker_font = nullptr;
    float scene_marker_font_size = 0.0f;
    ImTextureID scene_marker_font_texture_id = ImTextureID_Invalid;
    int scene_marker_font_texture_unique_id = -1;
    int scene_marker_font_texture_width = 0;
    int scene_marker_font_texture_height = 0;
    std::map<std::string, canvas3d_detail::SceneModelGpu> scene_models;
    std::unordered_set<std::string> scene_put_between_preview_base_model_keys;
    std::unordered_map<std::string, canvas3d_detail::SceneTextureCacheEntry> scene_texture_cache;
    std::unordered_set<std::string> scene_texture_warning_keys;
    size_t scene_model_worker_limit = 0;
    bool scene_texture_cache_enabled = true;
    std::mutex scene_upload_mutex;
    std::vector<canvas3d_detail::CpuModelData> scene_pending_uploads;
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
    std::optional<canvas3d_detail::ScenePutBetweenPreviewJob> scene_put_between_preview_pending;
    std::optional<canvas3d_detail::ScenePutBetweenPreviewResult> scene_put_between_preview_completed;
    std::uint64_t scene_put_between_preview_next_sequence = 0;
    std::uint64_t scene_put_between_preview_latest_sequence = 0;
    canvas3d_detail::DVec3 scene_camera_pos;
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
    canvas3d_detail::SceneHighlightBatch scene_hover_highlight_batch;
    canvas3d_detail::SceneHighlightBatch scene_focus_highlight_batch;
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
    std::atomic<int> debug_texture_allocation_throw_countdown{0};
    std::atomic<int> debug_scene_index_buffer_failure_countdown{0};
#endif
    canvas3d_detail::ModelLoaderClient loader;
    canvas3d_detail::ModelLoaderClient scene_loader;
};
