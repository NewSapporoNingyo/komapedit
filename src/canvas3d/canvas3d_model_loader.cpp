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
#include <memory>
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

std::vector<GpuVertex> copy_model_vertices(const MlMeshData& data) {
    std::vector<GpuVertex> vertices;
    vertices.reserve(data.vertex_count);
    for (size_t i = 0; i < data.vertex_count; ++i) {
        const auto& vertex = data.vertices[i];
        vertices.push_back({vertex.px, vertex.py, vertex.pz, vertex.nx, vertex.ny, vertex.nz,
                            vertex.u, vertex.v});
    }
    return vertices;
}

float normalize_material_alpha(float value) {
    // BVE .x models commonly use 0.99/0.999999 for opaque alpha-tested textures.
    return value >= k_material_opaque_alpha_threshold ? 1.0f : value;
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
    std::filesystem::path normalized = utf8_to_wide(path);
    if (normalized.is_relative()) {
        std::error_code ec;
        auto absolute = std::filesystem::absolute(normalized, ec);
        if (!ec) normalized = std::move(absolute);
    }
    return wide_to_utf8(normalized.lexically_normal().native());
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
            height > D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION) {
            error = "texture dimensions exceed the Direct3D 11 limit: " + path;
            return fail();
        }
        constexpr std::uint64_t maximum_bytes =
            std::uint64_t{D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION} *
            D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION * 4;
        static_assert(maximum_bytes <= std::numeric_limits<UINT>::max() &&
                      maximum_bytes <= std::numeric_limits<size_t>::max());
        const UINT row_stride = width * 4;
        const size_t pixel_bytes = static_cast<size_t>(row_stride) * height;
