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
#include "datatable_internal.h"
#include "touch_input.h"

#include "canvas3D.h"
#include "text_decoder.h"
#include "imgui.h"
#include "misc/cpp/imgui_stdlib.h"
#include "repeater_linkage.h"

#include <windows.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <initializer_list>
#include <map>
#include <string>
#include <system_error>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

using namespace datatable_internal;

namespace {
template <typename RunFindFn, typename RunUnusedFn, typename StepFn, typename StatusTextFn>
void render_table_find_panel(TableFindState& state,
                             const char* id,
                             const std::string& find_label,
                             const std::string& partial_label,
                             const std::string& exact_label,
                             const std::string& unused_label,
                             RunFindFn run_find,
                             RunUnusedFn run_unused,
                             StepFn step,
                             StatusTextFn status_text) {
    std::string toggle_id = std::string("##") + id + "_find_panel_toggle";
    std::string input_id = std::string("##") + id + "_find_text";
    std::string prev_id = std::string("↑##") + id + "_find_prev";
    std::string next_id = std::string("↓##") + id + "_find_next";
    std::string partial_id = partial_label + "##" + id + "_partial";
    std::string exact_id = exact_label + "##" + id + "_exact";

    ImGui::BeginGroup();
    if (render_find_panel_toggle(toggle_id.c_str(), find_label, state.panel_expanded)) {
        state.panel_expanded = !state.panel_expanded;
    }
    if (state.panel_expanded) {
        const float indent = ImGui::GetStyle().FramePadding.x;
        const float right_padding = ImGui::GetStyle().FramePadding.x;
        const float arrow_button_width = ImGui::GetFrameHeight();
        const float partial_width = radio_button_width_for_label(partial_label);
        const float exact_width = radio_button_width_for_label(exact_label);
        const float unused_width = button_width_for_label(unused_label);
        const float step_controls_width = width_after_previous_item({
            arrow_button_width,
            arrow_button_width,
        });
        const float all_controls_width = width_after_previous_item({
            arrow_button_width,
            arrow_button_width,
            partial_width,
            exact_width,
            unused_width,
        });
        ImGui::Spacing();
        ImGui::Indent(indent);
        if (ImGui::Button(find_label.c_str())) run_find();
        float input_available_width = ImGui::GetContentRegionAvail().x - right_padding;
        const float available_after_find = available_width_after_current_item(right_padding);
        if (available_after_find >= k_find_input_min_width) {
            input_available_width = available_after_find;
            ImGui::SameLine();
        }
        const float input_width = find_input_width(input_available_width,
                                                  all_controls_width,
                                                  step_controls_width);
        ImGui::SetNextItemWidth(input_width);
        if (ImGui::InputText(input_id.c_str(), state.query, IM_ARRAYSIZE(state.query),
                             ImGuiInputTextFlags_EnterReturnsTrue)) {
            run_find();
        }
        same_line_if_next_item_fits(arrow_button_width, right_padding);
        ImGui::BeginDisabled(state.matches.empty());
        if (ImGui::Button(prev_id.c_str(), ImVec2(arrow_button_width, 0.0f))) {
            step(-1);
        }
        same_line_if_next_item_fits(arrow_button_width, right_padding);
        if (ImGui::Button(next_id.c_str(), ImVec2(arrow_button_width, 0.0f))) {
            step(1);
        }
        ImGui::EndDisabled();
        same_line_if_next_item_fits(partial_width, right_padding);
        if (ImGui::RadioButton(partial_id.c_str(), !state.exact)) {
            state.exact = false;
            if (state.has_run || !blank_ascii(state.query)) run_find();
        }
        same_line_if_next_item_fits(exact_width, right_padding);
        if (ImGui::RadioButton(exact_id.c_str(), state.exact)) {
            state.exact = true;
            if (state.has_run || !blank_ascii(state.query)) run_find();
        }
        same_line_if_next_item_fits(unused_width, right_padding);
        if (ImGui::Button(unused_label.c_str())) run_unused();
        render_status_line(status_text());
        ImGui::Unindent(indent);
    }
    ImGui::EndGroup();
    ImVec2 find_panel_max = ImGui::GetItemRectMax();
    if (state.panel_expanded) find_panel_max.x += ImGui::GetStyle().FramePadding.x;
    render_find_panel_border(ImGui::GetItemRectMin(), find_panel_max);
    ImGui::Spacing();
}

template <typename ContextMenu>
bool render_editable_cell_input(std::string& buffer, bool& fresh,
                                ContextMenu&& render_context_menu) {
    if (fresh) {
        ImGui::SetKeyboardFocusHere(0);
        fresh = false;
    }
    ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x);
    const bool returned = ImGui::InputText(
        "##cell_edit", &buffer,
        ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
    ImGui::PopItemWidth();
    render_context_menu();
    return returned || ImGui::IsItemDeactivated();
}
} // namespace

