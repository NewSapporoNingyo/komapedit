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

namespace kme::maploader::detail {

using kme::maploader::path_to_utf8;

constexpr size_t k_max_include_depth = 64;

struct MapObject {
    std::string label;
    Value key;
    bool has_key = false;
};

struct MapFunction {
    std::string label;
    std::vector<Value> args;
    std::string raw_arguments;
    size_t explicit_argument_count = 0;
};

struct ParsedMapElement {
    std::vector<MapObject> objects;
    MapFunction function;
};

class Parser {
public:
    Parser(MapContext& context, LoadedText loaded)
        : ctx_(context), loaded_(std::move(loaded)), src_(loaded_.body), file_path_(loaded_.path) {
        register_source_file(ctx_, loaded_);
        ctx_.current_file_path = loaded_.normalized_path;
        if (ctx_.include_stack.empty()) {
            ctx_.include_stack.push_back(ctx_.current_file_path);
        }
        if (ctx_.current_include_invocation_key.empty()) {
            ctx_.current_include_invocation_key = k_root_include_invocation_key;
            ctx_.current_include_invocation_index = k_no_source_ref;
        }
    }

    void parse() {
        while (true) {
            skip();
            if (eof()) break;
            current_statement_start_ = pos_;
            try {
                parse_statement();
            } catch (const FatalParseError&) {
                throw;
            } catch (const std::exception& e) {
                add_warning(current_statement_start_, "Syntax", e.what());
                synchronize_statement();
            }
        }
        flush_pending_includes();
    }

private:
    class FatalParseError final : public std::runtime_error {
    public:
        using std::runtime_error::runtime_error;
    };

    struct IncludeResult {
        MapContext context;
        std::string error;
        bool fatal = false;
    };

    struct PendingInclude {
        std::filesystem::path path;
        std::string include_path;
        std::string include_invocation_key;
        double seed_distance = 0.0;
        std::unordered_map<std::string, Value> seed_variables;
        MapDiagnostic source;
        std::future<IncludeResult> future;
    };

    MapContext& ctx_;
    LoadedText loaded_;
    const std::string& src_;
    std::filesystem::path file_path_;
    size_t pos_ = 0;
    size_t current_statement_start_ = 0;
    std::vector<size_t> diagnostic_line_starts_ = build_line_starts(src_);
    std::vector<PendingInclude> pending_includes_;
    std::unordered_map<std::string, size_t> include_call_occurrences_;

    bool eof() const { return pos_ >= src_.size(); }
    char peek() const { return eof() ? '\0' : src_[pos_]; }

    bool starts_with(size_t pos, const std::string& token) const {
        return src_.compare(pos, token.size(), token) == 0;
    }

    void skip() {
        while (!eof()) {
            unsigned char c = static_cast<unsigned char>(src_[pos_]);
            if (std::isspace(c) || c == '\\' || c == '"') {
                ++pos_;
            } else if (c == '#') {
                pos_ = text_line_span(src_, pos_).next_begin;
            } else if (starts_with(pos_, "//")) {
                pos_ += 2;
                pos_ = text_line_span(src_, pos_).next_begin;
            } else if (starts_with(pos_, "\xC2\xA0")) {
                pos_ += 2;
            } else if (starts_with(pos_, "\xE3\x80\x80")) {
                pos_ += 3;
            } else {
                break;
            }
        }
    }

    bool accept(char ch) {
        skip();
        if (peek() == ch) {
            ++pos_;
            return true;
        }
        return false;
    }

    void expect(char ch) {
        if (!accept(ch)) {
            std::ostringstream out;
            out << "Expected '" << ch << "' at byte " << pos_;
            throw std::runtime_error(out.str());
        }
    }

    std::pair<int, int> diagnostic_line_column(size_t position) const {
        const auto it = std::upper_bound(diagnostic_line_starts_.begin(),
                                         diagnostic_line_starts_.end(), position);
        const size_t line_index =
            it == diagnostic_line_starts_.begin()
                ? 0
                : static_cast<size_t>(std::distance(diagnostic_line_starts_.begin(), it) - 1);
        const size_t line_start = diagnostic_line_starts_[line_index];
        return {loaded_.body_start_line + static_cast<int>(line_index),
                utf8_column_count(src_, line_start, position) + 1};
    }

    MapDiagnostic diagnostic_source(size_t position, const std::string& statement_kind,
                                    const std::string& message = {}) const {
        const auto location = diagnostic_line_column(std::min(position, src_.size()));
        MapDiagnostic diagnostic;
        diagnostic.file_path = loaded_.normalized_path;
        diagnostic.line = location.first;
        diagnostic.column = location.second;
        diagnostic.statement_kind = statement_kind;
        diagnostic.message = message;
        return diagnostic;
    }

    void add_warning(size_t position, const std::string& statement_kind,
                     const std::string& message) {
        ctx_.diagnostics.push_back(diagnostic_source(position, statement_kind, message));
    }

    bool finish_statement(size_t statement_start, const std::string& statement_kind) {
        skip();
        if (peek() == ';') {
            ++pos_;
            return true;
        }
        add_warning(statement_start, statement_kind, "Missing statement terminator ';'.");
        return false;
    }

    void synchronize_statement() {
        bool in_string = false;
        while (!eof()) {
            const char ch = src_[pos_++];
            if (ch == '\'') {
                in_string = !in_string;
            } else if (!in_string && ch == ';') {
                break;
            }
        }
    }

    bool is_ident_start(unsigned char c) const {
        return std::isalpha(c) || c == '_' || c >= 0x80;
    }

    bool is_ident_part(unsigned char c) const {
        return std::isalnum(c) || c == '_' || c >= 0x80;
    }

    std::string parse_label() {
        skip();
        if (eof() || !is_ident_start(static_cast<unsigned char>(peek()))) {
            throw std::runtime_error("Expected label at byte " + std::to_string(pos_));
        }
        size_t start = pos_;
        ++pos_;
        while (!eof() && is_ident_part(static_cast<unsigned char>(peek()))) ++pos_;
        return src_.substr(start, pos_ - start);
    }

    std::string parse_variable_name() {
        skip();
        if (eof() || !is_ident_part(static_cast<unsigned char>(peek()))) {
            throw std::runtime_error("Expected variable name at byte " + std::to_string(pos_));
        }
        size_t start = pos_;
        ++pos_;
        while (!eof() && is_ident_part(static_cast<unsigned char>(peek()))) ++pos_;
        return src_.substr(start, pos_ - start);
    }

    bool current_starts_map_element() {
        size_t p = pos_;
        skip_at(p);
        if (p >= src_.size() || !is_ident_start(static_cast<unsigned char>(src_[p]))) return false;
        skip_label_at(p);
        skip_at(p);
        if (p < src_.size() && src_[p] == '[') {
            int depth = 1;
            ++p;
            while (p < src_.size() && depth > 0) {
                if (src_[p] == '\'') {
                    ++p;
                    while (p < src_.size() && src_[p] != '\'') ++p;
                } else if (src_[p] == '[') {
                    ++depth;
                } else if (src_[p] == ']') {
                    --depth;
                }
                ++p;
            }
        }
        skip_at(p);
        return p < src_.size() && src_[p] == '.';
    }

    void skip_at(size_t& p) const {
        while (p < src_.size()) {
            unsigned char c = static_cast<unsigned char>(src_[p]);
            if (std::isspace(c) || c == '\\' || c == '"') {
                ++p;
            } else if (c == '#') {
                p = text_line_span(src_, p).next_begin;
            } else if (src_.compare(p, 2, "//") == 0) {
                p += 2;
                p = text_line_span(src_, p).next_begin;
            } else {
                break;
            }
        }
    }

    void skip_label_at(size_t& p) const {
        if (p < src_.size() && is_ident_start(static_cast<unsigned char>(src_[p]))) {
            ++p;
            while (p < src_.size() && is_ident_part(static_cast<unsigned char>(src_[p]))) ++p;
        }
    }

    void parse_statement() {
        skip();
        if (accept(';')) return;
        size_t statement_start = pos_;

        if (peek() == '$' && next_is_variable_assignment()) {
            flush_pending_includes();
            ++pos_;
            std::string source_name = parse_variable_name();
            std::string name = ascii_lower(source_name);
            expect('=');
            size_t args_start = pos_;
            Value value = parse_expression();
            size_t args_end = pos_;
            finish_statement(statement_start, "Variable.Assign");
            VariableAssignment preview;
            preview.normalized_name = name;
            preview.source_name = source_name;
            preview.value = value;
            preview.expression = trim_field_copy(
                src_.substr(args_start, args_end - args_start));
            preview.file_path = ctx_.current_file_path;
            preview.order = ctx_.next_parse_order();
            preview.source = diagnostic_source(statement_start, "Variable.Assign");
            ctx_.variable_assignments.push_back(std::move(preview));
            if (ctx_.parse_options.collect_edit_metadata) {
                add_parsed_statement(ctx_, "Variable.Assign",
                                     make_source_span(ctx_, loaded_, statement_start, pos_, ctx_.include_stack),
                                     src_.substr(statement_start, pos_ - statement_start),
                                     trim_field_copy(src_.substr(args_start, args_end - args_start)),
                                     {value}, ctx_.distance_expression, ctx_.distance);
            }
            ctx_.variables[name] = value;
            note_variable_write(ctx_, name);
            if (ctx_.parse_options.collect_edit_metadata) {
                rebuild_variable_environment_snapshot(ctx_);
            }
            return;
        }

        size_t save = pos_;
        if (!eof() && is_ident_start(static_cast<unsigned char>(peek()))) {
            std::string first = parse_label();
            std::string first_l = ascii_lower(first);
            pos_ = save;
            if (first_l == "include" && !current_starts_map_element()) {
                if (!include_path_is_simple_string(save)) flush_pending_includes();
                parse_label();
                size_t args_start = pos_;
                Value path = parse_expression();
                size_t args_end = pos_;
                finish_statement(statement_start, "Include");
                if (ctx_.parse_options.collect_edit_metadata) {
                    add_parsed_statement(ctx_, "Include",
                                         make_source_span(ctx_, loaded_, statement_start, pos_, ctx_.include_stack),
                                         src_.substr(statement_start, pos_ - statement_start),
                                         trim_field_copy(src_.substr(args_start, args_end - args_start)),
                                         {path}, ctx_.distance_expression, ctx_.distance);
                }
                queue_include(as_text(path), statement_start, pos_);
                return;
            }
            if (current_starts_map_element()) {
                flush_pending_includes();
                ParsedMapElement element = parse_map_element();
                const std::string statement_kind =
                    map_statement_kind(element.objects, element.function);
                finish_statement(statement_start, statement_kind);
                size_t statement_index = k_no_source_ref;
                if (ctx_.parse_options.collect_edit_metadata) {
                    statement_index = add_parsed_statement(
                        ctx_, statement_kind,
                        make_source_span(ctx_, loaded_, statement_start, pos_, ctx_.include_stack),
                        src_.substr(statement_start, pos_ - statement_start),
                        element.function.raw_arguments,
                        element.function.args,
                        ctx_.distance_expression,
                        ctx_.distance);
                }
                ActiveStatementScope active(ctx_, statement_index);
                dispatch(element.objects, element.function);
                return;
            }
        }

        flush_pending_includes();
        size_t args_start = pos_;
        Value distance = parse_expression();
        size_t args_end = pos_;
        finish_statement(statement_start, "Distance.Set");
        std::string raw_distance = trim_field_copy(src_.substr(args_start, args_end - args_start));
        double distance_value = as_number(distance);
        if (ctx_.parse_options.collect_edit_metadata) {
            add_parsed_statement(ctx_, "Distance.Set",
                                 make_source_span(ctx_, loaded_, statement_start, pos_, ctx_.include_stack),
                                 src_.substr(statement_start, pos_ - statement_start),
                                 raw_distance,
                                 {distance}, raw_distance, distance_value);
        }
        ctx_.distance_expression = raw_distance;
        set_distance(ctx_, distance_value);
    }

    bool include_path_is_simple_string(size_t include_pos) const {
        size_t p = include_pos;
        skip_label_at(p);
        skip_at(p);
        if (p >= src_.size() || src_[p] != '\'') return false;
        ++p;
        while (p < src_.size() && src_[p] != '\'') ++p;
        if (p < src_.size()) ++p;
        skip_at(p);
        return p < src_.size() && src_[p] == ';';
    }

    MapContext make_child_seed(const std::filesystem::path& child,
                               const std::string& include_path,
                               const std::string& include_invocation_key) {
        MapContext seed;
        seed.rootpath = ctx_.rootpath;
        seed.rootpath_utf8 = ctx_.rootpath_utf8;
        seed.entry_file_path = ctx_.entry_file_path;
        seed.current_file_path = normalized_source_path(child);
        seed.include_stack = ctx_.include_stack;
        seed.current_include_invocation_key = include_invocation_key;
        seed.source_overrides = ctx_.source_overrides;
        seed.parse_options = ctx_.parse_options;
        seed.unit_distance = ctx_.unit_distance;
        std::string child_path = normalized_source_path(child);
        seed.file_structure.push_back({k_no_source_ref, include_path, child_path});
        if (seed.include_stack.empty() ||
            normalized_source_key(seed.include_stack.back()) != normalized_source_key(child_path)) {
            seed.include_stack.push_back(child_path);
        }
        seed.distance = ctx_.distance;
        seed.distance_expression = ctx_.distance_expression;
        seed.variables = ctx_.variables;
        if (seed.parse_options.collect_edit_metadata) {
            seed.variable_environment_snapshot = current_variable_environment_snapshot(ctx_);
        }
        return seed;
    }

