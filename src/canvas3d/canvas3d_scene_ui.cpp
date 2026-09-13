/*
 * Copyright (c) 2026 Sapporo_ningyo
 *
 * Licensed under Apache License 2.0; see LICENSE and NOTICE.
 */

#ifdef _MSC_VER
#pragma execution_character_set("utf-8")
#endif

#include "canvas3d_impl.h"
#include "kme.h"
#include "scene_route_overlay.h"
#include "scene_frame_profile.h"
#include "touch_input.h"
#include "imgui.h"
#include <d3d11.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <optional>
#include <string>

using namespace canvas3d_detail;

namespace canvas3d_detail {

constexpr float k_scene_fps_smoothing = 0.15f;

} // namespace canvas3d_detail

namespace canvas3d_detail {

// The event-driven canvas stops repainting while idle, so preserve the last active FPS across long gaps.
constexpr double k_scene_fps_idle_reset_seconds = 0.25;

} // namespace canvas3d_detail

void Canvas3D::Impl::reset_scene_fps_counter() {
    scene_fps_last_frame_valid = false;
    scene_fps_value = 0.0f;
}

void Canvas3D::Impl::update_scene_fps_counter() {
    using Clock = std::chrono::steady_clock;
    const Clock::time_point now = Clock::now();
    if (scene_fps_last_frame_valid) {
        const double elapsed_seconds = std::chrono::duration<double>(now - scene_fps_last_frame_at).count();
        if (elapsed_seconds > 0.0 && elapsed_seconds <= k_scene_fps_idle_reset_seconds) {
            const float sample = static_cast<float>(1.0 / elapsed_seconds);
            scene_fps_value = scene_fps_value > 0.0f
                ? scene_fps_value + (sample - scene_fps_value) * k_scene_fps_smoothing
                : sample;
        }
    }
    scene_fps_last_frame_at = now;
    scene_fps_last_frame_valid = true;
}

SceneOverlayLabelLayout Canvas3D::Impl::scene_overlay_label_layout(
    ImVec2 origin, ImVec2 size, ImVec2 text_size, SceneOverlayCorner corner) const {
    const float pad = std::max(4.0f, ImGui::GetStyle().FramePadding.x);
    ImVec2 pos(origin.x + pad * 2.0f, origin.y + pad * 2.0f);
    if (corner == SceneOverlayCorner::TopRight) {
        pos.x = origin.x + size.x - text_size.x - pad * 2.0f;
    } else if (corner == SceneOverlayCorner::BottomRight) {
        pos.x = origin.x + size.x - text_size.x - pad * 2.0f;
        pos.y = origin.y + size.y - text_size.y - pad * 2.0f;
    }
    pos.x = std::max(origin.x + pad, pos.x);
    pos.y = std::max(origin.y + pad, pos.y);
    return SceneOverlayLabelLayout{pos, pad};
}

void Canvas3D::Impl::draw_scene_overlay_label_background(ImDrawList* draw,
                                         const SceneOverlayLabelLayout& layout,
                                         ImVec2 text_size) const {
    if (!draw) return;
    draw->AddRectFilled(ImVec2(layout.pos.x - layout.pad, layout.pos.y - layout.pad * 0.5f),
                        ImVec2(layout.pos.x + text_size.x + layout.pad,
                               layout.pos.y + text_size.y + layout.pad * 0.5f),
                        IM_COL32(0, 0, 0, 140), 3.0f);
}

void Canvas3D::Impl::draw_scene_overlay_label(ImDrawList* draw, ImVec2 origin, ImVec2 size,
                              const char* text, SceneOverlayCorner corner) const {
    if (!draw || !text || size.x <= 0.0f || size.y <= 0.0f) return;
    const ImVec2 text_size = ImGui::CalcTextSize(text);
    const SceneOverlayLabelLayout layout =
        scene_overlay_label_layout(origin, size, text_size, corner);
    draw_scene_overlay_label_background(draw, layout, text_size);
    draw->AddText(layout.pos, IM_COL32(255, 255, 255, 230), text);
}

