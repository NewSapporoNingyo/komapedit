---
name: komapedit-slop-fix
description: Audit and repair evidence-backed maintainability, correctness, safety, and performance slop in komapedit without changing intended behavior. Use for sustainability passes covering duplicated logic, dead state, avoidable complexity, undefined behavior, unsafe bounds or ownership, repeated work, incomplete cache keys, confusing flow, or hidden crash/hang risks.
---

# Komapedit Slop Fix

## Audit before editing

1. Read `AGENTS.md`, current source, affected tests, and relevant memories.
2. Establish the requested scope and a clean baseline. Record existing test or benchmark failures before attributing them to new work.
3. Inspect read-only first and create an evidence ledger with:
   - candidate problem;
   - concrete code evidence;
   - user-visible or maintenance risk;
   - smallest safe repair;
   - validation that can disprove or confirm the repair.
4. Drop candidates that are speculative, intentional, generated, third-party, or merely stylistic.

## Review categories

- Equivalent production logic scattered across modules.
- Unused fields, helpers, state, or branches.
- Code that can be simplified without compressing lines or deleting useful comments.
- Undefined behavior, overflow, narrowing, lifetime, ownership, exception-boundary, or invalid-pointer risks.
- Repeated decode, I/O, hashing, allocation, table construction, scene generation, or per-frame work.
- Cache keys that omit visible inputs or invalidation with no clear owner.
- Async/thread paths that can exhaust resources, deadlock, or leave inconsistent state.
- Logic whose names or structure conceal the real invariant.
- Overdesign and over-engineering, such as “significantly increasing code complexity for a boundary case that occurs only one in a thousand times.”
- Module boundaries are unclear; for example, “a function that places a model on 3D canvas is located in a source file related to the main window.”
- Overly fragmented logic, such as “the implementation required to draw a particular graphic is scattered across more than a dozen source files.”

## Repair conservatively

1. Verify every finding against the current tree immediately before editing.
2. Do not add many patches directly to problematic code, as this leads to an accumulation of patch code, increased complexity, and reduced code readability. Instead, use a correct and efficient implementation to directly replace the problematic section.
3. Preserve user interaction, BVE compatibility, ABI layout/version, source writeback, cache outputs, and rendering behavior.
4. Never claim improvement from deleted comments, combined lines, renamed whitespace, or moved code alone.
5. Count self-owned production code before and after. Exclude tests, documentation, generated files, and third-party trees. Explain any increase with measured correctness or performance value.
6. Keep behavior-changing proposals out of a slop-fix pass unless the user separately approves them.
7. Do not abandon a fix for a particular section simply because “the issue discovered is too severe.”

## Validate rigorously

1. Configure or verify Debug with `KOMAPEDIT_STRICT_WARNINGS=ON` for strict maintenance validation.
2. Run all registered CTests and affected Debug headless checks.
3. Use a representative real route for parser/edit/scene risk when available, with source hash protection around writeback paths.
4. Measure performance before and after on the same machine, route, parameters, build type, and load profile; report median and p95.
5. Treat an unmet strict budget as unmet. Do not lower thresholds or repeat selectively to manufacture a pass.
6. If no problem is proven and no code changes, do not run unnecessary tests merely to create activity.

## Report honestly

List proven findings and fixes, rejected or deferred candidates, production-code counts, validation evidence, existing failures, and remaining manual checks. A successful slop-fix may legitimately make no code change.
