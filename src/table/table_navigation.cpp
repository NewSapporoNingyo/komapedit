/*
 * Copyright (c) 2026 Sapporo_ningyo
 *
 * Licensed under Apache License 2.0; see LICENSE and NOTICE.
 * The GUI uses Dear ImGui and ImPlot; see THIRD_PARTY_NOTICES.md.
 */

#include "kme.h"

#include "canvas3D.h"

#include <algorithm>
#include <cstddef>
void App::invalidate_table_cache() {
    table_cache_ = TableUiCache{};
    reset_structure_model_find_results();
    reset_signal_aspect_find_results();
    reset_sound_file_find_results(false);
    reset_sound_file_find_results(true);
    structure_list_scroll_row_ = -1;
    structure_list_highlight_row_ = -1;
    repeater_list_scroll_row_ = -1;
    repeater_list_highlight_row_ = -1;
    signal_list_scroll_row_ = -1;
    signal_list_highlight_row_ = -1;
    section_list_scroll_row_ = -1;
    section_list_highlight_row_ = -1;
    other_train_stop_list_scroll_row_ = -1;
    other_train_stop_list_highlight_row_ = -1;
    beacon_list_scroll_row_ = -1;
    beacon_list_highlight_row_ = -1;
    irregularity_list_scroll_row_ = -1;
    irregularity_list_highlight_row_ = -1;
    map_sound_list_scroll_row_ = -1;
    map_sound_list_highlight_row_ = -1;
    map_sound_3d_list_scroll_row_ = -1;
    map_sound_3d_list_highlight_row_ = -1;
    rolling_noise_list_scroll_row_ = -1;
    rolling_noise_list_highlight_row_ = -1;
    flange_noise_list_scroll_row_ = -1;
    flange_noise_list_highlight_row_ = -1;
    joint_noise_list_scroll_row_ = -1;
    joint_noise_list_highlight_row_ = -1;
    background_list_scroll_row_ = -1;
    background_list_highlight_row_ = -1;
    adhesion_list_scroll_row_ = -1;
    adhesion_list_highlight_row_ = -1;
    cab_illuminance_list_scroll_row_ = -1;
    cab_illuminance_list_highlight_row_ = -1;
    fog_list_scroll_row_ = -1;
    fog_list_highlight_row_ = -1;
    legacy_fog_list_scroll_row_ = -1;
    legacy_fog_list_highlight_row_ = -1;
    draw_distance_list_scroll_row_ = -1;
    draw_distance_list_highlight_row_ = -1;
    speed_limit_list_scroll_row_ = -1;
    speed_limit_list_highlight_row_ = -1;
    plan_context_menu_entries_.clear();
    profile_context_menu_entries_.clear();
    plan_marker_selection_.clear();
}

void App::reset_marker_visibility() {
    structure_row_visible_.assign(structure_marker_cache_.size(), 0);
    repeater_row_visible_.assign(repeater_marker_cache_.size(), 0);
    signal_row_visible_.assign(signal_marker_cache_.size(), 0);
    other_train_path_visible_.assign(model_.other_trains.size(), 0);
}

void App::sync_marker_visibility_sizes() {
    structure_row_visible_.resize(structure_marker_cache_.size(), 0);
    repeater_row_visible_.resize(repeater_marker_cache_.size(), 0);
    signal_row_visible_.resize(signal_marker_cache_.size(), 0);
    other_train_path_visible_.resize(model_.other_trains.size(), 0);
}

void App::locate_structure_row_on_plan(size_t row_index) {
    sync_marker_visibility_sizes();
    if (row_index >= structure_marker_cache_.size() || !structure_marker_cache_[row_index]) return;
    if (row_index < structure_row_visible_.size()) structure_row_visible_[row_index] = 1;
    const PlanStructureMarker& marker = *structure_marker_cache_[row_index];
    focus_plan_at_model_point(marker.x, marker.y);
}

void App::locate_structure_row_in_list(size_t row_index) {
    sync_marker_visibility_sizes();
    if (row_index >= structure_marker_cache_.size() || !structure_marker_cache_[row_index]) return;
    const bool put_between = row_index >= model_.structures.size();
    if (put_between) {
        show_structures_between_window_ = true;
        focus_structures_between_next_ = true;
    } else {
        show_structures_window_ = true;
        focus_structures_next_ = true;
    }
    structure_list_scroll_row_ = static_cast<int>(row_index);
    structure_list_highlight_row_ = static_cast<int>(row_index);
}

