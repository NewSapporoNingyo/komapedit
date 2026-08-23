/*
 * Copyright (c) 2026 Sapporo_ningyo
 *
 * Licensed under Apache License 2.0; see LICENSE and NOTICE.
 * The GUI uses Dear ImGui and ImPlot; see THIRD_PARTY_NOTICES.md.
 */

#ifdef _MSC_VER
#pragma execution_character_set("utf-8")
#endif

#include "kme.h"
#include "app_settings.h"
#include "debug_headless.h"
#include "touch_input.h"

#include "canvas3D.h"
#include "maploader.h"
#include "numeric_safety.h"
#include "own_track_transition_linkage.h"
#include "repeater_linkage.h"
#include "text_decoder.h"
#include "resource.h"

#include "imgui.h"
#include "imgui_internal.h"
#include "misc/cpp/imgui_stdlib.h"
#include "imgui_impl_dx11.h"
#include "imgui_impl_win32.h"
#include "implot.h"

#include <windows.h>
#if defined(_MSC_VER) && !defined(NDEBUG)
#include <crtdbg.h>
#endif
#include <commdlg.h>
#include <d3d11.h>
#include <shellapi.h>
#include <shlobj.h>
#include <wincodec.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cctype>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <new>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

void App::start_scene_preview() {
    scene_preview_started_ = true;
    show_scene_preview_window_ = true;
    focus_scene_preview_next_ = true;
    scene_preview_preserve_models_on_rebuild_ = false;
    scene_preview_preserve_camera_on_rebuild_ = false;
    pending_scene_preview_started_at_ = std::chrono::steady_clock::now();
    set_program_status("status.scene_preview_loading");
    add_log("[info]gui_kme.cpp: starting 3D scene preview");
    rebuild_scene_preview(false, false);
}

void App::stop_scene_preview() {
    scene_preview_started_ = false;
    scene_preview_dirty_ = true;
    scene_preview_preserve_models_on_rebuild_ = false;
    scene_preview_preserve_camera_on_rebuild_ = false;
    pending_scene_preview_started_at_.reset();
    if (scene_preview_canvas_) scene_preview_canvas_->clear_scene();
    set_program_status("status.scene_preview_stopped");
    add_log("[INFO]3D scene preview stopped");
}

double App::rebuild_scene_preview(bool preserve_loaded_models, bool preserve_camera) {
    if (!scene_preview_canvas_ || !scene_preview_started_) {
        pending_scene_preview_started_at_.reset();
        return 0.0;
    }
    if (!has_model_ || model_.own.empty()) {
        scene_preview_canvas_->clear_scene();
        scene_preview_dirty_ = true;
        scene_preview_preserve_models_on_rebuild_ = false;
        scene_preview_preserve_camera_on_rebuild_ = false;
        pending_scene_preview_started_at_.reset();
        add_log("[warn]gui_kme.cpp: 3D scene preview has no map geometry loaded");
        return 0.0;
    }
    add_log(preserve_loaded_models
                ? "[info]gui_kme.cpp: reloading 3D scene preview track geometry with preserved models"
                : "[info]gui_kme.cpp: generating 3D scene preview track geometry");
    Canvas3DSceneBuildOptions options;
    options.model = &model_;
    options.map_handle = handle_;
    options.unit_distance = unit_distance_;
    options.control_point_interval = cp_interval_;
    options.station_index = station_jump_index_;
    options.show_own_track_markers = show_scene_owntrack_markers_;
    const auto scene_build_started_at = std::chrono::steady_clock::now();
    Canvas3DSceneBuildResult build_result = build_canvas3d_scene_preview(options);
    const double scene_build_seconds = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - scene_build_started_at).count();
    for (const std::string& message : build_result.log_messages) add_log(message);

    size_t track_point_count = 0;
    for (const Canvas3DTrackPath& track : build_result.scene.tracks) {
        track_point_count += track.points.size();
    }
    add_log("[info]gui_kme.cpp: 3D scene preview track geometry ready: tracks=" +
            std::to_string(build_result.scene.tracks.size()) +
            " points=" + std::to_string(track_point_count) +
            " instances=" + std::to_string(build_result.scene.instances.size()) +
            " repeaters=" + std::to_string(build_result.scene.repeaters.size()));

    std::string error;
    if (!scene_preview_canvas_->load_scene(std::move(build_result.scene), error,
                                           preserve_loaded_models, preserve_camera)) {
        add_log("[error]gui_kme.cpp: 3D scene preview failed: " + error);
        scene_preview_dirty_ = true;
        scene_preview_preserve_models_on_rebuild_ = false;
        scene_preview_preserve_camera_on_rebuild_ = false;
        pending_scene_preview_started_at_.reset();
        set_program_status("status.scene_preview_failed");
        return scene_build_seconds;
    }
    sync_scene_preview_marker_visibility();
    Canvas3DSceneStats stats = scene_preview_canvas_->scene_stats();
    std::ostringstream stage_timing;
    stage_timing << std::fixed << std::setprecision(3)
                 << "[info]gui_kme.cpp: 3D scene preview stage timing: scene_build="
                 << scene_build_seconds << " s track_gpu_setup="
                 << stats.track_gpu_setup_seconds << " s model_queue="
                 << stats.model_queue_seconds << " s";
    add_log(stage_timing.str());
    scene_preview_dirty_ = false;
    scene_preview_preserve_models_on_rebuild_ = false;
    scene_preview_preserve_camera_on_rebuild_ = false;
    if (preserve_loaded_models) {
        add_log("[info]gui_kme.cpp: 3D scene preview line geometry reloaded: models_preserved=" +
                std::to_string(stats.model_ready_count) +
                " models_total=" + std::to_string(stats.model_path_count));
    } else {
        add_log("[info]gui_kme.cpp: 3D scene preview model loading queued: models=" +
                std::to_string(stats.model_path_count));
    }
    add_log("[info]gui_kme.cpp: 3D scene preview started: chunks=" + std::to_string(stats.chunk_count) +
            " instances=" + std::to_string(stats.instance_count) +
            " models=" + std::to_string(stats.model_path_count));
    return scene_build_seconds;
}

