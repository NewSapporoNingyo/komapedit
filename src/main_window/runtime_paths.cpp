/*
 * Copyright (c) 2026 Sapporo_ningyo
 *
 * Licensed under Apache License 2.0; see LICENSE and NOTICE.
 */

#include "runtime_paths.h"

#include <string>
#include <system_error>
#include <vector>

namespace runtime_paths {

const std::filesystem::path& executable_directory() {
    static const std::filesystem::path directory = [] {
        std::vector<wchar_t> buffer(MAX_PATH);
        while (true) {
            DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
            if (length == 0) break;
            if (length < buffer.size() - 1) {
                return std::filesystem::path(std::wstring(buffer.data(), length)).parent_path();
            }
            buffer.resize(buffer.size() * 2);
        }

        std::error_code ec;
        std::filesystem::path current = std::filesystem::current_path(ec);
        return ec ? std::filesystem::path(L".") : current;
    }();
    return directory;
}

const std::filesystem::path& dll_directory() {
    static const std::filesystem::path directory = executable_directory() / L"bin";
    return directory;
}

const std::filesystem::path& settings_directory() {
    static const std::filesystem::path directory = [] {
        std::filesystem::path result = executable_directory() / L"settings";
        std::error_code ignored;
        std::filesystem::create_directories(result, ignored);
        return result;
    }();
    return directory;
}

std::filesystem::path dll_path(std::wstring_view filename) {
    return dll_directory() / std::wstring(filename);
}

HMODULE load_dll(std::wstring_view filename, DWORD* error_code) {
    const std::filesystem::path path = dll_path(filename);
    HMODULE library = LoadLibraryExW(path.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (error_code) *error_code = library ? ERROR_SUCCESS : GetLastError();
    return library;
}

} // namespace runtime_paths
