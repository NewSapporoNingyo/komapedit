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

bool App::render_map_element_field_control(MapElementEditFieldState& field,
                                           float width) {
    ImGui::SetNextItemWidth(width);
    const MapElementNumericChoiceSet options =
        map_element_numeric_choices(field.numeric_constraint);
    if (options.count != 0) {
        const std::string current_value = edit_field_buffer_text(field);
        const int selected_option = gui_numeric_choice_option_index(
            current_value, field.numeric_constraint);
        const auto option_text = [&](size_t option) {
            const MapElementNumericChoice& choice = options.choices[option];
            return std::to_string(choice.value) + ": " + tr(choice.description_key);
        };
        const std::string preview = selected_option >= 0
            ? option_text(static_cast<size_t>(selected_option))
            : current_value;
        if (!ImGui::BeginCombo(field.label.c_str(), preview.c_str())) return false;

        bool input_changed = false;
        for (size_t option = 0; option < options.count; ++option) {
            const bool selected = static_cast<int>(option) == selected_option;
            const std::string display_text = option_text(option);
            if (ImGui::Selectable(display_text.c_str(), selected)) {
                const std::string candidate = std::to_string(options.choices[option].value);
                if (candidate != current_value) {
                    set_edit_field_buffer(field, candidate);
                    input_changed = true;
                }
            }
            if (selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
        return input_changed;
    }
    if (field.key_source == MapElementKeySource::None) {
        return ImGui::InputText(field.label.c_str(), &field.value);
    }

    const std::string current_value = edit_field_buffer_text(field);
    bool input_changed = false;
    if (!ImGui::BeginCombo(field.label.c_str(), current_value.c_str())) {
        return false;
    }

    int option_id = 0;
    auto select_option = [&](const std::string& candidate,
                             const std::string& display_text) {
        const bool selected = field.key_source == MapElementKeySource::Track
            ? track_key_values_equal(current_value, candidate)
            : resource_key_values_equal(current_value, candidate);
        ImGui::PushID(option_id++);
        if (ImGui::Selectable(display_text.c_str(), selected) &&
            candidate != current_value) {
            set_edit_field_buffer(field, candidate);
            input_changed = true;
        }
        if (selected) ImGui::SetItemDefaultFocus();
        ImGui::PopID();
    };
    auto render_table_options = [&](const std::vector<TableRow>& rows,
                                    const std::string& cell_key) {
        for (const TableRow& row : rows) {
            const std::string& candidate = table_cell(row, cell_key);
            if (candidate.empty()) continue;
            select_option(candidate, candidate);
        }
    };

    switch (field.key_source) {
    case MapElementKeySource::Structure:
        render_table_options(model_.structure_models, "structureKey");
        break;
    case MapElementKeySource::Sound:
        render_table_options(model_.sound_list, "soundKey");
        break;
    case MapElementKeySource::Sound3D:
        render_table_options(model_.sound_3d_list, "soundKey");
        break;
    case MapElementKeySource::SignalAspect:
        render_table_options(model_.signal_aspects, "signalAspectKey");
        break;
    case MapElementKeySource::Track: {
        static const std::array<std::string, 3> k_own_track_keys = {
            "0", "''", "'0'"
        };
        for (const std::string& candidate : k_own_track_keys) {
            select_option(candidate, candidate + " " + tr("value.own_track"));
        }
        for (const OtherTrack& track : model_.other_tracks) {
            if (track.key.empty()) continue;
            const bool own_track_key = std::any_of(
                k_own_track_keys.begin(), k_own_track_keys.end(),
                [&](const std::string& candidate) {
                    return track_key_values_equal(track.key, candidate);
                });
            if (!own_track_key) select_option(track.key, track.key);
        }
        break;
    }
    case MapElementKeySource::None:
        break;
    }
    ImGui::EndCombo();
    return input_changed;
}

namespace {

bool optional_insertion_argument_prerequisites_enabled(
    const MapElementInspectorState& inspector, size_t field_index) {
    for (size_t index = 0; index < field_index; ++index) {
        const MapElementEditFieldState& preceding = inspector.fields[index];
        if (preceding.optional_insertion_argument && preceding.disabled) return false;
    }
    return true;
}

} // namespace

void normalize_optional_insertion_argument_enablement(
    MapElementInspectorState& inspector) {
    bool earlier_argument_omitted = false;
    for (MapElementEditFieldState& field : inspector.fields) {
        if (!field.optional_insertion_argument) continue;
        if (earlier_argument_omitted) field.disabled = true;
        earlier_argument_omitted = earlier_argument_omitted || field.disabled;
    }
}

bool set_optional_insertion_argument_enabled(
    MapElementInspectorState& inspector, std::string_view field_key, bool included) {
    const auto found = std::find_if(
        inspector.fields.begin(), inspector.fields.end(),
        [&](const MapElementEditFieldState& field) { return field.key == field_key; });
    if (found == inspector.fields.end() || !found->optional_insertion_argument) {
        return false;
    }
    const size_t field_index = static_cast<size_t>(
        std::distance(inspector.fields.begin(), found));
    if (included &&
        !optional_insertion_argument_prerequisites_enabled(inspector, field_index)) {
        return false;
    }
    found->disabled = !included;
    normalize_optional_insertion_argument_enablement(inspector);
    return true;
}

void App::render_map_element_field_inputs(
    MapElementInspectorState& inspector,
    bool render_optional_insertion_argument_toggles) {
    const bool repeater_inspector = inspector.row_kind == "repeater";
    const bool coordinate_offset_inspector =
        inspector.row_kind == "structure.put" || repeater_inspector;
    const bool section_inspector = inspector.row_kind == "section.begin" ||
        inspector.row_kind == "section.speedLimit";
    for (size_t field_index = 0; field_index < inspector.fields.size(); ++field_index) {
        MapElementEditFieldState& field = inspector.fields[field_index];
        if (repeater_inspector && is_repeater_structure_key_field(field)) {
            continue;
        }
        if (section_inspector && is_section_values_field(field)) {
            continue;
        }
        if (coordinate_offset_inspector && !inspector.coordinate_offsets_enabled &&
            is_coordinate_offset_field(field.key)) {
            continue;
        }
        const bool optional_insertion_argument =
            render_optional_insertion_argument_toggles &&
            field.optional_insertion_argument;
        if (field.key == "structureKey" && field_index > 0) ImGui::Separator();
        if (repeater_inspector && field.key == "repeaterKey") ImGui::Separator();
        const bool changed = edit_field_buffer_text(field) != field.original_value;
        if (changed) ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.28f, 0.23f, 0.08f, 1.0f));
        const float input_width =
            std::max(160.0f, ImGui::GetContentRegionAvail().x * 0.55f);
        ImGui::BeginDisabled(field.read_only || field.disabled);
        const std::string previous_value = edit_field_buffer_text(field);
        const bool input_changed = render_map_element_field_control(field, input_width);
        ImGui::EndDisabled();
        if (input_changed && field.requires_signal_full_form &&
            inspector.source_signal_short_form &&
            !inspector.signal_full_form_conversion_draft) {
            inspector.pending_signal_full_form_field = field.key;
            inspector.pending_signal_full_form_value = edit_field_buffer_text(field);
            set_edit_field_buffer(field, previous_value);
            inspector.signal_full_form_prompt_requested = true;
        }
        if (ImGui::IsItemDeactivatedAfterEdit() &&
            !validate_and_canonicalize_edit_field(field, true)) {
            set_program_status("status.edit.invalid_number");
        }
        if (optional_insertion_argument) {
            ImGui::SameLine();
            bool include_argument = !field.disabled;
            const std::string include_label =
                tr("chk.new_element_include_parameter") +
                "##NewElementOptionalArgument_" + std::to_string(field_index);
            ImGui::BeginDisabled(
                !optional_insertion_argument_prerequisites_enabled(inspector, field_index));
            if (ImGui::Checkbox(include_label.c_str(), &include_argument)) {
                (void)set_optional_insertion_argument_enabled(
                    inspector, field.key, include_argument);
            }
            ImGui::EndDisabled();
        }
        if (changed) ImGui::PopStyleColor();
        if (!field.source_distance_string.empty()) {
            render_inline_wrapped_text(tr("label.source_distance_string").c_str(),
                                       field.source_distance_string);
        }
    }
    if (render_optional_insertion_argument_toggles) {
        normalize_optional_insertion_argument_enablement(inspector);
    }
}

