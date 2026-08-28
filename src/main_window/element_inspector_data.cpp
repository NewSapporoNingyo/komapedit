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

void App::request_element_inspector(const std::string& edit_id, const std::string& row_kind) {
    if (!edit_actions_available()) {
        if (edit_mode_enabled_ && !edit_registry_loaded_) {
            KME_ADD_LOG("[info]edit metadata is still loading");
        }
        return;
    }
    if (edit_id.empty()) return;
    pending_inspector_request_ = MapElementInspectorRequest{edit_id, row_kind};
}

void App::process_pending_element_inspector() {
    if (!pending_inspector_request_) return;
    MapElementInspectorRequest request = std::move(*pending_inspector_request_);
    pending_inspector_request_.reset();
    open_element_inspector(request);
}

bool row_kind_supports_delete(const std::string& row_kind) {
    return row_kind == "structure.model" || row_kind == "structure.put" ||
        row_kind == "structure.between" || row_kind == "station.put" ||
        row_kind == "signal.put" || row_kind == "repeater" ||
        row_kind == "irregularity.change" ||
        row_kind == "beacon.put" || row_kind == "mapSound.play" ||
        row_kind == "mapSound3D.put" || row_kind == "rollingNoise.change" ||
        row_kind == "flangeNoise.change" || row_kind == "jointNoise.play" ||
        row_kind == "background.change" || row_kind == "adhesion.change" ||
        row_kind == "cabIlluminance.change" || row_kind == "fog.change" ||
        row_kind == "light.ambient" || row_kind == "light.diffuse" ||
        row_kind == "light.direction" ||
        row_kind == "drawDistance.change" || row_kind == "speedlimit" ||
        row_kind == "section.begin" || row_kind == "section.speedLimit" ||
        row_kind == "curve" || row_kind == "gradient" ||
        row_kind == "otherTrack.change" || row_kind == "include";
}

std::optional<RepeaterDeleteChain> repeater_delete_chain_for_edit_id(
    const std::vector<TableRow>& rows, const std::string& edit_id) {
    const repeater_linkage::Linkage linkage =
        repeater_linkage::pair_linkage(table_repeater_events(rows));
    for (const repeater_linkage::Segment& segment : linkage.segments) {
        if (segment.begin_source_index >= rows.size() ||
            rows[segment.begin_source_index].edit_id != edit_id ||
            segment.chain_index >= linkage.chains.size()) {
            continue;
        }
        const repeater_linkage::Chain& chain = linkage.chains[segment.chain_index];
        RepeaterDeleteChain result;
        result.begin_source_indices = chain.begin_source_indices;
        result.end_source_index = chain.end_source_index;
        result.selected_begin_index = segment.chain_begin_index;
        return result;
    }
    return std::nullopt;
}

void App::request_element_delete(const std::string& edit_id, const std::string& row_kind,
                                 RepeaterDeleteMode repeater_mode) {
    if (!edit_actions_available() || edit_id.empty() || !row_kind_supports_delete(row_kind)) return;
    pending_delete_request_ = MapElementDeleteRequest{edit_id, row_kind, repeater_mode};
}

void App::process_pending_element_delete() {
    if (!pending_delete_request_) return;
    MapElementDeleteRequest request = std::move(*pending_delete_request_);
    pending_delete_request_.reset();
    delete_element_target(request);
}

void App::request_include_file_change(const std::string& edit_id,
                                      const std::string& parent_file_path,
                                      const std::string& node_absolute_path) {
    if (!edit_actions_available() || edit_id.empty()) return;
    pending_include_file_change_request_ = IncludeFileChangeRequest{
        edit_id, parent_file_path, node_absolute_path};
}

void App::request_include_file_insert(const std::string& target_file_path,
                                      bool create_new_file) {
    if (!edit_actions_available() || target_file_path.empty()) return;
    pending_include_file_insert_request_ = IncludeFileInsertRequest{
        target_file_path, create_new_file};
}

void App::process_pending_include_file_change() {
    if (!pending_include_file_change_request_) return;
    IncludeFileChangeRequest request =
        std::move(*pending_include_file_change_request_);
    pending_include_file_change_request_.reset();
    if (!edit_actions_available() || !has_model_) return;

    const EditStatementInfo* statement = nullptr;
    for (const EditStatementInfo& item : model_.edit_statements) {
        if (item.edit_id == request.edit_id && item.statement_kind == "Include") {
            statement = &item;
            break;
        }
    }
    if (!statement || statement->source.file_path.empty()) return;

    const std::string initial_directory = list_asset_picker_initial_directory(
        request.node_absolute_path, model_.path);
    const std::string selected_file =
        open_include_file_dialog(initial_directory);
    if (selected_file.empty()) return;

    // Include arguments resolve against the entry map directory in the
    // parser, so that directory is also the relative-path base here.
    ListAssetSourcePathResult selected_path =
        make_list_asset_source_path(model_.path, selected_file);
    if (selected_path.source_path.empty()) {
        KME_ADD_LOG("[warn]failed to derive an include path for the selected file: selected=\"" +
                selected_path.resolved_path + "\"");
        set_program_status("status.edit.required_field");
        return;
    }
    if (selected_path.source_path == statement->first_evaluated_value) return;

    std::string source_hash;
    for (const EditSourceFileInfo& file : model_.edit_files) {
        if (file.file_path == statement->source.file_path) {
            source_hash = file.source_hash;
            break;
        }
    }

    std::map<std::string, MapElementPendingChange> candidate = pending_edit_changes_;
    MapElementPendingChange change;
    change.change_id = "change-" + request.edit_id;
    change.edit_id = request.edit_id;
    change.row_kind = "include";
    change.operation = "update";
    change.field_changes.emplace("includePath", selected_path.source_path);
    change.expected_source_hash = expected_source_hash_for_edit_target(
        model_, pending_edit_changes_, request.edit_id, source_hash,
        statement->source.file_path);
    candidate[request.edit_id] = std::move(change);

    if (!apply_edit_ledger_to_preview(candidate, std::nullopt, true)) return;
    if (!selected_path.fallback_reason.empty()) {
        KME_ADD_LOG(
            LogSeverity::Warning,
            "[warning]unable to create a relative include path; "
            "using absolute path: reason=\"" + selected_path.fallback_reason +
            "\", selected=\"" + selected_path.resolved_path +
            "\", map=\"" + model_.path + "\"");
        set_program_status("status.edit.relative_path_fallback");
    } else {
        set_program_status("status.edit.pending");
    }
}

void App::request_other_track_rename(const std::string& track_key) {
    if (!edit_actions_available() || track_key.empty()) return;
    pending_other_track_rename_request_ = track_key;
}

void App::process_pending_other_track_rename() {
    if (!pending_other_track_rename_request_) return;
    std::string source_key = std::move(*pending_other_track_rename_request_);
    pending_other_track_rename_request_.reset();
    const bool has_editable_row = std::any_of(
        model_.other_track_changes.begin(), model_.other_track_changes.end(),
        [&](const TableRow& row) {
            return !row.edit_id.empty() &&
                resource_key_values_equal(table_cell(row, "trackKey"), source_key);
        });
    if (!has_editable_row) return;
    other_track_rename_ = OtherTrackRenameState{};
    other_track_rename_.source_key = std::move(source_key);
    other_track_rename_.draft_key = other_track_rename_.source_key;
    other_track_rename_.popup_requested = true;
}

const EditSourceFileInfo* find_model_source_file(const MapModel& model, const std::string& path) {
    for (const EditSourceFileInfo& file : model.edit_files) {
        if (file.file_path == path) return &file;
    }
    return nullptr;
}

std::optional<ResourceListKind> resource_list_kind_for_new_file(NewFileKind kind) {
    switch (kind) {
    case NewFileKind::Structure: return ResourceListKind::Structure;
    case NewFileKind::Signal: return ResourceListKind::Signal;
    case NewFileKind::Sound: return ResourceListKind::Sound;
    case NewFileKind::Sound3D: return ResourceListKind::Sound3D;
    case NewFileKind::Station: return ResourceListKind::Station;
    case NewFileKind::Map: return std::nullopt;
    }
    return std::nullopt;
}

const char* resource_list_name_translation_key(ResourceListKind kind) {
    switch (kind) {
    case ResourceListKind::Station: return "resource_list.name.station";
    case ResourceListKind::Structure: return "resource_list.name.structure";
    case ResourceListKind::Signal: return "resource_list.name.signal";
    case ResourceListKind::Sound: return "resource_list.name.sound";
    case ResourceListKind::Sound3D: return "resource_list.name.sound3d";
    case ResourceListKind::Count: return "";
    }
    return "";
}

bool new_file_resource_list_is_already_referenced(
    const MapModel& model,
    const std::map<std::string, MapElementPendingChange>& changes,
    NewFileKind kind) {
    const std::optional<ResourceListKind> resource_kind = resource_list_kind_for_new_file(kind);
    if (!resource_kind) return false;
    const size_t index = static_cast<size_t>(*resource_kind);
    if (index < model.resource_list_sources.size() && model.resource_list_sources[index].present) {
        return true;
    }
    const std::string kind_name(new_file_resource_list_kind(kind));
    return std::any_of(changes.begin(), changes.end(), [&](const auto& item) {
        const MapElementPendingChange& change = item.second;
        const auto field = change.field_changes.find("resourceListKind");
        return change.operation == "insert" && change.row_kind == "resourceList.load" &&
            field != change.field_changes.end() && field->second == kind_name;
    });
}

bool new_file_name_is_safe(const std::string& file_name) {
    if (file_name.empty() || file_name == "." || file_name == "..") return false;
    for (const unsigned char ch : file_name) {
        if (ch < 0x20 || ch == '<' || ch == '>' || ch == ':' || ch == '"' ||
            ch == '/' || ch == '\\' || ch == '|' || ch == '?' || ch == '*') {
            return false;
        }
    }
    const std::filesystem::path path(utf8_to_wide(file_name));
    return !path.empty() && path.filename() == path && !path.has_root_path();
}

bool App::stage_new_file_reference(NewFileKind kind,
                                   const std::string& target_file_path,
                                   const std::string& selected_file) {
    if (!edit_actions_available() || !has_model_) {
        set_program_status("status.new_file.reference_requires_edit");
        return false;
    }
    if (!find_model_source_file(model_, target_file_path)) {
        KME_ADD_LOG("[warn]new-file reference target is not part of the loaded map: " +
                target_file_path);
        return false;
    }
    if (new_file_resource_list_is_already_referenced(model_, pending_edit_changes_, kind)) {
        set_program_status("status.new_file.resource_list_already_loaded");
        return false;
    }

    // Map references and resource-list Load paths both resolve from the entry
    // map directory, so use the existing source-path helper for both.
    const ListAssetSourcePathResult selected_path =
        make_list_asset_source_path(model_.path, selected_file);
    if (selected_path.source_path.empty()) {
        KME_ADD_LOG("[warn]failed to derive a new-file reference path: selected=\"" +
                selected_path.resolved_path + "\"");
        set_program_status("status.edit.required_field");
        return false;
    }

    std::map<std::string, MapElementPendingChange> candidate = pending_edit_changes_;
    const std::string resource_kind(new_file_resource_list_kind(kind));
    // The ledger is keyed by edit ID and therefore serializes in key order.
    // Keep all wizard-created references in one zero-padded sequence so their
    // generated source remains in creation order when the ledger is replayed.
    static constexpr char k_id_prefix[] = "new-file-reference-";
    std::string edit_id;
    for (std::uint64_t suffix = 1;; ++suffix) {
        std::ostringstream id;
        id << k_id_prefix << std::setw(20) << std::setfill('0') << suffix;
        edit_id = id.str();
        const bool pending = candidate.find(edit_id) != candidate.end();
        const bool loaded = std::any_of(
            model_.edit_statements.begin(), model_.edit_statements.end(),
            [&](const EditStatementInfo& statement) { return statement.edit_id == edit_id; });
        if (!pending && !loaded) break;
    }

    MapElementPendingChange change;
    change.change_id = edit_id;
    change.edit_id = edit_id;
    change.row_kind = resource_kind.empty() ? "include" : "resourceList.load";
    change.operation = "insert";
    change.target_file_path = target_file_path;
    if (resource_kind.empty()) {
        change.field_changes.emplace("includePath", selected_path.source_path);
    } else {
        change.field_changes.emplace("resourceListKind", resource_kind);
        change.field_changes.emplace("resourceListPath", selected_path.source_path);
    }
    // Inserts deliberately leave expectedSourceHash empty: maploader compares
    // the target with its authoritative disk baseline across Apply/Save cycles.
    candidate[edit_id] = std::move(change);

    if (!apply_edit_ledger_to_preview(candidate, std::nullopt, false)) return false;
    if (!selected_path.fallback_reason.empty()) {
        KME_ADD_LOG(LogSeverity::Warning,
                "[warning]unable to create a relative new-file reference; "
                "using absolute path: reason=\"" + selected_path.fallback_reason +
                "\", selected=\"" + selected_path.resolved_path +
                "\", map=\"" + model_.path + "\"");
        set_program_status("status.edit.relative_path_fallback");
    } else {
        set_program_status("status.edit.pending");
    }
    return true;
}

