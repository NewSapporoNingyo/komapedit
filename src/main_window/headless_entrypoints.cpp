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

#ifndef NDEBUG
int App::run_debug_headless_new_element_edit(
    const HeadlessNewElementEditOptions& options) {
    std::ofstream output_file;
    std::ostream* out = &std::cout;
    if (!options.output_path.empty()) {
        output_file.open(std::filesystem::path(utf8_to_wide(options.output_path)),
                         std::ios::out | std::ios::trunc | std::ios::binary);
        if (!output_file) {
            std::cerr << "failed to open headless output: " << options.output_path << "\n";
            return 1;
        }
        out = &output_file;
    }
    *out << "command=debug-headless-new-element-edit\n"
         << "path=" << options.path << "\n"
         << "commit=" << (options.commit ? 1 : 0) << "\n"
         << "memory_apply_only=" << (options.commit ? 0 : 1) << "\n"
         << "stage=load-start\n";
    out->flush();

    LoadResult load = load_map_worker(
        options.path, options.unit_distance, false, 0.0, 0.0,
        options.unit_distance, LoadModelOptions{true});
    if (!load.ok) {
        *out << "load_error=" << load.error << "\nresult=FAIL\n";
        if (load.handle) kv_free(load.handle);
        return 2;
    }

    int failed_cases = 0;
    auto check = [&](const char* name, bool value) {
        *out << name << "=" << (value ? 1 : 0) << "\n";
        if (!value) ++failed_cases;
        return value;
    };
    auto source_hash_for_path = [](const MapModel& model,
                                   const std::string& path) {
        const auto found = std::find_if(
            model.edit_files.begin(), model.edit_files.end(),
            [&](const EditSourceFileInfo& file) {
                return file.file_path == path;
            });
        return found == model.edit_files.end()
            ? std::string{}
            : found->source_hash;
    };

    ImGui::CreateContext();
    ImPlot::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(1280.0f, 720.0f);
    io.DeltaTime = 1.0f / 60.0f;
    io.IniFilename = nullptr;
    io.Fonts->AddFontDefault();
    io.Fonts->Build();
    ImGui::NewFrame();
    try {
        UserSettings settings;
        settings.language = Language::En;
        App app(nullptr, settings, 1.0f, false, false);
        app.handle_ = load.handle;
        load.handle = nullptr;
        app.model_ = std::move(load.model);
        app.file_path_ = options.path;
        app.has_model_ = true;
        app.edit_mode_enabled_ = true;
        app.edit_registry_loaded_ = true;
        app.edit_memory_matches_pending_ledger_ = true;
        app.dmin_ = app.model_.default_min;
        app.dmax_ = app.model_.default_max;
        app.unit_distance_ = options.unit_distance;
        *out << "stage=load-complete\n";

        const size_t baseline_repeater_count = app.model_.repeaters.size();
        const size_t baseline_structure_count = app.model_.structures.size();
        const size_t baseline_other_track_count = app.model_.other_tracks.size();
        const size_t baseline_other_track_change_count =
            app.model_.other_track_changes.size();
        const size_t baseline_curve_count = app.model_.curve_rows.size();
        const size_t baseline_gradient_count = app.model_.gradient_rows.size();
        const std::string baseline_entry_hash =
            source_hash_for_path(app.model_, options.path);

        const std::vector<std::string> target_candidates =
            new_element_target_candidates(app.model_);
        if (target_candidates.empty()) {
            throw std::runtime_error("new-element wizard found no target source file");
        }
        const std::string target_file = target_candidates.front();
        const std::string baseline_target_hash =
            source_hash_for_path(app.model_, target_file);
        *out << "target_file=" << target_file << "\n"
             << "entry_source_hash_before=" << baseline_entry_hash << "\n"
             << "target_source_hash_before=" << baseline_target_hash << "\n";
        check("baseline_target_source_hash_present", !baseline_target_hash.empty());
        check("entry_source_hash_present_when_entry_is_editable",
              options.path != target_file || !baseline_entry_hash.empty());

        std::vector<double> distance_values;
        for (const EditStatementInfo& statement : app.model_.edit_statements) {
            if (statement.statement_kind == "Distance.Set" &&
                statement.source.file_path == target_file &&
                std::isfinite(statement.distance_value)) {
                distance_values.push_back(statement.distance_value);
            }
        }
        std::sort(distance_values.begin(), distance_values.end());
        distance_values.erase(
            std::unique(distance_values.begin(), distance_values.end()),
            distance_values.end());
        if (distance_values.empty()) {
            throw std::runtime_error("new-element target has no distance statements");
        }
        double begin_distance = distance_values.front();
        double end_distance = begin_distance;
        for (size_t index = 1; index < distance_values.size(); ++index) {
            if (distance_values[index] - distance_values[index - 1] >= 3.0) {
                begin_distance = distance_values[index - 1] + 1.0;
                end_distance = begin_distance + 1.0;
                break;
            }
        }
        *out << "insert_begin_distance=" << format_double(begin_distance, 6) << "\n"
             << "insert_end_distance=" << format_double(end_distance, 6) << "\n";
        check("own_track_pair_distances_distinct", begin_distance != end_distance);

        std::string track_key;
        std::string structure_key;
        for (const TableRow& row : app.model_.repeaters) {
            const std::string method = ascii_lower(table_cell(row, "method"));
            if (method != "begin" && method != "begin0") continue;
            const std::vector<std::string> keys =
                split_repeater_structure_keys(table_cell(row, "structureKeys"));
            if (keys.empty() || table_cell(row, "trackKey").empty()) continue;
            track_key = table_cell(row, "trackKey");
            structure_key = keys.front();
            break;
        }
        if (track_key.empty() || structure_key.empty()) {
            for (const TableRow& row : app.model_.structures) {
                if (table_cell(row, "trackKey").empty() ||
                    table_cell(row, "structureKey").empty()) {
                    continue;
                }
                track_key = table_cell(row, "trackKey");
                structure_key = table_cell(row, "structureKey");
                break;
            }
        }
        check("source_keys_found", !track_key.empty() && !structure_key.empty());
        if (track_key.empty() || structure_key.empty()) {
            throw std::runtime_error("unable to select Repeater source keys");
        }

        auto repeater_key_in_use = [&](const std::string& key) {
            const std::string canonical = repeater_linkage::canonical_key(key);
            return std::any_of(
                app.model_.repeaters.begin(), app.model_.repeaters.end(),
                [&](const TableRow& row) {
                    return repeater_linkage::canonical_key(
                        table_cell(row, "repeaterKey")) == canonical;
                });
        };
        std::string repeater_key = "headless-new-element-repeater";
        for (size_t suffix = 0; repeater_key_in_use(repeater_key); ++suffix) {
            repeater_key = "headless-new-element-repeater-" +
                std::to_string(suffix + 1);
        }
        std::string renamed_repeater_key = repeater_key + "-renamed";
        while (repeater_key_in_use(renamed_repeater_key)) {
            renamed_repeater_key += "-next";
        }

        auto template_index = [&](std::string_view id) {
            const auto& templates = new_element_templates();
            const auto found = std::find_if(
                templates.begin(), templates.end(),
                [&](const NewElementTemplate& tpl) {
                    return std::string_view(tpl.id) == id;
                });
            return found == templates.end()
                ? -1
                : static_cast<int>(std::distance(templates.begin(), found));
        };
        auto prepare_wizard = [&](std::string_view template_id) {
            const int selected_template = template_index(template_id);
            if (selected_template < 0) return false;
            app.new_element_wizard_ = NewElementWizardState{};
            NewElementWizardState& wizard = app.new_element_wizard_;
            wizard.open = true;
            wizard.selected_template = selected_template;
            wizard.target_file_path = target_file;
            wizard.target_candidates_built = true;
            wizard.target_file_candidates = {target_file};
            app.rebuild_new_element_wizard_form();
            return true;
        };
        auto set_field = [&](MapElementInspectorState& form,
                             const std::string& key,
                             const std::string& value) {
            MapElementEditFieldState* field = find_inspector_field(form, key);
            if (!field || field->read_only || field->disabled) return false;
            set_edit_field_buffer(*field, value);
            return true;
        };
        auto set_optional_argument = [&](MapElementInspectorState& form,
                                         std::string_view key, bool included) {
            return set_optional_insertion_argument_enabled(form, key, included);
        };
        auto settle_distance_resolution = [&]() {
            for (int attempt = 0; attempt < 8; ++attempt) {
                if (app.distance_resolution_workflow_.phase !=
                    DistanceResolutionPhase::None) {
                    const DistanceResolutionRequest& request =
                        app.distance_resolution_workflow_.request;
                    DistanceResolutionChoice choice;
                    if (!request.suggested_expression.empty()) {
                        choice.distance_expression = request.suggested_expression;
                    } else if (!request.allowed_boundaries.empty()) {
                        choice.boundary_token = request.allowed_boundaries.front().token;
                    } else if (request.can_confirm_reuse) {
                        choice.confirm_environment_mismatch = true;
                    } else {
                        return false;
                    }
                    app.apply_distance_resolution_choice(choice);
                }
                if (app.distance_resolution_workflow_.retry_requested) {
                    app.process_distance_resolution_retry();
                    continue;
                }
                return app.distance_resolution_workflow_.phase ==
                    DistanceResolutionPhase::None;
            }
            return false;
        };
        auto apply_wizard = [&]() {
            const bool applied_immediately = app.apply_new_element_insert();
            if (!applied_immediately &&
                app.distance_resolution_workflow_.phase ==
                    DistanceResolutionPhase::None &&
                !app.distance_resolution_workflow_.retry_requested) {
                return false;
            }
            return settle_distance_resolution();
        };
        auto apply_inspector = [&]() {
            app.apply_inspector_changes();
            if (!settle_distance_resolution()) return false;
            app.process_pending_element_inspector();
            return true;
        };
        auto pending_insert = [&](const std::string& edit_id)
            -> const MapElementPendingChange* {
            const auto found = app.pending_edit_changes_.find(edit_id);
            return found != app.pending_edit_changes_.end() &&
                    found->second.operation == "insert"
                ? &found->second
                : nullptr;
        };
        auto model_row = [](const std::vector<TableRow>& rows,
                            const std::string& edit_id) -> const TableRow* {
            const auto found = std::find_if(
                rows.begin(), rows.end(),
                [&](const TableRow& row) { return row.edit_id == edit_id; });
            return found == rows.end() ? nullptr : &*found;
        };
        auto pending_field = [](const MapElementPendingChange* change,
                                const char* key) {
            if (!change) return std::string{};
            const auto field = change->field_changes.find(key);
            return field == change->field_changes.end()
                ? std::string{}
                : field->second;
        };
        auto pending_insert_id_for = [&](std::string_view row_kind,
                                         std::string_view method) {
            const auto found = std::find_if(
                app.pending_edit_changes_.begin(), app.pending_edit_changes_.end(),
                [&](const auto& entry) {
                    if (entry.second.operation != "insert" ||
                        entry.second.row_kind != row_kind) {
                        return false;
                    }
                    const auto method_field =
                        entry.second.field_changes.find("method");
                    return method_field != entry.second.field_changes.end() &&
                        method_field->second == method;
                });
            return found == app.pending_edit_changes_.end()
                ? std::string{}
                : found->first;
        };

        auto resource_key_from_rows = [](const std::vector<TableRow>& rows,
                                         const char* cell_key) {
            const auto found = std::find_if(
                rows.begin(), rows.end(), [&](const TableRow& row) {
                    return !blank_ascii(table_cell(row, cell_key));
                });
            return found == rows.end()
                ? std::string{}
                : table_cell(*found, cell_key);
        };
        const std::string structure_model_key = resource_key_from_rows(
            app.model_.structure_models, "structureKey");
        const std::string sound_key = resource_key_from_rows(
            app.model_.sound_list, "soundKey");
        const std::string sound_3d_key = resource_key_from_rows(
            app.model_.sound_3d_list, "soundKey");
        check("resource_list_keys_found",
              !structure_model_key.empty() && !sound_key.empty() &&
                  !sound_3d_key.empty());
        if (structure_model_key.empty() || sound_key.empty() ||
            sound_3d_key.empty()) {
            throw std::runtime_error("resource-key wizard test requires structure and sound lists");
        }

        const int structure_template = template_index("structure.put0");
        const int sound_template = template_index("map_sound.play");
        const int sound_3d_template = template_index("map_sound3d.put");
        check("resource_key_templates_found",
              structure_template >= 0 && sound_template >= 0 &&
                  sound_3d_template >= 0);
        if (structure_template < 0 || sound_template < 0 ||
            sound_3d_template < 0) {
            throw std::runtime_error("resource-key wizard templates are unavailable");
        }

        app.new_element_wizard_ = NewElementWizardState{};
        app.new_element_wizard_.selected_template = structure_template;
        const bool closed_wizard_structure_prefill =
            app.can_use_resource_key_in_new_element_wizard(
                MapElementKeySource::Structure) &&
            app.use_resource_key_in_new_element_wizard(
                MapElementKeySource::Structure, structure_model_key);
        const MapElementEditFieldState* closed_structure_field =
            find_inspector_field(app.new_element_wizard_.form, "structureKey");
        check("resource_key_closed_wizard_prefill_ok",
              closed_wizard_structure_prefill && app.new_element_wizard_.open &&
                  closed_structure_field &&
                  edit_field_buffer_text(*closed_structure_field) == structure_model_key);

        check("structure_resource_key_wizard_prepared",
              prepare_wizard("structure.put0"));
        NewElementWizardState& resource_wizard = app.new_element_wizard_;
        const std::string preserved_structure_target =
            resource_wizard.target_file_path;
        const bool structure_draft_ready =
            set_field(resource_wizard.form, "structureKey", "previous-model") &&
            set_field(resource_wizard.form, "trackKey", "1") &&
            set_field(resource_wizard.form, "span", "31");
        const bool structure_prefill = structure_draft_ready &&
            app.can_use_resource_key_in_new_element_wizard(
                MapElementKeySource::Structure) &&
            app.use_resource_key_in_new_element_wizard(
                MapElementKeySource::Structure, structure_model_key);
        const MapElementEditFieldState* structure_field =
            find_inspector_field(resource_wizard.form, "structureKey");
        const MapElementEditFieldState* structure_track_field =
            find_inspector_field(resource_wizard.form, "trackKey");
        const MapElementEditFieldState* structure_span_field =
            find_inspector_field(resource_wizard.form, "span");
        check("structure_resource_key_prefill_preserves_draft",
              structure_prefill && structure_field && structure_track_field &&
                  structure_span_field &&
                  edit_field_buffer_text(*structure_field) == structure_model_key &&
                  edit_field_buffer_text(*structure_track_field) == "1" &&
                  edit_field_buffer_text(*structure_span_field) == "31" &&
                  resource_wizard.target_file_path == preserved_structure_target);

        check("repeater_resource_key_wizard_prepared",
              prepare_wizard("repeater.begin0"));
        NewElementWizardState& repeater_resource_wizard = app.new_element_wizard_;
        repeater_resource_wizard.repeater_add_begin = true;
        repeater_resource_wizard.repeater_add_end = false;
        update_repeater_wizard_field_enablement(repeater_resource_wizard);
        const std::string preserved_repeater_target =
            repeater_resource_wizard.target_file_path;
        const bool repeater_resource_draft_ready =
            set_field(repeater_resource_wizard.form, "structureKeys.0",
                      "previous-repeater-model") &&
            set_field(repeater_resource_wizard.form, "repeaterKey",
                      "previous-repeater");
        const bool repeater_resource_prefill = repeater_resource_draft_ready &&
            app.can_use_resource_key_in_new_element_wizard(
                MapElementKeySource::Structure) &&
            app.use_resource_key_in_new_element_wizard(
                MapElementKeySource::Structure, structure_model_key);
        const MapElementEditFieldState* repeater_structure_field =
            find_inspector_field(repeater_resource_wizard.form, "structureKeys.0");
        const MapElementEditFieldState* repeater_key_field =
            find_inspector_field(repeater_resource_wizard.form, "repeaterKey");
        check("repeater_resource_key_prefill_preserves_state",
              repeater_resource_prefill && repeater_structure_field &&
                  repeater_key_field &&
                  edit_field_buffer_text(*repeater_structure_field) ==
                      structure_model_key &&
                  edit_field_buffer_text(*repeater_key_field) ==
                      "previous-repeater" &&
                  repeater_resource_wizard.repeater_add_begin &&
                  !repeater_resource_wizard.repeater_add_end &&
                  repeater_resource_wizard.target_file_path ==
                      preserved_repeater_target);

        check("sound_resource_key_wizard_prepared",
              prepare_wizard("map_sound.play"));
        const std::string preserved_sound_target =
            app.new_element_wizard_.target_file_path;
        const bool sound_draft_ready =
            set_field(app.new_element_wizard_.form, "soundKey", "previous-sound") &&
            set_field(app.new_element_wizard_.form, "distance", "37");
        const bool sound_prefill = sound_draft_ready &&
            app.can_use_resource_key_in_new_element_wizard(
                MapElementKeySource::Sound) &&
            app.use_resource_key_in_new_element_wizard(
                MapElementKeySource::Sound, sound_key);
        const MapElementEditFieldState* sound_field =
            find_inspector_field(app.new_element_wizard_.form, "soundKey");
        const MapElementEditFieldState* sound_distance_field =
            find_inspector_field(app.new_element_wizard_.form, "distance");
        check("sound_resource_key_prefill_preserves_draft",
              sound_prefill && sound_field && sound_distance_field &&
                  edit_field_buffer_text(*sound_field) == sound_key &&
                  edit_field_buffer_text(*sound_distance_field) == "37" &&
                  app.new_element_wizard_.target_file_path == preserved_sound_target);
        check("resource_key_template_mismatch_disabled",
              !app.can_use_resource_key_in_new_element_wizard(
                  MapElementKeySource::Structure) &&
                  !app.use_resource_key_in_new_element_wizard(
                      MapElementKeySource::Structure, structure_model_key));

        check("sound_3d_resource_key_wizard_prepared",
              prepare_wizard("map_sound3d.put"));
        const std::string preserved_sound_3d_target =
            app.new_element_wizard_.target_file_path;
        const bool sound_3d_draft_ready =
            set_field(app.new_element_wizard_.form, "soundKey", "previous-3d-sound") &&
            set_field(app.new_element_wizard_.form, "x", "4") &&
            set_field(app.new_element_wizard_.form, "y", "5");
        const bool sound_3d_prefill = sound_3d_draft_ready &&
            app.can_use_resource_key_in_new_element_wizard(
                MapElementKeySource::Sound3D) &&
            app.use_resource_key_in_new_element_wizard(
                MapElementKeySource::Sound3D, sound_3d_key);
        const MapElementEditFieldState* sound_3d_field =
            find_inspector_field(app.new_element_wizard_.form, "soundKey");
        const MapElementEditFieldState* sound_3d_x_field =
            find_inspector_field(app.new_element_wizard_.form, "x");
        const MapElementEditFieldState* sound_3d_y_field =
            find_inspector_field(app.new_element_wizard_.form, "y");
        check("sound_3d_resource_key_prefill_preserves_draft",
              sound_3d_prefill && sound_3d_field && sound_3d_x_field &&
                  sound_3d_y_field &&
                  edit_field_buffer_text(*sound_3d_field) == sound_3d_key &&
                  edit_field_buffer_text(*sound_3d_x_field) == "4" &&
                  edit_field_buffer_text(*sound_3d_y_field) == "5" &&
                  app.new_element_wizard_.target_file_path ==
                      preserved_sound_3d_target);
        check("resource_key_prefill_keeps_pending_ledger_empty",
              app.pending_edit_changes_.empty());

        check("repeater_template_found", prepare_wizard("repeater.begin0"));
        NewElementWizardState& repeater_wizard = app.new_element_wizard_;
        repeater_wizard.repeater_add_begin = true;
        repeater_wizard.repeater_add_end = true;
        update_repeater_wizard_field_enablement(repeater_wizard);
        bool repeater_fields_ok =
            set_field(repeater_wizard.form, "distance",
                      format_double(begin_distance, 6)) &&
            set_field(repeater_wizard.form, "endDistance",
                      format_double(end_distance, 6)) &&
            set_field(repeater_wizard.form, "repeaterKey", repeater_key) &&
            set_field(repeater_wizard.form, "trackKey", track_key) &&
            set_field(repeater_wizard.form, "interval", "25") &&
            set_field(repeater_wizard.form, "structureKeys.0", structure_key);
        check("repeater_wizard_fields_set", repeater_fields_ok);
        check("repeater_wizard_apply_ok", repeater_fields_ok && apply_wizard());

        std::string begin_edit_id;
        std::string end_edit_id;
        std::string repeater_pair_id;
        for (const auto& entry : app.pending_edit_changes_) {
            if (entry.second.row_kind != "repeater" ||
                entry.second.operation != "insert") {
                continue;
            }
            const auto method = entry.second.field_changes.find("method");
            if (method == entry.second.field_changes.end()) continue;
            if (ascii_lower(method->second) == "end") {
                end_edit_id = entry.first;
            } else {
                begin_edit_id = entry.first;
            }
            if (repeater_pair_id.empty()) {
                repeater_pair_id = entry.second.repeater_pair_id;
            }
        }
        const MapElementPendingChange* begin_insert = pending_insert(begin_edit_id);
        const MapElementPendingChange* end_insert = pending_insert(end_edit_id);
        *out << "repeater_begin_edit_id=" << begin_edit_id << "\n"
             << "repeater_end_edit_id=" << end_edit_id << "\n"
             << "repeater_pair_id=" << repeater_pair_id << "\n";
        check("repeater_pair_insert_ledger_ok",
              app.pending_edit_changes_.size() == 2 && begin_insert && end_insert &&
              !repeater_pair_id.empty() &&
              begin_insert->repeater_pair_id == repeater_pair_id &&
              end_insert->repeater_pair_id == repeater_pair_id);
        check("repeater_pair_model_rows_ok",
              model_row(app.model_.repeaters, begin_edit_id) &&
              model_row(app.model_.repeaters, end_edit_id) &&
              app.model_.repeaters.size() == baseline_repeater_count + 2);
        *out << "stage=repeater-created\n";

        check("repeater_inspector_open_ok",
              app.open_element_inspector(begin_edit_id, "repeater"));
        const double edited_end_distance = end_distance + 1.0;
        const bool edit_fields_ok =
            set_field(app.inspector_, "interval", "7") &&
            set_field(app.inspector_, "endDistance",
                      format_double(edited_end_distance, 6));
        check("repeater_inspector_fields_set", edit_fields_ok);
        check("repeater_insert_followup_apply_ok",
              edit_fields_ok && apply_inspector());
        *out << "followup_status=" << app.program_status_key_ << "\n";
        begin_insert = pending_insert(begin_edit_id);
        end_insert = pending_insert(end_edit_id);
        *out << "followup_begin_interval="
             << pending_field(begin_insert, "interval") << "\n"
             << "followup_begin_distance="
             << pending_field(begin_insert, "distance") << "\n"
             << "followup_end_distance="
             << pending_field(end_insert, "distance") << "\n"
             << "followup_begin_pair_id="
             << (begin_insert ? begin_insert->repeater_pair_id : std::string{}) << "\n"
             << "followup_end_pair_id="
             << (end_insert ? end_insert->repeater_pair_id : std::string{}) << "\n";
        const bool followup_ledger_ok = begin_insert && end_insert &&
            begin_insert->repeater_pair_id == repeater_pair_id &&
            end_insert->repeater_pair_id == repeater_pair_id &&
            begin_insert->field_changes.find("interval") !=
                begin_insert->field_changes.end() &&
            begin_insert->field_changes.at("interval") == "7" &&
            end_insert->field_changes.find("distance") !=
                end_insert->field_changes.end() &&
            std::abs(std::stod(end_insert->field_changes.at("distance")) -
                     edited_end_distance) < 1e-9;
        check("repeater_insert_followup_ledger_ok", followup_ledger_ok);

        const TableRow* edited_begin_row =
            model_row(app.model_.repeaters, begin_edit_id);
        const TableRow* edited_end_row =
            model_row(app.model_.repeaters, end_edit_id);
        *out << "snapshot_begin_interval="
             << (edited_begin_row ? table_cell(*edited_begin_row, "interval")
                                  : std::string{}) << "\n"
             << "snapshot_end_distance="
             << (edited_end_row ? table_cell(*edited_end_row, "distance")
                                : std::string{}) << "\n";
        check("repeater_insert_followup_snapshot_ok",
              edited_begin_row && edited_end_row &&
              std::abs(table_cell_number(*edited_begin_row, "interval") - 7.0) < 1e-9 &&
              std::abs(table_cell_number(*edited_end_row, "distance") -
                       edited_end_distance) < 1e-9);
        *out << "stage=repeater-followup-applied\n";

        if (!app.inspector_.open || app.inspector_.edit_id != begin_edit_id) {
            app.open_element_inspector(begin_edit_id, "repeater");
        }
        const bool rename_field_ok =
            set_field(app.inspector_, "repeaterKey", renamed_repeater_key);
        check("repeater_rename_field_set", rename_field_ok);
        check("repeater_insert_rename_apply_ok",
              rename_field_ok && apply_inspector());
        *out << "rename_status=" << app.program_status_key_ << "\n";
        begin_insert = pending_insert(begin_edit_id);
        end_insert = pending_insert(end_edit_id);
        *out << "renamed_begin_key="
             << pending_field(begin_insert, "repeaterKey") << "\n"
             << "renamed_end_key="
             << pending_field(end_insert, "repeaterKey") << "\n";
        const bool renamed_ledger_ok = begin_insert && end_insert &&
            begin_insert->field_changes.find("repeaterKey") !=
                begin_insert->field_changes.end() &&
            end_insert->field_changes.find("repeaterKey") !=
                end_insert->field_changes.end() &&
            begin_insert->field_changes.at("repeaterKey") == renamed_repeater_key &&
            end_insert->field_changes.at("repeaterKey") == renamed_repeater_key;
        check("repeater_insert_rename_ledger_ok", renamed_ledger_ok);
        *out << "stage=repeater-renamed\n";

        MapElementDeleteRequest repeater_delete;
        repeater_delete.edit_id = begin_edit_id;
        repeater_delete.row_kind = "repeater";
        repeater_delete.repeater_mode = RepeaterDeleteMode::EntireChain;
        check("repeater_insert_cancel_ok",
              app.delete_element_target(repeater_delete));
        check("repeater_insert_cancel_restores_baseline",
              app.pending_edit_changes_.empty() &&
              app.model_.repeaters.size() == baseline_repeater_count);
        *out << "stage=repeater-cancelled\n";

        check("own_track_templates_found",
              template_index("curve") >= 0 && template_index("gradient") >= 0 &&
                  template_index("curve.begin") < 0 &&
                  template_index("curve.begin_transition") < 0 &&
                  template_index("curve.change") < 0 &&
                  template_index("curve.end") < 0 &&
                  template_index("gradient.begin") < 0 &&
                  template_index("gradient.end") < 0);
        check("curve_template_fields_and_defaults",
              prepare_wizard("curve") &&
                  find_inspector_field(app.new_element_wizard_.form, "distance") &&
                  find_inspector_field(app.new_element_wizard_.form, "transitionStart") &&
                  find_inspector_field(app.new_element_wizard_.form, "method") &&
                  find_inspector_field(app.new_element_wizard_.form, "radius") &&
                  find_inspector_field(app.new_element_wizard_.form, "cant") &&
                  find_inspector_field(app.new_element_wizard_.form, "endTransitionStart") &&
                  find_inspector_field(app.new_element_wizard_.form, "endDistance") &&
                  app.new_element_wizard_.form.fields.size() == 7 &&
                  app.new_element_wizard_.own_track_add_start &&
                  app.new_element_wizard_.own_track_add_end &&
                  app.new_element_wizard_.own_track_start_add_transition &&
                  app.new_element_wizard_.own_track_end_add_transition &&
                  app.new_element_wizard_.own_track_curve_add_cant &&
                  own_track_curve_method(app.new_element_wizard_) == "Begin");
        check("gradient_template_fields_and_defaults",
              prepare_wizard("gradient") &&
                  find_inspector_field(app.new_element_wizard_.form, "distance") &&
                  find_inspector_field(app.new_element_wizard_.form, "transitionStart") &&
                  find_inspector_field(app.new_element_wizard_.form, "gradient") &&
                  find_inspector_field(app.new_element_wizard_.form, "endTransitionStart") &&
                  find_inspector_field(app.new_element_wizard_.form, "endDistance") &&
                  app.new_element_wizard_.form.fields.size() == 5 &&
                  app.new_element_wizard_.own_track_add_start &&
                  app.new_element_wizard_.own_track_add_end &&
                  app.new_element_wizard_.own_track_start_add_transition &&
                  app.new_element_wizard_.own_track_end_add_transition);

        check("curve_transition_distance_validation_prepared", prepare_wizard("curve"));
        NewElementWizardState& invalid_curve_wizard = app.new_element_wizard_;
        invalid_curve_wizard.own_track_add_end = false;
        update_own_track_wizard_field_enablement(invalid_curve_wizard);
        const bool invalid_curve_fields_ok =
            set_field(invalid_curve_wizard.form, "transitionStart",
                      format_double(end_distance, 6)) &&
            set_field(invalid_curve_wizard.form, "distance",
                      format_double(begin_distance, 6)) &&
            set_field(invalid_curve_wizard.form, "radius", "300") &&
            set_field(invalid_curve_wizard.form, "cant", "0.15");
        check("curve_transition_after_statement_rejected",
              invalid_curve_fields_ok && !app.apply_new_element_insert() &&
                  std::string_view(app.program_status_key_) ==
                      "status.edit.transition_start_after_distance" &&
                  app.pending_edit_changes_.empty());

        check("curve_change_template_prepared", prepare_wizard("curve"));
        NewElementWizardState& curve_change_wizard = app.new_element_wizard_;
        curve_change_wizard.own_track_add_end = false;
        check("curve_change_method_set",
              set_field(curve_change_wizard.form, "method", "Change"));
        update_own_track_wizard_field_enablement(curve_change_wizard);
        const MapElementEditFieldState* change_cant =
            find_inspector_field(curve_change_wizard.form, "cant");
        const MapElementEditFieldState* change_transition =
            find_inspector_field(curve_change_wizard.form, "transitionStart");
        check("curve_change_disables_transition_and_cant",
              change_cant && change_cant->disabled && change_transition &&
                  change_transition->disabled &&
                  !curve_change_wizard.own_track_start_add_transition &&
                  !curve_change_wizard.own_track_curve_add_cant);
        const bool curve_change_fields_ok =
            set_field(curve_change_wizard.form, "distance",
                      format_double(begin_distance, 6)) &&
            set_field(curve_change_wizard.form, "radius", "250");
        check("curve_change_wizard_apply_ok",
              curve_change_fields_ok && apply_wizard());
        const std::string curve_change_edit_id = "insert-1-b-start";
        const MapElementPendingChange* curve_change_insert =
            pending_insert(curve_change_edit_id);
        check("curve_change_insert_shape_ok",
              curve_change_insert && app.pending_edit_changes_.size() == 1 &&
                  pending_field(curve_change_insert, "method") == "Curve.Change" &&
                  pending_field(curve_change_insert, "radius") == "250" &&
                  pending_field(curve_change_insert, "cant").empty());
        MapElementDeleteRequest curve_change_delete;
        curve_change_delete.edit_id = curve_change_edit_id;
        curve_change_delete.row_kind = "curve";
        check("curve_change_cancel_ok",
              app.delete_element_target(curve_change_delete) &&
                  app.pending_edit_changes_.empty() &&
                  app.model_.curve_rows.size() == baseline_curve_count);

        check("curve_paired_template_prepared",
              prepare_wizard("curve"));
        NewElementWizardState& curve_wizard = app.new_element_wizard_;
        curve_wizard.own_track_add_end = false;
        update_own_track_wizard_field_enablement(curve_wizard);
        const bool curve_pair_fields_ok =
            curve_wizard.own_track_start_add_transition &&
            curve_wizard.own_track_curve_add_cant &&
            find_inspector_field(curve_wizard.form, "cant") &&
            find_inspector_field(curve_wizard.form, "transitionStart") &&
            !find_inspector_field(curve_wizard.form, "transitionStart")->disabled &&
            set_field(curve_wizard.form, "transitionStart", format_double(begin_distance, 6)) &&
            set_field(curve_wizard.form, "distance", format_double(end_distance, 6)) &&
            set_field(curve_wizard.form, "radius", "300") &&
            set_field(curve_wizard.form, "cant", "0.15");
        check("curve_paired_wizard_fields_set", curve_pair_fields_ok);
        check("curve_paired_wizard_apply_ok",
              curve_pair_fields_ok && apply_wizard());
        const std::string curve_transition_edit_id = pending_insert_id_for(
            "curve", "Curve.BeginTransition");
        const std::string curve_primary_edit_id = pending_insert_id_for(
            "curve", "Curve.Begin");
        const MapElementPendingChange* curve_transition_insert =
            pending_insert(curve_transition_edit_id);
        const MapElementPendingChange* curve_primary_insert =
            pending_insert(curve_primary_edit_id);
        const TableRow* curve_transition_row = model_row(
            app.model_.curve_rows, curve_transition_edit_id);
        const TableRow* curve_primary_row = model_row(
            app.model_.curve_rows, curve_primary_edit_id);
        *out << "curve_transition_edit_id=" << curve_transition_edit_id << "\n"
             << "curve_primary_edit_id=" << curve_primary_edit_id << "\n";
        check("curve_paired_insert_ledger_ok",
              curve_transition_edit_id == "insert-1-a-start-transition" &&
                  curve_primary_edit_id == "insert-1-b-start" &&
                  app.pending_edit_changes_.size() == 2 && curve_transition_insert &&
                  curve_primary_insert &&
                  std::abs(std::stod(pending_field(curve_transition_insert, "distance")) -
                           begin_distance) < 1e-9 &&
                  std::abs(std::stod(pending_field(curve_primary_insert, "distance")) -
                           end_distance) < 1e-9 &&
                  pending_field(curve_primary_insert, "transitionStart").empty() &&
                  pending_field(curve_primary_insert, "radius") == "300" &&
                  pending_field(curve_primary_insert, "cant") == "0.15");
        check("curve_paired_target_source_and_order_ok",
              curve_transition_row && curve_primary_row &&
                  app.model_.curve_rows.size() == baseline_curve_count + 2 &&
                  table_cell(*curve_transition_row, "filePath") == target_file &&
                  table_cell(*curve_primary_row, "filePath") == target_file &&
                  table_cell(*curve_primary_row, "_transitionEditId") ==
                      curve_transition_edit_id &&
                  std::abs(table_cell_number(*curve_transition_row, "distance") -
                           begin_distance) < 1e-9 &&
                  std::abs(table_cell_number(*curve_primary_row, "distance") -
                           end_distance) < 1e-9 &&
                  table_cell_number(*curve_transition_row, "order") <
                      table_cell_number(*curve_primary_row, "order"));
        check("curve_paired_inspector_open_ok",
              app.open_element_inspector(curve_primary_edit_id, "curve"));
        const bool curve_inspector_fields_ok =
            find_inspector_field(app.inspector_, "transitionStart") &&
            set_field(app.inspector_, "radius", "320");
        check("curve_paired_inspector_field_set", curve_inspector_fields_ok);
        check("curve_paired_inspector_apply_ok",
              curve_inspector_fields_ok && apply_inspector());
        curve_primary_insert = pending_insert(curve_primary_edit_id);
        curve_primary_row = model_row(app.model_.curve_rows, curve_primary_edit_id);
        check("curve_paired_inspector_followup_ok",
              curve_primary_insert && curve_primary_row &&
                  pending_field(curve_primary_insert, "radius") == "320" &&
                  std::abs(table_cell_number(*curve_primary_row, "radius") - 320.0) < 1e-9);
        MapElementDeleteRequest curve_pair_delete;
        curve_pair_delete.edit_id = curve_primary_edit_id;
        curve_pair_delete.row_kind = "curve";
        check("curve_paired_insert_cancel_ok",
              app.delete_element_target(curve_pair_delete));
        check("curve_paired_insert_cancel_restores_baseline",
              app.pending_edit_changes_.empty() &&
                  app.model_.curve_rows.size() == baseline_curve_count);
        *out << "stage=curve-pair-cancelled\n";

        check("curve_end_pair_template_prepared", prepare_wizard("curve"));
        NewElementWizardState& curve_end_wizard = app.new_element_wizard_;
        curve_end_wizard.own_track_add_start = false;
        update_own_track_wizard_field_enablement(curve_end_wizard);
        const bool curve_end_primary_distance_set =
            set_field(curve_end_wizard.form, "endDistance",
                      format_double(end_distance, 6));
        const bool curve_end_pair_fields_ok =
            curve_end_primary_distance_set &&
            find_inspector_field(curve_end_wizard.form, "endTransitionStart") &&
            !find_inspector_field(curve_end_wizard.form, "endTransitionStart")->disabled &&
            edit_field_buffer_text(*find_inspector_field(
                curve_end_wizard.form, "endTransitionStart")) == "0" &&
            set_field(curve_end_wizard.form, "endTransitionStart",
                      format_double(begin_distance, 6));
        check("curve_end_pair_wizard_fields_set", curve_end_pair_fields_ok);
        check("curve_end_pair_wizard_apply_ok",
              curve_end_pair_fields_ok && apply_wizard());
        const std::string curve_end_transition_edit_id = pending_insert_id_for(
            "curve", "Curve.BeginTransition");
        const std::string curve_end_primary_edit_id = pending_insert_id_for(
            "curve", "Curve.End");
        const TableRow* curve_end_transition_row = model_row(
            app.model_.curve_rows, curve_end_transition_edit_id);
        const TableRow* curve_end_primary_row = model_row(
            app.model_.curve_rows, curve_end_primary_edit_id);
        const MapElementPendingChange* curve_end_transition_insert =
            pending_insert(curve_end_transition_edit_id);
        const MapElementPendingChange* curve_end_primary_insert =
            pending_insert(curve_end_primary_edit_id);
        check("curve_end_pair_ledger_and_target_ok",
              curve_end_transition_edit_id == "insert-1-c-end-transition" &&
                  curve_end_primary_edit_id == "insert-1-d-end" &&
                  curve_end_transition_row && curve_end_primary_row &&
                  curve_end_transition_insert && curve_end_primary_insert &&
                  app.pending_edit_changes_.size() == 2 &&
                  std::abs(std::stod(pending_field(curve_end_transition_insert,
                                                    "distance")) - begin_distance) < 1e-9 &&
                  std::abs(std::stod(pending_field(curve_end_primary_insert,
                                                    "distance")) - end_distance) < 1e-9 &&
                  table_cell(*curve_end_primary_row, "_transitionEditId") ==
                      curve_end_transition_edit_id &&
                  std::abs(table_cell_number(*curve_end_transition_row, "distance") -
                           begin_distance) < 1e-9 &&
                  std::abs(table_cell_number(*curve_end_primary_row, "distance") -
                           end_distance) < 1e-9 &&
                  table_cell_number(*curve_end_transition_row, "order") <
                      table_cell_number(*curve_end_primary_row, "order") &&
                  table_cell(*curve_end_transition_row, "filePath") == target_file &&
                  table_cell(*curve_end_primary_row, "filePath") == target_file);
        MapElementDeleteRequest curve_end_pair_delete;
        curve_end_pair_delete.edit_id = curve_end_primary_edit_id;
        curve_end_pair_delete.row_kind = "curve";
        check("curve_end_pair_cancel_ok",
              app.delete_element_target(curve_end_pair_delete));
        check("curve_end_pair_cancel_restores_baseline",
              app.pending_edit_changes_.empty() &&
                  app.model_.curve_rows.size() == baseline_curve_count);

        check("gradient_begin_pair_template_prepared", prepare_wizard("gradient"));
        NewElementWizardState& gradient_begin_wizard = app.new_element_wizard_;
        gradient_begin_wizard.own_track_add_end = false;
        update_own_track_wizard_field_enablement(gradient_begin_wizard);
        const bool gradient_begin_primary_distance_set =
            set_field(gradient_begin_wizard.form, "distance", format_double(end_distance, 6));
        const bool gradient_begin_pair_fields_ok =
            gradient_begin_primary_distance_set &&
            find_inspector_field(gradient_begin_wizard.form, "transitionStart") &&
            !find_inspector_field(gradient_begin_wizard.form, "transitionStart")->disabled &&
            edit_field_buffer_text(*find_inspector_field(
                gradient_begin_wizard.form, "transitionStart")) == format_double(end_distance, 6) &&
            find_inspector_field(gradient_begin_wizard.form, "gradient") &&
            set_field(gradient_begin_wizard.form, "transitionStart", format_double(begin_distance, 6)) &&
            set_field(gradient_begin_wizard.form, "gradient", "12");
        check("gradient_begin_pair_wizard_fields_set", gradient_begin_pair_fields_ok);
        check("gradient_begin_pair_wizard_apply_ok",
              gradient_begin_pair_fields_ok && apply_wizard());
        const std::string gradient_begin_transition_edit_id = pending_insert_id_for(
            "gradient", "Gradient.BeginTransition");
        const std::string gradient_begin_primary_edit_id = pending_insert_id_for(
            "gradient", "Gradient.Begin");
        const TableRow* gradient_begin_transition_row = model_row(
            app.model_.gradient_rows, gradient_begin_transition_edit_id);
        const TableRow* gradient_begin_primary_row = model_row(
            app.model_.gradient_rows, gradient_begin_primary_edit_id);
        const MapElementPendingChange* gradient_begin_transition_insert =
            pending_insert(gradient_begin_transition_edit_id);
        const MapElementPendingChange* gradient_begin_primary_insert =
            pending_insert(gradient_begin_primary_edit_id);
        check("gradient_begin_pair_ledger_and_target_ok",
              gradient_begin_transition_edit_id == "insert-1-a-start-transition" &&
                  gradient_begin_primary_edit_id == "insert-1-b-start" &&
                  gradient_begin_transition_row && gradient_begin_primary_row &&
                  gradient_begin_transition_insert && gradient_begin_primary_insert &&
                  app.pending_edit_changes_.size() == 2 &&
                  std::abs(std::stod(pending_field(gradient_begin_transition_insert,
                                                    "distance")) - begin_distance) < 1e-9 &&
                  std::abs(std::stod(pending_field(gradient_begin_primary_insert,
                                                    "distance")) - end_distance) < 1e-9 &&
                  pending_field(gradient_begin_primary_insert, "gradient") == "12" &&
                  table_cell(*gradient_begin_primary_row, "_transitionEditId") ==
                      gradient_begin_transition_edit_id &&
                  std::abs(table_cell_number(*gradient_begin_transition_row, "distance") -
                           begin_distance) < 1e-9 &&
                  std::abs(table_cell_number(*gradient_begin_primary_row, "distance") -
                           end_distance) < 1e-9 &&
                  table_cell_number(*gradient_begin_transition_row, "order") <
                      table_cell_number(*gradient_begin_primary_row, "order") &&
                  table_cell(*gradient_begin_transition_row, "filePath") == target_file &&
                  table_cell(*gradient_begin_primary_row, "filePath") == target_file);
        MapElementDeleteRequest gradient_begin_pair_delete;
        gradient_begin_pair_delete.edit_id = gradient_begin_primary_edit_id;
        gradient_begin_pair_delete.row_kind = "gradient";
        check("gradient_begin_pair_cancel_ok",
              app.delete_element_target(gradient_begin_pair_delete));
        check("gradient_begin_pair_cancel_restores_baseline",
              app.pending_edit_changes_.empty() &&
                  app.model_.gradient_rows.size() == baseline_gradient_count);

        check("gradient_end_pair_template_prepared", prepare_wizard("gradient"));
        NewElementWizardState& gradient_end_wizard = app.new_element_wizard_;
        gradient_end_wizard.own_track_add_start = false;
        update_own_track_wizard_field_enablement(gradient_end_wizard);
        const bool gradient_end_primary_distance_set =
            set_field(gradient_end_wizard.form, "endDistance",
                      format_double(end_distance, 6));
        const bool gradient_end_pair_fields_ok =
            gradient_end_primary_distance_set &&
            find_inspector_field(gradient_end_wizard.form, "endTransitionStart") &&
            !find_inspector_field(gradient_end_wizard.form, "endTransitionStart")->disabled &&
            edit_field_buffer_text(*find_inspector_field(
                gradient_end_wizard.form, "endTransitionStart")) == "0" &&
            set_field(gradient_end_wizard.form, "endTransitionStart",
                      format_double(begin_distance, 6));
        check("gradient_end_pair_wizard_fields_set", gradient_end_pair_fields_ok);
        check("gradient_end_pair_wizard_apply_ok",
              gradient_end_pair_fields_ok && apply_wizard());
        const std::string gradient_end_transition_edit_id = pending_insert_id_for(
            "gradient", "Gradient.BeginTransition");
        const std::string gradient_end_primary_edit_id = pending_insert_id_for(
            "gradient", "Gradient.End");
        const TableRow* gradient_end_transition_row = model_row(
            app.model_.gradient_rows, gradient_end_transition_edit_id);
        const TableRow* gradient_end_primary_row = model_row(
            app.model_.gradient_rows, gradient_end_primary_edit_id);
        const MapElementPendingChange* gradient_end_transition_insert =
            pending_insert(gradient_end_transition_edit_id);
        const MapElementPendingChange* gradient_end_primary_insert =
            pending_insert(gradient_end_primary_edit_id);
        check("gradient_end_pair_ledger_and_target_ok",
              gradient_end_transition_edit_id == "insert-1-c-end-transition" &&
                  gradient_end_primary_edit_id == "insert-1-d-end" &&
                  gradient_end_transition_row && gradient_end_primary_row &&
                  gradient_end_transition_insert && gradient_end_primary_insert &&
                  app.pending_edit_changes_.size() == 2 &&
                  std::abs(std::stod(pending_field(gradient_end_transition_insert,
                                                    "distance")) - begin_distance) < 1e-9 &&
                  std::abs(std::stod(pending_field(gradient_end_primary_insert,
                                                    "distance")) - end_distance) < 1e-9 &&
                  table_cell(*gradient_end_primary_row, "_transitionEditId") ==
                      gradient_end_transition_edit_id &&
                  std::abs(table_cell_number(*gradient_end_transition_row, "distance") -
                           begin_distance) < 1e-9 &&
                  std::abs(table_cell_number(*gradient_end_primary_row, "distance") -
                           end_distance) < 1e-9 &&
                  table_cell_number(*gradient_end_transition_row, "order") <
                      table_cell_number(*gradient_end_primary_row, "order") &&
                  table_cell(*gradient_end_transition_row, "filePath") == target_file &&
                  table_cell(*gradient_end_primary_row, "filePath") == target_file);
        MapElementDeleteRequest gradient_end_pair_delete;
        gradient_end_pair_delete.edit_id = gradient_end_primary_edit_id;
        gradient_end_pair_delete.row_kind = "gradient";
        check("gradient_end_pair_cancel_ok",
              app.delete_element_target(gradient_end_pair_delete));
        check("gradient_end_pair_cancel_restores_baseline",
              app.pending_edit_changes_.empty() &&
                  app.model_.gradient_rows.size() == baseline_gradient_count);

        check("curve_combined_template_prepared", prepare_wizard("curve"));
        NewElementWizardState& combined_curve_wizard = app.new_element_wizard_;
        const bool combined_curve_fields_ok =
            set_field(combined_curve_wizard.form, "transitionStart",
                      format_double(begin_distance, 6)) &&
            set_field(combined_curve_wizard.form, "distance",
                      format_double(end_distance, 6)) &&
            set_field(combined_curve_wizard.form, "radius", "300") &&
            set_field(combined_curve_wizard.form, "cant", "0.15") &&
            set_field(combined_curve_wizard.form, "endTransitionStart",
                      format_double(end_distance, 6)) &&
            set_field(combined_curve_wizard.form, "endDistance",
                      format_double(end_distance, 6));
        check("curve_combined_apply_ok",
              combined_curve_fields_ok && apply_wizard());
        const std::array<std::string, 4> combined_curve_ids = {
            "insert-1-a-start-transition", "insert-1-b-start",
            "insert-1-c-end-transition", "insert-1-d-end",
        };
        const TableRow* combined_curve_start_transition =
            model_row(app.model_.curve_rows, combined_curve_ids[0]);
        const TableRow* combined_curve_start =
            model_row(app.model_.curve_rows, combined_curve_ids[1]);
        const TableRow* combined_curve_end_transition =
            model_row(app.model_.curve_rows, combined_curve_ids[2]);
        const TableRow* combined_curve_end =
            model_row(app.model_.curve_rows, combined_curve_ids[3]);
        check("curve_combined_order_and_ledger_ok",
              app.pending_edit_changes_.size() == combined_curve_ids.size() &&
                  std::all_of(combined_curve_ids.begin(), combined_curve_ids.end(),
                              [&](const std::string& id) { return pending_insert(id); }) &&
                  combined_curve_start_transition && combined_curve_start &&
                  combined_curve_end_transition && combined_curve_end &&
                  table_cell_number(*combined_curve_start_transition, "order") <
                      table_cell_number(*combined_curve_start, "order") &&
                  table_cell_number(*combined_curve_start, "order") <
                      table_cell_number(*combined_curve_end_transition, "order") &&
                  table_cell_number(*combined_curve_end_transition, "order") <
                      table_cell_number(*combined_curve_end, "order"));
        MapElementDeleteRequest combined_curve_start_delete;
        combined_curve_start_delete.edit_id = combined_curve_ids[1];
        combined_curve_start_delete.row_kind = "curve";
        MapElementDeleteRequest combined_curve_end_delete;
        combined_curve_end_delete.edit_id = combined_curve_ids[3];
        combined_curve_end_delete.row_kind = "curve";
        check("curve_combined_cancel_ok",
              app.delete_element_target(combined_curve_start_delete) &&
                  app.delete_element_target(combined_curve_end_delete) &&
                  app.pending_edit_changes_.empty() &&
                  app.model_.curve_rows.size() == baseline_curve_count);

        check("gradient_combined_template_prepared", prepare_wizard("gradient"));
        NewElementWizardState& combined_gradient_wizard = app.new_element_wizard_;
        const bool combined_gradient_fields_ok =
            set_field(combined_gradient_wizard.form, "transitionStart",
                      format_double(begin_distance, 6)) &&
            set_field(combined_gradient_wizard.form, "distance",
                      format_double(end_distance, 6)) &&
            set_field(combined_gradient_wizard.form, "gradient", "12") &&
            set_field(combined_gradient_wizard.form, "endTransitionStart",
                      format_double(end_distance, 6)) &&
            set_field(combined_gradient_wizard.form, "endDistance",
                      format_double(end_distance, 6));
        check("gradient_combined_apply_ok",
              combined_gradient_fields_ok && apply_wizard());
        const std::array<std::string, 4> combined_gradient_ids = {
            "insert-1-a-start-transition", "insert-1-b-start",
            "insert-1-c-end-transition", "insert-1-d-end",
        };
        check("gradient_combined_ledger_ok",
              app.pending_edit_changes_.size() == combined_gradient_ids.size() &&
                  std::all_of(combined_gradient_ids.begin(), combined_gradient_ids.end(),
                              [&](const std::string& id) { return pending_insert(id); }));
        MapElementDeleteRequest combined_gradient_start_delete;
        combined_gradient_start_delete.edit_id = combined_gradient_ids[1];
        combined_gradient_start_delete.row_kind = "gradient";
        MapElementDeleteRequest combined_gradient_end_delete;
        combined_gradient_end_delete.edit_id = combined_gradient_ids[3];
        combined_gradient_end_delete.row_kind = "gradient";
        check("gradient_combined_cancel_ok",
              app.delete_element_target(combined_gradient_start_delete) &&
                  app.delete_element_target(combined_gradient_end_delete) &&
                  app.pending_edit_changes_.empty() &&
                  app.model_.gradient_rows.size() == baseline_gradient_count);
        *out << "stage=own-track-pairs-cancelled\n";

        check("structure_template_found", prepare_wizard("structure.put0"));
        NewElementWizardState& structure_wizard = app.new_element_wizard_;
        const bool structure_fields_ok =
            set_field(structure_wizard.form, "distance",
                      format_double(begin_distance, 6)) &&
            set_field(structure_wizard.form, "structureKey", structure_key) &&
            set_field(structure_wizard.form, "trackKey", track_key) &&
            set_field(structure_wizard.form, "span", "25");
        check("structure_wizard_fields_set", structure_fields_ok);
        check("structure_wizard_apply_ok", structure_fields_ok && apply_wizard());
        std::string structure_edit_id;
        for (const auto& entry : app.pending_edit_changes_) {
            if (entry.second.row_kind == "structure.put" &&
                entry.second.operation == "insert") {
                structure_edit_id = entry.first;
                break;
            }
        }
        check("structure_insert_ledger_ok",
              !structure_edit_id.empty() &&
              app.pending_edit_changes_.size() == 1 &&
              model_row(app.model_.structures, structure_edit_id) &&
              app.model_.structures.size() == baseline_structure_count + 1);
        *out << "stage=structure-created\n";
        check("structure_inspector_open_ok",
              app.open_element_inspector(structure_edit_id, "structure.put"));
        const bool structure_edit_field_ok =
            set_field(app.inspector_, "span", "31");
        check("structure_inspector_field_set", structure_edit_field_ok);
        check("structure_insert_followup_apply_ok",
              structure_edit_field_ok && apply_inspector());
        const MapElementPendingChange* structure_insert =
            pending_insert(structure_edit_id);
        const TableRow* structure_row =
            model_row(app.model_.structures, structure_edit_id);
        check("structure_insert_followup_state_ok",
              structure_insert && structure_row &&
              structure_insert->field_changes.find("span") !=
                  structure_insert->field_changes.end() &&
              structure_insert->field_changes.at("span") == "31" &&
              std::abs(table_cell_number(*structure_row, "span") - 31.0) < 1e-9);
        *out << "stage=structure-followup-applied\n";

        MapElementDeleteRequest structure_delete;
        structure_delete.edit_id = structure_edit_id;
        structure_delete.row_kind = "structure.put";
        check("structure_insert_cancel_ok",
              app.delete_element_target(structure_delete));
        check("structure_insert_cancel_restores_baseline",
              app.pending_edit_changes_.empty() &&
              app.model_.structures.size() == baseline_structure_count);
        *out << "stage=structure-cancelled\n";

        auto other_track_key_in_use = [&](const std::string& key) {
            const std::string canonical = normalize_track_lookup_key("'" + key + "'");
            return std::any_of(
                app.model_.other_tracks.begin(), app.model_.other_tracks.end(),
                [&](const OtherTrack& track) {
                    return normalize_track_lookup_key(track.key) == canonical;
                });
        };
        std::string other_track_key = "headless-new-element-other-track";
        for (size_t suffix = 0; other_track_key_in_use(other_track_key); ++suffix) {
            other_track_key = "headless-new-element-other-track-" +
                std::to_string(suffix + 1);
        }
        check("other_track_position_template_found",
              prepare_wizard("other_track.position"));
        NewElementWizardState& other_position_wizard = app.new_element_wizard_;
        const MapElementEditFieldState* radius_h =
            find_inspector_field(other_position_wizard.form, "parameter2");
        const MapElementEditFieldState* radius_v =
            find_inspector_field(other_position_wizard.form, "parameter3");
        check("other_track_position_optional_defaults_enabled",
              radius_h && radius_v && !radius_h->disabled && !radius_v->disabled);
        const bool position_optional_dependency_ok =
            set_optional_argument(other_position_wizard.form, "parameter2", false) &&
            (radius_v = find_inspector_field(other_position_wizard.form, "parameter3")) &&
            radius_v->disabled &&
            !set_optional_argument(other_position_wizard.form, "parameter3", true) &&
            set_optional_argument(other_position_wizard.form, "parameter2", true);
        check("other_track_position_optional_dependency_ok",
              position_optional_dependency_ok);
        const bool other_position_fields_ok =
            set_field(other_position_wizard.form, "distance",
                      format_double(begin_distance, 6)) &&
            set_field(other_position_wizard.form, "trackKey", other_track_key) &&
            set_field(other_position_wizard.form, "parameter0", "2") &&
            set_field(other_position_wizard.form, "parameter1", "3") &&
            set_field(other_position_wizard.form, "parameter2", "4");
        check("other_track_position_wizard_fields_set", other_position_fields_ok);
        check("other_track_position_wizard_apply_ok",
              other_position_fields_ok && apply_wizard());
        const std::string other_position_edit_id = pending_insert_id_for(
            "otherTrack.change", "Track.Position");
        const TableRow* other_position_row = model_row(
            app.model_.other_track_changes, other_position_edit_id);
        const std::string canonical_other_track_key = "'" + other_track_key + "'";
        check("other_track_position_insert_state_ok",
              !other_position_edit_id.empty() &&
              pending_insert(other_position_edit_id) && other_position_row &&
               app.model_.other_track_changes.size() ==
                   baseline_other_track_change_count + 1 &&
               app.model_.other_tracks.size() == baseline_other_track_count + 1 &&
               table_cell_number(*other_position_row, "parameterCount") == 3.0 &&
               normalize_track_lookup_key(table_cell(*other_position_row, "trackKey")) ==
                   normalize_track_lookup_key(canonical_other_track_key) &&
               table_cell(*other_position_row, "filePath") == target_file);
        *out << "other_track_position_edit_id=" << other_position_edit_id << "\n"
             << "stage=other-track-position-created\n";

        check("other_track_same_key_template_found",
              prepare_wizard("other_track.x_interpolate"));
        NewElementWizardState& other_interpolate_wizard = app.new_element_wizard_;
        const bool other_interpolate_optional_shape_ok =
            set_optional_argument(other_interpolate_wizard.form, "parameter1", false) &&
            find_inspector_field(other_interpolate_wizard.form, "parameter1")->disabled;
        check("other_track_interpolate_optional_shape_ok",
              other_interpolate_optional_shape_ok);
        const bool other_interpolate_fields_ok =
            set_field(other_interpolate_wizard.form, "distance",
                      format_double(end_distance, 6)) &&
            set_field(other_interpolate_wizard.form, "trackKey",
                      ascii_lower(other_track_key)) &&
            set_field(other_interpolate_wizard.form, "parameter0", "4");
        check("other_track_same_key_wizard_fields_set", other_interpolate_fields_ok);
        check("other_track_same_key_wizard_apply_ok",
              other_interpolate_fields_ok && apply_wizard());
        const std::string other_interpolate_edit_id = pending_insert_id_for(
            "otherTrack.change", "Track.X.Interpolate");
        const TableRow* other_interpolate_row = model_row(
            app.model_.other_track_changes, other_interpolate_edit_id);
        check("other_track_same_key_reuses_track_ok",
              !other_interpolate_edit_id.empty() &&
              pending_insert(other_interpolate_edit_id) && other_interpolate_row &&
               app.pending_edit_changes_.size() == 2 &&
               app.model_.other_track_changes.size() ==
                   baseline_other_track_change_count + 2 &&
               app.model_.other_tracks.size() == baseline_other_track_count + 1 &&
               table_cell_number(*other_interpolate_row, "parameterCount") == 1.0 &&
               normalize_track_lookup_key(table_cell(*other_interpolate_row, "trackKey")) ==
                   normalize_track_lookup_key(canonical_other_track_key) &&
               table_cell(*other_interpolate_row, "filePath") == target_file);
        *out << "other_track_interpolate_edit_id=" << other_interpolate_edit_id << "\n"
             << "stage=other-track-same-key-created\n";

        check("other_track_cant_interpolate_template_found",
              prepare_wizard("other_track.cant_interpolate"));
        NewElementWizardState& other_cant_wizard = app.new_element_wizard_;
        const bool other_cant_optional_shape_ok =
            set_optional_argument(other_cant_wizard.form, "parameter0", false) &&
            find_inspector_field(other_cant_wizard.form, "parameter0")->disabled;
        check("other_track_cant_interpolate_optional_shape_ok",
              other_cant_optional_shape_ok);
        const bool other_cant_fields_ok =
            set_field(other_cant_wizard.form, "distance",
                      format_double(end_distance + 1.0, 6)) &&
            set_field(other_cant_wizard.form, "trackKey", other_track_key);
        check("other_track_cant_interpolate_wizard_fields_set", other_cant_fields_ok);
        check("other_track_cant_interpolate_wizard_apply_ok",
              other_cant_fields_ok && apply_wizard());
        const std::string other_cant_edit_id = pending_insert_id_for(
            "otherTrack.change", "Track.Cant.Interpolate");
        const TableRow* other_cant_row = model_row(
            app.model_.other_track_changes, other_cant_edit_id);
        check("other_track_cant_interpolate_no_argument_state_ok",
              !other_cant_edit_id.empty() && pending_insert(other_cant_edit_id) &&
              other_cant_row && app.pending_edit_changes_.size() == 3 &&
              app.model_.other_track_changes.size() ==
                  baseline_other_track_change_count + 3 &&
              app.model_.other_tracks.size() == baseline_other_track_count + 1 &&
              table_cell_number(*other_cant_row, "parameterCount") == 0.0 &&
              normalize_track_lookup_key(table_cell(*other_cant_row, "trackKey")) ==
                  normalize_track_lookup_key(canonical_other_track_key) &&
              table_cell(*other_cant_row, "filePath") == target_file);
        *out << "other_track_cant_edit_id=" << other_cant_edit_id << "\n"
             << "stage=other-track-cant-no-argument-created\n";

        check("other_track_insert_inspector_open_ok",
              app.open_element_inspector(other_position_edit_id, "otherTrack.change"));
        const bool other_track_edit_field_ok =
            set_field(app.inspector_, "parameter0", "6.5");
        check("other_track_insert_inspector_field_set", other_track_edit_field_ok);
        check("other_track_insert_followup_apply_ok",
              other_track_edit_field_ok && apply_inspector());
        const MapElementPendingChange* other_position_insert =
            pending_insert(other_position_edit_id);
        other_position_row = model_row(app.model_.other_track_changes,
                                       other_position_edit_id);
        check("other_track_insert_followup_preview_and_ledger_ok",
              other_position_insert && other_position_row &&
              pending_field(other_position_insert, "parameter0") == "6.5" &&
              std::abs(table_cell_number(*other_position_row, "parameter0") - 6.5) < 1e-9);
        *out << "stage=other-track-followup-applied\n";

        MapElementDeleteRequest other_interpolate_delete;
        other_interpolate_delete.edit_id = other_interpolate_edit_id;
        other_interpolate_delete.row_kind = "otherTrack.change";
        MapElementDeleteRequest other_position_delete;
        other_position_delete.edit_id = other_position_edit_id;
        other_position_delete.row_kind = "otherTrack.change";
        MapElementDeleteRequest other_cant_delete;
        other_cant_delete.edit_id = other_cant_edit_id;
        other_cant_delete.row_kind = "otherTrack.change";
        check("other_track_same_key_insert_cancel_ok",
              app.delete_element_target(other_interpolate_delete) &&
              app.delete_element_target(other_position_delete) &&
              app.delete_element_target(other_cant_delete));
        check("other_track_insert_cancel_restores_baseline",
              app.pending_edit_changes_.empty() &&
              app.model_.other_track_changes.size() == baseline_other_track_change_count &&
              app.model_.other_tracks.size() == baseline_other_track_count);
        *out << "stage=other-track-cancelled\n";

        if (options.commit) {
            check("commit_curve_pair_template_prepared",
                  prepare_wizard("curve"));
            NewElementWizardState& commit_curve_wizard = app.new_element_wizard_;
            commit_curve_wizard.own_track_add_end = false;
            update_own_track_wizard_field_enablement(commit_curve_wizard);
            const bool commit_curve_fields_ok =
                set_field(commit_curve_wizard.form, "transitionStart",
                          format_double(begin_distance, 6)) &&
                set_field(commit_curve_wizard.form, "distance",
                          format_double(end_distance, 6)) &&
                set_field(commit_curve_wizard.form, "radius", "450") &&
                set_field(commit_curve_wizard.form, "cant", "0.12");
            check("commit_curve_pair_fields_set", commit_curve_fields_ok);
            check("commit_curve_pair_apply_ok",
                  commit_curve_fields_ok && apply_wizard());
            const std::string commit_curve_transition_id = pending_insert_id_for(
                "curve", "Curve.BeginTransition");
            const std::string commit_curve_primary_id = pending_insert_id_for(
                "curve", "Curve.Begin");

            check("commit_gradient_pair_template_prepared",
                  prepare_wizard("gradient"));
            NewElementWizardState& commit_gradient_wizard = app.new_element_wizard_;
            commit_gradient_wizard.own_track_add_end = false;
            update_own_track_wizard_field_enablement(commit_gradient_wizard);
            const bool commit_gradient_fields_ok =
                set_field(commit_gradient_wizard.form, "transitionStart",
                          format_double(begin_distance, 6)) &&
                set_field(commit_gradient_wizard.form, "distance",
                          format_double(end_distance, 6)) &&
                set_field(commit_gradient_wizard.form, "gradient", "8");
            check("commit_gradient_pair_fields_set", commit_gradient_fields_ok);
            check("commit_gradient_pair_apply_ok",
                  commit_gradient_fields_ok && apply_wizard());
            const std::string commit_gradient_transition_id = pending_insert_id_for(
                "gradient", "Gradient.BeginTransition");
            const std::string commit_gradient_primary_id = pending_insert_id_for(
                "gradient", "Gradient.Begin");
            check("commit_pairs_pending_ledger_ok",
                  app.pending_edit_changes_.size() == 4 &&
                      commit_curve_transition_id == "insert-1-a-start-transition" &&
                      commit_curve_primary_id == "insert-1-b-start" &&
                      commit_gradient_transition_id == "insert-2-a-start-transition" &&
                      commit_gradient_primary_id == "insert-2-b-start");
            *out << "commit_curve_transition_edit_id=" << commit_curve_transition_id << "\n"
                 << "commit_curve_primary_edit_id=" << commit_curve_primary_id << "\n"
                 << "commit_gradient_transition_edit_id=" << commit_gradient_transition_id << "\n"
                 << "commit_gradient_primary_edit_id=" << commit_gradient_primary_id << "\n";
            const bool commit_ok = app.save_pending_edits(false);
            *out << "commit_save_ok=" << (commit_ok ? 1 : 0) << "\n"
                 << "committed_file=" << target_file << "\n";
            check("commit_save_path_ok",
                  commit_ok && app.pending_edit_changes_.empty() &&
                      app.model_.curve_rows.size() == baseline_curve_count + 2 &&
                      app.model_.gradient_rows.size() == baseline_gradient_count + 2);

            LoadResult disk_reload = load_map_worker(
                options.path, options.unit_distance, false, 0.0, 0.0,
                options.unit_distance, LoadModelOptions{true});
            const std::string entry_hash_after = disk_reload.ok
                ? source_hash_for_path(disk_reload.model, options.path)
                : std::string{};
            const std::string target_hash_after = disk_reload.ok
                ? source_hash_for_path(disk_reload.model, target_file)
                : std::string{};
            *out << "entry_source_hash_after=" << entry_hash_after << "\n"
                 << "target_source_hash_after=" << target_hash_after << "\n";
            check("commit_disk_reload_ok", disk_reload.ok);
            check("commit_target_source_hash_changed",
                  disk_reload.ok && !target_hash_after.empty() &&
                      target_hash_after != baseline_target_hash);
            check("commit_entry_source_hash_changed_when_target_is_entry",
                  disk_reload.ok && (target_file != options.path ||
                      entry_hash_after != baseline_entry_hash));
            const auto has_committed_row = [&](const std::vector<TableRow>& rows,
                                               const char* method,
                                               double distance,
                                               int argument_count) {
                return std::any_of(rows.begin(), rows.end(), [&](const TableRow& row) {
                    return table_cell(row, "method") == method &&
                        std::abs(table_cell_number(row, "distance") - distance) < 1e-9 &&
                        static_cast<int>(table_cell_number(row, "argumentCount")) ==
                            argument_count &&
                        table_cell(row, "filePath") == target_file;
                });
            };
            check("commit_reload_own_track_pairs_ok",
                  disk_reload.ok &&
                      has_committed_row(disk_reload.model.curve_rows,
                                        "Curve.BeginTransition", begin_distance, 0) &&
                      has_committed_row(disk_reload.model.curve_rows,
                                        "Curve.Begin", end_distance, 2) &&
                      has_committed_row(disk_reload.model.gradient_rows,
                                        "Gradient.BeginTransition", begin_distance, 0) &&
                      has_committed_row(disk_reload.model.gradient_rows,
                                        "Gradient.Begin", end_distance, 1));
            if (disk_reload.handle) kv_free(disk_reload.handle);
            *out << "stage=commit-and-disk-reload-complete\n";
        } else {
            check("deferred_revert_template_prepared",
                  prepare_wizard("structure.put0"));
            NewElementWizardState& deferred_revert_wizard =
                app.new_element_wizard_;
            const bool deferred_revert_fields_ok =
                set_field(deferred_revert_wizard.form, "distance",
                          format_double(begin_distance, 6)) &&
                set_field(deferred_revert_wizard.form, "structureKey",
                          structure_key) &&
                set_field(deferred_revert_wizard.form, "trackKey", track_key);
            check("deferred_revert_insert_created",
                  deferred_revert_fields_ok && apply_wizard() &&
                      !app.pending_edit_changes_.empty());
            const size_t deferred_revert_pending_count =
                app.pending_edit_changes_.size();
            app.request_edit_ui_operation(PendingEditUiOperation::Revert);
            app.process_pending_edit_ui_operation();
            check("deferred_revert_waits_for_present",
                  app.pending_edit_ui_operation_.operation ==
                      PendingEditUiOperation::Revert &&
                  app.pending_edit_changes_.size() ==
                      deferred_revert_pending_count);
            app.pending_edit_ui_operation_.progress_rendered = true;
            check("deferred_revert_progress_presented",
                  app.on_frame_presented() &&
                  !app.pending_edit_changes_.empty());
            app.process_pending_edit_ui_operation();
            check("deferred_revert_applied_after_present",
                  !app.edit_ui_operation_pending() &&
                  app.pending_edit_changes_.empty() &&
                  app.model_.structures.size() == baseline_structure_count);

            check("deferred_discard_template_prepared",
                  prepare_wizard("structure.put0"));
            NewElementWizardState& deferred_discard_wizard =
                app.new_element_wizard_;
            const bool deferred_discard_fields_ok =
                set_field(deferred_discard_wizard.form, "distance",
                          format_double(begin_distance, 6)) &&
                set_field(deferred_discard_wizard.form, "structureKey",
                          structure_key) &&
                set_field(deferred_discard_wizard.form, "trackKey", track_key);
            check("deferred_discard_insert_created",
                  deferred_discard_fields_ok && apply_wizard() &&
                      !app.pending_edit_changes_.empty());
            app.pending_close_action_ = PendingCloseAction::DisableEditMode;
            check("deferred_discard_queued",
                  !app.resolve_pending_close_action(false) &&
                  app.pending_edit_ui_operation_.operation ==
                      PendingEditUiOperation::DiscardAndResolveClose);
            app.process_pending_edit_ui_operation();
            check("deferred_discard_waits_for_present",
                  app.edit_mode_enabled_ &&
                  !app.pending_edit_changes_.empty() &&
                  app.pending_close_action_ ==
                      PendingCloseAction::DisableEditMode);
            app.pending_edit_ui_operation_.progress_rendered = true;
            check("deferred_discard_progress_presented",
                  app.on_frame_presented() &&
                  !app.pending_edit_changes_.empty());
            app.process_pending_edit_ui_operation();
            check("deferred_discard_applied_after_present",
                  !app.edit_ui_operation_pending() &&
                  app.pending_close_action_ == PendingCloseAction::None &&
                  !app.edit_mode_enabled_ &&
                  app.pending_edit_changes_.empty() &&
                  app.model_.structures.size() == baseline_structure_count);

            View2D degenerate_fit;
            degenerate_fit.fit(10.0, 20.0, 10.0, 20.0,
                               ImVec2(100.0f, 100.0f));
            check("degenerate_fit_scale_clamped",
                  degenerate_fit.scale == 10000.0);
            check("working_copy_reset_ok",
                  app.edit_memory_matches_pending_ledger_ &&
                  kv_edit_reset_memory(app.handle_) != 0);
            LoadResult disk_reload = load_map_worker(
                options.path, options.unit_distance, false, 0.0, 0.0,
                options.unit_distance, LoadModelOptions{true});
            const std::string entry_hash_after = disk_reload.ok
                ? source_hash_for_path(disk_reload.model, options.path)
                : std::string{};
            const std::string target_hash_after = disk_reload.ok
                ? source_hash_for_path(disk_reload.model, target_file)
                : std::string{};
            *out << "entry_source_hash_after=" << entry_hash_after << "\n"
                 << "target_source_hash_after=" << target_hash_after << "\n";
            check("disk_reload_ok", disk_reload.ok);
            check("disk_source_hashes_unchanged",
                  disk_reload.ok && entry_hash_after == baseline_entry_hash &&
                  target_hash_after == baseline_target_hash);
            if (disk_reload.handle) kv_free(disk_reload.handle);
            *out << "stage=reset-and-disk-check-complete\n";
        }
        for (const LogLine& line : app.logs_) {
            if (line.severity == LogSeverity::Error) {
                *out << "app_error=" << line.text << "\n";
            }
        }
    } catch (const std::exception& e) {
        *out << "exception=" << e.what() << "\n";
        ++failed_cases;
    }
    ImGui::EndFrame();
    ImPlot::DestroyContext();
    ImGui::DestroyContext();
    if (load.handle) kv_free(load.handle);

    *out << "result=" << (failed_cases == 0 ? "PASS" : "FAIL") << "\n";
    out->flush();
    return failed_cases == 0 ? 0 : 20;
}

