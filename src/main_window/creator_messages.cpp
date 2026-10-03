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
#include "../table/datatable_internal.h"
#include "misc/cpp/imgui_stdlib.h"

#include <iterator>

using namespace datatable_internal;

bool App::has_creator_message_drafts() const {
    return std::any_of(creator_message_drafts_.begin(), creator_message_drafts_.end(),
        [](const auto& item) {
            return item.second.deleted || item.second.content != item.second.original_content;
        });
}

void App::clear_creator_message_drafts() {
    creator_message_drafts_.clear();
    creator_message_selected_id_.clear();
    reset_table_find_results(creator_message_find_);
}

bool App::set_creator_message_draft(const std::string& edit_id,
                                   std::string content, bool deleted) {
    if (!edit_actions_available()) return false;
    if (content.find_first_of("\r\n") != std::string::npos || content.find('\0') != std::string::npos) {
        set_program_status("status.creator_message.single_line");
        return false;
    }
    size_t row_index = 0;
    if (!find_row_index_by_edit_id(model_.creator_messages, edit_id, row_index)) return false;
    const TableRow& row = model_.creator_messages[row_index];
    std::string error;
    auto metadata = resolve_inspector_target_metadata(handle_, edit_id, "creator.message", &error);
    if (!metadata) {
        KME_ADD_LOG("[error]Creator message edit metadata unavailable: " + error);
        return false;
    }
    auto existing = creator_message_drafts_.find(edit_id);
    if (existing == creator_message_drafts_.end()) {
        CreatorMessageDraft draft;
        draft.edit_id = edit_id;
        draft.original_content = table_cell(row, "content");
        existing = creator_message_drafts_.emplace(edit_id, std::move(draft)).first;
    } else if (!existing->second.deleted && existing->second.content == existing->second.original_content) {
        existing->second.original_content = table_cell(row, "content");
    }
    existing->second.source_file = metadata->source.file_path;
    existing->second.expected_source_hash = expected_source_hash_for_edit_target(
        model_, pending_edit_changes_, edit_id, metadata->expected_source_hash, metadata->source.file_path);
    existing->second.content = std::move(content);
    existing->second.deleted = deleted;
    return true;
}

bool App::apply_creator_message_drafts() {
    if (!edit_actions_available() || !has_creator_message_drafts()) return false;
    std::map<std::string, MapElementPendingChange> candidate = pending_edit_changes_;
    for (const auto& item : creator_message_drafts_) {
        const CreatorMessageDraft& draft = item.second;
        if (!draft.deleted && draft.content == draft.original_content) continue;
        if (draft.content.find_first_of("\r\n") != std::string::npos ||
            draft.content.find('\0') != std::string::npos) {
            set_program_status("status.creator_message.single_line");
            return false;
        }
        const auto previous = candidate.find(draft.edit_id);
        if (draft.deleted && previous != candidate.end() && previous->second.operation == "insert") {
            candidate.erase(previous);
            continue;
        }
        std::string error;
        auto metadata = resolve_inspector_target_metadata(handle_, draft.edit_id, "creator.message", &error);
        if (!metadata) {
            KME_ADD_LOG("[error]Creator message Apply target unavailable: " + error);
            return false;
        }
        MapElementPendingChange change;
        if (previous != candidate.end()) change = previous->second;
        else {
            change.change_id = "creator-message-" + draft.edit_id;
            change.edit_id = draft.edit_id;
            change.row_kind = "creator.message";
        }
        // Save advances the handle's disk baseline without rebuilding clean
        // table drafts. Resolve its current guard when the draft is applied;
        // an existing ledger entry still pins its original disk baseline.
        change.target_file_path = metadata->source.file_path;
        change.expected_source_hash = expected_source_hash_for_edit_target(
            model_, pending_edit_changes_, draft.edit_id,
            metadata->expected_source_hash, metadata->source.file_path);
        if (draft.deleted) {
            change.operation = "delete";
            change.field_changes.clear();
        } else {
            const auto original = original_edit_rows_.find(draft.edit_id);
            if (change.operation == "update" && original != original_edit_rows_.end() &&
                draft.content == table_cell(original->second.row, "content")) {
                change.field_changes.erase("content");
                if (change.field_changes.empty() && change.replacement_statement.empty()) {
                    candidate.erase(draft.edit_id);
                    continue;
                }
            } else {
                change.field_changes["content"] = draft.content;
            }
        }
        candidate[draft.edit_id] = std::move(change);
    }
    if (!apply_edit_ledger_to_preview(candidate, std::nullopt, false)) {
        set_program_status("status.edit.pending");
        return false;
    }
    clear_creator_message_drafts();
    set_program_status("status.edit.applied_to_preview");
    return true;
}

