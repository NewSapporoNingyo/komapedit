/*
 * Copyright (c) 2026 Sapporo_ningyo
 *
 * Licensed under Apache License 2.0; see LICENSE and NOTICE.
 */

#ifdef _MSC_VER
#pragma execution_character_set("utf-8")
#endif

#include "canvas3d_impl.h"
#include "canvas3d_put_between.h"
#include "kme.h"
#include "scene_frame_profile.h"
#include "model_loader.h"
#include "runtime_paths.h"
#include "text_decoder.h"
#include <windows.h>
#include <wincodec.h>
#include <d3d11.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <map>
#include <filesystem>
#include <mutex>
#include <new>
#include <limits>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>
#include <stdexcept>

using namespace canvas3d_detail;

namespace canvas3d_detail {

constexpr size_t k_scene_model_max_workers = 8;

} // namespace canvas3d_detail

namespace canvas3d_detail {

float normalize_material_alpha(float value) {
    float alpha = clamp_color_component(value);
    // BVE .x models commonly use 0.99/0.999999 for opaque alpha-tested textures.
    return alpha >= k_material_opaque_alpha_threshold ? 1.0f : alpha;
}
static bool texture_pixels_have_alpha(const std::vector<unsigned char>& pixels) {
    for (size_t i = 3; i < pixels.size(); i += 4) {
        if (pixels[i] < 255) return true;
    }
    return false;
}

static std::string win32_error_text(DWORD code) {
    if (code == 0) return {};
    wchar_t* buffer = nullptr;
    DWORD flags = FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS;
    DWORD n = FormatMessageW(flags, nullptr, code, 0, reinterpret_cast<LPWSTR>(&buffer), 0, nullptr);
    if (n == 0 || !buffer) return "Win32 error " + std::to_string(code);
    std::wstring text(buffer, n);
    LocalFree(buffer);
    while (!text.empty() && (text.back() == L'\n' || text.back() == L'\r' || text.back() == L' ')) text.pop_back();
    return wide_to_utf8(text);
}

std::string hresult_text(const char* action, HRESULT hr) {
    std::string message = action;
    message += " failed: 0x";
    char hex[16] = {};
    std::snprintf(hex, sizeof(hex), "%08lx", static_cast<unsigned long>(hr));
    message += hex;
    return message;
}
static std::string normalized_texture_cache_key(const std::string& path) {
    try {
        std::filesystem::path normalized = utf8_to_wide(path);
        std::error_code ec;
        if (normalized.is_relative()) {
            std::filesystem::path absolute = std::filesystem::absolute(normalized, ec);
            if (!ec) normalized = std::move(absolute);
        }
        std::string key = wide_to_utf8(normalized.lexically_normal().wstring());
        return key.empty() ? path : key;
    } catch (...) {
        return path;
    }
}

static size_t scene_model_worker_count_for(size_t source_count) {
    if (source_count == 0) return 0;
    size_t available = std::thread::hardware_concurrency();
    if (available == 0) available = 4;
    if (available > 2) --available;
    return std::max<size_t>(1, std::min({source_count, available, k_scene_model_max_workers}));
}

} // namespace canvas3d_detail