    static IncludeResult parse_include_context(MapContext seed, std::filesystem::path child) {
        IncludeResult result;
        result.context = std::move(seed);
        bool child_header_loaded = false;
        try {
            ActiveTimingScope active(result.context.timing);
            LoadedText loaded = load_header_text(result.context, child, "BveTs Map ", 2.0);
            child_header_loaded = true;
            register_source_file(result.context, loaded);
            result.context.current_file_path = loaded.normalized_path;
            if (!result.context.file_structure.empty()) {
                result.context.file_structure.front().absolute_path = loaded.normalized_path;
            }
            Parser nested(result.context, std::move(loaded));
            nested.parse();
        } catch (const FatalParseError& e) {
            result.error = e.what();
            result.fatal = true;
        } catch (const std::bad_alloc&) {
            result.error = "out of memory while loading Include";
            result.fatal = true;
        } catch (const std::exception& e) {
            result.error = e.what();
            result.fatal = child_header_loaded;
        }
        return result;
    }

    void queue_include(const std::string& path_text, size_t body_start, size_t body_end) {
        std::filesystem::path child = join_path(ctx_.rootpath, path_text);
        const std::string child_path = normalized_source_path(child);
        const std::string child_key = normalized_source_key(child_path);
        for (const std::string& ancestor : ctx_.include_stack) {
            if (normalized_source_key(ancestor) == child_key) {
                throw FatalParseError("Include cycle detected: " + child_path);
            }
        }
        if (ctx_.include_stack.size() >= k_max_include_depth) {
            throw FatalParseError(
                "Include depth exceeds the supported limit of " +
                std::to_string(k_max_include_depth) + ": " + child_path);
        }
        KME_MAPLOADER_LOG_INFO("including " + path_to_utf8(child));
        const size_t byte_start = loaded_.body_offset + body_start;
        const size_t byte_end = loaded_.body_offset + body_end;
        std::string callsite_key = make_include_invocation_key(
            ctx_.current_include_invocation_key, loaded_.normalized_key,
            byte_start, byte_end, 0);
        const size_t occurrence = include_call_occurrences_[callsite_key]++;
        std::string include_invocation_key = make_include_invocation_key(
            ctx_.current_include_invocation_key, loaded_.normalized_key,
            byte_start, byte_end, occurrence);
        MapContext seed = make_child_seed(child, path_text, include_invocation_key);
        PendingInclude pending;
        pending.path = child;
        pending.include_path = path_text;
        pending.include_invocation_key = std::move(include_invocation_key);
        pending.seed_distance = seed.distance;
        pending.seed_variables = seed.variables;
        pending.source = diagnostic_source(body_start, "Include");
        pending.future = launch_bounded_maploader_task(
            [seed = std::move(seed), child]() mutable {
                return parse_include_context(std::move(seed), child);
            });
        pending_includes_.push_back(std::move(pending));
    }

    bool include_result_is_stale(const PendingInclude& pending, const MapContext& parsed) const {
        if (parsed.depends_on_initial_distance && ctx_.distance != pending.seed_distance) {
            return true;
        }
        if (ctx_.parse_options.collect_edit_metadata) {
            if (ctx_.variables.size() != pending.seed_variables.size()) return true;
            for (const auto& variable : ctx_.variables) {
                if (!variable_value_matches(ctx_.variables, pending.seed_variables,
                                            variable.first)) {
                    return true;
                }
            }
        }
        for (const std::string& key : parsed.external_variable_reads) {
            if (!variable_value_matches(ctx_.variables, pending.seed_variables, key)) {
                return true;
            }
        }
        return false;
    }

    void merge_file_structure(MapContext& child) {
        if (child.file_structure.empty()) return;
        if (ctx_.file_structure.empty()) {
            ctx_.file_structure.push_back({k_no_source_ref, {}, ctx_.current_file_path});
        }

        const size_t child_root_index = ctx_.file_structure.size();
        for (size_t i = 0; i < child.file_structure.size(); ++i) {
            FileStructureRecord& record = child.file_structure[i];
            if (i == 0) {
                record.parent_index = 0;
            } else if (record.parent_index == k_no_source_ref || record.parent_index >= i) {
                record.parent_index = child_root_index;
            } else {
                record.parent_index += child_root_index;
            }
            ctx_.file_structure.push_back(std::move(record));
        }
    }

    void merge_include_context(MapContext& child) {
        ctx_.timing.read_decode_seconds += child.timing.read_decode_seconds;
        if (child.depends_on_initial_distance && !ctx_.has_distance_assignment) {
            ctx_.depends_on_initial_distance = true;
        }
        for (const std::string& key : child.external_variable_reads) {
            if (ctx_.variable_writes.find(key) == ctx_.variable_writes.end()) {
                ctx_.external_variable_reads.insert(key);
            }
        }
        ctx_.variable_writes.insert(child.variable_writes.begin(), child.variable_writes.end());

        merge_file_structure(child);
        std::vector<size_t> source_file_index_map = merge_source_file_records(ctx_, child);
        std::vector<size_t> include_stack_index_map = merge_include_stacks(ctx_, child);
        std::vector<size_t> include_invocation_index_map = merge_include_invocation_keys(ctx_, child);
        size_t statement_index_base = ctx_.parsed_statements.size();
        int edit_order_base = ctx_.edit_order;
        for (auto& statement : child.parsed_statements) {
            if (statement.global_order > 0) {
                statement.global_order += edit_order_base;
                statement.edit_id.clear();
            }
            if (statement.source.source_file_index < source_file_index_map.size()) {
                statement.source.source_file_index = source_file_index_map[statement.source.source_file_index];
            }
            if (statement.source.include_stack_index < include_stack_index_map.size()) {
                statement.source.include_stack_index = include_stack_index_map[statement.source.include_stack_index];
            }
            if (statement.source.include_invocation_index < include_invocation_index_map.size()) {
                statement.source.include_invocation_index =
                    include_invocation_index_map[statement.source.include_invocation_index];
            }
        }
        ctx_.edit_order += child.edit_order;
        offset_row_edit_refs(child.own_track, statement_index_base);
        offset_row_edit_refs(child.curves, statement_index_base);
        offset_row_edit_refs(child.gradients, statement_index_base);
        offset_row_edit_refs(child.other_track_changes, statement_index_base);
        for (auto& row : child.station_list) offset_edit_ref(row.edit_ref, statement_index_base);
        for (auto& row : child.station_puts) offset_row_edit_ref(row, statement_index_base);
        for (auto& kv : child.othertrack) offset_row_edit_refs(kv.second, statement_index_base);
        offset_row_edit_refs(child.structure_loads, statement_index_base);
        offset_row_edit_refs(child.structure_models, statement_index_base);
        offset_row_edit_refs(child.sound_list, statement_index_base);
        offset_row_edit_refs(child.other_trains, statement_index_base);
        offset_row_edit_refs(child.other_train_enables, statement_index_base);
        offset_row_edit_refs(child.other_train_stops, statement_index_base);
        offset_row_edit_refs(child.other_train_structure_keys, statement_index_base);
        offset_row_edit_refs(child.other_train_sound_3d_keys, statement_index_base);
        offset_row_edit_refs(child.map_sounds, statement_index_base);
        offset_row_edit_refs(child.map_sound_3d, statement_index_base);
        offset_row_edit_refs(child.rolling_noises, statement_index_base);
        offset_row_edit_refs(child.flange_noises, statement_index_base);
        offset_row_edit_refs(child.joint_noises, statement_index_base);
        offset_row_edit_refs(child.structure_puts, statement_index_base);
        offset_row_edit_refs(child.structure_betweens, statement_index_base);
        offset_row_edit_refs(child.repeaters, statement_index_base);
        offset_row_edit_refs(child.section_begins, statement_index_base);
        offset_row_edit_refs(child.section_speed_limits, statement_index_base);
        offset_row_edit_refs(child.signal_aspects, statement_index_base);
        offset_row_edit_refs(child.signal_puts, statement_index_base);
        offset_row_edit_refs(child.beacons, statement_index_base);
        offset_row_edit_refs(child.pretrains, statement_index_base);
        offset_row_edit_refs(child.irregularities, statement_index_base);
        offset_row_edit_refs(child.backgrounds, statement_index_base);
        offset_row_edit_refs(child.adhesions, statement_index_base);
        offset_row_edit_refs(child.cab_illuminance, statement_index_base);
        offset_row_edit_refs(child.fogs, statement_index_base);
        offset_row_edit_refs(child.legacy_fogs, statement_index_base);
        offset_row_edit_refs(child.light_ambient, statement_index_base);
        offset_row_edit_refs(child.light_diffuse, statement_index_base);
        offset_row_edit_refs(child.light_direction, statement_index_base);
        offset_row_edit_refs(child.draw_distances, statement_index_base);
        offset_row_edit_refs(child.speedlimits, statement_index_base);
        auto remap_source_file_index = [&](size_t& index) {
            if (index < source_file_index_map.size()) {
                index = source_file_index_map[index];
            }
        };
        for (auto& row : child.structure_models) {
            remap_source_file_index(row.source_file_index);
        }
        for (auto& row : child.sound_list) {
            remap_source_file_index(row.source_file_index);
        }

        int order_base = ctx_.parse_order;
        auto offset_order = [order_base](int& order) {
            if (order > 0) order += order_base;
        };
        for (auto& row : child.station_puts) offset_order(row.order);
        for (auto& row : child.curves) offset_order(row.order);
        for (auto& row : child.gradients) offset_order(row.order);
        for (auto& row : child.other_track_changes) offset_order(row.order);
        for (auto& row : child.station_list) offset_order(row.order);
        for (auto& row : child.structure_loads) offset_order(row.order);
        for (auto& row : child.structure_puts) offset_order(row.order);
        for (auto& row : child.structure_betweens) offset_order(row.order);
        for (auto& row : child.other_trains) offset_order(row.order);
        for (auto& row : child.other_train_enables) offset_order(row.order);
        for (auto& row : child.other_train_stops) offset_order(row.order);
        for (auto& row : child.repeaters) offset_order(row.order);
        for (auto& row : child.section_begins) offset_order(row.order);
        for (auto& row : child.section_speed_limits) offset_order(row.order);
        for (auto& row : child.signal_puts) offset_order(row.order);
        for (auto& row : child.beacons) offset_order(row.order);
        for (auto& row : child.pretrains) offset_order(row.order);
        for (auto& row : child.map_sounds) offset_order(row.order);
        for (auto& row : child.map_sound_3d) offset_order(row.order);
        for (auto& row : child.rolling_noises) offset_order(row.order);
        for (auto& row : child.flange_noises) offset_order(row.order);
        for (auto& row : child.joint_noises) offset_order(row.order);
        for (auto& row : child.irregularities) offset_order(row.order);
        for (auto& row : child.backgrounds) offset_order(row.order);
        for (auto& row : child.adhesions) offset_order(row.order);
        for (auto& row : child.cab_illuminance) offset_order(row.order);
        for (auto& row : child.fogs) offset_order(row.order);
        for (auto& row : child.legacy_fogs) offset_order(row.order);
        for (auto& row : child.light_ambient) offset_order(row.order);
        for (auto& row : child.light_diffuse) offset_order(row.order);
        for (auto& row : child.light_direction) offset_order(row.order);
        for (auto& row : child.draw_distances) offset_order(row.order);
        for (auto& row : child.speedlimits) offset_order(row.order);
        for (auto& row : child.variable_assignments) offset_order(row.order);
        for (auto& row : child.resource_list_loads) offset_order(row.order);
        ctx_.parse_order += child.parse_order;

        if (child.has_distance_assignment) {
            ctx_.distance = child.distance;
            ctx_.distance_expression = child.distance_expression;
            ctx_.has_distance_assignment = true;
        }
        bool parent_variable_environment_changed = false;
        for (const std::string& key : child.variable_writes) {
            auto it = child.variables.find(key);
            if (it != child.variables.end()) {
                ctx_.variables[key] = std::move(it->second);
                parent_variable_environment_changed = true;
            }
        }
        if (parent_variable_environment_changed && ctx_.parse_options.collect_edit_metadata) {
            rebuild_variable_environment_snapshot(ctx_);
        }

        for (auto& statement : child.parsed_statements) {
            ctx_.parsed_statements.push_back(std::move(statement));
        }
        ctx_.controlpoints.insert(ctx_.controlpoints.end(), child.controlpoints.begin(), child.controlpoints.end());
        for (auto& row : child.own_track) ctx_.own_track.push_back(std::move(row));
        for (auto& row : child.curves) ctx_.curves.push_back(std::move(row));
        for (auto& row : child.gradients) ctx_.gradients.push_back(std::move(row));
        for (auto& row : child.other_track_changes) ctx_.other_track_changes.push_back(std::move(row));
        for (auto& kv : child.station_position) ctx_.station_position[kv.first] = std::move(kv.second);
        for (auto& kv : child.station_key) ctx_.station_key[kv.first] = std::move(kv.second);
        for (auto& row : child.station_list) ctx_.station_list.push_back(std::move(row));
        for (auto& row : child.station_puts) ctx_.station_puts.push_back(std::move(row));
        for (const std::string& key : child.othertrack_order) {
            ensure_othertrack(ctx_, key);
            auto& dest = ctx_.othertrack[key];
            auto& src = child.othertrack[key];
            for (auto& row : src) dest.push_back(std::move(row));
        }
        for (auto& row : child.structure_loads) ctx_.structure_loads.push_back(std::move(row));
        for (auto& row : child.structure_models) ctx_.structure_models.push_back(std::move(row));
        for (auto& row : child.sound_list) ctx_.sound_list.push_back(std::move(row));
        for (auto& row : child.other_trains) ctx_.other_trains.push_back(std::move(row));
        for (auto& row : child.other_train_enables) ctx_.other_train_enables.push_back(std::move(row));
        for (auto& row : child.other_train_stops) ctx_.other_train_stops.push_back(std::move(row));
        for (auto& row : child.other_train_structure_keys) ctx_.other_train_structure_keys.push_back(std::move(row));
        for (auto& row : child.other_train_sound_3d_keys) ctx_.other_train_sound_3d_keys.push_back(std::move(row));
        ctx_.parsed_other_train_files.insert(child.parsed_other_train_files.begin(), child.parsed_other_train_files.end());
        for (auto& row : child.map_sounds) ctx_.map_sounds.push_back(std::move(row));
        for (auto& row : child.map_sound_3d) ctx_.map_sound_3d.push_back(std::move(row));
        for (auto& row : child.rolling_noises) ctx_.rolling_noises.push_back(std::move(row));
        for (auto& row : child.flange_noises) ctx_.flange_noises.push_back(std::move(row));
        for (auto& row : child.joint_noises) ctx_.joint_noises.push_back(std::move(row));
        for (auto& row : child.structure_puts) ctx_.structure_puts.push_back(std::move(row));
        for (auto& row : child.structure_betweens) ctx_.structure_betweens.push_back(std::move(row));
        for (auto& row : child.repeaters) ctx_.repeaters.push_back(std::move(row));
        for (auto& row : child.section_begins) ctx_.section_begins.push_back(std::move(row));
        for (auto& row : child.section_speed_limits) ctx_.section_speed_limits.push_back(std::move(row));
        for (auto& row : child.signal_aspects) ctx_.signal_aspects.push_back(std::move(row));
        for (auto& row : child.signal_puts) ctx_.signal_puts.push_back(std::move(row));
        for (auto& row : child.beacons) ctx_.beacons.push_back(std::move(row));
        for (auto& row : child.pretrains) ctx_.pretrains.push_back(std::move(row));
        for (auto& row : child.irregularities) ctx_.irregularities.push_back(std::move(row));
        for (auto& row : child.backgrounds) ctx_.backgrounds.push_back(std::move(row));
        for (auto& row : child.adhesions) ctx_.adhesions.push_back(std::move(row));
        for (auto& row : child.cab_illuminance) ctx_.cab_illuminance.push_back(std::move(row));
        for (auto& row : child.fogs) ctx_.fogs.push_back(std::move(row));
        for (auto& row : child.legacy_fogs) ctx_.legacy_fogs.push_back(std::move(row));
        for (auto& row : child.light_ambient) ctx_.light_ambient.push_back(std::move(row));
        for (auto& row : child.light_diffuse) ctx_.light_diffuse.push_back(std::move(row));
        for (auto& row : child.light_direction) ctx_.light_direction.push_back(std::move(row));
        for (auto& row : child.draw_distances) ctx_.draw_distances.push_back(std::move(row));
        for (auto& row : child.speedlimits) ctx_.speedlimits.push_back(std::move(row));
        for (auto& row : child.variable_assignments) {
            ctx_.variable_assignments.push_back(std::move(row));
        }
        for (auto& row : child.resource_list_loads) {
            ctx_.resource_list_loads.push_back(std::move(row));
        }
        for (auto& diagnostic : child.diagnostics) {
            ctx_.diagnostics.push_back(std::move(diagnostic));
        }
        for (auto& reference : child.deferred_key_references) {
            ctx_.deferred_key_references.push_back(std::move(reference));
        }
        for (auto& event : child.transition_events) {
            ctx_.transition_events.push_back(std::move(event));
        }
    }

