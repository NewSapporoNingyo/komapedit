/*
 * Copyright (c) 2026 Sapporo_ningyo
 *
 * Licensed under Apache License 2.0; see LICENSE and NOTICE.
 */

#include "kme.h"
#include "maploader.h"

#include <windows.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <system_error>

#ifndef NDEBUG
#include "debug_headless.h"
#include "imgui.h"
#include "implot.h"

#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <thread>
#endif

namespace {
namespace fs = std::filesystem;

constexpr const char* k_map_name = "Map.txt";
constexpr const char* k_readme_name = "README.md";
constexpr std::array<const char*, 2> k_project_subdirs = {"Sounds", "Structures"};

struct ProjectResource {
    NewFileKind kind;
    const char* path;
    const char* element;
};

constexpr std::array<ProjectResource, 5> k_project_resources = {{
    {NewFileKind::Structure, "Structures\\structures.txt", "Structure"},
    {NewFileKind::Signal, "Signals.txt", "Signal"},
    {NewFileKind::Sound, "Sounds\\sounds.txt", "Sound"},
    {NewFileKind::Sound3D, "Sounds\\sound3D.txt", "Sound3D"},
    {NewFileKind::Station, "Stations.txt", "Station"},
}};

struct ProjectLayout {
    explicit ProjectLayout(std::string_view name)
        : project(fs::u8path(name)), scenario(L"Scenario-" + project.native() + L".txt") {}

    std::array<fs::path, 3> publication_order() const {
        return {project, k_readme_name, scenario};
    }

    fs::path project;
    fs::path scenario;
};

fs::path unique_project_directory(const fs::path& parent, const char* prefix) {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    return parent / (prefix + std::to_string(GetCurrentProcessId()) + "-" + std::to_string(stamp));
}

std::string project_io_error(const char* action, const fs::path& path, DWORD code) {
    return std::string(action) + ": " + path.u8string() + " (Win32 error " +
        std::to_string(code) + ")";
}

bool write_project_stage(const fs::path& stage, const ProjectLayout& layout,
                         const std::string& name, const std::string& scenario_content,
                         std::string& error) {
    const auto make_directory = [&](const fs::path& path) {
        std::error_code ec;
        if (fs::create_directory(path, ec)) return true;
        error = "Failed to create staged project directory: " + path.u8string() +
            (ec ? " (" + ec.message() + ")" : " (target exists)");
        return false;
    };
    const auto write = [&](const fs::path& path, std::string_view content) {
        if (create_utf8_bve_file_exclusive(path, content, error)) return true;
        error += ": " + path.u8string();
        return false;
    };

    const fs::path project = stage / layout.project;
    if (!make_directory(project)) return false;
    for (const char* subdirectory : k_project_subdirs) {
        if (!make_directory(project / subdirectory)) return false;
    }
    std::string map_content(new_bve_file_header(NewFileKind::Map));
    for (const ProjectResource& resource : k_project_resources) {
        if (!write(project / resource.path, new_bve_file_header(resource.kind))) return false;
        map_content += resource.element;
        map_content += ".Load('";
        map_content += resource.path;
        map_content += "');\r\n";
    }
    return write(project / k_map_name, map_content) &&
        write(stage / k_readme_name, "# " + name + "\nBveTs scenario project created with komapedit.\n") &&
        write(stage / layout.scenario, scenario_content);
}

bool validate_project_stage(const fs::path& stage, const ProjectLayout& layout,
                            double unit_distance, std::string& error) {
    const std::string scenario = (stage / layout.scenario).u8string();
    const fs::path map = stage / layout.project / k_map_name;
    std::uint64_t route_count = 0;
    const std::unique_ptr<const KvScenarioRouteCandidate, decltype(&kv_free_scenario_candidates)>
        routes(kv_resolve_scenario_routes(scenario.c_str(), &route_count), kv_free_scenario_candidates);
    if (!routes) {
        error = "Failed to resolve staged Scenario " + scenario + ": " + kv_get_last_error();
        return false;
    }
    if (route_count != 1 || fs::u8path(routes.get()->resolved_path) != map) {
        error = "Staged Scenario Route differs from the project map: " + scenario;
        return false;
    }

    const std::string map_path = map.u8string();
    const std::unique_ptr<void, decltype(&kv_free)> handle(
        kv_load_map_ex(map_path.c_str(), unit_distance, KV_LOAD_PREVIEW), kv_free);
    KvMapSnapshot snapshot{};
    if (!handle || !kv_get_map_snapshot(handle.get(), KV_MAP_SNAPSHOT_VERSION, &snapshot, sizeof(snapshot))) {
        error = "Failed to parse staged map " + map_path + ": " + kv_get_last_error();
        return false;
    }
    if (snapshot.resource_list_load_count != k_project_resources.size() ||
        snapshot.source_file_count != k_project_resources.size() + 1) {
        error = "Staged map must load all five project resource lists: " + map_path;
        return false;
    }
    const auto snapshot_text = [&](KvStringRef ref) {
        return std::string_view(snapshot.string_data + ref.offset, ref.length);
    };
    const KvSourceFileRow* sources_end = snapshot.source_files + snapshot.source_file_count;
    for (size_t i = 0; i < k_project_resources.size(); ++i) {
        const ProjectResource& resource = k_project_resources[i];
        const KvResourceListLoadRow& load = snapshot.resource_list_loads[i];
        const auto expected_kind = resource_list_kind_for_new_file(resource.kind).value();
        const std::string_view resolved = snapshot_text(load.resolved_path);
        const bool loaded = std::any_of(snapshot.source_files, sources_end, [&](const auto& source) {
            return snapshot_text(source.file_path) == resolved;
        });
        if (!loaded || load.kind != static_cast<std::uint32_t>(expected_kind) ||
            snapshot_text(load.evaluated_path) != resource.path) {
            error = "Failed to validate staged resource list: " + (map.parent_path() / resource.path).u8string();
            return false;
        }
    }
    return true;
}

void remove_project_entry(const fs::path& path, std::string& error) {
    std::error_code ec;
    fs::remove(path, ec);
    if (ec) error += "\nFailed to remove " + path.u8string() + ": " + ec.message();
}

enum class ProjectPublication { Published, Conflict, Failed };

ProjectPublication publish_project_stage(const fs::path& stage, const fs::path& parent,
                                         const ProjectLayout& layout, std::string& error) {
    const auto entries = layout.publication_order();
    for (size_t i = 0; i < entries.size(); ++i) {
        const fs::path source = stage / entries[i];
        const fs::path target = parent / entries[i];
        if (MoveFileW(source.c_str(), target.c_str())) continue;
        const DWORD code = GetLastError();
        error = project_io_error("Failed to publish project entry", target, code);
        if (i >= 2) remove_project_entry(parent / k_readme_name, error);
        if (i >= 1) {
            const fs::path project = parent / layout.project;
            for (const ProjectResource& resource : k_project_resources) {
                remove_project_entry(project / resource.path, error);
            }
            remove_project_entry(project / k_map_name, error);
            for (const char* subdirectory : k_project_subdirs) {
                remove_project_entry(project / subdirectory, error);
            }
            remove_project_entry(project, error);
        }
        return code == ERROR_ALREADY_EXISTS || code == ERROR_FILE_EXISTS
            ? ProjectPublication::Conflict : ProjectPublication::Failed;
    }
    return ProjectPublication::Published;
}
} // namespace

