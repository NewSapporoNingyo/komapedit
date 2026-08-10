# AGENTS.md — komapedit Repository Rules

This file is the entry point for AI coding tools working in this repository. It contains non-negotiable constraints, the general change workflow, and indexes to detailed project skills and memories. Do not expand it into an exhaustive implementation manual.

## Authority and document ownership

Resolve conflicts in this order:

1. the current user request;
2. this file's non-negotiable repository constraints;
3. current source code, public headers, tests, and build scripts;
4. current project documentation and `TODO.md`;
5. `.agents/memories`, which is historical evidence only.

Use the documents by ownership:

- `README.md` / `docs/README_zhcn.md`: user-visible behavior, setup, usage, supported syntax, and limitations.
- `TODO.md`: unfinished work only; `docs/TODO_done.md`: completed-work archive.
- `docs/dev.md` / `docs/dev_zhcn.md`: human development setup, architecture, source map, and validation details.
- `docs/ai-dev.md` / `docs/ai-dev_zhcn.md`: guidance for people operating AI coding tools.
- `.agents/skills/*`: repeatable AI execution workflows.
- `.agents/memories/*`: durable lessons and historical task evidence.

Current implementation status comes from source and `TODO.md`, not from an old memory or an exhaustive list copied into this file.

## Non-negotiable project constraints

### Platform and architecture

- Use C++17. Windows, Win32, DirectX 11, WIC, Dear ImGui, ImPlot, CMake, and Ninja are the supported application architecture and workflow.
- Preserve the three runtime components: `maploader.dll`, `model_loader.dll`, and `komapedit.exe`.
- Do not replace the GUI stack or introduce Qt, wxWidgets, SDL, GLFW, Electron, Tauri, another build system, or another package manager unless the task explicitly requires an approved architectural change.
- Keep Assimp isolated inside `model_loader.dll` where practical. Do not leak Assimp types through the GUI-facing ABI.
- Do not edit vendored `third_party` sources unless the task explicitly targets them.

### BVE parsing, geometry, and source fidelity

- Implement official BVE map/list syntax; do not add private route syntax extensions.
- Preserve currently supported encodings, BOMs, line endings, Include behavior, variables/expressions, parse order, comments, legacy syntax where already supported, and existing geometry semantics.
- Keep source ownership in maploader records: physical source path, Include stack, source span, original statement/arguments, evaluated values, distance expression, global parse order, and stable edit identity.
- Do not reparse route source in GUI tables/canvases or create a parallel GUI-owned source document model.
- Source writeback must preserve original encoding and line endings where possible. If new text is not representable, block the write; do not silently convert to UTF-8.
- Prefer general format rules over route-specific special cases. Presets must emit ordinary BVE statements.

### Public C ABI and typed snapshots

- `maploader.dll` uses the exact versioned typed C ABI in `include/maploader.h` and `include/maploader_snapshot.h`; `model_loader.dll` uses its documented v2 C ABI.
- Never expose STL types, C++ classes, exceptions, or ownership-ambiguous pointers across a C ABI boundary. Catch exceptions at the boundary.
- Every DLL allocation returned to a caller requires the matching DLL free function.
- Treat typed ABI inputs as call-scoped views. Respect documented snapshot, scene snapshot, edit-target, and report ownership/lifetime invalidation.
- Public ABI changes require an explicit version/structure-size decision, synchronized EXE/DLL updates, call-site updates, ownership documentation, and contract tests. Do not add legacy fallbacks to hide a version mismatch.
- Runtime map data and edit commands must remain strongly typed. Text/JSON route transport, fallback models, alternate parsers, or disk snapshot caches require explicit human approval and a task that genuinely needs the architecture change.

### Source-backed editing contract

- Extend the shared source-first path (`SourceSpan`, parsed rows/statements, `EditSourceRef`, typed edit batches/reports). Do not duplicate string rewrite logic in UI features.
- Preserve operation roles: dry run validates; memory Apply reparses the working copy for preview; direct Apply is the existing direct-write path; commit saves the validated working copy; reset discards overrides only when needed.
- Properties/Edit `Apply` must not write disk. Toolbar/Ctrl+S `Save` commits. `Revert` discards pending in-memory changes. `Reload` rereads disk after unsaved-change confirmation.
- Pending inline-list drafts must be applied in their table before Save is allowed.
- `sourceHash` identifies the working copy. `expectedSourceHash` remains the disk-baseline concurrency guard across repeated Apply/Delete operations.
- Before Apply or Save succeeds, fully reparse the patched sources, prove each target semantic value, and reject unexpected non-target or variable/distance-environment changes.
- Preserve stable statement order and source structure. Use shared linkage helpers for Repeater and own-track transition chains.
- Insertions must use structured fields and the same distance/environment/full-reparse validation as updates; do not accept arbitrary replacement source as an insertion shortcut.