void App::locate_structure_row_in_scene_preview(size_t row_index) {
    if (!can_locate_scene_preview_row()) return;
    if (!scene_preview_canvas_->jump_scene_camera_to_object(Canvas3DSceneObjectKind::Structure, row_index)) return;
    show_scene_preview_window_ = true;
    focus_scene_preview_next_ = true;
}

void App::locate_repeater_row_on_plan(size_t row_index) {
    sync_marker_visibility_sizes();
    if (row_index >= repeater_marker_cache_.size() || !repeater_marker_cache_[row_index].begin_marker) return;
    if (row_index < repeater_row_visible_.size()) repeater_row_visible_[row_index] = 1;
    const PlanRepeaterMarker& marker = *repeater_marker_cache_[row_index].begin_marker;
    focus_plan_at_model_point(marker.x, marker.y);
}

void App::locate_repeater_row_in_list(size_t row_index) {
    sync_marker_visibility_sizes();
    if (row_index >= repeater_marker_cache_.size()) return;
    show_repeaters_window_ = true;
    focus_repeaters_next_ = true;
    repeater_list_scroll_row_ = static_cast<int>(row_index);
    repeater_list_highlight_row_ = static_cast<int>(row_index);
}

void App::locate_repeater_row_in_scene_preview(size_t row_index) {
    if (!scene_preview_started_ || !scene_preview_canvas_ || !scene_preview_canvas_->has_scene()) return;
    if (!scene_preview_canvas_->jump_scene_camera_to_object(Canvas3DSceneObjectKind::Repeater, row_index)) return;
    show_scene_preview_window_ = true;
    focus_scene_preview_next_ = true;
}

void App::locate_signal_row_on_plan(size_t row_index) {
    sync_marker_visibility_sizes();
    if (row_index >= signal_marker_cache_.size() || !signal_marker_cache_[row_index]) return;
    if (row_index < signal_row_visible_.size()) signal_row_visible_[row_index] = 1;
    const PlanSignalMarker& marker = *signal_marker_cache_[row_index];
    focus_plan_at_model_point(marker.x, marker.y);
}

void App::locate_signal_row_in_list(size_t row_index) {
    sync_marker_visibility_sizes();
    if (row_index >= signal_marker_cache_.size() || !signal_marker_cache_[row_index]) return;
    show_signals_window_ = true;
    focus_signals_next_ = true;
    signal_list_scroll_row_ = static_cast<int>(row_index);
    signal_list_highlight_row_ = static_cast<int>(row_index);
}

void App::locate_signal_row_in_scene_preview(size_t row_index) {
    if (!can_locate_scene_preview_row()) return;
    if (!scene_preview_canvas_->jump_scene_camera_to_object(
            Canvas3DSceneObjectKind::Signal, row_index)) {
        return;
    }
    show_scene_preview_window_ = true;
    focus_scene_preview_next_ = true;
}

void App::locate_standard_marker_on_plan(
    const std::vector<std::optional<PlanMarker>>& cache,
    size_t row_index, bool& markers_visible) {
    if (row_index >= cache.size() || !cache[row_index]) return;
    markers_visible = true;
    focus_plan_at_model_point(cache[row_index]->x, cache[row_index]->y);
}

void App::locate_standard_marker_in_list(
    const std::vector<std::optional<PlanMarker>>& cache,
    size_t row_index, bool& window_visible, bool& focus_window,
    int& scroll_row, int& highlight_row) {
    if (row_index >= cache.size() || !cache[row_index]) return;
    window_visible = true;
    focus_window = true;
    scroll_row = static_cast<int>(row_index);
    highlight_row = static_cast<int>(row_index);
}

void App::locate_beacon_row_on_plan(size_t row_index) {
    locate_standard_marker_on_plan(beacon_marker_cache_, row_index, show_beacon_markers_);
}

void App::locate_section_row_on_plan(size_t row_index) {
    locate_standard_marker_on_plan(
        section_marker_cache_, row_index, show_section_markers_);
}

