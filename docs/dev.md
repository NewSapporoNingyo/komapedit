# Developer Guide

[简体中文](dev_zhcn.md) · [User guide](../README.md) · [Roadmap](../TODO.md) · [AI-assisted development](ai-dev.md)

This guide describes the setup, architecture, conventions, and validation needed for conventional human-led development of komapedit. If an AI coding tool contributes to a change, also follow [`ai-dev.md`](ai-dev.md), [`AGENTS.md`](../AGENTS.md), and the matching workflow under [`.agents/skills`](../.agents/skills).

## Scope and supported environment

komapedit is a C++17 Windows desktop application for viewing and gradually editing BVE Trainsim map files. Windows, Win32, DirectX 11, WIC, Dear ImGui, ImPlot, CMake, and Ninja are the supported architecture and workflow. Do not treat the repository as a generic cross-platform GUI project.

The application has three runtime components:

- `maploader.dll` parses maps and lists, handles includes and encodings, generates own-track and other-track geometry, and owns the versioned typed map/edit snapshots.
- `model_loader.dll` uses Assimp to load Structure meshes, materials, and textures through a C ABI.
- `komapedit.exe` provides the Win32/DirectX 11 GUI, tables, 2D charts, 3D previews, and editing workflow.

Current feature status is maintained only in [`TODO.md`](../TODO.md). Do not infer support from planned APIs or unfinished UI.

## Prerequisites

- Windows
- CMake 3.20 or newer; 3.21 or newer is recommended for the generic Assimp runtime-DLL copy fallback
- Ninja
- A C++17 compiler such as MSVC or MinGW
- Windows SDK, DirectX 11, and WIC development libraries
- Git
- Assimp discoverable by CMake as `assimp::assimp`

Fetch Dear ImGui and ImPlot:

```bat
.\get_3rd_party_packages.bat
```

Install Assimp separately. With vcpkg, set `VCPKG_ROOT` and install a matching triplet:

```bat
set VCPKG_ROOT=C:\path\to\vcpkg
%VCPKG_ROOT%\vcpkg install assimp:x64-mingw-dynamic
```

The build scripts use vcpkg automatically when `VCPKG_ROOT` is set. If `VCPKG_DEFAULT_TRIPLET` is unset, they default to `x64-mingw-dynamic`. MSVC users should explicitly select a suitable triplet such as `x64-windows`. `install_Assimp.bat` is a MinGW-oriented helper and may require a local vcpkg path; never commit that local path.

## Build and test

Debug build:

```bat
.\build_dev.bat
```

Release build:

```bat
.\build_release.bat
```

Run registered Debug tests:

```bat
ctest --test-dir build --output-on-failure
```

For strict validation, configure the Debug tree explicitly:

