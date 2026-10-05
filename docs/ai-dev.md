# Developing komapedit with AI Coding Tools

[Chinese version](ai-dev_zhcn.md) · [Human Developer Guide](dev.md) · [Repository Rules for AI Tools](../AGENTS.md) · [Development Progress](../TODO.md)

This guide is for people using AI coding tools to develop komapedit. It explains how to describe a task, choose a workflow, follow the changes, and review the result.

Before starting, it helps to understand the affected application features, basic C++, the Windows build environment, and how to read diffs and test results. This knowledge helps you assess an AI's proposed solution and make product and acceptance decisions.

The repository provides [`AGENTS.md`](../AGENTS.md) and [project skills](../.agents/skills) for AI tools; prompts can refer directly to the relevant skills. [Project memories](../.agents/memories/INDEX.md) provide context for past decisions. Current implementation details come from source and tests, while [`TODO.md`](../TODO.md) tracks development progress. Project-specific skills and lessons belong in `.agents`; user-level guidance is for content shared across projects.

## Describe the task clearly

Start with concrete actions and observable results. Include:

- the observed behavior or desired feature;
- reproduction steps and relevant inputs, logs, screenshots, or route files;
- the scope of changes and the interactions, compatibility, and performance to preserve;
- the expected result and acceptance criteria;
- the build and test scope, routes available for validation, and arrangements for manual GUI checks.

For example, when addressing a slowdown, the triggering action, route size, build type, and elapsed time make diagnosis and acceptance easier than a general request to “improve performance.” For important choices involving file formats, compatibility migrations, public ABIs, or new dependencies, you can request a read-only investigation and agree on a plan before implementation.

## Choose a workflow

The repository provides four workflows: everyday development, bug fixing, code maintenance, and documentation writing. If your client supports explicit skill invocation, you can use the prompt templates below. In other clients, describe the task in natural language and reference the skill path. The workflow skills bring in specialist skills for source editing, tables, 2D/3D previews, settings, multilingual UI, and validation as needed.

### Everyday development

[`komapedit-develop`](../.agents/skills/komapedit-develop/SKILL.md) is suitable for new features or behavior changes.

```text
Use $komapedit-develop.
Request: [feature, affected workflow, constraints, and acceptance criteria]
Preserve: [compatibility, performance, user interactions, files, or APIs]
Validation: [Debug build, relevant CTest/headless checks, validation routes, and manual GUI checks]
```

Use Debug builds for routine development. Add Release validation for distribution packaging, runtime dependencies, or optimization-related issues. See the [Human Developer Guide](dev.md) for commands.

### Bug fixing

[`komapedit-fix`](../.agents/skills/komapedit-fix/SKILL.md) starts by reproducing the problem and identifying its cause, then fixes it in the module responsible for that behavior. If the cause is unclear, you can request a read-only diagnosis first. Review the result against the original reproduction steps and relevant regression tests.

```text
Use $komapedit-fix.
Observed: [symptoms, logs, build type, route/input, and reproduction steps]
Expected: [correct behavior]
Scope: [diagnosis only, or diagnose and fix]
Validation: [original reproduction plus the relevant Debug/Release/headless checks]
```

### Code maintenance (slop-fix)

[`komapedit-slop-fix`](../.agents/skills/komapedit-slop-fix/SKILL.md) addresses code quality problems supported by evidence, such as duplicated logic, unused state, cache errors, and hidden crash risks. It audits first, then fixes confirmed problems while preserving existing behavior and ABI.

```text
Use $komapedit-slop-fix.
Scope: [specific subsystem, changed files, or the entire project]
Start with a read-only audit listing the evidence, impact, proposed fix, and validation method for each problem, then fix confirmed findings.
Preserve existing behavior and ABI.
Validation: Debug with strict warnings enabled, registered CTests, and relevant headless checks; include before/after data under identical conditions for performance work.
```

An audit can also conclude that no changes are needed.

### Documentation writing

[`komapedit-write-docs`](../.agents/skills/komapedit-write-docs/SKILL.md) is suitable for revising documentation or updating it after code changes. Specify the audience, files, and languages, using current source, tests, and build scripts as the factual basis.

```text
Use $komapedit-write-docs.
Content and audience: [current behavior or workflow to explain, and intended readers]
Files and languages: [document paths; English/Simplified Chinese versions to synchronize]
Scope: documentation only.
```

