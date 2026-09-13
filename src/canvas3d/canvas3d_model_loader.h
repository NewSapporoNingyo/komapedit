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

using MlApiVersionFn = unsigned int (*)();
using MlLoadModelFn = int (*)(const char*, MlMeshData*);
using MlFreeModelFn = void (*)(MlMeshData*);
using MlGetLastErrorFn = const char* (*)();

class ModelLoaderClient {
public:
    // Keep Assimp/model_loader loaded for the process lifetime; unloading it before
    // CRT/DLL teardown can leave stale cleanup callbacks in some dependency builds.
    ~ModelLoaderClient() = default;

    bool prepare(std::string& error);

    bool load(const std::string& path, MlMeshData& data, std::string& error);

    void free_model(MlMeshData& data);

#ifndef NDEBUG
    static void debug_reset_counts() noexcept;

    static size_t debug_successful_load_count() noexcept;

    static size_t debug_free_count() noexcept;
#endif

private:
    bool ensure_loaded(std::string& error);

    HMODULE library_ = nullptr;
    MlApiVersionFn api_version_ = nullptr;
    MlLoadModelFn load_model_ = nullptr;
    MlFreeModelFn free_model_ = nullptr;
    MlGetLastErrorFn get_last_error_ = nullptr;
#ifndef NDEBUG
    static inline std::atomic<size_t> debug_successful_load_count_{0};
    static inline std::atomic<size_t> debug_free_count_{0};
#endif
};

class ModelDataGuard {
public:
    ModelDataGuard(ModelLoaderClient& loader, MlMeshData& data) noexcept
        : loader_(&loader), data_(&data) {}

    ModelDataGuard(const ModelDataGuard&) = delete;
    ModelDataGuard& operator=(const ModelDataGuard&) = delete;

    ~ModelDataGuard() noexcept {
        if (!loaded_) return;
        try {
            loader_->free_model(*data_);
        } catch (...) {
            *data_ = {};
        }
    }

    void mark_loaded() noexcept { loaded_ = true; }

private:
    ModelLoaderClient* loader_ = nullptr;
    MlMeshData* data_ = nullptr;
    bool loaded_ = false;
};

float normalize_material_alpha(float value);
std::string hresult_text(const char* action, HRESULT hr);

} // namespace canvas3d_detail