#ifndef NDEBUG
        int remaining =
            debug_faults.texture_allocation_throw_countdown.load(std::memory_order_relaxed);
        while (remaining > 0 &&
               !debug_faults.texture_allocation_throw_countdown.compare_exchange_weak(
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
        *out_srv = cached->second.texture;
        (*out_srv)->AddRef();
        if (out_has_alpha) *out_has_alpha = cached->second.has_alpha;
        return true;
    }

    ++scene_stats_value.texture_cache_miss_count;
    SceneTextureCacheEntry entry;
    if (!load_texture(path, out_srv, error, &entry.has_alpha)) {
        entry.failed = true;
        entry.error = error;
        scene_texture_cache.emplace(key, std::move(entry));
        return false;
    }

    entry.texture = *out_srv;
    auto& cached_entry = scene_texture_cache.emplace(key, std::move(entry)).first->second;
    cached_entry.texture->AddRef();
    if (out_has_alpha) *out_has_alpha = cached_entry.has_alpha;
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

void Canvas3D::Impl::notify_scene_loading_progress() noexcept {
    if (!wake_callback) return;
    bool expected = false;
    if (!scene_wake_pending.compare_exchange_strong(expected, true)) return;
    try {
        wake_callback();
    } catch (...) {
        scene_wake_pending.store(false);
        log_scene_worker_failure({"[warn]canvas3D.cpp: scene wake callback failed"});
    }
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
    queued_scene_model_requests.clear();
    active_scene_model_requests.clear();
    clear_pending_scene_model_uploads();
}

CpuModelData Canvas3D::Impl::copy_cpu_model(const std::string& path, const MlMeshData& data) {
#ifndef NDEBUG
    int remaining = debug_faults.copy_cpu_model_throw_countdown.load(std::memory_order_relaxed);
    while (remaining > 0 &&
           !debug_faults.copy_cpu_model_throw_countdown.compare_exchange_weak(
               remaining, remaining - 1, std::memory_order_relaxed)) {
    }
    if (remaining == 1) {
        throw std::runtime_error("debug injected CPU model copy failure");
    }
#endif
    CpuModelData out;
    out.path = path;
    out.scene_key = path;
    out.bounds_min = {data.bounds_min[0], data.bounds_min[1], data.bounds_min[2]};
    out.bounds_max = {data.bounds_max[0], data.bounds_max[1], data.bounds_max[2]};
    out.center = {data.center[0], data.center[1], data.center[2]};
    out.radius = data.radius;
    out.vertices = copy_model_vertices(data);
    out.indices.assign(data.indices, data.indices + data.index_count);
    out.parts.reserve(data.part_count);
    for (size_t i = 0; i < data.part_count; ++i) {
        const MlMeshPart& part = data.parts[i];
        out.parts.push_back({part.start_index, part.index_count, part.material_index});
    }
    out.materials.resize(data.material_count);
    for (size_t i = 0; i < data.material_count; ++i) {
        const MlMaterial& src = data.materials[i];
        auto& dest = out.materials[i];
        std::copy_n(src.diffuse, 3, dest.diffuse);
        dest.diffuse[3] = normalize_material_alpha(src.diffuse[3]);
        if (src.texture_path) dest.texture_path = src.texture_path;
    }
    out.ok = true;
    return out;
}

void Canvas3D::Impl::push_scene_load_log(std::string message) {
    std::lock_guard<std::mutex> lock(scene_log_mutex);
    scene_pending_logs.push_back(std::move(message));
}

void Canvas3D::Impl::log_scene_worker_failure(
    std::initializer_list<std::string_view> fragments) noexcept {
    try {
#ifndef NDEBUG
        if (debug_faults.log_failure.exchange(false)) throw std::bad_alloc();
#endif
        std::string message;
        for (std::string_view fragment : fragments) message.append(fragment);
        push_scene_load_log(std::move(message));
    } catch (...) {}
}

bool Canvas3D::Impl::load_scene_model_data(const std::string& path, MlMeshData& data, std::string& error) {
    const auto status = ModelLoaderClient::load(path, data, error);
    if (status == ML_LOAD_WHITE_MATERIAL) log_scene_worker_failure({k_white_model_warning, path});
    return status != ML_LOAD_FAILED;
}

void Canvas3D::Impl::build_scene_model_group(SceneModelLoadGroup& group) {
    const std::string& path = group.requests.front().source_path;
    CpuModelData source;
    {
        MlMeshData data{};
        if (!load_scene_model_data(path, data, group.error)) return;
        ModelDataGuard guard(data);
        source = copy_cpu_model(path, data);
    }
    if (scene_cancel.load()) return;

    const auto regular = std::find_if(group.requests.begin(), group.requests.end(),
        [](const auto& request) { return !request.put_between.enabled; });
    group.outputs.reserve(group.requests.size());
    if (regular != group.requests.end()) group.outputs.emplace_back();
    std::optional<PutBetweenSourceTemplate> prepared;
    for (const auto& request : group.requests) {
        if (!request.put_between.enabled) continue;
        if (!prepared) prepared.emplace(prepare_put_between_source(source));
        group.outputs.push_back(derive_put_between_model(source, *prepared, request));
        if (scene_cancel.load()) return;
    }
    if (regular != group.requests.end()) {
        source.scene_key = regular->key;
        group.outputs.front() = std::move(source);
    }
}

void Canvas3D::Impl::start_scene_model_worker(std::vector<SceneModelLoadRequest> requests) {
    for (auto& request : requests) {
        if (active_scene_model_requests.count(request.key) == 0) {
            queued_scene_model_requests.try_emplace(request.key, std::move(request));
        }
    }
    if (scene_worker_running.load() || !active_scene_model_requests.empty() ||
        queued_scene_model_requests.empty()) return;
    if (scene_worker.joinable()) scene_worker.join();

    std::map<std::string, std::vector<SceneModelLoadRequest>> requests_by_source;
    for (auto& [key, request] : queued_scene_model_requests) {
        active_scene_model_requests.insert(key);
        requests_by_source[request.source_path].push_back(std::move(request));
    }
    queued_scene_model_requests.clear();

    std::list<SceneModelLoadGroup> groups;
    std::vector<decltype(groups)::iterator> work;
    work.reserve(requests_by_source.size());
    for (auto& entry : requests_by_source) {
        work.push_back(groups.emplace(groups.end()));
        work.back()->requests = std::move(entry.second);
    }
    size_t worker_count = scene_model_worker_count_for(work.size());
    if (scene_model_worker_limit > 0) worker_count = std::min(worker_count, scene_model_worker_limit);

    scene_load_summary_pending = true;
    scene_model_load_started_at = std::chrono::steady_clock::now();
    scene_model_load_timer_active = true;
    scene_stats_value.model_load_seconds = 0.0;
    scene_model_worker_count_value.store(worker_count);
    scene_stats_value.texture_cache_hit_count = 0;
    scene_stats_value.texture_cache_miss_count = 0;
    scene_worker_running.store(true);
    try {
        scene_worker = std::thread(
            [this, groups = std::move(groups), work = std::move(work), worker_count]() mutable {
                try {
                    std::string error;
                    if (!ModelLoaderClient::prepare(error)) throw std::runtime_error(error);
                } catch (...) {
                    const auto failure = std::current_exception();
                    scene_model_worker_count_value.store(1);
                    for (auto& group : groups) group.exception = failure;
                    {
                        std::lock_guard<std::mutex> lock(scene_upload_mutex);
                        scene_pending_uploads.splice(scene_pending_uploads.end(), groups);
                    }
                    scene_worker_running.store(false);
                    notify_scene_loading_progress();
                    return;
                }

                std::atomic<size_t> next{0};
                const auto process_groups = [&] {
                    while (!scene_cancel.load()) {
                        const size_t index = next.fetch_add(1);
                        if (index >= work.size()) return;
                        const auto group = work[index];
                        try {
                            build_scene_model_group(*group);
                        } catch (...) {
                            group->outputs.clear();
                            group->exception = std::current_exception();
                        }
                        if (scene_cancel.load()) return;
                        {
                            std::lock_guard<std::mutex> lock(scene_upload_mutex);
                            scene_pending_uploads.splice(scene_pending_uploads.end(), groups, group);
                        }
                        notify_scene_loading_progress();
                    }
                };

                std::array<std::thread, k_scene_model_max_workers - 1> helpers;
                size_t started = 0;
                for (; started + 1 < worker_count; ++started) {
                    try {
#ifndef NDEBUG
                        if (started + 1 == debug_faults.helper_start_failure_at.load()) {
                            throw std::runtime_error("debug injected helper thread start failure");
                        }
#endif
                        helpers[started] = std::thread(process_groups);
                    } catch (const std::exception& exception) {
                        log_scene_worker_failure({
                            "[warn]canvas3D.cpp: scene model worker count reduced: ", exception.what()});
                        break;
                    }
                }
                scene_model_worker_count_value.store(started + 1);
                process_groups();
                for (size_t i = 0; i < started; ++i) helpers[i].join();
                scene_worker_running.store(false);
                notify_scene_loading_progress();
            });
    } catch (const std::exception& exception) {
        scene_model_worker_count_value.store(0);
        scene_worker_running.store(false);
        const std::string error = "failed to start scene model worker: " + std::string(exception.what());
        for (const auto& key : active_scene_model_requests) {
            auto& model = scene_models.at(key);
            model.state = SceneModelGpu::State::Failed;
            model.error = error;
        }
        active_scene_model_requests.clear();
        log_scene_worker_failure({"[warn]canvas3D.cpp: ", error});
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
    auto& current = it->second;
    SceneModelGpu model;
    model.preview_only = current.preview_only;
    const auto release = [this](SceneModelGpu* pending) { release_scene_model(*pending); };
    const std::unique_ptr<SceneModelGpu, decltype(release)> cleanup(&model, release);
    const auto mark_failed = [&](std::string_view message) {
        if (current.state == SceneModelGpu::State::Ready) return;
        current.state = SceneModelGpu::State::Failed;
        current.error = message;
    };
    const auto fail_upload = [&](std::string message) {
        mark_failed(message);
        error = std::move(message);
        return false;
    };
    const auto report_exception = [&](std::string_view message) {
        if (current.state != SceneModelGpu::State::Ready) current.state = SceneModelGpu::State::Failed;
        log_scene_worker_failure({"[warn]canvas3D.cpp: Failed to upload scene model ", cpu.path, ": ", message});
        return false;
    };
    try {
        if (!cpu.ok) {
            mark_failed(cpu.error.empty() ? "model load failed" : cpu.error);
            return true;
        }
        if (cpu.vertices.size() > static_cast<size_t>(std::numeric_limits<UINT>::max() / sizeof(GpuVertex)) ||
            cpu.indices.size() > static_cast<size_t>(std::numeric_limits<UINT>::max() / sizeof(unsigned int))) {
            mark_failed("model is too large for a Direct3D 11 buffer");
            return true;
        }

        const SceneModelGpu* shared_model = nullptr;
        if (!cpu.shared_model_key.empty()) {
            auto shared_it = scene_models.find(cpu.shared_model_key);
            if (shared_it == scene_models.end() ||
                shared_it->second.state != SceneModelGpu::State::Ready ||
                !shared_it->second.index_buffer) {
                mark_failed("PutBetween base model is not ready");
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
                debug_faults.index_buffer_failure_countdown.load(std::memory_order_relaxed);
            while (remaining > 0 &&
                   !debug_faults.index_buffer_failure_countdown.compare_exchange_weak(
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
            model.materials.resize(cpu.materials.size());
            for (size_t i = 0; i < cpu.materials.size(); ++i) {
                const CpuMaterial& src = cpu.materials[i];
                GpuMaterial& material = model.materials[i];
                std::copy_n(src.diffuse, 4, material.diffuse);
                if (!src.texture_path.empty()) {
                    std::string texture_error;
                    bool texture_has_alpha = false;
                    if (load_scene_texture(src.texture_path, &material.texture,
                                           texture_error, &texture_has_alpha)) {
                        material.has_texture = true;
                        material.texture_has_alpha = texture_has_alpha;
                    } else {
                        const std::string texture_key =
                            normalized_texture_cache_key(src.texture_path);
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
        model.radius = cpu.radius;
        model.state = SceneModelGpu::State::Ready;
        std::swap(current, model);
        return true;
    } catch (const std::exception& exception) {
        return report_exception(exception.what());
    } catch (...) {
        return report_exception("unknown upload error");
    }
}

void Canvas3D::Impl::upload_pending_scene_models() {
    KME_SCENE_PROFILE(Loading);
    scene_wake_pending.store(false);
    std::list<SceneModelLoadGroup> pending;
    {
        std::lock_guard<std::mutex> lock(scene_upload_mutex);
        pending.swap(scene_pending_uploads);
    }
    for (const auto& group : pending) {
        for (const auto& request : group.requests) active_scene_model_requests.erase(request.key);
        if (group.outputs.empty()) {
            const auto fail_group = [&](std::string_view error) {
                for (const auto& request : group.requests) {
                    const auto found = scene_models.find(request.key);
                    if (found == scene_models.end()) continue;
                    release_scene_model(found->second);
                    found->second.state = SceneModelGpu::State::Failed;
                    found->second.error = error;
                }
                log_scene_worker_failure({"[warn]canvas3D.cpp: failed to load scene model ",
                    group.requests.front().source_path, ": ", error});
            };
            if (group.exception) {
                try {
                    std::rethrow_exception(group.exception);
                } catch (const std::exception& exception) {
                    fail_group(exception.what());
                } catch (...) {
                    fail_group("unknown worker error");
                }
            } else {
                fail_group(group.error);
            }
            continue;
        }
        for (const CpuModelData& cpu : group.outputs) {
            if (!cpu.ok) log_scene_worker_failure({
                "[warn]canvas3D.cpp: failed to deform PutBetween model: ", cpu.path, ": ", cpu.error});
            std::string error;
            if (!upload_scene_model(cpu, error) && !error.empty()) {
                push_scene_load_log("[warn]canvas3D.cpp: failed to upload scene model: " +
                                    cpu.path + ": " + error);
            }
        }
    }
    start_scene_model_worker({});
    maybe_log_scene_model_load_summary();
}

namespace canvas3d_detail {
namespace {

struct ModelLoaderApi {
    decltype(&ml_load_model) load;
    decltype(&ml_free_model) free;
    decltype(&ml_get_last_error) last_error;
};

const ModelLoaderApi& model_loader_api() {
    // Keep the successful binding alive through process teardown.
    static const auto api = [] {
        DWORD code = ERROR_SUCCESS;
        HMODULE library = runtime_paths::load_dll(L"model_loader.dll", &code);
        if (!library) {
            throw std::runtime_error("bin/model_loader.dll load failed: " + win32_error_text(code));
        }
        const auto version = runtime_paths::resolve_dll_function<decltype(&ml_api_version)>(
            library, "ml_api_version");
        const ModelLoaderApi functions{
            runtime_paths::resolve_dll_function<decltype(&ml_load_model)>(library, "ml_load_model"),
            runtime_paths::resolve_dll_function<decltype(&ml_free_model)>(library, "ml_free_model"),
            runtime_paths::resolve_dll_function<decltype(&ml_get_last_error)>(library, "ml_get_last_error"),
        };
        if (!version || !functions.load || !functions.free || !functions.last_error) {
            FreeLibrary(library);
            throw std::runtime_error("model_loader.dll is missing required entry points");
        }
        if (version() != ML_API_VERSION) {
            FreeLibrary(library);
            throw std::runtime_error("model_loader.dll API version is not supported");
        }
        return functions;
    }();
    return api;
}

const ModelLoaderApi* bind_model_loader(std::string& error) {
    try {
        return &model_loader_api();
    } catch (const std::runtime_error& exception) {
        error = exception.what();
        return nullptr;
    }
}

} // namespace

#ifndef NDEBUG
size_t ModelLoaderClient::debug_free_count() noexcept {
    return debug_free_count_.load(std::memory_order_relaxed);
}

size_t ModelLoaderClient::debug_successful_load_count() noexcept {
    return debug_successful_load_count_.load(std::memory_order_relaxed);
}

void ModelLoaderClient::debug_reset_counts() noexcept {
    debug_successful_load_count_.store(0, std::memory_order_relaxed);
    debug_free_count_.store(0, std::memory_order_relaxed);
}
#endif

bool ModelLoaderClient::prepare(std::string& error) {
    return bind_model_loader(error) != nullptr;
}

MlModelLoadStatus ModelLoaderClient::load(const std::string& path, MlMeshData& data, std::string& error) {
    const auto* api = bind_model_loader(error);
    if (!api) return ML_LOAD_FAILED;
    const auto status = static_cast<MlModelLoadStatus>(api->load(path.c_str(), &data));
    if (status == ML_LOAD_FAILED) {
        const char* reason = api->last_error();
        error = *reason ? reason : "model_loader.dll could not load the model";
        return status;
    }
#ifndef NDEBUG
    debug_successful_load_count_.fetch_add(1, std::memory_order_relaxed);
#endif
    return status;
}

void ModelLoaderClient::free_model(MlMeshData& data) noexcept {
#ifndef NDEBUG
    debug_free_count_.fetch_add(1, std::memory_order_relaxed);
#endif
    model_loader_api().free(&data);
}

} // namespace canvas3d_detail