void App::render_editable_list_table(
    const char* table_id,
    const TableColumnDef* columns,
    int column_count,
    const std::vector<CachedTableRow>& cached_rows,
    EditableListEditState& edit,
    const EditableListSpec& spec,
    float path_column_width,
    float last_column_width,
    TableFindState* find_state,
    const std::vector<std::string>* cached_column_headers,
    const std::vector<float>* cached_column_widths,
    const std::vector<EditableListDisplayRow>* cached_display_rows) {
    const bool is_station = std::string_view(spec.row_kind) == "station.list";
    const bool is_structure = std::string_view(spec.row_kind) == "structure.model";
    const bool is_signal_aspect =
        std::string_view(spec.row_kind) == "signal.aspect";
    const MapElementKeySource resource_key_source = is_structure
        ? MapElementKeySource::Structure
        : std::string_view(spec.row_kind) == "sound.list"
        ? MapElementKeySource::Sound
        : std::string_view(spec.row_kind) == "sound3D.list"
        ? MapElementKeySource::Sound3D
        : MapElementKeySource::None;
    const std::vector<EditableListDisplayRow>* display_rows =
        is_signal_aspect
        ? (edit.rows_initialized
            ? &edit.display_rows
            : cached_display_rows)
        : nullptr;
    const int logical_row_count = edit.rows_initialized
        ? static_cast<int>(edit.visible_rows.size())
        : static_cast<int>(cached_rows.size());
    const int row_count = display_rows
        ? static_cast<int>(display_rows->size())
        : logical_row_count;
    if (find_state &&
        find_state->scroll_row >= logical_row_count) {
        find_state->scroll_row = -1;
    }
    int scroll_target_row =
        find_state ? find_state->scroll_row : -1;
    if (display_rows && scroll_target_row >= 0) {
        const auto target = std::find_if(
            display_rows->begin(), display_rows->end(),
            [&](const EditableListDisplayRow& row) {
                return !row.secondary &&
                    row.logical_row ==
                        static_cast<size_t>(
                            scroll_target_row);
            });
        scroll_target_row =
            target == display_rows->end()
            ? -1
            : static_cast<int>(
                std::distance(
                    display_rows->begin(), target));
    }
    const bool has_cached_columns =
        cached_column_headers && cached_column_widths &&
        cached_column_headers->size() >=
            static_cast<size_t>(column_count) &&
        cached_column_widths->size() >=
            static_cast<size_t>(column_count);
    if (!columns && !has_cached_columns) return;
    const int path_column = spec.path_field < 0
        ? -1
        : static_cast<int>(spec.cache_column_offset) + spec.path_field;
    if (logical_row_count == 0 && edit_actions_available() &&
        ImGui::Button(tr("button.add_row").c_str())) {
        insert_editable_list_row(edit, spec, -1, false);
    }
    const ImVec2 table_size = is_station
        ? ImVec2(0.0f, scroll_x_table_height_for_rows(row_count))
        : ImVec2(0.0f, 0.0f);
    if (!ImGui::BeginTable(
            table_id, column_count,
            ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollX |
                (is_station ? ImGuiTableFlags_None : ImGuiTableFlags_ScrollY),
            table_size)) {
        return;
    }

    const std::string file_name_header = tr("column.file_name");
    for (int column = 0; column < column_count; ++column) {
        const bool use_cached_columns = has_cached_columns;
        float width = use_cached_columns
            ? (*cached_column_widths)[static_cast<size_t>(column)]
            : columns[column].width;
        if (column == path_column && path_column_width > 0.0f) {
            width = path_column_width;
        } else if (column == column_count - 1 && last_column_width > 0.0f) {
            width = last_column_width;
        }
        const char* header = column == path_column
            ? file_name_header.c_str()
            : (use_cached_columns
                ? (*cached_column_headers)[
                    static_cast<size_t>(column)].c_str()
                : columns[column].header);
        ImGui::TableSetupColumn(
            header, width > 0.0f ? ImGuiTableColumnFlags_WidthFixed : 0, width);
    }
    setup_fixed_table_header();
    ImGui::TableHeadersRow();

    const bool can_edit = edit_actions_available();
    const ImVec4 preview_text_color(1.0f, 1.0f, 0.0f, 1.0f);
    const auto display_at =
        [&](int row) -> const EditableListDisplayRow* {
        return display_rows
            ? &(*display_rows)[static_cast<size_t>(row)]
            : nullptr;
    };
    const auto logical_row_at = [&](int row) {
        const EditableListDisplayRow* display =
            display_at(row);
        return display
            ? static_cast<int>(display->logical_row)
            : row;
    };
    const auto draft_at = [&](int logical_row) -> EditableListDraftRow* {
        return edit.rows_initialized
            ? &edit.rows[
                edit.visible_rows[
                    static_cast<size_t>(logical_row)]]
            : nullptr;
    };
    const auto cached_at = [&](int logical_row) -> const CachedTableRow* {
        return edit.rows_initialized
            ? nullptr
            : &cached_rows[static_cast<size_t>(logical_row)];
    };
    const auto source_file_at = [&](int logical_row) -> const std::string& {
        EditableListDraftRow* draft = draft_at(logical_row);
        return draft
            ? draft->target_source_file
            : cached_at(logical_row)->source.file_path;
    };
    const auto edit_id_at = [&](int logical_row) -> const std::string& {
        EditableListDraftRow* draft = draft_at(logical_row);
        return draft
            ? editable_list_row_identity(*draft)
            : cached_at(logical_row)->edit_id;
    };
    const auto row_deleted_at = [&](int logical_row) {
        EditableListDraftRow* draft = draft_at(logical_row);
        return draft && draft->deleted;
    };

    ImGuiListClipper clipper;
    clipper.Begin(row_count);
    if (scroll_target_row >= 0 && scroll_target_row < row_count) {
        clipper.IncludeItemByIndex(scroll_target_row);
    }
    while (clipper.Step()) {
        for (int row_index = clipper.DisplayStart;
             row_index < clipper.DisplayEnd; ++row_index) {
            const int logical_row =
                logical_row_at(row_index);
            const EditableListDisplayRow* display_row =
                display_at(row_index);
            const bool secondary_row =
                display_row && display_row->secondary;
            EditableListDraftRow* draft =
                draft_at(logical_row);
            const CachedTableRow* cached =
                cached_at(logical_row);
            const std::string& target_edit_id =
                draft ? editable_list_row_identity(*draft) : cached->edit_id;
            const std::string& resolved_path =
                draft ? draft->resolved_path : cached->open_path;
            const bool whole_draft_deleted =
                draft && draft->deleted;
            const bool draft_deleted =
                whole_draft_deleted ||
                (draft && secondary_row &&
                 draft->secondary_row_deleted);
            const bool row_editable =
                can_edit && !draft_deleted && !target_edit_id.empty();
            const bool is_preview_model =
                is_structure && model_preview_canvas_ &&
                model_preview_canvas_->has_model() &&
                resolved_path == model_preview_canvas_->model_path();
            const ImU32 text_color = ImGui::GetColorU32(
                is_preview_model ? preview_text_color : ImGui::GetStyleColorVec4(ImGuiCol_Text));
            const bool is_find_match =
                find_state &&
                static_cast<size_t>(logical_row) <
                    find_state->row_matches.size() &&
                find_state->row_matches[
                    static_cast<size_t>(logical_row)] != 0;
            const bool is_unused =
                find_state &&
                static_cast<size_t>(logical_row) <
                    find_state->unused_row_matches.size() &&
                find_state->unused_row_matches[
                    static_cast<size_t>(logical_row)] != 0;

            ImGui::TableNextRow();
            if (draft_deleted || row_is_pending_delete(target_edit_id)) {
                ImGui::TableSetBgColor(
                    ImGuiTableBgTarget_RowBg0, k_pending_delete_row_color);
            } else if (is_unused) {
                ImGui::TableSetBgColor(
                    ImGuiTableBgTarget_RowBg0, k_unused_structure_model_row_color);
            } else if (is_find_match) {
                ImGui::TableSetBgColor(
                    ImGuiTableBgTarget_RowBg0, k_find_match_row_color);
            } else if (row_has_pending_edit(target_edit_id) ||
                       (draft && editable_list_row_has_draft(*draft))) {
                ImGui::TableSetBgColor(
                    ImGuiTableBgTarget_RowBg0, k_pending_edit_row_color);
            }
            if (row_index == scroll_target_row && find_state) {
                ImGui::SetScrollHereY(0.0f);
                find_state->scroll_row = -1;
            }

            ImGui::PushID(row_index);
            for (int column = 0; column < column_count; ++column) {
                ImGui::TableSetColumnIndex(column);
                ImGui::PushID(column);
                const int display_field_index =
                    column - static_cast<int>(spec.cache_column_offset);
                const size_t editable_field_count = draft
                    ? draft->values.size()
                    : (cached->editable_field_count != 0
                        ? cached->editable_field_count
                        : spec.field_count);
                int field_index = display_field_index;
                if (display_row && display_field_index >= 0) {
                    if (display_field_index == 0) {
                        field_index =
                            secondary_row ? -1 : 0;
                    } else if (
                        static_cast<size_t>(
                            display_field_index - 1) <
                        display_row->structure_field_count) {
                        field_index = static_cast<int>(
                            display_row->structure_field_offset +
                            static_cast<size_t>(
                                display_field_index - 1));
                    } else {
                        field_index = -1;
                    }
                }
                const bool editable_cell =
                    field_index >= 0 &&
                    static_cast<size_t>(field_index) <
                        editable_field_count;
                const std::string sequence = display_row
                    ? display_row->sequence
                    : std::to_string(
                        static_cast<size_t>(
                            logical_row) + 1);
                static const std::string empty;
                const size_t cached_column = editable_cell
                    ? spec.cache_column_offset +
                        static_cast<size_t>(field_index)
                    : static_cast<size_t>(column);
                const std::string& display = draft
                    ? (editable_cell
                        ? draft->values[static_cast<size_t>(field_index)]
                        : (display_field_index < 0
                            ? sequence : empty))
                    : (display_field_index < 0
                        ? sequence
                        : (editable_cell &&
                           cached_column <
                            cached->cells.size()
                            ? cached->cells[cached_column]
                            : empty));
                const bool row_key_is_editing = draft &&
                    edit.editing_edit_id == target_edit_id &&
                    edit.editing_column == 0;
                const std::string& resource_key =
                    resource_key_source == MapElementKeySource::None
                    ? empty
                    : draft
                    ? (row_key_is_editing
                        ? edit.edit_buffer
                        : (draft->values.empty() ? empty : draft->values.front()))
                    : (cached->cells.size() > spec.cache_column_offset
                        ? cached->cells[spec.cache_column_offset]
                        : empty);
                const bool is_editing =
                    editable_cell && edit.editing_edit_id == target_edit_id &&
                    edit.editing_column == field_index;
                const bool is_selected =
                    edit.selected_row == logical_row &&
                    edit.selected_secondary_row ==
                        secondary_row &&
                    edit.selected_column == column;

                const auto render_context_menu = [&]() {
                    // Mutating draft vectors while the table/popup is rendering
                    // invalidates pointers this render loop still uses. Record
                    // the request and run it after ImGui::EndTable().
                    const auto defer_action =
                        [&](DeferredEditableListAction::Kind kind,
                            int action_row, int action_column,
                            bool select_secondary = false) {
                            pending_editable_list_actions_.push_back(
                                {&edit, &spec, kind, action_row, action_column,
                                 select_secondary});
                        };
                    if (column == path_column && ImGui::IsItemHovered() &&
                        !resolved_path.empty()) {
                        ImGui::SetTooltip("%s", resolved_path.c_str());
                    }
                    touch_input::open_popup_on_last_item_long_press(
                        "##editable_list_context");
                    if (!ImGui::BeginPopupContextItem(
                            "##editable_list_context",
                            ImGuiPopupFlags_MouseButtonRight)) {
                        return;
                    }
                    bool has_top_action = false;
                    if (is_structure) {
                        ImGui::BeginDisabled(blank_ascii(resolved_path));
                        if (ImGui::MenuItem(tr("menu.preview_model").c_str())) {
                            preview_structure_model(resolved_path);
                        }
                        ImGui::EndDisabled();
                        has_top_action = true;
                    }
                    if (resource_key_source != MapElementKeySource::None) {
                        const bool can_use_resource_key = !draft_deleted &&
                            !row_is_pending_delete(target_edit_id) &&
                            !blank_ascii(resource_key) &&
                            can_use_resource_key_in_new_element_wizard(
                                resource_key_source);
                        ImGui::BeginDisabled(!can_use_resource_key);
                        if (ImGui::MenuItem(tr(is_structure
                                ? "menu.use_this_model"
                                : "menu.use_this_sound").c_str())) {
                            use_resource_key_in_new_element_wizard(
                                resource_key_source, resource_key);
                        }
                        ImGui::EndDisabled();
                        has_top_action = true;
                    }
                    if (is_signal_aspect &&
                        display_field_index > 0) {
                        ImGui::BeginDisabled(blank_ascii(display));
                        if (ImGui::MenuItem(
                                tr("menu.find_in_structure_models").c_str())) {
                            find_structure_model_for_structure_key(display);
                        }
                        ImGui::EndDisabled();
                        has_top_action = true;
                    }
                    if (column == path_column) {
                        ImGui::BeginDisabled(!row_editable);
                        if (ImGui::MenuItem(tr("menu.select_file").c_str())) {
                            defer_action(
                                DeferredEditableListAction::Kind::ChooseFile,
                                logical_row, -1);
                        }
                        ImGui::EndDisabled();
                        ImGui::BeginDisabled(blank_ascii(resolved_path));
                        if (ImGui::MenuItem(tr("menu.open_in_explorer").c_str())) {
                            open_parent_directory_in_explorer(resolved_path);
                        }
                        ImGui::EndDisabled();
                        has_top_action = true;
                    }
                    if (has_top_action) ImGui::Separator();

                    ImGui::BeginDisabled(!row_editable);
                    if (ImGui::MenuItem(
                            tr("context.editable_list.insert_above").c_str())) {
                        defer_action(
                            DeferredEditableListAction::Kind::InsertAbove,
                            logical_row, -1);
                        ImGui::EndDisabled();
                        ImGui::EndPopup();
                        return;
                    }
                    if (ImGui::MenuItem(
                            tr("context.editable_list.insert_below").c_str())) {
                        defer_action(
                            DeferredEditableListAction::Kind::InsertBelow,
                            logical_row, -1);
                        ImGui::EndDisabled();
                        ImGui::EndPopup();
                        return;
                    }
                    ImGui::EndDisabled();

                    const bool move_up_available =
                        row_editable && logical_row > 0 &&
                        !edit_id_at(logical_row - 1).empty() &&
                        !row_deleted_at(logical_row - 1) &&
                        source_file_at(logical_row - 1) ==
                            source_file_at(logical_row);
                    const bool move_down_available =
                        row_editable &&
                        logical_row + 1 < logical_row_count &&
                        !edit_id_at(logical_row + 1).empty() &&
                        !row_deleted_at(logical_row + 1) &&
                        source_file_at(logical_row + 1) ==
                            source_file_at(logical_row);
                    ImGui::BeginDisabled(!move_up_available);
                    if (ImGui::MenuItem(
                            tr("context.editable_list.move_up").c_str())) {
                        defer_action(
                            DeferredEditableListAction::Kind::MoveUp,
                            logical_row, -1);
                        ImGui::EndDisabled();
                        ImGui::EndPopup();
                        return;
                    }
                    ImGui::EndDisabled();
                    ImGui::BeginDisabled(!move_down_available);
                    if (ImGui::MenuItem(
                            tr("context.editable_list.move_down").c_str())) {
                        defer_action(
                            DeferredEditableListAction::Kind::MoveDown,
                            logical_row, -1);
                        ImGui::EndDisabled();
                        ImGui::EndPopup();
                        return;
                    }
                    ImGui::EndDisabled();
                    ImGui::BeginDisabled(!row_editable || !editable_cell);
                    if (ImGui::MenuItem(
                            tr("context.editable_list.clear_cell").c_str())) {
                        defer_action(
                            DeferredEditableListAction::Kind::ClearCell,
                            logical_row, field_index, secondary_row);
                        ImGui::EndDisabled();
                        ImGui::EndPopup();
                        return;
                    }
                    ImGui::EndDisabled();
                    ImGui::BeginDisabled(!row_editable);
                    if (ImGui::MenuItem(
                            tr("context.editable_list.delete_row").c_str())) {
                        defer_action(
                            DeferredEditableListAction::Kind::DeleteRow,
                            logical_row, -1);
                        ImGui::EndDisabled();
                        ImGui::EndPopup();
                        return;
                    }
                    ImGui::EndDisabled();
                    const size_t secondary_structure_field_count =
                        draft
                        ? draft->secondary_structure_field_count
                        : cached->secondary_structure_field_count;
                    const size_t primary_structure_field_count =
                        draft
                        ? draft->primary_structure_field_count
                        : cached->primary_structure_field_count;
                    if (is_signal_aspect) {
                        const bool add_glare_available =
                            row_editable &&
                            secondary_structure_field_count == 0 &&
                            (!draft || !draft->secondary_row_deleted) &&
                            primary_structure_field_count != 0;
                        ImGui::BeginDisabled(!add_glare_available);
                        if (ImGui::MenuItem(
                                tr("context.signal_aspect.add_glare")
                                    .c_str())) {
                            defer_action(
                                DeferredEditableListAction::Kind::AddGlare,
                                logical_row, -1);
                            ImGui::EndDisabled();
                            ImGui::EndPopup();
                            return;
                        }
                        ImGui::EndDisabled();
                    }
                    if (is_signal_aspect &&
                        secondary_structure_field_count != 0) {
                        const bool delete_glare_available =
                            row_editable &&
                            (!draft ||
                             !draft->secondary_row_deleted);
                        ImGui::BeginDisabled(
                            !delete_glare_available);
                        if (ImGui::MenuItem(
                                tr("context.signal_aspect.delete_glare")
                                    .c_str())) {
                            defer_action(
                                DeferredEditableListAction::Kind::DeleteGlare,
                                logical_row, -1);
                            ImGui::EndDisabled();
                            ImGui::EndPopup();
                            return;
                        }
                        ImGui::EndDisabled();
                    }
                    ImGui::EndPopup();
                };

                if (row_editable && is_editing) {
                    if (render_editable_cell_input(
                            edit.edit_buffer, edit.edit_buffer_fresh,
                            render_context_menu)) {
                        commit_editable_list_active_edit(edit, spec);
                    }
                    ImGui::PopID();
                    continue;
                }

                const float cell_width =
                    std::max(1.0f, ImGui::GetContentRegionAvail().x);
                const float cell_height = ImGui::GetFrameHeight();
                const EditableCellInteraction interaction = render_editable_cell_button(
                    display, is_selected, text_color, cell_width, cell_height);
                if (interaction.left_clicked || interaction.right_clicked) {
                    edit.selected_row = logical_row;
                    edit.selected_secondary_row =
                        secondary_row;
                    edit.selected_column = column;
                }
                render_context_menu();
                if (row_editable && editable_cell && interaction.double_clicked) {
                    commit_editable_list_active_edit(edit, spec);
                    if (initialize_editable_list_draft_rows(edit, spec)) {
                        EditableListDraftRow& active =
                            edit.rows[edit.visible_rows[
                                static_cast<size_t>(logical_row)]];
                        edit.editing_column = field_index;
                        edit.editing_edit_id = editable_list_row_identity(active);
                        edit.editing_baseline =
                            active.values[static_cast<size_t>(field_index)];
                        edit.edit_buffer = edit.editing_baseline;
                        edit.edit_buffer_fresh = true;
                    }
                }
                ImGui::PopID();
            }
            ImGui::PopID();
        }
    }
    ImGui::EndTable();
    run_pending_editable_list_actions();
}

