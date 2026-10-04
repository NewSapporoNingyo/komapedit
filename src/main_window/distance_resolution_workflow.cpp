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

namespace {

std::string distance_resolution_reason_description(std::string_view reason) {
    if (reason == "ambiguous_Source_Section") {
        return "the surrounding distance anchors do not define one unambiguous monotonic source section";
    }
    if (reason == "multiple_Equivalent_Distance_Blocks") {
        return "more than one distance block in the source section has the target distance";
    }
    if (reason == "no_Unique_Distance_Bracket") {
        return "the target distance is neither one unique existing block nor inside one unique adjacent distance bracket";
    }
    if (reason == "distance_Expression_Requires_Manual_Edit") {
        return "a safe common target-distance expression cannot be derived automatically";
    }
    if (reason == "variable_Has_Multiple_Context_Values") {
        return "a referenced variable has multiple values or availability states in the candidate source section";
    }
    if (reason == "incompatible_Evaluation_Environment") {
        return "the statement or distance expression would be evaluated in an incompatible destination environment";
    }
    if (reason == "physical_Source_Has_Incompatible_Include_Contexts") {
        return "the same physical source statement is used by Include contexts that cannot share one safe placement";
    }
    if (reason == "evaluation_Environment_Requires_Boundary") {
        return "another source boundary is required to preserve statement evaluation and Include contexts";
    }
    if (reason == "stale_Distance_Resolution") {
        return "the submitted manual resolution belongs to a different or recomputed distance-edit group";
    }
    if (reason == "conflicting_Manual_Boundaries") {
        return "members of the same distance-edit group specify different manual source boundaries";
    }
    if (reason == "conflicting_Manual_Distance_Expressions") {
        return "members of the same distance-edit group specify different manual distance expressions";
    }
    if (reason == "stale_Distance_Boundary") {
        return "the selected source boundary is no longer valid for the current parsed source";
    }
    if (reason == "multiple_Distance_Brackets") {
        return "more than one adjacent source-distance interval brackets the target distance";
    }
    if (reason == "destination_Boundary_Unavailable") {
        return "the selected destination has no valid parser-approved insertion boundary";
    }
    return "the backend returned an unknown distance-resolution reason";
}

bool distance_request_needs_expression(const DistanceResolutionRequest& request) {
    // A variable name is diagnostic context, not an instruction to edit the
    // distance expression. Statement-argument conflicts require a boundary.
    return request.reason == "distance_Expression_Requires_Manual_Edit" ||
        request.reason == "conflicting_Manual_Distance_Expressions" ||
        request.reason == "variable_Has_Multiple_Context_Values" ||
        request.reason == "incompatible_Evaluation_Environment";
}

std::string trimmed_distance_expression(std::string_view expression) {
    while (!expression.empty() &&
           std::isspace(static_cast<unsigned char>(expression.front()))) {
        expression.remove_prefix(1);
    }
    while (!expression.empty() &&
           std::isspace(static_cast<unsigned char>(expression.back()))) {
        expression.remove_suffix(1);
    }
    return std::string(expression);
}

void append_distance_key_field(std::ostringstream& key, std::string_view value) {
    key << value.size() << ':' << value;
}

std::string distance_choice_key(const DistanceResolutionChoice& choice) {
    std::ostringstream key;
    append_distance_key_field(key, choice.boundary_token);
    append_distance_key_field(key, trimmed_distance_expression(choice.distance_expression));
    key << (choice.confirm_environment_mismatch ? '1' : '0');
    return key.str();
}

std::set<std::string> distance_workflow_edit_ids(
    const DistanceResolutionWorkflowState& workflow) {
    std::set<std::string> result(workflow.request.affected_edit_ids.begin(),
                                 workflow.request.affected_edit_ids.end());
    for (const auto& entry : workflow.candidate_changes) {
        if (entry.second.distance_resolution_key == workflow.request.resolution_key) {
            result.insert(entry.first);
        }
    }
    const bool has_target = std::any_of(result.begin(), result.end(),
        [&](const std::string& id) { return workflow.candidate_changes.count(id) != 0; });
    if (!has_target && !workflow.origin_edit_id.empty()) result.insert(workflow.origin_edit_id);
    return result;
}

DistanceResolutionChoice combined_distance_choice(
    const DistanceResolutionWorkflowState& workflow,
    const std::map<std::string, DistanceResolutionChoice>& cached_choices,
    const DistanceResolutionChoice& choice) {
    DistanceResolutionChoice combined;
    const auto cached = cached_choices.find(workflow.request.resolution_key);
    if (cached != cached_choices.end()) {
        combined = cached->second;
    } else {
        // A rejected cache entry may have been removed while the current batch
        // still carries the boundary needed to correct its expression.
        for (const std::string& id : workflow.request.affected_edit_ids) {
            const auto found = workflow.candidate_changes.find(id);
            if (found == workflow.candidate_changes.end() ||
                found->second.distance_resolution_key != workflow.request.resolution_key) {
                continue;
            }
            const MapElementPendingChange& value = found->second;
            combined = {value.distance_boundary_token, value.distance_expression,
                        value.confirm_environment_mismatch};
            break;
        }
    }
    const std::string expression = trimmed_distance_expression(choice.distance_expression);
    if (!expression.empty()) combined.distance_expression = expression;
    if (!choice.boundary_token.empty()) {
        combined.boundary_token = choice.boundary_token;
        combined.confirm_environment_mismatch = false;
    }
    if (choice.confirm_environment_mismatch) {
        combined.confirm_environment_mismatch = true;
        combined.boundary_token.clear();
    }
    return combined;
}

std::string distance_attempt_context(const DistanceResolutionWorkflowState& workflow) {
    std::ostringstream key;
    append_distance_key_field(key, workflow.request.resolution_key);
    key << (distance_request_needs_expression(workflow.request) ? 'E' : 'B');
    const std::set<std::string> group_ids = distance_workflow_edit_ids(workflow);
    for (const auto& entry : workflow.candidate_changes) {
        if (group_ids.count(entry.first) != 0) continue;
        append_distance_key_field(key, entry.first);
        append_distance_key_field(key, entry.second.distance_resolution_key);
        append_distance_key_field(key, distance_choice_key({
            entry.second.distance_boundary_token, entry.second.distance_expression,
            entry.second.confirm_environment_mismatch}));
    }
    return key.str();
}

void append_distance_resolution_list(std::ostringstream& message,
                                     std::string_view name,
                                     const std::vector<std::string>& values) {
    if (values.empty()) return;
    message << "; " << name << '=';
    for (size_t index = 0; index < values.size(); ++index) {
        if (index != 0) message << " -> ";
        message << values[index];
    }
}

std::string format_distance_resolution_log(const DistanceResolutionRequest& request) {
    std::ostringstream message;
    message << "Automatic map-statement placement requires manual input: reason="
            << (request.reason.empty() ? "<missing>" : request.reason)
            << " (" << distance_resolution_reason_description(request.reason) << ')';
    if (!request.source_file.empty()) message << "; source=" << request.source_file;
    if (!request.target_distance.empty()) {
        message << "; targetDistance=" << request.target_distance;
    }
    if (request.source_section_first_line > 0 &&
        request.source_section_last_line >= request.source_section_first_line) {
        message << "; sectionLines=" << request.source_section_first_line;
        if (request.source_section_last_line != request.source_section_first_line) {
            message << '-' << request.source_section_last_line;
        }
    }
    if (!request.source_section_direction.empty()) {
        message << "; direction=" << request.source_section_direction;
    }
    if (!request.variable_name.empty()) message << "; variable=" << request.variable_name;
    append_distance_resolution_list(message, "includeStack", request.include_stack);
    append_distance_resolution_list(message, "affectedEditIds", request.affected_edit_ids);
    return message.str();
}

}  // namespace

