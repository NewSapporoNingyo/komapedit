/*
 * Copyright (c) 2026 Sapporo_ningyo
 *
 * Licensed under Apache License 2.0; see LICENSE and NOTICE.
 */

#include "scenario_route.h"

#include "diagnostics.h"
#include "maploader_internal.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <limits>
#include <stdexcept>

namespace kme::maploader::detail {
namespace {

constexpr size_t k_probe_byte_limit = 1024;

std::string read_first_bytes(const std::filesystem::path& path, size_t limit) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return {};
    std::string bytes(limit, '\0');
    file.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    bytes.resize(static_cast<size_t>(file.gcount()));
    return bytes;
}

std::string first_line_of(std::string text) {
    const TextLineSpan line = text_line_span(text, 0);
    return text.substr(0, line.content_end);
}

bool ascii_starts_with(const std::string& lower_text, const char* prefix) {
    return lower_text.rfind(prefix, 0) == 0;
}

} // namespace

BveFileKind probe_bve_file_kind(const std::filesystem::path& path) {
    try {
        std::string bytes = read_first_bytes(path, k_probe_byte_limit);
        if (bytes.empty()) return BveFileKind::Unknown;

        std::string first_line;
        if (bytes.size() >= 2 &&
            static_cast<unsigned char>(bytes[0]) == 0xff &&
            static_cast<unsigned char>(bytes[1]) == 0xfe) {
            first_line = first_line_of(decode_utf16(bytes, true));
        } else if (bytes.size() >= 2 &&
                   static_cast<unsigned char>(bytes[0]) == 0xfe &&
                   static_cast<unsigned char>(bytes[1]) == 0xff) {
            first_line = first_line_of(decode_utf16(bytes, false));
        } else if (has_utf8_bom(bytes)) {
            first_line = first_line_of(bytes.substr(3));
        } else {
            first_line = first_line_ascii(bytes);
        }

        const std::string lower = ascii_lower(trim_field_copy(first_line));
        if (ascii_starts_with(lower, "bvets scenario")) return BveFileKind::Scenario;
        if (ascii_starts_with(lower, "bvets map")) return BveFileKind::Map;
        return BveFileKind::Unknown;
    } catch (...) {
        return BveFileKind::Unknown;
    }
}

namespace {

struct RouteTerm {
    std::string path_text;
    double weight = 1.0;
};

RouteTerm parse_route_term(const std::string& raw_term,
                           const std::string& route_value) {
    RouteTerm term;
    const size_t star = raw_term.find('*');
    std::string weight_text;
    if (star == std::string::npos) {
        term.path_text = trim_field_copy(raw_term);
    } else {
        term.path_text = trim_field_copy(raw_term.substr(0, star));
        weight_text = trim_field_copy(raw_term.substr(star + 1));
    }
    if (term.path_text.empty()) {
        throw std::runtime_error(
            "Scenario Route contains an empty candidate: " + route_value);
    }
    if (!weight_text.empty()) {
        if (!parse_finite_number(weight_text, term.weight) || term.weight <= 0.0) {
            throw std::runtime_error(
                "Scenario Route weight must be a positive finite number: " +
                weight_text);
        }
    }
    return term;
}

} // namespace

std::vector<ScenarioRouteCandidate> resolve_scenario_route_candidates(
    const std::filesystem::path& scenario_path) {
    // Validates the official "BveTs Scenario 2.00:<encoding>" header and
    // decodes the body with the declared encoding (UTF-8 default, CP932 for
    // shift_jis/sjis), mirroring map file loading.
    const LoadedText loaded =
        load_header_text(scenario_path, "BveTs Scenario", 2.00, nullptr, false);

    std::string route_value;
    bool has_route = false;
    for_each_loaded_body_line(loaded, [&](const std::string& line, size_t, size_t, int) {
        const std::string without_comment = strip_ini_comment_copy(line);
        const std::string trimmed = trim_field_copy(without_comment);
        if (trimmed.empty()) return;
        const size_t equals = trimmed.find('=');
        if (equals == std::string::npos) return;
        const std::string key = trim_field_copy(trimmed.substr(0, equals));
        if (key != "Route") return; // Only the documented spelling is consumed.
        const std::string value = trim_field_copy(trimmed.substr(equals + 1));
        if (value.empty()) {
            throw std::runtime_error("Scenario Route entry is empty");
        }
        if (has_route && value != route_value) {
            log_warn(path_to_utf8(scenario_path.filename()) +
                     " declares multiple Route entries; the last one wins.");
        }
        route_value = value;
        has_route = true;
    });
    if (!has_route) {
        throw std::runtime_error(
            "Scenario file has no Route entry: " + path_to_utf8(scenario_path));
    }

    std::vector<ScenarioRouteCandidate> candidates;
    size_t begin = 0;
    while (true) {
        const size_t pipe = route_value.find('|', begin);
        const std::string term_text = pipe == std::string::npos
            ? route_value.substr(begin)
            : route_value.substr(begin, pipe - begin);
        const RouteTerm term = parse_route_term(term_text, route_value);
        const std::filesystem::path resolved =
            join_path(loaded.root, term.path_text).lexically_normal();
        if (!std::filesystem::is_regular_file(resolved)) {
            throw std::runtime_error(
                "Scenario Route target does not exist: " + path_to_utf8(resolved));
        }
        candidates.push_back(ScenarioRouteCandidate{term.path_text,
                                                    path_to_utf8(resolved)});
        if (pipe == std::string::npos) break;
        begin = pipe + 1;
    }
    return candidates;
}

} // namespace kme::maploader::detail
