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
#include <wrl/client.h>

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

namespace {
bool checked_rgba_image_layout(UINT width,
                               UINT height,
                               UINT& row_stride,
                               size_t& pixel_bytes,
                               std::string& error) {
    row_stride = 0;
    pixel_bytes = 0;
    if (width == 0 || height == 0) {
        error = "image dimensions must be nonzero";
        return false;
    }
    if (width > D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION ||
        height > D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION) {
        error = "image dimensions exceed the Direct3D 11 limit";
        return false;
    }
    constexpr std::uint64_t maximum_bytes =
        std::uint64_t{D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION} *
        D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION * 4;
    static_assert(maximum_bytes <= std::numeric_limits<UINT>::max() &&
                  maximum_bytes <= std::numeric_limits<size_t>::max());
    row_stride = width * 4;
    pixel_bytes = static_cast<size_t>(row_stride) * height;
    return true;
}

struct DecodedRgbaImage {
    UINT width = 0;
    UINT height = 0;
    std::vector<unsigned char> pixels;
};

#ifndef NDEBUG
enum class WicDecodeDebugFault { None, GetSizeHresult, Allocation };
#endif

bool decode_background_image_rgba(const std::string& path, DecodedRgbaImage& decoded,
                                  std::string& error
#ifndef NDEBUG
                                  , WicDecodeDebugFault debug_fault = WicDecodeDebugFault::None
#endif
                                  ) try {
    using Microsoft::WRL::ComPtr;
    const auto fail = [&](const char* reason) {
        error = reason;
        return false;
    };
    ComPtr<IWICImagingFactory> factory;
    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                   IID_PPV_ARGS(factory.GetAddressOf()));
    if (FAILED(hr)) return fail("failed to create the WIC imaging factory");
    ComPtr<IWICBitmapDecoder> decoder;
    hr = factory->CreateDecoderFromFilename(
        utf8_to_wide(path).c_str(), nullptr, GENERIC_READ,
        WICDecodeMetadataCacheOnLoad, decoder.GetAddressOf());
    if (FAILED(hr)) return fail("the file cannot be opened or decoded");
    ComPtr<IWICBitmapFrameDecode> frame;
    hr = decoder->GetFrame(0, frame.GetAddressOf());
    if (FAILED(hr)) return fail("the first image frame cannot be decoded");
    ComPtr<IWICFormatConverter> converter;
    hr = factory->CreateFormatConverter(converter.GetAddressOf());
    if (FAILED(hr)) return fail("failed to create the WIC format converter");
    hr = converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppRGBA,
                               WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom);
    if (FAILED(hr)) return fail("the image cannot be converted to RGBA");

    UINT width = 0, height = 0;
#ifndef NDEBUG
    if (debug_fault == WicDecodeDebugFault::GetSizeHresult) return fail("failed to read image dimensions");
#endif
    hr = converter->GetSize(&width, &height);
    if (FAILED(hr)) return fail("failed to read image dimensions");
    UINT row_stride = 0;
    size_t pixel_bytes = 0;
    if (!checked_rgba_image_layout(width, height, row_stride, pixel_bytes, error)) return false;

#ifndef NDEBUG
    if (debug_fault == WicDecodeDebugFault::Allocation) throw std::bad_alloc();
#endif
    std::vector<unsigned char> pixels(pixel_bytes);
    hr = converter->CopyPixels(nullptr, row_stride, static_cast<UINT>(pixel_bytes), pixels.data());
    if (FAILED(hr)) return fail("failed to copy image pixels");
    decoded = {width, height, std::move(pixels)};
    error.clear();
    return true;
} catch (const std::bad_alloc&) {
    error = "memory allocation failed while decoding the image";
    return false;
} catch (const std::exception& exception) {
    error = exception.what();
    return false;
}

} // namespace

