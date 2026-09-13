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

Komapedit-only skills and distilled memories belong in the repository's `.agents/skills` and `.agents/memories` trees. Keep user-level Codex skills and memories for genuinely cross-project guidance. When a global registry mixes projects, extract only shareable, durable conclusions that still match current source; never copy the mixed registry wholesale. Raw rollout summaries may be kept only in the Git-ignored `.agents/memories/rollout-summaries/` archive and must not enter a commit.

## Establish facts

1. Read the current implementation, tests, CMake/build scripts, and `TODO.md` relevant to each statement.
2. Use current code as the behavior source. Use Git history and memories to understand intent, not to override current code.
3. Identify paired English/Chinese files and any tables or lists whose structure must remain aligned.
4. Confirm whether the task is documentation-only. Do not modify source or run unrelated builds in a doc-only task.
5. For source maps and command inventories, compare `rg --files` and the owning CMake target/source lists with current option parsing, dispatch, and usage text. Do not infer current modules or CLI modes from an older guide alone.

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
3. Reconcile changed source-map entries against the current tracked inventory/CMake owners and changed CLI examples against the live parser/dispatch source.
4. Compare paired README/dev/AI-guide tables, row counts, and status language when changed.
5. When skills are in scope, validate every changed `SKILL.md` frontmatter plus any matching `agents/openai.yaml` description or prompt, and confirm the indexes in `AGENTS.md` and the paired AI guides still describe the same routing.
6. Inspect ignored documents directly if the user placed them in scope; Git status cannot prove their contents.
7. For AI-asset migration, verify project ownership from content, classify raw summaries by normalized header `cwd`, compare same-name files before copying, and confirm ignored archives remain ignored. If external mixed registries need cleanup, use their managed update mechanism rather than treating the project copy as physical deletion.
8. Skip compilation for pure prose changes unless the documentation changes a command that should be smoke-tested; honor an explicit no-build boundary.

## Report scope

State which facts were synchronized, which paired documents were updated, the checks performed, and that builds/tests were intentionally skipped when the task was documentation-only.
