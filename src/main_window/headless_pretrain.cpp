/*
 * Copyright (c) 2026 Sapporo_ningyo
 *
 * Licensed under Apache License 2.0; see LICENSE and NOTICE.
 */

#include "kme.h"
#include "app_settings.h"
#include "debug_headless.h"
#include "canvas3D.h"
#include "maploader.h"
#include "../canvas3d/canvas3d_scene_data.h"
#include "imgui.h"
#include "implot.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <set>
#include <stdexcept>

#ifndef NDEBUG
int App::run_debug_headless_pretrain_edit(const HeadlessPreTrainEditOptions& options) {
    std::ofstream output_file;
    std::ostream* out = &std::cout;
    if (!options.output_path.empty()) {
        output_file.open(std::filesystem::u8path(options.output_path),
                         std::ios::binary | std::ios::trunc);
        if (!output_file) return 1;
        out = &output_file;
    }
    *out << "command=debug-headless-pretrain-edit\npath=" << options.path
         << "\nmemory_apply_only=1\nstage=preview-load-start\n";
    out->flush();
    LoadModelOptions preview_options;
    preview_options.load_profile = "preview";
    LoadResult preview = load_map_worker(options.path, options.unit_distance,
        false, 0.0, 0.0, options.unit_distance, preview_options);
    LoadModelOptions edit_options;
    edit_options.full_edit_registry = true;
    edit_options.load_profile = "edit";
    LoadResult metadata = load_map_worker(options.path, options.unit_distance,
        false, 0.0, 0.0, options.unit_distance, edit_options);
    if (!preview.ok || !metadata.ok) {
        *out << "load_error=" << preview.error << " " << metadata.error << "\nresult=FAIL\n";
        if (preview.handle) kv_free(preview.handle);
        if (metadata.handle) kv_free(metadata.handle);
        return 2;
    }
    int failures = 0;
    const auto check = [&](const char* name, bool ok) {
        *out << name << "=" << (ok ? 1 : 0) << "\n";
        out->flush();
        if (!ok) ++failures;
        return ok;
    };
    ImGui::CreateContext();
    ImPlot::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(1280, 720);
    io.DeltaTime = 1.0f / 60.0f;
    io.IniFilename = nullptr;
    io.Fonts->AddFontDefault();
    io.Fonts->Build();
    ImGui::NewFrame();
    try {
        UserSettings settings;
        settings.language = Language::En;
        App app(nullptr, settings, 1.0f, false, false);
        app.handle_ = preview.handle;
        preview.handle = nullptr;
        app.model_ = std::move(preview.model);
        app.file_path_ = options.path;
        app.has_model_ = true;
        app.edit_mode_enabled_ = true;
        app.edit_memory_matches_pending_ledger_ = true;
        app.dmin_ = app.model_.default_min;
        app.dmax_ = app.model_.default_max;
        app.unit_distance_ = options.unit_distance;
        app.apply_edit_metadata_result(std::move(metadata));
        metadata.handle = nullptr;
        check("edit_metadata_merged", app.edit_registry_loaded_);
        *out << "stage=edit-metadata-merge-complete\n";
        const std::vector<TableRow> baseline = app.model_.pretrains;
        *out << "baseline_pretrain_count=" << baseline.size() << "\n";
        if (baseline.empty()) throw std::runtime_error("map has no existing PreTrain.Pass rows");
        // The UI's default station range can end before the final pass point.
        // Include every tested target, as a user would by extending the range.
        for (const TableRow& row : baseline) {
            app.dmin_ = std::min(app.dmin_, table_cell_number(row, "distance"));
            app.dmax_ = std::max(app.dmax_, table_cell_number(row, "distance"));
        }
        const auto read_bytes = [](const std::string& path) {
            std::ifstream input(std::filesystem::u8path(path), std::ios::binary);
            if (!input) throw std::runtime_error("cannot read protected source: " + path);
            return std::string(std::istreambuf_iterator<char>(input), {});
        };
        std::map<std::string, std::string> disk;
        for (const EditSourceFileInfo& source : app.model_.edit_files) {
            disk.emplace(source.file_path, read_bytes(source.file_path));
        }
        check("protected_sources_present", !disk.empty());
        const auto disk_unchanged = [&]() {
            return std::all_of(disk.begin(), disk.end(), [&](const auto& item) {
                return read_bytes(item.first) == item.second;
            });
        };
        const auto row_for = [&](const std::string& id) -> const TableRow* {
            size_t index = 0;
            return find_row_index_by_edit_id(app.model_.pretrains, id, index)
                ? &app.model_.pretrains[index] : nullptr;
        };
        std::set<std::string> ids;
        for (const TableRow& row : baseline) {
            const bool unique = !row.edit_id.empty() && ids.insert(row.edit_id).second;
            app.request_element_inspector(row.edit_id, "preTrain.pass");
            app.process_pending_element_inspector();
            check("existing_row_inspector_writable", unique &&
                app.inspector_.edit_id == row.edit_id && app.inspector_.fields.size() == 2 &&
                std::all_of(app.inspector_.fields.begin(), app.inspector_.fields.end(),
                    [](const auto& field) { return !field.read_only && !field.disabled; }));
        }

        app.show_pretrain_markers_ = true;
        app.rebuild_marker_overlay_cache();
        Canvas3DSceneBuildOptions scene_options;
        scene_options.model = &app.model_;
        scene_options.map_handle = app.handle_;
        scene_options.unit_distance = options.unit_distance;
        scene_options.control_point_interval = options.unit_distance;
        Canvas3DSceneBuildResult scene = build_canvas3d_scene_preview(scene_options);
        const auto markers_match = [&]() {
            const PlanData plan = app.current_plan_data();
            if (plan.pretrain_markers.size() != app.model_.pretrains.size()) {
                *out << "marker_mismatch=plan-count actual=" << plan.pretrain_markers.size()
                     << " expected=" << app.model_.pretrains.size() << "\n";
                return false;
            }
            canvas3d_detail::populate_canvas3d_scene_markers(scene.scene, app.model_);
            size_t scene_count = 0;
            for (const auto& marker : scene.scene.markers) {
                if (marker.kind != MapMarkerVisualKind::PreTrain) continue;
                ++scene_count;
                const TableRow* row = row_for(marker.edit_id);
                if (!row || marker.row_kind != "preTrain.pass" ||
                    marker.label != table_cell(*row, "passTime") ||
                    std::abs(marker.track_point.distance - table_cell_number(*row, "distance")) > 1e-6) {
                    *out << "marker_mismatch=scene-value id=" << marker.edit_id
                         << " label=" << marker.label << " distance=" << marker.track_point.distance
                         << " row_found=" << (row != nullptr) << "\n";
                    return false;
                }
            }
            for (const auto& marker : plan.pretrain_markers) {
                const TableRow* row = row_for(marker.edit_id);
                if (!row || marker.label != table_cell(*row, "passTime") ||
                    std::abs(marker.d - table_cell_number(*row, "distance")) > 1e-6) {
                    *out << "marker_mismatch=plan-value id=" << marker.edit_id
                         << " label=" << marker.label << " distance=" << marker.d
                         << " row_found=" << (row != nullptr) << "\n";
                    return false;
                }
                app.plan_view_.cx = marker.x;
                app.plan_view_.cy = marker.y;
                app.plan_view_.scale = 1.0;
                app.plan_view_.rotation = 0.0;
                // Isolate this family so a dense route's other markers cannot
                // consume the context menu's deliberate candidate limit.
                PlanData isolated;
                isolated.pretrain_markers.push_back(marker);
                const ImVec2 origin(0, 0), size(800, 600);
                const auto entries = app.collect_plan_context_entries(isolated,
                    app.plan_view_.world_to_screen(marker.x, marker.y, origin, size),
                    origin, size, 12.0f, 144.0, true);
                if (std::none_of(entries.begin(), entries.end(), [&](const auto& entry) {
                    return entry.kind == PlanMarkerKind::PreTrain &&
                        entry.row_kind == "preTrain.pass" && entry.edit_id == marker.edit_id;
                })) {
                    *out << "marker_mismatch=plan-context id=" << marker.edit_id << "\n";
                    return false;
                }
            }
            if (scene_count != app.model_.pretrains.size())
                *out << "marker_mismatch=scene-count actual=" << scene_count
                     << " expected=" << app.model_.pretrains.size() << "\n";
            return scene_count == app.model_.pretrains.size();
        };
        check("baseline_plan_scene_identity", markers_match());
        const auto edit = [&](const std::string& id, const std::string& value,
                              const std::string& distance) {
            app.request_element_inspector(id, "preTrain.pass");
            app.process_pending_element_inspector();
            auto* time = find_inspector_field(app.inspector_, "passTime");
            auto* position = find_inspector_field(app.inspector_, "distance");
            if (!time || !position) return false;
            set_edit_field_buffer(*time, value);
            set_edit_field_buffer(*position, distance);
            app.apply_inspector_changes();
            const TableRow* row = row_for(id);
            return row && table_cell(*row, "passTime") == value &&
                table_cell(*row, "distance") == distance;
        };
        for (const TableRow& row : baseline) {
            check("existing_row_numeric_apply", edit(row.edit_id, "46801.5", table_cell(row, "distance")));
            check("existing_row_repeated_clock_apply", edit(row.edit_id, "26:02:03", table_cell(row, "distance")));
        }
        check("time_edit_marker_refresh", markers_match());
        const std::string moved_distance = table_cell(baseline.back(), "distance");
        check("inspector_distance_move", edit(baseline.front().edit_id, "26:02:03", moved_distance));
        check("distance_move_marker_refresh", markers_match());
        const size_t pending_before_bad = app.pending_edit_changes_.size();
        check("inspector_invalid_time_rejected", !edit(baseline.front().edit_id, "12:99:00", moved_distance) &&
            app.pending_edit_changes_.size() == pending_before_bad);
        check("apply_preserves_disk", disk_unchanged());
        *out << "stage=inspector-edits-complete\n";
        for (const TableRow& row : baseline) {
            app.request_element_delete(row.edit_id, "preTrain.pass");
            check("delete_is_deferred", app.pending_delete_request_.has_value() && row_for(row.edit_id));
            app.process_pending_element_delete();
            check("delete_removes_selected_row", !row_for(row.edit_id));
        }
        check("delete_marker_refresh", markers_match());
        check("delete_preserves_disk", disk_unchanged());
        *out << "stage=deferred-deletes-complete\n";
        const std::string target_file = baseline.front().source.file_path;
        const std::string insert_distance = table_cell(baseline.front(), "distance");
        const auto create = [&](const char* time) {
            if (!app.open_new_element_wizard_for_template("pretrain.pass")) return false;
            auto& wizard = app.new_element_wizard_;
            wizard.target_file_path = target_file;
            wizard.target_file_candidates = {target_file};
            wizard.target_candidates_built = true;
            wizard.built_template = -1;
            wizard.built_target_file.clear();
            app.rebuild_new_element_wizard_form();
            auto* value = find_inspector_field(wizard.form, "passTime");
            auto* distance = find_inspector_field(wizard.form, "distance");
            if (!value || !distance) return false;
            check("wizard_default_clock", edit_field_buffer_text(*value) == "00:00:00");
            set_edit_field_buffer(*value, time);
            set_edit_field_buffer(*distance, insert_distance);
            return app.apply_new_element_insert();
        };
        check("wizard_invalid_time_rejected", !create("NaN") && app.model_.pretrains.empty());
        check("wizard_clock_insert", create("13:01:00"));
        check("wizard_seconds_insert", create("46860.5"));
        check("wizard_marker_refresh", markers_match());
        if (app.model_.pretrains.size() != 2) throw std::runtime_error("wizard expected two PreTrain rows");
        const std::string new_id = app.model_.pretrains.front().edit_id;
        check("inserted_row_followup_apply", edit(new_id, "46861.5", insert_distance));
        app.request_element_delete(new_id, "preTrain.pass");
        app.process_pending_element_delete();
        check("inserted_row_delete_cancels_insert", !row_for(new_id) &&
            !app.pending_edit_changes_.count(new_id) && app.model_.pretrains.size() == 1);
        check("cancel_insert_marker_refresh", markers_match());
        *out << "stage=wizard-inserts-complete\n";
        check("revert_succeeds", app.revert_all_pending_edits());
        bool restored = app.model_.pretrains.size() == baseline.size();
        for (const TableRow& row : baseline) {
            const TableRow* actual = row_for(row.edit_id);
            restored = restored && actual && actual->cells == row.cells &&
                actual->source.file_path == row.source.file_path && actual->source.line == row.source.line;
        }
        check("revert_restores_baseline", restored && app.pending_edit_changes_.empty());
        check("revert_marker_refresh", markers_match());
        check("all_source_bytes_unchanged", disk_unchanged());
        *out << "protected_source_count=" << disk.size() << "\nstage=revert-complete\n";
    } catch (const std::exception& error) {
        *out << "exception=" << error.what() << "\n";
        ++failures;
    }
    ImGui::EndFrame();
    ImPlot::DestroyContext();
    ImGui::DestroyContext();
    if (preview.handle) kv_free(preview.handle);
    if (metadata.handle) kv_free(metadata.handle);
    *out << "failed_cases=" << failures << "\nresult=" << (failures ? "FAIL" : "PASS") << "\n";
    return failures ? 3 : 0;
}
#endif
