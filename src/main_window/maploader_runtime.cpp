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
    X(load_map, kv_load_map) \
    X(load_map_ex, kv_load_map_ex) \
    X(generate_geometry, kv_generate_geometry) \
    X(generate_scene_geometry, kv_generate_scene_geometry) \
    X(get_owntrack_buffer, kv_get_owntrack_buffer) \
    X(get_curveradius_buffer, kv_get_curveradius_buffer) \
    X(get_othertrack_count, kv_get_othertrack_count) \
    X(get_othertrack_key, kv_get_othertrack_key) \
    X(get_othertrack_buffer, kv_get_othertrack_buffer) \
    X(get_structure_puts, kv_get_structure_puts) \
    X(get_preview_cache_hit, kv_get_preview_cache_hit) \
    X(get_ir_json_ex, kv_get_ir_json_ex) \
    X(get_ir_json, kv_get_ir_json) \
    X(get_edit_target_info, kv_get_edit_target_info) \
    X(edit_dry_run, kv_edit_dry_run) \
    X(edit_apply_to_memory, kv_edit_apply_to_memory) \
    X(edit_reset_memory, kv_edit_reset_memory) \
    X(edit_apply, kv_edit_apply) \
    X(edit_commit, kv_edit_commit) \
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

private:
    template <typename Function>
    bool resolve(Function& function, const char* name) {
        function = reinterpret_cast<Function>(GetProcAddress(library_, name));
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

void kv_set_log_callback(KvLogCallback callback) {
    MaploaderRuntime& runtime = maploader_runtime();
    if (runtime.available()) runtime.set_log_callback(callback);
}

void* kv_load_map(const char* path, double unit_distance) {
    MaploaderRuntime& runtime = maploader_runtime();
    return runtime.available() ? runtime.load_map(path, unit_distance) : nullptr;
}

void* kv_load_map_ex(const char* path, double unit_distance, unsigned flags) {
    MaploaderRuntime& runtime = maploader_runtime();
    return runtime.available() ? runtime.load_map_ex(path, unit_distance, flags) : nullptr;
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

KvDoubleBuffer kv_get_owntrack_buffer(void* handle) {
    MaploaderRuntime& runtime = maploader_runtime();
    return runtime.available() ? runtime.get_owntrack_buffer(handle) : KvDoubleBuffer{};
}

KvDoubleBuffer kv_get_curveradius_buffer(void* handle) {
    MaploaderRuntime& runtime = maploader_runtime();
    return runtime.available() ? runtime.get_curveradius_buffer(handle) : KvDoubleBuffer{};
}

size_t kv_get_othertrack_count(void* handle) {
    MaploaderRuntime& runtime = maploader_runtime();
    return runtime.available() ? runtime.get_othertrack_count(handle) : 0;
}

const char* kv_get_othertrack_key(void* handle, size_t index) {
    MaploaderRuntime& runtime = maploader_runtime();
    return runtime.available() ? runtime.get_othertrack_key(handle, index) : nullptr;
}

KvDoubleBuffer kv_get_othertrack_buffer(void* handle, const char* key) {
    MaploaderRuntime& runtime = maploader_runtime();
    return runtime.available() ? runtime.get_othertrack_buffer(handle, key) : KvDoubleBuffer{};
}

KvDoubleBuffer kv_get_structure_puts(void* handle) {
    MaploaderRuntime& runtime = maploader_runtime();
    return runtime.available() ? runtime.get_structure_puts(handle) : KvDoubleBuffer{};
}

int kv_get_preview_cache_hit(void* handle) {
    MaploaderRuntime& runtime = maploader_runtime();
    return runtime.available() ? runtime.get_preview_cache_hit(handle) : 0;
}

const char* kv_get_ir_json_ex(void* handle, unsigned flags) {
    MaploaderRuntime& runtime = maploader_runtime();
    return runtime.available() ? runtime.get_ir_json_ex(handle, flags) : nullptr;
}

const char* kv_get_ir_json(void* handle) {
    MaploaderRuntime& runtime = maploader_runtime();
    return runtime.available() ? runtime.get_ir_json(handle) : nullptr;
}

const char* kv_get_edit_target_info(void* handle, const char* edit_id) {
    MaploaderRuntime& runtime = maploader_runtime();
    return runtime.available() ? runtime.get_edit_target_info(handle, edit_id) : nullptr;
}

const char* kv_edit_dry_run(void* handle, const char* changes_json) {
    MaploaderRuntime& runtime = maploader_runtime();
    return runtime.available() ? runtime.edit_dry_run(handle, changes_json) : nullptr;
}

const char* kv_edit_apply_to_memory(void* handle, const char* changes_json) {
    MaploaderRuntime& runtime = maploader_runtime();
    return runtime.available() ? runtime.edit_apply_to_memory(handle, changes_json) : nullptr;
}

int kv_edit_reset_memory(void* handle) {
    MaploaderRuntime& runtime = maploader_runtime();
    return runtime.available() ? runtime.edit_reset_memory(handle) : 0;
}

const char* kv_edit_apply(void* handle, const char* changes_json) {
    MaploaderRuntime& runtime = maploader_runtime();
    return runtime.available() ? runtime.edit_apply(handle, changes_json) : nullptr;
}

const char* kv_edit_commit(void* handle) {
    MaploaderRuntime& runtime = maploader_runtime();
    return runtime.available() ? runtime.edit_commit(handle) : nullptr;
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