void Canvas3D::Impl::draw_scene_overlay(ImDrawList* draw, ImVec2 origin, ImVec2 size) const {
    if (!draw || size.x <= 0.0f || size.y <= 0.0f || !scene_active) return;
    char buffer[256] = {};
    std::snprintf(buffer, sizeof(buffer), "x=%.1fm  y=%.1fm  d=%.1fm",
                  scene_camera_lateral_offset,
                  static_cast<double>(scene_camera_vertical_offset),
                  scene_camera_distance);
    draw_scene_overlay_label(draw, origin, size, buffer, SceneOverlayCorner::TopLeft);
}

void Canvas3D::Impl::draw_scene_route_overlay(ImDrawList* draw, ImVec2 origin, ImVec2 size,
                              const Canvas3DSceneUiText& ui_text) const {
    if (!draw || size.x <= 0.0f || size.y <= 0.0f || !scene_active) return;

    char curve_line[512] = {};
    scene_route_overlay::format_curve_line(
        curve_line,
        sizeof(curve_line),
        scene_data.route_info.radius_events,
        scene_data.route_info.cant_events,
        scene_camera_distance,
        ui_text.straight,
        ui_text.interpolate);
    const route_value_sampling::Sample gradient = route_value_sampling::sample(
        scene_data.route_info.gradient_events, scene_camera_distance);

    const bool gradient_transition =
        gradient.mode != route_value_sampling::Mode::Constant;
    const bool use_gradient_target =
        !gradient_transition || std::abs(gradient.to_value) > k_scene_route_display_zero_epsilon ||
        std::abs(gradient.from_value) <= k_scene_route_display_zero_epsilon;
    const double displayed_gradient = gradient_transition
        ? (use_gradient_target ? gradient.to_value : gradient.from_value)
        : gradient.value;
    char gradient_text[64] = {};
    scene_route_overlay::format_number(gradient_text, sizeof(gradient_text),
                                       std::abs(displayed_gradient));
    char gradient_line[160] = {};
    const char* gradient_prefix = gradient_transition ? "[Tr.] " : "";
    if (std::abs(displayed_gradient) <= k_scene_route_display_zero_epsilon) {
        std::snprintf(gradient_line, sizeof(gradient_line), "%s0‰", gradient_prefix);
    } else {
        std::snprintf(gradient_line, sizeof(gradient_line), "%s%s %s‰",
                      gradient_prefix,
                      displayed_gradient > 0.0 ? u8"↗" : u8"↘",
                      gradient_text);
    }

    char speed_limit_line[160] = {};
    const auto speed_limit_next = std::upper_bound(
        scene_data.route_info.speed_limit_events.begin(),
        scene_data.route_info.speed_limit_events.end(), scene_camera_distance,
        [](double distance, const Canvas3DSceneSpeedLimitEvent& event) {
            return distance < event.distance;
        });
    if (speed_limit_next == scene_data.route_info.speed_limit_events.begin() ||
        !(speed_limit_next - 1)->has_speed) {
        std::snprintf(speed_limit_line, sizeof(speed_limit_line), "%s -",
                      ui_text.speed_limit);
    } else {
        char speed_text[64] = {};
        scene_route_overlay::format_number(speed_text, sizeof(speed_text),
                                           (speed_limit_next - 1)->speed);
        std::snprintf(speed_limit_line, sizeof(speed_limit_line),
                      "%s %s km/h", ui_text.speed_limit, speed_text);
    }

    char signal_line[768] = {};
    const auto section_signal_next = std::upper_bound(
        scene_data.route_info.section_signal_events.begin(),
        scene_data.route_info.section_signal_events.end(), scene_camera_distance,
        [](double distance, const Canvas3DSceneSectionSignalEvent& event) {
            return distance < event.distance;
        });
    if (section_signal_next ==
            scene_data.route_info.section_signal_events.begin() ||
        (section_signal_next - 1)->values.empty()) {
        std::snprintf(signal_line, sizeof(signal_line), "%s -", ui_text.signal);
    } else {
        std::snprintf(signal_line, sizeof(signal_line), "%s %s", ui_text.signal,
                      (section_signal_next - 1)->values.c_str());
    }

    char station_line[768] = {};
    const auto next_station = std::upper_bound(
        scene_data.route_info.stations.begin(), scene_data.route_info.stations.end(),
        scene_camera_distance,
        [](double distance, const Canvas3DSceneRouteStation& station) {
            return distance < station.distance;
        });
    if (next_station == scene_data.route_info.stations.end()) {
        std::snprintf(station_line, sizeof(station_line), "%s",
                      ui_text.no_station_ahead);
    } else {
        const double remaining_m = std::max(0.0, next_station->distance - scene_camera_distance);
        std::snprintf(station_line, sizeof(station_line), "%s %s  %.0fm",
                      ui_text.next_station, next_station->name.c_str(), remaining_m);
    }

    char buffer[2048] = {};
    std::snprintf(buffer, sizeof(buffer), "%s\n%s\n%s\n%s\n%s",
                  curve_line, gradient_line, speed_limit_line,
                  signal_line, station_line);
    draw_scene_overlay_label(draw, origin, size, buffer, SceneOverlayCorner::TopRight);
}