void App::request_new_file_create(NewFileCreateRequest request) {
    pending_new_file_create_request_ = std::move(request);
}

void App::process_pending_new_file_create() {
    if (!pending_new_file_create_request_) return;
    NewFileCreateRequest request = std::move(*pending_new_file_create_request_);
    pending_new_file_create_request_.reset();
    if (!new_file_name_is_safe(request.file_name)) {
        set_program_status("status.new_file.invalid_path");
        return;
    }

    const std::filesystem::path directory(utf8_to_wide(request.directory));
    std::error_code ec;
    if (directory.empty() || !std::filesystem::is_directory(directory, ec) || ec) {
        set_program_status("status.new_file.invalid_path");
        return;
    }
    std::filesystem::path file_name(utf8_to_wide(request.file_name));
    file_name += request.use_csv_extension ? L".csv" : L".txt";
    const std::filesystem::path path = directory / file_name;

    if (!request.target_file_path.empty()) {
        if (!edit_actions_available() || !has_model_) {
            set_program_status("status.new_file.reference_requires_edit");
            return;
        }
        if (!find_model_source_file(model_, request.target_file_path)) {
            KME_ADD_LOG("[warn]new-file reference target is not part of the loaded map: " +
                    request.target_file_path);
            set_program_status("status.new_file.reference_failed");
            return;
        }
        if (new_file_resource_list_is_already_referenced(model_, pending_edit_changes_, request.kind)) {
            set_program_status("status.new_file.resource_list_already_loaded");
            return;
        }
    }

    const bool existing_file = std::filesystem::exists(path, ec);
    if (ec) {
        KME_ADD_LOG("[error]failed to inspect new BVE file path: " +
                wide_to_utf8(path.wstring()));
        set_program_status("status.new_file.create_failed");
        return;
    }
    if (existing_file && (!std::filesystem::is_regular_file(path, ec) || ec)) {
        KME_ADD_LOG("[warn]new BVE file path is not a regular file: " +
                wide_to_utf8(path.wstring()));
        set_program_status("status.new_file.create_failed");
        return;
    }
    if (!existing_file) {
        std::string create_error;
        if (!create_utf8_bve_file_exclusive(path, new_bve_file_header(request.kind), create_error)) {
            KME_ADD_LOG("[error]" + create_error + ": " +
                    wide_to_utf8(path.wstring()));
            set_program_status("status.new_file.create_failed");
            return;
        }
    }

    const std::string selected_file = wide_to_utf8(path.wstring());
    if (!request.target_file_path.empty() &&
        !stage_new_file_reference(request.kind, request.target_file_path, selected_file)) {
        KME_ADD_LOG("[warn]new BVE file was not staged as a reference: " +
                selected_file);
        set_program_status("status.new_file.reference_failed");
        return;
    }
    new_file_wizard_.open = false;
    if (request.load_after_create && request.kind == NewFileKind::Map) {
        open_document(selected_file, true);
        return;
    }
    if (request.target_file_path.empty()) {
        set_program_status(existing_file ? "status.new_file.reused" : "status.new_file.created");
    }
}

void App::process_pending_include_file_insert() {
    if (!pending_include_file_insert_request_) return;
    IncludeFileInsertRequest request =
        std::move(*pending_include_file_insert_request_);
    pending_include_file_insert_request_.reset();
    if (!edit_actions_available() || !has_model_) return;

    if (!find_model_source_file(model_, request.target_file_path)) {
        KME_ADD_LOG("[warn]Include insert target is not part of the loaded map: " +
                request.target_file_path);
        return;
    }
    const std::string initial_directory = list_asset_picker_initial_directory(
        request.target_file_path, model_.path);
    const std::string selected_file = request.create_new_file
        ? save_include_file_dialog(initial_directory)
        : open_include_file_dialog(initial_directory);
    if (selected_file.empty()) return;

    if (request.create_new_file) {
        std::string create_error;
        if (!create_utf8_bve_map_file_exclusive(
                std::filesystem::path(utf8_to_wide(selected_file)), create_error)) {
            KME_ADD_LOG("[error]" + create_error + ": " + selected_file);
            set_program_status("status.edit.include_create_failed");
            return;
        }
    }

    stage_new_file_reference(NewFileKind::Map, request.target_file_path, selected_file);
}

std::string expected_source_hash_for_edit_target(
    const MapModel& model,
    const std::map<std::string, MapElementPendingChange>& pending_changes,
    const std::string& edit_id,
    const std::string& preferred_hash,
    const std::string& source_file_path) {
    auto pending = pending_changes.find(edit_id);
    if (pending != pending_changes.end() && !pending->second.expected_source_hash.empty()) {
        return pending->second.expected_source_hash;
    }

    // The complete pending ledger is replayed from disk on every Apply. Keep
    // its optimistic-concurrency hash pinned to that disk baseline even while
    // the active maploader handle represents a dirty in-memory working copy.
    if (!preferred_hash.empty()) return preferred_hash;
    const EditSourceFileInfo* source_file = find_model_source_file(model, source_file_path);
    if (source_file && !source_file->source_hash.empty()) return source_file->source_hash;
    return {};
}

namespace {

const char* resource_list_content_row_kind(ResourceListKind kind) {
    switch (kind) {
    case ResourceListKind::Structure: return "structure.model";
    case ResourceListKind::Signal: return "signal.aspect";
    case ResourceListKind::Sound: return "sound.list";
    case ResourceListKind::Sound3D: return "sound3D.list";
    case ResourceListKind::Station: return "station.list";
    case ResourceListKind::Count:
        return "";
    }
    return "";
}

bool resource_list_content_change_is_for_source(
    const MapModel& model, const MapElementPendingChange& change,
    ResourceListKind kind, const std::string& source_path) {
    const char* row_kind = resource_list_content_row_kind(kind);
    if (*row_kind == '\0' || change.row_kind != row_kind) return false;
    return std::any_of(
        model.edit_elements.begin(), model.edit_elements.end(),
        [&](const EditElementInfo& element) {
            return element.edit_id == change.edit_id &&
                element.row_kind == row_kind &&
                element.source_file_path == source_path;
        });
}

} // namespace

void App::request_resource_list_file_change(ResourceListKind kind) {
    if (!edit_actions_available() || !has_model_ ||
        kind == ResourceListKind::Count) {
        return;
    }
    const size_t index = static_cast<size_t>(kind);
    if (index >= model_.resource_list_sources.size()) return;
    const ResourceListSource& source = model_.resource_list_sources[index];
    if (!source.present || source.edit_id.empty() || source.source_file_path.empty()) return;
    pending_resource_list_file_change_request_ = ResourceListFileChangeRequest{
        kind, source.edit_id, source.source_file_path, source.evaluated_path,
        source.resolved_path, {}, {}, {}, false};
}

void App::process_pending_resource_list_file_change() {
    if (!pending_resource_list_file_change_request_) return;
    ResourceListFileChangeRequest request =
        std::move(*pending_resource_list_file_change_request_);
    pending_resource_list_file_change_request_.reset();
    if (!edit_actions_available() || !has_model_) return;

    if (request.selected_source_path.empty()) {
        const std::string initial_directory = list_asset_picker_initial_directory(
            request.current_resolved_path, model_.path);
        const std::string selected_file = open_include_file_dialog(
            initial_directory, "dialog.select_resource_list_file",
            "dialog.filter.resource_list_files");
        if (selected_file.empty()) return;
        ListAssetSourcePathResult selected_path = make_list_asset_source_path(
            model_.path, selected_file);
        if (selected_path.source_path.empty()) {
            KME_ADD_LOG("[warn]failed to derive a resource-list path for the selected file: selected=\"" +
                    selected_path.resolved_path + "\"");
            set_program_status("status.edit.required_field");
            return;
        }
        request.selected_source_path = std::move(selected_path.source_path);
        request.selected_resolved_path = std::move(selected_path.resolved_path);
        request.fallback_reason = std::move(selected_path.fallback_reason);
    }
    if (request.selected_source_path == request.current_evaluated_path) return;

    std::map<std::string, MapElementPendingChange> candidate = pending_edit_changes_;
    bool discards_applied_content = false;
    for (auto change = candidate.begin(); change != candidate.end();) {
        if (resource_list_content_change_is_for_source(
                model_, change->second, request.kind,
                request.current_resolved_path)) {
            change = candidate.erase(change);
            discards_applied_content = true;
        } else {
            ++change;
        }
    }

    const EditSourceFileInfo* source_file = find_model_source_file(
        model_, request.source_file_path);
    MapElementPendingChange change;
    change.change_id = "resource-list-load-" + request.edit_id;
    change.edit_id = request.edit_id;
    change.row_kind = "resourceList.load";
    change.operation = "update";
    change.field_changes.emplace("resourceListPath", request.selected_source_path);
    change.expected_source_hash = expected_source_hash_for_edit_target(
        model_, pending_edit_changes_, request.edit_id,
        source_file ? source_file->source_hash : std::string{},
        request.source_file_path);
    candidate[request.edit_id] = std::move(change);

    if (!validate_resource_list_file_change_candidate(candidate, request.kind)) return;

    EditableListEditState* target_edit = nullptr;
    const EditableListSpec* target_spec = nullptr;
    switch (request.kind) {
    case ResourceListKind::Station:
        target_edit = &station_definition_edit_;
        target_spec = &k_station_definition_edit_spec;
        break;
    case ResourceListKind::Structure:
        target_edit = &structure_model_edit_;
        target_spec = &k_structure_model_edit_spec;
        break;
    case ResourceListKind::Signal:
        target_edit = &signal_aspect_edit_;
        target_spec = &k_signal_aspect_edit_spec;
        break;
    case ResourceListKind::Sound:
        target_edit = &sound_list_edit_;
        target_spec = &k_sound_list_edit_spec;
        break;
    case ResourceListKind::Sound3D:
        target_edit = &sound_3d_list_edit_;
        target_spec = &k_sound_3d_list_edit_spec;
        break;
    case ResourceListKind::Count:
        return;
    }
    const bool discards_drafts = target_edit && target_spec &&
        has_editable_list_drafts(*target_edit, *target_spec);
    if (!request.confirmed_discard &&
        (discards_drafts || discards_applied_content)) {
        resource_list_file_change_confirmation_ = std::move(request);
        popups_.resource_list_file_change_confirm = true;
        return;
    }

    if (!apply_edit_ledger_to_preview(candidate, std::nullopt, false)) return;
    if (target_edit && target_spec) {
        *target_edit = EditableListEditState{};
        invalidate_table_cache();
        rebind_other_editable_list_drafts(target_edit);
        reset_editable_list_find_results(*target_spec);
    }
    if (!request.fallback_reason.empty()) {
        KME_ADD_LOG(
            LogSeverity::Warning,
            "[warning]unable to create a relative resource-list path; "
            "using absolute path: reason=\"" + request.fallback_reason +
            "\", selected=\"" + request.selected_resolved_path +
            "\", map=\"" + model_.path + "\"");
        set_program_status("status.edit.relative_path_fallback");
    } else {
        set_program_status("status.edit.pending");
    }
}

