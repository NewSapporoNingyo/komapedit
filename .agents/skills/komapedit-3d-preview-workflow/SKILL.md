---
name: komapedit-3d-preview-workflow
description: Add, diagnose, or repair komapedit 3D model/scene preview behavior. Use for Canvas3D rendering, scene geometry snapshots, camera/depth/fog, Structure/Signal/Repeater placement and gizmos, map markers, picking/navigation, route overlays, scene caches, model_loader.dll, Assimp integration, 3D settings, or runtime model dependency packaging.
---

# Komapedit 3D Preview Workflow

## Locate the owning layer

- Main-window wiring, Inspector refresh, menu/dock state: `src/main_window/gui_kme.cpp` and `kme.h`.
- Scene/model rendering, camera, picking, overlays, gizmos, caches: `src/canvas3d/canvas3D.cpp` and `include/canvas3D.h`.
- Scene geometry generation and revisions: `src/maploader/maploader_geometry.cpp`, snapshot code, and `MapContext`.
- Shared symbols: `src/main_window/map_marker_visuals.cpp` and `include/map_marker_visuals.h`.
- Structure model import/materials/textures: `src/model_loader/model_loader.cpp` and `include/model_loader.h`.
- Other-track/list navigation: `src/table/` and `table_navigation.cpp`.
- Runtime DLL layout: CMake, build scripts, and `runtime_paths.*`.

Determine whether the defect is UI wiring, scene-cache data, geometry/placement semantics, rendering precision/depth, model import, or packaging before patching.

## Preserve scene invariants

1. Keep 3D scene functionality shared in `Canvas3D`; do not add one-off GUI overlays or render paths.
2. Consume typed scene/map snapshots. Keep geometry/cache ownership at the maploader or Canvas3D boundary and invalidate it from the state owner.
3. Build map-marker recipes and edit/navigation metadata during scene-cache construction. Visibility toggles should update marker visibility/index data without a full geometry rebuild.
4. Preserve camera jumps, table↔scene navigation, object/marker picking, hover/selection separation, highlight outlines, and context-popup state.
5. Use deferred Inspector/delete requests; never mutate GUI tables or Inspector state while an ImGui popup is rendering.
6. Bind stable `edit_id`/row kind directly. Do not resolve identical-distance objects with an ambiguous lookup.
7. Keep placement syntax exact: empty key or `0` is own track where the current placement contract defines it; `1` remains a real other-track key. Do not leak broad aliases into placement lookup.
8. Require the documented explicit conversion before editing `Put0`, `Begin0`, or short-form `Signal.Put` extended coordinates.
9. Preserve reversed-Z/camera-relative or chunk-local precision behavior, depth clear/compare pairing, fog, draw distance, transparent material ordering, and fallback warnings.
10. Keep route-overlay fields mapped to their actual total/current statistics; verify labels before changing geometry.

## Handle models and dependencies

1. Keep Assimp types inside `model_loader.dll`; maintain explicit v2 C ABI ownership and matching free paths.
2. Inspect `CMakeCache.txt` before diagnosing ABI/toolchain issues. MinGW/TDM-GCC requires a compatible Assimp triplet such as `x64-mingw-dynamic`; an MSVC package is not ABI-compatible.
3. Ensure `model_loader.dll`, Assimp, and copied runtime dependencies land under `bin/` for the affected build.
4. Handle missing textures, unsupported formats, or load failures with existing English console diagnostics and safe fallback behavior.

## Validate

1. Build Debug.
2. Run `--debug-headless-scene3d-bench` for scene changes and `--debug-headless-scene-camera-transfer` for camera/station transfer.
3. Use the affected real route only when scale or resource layout matters; compare identical benchmark parameters and report strict budget failures honestly.
4. Build Release and verify runtime DLLs only for packaging, dependency, or optimization-specific work.
5. Run a short manual model/scene preview check when visual materials, picking, gizmos, labels, or startup layout changed; headless proof does not replace it.
