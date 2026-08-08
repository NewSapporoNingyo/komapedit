# AGENTS.md — Project Development Guide

This file is the authoritative repository guide for AI coding tools. Human contributors should also consult [`docs/dev.md`](docs/dev.md); AI-assisted workflows and acceptance gates are expanded in [`docs/ai-dev.md`](docs/ai-dev.md). User-facing behavior belongs in [`README.md`](README.md), and current implementation progress belongs in [`TODO.md`](TODO.md).

## Repository Overview

`komapedit` is a C++17 / Win32 desktop application for viewing and gradually editing BVE Trainsim map files. It currently provides source-backed editing for existing list rows referenced by `Station.Load`, `Structure.Load`, `Signal.Load`, `Sound.Load`, and `Sound3D.Load`; supported own-track curve/gradient change points; supported other-track change points; supported Structure, Signal, and Station placements; linked `Repeater.Begin`/`Begin0`/`End` segments; and supported Section, speed-limit, irregularity, beacon, sound/noise, background, adhesion, cab-illuminance, fog, and draw-distance rows. It also provides live 3D X/Y/Z placement editing for Structure, Signal, and Repeater Begin entries, while applicable tables, charts, and 2D/3D markers expose the supported shared inspector/delete actions. It is not yet a complete full-map editor. Use the current code and `TODO.md` as the source of truth for implementation status, and preserve the source-first, save/round-trip, preset, and route-release-export direction unless a task explicitly changes it.

The repository is organized around three runtime components:

* `maploader.dll`: parses BVE Map files, handles includes and encodings, generates own-track and other-track geometry, and exposes parsed/intermediate data through a C ABI.
* `model_loader.dll`: loads Structure model files through Assimp and exposes mesh/material data through a C ABI for model and scene previews.
* `komapedit.exe`: the GUI application built with Dear ImGui, ImPlot, Win32, DirectX 11, and WIC.

Do not treat this as a generic cross-platform GUI project. Windows, Win32, DirectX 11, and the current CMake/Ninja workflow are the primary supported environment unless a task explicitly says otherwise.

## Important Source Areas

Use the existing file boundaries whenever possible.

* `src/maploader/maploader.cpp`
  Public C ABI entry points, API-version reporting, map handle lifecycle, regular/scene geometry regeneration, typed snapshot/edit dispatch, source-text retrieval, and C-boundary exception/error handling.

* `src/maploader/maploader_internal.h`
  Shared maploader-owned state and declarations: `MapContext`, parsed row records, source files/spans, include stacks, edit refs, edit reports, timing, and module helper APIs. Keep source anchors and edit metadata rooted in these maploader-owned structures rather than reconstructing them ad hoc from GUI tables.

* `src/maploader/maploader_core.cpp`
  Common string/value helpers, source file registration, source-span and include-stack bookkeeping, loaded-line statement helpers, parse timing, and shared `MapContext` mutation helpers.

* `src/maploader/maploader_parser.cpp`
  BVE Map/list parsing, `Include` processing, variable/expression handling, assignment and resource-list `Load` preview metadata, uniqueness validation for resource-list Loads and `Train[].Enable`, statement/source-anchor collection, and population of map/list row records before geometry and IR generation.

* `src/maploader/maploader_geometry.cpp`
  Own-track and other-track geometry generation, relocation, curve/radius/gradient calculations, structure placement buffers, and adaptive scene control points.

* `src/maploader/maploader_identity.cpp`
  Stable edit-id generation and deterministic hashing shared by snapshots and editing.

* `src/maploader/maploader_snapshot.cpp`
  One-pass construction of `KvMapSnapshot` v6 and `KvSceneGeometrySnapshot` v1, string/value arenas, fixed row arrays, and content/geometry/scene revision invalidation.

* `src/maploader/maploader_semantic.cpp`
  Typed semantic comparison and deterministic validation fingerprints for supported edit operations.

* `src/maploader/maploader_edits.cpp`
  Typed edit-batch validation, edit target/report snapshot construction, source patch preview, dry-run/apply/commit, in-memory source overrides, reparsing, encoding-aware writeback, and distance-expression adjustment.

* `src/maploader/text_decoder.cpp` / `src/maploader/text_decoder.h`
  File reading, UTF-8 path handling, BOM detection, UTF-16/CP932 conversion, and decoded text ownership helpers.

* `src/maploader/diagnostics.cpp` / `src/maploader/c_api.cpp`
  Loader diagnostic state, last-error handling, and C ABI allocation/free helpers.

* `include/maploader.h`
  Public function surface for maploader API v6. The bundled EXE requires an exact `kv_api_version()` match and does not use a legacy fallback.

* `include/maploader_snapshot.h`
  Fixed-width POD definitions for the map, scene, edit-target, edit-batch, and edit-report ABI. Keep spans, views, ownership, version, and structure-size rules explicit; never expose STL types or exceptions across this boundary.

* `include/repeater_linkage.h`
  Shared `Repeater.Begin`/`Begin0`/`End` pairing and chain metadata for maploader, tables, and canvases. Use this helper instead of independently reconstructing a Repeater segment in a single UI path.

* `include/own_track_transition_linkage.h`
  Shared source-order pairing rules between `Curve`/`Gradient` `BeginTransition` rows and their consuming Begin/End statements. The edit and GUI marker paths use this helper; parser validation currently uses its own `TransitionEvent` state, so keep the two representations behaviorally consistent.

* `src/model_loader/model_loader.cpp`
  Assimp-based Structure model loading, mesh/material conversion, texture path handling, bounds/center/radius calculation, and error reporting.

