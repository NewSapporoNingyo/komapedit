/*
 * Copyright (c) 2026 Sapporo_ningyo
 *
 * Licensed under Apache License 2.0; see LICENSE and NOTICE.
 */

#pragma once

#include <string>

namespace kme::maploader {

// Allocates a NUL-terminated string owned by maploader.dll; callers release it with kv_free_string.
char* copy_c_string(const std::string& s);

} // namespace kme::maploader