#ifndef NDEBUG
HeadlessResourceSafetyContractResult run_debug_resource_safety_contract(
    const std::string& valid_image_path,
    const std::string& missing_image_path) {
    HeadlessResourceSafetyContractResult result;
    UINT row_stride = 0;
    size_t pixel_bytes = 0;
    std::string error;
    const bool small_valid = checked_rgba_image_layout(
        2, 3, row_stride, pixel_bytes, error) &&
        row_stride == 8 && pixel_bytes == 24;
    const bool zero_rejected = !checked_rgba_image_layout(
        0, 3, row_stride, pixel_bytes, error);
    const bool d3d_over_limit_rejected = !checked_rgba_image_layout(
        D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION + 1, 1,
        row_stride, pixel_bytes, error);
    const bool maximum_valid = checked_rgba_image_layout(
        D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION,
        D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION,
        row_stride, pixel_bytes, error) &&
        row_stride == D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION * 4 &&
        pixel_bytes == static_cast<size_t>(D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION) *
                           D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION * 4;
    result.image_layout = small_valid && zero_rejected &&
        d3d_over_limit_rejected && maximum_valid;

    DecodedRgbaImage valid_image;
    std::string decode_error;
    const bool valid_decode = decode_background_image_rgba(
        valid_image_path, valid_image, decode_error) &&
        valid_image.width == 1 && valid_image.height == 1 &&
        valid_image.pixels.size() == 4;
    DecodedRgbaImage failed_image;
    const bool missing_rejected = !decode_background_image_rgba(
        missing_image_path, failed_image, decode_error) &&
        !decode_error.empty() && failed_image.pixels.empty();
    const bool hresult_rejected = !decode_background_image_rgba(
        valid_image_path, failed_image, decode_error,
        WicDecodeDebugFault::GetSizeHresult) &&
        decode_error == "failed to read image dimensions";
    const bool allocation_rejected = !decode_background_image_rgba(
        valid_image_path, failed_image, decode_error,
        WicDecodeDebugFault::Allocation) &&
        decode_error == "memory allocation failed while decoding the image";
    result.image_decode = valid_decode && missing_rejected &&
        hresult_rejected && allocation_rejected;

    const int int_min = std::numeric_limits<int>::lowest();
    const int int_max = std::numeric_limits<int>::max();
    const bool thousandths_truncation =
        std::abs(truncate_gui_thousandths(1.2349) - 1.234) < 1e-12 &&
        std::abs(truncate_gui_thousandths(-1.2349) + 1.234) < 1e-12 &&
        truncate_gui_thousandths(-0.0004) == 0.0 &&
        !std::signbit(truncate_gui_thousandths(-0.0004)) &&
        std::isnan(truncate_gui_thousandths(
            std::numeric_limits<double>::quiet_NaN()));
    result.numeric_conversion =
        kme::truncating_int_or_zero(42.875) == 42 &&
        kme::truncating_int_or_zero(-42.875) == -42 &&
        kme::truncating_int_or_zero(static_cast<double>(int_min)) == int_min &&
        kme::truncating_int_or_zero(static_cast<double>(int_max)) == int_max &&
        kme::truncating_int_or_zero(static_cast<double>(int_max) + 1.0) == 0 &&
        kme::truncating_int_or_zero(static_cast<double>(int_min) - 1.0) == 0 &&
        kme::truncating_int_or_zero(std::numeric_limits<double>::quiet_NaN()) == 0 &&
        kme::truncating_int_or_zero(std::numeric_limits<double>::infinity()) == 0 &&
        kme::truncating_int_or_zero(-std::numeric_limits<double>::infinity()) == 0 &&
        thousandths_truncation;
    return result;
}