void App::finish_pending_scene_preview_load_timing() {
    if (!pending_scene_preview_started_at_ || !scene_preview_canvas_) return;
    const Canvas3DSceneStats stats = scene_preview_canvas_->scene_stats();
    if (!stats.active || stats.loading) return;

    const double elapsed_seconds = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - *pending_scene_preview_started_at_).count();
    pending_scene_preview_started_at_.reset();

    const std::string elapsed = format_elapsed_seconds_value(elapsed_seconds);
    add_log("3D preview loaded in " + elapsed + " s");
    set_program_status("status.scene_preview_loaded", elapsed);
}

void App::reload_scene_preview_models() {
    if (!scene_preview_canvas_ || !scene_preview_started_) return;
    std::string error;
    if (!scene_preview_canvas_->reload_scene_models(error)) {
        add_log("[error]gui_kme.cpp: 3D scene preview model reload failed: " + error);
        return;
    }
    Canvas3DSceneStats stats = scene_preview_canvas_->scene_stats();
    add_log("[info]gui_kme.cpp: 3D scene preview model reload queued: models=" +
            std::to_string(stats.model_path_count));
}

void App::sync_scene_preview_track_visibility() {
    if (!scene_preview_canvas_ || !scene_preview_started_) return;

    std::vector<Canvas3DTrackVisibility> visibility =
        build_canvas3d_scene_track_visibility(model_, show_scene_owntrack_markers_);

    std::string error;
    if (!scene_preview_canvas_->set_scene_track_visibility(visibility, error)) {
        add_log("[error]gui_kme.cpp: 3D scene preview track visibility failed: " + error);
    }
}