void App::locate_section_row_in_list(size_t row_index) {
    locate_standard_marker_in_list(
        section_marker_cache_, row_index, show_sections_window_,
        focus_sections_next_, section_list_scroll_row_,
        section_list_highlight_row_);
}

void App::locate_beacon_row_in_list(size_t row_index) {
    locate_standard_marker_in_list(beacon_marker_cache_, row_index,
                                   show_beacons_window_, focus_beacons_next_,
                                   beacon_list_scroll_row_, beacon_list_highlight_row_);
}
void App::locate_other_train_stop_row_on_plan(size_t row_index) {
    sync_marker_visibility_sizes();
    if (row_index >= other_train_stop_marker_cache_.size() || !other_train_stop_marker_cache_[row_index]) return;
    const PlanOtherTrainStopMarker& marker = *other_train_stop_marker_cache_[row_index];
    if (marker.definition_row_index < other_train_path_visible_.size()) {
        other_train_path_visible_[marker.definition_row_index] = 1;
    }
    plan_marker_selection_ = PlanMarkerSelection{PlanMarkerKind::OtherTrainStop, row_index};
    focus_plan_at_model_point(marker.x, marker.y);
}

void App::locate_other_train_stop_row_in_list(size_t row_index) {
    if (row_index >= other_train_stop_marker_cache_.size() || !other_train_stop_marker_cache_[row_index]) return;
    show_other_trains_window_ = true;
    focus_other_trains_next_ = true;
    other_train_stop_list_scroll_row_ = static_cast<int>(row_index);
    other_train_stop_list_highlight_row_ = static_cast<int>(row_index);
}

void App::locate_irregularity_row_on_plan(size_t row_index) {
    locate_standard_marker_on_plan(
        irregularity_marker_cache_, row_index, show_irregularity_markers_);
}

void App::locate_irregularity_row_in_list(size_t row_index) {
    locate_standard_marker_in_list(irregularity_marker_cache_, row_index,
                                   show_irregularities_window_, focus_irregularities_next_,
                                   irregularity_list_scroll_row_, irregularity_list_highlight_row_);
}

void App::locate_map_sound_row_on_plan(size_t row_index) {
    locate_standard_marker_on_plan(map_sound_marker_cache_, row_index, show_map_sound_markers_);
}

void App::locate_map_sound_row_in_list(size_t row_index) {
    locate_standard_marker_in_list(map_sound_marker_cache_, row_index,
                                   show_map_sounds_window_, focus_map_sounds_next_,
                                   map_sound_list_scroll_row_, map_sound_list_highlight_row_);
}

void App::locate_map_sound_3d_row_on_plan(size_t row_index) {
    locate_standard_marker_on_plan(
        map_sound_3d_marker_cache_, row_index, show_map_sound_3d_markers_);
}

void App::locate_map_sound_3d_row_in_list(size_t row_index) {
    locate_standard_marker_in_list(map_sound_3d_marker_cache_, row_index,
                                   show_map_sound_3d_window_, focus_map_sound_3d_next_,
                                   map_sound_3d_list_scroll_row_, map_sound_3d_list_highlight_row_);
}

void App::locate_rolling_noise_row_on_plan(size_t row_index) {
    locate_standard_marker_on_plan(
        rolling_noise_marker_cache_, row_index, show_rolling_noise_markers_);
}

void App::locate_rolling_noise_row_in_list(size_t row_index) {
    locate_standard_marker_in_list(rolling_noise_marker_cache_, row_index,
                                   show_rolling_noises_window_, focus_rolling_noises_next_,
                                   rolling_noise_list_scroll_row_, rolling_noise_list_highlight_row_);
}

void App::locate_flange_noise_row_on_plan(size_t row_index) {
    locate_standard_marker_on_plan(
        flange_noise_marker_cache_, row_index, show_flange_noise_markers_);
}

void App::locate_flange_noise_row_in_list(size_t row_index) {
    locate_standard_marker_in_list(flange_noise_marker_cache_, row_index,
                                   show_flange_noises_window_, focus_flange_noises_next_,
                                   flange_noise_list_scroll_row_, flange_noise_list_highlight_row_);
}

void App::locate_joint_noise_row_on_plan(size_t row_index) {
    locate_standard_marker_on_plan(
        joint_noise_marker_cache_, row_index, show_joint_noise_markers_);
}

