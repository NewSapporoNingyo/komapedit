/*
 * Copyright (c) 2026 Sapporo_ningyo
 *
 * Licensed under Apache License 2.0; see LICENSE and NOTICE.
 */

#include "text_decoder.h"

#include <algorithm>
#include <cctype>
#include <climits>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace kme::maploader {

TextLineSpan text_line_span(std::string_view text, size_t begin) noexcept {
    begin = std::min(begin, text.size());
    size_t content_end = begin;
    while (content_end < text.size() &&
           text[content_end] != '\r' && text[content_end] != '\n') {
        ++content_end;
    }

    size_t next_begin = content_end;
    if (next_begin < text.size()) {
        if (text[next_begin] == '\r' && next_begin + 1 < text.size() &&
            text[next_begin + 1] == '\n') {
            next_begin += 2;
        } else {
            ++next_begin;
        }
    }
    return {content_end, next_begin};
}

size_t text_line_start_at(std::string_view text, size_t offset) noexcept {
    offset = std::min(offset, text.size());
    size_t line_start = 0;
    while (line_start < text.size()) {
        const TextLineSpan line = text_line_span(text, line_start);
        if (offset < line.next_begin || offset <= line.content_end) return line_start;
        if (!line.has_terminator()) break;
        line_start = line.next_begin;
    }
    return line_start;
}

FileOpenFailureKind classify_file_open_failure(const std::filesystem::path& path) {
    std::error_code error;
    const bool exists = std::filesystem::exists(path, error);
    if (error) return FileOpenFailureKind::StatusUnknown;
    return exists ? FileOpenFailureKind::ExistsButCannotOpen : FileOpenFailureKind::Missing;
}

std::string file_open_failure_message(const std::filesystem::path& path) {
    const std::string path_text = path_to_utf8(path);
    switch (classify_file_open_failure(path)) {
    case FileOpenFailureKind::Missing:
        return "File not found at specified path: " + path_text;
    case FileOpenFailureKind::ExistsButCannotOpen:
        return "File open error; file exists but cannot be opened: " + path_text;
    case FileOpenFailureKind::StatusUnknown:
        return "File open error; file status could not be determined: " + path_text;
    }
    return "File open error: " + path_text;
}

std::string path_to_utf8(const std::filesystem::path& path) {
#if defined(__cpp_char8_t)
    auto s = path.u8string();
    return std::string(reinterpret_cast<const char*>(s.data()), s.size());
#else
    return path.u8string();
#endif
}

#if defined(_WIN32)
std::wstring utf8_to_wide(const std::string& utf8) {
    if (utf8.empty()) return {};
    int wide_len = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                       utf8.data(), static_cast<int>(utf8.size()),
                                       nullptr, 0);
    if (wide_len <= 0) {
        wide_len = MultiByteToWideChar(CP_UTF8, 0,
                                       utf8.data(), static_cast<int>(utf8.size()),
                                       nullptr, 0);
    }
    if (wide_len <= 0) throw std::runtime_error("UTF-8 path decode failed");
    std::wstring wide(wide_len, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()),
                        wide.data(), wide_len);
    return wide;
}

std::filesystem::path path_from_utf8(const std::string& utf8) {
    return std::filesystem::path(utf8_to_wide(utf8));
}
#else
std::filesystem::path path_from_utf8(const std::string& utf8) {
    return std::filesystem::path(utf8);
}
#endif

std::filesystem::path join_utf8_path(const std::filesystem::path& root,
                                     const std::string& path_text) {
    std::string normalized = path_text;
    std::replace(normalized.begin(), normalized.end(), '\\', '/');
    std::filesystem::path path = path_from_utf8(normalized);
    return path.is_absolute() ? path : root / path;
}

std::string read_binary_file(const std::filesystem::path& path) {
#if defined(_WIN32)
    std::unique_ptr<FILE, decltype(&std::fclose)> input(
        _wfopen(path.wstring().c_str(), L"rb"), &std::fclose);
    if (!input) {
        throw std::runtime_error(file_open_failure_message(path));
    }
    std::string result;
    char buffer[8192];
    while (true) {
        size_t n = std::fread(buffer, 1, sizeof(buffer), input.get());
        if (n > 0) result.append(buffer, n);
        if (n < sizeof(buffer)) {
            if (std::ferror(input.get())) {
                throw std::runtime_error("File read error: " + path_to_utf8(path));
            }
            break;
        }
    }
    return result;
#else
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error(file_open_failure_message(path));
    }
    std::ostringstream buffer;
    buffer << input.rdbuf();
    return buffer.str();
#endif
}

