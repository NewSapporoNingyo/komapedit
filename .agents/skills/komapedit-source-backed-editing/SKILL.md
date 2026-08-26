---
name: komapedit-source-backed-editing
description: Extend or repair komapedit's typed source-backed map/list editing pipeline. Use for editable statement support, Properties/Edit, inline resource-list drafts, New Map Element insertion, deletion, Apply/Revert/Save/Reload behavior, stable edit IDs, source spans, distance moves, semantic validation, encoding-aware writeback, or edit-related C ABI changes.
---

# Komapedit Source-Backed Editing

Use `komapedit-bve-format-compliance` first for every touched BVE statement or list-row shape. Its live official-source check and compliance matrix are mandatory for reading, editing, creation, serialization, and writeback; this skill supplies the separate source-lifecycle contract.

## Trace the complete lifecycle

Read the relevant portions of:

- `maploader_parser.cpp` and maploader-owned row/source records;
- `maploader_identity.cpp` and `maploader_snapshot.cpp`;
- `include/maploader_snapshot.h` and `include/maploader.h`;
- `maploader_semantic.cpp` and `maploader_edits.cpp`;
- `gui_kme.cpp`, `kme.h`, relevant tables/canvases, and `debug_headless.cpp`.

Map every requested operation through parse → source identity → typed snapshot → GUI draft → dry run → source patch → full reparse → semantic proof → memory Apply or commit → refreshed snapshot. A change is incomplete if only one stage recognizes the row.

## Preserve the editing contract

1. Keep Apply in memory, Save as the disk-write boundary, Revert as memory reset, and Reload as a fresh disk read after unsaved-change handling.
2. Keep `sourceHash` as working-copy identity and `expectedSourceHash` as the disk-baseline concurrency guard across repeated Apply/Delete cycles.
3. Preserve source file, Include stack, source span, raw argument text, evaluated values, parse order, stable edit ID, encoding, BOM, and line endings.
4. Reject writes that cannot be represented in the original encoding. Do not silently convert files to UTF-8.
5. Fully reparse the patched source set and prove target semantics without changing non-target elements or final variable bindings unexpectedly. A valid edit may change the final current `distance`.
6. Keep typed input views call-scoped and snapshot/report ownership within the documented DLL lifetime.

## Extend a row end to end

1. Add or verify parser-owned row data and `EditSourceRef` metadata.
2. Generate stable identity from physical source ownership and statement semantics.
3. Extend `KvMapSnapshot` only with an explicit ABI version/structure-size decision and update every caller.
4. Hydrate the GUI from typed snapshot data; never reparse raw source in tables or canvases.
5. Add edit-target field construction and method/shape constraints.
6. Add semantic before/after writers and validation fingerprints.
7. Add update/delete/insert patch construction, edited-row reconnection, committed metadata refresh, and reset behavior.
8. Bind the same edit ID into tables and 2D/3D markers during cache/model construction, not through ambiguous per-frame matching.
9. Add contract tests and the narrow relevant Debug headless path.

## Handle special source shapes

- Plan distance moves as batches grouped by source file, Include context/section, and target distance. Reuse a compatible block or require the existing explicit boundary/expression workflow.
- Preserve untouched raw argument expressions. Rewrite a whole variable-length list only when its count changes.
- Use `repeater_linkage` and `own_track_transition_linkage` for linked rows; do not reconstruct chains independently in one UI path.
- Keep orphan `BeginTransition` rows read-only. Use the explicit Inspector coordinate-offset controls for `Put`/`Put0` and `Begin`/`Begin0`, confirming before nonzero offsets are discarded; keep the documented confirmation paths for short-form `Signal.Put` and permitted Repeater trim conversion.
- Treat inline Station/Structure/Signal/Sound/Sound3D list editing as a shared draft workflow. Preserve physical source file, source order, unknown trailing fields, and optional Signal glare-row shape.
- For `KV_EDIT_INSERT`, use structured fields and the same distance/environment/full-reparse validation as updates. Do not accept arbitrary replacement statements.

## Validate source safety

Run the affected registered contracts and the corresponding headless source-anchor/edit mode. For any commit test, prefer temporary fixtures; if a real route is explicitly used, hash every touched source before and after and restore only through a verified, user-authorized path. Confirm Apply leaves disk unchanged, Save survives reload, IDs remain stable, and non-target snapshot content is unchanged.