bool App::debug_background_drag_contract(const std::filesystem::path& directory, std::ostream& out) {
    bool passed = true;
    const auto check = [&](const char* name, bool value) {
        out << "background_" << name << '=' << (value ? "PASS" : "FAIL") << '\n';
        passed = value && passed;
    };
    const ImVec2 origin(15.0f, 29.0f);
    const ImVec2 size(900.0f, 700.0f);
    const auto* texture = bg_image_.srv;
    history_path_ = directory / "history.ini";
    recent_maps_.clear();
    creator_message_history_.clear();
    file_path_ = (directory / "drag-map.txt").u8string();
    plan_view_.rotation = 0.55;
    plan_view_.scale = 0.8;
    plan_view_.cx = 100.0;
    plan_view_.cy = -50.0;
    show_background_adjust_ = true;
    bg_transform_ = {125.0, -65.0, 320.0, 180.0, 32.0};
    sync_pending_background_values();
    pending_bg_transform_.x += 11.0;
    pending_bg_brightness_ = 180.0;
    auto& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(1280.0f, 900.0f);
    io.DeltaTime = 1.0f / 60.0f;
    io.Fonts->Build();
    const auto frame = [&](ImVec2 pointer, bool hovered = true) {
        io.AddMousePosEvent(pointer.x, pointer.y);
        ImGui::NewFrame();
        const bool consumed = process_background_drag(origin, size, 1.0f, hovered, true);
        ImGui::EndFrame();
        return consumed;
    };
    const auto mouse_button = [&](bool down, ImVec2 pointer, bool hovered = true) {
        io.AddMouseButtonEvent(ImGuiMouseButton_Left, down);
        return frame(pointer, hovered);
    };
    const auto point = [&](const canvas2d::BackgroundTransform& pose, double x, double y) {
        const double angle = pose.rotation_deg * 3.14159265358979323846 / 180.0;
        return plan_view_.world_to_screen(pose.x + x * std::cos(angle) - y * std::sin(angle),
            pose.y + x * std::sin(angle) + y * std::cos(angle), origin, size);
    };
    const auto points_are_close = [](ImVec2 a, ImVec2 b, float tolerance) {
        return std::hypot(a.x - b.x, a.y - b.y) < tolerance;
    };
    const auto history_bytes = [&]() {
        std::ifstream input(history_path_, std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
    };

    const auto typed = pending_bg_transform_;
    const ImVec2 move_start = point(typed, 20.0, 15.0);
    const ImVec2 move_end = point(typed, 62.0, -3.0);
    frame(move_start);
    check("interior_captures_drag", mouse_button(true, move_start) && bg_drag_.has_value());
    frame(move_end, false);
    check("move_uses_typed_preview", points_are_close(point(pending_bg_transform_, 0.0, 0.0),
        point(typed, 42.0, -18.0), 2.0f) && bg_transform_.x == 125.0);
    check("preview_reuses_texture_without_history", bg_image_.srv == texture && history_bytes().empty());
    check("release_outside_commits", mouse_button(false, move_end, false) && !bg_drag_ &&
        bg_transform_ == pending_bg_transform_ && bg_image_.srv == texture &&
        bg_brightness_ != pending_bg_brightness_ && !history_bytes().empty());
    const auto moved = bg_transform_;
    const auto saved_move = history_bytes();

    plan_view_.scale = 1.4;
    const ImVec2 edge_start = point(moved, moved.width * 0.5, moved.height * 0.15);
    const ImVec2 edge_end = point(moved, moved.width * 0.5 + 80.0, moved.height * 0.15 + 20.0);
    frame(edge_start);
    mouse_button(true, edge_start);
    frame(edge_end);
    const auto edge = pending_bg_transform_;
    check("edge_local_axis_and_anchor", std::abs(edge.width - moved.width - 80.0) < 2.0 &&
        edge.height == moved.height && points_are_close(point(edge, -edge.width * 0.5, 0.0),
        point(moved, -moved.width * 0.5, 0.0), 0.02f));
    io.AddKeyEvent(ImGuiMod_Shift, true);
    frame(edge_end);
    const auto scaled = pending_bg_transform_;
    check("shift_preserves_drag_start_ratio", scaled.height > edge.height &&
        std::abs(scaled.width / scaled.height - moved.width / moved.height) < 1e-12 &&
        points_are_close(point(scaled, -scaled.width * 0.5, 0.0),
                         point(moved, -moved.width * 0.5, 0.0), 0.02f));
    io.AddKeyEvent(ImGuiKey_Escape, true);
    const bool escape_consumed = frame(edge_end);
    io.AddKeyEvent(ImGuiMod_Shift, false);
    io.AddKeyEvent(ImGuiKey_Escape, false);
    frame(edge_start);
    check("escape_restores_and_consumes_release", escape_consumed && mouse_button(false, edge_start) &&
        !bg_drag_ && pending_bg_transform_ == moved && bg_transform_ == moved && history_bytes() == saved_move);

    const ImVec2 corner_start = point(moved, moved.width * 0.5, moved.height * 0.5);
    const ImVec2 corner_end = point(moved, moved.width * 0.5 + 35.0, moved.height * 0.5 - 25.0);
    frame(corner_start);
    mouse_button(true, corner_start);
    frame(corner_end);
    mouse_button(false, corner_end);
    const auto resized = bg_transform_;
    check("corner_axes_and_anchor", std::abs(resized.width - moved.width - 35.0) < 2.0 &&
        std::abs(resized.height - moved.height + 25.0) < 2.0 &&
        points_are_close(point(resized, -resized.width * 0.5, -resized.height * 0.5),
        point(moved, -moved.width * 0.5, -moved.height * 0.5), 0.02f));

    const ImVec2 turn_start = point(resized, 0.0, -resized.height * 0.5 - 28.0 / plan_view_.scale);
    const ImVec2 center = point(resized, 0.0, 0.0);
    const ImVec2 turn_end(center.x - (turn_start.y - center.y), center.y + (turn_start.x - center.x));
    frame(turn_start);
    mouse_button(true, turn_start);
    frame(turn_end);
    mouse_button(false, turn_end);
    const auto rotated = bg_transform_;
    check("rotation_keeps_center_and_size", std::abs(rotated.rotation_deg - resized.rotation_deg - 90.0) < 2.0 &&
        rotated.x == resized.x && rotated.y == resized.y &&
        rotated.width == resized.width && rotated.height == resized.height && bg_image_.srv == texture);

    const HistoryState saved = load_history_state(history_path_);
    close_background_adjust();
    clear_background_image();
    check("history_restores_confirmed_pose", bg_show_ && saved.recent_maps.size() == 1 &&
        apply_background_history(saved.recent_maps.front().background) && bg_transform_ == rotated &&
        bg_brightness_ == saved.recent_maps.front().background.brightness);
    return passed;
}

bool App::debug_background_texture_contract(ID3D11Device* device,
                                             const std::string& image_path,
                                             std::ostream& out,
                                             bool test_pointer_drag) {
    bool passed = true;
    const auto check = [&](const char* name, bool value) {
        out << "background_" << name << '=' << (value ? "PASS" : "FAIL") << '\n';
        passed = value && passed;
        return value;
    };
    struct HeldView {
        ID3D11ShaderResourceView* value;
        explicit HeldView(ID3D11ShaderResourceView* view) : value(view) {
            if (value) value->AddRef();
        }
        ~HeldView() { release_com(value); }
    };
    const auto pixel = [&](ID3D11ShaderResourceView* view)
        -> std::optional<std::array<unsigned char, 4>> {
        if (!view) return std::nullopt;
        struct Resources {
            ID3D11Resource* resource = nullptr;
            ID3D11Texture2D* texture = nullptr;
            ID3D11Texture2D* staging = nullptr;
            ID3D11DeviceContext* context = nullptr;
            ~Resources() {
                release_com(context);
                release_com(staging);
                release_com(texture);
                release_com(resource);
            }
        } resources;
        view->GetResource(&resources.resource);
        if (!resources.resource || FAILED(resources.resource->QueryInterface(
                IID_PPV_ARGS(&resources.texture)))) return std::nullopt;
        D3D11_TEXTURE2D_DESC desc{};
        resources.texture->GetDesc(&desc);
        desc.Usage = D3D11_USAGE_STAGING;
        desc.BindFlags = 0;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        if (FAILED(device->CreateTexture2D(&desc, nullptr, &resources.staging))) return std::nullopt;
        device->GetImmediateContext(&resources.context);
        resources.context->CopyResource(resources.staging, resources.texture);
        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (FAILED(resources.context->Map(resources.staging, 0, D3D11_MAP_READ, 0, &mapped))) return std::nullopt;
        std::array<unsigned char, 4> value{};
        std::copy_n(static_cast<const unsigned char*>(mapped.pData), value.size(), value.begin());
        resources.context->Unmap(resources.staging, 0);
        return value;
    };
    struct Fixture {
        std::filesystem::path directory = std::filesystem::temp_directory_path() /
            ("komapedit-background-cache-" + std::to_string(
                std::chrono::steady_clock::now().time_since_epoch().count()));
        bool owned = false;
        ~Fixture() {
            if (!owned) return;
            std::error_code error;
            std::filesystem::remove_all(directory, error);
        }
    } fixture;
    ImGui::CreateContext();
    ImPlot::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;
    try {
        UserSettings settings;
        settings.language = Language::En;
        App app(device, settings, 1.0f, false, false);
        if (!check("initial_load", app.load_background_image(image_path, true))) {
            throw std::runtime_error("background fixture could not load");
        }
        HeldView original(app.bg_image_.srv);
        check("initial_pixel", pixel(original.value) ==
            std::array<unsigned char, 4>{0x11, 0x22, 0x33, 0xff});
        app.sync_pending_background_values();
        app.apply_pending_background_values(false);
        check("unchanged_reuses_texture", app.bg_image_.srv == original.value);
        app.pending_bg_transform_.x += 10;
        app.pending_bg_transform_.y -= 20;
        app.pending_bg_transform_.width *= 2;
        app.pending_bg_transform_.height *= 3;
        app.pending_bg_transform_.rotation_deg += 30;
        app.apply_pending_background_values(false);
        check("geometry_reuses_texture", app.bg_image_.srv == original.value &&
            app.bg_transform_ == app.pending_bg_transform_);
        app.pending_bg_brightness_ = 200;
        app.apply_pending_background_values(false);
        check("brightness_rebuilds_texture", app.bg_image_.srv != original.value &&
            app.bg_image_.brightness == 200 && pixel(app.bg_image_.srv) ==
                std::array<unsigned char, 4>{0x22, 0x44, 0x66, 0xff});
        HeldView bright(app.bg_image_.srv);
        app.pending_bg_brightness_ = 150;
        app.device_ = nullptr;
        app.apply_pending_background_values(false);
        app.device_ = device;
        check("failed_upload_retains_texture", app.bg_image_.srv == bright.value &&
            app.bg_image_.brightness == 200);
        app.apply_pending_background_values(false);
        check("failed_upload_retries", app.bg_image_.srv != bright.value &&
            app.bg_image_.brightness == 150 && pixel(app.bg_image_.srv) ==
                std::array<unsigned char, 4>{25, 51, 76, 255});

        fixture.owned = std::filesystem::create_directory(fixture.directory);
        if (!fixture.owned) {
            throw std::runtime_error("background fixture directory could not be created");
        }
        const auto replacement = fixture.directory / "replacement.bmp";
        std::filesystem::copy_file(std::filesystem::path(utf8_to_wide(image_path)), replacement);
        {
            std::fstream file(replacement, std::ios::in | std::ios::out | std::ios::binary);
            const unsigned char bgra[] = {20, 40, 80, 255};
            file.seekp(54);
            file.write(reinterpret_cast<const char*>(bgra), sizeof(bgra));
            if (!file) throw std::runtime_error("background replacement could not be written");
        }
        HeldView previous(app.bg_image_.srv);
        check("new_image_rebuilds_same_brightness",
            app.load_background_image(wide_to_utf8(replacement.wstring()), false) &&
            app.bg_image_.srv != previous.value && app.bg_image_.brightness == 150 &&
            pixel(app.bg_image_.srv) == std::array<unsigned char, 4>{120, 60, 30, 255});

        if (test_pointer_drag) {
            HeldView drag_texture(app.bg_image_.srv);
            passed = app.debug_background_drag_contract(fixture.directory, out) && passed;
        }

        // Same pixel data, build and Apply path for before/after measurements.
        app.bg_image_.release();
        app.bg_image_.width = app.bg_image_.height = 1024;
        app.bg_image_.pixels_rgba.assign(size_t{1024} * 1024 * 4, 127);
        app.bg_brightness_ = 100;
        if (!app.rebuild_background_texture()) throw std::runtime_error("background benchmark upload failed");
        app.sync_pending_background_values();
        std::vector<double> samples;
        size_t replacements = 0;
        for (int sample = 0; sample < 9; ++sample) {
            const auto started = std::chrono::steady_clock::now();
            for (int iteration = 0; iteration < 8; ++iteration) {
                HeldView before(app.bg_image_.srv);
                app.pending_bg_transform_.x += 1;
                app.apply_pending_background_values(false);
                replacements += app.bg_image_.srv != before.value ? 1 : 0;
            }
            samples.push_back(std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - started).count());
        }
        std::sort(samples.begin(), samples.end());
        out << "background_apply_benchmark pixels=1048576 samples=9 applies_per_sample=8"
            << " texture_replacements=" << replacements << " median_ms=" << samples[4]
            << " p95_ms=" << samples.back() << '\n';
        check("repeated_apply_reuses_texture", replacements == 0);
    } catch (const std::exception& error) {
        out << "background_contract_error=" << error.what() << '\n';
        passed = false;
    }
    ImPlot::DestroyContext();
    ImGui::DestroyContext();
    return passed;
}
#endif
void TextureImage::release() {
    release_com(srv);
    pixels_rgba.clear();
    width = height = 0;
    path.clear();
}
void App::save_history() {
    if (!save_history_state(history_path_, HistoryState{recent_maps_, creator_message_history_})) {
        KME_ADD_LOG("[WARN] Failed to save history.ini");
    }
}

