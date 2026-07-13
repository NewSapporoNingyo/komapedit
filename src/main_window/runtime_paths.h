/*
 * Copyright (c) 2026 Sapporo_ningyo
 *
 * Licensed under Apache License 2.0; see LICENSE and NOTICE.
 */

#pragma once

#include <windows.h>

#include <filesystem>
#include <string_view>

namespace runtime_paths {

const std::filesystem::path& executable_directory();
const std::filesystem::path& dll_directory();
const std::filesystem::path& settings_directory();
std::filesystem::path dll_path(std::wstring_view filename);
HMODULE load_dll(std::wstring_view filename, DWORD* error_code = nullptr);

} // namespace runtime_paths
