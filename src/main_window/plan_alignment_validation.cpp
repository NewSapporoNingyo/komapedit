/*
 * Copyright (c) 2026 Sapporo_ningyo
 *
 * Licensed under Apache License 2.0; see LICENSE and NOTICE.
 */

#include "kme.h"
#include "maploader.h"
#include "../canvas2d/canvas2d_alignment.h"
#include "../canvas2d/canvas2d_marker_cache.h"
#include "../canvas2d/canvas2d_primitives.h"
#include "imgui_internal.h"
#include "implot.h"

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
            "0; Curve.SetFunction(1); Curve.Begin(0); Gradient.Begin(0); Track['parallel'].Position(3,1);\r\n"
            "$base-50; Curve.BeginTransition(); Gradient.BeginTransition();\r\n"
            "$base+2.5;\r\n  Curve.Begin( $radius , $cant ); // curve comment\r\n"
            "$base+30; Gradient.Begin($grade);\r\n"
            "$base+115; Gradient.Interpolate($grade+2);\r\n"
            "$base+180; Section.Begin(0,1); SpeedLimit.Begin(80);\r\n"
            "$base+240; Gradient.Interpolate();\r\n"
            "$base+400; Curve.BeginTransition();\r\n"
            "$base+450; Curve.End();\r\n"
            "$base+500; Curve.Interpolate(5);\r\n"
            "$base+590; Curve.Interpolate();\r\n"
            "$base+655; Curve.Interpolate(0);\r\n"
            "$base+700; Gradient.BeginTransition();\r\n"
            "$base+765; Curve.Change(2); Track['parallel'].Position(3,1);\r\n"
            "4245; Curve.Change(0);\r\n"
            "4300; Curve.BeginTransition();\r\n"
            "4340; Curve.Begin(300);\r\n"
            "4400; Gradient.BeginTransition();\r\n"
            "4410; gRaDiEnT.BeGiN( // keep grade comment\r\n"
            "    $grade );\r\n"
            "4500; Curve.BeginTransition();\r\n"
            "4540; Curve.End();\r\n"
            "4600; Curve.BeginTransition();\r\n"
            "4640; Curve.Change(-400);\r\n"
            "4700; Curve.BeginTransition();\r\n"
            "4740; Curve.End();\r\n"
            "4800; Curve.BeginTransition();\r\n"
            "4840; Curve.BeginCircular(500,$cant);\r\n"
            "4900; Curve.BeginTransition();\r\n"
            "4940; Curve.End();\r\n"
            "5000; Legacy.Curve(450);\r\n"
            "5100; lEgAcY.CuRvE( -450 # keep radius comment, including comma\r\n"
            "    , $cant );\r\n"
            "5200; Legacy.Curve(0,0);\r\n"
            "5300; Curve.Interpolate(500);\r\n";
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
        app.show_section_markers_ = true;
        app.rebuild_marker_overlay_cache();
        auto& editor = *app.plan_alignment_;
        app.set_2d_mode(Mode::Edit);
        editor.set_source(app.model_, app.plan_data_source_revision_, app.repeater_marker_cache_);
        const auto row_id = [&](const auto& rows, const char* method, double distance) {
            for (const auto& row : rows) {
                if (table_cell(row, "method") == method && table_cell_number(row, "distance") == distance) {
                    return row.edit_id;
                }
            }
            throw std::runtime_error(method);
        };
        const std::string curve = row_id(app.model_.curve_rows, "Curve.Begin", 3472.5);
        const std::string equal_radius = row_id(app.model_.curve_rows, "Curve.Change", 4235.0);
        const std::string paired = row_id(app.model_.gradient_rows, "Gradient.BeginTransition", 3420.0);
        const std::string orphan = row_id(app.model_.gradient_rows, "Gradient.BeginTransition", 4170.0);
        const std::string gradient = row_id(app.model_.gradient_rows, "Gradient.Interpolate", 3585.0);
        const std::string inherited = row_id(app.model_.gradient_rows, "Gradient.Interpolate", 3710.0);
        check("event_identity", editor.target(curve) && editor.target(paired) &&
              !editor.target(orphan) && editor.target(gradient) && editor.target(inherited));

        const auto wait_preview = [&] {
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
            while (!editor.current_preview_ready() && std::chrono::steady_clock::now() < deadline) {
                const std::string error = editor.poll();
                if (!error.empty()) throw std::runtime_error(error);
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            return editor.current_preview_ready();
        };
        canvas2d::PlanScreenTransform label_transform;
        label_transform.scale = 0.02;
        label_transform.screen_cx = label_transform.screen_cy = 250.0f;
        const ImVec2 label_origin(0.0f, 0.0f);
        const ImVec2 label_size(500.0f, 500.0f);
        const auto& labels = editor.radius_labels(app.model_.own, label_transform,
                                                 0.0, 7075.0, label_origin, label_size);
        const auto owner_is = [](const std::string& id) {
            return [&id](const auto& label) { return label.span->edit_id == id; };
        };
        const auto curve_label = std::find_if(labels.begin(), labels.end(), owner_is(curve));
        check("shared_radius_labels", curve_label != labels.end() && !curve_label->span->transition &&
              std::count_if(labels.begin(), labels.end(), owner_is(curve)) == 1 &&
              std::count_if(labels.begin(), labels.end(), owner_is(equal_radius)) == 1);
        const auto& only_transition = editor.radius_labels(app.model_.own, label_transform,
                                                           3420.0, 3470.0, label_origin, label_size);
        check("visible_transition_label", only_transition.size() == 1 &&
              only_transition.front().span->transition && only_transition.front().span->edit_id == curve);

        const auto profile_marker = [&](const ProfileData& data) {
            const auto marker = std::find_if(data.gradient_edit_markers.begin(),
                data.gradient_edit_markers.end(), [&](const auto& row) { return row.edit_id == gradient; });
            if (marker == data.gradient_edit_markers.end()) throw std::runtime_error("profile marker missing");
            return *marker;
        };
        ImGuiIO& io = ImGui::GetIO();
        io.DisplaySize = ImVec2(1000, 720);
        io.DeltaTime = 1.0f / 60.0f;
        io.Fonts->Build();
        const auto render_charts = [&] {
            ImGui::NewFrame();
            ImGui::SetNextWindowPos(ImVec2(0, 0));
            ImGui::SetNextWindowSize(io.DisplaySize);
            ImGui::Begin("Alignment profile contract", nullptr,
                         ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings);
            const auto& profile = app.current_profile_data();
            app.render_profile_plot(profile, ImVec2(950, 330));
            app.render_radius_plot(profile, ImVec2(950, 330));
            ImGui::End();
            ImGui::Render();
        };
        const auto marker_pixel = [&] {
            const auto marker = profile_marker(app.current_profile_data());
            const auto& axes = app.profile_plot_limits_;
            return ImVec2(
                app.profile_plot_pos_.x + app.profile_plot_size_.x *
                    static_cast<float>((marker.x - axes[0]) / (axes[1] - axes[0])),
                app.profile_plot_pos_.y + app.profile_plot_size_.y *
                    static_cast<float>((axes[3] - marker.y) / (axes[3] - axes[2])));
        };
        app.set_2d_mode(Mode::Measure);
        app.profile_x_span_ = 200.0;
        app.request_plot_focus(3585.0, true, false);
        render_charts();
        const auto applied_elevations = app.current_profile_data().own_y;
        const ImVec2 hit = marker_pixel();
        io.AddMousePosEvent(hit.x, hit.y);
        render_charts();
        render_charts();
        const bool profile_measured = app.measure_distance_.has_value() && !app.measure_text_.empty();
        app.clear_measure();
        io.AddMousePosEvent(500.0f, 510.0f);
        render_charts();
        render_charts();
        const bool radius_measured = app.measure_distance_.has_value() && !app.measure_text_.empty();
        app.set_2d_mode(Mode::Edit);
        render_charts();
        check("measure_exits_for_charts", profile_measured && radius_measured &&
              !app.measure_distance_ && app.measure_text_.empty() &&
              app.current_view_2d_settings().mode == 1);
        io.AddMousePosEvent(hit.x, hit.y);
        render_charts();
        const auto axes_before_drag = app.profile_plot_limits_;
        const float pixels_per_metre = app.profile_plot_size_.x /
            static_cast<float>(axes_before_drag[1] - axes_before_drag[0]);
        io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
        render_charts();
        const bool grabbed = editor.dragging(canvas2d::AlignmentDragSurface::Profile);
        io.AddMousePosEvent(hit.x + 7.0f * pixels_per_metre, hit.y);
        render_charts();
        const bool moved_during_drag = grabbed && editor.dirty() && wait_preview() &&
            profile_marker(app.current_profile_data()).d == 3590.0 &&
            app.current_profile_data().own_y != applied_elevations &&
            app.profile_plot_limits_ == axes_before_drag && editor.marker_revision() == 0;
        io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
        render_charts();
        check("profile_drag_preview_and_release", moved_during_drag && !editor.dragging() &&
              wait_preview() && editor.marker_revision() == editor.preview_revision());

        const ImVec2 next_hit = marker_pixel();
        io.AddMousePosEvent(next_hit.x, next_hit.y);
        render_charts();
        io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
        render_charts();
        io.AddMousePosEvent(next_hit.x + 12.0f * pixels_per_metre, next_hit.y);
        render_charts();
        const bool moved_existing_draft = editor.dragging() && editor.dirty() &&
            editor.drafts().at(gradient).distance == 3600.0;
        io.AddKeyEvent(ImGuiKey_Escape, true);
        render_charts();
        check("profile_escape_restores_draft", moved_existing_draft && !editor.dragging() &&
              editor.drafts().at(gradient).distance == 3590.0);
        io.AddKeyEvent(ImGuiKey_Escape, false);
        io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
        render_charts();
        editor.clear_drafts();
        app.apply_edit_mode_enabled(false);
        check("leave_edit_restores_measure", app.mode_ == Mode::Measure && !editor.dirty());
        app.edit_mode_enabled_ = true;
        app.set_2d_mode(Mode::Edit);
        const PlanMarker applied_section = app.current_plan_data().section_markers.at(0);

        canvas2d::PlanScreenTransform plan_transform;
        const auto plan_frame = [&] {
            ImGui::NewFrame();
            const auto& plan = app.current_plan_data();
            const auto* overlay = editor.overlay(plan, app.plan_data_cache_.generation, plan_transform);
            const bool consumed = editor.process_mouse(plan, overlay, editor.geometry(app.model_.own),
                plan_transform, io.MousePos, app.dmin_, app.dmax_, 1.0f, true, true,
                app.plan_marker_selection_);
            ImGui::EndFrame();
            return consumed;
        };
        const auto focus = [&](double distance) {
            if (editor.dirty() && !wait_preview()) throw std::runtime_error("plan preview is not ready");
            View2D view;
            view.scale = 2.0;
            plan_transform = canvas2d::make_plan_transform(view, -app.current_plan_data().origin_angle,
                                                           ImVec2(0, 0), io.DisplaySize);
            const auto point = canvas2d::sample_matrix_track_point(
                editor.geometry(app.model_.own), distance, true).value();
            std::tie(plan_transform.cx, plan_transform.cy) = plan_transform.model_to_plan(point.x, point.y);
        };
        const auto move_pointer = [&](double distance) {
            const auto point = canvas2d::sample_matrix_track_point(
                editor.geometry(app.model_.own), distance, true).value();
            const auto screen = plan_transform.model_to_screen(point.x, point.y);
            io.AddMousePosEvent(screen.x, screen.y);
            plan_frame();
        };
        const auto start_drag = [&](double distance) {
            focus(distance);
            app.plan_marker_selection_.clear();
            move_pointer(distance);
            io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
            return plan_frame() && editor.dragging(canvas2d::AlignmentDragSurface::Plan);
        };
        const auto release = [&] {
            io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
            plan_frame();
        };
        const auto drag = [&](double from, double to) {
            const bool started = start_drag(from);
            move_pointer(to);
            release();
            return started && !editor.dragging();
        };
        const auto wheel = [&](float delta) {
            io.AddMouseWheelEvent(0.0f, delta);
            return plan_frame();
        };
        const auto distance_staged = [&](const std::string& id, double value) {
            const auto draft = editor.drafts().find(id);
            return draft != editor.drafts().end() && draft->second.distance == value;
        };
        const auto curve_id = [&](const char* method, double distance) {
            return row_id(app.model_.curve_rows, method, distance);
        };
        const std::string short_start = curve_id("Curve.Begin", 4340.0);
        const std::string short_transition = curve_id("Curve.BeginTransition", 4300.0);
        check("plan_only_pair", editor.target(short_start)->transition_edit_id == short_transition &&
              table_cell(app.model_.curve_rows.at(editor.target(short_transition)->row_index),
                         "_transitionStatus") == "orphan");
        const auto requests_before = editor.metrics().submitted;
        const bool pair_drag = start_drag(4340.0);
        move_pointer(4350.0);
        check("plan_pair_one_preview", pair_drag && editor.metrics().submitted == requests_before + 1 &&
              distance_staged(short_start, 4350.0) && distance_staged(short_transition, 4310.0));
        release();
        const bool second_pair_drag = start_drag(4350.0);
        move_pointer(4265.0);
        const bool limited = second_pair_drag && distance_staged(short_start, 4285.0) &&
            distance_staged(short_transition, 4245.0);
        io.AddKeyEvent(ImGuiKey_Escape, true);
        plan_frame();
        check("plan_pair_limit_and_escape", limited && !editor.dragging() &&
              distance_staged(short_start, 4350.0) && distance_staged(short_transition, 4310.0));
        io.AddKeyEvent(ImGuiKey_Escape, false);
        release();
        check("plan_transition_drag_single", drag(4310.0, 4315.0) &&
              distance_staged(short_transition, 4315.0) && distance_staged(short_start, 4350.0));
        check("plan_end_pair", drag(4540.0, 4530.0) &&
              distance_staged(curve_id("Curve.End", 4540.0), 4530.0) &&
              distance_staged(curve_id("Curve.BeginTransition", 4500.0), 4490.0));
        check("plan_change_pair", drag(4640.0, 4650.0) &&
              distance_staged(curve_id("Curve.Change", 4640.0), 4650.0) &&
              distance_staged(curve_id("Curve.BeginTransition", 4600.0), 4610.0));
        check("plan_begin_circular_pair", drag(4840.0, 4850.0) &&
              distance_staged(curve_id("Curve.BeginCircular", 4840.0), 4850.0) &&
              distance_staged(curve_id("Curve.BeginTransition", 4800.0), 4810.0));
        const auto staged_before_interpolate = editor.drafts().size();
        check("plan_interpolate_drag_single", drag(5300.0, 5310.0) &&
              editor.drafts().size() == staged_before_interpolate + 1 &&
              distance_staged(curve_id("Curve.Interpolate", 5300.0), 5310.0));

        const std::string legacy = curve_id("Legacy.Curve", 5000.0);
        const std::string legacy_cant = curve_id("Legacy.Curve", 5100.0);
        const bool legacy_drag = drag(5000.0, 5005.0) && drag(5100.0, 5110.0);
        check("plan_legacy_drag", legacy_drag && distance_staged(legacy, 5005.0) &&
              distance_staged(legacy_cant, 5110.0) && drag(5200.0, 5205.0) &&
              distance_staged(curve_id("Legacy.Curve", 5200.0), 5205.0));
        focus(5050.0);
        move_pointer(5050.0);
        const bool legacy_wheel = wheel(1.0f) && editor.drafts().at(legacy).radius == 455.0;
        focus(5150.0);
        move_pointer(5150.0);
        check("plan_legacy_wheel", legacy_wheel && wheel(1.0f) &&
              editor.drafts().at(legacy_cant).radius == -445.0);

        const std::string grade = row_id(app.model_.gradient_rows, "Gradient.Begin", 4410.0);
        focus(4410.0);
        move_pointer(4410.0);
        const bool half_tick = wheel(0.5f) && editor.drafts().count(grade) == 0;
        check("plan_gradient_wheel_priority", half_tick && wheel(0.5f) &&
              editor.drafts().at(grade).gradient->value() == 6.1 &&
              !editor.drafts().at(short_start).radius);
        check("plan_gradient_opposite_steps", wheel(-1.0f) && editor.drafts().count(grade) == 0 &&
              wheel(-1.0f) && editor.drafts().at(grade).gradient->value() == 5.9 &&
              wheel(2.0f) && editor.drafts().at(grade).gradient->value() == 6.1);
        io.AddKeyEvent(ImGuiMod_Shift, true);
        plan_frame();
        check("plan_shift_wheel_reserved", !wheel(1.0f) &&
              editor.drafts().at(grade).gradient->value() == 6.1);
        io.AddKeyEvent(ImGuiMod_Shift, false);
        plan_frame();
        if (!wait_preview()) throw std::runtime_error("gradient preview is not ready");
        const auto& grade_plan = app.current_plan_data();
        const auto* grade_overlay = editor.overlay(grade_plan, app.plan_data_cache_.generation, plan_transform);
        const auto& grade_profile = app.current_profile_data();
        const auto shows_grade = [&](const auto& marker) { return marker.edit_id == grade && marker.label == "6.1"; };
        check("plan_gradient_preview_labels",
              std::any_of(grade_overlay->gradient_markers.begin(), grade_overlay->gradient_markers.end(), shows_grade) &&
              std::any_of(grade_profile.gradient_edit_markers.begin(), grade_profile.gradient_edit_markers.end(), shows_grade) &&
              std::any_of(grade_profile.gradient_labels.begin(), grade_profile.gradient_labels.end(),
                          [](const auto& label) { return label.text == "6.1"; }));

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
        app.set_2d_mode(Mode::Pan);
        check("mode_switch_retains_drafts", app.has_unsaved_edit_state() && editor.dirty());
        check("draft_blocks_save", !app.save_pending_document_changes() &&
              std::string_view(app.program_status_key_) == "status.edit.apply_plan_before_save");

        check("latest_preview", wait_preview());
        check("coalesces_burst", editor.metrics().superseded > 0 &&
              editor.metrics().computed < editor.metrics().submitted);
        if (unit_distance <= 25.0) check("reuses_prefix", editor.metrics().reused_rows > 0);
        app.set_2d_mode(Mode::Edit);
        const PlanData& plan = app.current_plan_data();
        const auto& section = plan.section_markers.at(0);
        const auto own_position = canvas2d::sample_matrix_track_point(
            editor.geometry(app.model_.own), section.d, true).value();
        const auto transform = canvas2d::make_plan_transform(app.plan_view_, -plan.origin_angle,
                                                             label_origin, label_size);
        const auto [x, y] = transform.model_to_plan(own_position.x, own_position.y);
        const auto* projection = app.alignment_reprojection();
        const auto other = canvas2d::sample_matrix_track_point(projection->other_tracks.at(0), section.d, false).value();
        check("settled_marker_geometry", section.edit_id == applied_section.edit_id &&
              section.row_index == applied_section.row_index &&
              std::hypot(section.x - applied_section.x, section.y - applied_section.y) > 1e-3 &&
              std::hypot(section.x - x, section.y - y) < 1e-3 &&
              std::abs(std::hypot(other.x - own_position.x, other.y - own_position.y) - 3.0) < 1e-6 &&
              std::abs(other.z - own_position.z - 1.0) < 1e-6);
        const auto cache_generation = app.plan_data_cache_.generation;
        app.current_plan_data();
        check("settled_marker_cache_hit", app.plan_data_cache_.generation == cache_generation);
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

        editor.set_source(app.model_, app.plan_data_source_revision_, app.repeater_marker_cache_);
        editor.step_radius(curve, -1);
        check("repeated_apply", app.apply_plan_alignment_drafts() &&
              app.pending_edit_changes_.at(curve).field_changes.at("radius") == "-10" &&
              app.pending_edit_changes_.at(curve).field_changes.at("distance") == "3477.5");
        check("save", app.save_pending_document_changes());
        const auto saved_bytes = bytes(include);
        check("source_fidelity", bytes(map) == root_text &&
              saved_bytes.find("Curve.Begin( -10 , $cant );") != std::string::npos &&
              saved_bytes.find("// curve comment\r\n") != std::string::npos &&
              saved_bytes.find("Legacy.Curve(455);") != std::string::npos &&
              saved_bytes.find("lEgAcY.CuRvE( -445 # keep radius comment, including comma\r\n"
                               "    , $cant );") != std::string::npos &&
              saved_bytes.find("gRaDiEnT.BeGiN( // keep grade comment\r\n"
                               "    6.0999999999999996 );") != std::string::npos &&
              saved_bytes.find("Gradient.Interpolate($grade+2);\r\n") != std::string::npos &&
              saved_bytes.find("Gradient.Interpolate();\r\n") != std::string::npos);
        auto reopen = load_map_worker(path, unit_distance, false, 0.0, 0.0, unit_distance, options);
        check("reopen_matches", reopen.ok && matching_geometry(reopen.model.own, app.model_.own));
        if (reopen.handle) kv_free(reopen.handle);

        editor.set_source(app.model_, app.plan_data_source_revision_, app.repeater_marker_cache_);
        const Matrix saved_geometry = app.model_.own;
        editor.set_distance(curve, 3970.0);
        check("failed_apply_retains_draft", !app.apply_plan_alignment_drafts() && editor.dirty() &&
              matching_geometry(app.model_.own, saved_geometry) && bytes(include) == saved_bytes);
        app.cancel_distance_resolution_workflow();
        check("revert_discards_draft", app.revert_all_pending_edits() &&
              !editor.dirty() && bytes(include) == saved_bytes);

        app.set_2d_mode(Mode::Measure);
        app.set_2d_mode(Mode::Edit);
        app.set_2d_mode(Mode::NewAtMileage);
        const auto& own = app.current_plan_data().own;
        const TrackPoint track_point = own.at(own.size() / 2);
        app.plan_view_.cx = track_point.x;
        app.plan_view_.cy = track_point.y;
        app.plan_view_.scale = 2.0;
        app.plan_view_.fitted = true;
        app.keep_plan_view_ = true;
        const auto render_plan = [&] {
            ImGui::NewFrame();
            ImGui::SetNextWindowPos(ImVec2(0, 0));
            ImGui::SetNextWindowSize(io.DisplaySize);
            ImGui::Begin("Plan mileage contract", nullptr,
                         ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings);
            app.render_plan_canvas(ImVec2(800, 600));
            const auto* child = ImGui::GetCurrentWindowRead()->DC.ChildWindows.back();
            const ImRect bounds(child->DC.CursorStartPos, child->ContentRegionRect.Max);
            ImGui::End();
            ImGui::Render();
            return bounds;
        };
        const ImRect plan_bounds = render_plan();
        const ImVec2 plan_hit = plan_bounds.GetCenter();
        const auto press_plan_at = [&](ImVec2 position) {
            io.AddMousePosEvent(position.x, position.y);
            render_plan();
            io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
            render_plan();
        };
        const auto release_plan = [&] {
            io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
            render_plan();
        };

        press_plan_at(plan_hit);
        const bool opened_on_press = app.new_element_wizard_.open;
        release_plan();
        check("plan_mileage_click_prefill", !opened_on_press && app.new_element_wizard_.open &&
              app.new_element_wizard_.distance_prefill == track_point.d);
        app.new_element_wizard_.open = false;

        press_plan_at(ImVec2(plan_bounds.Min.x + 40.0f, plan_bounds.Min.y + 40.0f));
        release_plan();
        check("plan_mileage_blank_click", !app.new_element_wizard_.open);

        press_plan_at(plan_hit);
        const double center_before_drag = app.plan_view_.cx;
        io.AddMousePosEvent(plan_hit.x + 40.0f, plan_hit.y);
        render_plan();
        const bool panned_on_first_drag_frame = app.plan_view_.cx != center_before_drag;
        io.AddMousePosEvent(plan_hit.x, plan_hit.y);
        render_plan();
        release_plan();
        check("plan_mileage_drag_release", panned_on_first_drag_frame && !app.new_element_wizard_.open);

        press_plan_at(plan_hit);
        io.AddMousePosEvent(plan_hit.x + io.MouseDragThreshold + 1.0f, plan_hit.y);
        release_plan();
        check("plan_mileage_release_motion", !app.new_element_wizard_.open);
        const bool retains_navigation_setting = app.current_view_2d_settings().mode == 1;
        app.apply_edit_mode_enabled(false);
        check("plan_mileage_restores_navigation", retains_navigation_setting &&
              app.mode_ == Mode::Measure);
    } catch (const std::exception& error) {
        out << "alignment_error=" << error.what() << '\n';
        return false;
    }
    out << "alignment_contract=" << (passed ? "PASS" : "FAIL") << '\n';
    return passed;
}