    void flush_pending_includes() {
        if (pending_includes_.empty()) return;

        std::string first_fatal_error;
        for (auto& pending : pending_includes_) {
            IncludeResult result;
            try {
                result = pending.future.get();
            } catch (const std::exception& e) {
                result.error = e.what();
                result.fatal = true;
            }
            if (include_result_is_stale(pending, result.context)) {
                result = parse_include_context(
                    make_child_seed(pending.path, pending.include_path,
                                    pending.include_invocation_key), pending.path);
            }
            if (!result.error.empty()) {
                for (auto& diagnostic : result.context.diagnostics) {
                    ctx_.diagnostics.push_back(std::move(diagnostic));
                }
                if (result.fatal) {
                    if (first_fatal_error.empty()) first_fatal_error = result.error;
                    continue;
                }
                merge_file_structure(result.context);
                MapDiagnostic diagnostic = pending.source;
                diagnostic.message = "Include load skipped: " + pending.include_path + ": " +
                    result.error;
                ctx_.diagnostics.push_back(std::move(diagnostic));
                continue;
            }
            merge_include_context(result.context);
        }
        pending_includes_.clear();
        if (!first_fatal_error.empty()) {
            throw FatalParseError("Include load failed: " + first_fatal_error);
        }
    }

    std::string map_statement_kind(const std::vector<MapObject>& objects,
                                   const MapFunction& function) const {
        std::string kind;
        for (const MapObject& object : objects) {
            if (!kind.empty()) kind += ".";
            kind += object.label;
        }
        if (!kind.empty()) kind += ".";
        kind += function.label;
        return kind;
    }

    ParsedMapElement parse_map_element() {
        ParsedMapElement element;
        do {
            element.objects.push_back(parse_map_object());
            expect('.');
        } while (!next_is_function());
        element.function = parse_map_function();
        return element;
    }

    bool next_is_function() {
        size_t p = pos_;
        skip_at(p);
        if (p >= src_.size() || !is_ident_start(static_cast<unsigned char>(src_[p]))) return false;
        skip_label_at(p);
        skip_at(p);
        return p < src_.size() && src_[p] == '(';
    }

    MapObject parse_map_object() {
        MapObject object;
        object.label = parse_label();
        if (accept('[')) {
            object.has_key = true;
            object.key = parse_expression();
            expect(']');
        }
        return object;
    }

    MapFunction parse_map_function() {
        MapFunction function;
        function.label = parse_label();
        expect('(');
        size_t args_start = pos_;
        parse_map_args(function.args);
        function.explicit_argument_count =
            function.args.size() == 1 && function.args.front().is_null() &&
                    trim_field_copy(src_.substr(args_start, pos_ - args_start - 1)).empty()
                ? 0
                : function.args.size();
        size_t args_end = pos_ > args_start ? pos_ - 1 : args_start;
        function.raw_arguments = src_.substr(args_start, args_end - args_start);
        return function;
    }

    void parse_map_args(std::vector<Value>& args) {
        skip();
        if (accept(')')) {
            args.push_back(Value::null());
            return;
        }
        while (true) {
            skip();
            if (peek() == ',' || peek() == ')') {
                args.push_back(Value::null());
            } else {
                args.push_back(parse_expression());
            }
            skip();
            if (accept(',')) continue;
            expect(')');
            break;
        }
    }

    Value parse_expression(int min_prec = 0) {
        Value lhs = parse_prefix();
        while (true) {
            skip();
            char op = peek();
            int prec = precedence(op);
            if (prec < min_prec) break;
            ++pos_;
            Value rhs = parse_expression(prec + 1);
            lhs = apply_binary(op, lhs, rhs);
        }
        return lhs;
    }

    int precedence(char op) const {
        if (op == '+' || op == '-') return 10;
        if (op == '*' || op == '/' || op == '%') return 20;
        return -1;
    }

    Value parse_prefix() {
        skip();
        if (accept('+')) return parse_prefix();
        if (accept('-')) {
            Value v = parse_prefix();
            return Value::num(-as_number(v));
        }
        return parse_primary();
    }

    Value parse_primary() {
        skip();
        if (accept('(')) {
            Value v = parse_expression();
            expect(')');
            return v;
        }
        if (peek() == '\'') return parse_string();
        if (peek() == '$') {
            ++pos_;
            std::string name = ascii_lower(parse_variable_name());
            note_variable_read(ctx_, name);
            auto it = ctx_.variables.find(name);
            if (it == ctx_.variables.end()) throw std::runtime_error("Undefined variable: " + name);
            return it->second;
        }
        if (std::isdigit(static_cast<unsigned char>(peek())) || peek() == '.') {
            return parse_number();
        }
        if (is_ident_start(static_cast<unsigned char>(peek()))) {
            std::string label = parse_label();
            std::string lower = ascii_lower(label);
            skip();
            if (accept('(')) {
                std::vector<Value> args;
                skip();
                if (!accept(')')) {
                    while (true) {
                        args.push_back(parse_expression());
                        if (accept(',')) continue;
                        expect(')');
                        break;
                    }
                }
                return call_function(lower, args);
            }
            if (lower == "null") return Value::null();
            if (lower == "distance") {
                note_distance_use(ctx_);
                return Value::num(ctx_.distance);
            }
            throw std::runtime_error("Unknown predefined variable: " + label);
        }
        throw std::runtime_error("Expected expression at byte " + std::to_string(pos_));
    }

    bool next_is_variable_assignment() const {
        size_t p = pos_;
        skip_at(p);
        if (p >= src_.size() || src_[p] != '$') return false;
        ++p;
        skip_at(p);
        if (p >= src_.size() || !is_ident_part(static_cast<unsigned char>(src_[p]))) return false;
        ++p;
        while (p < src_.size() && is_ident_part(static_cast<unsigned char>(src_[p]))) ++p;
        skip_at(p);
        return p < src_.size() && src_[p] == '=';
    }

    Value parse_string() {
        expect('\'');
        size_t start = pos_;
        while (!eof() && peek() != '\'') ++pos_;
        std::string s = src_.substr(start, pos_ - start);
        expect('\'');
        return Value::str(s);
    }

    Value parse_number() {
        skip();
        const char* begin = src_.c_str() + pos_;
        char* end = nullptr;
        errno = 0;
        double value = std::strtod(begin, &end);
        if (end == begin || errno == ERANGE) {
            throw std::runtime_error("Invalid number at byte " + std::to_string(pos_));
        }
        pos_ += static_cast<size_t>(end - begin);
        return Value::num(value);
    }

    Value apply_binary(char op, const Value& lhs, const Value& rhs) {
        if (op == '+') {
            if ((lhs.is_string() && rhs.is_string()) || (!lhs.is_string() && !rhs.is_string())) {
                if (lhs.is_string()) return Value::str(lhs.text + rhs.text);
                return Value::num(as_number(lhs) + as_number(rhs));
            }
            if (lhs.is_string()) {
                return Value::str(lhs.text + truncated_integer_or_number_text(as_number(rhs)));
            }
            return Value::str(truncated_integer_or_number_text(as_number(lhs)) + rhs.text);
        }
        if (op == '-') return Value::num(as_number(lhs) - as_number(rhs));
        if (op == '*') return Value::num(as_number(lhs) * as_number(rhs));
        if (op == '/') {
            double a = as_number(lhs);
            double b = as_number(rhs);
            return Value::num(b != 0.0 ? a / b : std::copysign(k_inf, a));
        }
        if (op == '%') return Value::num(std::fmod(as_number(lhs), as_number(rhs)));
        throw std::runtime_error("Unknown operator");
    }

    Value call_function(const std::string& label, const std::vector<Value>& args) {
        static thread_local std::mt19937 rng{std::random_device{}()};
        if (label == "rand") {
            if (args.empty()) {
                return Value::num(std::uniform_real_distribution<double>(0.0, 1.0)(rng));
            }
            return Value::num(std::uniform_real_distribution<double>(0.0, as_number(args[0]))(rng));
        }
        if (label == "abs") return Value::num(std::fabs(as_number(args.at(0))));
        if (label == "sin") return Value::num(std::sin(as_number(args.at(0))));
        if (label == "cos") return Value::num(std::cos(as_number(args.at(0))));
        if (label == "atan2") return Value::num(std::atan2(as_number(args.at(0)), as_number(args.at(1))));
        if (label == "sqrt") return Value::num(std::sqrt(as_number(args.at(0))));
        if (label == "exp") return Value::num(std::exp(as_number(args.at(0))));
        if (label == "log") return Value::num(std::log(as_number(args.at(0))));
        if (label == "floor") return Value::num(std::floor(as_number(args.at(0))));
        if (label == "ceil") return Value::num(std::ceil(as_number(args.at(0))));
        if (label == "pow") return Value::num(std::pow(as_number(args.at(0)), as_number(args.at(1))));
        throw std::runtime_error("Unknown function: " + label);
    }

    enum class RuleArgumentKind {
        Any,
        Number,
        Text,
    };

    enum class RuleKeyUse {
        Any,
        Required,
        Forbidden,
    };

    struct MethodRule {
        std::vector<size_t> allowed_counts;
        size_t minimum_count = 0;
        size_t maximum_count = 0;
        std::vector<RuleArgumentKind> argument_kinds;
        RuleArgumentKind tail_kind = RuleArgumentKind::Any;
        RuleKeyUse key_use = RuleKeyUse::Any;

        MethodRule(std::vector<size_t> counts = {}, size_t minimum = 0,
                   size_t maximum = 0,
                   std::vector<RuleArgumentKind> kinds = {},
                   RuleArgumentKind tail = RuleArgumentKind::Any,
                   RuleKeyUse key = RuleKeyUse::Any)
            : allowed_counts(std::move(counts)),
              minimum_count(minimum),
              maximum_count(maximum),
              argument_kinds(std::move(kinds)),
              tail_kind(tail),
              key_use(key) {}
    };