### UI, tables, 2D, and 3D behavior

- Preserve the Dear ImGui docking/window model, shared menu concepts, cross-view navigation, current editing lifecycle, and table/plan/scene identity.
- Update Simplified Chinese, English, and Japanese together for visible UI text. Keep console diagnostics English by default.
- Store application preferences in `settings/settings.ini`, recent-map/per-map history in `settings/history.ini`, and Dear ImGui layout in `settings/imgui.ini`; do not invent overlapping sidecar files.
- Cache table rows, marker layouts, and scene data outside per-frame loops. Attach stable edit/navigation metadata during hydration/cache construction rather than ambiguous per-frame lookups.
- Keep 2D pan/zoom/rotation, grid/background alignment, measurement, station/distance jump, markers, and navigation behavior unless the task explicitly changes them.
- Keep 3D camera jumps, picking/highlights, route overlays, track visibility, Structure/Signal/Repeater gizmos, model loading, and shared marker visuals consistent. Source-form conversions such as `Put0`, `Begin0`, or short `Signal.Put` require the existing explicit confirmation path.
- Perform Inspector/delete state mutation through deferred requests, not while an ImGui table or popup is rendering.

### Performance, concurrency, and safety

- Avoid repeated file reads, decoding, hashing, typed conversion, allocation, geometry generation, table rebuilding, or per-frame I/O when inputs have not changed.
- Avoid accidental O(n²) work on large route arrays. Keep large data contiguous and cache repeated key/path lookups where practical.
- Give every cache a complete key and an explicit invalidation owner. Do not disable caching globally to hide an invalidation bug.
- Keep long parsing/model work off the UI thread or provide the existing progress/diagnostic behavior.
- Preserve C-boundary error handling, numeric range checks, async resource limits, and graceful handling of missing/invalid models and textures.
- Performance claims require comparable before/after measurements on the same route, parameters, build type, and load profile. Do not lower strict budgets to manufacture a pass.

### Dependencies, distribution, and repository hygiene

- New dependencies require an explicit need, Windows/toolchain compatibility review, CMake integration, developer documentation, license/notice updates, and runtime packaging verification.
- Preserve `LICENSE`, `NOTICE`, and `THIRD_PARTY_NOTICES.md` unless a dependency/license task requires a synchronized update.
- Do not commit `build/`, `build_release/`, cloned `third_party` trees, generated settings, CSV/test outputs, or temporary route/map/model fixtures.
- Preserve unrelated user changes in a dirty worktree. Never use destructive Git recovery commands without explicit authorization.

## General workflow

1. Read this file and inspect `.agents/skills` descriptions. Use the matching scenario skill first, then every narrower skill that applies.
2. Read the relevant section of `docs/dev.md`, `TODO.md`, current source, public headers, tests, and current working-tree diff.
3. Convert the request into observable acceptance criteria and identify the owning component boundary.
4. Inspect before editing. For bugs and slop-fix work, establish evidence and a plan before mutation.
5. Implement the smallest coherent change in the owning layer; extend shared typed/state paths before consumers.
6. Add or update focused coverage when the behavior has a deterministic contract.
7. Build and validate in proportion to risk. Default to Debug; add Release only when justified.
8. Synchronize the documents owned by the changed behavior/status/workflow.
9. Review the final diff for scope, ABI/source fidelity, localization, cache/performance impact, generated files, encoding, and whitespace.
10. Report the outcome, validation evidence, known failures, and remaining manual checks without overclaiming.
11. If modified the code or documentation, generate a Git commit message based on the actual changes, using the format “Change Type: What Was Done.”

Stop and request a human decision before silently choosing a new file-format strategy, compatibility break, public ABI migration, dependency, framework, or other architecture change not determined by the task.If there are any discrepancies between the requirements specification and the actual code, or if the description is unclear,also stop and ask for human decision.

## Component routing

Use `docs/dev.md` for the detailed source map. Start with these owners:

| Area | Primary owners |
| --- | --- |
| Public map/model ABI | `include/maploader*.h`, `include/model_loader.h` |
| Parser/source records/snapshots/edits | `src/maploader/` by module responsibility |
| Model import | `src/model_loader/model_loader.cpp` |
| Main GUI/settings/runtime loading | `src/main_window/` |
| 2D plan/profile | `src/canvas2d/` |
| 3D model/scene preview | `src/canvas3d/canvas3D.cpp`, `include/canvas3D.h` |
| Tables/navigation | `src/table/` |
| Shared marker/linkage rules | `include/map_marker_visuals.h`, linkage headers, corresponding sources |
| Contract/headless validation | `src/maploader/tests/`, `src/main_window/debug_headless.*` |

## Project skill index

All project skills live under `.agents/skills/<name>/SKILL.md`.

| Skill | Use for |
| --- | --- |
| `komapedit-develop` | Everyday scoped feature/behavior development and workflow routing |
| `komapedit-fix` | Evidence-led diagnosis and minimal repair of a concrete defect |
| `komapedit-slop-fix` | Sustainability audit and evidence-backed cleanup without behavior drift |
| `komapedit-write-docs` | Documentation ownership, synchronization, bilingual updates, and AGENTS/TODO maintenance |
| `komapedit-source-backed-editing` | Typed editable rows, source spans, Apply/Save, insertion/deletion, writeback, and edit ABI |
| `komapedit-debug-headless-validation` | Debug/CTest/headless command selection, real-route safety, and result interpretation |
| `komapedit-3d-preview-workflow` | Canvas3D, scene geometry/cache, models, markers, gizmos, camera/depth, and packaging |
| `komapedit-table-feature-workflow` | Typed snapshot table features, caching/find, and table↔plan/scene navigation |
| `komapedit-station-edit-workflow` | `Station.List`/`Station.Put` identity, drafts, source order, markers, and writeback |
| `komapedit-ui-persistence` | `settings.ini`, `history.ini`, `imgui.ini`, defaults, migrations, and visibility state |
| `komapedit-trilingual-ui-menu-change` | Focused EN/ZH/JA UI/menu/label/link changes |
| `komapedit-doc-sync-validation` | Final doc-only scope, parity, encoding, table, and link validation |

## Build and validation baseline

Initial third-party setup uses `.\get_3rd_party_packages.bat`; install Assimp for the active compiler/triplet.

```bat
.\build_dev.bat
.\build_release.bat
```

- Debug output: `build\`; Release output: `build_release\`.
- Normal changes use Debug first. Release is required only for release packaging/distribution, runtime dependency layout, optimization-specific behavior, or explicit request.
- Strict self-owned warnings require an explicit configure with `-DKOMAPEDIT_STRICT_WARNINGS=ON -DBUILD_TESTING=ON`.
- Registered contracts are `typed_snapshot_contract`, `maploader_gradient_projection_contract`, `typed_edit_contract`, and `maploader_diagnostics_contract`.
- The diagnostics contract needs ignored local fixtures under `tests/`; verify availability before interpreting a clean-checkout failure.
- Use `komapedit-debug-headless-validation` for current headless commands. Because `komapedit.exe` is a GUI-subsystem executable, PowerShell capture must use `Start-Process -Wait -WindowStyle Hidden -PassThru` and `--headless-output`.
- Separate build/contract/headless evidence from unperformed manual GUI or visual checks.

## Project memory index

Start at [`.agents/memories/INDEX.md`](.agents/memories/INDEX.md). Read only the topic relevant to the task:

- [Source-backed editing](.agents/memories/source-backed-editing.md)
- [Rendering and navigation](.agents/memories/rendering-and-navigation.md)
- [Tables, UI, and settings](.agents/memories/tables-ui-and-settings.md)
- [Validation and maintenance](.agents/memories/validation-and-maintenance.md)
- [Build, Release, runtime, and documentation](.agents/memories/build-release-and-docs.md)

A local `.agents/memories/rollout-summaries/` archive may exist for the current user. It is Git-ignored and must not be committed because it can contain user-specific operating preferences, Codex task/session metadata, absolute local file paths, and environment-specific commands or validation records. When present, search it narrowly and treat it only as historical evidence, never as current truth.

After a code change, update only the shareable curated project memories above when the change creates a durable, reusable lesson. Do not copy raw task transcripts or user-specific data into tracked memories.
