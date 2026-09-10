---
name: komapedit-resource-list-source-editing
description: Implement or diagnose komapedit Station, Structure, Signal, Sound, and Sound3D resource-list loading, replacement, creation, and row insertion through the typed source-backed edit pipeline. Use for related New File, blank-list, ordering, glare-row, Apply/Save, or apparently inert UI workflows.
---

# Komapedit Resource-List Source Editing

## Use the required companion skills

Read `komapedit-bve-format-compliance` first for every affected `*.Load` statement and resource-list row. Build its dated-cache compliance matrix before implementation. Use `komapedit-source-backed-editing` alongside this skill for stable identity, typed edits, full reparse, encoding-aware writeback, and Apply/Revert/Save behavior. This skill owns only the narrower resource-list workflow and must not redefine official formats or create another string-rewrite path.

Use `komapedit-develop` for a feature or behavior change and `komapedit-fix` for a concrete defect. Read `komapedit-debug-headless-validation` before selecting runtime proof.

## Establish the current path

1. Read `AGENTS.md`, the applicable sections of `docs/dev.md` and `TODO.md`, and the current working-tree diff.
2. Confirm the affected official shape from the compliance skill's current cache and matrix. Preserve headers, relative paths, source order, supported encodings, unknown trailing fields, and every parser-supported optional field.
3. Inspect the current owners before editing: `src/table/datatable.cpp`, `src/main_window/editable_list_drafts.cpp`, `src/main_window/edit_ledger.cpp`, `src/main_window/map_load_pipeline.cpp`, `src/main_window/element_inspector_data.cpp`, `src/main_window/new_element_wizard.cpp`, `src/main_window/app_dialogs.cpp`, and `src/maploader/maploader_edits.cpp` / `src/maploader/maploader_semantic.cpp`.
4. Trace the production path from table or menu request through deferred UI processing, the typed edit ledger, maploader dry run and full reparse, memory Apply, cache hydration, and Save/Commit. A UI-only or parser-only change is incomplete.

## Preserve resource-list identity and creation

1. Reuse the typed `resourceList.load` and `KV_EDIT_INSERT` paths. Never rewrite route or list source directly in a table or dialog.
2. Keep `ResourceListSource.edit_id`, `present`, `source_file_path`, and disk-baseline hash intact when `merge_edit_metadata()` replaces a preview snapshot with the full edit registry. A missing edit ID must not make a replacement picker silently return.
3. Keep file creation exclusive and staged. Check both currently loaded sources and pending `resourceList.load` edits before creating or connecting a duplicate list.
4. An empty list's first draft row takes its target source file and expected disk hash from `ResourceListSource`; it cannot depend on an anchor row that does not yet exist.
5. Represent inserted rows with `EditableListDraftRow`, `insertBeforeEditId`, `expectedSourceHash`, and `resource_list_insert_order`. Preserve physical source ownership and parser order.
6. Treat a Signal primary row and its optional adjacent glare row as one logical block. Never insert between them, add glare only through the explicit typed field, and ensure semantic validation accepts every field emitted by the workflow.
7. Keep Apply memory-only, Save as the disk-write boundary, Revert as working-copy reset, and Reload as a fresh disk read after unsaved-change handling.

## Diagnose the complete failure stage

- If a picker or action appears inert, first check deferred request state and source edit identity rather than adding another UI callback.
- If a focused insert test passes but the full edit path fails, compare emitted typed fields with the semantic allow-list and full-reparse proof.
- If an inserted `*.Load` and rows from its new file are in one logical operation, stage the reference insert and reparse before resolving row edit IDs in the connected list.
- A headless success is meaningful only when it proves the requested queueing, in-memory value, reparse/cache refresh, expected negative branch, and disk-hash boundary.

## Validate safely

1. Use temporary fixtures for writeback. If the user explicitly authorizes a real route, hash every touched source before and after; avoid Save/Commit for memory-only checks.
2. Run the affected contracts and the narrowest production-path headless mode selected through `komapedit-debug-headless-validation`. Run the full registered CTest set after shared typed or semantic changes.
3. For GUI-subsystem headless runs, use the documented `Start-Process -Wait -WindowStyle Hidden -PassThru` pattern and inspect `--headless-output`, exit code, stage markers, and `result=PASS`.
4. Report build, CTest, headless, manual GUI, and physical disk-write evidence separately. Never claim an unperformed GUI action or a disk-safe result without hashes.