```bat
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DKOMAPEDIT_STRICT_WARNINGS=ON -DBUILD_TESTING=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

`KOMAPEDIT_STRICT_WARNINGS` is off in the normal scripts. The diagnostics test requires the ignored local fixtures under `tests/`; confirm that they exist before interpreting a clean-checkout failure.

Runtime output is organized as follows:

- `build\` for Debug and `build_release\` for Release.
- The output root contains `komapedit.exe`, `LICENSE`, `NOTICE`, and `THIRD_PARTY_NOTICES.md`.
- `bin\` contains `maploader.dll`, `model_loader.dll`, Assimp, and copied runtime DLLs.
- `settings\` contains application-created `settings.ini`, `history.ini`, and `imgui.ini`.

Do not commit build directories, cloned `third_party` source trees, settings files, generated CSV files, local test output, or temporary route/map/model fixtures.

## Source map

| Area | Primary files and responsibility |
| --- | --- |
| Public map ABI | `include/maploader.h`, `include/maploader_snapshot.h`: API v6 functions, fixed-width POD snapshots, edit batches, reports, spans, ownership, versions, and structure sizes |
| Map lifecycle | `src/maploader/maploader.cpp`: C ABI entry points, handles, regeneration, dispatch, source retrieval, and boundary error handling |
| Map state | `src/maploader/maploader_internal.h`: `MapContext`, parsed rows, source spans, include stacks, edit references, reports, and timing |
| Parsing | `maploader_core.cpp`, `maploader_parser.cpp`, `text_decoder.cpp/.h`: statements, values, includes, variables, encodings, source anchors, uniqueness checks |
| Geometry | `maploader_geometry.cpp`: own/other-track geometry, relocation, curves, gradients, placement buffers, scene control points |
| Identity and snapshots | `maploader_identity.cpp`, `maploader_snapshot.cpp`, `maploader_semantic.cpp`: stable IDs, typed snapshots, revisions, comparisons, fingerprints |
| Editing | `maploader_edits.cpp`: dry run, in-memory Apply, direct Apply, commit, reset, source patching, encoding-aware writeback, distance adjustment |
| Shared linkage | `include/repeater_linkage.h`, `include/own_track_transition_linkage.h`: Repeater chains and Curve/Gradient transition pairing |
| Model loading | `src/model_loader/model_loader.cpp`, `include/model_loader.h`: Assimp isolation and model-loader API v2 |
| Main window | `src/main_window/gui_kme.cpp`, `kme.h`: application loop, menus, loading, edit inspector, Apply/Save/Revert/Reload, shared GUI state |
| Runtime/settings | `app_settings.cpp/.h`, `runtime_paths.cpp/.h`, `maploader_runtime.cpp`: INI persistence, executable-relative paths, DLL loading, exact API checks |
| Source tools | `file_structure_diagram.cpp`, `text_preview.cpp`: Include graph, working-copy source preview, source actions, distance-boundary selection |
| Debug validation | `debug_headless.cpp/.h`, `touch_input.cpp/.h`: headless contracts, benchmarks, camera transfer, find, and touch checks |
| 2D views | `src/canvas2d/canvas2D.cpp`, `profile_plots.cpp`: plan, charts, transforms, markers, measurement, background images |
| 3D views | `src/canvas3d/canvas3D.cpp`, `include/canvas3D.h`: model/scene rendering, camera, picking, markers, overlays, gizmos |
| Tables/navigation | `src/table/datatable.cpp`, `table_navigation.cpp`: cached tables, inline editing, find, row/plan/scene navigation |
| Shared marker visuals | `include/map_marker_visuals.h`, `src/main_window/map_marker_visuals.cpp`: canonical 2D/3D marker recipes |
| Localization | `include/multilanguage.h`: Simplified Chinese, English, and Japanese UI strings |

Use existing boundaries and shared helpers. Do not duplicate source ownership, linkage, marker recipes, navigation, parsing, validation, or writeback logic inside a single UI path.

## Core engineering rules

### C++ and ABI

- Use C++17 and prefer small, focused changes.
- Prefer RAII, standard containers, `std::filesystem`, and narrow internal helpers.
- Preserve `UNICODE`, `_UNICODE`, `NOMINMAX`, and `WIN32_LEAN_AND_MEAN` assumptions.
- Never let exceptions, STL types, C++ classes, or ownership-ambiguous pointers cross a public C ABI.
- Memory allocated by a DLL and returned through the ABI must have a matching free function.
- The bundled EXE requires exact maploader API v6 and model-loader API v2 matches.
- Treat typed ABI inputs as call-scoped views. Nested snapshot storage is handle-owned and is invalidated according to its documented regular-geometry, scene-geometry, edit-operation, reset, reparse, and free rules.
- Any public ABI change requires an explicit version/structure-size decision, synchronized EXE/DLL changes, updated callers, and documented ownership and validity.

### Parsing, geometry, and source fidelity

Preserve BVE Map 2.0+, supported legacy syntax, `Include`, variables, predefined `distance`, math functions, comments, and UTF-8/BOM, UTF-16LE/BE, and CP932/Shift_JIS-related input.

Implement general rules matching official BVE syntax; do not add private route syntax or special-case one route. Presets must emit ordinary BVE map/list statements.

Editable rows must retain source path, include stack, source span, original statement and arguments, evaluated values, distance expression, parse order, and stable ID. Keep `KvMapSnapshot` exhaustive and strongly typed. Source writeback preserves original encoding and line endings where possible and blocks unrepresentable characters; there is no Save-as-UTF-8 fallback.

### Editing model

- Source ownership stays in maploader structures; never reconstruct anchors from GUI table text or create a parallel GUI document model.
- Preview and Edit hydration are separated only by the existing capability bits.
- `kv_edit_dry_run_typed()` validates, `kv_edit_apply_to_memory_typed()` updates the working-copy preview, `kv_edit_apply_typed()` is the direct write path, `kv_edit_commit_typed()` saves the validated working copy, and `kv_edit_reset_memory()` discards overrides only when needed.
- Apply must not write to disk. Save must not silently reload. Revert and Reload keep their current confirmation and disk semantics.
- `sourceHash` identifies the working copy; `expectedSourceHash` remains the disk concurrency baseline through repeated in-memory edits.
- Batch distance moves by source file, Include context/section, and target distance. Preserve statement order and user-authored comments or empty distance structure.
- Fully reparse before Apply or Save, prove target semantic values, and reject unexpected changes to non-target elements or the final variable/distance environment.
- Preserve statement method and argument shapes except for explicit Inspector actions: the coordinate-offset buttons convert `Structure.Put`/`Put0` and `Repeater.Begin`/`Begin0` in either direction (confirming before nonzero offsets are discarded), while short `Signal.Put` and Repeater trim keep their existing confirmation flows.
- Use shared inline-table drafts for loaded Station, Structure, Signal, Sound, and Sound3D rows. They do not insert new resource rows.
- Use `repeater_linkage` and transition-linkage rules across maploader, tables, 2D, and 3D instead of recreating chains locally.
- Repeater lifetimes are half-open `[first Begin, End)` intervals; an End at the same distance is ordered before every Begin regardless of source order. A `repeaterKey` rename is one typed batch containing every Begin/Begin0 and explicit End in that chain, and maploader rejects incomplete batches or canonical same-name intervals that overlap.
- An other-track `trackKey` rename is one typed batch containing every surviving `Track[trackKey].*` statement with the same case-insensitive, type-preserving key across the root map and Includes. Maploader rejects incomplete batches and any final key already owned by another other track, without distance/interval exceptions; dependent map-element track references remain non-target rows.

### UI, tables, and rendering

- Keep the Dear ImGui docking layout and current menu/tool concepts.
- Add every user-visible string in Simplified Chinese, English, and Japanese, with concise toolbar/menu wording.
- Keep diagnostics in the existing console, in English by default.
- Store real preferences in `settings/settings.ini`, recent-map/background alignment in `settings/history.ini`, and layout in `settings/imgui.ini`.
- Preserve plan pan/zoom/rotation/fit, measurement, grids, station jumps, coordinate transforms, marker synchronization, context actions, and background-image alignment.
- Keep table content cached, dynamic Section arguments and explicit `null` values intact, Variable List ordering stable, and row/plan/scene navigation side effects synchronized.
- Keep Assimp isolated in `model_loader.dll`; handle missing textures, invalid files, and unsupported models without crashing.
- Preserve scene camera transfers, picking/highlights, visibility synchronization, marker recipes, route overlay, X/Y/Z gizmo synchronization, and the exact coalesced vertex preview used by the Z-only `Structure.PutBetween` distance gizmo.

### Performance

- Avoid repeated reads, decoding, conversion, hashing, resolution, geometry generation, and O(n²) traversal of large track arrays.
- Keep large arrays contiguous where practical and avoid allocation in tight geometry or per-frame paths.
- Do not rebuild unchanged snapshots, tables, marker chunks, labels, or model data each frame.
- Use asynchronous loading for long parser/model work and keep the UI responsive with status feedback.
- Every cache needs a complete key, an exact invalidation owner and revision, and coverage for both invalidation and the intended hit path.

## Validation

Choose checks by the affected component and report exactly what ran. Do not describe an unrun manual check as passed.

Useful Debug headless commands include:

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
build\komapedit.exe --debug-headless-repeater-key-edit <map-path> [--commit] --headless-output build\repeater-key-edit.txt
build\komapedit.exe --debug-headless-other-track-key-edit <map-path> [--commit] --headless-output build\other-track-key-edit.txt
build\komapedit.exe --debug-headless-insert-edit [map-path] --repeater-only [--commit] --headless-output build\repeater-insert-edit.txt
build\komapedit.exe --debug-headless-new-element-edit <map-path> --headless-output build\new-element-edit.txt
build\komapedit.exe --debug-headless-section-edit-batch [map-path] [--commit] --headless-output build\section-edit-batch.txt
build\komapedit.exe --debug-headless-table-find --headless-output build\headless-table-find.txt
build\komapedit.exe --debug-headless-touch-input --headless-output build\headless-touch-input.txt
build\bin\typed_snapshot_tests.exe signal-glare <map-path> [--commit]
```

