---
name: komapedit-write-docs
description: Create or synchronize komapedit project documentation from current source behavior and repository policy. Use for documentation-only tasks, README or developer-guide updates, TODO status changes, AI workflow documentation, AGENTS.md maintenance, bilingual English/Simplified-Chinese synchronization, or documenting a completed code change.
---

# Komapedit Documentation

## Choose the document owner

- `README.md` and `docs/README_zhcn.md`: user-visible features, supported syntax, setup, usage, and limitations.
- `TODO.md`: unfinished work only; move completed items to `docs/TODO_done.md`.
- `docs/dev.md` and `docs/dev_zhcn.md`: human setup, architecture, implementation facts, and validation commands.
- `docs/ai-dev.md` and `docs/ai-dev_zhcn.md`: guidance for people operating AI coding tools.
- `AGENTS.md`: absolute repository constraints, general workflow, and indexes for detailed skills/memories.
- `.agents/skills/*`: reusable AI execution workflows.
- `.agents/memories/*`: durable historical lessons and archived task evidence; never current specification.

Do not duplicate the same detailed workflow across several owners. Link to the authoritative document instead.

## Establish facts

1. Read the current implementation, tests, CMake/build scripts, and `TODO.md` relevant to each statement.
2. Use current code as the behavior source. Use Git history and memories to understand intent, not to override current code.
3. Identify paired English/Chinese files and any tables or lists whose structure must remain aligned.
4. Confirm whether the task is documentation-only. Do not modify source or run unrelated builds in a doc-only task.

## Edit surgically

1. Change only statements affected by the task. Preserve established terminology and link style.
2. Keep English and Simplified Chinese documents semantically aligned; translate meaning, not punctuation mechanically.
3. Update all cross-links and indexes when a document, skill, or memory moves.
4. Preserve UTF-8 without BOM and each file's existing line-ending policy. Do not introduce mixed endings or mojibake.
5. Do not describe planned work as implemented or historical behavior as a current guarantee.
6. Keep exhaustive volatile lists out of `AGENTS.md`; point to source, `TODO.md`, or the appropriate detailed guide.

## Validate documentation

1. Run `git diff --check` on tracked documents.
2. Check relative links, referenced paths, headings, code fences, and command names.
3. Compare paired README/dev/AI-guide tables, row counts, and status language when changed.
4. Inspect ignored documents directly if the user placed them in scope; Git status cannot prove their contents.
5. Skip compilation for pure prose changes unless the documentation changes a command that should be smoke-tested.

## Report scope

State which facts were synchronized, which paired documents were updated, the checks performed, and that builds/tests were intentionally skipped when the task was documentation-only.