bool Canvas3D::Impl::load_texture(const std::string& path,
                  ID3D11ShaderResourceView** out_srv,
                  std::string& error,
                  bool* out_has_alpha) {
    if (!out_srv) return false;
    *out_srv = nullptr;
    if (out_has_alpha) *out_has_alpha = false;
    struct TextureDecodeResources {
        IWICImagingFactory* factory = nullptr;
        IWICBitmapDecoder* decoder = nullptr;
        IWICBitmapFrameDecode* frame = nullptr;
        IWICFormatConverter* converter = nullptr;
        ID3D11Texture2D* texture = nullptr;

        ~TextureDecodeResources() {
            release_com(texture);
            release_com(converter);
            release_com(frame);
            release_com(decoder);
            release_com(factory);
        }
    } resources;
    auto fail = [&]() noexcept {
        release_com(*out_srv);
        if (out_has_alpha) *out_has_alpha = false;
        return false;
    };

    try {
        HRESULT hr = CoCreateInstance(
            CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(&resources.factory));
        if (FAILED(hr)) {
            error = hresult_text("CoCreateInstance(WIC)", hr);
            return fail();
        }
        hr = resources.factory->CreateDecoderFromFilename(
            utf8_to_wide(path).c_str(), nullptr, GENERIC_READ,
            WICDecodeMetadataCacheOnLoad, &resources.decoder);
        if (FAILED(hr)) {
            const auto failure = kme::maploader::classify_file_open_failure(
                kme::maploader::path_from_utf8(path));
            if (failure == kme::maploader::FileOpenFailureKind::Missing) {
                error = "Texture file not found at specified path: " + path;
            } else if (failure ==
                       kme::maploader::FileOpenFailureKind::ExistsButCannotOpen) {
                error = "Texture file exists but cannot be opened or decoded: " + path;
            } else {
                error = "Texture file status could not be determined: " + path;
            }
            return fail();
        }
        hr = resources.decoder->GetFrame(0, &resources.frame);
        if (FAILED(hr)) {
            error = "Texture file exists but cannot be opened or decoded: " + path;
            return fail();
        }
        hr = resources.factory->CreateFormatConverter(&resources.converter);
        if (FAILED(hr)) {
            error = hresult_text("CreateFormatConverter", hr);
            return fail();
        }
        hr = resources.converter->Initialize(
            resources.frame, GUID_WICPixelFormat32bppRGBA,
            WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom);
        if (FAILED(hr)) {
            error = "Texture file exists but cannot be opened or decoded: " + path;
            return fail();
        }

        UINT width = 0;
        UINT height = 0;
        hr = resources.converter->GetSize(&width, &height);
        if (FAILED(hr)) {
            error = "failed to read texture dimensions: " + path;
            return fail();
        }
        if (width == 0 || height == 0) {
            error = "texture has invalid size: " + path;
            return fail();
        }
        if (width > D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION ||
            height > D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION ||
            width > std::numeric_limits<UINT>::max() / 4) {
            error = "texture dimensions exceed the Direct3D 11 limit: " + path;
            return fail();
        }
        const UINT row_stride = width * 4;
        if (height > std::numeric_limits<size_t>::max() / row_stride) {
            error = "texture byte size overflows the host address space: " + path;
            return fail();
        }
        const size_t pixel_bytes = static_cast<size_t>(row_stride) * height;
        if (pixel_bytes > std::numeric_limits<UINT>::max()) {
            error = "texture byte size exceeds the WIC copy limit: " + path;
            return fail();
        }
#ifndef NDEBUG
        int remaining =
            debug_texture_allocation_throw_countdown.load(std::memory_order_relaxed);
        while (remaining > 0 &&
               !debug_texture_allocation_throw_countdown.compare_exchange_weak(
                   remaining, remaining - 1, std::memory_order_relaxed)) {
        }
        if (remaining == 1) throw std::bad_alloc();
#endif
        std::vector<unsigned char> pixels(pixel_bytes);
        hr = resources.converter->CopyPixels(
            nullptr, row_stride, static_cast<UINT>(pixel_bytes), pixels.data());
        if (FAILED(hr)) {
            error = "failed to copy texture pixels: " + path;
            return fail();
        }
        if (out_has_alpha) *out_has_alpha = texture_pixels_have_alpha(pixels);

        D3D11_TEXTURE2D_DESC desc = {};
        desc.Width = width;
        desc.Height = height;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA sub = {};
        sub.pSysMem = pixels.data();
        sub.SysMemPitch = row_stride;
        hr = device->CreateTexture2D(&desc, &sub, &resources.texture);
        if (FAILED(hr)) {
            error = hresult_text("CreateTexture2D(texture)", hr);
            return fail();
        }
        D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
        srv_desc.Format = desc.Format;
        srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        srv_desc.Texture2D.MipLevels = 1;
        hr = device->CreateShaderResourceView(resources.texture, &srv_desc, out_srv);
        if (FAILED(hr)) {
            error = hresult_text("CreateShaderResourceView(texture)", hr);
            return fail();
        }
        return true;
    } catch (const std::bad_alloc&) {
        error = "out of memory";
        return fail();
    }
}

