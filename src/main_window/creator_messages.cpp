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
#include "imgui_internal.h"
#include "misc/cpp/imgui_stdlib.h"

#include <iterator>

using namespace datatable_internal;

bool App::has_creator_message_drafts() const {
    if (!creator_message_cell_edit_.edit_id.empty() &&
        creator_message_cell_edit_.buffer != creator_message_cell_edit_.baseline) return true;
    return std::any_of(creator_message_drafts_.begin(), creator_message_drafts_.end(),
        [](const auto& item) {
            return item.second.deleted || item.second.content != item.second.original_content;
        });
}

void App::clear_creator_message_drafts() {
    creator_message_drafts_.clear();
    creator_message_selected_id_.clear();
    creator_message_cell_edit_ = CreatorMessageCellEdit{};
    table_cache_.creator_message_layout.valid = false;
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
    const bool changed = existing == creator_message_drafts_.end()
        ? deleted || content != table_cell(row, "content")
        : existing->second.deleted != deleted || existing->second.content != content;
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
    table_cache_.creator_message_layout.valid = false;
    if (changed) reset_table_find_results(creator_message_find_);
    return true;
}

bool App::commit_creator_message_cell_edit() {
    const auto& edit = creator_message_cell_edit_;
    if (!edit.edit_id.empty() && edit.buffer != edit.baseline &&
        !set_creator_message_draft(edit.edit_id, edit.buffer, false)) return false;
    creator_message_cell_edit_ = CreatorMessageCellEdit{};
    return true;
}

bool App::begin_creator_message_cell_edit(const std::string& edit_id) {
    if (!edit_actions_available() || !commit_creator_message_cell_edit()) return false;
    size_t index = 0;
    if (!find_row_index_by_edit_id(model_.creator_messages, edit_id, index)) return false;
    const auto draft = creator_message_drafts_.find(edit_id);
    if (draft != creator_message_drafts_.end() && draft->second.deleted) return false;
    const std::string content = draft == creator_message_drafts_.end()
        ? table_cell(model_.creator_messages[index], "content") : draft->second.content;
    if (!set_creator_message_draft(edit_id, content, false)) return false;
    creator_message_cell_edit_ = CreatorMessageCellEdit{edit_id, content, content, true};
    creator_message_selected_id_ = edit_id;
    return true;
}

bool App::stage_creator_message_delete(const std::string& edit_id) {
    if (!edit_actions_available() || !commit_creator_message_cell_edit()) return false;
    size_t index = 0;
    if (!find_row_index_by_edit_id(model_.creator_messages, edit_id, index)) return false;
    const auto draft = creator_message_drafts_.find(edit_id);
    if (draft != creator_message_drafts_.end() && draft->second.deleted) return false;
    return set_creator_message_draft(edit_id, draft == creator_message_drafts_.end()
        ? table_cell(model_.creator_messages[index], "content") : draft->second.content, true);
}

void App::run_creator_message_find() {
    if (!commit_creator_message_cell_edit()) return;
    std::vector<CachedTableRow> search_rows = table_cache_.creator_message_rows;
    for (CachedTableRow& row : search_rows) {
        if (!row.cells.empty()) row.cells[0] = row.source.file_path + ":" + std::to_string(row.source.line);
        const auto draft = creator_message_drafts_.find(row.edit_id);
        if (draft == creator_message_drafts_.end()) continue;
        if (draft->second.deleted) row.cells.clear();
        else if (row.cells.size() > 1) row.cells[1] = draft->second.content;
    }
    run_table_find(creator_message_find_, TableFindRowsView{&search_rows, nullptr, nullptr}, {0, 1});
}