ImU32 Canvas3D::Impl::scene_instance_metric_color(size_t instance_count) const {
    if (!scene_performance_warning_enabled) return IM_COL32(255, 255, 255, 230);
    if (instance_count > scene_instance_critical_warning_threshold) {
        return IM_COL32(255, 96, 96, 230);
    }
    if (instance_count > scene_instance_warning_threshold) {
        return IM_COL32(255, 220, 0, 230);
    }
    return IM_COL32(255, 255, 255, 230);
}

void Canvas3D::Impl::draw_scene_metrics_overlay(ImDrawList* draw, ImVec2 origin, ImVec2 size,
                                const Canvas3DSceneStats& stats) const {
    if (!draw || size.x <= 0.0f || size.y <= 0.0f || !scene_active) return;
    char prefix[64] = {};
    char instance[64] = {};
    char suffix[96] = {};
    std::snprintf(prefix, sizeof(prefix), "chunks=%zu  ", stats.chunk_count);
    std::snprintf(instance, sizeof(instance), "instances=%zu", stats.drawn_instance_count);
    std::snprintf(suffix, sizeof(suffix), "  models=%zu/%zu  %.1f fps",
                  stats.model_ready_count,
                  stats.model_path_count,
                  static_cast<double>(scene_fps_value));
    const ImVec2 prefix_size = ImGui::CalcTextSize(prefix);
    const ImVec2 instance_size = ImGui::CalcTextSize(instance);
    const ImVec2 suffix_size = ImGui::CalcTextSize(suffix);
    const ImVec2 text_size(
        prefix_size.x + instance_size.x + suffix_size.x,
        std::max(prefix_size.y, std::max(instance_size.y, suffix_size.y)));
    const SceneOverlayLabelLayout layout = scene_overlay_label_layout(
        origin, size, text_size, SceneOverlayCorner::BottomRight);
    draw_scene_overlay_label_background(draw, layout, text_size);
    const ImU32 normal_color = IM_COL32(255, 255, 255, 230);
    draw->AddText(layout.pos, normal_color, prefix);
    ImVec2 instance_pos(layout.pos.x + prefix_size.x, layout.pos.y);
    draw->AddText(instance_pos,
                  scene_instance_metric_color(stats.drawn_instance_count), instance);
    ImVec2 suffix_pos(instance_pos.x + instance_size.x, layout.pos.y);
    draw->AddText(suffix_pos, normal_color, suffix);
}

