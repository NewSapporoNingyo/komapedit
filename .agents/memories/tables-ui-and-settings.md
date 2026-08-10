# Tables, UI, and Settings Memory

## Table data and search

- Hydrate tables from `KvMapSnapshot`/`MapModel` and cache formatted rows in `TableUiCache`. The former IR JSON transport was removed; do not restore it as a convenience path.
- Reuse `TableFindState` and shared find/unused helpers. Searches run only on explicit user action; Apply may invalidate stale results but must not silently rerun and change selection.
- Draft-aware search views must exclude deleted rows when a search is explicitly executed while preserving pending deletions in the shared ledger.
- Keep source actions attached to parser-provided physical source metadata. Do not infer a source file from a displayed resource path.
- For duplicate keys or same-distance rows, navigation/editing uses stable identity, not the displayed value.

## Layout lessons

- Frozen headers and scroll ownership must be deliberate. Station Definitions needed horizontal table scrolling with outer-page vertical scrolling; nested `ScrollY` hid or constrained rows.
- Keep find panels responsive at narrow widths: stable button widths and an input field that consumes remaining space.
- Dynamic tables preserve their source shape. Section argument counts, Signal aspect structure-key columns/glare rows, and untouched excess fields must not be flattened by the GUI.
- Do not mutate table caches while an active table/popup is rendering; defer the action and refresh afterward.

## Localization

- Add visible strings inside the existing English, Simplified Chinese, and Japanese initializer blocks in `include/multilanguage.h`.
- Keep console diagnostics English by default. Do not create post-initializer assignments or another localization store for a small change.
- Read/write localization files explicitly as UTF-8 and verify non-ASCII text after patching.

## Persistence ownership

- `settings/settings.ini` stores application preferences and window/view/3D settings.
- `settings/history.ini` stores recent maps and per-map background alignment/history.
- `settings/imgui.ini` stores docking, layout, and window geometry.
- Saved state and code defaults are different. A new default must not overwrite an existing user's preference unless an explicit migration requires it.
- Bind menu checkmarks to the real runtime visibility boolean and reuse the existing load/apply/dirty-save flow. Do not invent a second toggle state.
- Headless modes should set ImGui persistence off so validation does not create or modify `imgui.ini`.