* `include/model_loader.h`
  Public v2 C ABI for `model_loader.dll`. The GUI requires `ml_api_version() == 2`; keep ownership and allocation/free rules explicit, and remember that `MlMeshData` count fields use `size_t`.

* `src/main_window/gui_kme.cpp`
  Main Win32 window, DirectX 11 setup, Dear ImGui application loop, menus/toolbars, map loading and edit-mode workflow, Properties/Edit inspector, working-copy Apply/Save/Reload behavior, settings/history integration, and high-level UI behavior.

* `src/main_window/kme.h`
  Shared GUI data structures and application state. Be careful when adding fields because this file is used across multiple GUI modules.

* `src/main_window/app_settings.cpp` / `src/main_window/app_settings.h`
  `settings/settings.ini` and `settings/history.ini` persistence, view/window visibility state, UI style settings, and compatibility aliases for older setting keys.

* `src/main_window/runtime_paths.cpp` / `src/main_window/runtime_paths.h`
  Shared executable-relative paths, creation of the `settings` directory, and dependency-aware loading of DLLs from `bin`.

* `src/main_window/maploader_runtime.cpp`
  Process-lifetime loading of `bin/maploader.dll`, exact API-version validation, and cached typed C ABI entry points used by the existing `kv_*` call sites.

* `src/main_window/file_structure_diagram.cpp`
  Include-file structure diagram layout/rendering and shared source-file context actions used by the diagram and Properties/Edit inspector.

* `src/main_window/text_preview.cpp`
  Read-only source preview backed by `kv_get_source_text`, including working-copy refresh and parser-confirmed distance-boundary selection for ambiguous distance moves.

* `src/main_window/debug_headless.cpp` / `src/main_window/debug_headless.h`
  Debug-only headless validation entry points, including map load, source-anchor/edit round-trip, own-track and other-track change-point editing, grouped distance editing, station-list editing, Repeater and Section editing, open/plan/3D-scene benchmarks, scene-camera transfer, table find, and touch-input checks.

* `src/main_window/touch_input.cpp` / `src/main_window/touch_input.h`
  Touch input gesture translation for the Win32/Dear ImGui UI and related Debug-only validation helpers.

* `src/canvas2d/canvas2D.cpp`
  2D plan view, curve/gradient and station/signal/Section/sound/effect markers and labels, source-backed marker inspector/delete contexts, measurement mode, background image display/alignment, grid modes, panning/zooming/rotation behavior.

* `src/canvas2d/profile_plots.cpp`
  Profile and curve-radius chart rendering, including source-backed curve/gradient change-point marker contexts.

* `src/canvas3d/canvas3D.cpp` and `include/canvas3D.h`
  DirectX 11 Structure model preview and 3D scene preview rendering, scene building, route-information overlays including active speed limits and Section-selected signal limits, shared Structure/Signal/Repeater placement gizmos, source-backed object/marker inspector and supported delete actions, Repeater edit adaptation, object/marker picking and context actions, scene camera state, model loading worker state, and scene track/marker visibility.

* `include/map_marker_visuals.h` and `src/main_window/map_marker_visuals.cpp`
  Shared map-marker kinds, colors, and icon recipes used by the 2D and 3D canvases. Add or change a map-element symbol here instead of maintaining duplicate 2D and 3D recipes.

* `src/table/datatable.cpp`
  Data table windows, cached table data, station/track/Structure/Repeater/signal/sound/effect lists, dynamic Section and variable views, resource-list Load source summaries, other-train Enable presentation, shared inline list editing, file selection/open/find actions, and related UI.

* `src/table/table_navigation.cpp`
  Table-to-plan/3D-scene marker navigation, scene-to-table navigation, highlighted row state, and marker visibility updates triggered by locating rows.

* `include/multilanguage.h`
  UI localization strings. When adding user-visible UI text, update Simplified Chinese, English, and Japanese entries consistently.

* `third_party/imgui` and `third_party/implot`
  External source trees fetched by script. Do not manually edit vendored third-party code unless the task explicitly requires it.

## Build and Setup

The normal Windows setup flow is:

```bat
.\get_3rd_party_packages.bat
```

Then install Assimp. If using vcpkg, set `VCPKG_ROOT` and install the expected triplet, for example:

```bat
set VCPKG_ROOT=C:\path\to\vcpkg
%VCPKG_ROOT%\vcpkg install assimp:x64-mingw-dynamic
```

Debug build:

```bat
.\build_dev.bat
```

Release build:

```bat
.\build_release.bat
```

The build scripts use CMake + Ninja. They also use vcpkg automatically when `VCPKG_ROOT` is set. If `VCPKG_DEFAULT_TRIPLET` is not set, the scripts default to `x64-mingw-dynamic`; MSVC builds must select an appropriate triplet such as `x64-windows` explicitly.

CMake 3.20 is the project minimum. CMake 3.21 or newer is recommended when
Assimp is not supplied through vcpkg or an explicit prefix, because the generic
runtime-DLL copy fallback requires it.

Expected build outputs:

