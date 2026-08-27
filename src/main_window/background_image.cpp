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
        height > D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION ||
        width > std::numeric_limits<UINT>::max() / 4) {
        error = "image dimensions exceed the Direct3D 11 limit";
        return false;
    }
    row_stride = width * 4;
    if (height > std::numeric_limits<size_t>::max() / row_stride) {
        error = "image byte size overflows the host address space";
        return false;
    }
    pixel_bytes = static_cast<size_t>(row_stride) * height;
    if (pixel_bytes > std::numeric_limits<UINT>::max()) {
        error = "image byte size exceeds the WIC copy limit";
        return false;
    }
    return true;
}

struct DecodedRgbaImage {
    UINT width = 0;
    UINT height = 0;
    std::vector<unsigned char> pixels;
};

enum class WicDecodeDebugFault {
    None,
    GetSizeHresult,
    Allocation,
};

bool decode_background_image_rgba(const std::string& path,
                                  DecodedRgbaImage& decoded,
                                  std::string& error,
                                  WicDecodeDebugFault debug_fault =
                                      WicDecodeDebugFault::None) {
    IWICImagingFactory* factory = nullptr;
    IWICBitmapDecoder* decoder = nullptr;
    IWICBitmapFrameDecode* frame = nullptr;
    IWICFormatConverter* converter = nullptr;
    auto release_wic = [&]() noexcept {
        release_com(converter);
        release_com(frame);
        release_com(decoder);
        release_com(factory);
    };
    auto fail = [&](const char* reason) {
        release_wic();
        error = reason;
        return false;
    };

    try {
        HRESULT hr = CoCreateInstance(
            CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(&factory));
        if (FAILED(hr)) return fail("failed to create the WIC imaging factory");
        hr = factory->CreateDecoderFromFilename(
            utf8_to_wide(path).c_str(), nullptr, GENERIC_READ,
            WICDecodeMetadataCacheOnLoad, &decoder);
        if (FAILED(hr)) return fail("the file cannot be opened or decoded");
        hr = decoder->GetFrame(0, &frame);
        if (FAILED(hr)) return fail("the first image frame cannot be decoded");
        hr = factory->CreateFormatConverter(&converter);
        if (FAILED(hr)) return fail("failed to create the WIC format converter");
        hr = converter->Initialize(
            frame, GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone,
            nullptr, 0.0, WICBitmapPaletteTypeCustom);
        if (FAILED(hr)) return fail("the image cannot be converted to RGBA");

        UINT width = 0;
        UINT height = 0;
        hr = debug_fault == WicDecodeDebugFault::GetSizeHresult
            ? E_FAIL : converter->GetSize(&width, &height);
        if (FAILED(hr)) return fail("failed to read image dimensions");
        UINT row_stride = 0;
        size_t pixel_bytes = 0;
        if (!checked_rgba_image_layout(
                width, height, row_stride, pixel_bytes, error)) {
            release_wic();
            return false;
        }

        if (debug_fault == WicDecodeDebugFault::Allocation) {
            throw std::bad_alloc();
        }
        std::vector<unsigned char> pixels(pixel_bytes);
        hr = converter->CopyPixels(
            nullptr, row_stride, static_cast<UINT>(pixel_bytes), pixels.data());
        if (FAILED(hr)) return fail("failed to copy image pixels");
        release_wic();

        DecodedRgbaImage next;
        next.width = width;
        next.height = height;
        next.pixels = std::move(pixels);
        decoded = std::move(next);
        error.clear();
        return true;
    } catch (const std::bad_alloc&) {
        return fail("memory allocation failed while decoding the image");
    } catch (const std::exception& exception) {
        release_wic();
        error = exception.what();
        return false;
    } catch (...) {
        return fail("an unknown error occurred while decoding the image");
    }
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
#endif
void TextureImage::release() {
    release_com(srv);
    pixels_rgba.clear();
    width = height = 0;
    path.clear();
}
void App::save_history() {
    if (!save_history_entries(history_path_, recent_maps_)) {
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
    bg.x = bg_x_;
    bg.y = bg_y_;
    bg.width = bg_width_;
    bg.height = bg_height_;
    bg.rotation_deg = bg_rotation_deg_;
    bg.brightness = bg_brightness_;
    return bg;
}

void App::save_current_background_to_history() {
    if (file_path_.empty()) return;
    if (bg_image_.path.empty()) return;
    upsert_recent_map(file_path_, current_background_history());
}

void App::sync_pending_background_values() {
    pending_bg_x_ = bg_x_;
    pending_bg_y_ = bg_y_;
    pending_bg_width_ = bg_width_;
    pending_bg_height_ = bg_height_;
    pending_bg_rotation_deg_ = bg_rotation_deg_;
    pending_bg_brightness_ = bg_brightness_;
}

void App::apply_pending_background_values(bool save_history_entry) {
    bg_x_ = pending_bg_x_;
    bg_y_ = pending_bg_y_;
    bg_width_ = pending_bg_width_;
    bg_height_ = pending_bg_height_;
    bg_rotation_deg_ = pending_bg_rotation_deg_;
    bg_brightness_ = std::clamp(pending_bg_brightness_, 1.0, 200.0);
    pending_bg_brightness_ = bg_brightness_;
    if (!bg_image_.pixels_rgba.empty()) rebuild_background_texture();
    if (save_history_entry) save_current_background_to_history();
}

void App::clear_background_image() {
    bg_image_.release();
    bg_show_ = false;
    sync_pending_background_values();
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

    bg_x_ = background.x;
    bg_y_ = background.y;
    bg_width_ = background.width;
    bg_height_ = background.height;
    bg_rotation_deg_ = background.rotation_deg;
    bg_brightness_ = std::clamp(background.brightness, 1.0, 200.0);
    bg_show_ = settings_.view_2d.show_background_image;
    if (!load_background_image(background.image_path, false)) {
        bg_show_ = false;
        sync_pending_background_values();
        return false;
    }
    if (bg_width_ <= 0.0) bg_width_ = static_cast<double>(bg_image_.width);
    if (bg_height_ <= 0.0) bg_height_ = static_cast<double>(bg_image_.height);
    sync_pending_background_values();
    return true;
}

bool App::load_background_image(const std::string& path, bool reset_parameters) {
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
            bg_width_ = static_cast<double>(decoded.width);
            bg_height_ = static_cast<double>(decoded.height);
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

    try {
        std::vector<unsigned char> adjusted = bg_image_.pixels_rgba;
        const double mul = std::clamp(bg_brightness_, 1.0, 200.0) / 100.0;
        for (size_t i = 0; i + 3 < adjusted.size(); i += 4) {
            adjusted[i + 0] = static_cast<unsigned char>(
                std::clamp(adjusted[i + 0] * mul, 0.0, 255.0));
            adjusted[i + 1] = static_cast<unsigned char>(
                std::clamp(adjusted[i + 1] * mul, 0.0, 255.0));
            adjusted[i + 2] = static_cast<unsigned char>(
                std::clamp(adjusted[i + 2] * mul, 0.0, 255.0));
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
        sub.pSysMem = adjusted.data();
        sub.SysMemPitch = row_stride;
        ID3D11Texture2D* texture = nullptr;
        HRESULT hr = device_->CreateTexture2D(&desc, &sub, &texture);
        if (FAILED(hr)) {
            KME_ADD_LOG("[ERROR] Failed to rebuild background image texture: "
                    "Direct3D texture creation failed");
            return false;
        }

        ID3D11ShaderResourceView* new_srv = nullptr;
        D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
        srv_desc.Format = desc.Format;
        srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        srv_desc.Texture2D.MipLevels = 1;
        hr = device_->CreateShaderResourceView(texture, &srv_desc, &new_srv);
        texture->Release();
        if (FAILED(hr)) {
            release_com(new_srv);
            KME_ADD_LOG("[ERROR] Failed to rebuild background image texture: "
                    "Direct3D shader-resource-view creation failed");
            return false;
        }
        release_com(bg_image_.srv);
        bg_image_.srv = new_srv;
        bg_image_.brightness = bg_brightness_;
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
