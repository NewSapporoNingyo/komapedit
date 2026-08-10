---
name: komapedit-ui-persistence
description: Add or repair komapedit GUI state persistence. Use for settings.ini preferences, history.ini recent-map/per-map background data, imgui.ini docking/window layout, default visibility or controls, settings aliases/migrations, menu checkmarks, 3D quality/gizmo preferences, or persistence-related table/window regressions.
---

# Komapedit UI Persistence

## Choose the correct owner

- `settings/settings.ini`: application preferences, view visibility, UI style, 3D scene quality/gizmo settings, and reusable controls.
- `settings/history.ini`: recent maps and per-map background-image alignment/history.
- `settings/imgui.ini`: Dear ImGui window geometry, docking, and layout.

Do not create a new sidecar file when one of these owners already fits.

## Extend the existing lifecycle

1. Inspect `app_settings.cpp/.h`, `runtime_paths.cpp/.h`, `gui_kme.cpp`, and the relevant state in `kme.h`.
2. Find initialization defaults, load/parsing, runtime application, change detection, save, and compatibility alias paths before editing.
3. Add one source of truth in the existing state object. Bind menu checkmarks directly to the actual visibility/control boolean where practical.
4. Distinguish code defaults from a saved user's value. Do not force a new default over existing persisted settings unless a migration is explicitly required.
5. Preserve compatibility aliases for renamed keys and rewrite/migrate older config only through the established settings path.
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
3. Test both no-existing-config defaults and an existing older config when migration/default behavior changed.
4. Confirm no unexpected settings/history/imgui artifacts are staged or committed.
5. Leave visual docking, focus, and menu checkmark behavior as manual checks unless a runtime test actually exercised them.