    static const std::unordered_map<std::string, MethodRule>& method_rules() {
        using A = RuleArgumentKind;
        using K = RuleKeyUse;
        static const std::unordered_map<std::string, MethodRule> rules = {
            {"curve.gauge", {{1}, 0, 0, {A::Number}}},
            {"curve.setgauge", {{1}, 0, 0, {A::Number}}},
            {"curve.setcenter", {{1}, 0, 0, {A::Number}}},
            {"curve.setfunction", {{1}, 0, 0, {A::Number}}},
            {"curve.begintransition", {{0}}},
            {"curve.begin", {{1, 2}, 0, 0, {A::Number, A::Number}}},
            {"curve.begincircular", {{2}, 0, 0, {A::Number, A::Number}}},
            {"curve.change", {{1, 2}, 0, 0, {A::Number, A::Number}}},
            {"curve.end", {{0}}},
            {"curve.interpolate", {{0, 1, 2}, 0, 0, {A::Number, A::Number}}},
            {"gradient.begintransition", {{0}}},
            {"gradient.begin", {{1}, 0, 0, {A::Number}}},
            {"gradient.beginconst", {{1}, 0, 0, {A::Number}}},
            {"gradient.end", {{0}}},
            {"gradient.interpolate", {{0, 1}, 0, 0, {A::Number}}},
            {"legacy.turn", {{1}, 0, 0, {A::Number}}},
            {"legacy.curve", {{1, 2}, 0, 0, {A::Number, A::Number}}},
            {"legacy.pitch", {{1}, 0, 0, {A::Number}}},
            {"legacy.fog", {{5}, 0, 0,
                            {A::Number, A::Number, A::Number, A::Number, A::Number}}},
            {"station.load", {{1}, 0, 0, {A::Text}, A::Any, K::Forbidden}},
            {"station.put", {{1, 2, 3, 4}, 0, 0,
                             {A::Any, A::Number, A::Number, A::Number}}},
            {"track.position", {{3, 4, 5}, 0, 0,
                                {A::Any, A::Number, A::Number, A::Number, A::Number},
                                A::Any, K::Required}},
            {"track.gauge", {{2}, 0, 0, {A::Any, A::Number}, A::Any, K::Required}},
            {"track.cant", {{2}, 0, 0, {A::Any, A::Number}, A::Any, K::Required}},
            {"track.x.interpolate", {{1, 2, 3}, 0, 0,
                                     {A::Any, A::Number, A::Number}, A::Any, K::Required}},
            {"track.y.interpolate", {{1, 2, 3}, 0, 0,
                                     {A::Any, A::Number, A::Number}, A::Any, K::Required}},
            {"track.cant.setgauge", {{2}, 0, 0, {A::Any, A::Number}, A::Any, K::Required}},
            {"track.cant.setcenter", {{2}, 0, 0, {A::Any, A::Number}, A::Any, K::Required}},
            {"track.cant.setfunction", {{2}, 0, 0, {A::Any, A::Number}, A::Any, K::Required}},
            {"track.cant.begintransition", {{1}, 0, 0, {}, A::Any, K::Required}},
            {"track.cant.begin", {{2}, 0, 0, {A::Any, A::Number}, A::Any, K::Required}},
            {"track.cant.end", {{1}, 0, 0, {}, A::Any, K::Required}},
            {"track.cant.interpolate", {{1, 2}, 0, 0, {A::Any, A::Number}, A::Any, K::Required}},
            {"speedlimit.begin", {{1}, 0, 0, {A::Number}}},
            {"speedlimit.end", {{0}}},
            {"section.begin", {{}, 1, std::numeric_limits<size_t>::max(), {},
                               A::Number}},
            {"section.beginnew", {{}, 1, std::numeric_limits<size_t>::max(), {},
                                  A::Number}},
            {"section.setspeedlimit", {{}, 1, std::numeric_limits<size_t>::max(), {},
                                       A::Number}},
            {"signal.load", {{1}, 0, 0, {A::Text}, A::Any, K::Forbidden}},
            {"signal.speedlimit", {{}, 1, std::numeric_limits<size_t>::max(), {},
                                   A::Number, K::Forbidden}},
            {"signal.put", {{5, 11}, 0, 0,
                            {A::Any, A::Number, A::Any, A::Number, A::Number,
                             A::Number, A::Number, A::Number, A::Number, A::Number,
                             A::Number},
                            A::Any, K::Required}},
            {"structure.load", {{1}, 0, 0, {A::Text}, A::Any, K::Forbidden}},
            {"structure.put", {{10}, 0, 0,
                               {A::Any, A::Any, A::Number, A::Number, A::Number,
                                A::Number, A::Number, A::Number, A::Number, A::Number}}},
            {"structure.put0", {{4}, 0, 0,
                                {A::Any, A::Any, A::Number, A::Number}}},
            {"structure.putbetween", {{3, 4}, 0, 0,
                                      {A::Any, A::Any, A::Any, A::Number}}},
            {"beacon.put", {{3}, 0, 0, {A::Number, A::Number, A::Number}}},
            {"pretrain.pass", {{1}}},
            {"sound.load", {{1}, 0, 0, {A::Text}, A::Any, K::Forbidden}},
            {"sound.play", {{1}, 0, 0, {}, A::Any, K::Required}},
            {"sound3d.load", {{1}, 0, 0, {A::Text}, A::Any, K::Forbidden}},
            {"sound3d.put", {{3}, 0, 0, {A::Any, A::Number, A::Number},
                             A::Any, K::Required}},
            {"train.add", {{4}, 0, 0,
                           {A::Any, A::Text, A::Any, A::Number}}},
            {"train.load", {{4}, 0, 0,
                            {A::Any, A::Text, A::Any, A::Number},
                            A::Any, K::Required}},
            {"train.enable", {{2}, 0, 0, {}, A::Any, K::Required}},
            {"train.stop", {{5}, 0, 0,
                            {A::Any, A::Number, A::Number, A::Number, A::Number},
                            A::Any, K::Required}},
            {"rollingnoise.change", {{1}, 0, 0, {A::Number}}},
            {"flangenoise.change", {{1}, 0, 0, {A::Number}}},
            {"jointnoise.play", {{1}, 0, 0, {A::Number}}},
            {"repeater.begin", {{}, 12, std::numeric_limits<size_t>::max(),
                                {A::Any, A::Any, A::Number, A::Number, A::Number,
                                 A::Number, A::Number, A::Number, A::Number, A::Number,
                                 A::Number},
                                A::Any, K::Required}},
            {"repeater.begin0", {{}, 6, std::numeric_limits<size_t>::max(),
                                 {A::Any, A::Any, A::Number, A::Number, A::Number},
                                 A::Any, K::Required}},
            {"repeater.end", {{1}, 0, 0, {}, A::Any, K::Required}},
            {"irregularity.change", {{6}, 0, 0,
                                     {A::Number, A::Number, A::Number, A::Number,
                                      A::Number, A::Number}}},
            {"background.change", {{1}}},
            {"adhesion.change", {{1, 3}, 0, 0,
                                 {A::Number, A::Number, A::Number}}},
            {"cabilluminance.interpolate", {{0, 1}, 0, 0, {A::Number}}},
            {"cabilluminance.set", {{1}, 0, 0, {A::Number}}},
            {"fog.interpolate", {{0, 1, 4}, 0, 0,
                                 {A::Number, A::Number, A::Number, A::Number}}},
            {"fog.set", {{4}, 0, 0,
                         {A::Number, A::Number, A::Number, A::Number}}},
            {"drawdistance.change", {{1}, 0, 0, {A::Number}}},
            {"light.ambient", {{3}, 0, 0, {A::Number, A::Number, A::Number}}},
            {"light.diffuse", {{3}, 0, 0, {A::Number, A::Number, A::Number}}},
            {"light.direction", {{2}, 0, 0, {A::Number, A::Number}}},
        };
        return rules;
    }

    static std::string object_path(const std::vector<std::string>& labels) {
        std::string path;
        for (const std::string& label : labels) {
            if (!path.empty()) path += ".";
            path += label;
        }
        return path;
    }

    bool validate_statement(const std::vector<MapObject>& objects,
                            const MapFunction& function,
                            const std::vector<std::string>& labels,
                            std::string& rule_key) {
        const std::string path = object_path(labels);
        const std::string method = ascii_lower(function.label);
        rule_key = path + "." + method;
        const auto& rules = method_rules();
        const auto rule_it = rules.find(rule_key);
        if (rule_it == rules.end()) {
            bool known_path = false;
            const std::string prefix = path + ".";
            for (const auto& candidate : rules) {
                if (candidate.first.rfind(prefix, 0) == 0) {
                    known_path = true;
                    break;
                }
            }
            if (known_path) {
                add_warning(current_statement_start_, rule_key,
                            "Unknown submethod '" + function.label + "' for '" + path + "'.");
            } else {
                add_warning(current_statement_start_, rule_key,
                            "Unknown element name '" + path + "'.");
            }
            return false;
        }

        const MethodRule& rule = rule_it->second;
        const bool has_key = !objects.empty() && objects.front().has_key;
        if (rule.key_use == RuleKeyUse::Required && !has_key) {
            add_warning(current_statement_start_, rule_key,
                        "An element key is required.");
            return false;
        }
        if (rule.key_use == RuleKeyUse::Forbidden && has_key) {
            add_warning(current_statement_start_, rule_key,
                        "An element key is not allowed.");
            return false;
        }

        const size_t key_argument_count = has_key ? 1 : 0;
        const size_t count = function.explicit_argument_count + key_argument_count;
        auto argument_at = [&](size_t argument_index) -> const Value& {
            return has_key && argument_index == 0
                       ? objects.front().key
                       : function.args[argument_index - key_argument_count];
        };
        bool count_is_valid = rule.allowed_counts.empty()
                                  ? count >= rule.minimum_count && count <= rule.maximum_count
                                  : std::find(rule.allowed_counts.begin(), rule.allowed_counts.end(),
                                              count) != rule.allowed_counts.end();
        if (!count_is_valid) {
            std::ostringstream expected;
            if (!rule.allowed_counts.empty()) {
                for (size_t i = 0; i < rule.allowed_counts.size(); ++i) {
                    if (i != 0) expected << (i + 1 == rule.allowed_counts.size() ? " or " : ", ");
                    expected << rule.allowed_counts[i];
                }
            } else {
                expected << "at least " << rule.minimum_count;
            }
            add_warning(current_statement_start_, rule_key,
                        "Parameter count error: expected " + expected.str() +
                            ", got " + std::to_string(count) + ".");
            return false;
        }

        for (size_t i = 0; i < count; ++i) {
            const Value& argument = argument_at(i);
            if (argument.is_null()) continue;
            const RuleArgumentKind kind =
                i < rule.argument_kinds.size() ? rule.argument_kinds[i] : rule.tail_kind;
            const bool valid =
                kind == RuleArgumentKind::Any ||
                (kind == RuleArgumentKind::Number && argument.is_number()) ||
                (kind == RuleArgumentKind::Text && argument.is_string());
            if (!valid) {
                add_warning(
                    current_statement_start_, rule_key,
                    "Parameter content error: argument " + std::to_string(i + 1) +
                        (kind == RuleArgumentKind::Number ? " must be numeric." :
                                                           " must be a quoted file path."));
                return false;
            }
        }
        return true;
    }

    void add_deferred_key(DeferredKeyKind kind, const Value& value,
                          const std::string& statement_kind) {
        const std::string display_key = trim_field_copy(key_text(value));
        const std::string key = ascii_lower(display_key);
        if (key.empty()) return;
        DeferredKeyReference reference;
        reference.kind = kind;
        reference.key = key;
        reference.display_key = display_key;
        reference.source =
            diagnostic_source(current_statement_start_, statement_kind);
        ctx_.deferred_key_references.push_back(std::move(reference));
    }

    void add_loaded_deferred_key(DeferredKeyKind kind, const std::string& key_text_value,
                                 const LoadedText& loaded, int line,
                                 const std::string& statement_kind) {
        const std::string display_key = trim_field_copy(key_text_value);
        const std::string key = ascii_lower(display_key);
        if (key.empty()) return;
        DeferredKeyReference reference;
        reference.kind = kind;
        reference.key = key;
        reference.display_key = display_key;
        reference.source.file_path = loaded.normalized_path;
        reference.source.line = line;
        reference.source.column = 1;
        reference.source.statement_kind = statement_kind;
        ctx_.deferred_key_references.push_back(std::move(reference));
    }

    void record_deferred_semantics(const std::vector<MapObject>& objects,
                                   const MapFunction& function,
                                   const std::string& kind) {
        const bool has_key = !objects.empty() && objects.front().has_key;
        const size_t key_argument_count = has_key ? 1 : 0;
        const size_t argument_count =
            function.explicit_argument_count + key_argument_count;
        auto argument_at = [&](size_t argument_index) -> const Value& {
            return has_key && argument_index == 0
                       ? objects.front().key
                       : function.args[argument_index - key_argument_count];
        };

        auto add_transition = [&](TransitionEventKind event_kind) {
            TransitionEvent event;
            event.kind = event_kind;
            event.argument_count = static_cast<int>(function.explicit_argument_count);
            event.source = diagnostic_source(current_statement_start_, kind);
            ctx_.transition_events.push_back(std::move(event));
        };
        if (kind == "curve.begintransition") add_transition(TransitionEventKind::CurveBeginTransition);
        else if (kind == "curve.begin") add_transition(TransitionEventKind::CurveBegin);
        else if (kind == "curve.begincircular") add_transition(TransitionEventKind::CurveBeginCircular);
        else if (kind == "curve.end") add_transition(TransitionEventKind::CurveEnd);
        else if (kind == "gradient.begintransition") add_transition(TransitionEventKind::GradientBeginTransition);
        else if (kind == "gradient.begin" || kind == "gradient.beginconst") {
            add_transition(TransitionEventKind::GradientBegin);
        } else if (kind == "gradient.end") {
            add_transition(TransitionEventKind::GradientEnd);
        }

        if ((kind == "structure.put" || kind == "structure.put0" ||
             kind == "structure.putbetween" || kind == "background.change") &&
            argument_count != 0) {
            add_deferred_key(DeferredKeyKind::Structure, argument_at(0), kind);
        } else if (kind == "repeater.begin" && argument_count > 11) {
            for (size_t i = 11; i < argument_count; ++i) {
                add_deferred_key(DeferredKeyKind::Structure, argument_at(i), kind);
            }
        } else if (kind == "repeater.begin0" && argument_count > 5) {
            for (size_t i = 5; i < argument_count; ++i) {
                add_deferred_key(DeferredKeyKind::Structure, argument_at(i), kind);
            }
        } else if (kind == "station.put" && argument_count != 0) {
            add_deferred_key(DeferredKeyKind::Station, argument_at(0), kind);
        } else if (kind == "signal.put" && argument_count != 0) {
            add_deferred_key(DeferredKeyKind::SignalAspect, argument_at(0), kind);
        } else if (kind == "sound.play" && argument_count != 0) {
            add_deferred_key(DeferredKeyKind::Sound, argument_at(0), kind);
        } else if (kind == "sound3d.put" && argument_count != 0) {
            add_deferred_key(DeferredKeyKind::Sound3D, argument_at(0), kind);
        } else if ((kind == "train.enable" || kind == "train.stop") &&
                   argument_count != 0) {
            add_deferred_key(DeferredKeyKind::Train, argument_at(0), kind);
        }
    }

