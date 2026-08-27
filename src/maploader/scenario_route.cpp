/*
 * Copyright (c) 2026 Sapporo_ningyo
 *
 * Licensed under Apache License 2.0; see LICENSE and NOTICE.
 */

#include "scenario_route.h"

#include "diagnostics.h"
#include "maploader_internal.h"

#include <algorithm>
#include <array>
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

ScenarioPathWeight parse_weighted_path_term(const std::string& raw_term,
                                            const std::string& value,
                                            const char* field_name) {
    ScenarioPathWeight term;
    const size_t star = raw_term.find('*');
    std::string weight_text;
    if (star == std::string::npos) {
        term.path_text = trim_field_copy(raw_term);
    } else {
        term.path_text = trim_field_copy(raw_term.substr(0, star));
        weight_text = trim_field_copy(raw_term.substr(star + 1));
        term.has_explicit_weight = true;
    }
    if (term.path_text.empty()) {
        throw std::runtime_error(
            std::string("Scenario ") + field_name +
            " contains an empty candidate: " + value);
    }
    if (term.has_explicit_weight &&
        (!parse_finite_number(weight_text, term.weight) || term.weight <= 0.0)) {
        throw std::runtime_error(
            std::string("Scenario ") + field_name +
            " weight must be a positive finite number: " + weight_text);
    }
    return term;
}

std::vector<ScenarioPathWeight> parse_weighted_path_terms(const std::string& value,
                                                           const char* field_name) {
    if (value.empty()) {
        throw std::runtime_error(std::string("Scenario ") + field_name +
                                 " entry is empty");
    }
    std::vector<ScenarioPathWeight> terms;
    size_t begin = 0;
    while (true) {
        const size_t pipe = value.find('|', begin);
        const std::string term_text = pipe == std::string::npos
            ? value.substr(begin)
            : value.substr(begin, pipe - begin);
        terms.push_back(parse_weighted_path_term(term_text, value, field_name));
        if (pipe == std::string::npos) break;
        begin = pipe + 1;
    }
    return terms;
}

} // namespace

ScenarioDocument load_scenario_document(const std::filesystem::path& scenario_path) {
    // Validates the official "BveTs Scenario 2.00:<encoding>" header and
    // decodes the body with the declared encoding (UTF-8 default, CP932 for
    // shift_jis/sjis), mirroring map file loading.
    const LoadedText loaded =
        load_header_text(scenario_path, "BveTs Scenario", 2.00, nullptr, false);

    ScenarioDocument document;
    document.root = loaded.root;
    constexpr std::array<const char*, 8> k_field_names{
        "Title", "Route", "RouteTitle", "Vehicle", "VehicleTitle", "Author", "Image", "Comment"};
    std::array<bool, k_field_names.size()> seen{};
    std::array<std::string, k_field_names.size()> values;
    auto set_field = [&](size_t index, const std::string& value, auto& target, auto parsed) {
        if (seen[index] && values[index] != value) {
            KME_MAPLOADER_LOG_WARN(path_to_utf8(scenario_path.filename()) + " declares multiple " +
                                    k_field_names[index] + " entries; the last one wins.");
        }
        target = std::move(parsed);
        values[index] = value;
        seen[index] = true;
    };
    for_each_loaded_body_line(loaded, [&](const std::string& line, size_t, size_t, int) {
        const std::string without_comment = strip_ini_comment_copy(line);
        const std::string trimmed = trim_field_copy(without_comment);
        if (trimmed.empty()) return;
        const size_t equals = trimmed.find('=');
        if (equals == std::string::npos) return;
        const std::string key = trim_field_copy(trimmed.substr(0, equals));
        const std::string value = trim_field_copy(trimmed.substr(equals + 1));
        if (key == "Title") {
            set_field(0, value, document.title, value);
        } else if (key == "Route") {
            set_field(1, value, document.routes,
                      parse_weighted_path_terms(value, "Route"));
        } else if (key == "RouteTitle") {
            set_field(2, value, document.route_title, value);
        } else if (key == "Vehicle") {
            set_field(3, value, document.vehicles,
                      parse_weighted_path_terms(value, "Vehicle"));
        } else if (key == "VehicleTitle") {
            set_field(4, value, document.vehicle_title, value);
        } else if (key == "Author") {
            set_field(5, value, document.author, value);
        } else if (key == "Image") {
            set_field(6, value, document.image, value);
        } else if (key == "Comment") {
            set_field(7, value, document.comment, value);
        }
    });
    return document;
}

std::vector<ScenarioRouteCandidate> resolve_scenario_route_candidates(
    const std::filesystem::path& scenario_path) {
    const ScenarioDocument document = load_scenario_document(scenario_path);
    if (document.routes.empty()) {
        throw std::runtime_error(
            "Scenario file has no Route entry: " + path_to_utf8(scenario_path));
    }

    std::vector<ScenarioRouteCandidate> candidates;
    candidates.reserve(document.routes.size());
    for (const ScenarioPathWeight& term : document.routes) {
        const std::filesystem::path resolved =
            join_path(document.root, term.path_text).lexically_normal();
        if (!std::filesystem::is_regular_file(resolved)) {
            throw std::runtime_error(
                "Scenario Route target does not exist: " + path_to_utf8(resolved));
        }
        candidates.push_back(ScenarioRouteCandidate{term.path_text,
                                                    path_to_utf8(resolved)});
    }
    return candidates;
}

} // namespace kme::maploader::detail