void App::render_repeater_structure_keys_edit_ui(MapElementInspectorState& inspector) {
    ImGui::Separator();
    ImGui::TextUnformatted(tr("label.repeater_structure_keys").c_str());
    ImGui::PushID(&inspector);
    std::vector<size_t> key_field_indices;
    for (size_t index = 0; index < inspector.fields.size(); ++index) {
        if (is_repeater_structure_key_field(inspector.fields[index])) {
            key_field_indices.push_back(index);
        }
    }
    std::optional<size_t> remove_key_index;
    std::optional<size_t> move_key_from;
    std::optional<size_t> move_key_to;
    const float structure_key_input_x = ImGui::GetCursorPosX();
    const float structure_key_input_width =
        std::max(160.0f, ImGui::GetContentRegionAvail().x * 0.42f);
    const float add_structure_key_button_width =
        std::max(80.0f, structure_key_input_width * 0.5f);
    for (size_t list_index = 0; list_index < key_field_indices.size(); ++list_index) {
        MapElementEditFieldState& field = inspector.fields[key_field_indices[list_index]];
        const bool changed = edit_field_buffer_text(field) != field.original_value;
        if (changed) ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.28f, 0.23f, 0.08f, 1.0f));
        render_map_element_field_control(field, structure_key_input_width);
        if (changed) ImGui::PopStyleColor();
        ImGui::SameLine();
        ImGui::BeginDisabled(list_index == 0);
        if (ImGui::SmallButton((u8"\u2191##RepeaterKey" + std::to_string(list_index)).c_str())) {
            move_key_from = list_index;
            move_key_to = list_index - 1;
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(list_index + 1 >= key_field_indices.size());
        if (ImGui::SmallButton((u8"\u2193##RepeaterKey" + std::to_string(list_index)).c_str())) {
            move_key_from = list_index;
            move_key_to = list_index + 1;
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(key_field_indices.size() <= 1);
        if (ImGui::SmallButton(("-##RepeaterKey" + std::to_string(list_index)).c_str())) {
            remove_key_index = list_index;
        }
        ImGui::EndDisabled();
    }
    ImGui::SetCursorPosX(structure_key_input_x +
                         (structure_key_input_width - add_structure_key_button_width) * 0.5f);
    if (ImGui::Button("+##RepeaterKey", ImVec2(add_structure_key_button_width, 0.0f))) {
        inspector.fields.push_back(make_repeater_structure_key_field(
            inspector, key_field_indices.size(), {}));
    }
    if (remove_key_index && *remove_key_index < key_field_indices.size()) {
        inspector.fields.erase(inspector.fields.begin() + static_cast<std::ptrdiff_t>(
            key_field_indices[*remove_key_index]));
    } else if (move_key_from && move_key_to &&
               *move_key_from < key_field_indices.size() &&
               *move_key_to < key_field_indices.size()) {
        std::swap(inspector.fields[key_field_indices[*move_key_from]],
                  inspector.fields[key_field_indices[*move_key_to]]);
    }
    if (remove_key_index || move_key_from) {
        reindex_repeater_structure_key_fields(inspector);
    }
    ImGui::PopID();
}

