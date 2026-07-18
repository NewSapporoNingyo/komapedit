/*
 * Copyright (c) 2026 Sapporo_ningyo
 *
 * Licensed under Apache License 2.0; see LICENSE and NOTICE.
 */

#include "maploader_internal.h"

namespace kme::maploader::detail {

std::uint64_t stable_hash64(const std::string& text) {
    std::uint64_t hash = 14695981039346656037ull;
    for (unsigned char ch : text) {
        hash ^= static_cast<std::uint64_t>(ch);
        hash *= 1099511628211ull;
    }
    return hash;
}

std::string hex64(std::uint64_t value) {
    std::ostringstream out;
    out << std::hex << std::setw(16) << std::setfill('0') << value;
    return out.str();
}

std::string edit_kind_token(std::string kind) {
    for (char& ch : kind) {
        const unsigned char value = static_cast<unsigned char>(ch);
        ch = std::isalnum(value) ? static_cast<char>(std::tolower(value)) : '_';
    }
    while (!kind.empty() && kind.back() == '_') kind.pop_back();
    return kind.empty() ? "row" : kind;
}

std::string make_edit_id(const std::string& source_key, int global_order,
                         const std::string& kind, int element_index) {
    const std::string key = source_key + "|" + std::to_string(global_order) + "|" +
        ascii_lower(kind) + "|" + std::to_string(element_index);
    return "edit-" + hex64(stable_hash64(key)) + "-" +
        std::to_string(global_order) + "-" + edit_kind_token(kind) + "-" +
        std::to_string(element_index);
}

std::string statement_edit_id(MapContext& ctx, ParsedStatement& statement) {
    if (statement.edit_id.empty()) {
        statement.edit_id = make_edit_id(source_file_key(ctx, statement.source),
                                         statement.global_order,
                                         "statement." + statement.statement_kind, 0);
    }
    return statement.edit_id;
}

std::string native_element_edit_id(const MapContext& ctx, const EditSourceRef& ref,
                                   const std::string& row_kind) {
    if (!ref.valid() || ref.statement_index >= ctx.parsed_statements.size()) return {};
    const ParsedStatement& statement = ctx.parsed_statements[ref.statement_index];
    return make_edit_id(source_file_key(ctx, statement.source), statement.global_order,
                        row_kind, ref.element_index);
}

std::string element_edit_id(const MapContext& ctx, const EditSourceRef& ref,
                            const std::string& row_kind) {
    if (!ref.valid() || ref.statement_index >= ctx.parsed_statements.size()) return {};
    const std::string cache_key = std::to_string(ref.statement_index) + "|" +
        std::to_string(ref.element_index) + "|" + row_kind;
    auto cached = ctx.element_edit_id_cache.find(cache_key);
    if (cached != ctx.element_edit_id_cache.end()) return cached->second;
    std::string edit_id = native_element_edit_id(ctx, ref, row_kind);
    auto stable = ctx.native_element_edit_id_to_stable.find(edit_id);
    if (stable != ctx.native_element_edit_id_to_stable.end()) edit_id = stable->second;
    auto inserted = ctx.element_edit_id_cache.emplace(cache_key, std::move(edit_id));
    return inserted.first->second;
}

} // namespace kme::maploader::detail
