---
name: komapedit-debug-headless-validation
description: Select and run komapedit's Debug builds, registered CTests, DLL checks, and GUI-subsystem headless validation. Use when a change needs parser, edit-roundtrip, table, plan, scene, camera, touch, load, or performance proof without manual GUI operation, especially when PowerShell process behavior or real-route safety matters.
---

# Komapedit Debug and Headless Validation

## Prepare the proof

1. Read `src/main_window/debug_headless.cpp` for current options; do not rely only on historical command lists.
2. Confirm the requested build scope. Default to Debug; add Release only for release-specific behavior or an explicit request.
3. Prefer fixtures for deterministic contracts. Use a real route only when it proves Include scale, resource layout, scene performance, or writeback behavior that fixtures cannot.
4. Confirm ignored `tests/` fixtures exist before interpreting diagnostics failures from a clean checkout.

## Build and run contracts

- Normal Debug: `.\build_dev.bat`.
- Strict maintenance validation: configure with `-DKOMAPEDIT_STRICT_WARNINGS=ON -DBUILD_TESTING=ON`, then build Debug.
- Registered non-headless CTest contracts:
  - `typed_snapshot_contract`;
  - `maploader_gradient_projection_contract`;
  - `typed_edit_contract`;
  - `maploader_diagnostics_contract`;
  - `canvas3d_camera_contract`;
- Headless validation is invoked explicitly and is never registered with CTest.
- Specialized unregistered `typed_snapshot_tests.exe` modes, such as `signal-glare`, must be invoked explicitly when relevant.

Use `ctest --test-dir build -C Debug --output-on-failure` after the tree is configured.

## Select the narrow headless mode

- Load/GUI wiring: `--headless-load-map`.
- Open latency/cache lifecycle: `--debug-headless-open-bench`.
- 2D plan/render markers: `--debug-headless-plan-bench`.
- 3D scene building/rendering: `--debug-headless-scene3d-bench`.
- Scene camera/station transfer: `--debug-headless-scene-camera-transfer`.
- Source metadata: `--debug-headless-source-anchors`.
- General edit lifecycle: `--debug-headless-edit-roundtrip`.
- Own/other track, grouped distance, Station list, Repeater, Section, or insertion edits: use the matching `--debug-headless-*-edit*` mode.
- Lighting Effects source editability: `--debug-headless-light-edit`; it must exercise the production preview-to-edit-metadata merge, every statement already present, and the create path for any missing optional light kind.
- Cached table search: `--debug-headless-table-find`.
- Touch state machine: `--debug-headless-touch-input`.
- Canonical settings/history persistence: `--debug-headless-settings-persistence`.

Always pass `--headless-output <file>` when the mode supports it.

## Launch the GUI-subsystem executable correctly

Use `Start-Process -Wait -WindowStyle Hidden -PassThru` from PowerShell, then inspect the output file and process exit code. Direct invocation can return misleadingly early or lose output. Keep headless code from writing `imgui.ini`; create any required ImGui/ImPlot contexts before constructing `App` state.

## Protect real routes

1. Prefer preview, memory Apply, Reset, and temporary fixtures.
2. Use `--commit` only when the test specifically requires disk writeback and the target is authorized.
3. Hash every potentially touched source before and after; verify rollback or reload results explicitly.
4. Never infer a route path from history and write to it without confirming that the current task placed it in scope.

## Interpret results

1. Verify stage markers, counts, and `result=PASS`; a process exit alone is insufficient.
2. Separate correctness failures, tooling/process failures, missing fixtures, existing test debt, and performance-budget failures.
3. Compare performance with the same route, parameters, build type, load profile, and environment. Report median and p95 where available.
4. Do not lower a strict budget or use a looser rerun to claim the original gate passed.
5. State which manual visual or interaction checks remain unproven.
