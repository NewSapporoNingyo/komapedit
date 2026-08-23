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

void App::add_log(std::string text) {
    const LogSeverity severity = classify_log_severity(text);
    add_log(severity, std::move(text));
}

void App::add_log(LogSeverity severity, std::string text) {
    {
        std::lock_guard<std::mutex> lock(log_mutex_);
        if (logs_.size() >= k_max_console_log_lines) {
            const LogSeverity dropped = logs_.front().severity;
            logs_.pop_front();
            if (dropped == LogSeverity::Error) {
                error_count_.fetch_sub(1, std::memory_order_relaxed);
            }
            if (dropped == LogSeverity::Warning) {
                warn_count_.fetch_sub(1, std::memory_order_relaxed);
            }
        }
        logs_.push_back({std::move(text), severity});
        if (severity == LogSeverity::Error) {
            error_count_.fetch_add(1, std::memory_order_relaxed);
        }
        if (severity == LogSeverity::Warning) {
            warn_count_.fetch_add(1, std::memory_order_relaxed);
        }
        ++log_revision_;
    }
    wake_main_window();
}

void App::refresh_diagnostics_snapshot() {
    std::lock_guard<std::mutex> lock(log_mutex_);
    if (diagnostics_snapshot_revision_ == log_revision_) return;
    std::vector<LogLine> next_snapshot;
    next_snapshot.reserve(static_cast<size_t>(
        std::max(0, error_count_.load(std::memory_order_relaxed)) +
        std::max(0, warn_count_.load(std::memory_order_relaxed))));
    for (const LogLine& line : logs_) {
        if (line.severity != LogSeverity::Info) next_snapshot.push_back(line);
    }
    diagnostics_snapshot_.swap(next_snapshot);
    diagnostics_snapshot_revision_ = log_revision_;
}

void App::set_program_status(const char* key, std::string_view elapsed_seconds) {
    program_status_key_ = key;
    program_status_elapsed_suffix_.clear();
    if (elapsed_seconds.empty()) return;

    program_status_elapsed_suffix_ = " (";
    program_status_elapsed_suffix_.append(elapsed_seconds);
    program_status_elapsed_suffix_ += "s)";
}

void App::export_csv_to_directory(const std::filesystem::path& dir) const {
    std::string base = narrow_path(dir.filename());
    if (base.empty()) base = "kobushi";

    auto write_matrix = [&](const std::filesystem::path& path, const Matrix& m, const std::string& header) {
        std::ofstream out(path, std::ios::binary);
        out << "#" << header << "\n";
        for (size_t r = 0; r < m.rows; ++r) {
            for (size_t c = 0; c < m.cols; ++c) {
                if (c) out << ",";
                out << std::fixed << std::setprecision(6) << m.at(r, c);
            }
            out << "\n";
        }
    };
    write_matrix(dir / utf8_to_wide(base + "_owntrack.csv"), model_.own,
                 "distance,x,y,z,direction,radius,gradient,interpolate_func,cant,center,gauge");
    for (const auto& t : model_.other_tracks) {
        write_matrix(dir / utf8_to_wide(base + "_" + sanitize_filename(t.key) + ".csv"), t.points,
                     "distance,x,y,z,interpolate_func,cant,center,gauge");
    }
}

void App::export_csv() {
    if (!has_model_) return;
    std::string folder = choose_folder_dialog();
    if (folder.empty()) return;
    export_csv_to_directory(std::filesystem::path(utf8_to_wide(folder)));
    add_log("CSV exported: " + folder);
}