void App::prepare_distance_resolution_operation(
    const std::map<std::string, MapElementPendingChange>& changes) {
    std::ostringstream key;
    append_distance_key_field(key, file_path_);
    std::map<std::string, std::string> source_hashes;
    for (const EditSourceFileInfo& file : model_.edit_files) {
        source_hashes.emplace(file.file_path, file.source_hash);
    }
    key << source_hashes.size() << ':';
    for (const auto& source : source_hashes) {
        append_distance_key_field(key, source.first);
        append_distance_key_field(key, source.second);
    }
    key << changes.size() << ':';
    for (const auto& entry : changes) {
        const MapElementPendingChange& change = entry.second;
        append_distance_key_field(key, entry.first);
        append_distance_key_field(key, change.edit_id);
        append_distance_key_field(key, change.operation);
        append_distance_key_field(key, change.row_kind);
        append_distance_key_field(key, change.target_file_path);
        append_distance_key_field(key, change.expected_source_hash);
        append_distance_key_field(key, change.replacement_statement);
        append_distance_key_field(key, change.insert_before_edit_id);
        append_distance_key_field(key, change.repeater_pair_id);
        append_distance_key_field(key, std::to_string(change.source_insert_order));
        key << (change.confirm_repeater_change_point ? '1' : '0');
        key << change.field_changes.size() << ':';
        for (const auto& field : change.field_changes) {
            append_distance_key_field(key, field.first);
            append_distance_key_field(key, field.second);
        }
    }
    const std::string operation_key = key.str();
    if (distance_resolution_attempt_history_.operation_key == operation_key) return;
    // Keep prepopulated cache choices for the first request (including the
    // existing cached-choice headless contract), but never carry them across
    // changes to an established source/batch operation.
    if (!distance_resolution_attempt_history_.operation_key.empty()) {
        distance_resolution_choices_.clear();
    }
    distance_resolution_attempt_history_ = DistanceResolutionAttemptHistory{};
    distance_resolution_attempt_history_.operation_key = operation_key;
}