void App::ensure_creator_message_layout(float width) {
    auto& layout = table_cache_.creator_message_layout;
    const auto& style = ImGui::GetStyle();
    if (layout.valid && layout.font == ImGui::GetFont() &&
        layout.font_size == ImGui::GetFontSize() && layout.width == width &&
        layout.padding.x == style.CellPadding.x && layout.padding.y == style.CellPadding.y &&
        layout.frame_height == ImGui::GetFrameHeight() && layout.language == lang_) return;
    layout.font = ImGui::GetFont();
    layout.font_size = ImGui::GetFontSize();
    layout.width = width;
    layout.padding = style.CellPadding;
    layout.frame_height = ImGui::GetFrameHeight();
    layout.language = lang_;
    layout.text.clear();
    layout.offsets.clear();
    const auto& rows = table_cache_.creator_message_rows;
    layout.text.reserve(rows.size());
    layout.offsets.reserve(rows.size() + 1);
    layout.offsets.push_back(0.0f);
    for (const auto& row : rows) {
        const auto draft = creator_message_drafts_.find(row.edit_id);
        const std::string& content = draft == creator_message_drafts_.end()
            ? row.cells[1] : draft->second.deleted ? tr("status.edit.pending_delete") : draft->second.content;
        layout.text.push_back(wrap_table_cell_text(content, width - style.CellPadding.x * 2.0f));
        const float text_height = ImGui::CalcTextSize(layout.text.back().c_str(), nullptr, false).y;
        const float cell_height = std::max(layout.frame_height, text_height + style.CellPadding.y * 2.0f);
        layout.offsets.push_back(layout.offsets.back() + cell_height + style.CellPadding.y * 2.0f);
    }
    layout.valid = true;
}

