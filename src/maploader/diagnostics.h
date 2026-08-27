/*
 * Copyright (c) 2026 Sapporo_ningyo
 *
 * Licensed under Apache License 2.0; see LICENSE and NOTICE.
 */

#pragma once

#include "maploader.h"

#include <string>
#include <string_view>

namespace kme::maploader {

void set_log_callback(KvLogCallback callback) noexcept;
void set_last_error(std::string_view message) noexcept;
const char* last_error_c_str() noexcept;

void log_info_at(std::string_view source_path, const std::string& message) noexcept;
void log_warn_at(std::string_view source_path, const std::string& message) noexcept;
void log_error_at(std::string_view source_path, const std::string& message) noexcept;

} // namespace kme::maploader

#define KME_MAPLOADER_LOG_INFO(message) \
    ::kme::maploader::log_info_at(__FILE__, (message))
#define KME_MAPLOADER_LOG_WARN(message) \
    ::kme::maploader::log_warn_at(__FILE__, (message))
#define KME_MAPLOADER_LOG_ERROR(message) \
    ::kme::maploader::log_error_at(__FILE__, (message))