void App::render_station_list_window() {
    if (!show_station_list_window_) return;
    if (dock_right_id_) ImGui::SetNextWindowDockID(dock_right_id_, ImGuiCond_FirstUseEver);
    std::string title = tr("frame.station_list") + "###StationList";
    if (!ImGui::Begin(title.c_str(), &show_station_list_window_)) {
        ImGui::End();
        return;
    }
    const ResourceListKind resource_kind = ResourceListKind::Station;
    const bool resource_list_present = has_model_ &&
        model_.resource_list_sources[static_cast<size_t>(resource_kind)].present;
    if (!resource_list_present) {
        const std::string message = resource_list_unavailable_message(
            has_model_, tr("status.resource_list_no_map"),
            tr("status.resource_list_not_specified"),
            tr(resource_list_name_translation_key(resource_kind)));
        if (render_resource_list_empty_overlay(
                message, tr("button.new_or_import_file"))) {
            open_new_file_wizard(NewFileKind::Station);
        }
        ImGui::End();
        return;
    }
    if (render_resource_list_source(
            model_.resource_list_sources[static_cast<size_t>(ResourceListKind::Station)],
            tr("label.source_path"), tr("menu.open_in_explorer"),
            tr("menu.change_file"), edit_actions_available())) {
        request_resource_list_file_change(ResourceListKind::Station);
    }
    ensure_table_cache();
    auto render_station_table = [&](const char* table_id,
                                    const TableColumnDef* columns,
                                    int column_count,
                                    const std::vector<CachedTableRow>& rows,
                                    bool allow_station_put_properties) {
        const int row_count = static_cast<int>(rows.size());
        ImVec2 table_size(0.0f, scroll_x_table_height_for_rows(row_count));
        if (!ImGui::BeginTable(table_id, column_count,
                               ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                               ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollX,
                               table_size)) {
            return;
        }
        for (int i = 0; i < column_count; ++i) {
            ImGui::TableSetupColumn(columns[i].header, ImGuiTableColumnFlags_WidthFixed,
                                    columns[i].width);
        }
        setup_fixed_table_header();
        ImGui::TableHeadersRow();
        ImGuiListClipper clipper;
        clipper.Begin(row_count);
        while (clipper.Step()) {
            for (int row_index = clipper.DisplayStart; row_index < clipper.DisplayEnd; ++row_index) {
                const CachedTableRow& row = rows[static_cast<size_t>(row_index)];
                ImGui::TableNextRow();
                if (allow_station_put_properties && row_is_pending_delete(row.edit_id)) {
                    ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, k_pending_delete_row_color);
                } else if (allow_station_put_properties && row_has_pending_edit(row.edit_id)) {
                    ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, k_pending_edit_row_color);
                }
                ImGui::PushID(row_index);
                for (int i = 0; i < column_count; ++i) {
                    ImGui::TableSetColumnIndex(i);
                    const std::string& value = row.cells[static_cast<size_t>(i)];
                    if (value.empty()) continue;
                    if (!allow_station_put_properties) {
                        ImGui::TextUnformatted(value.c_str());
                        continue;
                    }
                    ImGui::PushID(i);
                    const bool edit_enabled = edit_actions_available() && !row.edit_id.empty();
                    const TextCellContextAction action = render_text_cell_with_context_actions(
                        value,
                        tr("dialog.element_properties"), edit_enabled,
                        {}, false,
                        tr("button.delete"), edit_enabled);
                    if (action == TextCellContextAction::Primary) {
                        request_element_inspector(row.edit_id, "station.put");
                    } else if (action == TextCellContextAction::Tertiary) {
                        request_element_delete(row.edit_id, "station.put");
                    }
                    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                        request_element_inspector(row.edit_id, "station.put");
                    }
                    ImGui::PopID();
                }
                ImGui::PopID();
            }
        }
        ImGui::EndTable();
    };

    ImGui::TextUnformatted(tr("frame.station_positions").c_str());
    render_station_table("station_positions", k_station_position_columns,
                         IM_ARRAYSIZE(k_station_position_columns),
                         table_cache_.station_position_rows, true);
    ImGui::Separator();
    ImGui::TextUnformatted(tr("frame.station_definitions").c_str());
    ImGui::SameLine();
    ImGui::BeginDisabled(!edit_actions_available() ||
        !has_editable_list_drafts(station_definition_edit_, k_station_definition_edit_spec));
    if (ImGui::Button(tr("button.apply").c_str())) {
        apply_editable_list_drafts(station_definition_edit_, k_station_definition_edit_spec);
    }
    ImGui::EndDisabled();
    {
        const TableColumnDef* columns = k_station_definition_columns;
        const int column_count = IM_ARRAYSIZE(k_station_definition_columns);
        const std::vector<CachedTableRow>& rows = table_cache_.station_definition_rows;
        EditableListEditState& edit = station_definition_edit_;
        const int row_count = edit.rows_initialized
            ? static_cast<int>(edit.visible_rows.size())
            : static_cast<int>(rows.size());
        const bool can_edit = edit_actions_available();
        if (row_count == 0 && can_edit &&
            ImGui::Button(tr("button.add_row").c_str())) {
            insert_editable_list_row(
                edit, k_station_definition_edit_spec, -1, false);
        }
        ImVec2 table_size(0.0f, scroll_x_table_height_for_rows(row_count));
        if (ImGui::BeginTable("station_definitions", column_count,
                              ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                              ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollX,
                              table_size)) {
            for (int i = 0; i < column_count; ++i) {
                ImGui::TableSetupColumn(columns[i].header, ImGuiTableColumnFlags_WidthFixed,
                                        columns[i].width);
            }
            setup_fixed_table_header();
            ImGui::TableHeadersRow();
            const auto source_file_at = [&](int visible_row) -> const std::string& {
                if (edit.rows_initialized) {
                    return edit.rows[edit.visible_rows[static_cast<size_t>(visible_row)]]
                        .target_source_file;
                }
                return rows[static_cast<size_t>(visible_row)].source.file_path;
            };
            const auto edit_id_at = [&](int visible_row) -> const std::string& {
                if (edit.rows_initialized) {
                    return editable_list_row_identity(
                        edit.rows[edit.visible_rows[static_cast<size_t>(visible_row)]]);
                }
                return rows[static_cast<size_t>(visible_row)].edit_id;
            };
            const auto row_deleted_at = [&](int visible_row) {
                return edit.rows_initialized &&
                    edit.rows[edit.visible_rows[static_cast<size_t>(visible_row)]]
                        .deleted;
            };
            const auto render_context_menu = [&](int visible_row, int column,
                                                 bool actions_available) {
                // Mutating draft vectors mid-render invalidates the row
                // pointers below; queue the action until after EndTable().
                const auto defer_action =
                    [&](DeferredEditableListAction::Kind kind, int action_row,
                        int action_column) {
                        pending_editable_list_actions_.push_back(
                            {&edit, &k_station_definition_edit_spec, kind,
                             action_row, action_column, false});
                    };
                const bool move_up_available = actions_available && visible_row > 0 &&
                    !edit_id_at(visible_row - 1).empty() &&
                    !row_deleted_at(visible_row - 1) &&
                    source_file_at(visible_row - 1) == source_file_at(visible_row);
                const bool move_down_available = actions_available &&
                    visible_row + 1 < row_count &&
                    !edit_id_at(visible_row + 1).empty() &&
                    !row_deleted_at(visible_row + 1) &&
                    source_file_at(visible_row + 1) == source_file_at(visible_row);
                if (!ImGui::BeginPopupContextItem("##station_definition_context")) return;
                ImGui::BeginDisabled(!actions_available);
                if (ImGui::MenuItem(
                        tr("context.station_list.insert_above").c_str())) {
                    defer_action(
                        DeferredEditableListAction::Kind::InsertAbove,
                        visible_row, -1);
                    ImGui::EndDisabled();
                    ImGui::EndPopup();
                    return;
                }
                if (ImGui::MenuItem(
                        tr("context.station_list.insert_below").c_str())) {
                    defer_action(
                        DeferredEditableListAction::Kind::InsertBelow,
                        visible_row, -1);
                    ImGui::EndDisabled();
                    ImGui::EndPopup();
                    return;
                }
                ImGui::EndDisabled();
                ImGui::BeginDisabled(!move_up_available);
                if (ImGui::MenuItem(tr("context.station_list.move_up").c_str())) {
                    defer_action(DeferredEditableListAction::Kind::MoveUp,
                                 visible_row, -1);
                    ImGui::EndDisabled();
                    ImGui::EndPopup();
                    return;
                }
                ImGui::EndDisabled();
                ImGui::BeginDisabled(!move_down_available);
                if (ImGui::MenuItem(tr("context.station_list.move_down").c_str())) {
                    defer_action(DeferredEditableListAction::Kind::MoveDown,
                                 visible_row, -1);
                    ImGui::EndDisabled();
                    ImGui::EndPopup();
                    return;
                }
                ImGui::EndDisabled();
                ImGui::BeginDisabled(!actions_available);
                if (ImGui::MenuItem(tr("context.station_list.clear_cell").c_str())) {
                    defer_action(DeferredEditableListAction::Kind::ClearCell,
                                 visible_row, column);
                    ImGui::EndDisabled();
                    ImGui::EndPopup();
                    return;
                }
                if (ImGui::MenuItem(tr("context.station_list.delete_row").c_str())) {
                    defer_action(DeferredEditableListAction::Kind::DeleteRow,
                                 visible_row, -1);
                    ImGui::EndDisabled();
                    ImGui::EndPopup();
                    return;
                }
                ImGui::EndDisabled();
                ImGui::EndPopup();
            };
            ImGuiListClipper clipper;
            clipper.Begin(row_count);
            while (clipper.Step()) {
                for (int row_index = clipper.DisplayStart; row_index < clipper.DisplayEnd; ++row_index) {
                    const CachedTableRow* cached_row = edit.rows_initialized ? nullptr :
                        &rows[static_cast<size_t>(row_index)];
                    EditableListDraftRow* draft_row = edit.rows_initialized
                        ? &edit.rows[edit.visible_rows[static_cast<size_t>(row_index)]]
                        : nullptr;
                    const std::string& target_edit_id = draft_row
                        ? editable_list_row_identity(*draft_row) : cached_row->edit_id;
                    const bool draft_deleted = draft_row && draft_row->deleted;
                    const bool row_editable =
                        can_edit && !draft_deleted && !target_edit_id.empty();
                    ImGui::TableNextRow();
                    if (draft_deleted || row_is_pending_delete(target_edit_id)) {
                        ImGui::TableSetBgColor(
                            ImGuiTableBgTarget_RowBg0, k_pending_delete_row_color);
                    } else if (row_has_pending_edit(target_edit_id) ||
                        (draft_row && editable_list_row_has_draft(*draft_row))) {
                        ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, k_pending_edit_row_color);
                    }
                    ImGui::PushID(row_index);
                    for (int col = 0; col < column_count; ++col) {
                        ImGui::TableSetColumnIndex(col);
                        ImGui::PushID(col);
                        const std::string& display = draft_row
                            ? draft_row->values[static_cast<size_t>(col)]
                            : cached_row->cells[static_cast<size_t>(col)];
                        const bool is_editing =
                            edit.editing_edit_id == target_edit_id &&
                            edit.editing_column == col;
                        const bool is_selected = (edit.selected_row == row_index && edit.selected_column == col);

                        if (row_editable && is_editing) {
                            if (render_editable_cell_input(
                                    edit.edit_buffer, edit.edit_buffer_fresh,
                                    [&]() {
                                        render_context_menu(row_index, col, row_editable);
                                    })) {
                                commit_editable_list_active_edit(
                                    edit, k_station_definition_edit_spec);
                            }
                            ImGui::PopID();
                            continue;
                        }

                        float cell_w = ImGui::GetContentRegionAvail().x;
                        float cell_h = ImGui::GetFrameHeight();
                        const EditableCellInteraction interaction = render_editable_cell_button(
                            display, is_selected, ImGui::GetColorU32(ImGuiCol_Text),
                            cell_w, cell_h);
                        if (interaction.right_clicked) {
                            edit.selected_row = row_index;
                            edit.selected_column = col;
                        }
                        render_context_menu(row_index, col, row_editable);
                        if (row_editable) {
                            if (interaction.left_clicked) {
                                edit.selected_row = row_index;
                                edit.selected_column = col;
                            }
                            if (interaction.double_clicked) {
                                commit_editable_list_active_edit(
                                    edit, k_station_definition_edit_spec);
                                if (!initialize_editable_list_draft_rows(
                                        edit, k_station_definition_edit_spec)) {
                                    ImGui::PopID();
                                    continue;
                                }
                                EditableListDraftRow& active_row =
                                    edit.rows[edit.visible_rows[static_cast<size_t>(row_index)]];
                                edit.editing_column = col;
                                edit.editing_edit_id =
                                    editable_list_row_identity(active_row);
                                edit.editing_baseline =
                                    active_row.values[static_cast<size_t>(col)];
                                edit.edit_buffer = edit.editing_baseline;
                                edit.edit_buffer_fresh = true;
                            }
                        }
                        ImGui::PopID();
                    }
                    ImGui::PopID();
                }
            }
            ImGui::EndTable();
            run_pending_editable_list_actions();
        }
    }
    ImGui::End();
}