bool Canvas3D::Impl::load_scene_texture(const std::string& path,
                        ID3D11ShaderResourceView** out_srv,
                        std::string& error,
                        bool* out_has_alpha) {
    if (!scene_texture_cache_enabled) return load_texture(path, out_srv, error, out_has_alpha);
    if (!out_srv) return false;
    *out_srv = nullptr;
    if (out_has_alpha) *out_has_alpha = false;

    const std::string key = normalized_texture_cache_key(path);
    auto cached = scene_texture_cache.find(key);
    if (cached != scene_texture_cache.end()) {
        ++scene_stats_value.texture_cache_hit_count;
        if (cached->second.failed) {
            error = cached->second.error;
            return false;
        }
        if (cached->second.texture) {
            cached->second.texture->AddRef();
            *out_srv = cached->second.texture;
            if (out_has_alpha) *out_has_alpha = cached->second.has_alpha;
            return true;
        }
    }

    ++scene_stats_value.texture_cache_miss_count;
    bool has_alpha = false;
    if (!load_texture(path, out_srv, error, &has_alpha)) {
        SceneTextureCacheEntry entry;
        entry.failed = true;
        entry.error = error;
        scene_texture_cache.emplace(key, std::move(entry));
        return false;
    }

    auto [inserted, was_inserted] = scene_texture_cache.try_emplace(key);
    if (!was_inserted) {
        release_com(*out_srv);
        error = "texture cache insertion conflict";
        return false;
    }
    inserted->second.texture = *out_srv;
    inserted->second.has_alpha = has_alpha;
    inserted->second.texture->AddRef();
    if (out_has_alpha) *out_has_alpha = has_alpha;
    return true;
}

void Canvas3D::Impl::release_scene_texture_cache() {
    for (auto& entry : scene_texture_cache) release_com(entry.second.texture);
    scene_texture_cache.clear();
    scene_texture_warning_keys.clear();
}

void Canvas3D::Impl::release_scene_model(SceneModelGpu& model) {
    for (GpuMaterial& material : model.materials) {
        release_com(material.texture);
        material.has_texture = false;
        material.texture_has_alpha = false;
    }
    model.materials.clear();
    model.parts.clear();
    release_com(model.vertex_buffer);
    release_com(model.index_buffer);
    release_com(model.instance_buffer);
    model.vertex_capacity = 0;
    model.dynamic_vertices = false;
    model.shared_model_key.clear();
    model.instance_capacity = 0;
    model.index_count = 0;
    model.center = {};
    model.radius = 1.0f;
}

void Canvas3D::Impl::notify_scene_loading_progress() {
    if (!wake_callback) return;
    bool expected = false;
    if (scene_wake_pending.compare_exchange_strong(expected, true)) wake_callback();
}

void Canvas3D::Impl::queue_scene_model_uploads(std::vector<CpuModelData> outputs) {
    if (outputs.empty()) return;
    {
        std::lock_guard<std::mutex> lock(scene_upload_mutex);
        for (CpuModelData& output : outputs) {
            scene_pending_uploads.push_back(std::move(output));
        }
    }
    notify_scene_loading_progress();
}

void Canvas3D::Impl::clear_pending_scene_model_uploads() {
    std::lock_guard<std::mutex> lock(scene_upload_mutex);
    scene_pending_uploads.clear();
    scene_wake_pending.store(false);
}

void Canvas3D::Impl::stop_scene_loader() {
    scene_cancel.store(true);
    if (scene_worker.joinable()) scene_worker.join();
    scene_worker_running.store(false);
    scene_cancel.store(false);
}

CpuModelData Canvas3D::Impl::copy_cpu_model(const std::string& path, const MlMeshData& data) {
#ifndef NDEBUG
    int remaining = debug_copy_cpu_model_throw_countdown.load(std::memory_order_relaxed);
    while (remaining > 0 &&
           !debug_copy_cpu_model_throw_countdown.compare_exchange_weak(
               remaining, remaining - 1, std::memory_order_relaxed)) {
    }
    if (remaining == 1) {
        throw std::runtime_error("debug injected CPU model copy failure");
    }
#endif
    CpuModelData out;
    out.path = path;
    out.scene_key = path;
    if (data.vertex_count == 0 || data.index_count == 0 || !data.vertices || !data.indices) {
        out.error = "model contains no renderable data";
        return out;
    }
    out.bounds_min = {data.bounds_min[0], data.bounds_min[1], data.bounds_min[2]};
    out.bounds_max = {data.bounds_max[0], data.bounds_max[1], data.bounds_max[2]};
    out.center = {data.center[0], data.center[1], data.center[2]};
    out.radius = std::max(data.radius, 0.001f);
    out.vertices.resize(data.vertex_count);
    for (size_t i = 0; i < data.vertex_count; ++i) {
        out.vertices[i] = {
            data.vertices[i].px, data.vertices[i].py, data.vertices[i].pz,
            data.vertices[i].nx, data.vertices[i].ny, data.vertices[i].nz,
            data.vertices[i].u, data.vertices[i].v
        };
    }
    out.indices.assign(data.indices, data.indices + data.index_count);
    if (data.parts && data.part_count > 0) {
        out.parts.reserve(data.part_count);
        for (size_t i = 0; i < data.part_count; ++i) {
            out.parts.push_back({data.parts[i].start_index, data.parts[i].index_count, data.parts[i].material_index});
        }
    } else {
        out.parts.push_back({0, static_cast<UINT>(data.index_count), 0});
    }
    size_t material_count = std::max<size_t>(data.material_count, 1);
    out.materials.resize(material_count);
    for (size_t i = 0; i < material_count; ++i) {
        const MlMaterial* src = data.materials && i < data.material_count ? &data.materials[i] : nullptr;
        if (!src) continue;
        out.materials[i].diffuse[0] = src->diffuse[0];
        out.materials[i].diffuse[1] = src->diffuse[1];
        out.materials[i].diffuse[2] = src->diffuse[2];
        out.materials[i].diffuse[3] = normalize_material_alpha(src->diffuse[3]);
        if (src->texture_path && *src->texture_path) out.materials[i].texture_path = src->texture_path;
    }
    out.ok = true;
    return out;
}