void App::sync_scene_preview_marker_visibility() {
    if (!scene_preview_canvas_ || !scene_preview_started_) return;

    Canvas3DSceneMarkerVisibility visibility;
    auto set_marker = [&](MapMarkerVisualKind kind, bool visible) {
        if (visible) visibility.marker_mask |= map_marker_visual_bit(kind);
    };
    auto set_label = [&](MapMarkerVisualKind kind, bool visible) {
        if (visible) visibility.label_mask |= map_marker_visual_bit(kind);
    };
    auto set_marker_and_label = [&](MapMarkerVisualKind kind, bool visible) {
        set_marker(kind, visible);
        set_label(kind, visible);
    };

    set_marker(MapMarkerVisualKind::Station, show_stations_);
    set_label(MapMarkerVisualKind::Station,
              show_stations_ && show_station_names_);

    for (MapMarkerVisualKind kind : {
             MapMarkerVisualKind::CurveTransitionStart,
             MapMarkerVisualKind::CurveCircularStart,
             MapMarkerVisualKind::CurveEnd}) {
        set_marker(kind, show_curve_values_);
    }
    for (MapMarkerVisualKind kind : {
             MapMarkerVisualKind::CurveTransitionStart,
             MapMarkerVisualKind::CurveCircularStart,
             MapMarkerVisualKind::CurveEnd}) {
        set_label(kind, show_curve_values_);
    }

    for (MapMarkerVisualKind kind : {
             MapMarkerVisualKind::GradientTransitionStart,
             MapMarkerVisualKind::GradientStart,
             MapMarkerVisualKind::GradientEnd}) {
        set_marker(kind, show_gradient_pos_);
    }
    for (MapMarkerVisualKind kind : {
             MapMarkerVisualKind::GradientTransitionStart,
             MapMarkerVisualKind::GradientStart,
             MapMarkerVisualKind::GradientEnd}) {
        set_label(kind, show_gradient_pos_ && show_gradient_values_);
    }

    set_marker_and_label(MapMarkerVisualKind::SpeedLimit, show_speedlimits_);
    set_marker_and_label(MapMarkerVisualKind::Section, show_section_markers_);
    set_marker_and_label(MapMarkerVisualKind::Beacon, show_beacon_markers_);
    set_marker_and_label(MapMarkerVisualKind::PreTrain, show_pretrain_markers_);
    set_marker_and_label(MapMarkerVisualKind::Irregularity, show_irregularity_markers_);
    set_marker_and_label(MapMarkerVisualKind::MapSound, show_map_sound_markers_);
    set_marker_and_label(MapMarkerVisualKind::MapSound3D, show_map_sound_3d_markers_);
    set_marker_and_label(MapMarkerVisualKind::RollingNoise, show_rolling_noise_markers_);
    set_marker_and_label(MapMarkerVisualKind::FlangeNoise, show_flange_noise_markers_);
    set_marker_and_label(MapMarkerVisualKind::JointNoise, show_joint_noise_markers_);
    set_marker_and_label(MapMarkerVisualKind::Background, show_background_markers_);
    set_marker_and_label(MapMarkerVisualKind::Adhesion, show_adhesion_markers_);
    set_marker_and_label(MapMarkerVisualKind::CabIlluminance, show_cab_illuminance_markers_);
    set_marker_and_label(MapMarkerVisualKind::Fog, show_fog_markers_);
    set_marker_and_label(MapMarkerVisualKind::DrawDistance, show_draw_distance_markers_);
    set_marker_and_label(MapMarkerVisualKind::OtherTrackChange,
                         edit_actions_available());

    std::string error;
    if (!scene_preview_canvas_->set_scene_marker_visibility(
            visibility, error)) {
        add_log("[error]gui_kme.cpp: 3D scene marker visibility failed: " +
                (error.empty() ? std::string("unknown error") : error));
    }
}

