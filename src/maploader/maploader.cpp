/*
 * Copyright (c) 2026 Sapporo_ningyo
 *
 * Portions of the map parsing and track-geometry design are derived from or
 * reimplemented with reference to kobushi-trackviewer, Copyright (c) 2021-2024
 * konawasabi, licensed under Apache License 2.0.
 *
 * Licensed under Apache License 2.0; see LICENSE and NOTICE.
 */

#define MAPLOADER_EXPORTS
#include "maploader_internal.h"

#include "c_api.h"
#include "diagnostics.h"

namespace {

using kme::maploader::copy_c_string;
using kme::maploader::last_error_c_str;
using kme::maploader::log_error;
using kme::maploader::log_info;
using kme::maploader::log_warn;
using kme::maploader::path_from_utf8;
using kme::maploader::path_to_utf8;
using kme::maploader::set_last_error;
using kme::maploader::set_log_callback;
using kme::maploader::detail::MapContext;
using kme::maploader::detail::MapEditChange;
using kme::maploader::detail::MapEditReport;
using kme::maploader::detail::MapParseOptions;
using kme::maploader::detail::Matrix;
using kme::maploader::detail::SourceTextOverrides;

KvDoubleBuffer make_buffer(const Matrix& m) {
    return {m.data.empty() ? nullptr : m.data.data(), m.rows, m.cols};
}

struct PreviewCacheEntry {
    std::string path;
    std::uint64_t size = 0;
    std::int64_t mtime = 0;
};

constexpr char kPreviewCacheMagic[] = "KMPVC002";
constexpr std::uint32_t kPreviewCacheVersion = 2;
constexpr const char* kPreviewCacheDirectory = "preview-v2";
constexpr const char* kPreviewCacheKeyVersion = "loader-preview-cache-v2";

MapParseOptions parse_options_from_load_flags(unsigned flags) {
    MapParseOptions options;
    const bool preview = (flags & KV_LOAD_PREVIEW) != 0;
    const bool edit_metadata = (flags & KV_LOAD_EDIT_METADATA) != 0;
    options.collect_edit_metadata = edit_metadata || !preview;
    options.use_preview_cache = (flags & KV_LOAD_USE_PREVIEW_CACHE) != 0;
    options.rebuild_preview_cache = (flags & KV_LOAD_REBUILD_PREVIEW_CACHE) != 0;
    if (options.rebuild_preview_cache) options.use_preview_cache = true;
    return options;
}

template <typename T>
void write_pod(std::ostream& out, const T& value) {
    out.write(reinterpret_cast<const char*>(&value), sizeof(T));
    if (!out) throw std::runtime_error("failed to write preview cache");
}

template <typename T>
T read_pod(std::istream& in) {
    T value{};
    in.read(reinterpret_cast<char*>(&value), sizeof(T));
    if (!in) throw std::runtime_error("failed to read preview cache");
    return value;
}

void write_string(std::ostream& out, const std::string& value) {
    std::uint64_t size = static_cast<std::uint64_t>(value.size());
    write_pod(out, size);
    if (size > 0) out.write(value.data(), static_cast<std::streamsize>(size));
    if (!out) throw std::runtime_error("failed to write preview cache string");
}

std::string read_string(std::istream& in) {
    std::uint64_t size = read_pod<std::uint64_t>(in);
    if (size > 256ull * 1024ull * 1024ull) {
        throw std::runtime_error("preview cache string is too large");
    }
    std::string value(static_cast<size_t>(size), '\0');
    if (size > 0) in.read(&value[0], static_cast<std::streamsize>(size));
    if (!in) throw std::runtime_error("failed to read preview cache string");
    return value;
}

void write_matrix(std::ostream& out, const Matrix& matrix) {
    write_pod(out, static_cast<std::uint64_t>(matrix.rows));
    write_pod(out, static_cast<std::uint64_t>(matrix.cols));
    write_pod(out, static_cast<std::uint64_t>(matrix.data.size()));
    if (!matrix.data.empty()) {
        out.write(reinterpret_cast<const char*>(matrix.data.data()),
                  static_cast<std::streamsize>(matrix.data.size() * sizeof(double)));
    }
    if (!out) throw std::runtime_error("failed to write preview cache matrix");
}

Matrix read_matrix(std::istream& in) {
    Matrix matrix;
    matrix.rows = static_cast<size_t>(read_pod<std::uint64_t>(in));
    matrix.cols = static_cast<size_t>(read_pod<std::uint64_t>(in));
    std::uint64_t count = read_pod<std::uint64_t>(in);
    if (count > 256ull * 1024ull * 1024ull / sizeof(double)) {
        throw std::runtime_error("preview cache matrix is too large");
    }
    if (matrix.rows != 0 && matrix.cols != 0 &&
        count != static_cast<std::uint64_t>(matrix.rows) * static_cast<std::uint64_t>(matrix.cols)) {
        throw std::runtime_error("preview cache matrix shape mismatch");
    }
    matrix.data.resize(static_cast<size_t>(count));
    if (count > 0) {
        in.read(reinterpret_cast<char*>(matrix.data.data()),
                static_cast<std::streamsize>(matrix.data.size() * sizeof(double)));
    }
    if (!in) throw std::runtime_error("failed to read preview cache matrix");
    return matrix;
}

std::filesystem::path preview_cache_root() {
#if defined(_WIN32)
    std::filesystem::path base;
    DWORD required = GetEnvironmentVariableW(L"LOCALAPPDATA", nullptr, 0);
    if (required > 1) {
        std::wstring buffer(required, L'\0');
        DWORD copied = GetEnvironmentVariableW(L"LOCALAPPDATA", buffer.data(), required);
        if (copied > 0 && copied < required) {
            buffer.resize(copied);
            base = buffer;
        }
    }
    if (base.empty()) base = std::filesystem::temp_directory_path();
#else
    const char* local = std::getenv("LOCALAPPDATA");
    std::filesystem::path base = local && *local ? local : std::filesystem::temp_directory_path();
#endif
    return base / "komapedit" / "cache" / kPreviewCacheDirectory;
}

std::filesystem::path preview_cache_path(const std::filesystem::path& map_path, double unit_distance) {
    std::ostringstream key;
    key << kme::maploader::detail::normalized_source_key(
               kme::maploader::detail::normalized_source_path(map_path))
        << "|" << std::setprecision(17) << unit_distance
        << "|" << kPreviewCacheKeyVersion;
    std::string name = kme::maploader::detail::hex64(
        kme::maploader::detail::stable_hash64(key.str())) + ".kmpv";
    return preview_cache_root() / name;
}

bool stat_preview_cache_file(const std::string& utf8_path,
                             std::uint64_t& size,
                             std::int64_t& mtime) {
    std::error_code ec;
    std::filesystem::path path = path_from_utf8(utf8_path);
    if (!std::filesystem::is_regular_file(path, ec) || ec) return false;
    std::uintmax_t file_size = std::filesystem::file_size(path, ec);
    if (ec) return false;
    auto file_time = std::filesystem::last_write_time(path, ec);
    if (ec) return false;
    size = static_cast<std::uint64_t>(file_size);
    mtime = static_cast<std::int64_t>(file_time.time_since_epoch().count());
    return true;
}

std::vector<PreviewCacheEntry> make_preview_cache_manifest(const MapContext& ctx) {
    std::vector<PreviewCacheEntry> manifest;
    std::unordered_set<std::string> seen;
    for (const auto& source : ctx.source_files) {
        if (source.file_path.empty()) continue;
        std::string key = kme::maploader::detail::normalized_source_key(source.file_path);
        if (!seen.insert(key).second) continue;
        PreviewCacheEntry entry;
        entry.path = source.file_path;
        if (!stat_preview_cache_file(entry.path, entry.size, entry.mtime)) {
            log_warn("preview cache manifest skipped source file: " + entry.path);
            continue;
        }
        manifest.push_back(std::move(entry));
    }
    if (manifest.empty() && !ctx.entry_file_path.empty()) {
        PreviewCacheEntry entry;
        entry.path = ctx.entry_file_path;
        if (stat_preview_cache_file(entry.path, entry.size, entry.mtime)) {
            manifest.push_back(std::move(entry));
        } else {
            log_warn("preview cache manifest could not stat entry file: " + ctx.entry_file_path);
        }
    }
    return manifest;
}

bool preview_cache_manifest_valid(const std::vector<PreviewCacheEntry>& manifest) {
    if (manifest.empty()) return false;
    auto validate_range = [&manifest](size_t begin, size_t end) {
        for (size_t i = begin; i < end; ++i) {
            const PreviewCacheEntry& entry = manifest[i];
            std::uint64_t size = 0;
            std::int64_t mtime = 0;
            if (!stat_preview_cache_file(entry.path, size, mtime)) return false;
            if (size != entry.size || mtime != entry.mtime) return false;
        }
        return true;
    };
    if (manifest.size() < 8) return validate_range(0, manifest.size());

    unsigned concurrency = std::thread::hardware_concurrency();
    if (concurrency == 0) concurrency = 2;
    size_t worker_count = std::min(manifest.size(), static_cast<size_t>(std::min(concurrency, 8u)));
    size_t chunk_size = (manifest.size() + worker_count - 1) / worker_count;
    std::vector<std::future<bool>> futures;
    futures.reserve(worker_count);
    for (size_t begin = 0; begin < manifest.size(); begin += chunk_size) {
        size_t end = std::min(manifest.size(), begin + chunk_size);
        futures.push_back(std::async(std::launch::async, validate_range, begin, end));
    }
    for (auto& future : futures) {
        if (!future.get()) return false;
    }
    return true;
}

std::unique_ptr<MapContext> try_load_preview_cache(const std::filesystem::path& map_path,
                                                   double unit_distance) {
    std::ifstream in(preview_cache_path(map_path, unit_distance), std::ios::binary);
    if (!in) return nullptr;
    try {
        char magic[sizeof(kPreviewCacheMagic) - 1] = {};
        in.read(magic, sizeof(magic));
        if (!in || std::memcmp(magic, kPreviewCacheMagic, sizeof(magic)) != 0) return nullptr;
        std::uint32_t version = read_pod<std::uint32_t>(in);
        if (version != kPreviewCacheVersion) return nullptr;
        double cached_unit_distance = read_pod<double>(in);
        if (cached_unit_distance != unit_distance) return nullptr;

        std::uint64_t manifest_count = read_pod<std::uint64_t>(in);
        if (manifest_count > 100000) return nullptr;
        std::vector<PreviewCacheEntry> manifest;
        manifest.reserve(static_cast<size_t>(manifest_count));
        for (std::uint64_t i = 0; i < manifest_count; ++i) {
            PreviewCacheEntry entry;
            entry.path = read_string(in);
            entry.size = read_pod<std::uint64_t>(in);
            entry.mtime = read_pod<std::int64_t>(in);
            manifest.push_back(std::move(entry));
        }
        if (!preview_cache_manifest_valid(manifest)) return nullptr;

        std::string compact_json = read_string(in);
        Matrix owntrack = read_matrix(in);
        Matrix curve = read_matrix(in);
        Matrix structures = read_matrix(in);
        std::uint64_t other_count = read_pod<std::uint64_t>(in);
        if (other_count > 100000) return nullptr;

        auto ctx = std::make_unique<MapContext>();
        ctx->rootpath = std::filesystem::absolute(map_path).parent_path();
        ctx->rootpath_utf8 = path_to_utf8(ctx->rootpath);
        ctx->entry_file_path = kme::maploader::detail::normalized_source_path(map_path);
        ctx->current_file_path = ctx->entry_file_path;
        ctx->unit_distance = unit_distance;
        ctx->parse_options.collect_edit_metadata = false;
        ctx->parse_options.use_preview_cache = true;
        ctx->preview_cache_hit = true;
        ctx->preview_snapshot_only = true;
        ctx->owntrack_buffer = std::move(owntrack);
        ctx->curveradius_buffer = std::move(curve);
        ctx->structure_put_buffer = std::move(structures);
        ctx->ir_json_cache_by_flags[KV_IR_JSON_COMPACT] = std::move(compact_json);
        for (std::uint64_t i = 0; i < other_count; ++i) {
            std::string key = read_string(in);
            Matrix matrix = read_matrix(in);
            ctx->othertrack_order.push_back(key);
            ctx->othertrack_buffers[std::move(key)] = std::move(matrix);
        }
        log_info(path_to_utf8(map_path.filename()) + " loaded from preview cache");
        return ctx;
    } catch (const std::exception& e) {
        log_warn(std::string("preview cache ignored: ") + e.what());
        return nullptr;
    }
}

void write_preview_cache(const MapContext& ctx, const std::filesystem::path& map_path,
                         double unit_distance, const std::string& compact_json) {
    try {
        std::vector<PreviewCacheEntry> manifest = make_preview_cache_manifest(ctx);
        if (manifest.empty()) return;

        std::filesystem::path path = preview_cache_path(map_path, unit_distance);
        std::filesystem::create_directories(path.parent_path());
        std::filesystem::path temp = path;
        temp += ".tmp";
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        if (!out) throw std::runtime_error("cannot open preview cache for writing");

        out.write(kPreviewCacheMagic, sizeof(kPreviewCacheMagic) - 1);
        write_pod(out, kPreviewCacheVersion);
        write_pod(out, unit_distance);
        write_pod(out, static_cast<std::uint64_t>(manifest.size()));
        for (const PreviewCacheEntry& entry : manifest) {
            write_string(out, entry.path);
            write_pod(out, entry.size);
            write_pod(out, entry.mtime);
        }
        write_string(out, compact_json);
        write_matrix(out, ctx.owntrack_buffer);
        write_matrix(out, ctx.curveradius_buffer);
        write_matrix(out, ctx.structure_put_buffer);
        write_pod(out, static_cast<std::uint64_t>(ctx.othertrack_order.size()));
        for (const std::string& key : ctx.othertrack_order) {
            auto it = ctx.othertrack_buffers.find(key);
            write_string(out, key);
            write_matrix(out, it == ctx.othertrack_buffers.end() ? Matrix{} : it->second);
        }
        out.close();
        if (!out) throw std::runtime_error("failed to flush preview cache");

        std::error_code ec;
        std::filesystem::remove(path, ec);
        ec.clear();
        std::filesystem::rename(temp, path, ec);
        if (ec) throw std::runtime_error("failed to publish preview cache: " + ec.message());
    } catch (const std::exception& e) {
        log_warn(std::string("preview cache write skipped: ") + e.what());
    }
}

} // namespace

