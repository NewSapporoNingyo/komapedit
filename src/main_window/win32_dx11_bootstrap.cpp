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
#include "runtime_paths.h"
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
#include <system_error>
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

#ifndef NDEBUG
static std::optional<int> run_headless_commands(const std::vector<std::string>& args) {
    std::optional<int> status;
    const auto dispatch = [&](const char* command, auto parse, auto run, int usage_status,
                              const char* usage, bool (*matches)(std::string_view) = nullptr) {
        if (status || std::none_of(args.begin(), args.end(), [&](const std::string& arg) {
                return matches ? matches(arg) : arg == command;
            })) return;
        const auto options = parse(args);
        if (!options.requested) return;
        if (!options.error.empty()) {
            std::cerr << options.error << '\n';
            if (usage) std::cerr << "usage: komapedit.exe " << command << usage << '\n';
            status = usage_status;
            return;
        }
        status = run(options);
    };

    dispatch("--debug-headless-scene-loader-contract", parse_headless_scene_loader_contract_options,
             run_debug_headless_scene_loader_contract, 1,
             " [--headless-output FILE]");
    dispatch("--debug-headless-diagnostics-popup-bench", parse_headless_diagnostics_popup_bench_options,
             App::run_debug_headless_diagnostics_popup_benchmark, 1,
             " [--headless-output FILE]");
    dispatch("--debug-headless-table-find", parse_headless_table_find_options,
             [](const HeadlessTableFindOptions& options) {
                 return App::run_debug_headless_table_find(
                     options.output_path);
             }, 1, " [--headless-output FILE]");
    dispatch("--debug-headless-table-cache-bench", parse_headless_table_cache_benchmark_options,
             App::run_debug_headless_table_cache_benchmark, 1,
             " <map-path> [--repeat N] [--unit-distance M] [--headless-output FILE]");
    dispatch("--debug-headless-settings-persistence", parse_headless_settings_persistence_options,
             run_debug_headless_settings_persistence, 1,
             " [--headless-output FILE]");
    dispatch("--debug-headless-touch-input", parse_headless_touch_input_options,
             run_debug_headless_touch_input, 1,
             " [--headless-output FILE]");
    dispatch("--debug-headless-scene3d-bench", parse_headless_scene3d_benchmark_options,
             [](const HeadlessScene3DBenchmarkOptions& options) {
                 return App::run_debug_headless_scene3d_benchmark(
                     options.path, options.frames, options.unit_distance, options.max_frame_ms,
                     options.window_back_m, options.window_forward_m, options.scene_model_workers,
                     options.disable_scene_texture_cache, options.profile_stages, options.interaction,
                     options.output_path);
             }, 1, " <map-path> [--frames N] [--unit-distance M] [--max-frame-ms MS] [--window-back-m M] [--window-forward-m M] [--scene-model-workers N] [--disable-scene-texture-cache] [--profile-stages] [--interaction stationary|moving] [--headless-output FILE]");
    dispatch("--debug-headless-scene-camera-transfer", parse_headless_scene_camera_transfer_options,
             [](const HeadlessSceneCameraTransferOptions& options) {
                 return App::run_debug_headless_scene_camera_transfer(
                     options.path, options.unit_distance, options.has_camera_distance,
                     options.camera_distance, options.output_path);
             }, 1, " <map-path> [--unit-distance M] [--camera-distance M] [--headless-output FILE]");
    dispatch("--debug-headless-source-anchors", parse_headless_source_anchor_options,
             [](const HeadlessSourceAnchorOptions& options) {
                 return App::run_debug_headless_source_anchors(
                     options.path, options.unit_distance, options.output_path);
             }, 1, " <map-path> [--unit-distance M] [--headless-output FILE]");
    dispatch("--debug-headless-station-list-edit", parse_headless_station_list_edit_options,
             run_debug_headless_station_list_edit, 2,
             " <map-path> [--unit-distance M] [--commit] [--headless-output FILE]");
    dispatch("--debug-headless-distance-edit-batch", parse_headless_distance_edit_batch_options,
             run_debug_headless_distance_edit_batch, 2,
             " <map-path> [--unit-distance M] [--commit] [--headless-output FILE]");
    dispatch("--debug-headless-auto-insert-diagnostics", parse_headless_auto_insert_diagnostics_options,
             App::run_debug_headless_auto_insert_diagnostics, 2,
             " <fixture-directory> [--unit-distance M] [--headless-output FILE]");
    dispatch("--debug-headless-repeater-edit-batch", parse_headless_repeater_edit_batch_options,
             run_debug_headless_repeater_edit_batch, 2,
             " <map-path> [--unit-distance M] [--commit] [--headless-output FILE]");
    dispatch("--debug-headless-repeater-key-edit", parse_headless_repeater_key_edit_options,
             run_debug_headless_repeater_key_edit, 2,
             " <map-path> [--unit-distance M] [--commit] [--headless-output FILE]");
    dispatch("--debug-headless-other-track-key-edit", parse_headless_other_track_key_edit_options,
             App::run_debug_headless_other_track_key_edit, 2,
             " <map-path> [--unit-distance M] [--commit] [--headless-output FILE]");
    dispatch("--debug-headless-section-edit-batch", parse_headless_section_edit_batch_options,
             run_debug_headless_section_edit_batch, 2,
             " <map-path> [--unit-distance M] [--commit] [--headless-output FILE]");
    dispatch("--debug-headless-new-element-edit", parse_headless_new_element_edit_options,
             App::run_debug_headless_new_element_edit, 2,
             " <map-path> [--unit-distance M] [--commit] [--headless-output FILE]");
    dispatch("--debug-headless-legacy-fog-edit", parse_headless_legacy_fog_edit_options,
             App::run_debug_headless_legacy_fog_edit, 1,
             " <map-path> [--unit-distance N] [--headless-output <file>]");
    dispatch("--debug-headless-pretrain-edit", parse_headless_pretrain_edit_options,
             App::run_debug_headless_pretrain_edit, 2,
             " <map-path> [--unit-distance M] [--headless-output FILE]");
    dispatch("--debug-headless-light-edit", parse_headless_light_edit_options,
             App::run_debug_headless_light_edit, 2,
             " <map-path> [--unit-distance M] [--headless-output FILE]");
    dispatch("--debug-headless-curve-parameter-edit", parse_headless_curve_parameter_edit_options,
             App::run_debug_headless_curve_parameter_edit, 2,
             " <map-path> [--unit-distance M] [--headless-output FILE]");
    dispatch("--debug-headless-station-put-margin-edit", parse_headless_station_put_margin_edit_options,
             App::run_debug_headless_station_put_margin_edit, 2,
             " <map-path> [--unit-distance M] [--headless-output FILE]");
    dispatch("--debug-headless-sparse-new-element", parse_headless_sparse_new_element_options,
             App::run_debug_headless_sparse_new_element, 2,
             " <map-path> [--unit-distance M] [--headless-output FILE]");
    dispatch("--debug-headless-insert-edit", parse_headless_insert_edit_options,
             run_debug_headless_insert_edit, 2,
             " <map-path> [--repeater-only] [--unit-distance M] [--commit] [--headless-output FILE]");
    dispatch("--debug-headless-include-delete", parse_headless_include_delete_options,
             run_debug_headless_include_delete, 2,
             " <map-path> [--index N] [--unit-distance M] [--commit] [--headless-output FILE]");
    dispatch("--debug-headless-include-replace", parse_headless_include_replace_options,
             run_debug_headless_include_replace, 2,
             " <map-path> --new-path <file> [--index N] [--unit-distance M] [--commit] [--headless-output FILE]");
    dispatch("--debug-headless-resource-list-replace", parse_headless_resource_list_replace_options,
             App::run_debug_headless_resource_list_replace, 2,
             " <map-path> [--unit-distance M] [--headless-output FILE]");
    dispatch("--debug-headless-resource-list-insert", parse_headless_resource_list_insert_options,
             App::run_debug_headless_resource_list_insert, 2,
             " <map-path> --kind structure|signal [--unit-distance M] [--headless-output FILE]");
    dispatch("--debug-headless-new-file-wizard", parse_headless_new_file_wizard_options,
             App::run_debug_headless_new_file_wizard, 2,
             " <new-map-path-under-tests> [--unit-distance M] [--headless-output FILE]");
    dispatch("--debug-headless-new-project", parse_headless_new_project_options,
             App::run_debug_headless_new_project, 2,
             " <existing-directory> [--headless-output FILE]");
    dispatch("--debug-headless-scenario-lifecycle", parse_headless_scenario_lifecycle_options,
             App::run_debug_headless_scenario_lifecycle, 2,
             " <scenario-path> [--scenario-index N] [--unit-distance M] [--headless-output FILE]");
    dispatch("--debug-headless-creator-message", parse_headless_creator_message_options,
             App::run_debug_headless_creator_message, 2,
             " <map-or-scenario-path> [--scenario-index N] [--unit-distance M] [--headless-output FILE]");
    dispatch("--debug-headless-scenario-create", parse_headless_scenario_create_options,
             App::run_debug_headless_scenario_create, 2,
             " <new-scenario-path-under-tests> --route <existing-map-path> [--headless-output FILE]");
    dispatch("--debug-headless-fresh-resource-list-workflow", parse_headless_fresh_resource_list_workflow_options,
             App::run_debug_headless_fresh_resource_list_workflow, 2,
             " <map-path> [--unit-distance M] [--headless-output FILE]");
    dispatch("--debug-headless-signal-aspect-columns", parse_headless_signal_aspect_columns_options,
             App::run_debug_headless_signal_aspect_columns, 2,
             " <map-or-scenario-path> [--unit-distance M] [--headless-output FILE]");
    dispatch("--debug-headless-include-import-create", parse_headless_include_import_create_options,
             run_debug_headless_include_import_create, 2,
             " <map-path> [--unit-distance M] [--headless-output FILE]");
    dispatch("--debug-headless-edit-bench", parse_headless_edit_benchmark_options,
             App::run_debug_headless_edit_benchmark, 2,
             nullptr);
    dispatch("--debug-headless-edit-roundtrip", parse_headless_edit_roundtrip_options,
             [](const HeadlessEditRoundtripOptions& options) {
                 return App::run_debug_headless_edit_roundtrip(
                     options.path, options.unit_distance, options.output_path);
             }, 2, " <map-path> [--unit-distance M] [--headless-output FILE]");
    dispatch("--debug-headless-own-track-edit", parse_headless_own_track_edit_options,
             App::run_debug_headless_own_track_edit, 2,
             " <map-path> [--unit-distance M] [--headless-output FILE]");
    dispatch("--debug-headless-other-track-edit", parse_headless_other_track_edit_options,
             App::run_debug_headless_other_track_edit, 2,
             " <map-path> [--unit-distance M] [--commit] [--headless-output FILE]");
    dispatch("--debug-headless-open-bench", parse_headless_open_benchmark_options,
             App::run_debug_headless_open_benchmark, 1,
             " <map-path> [--repeat N] [--unit-distance M] [--headless-output FILE]");
    dispatch("--debug-headless-plan-bench", parse_headless_plan_benchmark_options,
             [](const HeadlessPlanBenchmarkOptions& options) {
                 return App::run_debug_headless_plan_benchmark(
                     options.path, options.frames, options.unit_distance, options.pan_pixels,
                     options.max_frame_ms, options.output_path, options.profile_stages,
                     options.interaction);
             }, 1, " <map-path> [--frames N] [--unit-distance M] [--pan-pixels P] [--interaction pan|measure-stationary|measure-moving|edit-stationary|edit-drag|edit-wheel] [--max-frame-ms MS] [--headless-output FILE] [--profile-stages]");
    dispatch("--headless-load-map", parse_headless_load_options,
             run_headless_load_map, 1,
             " <map-path> [--repeat N] [--unit-distance M] [--load-profile preview|edit] [--headless-output FILE]",
             is_headless_load_map_command);
    dispatch("--headless-load-scenario", parse_headless_load_scenario_options,
             run_headless_load_scenario, 1,
             " <scenario-path> [--scenario-index N] [--unit-distance M] [--expect-no-map] [--scenario-edit-roundtrip] [--load-profile preview|edit] [--headless-output FILE]");
    return status;
}
#endif