void Canvas3D::Impl::draw_scene_loading_overlay(ImDrawList* draw, ImVec2 origin, ImVec2 size,
                                const char* text) const {
    if (!draw || size.x <= 0.0f || size.y <= 0.0f) return;
    ImVec2 end(origin.x + size.x, origin.y + size.y);
    ImVec4 bg = clamp_theme_color(background_color_value);
    bg.w = 1.0f;
    draw->AddRectFilled(origin, end, ImGui::ColorConvertFloat4ToU32(bg));

    const char* label = text && text[0] ? text : "Loading...";
    ImVec2 text_size = ImGui::CalcTextSize(label);
    ImVec2 pos(origin.x + std::max(0.0f, (size.x - text_size.x) * 0.5f),
               origin.y + std::max(0.0f, (size.y - text_size.y) * 0.5f));
    const float luminance = bg.x * 0.2126f + bg.y * 0.7152f + bg.z * 0.0722f;
    const ImU32 text_color = luminance > 0.55f ? IM_COL32(24, 24, 24, 235)
                                               : IM_COL32(255, 255, 255, 235);
    const ImU32 shadow_color = luminance > 0.55f ? IM_COL32(255, 255, 255, 90)
                                                 : IM_COL32(0, 0, 0, 110);
    draw->AddText(ImVec2(pos.x + 1.0f, pos.y + 1.0f), shadow_color, label);
    draw->AddText(pos, text_color, label);
}

const char* Canvas3D::Impl::scene_object_edit_row_kind(const Canvas3DSceneObject& object) {
    if (object.kind == Canvas3DSceneObjectKind::Structure) {
        return object.structure_put_between ? "structure.between" : "structure.put";
    }
    if (object.kind == Canvas3DSceneObjectKind::Signal) return "signal.put";
    return object.kind == Canvas3DSceneObjectKind::Repeater ? "repeater" : nullptr;
}

