/*
 * Copyright (c) 2026 Sapporo_ningyo
 *
 * Licensed under Apache License 2.0; see LICENSE and NOTICE.
 */

#include "kme.h"
#include "app_settings.h"
#include "debug_headless.h"
#include "maploader.h"
#include "../canvas3d/canvas3d_scene_data.h"
#include "../canvas3d/scene_fog.h"
#include "imgui.h"
#include "implot.h"

#include <d3d11.h>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <stdexcept>

int App::run_debug_headless_legacy_fog_edit(const HeadlessLegacyFogEditOptions& options) {
    std::ofstream output_file;
    std::ostream* out = &std::cout;
    if (!options.output_path.empty()) {
        output_file.open(std::filesystem::u8path(options.output_path), std::ios::binary);
        if (!output_file) return 1;
        out = &output_file;
    }
    *out << "command=debug-headless-legacy-fog-edit\npath=" << options.path
         << "\nmemory_apply_only=1\nstage=preview-load-start\n";
    out->flush();
    LoadModelOptions preview_options;
    preview_options.load_profile = "preview";
    LoadResult preview = load_map_worker(options.path, options.unit_distance, false, 0, 0,
                                         options.unit_distance, preview_options);
    LoadModelOptions edit_options;
    edit_options.full_edit_registry = true;
    edit_options.load_profile = "edit";
    LoadResult edit_metadata = load_map_worker(options.path, options.unit_distance, false, 0, 0,
                                               options.unit_distance, edit_options);
    int failed_cases = 0;
    const auto require = [&](bool value, const char* name) {
        *out << name << '=' << (value ? 1 : 0) << '\n';
        out->flush();
        if (!value) throw std::runtime_error(name);
    };
    const auto read_bytes = [](const std::string& path) {
        std::ifstream input(std::filesystem::u8path(path), std::ios::binary);
        if (!input) throw std::runtime_error("cannot read protected source: " + path);
        return std::string(std::istreambuf_iterator<char>(input), {});
    };
    std::map<std::string, std::string> protected_sources;
    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* context = nullptr;
    ImGui::CreateContext();
    ImPlot::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = ImVec2(1280, 720);
    io.DeltaTime = 1.0f / 60.0f;
    io.Fonts->AddFontDefault();
    io.Fonts->Build();
    ImGui::NewFrame();
    try {
        require(preview.ok && edit_metadata.ok, "both_load_profiles_succeeded");
        const std::vector<TableRow> baseline = edit_metadata.model.legacy_fogs;
        protected_sources.emplace(options.path, read_bytes(options.path));
        for (const auto& file : edit_metadata.model.edit_files) {
            protected_sources.emplace(file.file_path, read_bytes(file.file_path));
        }
        D3D_FEATURE_LEVEL feature_level{};
        require(SUCCEEDED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0,
            nullptr, 0, D3D11_SDK_VERSION, &device, &feature_level, &context)), "scene_device_created");
        UserSettings settings;
        settings.language = Language::En;
        App app(device, settings, 1.0f, false, false);
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
        app.apply_edit_metadata_result(std::move(edit_metadata));
        edit_metadata.handle = nullptr;
        require(app.edit_registry_loaded_ && app.model_.legacy_fogs.size() == baseline.size(),
                "preview_edit_metadata_merged");
        for (size_t i = 0; i < baseline.size(); ++i) {
            const TableRow& row = app.model_.legacy_fogs[i];
            KvEditTargetSnapshot target{};
            const KvUtf8View id{row.edit_id.data(), static_cast<uint64_t>(row.edit_id.size())};
            require(!row.edit_id.empty() && row.edit_id == baseline[i].edit_id &&
                    kv_get_edit_target_typed(app.handle_, id, &target, sizeof(target)) != 0,
                    "existing_row_editable");
        }
        *out << "baseline_legacy_fog_count=" << baseline.size() << '\n';
        app.rebuild_marker_overlay_cache();
        // A fog-only scene exercises the production refresh without importing route models.
        Canvas3DScene initial_scene;
        initial_scene.min_distance = app.dmin_;
        initial_scene.max_distance = app.dmax_;
        initial_scene.camera.distance = baseline.empty() ? 0 : table_cell_number(baseline.front(), "distance");
        canvas3d_detail::populate_canvas3d_scene_fog(initial_scene, app.model_);
        std::string scene_error;
        require(app.scene_preview_canvas_->load_scene(std::move(initial_scene), scene_error),
                "initial_fog_scene_loaded");
        app.scene_preview_canvas_->set_scene_fog_enabled(true);
        app.scene_preview_started_ = true;
        app.scene_preview_dirty_ = false;
        const auto row_for = [&](const std::string& id) -> const TableRow* {
            const auto& rows = app.model_.legacy_fogs;
            const auto found = std::find_if(rows.begin(), rows.end(), [&](const TableRow& row) {
                return row.edit_id == id;
            });
            return found == rows.end() ? nullptr : &*found;
        };
        const auto open_inspector = [&](const std::string& id) {
            app.request_element_inspector(id, "legacyFog.change");
            require(app.pending_inspector_request_.has_value(), "inspector_request_deferred");
            app.process_pending_element_inspector();
            require(app.inspector_.open && app.inspector_.edit_id == id &&
                    app.inspector_.fields.size() == 6, "inspector_has_six_fields");
        };
        const auto set_field = [&](MapElementInspectorState& form, const char* key, const char* value) {
            auto* field = find_inspector_field(form, key);
            require(field && !field->read_only && !field->disabled, "form_field_writable");
            set_edit_field_buffer(*field, value);
        };
        const auto verify_preview = [&](bool allow_rebuild) {
            app.ensure_table_cache();
            require(app.table_cache_.legacy_fog_rows.size() == app.model_.legacy_fogs.size(),
                    "table_rows_refreshed");
            require(app.legacy_fog_marker_cache_.size() == app.model_.legacy_fogs.size(),
                    "plan_markers_refreshed");
            for (size_t i = 0; i < app.legacy_fog_marker_cache_.size(); ++i) {
                const auto& marker = app.legacy_fog_marker_cache_[i];
                require(!marker || marker->edit_id == app.model_.legacy_fogs[i].edit_id,
                        "plan_marker_identity_matches");
            }
            if (allow_rebuild && app.scene_preview_dirty_) {
                require(app.scene_preview_preserve_models_on_rebuild_ &&
                        app.scene_preview_preserve_camera_on_rebuild_, "scene_rebuild_preserves_state");
                return;
            }
            require(!app.scene_preview_dirty_, "fog_update_uses_scene_map_refresh");
            Canvas3DScene expected;
            canvas3d_detail::populate_canvas3d_scene_fog(expected, app.model_);
            const auto actual = app.scene_preview_canvas_->debug_scene_fog_state();
            const auto sample = canvas3d_detail::sample_canvas3d_scene_fog(
                expected.fog_keyframes, actual.camera_distance, true);
            require(actual.keyframe_count == expected.fog_keyframes.size() &&
                    actual.sampled_enabled == sample.enabled &&
                    std::abs(actual.color.x - sample.color.x) < 1e-6f &&
                    std::abs(actual.color.y - sample.color.y) < 1e-6f &&
                    std::abs(actual.color.z - sample.color.z) < 1e-6f,
                    "live_scene_fog_matches_working_copy");
        };
        if (!baseline.empty()) {
            const std::string id = baseline.front().edit_id;
            open_inspector(id);
            for (const char* value : {"", "nan", "inf", "abc"}) {
                set_field(app.inspector_, "start", value);
                app.apply_inspector_changes();
                require(app.pending_edit_changes_.empty() && row_for(id) &&
                        row_for(id)->cells == baseline.front().cells, "invalid_inspector_input_rejected");
            }
            set_field(app.inspector_, "start", "-60.25");
            set_field(app.inspector_, "red", "64");
            app.apply_inspector_changes();
            require(row_for(id) && table_cell_number(*row_for(id), "start") == -60.25 &&
                    table_cell_number(*row_for(id), "red") == 64, "inspector_apply_updates_values");
            verify_preview(false);
            open_inspector(id);
            set_field(app.inspector_, "green", "300.5");
            app.apply_inspector_changes();
            require(row_for(id) && table_cell_number(*row_for(id), "start") == -60.25 &&
                    table_cell_number(*row_for(id), "green") == 300.5, "repeated_apply_keeps_prior_fields");
            verify_preview(false);
            app.request_element_delete(id, "legacyFog.change");
            require(app.pending_delete_request_.has_value(), "existing_delete_deferred");
            app.process_pending_element_delete();
            require(!row_for(id) && app.model_.legacy_fogs.size() + 1 == baseline.size(),
                    "existing_delete_removes_only_target");
            verify_preview(true);
            require(app.revert_all_pending_edits(), "existing_edits_reverted");
        }
        const auto prepare_wizard = [&]() {
            require(app.open_new_element_wizard_for_template("legacy.fog"), "legacy_fog_template_available");
            auto& state = app.new_element_wizard_;
            state.target_file_path = app.model_.path;
            state.target_file_candidates = {app.model_.path};
            state.target_candidates_built = true;
            state.built_template = -1;
            state.built_target_file.clear();
            app.rebuild_new_element_wizard_form();
        };
        prepare_wizard();
        auto& wizard = app.new_element_wizard_;
        require(wizard.form.fields.size() == 6 &&
                edit_field_buffer_text(*find_inspector_field(wizard.form, "start")) == "0" &&
                edit_field_buffer_text(*find_inspector_field(wizard.form, "end")) == "600" &&
                edit_field_buffer_text(*find_inspector_field(wizard.form, "red")) == "128",
                "wizard_defaults_match_contract");
        set_field(wizard.form, "blue", "");
        require(!app.apply_new_element_insert() && app.pending_edit_changes_.empty(),
                "wizard_missing_parameter_rejected");
        set_field(wizard.form, "blue", "128");
        require(app.apply_new_element_insert(), "wizard_insert_applied");
        const auto inserted = std::find_if(app.pending_edit_changes_.begin(), app.pending_edit_changes_.end(),
            [](const auto& entry) {
                return entry.second.row_kind == "legacyFog.change" && entry.second.operation == "insert";
            });
        require(inserted != app.pending_edit_changes_.end(), "insert_ledger_entry_present");
        const std::string inserted_id = inserted->first;
        require(row_for(inserted_id) && app.model_.legacy_fogs.size() == baseline.size() + 1,
                "inserted_row_has_stable_identity");
        verify_preview(true);
        open_inspector(inserted_id);
        set_field(app.inspector_, "end", "-20");
        app.apply_inspector_changes();
        require(row_for(inserted_id) && table_cell_number(*row_for(inserted_id), "end") == -20 &&
                app.pending_edit_changes_.at(inserted_id).operation == "insert",
                "inserted_row_edit_retains_creation_ledger");
        app.request_element_delete(inserted_id, "legacyFog.change");
        require(app.pending_delete_request_.has_value(), "inserted_delete_deferred");
        app.process_pending_element_delete();
        require(!row_for(inserted_id) && app.pending_edit_changes_.empty(), "inserted_delete_cancels_creation");
        prepare_wizard();
        require(app.apply_new_element_insert(), "second_insert_applied_for_revert");
        require(app.revert_all_pending_edits(), "final_revert_succeeded");
        require(app.model_.legacy_fogs.size() == baseline.size(), "baseline_row_count_restored");
        for (size_t i = 0; i < baseline.size(); ++i) {
            require(app.model_.legacy_fogs[i].edit_id == baseline[i].edit_id &&
                    app.model_.legacy_fogs[i].cells == baseline[i].cells,
                    "baseline_values_and_ids_restored");
        }
        *out << "stage=workflow-complete\n";
    } catch (const std::exception& error) {
        ++failed_cases;
        *out << "error=" << error.what() << '\n';
    }
    for (const auto& source : protected_sources) {
        try {
            require(read_bytes(source.first) == source.second, "protected_source_bytes_unchanged");
        } catch (const std::exception& error) {
            ++failed_cases;
            *out << "protected_source_error=" << source.first << ": " << error.what() << '\n';
        }
    }
    ImGui::EndFrame();
    ImPlot::DestroyContext();
    ImGui::DestroyContext();
    if (preview.handle) kv_free(preview.handle);
    if (edit_metadata.handle) kv_free(edit_metadata.handle);
    if (context) context->Release();
    if (device) device->Release();
    *out << "failed_cases=" << failed_cases << "\nresult=" << (failed_cases == 0 ? "PASS" : "FAIL") << '\n';
    return failed_cases == 0 ? 0 : 20;
}
