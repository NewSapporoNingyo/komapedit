# Developing komapedit with AI Coding Tools

[Chinese version](ai-dev_zhcn.md) · [Human Developer Guide](dev.md) · [Repository Rules for AI Tools](../AGENTS.md) · [Development Progress](../TODO.md)

This document is for people who use AI coding tools to work on komapedit. It explains how to define a task, select the repository workflow, supervise changes, and decide whether the result is safe to accept.

AI tools should read [`AGENTS.md`](../AGENTS.md) themselves. Repeatable execution workflows live in [`.agents/skills`](../.agents/skills), and durable historical lessons live in [`.agents/memories`](../.agents/memories/INDEX.md). Do not paste all of these documents into a prompt. The operator remains responsible for scope, product decisions, validation, and final acceptance.

## Understand the limitations

Don’t treat AI agents as some kind of magical beings that can “do anything with just a single command.”AI agents can produce plausible but incorrect code, lose constraints in long tasks, overfit to one example route, and claim more validation than they performed. Using an AI tool does not remove the need to understand basic C/C++, the Windows build environment, diffs, tests, and the affected application behavior.

Avoid vague requests such as “make the code better,” “fix all bugs,” “improve performance,” or “rewrite this module.” A useful request states:

- the observed behavior or desired feature;
- the affected workflow and what must remain unchanged;
- concrete inputs, logs, screenshots, or route files when relevant;
- the expected result and acceptance criteria;
- allowed build/test scope, including whether manual GUI testing will happen later;
- any file, compatibility, dependency, or Release constraints.

If an important choice cannot be inferred from current code—such as a format strategy, breaking migration, public ABI change, or new dependency—ask for a read-only investigation first and make the human decision before implementation.

## Mandatory BVE format compliance

Every change that adds or modifies BVE map-element reading, parsing, validation, typed representation, editing, creation, serialization, or writeback must use [`komapedit-bve-format-compliance`](../.agents/skills/komapedit-bve-format-compliance/SKILL.md). This includes the official Map, Structure List, Signal Aspects List, Sound List, other-train, and Scenario formats covered by its [official-source baseline](../.agents/skills/komapedit-bve-format-compliance/references/official-bve-format-baseline.md).

Use the compliance skill in addition to `komapedit-develop`, `komapedit-fix`, `komapedit-source-backed-editing`, or another matching subsystem skill. It requires the agent to re-open the affected live official page, classify current syntax, official legacy aliases, project compatibility forms, and unsupported forms separately, then build a compliance matrix before implementation. If the official page, request, and current implementation disagree in a behavior-changing way, the agent must stop for a human decision.

```text
Use $komapedit-bve-format-compliance together with $komapedit-develop (or $komapedit-fix).
Official formats/elements: [affected files, statements, rows, sections, or keys]
Operations: [read / validate / edit / create / serialize / write back]
Acceptance: [documented signatures and semantics, round trip, negative cases, source fidelity]
```

## Four common project workflows

The repository provides four scenario skills plus narrower subsystem skills. When the client supports explicit skill invocation, reference the skill name. Otherwise describe the scenario clearly; the agent should route the task through the matching project skill listed in `AGENTS.md`.

### Everyday development

Use [`komapedit-develop`](../.agents/skills/komapedit-develop/SKILL.md) for a scoped feature or behavior change.

```text
Use $komapedit-develop.
Request: [describe the feature, affected workflow, constraints, and acceptance criteria]
Validation: [Debug build only / affected CTests / headless mode and authorized route / manual GUI check later]
Preserve: [compatibility, performance, user interaction, files, or APIs that must not change]
```

The skill routes source editing, tables, 3D preview, settings, localization, and validation to narrower project skills when needed.

### Bug fixing

Use [`komapedit-fix`](../.agents/skills/komapedit-fix/SKILL.md) for a concrete regression or defect.

```text
Use $komapedit-fix.
Observed: [symptoms, logs, build type, route/input, and reproduction steps]
Expected: [correct behavior]
Scope: [diagnosis only, or diagnose and implement the smallest proven fix]
Validation: [original reproduction plus the relevant Debug/Release/headless checks]
```