void App::setup_initial_dockspace(ImGuiID dockspace_id) {
    if (initial_dockspace_done_) return;
    initial_dockspace_done_ = true;
    if (has_saved_layout_) return;

    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::DockBuilderRemoveNode(dockspace_id);
    ImGui::DockBuilderAddNode(dockspace_id, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(dockspace_id, viewport->Size);

    ImGuiID dock_main = dockspace_id;
    ImGuiID dock_right = ImGui::DockBuilderSplitNode(dock_main, ImGuiDir_Right, 0.23f, nullptr, &dock_main);
    ImGuiID dock_console = ImGui::DockBuilderSplitNode(dock_right, ImGuiDir_Down, 0.32f, nullptr, &dock_right);
    dock_main_id_ = dock_main;
    dock_right_id_ = dock_right;
    ImGui::DockBuilderDockWindow("OtherTracks", dock_right);
    ImGui::DockBuilderDockWindow("StationList", dock_right);
    ImGui::DockBuilderDockWindow("Structures", dock_right);
    ImGui::DockBuilderDockWindow("StructuresPutBetween", dock_right);
    ImGui::DockBuilderDockWindow("StructureModels", dock_right);
    ImGui::DockBuilderDockWindow("OtherTrains", dock_right);
    ImGui::DockBuilderDockWindow("SoundList", dock_right);
    ImGui::DockBuilderDockWindow("Sound3DList", dock_right);
    ImGui::DockBuilderDockWindow("Repeaters", dock_right);
    ImGui::DockBuilderDockWindow("SignalAspects", dock_right);
    ImGui::DockBuilderDockWindow("Signals", dock_right);
    ImGui::DockBuilderDockWindow("Sections", dock_right);
    ImGui::DockBuilderDockWindow("Variables", dock_right);
    ImGui::DockBuilderDockWindow("Beacons", dock_right);
    ImGui::DockBuilderDockWindow("Irregularities", dock_right);
    ImGui::DockBuilderDockWindow("MapSounds", dock_right);
    ImGui::DockBuilderDockWindow("MapSound3D", dock_right);
    ImGui::DockBuilderDockWindow("RollingNoises", dock_right);
    ImGui::DockBuilderDockWindow("FlangeNoises", dock_right);
    ImGui::DockBuilderDockWindow("JointNoises", dock_right);
    ImGui::DockBuilderDockWindow("Backgrounds", dock_right);
    ImGui::DockBuilderDockWindow("Adhesions", dock_right);
    ImGui::DockBuilderDockWindow("CabIlluminance", dock_right);
    ImGui::DockBuilderDockWindow("Fogs", dock_right);
    ImGui::DockBuilderDockWindow("LegacyFogs", dock_right);
    ImGui::DockBuilderDockWindow("DrawDistances", dock_right);
    ImGui::DockBuilderDockWindow("Console", dock_console);
    ImGui::DockBuilderDockWindow("FileStructureDiagram", dock_main);
    ImGui::DockBuilderDockWindow("TextPreview", dock_main);
    ImGui::DockBuilderDockWindow("ModelPreview3D", dock_main);
    ImGui::DockBuilderDockWindow("ScenePreview3D", dock_main);
    ImGui::DockBuilderDockWindow("Plots", dock_main);
    if (ImGuiDockNode* main_node = ImGui::DockBuilderGetNode(dock_main)) {
        main_node->SelectedTabId = ImHashStr("Plots");
    }
    focus_plots_next_ = true;
    ImGui::DockBuilderFinish(dockspace_id);
}

WindowVisibilitySettings App::current_window_visibility() const {
    WindowVisibilitySettings visibility;
    visibility.show_othertracks_window = show_othertracks_window_;
    visibility.show_station_list_window = show_station_list_window_;
    visibility.show_structures_window = show_structures_window_;
    visibility.show_structures_between_window = show_structures_between_window_;
    visibility.show_structure_models_window = show_structure_models_window_;
    visibility.show_other_trains_window = show_other_trains_window_;
    visibility.show_sound_list_window = show_sound_list_window_;
    visibility.show_sound_3d_list_window = show_sound_3d_list_window_;
    visibility.show_repeaters_window = show_repeaters_window_;
    visibility.show_signal_aspects_window = show_signal_aspects_window_;
    visibility.show_signals_window = show_signals_window_;
    visibility.show_sections_window = show_sections_window_;
    visibility.show_variables_window = show_variables_window_;
    visibility.show_beacons_window = show_beacons_window_;
    visibility.show_irregularities_window = show_irregularities_window_;
    visibility.show_map_sounds_window = show_map_sounds_window_;
    visibility.show_map_sound_3d_window = show_map_sound_3d_window_;
    visibility.show_rolling_noises_window = show_rolling_noises_window_;
    visibility.show_flange_noises_window = show_flange_noises_window_;
    visibility.show_joint_noises_window = show_joint_noises_window_;
    visibility.show_backgrounds_window = show_backgrounds_window_;
    visibility.show_adhesions_window = show_adhesions_window_;
    visibility.show_cab_illuminance_window = show_cab_illuminance_window_;
    visibility.show_fogs_window = show_fogs_window_;
    visibility.show_legacy_fogs_window = show_legacy_fogs_window_;
    visibility.show_draw_distances_window = show_draw_distances_window_;
    visibility.show_speed_limits_window = show_speed_limits_window_;
    visibility.show_file_structure_window = show_file_structure_window_;
    visibility.show_console_window = show_console_window_;
    visibility.show_plots_window = show_plots_window_;
    visibility.show_model_preview_window = show_model_preview_window_;
    visibility.show_scene_preview_window = show_scene_preview_window_;
    return visibility;
}

void App::apply_window_visibility_settings(const WindowVisibilitySettings& visibility) {
    show_othertracks_window_ = visibility.show_othertracks_window;
    show_station_list_window_ = visibility.show_station_list_window;
    show_structures_window_ = visibility.show_structures_window;
    show_structures_between_window_ = visibility.show_structures_between_window;
    show_structure_models_window_ = visibility.show_structure_models_window;
    show_other_trains_window_ = visibility.show_other_trains_window;
    show_sound_list_window_ = visibility.show_sound_list_window;
    show_sound_3d_list_window_ = visibility.show_sound_3d_list_window;
    show_repeaters_window_ = visibility.show_repeaters_window;
    show_signal_aspects_window_ = visibility.show_signal_aspects_window;
    show_signals_window_ = visibility.show_signals_window;
    show_sections_window_ = visibility.show_sections_window;
    show_variables_window_ = visibility.show_variables_window;
    show_beacons_window_ = visibility.show_beacons_window;
    show_irregularities_window_ = visibility.show_irregularities_window;
    show_map_sounds_window_ = visibility.show_map_sounds_window;
    show_map_sound_3d_window_ = visibility.show_map_sound_3d_window;
    show_rolling_noises_window_ = visibility.show_rolling_noises_window;
    show_flange_noises_window_ = visibility.show_flange_noises_window;
    show_joint_noises_window_ = visibility.show_joint_noises_window;
    show_backgrounds_window_ = visibility.show_backgrounds_window;
    show_adhesions_window_ = visibility.show_adhesions_window;
    show_cab_illuminance_window_ = visibility.show_cab_illuminance_window;
    show_fogs_window_ = visibility.show_fogs_window;
    show_legacy_fogs_window_ = visibility.show_legacy_fogs_window;
    show_draw_distances_window_ = visibility.show_draw_distances_window;
    show_speed_limits_window_ = visibility.show_speed_limits_window;
    show_file_structure_window_ = visibility.show_file_structure_window;
    show_console_window_ = visibility.show_console_window;
    show_plots_window_ = visibility.show_plots_window;
    show_model_preview_window_ = visibility.show_model_preview_window;
    show_scene_preview_window_ = visibility.show_scene_preview_window;
}

View2DSettings App::current_view_2d_settings() const {
    View2DSettings view;
    view.show_stations = show_stations_;
    view.show_station_names = show_station_names_;
    view.show_station_mileage = show_station_mileage_;
    view.show_gradient_pos = show_gradient_pos_;
    view.show_gradient_values = show_gradient_values_;
    view.show_curve_values = show_curve_values_;
    view.show_profile_other = show_profile_other_;
    view.show_speedlimits = show_speedlimits_;
    view.show_section_markers = show_section_markers_;
    view.show_irregularity_markers = show_irregularity_markers_;
    view.show_beacon_markers = show_beacon_markers_;
    view.show_pretrain_markers = show_pretrain_markers_;
    view.show_map_sound_markers = show_map_sound_markers_;
    view.show_map_sound_3d_markers = show_map_sound_3d_markers_;
    view.show_rolling_noise_markers = show_rolling_noise_markers_;
    view.show_flange_noise_markers = show_flange_noise_markers_;
    view.show_joint_noise_markers = show_joint_noise_markers_;
    view.show_background_markers = show_background_markers_;
    view.show_adhesion_markers = show_adhesion_markers_;
    view.show_cab_illuminance_markers = show_cab_illuminance_markers_;
    view.show_fog_markers = show_fog_markers_;
    view.show_draw_distance_markers = show_draw_distance_markers_;
    view.show_profile_graph = show_profile_graph_;
    view.show_radius_graph = show_radius_graph_;
    view.show_background_image = bg_show_;
    view.mode = mode_ == Mode::Measure ? 1 : 0;
    view.grid_mode = grid_mode_ == GridMode::Movable ? 1 : (grid_mode_ == GridMode::None ? 2 : 0);
    return view;
}

void App::apply_view_2d_settings(const View2DSettings& settings) {
    show_stations_ = settings.show_stations;
    show_station_names_ = settings.show_station_names;
    show_station_mileage_ = settings.show_station_mileage;
    show_gradient_pos_ = settings.show_gradient_pos;
    show_gradient_values_ = settings.show_gradient_values;
    show_curve_values_ = settings.show_curve_values;
    show_profile_other_ = settings.show_profile_other;
    show_speedlimits_ = settings.show_speedlimits;
    show_section_markers_ = settings.show_section_markers;
    show_irregularity_markers_ = settings.show_irregularity_markers;
    show_beacon_markers_ = settings.show_beacon_markers;
    show_pretrain_markers_ = settings.show_pretrain_markers;
    show_map_sound_markers_ = settings.show_map_sound_markers;
    show_map_sound_3d_markers_ = settings.show_map_sound_3d_markers;
    show_rolling_noise_markers_ = settings.show_rolling_noise_markers;
    show_flange_noise_markers_ = settings.show_flange_noise_markers;
    show_joint_noise_markers_ = settings.show_joint_noise_markers;
    show_background_markers_ = settings.show_background_markers;
    show_adhesion_markers_ = settings.show_adhesion_markers;
    show_cab_illuminance_markers_ = settings.show_cab_illuminance_markers;
    show_fog_markers_ = settings.show_fog_markers;
    show_draw_distance_markers_ = settings.show_draw_distance_markers;
    show_profile_graph_ = settings.show_profile_graph;
    show_radius_graph_ = settings.show_radius_graph;
    bg_show_ = settings.show_background_image;
    mode_ = normalize_view_2d_mode(settings.mode) == 1 ? Mode::Measure : Mode::Pan;
    switch (normalize_grid_mode(settings.grid_mode)) {
        case 1:
            grid_mode_ = GridMode::Movable;
            break;
        case 2:
            grid_mode_ = GridMode::None;
            break;
        default:
            grid_mode_ = GridMode::Fixed;
            break;
    }
}

View3DSettings App::current_view_3d_settings() const {
    View3DSettings view;
    view.show_scene_owntrack_markers = show_scene_owntrack_markers_;
    view.show_scene_current_position_on_plan = show_scene_current_position_on_plan_;
    view.scene_fog_enabled = scene_fog_enabled_;
    view.scene_map_draw_distance_enabled = scene_map_draw_distance_enabled_;
    view.scene_auto_load_on_map_open = scene_auto_load_on_map_open_;
    view.scene_draw_distance_m = scene_draw_distance_m_;
    view.scene_edit_component_size_percent = scene_edit_component_size_percent_;
    view.scene_camera_speed_percent = scene_camera_speed_percent_;
    view.scene_performance_warning_enabled = scene_performance_warning_enabled_;
    view.scene_instance_warning_threshold = scene_instance_warning_threshold_;
    view.scene_instance_critical_warning_threshold =
        scene_instance_critical_warning_threshold_;
    return view;
}

void App::apply_view_3d_settings(const View3DSettings& settings) {
    show_scene_owntrack_markers_ = settings.show_scene_owntrack_markers;
    show_scene_current_position_on_plan_ = settings.show_scene_current_position_on_plan;
    scene_fog_enabled_ = settings.scene_fog_enabled;
    scene_map_draw_distance_enabled_ = settings.scene_map_draw_distance_enabled;
    scene_auto_load_on_map_open_ = settings.scene_auto_load_on_map_open;
    scene_draw_distance_m_ = clamp_scene_draw_distance(settings.scene_draw_distance_m);
    scene_edit_component_size_percent_ =
        clamp_scene_edit_component_size_percent(settings.scene_edit_component_size_percent);
    scene_camera_speed_percent_ = std::clamp(settings.scene_camera_speed_percent,
                                              k_min_scene_camera_speed_percent,
                                              k_max_scene_camera_speed_percent);
    scene_performance_warning_enabled_ = settings.scene_performance_warning_enabled;
    scene_instance_warning_threshold_ = settings.scene_instance_warning_threshold;
    scene_instance_critical_warning_threshold_ =
        settings.scene_instance_critical_warning_threshold;
    normalize_scene_instance_warning_thresholds(
        scene_instance_warning_threshold_, scene_instance_critical_warning_threshold_);
    apply_scene_draw_distance_to_canvas(scene_draw_distance_m_);
    apply_scene_edit_component_size_to_canvas(scene_edit_component_size_percent_);
    apply_scene_fog_effect_to_canvas(scene_fog_enabled_);
    apply_scene_map_draw_distance_to_canvas(scene_map_draw_distance_enabled_);
    apply_scene_camera_speed_to_canvas(scene_camera_speed_percent_);
    apply_scene_performance_warning_to_canvas(
        scene_performance_warning_enabled_,
        scene_instance_warning_threshold_,
        scene_instance_critical_warning_threshold_);
}

void App::apply_scene_draw_distance_to_canvas(int distance_m) {
    if (scene_preview_canvas_) {
        scene_preview_canvas_->set_scene_window(k_scene_window_back_distance_m,
                                                static_cast<double>(clamp_scene_draw_distance(distance_m)));
    }
}

void App::apply_scene_edit_component_size_to_canvas(int size_percent) {
    if (scene_preview_canvas_) {
        scene_preview_canvas_->set_scene_edit_component_scale(
            static_cast<float>(clamp_scene_edit_component_size_percent(size_percent)) / 100.0f);
    }
}

void App::apply_scene_fog_effect_to_canvas(bool enabled) {
    if (scene_preview_canvas_) scene_preview_canvas_->set_scene_fog_enabled(enabled);
}

void App::apply_scene_map_draw_distance_to_canvas(bool enabled) {
    if (scene_preview_canvas_) scene_preview_canvas_->set_scene_map_draw_distance_enabled(enabled);
}

void App::apply_scene_camera_speed_to_canvas(int percent) {
    if (scene_preview_canvas_) scene_preview_canvas_->set_scene_camera_speed_percent(percent);
}

void App::apply_scene_performance_warning_to_canvas(bool enabled,
                                                    int warning_threshold,
                                                    int critical_warning_threshold) {
    normalize_scene_instance_warning_thresholds(warning_threshold, critical_warning_threshold);
    if (scene_preview_canvas_) {
        scene_preview_canvas_->set_scene_performance_warning(
            enabled,
            static_cast<size_t>(warning_threshold),
            static_cast<size_t>(critical_warning_threshold));
    }
}

void App::sync_scene_settings_dialog_state_from_current() {
    pending_scene_draw_distance_m_ = scene_draw_distance_m_;
    scene_draw_distance_before_dialog_m_ = scene_draw_distance_m_;
    pending_scene_edit_component_size_percent_ = scene_edit_component_size_percent_;
    scene_edit_component_size_before_dialog_percent_ = scene_edit_component_size_percent_;
    pending_scene_camera_speed_percent_ = scene_camera_speed_percent_;
    scene_camera_speed_percent_before_dialog_ = scene_camera_speed_percent_;
    pending_scene_fog_enabled_ = scene_fog_enabled_;
    scene_fog_enabled_before_dialog_ = scene_fog_enabled_;
    pending_scene_map_draw_distance_enabled_ = scene_map_draw_distance_enabled_;
    scene_map_draw_distance_enabled_before_dialog_ = scene_map_draw_distance_enabled_;
    pending_scene_auto_load_on_map_open_ = scene_auto_load_on_map_open_;
    scene_auto_load_on_map_open_before_dialog_ = scene_auto_load_on_map_open_;
    pending_scene_performance_warning_enabled_ = scene_performance_warning_enabled_;
    scene_performance_warning_enabled_before_dialog_ = scene_performance_warning_enabled_;
    pending_scene_instance_warning_threshold_ = scene_instance_warning_threshold_;
    scene_instance_warning_threshold_before_dialog_ = scene_instance_warning_threshold_;
    pending_scene_instance_critical_warning_threshold_ =
        scene_instance_critical_warning_threshold_;
    scene_instance_critical_warning_threshold_before_dialog_ =
        scene_instance_critical_warning_threshold_;
}

bool App::scene_settings_preview_differs_from_dialog_baseline() const {
    return pending_scene_draw_distance_m_ != scene_draw_distance_before_dialog_m_ ||
        pending_scene_edit_component_size_percent_ !=
            scene_edit_component_size_before_dialog_percent_ ||
        pending_scene_camera_speed_percent_ != scene_camera_speed_percent_before_dialog_ ||
        pending_scene_fog_enabled_ != scene_fog_enabled_before_dialog_ ||
        pending_scene_map_draw_distance_enabled_ !=
            scene_map_draw_distance_enabled_before_dialog_ ||
        pending_scene_auto_load_on_map_open_ !=
            scene_auto_load_on_map_open_before_dialog_ ||
        pending_scene_performance_warning_enabled_ !=
            scene_performance_warning_enabled_before_dialog_ ||
        pending_scene_instance_warning_threshold_ !=
            scene_instance_warning_threshold_before_dialog_ ||
        pending_scene_instance_critical_warning_threshold_ !=
            scene_instance_critical_warning_threshold_before_dialog_;
}

void App::restore_scene_settings_preview() {
    pending_scene_draw_distance_m_ = scene_draw_distance_before_dialog_m_;
    pending_scene_edit_component_size_percent_ =
        scene_edit_component_size_before_dialog_percent_;
    pending_scene_camera_speed_percent_ = scene_camera_speed_percent_before_dialog_;
    pending_scene_fog_enabled_ = scene_fog_enabled_before_dialog_;
    pending_scene_map_draw_distance_enabled_ =
        scene_map_draw_distance_enabled_before_dialog_;
    pending_scene_auto_load_on_map_open_ = scene_auto_load_on_map_open_before_dialog_;
    pending_scene_performance_warning_enabled_ =
        scene_performance_warning_enabled_before_dialog_;
    pending_scene_instance_warning_threshold_ =
        scene_instance_warning_threshold_before_dialog_;
    pending_scene_instance_critical_warning_threshold_ =
        scene_instance_critical_warning_threshold_before_dialog_;
    apply_scene_draw_distance_to_canvas(scene_draw_distance_m_);
    apply_scene_edit_component_size_to_canvas(scene_edit_component_size_percent_);
    apply_scene_camera_speed_to_canvas(scene_camera_speed_percent_);
    apply_scene_fog_effect_to_canvas(scene_fog_enabled_);
    apply_scene_map_draw_distance_to_canvas(scene_map_draw_distance_enabled_);
    apply_scene_performance_warning_to_canvas(
        scene_performance_warning_enabled_,
        scene_instance_warning_threshold_,
        scene_instance_critical_warning_threshold_);
}

void App::save_runtime_settings_if_changed() {
    bool changed = false;
    WindowVisibilitySettings visibility = current_window_visibility();
    if (visibility != last_saved_window_visibility_) {
        settings_.window_visibility = visibility;
        last_saved_window_visibility_ = visibility;
        changed = true;
    }
    View2DSettings view_2d = current_view_2d_settings();
    if (view_2d != last_saved_view_2d_settings_) {
        settings_.view_2d = view_2d;
        last_saved_view_2d_settings_ = view_2d;
        changed = true;
    }
    View3DSettings view_3d = current_view_3d_settings();
    if (view_3d != last_saved_view_3d_settings_) {
        settings_.view_3d = view_3d;
        last_saved_view_3d_settings_ = view_3d;
        changed = true;
    }
    if (changed) save_user_settings(settings_);
}

void App::render_menu() {
    if (!ImGui::BeginMainMenuBar()) return;
    const bool operation_pending = edit_ui_operation_pending();
    if (ImGui::BeginMenu(tr("menu.file").c_str())) {
        if (ImGui::MenuItem(tr("menu.open").c_str(), "Ctrl+O", false,
                            !operation_pending)) {
            std::string p = open_map_dialog();
            if (!p.empty()) begin_load(p, false, true);
        }
        if (ImGui::BeginMenu(tr("menu.recent_maps").c_str())) {
            if (recent_maps_.empty()) {
                ImGui::MenuItem(tr("menu.none").c_str(), nullptr, false, false);
            } else {
                for (size_t i = 0; i < recent_maps_.size(); ++i) {
                    const RecentMapEntry& entry = recent_maps_[i];
                    std::string label = display_name_from_path(entry.path) + "###recent_map_" + std::to_string(i);
                    if (ImGui::MenuItem(label.c_str(), nullptr, false,
                                        !operation_pending && !load_state_.running)) {
                        begin_load(entry.path, false, true, entry.background);
                    }
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", entry.path.c_str());
                }
            }
            ImGui::Separator();
            if (ImGui::MenuItem(tr("menu.clear_recent_maps").c_str())) {
                recent_maps_.clear();
                save_history();
            }
            ImGui::EndMenu();
        }
        if (ImGui::MenuItem(tr("menu.reload").c_str(), "F5", false,
                            !operation_pending && !load_state_.running &&
                            ((has_model_ && !file_path_.empty()) ||
                                          (model_preview_canvas_ && model_preview_canvas_->has_model())))) {
            reload_current_map_and_model_preview();
        }
        if (ImGui::MenuItem(tr("menu.export_csv").c_str(), nullptr, false, has_model_)) export_csv();
        if (ImGui::MenuItem(tr("menu.exit").c_str(), nullptr, false,
                            !operation_pending)) {
            request_exit();
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu(tr("menu.options").c_str())) {
        if (ImGui::MenuItem(tr("menu.ui_settings").c_str())) {
            pending_font_size_ = font_size_;
            font_size_before_dialog_ = font_size_;
            pending_ui_component_size_ = ui_component_size_;
            ui_component_size_before_dialog_ = ui_component_size_;
            pending_theme_color_ = theme_color_;
            theme_color_before_dialog_ = theme_color_;
            popups_.ui_settings = true;
        }
        if (ImGui::BeginMenu(tr("menu.canvas_2d_settings").c_str())) {
            if (ImGui::MenuItem(tr("menu.canvas_element_sizes").c_str())) {
                pending_marker_size_percent_ = marker_size_percent_;
                marker_size_percent_before_dialog_ = marker_size_percent_;
                pending_canvas_line_widths_ = canvas_line_widths_;
                canvas_line_widths_before_dialog_ = canvas_line_widths_;
                popups_.canvas_element_sizes = true;
            }
            ImGui::Separator();
            if (ImGui::MenuItem(tr("menu.plotlimit").c_str(), nullptr, false, has_model_)) {
                plot_min_ = dmin_;
                plot_max_ = dmax_;
                popups_.range = true;
            }
            if (ImGui::MenuItem(tr("menu.controlpoints").c_str(), nullptr, false, has_model_)) {
                cp_start_ = model_.cp_arb[0];
                cp_end_ = model_.cp_arb[1];
                cp_interval_ = model_.cp_arb[2];
                popups_.control_points = true;
            }
            ImGui::EndMenu();
        }
        if (ImGui::MenuItem(tr("menu.canvas_3d_settings").c_str())) {
            sync_scene_settings_dialog_state_from_current();
            popups_.canvas_3d_settings = true;
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu(tr("menu.map_info").c_str())) {
        struct MapInfoMenuEntry {
            const char* label_key;
            bool App::*window_visible;
        };
        static constexpr std::array<MapInfoMenuEntry, 34> k_map_info_menu_entries = {{
            {"aux.station", nullptr},
            {"menu.map_info.station", &App::show_station_list_window_},
            {"aux.scenery", nullptr},
            {"menu.map_info.structures", &App::show_structures_window_},
            {"menu.map_info.structures_put_between", &App::show_structures_between_window_},
            {"menu.map_info.structure_models", &App::show_structure_models_window_},
            {"menu.map_info.repeaters", &App::show_repeaters_window_},
            {"menu.map_info.other_trains", &App::show_other_trains_window_},
            {"aux.track_geometry", nullptr},
            {"menu.map_info.othertracks", &App::show_othertracks_window_},
            {"menu.map_info.irregularities", &App::show_irregularities_window_},
            {"menu.map_info.adhesions", &App::show_adhesions_window_},
            {"aux.signal", nullptr},
            {"menu.map_info.signal_aspects", &App::show_signal_aspects_window_},
            {"menu.map_info.signals", &App::show_signals_window_},
            {"menu.map_info.sections", &App::show_sections_window_},
            {"menu.map_info.beacons", &App::show_beacons_window_},
            {"menu.map_info.speed_limits", &App::show_speed_limits_window_},
            {"aux.sound", nullptr},
            {"menu.map_info.sound_files", &App::show_sound_list_window_},
            {"menu.map_info.sound_3d_files", &App::show_sound_3d_list_window_},
            {"menu.map_info.map_sounds", &App::show_map_sounds_window_},
            {"menu.map_info.map_sound_3d", &App::show_map_sound_3d_window_},
            {"menu.map_info.rolling_noises", &App::show_rolling_noises_window_},
            {"menu.map_info.flange_noises", &App::show_flange_noises_window_},
            {"menu.map_info.joint_noises", &App::show_joint_noises_window_},
            {"aux.effects", nullptr},
            {"menu.map_info.backgrounds", &App::show_backgrounds_window_},
            {"menu.map_info.cab_illuminance", &App::show_cab_illuminance_window_},
            {"menu.map_info.fogs", &App::show_fogs_window_},
            {"menu.map_info.legacy_fogs", &App::show_legacy_fogs_window_},
            {"menu.map_info.draw_distances", &App::show_draw_distances_window_},
            {"aux.other", nullptr},
            {"menu.map_info.variables", &App::show_variables_window_},
        }};
        bool has_category = false;
        for (const MapInfoMenuEntry& entry : k_map_info_menu_entries) {
            if (!entry.window_visible) {
                if (has_category) ImGui::Separator();
                ImGui::MenuItem(tr(entry.label_key).c_str(), nullptr, false, false);
                has_category = true;
                continue;
            }
            bool& window_visible = this->*entry.window_visible;
            ImGui::MenuItem(tr(entry.label_key).c_str(), nullptr, &window_visible);
        }
        ImGui::EndMenu();
    }
    auto render_aux_info_menu_items = [&]() {
        ImGui::MenuItem(tr("aux.station").c_str(), nullptr, false, false);
        ImGui::MenuItem(tr("chk.station_pos").c_str(), nullptr, &show_stations_);
        ImGui::MenuItem(tr("chk.station_name").c_str(), nullptr, &show_station_names_);
        ImGui::MenuItem(tr("chk.station_mileage").c_str(), nullptr, &show_station_mileage_);
        ImGui::Separator();
        ImGui::MenuItem(tr("aux.track_geometry").c_str(), nullptr, false, false);
        ImGui::MenuItem(tr("chk.gradient_pos").c_str(), nullptr, &show_gradient_pos_);
        ImGui::MenuItem(tr("chk.gradient_val").c_str(), nullptr, &show_gradient_values_);
        ImGui::MenuItem(tr("chk.curve_val").c_str(), nullptr, &show_curve_values_);
        ImGui::MenuItem(tr("chk.irregularity_markers").c_str(), nullptr, &show_irregularity_markers_);
        ImGui::MenuItem(tr("chk.adhesion_markers").c_str(), nullptr, &show_adhesion_markers_);
        ImGui::Separator();
        ImGui::MenuItem(tr("aux.signal").c_str(), nullptr, false, false);
        ImGui::MenuItem(tr("chk.speedlimit").c_str(), nullptr, &show_speedlimits_);
        ImGui::MenuItem(tr("chk.section_markers").c_str(), nullptr,
                        &show_section_markers_);
        ImGui::MenuItem(tr("chk.beacon_markers").c_str(), nullptr, &show_beacon_markers_);
        ImGui::MenuItem(tr("chk.pretrain_markers").c_str(), nullptr, &show_pretrain_markers_);
        ImGui::Separator();
        ImGui::MenuItem(tr("aux.sound").c_str(), nullptr, false, false);
        ImGui::MenuItem(tr("chk.map_sound_markers").c_str(), nullptr, &show_map_sound_markers_);
        ImGui::MenuItem(tr("chk.map_sound_3d_markers").c_str(), nullptr, &show_map_sound_3d_markers_);
        ImGui::MenuItem(tr("chk.rolling_noise_markers").c_str(), nullptr, &show_rolling_noise_markers_);
        ImGui::MenuItem(tr("chk.flange_noise_markers").c_str(), nullptr, &show_flange_noise_markers_);
        ImGui::MenuItem(tr("chk.joint_noise_markers").c_str(), nullptr, &show_joint_noise_markers_);
        ImGui::Separator();
        ImGui::MenuItem(tr("aux.effects").c_str(), nullptr, false, false);
        ImGui::MenuItem(tr("chk.background_markers").c_str(), nullptr, &show_background_markers_);
        ImGui::MenuItem(tr("chk.cab_illuminance_markers").c_str(), nullptr, &show_cab_illuminance_markers_);
        ImGui::MenuItem(tr("chk.fog_markers").c_str(), nullptr, &show_fog_markers_);
        ImGui::MenuItem(tr("chk.draw_distance_markers").c_str(), nullptr, &show_draw_distance_markers_);
        ImGui::Separator();
        ImGui::MenuItem(tr("aux.scene_3d").c_str(), nullptr, false, false);
        if (ImGui::MenuItem(tr("chk.scene_owntrack_markers").c_str(), nullptr, &show_scene_owntrack_markers_)) {
            sync_scene_preview_track_visibility();
        }
        ImGui::MenuItem(tr("chk.scene_current_position_on_plan").c_str(), nullptr,
                        &show_scene_current_position_on_plan_, scene_preview_started_);
        ImGui::Separator();
        ImGui::MenuItem(tr("aux.other").c_str(), nullptr, false, false);
        if (ImGui::MenuItem(tr("frame.file_structure_diagram").c_str(), nullptr,
                            &show_file_structure_window_) && show_file_structure_window_) {
            focus_file_structure_next_ = true;
        }
        const bool can_preview_root = has_model_ && !model_.file_structure.empty() &&
            is_supported_text_preview_file(model_.file_structure.front().absolute_path);
        ImGui::BeginDisabled(!can_preview_root);
        if (ImGui::MenuItem(tr("frame.text_preview").c_str(), nullptr, text_preview_.open)) {
            open_text_preview(model_.file_structure.front().absolute_path, true);
        }
        ImGui::EndDisabled();
        ImGui::MenuItem(tr("chk.console_window").c_str(), nullptr, &show_console_window_);
    };

    if (ImGui::BeginMenu(tr("menu.view_2d").c_str())) {
        if (ImGui::MenuItem(tr("chk.view_2d_window").c_str(), nullptr, &show_plots_window_) && show_plots_window_) {
            focus_plots_next_ = true;
        }
        ImGui::Separator();
        ImGui::MenuItem(tr("frame.chart_visibility").c_str(), nullptr, false, false);
        if (ImGui::MenuItem(tr("chk.gradient_graph").c_str(), nullptr, &show_profile_graph_) && show_profile_graph_) reset_profile_axes_next_ = true;
        if (ImGui::MenuItem(tr("chk.curve_graph").c_str(), nullptr, &show_radius_graph_) && show_radius_graph_) reset_radius_axes_next_ = true;
        ImGui::MenuItem(tr("chk.gradient_pos").c_str(), nullptr, &show_gradient_pos_);
        ImGui::MenuItem(tr("chk.gradient_val").c_str(), nullptr, &show_gradient_values_);
        ImGui::MenuItem(tr("chk.prof_othert").c_str(), nullptr, &show_profile_other_);
        ImGui::Separator();
        ImGui::MenuItem(tr("frame.bgimage").c_str(), nullptr, false, false);
        if (ImGui::MenuItem(tr("button.import_bg").c_str())) {
            std::string p = open_image_dialog();
            if (!p.empty() && load_background_image(p)) {
                bg_show_ = true;
                sync_pending_background_values();
                save_current_background_to_history();
            }
        }
        ImGui::MenuItem(tr("chk.bgimg_show").c_str(), nullptr, &bg_show_);
        if (ImGui::MenuItem(tr("button.adjust_bg").c_str())) {
            sync_pending_background_values();
            popups_.background_adjust = true;
        }
        if (ImGui::MenuItem(tr("button.align_to_station").c_str(), nullptr, false,
                            has_model_ && model_.stations.size() >= 2 && !bg_image_.path.empty())) {
            align_pick1_.reset();
            align_pick2_.reset();
            pick_slot_ = 0;
            popups_.background_align = true;
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu(tr("menu.view_3d").c_str())) {
        ImGui::MenuItem(tr("menu.structure_model_preview").c_str(), nullptr, &show_model_preview_window_);
        if (ImGui::MenuItem(tr("menu.scene_preview").c_str(), nullptr, &show_scene_preview_window_) &&
            show_scene_preview_window_) {
            focus_scene_preview_next_ = true;
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu(tr("frame.aux_info").c_str())) {
        render_aux_info_menu_items();
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu(tr("menu.lang").c_str())) {
        auto set_language = [&](Language lang) {
            if (lang_ == lang) return;
            lang_ = lang;
            settings_.language = lang_;
            settings_.window_visibility = current_window_visibility();
            last_saved_window_visibility_ = settings_.window_visibility;
            settings_.view_2d = current_view_2d_settings();
            last_saved_view_2d_settings_ = settings_.view_2d;
            settings_.view_3d = current_view_3d_settings();
            last_saved_view_3d_settings_ = settings_.view_3d;
            save_user_settings(settings_);
        };
        if (ImGui::MenuItem("简体中文", nullptr, lang_ == Language::Zh)) set_language(Language::Zh);
        if (ImGui::MenuItem("English", nullptr, lang_ == Language::En)) set_language(Language::En);
        if (ImGui::MenuItem("日本語", nullptr, lang_ == Language::Ja)) set_language(Language::Ja);
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu(tr("menu.help").c_str())) {
        if (ImGui::MenuItem(tr("menu.online_docs").c_str())) {
            ShellExecuteW(nullptr, L"open", L"https://github.com/NewSapporoNingyo/komapedit", nullptr, nullptr, SW_SHOWNORMAL);
        }
        if (ImGui::MenuItem(tr("menu.report_bugs").c_str())) {
            ShellExecuteW(nullptr, L"open", L"https://github.com/NewSapporoNingyo/komapedit/issues/new", nullptr, nullptr, SW_SHOWNORMAL);
        }
        if (ImGui::MenuItem(tr("menu.about").c_str())) popups_.about = true;
        ImGui::EndMenu();
    }
    ImGui::EndMainMenuBar();
}

void App::render_toolbar() {
    ImGuiStyle& style = ImGui::GetStyle();
    const ImVec4 toolbar_bg = main_bar_background_color();
    const bool operation_pending = edit_ui_operation_pending();

    const float button_height = ImGui::GetFrameHeight();
    const float toolbar_padding_y = button_height * 0.25f;
    const float toolbar_height = button_height + toolbar_padding_y * 2.0f;
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings;
    ImGui::PushStyleColor(ImGuiCol_WindowBg, toolbar_bg);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(style.WindowPadding.x, toolbar_padding_y));
    bool visible = ImGui::BeginViewportSideBar("##MainToolbar", ImGui::GetMainViewport(), ImGuiDir_Up, toolbar_height, flags);
    if (visible) {
        auto render_section_separator = [&style]() {
            ImGui::SameLine(0.0f, style.ItemSpacing.x);
            ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
            ImGui::SameLine(0.0f, style.ItemSpacing.x);
        };

        ImGui::BeginDisabled(operation_pending);
        if (ImGui::Button(tr("button.open").c_str())) {
            std::string p = open_map_dialog();
            if (!p.empty()) begin_load(p, false, true);
        }
        ImGui::EndDisabled();
        ImGui::SameLine();

        const bool can_reload = !operation_pending && !load_state_.running &&
                                             ((has_model_ && !file_path_.empty()) ||
                                             (model_preview_canvas_ && model_preview_canvas_->has_model()));
        ImGui::BeginDisabled(!can_reload);
        if (ImGui::Button(tr("button.reload").c_str())) reload_current_map_and_model_preview();
        ImGui::EndDisabled();

        ImGui::SameLine();
        const bool can_reload_geometry = !operation_pending && !load_state_.running &&
            has_model_ && !file_path_.empty();
        ImGui::BeginDisabled(!can_reload_geometry);
        if (ImGui::Button(tr("button.reload_geometry").c_str())) reload_current_map_geometry();
        ImGui::EndDisabled();

        render_section_separator();
        bool requested_edit_mode = edit_mode_enabled_;
        ImGui::BeginDisabled(operation_pending);
        if (ImGui::Checkbox(tr("chk.edit_mode").c_str(), &requested_edit_mode)) {
            set_edit_mode_enabled(requested_edit_mode);
        }
        ImGui::EndDisabled();

        ImGui::SameLine();
        ImGui::BeginDisabled(operation_pending || !edit_actions_available());
        if (ImGui::Button(tr("button.add_map_element").c_str())) {
            open_new_element_wizard();
        }
        ImGui::EndDisabled();

        ImGui::SameLine();
        ImGui::BeginDisabled(operation_pending || !edit_actions_available() || !has_pending_edits() ||
                             has_unapplied_editable_list_drafts());
        if (ImGui::Button(tr("button.save").c_str())) {
            request_edit_ui_operation(PendingEditUiOperation::Save);
        }
        ImGui::EndDisabled();

        ImGui::SameLine();
        ImGui::BeginDisabled(operation_pending || !edit_actions_available() ||
                             !has_unsaved_edit_state());
        if (ImGui::Button(tr("button.revert").c_str())) {
            popups_.revert_all_edits_confirm = true;
        }
        ImGui::EndDisabled();

        render_section_separator();
        render_station_jump_combo();
        ImGui::SameLine(0.0f, style.ItemSpacing.x);
        render_distance_jump_control();
    }
    ImGui::End();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
}

void App::render_status_bar() {
    constexpr float k_font_scale = 0.80f;
    constexpr float k_height_scale = 1.20f;
    const float status_font_size = ImGui::GetFontSize() * k_font_scale;
    const float status_bar_height = status_font_size * k_height_scale;
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoScrollbar |
                                   ImGuiWindowFlags_NoSavedSettings |
                                   ImGuiWindowFlags_NoNavInputs;

    ImGui::PushStyleColor(ImGuiCol_WindowBg, main_bar_background_color());
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowMinSize, ImVec2(0.0f, 0.0f));
    const bool visible = ImGui::BeginViewportSideBar(
        "##MainStatusBar", ImGui::GetMainViewport(), ImGuiDir_Down, status_bar_height, flags);
    if (visible) {
        ImGui::SetWindowFontScale(k_font_scale);

        const int error_count = error_count_.load(std::memory_order_relaxed);
        const int warning_count = warn_count_.load(std::memory_order_relaxed);

        std::array<char, 32> error_text{};
        std::array<char, 32> warning_text{};
        std::snprintf(error_text.data(), error_text.size(), "Err %d", error_count);
        std::snprintf(warning_text.data(), warning_text.size(), "Warn %d", warning_count);
        const ImVec2 error_text_size = ImGui::CalcTextSize(error_text.data());
        const ImVec2 warning_text_size = ImGui::CalcTextSize(warning_text.data());
        const ImGuiStyle& style = ImGui::GetStyle();
        const float horizontal_padding = style.ItemSpacing.x;
        const float count_spacing = style.ItemInnerSpacing.x;
        const ImVec2 count_region_size(error_text_size.x + count_spacing + warning_text_size.x +
                                           horizontal_padding * 2.0f,
                                       status_bar_height);
        const ImVec2 count_region_min = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton("##StatusDiagnosticsButton", count_region_size);
        const bool count_region_hovered = ImGui::IsItemHovered();
        const bool open_diagnostics = ImGui::IsItemClicked();

        const ImVec4 count_background = darkened_theme_color(theme_color_);
        ImDrawList* draw_list = ImGui::GetWindowDrawList();
        draw_list->AddRectFilled(count_region_min,
                                 ImVec2(count_region_min.x + count_region_size.x,
                                        count_region_min.y + count_region_size.y),
                                 ImGui::GetColorU32(count_background));
        const float text_y = count_region_min.y +
                             (status_bar_height - error_text_size.y) * 0.5f;
        const float error_text_x = count_region_min.x + horizontal_padding;
        const float warning_text_x = error_text_x + error_text_size.x + count_spacing;
        draw_list->AddText(ImVec2(error_text_x, text_y),
                           ImGui::GetColorU32(log_severity_color(LogSeverity::Error)),
                           error_text.data());
        draw_list->AddText(ImVec2(warning_text_x, text_y),
                           ImGui::GetColorU32(log_severity_color(LogSeverity::Warning)),
                           warning_text.data());
        if (count_region_hovered) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        if (open_diagnostics) ImGui::OpenPopup("##StatusDiagnosticsPopup");

        ImGui::SameLine(0.0f, horizontal_padding);
        ImGui::SetCursorPosY((status_bar_height - status_font_size) * 0.5f);
        ImGui::TextUnformatted(tr(program_status_key_).c_str());
        if (!program_status_elapsed_suffix_.empty()) {
            ImGui::SameLine(0.0f, 0.0f);
            ImGui::TextUnformatted(program_status_elapsed_suffix_.c_str());
        }
        if (edit_ui_operation_pending() &&
            std::string_view(program_status_key_) ==
                pending_edit_ui_operation_.progress_status_key) {
            pending_edit_ui_operation_.progress_rendered = true;
        }

        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        const ImVec2 popup_size(
            std::max(280.0f * dpi_scale_, std::min(720.0f * dpi_scale_, viewport->WorkSize.x * 0.60f)),
            std::max(160.0f * dpi_scale_, std::min(360.0f * dpi_scale_, viewport->WorkSize.y * 0.40f)));
        ImGui::SetNextWindowSize(popup_size, ImGuiCond_Appearing);
        if (ImGui::BeginPopup("##StatusDiagnosticsPopup")) {
            ImGui::SetWindowFontScale(1.0f);
            ImGui::TextUnformatted(tr("frame.errors_warnings").c_str());
            ImGui::Separator();
            ImGui::BeginChild("##StatusDiagnosticsList", ImVec2(0.0f, 0.0f), false,
                              ImGuiWindowFlags_HorizontalScrollbar);
            refresh_diagnostics_snapshot();
            if (diagnostics_snapshot_.empty()) {
                ImGui::TextDisabled("%s", tr("status.no_errors_warnings").c_str());
            } else {
                ImGuiListClipper clipper;
                clipper.Begin(static_cast<int>(diagnostics_snapshot_.size()));
                while (clipper.Step()) {
                    for (int index = clipper.DisplayStart;
                         index < clipper.DisplayEnd; ++index) {
                        const LogLine& line =
                            diagnostics_snapshot_[static_cast<size_t>(index)];
                        ImGui::TextColored(
                            log_severity_color(line.severity), "%s", line.text.c_str());
                    }
                }
            }
            ImGui::EndChild();
            ImGui::EndPopup();
        }
    }
    ImGui::End();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();
}

void App::render_station_jump_combo() {
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(tr("label.station_jump").c_str());
    ImGui::SameLine();

    const bool can_jump = has_model_ && !model_.stations.empty();
    std::string preview;
    const char* preview_text = "-";
    if (can_jump) {
        station_jump_index_ = std::clamp(station_jump_index_, 0, static_cast<int>(model_.stations.size()) - 1);
        preview = model_.stations[station_jump_index_].key + ", " + model_.stations[station_jump_index_].name;
        preview_text = preview.c_str();
    }

    ImGui::BeginDisabled(!can_jump);
    const ImGuiStyle& style = ImGui::GetStyle();
    const float reserved_width =
        distance_jump_control_width(tr("label.distance_jump").c_str(), tr("button.jump").c_str()) + style.ItemSpacing.x;
    const float available_width = ImGui::GetContentRegionAvail().x;
    float combo_width = std::min(360.0f, available_width);
    if (available_width > reserved_width + 120.0f) {
        combo_width = std::min(360.0f, available_width - reserved_width);
    } else {
        combo_width = std::min(240.0f, std::max(80.0f, available_width * 0.5f));
    }
    ImGui::SetNextItemWidth(std::max(1.0f, combo_width));
    if (ImGui::BeginCombo("##toolbar_station", preview_text)) {
        for (int i = 0; i < static_cast<int>(model_.stations.size()); ++i) {
            std::string label = model_.stations[i].key + ", " + model_.stations[i].name;
            const bool selected = i == station_jump_index_;
            if (ImGui::Selectable(label.c_str(), selected)) {
                station_jump_index_ = i;
                const double distance = model_.stations[i].distance;
                jump_to_distance(distance);
            }
            if (selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    ImGui::EndDisabled();
}

void App::render_distance_jump_control() {
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(tr("label.distance_jump").c_str());
    ImGui::SameLine();

    const bool has_track = has_model_ && !model_.own.empty();
    double distance = 0.0;

    ImGui::BeginDisabled(!has_track);
    ImGui::SetNextItemWidth(distance_jump_input_width());
    const bool enter_pressed = ImGui::InputText(
        "##toolbar_distance_jump", distance_jump_input_, sizeof(distance_jump_input_),
        ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CallbackCharFilter,
        distance_jump_input_filter);
    ImGui::EndDisabled();

    const bool valid_distance = parse_distance_jump_input(distance_jump_input_, distance);
    ImGui::SameLine();
    const bool can_jump = has_track && valid_distance;
    ImGui::BeginDisabled(!can_jump);
    if (ImGui::Button(tr("button.jump").c_str()) || (enter_pressed && can_jump)) {
        jump_to_distance(distance);
    }
    ImGui::EndDisabled();
}

void App::render_console() {
    if (!show_console_window_) return;
    std::string title = tr("frame.console") + "###Console";
    if (!ImGui::Begin(title.c_str(), &show_console_window_)) {
        ImGui::End();
        return;
    }
    if (ImGui::Button(tr("button.clear").c_str())) {
        std::lock_guard<std::mutex> lock(log_mutex_);
        logs_.clear();
        ++log_revision_;
        error_count_.store(0, std::memory_order_relaxed);
        warn_count_.store(0, std::memory_order_relaxed);
    }
    ImGui::SameLine();
    if (ImGui::Button(tr("button.copy").c_str())) {
        std::string console_text;
        {
            std::lock_guard<std::mutex> lock(log_mutex_);
            size_t text_size = logs_.empty() ? 0 : logs_.size() - 1;
            for (const auto& line : logs_) text_size += line.text.size();
            console_text.reserve(text_size);
            for (size_t i = 0; i < logs_.size(); ++i) {
                if (i > 0) console_text.push_back('\n');
                console_text.append(logs_[i].text);
            }
        }
        ImGui::SetClipboardText(console_text.c_str());
    }
    ImGui::SameLine();
    ImGui::TextColored(log_severity_color(LogSeverity::Error), "E %d",
                       error_count_.load(std::memory_order_relaxed));
    ImGui::SameLine();
    ImGui::TextColored(log_severity_color(LogSeverity::Warning), "W %d",
                       warn_count_.load(std::memory_order_relaxed));
    ImGui::Separator();
    ImGui::BeginChild("console_scroll", ImVec2(0, 0), true, ImGuiWindowFlags_HorizontalScrollbar);
    const bool was_at_bottom = ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 4.0f;
    const touch_input::TouchFrame& touch = touch_input::current_frame();
    ImGuiWindow* console_window = ImGui::GetCurrentWindow();
    ImRect console_rect(console_window->Pos, ImVec2(console_window->Pos.x + console_window->Size.x,
                                                   console_window->Pos.y + console_window->Size.y));
    const bool touch_vertical_scroll =
        touch.single_drag && touch.active_count == 1 && std::abs(touch.single_drag_delta.y) > 0.01f &&
        (console_rect.Contains(touch.single_start_pos) || console_rect.Contains(touch.single_pos));
    std::lock_guard<std::mutex> lock(log_mutex_);
    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(logs_.size()));
    while (clipper.Step()) {
        for (int index = clipper.DisplayStart; index < clipper.DisplayEnd; ++index) {
            const LogLine& line = logs_[static_cast<size_t>(index)];
            ImGui::TextColored(
                log_severity_color(line.severity), "%s", line.text.c_str());
        }
    }
    if (was_at_bottom && !touch_vertical_scroll) ImGui::SetScrollHereY(1.0f);
    ImGui::EndChild();
    ImGui::End();
}

void App::handle_shortcuts() {
    if (!edit_ui_operation_pending() && ImGui::IsKeyPressed(ImGuiKey_F5, false)) {
        reload_current_map_and_model_preview();
    }
    const ImGuiIO& io = ImGui::GetIO();
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S, false)) {
        request_edit_ui_operation(PendingEditUiOperation::Save);
    }
}

void App::render() {
    process_pending_edit_ui_operation();
    touch_input::new_frame();
    poll_loader();
    handle_shortcuts();
    render_menu();
    render_toolbar();
    render_status_bar();
    ImGuiID dockspace_id = ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport());
    setup_initial_dockspace(dockspace_id);
    render_othertracks_window();
    render_station_list_window();
    render_console();
    render_plots();
    render_file_structure_window();
    render_text_preview_window();
    render_model_preview_window();
    render_scene_preview_window();
    render_structures_window();
    render_structures_between_window();
    render_structure_models_window();
    render_other_trains_window();
    render_sound_list_window();
    render_sound_3d_list_window();
    render_repeaters_window();
    render_signal_aspects_window();
    render_signals_window();
    render_sections_window();
    render_variables_window();
    render_beacons_window();
    render_irregularities_window();
    render_map_sounds_window();
    render_map_sound_3d_window();
    render_rolling_noises_window();
    render_flange_noises_window();
    render_joint_noises_window();
    render_backgrounds_window();
    render_adhesions_window();
    render_cab_illuminance_window();
    render_fogs_window();
    render_legacy_fogs_window();
    render_draw_distances_window();
    render_speed_limits_window();
    process_pending_element_delete();
    process_pending_include_file_change();
    process_pending_resource_list_file_change();
    process_pending_include_file_insert();
    process_pending_other_track_rename();
    process_pending_element_inspector();
    render_element_inspector();
    render_new_element_wizard();
    render_popups();
    process_distance_resolution_retry();
    touch_input::apply_touch_scroll_to_hovered_window();
    save_runtime_settings_if_changed();
}