void App::render_structure_models_window() {
    if (!show_structure_models_window_) return;
    if (dock_right_id_) ImGui::SetNextWindowDockID(dock_right_id_, ImGuiCond_FirstUseEver);
    std::string title = tr("frame.structure_models") + "###StructureModels";
    if (!ImGui::Begin(title.c_str(), &show_structure_models_window_)) {
        ImGui::End();
        return;
    }
    const ResourceListKind resource_kind = ResourceListKind::Structure;
    const bool resource_list_present = has_model_ &&
        model_.resource_list_sources[static_cast<size_t>(resource_kind)].present;
    if (!resource_list_present) {
        const std::string message = resource_list_unavailable_message(
            has_model_, tr("status.resource_list_no_map"),
            tr("status.resource_list_not_specified"),
            tr(resource_list_name_translation_key(resource_kind)));
        if (render_resource_list_empty_overlay(
                message, tr("button.new_or_import_file"))) {
            open_new_file_wizard(NewFileKind::Structure);
        }
        ImGui::End();
        return;
    }
    if (render_resource_list_source(
        model_.resource_list_sources[static_cast<size_t>(resource_kind)],
        tr("label.source_path"), tr("menu.open_in_explorer"),
        tr("menu.change_file"), edit_actions_available())) {
        request_resource_list_file_change(ResourceListKind::Structure);
    }
    ensure_table_cache();

    render_table_find_panel(
        structure_model_find_,
        "structure_model",
        tr("button.find"),
        tr("find.partial_match"),
        tr("find.exact_match"),
        tr("button.find_unused_structure_models"),
        [this]() { run_structure_model_find(); },
        [this]() { run_unused_structure_model_search(); },
        [this](int delta) { step_structure_model_find(delta); },
        [this]() { return structure_model_find_status_text(); });

    ImGui::BeginDisabled(
        !edit_actions_available() ||
        !has_editable_list_drafts(
            structure_model_edit_, k_structure_model_edit_spec));
    if (ImGui::Button(tr("button.apply").c_str())) {
        apply_editable_list_drafts(
            structure_model_edit_, k_structure_model_edit_spec);
    }
    ImGui::EndDisabled();
    render_editable_list_table(
        "structure_models", k_structure_model_columns,
        IM_ARRAYSIZE(k_structure_model_columns),
        table_cache_.structure_model_rows, structure_model_edit_,
        k_structure_model_edit_spec,
        table_cache_.structure_model_file_path_width, 0.0f,
        &structure_model_find_);
    ImGui::End();
}