    void dispatch(const std::vector<MapObject>& objects, const MapFunction& function) {
        std::vector<std::string> labels;
        labels.reserve(objects.size());
        for (const auto& object : objects) labels.push_back(ascii_lower(object.label));
        std::string statement_kind;
        if (!validate_statement(objects, function, labels, statement_kind)) return;
        const std::string& first = labels.front();
        if (first == "curve" || first == "gradient" || first == "structure" ||
            first == "background" || first == "repeater" || first == "station" ||
            first == "signal" || first == "sound" || first == "sound3d" ||
            first == "train") {
            record_deferred_semantics(objects, function, statement_kind);
        }
        std::string fn = ascii_lower(function.label);

        if (first == "curve") {
            dispatch_curve(fn, function.args, function.explicit_argument_count);
        } else if (first == "gradient") {
            dispatch_gradient(fn, function.args, function.explicit_argument_count);
        } else if (first == "legacy") {
            dispatch_legacy(fn, function.args);
        } else if (first == "station") {
            std::vector<Value> args = function.args;
            if (objects.front().has_key) args.insert(args.begin(), objects.front().key);
            dispatch_station(fn, args, function.raw_arguments);
        } else if (first == "track") {
            if (!objects.front().has_key) throw std::runtime_error("Track key is required");
            dispatch_track(objects.front().key, labels, fn, function.args,
                           function.explicit_argument_count);
        } else if (first == "speedlimit") {
            dispatch_speedlimit(fn, function.args);
        } else if (first == "section") {
            dispatch_section(fn, function.args);
        } else if (first == "signal") {
            std::vector<Value> args = function.args;
            if (objects.front().has_key) args.insert(args.begin(), objects.front().key);
            dispatch_signal(fn, args, objects.front().has_key,
                            function.raw_arguments);
        } else if (first == "structure") {
            std::vector<Value> args = function.args;
            if (objects.front().has_key) args.insert(args.begin(), objects.front().key);
            dispatch_structure(fn, args, function.raw_arguments);
        } else if (first == "beacon") {
            dispatch_beacon(fn, function.args);
        } else if (first == "pretrain") {
            dispatch_pretrain(fn, function.args);
        } else if (first == "sound" || first == "sound3d") {
            dispatch_sound(fn, function.args, first == "sound3d",
                           objects.front().has_key, objects.front().key,
                           function.raw_arguments);
        } else if (first == "train") {
            std::vector<Value> args = function.args;
            if (objects.front().has_key) args.insert(args.begin(), objects.front().key);
            dispatch_train(fn, args, objects.front().has_key);
        } else if (first == "rollingnoise") {
            dispatch_rolling_noise(fn, function.args);
        } else if (first == "flangenoise") {
            dispatch_flange_noise(fn, function.args);
        } else if (first == "jointnoise") {
            dispatch_joint_noise(fn, function.args);
        } else if (first == "repeater") {
            std::vector<Value> args = function.args;
            if (objects.front().has_key) args.insert(args.begin(), objects.front().key);
            dispatch_repeater(fn, args);
        } else if (first == "irregularity") {
            dispatch_irregularity(fn, function.args);
        } else if (first == "background") {
            dispatch_background(fn, function.args);
        } else if (first == "adhesion") {
            dispatch_adhesion(fn, function.args);
        } else if (first == "cabilluminance") {
            dispatch_cab_illuminance(fn, function.args);
        } else if (first == "fog") {
            dispatch_fog(fn, function.args);
        } else if (first == "light") {
            dispatch_light(fn, function.args);
        } else if (first == "drawdistance") {
            dispatch_draw_distance(fn, function.args);
        }
    }

    void dispatch_curve(const std::string& fn, const std::vector<Value>& a,
                        size_t explicit_argument_count) {
        auto add_edit_row = [&](const std::string& method) {
            CurveEditRow row;
            row.distance = ctx_.distance;
            row.method = method;
            row.radius = a.empty() ? Value::null() : a[0];
            row.cant = a.size() > 1 ? a[1] : Value::null();
            row.argument_count = static_cast<int>(explicit_argument_count);
            row.file_path = ctx_.current_file_path;
            row.order = ctx_.next_parse_order();
            attach_active_edit_ref(ctx_, row);
            ctx_.curves.push_back(std::move(row));
        };
        if (fn == "setgauge" || fn == "gauge") put_own(ctx_, "gauge", arg_or_null(a));
        else if (fn == "setcenter") put_own(ctx_, "center", arg_or_null(a));
        else if (fn == "setfunction") put_own(ctx_, "interpolate_func", Value::str(as_number(arg_or_null(a)) == 0.0 ? "sin" : "line"));
        else if (fn == "begintransition") {
            add_edit_row("Curve.BeginTransition");
            put_own(ctx_, "radius", Value::null(), "bt");
            put_own(ctx_, "cant", Value::null(), "bt");
        } else if (fn == "begincircular" || fn == "begin" || fn == "change") {
            add_edit_row(fn == "begincircular" ? "Curve.BeginCircular" :
                         fn == "begin" ? "Curve.Begin" : "Curve.Change");
            put_own(ctx_, "radius", arg_or_null(a));
            put_own(ctx_, "cant", a.size() > 1 ? a.at(1) : Value::num(0.0));
        } else if (fn == "end") {
            add_edit_row("Curve.End");
            put_own(ctx_, "radius", Value::num(0.0));
            put_own(ctx_, "cant", Value::num(0.0));
        } else if (fn == "interpolate") {
            put_own(ctx_, "radius", arg_or_null(a), "i");
            put_own(ctx_, "cant", a.size() > 1 ? a.at(1) : Value::null(), "i");
        }
    }

    void dispatch_gradient(const std::string& fn, const std::vector<Value>& a,
                           size_t explicit_argument_count) {
        auto add_edit_row = [&](const std::string& method) {
            GradientEditRow row;
            row.distance = ctx_.distance;
            row.method = method;
            row.gradient = a.empty() ? Value::null() : a[0];
            row.argument_count = static_cast<int>(explicit_argument_count);
            row.file_path = ctx_.current_file_path;
            row.order = ctx_.next_parse_order();
            attach_active_edit_ref(ctx_, row);
            ctx_.gradients.push_back(std::move(row));
        };
        if (fn == "begintransition") {
            add_edit_row("Gradient.BeginTransition");
            put_own(ctx_, "gradient", Value::null(), "bt");
        } else if (fn == "begin" || fn == "beginconst") {
            add_edit_row(fn == "begin" ? "Gradient.Begin" : "Gradient.BeginConst");
            put_own(ctx_, "gradient", arg_or_null(a));
        } else if (fn == "end") {
            add_edit_row("Gradient.End");
            put_own(ctx_, "gradient", Value::num(0.0));
        }
        else if (fn == "interpolate") put_own(ctx_, "gradient", arg_or_null(a), "i");
    }

    void dispatch_legacy(const std::string& fn, const std::vector<Value>& a) {
        if (fn == "turn") put_own(ctx_, "turn", arg_or_null(a));
        else if (fn == "curve") {
            CurveEditRow row;
            row.distance = ctx_.distance;
            row.method = "Legacy.Curve";
            row.radius = a.empty() ? Value::null() : a[0];
            row.cant = a.size() > 1 ? a[1] : Value::null();
            row.argument_count = static_cast<int>(a.size());
            row.file_path = ctx_.current_file_path;
            row.order = ctx_.next_parse_order();
            attach_active_edit_ref(ctx_, row);
            ctx_.curves.push_back(std::move(row));
            put_own(ctx_, "radius", arg_or_null(a));
            put_own(ctx_, "cant", a.size() > 1 ? a.at(1) : Value::num(0.0));
        } else if (fn == "pitch") {
            put_own(ctx_, "gradient", arg_or_null(a));
        } else if (fn == "fog") {
            note_distance_use(ctx_);
            LegacyFogChange row;
            row.distance = ctx_.distance;
            row.start = as_number(a[0]);
            row.end = as_number(a[1]);
            row.red = as_number(a[2]);
            row.green = as_number(a[3]);
            row.blue = as_number(a[4]);
            row.file_path = ctx_.current_file_path;
            row.order = ctx_.next_parse_order();
            attach_active_edit_ref(ctx_, row);
            ctx_.legacy_fogs.push_back(std::move(row));
        }
    }

    template <typename ParseCallback>
    void load_resource_list(const std::string& path_text,
                            const std::string& header,
                            double minimum_version,
                            const std::string& statement_kind,
                            ParseCallback&& parse_callback) {
        if (path_text.empty()) return;
        try {
            const std::filesystem::path path = join_path(ctx_.rootpath, path_text);
            LoadedText loaded = load_header_text(ctx_, path, header, minimum_version);
            parse_callback(loaded);
        } catch (const std::exception& e) {
            add_warning(current_statement_start_, statement_kind,
                        "Resource list load failed: " + std::string(e.what()));
        }
    }

    void record_resource_list_load(ResourceListLoadKind kind,
                                   const std::string& path_text,
                                   const std::string& raw_argument,
                                   const std::string& statement_kind) {
        ResourceListLoad row;
        row.kind = kind;
        row.evaluated_path = path_text;
        row.raw_argument = trim_field_copy(raw_argument);
        const std::filesystem::path joined = join_path(ctx_.rootpath, path_text);
        std::error_code ec;
        std::filesystem::path resolved = std::filesystem::absolute(joined, ec);
        if (ec) resolved = joined;
        row.resolved_path = path_to_utf8(resolved.lexically_normal());
        row.file_path = ctx_.current_file_path;
        row.order = ctx_.next_parse_order();
        row.source = diagnostic_source(current_statement_start_, statement_kind);
        ctx_.resource_list_loads.push_back(std::move(row));
    }

    void dispatch_station(const std::string& fn, const std::vector<Value>& a,
                          const std::string& raw_arguments) {
        if (fn == "put" && !a.empty()) {
            note_distance_use(ctx_);
            std::string key = key_text(a.at(0));
            ctx_.station_position[ctx_.distance] = key;
            StationPut row;
            row.distance = ctx_.distance;
            row.station_key = Value::str(key);
            row.door = arg_or_null(a, 1);
            row.margin1 = arg_or_null(a, 2);
            row.margin2 = arg_or_null(a, 3);
            row.file_path = ctx_.current_file_path;
            row.order = ctx_.next_parse_order();
            attach_active_edit_ref(ctx_, row);
            ctx_.station_puts.push_back(std::move(row));
        } else if (fn == "load" && !a.empty()) {
            record_resource_list_load(ResourceListLoadKind::Station,
                                      as_text(a.at(0)), raw_arguments,
                                      "station.load");
            load_resource_list(as_text(a.at(0)), "BveTs Station List ", 0.04,
                               "station.load",
                               [&](const LoadedText& loaded) { parse_station_list(loaded); });
        }
    }

    void parse_station_list(const LoadedText& loaded) {
        register_source_file(ctx_, loaded);
        std::vector<std::string> stack = include_stack_for_file(ctx_, loaded.path);
        for_each_loaded_body_line(loaded, [&](const std::string& line, size_t line_start,
                                              size_t line_end, int line_number) {
            std::vector<std::string> fields = parse_comma_separated_fields(line, true);
            if (fields.size() < 2) return;

            StationListEntry row;
            for (size_t i = 0; i < row.fields.size() && i < fields.size(); ++i) row.fields[i] = fields[i];
            row.order = ctx_.next_parse_order();
            row.edit_ref = add_loaded_line_statement(ctx_, loaded, stack, "StationList.Row",
                                                     line_start, line_end, line, fields, true);
            std::string key = ascii_lower(row.fields[0]);
            if (!key.empty()) ctx_.station_key[key] = row.fields[1];
            add_loaded_deferred_key(DeferredKeyKind::Sound, row.fields[9], loaded,
                                    line_number, "StationList.Row");
            add_loaded_deferred_key(DeferredKeyKind::Sound, row.fields[10], loaded,
                                    line_number, "StationList.Row");
            ctx_.station_list.push_back(std::move(row));
        });
    }

    void parse_structure_list(const LoadedText& loaded) {
        const size_t source_file_index = register_source_file_index(ctx_, loaded);
        std::vector<std::string> stack = include_stack_for_file(ctx_, loaded.path);
        for_each_loaded_body_line(loaded, [&](const std::string& line, size_t line_start,
                                              size_t line_end, int) {
            std::string trimmed = trim_field_copy(line);
            if (trimmed.empty() || trimmed[0] == '#') return;

            std::vector<std::string> fields = parse_comma_separated_fields(line, true);
            if (fields.empty()) return;

            StructureModel row;
            row.structure_key = fields[0];
            if (fields.size() > 1) row.file_path = fields[1];
            row.source_file_index = source_file_index;
            row.edit_ref = add_loaded_line_statement(ctx_, loaded, stack, "StructureList.Row",
                                                     line_start, line_end, line, fields, true);
            ctx_.structure_models.push_back(std::move(row));
        });
    }

