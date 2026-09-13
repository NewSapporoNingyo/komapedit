/*
 * Copyright (c) 2026 Sapporo_ningyo
 * Licensed under Apache License 2.0; see LICENSE and NOTICE.
 */
#pragma once

#ifndef NDEBUG
#include "canvas3D.h"
#include <d3d11.h>
#include <array>
#include <chrono>

// Debug-only instrumentation. Query allocation happens before the benchmark;
// a busy ring slot is skipped, never flushed or waited for inside a frame.
class SceneFrameProfiler {
public:
    SceneFrameProfiler() = default;
    SceneFrameProfiler(const SceneFrameProfiler&) = delete;
    SceneFrameProfiler& operator=(const SceneFrameProfiler&) = delete;
    class Scope {
    public:
        explicit Scope(double* total) : total_(total) {
            if (total_) start_ = Clock::now();
        }
        ~Scope() {
            if (total_) *total_ += std::chrono::duration<double, std::milli>(Clock::now() - start_).count();
        }
        Scope(const Scope&) = delete;
        Scope& operator=(const Scope&) = delete;
    private:
        using Clock = std::chrono::steady_clock;
        double* total_;
        Clock::time_point start_{};
    };

    ~SceneFrameProfiler() { release(); }

    bool configure(ID3D11Device* device, bool enabled, std::string& error) {
        release();
        if (!enabled) return true;
        for (Query& query : queries_) {
            D3D11_QUERY_DESC desc{D3D11_QUERY_TIMESTAMP_DISJOINT, 0};
            HRESULT hr = device->CreateQuery(&desc, &query.disjoint);
            if (SUCCEEDED(hr)) {
                desc.Query = D3D11_QUERY_TIMESTAMP;
                hr = device->CreateQuery(&desc, &query.start);
                if (SUCCEEDED(hr)) hr = device->CreateQuery(&desc, &query.end);
            }
            if (FAILED(hr)) {
                error = "Failed to allocate scene GPU timestamp queries";
                release();
                return false;
            }
        }
        enabled_ = true;
        return true;
    }

    void begin_frame(ID3D11DeviceContext* context) {
        result = {};
        active_ = false;
        if (!enabled_) return;
        Query& query = queries_[slot_];
        if (query.pending) {
            D3D11_QUERY_DATA_TIMESTAMP_DISJOINT disjoint{};
            UINT64 start = 0, end = 0;
            if (context->GetData(query.disjoint, &disjoint, sizeof(disjoint), D3D11_ASYNC_GETDATA_DONOTFLUSH) != S_OK ||
                context->GetData(query.start, &start, sizeof(start), D3D11_ASYNC_GETDATA_DONOTFLUSH) != S_OK ||
                context->GetData(query.end, &end, sizeof(end), D3D11_ASYNC_GETDATA_DONOTFLUSH) != S_OK) {
                slot_ = (slot_ + 1) % queries_.size();
                return;
            }
            if (!disjoint.Disjoint && disjoint.Frequency && end >= start) {
                result.gpu_ms = static_cast<double>(end - start) * 1000.0 / static_cast<double>(disjoint.Frequency);
            }
            query.pending = false;
        }
        context->Begin(query.disjoint);
        context->End(query.start);
        active_ = true;
    }

    void end_frame(ID3D11DeviceContext* context) {
        if (!active_) return;
        Query& query = queries_[slot_];
        context->End(query.end);
        context->End(query.disjoint);
        query.pending = true;
        active_ = false;
        slot_ = (slot_ + 1) % queries_.size();
    }

    Scope measure(Canvas3DSceneFrameStage stage) {
        return Scope(enabled_ ? &result.cpu_ms[static_cast<size_t>(stage)] : nullptr);
    }
    void draw() { if (enabled_) ++result.draw_calls; }
    void upload(size_t bytes) { if (enabled_) result.uploaded_bytes += bytes; }
    Canvas3DSceneFrameProfile result;

private:
    struct Query {
        ID3D11Query* disjoint = nullptr;
        ID3D11Query* start = nullptr;
        ID3D11Query* end = nullptr;
        bool pending = false;
    };
    void release() {
        for (Query& query : queries_) {
            if (query.disjoint) query.disjoint->Release();
            if (query.start) query.start->Release();
            if (query.end) query.end->Release();
            query = {};
        }
        enabled_ = false;
        active_ = false;
        slot_ = 0;
    }
    std::array<Query, 8> queries_{};
    size_t slot_ = 0;
    bool enabled_ = false;
    bool active_ = false;
};

#define KME_SCENE_PROFILE(stage) auto profile_scope_##stage = scene_frame_profiler.measure(Canvas3DSceneFrameStage::stage)
#else
#define KME_SCENE_PROFILE(stage) ((void)0)
#endif
