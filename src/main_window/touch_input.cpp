/*
 * Copyright (c) 2026 Sapporo_ningyo
 *
 * Licensed under Apache License 2.0; see LICENSE and NOTICE.
 * The GUI uses Dear ImGui and ImPlot; see THIRD_PARTY_NOTICES.md.
 */

#ifndef WINVER
#define WINVER 0x0602
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0602
#endif

#include "touch_input.h"

#include "imgui_internal.h"

#include <windowsx.h>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <iterator>
#include <map>
#include <optional>

namespace touch_input {
namespace {

constexpr double k_long_press_seconds = 0.55;
constexpr double k_tap_max_seconds = 0.45;
constexpr float k_move_cancel_pixels = 12.0f;
constexpr float k_move_cancel_pixels_sq = k_move_cancel_pixels * k_move_cancel_pixels;
constexpr float k_scroll_start_pixels = 8.0f;
constexpr float k_scroll_start_pixels_sq = k_scroll_start_pixels * k_scroll_start_pixels;
constexpr float k_axis_pinch_min_pixels = 8.0f;

struct ActiveTouch {
    ImVec2 start_pos = ImVec2(0.0f, 0.0f);
    ImVec2 pos = ImVec2(0.0f, 0.0f);
    ImVec2 prev_pos = ImVec2(0.0f, 0.0f);
    double down_at = 0.0;
    float max_move_sq = 0.0f;
    bool long_press_sent = false;
};

struct PairState {
    bool valid = false;
    std::uint32_t first_id = 0;
    std::uint32_t second_id = 0;
    ImVec2 center = ImVec2(0.0f, 0.0f);
    float dx = 0.0f;
    float dy = 0.0f;
    float distance = 0.0f;
    float angle = 0.0f;
};

struct TouchManager {
    std::map<std::uint32_t, ActiveTouch> touches;
    std::uint32_t primary_id = 0;
    std::uint32_t imgui_mouse_id = 0;
    bool imgui_mouse_down = false;
    PairState pair;
    TouchFrame pending;
    TouchFrame frame;
    bool tap_consumed = false;
    bool long_press_consumed = false;
    std::optional<double> debug_time;

    double now() const {
        if (debug_time) return *debug_time;
        return static_cast<double>(::GetTickCount64()) / 1000.0;
    }

    void reset(double now_seconds = 0.0) {
        touches.clear();
        primary_id = 0;
        imgui_mouse_id = 0;
        imgui_mouse_down = false;
        pair = PairState{};
        pending = TouchFrame{};
        frame = TouchFrame{};
        tap_consumed = false;
        long_press_consumed = false;
        debug_time = now_seconds;
    }

    ActiveTouch* primary_touch() {
        if (primary_id == 0) return nullptr;
        auto it = touches.find(primary_id);
        return it == touches.end() ? nullptr : &it->second;
    }

    void choose_new_primary() {
        primary_id = touches.empty() ? 0 : touches.begin()->first;
    }

    static float distance_sq(ImVec2 a, ImVec2 b) {
        float dx = a.x - b.x;
        float dy = a.y - b.y;
        return dx * dx + dy * dy;
    }

    static float angle_delta(float next, float prev) {
        return std::atan2(std::sin(next - prev), std::cos(next - prev));
    }

    static void submit_imgui_mouse_pos(ImVec2 pos) {
        if (!ImGui::GetCurrentContext()) return;
        ImGuiIO& io = ImGui::GetIO();
        io.AddMouseSourceEvent(ImGuiMouseSource_TouchScreen);
        io.AddMousePosEvent(pos.x, pos.y);
    }

    static void submit_imgui_mouse_button(ImVec2 pos, bool down) {
        if (!ImGui::GetCurrentContext()) return;
        ImGuiIO& io = ImGui::GetIO();
        io.AddMouseSourceEvent(ImGuiMouseSource_TouchScreen);
        io.AddMousePosEvent(pos.x, pos.y);
        io.AddMouseButtonEvent(ImGuiMouseButton_Left, down);
    }

