/*
 * Copyright (c) 2026 Sapporo_ningyo
 *
 * Licensed under Apache License 2.0; see LICENSE and NOTICE.
 */

#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>

namespace kme::maploader {

enum class FileOpenFailureKind {
    Missing,
    ExistsButCannotOpen,
    StatusUnknown,
};

struct TextLineSpan {
    size_t content_end = 0;
    size_t next_begin = 0;

    bool has_terminator() const noexcept { return next_begin > content_end; }
};

TextLineSpan text_line_span(std::string_view text, size_t begin) noexcept;
size_t text_line_start_at(std::string_view text, size_t offset) noexcept;

std::string path_to_utf8(const std::filesystem::path& path);
std::filesystem::path path_from_utf8(const std::string& utf8);
std::filesystem::path join_utf8_path(const std::filesystem::path& root,
                                     const std::string& path_text);
FileOpenFailureKind classify_file_open_failure(const std::filesystem::path& path);
std::string file_open_failure_message(const std::filesystem::path& path);
std::string read_binary_file(const std::filesystem::path& path);
std::string decode_codepage(const std::string& bytes, unsigned int codepage, bool strict);
std::string decode_utf16(const std::string& bytes, bool little_endian);
std::string decode_text_bytes(const std::string& bytes, const std::string& encoding);
std::string first_line_ascii(const std::string& bytes);
bool has_utf8_bom(const std::string& bytes);
std::string encode_text_for_writeback(const std::string& utf8_text,
                                      const std::string& encoding,
                                      bool utf8_bom);

} // namespace kme::maploader