void App::upsert_recent_map(const std::string& path,
                            const std::optional<BackgroundHistory>& background) {
    std::string stored_path = normalized_storage_path(path);
    std::string key = normalized_path_key(stored_path);
    if (key.empty()) return;

    RecentMapEntry selected;
    selected.path = stored_path;
    std::vector<RecentMapEntry> kept;
    bool found = false;
    for (const auto& entry : recent_maps_) {
        if (normalized_path_key(entry.path) == key) {
            if (!found) {
                selected = entry;
                selected.path = stored_path;
                found = true;
            }
        } else {
            kept.push_back(entry);
        }
    }
    if (background) selected.background = *background;
    kept.insert(kept.begin(), std::move(selected));
    if (kept.size() > k_max_recent_maps) kept.resize(k_max_recent_maps);
    recent_maps_ = std::move(kept);
    save_history();
}

void App::touch_recent_map(const std::string& path) {
    upsert_recent_map(path, std::nullopt);
}

BackgroundHistory App::current_background_history() const {
    BackgroundHistory bg;
    if (bg_image_.path.empty()) return bg;
    bg.has_image = true;
    bg.image_path = normalized_storage_path(bg_image_.path);
    bg.x = bg_transform_.x;
    bg.y = bg_transform_.y;
    bg.width = bg_transform_.width;
    bg.height = bg_transform_.height;
    bg.rotation_deg = bg_transform_.rotation_deg;
    bg.brightness = bg_brightness_;
    return bg;
}