* Debug: `build\`
* Release: `build_release\`
* Runtime output layout:

  * Output root: `komapedit.exe`, `LICENSE`, `NOTICE`, and `THIRD_PARTY_NOTICES.md`
  * `bin/`: `maploader.dll`, `model_loader.dll`, Assimp runtime DLLs, and copied DLL dependencies
  * `settings/`: the directory is created when absent; `settings.ini`, `history.ini`, and `imgui.ini` are created or updated by the application while it runs and exits

Do not commit generated build outputs.

## Files That Should Normally Not Be Committed

Avoid committing local/generated files such as:

* `build/`
* `build_release/`
* cloned third-party source trees under `third_party/`
* `settings/imgui.ini`
* `settings/settings.ini`
* `settings/history.ini`
* local test output folders
* generated CSV exports
* temporary route/map/model files used only for manual testing

## Coding Style and General Rules

* Use C++17.
* Prefer small, focused patches over large rewrites.
* Preserve the current Win32 / DirectX 11 / Dear ImGui architecture unless specifically asked to refactor it.
* Do not introduce a new GUI framework such as Qt, wxWidgets, SDL, GLFW, Tauri, or Electron unless the task explicitly requires it.
* Avoid adding new package managers or build systems. Keep CMake as the source of truth.
* Prefer RAII, `std::vector`, `std::string`, `std::filesystem`, and narrow helper functions for new internal C++ code.
* Keep Windows macros controlled. Preserve `UNICODE`, `_UNICODE`, `NOMINMAX`, and `WIN32_LEAN_AND_MEAN` assumptions.
* Do not let C++ exceptions cross the public C ABI boundary.
* Do not expose STL types, C++ classes, exceptions, or ownership-ambiguous pointers through `maploader.h` or `model_loader.h`.
* Keep memory ownership explicit. Any memory allocated inside a DLL and returned through the C ABI must have a matching free function.
* For public C ABI changes, update all call sites and document the ownership/validity rules.
* Treat typed ABI inputs as call-scoped views. `KvMapSnapshot` nested storage is handle-owned until reparse or regular-geometry invalidation; `KvSceneGeometrySnapshot` storage is independently handle-owned until scene/regular-geometry regeneration; typed target/report storage is valid until the next edit operation, target lookup, reset, reparse, or free. Callers copy only the GUI state they need and never free nested snapshot pointers.
* Preserve Apache License 2.0 headers and third-party notices.

## Map Loading and Geometry Rules

Changes in `maploader` must be tested carefully because route files can be large, nested, and encoded in different formats.

Preserve support for:

* `BveTs Map 2.0+`
* UTF-8
* UTF-8 with BOM
* UTF-16LE
* UTF-16BE
* CP932 / Shift_JIS related input
* `Include` references
* `$variable = expression;`
* predefined `distance`
* basic math functions
* `#` and `//` comments
* legacy syntax where currently supported
* own-track curves and gradients
* other-track lateral/vertical interpolation, gauge, center offset, and cant
* station data, Structure placement, Structure model lists, Repeater lists, signal/beacon data, PreTrain passes, sound/noise events, irregularity/adhesion data, environmental change points, speed-limit sections, and CSV geometry export where currently implemented

When modifying parsing behavior, avoid special-casing only one route file. Prefer a general rule that matches the official BVE Map syntax, and do not add or document private map syntax extensions. Preset features must generate ordinary BVE map/list statements rather than requiring custom syntax in route files.

When adding editable statement support, preserve enough source information to write back safely:

* source file path and include stack
* line/column or byte span
* original statement text
* statement type and raw argument text
* evaluated values and the current distance expression
* global parse order and a stable element id

Keep `KvMapSnapshot` exhaustive and strongly typed. Preview handles may omit full edit metadata only through explicit capability bits; Edit handles must expose complete source files, statements, elements, evaluated values, include stacks, source spans, and stable edit identities. Add or change ABI fields only with a version/structure-size decision and synchronized EXE/DLL updates.

Source writeback must preserve the original encoding and line endings where possible. If newly inserted characters cannot be represented in the original encoding, the current implementation blocks the write and reports an error; it does not provide a Save-as-UTF-8 fallback.

## Performance Guidelines

This application is intended to load long BVE routes with nested include files. Avoid performance regressions in map loading, geometry generation, and per-frame rendering.

When working on parser or geometry code:

* Avoid unnecessary repeated file reads.
* Avoid repeated conversion between encodings, parser records, typed snapshots, and GUI structures.
* Avoid accidental O(n²) loops over large track point arrays.
* Prefer cached lookups for repeated Structure/station/track key resolution.
* Keep large arrays contiguous where practical.
* Avoid excessive allocation inside tight geometry loops.

When working on GUI/rendering code:

* Avoid heap allocation in every frame.
* Cache table data when the source data has not changed.
* Avoid rebuilding large strings, labels, and vectors every frame unless necessary.
* Keep asynchronous map loading responsive.
* Do not block the UI thread with long parsing/model-loading work without progress or status feedback.

## UI and Localization Guidelines

* Keep the Dear ImGui docking-based multi-window layout.
* Preserve current menu/tool concepts: File/Open/Reload/Export CSV, 2D View chart/background options, Auxiliary Info marker toggles, Map Info tables, 3D View Structure and scene previews, station jump, distance jump, and measurement mode. Other-train path visibility belongs to the Other Train List in Map Info, not Auxiliary Info.
* Preserve the current editing contract: Properties/Edit `Apply` updates the in-memory working copy and preview, toolbar `Save` commits it to source files, `Revert` discards all pending changes after confirmation and restores the in-memory map to its last saved state, and File/toolbar `Reload` rereads the map from disk after unsaved-change confirmation. Pending inline list drafts must be applied in their table before toolbar Save is allowed to commit them.
* Keep File Structure Diagram and Text Preview source actions on the shared decoded/working-copy path; do not bypass `kv_get_source_text` by rereading parser-owned source files directly.
* When adding new user-facing strings, update all supported UI languages in `include/multilanguage.h`: Simplified Chinese, English, and Japanese.
* Keep wording short enough for toolbar/menu use.
* Store persistent user settings in `settings/settings.ini` only when they are actual application preferences.
* Store recent-map or per-map background-image alignment data in `settings/history.ini`, not in global settings.
* Do not break existing `settings/imgui.ini` layout persistence unless the task is specifically about layout reset/migration.
* Keep load, parse, model, save, and export diagnostics in the existing console window, with English log output by default. Do not add a separate read-diagnostics window unless a task explicitly asks for one.