Pass explicit map paths for portable runs. The Repeater-key and new-element-follow-up commands always require one; the own-track, other-track, distance, Repeater-batch, Repeater-only insert, and Section tools otherwise fall back to a developer-machine route path. `--repeater-only` validates exactly one uniquely keyed `Repeater.Begin` and one `Begin0` through dry run, memory Apply/Reset, and, when requested, Commit/reload. Repeater-key and Repeater-only commit validation leave the authorized route edits in place for physical diff inspection.

`--debug-headless-other-track-key-edit` requires an explicit map path. It selects a string-key other track with at least two statements, checks whole-track atomicity and global duplicate rejection, then performs dry run, memory Apply, Reset, a second Apply, and reload. `--commit` writes the validated working copy and intentionally leaves authorized route changes in place for physical diff inspection; the report includes the old/new keys, every target, changed files, dependency-reference preservation, and source hashes.

`--debug-headless-new-element-edit` drives the production New Map Element wizard, Inspector Apply, and delete/cancel paths. It creates a paired Repeater, changes a normal Begin parameter, the End distance, and the key, cancels the chain, then repeats the follow-up edit/cancel flow for a single-row Structure element. The command is intentionally memory-only: it rejects `--commit`, resets the working copy, reloads from disk, and reports ledger members, stable edit IDs, semantic row checks, and source hashes before returning PASS/FAIL.