Canvas3DSceneContextAction Canvas3D::Impl::render_scene_context_popup(
    const Canvas3DSceneUiText& ui_text,
    const Canvas3DSceneContextMenuOptions& context_menu_options) {
    Canvas3DSceneContextAction action;
    if (!ImGui::BeginPopup("ScenePreviewObjectContext")) return action;

    if (scene_object_index_valid(scene_context_object_index)) {
        Canvas3DSceneObject& object = scene_data.objects[static_cast<size_t>(scene_context_object_index)];
        if (object.kind == Canvas3DSceneObjectKind::Structure) {
            const char* locate_label = object.structure_put_between
                ? ui_text.locate_structure_put_between_list
                : ui_text.locate_structure_list;
            if (ImGui::MenuItem(locate_label)) {
                action.kind = Canvas3DSceneContextActionKind::LocateStructure;
                action.row_index = object.source_row;
            }
            const char* edit_row_kind = scene_object_edit_row_kind(object);
            ImGui::BeginDisabled(!context_menu_options.element_properties_enabled ||
                                 object.edit_id.empty() || !edit_row_kind);
            if (ImGui::MenuItem(ui_text.element_properties)) {
                action.kind = Canvas3DSceneContextActionKind::EditElement;
                action.edit_id = object.edit_id;
                action.row_kind = edit_row_kind;
            }
            if (ImGui::MenuItem(ui_text.delete_element)) {
                action.kind = Canvas3DSceneContextActionKind::DeleteElement;
                action.edit_id = object.edit_id;
                action.row_kind = edit_row_kind;
            }
            ImGui::EndDisabled();
        } else if (object.kind == Canvas3DSceneObjectKind::Repeater) {
            if (ImGui::MenuItem(ui_text.locate_repeater_list)) {
                action.kind = Canvas3DSceneContextActionKind::LocateRepeater;
                action.row_index = object.source_row;
            }
            const char* edit_row_kind = scene_object_edit_row_kind(object);
            ImGui::BeginDisabled(!context_menu_options.element_properties_enabled ||
                                 object.edit_id.empty() || !edit_row_kind);
            if (ImGui::MenuItem(ui_text.element_properties)) {
                action.kind = Canvas3DSceneContextActionKind::EditElement;
                action.edit_id = object.edit_id;
                action.row_kind = edit_row_kind;
            }
            ImGui::EndDisabled();
            const Canvas3DRepeaterSegment* repeater = find_repeater_segment(object.source_row);
            const bool delete_enabled = context_menu_options.element_properties_enabled &&
                !object.edit_id.empty() && edit_row_kind && repeater;
            if (repeater && repeater->chain_begin_count > 1) {
                if (ImGui::BeginMenu(ui_text.delete_element, delete_enabled)) {
                    if (ImGui::MenuItem(ui_text.delete_repeater_all)) {
                        action.kind = Canvas3DSceneContextActionKind::DeleteRepeaterAll;
                    } else if (ImGui::MenuItem(ui_text.delete_repeater_change_point)) {
                        action.kind = Canvas3DSceneContextActionKind::DeleteRepeaterChangePoint;
                    } else if (repeater->chain_begin_index != 0 &&
                               ImGui::MenuItem(ui_text.trim_repeater_to_change_point)) {
                        action.kind = Canvas3DSceneContextActionKind::TrimRepeaterToChangePoint;
                    } else if (repeater->chain_begin_index != 0 &&
                               ImGui::MenuItem(ui_text.start_repeater_from_change_point)) {
                        action.kind = Canvas3DSceneContextActionKind::StartRepeaterFromChangePoint;
                    }
                    if (action.kind != Canvas3DSceneContextActionKind::None) {
                        action.edit_id = object.edit_id;
                        action.row_kind = edit_row_kind;
                    }
                    ImGui::EndMenu();
                }
            } else {
                ImGui::BeginDisabled(!delete_enabled);
                if (ImGui::MenuItem(ui_text.delete_element)) {
                    action.kind = Canvas3DSceneContextActionKind::DeleteRepeaterAll;
                    action.edit_id = object.edit_id;
                    action.row_kind = edit_row_kind;
                }
                ImGui::EndDisabled();
            }
            ImGui::Separator();
            if (ImGui::MenuItem(ui_text.jump_to_repeater_start_position)) {
                jump_scene_camera_to_object(Canvas3DSceneObjectKind::Repeater, object.source_row);
            }
            ImGui::BeginDisabled(!repeater || !repeater->has_end_or_change_position);
            if (ImGui::MenuItem(ui_text.jump_to_repeater_end_or_change_position)) {
                jump_scene_camera_to_repeater_end_or_change(object.source_row);
            }
            ImGui::EndDisabled();
        } else if (object.kind == Canvas3DSceneObjectKind::Signal) {
            if (ImGui::MenuItem(ui_text.locate_signal_list)) {
                action.kind = Canvas3DSceneContextActionKind::LocateSignal;
                action.row_index = object.source_row;
            }
            const char* edit_row_kind = scene_object_edit_row_kind(object);
            ImGui::BeginDisabled(!context_menu_options.element_properties_enabled ||
                                 object.edit_id.empty() || !edit_row_kind);
            if (ImGui::MenuItem(ui_text.element_properties)) {
                action.kind = Canvas3DSceneContextActionKind::EditElement;
                action.edit_id = object.edit_id;
                action.row_kind = edit_row_kind;
            }
            if (ImGui::MenuItem(ui_text.delete_element)) {
                action.kind = Canvas3DSceneContextActionKind::DeleteElement;
                action.edit_id = object.edit_id;
                action.row_kind = edit_row_kind;
            }
            ImGui::EndDisabled();
            if (ImGui::BeginMenu(ui_text.switch_signal_aspect, !object.model_options.empty())) {
                for (size_t i = 0; i < object.model_options.size(); ++i) {
                    const Canvas3DSceneModelOption& option = object.model_options[i];
                    std::string label = std::to_string(option.structure_key_index) + " - " + option.structure_key;
                    const bool selected = i == object.selected_model_option;
                    ImGui::BeginDisabled(option.model_path.empty());
                    if (ImGui::MenuItem(label.c_str(), nullptr, selected)) {
                        set_scene_object_model_option(scene_context_object_index, i);
                    }
                    ImGui::EndDisabled();
                }
                ImGui::EndMenu();
            }
        }
    }

    ImGui::EndPopup();
    return action;
}

