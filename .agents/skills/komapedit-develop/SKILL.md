---
name: komapedit-develop
description: Plan and implement scoped feature or behavior changes in komapedit. Use for everyday development that adds or extends parser behavior, typed snapshots, source-backed editing, tables, 2D/3D views, GUI behavior, build scripts, packaging, or related tests; combine it with a narrower komapedit skill when one matches the task.
---

# Komapedit Development

## Establish the contract

1. Read `AGENTS.md`, the relevant part of `docs/dev.md`, and `TODO.md` before editing.
2. Treat current source and tests as implementation truth. Treat memories as historical evidence, not current specification.
3. Inspect the working tree and preserve unrelated user changes.
4. Convert the request into observable acceptance criteria. Ask only when a missing product or compatibility decision would materially change the result.

## Route the work

- Use `komapedit-source-backed-editing` for parser-to-save editing work.
- Use `komapedit-table-feature-workflow` for typed snapshot rows, Map Info tables, table search, and cross-view navigation.
- Use `komapedit-3d-preview-workflow` for scene/model preview work.
- Use `komapedit-station-edit-workflow` for `Station.List` or `Station.Put` behavior.
- Use `komapedit-ui-persistence` for settings, visibility, docking, and persisted controls.
- Use `komapedit-trilingual-ui-menu-change` for small localized UI/menu changes.
- Use `komapedit-debug-headless-validation` to select the smallest convincing runtime proof.

Read every triggered skill before changing files.

## Implement a vertical slice

1. Trace the owning path from input to output before patching. Respect the existing module boundaries documented in `docs/dev.md`.
2. Plan a small, coherent slice. Avoid parallel parsers, GUI-owned source models, duplicated state machines, and per-frame reconstruction.
3. Extend the shared representation first, then its consumers. Typical flows are:
   - parser → `KvMapSnapshot` → `MapModel` → table/2D/3D consumers;
   - source metadata → edit target → semantic validation → source patch → full reparse → GUI refresh.
4. Preserve the public C ABI rules, source encodings, official BVE syntax, stable edit identities, UI lifecycle, and cache invalidation contracts.
5. Update Simplified Chinese, English, and Japanese together when visible GUI text changes.
6. Add focused regression coverage when the change has a deterministic contract. Do not invent a large new harness if an existing CTest or Debug headless entry point proves the path.

## Validate proportionately

1. Build Debug by default with `.\build_dev.bat` or the already configured equivalent.
2. Run affected CTests and the narrowest relevant headless check. Use a real route only when it adds evidence beyond fixtures.
3. Build Release only for release-specific behavior, packaging, runtime DLL layout, optimization-sensitive bugs, or an explicit request.
4. Separate compile/headless evidence from manual GUI acceptance. Never claim an unperformed visual check passed.
5. If a performance gate fails, report the measured result; do not lower the threshold or substitute a looser run as a pass.

## Finish the change

1. Update documentation according to ownership: user behavior in `README.md`, unfinished status in `TODO.md`, completed work in `docs/TODO_done.md`, human development facts in `docs/dev*.md`, and reusable AI workflows in `.agents/skills`.
2. Run `git diff --check` and review the final diff for unrelated churn, encoding damage, generated files, and accidental ABI changes.
3. Report the outcome first, then changed behavior, validation performed, remaining manual checks, and an English commit-title suggestion when useful.
