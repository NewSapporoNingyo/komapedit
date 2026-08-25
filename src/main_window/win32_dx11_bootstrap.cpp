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

App* g_app = nullptr;
HWND g_main_hwnd = nullptr;
constexpr UINT k_app_wake_message = WM_APP + 1;

void wake_main_window() {
    if (g_main_hwnd) PostMessageW(g_main_hwnd, k_app_wake_message, 0, 0);
}
ID3D11Device* g_pd3dDevice = nullptr;
ID3D11DeviceContext* g_pd3dDeviceContext = nullptr;
IDXGISwapChain* g_pSwapChain = nullptr;
bool g_SwapChainOccluded = false;
UINT g_ResizeWidth = 0, g_ResizeHeight = 0;
ID3D11RenderTargetView* g_mainRenderTargetView = nullptr;

bool CreateRenderTarget() {
    if (!g_pSwapChain || !g_pd3dDevice) return false;
    ID3D11Texture2D* back_buffer = nullptr;
    HRESULT hr = g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&back_buffer));
    if (FAILED(hr) || !back_buffer) {
        std::cerr << "CreateRenderTarget: failed to acquire the swap-chain buffer\n";
        return false;
    }
    hr = g_pd3dDevice->CreateRenderTargetView(
        back_buffer, nullptr, &g_mainRenderTargetView);
    back_buffer->Release();
    if (FAILED(hr) || !g_mainRenderTargetView) {
        g_mainRenderTargetView = nullptr;
        std::cerr << "CreateRenderTarget: failed to create the render-target view\n";
        return false;
    }
    return true;
}

void CleanupRenderTarget() {
    release_com(g_mainRenderTargetView);
}

bool CreateDeviceD3D(HWND hWnd) {
    DXGI_SWAP_CHAIN_DESC sd = {};
    sd.BufferCount = 2;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hWnd;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    UINT flags = 0;
    D3D_FEATURE_LEVEL feature_level;
    const D3D_FEATURE_LEVEL levels[2] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0};
    HRESULT hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, levels, 2,
                                               D3D11_SDK_VERSION, &sd, &g_pSwapChain, &g_pd3dDevice,
                                               &feature_level, &g_pd3dDeviceContext);
    if (hr == DXGI_ERROR_UNSUPPORTED) {
        hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, flags, levels, 2,
                                           D3D11_SDK_VERSION, &sd, &g_pSwapChain, &g_pd3dDevice,
                                           &feature_level, &g_pd3dDeviceContext);
    }
    if (FAILED(hr)) return false;
    IDXGIFactory* factory = nullptr;
    if (SUCCEEDED(g_pSwapChain->GetParent(IID_PPV_ARGS(&factory)))) {
        factory->MakeWindowAssociation(hWnd, DXGI_MWA_NO_ALT_ENTER);
        factory->Release();
    }
    return CreateRenderTarget();
}

void CleanupDeviceD3D() {
    CleanupRenderTarget();
    release_com(g_pSwapChain);
    release_com(g_pd3dDeviceContext);
    release_com(g_pd3dDevice);
}

LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (::ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam)) return true;
    if (touch_input::handle_message(hWnd, msg, wParam, lParam)) return 0;
    switch (msg) {
        case k_app_wake_message:
            return 0;
        case WM_SIZE:
            if (wParam == SIZE_MINIMIZED) return 0;
            g_ResizeWidth = LOWORD(lParam);
            g_ResizeHeight = HIWORD(lParam);
            return 0;
        case WM_SYSCOMMAND:
            if ((wParam & 0xfff0) == SC_KEYMENU) return 0;
            break;
        case WM_CLOSE:
            if (g_app) {
                g_app->request_exit();
                return 0;
            }
            break;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

int main(int, char**) {
#ifndef NDEBUG
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    std::vector<std::string> args = command_line_args_utf8();
#if defined(_MSC_VER)
    const bool debug_headless_requested = std::any_of(
        args.begin(), args.end(), [](const std::string& arg) {
            return arg == "--headless-load-map" ||
                arg == "--headless-load-scenario" ||
                arg.rfind("--debug-headless-", 0) == 0;
        });
    if (debug_headless_requested) {
        _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
        _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
    }
#endif
    HeadlessSceneLoaderContractOptions scene_loader_contract =
        parse_headless_scene_loader_contract_options(args);
    if (scene_loader_contract.requested) {
        if (!scene_loader_contract.error.empty()) {
            std::cerr << scene_loader_contract.error << "\n"
                      << "usage: komapedit.exe --debug-headless-scene-loader-contract "
                         "[--headless-output FILE]\n";
            return 1;
        }
        return run_debug_headless_scene_loader_contract(scene_loader_contract);
    }

    HeadlessDiagnosticsPopupBenchOptions diagnostics_popup_bench =
        parse_headless_diagnostics_popup_bench_options(args);
    if (diagnostics_popup_bench.requested) {
        if (!diagnostics_popup_bench.error.empty()) {
            std::cerr << diagnostics_popup_bench.error << "\n"
                      << "usage: komapedit.exe --debug-headless-diagnostics-popup-bench "
                         "[--headless-output FILE]\n";
            return 1;
        }
        return App::run_debug_headless_diagnostics_popup_benchmark(
            diagnostics_popup_bench);
    }

    HeadlessTableFindOptions table_find = parse_headless_table_find_options(args);
    if (table_find.requested) {
        if (!table_find.error.empty()) {
            std::cerr << table_find.error << "\n"
                      << "usage: komapedit.exe --debug-headless-table-find [--headless-output FILE]\n";
            return 1;
        }
        return App::run_debug_headless_table_find(table_find.output_path);
    }

    HeadlessSettingsPersistenceOptions settings_persistence =
        parse_headless_settings_persistence_options(args);
    if (settings_persistence.requested) {
        if (!settings_persistence.error.empty()) {
            std::cerr << settings_persistence.error << "\n"
                      << "usage: komapedit.exe --debug-headless-settings-persistence "
                         "[--headless-output FILE]\n";
            return 1;
        }
        return run_debug_headless_settings_persistence(settings_persistence);
    }

    HeadlessTouchInputOptions touch_input_options = parse_headless_touch_input_options(args);
    if (touch_input_options.requested) {
        if (!touch_input_options.error.empty()) {
            std::cerr << touch_input_options.error << "\n"
                      << "usage: komapedit.exe --debug-headless-touch-input [--headless-output FILE]\n";
            return 1;
        }
        return run_debug_headless_touch_input(touch_input_options);
    }

    HeadlessScene3DBenchmarkOptions scene3d_bench = parse_headless_scene3d_benchmark_options(args);
    if (scene3d_bench.requested) {
        if (!scene3d_bench.error.empty()) {
            std::cerr << scene3d_bench.error << "\n"
                      << "usage: komapedit.exe --debug-headless-scene3d-bench <map-path> "
                      << "[--frames N] [--unit-distance M] [--max-frame-ms MS] "
                      << "[--window-back-m M] [--window-forward-m M] "
                      << "[--scene-model-workers N] [--disable-scene-texture-cache] "
                      << "[--headless-output FILE]\n";
            return 1;
        }
        return App::run_debug_headless_scene3d_benchmark(scene3d_bench.path, scene3d_bench.frames,
                                                         scene3d_bench.unit_distance, scene3d_bench.max_frame_ms,
                                                         scene3d_bench.window_back_m,
                                                         scene3d_bench.window_forward_m,
                                                         scene3d_bench.scene_model_workers,
                                                         scene3d_bench.disable_scene_texture_cache,
                                                         scene3d_bench.output_path);
    }

    HeadlessSceneCameraTransferOptions scene_camera_transfer = parse_headless_scene_camera_transfer_options(args);
    if (scene_camera_transfer.requested) {
        if (!scene_camera_transfer.error.empty()) {
            std::cerr << scene_camera_transfer.error << "\n"
                      << "usage: komapedit.exe --debug-headless-scene-camera-transfer <map-path> "
                      << "[--unit-distance M] [--camera-distance M] [--headless-output FILE]\n";
            return 1;
        }
        return App::run_debug_headless_scene_camera_transfer(scene_camera_transfer.path,
                                                             scene_camera_transfer.unit_distance,
                                                             scene_camera_transfer.has_camera_distance,
                                                             scene_camera_transfer.camera_distance,
                                                             scene_camera_transfer.output_path);
    }

    HeadlessSourceAnchorOptions source_anchors = parse_headless_source_anchor_options(args);
    if (source_anchors.requested) {
        if (!source_anchors.error.empty()) {
            std::cerr << source_anchors.error << "\n"
                      << "usage: komapedit.exe --debug-headless-source-anchors <map-path> "
                      << "[--unit-distance M] [--headless-output FILE]\n";
            return 1;
        }
        return App::run_debug_headless_source_anchors(source_anchors.path,
                                                      source_anchors.unit_distance,
                                                      source_anchors.output_path);
    }

    HeadlessStationListEditOptions station_list_edit =
        parse_headless_station_list_edit_options(args);
    if (station_list_edit.requested) {
        if (!station_list_edit.error.empty()) {
            std::cerr << station_list_edit.error << "\n"
                      << "usage: komapedit.exe --debug-headless-station-list-edit <map-path> "
                      << "[--unit-distance M] [--commit] [--headless-output FILE]\n";
            return 2;
        }
        return run_debug_headless_station_list_edit(station_list_edit);
    }

    HeadlessDistanceEditBatchOptions distance_edit_batch =
        parse_headless_distance_edit_batch_options(args);
    if (distance_edit_batch.requested) {
        if (!distance_edit_batch.error.empty()) {
            std::cerr << distance_edit_batch.error << "\n"
                      << "usage: komapedit.exe --debug-headless-distance-edit-batch [map-path] "
                      << "[--unit-distance M] [--commit] [--headless-output FILE]\n";
            return 2;
        }
        return run_debug_headless_distance_edit_batch(distance_edit_batch);
    }

    HeadlessRepeaterEditBatchOptions repeater_edit_batch =
        parse_headless_repeater_edit_batch_options(args);
    if (repeater_edit_batch.requested) {
        if (!repeater_edit_batch.error.empty()) {
            std::cerr << repeater_edit_batch.error << "\n"
                      << "usage: komapedit.exe --debug-headless-repeater-edit-batch [map-path] "
                      << "[--unit-distance M] [--commit] [--headless-output FILE]\n";
            return 2;
        }
        return run_debug_headless_repeater_edit_batch(repeater_edit_batch);
    }

    HeadlessRepeaterKeyEditOptions repeater_key_edit =
        parse_headless_repeater_key_edit_options(args);
    if (repeater_key_edit.requested) {
        if (!repeater_key_edit.error.empty()) {
            std::cerr << repeater_key_edit.error << "\n"
                      << "usage: komapedit.exe --debug-headless-repeater-key-edit <map-path> "
                      << "[--unit-distance M] [--commit] [--headless-output FILE]\n";
            return 2;
        }
        return run_debug_headless_repeater_key_edit(repeater_key_edit);
    }

    HeadlessOtherTrackKeyEditOptions other_track_key_edit =
        parse_headless_other_track_key_edit_options(args);
    if (other_track_key_edit.requested) {
        if (!other_track_key_edit.error.empty()) {
            std::cerr << other_track_key_edit.error << "\n"
                      << "usage: komapedit.exe --debug-headless-other-track-key-edit "
                         "<map-path> [--unit-distance M] [--commit] "
                         "[--headless-output FILE]\n";
            return 2;
        }
        return App::run_debug_headless_other_track_key_edit(other_track_key_edit);
    }

    HeadlessSectionEditBatchOptions section_edit_batch =
        parse_headless_section_edit_batch_options(args);
    if (section_edit_batch.requested) {
        if (!section_edit_batch.error.empty()) {
            std::cerr << section_edit_batch.error << "\n"
                      << "usage: komapedit.exe --debug-headless-section-edit-batch [map-path] "
                      << "[--unit-distance M] [--commit] [--headless-output FILE]\n";
            return 2;
        }
        return run_debug_headless_section_edit_batch(section_edit_batch);
    }

    HeadlessNewElementEditOptions new_element_edit =
        parse_headless_new_element_edit_options(args);
    if (new_element_edit.requested) {
        if (!new_element_edit.error.empty()) {
            std::cerr << new_element_edit.error << "\n"
                      << "usage: komapedit.exe --debug-headless-new-element-edit <map-path> "
                      << "[--unit-distance M] [--commit] [--headless-output FILE]\n";
            return 2;
        }
        return App::run_debug_headless_new_element_edit(new_element_edit);
    }

    HeadlessInsertEditOptions insert_edit = parse_headless_insert_edit_options(args);
    if (insert_edit.requested) {
        if (!insert_edit.error.empty()) {
            std::cerr << insert_edit.error << "\n"
                      << "usage: komapedit.exe --debug-headless-insert-edit [map-path] "
                      << "[--repeater-only] [--unit-distance M] [--commit] "
                      << "[--headless-output FILE]\n";
            return 2;
        }
        return run_debug_headless_insert_edit(insert_edit);
    }

    HeadlessIncludeDeleteOptions include_delete =
        parse_headless_include_delete_options(args);
    if (include_delete.requested) {
        if (!include_delete.error.empty()) {
            std::cerr << include_delete.error << "\n"
                      << "usage: komapedit.exe --debug-headless-include-delete <map-path> "
                      << "[--index N] [--unit-distance M] [--commit] "
                      << "[--headless-output FILE]\n";
            return 2;
        }
        return run_debug_headless_include_delete(include_delete);
    }

    HeadlessIncludeReplaceOptions include_replace =
        parse_headless_include_replace_options(args);
    if (include_replace.requested) {
        if (!include_replace.error.empty()) {
            std::cerr << include_replace.error << "\n"
                      << "usage: komapedit.exe --debug-headless-include-replace <map-path> "
                      << "--new-path <file> [--index N] [--unit-distance M] "
                      << "[--commit] [--headless-output FILE]\n";
            return 2;
        }
        return run_debug_headless_include_replace(include_replace);
    }

    HeadlessResourceListReplaceOptions resource_list_replace =
        parse_headless_resource_list_replace_options(args);
    if (resource_list_replace.requested) {
        if (!resource_list_replace.error.empty()) {
            std::cerr << resource_list_replace.error << "\n"
                      << "usage: komapedit.exe --debug-headless-resource-list-replace <map-path> "
                      << "[--unit-distance M] [--headless-output FILE]\n";
            return 2;
        }
        return App::run_debug_headless_resource_list_replace(resource_list_replace);
    }

    HeadlessResourceListInsertOptions resource_list_insert =
        parse_headless_resource_list_insert_options(args);
    if (resource_list_insert.requested) {
        if (!resource_list_insert.error.empty()) {
            std::cerr << resource_list_insert.error << "\n"
                      << "usage: komapedit.exe --debug-headless-resource-list-insert <map-path> "
                         "--kind structure|signal [--unit-distance M] "
                         "[--headless-output FILE]\n";
            return 2;
        }
        return App::run_debug_headless_resource_list_insert(resource_list_insert);
    }

    HeadlessNewFileWizardOptions new_file_wizard =
        parse_headless_new_file_wizard_options(args);
    if (new_file_wizard.requested) {
        if (!new_file_wizard.error.empty()) {
            std::cerr << new_file_wizard.error << "\n"
                      << "usage: komapedit.exe --debug-headless-new-file-wizard <new-map-path-under-tests> "
                      << "[--unit-distance M] [--headless-output FILE]\n";
            return 2;
        }
        return App::run_debug_headless_new_file_wizard(new_file_wizard);
    }

    HeadlessFreshResourceListWorkflowOptions fresh_resource_list =
        parse_headless_fresh_resource_list_workflow_options(args);
    if (fresh_resource_list.requested) {
        if (!fresh_resource_list.error.empty()) {
            std::cerr << fresh_resource_list.error << "\n"
                      << "usage: komapedit.exe --debug-headless-fresh-resource-list-workflow <map-path> "
                         "[--unit-distance M] [--headless-output FILE]\n";
            return 2;
        }
        return App::run_debug_headless_fresh_resource_list_workflow(fresh_resource_list);
    }

    HeadlessIncludeImportCreateOptions include_import_create =
        parse_headless_include_import_create_options(args);
    if (include_import_create.requested) {
        if (!include_import_create.error.empty()) {
            std::cerr << include_import_create.error << "\n"
                      << "usage: komapedit.exe --debug-headless-include-import-create <map-path> "
                      << "[--unit-distance M] [--headless-output FILE]\n";
            return 2;
        }
        return run_debug_headless_include_import_create(include_import_create);
    }

    HeadlessEditRoundtripOptions edit_roundtrip = parse_headless_edit_roundtrip_options(args);
    if (edit_roundtrip.requested) {
        if (!edit_roundtrip.error.empty()) {
            std::cerr << edit_roundtrip.error << "\n"
                      << "usage: komapedit.exe --debug-headless-edit-roundtrip <map-path> "
                      << "[--unit-distance M] [--headless-output FILE]\n";
            return 2;
        }
        return App::run_debug_headless_edit_roundtrip(edit_roundtrip.path,
                                                      edit_roundtrip.unit_distance,
                                                      edit_roundtrip.output_path);
    }

    HeadlessOwnTrackEditOptions own_track_edit =
        parse_headless_own_track_edit_options(args);
    if (own_track_edit.requested) {
        if (!own_track_edit.error.empty()) {
            std::cerr << own_track_edit.error << "\n"
                      << "usage: komapedit.exe --debug-headless-own-track-edit [map-path] "
                      << "[--unit-distance M] [--headless-output FILE]\n";
            return 2;
        }
        return App::run_debug_headless_own_track_edit(own_track_edit);
    }

    HeadlessOtherTrackEditOptions other_track_edit =
        parse_headless_other_track_edit_options(args);
    if (other_track_edit.requested) {
        if (!other_track_edit.error.empty()) {
            std::cerr << other_track_edit.error << "\n"
                      << "usage: komapedit.exe --debug-headless-other-track-edit [map-path] "
                      << "[--unit-distance M] [--commit] [--headless-output FILE]\n";
            return 2;
        }
        return App::run_debug_headless_other_track_edit(other_track_edit);
    }

    HeadlessOpenBenchmarkOptions open_bench = parse_headless_open_benchmark_options(args);
    if (open_bench.requested) {
        if (!open_bench.error.empty()) {
            std::cerr << open_bench.error << "\n"
                      << "usage: komapedit.exe --debug-headless-open-bench <map-path> "
                      << "[--repeat N] [--unit-distance M] [--headless-output FILE]\n";
            return 1;
        }
        return App::run_debug_headless_open_benchmark(open_bench);
    }

    HeadlessPlanBenchmarkOptions plan_bench = parse_headless_plan_benchmark_options(args);
    if (plan_bench.requested) {
        if (!plan_bench.error.empty()) {
            std::cerr << plan_bench.error << "\n"
                      << "usage: komapedit.exe --debug-headless-plan-bench <map-path> "
                      << "[--frames N] [--unit-distance M] [--pan-pixels P] "
                      << "[--interaction pan|measure-stationary|measure-moving] "
                      << "[--max-frame-ms MS] [--headless-output FILE] [--profile-stages]\n";
            return 1;
        }
        return App::run_debug_headless_plan_benchmark(plan_bench.path, plan_bench.frames,
                                                      plan_bench.unit_distance, plan_bench.pan_pixels,
                                                      plan_bench.max_frame_ms, plan_bench.output_path,
                                                      plan_bench.profile_stages,
                                                      plan_bench.interaction);
    }

    HeadlessLoadOptions headless = parse_headless_load_options(args);
    if (headless.requested) {
        if (!headless.error.empty()) {
            std::cerr << headless.error << "\n"
                      << "usage: komapedit.exe --headless-load-map <map-path> "
                      << "[--repeat N] [--unit-distance M] [--load-profile preview|edit] "
                      << "[--headless-output FILE]\n";
            return 1;
        }
        return run_headless_load_map(headless);
    }

    HeadlessLoadScenarioOptions headless_scenario =
        parse_headless_load_scenario_options(args);
    if (headless_scenario.requested) {
        if (!headless_scenario.error.empty()) {
            std::cerr << headless_scenario.error << "\n"
                      << "usage: komapedit.exe --headless-load-scenario <scenario-path> "
                      << "[--scenario-index N] [--unit-distance M] "
                      << "[--load-profile preview|edit] [--headless-output FILE]\n";
            return 1;
        }
        return run_headless_load_scenario(headless_scenario);
    }
#endif

    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    UserSettings settings = load_user_settings();
    ImGui_ImplWin32_EnableDpiAwareness();
    float scale = ImGui_ImplWin32_GetDpiScaleForMonitor(MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY));

    HINSTANCE instance = GetModuleHandleW(nullptr);
    HICON app_icon = static_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(IDI_KOMAPEDIT), IMAGE_ICON,
                                                   GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), 0));
    HICON app_icon_small = static_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(IDI_KOMAPEDIT), IMAGE_ICON,
                                                         GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0));

    WNDCLASSEXW wc = {sizeof(wc), CS_CLASSDC, WndProc, 0L, 0L, instance, app_icon, nullptr, nullptr, nullptr, L"komapedit", app_icon_small};
    RegisterClassExW(&wc);
    HWND hwnd = CreateWindowW(wc.lpszClassName, L"komapedit", WS_OVERLAPPEDWINDOW,
                              100, 100, static_cast<int>(1440 * scale), static_cast<int>(900 * scale),
                              nullptr, nullptr, wc.hInstance, nullptr);
    if (!hwnd) {
        UnregisterClassW(wc.lpszClassName, wc.hInstance);
        if (app_icon) DestroyIcon(app_icon);
        if (app_icon_small) DestroyIcon(app_icon_small);
        CoUninitialize();
        return 1;
    }
    if (app_icon) SendMessageW(hwnd, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(app_icon));
    if (app_icon_small) SendMessageW(hwnd, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(app_icon_small));
    g_main_hwnd = hwnd;
    if (!CreateDeviceD3D(hwnd)) {
        CleanupDeviceD3D();
        DestroyWindow(hwnd);
        UnregisterClassW(wc.lpszClassName, wc.hInstance);
        if (app_icon) DestroyIcon(app_icon);
        if (app_icon_small) DestroyIcon(app_icon_small);
        g_main_hwnd = nullptr;
        CoUninitialize();
        return 1;
    }

    ShowWindow(hwnd, SW_SHOWDEFAULT);
    UpdateWindow(hwnd);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImPlot::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
    io.IniFilename = nullptr;
    io.IniSavingRate = 0.25f;
    std::filesystem::path layout_path = default_imgui_ini_path();
    bool has_saved_layout = load_imgui_layout(layout_path);

    bool viewports_enabled = (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) != 0;
    apply_ui_settings(settings.font_size, settings.ui_component_size, settings.theme_color, scale, viewports_enabled);
    io.ConfigDpiScaleFonts = true;
    io.ConfigDpiScaleViewports = true;

    const char* font_candidates[] = {
        "C:/Windows/Fonts/msyh.ttc",
        "C:/Windows/Fonts/YuGothM.ttc",
        "C:/Windows/Fonts/meiryo.ttc",
        "C:/Windows/Fonts/segoeui.ttf"
    };
    bool font_loaded = false;
    for (const char* font : font_candidates) {
        if (std::filesystem::exists(font)) {
            io.Fonts->AddFontFromFileTTF(font, k_default_font_size * scale, nullptr,
                                         application_font_glyph_ranges(*io.Fonts));
            font_loaded = true;
            break;
        }
    }
    if (!font_loaded) io.Fonts->AddFontDefault();
    merge_required_symbol_glyphs(*io.Fonts, k_default_font_size * scale);

    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);

    App app(g_pd3dDevice, std::move(settings), scale, viewports_enabled, has_saved_layout);

    bool done = false;
    bool needs_render = true;
    int warmup_frames = 2;
    while (!done) {
        bool received_message = false;
        MSG msg;
        while (PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE)) {
            received_message = true;
            TranslateMessage(&msg);
            DispatchMessage(&msg);
            if (msg.message == WM_QUIT) done = true;
        }
        if (done) break;

        if (!needs_render && !received_message) {
            MsgWaitForMultipleObjectsEx(0, nullptr, INFINITE, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
            continue;
        }
        needs_render = false;

        if (g_SwapChainOccluded && g_pSwapChain->Present(0, DXGI_PRESENT_TEST) == DXGI_STATUS_OCCLUDED) {
            continue;
        }
        g_SwapChainOccluded = false;

        if (g_ResizeWidth != 0 && g_ResizeHeight != 0) {
            CleanupRenderTarget();
            const HRESULT resize_hr = g_pSwapChain->ResizeBuffers(
                0, g_ResizeWidth, g_ResizeHeight, DXGI_FORMAT_UNKNOWN, 0);
            if (FAILED(resize_hr)) {
                std::cerr << "ResizeBuffers: failed to resize the main swap chain\n";
            } else {
                g_ResizeWidth = g_ResizeHeight = 0;
            }
        }
        if (!g_mainRenderTargetView && !CreateRenderTarget()) {
            continue;
        }

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();
        app.render();

        ImGui::Render();
        const float clear_color[4] = {0.06f, 0.07f, 0.08f, 1.0f};
        g_pd3dDeviceContext->OMSetRenderTargets(
            1, &g_mainRenderTargetView, nullptr);
        g_pd3dDeviceContext->ClearRenderTargetView(
            g_mainRenderTargetView, clear_color);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

        if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
            ImGui::UpdatePlatformWindows();
            ImGui::RenderPlatformWindowsDefault();
        }

        HRESULT hr = g_pSwapChain->Present(1, 0);
        g_SwapChainOccluded = (hr == DXGI_STATUS_OCCLUDED);
        if (!g_SwapChainOccluded && app.on_frame_presented()) needs_render = true;
        if (warmup_frames > 0) {
            --warmup_frames;
            needs_render = true;
        }
        save_imgui_layout_if_requested(layout_path);
        if (GImGui && GImGui->InputEventsQueue.Size > 0) needs_render = true;
        if (touch_input::wants_continuous_render()) needs_render = true;
        if (imgui_layout_save_pending()) needs_render = true;
    }

    save_imgui_layout(layout_path);

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImPlot::DestroyContext();
    ImGui::DestroyContext();

    CleanupDeviceD3D();
    g_main_hwnd = nullptr;
    DestroyWindow(hwnd);
    UnregisterClassW(wc.lpszClassName, wc.hInstance);
    if (app_icon) DestroyIcon(app_icon);
    if (app_icon_small) DestroyIcon(app_icon_small);
    CoUninitialize();
    return 0;
}