void App::refresh_creator_message_history(bool request_popup) {
    if (!has_model_ || file_path_.empty()) return;
    const std::string path = normalized_storage_path(file_path_);
    const std::string key = normalized_path_key(path);
    auto entry = std::find_if(creator_message_history_.begin(), creator_message_history_.end(),
        [&](const CreatorMessageHistory& item) { return normalized_path_key(item.path) == key; });
    const bool has_messages = !model_.creator_messages.empty();
    bool changed = false;
    if (entry == creator_message_history_.end()) {
        creator_message_history_.push_back(CreatorMessageHistory{path, has_messages, false});
        entry = std::prev(creator_message_history_.end());
        changed = true;
    } else if (entry->has_messages != has_messages) {
        entry->has_messages = has_messages;
        changed = true;
    }
    if (changed) save_history();
    if (!request_popup || !has_messages || entry->suppressed) return;
    creator_message_popup_ = CreatorMessagePopupState{};
    creator_message_popup_.requested = true;
    creator_message_popup_.map_path = path;
    for (const TableRow& row : model_.creator_messages) {
        creator_message_popup_.contents.push_back(table_cell(row, "content"));
    }
}

void App::confirm_creator_message_popup() {
    const std::string key = normalized_path_key(creator_message_popup_.map_path);
    auto entry = std::find_if(creator_message_history_.begin(), creator_message_history_.end(),
        [&](const CreatorMessageHistory& item) { return normalized_path_key(item.path) == key; });
    if (entry != creator_message_history_.end() && entry->suppressed != creator_message_popup_.suppressed) {
        entry->suppressed = creator_message_popup_.suppressed;
        save_history();
    }
    creator_message_popup_ = CreatorMessagePopupState{};
}