bool App::apply_other_track_rename() {
    if (!edit_actions_available()) return false;
    const std::string source_key = trim_gui_ascii_copy(other_track_rename_.source_key);
    const std::string requested_key = trim_gui_ascii_copy(other_track_rename_.apply_key);
    if (source_key.empty() || requested_key.empty()) {
        set_program_status("status.edit.required_field");
        return false;
    }

    struct TrackViewState {
        bool found = false;
        bool visible = false;
        double range_min = 0.0;
        double range_max = 0.0;
        ImVec4 color{};
    } view_state;
    for (const OtherTrack& track : model_.other_tracks) {
        if (!resource_key_values_equal(track.key, source_key)) continue;
        view_state.found = true;
        view_state.visible = track.visible;
        view_state.range_min = track.range_min;
        view_state.range_max = track.range_max;
        view_state.color = track.color;
        break;
    }

    std::map<std::string, MapElementPendingChange> candidate = pending_edit_changes_;
    std::vector<std::string> target_edit_ids;
    bool ledger_changed = false;
    for (const TableRow& row : model_.other_track_changes) {
        if (row.edit_id.empty() ||
            !resource_key_values_equal(table_cell(row, "trackKey"), source_key)) {
            continue;
        }
        target_edit_ids.push_back(row.edit_id);
        auto [pending, inserted] = candidate.try_emplace(row.edit_id);
        MapElementPendingChange& change = pending->second;
        if (inserted) {
            change.change_id = "other-track-key-" + row.edit_id;
            change.edit_id = row.edit_id;
            change.row_kind = "otherTrack.change";
            change.operation = "update";
        } else if (change.operation == "delete") {
            continue;
        } else if (change.operation != "update" ||
                   change.row_kind != "otherTrack.change") {
            KME_ADD_LOG("[error]other-track rename conflicts with pending edit: " +
                    row.edit_id);
            set_program_status("status.edit.pending");
            return false;
        }
        if (change.expected_source_hash.empty()) {
            change.expected_source_hash = expected_source_hash_for_edit_target(
                model_, pending_edit_changes_, row.edit_id, {}, row.source.file_path);
        }

        const TableRow* baseline_row = &row;
        const auto original = original_edit_rows_.find(row.edit_id);
        if (original != original_edit_rows_.end()) {
            baseline_row = &original->second.row;
        }
        const std::string baseline_key =
            trim_gui_ascii_copy(table_cell(*baseline_row, "trackKey"));
        if (requested_key == baseline_key) {
            ledger_changed = change.field_changes.erase("trackKey") != 0 || ledger_changed;
            if (change.field_changes.empty() && change.replacement_statement.empty()) {
                candidate.erase(row.edit_id);
            }
        } else {
            auto existing_key = change.field_changes.find("trackKey");
            if (existing_key == change.field_changes.end() ||
                existing_key->second != requested_key) {
                change.field_changes["trackKey"] = requested_key;
                ledger_changed = true;
            }
        }
    }
    if (target_edit_ids.empty()) {
        KME_ADD_LOG("[error]other-track rename found no editable Track statements");
        set_program_status("status.edit.pending");
        return false;
    }
    if (!ledger_changed) {
        other_track_rename_ = OtherTrackRenameState{};
        set_program_status("status.edit.no_changes");
        return true;
    }

    if (!apply_edit_ledger_to_preview(candidate, std::nullopt, false)) return false;

    std::string applied_key;
    for (const TableRow& row : model_.other_track_changes) {
        if (std::find(target_edit_ids.begin(), target_edit_ids.end(), row.edit_id) ==
            target_edit_ids.end()) {
            continue;
        }
        applied_key = table_cell(row, "trackKey");
        break;
    }
    if (view_state.found && !applied_key.empty()) {
        for (OtherTrack& track : model_.other_tracks) {
            if (!resource_key_values_equal(track.key, applied_key)) continue;
            track.visible = view_state.visible;
            track.range_min = view_state.range_min;
            track.range_max = view_state.range_max;
            track.color = view_state.color;
            break;
        }
        refresh_local_preview_after_edit("otherTrack.change");
        sync_scene_preview_track_visibility();
    }
    other_track_rename_ = OtherTrackRenameState{};
    set_program_status("status.edit.applied_to_preview");
    return true;
}

std::string inspector_expected_source_hash(
    const MapModel& model,
    const std::map<std::string, MapElementPendingChange>& pending_changes,
    const MapElementInspectorState& inspector) {
    return expected_source_hash_for_edit_target(
        model, pending_changes, inspector.edit_id, inspector.expected_source_hash,
        inspector.source_file);
}

bool find_row_index_by_edit_id(const std::vector<TableRow>& rows,
                               const std::string& edit_id,
                               size_t& row_index) {
    if (edit_id.empty()) return false;
    for (size_t i = 0; i < rows.size(); ++i) {
        if (rows[i].edit_id == edit_id) {
            row_index = i;
            return true;
        }
    }
    return false;
}

std::string inspector_row_field_value(const TableRow& row,
                                      const std::string& row_kind,
                                      const std::string& field_key) {
    if (row_kind == "cabIlluminance.change" && field_key == "value") {
        return table_cell(row, "_sourceValue");
    }
    if (row_kind == "station.put") {
        if (field_key == "distance") return table_cell(row, "_distance");
        if (field_key == "stationKey") return table_cell(row, "posKey");
    }
    if (row_kind == "section.begin" || row_kind == "section.speedLimit") {
        constexpr std::string_view k_values_prefix = "values.";
        if (field_key.rfind(k_values_prefix.data(), 0) == 0) {
            const size_t index = static_cast<size_t>(std::strtoul(
                field_key.c_str() + k_values_prefix.size(), nullptr, 10));
            return table_cell(row, "value" + std::to_string(index));
        }
    }
    return table_cell(row, field_key);
}

void set_inspector_row_field_value(TableRow& row,
                                   const std::string& row_kind,
                                   const std::string& field_key,
                                   const std::string& value,
                                   double distance_origin) {
    if (row_kind == "signal.put" && field_key == "form") return;
    if (row_kind == "cabIlluminance.change" && field_key == "value") {
        row.cells["_sourceValue"] = value;
        return;
    }
    if (row_kind == "station.put") {
        if (field_key == "distance") {
            row.cells["_distance"] = value;
            double absolute_distance = table_cell_number(row, "_distance");
            row.cells["dist"] = format_double(absolute_distance - distance_origin, 0);
            return;
        }
        if (field_key == "stationKey") {
            row.cells["posKey"] = value;
            return;
        }
    }
    if (row_kind == "repeater") {
        constexpr std::string_view k_structure_key_prefix = "structureKeys.";
        if (field_key.rfind(k_structure_key_prefix.data(), 0) == 0) {
            if (field_key == "structureKeys.count") {
                double parsed_count = 0.0;
                if (!parse_gui_edit_number(value, &parsed_count) || parsed_count < 1.0 ||
                    std::trunc(parsed_count) != parsed_count) {
                    return;
                }
                const size_t count = static_cast<size_t>(parsed_count);
                std::string joined;
                for (size_t index = 0; index < count; ++index) {
                    const std::string key = "_structureKeys." + std::to_string(index);
                    const auto item = row.cells.find(key);
                    if (item == row.cells.end() || item->second.empty()) return;
                    if (!joined.empty()) joined += ",";
                    joined += item->second;
                }
                row.cells["structureKeys"] = std::move(joined);
            } else {
                row.cells["_" + field_key] = value;
            }
            return;
        }
    }
    if (row_kind == "section.begin" || row_kind == "section.speedLimit") {
        constexpr std::string_view k_values_prefix = "values.";
        if (field_key.rfind(k_values_prefix.data(), 0) == 0) {
            if (field_key == "values.count") {
                double parsed_count = 0.0;
                if (!parse_gui_edit_number(value, &parsed_count) || parsed_count < 1.0 ||
                    std::trunc(parsed_count) != parsed_count) {
                    return;
                }
                const size_t count = static_cast<size_t>(parsed_count);
                for (size_t index = 0; index < count; ++index) {
                    const std::string key = "_values." + std::to_string(index);
                    const auto item = row.cells.find(key);
                    if (item == row.cells.end() || item->second.empty()) return;
                    row.cells["value" + std::to_string(index)] = item->second;
                }
                for (size_t stale_index = count;; ++stale_index) {
                    const auto stale = row.cells.find(
                        "value" + std::to_string(stale_index));
                    if (stale == row.cells.end()) break;
                    row.cells.erase(stale);
                }
                row.cells["valueCount"] = std::to_string(count);
            } else {
                row.cells["_" + field_key] = value;
            }
            return;
        }
    }
    row.cells[field_key] = value;
}

bool row_kind_has_source_distance_string(const std::string& row_kind) {
    static constexpr std::array<const char*, 23> k_distance_row_kinds = {
        "station.put",
        "structure.put",
        "structure.between",
        "repeater",
        "signal.put",
        "irregularity.change",
        "beacon.put",
        "mapSound.play",
        "mapSound3D.put",
        "rollingNoise.change",
        "flangeNoise.change",
        "jointNoise.play",
        "background.change",
        "adhesion.change",
        "cabIlluminance.change",
        "fog.change",
        "drawDistance.change",
        "speedlimit",
        "section.begin",
        "section.speedLimit",
        "curve",
        "gradient",
        "otherTrack.change",
    };
    return std::any_of(k_distance_row_kinds.begin(), k_distance_row_kinds.end(),
                       [&](const char* value) { return row_kind == value; });
}

MapElementKeySource map_element_key_source_for_field(
    std::string_view row_kind, std::string_view field_key) noexcept {
    if (field_key == "structureKey" &&
        (row_kind == "structure.put" || row_kind == "structure.between" ||
         row_kind == "background.change")) {
        return MapElementKeySource::Structure;
    }
    if (row_kind == "repeater" && field_key.rfind("structureKeys.", 0) == 0) {
        return MapElementKeySource::Structure;
    }
    if (row_kind == "mapSound.play" && field_key == "soundKey") {
        return MapElementKeySource::Sound;
    }
    if (row_kind == "mapSound3D.put" && field_key == "soundKey") {
        return MapElementKeySource::Sound3D;
    }
    if (row_kind == "signal.put" && field_key == "signalAspectKey") {
        return MapElementKeySource::SignalAspect;
    }
    if ((field_key == "trackKey" &&
         (row_kind == "structure.put" || row_kind == "signal.put" ||
          row_kind == "repeater")) ||
        (row_kind == "structure.between" &&
         (field_key == "trackKey1" || field_key == "trackKey2"))) {
        return MapElementKeySource::Track;
    }
    return MapElementKeySource::None;
}

std::string_view trim_ascii_view(std::string_view value) noexcept {
    while (!value.empty() &&
           std::isspace(static_cast<unsigned char>(value.front())) != 0) {
        value.remove_prefix(1);
    }
    while (!value.empty() &&
           std::isspace(static_cast<unsigned char>(value.back())) != 0) {
        value.remove_suffix(1);
    }
    return value;
}

bool resource_key_values_equal(std::string_view left, std::string_view right) noexcept {
    left = trim_ascii_view(left);
    right = trim_ascii_view(right);
    if (left.size() != right.size()) return false;
    for (size_t index = 0; index < left.size(); ++index) {
        if (ascii_lower(static_cast<unsigned char>(left[index])) !=
            ascii_lower(static_cast<unsigned char>(right[index]))) {
            return false;
        }
    }
    return true;
}

