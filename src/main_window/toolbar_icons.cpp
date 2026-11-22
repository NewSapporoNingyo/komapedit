/*
 * Copyright (c) 2026 Sapporo_ningyo
 *
 * Licensed under Apache License 2.0; see LICENSE and NOTICE.
 */

#include "toolbar_icons.h"
#include "kme.h"
#include "resource.h"

#include <windows.h>
#include <d3d11.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <sstream>
#include <vector>

namespace toolbar_icons {
namespace {

using Microsoft::WRL::ComPtr;

struct IconResource {
    int first_id;
    const char* name;
};

constexpr std::array<IconResource, static_cast<std::size_t>(Icon::Count)> resources{{
    {IDR_TOOLBAR_MOVE, "move"},
    {IDR_TOOLBAR_MEASURE, "measure"},
    {IDR_TOOLBAR_SELECT, "select"},
    {IDR_TOOLBAR_MILEAGE_SELECT, "mileage-select"},
    {IDR_TOOLBAR_TRANSLATE, "translate"},
    {IDR_TOOLBAR_ROTATE, "rotate"},
    {IDR_TOOLBAR_EDIT, "edit"},
    {IDR_TOOLBAR_APPLY, "apply"},
    {IDR_TOOLBAR_OPEN, "open"},
    {IDR_TOOLBAR_RELOAD, "reload"},
    {IDR_TOOLBAR_RELOAD_GEOMETRY, "reload-geometry"},
    {IDR_TOOLBAR_ADD_MAP_ELEMENT, "add-map-element"},
    {IDR_TOOLBAR_SAVE, "save"},
    {IDR_TOOLBAR_REVERT, "revert"},
    {IDR_TOOLBAR_GRID_FIXED, "grid-fixed"},
    {IDR_TOOLBAR_GRID_MOVABLE, "grid-movable"},
    {IDR_TOOLBAR_GRID_NONE, "grid-none"},
    {IDR_TOOLBAR_CLEAR, "clear"},
    {IDR_TOOLBAR_COPY, "copy"},
    {IDR_TOOLBAR_FIND, "find"},
    {IDR_TOOLBAR_FIND_UNUSED, "find-unused"},
    {IDR_TOOLBAR_MODEL_LIST, "model-list"},
    {IDR_TOOLBAR_BACKGROUND_COLOR, "background-color"},
    {IDR_TOOLBAR_RELOAD_SCENE_MODELS, "reload-scene-models"},
    {IDR_TOOLBAR_CLOSE, "close"},
}};

} // namespace

Size size_for_dpi(float dpi_scale) {
    const int pixels = static_cast<int>(std::lround(16.0f * dpi_scale));
    const auto source = std::lower_bound(source_sizes.begin(), source_sizes.end(), pixels);
    const auto index = static_cast<std::size_t>(source - source_sizes.begin());
    return {pixels, std::min(index, source_sizes.size() - 1)};
}

ImVec2 button_size(Size size) {
    const float pixels = static_cast<float>(size.display_pixels);
    const ImVec2 padding = ImGui::GetStyle().FramePadding;
    return ImVec2(pixels + 2.0f * padding.x,
                  std::max(ImGui::GetFrameHeight(), pixels + 2.0f * padding.y));
}

Cache::~Cache() {
    for (auto& sizes : textures_) {
        for (auto*& view : sizes) release_com(view);
    }
    release_com(factory_);
}

ID3D11ShaderResourceView* Cache::texture(Icon icon, std::size_t source_index) {
    const auto icon_index = static_cast<std::size_t>(icon);
    auto*& cached_view = textures_[icon_index][source_index];
    if (cached_view) return cached_view;

    const IconResource& asset = resources[icon_index];
    const int pixels = source_sizes[source_index];
    const auto check = [&](HRESULT result, const char* operation) {
        if (FAILED(result)) {
            std::ostringstream message;
            message << "Toolbar icon " << asset.name << " (" << pixels << " px): "
                    << operation << " failed (0x" << std::hex
                    << static_cast<std::uint32_t>(result) << ").";
            throw LoadError(message.str());
        }
    };

    const HMODULE module = GetModuleHandleW(nullptr);
    const int resource_id = asset.first_id + static_cast<int>(source_index);
    const HRSRC resource = FindResourceW(module, MAKEINTRESOURCEW(resource_id), RT_RCDATA);
    if (!resource) check(HRESULT_FROM_WIN32(GetLastError()), "FindResourceW");
    const HGLOBAL loaded = LoadResource(module, resource);
    if (!loaded) check(HRESULT_FROM_WIN32(GetLastError()), "LoadResource");
    const DWORD length = SizeofResource(module, resource);
    if (length == 0) check(E_FAIL, "SizeofResource");
    auto* bytes = static_cast<BYTE*>(LockResource(loaded));
    if (!bytes) check(E_FAIL, "LockResource");

    if (!factory_) {
        check(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                               IID_PPV_ARGS(&factory_)), "CoCreateInstance(WIC)");
    }
    ComPtr<IWICStream> stream;
    check(factory_->CreateStream(stream.GetAddressOf()), "CreateStream");
    check(stream->InitializeFromMemory(bytes, length), "InitializeFromMemory");