    void parse_signal_aspect_list(const LoadedText& loaded) {
        register_source_file(ctx_, loaded);
        std::vector<std::string> stack = include_stack_for_file(ctx_, loaded.path);
        SignalAspect* current_aspect = nullptr;
        for_each_loaded_body_line(loaded, [&](const std::string& line, size_t line_start,
                                              size_t line_end, int line_number) {
            std::string trimmed = trim_field_copy(line);
            if (trimmed.empty() || trimmed[0] == '#') return;

            std::vector<std::string> fields = parse_comma_separated_fields(line, true);
            trim_trailing_empty_fields(fields);
            if (fields.empty()) return;
            const bool starts_glare_row = fields[0].empty();
            if (starts_glare_row && !current_aspect) return;

            if (!starts_glare_row) {
                SignalAspect row;
                row.signal_aspect_key = fields[0];
                row.edit_ref = add_loaded_line_statement(ctx_, loaded, stack, "SignalList.Row",
                                                         line_start, line_end, line, fields, true);
                ctx_.signal_aspects.push_back(std::move(row));
                current_aspect = &ctx_.signal_aspects.back();
            }
            for (size_t i = 1; i < fields.size(); ++i) {
                current_aspect->structure_keys.push_back(fields[i]);
                add_loaded_deferred_key(DeferredKeyKind::Structure, fields[i], loaded,
                                        line_number, "SignalList.Row");
            }
            if (!starts_glare_row) {
                current_aspect->main_structure_key_count =
                    current_aspect->structure_keys.size();
            }
            if (starts_glare_row) {
                std::vector<std::string> flattened_fields;
                flattened_fields.reserve(
                    current_aspect->structure_keys.size() + 1);
                flattened_fields.push_back(current_aspect->signal_aspect_key);
                flattened_fields.insert(
                    flattened_fields.end(),
                    current_aspect->structure_keys.begin(),
                    current_aspect->structure_keys.end());
                extend_loaded_line_statement(
                    ctx_, loaded, stack, current_aspect->edit_ref,
                    line_end, flattened_fields);
            }
        });
    }

    void parse_sound_list(const LoadedText& loaded, bool is_3d) {
        const size_t source_file_index = register_source_file_index(ctx_, loaded);
        std::vector<std::string> stack = include_stack_for_file(ctx_, loaded.path);
        for_each_loaded_body_line(loaded, [&](const std::string& line, size_t line_start,
                                              size_t line_end, int) {
            std::string trimmed = trim_field_copy(line);
            if (trimmed.empty() || trimmed[0] == '#') return;

            std::vector<std::string> fields = parse_comma_separated_fields(line, true);
            if (fields.empty()) return;

            SoundListEntry row;
            row.sound_key = fields[0];
            row.is_3d = is_3d;
            if (fields.size() > 1) row.file_path = fields[1];
            if (fields.size() > 2) {
                row.buffer_count = parse_sound_buffer_count(fields[2]);
            }
            row.source_file_index = source_file_index;
            row.edit_ref = add_loaded_line_statement(ctx_, loaded, stack,
                                                     is_3d ? "Sound3DList.Row" : "SoundList.Row",
                                                     line_start, line_end, line, fields, true);
            ctx_.sound_list.push_back(std::move(row));
        });
    }

    void parse_other_train_file(const std::filesystem::path& path) {
        std::error_code ec;
        std::filesystem::path abs = std::filesystem::absolute(path, ec);
        if (ec) abs = path;
        abs = abs.lexically_normal();
        std::string path_key = path_to_utf8(abs);
        if (!ctx_.parsed_other_train_files.insert(path_key).second) return;

        LoadedText loaded = load_header_text(ctx_, abs, "BveTs Train ", 0.0);
        register_source_file(ctx_, loaded);
        std::vector<std::string> stack = include_stack_for_file(ctx_, loaded.path);
        std::string section;
        for_each_loaded_body_line(loaded, [&](const std::string& line, size_t line_start,
                                              size_t line_end, int line_number) {
            std::string trimmed = trim_field_copy(strip_ini_comment_copy(line));
            if (trimmed.empty()) return;
            if (trimmed.front() == '[' && trimmed.back() == ']' && trimmed.size() >= 2) {
                section = ascii_lower(trim_field_copy(trimmed.substr(1, trimmed.size() - 2)));
                return;
            }

            size_t eq = trimmed.find('=');
            if (eq == std::string::npos) return;
            std::string key = ascii_lower(trim_field_copy(trimmed.substr(0, eq)));
            if (key != "key") return;

            std::string value = trim_matching_quotes(trimmed.substr(eq + 1));
            if (value.empty()) return;
            std::vector<std::string> fields{key, value};
            EditSourceRef ref = add_loaded_line_statement(ctx_, loaded, stack, "OtherTrainFile.Row",
                                                          line_start, line_end, line, fields, false);
            if (section == "structure") {
                ctx_.other_train_structure_keys.push_back({value, path_key, ref});
                add_loaded_deferred_key(DeferredKeyKind::Structure, value, loaded,
                                        line_number, "OtherTrainFile.Row");
            } else if (section == "sound3d") {
                ctx_.other_train_sound_3d_keys.push_back({value, path_key, ref});
                add_loaded_deferred_key(DeferredKeyKind::Sound3D, value, loaded,
                                        line_number, "OtherTrainFile.Row");
            }
        });
    }

    void dispatch_track(const Value& track_key, const std::vector<std::string>& labels,
                        const std::string& fn, const std::vector<Value>& a,
                        size_t explicit_argument_count) {
        auto add_change = [&](const std::string& method) {
            OtherTrackChange row;
            row.distance = ctx_.distance;
            row.track_key = track_key;
            row.method = method;
            row.parameters.assign(
                a.begin(), a.begin() + static_cast<std::ptrdiff_t>(
                    std::min(explicit_argument_count, a.size())));
            row.file_path = ctx_.current_file_path;
            row.order = ctx_.next_parse_order();
            attach_active_edit_ref(ctx_, row);
            ctx_.other_track_changes.push_back(std::move(row));
        };
        if (labels.size() == 1 && fn == "position") {
            add_change("Track.Position");
            track_position(track_key, a);
            return;
        }
        if (labels.size() == 1 && fn == "cant") {
            add_change("Track.Cant");
            put_other(ctx_, track_key, "cant", arg_or_null(a), "i");
            return;
        }
        if (labels.size() == 1 && fn == "gauge") {
            add_change("Track.Gauge");
            put_other(ctx_, track_key, "gauge", arg_or_null(a));
            return;
        }
        if (labels.size() >= 2 && (labels[1] == "x" || labels[1] == "y") && fn == "interpolate") {
            add_change(labels[1] == "x" ? "Track.X.Interpolate" : "Track.Y.Interpolate");
            setposition_interpolate(track_key, labels[1], a);
            return;
        }
        if (labels.size() >= 2 && labels[1] == "cant") {
            const std::string suffix =
                fn == "setgauge" ? "SetGauge" :
                fn == "setcenter" ? "SetCenter" :
                fn == "setfunction" ? "SetFunction" :
                fn == "begintransition" ? "BeginTransition" :
                fn == "begin" ? "Begin" :
                fn == "end" ? "End" : "Interpolate";
            add_change("Track.Cant." + suffix);
            if (fn == "setgauge") put_other(ctx_, track_key, "gauge", arg_or_null(a));
            else if (fn == "setcenter") put_other(ctx_, track_key, "center", arg_or_null(a));
            else if (fn == "setfunction") put_other(ctx_, track_key, "interpolate_func", Value::str(as_number(arg_or_null(a)) == 0.0 ? "sin" : "line"));
            else if (fn == "begintransition") put_other(ctx_, track_key, "cant", Value::null(), "bt");
            else if (fn == "begin") put_other(ctx_, track_key, "cant", arg_or_null(a), "i");
            else if (fn == "end") put_other(ctx_, track_key, "cant", Value::num(0.0), "i");
            else if (fn == "interpolate") put_other(ctx_, track_key, "cant", arg_or_null(a), "i");
        }
    }

    void setposition_interpolate(const Value& track_key, const std::string& dim, const std::vector<Value>& a) {
        if (a.empty()) {
            put_other(ctx_, track_key, dim + ".position", Value::null());
            put_other(ctx_, track_key, dim + ".radius", Value::null());
        } else if (a.size() == 1) {
            put_other(ctx_, track_key, dim + ".position", a.at(0));
            put_other(ctx_, track_key, dim + ".radius", Value::null());
        } else {
            put_other(ctx_, track_key, dim + ".position", a.at(0));
            put_other(ctx_, track_key, dim + ".radius", a.at(1));
        }
    }

    void track_position(const Value& track_key, const std::vector<Value>& a) {
        if (a.size() == 2) {
            setposition_interpolate(track_key, "x", {a[0].is_null() ? Value::num(0.0) : a[0], Value::num(0.0)});
            setposition_interpolate(track_key, "y", {a[1].is_null() ? Value::num(0.0) : a[1], Value::num(0.0)});
        } else if (a.size() == 3) {
            setposition_interpolate(track_key, "x", {a[0].is_null() ? Value::num(0.0) : a[0], a[2].is_null() ? Value::num(0.0) : a[2]});
            setposition_interpolate(track_key, "y", {a[1].is_null() ? Value::num(0.0) : a[1], Value::num(0.0)});
        } else if (a.size() >= 4) {
            setposition_interpolate(track_key, "x", {a[0].is_null() ? Value::num(0.0) : a[0], a[2].is_null() ? Value::num(0.0) : a[2]});
            setposition_interpolate(track_key, "y", {a[1].is_null() ? Value::num(0.0) : a[1], a[3].is_null() ? Value::num(0.0) : a[3]});
        }
    }

    void dispatch_speedlimit(const std::string& fn, const std::vector<Value>& a) {
        if (fn == "begin") {
            note_distance_use(ctx_);
            SpeedLimitEvent row;
            row.distance = ctx_.distance;
            row.speed = a.empty() ? Value::num(0.0) : a[0];
            row.file_path = ctx_.current_file_path;
            row.order = ctx_.next_parse_order();
            attach_active_edit_ref(ctx_, row);
            ctx_.speedlimits.push_back(std::move(row));
        } else if (fn == "end") {
            note_distance_use(ctx_);
            SpeedLimitEvent row;
            row.distance = ctx_.distance;
            row.speed = Value::null();
            row.file_path = ctx_.current_file_path;
            row.order = ctx_.next_parse_order();
            attach_active_edit_ref(ctx_, row);
            ctx_.speedlimits.push_back(std::move(row));
        }
    }

    void dispatch_section(const std::string& fn, const std::vector<Value>& a) {
        if (fn == "begin" || fn == "beginnew") {
            note_distance_use(ctx_);
            SectionBegin row;
            row.distance = ctx_.distance;
            row.method = fn == "beginnew" ? "Section.BeginNew" : "Section.Begin";
            row.signal_indices = a;
            row.file_path = ctx_.current_file_path;
            row.order = ctx_.next_parse_order();
            attach_active_edit_ref(ctx_, row);
            ctx_.section_begins.push_back(std::move(row));
        } else if (fn == "setspeedlimit") {
            note_distance_use(ctx_);
            SectionSpeedLimit row;
            row.distance = ctx_.distance;
            row.method = "Section.SetSpeedLimit";
            row.speeds = a;
            row.file_path = ctx_.current_file_path;
            row.order = ctx_.next_parse_order();
            attach_active_edit_ref(ctx_, row);
            ctx_.section_speed_limits.push_back(std::move(row));
        }
    }

    void dispatch_signal(const std::string& fn, const std::vector<Value>& a,
                         bool has_signal_key,
                         const std::string& raw_arguments) {
        if (fn == "load" && !has_signal_key && !a.empty()) {
            std::string list_path_text = as_text(a.at(0));
            record_resource_list_load(ResourceListLoadKind::Signal,
                                      list_path_text, raw_arguments,
                                      "signal.load");
            load_resource_list(list_path_text, "BveTs Signal Aspects List ", 2.0,
                               "signal.load",
                               [&](const LoadedText& loaded) { parse_signal_aspect_list(loaded); });
        } else if (fn == "speedlimit" && !has_signal_key) {
            note_distance_use(ctx_);
            SectionSpeedLimit row;
            row.distance = ctx_.distance;
            row.method = "Signal.SpeedLimit";
            row.speeds = a;
            row.file_path = ctx_.current_file_path;
            row.order = ctx_.next_parse_order();
            attach_active_edit_ref(ctx_, row);
            ctx_.section_speed_limits.push_back(std::move(row));
        } else if (fn == "put" && has_signal_key && a.size() >= 4) {
            note_distance_use(ctx_);
            SignalPut row;
            row.distance = ctx_.distance;
            row.signal_aspect_key = a[0];
            row.section = a[1];
            row.track_key = a[2];
            row.x = as_number(a[3]);
            row.y = a.size() > 4 ? as_number(a[4]) : 0.0;
            if (a.size() >= 11) {
                row.z = as_number(a[5]);
                row.rx = as_number(a[6]);
                row.ry = as_number(a[7]);
                row.rz = as_number(a[8]);
                row.tilt = as_number(a[9]);
                row.span = as_number(a[10]);
            }
            row.file_path = ctx_.current_file_path;
            row.order = ctx_.next_parse_order();
            attach_active_edit_ref(ctx_, row);
            ctx_.signal_puts.push_back(std::move(row));
        }
    }

    void dispatch_beacon(const std::string& fn, const std::vector<Value>& a) {
        if (fn != "put" || a.size() < 3) return;
        note_distance_use(ctx_);
        BeaconPut row;
        row.distance = ctx_.distance;
        row.type = a[0];
        row.section = a[1];
        row.send_data = a[2];
        row.file_path = ctx_.current_file_path;
        row.order = ctx_.next_parse_order();
        attach_active_edit_ref(ctx_, row);
        ctx_.beacons.push_back(std::move(row));
    }