bool Canvas3D::Impl::scene_marker_has_list_target(const Canvas3DSceneMarker& marker) {
    return scene_marker_list_kind_is_navigable(marker.list_kind) &&
        marker.row_index.has_value();
}

bool Canvas3D::Impl::scene_marker_has_edit_target(const Canvas3DSceneMarker& marker) {
    return (marker.kind == MapMarkerVisualKind::Station &&
             marker.row_kind == "station.put") ||
           marker.row_kind == "curve" || marker.row_kind == "gradient" ||
           marker.row_kind == "otherTrack.change" ||
           scene_marker_has_list_target(marker);
}

Canvas3DSceneContextAction Canvas3D::Impl::render_scene_marker_context_popup(
    const Canvas3DSceneUiText& ui_text,
    const Canvas3DSceneContextMenuOptions& context_menu_options) {
    Canvas3DSceneContextAction action;
    if (!ImGui::BeginPopup("ScenePreviewMarkerContext")) return action;

    if (scene_context_marker_index >= 0 &&
        static_cast<size_t>(scene_context_marker_index) < scene_data.markers.size()) {
        const Canvas3DSceneMarker& marker =
            scene_data.markers[static_cast<size_t>(scene_context_marker_index)];
        const char* locate_label = scene_marker_has_list_target(marker)
            ? ui_text.locate_marker_list_labels[
                scene_marker_list_kind_slot(marker.list_kind)]
            : nullptr;
        if (locate_label && ImGui::MenuItem(locate_label)) {
            action.kind = Canvas3DSceneContextActionKind::LocateMarkerList;
            action.marker_list_kind = marker.list_kind;
            action.row_index = *marker.row_index;
        }

        const bool edit_target = scene_marker_has_edit_target(marker);
        if (edit_target) {
            ImGui::BeginDisabled(!context_menu_options.element_properties_enabled ||
                                 marker.edit_id.empty());
            if (ImGui::MenuItem(ui_text.element_properties)) {
                action.kind = Canvas3DSceneContextActionKind::EditElement;
                action.edit_id = marker.edit_id;
                action.row_kind = marker.row_kind;
            }
            ImGui::EndDisabled();
            ImGui::BeginDisabled(!context_menu_options.element_properties_enabled ||
                                 marker.edit_id.empty());
            if (ImGui::MenuItem(ui_text.delete_element)) {
                action.kind = Canvas3DSceneContextActionKind::DeleteElement;
                action.edit_id = marker.edit_id;
                action.row_kind = marker.row_kind;
            }
            ImGui::EndDisabled();
        }
        if (context_menu_options.element_properties_enabled &&
            marker.unpaired_transition) {
            ImGui::Separator();
            ImGui::TextWrapped("%s", ui_text.unpaired_transition);
        }
    }

    ImGui::EndPopup();
    return action;
}

std::optional<double> Canvas3D::Impl::render_scene_mileage_context_popup(
    const Canvas3DSceneUiText& ui_text,
    const Canvas3DSceneContextMenuOptions& context_menu_options) {
    std::optional<double> requested_mileage;
    if (ImGui::BeginPopup("ScenePreviewMileageContext")) {
        if (scene_interaction_mode != Canvas3DSceneInteractionMode::MileageSelect ||
            !scene_context_mileage) {
            ImGui::CloseCurrentPopup();
        } else if (ImGui::MenuItem(ui_text.add_map_element_at_mileage, nullptr, false,
                                  context_menu_options.new_element_enabled)) {
            requested_mileage = scene_context_mileage;
        }
        ImGui::EndPopup();
    }
    if (!ImGui::IsPopupOpen("ScenePreviewMileageContext")) {
        scene_context_mileage.reset();
    }
    return requested_mileage;
}

