/*
 * Copyright (c) 2026 Sapporo_ningyo
 *
 * Licensed under Apache License 2.0; see LICENSE and NOTICE.
 */

#pragma once

#include "maploader.h"

#include <string>

namespace kme::maploader {

void set_log_callback(KvLogCallback callback);
void set_last_error(std::string message);
const char* last_error_c_str();

void log_info(const std::string& message);
void log_warn(const std::string& message);
void log_error(const std::string& message);
void log_load_failure(const std::string& message);

} // namespace kme::maploader
