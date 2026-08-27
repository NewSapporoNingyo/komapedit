/*
 * Copyright (c) 2026 Sapporo_ningyo
 *
 * Licensed under Apache License 2.0; see LICENSE and NOTICE.
 * The GUI uses Dear ImGui and ImPlot; see THIRD_PARTY_NOTICES.md.
 */

#ifdef _MSC_VER
#pragma execution_character_set("utf-8")
#endif

#include "kme.h"
#include "app_settings.h"
#include "debug_headless.h"
#include "touch_input.h"

#include "canvas3D.h"
#include "maploader.h"
#include "numeric_safety.h"
#include "own_track_transition_linkage.h"
#include "repeater_linkage.h"
#include "text_decoder.h"
#include "resource.h"

#include "imgui.h"
#include "imgui_internal.h"
#include "misc/cpp/imgui_stdlib.h"
#include "imgui_impl_dx11.h"
#include "imgui_impl_win32.h"
#include "implot.h"

#include <windows.h>
#if defined(_MSC_VER) && !defined(NDEBUG)
#include <crtdbg.h>
#endif
#include <commdlg.h>
#include <d3d11.h>
#include <shellapi.h>
#include <shlobj.h>
#include <wincodec.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cctype>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <new>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

template <typename Snapshot>
std::string typed_snapshot_string(const Snapshot& snapshot, KvStringRef ref) {
    if (!snapshot.string_data || ref.offset > snapshot.string_size ||
        ref.length > snapshot.string_size - ref.offset) {
        return {};
    }
    return std::string(snapshot.string_data + static_cast<size_t>(ref.offset),
                       static_cast<size_t>(ref.length));
}

bool typed_snapshot_span_valid(KvSpan span, std::uint64_t size) {
    return span.offset <= size && span.count <= size - span.offset;
}

std::optional<InspectorTargetMetadata> resolve_inspector_target_metadata(
    void* handle, const std::string& edit_id,
    const std::string& expected_row_kind,
    std::string* error_message) {
    if (error_message) error_message->clear();
    if (!handle || edit_id.empty()) {
        if (error_message) *error_message = "invalid edit target request";
        return std::nullopt;
    }

    KvEditTargetSnapshot target{};
    const KvUtf8View edit_id_view{edit_id.data(), static_cast<std::uint64_t>(edit_id.size())};
    if (!kv_get_edit_target_typed(handle, edit_id_view, &target, sizeof(target))) {
        if (error_message) {
            const char* err = kv_get_last_error();
            *error_message = err ? err : "kv_get_edit_target_typed failed";
        }
        return std::nullopt;
    }
    try {
        if (target.version != KV_EDIT_TARGET_SNAPSHOT_VERSION ||
            target.structure_size < sizeof(KvEditTargetSnapshot)) {
            throw std::runtime_error("edit target snapshot version or size mismatch");
        }
        InspectorTargetMetadata info;
        info.row_kind = typed_snapshot_string(target, target.row_kind);
        if (!expected_row_kind.empty() && info.row_kind != expected_row_kind) {
            if (error_message) {
                *error_message = "edit target row kind changed from " +
                    expected_row_kind + " to " + info.row_kind;
            }
            return std::nullopt;
        }
        info.row_index = static_cast<size_t>(target.row_index);
        info.elements_for_statement = static_cast<int>(target.elements_for_statement);
        info.statement_kind = typed_snapshot_string(target, target.statement_kind);
        info.source_hash = typed_snapshot_string(target, target.source_hash);
        info.expected_source_hash = typed_snapshot_string(target, target.expected_source_hash);
        info.source.file_path = typed_snapshot_string(target, target.source_file_path);
        info.source.line = target.source.line;
        info.source.column = target.source.column;
        info.source.raw_text_preview = typed_snapshot_string(target, target.raw_text_preview);
        info.raw_statement = typed_snapshot_string(target, target.raw_text);
        info.raw_arguments = typed_snapshot_string(target, target.raw_arguments);
        info.flags = target.flags;
        if (row_kind_has_source_distance_string(info.row_kind)) {
            info.source_distance_string = typed_snapshot_string(target, target.distance_expression);
        }
        info.distance_value = target.distance_value;
        return info;
    } catch (const std::exception& e) {
        if (error_message) *error_message = e.what();
        return std::nullopt;
    }
}

Matrix copy_buffer(KvDoubleBuffer buffer) {
    Matrix m;
    m.rows = buffer.rows;
    m.cols = buffer.cols;
    if (buffer.data && buffer.rows > 0 && buffer.cols > 0) {
        m.data.assign(buffer.data, buffer.data + buffer.rows * buffer.cols);
    }
    return m;
}

ImVec4 other_track_palette_color(size_t index) {
    static constexpr ImVec4 palette[] = {
        ImVec4(0.12f, 0.47f, 0.71f, 1.0f), ImVec4(1.00f, 0.50f, 0.05f, 1.0f),
        ImVec4(0.17f, 0.63f, 0.17f, 1.0f), ImVec4(0.84f, 0.15f, 0.16f, 1.0f),
        ImVec4(0.58f, 0.40f, 0.74f, 1.0f), ImVec4(0.55f, 0.34f, 0.29f, 1.0f),
        ImVec4(0.89f, 0.47f, 0.76f, 1.0f), ImVec4(0.50f, 0.50f, 0.50f, 1.0f),
        ImVec4(0.74f, 0.74f, 0.13f, 1.0f), ImVec4(0.09f, 0.75f, 0.81f, 1.0f)
    };
    return palette[index % (sizeof(palette) / sizeof(palette[0]))];
}

std::string map_snapshot_string(const KvMapSnapshot& snapshot, KvStringRef ref) {
    return typed_snapshot_string(snapshot, ref);
}

bool map_snapshot_span_valid(KvSpan span, std::uint64_t size) {
    return typed_snapshot_span_valid(span, size);
}

std::string map_snapshot_value_text(const KvMapSnapshot& snapshot, const KvValue& value) {
    switch (value.kind) {
        case KV_VALUE_NUMBER: return format_double(value.number_value, 6);
        case KV_VALUE_STRING: return map_snapshot_string(snapshot, value.string_value);
        case KV_VALUE_CONTINUE: return "c";
        case KV_VALUE_NULL:
        default: return {};
    }
}

std::string map_snapshot_preview_value_text(const KvMapSnapshot& snapshot,
                                            const KvValue& value) {
    if (value.kind == KV_VALUE_NULL) return "null";
    return map_snapshot_value_text(snapshot, value);
}

std::string map_snapshot_track_key_text(const KvMapSnapshot& snapshot, const KvValue& value) {
    if (value.kind == KV_VALUE_STRING) {
        return "'" + map_snapshot_string(snapshot, value.string_value) + "'";
    }
    if (value.kind == KV_VALUE_NUMBER) {
        if (value.number_value == 0.0) return "0";
        std::array<char, 64> buffer{};
        const int written = std::snprintf(buffer.data(), buffer.size(), "%.17g",
                                          value.number_value);
        if (written > 0 && static_cast<size_t>(written) < buffer.size()) {
            return std::string(buffer.data(), static_cast<size_t>(written));
        }
    }
    return map_snapshot_value_text(snapshot, value);
}

std::string map_snapshot_value_span_text(const KvMapSnapshot& snapshot, KvSpan span) {
    if (!map_snapshot_span_valid(span, snapshot.value_count) ||
        (span.count != 0 && !snapshot.values)) {
        return {};
    }
    std::string output;
    for (std::uint64_t i = 0; i < span.count; ++i) {
        if (i) output += ", ";
        output += map_snapshot_value_text(snapshot, snapshot.values[span.offset + i]);
    }
    return output;
}

