/*
 * Copyright (c) 2026 Sapporo_ningyo
 *
 * Licensed under Apache License 2.0; see LICENSE and NOTICE.
 * The GUI uses Dear ImGui and ImPlot; see THIRD_PARTY_NOTICES.md.
 */

#include "kme.h"

#include <algorithm>
#include <cstddef>
void App::invalidate_table_cache() {
    table_cache_ = TableUiCache{};
    reset_structure_model_find_results();
    reset_sound_file_find_results(false);
    reset_sound_file_find_results(true);
    structure_list_scroll_row_ = -1;
    structure_list_highlight_row_ = -1;
    repeater_list_scroll_row_ = -1;
    repeater_list_highlight_row_ = -1;
    signal_list_scroll_row_ = -1;
    signal_list_highlight_row_ = -1;
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
    plan_structure_popup_row_ = -1;
    plan_repeater_popup_row_ = -1;
    plan_signal_popup_row_ = -1;
    plan_beacon_popup_row_ = -1;
    plan_irregularity_popup_row_ = -1;
    plan_map_sound_popup_row_ = -1;
    plan_map_sound_3d_popup_row_ = -1;
    plan_rolling_noise_popup_row_ = -1;
    plan_flange_noise_popup_row_ = -1;
    plan_joint_noise_popup_row_ = -1;
    plan_background_popup_row_ = -1;
    plan_adhesion_popup_row_ = -1;
    plan_cab_illuminance_popup_row_ = -1;
    plan_fog_popup_row_ = -1;
}

void App::reset_marker_visibility() {
    structure_row_visible_.assign(structure_marker_cache_.size(), 0);
    repeater_row_visible_.assign(repeater_marker_cache_.size(), 0);
    signal_row_visible_.assign(signal_marker_cache_.size(), 0);
}