void App::render_scene_preview_window() {
    if (scene_preview_canvas_) scene_preview_canvas_->process_scene_loading();
    auto drain_scene_preview_logs = [this]() {
        if (!scene_preview_canvas_) return;
        for (std::string& message : scene_preview_canvas_->drain_scene_load_messages()) {
            add_log(std::move(message));
        }
    };
    drain_scene_preview_logs();
    finish_pending_scene_preview_load_timing();
    if (!show_scene_preview_window_) return;
    if (dock_main_id_) ImGui::SetNextWindowDockID(dock_main_id_, ImGuiCond_FirstUseEver);
    if (focus_scene_preview_next_) ImGui::SetNextWindowFocus();
    std::string title = tr("frame.scene_preview") + "###ScenePreview3D";
    ImGuiStyle& style = ImGui::GetStyle();
    const float button_height = ImGui::GetFrameHeight();
    const float toolbar_padding_y = button_height * 0.25f;
    const float window_padding_x = style.WindowPadding.x;
    const float item_spacing_x = style.ItemSpacing.x;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(window_padding_x, toolbar_padding_y));
    if (ImGui::Begin(title.c_str(), &show_scene_preview_window_)) {
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(item_spacing_x * 1.35f, 0.0f));
        ImGui::BeginDisabled(scene_preview_started_ || load_state_.running || !has_model_);
        if (ImGui::Button(tr("button.start_scene_preview").c_str())) start_scene_preview();
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(!scene_preview_started_ || load_state_.running || !has_model_);
        if (ImGui::Button(tr("button.reload_scene_models").c_str())) reload_scene_preview_models();
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(!scene_preview_started_);
        if (ImGui::Button(tr("button.close").c_str())) stop_scene_preview();
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(tr("frame.mode").c_str());
        ImGui::SameLine();
        Canvas3DSceneInteractionMode scene_mode = scene_preview_canvas_
            ? scene_preview_canvas_->scene_interaction_mode()
            : Canvas3DSceneInteractionMode::Move;
        if (ImGui::RadioButton((tr("mode.pan") + "##scene_preview_move").c_str(),
                               scene_mode == Canvas3DSceneInteractionMode::Move)) {
            scene_mode = Canvas3DSceneInteractionMode::Move;
            if (scene_preview_canvas_) scene_preview_canvas_->set_scene_interaction_mode(scene_mode);
        }
        ImGui::SameLine();
        if (ImGui::RadioButton((tr("mode.select") + "##scene_preview_select").c_str(),
                               scene_mode == Canvas3DSceneInteractionMode::Select)) {
            scene_mode = Canvas3DSceneInteractionMode::Select;
            if (scene_preview_canvas_) scene_preview_canvas_->set_scene_interaction_mode(scene_mode);
        }
        ImGui::SameLine();
        if (ImGui::RadioButton((tr("mode.mileage_select") + "##scene_preview_mileage_select").c_str(),
                               scene_mode == Canvas3DSceneInteractionMode::MileageSelect)) {
            scene_mode = Canvas3DSceneInteractionMode::MileageSelect;
            if (scene_preview_canvas_) scene_preview_canvas_->set_scene_interaction_mode(scene_mode);
        }
        ImGui::PopStyleVar();
        if (scene_preview_started_ && scene_preview_dirty_ && has_model_ && !load_state_.running) {
            const bool preserve_loaded_models = scene_preview_preserve_models_on_rebuild_;
            const bool preserve_camera = scene_preview_preserve_camera_on_rebuild_;
            rebuild_scene_preview(preserve_loaded_models, preserve_camera);
        }
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + toolbar_padding_y);
        ImVec2 avail = ImGui::GetContentRegionAvail();
        Canvas3DSceneUiText scene_ui_text;
        scene_ui_text.mileage = tr("info.mileage").c_str();
        scene_ui_text.unit_m = tr("unit.m").c_str();
        scene_ui_text.add_map_element_at_mileage =
            tr("menu.add_map_element_at_current_mileage").c_str();
        scene_ui_text.switch_signal_aspect = tr("menu.switch_signal_aspect").c_str();
        scene_ui_text.element_properties = tr("dialog.element_properties").c_str();
        scene_ui_text.delete_element = tr("button.delete").c_str();
        scene_ui_text.unpaired_transition = tr("status.transition_unpaired").c_str();
        scene_ui_text.delete_repeater_all = tr("menu.repeater_delete_all").c_str();
        scene_ui_text.delete_repeater_change_point =
            tr("menu.repeater_delete_change_point").c_str();
        scene_ui_text.trim_repeater_to_change_point =
            tr("menu.repeater_trim_to_change_point").c_str();
        scene_ui_text.start_repeater_from_change_point =
            tr("menu.repeater_start_from_change_point").c_str();
        scene_ui_text.locate_structure_list = tr("menu.locate_in_structure_list").c_str();
        scene_ui_text.locate_structure_put_between_list = tr("menu.locate_in_structure_put_between_list").c_str();
        scene_ui_text.locate_repeater_list = tr("menu.locate_in_repeater_list").c_str();
        scene_ui_text.locate_signal_list = tr("menu.locate_in_signal_list").c_str();
        auto set_marker_list_label = [&scene_ui_text](Canvas3DSceneMarkerListKind kind,
                                                       const std::string& label) {
            scene_ui_text.locate_marker_list_labels[static_cast<size_t>(kind)] = label.c_str();
        };
        set_marker_list_label(Canvas3DSceneMarkerListKind::Beacon,
                              tr("menu.locate_in_beacon_list"));
        set_marker_list_label(Canvas3DSceneMarkerListKind::Section,
                              tr("menu.locate_in_section_list"));
        set_marker_list_label(Canvas3DSceneMarkerListKind::Irregularity,
                              tr("menu.locate_in_irregularity_list"));
        set_marker_list_label(Canvas3DSceneMarkerListKind::MapSound,
                              tr("menu.locate_in_map_sound_list"));
        set_marker_list_label(Canvas3DSceneMarkerListKind::MapSound3D,
                              tr("menu.locate_in_map_sound_3d_list"));
        set_marker_list_label(Canvas3DSceneMarkerListKind::RollingNoise,
                              tr("menu.locate_in_rolling_noise_list"));
        set_marker_list_label(Canvas3DSceneMarkerListKind::FlangeNoise,
                              tr("menu.locate_in_flange_noise_list"));
        set_marker_list_label(Canvas3DSceneMarkerListKind::JointNoise,
                              tr("menu.locate_in_joint_noise_list"));
        set_marker_list_label(Canvas3DSceneMarkerListKind::Background,
                              tr("menu.locate_in_background_list"));
        set_marker_list_label(Canvas3DSceneMarkerListKind::Adhesion,
                              tr("menu.locate_in_adhesion_list"));
        set_marker_list_label(Canvas3DSceneMarkerListKind::CabIlluminance,
                              tr("menu.locate_in_cab_illuminance_list"));
        set_marker_list_label(Canvas3DSceneMarkerListKind::Fog,
                              tr("menu.locate_in_fog_list"));
        set_marker_list_label(Canvas3DSceneMarkerListKind::LegacyFog,
                              tr("menu.locate_in_legacy_fog_list"));
        set_marker_list_label(Canvas3DSceneMarkerListKind::DrawDistance,
                              tr("menu.locate_in_draw_distance_list"));
        set_marker_list_label(Canvas3DSceneMarkerListKind::SpeedLimit,
                              tr("menu.locate_in_speed_limit_list"));
        scene_ui_text.jump_to_repeater_start_position = tr("menu.jump_to_repeater_start_position").c_str();
        scene_ui_text.jump_to_repeater_end_or_change_position =
            tr("menu.jump_to_repeater_end_or_change_position").c_str();
        scene_ui_text.loading = tr("status.scene_loading").c_str();
        scene_ui_text.straight = tr("scene.route_info.straight").c_str();
        scene_ui_text.interpolate_unsupported = tr("scene.route_info.interpolate_unsupported").c_str();
        scene_ui_text.next_station = tr("scene.route_info.next_station").c_str();
        scene_ui_text.speed_limit = tr("scene.route_info.speed_limit").c_str();
        scene_ui_text.signal = tr("scene.route_info.signal").c_str();
        scene_ui_text.no_station_ahead = tr("scene.route_info.no_station_ahead").c_str();
        Canvas3DSceneContextMenuOptions context_menu_options;
        context_menu_options.element_properties_enabled = edit_actions_available();
        context_menu_options.new_element_enabled = edit_actions_available();
        sync_scene_placement_edit_from_inspector();
        sync_scene_preview_marker_visibility();
        Canvas3DSceneFrameResult scene_result =
            scene_preview_canvas_->render_scene_preview(avail, scene_ui_text, context_menu_options);
        if (scene_result.placement_drag) {
            apply_scene_placement_drag_update(*scene_result.placement_drag);
        }
        if (scene_result.new_element_mileage) {
            open_new_element_wizard(scene_result.new_element_mileage);
        }
        const Canvas3DSceneContextAction& scene_action = scene_result.context_action;
        if (scene_action.kind == Canvas3DSceneContextActionKind::LocateStructure) {
            locate_structure_row_in_list(scene_action.row_index);
        } else if (scene_action.kind == Canvas3DSceneContextActionKind::LocateRepeater) {
            locate_repeater_row_in_list(scene_action.row_index);
        } else if (scene_action.kind == Canvas3DSceneContextActionKind::LocateSignal) {
            locate_signal_row_in_list(scene_action.row_index);
        } else if (scene_action.kind == Canvas3DSceneContextActionKind::LocateMarkerList) {
            locate_scene_marker_row_in_list(scene_action.marker_list_kind, scene_action.row_index);
        } else if (scene_action.kind == Canvas3DSceneContextActionKind::EditElement) {
            request_element_inspector(scene_action.edit_id, scene_action.row_kind);
        } else if (scene_action.kind == Canvas3DSceneContextActionKind::DeleteElement) {
            request_element_delete(scene_action.edit_id, scene_action.row_kind);
        } else if (scene_action.kind == Canvas3DSceneContextActionKind::DeleteRepeaterAll) {
            request_element_delete(scene_action.edit_id, scene_action.row_kind,
                                   RepeaterDeleteMode::EntireChain);
        } else if (scene_action.kind == Canvas3DSceneContextActionKind::DeleteRepeaterChangePoint) {
            request_element_delete(scene_action.edit_id, scene_action.row_kind,
                                   RepeaterDeleteMode::ChangePoint);
        } else if (scene_action.kind == Canvas3DSceneContextActionKind::TrimRepeaterToChangePoint) {
            request_element_delete(scene_action.edit_id, scene_action.row_kind,
                                   RepeaterDeleteMode::TrimToChangePoint);
        } else if (scene_action.kind == Canvas3DSceneContextActionKind::StartRepeaterFromChangePoint) {
            request_element_delete(scene_action.edit_id, scene_action.row_kind,
                                   RepeaterDeleteMode::StartFromChangePoint);
        }
        drain_scene_preview_logs();
    }
    focus_scene_preview_next_ = false;
    ImGui::End();
    ImGui::PopStyleVar();
}