bool App::distance_resolution_choice_rejected(const DistanceResolutionChoice& choice) const {
    const auto history = distance_resolution_attempt_history_.rejected_choices.find(
        distance_resolution_workflow_.attempt_context);
    if (history == distance_resolution_attempt_history_.rejected_choices.end()) return false;
    const DistanceResolutionChoice combined = combined_distance_choice(
        distance_resolution_workflow_, distance_resolution_choices_, choice);
    return history->second.count(distance_choice_key(combined)) != 0;
}

void App::begin_distance_resolution_workflow(
    const std::map<std::string, MapElementPendingChange>& changes,
    std::optional<MapElementInspectorRequest> reload_request,
    bool applying_delete,
    std::string origin_edit_id,
    const std::vector<DistanceResolutionRequest>& requests) {
    if (requests.empty()) return;

    DistanceResolutionWorkflowState workflow;
    workflow.request = requests.front();
    workflow.candidate_changes = changes;
    workflow.reload_request = std::move(reload_request);
    workflow.applying_delete = applying_delete;
    workflow.origin_edit_id = std::move(origin_edit_id);
    if (workflow.origin_edit_id.empty()) {
        for (const std::string& edit_id : workflow.request.affected_edit_ids) {
            if (workflow.candidate_changes.find(edit_id) != workflow.candidate_changes.end()) {
                workflow.origin_edit_id = edit_id;
                break;
            }
        }
    }

    if (workflow.origin_edit_id.empty()) {
        for (const auto& item : workflow.candidate_changes) {
            if (item.second.field_changes.find("distance") != item.second.field_changes.end() &&
                item.second.distance_resolution_key.empty()) {
                workflow.origin_edit_id = item.first;
                break;
            }
        }
    }

    workflow.attempt_context = distance_attempt_context(workflow);
    const std::string expression = workflow.request.suggested_expression;
    const size_t expression_size = std::min(
        expression.size(), workflow.expression_buffer.size() - 1);
    if (expression_size > 0) {
        std::memcpy(workflow.expression_buffer.data(), expression.data(), expression_size);
    }
    workflow.expression_buffer[expression_size] = '\0';
    distance_resolution_workflow_ = std::move(workflow);

    auto cached = distance_resolution_choices_.find(
        distance_resolution_workflow_.request.resolution_key);
    if (cached != distance_resolution_choices_.end()) {
        const bool needs_expression =
            distance_request_needs_expression(distance_resolution_workflow_.request);
        const bool cached_satisfies_request = needs_expression
            ? !cached->second.distance_expression.empty()
            : (!cached->second.boundary_token.empty() ||
               (distance_resolution_workflow_.request.can_confirm_reuse &&
                cached->second.confirm_environment_mismatch));
        if (cached_satisfies_request) {
            size_t target_count = 0;
            size_t matching_count = 0;
            auto count_target = [&](const std::string& edit_id) {
                auto change = distance_resolution_workflow_.candidate_changes.find(edit_id);
                if (change == distance_resolution_workflow_.candidate_changes.end()) return;
                ++target_count;
                const MapElementPendingChange& value = change->second;
                if (value.distance_resolution_key !=
                    distance_resolution_workflow_.request.resolution_key) {
                    return;
                }
                const bool matches = needs_expression
                    ? value.distance_expression == cached->second.distance_expression
                    : (!cached->second.boundary_token.empty()
                       ? value.distance_boundary_token == cached->second.boundary_token
                       : value.confirm_environment_mismatch ==
                         cached->second.confirm_environment_mismatch);
                if (matches) ++matching_count;
            };
            for (const std::string& edit_id :
                 distance_resolution_workflow_.request.affected_edit_ids) {
                count_target(edit_id);
            }
            if (target_count == 0 && !distance_resolution_workflow_.origin_edit_id.empty()) {
                count_target(distance_resolution_workflow_.origin_edit_id);
            }
            if (target_count != 0 && matching_count != target_count &&
                !distance_resolution_choice_rejected(cached->second)) {
                if (apply_distance_resolution_choice(cached->second)) return;
            }

            DistanceResolutionChoice remaining = cached->second;
            if (needs_expression) {
                remaining.distance_expression.clear();
            } else {
                remaining.boundary_token.clear();
                remaining.confirm_environment_mismatch = false;
            }
            if (remaining.boundary_token.empty() && remaining.distance_expression.empty() &&
                !remaining.confirm_environment_mismatch) {
                distance_resolution_choices_.erase(cached);
            } else {
                cached->second = std::move(remaining);
            }
        }
    }

    KME_ADD_LOG(LogSeverity::Warning,
                format_distance_resolution_log(distance_resolution_workflow_.request));
    const bool needs_expression =
        distance_request_needs_expression(distance_resolution_workflow_.request);
    if (distance_resolution_workflow_.candidate_changes.empty() ||
        (!needs_expression &&
         distance_resolution_workflow_.request.allowed_boundaries.empty() &&
         !distance_resolution_workflow_.request.can_confirm_reuse)) {
        KME_ADD_LOG("[error]distance resolution has no actionable source choice: reason=" +
                    distance_resolution_workflow_.request.reason);
        distance_resolution_workflow_ = DistanceResolutionWorkflowState{};
        text_preview_.placement = TextPreviewPlacementState{};
        set_program_status("status.edit.distance_resolution_blocked");
        return;
    }
    distance_resolution_workflow_.phase = needs_expression
        ? DistanceResolutionPhase::EditExpression
        : DistanceResolutionPhase::ConfirmAction;
    distance_resolution_workflow_.popup_requested = true;
    if (inspector_.open) {
        set_program_status("status.edit.distance_resolution_required");
    }
}