#if defined(_WIN32)
std::string wide_to_utf8(const std::wstring& wide) {
    if (wide.empty()) return {};
    int bytes = WideCharToMultiByte(CP_UTF8, 0, wide.data(),
                                    static_cast<int>(wide.size()),
                                    nullptr, 0, nullptr, nullptr);
    if (bytes <= 0) throw std::runtime_error("WideCharToMultiByte failed");
    std::string out(bytes, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()),
                        out.data(), bytes, nullptr, nullptr);
    return out;
}

std::string decode_codepage(const std::string& bytes, unsigned int codepage, bool strict) {
    if (bytes.empty()) return {};
    DWORD flags = strict && codepage == CP_UTF8 ? MB_ERR_INVALID_CHARS : 0;
    int wide_len = MultiByteToWideChar(codepage, flags, bytes.data(),
                                       static_cast<int>(bytes.size()),
                                       nullptr, 0);
    if (wide_len <= 0) {
        throw std::runtime_error("text decode failed");
    }
    std::wstring wide(wide_len, L'\0');
    MultiByteToWideChar(codepage, flags, bytes.data(), static_cast<int>(bytes.size()),
                        wide.data(), wide_len);
    return wide_to_utf8(wide);
}
#else
std::string decode_codepage(const std::string& bytes, unsigned int, bool) {
    return bytes;
}
#endif

