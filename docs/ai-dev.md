# Using AI Coding Tools for komapedit

[简体中文](ai-dev_zhcn.md) · [Human developer guide](dev.md) · [Repository rules for AI tools](../AGENTS.md) · [Todo List](../TODO.md)

This guide is for people who use an AI coding tool to develop komapedit. It explains how to prepare work, supervise the tool, review its changes, and decide whether the result is safe to accept. It is not a prompt library and is not intended to be pasted into an AI tool.

The tool itself should read [`AGENTS.md`](../AGENTS.md), which contains the detailed repository rules. The person operating the tool remains responsible for the goal, scope, technical decisions, validation, and final acceptance.

## Divide responsibilities clearly

The operator should decide:

- what problem is worth solving and what outcome is expected;
- which files, components, users, and data are in scope;
- which existing behavior must remain unchanged;
- what evidence will count as completion;
- whether an architectural, ABI, dependency, or product-policy change is acceptable.

The AI tool can help inspect the repository, trace behavior, suggest a plan, edit files, and run checks. Treat its conclusions as proposals supported by evidence, not as decisions that automatically become correct because they sound confident.

Use the project documents for different purposes:

- [`README.md`](../README.md): current user behavior and limitations;
- [`TODO.md`](../TODO.md): implementation status and planned work;
- [`dev.md`](dev.md): build, architecture, coding, and testing information for developers;
- [`AGENTS.md`](../AGENTS.md): detailed rules the AI tool must follow.

## Prepare the work before asking for code

Start by checking the working tree and noting any existing changes that must not be overwritten. For a bug, reproduce it if possible. For a performance concern, record a baseline. For editing or save behavior, use backed-up route files or version-controlled fixtures.

Write down a short work brief for yourself. It should answer:

1. What is wrong or missing?
2. What should the user or developer observe after the change?
3. Which component appears to own the behavior?
4. What must not change?
5. Which tests, comparisons, or manual checks will be used?
6. What is explicitly outside the task?

This is a decision checklist for the operator, not a required prompt format. The wording sent to the tool can be conversational, but the underlying decisions should be clear.

Do not send secrets, private route data, credentials, or unrelated personal files to a tool unless its data-handling rules and the task genuinely allow it.

## Avoid vague requests and “wishful programming”

Requests such as “make the code better,” “modernize the project,” “fix all bugs,” “improve performance,” or “add a smart editor” do not define a finish line. If the tool is allowed to implement immediately, it must guess the product requirements, architecture, scope, and acceptance criteria. That is wishful programming.

When the goal is still vague, use the tool for investigation first. Ask it to show the relevant owners, call paths, tests, measurements, and possible options without changing code. Review that evidence, make the missing decisions yourself, and only then authorize a bounded implementation.

If an important choice cannot be resolved from the repository—such as file-format policy, compatibility level, destructive migration, or a new dependency—pause the work and decide it explicitly. Do not let the tool silently choose.

## A practical working cycle

### 1. Ask for inspection

Have the tool read `AGENTS.md` and inspect the relevant implementation, tests, build files, and documentation. It should identify the current behavior, canonical owner, affected consumers, and evidence for the proposed change.

### 2. Review the proposed approach

Before allowing a non-trivial edit, check that the approach:

- stays within the requested component;
- reuses an existing owner or shared helper;
- does not add speculative abstractions or duplicate implementations;
- identifies compatibility, ownership, invalidation, localization, and source-writeback effects;
- includes a realistic validation plan.

If the explanation is unclear, ask for evidence or a smaller plan. A polished plan is not a substitute for correct repository analysis.

### 3. Let the tool implement a bounded change

Keep unrelated cleanup out of the same task. Watch for unexpected rewrites, broad formatting changes, generated files, vendored changes, local paths, and new dependencies. Stop and reassess if the diff grows beyond the agreed scope.

### 4. Inspect the diff yourself

Read the changed code and documentation. Check the behavior at the boundaries, not only the most obvious function. Search for missed callers, stale fields, duplicated logic, incomplete localization, and invalid cache assumptions.

### 5. Verify the result

Run or independently confirm the relevant build, tests, headless checks, manual checks, save-and-reload comparisons, or benchmarks. The commands and component checklists are in [`dev.md`](dev.md) and [`AGENTS.md`](../AGENTS.md).

Do not accept “tests passed” without the command, input, result, and any skipped checks. If a check could not run, decide whether the remaining risk is acceptable.

### 6. Record the outcome

The final record should say what changed, why it changed, what was tested, what was not tested, and what risks remain. Update the correct project document when user behavior, development practice, AI workflow, or implementation status changes.