bool track_key_values_equal(std::string_view left, std::string_view right) noexcept {
    size_t left_index = 0;
    size_t right_index = 0;
    const auto skip_spaces = [](std::string_view value, size_t& index) {
        while (index < value.size() &&
               std::isspace(static_cast<unsigned char>(value[index])) != 0) {
            ++index;
        }
    };
    for (;;) {
        skip_spaces(left, left_index);
        skip_spaces(right, right_index);
        const bool left_end = left_index == left.size();
        const bool right_end = right_index == right.size();
        if (left_end || right_end) return left_end && right_end;
        if (ascii_lower(static_cast<unsigned char>(left[left_index])) !=
            ascii_lower(static_cast<unsigned char>(right[right_index]))) {
            return false;
        }
        ++left_index;
        ++right_index;
    }
}

bool is_repeater_structure_key_field(const MapElementEditFieldState& field) {
    return field.key.rfind("structureKeys.", 0) == 0;
}

bool is_coordinate_offset_field(std::string_view field) {
    static constexpr std::array<std::string_view, 6> k_coordinate_fields = {
        "x", "y", "z", "rx", "ry", "rz"
    };
    return std::find(k_coordinate_fields.begin(), k_coordinate_fields.end(), field) !=
        k_coordinate_fields.end();
}

bool is_section_values_field(std::string_view field) {
    return field.rfind("values.", 0) == 0;
}

bool is_section_values_field(const MapElementEditFieldState& field) {
    return is_section_values_field(field.key);
}

std::vector<std::string> split_repeater_structure_keys(const std::string& text) {
    std::vector<std::string> keys;
    size_t begin = 0;
    while (begin <= text.size()) {
        const size_t end = text.find(',', begin);
        const std::string key = trim_gui_ascii_copy(
            text.substr(begin, end == std::string::npos ? std::string::npos : end - begin));
        if (!key.empty()) keys.push_back(key);
        if (end == std::string::npos) break;
        begin = end + 1;
    }
    return keys;
}

void reindex_repeater_structure_key_fields(MapElementInspectorState& inspector) {
    size_t index = 0;
    for (MapElementEditFieldState& field : inspector.fields) {
        if (!is_repeater_structure_key_field(field)) continue;
        field.key = "structureKeys." + std::to_string(index);
        field.backend_key = field.key;
        field.label = "structureKey[" + std::to_string(index + 1) + "]";
        ++index;
    }
}

MapElementEditFieldState make_repeater_structure_key_field(
    const MapElementInspectorState& inspector, size_t index, const std::string& value) {
    MapElementEditFieldState field;
    field.key = "structureKeys." + std::to_string(index);
    field.backend_key = field.key;
    field.target_edit_id = inspector.edit_id;
    field.expected_source_hash = inspector.expected_source_hash;
    field.label = "structureKey[" + std::to_string(index + 1) + "]";
    field.original_value = index < inspector.repeater_structure_keys_original.size()
        ? inspector.repeater_structure_keys_original[index]
        : std::string{};
    field.key_source = MapElementKeySource::Structure;
    field.required = true;
    set_edit_field_buffer(field, value);
    return field;
}

void replace_repeater_structure_key_fields(
    MapElementInspectorState& inspector, const std::vector<std::string>& structure_keys) {
    if (structure_keys.empty()) return;

    size_t insertion_index = inspector.fields.size();
    for (size_t index = 0; index < inspector.fields.size(); ++index) {
        if (is_repeater_structure_key_field(inspector.fields[index])) {
            insertion_index = index;
            break;
        }
        if (inspector.fields[index].key == "endDistance") {
            insertion_index = index;
            break;
        }
    }
    inspector.fields.erase(
        std::remove_if(inspector.fields.begin(), inspector.fields.end(),
                       [](const MapElementEditFieldState& field) {
                           return is_repeater_structure_key_field(field);
                       }),
        inspector.fields.end());
    insertion_index = std::min(insertion_index, inspector.fields.size());
    std::vector<MapElementEditFieldState> restored_fields;
    restored_fields.reserve(structure_keys.size());
    for (size_t index = 0; index < structure_keys.size(); ++index) {
        restored_fields.push_back(
            make_repeater_structure_key_field(inspector, index, structure_keys[index]));
    }
    inspector.fields.insert(
        inspector.fields.begin() + static_cast<std::ptrdiff_t>(insertion_index),
        std::make_move_iterator(restored_fields.begin()),
        std::make_move_iterator(restored_fields.end()));
}

const char* section_values_label_prefix(const std::string& row_kind) {
    return row_kind == "section.begin" ? "signal" : "v";
}

void reindex_section_values_fields(MapElementInspectorState& inspector) {
    size_t index = 0;
    for (MapElementEditFieldState& field : inspector.fields) {
        if (!is_section_values_field(field)) continue;
        field.key = "values." + std::to_string(index);
        field.backend_key = field.key;
        field.label = std::string(section_values_label_prefix(inspector.row_kind)) +
            std::to_string(index);
        ++index;
    }
}

MapElementEditFieldState make_section_values_field(
    const MapElementInspectorState& inspector, size_t index, const std::string& value) {
    MapElementEditFieldState field;
    field.key = "values." + std::to_string(index);
    field.backend_key = field.key;
    field.target_edit_id = inspector.edit_id;
    field.expected_source_hash = inspector.expected_source_hash;
    field.label = std::string(section_values_label_prefix(inspector.row_kind)) +
        std::to_string(index);
    field.original_value = index < inspector.section_values_original.size()
        ? inspector.section_values_original[index]
        : std::string{};
    field.numeric_constraint = MapElementNumericConstraint::Finite;
    field.required = true;
    set_edit_field_buffer(field, value);
    return field;
}

void set_inspector_coordinate_offsets_enabled(MapElementInspectorState& inspector,
                                              bool enabled) {
    const bool supported_row_kind = inspector.row_kind == "structure.put" ||
        inspector.row_kind == "repeater";
    if (!inspector.open || !supported_row_kind) return;
    if (!enabled) {
        for (MapElementEditFieldState& field : inspector.fields) {
            if (is_coordinate_offset_field(field.key)) {
                set_edit_field_buffer(field, "0");
            }
        }
    }
    inspector.coordinate_offsets_enabled = enabled;
    inspector.coordinate_offset_discard_prompt_requested = false;
}

bool inspector_coordinate_offsets_are_zero(const MapElementInspectorState& inspector) {
    for (const char* key : {"x", "y", "z", "rx", "ry", "rz"}) {
        const MapElementEditFieldState* field = find_inspector_field(inspector, key);
        double value = 0.0;
        if (!field || !parse_gui_edit_number(edit_field_buffer_text(*field), &value) ||
            truncate_gui_thousandths(value) != 0.0) {
            return false;
        }
    }
    return true;
}

void capture_repeater_inspector_draft(MapElementInspectorState& inspector) {
    if (!inspector.open || inspector.row_kind != "repeater" || inspector.edit_id.empty()) return;
    RepeaterInspectorDraft draft;
    draft.coordinate_offsets_enabled = inspector.coordinate_offsets_enabled;
    draft.fields.reserve(inspector.fields.size());
    for (const MapElementEditFieldState& field : inspector.fields) {
        if (!field.read_only) draft.fields.emplace_back(field.key, edit_field_buffer_text(field));
    }
    inspector.session.repeater_drafts[inspector.edit_id] = std::move(draft);
}

void restore_repeater_inspector_draft(MapElementInspectorState& inspector) {
    if (!inspector.open || inspector.row_kind != "repeater" || inspector.edit_id.empty()) return;
    const auto draft_it = inspector.session.repeater_drafts.find(inspector.edit_id);
    if (draft_it == inspector.session.repeater_drafts.end()) return;
    const RepeaterInspectorDraft& draft = draft_it->second;

    set_inspector_coordinate_offsets_enabled(
        inspector, draft.coordinate_offsets_enabled);

    std::vector<std::string> structure_keys;
    for (const auto& entry : draft.fields) {
        if (entry.first.rfind("structureKeys.", 0) == 0) {
            structure_keys.push_back(entry.second);
            continue;
        }
        MapElementEditFieldState* field = find_inspector_field(inspector, entry.first);
        if (field && !field->read_only) set_edit_field_buffer(*field, entry.second);
    }
    replace_repeater_structure_key_fields(inspector, structure_keys);
}

MapElementInspectorRequest make_inspector_reload_request(const MapElementInspectorState& inspector) {
    MapElementInspectorRequest request;
    request.edit_id = inspector.edit_id;
    request.row_kind = inspector.row_kind;
    request.source_file = inspector.source_file;
    request.line = inspector.line;
    request.column = inspector.column;
    for (const MapElementEditFieldState& field : inspector.fields) {
        if (field.read_only ||
            (!field.target_edit_id.empty() && field.target_edit_id != inspector.edit_id)) {
            continue;
        }
        const std::string& backend_key = field.backend_key.empty() ? field.key : field.backend_key;
        request.field_values[backend_key] = trim_gui_ascii_copy(edit_field_buffer_text(field));
    }
    request.inspector_session = inspector.session;
    return request;
}

