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

#ifndef NDEBUG
std::ostream* g_debug_plan_benchmark_log = nullptr;
#endif

App::App(ID3D11Device* device, UserSettings settings, float dpi_scale, bool viewports_enabled, bool has_saved_layout)
    : device_(device), dpi_scale_(dpi_scale), viewports_enabled_(viewports_enabled), settings_(std::move(settings)),
      has_saved_layout_(has_saved_layout) {
    g_app = this;
    kv_set_log_callback(&App::log_callback);
    model_preview_canvas_ = std::make_unique<Canvas3D>(device_);
    model_preview_canvas_->set_background_color(model_preview_bg_color_);
    scene_preview_canvas_ = std::make_unique<Canvas3D>(device_, wake_main_window);
    scene_preview_canvas_->set_background_color(ImVec4(0.0f, 0.0f, 0.0f, 1.0f));
    lang_ = settings_.language;
    font_size_ = clamp_font_size(settings_.font_size);
    ui_component_size_ = clamp_ui_component_size(settings_.ui_component_size);
    marker_size_percent_ = clamp_marker_size_percent(settings_.marker_size_percent);
    canvas_line_widths_ = clamp_canvas_line_widths(settings_.canvas_line_widths);
    theme_color_ = clamp_theme_color(settings_.theme_color);
    settings_.language = lang_;
    settings_.font_size = font_size_;
    settings_.ui_component_size = ui_component_size_;
    settings_.marker_size_percent = marker_size_percent_;
    settings_.canvas_line_widths = canvas_line_widths_;
    settings_.theme_color = theme_color_;
    edit_mode_enabled_ = settings_.edit_mode_enabled;
    settings_.edit_mode_enabled = edit_mode_enabled_;
    pending_font_size_ = font_size_;
    font_size_before_dialog_ = font_size_;
    pending_ui_component_size_ = ui_component_size_;
    ui_component_size_before_dialog_ = ui_component_size_;
    pending_marker_size_percent_ = marker_size_percent_;
    marker_size_percent_before_dialog_ = marker_size_percent_;
    pending_canvas_line_widths_ = canvas_line_widths_;
    canvas_line_widths_before_dialog_ = canvas_line_widths_;
    pending_theme_color_ = theme_color_;
    theme_color_before_dialog_ = theme_color_;
    apply_window_visibility_settings(settings_.window_visibility);
    last_saved_window_visibility_ = current_window_visibility();
    settings_.window_visibility = last_saved_window_visibility_;
    apply_view_2d_settings(settings_.view_2d);
    last_saved_view_2d_settings_ = current_view_2d_settings();
    settings_.view_2d = last_saved_view_2d_settings_;
    apply_view_3d_settings(settings_.view_3d);
    last_saved_view_3d_settings_ = current_view_3d_settings();
    settings_.view_3d = last_saved_view_3d_settings_;
    sync_scene_settings_dialog_state_from_current();
    apply_ui_settings(font_size_, ui_component_size_, theme_color_, dpi_scale_, viewports_enabled_);
    history_path_ = default_history_path();
    recent_maps_ = load_history_entries(history_path_);
    sync_pending_background_values();
}

App::~App() {
    stop_loader();
    if (handle_) kv_free(handle_);
    bg_image_.release();
    g_app = nullptr;
}

void App::log_callback(const char* message) {
    if (g_app && message) g_app->add_log(message);
}
