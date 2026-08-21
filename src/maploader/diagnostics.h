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

void log_info(const std::string& message) noexcept;
void log_warn(const std::string& message) noexcept;
void log_error(const std::string& message) noexcept;

} // namespace kme::maploader
