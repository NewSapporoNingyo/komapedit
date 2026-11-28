/*
 * Copyright (c) 2026 Sapporo_ningyo
 *
 * Licensed under Apache License 2.0; see LICENSE and NOTICE.
 */

#pragma once

#include "canvas3d_types.h"
#include "model_loader.h"
#include <atomic>
#include <string_view>

namespace canvas3d_detail {

inline constexpr std::string_view k_white_model_warning =
    "[WARN]canvas3D.cpp: rendering a white model because Assimp supplied no materials for ";

std::vector<GpuVertex> copy_model_vertices(const MlMeshData& data);

class ModelLoaderClient {
public:
    static bool prepare(std::string& error);
    static MlModelLoadStatus load(const std::string& path, MlMeshData& data, std::string& error);
    static void free_model(MlMeshData& data) noexcept;

#ifndef NDEBUG
    static void debug_reset_counts() noexcept;
    static size_t debug_successful_load_count() noexcept;
    static size_t debug_free_count() noexcept;

private:
    static inline std::atomic<size_t> debug_successful_load_count_{0};
    static inline std::atomic<size_t> debug_free_count_{0};
#endif
};

class ModelDataGuard {
public:
    explicit ModelDataGuard(MlMeshData& data) noexcept : data_(data) {}
    ModelDataGuard(const ModelDataGuard&) = delete;
    ModelDataGuard& operator=(const ModelDataGuard&) = delete;
    ~ModelDataGuard() { ModelLoaderClient::free_model(data_); }

private:
    MlMeshData& data_;
};

float normalize_material_alpha(float value);
std::string hresult_text(const char* action, HRESULT hr);

} // namespace canvas3d_detail