At minimum, validate the affected combination of normal and Include map loading, reload, plan/profile/radius charts, station jump, measurement, CSV export, model preview/error handling, 3D tracks/objects/markers/camera/overlay, edit Apply/Revert/Save/Reload, source round trip, encodings/newlines, inline-list drafts, settings persistence, and release contents. Changes to disk writeback require save-then-reload comparison. Performance-sensitive changes require repeatable before/after runs on the same route, parameters, build type, and load profile.

Because `komapedit.exe` is a GUI-subsystem executable, PowerShell validation that captures output should launch it with `Start-Process -Wait -WindowStyle Hidden -PassThru` and `--headless-output`.

## Build scripts, dependencies, and distribution

- Keep CMake as the source of truth and keep the batch scripts simple and Windows-friendly.
- Preserve `NINJA_EXE`, `VCPKG_ROOT`, and the `x64-mingw-dynamic` fallback.
- Keep the EXE and notice files at the output root, DLLs in `bin`, and INIs in `settings`.
- Distribution cleanup preserves `bin`, `settings`, `LICENSE`, `NOTICE`, and `THIRD_PARTY_NOTICES.md`.
- Legacy root INIs move into `settings` only when the destination is absent; conflicts abort without overwriting.
- Fetch ImGui from its docking branch and ImPlot from upstream.
- Never remove or bypass license/notice files. A new dependency requires CMake, developer documentation, and third-party notice updates.
- Route release export is separate from `build_release.bat`; when implemented it must expand includes, optionally constantize expressions, copy only used resources, report results, and protect development directories through temporary output.