int App::run_debug_headless_resource_list_replace(
    const HeadlessResourceListReplaceOptions& options) {
    std::ofstream output_file;
    std::ostream* out = &std::cout;
    if (!options.output_path.empty()) {
        output_file.open(std::filesystem::path(utf8_to_wide(options.output_path)),
                         std::ios::out | std::ios::trunc | std::ios::binary);
        if (!output_file) {
            std::cerr << "failed to open headless output: " << options.output_path << "\n";
            return 1;
        }
        out = &output_file;
    }
    *out << "command=debug-headless-resource-list-replace\n"
         << "map_path=" << options.path << "\n"
         << "memory_apply_only=1\n"
         << "resource_list_kind=Structure\n"
         << "stage=preview-load-start\n";
    out->flush();

    LoadModelOptions preview_options;
    preview_options.load_profile = "preview";
    LoadResult preview = load_map_worker(
        options.path, options.unit_distance, false, 0.0, 0.0,
        options.unit_distance, preview_options);
    if (!preview.ok) {
        *out << "preview_load_error=" << preview.error << "\nresult=FAIL\n";
        if (preview.handle) kv_free(preview.handle);
        return 2;
    }

    LoadModelOptions edit_options;
    edit_options.full_edit_registry = true;
    edit_options.load_profile = "edit";
    LoadResult edit_metadata = load_map_worker(
        options.path, options.unit_distance, false, 0.0, 0.0,
        options.unit_distance, edit_options);
    if (!edit_metadata.ok) {
        *out << "edit_metadata_load_error=" << edit_metadata.error << "\nresult=FAIL\n";
        if (preview.handle) kv_free(preview.handle);
        if (edit_metadata.handle) kv_free(edit_metadata.handle);
        return 3;
    }

    int failed_cases = 0;
    auto check = [&](const char* name, bool value) {
        *out << name << "=" << (value ? 1 : 0) << "\n";
        if (!value) ++failed_cases;
        return value;
    };
    const auto source_hashes = [](const MapModel& model) {
        std::map<std::string, std::string> hashes;
        for (const EditSourceFileInfo& file : model.edit_files) {
            hashes.emplace(file.file_path, file.source_hash);
        }
        return hashes;
    };
    const size_t structure_index = static_cast<size_t>(ResourceListKind::Structure);
    const std::string expected_edit_id =
        edit_metadata.model.resource_list_sources[structure_index].edit_id;

    ImGui::CreateContext();
    ImPlot::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(1280.0f, 720.0f);
    io.DeltaTime = 1.0f / 60.0f;
    io.IniFilename = nullptr;
    io.Fonts->AddFontDefault();
    io.Fonts->Build();
    ImGui::NewFrame();
    try {
        {
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

            *out << "stage=preview-load-complete\n";
            app.apply_edit_metadata_result(std::move(edit_metadata));
            edit_metadata.handle = nullptr;
            const ResourceListSource& merged_source =
                app.model_.resource_list_sources[structure_index];
            check("edit_metadata_merge_completed", app.edit_registry_loaded_);
            check("structure_source_present", merged_source.present);
            check("structure_edit_id_preserved",
                  !expected_edit_id.empty() &&
                      merged_source.edit_id == expected_edit_id);
            if (!merged_source.present || merged_source.edit_id.empty() ||
                merged_source.source_file_path.empty()) {
                throw std::runtime_error("Structure.Load source is not editable after metadata merge");
            }

            const std::string baseline_evaluated_path = merged_source.evaluated_path;
            const std::string baseline_resolved_path = merged_source.resolved_path;
            const std::map<std::string, std::string> baseline_source_hashes =
                source_hashes(app.model_);
            check("loaded_source_hashes_present", !baseline_source_hashes.empty());
            app.ensure_table_cache();
            check("baseline_structure_list_cache_ready",
                  app.table_cache_.valid &&
                      app.table_cache_.structure_model_rows.size() ==
                          app.model_.structure_models.size());

            app.request_resource_list_file_change(ResourceListKind::Structure);
            check("file_picker_request_queued",
                  app.pending_resource_list_file_change_request_.has_value());
            *out << "stage=file-dialog\n";
            out->flush();
            app.process_pending_resource_list_file_change();
            const DWORD dialog_error = CommDlgExtendedError();

            const ResourceListSource& applied_source =
                app.model_.resource_list_sources[structure_index];
            const auto pending = app.pending_edit_changes_.find(expected_edit_id);
            const bool source_path_changed =
                applied_source.evaluated_path != baseline_evaluated_path &&
                applied_source.resolved_path != baseline_resolved_path;
            const bool pending_update = pending != app.pending_edit_changes_.end() &&
                pending->second.row_kind == "resourceList.load" &&
                pending->second.operation == "update" &&
                pending->second.field_changes.find("resourceListPath") !=
                    pending->second.field_changes.end();
            *out << "dialog_error=" << dialog_error << "\n"
                 << "source_path_before=" << baseline_resolved_path << "\n"
                 << "source_path_after=" << applied_source.resolved_path << "\n";
            check("different_valid_structure_list_selected", source_path_changed);
            check("memory_apply_full_reparse_ok",
                  source_path_changed && pending_update &&
                      app.edit_memory_matches_pending_ledger_ && app.handle_);
            app.ensure_table_cache();
            check("structure_list_cache_refreshed",
                  source_path_changed && app.table_cache_.valid &&
                      app.table_cache_.structure_model_rows.size() ==
                          app.model_.structure_models.size());

            LoadResult disk_reload = load_map_worker(
                options.path, options.unit_distance, false, 0.0, 0.0,
                options.unit_distance, edit_options);
            const std::map<std::string, std::string> disk_source_hashes =
                disk_reload.ok ? source_hashes(disk_reload.model)
                               : std::map<std::string, std::string>{};
            *out << "disk_reload_ok=" << (disk_reload.ok ? 1 : 0) << "\n"
                 << "loaded_source_file_count=" << baseline_source_hashes.size() << "\n";
            check("all_loaded_disk_source_hashes_unchanged",
                  disk_reload.ok && disk_source_hashes == baseline_source_hashes);
            if (disk_reload.handle) kv_free(disk_reload.handle);
            for (const LogLine& line : app.logs_) {
                if (line.severity == LogSeverity::Error) {
                    *out << "app_error=" << line.text << "\n";
                }
            }
        }
    } catch (const std::exception& e) {
        *out << "exception=" << e.what() << "\n";
        ++failed_cases;
    }
    ImGui::EndFrame();
    ImPlot::DestroyContext();
    ImGui::DestroyContext();
    if (preview.handle) kv_free(preview.handle);
    if (edit_metadata.handle) kv_free(edit_metadata.handle);

    *out << "result=" << (failed_cases == 0 ? "PASS" : "FAIL") << "\n";
    out->flush();
    return failed_cases == 0 ? 0 : 21;
}

