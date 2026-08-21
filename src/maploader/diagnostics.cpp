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

void log_info(const std::string& message) noexcept {
    try { emit_log("[INFO]maploader.cpp: " + message); } catch (...) {}
}

void log_warn(const std::string& message) noexcept {
    try { emit_log("[WARN]maploader.cpp: " + message); } catch (...) {}
}

void log_error(const std::string& message) noexcept {
    try { emit_log("[ERROR]maploader.cpp: " + message); } catch (...) {}
}

} // namespace kme::maploader
