---
name: komapedit-ui-persistence
description: Add or repair komapedit GUI state persistence. Use for settings.ini preferences, history.ini recent-map/per-map background data, imgui.ini docking/window layout, canonical setting schemas, default visibility or controls, menu checkmarks, 3D quality/gizmo preferences, or persistence-related table/window regressions.
---

# Komapedit UI Persistence

## Choose the correct owner

- `settings/settings.ini`: application preferences, view visibility, UI style, 3D scene quality/gizmo settings, and reusable controls.
- `settings/history.ini`: recent maps and per-map background-image alignment/history.
- `settings/imgui.ini`: Dear ImGui window geometry, docking, and layout.

Do not create a new sidecar file when one of these owners already fits.

## Extend the existing lifecycle

1. Inspect `app_settings.cpp/.h`, `runtime_paths.cpp/.h`, the menu/settings projection in `ui_elements.cpp`, and the relevant state in `kme.h`. `gui_kme.cpp` now retains only App construction/destruction and log wiring.
2. Find initialization defaults, canonical load/parsing, runtime application, change detection, and save paths before editing.
3. Add one source of truth in the existing state object. Bind menu checkmarks directly to the actual visibility/control boolean where practical.
4. Distinguish code defaults from a valid saved value. Invalid, obsolete, unknown, or wrong-section entries use code defaults.
5. Keep the current schema canonical-only: accept exact saver-emitted sections, keys, and value grammars. Do not add aliases or migrations, and never rewrite an existing file during load; explicit Save is the only path that canonicalizes it.
6. Keep startup tab/focus behavior explicit in code when the request requires a default; saved `imgui.ini` selection may otherwise override assumptions.
7. Avoid per-frame disk writes. Save only through the existing dirty/change-detection lifecycle.

## Preserve related contracts

1. Update all three UI languages if visible text changes.
2. Keep headless modes from writing `imgui.ini`.
3. Use existing warnings and fallbacks for unsupported 3D render scale, MSAA, filtering, or outline settings.
4. Keep window visibility synchronization shared between menus, runtime state, and persisted settings.

## Validate

1. Build Debug.
2. Inspect the emitted config file for the exact section/key/value and verify a load/save round trip when feasible.
3. Run `--debug-headless-settings-persistence --headless-output <file>` when settings or history parsing changes; inspect its stage markers and `result=PASS`. This is an explicit headless mode, not a registered CTest.
4. Test missing-file defaults plus rejection and non-mutation of an existing obsolete or malformed file when schema/default behavior changes.
5. Confirm no unexpected settings/history/imgui artifacts are staged or committed.
6. Leave visual docking, focus, and menu checkmark behavior as manual checks unless a runtime test actually exercised them.
