/*
 * Copyright (c) 2026 Sapporo_ningyo
 *
 * Licensed under Apache License 2.0; see LICENSE and NOTICE.
 */

#pragma once

#include <windows.h>

#include <cstring>
#include <filesystem>
#include <string_view>
#include <type_traits>

namespace runtime_paths {

const std::filesystem::path& executable_directory();
const std::filesystem::path& dll_directory();
const std::filesystem::path& settings_directory();
const std::filesystem::path& preview_cache_directory();
std::filesystem::path dll_path(std::wstring_view filename);
HMODULE load_dll(std::wstring_view filename, DWORD* error_code = nullptr);

template <typename Function>
Function resolve_dll_function(HMODULE module, const char* name) {
    static_assert(std::is_pointer_v<Function> &&
                  std::is_function_v<std::remove_pointer_t<Function>>,
                  "DLL exports must be resolved to function pointers");
    FARPROC address = GetProcAddress(module, name);
    static_assert(sizeof(Function) == sizeof(address),
                  "Windows function pointer representations must match FARPROC");
    Function function = nullptr;
    // GetProcAddress requires a platform function-pointer conversion. Copying the
    // representation keeps that Windows-specific exception explicit and localized.
    std::memcpy(&function, &address, sizeof(function));
    return function;
}

} // namespace runtime_paths