void App::save_current_background_to_history() {
    const std::string entry_path = current_document_entry_path();
    if (entry_path.empty()) return;
    if (bg_image_.path.empty()) return;
    upsert_recent_map(entry_path, current_background_history());
}

void App::sync_pending_background_values() {
    pending_bg_transform_ = bg_transform_;
    pending_bg_brightness_ = bg_brightness_;
}

void App::apply_pending_background_values(bool save_history_entry) {
    bg_transform_ = pending_bg_transform_;
    bg_brightness_ = std::clamp(pending_bg_brightness_, 1.0, 200.0);
    pending_bg_brightness_ = bg_brightness_;
    if (!bg_image_.pixels_rgba.empty()) rebuild_background_texture();
    if (save_history_entry) save_current_background_to_history();
}

void App::close_background_adjust() {
    cancel_background_drag();
    show_background_adjust_ = false;
    focus_background_adjust_next_ = false;
    sync_pending_background_values();
}

void App::clear_background_image() {
    close_background_adjust();
    bg_image_.release();
}

bool App::apply_background_history(const BackgroundHistory& background) {
    if (!background.has_image || background.image_path.empty()) {
        clear_background_image();
        return true;
    }

    std::error_code ec;
    bool exists = std::filesystem::exists(std::filesystem::path(utf8_to_wide(background.image_path)), ec);
    if (ec || !exists) {
        clear_background_image();
        std::string message = "[WARN] Background image not found: " + background.image_path;
        KME_ADD_LOG(message);
        std::cerr << message << std::endl;
        return false;
    }

    bg_transform_ = {background.x, background.y, background.width,
                     background.height, background.rotation_deg};
    bg_brightness_ = std::clamp(background.brightness, 1.0, 200.0);
    if (!load_background_image(background.image_path, false)) {
        sync_pending_background_values();
        return false;
    }
    if (bg_transform_.width <= 0.0) bg_transform_.width = static_cast<double>(bg_image_.width);
    if (bg_transform_.height <= 0.0) bg_transform_.height = static_cast<double>(bg_image_.height);
    sync_pending_background_values();
    return true;
}

