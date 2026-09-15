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
#include "datatable_internal.h"
#include "touch_input.h"

#include "canvas3D.h"
#include "text_decoder.h"
#include "imgui.h"
#include "misc/cpp/imgui_stdlib.h"
#include "repeater_linkage.h"

#include <windows.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <initializer_list>
#include <map>
#include <string>
#include <system_error>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

using namespace datatable_internal;

void App::render_scenario_file_window() {
    if (!show_scenario_file_window_ || !scenario_preview_) return;
    if (dock_right_id_) ImGui::SetNextWindowDockID(
        dock_right_id_, ImGuiCond_FirstUseEver);
    if (focus_scenario_file_next_) ImGui::SetNextWindowFocus();
    std::string title = tr("frame.scenario_file") + "###ScenarioFile";
    if (!ImGui::Begin(title.c_str(), &show_scenario_file_window_)) {
        focus_scenario_file_next_ = false;
        ImGui::End();
        return;
    }

    ScenarioPreview& preview = *scenario_preview_;
    const bool editable = edit_mode_enabled_ && !scenario_source_path_.empty();
    const auto relative_scenario_path = [&](const std::string& path) {
        if (scenario_source_path_.empty() || path.empty()) return std::string{};
        return resolve_list_asset_path(scenario_source_path_, path);
    };
    const auto select_scenario_path = [&](std::string& path, bool image) {
        if (!editable) return;
        const std::string initial = list_asset_picker_initial_directory(
            relative_scenario_path(path), scenario_source_path_);
        const std::string selected = image
            ? open_image_dialog(initial)
            : open_map_dialog(initial);
        if (selected.empty()) return;
        ListAssetSourcePathResult result = make_list_asset_source_path(
            scenario_source_path_, selected);
        path = result.source_path;
        if (!result.fallback_reason.empty()) {
            KME_ADD_LOG(LogSeverity::Warning,
                        "Scenario path kept absolute: " + result.fallback_reason);
            set_program_status("status.scenario_path_absolute_fallback");
        }
        update_scenario_route_warning();
    };
    const auto open_scenario_path = [&](const std::string& path) {
        const std::string resolved = relative_scenario_path(path);
        std::string error;
        std::error_code filesystem_error;
        const std::filesystem::path resolved_path =
            resolved.empty() ? std::filesystem::path{}
                             : kme::maploader::path_from_utf8(resolved);
        if (!resolved.empty() && std::filesystem::is_directory(
                resolved_path.parent_path(), filesystem_error) && !filesystem_error) {
            open_parent_directory_in_explorer(resolved, &error);
        } else {
            error = filesystem_error
                ? "Scenario path parent directory check failed: " + filesystem_error.message()
                : "Scenario path does not resolve to an existing file";
        }
        if (!error.empty()) KME_ADD_LOG(LogSeverity::Warning,
                                        "Open in Explorer failed: " + error);
    };
    enum class ScenarioCandidateActionKind {
        None,
        AddAfter,
        Delete,
        MoveUp,
        MoveDown,
    };
    ScenarioCandidateActionKind pending_candidate_action =
        ScenarioCandidateActionKind::None;
    std::vector<ScenarioPreviewPath>* pending_candidate_paths = nullptr;
    size_t pending_candidate_index = 0;
    const auto defer_candidate_action =
        [&](ScenarioCandidateActionKind kind,
            std::vector<ScenarioPreviewPath>& paths, size_t index) {
            if (pending_candidate_action != ScenarioCandidateActionKind::None) return;
            pending_candidate_action = kind;
            pending_candidate_paths = &paths;
            pending_candidate_index = index;
        };
    const auto render_context_menu = [&](const char* id, std::string& path, bool image,
                                         std::vector<ScenarioPreviewPath>* candidate_paths,
                                         size_t candidate_index) {
        if (!ImGui::BeginPopupContextItem(id, ImGuiPopupFlags_MouseButtonRight)) return;
        if (!editable) ImGui::BeginDisabled();
        if (ImGui::MenuItem(tr("menu.select_file").c_str()) && editable) {
            select_scenario_path(path, image);
        }
        if (!editable) ImGui::EndDisabled();
        const bool can_open = !path.empty();
        if (!can_open) ImGui::BeginDisabled();
        if (ImGui::MenuItem(tr("menu.open_in_explorer").c_str())) {
            open_scenario_path(path);
        }
        if (!can_open) ImGui::EndDisabled();
        if (candidate_paths) {
            ImGui::Separator();
            ImGui::BeginDisabled(!editable);
            if (ImGui::MenuItem(tr("context.scenario.add_candidate").c_str())) {
                defer_candidate_action(ScenarioCandidateActionKind::AddAfter,
                                       *candidate_paths, candidate_index);
                ImGui::EndDisabled();
                ImGui::EndPopup();
                return;
            }
            ImGui::EndDisabled();

            ImGui::BeginDisabled(!editable || candidate_paths->size() <= 1);
            if (ImGui::MenuItem(tr("context.scenario.delete_candidate").c_str())) {
                defer_candidate_action(ScenarioCandidateActionKind::Delete,
                                       *candidate_paths, candidate_index);
                ImGui::EndDisabled();
                ImGui::EndPopup();
                return;
            }
            ImGui::EndDisabled();

            ImGui::BeginDisabled(!editable || candidate_index == 0);
            if (ImGui::MenuItem(tr("context.scenario.move_up").c_str())) {
                defer_candidate_action(ScenarioCandidateActionKind::MoveUp,
                                       *candidate_paths, candidate_index);
                ImGui::EndDisabled();
                ImGui::EndPopup();
                return;
            }
            ImGui::EndDisabled();

            ImGui::BeginDisabled(!editable ||
                                 candidate_index + 1 >= candidate_paths->size());
            if (ImGui::MenuItem(tr("context.scenario.move_down").c_str())) {
                defer_candidate_action(ScenarioCandidateActionKind::MoveDown,
                                       *candidate_paths, candidate_index);
                ImGui::EndDisabled();
                ImGui::EndPopup();
                return;
            }
            ImGui::EndDisabled();
        }
        ImGui::EndPopup();
    };
    if (ImGui::BeginTable("scenario_file", 2,
                          ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_RowBg |
                          ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn(tr("column.field").c_str(), ImGuiTableColumnFlags_WidthFixed, 106.0f);
        ImGui::TableSetupColumn(tr("column.value").c_str(), ImGuiTableColumnFlags_WidthStretch);
        ImVec4 hovered_path_frame = ImGui::GetStyleColorVec4(ImGuiCol_FrameBg);
        hovered_path_frame.x = clamp_color_component(hovered_path_frame.x * 1.15f);
        hovered_path_frame.y = clamp_color_component(hovered_path_frame.y * 1.15f);
        hovered_path_frame.z = clamp_color_component(hovered_path_frame.z * 1.15f);
        const auto render_path_input = [&](const char* id, std::string& value,
                                           float width, bool allow_edit = true) {
            ImGui::SetNextItemWidth(width);
            const ImVec2 input_min = ImGui::GetCursorScreenPos();
            const ImVec2 input_max(input_min.x + ImGui::CalcItemWidth(),
                                   input_min.y + ImGui::GetFrameHeight());
            const bool hovered = ImGui::IsWindowHovered() &&
                ImGui::IsMouseHoveringRect(input_min, input_max, true);
            if (hovered) ImGui::PushStyleColor(ImGuiCol_FrameBg, hovered_path_frame);
            const bool changed = ImGui::InputText(
                id, &value,
                editable && allow_edit
                    ? ImGuiInputTextFlags_None
                    : ImGuiInputTextFlags_ReadOnly);
            if (hovered) ImGui::PopStyleColor();
            return changed;
        };
        const auto render_value = [&](const char* field, std::string& value,
                                      bool path_context = false, bool image = false,
                                      bool allow_edit = true) {
            ImGui::PushID(field);
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(field);
            ImGui::TableSetColumnIndex(1);
            if (path_context) {
                render_path_input("##value", value, -1.0f, allow_edit);
            } else {
                ImGui::SetNextItemWidth(-1.0f);
                ImGui::InputText("##value", &value,
                                 editable && allow_edit
                                     ? ImGuiInputTextFlags_None
                                     : ImGuiInputTextFlags_ReadOnly);
            }
            if (path_context) {
                render_context_menu("##scenario_path_context", value, image, nullptr, 0);
            }
            ImGui::PopID();
        };
        const auto render_paths = [&](const char* field,
                                      std::vector<ScenarioPreviewPath>& paths) {
            const bool show_weight = paths.size() > 1 || std::any_of(
                paths.begin(), paths.end(), [](const ScenarioPreviewPath& path) {
                    return path.has_explicit_weight;
                });
            if (paths.empty()) {
                std::string empty;
                // An absent Route/Vehicle entry is intentionally not an
                // insertion surface: this editor only changes existing
                // candidates, while missing scalar fields may be filled in.
                render_value(field, empty, false, false, false);
                return;
            }
            ImGui::PushID(field);
            for (size_t index = 0; index < paths.size(); ++index) {
                ScenarioPreviewPath& path = paths[index];
                ImGui::PushID(static_cast<int>(index));
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::AlignTextToFramePadding();
                if (index == 0) ImGui::TextUnformatted(field);
                ImGui::TableSetColumnIndex(1);
                if (!show_weight) {
                    if (render_path_input("##path", path.path, -1.0f)) {
                        update_scenario_route_warning();
                    }
                    render_context_menu("##scenario_path_context", path.path, false,
                                       &paths, index);
                } else {
                    const float weight_width = 72.0f;
                    const float path_width = std::max(
                        80.0f, ImGui::GetContentRegionAvail().x - weight_width -
                                   ImGui::GetStyle().ItemSpacing.x);
                    if (render_path_input("##path", path.path, path_width)) {
                        update_scenario_route_warning();
                    }
                    render_context_menu("##scenario_path_context", path.path, false,
                                       &paths, index);
                    ImGui::SameLine();
                    ImGui::SetNextItemWidth(weight_width);
                    double edited_weight = path.weight;
                    if (ImGui::InputDouble("##weight", &edited_weight, 0.0, 0.0, "%.12g",
                                           editable ? ImGuiInputTextFlags_None : ImGuiInputTextFlags_ReadOnly)) {
                        if (std::isfinite(edited_weight) && edited_weight > 0.0) {
                            path.weight = edited_weight;
                            path.has_explicit_weight = true;
                            update_scenario_route_warning();
                        }
                    }
                }
                ImGui::PopID();
            }
            ImGui::PopID();
        };

        render_value("Title", preview.title);
        render_paths("Route", preview.routes);
        render_value("RouteTitle", preview.route_title);
        render_paths("Vehicle", preview.vehicles);
        render_value("VehicleTitle", preview.vehicle_title);
        render_value("Author", preview.author);
        render_value("Image", preview.image, true, true);
        render_value("Comment", preview.comment);
        ImGui::EndTable();

        if (pending_candidate_paths &&
            pending_candidate_action != ScenarioCandidateActionKind::None) {
            std::vector<ScenarioPreviewPath>& paths = *pending_candidate_paths;
            const size_t index = pending_candidate_index;
            bool changed = false;
            switch (pending_candidate_action) {
            case ScenarioCandidateActionKind::AddAfter:
                if (index < paths.size()) {
                    paths.insert(paths.begin() + static_cast<std::ptrdiff_t>(index + 1),
                                 ScenarioPreviewPath{});
                    changed = true;
                }
                break;
            case ScenarioCandidateActionKind::Delete:
                if (paths.size() > 1 && index < paths.size()) {
                    paths.erase(paths.begin() + static_cast<std::ptrdiff_t>(index));
                    changed = true;
                }
                break;
            case ScenarioCandidateActionKind::MoveUp:
                if (index > 0 && index < paths.size()) {
                    std::swap(paths[index], paths[index - 1]);
                    changed = true;
                }
                break;
            case ScenarioCandidateActionKind::MoveDown:
                if (index + 1 < paths.size()) {
                    std::swap(paths[index], paths[index + 1]);
                    changed = true;
                }
                break;
            case ScenarioCandidateActionKind::None:
                break;
            }
            if (changed && pending_candidate_paths == &preview.routes) {
                update_scenario_route_warning();
            }
        }
    }
    focus_scenario_file_next_ = false;
    ImGui::End();
}

