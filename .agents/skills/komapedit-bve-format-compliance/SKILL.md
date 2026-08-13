---
name: komapedit-bve-format-compliance
description: Verify and implement komapedit route-format behavior against the official BVE specifications. Use whenever changing or adding reading, parsing, validation, typed representation, editing, element creation, serialization, or source writeback for BVE map elements or the Map, Structure List, Signal Aspects List, Sound List, other-train, and Scenario formats; also use for syntax-related defects, compatibility decisions, tests, or documentation. This skill is mandatory alongside the relevant development, fix, or source-editing skill for those tasks.
---

# Komapedit BVE Format Compliance

## Establish the official contract

1. Read `references/official-bve-format-baseline.md` completely before planning the change.
2. Identify every affected file format, statement or row shape, cross-file reference, and lifecycle stage.
3. Open the current official page for each affected format from the reference source table. Treat the live official page as normative and the bundled baseline as a navigation aid and review checklist.
4. Treat current source and tests as evidence of implemented behavior, not as evidence that a syntax shape is official. Treat route samples, project documentation, and memories as secondary evidence only.
5. Classify every affected construct as current official syntax, an official legacy alias, project-only compatibility syntax, or unsupported syntax. Never present one class as another.
6. If the live official page, this baseline, the request, and current code disagree in a way that affects behavior, stop and request a human decision as required by `AGENTS.md`. Do not silently choose a compatibility or migration policy.

Do not infer a signature, optional argument, default, unit, range, alias, comment rule, case rule, or omission behavior from a similar command or another BVE file format.

## Build a compliance matrix before editing

Record the following for each affected construct in task notes or the implementation plan:

- authoritative official page and the date it was checked;
- exact header/version and encoding declaration rules;
- lexical form, separator/terminator, comment syntax, and case behavior;
- exact element/function or section/key spelling and every documented arity;
- argument order, type, unit, range, sentinel, default, and omission semantics;
- current-distance, ordering, cardinality, pairing, interpolation, or state prerequisites;
- key and relative-path relationships to other route files;
- current komapedit support for read, edit, create, writeback, and round trip;
- positive, negative, semantic, and preservation evidence needed for acceptance.

Keep syntax acceptance separate from feature support. Accepting an official form does not imply that komapedit previews, edits, creates, or graphically manipulates it.

## Implement the full affected path

### Reading and parsing

1. Validate the file-specific header and body grammar without projecting Map rules onto list, INI-like, or Scenario files.
2. Accept documented statement/row shapes and intentional official legacy aliases. Preserve established project-only compatibility forms as a separate compatibility contract unless the task explicitly determines a change; never emit them for new official elements. Do not widen arity or coerce undocumented forms for convenience.
3. Preserve official evaluation order, `distance`, variables/expressions, `include`, comments, relative paths, source order, and cross-file identity where applicable.
4. Preserve physical source ownership, Include stack, source spans, raw arguments, evaluated values, and diagnostics in maploader-owned records.
5. Keep official semantic values strongly typed through snapshots and the public C ABI. Do not replace syntax-specific data with opaque text or JSON.

### Editing, creation, and writeback

1. Combine this skill with `komapedit-source-backed-editing` whenever source can change.
2. Preserve an existing valid statement's official overload or legacy alias and untouched raw expressions unless the requested operation explicitly and safely requires a documented conversion.
3. Emit a documented current official form for new elements. Never invent convenience syntax, arbitrary replacement statements, private aliases, or omitted arguments whose defaults are not documented.
4. Enforce the official argument order, types, units, ranges, enumerations, defaults, and state prerequisites before Apply or Save.
5. Preserve list row shape, blank fields with defined meaning, source order, relative-path base, encoding, BOM, line endings, and unrelated text.
6. Fully reparse the patched source set and prove target semantics, cross-file references, and unchanged non-target behavior before Apply or Save succeeds.

### Tests and documentation

1. Add canonical positive coverage for every affected official overload and intentional official legacy alias.
2. Add boundary and negative coverage for wrong arity, order, type, range, header, comment, row shape, or prerequisite when the parser exposes deterministic diagnostics.
3. Prove read and write paths independently, then prove parse → typed snapshot → edit/create → serialize → full reparse for source-backed behavior.
4. Include the relevant encoding, Include, expression, ordering, and cross-file cases. Use temporary fixtures for write tests.
5. Update support tables only for behavior actually implemented and validated. Label official legacy aliases and project compatibility syntax accurately.
6. Report the official pages checked, compliance matrix outcome, tests run, unsupported forms, intentional compatibility behavior, and any unresolved discrepancy.

Do not claim official compliance from compilation alone, from one sample route, or from a successful parse that does not prove the documented semantics.

## Maintain the bundled baseline

Use `references/official-bve-format-baseline.md` as a compact snapshot of the six official sources supplied for this project. If a live page has changed:

1. record the exact difference;
2. update the baseline only from the official page;
3. determine whether source, tests, support documentation, or compatibility policy is affected;
4. stop for a human decision before implementing a behavior-changing interpretation that the current task did not already determine.

For a BVE format outside the six-source scope, locate and read its official specification before implementation. Do not extrapolate from the nearest covered format.