int App::run_debug_headless_resource_list_insert(
    const HeadlessResourceListInsertOptions& options) {
    std::ofstream output_file;
    std::ostream* out = &std::cout;
    if (!options.output_path.empty()) {
        output_file.open(std::filesystem::path(utf8_to_wide(options.output_path)),
                         std::ios::out | std::ios::trunc | std::ios::binary);
        if (!output_file) {
            std::cerr << "failed to open headless output: " << options.output_path << "\n";
            return 1;
        }
        out = &output_file;
    }
    *out << "command=debug-headless-resource-list-insert\n"
         << "map_path=" << options.path << "\n"
         << "kind=" << options.kind << "\n"
         << "memory_apply_only=1\n"
         << "stage=load-start\n";
    out->flush();

    LoadModelOptions load_options;
    load_options.full_edit_registry = true;
    load_options.load_profile = "edit";
    LoadResult load = load_map_worker(
        options.path, options.unit_distance, false, 0.0, 0.0,
        options.unit_distance, load_options);
    if (!load.ok) {
        *out << "load_error=" << load.error << "\nresult=FAIL\n";
        if (load.handle) kv_free(load.handle);
        return 2;
    }

    int failed_cases = 0;
    const auto check = [&](const char* name, bool value) {
        *out << name << "=" << (value ? 1 : 0) << "\n";
        if (!value) ++failed_cases;
        return value;
    };
    const auto source_hashes = [](const MapModel& model) {
        std::map<std::string, std::string> hashes;
        for (const EditSourceFileInfo& file : model.edit_files) {
            hashes.emplace(file.file_path, file.source_hash);
        }
        return hashes;
    };
    const auto source_text = [](void* handle, const std::string& path) {
        const char* text = kv_get_source_text(handle, path.c_str());
        std::string value = text ? text : "";
        kv_free_string(text);
        return value;
    };
    const auto csv_field_count_at = [](const std::string& text, size_t begin) {
        const size_t end = text.find_first_of("\r\n", begin);
        return static_cast<size_t>(std::count(
            text.begin() + static_cast<std::ptrdiff_t>(begin),
            text.begin() + static_cast<std::ptrdiff_t>(end == std::string::npos
                ? text.size() : end), ',')) + 1;
    };

    ImGui::CreateContext();
    ImPlot::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(1280.0f, 720.0f);
    io.DeltaTime = 1.0f / 60.0f;
    io.IniFilename = nullptr;
    io.Fonts->AddFontDefault();
    io.Fonts->Build();
    ImGui::NewFrame();
    try {
        UserSettings settings;
        settings.language = Language::En;
        App app(nullptr, settings, 1.0f, false, false);
        app.handle_ = load.handle;
        load.handle = nullptr;
        app.model_ = std::move(load.model);
        app.file_path_ = options.path;
        app.has_model_ = true;
        app.edit_mode_enabled_ = true;
        app.edit_registry_loaded_ = true;
        app.edit_memory_matches_pending_ledger_ = true;
        app.dmin_ = app.model_.default_min;
        app.dmax_ = app.model_.default_max;
        app.unit_distance_ = options.unit_distance;
        const std::map<std::string, std::string> baseline_hashes =
            source_hashes(app.model_);
        check("loaded_source_hashes_present", !baseline_hashes.empty());
        *out << "stage=load-complete\n";

        if (options.kind == "structure") {
            EditableListEditState& edit = app.structure_model_edit_;
            const EditableListSpec& spec = k_structure_model_edit_spec;
            app.ensure_table_cache();
            check("structure_drafts_initialized",
                  app.initialize_editable_list_draft_rows(edit, spec));
            if (edit.visible_rows.empty()) {
                throw std::runtime_error("Structure List contains no editable rows");
            }
            const EditableListDraftRow& original =
                edit.rows[edit.visible_rows.front()];
            const std::string original_key = original.values.empty()
                ? std::string{}
                : original.values.front();
            const std::string target_file = original.target_source_file;
            const ResourceListSource& load_source = app.model_.resource_list_sources[
                static_cast<size_t>(ResourceListKind::Structure)];
            check("structure_list_source_present", !target_file.empty());
            check("structure_load_comes_from_include",
                  !load_source.source_file_path.empty() &&
                      load_source.source_file_path != options.path);
            check("structure_original_key_present", !original_key.empty());

            constexpr const char* above_key = "kmeHeadlessStructureAbove";
            constexpr const char* below_key = "kmeHeadlessStructureBelow";
            check("structure_insert_above_draft",
                  app.insert_editable_list_row(edit, spec, 0, true));
            EditableListDraftRow& above = edit.rows[edit.visible_rows.front()];
            above.values[0] = above_key;
            check("structure_insert_below_draft",
                  app.insert_editable_list_row(edit, spec, 1, false));
            EditableListDraftRow& below =
                edit.rows[edit.visible_rows[2]];
            below.values[0] = below_key;
            const EditableListDraftRow& final_above =
                edit.rows[edit.visible_rows.front()];
            const EditableListDraftRow& final_below =
                edit.rows[edit.visible_rows[2]];
            check("structure_insert_field_counts",
                  final_above.values.size() == 2 &&
                      final_below.values.size() == 2);
            app.apply_editable_list_drafts(edit, spec);

            const std::string working = source_text(app.handle_, target_file);
            const size_t above_pos = working.find(std::string(above_key) + ",");
            const size_t original_pos = working.find(original_key + ",");
            const size_t below_pos = working.find(std::string(below_key) + ",");
            const size_t inserted_count = static_cast<size_t>(std::count_if(
                app.pending_edit_changes_.begin(), app.pending_edit_changes_.end(),
                [&](const auto& entry) {
                    return entry.second.operation == "insert" &&
                        entry.second.row_kind == spec.row_kind;
                }));
            check("structure_memory_apply_ok",
                  app.edit_memory_matches_pending_ledger_ && inserted_count == 2);
            check("structure_insert_order",
                  above_pos != std::string::npos &&
                      original_pos != std::string::npos &&
                      below_pos != std::string::npos &&
                      above_pos < original_pos && original_pos < below_pos);
            check("structure_insert_exact_csv_fields",
                  above_pos != std::string::npos && below_pos != std::string::npos &&
                      csv_field_count_at(working, above_pos) == 2 &&
                      csv_field_count_at(working, below_pos) == 2);
            check("structure_hydration_contains_insertions",
                  app.model_.structure_models.size() >= 2 &&
                      std::any_of(app.model_.structure_models.begin(),
                                  app.model_.structure_models.end(),
                                  [&](const TableRow& row) {
                                      return table_cell(row, "structureKey") == above_key;
                                  }) &&
                      std::any_of(app.model_.structure_models.begin(),
                                  app.model_.structure_models.end(),
                                  [&](const TableRow& row) {
                                      return table_cell(row, "structureKey") == below_key;
                                  }));
        } else {
            EditableListEditState& edit = app.signal_aspect_edit_;
            const EditableListSpec& spec = k_signal_aspect_edit_spec;
            app.ensure_table_cache();
            check("signal_drafts_initialized",
                  app.initialize_editable_list_draft_rows(edit, spec));
            int target_row = -1;
            for (size_t index = 0; index < edit.visible_rows.size(); ++index) {
                const EditableListDraftRow& candidate =
                    edit.rows[edit.visible_rows[index]];
                if (candidate.secondary_structure_field_count != 0 &&
                    candidate.primary_structure_field_count != 0 &&
                    candidate.values.size() > 1 &&
                    !candidate.values[0].empty() && !candidate.values[1].empty()) {
                    target_row = static_cast<int>(index);
                    break;
                }
            }
            if (target_row < 0) {
                throw std::runtime_error(
                    "Signal Aspects List contains no editable main/glare pair");
            }
            const EditableListDraftRow& original = edit.rows[
                edit.visible_rows[static_cast<size_t>(target_row)]];
            const std::string original_key = original.values[0];
            const std::string structure_key = original.values[1];
            const std::string target_file = original.target_source_file;
            const std::string original_source_block = original.payload_raw_statement;
            check("signal_list_source_present", !target_file.empty());

            constexpr const char* above_key = "kmeHeadlessSignalAbove";
            constexpr const char* below_key = "kmeHeadlessSignalBelow";
            check("signal_insert_above_draft",
                  app.insert_editable_list_row(edit, spec, target_row, true));
            EditableListDraftRow& above = edit.rows[
                edit.visible_rows[static_cast<size_t>(target_row)]];
            above.values[0] = above_key;
            above.values[1] = structure_key;
            const int original_after_above = target_row + 1;
            check("signal_insert_below_pair_draft",
                  app.insert_editable_list_row(
                      edit, spec, original_after_above, false));
            EditableListDraftRow& below = edit.rows[
                edit.visible_rows[static_cast<size_t>(original_after_above + 1)]];
            below.values[0] = below_key;
            below.values[1] = structure_key;
            check("signal_add_glare_draft",
                  app.add_editable_list_secondary_row(
                      edit, spec, original_after_above + 1));
            check("signal_cancel_new_glare_draft",
                  app.delete_editable_list_secondary_row(
                      edit, spec, original_after_above + 1));
            const EditableListDraftRow& glare_cancelled = edit.rows[
                edit.visible_rows[static_cast<size_t>(original_after_above + 1)]];
            check("signal_new_glare_cancelled_without_delete",
                  glare_cancelled.values.size() == 6 &&
                      glare_cancelled.secondary_structure_field_count == 0 &&
                      !glare_cancelled.secondary_row_added);
            check("signal_readd_glare_draft",
                  app.add_editable_list_secondary_row(
                      edit, spec, original_after_above + 1));
            const EditableListDraftRow& final_above = edit.rows[
                edit.visible_rows[static_cast<size_t>(target_row)]];
            EditableListDraftRow& below_with_glare = edit.rows[
                edit.visible_rows[static_cast<size_t>(original_after_above + 1)]];
            check("signal_insert_field_counts",
                  final_above.values.size() == 6 &&
                      below_with_glare.values.size() == 11 &&
                      below_with_glare.secondary_structure_field_count == 5);
            below_with_glare.values[6] = structure_key;
            app.apply_editable_list_drafts(edit, spec);

            const std::string working = source_text(app.handle_, target_file);
            const size_t above_pos = working.find(std::string(above_key) + ",");
            const size_t original_pos = working.find(original_key + ",");
            const size_t original_block_end = original_pos == std::string::npos
                ? std::string::npos
                : original_pos + original_source_block.size();
            const size_t below_pos = working.find(std::string(below_key) + ",");
            const size_t below_glare_pos = below_pos == std::string::npos
                ? std::string::npos
                : working.find("\n,", below_pos);
            *out << "signal_positions=" << above_pos << "," << original_pos << ","
                 << original_block_end << "," << below_pos << ","
                 << below_glare_pos << "\n";
            const auto signal_row_by_key = [&](const std::string& key) -> const TableRow* {
                const auto found = std::find_if(
                    app.model_.signal_aspects.begin(),
                    app.model_.signal_aspects.end(),
                    [&](const TableRow& row) {
                        return table_cell(row, "signalAspectKey") == key;
                    });
                return found == app.model_.signal_aspects.end() ? nullptr : &*found;
            };
            const TableRow* above_row = signal_row_by_key(above_key);
            const TableRow* below_row = signal_row_by_key(below_key);
            check("signal_memory_apply_ok", app.edit_memory_matches_pending_ledger_);
            check("signal_existing_pair_not_split",
                  above_pos != std::string::npos &&
                      original_pos != std::string::npos &&
                      below_pos != std::string::npos &&
                      !original_source_block.empty() &&
                      above_pos < original_pos && original_block_end <= below_pos);
            check("signal_insert_exact_csv_fields",
                  above_pos != std::string::npos && below_pos != std::string::npos &&
                      below_glare_pos != std::string::npos &&
                      csv_field_count_at(working, above_pos) == 6 &&
                      csv_field_count_at(working, below_pos) == 6 &&
                      csv_field_count_at(working, below_glare_pos + 1) == 6);
            check("signal_default_and_manual_glare_binding",
                  above_row && below_row &&
                      table_cell_number(*above_row,
                                        "_signalGlareStructureKeyCount") == 0.0 &&
                      table_cell_number(*below_row,
                                        "_signalGlareStructureKeyCount") > 0.0);
        }

        check("memory_reset_ok", kv_edit_reset_memory(app.handle_) != 0);
        LoadResult disk_reload = load_map_worker(
            options.path, options.unit_distance, false, 0.0, 0.0,
            options.unit_distance, load_options);
        const std::map<std::string, std::string> disk_hashes = disk_reload.ok
            ? source_hashes(disk_reload.model)
            : std::map<std::string, std::string>{};
        check("disk_reload_ok", disk_reload.ok);
        check("all_disk_source_hashes_unchanged",
              disk_reload.ok && disk_hashes == baseline_hashes);
        if (disk_reload.handle) kv_free(disk_reload.handle);
    } catch (const std::exception& e) {
        *out << "exception=" << e.what() << "\n";
        ++failed_cases;
    }
    ImGui::EndFrame();
    ImPlot::DestroyContext();
    ImGui::DestroyContext();
    if (load.handle) kv_free(load.handle);

    *out << "result=" << (failed_cases == 0 ? "PASS" : "FAIL") << "\n";
    out->flush();
    return failed_cases == 0 ? 0 : 22;
}

