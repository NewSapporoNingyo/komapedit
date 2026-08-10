---
name: komapedit-table-feature-workflow
description: Add or repair typed map/list data shown in komapedit tables and connected 2D/3D views. Use for new `KvMapSnapshot` row families, MapModel hydration, TableUiCache rows, dynamic columns, shared table find/unused search, inline list tables, source actions, or table-to-plan/scene and reverse navigation.
---

# Komapedit Table Feature Workflow

## Follow the current typed pipeline

Use parser-owned records → `KvMapSnapshot` → `hydrate_map_snapshot()`/`MapModel` → `TableUiCache` → table rendering → optional marker/navigation cache.

Do not reintroduce `kv_get_ir_json()`, GUI-side source reparsing, a fallback data model, or per-frame row construction.

## Extend data once

1. Add or verify the parser-owned typed row and source metadata.
2. Make an explicit ABI version/structure-size decision for public snapshot changes; update DLL and EXE together.
3. Populate the snapshot arena/array and validate count/pointer/lifetime contracts.
4. Hydrate the GUI model once per relevant revision.
5. Cache formatted rows and widths in `TableUiCache`; invalidate on the owning content/settings/language inputs.
6. Bind stable `edit_id`, row kind, source file, and navigation identity during hydration/cache construction.

## Reuse table behavior

1. Reuse existing table headers, frozen-row setup, source path tooltip/open/find actions, and dock/menu conventions.
2. Reuse `TableFindState` and shared helpers. Do not create a bespoke search state machine per table.
3. Keep searches explicit: Apply may invalidate stale results, but must not silently rerun a user search.
4. Make the find input adapt to narrow widths while keeping button widths stable.
5. Assign horizontal/vertical scroll ownership deliberately. Avoid nested vertical scrolling that hides rows or headers.
6. Preserve current dynamic shapes such as Section parameter columns and Signal aspect structure/glare rows.

## Connect views through stable identity

1. Add plan/scene markers only when required and build them with the owning overlay/scene cache, not every frame.
2. Reuse shared marker visuals and table-navigation helpers.
3. Preserve one-to-one row identity for duplicate keys or same-distance events; do not locate by distance alone.
4. Ensure locate actions update marker visibility/highlight side effects consistently in both directions.
5. Route editing through `komapedit-source-backed-editing` rather than adding table-local rewrite logic.

## Validate

Build Debug, run `typed_snapshot_contract` for snapshot changes, `--debug-headless-table-find` for search semantics, and plan/scene headless modes for connected markers/navigation. Update all three UI languages when visible text changes and document any manual table-layout checks still required.
