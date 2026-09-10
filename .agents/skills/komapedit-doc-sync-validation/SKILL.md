---
name: komapedit-doc-sync-validation
description: Validate a komapedit documentation synchronization after content is written. Use for documentation-only diffs, paired English/Simplified-Chinese README, developer-guide, or AI-guide updates, skill/index synchronization, syntax/status table parity, UTF-8 and line-ending checks, ignored local documentation, cross-links, or a request to change only necessary documentation.
---

# Komapedit Documentation Sync Validation

Use `komapedit-write-docs` to choose content ownership and establish facts. Use this skill for the final narrow validation pass.

## Check scope

1. Compare the changed-file list with the user's requested document scope.
2. Confirm no source, build artifact, generated setting, or unrelated prose changed in a documentation-only task.
3. Inspect ignored documents directly; Git status cannot prove their state.

## Check content

1. Map every changed behavior statement to current source, tests, or build scripts.
2. Compare paired English/Chinese sections for meaning, supported-status wording, row count, column count, and links.
3. Confirm `TODO.md` contains unfinished work only and completed items are archived in `docs/TODO_done.md`.
4. Confirm `AGENTS.md` indexes detailed skills/memories instead of duplicating their volatile details.
5. When skills changed, confirm each `SKILL.md` has valid frontmatter, referenced paths and sibling skill names exist, and matching `agents/openai.yaml` metadata is not contradicted by the workflow text.

## Check project AI-asset migrations

1. Confirm komapedit-only skills and distilled memories live under the project `.agents` tree, while user-level locations retain only cross-project material or a pending managed-cleanup copy.
2. Do not copy a mixed global registry into tracked project memories. Check that each distilled conclusion is shareable and still agrees with current source, tests, or build scripts.
3. Classify raw rollout summaries by normalized header `cwd`, not filename keywords. Compare same-name files and stop on a material conflict; verify copied bytes or an explicitly documented path-prefix-only equivalence.
4. Confirm `.agents/memories/rollout-summaries/` is ignored and absent from the tracked diff, and check any required external cleanup request independently of Git status.
5. Confirm the new or moved skill is synchronized across `AGENTS.md`, `.agents/memories/INDEX.md` when relevant, both AI guides, and routing skills.

## Check files

1. Require valid UTF-8 without BOM and no mojibake.
2. Preserve the file's established line-ending policy; reject newly mixed endings.
3. Run `git diff --check` on tracked files.
4. Verify relative links and referenced paths exist.
5. Validate Markdown headings, code fences, command spelling, and any changed tables.

## Report

State the exact documents checked, parity/link/encoding results, and that builds/tests were skipped when only prose changed. Do not describe doc validation as runtime validation.
