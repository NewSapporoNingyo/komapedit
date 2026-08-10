# Source-Backed Editing Memory

## Durable contracts

- Apply changes the in-memory working copy and preview. Save commits that validated working copy to disk. Revert resets memory overrides. Reload rereads disk after unsaved-change handling.
- `sourceHash` identifies the current working copy; `expectedSourceHash` remains the disk-baseline concurrency guard. Repeated Apply/Delete must not replace the disk baseline with an in-memory hash.
- The complete lifecycle matters: parser row and source ref, stable identity, typed snapshot, target lookup/count, semantic writer, source patch, full reparse/reconnection, committed metadata, GUI refresh, and regression proof. A row added to only one stage will fail later or lose identity.
- Preserve physical source path, Include stack, parse order, source span, raw expressions, method/argument shape, encoding, BOM, line ending, and unknown fields. Do not reconstruct source ownership from GUI display text.
- Full reparse must prove the target reached the requested semantic value without unexpected changes to non-target rows or the final variable/distance environment.

## Source identity lessons

- Source ordering must use explicit parser order. `Station.List` once regressed because map-key iteration replaced source order; `ordered_station_list_entries(ctx)` became the shared ordering path.
- Same-distance markers and duplicate keys require direct stable edit IDs attached during hydration/cache construction. Distance-only or lazy matching can select the wrong source row.
- Edited-row reconnection must use the physical replacement identity, not an overly broad span. Signal glare deletion exposed this: glare content can be removed while a separator newline remains, so the surviving main row's identity range must exclude that newline without weakening global uniqueness checks.
- Commit reports need not repeat Apply counters. Validate committed files, reparse results, snapshots, physical source rows, and refreshed metadata instead.

## Distance and linked statements

- Group distance moves by physical source file, Include context/source section, and target distance. Reuse a compatible distance block; otherwise use a parser-confirmed gap or the existing manual boundary/expression resolution.
- Preserve safe variable expressions and untouched arguments. Do not globally sort a file or delete user-authored empty distance/comment structure.
- Pair Curve/Gradient transitions and Repeater chains through shared linkage helpers. Orphan `BeginTransition` remains read-only. Repeater deletion/editing may touch several physical statements atomically.
- Conversion of compact source forms is exceptional and explicit: Inspector coordinate-offset controls convert `Put`/`Put0` and `Begin`/`Begin0` in either direction, confirming before nonzero offsets are discarded; short-form `Signal.Put` and the documented Repeater trim case remain confirmation-driven.

## GUI lifecycle lessons

- Defer Inspector/delete actions requested from an ImGui table or popup. Mutating Inspector/table/model state during rendering caused crashes; use the shared pending request path.
- Inline list editors maintain UI drafts separate from applied working-copy changes. Commit the active cell before Apply and block toolbar Save while unapplied list drafts remain.
- Revert/Reload/exit must discard or resolve drafts and pending deletions through the shared ledger; search refresh must not silently erase pending deletions.
- Cache/model refresh should preserve unrelated scene geometry, camera, and model caches when the typed content revision allows it.

## Safe validation

- Prefer temporary fixtures for commit tests. If an authorized real route is used, hash touched files before and after and inspect the physical source, not just the in-memory snapshot.
- Useful proof layers are the registered typed edit/snapshot contracts, source-anchor validation, the matching statement-family headless edit mode, Save→Reload comparison, and stable-ID checks.