    void dispatch_pretrain(const std::string& fn, const std::vector<Value>& a) {
        if (fn != "pass" || a.empty()) return;
        note_distance_use(ctx_);
        PreTrainPass row;
        row.distance = ctx_.distance;
        row.pass_time = a[0];
        row.file_path = ctx_.current_file_path;
        row.order = ctx_.next_parse_order();
        attach_active_noneditable_ref(ctx_, row);
        ctx_.pretrains.push_back(std::move(row));
    }

    void dispatch_structure(const std::string& fn, const std::vector<Value>& a,
                            const std::string& raw_arguments) {
        if (fn == "load") {
            note_distance_use(ctx_);
            StructureLoad row;
            row.distance = ctx_.distance;
            row.method = "Load";
            row.load_file_path = a.empty() ? Value::str("") : a[0];
            row.file_path = ctx_.current_file_path;
            row.order = ctx_.next_parse_order();
            attach_active_noneditable_ref(ctx_, row);
            ctx_.structure_loads.push_back(row);
            std::string list_path_text = as_text(row.load_file_path);
            record_resource_list_load(ResourceListLoadKind::Structure,
                                      list_path_text, raw_arguments,
                                      "structure.load");
            load_resource_list(list_path_text, "BveTs Structure List ", 1.0,
                               "structure.load",
                               [&](const LoadedText& loaded) { parse_structure_list(loaded); });
        } else if (fn == "put" && a.size() >= 10) {
            note_distance_use(ctx_);
            StructurePut row;
            row.distance = ctx_.distance;
            row.method = "Put";
            row.structure_key = a[0];
            row.track_key = a[1];
            row.x = as_number(a[2]); row.y = as_number(a[3]); row.z = as_number(a[4]);
            row.rx = as_number(a[5]); row.ry = as_number(a[6]); row.rz = as_number(a[7]);
            row.tilt = as_number(a[8]); row.span = as_number(a[9]);
            row.file_path = ctx_.current_file_path;
            row.order = ctx_.next_parse_order();
            attach_active_edit_ref(ctx_, row);
            ctx_.structure_puts.push_back(row);
        } else if (fn == "put0" && a.size() >= 4) {
            note_distance_use(ctx_);
            StructurePut row;
            row.distance = ctx_.distance;
            row.method = "Put0";
            row.structure_key = a[0];
            row.track_key = a[1];
            row.tilt = as_number(a[2]); row.span = as_number(a[3]);
            row.file_path = ctx_.current_file_path;
            row.order = ctx_.next_parse_order();
            attach_active_edit_ref(ctx_, row);
            ctx_.structure_puts.push_back(row);
        } else if (fn == "putbetween" && a.size() >= 3) {
            note_distance_use(ctx_);
            StructurePut row;
            row.distance = ctx_.distance;
            row.method = "PutBetween";
            row.structure_key = a[0];
            row.track_key1 = a[1];
            row.track_key2 = a[2];
            row.flag = a.size() > 3 ? as_number(a[3]) : 0.0;
            row.file_path = ctx_.current_file_path;
            row.order = ctx_.next_parse_order();
            attach_active_edit_ref(ctx_, row);
            ctx_.structure_betweens.push_back(row);
        }
    }

    void dispatch_sound(const std::string& fn, const std::vector<Value>& a,
                        bool is_3d, bool has_key, const Value& sound_key,
                        const std::string& raw_arguments) {
        if (fn == "load" && !has_key && !a.empty()) {
            std::string list_path_text = as_text(a.at(0));
            record_resource_list_load(
                is_3d ? ResourceListLoadKind::Sound3D : ResourceListLoadKind::Sound,
                list_path_text, raw_arguments,
                is_3d ? "sound3d.load" : "sound.load");
            load_resource_list(
                list_path_text, "BveTs Sound List ", 2.0,
                is_3d ? "sound3d.load" : "sound.load",
                [&](const LoadedText& loaded) { parse_sound_list(loaded, is_3d); });
        } else if (!is_3d && fn == "play" && has_key) {
            note_distance_use(ctx_);
            MapSoundPlay row;
            row.distance = ctx_.distance;
            row.sound_key = sound_key;
            row.file_path = ctx_.current_file_path;
            row.order = ctx_.next_parse_order();
            attach_active_edit_ref(ctx_, row);
            ctx_.map_sounds.push_back(std::move(row));
        } else if (is_3d && fn == "put" && has_key && a.size() >= 2) {
            note_distance_use(ctx_);
            MapSound3DPut row;
            row.distance = ctx_.distance;
            row.sound_key = sound_key;
            row.x = as_number(a[0]);
            row.y = as_number(a[1]);
            row.file_path = ctx_.current_file_path;
            row.order = ctx_.next_parse_order();
            attach_active_edit_ref(ctx_, row);
            ctx_.map_sound_3d.push_back(std::move(row));
        }
    }

    void add_other_train_definition(const std::string& method,
                                    const Value& train_key,
                                    const Value& file_path,
                                    const Value& track_key,
                                    const Value& direction) {
        note_distance_use(ctx_);
        OtherTrainDefinition row;
        row.distance = ctx_.distance;
        row.method = method;
        row.train_key = train_key;
        row.load_file_path = file_path;
        row.track_key = track_key;
        row.direction = direction;
        row.source_file_path = ctx_.current_file_path;
        row.order = ctx_.next_parse_order();
        attach_active_noneditable_ref(ctx_, row);

        std::string path_text = as_text(file_path);
        if (!path_text.empty()) {
            std::filesystem::path path = join_path(ctx_.rootpath, path_text);
            std::error_code ec;
            std::filesystem::path abs = std::filesystem::absolute(path, ec);
            if (!ec) path = abs;
            path = path.lexically_normal();
            row.resolved_file_path = path_to_utf8(path);
            try {
                parse_other_train_file(path);
            } catch (const std::exception& e) {
                add_warning(current_statement_start_, "train.load",
                            "Resource list load failed: " + std::string(e.what()));
            }
        }

        ctx_.other_trains.push_back(std::move(row));
    }

    void dispatch_train(const std::string& fn, const std::vector<Value>& a, bool has_train_key) {
        if (fn == "add" && !has_train_key && a.size() >= 4) {
            add_other_train_definition("Add", a[0], a[1], a[2], a[3]);
        } else if (fn == "load" && has_train_key && a.size() >= 4) {
            add_other_train_definition("Load", a[0], a[1], a[2], a[3]);
        } else if (fn == "enable" && has_train_key && a.size() >= 2) {
            note_distance_use(ctx_);
            OtherTrainEnable row;
            row.distance = ctx_.distance;
            row.train_key = a[0];
            row.time = a[1];
            row.file_path = ctx_.current_file_path;
            row.order = ctx_.next_parse_order();
            row.source = diagnostic_source(current_statement_start_, "train.enable");
            attach_active_noneditable_ref(ctx_, row);
            ctx_.other_train_enables.push_back(std::move(row));
        } else if (fn == "stop" && has_train_key && a.size() >= 5) {
            note_distance_use(ctx_);
            OtherTrainStop row;
            row.distance = ctx_.distance;
            row.train_key = a[0];
            row.decelerate = a[1];
            row.stop_time = a[2];
            row.accelerate = a[3];
            row.speed = a[4];
            row.file_path = ctx_.current_file_path;
            row.order = ctx_.next_parse_order();
            attach_active_noneditable_ref(ctx_, row);
            ctx_.other_train_stops.push_back(std::move(row));
        }
    }

    void dispatch_rolling_noise(const std::string& fn, const std::vector<Value>& a) {
        if (fn != "change" || a.empty()) return;
        note_distance_use(ctx_);
        RollingNoiseChange row;
        row.distance = ctx_.distance;
        row.index = a[0];
        row.file_path = ctx_.current_file_path;
        row.order = ctx_.next_parse_order();
        attach_active_edit_ref(ctx_, row);
        ctx_.rolling_noises.push_back(std::move(row));
    }

    void dispatch_flange_noise(const std::string& fn, const std::vector<Value>& a) {
        if (fn != "change" || a.empty()) return;
        note_distance_use(ctx_);
        FlangeNoiseChange row;
        row.distance = ctx_.distance;
        row.index = a[0];
        row.file_path = ctx_.current_file_path;
        row.order = ctx_.next_parse_order();
        attach_active_edit_ref(ctx_, row);
        ctx_.flange_noises.push_back(std::move(row));
    }

    void dispatch_joint_noise(const std::string& fn, const std::vector<Value>& a) {
        if (fn != "play" || a.empty()) return;
        note_distance_use(ctx_);
        JointNoisePlay row;
        row.distance = ctx_.distance;
        row.index = a[0];
        row.file_path = ctx_.current_file_path;
        row.order = ctx_.next_parse_order();
        attach_active_edit_ref(ctx_, row);
        ctx_.joint_noises.push_back(std::move(row));
    }

    void dispatch_repeater(const std::string& fn, const std::vector<Value>& a) {
        if (fn == "begin" && a.size() >= 11) {
            note_distance_use(ctx_);
            RepeaterEvent row;
            row.distance = ctx_.distance;
            row.method = "Begin";
            row.repeater_key = a[0]; row.track_key = a[1];
            row.x = as_number(a[2]); row.y = as_number(a[3]); row.z = as_number(a[4]);
            row.rx = as_number(a[5]); row.ry = as_number(a[6]); row.rz = as_number(a[7]);
            row.tilt = as_number(a[8]); row.span = as_number(a[9]); row.interval = as_number(a[10]);
            row.structure_keys.assign(a.begin() + 11, a.end());
            row.file_path = ctx_.current_file_path;
            row.order = ctx_.next_parse_order();
            attach_active_edit_ref(ctx_, row);
            ctx_.repeaters.push_back(row);
        } else if (fn == "begin0" && a.size() >= 5) {
            note_distance_use(ctx_);
            RepeaterEvent row;
            row.distance = ctx_.distance;
            row.method = "Begin0";
            row.repeater_key = a[0]; row.track_key = a[1];
            row.tilt = as_number(a[2]); row.span = as_number(a[3]); row.interval = as_number(a[4]);
            row.structure_keys.assign(a.begin() + 5, a.end());
            row.file_path = ctx_.current_file_path;
            row.order = ctx_.next_parse_order();
            attach_active_edit_ref(ctx_, row);
            ctx_.repeaters.push_back(row);
        } else if (fn == "end" && !a.empty()) {
            note_distance_use(ctx_);
            RepeaterEvent row;
            row.distance = ctx_.distance;
            row.method = "End";
            row.repeater_key = a[0];
            row.track_key = Value::str("");
            row.file_path = ctx_.current_file_path;
            row.order = ctx_.next_parse_order();
            attach_active_edit_ref(ctx_, row);
            ctx_.repeaters.push_back(row);
        }
    }

    void dispatch_irregularity(const std::string& fn, const std::vector<Value>& a) {
        if (fn != "change" || a.size() < 6) return;
        note_distance_use(ctx_);
        IrregularityChange row;
        row.distance = ctx_.distance;
        row.x = as_number(a[0]);
        row.y = as_number(a[1]);
        row.r = as_number(a[2]);
        row.lx = as_number(a[3]);
        row.ly = as_number(a[4]);
        row.lr = as_number(a[5]);
        row.file_path = ctx_.current_file_path;
        row.order = ctx_.next_parse_order();
        attach_active_edit_ref(ctx_, row);
        ctx_.irregularities.push_back(row);
    }

    void dispatch_background(const std::string& fn, const std::vector<Value>& a) {
        if (fn != "change" || a.empty() || a[0].is_null()) return;
        note_distance_use(ctx_);
        BackgroundChange row;
        row.distance = ctx_.distance;
        row.structure_key = a[0];
        row.file_path = ctx_.current_file_path;
        row.order = ctx_.next_parse_order();
        attach_active_edit_ref(ctx_, row);
        ctx_.backgrounds.push_back(std::move(row));
    }

    void dispatch_adhesion(const std::string& fn, const std::vector<Value>& a) {
        if (fn != "change" || a.empty()) return;
        note_distance_use(ctx_);
        AdhesionChange row;
        row.distance = ctx_.distance;
        row.a = a[0];
        if (a.size() >= 3) {
            row.b = a[1];
            row.c = a[2];
        }
        row.file_path = ctx_.current_file_path;
        row.order = ctx_.next_parse_order();
        attach_active_edit_ref(ctx_, row);
        ctx_.adhesions.push_back(std::move(row));
    }

    void dispatch_cab_illuminance(const std::string& fn, const std::vector<Value>& a) {
        const bool no_value = a.empty() || a[0].is_null();
        if ((fn != "interpolate" && fn != "set") || (fn == "set" && no_value)) return;
        note_distance_use(ctx_);
        CabIlluminanceChange row;
        row.distance = ctx_.distance;
        row.value = no_value ? Value::cont() : a[0];
        row.file_path = ctx_.current_file_path;
        row.order = ctx_.next_parse_order();
        attach_active_edit_ref(ctx_, row);
        ctx_.cab_illuminance.push_back(std::move(row));
    }

    void dispatch_fog(const std::string& fn, const std::vector<Value>& a) {
        if (fn != "interpolate" && fn != "set") return;
        if (fn == "set" && (a.empty() || a[0].is_null())) return;
        note_distance_use(ctx_);
        FogChange row;
        row.distance = ctx_.distance;
        if (!a.empty()) row.density = a[0];
        if (a.size() > 1) row.red = a[1];
        if (a.size() > 2) row.green = a[2];
        if (a.size() > 3) row.blue = a[3];
        row.file_path = ctx_.current_file_path;
        row.order = ctx_.next_parse_order();
        attach_active_edit_ref(ctx_, row);
        ctx_.fogs.push_back(std::move(row));
    }

