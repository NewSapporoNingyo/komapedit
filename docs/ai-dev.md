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