void App::locate_joint_noise_row_in_list(size_t row_index) {
    locate_standard_marker_in_list(joint_noise_marker_cache_, row_index,
                                   show_joint_noises_window_, focus_joint_noises_next_,
                                   joint_noise_list_scroll_row_, joint_noise_list_highlight_row_);
}

void App::locate_background_row_on_plan(size_t row_index) {
    locate_standard_marker_on_plan(
        background_marker_cache_, row_index, show_background_markers_);
}

void App::locate_background_row_in_list(size_t row_index) {
    locate_standard_marker_in_list(background_marker_cache_, row_index,
                                   show_backgrounds_window_, focus_backgrounds_next_,
                                   background_list_scroll_row_, background_list_highlight_row_);
}

void App::locate_adhesion_row_on_plan(size_t row_index) {
    locate_standard_marker_on_plan(adhesion_marker_cache_, row_index, show_adhesion_markers_);
}

void App::locate_adhesion_row_in_list(size_t row_index) {
    locate_standard_marker_in_list(adhesion_marker_cache_, row_index,
                                   show_adhesions_window_, focus_adhesions_next_,
                                   adhesion_list_scroll_row_, adhesion_list_highlight_row_);
}

void App::locate_cab_illuminance_row_on_plan(size_t row_index) {
    locate_standard_marker_on_plan(
        cab_illuminance_marker_cache_, row_index, show_cab_illuminance_markers_);
}

void App::locate_cab_illuminance_row_in_list(size_t row_index) {
    locate_standard_marker_in_list(cab_illuminance_marker_cache_, row_index,
                                   show_cab_illuminance_window_, focus_cab_illuminance_next_,
                                   cab_illuminance_list_scroll_row_,
                                   cab_illuminance_list_highlight_row_);
}

void App::locate_fog_row_on_plan(size_t row_index) {
    locate_standard_marker_on_plan(fog_marker_cache_, row_index, show_fog_markers_);
}

void App::locate_fog_row_in_list(size_t row_index) {
    locate_standard_marker_in_list(fog_marker_cache_, row_index,
                                   show_fogs_window_, focus_fogs_next_,
                                   fog_list_scroll_row_, fog_list_highlight_row_);
}

void App::locate_legacy_fog_row_on_plan(size_t row_index) {
    locate_standard_marker_on_plan(legacy_fog_marker_cache_, row_index, show_fog_markers_);
}

void App::locate_legacy_fog_row_in_list(size_t row_index) {
    locate_standard_marker_in_list(legacy_fog_marker_cache_, row_index,
                                   show_legacy_fogs_window_, focus_legacy_fogs_next_,
                                   legacy_fog_list_scroll_row_, legacy_fog_list_highlight_row_);
}

void App::locate_draw_distance_row_on_plan(size_t row_index) {
    locate_standard_marker_on_plan(
        draw_distance_marker_cache_, row_index, show_draw_distance_markers_);
}

void App::locate_draw_distance_row_in_list(size_t row_index) {
    locate_standard_marker_in_list(draw_distance_marker_cache_, row_index,
                                   show_draw_distances_window_, focus_draw_distances_next_,
                                   draw_distance_list_scroll_row_,
                                   draw_distance_list_highlight_row_);
}

void App::locate_speed_limit_row_on_plan(size_t row_index) {
    locate_standard_marker_on_plan(
        speed_limit_marker_cache_, row_index, show_speedlimits_);
}

void App::locate_speed_limit_row_in_list(size_t row_index) {
    locate_standard_marker_in_list(speed_limit_marker_cache_, row_index,
                                   show_speed_limits_window_, focus_speed_limits_next_,
                                   speed_limit_list_scroll_row_,
                                   speed_limit_list_highlight_row_);
}

