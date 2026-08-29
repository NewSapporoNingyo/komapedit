---
name: collaborate-with-pi
description: Coordinate a local Pi Agent as the primary implementation agent. Use only when a non-Pi programming agent receives an explicit user request to “调用pi agent” or explicitly invokes $collaborate-with-pi for that purpose; never select it automatically, merely because a task is difficult, or from inside Pi Agent.
---

# Collaborate with Pi Agent

## Enforce the activation boundary

Use this skill only when the current user explicitly asks to “调用pi agent” (case and spacing variants are equivalent) or explicitly invokes `$collaborate-with-pi` for that purpose.

- Do not infer permission from task size, difficulty, a request to collaborate, the presence of Pi files, or an earlier invocation in another turn.
- Merely discussing Pi Agent or this skill does not authorize launching Pi.
- Without an explicit activation in the current request, complete the work in the current agent and do not launch, message, or otherwise call Pi Agent.
- This skill is only for programming agents other than Pi Agent. If the current agent is Pi Agent, do not load another Pi, do not run the launcher, and tell the user that nested Pi invocation is unsupported.

Explicit activation authorizes the collaboration workflow, not a broader product decision, architecture change, destructive action, commit, or push.

## Keep responsibilities separate

### Supervising agent

The non-Pi supervising agent retains end-to-end responsibility. It must:

1. Read `AGENTS.md`, inspect the current worktree, load every task-relevant project skill, and research the owning code, tests, documentation, and current behavior before delegating.
2. Convert the request into observable acceptance criteria and identify evidence, ambiguities, non-goals, protected files, and any required human decisions.
3. Write a self-contained implementation brief to the Git-ignored repository-root file `pi-prompts_local.txt`.
4. Start `pi-agent-here(local).bat` in a user-visible Windows Terminal and prove that the actual Pi/Node process started.
5. Avoid production edits and concurrent builds while Pi is working in the shared worktree.
6. Inspect Pi's actual diff and independently validate the result after Pi exits. Treat Pi's report as claims to verify, not proof.
7. Decide whether the result is acceptable. If not, overwrite `pi-prompts_local.txt` with a narrow, evidence-backed correction brief and launch another Pi round.
8. Deliver the final report, including verified behavior, validation, remaining checks, and an English commit-message suggestion required by `AGENTS.md`.

The supervising agent must stop for the user's decision when the request, current implementation, official specification, or architecture constraints disagree in a behavior-changing way. It must not delegate that product decision to Pi.

### Pi Agent

Pi Agent performs the main development round. Its brief must require it to:

1. Read `AGENTS.md` and all named project skills itself, then verify the supervising agent's findings against the current checkout before editing.
2. Implement the smallest coherent change in the owning layer, preserve unrelated changes, and avoid parallel implementations or speculative defects.
3. Add or update focused tests and owned documentation, then run the requested build, CTest, headless, or other validation within the authorized scope.
4. Report changed files, behavior, exact validation evidence, known failures, unperformed checks, and any discrepancy that needs a human decision.
5. Avoid commits, pushes, destructive recovery, scope expansion, and new architecture or compatibility policy unless the user separately authorized them.
6. Never invoke this skill, `pi-agent-here(local).bat`, or another Pi Agent. Pi does not nest Pi.

Pi may refine an implementation detail after inspecting the source, but it must not silently reinterpret acceptance criteria or overrule repository constraints.

## Prepare the Pi brief

Before overwriting `pi-prompts_local.txt`, confirm that it is ignored and inspect `pi-agent-here(local).bat` so the brief matches the current launcher. Include:

- the user's request and observable acceptance criteria;
- current-tree findings, with facts separated from hypotheses;
- relevant owners, symbols, tests, documents, and project skills;
- a concrete implementation path that begins by rechecking those findings;
- scope limits, non-goals, unrelated changes to preserve, and prohibited actions;
- exact build and validation expectations, including real-route safety or hashes when applicable;
- the stopping conditions for ambiguity, specification conflict, build failure, or missing fixtures;
- the required final report and evidence.

Do not ask Pi to blindly apply a predetermined patch. If research shows the requested behavior already exists, instruct Pi to repair only proven gaps and add only evidence that materially strengthens the contract.

## Launch Pi visibly on Windows

Use a visible Windows Terminal window. Do not treat the Windows Terminal launcher PID as proof that Pi started, and do not use `wt --help`, which may open a modal help window.

The batch filename contains parentheses, and raw nested quoting through `wt.exe` is fragile. Prefer an encoded PowerShell child command equivalent to:

```powershell
$piChild = @'
$Host.UI.RawUI.WindowTitle = 'komapedit-pi-agent'
Set-Location -LiteralPath 'E:\github\komapedit'
& '.\pi-agent-here(local).bat'
'@
$piEncoded = [Convert]::ToBase64String([Text.Encoding]::Unicode.GetBytes($piChild))
$piTerminal = "$env:LOCALAPPDATA\Microsoft\WindowsApps\wt.exe"
$piArgs = "-w new new-tab --title komapedit-pi-agent powershell.exe -NoLogo -NoProfile -EncodedCommand $piEncoded"
Start-Process -FilePath $piTerminal -WorkingDirectory 'E:\github\komapedit' -ArgumentList $piArgs -PassThru
```

Resolve the repository path and Terminal executable from the current environment rather than assuming the example paths are valid elsewhere. Keep the title free of spaces. Confirm the marked child PowerShell and actual Pi/Node command line are running; if only Terminal opened or it reports `0x80070002`, fix the launch and confirm that no stray Pi process was created before retrying.

Use the timeout explicitly requested by the user. When no timeout is given, reserve up to 60 minutes for one Pi development round because analysis, building, and headless validation can be lengthy. Reaching the observation deadline is not permission to terminate Pi or discard its work; inspect the state and ask the user if a consequential choice is required.

## Monitor at five-minute intervals

After startup is confirmed, inspect Pi's state only about once every five minutes. Do not poll the process, Terminal, worktree, or build status more frequently merely to produce progress messages.

At each checkpoint, use read-only evidence such as:

- whether the Pi/Node process or an exit marker still exists;
- whether CPU time is advancing or the process is waiting for input;
- `git status --short` and any separately managed test-route repository status;
- whether a build/test child process is active;
- whether the changed-file scope is expanding unexpectedly.

Routine commentary between checkpoints may say that the agent is still waiting, but must not trigger another state inspection. Do not read or inject Terminal input unless evidence shows Pi needs user interaction. Do not start a competing build or edit overlapping files. If Pi exits, proceed to review without waiting for another scheduled checkpoint.

## Review and correct independently

After Pi exits or the user reports completion:

1. Check repository and nested-repository status before trusting the report.
2. Read the complete diff and map every change to an acceptance criterion and owning layer.
3. Recheck applicable format, ABI, source-fidelity, localization, performance, dependency, and documentation constraints.
4. Re-run the smallest decisive validation independently. For real routes, preserve the requested non-write mode and compare pre/post hashes.
5. Check documentation parity, links, encoding, line endings, generated files, temporary artifacts, and `git diff --check`.
6. Distinguish Pi-run evidence from supervising-agent reruns and from unperformed manual checks.

If a defect is proven, do not silently take over the main implementation. Put the exact evidence, expected correction, permitted file scope, and targeted regression command into a replacement `pi-prompts_local.txt`, then repeat the visible-launch and five-minute-monitoring workflow. If Pi cannot proceed or a new decision is required, stop and ask the user rather than widening authority.
