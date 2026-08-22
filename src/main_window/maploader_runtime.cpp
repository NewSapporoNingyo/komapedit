/*
 * Copyright (c) 2026 Sapporo_ningyo
 *
 * Licensed under Apache License 2.0; see LICENSE and NOTICE.
 */

#include "maploader.h"
#include "runtime_paths.h"

#include <string>
#include <system_error>

namespace {

#define KME_MAPLOADER_FUNCTIONS(X) \
    X(set_log_callback, kv_set_log_callback) \
    X(load_map_ex, kv_load_map_ex) \
    X(probe_file_kind, kv_probe_file_kind) \
    X(resolve_scenario_routes, kv_resolve_scenario_routes) \
    X(free_scenario_candidates, kv_free_scenario_candidates) \
    X(generate_geometry, kv_generate_geometry) \
    X(generate_scene_geometry, kv_generate_scene_geometry) \
    X(get_map_snapshot, kv_get_map_snapshot) \
    X(get_scene_geometry_snapshot, kv_get_scene_geometry_snapshot) \
    X(get_edit_target_typed, kv_get_edit_target_typed) \
    X(get_source_text, kv_get_source_text) \
    X(edit_dry_run_typed, kv_edit_dry_run_typed) \
    X(edit_apply_to_memory_typed, kv_edit_apply_to_memory_typed) \
    X(edit_reset_memory, kv_edit_reset_memory) \
    X(edit_apply_typed, kv_edit_apply_typed) \
    X(edit_commit_typed, kv_edit_commit_typed) \
    X(get_last_error, kv_get_last_error) \
    X(free_handle, kv_free) \
    X(free_string, kv_free_string)

std::string system_error_text(DWORD code) {
    if (code == ERROR_SUCCESS) return {};
    return std::error_code(static_cast<int>(code), std::system_category()).message();
}

class MaploaderRuntime {
public:
    MaploaderRuntime() {
        DWORD error_code = ERROR_SUCCESS;
        library_ = runtime_paths::load_dll(L"maploader.dll", &error_code);
        if (!library_) {
            load_error_ = "bin/maploader.dll load failed";
            const std::string detail = system_error_text(error_code);
            if (!detail.empty()) load_error_ += ": " + detail;
            return;
        }

        if (!resolve(api_version, "kv_api_version")) {
            FreeLibrary(library_);
            library_ = nullptr;
            return;
        }
        const std::uint32_t loaded_version = api_version();
        if (loaded_version != KV_MAPLOADER_API_VERSION) {
            load_error_ = "bin/maploader.dll API version mismatch: executable requires " +
                std::to_string(KV_MAPLOADER_API_VERSION) + ", DLL provides " +
                std::to_string(loaded_version);
            FreeLibrary(library_);
            library_ = nullptr;
            return;
        }

#define KME_RESOLVE_FUNCTION(member, symbol) \
        if (!resolve(member, #symbol)) { \
            FreeLibrary(library_); \
            library_ = nullptr; \
            return; \
        }
        KME_MAPLOADER_FUNCTIONS(KME_RESOLVE_FUNCTION)
#undef KME_RESOLVE_FUNCTION
    }

    bool available() const {
        return library_ != nullptr;
    }

    const char* load_error() const {
        return load_error_.c_str();
    }

#define KME_DECLARE_FUNCTION(member, symbol) decltype(&symbol) member = nullptr;
    KME_MAPLOADER_FUNCTIONS(KME_DECLARE_FUNCTION)
#undef KME_DECLARE_FUNCTION
    decltype(&kv_api_version) api_version = nullptr;

private:
    template <typename Function>
    bool resolve(Function& function, const char* name) {
        function = runtime_paths::resolve_dll_function<Function>(library_, name);
        if (function) return true;
        load_error_ = "bin/maploader.dll is missing required entry point: ";
        load_error_ += name;
        return false;
    }

    // Keep maploader and its returned buffers/strings valid until process exit.
    HMODULE library_ = nullptr;
    std::string load_error_;
};

MaploaderRuntime& maploader_runtime() {
    static MaploaderRuntime runtime;
    return runtime;
}

} // namespace

extern "C" {

uint32_t kv_api_version(void) {
    MaploaderRuntime& runtime = maploader_runtime();
    return runtime.available() ? runtime.api_version() : 0u;
}

void kv_set_log_callback(KvLogCallback callback) {
    MaploaderRuntime& runtime = maploader_runtime();
    if (runtime.available()) runtime.set_log_callback(callback);
}

void* kv_load_map_ex(const char* path, double unit_distance, unsigned flags) {
    MaploaderRuntime& runtime = maploader_runtime();
    return runtime.available() ? runtime.load_map_ex(path, unit_distance, flags) : nullptr;
}

int kv_probe_file_kind(const char* path) {
    MaploaderRuntime& runtime = maploader_runtime();
    return runtime.available() ? runtime.probe_file_kind(path) : KV_FILE_KIND_UNKNOWN;
}

const KvScenarioRouteCandidate* kv_resolve_scenario_routes(
    const char* scenario_path, uint64_t* out_count) {
    MaploaderRuntime& runtime = maploader_runtime();
    return runtime.available()
        ? runtime.resolve_scenario_routes(scenario_path, out_count)
        : nullptr;
}

void kv_free_scenario_candidates(const KvScenarioRouteCandidate* candidates) {
    if (!candidates) return;
    MaploaderRuntime& runtime = maploader_runtime();
    if (runtime.available()) runtime.free_scenario_candidates(candidates);
}

int kv_generate_geometry(
    void* handle,
    double unit_distance,
    int has_arbitrary_distribution,
    double arbitrary_start,
    double arbitrary_end,
    double arbitrary_step) {
    MaploaderRuntime& runtime = maploader_runtime();
    return runtime.available()
        ? runtime.generate_geometry(
            handle,
            unit_distance,
            has_arbitrary_distribution,
            arbitrary_start,
            arbitrary_end,
            arbitrary_step)
        : 0;
}

int kv_generate_scene_geometry(
    void* handle,
    double unit_distance,
    double min_step,
    double max_step,
    double max_angle_degrees,
    double max_chord_error) {
    MaploaderRuntime& runtime = maploader_runtime();
    return runtime.available()
        ? runtime.generate_scene_geometry(
            handle,
            unit_distance,
            min_step,
            max_step,
            max_angle_degrees,
            max_chord_error)
        : 0;
}

int kv_get_map_snapshot(void* handle, uint32_t version,
                        KvMapSnapshot* out_snapshot, uint64_t out_size) {
    MaploaderRuntime& runtime = maploader_runtime();
    return runtime.available()
        ? runtime.get_map_snapshot(handle, version, out_snapshot, out_size)
        : 0;
}

int kv_get_scene_geometry_snapshot(void* handle, uint32_t version,
                                   KvSceneGeometrySnapshot* out_snapshot,
                                   uint64_t out_size) {
    MaploaderRuntime& runtime = maploader_runtime();
    return runtime.available()
        ? runtime.get_scene_geometry_snapshot(handle, version, out_snapshot, out_size)
        : 0;
}

int kv_get_edit_target_typed(void* handle, KvUtf8View edit_id,
                             KvEditTargetSnapshot* out_target,
                             uint64_t out_size) {
    MaploaderRuntime& runtime = maploader_runtime();
    return runtime.available()
        ? runtime.get_edit_target_typed(handle, edit_id, out_target, out_size)
        : 0;
}

const char* kv_get_source_text(void* handle, const char* file_path) {
    MaploaderRuntime& runtime = maploader_runtime();
    return runtime.available() ? runtime.get_source_text(handle, file_path) : nullptr;
}

int kv_edit_dry_run_typed(void* handle, const KvEditBatch* batch,
                          KvEditReportSnapshot* out_report,
                          uint64_t out_size) {
    MaploaderRuntime& runtime = maploader_runtime();
    return runtime.available()
        ? runtime.edit_dry_run_typed(handle, batch, out_report, out_size)
        : 0;
}

int kv_edit_apply_to_memory_typed(void* handle, const KvEditBatch* batch,
                                  KvEditReportSnapshot* out_report,
                                  uint64_t out_size) {
    MaploaderRuntime& runtime = maploader_runtime();
    return runtime.available()
        ? runtime.edit_apply_to_memory_typed(handle, batch, out_report, out_size)
        : 0;
}

int kv_edit_reset_memory(void* handle) {
    MaploaderRuntime& runtime = maploader_runtime();
    return runtime.available() ? runtime.edit_reset_memory(handle) : 0;
}

int kv_edit_apply_typed(void* handle, const KvEditBatch* batch,
                        KvEditReportSnapshot* out_report,
                        uint64_t out_size) {
    MaploaderRuntime& runtime = maploader_runtime();
    return runtime.available()
        ? runtime.edit_apply_typed(handle, batch, out_report, out_size)
        : 0;
}

int kv_edit_commit_typed(void* handle, KvEditReportSnapshot* out_report,
                         uint64_t out_size) {
    MaploaderRuntime& runtime = maploader_runtime();
    return runtime.available()
        ? runtime.edit_commit_typed(handle, out_report, out_size)
        : 0;
}

const char* kv_get_last_error(void) {
    MaploaderRuntime& runtime = maploader_runtime();
    return runtime.available() ? runtime.get_last_error() : runtime.load_error();
}

void kv_free(void* handle) {
    if (!handle) return;
    MaploaderRuntime& runtime = maploader_runtime();
    if (runtime.available()) runtime.free_handle(handle);
}

void kv_free_string(const char* text) {
    if (!text) return;
    MaploaderRuntime& runtime = maploader_runtime();
    if (runtime.available()) runtime.free_string(text);
}

} // extern "C"

#undef KME_MAPLOADER_FUNCTIONS
