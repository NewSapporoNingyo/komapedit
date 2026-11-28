/*
 * Copyright (c) 2026 Sapporo_ningyo
 *
 * Licensed under Apache License 2.0; see LICENSE and NOTICE.
 * The GUI uses Dear ImGui and ImPlot; see THIRD_PARTY_NOTICES.md.
 */

#include "kme.h"
#include "datatable_internal.h"

#include "canvas3D.h"

#include <algorithm>
#include <cstddef>
#include <iterator>
void App::invalidate_table_cache() {
    GuiTiming::Stage edit_timing(kme::timing::GuiStage::TableInvalidate);
    table_cache_ = TableUiCache{};
    reset_structure_model_find_results();
    reset_signal_aspect_find_results();
    reset_sound_file_find_results(false);
    reset_sound_file_find_results(true);
    datatable_internal::reset_table_find_results(creator_message_find_);
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
    repeater_row_visible_.assign(repeater_marker_cache_->size(), 0);
    signal_row_visible_.assign(signal_marker_cache_.size(), 0);
    other_train_path_visible_.assign(model_.other_trains.size(), 0);
}

void App::sync_marker_visibility_sizes() {
    structure_row_visible_.resize(structure_marker_cache_.size(), 0);
    repeater_row_visible_.resize(repeater_marker_cache_->size(), 0);
    signal_row_visible_.resize(signal_marker_cache_.size(), 0);
    other_train_path_visible_.resize(model_.other_trains.size(), 0);
}

void App::locate_structure_row_on_plan(size_t row_index) {
    sync_marker_visibility_sizes();
    if (row_index >= structure_marker_cache_.size() || !structure_marker_cache_[row_index]) return;
    structure_row_visible_[row_index] = 1;
    const PlanStructureMarker& marker = *structure_marker_cache_[row_index];
    focus_plan_at_model_point(marker.x, marker.y, marker.d);
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
    const auto& rows = *repeater_marker_cache_;
    if (row_index >= rows.size() || !rows[row_index].begin_marker) return;
    repeater_row_visible_[row_index] = 1;
    const PlanRepeaterMarker& marker = *rows[row_index].begin_marker;
    focus_plan_at_model_point(marker.x, marker.y, marker.d);
}

void App::locate_repeater_row_in_list(size_t row_index) {
    sync_marker_visibility_sizes();
    if (row_index >= repeater_marker_cache_->size()) return;
    show_repeaters_window_ = true;
    focus_repeaters_next_ = true;
    repeater_list_scroll_row_ = static_cast<int>(row_index);
    repeater_list_highlight_row_ = static_cast<int>(row_index);
}

void App::locate_repeater_row_in_scene_preview(size_t row_index) {
    if (!can_locate_scene_preview_row()) return;
    if (!scene_preview_canvas_->jump_scene_camera_to_object(Canvas3DSceneObjectKind::Repeater, row_index)) return;
    show_scene_preview_window_ = true;
    focus_scene_preview_next_ = true;
}

