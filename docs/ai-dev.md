# Developing komapedit with AI Coding Tools

[Chinese version](ai-dev_zhcn.md) · [Human Developer Guide](dev.md) · [Repository Rules for AI Tools](../AGENTS.md) · [Development Progress TODO](../TODO.md)

This document is for people who use AI coding tools to develop komapedit. It explains how to prepare tasks, supervise tools, review changes, and determine whether the result is safe to accept. It is not a prompt collection, and it should not be copied in its entirety into an AI coding tool.

The AI tool itself should read [`AGENTS.md`](../AGENTS.md), which contains the detailed repository rules. The person operating the tool remains responsible for the objective, scope, technical decisions, validation, and final acceptance.

## AI Agents Are Not Gods; They Have Many Limitations

The field of artificial intelligence is currently full of hype and exaggerated claims. This leads many people to believe that “an AI tool can do anything from a short one-sentence instruction.” In reality, AI has many shortcomings, some of which can cause serious trouble. If you know little about the relevant facts but plan to use an AI coding tool to develop this project, you must understand the following before changing any code:

1. **Hallucinations:** Large language models (LLMs) based on the Transformer architecture commonly suffer from a serious problem called “hallucination.” Hallucinations can occur in any model, even the most advanced models available today. This means a model may generate content that “looks fine but deviates significantly from the facts or instructions,” without judging for itself whether the content is factual or meets the user's needs.
2. **Information loss in long contexts:** Today's leading models commonly claim to have context windows of about one million tokens, but as the amount of information in the input context grows, some information is lost. It is impossible to predict what will be lost, which means the model may gradually drift away from the user's requirements and project rules as the context grows. In general, use only about 25% of the context window in a single conversation. For especially large tasks, split the work into multiple steps and have a separate conversation with the AI agent for each step. Some AI agents provide context compression; generally, once compression has been triggered once in a conversation, it is not recommended to continue using that conversation for another round of changes.
3. **The importance of prompts:** Prompts determine what the AI tool does according to your instructions. Make your instructions as clear as possible, state all requirements, and avoid making the AI agent guess what you need.

## Basic Knowledge You Should Have

Using AI does not mean that you can complete the desired software changes without understanding software development. The following are some points you should know:

1. Understand the basics of software development and related tools. You do not need to install a complete integrated development environment (IDE), but you should at least install the build tools required by this project and one text editor for manually viewing and editing code.
2. Have at least some knowledge of C/C++. You do not need practical project-development experience, but you should know basic C/C++ syntax, the purposes of various files such as `.h` and `.cpp`, and the purposes of the related tools.

## Avoid Vague Requirements and “Wishful Coding”

- Do not give the AI tool unconstrained, vague requests such as “make the code better,” “rewrite the entire project using xx framework,” “fix all bugs,” “improve performance,” or “add a feature.” If the agent is allowed to implement such a request directly, it can only guess the product requirements, architecture, scope, and acceptance criteria. This is “Wishful Coding.”

- If the program has a problem or the UI is unattractive, do not only say “the program does not work,” “there is an error,” “the program suddenly crashed,” “a menu looks ugly,” or “a marker is not symmetrical.” This only makes the model guess at the problem and may cause the problem to become worse during attempted fixes.

- If the requirements contain elements that cannot be determined independently, first ask the agent to perform read-only checks instead of immediately modifying code. You can use an agent's “plan” mode for this. The operator should review the evidence, fill in the missing decisions, and then approve an implementation with a clearly defined scope.

- If an AI agent's static code inspection cannot answer an important choice, such as the file-format strategy, compatibility level, breaking migration, or addition of a dependency, pause and have a person make the decision. Do not let the tool choose silently.

## Prompt Templates for Three Scenarios

Fill in the prompt templates according to the actual situation, or select only part of a template for the actual input. Do not copy an unmodified prompt template into an AI agent.

### Everyday Development

This prompt is for adding features and similar development tasks.

```
According to the following specification documents and task requirements, execute the development task. Plan before execution:
Project development rules: AGENTS.md

Request: [Fill in your detailed request]
When implementing the request above, make the code as performant as possible, keep the structure clear and readable, and avoid scattering large amounts of duplicated logic throughout the codebase.
If the request description is inconsistent with the actual code or unclear, ask about it promptly instead of making an arbitrary decision.

Build: use build_dev.bat to generate a debug build; do not perform a release build.
Tests:
[1 - No testing] UI-related functionality requires manual testing later. Do not perform automated tests for this task.
[2 - Standard testing] Use the real map “[BVE map file path]” for headless testing. If problems occur during compilation or testing, other debugging or testing tools may also be used.
Goal: The program should work as required, perform well, and keep existing functionality working normally.
Completion report: After completing the task, explain the detailed logic and approach of this round of changes so that it can be checked manually. In the completion report, include an English Git commit title in the format (commit category: what was done).
```