int App::run_debug_headless_new_file_wizard(
    const HeadlessNewFileWizardOptions& options) {
    std::ofstream output_file;
    std::ostream* out = &std::cout;
    if (!options.output_path.empty()) {
        output_file.open(std::filesystem::path(utf8_to_wide(options.output_path)),
                         std::ios::out | std::ios::trunc | std::ios::binary);
        if (!output_file) {
            std::cerr << "failed to open headless output: " << options.output_path << "\n";
            return 1;
        }
        out = &output_file;
    }
    *out << "command=debug-headless-new-file-wizard\n"
         << "map_path=" << options.path << "\n";

    const auto read_file_bytes = [](const std::filesystem::path& path) {
        std::ifstream file(path, std::ios::binary);
        return std::string((std::istreambuf_iterator<char>(file)),
                           std::istreambuf_iterator<char>());
    };
    std::error_code path_error;
    const std::filesystem::path tests_directory =
        std::filesystem::absolute(std::filesystem::path(L"tests"), path_error).lexically_normal();
    const std::filesystem::path requested_path(utf8_to_wide(options.path));
    const std::filesystem::path map_path =
        std::filesystem::absolute(requested_path, path_error).lexically_normal();
    const std::filesystem::path relative_to_tests = map_path.lexically_relative(tests_directory);
    const bool path_is_under_tests = !path_error && !requested_path.empty() &&
        !map_path.filename().empty() && !relative_to_tests.empty() &&
        !relative_to_tests.is_absolute() && relative_to_tests.begin() != relative_to_tests.end() &&
        *relative_to_tests.begin() != "..";
    std::error_code exists_error;
    if (!path_is_under_tests || !std::filesystem::is_directory(map_path.parent_path(), path_error) ||
        path_error || std::filesystem::exists(map_path, exists_error) || exists_error) {
        *out << "error=new-file wizard headless path must be a nonexistent file below tests/\n"
             << "result=FAIL\n";
        out->flush();
        return 2;
    }

    struct ResourceFile {
        NewFileKind kind;
        ResourceListKind source_kind;
        const char* name;
        const char* header;
        const char* statement;
    };
    const std::array<ResourceFile, 5> resources = {{
        {NewFileKind::Structure, ResourceListKind::Structure, "new-structure.csv",
         "BveTs Structure List 2.00:utf-8\r\n",
         "Structure.Load('new-structure.csv');\r\n"},
        {NewFileKind::Signal, ResourceListKind::Signal, "new-signal.csv",
         "BveTs Signal Aspects List 2.00:utf-8\r\n",
         "Signal.Load('new-signal.csv');\r\n"},
        {NewFileKind::Sound, ResourceListKind::Sound, "new-sound.csv",
         "BveTs Sound List 2.00:utf-8\r\n",
         "Sound.Load('new-sound.csv');\r\n"},
        {NewFileKind::Sound3D, ResourceListKind::Sound3D, "new-sound3d.csv",
         "BveTs Sound List 2.00:utf-8\r\n",
         "Sound3D.Load('new-sound3d.csv');\r\n"},
        {NewFileKind::Station, ResourceListKind::Station, "new-station.csv",
         "BveTs Station List 2.00:utf-8\r\n",
         "Station.Load('new-station.csv');\r\n"},
    }};

    int failed_cases = 0;
    auto check = [&](const char* label, bool value) {
        *out << label << '=' << (value ? 1 : 0) << "\n";
        if (!value) ++failed_cases;
        return value;
    };
    std::vector<std::filesystem::path> created_files;
    const auto cleanup_created_files = [&]() {
        for (auto it = created_files.rbegin(); it != created_files.rend(); ++it) {
            std::error_code error;
            std::filesystem::remove(*it, error);
        }
    };

    ImGui::CreateContext();
    ImPlot::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(1280.0f, 720.0f);
    io.DeltaTime = 1.0f / 60.0f;
    io.IniFilename = nullptr;
    io.Fonts->AddFontDefault();
    io.Fonts->Build();
    ImGui::NewFrame();
    try {
        {
            UserSettings settings;
            settings.language = Language::En;
            App app(nullptr, settings, 1.0f, false, false);
            const std::string directory = wide_to_utf8(map_path.parent_path().wstring());
            const std::string map_name = wide_to_utf8(map_path.filename().wstring());
            app.request_new_file_create({NewFileKind::Map, map_name, directory, {}});
            app.process_pending_new_file_create();
            check("blank_map_created",
                  std::filesystem::exists(map_path) &&
                      read_file_bytes(map_path) == "BveTs Map 2.02:utf-8\r\n");
            if (!std::filesystem::exists(map_path)) {
                throw std::runtime_error("new map confirmation did not create the requested file");
            }
            created_files.push_back(map_path);
            std::string map_overwrite_error;
            check("blank_map_overwrite_rejected",
                  !create_utf8_bve_file_exclusive(
                      map_path, new_bve_file_header(NewFileKind::Map), map_overwrite_error) &&
                      read_file_bytes(map_path) == "BveTs Map 2.02:utf-8\r\n");

            LoadModelOptions edit_options;
            edit_options.full_edit_registry = true;
            edit_options.load_profile = "edit";
            const std::string map_path_utf8 = wide_to_utf8(map_path.wstring());
            LoadResult load = load_map_worker(
                map_path_utf8, options.unit_distance, false, 0.0, 0.0,
                options.unit_distance, edit_options);
            if (!load.ok) {
                if (load.handle) kv_free(load.handle);
                throw std::runtime_error("blank map load failed: " + load.error);
            }
            app.handle_ = load.handle;
            load.handle = nullptr;
            app.model_ = std::move(load.model);
            app.file_path_ = map_path_utf8;
            app.has_model_ = true;
            app.edit_mode_enabled_ = true;
            app.edit_registry_loaded_ = true;
            app.edit_memory_matches_pending_ledger_ = true;
            app.dmin_ = app.model_.default_min;
            app.dmax_ = app.model_.default_max;
            app.unit_distance_ = options.unit_distance;
            const std::vector<std::string> target_candidates =
                new_element_target_candidates(app.model_);
            check("blank_map_is_reference_candidate", target_candidates.size() == 1);
            if (target_candidates.empty()) {
                throw std::runtime_error("blank map has no editable reference target");
            }
            const std::string target_file_path = target_candidates.front();

            for (const ResourceFile& resource : resources) {
                const std::filesystem::path resource_path = map_path.parent_path() / resource.name;
                const size_t pending_before = app.pending_edit_changes_.size();
                app.request_new_file_create(
                    {resource.kind, resource.name, directory, target_file_path});
                app.process_pending_new_file_create();
                check((std::string("created_") + resource.name).c_str(),
                      std::filesystem::exists(resource_path) &&
                          read_file_bytes(resource_path) == resource.header);
                if (!std::filesystem::exists(resource_path)) {
                    throw std::runtime_error("resource-list confirmation did not create " +
                                             std::string(resource.name));
                }
                created_files.push_back(resource_path);
                std::string overwrite_error;
                check((std::string("overwrite_rejected_") + resource.name).c_str(),
                      !create_utf8_bve_file_exclusive(
                          resource_path, new_bve_file_header(resource.kind), overwrite_error) &&
                          read_file_bytes(resource_path) == resource.header);
                const auto staged = std::find_if(
                    app.pending_edit_changes_.begin(), app.pending_edit_changes_.end(),
                    [&](const auto& item) {
                        const MapElementPendingChange& change = item.second;
                        const auto kind = change.field_changes.find("resourceListKind");
                        const auto path = change.field_changes.find("resourceListPath");
                        return change.operation == "insert" &&
                            change.row_kind == "resourceList.load" &&
                            kind != change.field_changes.end() &&
                            path != change.field_changes.end() &&
                            kind->second == std::string(new_file_resource_list_kind(resource.kind)) &&
                            path->second == resource.name;
                    });
                check((std::string("reference_staged_") + resource.name).c_str(),
                      app.pending_edit_changes_.size() == pending_before + 1 &&
                          staged != app.pending_edit_changes_.end() &&
                          app.edit_memory_matches_pending_ledger_);
            }
            check("five_resource_references_pending", app.pending_edit_changes_.size() == resources.size());
            check("save_commits_resource_references", app.save_pending_edits(false));

            const std::string map_text = read_file_bytes(map_path);
            size_t last_position = 0;
            bool statement_order_matches =
                map_text.rfind("BveTs Map 2.02:utf-8\r\n", 0) == 0;
            for (const ResourceFile& resource : resources) {
                const size_t position = map_text.find(resource.statement);
                statement_order_matches = statement_order_matches &&
                    position != std::string::npos && position >= last_position;
                last_position = position == std::string::npos
                    ? last_position
                    : position + std::string_view(resource.statement).size();
            }
            check("saved_map_has_ordered_resource_references", statement_order_matches);

            LoadResult reloaded = load_map_worker(
                map_path_utf8, options.unit_distance, false, 0.0, 0.0,
                options.unit_distance, edit_options);
            bool reload_ok = reloaded.ok;
            if (reloaded.ok) {
                for (const ResourceFile& resource : resources) {
                    const ResourceListSource& source = reloaded.model.resource_list_sources[
                        static_cast<size_t>(resource.source_kind)];
                    reload_ok = reload_ok && source.present &&
                        source.evaluated_path == resource.name;
                }
                reload_ok = reload_ok && reloaded.model.structure_models.empty() &&
                    reloaded.model.station_definition_rows.empty() &&
                    reloaded.model.signal_aspects.empty() && reloaded.model.sound_list.empty() &&
                    reloaded.model.sound_3d_list.empty();
            }
            check("reloaded_empty_resource_lists_match", reload_ok);
            if (reloaded.handle) kv_free(reloaded.handle);
        }
        cleanup_created_files();
        check("created_files_cleaned", std::all_of(
            created_files.begin(), created_files.end(), [](const std::filesystem::path& path) {
                std::error_code error;
                return !std::filesystem::exists(path, error);
            }));
    } catch (const std::exception& e) {
        cleanup_created_files();
        *out << "exception=" << e.what() << "\n";
        ++failed_cases;
    }
    ImGui::EndFrame();
    ImPlot::DestroyContext();
    ImGui::DestroyContext();
    *out << "result=" << (failed_cases == 0 ? "PASS" : "FAIL") << "\n";
    out->flush();
    return failed_cases == 0 ? 0 : 22;
}
#endif
