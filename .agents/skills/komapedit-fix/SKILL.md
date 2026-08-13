---
name: komapedit-fix
description: Diagnose and repair a specific komapedit defect or regression. Use for crashes, incorrect parsing or geometry, broken source editing, wrong table/marker/navigation behavior, UI state regressions, performance regressions, build failures, or Release-only failures when the user wants the cause fixed rather than a new feature.
---

# Komapedit Bug Fix

## Prove the problem

1. Read `AGENTS.md` and inspect the exact code path before changing anything.
2. For any syntax-related reading, parsing, validation, editing, creation, serialization, or writeback defect, use `komapedit-bve-format-compliance` to establish the live official contract before deciding what is incorrect.
3. Record expected behavior, observed behavior, inputs, build type, and the narrowest reproducible condition.
4. Reproduce with the smallest existing fixture, CTest, headless mode, direct DLL call, or controlled real route that exercises the failure.
5. If the request is diagnosis-only, stop after establishing the cause and evidence. Do not implement without authorization.

## Find the owning boundary

- Parsing/source anchors: `src/maploader/maploader_parser.cpp` and maploader-owned records.
- Geometry: `src/maploader/maploader_geometry.cpp`.
- Typed transport/snapshot lifetime: `maploader_snapshot.cpp`, `include/maploader_snapshot.h`, and runtime dispatch.
- Source edits: `maploader_edits.cpp`, `maploader_semantic.cpp`, and the GUI pending-edit lifecycle.
- Tables/navigation: `src/table/` and cached `MapModel` data.
- 2D/3D behavior: the relevant canvas plus shared marker/linkage helpers.
- Settings/window lifecycle: `app_settings.*`, `gui_kme.cpp`, and `kme.h`.
- Release/runtime dependencies: CMake, build scripts, runtime paths, and actual toolchain cache.

Trace the value or state through its full owner chain. Do not patch the visible symptom in a downstream view when the invariant is broken upstream.

## Apply the smallest proven repair

1. Prefer a focused fix in the owning layer and reuse the existing shared path.
2. Preserve public ABI, official BVE syntax proven by the compliance matrix, source fidelity, Apply/Save semantics, stable IDs, localization, and current user interaction unless the bug is in that contract.
3. Add a regression check that fails for the original cause when practical. Avoid weakening global validation to make one fixture pass.
4. Keep unrelated failures separate. Do not widen the patch because another test exposes independent debt.
5. For caches, repair the key or invalidation owner; do not disable caching globally without evidence.
6. For ImGui context actions, keep mutation deferred until the active table/popup render has completed.

## Verify the repair

1. Re-run the original reproduction.
2. Build Debug and run the affected contract/headless checks. Use `komapedit-debug-headless-validation` for command selection.
3. Test Release only when the defect is Release-specific, optimization-sensitive, or packaging-related.
4. Compare before/after behavior using identical route, parameters, build type, and load profile for performance issues.
5. Verify source hashes or a temporary copy around any real-route writeback test.

## Report with evidence

State the root cause, why the fix belongs in that layer, the exact validation that passed, any unrelated failure, and any manual GUI check still required. Do not describe compilation alone as proof that a runtime or visual defect is fixed.
