---
name: komapedit-debug-headless-validation
description: Select and run komapedit's Debug builds, registered CTests, DLL checks, and GUI-subsystem headless validation. Use when a change needs localization, parser, Scenario, file-creation, edit-roundtrip, table, plan, scene, camera, touch, load, or performance proof without manual GUI operation, especially when PowerShell process behavior or real-route safety matters.
---

# Komapedit Debug and Headless Validation

## Prepare the proof

1. Read `src/main_window/debug_headless.cpp/.h` for option parsing, `win32_dx11_bootstrap.cpp` for current dispatch and usage text, `headless_entrypoints.cpp` for App-level workflows, and `edit_benchmark.cpp` for the separate edit benchmark. Do not rely only on historical command lists.
2. Confirm the requested build scope. Default to Debug; add Release only for release-specific behavior or an explicit request.
3. Prefer fixtures for deterministic contracts. Use a real route only when it proves Include scale, resource layout, scene performance, or writeback behavior that fixtures cannot.
4. Confirm ignored `tests/` fixtures exist before interpreting diagnostics failures from a clean checkout.

## Build and run contracts

- Normal Debug: `.\build_dev.bat`.
- Strict maintenance validation: configure with `-DKOMAPEDIT_STRICT_WARNINGS=ON -DBUILD_TESTING=ON`, then build Debug.
- Registered non-headless CTest contracts:
  - `multilanguage_contract`;
  - `typed_snapshot_contract`;
  - `maploader_gradient_projection_contract`;
  - `typed_edit_contract`;
  - `maploader_diagnostics_contract`;
  - `canvas3d_camera_contract`;
  - `route_value_sampling_contract`;
- Headless validation is invoked explicitly and is never registered with CTest.
- Specialized unregistered `typed_snapshot_tests.exe` modes, such as `signal-glare`, must be invoked explicitly when relevant.

Use `ctest --test-dir build -C Debug --output-on-failure` after the tree is configured.

## Select the narrow headless mode

- Load/GUI wiring: `--headless-load-map`.
- Scenario preview/resolution/direct-save lifecycle: `--headless-load-scenario [--expect-no-map] [--scenario-edit-roundtrip]`.
- Actual App Scenario open/history/reload and Save ordering: `--debug-headless-scenario-lifecycle <scenario-path> [--scenario-index N] [--unit-distance M]`. The supplied Scenario and loaded Map sources are read-only; creation, Map/Scenario Save, and settings retry checks use exclusive temporary fixtures.
- Open latency/cache lifecycle: `--debug-headless-open-bench`.
- 2D plan/render markers: `--debug-headless-plan-bench`.
- 3D scene building/rendering: `--debug-headless-scene3d-bench`.
- Scene loader/cache/resource ownership: `--debug-headless-scene-loader-contract`.
- Scene camera/station transfer: `--debug-headless-scene-camera-transfer`.
- Diagnostics-popup scale: `--debug-headless-diagnostics-popup-bench`.
- Source metadata: `--debug-headless-source-anchors`.
- General edit lifecycle: `--debug-headless-edit-roundtrip`.
- Real-route edit timing: `--debug-headless-edit-bench <map-path> --scene off|on --repeat 5 --unit-distance 25`; use three sequential processes per scene state for comparisons. Initial models settle before timing; Save uses independent temporary copies, and every original physical source is byte/hash checked. Stage durations are inclusive, and refresh/rollback assertions are distinct from timing results.
- Own/other track, grouped distance, Station list, Station.Put margins, Repeater, Section, Curve parameters, sparse/new-element creation, or insertion edits: use the matching `--debug-headless-*-edit*` mode.
- Include deletion/replacement/import-and-create: use `--debug-headless-include-delete`, `--debug-headless-include-replace`, or `--debug-headless-include-import-create`.
- Source-backed resource-list replacement/insertion: use `--debug-headless-resource-list-replace` or `--debug-headless-resource-list-insert`; read each mode's documented interaction and commit restrictions before launch.
- Lighting Effects source editability: `--debug-headless-light-edit`; it must exercise the production preview-to-edit-metadata merge, every statement already present, and the create path for any missing optional light kind.
- Cached table search: `--debug-headless-table-find`.
- Touch state machine: `--debug-headless-touch-input`.
- Canonical settings/history persistence: `--debug-headless-settings-persistence`.
- New-map/list wizard workflow: `--debug-headless-new-file-wizard`.
- Empty-map resource drafts: `--debug-headless-fresh-resource-list-workflow <map-path>`; the input is read-only and the workflow uses an exclusive temporary Map/list directory.
- New Scenario creation and normal-open workflow: `--debug-headless-scenario-create`.

Always pass `--headless-output <file>` when the mode supports it.

## Launch the GUI-subsystem executable correctly

Use `Start-Process -Wait -WindowStyle Hidden -PassThru` from PowerShell, then inspect the output file and process exit code. Direct invocation can return misleadingly early or lose output. Keep headless code from writing `imgui.ini`; create any required ImGui/ImPlot contexts before constructing `App` state.

## Protect real routes

1. Prefer preview, memory Apply, Reset, and temporary fixtures.
2. Use `--commit` only when the test specifically requires disk writeback and the target is authorized.
3. Hash every potentially touched source before and after; verify rollback or reload results explicitly.
4. Never infer a route path from history and write to it without confirming that the current task placed it in scope.
5. The new-file-wizard headless mode requires a nonexistent Map below `tests/` and preflights every fixed resource target before creating anything. Existing targets must cause failure and remain untouched.

## Interpret results

1. Verify stage markers, counts, and `result=PASS`; a process exit alone is insufficient.
2. Separate correctness failures, tooling/process failures, missing fixtures, existing test debt, and performance-budget failures.
3. Compare performance with the same route, parameters, build type, load profile, and environment. Report median and p95 where available.
4. Use `--profile-stages` on plan/scene benchmarks only for the current stage diagnostics. Do not equate scene-overlay FPS, headless CPU-frame p95, asynchronous GPU intervals, swap-chain `Present`, or monitor refresh; they have different owners and measurement boundaries.
5. Do not lower a strict budget or use a looser rerun to claim the original gate passed.
6. State which manual visual or interaction checks remain unproven.