bool App::apply_creator_message_drafts() {
    if (!edit_actions_available() || !commit_creator_message_cell_edit() || !has_creator_message_drafts()) return false;
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
    bool find = false;
    const float arrow_width = ImGui::GetFrameHeight();
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    const float controls_width = button_width_for_label(tr("button.find")) +
        2.0f * (spacing + arrow_width);
    const float available_width = ImGui::GetContentRegionAvail().x;
    const bool single_line = available_width >= 80.0f + spacing + controls_width;
    ImGui::SetNextItemWidth(std::max(1.0f,
        single_line ? available_width - spacing - controls_width : available_width));
    if (ImGui::InputText("##CreatorMessageFind", creator_message_find_.query,
                        IM_ARRAYSIZE(creator_message_find_.query), ImGuiInputTextFlags_EnterReturnsTrue)) find = true;
    if (single_line) ImGui::SameLine();
    if (ImGui::Button(tr("button.find").c_str())) find = true;
    ImGui::SameLine();
    ImGui::BeginDisabled(creator_message_find_.matches.empty());
    if (ImGui::Button("↑##CreatorMessageFindPrevious", ImVec2(arrow_width, 0))) step_table_find(creator_message_find_, -1);
    ImGui::SameLine();
    if (ImGui::Button("↓##CreatorMessageFindNext", ImVec2(arrow_width, 0))) step_table_find(creator_message_find_, 1);
    ImGui::EndDisabled();
    std::string selected, begin_edit, delete_message;
    bool finish_edit = false;
    bool editor_rendered = false;
    if (rows.empty()) ImGui::TextUnformatted(tr("creator_message.empty").c_str());
    else if (ImGui::BeginTable("##CreatorMessagesTable", 2,
        ImGuiTableFlags_Borders | ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY,
        ImVec2(0, std::max(60.0f, ImGui::GetContentRegionAvail().y)))) {
        ImGui::TableSetupColumn(tr("label.source_file").c_str(), ImGuiTableColumnFlags_WidthFixed, 220.0f);
        ImGui::TableSetupColumn(tr("creator_message.content").c_str(), ImGuiTableColumnFlags_WidthStretch);
        setup_fixed_table_header();
        ImGui::TableHeadersRow();
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(1);
        const float width = std::max(1.0f, ImGui::GetContentRegionAvail().x);
        ensure_creator_message_layout(width);
        const auto& layout = table_cache_.creator_message_layout;
        const float padding_y = ImGui::GetStyle().CellPadding.y;
        const float body_top = ImGui::GetCursorScreenPos().y - padding_y;
        const ImRect clip = ImGui::GetCurrentWindow()->ClipRect;
        const auto range = visible_wrapped_table_rows(layout.offsets,
            clip.Min.y - body_top, clip.Max.y - body_top);
        if (creator_message_find_.scroll_row >= 0 &&
            static_cast<size_t>(creator_message_find_.scroll_row) < rows.size()) {
            ImGui::SetScrollY(layout.offsets[static_cast<size_t>(creator_message_find_.scroll_row)]);
            creator_message_find_.scroll_row = -1;
        }
        // Offscreen rows are represented by their exact cached total height.
        // This keeps the table's scroll extent without an equal-height clipper.
        if (range.first > 0) {
            ImGui::Dummy(ImVec2(0, layout.offsets[range.first] - 2.0f * padding_y));
        }
        for (size_t index = range.first; index < range.second; ++index) {
            const CachedTableRow& row = rows[index];
            const auto draft = creator_message_drafts_.find(row.edit_id);
            const bool deleted = draft != creator_message_drafts_.end() && draft->second.deleted;
            if (row.edit_id.empty()) ImGui::PushID(static_cast<int>(index));
            else ImGui::PushID(row.edit_id.c_str());
            if (index > 0) ImGui::TableNextRow();
            ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0,
                ImGui::GetColorU32(index % 2 == 0 ? ImGuiCol_TableRowBg : ImGuiCol_TableRowBgAlt));
            if (index < creator_message_find_.row_matches.size() && creator_message_find_.row_matches[index]) {
                ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, IM_COL32(115, 100, 25, 100));
            }
            if (ImGui::TableSetColumnIndex(0)) {
                render_file_path_cell_with_context(row.cells[0], row.open_path, tr("menu.open_in_explorer"), row.source.file_path);
            }
            ImGui::TableSetColumnIndex(1);
            const float height = layout.offsets[index + 1] - layout.offsets[index] - 2.0f * padding_y;
            if (can_edit && !deleted && creator_message_cell_edit_.edit_id == row.edit_id) {
                const ImVec2 position = ImGui::GetCursorPos();
                ImGui::Dummy(ImVec2(0, height));
                ImGui::SetCursorPos(position);
                editor_rendered = true;
                finish_edit = render_editable_cell_input(
                    creator_message_cell_edit_.buffer, creator_message_cell_edit_.fresh);
            } else {
                const auto interaction = render_editable_cell_button(layout.text[index],
                    !row.edit_id.empty() && creator_message_selected_id_ == row.edit_id,
                    ImGui::GetColorU32(deleted ? ImGuiCol_TextDisabled : ImGuiCol_Text), width, height);
                if (interaction.left_clicked || interaction.right_clicked) selected = row.edit_id;
                if (can_edit && !deleted && interaction.double_clicked) begin_edit = row.edit_id;
            }
            if (ImGui::BeginPopupContextItem("##CreatorMessageContext")) {
                ImGui::BeginDisabled(!can_edit || deleted);
                if (ImGui::MenuItem(tr("context.editable_list.delete_row").c_str())) delete_message = row.edit_id;
                ImGui::EndDisabled();
                ImGui::EndPopup();
            }
            ImGui::PopID();
        }
        if (range.second < rows.size()) {
            ImGui::TableNextRow(ImGuiTableRowFlags_None,
                layout.offsets.back() - layout.offsets[range.second]);
        }
        ImGui::EndTable();
    }
    // Row selection and all draft mutations occur after EndTable().
    if (!selected.empty()) creator_message_selected_id_ = std::move(selected);
    if (finish_edit || !editor_rendered) commit_creator_message_cell_edit();
    if (!delete_message.empty()) stage_creator_message_delete(delete_message);
    if (!begin_edit.empty()) begin_creator_message_cell_edit(begin_edit);
    if (find) run_creator_message_find();
    ImGui::End();
    if (apply) request_edit_ui_operation(PendingEditUiOperation::ApplyCreatorMessages);
}
