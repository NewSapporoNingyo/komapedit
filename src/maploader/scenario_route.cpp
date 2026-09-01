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
#include <cmath>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string_view>

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

ScenarioDocument parse_scenario_loaded_document(const LoadedText& loaded) {
    ScenarioDocument document;
    document.root = loaded.root;
    document.source_hash = loaded.source_hash;
    constexpr std::array<const char*, 8> k_field_names{
        "Title", "Route", "RouteTitle", "Vehicle", "VehicleTitle", "Author", "Image", "Comment"};
    std::array<bool, k_field_names.size()> seen{};
    std::array<std::string, k_field_names.size()> values;
    auto set_field = [&](size_t index, const std::string& value, auto& target, auto parsed) {
        if (seen[index] && values[index] != value) {
            KME_MAPLOADER_LOG_WARN(path_to_utf8(loaded.path.filename()) + " declares multiple " +
                                    k_field_names[index] + " entries; the last one wins.");
        }
        target = std::move(parsed);
        values[index] = value;
        seen[index] = true;
        document.present_fields |= (1u << static_cast<unsigned>(index));
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

struct ScenarioSourceText {
    LoadedText loaded;
    std::string text;
    bool utf8_bom = false;
};

ScenarioSourceText load_scenario_source_text(const std::filesystem::path& path) {
    // load_header_text performs the canonical header/version/encoding checks
    // and computes the baseline hash. Decode the complete source again so
    // source ranges can be patched without touching comments or unknown rows.
    ScenarioSourceText source;
    source.loaded = load_header_text(path, "BveTs Scenario", 2.00, nullptr, true);
    const std::string bytes = read_binary_file(path);
    source.utf8_bom = has_utf8_bom(bytes);
    if (source.loaded.encoding == "utf-16le") {
        source.text = decode_utf16(bytes, true);
    } else if (source.loaded.encoding == "utf-16be") {
        source.text = decode_utf16(bytes, false);
    } else if (source.utf8_bom) {
        source.text = decode_codepage(bytes.substr(3), 65001, true);
    } else if (source.loaded.encoding == "cp932") {
        source.text = decode_codepage(bytes, 932, false);
    } else {
        source.text = decode_codepage(bytes, 65001, true);
    }
    const TextLineSpan first_line = text_line_span(source.text, 0);
    source.loaded.body_offset = first_line.has_terminator() ? first_line.next_begin : source.text.size();
    source.loaded.body = source.loaded.body_offset < source.text.size()
        ? source.text.substr(source.loaded.body_offset) : std::string();
    return source;
}

struct ScenarioFieldRange {
    bool present = false;
    size_t value_begin = 0;
    size_t value_end = 0;
    std::string value;
};

size_t unquoted_comment_begin(std::string_view line) {
    bool single_quoted = false;
    bool double_quoted = false;
    for (size_t i = 0; i < line.size(); ++i) {
        const char ch = line[i];
        if (ch == '\'' && !double_quoted) {
            single_quoted = !single_quoted;
        } else if (ch == '"' && !single_quoted) {
            double_quoted = !double_quoted;
        } else if ((ch == '#' || ch == ';') &&
                   !single_quoted && !double_quoted) {
            return i;
        }
    }
    return line.size();
}

std::array<ScenarioFieldRange, 8> find_scenario_field_ranges(
    const ScenarioSourceText& source) {
    std::array<ScenarioFieldRange, 8> result{};
    const std::array<const char*, 8> keys{
        "Title", "Route", "RouteTitle", "Vehicle", "VehicleTitle", "Author", "Image", "Comment"};
    size_t pos = source.loaded.body_offset;
    while (pos <= source.text.size()) {
        const TextLineSpan line = text_line_span(source.text, pos);
        const size_t content_end = line.content_end;
        const std::string_view text(source.text.data() + pos, content_end - pos);
        const size_t comment = unquoted_comment_begin(text);
        const size_t equal = text.substr(0, comment).find('=');
        if (equal != std::string_view::npos) {
            size_t key_begin = 0;
            while (key_begin < equal && std::isspace(static_cast<unsigned char>(text[key_begin]))) ++key_begin;
            size_t key_end = equal;
            while (key_end > key_begin && std::isspace(static_cast<unsigned char>(text[key_end - 1]))) --key_end;
            const std::string key(text.substr(key_begin, key_end - key_begin));
            size_t index = keys.size();
            for (size_t i = 0; i < keys.size(); ++i) {
                if (key == keys[i]) { index = i; break; }
            }
            if (index < keys.size()) {
                const size_t value_region_begin = pos + equal + 1;
                const size_t value_region_end = pos + comment;
                size_t value_begin = value_region_begin;
                while (value_begin < value_region_end &&
                       std::isspace(static_cast<unsigned char>(source.text[value_begin]))) ++value_begin;
                size_t value_end = value_region_end;
                while (value_end > value_begin &&
                       std::isspace(static_cast<unsigned char>(source.text[value_end - 1]))) --value_end;
                result[index] = ScenarioFieldRange{
                    true, value_begin, value_end,
                    source.text.substr(value_begin, value_end - value_begin)};
            }
        }
        if (!line.has_terminator()) break;
        pos = line.next_begin;
    }
    return result;
}

struct ScenarioReplacement {
    size_t begin = 0;
    size_t end = 0;
    std::string text;
};

void add_replacement(std::vector<ScenarioReplacement>& replacements,
                     size_t begin, size_t end, std::string text) {
    replacements.push_back(ScenarioReplacement{begin, end, std::move(text)});
}

std::string scenario_newline(const std::string& newline) {
    if (newline == "lf") return "\n";
    if (newline == "cr") return "\r";
    return "\r\n";
}

std::string scenario_weight_text(double weight) {
    return canonical_number(weight);
}

void validate_scenario_edit_path(const ScenarioEditPath& row, const char* field_name) {
    if (trim_field_copy(row.path).empty()) {
        throw std::runtime_error(std::string("Scenario ") + field_name + " path cannot be empty");
    }
    if (row.path.find_first_of("\r\n|") != std::string::npos || row.path.find('*') != std::string::npos) {
        throw std::runtime_error(std::string("Scenario ") + field_name + " path contains reserved syntax");
    }
    if (!std::isfinite(row.weight) || row.weight <= 0.0) {
        throw std::runtime_error(std::string("Scenario ") + field_name +
                                 " weight must be a positive finite number");
    }
    if (!row.has_explicit_weight && row.weight != 1.0) {
        throw std::runtime_error(std::string("Scenario ") + field_name +
                                 " omitted weight must remain 1");
    }
}

void patch_scenario_path_value(const std::string& source_text,
                              const ScenarioFieldRange& field,
                              const std::vector<ScenarioEditPath>& desired,
                              const char* field_name,
                              std::vector<ScenarioReplacement>& replacements) {
    const std::vector<ScenarioPathWeight> current =
        parse_weighted_path_terms(field.value, field_name);
    if (current.size() != desired.size()) {
        throw std::runtime_error(std::string("Scenario ") + field_name +
                                 " candidate count cannot change");
    }
    size_t term_begin = field.value_begin;
    for (size_t i = 0; i < current.size(); ++i) {
        size_t term_end = source_text.find('|', term_begin);
        if (term_end == std::string::npos || term_end > field.value_end) term_end = field.value_end;
        size_t raw_begin = term_begin;
        while (raw_begin < term_end && std::isspace(static_cast<unsigned char>(source_text[raw_begin]))) ++raw_begin;
        size_t raw_end = term_end;
        while (raw_end > raw_begin && std::isspace(static_cast<unsigned char>(source_text[raw_end - 1]))) --raw_end;
        const size_t star_rel = source_text.find('*', raw_begin);
        const bool has_star = star_rel != std::string::npos && star_rel < raw_end;
        size_t path_begin = raw_begin;
        size_t path_end = has_star ? star_rel : raw_end;
        while (path_end > path_begin && std::isspace(static_cast<unsigned char>(source_text[path_end - 1]))) --path_end;
        while (path_begin < path_end && std::isspace(static_cast<unsigned char>(source_text[path_begin]))) ++path_begin;

        const ScenarioEditPath& target = desired[i];
        validate_scenario_edit_path(target, field_name);
        if (current[i].path_text != target.path) {
            add_replacement(replacements, path_begin, path_end, target.path);
        }
        if (target.has_explicit_weight) {
            const std::string weight = scenario_weight_text(target.weight);
            if (has_star) {
                size_t weight_begin = star_rel + 1;
                while (weight_begin < raw_end && std::isspace(static_cast<unsigned char>(source_text[weight_begin]))) ++weight_begin;
                size_t weight_end = raw_end;
                while (weight_end > weight_begin && std::isspace(static_cast<unsigned char>(source_text[weight_end - 1]))) --weight_end;
                if (!current[i].has_explicit_weight || current[i].weight != target.weight) {
                    add_replacement(replacements, weight_begin, weight_end, weight);
                }
            } else {
                add_replacement(replacements, raw_end, raw_end, " * " + weight);
            }
        } else if (has_star) {
            // Removing an explicit suffix restores the official default-weight
            // representation while retaining the candidate path and spacing.
            size_t remove_begin = star_rel;
            while (remove_begin > path_end && std::isspace(static_cast<unsigned char>(source_text[remove_begin - 1]))) --remove_begin;
            add_replacement(replacements, remove_begin, raw_end, {});
        }
        if (term_end >= field.value_end) break;
        term_begin = term_end + 1;
    }
}

} // namespace

ScenarioDocument load_scenario_document(const std::filesystem::path& scenario_path) {
    // Validates the official "BveTs Scenario 2.00:<encoding>" header and
    // decodes the body with the declared encoding (UTF-8 default, CP932 for
    // shift_jis/sjis), mirroring map file loading.
    const LoadedText loaded =
        load_header_text(scenario_path, "BveTs Scenario", 2.00, nullptr, true);
    return parse_scenario_loaded_document(loaded);
}

ScenarioDocument save_scenario_document(const std::filesystem::path& scenario_path,
                                        const ScenarioEditRequest& request) {
    ScenarioSourceText source = load_scenario_source_text(scenario_path);
    if (request.expected_source_hash.empty()) {
        throw std::runtime_error("Scenario expected source hash is empty");
    }
    if (source.loaded.source_hash != request.expected_source_hash) {
        throw std::runtime_error("Scenario source file changed externally; reload before saving");
    }

    const ScenarioDocument before = parse_scenario_loaded_document(source.loaded);
    if (before.routes.size() != request.routes.size() ||
        before.vehicles.size() != request.vehicles.size()) {
        throw std::runtime_error("Scenario candidate count cannot change");
    }

    const auto ranges = find_scenario_field_ranges(source);
    std::vector<ScenarioReplacement> replacements;
    replacements.reserve(32);
    const std::array<std::string, 8> scalar_values{
        request.title, {}, request.route_title, {}, request.vehicle_title,
        request.author, request.image, request.comment};
    const std::array<const char*, 8> scalar_names{
        "Title", "Route", "RouteTitle", "Vehicle", "VehicleTitle", "Author", "Image", "Comment"};
    const std::array<const std::vector<ScenarioEditPath>*, 8> path_values{
        nullptr, &request.routes, nullptr, &request.vehicles, nullptr, nullptr, nullptr, nullptr};
    std::string missing_scalar_insertions;
    const std::string insertion_newline = scenario_newline(source.loaded.newline);

    for (size_t i = 0; i < scalar_names.size(); ++i) {
        if (path_values[i]) {
            if (!ranges[i].present) {
                if (!path_values[i]->empty()) {
                    throw std::runtime_error(std::string("Scenario ") + scalar_names[i] +
                                             " entry is missing; candidate insertion is not supported");
                }
                continue;
            }
            patch_scenario_path_value(source.text, ranges[i], *path_values[i], scalar_names[i], replacements);
            continue;
        }
        const std::string& desired = scalar_values[i];
        if (ranges[i].present) {
            add_replacement(replacements, ranges[i].value_begin, ranges[i].value_end, desired);
        } else if (!desired.empty()) {
            // Missing scalar fields are appended in canonical key spelling;
            // existing rows, comments and unknown keys retain their order.
            if (!missing_scalar_insertions.empty()) missing_scalar_insertions += insertion_newline;
            missing_scalar_insertions += scalar_names[i];
            missing_scalar_insertions += " = ";
            missing_scalar_insertions += desired;
        }
    }
    if (!missing_scalar_insertions.empty()) {
        const bool needs_leading = !source.text.empty() && source.text.back() != '\n' && source.text.back() != '\r';
        std::string insertion;
        if (needs_leading) insertion += insertion_newline;
        insertion += missing_scalar_insertions;
        if (source.loaded.newline != "none" || needs_leading) insertion += insertion_newline;
        add_replacement(replacements, source.text.size(), source.text.size(), std::move(insertion));
    }

    std::stable_sort(replacements.begin(), replacements.end(),
                     [](const ScenarioReplacement& a, const ScenarioReplacement& b) {
                         if (a.begin != b.begin) return a.begin > b.begin;
                         return a.end > b.end;
                     });
    for (size_t i = 1; i < replacements.size(); ++i) {
        if (replacements[i - 1].begin < replacements[i].end) {
            throw std::runtime_error("Scenario source patch ranges overlap");
        }
    }
    std::string patched = source.text;
    for (const ScenarioReplacement& replacement : replacements) {
        patched.replace(replacement.begin, replacement.end - replacement.begin, replacement.text);
    }

    LoadedText patched_loaded = source.loaded;
    patched_loaded.body = patched.substr(source.loaded.body_offset);
    const ScenarioDocument expected = parse_scenario_loaded_document(patched_loaded);
    if (expected.title != request.title || expected.route_title != request.route_title ||
        expected.vehicle_title != request.vehicle_title || expected.author != request.author ||
        expected.image != request.image || expected.comment != request.comment ||
        expected.routes.size() != request.routes.size() ||
        expected.vehicles.size() != request.vehicles.size()) {
        throw std::runtime_error("Scenario save validation did not match the requested fields");
    }
    auto compare_paths = [](const std::vector<ScenarioPathWeight>& actual,
                            const std::vector<ScenarioEditPath>& desired,
                            const char* field) {
        for (size_t i = 0; i < actual.size(); ++i) {
            if (actual[i].path_text != desired[i].path ||
                actual[i].has_explicit_weight != desired[i].has_explicit_weight ||
                std::abs(actual[i].weight - desired[i].weight) >
                    std::max(1e-12, std::abs(desired[i].weight) * 1e-12)) {
                throw std::runtime_error(std::string("Scenario save validation did not match ") + field);
            }
        }
    };
    compare_paths(expected.routes, request.routes, "Route candidates");
    compare_paths(expected.vehicles, request.vehicles, "Vehicle candidates");

    const std::string encoded = encode_text_for_writeback(
        patched, source.loaded.encoding, source.utf8_bom);
    const std::string new_hash = hex64(stable_hash64(encoded));
    if (new_hash != source.loaded.source_hash) {
        transactional_write_file(scenario_path, encoded, source.loaded.source_hash, new_hash);
    }
    ScenarioDocument result = load_scenario_document(scenario_path);
    if (result.source_hash.empty()) result.source_hash = new_hash;
    return result;
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
