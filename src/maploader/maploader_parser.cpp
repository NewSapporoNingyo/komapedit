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

using kme::maploader::log_info;
using kme::maploader::log_load_failure;
using kme::maploader::path_to_utf8;

std::string resolve_loaded_asset_path(const std::filesystem::path& root,
                                      const std::string& path_text) {
    std::filesystem::path path = join_path(root, path_text);
    std::error_code error;
    const std::filesystem::path absolute = std::filesystem::absolute(path, error);
    if (!error) path = absolute;
    return path_to_utf8(path.lexically_normal());
}

struct MapObject {
    std::string label;
    Value key;
    bool has_key = false;
};

struct MapFunction {
    std::string label;
    std::vector<Value> args;
    std::string raw_arguments;
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
            ctx_.current_include_invocation_key = kRootIncludeInvocationKey;
            ctx_.current_include_invocation_index = kNoSourceRef;
        }
    }

    void parse() {
        while (true) {
            skip();
            if (eof()) break;
            parse_statement();
        }
        flush_pending_includes();
    }

private:
    struct IncludeResult {
        MapContext context;
        std::string error;
    };

    struct PendingInclude {
        std::filesystem::path path;
        std::string include_path;
        std::string include_invocation_key;
        double seed_distance = 0.0;
        std::unordered_map<std::string, Value> seed_variables;
        std::future<IncludeResult> future;
    };

    MapContext& ctx_;
    LoadedText loaded_;
    std::string src_;
    std::filesystem::path file_path_;
    size_t pos_ = 0;
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
                while (!eof() && src_[pos_] != '\n') ++pos_;
            } else if (starts_with(pos_, "//")) {
                pos_ += 2;
                while (!eof() && src_[pos_] != '\n') ++pos_;
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
                while (p < src_.size() && src_[p] != '\n') ++p;
            } else if (src_.compare(p, 2, "//") == 0) {
                p += 2;
                while (p < src_.size() && src_[p] != '\n') ++p;
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
            std::string name = ascii_lower(parse_variable_name());
            expect('=');
            size_t args_start = pos_;
            Value value = parse_expression();
            size_t args_end = pos_;
            expect(';');
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
                expect(';');
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
                expect(';');
                size_t statement_index = kNoSourceRef;
                if (ctx_.parse_options.collect_edit_metadata) {
                    statement_index = add_parsed_statement(
                        ctx_, map_statement_kind(element.objects, element.function),
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
        expect(';');
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
        seed.file_structure.push_back({kNoSourceRef, include_path, child_path});
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
        try {
            ActiveTimingScope active(result.context.timing);
            LoadedText loaded = load_header_text(result.context, child, "BveTs Map ", 2.0);
            register_source_file(result.context, loaded);
            result.context.current_file_path = loaded.normalized_path;
            if (!result.context.file_structure.empty()) {
                result.context.file_structure.front().absolute_path = loaded.normalized_path;
            }
            Parser nested(result.context, std::move(loaded));
            nested.parse();
        } catch (const std::exception& e) {
            result.error = e.what();
        }
        return result;
    }

    void queue_include(const std::string& path_text, size_t body_start, size_t body_end) {
        std::filesystem::path child = join_path(ctx_.rootpath, path_text);
        log_info("including " + path_to_utf8(child));
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
        pending.future = std::async(std::launch::async,
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
            ctx_.file_structure.push_back({kNoSourceRef, {}, ctx_.current_file_path});
        }

        const size_t child_root_index = ctx_.file_structure.size();
        for (size_t i = 0; i < child.file_structure.size(); ++i) {
            FileStructureRecord& record = child.file_structure[i];
            if (i == 0) {
                record.parent_index = 0;
            } else if (record.parent_index == kNoSourceRef || record.parent_index >= i) {
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
        for (auto& kv : child.station_list) offset_edit_ref(kv.second.edit_ref, statement_index_base);
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
        offset_row_edit_refs(child.speedlimits, statement_index_base);

        int order_base = ctx_.parse_order;
        auto offset_order = [order_base](int& order) {
            if (order > 0) order += order_base;
        };
        for (auto& row : child.station_puts) offset_order(row.order);
        for (auto& entry : child.station_list) offset_order(entry.second.order);
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
        for (auto& row : child.speedlimits) offset_order(row.order);
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
        for (auto& kv : child.station_position) ctx_.station_position[kv.first] = std::move(kv.second);
        for (auto& kv : child.station_key) ctx_.station_key[kv.first] = std::move(kv.second);
        for (auto& kv : child.station_list) ctx_.station_list[kv.first] = std::move(kv.second);
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
        for (auto& row : child.speedlimits) ctx_.speedlimits.push_back(std::move(row));
    }

    void flush_pending_includes() {
        if (pending_includes_.empty()) return;

        for (auto& pending : pending_includes_) {
            IncludeResult result;
            try {
                result = pending.future.get();
            } catch (const std::exception& e) {
                result.error = e.what();
            }
            if (!result.error.empty()) {
                result = parse_include_context(
                    make_child_seed(pending.path, pending.include_path,
                                    pending.include_invocation_key), pending.path);
                if (!result.error.empty()) {
                    log_load_failure(result.error);
                    continue;
                }
            } else if (include_result_is_stale(pending, result.context)) {
                result = parse_include_context(
                    make_child_seed(pending.path, pending.include_path,
                                    pending.include_invocation_key), pending.path);
                if (!result.error.empty()) {
                    log_load_failure(result.error);
                    continue;
                }
            }
            merge_include_context(result.context);
        }
        pending_includes_.clear();
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
                return Value::str(lhs.text + std::to_string(static_cast<long long>(as_number(rhs))));
            }
            return Value::str(std::to_string(static_cast<long long>(as_number(lhs))) + rhs.text);
        }
        if (op == '-') return Value::num(as_number(lhs) - as_number(rhs));
        if (op == '*') return Value::num(as_number(lhs) * as_number(rhs));
        if (op == '/') {
            double a = as_number(lhs);
            double b = as_number(rhs);
            return Value::num(b != 0.0 ? a / b : std::copysign(kInf, a));
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

    void dispatch(const std::vector<MapObject>& objects, const MapFunction& function) {
        std::string first = ascii_lower(objects.front().label);
        std::vector<std::string> labels;
        labels.reserve(objects.size());
        for (const auto& object : objects) labels.push_back(ascii_lower(object.label));
        std::string fn = ascii_lower(function.label);

        if (first == "curve") {
            dispatch_curve(fn, function.args);
        } else if (first == "gradient") {
            dispatch_gradient(fn, function.args);
        } else if (first == "legacy") {
            dispatch_legacy(fn, function.args);
        } else if (first == "station") {
            std::vector<Value> args = function.args;
            if (objects.front().has_key) args.insert(args.begin(), objects.front().key);
            dispatch_station(fn, args);
        } else if (first == "track") {
            if (!objects.front().has_key) throw std::runtime_error("Track key is required");
            dispatch_track(objects.front().key, labels, fn, function.args);
        } else if (first == "speedlimit") {
            dispatch_speedlimit(fn, function.args);
        } else if (first == "section") {
            dispatch_section(fn, function.args);
        } else if (first == "signal") {
            std::vector<Value> args = function.args;
            if (objects.front().has_key) args.insert(args.begin(), objects.front().key);
            dispatch_signal(fn, args, objects.front().has_key);
        } else if (first == "structure") {
            std::vector<Value> args = function.args;
            if (objects.front().has_key) args.insert(args.begin(), objects.front().key);
            dispatch_structure(fn, args);
        } else if (first == "beacon") {
            dispatch_beacon(fn, function.args);
        } else if (first == "pretrain") {
            dispatch_pretrain(fn, function.args);
        } else if (first == "sound" || first == "sound3d") {
            dispatch_sound(fn, function.args, first == "sound3d",
                           objects.front().has_key, objects.front().key);
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
        }
    }

    void dispatch_curve(const std::string& fn, const std::vector<Value>& a) {
        if (fn == "setgauge" || fn == "gauge") put_own(ctx_, "gauge", arg_or_null(a));
        else if (fn == "setcenter") put_own(ctx_, "center", arg_or_null(a));
        else if (fn == "setfunction") put_own(ctx_, "interpolate_func", Value::str(as_number(arg_or_null(a)) == 0.0 ? "sin" : "line"));
        else if (fn == "begintransition") {
            put_own(ctx_, "radius", Value::null(), "bt");
            put_own(ctx_, "cant", Value::null(), "bt");
        } else if (fn == "begincircular" || fn == "begin" || fn == "change") {
            put_own(ctx_, "radius", arg_or_null(a));
            put_own(ctx_, "cant", a.size() > 1 ? a.at(1) : Value::num(0.0));
        } else if (fn == "end") {
            put_own(ctx_, "radius", Value::num(0.0));
            put_own(ctx_, "cant", Value::num(0.0));
        } else if (fn == "interpolate") {
            put_own(ctx_, "radius", arg_or_null(a), "i");
            put_own(ctx_, "cant", a.size() > 1 ? a.at(1) : Value::null(), "i");
        }
    }

    void dispatch_gradient(const std::string& fn, const std::vector<Value>& a) {
        if (fn == "begintransition") put_own(ctx_, "gradient", Value::null(), "bt");
        else if (fn == "begin" || fn == "beginconst") put_own(ctx_, "gradient", arg_or_null(a));
        else if (fn == "end") put_own(ctx_, "gradient", Value::num(0.0));
        else if (fn == "interpolate") put_own(ctx_, "gradient", arg_or_null(a), "i");
    }

    void dispatch_legacy(const std::string& fn, const std::vector<Value>& a) {
        if (fn == "turn") put_own(ctx_, "turn", arg_or_null(a));
        else if (fn == "curve") {
            put_own(ctx_, "radius", arg_or_null(a));
            put_own(ctx_, "cant", a.size() > 1 ? a.at(1) : Value::num(0.0));
        } else if (fn == "pitch") {
            put_own(ctx_, "gradient", arg_or_null(a));
        }
    }

    void dispatch_station(const std::string& fn, const std::vector<Value>& a) {
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
            std::filesystem::path path = join_path(ctx_.rootpath, as_text(a.at(0)));
            LoadedText loaded = load_header_text(ctx_, path, "BveTs Station List ", 0.04);
            parse_station_list(loaded);
        }
    }

    void parse_station_list(const LoadedText& loaded) {
        register_source_file(ctx_, loaded);
        std::vector<std::string> stack = include_stack_for_file(ctx_, loaded.path);
        for_each_loaded_body_line(loaded, [&](const std::string& line, size_t line_start,
                                              size_t line_end, int) {
            std::vector<std::string> fields = parse_comma_separated_fields(line, true);
            if (fields.empty() || fields[0].empty()) return;

            StationListEntry row;
            for (size_t i = 0; i < row.fields.size() && i < fields.size(); ++i) row.fields[i] = fields[i];
            row.order = ctx_.next_parse_order();
            row.edit_ref = add_loaded_line_statement(ctx_, loaded, stack, "StationList.Row",
                                                     line_start, line_end, line, fields);
            std::string key = ascii_lower(row.fields[0]);
            ctx_.station_key[key] = row.fields[1];
            ctx_.station_list[key] = std::move(row);
        });
    }

    void parse_structure_list(const LoadedText& loaded) {
        register_source_file(ctx_, loaded);
        std::vector<std::string> stack = include_stack_for_file(ctx_, loaded.path);
        for_each_loaded_body_line(loaded, [&](const std::string& line, size_t line_start,
                                              size_t line_end, int) {
            std::string trimmed = trim_field_copy(line);
            if (trimmed.empty() || trimmed[0] == '#') return;

            std::vector<std::string> fields = parse_comma_separated_fields(line, true);
            if (fields.empty() || fields[0].empty()) return;

            StructureModel row;
            row.structure_key = fields[0];
            if (fields.size() > 1 && !fields[1].empty()) {
                row.file_path = resolve_loaded_asset_path(loaded.root, fields[1]);
            }
            row.edit_ref = add_loaded_line_statement(ctx_, loaded, stack, "StructureList.Row",
                                                     line_start, line_end, line, fields);
            ctx_.structure_models.push_back(std::move(row));
        });
    }

    void parse_signal_aspect_list(const LoadedText& loaded) {
        register_source_file(ctx_, loaded);
        std::vector<std::string> stack = include_stack_for_file(ctx_, loaded.path);
        SignalAspect* current_aspect = nullptr;
        for_each_loaded_body_line(loaded, [&](const std::string& line, size_t line_start,
                                              size_t line_end, int) {
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
                                                         line_start, line_end, line, fields);
                ctx_.signal_aspects.push_back(std::move(row));
                current_aspect = &ctx_.signal_aspects.back();
            } else if (current_aspect && !current_aspect->edit_ref.valid()) {
                current_aspect->edit_ref = add_loaded_line_statement(ctx_, loaded, stack, "SignalList.Row",
                                                                     line_start, line_end, line, fields);
            }
            for (size_t i = 1; i < fields.size(); ++i) {
                current_aspect->structure_keys.push_back(fields[i]);
            }
        });
    }

    void parse_sound_list(const LoadedText& loaded, bool is_3d) {
        register_source_file(ctx_, loaded);
        std::vector<std::string> stack = include_stack_for_file(ctx_, loaded.path);
        for_each_loaded_body_line(loaded, [&](const std::string& line, size_t line_start,
                                              size_t line_end, int) {
            std::string trimmed = trim_field_copy(line);
            if (trimmed.empty() || trimmed[0] == '#') return;

            std::vector<std::string> fields = parse_comma_separated_fields(line, true);
            if (fields.empty() || fields[0].empty()) return;

            SoundListEntry row;
            row.sound_key = fields[0];
            row.is_3d = is_3d;
            if (fields.size() > 1 && !fields[1].empty()) {
                row.file_path = resolve_loaded_asset_path(loaded.root, fields[1]);
            }
            if (fields.size() > 2) {
                row.buffer_count = parse_sound_buffer_count(fields[2]);
            }
            row.edit_ref = add_loaded_line_statement(ctx_, loaded, stack,
                                                     is_3d ? "Sound3DList.Row" : "SoundList.Row",
                                                     line_start, line_end, line, fields);
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
                                              size_t line_end, int) {
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
                                                          line_start, line_end, line, fields);
            if (section == "structure") {
                ctx_.other_train_structure_keys.push_back({value, path_key, ref});
            } else if (section == "sound3d") {
                ctx_.other_train_sound_3d_keys.push_back({value, path_key, ref});
            }
        });
    }

    void dispatch_track(const Value& track_key, const std::vector<std::string>& labels,
                        const std::string& fn, const std::vector<Value>& a) {
        if (labels.size() >= 2 && labels[1] == "cant" && fn == "cant") {
            return;
        }
        if (labels.size() == 1 && fn == "position") {
            track_position(track_key, a);
            return;
        }
        if (labels.size() == 1 && fn == "cant") {
            put_other(ctx_, track_key, "cant", arg_or_null(a), "i");
            return;
        }
        if (labels.size() == 1 && fn == "gauge") {
            put_other(ctx_, track_key, "gauge", arg_or_null(a));
            return;
        }
        if (labels.size() >= 2 && (labels[1] == "x" || labels[1] == "y") && fn == "interpolate") {
            setposition_interpolate(track_key, labels[1], a);
            return;
        }
        if (labels.size() >= 2 && labels[1] == "cant") {
            if (fn == "setgauge") put_other(ctx_, track_key, "gauge", arg_or_null(a));
            else if (fn == "setcenter") put_other(ctx_, track_key, "center", arg_or_null(a));
            else if (fn == "setfunction") put_other(ctx_, track_key, "interpolate_func", Value::str(as_number(arg_or_null(a)) == 0.0 ? "sin" : "line"));
            else if (fn == "begintransition") put_other(ctx_, track_key, "cant", Value::null(), "bt");
            else if (fn == "begin") put_other(ctx_, track_key, "cant", arg_or_null(a), "i");
            else if (fn == "end") put_other(ctx_, track_key, "cant", Value::num(0.0), "i");
            else if (fn == "interpolate" || fn == "cant") put_other(ctx_, track_key, "cant", arg_or_null(a), "i");
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
            row.signal_indices = a;
            row.file_path = ctx_.current_file_path;
            row.order = ctx_.next_parse_order();
            attach_active_edit_ref(ctx_, row);
            ctx_.section_begins.push_back(std::move(row));
        } else if (fn == "setspeedlimit") {
            note_distance_use(ctx_);
            SectionSpeedLimit row;
            row.distance = ctx_.distance;
            row.speeds = a;
            row.file_path = ctx_.current_file_path;
            row.order = ctx_.next_parse_order();
            attach_active_edit_ref(ctx_, row);
            ctx_.section_speed_limits.push_back(std::move(row));
        }
    }

    void dispatch_signal(const std::string& fn, const std::vector<Value>& a, bool has_signal_key) {
        if (fn == "load" && !has_signal_key && !a.empty()) {
            std::string list_path_text = as_text(a.at(0));
            if (list_path_text.empty()) return;
            try {
                std::filesystem::path path = join_path(ctx_.rootpath, list_path_text);
                LoadedText loaded = load_header_text(ctx_, path, "BveTs Signal Aspects List ", 2.0);
                parse_signal_aspect_list(loaded);
            } catch (const std::exception& e) {
                log_load_failure(e.what());
            }
        } else if (fn == "speedlimit" && !has_signal_key) {
            note_distance_use(ctx_);
            SectionSpeedLimit row;
            row.distance = ctx_.distance;
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
        attach_active_edit_ref(ctx_, row);
        ctx_.pretrains.push_back(std::move(row));
    }

    void dispatch_structure(const std::string& fn, const std::vector<Value>& a) {
        if (fn == "load") {
            note_distance_use(ctx_);
            StructureLoad row;
            row.distance = ctx_.distance;
            row.method = "Load";
            row.load_file_path = a.empty() ? Value::str("") : a[0];
            row.file_path = ctx_.current_file_path;
            row.order = ctx_.next_parse_order();
            attach_active_edit_ref(ctx_, row);
            ctx_.structure_loads.push_back(row);
            std::string list_path_text = as_text(row.load_file_path);
            if (!list_path_text.empty()) {
                try {
                    std::filesystem::path path = join_path(ctx_.rootpath, list_path_text);
                    LoadedText loaded = load_header_text(ctx_, path, "BveTs Structure List ", 1.0);
                    parse_structure_list(loaded);
                } catch (const std::exception& e) {
                    log_load_failure(e.what());
                }
            }
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
                        bool is_3d, bool has_key, const Value& sound_key) {
        if (fn == "load" && !has_key && !a.empty()) {
            std::string list_path_text = as_text(a.at(0));
            if (list_path_text.empty()) return;
            try {
                std::filesystem::path path = join_path(ctx_.rootpath, list_path_text);
                LoadedText loaded = load_header_text(ctx_, path, "BveTs Sound List ", 2.0);
                parse_sound_list(loaded, is_3d);
            } catch (const std::exception& e) {
                log_load_failure(e.what());
            }
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
        attach_active_edit_ref(ctx_, row);

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
                log_load_failure(e.what());
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
            attach_active_edit_ref(ctx_, row);
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
            attach_active_edit_ref(ctx_, row);
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
        if ((fn != "interpolate" && fn != "set") || a.empty()) return;
        note_distance_use(ctx_);
        CabIlluminanceChange row;
        row.distance = ctx_.distance;
        row.value = a[0];
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
};


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
    log_info("loading map " + path_to_utf8(map_path));
    LoadedText loaded = load_header_text(*ctx, map_path, "BveTs Map ", 2.0);
    register_source_file(*ctx, loaded);
    ctx->rootpath = loaded.root;
    ctx->rootpath_utf8 = path_to_utf8(loaded.root);
    ctx->entry_file_path = loaded.normalized_path;
    ctx->current_file_path = loaded.normalized_path;
    ctx->include_stack.push_back(ctx->current_file_path);
    ctx->file_structure.push_back({kNoSourceRef, {}, ctx->entry_file_path});

    log_info("parsing syntax tree");
    {
        ScopedTimer timer(&ctx->timing.parse_seconds);
        Parser parser(*ctx, std::move(loaded));
        parser.parse();
    }

    log_info("sorting parsed IR");
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