void apply_map_row_metadata(TableRow& output, const KvMapSnapshot& snapshot,
                            const KvRowMetadata& metadata) {
    output.edit_id = map_snapshot_string(snapshot, metadata.edit_id);
    if (metadata.source_file_index < snapshot.source_file_count && snapshot.source_files) {
        output.source.file_path = map_snapshot_string(
            snapshot, snapshot.source_files[metadata.source_file_index].file_path);
    }
    output.source.line = metadata.line;
    output.source.column = metadata.column;
    output.source.raw_text_preview = map_snapshot_string(snapshot, metadata.raw_text_preview);
}

std::string resolve_list_asset_path(const std::string& list_file,
                                    const std::string& source_path) {
    if (list_file.empty() || source_path.empty()) return {};
    std::filesystem::path resolved = kme::maploader::join_utf8_path(
        kme::maploader::path_from_utf8(list_file).parent_path(), source_path);
    std::error_code ec;
    const std::filesystem::path absolute = std::filesystem::absolute(resolved, ec);
    if (!ec) resolved = absolute;
    return kme::maploader::path_to_utf8(resolved.lexically_normal());
}

std::string preferred_utf8_path(std::filesystem::path path) {
    path = path.lexically_normal();
    path.make_preferred();
    return kme::maploader::path_to_utf8(path);
}

std::string lowercase_ascii_path(std::filesystem::path path) {
    std::string text = kme::maploader::path_to_utf8(path);
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char ch) {
                       return static_cast<char>(ascii_lower(ch));
                   });
    return text;
}

ListAssetSourcePathResult make_list_asset_source_path(
    const std::string& list_file,
    const std::string& selected_file) {
    ListAssetSourcePathResult result;
    const std::filesystem::path selected_path =
        kme::maploader::path_from_utf8(selected_file);
    std::error_code ec;
    std::filesystem::path selected_absolute =
        std::filesystem::absolute(selected_path, ec);
    if (ec) {
        result.resolved_path = preferred_utf8_path(selected_path);
        if (result.resolved_path.empty()) result.resolved_path = selected_file;
        result.source_path = result.resolved_path;
        result.fallback_reason =
            "failed to make the selected file path absolute: " + ec.message();
        return result;
    }
    selected_absolute = selected_absolute.lexically_normal();
    result.resolved_path = preferred_utf8_path(selected_absolute);

    const auto use_absolute = [&](std::string reason) {
        result.source_path = result.resolved_path;
        result.fallback_reason = std::move(reason);
        return result;
    };
    if (list_file.empty()) {
        return use_absolute("the source list file is unavailable");
    }

    const std::filesystem::path list_path =
        kme::maploader::path_from_utf8(list_file);
    std::filesystem::path list_absolute =
        std::filesystem::absolute(list_path, ec);
    if (ec) {
        return use_absolute(
            "failed to make the source list path absolute: " + ec.message());
    }
    const std::filesystem::path list_directory =
        list_absolute.lexically_normal().parent_path();
    if (list_directory.empty()) {
        return use_absolute("the source list directory is unavailable");
    }
    if (lowercase_ascii_path(selected_absolute.root_name()) !=
            lowercase_ascii_path(list_directory.root_name()) ||
        selected_absolute.has_root_directory() !=
            list_directory.has_root_directory()) {
        return use_absolute(
            "the selected file and source list file are on different filesystem roots");
    }

    ec.clear();
    std::filesystem::path relative =
        std::filesystem::relative(selected_absolute, list_directory, ec);
    if (ec) {
        return use_absolute("relative path calculation failed: " + ec.message());
    }
    if (relative.empty() || relative.is_absolute()) {
        return use_absolute("relative path calculation returned no usable path");
    }
    result.source_path = preferred_utf8_path(std::move(relative));
    return result;
}

std::string list_asset_picker_initial_directory(
    const std::string& resolved_file,
    const std::string& list_file) {
    const auto existing_parent = [](const std::string& file) {
        if (file.empty()) return std::string{};
        std::error_code ec;
        std::filesystem::path absolute = std::filesystem::absolute(
            kme::maploader::path_from_utf8(file), ec);
        if (ec) return std::string{};
        std::filesystem::path parent =
            absolute.lexically_normal().parent_path();
        if (parent.empty() || !std::filesystem::is_directory(parent, ec) || ec) {
            return std::string{};
        }
        return preferred_utf8_path(std::move(parent));
    };
    std::string directory = existing_parent(resolved_file);
    return directory.empty() ? existing_parent(list_file) : directory;
}

std::string resolve_list_asset_path(const KvMapSnapshot& snapshot,
                                    const KvRowMetadata& metadata,
                                    const std::string& source_path) {
    if (metadata.source_file_index >= snapshot.source_file_count ||
        !snapshot.source_files) {
        return {};
    }
    return resolve_list_asset_path(
        map_snapshot_string(
            snapshot, snapshot.source_files[metadata.source_file_index].file_path),
        source_path);
}

template <typename Row>
void put_map_common_event_cells(TableRow& output, const KvMapSnapshot& snapshot,
                                const Row& input) {
    output.cells["distance"] = format_double(input.distance, 6);
    output.cells["filePath"] = map_snapshot_string(snapshot, input.file_path);
    output.cells["order"] = std::to_string(input.order);
}

void normalize_cab_illuminance_preview_rows(MapModel& model) {
    std::vector<size_t> rows;
    rows.reserve(model.cab_illuminance.size());
    for (size_t index = 0; index < model.cab_illuminance.size(); ++index) {
        rows.push_back(index);
    }
    std::stable_sort(rows.begin(), rows.end(), [&](size_t left, size_t right) {
        return table_cell_number(model.cab_illuminance[left], "order") <
            table_cell_number(model.cab_illuminance[right], "order");
    });

    std::string previous_value;
    for (const size_t index : rows) {
        TableRow& row = model.cab_illuminance[index];
        const std::string source_value = table_cell(row, "_sourceValue");
        row.cells["value"] = source_value.empty() ? previous_value : source_value;
        if (!source_value.empty()) previous_value = source_value;
    }
}

std::vector<TableRow> hydrate_signal_aspect_rows(
    const KvMapSnapshot& snapshot) {
    std::vector<TableRow> rows;
    rows.reserve(static_cast<size_t>(
        snapshot.signal_aspect_count));
    for (std::uint64_t i = 0;
         i < snapshot.signal_aspect_count; ++i) {
        const KvSignalAspectRow& input =
            snapshot.signal_aspects[i];
        TableRow row;
        row.cells["signalAspectKey"] =
            map_snapshot_string(
                snapshot, input.signal_aspect_key);
        if (map_snapshot_span_valid(
                input.structure_keys,
                snapshot.string_ref_count) &&
            (input.structure_keys.count == 0 ||
             snapshot.string_refs)) {
            for (std::uint64_t j = 0;
                 j < input.structure_keys.count; ++j) {
                row.cells[
                    "structureKey" +
                    std::to_string(j + 1)] =
                    map_snapshot_string(
                        snapshot,
                        snapshot.string_refs[
                            input.structure_keys.offset + j]);
            }
        }
        row.cells["_structureKeyCount"] =
            std::to_string(input.structure_keys.count);
        const size_t total_structure_key_count =
            static_cast<size_t>(
                input.structure_keys.count);
        const size_t main_structure_key_count =
            static_cast<size_t>(input.metadata.reserved);
        if (main_structure_key_count >
            total_structure_key_count) {
            throw std::runtime_error(
                "Signal aspect snapshot has an invalid main-row structure-key count");
        }
        const size_t glare_structure_key_count =
            total_structure_key_count -
            main_structure_key_count;
        row.cells["_signalMainStructureKeyCount"] =
            std::to_string(main_structure_key_count);
        row.cells["_signalGlareStructureKeyCount"] =
            std::to_string(glare_structure_key_count);
        apply_map_row_metadata(
            row, snapshot, input.metadata);
        rows.push_back(std::move(row));
    }
    return rows;
}