int main(int, char**) {
#ifndef NDEBUG
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    std::vector<std::string> args = command_line_args_utf8();
    const bool debug_headless_requested = std::any_of(
        args.begin(), args.end(), [](const std::string& arg) {
            return is_headless_load_map_command(arg) ||
                arg == "--headless-load-scenario" ||
                arg.rfind("--debug-headless-", 0) == 0;
        });
#endif
    try {
        runtime_paths::executable_directory();
    } catch (const std::system_error& error) {
#ifndef NDEBUG
        if (debug_headless_requested) {
            std::cerr << error.what() << '\n';
            return EXIT_FAILURE;
        }
#endif
        MessageBoxA(nullptr, error.what(), "komapedit", MB_OK | MB_ICONERROR);
        return EXIT_FAILURE;
    }
#ifndef NDEBUG
#if defined(_MSC_VER)
    if (debug_headless_requested) {
        _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
        _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
    }
#endif
    if (const auto status = run_headless_commands(args)) return *status;
#endif

    const HRESULT apartment = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(apartment)) {
        std::cerr << "CoInitializeEx failed (HRESULT 0x" << std::hex
                  << static_cast<std::uint32_t>(apartment) << ")\n";
        return EXIT_FAILURE;
    }
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
    const auto cleanup_main_window = [&] {
        CleanupDeviceD3D();
        g_main_hwnd = nullptr;
        DestroyWindow(hwnd);
        UnregisterClassW(wc.lpszClassName, wc.hInstance);
        if (app_icon) DestroyIcon(app_icon);
        if (app_icon_small) DestroyIcon(app_icon_small);
        CoUninitialize();
    };
    if (!CreateDeviceD3D(hwnd)) {
        cleanup_main_window();
        return 1;
    }

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
        "C:/Windows/Fonts/msjh.ttc",
        "C:/Windows/Fonts/segoeui.ttf"
    };
    const float font_size = k_default_font_size * scale;
    bool font_loaded = false;
    for (const char* font : font_candidates) {
        if (std::filesystem::exists(font)) {
            io.Fonts->AddFontFromFileTTF(font, font_size, nullptr,
                                         application_font_glyph_ranges(*io.Fonts));
            font_loaded = true;
            break;
        }
    }
    if (!font_loaded) {
        ImFontConfig config;
        config.SizePixels = font_size;
        io.Fonts->AddFontDefault(&config);
    }
    merge_required_symbol_glyphs(*io.Fonts, font_size);
    if (!merge_korean_font(*io.Fonts, font_size)) {
        MessageBoxW(hwnd, L"Unable to load the Windows Malgun Gothic font (malgun.ttf).",
                    L"komapedit", MB_OK | MB_ICONERROR);
        ImPlot::DestroyContext();
        ImGui::DestroyContext();
        cleanup_main_window();
        return 1;
    }

    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);
    ShowWindow(hwnd, SW_SHOWDEFAULT);
    UpdateWindow(hwnd);

    int exit_code = EXIT_SUCCESS;
    try {
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

            app.service_pending_persistence(layout_path);

            if (!needs_render && !received_message) {
                const DWORD wait_result = MsgWaitForMultipleObjectsEx(
                    0, nullptr, app.idle_wait_timeout_ms(), QS_ALLINPUT, MWMO_INPUTAVAILABLE);
                if (wait_result == WAIT_TIMEOUT) needs_render = true;
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
            app.service_pending_persistence(layout_path);
            if (GImGui && GImGui->InputEventsQueue.Size > 0) needs_render = true;
            if (touch_input::wants_continuous_render()) needs_render = true;
            if (imgui_layout_save_pending()) needs_render = true;
        }

        if (!save_imgui_layout(layout_path)) {
            app.add_log_at(__FILE__, LogSeverity::Warning, "Failed to save ImGui layout on exit.");
        }
    } catch (const toolbar_icons::LoadError& error) {
        MessageBoxA(hwnd, error.what(), "komapedit", MB_OK | MB_ICONERROR);
        exit_code = EXIT_FAILURE;
    }

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImPlot::DestroyContext();
    ImGui::DestroyContext();

    cleanup_main_window();
    return exit_code;
}
