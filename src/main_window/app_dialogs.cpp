/*
 * Copyright (c) 2026 Sapporo_ningyo
 *
 * Licensed under Apache License 2.0; see LICENSE and NOTICE.
 * The GUI uses Dear ImGui and ImPlot; see THIRD_PARTY_NOTICES.md.
 */

#ifdef _MSC_VER
#pragma execution_character_set("utf-8")
#endif

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
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
#include <shobjidl.h>
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

std::string App::open_map_dialog() {
    wchar_t file[MAX_PATH] = {};
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = nullptr;
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    std::wstring filter;
    const auto append_filter =
        [&](const std::wstring& label, const wchar_t* value) {
            filter += label;
            filter.push_back(L'\0');
            filter += value;
            filter.push_back(L'\0');
        };
    append_filter(utf8_to_wide(tr("dialog.filter.map_files")), L"*.txt;*.csv");
    append_filter(utf8_to_wide(tr("dialog.filter.all_files")), L"*.*");
    ofn.lpstrFilter = filter.c_str();
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
    if (GetOpenFileNameW(&ofn)) return wide_to_utf8(file);
    return {};
}

std::string App::open_map_dialog(const std::string& initial_directory,
                                 const char* title_key) {
    wchar_t file[MAX_PATH] = {};
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = nullptr;
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    std::wstring filter;
    const auto append_filter = [&](const std::wstring& label, const wchar_t* value) {
        filter += label;
        filter.push_back(L'\0');
        filter += value;
        filter.push_back(L'\0');
    };
    append_filter(utf8_to_wide(tr("dialog.filter.map_files")), L"*.txt;*.csv");
    append_filter(utf8_to_wide(tr("dialog.filter.all_files")), L"*.*");
    filter.push_back(L'\0');
    ofn.lpstrFilter = filter.c_str();
    ofn.nFilterIndex = 1;
    const std::wstring initial = utf8_to_wide(initial_directory);
    ofn.lpstrInitialDir = initial.empty() ? nullptr : initial.c_str();
    const std::wstring title = utf8_to_wide(tr(title_key));
    ofn.lpstrTitle = title.c_str();
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
    if (GetOpenFileNameW(&ofn)) return wide_to_utf8(file);
    return {};
}

std::string App::open_image_dialog() {
    wchar_t file[MAX_PATH] = {};
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = nullptr;
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrFilter = L"Images\0*.png;*.jpg;*.jpeg;*.bmp;*.tif;*.tiff\0All files\0*.*\0";
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
    if (GetOpenFileNameW(&ofn)) return wide_to_utf8(file);
    return {};
}

std::string App::open_image_dialog(const std::string& initial_directory,
                                   const char* title_key) {
    wchar_t file[MAX_PATH] = {};
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = nullptr;
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrFilter = L"Images\0*.png;*.jpg;*.jpeg;*.bmp;*.tif;*.tiff\0All files\0*.*\0\0";
    ofn.nFilterIndex = 1;
    const std::wstring initial = utf8_to_wide(initial_directory);
    ofn.lpstrInitialDir = initial.empty() ? nullptr : initial.c_str();
    const std::wstring title = utf8_to_wide(tr(title_key));
    ofn.lpstrTitle = title.c_str();
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
    if (GetOpenFileNameW(&ofn)) return wide_to_utf8(file);
    return {};
}

std::string App::open_editable_list_file_dialog(
    const EditableListSpec& spec,
    const std::string& initial_directory) {
    const bool is_structure =
        std::string_view(spec.row_kind) == "structure.model";
    const wchar_t* pattern = is_structure
        ? L"*.csv;*.b3d;*.x;*.obj;*.fbx;*.dae;*.gltf;*.glb"
        : L"*.wav;*.ogg;*.mp3;*.flac";
    std::wstring resource_label = utf8_to_wide(
        tr(is_structure ? "dialog.filter.model_files"
                        : "dialog.filter.audio_files"));
    resource_label += L" (";
    resource_label += pattern;
    resource_label += L")";

    std::wstring filter;
    const auto append_filter =
        [&](const std::wstring& label, const wchar_t* value) {
            filter += label;
            filter.push_back(L'\0');
            filter += value;
            filter.push_back(L'\0');
        };
    append_filter(resource_label, pattern);
    append_filter(utf8_to_wide(tr("dialog.filter.all_files")), L"*.*");
    filter.push_back(L'\0');

    wchar_t file[MAX_PATH] = {};
    const std::wstring initial_directory_wide =
        utf8_to_wide(initial_directory);
    const std::wstring title =
        utf8_to_wide(tr("dialog.select_resource_file"));
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = nullptr;
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrFilter = filter.c_str();
    ofn.nFilterIndex = 1;
    ofn.lpstrInitialDir = initial_directory_wide.empty()
        ? nullptr : initial_directory_wide.c_str();
    ofn.lpstrTitle = title.c_str();
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
    if (GetOpenFileNameW(&ofn)) return wide_to_utf8(file);
    return {};
}

std::string App::open_include_file_dialog(const std::string& initial_directory,
                                          const char* title_key,
                                          const char* filter_key) {
    wchar_t file[MAX_PATH] = {};
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = nullptr;
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    std::wstring filter;
    const auto append_filter =
        [&](const std::wstring& label, const wchar_t* value) {
            filter += label;
            filter.push_back(L'\0');
            filter += value;
            filter.push_back(L'\0');
        };
    append_filter(utf8_to_wide(tr(filter_key)), L"*.txt;*.csv");
    append_filter(utf8_to_wide(tr("dialog.filter.all_files")), L"*.*");
    filter.push_back(L'\0');
    ofn.lpstrFilter = filter.c_str();
    ofn.nFilterIndex = 1;
    const std::wstring initial_directory_wide =
        utf8_to_wide(initial_directory);
    ofn.lpstrInitialDir = initial_directory_wide.empty()
        ? nullptr : initial_directory_wide.c_str();
    const std::wstring title = utf8_to_wide(tr(title_key));
    ofn.lpstrTitle = title.c_str();
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
    if (GetOpenFileNameW(&ofn)) return wide_to_utf8(file);
    return {};
}

std::string App::save_include_file_dialog(const std::string& initial_directory) {
    wchar_t file[MAX_PATH] = {};
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = nullptr;
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    std::wstring filter;
    const auto append_filter =
        [&](const std::wstring& label, const wchar_t* value) {
            filter += label;
            filter.push_back(L'\0');
            filter += value;
            filter.push_back(L'\0');
        };
    append_filter(utf8_to_wide(tr("dialog.filter.map_files")), L"*.txt;*.csv");
    append_filter(utf8_to_wide(tr("dialog.filter.all_files")), L"*.*");
    filter.push_back(L'\0');
    ofn.lpstrFilter = filter.c_str();
    ofn.nFilterIndex = 1;
    ofn.lpstrDefExt = L"txt";
    const std::wstring initial_directory_wide = utf8_to_wide(initial_directory);
    ofn.lpstrInitialDir = initial_directory_wide.empty()
        ? nullptr : initial_directory_wide.c_str();
    const std::wstring title = utf8_to_wide(tr("dialog.create_include_file"));
    ofn.lpstrTitle = title.c_str();
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (GetSaveFileNameW(&ofn)) return wide_to_utf8(file);
    return {};
}