void bind_station_position_edit_ids(MapModel& model) {
    for (Station& station : model.station_positions) station.edit_id.clear();

    size_t station_row_count = 0;
    for (const TableRow& row : model.station_list_rows) {
        if (!table_cell(row, "posKey").empty()) ++station_row_count;
    }

    if (station_row_count == model.station_positions.size()) {
        size_t row_cursor = 0;
        for (Station& station : model.station_positions) {
            while (row_cursor < model.station_list_rows.size() &&
                   table_cell(model.station_list_rows[row_cursor], "posKey").empty()) {
                ++row_cursor;
            }
            if (row_cursor >= model.station_list_rows.size()) return;
            station.edit_id = model.station_list_rows[row_cursor++].edit_id;
        }
        return;
    }

    constexpr double k_station_row_distance_epsilon = 1e-6;
    size_t row_cursor = 0;
    for (Station& station : model.station_positions) {
        while (row_cursor < model.station_list_rows.size()) {
            const double row_distance =
                table_cell_number(model.station_list_rows[row_cursor], "_distance");
            if (std::isfinite(row_distance) &&
                row_distance >= station.distance - k_station_row_distance_epsilon) {
                break;
            }
            ++row_cursor;
        }

        const TableRow* matched_row = nullptr;
        size_t scan = row_cursor;
        while (scan < model.station_list_rows.size()) {
            const TableRow& row = model.station_list_rows[scan];
            const double row_distance = table_cell_number(row, "_distance");
            if (std::isfinite(row_distance) &&
                row_distance > station.distance + k_station_row_distance_epsilon) {
                break;
            }
            if (std::isfinite(row_distance) &&
                std::abs(row_distance - station.distance) <=
                    k_station_row_distance_epsilon &&
                table_cell(row, "posKey") == station.key) {
                matched_row = &row;
            }
            ++scan;
        }
        row_cursor = scan;
        if (matched_row) station.edit_id = matched_row->edit_id;
    }
}

void rebuild_speed_limit_runtime_cache(MapModel& model) {
    model.speedlimits.clear();
    model.speedlimits.reserve(model.speed_limit_rows.size());
    for (size_t row_index = 0; row_index < model.speed_limit_rows.size();
         ++row_index) {
        const TableRow& row = model.speed_limit_rows[row_index];
        SpeedLimit speed;
        speed.distance = table_cell_number(row, "distance");
        speed.has_speed = ascii_lower(table_cell(row, "method")) == "begin";
        if (speed.has_speed) speed.speed = table_cell_number(row, "speed");
        speed.order = static_cast<int>(table_cell_number(row, "order"));
        speed.row_index = row_index;
        model.speedlimits.push_back(speed);
    }
    std::stable_sort(
        model.speedlimits.begin(), model.speedlimits.end(),
        [](const SpeedLimit& left, const SpeedLimit& right) {
            if (left.distance != right.distance) {
                return left.distance < right.distance;
            }
            if (left.order != right.order) return left.order < right.order;
            return left.row_index < right.row_index;
        });
}

void annotate_own_track_transition_links(MapModel& model) {
    using namespace own_track_transition_linkage;
    std::vector<Event> events;
    events.reserve(model.curve_rows.size() + model.gradient_rows.size());
    for (size_t i = 0; i < model.curve_rows.size(); ++i) {
        TableRow& row = model.curve_rows[i];
        row.cells["_transitionEditId"].clear();
        row.cells["_primaryEditId"].clear();
        row.cells["_transitionStatus"] = "none";
        const std::string method = ascii_lower(table_cell(row, "method"));
        const EventKind kind = method == "curve.begintransition"
            ? EventKind::CurveBeginTransition
            : method == "curve.begin" ? EventKind::CurveBegin
            : method == "curve.begincircular" ? EventKind::CurveBeginCircular
            : method == "curve.end" ? EventKind::CurveEnd
                                      : EventKind::CurveOther;
        events.push_back(Event{i, static_cast<int>(table_cell_number(row, "order")),
                               static_cast<int>(table_cell_number(row, "argumentCount")), kind});
    }
    const size_t gradient_base = model.curve_rows.size();
    for (size_t i = 0; i < model.gradient_rows.size(); ++i) {
        TableRow& row = model.gradient_rows[i];
        row.cells["_transitionEditId"].clear();
        row.cells["_primaryEditId"].clear();
        row.cells["_transitionStatus"] = "none";
        const std::string method = ascii_lower(table_cell(row, "method"));
        const EventKind kind = method == "gradient.begintransition"
            ? EventKind::GradientBeginTransition
            : method == "gradient.end" ? EventKind::GradientEnd
                                        : EventKind::GradientBegin;
        events.push_back(Event{gradient_base + i,
                               static_cast<int>(table_cell_number(row, "order")),
                               static_cast<int>(table_cell_number(row, "argumentCount")), kind});
    }
    auto row_at = [&](size_t source_index) -> TableRow& {
        return source_index < gradient_base
            ? model.curve_rows[source_index]
            : model.gradient_rows[source_index - gradient_base];
    };
    const Linkage linkage = pair_transitions(std::move(events));
    for (const Pair& pair : linkage.pairs) {
        TableRow& transition = row_at(pair.transition_source_index);
        TableRow& primary = row_at(pair.primary_source_index);
        transition.cells["_primaryEditId"] = primary.edit_id;
        transition.cells["_transitionStatus"] = "paired";
        primary.cells["_transitionEditId"] = transition.edit_id;
        primary.cells["_transitionStatus"] = "paired";
    }
    for (size_t source_index : linkage.orphan_transition_source_indices) {
        row_at(source_index).cells["_transitionStatus"] = "orphan";
    }
}