void Canvas3D::Impl::push_scene_load_log(std::string message) {
    std::lock_guard<std::mutex> lock(scene_log_mutex);
    scene_pending_logs.push_back(std::move(message));
}

void Canvas3D::Impl::start_scene_model_worker(std::vector<SceneModelLoadRequest> requests) {
    if (requests.empty()) return;
    if (scene_worker.joinable()) {
        if (scene_worker_running.load()) return;
        scene_worker.join();
    }
    std::map<std::string, std::vector<SceneModelLoadRequest>> requests_by_source;
    for (SceneModelLoadRequest& request : requests) {
        if (request.key.empty() || request.source_path.empty()) continue;
        requests_by_source[request.source_path].push_back(std::move(request));
    }
    if (requests_by_source.empty()) return;

    using SceneModelRequestGroup = std::pair<std::string, std::vector<SceneModelLoadRequest>>;
    std::vector<SceneModelRequestGroup> request_groups;
    request_groups.reserve(requests_by_source.size());
    for (auto& entry : requests_by_source) {
        request_groups.emplace_back(entry.first, std::move(entry.second));
    }

    scene_load_summary_pending = true;
    scene_model_load_started_at = std::chrono::steady_clock::now();
    scene_model_load_timer_active = true;
    scene_stats_value.model_load_seconds = 0.0;
    scene_stats_value.model_worker_count = scene_model_worker_count_for(request_groups.size());
    if (scene_model_worker_limit > 0) {
        scene_stats_value.model_worker_count = std::min(
            scene_stats_value.model_worker_count,
            std::max<size_t>(1, scene_model_worker_limit));
    }
    scene_model_worker_count_value.store(scene_stats_value.model_worker_count);
    scene_stats_value.texture_cache_hit_count = 0;
    scene_stats_value.texture_cache_miss_count = 0;
    scene_worker_running.store(true);
    try {
        const size_t requested_worker_count = scene_stats_value.model_worker_count;
        scene_worker = std::thread(
            [this, request_groups = std::move(request_groups),
             requested_worker_count]() mutable noexcept {
                auto safe_log = [this](std::string message) noexcept {
                    try {
                        push_scene_load_log(std::move(message));
                    } catch (...) {
                    }
                };
                auto finish = [this]() noexcept {
                    scene_worker_running.store(false);
                    try {
                        notify_scene_loading_progress();
                    } catch (...) {
                    }
                };
                auto queue_group_failure =
                    [this, &safe_log](const SceneModelRequestGroup& group,
                                     const std::string& error) noexcept {
                        try {
                            std::vector<CpuModelData> outputs;
                            outputs.reserve(group.second.size());
                            for (const SceneModelLoadRequest& request : group.second) {
                                CpuModelData failed;
                                failed.path = group.first;
                                failed.scene_key = request.key;
                                failed.error = error;
                                outputs.push_back(std::move(failed));
                            }
                            queue_scene_model_uploads(std::move(outputs));
                        } catch (const std::exception& failure_error) {
                            safe_log(
                                "[warn]canvas3D.cpp: failed to queue scene model failure: " +
                                std::string(failure_error.what()));
                        } catch (...) {
                            safe_log(
                                "[warn]canvas3D.cpp: failed to queue scene model failure: "
                                "unknown worker error");
                        }
                    };

                try {
                    std::string loader_error;
                    if (!scene_loader.prepare(loader_error)) {
                        safe_log(
                            "[warn]canvas3D.cpp: scene model loader initialization failed: " +
                            loader_error);
                        if (!scene_cancel.load()) {
                            for (const SceneModelRequestGroup& group : request_groups) {
                                queue_group_failure(group, loader_error);
                            }
                        }
                        scene_model_worker_count_value.store(1);
                        finish();
                        return;
                    }

                    std::atomic<size_t> next_group{0};
                    auto process_groups = [this, &request_groups, &next_group,
                                           &queue_group_failure,
                                           &safe_log]() noexcept {
                        while (!scene_cancel.load()) {
                            const size_t group_index = next_group.fetch_add(1);
                            if (group_index >= request_groups.size()) return;

                            SceneModelRequestGroup& group = request_groups[group_index];
                            const std::string& path = group.first;
                            std::vector<SceneModelLoadRequest>& source_requests = group.second;
                            try {
                                const std::string progress =
                                    std::to_string(group_index + 1) + "/" +
                                    std::to_string(request_groups.size());
                                CpuModelData source_cpu;
                                source_cpu.path = path;
                                source_cpu.scene_key = path;
                                MlMeshData data = {};
                                ModelDataGuard data_guard(scene_loader, data);
                                std::string error;
                                if (scene_loader.load(path, data, error)) {
                                    data_guard.mark_loaded();
                                    source_cpu = copy_cpu_model(path, data);
                                    if (!source_cpu.ok) {
                                        safe_log(
                                            "[warn]canvas3D.cpp: failed to read scene model " +
                                            progress + ": " + path + ": " +
                                            source_cpu.error);
                                    }
                                } else {
                                    source_cpu.error = error;
                                    safe_log(
                                        "[warn]canvas3D.cpp: failed to read scene model " +
                                        progress + ": " + path + ": " + error);
                                }
                                if (scene_cancel.load()) return;

                                std::vector<CpuModelData> outputs;
                                outputs.reserve(source_requests.size());
                                if (!source_cpu.ok) {
                                    for (const SceneModelLoadRequest& request : source_requests) {
                                        CpuModelData failed;
                                        failed.path = path;
                                        failed.scene_key = request.key;
                                        failed.error = source_cpu.error;
                                        outputs.push_back(std::move(failed));
                                    }
                                } else {
                                    const SceneModelLoadRequest* regular_request = nullptr;
                                    std::optional<PutBetweenSourceTemplate> put_between_source;
                                    std::vector<CpuModelData> derived_models;
                                    derived_models.reserve(source_requests.size());
                                    for (const SceneModelLoadRequest& request : source_requests) {
                                        if (!request.put_between.enabled) {
                                            regular_request = &request;
                                            continue;
                                        }
                                        if (!put_between_source) {
                                            put_between_source.emplace(
                                                prepare_put_between_source(source_cpu));
                                        }
                                        CpuModelData derived = derive_put_between_model(
                                            source_cpu, *put_between_source, request);
                                        if (!derived.ok) {
                                            safe_log(
                                                "[warn]canvas3D.cpp: failed to deform "
                                                "PutBetween model: " + path + ": " +
                                                derived.error);
                                        }
                                        derived_models.push_back(std::move(derived));
                                        if (scene_cancel.load()) return;
                                    }
                                    if (regular_request) {
                                        source_cpu.scene_key = regular_request->key;
                                        outputs.push_back(std::move(source_cpu));
                                    }
                                    for (CpuModelData& derived : derived_models) {
                                        outputs.push_back(std::move(derived));
                                    }
                                }
                                queue_scene_model_uploads(std::move(outputs));
                            } catch (const std::exception& error) {
                                const std::string detail = error.what();
                                safe_log(
                                    "[warn]canvas3D.cpp: scene model worker failed: " +
                                    path + ": " + detail);
                                queue_group_failure(group, detail);
                            } catch (...) {
                                safe_log(
                                    "[warn]canvas3D.cpp: scene model worker failed: " +
                                    path + ": unknown worker error");
                                queue_group_failure(group, "unknown worker error");
                            }
                        }
                    };

                    std::vector<std::thread> helpers;
                    helpers.reserve(
                        requested_worker_count > 0 ? requested_worker_count - 1 : 0);
                    for (size_t worker = 1; worker < requested_worker_count; ++worker) {
                        try {
                            helpers.emplace_back(process_groups);
                        } catch (const std::exception& error) {
                            safe_log(
                                "[warn]canvas3D.cpp: scene model worker count reduced: " +
                                std::string(error.what()));
                            break;
                        }
                    }
                    scene_model_worker_count_value.store(helpers.size() + 1);
                    process_groups();
                    for (std::thread& helper : helpers) helper.join();
                } catch (const std::exception& error) {
                    safe_log(
                        "[warn]canvas3D.cpp: scene model worker failed: " +
                        std::string(error.what()));
                    if (!scene_cancel.load()) {
                        for (const SceneModelRequestGroup& group : request_groups) {
                            queue_group_failure(group, error.what());
                        }
                    }
                } catch (...) {
                    safe_log(
                        "[warn]canvas3D.cpp: scene model worker failed: "
                        "unknown worker error");
                    if (!scene_cancel.load()) {
                        for (const SceneModelRequestGroup& group : request_groups) {
                            queue_group_failure(group, "unknown worker error");
                        }
                    }
                }
                finish();
            });
    } catch (const std::exception& e) {
        const std::string error = "failed to start scene model worker: " + std::string(e.what());
        for (auto& entry : scene_models) {
            if (entry.second.state != SceneModelGpu::State::Pending) continue;
            entry.second.state = SceneModelGpu::State::Failed;
            entry.second.error = error;
        }
        scene_model_worker_count_value.store(0);
        scene_worker_running.store(false);
        push_scene_load_log("[warn]canvas3D.cpp: " + error);
        notify_scene_loading_progress();
    } catch (...) {
        const std::string error = "failed to start scene model worker: unknown error";
        for (auto& entry : scene_models) {
            if (entry.second.state != SceneModelGpu::State::Pending) continue;
            entry.second.state = SceneModelGpu::State::Failed;
            entry.second.error = error;
        }
        scene_model_worker_count_value.store(0);
        scene_worker_running.store(false);
        push_scene_load_log("[warn]canvas3D.cpp: " + error);
        notify_scene_loading_progress();
    }
}