## 2D Canvas Guidelines

For `src/canvas2d/canvas2D.cpp` changes:

* Preserve pan, wheel zoom, rotation, double-click fit-to-view, station jump, measurement mode, and grid modes.
* Be careful with coordinate transforms between world/map coordinates and screen coordinates.
* Keep mileage, elevation, gradient, radius, speed-limit, station marker, and other-track display consistent.
* Preserve other-train paths and stop markers, Section markers and their parameter labels, plan marker context menus, source-backed inspector/delete actions, and table-location behavior.
* Keep curve/gradient change-point markers synchronized across the plan, profile, curve-radius chart, and 3D scene. Only paired transition markers may open the consuming statement's inspector/delete action; keep orphan `BeginTransition` markers read-only and visibly identified as unpaired.
* Do not mix absolute map distance with screen-space or local relative distance without clearly named variables.
* Background image transform code should keep position, size/scale, rotation, brightness, and two-station alignment behavior stable.
* If splitting the 2D canvas, keep view state, marker caches, hit testing/context menus, background image handling, drawing primitives, and the plan-render entry point clearly separated while preserving behavior.

## 3D Preview Guidelines

For `model_loader` and `canvas3D` changes:

* Keep Assimp isolated inside `model_loader.dll` where possible.
* Do not leak Assimp types into the GUI-facing C ABI.
* Preserve mesh/material/texture ownership rules.
* Always keep a matching free path for allocated model data.
* Test model preview after changes by opening a Structure model from the Structure model list.
* Treat the 3D scene preview as shared `canvas3D` functionality, not as a one-off GUI overlay.
* Keep scene build logic consistent with the current `MapModel`, `Structure.Put`/`Put0`/`PutBetween`, `Repeater.Begin`/`Begin0`/`End`, background changes, and own/other-track geometry.
* Preserve scene camera jump behavior from station selections, numeric distance jumps, table-to-scene object jumps, and Debug scene-camera transfer validation.
* Preserve 3D object picking, highlight outlines, context-popup state, and scene-to-table locate behavior when changing scene interaction code.
* Preserve scene track visibility synchronization with Other Tracks and `Auxiliary Info -> 3D Scene` settings.
* Keep map-element icon recipes in `map_marker_visuals`; build their layout during scene-cache construction, let Auxiliary Info visibility changes update only marker visibility/index data, and preserve hover/context/table navigation.
* Preserve the route-information overlay for current radius/cant, gradient, active `SpeedLimit.Begin`/`End` state, Section-selected signal speeds, and next-station distance when changing scene camera or route-event sampling.
* Keep live Structure, Signal, and Repeater Begin placement gizmo edits synchronized with the Properties/Edit fields and in-memory preview. `Put0`/`Begin0` must be explicitly converted before coordinate editing; short-form `Signal.Put` must be explicitly converted before editing its extended placement fields.
* Keep gizmo sizing in the existing 3D Canvas Settings persistence path, and preserve camera-side axis orientation and drag hit testing when changing its rendering.
* Handle missing textures, unsupported model formats, and invalid files gracefully.
* When adding 3D quality settings, handle unsupported render scale, MSAA, texture filtering, or outline options with a fallback and an English console warning.

## Data Table Guidelines

For `src/table/datatable.cpp` or `src/table/table_navigation.cpp` changes:

* Keep table data consistent with the parsed map and typed snapshot.
* Avoid recomputing large table contents every frame.
* Keep the shared table find panel responsive at narrow widths, with stable button widths and an input field that adapts to available space.
* Preserve row-to-plan, row-to-scene, and scene/plan-to-row navigation state, including highlighted rows and marker visibility side effects.
* Keep the Section views dynamic: the current UI shows row number, distance, `signalN` or `vN` argument columns, and source file, while method identity remains in the typed model rather than a visible table column. Preserve explicit `null` values and source-file actions. Section rows are source-backed editable: `section.begin` covers `Section.Begin`/`Section.BeginNew` and `section.speedLimit` covers `Section.SetSpeedLimit`/`Signal.SpeedLimit`; both expose finite `distance` and a variable-length numeric parameter list whose parameter count can be increased or decreased (at least one parameter, matching the parser minimum). Editing one parameter keeps the untouched source argument text intact (expressions such as `$Variable` survive), while a count change rewrites the whole list.
* Keep the Variable List grouped by case-insensitive normalized name while preserving first-seen group order and assignment parse order; retain the original expression tooltip and source-file action.
* Keep each Station/Structure/Signal/Sound/Sound3D list header on the shared `ResourceListSource` metadata path so it shows the evaluated Load argument and exposes the raw expression/resolved path. Show the unique case-insensitive `Train[].Enable` time on its matching other-train stop group; do not add a last-one-wins fallback because the parser rejects duplicate declarations.
* The currently editable rows are existing inline `station.list`, `structure.model`, `signal.aspect`, `sound.list`, and `sound3D.list` rows, plus `curve`, `gradient`, `otherTrack.change`, `structure.put`, `structure.between`, `station.put`, `signal.put`, linked `repeater` segments, `section.begin`, `section.speedLimit`, `speedlimit`, `irregularity.change`, `beacon.put`, `mapSound.play`, `mapSound3D.put`, `rollingNoise.change`, `flangeNoise.change`, `jointNoise.play`, `background.change`, `adhesion.change`, `cabIlluminance.change`, `fog.change`, and `drawDistance.change`. Inline list rows support editing, clearing, same-file reordering, and deletion; resource-path rows also offer file selection. `SpeedLimit.Begin` exposes finite `distance` and `speed`, while `SpeedLimit.End` exposes finite `distance` only; they are independent rows and require no pairing validation. `Section.Begin`/`BeginNew` and `Section.SetSpeedLimit`/`Signal.SpeedLimit` expose finite `distance` plus a variable-length numeric parameter list; the parameter count can be increased or decreased (minimum one), and the method text is read-only. `otherTrack.change` rows expose finite `distance` plus the existing method-appropriate parameter slots; the track key, method, and parameter count are read-only, and zero-argument statements such as `Cant.BeginTransition`/`Cant.End` allow distance edits and deletion only. Additions must have an unambiguous source location in the map or related list file before exposing editable columns.
* Do not silently discard original data that the current app cannot yet save.