void App::render_sound_file_find_panel(bool is_3d) {
    TableFindState& state = is_3d ? sound_3d_file_find_ : sound_file_find_;

    render_table_find_panel(
        state,
        is_3d ? "sound_3d_file" : "sound_file",
        tr("button.find"),
        tr("find.partial_match"),
        tr("find.exact_match"),
        tr(is_3d ? "button.find_unused_sound_3d_files" : "button.find_unused_sound_files"),
        [this, is_3d]() { run_sound_file_find(is_3d); },
        [this, is_3d]() { run_unused_sound_file_search(is_3d); },
        [this, is_3d](int delta) { step_sound_file_find(is_3d, delta); },
        [this, is_3d]() { return sound_file_find_status_text(is_3d); });
}

void App::render_sound_list_window() {
    if (!show_sound_list_window_) return;
    if (dock_right_id_) ImGui::SetNextWindowDockID(dock_right_id_, ImGuiCond_FirstUseEver);
    std::string title = tr("frame.sound_list") + "###SoundList";
    if (!ImGui::Begin(title.c_str(), &show_sound_list_window_)) {
        ImGui::End();
        return;
    }
    const ResourceListKind resource_kind = ResourceListKind::Sound;
    const bool resource_list_present = has_model_ &&
        model_.resource_list_sources[static_cast<size_t>(resource_kind)].present;
    if (!resource_list_present) {
        const std::string message = resource_list_unavailable_message(
            has_model_, tr("status.resource_list_no_map"),
            tr("status.resource_list_not_specified"),
            tr(resource_list_name_translation_key(resource_kind)));
        if (render_resource_list_empty_overlay(
                message, tr("button.new_or_import_file"))) {
            open_new_file_wizard(NewFileKind::Sound);
        }
        ImGui::End();
        return;
    }
    if (render_resource_list_source(
        model_.resource_list_sources[static_cast<size_t>(ResourceListKind::Sound)],
        tr("label.source_path"), tr("menu.open_in_explorer"),
        tr("menu.change_file"), edit_actions_available())) {
        request_resource_list_file_change(ResourceListKind::Sound);
    }
    ensure_table_cache();
    render_sound_file_find_panel(false);
    ImGui::BeginDisabled(
        !edit_actions_available() ||
        !has_editable_list_drafts(sound_list_edit_, k_sound_list_edit_spec));
    if (ImGui::Button(tr("button.apply").c_str())) {
        apply_editable_list_drafts(sound_list_edit_, k_sound_list_edit_spec);
    }
    ImGui::EndDisabled();
    render_editable_list_table(
        "sound_list", k_sound_list_columns, IM_ARRAYSIZE(k_sound_list_columns),
        table_cache_.sound_list_rows, sound_list_edit_, k_sound_list_edit_spec,
        table_cache_.sound_list_file_path_width,
        table_cache_.sound_list_buffer_count_width, &sound_file_find_);
    ImGui::End();
}