void Canvas3D::Impl::maybe_log_scene_model_load_summary() {
    if (!scene_load_summary_pending || scene_worker_running.load()) return;
    Canvas3DSceneStats stats = scene_stats();
    if (stats.model_ready_count + stats.model_failed_count < stats.model_path_count) return;
    if (scene_model_load_timer_active) {
        scene_stats_value.model_load_seconds = std::chrono::duration<double>(
            std::chrono::steady_clock::now() - scene_model_load_started_at).count();
        scene_model_load_timer_active = false;
        stats.model_load_seconds = scene_stats_value.model_load_seconds;
    }
    char elapsed[64] = {};
    std::snprintf(elapsed, sizeof(elapsed), "%.3f", stats.model_load_seconds);
    push_scene_load_log("[info]canvas3D.cpp: scene model loading finished in " +
                        std::string(elapsed) + " s: workers=" +
                        std::to_string(stats.model_worker_count) +
                        " loaded=" +
                        std::to_string(stats.model_ready_count) +
                        " failed=" + std::to_string(stats.model_failed_count) +
                        " total=" + std::to_string(stats.model_path_count) +
                        " texture_cache_hits=" + std::to_string(stats.texture_cache_hit_count) +
                        " texture_cache_misses=" + std::to_string(stats.texture_cache_miss_count));
    scene_load_summary_pending = false;
}