    PairState current_pair_state() const {
        PairState out;
        if (touches.size() < 2) return out;
        auto first = touches.begin();
        auto second = std::next(first);
        out.valid = true;
        out.first_id = first->first;
        out.second_id = second->first;
        ImVec2 a = first->second.pos;
        ImVec2 b = second->second.pos;
        out.center = ImVec2((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f);
        float dx = b.x - a.x;
        float dy = b.y - a.y;
        out.dx = dx;
        out.dy = dy;
        out.distance = std::sqrt(dx * dx + dy * dy);
        out.angle = std::atan2(dy, dx);
        return out;
    }

    static float axis_scale(float prev_delta, float next_delta) {
        float prev = std::abs(prev_delta);
        float next = std::abs(next_delta);
        if (prev < k_axis_pinch_min_pixels || next < k_axis_pinch_min_pixels) return 1.0f;
        return std::clamp(next / prev, 0.75f, 1.333f);
    }

    void update_pair_gesture() {
        PairState next = current_pair_state();
        if (!next.valid) {
            pair = PairState{};
            return;
        }
        if (!pair.valid || pair.first_id != next.first_id || pair.second_id != next.second_id) {
            pair = next;
            return;
        }

        ImVec2 center_delta(next.center.x - pair.center.x, next.center.y - pair.center.y);
        pending.pinch = true;
        pending.pinch_previous_center = pair.center;
        pending.pinch_center = next.center;
        pending.pinch_center_delta.x += center_delta.x;
        pending.pinch_center_delta.y += center_delta.y;
        if (pair.distance > 1.0f && next.distance > 1.0f) {
            float scale = next.distance / pair.distance;
            pending.pinch_scale *= std::clamp(scale, 0.75f, 1.333f);
        }
        pending.pinch_x_scale *= axis_scale(pair.dx, next.dx);
        pending.pinch_y_scale *= axis_scale(pair.dy, next.dy);
        pending.pinch_axis =
            std::abs(next.dx) >= std::abs(next.dy) ? PinchAxis::Horizontal : PinchAxis::Vertical;
        pending.pinch_rotation_delta += angle_delta(next.angle, pair.angle);
        pair = next;
    }

    void down(std::uint32_t id, ImVec2 pos) {
        ActiveTouch touch;
        touch.start_pos = pos;
        touch.pos = pos;
        touch.prev_pos = pos;
        touch.down_at = now();
        touches[id] = touch;
        if (primary_id == 0) primary_id = id;
        if (imgui_mouse_id == 0) {
            imgui_mouse_id = id;
            imgui_mouse_down = true;
            submit_imgui_mouse_button(pos, true);
        }
        update_pair_gesture();
    }

    void move(std::uint32_t id, ImVec2 pos) {
        auto it = touches.find(id);
        if (it == touches.end()) {
            down(id, pos);
            return;
        }
        ActiveTouch& touch = it->second;
        touch.prev_pos = touch.pos;
        touch.pos = pos;
        if (id == imgui_mouse_id) submit_imgui_mouse_pos(pos);
        touch.max_move_sq = std::max(touch.max_move_sq, distance_sq(touch.start_pos, touch.pos));

        if (touches.size() == 1 && id == primary_id && touch.max_move_sq >= k_scroll_start_pixels_sq) {
            pending.single_drag = true;
            pending.single_pos = touch.pos;
            pending.single_start_pos = touch.start_pos;
            pending.single_drag_delta.x += touch.pos.x - touch.prev_pos.x;
            pending.single_drag_delta.y += touch.pos.y - touch.prev_pos.y;
        }
        update_pair_gesture();
    }

    void up(std::uint32_t id, ImVec2 pos) {
        auto it = touches.find(id);
        if (it == touches.end()) {
            update_pair_gesture();
            return;
        }
        ActiveTouch touch = it->second;
        touch.prev_pos = touch.pos;
        touch.pos = pos;
        if (id == imgui_mouse_id) {
            if (imgui_mouse_down) submit_imgui_mouse_button(pos, false);
            imgui_mouse_id = 0;
            imgui_mouse_down = false;
        }
        touch.max_move_sq = std::max(touch.max_move_sq, distance_sq(touch.start_pos, touch.pos));
        const double age = now() - touch.down_at;
        if (!touch.long_press_sent && touch.max_move_sq < k_move_cancel_pixels_sq && age <= k_tap_max_seconds) {
            pending.tap = true;
            pending.tap_pos = touch.pos;
        }
        touches.erase(it);
        if (primary_id == id) choose_new_primary();
        update_pair_gesture();
    }

    void cancel(std::uint32_t id) {
        if (id == imgui_mouse_id) {
            auto it = touches.find(id);
            ImVec2 pos = it == touches.end() ? ImVec2(-FLT_MAX, -FLT_MAX) : it->second.pos;
            if (imgui_mouse_down) submit_imgui_mouse_button(pos, false);
            imgui_mouse_id = 0;
            imgui_mouse_down = false;
        }
        touches.erase(id);
        if (primary_id == id) choose_new_primary();
        update_pair_gesture();
    }

    void emit_due_long_press() {
        ActiveTouch* touch = primary_touch();
        if (!touch || touches.size() != 1) return;
        if (touch->long_press_sent || touch->max_move_sq >= k_move_cancel_pixels_sq) return;
        if (now() - touch->down_at < k_long_press_seconds) return;
        touch->long_press_sent = true;
        pending.long_press = true;
        pending.long_press_pos = touch->pos;
    }

    TouchFrame consume_frame() {
        emit_due_long_press();
        frame = pending;
        frame.active_count = static_cast<int>(touches.size());
        if (ActiveTouch* touch = primary_touch()) {
            frame.single_pos = touch->pos;
            frame.single_start_pos = touch->start_pos;
        }
        tap_consumed = false;
        long_press_consumed = false;
        pending = TouchFrame{};
        pending.pinch_scale = 1.0f;
        return frame;
    }
};

TouchManager g_touch;

bool point_in_rect(ImVec2 p, ImVec2 min, ImVec2 max) {
    return p.x >= min.x && p.x <= max.x && p.y >= min.y && p.y <= max.y;
}

ImVec2 pointer_pos_screen(HWND hwnd, std::uint32_t id, LPARAM fallback_l_param) {
    POINT pt = {GET_X_LPARAM(fallback_l_param), GET_Y_LPARAM(fallback_l_param)};
#if _WIN32_WINNT >= 0x0602
    POINTER_INFO info = {};
    if (::GetPointerInfo(id, &info)) {
        pt = info.ptPixelLocation;
    } else if (hwnd) {
        ::ClientToScreen(hwnd, &pt);
    }
#else
    if (hwnd) {
        ::ClientToScreen(hwnd, &pt);
    }
#endif
    return ImVec2(static_cast<float>(pt.x), static_cast<float>(pt.y));
}

bool pointer_is_touch(std::uint32_t id) {
#if _WIN32_WINNT >= 0x0602
    POINTER_INPUT_TYPE type = PT_POINTER;
    if (::GetPointerType(id, &type)) return type == PT_TOUCH;
#endif
    (void)id;
    return true;
}

} // namespace

void initialize(HWND) {
}

bool handle_message(HWND hwnd, UINT msg, WPARAM w_param, LPARAM l_param) {
#ifdef WM_POINTERDOWN
    switch (msg) {
        case WM_POINTERDOWN:
        case WM_POINTERUPDATE:
        case WM_POINTERUP:
        case WM_POINTERCAPTURECHANGED:
        {
            const std::uint32_t id = static_cast<std::uint32_t>(GET_POINTERID_WPARAM(w_param));
            if (!pointer_is_touch(id)) return false;
            ImVec2 pos = pointer_pos_screen(hwnd, id, l_param);
            if (msg == WM_POINTERDOWN) {
                g_touch.down(id, pos);
            } else if (msg == WM_POINTERUPDATE) {
                g_touch.move(id, pos);
            } else if (msg == WM_POINTERUP) {
                g_touch.up(id, pos);
            } else {
                g_touch.cancel(id);
            }
            return true;
        }
        default:
            break;
    }
#else
    (void)msg;
    (void)w_param;
    (void)l_param;
#endif
    return false;
}

void new_frame() {
    g_touch.consume_frame();
}

const TouchFrame& current_frame() {
    return g_touch.frame;
}

bool wants_continuous_render() {
    return !g_touch.touches.empty() || g_touch.pending.tap || g_touch.pending.long_press ||
           g_touch.pending.single_drag || g_touch.pending.pinch ||
           g_touch.frame.single_drag || g_touch.frame.pinch;
}

bool consume_tap_in_rect(ImVec2 min, ImVec2 max, ImVec2* pos) {
    if (g_touch.tap_consumed || !g_touch.frame.tap || !point_in_rect(g_touch.frame.tap_pos, min, max)) return false;
    g_touch.tap_consumed = true;
    if (pos) *pos = g_touch.frame.tap_pos;
    return true;
}

bool consume_long_press_in_rect(ImVec2 min, ImVec2 max, ImVec2* pos) {
    if (g_touch.long_press_consumed || !g_touch.frame.long_press ||
        !point_in_rect(g_touch.frame.long_press_pos, min, max)) {
        return false;
    }
    g_touch.long_press_consumed = true;
    if (pos) *pos = g_touch.frame.long_press_pos;
    return true;
}

bool open_popup_on_last_item_long_press(const char* popup_id) {
    ImVec2 pos;
    if (!consume_long_press_in_rect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), &pos)) return false;
    (void)pos;
    ImGui::OpenPopup(popup_id);
    return true;
}

