/*
 * Copyright (c) 2026 Sapporo_ningyo
 *
 * Licensed under Apache License 2.0; see LICENSE and NOTICE.
 */

#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace kme::maploader::detail {

enum class BveFileKind {
    Unknown,
    Map,
    Scenario,
};

/* Lightweight header probe that reads only the first bytes of the file.
   Missing or unreadable files report Unknown so the regular loader can
   produce its own detailed error message. */
BveFileKind probe_bve_file_kind(const std::filesystem::path& path);

struct ScenarioRouteCandidate {
    std::string route_text;
    std::string resolved_path;
};

/* Parses every Route candidate of one BVE Scenario file per the official
   Scenario schema: "BveTs Scenario 2.00:<encoding>" header, '#' or ';'
   comments, top-level key = value rows, and Route values of the form
   "path" or "path * weight | path * weight | ...". Relative paths resolve
   against the Scenario file's directory. Every candidate target must exist.
   Throws std::runtime_error with an English diagnostic on failure. */
std::vector<ScenarioRouteCandidate> resolve_scenario_route_candidates(
    const std::filesystem::path& scenario_path);

} // namespace kme::maploader::detail