std::string_view new_bve_file_header(NewFileKind kind) {
    switch (kind) {
    case NewFileKind::Map: return "BveTs Map 2.02:utf-8\r\n";
    case NewFileKind::Structure: return "BveTs Structure List 2.00:utf-8\r\n";
    case NewFileKind::Signal: return "BveTs Signal Aspects List 2.00:utf-8\r\n";
    case NewFileKind::Sound:
    case NewFileKind::Sound3D: return "BveTs Sound List 2.00:utf-8\r\n";
    case NewFileKind::Station: return "BveTs Station List 2.00:utf-8\r\n";
    case NewFileKind::Scenario: return "BveTs Scenario 2.00:utf-8\r\n";
    }
    return {};
}

std::string_view new_file_resource_list_kind(NewFileKind kind) {
    switch (kind) {
    case NewFileKind::Structure: return "structure";
    case NewFileKind::Signal: return "signal";
    case NewFileKind::Sound: return "sound";
    case NewFileKind::Sound3D: return "sound3d";
    case NewFileKind::Station: return "station";
    case NewFileKind::Map:
    case NewFileKind::Scenario: return {};
    }
    return {};
}

bool build_new_scenario_file_content(const NewFileScenarioDraft& draft,
                                     std::string& content,
                                     std::string& error) {
    struct ScenarioField {
        const char* key;
        const std::string& value;
        bool path;
    };
    const ScenarioField fields[] = {
        {"Title", draft.title, false},
        {"Route", draft.route, true},
        {"RouteTitle", draft.route_title, false},
        {"Vehicle", draft.vehicle, true},
        {"VehicleTitle", draft.vehicle_title, false},
        {"Author", draft.author, false},
        {"Image", draft.image, true},
        {"Comment", draft.comment, false},
    };
    content = new_bve_file_header(NewFileKind::Scenario);
    for (const ScenarioField& field : fields) {
        std::string value = trim_gui_ascii_copy(field.value);
        if (value.empty()) continue;
        // A Scenario value must round-trip as one official key = value row:
        // '#' or ';' start a comment, and '|' or '*' start weighted
        // multi-candidate syntax that the single-path wizard draft does not
        // model. Reject instead of emitting text with different semantics.
        if (value.find_first_of("#;\r\n") != std::string::npos ||
            (field.path && value.find_first_of("|*") != std::string::npos)) {
            error = std::string("new Scenario ") + field.key +
                " value contains characters that are invalid in a single-path"\
                " official Scenario row: \"" + value + "\"";
            return false;
        }
        content += field.key;
        content += " = ";
        content += value;
        content += "\r\n";
    }
    return true;
}

bool create_utf8_bve_file_exclusive(const std::filesystem::path& path,
                                    std::string_view header,
                                    std::string& error) {
    if (path.empty() || path.filename().empty()) {
        error = "new BVE file path is empty";
        return false;
    }
    // |header| carries the initial file bytes: a bare format header for map
    // and list files, or the complete official body for Scenario files.
    if (header.empty() || header.size() > static_cast<size_t>(std::numeric_limits<DWORD>::max())) {
        error = "new BVE file content is invalid";
        return false;
    }
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        error = "failed to create BVE file (Win32 error " +
            std::to_string(static_cast<unsigned long>(GetLastError())) + ")";
        return false;
    }
    DWORD written = 0;
    const BOOL wrote = WriteFile(file, header.data(),
                                 static_cast<DWORD>(header.size()),
                                 &written, nullptr);
    const DWORD write_error = wrote ? ERROR_SUCCESS : GetLastError();
    const BOOL closed = CloseHandle(file);
    if (!wrote || written != header.size() || !closed) {
        error = "failed to write BVE file content (Win32 error " +
            std::to_string(static_cast<unsigned long>(
                wrote && closed ? ERROR_WRITE_FAULT : write_error)) + ")";
        return false;
    }
    return true;
}

bool create_utf8_bve_map_file_exclusive(const std::filesystem::path& path,
                                        std::string& error) {
    return create_utf8_bve_file_exclusive(path, new_bve_file_header(NewFileKind::Map), error);
}

std::string App::choose_folder_dialog(const char* title_key) {
    IFileOpenDialog* dialog = nullptr;
    HRESULT hr = CoCreateInstance(
        CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_IFileOpenDialog,
        reinterpret_cast<void**>(&dialog));
    if (FAILED(hr) || !dialog) return {};

    FILEOPENDIALOGOPTIONS options = {};
    hr = dialog->GetOptions(&options);
    if (SUCCEEDED(hr)) {
        hr = dialog->SetOptions(static_cast<FILEOPENDIALOGOPTIONS>(
            options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST));
    }
    const std::wstring title = utf8_to_wide(tr(title_key));
    if (SUCCEEDED(hr)) hr = dialog->SetTitle(title.c_str());
    if (SUCCEEDED(hr)) hr = dialog->Show(nullptr);

    IShellItem* item = nullptr;
    if (SUCCEEDED(hr)) hr = dialog->GetResult(&item);
    PWSTR path = nullptr;
    if (SUCCEEDED(hr) && item) hr = item->GetDisplayName(SIGDN_FILESYSPATH, &path);

    std::string selected;
    if (SUCCEEDED(hr) && path) selected = wide_to_utf8(path);
    if (path) CoTaskMemFree(path);
    if (item) item->Release();
    dialog->Release();
    return selected;
}