void App::render_section_values_edit_ui(MapElementInspectorState& inspector) {
    ImGui::Separator();
    ImGui::TextUnformatted(tr("label.section_parameters").c_str());
    std::vector<size_t> value_field_indices;
    for (size_t index = 0; index < inspector.fields.size(); ++index) {
        if (is_section_values_field(inspector.fields[index])) {
            value_field_indices.push_back(index);
        }
    }
    std::optional<size_t> remove_value_index;
    std::optional<size_t> move_value_from;
    std::optional<size_t> move_value_to;
    const float section_value_input_x = ImGui::GetCursorPosX();
    const float section_value_input_width =
        std::max(160.0f, ImGui::GetContentRegionAvail().x * 0.42f);
    const float add_section_value_button_width =
        std::max(80.0f, section_value_input_width * 0.5f);
    for (size_t list_index = 0; list_index < value_field_indices.size(); ++list_index) {
        MapElementEditFieldState& field = inspector.fields[value_field_indices[list_index]];
        const bool changed = edit_field_buffer_text(field) != field.original_value;
        if (changed) ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.28f, 0.23f, 0.08f, 1.0f));
        ImGui::SetNextItemWidth(section_value_input_width);
        ImGui::InputText(field.label.c_str(), &field.value);
        if (changed) ImGui::PopStyleColor();
        ImGui::SameLine();
        ImGui::BeginDisabled(list_index == 0);
        if (ImGui::SmallButton((u8"\u2191##SectionValue" + std::to_string(list_index)).c_str())) {
            move_value_from = list_index;
            move_value_to = list_index - 1;
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(list_index + 1 >= value_field_indices.size());
        if (ImGui::SmallButton((u8"\u2193##SectionValue" + std::to_string(list_index)).c_str())) {
            move_value_from = list_index;
            move_value_to = list_index + 1;
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(value_field_indices.size() <= 1);
        if (ImGui::SmallButton(("-##SectionValue" + std::to_string(list_index)).c_str())) {
            remove_value_index = list_index;
        }
        ImGui::EndDisabled();
    }
    ImGui::SetCursorPosX(section_value_input_x +
                         (section_value_input_width - add_section_value_button_width) * 0.5f);
    if (ImGui::Button("+##SectionValue", ImVec2(add_section_value_button_width, 0.0f))) {
        inspector.fields.push_back(make_section_values_field(
            inspector, value_field_indices.size(), {}));
    }
    if (remove_value_index && *remove_value_index < value_field_indices.size()) {
        inspector.fields.erase(inspector.fields.begin() + static_cast<std::ptrdiff_t>(
            value_field_indices[*remove_value_index]));
    } else if (move_value_from && move_value_to &&
               *move_value_from < value_field_indices.size() &&
               *move_value_to < value_field_indices.size()) {
        std::swap(inspector.fields[value_field_indices[*move_value_from]],
                  inspector.fields[value_field_indices[*move_value_to]]);
    }
    if (remove_value_index || move_value_from) {
        reindex_section_values_fields(inspector);
    }
}