void App::create_new_project(const NewFileCreateRequest& request, const fs::path& directory) {
    const std::string& name = request.file_name;
    if (trim_gui_ascii_copy(name) != name || name.back() == '.' ||
        ascii_lower(name) == "readme.md") {
        set_program_status("status.new_project.invalid_path");
        return;
    }

    const ProjectLayout layout(name);
    NewFileScenarioDraft draft;
    draft.title = name;
    draft.route = (layout.project / k_map_name).make_preferred().u8string();
    draft.route_title = name;
    std::string scenario_content;
    std::string error;
    if (!build_new_scenario_file_content(draft, scenario_content, error)) {
        set_program_status("status.new_project.invalid_path");
        KME_ADD_LOG(LogSeverity::Error, std::move(error));
        return;
    }

    std::error_code ec;
    const fs::path parent = fs::absolute(directory, ec).lexically_normal();
    if (ec) {
        set_program_status("status.new_project.create_failed");
        KME_ADD_LOG(LogSeverity::Error, "Failed to resolve project directory: " +
            directory.u8string() + " (" + ec.message() + ")");
        return;
    }
    for (const fs::path& entry : layout.publication_order()) {
        const fs::path target = parent / entry;
        if (GetFileAttributesW(target.c_str()) != INVALID_FILE_ATTRIBUTES) {
            set_program_status("status.new_project.conflict");
            KME_ADD_LOG(LogSeverity::Warning, "Project target already exists: " + target.u8string());
            return;
        }
        const DWORD code = GetLastError();
        if (code != ERROR_FILE_NOT_FOUND && code != ERROR_PATH_NOT_FOUND) {
            set_program_status("status.new_project.create_failed");
            KME_ADD_LOG(LogSeverity::Error, project_io_error("Failed to inspect project target", target, code));
            return;
        }
    }

    const fs::path stage = unique_project_directory(parent, ".komapedit-project-");
    if (!fs::create_directory(stage, ec)) {
        set_program_status("status.new_project.create_failed");
        KME_ADD_LOG(LogSeverity::Error, "Failed to allocate project staging directory: " +
            stage.u8string() + (ec ? " (" + ec.message() + ")" : " (target exists)"));
        return;
    }
    ProjectPublication result = ProjectPublication::Failed;
    if (write_project_stage(stage, layout, name, scenario_content, error) &&
        validate_project_stage(stage, layout, unit_distance_, error)) {
        result = publish_project_stage(stage, parent, layout, error);
    }
    fs::remove_all(stage, ec);
    if (ec) {
        KME_ADD_LOG(LogSeverity::Warning, "Failed to clean project staging directory: " +
            stage.u8string() + " (" + ec.message() + ")");
    }
    if (result != ProjectPublication::Published) {
        set_program_status(result == ProjectPublication::Conflict ? "status.new_project.conflict"
                                                                  : "status.new_project.create_failed");
        KME_ADD_LOG(LogSeverity::Error, std::move(error));
        return;
    }

    new_file_wizard_.open = false;
    set_program_status("status.new_project.created");
    if (request.load_after_create) open_document((parent / layout.scenario).u8string(), true);
}