bool Canvas3D::Impl::upload_scene_model(const CpuModelData& cpu, std::string& error) {
    const std::string& scene_key = cpu.scene_key.empty() ? cpu.path : cpu.scene_key;
    auto it = scene_models.find(scene_key);
    if (it == scene_models.end()) return true;
    SceneModelGpu& model = it->second;
    release_scene_model(model);
    auto fail_upload = [&](const std::string& message) {
        release_scene_model(model);
        model.state = SceneModelGpu::State::Failed;
        model.error = message;
        error = message;
        return false;
    };
    try {
    if (!cpu.ok) {
        model.state = SceneModelGpu::State::Failed;
        model.error = cpu.error.empty() ? "model load failed" : cpu.error;
        return true;
    }
    if (cpu.vertices.size() > static_cast<size_t>(std::numeric_limits<UINT>::max() / sizeof(GpuVertex)) ||
        cpu.indices.size() > static_cast<size_t>(std::numeric_limits<UINT>::max() / sizeof(unsigned int))) {
        model.state = SceneModelGpu::State::Failed;
        model.error = "model is too large for a Direct3D 11 buffer";
        return true;
    }

    const SceneModelGpu* shared_model = nullptr;
    if (!cpu.shared_model_key.empty()) {
        auto shared_it = scene_models.find(cpu.shared_model_key);
        if (shared_it == scene_models.end() ||
            shared_it->second.state != SceneModelGpu::State::Ready ||
            !shared_it->second.index_buffer) {
            model.state = SceneModelGpu::State::Failed;
            model.error = "PutBetween base model is not ready";
            return true;
        }
        shared_model = &shared_it->second;
    }

    D3D11_BUFFER_DESC vb_desc = {};
    vb_desc.ByteWidth = static_cast<UINT>(cpu.vertices.size() * sizeof(GpuVertex));
    vb_desc.Usage = D3D11_USAGE_DEFAULT;
    vb_desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    D3D11_SUBRESOURCE_DATA vb_data = {};
    vb_data.pSysMem = cpu.vertices.data();
    HRESULT hr = device->CreateBuffer(&vb_desc, &vb_data, &model.vertex_buffer);
    if (FAILED(hr)) {
        return fail_upload(hresult_text("CreateBuffer(scene vertex)", hr));
    }

    if (shared_model) {
        model.index_buffer = shared_model->index_buffer;
        model.index_buffer->AddRef();
        model.parts = shared_model->parts;
        model.materials = shared_model->materials;
        for (GpuMaterial& material : model.materials) {
            if (material.texture) material.texture->AddRef();
        }
        model.index_count = shared_model->index_count;
    } else {
        D3D11_BUFFER_DESC ib_desc = {};
        ib_desc.ByteWidth = static_cast<UINT>(cpu.indices.size() * sizeof(unsigned int));
        ib_desc.Usage = D3D11_USAGE_DEFAULT;
        ib_desc.BindFlags = D3D11_BIND_INDEX_BUFFER;
        D3D11_SUBRESOURCE_DATA ib_data = {};
        ib_data.pSysMem = cpu.indices.data();
#ifndef NDEBUG
        int remaining =
            debug_scene_index_buffer_failure_countdown.load(std::memory_order_relaxed);
        while (remaining > 0 &&
               !debug_scene_index_buffer_failure_countdown.compare_exchange_weak(
                   remaining, remaining - 1, std::memory_order_relaxed)) {
        }
        hr = remaining == 1
            ? E_FAIL
            : device->CreateBuffer(&ib_desc, &ib_data, &model.index_buffer);
#else
        hr = device->CreateBuffer(&ib_desc, &ib_data, &model.index_buffer);
#endif
        if (FAILED(hr)) {
            return fail_upload(hresult_text("CreateBuffer(scene index)", hr));
        }

        model.parts = cpu.parts;
        model.materials.resize(std::max<size_t>(cpu.materials.size(), 1));
        for (size_t i = 0; i < model.materials.size(); ++i) {
            const CpuMaterial* src = i < cpu.materials.size() ? &cpu.materials[i] : nullptr;
            if (!src) continue;
            model.materials[i].diffuse[0] = src->diffuse[0];
            model.materials[i].diffuse[1] = src->diffuse[1];
            model.materials[i].diffuse[2] = src->diffuse[2];
            model.materials[i].diffuse[3] = normalize_material_alpha(src->diffuse[3]);
            if (!src->texture_path.empty()) {
                std::string texture_error;
                bool texture_has_alpha = false;
                if (load_scene_texture(src->texture_path, &model.materials[i].texture,
                                       texture_error, &texture_has_alpha)) {
                    model.materials[i].has_texture = true;
                    model.materials[i].texture_has_alpha = texture_has_alpha;
                } else {
                    if (scene_last_error.empty()) scene_last_error = texture_error;
                    const std::string texture_key =
                        normalized_texture_cache_key(src->texture_path);
                    if (scene_texture_warning_keys.insert(texture_key).second) {
                        push_scene_load_log(
                            "[WARN]canvas3D.cpp: scene texture warning: model=" +
                            cpu.path + "; " + texture_error);
                    }
                }
            }
        }
        model.index_count = static_cast<UINT>(cpu.indices.size());
    }
    model.bounds_min = cpu.bounds_min;
    model.bounds_max = cpu.bounds_max;
    model.center = cpu.center;
    model.radius = std::max(cpu.radius, 0.001f);
    model.state = SceneModelGpu::State::Ready;
    model.error.clear();
    return true;
    } catch (const std::exception& exception) {
        release_scene_model(model);
        model.state = SceneModelGpu::State::Failed;
        try {
            model.error = "scene model upload failed: " +
                std::string(exception.what());
            error = model.error;
        } catch (...) {
            model.error = "scene model upload failed";
            error = model.error;
        }
        return false;
    } catch (...) {
        return fail_upload("scene model upload failed: unknown error");
    }
}