void App::preview_structure_model(const std::string& path) {
    if (path.empty()) {
        add_log("[WARN]model preview: empty model path");
        return;
    }
    show_model_preview_window_ = true;
    focus_model_preview_next_ = true;
    std::string error;
    if (!model_preview_canvas_->load_model(path, error)) {
        add_log("[ERROR]model preview: " + error);
        return;
    }
    for (std::string& warning : model_preview_canvas_->drain_model_load_warnings()) {
        add_log(std::move(warning));
    }
    add_log("[INFO]model preview: " + path);
}

void App::reload_model_preview() {
    if (!model_preview_canvas_ || !model_preview_canvas_->has_model()) return;
    std::string path = model_preview_canvas_->model_path();
    std::string error;
    if (!model_preview_canvas_->reload_model(error)) {
        add_log("[ERROR]model preview reload: " + error);
        return;
    }
    for (std::string& warning : model_preview_canvas_->drain_model_load_warnings()) {
        add_log(std::move(warning));
    }
    add_log("[INFO]model preview reloaded: " + path);
}

void App::perform_reload_current_map_and_model_preview() {
    if (has_model_ && !file_path_.empty()) {
        begin_load(file_path_, true, false, std::nullopt, false, true);
    }
    reload_model_preview();
}

void App::perform_reload_current_map_geometry() {
    add_log("[info]gui_kme.cpp: reloading map geometry with existing 3D models preserved");
    begin_load(file_path_, true, false, std::nullopt, true, true);
}