bool App::apply_distance_resolution_choice(const DistanceResolutionChoice& choice) {
    DistanceResolutionWorkflowState& workflow = distance_resolution_workflow_;
    if (workflow.request.resolution_key.empty() || workflow.candidate_changes.empty()) return false;
    if (choice.boundary_token.empty() &&
        trimmed_distance_expression(choice.distance_expression).empty() &&
        !choice.confirm_environment_mismatch) return false;
    if (distance_resolution_choice_rejected(choice)) {
        set_program_status("status.edit.distance_choice_rejected");
        return false;
    }
    const DistanceResolutionChoice combined = combined_distance_choice(
        workflow, distance_resolution_choices_, choice);
    bool applied = false;
    auto apply_to_edit = [&](const std::string& edit_id) {
        auto change = workflow.candidate_changes.find(edit_id);
        if (change == workflow.candidate_changes.end()) return;
        MapElementPendingChange& value = change->second;
        value.distance_resolution_key = workflow.request.resolution_key;
        value.distance_boundary_token = combined.boundary_token;
        value.distance_expression = combined.distance_expression;
        value.confirm_environment_mismatch = combined.confirm_environment_mismatch;
        applied = true;
    };
    for (const std::string& edit_id : workflow.request.affected_edit_ids) {
        apply_to_edit(edit_id);
    }
    for (auto& item : workflow.candidate_changes) {
        if (item.second.distance_resolution_key == workflow.request.resolution_key) {
            apply_to_edit(item.first);
        }
    }
    if (!applied && !workflow.origin_edit_id.empty()) apply_to_edit(workflow.origin_edit_id);
    if (!applied) {
        KME_ADD_LOG("[error]distance resolution does not reference a change in the current batch");
        set_program_status("status.edit.distance_resolution_blocked");
        return false;
    }

    distance_resolution_choices_[workflow.request.resolution_key] = combined;
    workflow.submitted_choice_key = distance_choice_key(combined);

    text_preview_.placement = TextPreviewPlacementState{};
    workflow.phase = DistanceResolutionPhase::None;
    workflow.popup_requested = false;
    workflow.retry_requested = true;
    return true;
}

