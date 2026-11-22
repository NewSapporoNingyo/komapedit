/*
 * Copyright (c) 2026 Sapporo_ningyo
 *
 * Licensed under Apache License 2.0; see LICENSE and NOTICE.
 */

#pragma once

#include <array>
#include <cstddef>
#include <stdexcept>

struct ID3D11Device;
struct ID3D11ShaderResourceView;
struct IWICImagingFactory;

namespace toolbar_icons {

enum class Icon { Move, Measure, Select, MileageSelect, Translate, Rotate, Edit, Apply, Count };

inline constexpr std::array<int, 9> source_sizes{16, 20, 24, 28, 32, 40, 48, 64, 80};

struct Size {
    int display_pixels;
    std::size_t source_index;
};

Size size_for_dpi(float dpi_scale);

class LoadError final : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

class Cache {
public:
    explicit Cache(ID3D11Device* device) : device_(device) {}
    ~Cache();
    Cache(const Cache&) = delete;
    Cache& operator=(const Cache&) = delete;

    bool button(Icon icon, const char* tooltip, bool selected);

private:
    ID3D11ShaderResourceView* texture(Icon icon, std::size_t source_index);

    ID3D11Device* device_;
    IWICImagingFactory* factory_ = nullptr;
    std::array<std::array<ID3D11ShaderResourceView*, source_sizes.size()>,
               static_cast<std::size_t>(Icon::Count)> textures_{};
};

} // namespace toolbar_icons