void append_utf8_codepoint(std::string& out, char32_t cp) {
    if (cp <= 0x7F) {
        out.push_back(static_cast<char>(cp));
    } else if (cp <= 0x7FF) {
        out.push_back(static_cast<char>(0xC0 | ((cp >> 6) & 0x1F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp <= 0xFFFF) {
        out.push_back(static_cast<char>(0xE0 | ((cp >> 12) & 0x0F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | ((cp >> 18) & 0x07)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

std::string decode_utf16(const std::string& bytes, bool little_endian) {
    size_t start = 0;
    if (bytes.size() >= 2) {
        unsigned char b0 = static_cast<unsigned char>(bytes[0]);
        unsigned char b1 = static_cast<unsigned char>(bytes[1]);
        if ((b0 == 0xff && b1 == 0xfe) || (b0 == 0xfe && b1 == 0xff)) {
            start = 2;
        }
    }
    if ((bytes.size() - start) % 2 != 0) {
        throw std::runtime_error("UTF-16 input has an incomplete trailing code unit");
    }
    std::string out;
    out.reserve(bytes.size() - start);
    for (size_t i = start; i < bytes.size(); i += 2) {
        unsigned char a = static_cast<unsigned char>(bytes[i]);
        unsigned char b = static_cast<unsigned char>(bytes[i + 1]);
        uint16_t unit = little_endian
            ? static_cast<uint16_t>(a | (b << 8))
            : static_cast<uint16_t>((a << 8) | b);
        if (unit >= 0xD800 && unit <= 0xDBFF) {
            if (i + 3 >= bytes.size()) {
                throw std::runtime_error("UTF-16 input ends with an unpaired high surrogate");
            }
            unsigned char c = static_cast<unsigned char>(bytes[i + 2]);
            unsigned char d = static_cast<unsigned char>(bytes[i + 3]);
            uint16_t low = little_endian
                ? static_cast<uint16_t>(c | (d << 8))
                : static_cast<uint16_t>((c << 8) | d);
            if (low >= 0xDC00 && low <= 0xDFFF) {
                char32_t cp = 0x10000 + (((unit - 0xD800) << 10) | (low - 0xDC00));
                append_utf8_codepoint(out, cp);
                i += 2;
                continue;
            }
            throw std::runtime_error("UTF-16 input contains an unpaired high surrogate");
        }
        if (unit >= 0xDC00 && unit <= 0xDFFF) {
            throw std::runtime_error("UTF-16 input contains an unpaired low surrogate");
        }
        append_utf8_codepoint(out, unit);
    }
    return out;
}

std::string ascii_lower_in_place(std::string text) {
    for (char& ch : text) {
        if (ch >= 'A' && ch <= 'Z') ch = static_cast<char>(ch - 'A' + 'a');
    }
    return text;
}

std::string decode_text_bytes(const std::string& bytes, const std::string& encoding) {
    const std::string lower = ascii_lower_in_place(encoding);
    if (lower == "utf-16le") return decode_utf16(bytes, true);
    if (lower == "utf-16be") return decode_utf16(bytes, false);
    if (lower == "cp932" || lower == "shift_jis" || lower == "sjis") {
        return decode_codepage(bytes, 932, false);
    }
    if (has_utf8_bom(bytes)) return decode_codepage(bytes.substr(3), 65001, true);
    return decode_codepage(bytes, 65001, true);
}

std::string first_line_ascii(const std::string& bytes) {
    const TextLineSpan first_line = text_line_span(bytes, 0);
    size_t end = first_line.content_end;
    if (!first_line.has_terminator()) end = std::min<size_t>(end, 512);
    std::string line = bytes.substr(0, end);
    for (char& ch : line) {
        unsigned char c = static_cast<unsigned char>(ch);
        if (c >= 0x80) ch = ' ';
    }
    return line;
}

bool has_utf8_bom(const std::string& bytes) {
    return bytes.size() >= 3 &&
           static_cast<unsigned char>(bytes[0]) == 0xef &&
           static_cast<unsigned char>(bytes[1]) == 0xbb &&
           static_cast<unsigned char>(bytes[2]) == 0xbf;
}

std::string append_utf16_bytes(const std::wstring& wide, bool little_endian) {
    std::string out;
    out.reserve(2 + wide.size() * 2);
    if (little_endian) {
        out.push_back(static_cast<char>(0xff));
        out.push_back(static_cast<char>(0xfe));
    } else {
        out.push_back(static_cast<char>(0xfe));
        out.push_back(static_cast<char>(0xff));
    }
    for (wchar_t wc : wide) {
        uint32_t cp = static_cast<uint32_t>(wc);
#if WCHAR_MAX > 0xffff
        if (cp > 0xffff) {
            cp -= 0x10000;
            uint16_t high = static_cast<uint16_t>(0xd800 + ((cp >> 10) & 0x3ff));
            uint16_t low = static_cast<uint16_t>(0xdc00 + (cp & 0x3ff));
            uint16_t units[] = {high, low};
            for (uint16_t unit : units) {
                if (little_endian) {
                    out.push_back(static_cast<char>(unit & 0xff));
                    out.push_back(static_cast<char>((unit >> 8) & 0xff));
                } else {
                    out.push_back(static_cast<char>((unit >> 8) & 0xff));
                    out.push_back(static_cast<char>(unit & 0xff));
                }
            }
            continue;
        }
#endif
        uint16_t unit = static_cast<uint16_t>(cp);
        if (little_endian) {
            out.push_back(static_cast<char>(unit & 0xff));
            out.push_back(static_cast<char>((unit >> 8) & 0xff));
        } else {
            out.push_back(static_cast<char>((unit >> 8) & 0xff));
            out.push_back(static_cast<char>(unit & 0xff));
        }
    }
    return out;
}

std::string encode_text_for_writeback(const std::string& utf8_text,
                                      const std::string& encoding,
                                      bool utf8_bom) {
    const std::string lower = ascii_lower_in_place(encoding);
    if (lower == "utf-8" || lower == "utf8" || lower.empty()) {
        return utf8_bom ? std::string("\xef\xbb\xbf") + utf8_text : utf8_text;
    }

#if defined(_WIN32)
    std::wstring wide = utf8_to_wide(utf8_text);
    if (lower == "utf-16le") return append_utf16_bytes(wide, true);
    if (lower == "utf-16be") return append_utf16_bytes(wide, false);
    if (lower == "cp932" || lower == "shift_jis" || lower == "sjis") {
        if (wide.empty()) return {};
        BOOL used_default = FALSE;
        int bytes = WideCharToMultiByte(932, WC_NO_BEST_FIT_CHARS,
                                        wide.data(), static_cast<int>(wide.size()),
                                        nullptr, 0, nullptr, &used_default);
        if (bytes <= 0 || used_default) {
            throw std::runtime_error("text cannot be represented as CP932");
        }
        std::string out(bytes, '\0');
        used_default = FALSE;
        WideCharToMultiByte(932, WC_NO_BEST_FIT_CHARS,
                            wide.data(), static_cast<int>(wide.size()),
                            out.data(), bytes, nullptr, &used_default);
        if (used_default) throw std::runtime_error("text cannot be represented as CP932");
        return out;
    }
#else
    if (lower == "cp932" || lower == "shift_jis" || lower == "sjis") {
        if (std::any_of(utf8_text.begin(), utf8_text.end(), [](unsigned char ch) { return ch >= 0x80; })) {
            throw std::runtime_error("CP932 writeback is only supported on Windows");
        }
        return utf8_text;
    }
    if (lower == "utf-16le" || lower == "utf-16be") {
        throw std::runtime_error("UTF-16 writeback is only supported on Windows");
    }
#endif

    throw std::runtime_error("unsupported source encoding for writeback: " + encoding);
}

} // namespace kme::maploader