bool App::load_background_image(const std::string& path, bool reset_parameters) {
    cancel_background_drag();
    bg_image_.release();
    auto fail = [&](const std::string& reason) {
        bg_image_.release();
        const std::string message =
            "[ERROR] Failed to load background image: " + path + ": " + reason;
        KME_ADD_LOG(message);
        std::cerr << message << std::endl;
        return false;
    };

    try {
        DecodedRgbaImage decoded;
        std::string decode_error;
        if (!decode_background_image_rgba(path, decoded, decode_error)) {
            return fail(decode_error);
        }

        bg_image_.path = path;
        bg_image_.width = static_cast<int>(decoded.width);
        bg_image_.height = static_cast<int>(decoded.height);
        bg_image_.pixels_rgba = std::move(decoded.pixels);
        if (reset_parameters) {
            bg_transform_.width = static_cast<double>(decoded.width);
            bg_transform_.height = static_cast<double>(decoded.height);
            bg_brightness_ = 100.0;
        }
        if (!rebuild_background_texture()) {
            bg_image_.release();
            return false;
        }
        return true;
    } catch (const std::exception& error) {
        return fail(error.what());
    } catch (...) {
        return fail("an unknown error occurred while decoding the image");
    }
}

bool App::rebuild_background_texture() {
    if (!device_ || bg_image_.pixels_rgba.empty()) return false;
    UINT row_stride = 0;
    size_t pixel_bytes = 0;
    std::string layout_error;
    if (bg_image_.width <= 0 || bg_image_.height <= 0 ||
        !checked_rgba_image_layout(
            static_cast<UINT>(bg_image_.width),
            static_cast<UINT>(bg_image_.height),
            row_stride, pixel_bytes, layout_error) ||
        pixel_bytes != bg_image_.pixels_rgba.size()) {
        KME_ADD_LOG("[ERROR] Failed to rebuild background image texture: " +
                (layout_error.empty() ? std::string("pixel buffer size is inconsistent")
                                      : layout_error));
        return false;
    }

    const double brightness = std::clamp(bg_brightness_, 1.0, 200.0);
    if (bg_image_.srv && bg_image_.brightness == brightness) return true;

    try {
        const unsigned char* pixels = bg_image_.pixels_rgba.data();
        std::vector<unsigned char> adjusted;
        if (brightness != 100.0) {
            adjusted = bg_image_.pixels_rgba;
            const double mul = brightness / 100.0;
            for (size_t i = 0; i < adjusted.size(); i += 4) {
                for (size_t channel = 0; channel < 3; ++channel) {
                    auto& component = adjusted[i + channel];
                    component = static_cast<unsigned char>(std::clamp(component * mul, 0.0, 255.0));
                }
            }
            pixels = adjusted.data();
        }

        D3D11_TEXTURE2D_DESC desc = {};
        desc.Width = static_cast<UINT>(bg_image_.width);
        desc.Height = static_cast<UINT>(bg_image_.height);
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA sub = {};
        sub.pSysMem = pixels;
        sub.SysMemPitch = row_stride;
        Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
        HRESULT hr = device_->CreateTexture2D(&desc, &sub, texture.GetAddressOf());
        if (FAILED(hr)) {
            KME_ADD_LOG("[ERROR] Failed to rebuild background image texture: "
                    "Direct3D texture creation failed");
            return false;
        }

        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> new_srv;
        D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
        srv_desc.Format = desc.Format;
        srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        srv_desc.Texture2D.MipLevels = 1;
        hr = device_->CreateShaderResourceView(texture.Get(), &srv_desc, new_srv.GetAddressOf());
        if (FAILED(hr)) {
            KME_ADD_LOG("[ERROR] Failed to rebuild background image texture: "
                    "Direct3D shader-resource-view creation failed");
            return false;
        }
        release_com(bg_image_.srv);
        bg_image_.srv = new_srv.Detach();
        bg_image_.brightness = brightness;
        return true;
    } catch (const std::exception& error) {
        KME_ADD_LOG("[ERROR] Failed to rebuild background image texture: " +
                std::string(error.what()));
        return false;
    } catch (...) {
        KME_ADD_LOG("[ERROR] Failed to rebuild background image texture: unknown error");
        return false;
    }
}