    void dispatch_light(const std::string& fn, const std::vector<Value>& a) {
        if (fn == "ambient" || fn == "diffuse") {
            LightColor row;
            row.red = as_number(a[0]);
            row.green = as_number(a[1]);
            row.blue = as_number(a[2]);
            row.file_path = ctx_.current_file_path;
            row.order = ctx_.next_parse_order();
            row.source = diagnostic_source(
                current_statement_start_,
                fn == "ambient" ? "light.ambient" : "light.diffuse");
            attach_active_edit_ref(ctx_, row);
            if (fn == "ambient") {
                ctx_.light_ambient.push_back(std::move(row));
            } else {
                ctx_.light_diffuse.push_back(std::move(row));
            }
        } else if (fn == "direction") {
            LightDirection row;
            row.distance = ctx_.distance;
            row.pitch = as_number(a[0]);
            row.yaw = as_number(a[1]);
            row.file_path = ctx_.current_file_path;
            row.order = ctx_.next_parse_order();
            row.source = diagnostic_source(current_statement_start_, "light.direction");
            attach_active_edit_ref(ctx_, row);
            ctx_.light_direction.push_back(std::move(row));
        }
    }

    void dispatch_draw_distance(const std::string& fn, const std::vector<Value>& a) {
        if (fn != "change" || a.empty() || a[0].is_null()) return;
        note_distance_use(ctx_);
        DrawDistanceChange row;
        row.distance = ctx_.distance;
        row.value = as_number(a[0]);
        row.file_path = ctx_.current_file_path;
        row.order = ctx_.next_parse_order();
        attach_active_edit_ref(ctx_, row);
        ctx_.draw_distances.push_back(std::move(row));
    }
};

namespace {

std::string deferred_key_kind_name(DeferredKeyKind kind) {
    switch (kind) {
    case DeferredKeyKind::Structure: return "StructureKey";
    case DeferredKeyKind::Sound: return "SoundKey";
    case DeferredKeyKind::Sound3D: return "Sound3DKey";
    case DeferredKeyKind::Station: return "StationKey";
    case DeferredKeyKind::SignalAspect: return "SignalAspectKey";
    case DeferredKeyKind::Train: return "TrainKey";
    }
    return "Key";
}

std::string format_diagnostic(const MapDiagnostic& diagnostic) {
    std::ostringstream out;
    out << diagnostic.file_path << ":" << diagnostic.line << ":" << diagnostic.column;
    if (!diagnostic.statement_kind.empty()) {
        out << " [" << diagnostic.statement_kind << "]";
    }
    out << " " << diagnostic.message;
    return out.str();
}

std::string diagnostic_location(const MapDiagnostic& diagnostic) {
    std::ostringstream out;
    out << diagnostic.file_path << ":" << diagnostic.line << ":" << diagnostic.column;
    return out.str();
}

const char* resource_list_statement_name(ResourceListLoadKind kind) {
    switch (kind) {
    case ResourceListLoadKind::Station: return "Station.Load";
    case ResourceListLoadKind::Structure: return "Structure.Load";
    case ResourceListLoadKind::Signal: return "Signal.Load";
    case ResourceListLoadKind::Sound: return "Sound.Load";
    case ResourceListLoadKind::Sound3D: return "Sound3D.Load";
    }
    return "Load";
}

[[noreturn]] void throw_duplicate_statement(const MapDiagnostic& second,
                                            const MapDiagnostic& first,
                                            const std::string& statement) {
    MapDiagnostic diagnostic = second;
    diagnostic.message = "Duplicate " + statement +
                         "; first declaration is at " +
                         diagnostic_location(first) + ".";
    throw std::runtime_error(format_diagnostic(diagnostic));
}

template <typename Row>
void invalidate_duplicate_light_declarations(MapContext& ctx, std::vector<Row>& rows,
                                             const char* statement) {
    if (rows.size() < 2) return;
    std::stable_sort(rows.begin(), rows.end(),
                     [](const Row& left, const Row& right) {
                         return left.order < right.order;
                     });
    MapDiagnostic diagnostic = rows.front().source;
    std::ostringstream locations;
    for (size_t index = 0; index < rows.size(); ++index) {
        if (index != 0) locations << ", ";
        locations << diagnostic_location(rows[index].source);
    }
    diagnostic.message = "Multiple " + std::string(statement) +
                         " declarations are invalid in one logical map: " +
                         locations.str() + ".";
    ctx.diagnostics.push_back(std::move(diagnostic));
    rows.clear();
}

bool light_rgb_is_valid(const LightColor& row) {
    return row.red >= 0.0 && row.red <= 1.0 &&
           row.green >= 0.0 && row.green <= 1.0 &&
           row.blue >= 0.0 && row.blue <= 1.0;
}

void validate_light_statements(MapContext& ctx) {
    invalidate_duplicate_light_declarations(ctx, ctx.light_ambient, "Light.Ambient");
    invalidate_duplicate_light_declarations(ctx, ctx.light_diffuse, "Light.Diffuse");
    invalidate_duplicate_light_declarations(ctx, ctx.light_direction, "Light.Direction");

    const auto validate_color = [&ctx](std::vector<LightColor>& rows,
                                       const char* statement) {
        if (rows.empty() || light_rgb_is_valid(rows.front())) return;
        MapDiagnostic diagnostic = rows.front().source;
        diagnostic.message = std::string(statement) +
                             " RGB values must be between 0 and 1.";
        ctx.diagnostics.push_back(std::move(diagnostic));
        rows.clear();
    };
    validate_color(ctx.light_ambient, "Light.Ambient");
    validate_color(ctx.light_diffuse, "Light.Diffuse");

    if (!ctx.light_direction.empty() && ctx.light_direction.front().distance != 0.0) {
        MapDiagnostic diagnostic = ctx.light_direction.front().source;
        diagnostic.message = "Light.Direction must be declared at route distance 0.";
        ctx.diagnostics.push_back(std::move(diagnostic));
        ctx.light_direction.clear();
    }
}

void validate_unique_preview_statements(const MapContext& ctx) {
    std::vector<const ResourceListLoad*> loads;
    loads.reserve(ctx.resource_list_loads.size());
    for (const ResourceListLoad& row : ctx.resource_list_loads) loads.push_back(&row);
    std::stable_sort(loads.begin(), loads.end(),
                     [](const ResourceListLoad* left,
                        const ResourceListLoad* right) {
                         return left->order < right->order;
                     });
    std::array<const ResourceListLoad*, 5> first_loads{};
    for (const ResourceListLoad* row : loads) {
        const size_t index = static_cast<size_t>(row->kind);
        if (first_loads[index]) {
            throw_duplicate_statement(
                row->source, first_loads[index]->source,
                resource_list_statement_name(row->kind));
        }
        first_loads[index] = row;
    }

    std::vector<const OtherTrainEnable*> enables;
    enables.reserve(ctx.other_train_enables.size());
    for (const OtherTrainEnable& row : ctx.other_train_enables) enables.push_back(&row);
    std::stable_sort(enables.begin(), enables.end(),
                     [](const OtherTrainEnable* left,
                        const OtherTrainEnable* right) {
                         return left->order < right->order;
                     });
    std::unordered_map<std::string, const OtherTrainEnable*> first_enables;
    for (const OtherTrainEnable* row : enables) {
        const std::string key = ascii_lower(
            trim_field_copy(key_text(row->train_key)));
        const auto inserted = first_enables.emplace(key, row);
        if (!inserted.second) {
            throw_duplicate_statement(
                row->source, inserted.first->second->source,
                "Train[" + key + "].Enable");
        }
    }
}

void append_transition_diagnostics(MapContext& ctx) {
    bool curve_transition_pending = false;
    bool gradient_transition_pending = false;
    for (const TransitionEvent& event : ctx.transition_events) {
        auto warn = [&](const char* method) {
            MapDiagnostic diagnostic = event.source;
            diagnostic.message =
                std::string("'BeginTransition' is required before '") + method + "'.";
            ctx.diagnostics.push_back(std::move(diagnostic));
        };
        switch (event.kind) {
        case TransitionEventKind::CurveBeginTransition:
            curve_transition_pending = true;
            break;
        case TransitionEventKind::CurveBegin:
            if (event.argument_count == 2) {
                if (!curve_transition_pending) warn("Begin");
                curve_transition_pending = false;
            }
            break;
        case TransitionEventKind::CurveBeginCircular:
            if (!curve_transition_pending) warn("Begin");
            curve_transition_pending = false;
            break;
        case TransitionEventKind::CurveEnd:
            if (!curve_transition_pending) warn("End");
            curve_transition_pending = false;
            break;
        case TransitionEventKind::GradientBeginTransition:
            gradient_transition_pending = true;
            break;
        case TransitionEventKind::GradientBegin:
            if (!gradient_transition_pending) warn("Begin");
            gradient_transition_pending = false;
            break;
        case TransitionEventKind::GradientEnd:
            if (!gradient_transition_pending) warn("End");
            gradient_transition_pending = false;
            break;
        }
    }
}

void append_deferred_key_diagnostics(MapContext& ctx) {
    std::array<std::unordered_set<std::string>, 6> definitions;
    const auto index = [](DeferredKeyKind kind) {
        return static_cast<size_t>(kind);
    };
    for (const StructureModel& row : ctx.structure_models) {
        definitions[index(DeferredKeyKind::Structure)].insert(
            ascii_lower(trim_field_copy(row.structure_key)));
    }
    for (const SoundListEntry& row : ctx.sound_list) {
        definitions[index(row.is_3d ? DeferredKeyKind::Sound3D : DeferredKeyKind::Sound)]
            .insert(ascii_lower(trim_field_copy(row.sound_key)));
    }
    for (const auto& row : ctx.station_list) {
        const std::string key = ascii_lower(trim_field_copy(row.fields[0]));
        if (!key.empty()) definitions[index(DeferredKeyKind::Station)].insert(key);
    }
    for (const SignalAspect& row : ctx.signal_aspects) {
        definitions[index(DeferredKeyKind::SignalAspect)].insert(
            ascii_lower(trim_field_copy(row.signal_aspect_key)));
    }
    for (const OtherTrainDefinition& row : ctx.other_trains) {
        definitions[index(DeferredKeyKind::Train)].insert(
            ascii_lower(trim_field_copy(key_text(row.train_key))));
    }

    for (const DeferredKeyReference& reference : ctx.deferred_key_references) {
        const auto& keys = definitions[index(reference.kind)];
        if (keys.find(reference.key) != keys.end()) continue;
        MapDiagnostic diagnostic = reference.source;
        diagnostic.message = "Unknown " + deferred_key_kind_name(reference.kind) +
                             " '" + reference.display_key + "'.";
        ctx.diagnostics.push_back(std::move(diagnostic));
    }
}

void emit_diagnostics(MapContext& ctx) {
    auto source_order = [&](const MapDiagnostic& diagnostic) {
        const auto it =
            ctx.source_file_indices.find(normalized_source_key(diagnostic.file_path));
        return it == ctx.source_file_indices.end() ? k_no_source_ref : it->second;
    };
    std::stable_sort(
        ctx.diagnostics.begin(), ctx.diagnostics.end(),
        [&](const MapDiagnostic& left, const MapDiagnostic& right) {
            const size_t left_source = source_order(left);
            const size_t right_source = source_order(right);
            if (left_source != right_source) return left_source < right_source;
            if (left.line != right.line) return left.line < right.line;
            return left.column < right.column;
        });
    for (const MapDiagnostic& diagnostic : ctx.diagnostics) {
        KME_MAPLOADER_LOG_WARN(format_diagnostic(diagnostic));
    }
}

} // namespace


std::unique_ptr<MapContext> parse_map_context(std::filesystem::path map_path,
                                              double unit_distance,
                                              SourceTextOverrides overrides,
                                              bool has_arbitrary_distribution,
                                              const std::array<double, 3>& arbitrary_distribution,
                                              MapParseOptions options) {
    auto ctx = std::make_unique<MapContext>();
    ctx->source_overrides = std::move(overrides);
    ctx->parse_options = options;
    ActiveTimingScope active(ctx->timing);
    KME_MAPLOADER_LOG_INFO("loading map " + path_to_utf8(map_path));
    LoadedText loaded = load_header_text(*ctx, map_path, "BveTs Map ", 2.0);
    register_source_file(*ctx, loaded);
    ctx->rootpath = loaded.root;
    ctx->rootpath_utf8 = path_to_utf8(loaded.root);
    ctx->entry_file_path = loaded.normalized_path;
    ctx->current_file_path = loaded.normalized_path;
    ctx->include_stack.push_back(ctx->current_file_path);
    ctx->file_structure.push_back({k_no_source_ref, {}, ctx->entry_file_path});

    KME_MAPLOADER_LOG_INFO("parsing syntax tree");
    try {
        ScopedTimer timer(&ctx->timing.parse_seconds);
        Parser parser(*ctx, std::move(loaded));
        parser.parse();
        validate_light_statements(*ctx);
        validate_unique_preview_statements(*ctx);
    } catch (...) {
        emit_diagnostics(*ctx);
        throw;
    }
    append_transition_diagnostics(*ctx);
    append_deferred_key_diagnostics(*ctx);
    emit_diagnostics(*ctx);

    KME_MAPLOADER_LOG_INFO("sorting parsed IR");
    {
        ScopedTimer timer(&ctx->timing.relocate_seconds);
        relocate(*ctx);
    }
    generate_geometry(*ctx, unit_distance, has_arbitrary_distribution,
                      arbitrary_distribution[0], arbitrary_distribution[1],
                      arbitrary_distribution[2]);
    return ctx;
}

} // namespace kme::maploader::detail