extern "C" {

KV_API void kv_set_log_callback(KvLogCallback callback) {
    set_log_callback(callback);
}

KV_API void* kv_load_map(const char* path, double unit_distance) {
    return kv_load_map_ex(path, unit_distance, KV_LOAD_EDIT_METADATA);
}

KV_API void* kv_load_map_ex(const char* path, double unit_distance, unsigned flags) {
    try {
        if (!path) throw std::runtime_error("path is null");
        std::filesystem::path map_path = path_from_utf8(path);
        MapParseOptions options = parse_options_from_load_flags(flags);
        if (!options.collect_edit_metadata && options.use_preview_cache && !options.rebuild_preview_cache) {
            if (auto cached = try_load_preview_cache(map_path, unit_distance)) {
                return cached.release();
            }
            log_info("preview_cache=miss");
        } else if (!options.collect_edit_metadata && options.rebuild_preview_cache) {
            log_info("preview_cache=rebuild");
        }
        auto ctx = kme::maploader::detail::parse_map_context(
            map_path, unit_distance, SourceTextOverrides{}, false, {0.0, 0.0, 0.0}, options);
        if (!options.collect_edit_metadata && options.use_preview_cache) {
            {
                kme::maploader::detail::ScopedTimer timer(&ctx->timing.json_seconds);
                ctx->ir_json_cache_by_flags[KV_IR_JSON_COMPACT] =
                    kme::maploader::detail::build_ir_json(*ctx, KV_IR_JSON_COMPACT);
            }
            write_preview_cache(*ctx, map_path, unit_distance,
                                ctx->ir_json_cache_by_flags[KV_IR_JSON_COMPACT]);
        }
        log_info(path_to_utf8(map_path.filename()) +
                 (options.collect_edit_metadata ? " loaded (edit)" : " loaded (preview)"));
        return ctx.release();
    } catch (const std::exception& e) {
        set_last_error(e.what());
        log_error(e.what());
        return nullptr;
    }
}

KV_API int kv_generate_geometry(void* handle, double unit_distance,
                                int has_arbitrary_distribution,
                                double arbitrary_start,
                                double arbitrary_end,
                                double arbitrary_step) {
    try {
        if (!handle) throw std::runtime_error("handle is null");
        auto* ctx = static_cast<MapContext*>(handle);
        if (ctx->preview_snapshot_only) {
            throw std::runtime_error("preview cache handle cannot regenerate geometry");
        }
        kme::maploader::detail::generate_geometry(*ctx, unit_distance, has_arbitrary_distribution != 0,
                                                  arbitrary_start, arbitrary_end, arbitrary_step);
        return 1;
    } catch (const std::exception& e) {
        set_last_error(e.what());
        log_error(e.what());
        return 0;
    }
}

KV_API int kv_generate_scene_geometry(void* handle, double unit_distance,
                                      double min_step, double max_step,
                                      double max_angle_degrees,
                                      double max_chord_error) {
    try {
        if (!handle) throw std::runtime_error("handle is null");
        auto* ctx = static_cast<MapContext*>(handle);
        if (ctx->preview_snapshot_only) {
            throw std::runtime_error("preview cache handle cannot generate scene geometry");
        }
        if (ctx->owntrack_buffer.rows == 0) {
            kme::maploader::detail::generate_geometry(*ctx, unit_distance, false, 0.0, 0.0, 0.0);
        }

        const bool has_arb = ctx->has_cp_arbdistribution;
        const std::array<double, 3> arb = ctx->cp_arbdistribution;
        Matrix baseline = ctx->owntrack_buffer;
        std::vector<double> extra = kme::maploader::detail::build_scene_adaptive_controlpoints(
            *ctx, baseline, min_step, max_step, max_angle_degrees, max_chord_error);
        kme::maploader::detail::generate_geometry(*ctx, unit_distance, has_arb, arb[0], arb[1], arb[2], &extra);
        return 1;
    } catch (const std::exception& e) {
        set_last_error(e.what());
        log_error(e.what());
        return 0;
    }
}

KV_API KvDoubleBuffer kv_get_owntrack_buffer(void* handle) {
    if (!handle) return {nullptr, 0, 0};
    return make_buffer(static_cast<MapContext*>(handle)->owntrack_buffer);
}

KV_API KvDoubleBuffer kv_get_curveradius_buffer(void* handle) {
    if (!handle) return {nullptr, 0, 0};
    return make_buffer(static_cast<MapContext*>(handle)->curveradius_buffer);
}

KV_API size_t kv_get_othertrack_count(void* handle) {
    if (!handle) return 0;
    return static_cast<MapContext*>(handle)->othertrack_order.size();
}

KV_API const char* kv_get_othertrack_key(void* handle, size_t index) {
    if (!handle) return nullptr;
    auto* ctx = static_cast<MapContext*>(handle);
    if (index >= ctx->othertrack_order.size()) return nullptr;
    return ctx->othertrack_order[index].c_str();
}

KV_API KvDoubleBuffer kv_get_othertrack_buffer(void* handle, const char* key) {
    if (!handle || !key) return {nullptr, 0, 0};
    auto* ctx = static_cast<MapContext*>(handle);
    std::string k = kme::maploader::detail::ascii_lower(key);
    auto it = ctx->othertrack_buffers.find(k);
    if (it == ctx->othertrack_buffers.end()) return {nullptr, 0, 0};
    return make_buffer(it->second);
}

KV_API KvDoubleBuffer kv_get_structure_puts(void* handle) {
    if (!handle) return {nullptr, 0, 0};
    return make_buffer(static_cast<MapContext*>(handle)->structure_put_buffer);
}

KV_API int kv_get_preview_cache_hit(void* handle) {
    if (!handle) return 0;
    return static_cast<MapContext*>(handle)->preview_cache_hit ? 1 : 0;
}

KV_API const char* kv_get_ir_json_ex(void* handle, unsigned flags) {
    try {
        if (!handle) throw std::runtime_error("handle is null");
        auto* ctx = static_cast<MapContext*>(handle);
        flags = kme::maploader::detail::normalize_ir_json_flags(flags);
        if (ctx->preview_snapshot_only && flags != KV_IR_JSON_COMPACT) {
            throw std::runtime_error("full IR JSON is not available from a preview cache handle");
        }
        auto& cache = ctx->ir_json_cache_by_flags[flags];
        if (cache.empty()) {
            kme::maploader::detail::ScopedTimer timer(&ctx->timing.json_seconds);
            cache = kme::maploader::detail::build_ir_json(*ctx, flags);
        }
        if (!ctx->load_timing_logged) {
            kme::maploader::detail::log_load_timing(*ctx);
            ctx->load_timing_logged = true;
        }
        return copy_c_string(cache);
    } catch (const std::exception& e) {
        set_last_error(e.what());
        return nullptr;
    }
}

KV_API const char* kv_get_ir_json(void* handle) {
    return kv_get_ir_json_ex(handle, KV_IR_JSON_FULL_EDIT | KV_IR_JSON_FULL_STATEMENT_SOURCE);
}

KV_API const char* kv_get_edit_target_info(void* handle, const char* edit_id) {
    try {
        if (!handle) throw std::runtime_error("handle is null");
        auto* ctx = static_cast<MapContext*>(handle);
        return copy_c_string(kme::maploader::detail::edit_target_info_json(*ctx, edit_id ? edit_id : ""));
    } catch (const std::exception& e) {
        set_last_error(e.what());
        return nullptr;
    }
}

KV_API const char* kv_get_source_text(void* handle, const char* file_path) {
    try {
        if (!handle) throw std::runtime_error("handle is null");
        auto* ctx = static_cast<MapContext*>(handle);
        return copy_c_string(kme::maploader::detail::current_source_text(
            *ctx, file_path ? file_path : ""));
    } catch (const std::exception& e) {
        set_last_error(e.what());
        return nullptr;
    }
}

KV_API const char* kv_edit_dry_run(void* handle, const char* changes_json) {
    try {
        if (!handle) throw std::runtime_error("handle is null");
        auto* ctx = static_cast<MapContext*>(handle);
        std::vector<MapEditChange> changes = kme::maploader::detail::parse_edit_changes_json(changes_json);
        MapEditReport report = kme::maploader::detail::build_edit_report(*ctx, changes, false);
        return copy_c_string(kme::maploader::detail::report_json(report));
    } catch (const std::exception& e) {
        set_last_error(e.what());
        return nullptr;
    }
}

KV_API const char* kv_edit_apply_to_memory(void* handle, const char* changes_json) {
    try {
        if (!handle) throw std::runtime_error("handle is null");
        auto* ctx = static_cast<MapContext*>(handle);
        std::vector<MapEditChange> changes = kme::maploader::detail::parse_edit_changes_json(changes_json);
        MapEditReport report = kme::maploader::detail::build_edit_report(*ctx, changes, false);
        if (report.ok()) {
            try {
                kme::maploader::detail::apply_edit_report_to_memory(*ctx, report);
            } catch (const std::exception& e) {
                report.blocking_errors.push_back(std::string("edited cache reload failed: ") + e.what());
            }
        }
        return copy_c_string(kme::maploader::detail::report_json(report));
    } catch (const std::exception& e) {
        set_last_error(e.what());
        return nullptr;
    }
}

KV_API int kv_edit_reset_memory(void* handle) {
    try {
        if (!handle) throw std::runtime_error("handle is null");
        auto* ctx = static_cast<MapContext*>(handle);
        kme::maploader::detail::reset_memory_edits(*ctx);
        return 1;
    } catch (const std::exception& e) {
        set_last_error(e.what());
        return 0;
    }
}

KV_API const char* kv_edit_apply(void* handle, const char* changes_json) {
    try {
        if (!handle) throw std::runtime_error("handle is null");
        auto* ctx = static_cast<MapContext*>(handle);
        std::vector<MapEditChange> changes = kme::maploader::detail::parse_edit_changes_json(changes_json);
        MapEditReport report = kme::maploader::detail::build_edit_report(*ctx, changes, true);
        return copy_c_string(kme::maploader::detail::report_json(report));
    } catch (const std::exception& e) {
        set_last_error(e.what());
        return nullptr;
    }
}

KV_API const char* kv_edit_commit(void* handle) {
    try {
        if (!handle) throw std::runtime_error("handle is null");
        auto* ctx = static_cast<MapContext*>(handle);
        MapEditReport report = kme::maploader::detail::commit_memory_edits(*ctx);
        return copy_c_string(kme::maploader::detail::report_json(report));
    } catch (const std::exception& e) {
        set_last_error(e.what());
        return nullptr;
    }
}

KV_API const char* kv_get_last_error(void) {
    return last_error_c_str();
}

KV_API void kv_free(void* handle) {
    delete static_cast<MapContext*>(handle);
}

KV_API void kv_free_string(const char* text) {
    std::free(const_cast<char*>(text));
}

}