void App::render_creator_message_popup() {
    const std::string title = tr("creator_message.popup_title") + "###CreatorMessagePopup";
    if (creator_message_popup_.requested &&
        !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel)) {
        ImGui::OpenPopup(title.c_str());
        creator_message_popup_.requested = false;
    }
    ImGui::SetNextWindowSize(ImVec2(640.0f, 340.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::BeginPopupModal(title.c_str(), nullptr, ImGuiWindowFlags_None)) return;
    CreatorMessagePopupState& popup = creator_message_popup_;
    if (popup.contents.empty()) {
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return;
    }
    popup.index = std::min(popup.index, popup.contents.size() - 1);
    const float footer = ImGui::GetFrameHeightWithSpacing() * 2.0f;
    ImGui::BeginChild("##CreatorMessageText", ImVec2(0, -footer), true);
    ImGui::PushTextWrapPos();
    ImGui::TextUnformatted(popup.contents[popup.index].c_str());
    ImGui::PopTextWrapPos();
    ImGui::EndChild();
    if (popup.contents.size() > 1) {
        ImGui::BeginDisabled(popup.index == 0);
        if (ImGui::Button("◀##CreatorMessagePrevious")) --popup.index;
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::Text("%zu / %zu", popup.index + 1, popup.contents.size());
        ImGui::SameLine();
        ImGui::BeginDisabled(popup.index + 1 == popup.contents.size());
        if (ImGui::Button("▶##CreatorMessageNext")) ++popup.index;
        ImGui::EndDisabled();
    }
    ImGui::Checkbox(tr("creator_message.do_not_show").c_str(), &popup.suppressed);
    ImGui::SameLine();
    if (ImGui::Button(tr("button.ok").c_str())) {
        confirm_creator_message_popup();
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

void App::render_creator_messages_window() {
    if (!show_creator_messages_window_) return;
    const std::string title = tr("frame.creator_messages") + "###CreatorMessages";
    if (!ImGui::Begin(title.c_str(), &show_creator_messages_window_)) {
        ImGui::End();
        return;
    }
    ensure_table_cache();
    const auto& rows = table_cache_.creator_message_rows;
    bool apply = false;
    const bool can_edit = edit_actions_available();
    if (can_edit) {
        ImGui::BeginDisabled(!has_creator_message_drafts());
        apply = ImGui::Button(tr("button.apply").c_str());
        ImGui::EndDisabled();
    }
    const auto run_find = [&]() {
        std::vector<CachedTableRow> search_rows = rows;
        for (CachedTableRow& row : search_rows) {
            if (!row.cells.empty()) row.cells[0] = row.source.file_path + ":" + std::to_string(row.source.line);
            const auto draft = creator_message_drafts_.find(row.edit_id);
            if (draft == creator_message_drafts_.end()) continue;
            if (draft->second.deleted) row.cells.clear();
            else if (row.cells.size() > 1) row.cells[1] = draft->second.content;
        }
        run_table_find(creator_message_find_, TableFindRowsView{&search_rows, nullptr, nullptr}, {0, 1});
    };
    ImGui::SetNextItemWidth(std::max(80.0f, ImGui::GetContentRegionAvail().x - 150.0f));
    if (ImGui::InputText("##CreatorMessageFind", creator_message_find_.query,
                        IM_ARRAYSIZE(creator_message_find_.query), ImGuiInputTextFlags_EnterReturnsTrue)) run_find();
    same_line_if_next_item_fits(button_width_for_label(tr("button.find")), 0.0f);
    if (ImGui::Button(tr("button.find").c_str())) run_find();
    same_line_if_next_item_fits(ImGui::GetFrameHeight(), 0.0f);
    ImGui::BeginDisabled(creator_message_find_.matches.empty());
    if (ImGui::Button("↑##CreatorMessageFindPrevious")) step_table_find(creator_message_find_, -1);
    same_line_if_next_item_fits(ImGui::GetFrameHeight(), 0.0f);
    if (ImGui::Button("↓##CreatorMessageFindNext")) step_table_find(creator_message_find_, 1);
    ImGui::EndDisabled();
    std::string selected;
    const float footer = can_edit ? ImGui::GetFrameHeightWithSpacing() * 4.0f : 0.0f;
    if (rows.empty()) ImGui::TextUnformatted(tr("creator_message.empty").c_str());
    else if (ImGui::BeginTable("##CreatorMessagesTable", 2,
        ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable |
        ImGuiTableFlags_ScrollX | ImGuiTableFlags_ScrollY,
        ImVec2(0, std::max(60.0f, ImGui::GetContentRegionAvail().y - footer)))) {
        ImGui::TableSetupColumn(tr("label.source_file").c_str(), ImGuiTableColumnFlags_WidthFixed, 220.0f);
        ImGui::TableSetupColumn(tr("creator_message.content").c_str(), ImGuiTableColumnFlags_WidthStretch);
        setup_fixed_table_header();
        ImGui::TableHeadersRow();
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(rows.size()));
        if (creator_message_find_.scroll_row >= 0 &&
            static_cast<size_t>(creator_message_find_.scroll_row) < rows.size()) {
            clipper.IncludeItemByIndex(creator_message_find_.scroll_row);
        }
        while (clipper.Step()) {
        for (int visible = clipper.DisplayStart; visible < clipper.DisplayEnd; ++visible) {
            const size_t index = static_cast<size_t>(visible);
            const CachedTableRow& row = rows[index];
            const auto draft = creator_message_drafts_.find(row.edit_id);
            const bool deleted = draft != creator_message_drafts_.end() && draft->second.deleted;
            const std::string& content = draft == creator_message_drafts_.end()
                ? row.cells[1] : draft->second.content;
            ImGui::PushID(static_cast<int>(index));
            ImGui::TableNextRow();
            if (creator_message_find_.scroll_row == static_cast<int>(index)) {
                ImGui::SetScrollHereY();
                creator_message_find_.scroll_row = -1;
            }
            if (index < creator_message_find_.row_matches.size() && creator_message_find_.row_matches[index]) {
                ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, IM_COL32(115, 100, 25, 100));
            }
            ImGui::TableSetColumnIndex(0);
            render_file_path_cell_with_context(row.cells[0], row.open_path, tr("menu.open_in_explorer"), row.source.file_path);
            ImGui::TableSetColumnIndex(1);
            const std::string& label = deleted ? tr("status.edit.pending_delete") : content;
            const ImVec2 content_position = ImGui::GetCursorPos();
            if (ImGui::Selectable("##content", creator_message_selected_id_ == row.edit_id)) selected = row.edit_id;
            ImGui::SetCursorPos(content_position);
            ImGui::TextUnformatted(label.c_str());
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", content.c_str());
            ImGui::PopID();
        }
        }
        ImGui::EndTable();
    }
    // Row selection and all draft mutations occur after EndTable().
    if (!selected.empty()) creator_message_selected_id_ = std::move(selected);
    if (can_edit && !rows.empty()) {
        if (creator_message_selected_id_.empty()) creator_message_selected_id_ = rows.front().edit_id;
        auto draft = creator_message_drafts_.find(creator_message_selected_id_);
        if (draft == creator_message_drafts_.end()) {
            size_t row_index = 0;
            if (find_row_index_by_edit_id(model_.creator_messages, creator_message_selected_id_, row_index)) {
                set_creator_message_draft(creator_message_selected_id_, table_cell(model_.creator_messages[row_index], "content"), false);
                draft = creator_message_drafts_.find(creator_message_selected_id_);
            }
        }
        if (draft != creator_message_drafts_.end()) {
            ImGui::TextUnformatted(tr("creator_message.content").c_str());
            ImGui::BeginDisabled(draft->second.deleted);
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::InputText("##CreatorMessageContent", &draft->second.content)) {
                reset_table_find_results(creator_message_find_);
            }
            ImGui::EndDisabled();
            const std::string delete_label = tr("button.delete") + "##CreatorMessageDelete";
            ImGui::BeginDisabled(draft->second.deleted);
            if (ImGui::Button(delete_label.c_str())) {
                draft->second.deleted = true;
                reset_table_find_results(creator_message_find_);
            }
            ImGui::EndDisabled();
        }
    }
    ImGui::End();
    if (apply) request_edit_ui_operation(PendingEditUiOperation::ApplyCreatorMessages);
}
