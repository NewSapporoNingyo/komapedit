/*
 * Copyright (c) 2026 Sapporo_ningyo
 *
 * Licensed under Apache License 2.0; see LICENSE and NOTICE.
 */

#pragma once

#include <filesystem>
#include <string>

namespace kme::maploader {

std::string path_to_utf8(const std::filesystem::path& path);
std::filesystem::path path_from_utf8(const std::string& utf8);
std::string read_binary_file(const std::filesystem::path& path);
std::string decode_codepage(const std::string& bytes, unsigned int codepage, bool strict);
std::string decode_utf16(const std::string& bytes, bool little_endian);
std::string first_line_ascii(const std::string& bytes);
bool has_utf8_bom(const std::string& bytes);

} // namespace kme::maploader