    ComPtr<IWICBitmapDecoder> decoder;
    check(factory_->CreateDecoderFromStream(stream.Get(), nullptr, WICDecodeMetadataCacheOnLoad,
                                           decoder.GetAddressOf()), "CreateDecoderFromStream");
    ComPtr<IWICBitmapFrameDecode> frame;
    check(decoder->GetFrame(0, frame.GetAddressOf()), "GetFrame");
    ComPtr<IWICFormatConverter> converter;
    check(factory_->CreateFormatConverter(converter.GetAddressOf()), "CreateFormatConverter");
    check(converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppRGBA,
                                WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom),
          "Convert to RGBA");

    UINT width = 0;
    UINT height = 0;
    check(converter->GetSize(&width, &height), "GetSize");
    if (width != static_cast<UINT>(pixels) || height != width) {
        check(E_INVALIDARG, "Validate PNG dimensions");
    }
    const UINT row_bytes = width * 4;
    const UINT byte_count = row_bytes * height;
    std::vector<BYTE> rgba(byte_count);
    check(converter->CopyPixels(nullptr, row_bytes, byte_count, rgba.data()), "CopyPixels");
    for (std::size_t i = 0; i < rgba.size(); i += 4) {
        rgba[i] = rgba[i + 1] = rgba[i + 2] = 255;
    }

    D3D11_TEXTURE2D_DESC description{};
    description.Width = width;
    description.Height = height;
    description.ArraySize = 1;
    description.MipLevels = 1;
    description.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    description.SampleDesc.Count = 1;
    description.Usage = D3D11_USAGE_IMMUTABLE;
    description.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    const D3D11_SUBRESOURCE_DATA initial_data{rgba.data(), row_bytes, 0};
    ComPtr<ID3D11Texture2D> image;
    check(device_->CreateTexture2D(&description, &initial_data, image.GetAddressOf()), "CreateTexture2D");
    ComPtr<ID3D11ShaderResourceView> view;
    check(device_->CreateShaderResourceView(image.Get(), nullptr, view.GetAddressOf()),
          "CreateShaderResourceView");
    cached_view = view.Detach();
    return cached_view;
}

bool Cache::button(Icon icon, const char* tooltip, bool selected) {
    const float dpi_scale = ImGui::GetWindowDpiScale();
    const Size size = size_for_dpi(dpi_scale);
    const float pixels = static_cast<float>(size.display_pixels);
    const ImGuiStyle& style = ImGui::GetStyle();
    ImGui::PushID(resources[static_cast<std::size_t>(icon)].name);
    ImGui::PushStyleColor(ImGuiCol_Button, style.Colors[selected ? ImGuiCol_FrameBgActive : ImGuiCol_FrameBg]);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                         style.Colors[selected ? ImGuiCol_FrameBgActive : ImGuiCol_FrameBgHovered]);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, style.Colors[ImGuiCol_FrameBgActive]);
    const bool pressed = ImGui::Button("##toolbar_icon", button_size(size));
    ImGui::PopStyleColor(3);
    ImGui::PopID();

    const bool hovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled);
    if (ImGui::IsItemVisible()) {
        const ImVec2 min = ImGui::GetItemRectMin();
        const ImVec2 max = ImGui::GetItemRectMax();
        const ImVec2 origin(std::floor((min.x + max.x - pixels) * 0.5f),
                            std::floor((min.y + max.y - pixels) * 0.5f));
        auto* draw_list = ImGui::GetWindowDrawList();
        if (selected) {
            draw_list->AddRect(min, max, ImGui::GetColorU32(ImGuiCol_CheckMark),
                               style.FrameRounding, dpi_scale);
        }
        const auto id = reinterpret_cast<ImTextureID>(texture(icon, size.source_index));
        draw_list->AddImage(id, origin, ImVec2(origin.x + pixels, origin.y + pixels),
                            ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f),
                            ImGui::GetColorU32(ImGuiCol_Text, selected || hovered ? 1.0f : 0.85f));
    }
    if (hovered) ImGui::SetTooltip("%s", tooltip);
    return pressed;
}

} // namespace toolbar_icons
