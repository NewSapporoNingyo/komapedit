/*
 * Copyright (c) 2026 Sapporo_ningyo
 *
 * Licensed under Apache License 2.0; see LICENSE and NOTICE.
 */

#include "kme.h"
#include "app_settings.h"
#include "debug_headless.h"
#include "maploader.h"
#include "../table/datatable_internal.h"
#include "imgui.h"
#include "implot.h"

#include <windows.h>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <set>
#include <stdexcept>
#include <thread>

#ifndef NDEBUG
namespace {
std::string read_creator_message_source(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("cannot read protected source: " + path.u8string());
    const std::string bytes((std::istreambuf_iterator<char>(input)), {});
    if (input.bad()) throw std::runtime_error("source read failed: " + path.u8string());
    return bytes;
}

struct CreatorMessageFixtureDirectory {
    std::filesystem::path path;
    ~CreatorMessageFixtureDirectory() {
        std::error_code error;
        if (!path.empty()) std::filesystem::remove_all(path, error);
    }
};

struct CreatorMessageGuiContext {
    CreatorMessageGuiContext() {
        ImGui::CreateContext();
        ImPlot::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.DisplaySize = ImVec2(1280.0f, 720.0f);
        io.DeltaTime = 1.0f / 60.0f;
        io.Fonts->AddFontDefault();
        io.Fonts->Build();
        ImGui::NewFrame();
    }
    ~CreatorMessageGuiContext() {
        ImGui::EndFrame();
        ImPlot::DestroyContext();
        ImGui::DestroyContext();
    }
};
} // namespace