MapModel hydrate_map_snapshot(const KvMapSnapshot& snapshot,
                              const std::string& path,
                              double snapshot_call_seconds) {
    const auto hydrate_started_at = std::chrono::steady_clock::now();
    MapModel model;
    model.path = path;
    model.snapshot_build_seconds = snapshot.build_seconds > 0.0
        ? snapshot.build_seconds : snapshot_call_seconds;

    model.file_structure.reserve(static_cast<size_t>(snapshot.file_structure_count));
    for (std::uint64_t i = 0; i < snapshot.file_structure_count; ++i) {
        const KvFileStructureRow& input = snapshot.file_structure[i];
        FileStructureNode node;
        node.include_path = map_snapshot_string(snapshot, input.include_path);
        node.absolute_path = map_snapshot_string(snapshot, input.absolute_path);
        node.display_name = display_name_from_path(node.absolute_path);
        if (node.display_name.empty()) node.display_name = node.include_path;
        if (!model.file_structure.empty()) {
            node.parent_index = input.parent_index >= 0 &&
                    static_cast<std::uint64_t>(input.parent_index) < model.file_structure.size()
                ? static_cast<size_t>(input.parent_index) : 0;
        }
        model.file_structure.push_back(std::move(node));
    }
    if (model.file_structure.empty() && !path.empty()) {
        FileStructureNode root;
        root.absolute_path = path;
        root.display_name = display_name_from_path(path);
        model.file_structure.push_back(std::move(root));
    }
    model.edit_files.reserve(static_cast<size_t>(snapshot.source_file_count));
    for (std::uint64_t i = 0; i < snapshot.source_file_count; ++i) {
        const KvSourceFileRow& input = snapshot.source_files[i];
        EditSourceFileInfo file;
        file.file_path = map_snapshot_string(snapshot, input.file_path);
        file.display_path = map_snapshot_string(snapshot, input.display_path);
        file.encoding = map_snapshot_string(snapshot, input.encoding);
        file.newline = map_snapshot_string(snapshot, input.newline);
        file.source_hash = map_snapshot_string(snapshot, input.source_hash);
        file.byte_length = static_cast<size_t>(input.byte_length);
        model.edit_files.push_back(std::move(file));
    }
    std::set<std::string> loaded_file_paths;
    for (const EditSourceFileInfo& file : model.edit_files) {
        loaded_file_paths.insert(file.file_path);
    }
    for (FileStructureNode& node : model.file_structure) {
        if (node.parent_index != k_no_file_structure_parent) {
            node.is_valid = loaded_file_paths.find(node.absolute_path) !=
                loaded_file_paths.end();
        }
    }
    model.file_structure_revision = file_structure_revision(model.file_structure);

    model.edit_statements.reserve(static_cast<size_t>(snapshot.statement_count));
    for (std::uint64_t i = 0; i < snapshot.statement_count; ++i) {
        const KvStatementRow& input = snapshot.statements[i];
        EditStatementInfo row;
        row.edit_id = map_snapshot_string(snapshot, input.edit_id);
        row.statement_kind = map_snapshot_string(snapshot, input.statement_kind);
        if (input.source.source_file_index < snapshot.source_file_count && snapshot.source_files) {
            row.source.file_path = map_snapshot_string(
                snapshot, snapshot.source_files[input.source.source_file_index].file_path);
        }
        row.source.line = input.source.line;
        row.source.column = input.source.column;
        row.source.raw_text_preview = map_snapshot_string(snapshot, input.raw_text_preview);
        row.raw_text = map_snapshot_string(snapshot, input.raw_text);
        row.raw_arguments = map_snapshot_string(snapshot, input.raw_arguments);
        row.distance_expression = map_snapshot_string(snapshot, input.distance_expression);
        row.distance_value = input.distance_value;
        row.global_order = input.global_order;
        if (snapshot.values && input.evaluated_values.count != 0 &&
            input.evaluated_values.offset < snapshot.value_count &&
            snapshot.value_count - input.evaluated_values.offset >=
                input.evaluated_values.count) {
            row.first_evaluated_value = map_snapshot_value_text(
                snapshot, snapshot.values[input.evaluated_values.offset]);
        }
        model.edit_statements.push_back(std::move(row));
    }
    model.edit_elements.reserve(static_cast<size_t>(snapshot.element_count));
    for (std::uint64_t i = 0; i < snapshot.element_count; ++i) {
        const KvElementRow& input = snapshot.elements[i];
        EditElementInfo row;
        row.edit_id = map_snapshot_string(snapshot, input.edit_id);
        row.row_kind = map_snapshot_string(snapshot, input.row_kind);
        row.row_index = static_cast<size_t>(input.row_index);
        if (input.source_file_index < snapshot.source_file_count && snapshot.source_files) {
            row.source_file_path = map_snapshot_string(
                snapshot, snapshot.source_files[input.source_file_index].file_path);
        }
        row.global_order = input.global_order;
        model.edit_elements.push_back(std::move(row));
    }

    double buffer_copy_seconds = 0.0;
    auto copy_buffer_timed = [&buffer_copy_seconds](KvDoubleBuffer buffer) {
        const auto started_at = std::chrono::steady_clock::now();
        Matrix matrix = copy_buffer(buffer);
        buffer_copy_seconds += std::chrono::duration<double>(
            std::chrono::steady_clock::now() - started_at).count();
        return matrix;
    };
    model.own = copy_buffer_timed(snapshot.own_track_geometry);
    model.curve = copy_buffer_timed(snapshot.curve_radius_geometry);
    if (snapshot.controlpoints && snapshot.controlpoint_count != 0) {
        model.controlpoints.assign(snapshot.controlpoints,
                                   snapshot.controlpoints + snapshot.controlpoint_count);
    }
    for (size_t i = 0; i < 3; ++i) model.cp_arb[i] = snapshot.cp_arbdistribution[i];
    if (!model.own.empty()) {
        model.distance_origin = model.own.at(0, 0);
        model.height_origin = model.own.at(0, 3);
        model.origin_angle = model.own.at(0, 4);
    }

    model.other_tracks.reserve(static_cast<size_t>(snapshot.other_track_count));
    for (std::uint64_t i = 0; i < snapshot.other_track_count; ++i) {
        const KvOtherTrackRow& input = snapshot.other_tracks[i];
        OtherTrack track;
        track.key = map_snapshot_string(snapshot, input.key);
        track.color = other_track_palette_color(static_cast<size_t>(i));
        track.range_min = input.range_min;
        track.range_max = input.range_max;
        track.points = copy_buffer_timed(input.points);
        if (!track.points.empty() && track.range_min == track.range_max) {
            track.range_min = track.points.at(0, 0);
            track.range_max = track.points.at(track.points.rows - 1, 0);
        }
        model.other_tracks.push_back(std::move(track));
    }
    model.own_events.reserve(static_cast<size_t>(snapshot.own_track_event_count));
    for (std::uint64_t i = 0; i < snapshot.own_track_event_count; ++i) {
        const KvTrackEventRow& input = snapshot.own_track_events[i];
        TrackEvent event;
        event.distance = input.distance;
        event.key = map_snapshot_string(snapshot, input.key);
        event.flag = map_snapshot_string(snapshot, input.flag);
        if (input.value.kind == KV_VALUE_NUMBER) {
            event.value_number = true;
            event.number = input.value.number_value;
        } else {
            event.text = map_snapshot_value_text(snapshot, input.value);
        }
        model.own_events.push_back(std::move(event));
    }
    model.curve_rows.reserve(static_cast<size_t>(snapshot.curve_count));
    for (std::uint64_t i = 0; i < snapshot.curve_count; ++i) {
        const KvCurveRow& input = snapshot.curves[i];
        TableRow row;
        row.cells["distance"] = format_double(input.distance, 6);
        row.cells["method"] = map_snapshot_string(snapshot, input.method);
        row.cells["radius"] = map_snapshot_value_text(snapshot, input.radius);
        row.cells["cant"] = map_snapshot_value_text(snapshot, input.cant);
        row.cells["argumentCount"] = std::to_string(input.argument_count);
        row.cells["filePath"] = map_snapshot_string(snapshot, input.file_path);
        row.cells["order"] = std::to_string(input.order);
        apply_map_row_metadata(row, snapshot, input.metadata);
        model.curve_rows.push_back(std::move(row));
    }
    model.gradient_rows.reserve(static_cast<size_t>(snapshot.gradient_count));
    for (std::uint64_t i = 0; i < snapshot.gradient_count; ++i) {
        const KvGradientRow& input = snapshot.gradients[i];
        TableRow row;
        row.cells["distance"] = format_double(input.distance, 6);
        row.cells["method"] = map_snapshot_string(snapshot, input.method);
        row.cells["gradient"] = map_snapshot_value_text(snapshot, input.gradient);
        row.cells["argumentCount"] = std::to_string(input.argument_count);
        row.cells["filePath"] = map_snapshot_string(snapshot, input.file_path);
        row.cells["order"] = std::to_string(input.order);
        apply_map_row_metadata(row, snapshot, input.metadata);
        model.gradient_rows.push_back(std::move(row));
    }
    model.other_track_changes.reserve(
        static_cast<size_t>(snapshot.other_track_change_count));
    for (std::uint64_t i = 0; i < snapshot.other_track_change_count; ++i) {
        const KvOtherTrackChangeRow& input = snapshot.other_track_changes[i];
        TableRow row;
        row.cells["distance"] = format_double(input.distance, 6);
        row.cells["trackKey"] = map_snapshot_track_key_text(snapshot, input.track_key);
        row.cells["method"] = map_snapshot_string(snapshot, input.method);
        row.cells["parameterCount"] = std::to_string(input.parameters.count);
        row.cells["parameters"] = map_snapshot_value_span_text(snapshot, input.parameters);
        if (map_snapshot_span_valid(input.parameters, snapshot.value_count) &&
            (input.parameters.count == 0 || snapshot.values)) {
            for (std::uint64_t parameter = 0;
                 parameter < input.parameters.count; ++parameter) {
                row.cells["parameter" + std::to_string(parameter)] =
                    map_snapshot_value_text(
                        snapshot,
                        snapshot.values[input.parameters.offset + parameter]);
            }
        }
        row.cells["filePath"] = map_snapshot_string(snapshot, input.file_path);
        row.cells["order"] = std::to_string(input.order);
        apply_map_row_metadata(row, snapshot, input.metadata);
        model.other_track_changes.push_back(std::move(row));
    }
    annotate_own_track_transition_links(model);
    model.speed_limit_rows.reserve(
        static_cast<size_t>(snapshot.speed_limit_count));
    for (std::uint64_t i = 0; i < snapshot.speed_limit_count; ++i) {
        const KvSpeedLimitRow& input = snapshot.speed_limits[i];
        TableRow row;
        row.cells["distance"] = format_double(input.distance, 6);
        row.cells["method"] =
            input.speed.kind == KV_VALUE_NUMBER ? "Begin" : "End";
        row.cells["speed"] = input.speed.kind == KV_VALUE_NUMBER
            ? map_snapshot_value_text(snapshot, input.speed)
            : std::string{};
        row.cells["filePath"] = map_snapshot_string(snapshot, input.file_path);
        row.cells["order"] = std::to_string(input.order);
        apply_map_row_metadata(row, snapshot, input.metadata);
        model.speed_limit_rows.push_back(std::move(row));
    }
    rebuild_speed_limit_runtime_cache(model);
    for (std::uint64_t i = 0; i < snapshot.station_name_count; ++i) {
        const KvStationNameRow& input = snapshot.station_names[i];
        model.station_names[map_snapshot_string(snapshot, input.key)] =
            map_snapshot_string(snapshot, input.name);
    }
    std::set<std::string> seen_stations;
    size_t own_index = 0;
    model.station_positions.reserve(static_cast<size_t>(snapshot.station_position_count));
    model.stations.reserve(static_cast<size_t>(snapshot.station_position_count));
    for (std::uint64_t i = 0; i < snapshot.station_position_count; ++i) {
        const KvStationPositionRow& input = snapshot.station_positions[i];
        Station station;
        station.distance = input.distance;
        station.key = map_snapshot_string(snapshot, input.key);
        auto name = model.station_names.find(station.key);
        if (name != model.station_names.end()) station.name = name->second;
        if (station.name.empty()) station.name = station.key;
        station.mileage = station.distance - model.distance_origin;
        if (!model.own.empty()) {
            while (own_index + 1 < model.own.rows && model.own.at(own_index, 0) < station.distance) {
                ++own_index;
            }
            station.x = model.own.at(own_index, 1);
            station.y = model.own.at(own_index, 2);
            station.z = model.own.at(own_index, 3);
        }
        model.station_positions.push_back(station);
        if (seen_stations.insert(station.key).second) model.stations.push_back(std::move(station));
    }

    auto add_structure_put = [&](const KvStructurePutRow& input) {
        TableRow row;
        put_map_common_event_cells(row, snapshot, input);
        row.cells["method"] = map_snapshot_string(snapshot, input.method);
        row.cells["structureKey"] = map_snapshot_value_text(snapshot, input.structure_key);
        row.cells["trackKey"] = map_snapshot_track_key_text(snapshot, input.track_key);
        row.cells["x"] = format_double(input.x, 6); row.cells["y"] = format_double(input.y, 6);
        row.cells["z"] = format_double(input.z, 6); row.cells["rx"] = format_double(input.rx, 6);
        row.cells["ry"] = format_double(input.ry, 6); row.cells["rz"] = format_double(input.rz, 6);
        row.cells["tilt"] = format_double(input.tilt, 6);
        row.cells["span"] = format_double(input.span, 6);
        apply_map_row_metadata(row, snapshot, input.metadata);
        model.structures.push_back(std::move(row));
    };
    model.structures.reserve(static_cast<size_t>(snapshot.structure_put_count));
    for (std::uint64_t i = 0; i < snapshot.structure_put_count; ++i) {
        add_structure_put(snapshot.structure_puts[i]);
    }
    model.structures_between.reserve(static_cast<size_t>(snapshot.structure_between_count));
    for (std::uint64_t i = 0; i < snapshot.structure_between_count; ++i) {
        const KvStructureBetweenRow& input = snapshot.structure_betweens[i];
        TableRow row;
        put_map_common_event_cells(row, snapshot, input);
        row.cells["method"] = map_snapshot_string(snapshot, input.method);
        row.cells["structureKey"] = map_snapshot_value_text(snapshot, input.structure_key);
        row.cells["trackKey1"] = map_snapshot_track_key_text(snapshot, input.track_key1);
        row.cells["trackKey2"] = map_snapshot_track_key_text(snapshot, input.track_key2);
        row.cells["flag"] = format_double(input.flag, 6);
        apply_map_row_metadata(row, snapshot, input.metadata);
        model.structures_between.push_back(std::move(row));
    }
    model.structure_models.reserve(static_cast<size_t>(snapshot.structure_model_count));
    for (std::uint64_t i = 0; i < snapshot.structure_model_count; ++i) {
        const KvStructureModelRow& input = snapshot.structure_models[i];
        TableRow row;
        row.cells["structureKey"] = map_snapshot_string(snapshot, input.structure_key);
        row.cells["filePath"] = map_snapshot_string(snapshot, input.file_path);
        row.cells["resolvedFilePath"] = resolve_list_asset_path(
            snapshot, input.metadata, row.cells["filePath"]);
        apply_map_row_metadata(row, snapshot, input.metadata);
        model.structure_models.push_back(std::move(row));
    }
    model.other_trains.reserve(static_cast<size_t>(snapshot.other_train_definition_count));
    for (std::uint64_t i = 0; i < snapshot.other_train_definition_count; ++i) {
        const KvOtherTrainDefinitionRow& input = snapshot.other_train_definitions[i];
        TableRow row;
        row.cells["distance"] = format_double(input.distance, 6);
        row.cells["method"] = map_snapshot_string(snapshot, input.method);
        row.cells["trainKey"] = map_snapshot_value_text(snapshot, input.train_key);
        row.cells["filePath"] = map_snapshot_value_text(snapshot, input.load_file_path);
        row.cells["resolvedFilePath"] = map_snapshot_string(snapshot, input.resolved_file_path);
        row.cells["trackKey"] = map_snapshot_track_key_text(snapshot, input.track_key);
        row.cells["direction"] = map_snapshot_value_text(snapshot, input.direction);
        row.cells["sourceFilePath"] = map_snapshot_string(snapshot, input.source_file_path);
        row.cells["order"] = std::to_string(input.order);
        apply_map_row_metadata(row, snapshot, input.metadata);
        model.other_trains.push_back(std::move(row));
    }
    model.other_train_enables.reserve(
        static_cast<size_t>(snapshot.other_train_enable_count));
    for (std::uint64_t i = 0; i < snapshot.other_train_enable_count; ++i) {
        const KvOtherTrainEnableRow& input = snapshot.other_train_enables[i];
        TableRow row;
        put_map_common_event_cells(row, snapshot, input);
        row.cells["trainKey"] = map_snapshot_value_text(snapshot, input.train_key);
        row.cells["time"] = map_snapshot_value_text(snapshot, input.time);
        apply_map_row_metadata(row, snapshot, input.metadata);
        model.other_train_enables.push_back(std::move(row));
    }
    model.other_train_stops.reserve(static_cast<size_t>(snapshot.other_train_stop_count));
    for (std::uint64_t i = 0; i < snapshot.other_train_stop_count; ++i) {
        const KvOtherTrainStopRow& input = snapshot.other_train_stops[i];
        TableRow row;
        put_map_common_event_cells(row, snapshot, input);
        row.cells["trainKey"] = map_snapshot_value_text(snapshot, input.train_key);
        row.cells["decelerate"] = map_snapshot_value_text(snapshot, input.decelerate);
        row.cells["stopTime"] = map_snapshot_value_text(snapshot, input.stop_time);
        row.cells["accelerate"] = map_snapshot_value_text(snapshot, input.accelerate);
        row.cells["speed"] = map_snapshot_value_text(snapshot, input.speed);
        apply_map_row_metadata(row, snapshot, input.metadata);
        model.other_train_stops.push_back(std::move(row));
    }
    auto add_referenced_keys = [&](const KvReferencedKeyRow* inputs, std::uint64_t count,
                                   std::vector<TableRow>& output) {
        output.reserve(static_cast<size_t>(count));
        for (std::uint64_t i = 0; i < count; ++i) {
            TableRow row;
            row.cells["key"] = map_snapshot_string(snapshot, inputs[i].key);
            row.cells["filePath"] = map_snapshot_string(snapshot, inputs[i].file_path);
            apply_map_row_metadata(row, snapshot, inputs[i].metadata);
            output.push_back(std::move(row));
        }
    };
    add_referenced_keys(snapshot.other_train_structure_keys,
                        snapshot.other_train_structure_key_count,
                        model.other_train_structure_keys);
    add_referenced_keys(snapshot.other_train_sound_3d_keys,
                        snapshot.other_train_sound_3d_key_count,
                        model.other_train_sound_3d_keys);
    auto add_sections = [&](const KvSectionRow* inputs, std::uint64_t count,
                            std::vector<TableRow>& output) {
        output.reserve(static_cast<size_t>(count));
        for (std::uint64_t i = 0; i < count; ++i) {
            const KvSectionRow& input = inputs[i];
            TableRow row;
            row.cells["distance"] = format_double(input.distance, 6);
            row.cells["method"] = map_snapshot_string(snapshot, input.method);
            row.cells["filePath"] = map_snapshot_string(snapshot, input.file_path);
            row.cells["order"] = std::to_string(input.order);
            if (map_snapshot_span_valid(input.values, snapshot.value_count) &&
                (input.values.count == 0 || snapshot.values)) {
                for (std::uint64_t value_index = 0;
                     value_index < input.values.count; ++value_index) {
                    row.cells["value" + std::to_string(value_index)] =
                        map_snapshot_preview_value_text(
                            snapshot,
                            snapshot.values[input.values.offset + value_index]);
                }
            }
            row.cells["valueCount"] = std::to_string(input.values.count);
            apply_map_row_metadata(row, snapshot, input.metadata);
            output.push_back(std::move(row));
        }
    };
    add_sections(snapshot.section_begins, snapshot.section_begin_count,
                 model.section_begins);
    add_sections(snapshot.section_speed_limits,
                 snapshot.section_speed_limit_count,
                 model.section_speed_limits);
    model.sound_list.reserve(static_cast<size_t>(snapshot.sound_list_count));
    model.sound_3d_list.reserve(static_cast<size_t>(snapshot.sound_list_count));
    for (std::uint64_t i = 0; i < snapshot.sound_list_count; ++i) {
        const KvSoundListRow& input = snapshot.sound_list[i];
        TableRow row;
        row.cells["soundKey"] = map_snapshot_string(snapshot, input.sound_key);
        row.cells["filePath"] = map_snapshot_string(snapshot, input.file_path);
        row.cells["resolvedFilePath"] = resolve_list_asset_path(
            snapshot, input.metadata, row.cells["filePath"]);
        row.cells["bufferCount"] = std::to_string(input.buffer_count);
        row.cells["is3D"] = input.is_3d ? "true" : "false";
        apply_map_row_metadata(row, snapshot, input.metadata);
        (input.is_3d ? model.sound_3d_list : model.sound_list).push_back(
            std::move(row));
    }
    model.repeaters.reserve(static_cast<size_t>(snapshot.repeater_count));
    for (std::uint64_t i = 0; i < snapshot.repeater_count; ++i) {
        const KvRepeaterRow& input = snapshot.repeaters[i];
        TableRow row;
        put_map_common_event_cells(row, snapshot, input);
        row.cells["method"] = map_snapshot_string(snapshot, input.method);
        row.cells["repeaterKey"] = map_snapshot_value_text(snapshot, input.repeater_key);
        row.cells["trackKey"] = map_snapshot_track_key_text(snapshot, input.track_key);
        row.cells["x"] = format_double(input.x, 6); row.cells["y"] = format_double(input.y, 6);
        row.cells["z"] = format_double(input.z, 6); row.cells["rx"] = format_double(input.rx, 6);
        row.cells["ry"] = format_double(input.ry, 6); row.cells["rz"] = format_double(input.rz, 6);
        row.cells["tilt"] = format_double(input.tilt, 6);
        row.cells["span"] = format_double(input.span, 6);
        row.cells["interval"] = format_double(input.interval, 6);
        row.cells["structureKeys"] = map_snapshot_value_span_text(snapshot, input.structure_keys);
        apply_map_row_metadata(row, snapshot, input.metadata);
        model.repeaters.push_back(std::move(row));
    }
    model.signal_aspects =
        hydrate_signal_aspect_rows(snapshot);
    model.signals.reserve(static_cast<size_t>(snapshot.signal_put_count));
    for (std::uint64_t i = 0; i < snapshot.signal_put_count; ++i) {
        const KvSignalPutRow& input = snapshot.signal_puts[i];
        TableRow row;
        put_map_common_event_cells(row, snapshot, input);
        row.cells["signalAspectKey"] = map_snapshot_value_text(snapshot, input.signal_aspect_key);
        row.cells["section"] = map_snapshot_value_text(snapshot, input.section);
        row.cells["trackKey"] = map_snapshot_track_key_text(snapshot, input.track_key);
        row.cells["x"] = format_double(input.x, 6); row.cells["y"] = format_double(input.y, 6);
        row.cells["z"] = format_double(input.z, 6); row.cells["rx"] = format_double(input.rx, 6);
        row.cells["ry"] = format_double(input.ry, 6); row.cells["rz"] = format_double(input.rz, 6);
        row.cells["tilt"] = format_double(input.tilt, 6);
        row.cells["span"] = format_double(input.span, 6);
        apply_map_row_metadata(row, snapshot, input.metadata);
        model.signals.push_back(std::move(row));
    }
    model.beacons.reserve(static_cast<size_t>(snapshot.beacon_count));
    for (std::uint64_t i = 0; i < snapshot.beacon_count; ++i) {
        const KvBeaconRow& input = snapshot.beacons[i];
        TableRow row;
        put_map_common_event_cells(row, snapshot, input);
        row.cells["type"] = map_snapshot_value_text(snapshot, input.type);
        row.cells["section"] = map_snapshot_value_text(snapshot, input.section);
        row.cells["sendData"] = map_snapshot_value_text(snapshot, input.send_data);
        apply_map_row_metadata(row, snapshot, input.metadata);
        model.beacons.push_back(std::move(row));
    }
    model.pretrains.reserve(static_cast<size_t>(snapshot.pretrain_count));
    for (std::uint64_t i = 0; i < snapshot.pretrain_count; ++i) {
        const KvPreTrainRow& input = snapshot.pretrains[i];
        TableRow row;
        put_map_common_event_cells(row, snapshot, input);
        row.cells["passTime"] = map_snapshot_value_text(snapshot, input.pass_time);
        apply_map_row_metadata(row, snapshot, input.metadata);
        model.pretrains.push_back(std::move(row));
    }
    model.irregularities.reserve(static_cast<size_t>(snapshot.irregularity_count));
    for (std::uint64_t i = 0; i < snapshot.irregularity_count; ++i) {
        const KvIrregularityRow& input = snapshot.irregularities[i];
        TableRow row;
        put_map_common_event_cells(row, snapshot, input);
        row.cells["x"] = format_double(input.x, 6); row.cells["y"] = format_double(input.y, 6);
        row.cells["r"] = format_double(input.r, 6); row.cells["lx"] = format_double(input.lx, 6);
        row.cells["ly"] = format_double(input.ly, 6); row.cells["lr"] = format_double(input.lr, 6);
        apply_map_row_metadata(row, snapshot, input.metadata);
        model.irregularities.push_back(std::move(row));
    }
    model.map_sounds.reserve(static_cast<size_t>(snapshot.map_sound_count));
    for (std::uint64_t i = 0; i < snapshot.map_sound_count; ++i) {
        const KvMapSoundRow& input = snapshot.map_sounds[i];
        TableRow row;
        put_map_common_event_cells(row, snapshot, input);
        row.cells["soundKey"] = map_snapshot_value_text(snapshot, input.sound_key);
        apply_map_row_metadata(row, snapshot, input.metadata);
        model.map_sounds.push_back(std::move(row));
    }
    model.map_sound_3d.reserve(static_cast<size_t>(snapshot.map_sound_3d_count));
    for (std::uint64_t i = 0; i < snapshot.map_sound_3d_count; ++i) {
        const KvMapSound3DRow& input = snapshot.map_sounds_3d[i];
        TableRow row;
        put_map_common_event_cells(row, snapshot, input);
        row.cells["soundKey"] = map_snapshot_value_text(snapshot, input.sound_key);
        row.cells["x"] = format_double(input.x, 6); row.cells["y"] = format_double(input.y, 6);
        apply_map_row_metadata(row, snapshot, input.metadata);
        model.map_sound_3d.push_back(std::move(row));
    }
    auto add_noises = [&](const KvNoiseRow* inputs, std::uint64_t count,
                          std::vector<TableRow>& output) {
        output.reserve(static_cast<size_t>(count));
        for (std::uint64_t i = 0; i < count; ++i) {
            TableRow row;
            put_map_common_event_cells(row, snapshot, inputs[i]);
            row.cells["index"] = map_snapshot_value_text(snapshot, inputs[i].index);
            apply_map_row_metadata(row, snapshot, inputs[i].metadata);
            output.push_back(std::move(row));
        }
    };
    add_noises(snapshot.rolling_noises, snapshot.rolling_noise_count, model.rolling_noises);
    add_noises(snapshot.flange_noises, snapshot.flange_noise_count, model.flange_noises);
    add_noises(snapshot.joint_noises, snapshot.joint_noise_count, model.joint_noises);
    model.backgrounds.reserve(static_cast<size_t>(snapshot.background_count));
    for (std::uint64_t i = 0; i < snapshot.background_count; ++i) {
        const KvBackgroundRow& input = snapshot.backgrounds[i];
        TableRow row;
        put_map_common_event_cells(row, snapshot, input);
        row.cells["structureKey"] = map_snapshot_value_text(snapshot, input.structure_key);
        apply_map_row_metadata(row, snapshot, input.metadata);
        model.backgrounds.push_back(std::move(row));
    }
    model.adhesions.reserve(static_cast<size_t>(snapshot.adhesion_count));
    for (std::uint64_t i = 0; i < snapshot.adhesion_count; ++i) {
        const KvAdhesionRow& input = snapshot.adhesions[i];
        TableRow row;
        put_map_common_event_cells(row, snapshot, input);
        row.cells["a"] = map_snapshot_value_text(snapshot, input.a);
        row.cells["b"] = map_snapshot_value_text(snapshot, input.b);
        row.cells["c"] = map_snapshot_value_text(snapshot, input.c);
        apply_map_row_metadata(row, snapshot, input.metadata);
        model.adhesions.push_back(std::move(row));
    }
    model.cab_illuminance.reserve(static_cast<size_t>(snapshot.cab_illuminance_count));
    for (std::uint64_t i = 0; i < snapshot.cab_illuminance_count; ++i) {
        const KvCabIlluminanceRow& input = snapshot.cab_illuminance[i];
        TableRow row;
        put_map_common_event_cells(row, snapshot, input);
        row.cells["_sourceValue"] = input.value.kind == KV_VALUE_CONTINUE
            ? std::string{}
            : map_snapshot_value_text(snapshot, input.value);
        row.cells["value"] = row.cells["_sourceValue"];
        apply_map_row_metadata(row, snapshot, input.metadata);
        model.cab_illuminance.push_back(std::move(row));
    }
    normalize_cab_illuminance_preview_rows(model);
    model.fogs.reserve(static_cast<size_t>(snapshot.fog_count));
    for (std::uint64_t i = 0; i < snapshot.fog_count; ++i) {
        const KvFogRow& input = snapshot.fogs[i];
        TableRow row;
        put_map_common_event_cells(row, snapshot, input);
        row.cells["density"] = map_snapshot_value_text(snapshot, input.density);
        row.cells["red"] = map_snapshot_value_text(snapshot, input.red);
        row.cells["green"] = map_snapshot_value_text(snapshot, input.green);
        row.cells["blue"] = map_snapshot_value_text(snapshot, input.blue);
        apply_map_row_metadata(row, snapshot, input.metadata);
        model.fogs.push_back(std::move(row));
    }
    model.legacy_fogs.reserve(static_cast<size_t>(snapshot.legacy_fog_count));
    for (std::uint64_t i = 0; i < snapshot.legacy_fog_count; ++i) {
        const KvLegacyFogRow& input = snapshot.legacy_fogs[i];
        TableRow row;
        put_map_common_event_cells(row, snapshot, input);
        row.cells["start"] = format_double(input.start, 6);
        row.cells["end"] = format_double(input.end, 6);
        row.cells["red"] = format_double(input.red, 6);
        row.cells["green"] = format_double(input.green, 6);
        row.cells["blue"] = format_double(input.blue, 6);
        apply_map_row_metadata(row, snapshot, input.metadata);
        model.legacy_fogs.push_back(std::move(row));
    }
    const auto hydrate_light_color = [&](const KvLightColorRow* rows,
                                         std::uint64_t count,
                                         std::vector<TableRow>& output) {
        if (!rows || count != 1) return;
        const KvLightColorRow& input = rows[0];
        TableRow row;
        row.cells["red"] = format_double(input.red, 6);
        row.cells["green"] = format_double(input.green, 6);
        row.cells["blue"] = format_double(input.blue, 6);
        apply_map_row_metadata(row, snapshot, input.metadata);
        output.push_back(std::move(row));
    };
    hydrate_light_color(snapshot.light_ambient, snapshot.light_ambient_count,
                        model.light_ambient);
    hydrate_light_color(snapshot.light_diffuse, snapshot.light_diffuse_count,
                        model.light_diffuse);
    if (snapshot.light_direction && snapshot.light_direction_count == 1) {
        const KvLightDirectionRow& input = snapshot.light_direction[0];
        TableRow row;
        row.cells["pitch"] = format_double(input.pitch, 6);
        row.cells["yaw"] = format_double(input.yaw, 6);
        apply_map_row_metadata(row, snapshot, input.metadata);
        model.light_direction.push_back(std::move(row));
    }
    model.draw_distances.reserve(static_cast<size_t>(snapshot.draw_distance_count));
    for (std::uint64_t i = 0; i < snapshot.draw_distance_count; ++i) {
        const KvDrawDistanceRow& input = snapshot.draw_distances[i];
        TableRow row;
        put_map_common_event_cells(row, snapshot, input);
        row.cells["value"] = format_double(input.value, 6);
        apply_map_row_metadata(row, snapshot, input.metadata);
        model.draw_distances.push_back(std::move(row));
    }

    model.variable_assignments.reserve(
        static_cast<size_t>(snapshot.variable_assignment_count));
    for (std::uint64_t i = 0; i < snapshot.variable_assignment_count; ++i) {
        const KvVariableAssignmentRow& input = snapshot.variable_assignments[i];
        TableRow row;
        row.cells["normalizedName"] =
            map_snapshot_string(snapshot, input.normalized_name);
        row.cells["sourceName"] = map_snapshot_string(snapshot, input.source_name);
        row.cells["value"] = map_snapshot_preview_value_text(snapshot, input.value);
        row.cells["expression"] = map_snapshot_string(snapshot, input.expression);
        row.cells["filePath"] = map_snapshot_string(snapshot, input.file_path);
        row.cells["order"] = std::to_string(input.order);
        model.variable_assignments.push_back(std::move(row));
    }
    const auto resource_list_statement_kind = [](std::uint32_t kind) {
        switch (static_cast<ResourceListKind>(kind)) {
        case ResourceListKind::Station: return "station.load";
        case ResourceListKind::Structure: return "structure.load";
        case ResourceListKind::Signal: return "signal.load";
        case ResourceListKind::Sound: return "sound.load";
        case ResourceListKind::Sound3D: return "sound3d.load";
        case ResourceListKind::Count: break;
        }
        return "";
    };
    for (std::uint64_t i = 0; i < snapshot.resource_list_load_count; ++i) {
        const KvResourceListLoadRow& input = snapshot.resource_list_loads[i];
        if (input.kind >= static_cast<std::uint32_t>(ResourceListKind::Count)) continue;
        ResourceListSource& output = model.resource_list_sources[input.kind];
        output.present = true;
        output.evaluated_path = map_snapshot_string(snapshot, input.evaluated_path);
        output.raw_argument = map_snapshot_string(snapshot, input.raw_argument);
        output.resolved_path = map_snapshot_string(snapshot, input.resolved_path);
        output.source_file_path = map_snapshot_string(snapshot, input.file_path);
        output.order = input.order;
        const char* statement_kind = resource_list_statement_kind(input.kind);
        for (const EditStatementInfo& statement : model.edit_statements) {
            if (ascii_lower(statement.statement_kind) == statement_kind &&
                statement.source.file_path == output.source_file_path &&
                statement.first_evaluated_value == output.evaluated_path) {
                output.edit_id = statement.edit_id;
                break;
            }
        }
    }

    static_assert(k_station_list_field_names.size() ==
                  std::extent_v<decltype(KvStationListRow::fields)>);
    model.station_definition_rows.reserve(static_cast<size_t>(snapshot.station_list_count));
    std::map<std::string, size_t> station_definition_row_indices_by_key;
    for (std::uint64_t i = 0; i < snapshot.station_list_count; ++i) {
        const KvStationListRow& input = snapshot.station_list[i];
        TableRow row;
        for (size_t field = 0; field < k_station_list_field_names.size(); ++field) {
            row.cells[k_station_list_field_names[field]] =
                map_snapshot_string(snapshot, input.fields[field]);
        }
        apply_map_row_metadata(row, snapshot, input.metadata);
        const std::string object_key = map_snapshot_string(snapshot, input.object_key);
        if (table_cell(row, "stationKey").empty()) row.cells["stationKey"] = object_key;
        const size_t row_index = model.station_definition_rows.size();
        model.station_definition_rows.push_back(std::move(row));
        const std::string normalized_key = ascii_lower(object_key);
        if (!normalized_key.empty()) {
            station_definition_row_indices_by_key[normalized_key] = row_index;
        }
    }
    model.station_list_rows.reserve(static_cast<size_t>(snapshot.station_put_count));
    for (std::uint64_t i = 0; i < snapshot.station_put_count; ++i) {
        const KvStationPutRow& input = snapshot.station_puts[i];
        const std::string key = map_snapshot_value_text(snapshot, input.station_key);
        TableRow row;
        row.cells["_distance"] = format_double(input.distance, 6);
        row.cells["_order"] = std::to_string(input.order);
        row.cells["dist"] = format_double(input.distance - model.distance_origin, 0);
        row.cells["posKey"] = key;
        row.cells["door"] = map_snapshot_value_text(snapshot, input.door);
        row.cells["margin1"] = map_snapshot_value_text(snapshot, input.margin1);
        row.cells["margin2"] = map_snapshot_value_text(snapshot, input.margin2);
        apply_map_row_metadata(row, snapshot, input.metadata);
        auto existing = station_definition_row_indices_by_key.find(ascii_lower(key));
        if (existing != station_definition_row_indices_by_key.end()) {
            const TableRow& definition = model.station_definition_rows[existing->second];
            for (const auto& cell : definition.cells) row.cells[cell.first] = cell.second;
        }
        model.station_list_rows.push_back(std::move(row));
    }
    std::stable_sort(model.station_list_rows.begin(), model.station_list_rows.end(),
                     [](const TableRow& a, const TableRow& b) {
        const double da = table_cell_number(a, "_distance");
        const double db = table_cell_number(b, "_distance");
        if (da != db) return da < db;
        return table_cell_number(a, "_order") < table_cell_number(b, "_order");
    });
    for (size_t i = 0; i < model.station_list_rows.size(); ++i) {
        model.station_list_rows[i].cells["rowNumber"] = std::to_string(i + 1);
    }
    bind_station_position_edit_ids(model);

    if (!model.stations.empty()) {
        double minimum = model.stations.front().distance;
        double maximum = minimum;
        for (const Station& station : model.stations) {
            minimum = std::min(minimum, station.distance);
            maximum = std::max(maximum, station.distance);
        }
        model.default_min = round_to_100(minimum) - 500.0;
        model.default_max = round_to_100(maximum) + 500.0;
    } else if (!model.controlpoints.empty()) {
        auto range = std::minmax_element(model.controlpoints.begin(), model.controlpoints.end());
        model.default_min = round_to_100(*range.first) - 500.0;
        model.default_max = round_to_100(*range.second) + 500.0;
    } else if (!model.own.empty()) {
        model.default_min = model.own.at(0, 0);
        model.default_max = model.own.at(model.own.rows - 1, 0);
    }
    model.buffer_copy_seconds = buffer_copy_seconds;
    model.snapshot_hydrate_seconds = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - hydrate_started_at).count();
    annotate_scene_track_key_warnings(model);
    return model;
}