void App::sync_marker_visibility_sizes() {
    structure_row_visible_.resize(structure_marker_cache_.size(), 0);
    repeater_row_visible_.resize(repeater_marker_cache_.size(), 0);
    signal_row_visible_.resize(signal_marker_cache_.size(), 0);
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
    show_structures_window_ = true;
    focus_structures_next_ = true;
    structure_list_scroll_row_ = static_cast<int>(row_index);
    structure_list_highlight_row_ = static_cast<int>(row_index);
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

void App::locate_beacon_row_on_plan(size_t row_index) {
    if (row_index >= beacon_marker_cache_.size() || !beacon_marker_cache_[row_index]) return;
    show_beacon_markers_ = true;
    const PlanBeaconMarker& marker = *beacon_marker_cache_[row_index];
    focus_plan_at_model_point(marker.x, marker.y);
}

void App::locate_beacon_row_in_list(size_t row_index) {
    if (row_index >= beacon_marker_cache_.size() || !beacon_marker_cache_[row_index]) return;
    show_beacons_window_ = true;
    focus_beacons_next_ = true;
    beacon_list_scroll_row_ = static_cast<int>(row_index);
    beacon_list_highlight_row_ = static_cast<int>(row_index);
}

void App::locate_irregularity_row_on_plan(size_t row_index) {
    if (row_index >= irregularity_marker_cache_.size() || !irregularity_marker_cache_[row_index]) return;
    show_irregularity_markers_ = true;
    const PlanIrregularityMarker& marker = *irregularity_marker_cache_[row_index];
    focus_plan_at_model_point(marker.x, marker.y);
}

void App::locate_irregularity_row_in_list(size_t row_index) {
    if (row_index >= irregularity_marker_cache_.size() || !irregularity_marker_cache_[row_index]) return;
    show_irregularities_window_ = true;
    focus_irregularities_next_ = true;
    irregularity_list_scroll_row_ = static_cast<int>(row_index);
    irregularity_list_highlight_row_ = static_cast<int>(row_index);
}

void App::locate_map_sound_row_on_plan(size_t row_index) {
    if (row_index >= map_sound_marker_cache_.size() || !map_sound_marker_cache_[row_index]) return;
    show_map_sound_markers_ = true;
    const PlanMapSoundMarker& marker = *map_sound_marker_cache_[row_index];
    focus_plan_at_model_point(marker.x, marker.y);
}

void App::locate_map_sound_row_in_list(size_t row_index) {
    if (row_index >= map_sound_marker_cache_.size() || !map_sound_marker_cache_[row_index]) return;
    show_map_sounds_window_ = true;
    focus_map_sounds_next_ = true;
    map_sound_list_scroll_row_ = static_cast<int>(row_index);
    map_sound_list_highlight_row_ = static_cast<int>(row_index);
}

void App::locate_map_sound_3d_row_on_plan(size_t row_index) {
    if (row_index >= map_sound_3d_marker_cache_.size() || !map_sound_3d_marker_cache_[row_index]) return;
    show_map_sound_3d_markers_ = true;
    const PlanMapSound3DMarker& marker = *map_sound_3d_marker_cache_[row_index];
    focus_plan_at_model_point(marker.x, marker.y);
}

void App::locate_map_sound_3d_row_in_list(size_t row_index) {
    if (row_index >= map_sound_3d_marker_cache_.size() || !map_sound_3d_marker_cache_[row_index]) return;
    show_map_sound_3d_window_ = true;
    focus_map_sound_3d_next_ = true;
    map_sound_3d_list_scroll_row_ = static_cast<int>(row_index);
    map_sound_3d_list_highlight_row_ = static_cast<int>(row_index);
}

void App::locate_rolling_noise_row_on_plan(size_t row_index) {
    if (row_index >= rolling_noise_marker_cache_.size() || !rolling_noise_marker_cache_[row_index]) return;
    show_rolling_noise_markers_ = true;
    const PlanRollingNoiseMarker& marker = *rolling_noise_marker_cache_[row_index];
    focus_plan_at_model_point(marker.x, marker.y);
}

void App::locate_rolling_noise_row_in_list(size_t row_index) {
    if (row_index >= rolling_noise_marker_cache_.size() || !rolling_noise_marker_cache_[row_index]) return;
    show_rolling_noises_window_ = true;
    focus_rolling_noises_next_ = true;
    rolling_noise_list_scroll_row_ = static_cast<int>(row_index);
    rolling_noise_list_highlight_row_ = static_cast<int>(row_index);
}

void App::locate_flange_noise_row_on_plan(size_t row_index) {
    if (row_index >= flange_noise_marker_cache_.size() || !flange_noise_marker_cache_[row_index]) return;
    show_flange_noise_markers_ = true;
    const PlanFlangeNoiseMarker& marker = *flange_noise_marker_cache_[row_index];
    focus_plan_at_model_point(marker.x, marker.y);
}

void App::locate_flange_noise_row_in_list(size_t row_index) {
    if (row_index >= flange_noise_marker_cache_.size() || !flange_noise_marker_cache_[row_index]) return;
    show_flange_noises_window_ = true;
    focus_flange_noises_next_ = true;
    flange_noise_list_scroll_row_ = static_cast<int>(row_index);
    flange_noise_list_highlight_row_ = static_cast<int>(row_index);
}

void App::locate_joint_noise_row_on_plan(size_t row_index) {
    if (row_index >= joint_noise_marker_cache_.size() || !joint_noise_marker_cache_[row_index]) return;
    show_joint_noise_markers_ = true;
    const PlanJointNoiseMarker& marker = *joint_noise_marker_cache_[row_index];
    focus_plan_at_model_point(marker.x, marker.y);
}

void App::locate_joint_noise_row_in_list(size_t row_index) {
    if (row_index >= joint_noise_marker_cache_.size() || !joint_noise_marker_cache_[row_index]) return;
    show_joint_noises_window_ = true;
    focus_joint_noises_next_ = true;
    joint_noise_list_scroll_row_ = static_cast<int>(row_index);
    joint_noise_list_highlight_row_ = static_cast<int>(row_index);
}

void App::locate_background_row_on_plan(size_t row_index) {
    if (row_index >= background_marker_cache_.size() || !background_marker_cache_[row_index]) return;
    show_background_markers_ = true;
    const PlanBackgroundMarker& marker = *background_marker_cache_[row_index];
    focus_plan_at_model_point(marker.x, marker.y);
}

void App::locate_background_row_in_list(size_t row_index) {
    if (row_index >= background_marker_cache_.size() || !background_marker_cache_[row_index]) return;
    show_backgrounds_window_ = true;
    focus_backgrounds_next_ = true;
    background_list_scroll_row_ = static_cast<int>(row_index);
    background_list_highlight_row_ = static_cast<int>(row_index);
}

void App::locate_adhesion_row_on_plan(size_t row_index) {
    if (row_index >= adhesion_marker_cache_.size() || !adhesion_marker_cache_[row_index]) return;
    show_adhesion_markers_ = true;
    const PlanAdhesionMarker& marker = *adhesion_marker_cache_[row_index];
    focus_plan_at_model_point(marker.x, marker.y);
}

void App::locate_adhesion_row_in_list(size_t row_index) {
    if (row_index >= adhesion_marker_cache_.size() || !adhesion_marker_cache_[row_index]) return;
    show_adhesions_window_ = true;
    focus_adhesions_next_ = true;
    adhesion_list_scroll_row_ = static_cast<int>(row_index);
    adhesion_list_highlight_row_ = static_cast<int>(row_index);
}

void App::locate_cab_illuminance_row_on_plan(size_t row_index) {
    if (row_index >= cab_illuminance_marker_cache_.size() || !cab_illuminance_marker_cache_[row_index]) return;
    show_cab_illuminance_markers_ = true;
    const PlanCabIlluminanceMarker& marker = *cab_illuminance_marker_cache_[row_index];
    focus_plan_at_model_point(marker.x, marker.y);
}

void App::locate_cab_illuminance_row_in_list(size_t row_index) {
    if (row_index >= cab_illuminance_marker_cache_.size() || !cab_illuminance_marker_cache_[row_index]) return;
    show_cab_illuminance_window_ = true;
    focus_cab_illuminance_next_ = true;
    cab_illuminance_list_scroll_row_ = static_cast<int>(row_index);
    cab_illuminance_list_highlight_row_ = static_cast<int>(row_index);
}

void App::locate_fog_row_on_plan(size_t row_index) {
    if (row_index >= fog_marker_cache_.size() || !fog_marker_cache_[row_index]) return;
    show_fog_markers_ = true;
    const PlanFogMarker& marker = *fog_marker_cache_[row_index];
    focus_plan_at_model_point(marker.x, marker.y);
}

void App::locate_fog_row_in_list(size_t row_index) {
    if (row_index >= fog_marker_cache_.size() || !fog_marker_cache_[row_index]) return;
    show_fogs_window_ = true;
    focus_fogs_next_ = true;
    fog_list_scroll_row_ = static_cast<int>(row_index);
    fog_list_highlight_row_ = static_cast<int>(row_index);
}