void App::render_popups() {
    auto sync_runtime_settings_before_save = [&]() {
        settings_.window_visibility = current_window_visibility();
        last_saved_window_visibility_ = settings_.window_visibility;
        settings_.view_2d = current_view_2d_settings();
        last_saved_view_2d_settings_ = settings_.view_2d;
        settings_.view_3d = current_view_3d_settings();
        last_saved_view_3d_settings_ = settings_.view_3d;
    };

    if (other_track_rename_.popup_requested) {
        ImGui::OpenPopup(tr("dialog.other_track_rename_title").c_str());
        other_track_rename_.popup_requested = false;
    }
    bool apply_other_track_rename_after_popup = false;
    if (ImGui::BeginPopupModal(
            tr("dialog.other_track_rename_title").c_str(), nullptr,
            ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(tr("label.track_key").c_str());
        ImGui::SameLine();
        ImGui::SetNextItemWidth(360.0f);
        ImGui::InputText("##OtherTrackRenameKey", &other_track_rename_.draft_key);
        ImGui::Separator();
        if (ImGui::Button(tr("button.ok").c_str())) {
            const std::string requested_key =
                trim_gui_ascii_copy(other_track_rename_.draft_key);
            if (requested_key.empty()) {
                set_program_status("status.edit.required_field");
            } else {
                other_track_rename_.apply_key = requested_key;
                apply_other_track_rename_after_popup = true;
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button(tr("button.cancel").c_str())) {
            other_track_rename_ = OtherTrackRenameState{};
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    if (apply_other_track_rename_after_popup) {
        request_edit_ui_operation(PendingEditUiOperation::ApplyOtherTrackRename);
        return;
    }

    if (popups_.resource_list_file_change_confirm) {
        ImGui::OpenPopup(tr("dialog.resource_list_file_change_title").c_str());
        popups_.resource_list_file_change_confirm = false;
    }
    if (ImGui::BeginPopupModal(
            tr("dialog.resource_list_file_change_title").c_str(), nullptr,
            ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 520.0f);
        ImGui::TextUnformatted(tr("dialog.resource_list_file_change_message").c_str());
        ImGui::PopTextWrapPos();
        ImGui::Separator();
        if (ImGui::Button(tr("button.ok").c_str())) {
            if (resource_list_file_change_confirmation_) {
                resource_list_file_change_confirmation_->confirmed_discard = true;
                pending_resource_list_file_change_request_ = std::move(
                    *resource_list_file_change_confirmation_);
                resource_list_file_change_confirmation_.reset();
            }
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button(tr("button.cancel").c_str())) {
            resource_list_file_change_confirmation_.reset();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    if (popups_.edit_mode_warning) {
        ImGui::OpenPopup(tr("dialog.edit_mode_warning_title").c_str());
        popups_.edit_mode_warning = false;
    }
    ImGui::SetNextWindowSize(ImVec2(560.0f, 0.0f), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal(tr("dialog.edit_mode_warning_title").c_str(), nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 520.0f);
        ImGui::TextUnformatted(tr("dialog.edit_mode_warning_message").c_str());
        ImGui::PopTextWrapPos();
        ImGui::Checkbox(tr("chk.edit_mode_warning_dont_show").c_str(),
                        &edit_mode_warning_dont_show_);
        ImGui::Separator();
        if (ImGui::Button(tr("button.ok").c_str())) {
            if (edit_mode_warning_dont_show_) {
                settings_.edit_mode_warning_suppressed = true;
            }
            apply_edit_mode_enabled(true);
            edit_mode_warning_dont_show_ = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button(tr("button.cancel").c_str())) {
            edit_mode_warning_dont_show_ = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    if (new_element_wizard_.open &&
        new_element_wizard_.repeater_change_point_prompt_requested) {
        ImGui::OpenPopup(tr("dialog.repeater_change_point_insert_title").c_str());
        new_element_wizard_.repeater_change_point_prompt_requested = false;
    }
    bool apply_repeater_change_point_after_popup = false;
    if (ImGui::BeginPopupModal(
            tr("dialog.repeater_change_point_insert_title").c_str(), nullptr,
            ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 520.0f);
        ImGui::TextUnformatted(
            tr("dialog.repeater_change_point_insert_message").c_str());
        ImGui::PopTextWrapPos();
        ImGui::Separator();
        if (ImGui::Button(tr("button.ok").c_str())) {
            new_element_wizard_.confirm_repeater_change_point_once = true;
            apply_repeater_change_point_after_popup = true;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button(tr("button.cancel").c_str())) {
            new_element_wizard_.confirm_repeater_change_point_once = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    if (apply_repeater_change_point_after_popup) {
        request_edit_ui_operation(PendingEditUiOperation::ApplyNewElement);
        return;
    }

    if (new_element_wizard_.open &&
        new_element_wizard_.repeater_extra_end_prompt_requested) {
        ImGui::OpenPopup(tr("dialog.repeater_extra_end_title").c_str());
        new_element_wizard_.repeater_extra_end_prompt_requested = false;
    }
    if (ImGui::BeginPopupModal(tr("dialog.repeater_extra_end_title").c_str(), nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 520.0f);
        ImGui::TextUnformatted(tr("dialog.repeater_extra_end_message").c_str());
        ImGui::PopTextWrapPos();
        ImGui::Separator();
        if (ImGui::Button(tr("button.ok").c_str())) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    if (inspector_.open &&
        inspector_.coordinate_offset_discard_prompt_requested) {
        ImGui::OpenPopup(tr("dialog.coordinate_offset_discard_title").c_str());
        inspector_.coordinate_offset_discard_prompt_requested = false;
    }
    if (ImGui::BeginPopupModal(
            tr("dialog.coordinate_offset_discard_title").c_str(), nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 480.0f);
        ImGui::TextUnformatted(tr("dialog.coordinate_offset_discard_message").c_str());
        ImGui::PopTextWrapPos();
        ImGui::Separator();
        if (ImGui::Button(tr("button.ok").c_str())) {
            set_inspector_coordinate_offsets_enabled(inspector_, false);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button(tr("button.cancel").c_str())) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    if (inspector_.open && inspector_.signal_full_form_prompt_requested) {
        ImGui::OpenPopup(tr("dialog.signal_full_form_convert_title").c_str());
        inspector_.signal_full_form_prompt_requested = false;
    }
    if (ImGui::BeginPopupModal(tr("dialog.signal_full_form_convert_title").c_str(), nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 480.0f);
        ImGui::TextUnformatted(tr("dialog.signal_full_form_convert_message").c_str());
        ImGui::PopTextWrapPos();
        ImGui::Separator();
        if (ImGui::Button(tr("button.ok").c_str())) {
            inspector_.signal_full_form_conversion_draft = true;
            if (MapElementEditFieldState* field = find_inspector_field(
                    inspector_, inspector_.pending_signal_full_form_field)) {
                set_edit_field_buffer(*field, inspector_.pending_signal_full_form_value);
            }
            inspector_.pending_signal_full_form_field.clear();
            inspector_.pending_signal_full_form_value.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button(tr("button.cancel").c_str())) {
            inspector_.pending_signal_full_form_field.clear();
            inspector_.pending_signal_full_form_value.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    if (inspector_.open && inspector_.z_rebase_prompt_requested) {
        ImGui::OpenPopup(tr("dialog.structure_z_rebase_title").c_str());
        inspector_.z_rebase_prompt_requested = false;
    }
    bool apply_z_rebase_after_popup = false;
    if (ImGui::BeginPopupModal(tr("dialog.structure_z_rebase_title").c_str(), nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 480.0f);
        ImGui::TextUnformatted(tr("dialog.structure_z_rebase_message").c_str());
        ImGui::PopTextWrapPos();
        ImGui::Separator();
        if (ImGui::Button(tr("button.ok").c_str())) {
            MapElementEditFieldState* distance_field = find_inspector_field(inspector_, "distance");
            MapElementEditFieldState* z_field = find_inspector_field(inspector_, "z");
            double distance = 0.0;
            double z = 0.0;
            if (!distance_field || !z_field ||
                !parse_gui_edit_number(edit_field_buffer_text(*distance_field), &distance) ||
                !parse_gui_edit_number(edit_field_buffer_text(*z_field), &z)) {
                set_program_status("status.edit.invalid_number");
                ImGui::CloseCurrentPopup();
            } else {
                z = truncate_gui_thousandths(z);
                const double distance_part = std::trunc(z);
                set_edit_field_buffer(*distance_field,
                                      format_double(distance + distance_part, 12));
                set_edit_field_buffer(*z_field,
                                      format_gui_transform_number(z - distance_part));
                ImGui::CloseCurrentPopup();
                apply_z_rebase_after_popup = true;
            }
        }
        ImGui::SameLine();
        if (ImGui::Button(tr("button.cancel").c_str())) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    if (apply_z_rebase_after_popup) {
        request_edit_ui_operation(PendingEditUiOperation::ApplyInspector);
        return;
    }

    bool cancel_distance_resolution = false;
    if (distance_resolution_workflow_.phase == DistanceResolutionPhase::ConfirmAction &&
        distance_resolution_workflow_.popup_requested) {
        ImGui::OpenPopup(tr("dialog.distance_resolution_title").c_str());
        distance_resolution_workflow_.popup_requested = false;
    }
    if (ImGui::BeginPopupModal(tr("dialog.distance_resolution_title").c_str(), nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 520.0f);
        ImGui::TextUnformatted(tr("dialog.distance_resolution_message").c_str());
        if (!distance_resolution_workflow_.request.source_file.empty()) {
            ImGui::TextDisabled("%s", distance_resolution_workflow_.request.source_file.c_str());
        }
        ImGui::PopTextWrapPos();
        ImGui::Separator();
        if (distance_resolution_workflow_.request.can_confirm_reuse) {
            if (ImGui::Button(tr("button.confirm_reuse").c_str())) {
                DistanceResolutionChoice choice;
                choice.confirm_environment_mismatch = true;
                apply_distance_resolution_choice(choice);
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
        }
        ImGui::BeginDisabled(
            distance_resolution_workflow_.request.allowed_boundaries.empty());
        if (ImGui::Button(tr("button.manual_select").c_str())) {
            distance_resolution_workflow_.phase = DistanceResolutionPhase::SelectBoundary;
            open_text_preview_for_distance_resolution(
                distance_resolution_workflow_.request);
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button(tr("button.cancel").c_str())) {
            cancel_distance_resolution = true;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    if (distance_resolution_workflow_.phase == DistanceResolutionPhase::EditExpression &&
        distance_resolution_workflow_.popup_requested) {
        ImGui::OpenPopup(tr("dialog.distance_expression_title").c_str());
        distance_resolution_workflow_.popup_requested = false;
    }
    if (ImGui::BeginPopupModal(tr("dialog.distance_expression_title").c_str(), nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        std::string message;
        if (distance_resolution_workflow_.request.variable_name.empty()) {
            message = tr("dialog.distance_expression_message");
        } else {
            message = tr("dialog.distance_variable_message");
            const std::string placeholder = "{variable}";
            size_t placeholder_at = message.find(placeholder);
            if (placeholder_at != std::string::npos) {
                message.replace(placeholder_at, placeholder.size(),
                                distance_resolution_workflow_.request.variable_name);
            }
        }
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 520.0f);
        ImGui::TextUnformatted(message.c_str());
        ImGui::PopTextWrapPos();
        ImGui::SetNextItemWidth(520.0f);
        ImGui::InputText("##distance_source_expression",
                         distance_resolution_workflow_.expression_buffer.data(),
                         distance_resolution_workflow_.expression_buffer.size());
        ImGui::Separator();
        ImGui::BeginDisabled(distance_resolution_workflow_.expression_buffer[0] == '\0');
        if (ImGui::Button(tr("button.apply").c_str())) {
            DistanceResolutionChoice choice;
            choice.distance_expression = distance_resolution_workflow_.expression_buffer.data();
            apply_distance_resolution_choice(choice);
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button(tr("button.cancel").c_str())) {
            cancel_distance_resolution = true;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    if (distance_resolution_workflow_.phase == DistanceResolutionPhase::ConfirmAction &&
        !distance_resolution_workflow_.popup_requested &&
        !ImGui::IsPopupOpen(tr("dialog.distance_resolution_title").c_str())) {
        cancel_distance_resolution = true;
    }
    if (distance_resolution_workflow_.phase == DistanceResolutionPhase::EditExpression &&
        !distance_resolution_workflow_.popup_requested &&
        !ImGui::IsPopupOpen(tr("dialog.distance_expression_title").c_str())) {
        cancel_distance_resolution = true;
    }
    if (cancel_distance_resolution) cancel_distance_resolution_workflow();

    if (popups_.ui_settings) {
        ImGui::OpenPopup(tr("dialog.ui_settings").c_str());
        popups_.ui_settings = false;
    }
    bool ui_settings_popup_open = true;
    if (ImGui::BeginPopupModal(tr("dialog.ui_settings").c_str(), &ui_settings_popup_open, ImGuiWindowFlags_AlwaysAutoResize)) {
        auto apply_pending_ui_settings = [&]() {
            apply_ui_settings(pending_font_size_, pending_ui_component_size_, pending_theme_color_, dpi_scale_, viewports_enabled_);
        };
        ImGui::SetNextItemWidth(260.0f);
        if (ImGui::SliderFloat(tr("label.font_size").c_str(), &pending_font_size_, k_min_font_size, k_max_font_size, "%.0f px", ImGuiSliderFlags_AlwaysClamp)) {
            pending_font_size_ = clamp_font_size(pending_font_size_);
            apply_pending_ui_settings();
        }
        ImGui::SetNextItemWidth(260.0f);
        if (ImGui::SliderFloat(tr("label.ui_component_size").c_str(), &pending_ui_component_size_, k_min_ui_component_size, k_max_ui_component_size, "%.0f%%", ImGuiSliderFlags_AlwaysClamp)) {
            pending_ui_component_size_ = clamp_ui_component_size(pending_ui_component_size_);
            apply_pending_ui_settings();
        }
        ImGui::Separator();
        const ImGuiColorEditFlags color_flags = ImGuiColorEditFlags_NoAlpha
            | ImGuiColorEditFlags_DisplayRGB
            | ImGuiColorEditFlags_InputRGB
            | ImGuiColorEditFlags_Uint8
            | ImGuiColorEditFlags_PickerHueBar;
        std::string theme_hex = theme_color_to_string(pending_theme_color_);
        ImGui::TextUnformatted(tr("label.ui_theme_color").c_str());
        ImGui::SameLine();
        const float preview_size = ImGui::GetFrameHeight();
        if (ImGui::ColorButton("##theme_color_preview", pending_theme_color_, ImGuiColorEditFlags_NoAlpha, ImVec2(preview_size, preview_size))) {
            ImGui::OpenPopup("theme_color_popup");
        }
        ImGui::SameLine();
        ImGui::Text("#%s", theme_hex.c_str());
        const auto& palette = ui_theme_palette();
        if (ImGui::BeginPopup("theme_color_popup")) {
            ImGui::SetNextItemWidth(260.0f);
            if (ImGui::ColorPicker3("##theme_color_picker", &pending_theme_color_.x, color_flags)) {
                pending_theme_color_ = clamp_theme_color(pending_theme_color_);
                apply_pending_ui_settings();
            }
            ImGui::Separator();
            const float swatch_size = ImGui::GetFrameHeight();
            for (size_t i = 0; i < palette.size(); ++i) {
                if (i > 0 && i % 6 != 0) ImGui::SameLine();
                std::string id = "##theme_palette_" + std::to_string(i);
                if (ImGui::ColorButton(id.c_str(), palette[i], ImGuiColorEditFlags_NoAlpha, ImVec2(swatch_size, swatch_size))) {
                    pending_theme_color_ = clamp_theme_color(palette[i]);
                    apply_pending_ui_settings();
                }
            }
            ImGui::EndPopup();
        }
        if (ImGui::Button(tr("button.ok").c_str())) {
            font_size_ = clamp_font_size(pending_font_size_);
            ui_component_size_ = clamp_ui_component_size(pending_ui_component_size_);
            theme_color_ = clamp_theme_color(pending_theme_color_);
            settings_.font_size = font_size_;
            settings_.ui_component_size = ui_component_size_;
            settings_.theme_color = theme_color_;
            sync_runtime_settings_before_save();
            save_user_settings(settings_);
            apply_ui_settings(font_size_, ui_component_size_, theme_color_, dpi_scale_, viewports_enabled_);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button(tr("button.cancel").c_str())) {
            pending_font_size_ = font_size_before_dialog_;
            pending_ui_component_size_ = ui_component_size_before_dialog_;
            pending_theme_color_ = theme_color_before_dialog_;
            apply_ui_settings(font_size_, ui_component_size_, theme_color_, dpi_scale_, viewports_enabled_);
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    if (!ui_settings_popup_open) {
        pending_font_size_ = font_size_;
        pending_ui_component_size_ = ui_component_size_;
        pending_theme_color_ = theme_color_;
        apply_ui_settings(font_size_, ui_component_size_, theme_color_, dpi_scale_, viewports_enabled_);
    }

    if (popups_.canvas_element_sizes) {
        ImGui::OpenPopup(tr("dialog.canvas_element_sizes").c_str());
        popups_.canvas_element_sizes = false;
    }
    bool canvas_element_sizes_popup_open = true;
    if (ImGui::BeginPopupModal(tr("dialog.canvas_element_sizes").c_str(), &canvas_element_sizes_popup_open, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::SetNextItemWidth(260.0f);
        int marker_size_steps = static_cast<int>(clamp_marker_size_percent(pending_marker_size_percent_) / k_marker_size_percent_step);
        if (ImGui::SliderInt(tr("label.station_marker_size").c_str(),
                             &marker_size_steps,
                             static_cast<int>(k_min_marker_size_percent) / k_marker_size_percent_step,
                             static_cast<int>(k_max_marker_size_percent) / k_marker_size_percent_step,
                             "%d0%%",
                             ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_NoInput)) {
            pending_marker_size_percent_ = clamp_marker_size_percent(static_cast<float>(marker_size_steps * k_marker_size_percent_step));
            marker_size_percent_ = pending_marker_size_percent_;
        }
        auto line_width_slider = [&](const char* label_key, float* value, float fallback) {
            ImGui::SetNextItemWidth(260.0f);
            int width_steps = static_cast<int>(std::round(clamp_canvas_line_width(*value, fallback) /
                                                          k_canvas_line_width_step_px));
            const int min_steps = static_cast<int>(std::round(k_min_canvas_line_width_px / k_canvas_line_width_step_px));
            const int max_steps = static_cast<int>(std::round(k_max_canvas_line_width_px / k_canvas_line_width_step_px));
            std::string slider_id = std::string("##") + label_key;
            if (ImGui::SliderInt(slider_id.c_str(),
                                 &width_steps,
                                 min_steps,
                                 max_steps,
                                 "",
                                 ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_NoInput)) {
                *value = clamp_canvas_line_width(static_cast<float>(width_steps) * k_canvas_line_width_step_px,
                                                 fallback);
                pending_canvas_line_widths_ = clamp_canvas_line_widths(pending_canvas_line_widths_);
                canvas_line_widths_ = pending_canvas_line_widths_;
            }
            ImVec2 slider_min = ImGui::GetItemRectMin();
            ImVec2 slider_max = ImGui::GetItemRectMax();
            std::string value_text = format_double(
                static_cast<double>(clamp_canvas_line_width(static_cast<float>(width_steps) *
                                                            k_canvas_line_width_step_px,
                                                            fallback)),
                1) + " px";
            ImVec2 value_size = ImGui::CalcTextSize(value_text.c_str());
            ImGui::GetWindowDrawList()->AddText(
                ImVec2((slider_min.x + slider_max.x - value_size.x) * 0.5f,
                       (slider_min.y + slider_max.y - value_size.y) * 0.5f),
                ImGui::GetColorU32(ImGuiCol_Text),
                value_text.c_str());
            ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
            ImGui::TextUnformatted(tr(label_key).c_str());
        };
        line_width_slider("label.own_track_line_width",
                          &pending_canvas_line_widths_.own_track_px,
                          k_default_own_track_line_width_px);
        line_width_slider("label.other_track_line_width",
                          &pending_canvas_line_widths_.other_track_px,
                          k_default_other_track_line_width_px);
        line_width_slider("label.chart_marker_line_width",
                          &pending_canvas_line_widths_.chart_marker_px,
                          k_default_chart_marker_line_width_px);
        line_width_slider("label.background_grid_line_width",
                          &pending_canvas_line_widths_.background_grid_px,
                          k_default_background_grid_line_width_px);
        if (ImGui::Button(tr("button.ok").c_str())) {
            marker_size_percent_ = clamp_marker_size_percent(pending_marker_size_percent_);
            marker_size_percent_before_dialog_ = marker_size_percent_;
            canvas_line_widths_ = clamp_canvas_line_widths(pending_canvas_line_widths_);
            pending_canvas_line_widths_ = canvas_line_widths_;
            canvas_line_widths_before_dialog_ = canvas_line_widths_;
            settings_.marker_size_percent = marker_size_percent_;
            settings_.canvas_line_widths = canvas_line_widths_;
            sync_runtime_settings_before_save();
            save_user_settings(settings_);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button(tr("button.cancel").c_str())) {
            pending_marker_size_percent_ = marker_size_percent_before_dialog_;
            marker_size_percent_ = marker_size_percent_before_dialog_;
            pending_canvas_line_widths_ = canvas_line_widths_before_dialog_;
            canvas_line_widths_ = canvas_line_widths_before_dialog_;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    if (!canvas_element_sizes_popup_open) {
        pending_marker_size_percent_ = marker_size_percent_before_dialog_;
        marker_size_percent_ = marker_size_percent_before_dialog_;
        pending_canvas_line_widths_ = canvas_line_widths_before_dialog_;
        canvas_line_widths_ = canvas_line_widths_before_dialog_;
    }

    if (popups_.canvas_3d_settings) {
        ImGui::OpenPopup(tr("dialog.canvas_3d_settings").c_str());
        popups_.canvas_3d_settings = false;
    }
    bool canvas_3d_settings_popup_open = true;
    if (ImGui::BeginPopupModal(tr("dialog.canvas_3d_settings").c_str(), &canvas_3d_settings_popup_open, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Checkbox(tr("label.scene_auto_load_on_map_open").c_str(),
                        &pending_scene_auto_load_on_map_open_);
        if (ImGui::Checkbox(tr("label.scene_fog_effect").c_str(), &pending_scene_fog_enabled_)) {
            apply_scene_fog_effect_to_canvas(pending_scene_fog_enabled_);
        }
        if (ImGui::Checkbox(tr("label.scene_map_draw_distance").c_str(),
                            &pending_scene_map_draw_distance_enabled_)) {
            apply_scene_map_draw_distance_to_canvas(pending_scene_map_draw_distance_enabled_);
        }
        int draw_distance_chunks = clamp_scene_draw_distance(pending_scene_draw_distance_m_) / k_scene_draw_distance_step_m;
        ImGui::SetNextItemWidth(300.0f);
        if (ImGui::SliderInt(tr("label.scene_draw_distance").c_str(),
                             &draw_distance_chunks,
                             k_min_scene_draw_distance_m / k_scene_draw_distance_step_m,
                             k_max_scene_draw_distance_m / k_scene_draw_distance_step_m,
                             "%d00 m",
                             ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_NoInput)) {
            pending_scene_draw_distance_m_ = clamp_scene_draw_distance(draw_distance_chunks * k_scene_draw_distance_step_m);
            apply_scene_draw_distance_to_canvas(pending_scene_draw_distance_m_);
        }
        int edit_component_size_steps =
            clamp_scene_edit_component_size_percent(pending_scene_edit_component_size_percent_) /
            k_scene_edit_component_size_step_percent;
        ImGui::SetNextItemWidth(300.0f);
        if (ImGui::SliderInt(
                tr("label.scene_edit_component_size").c_str(),
                &edit_component_size_steps,
                k_min_scene_edit_component_size_percent / k_scene_edit_component_size_step_percent,
                k_max_scene_edit_component_size_percent / k_scene_edit_component_size_step_percent,
                "%d0%%",
                ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_NoInput)) {
            pending_scene_edit_component_size_percent_ =
                clamp_scene_edit_component_size_percent(
                    edit_component_size_steps * k_scene_edit_component_size_step_percent);
            apply_scene_edit_component_size_to_canvas(pending_scene_edit_component_size_percent_);
        }
        int camera_speed_steps =
            pending_scene_camera_speed_percent_ / k_scene_camera_speed_step_percent;
        ImGui::SetNextItemWidth(300.0f);
        if (ImGui::SliderInt(
                tr("label.scene_camera_speed").c_str(),
                &camera_speed_steps,
                k_min_scene_camera_speed_percent / k_scene_camera_speed_step_percent,
                k_max_scene_camera_speed_percent / k_scene_camera_speed_step_percent,
                "%d0%%",
                ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_NoInput)) {
            pending_scene_camera_speed_percent_ =
                camera_speed_steps * k_scene_camera_speed_step_percent;
            apply_scene_camera_speed_to_canvas(pending_scene_camera_speed_percent_);
        }
        ImGui::Separator();
        if (ImGui::Checkbox(tr("label.scene_performance_warning").c_str(),
                            &pending_scene_performance_warning_enabled_)) {
            apply_scene_performance_warning_to_canvas(
                pending_scene_performance_warning_enabled_,
                pending_scene_instance_warning_threshold_,
                pending_scene_instance_critical_warning_threshold_);
        }
        const auto render_scene_instance_threshold_slider = [](const char* label, int& value) {
            constexpr int step_count =
                (k_max_scene_instance_warning_threshold -
                 k_min_scene_instance_warning_threshold) /
                k_scene_instance_warning_threshold_step;
            int step_index = (value - k_min_scene_instance_warning_threshold) /
                k_scene_instance_warning_threshold_step;
            ImGui::SetNextItemWidth(300.0f);
            const ImVec2 slider_pos = ImGui::GetCursorScreenPos();
            const float slider_width = ImGui::CalcItemWidth();
            const bool changed = ImGui::SliderInt(
                label, &step_index, 0, step_count, "",
                ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_NoInput);
            if (changed) {
                value = k_min_scene_instance_warning_threshold +
                    step_index * k_scene_instance_warning_threshold_step;
            }
            char value_text[16] = {};
            std::snprintf(value_text, sizeof(value_text), "%d", value);
            const ImVec2 value_size = ImGui::CalcTextSize(value_text);
            ImGui::GetWindowDrawList()->AddText(
                ImVec2(slider_pos.x + (slider_width - value_size.x) * 0.5f,
                       slider_pos.y + ImGui::GetStyle().FramePadding.y),
                ImGui::GetColorU32(ImGuiCol_Text), value_text);
            return changed;
        };
        const auto apply_scene_instance_warning_preview = [this]() {
            if (pending_scene_instance_critical_warning_threshold_ <
                pending_scene_instance_warning_threshold_) {
                pending_scene_instance_critical_warning_threshold_ =
                    pending_scene_instance_warning_threshold_;
            }
            apply_scene_performance_warning_to_canvas(
                pending_scene_performance_warning_enabled_,
                pending_scene_instance_warning_threshold_,
                pending_scene_instance_critical_warning_threshold_);
        };
        if (render_scene_instance_threshold_slider(
                tr("label.scene_instance_warning_threshold").c_str(),
                pending_scene_instance_warning_threshold_)) {
            apply_scene_instance_warning_preview();
        }
        if (render_scene_instance_threshold_slider(
                tr("label.scene_instance_critical_warning_threshold").c_str(),
                pending_scene_instance_critical_warning_threshold_)) {
            apply_scene_instance_warning_preview();
        }
        if (ImGui::Button(tr("button.ok").c_str())) {
            scene_draw_distance_m_ = clamp_scene_draw_distance(pending_scene_draw_distance_m_);
            scene_edit_component_size_percent_ = clamp_scene_edit_component_size_percent(
                pending_scene_edit_component_size_percent_);
            scene_camera_speed_percent_ = pending_scene_camera_speed_percent_;
            scene_performance_warning_enabled_ = pending_scene_performance_warning_enabled_;
            scene_instance_warning_threshold_ = pending_scene_instance_warning_threshold_;
            scene_instance_critical_warning_threshold_ =
                pending_scene_instance_critical_warning_threshold_;
            normalize_scene_instance_warning_thresholds(
                scene_instance_warning_threshold_,
                scene_instance_critical_warning_threshold_);
            scene_fog_enabled_ = pending_scene_fog_enabled_;
            scene_map_draw_distance_enabled_ = pending_scene_map_draw_distance_enabled_;
            scene_auto_load_on_map_open_ = pending_scene_auto_load_on_map_open_;
            sync_scene_settings_dialog_state_from_current();
            apply_scene_draw_distance_to_canvas(scene_draw_distance_m_);
            apply_scene_edit_component_size_to_canvas(scene_edit_component_size_percent_);
            apply_scene_camera_speed_to_canvas(scene_camera_speed_percent_);
            apply_scene_fog_effect_to_canvas(scene_fog_enabled_);
            apply_scene_map_draw_distance_to_canvas(scene_map_draw_distance_enabled_);
            apply_scene_performance_warning_to_canvas(
                scene_performance_warning_enabled_,
                scene_instance_warning_threshold_,
                scene_instance_critical_warning_threshold_);
            sync_runtime_settings_before_save();
            save_user_settings(settings_);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button(tr("button.cancel").c_str())) {
            restore_scene_settings_preview();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    if (!canvas_3d_settings_popup_open &&
        scene_settings_preview_differs_from_dialog_baseline()) {
        restore_scene_settings_preview();
    }

    if (popups_.range) {
        ImGui::OpenPopup((tr("menu.plotlimit") + "###PlotRange").c_str());
        popups_.range = false;
    }
    if (ImGui::BeginPopupModal((tr("menu.plotlimit") + "###PlotRange").c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::InputDouble((tr("label.minimum") + "##PlotRangeMin").c_str(), &plot_min_);
        ImGui::InputDouble((tr("label.maximum") + "##PlotRangeMax").c_str(), &plot_max_);
        if (ImGui::Button(tr("button.apply").c_str())) {
            dmin_ = plot_min_;
            dmax_ = plot_max_;
            keep_plan_view_ = false;
            reset_plot_axes();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button(tr("button.reset").c_str())) {
            plot_min_ = model_.default_min;
            plot_max_ = model_.default_max;
        }
        ImGui::EndPopup();
    }

    if (popups_.control_points) {
        ImGui::OpenPopup((tr("menu.controlpoints") + "###ControlPoints").c_str());
        popups_.control_points = false;
    }
    if (ImGui::BeginPopupModal((tr("menu.controlpoints") + "###ControlPoints").c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::InputDouble((tr("label.minimum") + "##ControlPointsMin").c_str(), &cp_start_);
        ImGui::InputDouble((tr("label.maximum") + "##ControlPointsMax").c_str(), &cp_end_);
        ImGui::InputDouble((tr("label.interval") + "##ControlPointsInterval").c_str(), &cp_interval_);
        if (ImGui::Button(tr("button.apply").c_str())) {
            regenerate_geometry();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button(tr("button.reset").c_str())) {
            cp_start_ = std::max(0.0, round_to_100(model_.own.empty() ? 0.0 : model_.own.at(0, 0)) - 500.0);
            cp_end_ = round_to_100(model_.own.empty() ? 0.0 : model_.own.at(model_.own.rows - 1, 0)) + 500.0;
            cp_interval_ = 25.0;
        }
        ImGui::EndPopup();
    }

    if (popups_.background_adjust) {
        ImGui::OpenPopup(tr("dialog.bgimage_adjust").c_str());
        popups_.background_adjust = false;
    }
    if (ImGui::BeginPopupModal(tr("dialog.bgimage_adjust").c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        if (ImGui::BeginTable("bg_adjust_params", 2, ImGuiTableFlags_SizingStretchProp)) {
            auto input_row = [](const char* id, const std::string& label, double& value, const char* format) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextUnformatted(label.c_str());
                ImGui::TableSetColumnIndex(1);
                ImGui::SetNextItemWidth(220.0f);
                ImGui::InputDouble(id, &value, 0.0, 0.0, format);
            };
            auto slider_row = [](const char* id, const std::string& label, double& value, double min_value, double max_value, const char* format) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextUnformatted(label.c_str());
                ImGui::TableSetColumnIndex(1);
                ImGui::SetNextItemWidth(220.0f);
                float slider_value = static_cast<float>(std::clamp(value, min_value, max_value));
                if (ImGui::SliderFloat(id, &slider_value, static_cast<float>(min_value), static_cast<float>(max_value), format,
                                       ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_NoInput)) {
                    value = slider_value;
                }
            };
            input_row("##bg_adjust_x", tr("label.bgimg_x"), pending_bg_x_, "%.3f");
            input_row("##bg_adjust_y", tr("label.bgimg_y"), pending_bg_y_, "%.3f");
            input_row("##bg_adjust_width", tr("label.bgimg_width"), pending_bg_width_, "%.3f");
            input_row("##bg_adjust_height", tr("label.bgimg_height"), pending_bg_height_, "%.3f");
            input_row("##bg_adjust_rotation", tr("label.bgimg_rotation"), pending_bg_rotation_deg_, "%.3f");
            slider_row("##bg_adjust_brightness", tr("label.bgimg_brightness"), pending_bg_brightness_, 1.0, 200.0, "%.0f%%");
            ImGui::EndTable();
        }
        ImGui::Separator();
        if (ImGui::Button(tr("button.ok").c_str())) {
            apply_pending_background_values(true);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button(tr("button.cancel").c_str())) {
            sync_pending_background_values();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    if (popups_.background_align) {
        ImGui::OpenPopup(tr("dialog.align_to_station").c_str());
        popups_.background_align = false;
    }
    if (ImGui::BeginPopupModal(tr("dialog.align_to_station").c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        auto combo_station = [&](const char* label, int& index) {
            index = std::clamp(index, 0, static_cast<int>(model_.stations.size()) - 1);
            std::string preview = model_.stations[index].key + ", " + model_.stations[index].name;
            if (ImGui::BeginCombo(label, preview.c_str())) {
                for (int i = 0; i < static_cast<int>(model_.stations.size()); ++i) {
                    std::string item = model_.stations[i].key + ", " + model_.stations[i].name;
                    if (ImGui::Selectable(item.c_str(), i == index)) index = i;
                }
                ImGui::EndCombo();
            }
        };
        if (has_model_ && model_.stations.size() >= 2) {
            combo_station("Station 1", align_station1_);
            std::string pick1_label = (align_pick1_ ? tr("button.pick_on_bg_ok") : tr("button.pick_on_bg")) + "##align_pick1";
            if (ImGui::Button(pick1_label.c_str())) {
                pick_slot_ = 1;
                ImGui::CloseCurrentPopup();
            }
            combo_station("Station 2", align_station2_);
            std::string pick2_label = (align_pick2_ ? tr("button.pick_on_bg_ok") : tr("button.pick_on_bg")) + "##align_pick2";
            if (ImGui::Button(pick2_label.c_str())) {
                pick_slot_ = 2;
                ImGui::CloseCurrentPopup();
            }
            if (ImGui::Button(tr("button.apply").c_str())) apply_background_alignment();
            ImGui::SameLine();
        }
        if (ImGui::Button(tr("button.ok").c_str())) {
            pick_slot_ = 0;
            apply_background_alignment();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    if (popups_.open_document_unsaved_confirm) {
        ImGui::OpenPopup(tr("dialog.open_document_unsaved_title").c_str());
        popups_.open_document_unsaved_confirm = false;
    }
    if (ImGui::BeginPopupModal(tr("dialog.open_document_unsaved_title").c_str(), nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 420.0f);
        ImGui::TextUnformatted(tr("dialog.open_document_unsaved_message").c_str());
        ImGui::PopTextWrapPos();
        ImGui::Separator();
        if (ImGui::Button(tr("button.open").c_str())) {
            if (pending_document_open_) {
                PendingDocumentOpen request = std::move(*pending_document_open_);
                pending_document_open_.reset();
                perform_open_document(std::move(request));
            }
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button(tr("button.cancel").c_str())) {
            pending_document_open_.reset();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    if (popups_.reload_unsaved_confirm) {
        ImGui::OpenPopup(tr("dialog.reload_unsaved_title").c_str());
        popups_.reload_unsaved_confirm = false;
    }
    if (ImGui::BeginPopupModal(tr("dialog.reload_unsaved_title").c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 420.0f);
        ImGui::TextUnformatted(tr("dialog.reload_unsaved_message").c_str());
        ImGui::PopTextWrapPos();
        ImGui::Separator();
        if (ImGui::Button(tr("button.ok").c_str())) {
            execute_pending_reload_action();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button(tr("button.cancel").c_str())) {
            pending_reload_action_ = PendingReloadAction::None;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    if (popups_.revert_all_edits_confirm) {
        ImGui::OpenPopup(tr("dialog.revert_all_edits_title").c_str());
        popups_.revert_all_edits_confirm = false;
    }
    if (ImGui::BeginPopupModal(tr("dialog.revert_all_edits_title").c_str(), nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 420.0f);
        ImGui::TextUnformatted(tr("dialog.revert_all_edits_message").c_str());
        ImGui::PopTextWrapPos();
        ImGui::Separator();
        if (ImGui::Button(tr("button.revert").c_str())) {
            request_edit_ui_operation(PendingEditUiOperation::Revert);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button(tr("button.cancel").c_str())) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    if (popups_.close_unsaved_confirm) {
        ImGui::OpenPopup(tr("dialog.unsaved_changes_title").c_str());
        popups_.close_unsaved_confirm = false;
    }
    ImGuiWindowFlags close_unsaved_flags = ImGuiWindowFlags_AlwaysAutoResize;
    if (pending_edit_ui_operation_.operation ==
            PendingEditUiOperation::SaveAndResolveClose ||
        pending_edit_ui_operation_.operation ==
            PendingEditUiOperation::DiscardAndResolveClose) {
        close_unsaved_flags |= ImGuiWindowFlags_NoInputs;
    }
    if (ImGui::BeginPopupModal(tr("dialog.unsaved_changes_title").c_str(), nullptr,
                               close_unsaved_flags)) {
        if (pending_close_action_ == PendingCloseAction::None) {
            ImGui::CloseCurrentPopup();
        } else {
            const bool exiting = pending_close_action_ == PendingCloseAction::ExitApplication;
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 520.0f);
            ImGui::TextUnformatted(tr(exiting
                ? "dialog.unsaved_exit_message"
                : "dialog.unsaved_close_edit_message").c_str());
            const bool list_drafts_pending = has_unapplied_editable_list_drafts();
            if (list_drafts_pending) {
                ImGui::Spacing();
                ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.25f, 1.0f), "%s",
                                   tr("dialog.apply_list_before_save").c_str());
            }
            ImGui::PopTextWrapPos();
            ImGui::Separator();
            if (ImGui::Button(tr("button.cancel").c_str())) {
                pending_close_action_ = PendingCloseAction::None;
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            ImGui::BeginDisabled(list_drafts_pending);
            if (ImGui::Button(tr(exiting
                    ? "button.save_changes_and_exit"
                    : "button.save_changes_and_close_edit").c_str())) {
                resolve_pending_close_action(true);
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            if (ImGui::Button(tr(exiting
                    ? "button.discard_changes_and_exit"
                    : "button.discard_changes_and_close_edit").c_str())) {
                resolve_pending_close_action(false);
            }
        }
        ImGui::EndPopup();
    }

    if (popups_.about) {
        ImGui::OpenPopup(tr("menu.about").c_str());
        popups_.about = false;
    }
    ImGui::SetNextWindowSize(ImVec2(560.0f, 0.0f), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal(tr("menu.about").c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 520.0f);
        ImGui::TextUnformatted(tr("about.text").c_str());
        ImGui::PopTextWrapPos();
        if (ImGui::Button(tr("button.ok").c_str())) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    render_scenario_route_pick_popup();
}

void App::render_scenario_route_pick_popup() {
    if (scenario_route_pick_.popup_requested) {
        ImGui::OpenPopup(tr("dialog.scenario_route_select_title").c_str());
        scenario_route_pick_.popup_requested = false;
    }
    ImGui::SetNextWindowSize(ImVec2(560.0f, 0.0f), ImGuiCond_Appearing);
    if (!ImGui::BeginPopupModal(tr("dialog.scenario_route_select_title").c_str(),
                                nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }

    // The child region has a fixed declared size, so long relative paths are
    // clipped inside the list instead of widening the dialog.
    const float row_height = ImGui::GetTextLineHeightWithSpacing();
    const float list_height = std::min(
        static_cast<float>(scenario_route_pick_.items.size()) * row_height +
            ImGui::GetStyle().FramePadding.y * 2.0f,
        260.0f);
    bool confirm_requested = false;
    ImGui::BeginChild("##ScenarioRouteList", ImVec2(520.0f, list_height),
                      ImGuiChildFlags_Borders);
    for (size_t i = 0; i < scenario_route_pick_.items.size(); ++i) {
        const ScenarioRoutePickItem& item = scenario_route_pick_.items[i];
        ImGui::PushID(static_cast<int>(i));
        if (ImGui::Selectable(item.route_text.c_str(),
                              scenario_route_pick_.selected ==
                                  static_cast<int>(i),
                              ImGuiSelectableFlags_AllowDoubleClick)) {
            scenario_route_pick_.selected = static_cast<int>(i);
            if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                confirm_requested = true;
            }
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s\n%s", item.route_text.c_str(),
                              item.resolved_path.c_str());
        }
        ImGui::PopID();
    }
    ImGui::EndChild();

    ImGui::Separator();
    if (ImGui::Button(tr("button.ok").c_str())) confirm_requested = true;
    ImGui::SameLine();
    if (ImGui::Button(tr("button.cancel").c_str())) {
        scenario_route_pick_ = ScenarioRoutePickState{};
        ImGui::CloseCurrentPopup();
    }
    if (confirm_requested && !scenario_route_pick_.items.empty()) {
        const ScenarioRoutePickItem chosen =
            scenario_route_pick_.items[static_cast<size_t>(
                scenario_route_pick_.selected)];
        const bool preserve_settings = scenario_route_pick_.preserve_settings;
        const bool record_history = scenario_route_pick_.record_history;
        const bool preserve_models = scenario_route_pick_.preserve_scene_preview_models;
        const bool preserve_camera = scenario_route_pick_.preserve_scene_preview_camera;
        std::optional<BackgroundHistory> background;
        if (scenario_route_pick_.background_to_restore) {
            background = *scenario_route_pick_.background_to_restore;
        }
        scenario_route_pick_ = ScenarioRoutePickState{};
        ImGui::CloseCurrentPopup();
        begin_map_load(chosen.resolved_path, preserve_settings, record_history,
                       std::move(background), preserve_models, preserve_camera);
    }
    ImGui::EndPopup();
}
