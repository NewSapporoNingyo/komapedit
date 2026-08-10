---
name: komapedit-station-edit-workflow
description: Extend or repair komapedit Station.List and Station.Put workflows. Use for station definition inline editing, source ordering, draft Apply/Save behavior, Station.Put Properties/Edit, station marker identity, deletion, table/plan/scene navigation, or station-specific source writeback regressions.
---

# Komapedit Station Editing

## Separate the two station models

- `Station.List` rows are resource definitions in a physical list file.
- `Station.Put` rows are map positions and operational station placements.
- `model_.stations` is suitable for deduplicated jump UI; `model_.station_positions` preserves one source row per marker/action.

Do not use one collection as a fallback for the other.

## Preserve Station.List source behavior

1. Route snapshots, edit lookup, and committed metadata through `ordered_station_list_entries(ctx)` so parser/source order survives map-key normalization.
2. Preserve the complete row payload, raw source, unknown trailing CSV fields, source identity, encoding, and newline.
3. Reorder only within the same physical source file and swap complete row payloads.
4. Keep empty keys as source rows but exclude them from station-name lookup.
5. Route drafts through the shared editable-list ledger. Commit the active cell before Apply; block Save while unapplied inline drafts remain.
6. Extend target counting, patch construction, reparse reconnection, snapshot hydration, and committed metadata together. Fixing only the first rejection is incomplete.

## Preserve Station.Put identity and actions

1. Bind `edit_id` during model hydration/normalization with the shared station-position binding path.
2. Use full `station_positions` for same-distance or duplicate marker identity.
3. Reuse deferred `request_element_inspector` and `request_element_delete` actions from tables and canvases.
4. Keep table, 2D, and 3D refresh/navigation synchronized after memory Apply, delete, Revert, and Save.

## Validate

Run `typed_edit_contract`, `--debug-headless-station-list-edit`, and the general edit/source-anchor mode when the touched path requires them. Confirm Apply leaves disk untouched, Save survives reload, source order and hashes remain valid, and same-distance markers address the intended source row. Leave popup visibility, hit priority, and other visual behavior as explicit manual checks unless GUI automation actually exercised them.