void Canvas3D::Impl::upload_pending_scene_models() {
    KME_SCENE_PROFILE(Loading);
    scene_wake_pending.store(false);
    std::vector<CpuModelData> pending;
    {
        std::lock_guard<std::mutex> lock(scene_upload_mutex);
        pending.swap(scene_pending_uploads);
    }
    for (const CpuModelData& cpu : pending) {
        std::string error;
        if (!upload_scene_model(cpu, error)) {
            if (!error.empty()) {
                scene_last_error = error;
                push_scene_load_log("[warn]canvas3D.cpp: failed to upload scene model: " +
                                    cpu.path + ": " + error);
            }
        }
    }
    maybe_log_scene_model_load_summary();
}

namespace canvas3d_detail {

bool ModelLoaderClient::ensure_loaded(std::string& error) {
    if (library_) return true;

    DWORD first_error = ERROR_SUCCESS;
    library_ = runtime_paths::load_dll(L"model_loader.dll", &first_error);
    if (!library_) {
        error = "bin/model_loader.dll load failed: " + win32_error_text(first_error);
        return false;
    }

    api_version_ = runtime_paths::resolve_dll_function<MlApiVersionFn>(library_, "ml_api_version");
    load_model_ = runtime_paths::resolve_dll_function<MlLoadModelFn>(library_, "ml_load_model");
    free_model_ = runtime_paths::resolve_dll_function<MlFreeModelFn>(library_, "ml_free_model");
    get_last_error_ = runtime_paths::resolve_dll_function<MlGetLastErrorFn>(library_, "ml_get_last_error");
    if (!api_version_ || !load_model_ || !free_model_ || !get_last_error_) {
        error = "model_loader.dll is missing required entry points";
        FreeLibrary(library_);
        library_ = nullptr;
        return false;
    }
    if (api_version_() != 2) {
        error = "model_loader.dll API version is not supported";
        FreeLibrary(library_);
        library_ = nullptr;
        return false;
    }
    return true;
}

} // namespace canvas3d_detail