#ifndef NDEBUG
int App::run_debug_headless_new_project(const HeadlessNewProjectOptions& options) {
    std::ofstream output_file;
    std::ostream* out = &std::cout;
    if (!options.output_path.empty()) {
        output_file.open(fs::u8path(options.output_path), std::ios::binary);
        if (!output_file) return 1;
        out = &output_file;
    }
    *out << "command=debug-headless-new-project\ndirectory=" << options.directory << '\n';
    const auto require = [&](const std::string& label, bool passed) {
        *out << label << '=' << (passed ? "PASS" : "FAIL") << '\n';
        if (!passed) throw std::runtime_error(label);
    };
    const auto read_file = [](const fs::path& path) {
        std::ifstream input(path, std::ios::binary);
        if (!input) throw std::runtime_error("Failed to read fixture: " + path.u8string());
        return std::string(std::istreambuf_iterator<char>(input), {});
    };

    ImGui::CreateContext();
    ImPlot::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = ImVec2(1280.0f, 720.0f);
    io.DeltaTime = 1.0f / 60.0f;
    io.Fonts->AddFontDefault();
    io.Fonts->Build();
    ImGui::NewFrame();

    fs::path fixture;
    bool passed = false;
    try {
        const fs::path parent = fs::absolute(fs::u8path(options.directory)).lexically_normal();
        require("existing_test_directory", fs::is_directory(parent));
        const fs::path root = unique_project_directory(parent, "komapedit-s2-");
        require("exclusive_fixture_directory", fs::create_directory(root));
        fixture = root;
        const auto case_directory = [&](const fs::path& name) {
            const fs::path path = root / name;
            if (!fs::create_directory(path)) {
                throw std::runtime_error("Fixture directory already exists: " + path.u8string());
            }
            return path;
        };

        UserSettings settings;
        settings.language = Language::En;
        settings.edit_mode_enabled = true;
        App app(nullptr, settings, 1.0f, false, false);
        app.history_path_ = root / "history.ini";
        app.recent_maps_.clear();
        app.creator_message_history_.clear();

        const std::string name = u8"线路 项目";
        const ProjectLayout layout(name);
        const fs::path project = layout.project;
        const fs::path map = project / "Map.txt";
        const fs::path stations = project / "Stations.txt";
        const std::string relative_route = name + "\\Map.txt";
        const std::array<std::pair<fs::path, std::string>, 8> contents = {{
            {fs::u8path("Scenario-" + name + ".txt"),
             "BveTs Scenario 2.00:utf-8\r\nTitle = " + name +
             "\r\nRoute = " + relative_route + "\r\nRouteTitle = " + name + "\r\n"},
            {"README.md", "# " + name + "\nBveTs scenario project created with komapedit.\n"},
            {map, "BveTs Map 2.02:utf-8\r\n"
                  "Structure.Load('Structures\\structures.txt');\r\n"
                  "Signal.Load('Signals.txt');\r\n"
                  "Sound.Load('Sounds\\sounds.txt');\r\n"
                  "Sound3D.Load('Sounds\\sound3D.txt');\r\n"
                  "Station.Load('Stations.txt');\r\n"},
            {stations, "BveTs Station List 2.00:utf-8\r\n"},
            {project / "Signals.txt", "BveTs Signal Aspects List 2.00:utf-8\r\n"},
            {project / "Sounds" / "sounds.txt", "BveTs Sound List 2.00:utf-8\r\n"},
            {project / "Sounds" / "sound3D.txt", "BveTs Sound List 2.00:utf-8\r\n"},
            {project / "Structures" / "structures.txt", "BveTs Structure List 2.00:utf-8\r\n"},
        }};
        const auto matches_initial_bytes = [&](const fs::path& directory) {
            return std::all_of(contents.begin(), contents.end(), [&](const auto& file) {
                return read_file(directory / file.first) == file.second;
            });
        };
        const auto submit = [&](const fs::path& directory, const std::string& project_name, bool load) {
            app.open_new_file_wizard(NewFileKind::Project);
            app.request_new_file_create({NewFileKind::Project, project_name, directory.u8string(),
                                        app.file_path_, false, load, {}});
            app.process_pending_new_file_create();
        };
        const auto wait_for_editable_map = [&]() {
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
            while (std::chrono::steady_clock::now() < deadline) {
                if (app.load_state_.running) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                    continue;
                }
                auto result = app.take_pending_load_result();
                if (!result) break;
                app.apply_load_result(std::move(*result));
            }
            const bool ready = !app.load_state_.running && app.has_model_ && app.edit_registry_loaded_;
            if (!ready) {
                *out << "map_load_status=" << app.program_status_key_ << '\n';
                for (const LogLine& log : app.logs_) *out << log.text << '\n';
            }
            return ready;
        };
        const auto lists_connected = [&](const fs::path& directory) {
            return std::all_of(k_project_resources.begin(), k_project_resources.end(),
                               [&](const ProjectResource& resource) {
                const size_t index = static_cast<size_t>(resource_list_kind_for_new_file(resource.kind).value());
                const auto& source = app.model_.resource_list_sources[index];
                return source.present && fs::u8path(source.resolved_path) == directory / project / resource.path;
            });
        };

        const fs::path create_only = case_directory("confirm");
        submit(create_only, name, false);
        require("confirm_creates_without_opening",
                std::string_view(app.program_status_key_) == "status.new_project.created" &&
                !app.new_file_wizard_.open && !app.has_model_ && !app.load_state_.running);
        require("confirm_writes_expected_bytes", matches_initial_bytes(create_only));

        const fs::path load_directory = case_directory(fs::u8path(u8"中文 目录"));
        submit(load_directory, name, true);
        require("confirm_and_load_opens_map", wait_for_editable_map() &&
                fs::u8path(app.file_path_) == load_directory / map);
        require("scenario_has_relative_route", app.scenario_preview_ &&
                app.scenario_preview_->title == name && app.scenario_preview_->route_title == name &&
                app.scenario_preview_->routes.size() == 1 &&
                app.scenario_preview_->routes.front().path == relative_route &&
                app.scenario_preview_->routes.front().weight == 1.0 &&
                app.scenario_preview_->vehicles.empty());
        require("exact_project_layout",
                matches_initial_bytes(load_directory) &&
                std::distance(fs::recursive_directory_iterator(load_directory),
                              fs::recursive_directory_iterator{}) == 11);
        require("five_empty_lists_connected", lists_connected(load_directory) &&
                app.model_.station_definition_rows.empty() && app.model_.structure_models.empty() &&
                app.model_.signal_aspects.empty() && app.model_.sound_list.empty() &&
                app.model_.sound_3d_list.empty());
        require("readme_absent_from_source_snapshot", app.model_.edit_files.size() == 6);

        app.ensure_table_cache();
        auto& station_draft = app.station_definition_edit_;
        require("first_station_row_created",
                app.insert_editable_list_row(station_draft, k_station_definition_edit_spec, -1, false));
        station_draft.rows.front().values = {
            "start", u8"验收站", "12:00:00", "12:00:30", "30", "12:00:00",
            "0", "0", "100", "", "", "0", "0"};
        app.apply_editable_list_drafts(station_draft, k_station_definition_edit_spec);
        require("first_station_row_applied", app.model_.station_definition_rows.size() == 1 &&
                table_cell(app.model_.station_definition_rows.front(), "stationKey") == "start");

        require("station_put_form_available", app.open_new_element_wizard_for_template("station.put"));
        auto& wizard = app.new_element_wizard_;
        wizard.target_file_path = app.file_path_;
        wizard.target_file_candidates = {app.file_path_};
        wizard.target_candidates_built = true;
        app.rebuild_new_element_wizard_form();
        set_edit_field_buffer(*find_inspector_field(wizard.form, "distance"), "100");
        set_edit_field_buffer(*find_inspector_field(wizard.form, "stationKey"), "start");
        set_edit_field_buffer(*find_inspector_field(wizard.form, "door"), "-1");
        require("first_map_element_applied", app.apply_new_element_insert() &&
                app.model_.station_positions.size() == 1 &&
                app.model_.station_positions.front().distance == 100.0 &&
                app.model_.station_positions.front().key == "start");
        require("apply_leaves_all_files_unchanged", matches_initial_bytes(load_directory));
        require("save_commits_first_row_and_element", app.save_pending_document_changes(false) &&
                !app.has_pending_edits() &&
                read_file(load_directory / stations).find(u8"start,验收站") != std::string::npos &&
                read_file(load_directory / map).find("Station['start'].Put(") != std::string::npos);
        const fs::path scenario = load_directory / layout.scenario;
        app.open_document(scenario.u8string(), false);
        require("reopen_preserves_first_edits", wait_for_editable_map() &&
                app.scenario_source_path_ == scenario.u8string() &&
                app.model_.station_definition_rows.size() == 1 &&
                table_cell(app.model_.station_definition_rows.front(), "stationName") == u8"验收站" &&
                app.model_.station_positions.size() == 1 && app.model_.station_positions.front().distance == 100.0 &&
                app.model_.station_positions.front().key == "start" && lists_connected(load_directory));
        require("save_preserves_other_files", std::all_of(contents.begin(), contents.end(), [&](const auto& file) {
            return file.first == map || file.first == stations ||
                read_file(load_directory / file.first) == file.second;
        }));

        constexpr std::string_view sentinel = "existing data\r\n";
        const auto entries = layout.publication_order();
        for (size_t i = 0; i < entries.size(); ++i) {
            const fs::path directory = case_directory("conflict-" + std::to_string(i));
            fs::path blocked = directory / entries[i];
            if (i == 0) {
                require("existing_project_directory_prepared", fs::create_directory(blocked));
                blocked /= "keep.txt";
            }
            std::string error;
            if (!create_utf8_bve_file_exclusive(blocked, sentinel, error)) throw std::runtime_error(error);
            submit(directory, name, true);
            require("preflight_conflict_" + std::to_string(i),
                    std::string_view(app.program_status_key_) == "status.new_project.conflict" &&
                    app.new_file_wizard_.open && read_file(blocked) == sentinel &&
                    std::distance(fs::directory_iterator(directory), fs::directory_iterator{}) == 1 &&
                    app.scenario_source_path_ == scenario.u8string());
        }
        const fs::path invalid = case_directory("invalid");
        for (const char* bad_name : {"..", "README.md", "ends.", "with#comment", "with;comment"}) {
            submit(invalid, bad_name, false);
            require(std::string("invalid_name_") + bad_name,
                    std::string_view(app.program_status_key_) == "status.new_project.invalid_path" &&
                    fs::is_empty(invalid));
        }

        const fs::path late = case_directory("late-conflict");
        const fs::path late_scenario = late / layout.scenario;
        std::string error;
        if (!create_utf8_bve_file_exclusive(late_scenario, sentinel, error)) throw std::runtime_error(error);
        require("late_conflict_rolls_back_published_entries",
                publish_project_stage(create_only, late, layout, error) == ProjectPublication::Conflict &&
                read_file(late_scenario) == sentinel &&
                std::distance(fs::directory_iterator(late), fs::directory_iterator{}) == 1);
        require("late_conflict_fixture_reset", fs::remove(late_scenario));
        submit(late, name, false);
        require("retry_after_conflict_succeeds", matches_initial_bytes(late));
        passed = true;
    } catch (const std::exception& error) {
        *out << "error=" << error.what() << '\n';
    }
    if (!fixture.empty()) {
        std::error_code ec;
        fs::remove_all(fixture, ec);
        *out << "fixture_cleanup=" << (ec ? ec.message() : "PASS") << '\n';
        passed = passed && !ec;
    }
    ImGui::EndFrame();
    ImPlot::DestroyContext();
    ImGui::DestroyContext();
    *out << "result=" << (passed ? "PASS" : "FAIL") << '\n';
    return passed ? 0 : 1;
}
#endif
