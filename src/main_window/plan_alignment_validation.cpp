/*
 * Copyright (c) 2026 Sapporo_ningyo
 *
 * Licensed under Apache License 2.0; see LICENSE and NOTICE.
 */

#include "kme.h"
#include "maploader.h"
#include "../canvas2d/canvas2d_alignment.h"

#include <fstream>
#include <iterator>
#include <stdexcept>

bool App::debug_plan_alignment_contract(double unit_distance, std::ostream& out) {
    struct Fixture {
        std::filesystem::path root;
        bool owns_directory = false;
        ~Fixture() {
            if (!owns_directory) return;
            std::error_code error;
            std::filesystem::remove(root / "map.txt", error);
            std::filesystem::remove(root / "alignment.txt", error);
            std::filesystem::remove(root, error);
        }
    } fixture{std::filesystem::temp_directory_path() /
        ("komapedit-plan-alignment-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()))};
    bool passed = true;
    const auto check = [&](const char* name, bool ok) {
        out << "alignment_" << name << '=' << (ok ? "PASS" : "FAIL") << '\n';
        passed = passed && ok;
    };
    try {
        fixture.owns_directory = std::filesystem::create_directory(fixture.root);
        if (!fixture.owns_directory) throw std::runtime_error("cannot create fixture");
        const auto map = fixture.root / "map.txt";
        const auto include = fixture.root / "alignment.txt";
        const std::string root_text = "BveTs Map 2.02:utf-8\n$base=3470; include 'alignment.txt';\n7075;\n";
        const std::string include_text =
            "BveTs Map 2.02:utf-8\r\n"
            "$radius=2; $cant=0.02; $grade=6;\r\n"
            "0; Curve.SetFunction(1); Curve.Begin(0); Gradient.Begin(0);\r\n"
            "$base-50; Curve.BeginTransition(); Gradient.BeginTransition();\r\n"
            "$base+2.5;\r\n  Curve.Begin( $radius , $cant ); // curve comment\r\n"
            "$base+30; Gradient.Begin($grade);\r\n"
            "$base+115; Gradient.Interpolate($grade+2);\r\n"
            "$base+240; Gradient.Interpolate();\r\n"
            "$base+400; Curve.BeginTransition();\r\n"
            "$base+450; Curve.End();\r\n"
            "$base+500; Curve.Interpolate(5);\r\n"
            "$base+590; Curve.Interpolate();\r\n"
            "$base+655; Curve.Interpolate(0);\r\n"
            "$base+700; Gradient.BeginTransition();\r\n";
        std::ofstream(map, std::ios::binary) << root_text;
        std::ofstream(include, std::ios::binary) << include_text;
        const auto bytes = [](const std::filesystem::path& path) {
            std::ifstream input(path, std::ios::binary);
            return std::string(std::istreambuf_iterator<char>(input), {});
        };
        const auto matching_geometry = [](const Matrix& a, const Matrix& b) {
            if (a.cols != b.cols || a.rows != b.rows) return false;
            return std::equal(a.data.begin(), a.data.end(), b.data.begin(), [](double x, double y) {
                return std::abs(x - y) <= 1e-9 * std::max({1.0, std::abs(x), std::abs(y)});
            });
        };

        const std::string path = map.u8string();
        LoadModelOptions options;
        options.full_edit_registry = true;
        options.load_profile = "edit";
        auto load = load_map_worker(path, unit_distance, false, 0.0, 0.0, unit_distance, options);
        if (!load.ok) throw std::runtime_error(load.error);

        App app(nullptr, UserSettings{}, 1.0f, false, false);
        app.handle_ = load.handle;
        app.model_ = std::move(load.model);
        app.file_path_ = path;
        app.has_model_ = true;
        app.edit_mode_enabled_ = true;
        app.edit_registry_loaded_ = true;
        app.dmin_ = app.model_.default_min;
        app.dmax_ = app.model_.default_max;
        app.unit_distance_ = unit_distance;
        app.rebuild_marker_overlay_cache();
        auto& editor = *app.plan_alignment_;
        editor.enabled = true;
        editor.set_source(app.model_, app.plan_data_source_revision_);
        const auto row_id = [&](const auto& rows, const char* method, double distance) {
            for (const auto& row : rows) {
                if (table_cell(row, "method") == method && table_cell_number(row, "distance") == distance) {
                    return row.edit_id;
                }
            }
            throw std::runtime_error(method);
        };
        const std::string curve = row_id(app.model_.curve_rows, "Curve.Begin", 3472.5);
        const std::string paired = row_id(app.model_.gradient_rows, "Gradient.BeginTransition", 3420.0);
        const std::string orphan = row_id(app.model_.gradient_rows, "Gradient.BeginTransition", 4170.0);
        const std::string gradient = row_id(app.model_.gradient_rows, "Gradient.Interpolate", 3585.0);
        const std::string inherited = row_id(app.model_.gradient_rows, "Gradient.Interpolate", 3710.0);
        check("event_identity", editor.target(curve) && editor.target(paired) &&
              !editor.target(orphan) && editor.target(gradient) && editor.target(inherited));

        editor.step_radius(curve, -1);
        check("positive_to_zero", editor.drafts().at(curve).radius == 0.0);
        editor.step_radius(curve, -1);
        editor.step_radius(curve, 2);
        check("negative_to_positive", editor.drafts().at(curve).radius == 5.0);
        editor.step_radius(curve, -2);
        check("multi_tick_to_negative", editor.drafts().at(curve).radius == -5.0);
        for (int input = 0; input < 64; ++input) {
            editor.set_distance(curve, input % 2 == 0 ? 3482.5 : 3477.5);
        }
        editor.set_distance(gradient, 3590.0);
        editor.set_distance(inherited, 3705.0);
        editor.enabled = false;
        check("mode_switch_retains_drafts", app.has_unsaved_edit_state() && editor.dirty());
        check("draft_blocks_save", !app.save_pending_document_changes() &&
              std::string_view(app.program_status_key_) == "status.edit.apply_plan_before_save");

        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (!editor.current_preview_ready() && std::chrono::steady_clock::now() < deadline) {
            const std::string error = editor.poll();
            if (!error.empty()) throw std::runtime_error(error);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        check("latest_preview", editor.current_preview_ready());
        check("coalesces_burst", editor.metrics().superseded > 0 &&
              editor.metrics().computed < editor.metrics().submitted);
        if (unit_distance <= 25.0) check("reuses_prefix", editor.metrics().reused_rows > 0);
        const Matrix draft_geometry = editor.geometry(app.model_.own);
        app.request_edit_ui_operation(PendingEditUiOperation::ApplyPlanAlignment);
        check("busy_disables_draft_edit", !app.plan_alignment_actions_available());
        app.pending_edit_ui_operation_.progress_presented = true;
        app.process_pending_edit_ui_operation();
        const bool applied = !editor.dirty() && app.has_pending_edits();
        check("memory_apply", applied && bytes(include) == include_text && bytes(map) == root_text);
        check("preview_equals_apply", applied && matching_geometry(draft_geometry, app.model_.own));
        if (!applied) {
            for (const auto& log : app.logs_) out << "alignment_log=" << log.text << '\n';
            return false;
        }

        editor.set_source(app.model_, app.plan_data_source_revision_);
        editor.step_radius(curve, -1);
        check("repeated_apply", app.apply_plan_alignment_drafts() &&
              app.pending_edit_changes_.at(curve).field_changes.at("radius") == "-10" &&
              app.pending_edit_changes_.at(curve).field_changes.at("distance") == "3477.5");
        check("save", app.save_pending_document_changes());
        const auto saved_bytes = bytes(include);
        check("source_fidelity", bytes(map) == root_text &&
              saved_bytes.find("Curve.Begin( -10 , $cant );") != std::string::npos &&
              saved_bytes.find("// curve comment\r\n") != std::string::npos &&
              saved_bytes.find("Gradient.Interpolate($grade+2);\r\n") != std::string::npos &&
              saved_bytes.find("Gradient.Interpolate();\r\n") != std::string::npos);
        auto reopen = load_map_worker(path, unit_distance, false, 0.0, 0.0, unit_distance, options);
        check("reopen_matches", reopen.ok && matching_geometry(reopen.model.own, app.model_.own));
        if (reopen.handle) kv_free(reopen.handle);

        editor.set_source(app.model_, app.plan_data_source_revision_);
        const Matrix saved_geometry = app.model_.own;
        editor.set_distance(curve, 3970.0);
        check("failed_apply_retains_draft", !app.apply_plan_alignment_drafts() && editor.dirty() &&
              matching_geometry(app.model_.own, saved_geometry) && bytes(include) == saved_bytes);
        app.cancel_distance_resolution_workflow();
        check("revert_discards_draft", app.revert_all_pending_edits() &&
              !editor.dirty() && bytes(include) == saved_bytes);
    } catch (const std::exception& error) {
        out << "alignment_error=" << error.what() << '\n';
        return false;
    }
    out << "alignment_contract=" << (passed ? "PASS" : "FAIL") << '\n';
    return passed;
}