int App::run_debug_headless_creator_message(const HeadlessCreatorMessageOptions& options) {
    std::ofstream output_file;
    std::ostream* out = &std::cout;
    if (!options.output_path.empty()) {
        output_file.open(std::filesystem::u8path(options.output_path), std::ios::binary);
        if (!output_file) return 1;
        out = &output_file;
    }
    *out << "command=debug-headless-creator-message\npath=" << options.path
         << "\nreal_sources_read_only=1\n";
    int failures = 0;
    const auto check = [&](const std::string& name, bool value) {
        *out << name << '=' << (value ? "PASS" : "FAIL") << '\n';
        out->flush();
        if (!value) ++failures;
        return value;
    };
    const auto require = [&](const std::string& name, bool value) {
        if (!check(name, value)) throw std::runtime_error(name);
    };
    const auto same_path = [](const std::string& first, const std::string& second) {
        return normalized_path_key(first) == normalized_path_key(second);
    };
    std::map<std::filesystem::path, std::string> protected_sources;
    CreatorMessageFixtureDirectory fixtures;
    CreatorMessageGuiContext gui_context;
    try {
        const auto root = std::filesystem::temp_directory_path() /
            ("komapedit-creator-message-" + std::to_string(GetCurrentProcessId()) + "-" +
             std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        require("exclusive_fixture_directory", std::filesystem::create_directory(root));
        fixtures.path = root;
        const auto create = [&](const std::filesystem::path& path, const std::string& bytes) {
            std::string error;
            if (!create_utf8_bve_file_exclusive(path, bytes, error))
                throw std::runtime_error("fixture creation failed: " + error);
        };
        const auto map_path = root / "map.txt";
        const auto child_path = root / "child.txt";
        const auto scenario_path = root / "scenario.txt";
        const auto empty_path = root / "empty.txt";
        const auto ordered_path = root / "ordered.txt";
        const auto draft_state_path = root / "draft-state.txt";
        const std::string original_map =
            "BveTs Map 2.02:utf-8\r\n\r\n"
            "//--kme--message-from-creator:\"Root message\"\r\n"
            "# Preserve this original comment.\r\n0;\r\n"
            "Curve.SetGauge(1.067);\r\ninclude 'child.txt';\r\n"
            "1000;\r\nGradient.Interpolate(0);\r\n";
        const std::string original_child =
            "BveTs Map 2.02:utf-8\r\n\r\n"
            "//--kme--message-from-creator:\"Included message\"\r\n"
            "# Preserve the included source.\r\nDrawDistance.Change(600);\r\n";
        create(map_path, original_map);
        create(child_path, original_child);
        create(scenario_path, "BveTs Scenario 2.00:utf-8\r\nTitle = Message fixture\r\nRoute = map.txt\r\n");
        create(empty_path, "BveTs Map 2.02:utf-8\r\n0;\r\n1000;\r\n");
        const std::string ordered_baseline = "BveTs Map 2.02:utf-8\r\n0;\r\n1000;\r\n";
        create(ordered_path, ordered_baseline);
        const std::string draft_state_baseline =
            "BveTs Map 2.02:utf-8\r\n\r\n//--kme--message-from-creator:\"A\"\r\n0;\r\n1000;\r\n";
        create(draft_state_path, draft_state_baseline);

        UserSettings settings;
        settings.language = Language::En;
        settings.edit_mode_enabled = true;
        settings.path = root / "settings.ini";
        require("isolated_settings_created", save_user_settings(settings));
        App app(nullptr, settings, 1.0f, false, false);
        app.history_path_ = root / "history.ini";
        app.recent_maps_.clear();
        app.creator_message_history_.clear();
        app.unit_distance_ = options.unit_distance;
        app.scene_auto_load_on_map_open_ = false;
        const auto wait_for_map = [&]() {
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(300);
            while (std::chrono::steady_clock::now() < deadline) {
                if (app.load_state_.running) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                    continue;
                }
                app.stop_loader();
                bool pending = false;
                {
                    std::lock_guard<std::mutex> lock(app.load_state_.result_mutex);
                    pending = app.load_state_.pending_result.has_value();
                    if (pending && !app.load_state_.pending_result->ok) return false;
                }
                if (pending) {
                    app.poll_loader();
                    continue;
                }
                return app.has_model_ && app.handle_ && app.edit_registry_loaded_;
            }
            return false;
        };
        const auto open = [&](const std::string& path, int scenario_index) {
            app.open_document(path, true);
            if (!app.scenario_route_pick_.items.empty()) {
                require("scenario_index_in_range", scenario_index >= 0 &&
                    static_cast<size_t>(scenario_index) < app.scenario_route_pick_.items.size());
                app.scenario_route_pick_.selected = scenario_index;
                app.confirm_scenario_route_selection();
            }
            require("document_open_completed", wait_for_map());
        };
        const auto history = [&](const std::string& path) -> const CreatorMessageHistory* {
            const auto found = std::find_if(app.creator_message_history_.begin(),
                app.creator_message_history_.end(), [&](const CreatorMessageHistory& entry) {
                    return same_path(entry.path, path);
                });
            return found == app.creator_message_history_.end() ? nullptr : &*found;
        };
        const auto row_id = [&](const std::string& content) {
            const auto& rows = app.model_.creator_messages;
            const auto found = std::find_if(rows.begin(), rows.end(), [&](const TableRow& row) {
                return table_cell(row, "content") == content;
            });
            return found == rows.end() ? std::string{} : found->edit_id;
        };
        const auto report_edit_failure = [&](const char* stage) {
            const char* error = kv_get_last_error();
            *out << "failure_stage=" << stage << "\nmaploader_error=" << (error ? error : "")
                 << "\nloaded_map=" << app.file_path_
                 << "\nwizard_target=" << app.new_element_wizard_.target_file_path
                 << "\nform_source=" << app.new_element_wizard_.form.source_file
                 << "\npending_change_count=" << app.pending_edit_changes_.size() << '\n';
            std::lock_guard<std::mutex> lock(app.log_mutex_);
            const size_t first = app.logs_.size() > 8 ? app.logs_.size() - 8 : 0;
            for (size_t i = first; i < app.logs_.size(); ++i)
                *out << "app_log=" << app.logs_[i].text << '\n';
            out->flush();
        };

        *out << "stage=real-document-read-only\n";
        const auto input = std::filesystem::absolute(std::filesystem::u8path(options.path)).lexically_normal();
        protected_sources.emplace(input, read_creator_message_source(input));
        open(input.u8string(), options.scenario_index);
        require("real_edit_source_set_available", !app.model_.edit_files.empty());
        for (const auto& source : app.model_.edit_files) {
            const auto path = std::filesystem::u8path(source.file_path);
            protected_sources.emplace(path, read_creator_message_source(path));
        }
        *out << "real_protected_source_count=" << protected_sources.size()
             << "\nreal_message_count=" << app.model_.creator_messages.size() << '\n';
        require("real_messages_have_merged_source_metadata", std::all_of(
            app.model_.creator_messages.begin(), app.model_.creator_messages.end(),
            [](const TableRow& row) { return !row.edit_id.empty() && !row.source.file_path.empty(); }));
        if (app.creator_message_popup_.requested) app.confirm_creator_message_popup();

        *out << "stage=fixture-open-and-suppression\n";
        open(scenario_path.u8string(), 0);
        require("preview_and_edit_metadata_merge", app.model_.creator_messages.size() == 2 &&
            !row_id("Root message").empty() && !row_id("Included message").empty());
        require("popup_uses_resolved_map_and_order", app.creator_message_popup_.requested &&
            same_path(app.creator_message_popup_.map_path, map_path.u8string()) &&
            app.creator_message_popup_.contents == std::vector<std::string>{"Root message", "Included message"});
        std::set<std::string> source_paths;
        for (const auto& row : app.model_.creator_messages)
            source_paths.insert(normalized_path_key(row.source.file_path));
        require("messages_retain_two_physical_sources", source_paths.size() == 2);
        app.ensure_table_cache();
        require("message_table_cache_hydrated", app.table_cache_.creator_message_rows.size() == 2);
        app.creator_message_popup_.suppressed = true;
        app.confirm_creator_message_popup();
        const auto* suppressed = history(map_path.u8string());
        require("suppression_stored_by_map", suppressed && suppressed->has_messages && suppressed->suppressed &&
            !history(scenario_path.u8string()));
        const auto saved_history = load_history_state(app.history_path_);
        require("suppression_persisted", std::any_of(saved_history.creator_messages.begin(),
            saved_history.creator_messages.end(), [&](const CreatorMessageHistory& entry) {
                return same_path(entry.path, map_path.u8string()) && entry.has_messages && entry.suppressed;
            }));
        open(map_path.u8string(), 0);
        require("direct_open_shares_scenario_suppression", !app.creator_message_popup_.requested);

        *out << "stage=draft-apply-and-revert\n";
        require("draft_created", app.set_creator_message_draft(row_id("Root message"), "Temporary edit", false));
        require("unapplied_draft_blocks_save", app.has_creator_message_drafts() && !app.save_pending_edits(false));
        require("draft_applied_to_memory", app.apply_creator_message_drafts() &&
            !app.has_creator_message_drafts() && !row_id("Temporary edit").empty() && app.has_pending_edits());
        require("apply_preserves_disk_and_popup", read_creator_message_source(map_path) == original_map &&
            read_creator_message_source(child_path) == original_child && !app.creator_message_popup_.requested);
        require("revert_restores_message", app.revert_all_pending_edits() &&
            !row_id("Root message").empty() && row_id("Temporary edit").empty() && !app.has_pending_edits());
        for (const bool deleted : {false, true}) {
            const std::string label = deleted ? "unapplied_delete" : "unapplied_update";
            require(label + "_draft_created", app.set_creator_message_draft(row_id("Root message"),
                deleted ? "Root message" : "Draft only", deleted));
            require(label + "_toolbar_revert_enabled", app.edit_actions_available() &&
                !app.load_state_.running && !app.edit_ui_operation_pending() &&
                !app.has_pending_edits() && app.has_unsaved_edit_state());
            app.request_edit_ui_operation(PendingEditUiOperation::Revert);
            require(label + "_revert_deferred", app.edit_ui_operation_pending());
            app.pending_edit_ui_operation_.progress_presented = true;
            app.process_pending_edit_ui_operation();
            require(label + "_toolbar_revert_clears_draft", !app.edit_ui_operation_pending() &&
                !app.has_unsaved_edit_state() && app.creator_message_drafts_.empty() &&
                !row_id("Root message").empty() && app.model_.creator_messages.size() == 2 &&
                read_creator_message_source(map_path) == original_map &&
                read_creator_message_source(child_path) == original_child);
        }

        *out << "stage=cell-edit-and-wrapped-layout\n";
        require("cell_edit_begins", app.begin_creator_message_cell_edit(row_id("Root message")));
        app.creator_message_cell_edit_.buffer = "Cell draft";
        require("active_cell_blocks_save", app.has_creator_message_drafts() && !app.save_pending_edits(false));
        app.request_edit_ui_operation(PendingEditUiOperation::ApplyCreatorMessages);
        require("cell_apply_deferred", app.edit_ui_operation_pending() && !row_id("Root message").empty());
        app.pending_edit_ui_operation_.progress_presented = true;
        app.process_pending_edit_ui_operation();
        require("one_apply_collects_active_cell", !row_id("Cell draft").empty() &&
            app.creator_message_cell_edit_.edit_id.empty() && !app.has_creator_message_drafts() &&
            read_creator_message_source(map_path) == original_map);
        require("cell_apply_reverted", app.revert_all_pending_edits() && !row_id("Root message").empty());
        require("cell_revert_begins", app.begin_creator_message_cell_edit(row_id("Root message")));
        app.creator_message_cell_edit_.buffer = "Unfinished input";
        require("revert_clears_active_input", app.revert_all_pending_edits() &&
            app.creator_message_cell_edit_.edit_id.empty() && !app.has_unsaved_edit_state());
        const std::string root_id = row_id("Root message");
        require("context_delete_staged", app.stage_creator_message_delete(root_id) &&
            !row_id("Root message").empty() && app.creator_message_drafts_.at(root_id).deleted);
        require("pending_delete_not_editable", !app.begin_creator_message_cell_edit(root_id) &&
            !app.stage_creator_message_delete(root_id));
        app.ensure_table_cache();
        snprintf(app.creator_message_find_.query, sizeof(app.creator_message_find_.query), "%s", "Root message");
        app.run_creator_message_find();
        require("search_excludes_deleted_draft", app.creator_message_find_.matches.empty());
        require("context_delete_apply_memory_only", app.apply_creator_message_drafts() &&
            row_id("Root message").empty() && read_creator_message_source(map_path) == original_map);
        require("context_delete_reverted", app.revert_all_pending_edits() && !row_id("Root message").empty());
        app.edit_mode_enabled_ = false;
        require("readonly_cell_actions_blocked", !app.begin_creator_message_cell_edit(root_id) &&
            !app.stage_creator_message_delete(root_id));
        app.edit_mode_enabled_ = true;

        const std::string long_text =
            u8"中文消息日本語メッセージ English words " + std::string(180, 'X');
        require("wrapped_draft_created", app.set_creator_message_draft(root_id, long_text, false));
        app.ensure_table_cache();
        app.ensure_creator_message_layout(240.0f);
        const auto wide_offsets = app.table_cache_.creator_message_layout.offsets;
        require("mixed_height_rows", wide_offsets.size() == 3 &&
            wide_offsets[1] > wide_offsets[2] - wide_offsets[1]);
        std::string unwrapped = app.table_cache_.creator_message_layout.text[0];
        unwrapped.erase(std::remove(unwrapped.begin(), unwrapped.end(), '\n'), unwrapped.end());
        require("wrapping_preserves_utf8_and_literal_text", unwrapped == long_text);
        const auto visible = datatable_internal::visible_wrapped_table_rows(wide_offsets,
            wide_offsets[1], wide_offsets[2]);
        require("variable_height_visible_range", visible.first == 1 && visible.second == 2);
        app.ensure_creator_message_layout(120.0f);
        require("column_resize_rewraps", app.table_cache_.creator_message_layout.offsets[1] > wide_offsets[1]);
        require("shorter_draft_invalidates_layout", app.set_creator_message_draft(root_id, "Short", false) &&
            !app.table_cache_.creator_message_layout.valid);
        app.ensure_creator_message_layout(120.0f);
        require("shorter_draft_reduces_height", app.table_cache_.creator_message_layout.offsets[1] < wide_offsets[1]);
        require("cell_search_begins", app.begin_creator_message_cell_edit(root_id));
        app.creator_message_cell_edit_.buffer = "Search active input";
        snprintf(app.creator_message_find_.query, sizeof(app.creator_message_find_.query), "%s", "Search active");
        app.run_creator_message_find();
        require("search_collects_active_cell", app.creator_message_find_.matches.size() == 1 &&
            app.creator_message_cell_edit_.edit_id.empty());
        require("layout_drafts_reverted", app.revert_all_pending_edits() &&
            !app.table_cache_.creator_message_layout.valid && read_creator_message_source(map_path) == original_map);

        *out << "stage=wizard-create-and-save\n";
        const auto& templates = new_element_templates();
        require("other_category_template_last", !templates.empty() &&
            std::string_view(templates.back().id) == "creator.message" &&
            templates.back().category == NewElementTemplateCategory::Other);
        require("creator_wizard_opened", app.open_new_element_wizard_for_template("creator.message"));
        app.new_element_wizard_.target_file_path = child_path.u8string();
        app.rebuild_new_element_wizard_form();
        auto* content = find_inspector_field(app.new_element_wizard_.form, "content");
        require("creator_wizard_content_without_distance", content &&
            !find_inspector_field(app.new_element_wizard_.form, "distance"));
        const std::string created_text = "  Creator says \"hello\" at C:\\route  ";
        set_edit_field_buffer(*content, created_text);
        require("wizard_inserts_in_memory", app.apply_new_element_insert() &&
            app.model_.creator_messages.size() == 3 && !row_id(created_text).empty());
        require("wizard_apply_preserves_disk", read_creator_message_source(map_path) == original_map &&
            read_creator_message_source(child_path) == original_child && !app.creator_message_popup_.requested);
        require("wizard_save_commits", app.save_pending_edits(false) && !app.has_pending_edits());
        const auto committed_child = read_creator_message_source(child_path);
        require("save_preserves_header_gap_and_existing_body", read_creator_message_source(map_path) == original_map &&
            committed_child.rfind("BveTs Map 2.02:utf-8\r\n\r\n", 0) == 0 &&
            committed_child.find("//--kme--message-from-creator:\"" + created_text + "\"\r\n") != std::string::npos &&
            committed_child.find("# Preserve the included source.\r\nDrawDistance.Change(600);\r\n") != std::string::npos);
        app.reload_current_map_geometry();
        require("reload_refreshes_without_popup", wait_for_map() &&
            app.model_.creator_messages.size() == 3 && !app.creator_message_popup_.requested);

        *out << "stage=update-delete-and-empty-history\n";
        require("update_and_delete_drafts", app.set_creator_message_draft(row_id("Root message"), "Saved edit", false) &&
            app.set_creator_message_draft(row_id("Included message"), "Included message", true));
        require("update_delete_apply", app.apply_creator_message_drafts() &&
            app.model_.creator_messages.size() == 2 && !row_id("Saved edit").empty() && row_id("Included message").empty());
        require("update_delete_memory_only", read_creator_message_source(map_path) == original_map &&
            read_creator_message_source(child_path) == committed_child);
        require("update_delete_save", app.save_pending_edits(false));
        open(scenario_path.u8string(), 0);
        require("saved_update_delete_survive_open", app.model_.creator_messages.size() == 2 &&
            !row_id("Saved edit").empty() && !row_id(created_text).empty() && !app.creator_message_popup_.requested);
        const auto remaining = app.model_.creator_messages;
        for (const auto& row : remaining)
            require("delete_remaining_draft", app.set_creator_message_draft(row.edit_id, table_cell(row, "content"), true));
        require("delete_all_apply_and_save", app.apply_creator_message_drafts() && app.model_.creator_messages.empty() &&
            app.save_pending_edits(false));
        app.reload_current_map_geometry();
        require("empty_map_reload_has_no_popup", wait_for_map() && app.model_.creator_messages.empty() &&
            !app.creator_message_popup_.requested);
        const auto* empty_history = history(map_path.u8string());
        require("no_messages_retains_suppression_preference", empty_history && !empty_history->has_messages && empty_history->suppressed);

        require("suppressed_map_wizard_reopened", app.open_new_element_wizard_for_template("creator.message"));
        app.new_element_wizard_.target_file_path = map_path.u8string();
        app.rebuild_new_element_wizard_form();
        content = find_inspector_field(app.new_element_wizard_.form, "content");
        require("suppressed_map_content_field", content != nullptr);
        set_edit_field_buffer(*content, "Replacement message");
        require("suppressed_map_replacement_saved", app.apply_new_element_insert() && app.save_pending_edits(false));
        open(scenario_path.u8string(), 0);
        const auto* replacement_history = history(map_path.u8string());
        require("new_message_keeps_prior_suppression", app.model_.creator_messages.size() == 1 &&
            !row_id("Replacement message").empty() && !app.creator_message_popup_.requested &&
            replacement_history && replacement_history->has_messages && replacement_history->suppressed);

        open(empty_path.u8string(), 0);
        const auto* first_empty = history(empty_path.u8string());
        require("new_empty_map_history_state", first_empty && !first_empty->has_messages && !first_empty->suppressed &&
            !app.creator_message_popup_.requested);
        require("empty_map_wizard_opened", app.open_new_element_wizard_for_template("creator.message"));
        app.new_element_wizard_.target_file_path = empty_path.u8string();
        app.rebuild_new_element_wizard_form();
        content = find_inspector_field(app.new_element_wizard_.form, "content");
        require("empty_map_content_field", content != nullptr);
        set_edit_field_buffer(*content, "New message");
        const bool empty_applied = app.apply_new_element_insert();
        if (!empty_applied) report_edit_failure("empty-map-first-message-apply");
        require("empty_map_first_message_applied", empty_applied);
        const bool empty_saved = app.save_pending_edits(false);
        if (!empty_saved) report_edit_failure("empty-map-first-message-save");
        require("empty_map_first_message_saved", empty_saved);
        app.reload_current_map_geometry();
        require("reload_new_message_does_not_popup", wait_for_map() && !app.creator_message_popup_.requested);
        open(empty_path.u8string(), 0);
        require("open_new_message_requests_popup", app.creator_message_popup_.requested &&
            app.creator_message_popup_.contents == std::vector<std::string>{"New message"});
        app.creator_message_popup_.suppressed = false;
        app.confirm_creator_message_popup();
        const auto* shown = history(empty_path.u8string());
        require("unchecked_confirmation_keeps_showing", shown && shown->has_messages && !shown->suppressed);

        *out << "stage=draft-cancellation-and-post-save-edit\n";
        open(draft_state_path.u8string(), 0);
        app.confirm_creator_message_popup();
        require("roundtrip_draft_first_apply", app.set_creator_message_draft(row_id("A"), "B", false) &&
            app.apply_creator_message_drafts() && app.has_pending_edits());
        require("roundtrip_draft_return_to_baseline", app.set_creator_message_draft(row_id("B"), "A", false) &&
            app.apply_creator_message_drafts() && !app.has_pending_edits() &&
            !app.has_creator_message_drafts() && !row_id("A").empty() &&
            read_creator_message_source(draft_state_path) == draft_state_baseline);
        require("selected_draft_update_applied", app.set_creator_message_draft(row_id("A"), "B", false) &&
            app.apply_creator_message_drafts());
        const std::string selected_id = row_id("B");
        require("clean_selected_draft_exists_before_save", !selected_id.empty() &&
            app.set_creator_message_draft(selected_id, "B", false) &&
            app.creator_message_drafts_.count(selected_id) == 1 && !app.has_creator_message_drafts());
        const bool selected_saved = app.save_pending_edits(false);
        if (!selected_saved) report_edit_failure("clean-selected-draft-save");
        require("save_with_clean_selected_draft", selected_saved && !app.has_pending_edits());
        const auto selected_draft = app.creator_message_drafts_.find(selected_id);
        require("clean_selected_draft_survives_save", selected_draft != app.creator_message_drafts_.end());
        // Change the retained draft directly without another setter call;
        // Apply must resolve the current disk-baseline guard after Save.
        selected_draft->second.content = "C";
        require("same_row_edit_after_save_staged", app.has_creator_message_drafts());
        const bool subsequent_applied = app.apply_creator_message_drafts();
        if (!subsequent_applied) report_edit_failure("same-row-post-save-apply");
        require("same_row_edit_after_save_applied", subsequent_applied && !row_id("C").empty());
        const bool subsequent_saved = app.save_pending_edits(false);
        if (!subsequent_saved) report_edit_failure("same-row-post-save-save");
        require("same_row_edit_after_save_committed", subsequent_saved && !app.has_pending_edits() &&
            read_creator_message_source(draft_state_path).find(
                "//--kme--message-from-creator:\"C\"\r\n") != std::string::npos);
        open(draft_state_path.u8string(), 0);
        require("same_row_post_save_edit_survives_reopen", app.model_.creator_messages.size() == 1 &&
            !row_id("C").empty());
        app.confirm_creator_message_popup();

        require("post_save_cell_edit_begins", app.begin_creator_message_cell_edit(row_id("C")));
        app.creator_message_cell_edit_.buffer = "D";
        require("post_save_active_cell_saved", app.apply_creator_message_drafts() && app.save_pending_edits(false));
        open(draft_state_path.u8string(), 0);
        require("post_save_cell_survives_reopen", !row_id("D").empty());
        app.confirm_creator_message_popup();

        *out << "stage=unsaved-creation-order\n";
        open(ordered_path.u8string(), 0);
        std::vector<std::string> creation_order;
        const auto order_matches = [&]() {
            std::vector<std::string> model_contents;
            for (const auto& row : app.model_.creator_messages)
                model_contents.push_back(table_cell(row, "content"));
            if (model_contents != creation_order) return false;
            KvMapSnapshot snapshot{};
            if (!kv_get_map_snapshot(app.handle_, KV_MAP_SNAPSHOT_VERSION, &snapshot, sizeof(snapshot)) ||
                snapshot.creator_message_count != creation_order.size() ||
                (!creation_order.empty() && !snapshot.creator_messages)) return false;
            for (size_t i = 0; i < creation_order.size(); ++i) {
                const KvStringRef ref = snapshot.creator_messages[i].content;
                if (!snapshot.string_data || ref.offset > snapshot.string_size ||
                    ref.length > snapshot.string_size - ref.offset) return false;
                if (std::string(snapshot.string_data + static_cast<size_t>(ref.offset),
                                static_cast<size_t>(ref.length)) != creation_order[i]) return false;
            }
            return true;
        };
        const auto open_ordered_wizard = [&]() {
            require("ordered_wizard_opened", app.open_new_element_wizard_for_template("creator.message"));
            app.new_element_wizard_.target_file_path = ordered_path.u8string();
            app.rebuild_new_element_wizard_form();
        };
        open_ordered_wizard();
        for (int i = 1; i <= 12; ++i) {
            if (i == 7) {
                app.new_element_wizard_.open = false;
                open_ordered_wizard();
            }
            content = find_inspector_field(app.new_element_wizard_.form, "content");
            require("ordered_content_field", content != nullptr);
            const std::string text = "Creation " + std::to_string(i);
            set_edit_field_buffer(*content, text);
            if (i == 6) {
                app.show_creator_messages_window_ = false;
                app.new_element_wizard_.apply_then_open_created_element = true;
            }
            const bool applied = app.apply_new_element_insert();
            if (!applied) report_edit_failure("ordered-insert-apply");
            require("ordered_insert_" + std::to_string(i), applied);
            creation_order.push_back(text);
            require("creation_order_after_" + std::to_string(i), order_matches());
            if (i == 6) {
                require("apply_and_edit_opens_message_tab", app.show_creator_messages_window_ &&
                    !app.new_element_wizard_.open && !app.pending_inspector_request_ && !app.inspector_.open);
            }
        }
        require("twelve_unsaved_messages_leave_disk_unchanged", app.has_pending_edits() &&
            read_creator_message_source(ordered_path) == ordered_baseline);
        require("delete_unsaved_message_draft", app.set_creator_message_draft(row_id("Creation 5"), "Creation 5", true));
        require("delete_unsaved_message_apply", app.apply_creator_message_drafts());
        creation_order.erase(creation_order.begin() + 4);
        require("remaining_unsaved_messages_keep_order", order_matches());
        open_ordered_wizard();
        content = find_inspector_field(app.new_element_wizard_.form, "content");
        require("ordered_replacement_content_field", content != nullptr);
        set_edit_field_buffer(*content, "Creation 13");
        require("ordered_replacement_apply", app.apply_new_element_insert());
        creation_order.push_back("Creation 13");
        require("insert_after_pending_delete_keeps_order", order_matches() &&
            read_creator_message_source(ordered_path) == ordered_baseline);
        require("ordered_messages_save", app.save_pending_edits(false));
        const auto ordered_bytes = read_creator_message_source(ordered_path);
        size_t previous_end = 0;
        bool disk_order_matches = ordered_bytes.find("Creation 5\"") == std::string::npos;
        for (const auto& text : creation_order) {
            const std::string marker = "//--kme--message-from-creator:\"" + text + "\"\r\n";
            const size_t position = ordered_bytes.find(marker, previous_end);
            if (position == std::string::npos) {
                disk_order_matches = false;
                break;
            }
            previous_end = position + marker.size();
        }
        require("saved_physical_message_order", disk_order_matches);
        open(ordered_path.u8string(), 0);
        require("saved_message_order_survives_reopen", order_matches() &&
            app.creator_message_popup_.contents == creation_order);
        app.confirm_creator_message_popup();
    } catch (const std::exception& error) {
        ++failures;
        *out << "exception=" << error.what() << '\n';
    }
    try {
        bool unchanged = !protected_sources.empty();
        for (const auto& entry : protected_sources)
            unchanged = (read_creator_message_source(entry.first) == entry.second) && unchanged;
        check("real_sources_byte_identical", unchanged);
    } catch (const std::exception& error) {
        check("real_sources_byte_identical", false);
        *out << "source_verification_error=" << error.what() << '\n';
    }
    *out << "result=" << (failures == 0 ? "PASS" : "FAIL") << '\n';
    out->flush();
    return failures == 0 ? 0 : 3;
}
#endif