Do not authorize a broad rewrite while the cause is unknown. A fix should explain the owning-layer root cause and prove the original reproduction no longer fails.

### slop-fix

Use [`komapedit-slop-fix`](../.agents/skills/komapedit-slop-fix/SKILL.md) for a development-sustainability pass.

```text
Use $komapedit-slop-fix.
Scope: [specific subsystem or changed files]
Audit first: duplication, dead state, unsafe code, repeated work, cache/invalidation errors, hidden crash/hang risks, and confusing logic.
Repair only proven findings; preserve behavior and ABI.
Validation: strict Debug, registered CTests, affected headless checks, and controlled before/after performance evidence where applicable.
```

A valid result may contain no code change. Do not invent a defect, compress lines, delete useful comments, lower performance gates, or change user behavior merely to report progress.

### Documentation writing

Use [`komapedit-write-docs`](../.agents/skills/komapedit-write-docs/SKILL.md) for documentation-only updates or synchronization after a code change.

```text
Use $komapedit-write-docs.
Facts to document: [current implemented behavior or workflow]
Documents in scope: [README, TODO, developer guide, AI guide, AGENTS, skills, or memories]
Language scope: [English and Simplified Chinese pair, if applicable]
Keep the task documentation-only unless implementation discrepancies must only be reported.
```

Documentation must be derived from current source/tests. Use [`komapedit-doc-sync-validation`](../.agents/skills/komapedit-doc-sync-validation/SKILL.md) for the final scope, parity, encoding, link, and table checks.

## Explicit Pi Agent collaboration

[`collaborate-with-pi`](../.agents/skills/collaborate-with-pi/SKILL.md) is an explicit-only workflow. Use it only when the current user explicitly asks to “调用pi agent” (case and spacing variants are equivalent) or invokes `$collaborate-with-pi` for that purpose. Do not activate it because a task is difficult, because Pi-related files exist, or from a mere discussion of the skill. Without that current explicit request, the active agent must complete the work itself and must not launch Pi.

This skill may be used only by a programming agent other than Pi Agent; invoking another Pi from inside Pi is prohibited. The supervising agent researches the current tree, defines acceptance criteria, writes the complete `pi-prompts_local.txt` brief, launches `pi-agent-here(local).bat` in a visible Windows Terminal, and retains responsibility for scope, product decisions, independent review, and any correction round. Pi performs the main implementation, focused tests, owned documentation updates, and its evidence report. While Pi is working, the supervising agent checks state only about once every five minutes and does not make overlapping edits or start concurrent builds.

After Pi exits, the supervising agent must inspect the actual diff and independently rerun the smallest decisive validation. If a defect is proven, it writes a narrow correction brief and starts another visible Pi round; it does not accept Pi's report as proof or silently transfer final responsibility.

## Review the result

Even a small diff should be checked for the project risks that apply:

- **Scope:** no unrelated rewrite, generated files, or hidden behavior change.
- **Public ABI:** exact version/structure sizes, explicit ownership, matching free functions, and no STL types or exceptions crossing C boundaries.
- **Route-source fidelity:** official BVE syntax, Include context, raw expressions, parse order, encoding/BOM/line endings, and Apply/Revert/Save/Reload behavior.
- **UI:** English/Simplified Chinese/Japanese text, table/plan/scene identity and navigation, marker hit priority, settings persistence, and remaining manual visual checks.
- **Performance:** no new unconditional per-frame I/O/rebuilds, repeated decoding/hashing, incomplete cache keys, or unsupported performance claims.
- **Dependencies/distribution:** Windows/toolchain compatibility, license and notice updates, ABI impact, binary/runtime layout, and migration cost.
- **Evidence:** exact commands, relevant output, known failures, and an honest distinction between automated proof and unperformed manual testing.

Runtime route data and edit commands must continue to use the versioned typed ABI. Alternate parsers, text-serialized route transports, fallback data models, or disk snapshot caches require an explicit human architecture decision.

## Stop and reassess

Stop the agent when it clearly leaves scope, repeatedly performs the same failed action, starts a broad rewrite without evidence, modifies unrelated files, adds an unapproved framework/dependency, deletes substantial code without a recoverable plan, or claims tests/visual checks that were not run. Review the diff and restore control before continuing.
