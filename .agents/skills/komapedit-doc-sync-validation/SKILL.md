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

## Check files

1. Require valid UTF-8 without BOM and no mojibake.
2. Preserve the file's established line-ending policy; reject newly mixed endings.
3. Run `git diff --check` on tracked files.
4. Verify relative links and referenced paths exist.
5. Validate Markdown headings, code fences, command spelling, and any changed tables.

## Report

State the exact documents checked, parity/link/encoding results, and that builds/tests were skipped when only prose changed. Do not describe doc validation as runtime validation.
