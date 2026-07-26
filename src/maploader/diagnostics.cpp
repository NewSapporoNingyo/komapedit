/*
 * Copyright (c) 2026 Sapporo_ningyo
 *
 * Licensed under Apache License 2.0; see LICENSE and NOTICE.
 */

#include "diagnostics.h"

#include <iostream>
#include <string>
#include <utility>

namespace kme::maploader {
namespace {

KvLogCallback g_log_callback = nullptr;
thread_local std::string g_last_error;

void emit_log(const std::string& line) {
    if (g_log_callback) {
        g_log_callback(line.c_str());
    } else {
        std::cout << line << std::endl;
    }
}

} // namespace

void set_log_callback(KvLogCallback callback) {
    g_log_callback = callback;
}

void set_last_error(std::string message) {
    g_last_error = std::move(message);
}

const char* last_error_c_str() {
    return g_last_error.c_str();
}

void log_info(const std::string& message) {
    emit_log("[INFO]maploader.cpp: " + message);
}

void log_warn(const std::string& message) {
    emit_log("[WARN]maploader.cpp: " + message);
}

void log_error(const std::string& message) {
    emit_log("[ERROR]maploader.cpp: " + message);
}

} // namespace kme::maploader
