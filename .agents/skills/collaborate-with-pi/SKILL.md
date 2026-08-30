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

1. Read `AGENTS.md`, inspect the current worktree, load every task-relevant project skill, and finish researching the owning code, tests, documentation, and current behavior before delegating. The supervisor, not Pi, owns the repository investigation and development-plan design.
2. Convert the request and research into observable acceptance criteria, resolved component ownership, a complete ordered implementation path, validation commands, evidence requirements, ambiguities, non-goals, protected files, and any required human decisions.
3. Write that finished, directly executable development plan to the Git-ignored repository-root file `pi-prompts_local.txt`. It must tell Pi exactly what to implement and validate in each step; it must not copy or merely paraphrase the user request, leave plan creation to Pi, or use broad instructions such as “research the repository and devise a solution.”
4. Start `pi-agent-here(local).bat` in a user-visible Windows Terminal and prove that the actual Pi/Node process started.
5. Avoid production edits and concurrent builds while Pi is working in the shared worktree.
6. Inspect Pi's actual diff and independently validate the result after Pi exits. Treat Pi's report as claims to verify, not proof.
7. Decide whether a normally completed result is acceptable. If a scoped implementation defect is proven without a Pi-session abnormality, overwrite `pi-prompts_local.txt` with a narrow, directly executable, evidence-backed correction plan and launch another Pi round. Never take over the product implementation personally.
8. Deliver the final report, including verified behavior, validation, remaining checks, and an English commit-message suggestion required by `AGENTS.md`.

The supervising agent must stop for the user's decision when the request, current implementation, official specification, or architecture constraints disagree in a behavior-changing way. It must not delegate that product decision to Pi.

### Pi Agent

Pi Agent performs the main development round. Its brief must require it to:

1. Read `AGENTS.md` and all named project skills itself, then verify the plan's targeted current-tree anchors before editing. This verification is a safety check, not a request for Pi to repeat open-ended repository research or redesign the plan.
2. Execute the supplied ordered development plan, implementing the smallest coherent change in the identified owning layer, preserving unrelated changes, and avoiding parallel implementations or speculative defects.
3. Add or update focused tests and owned documentation, then run the requested build, CTest, headless, or other validation within the authorized scope.
4. Report changed files, behavior, exact validation evidence, known failures, unperformed checks, and any discrepancy that needs a human decision.
5. Avoid commits, pushes, destructive recovery, scope expansion, and new architecture or compatibility policy unless the user separately authorized them.
6. Never invoke this skill, `pi-agent-here(local).bat`, or another Pi Agent. Pi does not nest Pi.

Pi may refine an implementation detail after inspecting the source, but it must not silently reinterpret acceptance criteria or overrule repository constraints.

## Prepare a directly executable Pi development plan

Do not write `pi-prompts_local.txt` until the supervising agent has completed the repository-specific investigation and resolved the implementation route as far as the current request and repository authority allow. Before overwriting it, confirm that it is ignored and inspect `pi-agent-here(local).bat` so the plan matches the current launcher.

The file must be a self-contained, directly executable development plan. Give each ordered step enough detail for Pi to act without first inventing the plan, including the relevant files and symbols, intended change, existing owner or reuse path, constraints, focused coverage, validation command, and expected evidence. Include:

- the user's request and observable acceptance criteria;
- researched current-tree findings, with facts separated from any remaining hypotheses;
- relevant owners, symbols, tests, documents, and project skills;
- a complete ordered implementation path from the owning representation through every requested consumer, test, validation, and owned documentation update;
- scope limits, non-goals, unrelated changes to preserve, and prohibited actions;
- exact build and validation expectations, including real-route safety or hashes when applicable;
- the stopping conditions for ambiguity, specification conflict, build failure, or missing fixtures;
- the required final report and evidence.

Copying or lightly rewording the user's prompt is not a valid plan. Neither is a prompt that asks Pi to “investigate,” “design a solution,” “make a plan,” or broadly search the repository before deciding what to change. Pi may recheck the plan's named factual anchors and make local implementation decisions within the supplied route; if a material premise is wrong, it must stop and report the discrepancy instead of replacing the supervisor's plan with its own.

Do not ask Pi to blindly apply a predetermined textual patch. Specify behavior, ownership, reuse points, and evidence rather than fabricated line edits. If the supervisor's research shows that the requested behavior already exists, instruct Pi to repair only proven gaps and add only evidence that materially strengthens the contract.

## Terminate the collaboration on Pi-session abnormalities

Treat the following as Pi-session abnormalities: a hung or nonresponsive Pi/Node process, an unexpected process exit or crash, a provider/authentication/balance/model/tool error, corrupted or unusable session state, clear off-task behavior such as extensive unrelated exploration, or any user report that the Pi session is abnormal or has materially departed from the task.

When any such abnormality occurs:

1. Terminate the exact Pi session immediately if it is still running. If the user reports the abnormality, do not wait for the next five-minute checkpoint or argue with the report.
2. Do not restart or retry Pi in the same turn. Do not select a different model or provider, edit `pi-agent-here(local).bat`, alter Pi configuration, credentials, environment, cache, or launcher behavior, or rewrite the prompt to work around the failure.
3. Do not take over the feature implementation, modify production code, or start a build/test run as a substitute for Pi.
4. Preserve any existing worktree changes. Perform only the smallest read-only status, process, diff-summary, or hash checks needed to report what state Pi left behind; do not clean, reset, delete, or repair its partial work.
5. Report the abnormal termination, the observed error or user report, whether any files changed, and which requested work and validation remain incomplete, then end the collaboration turn. Resume only after a new explicit user instruction authorizes another Pi attempt or a different course of action.

Ordinary scoped compiler or test failures encountered by a healthy Pi session are implementation evidence, not automatically a session abnormality; Pi may diagnose and fix them within the supplied plan. A user report of abnormal Pi behavior always triggers immediate termination regardless of that distinction.

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

Resolve the repository path and Terminal executable from the current environment rather than assuming the example paths are valid elsewhere. Keep the title free of spaces. Confirm the marked child PowerShell and actual Pi/Node command line are running. If only Terminal opened, it reports `0x80070002`, or Pi otherwise fails to start correctly, treat that as a Pi-session abnormality: terminate any exact stray session, report the failure, and do not repair the launcher or retry in the same turn.

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

If a checkpoint proves that Pi is hung, crashed, or otherwise abnormal, follow the immediate-termination procedure above. A user-reported abnormality bypasses the checkpoint schedule and must be acted on immediately.

## Review and correct independently

After Pi exits or the user reports completion:

1. Check repository and nested-repository status before trusting the report.
2. Read the complete diff and map every change to an acceptance criterion and owning layer.
3. Recheck applicable format, ABI, source-fidelity, localization, performance, dependency, and documentation constraints.
4. Re-run the smallest decisive validation independently. For real routes, preserve the requested non-write mode and compare pre/post hashes.
5. Check documentation parity, links, encoding, line endings, generated files, temporary artifacts, and `git diff --check`.
6. Distinguish Pi-run evidence from supervising-agent reruns and from unperformed manual checks.

If a defect is proven after a normal Pi completion, do not silently take over the main implementation. Put the exact evidence, expected correction, permitted file scope, and targeted regression command into a replacement directly executable `pi-prompts_local.txt`, then repeat the visible-launch and five-minute-monitoring workflow. If Pi cannot proceed, the Pi session becomes abnormal, or a new decision is required, terminate or stop as applicable and ask the user rather than widening authority.