void App::locate_scene_marker_row_in_list(
    Canvas3DSceneMarkerListKind list_kind, size_t row_index) {
    switch (list_kind) {
    case Canvas3DSceneMarkerListKind::Section:
        locate_section_row_in_list(row_index);
        return;
    case Canvas3DSceneMarkerListKind::Beacon:
        locate_beacon_row_in_list(row_index);
        return;
    case Canvas3DSceneMarkerListKind::Irregularity:
        locate_irregularity_row_in_list(row_index);
        return;
    case Canvas3DSceneMarkerListKind::MapSound:
        locate_map_sound_row_in_list(row_index);
        return;
    case Canvas3DSceneMarkerListKind::MapSound3D:
        locate_map_sound_3d_row_in_list(row_index);
        return;
    case Canvas3DSceneMarkerListKind::RollingNoise:
        locate_rolling_noise_row_in_list(row_index);
        return;
    case Canvas3DSceneMarkerListKind::FlangeNoise:
        locate_flange_noise_row_in_list(row_index);
        return;
    case Canvas3DSceneMarkerListKind::JointNoise:
        locate_joint_noise_row_in_list(row_index);
        return;
    case Canvas3DSceneMarkerListKind::Background:
        locate_background_row_in_list(row_index);
        return;
    case Canvas3DSceneMarkerListKind::Adhesion:
        locate_adhesion_row_in_list(row_index);
        return;
    case Canvas3DSceneMarkerListKind::CabIlluminance:
        locate_cab_illuminance_row_in_list(row_index);
        return;
    case Canvas3DSceneMarkerListKind::Fog:
        locate_fog_row_in_list(row_index);
        return;
    case Canvas3DSceneMarkerListKind::LegacyFog:
        locate_legacy_fog_row_in_list(row_index);
        return;
    case Canvas3DSceneMarkerListKind::DrawDistance:
        locate_draw_distance_row_in_list(row_index);
        return;
    case Canvas3DSceneMarkerListKind::SpeedLimit:
        locate_speed_limit_row_in_list(row_index);
        return;
    case Canvas3DSceneMarkerListKind::None:
    case Canvas3DSceneMarkerListKind::Count:
        return;
    }
}

void App::locate_scene_marker_row_in_scene_preview(
    Canvas3DSceneMarkerListKind list_kind, size_t row_index) {
    if (!can_locate_scene_preview_row()) return;

    switch (list_kind) {
    case Canvas3DSceneMarkerListKind::Section:
        show_section_markers_ = true;
        break;
    case Canvas3DSceneMarkerListKind::Beacon:
        show_beacon_markers_ = true;
        break;
    case Canvas3DSceneMarkerListKind::Irregularity:
        show_irregularity_markers_ = true;
        break;
    case Canvas3DSceneMarkerListKind::MapSound:
        show_map_sound_markers_ = true;
        break;
    case Canvas3DSceneMarkerListKind::MapSound3D:
        show_map_sound_3d_markers_ = true;
        break;
    case Canvas3DSceneMarkerListKind::RollingNoise:
        show_rolling_noise_markers_ = true;
        break;
    case Canvas3DSceneMarkerListKind::FlangeNoise:
        show_flange_noise_markers_ = true;
        break;
    case Canvas3DSceneMarkerListKind::JointNoise:
        show_joint_noise_markers_ = true;
        break;
    case Canvas3DSceneMarkerListKind::Background:
        show_background_markers_ = true;
        break;
    case Canvas3DSceneMarkerListKind::Adhesion:
        show_adhesion_markers_ = true;
        break;
    case Canvas3DSceneMarkerListKind::CabIlluminance:
        show_cab_illuminance_markers_ = true;
        break;
    case Canvas3DSceneMarkerListKind::Fog:
        show_fog_markers_ = true;
        break;
    case Canvas3DSceneMarkerListKind::LegacyFog:
        show_fog_markers_ = true;
        break;
    case Canvas3DSceneMarkerListKind::DrawDistance:
        show_draw_distance_markers_ = true;
        break;
    case Canvas3DSceneMarkerListKind::SpeedLimit:
        show_speedlimits_ = true;
        break;
    case Canvas3DSceneMarkerListKind::None:
    case Canvas3DSceneMarkerListKind::Count:
        return;
    }

    sync_scene_preview_marker_visibility();
    if (!scene_preview_canvas_->jump_scene_camera_to_marker(list_kind, row_index)) return;
    show_scene_preview_window_ = true;
    focus_scene_preview_next_ = true;
}

bool App::can_locate_scene_preview_row() const {
    if (!scene_preview_started_ || !scene_preview_canvas_ || !scene_preview_canvas_->has_scene()) return false;
    return !scene_preview_canvas_->scene_stats().loading;
}