## Editing Model and Release Export Guidelines

The current editor implementation is source-first. Extend the shared editable-statement and working-copy path instead of duplicating string rewrite logic in each UI feature.

* Reuse the existing maploader-owned `SourceSpan`, `ParsedStatement`, `EditSourceRef`, and `MapEditChange` structures. Do not reconstruct source anchors from GUI table text or introduce a parallel GUI-owned source document model.
* Keep stable element ids flowing from maploader typed rows into `MapModel`, table caches, 2D markers, and 3D scene objects.
* Keep Preview loading separate from full Edit metadata hydration. `KV_MAP_CAP_EDIT_METADATA` and `KV_MAP_CAP_FULL_STATEMENT_SOURCE` are the only supported way to distinguish those profiles; do not add a second transport or fallback.
* Preserve API roles: `kv_edit_dry_run_typed()` validates without mutation, `kv_edit_apply_to_memory_typed()` reparses an in-memory working copy for preview, `kv_edit_apply_typed()` performs the existing direct write path, `kv_edit_commit_typed()` writes the validated working copy, and `kv_edit_reset_memory()` discards overrides and reparses disk sources only when a working-copy override exists; when the context already matches disk, reset is a no-op. Do not turn Apply into an implicit disk write or Save into an extra disk reload.
* Keep `sourceHash` as the current working-copy identity and `expectedSourceHash` as the disk-baseline concurrency guard. Repeated Apply/Delete cycles must not replace the disk baseline with an in-memory hash.
* The method-preserving/no-conversion rule for Curve/Gradient and SpeedLimit edits is scoped to those statement families. `Structure.Put0` and `Repeater.Begin0` have explicit confirmed conversion paths to `Put`/`Begin`, short-form `Signal.Put` has an explicit confirmed conversion to the full form, and Repeater trim-to-change-point deletion may convert a selected `Begin` to `End`.
* `KV_EDIT_INSERT` uses the existing typed source-writeback path for the New Map Element wizard. Insert changes must use structured fields, a loaded target file, and the same dry-run, distance planning, environment validation, full-reparse, and commit flow as updates; `replacementStatement` and `insertBeforeEditId` remain rejected for distance insertion.
* The Properties/Edit UI supports existing updates/deletion for `Curve`, `Gradient`, `otherTrack.change`, Structure/Signal/Station placements, linked Repeater segments, `Section.Begin`/`BeginNew`, `Section.SetSpeedLimit`/`Signal.SpeedLimit`, `SpeedLimit.Begin`/`End`, `Irregularity.Change`, `Beacon.Put`, `Sound.Play`, `Sound3D.Put`, rolling/flange/joint noise, `Background.Change`, `Adhesion.Change`, cab illuminance, fog, and draw-distance rows.
* A paired Curve/Gradient `BeginTransition` is edited through the consuming Begin/End inspector and must be deleted with that statement; an orphan `BeginTransition` is not independently editable or deletable. `otherTrack.change` keeps the track key, method, and parameter count read-only; zero-argument statements such as `Cant.BeginTransition`/`Cant.End` allow distance edits and deletion only, while `Track[].Cant.Cant(...)` remains rejected as unknown syntax.
* `SpeedLimit.Begin` remains a one-argument statement and `SpeedLimit.End` a zero-argument statement; each row supports independent distance/value editing or deletion, and both forms can be inserted by the New Map Element wizard without Begin/End conversion, automatic radius-based limits, or pairing checks. `Section.Begin`/`BeginNew` and `Section.SetSpeedLimit`/`Signal.SpeedLimit` expose a variable-length numeric parameter list (minimum one); individual parameter edits preserve untouched source argument text, while a count change rewrites the list.
* Existing Station, Structure, Signal-aspect, Sound, and Sound3D definition/resource rows use the shared inline-table draft workflow instead; no supported inline list editor inserts a new resource row. The New Map Element wizard is limited to the supported map placement/event/effect row kinds and must not be presented as a generic map or resource-list insertion editor.
* Plan distance moves as a batch grouped by source file, Include context/source section, and target distance. Reuse a unique compatible distance block when possible; otherwise create one block at a parser-confirmed monotonic gap or require explicit boundary/expression selection. Preserve stable statement order, do not globally sort the file, and do not delete user-authored empty distance/comment structure.
* Before Apply or Save succeeds, fully reparse the patched source set, prove each target reached its requested semantic value, and block the operation if non-target evaluated elements or the final variable/distance environment changed unexpectedly.
* Keep `Station.Load`, `Structure.Load`, `Signal.Load`, `Sound.Load`, and `Sound3D.Load` CSV/list rows bound to their physical source files through the shared inline-table draft workflow. `Signal.Load` aspect rows can have a variable number of structure-key columns and an optional glare row; preserve that shape and its source identity while editing or deleting it. The current GUI exposes at most 509 structure-key columns; excess source fields must remain intact even though they are not shown for editing.
* For simple map event/effect rows, preserve the source method and argument shape: `CabIlluminance.Set` versus `Interpolate`, `Fog.Set` versus `Interpolate`, the one-or-three-parameter `Adhesion.Change` form, and unchanged raw object keys. Keep distance/source ordering stable. New inserts with a numeric target distance must derive the source expression through the same distance-expression adjustment helper used by updates; preserve safe `$` expressions and route unsafe/predefined-distance cases through the existing manual resolution workflow.
* GUI and scene segment linkage must derive `Begin`/`Begin0`/`End` chains through `repeater_linkage`, update physical source rows through the typed edit batch, and leave generated 3D instances lazy; do not treat a single generated instance as an independent source object. Repeater `Begin0` coordinate editing requires explicit conversion to `Begin`, and trim-to-change-point deletion may convert a `Begin` to `End`.
* `element_presets.json` should store preset definitions, but applying a preset must emit normal BVE map/list statements.
* Route release export is a route-publishing tool, not a replacement for `build_release.bat`. It should expand includes, optionally constantize variable/distance expressions, copy only used resources, write an export report, and protect development route directories by using a temporary output directory before the final move.

