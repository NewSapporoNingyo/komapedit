# Komapedit Project Memories

These files preserve durable lessons from prior komapedit work. They are historical context, not current specification. Resolve conflicts in this order: current user request, `AGENTS.md`, current source/tests, current project documentation, then memories.

## Topic index

| Topic | Memory | Read when |
| --- | --- | --- |
| Typed source editing and writeback | [`source-backed-editing.md`](source-backed-editing.md) | Editing rows, source spans, Apply/Save, stable IDs, distance moves, linked statements, list drafts |
| 2D/3D rendering and navigation | [`rendering-and-navigation.md`](rendering-and-navigation.md) | Scene caches, placement semantics, markers, picking, overlays, camera/depth/fog, cross-view navigation |
| Tables, localization, and settings | [`tables-ui-and-settings.md`](tables-ui-and-settings.md) | Table caches/find, inline drafts, scroll ownership, trilingual UI, settings/history/imgui persistence |
| Validation and maintenance | [`validation-and-maintenance.md`](validation-and-maintenance.md) | CTests, Debug headless modes, real-route safety, benchmarks, slop-fix evidence and reporting |
| Build, Release, runtime layout, and docs | [`build-release-and-docs.md`](build-release-and-docs.md) | Assimp/toolchains, DLL packaging, Release-only failures, documentation ownership and synchronization |

## Optional local historical archive

A current user may keep `.agents/memories/rollout-summaries/` as a local historical archive. The directory is Git-ignored and must not be committed because raw summaries can contain user-specific operating preferences, Codex task/session metadata, absolute local file paths, and environment-specific commands or validation records.

The archive is optional and is not required for project development. When it exists, do not load it wholesale. Search filenames or contents for the exact subsystem, statement, error, or command, then read only the matching summaries. Examples:

```powershell
rg -l -i "sourceHash|expectedSourceHash" .agents/memories/rollout-summaries
rg -l -i "scene3d|reversed-Z|trackKey" .agents/memories/rollout-summaries
rg -l -i "Station.List|signal-glare" .agents/memories/rollout-summaries
```

Historical route paths, line numbers, ABI versions, test counts, performance numbers, and implementation details can become stale. Recheck them against the current tree before reuse. Do not copy user-specific details into tracked memories; distill only durable, project-wide lessons into the topic documents above.
