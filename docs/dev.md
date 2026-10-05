# Developer Guide

[简体中文](dev_zhcn.md) · [User guide](../README.md) · [Roadmap](../TODO.md) · [AI-assisted development](ai-dev.md)

This guide covers the development environment, architecture, source responsibilities, and validation workflows for komapedit. Work using AI coding tools must also follow [`ai-dev.md`](ai-dev.md), [`AGENTS.md`](../AGENTS.md), and the matching workflows in [`.agents/skills`](../.agents/skills).

## Scope and supported environment

komapedit is a Windows desktop application written in C++17 for viewing and editing BVE Trainsim maps. It uses Win32, DirectX 11, WIC, Dear ImGui, and ImPlot, with CMake and Ninja for builds.

The application has three runtime components:

- `maploader.dll`: parses maps and lists, handles Include and encodings, generates own-track and other-track geometry, and owns versioned typed map/edit snapshots.
- `model_loader.dll`: reads Structure meshes, materials, and textures through Assimp and exposes the data through a C ABI.
- `komapedit.exe`: provides the Win32/DirectX 11 GUI, tables, 2D plots, 3D previews, and editing workflows.

Implemented behavior is defined by the source and user guide. Pending work is listed in [`TODO.md`](../TODO.md), and completed work in [`TODO_done.md`](TODO_done.md).

## Prerequisites

- Windows
- CMake 3.20 or newer; 3.21 or newer is recommended for the generic Assimp runtime DLL copy fallback
- Ninja
- A C++17 compiler such as MSVC or MinGW
- Windows SDK, DirectX 11, and WIC development libraries
- Git
- Assimp discoverable by CMake as `assimp::assimp`

Fetch Dear ImGui and ImPlot:

```bat
.\get_3rd_party_packages.bat
```

Install Assimp separately. When using vcpkg, set `VCPKG_ROOT` and install the triplet matching your toolchain:

```bat
set VCPKG_ROOT=C:\path\to\vcpkg
%VCPKG_ROOT%\vcpkg install assimp:x64-mingw-dynamic
```

Build scripts use vcpkg when `VCPKG_ROOT` is set. If `VCPKG_DEFAULT_TRIPLET` is unset, they default to `x64-mingw-dynamic`; MSVC users should select a suitable triplet such as `x64-windows`. `install_Assimp.bat` is a MinGW helper that may need a local vcpkg path; keep that machine-specific path out of commits.

## Build and test

Debug build:

```bat
.\build_dev.bat
```

Release build:

```bat
.\build_release.bat
```

### Build completion notifications

`build_dev.bat` and `build_release.bat` send a completion notification after configuration, compilation, runtime-file checks, and the license-notice copy step.