void App::render_element_inspector() {
    if (!edit_mode_enabled_) {
        clear_scene_placement_edit_target();
        inspector_.open = false;
        return;
    }
    if (!inspector_.open) return;
    std::string title = tr("dialog.element_properties") + "###ElementInspector";
    const bool operation_pending = edit_ui_operation_pending();
    bool* inspector_open = operation_pending ? nullptr : &inspector_.open;
    const ImGuiWindowFlags inspector_flags = operation_pending
        ? ImGuiWindowFlags_NoInputs
        : ImGuiWindowFlags_None;
    if (!ImGui::Begin(title.c_str(), inspector_open, inspector_flags)) {
        const bool closed = !inspector_.open;
        ImGui::End();
        if (closed) {
            cancel_distance_resolution_workflow();
            clear_scene_placement_edit_target();
        }
        return;
    }

    ImGui::TextUnformatted(tr("label.source_file").c_str());
    ImGui::SameLine();
    ImGui::TextUnformatted(inspector_.source_file_name.c_str());
    const bool source_file_hovered = ImGui::IsItemHovered();
    render_source_file_context_menu("element_inspector_source_file_context",
                                    inspector_.source_file);
    if (source_file_hovered && !inspector_.source_file.empty() &&
        !ImGui::IsPopupOpen("element_inspector_source_file_context")) {
        ImGui::SetTooltip("%s", inspector_.source_file.c_str());
    }
    ImGui::Text("%s %d:%d", tr("label.source_position").c_str(), inspector_.line, inspector_.column);
    if (!inspector_.raw_statement.empty()) {
        ImGui::Separator();
        ImGui::TextUnformatted(tr("label.raw_statement").c_str());
        ImGui::TextWrapped("%s", inspector_.raw_statement.c_str());
    }
    const bool repeater_inspector = inspector_.row_kind == "repeater";
    bool repeater_navigation_changed = false;
    const auto render_repeater_end_source = [&] {
        if (inspector_.repeater_has_multiple_begins) {
            ImGui::TextUnformatted(tr("status.repeater_multiple_begins").c_str());
            ImGui::BeginDisabled(inspector_.repeater_previous_begin_edit_id.empty());
            if (ImGui::Button(tr("button.previous").c_str())) {
                repeater_navigation_changed = navigate_repeater_inspector(false);
            }
            ImGui::EndDisabled();
            if (repeater_navigation_changed) return;
            ImGui::SameLine();
            ImGui::BeginDisabled(inspector_.repeater_next_begin_edit_id.empty());
            if (ImGui::Button(tr("button.next").c_str())) {
                repeater_navigation_changed = navigate_repeater_inspector(true);
            }
            ImGui::EndDisabled();
            if (repeater_navigation_changed) return;
        }
        if (inspector_.repeater_boundary_kind == "open") {
            if (ImGui::Button(tr("button.add_repeater_end_position").c_str())) {
                open_repeater_end_wizard_from_inspector();
            }
        } else if (!inspector_.end_source_file_name.empty()) {
            ImGui::Text("%s %s %d:%d", tr("label.repeater_end_source").c_str(),
                        inspector_.end_source_file_name.c_str(), inspector_.end_line,
                        inspector_.end_column);
            if (!inspector_.end_raw_statement.empty()) {
                ImGui::TextWrapped("%s", inspector_.end_raw_statement.c_str());
            }
        }
    };

    ImGui::Separator();
    const bool coordinate_offset_inspector =
        inspector_.row_kind == "structure.put" || repeater_inspector;
    if (coordinate_offset_inspector) {
        const char* button_key = inspector_.coordinate_offsets_enabled
            ? "button.remove_coordinate_offsets"
            : "button.add_coordinate_offsets";
        if (ImGui::Button(tr(button_key).c_str())) {
            if (!inspector_.coordinate_offsets_enabled) {
                set_inspector_coordinate_offsets_enabled(inspector_, true);
            } else if (inspector_coordinate_offsets_are_zero(inspector_)) {
                set_inspector_coordinate_offsets_enabled(inspector_, false);
            } else {
                inspector_.coordinate_offset_discard_prompt_requested = true;
            }
        }
        if (repeater_inspector) {
            ImGui::SameLine();
            if (ImGui::Button(tr("button.insert_repeater_change_point").c_str())) {
                open_repeater_change_point_wizard_from_inspector();
            }
        }
        ImGui::Separator();
    }
    const bool section_inspector = inspector_.row_kind == "section.begin" ||
        inspector_.row_kind == "section.speedLimit";
    render_map_element_field_inputs(inspector_);
    if (repeater_inspector) {
        ImGui::Separator();
        render_repeater_end_source();
        if (repeater_navigation_changed) {
            ImGui::End();
            return;
        }
    }
    if (repeater_inspector) {
        render_repeater_structure_keys_edit_ui(inspector_);
    }
    if (section_inspector) {
        render_section_values_edit_ui(inspector_);
    }
    if (ImGui::Button(tr("button.apply").c_str())) {
        request_edit_ui_operation(PendingEditUiOperation::ApplyInspector);
    }
    ImGui::SameLine();
    if (ImGui::Button(tr("button.close").c_str())) {
        cancel_distance_resolution_workflow();
        clear_scene_placement_edit_target();
        inspector_.open = false;
    }

    ImGui::End();
    if (!inspector_.open &&
        (distance_resolution_workflow_.phase != DistanceResolutionPhase::None ||
         distance_resolution_workflow_.retry_requested)) {
        cancel_distance_resolution_workflow();
    }
    if (!inspector_.open) clear_scene_placement_edit_target();
}