void App::locate_signal_row_on_plan(size_t row_index) {
    sync_marker_visibility_sizes();
    if (row_index >= signal_marker_cache_.size() || !signal_marker_cache_[row_index]) return;
    signal_row_visible_[row_index] = 1;
    const PlanSignalMarker& marker = *signal_marker_cache_[row_index];
    focus_plan_at_model_point(marker.x, marker.y, marker.d);
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

struct App::StandardMarkerNavigation {
    std::vector<std::optional<PlanMarker>> App::*cache;
    bool App::*markers_visible;
    bool App::*window_visible;
    bool App::*focus;
    int App::*scroll;
    int App::*highlight;

    const PlanMarker* marker_at(const App& app, size_t row_index) const {
        const auto& rows = app.*cache;
        return row_index < rows.size() && rows[row_index] ? &*rows[row_index] : nullptr;
    }
};

const App::StandardMarkerNavigation* App::standard_marker_navigation(Canvas3DSceneMarkerListKind kind) {
    using Kind = Canvas3DSceneMarkerListKind;
    static constexpr std::pair<Kind, StandardMarkerNavigation> markers[] = {
        {Kind::Section, {&App::section_marker_cache_, &App::show_section_markers_,
            &App::show_sections_window_, &App::focus_sections_next_, &App::section_list_scroll_row_, &App::section_list_highlight_row_}},
        {Kind::Beacon, {&App::beacon_marker_cache_, &App::show_beacon_markers_,
            &App::show_beacons_window_, &App::focus_beacons_next_, &App::beacon_list_scroll_row_, &App::beacon_list_highlight_row_}},
        {Kind::Irregularity, {&App::irregularity_marker_cache_, &App::show_irregularity_markers_,
            &App::show_irregularities_window_, &App::focus_irregularities_next_, &App::irregularity_list_scroll_row_, &App::irregularity_list_highlight_row_}},
        {Kind::MapSound, {&App::map_sound_marker_cache_, &App::show_map_sound_markers_,
            &App::show_map_sounds_window_, &App::focus_map_sounds_next_, &App::map_sound_list_scroll_row_, &App::map_sound_list_highlight_row_}},
        {Kind::MapSound3D, {&App::map_sound_3d_marker_cache_, &App::show_map_sound_3d_markers_,
            &App::show_map_sound_3d_window_, &App::focus_map_sound_3d_next_, &App::map_sound_3d_list_scroll_row_, &App::map_sound_3d_list_highlight_row_}},
        {Kind::RollingNoise, {&App::rolling_noise_marker_cache_, &App::show_rolling_noise_markers_,
            &App::show_rolling_noises_window_, &App::focus_rolling_noises_next_, &App::rolling_noise_list_scroll_row_, &App::rolling_noise_list_highlight_row_}},
        {Kind::FlangeNoise, {&App::flange_noise_marker_cache_, &App::show_flange_noise_markers_,
            &App::show_flange_noises_window_, &App::focus_flange_noises_next_, &App::flange_noise_list_scroll_row_, &App::flange_noise_list_highlight_row_}},
        {Kind::JointNoise, {&App::joint_noise_marker_cache_, &App::show_joint_noise_markers_,
            &App::show_joint_noises_window_, &App::focus_joint_noises_next_, &App::joint_noise_list_scroll_row_, &App::joint_noise_list_highlight_row_}},
        {Kind::Background, {&App::background_marker_cache_, &App::show_background_markers_,
            &App::show_backgrounds_window_, &App::focus_backgrounds_next_, &App::background_list_scroll_row_, &App::background_list_highlight_row_}},
        {Kind::Adhesion, {&App::adhesion_marker_cache_, &App::show_adhesion_markers_,
            &App::show_adhesions_window_, &App::focus_adhesions_next_, &App::adhesion_list_scroll_row_, &App::adhesion_list_highlight_row_}},
        {Kind::CabIlluminance, {&App::cab_illuminance_marker_cache_, &App::show_cab_illuminance_markers_,
            &App::show_cab_illuminance_window_, &App::focus_cab_illuminance_next_, &App::cab_illuminance_list_scroll_row_, &App::cab_illuminance_list_highlight_row_}},
        {Kind::Fog, {&App::fog_marker_cache_, &App::show_fog_markers_,
            &App::show_fogs_window_, &App::focus_fogs_next_, &App::fog_list_scroll_row_, &App::fog_list_highlight_row_}},
        {Kind::LegacyFog, {&App::legacy_fog_marker_cache_, &App::show_fog_markers_,
            &App::show_legacy_fogs_window_, &App::focus_legacy_fogs_next_, &App::legacy_fog_list_scroll_row_, &App::legacy_fog_list_highlight_row_}},
        {Kind::DrawDistance, {&App::draw_distance_marker_cache_, &App::show_draw_distance_markers_,
            &App::show_draw_distances_window_, &App::focus_draw_distances_next_, &App::draw_distance_list_scroll_row_, &App::draw_distance_list_highlight_row_}},
        {Kind::SpeedLimit, {&App::speed_limit_marker_cache_, &App::show_speedlimits_,
            &App::show_speed_limits_window_, &App::focus_speed_limits_next_, &App::speed_limit_list_scroll_row_, &App::speed_limit_list_highlight_row_}},
    };
    constexpr auto limit = static_cast<size_t>(Kind::Count);
    static_assert(static_cast<size_t>(Kind::None) == 0 && std::size(markers) == limit - 1);
    static_assert([] {
        for (size_t i = 0; i < std::size(markers); ++i) {
            if (static_cast<size_t>(markers[i].first) != i + 1) return false;
        }
        return true;
    }());
    const auto index = static_cast<size_t>(kind);
    return index > 0 && index < limit ? &markers[index - 1].second : nullptr;
}

void App::locate_marker_row_on_plan(Canvas3DSceneMarkerListKind kind, size_t row_index) {
    const auto* navigation = standard_marker_navigation(kind);
    if (!navigation) return;
    const auto* marker = navigation->marker_at(*this, row_index);
    if (!marker) return;
    this->*navigation->markers_visible = true;
    focus_plan_at_model_point(marker->x, marker->y, marker->d);
}

void App::locate_marker_row_in_list(Canvas3DSceneMarkerListKind kind, size_t row_index) {
    const auto* navigation = standard_marker_navigation(kind);
    if (!navigation || !navigation->marker_at(*this, row_index)) return;
    this->*navigation->window_visible = true;
    this->*navigation->focus = true;
    this->*navigation->scroll = static_cast<int>(row_index);
    this->*navigation->highlight = static_cast<int>(row_index);
}

void App::locate_scene_marker_row_in_scene_preview(Canvas3DSceneMarkerListKind kind, size_t row_index) {
    const auto* navigation = standard_marker_navigation(kind);
    if (!navigation || !can_locate_scene_preview_row()) return;
    this->*navigation->markers_visible = true;
    sync_scene_preview_marker_visibility();
    if (!scene_preview_canvas_->jump_scene_camera_to_marker(kind, row_index)) return;
    show_scene_preview_window_ = true;
    focus_scene_preview_next_ = true;
}

void App::locate_other_train_stop_row_on_plan(size_t row_index) {
    sync_marker_visibility_sizes();
    if (row_index >= other_train_stop_marker_cache_.size() || !other_train_stop_marker_cache_[row_index]) return;
    const PlanOtherTrainStopMarker& marker = *other_train_stop_marker_cache_[row_index];
    if (marker.definition_row_index < other_train_path_visible_.size()) {
        other_train_path_visible_[marker.definition_row_index] = 1;
    }
    plan_marker_selection_ = PlanMarkerSelection{PlanMarkerKind::OtherTrainStop, row_index};
    focus_plan_at_model_point(marker.x, marker.y, marker.d);
}

void App::locate_other_train_stop_row_in_list(size_t row_index) {
    if (row_index >= other_train_stop_marker_cache_.size() || !other_train_stop_marker_cache_[row_index]) return;
    show_other_trains_window_ = true;
    focus_other_trains_next_ = true;
    other_train_stop_list_scroll_row_ = static_cast<int>(row_index);
    other_train_stop_list_highlight_row_ = static_cast<int>(row_index);
}

bool App::can_locate_scene_preview_row() const {
    if (!scene_preview_started_ || !scene_preview_canvas_ || !scene_preview_canvas_->has_scene()) return false;
    return !scene_preview_canvas_->scene_stats().loading;
}