For Windows Toast notifications, open Windows PowerShell (`powershell.exe`) and install the optional [`BurntToast`](https://www.powershellgallery.com/packages/BurntToast) module for the current user:

```powershell
Install-Module -Name BurntToast -Scope CurrentUser
Get-Module -ListAvailable -Name BurntToast
```

The scripts detect the module through `powershell.exe` and call `New-BurntToastNotification -Text 'Build finished'`, so verify that the module is available in that PowerShell environment.

Without `BurntToast`, the scripts use the Windows [`msg.exe`](https://learn.microsoft.com/windows-server/administration/windows-commands/msg) utility to display `build finished` to `%USERNAME%` for up to 10 seconds.

Run the registered Debug tests:

```bat
ctest --test-dir build --output-on-failure
```

Strict validation requires explicit configuration of the Debug directory:

```bat
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DKOMAPEDIT_STRICT_WARNINGS=ON -DBUILD_TESTING=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

`KOMAPEDIT_STRICT_WARNINGS` defaults to off. CTest registers seven contracts: `multilanguage_contract`, `typed_snapshot_contract`, `maploader_gradient_projection_contract`, `typed_edit_contract`, `maploader_diagnostics_contract`, `canvas3d_camera_contract`, and `route_value_sampling_contract`.

Test executables and headless implementations are compiled only in Debug: `build_dev.bat` enables `BUILD_TESTING`, while `build_release.bat` disables tests and checks for stale `*_tests.exe` files in its output. Run headless commands separately. The diagnostics contract uses Git-ignored local fixtures under `tests/`; check that they are available before running it.

`build\bin\typed_snapshot_tests.exe slop` runs regressions for object-key expressions, sparse Section indices, finite distances, and patch previews. These cases also belong to the snapshot/edit contracts.

`build\bin\typed_snapshot_tests.exe patch-bench 7` performs full dry runs for 100, 1,000, and 10,000 deterministic updates and measures `plan.patch_sources`. Each size receives one warmup, followed by all samples and median/p95 results. Repetitions default to 7, with a range of 5–50. Run this benchmark explicitly.

`build\bin\typed_snapshot_tests.exe distance-plan-bench 4000 middle` measures a single insertion in a synthetic Map with 1,000, 2,000, or 4,000 distance anchors. Use `middle` or `eof` for the target. Each process performs one warmup and five measured dry runs, reports `plan.prepare` and total median/p95 plus peak working-set bytes, and checks memory Apply/Reset and unchanged disk bytes.

Runtime output layout:

- Debug uses `build\`; Release uses `build_release\`.
- The output root contains `komapedit.exe`, `LICENSE`, `NOTICE`, and `THIRD_PARTY_NOTICES.md`.
- `bin\` contains `maploader.dll`, `model_loader.dll`, Assimp, and copied runtime DLLs.
- `settings\` contains the generated `settings.ini`, `history.ini`, and `imgui.ini`.
- Build and distribution-cleanup scripts stop immediately if obsolete INI files or DLLs exist in the output root and ask the user to handle them manually.

Keep build directories, cloned `third_party` trees, generated settings/CSV/test output, and temporary route, map, and model fixtures outside Git tracking.

## Source map

| Area | Main files and responsibilities |
| --- | --- |
| Public map ABI | `include/maploader.h`, `include/maploader_snapshot.h`: API v13 / map snapshot v9 functions, fixed-width POD snapshots, Scenario v2 snapshots and v2 direct-save drafts, edit batches, reports, spans, ownership, versions, and structure sizes |
| Map lifecycle | `src/maploader/maploader.cpp`: C ABI entry points, handles, rebuilds, dispatch, source access, and boundary error handling |
| Map state | `maploader_internal.h`: `MapContext`, parsed rows, source spans, Include stacks, edit references, reports, and timing |
| Parsing | `maploader_core.cpp`, `maploader_parser.cpp`, `text_decoder.cpp/.h`: statements, values, Include, variables, encodings, source anchors, and uniqueness checks |
| Scenario parsing and direct save | `scenario_route.cpp/.h`: BVE file-kind detection, the complete official Scenario snapshot schema, source-preserving direct save, and `Route` candidate resolution for opening a map from a Scenario |
| Geometry | `maploader_geometry.cpp`: own/other-track geometry, relocation, curves, gradients, placement buffers, and scene control points |
| Identity and snapshots | `maploader_identity.cpp`, `maploader_snapshot.cpp`, `maploader_semantic.cpp`: stable IDs, typed snapshots, revisions, comparison, and fingerprints |
| Editing | `maploader_edits.cpp`: dry run, memory apply, direct apply, commit, reset, source patches, encoding-aware writeback, and distance changes; restricted Include path updates are validated by full reparsing with old/new subtrees masked |
| Shared linkage | `include/repeater_linkage.h`, `include/own_track_transition_linkage.h`: Repeater chains and curve/gradient transition pairing |
| Shared route-value sampling | `include/route_value_sampling.h`, `src/main_window/route_value_sampling.cpp`: stable evaluated event sequences, BeginTransition/Interpolate interval classification, omitted-value inheritance, and endpoint distances shared by 2D/3D |
| Numeric safety and edit timing | `include/numeric_safety.h`: finite/range checks for GUI/scene double-to-integer conversion; `include/operation_timing.h`: optional inclusive steady-clock timing in separate GUI and maploader domains |
| Model import | `src/model_loader/model_loader.cpp`, `include/model_loader.h`: Assimp isolation and model-loader API v2 |
| Main window | `gui_kme.cpp`, `kme.h`, and focused `src/main_window/` modules: App state coordination, Win32/D3D11 startup, common utilities/background images, snapshot hydration/loading, edit/distance/Inspector/list-draft/new-file/new-element workflows, dialogs/UI, scene previews, and headless entry points. New-file rendering is in `new_element_wizard.cpp`, content/dialog helpers in `app_dialogs.cpp`, deferred creation in `element_inspector_data.cpp`, and workflow contracts in `headless_entrypoints.cpp` |
| Runtime and settings | `app_settings.cpp/.h`, `runtime_paths.cpp/.h`, `maploader_runtime.cpp`: INI files, executable-relative paths, DLL loading, and exact API checks |
| Source tools | `file_structure_diagram.cpp`, `text_preview.cpp`: Include graphs, working-copy previews, source actions (replace an Include file or remove its reference), and distance-boundary selection |
| Debug validation | `debug_headless.cpp/.h`, `headless_entrypoints.cpp`, `edit_benchmark.cpp`, `src/table/datatable_benchmark.cpp`, `touch_input.cpp/.h`: argument parsing, headless contracts, production workflows, edit/table-cache benchmarks, camera transfer, find, touch, editing, and file-creation checks |
| 2D views | `src/canvas2d/canvas2D.cpp`: plan/profile data, cached `Curve.Interpolate` endpoint-marker hydration, and plan rendering; `canvas2d_view_state.cpp/.h`: pan/zoom/rotation and coordinate conversion; `canvas2d_marker_cache.cpp/.h`: track sampling and marker/Repeater overlay caches; `canvas2d_interaction.cpp/.h`: measurement/marker hits, context targets/actions, and source mapping; `canvas2d_background.cpp/.h`: image coordinates, drawing, and two-point alignment; `canvas2d_primitives.cpp/.h`: screen transforms, clipped polylines, grids, scale bars, and markers; `profile_plots.cpp`: elevation and radius plots |
| 3D views | `include/canvas3D.h` and `src/canvas3d/canvas3D.cpp`: public preview interface and thin delegation; private `canvas3d_impl.h` state and focused `canvas3d_*.cpp` implementations, listed below; `scene_track_sampling.cpp/.h` and `scene_route_overlay.cpp/.h`: CPU sampling and pure route-information formatting |
| Tables and navigation | Focused `src/table/datatable*.cpp` modules, `datatable_internal.h`, and `table_navigation.cpp`: shared cells/columns, a single cache hydration path, find, inline resource-list editing, route/effect/Scenario windows, Debug benchmarks, and row/plan/scene navigation |
| Shared markers | `include/map_marker_visuals.h`, `map_marker_visuals.cpp`: the shared visual recipes for 2D/3D markers |
| Localization | `include/multilanguage.h`: Simplified Chinese, Traditional Chinese (Taiwan), Traditional Chinese (Hong Kong), English, and Japanese UI text |

Keep source ownership, linkage, markers, navigation, parsing, validation, and writeback within their component boundaries and shared implementations.

## Detailed code walkthrough

This section describes the main responsibilities of files in `include/` and `src/`. The GUI calls both DLLs through C ABIs; the parser owns source identity, snapshots expose ABI data views, and the editor generates, validates, and commits source patches.

### Public ABIs, shared algorithms, and resource headers

#### `include/maploader.h`

- **Exports and logging**: `KV_API` controls DLL export/import; `KvLogCallback` and `kv_set_log_callback()` deliver English diagnostics to the host. `kv_api_version()` returns the ABI version, which the EXE must match exactly.
- **Scenario interfaces**: `kv_probe_file_kind()` distinguishes maps from Scenarios; `kv_resolve_scenario_routes()` returns Route candidates with existing target files, released by `kv_free_scenario_candidates()`; `kv_load_scenario_snapshot()` returns an independent v2 snapshot, released by `kv_free_scenario_snapshot()`; v2 `kv_save_scenario_document()` checks the source hash, fully reparses the changes, and writes transactionally in the original encoding, supporting changes to the count and order of existing Route/Vehicle candidates.
- **Handles and geometry**: `kv_load_map_ex()` selects preview or edit metadata through `KV_LOAD_PREVIEW` and `KV_LOAD_EDIT_METADATA`. `kv_generate_geometry()` and `kv_generate_scene_geometry()` generate regular and scene track data, updating the handle's caches and their respective revisions.
- **Read-only snapshots**: `kv_get_map_snapshot()` and `kv_get_scene_geometry_snapshot()` validate versions and structure sizes, then return handle-owned views. Callers copy required data before reparsing or the corresponding geometry invalidation; the handle manages nested storage.
- **Editing and source access**: `kv_get_edit_target_typed()` retrieves fields and source information for a stable edit ID; `kv_get_source_text()` returns decoded text from disk or the memory override. `kv_edit_dry_run_typed()`, `kv_edit_apply_to_memory_typed()`, `kv_edit_apply_typed()`, `kv_edit_commit_typed()`, and `kv_edit_reset_memory()` respectively validate, apply to the working copy, write directly, commit the working copy, and discard overrides.
- **Errors and release**: `kv_get_last_error()` returns thread-local error text; `kv_free()` releases the map handle, and `kv_free_string()` releases standalone DLL-allocated strings.

#### `include/maploader_snapshot.h`

- **Versions and common views**: the header defines API/snapshot versions, `KV_INDEX_NONE`, capability bits, and edit flags. `KvUtf8View` is call-scoped UTF-8 input; `KvStringRef`, `KvSpan`, and `KvDoubleBuffer` are fixed-width views into snapshot arenas/arrays.
- **Scenario snapshots**: `KvScenarioPathWeightRow` stores relative paths, weights, and explicit-weight flags; v2 `KvScenarioSnapshot` stores the source hash and presence bits for eight fields; `KvScenarioEditDocument`/`KvScenarioEditPathRow` are call-scoped inputs to `kv_save_scenario_document()`.
- **Values and source identity**: `KvValueKind` and `KvValue` represent null, numbers, strings, and continue; `KvSourceFileRow`, `KvSourceSpanRow`, `KvStatementRow`, `KvElementRow`, and `KvRowMetadata` retain physical files, Include stacks, byte/line/column spans, original arguments, parse order, and stable edit IDs.
- **Typed rows**: `KvTrack`, `KvStation*`, `KvStructure*`, `KvRepeater*`, `KvSignal*`, `KvSection*`, `KvSound*`, `KvOtherTrain*`, and POD records for curves, gradients, speed limits, and environmental effects define each row's fields. Variable arguments reference shared value arrays through `KvSpan`.
- **Root snapshots**: `KvMapSnapshot` collects the string arena, values, typed row arrays, geometry matrices, source registries, capability bits, and content/geometry revisions. `KvSceneGeometrySnapshot` holds scene control points and track matrices with an independent invalidation lifecycle.
- **Edit protocol**: `KvEditField` and `KvEditTargetSnapshot` describe editable fields; `KvEditOperation`, `KvEditChange`, and `KvEditBatch` represent insert/update/delete requests. Distance resolution, patch previews, committed files/rows, and `KvEditReportSnapshot` describe validation and commit results. Requests and snapshots check versions and structure sizes as specified by the public headers; field changes require synchronized EXE/DLL updates.

#### `include/model_loader.h`

- `MlVertex` stores position, normal, and UV; `MlMaterial` stores diffuse color and texture paths; `MlMeshPart` binds index ranges to materials; `MlMeshData` collects vertices, indices, materials, parts, and bounds/center/radius. `ml_api_version()` returns v2, which the EXE checks exactly.
- `ml_api_version()`, `ml_load_model()`, `ml_free_model()`, and `ml_get_last_error()` form the complete C ABI. The DLL allocates all arrays; `ml_free_model()` releases them after success or partial failure.

#### `include/repeater_linkage.h`

- `EventKind` classifies `Begin`/`Begin0` as Begin and distinguishes End and other events; `BoundaryKind` distinguishes explicit End, a subsequent Begin, and an open boundary; `Event` retains distance, global parse order, repeater key, and source-row index.
- `canonical_key()` standardizes case-insensitive key comparison. `pair_linkage()` sorts stably by distance, global parse order, and source-row index, maintains active chains per repeater key, and produces `Chain` and `Segment` records. A new Begin closes the previous segment; End closes the chain. The last parsed event at a distance determines the active state.
- `pair_segments()` provides a flat segment list shared by snapshots, tables, 2D, and 3D.

#### `include/own_track_transition_linkage.h`

- `EventKind` represents curve/gradient `BeginTransition` events and their possible Begin/End consumers; `Pair` stores transition-row and consumer-row indices.
- `consumes_curve_transition()` and `consumes_gradient_transition()` define which statements consume pending transitions. `pair_transitions()` tracks separate pending curve/gradient states in source order and returns pairs and orphans; editing and marker paths use these to bind transitions to their consumers.

#### `include/map_marker_visuals.h`

- `MapMarkerVisualKind` defines the shared 2D/3D element visuals; `map_marker_visual_bit()` maps them to visibility bits.
- `MapMarkerPrimitiveKind`, `MapMarkerColorRole`, and `MapMarkerIconVariant` define primitives, color roles, and variants; `MapMarkerIconPrimitive` and `MapMarkerIconRecipe` store normalized points, line widths, closed/filled flags, and glyphs.
- `map_marker_theme_color()`, `map_marker_role_color()`, `map_marker_icon_recipe()`, and `draw_map_marker_icon()` unify colors, icon recipes, and ImDrawList interfaces for 2D/3D.

#### `include/route_value_sampling.h` and `src/main_window/route_value_sampling.cpp`

- `Event` retains evaluated route values and event kinds emitted by maploader in stable distance order. `append_event()` identifies ordinary values, `BeginTransition`, and `Interpolate`; an argument-free Interpolate inherits the previous value.
- `sample()` serves both 2D and 3D: constant intervals return the current value; transition/interpolation intervals return endpoint distances and values.

#### `include/numeric_safety.h` and `include/operation_timing.h`

- `kme::truncating_int_or_zero()` truncates finite doubles within the `int` range and returns `0` for nonfinite or out-of-range values. GUI and scene code share this boundary.
- `kme::timing::Timing` provides thread-local, explicitly activated inclusive stage timing. GUI and maploader use separate domains, accumulating milliseconds and counts per stage for console diagnostics.

#### `include/canvas3D.h`

- **Scene inputs**: `Canvas3DTrackPoint/Path/Visibility` describe track samples and display; `Canvas3DSceneObject`, `Canvas3DModelInstance`, `Canvas3DRepeaterSegment`, and background/fog/draw-distance events form scene inputs.
- **Route information and markers**: `route_value_sampling::Event`, stations, speed limits, and Section signal events support sampling at the camera distance. `Canvas3DSceneMarker` stores visual kind, distance, track position, table target, and edit ID; `Canvas3DSceneMarkerVisibility` controls index rebuilding with per-kind bits.
- **Build and refresh structures**: `Canvas3DScene` is a renderer-independent CPU scene description; `Canvas3DSceneBuildOptions/Result` and `Canvas3DSceneMapRefreshOptions` distinguish initial builds, dynamic-content refreshes, and map-content refreshes; `Canvas3DSceneStats` exposes instance, model, and frame-rate statistics.
- **Interaction structures**: camera poses, context actions, pick targets, `Canvas3DPlacementEditTarget`, drag axes, and `Canvas3DPlacementDragUpdate` turn renderer interaction into source-field updates that the GUI can apply.
- **`Canvas3D` facade**: exposes single-model and scene loading, refresh, visibility, view distance, fog, gizmos, camera controls, performance warnings, and debugging, delegating implementation to its private PImpl.

#### `include/multilanguage.h`

- `Language` selects Japanese, English, Simplified Chinese, Traditional Chinese (Taiwan), or Traditional Chinese (Hong Kong); the five `Translation` fields hold equivalent UI text. `Language::ZhTw` selects `zh_tw`, and `Language::ZhHk` selects `zh_hk`. Translation constants are grouped by windows, menus, toolbars, tables, property editing, errors, and 2D/3D operations.
- `tr()` and language-selection helpers return the active language field. Add all five translations in the same `Translation` initializer, with matching format placeholders. Use Taiwan terminology for `zh_tw` (rows: 列; columns: 欄) and Hong Kong terminology for `zh_hk` (rows: 行; columns: 欄; cells: 單元格). Base Hong Kong translations on the Simplified Chinese text, consulting Japanese and English for terminology when needed.
- The existing `[General] language` setting stores `ja`, `en`, `zh`, `zh-TW`, or `zh-HK`; Simplified Chinese remains the default. The `中文` submenu contains `简体`, `台湾繁體`, and `香港繁體` and sits alongside the English and Japanese options.
- `multilanguage_contract` checks matching key sets, nonempty values, placeholder names and counts, language lookup, and critical terminology. Font loading retains the full Chinese glyph range and includes Microsoft JhengHei (`msjh.ttc`) after the existing CJK candidates and before Segoe UI.

#### `include/resource.h`

- Windows resource compilation and C++ share resource IDs; `IDI_KOMAPEDIT` identifies the application icon in `komapedit.rc`.

### maploader state, parsing, and snapshots

#### `src/maploader/maploader_internal.h`

- **Utilities and timing**: string helpers, `SteadyClock`, `LoadTiming`, `ScopedTimer`, and `ActiveTimingScope` support read, parse, merge, geometry, and snapshot timing; task-slot declarations limit concurrent expensive loads.
- **Decoding and parse options**: `LoadedText` retains raw bytes, UTF-8 content, encoding, BOM, newlines, and line starts; `MapParseOptions` selects preview/edit metadata levels; `SourceTextOverride(s)` holds working-copy overrides.
- **Expression values**: `ValueKind`, `Value`, and `VariableEnvironment` represent parse-time null/number/string values and variable bindings. Shared immutable environment snapshots attach to statements for semantic checks during distance moves.
- **Source model**: `SourceFileRecord`, `FileStructureRecord`, `SourceSpan`, `ParsedStatement`, `EditSourceRef`, and `MapDiagnostic` retain physical files, Include invocation identity, original statements, line/column/byte anchors, parse order, and edit identity.
- **Parsed rows**: `CurveEditRow`, `GradientEditRow`, `OtherTrackChange`, and Station, Structure, Repeater, Signal, Section, Sound, Train, speed-limit, and effect records provide typed data to geometry, snapshots, and editing. Each row's `EditSourceRef` supports source writeback.
- **Matrix and snapshot storage**: `Matrix` manages contiguous row/column double buffers; `MapSnapshotStorage`, `SceneGeometrySnapshotStorage`, `EditTargetSnapshotStorage`, and `EditReportSnapshotStorage` own the vectors/string arenas behind ABI pointers.
- **`MapContext` aggregate**: owns the main path, source/Include tables, variable environment, all parsed rows, track/scene matrices, control points, revisions, snapshot caches, working-copy overrides, disk-baseline hashes, latest edit report, and timing. It is the ownership root for parsing, geometry, snapshots, and editing.
- **Active-statement and edit RAII**: `ActiveStatementScope` sets the active statement/source environment during dispatch and restores it afterward. `MapEditChange` and specialized field structures hold copied ABI requests; semantic snapshots, distance-resolution records, and patch/commit reports support transaction validation.
- **Internal declarations**: the end of the header declares DLL-internal parse, geometry, snapshot, semantic, edit, and identity entry points.

#### `src/maploader/text_decoder.h`

- Declares conversions between UTF-8 and `std::filesystem::path`, binary reading, UTF-16/code-page decoding, BOM/first-line detection, and encoding-aware writeback.
- `FileOpenFailureKind` distinguishes missing files, permission failures, directories, and general open failures for consistent parser/model-loader diagnostics. Writeback receives the target encoding and BOM and fails on unrepresentable characters.

#### `src/maploader/text_decoder.cpp`

- `classify_file_open_failure()`, `file_open_failure_message()`, and `read_binary_file()` handle reading and Windows error classification; `path_to_utf8()`, `utf8_to_wide()`, `wide_to_utf8()`, `path_from_utf8()`, and `join_utf8_path()` isolate Win32 wide-path details.
- `decode_codepage()` uses strict/permissive Windows code-page conversion, with a limited fallback on other platforms. `append_utf8_codepoint()` and `decode_utf16()` handle endianness, surrogate pairs, and invalid sequences.
- `decode_text_bytes()` selects UTF-8, UTF-16, or CP932 from the declared encoding/BOM; `first_line_ascii()` and `has_utf8_bom()` identify map headers before full decoding.
- `append_utf16_bytes()` and `encode_text_for_writeback()` convert the UTF-8 working copy back to its original encoding, preserve the BOM, and throw on unrepresentable characters.

#### `src/maploader/maploader_core.cpp`

- **Tasks, timing, and scalars**: `try_acquire_maploader_task_slot()`/`release_maploader_task_slot()` control load concurrency; `ActiveTimingScope` records total active time. `ascii_lower()`, trimming, `parse_finite_number()`, `canonical_number()`, and version/encoding-header parsing provide shared scalar rules.
- **Text loading**: `build_line_starts()`, `detect_newline()`, `make_loaded_header_text()`, and both `load_header_text()` overloads turn raw bytes into `LoadedText` with encoding, newline, and body-position metadata, preferring memory overrides.
- **Value/key conversion**: `as_number()`, `as_text()`, `key_text()`, `track_key_display_text()`, `track_key_from_display_text()`, and CSV/INI helpers unify parser values, table semantics, and edit-text representations.
- **Source registration and location**: `normalized_source_path/key()`, `current_source_text()`, `register_source_file_index()`, and Include-stack/invocation-key interning deduplicate source identities. `line_column_for_body_pos()` and `make_source_span()` convert body offsets to stable physical anchors.
- **Statements and environments**: `current_variable_environment_snapshot()`, `rebuild_variable_environment_snapshot()`, `add_parsed_statement()`, and `next_active_edit_ref()` register statements, environments, and edit references; merge/offset helpers merge Include contexts while preserving index validity.
- **List-row source records**: `add_loaded_line_statement()` and `extend_loaded_line_statement()` register editable statements for physical CSV/list rows; `parse_signal_aspect_source_values()` and field-name helpers retain variable structure-key columns and glare-row shapes.
- **Dependencies and state updates**: `value_equal()`, variable read/write records, and `log_load_timing()` support semantic validation and performance logs. `add_controlpoint()`, `set_distance()`, `put_own()`, `ensure_othertrack()`, and `put_other()` are shared parser-dispatch entry points for track-event state.

#### `src/maploader/maploader_parser.cpp`

- **Lexer and statement loop**: `Parser::parse()` processes the file; `eof()`, `peek()`, `skip()`, `accept()`, and `expect()` handle whitespace, comments, and punctuation. Diagnostics retain locations, with `finish_statement()`/`synchronize_statement()` recovering at the next statement.
- **Objects, functions, and expressions**: `parse_label()`, `parse_variable_name()`, `parse_map_object()`, `parse_map_function()`, and `parse_map_args()` build `MapObject`/`MapFunction`; `parse_expression()`, `parse_prefix()`, `parse_primary()`, `apply_binary()`, and `call_function()` implement precedence, variables, strings, numbers, and supported math functions.
- **Include workflow**: `include_path_is_simple_string()` checks preview paths; `make_child_seed()` inherits ordinary variables and Include identity, starting the child at zero distance with an empty distance expression. `parse_include_context()` supports parallel parsing; `queue_include()` and `flush_pending_includes()` merge sources, diagnostics, events, and variable writes in original order, preserve the parent distance, and reparse stale variable dependencies. Random engines derive from the process-session seed, source path, and lexical Include order. Reparsing retains the queued seed so Preview/Edit loads and working-copy reparses can replay unchanged `rand()` calls.
- **Syntax validation and dispatch**: `method_rules()` defines arity/null rules; `object_path()` and `validate_statement()` enforce general syntax; `dispatch()` routes top-level objects to specialized handlers. `record_deferred_semantics()` records keys requiring validation after resource lists have loaded.
- **Own and other tracks**: `dispatch_curve()`, `dispatch_gradient()`, and `dispatch_legacy()` record curve, gradient, and legacy events; `dispatch_track()`, `setposition_interpolate()`, and `track_position()` record other-track position, interpolation, gauge, center, and cant events.
- **Resource lists**: `load_resource_list()` and `record_resource_list_load()` retain Load expressions, evaluated paths, and source identities. `parse_station_list()`, `parse_structure_list()`, `parse_signal_aspect_list()`, and `parse_sound_list()` turn physical rows into typed editable records; `parse_other_train_file()` reads other-train files.
- **Map-element dispatch**: `dispatch_station/speedlimit/section/signal/beacon/pretrain/structure/sound/train/repeater/irregularity/background/adhesion/cab_illuminance/fog/draw_distance()` and three noise handlers check method shapes, read arguments, and append rows; `add_other_train_definition()` registers Train definitions.
- **Post-parse diagnostics**: `validate_unique_preview_statements()` checks duplicate Load/Enable declarations; `append_transition_diagnostics()` checks pairing; `append_deferred_key_diagnostics()` checks resource keys. When stations use arrival/departure sounds, `append_station_sound_load_order_diagnostic()` checks that `Sound.Load` precedes `Station.Load`. Incorrect order produces an English `[WARN]`: compare line/column within a file, Include depth across files, then global parse order at equal depth. `emit_diagnostics()` publishes diagnostics and turns errors into load failures.
- **Module entry point**: `parse_map_context()` creates `MapContext`, loads the main file, runs Parser, refreshes environments/diagnostics, and returns the complete context. All loads and post-edit reparses share it.

#### `src/maploader/maploader_geometry.cpp`

- **Track state machine**: `LastPos` holds the previous sample; `TrackPointer` advances own-track events by distance and supplies radius, gradient, cant, orientation, and coordinates for regular and event-boundary sampling.
- **Curve mathematics**: `rotate_xy()`, Gaussian integration, and Fresnel series/asymptotics integrate local coordinates; `circular_curve*()` computes circular curves, while `halfsin_intermediate()`, `linear_transition_curve_local()`, and `transition_curve*()` compute half-sine/linear transitions. Key/hash structures cache repeated curve parameters.
- **Gradient projection**: `constant_gradient_projection()`, `sinc()`, and `gradient_transition()` compute horizontal projection and elevation from route length; `build_gradient_projection_samples()` and `build_event_projected_distances()` map event distances to plan positions.
- **Own-track generation**: `sorted_unique()` and `append_arange()` combine event and regular sample points; `generate_owntrack()` writes distance, XYZ, orientation, radius, gradient, cant, and related columns; `generate_curveradius()` produces curve-radius plot data.
- **Other-track generation**: `relative_position()` computes curve-relative offsets; `CantProcessor` handles cant Begin/End/Interpolate; `build_othertrack_buffer()` combines position, X/Y interpolation, gauge, center, and cant into matrices aligned with the own track, with validity data.
- **Relocation and placement**: `relocate()` translates geometry to stable local coordinates; `build_structure_put_buffer()` supplies distance-sampled transforms for structure placement.
- **Adaptive scene control points**: angle/matrix helpers and `build_scene_adaptive_controlpoints()` combine events, model spans, curvature, gradients, and the requested range into control points with adaptive density.
- **Entry point**: `generate_geometry()` generates the own track, curve-radius data, other tracks, placement buffers, and scene control points, then updates timing, capabilities, and snapshot revisions.

#### `src/maploader/maploader_identity.cpp`

- `stable_hash64()` provides deterministic 64-bit hashing, `hex64()` produces fixed hexadecimal text, and `edit_kind_token()` normalizes row kinds.
- `make_edit_id()` combines normalized source key, global parse order, statement kind, and local ordinal into a stable ID; `statement_edit_id()` caches statement IDs; `native_element_edit_id()` and `element_edit_id()` provide shared native-row and derived-identity entry points.

#### `src/maploader/maploader_snapshot.cpp`

- `matrix_view()` and `data_or_null()` project contiguous internal containers into ABI views.
- `MapSnapshotBuilder::build()` adds root data, tracks, stations, structures, other trains, sections/signals/sounds, effects, creator messages, preview rows, and the edit registry, then calls `finalize()` to bind the snapshot.
- `string_ref()` reuses strings in the shared arena and appends new text; `value()`/`append_values()`/`append_strings()` build values and spans; `metadata()` converts `EditSourceRef` to `KvRowMetadata`. Each `add_*` block copies typed fields and attaches source data according to capability bits.
- `add_element(s)()` registers row-kind/edit-ID to row-index mappings; `bind()` binds raw pointers after vector growth is complete; `finalize()` writes versions, structure sizes, counts, revisions, and capabilities.
- `invalidate_map_snapshot()` and `invalidate_scene_geometry_snapshot()` define content, regular-geometry, and scene-geometry invalidation boundaries. `build_map_snapshot()` and `build_scene_geometry_snapshot()` rebuild caches lazily; `ordered_station_list_entries()` preserves physical station-list order.

#### `src/maploader/maploader_semantic.cpp`

- `SemanticWriter` writes types and values in fixed order while computing a hash; `field()`, `value_span()`, `begin_element()`, and `emit_element()` produce normalized semantic representations.
- `changed_field()` and number/string/value/track-key readers overlay a `MapEditChange` on snapshot values, rejecting invalid numbers or missing required values.
- `write_structure_model()`, `write_sound_list()`, `write_structure_put()`, `write_structure_between()`, `write_station_put/list()`, `write_signal_aspect/put()`, `write_repeater()`, and `write_*` handlers for beacon, sound, noise, background, adhesion, and fog define the fields expected to remain equal or change for each kind.
- `write_curve()`, `write_gradient()`, and `write_other_track_change()` retain method, arity, and pairing; `write_section_row()` supports variable value lists; `reject_unknown_target_fields()` rejects undeclared fields.
- `build_semantic_map_snapshot()` traverses protected elements to build an edit-ID-to-semantics index and map/environment fingerprints. `expected_target_semantic()` computes update/delete expectations; `FakeInsertSnapshotState`, `insert_semantic_container()`, and `expected_insert_semantic()` construct equivalent expectations for insertions.

#### `src/maploader/maploader_edits.cpp`

- **ABI input copying**: `copy_utf8_view()` and `copy_edit_batch()` validate structure sizes, pointer/length pairs, operations, flags, duplicate fields, and UTF-8 view lifetimes, then copy call-scoped POD into internal `MapEditChange` records.
- **Patch locations**: `load_source_patch()` reads the working copy; `source_range_in_text()`, `safe_statement_removal_range()`, and preview helpers convert `SourceSpan` to UTF-8 text ranges while preserving neighboring comments and statements.
- **Distance expressions**: scanners identify predefined `distance`, top-level addition/subtraction, and safe numeric addends. `find_safe_numeric_distance_addend()`, `apply_delta_to_distance_addend()`, and `adjust_distance_expression_by_delta()` preserve variable expressions and modify safe constants where possible, otherwise proposing distance resolution.
- **Arguments and CSV**: BVE argument splitting/quoting and numeric/optional/value/key helpers preserve unchanged raw arguments; CSV parsing, equivalence checks, and `build_editable_csv_list_statement()` preserve delimiters, trailing fields, and encoding representability.
- **Statement builders**: `build_structure_model/sound_list/station_list/signal_aspect_statement()` handles list rows; `build_station_put/structure_put/signal_put/repeater_statement()` handles explicit method/argument-shape conversions, including both directions between normal and zero-offset Structure/Repeater forms. Other `build_*` functions cover curves, gradients, other tracks, Section, speed limits, beacons, sounds/noises, and effects while retaining method and argument shape.
- **Targets and target snapshots**: templated `match_edit_ref()`, `find_simple_target()`, and `find_editable_target()` locate edit IDs in MapContext's typed rows; `build_edit_target_snapshot()` emits fields, original values, raw arguments, constraints, sourceHash, and expectedSourceHash.
- **Insertion validation**: `validate_insert_field_names()`, `validate_insert_method()`, and `validate_insert_change()` check row kind, method, and structured fields; `build_insert_statement()` generates ordinary BVE statements from them.
- **Distance-section planning**: `DistanceSectionAnalysis/PlanningIndex` indexes physical files, Include invocations, and distance sections to plan initial blocks, gaps, final blocks, and EOF. Moves within a clear final section may extend in its direction; insertions prefer an existing unique position, with ambiguous positions offered to the user. Candidate filtering shares variable/physical-instance checks; the selected plan receives full semantic validation before apply or disk writes.
- **Reports and transactional writes**: `build_edit_report_snapshot()` projects patch, resolution, and commit data; hash/temp-file helpers create staging files alongside the targets. `replace_files_transactionally()` replaces files in stages and rolls back failures; `TransactionalWriteError` retains both primary and rollback errors.
- **Full semantic validation**: `parse_report_candidate()` reparses with patch overrides; `validate_non_target_derived_state()`, `own_track_transition_state()`, and `validate_edit_report()` compare non-target elements, final variable bindings, station ownership, transition pairing, and each target's expected semantics. Valid edits may change the final current `distance`.
- **Batch pipeline**: `build_edit_report()` prepares targets, groups physical contexts and target distances, resolves boundaries, generates updates/deletions/insertions, checks overlaps, reparses, and validates. Dry run, memory Apply, and direct Apply share this core.
- **Working copy and commit**: `apply_patched_files_to_overrides()`, `reparse_context_with_overrides()`, and `apply_edit_report_to_memory()` update overrides while retaining the disk baseline; `reset_memory_edits()` returns to disk; `populate_committed_edit_state()` records saved identities; `commit_memory_edits()` rechecks and transactionally writes all overrides.

#### `src/maploader/maploader.cpp`

- `parse_options_from_load_flags()` converts public flags to internal profiles and rejects unknown combinations.
- `kv_*` exports validate handles, versions, structure sizes, and arguments before calling the implementation. They catch exceptions at the C ABI boundary, set last error, and return failure values.
- `kv_load_map_ex()` creates `MapContext`; the two geometry entry points update matrices/revisions; the two snapshot entry points return cached views; edit-target/source-text entry points return working-copy information.
- Scenario exports call `probe_bve_file_kind()`, `load_scenario_document()`, or `resolve_scenario_route_candidates()`, keeping independently allocated snapshots/candidate blocks and matching release functions within the DLL ownership boundary.
- Dry run builds a report; memory apply builds and installs overrides; reset discards overrides; direct apply writes the report transactionally; commit saves the validated working copy. `kv_free()`/`kv_free_string()` match DLL allocation ownership.

#### `src/maploader/scenario_route.h` and `src/maploader/scenario_route.cpp`

- `probe_bve_file_kind()` reads the start of the file to distinguish Map, Scenario, and Unknown. Unreadable files remain Unknown so the normal load path can report the detailed error.
- `load_scenario_document()` parses `BveTs Scenario 2.00` in its declared encoding, handles `#`/`;` comments and last-value-wins duplicate fields, and retains eight official fields, source hash/presence bits, and source-relative Route/Vehicle paths, weights, and explicit-weight flags.
- `save_scenario_document()` rereads disk and checks the expected source hash. It minimally patches candidates in the last effective field when counts match, or rewrites that field's complete candidate value when counts change. It rejects empty paths, reserved syntax characters, and nonpositive/nonfinite weights, requires at least one candidate per existing path field, then fully reparses and uses shared transactional writeback.
- `resolve_scenario_route_candidates()` reuses the parsed Scenario, resolves Route paths relative to its directory, and verifies target files exist; snapshot reading retains the declared data.

#### `src/maploader/diagnostics.h` and `src/maploader/diagnostics.cpp`

- The header declares the log callback, last-error, and info/warn/error interfaces. The callback is published through atomic access; each calling thread retains its last error in `thread_local` storage.
- `emit_log()` forwards complete log lines; `log_info_at()`, `log_warn_at()`, and `log_error_at()` add severity and source filename. ABI catch blocks use `set_last_error()` to store queryable errors.

#### `src/maploader/c_api.h` and `src/maploader/c_api.cpp`

- This DLL-internal helper layer manages C-string ownership. `copy_c_string()` uses `std::malloc()` to copy NUL-terminated UTF-8 text for the public ABI; `kv_free_string()` releases it through the matching `std::free()` path.

### Model loading

#### `src/model_loader/model_loader.cpp`

- Path/extension helpers normalize Assimp format hints; `copy_c_string()` allocates ABI strings for material texture paths; `resolved_texture_path()` resolves model-relative textures into UTF-8 paths.
- `free_mesh()` idempotently releases vertices, indices, material strings, and mesh parts; `MeshCleanupGuard` handles exception cleanup; `assign_bounds()` computes bounds, center, and radius.
- `load_with_assimp()` uses the shared binary reader for Unicode paths, then invokes Assimp. It combines aiMesh vertices/normals/UVs/indices, builds materials and parts, resolves the first diffuse texture, and computes bounds.
- `ml_api_version()` returns v2; `ml_load_model()` clears output, catches exceptions, and fills `MlMeshData`; `ml_free_model()` is the public release entry point; `ml_get_last_error()` returns thread-local diagnostics.

### Main-window state, loading, and editing

#### `src/main_window/kme.h`

- **Math and hashing**: `KmeByteHash64` supplies deterministic byte hashes for GUI caches/debugging; `Matrix` is a GUI-owned 2D double buffer copied from the ABI.
- **Map model**: `TrackEvent`, `OwnTrackEditMarker`, `OtherTrack`, Station/SpeedLimit/Section data, and `TableRow` collections form `MapModel`. It copies and owns the source registries, resource-list metadata, revisions, capabilities, and regular/scene matrices needed by the GUI.
- **2D data**: `View2D` stores pan, scale, and rotation; `TrackPoint`, `PlanMarker` and its aliases, `PlanRepeaterSegment`, `OtherTrainPathOverlay`, `PlanData`, and `ProfileData` feed canvas caches and hit testing.
- **Tables and source tools**: `TableRow/ColumnDef`, `CachedTableRow`, and `TableUiCache` store revision-based display data; File Structure, Text Preview, and DistanceResolution structures retain layout, selection, and parser-confirmed boundaries.
- **Settings and runtime state**: `TextureImage` manages D3D background textures; logs, window visibility, 2D/3D views, `UserSettings`, recent maps, and background history map to INI fields. `ScenarioRoutePickState` holds candidate selection/open options; `ScenarioPreview` and its disk baseline hold editable Scenario drafts.
- **Edit state machine**: field constraints, inspector sessions, pending changes, preview snapshots, Repeater drafts, NewElement templates/wizards, delete modes, distance workflows, and editable-list drafts distinguish unapplied UI drafts, applied working copies, and saved disk state.
- **`App` class**: declares window rendering, async loading, snapshot conversion, geometry/scene rebuilding, tables/navigation, edit/save/reload, dialogs, settings, backgrounds, 2D/3D interaction, and caches. Its member layout defines the shared application state across GUI `.cpp` files.

#### `src/main_window/maploader_runtime.cpp`

- `KME_MAPLOADER_FUNCTIONS` is the symbol inventory. `MaploaderRuntime` loads `maploader.dll` from `runtime_paths::dll_path()`, resolves all functions including the sole load entry point `kv_load_map_ex()`, and checks `kv_api_version()==KV_MAPLOADER_API_VERSION`. Failures include Win32 error text.
- Global `kv_*` functions call cached DLL pointers through `ensure_loaded()`, returning failure values and GUI-queryable diagnostics on error. GUI code uses public-header function names with runtime DLL loading.

#### `src/main_window/runtime_paths.h` and `src/main_window/runtime_paths.cpp`

- `executable_directory()` calls `GetModuleFileNameW()` once and caches the EXE directory; `dll_directory()` selects its `bin` subdirectory; `settings_directory()` creates and returns the `settings` subdirectory.
- `dll_path()` builds DLL paths; `load_dll()` loads dependencies from `bin` with restricted search flags and can return Win32 error codes.

#### `src/main_window/app_settings.h` and `src/main_window/app_settings.cpp`

- The header exposes default INI paths, internal settings loading with explicit paths, settings/history I/O, deferred ImGui layout saving, and runtime style application.
- Initial helpers normalize storage paths and recent-map keys/display names, and clamp fonts, controls, markers, line widths, scene distances, gizmos, and instance-warning thresholds. Color helpers handle hexadecimal serialization, palettes, alpha, and blending.
- Language, bool, 2D-mode, and grid-mode conversions define INI value grammars. `save_user_settings()` writes canonical keys under `General`, `WindowVisibility`, `View2D`, and `View3D`; `load_user_settings()` accepts exact sections, keys, and value syntax, using defaults for unknown or invalid entries. Save operations rewrite existing files.
- `load_imgui_layout()`, `save_imgui_layout()`, `save_imgui_layout_if_requested()`, and the pending flag manage explicit/deferred `imgui.ini` persistence.
- `load_history_state()` reads recent maps and eight path/background fields under `[Recent]`/`[MapN]`, plus creator-message preferences under `[CreatorMessages]`/`[CreatorMessageN]`. Recent paths are normalized, deduplicated, and capped at ten; `save_history_entries()` and `save_history_state()` write canonical format with locale-independent numbers.
- `apply_ui_font_size()`, `apply_ui_theme_color()`, `apply_ui_component_size()`, and `apply_ui_settings()` map persisted settings to ImGui style, adjusting rounding and sizes for DPI/viewports.

#### `src/main_window/gui_kme.cpp` and its companion modules

- `gui_kme.cpp` manages `App` construction, destruction, and log callbacks; `kme.h` declares shared EXE state and cross-translation-unit interfaces.
- `win32_dx11_bootstrap.cpp` owns the D3D11 device and render targets, `WndProc()`, the window message loop, and `main()`.
- `gui_common_utils.cpp` centralizes fonts, theme/log colors, encoding conversion, numeric formatting, paths, and distance-jump controls; `background_image.cpp` owns WIC decoding, background texture rebuilding, and background persistence in history.
- `map_snapshot_hydration.cpp` builds `MapModel`, row metadata, Station edit IDs, speed-limit caches, and transition links from typed snapshots, and copies Scenario snapshots for both loading and saving; `map_load_pipeline.cpp` handles Map/Scenario detection, Scenario draft baselines, Route candidates, asynchronous map loading, entry history, result application, metadata merging, load timing, and geometry regeneration.
- `edit_ledger.cpp` handles typed batches and reports, ledger synchronization, local previews, deletion, Save, Revert, and closing; `distance_resolution_workflow.cpp` handles distance-resolution requests and resuming Apply.
- `edit_benchmark.cpp` provides the separate Debug edit benchmark: real inputs are used for in-memory Apply/Delete/Revert with byte protection; Save runs on regular-file copies in an exclusive temporary directory that preserves relative dependencies. It also checks refresh coalescing, rollback, and stage-timing contracts.
- `element_inspector_data.cpp` manages opening and locating the Inspector, field and scene edit data, and Apply; `element_inspector_render.cpp` renders Inspector fields, optional insertion parameters, and variable-length Repeater/Section controls.
- `editable_list_drafts.cpp` manages resource-list drafts; `new_element_wizard.cpp` owns new-element templates, the wizard, structured insertion, and the state and rendering of the New File wizard; `headless_entrypoints.cpp` reuses production App workflows for new-element, resource-list replacement/insertion, New File wizard, and Scenario creation contracts.
- `app_dialogs.cpp` owns file dialogs, Scenario Route candidate selection, new Scenario file content, and other modal dialogs; `element_inspector_data.cpp` handles deferred new-file requests, exclusive creation, and opening the result; `ui_elements.cpp` owns the dockspace, menus, toolbar, status bar, console, shortcuts, and settings projection; `scene_preview_lifecycle.cpp` owns scene/model preview startup, shutdown, rebuilding, visibility, and window rendering.

#### `src/main_window/file_structure_diagram.cpp`

- Layout groups nodes by Include depth and caches node sizes, connections, bounds, and revision. `file_structure_layout_is_current()` checks the cache; `rebuild_file_structure_layout()` rebuilds it when the source structure, font, or dimensions change.
- `open_parent_directory_in_explorer()` opens the physical directory through ShellExecute. `render_source_file_context_menu()` supplies the shared Open Directory and Source Preview actions for the diagram and Inspector; in Edit mode, Include nodes also offer deferred Replace File and Unreference requests.
- `App::render_file_structure_window()` draws the pannable canvas, hierarchy connections, main-file/Include nodes, hover tooltips, and context menus, and passes the selected node to the working-copy Text Preview.

#### `src/main_window/text_preview.cpp`

- `build_text_preview_lines()` builds an index of line-start byte offsets; `decode_preview_bytes()` provides read-only decoding for files without parser confirmation. Normal map/list previews use `kv_get_source_text()` first, so they can display the working copy after in-memory Apply.
- Boundary range/gap/EOF functions locate `DistanceResolutionBoundary` entries by line; marker style/render functions show parser-confirmed insertion points between source lines, and `utf8_byte_for_source_column()` converts source columns to ImGui text-selection byte offsets.
- `open_text_preview()`, `refresh_text_preview_from_working_copy()`, and `refresh_text_preview_after_map_load()` manage ordinary previews; `open_text_preview_for_distance_resolution()` supplies candidate boundaries and the target statement location.
- `render_text_preview_window()` draws line numbers, read-only UTF-8 text, selection, source locations, and boundary buttons. After the user selects a boundary token from the report, the main edit state machine retries the operation.

#### `src/main_window/touch_input.h` and `src/main_window/touch_input.cpp`

- `TouchFrame` in the header collects each frame's tap, long-press, scroll, and pinch events, including `PinchAxis`; public functions handle Win32 messages, per-frame state, region consumption, popup gestures, and test injection.
- `ActiveTouch`, `PairState`, and `TouchManager` track pointer IDs, press positions and times, movement thresholds, two-finger centers, and scale. Message handling recognizes down/update/up/capture-lost events; long press and scroll are mutually exclusive, and pinch maps distance changes to axis scaling.
- `new_frame()` publishes and clears transient events, `consume_*` marks gestures as consumed, and `apply_touch_scroll_to_hovered_window()` maps them to ImGui scrolling. `debug_*` validates the state machine with a controlled clock and synthetic touch points.

#### `src/main_window/map_marker_visuals.cpp`

- Primitive builders `append_polyline/polygon/circle/glyph/sampled_arc()` write normalized geometry into a recipe; `*_recipe()` functions for station, curve, gradient, speed-limit, beacon, pretrain, sound/noise, background, adhesion, cab light, fog, and other-track markers define the shared icon shapes.
- `map_marker_theme_color()` returns a theme color by visual kind; `map_marker_role_color()` combines fill/outline/accent/text roles with the theme; `map_marker_icon_recipe()` selects a recipe by kind and variant.
- `draw_map_marker_icon()` uses `transform_icon_point()` to scale, rotate, and translate normalized coordinates onto the 2D screen, then draws each primitive with ImDrawList. The 3D code uses the same recipe to generate billboard vertices.

### 2D views and plots

#### `src/canvas2d/canvas2D.cpp`

- **Queries and view data:** `nearest_own_index()`, `interp_own_z()`, `track_info_at()`, `speed_at()`, and `curve_sections()` sample values for measurements and overlays. `build_plan_data()` combines own/other tracks, stations, speed limits, and markers; `current_plan_data()` caches the result by model/geometry/visibility revision. Profile data uses the same build/current split.
- **View controls and rendering:** Measurement, focus, coordinate conversion, and `jump_to_distance()` share navigation. `render_plan_canvas()` coordinates mouse/touch input, visible-distance calculation, and drawing the background, grid, tracks, Repeaters, stations, markers, labels, 3D position, and focus.

#### `src/canvas2d/canvas2d_view_state.h` and `src/canvas2d/canvas2d_view_state.cpp`

- The `App`-owned `View2D` stores the view center, scale, rotation, fit, and drag state, and provides world/screen conversion, panning by screen delta, and fitting to bounds.

#### `src/canvas2d/canvas2d_marker_cache.h` and `src/canvas2d/canvas2d_marker_cache.cpp`

- Matrix row/sample/lower/upper-bound functions sample own/other-track matrices by distance; local-offset functions position markers. Repeater LOD, bounds, and chunk construction define continuous scenery coverage.
- `rebuild_speed_limit_marker_overlay_cache()` and `rebuild_marker_overlay_cache()` build plan markers, Repeater segments, other-train paths, and visibility indices from edit IDs, source rows, and track positions. `App` owns the caches and their invalidation.

#### `src/canvas2d/canvas2d_interaction.h` and `src/canvas2d/canvas2d_interaction.cpp`

- Measurement hit testing caches a spatial grid by plan generation and scale, with exhaustive search for small datasets; marker hit testing selects targets by screen radius, row order, and overlap priority.
- Context-target collection, `render_plan_marker_context_menu()`, and `plan_context_source_for()` provide table navigation, Properties/Edit, Delete, and source locations for each marker kind.

#### `src/canvas2d/canvas2d_background.h` and `src/canvas2d/canvas2d_background.cpp`

- The background module handles world/UV conversion and screen-quad drawing, and computes scale, rotation, and translation from two station coordinates and two image points; the `App` wrapper saves background history.

#### `src/canvas2d/canvas2d_primitives.h` and `src/canvas2d/canvas2d_primitives.cpp`

- `PlanScreenTransform` converts model/plan/screen coordinates; polyline construction, range clipping, and Repeater chunk/overview LOD select visible geometry.
- Grid, scale-bar, triangle, diamond, signal, pretrain, direction-arrow, and text functions wrap low-level ImDrawList drawing.

#### `src/canvas2d/profile_plots.cpp`

- Helpers draw vector curves, titles/units, axis-edge masks, left/right radius signs, bottom-anchored labels, and vertical profile markers; RAII classes temporarily override ImPlot fit-button and wheel-zoom behavior.
- Touch zoom converts `TouchFrame` into X or XY axis limits and uses `preserved_plot_span()` to retain a reasonable span when data is empty or rebuilt.
- `render_profile_plot()` draws distance/elevation, gradient fills and labels, stations, speed limits, and editable curve/gradient markers, and handles shared focus, double-click measurement, hover, and context actions.
- `render_radius_plot()` draws distance/curve radius, separates left and right curves, and reuses transition-marker linkage. `render_plots()` allocates window space for the current 2D mode and shares cached `ProfileData`.

### 3D rendering and scene construction

#### 3D preview modules

`Canvas3D::Impl` owns preview state, workers, GPU resources, and caches. Private headers declare state and interfaces and retain inline math and Repeater visitor templates; CMake compiles each functional `.cpp` separately.

| Module | Responsibility |
| --- | --- |
| `canvas3D.cpp`, `canvas3d_impl.h` | Thin public delegation and shared private method/state declarations |
| `canvas3d_math.h`, `canvas3d_types.h` | Inline vector/matrix math, CPU/GPU records, shared constants, and predicates |
| `canvas3d_scene_data.cpp/.h` | Typed map/scene conversion, route values and stations, marker metadata, fog input conversion, and draw distance |
| `scene_fog.cpp/.h`, `scene_shader_source.h` | CPU fog-keyframe construction/sampling and scene HLSL; `route_value_sampling_contract` validates the CPU logic and shader compilation |
| `canvas3d_scene_lifecycle.cpp` | Scene replacement, dynamic/map/station refresh, visibility and settings, model requests, and resource lifetime |
| `canvas3d_model_loader.cpp/.h` | Model-loader v2 client, WIC textures and caches, CPU model workers, upload queues, and diagnostics |
| `canvas3d_put_between.cpp/.h` | Source-model preparation and deformation, asynchronous PutBetween preview, and publication after sequence checks |
| `canvas3d_model_preview.cpp` | Single-model loading, resource cleanup, and interactive preview |
| `canvas3d_d3d_resources.cpp` | HLSL, shader pipelines, depth/blend/rasterizer states, render targets, and instance buffers |
| `canvas3d_scene_geometry.cpp/.h` | Scene/track chunks, track placement frames, and Repeater instances and caches |
| `canvas3d_scene_markers.cpp` | Marker vertices, text and icons, font caches, visible indices, and marker drawing |
| `canvas3d_scene_camera.cpp` | Track sampling, camera movement and jumps, and focus-target state |
| `canvas3d_scene_edit.cpp`, `canvas3d_scene_gizmo.cpp` | Placement-preview updates and invalidation; gizmo projection, hit testing, dragging, and drawing |
| `canvas3d_scene_render.cpp`, `canvas3d_scene_ui.cpp` | Render passes, picking/highlights, and visible instances; ImGui orchestration, overlays, context menus, and deferred actions |
| `tests/scene_render_contract.cpp`, `tests/scene_loader_contract.cpp` | Debug rendering, cache, pixel, and picking contracts, plus model-loader ownership and failure contracts |

#### Rendering and interaction

- **Track sampling:** `scene_track_sampling.cpp/.h` provides ordinary track sampling, camera extrapolation before the track start, and camera distance bounds. Extrapolation before the start is specific to the camera.
- **Scene data:** Vector, matrix, and bounding-box functions build world transforms; CPU/GPU records manage models, materials, textures, chunk instances, markers, pick targets, and reverse navigation.
- **Model loading:** `ModelLoaderClient` loads the v2 API from `bin/model_loader.dll` and pairs allocations with their frees. Workers copy CPU model data; the main thread creates D3D resources through `upload_pending_scene_models()`. The lifecycle module coordinates cancellation, joining, wakeups, and uploads.
- **Resource and scene lifetimes:** `load_model()`, `upload_model()`, and `reload_model()` manage single-model preview; `load_scene()`, dynamic/map/station refresh, and `clear_scene()` manage scene replacement and refresh. Pipelines are created on demand, textures reuse caches, instance buffers grow as needed, and matching release functions free resources.
- **Chunks and placement:** `build_scene_chunks()` organizes Structure, Signal, Repeater, and track geometry by distance. Track sampling, cant frames, `make_track_placement_frame()`, and `make_track_world()` convert BVE placement parameters to world matrices. Scenes use camera-relative coordinates and reversed-Z depth.
- **Markers and picking:** Shared 2D icon recipes generate 3D billboards; visibility changes rebuild marker indices. The pick pass writes object/marker IDs and reads back one pixel; highlight masks and outline compositing draw hovered and selected outlines.
- **Route information:** `scene_route_overlay.cpp/.h` reuses route-value sampling to format radius, cant, gradient, speed limit, Section signal speed, and the next station. `Curve.Interpolate` intervals show evaluated radius/cant at both ends, direction arrows, and triangle separators; when both displayed radii are zero, they use the localized Straight label.
- **Frame orchestration:** `render_scene_preview()` in `canvas3d_scene_ui.cpp` handles asynchronous uploads, camera, gizmos, visible instances, drawing, picking, highlights, and context menus, then returns deferred navigation/edit/delete actions. Modules such as `canvas3d_scene_render.cpp` execute the render passes.

Gizmos generate `Canvas3DPlacementDragUpdate` for each edit target: ordinary placement coordinates are truncated to millimeters; Sound3D X/Y update source offsets while Z updates distance in whole meters; explicit Repeater End Z updates the segment end. `Structure.PutBetween` uses a Z axis along the own track's forward direction and snaps distance to whole meters. Its worker coalesces the latest target, reuses track samples by longitudinal model slice, and publishes results to reusable dynamic vertex buffers.

#### Frame rate and stage timing

The displayed FPS measures scene-render call frequency. `canvas3d_scene_ui.cpp` accumulates positive intervals of at most `0.1` seconds and publishes `interval_count / active_seconds` after active time reaches `0.2` seconds. Long gaps discard the unfinished window and retain the last reading; reset clears the anchor, accumulators, and reading. `win32_dx11_bootstrap.cpp` owns the main loop's Present and render wakeups.

`scene_frame_profile.h` provides Debug stage timing. Rendering and loader contracts are compiled into the EXE under `NDEBUG` guards and run through the scene benchmark and loader headless commands.

#### Fog

`scene_fog` builds keyframes from hydrated `Fog` and `Legacy.Fog` rows and rebuilds them on scene refresh:

- Events are processed by distance and global source order. A Legacy statement at a nonzero distance expands into an old-state node at its original distance and a target node at `distance + 25`, followed by a stable sort.
- At a shared distance, the first node is the previous segment's interpolation target and the last node takes effect at that distance. Omitted Fog parameters inherit from the farthest node already inserted, including future Legacy targets.
- Sampling uses binary search. Within one mode, density, RGB, and start/end interpolate in double precision; across modes, the previous node applies. With no fog events, fog stays off; initially omitted values use defaults.
- Input conversion filters invalid distances and interval values, clamps colors to 0–1, and limits shader distances to a safe float range.

The scene pixel shader uses camera-space depth (projection `w`) to calculate exponential fog or linear fog `clamp((end-depth)/(end-start), 0, 1)`. A zero-width interval steps at `end`; finite reversed intervals use the same formula. Fog affects the background, models, and tracks; UI, markers, and highlight masks are drawn separately. See Microsoft's [fog formulas](https://learn.microsoft.com/en-us/windows/win32/direct3d9/fog-formulas) and [pixel fog depth](https://learn.microsoft.com/en-us/windows/win32/direct3d9/pixel-fog) for the formula and depth basis.

`src/canvas3d/tests/scene_fog_tests.cpp` is part of `route_value_sampling_contract`. It covers Legacy/mixed modes, adjacent and same-distance boundaries, inheritance, toggling, and numeric guards, and uses `D3DCompile` to compile the scene's shared vertex and normal/fog pixel-shader entry points.

### Data tables and cross-view navigation

The following files are under `src/table/` and compiled separately by CMake. `App` owns `TableUiCache`; `datatable_cache.cpp` builds it locally and publishes it by move, while `table_navigation.cpp` resets state after invalidation and handles cross-view navigation.

| Module | Responsibility |
| --- | --- |
| `datatable.cpp` | Shared cell/numeric conversion, basic table UI helpers, and semantic annotations for scene track keys |
| `datatable_internal.h` | Private inline column definitions and internal action/view/helper declarations; windows, cache construction, and specialized templates live in their respective `.cpp` files |
| `datatable_cache.cpp` | Full `TableUiCache` hydration, dynamic Section/Signal columns, merged Repeater display rows, column-width measurement, and runtime speed-limit cache refresh |
| `datatable_find.cpp` | Case-insensitive find/reset/step, unused-key search, and dedicated Structure/Signal/Sound App search APIs |
| `datatable_resource_lists.cpp` | Shared editable-list rendering and Station, Structure model, Signal aspect, Sound, and Sound3D resource-list windows |
| `datatable_route_tables.cpp` | Other-track, Station.Put, Structure, other-train, Repeater, Signal.Put, Section, and Variable windows |
| `datatable_effect_tables.cpp` | Beacon, Irregularity, sound/noise, Background, Adhesion, CabIlluminance, Fog, Lighting, DrawDistance, and SpeedLimit windows |
| `datatable_scenario.cpp` | Scenario File table and path/candidate editing UI |
| `datatable_benchmark.cpp` | Debug-only real-map cache-rebuild, warm-hit, and no-input table-frame benchmarks, with cache-summary and source-integrity checks |
| `table_navigation.cpp` | State reset after cache invalidation and table/plan/scene navigation |

Map tables read `TableUiCache` and visibility state, and call shared `App` methods for draft changes, selection, and navigation. The Scenario table uses a separate Scenario draft.

#### `src/table/table_navigation.cpp`

- `invalidate_table_cache()` clears revisions and derived rows; `reset_marker_visibility()` and `sync_marker_visibility_sizes()` keep 2D marker flags aligned with model row counts.
- Structure, Repeater, and Signal `locate_*_on_plan/in_list/in_scene_preview()` functions update plan focus, table highlights/window visibility, and Canvas3D jumps. Repeater navigation also handles End and change boundaries.
- `locate_standard_marker_on_plan()` and `locate_standard_marker_in_list()` implement the paired navigation functions shared by Beacon, Section, Irregularity, Sound/Noise, Background, Adhesion, CabIlluminance, Fog, DrawDistance, SpeedLimit, and related markers.
- Locating an other-train stop opens its group and stop row. `locate_scene_marker_row_in_list()` and `locate_scene_marker_row_in_scene_preview()` map Canvas3D marker enums to the correct table/source row; `can_locate_scene_preview_row()` checks scene, index, and visibility conditions.

### Debug entry points and contract tests

#### `src/main_window/debug_headless.h`

- Each `*Options` structure corresponds to a command-line mode: Map/Scenario loading; plan/scene/open/edit/table-cache benchmarks; scene loader and camera transfer; diagnostics popup; source anchor; roundtrip; distance/own-track/other-track; Station/resource lists; Repeater; Section; Include; new files/elements; table find; touch; and settings persistence.
- The header declares Debug-only `run_debug_headless_*()` entry points; option structures connect command-line parsing in `main()` to the test implementations.

#### `src/main_window/debug_headless.cpp`

- **Shared utilities and options:** COM RAII, UTF paths, output files, timing statistics, hashes, snapshot matrix summaries, log capture, and fixture lookup provide deterministic headless output. This file also parses each mode's options. Edit benchmarks are implemented separately in `edit_benchmark.cpp`, and table cache/drawing benchmarks in `src/table/datatable_benchmark.cpp`.
- **Load, geometry, and scene checks:** Basic Map loading validates snapshot structure and matrices; Scenario loading validates v2 snapshots, edit roundtrips, candidate selection, and the resolved map. Plan/scene benchmarks repeatedly build caches and report stage timings, counts, and hashes; camera transfer checks the pose across rebuilds; scene debugging reads pixels and fog state to validate rendering.
- **`typed_edit_headless`:** `Field/Change/Batch/Report` are RAII wrappers for the public edit ABI, managing string-view lifetimes, copies of dry-run/apply/commit reports, and failure information.
- **Distance/own-track/other-track batches:** `distance_batch_headless` uses handles, edit targets, boundary choices, and reports to drive multi-file/Include/variable cases; own/other-track modes check method and argument-shape preservation, Apply/Reset/Commit, and geometry changes.
- **List and linked edits:** `station_list_edit_headless` creates temporary CSV fixtures and checks editing, clearing, reordering, deletion, and original encoding; `repeater_batch_headless` checks chain updates, trim conversion, and atomic deletion; `section_edit_batch_headless` checks dynamic argument insertion/removal, null/expression preservation, and commit.
- **Insertion and source anchors:** Insert modes check allowed templates, distance-block selection, and rejection of unknown fields; source-anchor/roundtrip modes check physical files, Include stacks, line/column/span, stable IDs, and consistency after saving and reloading.
- **UI and persistence checks:** Table find checks case handling, exact/step/unused state; touch uses synthetic input to check tap, long press, scroll, pinch, and consumption. Settings persistence uses a temporary directory to check canonical formats, defaults for invalid entries, file bytes after loading, and History boundaries. Each entry point reports PASS/FAIL.

#### `src/main_window/edit_benchmark.cpp`

- `App::run_debug_headless_edit_benchmark()` uses production App paths for Inspector Apply, deferred Delete, Save, Revert, refresh, and the first scene frame, reporting inclusive timings and aggregate statistics by operation. The input route and all loaded physical source files are protected byte for byte. Save runs on regular-file copies under an exclusive temporary root; copying rejects Windows reparse points and destinations outside that root.
- Refresh fixtures check coalescing of full/partial hydration, one-time table/plan cache invalidation, recovery from failed Apply, and empty Save. `NDEBUG` guards restrict this implementation to Debug.

#### `src/main_window/headless_entrypoints.cpp`

- These contracts reuse production `App` edit state and dialog-request handling to validate new-element Apply/Inspector/Delete; resource-list replacement and row insertion; creation or reuse of Maps, Scenarios, and the five resource-list types; reference commit, reload, and cleanup; and the Scenario lifecycle.
- New-file validation requires a nonexistent target under `tests/` and removes created files afterward. Resource-list insertion and replacement validate in-memory Apply; replacement uses the production file picker.

#### `src/maploader/tests/typed_snapshot_tests.cpp`

- `TempFixture` creates and removes temporary map/list files; encoding helpers generate UTF-8/BOM, UTF-16, and CP932 inputs; `MapHandle` calls `kv_free()` through RAII; assertions such as `CHECK_ARRAY` check both counts and null-pointer contracts.
- Snapshot tests traverse all root arrays, strings/spans, metadata, capabilities, revisions, and stable edit IDs. They check that the Windows export table retains `kv_load_map_ex()` as the sole load entry point, and include focused checks for Signal glare/variable keys, resource Load, Include, and scene snapshots.
- Geometry tests compare route length, plan projection, elevation, and event distances using gradient/curve fixtures.
- Wrappers such as `UpdateBatch` and `RepeaterTrimBatch` construct typed edits. Edit tests cover dry run, in-memory Apply/Reset, direct Apply, Commit, concurrency hashes, distance resolution, method/argument shapes, semantic guards, encoding, and transaction rollback.
- Diagnostics tests load local `tests/` fixtures and check missing files, invalid syntax, duplicate Load/Enable, unpaired transitions, unknown keys, and log/last-error text. `main()` selects test groups through `snapshot`, `geometry`, `edit`, `diagnostics`, and focused options, then returns a process status for CTest.

#### `src/main_window/tests/route_value_sampling_tests.cpp`

- CPU contracts cover interpolation endpoint ownership, omitted-value inheritance, BeginTransition, invalid numbers, positive/negative/zero radius formatting in 3D route information, and the Straight label for zero-to-zero interpolation.

### Static call-chain summary

```text
main / App
  -> kv_* forwarders in maploader_runtime
     -> C ABI exception boundary in maploader.cpp
        -> scenario_route -> KvScenarioSnapshot / Route candidates
        -> parse_map_context -> Parser -> MapContext
        -> generate_geometry -> Matrix / scene control points
        -> MapSnapshotBuilder -> KvMapSnapshot
        -> build_edit_report -> patched-source reparse and semantic validation -> memory overrides or transactional writes

App / MapModel
  -> datatable, canvas2D, profile_plots build 2D views cached by revision
  -> Canvas3DScene -> Canvas3D::Impl -> D3D11 chunks, asynchronous models, picking, and gizmos
  -> inspector / inline draft -> KvEditBatch -> maploader source-first edit pipeline
```

`MapContext` owns parser and snapshot storage; the GUI copies the data it needs into `MapModel`. Canvases manage derived caches by revision. Standalone strings and model arrays returned by a DLL are freed by that DLL's matching free functions.

## Core engineering rules

### C++ and ABI

- Use C++17, favoring RAII, standard containers, `std::filesystem`, and helpers with one clear responsibility.
- Keep `UNICODE`, `_UNICODE`, `NOMINMAX`, and `WIN32_LEAN_AND_MEAN` defined.
- Public C ABIs use fixed-width POD types and explicit memory ownership. Catch exceptions at the boundary and free DLL-allocated memory with the matching function.
- The EXE requires exact matches for maploader API v13, map snapshot v9, and model-loader API v2. `kv_load_map_ex()` is the sole map-loading entry point; `KvScenarioSnapshot` and `KvScenarioEditDocument` use v2. Map, scene-geometry, edit-target, and report contracts manage their versions and structure sizes separately.
- ABI inputs are call-scoped views. Nested snapshots are handle-owned and become invalid on geometry rebuild, edit, reset, reparse, or free as specified in the public headers. Scenario snapshots are allocated separately and freed with `kv_free_scenario_snapshot()`.
- Public ABI changes require an explicit version/structure-size policy, synchronized EXE/DLL/caller updates, and documented ownership and lifetimes.

### Parsing, geometry, and source fidelity

Support BVE Map 2.0+, existing legacy syntax, Include, variables, predefined `distance`, math functions, comments, and UTF-8/BOM, UTF-16LE/BE, and CP932/Shift_JIS input. Parsing and presets use official, general BVE syntax.

Each Map context starts at distance zero with a local distance expression. Include inherits ordinary variables and source identity; merging preserves the parent's distance and expression while incorporating child events, control points, and variable writes. Parallel preparse results are checked against variable dependencies and reparsed when needed.

Editable rows retain physical paths, Include stacks, source spans, original statements/arguments, evaluated values, distance expressions, parse order, and stable IDs; `KvMapSnapshot` carries them as strongly typed views. Writeback preserves the original encoding, BOM, and line endings, and blocks new characters that the original encoding cannot represent.

When AI coding tools change BVE map, list, or Scenario reading, validation, typed representation, editing, creation, or writeback, use the matching subsystem skill together with [`komapedit-bve-format-compliance`](../.agents/skills/komapedit-bve-format-compliance/SKILL.md). Before implementation, follow the skill to check the dated official-page cache, read the affected pages, and complete the compliance matrix.

#### Scenario

`scenario_route.cpp/.h` manages these workflows:

- **Read and preview:** Detect the file type, validate the `BveTs Scenario 2.00` header, decode the declared encoding, process `#`/`;` comments, and retain the last occurrence of each of the eight official fields, relative paths, weights, source hash, and field-presence bits. Snapshots can preview Scenarios with a missing Route field or missing target files.
- **Open a map:** `kv_resolve_scenario_routes()` checks Route candidates and target files, then passes them to the map loader for validation. Vehicle data is used for preview. The GUI retains the Scenario preview if Route resolution or map loading fails.
- **Save a draft:** The GUI commits Scenarios directly through Save. Existing Route/Vehicle fields retain at least one candidate, written in draft order; candidate paths must be nonempty and avoid reserved syntax characters, and weights must be finite and positive. Equal candidate counts are patched item by item; count changes rewrite that field's candidate value. Source-hash checks and full reparsing precede transactional writes, which preserve encoding, BOM, and line endings.
- **Create a file:** The New File wizard uses `build_new_scenario_file_content()` to generate UTF-8/CRLF in official key order, creates the file exclusively, and reparses it for validation. Route/Vehicle start as single unweighted paths; multiple candidates and weights are edited in the Scenario File tab.

#### Curve parameters and lighting

`Curve.SetGauge(value)`, `Curve.SetCenter(x)`, `Curve.SetFunction(id)`, and legacy `Curve.Gauge(value)` share `CurveEditRow` → `KvCurveRow` → `MapModel::curve_rows` and generate own-track geometry-state events. Updates retain the original method and source identity; the three creation templates use current methods with defaults of `1.067`, `0`, and `0`, respectively. Loading, updating, and inserting `SetFunction` require one numeric argument that evaluates to `0` or `1`.

`Light.Ambient`, `Light.Diffuse`, and `Light.Direction` support table parameter previews and source editing through the `light.ambient`, `light.diffuse`, and `light.direction` targets and `KvLightColorRow`/`KvLightDirectionRow`. All three forms remain visible; parameters become editable when edit conditions are met, and Apply, Delete, and New depend on target and draft state. Apply combines changed forms, Delete uses deferred requests, and the wizard creates statements at distance `0` in the selected source file.

After merging the root map and Includes, each lighting type can retain at most one syntactically valid declaration. Duplicates invalidate all conflicting rows of that type and produce English warnings with every physical location. Ambient/Diffuse RGB values are limited to `[0, 1]`, and Direction requires distance `0`. Valid rows enter the snapshot; updates preserve unchanged argument expressions and undergo full semantic validation.

### Editing model

maploader owns sources and edit identity; the GUI operates on working copies through typed requests. Preview/Edit hydrate data according to capability bits. Pending inline-list drafts must be applied in their table before Save.

| Operation | Responsibility |
| --- | --- |
| `kv_edit_dry_run_typed()` | Build and validate a patch report |
| `kv_edit_apply_to_memory_typed()` / GUI Apply | Update the in-memory working copy and preview |
| `kv_edit_apply_typed()` | Write directly through a transaction |
| `kv_edit_commit_typed()` / GUI Save | Commit the validated working copy |
| `kv_edit_reset_memory()` / GUI Revert | Discard memory overrides and restore the disk baseline |
| GUI Reload | Reread disk after confirming unsaved changes |

`sourceHash` identifies the working copy; `expectedSourceHash` remains the disk concurrency baseline across repeated Apply/Delete operations. Before Apply or Save, a full reparse checks each target value, non-target elements, and final variable bindings. A valid edit may change the final `distance`.

#### Distance planning and source patches

Distance moves and insertions share the parser's boundary planner, grouped by physical file, Include invocation, distance segment, and target distance. They preserve statement order, comments, and empty distance blocks. Planning covers implicit initial blocks, anchor gaps, final blocks, and EOF. An unambiguous final segment can extend in either increasing or decreasing direction; moves must originate in that segment, while insertion prefers an existing unique location. Candidate enumeration and token lookup share the planner, including the trailing adjacent gap of a turning segment.

Entry/exit environments for each file and Include instance record trailing assignments and variable writes and are rebuilt with edit metadata. Environment checks generate `evaluation_Environment_Requires_Boundary` candidates for recoverable issues and block edits with no viable location; reasons such as `ambiguous_Source_Section` accompany the report. After candidate filtering, the selected plan undergoes whole-map semantic validation.

Initial GUI handling and cache reuse share action selection, prioritizing blocking errors. Failed retries are keyed by working-copy hash, structured changes, and all manual choices in the batch. Distance assignments must be finite; finite negative distances are allowed. Unchanged object-key expressions, comments, and line breaks retain their original bytes. Section `values.N` must refer to an existing argument; length changes explicitly supply `values.count`.

#### Elements and resource lists

- **Method conversion:** Ordinary edits preserve method and argument shape. Inspector coordinate-offset buttons explicitly switch `Put`/`Put0` and `Begin`/`Begin0`; discarding nonzero offsets, converting short-form `Signal.Put`, and trimming Repeaters use their respective confirmation flows.
- **Legacy.Fog:** `legacyFog.change` supports update, delete, and insert with `distance/start/end/red/green/blue` fields. All five statement arguments are required finite numbers; negative/equal values, reversed intervals, and finite RGB outside the nominal range are accepted. Unchanged expressions are preserved. Baselines and edits share the semantic writer; Include replacement protects non-target values using subtree-exclusion rules, and refresh updates tables, scene fog, and markers.
- **Resource-list rows:** Station, Structure, Signal, Sound, and Sound3D share inline drafts. Context menus insert rows above or below; Structure, Sound/Sound3D, and Station use 2, 3, and 13 CSV fields, respectively. Signal main rows start with 6 fields and retain adjusted widths afterward; main/glare pairs are inserted as a unit, with glare added explicitly by the user.
- **Signal columns:** Preserve trailing empty fields on physical lines. `KvSignalAspectRow::metadata.reserved` records the main row's structure-field count; remaining `structure_keys` belong to glare. Shape edits supply `mainStructureKeyCount`, `glareStructureKeyCount`, and every numbered field; cell edits preserve the existing shape. Each existing physical row retains at least one structure field, which may be empty. The main/glare boundary participates in semantic and dirty-state checks; column operations use the full actual width beyond the 509-column display limit.
- **Repeater linkage and rename:** All layers share `repeater_linkage` and transition linkage. Lifetimes are half-open intervals `[first Begin, End)`, ordered by distance, global order, and source row; the last event at a distance determines active state. Empty intervals retain source identity. Rename batches include every Begin/Begin0 and explicit End in the chain and pass overlapping-name and chain-ownership checks. Reparsing validates non-target boundaries; new zero-length segments emit Begin followed by End.
- **Repeater structure keys:** Hydration, Inspector, and scene code share `_structureKeys.count` and `_structureKeys.N` through `repeater_structure_keys()` and `set_repeater_structure_keys()`; joined `structureKeys` text is for display. Model arrays retain missing-entry positions and use the original list length for `k % N`; if an endpoint model is missing, camera navigation uses the placement geometry anchor.
- **Other-track rename:** One typed batch must contain every same-key `Track[trackKey].*` statement in the root map and all Includes. Key comparison preserves numeric/string types and ignores case. Rename is accepted when the batch is complete and the new key is unique across the map; other elements that reference the key undergo non-target-row validation.

### UI, tables, and rendering

- Preserve Dear ImGui docking layout, menu, and tool concepts. Keep ordinary UI text synchronized in Simplified Chinese, Traditional Chinese (Taiwan), Traditional Chinese (Hong Kong), English, and Japanese, with stable ImGui IDs across language changes.
- Use official English names or abbreviations for BVE parameter labels, such as `distance`, `trackKey`, `x`, and `ry`. Program diagnostics and headless output use English; the surrounding console UI supports all five languages.
- Preserve 2D pan/zoom/rotation/fit, measurement, grid, station jumps, and background alignment, plus 3D camera transfer, picking/highlights, visibility, markers, route information, and linked gizmo behavior.
- Cache tables by revision and preserve dynamic Section arguments and explicit `null`, variable order, and cross-view navigation. Repeater find uses ordered typed structure keys; before finding unused structures, commit the active Signal main/glare cell into its draft.
- Keep Assimp inside `model_loader.dll`; loading errors follow diagnostic and cleanup paths. Model bounds are computed in double precision; nonfinite positions and radii outside the public float range are rejected. Failed dynamic scene refresh restores Repeater chunks and cache totals.

#### Draft and view state

`App` retains original disk rows for pending updates/deletes of existing rows; full and partial hydration use this baseline. Failed Apply restores the previous state; successful Apply removes baselines for entries that have left the ledger, and Save/Revert clear completed entries. The Inspector tracks changes per field and removes only the matching change when a field returns to its original value. Other tracks retain visibility, color, and display range by exact key; explicit rename transfers state through stable IDs.

#### Settings and creator messages

Application preferences, recent-map/background and creator-message preferences, and ImGui layout are stored in `settings/settings.ini`, `settings/history.ini`, and `settings/imgui.ini`, respectively. Loaders accept the savers' exact sections, keys, and value grammars; unknown or invalid entries use defaults, and explicit saves rewrite existing files. Settings and layout are marked saved after a complete write and successful close. On failure, `App::service_pending_persistence` retries each on its own one-second deadline, including while the window is occluded or idle. ImGui request flags own the layout's dirty state.

Creator messages come from standalone `//--kme--message-from-creator:` comments, retain physical locations and Include identity, and are deduplicated by physical message before being exposed to Preview/Edit as `KvCreatorMessageRow`. `creator.message` edits accept raw `content`; header insertion uses the shared patch, reparse, and commit path. `creator_messages.cpp` manages drafts, the table, and the document-open popup. History uses `[CreatorMessages] count` and `[CreatorMessageN] path/has_messages/suppressed` to retain display preferences by main Map, including suppression while messages are temporarily empty.

#### Curve markers

`CurveGauge`, `CurveCenter`, and `CurveFunction` markers carry row indices and edit IDs. 2D draws white `CG`/`CC`/`CF` rectangles; 3D draws white two-line signs with the code and evaluated parameter, sharing picking and blue highlights. `[View2D]` keys `show_curve_gauge_markers`, `show_curve_center_markers`, and `show_curve_function_markers` default to off and control each marker's visibility.

`Curve.Interpolate` preserves its 0/1/2-argument shape and edit identity. Hydration links evaluated events by source-file index and global statement order, handling sorted events and repeated Includes at the same distance. After Edit metadata merges, 2D endpoints and 3D signs styled as `CurveCircularStart` use the same source row, provide Properties/Edit and deferred Delete, and show one marker per statement. Signs display evaluated radius/cant, with `Intpl. 0` for zero radius.

During Preview, plan hover uses the marker-array index; source selection and editing wait for source binding. 3D retains the `curve` category and context menu, enabling Edit/Delete when metadata is ready. The creation template shows the full `Curve.Interpolate(radius, cant);` signature and uses optional trailing arguments to generate all three forms.

### Performance

- Reuse reads, decoding, hashes, path resolution, snapshots, tables, markers, and geometry by input and revision.
- Keep large arrays contiguous where practical, control tight-loop and per-frame allocations, and avoid O(n²) traversal of large track arrays.
- Use asynchronous workflows with progress for long parsing and model loading. Give each cache a complete key, invalidation owner, and revision, with coverage for hits and invalidation.

`refresh_local_preview_after_edits()` coalesces Apply/rollback refreshes: full hydration subsumes partial track/list conversion, and full scene rebuild subsumes updates to the old scene. Single-instance and Repeater coordinate edits use stable-ID fast paths. Distance and physical-anchor indices are built on first use and reused within the batch.

After sorting source patches and checking overlaps, the builder appends original and replacement fragments in order and computes identity offsets from final positions. Reports retain descending edit order and 80 bytes of preview context on each side, including already-applied edits to the right. Save moves encoded bytes into transaction requests while retaining length, hash, write verification, and rollback data.

Canvas3D builds a placement-track index when replacing a scene, supporting own-track aliases, the first normalized other-track match, and a missing-key fallback. Repeaters place instances at `begin + k * interval`, restart at each Begin, and select models with `k % N` over the original structure list. Double-precision transforms are cached in chunks within the current window, with a shared limit of 65,536; excess instances are computed on demand. Edits invalidate old and new chunk ranges, scene/chunk replacement resets caches, and leaving the window releases them. Placement geometry is independent of track visibility.

Background images reuse textures for geometry-only changes when the requested brightness matches the uploaded value. Replacing the image invalidates the texture; successful upload updates the brightness record.

Edit timing uses the steady clock in `operation_timing.h`. GUI and maploader record inclusive `*_ms` and `*_count` in separate domains; GUI totals include DLL time. App timing extends through deferred Inspector processing and the required first scene frame; hidden or collapsed scenes end that wait immediately. `source.read_decode_hash` records source processing on the calling thread, parallel Include work contributes to `parse.include_join_merge` and `parse.syntax_diagnostics`, and model loading has separate asynchronous logs.

## Validation

Select checks by affected component and record build, contract-test, headless, and manual UI results separately. For disk writeback, compare after Save and reload; assess visual interactions such as rendering, menus, dialogs, and dragging manually.

`komapedit.exe` uses the GUI subsystem. To capture output in PowerShell, use `Start-Process -Wait -WindowStyle Hidden -PassThru` with `--headless-output`. Supply input paths explicitly; omitted paths in some own-track, other-track, distance, Repeater, and Section modes fall back to a developer's local route.

### Performance benchmarks

Use the same route, parameters, build type, and load profile for before/after comparisons. Run scene, table-cache, and edit benchmarks in three separate sequential processes each, with builds and other benchmarks paused. Disable profiling for sustained-frame acceptance.

#### Scene and loader

`--debug-headless-scene3d-bench` defaults to 300 fixed-camera frames, 25 m sampling, a 100 m rear/1200 m forward window, and a 16.67 ms CPU-frame p95 limit. `--interaction moving` advances 2 m per frame with normal end clamping. Timing starts after model loading and five warm-up frames; reports include the adapter, workload, slowest frames, and visible-instance fingerprints.

`--profile-stages` records nested CPU stages and asynchronous GPU timestamps. Queries are preallocated and read across frames; only valid, ready samples count. GPU intervals include command gaps, and CPU parent stages include their children. Stage data locates bottlenecks; whole-frame CPU p95 determines acceptance.

After timing, scene contracts compare double-precision instance fingerprints, pixels, and picking between direct computation and cached paths, covering camera movement/rotation, chunk jumps/returns, window changes, fog, and track guides. `--debug-headless-scene-loader-contract` uses temporary models and headless D3D to check:

- Repeater distance/model sequences, typed-list hydration, Include order, missing entries, zero-length segments, geometry jump anchors, tilt/span, invalid intervals, edit/restore invalidation, track replacement, and cache budgets.
- Model-copy and PutBetween-worker failures, cancellation, request coordination, balanced DLL allocations/frees, and same-path model reuse/reload.
- Finite and representable model bounds, cleanup on failure, and Repeater cache restoration after failed dynamic refresh.
- Background texture identity and pixels for unchanged input, geometry-only changes, brightness changes, image replacement, and upload failure/retry. A 1024×1024 image benchmark reports geometry-Apply median/p95 and texture replacement counts.

#### Plan and tables

`--debug-headless-plan-bench` defaults to `--interaction pan` and compares cached/direct curve, transition-curve, and Interpolate endpoints. Measurement modes compare hit results against exhaustive search: small point sets use linear scans, large sets a spatial grid. `measure-stationary` fixes the pointer; `measure-moving` follows a deterministic path.

Plan, scene, and own-track-edit share temporary Map/Include contracts for Interpolate's 0/1/2-argument forms, Apply/Delete/Reset, source identity, and unique editable markers. Real routes are checked against available targets; empty row, event, and marker sets must agree, and instance-dependent checks are reported as not applicable. Own-track-edit also checks Preview→Edit merging, including maps that select source files through `rand()`.

`--debug-headless-table-cache-bench` uses Edit metadata and an ImGui/ImPlot context with INI persistence disabled. After warm-up, it separately measures cold-cache rebuild, one warm-cache hit, and one frame drawing all tables, checking fingerprints for rows, cells, identity, dynamic headers, and widths. Source byte/hash checks run outside the timed interval. Defaults are 5 repeats (range 1–100) and 25 m sampling.

`--debug-headless-diagnostics-popup-bench` uses 100,000 mixed log entries to check concurrent ordering, snapshot-revision caching, and clipped rendering.

#### Editing

`--debug-headless-edit-bench` calls production Apply, deferred Delete, Save, Revert, and refresh paths. The input map must contain editable `Structure.Put`, `Repeater.Begin`, and `Curve.SetGauge`; it selects the first valid targets in source order and applies fixed coordinate/gauge increments. The input route is used for in-memory operations and byte/hash checks; Save uses regular-file copies in an exclusive temporary directory with relative dependencies preserved, path boundaries and reparse points checked, and cleanup afterward.

Defaults are `--scene off`, `--repeat 5` (1–100), and `--unit-distance 25`. With scenes enabled, the benchmark uses a 1260×680 canvas, a 100 m rear/1200 m forward window, and a camera at the route's minimum distance + 500 m, clamped through the normal API. Each repeat restores the copies and creates a new App; timing starts after initial model loading and includes the required first scene frame. Reports cover target identity, nested stages, overall median/p95/maximum, source checks, and refresh/rollback contracts. Run three processes with five repeats each for each 3D state.

### Command entry points

```bat
build\komapedit.exe --headless-load-map <map-path> --headless-output build\headless-load-map.txt
build\komapedit.exe --headless-load-scenario <scenario-path> [--scenario-index N] [--expect-no-map] [--scenario-edit-roundtrip] --headless-output build\headless-load-scenario.txt
build\komapedit.exe --debug-headless-scenario-lifecycle <scenario-path> [--scenario-index N] [--unit-distance M] --headless-output build\scenario-lifecycle.txt
build\komapedit.exe --debug-headless-plan-bench <map-path> --interaction pan|measure-stationary|measure-moving --headless-output build\headless-plan-bench.txt
build\komapedit.exe --debug-headless-open-bench <map-path> --repeat 3 --headless-output build\headless-open-bench.txt
build\komapedit.exe --debug-headless-table-cache-bench <map-path> --repeat 5 --unit-distance 25 --headless-output build\table-cache-bench.txt
build\komapedit.exe --debug-headless-scene3d-bench <map-path> --window-back-m 100 --window-forward-m 1200 [--interaction stationary|moving] [--profile-stages] --headless-output build\headless-scene3d-bench.txt
build\komapedit.exe --debug-headless-scene-loader-contract --headless-output build\scene-loader-contract.txt
build\komapedit.exe --debug-headless-diagnostics-popup-bench --headless-output build\diagnostics-popup-bench.txt
build\komapedit.exe --debug-headless-scene-camera-transfer <map-path> --headless-output build\scene-camera-transfer.txt
build\komapedit.exe --debug-headless-source-anchors <map-path> --headless-output build\source-anchors.txt
build\komapedit.exe --debug-headless-station-list-edit <map-path> --headless-output build\station-list-edit.txt
build\komapedit.exe --debug-headless-station-put-margin-edit <map-path> --headless-output build\station-put-margin-edit.txt
build\komapedit.exe --debug-headless-edit-roundtrip <map-path> --headless-output build\edit-roundtrip.txt
build\komapedit.exe --debug-headless-edit-bench <map-path> --scene off|on --repeat 5 --unit-distance 25 --headless-output build\edit-bench.txt
build\komapedit.exe --debug-headless-own-track-edit [map-path] --headless-output build\own-track-edit.txt
build\komapedit.exe --debug-headless-other-track-edit [map-path] [--commit] --headless-output build\other-track-edit.txt
build\komapedit.exe --debug-headless-distance-edit-batch [map-path] --headless-output build\distance-edit-batch.txt
build\komapedit.exe --debug-headless-auto-insert-diagnostics <fixture-directory> [--unit-distance M] --headless-output build\auto-insert-diagnostics.txt
build\komapedit.exe --debug-headless-repeater-edit-batch [map-path] --headless-output build\repeater-edit-batch.txt
build\komapedit.exe --debug-headless-repeater-key-edit <map-path> [--commit] --headless-output build\repeater-key-edit.txt
build\komapedit.exe --debug-headless-other-track-key-edit <map-path> [--commit] --headless-output build\other-track-key-edit.txt
build\komapedit.exe --debug-headless-insert-edit [map-path] --repeater-only [--commit] --headless-output build\repeater-insert-edit.txt
build\komapedit.exe --debug-headless-include-delete <map-path> [--index N] [--commit] --headless-output build\include-delete.txt
build\komapedit.exe --debug-headless-include-replace <map-path> --new-path <file> [--index N] [--commit] --headless-output build\include-replace.txt
build\komapedit.exe --debug-headless-resource-list-replace <map-path> --headless-output build\resource-list-replace.txt
build\komapedit.exe --debug-headless-resource-list-insert <map-path> --kind structure|signal --headless-output build\resource-list-insert.txt
build\komapedit.exe --debug-headless-signal-aspect-columns <map-or-scenario-path> --headless-output build\signal-aspect-columns.txt
build\komapedit.exe --debug-headless-new-file-wizard <nonexistent-map-path-under-tests> --headless-output build\new-file-wizard.txt
build\komapedit.exe --debug-headless-scenario-create <nonexistent-scenario-path-under-tests> --route <existing-map-path> --headless-output build\scenario-create.txt
build\komapedit.exe --debug-headless-fresh-resource-list-workflow <map-path> --headless-output build\fresh-resource-list.txt
build\komapedit.exe --debug-headless-include-import-create <map-path> --headless-output build\include-import-create.txt
build\komapedit.exe --debug-headless-new-element-edit <map-path> [--commit] --headless-output build\new-element-edit.txt
build\komapedit.exe --debug-headless-light-edit <map-path> --headless-output build\headless-light-edit.txt
build\komapedit.exe --debug-headless-pretrain-edit <map-path> --headless-output build\headless-pretrain-edit.txt
build\komapedit.exe --debug-headless-legacy-fog-edit <map-path> --unit-distance 25 --headless-output build\headless-legacy-fog-edit.txt
build\komapedit.exe --debug-headless-sparse-new-element <map-path> --headless-output build\sparse-new-element.txt
build\komapedit.exe --debug-headless-section-edit-batch [map-path] [--commit] --headless-output build\section-edit-batch.txt
build\komapedit.exe --debug-headless-table-find --headless-output build\headless-table-find.txt
build\komapedit.exe --debug-headless-touch-input --headless-output build\headless-touch-input.txt
build\komapedit.exe --debug-headless-settings-persistence --headless-output build\settings-persistence.txt
build\komapedit.exe --debug-headless-curve-parameter-edit <map-path> --headless-output build\curve-parameter-edit.txt
build\bin\typed_snapshot_tests.exe signal-glare <map-path> [--commit]
```

### Resource-list and file workflows

| Command | Input and write scope | Main checks |
| --- | --- | --- |
| `--debug-headless-resource-list-replace` | Map path; opens the Win32 picker for manual selection of another valid Structure List; in-memory Apply | Preview/Edit metadata merge, stable identity, full reparse, list-cache/path refresh, and source hash after reload. Cancel, same-file selection, or an invalid list returns FAIL |
| `--debug-headless-resource-list-insert` | Map path and `--kind structure` or `signal`; in-memory Apply; rejects `--commit` | Structure requires a list loaded through Include and checks 2-field rows and above/below order; Signal checks main/glare insertion blocks, 6-field main rows, and explicit glare addition. Both check Reset and source hashes |
| `--debug-headless-signal-aspect-columns` | Map/Scenario; input-route edits stay in memory, Save/reload uses temporary fixtures; rejects `--commit` | Production column operations, confirm/cancel, independent main/glare widths, repeated Apply/Revert, and the display limit |
| `--debug-headless-new-file-wizard` | Nonexistent map path under `tests/`; creates files and cleans up afterward | Empty-map reload, exclusive creation, existing-list reuse, staging/saving all five Load types, and empty-list reload |
| `--debug-headless-scenario-create` | Nonexistent Scenario path under `tests/`, with `--route` pointing to an existing map; deletes the new Scenario afterward | Official key order, duplicate-creation rejection, field-presence bits, relative paths/default weights, Scenario preview, asynchronous map loading, and history preservation |
| `--debug-headless-scenario-lifecycle` | Scenario; reads input sources and writes in an exclusive temporary directory | History entries, metadata publication, view restoration on Reload, single/multiple Route selection, creation/loading, saving Map before Scenario, settings/layout retries, CSV export, and file-failure handling |
| `--debug-headless-fresh-resource-list-workflow` | Map path; reads the input and creates Map, Structure, Station, and Signal fixtures in a temporary directory | Load targets in a map without distance statements; applying multiple unsaved Loads and first rows of empty lists in one batch; repeated Signal Apply, glare deletion/re-addition, Revert, and adjacent-row Save/reload. Checks `input_map_bytes_unchanged` and `fixture_files_cleaned` |
| `--debug-headless-table-find` | Built-in fixtures | Station-draft references to ordinary Sound distinguished from Sound3D; Repeater keys containing commas/spaces; committing the active Signal main/glare cell before find |

### Element editing and insertion

`--debug-headless-new-element-edit` drives the production wizard, Inspector, and deferred Delete/Cancel flows. It covers resource, Repeater, Structure, other-track, and combined Curve/Gradient templates, checking start/end distances, transition/cant linkage, source order, target files, and subsequent edits. Resource-list keys prefill fields in matching templates.

By default, the command checks source hashes after Reset/Reload. With `--commit`, Save writes paired curve and gradient statements to the selected source files and leaves the changes for physical diff inspection. Mixed-ledger cases need editable `Structure.Put` and other-track position/X/Y interpolation-parameter rows; they check insertion order, repeated x/y edits, single-field restoration, failed Apply/Revert, and view state. Related Save/fresh Reload checks use four sets of temporary fixtures.

The `curve.interpolate` template uses optional trailing arguments, shared typed validation, and semantic fingerprints. It accepts the official 0/1/2-argument forms and rejects cant-only input, nonfinite values, unknown fields, and unsupported methods. `typed_edit_contract` runs dry-run, Apply/Reset/Commit/Reload on Shift-JIS/CRLF Include fixtures, checking expressions, comments, order, and identity. New-element headless validation checks the two-argument default, linked checkboxes, subsequent edit/cancel, and unique source markers.

The following commands require explicit input paths, validate production workflows through in-memory edits, and reject `--commit`:

| Command | Input requirements | Checks |
| --- | --- | --- |
| `--debug-headless-light-edit` | A map with any valid subset of Light declarations, such as `tests\light_valid.txt` | Metadata merge, Apply across all three forms, deferred deletion, all three wizard insertions at distance 0, evaluated values and source forms, and Revert to the original subset with byte checks |
| `--debug-headless-pretrain-edit` | A map with an existing `PreTrain.Pass` | `headless_pretrain.cpp` checks repeated time/seconds Apply, distance, invalid input, deletion, wizard creation, edit/cancel after insertion, Revert, 2D identity/labels, and 3D marker data |
| `--debug-headless-legacy-fog-edit` | A map with any number of legacy fog rows | `legacy_fog_edit_validation.cpp` checks identity, invalid input, repeated Apply, deletion, wizard creation, and Revert; a WARP scene checks fog refresh, with additional checks for table/plan caches and full-rebuild scheduling |
| `--debug-headless-curve-parameter-edit` | A map containing SetGauge, SetCenter, and SetFunction | CG/CC/CF identity, white signs, independent visibility, Inspector distance/parameters, rejection of `SetFunction(2)`, deletion/creation, Revert, and source bytes |
| `--debug-headless-station-put-margin-edit` | A map with an editable `Station.Put` | Selects an existing editable placement and reports its distance; checks rejection of zero/wrong-sign margins, wizard defaults `margin1=-5` and `margin2=5`, valid insertion, and Revert |
| `--debug-headless-sparse-new-element` | Target source with zero/one numeric distance statement, or nondecreasing anchors ending below 866 | Production `DrawDistance.Change(500)` wizard at distance 25 for sparse sources or 866 for monotonic tails; checks block reuse, insertion before/after, original text, new-row identity/values, and hashes after Reset |
| `--debug-headless-auto-insert-diagnostics` | `testmap\auto_insert_failures` fixture directory | Selects targets by physical file, line, type, distance, and identity; checks automatic success, manual recovery, hard rejection, actual Apply of every candidate, second Apply, retry termination, and Reset. Success requires `failed_cases=0` and `result=PASS` |

PreTrain `passTime` accepts unquoted `hh:mm:ss` or finite seconds, including times beyond 24 hours and nonincreasing times. Temporary typed-contract fixtures cover PreTrain and Legacy.Fog encoding, BOM/line endings, Include, disk concurrency, and Save/reload; automatic-insertion contracts also cover mixed EOF batches and stale choices.

### Linked edits and Include

For the following modes, `--commit` saves the validated working copy and leaves route changes for diff inspection. The default runs in-memory validation and source-hash checks.

| Command | Input requirements and validation scope |
| --- | --- |
| `--debug-headless-repeater-key-edit` | Explicit map path; whole-chain rename validation |
| `--debug-headless-other-track-key-edit` | Explicit map path; selects a string-keyed other track with at least two statements and checks whole-track atomicity, map-wide duplicate names, dependent references, Apply/Reset/Reload, and target/file hashes |
| `--debug-headless-insert-edit --repeater-only` | Creates separate Begin and Begin0 statements with unique keys, runs dry-run and Apply/Reset, and checks Save/Reload when committing |
| `--debug-headless-include-delete` | Explicit map path; `--index` defaults to 0. Checks stale-hash rejection, subtree deletion, non-target semantics, and Reset; blocks deletion when surviving statements depend on the removed subtree |
| `--debug-headless-include-replace` | Explicit map path, `--new-path <file>`, and `--index` defaulting to 0. Writes the path as a single-quoted argument and checks whole-map semantics with old/new subtrees excluded, structure refresh, and Reset/Reload; blocks old-variable dependencies or duplicate Load |

`--debug-headless-include-import-create` creates a uniquely named temporary child map beside the explicitly supplied input map. It checks importing existing files, creating files, Include insertion, zero-distance anchors, reparsing, and structure refresh. The child uses UTF-8 without BOM, CRLF, and a `BveTs Map 2.02:utf-8` header; the parent uses in-memory Apply/Reset. The command checks original-file hashes and removes the temporary child afterward.

### Creator messages and persistence

`--debug-headless-creator-message <map-or-scenario-path> [--scenario-index N] [--unit-distance M] --headless-output <report>` reads the input, checks source-file bytes, and rejects `--commit`. Separate temporary maps and history configuration validate Preview/Edit metadata, the wizard, raw content, Save blocking for unapplied drafts, Apply/Revert/Save/Reload, Include messages, the document-open popup, and suppression preferences. DLL contracts cover physical deduplication and source expansion order.

`--debug-headless-settings-persistence` checks canonical settings/history roundtrips, defaults for invalid entries, and unchanged bytes after loading. Creator-message `has_messages` and `suppressed` values are saved as `0`/`1`; only the exact value `1` loads as true.

### Manual acceptance

Select relevant checks for map/Include loading and reload, plan/profile/radius views, station jumps, measurement, CSV export, model preview and errors, 3D objects/markers/camera/gizmos, Apply/Revert/Save/Reload, inline drafts, settings persistence, and Release distribution. UI acceptance focuses on hit testing and highlights, menus, confirmation dialogs, column scrolling, and final rendered pixels.

## Build scripts, dependencies, and distribution

- CMake owns build configuration; batch scripts run the Windows build workflow.
- Preserve `NINJA_EXE`, `VCPKG_ROOT`, and the `x64-mingw-dynamic` fallback.
- Place the EXE and notice files at the output root, DLLs in `bin`, and INI files in `settings`.
- Distribution cleanup retains `bin`, `settings`, `LICENSE`, `NOTICE`, and `THIRD_PARTY_NOTICES.md`.
- Development builds, Release builds, and release cleanup share directory-layout checks and abort on legacy INI or DLL files at the root.
- Use ImGui's docking branch and upstream ImPlot.
- Preserve license and notice files; new dependencies require synchronized CMake, developer-documentation, and third-party-notice updates.
- Route publication export is listed in `TODO.md`: planned behavior expands Includes, optionally evaluates expressions to constants, copies used resources, and writes a report, using a temporary output directory to protect development routes.