### Bug Fixing

This prompt is for fixing problems.

```
According to the following specification documents and task requirements, execute the bug-fixing task. Review the code first, then make a detailed plan based on the problems you find before making changes:
Project development rules: AGENTS.md

Problem description and fix requirements: [Fill in the observed symptoms, console logs, and the effect that should be achieved after the fix]

When implementing the request above, make the code as performant as possible and keep the structure clear and readable. Do not add an equivalent implementation where one already exists. Avoid scattering large amounts of duplicated logic throughout the codebase, and avoid unnecessary repeated loading, reading, or computation. If the problem cannot be determined, explicitly report that it cannot be determined; do not perform a large-scale rewrite while the problem remains uncertain.
If the request description is inconsistent with the actual code or unclear, ask about it promptly instead of making an arbitrary decision.
Build: use build_dev.bat to generate a debug build; do not perform a release build.
Tests: [1 - No testing] UI-related functionality requires manual testing later. Do not perform automated tests for this task.
[2 - Standard testing] Use the real map “[BVE map file path]” for headless testing. If problems occur during compilation or testing, other debugging or testing tools may also be used.
Goal: The program should work as required, perform well, and keep existing functionality working normally.
Completion report: After completing the task, explain the detailed logic and approach of this round of changes so that it can be checked manually. In the completion report, include an English Git commit title in the format (commit category: what was done).
```

### slop-fix

Even when an AI coding tool is required to follow strict project rules and precise instructions, it may leave flaws in the code. One or more slop-fix passes may then be needed to keep the project sustainable for continued development.

```
The current project development rules are in: AGENTS.md
To prevent the project code from drifting away from the requirements and rules, perform a development-sustainability (slop-fix) inspection and repair of the current project. First perform a read-only inspection and create a plan; list the problems you find in the plan before making changes. If a described problem does not exist, skip that item:
1. Check whether large amounts of identical implementation logic are scattered across different locations.
2. Check for fields, functions, or variables that are no longer used.
3. Check whether any locations can use less code without changing functionality at all. (Absolutely do not reduce the line count by deleting comments or combining existing multi-line code into a single line.)
4. Check for defects that manual testing cannot reveal but that are visible at the code level.
5. Check for inefficient logic that can be optimized, such as frequent reloading, reading, computation, or generation.
6. Check for hidden risks that could cause the program to crash or become completely stuck while performing an operation.
7. Check for confusing or unclear logic in the code.

Use the real map “[BVE map file path]” during the testing phase. If no code was modified because no problems were found, testing is not required.

After checking and correcting the problems above, the following should be true:
1. The codebase is smaller and easier to maintain. However, do not force a reduction in code size when there is no suitable place to reduce it.
2. None of the existing functionality or user-operation logic changes.
3. Program performance does not decline.
4. If a corresponding problem exists, fix it. If it does not exist, make no change. Do not invent defects and rewrite or fix code merely to satisfy the goal of “fixing problems.”
```

## Key Technical Review Areas

Even when a change appears small, check the project-specific risks that apply:

- **Public ABI:** Exact versions and structure sizes, explicit ownership, matching free functions, and no exceptions or STL types crossing the C boundary.
- **Route-source fidelity:** Strict adherence to official BVE syntax, Include context, original parameter text, parse order, encoding, BOM, line endings, and Apply/Revert/Save/Reload behavior.
- **User interface:** Simplified Chinese, English, and Japanese text; table/plan/scene navigation; marker visibility and hit-test priority; and settings persistence.
- **Performance:** No unconditional per-frame I/O or rebuilding, no repeated decoding or hashing of unchanged input, and complete cache keys with clearly owned invalidation.
- **Dependencies:** Maintenance status, Windows compatibility, build and binary costs, licenses, ABI impact, and packaging and migration risks.
- **Distribution:** Unless the task actually requires an update, `LICENSE`, `NOTICE`, and `THIRD_PARTY_NOTICES.md` should remain present and unchanged.

Runtime map data and editing commands must continue to use the versioned, strongly typed ABI. Alternative parsers, text-serialized route transports, fallback data models, or disk snapshot caches require explicit human approval, and the task itself must genuinely require an architectural change.

## When to Stop the Agent Immediately

Immediately stop the AI agent's work in any of the following situations:

1. The changes clearly deviate from the requirements, modify unrelated code, or perform a large-scale rewrite of a module.
2. The model enters an infinite loop, repeatedly outputting the same passage or performing the same operation.
3. The changes introduce code files in languages other than C++, such as `.py` files.
4. A large amount or all of the code is deleted, leaving the project obviously damaged to the point that it cannot be restored to its original state.