bool App::open_element_inspector(const MapElementInspectorRequest& request) {
    if (!edit_actions_available()) return false;
    if (request.edit_id.empty() && request.field_values.empty()) return false;

    std::string edit_id = request.edit_id;
    size_t model_row_index = 0;
    const TableRow* row = find_model_row_for_inspector_request(
        model_, request, edit_id, model_row_index);
    if (!row) {
        KME_ADD_LOG("[warn]edit target row not found: " + request.edit_id);
        return false;
    }
    clear_scene_placement_edit_target();

    if (!request.edit_id.empty() && request.edit_id != edit_id) {
        auto pending = pending_edit_changes_.extract(request.edit_id);
        if (!pending.empty()) {
            pending.key() = edit_id;
            pending.mapped().edit_id = edit_id;
            pending_edit_changes_.insert(std::move(pending));
        }
        auto snapshot = original_edit_rows_.extract(request.edit_id);
        if (!snapshot.empty()) {
            snapshot.key() = edit_id;
            snapshot.mapped().row.edit_id = edit_id;
            original_edit_rows_.insert(std::move(snapshot));
        }
    }

    std::string info_error;
    std::optional<InspectorTargetMetadata> target_info =
        resolve_inspector_target_metadata(handle_, edit_id, request.row_kind, &info_error);
    if (!target_info && !info_error.empty()) {
        KME_ADD_LOG("[warn]edit target metadata fallback: " + info_error);
    }

    EditSourceInfo source = target_info ? target_info->source : row->source;
    if (source.file_path.empty()) source = row->source;

    MapElementInspectorState next;
    next.open = true;
    next.edit_id = edit_id;
    next.row_kind = request.row_kind;
    next.model_row_index = model_row_index;
    next.model_row_source_revision = plan_data_source_revision_;
    next.title = tr("dialog.element_properties");
    if (request.inspector_session) next.session = *request.inspector_session;
    next.source_file = source.file_path;
    next.source_file_name = display_name_from_path(next.source_file);
    if (target_info) next.expected_source_hash = target_info->expected_source_hash;
    next.line = source.line;
    next.column = source.column;
    if (target_info) next.source_distance_string = target_info->source_distance_string;
    next.raw_statement = target_info && !target_info->raw_statement.empty()
        ? target_info->raw_statement
        : source.raw_text_preview;
    if (target_info) next.statement_kind = target_info->statement_kind;
    next.source_signal_short_form = target_info &&
        (target_info->flags & KV_EDIT_TARGET_FLAG_SIGNAL_SHORT_FORM) != 0;
    next.owned_edit_ids.push_back(edit_id);
    if (next.expected_source_hash.empty()) {
        const EditSourceFileInfo* source_file = find_model_source_file(model_, next.source_file);
        if (source_file) next.expected_source_hash = source_file->source_hash;
    }

    const TableRow* original_row = row;
    auto snapshot = original_edit_rows_.find(edit_id);
    if (snapshot != original_edit_rows_.end()) {
        original_row = &snapshot->second.row;
    }

    auto add_field = [&](const std::string& key, const std::string& label,
                         const std::string& value, const std::string& original_value,
                         MapElementNumericConstraint numeric_constraint, bool required) {
        MapElementEditFieldState field;
        field.key = key;
        field.backend_key = key;
        field.target_edit_id = edit_id;
        field.expected_source_hash = next.expected_source_hash;
        field.label = label;
        field.original_value = original_value;
        if (key == "distance") field.source_distance_string = next.source_distance_string;
        field.numeric_constraint = numeric_constraint;
        field.key_source = map_element_key_source_for_field(request.row_kind, key);
        field.required = required;
        set_edit_field_buffer(field, value);
        next.fields.push_back(field);
    };
    auto add_row_field = [&](const std::string& key, const std::string& label,
                             MapElementNumericConstraint numeric_constraint, bool required) {
        add_field(key, label,
                  inspector_row_field_value(*row, request.row_kind, key),
                  inspector_row_field_value(*original_row, request.row_kind, key),
                  numeric_constraint, required);
    };
    auto add_related_field = [&](const std::string& key, const std::string& backend_key,
                                 const std::string& label, const std::string& value,
                                 const std::string& original_value,
                                 MapElementNumericConstraint numeric_constraint, bool required,
                                 const std::string& target_edit_id,
                                 const std::string& expected_source_hash,
                                 const std::string& source_distance_string) {
        add_field(key, label, value, original_value, numeric_constraint, required);
        MapElementEditFieldState& field = next.fields.back();
        field.backend_key = backend_key;
        field.target_edit_id = target_edit_id;
        field.expected_source_hash = expected_source_hash;
        field.source_distance_string = source_distance_string;
    };

    if (request.row_kind == "structure.model") {
        add_row_field("structureKey", "structureKey", MapElementNumericConstraint::None, true);
        add_row_field("filePath", "filePath", MapElementNumericConstraint::None, true);
    } else if (request.row_kind == "structure.put") {
        const std::string method = table_cell(*row, "method");
        const std::string original_method = table_cell(*original_row, "method");
        next.source_zero_offset_method = ascii_lower(original_method) == "put0";
        next.coordinate_offsets_enabled = ascii_lower(method) != "put0";
        add_row_field("distance", "distance", MapElementNumericConstraint::Finite, true);
        add_row_field("structureKey", "structureKey", MapElementNumericConstraint::None, true);
        add_row_field("trackKey", "trackKey", MapElementNumericConstraint::None, false);
        for (const char* key : {"x", "y", "z", "rx", "ry", "rz"}) {
            add_row_field(key, key, structure_edit_numeric_constraint(key), true);
        }
        add_row_field("tilt", "tilt", structure_edit_numeric_constraint("tilt"), true);
        add_row_field("span", "span", structure_edit_numeric_constraint("span"), true);
    } else if (request.row_kind == "structure.between") {
        add_row_field("distance", "distance", MapElementNumericConstraint::Finite, true);
        add_row_field("structureKey", "structureKey", MapElementNumericConstraint::None, true);
        add_row_field("trackKey1", "trackKey1", MapElementNumericConstraint::None, true);
        add_row_field("trackKey2", "trackKey2", MapElementNumericConstraint::None, true);
        add_row_field("flag", "flag", MapElementNumericConstraint::Finite, true);
    } else if (request.row_kind == "station.put") {
        add_row_field("distance", "distance", MapElementNumericConstraint::Finite, true);
        add_row_field("stationKey", "stationKey", MapElementNumericConstraint::None, true);
        add_row_field("door", "door", MapElementNumericConstraint::Door, false);
        add_row_field("margin1", "back", MapElementNumericConstraint::Negative, false);
        add_row_field("margin2", "front", MapElementNumericConstraint::Positive, false);
    } else if (request.row_kind == "irregularity.change") {
        add_row_field("distance", "distance", MapElementNumericConstraint::Finite, true);
        for (const char* key : {"x", "y", "r", "lx", "ly", "lr"}) {
            add_row_field(key, key, MapElementNumericConstraint::Finite, true);
        }
    } else if (request.row_kind == "beacon.put") {
        add_row_field("distance", "distance", MapElementNumericConstraint::Finite, true);
        for (const char* key : {"type", "section", "sendData"}) {
            add_row_field(key, key, MapElementNumericConstraint::Finite, true);
        }
    } else if (request.row_kind == "mapSound.play") {
        add_row_field("distance", "distance", MapElementNumericConstraint::Finite, true);
        add_row_field("soundKey", "soundKey", MapElementNumericConstraint::None, true);
    } else if (request.row_kind == "mapSound3D.put") {
        add_row_field("distance", "distance", MapElementNumericConstraint::Finite, true);
        add_row_field("soundKey", "soundKey", MapElementNumericConstraint::None, true);
        add_row_field("x", "x", MapElementNumericConstraint::Finite, true);
        add_row_field("y", "y", MapElementNumericConstraint::Finite, true);
    } else if (request.row_kind == "rollingNoise.change" ||
               request.row_kind == "flangeNoise.change" ||
               request.row_kind == "jointNoise.play") {
        add_row_field("distance", "distance", MapElementNumericConstraint::Finite, true);
        add_row_field("index", "index", MapElementNumericConstraint::Finite, true);
    } else if (request.row_kind == "background.change") {
        add_row_field("distance", "distance", MapElementNumericConstraint::Finite, true);
        add_row_field("structureKey", "structureKey", MapElementNumericConstraint::None, true);
    } else if (request.row_kind == "adhesion.change") {
        add_row_field("distance", "distance", MapElementNumericConstraint::Finite, true);
        add_row_field("a", "a", MapElementNumericConstraint::Finite, true);
        add_row_field("b", "b", MapElementNumericConstraint::Finite, false);
        add_row_field("c", "c", MapElementNumericConstraint::Finite, false);
    } else if (request.row_kind == "cabIlluminance.change") {
        add_row_field("distance", "distance", MapElementNumericConstraint::Finite, true);
        add_row_field("value", "value", MapElementNumericConstraint::Finite, false);
    } else if (request.row_kind == "fog.change") {
        const bool source_set = target_info &&
            ascii_lower(target_info->statement_kind) == "fog.set";
        add_row_field("distance", "distance", MapElementNumericConstraint::Finite, true);
        for (const char* key : {"density", "red", "green", "blue"}) {
            add_row_field(key, key, MapElementNumericConstraint::Finite, source_set);
        }
    } else if (request.row_kind == "drawDistance.change") {
        add_row_field("distance", "distance", MapElementNumericConstraint::Finite, true);
        add_row_field("value", "value", MapElementNumericConstraint::Finite, true);
    } else if (request.row_kind == "speedlimit") {
        add_row_field("distance", "distance", MapElementNumericConstraint::Finite, true);
        if (ascii_lower(table_cell(*row, "method")) == "begin") {
            add_row_field("speed", "speed", MapElementNumericConstraint::Finite, true);
        }
    } else if (request.row_kind == "section.begin" ||
               request.row_kind == "section.speedLimit") {
        const bool begins = request.row_kind == "section.begin";
        const std::vector<std::string> current_values =
            section_row_values(*row);
        const std::vector<std::string> original_values =
            section_row_values(*original_row);
        next.section_values_original = original_values;
        add_row_field("distance", "distance", MapElementNumericConstraint::Finite, true);
        const char* value_prefix = begins ? "signal" : "v";
        for (size_t value_index = 0; value_index < current_values.size(); ++value_index) {
            const std::string label = std::string(value_prefix) + std::to_string(value_index);
            add_row_field("values." + std::to_string(value_index), label,
                          MapElementNumericConstraint::Finite, true);
        }
    } else if (request.row_kind == "curve" || request.row_kind == "gradient") {
        const bool curve = request.row_kind == "curve";
        const std::string method = ascii_lower(table_cell(*row, "method"));
        const int argument_count = static_cast<int>(table_cell_number(*row, "argumentCount"));
        const bool transition = method == (curve ? "curve.begintransition"
                                                  : "gradient.begintransition");
        if (transition) {
            KME_ADD_LOG("[warn]BeginTransition must be edited through its paired Begin/End");
            return false;
        }
        const bool transition_capable = curve
            ? ((method == "curve.begin" && argument_count == 2) ||
               method == "curve.begincircular" || method == "curve.end")
            : (method == "gradient.begin" || method == "gradient.beginconst" ||
               method == "gradient.end");
        if (transition_capable) {
            const std::string transition_edit_id = table_cell(*row, "_transitionEditId");
            const std::vector<TableRow>& related_rows = curve
                ? model_.curve_rows : model_.gradient_rows;
            size_t transition_row_index = 0;
            if (!transition_edit_id.empty() &&
                find_row_index_by_edit_id(related_rows, transition_edit_id,
                                          transition_row_index)) {
                const TableRow& transition_row = related_rows[transition_row_index];
                const TableRow* original_transition_row = &transition_row;
                const auto original_transition = original_edit_rows_.find(transition_edit_id);
                if (original_transition != original_edit_rows_.end()) {
                    original_transition_row = &original_transition->second.row;
                }
                std::string transition_info_error;
                const std::optional<InspectorTargetMetadata> transition_info =
                    resolve_inspector_target_metadata(handle_, transition_edit_id,
                                                      request.row_kind,
                                                      &transition_info_error);
                if (!transition_info && !transition_info_error.empty()) {
                    KME_ADD_LOG("[warn]BeginTransition metadata fallback: " +
                            transition_info_error);
                }
                const EditSourceInfo transition_source = transition_info
                    ? transition_info->source : transition_row.source;
                std::string transition_hash = transition_info
                    ? transition_info->expected_source_hash : std::string{};
                if (transition_hash.empty()) {
                    const EditSourceFileInfo* file = find_model_source_file(
                        model_, transition_source.file_path);
                    if (file) transition_hash = file->source_hash;
                }
                next.owned_edit_ids.push_back(transition_edit_id);
                add_related_field(
                    "transitionStart", "distance", tr("label.transition_start"),
                    inspector_row_field_value(transition_row, request.row_kind, "distance"),
                    inspector_row_field_value(*original_transition_row,
                                              request.row_kind, "distance"),
                    MapElementNumericConstraint::Finite, true, transition_edit_id,
                    transition_hash,
                    transition_info ? transition_info->source_distance_string : std::string{});
            } else {
                add_field("transitionStart", tr("label.transition_start"),
                          tr("value.none"), tr("value.none"),
                          MapElementNumericConstraint::None, false);
                next.fields.back().read_only = true;
            }
        }
        add_row_field("distance", "Distance",
                      MapElementNumericConstraint::Finite, true);
        if (curve && argument_count > 0) {
            add_row_field("radius", "Radius",
                          MapElementNumericConstraint::Finite, true);
            if (argument_count == 2) {
                add_row_field("cant", "Cant",
                              MapElementNumericConstraint::Finite, true);
            }
        } else if (!curve && argument_count > 0) {
            add_row_field("gradient", "Gradient",
                          MapElementNumericConstraint::Finite, true);
        }
    } else if (request.row_kind == "otherTrack.change") {
        add_row_field("trackKey", "trackKey",
                      MapElementNumericConstraint::None, true);
        next.fields.back().read_only =
            map_element_inspector_field_forced_read_only(
                request.row_kind, "trackKey");
        add_row_field("distance", "Distance",
                      MapElementNumericConstraint::Finite, true);

        const std::string method = ascii_lower(table_cell(*row, "method"));
        const size_t parameter_count = static_cast<size_t>(std::max(
            0.0, table_cell_number(*row, "parameterCount")));
        auto parameter_label = [&](size_t index) -> std::string {
            if (method == "track.position") {
                static const char* labels[] = {
                    "x",
                    "y",
                    "radiusH",
                    "radiusV",
                };
                if (index < std::size(labels)) return labels[index];
            } else if (method == "track.x.interpolate") {
                return index == 0 ? "x" : "radius";
            } else if (method == "track.y.interpolate") {
                return index == 0 ? "y" : "radius";
            } else if (method == "track.gauge" ||
                       method == "track.cant.setgauge") {
                return "gauge";
            } else if (method == "track.cant.setcenter") {
                return "x";
            } else if (method == "track.cant.setfunction") {
                return "id";
            } else if (method == "track.cant" ||
                       method == "track.cant.begin" ||
                       method == "track.cant.interpolate") {
                return "cant";
            }
            return "Parameter " + std::to_string(index + 1);
        };
        for (size_t index = 0; index < parameter_count; ++index) {
            const std::string key = "parameter" + std::to_string(index);
            add_row_field(key, parameter_label(index),
                          MapElementNumericConstraint::Finite, true);
        }
    } else if (request.row_kind == "signal.put") {
        add_row_field("distance", "distance", MapElementNumericConstraint::Finite, true);
        add_row_field("signalAspectKey", "signalAspectKey",
                      MapElementNumericConstraint::None, true);
        add_row_field("section", "section", MapElementNumericConstraint::Finite, true);
        add_row_field("trackKey", "trackKey", MapElementNumericConstraint::None, false);
        for (const char* key : {"x", "y", "z", "rx", "ry", "rz", "tilt", "span"}) {
            add_row_field(key, key, structure_edit_numeric_constraint(key), true);
            if (std::strcmp(key, "z") == 0 || std::strcmp(key, "rx") == 0 ||
                std::strcmp(key, "ry") == 0 || std::strcmp(key, "rz") == 0 ||
                std::strcmp(key, "tilt") == 0 || std::strcmp(key, "span") == 0) {
                next.fields.back().requires_signal_full_form = true;
            }
        }
    } else if (request.row_kind == "repeater") {
        const repeater_linkage::Linkage repeater_linkage =
            repeater_linkage::pair_linkage(table_repeater_events(model_.repeaters));
        const auto linked = std::find_if(
            repeater_linkage.segments.begin(), repeater_linkage.segments.end(),
            [&](const repeater_linkage::Segment& segment) {
                return segment.begin_source_index < model_.repeaters.size() &&
                    model_.repeaters[segment.begin_source_index].edit_id == edit_id;
            });
        if (linked == repeater_linkage.segments.end() ||
            linked->chain_index >= repeater_linkage.chains.size()) {
            KME_ADD_LOG("[warn]Repeater inspector target is not a Begin statement: " +
                    edit_id);
            return false;
        }
        const repeater_linkage::Chain& repeater_chain =
            repeater_linkage.chains[linked->chain_index];
        next.repeater_chain_edit_ids.reserve(
            repeater_chain.begin_source_indices.size() +
            (repeater_chain.end_source_index ? 1u : 0u));
        for (size_t source_index : repeater_chain.begin_source_indices) {
            if (source_index >= model_.repeaters.size() ||
                model_.repeaters[source_index].edit_id.empty()) {
                KME_ADD_LOG("[warn]Repeater chain is missing editable Begin metadata");
                return false;
            }
            next.repeater_chain_edit_ids.push_back(
                model_.repeaters[source_index].edit_id);
        }
        if (repeater_chain.end_source_index) {
            if (*repeater_chain.end_source_index >= model_.repeaters.size() ||
                model_.repeaters[*repeater_chain.end_source_index].edit_id.empty()) {
                KME_ADD_LOG("[warn]Repeater chain is missing editable End metadata");
                return false;
            }
            next.repeater_chain_edit_ids.push_back(
                model_.repeaters[*repeater_chain.end_source_index].edit_id);
        }

        const std::string method = table_cell(*row, "method");
        const std::string original_method = table_cell(*original_row, "method");
        next.source_zero_offset_method = ascii_lower(original_method) == "begin0";
        next.coordinate_offsets_enabled = ascii_lower(method) != "begin0";
        next.repeater_boundary_kind =
            linked->boundary_kind == repeater_linkage::BoundaryKind::ExplicitEnd ? "end" :
            linked->boundary_kind == repeater_linkage::BoundaryKind::NextBegin ? "change" : "open";
        next.repeater_scene_row_index = linked->display_index - 1;
        const auto begin_edit_id_at = [&](const std::optional<size_t>& source_index) {
            if (!source_index || *source_index >= model_.repeaters.size()) return std::string{};
            return model_.repeaters[*source_index].edit_id;
        };
        next.repeater_previous_begin_edit_id =
            begin_edit_id_at(linked->previous_begin_source_index);
        if (linked->boundary_kind == repeater_linkage::BoundaryKind::NextBegin) {
            next.repeater_next_begin_edit_id =
                begin_edit_id_at(linked->boundary_source_index);
        }
        next.repeater_has_multiple_begins = !next.repeater_previous_begin_edit_id.empty() ||
            !next.repeater_next_begin_edit_id.empty();

        add_row_field("distance", "beginDistance", MapElementNumericConstraint::Finite, true);
        add_row_field("repeaterKey", "repeaterKey", MapElementNumericConstraint::None, true);
        add_row_field("trackKey", "trackKey", MapElementNumericConstraint::None, false);
        for (const char* key : {"x", "y", "z", "rx", "ry", "rz"}) {
            add_row_field(key, key, structure_edit_numeric_constraint(key), true);
        }
        add_row_field("tilt", "tilt", structure_edit_numeric_constraint("tilt"), true);
        add_row_field("span", "span", structure_edit_numeric_constraint("span"), true);
        add_row_field("interval", "interval", MapElementNumericConstraint::Finite, true);

        const std::vector<std::string> current_keys =
            split_repeater_structure_keys(table_cell(*row, "structureKeys"));
        next.repeater_structure_keys_original =
            split_repeater_structure_keys(table_cell(*original_row, "structureKeys"));
        for (size_t key_index = 0; key_index < current_keys.size(); ++key_index) {
            const std::string original_key = key_index < next.repeater_structure_keys_original.size()
                ? next.repeater_structure_keys_original[key_index]
                : std::string{};
            add_row_field("structureKeys." + std::to_string(key_index),
                          "structureKey[" + std::to_string(key_index + 1) + "]",
                          MapElementNumericConstraint::None, true);
            MapElementEditFieldState& field = next.fields.back();
            field.original_value = original_key;
            set_edit_field_buffer(field, current_keys[key_index]);
        }

        if (linked->boundary_kind == repeater_linkage::BoundaryKind::ExplicitEnd &&
            linked->boundary_source_index &&
            *linked->boundary_source_index < model_.repeaters.size()) {
            const TableRow& end_row = model_.repeaters[*linked->boundary_source_index];
            const TableRow* original_end_row = &end_row;
            const auto end_snapshot = original_edit_rows_.find(end_row.edit_id);
            if (end_snapshot != original_edit_rows_.end()) {
                original_end_row = &end_snapshot->second.row;
            }
            std::string end_info_error;
            const std::optional<InspectorTargetMetadata> end_info =
                resolve_inspector_target_metadata(handle_, end_row.edit_id, "repeater",
                                                  &end_info_error);
            if (!end_info && !end_info_error.empty()) {
                KME_ADD_LOG("[warn]Repeater End metadata fallback: " + end_info_error);
            }
            EditSourceInfo end_source = end_info ? end_info->source : end_row.source;
            if (end_source.file_path.empty()) end_source = end_row.source;
            next.end_source_file = end_source.file_path;
            next.end_source_file_name = display_name_from_path(next.end_source_file);
            next.end_line = end_source.line;
            next.end_column = end_source.column;
            next.end_raw_statement = end_info && !end_info->raw_statement.empty()
                ? end_info->raw_statement
                : end_source.raw_text_preview;
            next.end_source_distance_string = end_info ? end_info->source_distance_string : "";
            next.end_expected_source_hash = end_info ? end_info->expected_source_hash : "";
            if (next.end_expected_source_hash.empty()) {
                const EditSourceFileInfo* end_file =
                    find_model_source_file(model_, next.end_source_file);
                if (end_file) next.end_expected_source_hash = end_file->source_hash;
            }
            next.owned_edit_ids.push_back(end_row.edit_id);
            add_related_field("endDistance", "distance", "endDistance",
                              inspector_row_field_value(end_row, "repeater", "distance"),
                              inspector_row_field_value(*original_end_row, "repeater", "distance"),
                              MapElementNumericConstraint::Finite, true, end_row.edit_id,
                              next.end_expected_source_hash,
                              next.end_source_distance_string);
        }
    }

    for (MapElementEditFieldState& field : next.fields) {
        const std::string& field_edit_id = field.target_edit_id.empty()
            ? edit_id
            : field.target_edit_id;
        const auto pending = pending_edit_changes_.find(field_edit_id);
        if (pending == pending_edit_changes_.end()) continue;
        const std::string& backend_key = field.backend_key.empty() ? field.key : field.backend_key;
        const auto field_it = pending->second.field_changes.find(backend_key);
        if (field_it != pending->second.field_changes.end()) {
            set_edit_field_buffer(field, field_it->second);
        }
    }

    inspector_ = std::move(next);
    inspector_.expected_source_hash =
        inspector_expected_source_hash(model_, pending_edit_changes_, inspector_);
    if (inspector_.expected_source_hash.empty() && target_info) {
        inspector_.expected_source_hash = target_info->source_hash;
    }
    for (MapElementEditFieldState& field : inspector_.fields) {
        if (field.target_edit_id == inspector_.edit_id && field.expected_source_hash.empty()) {
            field.expected_source_hash = inspector_.expected_source_hash;
        }
    }

    restore_repeater_inspector_draft(inspector_);
    return true;
}