[`komapedit-doc-sync-validation`](../.agents/skills/komapedit-doc-sync-validation/SKILL.md) checks scope, meaning across languages, encoding, links, and tables. Source maps and validation commands also need to be checked against current files, CMake targets, and command-line entry points.

### Tasks involving BVE formats and source editing

Development and fixes involving BVE formats require [`komapedit-bve-format-compliance`](../.agents/skills/komapedit-bve-format-compliance/SKILL.md). It covers reading, parsing, validation, typed representation, editing, creation, serialization, and writeback for the Map, Structure List, Signal Aspects List, Sound List, other-train, and Scenario formats in the [official-source baseline](../.agents/skills/komapedit-bve-format-compliance/references/official-bve-format-baseline.md).

These tasks begin with a compliance matrix based on the official pages, distinguishing current syntax, legacy aliases, project compatibility forms, and unsupported forms before implementation. The official pages are cached locally with a date; the entire cache is refreshed when its timestamp is invalid or older than 30 days. The user decides how to resolve differences between the official specification, requirements, and implementation that affect behavior.

```text
Use $komapedit-bve-format-compliance together with $komapedit-develop (or $komapedit-fix).
Formats and elements: [affected files, statements, list rows, sections, or keys]
Operations: [read / validate / edit / create / serialize / write back]
Acceptance: [official signatures and semantics, save and reload, valid and invalid inputs, source fidelity]
```

Source editing uses [`komapedit-source-backed-editing`](../.agents/skills/komapedit-source-backed-editing/SKILL.md). For importing, replacing, or creating Station, Structure, Signal, Sound, or Sound3D lists, adding the first row to an empty list, or inserting rows, also use [`komapedit-resource-list-source-editing`](../.agents/skills/komapedit-resource-list-source-editing/SKILL.md) to check the format, editing, and saving workflows together.

## Collaborate with Pi Agent

Pi Agent is a simple, efficient coding agent. To have Pi handle the implementation, you can enter prompts directly in Pi, explicitly ask another coding agent to “调用 pi agent” (“invoke Pi Agent”), or invoke [`$collaborate-with-pi`](../.agents/skills/collaborate-with-pi/SKILL.md).

The coordinating agent first investigates the repository and prepares a development plan, writes it to `pi-prompts_local.txt`, and launches Pi through `pi-agent-here(local).bat` in a visible Windows Terminal. Pi handles the main implementation, relevant tests, and documentation updates. The coordinating agent follows progress, reviews the actual diff after Pi finishes, independently reruns key checks, and arranges corrections when needed.

If a Pi session encounters an abnormal condition, the collaboration ends with a report of completed changes and remaining work. The user decides what happens next. See the skill for launch, status-check, and correction procedures.

## Review the changes

First compare the actual code diff with the task scope and acceptance criteria, then choose the relevant checks:

- **Features and UI:** repeat the affected operations, checking English, Simplified Chinese, Traditional Chinese (Taiwan), Traditional Chinese (Hong Kong), and Japanese text, selection, markers, navigation across tables and 2D/3D views, and saved settings.
- **Route editing:** check preservation of Includes, original expressions, statement order, encoding, BOM, and line endings, and compare the contents after saving and reloading.
- **Module interfaces:** for DLL changes, check the versioned typed C ABI, structure sizes, data ownership, matching free functions, and exception handling.
- **Performance:** compare before/after data using the same route, parameters, build type, and workload; also review repeated I/O, per-frame rebuilding, and cache invalidation.
- **Dependencies and distribution:** check Windows and toolchain compatibility, licenses and notices, and the runtime DLL layout.

For maps and resource lists, Apply updates the in-memory working copy and preview, Save writes changes to files, Revert discards pending changes, and Reload rereads disk after requesting confirmation if there are unsaved changes. Resource-list drafts need to be applied in their table first; Scenario drafts are saved directly through Save. Use this sequence to plan complete editing-workflow checks.

The acceptance report should list the commands actually run, their results, and known failures, with separate records for builds, CTest, headless checks, and manual GUI checks. Rendering, menus, dialogs, and dragging also need to be checked in the actual interface. If work drifts outside scope, repeatedly stalls on the same failed operation, or the report conflicts with the evidence, you can pause the task, inspect the current diff, and clarify the objective.
