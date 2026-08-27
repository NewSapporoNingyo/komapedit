/*
 * Copyright (c) 2026 Sapporo_ningyo
 *
 * Licensed under Apache License 2.0; see LICENSE and NOTICE.
 */

#include "diagnostics.h"

#include <atomic>
#include <iostream>
#include <string>

namespace kme::maploader {
namespace {

std::atomic<KvLogCallback> g_log_callback{nullptr};
thread_local std::string g_last_error;
thread_local const char* g_last_error_fallback = nullptr;

void emit_log(const std::string& line) noexcept {
    try {
        if (KvLogCallback callback = g_log_callback.load(std::memory_order_acquire)) {
            callback(line.c_str());
        } else {
            std::cout << line << std::endl;
        }
    } catch (...) {
    }
}

std::string_view source_file_name(std::string_view source_path) noexcept {
    const size_t separator = source_path.find_last_of("\\/");
    return separator == std::string_view::npos ? source_path : source_path.substr(separator + 1);
}

void emit_log_at(std::string_view severity, std::string_view source_path,
                 const std::string& message) noexcept {
    try {
        std::string_view source_name = source_file_name(source_path);
        if (source_name.empty()) source_name = "unknown";
        std::string line;
        line.reserve(severity.size() + source_name.size() + 2 + message.size());
        line.append(severity.data(), severity.size());
        line.append(source_name.data(), source_name.size());
        line.append(": ");
        line.append(message);
        emit_log(line);
    } catch (...) {
    }
}

} // namespace

void set_log_callback(KvLogCallback callback) noexcept {
    g_log_callback.store(callback, std::memory_order_release);
}

void set_last_error(std::string_view message) noexcept {
    g_last_error_fallback = nullptr;
    try {
        if (message.empty()) g_last_error.clear();
        else g_last_error.assign(message.data(), message.size());
    } catch (...) {
        g_last_error.clear();
        g_last_error_fallback = "maploader error";
    }
}

const char* last_error_c_str() noexcept {
    return g_last_error_fallback ? g_last_error_fallback : g_last_error.c_str();
}

void log_info_at(std::string_view source_path, const std::string& message) noexcept {
    emit_log_at("[INFO]", source_path, message);
}

void log_warn_at(std::string_view source_path, const std::string& message) noexcept {
    emit_log_at("[WARN]", source_path, message);
}

void log_error_at(std::string_view source_path, const std::string& message) noexcept {
    emit_log_at("[ERROR]", source_path, message);
}

} // namespace kme::maploader