bool App::confirm_reload_if_unsaved(PendingReloadAction action) {
    if (!has_unsaved_edit_state() || !has_model_ || file_path_.empty()) return false;
    pending_reload_action_ = action;
    popups_.reload_unsaved_confirm = true;
    return true;
}

void App::execute_pending_reload_action() {
    PendingReloadAction action = pending_reload_action_;
    pending_reload_action_ = PendingReloadAction::None;
    if (action == PendingReloadAction::MapAndModelPreview) {
        perform_reload_current_map_and_model_preview();
    } else if (action == PendingReloadAction::GeometryOnly) {
        perform_reload_current_map_geometry();
    }
}

void App::reload_current_map_and_model_preview() {
    if (load_state_.running) return;
    if (confirm_reload_if_unsaved(PendingReloadAction::MapAndModelPreview)) return;
    perform_reload_current_map_and_model_preview();
}

void App::reload_current_map_geometry() {
    if (load_state_.running || !has_model_ || file_path_.empty()) return;
    if (confirm_reload_if_unsaved(PendingReloadAction::GeometryOnly)) return;
    perform_reload_current_map_geometry();
}

void App::render_model_preview_window() {
    if (!show_model_preview_window_) return;
    if (dock_main_id_) ImGui::SetNextWindowDockID(dock_main_id_, ImGuiCond_FirstUseEver);
    if (focus_model_preview_next_) ImGui::SetNextWindowFocus();
    std::string title = tr("frame.model_preview") + "###ModelPreview3D";
    ImGuiStyle& style = ImGui::GetStyle();
    const float button_height = ImGui::GetFrameHeight();
    const float toolbar_padding_y = button_height * 0.25f;
    const float window_padding_x = style.WindowPadding.x;
    const float item_spacing_x = style.ItemSpacing.x;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(window_padding_x, toolbar_padding_y));
    if (ImGui::Begin(title.c_str(), &show_model_preview_window_)) {
        const bool has_preview_model = model_preview_canvas_ && model_preview_canvas_->has_model();
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(item_spacing_x * 1.35f, 0.0f));

        ImGui::BeginDisabled(show_structure_models_window_);
        if (ImGui::Button(tr("button.model_list").c_str())) show_structure_models_window_ = true;
        ImGui::EndDisabled();
        ImGui::SameLine();

        ImGui::BeginDisabled(!has_preview_model);
        if (ImGui::Button(tr("button.reload").c_str())) reload_model_preview();
        ImGui::EndDisabled();
        ImGui::SameLine();

        ImGui::BeginDisabled(!has_preview_model);
        if (ImGui::Button(tr("button.clear").c_str())) model_preview_canvas_->clear_model();
        ImGui::EndDisabled();
        ImGui::SameLine();

        if (ImGui::Button(tr("button.background_color").c_str())) {
            ImGui::OpenPopup("model_preview_bg_color_popup");
        }
        if (ImGui::BeginPopup("model_preview_bg_color_popup")) {
            const ImGuiColorEditFlags color_flags = ImGuiColorEditFlags_NoAlpha
                | ImGuiColorEditFlags_DisplayRGB
                | ImGuiColorEditFlags_InputRGB
                | ImGuiColorEditFlags_Uint8
                | ImGuiColorEditFlags_PickerHueBar;
            ImGui::SetNextItemWidth(260.0f);
            if (ImGui::ColorPicker3("##model_preview_bg_color_picker", &model_preview_bg_color_.x, color_flags)) {
                model_preview_bg_color_ = clamp_theme_color(model_preview_bg_color_);
                model_preview_canvas_->set_background_color(model_preview_bg_color_);
            }
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + ImGui::GetStyle().WindowPadding.y);
            const float swatch_size = ImGui::GetFrameHeight();
            const std::array<std::pair<const char*, ImVec4>, 5> quick_colors = {{
                {"color.white", ImVec4(1.0f, 1.0f, 1.0f, 1.0f)},
                {"color.black", ImVec4(0.0f, 0.0f, 0.0f, 1.0f)},
                {"color.gray", ImVec4(0.5f, 0.5f, 0.5f, 1.0f)},
                {"color.blue", ImVec4(0.0f, 0.0f, 1.0f, 1.0f)},
                {"color.green", ImVec4(0.0f, 1.0f, 0.0f, 1.0f)},
            }};
            for (size_t i = 0; i < quick_colors.size(); ++i) {
                if (i > 0) ImGui::SameLine();
                std::string id = "##model_preview_quick_" + std::to_string(i);
                if (ImGui::ColorButton(id.c_str(), quick_colors[i].second, ImGuiColorEditFlags_NoAlpha,
                                       ImVec2(swatch_size, swatch_size))) {
                    model_preview_bg_color_ = quick_colors[i].second;
                    model_preview_canvas_->set_background_color(model_preview_bg_color_);
                }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tr(quick_colors[i].first).c_str());
            }
            ImGui::EndPopup();
        }
        ImGui::PopStyleVar();
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + toolbar_padding_y);
        ImVec2 avail = ImGui::GetContentRegionAvail();
        model_preview_canvas_->render(avail);
    }
    focus_model_preview_next_ = false;
    ImGui::End();
    ImGui::PopStyleVar();
}
