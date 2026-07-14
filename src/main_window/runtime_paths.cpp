/*
 * Copyright (c) 2026 Sapporo_ningyo
 *
 * Licensed under Apache License 2.0; see LICENSE and NOTICE.
 */

#ifndef NTDDI_VERSION
#define NTDDI_VERSION 0x06000000
#endif

#include "runtime_paths.h"

#include <shlobj.h>

#include <string>
#include <system_error>
#include <vector>

namespace {

bool ensure_writable_directory(const std::filesystem::path& directory) {
    std::error_code ec;
    std::filesystem::create_directories(directory, ec);
    if (ec || !std::filesystem::is_directory(directory, ec) || ec) return false;

    for (unsigned attempt = 0; attempt < 4; ++attempt) {
        const std::wstring probe_name =
            L".komapedit-cache-probe-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
            std::to_wstring(GetTickCount()) + L"-" + std::to_wstring(attempt) + L".tmp";
        const std::filesystem::path probe_path = directory / probe_name;
        HANDLE probe = CreateFileW(
            probe_path.c_str(),
            GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            nullptr,
            CREATE_NEW,
            FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE,
            nullptr);
        if (probe != INVALID_HANDLE_VALUE) {
            CloseHandle(probe);
            return true;
        }
        if (GetLastError() != ERROR_FILE_EXISTS) return false;
    }
    return false;
}

std::filesystem::path local_app_data_directory() {
    PWSTR value = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_DEFAULT, nullptr, &value)) || !value) {
        return {};
    }
    std::filesystem::path result(value);
    CoTaskMemFree(value);
    return result;
}

} // namespace

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

const std::filesystem::path& preview_cache_directory() {
    static const std::filesystem::path directory = [] {
        const std::filesystem::path portable = executable_directory() / L"cache";
        if (ensure_writable_directory(portable)) return portable;

        const std::filesystem::path local_app_data = local_app_data_directory();
        if (!local_app_data.empty()) {
            const std::filesystem::path fallback = local_app_data / L"komapedit" / L"cache";
            if (ensure_writable_directory(fallback)) return fallback;
        }
        return std::filesystem::path{};
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