## Testing Expectations

The typed map/edit ABI has focused `typed_snapshot_contract`, `maploader_gradient_projection_contract`, `typed_edit_contract`, and `maploader_diagnostics_contract` CTests; the Debug headless entry points remain the project's broader regression checks. The `typed_snapshot_tests.exe` binary also supports a `signal-glare` mode, which is not registered as a CTest. The diagnostics CTest requires the root `tests/` fixture directory, so verify that these currently ignored local fixtures are available before relying on a clean-checkout test run. For non-trivial changes, prefer Debug validation first and add Release validation only when release packaging or distribution behavior changes:

```bat
.\build_dev.bat
```

The build script leaves `KOMAPEDIT_STRICT_WARNINGS` off by default. For strict
validation, configure the Debug tree explicitly before building:

```bat
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DKOMAPEDIT_STRICT_WARNINGS=ON -DBUILD_TESTING=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

Then run the built executable or the relevant Debug headless path and manually verify the affected areas. Because `komapedit.exe` is a GUI-subsystem executable, PowerShell checks that need captured output should use `Start-Process -Wait -WindowStyle Hidden -PassThru` together with `--headless-output`.

Useful Debug headless entry points:

```bat
build\komapedit.exe --headless-load-map <map-path> --headless-output build\headless-load-map.txt
build\komapedit.exe --debug-headless-plan-bench <map-path> --headless-output build\headless-plan-bench.txt
build\komapedit.exe --debug-headless-open-bench <map-path> --repeat 3 --headless-output build\headless-open-bench.txt
build\komapedit.exe --debug-headless-scene3d-bench <map-path> --window-back-m 100 --window-forward-m 1200 --headless-output build\headless-scene3d-bench.txt
build\komapedit.exe --debug-headless-scene-camera-transfer <map-path> --headless-output build\scene-camera-transfer.txt
build\komapedit.exe --debug-headless-source-anchors <map-path> --headless-output build\source-anchors.txt
build\komapedit.exe --debug-headless-station-list-edit <map-path> --headless-output build\station-list-edit.txt
build\komapedit.exe --debug-headless-edit-roundtrip <map-path> --headless-output build\edit-roundtrip.txt
build\komapedit.exe --debug-headless-own-track-edit [map-path] --headless-output build\own-track-edit.txt
build\komapedit.exe --debug-headless-other-track-edit [map-path] [--commit] --headless-output build\other-track-edit.txt
build\komapedit.exe --debug-headless-distance-edit-batch [map-path] --headless-output build\distance-edit-batch.txt
build\komapedit.exe --debug-headless-repeater-edit-batch [map-path] --headless-output build\repeater-edit-batch.txt
build\komapedit.exe --debug-headless-section-edit-batch [map-path] [--commit] --headless-output build\section-edit-batch.txt
build\komapedit.exe --debug-headless-table-find --headless-output build\headless-table-find.txt
build\komapedit.exe --debug-headless-touch-input --headless-output build\headless-touch-input.txt
```

The typed contract binary also exposes the unregistered glare check:

```bat
build\bin\typed_snapshot_tests.exe signal-glare <map-path> [--commit]
```

The optional map path for the own-track, other-track, distance, Repeater, and
Section edit commands falls back to a developer-machine route path when omitted;
pass an explicit map path on other machines.

For edit/save changes, run the corresponding `--debug-headless-source-anchors`, `--debug-headless-edit-roundtrip`, `--debug-headless-own-track-edit`, `--debug-headless-other-track-edit`, `--debug-headless-distance-edit-batch`, `--debug-headless-station-list-edit`, `--debug-headless-repeater-edit-batch`, and `--debug-headless-section-edit-batch` checks as applicable, including save-then-reload typed snapshot comparison when disk writeback changes. Keep map load, open/plan bench, scene3d bench, scene-camera transfer, and table find checks in the validation set when those areas are touched. Route release export still needs its own focused headless coverage when implemented.

Minimum manual checks for map-loading or geometry changes:

* Open a normal `.txt` or `.csv` BVE map.
* Open a map that uses `Include`.
* Confirm maps with duplicate same-kind unkeyed resource-list Loads or case-insensitively duplicate `Train[].Enable` declarations fail with diagnostics that identify both declarations.
* Reload with `F5` or File -> Reload.
* Confirm own-track plan view appears.
* Confirm profile and curve-radius views still work.
* Confirm station jump still works.
* Confirm measurement mode still reports reasonable values.
* Export CSV and inspect the output headers and numeric formatting.

Minimum manual checks for Structure/model changes:

* Open a map with `Structure.Load`.
* Open the Structure model list.
* Preview at least one model.
* Verify rotation and zoom still work.
* Verify missing/invalid model files produce a user-visible error rather than a crash.

Minimum checks for 3D scene preview changes:

* Open a map with Structure and Repeater data.
* Open `3D View -> 3D Scene Preview` and start the preview.
* Verify own/other-track paths, Structures, Repeaters, and background changes render as expected.
* Use Station Jump and confirm the 3D scene camera moves to the selected distance.
* Toggle Other Tracks and `Auxiliary Info -> 3D Scene` options and confirm the scene/plan state remains synchronized.
* Verify affected scene markers still honor visibility settings, hover/highlight correctly, and locate to and from their supported table rows.
* Confirm the route overlay updates radius/cant, gradient, active speed limit, Section-selected signal speeds, and next-station distance as the camera moves.
* Run `--debug-headless-scene3d-bench` and `--debug-headless-scene-camera-transfer` when the change touches scene building, rendering, depth/camera behavior, or station jump transfer.

Minimum checks for the current linked editing workflow:

* Enable edit mode and open Properties/Edit from supported curve/gradient, placement, Repeater, speed-limit, irregularity, beacon, sound/noise, background, adhesion, cab-illuminance, fog, and draw-distance rows and applicable chart/2D/3D markers.
* Check Curve and Gradient methods with and without a paired `BeginTransition`: edit the primary distance/value fields, edit the paired transition-start distance, and delete the pair. Confirm orphan transitions remain read-only, method/argument shapes do not change, and the plan/profile/radius/3D geometry plus station and marker positions refresh together.
* Check standalone `SpeedLimit.Begin`, standalone `SpeedLimit.End`, and alternating Begin/End rows in the Speed Limit Point List, 2D plan, and 3D scene. Confirm Begin permits finite distance/speed edits, End permits finite distance edits only, each row can be deleted independently, and End always writes back as `SpeedLimit.End();` in the correct map/include source file.
* For speed-limit Apply/Delete, confirm the compact active-speed cache, table/2D marker cache, 3D route event cache, and 3D marker chunks refresh without rebuilding unrelated geometry or scene-model data.
* Check `Section.Begin`/`BeginNew` and `Section.SetSpeedLimit`/`Signal.SpeedLimit` rows in the Section List, 2D plan markers, and 3D scene markers. Confirm distance and parameter edits apply, parameters can be added or removed (never below one), individual parameter edits keep the untouched source argument text, method text stays read-only, each row can be deleted from its table, 2D marker, or 3D marker, and the 3D `Signal:` route overlay plus marker chunks refresh after Apply/Delete.
* Verify Apply changes the preview/working copy without writing disk, global Revert discards all pending edits after confirmation, Save writes the source file, and Reload rereads disk with the unsaved-change prompt. Keep inline list drafts applied before Save; otherwise Save is blocked.
* Cover update/delete for `station.list`, `structure.model`, `signal.aspect`, `sound.list`, `sound3D.list`, `curve`, `gradient`, `structure.put`, `structure.between`, `station.put`, `signal.put`, linked `repeater` segments, `section.begin`/`section.speedLimit`, and the supported map event/effect row kinds listed above, including an Include-owned statement and a referenced list file. Confirm `Signal.Put` deletion works from its table, 2D marker, and 3D object.
* Check numeric Curve, Gradient, Structure, Signal, Repeater, Station, sound/noise, and effect fields plus live X/Y/Z gizmo dragging; verify `Put0`/`Begin0` and short-form `Signal.Put` ask before conversion, method/argument shapes remain valid, and a distance move preserves stable edit identity.
* Run `--debug-headless-source-anchors`, `--debug-headless-station-list-edit` for Station.List work, `--debug-headless-edit-roundtrip`, `--debug-headless-own-track-edit`, `--debug-headless-other-track-edit`, `--debug-headless-distance-edit-batch`, `--debug-headless-repeater-edit-batch`, and `--debug-headless-section-edit-batch` when the change touches the corresponding source/edit path.

Minimum checks for route release export once implemented:

* Cover multiple entry maps, nested includes, variable distance expressions, constantization exclusions, shared resources, unused model/sound pruning, missing-resource reporting, and development-directory protection.

Minimum manual checks for UI/settings changes:

* Change the affected setting.
* Close and restart the app.
* Confirm `settings/settings.ini`, `settings/history.ini`, or `settings/imgui.ini` persistence still behaves as expected.

If you cannot build or run the application, state that clearly in the final response and describe what was checked statically.

## Build Script Guidelines

* Keep `build_dev.bat` and `build_release.bat` simple and Windows-friendly.
* Preserve support for `NINJA_EXE`.
* Preserve support for `VCPKG_ROOT`.
* Preserve fallback to `x64-mingw-dynamic` when `VCPKG_DEFAULT_TRIPLET` is unset.
* Do not hard-code a local vcpkg path in committed scripts.
* Keep the executable and notice files at the output root, runtime DLLs under `bin`, and INI files under `settings`.
* Preserve `bin` and `settings` when cleaning `build_release` for distribution.
* Migrate legacy root-level INI files into `settings` only when the destination is absent; abort on conflicts instead of overwriting either copy.
* `install_Assimp.bat` may require user-local editing; do not assume a universal vcpkg location.

## Dependency and License Rules

* Do not vendor Assimp into `third_party/`; it is expected to be discovered by CMake as `assimp::assimp`.
* `third_party/imgui` should come from the docking branch of `ocornut/imgui`.
* `third_party/implot` should come from `epezent/implot`.
* Do not remove or bypass `LICENSE`, `NOTICE`, or `THIRD_PARTY_NOTICES.md`.
* When changing distribution behavior, ensure license and notice files are still copied into Release output.
* If adding a dependency, update CMake, `docs/dev.md`, user documentation when applicable, and third-party notices.

## Coding Workflow

When acting as an AI coding agent in this repository:

1. Inspect the relevant files before editing.
2. Identify the component boundary: parser/DLL, editable statement model, model loader/DLL, GUI state, 2D canvas, 3D canvas, data tables, build scripts, release export, or documentation.
3. Make the smallest change that satisfies the request.
4. Do not rewrite unrelated systems.
5. Preserve public C ABI compatibility unless the task explicitly asks for an ABI change.
6. Update localization strings for UI text.
7. Update `README.md` for user behavior, `TODO.md` for implementation status, `docs/dev.md` for human development facts, `docs/ai-dev.md` for AI workflows, or this file for repository rules as applicable.
8. Prefer Debug-only validation unless Release packaging, distribution contents, route release export, or release-specific build behavior changed.
9. Report exactly what was tested.
10. If a change cannot be validated locally, explain the limitation and list the likely risk areas.
11.  If there are parts of the requirements that are difficult to implement or unclear, be sure to ask for clarification promptly; do not make decisions on your own.

## AI / Vibe-Coding Sustainability Gates

These rules apply especially to changes produced with AI coding tools. They are acceptance gates, not suggestions. The goal is to prevent rapid feature work from creating parallel implementations, speculative architecture, dead state, or hidden per-frame costs.

* Establish evidence before editing. Point to concrete call sites, profiler/benchmark output, compiler diagnostics, tests, or a reproducible defect. If the suspected problem is absent, record that result and do not manufacture a rewrite.
* Give every concern one canonical owner. Search the entire repository before adding a parser, serializer, path resolver, cache, formatter, navigation helper, validation routine, or math helper. A second implementation requires a documented compatibility reason; a third equivalent implementation is prohibited.
* Add an abstraction only when at least two real, current callers share behavior. Preserve category-specific behavior through narrow callbacks or wrappers. Do not introduce descriptor frameworks, registries, factories, fields, extension points, or configuration for hypothetical future use.
* Delete replaced implementations and now-unused fields/functions in the same change. Do not leave compatibility shims without an active caller and removal rationale. Never claim simplification by deleting useful comments, shortening names, joining statements onto one line, or moving the same complexity into generated code.
* Slop-fix and refactoring changes must reduce self-owned production code or provide measured performance/correctness value that justifies any growth. Report the before/after production-code count and explain any increase. Tests and documentation are excluded from this production-code count.
* Evaluate dependencies before building infrastructure. Record maintenance status, platform fit, binary/compile cost, license, ABI/ownership impact, and migration risk. Do not replace stable BVE, INI, DirectX, or rendering behavior solely to use a fashionable library.
* Runtime map data and edit commands must use the versioned typed ABI; do not reintroduce a text-serialized map transport, parser, fallback, or disk snapshot cache. Future standalone settings formats such as `element_presets.json` are separate from route-data transport and require their own scoped design when implemented.
* Per-frame code must not perform unconditional or avoidable file I/O, snapshot construction, full-model/table/marker reconstruction, or large allocation. Event-triggered settings/layout persistence should remain guarded and be deferred where practical. Parser and loader code must not repeatedly read, decode, hash, resolve, or generate unchanged inputs. A new repeated cost requires profiling evidence and a stated budget.
* For every new or modified cache, document or encode its complete key, exact invalidation owner, mutable-source revision, and whether view-only state belongs in the key. Prefer exact snapshots for small correctness-critical state over hashes. Add a test or benchmark that proves both invalidation and the intended hit path.
* Write a behavior-compatibility checklist before changing shared logic: typed C ABI versions/structure sizes/spans, numeric formatting, settings/INI aliases, source encoding/newlines, edit Apply/Revert/Save/Reload semantics, marker visibility/LOD/hit priority, navigation side effects, and user-operation order. Items outside the change must remain byte- or behavior-compatible where applicable.
* Keep patches component-scoped. Do not combine feature work with unrelated renaming, formatting, framework replacement, or repository-wide mechanical churn. Generated and vendored sources are outside refactoring scope unless the task explicitly targets them.
* For strict validation, configure self-owned targets with `-DKOMAPEDIT_STRICT_WARNINGS=ON` explicitly; the repository build scripts leave this option off by default. Warnings are errors and third-party targets stay excluded. Run `ctest` plus the affected Debug headless checks. Performance-sensitive work needs repeatable before/after runs on the same real route, parameters, build type, and load profile; report median and p95 and reject unexplained regression.
* Before handoff, report exact tests, benchmark inputs, skipped checks, remaining risks, production-code delta, and the count of equivalent implementations removed. Do not describe an unrun manual check as passed.

## Common Pitfalls

* Moving DLLs to `bin` without using executable-relative explicit loading and a dependency search mode that also resolves DLL dependencies from `bin`.
* Adding UI text in only one language.
* Returning memory from a DLL without a matching free function.
* Letting exceptions cross C ABI boundaries.
* Breaking `Include` path resolution when changing file-loading code.
* Mishandling CP932/Shift_JIS or UTF-16 map files.
* Adding parser support or README claims for syntax that is not official BVE Map syntax.
* Losing source anchors, raw argument text, include context, or parse order before save support can use them.
* Recomputing large track/table data every frame.
* Introducing local machine paths into build scripts.
* Accidentally committing generated build output or local ImGui/settings/history files.
* Letting route release export overwrite a development route directory or source resource directory.
* Modifying third-party source trees instead of fixing project-side integration.