void Canvas3D::Impl::draw_scene_mileage_label(ImVec2 origin,
                                     ImVec2 end,
                                     ImVec2 pointer_pos,
                                     double mileage,
                                     const Canvas3DSceneUiText& ui_text) {
    std::array<char, 128> label{};
    std::snprintf(label.data(), label.size(), "%s: %.0f %s",
                  ui_text.mileage, mileage, ui_text.unit_m);

    const ImGuiStyle& style = ImGui::GetStyle();
    const ImVec2 text_size = ImGui::CalcTextSize(label.data());
    const ImVec2 label_size(text_size.x + style.WindowPadding.x * 2.0f,
                            text_size.y + style.WindowPadding.y * 2.0f);
    const float pointer_gap = style.ItemSpacing.x;
    float label_x = pointer_pos.x + pointer_gap;
    if (label_x + label_size.x > end.x) {
        label_x = pointer_pos.x - pointer_gap - label_size.x;
    }
    label_x = std::clamp(label_x, origin.x,
                         std::max(origin.x, end.x - label_size.x));
    const float label_y = std::clamp(pointer_pos.y - label_size.y * 0.5f,
                                     origin.y,
                                     std::max(origin.y, end.y - label_size.y));

    ImGui::SetNextWindowPos(ImVec2(label_x, label_y), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.9f);
    constexpr ImGuiWindowFlags label_flags =
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
        ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoMove;
    if (ImGui::Begin("##ScenePreviewMileageLabel", nullptr, label_flags)) {
        ImGui::TextUnformatted(label.data());
    }
    ImGui::End();
}

