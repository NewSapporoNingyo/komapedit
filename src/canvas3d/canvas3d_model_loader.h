/*
 * Copyright (c) 2026 Sapporo_ningyo
 *
 * Licensed under Apache License 2.0; see LICENSE and NOTICE.
 */

#pragma once

#include "canvas3d_types.h"
#include "model_loader.h"
#include <atomic>

namespace canvas3d_detail {

class ModelLoaderClient {
public:
    static bool prepare(std::string& error);
    static bool load(const std::string& path, MlMeshData& data, std::string& error);
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