void apply_touch_scroll_to_hovered_window() {
    const TouchFrame& touch = current_frame();
    if (!touch.single_drag || touch.active_count != 1) return;
    if (std::abs(touch.single_drag_delta.x) < 0.01f && std::abs(touch.single_drag_delta.y) < 0.01f) return;
    if (!GImGui || !GImGui->HoveredWindow) return;

    ImGuiWindow* window = GImGui->HoveredWindow;
    if ((window->ScrollMax.x <= 0.0f && window->ScrollMax.y <= 0.0f) || window->Collapsed) return;
    ImRect rect(window->Pos, ImVec2(window->Pos.x + window->Size.x, window->Pos.y + window->Size.y));
    if (!rect.Contains(touch.single_start_pos) && !rect.Contains(touch.single_pos)) return;

    if (window->ScrollMax.x > 0.0f) {
        window->Scroll.x = std::clamp(window->Scroll.x - touch.single_drag_delta.x, 0.0f, window->ScrollMax.x);
    }
    if (window->ScrollMax.y > 0.0f) {
        window->Scroll.y = std::clamp(window->Scroll.y - touch.single_drag_delta.y, 0.0f, window->ScrollMax.y);
    }
}

void debug_reset_for_tests(double now_seconds) {
    g_touch.reset(now_seconds);
}

void debug_set_time_for_tests(double now_seconds) {
    g_touch.debug_time = now_seconds;
}

void debug_touch_down(std::uint32_t id, ImVec2 pos) {
    g_touch.down(id, pos);
}

void debug_touch_move(std::uint32_t id, ImVec2 pos) {
    g_touch.move(id, pos);
}

void debug_touch_up(std::uint32_t id, ImVec2 pos) {
    g_touch.up(id, pos);
}

} // namespace touch_input