bool App::open_element_inspector(const std::string& edit_id, const std::string& row_kind) {
    return open_element_inspector(MapElementInspectorRequest{edit_id, row_kind});
}

bool App::navigate_repeater_inspector(bool toward_next) {
    if (!inspector_.open || inspector_.row_kind != "repeater") return false;
    const std::string target_edit_id = toward_next
        ? inspector_.repeater_next_begin_edit_id
        : inspector_.repeater_previous_begin_edit_id;
    if (target_edit_id.empty()) return false;

    capture_repeater_inspector_draft(inspector_);
    MapElementInspectorRequest request{target_edit_id, "repeater"};
    request.inspector_session = inspector_.session;
    if (!open_element_inspector(request)) return false;

    if (scene_preview_started_ && scene_preview_canvas_ && scene_preview_canvas_->has_scene()) {
        locate_repeater_row_in_scene_preview(inspector_.repeater_scene_row_index);
    }
    return true;
}

void App::clear_scene_placement_edit_target() {
    if (scene_preview_canvas_) scene_preview_canvas_->clear_scene_placement_edit_target();
}

void App::sync_scene_placement_edit_from_inspector() {
    const bool structure_target = inspector_.row_kind == "structure.put";
    const bool structure_between_target = inspector_.row_kind == "structure.between";
    const bool signal_target = inspector_.row_kind == "signal.put";
    const bool repeater_target = inspector_.row_kind == "repeater";
    const bool sound3d_target = inspector_.row_kind == "mapSound3D.put";
    if (!scene_preview_started_ || !scene_preview_canvas_ || !inspector_.open ||
        (!structure_target && !structure_between_target && !signal_target &&
         !repeater_target && !sound3d_target)) {
        clear_scene_placement_edit_target();
        return;
    }

    const std::vector<TableRow>& rows = structure_target ? model_.structures :
        structure_between_target ? model_.structures_between :
        signal_target ? model_.signals :
        repeater_target ? model_.repeaters : model_.map_sound_3d;
    size_t row_index = inspector_.model_row_index;
    const bool cache_hit = inspector_.model_row_source_revision == plan_data_source_revision_ &&
        row_index < rows.size() && rows[row_index].edit_id == inspector_.edit_id;
    if (!cache_hit && !find_row_index_by_edit_id(rows, inspector_.edit_id, row_index)) {
        clear_scene_placement_edit_target();
        return;
    }
#ifndef NDEBUG
    if (!cache_hit) ++inspector_.model_row_cache_scans;
#endif
    if (!cache_hit) {
        inspector_.model_row_index = row_index;
        inspector_.model_row_source_revision = plan_data_source_revision_;
    }
    Canvas3DPlacementEditTarget target = scene_edit_target_from_row(
        rows[row_index],
        repeater_target ? Canvas3DSceneEditKind::Repeater
            : signal_target ? Canvas3DSceneEditKind::Signal
            : structure_between_target
                ? Canvas3DSceneEditKind::StructurePutBetween
                : sound3d_target ? Canvas3DSceneEditKind::Sound3D
                                 : Canvas3DSceneEditKind::Structure);
    const double model_distance = target.distance;
    const std::string model_track_key = target.track_key;
    if (const MapElementEditFieldState* distance_field =
            find_inspector_field(inspector_, "distance")) {
        if (!parse_gui_edit_number(edit_field_buffer_text(*distance_field), &target.distance)) return;
    }
    if (const MapElementEditFieldState* track_field =
            find_inspector_field(inspector_, "trackKey")) {
        target.track_key = trim_gui_ascii_copy(edit_field_buffer_text(*track_field));
    }
    if (repeater_target) {
        if (const MapElementEditFieldState* end_distance_field =
                find_inspector_field(inspector_, "endDistance")) {
            if (!parse_gui_edit_number(
                    edit_field_buffer_text(*end_distance_field),
                    &target.repeater_end_distance)) {
                return;
            }
            target.has_repeater_end_distance = true;
        }
    }
    if (structure_between_target) {
        const MapElementEditFieldState* structure_key_field =
            find_inspector_field(inspector_, "structureKey");
        if (!structure_key_field) return;
        const std::string structure_key = trim_gui_ascii_copy(
            edit_field_buffer_text(*structure_key_field));
        target.model_path = structure_model_path_for_key(model_, structure_key);
        if (target.model_path.empty()) return;
        const MapElementEditFieldState* track1_field =
            find_inspector_field(inspector_, "trackKey1");
        const MapElementEditFieldState* track2_field =
            find_inspector_field(inspector_, "trackKey2");
        const MapElementEditFieldState* flag_field =
            find_inspector_field(inspector_, "flag");
        double flag = 0.0;
        if (!track1_field || !track2_field || !flag_field ||
            !parse_gui_edit_number(edit_field_buffer_text(*flag_field), &flag)) {
            return;
        }
        target.put_between_track_key1 = trim_gui_ascii_copy(
            edit_field_buffer_text(*track1_field));
        target.put_between_track_key2 = trim_gui_ascii_copy(
            edit_field_buffer_text(*track2_field));
        target.put_between_flag = kme::truncating_int_or_zero(flag) & 1;
    }
    for (const char* key : {"x", "y", "z", "rx", "ry", "rz", "tilt", "span"}) {
        const MapElementEditFieldState* field = find_inspector_field(inspector_, key);
        if (!field) continue;
        double value = 0.0;
        if (!parse_gui_edit_number(edit_field_buffer_text(*field), &value)) return;
        if (field->numeric_constraint == MapElementNumericConstraint::Tilt &&
            gui_numeric_choice_option_index(value, field->numeric_constraint) < 0) {
            return;
        }
        if (field->numeric_constraint == MapElementNumericConstraint::Truncate3 &&
            trim_gui_ascii_copy(edit_field_buffer_text(*field)) != field->original_value) {
            value = truncate_gui_thousandths(value);
        }
        if (std::strcmp(key, "x") == 0) target.x = value;
        else if (std::strcmp(key, "y") == 0) target.y = value;
        else if (std::strcmp(key, "z") == 0) target.z = value;
        else if (std::strcmp(key, "rx") == 0) target.rx = value;
        else if (std::strcmp(key, "ry") == 0) target.ry = value;
        else if (std::strcmp(key, "rz") == 0) target.rz = value;
        else if (std::strcmp(key, "tilt") == 0) target.tilt = value;
        else if (std::strcmp(key, "span") == 0) target.span = value;
    }
    if (repeater_target &&
        target.track_key != model_track_key) {
        clear_scene_placement_edit_target();
        return;
    }
    const bool coordinate_offset_target = structure_target || repeater_target;
    target.placement_distance_gizmo =
        coordinate_offset_target && !inspector_.coordinate_offsets_enabled;
    if (repeater_target && !target.placement_distance_gizmo &&
        std::fabs(target.distance - model_distance) > 1e-9) {
        clear_scene_placement_edit_target();
        return;
    }
    const bool show_gizmo = !coordinate_offset_target ||
        inspector_.coordinate_offsets_enabled || target.placement_distance_gizmo;
    if (repeater_target) {
        scene_preview_canvas_->set_scene_repeater_edit_target(target, show_gizmo);
    } else {
        scene_preview_canvas_->set_scene_placement_edit_target(target, show_gizmo);
    }
}

