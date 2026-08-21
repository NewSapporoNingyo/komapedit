/*
 * Copyright (c) 2026 Sapporo_ningyo
 *
 * Licensed under Apache License 2.0; see LICENSE and NOTICE.
 * The GUI uses Dear ImGui and ImPlot; see THIRD_PARTY_NOTICES.md.
 */

#pragma once

#include "imgui.h"

#include <windows.h>

#include <cstdint>

namespace touch_input {

enum class PinchAxis {
    None,
    Horizontal,
    Vertical
};

struct TouchFrame {
    int active_count = 0;

    bool tap = false;
    ImVec2 tap_pos = ImVec2(0.0f, 0.0f);

    bool long_press = false;
    ImVec2 long_press_pos = ImVec2(0.0f, 0.0f);

    bool single_drag = false;
    ImVec2 single_pos = ImVec2(0.0f, 0.0f);
    ImVec2 single_start_pos = ImVec2(0.0f, 0.0f);
    ImVec2 single_drag_delta = ImVec2(0.0f, 0.0f);

    bool pinch = false;
    ImVec2 pinch_center = ImVec2(0.0f, 0.0f);
    ImVec2 pinch_previous_center = ImVec2(0.0f, 0.0f);
    ImVec2 pinch_center_delta = ImVec2(0.0f, 0.0f);
    float pinch_scale = 1.0f;
    float pinch_x_scale = 1.0f;
    float pinch_y_scale = 1.0f;
    PinchAxis pinch_axis = PinchAxis::None;
    float pinch_rotation_delta = 0.0f;
};

bool handle_message(HWND hwnd, UINT msg, WPARAM w_param, LPARAM l_param);
void new_frame();
const TouchFrame& current_frame();
bool wants_continuous_render();

bool consume_tap_in_rect(ImVec2 min, ImVec2 max, ImVec2* pos = nullptr);
bool consume_long_press_in_rect(ImVec2 min, ImVec2 max, ImVec2* pos = nullptr);
bool open_popup_on_last_item_long_press(const char* popup_id);
void apply_touch_scroll_to_hovered_window();

void debug_reset_for_tests(double now_seconds = 0.0);
void debug_set_time_for_tests(double now_seconds);
void debug_touch_down(std::uint32_t id, ImVec2 pos);
void debug_touch_move(std::uint32_t id, ImVec2 pos);
void debug_touch_up(std::uint32_t id, ImVec2 pos);

} // namespace touch_input