## Supervising feature development

For a new feature, first confirm that it was actually requested. An item in `TODO.md` describes status; it does not by itself authorize implementation.

Before accepting the design, make sure the tool has:

- checked the official BVE syntax instead of inventing private route syntax;
- found the existing canonical owner and all consumers that need synchronization;
- considered the typed ABI, ownership, lifetime, revisions, caches, source anchors, encoding, and writeback;
- covered the applicable parser/model, GUI cache, table, 2D/3D marker, navigation, editor, diagnostic, and localization paths;
- proposed focused tests and an update to the appropriate documentation.

Be cautious when the proposal starts with UI work but cannot explain how the underlying typed data and source ownership work. In this project, the data and writeback contract should be clear before a new editing UI is accepted.

Feature work is ready to accept only when the supported and unsupported cases are explicit, invalid input fails clearly, synchronized views agree, and the validation covers the complete affected path.

## Supervising problem and bug fixes

For a bug fix, require a concrete reproduction or strong code evidence. Record the expected and actual behavior before the implementation begins.

Ask the tool to trace the first incorrect value or state, not merely patch the last visible symptom. Check whether sibling paths share the same cause. A regression test or repeatable check should fail before the fix and pass afterward whenever practical.

If the suspected defect cannot be reproduced and the code does not establish it, accept an evidence-backed “not reproduced” result instead of demanding a rewrite.

For edit and save defects, review Include-owned statements or referenced lists when relevant, original encodings and line endings, stable edit identity, and the typed snapshot after save and reload. For performance defects, compare repeatable before-and-after runs using the same route, parameters, build type, and load profile.

## Supervising slop-fix work

A slop-fix removes low-quality generated or accumulated code, such as duplicate implementations, speculative abstractions, dead fields, unused compatibility shims, needless wrappers, or hidden repeated work. It is not a general invitation to restyle the repository.

Before authorizing a slop-fix, require an inventory:

- concrete evidence of duplication, dead state, needless indirection, or measured cost;
- every declaration, caller, test, build reference, localization entry, and ABI or serialized consumer in scope;
- the canonical implementation that will remain;
- the before count of self-owned production code and equivalent implementations;
- a compatibility and validation checklist.

During review, reject changes that merely shorten names, delete useful comments, join lines, move complexity into generated code, or introduce a new generic framework without at least two real current callers. Replaced implementations and newly unused fields or functions should be removed in the same change.

A slop-fix should reduce self-owned production code. If it grows, the tool must show measured correctness or performance value that justifies the increase. Tests and documentation do not count as production code for this comparison.

The completion report should include the production-code count before and after, the number of equivalent implementations removed, exact test and benchmark inputs and results, skipped checks, and remaining risks.

## Technical review points

Even when a change looks small, check the project-specific risks that apply:

- Public ABI: exact versions and structure sizes, explicit ownership, matching free functions, and no exceptions or STL types crossing the C boundary.
- Route source fidelity: official syntax, Include context, raw argument text, parse order, encoding, BOM, line endings, and Apply/Revert/Save/Reload behavior.
- UI: Simplified Chinese, English, and Japanese strings; table/plan/scene navigation; marker visibility and hit priority; settings persistence.
- Performance: no unconditional per-frame I/O or rebuilds, no repeated decoding or hashing of unchanged input, and complete cache keys with clear invalidation owners.
- Dependencies: maintenance, Windows fit, build and binary cost, license, ABI impact, packaging, and migration risk.
- Distribution: `LICENSE`, `NOTICE`, and `THIRD_PARTY_NOTICES.md` remain present and unchanged unless the task legitimately requires an update.

Runtime map data and edit commands must continue to use the versioned typed ABI. A proposal for an alternative parser, text-serialized route transport, fallback data model, or disk snapshot cache needs explicit human approval and a task that genuinely changes the architecture.

## When to pause or reject the work

Pause and reassess when:

- the tool cannot explain the current owner or reproduce the problem;
- the implementation requires a product decision that was never made;
- the diff touches unrelated components or user changes;
- a destructive write, migration, dependency, ABI change, or broad architecture change appears unexpectedly;
- required tests fail or are silently skipped;
- performance results are inconsistent or only favorable runs are reported;
- the proposed cleanup adds more abstraction without current callers;
- the final explanation does not match the actual diff.

AI assistance can accelerate investigation and implementation, but acceptance remains a human engineering decision. Prefer a smaller, well-understood change with honest validation over a larger result that is difficult to review.