void App::apply_scene_placement_drag_update(const Canvas3DPlacementDragUpdate& update) {
    const bool matching_kind =
        (update.kind == Canvas3DSceneEditKind::Structure && inspector_.row_kind == "structure.put") ||
        (update.kind == Canvas3DSceneEditKind::StructurePutBetween &&
         inspector_.row_kind == "structure.between") ||
        (update.kind == Canvas3DSceneEditKind::Signal && inspector_.row_kind == "signal.put") ||
        (update.kind == Canvas3DSceneEditKind::Repeater && inspector_.row_kind == "repeater") ||
        (update.kind == Canvas3DSceneEditKind::Sound3D &&
         inspector_.row_kind == "mapSound3D.put");
    if (!inspector_.open || !matching_kind ||
        inspector_.edit_id != update.edit_id) {
        return;
    }
    const char* field_key = nullptr;
    double value = 0.0;
    if (update.target == Canvas3DSceneDragTarget::RepeaterEndDistance) {
        field_key = "endDistance";
        value = update.repeater_end_distance;
    } else if (update.target == Canvas3DSceneDragTarget::PutBetweenDistance ||
               update.target == Canvas3DSceneDragTarget::PlacementDistance) {
        field_key = "distance";
        value = update.distance;
    } else if (update.axis == Canvas3DSceneDragAxis::X) {
        field_key = "x";
        value = update.x;
    } else if (update.axis == Canvas3DSceneDragAxis::Y) {
        field_key = "y";
        value = update.y;
    } else if (update.axis == Canvas3DSceneDragAxis::Z) {
        field_key = "z";
        value = update.z;
    }
    if (field_key) {
        if (MapElementEditFieldState* field = find_inspector_field(inspector_, field_key)) {
            if (field->requires_signal_full_form &&
                inspector_.source_signal_short_form &&
                !inspector_.signal_full_form_conversion_draft) {
                inspector_.pending_signal_full_form_field = field_key;
                inspector_.pending_signal_full_form_value =
                    format_gui_transform_number(value);
                inspector_.signal_full_form_prompt_requested = true;
                return;
            }
            const std::string formatted =
                update.target == Canvas3DSceneDragTarget::RepeaterEndDistance ||
                update.target == Canvas3DSceneDragTarget::PlacementDistance
                    ? format_double(value, 0)
                    : format_gui_transform_number(value);
            set_edit_field_buffer(*field, formatted);
        }
    }
}

