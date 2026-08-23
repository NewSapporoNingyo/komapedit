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
            !distance_resolution_workflow_.request.variable_name.empty() ||
            distance_resolution_workflow_.request.reason ==
                "distanceExpressionRequiresManualEdit" ||
            distance_resolution_workflow_.request.reason ==
                "conflictingManualDistanceExpressions";
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
            if (target_count == 0 || matching_count != target_count) {
                apply_distance_resolution_choice(cached->second);
                return;
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

    const bool needs_expression =
        !distance_resolution_workflow_.request.variable_name.empty() ||
        distance_resolution_workflow_.request.reason ==
            "distanceExpressionRequiresManualEdit" ||
        distance_resolution_workflow_.request.reason ==
            "conflictingManualDistanceExpressions";
    distance_resolution_workflow_.phase = needs_expression
        ? DistanceResolutionPhase::EditExpression
        : DistanceResolutionPhase::ConfirmAction;
    distance_resolution_workflow_.popup_requested = true;
    if (inspector_.open) {
        set_program_status("status.edit.distance_resolution_required");
    }
}

void App::apply_distance_resolution_choice(const DistanceResolutionChoice& choice) {
    DistanceResolutionWorkflowState& workflow = distance_resolution_workflow_;
    if (workflow.request.resolution_key.empty() || workflow.candidate_changes.empty()) return;

    DistanceResolutionChoice combined;
    auto cached = distance_resolution_choices_.find(workflow.request.resolution_key);
    if (cached != distance_resolution_choices_.end()) combined = cached->second;
    if (!choice.distance_expression.empty()) {
        combined.distance_expression = choice.distance_expression;
    }
    if (!choice.boundary_token.empty()) {
        combined.boundary_token = choice.boundary_token;
        combined.confirm_environment_mismatch = false;
    }
    if (choice.confirm_environment_mismatch) {
        combined.confirm_environment_mismatch = true;
        combined.boundary_token.clear();
    }
    distance_resolution_choices_[workflow.request.resolution_key] = combined;
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
        for (auto& item : workflow.candidate_changes) {
            if (item.second.field_changes.find("distance") == item.second.field_changes.end()) continue;
            apply_to_edit(item.first);
            break;
        }
    }

    text_preview_.placement = TextPreviewPlacementState{};
    workflow.phase = DistanceResolutionPhase::None;
    workflow.popup_requested = false;
    workflow.retry_requested = applied;
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
    if (apply_edit_ledger_to_preview(workflow.candidate_changes,
                                     std::move(workflow.reload_request),
                                     workflow.applying_delete,
                                     std::move(workflow.origin_edit_id)) &&
        finish_repeater_end_wizard) {
        finish_new_element_wizard_after_successful_apply();
    }
}