namespace canvas3d_detail {

#ifndef NDEBUG
size_t ModelLoaderClient::debug_free_count() noexcept {
    return debug_free_count_.load(std::memory_order_relaxed);
}
#endif

} // namespace canvas3d_detail

namespace canvas3d_detail {

#ifndef NDEBUG
size_t ModelLoaderClient::debug_successful_load_count() noexcept {
    return debug_successful_load_count_.load(std::memory_order_relaxed);
}
#endif

} // namespace canvas3d_detail

namespace canvas3d_detail {

#ifndef NDEBUG
void ModelLoaderClient::debug_reset_counts() noexcept {
    debug_successful_load_count_.store(0, std::memory_order_relaxed);
    debug_free_count_.store(0, std::memory_order_relaxed);
}
#endif

} // namespace canvas3d_detail

namespace canvas3d_detail {

void ModelLoaderClient::free_model(MlMeshData& data) {
#ifndef NDEBUG
    debug_free_count_.fetch_add(1, std::memory_order_relaxed);
#endif
    if (free_model_) free_model_(&data);
    else data = {};
}

} // namespace canvas3d_detail

namespace canvas3d_detail {

bool ModelLoaderClient::load(const std::string& path, MlMeshData& data, std::string& error) {
    if (!ensure_loaded(error)) return false;
    if (!load_model_(path.c_str(), &data)) {
        const char* loader_error = get_last_error_ ? get_last_error_() : nullptr;
        error = loader_error && *loader_error ? loader_error : "model_loader.dll could not load the model";
        return false;
    }
#ifndef NDEBUG
    debug_successful_load_count_.fetch_add(1, std::memory_order_relaxed);
#endif
    return true;
}

} // namespace canvas3d_detail

namespace canvas3d_detail {

bool ModelLoaderClient::prepare(std::string& error) {
    return ensure_loaded(error);
}

} // namespace canvas3d_detail