void App::apply_inspector_changes() {
    if (!edit_actions_available()) return;
    if (!inspector_.open || inspector_.edit_id.empty()) return;
    const bool repeater_inspector = inspector_.row_kind == "repeater";
    const std::string repeater_draft_edit_id = inspector_.edit_id;
    if (distance_resolution_workflow_.phase != DistanceResolutionPhase::None ||
        distance_resolution_workflow_.retry_requested) {
        cancel_distance_resolution_workflow();
    }
    std::map<std::string, MapElementPendingChange> replacements;
    auto change_for = [&](const MapElementEditFieldState& field)
        -> MapElementPendingChange& {
        const std::string target_edit_id = field.target_edit_id.empty()
            ? inspector_.edit_id
            : field.target_edit_id;
        auto [it, inserted] = replacements.try_emplace(target_edit_id);
        MapElementPendingChange& change = it->second;
        if (inserted) {
            change.change_id = "change-" + target_edit_id;
            change.edit_id = target_edit_id;
            change.row_kind = inspector_.row_kind;
            change.operation = "update";
            change.expected_source_hash = field.expected_source_hash;
            if (change.expected_source_hash.empty() && target_edit_id == inspector_.edit_id) {
                change.expected_source_hash =
                    inspector_expected_source_hash(model_, pending_edit_changes_, inspector_);
            }
            if (change.expected_source_hash.empty() && target_edit_id != inspector_.edit_id) {
                change.expected_source_hash = inspector_.end_expected_source_hash;
            }
        }
        return change;
    };

    std::vector<std::string> repeater_structure_keys;
    std::optional<std::string> repeater_key_value;
    std::optional<std::string> repeater_key_original;
    for (MapElementEditFieldState& field : inspector_.fields) {
        if (field.read_only) continue;
        std::string value = trim_gui_ascii_copy(edit_field_buffer_text(field));
        if (field.required && value.empty()) {
            set_program_status("status.edit.required_field");
            return;
        }
        const bool field_changed = value != field.original_value;
        if (!validate_and_canonicalize_edit_field(field, field_changed)) {
            if (inspector_.row_kind == "station.put" &&
                (field.key == "margin1" || field.key == "margin2")) {
                set_program_status("status.edit.station_margin_invalid");
            } else {
                set_program_status("status.edit.invalid_number");
            }
            return;
        }
        value = trim_gui_ascii_copy(edit_field_buffer_text(field));
        if (repeater_inspector && field.key == "repeaterKey") {
            repeater_key_value = value;
            repeater_key_original = trim_gui_ascii_copy(field.original_value);
        }
        if (inspector_.row_kind == "repeater" && is_repeater_structure_key_field(field)) {
            repeater_structure_keys.push_back(value);
            continue;
        }
        if ((inspector_.row_kind == "section.begin" ||
             inspector_.row_kind == "section.speedLimit") &&
            is_section_values_field(field)) {
            continue;
        }
        if (value != field.original_value) {
            MapElementPendingChange& change = change_for(field);
            const std::string& backend_key = field.backend_key.empty() ? field.key : field.backend_key;
            change.field_changes[backend_key] = value;
        }
    }

    auto inspector_field_is_blank = [&](const char* key) {
        const MapElementEditFieldState* field = find_inspector_field(inspector_, key);
        return !field || trim_gui_ascii_copy(edit_field_buffer_text(*field)).empty();
    };
    if (inspector_.row_kind == "adhesion.change" &&
        inspector_field_is_blank("b") != inspector_field_is_blank("c")) {
        set_program_status("status.edit.required_field");
        return;
    }
    if (inspector_.row_kind == "fog.change") {
        const bool density = !inspector_field_is_blank("density");
        const bool red = !inspector_field_is_blank("red");
        const bool green = !inspector_field_is_blank("green");
        const bool blue = !inspector_field_is_blank("blue");
        const bool all_colors = red && green && blue;
        const bool source_set = ascii_lower(inspector_.statement_kind) == "fog.set";
        if ((red || green || blue) != all_colors || (all_colors && !density) ||
            (source_set && (!density || !all_colors))) {
            set_program_status("status.edit.required_field");
            return;
        }
    }

    if (inspector_.row_kind == "repeater" &&
        repeater_structure_keys != inspector_.repeater_structure_keys_original) {
        if (repeater_structure_keys.empty()) {
            set_program_status("status.edit.required_field");
            return;
        }
        MapElementEditFieldState primary_field;
        primary_field.target_edit_id = inspector_.edit_id;
        primary_field.expected_source_hash = inspector_.expected_source_hash;
        MapElementPendingChange& change = change_for(primary_field);
        change.field_changes["structureKeys.count"] =
            std::to_string(repeater_structure_keys.size());
        for (size_t index = 0; index < repeater_structure_keys.size(); ++index) {
            change.field_changes["structureKeys." + std::to_string(index)] =
                repeater_structure_keys[index];
        }
    }

    if (inspector_.row_kind == "section.begin" ||
        inspector_.row_kind == "section.speedLimit") {
        std::vector<std::string> section_values;
        section_values.reserve(inspector_.fields.size());
        for (const MapElementEditFieldState& field : inspector_.fields) {
            if (!is_section_values_field(field)) continue;
            section_values.push_back(trim_gui_ascii_copy(edit_field_buffer_text(field)));
        }
        const bool count_changed =
            section_values.size() != inspector_.section_values_original.size();
        const bool any_value_changed = [&]() {
            for (const MapElementEditFieldState& field : inspector_.fields) {
                if (!is_section_values_field(field)) continue;
                if (trim_gui_ascii_copy(edit_field_buffer_text(field)) !=
                    field.original_value) {
                    return true;
                }
            }
            return false;
        }();
        if (count_changed || any_value_changed) {
            if (section_values.empty()) {
                set_program_status("status.edit.required_field");
                return;
            }
            MapElementEditFieldState primary_field;
            primary_field.target_edit_id = inspector_.edit_id;
            primary_field.expected_source_hash = inspector_.expected_source_hash;
            MapElementPendingChange& change = change_for(primary_field);
            if (count_changed) {
                change.field_changes["values.count"] =
                    std::to_string(section_values.size());
                for (size_t index = 0; index < section_values.size(); ++index) {
                    change.field_changes["values." + std::to_string(index)] =
                        section_values[index];
                }
            } else {
                size_t value_index = 0;
                for (const MapElementEditFieldState& field : inspector_.fields) {
                    if (!is_section_values_field(field)) continue;
                    const std::string value =
                        trim_gui_ascii_copy(edit_field_buffer_text(field));
                    if (value != field.original_value) {
                        change.field_changes["values." + std::to_string(value_index)] =
                            value;
                    }
                    ++value_index;
                }
            }
        }
    }

    const bool coordinate_offset_inspector =
        inspector_.row_kind == "structure.put" || inspector_.row_kind == "repeater";
    const bool draft_zero_offset_method =
        coordinate_offset_inspector && !inspector_.coordinate_offsets_enabled;
    if (coordinate_offset_inspector &&
        inspector_.source_zero_offset_method != draft_zero_offset_method) {
        MapElementEditFieldState primary_field;
        primary_field.target_edit_id = inspector_.edit_id;
        primary_field.expected_source_hash = inspector_.expected_source_hash;
        MapElementPendingChange& change = change_for(primary_field);
        if (inspector_.row_kind == "repeater") {
            change.field_changes["method"] = draft_zero_offset_method ? "Begin0" : "Begin";
        } else {
            change.field_changes["method"] = draft_zero_offset_method ? "Put0" : "Put";
        }
    }
    if (inspector_.row_kind == "signal.put" &&
        inspector_.source_signal_short_form &&
        inspector_.signal_full_form_conversion_draft) {
        MapElementEditFieldState primary_field;
        primary_field.target_edit_id = inspector_.edit_id;
        primary_field.expected_source_hash = inspector_.expected_source_hash;
        MapElementPendingChange& change = change_for(primary_field);
        change.field_changes["form"] = "full";
    }

    if (coordinate_offset_inspector &&
        inspector_.coordinate_offsets_enabled) {
        const MapElementEditFieldState* z_field = find_inspector_field(inspector_, "z");
        double z_value = 0.0;
        if (z_field && parse_gui_edit_number(edit_field_buffer_text(*z_field), &z_value) &&
            std::abs(truncate_gui_thousandths(z_value)) > 5.0) {
            inspector_.z_rebase_prompt_requested = true;
            return;
        }
    }

    // Editing a row that was created by an unfinished insert must merge into
    // the pending insert change: the insert edit id does not exist on disk, so
    // a plain update could never be replayed after a working-copy reset. The
    // merged change starts from the original insert so fields not shown in the
    // inspector (such as the selected method) and its resolution/placement
    // state survive, then the current implicit changes and form values win.
    for (auto& item : replacements) {
        auto existing = pending_edit_changes_.find(item.first);
        if (existing == pending_edit_changes_.end() ||
            existing->second.operation != "insert") {
            continue;
        }
        MapElementPendingChange& change = item.second;
        MapElementPendingChange merged = existing->second;
        // Variable-length Section fields are rebuilt from the current form;
        // retaining removed indices would make a reduced count invalid.
        if (inspector_.row_kind == "section.begin" ||
            inspector_.row_kind == "section.speedLimit") {
            for (auto field = merged.field_changes.begin();
                 field != merged.field_changes.end();) {
                if (is_section_values_field(field->first)) {
                    field = merged.field_changes.erase(field);
                } else {
                    ++field;
                }
            }
        }
        for (const auto& field : change.field_changes) {
            merged.field_changes.insert_or_assign(field.first, field.second);
        }
        const auto method = change.field_changes.find("method");
        if (method != change.field_changes.end()) {
            const std::string normalized_method = ascii_lower(method->second);
            const bool long_coordinate_form =
                normalized_method == "begin" || normalized_method == "put";
            for (const char* key : {"x", "y", "z", "rx", "ry", "rz"}) {
                if (!long_coordinate_form) {
                    merged.field_changes.erase(key);
                    continue;
                }
                const MapElementEditFieldState* field =
                    find_inspector_field(inspector_, key);
                if (field) {
                    merged.field_changes[key] =
                        trim_gui_ascii_copy(edit_field_buffer_text(*field));
                }
            }
        }
        if (change.field_changes.find("structureKeys.count") !=
            change.field_changes.end()) {
            for (auto field = merged.field_changes.begin();
                 field != merged.field_changes.end();) {
                if (field->first.rfind("structureKeys.", 0) == 0) {
                    field = merged.field_changes.erase(field);
                } else {
                    ++field;
                }
            }
            for (const auto& field : change.field_changes) {
                if (field.first.rfind("structureKeys.", 0) == 0) {
                    merged.field_changes[field.first] = field.second;
                }
            }
        }
        size_t section_value_count = 0;
        for (const MapElementEditFieldState& field : inspector_.fields) {
            if (!is_section_values_field(field)) continue;
            ++section_value_count;
            const std::string& backend_key =
                field.backend_key.empty() ? field.key : field.backend_key;
            merged.field_changes[backend_key] =
                trim_gui_ascii_copy(edit_field_buffer_text(field));
        }
        if (section_value_count > 0) {
            merged.field_changes["values.count"] = std::to_string(section_value_count);
        }
        change = std::move(merged);
    }

    std::map<std::string, MapElementPendingChange> candidate = pending_edit_changes_;
    for (const std::string& owned_edit_id : inspector_.owned_edit_ids) {
        const auto existing = candidate.find(owned_edit_id);
        if (existing != candidate.end() &&
            existing->second.operation == "insert") {
            // An Inspector owns every physical row represented by its form,
            // but an unfinished insert is also the operation that creates
            // that row. Keep untouched insert shells in the replay ledger;
            // replacements below merge changed fields into the shells they
            // actually target.
            continue;
        }
        candidate.erase(owned_edit_id);
    }

    for (auto& item : replacements) {
        MapElementPendingChange& change = item.second;
        auto existing_change = pending_edit_changes_.find(change.edit_id);
        if (existing_change == pending_edit_changes_.end()) continue;
        auto old_distance = existing_change->second.field_changes.find("distance");
        auto new_distance = change.field_changes.find("distance");
        if (old_distance != existing_change->second.field_changes.end() &&
            new_distance != change.field_changes.end() &&
            old_distance->second == new_distance->second) {
            change.distance_resolution_key = existing_change->second.distance_resolution_key;
            change.distance_boundary_token = existing_change->second.distance_boundary_token;
            change.distance_expression = existing_change->second.distance_expression;
            change.confirm_environment_mismatch =
                existing_change->second.confirm_environment_mismatch;
        }
    }

    for (auto& item : replacements) {
        candidate[item.first] = std::move(item.second);
    }

    const bool repeater_chain_has_pending_key = repeater_inspector &&
        std::any_of(inspector_.repeater_chain_edit_ids.begin(),
                    inspector_.repeater_chain_edit_ids.end(),
                    [&](const std::string& edit_id) {
                        auto pending = pending_edit_changes_.find(edit_id);
                        return pending != pending_edit_changes_.end() &&
                            pending->second.operation == "update" &&
                            pending->second.field_changes.find("repeaterKey") !=
                                pending->second.field_changes.end();
                    });
    const bool repeater_key_sync_requested = repeater_inspector &&
        repeater_key_value && repeater_key_original &&
        (*repeater_key_value != *repeater_key_original ||
         repeater_chain_has_pending_key);
    if (repeater_key_sync_requested) {
        if (inspector_.repeater_chain_edit_ids.empty()) {
            KME_ADD_LOG("[error]Repeater key edit has no linked chain metadata");
            set_program_status("status.edit.pending");
            return;
        }
        for (const std::string& chain_edit_id : inspector_.repeater_chain_edit_ids) {
            if (*repeater_key_value == *repeater_key_original) {
                auto existing = candidate.find(chain_edit_id);
                if (existing == candidate.end() ||
                    existing->second.operation != "update") {
                    continue;
                }
                existing->second.field_changes.erase("repeaterKey");
                if (existing->second.field_changes.empty()) candidate.erase(existing);
                continue;
            }

            size_t row_index = 0;
            if (!find_row_index_by_edit_id(
                    model_.repeaters, chain_edit_id, row_index)) {
                KME_ADD_LOG("[error]Repeater key edit lost a linked chain row: " +
                        chain_edit_id);
                set_program_status("status.edit.pending");
                return;
            }
            auto [entry, inserted] = candidate.try_emplace(chain_edit_id);
            MapElementPendingChange& change = entry->second;
            if (inserted) {
                change.change_id = "change-" + chain_edit_id;
                change.edit_id = chain_edit_id;
                change.row_kind = "repeater";
                change.operation = "update";
            }
            if (change.operation != "update" && change.operation != "insert") {
                KME_ADD_LOG("[error]Repeater key edit conflicts with a linked "
                        "non-update/insert operation: " + chain_edit_id);
                set_program_status("status.edit.pending");
                return;
            }
            // Inserts have no disk-baseline row hash. Retaining an empty hash
            // lets maploader use the handle's authoritative baseline, matching
            // the new-element path and avoiding a working-copy hash as a false
            // external-change guard during repeated Apply operations.
            if (change.operation == "update" &&
                change.expected_source_hash.empty()) {
                const TableRow& chain_row = model_.repeaters[row_index];
                change.expected_source_hash = expected_source_hash_for_edit_target(
                    model_, pending_edit_changes_, chain_edit_id, std::string{},
                    chain_row.source.file_path);
            }
            change.field_changes["repeaterKey"] = *repeater_key_value;
        }
    }

    if (replacements.empty() && !repeater_key_sync_requested) {
        if (apply_edit_ledger_to_preview(candidate, std::nullopt, false)) {
            if (repeater_inspector) {
                inspector_.session.repeater_drafts.erase(repeater_draft_edit_id);
            }
            set_program_status("status.edit.no_changes");
        }
        return;
    }
    std::optional<MapElementInspectorRequest> reload_request =
        make_inspector_reload_request(inspector_);
    if (repeater_inspector && reload_request->inspector_session) {
        reload_request->inspector_session->repeater_drafts.erase(repeater_draft_edit_id);
    }
    if (!apply_edit_ledger_to_preview(candidate, std::move(reload_request), false,
                                      inspector_.edit_id)) {
        if (distance_resolution_workflow_.phase == DistanceResolutionPhase::None &&
            !distance_resolution_workflow_.retry_requested &&
            std::string_view(program_status_key_) !=
                "status.edit.repeater_key_conflict") {
            set_program_status("status.edit.pending");
        }
    }
}
