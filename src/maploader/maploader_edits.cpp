/*
 * Copyright (c) 2026 Sapporo_ningyo
 *
 * Portions of the map parsing and track-geometry design are derived from or
 * reimplemented with reference to kobushi-trackviewer, Copyright (c) 2021-2024
 * konawasabi, licensed under Apache License 2.0.
 *
 * Licensed under Apache License 2.0; see LICENSE and NOTICE.
 */

#include "maploader_internal.h"
#include "own_track_transition_linkage.h"
#include "repeater_linkage.h"

namespace kme::maploader::detail {

using kme::maploader::encode_text_for_writeback;
using kme::maploader::log_warn;
using kme::maploader::path_from_utf8;
using kme::maploader::path_to_utf8;
using kme::maploader::read_binary_file;

using SemanticSnapshot = SemanticMapSnapshot;
using SemanticElement = SemanticElementSnapshot;

SemanticSnapshot semantic_snapshot_for_context(MapContext& ctx) {
    return build_semantic_map_snapshot(ctx);
}

struct EditableTarget {
    using SimpleStatementBuilder = std::string (*)(
        const MapEditChange&, const ParsedStatement&, const void*);

    size_t statement_index = k_no_source_ref;
    std::string row_kind;
    size_t row_index = 0;
    int element_index = 0;
    const StructurePut* structure_put = nullptr;
    const StationPut* station_put = nullptr;
    const IrregularityChange* irregularity = nullptr;
    const SignalPut* signal_put = nullptr;
    const SignalAspect* signal_aspect = nullptr;
    const RepeaterEvent* repeater = nullptr;
    const CurveEditRow* curve = nullptr;
    const GradientEditRow* gradient = nullptr;
    const OtherTrackChange* other_track_change = nullptr;
    const StationListEntry* station_list = nullptr;
    const StructureModel* structure_model = nullptr;
    const SoundListEntry* sound_list = nullptr;
    const void* simple_row = nullptr;
    SimpleStatementBuilder simple_statement_builder = nullptr;
    int elements_for_statement = 0;
};

EditableTarget find_editable_target(MapContext& ctx, const std::string& edit_id);

struct ResourceListEditSpec {
    ResourceListLoadKind kind;
    const char* header;
    double minimum_version;
    const char* error_key;
    const char* content_row_kind;
};

const ResourceListEditSpec* resource_list_edit_spec_for_kind(
    ResourceListLoadKind kind) {
    static constexpr ResourceListEditSpec k_station{
        ResourceListLoadKind::Station, "BveTs Station List ", 0.04,
        "station", "station.list"};
    static constexpr ResourceListEditSpec k_structure{
        ResourceListLoadKind::Structure, "BveTs Structure List ", 1.0,
        "structure", "structure.model"};
    static constexpr ResourceListEditSpec k_signal{
        ResourceListLoadKind::Signal, "BveTs Signal Aspects List ", 2.0,
        "signal", "signal.aspect"};
    static constexpr ResourceListEditSpec k_sound{
        ResourceListLoadKind::Sound, "BveTs Sound List ", 2.0,
        "sound", "sound.list"};
    static constexpr ResourceListEditSpec k_sound_3d{
        ResourceListLoadKind::Sound3D, "BveTs Sound List ", 2.0,
        "sound", "sound3D.list"};
    switch (kind) {
    case ResourceListLoadKind::Station: return &k_station;
    case ResourceListLoadKind::Structure: return &k_structure;
    case ResourceListLoadKind::Signal: return &k_signal;
    case ResourceListLoadKind::Sound: return &k_sound;
    case ResourceListLoadKind::Sound3D: return &k_sound_3d;
    }
    return nullptr;
}

const ResourceListEditSpec* resource_list_edit_spec_for_statement(
    std::string_view statement_kind) {
    const std::string kind = ascii_lower(std::string(statement_kind));
    if (kind == "station.load") return resource_list_edit_spec_for_kind(ResourceListLoadKind::Station);
    if (kind == "structure.load") return resource_list_edit_spec_for_kind(ResourceListLoadKind::Structure);
    if (kind == "signal.load") return resource_list_edit_spec_for_kind(ResourceListLoadKind::Signal);
    if (kind == "sound.load") return resource_list_edit_spec_for_kind(ResourceListLoadKind::Sound);
    if (kind == "sound3d.load") return resource_list_edit_spec_for_kind(ResourceListLoadKind::Sound3D);
    return nullptr;
}

const ResourceListEditSpec* resource_list_edit_spec_for_content_row_kind(
    std::string_view row_kind) {
    if (row_kind == "station.list") {
        return resource_list_edit_spec_for_kind(ResourceListLoadKind::Station);
    }
    if (row_kind == "structure.model") {
        return resource_list_edit_spec_for_kind(ResourceListLoadKind::Structure);
    }
    if (row_kind == "signal.aspect") {
        return resource_list_edit_spec_for_kind(ResourceListLoadKind::Signal);
    }
    if (row_kind == "sound.list") {
        return resource_list_edit_spec_for_kind(ResourceListLoadKind::Sound);
    }
    if (row_kind == "sound3D.list") {
        return resource_list_edit_spec_for_kind(ResourceListLoadKind::Sound3D);
    }
    return nullptr;
}

std::string_view resource_list_content_statement_kind(
    ResourceListLoadKind kind) {
    switch (kind) {
    case ResourceListLoadKind::Station: return "StationList.Row";
    case ResourceListLoadKind::Structure: return "StructureList.Row";
    case ResourceListLoadKind::Signal: return "SignalAspectList.Row";
    case ResourceListLoadKind::Sound: return "SoundList.Row";
    case ResourceListLoadKind::Sound3D: return "Sound3DList.Row";
    }
    return {};
}

const ResourceListLoad* resource_list_load_for_kind(
    const MapContext& ctx, ResourceListLoadKind kind) {
    const auto found = std::find_if(
        ctx.resource_list_loads.begin(), ctx.resource_list_loads.end(),
        [kind](const ResourceListLoad& row) { return row.kind == kind; });
    return found == ctx.resource_list_loads.end() ? nullptr : &*found;
}

struct TextReplacementIdentity {
    std::string edit_id;
    std::string row_kind;
    size_t relative_begin = 0;
    size_t relative_end = 0;
    int element_index = 0;
    int baseline_global_order = 0;
};

struct TextReplacement {
    size_t begin = 0;
    size_t end = 0;
    std::string text;
    std::vector<TextReplacementIdentity> identities;
};

struct SourcePatch {
    const SourceFileRecord* record = nullptr;
    std::string text;
    std::vector<size_t> line_starts;
    std::string current_hash;
    std::string base_hash;
    bool utf8_bom = false;
    std::vector<TextReplacement> replacements;
};

std::string copy_utf8_view(KvUtf8View view, const char* field_name) {
    if (view.length == 0) return {};
    if (!view.data) throw std::runtime_error(std::string(field_name) + " data is null");
    if (view.length > static_cast<std::uint64_t>(std::numeric_limits<size_t>::max())) {
        throw std::runtime_error(std::string(field_name) + " is too large");
    }
    return std::string(view.data, static_cast<size_t>(view.length));
}

std::vector<MapEditChange> copy_edit_batch(const KvEditBatch& batch) {
    if (batch.change_count != 0 && !batch.changes) {
        throw std::runtime_error("edit batch changes are null");
    }
    if (batch.field_count != 0 && !batch.fields) {
        throw std::runtime_error("edit batch fields are null");
    }
    if (batch.change_count > static_cast<std::uint64_t>(std::numeric_limits<size_t>::max()) ||
        batch.field_count > static_cast<std::uint64_t>(std::numeric_limits<size_t>::max())) {
        throw std::runtime_error("edit batch is too large");
    }
    std::vector<MapEditChange> changes;
    changes.reserve(static_cast<size_t>(batch.change_count));
    for (std::uint64_t i = 0; i < batch.change_count; ++i) {
        const KvEditChange& input = batch.changes[i];
        if (input.fields.offset > batch.field_count ||
            input.fields.count > batch.field_count - input.fields.offset) {
            throw std::runtime_error("edit change field span is out of bounds");
        }
        MapEditChange change;
        change.change_id = copy_utf8_view(input.change_id, "changeId");
        change.edit_id = copy_utf8_view(input.edit_id, "editId");
        switch (input.operation) {
            case KV_EDIT_UPDATE: change.operation = "update"; break;
            case KV_EDIT_INSERT: change.operation = "insert"; break;
            case KV_EDIT_DELETE: change.operation = "delete"; break;
            default: throw std::runtime_error("unsupported typed edit operation");
        }
        change.replacement_statement = copy_utf8_view(
            input.replacement_statement, "replacementStatement");
        change.target_file_path = copy_utf8_view(input.target_file_path, "targetFilePath");
        change.insert_before_edit_id = copy_utf8_view(
            input.insert_before_edit_id, "insertBeforeEditId");
        change.expected_source_hash = copy_utf8_view(
            input.expected_source_hash, "expectedSourceHash");
        change.distance_resolution_key = copy_utf8_view(
            input.distance_resolution_key, "distanceResolutionKey");
        change.distance_boundary_token = copy_utf8_view(
            input.distance_boundary_token, "distanceBoundaryToken");
        change.distance_expression = copy_utf8_view(
            input.distance_expression, "distanceExpression");
        change.confirm_environment_mismatch =
            (input.flags & KV_EDIT_CHANGE_CONFIRM_ENVIRONMENT_MISMATCH) != 0;
        change.confirm_repeater_change_point =
            (input.flags & KV_EDIT_CHANGE_CONFIRM_REPEATER_CHANGE_POINT) != 0;
        for (std::uint64_t j = 0; j < input.fields.count; ++j) {
            const KvEditField& field = batch.fields[input.fields.offset + j];
            std::string name = copy_utf8_view(field.name, "field name");
            if (name.empty()) throw std::runtime_error("edit field name is empty");
            if (change.operation == "insert" && name == "rowKind") {
                // Insert changes have no existing edit target to derive their
                // row kind from. The GUI transports it as a reserved field so
                // the typed edit path can build the statement and expected
                // semantic without it leaking into field validation.
                if (!change.row_kind.empty()) {
                    throw std::runtime_error("insert edit contains duplicate rowKind fields");
                }
                change.row_kind = trim_field_copy(copy_utf8_view(field.value, "rowKind"));
                if (change.row_kind.empty()) {
                    throw std::runtime_error("insert edit rowKind is empty");
                }
                continue;
            }
            if (change.operation == "insert" && name == "repeaterPairId") {
                if (!change.repeater_pair_id.empty()) {
                    throw std::runtime_error(
                        "insert edit contains duplicate repeaterPairId fields");
                }
                change.repeater_pair_id = trim_field_copy(
                    copy_utf8_view(field.value, "repeaterPairId"));
                if (change.repeater_pair_id.empty()) {
                    throw std::runtime_error("insert edit repeaterPairId is empty");
                }
                continue;
            }
            change.field_changes[std::move(name)] = copy_utf8_view(field.value, "field value");
        }
        changes.push_back(std::move(change));
    }
    return changes;
}

std::string newline_text(const std::string& newline) {
    if (newline == "crlf") return "\r\n";
    if (newline == "cr") return "\r";
    return "\n";
}

SourcePatch load_source_patch(const MapContext& ctx, const SourceFileRecord& record) {
    SourcePatch patch;
    patch.record = &record;
    auto override_it = ctx.source_overrides.find(record.source_key);
    if (override_it != ctx.source_overrides.end()) {
        const SourceTextOverride& source = override_it->second;
        patch.text = source.text;
        patch.current_hash = source.current_hash;
        patch.base_hash = source.base_hash.empty() ? source.current_hash : source.base_hash;
        patch.utf8_bom = source.utf8_bom;
        patch.line_starts = build_line_starts(patch.text);
        return patch;
    }

    std::filesystem::path path = path_from_utf8(record.file_path);
    std::string bytes = read_binary_file(path);
    patch.current_hash = hex64(stable_hash64(bytes));
    patch.base_hash = patch.current_hash;
    patch.utf8_bom = has_utf8_bom(bytes);
    patch.text = decode_text_bytes(bytes, record.encoding);
    patch.line_starts = build_line_starts(patch.text);
    return patch;
}

size_t utf8_next_offset(const std::string& text, size_t offset) {
    if (offset >= text.size()) return text.size();
    ++offset;
    while (offset < text.size() &&
           (static_cast<unsigned char>(text[offset]) & 0xc0) == 0x80) {
        ++offset;
    }
    return offset;
}

size_t offset_from_line_column(const std::string& text,
                               const std::vector<size_t>& line_starts,
                               int line,
                               int column) {
    if (line <= 0 || column <= 0 || static_cast<size_t>(line) > line_starts.size()) {
        return std::string::npos;
    }
    const size_t line_start = line_starts[static_cast<size_t>(line - 1)];
    const size_t line_end = text_line_span(text, line_start).content_end;
    size_t offset = line_start;
    int current_column = 1;
    while (offset < line_end && current_column < column) {
        offset = utf8_next_offset(text, offset);
        ++current_column;
    }
    return offset;
}

std::pair<size_t, size_t> source_range_in_text(
    const std::string& text,
    const std::vector<size_t>& line_starts,
    const SourceSpan& source) {
    size_t begin = offset_from_line_column(text, line_starts, source.line, source.column);
    size_t end = offset_from_line_column(
        text, line_starts, source.line_end, source.column_end);
    if (begin == std::string::npos || end == std::string::npos || end < begin) {
        throw std::runtime_error("invalid source span for edit");
    }
    return {begin, end};
}

std::pair<size_t, size_t> source_range_in_text(const SourcePatch& patch,
                                               const SourceSpan& source) {
    return source_range_in_text(patch.text, patch.line_starts, source);
}

std::pair<size_t, size_t> safe_statement_removal_range(
    const SourcePatch& patch,
    const std::pair<size_t, size_t>& statement_range) {
    const size_t line_start = text_line_start_at(patch.text, statement_range.first);
    const TextLineSpan source_line = text_line_span(patch.text, line_start);
    size_t line_end = source_line.content_end;
    auto whitespace_only = [&](size_t begin, size_t end) {
        for (size_t i = begin; i < end; ++i) {
            char ch = patch.text[i];
            if (ch != ' ' && ch != '\t') return false;
        }
        return true;
    };
    if (!whitespace_only(line_start, statement_range.first) ||
        !whitespace_only(statement_range.second, line_end)) {
        return statement_range;
    }
    if (source_line.has_terminator()) line_end = source_line.next_begin;
    return {line_start, line_end};
}

std::string preview_fragment(const std::string& text, size_t begin, size_t end) {
    const size_t context = 80;
    size_t preview_begin = begin > context ? begin - context : 0;
    size_t preview_end = std::min(text.size(), end + context);
    std::string out = text.substr(preview_begin, preview_end - preview_begin);
    if (preview_begin > 0) out = "..." + out;
    if (preview_end < text.size()) out += "...";
    return out;
}

bool parse_edit_number(const std::string& text, double& value) {
    return parse_finite_number(text, value);
}

std::string fallback_edit_number(double value) {
    if (!std::isfinite(value)) {
        throw std::runtime_error("invalid numeric edit fallback");
    }
    std::ostringstream out;
    out << std::setprecision(15) << value;
    std::string text = out.str();
    if (text == "-0") return "0";
    return text;
}

std::string normalized_number_arg(const std::string& text) {
    double value = 0.0;
    std::string trimmed = trim_field_copy(text);
    if (!parse_edit_number(trimmed, value)) {
        throw std::runtime_error("invalid numeric edit value: " + text);
    }
    return trimmed == "-0" ? "0" : trimmed;
}

bool edit_expr_ident_start(unsigned char c) {
    return std::isalpha(c) || c == '_' || c >= 0x80;
}

bool edit_expr_ident_part(unsigned char c) {
    return std::isalnum(c) || c == '_' || c >= 0x80;
}

size_t previous_nonspace_pos(const std::string& text, size_t pos) {
    while (pos > 0) {
        --pos;
        if (!std::isspace(static_cast<unsigned char>(text[pos]))) return pos;
    }
    return std::string::npos;
}

bool expression_references_predefined_distance(const std::string& expression) {
    bool single_quoted = false;
    bool double_quoted = false;
    for (size_t i = 0; i < expression.size();) {
        char ch = expression[i];
        if (ch == '\'' && !double_quoted) {
            single_quoted = !single_quoted;
            ++i;
            continue;
        }
        if (ch == '"' && !single_quoted) {
            double_quoted = !double_quoted;
            ++i;
            continue;
        }
        if (single_quoted || double_quoted) {
            ++i;
            continue;
        }
        if (!edit_expr_ident_start(static_cast<unsigned char>(ch))) {
            ++i;
            continue;
        }

        const size_t begin = i;
        ++i;
        while (i < expression.size() &&
               edit_expr_ident_part(static_cast<unsigned char>(expression[i]))) {
            ++i;
        }
        if (ascii_lower(expression.substr(begin, i - begin)) != "distance") continue;

        const size_t prev = previous_nonspace_pos(expression, begin);
        if (prev == std::string::npos || expression[prev] != '$') return true;
    }
    return false;
}

bool sign_is_exponent_part(const std::string& expression, size_t pos) {
    if (pos == 0 || pos + 1 >= expression.size()) return false;
    char previous = expression[pos - 1];
    if (previous != 'e' && previous != 'E') return false;
    return std::isdigit(static_cast<unsigned char>(expression[pos + 1]));
}

bool is_unary_additive_sign(const std::string& expression, size_t pos) {
    const size_t previous = previous_nonspace_pos(expression, pos);
    if (previous == std::string::npos) return true;
    char ch = expression[previous];
    return ch == '+' || ch == '-' || ch == '*' || ch == '/' ||
           ch == '%' || ch == '(' || ch == ',';
}

std::vector<size_t> top_level_additive_separator_positions(const std::string& expression) {
    std::vector<size_t> separators;
    bool single_quoted = false;
    bool double_quoted = false;
    int paren_depth = 0;
    for (size_t i = 0; i < expression.size(); ++i) {
        char ch = expression[i];
        if (ch == '\'' && !double_quoted) {
            single_quoted = !single_quoted;
            continue;
        }
        if (ch == '"' && !single_quoted) {
            double_quoted = !double_quoted;
            continue;
        }
        if (single_quoted || double_quoted) continue;
        if (ch == '(') {
            ++paren_depth;
            continue;
        }
        if (ch == ')' && paren_depth > 0) {
            --paren_depth;
            continue;
        }
        if (paren_depth != 0 || (ch != '+' && ch != '-')) continue;
        if (sign_is_exponent_part(expression, i) || is_unary_additive_sign(expression, i)) continue;
        separators.push_back(i);
    }
    return separators;
}

struct SafeDistanceAddend {
    size_t operator_pos = std::string::npos;
    size_t unary_sign_pos = std::string::npos;
    size_t number_begin = 0;
    size_t number_end = 0;
    int contribution_sign = 1;
    double literal_value = 0.0;
};

bool find_safe_numeric_distance_addend(const std::string& expression,
                                       SafeDistanceAddend& addend) {
    std::vector<size_t> separators = top_level_additive_separator_positions(expression);
    size_t term_begin = 0;
    for (size_t term_index = 0; term_index <= separators.size(); ++term_index) {
        const size_t term_end = term_index < separators.size()
            ? separators[term_index]
            : expression.size();
        const size_t operator_pos = term_index == 0
            ? std::string::npos
            : separators[term_index - 1];
        int sign = 1;
        size_t pos = term_begin;
        size_t unary_sign_pos = std::string::npos;
        if (operator_pos != std::string::npos) {
            sign = expression[operator_pos] == '-' ? -1 : 1;
            pos = operator_pos + 1;
        }
        while (pos < term_end && std::isspace(static_cast<unsigned char>(expression[pos]))) ++pos;
        while (pos < term_end && (expression[pos] == '+' || expression[pos] == '-')) {
            if (unary_sign_pos == std::string::npos) unary_sign_pos = pos;
            if (expression[pos] == '-') sign = -sign;
            ++pos;
            while (pos < term_end && std::isspace(static_cast<unsigned char>(expression[pos]))) ++pos;
        }
        if (pos >= term_end ||
            (!std::isdigit(static_cast<unsigned char>(expression[pos])) && expression[pos] != '.')) {
            term_begin = term_end + 1;
            continue;
        }

        const char* begin = expression.c_str() + pos;
        char* end = nullptr;
        errno = 0;
        double value = std::strtod(begin, &end);
        if (end == begin || errno == ERANGE || !std::isfinite(value)) {
            term_begin = term_end + 1;
            continue;
        }
        const size_t number_end = pos + static_cast<size_t>(end - begin);
        size_t trailing = number_end;
        while (trailing < term_end &&
               std::isspace(static_cast<unsigned char>(expression[trailing]))) {
            ++trailing;
        }
        if (trailing != term_end) {
            term_begin = term_end + 1;
            continue;
        }

        addend.operator_pos = operator_pos;
        addend.unary_sign_pos = unary_sign_pos;
        addend.number_begin = pos;
        addend.number_end = number_end;
        addend.contribution_sign = sign;
        addend.literal_value = value;
        return true;
    }
    return false;
}

std::string apply_delta_to_distance_addend(std::string expression,
                                           const SafeDistanceAddend& addend,
                                           double delta) {
    const double current_contribution =
        static_cast<double>(addend.contribution_sign) * addend.literal_value;
    const double next_contribution = current_contribution + delta;
    const int next_sign = next_contribution < 0.0 ? -1 : 1;
    const std::string next_number = fallback_edit_number(std::fabs(next_contribution));

    if (next_sign == addend.contribution_sign) {
        expression.replace(addend.number_begin,
                           addend.number_end - addend.number_begin,
                           next_number);
        return expression;
    }

    if (addend.operator_pos != std::string::npos) {
        expression.replace(addend.operator_pos,
                           addend.number_end - addend.operator_pos,
                           std::string(next_sign < 0 ? "-" : "+") + next_number);
        return expression;
    }

    const size_t replace_begin = addend.unary_sign_pos == std::string::npos
        ? addend.number_begin
        : addend.unary_sign_pos;
    expression.replace(replace_begin,
                       addend.number_end - replace_begin,
                       (next_sign < 0 ? "-" : "") + next_number);
    return expression;
}

std::string append_delta_to_distance_expression(const std::string& expression, double delta) {
    if (delta < 0.0) return expression + "-" + fallback_edit_number(-delta);
    return expression + "+" + fallback_edit_number(delta);
}

std::string adjust_distance_expression_by_delta(const std::string& expression, double delta) {
    std::string trimmed = trim_field_copy(expression);
    if (trimmed.empty()) {
        throw std::runtime_error("distance edit target has no source distance expression");
    }
    if (expression_references_predefined_distance(trimmed)) {
        throw std::runtime_error("distance edit is blocked because the source distance expression references predefined distance");
    }

    SafeDistanceAddend addend;
    if (find_safe_numeric_distance_addend(trimmed, addend)) {
        return apply_delta_to_distance_addend(std::move(trimmed), addend, delta);
    }
    return append_delta_to_distance_expression(trimmed, delta);
}

std::string suggested_distance_expression_for_target(
    const ParsedStatement& origin,
    double target_distance,
    const MapEditChange& change) {
    const std::string manual_expression = trim_field_copy(change.distance_expression);
    if (!manual_expression.empty()) return manual_expression;

    std::string source_expression = trim_field_copy(origin.distance_expression);
    if (source_expression.empty()) {
        source_expression = fallback_edit_number(origin.distance_value);
    }
    try {
        return adjust_distance_expression_by_delta(
            source_expression, target_distance - origin.distance_value);
    } catch (const std::exception&) {
        // Keep the existing manual distance-expression workflow responsible
        // for expressions that cannot be transformed safely.
        return {};
    }
}

std::vector<std::string> parse_bve_argument_fields(
    const std::string& line,
    std::vector<std::pair<size_t, size_t>>* value_spans = nullptr) {
    std::vector<std::string> fields;
    if (line.empty()) return fields;

    bool single_quoted = false;
    bool double_quoted = false;
    int paren_depth = 0;
    size_t field_begin = 0;
    for (size_t index = 0; index <= line.size(); ++index) {
        const bool at_end = index == line.size();
        const char ch = at_end ? '\0' : line[index];
        if (!at_end && ch == '\'' && !double_quoted) {
            single_quoted = !single_quoted;
        } else if (!at_end && ch == '"' && !single_quoted) {
            double_quoted = !double_quoted;
        } else if (!at_end && ch == '(' && !single_quoted && !double_quoted) {
            ++paren_depth;
        } else if (!at_end && ch == ')' && !single_quoted && !double_quoted &&
                   paren_depth > 0) {
            --paren_depth;
        }
        if (!at_end && (ch != ',' || single_quoted || double_quoted ||
                        paren_depth != 0)) continue;

        size_t value_begin = field_begin;
        size_t value_end = index;
        while (value_begin < value_end &&
               (line[value_begin] == ' ' || line[value_begin] == '\t')) {
            ++value_begin;
        }
        while (value_end > value_begin &&
               (line[value_end - 1] == ' ' || line[value_end - 1] == '\t')) {
            --value_end;
        }
        fields.push_back(line.substr(value_begin, value_end - value_begin));
        if (value_spans) value_spans->emplace_back(value_begin, value_end);
        field_begin = index + 1;
    }
    return fields;
}

std::string quoted_bve_string(const std::string& text) {
    if (text.find_first_of("'\r\n") != std::string::npos) {
        throw std::runtime_error("single quotes and line breaks are not supported in edited string values");
    }
    return "'" + text + "'";
}

std::string value_to_edit_text(const Value& value) {
    if (value.is_null()) return {};
    return as_text(value);
}

std::string value_to_bve_arg(const Value& value) {
    switch (value.kind) {
        case ValueKind::Null: return {};
        case ValueKind::ContinueValue: return "c";
        case ValueKind::Number: return canonical_number(value.number);
        case ValueKind::String: return quoted_bve_string(value.text);
    }
    return {};
}

bool has_field_change(const MapEditChange& change, const std::string& key) {
    return change.field_changes.find(key) != change.field_changes.end();
}

std::string field_text_or(const MapEditChange& change, const std::string& key,
                          const std::string& fallback) {
    auto it = change.field_changes.find(key);
    return it == change.field_changes.end() ? fallback : it->second;
}

const std::string* raw_arg_at(const std::vector<std::string>& raw_args, size_t index) {
    return index < raw_args.size() ? &raw_args[index] : nullptr;
}

std::string required_string_field(const MapEditChange& change, const std::string& key,
                                  const std::string& fallback) {
    std::string value = trim_field_copy(field_text_or(change, key, fallback));
    if (value.empty()) throw std::runtime_error("required edit field is empty: " + key);
    return value;
}

std::string numeric_field(const MapEditChange& change, const std::string& key,
                          double fallback, const std::string* raw_fallback = nullptr) {
    auto it = change.field_changes.find(key);
    if (it != change.field_changes.end()) return normalized_number_arg(it->second);
    if (raw_fallback) return trim_field_copy(*raw_fallback);
    return fallback_edit_number(fallback);
}

std::string optional_numeric_value_field(const MapEditChange& change, const std::string& key,
                                         const Value& fallback,
                                         const std::string* raw_fallback = nullptr) {
    auto it = change.field_changes.find(key);
    if (it != change.field_changes.end()) {
        const std::string value = trim_field_copy(it->second);
        return value.empty() ? std::string{} : normalized_number_arg(value);
    }
    if (raw_fallback) return trim_field_copy(*raw_fallback);
    return value_to_bve_arg(fallback);
}

std::string required_numeric_value_field(const MapEditChange& change, const std::string& key,
                                         const Value& fallback,
                                         const std::string* raw_fallback = nullptr) {
    const std::string value =
        optional_numeric_value_field(change, key, fallback, raw_fallback);
    if (value.empty()) throw std::runtime_error("required edit field is empty: " + key);
    return value;
}

std::string track_key_field_as_bve_arg(const MapEditChange& change, const std::string& key,
                                       const Value& fallback,
                                       const std::string* raw_fallback = nullptr) {
    auto it = change.field_changes.find(key);
    if (it == change.field_changes.end()) {
        if (raw_fallback) return trim_field_copy(*raw_fallback);
    }
    if (it == change.field_changes.end()) return value_to_bve_arg(fallback);
    return value_to_bve_arg(track_key_from_display_text(it->second));
}

std::string csv_field(const std::string& text) {
    if (text.find_first_of(",#\"\r\n") == std::string::npos &&
        text.find("//") == std::string::npos) return text;
    std::string out = "\"";
    for (char ch : text) {
        if (ch == '"') out += "\"\"";
        else out.push_back(ch);
    }
    out += "\"";
    return out;
}

struct CsvSourceLine {
    std::vector<std::string_view> fields;
    std::string_view comment_suffix;
};

bool editable_csv_list_values_equivalent(const char* field_name,
                                         const std::string& edited,
                                         const std::string& source) {
    if (std::string_view(field_name) != "bufferCount") {
        return edited == source;
    }
    const int edited_value = edited.empty() ? 1 : std::stoi(edited);
    return edited_value == parse_sound_buffer_count(source);
}

CsvSourceLine split_csv_source_line(std::string_view line) {
    CsvSourceLine result;
    size_t field_start = 0;
    bool quoted = false;
    for (size_t i = 0; i < line.size(); ++i) {
        const char ch = line[i];
        if (ch == '"') {
            if (quoted && i + 1 < line.size() && line[i + 1] == '"') {
                ++i;
            } else {
                quoted = !quoted;
            }
            continue;
        }
        if (quoted) continue;
        if (ch == ',') {
            result.fields.push_back(line.substr(field_start, i - field_start));
            field_start = i + 1;
            continue;
        }
        if (csv_comment_starts_at(line, i)) {
            size_t comment_start = i;
            while (comment_start > field_start &&
                   (line[comment_start - 1] == ' ' || line[comment_start - 1] == '\t')) {
                --comment_start;
            }
            result.fields.push_back(
                line.substr(field_start, comment_start - field_start));
            result.comment_suffix = line.substr(comment_start);
            return result;
        }
    }
    result.fields.push_back(line.substr(field_start));
    return result;
}

template <size_t FieldCount, typename Normalize>
std::string build_editable_csv_list_statement(
    const MapEditChange& change,
    const ParsedStatement& statement,
    const std::array<const char*, FieldCount>& field_names,
    const std::array<std::string, FieldCount>& fallback_values,
    Normalize&& normalize) {
    // A moved row supplies the complete source row as its template. Fields
    // whose semantic values already match that template keep their original
    // quoting/spacing; only genuinely edited slots are serialized anew.
    const std::string& source_text = change.replacement_statement.empty()
        ? statement.raw_arguments
        : change.replacement_statement;
    const CsvSourceLine source = split_csv_source_line(source_text);
    const std::vector<std::string> source_values =
        parse_comma_separated_fields(source_text, true);
    const size_t output_count =
        std::max(source.fields.size(), FieldCount);

    std::ostringstream out;
    for (size_t i = 0; i < output_count; ++i) {
        if (i) out << ",";
        if (i < FieldCount) {
            const auto edited = change.field_changes.find(field_names[i]);
            if (edited != change.field_changes.end()) {
                const std::string normalized = normalize(edited->second, i);
                const bool preserves_source =
                    i < source.fields.size() && i < source_values.size() &&
                    (std::string_view(field_names[i]) == "bufferCount"
                        ? editable_csv_list_values_equivalent(
                            field_names[i], normalized, source_values[i])
                        : normalized == normalize(source_values[i], i));
                if (preserves_source) {
                    out << source.fields[i];
                } else {
                    out << csv_field(normalized);
                }
                continue;
            }
        }
        if (i < source.fields.size()) {
            out << source.fields[i];
        } else {
            out << csv_field(fallback_values[i]);
        }
    }
    out << source.comment_suffix;
    return out.str();
}

std::string build_structure_model_statement(const MapEditChange& change,
                                            const ParsedStatement& statement,
                                            const StructureModel& row) {
    const std::array<std::string, 2> fallback = {
        row.structure_key, row.file_path
    };
    return build_editable_csv_list_statement(
        change, statement, k_structure_list_field_names, fallback,
        [](const std::string& value, size_t field_index) {
            return normalized_resource_list_edit_value(
                value, k_structure_list_field_names[field_index]);
        });
}

std::string build_sound_list_statement(const MapEditChange& change,
                                       const ParsedStatement& statement,
                                       const SoundListEntry& row) {
    const std::array<std::string, 3> fallback = {
        row.sound_key, row.file_path, std::to_string(row.buffer_count)
    };
    return build_editable_csv_list_statement(
        change, statement, k_sound_list_field_names, fallback,
        [](const std::string& value, size_t field_index) {
            return field_index == 2
                ? normalized_sound_buffer_count_edit_value(value)
                : normalized_resource_list_edit_value(
                    value, k_sound_list_field_names[field_index]);
        });
}

std::string build_station_list_statement(const MapEditChange& change,
                                         const ParsedStatement& statement,
                                         const StationListEntry& row) {
    return build_editable_csv_list_statement(
        change, statement, k_station_list_field_names, row.fields,
        [](const std::string& value, size_t field_index) {
            return normalized_station_list_edit_value(value, field_index);
        });
}

std::optional<size_t> signal_aspect_add_glare_count(
    const MapEditChange& change) {
    const auto input = change.field_changes.find("addGlare");
    if (input == change.field_changes.end()) return std::nullopt;
    const std::string text = trim_field_copy(input->second);
    if (text.empty()) {
        throw std::runtime_error("Signal aspect addGlare field is empty");
    }
    size_t count = 0;
    for (const char ch : text) {
        if (ch < '0' || ch > '9' ||
            count > (std::numeric_limits<size_t>::max() -
                     static_cast<size_t>(ch - '0')) / 10) {
            throw std::runtime_error(
                "Signal aspect addGlare field must be a positive integer");
        }
        count = count * 10 + static_cast<size_t>(ch - '0');
    }
    if (count == 0) {
        throw std::runtime_error(
            "Signal aspect addGlare field must be a positive integer");
    }
    return count;
}

std::string build_signal_aspect_statement(
    const MapEditChange& change,
    const ParsedStatement& statement,
    std::string_view inserted_newline) {
    const std::string& source_text = change.replacement_statement.empty()
        ? statement.raw_arguments
        : change.replacement_statement;
    const SignalAspectSourceValues source_values =
        parse_signal_aspect_source_values(source_text);
    bool delete_glare = false;
    if (const auto input =
            change.field_changes.find("deleteGlare");
        input != change.field_changes.end()) {
        if (trim_field_copy(input->second) != "1") {
            throw std::runtime_error(
                "Signal aspect deleteGlare field must be 1");
        }
        delete_glare = true;
    }
    const std::optional<size_t> add_glare_count =
        signal_aspect_add_glare_count(change);
    if (delete_glare && add_glare_count) {
        throw std::runtime_error(
            "Signal aspect cannot add and delete glare in one edit");
    }

    std::ostringstream out;
    size_t pos = 0;
    size_t structure_index = 0;
    size_t main_structure_count = 0;
    bool has_main_row = false;
    bool has_secondary_row = false;
    while (pos <= source_text.size()) {
        const TextLineSpan source_line = text_line_span(source_text, pos);
        const size_t content_end = source_line.content_end;
        const std::string_view line(
            source_text.data() + pos, content_end - pos);
        const std::string trimmed = trim_field_copy(std::string(line));
        bool emit_line = true;
        if (trimmed.empty() || trimmed[0] == '#') {
            out << line;
        } else {
            const CsvSourceLine source = split_csv_source_line(line);
            std::vector<std::string> semantic_fields =
                parse_comma_separated_fields(std::string(line), true);
            trim_trailing_empty_fields(semantic_fields);
            if (semantic_fields.empty()) {
                out << line;
            } else {
                const bool main_row = !has_main_row;
                if (main_row) {
                    if (semantic_fields[0].empty()) {
                        throw std::runtime_error(
                            "Signal aspect source block has no main row");
                    }
                    has_main_row = true;
                } else if (!semantic_fields[0].empty()) {
                    throw std::runtime_error(
                        "Signal aspect source block contains multiple main rows");
                } else {
                    has_secondary_row = true;
                }

                if (delete_glare && !main_row) {
                    structure_index += semantic_fields.size() - 1;
                    emit_line = false;
                } else {
                    for (size_t field = 0; field < source.fields.size(); ++field) {
                        if (field) out << ",";
                        const bool semantic_field =
                            field < semantic_fields.size();
                        std::string field_name;
                        bool required = false;
                        if (main_row && field == 0) {
                            field_name = "signalAspectKey";
                            required = true;
                        } else if (field > 0 && semantic_field) {
                            field_name = signal_aspect_structure_key_field_name(
                                structure_index++);
                        }
                        const auto edited = field_name.empty()
                            ? change.field_changes.end()
                            : change.field_changes.find(field_name);
                        if (edited == change.field_changes.end()) {
                            out << source.fields[field];
                            continue;
                        }
                        const std::string normalized =
                            normalized_signal_aspect_edit_value(
                                edited->second, field_name, required);
                        const std::string source_value =
                            normalized_signal_aspect_edit_value(
                                semantic_fields[field], field_name, required);
                        if (normalized == source_value) {
                            out << source.fields[field];
                        } else {
                            const std::string_view raw_field =
                                source.fields[field];
                            size_t value_begin = 0;
                            while (value_begin < raw_field.size() &&
                                   (raw_field[value_begin] == ' ' ||
                                    raw_field[value_begin] == '\t')) {
                                ++value_begin;
                            }
                            size_t value_end = raw_field.size();
                            while (value_end > value_begin &&
                                   (raw_field[value_end - 1] == ' ' ||
                                    raw_field[value_end - 1] == '\t')) {
                                --value_end;
                            }
                            out << raw_field.substr(0, value_begin)
                                << csv_field(normalized)
                                << raw_field.substr(value_end);
                        }
                    }
                    out << source.comment_suffix;
                    if (main_row) main_structure_count = structure_index;
                }
            }
        }

        const size_t line_after = source_line.next_begin;
        if (emit_line && content_end < line_after) {
            out << source_text.substr(content_end, line_after - content_end);
        }
        if (!source_line.has_terminator()) break;
        pos = source_line.next_begin;
    }

    const size_t source_structure_count = structure_index;
    if (!has_main_row ||
        source_structure_count != source_values.structure_keys.size()) {
        throw std::runtime_error(
            "Signal aspect source block field mapping is inconsistent");
    }
    std::vector<std::string> added_glare_values;
    if (add_glare_count) {
        if (has_secondary_row) {
            throw std::runtime_error(
                "Signal aspect already has a glare row");
        }
        if (main_structure_count == 0 ||
            *add_glare_count != main_structure_count) {
            throw std::runtime_error(
                "Signal aspect glare must use the main row structure-key count");
        }
        added_glare_values.reserve(*add_glare_count);
        for (size_t index = 0; index < *add_glare_count; ++index) {
            const std::string field_name = signal_aspect_structure_key_field_name(
                source_structure_count + index);
            const auto value = change.field_changes.find(field_name);
            if (value == change.field_changes.end()) {
                throw std::runtime_error(
                    "Signal aspect addGlare is missing field: " + field_name);
            }
            added_glare_values.push_back(normalized_signal_aspect_edit_value(
                value->second, field_name, false));
        }
        if (std::none_of(added_glare_values.begin(), added_glare_values.end(),
                         [](const std::string& value) { return !value.empty(); })) {
            throw std::runtime_error(
                "Signal aspect glare requires at least one structure key");
        }
    }
    for (const auto& field : change.field_changes) {
        if (field.first == "signalAspectKey" ||
            field.first == "deleteGlare" ||
            field.first == "addGlare") {
            continue;
        }
        size_t key_index = 0;
        if (!parse_signal_aspect_structure_key_field_name(
                field.first, key_index)) {
            throw std::runtime_error(
                "unsupported Signal aspect edit field: " + field.first);
        }
        if (key_index >= source_structure_count + added_glare_values.size()) {
            throw std::runtime_error(
                "Signal aspect edit cannot add a structure-key column: " +
                field.first);
        }
    }
    if (!added_glare_values.empty()) {
        out << (inserted_newline.empty() ? std::string_view("\n") : inserted_newline);
        for (size_t index = 0; index < added_glare_values.size(); ++index) {
            out << "," << csv_field(added_glare_values[index]);
        }
    }
    return out.str();
}

std::string build_station_put_statement(const MapEditChange& change,
                                        const ParsedStatement& statement,
                                        const StationPut& row) {
    std::string station_key = required_string_field(change, "stationKey", value_to_edit_text(row.station_key));
    std::string raw_args = trim_field_copy(statement.raw_arguments);
    const bool parameter_change = has_field_change(change, "door") ||
        has_field_change(change, "margin1") || has_field_change(change, "margin2");
    if (parameter_change) {
        std::vector<std::string> args = parse_bve_argument_fields(statement.raw_arguments);
        if (args.size() < 3) args.resize(3);
        args[0] = optional_numeric_value_field(change, "door", row.door, raw_arg_at(args, 0));
        args[1] = optional_numeric_value_field(change, "margin1", row.margin1,
                                               raw_arg_at(args, 1));
        args[2] = optional_numeric_value_field(change, "margin2", row.margin2,
                                               raw_arg_at(args, 2));
        while (!args.empty() && args.back().empty()) args.pop_back();
        raw_args.clear();
        for (size_t index = 0; index < args.size(); ++index) {
            if (index) raw_args += ",";
            raw_args += args[index];
        }
    }
    std::ostringstream out;
    out << "Station[" << quoted_bve_string(station_key) << "].Put("
        << raw_args << ");";
    return out.str();
}

std::string build_irregularity_statement(const MapEditChange& change,
                                         const ParsedStatement& statement,
                                         const IrregularityChange& row) {
    const std::vector<std::string> raw_args = parse_bve_argument_fields(statement.raw_arguments);
    std::ostringstream out;
    out << "Irregularity.Change("
        << numeric_field(change, "x", row.x, raw_arg_at(raw_args, 0)) << ","
        << numeric_field(change, "y", row.y, raw_arg_at(raw_args, 1)) << ","
        << numeric_field(change, "r", row.r, raw_arg_at(raw_args, 2)) << ","
        << numeric_field(change, "lx", row.lx, raw_arg_at(raw_args, 3)) << ","
        << numeric_field(change, "ly", row.ly, raw_arg_at(raw_args, 4)) << ","
        << numeric_field(change, "lr", row.lr, raw_arg_at(raw_args, 5))
        << ");";
    return out.str();
}

bool has_non_distance_field_change(const MapEditChange& change) {
    return std::any_of(change.field_changes.begin(), change.field_changes.end(),
                       [](const auto& field) { return field.first != "distance"; });
}

std::string raw_object_key_argument(const ParsedStatement& statement) {
    const std::string& text = statement.raw_text;
    const size_t open = text.find('[');
    if (open == std::string::npos) return {};
    bool single_quoted = false;
    bool double_quoted = false;
    int nested = 0;
    for (size_t i = open + 1; i < text.size(); ++i) {
        const char ch = text[i];
        if (ch == '\'' && !double_quoted) single_quoted = !single_quoted;
        else if (ch == '"' && !single_quoted) double_quoted = !double_quoted;
        else if (!single_quoted && !double_quoted) {
            if (ch == '[') ++nested;
            else if (ch == ']' && nested-- == 0) {
                return trim_field_copy(text.substr(open + 1, i - open - 1));
            }
        }
    }
    return {};
}

std::string object_key_field_as_bve_arg(const MapEditChange& change,
                                        const std::string& key,
                                        const Value& fallback,
                                        const ParsedStatement& statement) {
    auto edited = change.field_changes.find(key);
    if (edited != change.field_changes.end()) {
        return quoted_bve_string(required_string_field(
            change, key, value_to_edit_text(fallback)));
    }
    std::string raw = raw_object_key_argument(statement);
    return raw.empty() ? value_to_bve_arg(fallback) : raw;
}

std::string replace_raw_object_key_argument(const ParsedStatement& statement,
                                            const std::string& replacement) {
    const std::string& text = statement.raw_text;
    const size_t open = text.find('[');
    if (open == std::string::npos) {
        throw std::runtime_error("source statement has no object key");
    }
    bool single_quoted = false;
    bool double_quoted = false;
    int nested = 0;
    for (size_t index = open + 1; index < text.size(); ++index) {
        const char ch = text[index];
        if (ch == '\'' && !double_quoted) single_quoted = !single_quoted;
        else if (ch == '"' && !single_quoted) double_quoted = !double_quoted;
        else if (!single_quoted && !double_quoted) {
            if (ch == '[') {
                ++nested;
            } else if (ch == ']' && nested-- == 0) {
                size_t value_begin = open + 1;
                while (value_begin < index &&
                       (text[value_begin] == ' ' || text[value_begin] == '\t')) {
                    ++value_begin;
                }
                size_t value_end = index;
                while (value_end > value_begin &&
                       (text[value_end - 1] == ' ' || text[value_end - 1] == '\t')) {
                    --value_end;
                }
                return text.substr(0, value_begin) + replacement +
                    text.substr(value_end);
            }
        }
    }
    throw std::runtime_error("source statement has an unterminated object key");
}

std::string string_value_field_as_bve_arg(const MapEditChange& change,
                                          const std::string& key,
                                          const Value& fallback,
                                          const std::string* raw_fallback = nullptr) {
    auto edited = change.field_changes.find(key);
    if (edited == change.field_changes.end()) {
        if (raw_fallback) return trim_field_copy(*raw_fallback);
        return value_to_bve_arg(fallback);
    }
    return quoted_bve_string(required_string_field(
        change, key, value_to_edit_text(fallback)));
}

std::string source_change_method(const ParsedStatement& statement,
                                 const char* expected_object) {
    const std::string lower = ascii_lower(statement.statement_kind);
    const std::string prefix = ascii_lower(expected_object) + ".";
    if (lower.rfind(prefix, 0) != 0) {
        throw std::runtime_error("unexpected source statement kind: " +
                                 statement.statement_kind);
    }
    const std::string method = lower.substr(prefix.size());
    if (method == "set") return "Set";
    if (method == "interpolate") return "Interpolate";
    if (method == "change") return "Change";
    if (method == "play") return "Play";
    if (method == "put") return "Put";
    throw std::runtime_error("unsupported source statement method: " +
                             statement.statement_kind);
}

std::string build_beacon_statement(const MapEditChange& change,
                                   const ParsedStatement& statement,
                                   const BeaconPut& row) {
    if (!has_non_distance_field_change(change)) return statement.raw_text;
    const std::vector<std::string> args = parse_bve_argument_fields(statement.raw_arguments);
    std::ostringstream out;
    out << "Beacon.Put("
        << required_numeric_value_field(change, "type", row.type, raw_arg_at(args, 0)) << ","
        << required_numeric_value_field(change, "section", row.section, raw_arg_at(args, 1)) << ","
        << required_numeric_value_field(change, "sendData", row.send_data, raw_arg_at(args, 2))
        << ");";
    return out.str();
}

std::string build_map_sound_statement(const MapEditChange& change,
                                      const ParsedStatement& statement,
                                      const MapSoundPlay& row) {
    if (!has_non_distance_field_change(change)) return statement.raw_text;
    std::ostringstream out;
    out << "Sound[" << object_key_field_as_bve_arg(
        change, "soundKey", row.sound_key, statement) << "].Play();";
    return out.str();
}

std::string build_map_sound_3d_statement(const MapEditChange& change,
                                         const ParsedStatement& statement,
                                         const MapSound3DPut& row) {
    if (!has_non_distance_field_change(change)) return statement.raw_text;
    const std::vector<std::string> args = parse_bve_argument_fields(statement.raw_arguments);
    std::ostringstream out;
    out << "Sound3D[" << object_key_field_as_bve_arg(
        change, "soundKey", row.sound_key, statement) << "].Put("
        << numeric_field(change, "x", row.x, raw_arg_at(args, 0)) << ","
        << numeric_field(change, "y", row.y, raw_arg_at(args, 1)) << ");";
    return out.str();
}

template <typename Row>
std::string build_noise_statement(const MapEditChange& change,
                                  const ParsedStatement& statement,
                                  const Row& row,
                                  const char* object_name,
                                  const char* method_name) {
    if (!has_non_distance_field_change(change)) return statement.raw_text;
    const std::vector<std::string> args = parse_bve_argument_fields(statement.raw_arguments);
    std::ostringstream out;
    out << object_name << "." << method_name << "("
        << required_numeric_value_field(change, "index", row.index, raw_arg_at(args, 0))
        << ");";
    return out.str();
}

std::string build_rolling_noise_statement(const MapEditChange& change,
                                          const ParsedStatement& statement,
                                          const RollingNoiseChange& row) {
    return build_noise_statement(change, statement, row, "RollingNoise", "Change");
}

std::string build_flange_noise_statement(const MapEditChange& change,
                                         const ParsedStatement& statement,
                                         const FlangeNoiseChange& row) {
    return build_noise_statement(change, statement, row, "FlangeNoise", "Change");
}

std::string build_joint_noise_statement(const MapEditChange& change,
                                        const ParsedStatement& statement,
                                        const JointNoisePlay& row) {
    return build_noise_statement(change, statement, row, "JointNoise", "Play");
}

std::string build_background_statement(const MapEditChange& change,
                                       const ParsedStatement& statement,
                                       const BackgroundChange& row) {
    if (!has_non_distance_field_change(change)) return statement.raw_text;
    const std::vector<std::string> args = parse_bve_argument_fields(statement.raw_arguments);
    return "Background.Change(" + string_value_field_as_bve_arg(
        change, "structureKey", row.structure_key, raw_arg_at(args, 0)) + ");";
}

std::string build_adhesion_arguments(const std::string& a,
                                     const std::string& b,
                                     const std::string& c) {
    if (b.empty() != c.empty()) {
        throw std::runtime_error("Adhesion.Change requires either 1 or 3 parameters");
    }
    return a + (b.empty() ? "" : "," + b + "," + c);
}

std::string build_fog_arguments(std::string_view method,
                                const std::array<std::string, 4>& values) {
    const bool density = !values[0].empty();
    const bool color = !values[1].empty() && !values[2].empty() && !values[3].empty();
    const bool any_color = !values[1].empty() || !values[2].empty() || !values[3].empty();
    if (method == "Set") {
        if (!density || !color) throw std::runtime_error("Fog.Set requires 4 parameters");
    } else if (any_color != color || (color && !density)) {
        throw std::runtime_error("Fog.Interpolate requires 0, 1, or 4 parameters");
    }
    if (!density) return {};
    return values[0] + (color ? "," + values[1] + "," + values[2] + "," + values[3]
                              : std::string{});
}

std::string build_adhesion_statement(const MapEditChange& change,
                                     const ParsedStatement& statement,
                                     const AdhesionChange& row) {
    if (!has_non_distance_field_change(change)) return statement.raw_text;
    const std::vector<std::string> args = parse_bve_argument_fields(statement.raw_arguments);
    const std::string a = required_numeric_value_field(
        change, "a", row.a, raw_arg_at(args, 0));
    const std::string b = optional_numeric_value_field(
        change, "b", row.b, raw_arg_at(args, 1));
    const std::string c = optional_numeric_value_field(
        change, "c", row.c, raw_arg_at(args, 2));
    return "Adhesion.Change(" + build_adhesion_arguments(a, b, c) + ");";
}

std::string build_cab_illuminance_statement(const MapEditChange& change,
                                            const ParsedStatement& statement,
                                            const CabIlluminanceChange& row) {
    if (!has_non_distance_field_change(change)) return statement.raw_text;
    const std::vector<std::string> args = parse_bve_argument_fields(statement.raw_arguments);
    const std::string value = optional_numeric_value_field(
        change, "value", row.value, raw_arg_at(args, 0));
    const std::string method = value.empty()
        ? "Interpolate"
        : source_change_method(statement, "CabIlluminance");
    return "CabIlluminance." + method + "(" + value + ");";
}

std::string build_fog_statement(const MapEditChange& change,
                                const ParsedStatement& statement,
                                const FogChange& row) {
    if (!has_non_distance_field_change(change)) return statement.raw_text;
    const std::string method = source_change_method(statement, "Fog");
    const std::vector<std::string> args = parse_bve_argument_fields(statement.raw_arguments);
    std::array<std::string, 4> values = {
        optional_numeric_value_field(change, "density", row.density, raw_arg_at(args, 0)),
        optional_numeric_value_field(change, "red", row.red, raw_arg_at(args, 1)),
        optional_numeric_value_field(change, "green", row.green, raw_arg_at(args, 2)),
        optional_numeric_value_field(change, "blue", row.blue, raw_arg_at(args, 3)),
    };
    return "Fog." + method + "(" + build_fog_arguments(method, values) + ");";
}

std::string build_draw_distance_statement(const MapEditChange& change,
                                          const ParsedStatement& statement,
                                          const DrawDistanceChange& row) {
    if (!has_non_distance_field_change(change)) return statement.raw_text;
    const std::vector<std::string> args = parse_bve_argument_fields(statement.raw_arguments);
    return "DrawDistance.Change(" +
        numeric_field(change, "value", row.value, raw_arg_at(args, 0)) + ");";
}

std::string build_speed_limit_statement(const MapEditChange& change,
                                        const ParsedStatement& statement,
                                        const SpeedLimitEvent& row) {
    if (!has_non_distance_field_change(change)) return statement.raw_text;
    if (row.speed.is_null()) {
        throw std::runtime_error("SpeedLimit.End does not have a speed field");
    }
    const std::vector<std::string> args =
        parse_bve_argument_fields(statement.raw_arguments);
    return "SpeedLimit.Begin(" + required_numeric_value_field(
        change, "speed", row.speed, raw_arg_at(args, 0)) + ");";
}

std::string build_section_statement(const MapEditChange& change,
                                    const ParsedStatement& statement,
                                    const std::string& method) {
    if (!has_non_distance_field_change(change)) return statement.raw_text;
    validate_section_edit_fields(change);
    const SectionValuesEdit values = parse_section_values_edit(change);
    if (!values.changed) {
        throw std::runtime_error(
            "Section statement change requires at least one parameter field");
    }
    std::string arguments;
    if (values.has_count) {
        for (size_t index = 0; index < values.values.size(); ++index) {
            if (index) arguments += ",";
            arguments += normalized_number_arg(values.values[index]);
        }
    } else {
        const std::vector<std::string> raw_args =
            parse_bve_argument_fields(statement.raw_arguments);
        for (size_t index = 0; index < raw_args.size(); ++index) {
            if (index) arguments += ",";
            const auto edited = values.index_values.find(index);
            if (edited != values.index_values.end()) {
                arguments += normalized_number_arg(edited->second);
            } else {
                arguments += trim_field_copy(raw_args[index]);
            }
        }
    }
    return method + "(" + arguments + ");";
}

std::string build_section_begin_statement(const MapEditChange& change,
                                          const ParsedStatement& statement,
                                          const SectionBegin& row) {
    return build_section_statement(change, statement, row.method);
}

std::string build_section_speed_limit_statement(const MapEditChange& change,
                                                const ParsedStatement& statement,
                                                const SectionSpeedLimit& row) {
    return build_section_statement(change, statement, row.method);
}

std::string build_curve_statement(const MapEditChange& change,
                                  const ParsedStatement& statement,
                                  const CurveEditRow& row) {
    if (!has_non_distance_field_change(change)) return statement.raw_text;
    if (row.argument_count == 0) {
        throw std::runtime_error(row.method + " has no editable value fields");
    }
    const std::vector<std::string> args =
        parse_bve_argument_fields(statement.raw_arguments);
    std::string arguments = required_numeric_value_field(
        change, "radius", row.radius, raw_arg_at(args, 0));
    if (row.argument_count == 2) {
        arguments += "," + required_numeric_value_field(
            change, "cant", row.cant, raw_arg_at(args, 1));
    } else if (change.field_changes.find("cant") != change.field_changes.end()) {
        throw std::runtime_error(row.method + " source statement has no cant field");
    }
    return row.method + "(" + arguments + ");";
}

std::string build_gradient_statement(const MapEditChange& change,
                                     const ParsedStatement& statement,
                                     const GradientEditRow& row) {
    if (!has_non_distance_field_change(change)) return statement.raw_text;
    if (row.argument_count == 0) {
        throw std::runtime_error(row.method + " has no editable gradient field");
    }
    const std::vector<std::string> args =
        parse_bve_argument_fields(statement.raw_arguments);
    return row.method + "(" + required_numeric_value_field(
        change, "gradient", row.gradient, raw_arg_at(args, 0)) + ");";
}

std::string build_other_track_change_statement(
    const MapEditChange& change, const ParsedStatement& statement,
    const OtherTrackChange& row) {
    if (!has_non_distance_field_change(change)) return statement.raw_text;
    std::string source_text = statement.raw_text;
    const auto key_change = change.field_changes.find("trackKey");
    if (key_change != change.field_changes.end()) {
        source_text = replace_raw_object_key_argument(
            statement,
            track_key_field_as_bve_arg(change, "trackKey", row.track_key));
    }
    std::vector<std::pair<size_t, size_t>> argument_spans;
    std::vector<std::string> args =
        parse_bve_argument_fields(statement.raw_arguments, &argument_spans);
    if (args.size() != row.parameters.size()) {
        throw std::runtime_error(
            "other-track source parameter count no longer matches parsed row");
    }
    std::vector<std::optional<std::string>> replacements(args.size());
    for (const auto& field : change.field_changes) {
        if (field.first == "distance" || field.first == "trackKey") continue;
        constexpr std::string_view prefix = "parameter";
        if (field.first.compare(0, prefix.size(), prefix) != 0) {
            throw std::runtime_error("other-track method is read-only");
        }
        const std::string suffix = field.first.substr(prefix.size());
        if (suffix.empty() ||
            suffix.find_first_not_of("0123456789") != std::string::npos) {
            throw std::runtime_error("invalid other-track parameter field: " +
                                     field.first);
        }
        const size_t index = static_cast<size_t>(std::stoull(suffix));
        if (index >= args.size()) {
            throw std::runtime_error(
                "other-track source statement has no " + field.first);
        }
        replacements[index] = required_numeric_value_field(
            change, field.first, row.parameters[index], raw_arg_at(args, index));
    }

    if (argument_spans.size() != args.size()) {
        throw std::runtime_error(
            "other-track source argument layout no longer matches parsed row");
    }
    std::string raw_arguments = statement.raw_arguments;
    for (size_t index = replacements.size(); index-- > 0;) {
        if (!replacements[index]) continue;
        const auto [begin, end] = argument_spans[index];
        raw_arguments.replace(begin, end - begin, *replacements[index]);
    }

    const size_t open = source_text.find('(');
    const size_t close = source_text.rfind(')');
    if (open == std::string::npos || close == std::string::npos || close < open) {
        throw std::runtime_error("other-track source statement shape is invalid");
    }
    std::string output = source_text.substr(0, open + 1);
    output += raw_arguments;
    output += source_text.substr(close);
    return output;
}

template <typename Row, auto Builder>
std::string build_simple_statement_adapter(const MapEditChange& change,
                                           const ParsedStatement& statement,
                                           const void* row) {
    return Builder(change, statement, *static_cast<const Row*>(row));
}

std::string build_structure_put_statement(const MapEditChange& change,
                                          const ParsedStatement& statement,
                                          const StructurePut& row,
                                          bool between) {
    std::string structure_key = required_string_field(change, "structureKey", value_to_edit_text(row.structure_key));
    std::vector<std::string> raw_args = parse_bve_argument_fields(statement.raw_arguments);
    const bool source_put0 = ascii_lower(row.method) == "put0";
    std::string output_method = row.method;
    auto method_change = change.field_changes.find("method");
    if (method_change != change.field_changes.end()) {
        const std::string requested_method = ascii_lower(
            trim_field_copy(method_change->second));
        const bool supported_conversion =
            (source_put0 && requested_method == "put") ||
            (!source_put0 && requested_method == "put0");
        if (between || !supported_conversion) {
            throw std::runtime_error("unsupported Structure placement method conversion");
        }
        output_method = requested_method == "put0" ? "Put0" : "Put";
    }
    const bool output_put0 = ascii_lower(output_method) == "put0";
    std::ostringstream out;
    out << "Structure[" << quoted_bve_string(structure_key) << "]." << output_method << "(";
    if (between) {
        out << track_key_field_as_bve_arg(change, "trackKey1", row.track_key1, raw_arg_at(raw_args, 0)) << ","
            << track_key_field_as_bve_arg(change, "trackKey2", row.track_key2, raw_arg_at(raw_args, 1)) << ","
            << numeric_field(change, "flag", row.flag, raw_arg_at(raw_args, 2));
    } else if (output_put0) {
        const size_t tilt_index = source_put0 ? 1 : 7;
        const size_t span_index = source_put0 ? 2 : 8;
        out << track_key_field_as_bve_arg(change, "trackKey", row.track_key, raw_arg_at(raw_args, 0)) << ","
            << numeric_field(change, "tilt", row.tilt, raw_arg_at(raw_args, tilt_index)) << ","
            << numeric_field(change, "span", row.span, raw_arg_at(raw_args, span_index));
    } else if (source_put0) {
        out << track_key_field_as_bve_arg(change, "trackKey", row.track_key, raw_arg_at(raw_args, 0)) << ","
            << numeric_field(change, "x", 0.0) << ","
            << numeric_field(change, "y", 0.0) << ","
            << numeric_field(change, "z", 0.0) << ","
            << numeric_field(change, "rx", 0.0) << ","
            << numeric_field(change, "ry", 0.0) << ","
            << numeric_field(change, "rz", 0.0) << ","
            << numeric_field(change, "tilt", row.tilt, raw_arg_at(raw_args, 1)) << ","
            << numeric_field(change, "span", row.span, raw_arg_at(raw_args, 2));
    } else {
        out << track_key_field_as_bve_arg(change, "trackKey", row.track_key, raw_arg_at(raw_args, 0)) << ","
            << numeric_field(change, "x", row.x, raw_arg_at(raw_args, 1)) << ","
            << numeric_field(change, "y", row.y, raw_arg_at(raw_args, 2)) << ","
            << numeric_field(change, "z", row.z, raw_arg_at(raw_args, 3)) << ","
            << numeric_field(change, "rx", row.rx, raw_arg_at(raw_args, 4)) << ","
            << numeric_field(change, "ry", row.ry, raw_arg_at(raw_args, 5)) << ","
            << numeric_field(change, "rz", row.rz, raw_arg_at(raw_args, 6)) << ","
            << numeric_field(change, "tilt", row.tilt, raw_arg_at(raw_args, 7)) << ","
            << numeric_field(change, "span", row.span, raw_arg_at(raw_args, 8));
    }
    out << ");";
    return out.str();
}

std::string build_signal_put_statement(const MapEditChange& change,
                                       const ParsedStatement& statement,
                                       const SignalPut& row) {
    std::vector<std::string> raw_args =
        parse_bve_argument_fields(statement.raw_arguments);
    const bool source_short_form = raw_args.size() == 4;
    const std::string requested_form =
        trim_field_copy(field_text_or(change, "form", source_short_form ? "short" : "full"));
    const bool convert_to_full = source_short_form && ascii_lower(requested_form) == "full";
    if ((!source_short_form && has_field_change(change, "form")) ||
        (source_short_form && has_field_change(change, "form") && !convert_to_full)) {
        throw std::runtime_error("unsupported Signal.Put form conversion");
    }
    for (const char* field : {"z", "rx", "ry", "rz", "tilt", "span"}) {
        if (source_short_form && !convert_to_full && has_field_change(change, field)) {
            throw std::runtime_error(
                "short Signal.Put requires confirmation before editing full-form fields");
        }
    }

    const auto aspect_key_change = change.field_changes.find("signalAspectKey");
    const std::string aspect_key_arg = aspect_key_change == change.field_changes.end()
        ? value_to_bve_arg(row.signal_aspect_key)
        : quoted_bve_string(required_string_field(
            change, "signalAspectKey", value_to_edit_text(row.signal_aspect_key)));
    if (aspect_key_arg.empty()) {
        throw std::runtime_error("required edit field is empty: signalAspectKey");
    }
    std::ostringstream out;
    out << "Signal[" << aspect_key_arg << "].Put("
        << required_numeric_value_field(change, "section", row.section,
                                        raw_arg_at(raw_args, 0)) << ","
        << track_key_field_as_bve_arg(change, "trackKey", row.track_key,
                                      raw_arg_at(raw_args, 1)) << ","
        << numeric_field(change, "x", row.x, raw_arg_at(raw_args, 2)) << ","
        << numeric_field(change, "y", row.y, raw_arg_at(raw_args, 3));
    if (!source_short_form || convert_to_full) {
        out << "," << numeric_field(change, "z", row.z, raw_arg_at(raw_args, 4))
            << "," << numeric_field(change, "rx", row.rx, raw_arg_at(raw_args, 5))
            << "," << numeric_field(change, "ry", row.ry, raw_arg_at(raw_args, 6))
            << "," << numeric_field(change, "rz", row.rz, raw_arg_at(raw_args, 7))
            << "," << numeric_field(change, "tilt", row.tilt, raw_arg_at(raw_args, 8))
            << "," << numeric_field(change, "span", row.span, raw_arg_at(raw_args, 9));
    }
    out << ");";
    return out.str();
}

std::string repeater_key_bve_arg(const MapEditChange& change,
                                 const RepeaterEvent& row) {
    const auto edited = change.field_changes.find("repeaterKey");
    const std::string key = edited == change.field_changes.end()
        ? value_to_bve_arg(row.repeater_key)
        : quoted_bve_string(required_string_field(
              change, "repeaterKey", value_to_edit_text(row.repeater_key)));
    if (key.empty()) throw std::runtime_error("Repeater key is empty");
    return key;
}

bool repeater_has_field_change(const MapEditChange& change, const char* key) {
    return has_field_change(change, key);
}

void reject_repeater_position_fields(const MapEditChange& change,
                                     const std::string& message) {
    for (const char* field : {"x", "y", "z", "rx", "ry", "rz"}) {
        if (repeater_has_field_change(change, field)) {
            throw std::runtime_error(message);
        }
    }
}

void append_repeater_structure_keys(std::ostringstream& out,
                                    const RepeaterStructureKeyEdit& edited_keys,
                                    const std::vector<Value>& fallback_keys,
                                    const std::vector<std::string>& raw_args,
                                    size_t raw_start) {
    if (edited_keys.changed) {
        for (const std::string& key : edited_keys.values) {
            out << "," << quoted_bve_string(key);
        }
        return;
    }
    for (size_t index = 0; index < fallback_keys.size(); ++index) {
        const std::string* raw = raw_arg_at(raw_args, raw_start + index);
        out << "," << (raw ? trim_field_copy(*raw) : value_to_bve_arg(fallback_keys[index]));
    }
}

std::string build_repeater_statement(const MapEditChange& change,
                                     const ParsedStatement& statement,
                                     const RepeaterEvent& row) {
    validate_repeater_edit_fields(change);
    if (change.field_changes.size() == 1 &&
        has_field_change(change, "repeaterKey")) {
        return replace_raw_object_key_argument(
            statement, repeater_key_bve_arg(change, row));
    }
    const RepeaterStructureKeyEdit edited_keys = parse_repeater_structure_key_edit(change);
    const std::string source_method = ascii_lower(row.method);
    const std::vector<std::string> raw_args = parse_bve_argument_fields(statement.raw_arguments);
    const std::string key = repeater_key_bve_arg(change, row);

    if (source_method == "end") {
        for (const auto& field : change.field_changes) {
            if (field.first != "distance" && field.first != "repeaterKey") {
                throw std::runtime_error(
                    "only distance and repeaterKey can be edited on Repeater.End");
            }
        }
        return "Repeater[" + key + "].End();";
    }
    if (source_method != "begin" && source_method != "begin0") {
        throw std::runtime_error("unsupported Repeater method: " + row.method);
    }

    const bool source_begin0 = source_method == "begin0";
    std::string output_method = row.method;
    if (has_field_change(change, "method")) {
        const std::string requested_method = ascii_lower(
            trim_field_copy(field_text_or(change, "method", row.method)));
        if (requested_method == "end") {
            for (const auto& field : change.field_changes) {
                if (field.first != "method") {
                    throw std::runtime_error(
                        "Repeater Begin to End conversion only supports the method field");
                }
            }
            return "Repeater[" + key + "].End();";
        }
        const bool supported_conversion =
            (source_begin0 && requested_method == "begin") ||
            (!source_begin0 && requested_method == "begin0");
        if (!supported_conversion) {
            throw std::runtime_error("unsupported Repeater method conversion");
        }
        output_method = requested_method == "begin0" ? "Begin0" : "Begin";
    }
    const bool output_begin0 = ascii_lower(output_method) == "begin0";
    if (source_begin0 && output_begin0) {
        reject_repeater_position_fields(
            change, "Repeater.Begin0 requires conversion to Begin before editing position");
    }

    std::ostringstream out;
    out << "Repeater[" << key << "]." << output_method << "("
        << track_key_field_as_bve_arg(change, "trackKey", row.track_key, raw_arg_at(raw_args, 0));
    if (output_begin0) {
        const size_t tilt_index = source_begin0 ? 1 : 7;
        const size_t span_index = source_begin0 ? 2 : 8;
        const size_t interval_index = source_begin0 ? 3 : 9;
        const size_t structure_key_index = source_begin0 ? 4 : 10;
        out << "," << numeric_field(change, "tilt", row.tilt, raw_arg_at(raw_args, tilt_index))
            << "," << numeric_field(change, "span", row.span, raw_arg_at(raw_args, span_index))
            << "," << numeric_field(change, "interval", row.interval, raw_arg_at(raw_args, interval_index));
        append_repeater_structure_keys(
            out, edited_keys, row.structure_keys, raw_args, structure_key_index);
    } else if (source_begin0) {
        out << "," << numeric_field(change, "x", 0.0)
            << "," << numeric_field(change, "y", 0.0)
            << "," << numeric_field(change, "z", 0.0)
            << "," << numeric_field(change, "rx", 0.0)
            << "," << numeric_field(change, "ry", 0.0)
            << "," << numeric_field(change, "rz", 0.0)
            << "," << numeric_field(change, "tilt", row.tilt, raw_arg_at(raw_args, 1))
            << "," << numeric_field(change, "span", row.span, raw_arg_at(raw_args, 2))
            << "," << numeric_field(change, "interval", row.interval, raw_arg_at(raw_args, 3));
        append_repeater_structure_keys(out, edited_keys, row.structure_keys, raw_args, 4);
    } else {
        out << "," << numeric_field(change, "x", row.x, raw_arg_at(raw_args, 1))
            << "," << numeric_field(change, "y", row.y, raw_arg_at(raw_args, 2))
            << "," << numeric_field(change, "z", row.z, raw_arg_at(raw_args, 3))
            << "," << numeric_field(change, "rx", row.rx, raw_arg_at(raw_args, 4))
            << "," << numeric_field(change, "ry", row.ry, raw_arg_at(raw_args, 5))
            << "," << numeric_field(change, "rz", row.rz, raw_arg_at(raw_args, 6))
            << "," << numeric_field(change, "tilt", row.tilt, raw_arg_at(raw_args, 7))
            << "," << numeric_field(change, "span", row.span, raw_arg_at(raw_args, 8))
            << "," << numeric_field(change, "interval", row.interval, raw_arg_at(raw_args, 9));
        append_repeater_structure_keys(out, edited_keys, row.structure_keys, raw_args, 10);
    }
    out << ");";
    return out.str();
}

bool source_start_less(const SourceSpan& a, const SourceSpan& b) {
    if (a.line != b.line) return a.line < b.line;
    if (a.column != b.column) return a.column < b.column;
    if (a.line_end != b.line_end) return a.line_end < b.line_end;
    return a.column_end < b.column_end;
}

bool is_distance_statement(const ParsedStatement& statement) {
    return statement.statement_kind == "Distance.Set";
}

std::string statement_insertion_text(const std::string& source,
                                     size_t offset,
                                     const std::string& statement,
                                     const SourceFileRecord& file) {
    std::string nl = newline_text(file.newline);
    std::string text;
    if (offset > 0 && offset <= source.size()) {
        char previous = source[offset - 1];
        if (previous != '\n' && previous != '\r') text += nl;
    }
    text += statement;
    text += nl;
    return text;
}

std::string line_indent_of(const SourcePatch& patch, const ParsedStatement& statement) {
    const size_t line_start = offset_from_line_column(
        patch.text, patch.line_starts, statement.source.line, 1);
    const size_t statement_start = source_range_in_text(patch, statement.source).first;
    if (line_start == std::string::npos || line_start > statement_start) {
        return {};
    }
    std::string indent = patch.text.substr(
        line_start, statement_start - line_start);
    if (!std::all_of(indent.begin(), indent.end(), [](char ch) {
            return ch == ' ' || ch == '\t';
        })) {
        return {};
    }
    return indent;
}

struct ReferenceInsertionPlan {
    size_t offset = 0;
    size_t anchor_statement_index = k_no_source_ref;
    std::string statement;
    size_t identity_begin = 0;
    size_t identity_end = 0;
};

struct ResourceListContentInsertionPlan {
    size_t offset = 0;
    size_t anchor_statement_index = k_no_source_ref;
    std::string indent;
};

bool line_tail_starts_comment(const std::string& text, size_t offset, size_t end) {
    if (offset >= end) return false;
    return text[offset] == '#' ||
        (text[offset] == '/' && offset + 1 < end && text[offset + 1] == '/');
}

ReferenceInsertionPlan plan_reference_insertion(const MapContext& ctx,
                                                size_t source_file_index,
                                                const SourcePatch& patch,
                                                const std::string& statement_text) {
    std::map<std::pair<size_t, size_t>, size_t> physical_statements;
    for (size_t i = 0; i < ctx.parsed_statements.size(); ++i) {
        const ParsedStatement& statement = ctx.parsed_statements[i];
        if (statement.source.source_file_index != source_file_index ||
            (statement.statement_kind != "Include" &&
             !resource_list_edit_spec_for_statement(statement.statement_kind) &&
             !is_distance_statement(statement))) {
            continue;
        }
        const auto key = std::make_pair(statement.source.byte_start, statement.source.byte_end);
        auto found = physical_statements.find(key);
        if (found == physical_statements.end() ||
            statement.global_order < ctx.parsed_statements[found->second].global_order) {
            physical_statements[key] = i;
        }
    }

    std::vector<size_t> statements;
    statements.reserve(physical_statements.size());
    for (const auto& entry : physical_statements) statements.push_back(entry.second);
    std::stable_sort(statements.begin(), statements.end(), [&](size_t left, size_t right) {
        const SourceSpan& a = ctx.parsed_statements[left].source;
        const SourceSpan& b = ctx.parsed_statements[right].source;
        if (a.byte_start != b.byte_start) return a.byte_start < b.byte_start;
        return source_start_less(a, b);
    });

    size_t first_distance = k_no_source_ref;
    size_t last_zero_distance_reference = k_no_source_ref;
    for (size_t index : statements) {
        const ParsedStatement& statement = ctx.parsed_statements[index];
        if (is_distance_statement(statement)) {
            first_distance = index;
            break;
        }
        if (statement.statement_kind == "Include" ||
            resource_list_edit_spec_for_statement(statement.statement_kind)) {
            last_zero_distance_reference = index;
        }
    }

    ReferenceInsertionPlan plan;
    const auto finish = [&](size_t offset, const std::string& indent,
                            size_t anchor) {
        plan.offset = offset;
        plan.anchor_statement_index = anchor;
        plan.statement = indent + statement_text;
        plan.identity_begin = indent.size();
        plan.identity_end = plan.identity_begin + statement_text.size();
    };

    if (last_zero_distance_reference != k_no_source_ref) {
        const ParsedStatement& anchor = ctx.parsed_statements[last_zero_distance_reference];
        const auto range = source_range_in_text(patch, anchor.source);
        const size_t line_start = offset_from_line_column(
            patch.text, patch.line_starts, anchor.source.line, 1);
        if (line_start == std::string::npos) {
            throw std::runtime_error("failed to locate reference insertion line");
        }
        const TextLineSpan line = text_line_span(patch.text, line_start);
        size_t tail = range.second;
        while (tail < line.content_end &&
               (patch.text[tail] == ' ' || patch.text[tail] == '\t')) {
            ++tail;
        }
        const bool keep_tail_with_anchor =
            tail == line.content_end ||
            line_tail_starts_comment(patch.text, tail, line.content_end);
        finish(keep_tail_with_anchor ? line.next_begin : range.second,
               line_indent_of(patch, anchor), last_zero_distance_reference);
        return plan;
    }

    if (first_distance != k_no_source_ref) {
        const ParsedStatement& anchor = ctx.parsed_statements[first_distance];
        const size_t line_start = offset_from_line_column(
            patch.text, patch.line_starts, anchor.source.line, 1);
        if (line_start == std::string::npos) {
            throw std::runtime_error("failed to locate distance insertion line");
        }
        finish(line_start, line_indent_of(patch, anchor), first_distance);
        return plan;
    }

    finish(patch.text.size(), {}, k_no_source_ref);
    return plan;
}

ResourceListContentInsertionPlan plan_resource_list_content_insertion(
    MapContext& ctx,
    const MapEditChange& change,
    const ResourceListEditSpec& spec,
    size_t source_file_index,
    const SourcePatch& patch) {
    const ResourceListLoad* load = resource_list_load_for_kind(ctx, spec.kind);
    if (!load || !patch.record ||
        normalized_source_key(load->resolved_path) !=
            normalized_source_key(patch.record->file_path)) {
        throw std::runtime_error(
            "resource-list insert target does not match the loaded " +
            std::string(spec.error_key) + " list");
    }

    const auto make_before_plan = [&](size_t statement_index) {
        if (statement_index >= ctx.parsed_statements.size()) {
            throw std::runtime_error("resource-list insert anchor is invalid");
        }
        const ParsedStatement& statement = ctx.parsed_statements[statement_index];
        const auto range = source_range_in_text(patch, statement.source);
        const size_t line_start = offset_from_line_column(
            patch.text, patch.line_starts, statement.source.line, 1);
        if (line_start == std::string::npos || line_start > range.first) {
            throw std::runtime_error("failed to locate resource-list insert anchor");
        }
        ResourceListContentInsertionPlan plan;
        plan.offset = line_start;
        plan.anchor_statement_index = statement_index;
        plan.indent = line_indent_of(patch, statement);
        return plan;
    };

    if (!change.insert_before_edit_id.empty()) {
        const EditableTarget anchor = find_editable_target(
            ctx, change.insert_before_edit_id);
        if (anchor.statement_index == k_no_source_ref ||
            anchor.statement_index >= ctx.parsed_statements.size() ||
            anchor.row_kind != spec.content_row_kind) {
            throw std::runtime_error(
                "resource-list insert anchor is not a matching list row: " +
                change.insert_before_edit_id);
        }
        if (ctx.parsed_statements[anchor.statement_index].source.source_file_index !=
            source_file_index) {
            throw std::runtime_error(
                "resource-list insert anchor belongs to another source file");
        }
        return make_before_plan(anchor.statement_index);
    }

    size_t last_statement_index = k_no_source_ref;
    size_t last_end = 0;
    for (size_t index = 0; index < ctx.parsed_statements.size(); ++index) {
        const ParsedStatement& statement = ctx.parsed_statements[index];
        if (statement.source.source_file_index != source_file_index ||
            statement.statement_kind !=
                resource_list_content_statement_kind(spec.kind)) {
            continue;
        }
        const auto range = source_range_in_text(patch, statement.source);
        if (last_statement_index == k_no_source_ref || range.second > last_end) {
            last_statement_index = index;
            last_end = range.second;
        }
    }
    if (last_statement_index == k_no_source_ref) {
        // A list file that only carries its header is a valid official starting
        // point. Append the first logical row after the final physical line and
        // reuse the shared insertion text rules for newline/encoding fidelity.
        ResourceListContentInsertionPlan plan;
        plan.offset = patch.text.size();
        plan.anchor_statement_index = k_no_source_ref;
        plan.indent.clear();
        return plan;
    }

    const ParsedStatement& last = ctx.parsed_statements[last_statement_index];
    const size_t line_start = offset_from_line_column(
        patch.text, patch.line_starts, last.source.line_end, 1);
    if (line_start == std::string::npos) {
        throw std::runtime_error("failed to locate resource-list append line");
    }
    const TextLineSpan line = text_line_span(patch.text, line_start);
    ResourceListContentInsertionPlan plan;
    plan.offset = line.next_begin;
    plan.anchor_statement_index = last_statement_index;
    plan.indent = line_indent_of(patch, last);
    return plan;
}

template <typename Row>
bool match_edit_ref(MapContext& ctx, const Row& row, const std::string& row_kind,
                    size_t row_index, const std::string& edit_id, EditableTarget& target) {
    if (!row.edit_ref.valid() || row.edit_ref.statement_index >= ctx.parsed_statements.size()) return false;
    if (element_edit_id(ctx, row.edit_ref, row_kind) != edit_id) return false;
    target.statement_index = row.edit_ref.statement_index;
    target.row_kind = row_kind;
    target.row_index = row_index;
    target.element_index = row.edit_ref.element_index;
    target.elements_for_statement =
        ctx.parsed_statements[target.statement_index].editable_element_count;
    return true;
}

template <typename Row, auto Builder>
bool match_simple_edit_ref(MapContext& ctx, const Row& row,
                           const std::string& row_kind, size_t row_index,
                           const std::string& edit_id, EditableTarget& target) {
    if (!match_edit_ref(ctx, row, row_kind, row_index, edit_id, target)) return false;
    target.simple_row = &row;
    target.simple_statement_builder = &build_simple_statement_adapter<Row, Builder>;
    return true;
}

template <typename Rows, auto Builder>
bool find_simple_target(MapContext& ctx, const Rows& rows,
                        const std::string& row_kind, const std::string& edit_id,
                        EditableTarget& target) {
    for (size_t i = 0; i < rows.size(); ++i) {
        if (match_simple_edit_ref<typename Rows::value_type, Builder>(
                ctx, rows[i], row_kind, i, edit_id, target)) return true;
    }
    return false;
}

EditableTarget find_editable_target(MapContext& ctx, const std::string& edit_id) {
    EditableTarget target;
    for (size_t i = 0; i < ctx.parsed_statements.size(); ++i) {
        ParsedStatement& statement = ctx.parsed_statements[i];
        if (statement.statement_kind != "Include") continue;
        if (statement_edit_id(ctx, statement) != edit_id) continue;
        target.statement_index = i;
        target.row_kind = "include";
        target.row_index = i;
        target.elements_for_statement = 0;
        return target;
    }
    for (size_t i = 0; i < ctx.parsed_statements.size(); ++i) {
        ParsedStatement& statement = ctx.parsed_statements[i];
        if (!resource_list_edit_spec_for_statement(statement.statement_kind) ||
            statement_edit_id(ctx, statement) != edit_id) {
            continue;
        }
        target.statement_index = i;
        target.row_kind = "resourceList.load";
        target.row_index = i;
        target.elements_for_statement = 0;
        return target;
    }
    for (size_t i = 0; i < ctx.curves.size(); ++i) {
        const CurveEditRow& row = ctx.curves[i];
        if (match_edit_ref(ctx, row, "curve", i, edit_id, target)) {
            target.curve = &row;
            return target;
        }
    }
    for (size_t i = 0; i < ctx.gradients.size(); ++i) {
        const GradientEditRow& row = ctx.gradients[i];
        if (match_edit_ref(ctx, row, "gradient", i, edit_id, target)) {
            target.gradient = &row;
            return target;
        }
    }
    for (size_t i = 0; i < ctx.other_track_changes.size(); ++i) {
        const OtherTrackChange& row = ctx.other_track_changes[i];
        if (match_edit_ref(ctx, row, "otherTrack.change", i, edit_id, target)) {
            target.other_track_change = &row;
            return target;
        }
    }
    for (size_t i = 0; i < ctx.structure_models.size(); ++i) {
        const StructureModel& row = ctx.structure_models[i];
        if (match_edit_ref(ctx, row, "structure.model", i, edit_id, target)) {
            target.structure_model = &row;
            return target;
        }
    }
    size_t sound_index = 0;
    size_t sound_3d_index = 0;
    for (const SoundListEntry& row : ctx.sound_list) {
        const char* row_kind = row.is_3d ? "sound3D.list" : "sound.list";
        const size_t row_index = row.is_3d ? sound_3d_index++ : sound_index++;
        if (match_edit_ref(ctx, row, row_kind, row_index, edit_id, target)) {
            target.sound_list = &row;
            return target;
        }
    }
    for (size_t i = 0; i < ctx.structure_puts.size(); ++i) {
        const StructurePut& row = ctx.structure_puts[i];
        if (match_edit_ref(ctx, row, "structure.put", i, edit_id, target)) {
            target.structure_put = &row;
            return target;
        }
    }
    for (size_t i = 0; i < ctx.structure_betweens.size(); ++i) {
        const StructurePut& row = ctx.structure_betweens[i];
        if (match_edit_ref(ctx, row, "structure.between", i, edit_id, target)) {
            target.structure_put = &row;
            return target;
        }
    }
    for (size_t i = 0; i < ctx.station_puts.size(); ++i) {
        const StationPut& row = ctx.station_puts[i];
        if (match_edit_ref(ctx, row, "station.put", i, edit_id, target)) {
            target.station_put = &row;
            return target;
        }
    }
    {
        const auto entries = ordered_station_list_entries(ctx);
        for (size_t i = 0; i < entries.size(); ++i) {
            const StationListEntry& row = *entries[i];
            if (match_edit_ref(ctx, row, "station.list", i, edit_id, target)) {
                target.station_list = &row;
                return target;
            }
        }
    }
    for (size_t i = 0; i < ctx.signal_aspects.size(); ++i) {
        const SignalAspect& row = ctx.signal_aspects[i];
        if (match_edit_ref(
                ctx, row, "signal.aspect", i, edit_id, target)) {
            target.signal_aspect = &row;
            return target;
        }
    }
    for (size_t i = 0; i < ctx.signal_puts.size(); ++i) {
        const SignalPut& row = ctx.signal_puts[i];
        if (match_edit_ref(ctx, row, "signal.put", i, edit_id, target)) {
            target.signal_put = &row;
            return target;
        }
    }
    for (size_t i = 0; i < ctx.repeaters.size(); ++i) {
        const RepeaterEvent& row = ctx.repeaters[i];
        if (match_edit_ref(ctx, row, "repeater", i, edit_id, target)) {
            target.repeater = &row;
            return target;
        }
    }
    for (size_t i = 0; i < ctx.irregularities.size(); ++i) {
        const IrregularityChange& row = ctx.irregularities[i];
        if (match_edit_ref(ctx, row, "irregularity.change", i, edit_id, target)) {
            target.irregularity = &row;
            return target;
        }
    }
    if (find_simple_target<decltype(ctx.beacons), build_beacon_statement>(
            ctx, ctx.beacons, "beacon.put", edit_id, target) ||
        find_simple_target<decltype(ctx.section_begins), build_section_begin_statement>(
            ctx, ctx.section_begins, "section.begin", edit_id, target) ||
        find_simple_target<decltype(ctx.section_speed_limits),
                           build_section_speed_limit_statement>(
            ctx, ctx.section_speed_limits, "section.speedLimit", edit_id, target) ||
        find_simple_target<decltype(ctx.map_sounds), build_map_sound_statement>(
            ctx, ctx.map_sounds, "mapSound.play", edit_id, target) ||
        find_simple_target<decltype(ctx.map_sound_3d), build_map_sound_3d_statement>(
            ctx, ctx.map_sound_3d, "mapSound3D.put", edit_id, target) ||
        find_simple_target<decltype(ctx.rolling_noises), build_rolling_noise_statement>(
            ctx, ctx.rolling_noises, "rollingNoise.change", edit_id, target) ||
        find_simple_target<decltype(ctx.flange_noises), build_flange_noise_statement>(
            ctx, ctx.flange_noises, "flangeNoise.change", edit_id, target) ||
        find_simple_target<decltype(ctx.joint_noises), build_joint_noise_statement>(
            ctx, ctx.joint_noises, "jointNoise.play", edit_id, target) ||
        find_simple_target<decltype(ctx.backgrounds), build_background_statement>(
            ctx, ctx.backgrounds, "background.change", edit_id, target) ||
        find_simple_target<decltype(ctx.adhesions), build_adhesion_statement>(
            ctx, ctx.adhesions, "adhesion.change", edit_id, target) ||
        find_simple_target<decltype(ctx.cab_illuminance), build_cab_illuminance_statement>(
            ctx, ctx.cab_illuminance, "cabIlluminance.change", edit_id, target) ||
        find_simple_target<decltype(ctx.fogs), build_fog_statement>(
            ctx, ctx.fogs, "fog.change", edit_id, target) ||
        find_simple_target<decltype(ctx.draw_distances), build_draw_distance_statement>(
            ctx, ctx.draw_distances, "drawDistance.change", edit_id, target) ||
        find_simple_target<decltype(ctx.speedlimits), build_speed_limit_statement>(
            ctx, ctx.speedlimits, "speedlimit", edit_id, target)) {
        return target;
    }
    return target;
}

const KvEditTargetSnapshot& build_edit_target_snapshot(MapContext& ctx,
                                                       const std::string& edit_id) {
    if (edit_id.empty()) throw std::runtime_error("editId is empty");
    EditableTarget target = find_editable_target(ctx, edit_id);
    if (target.statement_index == k_no_source_ref ||
        target.statement_index >= ctx.parsed_statements.size()) {
        throw std::runtime_error("unsupported or unknown editId: " + edit_id);
    }

    const ParsedStatement& statement = ctx.parsed_statements[target.statement_index];
    const SourceFileRecord* file = statement.source.source_file_index < ctx.source_files.size()
        ? &ctx.source_files[statement.source.source_file_index]
        : nullptr;
    std::string expected_source_hash = file ? file->source_hash : std::string{};
    if (file) {
        auto override_it = ctx.source_overrides.find(file->source_key);
        if (override_it != ctx.source_overrides.end() &&
            !override_it->second.base_hash.empty()) {
            expected_source_hash = override_it->second.base_hash;
        }
    }

    auto storage = std::make_unique<EditTargetSnapshotStorage>();
    storage->string_arena.reserve(4096);
    auto string_ref = [&](const std::string& text) {
        KvStringRef ref{static_cast<std::uint64_t>(storage->string_arena.size()),
                        static_cast<std::uint64_t>(text.size())};
        storage->string_arena.append(text);
        return ref;
    };
    KvEditTargetSnapshot& view = storage->view;
    view.version = KV_EDIT_TARGET_SNAPSHOT_VERSION;
    if (target.row_kind == "signal.put" &&
        parse_bve_argument_fields(statement.raw_arguments).size() == 4) {
        view.flags |= KV_EDIT_TARGET_FLAG_SIGNAL_SHORT_FORM;
    }
    view.structure_size = sizeof(KvEditTargetSnapshot);
    view.edit_id = string_ref(edit_id);
    view.row_kind = string_ref(target.row_kind);
    view.row_index = static_cast<std::uint64_t>(target.row_index);
    view.elements_for_statement = static_cast<std::uint64_t>(target.elements_for_statement);
    view.statement_kind = string_ref(statement.statement_kind);
    view.source_hash = string_ref(file ? file->source_hash : std::string{});
    view.expected_source_hash = string_ref(expected_source_hash);
    view.source_file_path = string_ref(source_file_path(ctx, statement.source));
    view.source.source_file_index = static_cast<std::uint64_t>(statement.source.source_file_index);
    view.source.byte_start = static_cast<std::uint64_t>(statement.source.byte_start);
    view.source.byte_end = static_cast<std::uint64_t>(statement.source.byte_end);
    view.source.line = statement.source.line;
    view.source.column = statement.source.column;
    view.source.line_end = statement.source.line_end;
    view.source.column_end = statement.source.column_end;
    const std::vector<std::string>& include_stack = source_include_stack(ctx, statement.source);
    view.source.include_stack.offset = 0;
    view.source.include_stack.count = static_cast<std::uint64_t>(include_stack.size());
    storage->string_refs.reserve(include_stack.size());
    for (const std::string& item : include_stack) storage->string_refs.push_back(string_ref(item));
    view.source.include_invocation_key = string_ref(
        source_include_invocation_key(ctx, statement.source));
    view.raw_text = string_ref(statement.raw_text);
    view.raw_text_preview = string_ref(statement.raw_text_preview);
    view.raw_arguments = string_ref(statement.raw_arguments);
    view.distance_expression = string_ref(statement.distance_expression);
    view.distance_value = statement.distance_value;
    view.string_data = storage->string_arena.empty() ? nullptr : storage->string_arena.data();
    view.string_size = static_cast<std::uint64_t>(storage->string_arena.size());
    view.string_refs = storage->string_refs.empty() ? nullptr : storage->string_refs.data();
    view.string_ref_count = static_cast<std::uint64_t>(storage->string_refs.size());
    ctx.edit_target_snapshot = std::move(storage);
    return ctx.edit_target_snapshot->view;
}

std::string build_replacement_statement(const MapEditChange& change,
                                        const ParsedStatement& statement,
                                        const EditableTarget& target) {
    const bool editable_csv_list =
        target.row_kind == "station.list" ||
        target.row_kind == "signal.aspect" ||
        target.row_kind == "structure.model" ||
        target.row_kind == "sound.list" ||
        target.row_kind == "sound3D.list";
    if (!change.replacement_statement.empty() && !editable_csv_list) {
        return change.replacement_statement;
    }
    if (target.row_kind == "structure.model" && target.structure_model) {
        return build_structure_model_statement(change, statement,
                                               *target.structure_model);
    }
    if ((target.row_kind == "sound.list" ||
         target.row_kind == "sound3D.list") && target.sound_list) {
        return build_sound_list_statement(change, statement, *target.sound_list);
    }
    if (target.row_kind == "structure.put" && target.structure_put) {
        return build_structure_put_statement(change, statement, *target.structure_put, false);
    }
    if (target.row_kind == "structure.between" && target.structure_put) {
        return build_structure_put_statement(change, statement, *target.structure_put, true);
    }
    if (target.row_kind == "station.put" && target.station_put) {
        return build_station_put_statement(change, statement, *target.station_put);
    }
    if (target.row_kind == "station.list" && target.station_list) {
        return build_station_list_statement(change, statement, *target.station_list);
    }
    if (target.row_kind == "signal.aspect" && target.signal_aspect) {
        return build_signal_aspect_statement(change, statement);
    }
    if (target.row_kind == "signal.put" && target.signal_put) {
        return build_signal_put_statement(change, statement, *target.signal_put);
    }
    if (target.row_kind == "repeater" && target.repeater) {
        return build_repeater_statement(change, statement, *target.repeater);
    }
    if (target.row_kind == "irregularity.change" && target.irregularity) {
        return build_irregularity_statement(change, statement, *target.irregularity);
    }
    if (target.row_kind == "curve" && target.curve) {
        return build_curve_statement(change, statement, *target.curve);
    }
    if (target.row_kind == "gradient" && target.gradient) {
        return build_gradient_statement(change, statement, *target.gradient);
    }
    if (target.row_kind == "otherTrack.change" && target.other_track_change) {
        return build_other_track_change_statement(
            change, statement, *target.other_track_change);
    }
    if (target.simple_statement_builder && target.simple_row) {
        return target.simple_statement_builder(change, statement, target.simple_row);
    }
    throw std::runtime_error("unsupported editable target: " + target.row_kind);
}

std::string path_from_single_field_change(const MapEditChange& change,
                                          const char* field_name,
                                          const char* target_name) {
    if (change.field_changes.size() != 1 ||
        change.field_changes.begin()->first != field_name) {
        throw std::runtime_error(
            std::string(target_name) + " edits require exactly one " +
            field_name + " field: " + change.edit_id);
    }
    const std::string new_path = trim_field_copy(change.field_changes.begin()->second);
    if (new_path.empty()) {
        throw std::runtime_error(
            std::string(field_name) + " must not be empty: " + change.edit_id);
    }
    if (new_path.find('\'') != std::string::npos) {
        throw std::runtime_error(
            std::string(field_name) +
            " must be representable as a single-quoted string literal: " + new_path);
    }
    return new_path;
}

std::string build_path_statement(const MapEditChange& change,
                                 const ParsedStatement& statement,
                                 const std::string& new_path,
                                 const char* target_name) {
    const std::string replacement_arguments = "'" + new_path + "'";
    // Replace only the argument slice inside the original statement text so
    // the statement kind spelling and surrounding spacing survive unchanged.
    // raw_arguments is a trimmed contiguous slice of raw_text, so the search
    // succeeds for every parseable source statement with a path argument.
    if (!statement.raw_arguments.empty()) {
        const size_t position = statement.raw_text.find(statement.raw_arguments);
        if (position != std::string::npos) {
            return statement.raw_text.substr(0, position) + replacement_arguments +
                statement.raw_text.substr(position + statement.raw_arguments.size());
        }
    }
    // Fallback for statements without a recoverable argument slice: rebuild
    // from the original kind spelling while preserving the official form.
    size_t kind_end = 0;
    while (kind_end < statement.raw_text.size() &&
           (statement.raw_text[kind_end] == ' ' || statement.raw_text[kind_end] == '\t')) {
        ++kind_end;
    }
    const size_t kind_start = kind_end;
    while (kind_end < statement.raw_text.size() &&
           statement.raw_text[kind_end] != ' ' &&
           statement.raw_text[kind_end] != '\t' &&
           statement.raw_text[kind_end] != ';') {
        ++kind_end;
    }
    if (kind_end == kind_start) {
        throw std::runtime_error(
            std::string("failed to rewrite the ") + target_name +
            " statement: " + change.edit_id);
    }
    return statement.raw_text.substr(kind_start, kind_end - kind_start) +
        " " + replacement_arguments + ";";
}

std::string include_path_from_change(const MapEditChange& change) {
    return path_from_single_field_change(change, "includePath", "include");
}

std::string resource_list_path_from_change(const MapEditChange& change) {
    return path_from_single_field_change(
        change, "resourceListPath", "resource list");
}

void validate_resource_list_insert_fields(const MapEditChange& change) {
    const auto kind = change.field_changes.find("resourceListKind");
    const auto path = change.field_changes.find("resourceListPath");
    if (change.field_changes.size() != 2 || kind == change.field_changes.end() ||
        path == change.field_changes.end()) {
        throw std::runtime_error(
            "resource list insert requires resourceListKind and resourceListPath fields: " +
            change.edit_id);
    }
}

const ResourceListEditSpec* resource_list_insert_spec_from_change(const MapEditChange& change) {
    validate_resource_list_insert_fields(change);
    const std::string kind = ascii_lower(trim_field_copy(
        change.field_changes.find("resourceListKind")->second));
    if (kind == "station") return resource_list_edit_spec_for_kind(ResourceListLoadKind::Station);
    if (kind == "structure") return resource_list_edit_spec_for_kind(ResourceListLoadKind::Structure);
    if (kind == "signal") return resource_list_edit_spec_for_kind(ResourceListLoadKind::Signal);
    if (kind == "sound") return resource_list_edit_spec_for_kind(ResourceListLoadKind::Sound);
    if (kind == "sound3d") return resource_list_edit_spec_for_kind(ResourceListLoadKind::Sound3D);
    throw std::runtime_error("unsupported resourceListKind for insert: " + kind);
}

std::string resource_list_insert_path_from_change(const MapEditChange& change) {
    validate_resource_list_insert_fields(change);
    const std::string path = trim_field_copy(
        change.field_changes.find("resourceListPath")->second);
    if (path.empty()) {
        throw std::runtime_error("resourceListPath must not be empty: " + change.edit_id);
    }
    if (path.find('\'') != std::string::npos) {
        throw std::runtime_error(
            "resourceListPath must be representable as a single-quoted string literal: " + path);
    }
    return path;
}

const char* resource_list_insert_statement_kind(const ResourceListEditSpec& spec) {
    switch (spec.kind) {
    case ResourceListLoadKind::Station: return "Station.Load";
    case ResourceListLoadKind::Structure: return "Structure.Load";
    case ResourceListLoadKind::Signal: return "Signal.Load";
    case ResourceListLoadKind::Sound: return "Sound.Load";
    case ResourceListLoadKind::Sound3D: return "Sound3D.Load";
    }
    throw std::runtime_error("resource list insert has an unsupported kind");
}

std::string build_include_statement(const MapEditChange& change,
                                    const ParsedStatement& statement) {
    return build_path_statement(
        change, statement, include_path_from_change(change), "include");
}

std::string build_resource_list_load_statement(const MapEditChange& change,
                                               const ParsedStatement& statement) {
    return build_path_statement(
        change, statement, resource_list_path_from_change(change),
        "resource list");
}

void validate_resource_list_header(MapContext& ctx,
                                   const ParsedStatement& statement,
                                   const MapEditChange& change) {
    const ResourceListEditSpec* spec =
        resource_list_edit_spec_for_statement(statement.statement_kind);
    if (!spec) {
        throw std::runtime_error(
            "resource list edit has an unsupported source statement: " +
            statement.statement_kind);
    }
    const std::string path = resource_list_path_from_change(change);
    try {
        (void)load_header_text(
            ctx, join_path(ctx.rootpath, path), spec->header,
            spec->minimum_version);
    } catch (const std::exception& e) {
        throw std::runtime_error(
            std::string("resource-list-header-mismatch:") + spec->error_key +
            ": " + e.what());
    }
}

void validate_resource_list_insert_header(MapContext& ctx, const MapEditChange& change) {
    const ResourceListEditSpec* spec = resource_list_insert_spec_from_change(change);
    const std::string path = resource_list_insert_path_from_change(change);
    try {
        (void)load_header_text(
            ctx, join_path(ctx.rootpath, path), spec->header, spec->minimum_version);
    } catch (const std::exception& e) {
        throw std::runtime_error(
            std::string("resource-list-header-mismatch:") + spec->error_key +
            ": " + e.what());
    }
}

std::string insert_method_or_default(const MapEditChange& change, const char* fallback) {
    auto it = change.field_changes.find("method");
    return it == change.field_changes.end() ? std::string(fallback) : trim_field_copy(it->second);
}

void validate_insert_field_names(
    const MapEditChange& change,
    std::initializer_list<const char*> allowed_fields,
    bool allow_section_values = false) {
    for (const auto& field : change.field_changes) {
        const bool allowed = std::any_of(
            allowed_fields.begin(), allowed_fields.end(),
            [&](const char* name) { return field.first == name; });
        if (!allowed && (!allow_section_values ||
                         field.first.rfind("values.", 0) != 0)) {
            throw std::runtime_error(
                "unsupported insert field " + field.first + " for " + change.row_kind);
        }
    }
}

template <size_t FieldCount>
void validate_resource_list_insert_fields(
    const MapEditChange& change,
    const std::array<const char*, FieldCount>& field_names) {
    for (const auto& field : change.field_changes) {
        const bool known = std::any_of(
            field_names.begin(), field_names.end(),
            [&](const char* name) { return field.first == name; });
        if (!known) {
            throw std::runtime_error(
                "unsupported insert field " + field.first + " for " +
                change.row_kind);
        }
    }
    for (const char* name : field_names) {
        if (change.field_changes.find(name) == change.field_changes.end()) {
            throw std::runtime_error(
                "resource-list insert is missing field " + std::string(name));
        }
    }
}

void validate_signal_aspect_insert_fields(const MapEditChange& change) {
    constexpr size_t k_primary_structure_key_count = 5;
    const std::optional<size_t> add_glare_count =
        signal_aspect_add_glare_count(change);
    if (add_glare_count &&
        *add_glare_count != k_primary_structure_key_count) {
        throw std::runtime_error(
            "Signal aspect insert glare must contain five structure keys");
    }
    const size_t permitted_structure_key_count =
        k_primary_structure_key_count +
        (add_glare_count ? *add_glare_count : 0);
    for (const auto& field : change.field_changes) {
        if (field.first == "signalAspectKey" || field.first == "addGlare") {
            continue;
        }
        size_t index = 0;
        if (!parse_signal_aspect_structure_key_field_name(field.first, index) ||
            index >= permitted_structure_key_count) {
            throw std::runtime_error(
                "unsupported Signal aspect insert field: " + field.first);
        }
    }
    const auto aspect_key = change.field_changes.find("signalAspectKey");
    if (aspect_key == change.field_changes.end()) {
        throw std::runtime_error("Signal aspect insert is missing signalAspectKey");
    }
    (void)normalized_signal_aspect_edit_value(
        aspect_key->second, "signalAspectKey", true);

    bool has_main_structure_key = false;
    bool has_glare_structure_key = false;
    for (size_t index = 0; index < permitted_structure_key_count; ++index) {
        const std::string field_name = signal_aspect_structure_key_field_name(index);
        const auto field = change.field_changes.find(field_name);
        if (field == change.field_changes.end()) {
            throw std::runtime_error(
                "Signal aspect insert is missing field: " + field_name);
        }
        const std::string value = normalized_signal_aspect_edit_value(
            field->second, field_name, false);
        if (index < k_primary_structure_key_count) {
            has_main_structure_key = has_main_structure_key || !value.empty();
        } else {
            has_glare_structure_key = has_glare_structure_key || !value.empty();
        }
    }
    if (!has_main_structure_key) {
        throw std::runtime_error(
            "Signal aspect insert requires at least one structure key");
    }
    if (add_glare_count && !has_glare_structure_key) {
        throw std::runtime_error(
            "Signal aspect glare requires at least one structure key");
    }
}

void validate_resource_list_content_insert_change(const MapEditChange& change) {
    const ResourceListEditSpec* spec =
        resource_list_edit_spec_for_content_row_kind(change.row_kind);
    if (!spec) return;
    switch (spec->kind) {
    case ResourceListLoadKind::Station:
        validate_resource_list_insert_fields(change, k_station_list_field_names);
        return;
    case ResourceListLoadKind::Structure:
        validate_resource_list_insert_fields(change, k_structure_list_field_names);
        return;
    case ResourceListLoadKind::Signal:
        validate_signal_aspect_insert_fields(change);
        return;
    case ResourceListLoadKind::Sound:
    case ResourceListLoadKind::Sound3D:
        validate_resource_list_insert_fields(change, k_sound_list_field_names);
        return;
    }
}

void validate_insert_method(const MapEditChange& change,
                            const char* fallback,
                            std::initializer_list<const char*> allowed_methods) {
    const std::string method = insert_method_or_default(change, fallback);
    const bool allowed = std::any_of(
        allowed_methods.begin(), allowed_methods.end(),
        [&](const char* name) { return method == name; });
    if (!allowed) {
        throw std::runtime_error(
            "unsupported insert method " + method + " for " + change.row_kind);
    }
}

bool other_track_insert_parameter_index(std::string_view field_name, size_t& index) {
    constexpr std::string_view prefix = "parameter";
    if (field_name.rfind(prefix, 0) != 0 || field_name.size() == prefix.size()) {
        return false;
    }
    const std::string_view suffix = field_name.substr(prefix.size());
    if (suffix.size() > 1 && suffix.front() == '0') return false;
    size_t value = 0;
    for (const char ch : suffix) {
        if (ch < '0' || ch > '9') return false;
        const size_t digit = static_cast<size_t>(ch - '0');
        if (value > (std::numeric_limits<size_t>::max() - digit) / 10) {
            return false;
        }
        value = value * 10 + digit;
    }
    index = value;
    return true;
}

size_t other_track_insert_parameter_count(const MapEditChange& change) {
    size_t count = 0;
    for (const auto& field : change.field_changes) {
        if (field.first == "distance" || field.first == "method" ||
            field.first == "trackKey") {
            continue;
        }
        size_t index = 0;
        if (!other_track_insert_parameter_index(field.first, index)) {
            throw std::runtime_error(
                "unsupported insert field " + field.first + " for " + change.row_kind);
        }
        if (index == std::numeric_limits<size_t>::max()) {
            throw std::runtime_error("other-track insert parameter index is too large");
        }
        count = std::max(count, index + 1);
        (void)normalized_number_arg(field.second);
    }
    for (size_t index = 0; index < count; ++index) {
        if (change.field_changes.find("parameter" + std::to_string(index)) ==
            change.field_changes.end()) {
            throw std::runtime_error(
                "other-track insert parameters must be numbered continuously from parameter0");
        }
    }
    return count;
}

void validate_other_track_insert_change(const MapEditChange& change) {
    const std::string method = insert_method_or_default(change, "");
    const size_t parameter_count = other_track_insert_parameter_count(change);
    bool known_method = true;
    bool valid_arity = false;
    if (method == "Track.Position") {
        valid_arity = parameter_count >= 2 && parameter_count <= 4;
    } else if (method == "Track.X.Interpolate" ||
               method == "Track.Y.Interpolate") {
        valid_arity = parameter_count <= 2;
    } else if (method == "Track.Cant.SetGauge" ||
               method == "Track.Cant.SetCenter" ||
               method == "Track.Cant.SetFunction" ||
               method == "Track.Cant.Begin") {
        valid_arity = parameter_count == 1;
    } else if (method == "Track.Cant.BeginTransition" ||
               method == "Track.Cant.End") {
        valid_arity = parameter_count == 0;
    } else if (method == "Track.Cant.Interpolate") {
        valid_arity = parameter_count <= 1;
    } else {
        known_method = false;
    }
    if (!known_method) {
        throw std::runtime_error(
            "unsupported insert method " + method + " for " + change.row_kind);
    }
    if (!valid_arity) {
        throw std::runtime_error(
            "invalid parameter count for other-track insert method " + method);
    }

    const auto key = change.field_changes.find("trackKey");
    if (key == change.field_changes.end() || trim_field_copy(key->second).empty()) {
        throw std::runtime_error("other-track insert requires a trackKey");
    }
    try {
        (void)track_key_from_display_text(key->second);
    } catch (const std::exception& e) {
        throw std::runtime_error(
            std::string("other-track insert has an invalid trackKey: ") + e.what());
    }

    if (method == "Track.Cant.SetFunction") {
        double function = 0.0;
        if (!parse_edit_number(change.field_changes.at("parameter0"), function) ||
            (function != 0.0 && function != 1.0)) {
            throw std::runtime_error(
                "Track.Cant.SetFunction insert requires id 0 or 1");
        }
    }
}

void validate_own_track_insert_change(const MapEditChange& change) {
    const std::string method = insert_method_or_default(change, "");
    if (change.row_kind == "curve") {
        validate_insert_field_names(change, {"distance", "method", "radius", "cant"});
        validate_insert_method(change, "", {
            "Curve.BeginTransition", "Curve.Begin", "Curve.Change", "Curve.End",
        });
        if (method == "Curve.BeginTransition" || method == "Curve.End") {
            if (change.field_changes.find("radius") != change.field_changes.end() ||
                change.field_changes.find("cant") != change.field_changes.end()) {
                throw std::runtime_error(method + " insert does not accept radius or cant");
            }
            return;
        }
        (void)required_numeric_value_field(change, "radius", Value::null());
        if (method == "Curve.Change") {
            if (change.field_changes.find("cant") != change.field_changes.end()) {
                throw std::runtime_error("Curve.Change insert accepts exactly one radius argument");
            }
            return;
        }
        if (change.field_changes.find("cant") != change.field_changes.end()) {
            (void)required_numeric_value_field(change, "cant", Value::null());
        }
        return;
    }

    validate_insert_field_names(change, {"distance", "method", "gradient"});
    validate_insert_method(change, "", {
        "Gradient.BeginTransition", "Gradient.Begin", "Gradient.End",
    });
    if (method == "Gradient.BeginTransition" || method == "Gradient.End") {
        if (change.field_changes.find("gradient") != change.field_changes.end()) {
            throw std::runtime_error(method + " insert does not accept a gradient argument");
        }
        return;
    }
    (void)required_numeric_value_field(change, "gradient", Value::null());
}

void validate_insert_change(const MapEditChange& change) {
    if (change.row_kind.empty()) {
        throw std::runtime_error("insert edit is missing its row kind");
    }
    if (!change.replacement_statement.empty()) {
        throw std::runtime_error(
            "insert replacementStatement is unsupported; use structured fields: " +
            change.edit_id);
    }
    const ResourceListEditSpec* content_spec =
        resource_list_edit_spec_for_content_row_kind(change.row_kind);
    if (!change.insert_before_edit_id.empty() && !content_spec) {
        throw std::runtime_error(
            "insertBeforeEditId is unsupported for insert edits: " +
            change.edit_id);
    }

    const std::string& row_kind = change.row_kind;
    if (row_kind == "include") {
        (void)include_path_from_change(change);
        return;
    }
    if (row_kind == "resourceList.load") {
        (void)resource_list_insert_spec_from_change(change);
        (void)resource_list_insert_path_from_change(change);
        return;
    }
    if (content_spec) {
        validate_resource_list_content_insert_change(change);
        return;
    }
    if (change.field_changes.find("distance") == change.field_changes.end()) {
        throw std::runtime_error(
            "insert edit is missing its distance field: " + change.edit_id);
    }
    if (row_kind == "structure.put") {
        validate_insert_field_names(change,
                                    {"distance", "method", "structureKey", "trackKey",
                                     "x", "y", "z", "rx", "ry", "rz", "tilt", "span"});
        validate_insert_method(change, "Put", {"Put", "Put0"});
    } else if (row_kind == "repeater") {
        validate_repeater_edit_fields(change);
        validate_insert_method(change, "Begin", {"Begin", "Begin0", "End"});
        const std::string method = insert_method_or_default(change, "Begin");
        if (method == "End") {
            validate_insert_field_names(
                change, {"distance", "method", "repeaterKey"});
            if (change.confirm_repeater_change_point) {
                throw std::runtime_error(
                    "Repeater.End insert cannot confirm a change point");
            }
            return;
        }
        const RepeaterStructureKeyEdit structure_keys =
            parse_repeater_structure_key_edit(change);
        if (!structure_keys.changed || structure_keys.values.empty()) {
            throw std::runtime_error("Repeater insert requires at least one structure key");
        }
        if (method == "Begin0") {
            reject_repeater_position_fields(
                change, "Repeater.Begin0 insert cannot include coordinate offsets");
        }
    } else if (row_kind == "structure.between") {
        validate_insert_field_names(change,
                                    {"distance", "structureKey", "trackKey1", "trackKey2", "flag"});
    } else if (row_kind == "station.put") {
        validate_insert_field_names(change,
                                    {"distance", "stationKey", "door", "margin1", "margin2"});
    } else if (row_kind == "signal.put") {
        validate_insert_field_names(change,
                                    {"distance", "signalAspectKey", "section", "trackKey",
                                     "x", "y", "z", "rx", "ry", "rz", "tilt", "span"});
    } else if (row_kind == "otherTrack.change") {
        validate_other_track_insert_change(change);
    } else if (row_kind == "curve" || row_kind == "gradient") {
        validate_own_track_insert_change(change);
    } else if (row_kind == "irregularity.change") {
        validate_insert_field_names(change,
                                    {"distance", "x", "y", "r", "lx", "ly", "lr"});
    } else if (row_kind == "beacon.put") {
        validate_insert_field_names(change,
                                    {"distance", "type", "section", "sendData"});
    } else if (row_kind == "mapSound.play") {
        validate_insert_field_names(change, {"distance", "soundKey"});
    } else if (row_kind == "mapSound3D.put") {
        validate_insert_field_names(change, {"distance", "soundKey", "x", "y"});
    } else if (row_kind == "rollingNoise.change" ||
               row_kind == "flangeNoise.change" ||
               row_kind == "jointNoise.play") {
        validate_insert_field_names(change, {"distance", "index"});
    } else if (row_kind == "background.change") {
        validate_insert_field_names(change, {"distance", "structureKey"});
    } else if (row_kind == "adhesion.change") {
        validate_insert_field_names(change, {"distance", "a", "b", "c"});
    } else if (row_kind == "cabIlluminance.change") {
        validate_insert_field_names(change, {"distance", "method", "value"});
        validate_insert_method(change, "Set", {"Set", "Interpolate"});
    } else if (row_kind == "fog.change") {
        validate_insert_field_names(change,
                                    {"distance", "method", "density", "red", "green", "blue"});
        validate_insert_method(change, "Set", {"Set", "Interpolate"});
    } else if (row_kind == "drawDistance.change") {
        validate_insert_field_names(change, {"distance", "value"});
    } else if (row_kind == "speedlimit") {
        validate_insert_field_names(change, {"distance", "method", "speed"});
        validate_insert_method(change, "Begin", {"Begin", "End"});
    } else if (row_kind == "section.begin") {
        validate_insert_field_names(change, {"distance", "method"}, true);
        validate_insert_method(change, "Begin", {"Begin", "BeginNew"});
    } else if (row_kind == "section.speedLimit") {
        validate_insert_field_names(change, {"distance", "method"}, true);
        validate_insert_method(change, "SetSpeedLimit", {"SetSpeedLimit", "Signal.SpeedLimit"});
    } else {
        throw std::runtime_error("unsupported insert row kind: " + row_kind);
    }
}

std::string insert_required_number(const MapEditChange& change, const char* key) {
    return required_numeric_value_field(change, key, Value::null());
}

std::string insert_required_key(const MapEditChange& change, const char* key) {
    return quoted_bve_string(required_string_field(change, key, ""));
}

std::string insert_required_track_key(const MapEditChange& change, const char* key) {
    const std::string value = track_key_field_as_bve_arg(change, key, Value::null());
    if (value.empty()) {
        throw std::runtime_error(std::string("required edit field is empty: ") + key);
    }
    return value;
}

template <size_t FieldCount, typename Normalize>
std::string build_resource_list_insert_csv(
    const MapEditChange& change,
    const std::array<const char*, FieldCount>& field_names,
    Normalize&& normalize) {
    std::ostringstream out;
    for (size_t index = 0; index < FieldCount; ++index) {
        if (index != 0) out << ",";
        const auto field = change.field_changes.find(field_names[index]);
        if (field == change.field_changes.end()) {
            throw std::runtime_error(
                "resource-list insert is missing field " +
                std::string(field_names[index]));
        }
        out << csv_field(normalize(field->second, index));
    }
    return out.str();
}

std::string build_resource_list_content_insert_statement(
    const MapEditChange& change,
    std::string_view inserted_newline) {
    const ResourceListEditSpec* spec =
        resource_list_edit_spec_for_content_row_kind(change.row_kind);
    if (!spec) {
        throw std::runtime_error(
            "unsupported resource-list insert row kind: " + change.row_kind);
    }
    switch (spec->kind) {
    case ResourceListLoadKind::Station:
        return build_resource_list_insert_csv(
            change, k_station_list_field_names,
            [](const std::string& value, size_t field_index) {
                return normalized_station_list_edit_value(value, field_index);
            });
    case ResourceListLoadKind::Structure:
        return build_resource_list_insert_csv(
            change, k_structure_list_field_names,
            [](const std::string& value, size_t field_index) {
                return normalized_resource_list_edit_value(
                    value, k_structure_list_field_names[field_index]);
            });
    case ResourceListLoadKind::Sound:
    case ResourceListLoadKind::Sound3D:
        return build_resource_list_insert_csv(
            change, k_sound_list_field_names,
            [](const std::string& value, size_t field_index) {
                return field_index == 2
                    ? normalized_sound_buffer_count_edit_value(value)
                    : normalized_resource_list_edit_value(
                        value, k_sound_list_field_names[field_index]);
            });
    case ResourceListLoadKind::Signal:
        break;
    }

    constexpr size_t k_primary_structure_key_count = 5;
    std::ostringstream out;
    const auto aspect_key = change.field_changes.find("signalAspectKey");
    if (aspect_key == change.field_changes.end()) {
        throw std::runtime_error("Signal aspect insert is missing signalAspectKey");
    }
    out << csv_field(normalized_signal_aspect_edit_value(
        aspect_key->second, "signalAspectKey", true));
    for (size_t index = 0; index < k_primary_structure_key_count; ++index) {
        const std::string field_name = signal_aspect_structure_key_field_name(index);
        const auto field = change.field_changes.find(field_name);
        if (field == change.field_changes.end()) {
            throw std::runtime_error(
                "Signal aspect insert is missing field: " + field_name);
        }
        out << "," << csv_field(normalized_signal_aspect_edit_value(
            field->second, field_name, false));
    }
    const std::optional<size_t> add_glare_count =
        signal_aspect_add_glare_count(change);
    if (!add_glare_count) return out.str();

    out << (inserted_newline.empty() ? std::string_view("\n") : inserted_newline);
    for (size_t index = 0; index < *add_glare_count; ++index) {
        const std::string field_name = signal_aspect_structure_key_field_name(
            k_primary_structure_key_count + index);
        const auto field = change.field_changes.find(field_name);
        if (field == change.field_changes.end()) {
            throw std::runtime_error(
                "Signal aspect insert is missing field: " + field_name);
        }
        out << "," << csv_field(normalized_signal_aspect_edit_value(
            field->second, field_name, false));
    }
    return out.str();
}

// Builds the complete single-statement text for a KV_EDIT_INSERT change from
// its field values alone. There is no existing source row or raw argument
// text to preserve, so every emitted argument comes from the change fields
// with the same BVE quoting/number normalization the update builders use.
std::string build_insert_statement(const MapEditChange& change,
                                   std::string_view inserted_newline) {
    validate_insert_change(change);
    const std::string& row_kind = change.row_kind;
    if (row_kind == "include") {
        return "include '" + include_path_from_change(change) + "';";
    }
    if (row_kind == "resourceList.load") {
        const ResourceListEditSpec* spec = resource_list_insert_spec_from_change(change);
        return std::string(resource_list_insert_statement_kind(*spec)) + "('" +
            resource_list_insert_path_from_change(change) + "');";
    }
    if (resource_list_edit_spec_for_content_row_kind(row_kind)) {
        return build_resource_list_content_insert_statement(
            change, inserted_newline);
    }
    if (row_kind == "repeater") {
        const std::string method = insert_method_or_default(change, "Begin");
        const std::string key = insert_required_key(change, "repeaterKey");
        if (method == "End") {
            return "Repeater[" + key + "].End();";
        }
        const std::string track_key = insert_required_track_key(change, "trackKey");
        const RepeaterStructureKeyEdit structure_keys =
            parse_repeater_structure_key_edit(change);
        std::ostringstream out;
        out << "Repeater[" << key << "]." << method << "(" << track_key;
        if (method == "Begin0") {
            out << "," << insert_required_number(change, "tilt")
                << "," << insert_required_number(change, "span")
                << "," << insert_required_number(change, "interval");
        } else if (method == "Begin") {
            out << "," << insert_required_number(change, "x")
                << "," << insert_required_number(change, "y")
                << "," << insert_required_number(change, "z")
                << "," << insert_required_number(change, "rx")
                << "," << insert_required_number(change, "ry")
                << "," << insert_required_number(change, "rz")
                << "," << insert_required_number(change, "tilt")
                << "," << insert_required_number(change, "span")
                << "," << insert_required_number(change, "interval");
        } else {
            throw std::runtime_error("unsupported Repeater method for insert: " + method);
        }
        for (const std::string& structure_key : structure_keys.values) {
            out << "," << quoted_bve_string(structure_key);
        }
        out << ");";
        return out.str();
    }
    if (row_kind == "structure.put") {
        const std::string method = insert_method_or_default(change, "Put");
        const std::string key = insert_required_key(change, "structureKey");
        const std::string track_key = insert_required_track_key(change, "trackKey");
        if (method == "Put0") {
            return "Structure[" + key + "].Put0(" + track_key + ","
                + insert_required_number(change, "tilt") + ","
                + insert_required_number(change, "span") + ");";
        }
        if (method != "Put") {
            throw std::runtime_error("unsupported Structure placement method for insert: " + method);
        }
        return "Structure[" + key + "].Put(" + track_key + ","
            + insert_required_number(change, "x") + ","
            + insert_required_number(change, "y") + ","
            + insert_required_number(change, "z") + ","
            + insert_required_number(change, "rx") + ","
            + insert_required_number(change, "ry") + ","
            + insert_required_number(change, "rz") + ","
            + insert_required_number(change, "tilt") + ","
            + insert_required_number(change, "span") + ");";
    }
    if (row_kind == "structure.between") {
        const std::string key = insert_required_key(change, "structureKey");
        return "Structure[" + key + "].PutBetween("
            + insert_required_track_key(change, "trackKey1") + ","
            + insert_required_track_key(change, "trackKey2") + ","
            + insert_required_number(change, "flag") + ");";
    }
    if (row_kind == "station.put") {
        return "Station[" + insert_required_key(change, "stationKey") + "].Put("
            + insert_required_number(change, "door") + ","
            + insert_required_number(change, "margin1") + ","
            + insert_required_number(change, "margin2") + ");";
    }
    if (row_kind == "signal.put") {
        const std::string aspect_key = insert_required_key(change, "signalAspectKey");
        return "Signal[" + aspect_key + "].Put("
            + insert_required_number(change, "section") + ","
            + insert_required_track_key(change, "trackKey") + ","
            + insert_required_number(change, "x") + ","
            + insert_required_number(change, "y") + ","
            + insert_required_number(change, "z") + ","
            + insert_required_number(change, "rx") + ","
            + insert_required_number(change, "ry") + ","
            + insert_required_number(change, "rz") + ","
            + insert_required_number(change, "tilt") + ","
            + insert_required_number(change, "span") + ");";
    }
    if (row_kind == "curve") {
        const std::string method = insert_method_or_default(change, "");
        if (method == "Curve.BeginTransition" || method == "Curve.End") {
            return method + "();";
        }
        const std::string radius = insert_required_number(change, "radius");
        if (method == "Curve.Change") {
            return "Curve.Change(" + radius + ");";
        }
        const auto cant = change.field_changes.find("cant");
        return "Curve.Begin(" + radius +
            (cant == change.field_changes.end()
                 ? std::string{}
                 : "," + insert_required_number(change, "cant")) +
            ");";
    }
    if (row_kind == "gradient") {
        const std::string method = insert_method_or_default(change, "");
        if (method == "Gradient.BeginTransition" || method == "Gradient.End") {
            return method + "();";
        }
        return "Gradient.Begin(" + insert_required_number(change, "gradient") + ");";
    }
    if (row_kind == "otherTrack.change") {
        const std::string method = insert_method_or_default(change, "");
        const size_t parameter_count = other_track_insert_parameter_count(change);
        std::string statement = "Track[" + insert_required_track_key(change, "trackKey") +
            "]." + method.substr(std::string_view("Track.").size()) + "(";
        for (size_t index = 0; index < parameter_count; ++index) {
            if (index != 0) statement += ",";
            statement += insert_required_number(
                change, ("parameter" + std::to_string(index)).c_str());
        }
        return statement + ");";
    }
    if (row_kind == "irregularity.change") {
        return "Irregularity.Change("
            + insert_required_number(change, "x") + ","
            + insert_required_number(change, "y") + ","
            + insert_required_number(change, "r") + ","
            + insert_required_number(change, "lx") + ","
            + insert_required_number(change, "ly") + ","
            + insert_required_number(change, "lr") + ");";
    }
    if (row_kind == "beacon.put") {
        return "Beacon.Put("
            + insert_required_number(change, "type") + ","
            + insert_required_number(change, "section") + ","
            + insert_required_number(change, "sendData") + ");";
    }
    if (row_kind == "mapSound.play") {
        return "Sound[" + insert_required_key(change, "soundKey") + "].Play();";
    }
    if (row_kind == "mapSound3D.put") {
        return "Sound3D[" + insert_required_key(change, "soundKey") + "].Put("
            + insert_required_number(change, "x") + ","
            + insert_required_number(change, "y") + ");";
    }
    if (row_kind == "rollingNoise.change") {
        return "RollingNoise.Change(" + insert_required_number(change, "index") + ");";
    }
    if (row_kind == "flangeNoise.change") {
        return "FlangeNoise.Change(" + insert_required_number(change, "index") + ");";
    }
    if (row_kind == "jointNoise.play") {
        return "JointNoise.Play(" + insert_required_number(change, "index") + ");";
    }
    if (row_kind == "background.change") {
        return "Background.Change(" + insert_required_key(change, "structureKey") + ");";
    }
    if (row_kind == "adhesion.change") {
        const std::string a = insert_required_number(change, "a");
        const std::string b = optional_numeric_value_field(change, "b", Value::null());
        const std::string c = optional_numeric_value_field(change, "c", Value::null());
        return "Adhesion.Change(" + build_adhesion_arguments(a, b, c) + ");";
    }
    if (row_kind == "cabIlluminance.change") {
        const std::string method = insert_method_or_default(change, "Set");
        if (method != "Set" && method != "Interpolate") {
            throw std::runtime_error("unsupported CabIlluminance method for insert: " + method);
        }
        const std::string value = optional_numeric_value_field(
            change, "value", Value::null());
        if (value.empty()) return "CabIlluminance.Interpolate();";
        return "CabIlluminance." + method + "(" + value + ");";
    }
    if (row_kind == "fog.change") {
        const std::string method = insert_method_or_default(change, "Set");
        if (method != "Set" && method != "Interpolate") {
            throw std::runtime_error("unsupported Fog method for insert: " + method);
        }
        std::array<std::string, 4> values = {
            optional_numeric_value_field(change, "density", Value::null()),
            optional_numeric_value_field(change, "red", Value::null()),
            optional_numeric_value_field(change, "green", Value::null()),
            optional_numeric_value_field(change, "blue", Value::null()),
        };
        return "Fog." + method + "(" + build_fog_arguments(method, values) + ");";
    }
    if (row_kind == "drawDistance.change") {
        return "DrawDistance.Change(" + insert_required_number(change, "value") + ");";
    }
    if (row_kind == "speedlimit") {
        const std::string method = insert_method_or_default(change, "Begin");
        if (method == "End") return "SpeedLimit.End();";
        if (method != "Begin") {
            throw std::runtime_error("unsupported SpeedLimit method for insert: " + method);
        }
        return "SpeedLimit.Begin(" + insert_required_number(change, "speed") + ");";
    }
    if (row_kind == "section.begin" || row_kind == "section.speedLimit") {
        const SectionValuesEdit values = parse_section_values_edit(change);
        if (!values.changed || values.values.empty()) {
            throw std::runtime_error("Section insert requires at least one parameter");
        }
        const std::string method = row_kind == "section.begin"
            ? insert_method_or_default(change, "Begin")
            : insert_method_or_default(change, "SetSpeedLimit");
        if (row_kind == "section.begin" &&
            method != "Begin" && method != "BeginNew") {
            throw std::runtime_error("unsupported Section method for insert: " + method);
        }
        if (row_kind == "section.speedLimit" &&
            method != "SetSpeedLimit" && method != "Signal.SpeedLimit") {
            throw std::runtime_error("unsupported Section speed-limit method for insert: " + method);
        }
        std::string arguments;
        for (size_t index = 0; index < values.values.size(); ++index) {
            if (index) arguments += ",";
            arguments += normalized_number_arg(values.values[index]);
        }
        const std::string source_method = row_kind == "section.begin"
            ? (method == "BeginNew" ? "Section.BeginNew" : "Section.Begin")
            : (method == "Signal.SpeedLimit" ? "Signal.SpeedLimit"
                                               : "Section.SetSpeedLimit");
        return source_method + "(" + arguments + ");";
    }
    throw std::runtime_error("unsupported insert row kind: " + row_kind);
}

bool exact_distance_value(double a, double b) {
    return a == b;
}

std::string source_context_identity(const MapContext& ctx, const SourceSpan& source) {
    std::string invocation = source_include_invocation_key(ctx, source);
    if (!invocation.empty()) return invocation;
    return include_stack_key(source_include_stack(ctx, source));
}

bool same_statement_context(const MapContext& ctx,
                            const SourceSpan& a,
                            const SourceSpan& b) {
    return a.source_file_index == b.source_file_index &&
           source_context_identity(ctx, a) == source_context_identity(ctx, b);
}

struct DistanceSectionAnalysis {
    std::vector<size_t> anchors;
    size_t origin_position = k_no_source_ref;
    size_t first_position = 0;
    size_t last_position = 0;
    std::string direction = "ambiguous";
    bool resolved = false;
};

struct DistancePlanningIndex {
    using PhysicalKey = std::tuple<std::string, size_t, size_t, size_t>;

    std::map<std::string, std::vector<size_t>> anchors_by_context;
    std::map<std::string, std::vector<size_t>> statements_by_context;
    std::map<PhysicalKey, std::vector<size_t>> statements_by_physical_source;
    std::unordered_map<size_t, DistanceSectionAnalysis> sections_by_statement;

    explicit DistancePlanningIndex(const MapContext& ctx) {
        for (size_t i = 0; i < ctx.parsed_statements.size(); ++i) {
            const ParsedStatement& statement = ctx.parsed_statements[i];
            std::string context_key = key_for_context(ctx, statement.source);
            statements_by_context[context_key].push_back(i);
            if (is_distance_statement(statement)) {
                anchors_by_context[context_key].push_back(i);
            }
            statements_by_physical_source[physical_key(statement)].push_back(i);
        }
        for (auto& entry : anchors_by_context) {
            std::stable_sort(entry.second.begin(), entry.second.end(), [&](size_t lhs, size_t rhs) {
                const SourceSpan& a = ctx.parsed_statements[lhs].source;
                const SourceSpan& b = ctx.parsed_statements[rhs].source;
                if (a.byte_start != b.byte_start) return a.byte_start < b.byte_start;
                return source_start_less(a, b);
            });
        }
        for (auto& entry : statements_by_context) {
            std::stable_sort(entry.second.begin(), entry.second.end(), [&](size_t lhs, size_t rhs) {
                const SourceSpan& a = ctx.parsed_statements[lhs].source;
                const SourceSpan& b = ctx.parsed_statements[rhs].source;
                if (a.byte_start != b.byte_start) return a.byte_start < b.byte_start;
                return source_start_less(a, b);
            });
        }
    }

    static std::string key_for_context(const MapContext& ctx, const SourceSpan& source) {
        return std::to_string(source.source_file_index) + "\n" +
            source_context_identity(ctx, source);
    }

    static PhysicalKey physical_key(const ParsedStatement& statement) {
        return {statement.statement_kind,
                statement.source.source_file_index,
                statement.source.byte_start,
                statement.source.byte_end};
    }

    const std::vector<size_t>& anchors_for(const MapContext& ctx,
                                           const SourceSpan& source) const {
        static const std::vector<size_t> empty;
        auto it = anchors_by_context.find(key_for_context(ctx, source));
        return it == anchors_by_context.end() ? empty : it->second;
    }

    const std::vector<size_t>& physical_counterparts(
        const ParsedStatement& statement) const {
        static const std::vector<size_t> empty;
        auto it = statements_by_physical_source.find(physical_key(statement));
        return it == statements_by_physical_source.end() ? empty : it->second;
    }

    const std::vector<size_t>& statements_for(const MapContext& ctx,
                                              const SourceSpan& source) const {
        static const std::vector<size_t> empty;
        auto it = statements_by_context.find(key_for_context(ctx, source));
        return it == statements_by_context.end() ? empty : it->second;
    }
};

DistanceSectionAnalysis analyze_distance_section(const MapContext& ctx,
                                                 size_t statement_index,
                                                 DistancePlanningIndex& index) {
    auto cached = index.sections_by_statement.find(statement_index);
    if (cached != index.sections_by_statement.end()) return cached->second;
    DistanceSectionAnalysis result;
    if (statement_index >= ctx.parsed_statements.size()) return result;
    const ParsedStatement& origin = ctx.parsed_statements[statement_index];
    result.anchors = index.anchors_for(ctx, origin.source);
    if (result.anchors.empty()) {
        index.sections_by_statement.emplace(statement_index, result);
        return result;
    }

    for (size_t pos = 0; pos < result.anchors.size(); ++pos) {
        const SourceSpan& source = ctx.parsed_statements[result.anchors[pos]].source;
        if (source.byte_start < origin.source.byte_start) result.origin_position = pos;
        else break;
    }
    if (result.origin_position == k_no_source_ref) {
        index.sections_by_statement.emplace(statement_index, result);
        return result;
    }

    size_t plateau_first = result.origin_position;
    size_t plateau_last = result.origin_position;
    const double center = ctx.parsed_statements[result.anchors[result.origin_position]].distance_value;
    while (plateau_first > 0 &&
           exact_distance_value(ctx.parsed_statements[result.anchors[plateau_first - 1]].distance_value,
                                center)) {
        --plateau_first;
    }
    while (plateau_last + 1 < result.anchors.size() &&
           exact_distance_value(ctx.parsed_statements[result.anchors[plateau_last + 1]].distance_value,
                                center)) {
        ++plateau_last;
    }

    size_t inc_first = plateau_first;
    size_t inc_last = plateau_last;
    while (inc_first > 0) {
        double previous = ctx.parsed_statements[result.anchors[inc_first - 1]].distance_value;
        double current = ctx.parsed_statements[result.anchors[inc_first]].distance_value;
        if (!(previous <= current)) break;
        --inc_first;
    }
    while (inc_last + 1 < result.anchors.size()) {
        double current = ctx.parsed_statements[result.anchors[inc_last]].distance_value;
        double next = ctx.parsed_statements[result.anchors[inc_last + 1]].distance_value;
        if (!(current <= next)) break;
        ++inc_last;
    }

    size_t dec_first = plateau_first;
    size_t dec_last = plateau_last;
    while (dec_first > 0) {
        double previous = ctx.parsed_statements[result.anchors[dec_first - 1]].distance_value;
        double current = ctx.parsed_statements[result.anchors[dec_first]].distance_value;
        if (!(previous >= current)) break;
        --dec_first;
    }
    while (dec_last + 1 < result.anchors.size()) {
        double current = ctx.parsed_statements[result.anchors[dec_last]].distance_value;
        double next = ctx.parsed_statements[result.anchors[dec_last + 1]].distance_value;
        if (!(current >= next)) break;
        ++dec_last;
    }

    const bool inc_distinct =
        ctx.parsed_statements[result.anchors[inc_first]].distance_value <
        ctx.parsed_statements[result.anchors[inc_last]].distance_value;
    const bool dec_distinct =
        ctx.parsed_statements[result.anchors[dec_first]].distance_value >
        ctx.parsed_statements[result.anchors[dec_last]].distance_value;
    if (inc_distinct == dec_distinct) {
        // A turn or a flat plateau is ambiguous, but it is still local to the
        // monotonic runs touching the origin. Do not widen the manual choice
        // to every distance statement in the physical file.
        result.first_position = std::min(inc_first, dec_first);
        result.last_position = std::max(inc_last, dec_last);
        if (result.first_position == result.last_position) {
            if (result.first_position > 0) --result.first_position;
            if (result.last_position + 1 < result.anchors.size()) ++result.last_position;
        }
        index.sections_by_statement.emplace(statement_index, result);
        return result;
    }
    result.resolved = true;
    if (inc_distinct) {
        result.first_position = inc_first;
        result.last_position = inc_last;
        result.direction = "increasing";
    } else {
        result.first_position = dec_first;
        result.last_position = dec_last;
        result.direction = "decreasing";
    }
    index.sections_by_statement.emplace(statement_index, result);
    return result;
}

struct DistanceBoundaryPlan {
    size_t before_anchor_position = k_no_source_ref;
    size_t after_anchor_position = k_no_source_ref;
    size_t insert_offset = std::string::npos;
    std::string token;
    int line = 0;
    int column = 0;
    VariableEnvironmentSnapshot variable_environment;
    bool terminal_context_boundary = false;

    bool valid() const {
        return before_anchor_position != k_no_source_ref &&
               (after_anchor_position != k_no_source_ref || terminal_context_boundary) &&
               insert_offset != std::string::npos;
    }
};

std::string distance_boundary_token(const MapContext& ctx,
                                    const ParsedStatement& before,
                                    const ParsedStatement& after) {
    std::ostringstream key;
    key << source_file_key(ctx, before.source) << "\n"
        << source_context_identity(ctx, before.source) << "\n"
        << before.source.byte_start << ":" << before.source.byte_end << "\n"
        << after.source.byte_start << ":" << after.source.byte_end;
    return "distance-gap-" + hex64(stable_hash64(key.str()));
}

DistanceBoundaryPlan boundary_after_anchor(const MapContext& ctx,
                                           const SourcePatch& patch,
                                           const DistanceSectionAnalysis& section,
                                           size_t before_position) {
    DistanceBoundaryPlan boundary;
    if (before_position >= section.anchors.size() ||
        before_position + 1 >= section.anchors.size()) {
        return boundary;
    }
    const ParsedStatement& before = ctx.parsed_statements[section.anchors[before_position]];
    const ParsedStatement& after = ctx.parsed_statements[section.anchors[before_position + 1]];
    boundary.before_anchor_position = before_position;
    boundary.after_anchor_position = before_position + 1;
    const auto after_range = source_range_in_text(patch, after.source);
    boundary.insert_offset = after_range.first;
    size_t line_start = offset_from_line_column(
        patch.text, patch.line_starts, after.source.line, 1);
    if (line_start != std::string::npos && line_start <= after_range.first &&
        std::all_of(patch.text.begin() + static_cast<std::ptrdiff_t>(line_start),
                    patch.text.begin() + static_cast<std::ptrdiff_t>(after_range.first),
                    [](char ch) { return ch == ' ' || ch == '\t' || ch == '\r'; })) {
        boundary.insert_offset = line_start;
        boundary.column = 1;
    } else {
        boundary.column = after.source.column;
    }
    boundary.token = distance_boundary_token(ctx, before, after);
    boundary.line = after.source.line;
    boundary.variable_environment = after.variable_environment;
    return boundary;
}

DistanceBoundaryPlan terminal_boundary_for_last_anchor(
    const MapContext& ctx,
    const SourcePatch& patch,
    const DistanceSectionAnalysis& section,
    size_t before_position,
    const DistancePlanningIndex& index) {
    DistanceBoundaryPlan boundary;
    if (before_position >= section.anchors.size() ||
        before_position + 1 != section.anchors.size()) {
        return boundary;
    }
    const ParsedStatement& before = ctx.parsed_statements[section.anchors[before_position]];
    boundary.before_anchor_position = before_position;
    boundary.terminal_context_boundary = true;
    boundary.variable_environment = before.variable_environment;

    size_t terminal_statement_index = k_no_source_ref;
    for (size_t statement_index : index.statements_for(ctx, before.source)) {
        const ParsedStatement& statement = ctx.parsed_statements[statement_index];
        if (statement.source.byte_start > before.source.byte_end &&
            exact_distance_value(statement.distance_value, before.distance_value)) {
            terminal_statement_index = statement_index;
        }
    }
    if (terminal_statement_index != k_no_source_ref) {
        const ParsedStatement& terminal = ctx.parsed_statements[terminal_statement_index];
        auto range = source_range_in_text(patch, terminal.source);
        size_t line_start = offset_from_line_column(
            patch.text, patch.line_starts, terminal.source.line, 1);
        boundary.insert_offset = range.first;
        if (line_start != std::string::npos && line_start <= range.first &&
            std::all_of(patch.text.begin() + static_cast<std::ptrdiff_t>(line_start),
                        patch.text.begin() + static_cast<std::ptrdiff_t>(range.first),
                        [](char ch) { return ch == ' ' || ch == '\t' || ch == '\r'; })) {
            boundary.insert_offset = line_start;
        }
        boundary.line = terminal.source.line;
        boundary.column = terminal.source.column;
        boundary.variable_environment = terminal.variable_environment;
    } else {
        auto range = source_range_in_text(patch, before.source);
        boundary.insert_offset = range.second;
        boundary.line = before.source.line_end;
        boundary.column = before.source.column_end;
    }
    std::ostringstream token;
    token << source_file_key(ctx, before.source) << "\n"
          << source_context_identity(ctx, before.source) << "\nterminal\n"
          << before.source.byte_start << ":" << before.source.byte_end << "\n"
          << boundary.insert_offset;
    boundary.token = "distance-terminal-" + hex64(stable_hash64(token.str()));
    return boundary;
}

std::set<std::string> referenced_variables(const std::string& expression) {
    std::set<std::string> names;
    bool single_quoted = false;
    bool double_quoted = false;
    for (size_t i = 0; i < expression.size();) {
        char ch = expression[i];
        if (ch == '\'' && !double_quoted) {
            single_quoted = !single_quoted;
            ++i;
            continue;
        }
        if (ch == '"' && !single_quoted) {
            double_quoted = !double_quoted;
            ++i;
            continue;
        }
        if (single_quoted || double_quoted || ch != '$') {
            ++i;
            continue;
        }
        ++i;
        const size_t begin = i;
        while (i < expression.size() &&
               edit_expr_ident_part(static_cast<unsigned char>(expression[i]))) {
            ++i;
        }
        if (i > begin) names.insert(ascii_lower(expression.substr(begin, i - begin)));
    }
    return names;
}

const Value* environment_value(const VariableEnvironmentSnapshot& environment,
                               const std::string& name) {
    if (!environment) return nullptr;
    auto it = environment->find(name);
    return it == environment->end() ? nullptr : &it->second;
}

bool environment_binding_equal(const VariableEnvironmentSnapshot& a,
                               const VariableEnvironmentSnapshot& b,
                               const std::string& name) {
    const Value* av = environment_value(a, name);
    const Value* bv = environment_value(b, name);
    if (!av || !bv) return av == bv;
    return value_equal(*av, *bv);
}

std::string first_environment_mismatch(const VariableEnvironmentSnapshot& origin,
                                       const VariableEnvironmentSnapshot& destination,
                                       const std::set<std::string>& variables) {
    for (const std::string& variable : variables) {
        if (!environment_binding_equal(origin, destination, variable)) return variable;
    }
    return {};
}

std::string assigned_variable_name(const ParsedStatement& statement) {
    if (statement.statement_kind != "Variable.Assign") return std::string{};
    size_t pos = 0;
    while (pos < statement.raw_text.size() &&
           std::isspace(static_cast<unsigned char>(statement.raw_text[pos]))) {
        ++pos;
    }
    if (pos >= statement.raw_text.size() || statement.raw_text[pos] != '$') return std::string{};
    const size_t begin = ++pos;
    while (pos < statement.raw_text.size() &&
           edit_expr_ident_part(static_cast<unsigned char>(statement.raw_text[pos]))) {
        ++pos;
    }
    return pos > begin
        ? ascii_lower(statement.raw_text.substr(begin, pos - begin))
        : std::string{};
}

bool parse_include_invocation_number(const std::string& text, size_t& cursor,
                                     size_t& value) {
    value = 0;
    size_t digits = 0;
    while (cursor < text.size() && text[cursor] >= '0' && text[cursor] <= '9') {
        if (digits >= 20) return false;
        value = value * 10 + static_cast<size_t>(text[cursor] - '0');
        ++cursor;
        ++digits;
    }
    return digits > 0;
}

bool parse_include_invocation_segment(const std::string& text, size_t& cursor,
                                      std::string& segment) {
    size_t length = 0;
    if (!parse_include_invocation_number(text, cursor, length)) return false;
    if (cursor >= text.size() || text[cursor] != ':') return false;
    ++cursor;
    if (length > text.size() - cursor) return false;
    segment.assign(text, cursor, length);
    cursor += length;
    return true;
}

bool include_invocation_chain_contains(const std::string& key,
                                       const std::string& site_source_key,
                                       size_t site_byte_start,
                                       size_t site_byte_end) {
    std::string current = key;
    while (!current.empty()) {
        size_t cursor = 0;
        std::string parent;
        std::string source;
        size_t byte_start = 0;
        size_t byte_end = 0;
        if (!parse_include_invocation_segment(current, cursor, parent)) return false;
        if (cursor >= current.size() || current[cursor] != '|') return false;
        ++cursor;
        if (!parse_include_invocation_segment(current, cursor, source)) return false;
        if (cursor >= current.size() || current[cursor] != '|') return false;
        ++cursor;
        if (!parse_include_invocation_number(current, cursor, byte_start)) return false;
        if (cursor >= current.size() || current[cursor] != ':') return false;
        ++cursor;
        if (!parse_include_invocation_number(current, cursor, byte_end)) return false;
        if (source == site_source_key && byte_start == site_byte_start &&
            byte_end == site_byte_end) {
            return true;
        }
        current = std::move(parent);
    }
    return false;
}

void collect_subtree_element_ids(
    MapContext& baseline,
    const std::vector<bool>& removed_statements,
    std::set<std::string>* deletions) {
    if (!deletions || removed_statements.empty()) return;
    bool any_removed = false;
    for (size_t i = 0; i < removed_statements.size() && !any_removed; ++i) {
        any_removed = removed_statements[i];
    }
    if (!any_removed) return;
    auto exclude_rows = [&](const auto& rows, const char* kind) {
        for (const auto& row : rows) {
            const EditSourceRef& ref = row.edit_ref;
            if (!ref.valid() || ref.statement_index >= removed_statements.size() ||
                !removed_statements[ref.statement_index]) {
                continue;
            }
            deletions->insert(element_edit_id(baseline, ref, kind));
        }
    };
    exclude_rows(baseline.own_track, "own_track");
    exclude_rows(baseline.curves, "curve");
    exclude_rows(baseline.gradients, "gradient");
    exclude_rows(baseline.other_track_changes, "otherTrack.change");
    exclude_rows(baseline.station_puts, "station.put");
    {
        const auto entries = ordered_station_list_entries(baseline);
        for (size_t i = 0; i < entries.size(); ++i) {
            const StationListEntry& row = *entries[i];
            const EditSourceRef& ref = row.edit_ref;
            if (ref.valid() && ref.statement_index < removed_statements.size() &&
                removed_statements[ref.statement_index]) {
                deletions->insert(element_edit_id(baseline, ref, "station.list"));
            }
        }
    }
    exclude_rows(baseline.structure_loads, "structure.load");
    exclude_rows(baseline.structure_puts, "structure.put");
    exclude_rows(baseline.structure_betweens, "structure.between");
    exclude_rows(baseline.structure_models, "structure.model");
    exclude_rows(baseline.other_trains, "otherTrain.definition");
    exclude_rows(baseline.other_train_structure_keys, "otherTrain.structureKey");
    exclude_rows(baseline.other_train_sound_3d_keys, "otherTrain.sound3DKey");
    exclude_rows(baseline.other_train_enables, "otherTrain.enable");
    exclude_rows(baseline.other_train_stops, "otherTrain.stop");
    exclude_rows(baseline.section_begins, "section.begin");
    exclude_rows(baseline.section_speed_limits, "section.speedLimit");
    exclude_rows(baseline.signal_aspects, "signal.aspect");
    exclude_rows(baseline.signal_puts, "signal.put");
    exclude_rows(baseline.beacons, "beacon.put");
    exclude_rows(baseline.pretrains, "preTrain.pass");
    {
        size_t sound_index = 0;
        size_t sound_3d_index = 0;
        for (const SoundListEntry& row : baseline.sound_list) {
            const char* kind = row.is_3d ? "sound3D.list" : "sound.list";
            if (row.is_3d) ++sound_3d_index; else ++sound_index;
            const EditSourceRef& ref = row.edit_ref;
            if (ref.valid() && ref.statement_index < removed_statements.size() &&
                removed_statements[ref.statement_index]) {
                deletions->insert(element_edit_id(baseline, ref, kind));
            }
        }
    }
    exclude_rows(baseline.map_sounds, "mapSound.play");
    exclude_rows(baseline.map_sound_3d, "mapSound3D.put");
    exclude_rows(baseline.rolling_noises, "rollingNoise.change");
    exclude_rows(baseline.flange_noises, "flangeNoise.change");
    exclude_rows(baseline.joint_noises, "jointNoise.play");
    exclude_rows(baseline.repeaters, "repeater");
    exclude_rows(baseline.irregularities, "irregularity.change");
    exclude_rows(baseline.backgrounds, "background.change");
    exclude_rows(baseline.adhesions, "adhesion.change");
    exclude_rows(baseline.cab_illuminance, "cabIlluminance.change");
    exclude_rows(baseline.fogs, "fog.change");
    exclude_rows(baseline.legacy_fogs, "legacyFog.change");
    exclude_rows(baseline.draw_distances, "drawDistance.change");
    exclude_rows(baseline.speedlimits, "speedlimit");
}

void collect_include_subtree_removals(
    MapContext& baseline,
    const std::vector<size_t>& deleted_include_statements,
    std::vector<bool>& removed_statements,
    std::set<std::string>* deletions) {
    struct DeletedCallSite {
        std::string source_key;
        size_t byte_start = 0;
        size_t byte_end = 0;
    };
    std::vector<DeletedCallSite> sites;
    sites.reserve(deleted_include_statements.size());
    for (size_t statement_index : deleted_include_statements) {
        if (statement_index >= baseline.parsed_statements.size()) continue;
        const ParsedStatement& statement =
            baseline.parsed_statements[statement_index];
        DeletedCallSite site;
        site.source_key = source_file_key(baseline, statement.source);
        site.byte_start = statement.source.byte_start;
        site.byte_end = statement.source.byte_end;
        sites.push_back(std::move(site));
    }
    removed_statements.assign(baseline.parsed_statements.size(), false);
    for (size_t i = 0; i < baseline.parsed_statements.size(); ++i) {
        const ParsedStatement& statement = baseline.parsed_statements[i];
        const std::string& key =
            source_include_invocation_key(baseline, statement.source);
        for (const DeletedCallSite& site : sites) {
            if (include_invocation_chain_contains(key, site.source_key,
                                                  site.byte_start,
                                                  site.byte_end)) {
                removed_statements[i] = true;
                break;
            }
        }
    }
    collect_subtree_element_ids(baseline, removed_statements, deletions);
}

std::string first_multivalued_variable(const MapContext& ctx,
                                       const DistanceSectionAnalysis& section,
                                       const DistancePlanningIndex& distance_index,
                                       const std::set<std::string>& variables) {
    if (section.anchors.empty()) return {};
    const ParsedStatement& first_anchor = ctx.parsed_statements[
        section.anchors[std::min(section.first_position, section.anchors.size() - 1)]];
    const size_t begin_offset = first_anchor.source.byte_start;
    size_t end_offset = std::numeric_limits<size_t>::max();
    if (section.last_position + 1 < section.anchors.size()) {
        end_offset = ctx.parsed_statements[
            section.anchors[section.last_position + 1]].source.byte_start;
    }

    for (const std::string& variable : variables) {
        const Value* first = nullptr;
        bool first_missing = false;
        bool initialized = false;
        auto observe = [&](const Value* value) {
            if (!initialized) {
                first = value;
                first_missing = value == nullptr;
                initialized = true;
                return false;
            }
            return (value == nullptr) != first_missing ||
                (value && first && !value_equal(*value, *first));
        };

        for (size_t statement_index :
             distance_index.statements_for(ctx, first_anchor.source)) {
            const ParsedStatement& statement = ctx.parsed_statements[statement_index];
            if (statement.source.byte_start < begin_offset ||
                statement.source.byte_start > end_offset) {
                continue;
            }
            if (observe(environment_value(statement.variable_environment, variable))) {
                return variable;
            }
            if (assigned_variable_name(statement) == variable &&
                !statement.evaluated_values.empty() &&
                observe(&statement.evaluated_values.front())) {
                return variable;
            }
        }
    }
    return {};
}

struct PreparedEdit {
    const MapEditChange* change = nullptr;
    size_t input_ordinal = 0;
    EditableTarget target;
    size_t source_file_index = k_no_source_ref;
    std::pair<size_t, size_t> source_range{};
    std::pair<size_t, size_t> removal_range{};
    std::string source_indent;
    std::string replacement_statement;
    std::string operation;
    bool has_custom_identity_range = false;
    size_t identity_range_begin = 0;
    size_t identity_range_end = 0;
    bool initial_empty_source_insert = false;
    bool moves_distance = false;
    double target_distance = 0.0;
    std::string suggested_distance_expression;
    DistanceSectionAnalysis section;
};

void validate_own_track_transition_insert_pairs(
    const std::vector<PreparedEdit>& prepared, MapEditReport& report) {
    std::vector<const PreparedEdit*> events;
    for (const PreparedEdit& edit : prepared) {
        if (edit.operation != "insert" || !edit.change ||
            (edit.change->row_kind != "curve" && edit.change->row_kind != "gradient")) {
            continue;
        }
        events.push_back(&edit);
    }
    std::stable_sort(events.begin(), events.end(), [](const PreparedEdit* left,
                                                        const PreparedEdit* right) {
        if (left->source_file_index != right->source_file_index) {
            return left->source_file_index < right->source_file_index;
        }
        if (left->source_range.first != right->source_range.first) {
            return left->source_range.first < right->source_range.first;
        }
        return left->input_ordinal < right->input_ordinal;
    });

    const auto same_source_file = [](const PreparedEdit& left, const PreparedEdit& right) {
        return left.source_file_index == right.source_file_index;
    };
    const auto method = [](const PreparedEdit& edit) {
        return insert_method_or_default(*edit.change, "");
    };
    const auto has_cant = [](const PreparedEdit& edit) {
        return edit.change->field_changes.find("cant") != edit.change->field_changes.end();
    };

    const PreparedEdit* curve_transition = nullptr;
    const PreparedEdit* gradient_transition = nullptr;
    const auto consume = [&](const PreparedEdit*& transition, const PreparedEdit& primary,
                             const char* family) {
        if (!transition) return true;
        if (!same_source_file(*transition, primary)) {
            report.blocking_errors.push_back(
                std::string(family) + " BeginTransition and its consuming insert must use "
                "the same target source file");
            return false;
        }
        transition = nullptr;
        return true;
    };
    for (const PreparedEdit* event : events) {
        const std::string event_method = method(*event);
        if (event->change->row_kind == "curve") {
            if (event_method == "Curve.BeginTransition") {
                if (curve_transition) {
                    report.blocking_errors.push_back(
                        "Curve.BeginTransition insert must be followed by one consuming insert");
                    return;
                }
                curve_transition = event;
            } else if (event_method == "Curve.End" ||
                       (event_method == "Curve.Begin" && has_cant(*event))) {
                if (!curve_transition && event_method == "Curve.Begin" && has_cant(*event)) {
                    report.blocking_errors.push_back(
                        "Curve.Begin(radius, cant) insert requires a preceding Curve.BeginTransition");
                    return;
                }
                if (!consume(curve_transition, *event, "Curve")) return;
            } else if (curve_transition) {
                report.blocking_errors.push_back(
                    "Curve.BeginTransition insert must immediately precede its consuming insert");
                return;
            }
            continue;
        }

        if (event_method == "Gradient.BeginTransition") {
            if (gradient_transition) {
                report.blocking_errors.push_back(
                    "Gradient.BeginTransition insert must be followed by one consuming insert");
                return;
            }
            gradient_transition = event;
        } else if (event_method == "Gradient.Begin" || event_method == "Gradient.End") {
            if (!consume(gradient_transition, *event, "Gradient")) return;
        } else if (gradient_transition) {
            report.blocking_errors.push_back(
                "Gradient.BeginTransition insert must immediately precede its consuming insert");
            return;
        }
    }
    if (curve_transition) {
        report.blocking_errors.push_back(
            "Curve.BeginTransition insert requires one consuming insert");
    }
    if (gradient_transition) {
        report.blocking_errors.push_back(
            "Gradient.BeginTransition insert requires one consuming insert");
    }
}

struct DistanceEditGroup {
    std::string key;
    size_t source_file_index = k_no_source_ref;
    double target_distance = 0.0;
    DistanceSectionAnalysis section;
    std::vector<size_t> member_indices;
};

struct ResolvedDistanceGroup {
    const DistanceEditGroup* group = nullptr;
    DistanceBoundaryPlan boundary;
    bool create_distance_block = false;
    std::string distance_expression;
    std::string direction;
};

std::string source_section_key(const MapContext& ctx,
                               const DistanceSectionAnalysis& section,
                               const ParsedStatement& origin,
                               double target_distance) {
    std::ostringstream key;
    key << source_file_key(ctx, origin.source) << "\n"
        << source_context_identity(ctx, origin.source) << "\n"
        << section.direction << "\n" << canonical_number(target_distance) << "\n";
    if (!section.anchors.empty()) {
        const ParsedStatement& first = ctx.parsed_statements[
            section.anchors[std::min(section.first_position, section.anchors.size() - 1)]];
        const ParsedStatement& last = ctx.parsed_statements[
            section.anchors[std::min(section.last_position, section.anchors.size() - 1)]];
        key << trim_field_copy(first.distance_expression) << "="
            << canonical_number(first.distance_value) << "@"
            << first.source.byte_start << ":" << first.source.byte_end << "\n"
            << trim_field_copy(last.distance_expression) << "="
            << canonical_number(last.distance_value) << "@"
            << last.source.byte_start << ":" << last.source.byte_end;
    }
    return "distance-resolution-" + hex64(stable_hash64(key.str()));
}

std::vector<DistanceResolutionBoundary> resolution_boundaries(
    const MapContext& ctx,
    const SourcePatch& patch,
    const DistanceSectionAnalysis& section,
    const std::string& recommended_token) {
    std::vector<DistanceResolutionBoundary> boundaries;
    if (section.anchors.size() < 2) return boundaries;
    const size_t first = std::min(section.first_position, section.anchors.size() - 1);
    const size_t last = std::min(section.last_position, section.anchors.size() - 1);
    if (first >= last) return boundaries;
    for (size_t pos = first;
         pos <= last && pos + 1 < section.anchors.size(); ++pos) {
        DistanceBoundaryPlan boundary = boundary_after_anchor(ctx, patch, section, pos);
        if (!boundary.valid()) continue;
        boundaries.push_back({boundary.token, boundary.line, boundary.column,
                              boundary.token == recommended_token});
    }
    return boundaries;
}

void append_resolution_request(MapContext& ctx,
                               const SourcePatch& patch,
                               const DistanceEditGroup& group,
                               const std::vector<PreparedEdit>& prepared,
                               const std::string& reason,
                               const std::string& variable_name,
                               bool can_confirm_reuse,
                               const std::string& recommended_token,
                               MapEditReport& report) {
    if (group.member_indices.empty()) return;
    const PreparedEdit& first_edit = prepared[group.member_indices.front()];
    const ParsedStatement& origin = ctx.parsed_statements[first_edit.target.statement_index];
    DistanceResolutionRequest request;
    request.resolution_key = group.key;
    request.reason = reason;
    request.source_file = source_file_path(ctx, origin.source);
    request.include_stack = source_include_stack(ctx, origin.source);
    request.target_distance = group.target_distance;
    request.variable_name = variable_name;
    request.suggested_expression = first_edit.suggested_distance_expression;
    request.can_confirm_reuse = can_confirm_reuse;
    for (size_t index : group.member_indices) {
        request.affected_edit_ids.push_back(prepared[index].change->edit_id);
    }
    if (!group.section.anchors.empty()) {
        const size_t first_position = std::min(
            group.section.first_position, group.section.anchors.size() - 1);
        const size_t last_position = std::min(
            std::max(group.section.first_position, group.section.last_position),
            group.section.anchors.size() - 1);
        const ParsedStatement& first = ctx.parsed_statements[
            group.section.anchors[first_position]];
        const ParsedStatement& last = ctx.parsed_statements[
            group.section.anchors[last_position]];
        request.source_section.first_line = first.source.line;
        request.source_section.last_line = last.source.line_end;
    }
    request.source_section.direction = group.section.direction;
    std::string effective_recommended_token = recommended_token;
    if (effective_recommended_token.empty() && group.section.anchors.size() >= 2) {
        double best_score = std::numeric_limits<double>::infinity();
        const size_t first_position = std::min(
            group.section.first_position, group.section.anchors.size() - 1);
        const size_t last_position = std::min(
            group.section.last_position, group.section.anchors.size() - 1);
        for (size_t pos = first_position;
             pos < last_position && pos + 1 < group.section.anchors.size(); ++pos) {
            const double before =
                ctx.parsed_statements[group.section.anchors[pos]].distance_value;
            const double after =
                ctx.parsed_statements[group.section.anchors[pos + 1]].distance_value;
            double score = std::min(std::fabs(group.target_distance - before),
                                    std::fabs(group.target_distance - after));
            if (exact_distance_value(before, group.target_distance)) score = -1.0;
            else if ((before < group.target_distance && group.target_distance < after) ||
                     (before > group.target_distance && group.target_distance > after)) {
                score = 0.0;
            }
            if (score >= best_score) continue;
            DistanceBoundaryPlan candidate = boundary_after_anchor(
                ctx, patch, group.section, pos);
            if (!candidate.valid()) continue;
            best_score = score;
            effective_recommended_token = candidate.token;
        }
    }
    request.allowed_boundaries = resolution_boundaries(
        ctx, patch, group.section, effective_recommended_token);

    std::string nl = patch.record ? newline_text(patch.record->newline) : "\n";
    request.insertion_preview = request.suggested_expression + ";";
    for (size_t index : group.member_indices) {
        request.insertion_preview += nl + prepared[index].replacement_statement;
    }
    report.resolution_requests.push_back(std::move(request));
}

const KvEditReportSnapshot& build_edit_report_snapshot(MapContext& ctx,
                                                       const MapEditReport& report) {
    auto storage = std::make_unique<EditReportSnapshotStorage>();
    const size_t string_hint = report.changed_files.size() + report.warnings.size() +
        report.blocking_errors.size() + report.committed_files.size() * 2 +
        report.committed_rows.size() * 4 + report.previews.size() * 3 + 16;
    storage->string_arena.reserve(std::max<size_t>(4096, string_hint * 48));
    std::unordered_map<std::string, KvStringRef> strings;
    strings.reserve(string_hint);
    auto string_ref = [&](const std::string& text) {
        auto found = strings.find(text);
        if (found != strings.end()) return found->second;
        KvStringRef ref{static_cast<std::uint64_t>(storage->string_arena.size()),
                        static_cast<std::uint64_t>(text.size())};
        storage->string_arena.append(text);
        strings.emplace(text, ref);
        return ref;
    };
    auto append_string_span = [&](const std::vector<std::string>& inputs) {
        KvSpan span{static_cast<std::uint64_t>(storage->string_refs.size()),
                    static_cast<std::uint64_t>(inputs.size())};
        for (const std::string& input : inputs) storage->string_refs.push_back(string_ref(input));
        return span;
    };

    storage->changed_files.reserve(report.changed_files.size());
    for (const std::string& input : report.changed_files) {
        storage->changed_files.push_back(string_ref(input));
    }
    storage->committed_files.reserve(report.committed_files.size());
    for (const MapEditCommittedFile& input : report.committed_files) {
        storage->committed_files.push_back({string_ref(input.file_path),
                                            string_ref(input.source_hash),
                                            static_cast<std::uint64_t>(input.byte_length)});
    }
    storage->committed_rows.reserve(report.committed_rows.size());
    for (const MapEditCommittedRow& input : report.committed_rows) {
        KvEditCommittedRow row{};
        row.row_kind = string_ref(input.row_kind);
        row.row_index = static_cast<std::uint64_t>(input.row_index);
        row.edit_id = string_ref(input.edit_id);
        row.file_path = string_ref(input.file_path);
        row.line = input.line;
        row.column = input.column;
        row.raw_text_preview = string_ref(input.raw_text_preview);
        storage->committed_rows.push_back(row);
    }
    storage->warnings.reserve(report.warnings.size());
    for (const std::string& input : report.warnings) storage->warnings.push_back(string_ref(input));
    storage->blocking_errors.reserve(report.blocking_errors.size());
    for (const std::string& input : report.blocking_errors) {
        storage->blocking_errors.push_back(string_ref(input));
    }
    storage->resolution_requests.reserve(report.resolution_requests.size());
    for (const DistanceResolutionRequest& input : report.resolution_requests) {
        KvDistanceResolutionRow row{};
        row.resolution_key = string_ref(input.resolution_key);
        row.reason = string_ref(input.reason);
        row.source_file = string_ref(input.source_file);
        row.include_stack = append_string_span(input.include_stack);
        row.target_distance = input.target_distance;
        row.variable_name = string_ref(input.variable_name);
        row.affected_edit_ids = append_string_span(input.affected_edit_ids);
        row.suggested_expression = string_ref(input.suggested_expression);
        row.insertion_preview = string_ref(input.insertion_preview);
        row.can_confirm_reuse = input.can_confirm_reuse ? 1u : 0u;
        row.source_section_first_line = input.source_section.first_line;
        row.source_section_last_line = input.source_section.last_line;
        row.source_section_direction = string_ref(input.source_section.direction);
        row.allowed_boundaries.offset = static_cast<std::uint64_t>(storage->boundaries.size());
        row.allowed_boundaries.count = static_cast<std::uint64_t>(input.allowed_boundaries.size());
        storage->boundaries.reserve(storage->boundaries.size() + input.allowed_boundaries.size());
        for (const DistanceResolutionBoundary& boundary : input.allowed_boundaries) {
            KvDistanceBoundaryRow output{};
            output.token = string_ref(boundary.token);
            output.line = boundary.line;
            output.column = boundary.column;
            output.recommended = boundary.recommended ? 1u : 0u;
            storage->boundaries.push_back(output);
        }
        storage->resolution_requests.push_back(row);
    }
    storage->previews.reserve(report.previews.size());
    for (const MapEditPreview& input : report.previews) {
        storage->previews.push_back({string_ref(input.file_path),
                                     string_ref(input.before),
                                     string_ref(input.after)});
    }

    KvEditReportSnapshot& view = storage->view;
    view.version = KV_EDIT_REPORT_SNAPSHOT_VERSION;
    view.ok = report.ok() ? 1u : 0u;
    view.structure_size = sizeof(KvEditReportSnapshot);
    view.report_revision = ++ctx.edit_report_revision;
    view.update_count = report.update_count;
    view.insert_count = report.insert_count;
    view.delete_count = report.delete_count;
    view.full_reparse_ok = report.full_reparse_ok ? 1 : 0;
    view.target_distance_match_count = report.target_distance_match_count;
    view.non_target_changed_count = report.non_target_changed_count;
    view.created_distance_block_count = report.created_distance_block_count;
    view.reused_distance_block_count = report.reused_distance_block_count;
    view.distance_group_count = report.distance_group_count;
    view.validation_fingerprint = string_ref(report.validation_fingerprint);
    view.string_data = storage->string_arena.empty() ? nullptr : storage->string_arena.data();
    view.string_size = static_cast<std::uint64_t>(storage->string_arena.size());
    view.string_refs = storage->string_refs.empty() ? nullptr : storage->string_refs.data();
    view.string_ref_count = static_cast<std::uint64_t>(storage->string_refs.size());
    view.boundaries = storage->boundaries.empty() ? nullptr : storage->boundaries.data();
    view.boundary_count = static_cast<std::uint64_t>(storage->boundaries.size());
    view.changed_files = storage->changed_files.empty() ? nullptr : storage->changed_files.data();
    view.changed_file_count = static_cast<std::uint64_t>(storage->changed_files.size());
    view.committed_files = storage->committed_files.empty() ? nullptr : storage->committed_files.data();
    view.committed_file_count = static_cast<std::uint64_t>(storage->committed_files.size());
    view.committed_rows = storage->committed_rows.empty() ? nullptr : storage->committed_rows.data();
    view.committed_row_count = static_cast<std::uint64_t>(storage->committed_rows.size());
    view.warnings = storage->warnings.empty() ? nullptr : storage->warnings.data();
    view.warning_count = static_cast<std::uint64_t>(storage->warnings.size());
    view.blocking_errors = storage->blocking_errors.empty() ? nullptr : storage->blocking_errors.data();
    view.blocking_error_count = static_cast<std::uint64_t>(storage->blocking_errors.size());
    view.resolution_requests = storage->resolution_requests.empty()
        ? nullptr : storage->resolution_requests.data();
    view.resolution_request_count = static_cast<std::uint64_t>(storage->resolution_requests.size());
    view.preview_snippets = storage->previews.empty() ? nullptr : storage->previews.data();
    view.preview_snippet_count = static_cast<std::uint64_t>(storage->previews.size());
    ctx.edit_report_snapshot = std::move(storage);
    return ctx.edit_report_snapshot->view;
}

struct TransactionalWriteRequest {
    std::filesystem::path path;
    std::string bytes;
    std::string expected_hash;
    std::string new_hash;
};

struct TransactionalWriteOutcome {
    std::vector<std::string> warnings;
};

class TransactionalWriteError : public std::runtime_error {
public:
    TransactionalWriteError(std::string message, bool rollback_complete)
        : std::runtime_error(std::move(message)),
          rollback_complete_(rollback_complete) {}

    bool rollback_complete() const noexcept { return rollback_complete_; }

private:
    bool rollback_complete_ = true;
};

std::string source_file_hash(const std::filesystem::path& path) {
    return hex64(stable_hash64(read_binary_file(path)));
}

std::string native_file_error(const std::string& operation,
                              const std::filesystem::path& path,
                              unsigned long code) {
    return operation + " failed for " + path_to_utf8(path) +
        " (system error " + std::to_string(code) + ")";
}

void remove_transaction_artifact(const std::filesystem::path& path) noexcept {
    if (path.empty()) return;
    std::error_code error;
    std::filesystem::remove(path, error);
}

void write_new_binary_file_for_edit(const std::filesystem::path& path,
                                    const std::string& bytes) {
#if defined(_WIN32)
    HANDLE output = CreateFileW(path.wstring().c_str(), GENERIC_WRITE, 0, nullptr,
                                CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (output == INVALID_HANDLE_VALUE) {
        throw std::runtime_error(native_file_error(
            "temporary file creation", path, GetLastError()));
    }
    bool closed = false;
    try {
        size_t offset = 0;
        while (offset < bytes.size()) {
            const size_t remaining = bytes.size() - offset;
            const DWORD chunk = static_cast<DWORD>(std::min<size_t>(
                remaining, static_cast<size_t>(std::numeric_limits<DWORD>::max())));
            DWORD written = 0;
            if (!WriteFile(output, bytes.data() + offset, chunk, &written, nullptr) ||
                written == 0) {
                throw std::runtime_error(native_file_error(
                    "temporary file write", path, GetLastError()));
            }
            offset += written;
        }
        if (!FlushFileBuffers(output)) {
            throw std::runtime_error(native_file_error(
                "temporary file flush", path, GetLastError()));
        }
        if (!CloseHandle(output)) {
            throw std::runtime_error(native_file_error(
                "temporary file close", path, GetLastError()));
        }
        closed = true;
    } catch (...) {
        if (!closed) CloseHandle(output);
        throw;
    }
#else
    if (std::filesystem::exists(path)) {
        throw std::runtime_error("temporary file already exists: " + path_to_utf8(path));
    }
    std::ofstream output(path, std::ios::binary | std::ios::out | std::ios::trunc);
    if (!output) throw std::runtime_error("File open error: " + path_to_utf8(path));
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    output.flush();
    if (!output) throw std::runtime_error("File write error: " + path_to_utf8(path));
    output.close();
    if (output.fail()) throw std::runtime_error("File close error: " + path_to_utf8(path));
#endif
}

std::filesystem::path transaction_artifact_path(
    const std::filesystem::path& target,
    const std::string& token,
    size_t index,
    const char* suffix) {
    std::string file_name = "." + path_to_utf8(target.filename()) +
        ".komapedit." + token + "." + std::to_string(index) + suffix;
    return target.parent_path() / path_from_utf8(file_name);
}

struct TransactionalWriteEntry {
    TransactionalWriteRequest request;
    std::filesystem::path temporary;
    std::filesystem::path backup;
    std::filesystem::path rollback_discard;
    bool replaced = false;
    bool replacement_failure_uncertain = false;
};

bool replace_staged_file(TransactionalWriteEntry& entry, std::string& error) {
#if defined(_WIN32)
    if (!ReplaceFileW(entry.request.path.wstring().c_str(),
                      entry.temporary.wstring().c_str(),
                      entry.backup.wstring().c_str(), 0, nullptr, nullptr)) {
        const DWORD replace_error = GetLastError();
        error = native_file_error("source file replacement", entry.request.path,
                                  replace_error);
        try {
            const bool target_exists = std::filesystem::exists(entry.request.path);
            const bool backup_exists = std::filesystem::exists(entry.backup);
            if (target_exists) {
                const std::string target_hash = source_file_hash(entry.request.path);
                if ((!entry.request.expected_hash.empty() &&
                     target_hash == entry.request.expected_hash) ||
                    (entry.request.expected_hash.empty() && !backup_exists)) {
                    return false;
                }
                if (backup_exists) {
                    // ReplaceFileW can report a failure after it has already moved
                    // one or both paths. Mark this entry so the transaction's common
                    // rollback path restores the actual backup.
                    entry.replaced = true;
                    return false;
                }
            } else if (backup_exists) {
                if (MoveFileExW(entry.backup.wstring().c_str(),
                                entry.request.path.wstring().c_str(),
                                MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
                    if (entry.request.expected_hash.empty() ||
                        source_file_hash(entry.request.path) == entry.request.expected_hash) {
                        return false;
                    }
                    error += "; restored path has an unexpected hash";
                } else {
                    error += "; immediate backup restore failed with system error " +
                        std::to_string(GetLastError());
                }
            }
        } catch (const std::exception& inspection_error) {
            error += "; replacement-state inspection failed: ";
            error += inspection_error.what();
        }
        entry.replacement_failure_uncertain = true;
        error += "; recovery artifacts: backup=" + path_to_utf8(entry.backup) +
            ", staged=" + path_to_utf8(entry.temporary);
        return false;
    }
#else
    std::error_code filesystem_error;
    const auto permissions = std::filesystem::status(entry.request.path, filesystem_error).permissions();
    if (filesystem_error) {
        error = "source file status failed for " + path_to_utf8(entry.request.path) +
            ": " + filesystem_error.message();
        return false;
    }
    std::filesystem::permissions(entry.temporary, permissions, filesystem_error);
    if (filesystem_error) {
        error = "temporary file permission update failed for " +
            path_to_utf8(entry.temporary) + ": " + filesystem_error.message();
        return false;
    }
    std::filesystem::rename(entry.request.path, entry.backup, filesystem_error);
    if (filesystem_error) {
        error = "source file backup failed for " + path_to_utf8(entry.request.path) +
            ": " + filesystem_error.message();
        return false;
    }
    std::filesystem::rename(entry.temporary, entry.request.path, filesystem_error);
    if (filesystem_error) {
        std::error_code restore_error;
        std::filesystem::rename(entry.backup, entry.request.path, restore_error);
        error = "source file replacement failed for " + path_to_utf8(entry.request.path) +
            ": " + filesystem_error.message();
        if (restore_error) {
            entry.replacement_failure_uncertain = true;
            error += "; immediate restore failed: " + restore_error.message() +
                "; recovery artifacts: backup=" + path_to_utf8(entry.backup) +
                ", staged=" + path_to_utf8(entry.temporary);
        }
        return false;
    }
#endif
    entry.replaced = true;
    return true;
}

bool rollback_staged_file(TransactionalWriteEntry& entry, std::string& error) {
    if (!entry.replaced) return true;
#if defined(_WIN32)
    if (!ReplaceFileW(entry.request.path.wstring().c_str(),
                      entry.backup.wstring().c_str(),
                      entry.rollback_discard.wstring().c_str(), 0, nullptr, nullptr)) {
        error = native_file_error("source file rollback", entry.request.path,
                                  GetLastError());
        return false;
    }
#else
    std::error_code filesystem_error;
    std::filesystem::rename(entry.request.path, entry.rollback_discard, filesystem_error);
    if (filesystem_error) {
        error = "source file rollback staging failed for " +
            path_to_utf8(entry.request.path) + ": " + filesystem_error.message();
        return false;
    }
    std::filesystem::rename(entry.backup, entry.request.path, filesystem_error);
    if (filesystem_error) {
        error = "source file rollback failed for " + path_to_utf8(entry.request.path) +
            ": " + filesystem_error.message();
        return false;
    }
#endif
    entry.replaced = false;
    try {
        if (!entry.request.expected_hash.empty() &&
            source_file_hash(entry.request.path) != entry.request.expected_hash) {
            error = "source file rollback hash mismatch for " +
                path_to_utf8(entry.request.path);
            return false;
        }
    } catch (const std::exception& e) {
        error = e.what();
        return false;
    }
    return true;
}

TransactionalWriteOutcome replace_files_transactionally(
    std::vector<TransactionalWriteRequest> requests) {
    TransactionalWriteOutcome outcome;
    if (requests.empty()) return outcome;

    std::stable_sort(requests.begin(), requests.end(), [](const auto& lhs, const auto& rhs) {
        return ascii_lower(path_to_utf8(lhs.path.lexically_normal())) <
            ascii_lower(path_to_utf8(rhs.path.lexically_normal()));
    });
    for (size_t i = 1; i < requests.size(); ++i) {
        if (ascii_lower(path_to_utf8(requests[i - 1].path.lexically_normal())) ==
            ascii_lower(path_to_utf8(requests[i].path.lexically_normal()))) {
            throw std::runtime_error("duplicate source file in write transaction: " +
                                     path_to_utf8(requests[i].path));
        }
    }

    const std::string token = hex64(stable_hash64(
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "\n" +
#if defined(_WIN32)
        std::to_string(GetCurrentProcessId()) + "\n" +
#else
        std::to_string(std::hash<std::thread::id>{}(std::this_thread::get_id())) + "\n" +
#endif
        path_to_utf8(requests.front().path)));
    std::vector<TransactionalWriteEntry> entries;
    entries.reserve(requests.size());
    for (size_t i = 0; i < requests.size(); ++i) {
        TransactionalWriteEntry entry;
        entry.request = std::move(requests[i]);
        entry.temporary = transaction_artifact_path(entry.request.path, token, i, ".tmp");
        entry.backup = transaction_artifact_path(entry.request.path, token, i, ".bak");
        entry.rollback_discard = transaction_artifact_path(
            entry.request.path, token, i, ".rollback-new");
        if (std::filesystem::exists(entry.temporary) ||
            std::filesystem::exists(entry.backup) ||
            std::filesystem::exists(entry.rollback_discard)) {
            throw std::runtime_error("transaction artifact collision near: " +
                                     path_to_utf8(entry.request.path));
        }
        entries.push_back(std::move(entry));
    }

    try {
        for (TransactionalWriteEntry& entry : entries) {
            write_new_binary_file_for_edit(entry.temporary, entry.request.bytes);
            if (source_file_hash(entry.temporary) != entry.request.new_hash) {
                throw std::runtime_error("staged source hash mismatch for: " +
                                         path_to_utf8(entry.request.path));
            }
        }
        for (const TransactionalWriteEntry& entry : entries) {
            if (!entry.request.expected_hash.empty() &&
                source_file_hash(entry.request.path) != entry.request.expected_hash) {
                throw std::runtime_error("source file changed externally: " +
                                         path_to_utf8(entry.request.path));
            }
        }

        for (TransactionalWriteEntry& entry : entries) {
            if (!entry.request.expected_hash.empty() &&
                source_file_hash(entry.request.path) != entry.request.expected_hash) {
                throw std::runtime_error("source file changed during save: " +
                                         path_to_utf8(entry.request.path));
            }
            std::string replace_error;
            if (!replace_staged_file(entry, replace_error)) {
                throw std::runtime_error(replace_error);
            }
            const std::string target_hash = source_file_hash(entry.request.path);
            const std::string backup_hash = source_file_hash(entry.backup);
            if (target_hash != entry.request.new_hash ||
                (!entry.request.expected_hash.empty() &&
                 backup_hash != entry.request.expected_hash)) {
                throw std::runtime_error("post-replacement hash verification failed for: " +
                                         path_to_utf8(entry.request.path));
            }
        }
    } catch (const std::exception& e) {
        const std::string cause = e.what();
        bool rollback_ok = std::none_of(
            entries.begin(), entries.end(), [](const TransactionalWriteEntry& entry) {
                return entry.replacement_failure_uncertain;
            });
        std::ostringstream rollback_errors;
        for (const TransactionalWriteEntry& entry : entries) {
            if (!entry.replacement_failure_uncertain) continue;
            rollback_errors << " [replacement state uncertain for "
                            << path_to_utf8(entry.request.path) << "; backup="
                            << path_to_utf8(entry.backup) << "; staged="
                            << path_to_utf8(entry.temporary) << "]";
        }
        for (auto it = entries.rbegin(); it != entries.rend(); ++it) {
            if (!it->replaced) continue;
            std::string rollback_error;
            if (!rollback_staged_file(*it, rollback_error)) {
                rollback_ok = false;
                rollback_errors << " [" << rollback_error << "; backup="
                                << path_to_utf8(it->backup) << "; staged="
                                << path_to_utf8(it->temporary) << "]";
            }
        }
        for (TransactionalWriteEntry& entry : entries) {
            if (!entry.replacement_failure_uncertain) {
                remove_transaction_artifact(entry.temporary);
            }
            if (!entry.replacement_failure_uncertain &&
                (rollback_ok || !entry.replaced)) {
                remove_transaction_artifact(entry.backup);
                remove_transaction_artifact(entry.rollback_discard);
            }
        }
        if (rollback_ok) {
            throw TransactionalWriteError(
                "transactional source write failed; all files restored: " + cause,
                true);
        }
        throw TransactionalWriteError(
            "transactional source write failed and rollback was incomplete: " + cause +
            rollback_errors.str(), false);
    }

    for (TransactionalWriteEntry& entry : entries) {
        std::error_code cleanup_error;
        std::filesystem::remove(entry.backup, cleanup_error);
        if (cleanup_error) {
            outcome.warnings.push_back("committed source backup could not be removed: " +
                                       path_to_utf8(entry.backup) + ": " +
                                       cleanup_error.message());
        }
        remove_transaction_artifact(entry.temporary);
        remove_transaction_artifact(entry.rollback_discard);
    }
    return outcome;
}

bool variable_environment_equal(const VariableEnvironment& a, double a_distance,
                                const VariableEnvironment& b, double b_distance) {
    if (a_distance != b_distance || a.size() != b.size()) return false;
    for (const auto& entry : a) {
        auto found = b.find(entry.first);
        if (found == b.end() || !value_equal(entry.second, found->second)) return false;
    }
    return true;
}

std::map<double, std::string> effective_station_position_owners(MapContext& ctx) {
    struct OrderedOwner {
        int order = std::numeric_limits<int>::min();
        std::string edit_id;
    };
    std::map<double, OrderedOwner> ordered;
    for (const StationPut& row : ctx.station_puts) {
        OrderedOwner& owner = ordered[row.distance];
        if (row.order < owner.order) continue;
        owner.order = row.order;
        owner.edit_id = element_edit_id(ctx, row.edit_ref, "station.put");
    }
    std::map<double, std::string> owners;
    for (auto& entry : ordered) owners.emplace(entry.first, std::move(entry.second.edit_id));
    return owners;
}

bool validate_non_target_derived_state(MapContext& baseline,
                                       const MapContext& candidate,
                                       const std::set<std::string>& target_edit_ids,
                                       std::string& error) {
    const std::map<double, std::string> owners = effective_station_position_owners(baseline);
    for (const auto& entry : baseline.station_position) {
        auto owner = owners.find(entry.first);
        if (owner != owners.end() &&
            target_edit_ids.find(owner->second) != target_edit_ids.end()) {
            continue;
        }
        auto after = candidate.station_position.find(entry.first);
        if (after == candidate.station_position.end() || after->second != entry.second) {
            error = "full reparse changed a non-target effective station position at distance " +
                canonical_number(entry.first);
            return false;
        }
    }
    return true;
}

std::unique_ptr<MapContext> parse_report_candidate(MapContext& ctx,
                                                   const MapEditReport& report) {
    SourceTextOverrides overrides = ctx.source_overrides;
    apply_patched_files_to_overrides(overrides, report);
    std::string entry_file_path = ctx.entry_file_path;
    if (entry_file_path.empty()) {
        if (!ctx.include_stack.empty()) entry_file_path = ctx.include_stack.front();
        else if (!ctx.source_files.empty()) entry_file_path = ctx.source_files.front().file_path;
    }
    if (entry_file_path.empty()) throw std::runtime_error("map entry file is not known");
    MapParseOptions options = ctx.parse_options;
    options.collect_edit_metadata = true;
    return parse_map_context(path_from_utf8(entry_file_path), ctx.unit_distance,
                             std::move(overrides), ctx.cp_arbdistribution_explicit,
                             ctx.cp_arbdistribution, options);
}

struct OwnTrackTransitionState {
    std::set<std::pair<std::string, std::string>> pairs;
    std::set<std::string> orphan_transition_ids;
};

OwnTrackTransitionState own_track_transition_state(MapContext& ctx) {
    using namespace own_track_transition_linkage;
    std::vector<Event> events;
    events.reserve(ctx.curves.size() + ctx.gradients.size());
    auto curve_kind = [](const CurveEditRow& row) {
        const std::string method = ascii_lower(row.method);
        if (method == "curve.begintransition") return EventKind::CurveBeginTransition;
        if (method == "curve.begin") return EventKind::CurveBegin;
        if (method == "curve.begincircular") return EventKind::CurveBeginCircular;
        if (method == "curve.end") return EventKind::CurveEnd;
        return EventKind::CurveOther;
    };
    for (size_t i = 0; i < ctx.curves.size(); ++i) {
        events.push_back(Event{i, ctx.curves[i].order,
                               ctx.curves[i].argument_count, curve_kind(ctx.curves[i])});
    }
    const size_t gradient_base = ctx.curves.size();
    for (size_t i = 0; i < ctx.gradients.size(); ++i) {
        const std::string method = ascii_lower(ctx.gradients[i].method);
        const EventKind kind = method == "gradient.begintransition"
            ? EventKind::GradientBeginTransition
            : method == "gradient.end" ? EventKind::GradientEnd
                                          : EventKind::GradientBegin;
        events.push_back(Event{gradient_base + i, ctx.gradients[i].order,
                               ctx.gradients[i].argument_count, kind});
    }
    auto edit_id_at = [&](size_t source_index) {
        if (source_index < gradient_base) {
            return element_edit_id(ctx, ctx.curves[source_index].edit_ref, "curve");
        }
        const size_t index = source_index - gradient_base;
        return element_edit_id(ctx, ctx.gradients[index].edit_ref, "gradient");
    };

    OwnTrackTransitionState state;
    const Linkage linkage = pair_transitions(std::move(events));
    for (const Pair& pair : linkage.pairs) {
        state.pairs.emplace(edit_id_at(pair.transition_source_index),
                            edit_id_at(pair.primary_source_index));
    }
    for (size_t source_index : linkage.orphan_transition_source_indices) {
        state.orphan_transition_ids.insert(edit_id_at(source_index));
    }
    return state;
}

repeater_linkage::Linkage repeater_linkage_state(
    MapContext& ctx, std::vector<std::string>* edit_ids = nullptr) {
    std::vector<repeater_linkage::Event> events;
    events.reserve(ctx.repeaters.size());
    if (edit_ids) edit_ids->assign(ctx.repeaters.size(), std::string{});
    for (size_t index = 0; index < ctx.repeaters.size(); ++index) {
        const RepeaterEvent& row = ctx.repeaters[index];
        repeater_linkage::Event event;
        event.source_index = index;
        event.distance = row.distance;
        event.order = static_cast<double>(row.order);
        event.key = value_to_edit_text(row.repeater_key);
        const std::string method = ascii_lower(row.method);
        if (method == "begin" || method == "begin0") {
            event.kind = repeater_linkage::EventKind::Begin;
        } else if (method == "end") {
            event.kind = repeater_linkage::EventKind::End;
        }
        events.push_back(std::move(event));
        if (edit_ids) {
            (*edit_ids)[index] = element_edit_id(ctx, row.edit_ref, "repeater");
        }
    }
    return repeater_linkage::pair_linkage(std::move(events));
}

std::string repeater_interval_text(const repeater_linkage::Chain& chain) {
    return "[" + canonical_number(chain.begin_distance) + "," +
        (chain.end_distance ? canonical_number(*chain.end_distance)
                            : std::string("+inf")) + ")";
}

void append_repeater_interval_overlap_error(MapEditReport& report,
                                            const std::string& key,
                                            const repeater_linkage::Chain& left,
                                            const repeater_linkage::Chain& right) {
    report.blocking_errors.push_back(
        "Repeater key overlaps another Repeater interval: key=" + key +
        ", intervals=" + repeater_interval_text(left) + " and " +
        repeater_interval_text(right));
}

struct RepeaterNamedInterval {
    repeater_linkage::Chain interval;
    std::string key;
    bool is_candidate = false;
    bool allow_existing_overlap = false;
    bool has_explicit_end = false;
};

bool validate_repeater_interval_conflicts(
    const std::vector<RepeaterNamedInterval>& intervals,
    MapEditReport& report) {
    for (size_t candidate_index = 0; candidate_index < intervals.size(); ++candidate_index) {
        const RepeaterNamedInterval& candidate = intervals[candidate_index];
        if (!candidate.is_candidate) continue;
        for (size_t other_index = 0; other_index < intervals.size(); ++other_index) {
            if (other_index == candidate_index ||
                (intervals[other_index].is_candidate && other_index < candidate_index)) {
                continue;
            }
            const RepeaterNamedInterval& other = intervals[other_index];
            if (candidate.key != other.key ||
                !repeater_linkage::half_open_intervals_overlap(
                    candidate.interval, other.interval)) {
                continue;
            }
            if (candidate.allow_existing_overlap && !other.is_candidate &&
                repeater_linkage::chain_contains_distance(
                    other.interval, candidate.interval.begin_distance)) {
                continue;
            }
            append_repeater_interval_overlap_error(
                report, candidate.key, candidate.interval, other.interval);
            return false;
        }
    }
    return true;
}

std::string canonical_other_track_key(const Value& value) {
    return ascii_lower(track_key_display_text(value));
}

void validate_other_track_key_renames(
    MapContext& ctx, const std::vector<const MapEditChange*>& changes,
    MapEditReport& report) {
    if (ctx.other_track_changes.empty()) return;

    std::vector<std::string> edit_ids(ctx.other_track_changes.size());
    std::map<std::string, size_t> source_index_by_edit_id;
    std::map<std::string, std::vector<size_t>> groups;
    for (size_t index = 0; index < ctx.other_track_changes.size(); ++index) {
        const OtherTrackChange& row = ctx.other_track_changes[index];
        edit_ids[index] = element_edit_id(
            ctx, row.edit_ref, "otherTrack.change");
        if (!edit_ids[index].empty()) {
            source_index_by_edit_id.emplace(edit_ids[index], index);
        }
        groups[canonical_other_track_key(row.track_key)].push_back(index);
    }

    std::map<std::string, const MapEditChange*> changes_by_edit_id;
    for (const MapEditChange* change : changes) {
        if (source_index_by_edit_id.find(change->edit_id) !=
            source_index_by_edit_id.end()) {
            changes_by_edit_id[change->edit_id] = change;
        }
    }

    struct RenameRequest {
        std::string final_key;
        bool changes_source_value = false;
    };
    std::map<std::string, RenameRequest> requests;
    for (const MapEditChange* change : changes) {
        const auto source = source_index_by_edit_id.find(change->edit_id);
        if (source == source_index_by_edit_id.end()) continue;
        const auto key_change = change->field_changes.find("trackKey");
        if (key_change == change->field_changes.end()) continue;

        const std::string operation =
            ascii_lower(change->operation.empty() ? "update" : change->operation);
        if (operation != "update") {
            report.blocking_errors.push_back(
                "Other-track key rename only supports update operations: " +
                change->edit_id);
            return;
        }

        Value parsed;
        try {
            parsed = track_key_from_display_text(key_change->second);
        } catch (const std::exception& e) {
            report.blocking_errors.push_back(
                std::string("Other-track key rename has an invalid trackKey: ") +
                e.what());
            return;
        }
        const std::string final_key = canonical_other_track_key(parsed);
        if (final_key.empty()) {
            report.blocking_errors.push_back(
                "Other-track key rename contains an empty trackKey");
            return;
        }

        const OtherTrackChange& row = ctx.other_track_changes[source->second];
        const std::string original_key = canonical_other_track_key(row.track_key);
        RenameRequest& request = requests[original_key];
        if (!request.final_key.empty() && request.final_key != final_key) {
            report.blocking_errors.push_back(
                "Other-track key rename must use the same trackKey for every "
                "statement in the track");
            return;
        }
        request.final_key = final_key;
        request.changes_source_value = request.changes_source_value ||
            track_key_display_text(parsed) != track_key_display_text(row.track_key);
    }

    for (auto request = requests.begin(); request != requests.end();) {
        if (!request->second.changes_source_value) {
            request = requests.erase(request);
        } else {
            ++request;
        }
    }
    if (requests.empty()) return;

    for (const auto& request : requests) {
        const auto group = groups.find(request.first);
        if (group == groups.end()) {
            report.blocking_errors.push_back(
                "Other-track key rename resolved an invalid track");
            return;
        }
        for (size_t source_index : group->second) {
            const std::string& edit_id = edit_ids[source_index];
            const auto changed = changes_by_edit_id.find(edit_id);
            if (changed == changes_by_edit_id.end()) {
                report.blocking_errors.push_back(
                    "Other-track key rename must update every statement in the "
                    "track: " + edit_id);
                return;
            }
            const std::string operation = ascii_lower(
                changed->second->operation.empty()
                    ? "update" : changed->second->operation);
            if (operation == "delete") continue;
            if (operation != "update") {
                report.blocking_errors.push_back(
                    "Other-track key rename contains an unsupported operation: " +
                    edit_id);
                return;
            }
            const auto key_change =
                changed->second->field_changes.find("trackKey");
            if (key_change == changed->second->field_changes.end()) {
                report.blocking_errors.push_back(
                    "Other-track key rename must update every statement in the "
                    "track: " + edit_id);
                return;
            }
            Value parsed;
            try {
                parsed = track_key_from_display_text(key_change->second);
            } catch (const std::exception& e) {
                report.blocking_errors.push_back(
                    std::string("Other-track key rename has an invalid trackKey: ") +
                    e.what());
                return;
            }
            if (canonical_other_track_key(parsed) != request.second.final_key) {
                report.blocking_errors.push_back(
                    "Other-track key rename must use the same trackKey for every "
                    "statement in the track");
                return;
            }
        }
    }

    std::map<std::string, std::string> owner_by_final_key;
    for (const auto& group : groups) {
        bool has_surviving_row = false;
        for (size_t source_index : group.second) {
            const auto changed = changes_by_edit_id.find(edit_ids[source_index]);
            if (changed == changes_by_edit_id.end() ||
                ascii_lower(changed->second->operation.empty()
                                ? "update" : changed->second->operation) != "delete") {
                has_surviving_row = true;
                break;
            }
        }
        if (!has_surviving_row) continue;
        const auto renamed = requests.find(group.first);
        const std::string& final_key = renamed == requests.end()
            ? group.first : renamed->second.final_key;
        const auto inserted = owner_by_final_key.emplace(final_key, group.first);
        if (!inserted.second && inserted.first->second != group.first) {
            report.blocking_errors.push_back(
                "Other-track key already exists in map: key=" + final_key);
            return;
        }
    }
}

void validate_repeater_key_renames(
    MapContext& ctx, const std::vector<const MapEditChange*>& changes,
    MapEditReport& report) {
    std::vector<std::string> edit_ids;
    const repeater_linkage::Linkage linkage =
        repeater_linkage_state(ctx, &edit_ids);
    if (linkage.chains.empty()) return;

    std::map<std::string, size_t> source_index_by_edit_id;
    std::vector<std::optional<size_t>> chain_by_source_index(ctx.repeaters.size());
    for (size_t source_index = 0; source_index < edit_ids.size(); ++source_index) {
        if (!edit_ids[source_index].empty()) {
            source_index_by_edit_id.emplace(edit_ids[source_index], source_index);
        }
    }
    for (size_t chain_index = 0; chain_index < linkage.chains.size(); ++chain_index) {
        const repeater_linkage::Chain& chain = linkage.chains[chain_index];
        for (size_t source_index : chain.begin_source_indices) {
            if (source_index < chain_by_source_index.size()) {
                chain_by_source_index[source_index] = chain_index;
            }
        }
        if (chain.end_source_index &&
            *chain.end_source_index < chain_by_source_index.size()) {
            chain_by_source_index[*chain.end_source_index] = chain_index;
        }
    }

    struct RenameRequest {
        std::map<std::string, std::string> values_by_edit_id;
        bool changes_source_value = false;
    };
    std::map<size_t, RenameRequest> requests;
    for (const MapEditChange* change : changes) {
        const auto key_change = change->field_changes.find("repeaterKey");
        if (key_change == change->field_changes.end()) continue;
        auto source = source_index_by_edit_id.find(change->edit_id);
        if (source == source_index_by_edit_id.end()) continue;
        const size_t source_index = source->second;
        const std::string value = trim_field_copy(key_change->second);
        if (value.empty()) {
            report.blocking_errors.push_back(
                "Repeater key rename contains an empty repeaterKey");
            return;
        }
        const std::string operation =
            ascii_lower(change->operation.empty() ? "update" : change->operation);
        if (operation != "update") {
            report.blocking_errors.push_back(
                "Repeater key rename only supports update operations: " +
                change->edit_id);
            return;
        }
        if (!chain_by_source_index[source_index]) {
            report.blocking_errors.push_back(
                "Repeater key rename target is not part of a Begin chain: " +
                change->edit_id);
            return;
        }
        RenameRequest& request = requests[*chain_by_source_index[source_index]];
        request.values_by_edit_id.emplace(change->edit_id, value);
        request.changes_source_value = request.changes_source_value ||
            value != value_to_edit_text(ctx.repeaters[source_index].repeater_key);
    }

    std::vector<std::string> final_keys;
    final_keys.reserve(linkage.chains.size());
    for (const repeater_linkage::Chain& chain : linkage.chains) {
        final_keys.push_back(chain.key);
    }
    std::vector<bool> renamed(linkage.chains.size(), false);
    for (const auto& item : requests) {
        const size_t chain_index = item.first;
        const RenameRequest& request = item.second;
        if (!request.changes_source_value) continue;
        if (chain_index >= linkage.chains.size()) {
            report.blocking_errors.push_back(
                "Repeater key rename resolved an invalid chain");
            return;
        }
        const repeater_linkage::Chain& chain = linkage.chains[chain_index];
        std::vector<size_t> source_indices = chain.begin_source_indices;
        if (chain.end_source_index) source_indices.push_back(*chain.end_source_index);
        std::optional<std::string> proposed_key;
        for (size_t source_index : source_indices) {
            if (source_index >= edit_ids.size()) {
                report.blocking_errors.push_back(
                    "Repeater key rename chain contains invalid source metadata");
                return;
            }
            const std::string& edit_id = edit_ids[source_index];
            auto proposed = request.values_by_edit_id.find(edit_id);
            if (proposed == request.values_by_edit_id.end()) {
                report.blocking_errors.push_back(
                    "Repeater key rename must update every statement in the chain: " +
                    edit_id);
                return;
            }
            if (!proposed_key) {
                proposed_key = proposed->second;
            } else if (*proposed_key != proposed->second) {
                report.blocking_errors.push_back(
                    "Repeater key rename must use the same repeaterKey for every "
                    "statement in the chain");
                return;
            }
        }
        final_keys[chain_index] =
            repeater_linkage::canonical_key(proposed_key.value_or(std::string{}));
        if (final_keys[chain_index].empty()) {
            report.blocking_errors.push_back(
                "Repeater key rename contains an empty repeaterKey");
            return;
        }
        renamed[chain_index] = true;
    }

    std::vector<RepeaterNamedInterval> intervals;
    intervals.reserve(linkage.chains.size());
    for (size_t index = 0; index < linkage.chains.size(); ++index) {
        intervals.push_back({linkage.chains[index], final_keys[index], renamed[index]});
    }
    (void)validate_repeater_interval_conflicts(intervals, report);
}

void validate_repeater_insert_key_overlaps(
    MapContext& ctx, const std::vector<const MapEditChange*>& changes,
    MapEditReport& report) {
    struct InsertEvent {
        const MapEditChange* change = nullptr;
        std::string key;
        std::string method;
        double distance = 0.0;
    };

    std::vector<InsertEvent> inserted_events;
    inserted_events.reserve(changes.size());
    std::map<std::string, std::vector<size_t>> pair_members;
    for (const MapEditChange* change : changes) {
        const std::string operation =
            ascii_lower(change->operation.empty() ? "update" : change->operation);
        if (change->confirm_repeater_change_point &&
            (operation != "insert" || change->row_kind != "repeater")) {
            report.blocking_errors.push_back(
                "Repeater change-point confirmation is only valid for Repeater inserts");
            return;
        }
        if (!change->repeater_pair_id.empty() &&
            (operation != "insert" || change->row_kind != "repeater")) {
            report.blocking_errors.push_back(
                "repeaterPairId is only valid for Repeater insert changes");
            return;
        }
        if (operation != "insert" || change->row_kind != "repeater") continue;
        try {
            validate_insert_change(*change);
            InsertEvent event;
            event.change = change;
            event.key = repeater_linkage::canonical_key(
                required_string_field(*change, "repeaterKey", ""));
            event.method = ascii_lower(insert_method_or_default(*change, "Begin"));
            const std::string distance_text = insert_required_number(*change, "distance");
            if (event.key.empty() || !parse_edit_number(distance_text, event.distance)) {
                throw std::runtime_error("Repeater insert has an invalid key or distance");
            }
            if (change->confirm_repeater_change_point &&
                event.method != "begin" && event.method != "begin0") {
                throw std::runtime_error(
                    "only Repeater Begin inserts can confirm a change point");
            }
            inserted_events.push_back(std::move(event));
            if (!change->repeater_pair_id.empty()) {
                pair_members[change->repeater_pair_id].push_back(
                    inserted_events.size() - 1);
            }
        } catch (const std::exception& e) {
            report.blocking_errors.push_back(
                std::string("Repeater insert validation failed: ") + e.what());
            return;
        }
    }

    for (const auto& pair : pair_members) {
        if (pair.second.size() != 2) {
            report.blocking_errors.push_back(
                "Repeater paired insert must contain exactly one Begin and one End: " +
                pair.first);
            return;
        }
        const InsertEvent* begin = nullptr;
        const InsertEvent* end = nullptr;
        for (size_t index : pair.second) {
            const InsertEvent& event = inserted_events[index];
            if (event.method == "begin" || event.method == "begin0") begin = &event;
            else if (event.method == "end") end = &event;
        }
        if (!begin || !end || begin->key != end->key ||
            begin->change->target_file_path != end->change->target_file_path) {
            report.blocking_errors.push_back(
                "Repeater paired insert must use one Begin and one End with the same key and target file: " +
                pair.first);
            return;
        }
        if (begin->change->confirm_repeater_change_point) {
            report.blocking_errors.push_back(
                "Repeater paired insert cannot confirm a Begin change point: " + pair.first);
            return;
        }
        if (end->distance < begin->distance) {
            report.blocking_errors.push_back(
                "Repeater End distance cannot be less than Begin distance: pair=" +
                pair.first);
            return;
        }
    }

    const repeater_linkage::Linkage linkage = repeater_linkage_state(ctx);
    std::vector<RepeaterNamedInterval> intervals;
    intervals.reserve(linkage.chains.size() + inserted_events.size());
    for (const repeater_linkage::Chain& chain : linkage.chains) {
        intervals.push_back({chain, chain.key, false, false,
                             chain.end_source_index.has_value()});
    }

    for (const InsertEvent& inserted : inserted_events) {
        if (inserted.method != "begin" && inserted.method != "begin0") continue;

        // A later Begin or End of the same key bounds the newly created
        // half-open interval. Include every event in the typed batch so a
        // paired End bounds its Begin before source generation.
        repeater_linkage::Chain requested;
        requested.key = inserted.key;
        requested.begin_distance = inserted.distance;
        bool has_explicit_end = false;
        auto consider_boundary = [&](double distance, const std::string& method) {
            if (distance <= inserted.distance ||
                (method != "begin" && method != "begin0" && method != "end")) {
                return;
            }
            if (!requested.end_distance || distance < *requested.end_distance) {
                requested.end_distance = distance;
                has_explicit_end = method == "end";
            } else if (distance == *requested.end_distance && method == "end") {
                has_explicit_end = true;
            }
        };
        for (const RepeaterEvent& row : ctx.repeaters) {
            if (repeater_linkage::canonical_key(
                    value_to_edit_text(row.repeater_key)) != inserted.key) {
                continue;
            }
            consider_boundary(row.distance, ascii_lower(row.method));
        }
        for (const InsertEvent& other : inserted_events) {
            if (&other == &inserted || other.key != inserted.key) continue;
            consider_boundary(other.distance, other.method);
        }
        intervals.push_back({std::move(requested), inserted.key, true,
                             inserted.change->confirm_repeater_change_point,
                             has_explicit_end});
    }

    if (!validate_repeater_interval_conflicts(intervals, report)) return;

    for (const InsertEvent& inserted : inserted_events) {
        if (inserted.method != "end") continue;
        for (const RepeaterNamedInterval& interval : intervals) {
            if (!interval.has_explicit_end || interval.key != inserted.key ||
                !repeater_linkage::chain_contains_distance(
                    interval.interval, inserted.distance)) {
                continue;
            }
            report.blocking_errors.push_back(
                "Repeater End falls inside a same-name interval that already has an explicit End: key=" +
                inserted.key + ", interval=" + repeater_interval_text(interval.interval));
            return;
        }
    }
}

bool final_environment_matches_include_deletions(
    MapContext& baseline,
    const MapContext& candidate,
    const std::vector<bool>& removed_statements,
    std::string& error) {
    std::map<std::string, size_t> total_variable_writes;
    std::map<std::string, size_t> removed_variable_writes;
    for (size_t i = 0; i < baseline.parsed_statements.size(); ++i) {
        const std::string name =
            assigned_variable_name(baseline.parsed_statements[i]);
        if (name.empty()) continue;
        ++total_variable_writes[name];
        if (i < removed_statements.size() && removed_statements[i]) {
            ++removed_variable_writes[name];
        }
    }
    auto removal_owns_variable = [&](const std::string& name) {
        const auto total = total_variable_writes.find(name);
        return total != total_variable_writes.end() &&
            removed_variable_writes[name] == total->second;
    };

    for (const auto& entry : baseline.variables) {
        const auto found = candidate.variables.find(entry.first);
        if (found != candidate.variables.end() &&
            value_equal(entry.second, found->second)) {
            continue;
        }
        if (!removal_owns_variable(entry.first)) {
            error = "full reparse changed a variable outside the deleted "
                    "Include subtree: " + entry.first;
            return false;
        }
    }
    for (const auto& entry : candidate.variables) {
        const auto found = baseline.variables.find(entry.first);
        if (found != baseline.variables.end() &&
            value_equal(entry.second, found->second)) {
            continue;
        }
        if (!removal_owns_variable(entry.first)) {
            error = "full reparse changed a variable outside the deleted "
                    "Include subtree: " + entry.first;
            return false;
        }
    }

    int best_order = std::numeric_limits<int>::min();
    bool best_removed = false;
    int last_surviving_order = std::numeric_limits<int>::min();
    double surviving_distance = 0.0;
    bool has_surviving_distance = false;
    for (size_t i = 0; i < baseline.parsed_statements.size(); ++i) {
        const ParsedStatement& statement = baseline.parsed_statements[i];
        if (!is_distance_statement(statement)) continue;
        const bool removed = i < removed_statements.size() && removed_statements[i];
        if (statement.global_order > best_order) {
            best_order = statement.global_order;
            best_removed = removed;
        }
        if (!removed && statement.global_order > last_surviving_order) {
            last_surviving_order = statement.global_order;
            surviving_distance = statement.distance_value;
            has_surviving_distance = true;
        }
    }
    double expected_distance = baseline.distance;
    if (best_removed) {
        expected_distance = has_surviving_distance ? surviving_distance : 0.0;
    }
    if (candidate.distance != expected_distance) {
        error = "full reparse changed the final distance outside the deleted "
                "Include subtree";
        return false;
    }
    return true;
}

bool final_environment_matches_include_edits(
    MapContext& baseline,
    const MapContext& candidate,
    const std::vector<bool>& removed_statements,
    const std::vector<bool>& added_statements,
    std::string& error) {
    // An include path swap legitimately replaces the variables and distance
    // statements contributed by the old subtree with the ones contributed by
    // the new file. Any difference must be attributable to exactly those
    // statement masks; everything else is an unexpected environment change.
    std::map<std::string, size_t> total_baseline_writes;
    std::map<std::string, size_t> removed_variable_writes;
    for (size_t i = 0; i < baseline.parsed_statements.size(); ++i) {
        const std::string name =
            assigned_variable_name(baseline.parsed_statements[i]);
        if (name.empty()) continue;
        ++total_baseline_writes[name];
        if (i < removed_statements.size() && removed_statements[i]) {
            ++removed_variable_writes[name];
        }
    }
    auto removal_owns_variable = [&](const std::string& name) {
        const auto total = total_baseline_writes.find(name);
        if (total == total_baseline_writes.end()) return true;
        return removed_variable_writes[name] == total->second;
    };

    const bool has_added_mask = !added_statements.empty();
    std::map<std::string, size_t> total_candidate_writes;
    std::map<std::string, size_t> added_variable_writes;
    if (has_added_mask) {
        for (size_t i = 0; i < candidate.parsed_statements.size(); ++i) {
            const std::string name =
                assigned_variable_name(candidate.parsed_statements[i]);
            if (name.empty()) continue;
            ++total_candidate_writes[name];
            if (i < added_statements.size() && added_statements[i]) {
                ++added_variable_writes[name];
            }
        }
    }
    auto addition_owns_variable = [&](const std::string& name) {
        if (!has_added_mask) return true;
        const auto total = total_candidate_writes.find(name);
        if (total == total_candidate_writes.end()) return true;
        return added_variable_writes[name] == total->second;
    };

    for (const auto& entry : baseline.variables) {
        const auto found = candidate.variables.find(entry.first);
        if (found != candidate.variables.end() &&
            value_equal(entry.second, found->second)) {
            continue;
        }
        if (!removal_owns_variable(entry.first) ||
            !addition_owns_variable(entry.first)) {
            error = "full reparse changed a variable outside the swapped "
                    "Include subtree: " + entry.first;
            return false;
        }
    }
    for (const auto& entry : candidate.variables) {
        const auto found = baseline.variables.find(entry.first);
        if (found != baseline.variables.end() &&
            value_equal(entry.second, found->second)) {
            continue;
        }
        if (!removal_owns_variable(entry.first) ||
            !addition_owns_variable(entry.first)) {
            error = "full reparse changed a variable outside the swapped "
                    "Include subtree: " + entry.first;
            return false;
        }
    }

    // Distance statements outside both masks must survive unchanged as a
    // value sequence. The final distance itself may legitimately change when
    // a swapped-in file ends with its own distance statements.
    std::vector<double> base_distance_sequence;
    std::vector<double> candidate_distance_sequence;
    for (size_t i = 0; i < baseline.parsed_statements.size(); ++i) {
        const ParsedStatement& statement = baseline.parsed_statements[i];
        if (!is_distance_statement(statement)) continue;
        if (i < removed_statements.size() && removed_statements[i]) continue;
        base_distance_sequence.push_back(statement.distance_value);
    }
    for (size_t i = 0; i < candidate.parsed_statements.size(); ++i) {
        const ParsedStatement& statement = candidate.parsed_statements[i];
        if (!is_distance_statement(statement)) continue;
        if (has_added_mask && i < added_statements.size() && added_statements[i]) {
            continue;
        }
        candidate_distance_sequence.push_back(statement.distance_value);
    }
    if (base_distance_sequence != candidate_distance_sequence) {
        error = "full reparse changed a distance statement outside the "
                "swapped Include subtree";
        return false;
    }
    return true;
}

void validate_edit_report(MapContext& baseline,
                          const std::vector<MapEditChange>& changes,
                          MapEditReport& report) {
    std::unique_ptr<MapContext> candidate;
    SemanticSnapshot before_snapshot;
    SemanticSnapshot after_snapshot;
    try {
        candidate = parse_report_candidate(baseline, report);
        before_snapshot = semantic_snapshot_for_context(baseline);
        after_snapshot = semantic_snapshot_for_context(*candidate);
    } catch (const std::exception& e) {
        report.blocking_errors.push_back(std::string("full edited-map reparse failed: ") + e.what());
        return;
    }

    const std::vector<SemanticElement>& before_elements = before_snapshot.elements;
    const std::vector<SemanticElement>& after_elements = after_snapshot.elements;
    std::map<std::string, const SemanticElement*> before_by_id;
    std::map<std::string, const SemanticElement*> after_by_native_id;
    for (const SemanticElement& element : before_elements) {
        if (!before_by_id.emplace(element.edit_id, &element).second) {
            report.blocking_errors.push_back("duplicate baseline editId during validation: " +
                                             element.edit_id);
            return;
        }
    }
    for (const SemanticElement& element : after_elements) {
        if (!after_by_native_id.emplace(element.edit_id, &element).second) {
            report.blocking_errors.push_back("duplicate candidate editId during validation: " +
                                             element.edit_id);
            return;
        }
    }

    std::set<std::string> excluded_before;
    std::set<std::string> insert_edit_ids;
    std::set<std::string> include_insert_edit_ids;
    std::map<std::string, std::string> expected_target_canonical;
    int expected_distance_target_count = 0;
    std::vector<size_t> include_delete_statements;
    std::set<std::string> include_delete_edit_ids;
    std::map<size_t, std::string> include_update_expected_paths;
    struct ResourceListReplacement {
        const ResourceListEditSpec* spec = nullptr;
        std::string edit_id;
        bool is_insert = false;
        size_t baseline_statement_index = k_no_source_ref;
        std::string baseline_content_source_key;
        std::string baseline_structure_load_edit_id;
        std::string candidate_content_source_key;
        std::string candidate_structure_load_edit_id;
    };
    std::vector<ResourceListReplacement> resource_list_replacements;
    for (const MapEditChange& change : changes) {
        if (change.edit_id.empty()) continue;
        const std::string operation =
            ascii_lower(change.operation.empty() ? "update" : change.operation);
        if (operation == "delete") {
            EditableTarget delete_target =
                find_editable_target(baseline, change.edit_id);
            if (delete_target.statement_index != k_no_source_ref &&
                delete_target.row_kind == "include") {
                if (include_delete_edit_ids.insert(change.edit_id).second) {
                    include_delete_statements.push_back(
                        delete_target.statement_index);
                }
                continue;
            }
        }
        if (operation == "update") {
            // Include updates own no baseline semantic element, so they are
            // proven separately by reconnecting the replaced statement and
            // swapping the Include subtrees on both reparse sides.
            EditableTarget update_target =
                find_editable_target(baseline, change.edit_id);
            if (update_target.statement_index != k_no_source_ref &&
                update_target.row_kind == "include") {
                std::string new_path;
                try {
                    new_path = include_path_from_change(change);
                } catch (const std::exception& e) {
                    report.blocking_errors.push_back(
                        std::string("target validation failed for ") +
                        change.edit_id + ": " + e.what());
                    return;
                }
                if (!expected_target_canonical
                         .emplace(change.edit_id, "include:" + new_path)
                         .second) {
                    report.blocking_errors.push_back(
                        "more than one edit targets the same element: " +
                        change.edit_id);
                    return;
                }
                include_update_expected_paths.emplace(
                    update_target.statement_index, std::move(new_path));
                continue;
            }
            if (update_target.statement_index != k_no_source_ref &&
                update_target.row_kind == "resourceList.load") {
                const ParsedStatement& statement = baseline.parsed_statements[
                    update_target.statement_index];
                const ResourceListEditSpec* spec =
                    resource_list_edit_spec_for_statement(statement.statement_kind);
                const ResourceListLoad* load = spec
                    ? resource_list_load_for_kind(baseline, spec->kind)
                    : nullptr;
                if (!spec || !load) {
                    report.blocking_errors.push_back(
                        "target validation lost the resource list source: " +
                        change.edit_id);
                    return;
                }
                std::string new_path;
                try {
                    new_path = resource_list_path_from_change(change);
                } catch (const std::exception& e) {
                    report.blocking_errors.push_back(
                        std::string("target validation failed for ") +
                        change.edit_id + ": " + e.what());
                    return;
                }
                if (!expected_target_canonical
                         .emplace(change.edit_id,
                                  std::string("resourceList:") + spec->error_key +
                                      ":" + new_path)
                         .second) {
                    report.blocking_errors.push_back(
                        "more than one edit targets the same element: " +
                        change.edit_id);
                    return;
                }
                ResourceListReplacement replacement;
                replacement.spec = spec;
                replacement.edit_id = change.edit_id;
                replacement.baseline_statement_index = update_target.statement_index;
                replacement.baseline_content_source_key = normalized_source_key(
                    load->resolved_path);
                if (spec->kind == ResourceListLoadKind::Structure) {
                    for (const StructureLoad& structure_load : baseline.structure_loads) {
                        if (structure_load.edit_ref.statement_index !=
                            update_target.statement_index) {
                            continue;
                        }
                        replacement.baseline_structure_load_edit_id = element_edit_id(
                            baseline, structure_load.edit_ref, "structure.load");
                        break;
                    }
                }
                resource_list_replacements.push_back(std::move(replacement));
                continue;
            }
        }
        if (operation == "insert") {
            // Insert changes carry a temporary edit id that has no baseline
            // element; the expected semantic is derived from the change fields
            // and the target file instead.
            if (before_by_id.find(change.edit_id) != before_by_id.end()) {
                report.blocking_errors.push_back(
                    "insert edit id collides with an existing element: " + change.edit_id);
                return;
            }
            insert_edit_ids.insert(change.edit_id);
            if (change.row_kind == "include") {
                try {
                    const std::string new_path = include_path_from_change(change);
                    if (!expected_target_canonical
                             .emplace(change.edit_id, "include:" + new_path)
                             .second) {
                        report.blocking_errors.push_back(
                            "more than one edit targets the same element: " +
                            change.edit_id);
                        return;
                    }
                    include_insert_edit_ids.insert(change.edit_id);
                } catch (const std::exception& e) {
                    report.blocking_errors.push_back(
                        std::string("target validation failed for ") +
                        change.edit_id + ": " + e.what());
                    return;
                }
                continue;
            }
            if (change.row_kind == "resourceList.load") {
                try {
                    const ResourceListEditSpec* spec =
                        resource_list_insert_spec_from_change(change);
                    const std::string new_path = resource_list_insert_path_from_change(change);
                    if (!expected_target_canonical
                             .emplace(change.edit_id,
                                      std::string("resourceList:") + spec->error_key +
                                          ":" + new_path)
                             .second) {
                        report.blocking_errors.push_back(
                            "more than one edit targets the same element: " +
                            change.edit_id);
                        return;
                    }
                    ResourceListReplacement replacement;
                    replacement.spec = spec;
                    replacement.edit_id = change.edit_id;
                    replacement.is_insert = true;
                    resource_list_replacements.push_back(std::move(replacement));
                } catch (const std::exception& e) {
                    report.blocking_errors.push_back(
                        std::string("target validation failed for ") +
                        change.edit_id + ": " + e.what());
                    return;
                }
                continue;
            }
            try {
                expected_target_canonical.emplace(
                    change.edit_id, expected_insert_semantic(baseline, change));
            } catch (const std::exception& e) {
                report.blocking_errors.push_back(std::string("target validation failed for ") +
                                                 change.edit_id + ": " + e.what());
                return;
            }
            if (has_field_change(change, "distance")) ++expected_distance_target_count;
            continue;
        }
        auto before_it = before_by_id.find(change.edit_id);
        if (before_it == before_by_id.end()) {
            report.blocking_errors.push_back("validation lost baseline target: " + change.edit_id);
            return;
        }
        excluded_before.insert(change.edit_id);
        if (operation == "delete") continue;
        try {
            expected_target_canonical.emplace(
                change.edit_id,
                expected_target_semantic(baseline, *before_it->second, change));
        } catch (const std::exception& e) {
            report.blocking_errors.push_back(std::string("target validation failed for ") +
                                             change.edit_id + ": " + e.what());
            return;
        }
        if (has_field_change(change, "distance")) ++expected_distance_target_count;
    }

    std::vector<bool> include_removed_statements;
    std::set<std::string> include_subtree_deletions;
    if (!include_delete_statements.empty() || !include_update_expected_paths.empty()) {
        // Both include deletes and include path updates remove the previously
        // included subtree from the baseline side of the reparse comparison;
        // updates additionally introduce the new file's subtree, handled after
        // the candidate statement has been reconnected below.
        std::vector<size_t> include_changed_statements = include_delete_statements;
        for (const auto& entry : include_update_expected_paths) {
            include_changed_statements.push_back(entry.first);
        }
        collect_include_subtree_removals(
            baseline, include_changed_statements, include_removed_statements,
            &include_subtree_deletions);
        for (const std::string& edit_id : include_subtree_deletions) {
            if (before_by_id.find(edit_id) != before_by_id.end()) {
                excluded_before.insert(edit_id);
            }
        }
    }

    using IdentityLocation =
        std::tuple<std::string, size_t, size_t, std::string, int>;
    struct CandidateIdentityElement {
        std::string native_edit_id;
        int global_order = 0;
    };
    std::map<IdentityLocation, std::vector<const MapEditIdentityOrigin*>> origins_by_location;
    std::set<std::string> origin_edit_ids;
    for (const MapEditIdentityOrigin& origin : report.identity_origins) {
        if (expected_target_canonical.find(origin.edit_id) ==
            expected_target_canonical.end()) {
            report.blocking_errors.push_back(
                "generated edit identity does not belong to a surviving target: " +
                origin.edit_id);
            return;
        }
        if (!origin_edit_ids.insert(origin.edit_id).second) {
            report.blocking_errors.push_back(
                "generated more than one source identity for edit target: " + origin.edit_id);
            return;
        }
        origins_by_location[IdentityLocation{
            origin.source_key, origin.text_start, origin.text_end,
            origin.row_kind, origin.element_index,
        }].push_back(&origin);
    }
    if (origin_edit_ids.size() != expected_target_canonical.size()) {
        report.blocking_errors.push_back(
            "full reparse validation is missing source provenance for an edited target");
        return;
    }

    struct IndexedPatchedText {
        const std::string* text = nullptr;
        std::vector<size_t> line_starts;
    };
    std::map<std::string, IndexedPatchedText> patched_text_by_source_key;
    for (const MapEditPatchedFile& file : report.patched_files) {
        patched_text_by_source_key[file.source_key] = {
            &file.text, build_line_starts(file.text)};
    }
    std::map<IdentityLocation, std::vector<CandidateIdentityElement>> candidates_by_location;
    std::vector<size_t> bound_include_added_statements;
    auto collect_candidate_row = [&](const auto& row, const std::string& row_kind) {
        const EditSourceRef& ref = row.edit_ref;
        if (!ref.valid() || ref.statement_index >= candidate->parsed_statements.size()) return;
        const ParsedStatement& statement = candidate->parsed_statements[ref.statement_index];
        const std::string& source_key = source_file_key(*candidate, statement.source);
        auto patched_text = patched_text_by_source_key.find(source_key);
        if (patched_text == patched_text_by_source_key.end()) return;
        const auto range = source_range_in_text(
            *patched_text->second.text,
            patched_text->second.line_starts,
            statement.source);
        candidates_by_location[IdentityLocation{
            source_key, range.first, range.second, row_kind, ref.element_index,
        }].push_back({
            native_element_edit_id(*candidate, ref, row_kind),
            statement.global_order,
        });
    };
    auto collect_candidate_rows = [&](const auto& rows, const std::string& row_kind) {
        for (const auto& row : rows) {
            collect_candidate_row(row, row_kind);
        }
    };
    try {
        collect_candidate_rows(candidate->structure_models, "structure.model");
        collect_candidate_rows(candidate->curves, "curve");
        collect_candidate_rows(candidate->gradients, "gradient");
        collect_candidate_rows(candidate->other_track_changes, "otherTrack.change");
        for (const SoundListEntry& row : candidate->sound_list) {
            collect_candidate_row(row, row.is_3d ? "sound3D.list" : "sound.list");
        }
        collect_candidate_rows(candidate->structure_puts, "structure.put");
        collect_candidate_rows(candidate->structure_betweens, "structure.between");
        collect_candidate_rows(candidate->station_puts, "station.put");
        for (const auto& entry : ordered_station_list_entries(*candidate)) {
            collect_candidate_row(*entry, "station.list");
        }
        collect_candidate_rows(candidate->signal_aspects, "signal.aspect");
        collect_candidate_rows(candidate->signal_puts, "signal.put");
        collect_candidate_rows(candidate->repeaters, "repeater");
        collect_candidate_rows(candidate->irregularities, "irregularity.change");
        collect_candidate_rows(candidate->beacons, "beacon.put");
        collect_candidate_rows(candidate->section_begins, "section.begin");
        collect_candidate_rows(candidate->section_speed_limits, "section.speedLimit");
        collect_candidate_rows(candidate->map_sounds, "mapSound.play");
        collect_candidate_rows(candidate->map_sound_3d, "mapSound3D.put");
        collect_candidate_rows(candidate->rolling_noises, "rollingNoise.change");
        collect_candidate_rows(candidate->flange_noises, "flangeNoise.change");
        collect_candidate_rows(candidate->joint_noises, "jointNoise.play");
        collect_candidate_rows(candidate->backgrounds, "background.change");
        collect_candidate_rows(candidate->adhesions, "adhesion.change");
        collect_candidate_rows(candidate->cab_illuminance, "cabIlluminance.change");
        collect_candidate_rows(candidate->fogs, "fog.change");
        collect_candidate_rows(candidate->draw_distances, "drawDistance.change");
        collect_candidate_rows(candidate->speedlimits, "speedlimit");
    } catch (const std::exception& e) {
        report.blocking_errors.push_back(
            std::string("failed to resolve edited target source provenance: ") + e.what());
        return;
    }

    // Include statements own no typed element rows, so their reparse candidates
    // are collected directly from the parsed statement stream. Their identity
    // is proven by the evaluated path text at the patched source location.
    std::map<std::string, std::string> candidate_include_canonicals;
    std::map<std::string, size_t> candidate_include_statement_by_id;
    if (!include_update_expected_paths.empty() || !include_insert_edit_ids.empty()) {
        for (size_t i = 0; i < candidate->parsed_statements.size(); ++i) {
            const ParsedStatement& statement = candidate->parsed_statements[i];
            if (statement.statement_kind != "Include") continue;
            const std::string& source_key =
                source_file_key(*candidate, statement.source);
            auto patched_text = patched_text_by_source_key.find(source_key);
            if (patched_text == patched_text_by_source_key.end()) continue;
            const auto range = source_range_in_text(
                *patched_text->second.text,
                patched_text->second.line_starts,
                statement.source);
            candidate_include_canonicals[statement.edit_id] =
                statement.evaluated_values.empty()
                    ? std::string("include:")
                    : "include:" + as_text(statement.evaluated_values.front());
            candidate_include_statement_by_id.emplace(statement.edit_id, i);
            candidates_by_location[IdentityLocation{
                source_key, range.first, range.second, "include", 0,
            }].push_back({
                statement.edit_id,
                statement.global_order,
            });
        }
    }

    // Resource-list Load statements follow the same source-only identity
    // rule as Include, but their loaded list rows are scoped separately below.
    std::map<std::string, std::string> candidate_resource_list_canonicals;
    std::map<std::string, size_t> candidate_resource_list_statement_by_id;
    if (!resource_list_replacements.empty()) {
        for (size_t i = 0; i < candidate->parsed_statements.size(); ++i) {
            const ParsedStatement& statement = candidate->parsed_statements[i];
            const ResourceListEditSpec* spec =
                resource_list_edit_spec_for_statement(statement.statement_kind);
            if (!spec) continue;
            const std::string& source_key =
                source_file_key(*candidate, statement.source);
            auto patched_text = patched_text_by_source_key.find(source_key);
            if (patched_text == patched_text_by_source_key.end()) continue;
            const auto range = source_range_in_text(
                *patched_text->second.text,
                patched_text->second.line_starts,
                statement.source);
            candidate_resource_list_canonicals[statement.edit_id] =
                std::string("resourceList:") + spec->error_key + ":" +
                (statement.evaluated_values.empty()
                    ? std::string{}
                    : as_text(statement.evaluated_values.front()));
            candidate_resource_list_statement_by_id.emplace(statement.edit_id, i);
            candidates_by_location[IdentityLocation{
                source_key, range.first, range.second, "resourceList.load", 0,
            }].push_back({
                statement.edit_id,
                statement.global_order,
            });
        }
    }

    std::map<std::string, std::string> candidate_to_stable_edit_ids;
    std::map<std::string, std::string> stable_to_candidate_edit_ids;
    auto preserve_edit_identity = [&](const std::string& stable_id,
                                      const std::string& candidate_id) {
        if (stable_id.empty() || candidate_id.empty()) {
            report.blocking_errors.push_back(
                "validated edit identity contains an empty editId");
            return;
        }
        auto by_candidate = candidate_to_stable_edit_ids.emplace(candidate_id, stable_id);
        if (!by_candidate.second && by_candidate.first->second != stable_id) {
            report.blocking_errors.push_back(
                "validated candidate editId matched multiple baseline elements: " +
                candidate_id);
        }
        auto by_stable = stable_to_candidate_edit_ids.emplace(stable_id, candidate_id);
        if (!by_stable.second && by_stable.first->second != candidate_id) {
            report.blocking_errors.push_back(
                "validated baseline editId matched multiple candidate elements: " + stable_id);
        }
    };

    std::set<std::string> candidate_target_ids;
    std::set<std::string> insert_extra_native_ids;
    const auto source_path_canonical = [&](const std::string& edit_id)
        -> const std::string* {
        auto include = candidate_include_canonicals.find(edit_id);
        if (include != candidate_include_canonicals.end()) return &include->second;
        auto resource = candidate_resource_list_canonicals.find(edit_id);
        return resource == candidate_resource_list_canonicals.end()
            ? nullptr : &resource->second;
    };
    for (auto& origin_group : origins_by_location) {
        auto candidates = candidates_by_location.find(origin_group.first);
        if (candidates == candidates_by_location.end() ||
            candidates->second.empty()) {
            report.blocking_errors.push_back(
                "edited target could not be uniquely reconnected to its generated source statement");
            return;
        }
        const bool insert_origin = origin_group.second.size() == 1 &&
            insert_edit_ids.find(origin_group.second.front()->edit_id) !=
                insert_edit_ids.end();
        const bool include_origin =
            origin_group.second.front()->row_kind == "include";
        const bool resource_list_origin =
            origin_group.second.front()->row_kind == "resourceList.load";
        const bool source_path_origin = include_origin || resource_list_origin;
        if (!insert_origin &&
            candidates->second.size() != origin_group.second.size()) {
            report.blocking_errors.push_back(
                "edited target could not be uniquely reconnected to its generated source statement");
            return;
        }
        if (insert_origin) {
            // One physical insert statement may reparse into one logical row
            // per Include invocation of the target file. Bind the temporary
            // insert id to the first row whose canonical matches; the extra
            // invocation rows are skipped as insert-extra elements by the
            // non-target comparison below.
            std::stable_sort(candidates->second.begin(), candidates->second.end(),
                             [](const auto& lhs, const auto& rhs) {
                                 return lhs.global_order < rhs.global_order;
                             });
            bool bound = false;
            for (size_t i = 0; i < candidates->second.size(); ++i) {
                const CandidateIdentityElement& candidate_element = candidates->second[i];
                auto expected = expected_target_canonical.find(
                    origin_group.second.front()->edit_id);
                bool matched = false;
                if (source_path_origin) {
                    const std::string* canonical = source_path_canonical(
                        candidate_element.native_edit_id);
                    matched = canonical && expected != expected_target_canonical.end() &&
                        *canonical == expected->second;
                } else {
                    auto after = after_by_native_id.find(candidate_element.native_edit_id);
                    matched = after != after_by_native_id.end() &&
                        expected != expected_target_canonical.end() &&
                        after->second->canonical == expected->second;
                }
                if (!matched) {
                    continue;
                }
                preserve_edit_identity(origin_group.second.front()->edit_id,
                                       candidate_element.native_edit_id);
                candidate_target_ids.insert(candidate_element.native_edit_id);
                for (size_t j = 0; j < candidates->second.size(); ++j) {
                    if (j == i) continue;
                    insert_extra_native_ids.insert(
                        candidates->second[j].native_edit_id);
                }
                if (include_origin) {
                    for (const CandidateIdentityElement& duplicate : candidates->second) {
                        auto canonical_it = candidate_include_canonicals.find(
                            duplicate.native_edit_id);
                        if (canonical_it == candidate_include_canonicals.end() ||
                            canonical_it->second != expected->second) {
                            continue;
                        }
                        auto statement_it = candidate_include_statement_by_id.find(
                            duplicate.native_edit_id);
                        if (statement_it == candidate_include_statement_by_id.end()) {
                            report.blocking_errors.push_back(
                                "validated include insert lost its reparsed statement: " +
                                origin_group.second.front()->edit_id);
                            return;
                        }
                        bound_include_added_statements.push_back(statement_it->second);
                    }
                }
                bound = true;
                break;
            }
            if (!bound) {
                report.blocking_errors.push_back(
                    "edited target did not reparse to its expected semantic value: " +
                    origin_group.second.front()->edit_id);
                return;
            }
            continue;
        }
        std::stable_sort(origin_group.second.begin(), origin_group.second.end(),
                         [](const auto* lhs, const auto* rhs) {
                             return lhs->baseline_global_order < rhs->baseline_global_order;
                         });
        std::stable_sort(candidates->second.begin(), candidates->second.end(),
                         [](const auto& lhs, const auto& rhs) {
                             return lhs.global_order < rhs.global_order;
                         });
        for (size_t i = 0; i < origin_group.second.size(); ++i) {
            const MapEditIdentityOrigin& origin = *origin_group.second[i];
            const CandidateIdentityElement& candidate_element = candidates->second[i];
            auto expected = expected_target_canonical.find(origin.edit_id);
            bool matched = false;
            if (source_path_origin) {
                const std::string* canonical = source_path_canonical(
                    candidate_element.native_edit_id);
                matched = canonical && expected != expected_target_canonical.end() &&
                    *canonical == expected->second;
            } else {
                auto after = after_by_native_id.find(candidate_element.native_edit_id);
                matched = after != after_by_native_id.end() &&
                    expected != expected_target_canonical.end() &&
                    after->second->canonical == expected->second;
            }
            if (!matched) {
                report.blocking_errors.push_back(
                    "edited target did not reparse to its expected semantic value: " +
                    origin.edit_id);
                return;
            }
            preserve_edit_identity(origin.edit_id, candidate_element.native_edit_id);
            candidate_target_ids.insert(candidate_element.native_edit_id);
            if (include_origin) {
                auto statement_it = candidate_include_statement_by_id.find(
                    candidate_element.native_edit_id);
                if (statement_it == candidate_include_statement_by_id.end()) {
                    report.blocking_errors.push_back(
                        "validated include edit lost its reparsed statement: " +
                        origin.edit_id);
                    return;
                }
                bound_include_added_statements.push_back(statement_it->second);
            }
        }
    }
    if (!report.blocking_errors.empty()) return;

    for (ResourceListReplacement& replacement : resource_list_replacements) {
        const auto candidate_id = stable_to_candidate_edit_ids.find(
            replacement.edit_id);
        if (candidate_id == stable_to_candidate_edit_ids.end()) {
            report.blocking_errors.push_back(
                "validated resource list edit lost its reparsed statement: " +
                replacement.edit_id);
            return;
        }
        const auto statement = candidate_resource_list_statement_by_id.find(
            candidate_id->second);
        const ResourceListLoad* load = resource_list_load_for_kind(
            *candidate, replacement.spec->kind);
        if (statement == candidate_resource_list_statement_by_id.end() || !load) {
            report.blocking_errors.push_back(
                "validated resource list edit lost its reloaded list: " +
                replacement.edit_id);
            return;
        }
        replacement.candidate_content_source_key = normalized_source_key(
            load->resolved_path);
        if (replacement.spec->kind == ResourceListLoadKind::Structure) {
            for (const StructureLoad& structure_load : candidate->structure_loads) {
                if (structure_load.edit_ref.statement_index != statement->second) continue;
                replacement.candidate_structure_load_edit_id = element_edit_id(
                    *candidate, structure_load.edit_ref, "structure.load");
                break;
            }
            if (!replacement.is_insert &&
                (replacement.baseline_structure_load_edit_id.empty() ||
                 replacement.candidate_structure_load_edit_id.empty())) {
                report.blocking_errors.push_back(
                    "validated Structure.Load edit lost its source row: " +
                    replacement.edit_id);
                return;
            }
        }
    }

    // The swapped-in file's subtree is new content on the candidate side of
    // the reparse comparison. Collect its statement mask and element ids so
    // the non-target and environment checks only police untouched regions.
    std::vector<bool> include_added_statements;
    std::set<std::string> include_subtree_additions;
    if (!bound_include_added_statements.empty()) {
        include_added_statements.assign(candidate->parsed_statements.size(), false);
        for (size_t site_index : bound_include_added_statements) {
            if (site_index >= candidate->parsed_statements.size()) continue;
            const ParsedStatement& site =
                candidate->parsed_statements[site_index];
            const std::string& site_source_key = source_file_key(*candidate, site.source);
            for (size_t i = 0; i < candidate->parsed_statements.size(); ++i) {
                const ParsedStatement& statement =
                    candidate->parsed_statements[i];
                if (include_invocation_chain_contains(
                        source_include_invocation_key(*candidate, statement.source),
                        site_source_key, site.source.byte_start,
                        site.source.byte_end)) {
                    include_added_statements[i] = true;
                }
            }
        }
        collect_subtree_element_ids(
            *candidate, include_added_statements, &include_subtree_additions);
    }

    const auto resource_list_element_is_replaced =
        [&](const SemanticElement& element, bool candidate_side) {
        for (const ResourceListReplacement& replacement : resource_list_replacements) {
            const std::string& content_source_key = candidate_side
                ? replacement.candidate_content_source_key
                : replacement.baseline_content_source_key;
            if (element.row_kind == replacement.spec->content_row_kind &&
                normalized_source_key(element.source_file) == content_source_key) {
                return true;
            }
            const std::string& structure_load_id = candidate_side
                ? replacement.candidate_structure_load_edit_id
                : replacement.baseline_structure_load_edit_id;
            if (!structure_load_id.empty() &&
                element.row_kind == "structure.load" &&
                element.edit_id == structure_load_id) {
                return true;
            }
        }
        return false;
    };

    std::vector<const SemanticElement*> before_non_targets;
    before_non_targets.reserve(before_elements.size() - excluded_before.size());
    for (const SemanticElement& element : before_elements) {
        if (excluded_before.find(element.edit_id) == excluded_before.end() &&
            !resource_list_element_is_replaced(element, false)) {
            before_non_targets.push_back(&element);
        }
    }
    size_t before_position = 0;
    for (const SemanticElement& element : after_elements) {
        if (candidate_target_ids.find(element.edit_id) != candidate_target_ids.end()) {
            continue;
        }
        if (insert_extra_native_ids.find(element.edit_id) !=
            insert_extra_native_ids.end()) {
            // Additional Include-invocation rows of a physically inserted
            // statement are part of the insert itself, not non-target changes.
            continue;
        }
        if (include_subtree_additions.find(element.edit_id) !=
            include_subtree_additions.end()) {
            // Elements of a swapped-in Include subtree belong to the update
            // itself, not to the untouched remainder of the route.
            continue;
        }
        if (resource_list_element_is_replaced(element, true)) {
            // A resource-list Load replacement owns only the selected list's
            // rows; every map element and every other list remains strict.
            continue;
        }
        if (before_position >= before_non_targets.size() ||
            element.canonical != before_non_targets[before_position]->canonical) {
            report.non_target_changed_count = 1;
            report.blocking_errors.push_back(
                "full reparse changed one or more non-target evaluated elements");
            return;
        }
        preserve_edit_identity(before_non_targets[before_position]->edit_id,
                               element.edit_id);
        ++before_position;
    }
    if (before_position != before_non_targets.size() ||
        candidate_target_ids.size() != expected_target_canonical.size()) {
        report.non_target_changed_count =
            before_position == before_non_targets.size() ? 0 : 1;
        report.blocking_errors.push_back(
            candidate_target_ids.size() != expected_target_canonical.size()
                ? "edited targets did not reparse to the expected semantic set"
                : "full reparse changed one or more non-target evaluated elements");
        return;
    }
    if (!report.blocking_errors.empty()) return;

    // editId contains global_order, so moving a statement can regenerate IDs for
    // both the target and every crossed element. The GUI intentionally keeps the
    // disk-baseline IDs while a batch is pending so it can reset and replay the
    // complete ledger. Preserve those validated identities in the working-copy
    // context instead of forcing the GUI to guess an old-to-new row mapping.
    candidate->native_element_edit_id_to_stable.clear();
    candidate->native_element_edit_id_to_stable.reserve(
        candidate_to_stable_edit_ids.size());
    for (const auto& identity : candidate_to_stable_edit_ids) {
        candidate->native_element_edit_id_to_stable.emplace(
            identity.first, identity.second);
    }
    candidate->element_edit_id_cache.clear();

    std::set<std::string> deleted_ids;
    std::set<std::string> inserted_ids;
    for (const MapEditChange& change : changes) {
        const std::string operation =
            ascii_lower(change.operation.empty() ? "update" : change.operation);
        if (operation == "delete") {
            deleted_ids.insert(change.edit_id);
        } else if (operation == "insert") {
            inserted_ids.insert(change.edit_id);
        }
    }
    deleted_ids.insert(include_subtree_deletions.begin(),
                       include_subtree_deletions.end());
    OwnTrackTransitionState expected_links = own_track_transition_state(baseline);
    for (auto it = expected_links.pairs.begin(); it != expected_links.pairs.end();) {
        if (deleted_ids.find(it->first) != deleted_ids.end() ||
            deleted_ids.find(it->second) != deleted_ids.end()) {
            it = expected_links.pairs.erase(it);
        } else {
            ++it;
        }
    }
    for (const std::string& edit_id : deleted_ids) {
        expected_links.orphan_transition_ids.erase(edit_id);
    }
    OwnTrackTransitionState candidate_links = own_track_transition_state(*candidate);
    if (!include_subtree_additions.empty()) {
        // Transition pairs and orphans that live entirely inside a swapped-in
        // Include subtree are part of the update itself.
        for (auto it = candidate_links.pairs.begin();
             it != candidate_links.pairs.end();) {
            if (include_subtree_additions.find(it->first) !=
                    include_subtree_additions.end() &&
                include_subtree_additions.find(it->second) !=
                    include_subtree_additions.end()) {
                expected_links.pairs.insert(*it);
                it = candidate_links.pairs.erase(it);
            } else {
                ++it;
            }
        }
        for (auto it = candidate_links.orphan_transition_ids.begin();
             it != candidate_links.orphan_transition_ids.end();) {
            if (include_subtree_additions.find(*it) !=
                include_subtree_additions.end()) {
                it = candidate_links.orphan_transition_ids.erase(it);
            } else {
                ++it;
            }
        }
    }
    for (const auto& pair : candidate_links.pairs) {
        const bool transition_inserted = inserted_ids.find(pair.first) != inserted_ids.end();
        const bool primary_inserted = inserted_ids.find(pair.second) != inserted_ids.end();
        if (!transition_inserted && !primary_inserted) continue;
        if (!transition_inserted || !primary_inserted) {
            report.blocking_errors.push_back(
                "inserted BeginTransition pairing crossed an existing source statement");
            return;
        }
        expected_links.pairs.insert(pair);
    }
    for (const std::string& edit_id : candidate_links.orphan_transition_ids) {
        if (inserted_ids.find(edit_id) != inserted_ids.end()) {
            report.blocking_errors.push_back(
                "inserted BeginTransition did not reparse as a paired transition");
            return;
        }
    }
    if (candidate_links.pairs != expected_links.pairs ||
        candidate_links.orphan_transition_ids != expected_links.orphan_transition_ids) {
        report.blocking_errors.push_back(
            "full reparse changed a BeginTransition pairing outside the requested edit");
        return;
    }

    candidate->disk_native_element_edit_id_to_stable =
        baseline.disk_native_element_edit_id_to_stable;
    candidate->disk_source_hashes_for_stable_ids =
        baseline.disk_source_hashes_for_stable_ids;
    if (candidate->disk_native_element_edit_id_to_stable.empty()) {
        if (!baseline.native_element_edit_id_to_stable.empty()) {
            candidate->disk_native_element_edit_id_to_stable =
                baseline.native_element_edit_id_to_stable;
        } else {
            candidate->disk_native_element_edit_id_to_stable.reserve(
                before_elements.size());
            for (const SemanticElement& element : before_elements) {
                candidate->disk_native_element_edit_id_to_stable.emplace(
                    element.edit_id, element.edit_id);
            }
        }
    }
    if (candidate->disk_source_hashes_for_stable_ids.empty()) {
        candidate->disk_source_hashes_for_stable_ids.reserve(
            baseline.source_files.size());
        for (const SourceFileRecord& file : baseline.source_files) {
            candidate->disk_source_hashes_for_stable_ids.emplace(
                file.source_key, file.source_hash);
        }
    }

    std::string derived_error;
    if (!validate_non_target_derived_state(
            baseline, *candidate, excluded_before, derived_error)) {
        report.non_target_changed_count = 1;
        report.blocking_errors.push_back(std::move(derived_error));
        return;
    }

    bool final_environment_ok = false;
    std::string environment_error;
    if (include_removed_statements.empty() && bound_include_added_statements.empty()) {
        final_environment_ok = variable_environment_equal(
            baseline.variables, baseline.distance,
            candidate->variables, candidate->distance);
        if (!final_environment_ok) {
            environment_error =
                "full reparse changed the final variable or distance environment";
        }
    } else if (bound_include_added_statements.empty()) {
        final_environment_ok = final_environment_matches_include_deletions(
            baseline, *candidate, include_removed_statements, environment_error);
    } else {
        final_environment_ok = final_environment_matches_include_edits(
            baseline, *candidate, include_removed_statements,
            include_added_statements, environment_error);
    }
    if (!final_environment_ok) {
        report.non_target_changed_count = 1;
        report.blocking_errors.push_back(std::move(environment_error));
        return;
    }

    report.target_distance_match_count = expected_distance_target_count;
    report.validation_fingerprint = after_snapshot.full_fingerprint;
    candidate->edit_validation_fingerprint = report.validation_fingerprint;
    candidate->edit_validation_current = true;
    report.validated_context = std::shared_ptr<MapContext>(candidate.release());
    report.full_reparse_ok = true;
}

DistanceBoundaryPlan find_boundary_by_token(const MapContext& ctx,
                                            const SourcePatch& patch,
                                            const DistanceSectionAnalysis& section,
                                            const std::string& token) {
    if (token.empty() || section.anchors.size() < 2) return {};
    const size_t first = std::min(section.first_position, section.anchors.size() - 1);
    const size_t last = std::min(section.last_position, section.anchors.size() - 1);
    for (size_t pos = first; pos < last && pos + 1 < section.anchors.size(); ++pos) {
        DistanceBoundaryPlan boundary = boundary_after_anchor(ctx, patch, section, pos);
        if (boundary.valid() && boundary.token == token) return boundary;
    }
    return {};
}

std::string common_group_expression(const DistanceEditGroup& group,
                                    const std::vector<PreparedEdit>& prepared,
                                    bool manual_only,
                                    bool& conflict) {
    conflict = false;
    std::string expression;
    for (size_t index : group.member_indices) {
        const PreparedEdit& edit = prepared[index];
        const std::string candidate = manual_only
            ? trim_field_copy(edit.change->distance_expression)
            : trim_field_copy(edit.suggested_distance_expression);
        if (candidate.empty()) continue;
        if (expression.empty()) expression = candidate;
        else if (expression != candidate) conflict = true;
    }
    return expression;
}

std::set<std::string> group_statement_variables(const DistanceEditGroup& group,
                                                const std::vector<PreparedEdit>& prepared,
                                                const std::string& distance_expression,
                                                bool include_distance_expression) {
    std::set<std::string> variables;
    for (size_t index : group.member_indices) {
        std::set<std::string> statement_variables =
            referenced_variables(prepared[index].replacement_statement);
        variables.insert(statement_variables.begin(), statement_variables.end());
    }
    if (include_distance_expression) {
        std::set<std::string> expression_variables = referenced_variables(distance_expression);
        variables.insert(expression_variables.begin(), expression_variables.end());
    }
    return variables;
}

bool validate_distance_group_environment(
    MapContext& ctx,
    const SourcePatch& patch,
    const DistanceEditGroup& group,
    const std::vector<PreparedEdit>& prepared,
    const DistanceBoundaryPlan& boundary,
    const std::string& distance_expression,
    bool create_distance_block,
    bool force_distance_expression_check,
    bool has_manual_distance_expression,
    bool confirm_environment_mismatch,
    const DistancePlanningIndex& distance_index,
    MapEditReport& report) {
    const ParsedStatement& destination_anchor = ctx.parsed_statements[
        group.section.anchors[boundary.before_anchor_position]];
    const bool expression_context_matters = create_distance_block ||
        force_distance_expression_check ||
        trim_field_copy(destination_anchor.distance_expression) !=
            trim_field_copy(distance_expression);
    const std::set<std::string> statement_variables =
        group_statement_variables(group, prepared, {}, false);
    const std::set<std::string> expression_variables = expression_context_matters
        ? referenced_variables(distance_expression)
        : std::set<std::string>{};

    if (expression_context_matters && !has_manual_distance_expression) {
        const std::string multi_value = first_multivalued_variable(
            ctx, group.section, distance_index, expression_variables);
        if (!multi_value.empty()) {
            append_resolution_request(ctx, patch, group, prepared,
                                      "variableHasMultipleContextValues",
                                      multi_value, false, boundary.token, report);
            return false;
        }
    }

    std::string statement_mismatch;
    std::string expression_mismatch;
    for (size_t index : group.member_indices) {
        const PreparedEdit& edit = prepared[index];
        const ParsedStatement& origin = ctx.parsed_statements[edit.target.statement_index];
        if (statement_mismatch.empty()) {
            statement_mismatch = first_environment_mismatch(
                origin.variable_environment, boundary.variable_environment,
                statement_variables);
        }
        if (expression_mismatch.empty() && expression_context_matters) {
            expression_mismatch = first_environment_mismatch(
                origin.variable_environment, boundary.variable_environment,
                expression_variables);
        }
    }
    if (create_distance_block &&
        std::any_of(group.member_indices.begin(), group.member_indices.end(),
                    [&](size_t index) {
                        return expression_references_predefined_distance(
                            prepared[index].replacement_statement);
                    })) {
        statement_mismatch = statement_mismatch.empty() ? "distance" : statement_mismatch;
    }
    if (create_distance_block &&
        expression_references_predefined_distance(distance_expression)) {
        expression_mismatch = expression_mismatch.empty() ? "distance" : expression_mismatch;
    }

    if (!statement_mismatch.empty() && !confirm_environment_mismatch) {
        const std::string multi_value = first_multivalued_variable(
            ctx, group.section, distance_index, statement_variables);
        append_resolution_request(ctx, patch, group, prepared,
                                  multi_value.empty()
                                      ? "incompatibleEvaluationEnvironment"
                                      : "variableHasMultipleContextValues",
                                  multi_value.empty() ? statement_mismatch : multi_value,
                                  !create_distance_block, boundary.token, report);
        return false;
    }
    if (!expression_mismatch.empty() &&
        !confirm_environment_mismatch && !has_manual_distance_expression) {
        append_resolution_request(ctx, patch, group, prepared,
                                  "incompatibleEvaluationEnvironment",
                                  expression_mismatch, !create_distance_block,
                                  boundary.token, report);
        return false;
    }
    return true;
}

bool resolve_distance_group(MapContext& ctx,
                            const SourcePatch& patch,
                            const DistanceEditGroup& group,
                            const std::vector<PreparedEdit>& prepared,
                            DistancePlanningIndex& distance_index,
                            ResolvedDistanceGroup& resolved,
                            MapEditReport& report) {
    resolved.group = &group;
    resolved.direction = group.section.direction;
    if (group.member_indices.empty()) return false;

    for (size_t index : group.member_indices) {
        const MapEditChange& change = *prepared[index].change;
        if (!change.distance_resolution_key.empty() &&
            change.distance_resolution_key != group.key) {
            append_resolution_request(ctx, patch, group, prepared,
                                      "staleDistanceResolution", {}, false, {}, report);
            return false;
        }
    }

    std::string selected_boundary_token;
    bool boundary_token_conflict = false;
    bool confirm_environment_mismatch = false;
    for (size_t index : group.member_indices) {
        const MapEditChange& change = *prepared[index].change;
        if (!change.distance_boundary_token.empty()) {
            if (selected_boundary_token.empty()) {
                selected_boundary_token = change.distance_boundary_token;
            } else if (selected_boundary_token != change.distance_boundary_token) {
                boundary_token_conflict = true;
            }
        }
        confirm_environment_mismatch =
            confirm_environment_mismatch || change.confirm_environment_mismatch;
    }
    if (boundary_token_conflict) {
        append_resolution_request(ctx, patch, group, prepared,
                                  "conflictingManualBoundaries", {}, false, {}, report);
        return false;
    }

    bool manual_expression_conflict = false;
    std::string manual_expression =
        common_group_expression(group, prepared, true, manual_expression_conflict);
    if (manual_expression_conflict) {
        append_resolution_request(ctx, patch, group, prepared,
                                  "conflictingManualDistanceExpressions", {}, false, {}, report);
        return false;
    }

    if (!selected_boundary_token.empty()) {
        DistanceBoundaryPlan boundary = find_boundary_by_token(
            ctx, patch, group.section, selected_boundary_token);
        if (!boundary.valid()) {
            append_resolution_request(ctx, patch, group, prepared,
                                      "staleDistanceBoundary", {}, false, {}, report);
            return false;
        }
        const ParsedStatement& before =
            ctx.parsed_statements[group.section.anchors[boundary.before_anchor_position]];
        bool expression_conflict = false;
        std::string expression = manual_expression;
        if (expression.empty()) {
            expression = common_group_expression(group, prepared, false, expression_conflict);
        }
        if (expression_conflict || expression.empty()) {
            append_resolution_request(ctx, patch, group, prepared,
                                      "distanceExpressionRequiresManualEdit", {}, false,
                                      selected_boundary_token, report);
            return false;
        }
        resolved.boundary = std::move(boundary);
        resolved.create_distance_block =
            !exact_distance_value(before.distance_value, group.target_distance) ||
            (!manual_expression.empty() &&
             trim_field_copy(before.distance_expression) != manual_expression);
        resolved.distance_expression = std::move(expression);
        if (!validate_distance_group_environment(
                ctx, patch, group, prepared, resolved.boundary,
                resolved.distance_expression, resolved.create_distance_block,
                true, !manual_expression.empty(),
                confirm_environment_mismatch, distance_index, report)) {
            return false;
        }
        return true;
    }

    if (!group.section.resolved) {
        append_resolution_request(ctx, patch, group, prepared,
                                  "ambiguousSourceSection", {}, false, {}, report);
        return false;
    }

    std::vector<size_t> numeric_positions;
    for (size_t pos = group.section.first_position;
         pos <= group.section.last_position && pos < group.section.anchors.size(); ++pos) {
        const ParsedStatement& anchor = ctx.parsed_statements[group.section.anchors[pos]];
        if (exact_distance_value(anchor.distance_value, group.target_distance)) {
            numeric_positions.push_back(pos);
        }
    }
    if (numeric_positions.size() > 1) {
        append_resolution_request(ctx, patch, group, prepared,
                                  "multipleEquivalentDistanceBlocks", {}, false, {}, report);
        return false;
    }

    bool suggested_expression_conflict = false;
    std::string suggested_expression = common_group_expression(
        group, prepared, false, suggested_expression_conflict);
    if (suggested_expression.empty()) {
        append_resolution_request(ctx, patch, group, prepared,
                                  "distanceExpressionRequiresManualEdit", {}, false, {}, report);
        return false;
    }

    size_t destination_before_position = k_no_source_ref;
    bool create_distance_block = false;
    if (numeric_positions.size() == 1) {
        destination_before_position = numeric_positions.front();
    } else {
        std::vector<size_t> bracket_positions;
        for (size_t pos = group.section.first_position;
             pos < group.section.last_position && pos + 1 < group.section.anchors.size(); ++pos) {
            double before = ctx.parsed_statements[group.section.anchors[pos]].distance_value;
            double after = ctx.parsed_statements[group.section.anchors[pos + 1]].distance_value;
            bool bracketed = group.section.direction == "increasing"
                ? before < group.target_distance && group.target_distance < after
                : before > group.target_distance && group.target_distance > after;
            if (bracketed) bracket_positions.push_back(pos);
        }
        if (bracket_positions.size() != 1) {
            append_resolution_request(ctx, patch, group, prepared,
                                      bracket_positions.empty()
                                          ? "noUniqueDistanceBracket"
                                          : "multipleDistanceBrackets",
                                      {}, false, {}, report);
            return false;
        }
        destination_before_position = bracket_positions.front();
        create_distance_block = true;
    }

    DistanceBoundaryPlan boundary = boundary_after_anchor(
        ctx, patch, group.section, destination_before_position);
    if (!boundary.valid() && !create_distance_block &&
        destination_before_position + 1 == group.section.anchors.size()) {
        boundary = terminal_boundary_for_last_anchor(
            ctx, patch, group.section, destination_before_position, distance_index);
    }
    if (!boundary.valid()) {
        append_resolution_request(ctx, patch, group, prepared,
                                  "destinationBoundaryUnavailable", {}, false, {}, report);
        return false;
    }

    bool has_manual_distance_expression = false;
    for (size_t index : group.member_indices) {
        has_manual_distance_expression = has_manual_distance_expression ||
            !trim_field_copy(prepared[index].change->distance_expression).empty();
    }
    if (suggested_expression_conflict && create_distance_block) {
        append_resolution_request(ctx, patch, group, prepared,
                                  "distanceExpressionRequiresManualEdit", {}, false,
                                  boundary.token, report);
        return false;
    }

    if (!validate_distance_group_environment(
            ctx, patch, group, prepared, boundary, suggested_expression,
            create_distance_block, false, has_manual_distance_expression,
            confirm_environment_mismatch, distance_index, report)) {
        return false;
    }

    resolved.boundary = std::move(boundary);
    resolved.create_distance_block = create_distance_block;
    resolved.distance_expression = suggested_expression;
    return true;
}

bool physical_include_instances_are_compatible(
    MapContext& ctx,
    const SourcePatch& patch,
    const DistanceEditGroup& group,
    const std::vector<PreparedEdit>& prepared,
    const std::unordered_set<size_t>& targeted_statement_indices,
    const ResolvedDistanceGroup& resolved,
    DistancePlanningIndex& distance_index,
    std::string& mismatch_variable) {
    const bool user_selected_boundary = std::any_of(
        group.member_indices.begin(), group.member_indices.end(),
        [&](size_t index) {
            return !prepared[index].change->distance_boundary_token.empty();
        });
    const bool primary_terminal = resolved.boundary.terminal_context_boundary;
    size_t primary_after_byte = 0;
    if (!primary_terminal) {
        const size_t primary_after_index =
            group.section.anchors[resolved.boundary.after_anchor_position];
        primary_after_byte = ctx.parsed_statements[primary_after_index].source.byte_start;
    }
    for (size_t member_index : group.member_indices) {
        const PreparedEdit& member = prepared[member_index];
        const ParsedStatement& primary = ctx.parsed_statements[member.target.statement_index];
        for (size_t statement_index : distance_index.physical_counterparts(primary)) {
            if (statement_index == member.target.statement_index) continue;
            const ParsedStatement& counterpart = ctx.parsed_statements[statement_index];
            if (same_statement_context(ctx, counterpart.source, primary.source)) {
                continue;
            }
            if (targeted_statement_indices.find(statement_index) !=
                targeted_statement_indices.end()) {
                // The physical source rewrite necessarily affects every Include
                // invocation. A separately planned counterpart is validated by its
                // own group and by the final whole-map semantic comparison.
                continue;
            }

            DistanceSectionAnalysis section =
                analyze_distance_section(ctx, statement_index, distance_index);
            if (!section.resolved) return false;
            size_t before_position = k_no_source_ref;
            bool create_block = false;
            if (user_selected_boundary) {
                for (size_t pos = 0; pos + 1 < section.anchors.size(); ++pos) {
                    const ParsedStatement& after =
                        ctx.parsed_statements[section.anchors[pos + 1]];
                    if (after.source.byte_start != primary_after_byte) continue;
                    if (before_position != k_no_source_ref) return false;
                    before_position = pos;
                }
                create_block = resolved.create_distance_block;
            } else {
                std::vector<size_t> numeric_positions;
                for (size_t pos = section.first_position; pos <= section.last_position; ++pos) {
                    if (exact_distance_value(
                            ctx.parsed_statements[section.anchors[pos]].distance_value,
                            group.target_distance)) {
                        numeric_positions.push_back(pos);
                    }
                }
                if (numeric_positions.size() == 1) {
                    before_position = numeric_positions.front();
                } else if (numeric_positions.empty()) {
                    for (size_t pos = section.first_position; pos < section.last_position; ++pos) {
                        const double before =
                            ctx.parsed_statements[section.anchors[pos]].distance_value;
                        const double after =
                            ctx.parsed_statements[section.anchors[pos + 1]].distance_value;
                        const bool bracketed = section.direction == "increasing"
                            ? before < group.target_distance && group.target_distance < after
                            : before > group.target_distance && group.target_distance > after;
                        if (!bracketed) continue;
                        if (before_position != k_no_source_ref) return false;
                        before_position = pos;
                    }
                    create_block = true;
                } else {
                    return false;
                }
            }
            if (before_position == k_no_source_ref) return false;
            DistanceBoundaryPlan boundary = boundary_after_anchor(
                ctx, patch, section, before_position);
            if (!boundary.valid() && !create_block &&
                before_position + 1 == section.anchors.size()) {
                boundary = terminal_boundary_for_last_anchor(
                    ctx, patch, section, before_position, distance_index);
            }
            const bool same_physical_boundary = boundary.valid() &&
                boundary.terminal_context_boundary == primary_terminal &&
                (primary_terminal
                    ? boundary.insert_offset == resolved.boundary.insert_offset
                    : ctx.parsed_statements[section.anchors[boundary.after_anchor_position]]
                          .source.byte_start == primary_after_byte);
            if (!same_physical_boundary ||
                create_block != resolved.create_distance_block) {
                return false;
            }

            std::set<std::string> statement_variables =
                referenced_variables(member.replacement_statement);
            mismatch_variable = first_environment_mismatch(
                counterpart.variable_environment, boundary.variable_environment,
                statement_variables);
            if (!mismatch_variable.empty()) return false;
            if (create_block || user_selected_boundary) {
                std::set<std::string> expression_variables =
                    referenced_variables(resolved.distance_expression);
                mismatch_variable = first_environment_mismatch(
                    counterpart.variable_environment, boundary.variable_environment,
                    expression_variables);
                if (!mismatch_variable.empty()) return false;
            }
        }
    }
    return true;
}

void replace_context_and_advance_revisions(MapContext& current, MapContext&& replacement) {
    const std::uint64_t next_content_revision = current.content_revision + 1;
    const std::uint64_t next_geometry_revision = current.geometry_revision + 1;
    const std::uint64_t next_scene_revision = current.scene_revision + 1;
    current = std::move(replacement);
    current.content_revision = next_content_revision;
    current.geometry_revision = next_geometry_revision;
    current.scene_revision = next_scene_revision;
    current.map_snapshot.reset();
    current.scene_snapshot.reset();
    current.edit_target_snapshot.reset();
    current.edit_report_snapshot.reset();
}

MapEditReport build_edit_report(MapContext& ctx,
                                const std::vector<MapEditChange>& changes,
                                bool write_files) {
    MapEditReport report;
    std::map<size_t, SourcePatch> patches;
    DistancePlanningIndex distance_index(ctx);
    std::set<std::string> map_source_keys;
    for (const FileStructureRecord& record : ctx.file_structure) {
        if (!record.absolute_path.empty()) {
            map_source_keys.insert(normalized_source_key(record.absolute_path));
        }
    }

    auto change_signature = [](const MapEditChange& change) {
        std::ostringstream out;
        out << change.operation << "\n" << change.replacement_statement << "\n"
            << change.target_file_path << "\n" << change.insert_before_edit_id << "\n"
            << change.expected_source_hash << "\n" << change.distance_resolution_key << "\n"
            << change.distance_boundary_token << "\n" << change.distance_expression << "\n"
            << change.confirm_environment_mismatch << "\n";
        for (const auto& field : change.field_changes) {
            out << field.first.size() << ":" << field.first << "="
                << field.second.size() << ":" << field.second << "\n";
        }
        return out.str();
    };

    std::map<std::string, std::string> edit_signatures;
    std::vector<const MapEditChange*> effective_changes;
    effective_changes.reserve(changes.size());
    for (const MapEditChange& change : changes) {
        if (change.edit_id.empty()) {
            report.blocking_errors.push_back("edit change is missing editId");
            continue;
        }
        std::string signature = change_signature(change);
        auto existing = edit_signatures.find(change.edit_id);
        if (existing != edit_signatures.end()) {
            if (existing->second != signature) {
                report.blocking_errors.push_back(
                    "conflicting duplicate editId in one edit batch: " + change.edit_id);
            }
            continue;
        }
        edit_signatures.emplace(change.edit_id, std::move(signature));
        effective_changes.push_back(&change);
    }
    if (!report.blocking_errors.empty()) return report;

    {
        const OwnTrackTransitionState transition_state = own_track_transition_state(ctx);
        std::map<std::string, std::string> paired_ids;
        for (const auto& pair : transition_state.pairs) {
            paired_ids[pair.first] = pair.second;
            paired_ids[pair.second] = pair.first;
        }
        std::set<std::string> delete_ids;
        for (const MapEditChange* change : effective_changes) {
            if (ascii_lower(change->operation.empty() ? "update" : change->operation) == "delete") {
                delete_ids.insert(change->edit_id);
            }
        }
        for (const std::string& edit_id : delete_ids) {
            if (transition_state.orphan_transition_ids.find(edit_id) !=
                transition_state.orphan_transition_ids.end()) {
                report.blocking_errors.push_back(
                    "BeginTransition cannot be deleted without a paired Begin/End: " + edit_id);
                continue;
            }
            auto paired = paired_ids.find(edit_id);
            if (paired != paired_ids.end() && delete_ids.find(paired->second) == delete_ids.end()) {
                report.blocking_errors.push_back(
                    "paired BeginTransition and Begin/End must be deleted together: " + edit_id);
            }
        }
    }
    if (!report.blocking_errors.empty()) return report;

    validate_other_track_key_renames(ctx, effective_changes, report);
    if (!report.blocking_errors.empty()) return report;
    validate_repeater_key_renames(ctx, effective_changes, report);
    if (!report.blocking_errors.empty()) return report;
    validate_repeater_insert_key_overlaps(ctx, effective_changes, report);
    if (!report.blocking_errors.empty()) return report;

    std::vector<PreparedEdit> prepared;
    prepared.reserve(effective_changes.size());
    for (size_t input_ordinal = 0; input_ordinal < effective_changes.size(); ++input_ordinal) {
        const MapEditChange& change = *effective_changes[input_ordinal];
        std::string operation = ascii_lower(change.operation.empty() ? "update" : change.operation);
        EditableTarget target;
        const ParsedStatement* statement = nullptr;
        SourcePatch* patch = nullptr;
        if (operation != "insert") {
            target = find_editable_target(ctx, change.edit_id);
            if (target.statement_index == k_no_source_ref ||
                target.statement_index >= ctx.parsed_statements.size()) {
                report.blocking_errors.push_back("unsupported or unknown editId: " + change.edit_id);
                continue;
            }

            statement = &ctx.parsed_statements[target.statement_index];
            if (statement->source.source_file_index >= ctx.source_files.size()) {
                report.blocking_errors.push_back("edit target has no source file: " + change.edit_id);
                continue;
            }
            const SourceFileRecord& file = ctx.source_files[statement->source.source_file_index];
            SourcePatch& loaded_patch = patches[statement->source.source_file_index];
            if (!loaded_patch.record) {
                try {
                    loaded_patch = load_source_patch(ctx, file);
                } catch (const std::exception& e) {
                    report.blocking_errors.push_back(e.what());
                    continue;
                }
            }
            const std::string& expected_hash = change.expected_source_hash.empty()
                ? file.source_hash
                : change.expected_source_hash;
            if (!expected_hash.empty() && loaded_patch.current_hash != expected_hash &&
                loaded_patch.base_hash != expected_hash) {
                report.blocking_errors.push_back("source file changed externally: " + file.file_path);
                continue;
            }
            patch = &loaded_patch;
        }

        try {
            if (operation == "insert") {
                if (change.row_kind.empty()) {
                    report.blocking_errors.push_back(
                        "insert edit is missing its rowKind field: " + change.edit_id);
                    continue;
                }
                if (change.target_file_path.empty()) {
                    report.blocking_errors.push_back(
                        "insert edit is missing its target file path: " + change.edit_id);
                    continue;
                }
                const size_t target_file_index =
                    find_source_file_index(ctx, change.target_file_path);
                if (target_file_index == k_no_source_ref) {
                    report.blocking_errors.push_back(
                        "insert target file is not part of the loaded map: " +
                        change.target_file_path);
                    continue;
                }
                const SourceFileRecord& file = ctx.source_files[target_file_index];
                SourcePatch& target_patch = patches[target_file_index];
                if (!target_patch.record) {
                    try {
                        target_patch = load_source_patch(ctx, file);
                    } catch (const std::exception& e) {
                        report.blocking_errors.push_back(e.what());
                        continue;
                    }
                }
                const std::string& expected_hash = change.expected_source_hash.empty()
                    ? file.source_hash
                    : change.expected_source_hash;
                if (!expected_hash.empty() &&
                    target_patch.current_hash != expected_hash &&
                    target_patch.base_hash != expected_hash) {
                    report.blocking_errors.push_back(
                        "source file changed externally: " + file.file_path);
                    continue;
                }
                if (const ResourceListEditSpec* content_spec =
                        resource_list_edit_spec_for_content_row_kind(change.row_kind)) {
                    try {
                        const ResourceListContentInsertionPlan insertion =
                            plan_resource_list_content_insertion(
                                ctx, change, *content_spec, target_file_index,
                                target_patch);
                        const std::string inserted_statement = build_insert_statement(
                            change, newline_text(target_patch.record->newline));
                        PreparedEdit edit;
                        edit.change = &change;
                        edit.input_ordinal = input_ordinal;
                        edit.operation = "insert";
                        edit.target.statement_index = insertion.anchor_statement_index;
                        edit.target.row_kind = change.row_kind;
                        edit.target.element_index = 0;
                        edit.target.elements_for_statement = 1;
                        edit.source_file_index = target_file_index;
                        edit.source_range = {insertion.offset, insertion.offset};
                        edit.removal_range = {};
                        edit.replacement_statement = insertion.indent + inserted_statement;
                        edit.has_custom_identity_range = true;
                        edit.identity_range_begin = insertion.indent.size();
                        edit.identity_range_end = edit.replacement_statement.size();
                        ++report.insert_count;
                        prepared.push_back(std::move(edit));
                    } catch (const std::exception& e) {
                        report.blocking_errors.push_back(
                            std::string("edit change failed for ") +
                            change.edit_id + ": " + e.what());
                    }
                    continue;
                }
                if (change.row_kind == "include" ||
                    change.row_kind == "resourceList.load") {
                    try {
                        if (change.row_kind == "resourceList.load") {
                            validate_resource_list_insert_header(ctx, change);
                        }
                        const std::string inserted_statement = build_insert_statement(change);
                        const ReferenceInsertionPlan insertion = plan_reference_insertion(
                            ctx, target_file_index, target_patch, inserted_statement);
                        PreparedEdit edit;
                        edit.change = &change;
                        edit.input_ordinal = input_ordinal;
                        edit.operation = "insert";
                        edit.target.statement_index = insertion.anchor_statement_index;
                        edit.target.row_kind = change.row_kind;
                        edit.target.element_index = 0;
                        edit.target.elements_for_statement = 0;
                        edit.source_file_index = target_file_index;
                        edit.source_range = {insertion.offset, insertion.offset};
                        edit.removal_range = {};
                        edit.replacement_statement = insertion.statement;
                        edit.has_custom_identity_range = true;
                        edit.identity_range_begin = insertion.identity_begin;
                        edit.identity_range_end = insertion.identity_end;
                        ++report.insert_count;
                        prepared.push_back(std::move(edit));
                    } catch (const std::exception& e) {
                        report.blocking_errors.push_back(
                            std::string("edit change failed for ") +
                            change.edit_id + ": " + e.what());
                    }
                    continue;
                }
                try {
                    const std::string target_text = normalized_number_arg(
                        field_text_or(change, "distance", ""));
                    double target_distance = 0.0;
                    if (!parse_edit_number(target_text, target_distance)) {
                        throw std::runtime_error("invalid numeric edit value: " + target_text);
                    }
                    const bool target_has_parsed_statement = std::any_of(
                        ctx.parsed_statements.begin(), ctx.parsed_statements.end(),
                        [&](const ParsedStatement& candidate) {
                            return candidate.source.source_file_index == target_file_index;
                        });
                    if (!target_has_parsed_statement &&
                        map_source_keys.find(file.source_key) != map_source_keys.end()) {
                        PreparedEdit edit;
                        edit.change = &change;
                        edit.input_ordinal = input_ordinal;
                        edit.operation = "insert";
                        edit.target.statement_index = k_no_source_ref;
                        edit.target.row_kind = change.row_kind;
                        edit.target.element_index = 0;
                        edit.target.elements_for_statement = 1;
                        edit.source_file_index = target_file_index;
                        edit.source_range = {target_patch.text.size(), target_patch.text.size()};
                        edit.removal_range = {};
                        edit.replacement_statement = change.replacement_statement.empty()
                            ? build_insert_statement(change)
                            : trim_field_copy(change.replacement_statement);
                        if (edit.replacement_statement.empty()) {
                            throw std::runtime_error("insert produced an empty statement");
                        }
                        edit.target_distance = target_distance;
                        edit.initial_empty_source_insert = true;
                        ++report.insert_count;
                        prepared.push_back(std::move(edit));
                        continue;
                    }
                    // The insert has no existing map element to anchor to. Pick
                    // only a parser-recognized numeric distance statement in the
                    // target physical file. Other statements carry the current
                    // distance as metadata too, but are not valid source
                    // boundaries for a new distance block.
                    auto section_can_place_target = [&](const DistanceSectionAnalysis& section) {
                        if (!section.resolved || section.anchors.empty()) return false;
                        const size_t first = std::min(
                            section.first_position, section.anchors.size() - 1);
                        const size_t last = std::min(
                            section.last_position, section.anchors.size() - 1);
                        size_t exact_count = 0;
                        for (size_t pos = first; pos <= last; ++pos) {
                            if (exact_distance_value(
                                    ctx.parsed_statements[section.anchors[pos]].distance_value,
                                    target_distance)) {
                                ++exact_count;
                            }
                        }
                        if (exact_count > 1) return false;
                        if (exact_count == 1) return true;
                        size_t bracket_count = 0;
                        for (size_t pos = first;
                             pos < last && pos + 1 < section.anchors.size(); ++pos) {
                            const double before = ctx.parsed_statements[
                                section.anchors[pos]].distance_value;
                            const double after = ctx.parsed_statements[
                                section.anchors[pos + 1]].distance_value;
                            const bool bracketed = section.direction == "increasing"
                                ? before < target_distance && target_distance < after
                                : before > target_distance && target_distance > after;
                            if (bracketed) ++bracket_count;
                        }
                        return bracket_count == 1;
                    };

                    size_t origin_index = k_no_source_ref;
                    DistanceSectionAnalysis selected_section;
                    size_t fallback_origin_index = k_no_source_ref;
                    DistanceSectionAnalysis fallback_section;
                    double best_gap = std::numeric_limits<double>::max();
                    double best_resolved_gap = std::numeric_limits<double>::max();
                    for (size_t i = 0; i < ctx.parsed_statements.size(); ++i) {
                        const ParsedStatement& candidate = ctx.parsed_statements[i];
                        if (candidate.source.source_file_index != target_file_index ||
                            !is_distance_statement(candidate)) {
                            continue;
                        }
                        const double gap = std::fabs(candidate.distance_value - target_distance);
                        const auto tie_breaks_before = [&](size_t current_index) {
                            return candidate.source.byte_start <
                                    ctx.parsed_statements[current_index].source.byte_start ||
                                (candidate.source.byte_start ==
                                     ctx.parsed_statements[current_index].source.byte_start &&
                                 source_context_identity(ctx, candidate.source) <
                                     source_context_identity(
                                         ctx, ctx.parsed_statements[current_index].source));
                        };
                        if (fallback_origin_index == k_no_source_ref ||
                            gap < best_gap ||
                            (gap == best_gap && tie_breaks_before(fallback_origin_index))) {
                            fallback_origin_index = i;
                            fallback_section = analyze_distance_section(
                                ctx, i, distance_index);
                            best_gap = gap;
                        }
                        const DistanceSectionAnalysis section = analyze_distance_section(
                            ctx, i, distance_index);
                        if (!section_can_place_target(section)) continue;
                        if (origin_index == k_no_source_ref ||
                            gap < best_resolved_gap ||
                            (gap == best_resolved_gap && tie_breaks_before(origin_index))) {
                            origin_index = i;
                            selected_section = section;
                            best_resolved_gap = gap;
                        }
                    }
                    if (origin_index == k_no_source_ref) {
                        origin_index = fallback_origin_index;
                        selected_section = fallback_section;
                    }
                    if (origin_index == k_no_source_ref) {
                        report.blocking_errors.push_back(
                            "insert target file contains no numeric distance statements: " +
                            file.file_path);
                        continue;
                    }
                    /*
                     * The selected section is either a parser-confirmed unique
                     * placement window or the nearest local window, which lets
                     * the existing manual-boundary workflow explain the
                     * ambiguity without scanning the whole source file.
                     */
                    const ParsedStatement& origin = ctx.parsed_statements[origin_index];
                    auto range = source_range_in_text(target_patch, origin.source);
                    PreparedEdit edit;
                    edit.change = &change;
                    edit.input_ordinal = input_ordinal;
                    edit.operation = "insert";
                    edit.target.statement_index = origin_index;
                    edit.target.row_kind = change.row_kind;
                    edit.target.element_index = 0;
                    edit.target.elements_for_statement = 1;
                    edit.source_file_index = target_file_index;
                    edit.source_range = range;
                    edit.removal_range = {};
                    const size_t source_line_start = offset_from_line_column(
                        target_patch.text, target_patch.line_starts, origin.source.line, 1);
                    if (source_line_start != std::string::npos &&
                        source_line_start <= range.first) {
                        std::string indent = target_patch.text.substr(
                            source_line_start, range.first - source_line_start);
                        if (std::all_of(indent.begin(), indent.end(), [](char ch) {
                                return ch == ' ' || ch == '\t';
                            })) {
                            edit.source_indent = std::move(indent);
                        }
                    }
                    edit.replacement_statement = change.replacement_statement.empty()
                        ? build_insert_statement(change)
                        : trim_field_copy(change.replacement_statement);
                    if (edit.replacement_statement.empty()) {
                        throw std::runtime_error("insert produced an empty statement");
                    }
                    edit.target_distance = target_distance;
                    edit.moves_distance = true;
                    edit.suggested_distance_expression =
                        suggested_distance_expression_for_target(origin, target_distance, change);
                    edit.section = std::move(selected_section);
                    ++report.insert_count;
                    prepared.push_back(std::move(edit));
                } catch (const std::exception& e) {
                    report.blocking_errors.push_back(
                        std::string("edit change failed for ") + change.edit_id + ": " + e.what());
                }
                continue;
            }
            auto range = source_range_in_text(*patch, statement->source);
            PreparedEdit edit;
            edit.change = &change;
            edit.input_ordinal = input_ordinal;
            edit.target = target;
            edit.source_file_index = statement->source.source_file_index;
            edit.source_range = range;
            edit.removal_range = safe_statement_removal_range(*patch, range);
            const size_t source_line_start = offset_from_line_column(
                patch->text, patch->line_starts, statement->source.line, 1);
            if (source_line_start != std::string::npos &&
                source_line_start <= range.first) {
                std::string indent = patch->text.substr(
                    source_line_start, range.first - source_line_start);
                if (std::all_of(indent.begin(), indent.end(), [](char ch) {
                        return ch == ' ' || ch == '\t';
                    })) {
                    edit.source_indent = std::move(indent);
                }
            }
            edit.operation = operation;
            if (operation == "delete") {
                if (target.row_kind != "include" &&
                    target.elements_for_statement != 1) {
                    report.blocking_errors.push_back("delete is blocked because the source statement maps to multiple elements: " + change.edit_id);
                    continue;
                }
                ++report.delete_count;
            } else if (operation == "update") {
                if (target.row_kind == "include" ||
                    target.row_kind == "resourceList.load") {
                    // These source statements own no typed element rows. Their
                    // supported update rewrites only the path argument; full
                    // reparse validation proves the swapped reference.
                    if (target.row_kind == "resourceList.load") {
                        validate_resource_list_header(ctx, *statement, change);
                        edit.replacement_statement =
                            build_resource_list_load_statement(change, *statement);
                    } else {
                        edit.replacement_statement = build_include_statement(
                            change, *statement);
                    }
                    ++report.update_count;
                } else {
                    if (target.elements_for_statement != 1) {
                        report.blocking_errors.push_back("update is blocked because the source statement maps to multiple elements: " + change.edit_id);
                        continue;
                    }
                    edit.replacement_statement = target.row_kind == "signal.aspect"
                        ? build_signal_aspect_statement(
                            change, *statement, newline_text(patch->record->newline))
                        : build_replacement_statement(change, *statement, target);
                    if (has_field_change(change, "distance")) {
                        const std::string target_text = normalized_number_arg(
                            field_text_or(change, "distance",
                                          fallback_edit_number(statement->distance_value)));
                        if (!parse_edit_number(target_text, edit.target_distance)) {
                            throw std::runtime_error("invalid numeric edit value: " + target_text);
                        }
                        edit.moves_distance =
                            !exact_distance_value(edit.target_distance, statement->distance_value);
                        if (edit.moves_distance) {
                            edit.suggested_distance_expression =
                                suggested_distance_expression_for_target(
                                    *statement, edit.target_distance, change);
                            edit.section = analyze_distance_section(
                                ctx, target.statement_index, distance_index);
                        }
                    }
                    ++report.update_count;
                }
            } else {
                report.blocking_errors.push_back("unknown edit operation: " + change.operation);
                continue;
            }
            prepared.push_back(std::move(edit));
        } catch (const std::exception& e) {
            report.blocking_errors.push_back(std::string("edit change failed for ") + change.edit_id + ": " + e.what());
        }
    }

    if (!report.blocking_errors.empty()) return report;

    validate_own_track_transition_insert_pairs(prepared, report);
    if (!report.blocking_errors.empty()) return report;

    std::map<std::string, DistanceEditGroup> distance_groups;
    for (size_t index = 0; index < prepared.size(); ++index) {
        PreparedEdit& edit = prepared[index];
        if (!edit.moves_distance) continue;
        const ParsedStatement& statement = ctx.parsed_statements[edit.target.statement_index];
        const std::string key = source_section_key(
            ctx, edit.section, statement, edit.target_distance);
        DistanceEditGroup& group = distance_groups[key];
        if (group.member_indices.empty()) {
            group.key = key;
            group.source_file_index = edit.source_file_index;
            group.target_distance = edit.target_distance;
            group.section = edit.section;
        }
        group.member_indices.push_back(index);
    }
    report.distance_group_count = static_cast<int>(distance_groups.size());

    std::vector<ResolvedDistanceGroup> resolved_groups;
    resolved_groups.reserve(distance_groups.size());
    std::unordered_set<size_t> targeted_distance_statement_indices;
    targeted_distance_statement_indices.reserve(prepared.size());
    for (const PreparedEdit& edit : prepared) {
        if (edit.moves_distance && edit.operation != "insert") {
            // Insert edits only borrow an origin statement for distance
            // planning; that statement is not moved, so its include
            // counterparts must still be validated by
            // physical_include_instances_are_compatible().
            targeted_distance_statement_indices.insert(edit.target.statement_index);
        }
    }
    for (auto& group_entry : distance_groups) {
        DistanceEditGroup& group = group_entry.second;
        SourcePatch& patch = patches[group.source_file_index];
        ResolvedDistanceGroup resolved;
        if (resolve_distance_group(
                ctx, patch, group, prepared, distance_index, resolved, report)) {
            std::string mismatch_variable;
            if (!physical_include_instances_are_compatible(
                    ctx, patch, group, prepared,
                    targeted_distance_statement_indices, resolved,
                    distance_index, mismatch_variable)) {
                append_resolution_request(ctx, patch, group, prepared,
                                          "physicalSourceHasIncompatibleIncludeContexts",
                                          mismatch_variable, false,
                                          resolved.boundary.token, report);
            } else {
                resolved_groups.push_back(std::move(resolved));
            }
        }
    }
    if (!report.ok()) {
        report.previews.clear();
        report.patched_files.clear();
        report.changed_files.clear();
        return report;
    }

    struct InsertionStatement {
        size_t source_offset = 0;
        size_t input_ordinal = 0;
        std::string text;
        size_t statement_begin = 0;
        size_t statement_length = 0;
        std::vector<TextReplacementIdentity> identities;
    };
    struct InsertionPart {
        double target_distance = 0.0;
        std::string direction;
        bool create_distance_block = false;
        std::string distance_expression;
        std::string distance_indent;
        std::vector<InsertionStatement> statements;
        std::string identity;
        size_t first_source_offset = std::numeric_limits<size_t>::max();
    };
    std::map<std::pair<size_t, size_t>, std::vector<InsertionPart>> insertion_parts;
    std::map<std::tuple<size_t, size_t, size_t>, std::string> replacement_by_range;

    auto source_identity_range = [&](const PreparedEdit& identity_edit,
                                     const std::string& replacement_text) {
        if (identity_edit.has_custom_identity_range) {
            if (identity_edit.identity_range_end < identity_edit.identity_range_begin ||
                identity_edit.identity_range_end > replacement_text.size()) {
                throw std::runtime_error("include insert identity range is outside its replacement");
            }
            return std::make_pair(identity_edit.identity_range_begin,
                                  identity_edit.identity_range_end);
        }
        std::pair<size_t, size_t> range{0, replacement_text.size()};
        if (identity_edit.target.row_kind != "signal.aspect" ||
            !has_field_change(*identity_edit.change, "deleteGlare")) {
            return range;
        }

        // Signal aspects are one logical source statement spanning a main row
        // and its optional glare row. Deleting the glare retains the former
        // separator newline in the file, while the reparser's surviving main
        // row span deliberately excludes line endings. Keep the identity on
        // that generated main row only so the strict source-range reconnect
        // check still describes the parser's actual candidate statement.
        const TextLineSpan first_line = text_line_span(replacement_text, 0);
        if (!first_line.has_terminator()) return range;
        range.second = first_line.content_end;
        return range;
    };

    auto append_replacement_identity = [&](TextReplacement& replacement,
                                           const PreparedEdit* identity_edit) {
        if (!identity_edit || identity_edit->operation == "delete") return;
        const auto identity_range =
            source_identity_range(*identity_edit, replacement.text);
        int baseline_global_order = 0;
        if (identity_edit->target.statement_index != k_no_source_ref &&
            identity_edit->target.statement_index < ctx.parsed_statements.size()) {
            baseline_global_order =
                ctx.parsed_statements[identity_edit->target.statement_index].global_order;
        }
        replacement.identities.push_back({
            identity_edit->change->edit_id,
            identity_edit->target.row_kind,
            identity_range.first,
            identity_range.second,
            identity_edit->target.element_index,
            baseline_global_order,
        });
    };

    auto add_source_replacement = [&](size_t file_index,
                                      size_t begin,
                                      size_t end,
                                      const std::string& text,
                                      const PreparedEdit* identity_edit) {
        auto key = std::make_tuple(file_index, begin, end);
        auto existing = replacement_by_range.find(key);
        if (existing != replacement_by_range.end()) {
            if (existing->second != text) {
                report.blocking_errors.push_back(
                    "conflicting edits target the same physical source statement");
            } else if (identity_edit) {
                auto replacement = std::find_if(
                    patches[file_index].replacements.begin(),
                    patches[file_index].replacements.end(),
                    [&](const TextReplacement& candidate) {
                        return candidate.begin == begin && candidate.end == end;
                    });
                if (replacement != patches[file_index].replacements.end()) {
                    append_replacement_identity(*replacement, identity_edit);
                }
            }
            return;
        }
        replacement_by_range.emplace(key, text);
        TextReplacement replacement;
        replacement.begin = begin;
        replacement.end = end;
        replacement.text = text;
        append_replacement_identity(replacement, identity_edit);
        patches[file_index].replacements.push_back(std::move(replacement));
    };

    const auto is_direct_source_insert = [](const PreparedEdit& edit) {
        return edit.operation == "insert" &&
            (edit.target.row_kind == "include" ||
             edit.target.row_kind == "resourceList.load" ||
             resource_list_edit_spec_for_content_row_kind(edit.target.row_kind));
    };
    std::map<std::pair<size_t, size_t>, std::vector<const PreparedEdit*>>
        direct_source_inserts;

    for (const PreparedEdit& edit : prepared) {
        if (is_direct_source_insert(edit)) {
            direct_source_inserts[{edit.source_file_index, edit.source_range.first}].push_back(&edit);
            continue;
        }
        if (edit.initial_empty_source_insert) continue;
        if (edit.moves_distance && edit.operation != "insert") {
            add_source_replacement(edit.source_file_index,
                                   edit.removal_range.first,
                                   edit.removal_range.second,
                                   {}, nullptr);
        } else if (!edit.moves_distance) {
            add_source_replacement(edit.source_file_index,
                                   edit.operation == "delete" ? edit.removal_range.first
                                                              : edit.source_range.first,
                                   edit.operation == "delete" ? edit.removal_range.second
                                                              : edit.source_range.second,
                                   edit.operation == "delete" ? std::string{}
                                                              : edit.replacement_statement,
                                   edit.operation == "delete" ? nullptr : &edit);
        }
    }

    for (auto& entry : direct_source_inserts) {
        const size_t file_index = entry.first.first;
        const size_t offset = entry.first.second;
        std::vector<const PreparedEdit*>& inserts = entry.second;
        std::stable_sort(inserts.begin(), inserts.end(),
                         [](const PreparedEdit* left, const PreparedEdit* right) {
                             return left->input_ordinal < right->input_ordinal;
                         });

        SourcePatch& patch = patches[file_index];
        const std::string nl = newline_text(patch.record->newline);
        std::string insertion_body;
        std::vector<TextReplacementIdentity> identities;
        for (const PreparedEdit* insert : inserts) {
            if (!insertion_body.empty()) insertion_body += nl;
            const size_t statement_begin = insertion_body.size();
            insertion_body += insert->replacement_statement;
            const auto identity_range =
                source_identity_range(*insert, insert->replacement_statement);
            int baseline_global_order = 0;
            if (insert->target.statement_index != k_no_source_ref &&
                insert->target.statement_index < ctx.parsed_statements.size()) {
                baseline_global_order =
                    ctx.parsed_statements[insert->target.statement_index].global_order;
            }
            identities.push_back({
                insert->change->edit_id,
                insert->target.row_kind,
                statement_begin + identity_range.first,
                statement_begin + identity_range.second,
                insert->target.element_index,
                baseline_global_order,
            });
        }

        TextReplacement insertion;
        insertion.begin = offset;
        insertion.end = offset;
        insertion.text = statement_insertion_text(
            patch.text, offset, insertion_body, *patch.record);
        const size_t body_offset = insertion.text.find(insertion_body);
        if (body_offset == std::string::npos) {
            report.blocking_errors.push_back(
                "failed to retain edit identity in a generated source insertion");
        } else {
            for (TextReplacementIdentity& identity : identities) {
                identity.relative_begin += body_offset;
                identity.relative_end += body_offset;
            }
            insertion.identities = std::move(identities);
        }
        patch.replacements.push_back(std::move(insertion));
    }

    for (const ResolvedDistanceGroup& resolved : resolved_groups) {
        const DistanceEditGroup& group = *resolved.group;
        std::vector<size_t> members = group.member_indices;
        std::stable_sort(members.begin(), members.end(), [&](size_t lhs, size_t rhs) {
            const PreparedEdit& a = prepared[lhs];
            const PreparedEdit& b = prepared[rhs];
            const SourceSpan& as = ctx.parsed_statements[a.target.statement_index].source;
            const SourceSpan& bs = ctx.parsed_statements[b.target.statement_index].source;
            if (as.byte_start != bs.byte_start) return as.byte_start < bs.byte_start;
            return a.input_ordinal < b.input_ordinal;
        });

        InsertionPart part;
        part.target_distance = group.target_distance;
        part.direction = resolved.direction;
        part.create_distance_block = resolved.create_distance_block;
        part.distance_expression = resolved.distance_expression;
        if (!resolved.boundary.terminal_context_boundary) {
            const ParsedStatement& boundary_after = ctx.parsed_statements[
                group.section.anchors[resolved.boundary.after_anchor_position]];
            auto boundary_after_range = source_range_in_text(
                patches[group.source_file_index], boundary_after.source);
            if (resolved.boundary.insert_offset <= boundary_after_range.first) {
                part.distance_indent = patches[group.source_file_index].text.substr(
                    resolved.boundary.insert_offset,
                    boundary_after_range.first - resolved.boundary.insert_offset);
            }
        }
        // Insert statements have no source indentation of their own; borrow the
        // indentation of the statements already living in the destination
        // distance block, falling back to the destination anchor line, so the
        // generated source stays visually consistent with the surrounding file.
        const bool any_insert_member = std::any_of(
            members.begin(), members.end(), [&](size_t index) {
                return prepared[index].operation == "insert";
            });
        std::string part_statement_indent;
        if (any_insert_member) {
            const ParsedStatement& before_anchor = ctx.parsed_statements[
                group.section.anchors[resolved.boundary.before_anchor_position]];
            const SourcePatch& group_patch = patches[group.source_file_index];
            const auto before_anchor_range =
                source_range_in_text(group_patch, before_anchor.source);
            size_t indent_statement_index = k_no_source_ref;
            for (size_t statement_index :
                 distance_index.statements_for(ctx, before_anchor.source)) {
                const ParsedStatement& candidate = ctx.parsed_statements[statement_index];
                const auto candidate_range =
                    source_range_in_text(group_patch, candidate.source);
                if (candidate_range.first <= before_anchor_range.second) continue;
                if (candidate_range.first >= resolved.boundary.insert_offset) break;
                indent_statement_index = statement_index;
            }
            part_statement_indent = line_indent_of(
                group_patch,
                indent_statement_index != k_no_source_ref
                    ? ctx.parsed_statements[indent_statement_index]
                    : before_anchor);
        }
        std::map<std::tuple<size_t, size_t, size_t>, size_t> physical_statements;
        std::ostringstream identity;
        // Absolute target values may differ between executions of the same
        // physical Include file. The physical patch is identical when its
        // expression and moved source statements are identical, so deduplicate
        // on those facts rather than the per-invocation evaluated distance.
        identity << part.create_distance_block << "\n"
                 << part.distance_expression << "\n";
        for (size_t index : members) {
            const PreparedEdit& edit = prepared[index];
            TextReplacementIdentity statement_identity{
                edit.change->edit_id,
                edit.target.row_kind,
                0,
                0,
                edit.target.element_index,
                ctx.parsed_statements[edit.target.statement_index].global_order,
            };
            const auto physical_key = edit.operation == "insert"
                ? std::make_tuple(edit.source_range.first,
                                  edit.source_range.second,
                                  edit.input_ordinal)
                : std::make_tuple(edit.source_range.first,
                                  edit.source_range.second,
                                  size_t{0});
            auto inserted = physical_statements.emplace(
                physical_key, part.statements.size());
            if (inserted.second) {
                InsertionStatement statement;
                statement.source_offset = edit.source_range.first;
                statement.input_ordinal = edit.input_ordinal;
                const std::string statement_indent =
                    edit.operation == "insert" ? part_statement_indent : edit.source_indent;
                statement.text = statement_indent + edit.replacement_statement;
                statement.statement_begin = statement_indent.size();
                statement.statement_length = edit.replacement_statement.size();
                statement.identities.push_back(std::move(statement_identity));
                part.statements.push_back(std::move(statement));
            } else {
                part.statements[inserted.first->second].identities.push_back(
                    std::move(statement_identity));
            }
            part.first_source_offset = std::min(part.first_source_offset,
                                                edit.source_range.first);
            identity << edit.source_range.first << ":" << edit.source_range.second << "="
                     << edit.replacement_statement << "\n";
        }
        part.identity = hex64(stable_hash64(identity.str()));
        auto insertion_key = std::make_pair(group.source_file_index,
                                            resolved.boundary.insert_offset);
        auto& parts = insertion_parts[insertion_key];
        auto duplicate = std::find_if(parts.begin(), parts.end(),
                                      [&](const InsertionPart& existing) {
                                          return existing.identity == part.identity;
                                      });
        if (duplicate == parts.end()) {
            parts.push_back(std::move(part));
        } else {
            // Repeated Include invocations can resolve to one identical physical
            // patch. Coalesce the source text once, but retain every logical
            // element identity that the shared statement represents.
            for (InsertionStatement& statement : part.statements) {
                auto existing_statement = std::find_if(
                    duplicate->statements.begin(), duplicate->statements.end(),
                    [&](const InsertionStatement& candidate) {
                         return candidate.source_offset == statement.source_offset &&
                                candidate.input_ordinal == statement.input_ordinal &&
                                candidate.text == statement.text;
                    });
                if (existing_statement == duplicate->statements.end()) {
                    duplicate->statements.push_back(std::move(statement));
                } else {
                    existing_statement->identities.insert(
                        existing_statement->identities.end(),
                        std::make_move_iterator(statement.identities.begin()),
                        std::make_move_iterator(statement.identities.end()));
                }
            }
        }
    }

    for (auto& insertion_entry : insertion_parts) {
        const size_t file_index = insertion_entry.first.first;
        const size_t offset = insertion_entry.first.second;
        std::vector<InsertionPart>& parts = insertion_entry.second;
        std::string direction;
        for (const InsertionPart& part : parts) {
            if (direction.empty()) direction = part.direction;
            else if (direction != part.direction) {
                report.blocking_errors.push_back(
                    "incompatible source-section directions share one physical insertion gap");
            }
        }
        std::stable_sort(parts.begin(), parts.end(), [&](const InsertionPart& a,
                                                         const InsertionPart& b) {
            if (a.target_distance == b.target_distance) {
                if (a.first_source_offset != b.first_source_offset) {
                    return a.first_source_offset < b.first_source_offset;
                }
                return a.identity < b.identity;
            }
            return direction == "decreasing"
                ? a.target_distance > b.target_distance
                : a.target_distance < b.target_distance;
        });
        SourcePatch& patch = patches[file_index];
        const std::string nl = newline_text(patch.record->newline);
        std::string insertion_body;
        std::vector<TextReplacementIdentity> insertion_identities;
        for (size_t part_begin = 0; part_begin < parts.size();) {
            size_t part_end = part_begin + 1;
            while (part_end < parts.size() &&
                   exact_distance_value(parts[part_begin].target_distance,
                                        parts[part_end].target_distance)) {
                ++part_end;
            }
            const InsertionPart& first_part = parts[part_begin];
            bool compatible = true;
            std::vector<InsertionStatement> statements;
            for (size_t part_index = part_begin; part_index < part_end; ++part_index) {
                const InsertionPart& part = parts[part_index];
                if (part.create_distance_block != first_part.create_distance_block ||
                    (part.create_distance_block &&
                     part.distance_expression != first_part.distance_expression)) {
                    compatible = false;
                }
                statements.insert(statements.end(), part.statements.begin(),
                                  part.statements.end());
            }
            if (!compatible) {
                report.blocking_errors.push_back(
                    "incompatible plans for one target distance share a physical insertion gap");
                part_begin = part_end;
                continue;
            }
            std::stable_sort(statements.begin(), statements.end(),
                             [](const InsertionStatement& a,
                                const InsertionStatement& b) {
                if (a.source_offset != b.source_offset) {
                    return a.source_offset < b.source_offset;
                }
                return a.input_ordinal < b.input_ordinal;
            });
            if (first_part.create_distance_block) {
                if (!insertion_body.empty()) insertion_body += nl;
                insertion_body += first_part.distance_indent +
                    first_part.distance_expression + ";";
                ++report.created_distance_block_count;
            } else {
                ++report.reused_distance_block_count;
            }
            std::map<std::tuple<size_t, size_t, std::string>, std::pair<size_t, size_t>>
                emitted_statements;
            for (const InsertionStatement& statement : statements) {
                const auto statement_key =
                    std::make_tuple(statement.source_offset,
                                    statement.input_ordinal,
                                    statement.text);
                auto emitted = emitted_statements.find(statement_key);
                std::pair<size_t, size_t> statement_range;
                if (emitted == emitted_statements.end()) {
                    if (!insertion_body.empty()) insertion_body += nl;
                    const size_t text_begin = insertion_body.size();
                    insertion_body += statement.text;
                    statement_range = {
                        text_begin + statement.statement_begin,
                        text_begin + statement.statement_begin + statement.statement_length,
                    };
                    emitted_statements.emplace(statement_key, statement_range);
                } else {
                    statement_range = emitted->second;
                }
                for (const TextReplacementIdentity& source_identity : statement.identities) {
                    TextReplacementIdentity identity = source_identity;
                    identity.relative_begin = statement_range.first;
                    identity.relative_end = statement_range.second;
                    insertion_identities.push_back(std::move(identity));
                }
            }
            part_begin = part_end;
        }
        TextReplacement insertion;
        insertion.begin = offset;
        insertion.end = offset;
        insertion.text = statement_insertion_text(
            patch.text, offset, insertion_body, *patch.record);
        const size_t body_offset = insertion.text.find(insertion_body);
        if (body_offset == std::string::npos) {
            report.blocking_errors.push_back(
                "failed to retain edit identity in a generated distance insertion");
        } else {
            for (TextReplacementIdentity& identity : insertion_identities) {
                identity.relative_begin += body_offset;
                identity.relative_end += body_offset;
            }
            insertion.identities = std::move(insertion_identities);
        }
        patch.replacements.push_back(std::move(insertion));
    }

    std::map<size_t, std::vector<const PreparedEdit*>> empty_source_inserts;
    for (const PreparedEdit& edit : prepared) {
        if (edit.initial_empty_source_insert) {
            empty_source_inserts[edit.source_file_index].push_back(&edit);
        }
    }
    for (auto& entry : empty_source_inserts) {
        const size_t file_index = entry.first;
        std::vector<const PreparedEdit*>& inserts = entry.second;
        std::stable_sort(inserts.begin(), inserts.end(),
                         [](const PreparedEdit* left, const PreparedEdit* right) {
                             if (!exact_distance_value(left->target_distance,
                                                       right->target_distance)) {
                                 return left->target_distance < right->target_distance;
                             }
                             return left->input_ordinal < right->input_ordinal;
                         });

        SourcePatch& patch = patches[file_index];
        const std::string nl = newline_text(patch.record->newline);
        std::string insertion_body;
        std::vector<TextReplacementIdentity> insertion_identities;
        for (size_t distance_begin = 0; distance_begin < inserts.size();) {
            size_t distance_end = distance_begin + 1;
            while (distance_end < inserts.size() &&
                   exact_distance_value(inserts[distance_begin]->target_distance,
                                        inserts[distance_end]->target_distance)) {
                ++distance_end;
            }
            if (!insertion_body.empty()) insertion_body += nl;
            insertion_body += canonical_number(inserts[distance_begin]->target_distance) + ";";
            ++report.created_distance_block_count;
            for (size_t index = distance_begin; index < distance_end; ++index) {
                const PreparedEdit& edit = *inserts[index];
                insertion_body += nl;
                const size_t statement_begin = insertion_body.size();
                insertion_body += edit.replacement_statement;
                const auto identity_range =
                    source_identity_range(edit, edit.replacement_statement);
                insertion_identities.push_back({
                    edit.change->edit_id,
                    edit.target.row_kind,
                    statement_begin + identity_range.first,
                    statement_begin + identity_range.second,
                    edit.target.element_index,
                    0,
                });
            }
            distance_begin = distance_end;
        }

        TextReplacement insertion;
        insertion.begin = patch.text.size();
        insertion.end = patch.text.size();
        insertion.text = statement_insertion_text(
            patch.text, insertion.begin, insertion_body, *patch.record);
        const size_t body_offset = insertion.text.find(insertion_body);
        if (body_offset == std::string::npos) {
            report.blocking_errors.push_back(
                "failed to retain edit identity in a generated distance insertion");
        } else {
            for (TextReplacementIdentity& identity : insertion_identities) {
                identity.relative_begin += body_offset;
                identity.relative_end += body_offset;
            }
            insertion.identities = std::move(insertion_identities);
        }
        patch.replacements.push_back(std::move(insertion));
    }

    for (auto& patch_entry : patches) {
        SourcePatch& patch = patch_entry.second;
        if (!patch.record || patch.replacements.empty()) continue;
        std::sort(patch.replacements.begin(), patch.replacements.end(),
                  [](const TextReplacement& a, const TextReplacement& b) {
                      if (a.begin != b.begin) return a.begin > b.begin;
                      return a.end > b.end;
                  });
        for (size_t i = 1; i < patch.replacements.size(); ++i) {
            const TextReplacement& previous = patch.replacements[i - 1];
            const TextReplacement& current = patch.replacements[i];
            if (current.end > previous.begin) {
                report.blocking_errors.push_back("overlapping edits in source file: " + patch.record->file_path);
                break;
            }
        }
    }

    if (!report.ok()) return report;

    for (auto& patch_entry : patches) {
        SourcePatch& patch = patch_entry.second;
        if (!patch.record || patch.replacements.empty()) continue;
        std::string patched_text = patch.text;
        std::vector<MapEditIdentityOrigin> resolved_identities;
        for (const TextReplacement& replacement : patch.replacements) {
            report.previews.push_back({
                patch.record->file_path,
                preview_fragment(patched_text, replacement.begin, replacement.end),
                preview_fragment(replacement.text, 0, replacement.text.size())
            });
            const std::ptrdiff_t delta =
                static_cast<std::ptrdiff_t>(replacement.text.size()) -
                static_cast<std::ptrdiff_t>(replacement.end - replacement.begin);
            for (MapEditIdentityOrigin& identity : resolved_identities) {
                if (identity.text_start < replacement.end) continue;
                identity.text_start = static_cast<size_t>(
                    static_cast<std::ptrdiff_t>(identity.text_start) + delta);
                identity.text_end = static_cast<size_t>(
                    static_cast<std::ptrdiff_t>(identity.text_end) + delta);
            }
            patched_text.replace(replacement.begin,
                                 replacement.end - replacement.begin,
                                 replacement.text);
            for (const TextReplacementIdentity& source_identity : replacement.identities) {
                if (source_identity.relative_end < source_identity.relative_begin ||
                    source_identity.relative_end > replacement.text.size()) {
                    report.blocking_errors.push_back(
                        "generated edit identity range is outside its source replacement");
                    continue;
                }
                resolved_identities.push_back({
                    source_identity.edit_id,
                    source_identity.row_kind,
                    patch.record->source_key,
                    replacement.begin + source_identity.relative_begin,
                    replacement.begin + source_identity.relative_end,
                    source_identity.element_index,
                    source_identity.baseline_global_order,
                });
            }
        }
        std::string bytes;
        try {
            bytes = encode_text_for_writeback(patched_text, patch.record->encoding, patch.utf8_bom);
        } catch (const std::exception& e) {
            report.blocking_errors.push_back(e.what());
            continue;
        }
        std::string current_hash = hex64(stable_hash64(bytes));
        report.changed_files.push_back(patch.record->file_path);
        report.patched_files.push_back({
            patch.record->file_path,
            patch.record->source_key,
            std::move(patched_text),
            bytes,
            patch.record->encoding,
            patch.record->newline,
            patch.base_hash.empty() ? patch.current_hash : patch.base_hash,
            current_hash,
            bytes.size(),
            patch.utf8_bom
        });
        report.identity_origins.insert(
            report.identity_origins.end(),
            std::make_move_iterator(resolved_identities.begin()),
            std::make_move_iterator(resolved_identities.end()));
    }

    if (!report.ok()) return report;
    std::vector<MapEditChange> validation_changes;
    validation_changes.reserve(effective_changes.size());
    for (const MapEditChange* change : effective_changes) validation_changes.push_back(*change);
    validate_edit_report(ctx, validation_changes, report);
    if (!report.ok()) return report;

    if (write_files) {
        finalize_direct_disk_apply(ctx, report);
    }
    return report;
}

void finalize_direct_disk_apply(MapContext& ctx, MapEditReport& report) {
    if (std::any_of(ctx.source_overrides.begin(), ctx.source_overrides.end(),
                    [](const auto& entry) { return entry.second.dirty; })) {
        report.blocking_errors.push_back(
            "direct disk apply is blocked while working-copy edits are pending");
        return;
    }
    if (!report.validated_context) {
        report.blocking_errors.push_back(
            "direct disk apply has no validated full-reparse context");
        return;
    }
    std::vector<TransactionalWriteRequest> writes;
    writes.reserve(report.patched_files.size());
    for (const MapEditPatchedFile& file : report.patched_files) {
        writes.push_back({path_from_utf8(file.file_path), file.bytes,
                          file.base_hash, file.current_hash});
    }
    try {
        TransactionalWriteOutcome outcome =
            replace_files_transactionally(std::move(writes));
        report.warnings.insert(report.warnings.end(),
                               outcome.warnings.begin(), outcome.warnings.end());
    } catch (const std::exception& e) {
        report.blocking_errors.push_back(e.what());
        return;
    }

    replace_context_and_advance_revisions(
        ctx, std::move(*report.validated_context));
    ctx.disk_native_element_edit_id_to_stable =
        ctx.native_element_edit_id_to_stable;
    ctx.disk_source_hashes_for_stable_ids.clear();
    ctx.disk_source_hashes_for_stable_ids.reserve(ctx.source_files.size());
    for (const SourceFileRecord& file : ctx.source_files) {
        ctx.disk_source_hashes_for_stable_ids.emplace(
            file.source_key, file.source_hash);
    }
    ctx.source_overrides.clear();
    ctx.edit_validation_current = false;
    ctx.edit_validation_fingerprint.clear();
}

namespace {

// Returns true when one batch both introduces a new resource-list Load
// reference and edits resource-list rows. Rows of the newly connected file can
// only be planned against a context that already contains the inserted Load
// statement, so such a batch must be planned in two stages.
bool batch_needs_staged_load_replay(const std::vector<MapEditChange>& changes) {
    bool has_new_load = false;
    bool has_list_row_change = false;
    for (const MapEditChange& change : changes) {
        const std::string operation = ascii_lower(
            change.operation.empty() ? "update" : change.operation);
        if (operation == "insert" && change.row_kind == "resourceList.load") {
            has_new_load = true;
        } else if (resource_list_edit_spec_for_content_row_kind(change.row_kind)) {
            has_list_row_change = true;
        }
    }
    return has_new_load && has_list_row_change;
}

} // namespace

MapEditReport plan_staged_edit_batch(MapContext& ctx,
                                     const std::vector<MapEditChange>& changes) {
    if (!batch_needs_staged_load_replay(changes)) {
        return build_edit_report(ctx, changes, false);
    }

    std::vector<MapEditChange> load_inserts;
    std::vector<MapEditChange> remaining;
    for (const MapEditChange& change : changes) {
        const std::string operation = ascii_lower(
            change.operation.empty() ? "update" : change.operation);
        if (operation == "insert" && change.row_kind == "resourceList.load") {
            load_inserts.push_back(change);
        } else {
            remaining.push_back(change);
        }
    }

    // Stage 1: plan and fully reparse every new Load insert on the current
    // working copy. build_edit_report leaves ctx untouched without write_files.
    MapEditReport staged = build_edit_report(ctx, load_inserts, false);
    if (!staged.ok() || !staged.full_reparse_ok || !staged.validated_context) {
        return staged;
    }

    // Stage 2: plan the rest of the batch against the validated candidate
    // context that already contains the inserted references, so list rows of
    // the newly loaded files resolve their editIds. The candidate's source
    // overrides already contain stage-1 patches, so its full reparse covers
    // both stages when validating stage-2 edits.
    MapContext& candidate_base = *staged.validated_context;
    MapEditReport final = build_edit_report(candidate_base, remaining, false);

    MapEditReport merged;
    merged.warnings = staged.warnings;
    merged.warnings.insert(merged.warnings.end(),
                           final.warnings.begin(), final.warnings.end());
    merged.blocking_errors = std::move(final.blocking_errors);
    merged.resolution_requests = std::move(final.resolution_requests);
    merged.update_count = final.update_count;
    merged.delete_count = final.delete_count;
    merged.insert_count = staged.insert_count + final.insert_count;
    merged.created_distance_block_count = final.created_distance_block_count;
    merged.reused_distance_block_count = final.reused_distance_block_count;
    merged.distance_group_count = final.distance_group_count;
    merged.target_distance_match_count = final.target_distance_match_count;
    merged.non_target_changed_count = final.non_target_changed_count;
    merged.changed_files = std::move(staged.changed_files);
    for (const std::string& file_path : final.changed_files) {
        if (std::find(merged.changed_files.begin(), merged.changed_files.end(),
                      file_path) == merged.changed_files.end()) {
            merged.changed_files.push_back(file_path);
        }
    }
    merged.previews = std::move(staged.previews);
    merged.previews.insert(merged.previews.end(),
                           std::make_move_iterator(final.previews.begin()),
                           std::make_move_iterator(final.previews.end()));
    merged.patched_files = std::move(staged.patched_files);
    for (const MapEditPatchedFile& file : final.patched_files) {
        const bool conflicts = std::any_of(
            merged.patched_files.begin(), merged.patched_files.end(),
            [&](const MapEditPatchedFile& existing) {
                return existing.source_key == file.source_key ||
                    normalized_source_key(existing.file_path) ==
                        normalized_source_key(file.file_path);
            });
        if (conflicts) {
            merged.blocking_errors.push_back(
                "two planning stages produced conflicting patches for: " +
                file.file_path);
            continue;
        }
        merged.patched_files.push_back(file);
    }
    merged.identity_origins = std::move(staged.identity_origins);
    merged.identity_origins.insert(
        merged.identity_origins.end(),
        std::make_move_iterator(final.identity_origins.begin()),
        std::make_move_iterator(final.identity_origins.end()));
    merged.validation_fingerprint = final.validation_fingerprint;
    merged.validated_context = std::move(final.validated_context);
    merged.full_reparse_ok = final.full_reparse_ok;
    return merged;
}

void apply_patched_files_to_overrides(SourceTextOverrides& overrides,
                                      const MapEditReport& report) {
    for (const MapEditPatchedFile& file : report.patched_files) {
        SourceTextOverride source;
        source.file_path = file.file_path;
        source.source_key = file.source_key;
        source.text = file.text;
        source.encoding = file.encoding;
        source.newline = file.newline;
        source.base_hash = file.base_hash;
        source.current_hash = file.current_hash;
        source.byte_length = file.byte_length;
        source.utf8_bom = file.utf8_bom;
        source.dirty = true;
        overrides[source.source_key] = std::move(source);
    }
}

void reparse_context_with_overrides(MapContext& ctx,
                                    SourceTextOverrides overrides,
                                    bool has_arbitrary_distribution,
                                    const std::array<double, 3>& arbitrary_distribution) {
    const auto disk_identities = ctx.disk_native_element_edit_id_to_stable;
    const auto disk_source_hashes = ctx.disk_source_hashes_for_stable_ids;
    std::string entry_file_path = ctx.entry_file_path;
    if (entry_file_path.empty()) {
        if (!ctx.include_stack.empty()) entry_file_path = ctx.include_stack.front();
        else if (!ctx.source_files.empty()) entry_file_path = ctx.source_files.front().file_path;
    }
    if (entry_file_path.empty()) throw std::runtime_error("map entry file is not known");
    double unit_distance = ctx.unit_distance;
    MapParseOptions options = ctx.parse_options;
    options.collect_edit_metadata = true;
    auto next = parse_map_context(path_from_utf8(entry_file_path), unit_distance, std::move(overrides),
                                  has_arbitrary_distribution, arbitrary_distribution, options);
    if (!disk_identities.empty() && !disk_source_hashes.empty()) {
        bool disk_baseline_matches = next->source_files.size() == disk_source_hashes.size();
        for (const SourceFileRecord& file : next->source_files) {
            auto expected = disk_source_hashes.find(file.source_key);
            if (expected == disk_source_hashes.end() ||
                expected->second != file.source_hash) {
                disk_baseline_matches = false;
                break;
            }
        }
        if (!disk_baseline_matches) {
            throw std::runtime_error(
                "source files changed externally; full Reload is required before editing");
        }
    }
    next->native_element_edit_id_to_stable = disk_identities;
    next->disk_native_element_edit_id_to_stable = disk_identities;
    next->disk_source_hashes_for_stable_ids = disk_source_hashes;
    next->element_edit_id_cache.clear();
    replace_context_and_advance_revisions(ctx, std::move(*next));
}

void apply_edit_report_to_memory(MapContext& ctx, const MapEditReport& report) {
    if (!report.ok() || !report.full_reparse_ok || !report.validated_context) {
        throw std::runtime_error("edit report has no validated full-reparse result");
    }
    replace_context_and_advance_revisions(
        ctx, std::move(*report.validated_context));
    ctx.edit_validation_fingerprint = report.validation_fingerprint;
    ctx.edit_validation_current = true;
}

void reset_memory_edits(MapContext& ctx) {
    if (ctx.source_overrides.empty()) {
        // The current context already represents the disk baseline. Re-parsing
        // here would regenerate global-order-based editIds after a committed
        // distance move and break the session-stable identities held by the GUI.
        return;
    }
    bool has_arbitrary_distribution = ctx.cp_arbdistribution_explicit;
    std::array<double, 3> arbitrary_distribution = ctx.cp_arbdistribution;
    reparse_context_with_overrides(ctx, SourceTextOverrides{},
                                   has_arbitrary_distribution,
                                   arbitrary_distribution);
}

void append_committed_row(MapContext& ctx, MapEditReport& report,
                          const std::string& row_kind, size_t row_index,
                          const EditSourceRef& ref) {
    MapEditCommittedRow row;
    row.row_kind = row_kind;
    row.row_index = row_index;
    if (ref.valid() && ref.statement_index < ctx.parsed_statements.size()) {
        const ParsedStatement& statement = ctx.parsed_statements[ref.statement_index];
        row.edit_id = element_edit_id(ctx, ref, row_kind);
        row.file_path = source_file_path(ctx, statement.source);
        row.line = statement.source.line;
        row.column = statement.source.column;
        row.raw_text_preview = statement.raw_text_preview;
    }
    report.committed_rows.push_back(std::move(row));
}

template <typename Rows>
void append_committed_rows(MapContext& ctx, MapEditReport& report,
                           const std::string& row_kind, const Rows& rows) {
    for (size_t i = 0; i < rows.size(); ++i) {
        append_committed_row(ctx, report, row_kind, i, rows[i].edit_ref);
    }
}

void populate_committed_edit_state(MapContext& ctx, MapEditReport& report) {
    report.committed_files.reserve(ctx.source_files.size());
    for (const SourceFileRecord& file : ctx.source_files) {
        report.committed_files.push_back({file.file_path, file.source_hash, file.byte_length});
    }

    append_committed_rows(ctx, report, "structure.model", ctx.structure_models);
    append_committed_rows(ctx, report, "curve", ctx.curves);
    append_committed_rows(ctx, report, "gradient", ctx.gradients);
    append_committed_rows(ctx, report, "otherTrack.change", ctx.other_track_changes);
    append_committed_rows(ctx, report, "structure.put", ctx.structure_puts);
    append_committed_rows(ctx, report, "structure.between", ctx.structure_betweens);
    append_committed_rows(ctx, report, "signal.aspect", ctx.signal_aspects);
    append_committed_rows(ctx, report, "signal.put", ctx.signal_puts);
    append_committed_rows(ctx, report, "repeater", ctx.repeaters);
    append_committed_rows(ctx, report, "irregularity.change", ctx.irregularities);
    append_committed_rows(ctx, report, "beacon.put", ctx.beacons);
    append_committed_rows(ctx, report, "section.begin", ctx.section_begins);
    append_committed_rows(ctx, report, "section.speedLimit", ctx.section_speed_limits);
    append_committed_rows(ctx, report, "mapSound.play", ctx.map_sounds);
    append_committed_rows(ctx, report, "mapSound3D.put", ctx.map_sound_3d);
    append_committed_rows(ctx, report, "rollingNoise.change", ctx.rolling_noises);
    append_committed_rows(ctx, report, "flangeNoise.change", ctx.flange_noises);
    append_committed_rows(ctx, report, "jointNoise.play", ctx.joint_noises);
    append_committed_rows(ctx, report, "background.change", ctx.backgrounds);
    append_committed_rows(ctx, report, "adhesion.change", ctx.adhesions);
    append_committed_rows(ctx, report, "cabIlluminance.change", ctx.cab_illuminance);
    append_committed_rows(ctx, report, "fog.change", ctx.fogs);
    append_committed_rows(ctx, report, "drawDistance.change", ctx.draw_distances);
    append_committed_rows(ctx, report, "speedlimit", ctx.speedlimits);
    size_t sound_index = 0;
    size_t sound_3d_index = 0;
    for (const SoundListEntry& row : ctx.sound_list) {
        const bool is_3d = row.is_3d;
        append_committed_row(ctx, report,
                             is_3d ? "sound3D.list" : "sound.list",
                             is_3d ? sound_3d_index++ : sound_index++,
                             row.edit_ref);
    }

    std::vector<size_t> station_order;
    station_order.reserve(ctx.station_puts.size());
    for (size_t i = 0; i < ctx.station_puts.size(); ++i) station_order.push_back(i);
    std::stable_sort(station_order.begin(), station_order.end(), [&](size_t lhs, size_t rhs) {
        const StationPut& a = ctx.station_puts[lhs];
        const StationPut& b = ctx.station_puts[rhs];
        if (a.distance != b.distance) return a.distance < b.distance;
        return a.order < b.order;
    });
    for (size_t row_index = 0; row_index < station_order.size(); ++row_index) {
        append_committed_row(ctx, report, "station.put", row_index,
                             ctx.station_puts[station_order[row_index]].edit_ref);
    }

    // Reuse the canonical source-order helper so committed row indices line up
    // with the snapshot and the GUI table cache.
    const auto station_list_entries = ordered_station_list_entries(ctx);
    for (size_t row_index = 0; row_index < station_list_entries.size(); ++row_index) {
        append_committed_row(ctx, report, "station.list", row_index,
                             station_list_entries[row_index]->edit_ref);
    }
}

MapEditReport commit_memory_edits(MapContext& ctx) {
    MapEditReport report;
    struct PendingWrite {
        std::string source_key;
        std::filesystem::path path;
        std::string bytes;
        std::string expected_hash;
        std::string hash;
    };
    std::vector<PendingWrite> writes;

    bool has_dirty_override = false;
    for (const auto& entry : ctx.source_overrides) {
        if (entry.second.dirty) {
            has_dirty_override = true;
            break;
        }
    }
    if (has_dirty_override && !ctx.edit_validation_current) {
        report.blocking_errors.push_back(
            "working-copy edits have not passed full temporary-result validation");
        return report;
    }
    if (has_dirty_override) {
        try {
            MapEditReport empty_report;
            std::unique_ptr<MapContext> verified = parse_report_candidate(ctx, empty_report);
            const std::string fingerprint =
                semantic_snapshot_for_context(*verified).full_fingerprint;
            report.validation_fingerprint = fingerprint;
            report.full_reparse_ok = true;
            if (fingerprint != ctx.edit_validation_fingerprint) {
                report.blocking_errors.push_back(
                    "working-copy semantics changed after the last validated Apply");
                return report;
            }
        } catch (const std::exception& e) {
            report.blocking_errors.push_back(
                std::string("pre-save full edited-map reparse failed: ") + e.what());
            return report;
        }
    }

    for (const auto& entry : ctx.source_overrides) {
        const SourceTextOverride& source = entry.second;
        if (!source.dirty) continue;
        std::filesystem::path path = path_from_utf8(source.file_path);
        std::string disk_bytes;
        try {
            disk_bytes = read_binary_file(path);
        } catch (const std::exception& e) {
            report.blocking_errors.push_back(e.what());
            continue;
        }
        std::string disk_hash = hex64(stable_hash64(disk_bytes));
        if (!source.base_hash.empty() && disk_hash != source.base_hash) {
            report.blocking_errors.push_back("source file changed externally: " + source.file_path);
            continue;
        }

        try {
            std::string bytes = encode_text_for_writeback(source.text, source.encoding, source.utf8_bom);
            std::string hash = hex64(stable_hash64(bytes));
            writes.push_back({source.source_key, std::move(path), std::move(bytes),
                              source.base_hash, std::move(hash)});
            report.changed_files.push_back(source.file_path);
        } catch (const std::exception& e) {
            report.blocking_errors.push_back(e.what());
        }
    }

    if (!report.ok()) return report;

    std::vector<TransactionalWriteRequest> transaction_writes;
    transaction_writes.reserve(writes.size());
    for (const PendingWrite& write : writes) {
        transaction_writes.push_back({write.path, write.bytes,
                                      write.expected_hash, write.hash});
    }
    try {
        TransactionalWriteOutcome outcome =
            replace_files_transactionally(std::move(transaction_writes));
        report.warnings.insert(report.warnings.end(),
                               outcome.warnings.begin(), outcome.warnings.end());
    } catch (const TransactionalWriteError& e) {
        if (!e.rollback_complete()) {
            ctx.edit_validation_current = false;
            ctx.edit_validation_fingerprint.clear();
        }
        report.blocking_errors.push_back(e.what());
        return report;
    } catch (const std::exception& e) {
        report.blocking_errors.push_back(e.what());
        return report;
    }
    for (const PendingWrite& write : writes) {
        auto source_index = ctx.source_file_indices.find(write.source_key);
        if (source_index != ctx.source_file_indices.end() &&
            source_index->second < ctx.source_files.size()) {
            SourceFileRecord& source_file = ctx.source_files[source_index->second];
            source_file.source_hash = write.hash;
            source_file.byte_length = write.bytes.size();
        }
        ctx.source_overrides.erase(write.source_key);
    }
    ctx.edit_validation_current = false;
    ctx.edit_validation_fingerprint.clear();
    if (!writes.empty()) {
        ctx.disk_native_element_edit_id_to_stable =
            ctx.native_element_edit_id_to_stable;
        ctx.disk_source_hashes_for_stable_ids.clear();
        ctx.disk_source_hashes_for_stable_ids.reserve(ctx.source_files.size());
        for (const SourceFileRecord& file : ctx.source_files) {
            ctx.disk_source_hashes_for_stable_ids.emplace(
                file.source_key, file.source_hash);
        }
        populate_committed_edit_state(ctx, report);
        invalidate_map_snapshot(ctx, true, false);
        invalidate_scene_geometry_snapshot(ctx, false);
    }
    return report;
}


} // namespace kme::maploader::detail