void App::render_sound_3d_list_window() {
    if (!show_sound_3d_list_window_) return;
    if (dock_right_id_) ImGui::SetNextWindowDockID(dock_right_id_, ImGuiCond_FirstUseEver);
    std::string title = tr("frame.sound_3d_list") + "###Sound3DList";
    if (!ImGui::Begin(title.c_str(), &show_sound_3d_list_window_)) {
        ImGui::End();
        return;
    }
    const ResourceListKind resource_kind = ResourceListKind::Sound3D;
    const bool resource_list_present = has_model_ &&
        model_.resource_list_sources[static_cast<size_t>(resource_kind)].present;
    if (!resource_list_present) {
        const std::string message = resource_list_unavailable_message(
            has_model_, tr("status.resource_list_no_map"),
            tr("status.resource_list_not_specified"),
            tr(resource_list_name_translation_key(resource_kind)));
        if (render_resource_list_empty_overlay(
                message, tr("button.new_or_import_file"))) {
            open_new_file_wizard(NewFileKind::Sound3D);
        }
        ImGui::End();
        return;
    }
    if (render_resource_list_source(
        model_.resource_list_sources[static_cast<size_t>(ResourceListKind::Sound3D)],
        tr("label.source_path"), tr("menu.open_in_explorer"),
        tr("menu.change_file"), edit_actions_available())) {
        request_resource_list_file_change(ResourceListKind::Sound3D);
    }
    ensure_table_cache();
    render_sound_file_find_panel(true);
    ImGui::BeginDisabled(
        !edit_actions_available() ||
        !has_editable_list_drafts(
            sound_3d_list_edit_, k_sound_3d_list_edit_spec));
    if (ImGui::Button(tr("button.apply").c_str())) {
        apply_editable_list_drafts(
            sound_3d_list_edit_, k_sound_3d_list_edit_spec);
    }
    ImGui::EndDisabled();
    render_editable_list_table(
        "sound_3d_list", k_sound_list_columns,
        IM_ARRAYSIZE(k_sound_list_columns),
        table_cache_.sound_3d_list_rows, sound_3d_list_edit_,
        k_sound_3d_list_edit_spec,
        table_cache_.sound_3d_list_file_path_width,
        table_cache_.sound_3d_list_buffer_count_width, &sound_3d_file_find_);
    ImGui::End();
}