void App::select_distance_resolution_boundary(const std::string& token) {
    if (distance_resolution_workflow_.phase != DistanceResolutionPhase::SelectBoundary) return;
    const auto& boundaries = distance_resolution_workflow_.request.allowed_boundaries;
    const auto selected = std::find_if(
        boundaries.begin(), boundaries.end(),
        [&](const DistanceResolutionBoundary& boundary) { return boundary.token == token; });
    if (selected == boundaries.end()) return;
    text_preview_.placement.selected_boundary_token = token;
}

void App::confirm_distance_resolution_boundary() {
    if (distance_resolution_workflow_.phase != DistanceResolutionPhase::SelectBoundary) return;
    const std::string token = text_preview_.placement.selected_boundary_token;
    if (token.empty()) return;
    const auto& boundaries = distance_resolution_workflow_.request.allowed_boundaries;
    const bool allowed = std::any_of(
        boundaries.begin(), boundaries.end(),
        [&](const DistanceResolutionBoundary& boundary) { return boundary.token == token; });
    if (!allowed) return;
    DistanceResolutionChoice choice;
    choice.boundary_token = token;
    apply_distance_resolution_choice(choice);
}

void App::cancel_distance_resolution_workflow() {
    const bool had_active_workflow =
        distance_resolution_workflow_.phase != DistanceResolutionPhase::None ||
        distance_resolution_workflow_.retry_requested;
    if (!distance_resolution_workflow_.request.resolution_key.empty()) {
        distance_resolution_choices_.erase(
            distance_resolution_workflow_.request.resolution_key);
    }
    text_preview_.placement = TextPreviewPlacementState{};
    distance_resolution_workflow_ = DistanceResolutionWorkflowState{};
    distance_resolution_attempt_history_ = DistanceResolutionAttemptHistory{};
    if (had_active_workflow && inspector_.open) {
        set_program_status("status.edit.distance_resolution_cancelled");
    }
}

void App::process_distance_resolution_retry() {
    if (!distance_resolution_workflow_.retry_requested) return;
    DistanceResolutionWorkflowState workflow = std::move(distance_resolution_workflow_);
    distance_resolution_workflow_ = DistanceResolutionWorkflowState{};
    const bool finish_repeater_end_wizard =
        new_element_wizard_.open &&
        new_element_wizard_.close_after_successful_apply &&
        workflow.reload_request &&
        new_element_wizard_.return_inspector_request &&
        workflow.reload_request->edit_id ==
            new_element_wizard_.return_inspector_request->edit_id;
    const bool applied = apply_edit_ledger_to_preview(
        workflow.candidate_changes, std::move(workflow.reload_request),
        workflow.applying_delete, std::move(workflow.origin_edit_id));
    if (applied) {
        if (finish_repeater_end_wizard) finish_new_element_wizard_after_successful_apply();
        return;
    }

    const DistanceResolutionWorkflowState& next = distance_resolution_workflow_;
    const bool hard_failure = next.phase == DistanceResolutionPhase::None &&
        !next.retry_requested;
    const bool same_action = next.request.resolution_key == workflow.request.resolution_key &&
        distance_request_needs_expression(next.request) ==
            distance_request_needs_expression(workflow.request);
    if ((hard_failure || same_action) && !workflow.submitted_choice_key.empty()) {
        distance_resolution_attempt_history_.rejected_choices[workflow.attempt_context]
            .insert(workflow.submitted_choice_key);
        if (hard_failure) {
            // A failed manual expression must never remain a hidden automatic
            // retry on the user's next explicit Apply. The Inspector draft is
            // untouched and can start a corrected source-choice workflow.
            distance_resolution_choices_.erase(workflow.request.resolution_key);
            set_program_status("status.edit.distance_resolution_blocked");
        } else if (distance_resolution_workflow_.retry_requested) {
            distance_resolution_workflow_.retry_requested = false;
            distance_resolution_workflow_.phase =
                distance_request_needs_expression(next.request)
                    ? DistanceResolutionPhase::EditExpression
                    : DistanceResolutionPhase::ConfirmAction;
            distance_resolution_workflow_.popup_requested = true;
        }
    }
}