Canvas3DSceneFrameResult Canvas3D::Impl::render_scene_preview(
    ImVec2 requested_size,
    const Canvas3DSceneUiText& ui_text,
    const Canvas3DSceneContextMenuOptions& context_menu_options) {
#ifndef NDEBUG
    scene_frame_profiler.begin_frame(context);
#endif
    Canvas3DSceneFrameResult result;
    ImVec2 avail = requested_size;
    if (avail.x <= 0.0f || avail.y <= 0.0f) avail = ImGui::GetContentRegionAvail();
    avail.x = std::max(avail.x, 50.0f);
    avail.y = std::max(avail.y, 50.0f);

    ImVec2 origin = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("ScenePreview3DCanvas", avail,
                           ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
    bool hovered = ImGui::IsItemHovered();
    const bool context_popup_open =
        ImGui::IsPopupOpen("ScenePreviewObjectContext") ||
        ImGui::IsPopupOpen("ScenePreviewMarkerContext") ||
        ImGui::IsPopupOpen("ScenePreviewMileageContext");
    const bool loading_before_render = scene_stats().loading;
    ImGuiIO& io = ImGui::GetIO();
    ImVec2 pointer_pos = io.MousePos;
    const touch_input::TouchFrame& touch = touch_input::current_frame();
    if (touch.long_press) pointer_pos = touch.long_press_pos;
    else if (touch.tap) pointer_pos = touch.tap_pos;
    ImVec2 mouse_local(pointer_pos.x - origin.x, pointer_pos.y - origin.y);

    int width = std::max(1, static_cast<int>(std::round(avail.x)));
    int height = std::max(1, static_cast<int>(std::round(avail.y)));
    if (scene_active && !loading_before_render) {
        result.placement_drag = handle_scene_structure_gizmo_input(
            hovered, width, height, mouse_local);
        handle_scene_input(hovered, scene_structure_gizmo_consumes_left_input());
    }
    if (scene_active) {
        render_scene_preview_target(
            width, height, mouse_local,
            (hovered || context_popup_open) && !scene_structure_gizmo_consumes_left_input(),
            hovered && !loading_before_render);
    } else {
        std::string error;
        ensure_render_target(width, height, error);
        if (render_rtv && context) {
            const ImVec4 bg = clamp_theme_color(background_color_value);
            const float clear_color[4] = {bg.x, bg.y, bg.z, 1.0f};
            context->OMSetRenderTargets(1, &render_rtv, depth_dsv);
            context->ClearRenderTargetView(render_rtv, clear_color);
            if (depth_dsv) context->ClearDepthStencilView(depth_dsv, D3D11_CLEAR_DEPTH, 1.0f, 0);
            ID3D11RenderTargetView* null_rtv = nullptr;
            context->OMSetRenderTargets(1, &null_rtv, nullptr);
        }
    }

    ImDrawList* draw = ImGui::GetWindowDrawList();
    ImVec2 end(origin.x + avail.x, origin.y + avail.y);
    if (render_srv) {
        draw->AddImage(reinterpret_cast<void*>(render_srv), origin, end);
    } else {
        draw->AddRectFilled(origin, end, IM_COL32(0, 0, 0, 255));
    }
    const Canvas3DSceneStats stats = scene_stats();
    {
    KME_SCENE_PROFILE(Overlay);
    if (stats.loading) {
        draw_scene_loading_overlay(draw, origin, avail, ui_text.loading);
    } else {
        draw_scene_overlay(draw, origin, avail);
        draw_scene_route_overlay(draw, origin, avail, ui_text);
        draw_scene_metrics_overlay(draw, origin, avail, stats);
        draw_scene_structure_gizmo(draw, origin, width, height);
    }
    }

    const bool select_mode = scene_interaction_mode == Canvas3DSceneInteractionMode::Select;
    const bool mileage_select_mode =
        scene_interaction_mode == Canvas3DSceneInteractionMode::MileageSelect;
    if (mileage_select_mode && !stats.loading && hovered &&
        scene_hovered_mileage) {
        draw_scene_mileage_label(origin, end, pointer_pos,
                                 *scene_hovered_mileage, ui_text);
    }
    const bool marker_context_available =
        scene_hovered_marker_index >= 0 &&
        static_cast<size_t>(scene_hovered_marker_index) < scene_data.markers.size() &&
        (scene_marker_has_list_target(
             scene_data.markers[static_cast<size_t>(scene_hovered_marker_index)]) ||
         scene_marker_has_edit_target(
             scene_data.markers[static_cast<size_t>(scene_hovered_marker_index)]));
    if (!stats.loading && scene_structure_gizmo_consumes_left_input()) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
    } else if (!stats.loading && select_mode && hovered &&
               (scene_hovered_object_index >= 0 ||
                scene_hovered_marker_index >= 0)) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    }
    ImVec2 long_press_pos;
    const bool touch_context =
        select_mode && !stats.loading &&
        (scene_hovered_object_index >= 0 || marker_context_available) &&
        touch_input::consume_long_press_in_rect(origin, end, &long_press_pos);
    if (!stats.loading && select_mode &&
        (scene_hovered_object_index >= 0 || marker_context_available) &&
        ((hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) || touch_context)) {
        if (scene_hovered_object_index >= 0) {
            scene_context_object_index = scene_hovered_object_index;
            ImGui::OpenPopup("ScenePreviewObjectContext");
        } else {
            scene_context_marker_index = scene_hovered_marker_index;
            ImGui::OpenPopup("ScenePreviewMarkerContext");
        }
    }
    if (!stats.loading && mileage_select_mode && hovered && scene_hovered_mileage &&
        ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
        scene_context_mileage = scene_hovered_mileage;
        ImGui::OpenPopup("ScenePreviewMileageContext");
    }
    result.context_action = render_scene_context_popup(ui_text, context_menu_options);
    if (result.context_action.kind == Canvas3DSceneContextActionKind::None) {
        result.context_action = render_scene_marker_context_popup(
            ui_text, context_menu_options);
    }
    result.new_element_mileage =
        render_scene_mileage_context_popup(ui_text, context_menu_options);
#ifndef NDEBUG
    scene_frame_profiler.end_frame(context);
#endif
    return result;
}