void App::render_signal_aspects_window() {
    if (!show_signal_aspects_window_) return;
    if (dock_right_id_) ImGui::SetNextWindowDockID(dock_right_id_, ImGuiCond_FirstUseEver);
    if (focus_signal_aspects_next_) ImGui::SetNextWindowFocus();
    std::string title = tr("frame.signal_aspects") + "###SignalAspects";
    if (!ImGui::Begin(title.c_str(), &show_signal_aspects_window_)) {
        focus_signal_aspects_next_ = false;
        ImGui::End();
        return;
    }
    const ResourceListKind resource_kind = ResourceListKind::Signal;
    const bool resource_list_present = has_model_ &&
        model_.resource_list_sources[static_cast<size_t>(resource_kind)].present;
    if (!resource_list_present) {
        const std::string message = resource_list_unavailable_message(
            has_model_, tr("status.resource_list_no_map"),
            tr("status.resource_list_not_specified"),
            tr(resource_list_name_translation_key(resource_kind)));
        if (render_resource_list_empty_overlay(
                message, tr("button.new_or_import_file"))) {
            open_new_file_wizard(NewFileKind::Signal);
        }
        focus_signal_aspects_next_ = false;
        ImGui::End();
        return;
    }
    if (render_resource_list_source(
        model_.resource_list_sources[
            static_cast<size_t>(ResourceListKind::Signal)],
        tr("label.source_path"), tr("menu.open_in_explorer"),
        tr("menu.change_file"), edit_actions_available())) {
        request_resource_list_file_change(ResourceListKind::Signal);
    }
    ensure_table_cache();
    const bool stale_find_results = signal_aspect_find_.has_run &&
        signal_aspect_find_.committed != signal_aspect_find_.query;
    if (stale_find_results) reset_signal_aspect_find_results();

    render_table_find_panel(
        signal_aspect_find_,
        "signal_aspect",
        tr("button.find"),
        tr("find.partial_match"),
        tr("find.exact_match"),
        tr("button.find_unused_signal_aspects"),
        [this]() { run_signal_aspect_find(); },
        [this]() { run_unused_signal_aspect_search(); },
        [this](int delta) { step_signal_aspect_find(delta); },
        [this]() { return signal_aspect_find_status_text(); });

    ImGui::BeginDisabled(
        !edit_actions_available() ||
        !has_editable_list_drafts(
            signal_aspect_edit_, k_signal_aspect_edit_spec));
    if (ImGui::Button(tr("button.apply").c_str())) {
        apply_editable_list_drafts(
            signal_aspect_edit_, k_signal_aspect_edit_spec);
    }
    ImGui::EndDisabled();
    const bool has_inserted_signal_draft =
        signal_aspect_edit_.rows_initialized &&
        std::any_of(
            signal_aspect_edit_.rows.begin(), signal_aspect_edit_.rows.end(),
            [](const EditableListDraftRow& row) { return row.inserted; });
    if (has_inserted_signal_draft) {
        const size_t required_columns =
            k_signal_aspect_structure_key_column_offset + 5;
        while (table_cache_.signal_aspect_column_headers.size() <
               required_columns) {
            const size_t key_index =
                table_cache_.signal_aspect_column_headers.size() -
                k_signal_aspect_structure_key_column_offset + 1;
            table_cache_.signal_aspect_column_headers.push_back(
                "structureKey" + std::to_string(key_index));
            table_cache_.signal_aspect_column_widths.push_back(120.0f);
        }
    }
    render_editable_list_table(
        "signal_aspects", nullptr,
        static_cast<int>(
            table_cache_.signal_aspect_column_headers.size()),
        table_cache_.signal_aspect_rows, signal_aspect_edit_,
        k_signal_aspect_edit_spec, 0.0f, 0.0f,
        &signal_aspect_find_,
        &table_cache_.signal_aspect_column_headers,
        &table_cache_.signal_aspect_column_widths,
        &table_cache_.signal_aspect_display_rows);
    focus_signal_aspects_next_ = false;
    ImGui::End();
}

