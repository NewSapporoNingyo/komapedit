# Contributing to komapedit

Thank you for your interest in contributing to komapedit. This document outlines guidelines and requirements for pull requests (PRs). Pull requests that do not follow these guidelines may require revisions or be closed without being merged.

### 1. Before You Begin

- Please read the [README](README.md) and the [Developer Guide](docs/dev.md). When using AI tools to assist development, you must also follow the guidelines in [`AGENTS.md`](AGENTS.md).
- Search existing issues, pull requests, documentation, and the current source code to confirm that the feature or fix does not already exist and is not currently being worked on by someone else.
- For major changes—such as features spanning multiple modules, new file-format strategies, public ABI changes, new dependencies, or architectural modifications—please open an issue first to reach consensus with maintainers on the implementation plan before writing code.

### 2. Develop on the `dev` Branch

- All development must be based on the `dev` branch, **not** the `main` branch. The `main` branch is updated from `dev` exclusively by maintainers; do not develop directly on `main` or submit pull requests targeting `main`.
- Fork this repository, create a topic branch from the latest `dev`, and submit your pull request targeting `dev`.
- Pull requests targeting `main` will be asked to change their target branch or will be closed outright.
- Keep your branch synchronized with `dev` and resolve any conflicts before requesting review.
- A pull request should have a single purpose. Do not mix in unrelated fixes, refactorings, or formatting adjustments.

Below are reference Git commands for cloning and branching the repository. If you prefer a GUI, you can perform equivalent actions using tools like GitHub Desktop:

```bash
git clone https://github.com/<your-account>/komapedit.git
cd komapedit
git remote add upstream https://github.com/NewSapporoNingyo/komapedit.git
git fetch upstream
git switch -c fix/short-description upstream/dev
```

### 3. Pull Request Titles and Descriptions Must Be in English

- Pull request titles and descriptions **must be in English**—not Chinese, Japanese, or any other language—so that all contributors and reviewers can understand their content. Simple, clear English is sufficient; as long as you verify that the meaning is accurate, you may write in your native language first and use machine translation.
- Commit messages must also be in English. Using English for review discussions is also recommended.
- UI text translations and localized documentation will naturally contain other languages; this rule applies specifically to the pull request text itself.
- Pull request descriptions should include the following sections (please fill them out in English):

```text
## Purpose
Why is this change needed? Link the related issue if there is one.

## Changes
What was changed, and in which components?

## Validation
Commands actually run and their results, routes or files used, and manual GUI checks performed.

## Screenshots
Required for visible UI changes.

## Compatibility and risks
Behavior, file-format, ABI, performance, or known limitations to be aware of.

## AI assistance
Whether AI coding tools were used, and for which parts.
```

### 4. Rules for AI-Assisted Development

- AI coding agents may be used to assist development. The repository provides [`AGENTS.md`](AGENTS.md), [project skills](.agents/skills), and the [AI-assisted development guide](docs/ai-dev.md) for this purpose.
- The person submitting the pull request is considered its author and bears full responsibility for it. You must understand **what was done in this change** and why it was needed. You must be able to explain the changes yourself and respond to review comments.
- Before submitting, personally inspect the full diff, run the build and tests yourself, remove unrelated modifications and generated files, and verify that the reported results are genuine rather than fabricated by AI tools.
- The following types of pull requests will not be merged, and users who repeatedly submit them will be blocked:
  - Pull requests with no substantive developer involvement that were entirely delegated to AI agents (such as PRs automatically generated from issues or `TODO.md` entries and submitted without review);
  - Pull requests created merely to "pick a random project and write something," such as farming contribution counts or activity;
  - Batch or automated submissions, such as large numbers of auto-generated PRs or trivial edits across many files without an actual need;
  - Copying another user's pull request and attempting to submit it.

### 5. Follow Project Specifications and Avoid Duplicate Implementations

Regardless of whether AI tools are used, the non-negotiable constraints in [`AGENTS.md`](AGENTS.md) apply to all contributors. Key requirements include:

- Use C++17 and the existing Windows, Win32, DirectX 11, WIC, Dear ImGui, ImPlot, CMake, and Ninja tech stack. Do not introduce other GUI frameworks, build systems, or package managers without prior approval.
- Preserve the three runtime components—`maploader.dll`, `model_loader.dll`, and `komapedit.exe`—and their versioned C ABIs. Do not let STL types, C++ classes, or exceptions cross the C ABI boundary.
- Implement only official BVE syntax; do not add private route syntax extensions. When reading or writing route files, preserve encoding, BOM, line endings, Include statements, comments, and statement order. If you need to add support for older BVE versions or maps from other train simulation games, please contact the maintainers first.
- Map editing must be implemented through the shared, strongly typed, source-backed editing pipeline. Do not reparse route source in GUI code, and do not duplicate string rewrite logic in UI features.
- Visible UI text must be updated across all five supported languages simultaneously: English, Simplified Chinese, Traditional Chinese (Hong Kong), Traditional Chinese (Taiwan), and Japanese.
- Do not modify third-party source code in `third_party`. Adding new dependencies requires clear justification, CMake integration, documentation updates, and license notice updates.
- Avoid redundant I/O, per-frame rebuilds, and accidental O(n²) operations. Any performance claims must provide before-and-after benchmark data under identical conditions.

**Features already existing in the project must not be reimplemented.** Before implementation, check the source code, data tables, menus, documentation, and `TODO.md` to confirm whether an implementation already exists. Extend existing shared helper functions and processing pipelines rather than creating parallel implementations. If an existing feature is incomplete or imperfect, improve it in place and explain your rationale in the pull request.

Keep changes minimal and place them within the module responsible for that feature. Do not commit `build/`, `build_release/`, cloned `third_party` directories, generated settings files, CSV or test outputs, temporary route/map/model files, or machine-specific local paths.

### 6. Build and Validation

For environment setup, refer to the [Developer Guide](docs/dev.md). The basic workflow is as follows:

```bat
.\build_dev.bat
ctest --test-dir build --output-on-failure
```

- For code changes, the Debug build must succeed, and registered CTest contract tests must pass. The diagnostics contract test requires local test fixtures under `tests/` (containing minimal maps with valid and invalid syntax).
- Add or update targeted tests whenever the changed behavior has a deterministic contract.
- If changes affect packaging, runtime DLL layout, or optimization-dependent behavior, also run `.\build_release.bat`.
- Manually verify affected GUI behavior. For editing features, save, reload, and compare file contents, including encoding and line endings.
- Report only commands you actually ran and their real results, including any known failures.

### 7. Documentation

- Update documentation corresponding to the changed behavior: document user-visible behavior and syntax support in the README and localized versions under `docs/`, usage in the user manual, architecture and validation in the Developer Guide, and development status in `TODO.md` / `docs/TODO_done.md`.
- When modifying multilingual documentation, keep content consistent across all language versions. If you are unable to write in a specific language, please note this in your pull request.
- Save documentation files as UTF-8 without BOM, preserving the existing line-ending format of each file.

### 8. Commit Messages

- Commit messages must be written in English using the format `Change Type: What was done`, such as `Fix: preserve CRLF when saving Station lists` or `Docs: update build instructions`.
- Keep each commit focused on a single purpose. Where feasible, clean up meaningless commits (such as `wip`) before requesting review.

### 9. Reporting Issues

- When reporting bugs, please provide the komapedit version or commit hash, build type (Release/Debug), Windows version, reproduction steps, expected vs. actual behavior, and relevant logs or screenshots.
- Whenever possible, attach a minimal reproducible route sample. Share only route files, models, and textures that you have the right to distribute.
- When submitting feature requests, describe your use case. When BVE syntax is involved, include links to the relevant [official BVE specification](https://bvets.net/jp/edit/formats/route/map.html).

### 10. Review and Merging

- Maintainers will review PRs for purpose, compliance with project standards, correctness, validation evidence, and maintainability. A successful build alone does not guarantee a merge.
- Maintainers may request changes, ask you to split the pull request, or close it. Pull requests left unresponsive to review feedback for an extended period may be closed.
- Please communicate in a respectful and constructive manner.

### 11. Licensing

- komapedit is released under the [Apache License, Version 2.0](LICENSE). Unless explicitly stated otherwise, contributions submitted by you will be licensed under the same terms.
- Submit only content you have the right to contribute. Do not copy code with incompatible licenses or without proper attribution.
- Do not remove existing copyright or license notices. When adding third-party code, update [`NOTICE`](NOTICE) or [`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md) as required.

### Pull Request Checklist

- [ ] The branch is based on `dev`, and the pull request targets `dev`.
- [ ] The title and description are written in English.
- [ ] The pull request has a clear purpose; I understand and can explain every change, including parts completed with AI assistance.
- [ ] I have confirmed that this change does not duplicate existing functionality.
- [ ] The changes comply with `AGENTS.md` and the Developer Guide.
- [ ] The Debug build succeeded, relevant tests passed, and validation methods have been described.
- [ ] If visible UI text was changed, all five supported UI languages have been updated simultaneously.
- [ ] Documentation has been updated where necessary.
- [ ] No build artifacts, generated files, local paths, or unrelated changes are included.

---

# 对komapedit项目进行贡献

感谢你有意为 komapedit 做出贡献。本文档规定了 Pull Request（PR）的规范。不符合这些规范的 Pull Request 可能会被要求修改，或在不合并的情况下被关闭。

### 1. 开始之前

- 请阅读 [README](docs/README_zhcn.md)、[开发者指南](docs/dev_zhcn.md)、使用AI工具辅助开发时，也应遵守[`AGENTS.md`](AGENTS.md)中的规范。
- 请搜索现有的 Issue、Pull Request、文档和当前源码，确认该功能或修复尚不存在，也没有其他人正在进行。
- 对于大型变更，例如跨多个模块的功能、新的文件格式策略、公共 ABI 变更、新依赖或架构变更，请先创建 Issue，与维护者就实现方案达成一致后再编写代码。

### 2. 基于 `dev` 分支进行开发

- 所有开发都必须基于 `dev` 分支，**而不是** `main` 分支。`main` 由维护者从 `dev` 更新；请勿在 `main` 上开发，也不要向 `main` 提交 Pull Request。
- 请 Fork 本仓库，从最新的 `dev` 创建主题分支，并以 `dev` 为目标分支提交 Pull Request。
- 以 `main` 为目标的 Pull Request 会被要求更改目标分支，或被直接关闭。
- 请让你的分支与 `dev` 保持同步，并在请求审查前自行解决冲突。
- 一个 Pull Request 只应有一个目的。请勿混入无关的修复、重构或格式调整。

以下是供参考的用于拉取仓库的Git命令，如果你偏好图形化界面，也可以使用Github Desktop等工具完成类似的操作：

```bash
git clone https://github.com/<your-account>/komapedit.git
cd komapedit
git remote add upstream https://github.com/NewSapporoNingyo/komapedit.git
git fetch upstream
git switch -c fix/short-description upstream/dev
```

### 3. Pull Request 的标题和描述必须使用英语

- Pull Request 的标题和描述**必须使用英语**，不可使用中文、日语或其他语言，以便所有贡献者和审查者都能理解其中的内容。使用简单且清晰的英语即可；在确认含义正确的前提下，也可以先用母语书写，再使用机器翻译。
- 提交信息（commit message）也必须使用英语。审查讨论同样建议使用英语。
- 界面文本的翻译和本地化文档自然会包含其他语言；本规则针对的是 Pull Request 本身的文字。
- Pull Request 描述应包含以下内容（请用英语填写）：

```text
## Purpose
Why is this change needed? Link the related issue if there is one.

## Changes
What was changed, and in which components?

## Validation
Commands actually run and their results, routes or files used, and manual GUI checks performed.

## Screenshots
Required for visible UI changes.

## Compatibility and risks
Behavior, file-format, ABI, performance, or known limitations to be aware of.

## AI assistance
Whether AI coding tools were used, and for which parts.
```

### 4. AI 辅助开发相关规则

- 可以使用 AI 编程智能体辅助开发。仓库为此提供了 [AGENTS.md](AGENTS.md)、[项目技能](.agents/skills) 和 [AI 辅助开发指南](docs/ai-dev_zhcn.md)。
- 提交 Pull Request 的人就是该 PR 的作者，并对其负责。你必须理解**这一次开发做了什么事情**，以及为什么需要这样做。你必须能够亲自解释这些变更并回应审查意见。
- 提交前，请亲自检查完整的 diff，亲自运行构建和测试，移除无关修改和生成文件，并确认所报告的结果真实存在而非由工具编造。
- 以下形式的 Pull Request 不会被合并，反复多次提交类似的 Pull Request 的用户也会被屏蔽：
  - 没有开发者实质参与，全程由智能体代工的 Pull Request，例如根据 Issue 或 `TODO.md` 条目自动生成，并且未经审查就提交的 PR；
  - 以“随便找一个项目写点东西”为目的的 Pull Request，例如为了刷贡献次数或活跃度；
  - 批量或自动化提交，例如大量自动生成的 PR，或在没有实际需要的情况下对大量文件进行的琐碎修改。
  - 复制其他用户的 Pull Request 并尝试提出。

### 5. 遵守项目开发规范，避免重复实现

无论是否使用 AI 工具，[AGENTS.md](AGENTS.md) 中不可违反的约束都适用于所有贡献者。重点包括：

- 使用 C++17 以及现有的 Windows、Win32、DirectX 11、WIC、Dear ImGui、ImPlot、CMake 和 Ninja 技术栈。未经事先同意，请勿引入其他 GUI 框架、构建系统或包管理器。
- 保留 `maploader.dll`、`model_loader.dll` 和 `komapedit.exe` 三个运行时组件及其带版本的 C ABI。请勿让 STL 类型、C++ 类或异常跨越 C ABI 边界。
- 只实现官方 BVE 语法，不要添加私有的线路语法扩展。读写线路文件时，请保留编码、BOM、换行符、Include、注释和语句顺序。若需要加入对旧版BVE或其它铁路模拟游戏内的地图的支持，请先联系开发者。
- 地图编辑必须通过共享的强类型、源码驱动编辑管线实现。请勿在 GUI 中重新解析线路源码，也不要在界面功能中重复编写字符串改写逻辑。
- 可见的界面文本须同时更新英语、中文（简体、香港繁体、台湾繁体）和日语5种语言。
- 请勿修改 `third_party` 中的第三方源码。新增依赖需要明确的理由、CMake 集成、文档以及许可证声明的更新。
- 避免重复 I/O、逐帧重建和意外的 O(n²) 运算。性能改进的结论需要在相同条件下提供前后对比数据。

**项目内已有的功能不应被重复实现。** 实现之前，请在源码、数据表、菜单、文档和 `TODO.md` 中确认是否已有实现。请扩展现有的共享辅助函数和处理管线，而不是另建一套并行实现。如果现有功能不够完善，请在原处改进，并在 Pull Request 中说明原因。

请将变更控制在最小范围，并放在负责该功能的模块中。请勿提交 `build/`、`build_release/`、克隆的 `third_party` 目录、生成的设置文件、CSV 或测试输出、临时线路/地图/模型文件，以及特定于本机的本地路径。

### 6. 构建与验证

环境配置请参阅[开发者指南](docs/dev_zhcn.md)。基本流程如下：

```bat
.\build_dev.bat
ctest --test-dir build --output-on-failure
```

- 对于代码变更，Debug 构建必须成功，且已注册的 CTest 契约测试必须通过。诊断契约测试需要 `tests/` 下的本地测试数据（包含语法正确/错误的小地图）。
- 当变更的行为具有确定性的契约时，请添加或更新针对性的测试。
- 如果变更影响打包、运行时 DLL 布局或与优化相关的行为，还需运行 `.\build_release.bat`。
- 请手动检查受影响的 GUI 行为。对于编辑功能，请保存、重新加载并比较文件内容，包括编码和换行符。
- 只报告你实际运行过的命令及其真实结果，包括已知的失败项。

### 7. 文档

- 请更新与所变更行为对应的文档：用户可见的行为和语法支持写入 README 及 `docs/` 下的各语言版本，使用方法写入用户手册，架构与验证写入开发者指南，开发状态写入 `TODO.md` / `docs/TODO_done.md`。
- 修改有多语言版本的文档时，请保持各语言版本内容一致。如果你无法编写某种语言，请在 Pull Request 中说明。
- 文档请以不带 BOM 的 UTF-8 保存，并保留各文件现有的换行符格式。

### 8. 提交信息

- 提交信息请使用英语，格式为 `Change Type: What was done`，例如 `Fix: preserve CRLF when saving Station lists` 或 `Docs: update build instructions`。
- 保持每次提交的目的单一。在可行的情况下，请在请求审查前整理 `wip` 之类无意义的提交。

### 9. 报告问题

- 报告缺陷时，请提供 komapedit 的版本或提交、构建类型（Release/Debug）、Windows 版本、复现步骤、预期行为与实际行为，以及日志或截图。
- 尽可能附上最小化的线路样例。只分享你有权分享的线路文件、模型和贴图。
- 提出功能请求时，请描述使用场景。涉及 BVE 语法时，请附上相关的 [BVE 官方规范](https://bvets.net/jp/edit/formats/route/map.html)链接。

### 10. 审查与合并

- 维护者会审查 PR 的目的、是否遵守项目规范、正确性、验证证据和可维护性。仅仅构建成功并不保证会被合并。
- 维护者可能会要求修改、要求拆分 Pull Request，或将其关闭。长时间未回应审查意见的 Pull Request 可能会被关闭。
- 请以尊重、建设性的态度进行交流。

### 11. 许可证

- komapedit 以 [Apache License, Version 2.0](LICENSE) 发布。除非你另有明确声明，你提交的贡献将以相同的许可证授权。
- 请只提交你有权贡献的内容。请勿复制许可证不兼容或未正确注明出处的代码。
- 请勿删除现有的版权或许可证声明。添加第三方代码时，请按要求更新 [`NOTICE`](NOTICE) 或 [`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md)。

### Pull Request 检查清单

- [ ] 分支基于 `dev`，且 Pull Request 的目标分支为 `dev`。
- [ ] 标题和描述使用英语编写。
- [ ] Pull Request 有明确的目的，我理解并能解释其中的每一处变更，包括由 AI 辅助完成的部分。
- [ ] 我已确认该变更没有重复实现已有功能。
- [ ] 变更遵守 `AGENTS.md` 和开发者指南。
- [ ] Debug 构建成功，相关测试通过，并已说明验证方式。
- [ ] 如有可见界面文本变更，已同步更新全部五种界面语言。
- [ ] 如有需要，已更新文档。
- [ ] 不包含构建产物、生成文件、本地路径或无关变更。

---

# komapedit への貢献

komapedit への貢献に関心をお寄せいただき、ありがとうございます。本文書では、プルリクエスト（PR）に関するガイドラインおよび要件を定めています。これらのガイドラインに沿わないプルリクエストは、修正を求められるか、マージされずにクローズされる場合があります。

### 1. はじめる前に

- [README](docs/README_jp.md) および[開発者ガイド](docs/dev_jp.md)をお読みください。AI ツールを利用して開発を行う場合は、[AGENTS.md](AGENTS.md) の規則にも従う必要があります。
- 既存の Issue、プルリクエスト、ドキュメント、現在のソースコードを検索し、その機能や不具合修正がすでに存在していないか、あるいは他の誰かが現在作業中でないかをご確認ください。
- 複数モジュールにまたがる機能、新しいファイル形式の方針、公開 ABI の変更、新しい依存関係の追加、アーキテクチャの変更といった大規模な変更については、コードを書く前にまず Issue を作成し、実装計画についてメンテナーと合意を形成してください。

### 2. `dev` ブランチをベースとした開発

- すべての開発は `main` ブランチ**ではなく**、`dev` ブランチをベースに行う必要があります。`main` ブランチはメンテナーのみが `dev` から更新します。`main` 上で直接開発を行ったり、`main` を対象としてプルリクエストを送信したりしないでください。
- 本リポジトリをフォークし、最新の `dev` からトピックブランチを作成した上で、`dev` をターゲットブランチとしてプルリクエストを送信してください。
- `main` をターゲットとするプルリクエストは、ターゲットブランチの変更を求められるか、直接クローズされます。
- ブランチを常に `dev` と同期させ、レビューを依頼する前にコンフリクトを解決してください。
- 1つのプルリクエストには、単一の目的のみを持たせるようにしてください。無関係な修正、リファクタリング、フォーマットの調整を混在させないでください。

以下は、リポジトリのクローンやブランチ作成の参考となる Git コマンドです。GUI を好む場合は、GitHub Desktop などのツールを使用して同様の操作を行うこともできます：

```bash
git clone https://github.com/<your-account>/komapedit.git
cd komapedit
git remote add upstream https://github.com/NewSapporoNingyo/komapedit.git
git fetch upstream
git switch -c fix/short-description upstream/dev
```

### 3. プルリクエストのタイトルと説明には英語を使用する

- プルリクエストのタイトルと説明は、すべてのコントリビューターおよびレビュアーが内容を理解できるようにするため、**必ず英語を使用してください**。日本語、中国語、又はその他の言語は使用できません。平易で明瞭な英語で十分です。意味が正確であることを確認した上であれば、最初に母国語で記述してから機械翻訳を利用しても構いません。
- コミットメッセージ（commit message）も必ず英語を使用してください。レビュー時の議論についても同様に英語の使用を推奨します。
- UI テキストの翻訳やローカライズされたドキュメントには当然ながら他言語が含まれますが、本ルールはプルリクエスト自体の記述に適用されます。
- プルリクエストの説明には以下のセクションを含めてください（英語で記入してください）：

```text
## Purpose
Why is this change needed? Link the related issue if there is one.

## Changes
What was changed, and in which components?

## Validation
Commands actually run and their results, routes or files used, and manual GUI checks performed.

## Screenshots
Required for visible UI changes.

## Compatibility and risks
Behavior, file-format, ABI, performance, or known limitations to be aware of.

## AI assistance
Whether AI coding tools were used, and for which parts.
```

### 4. AI 補助開発に関する規則

- AI コーディングエージェントを利用して開発を補助することができます。リポジトリにはそのための [AGENTS.md](AGENTS.md)、[プロジェクトスキル](.agents/skills)、および [AI コーディングツールを使った開発ガイド](docs/ai-dev_jp.md) が用意されています。
- プルリクエストを提出した本人がその PR の作成者であり、全責任を負います。**今回の開発で何が行われたのか**、なぜそれが必要だったのかを理解していなければなりません。自分自身で変更内容を説明し、レビューコメントに回答できる必要があります。
- 提出前に、差分（diff）全体をご自身で確認し、ビルドとテストを自ら実行し、無関係な変更や生成ファイルを削除し、報告する結果がツールの捏造ではなく実際に得られたものであることを確認してください。
- 以下のような形式のプルリクエストはマージされず、同様のプルリクエストを繰り返し送信するユーザーはブロックされます：
  - 開発者の実質的な関与がなく、全体をエージェントが代行したプルリクエスト（例：Issue や `TODO.md` の項目から自動生成され、未確認のまま送信された PR など）。
  - 「適当なプロジェクトを選んで何か書く」ことだけを目的としたプルリクエスト（例：貢献回数やアクティビティ稼ぎなど）。
  - 一括または自動化された送信（例：大量の自動生成 PR や、実際の必要性がないにもかかわらず多数のファイルに対して行われた些細な変更など）。
  - 他のユーザーのプルリクエストをコピーして提出しようとしたもの。

### 5. プロジェクト仕様を遵守し、重複実装を避ける

AI ツールの使用有無にかかわらず、[`AGENTS.md`](AGENTS.md) に記載されている厳格な制約事項はすべてのコントリビューターに適用されます。主な要件は以下のとおりです：

- C++17 および既存の Windows、Win32、DirectX 11、WIC、Dear ImGui、ImPlot、CMake、Ninja の技術スタックを使用してください。事前の合意なしに、他の GUI フレームワーク、ビルドシステム、パッケージマネージャーを導入しないでください。
- `maploader.dll`、`model_loader.dll`、`komapedit.exe` の 3 つのランタイムコンポーネントと、それぞれのバージョン付き C ABI を維持してください。STL 型、C++ クラス、例外が C ABI の境界を越えないようにしてください。
- 公式の BVE 構文のみを実装し、独自の路線構文拡張を追加しないでください。路線ファイルの読み書き時には、文字コード、BOM、改行コード、Include、コメント、文の順序を保持してください。旧バージョンの BVE や他の鉄道シミュレーションゲームのマップ対応を追加する必要がある場合は、まずメンテナーにご相談ください。
- マップ編集は、共有の静的型付けされたソース主導の編集パイプラインを経由して実装する必要があります。GUI 内で路線ソースを再解析したり、UI 機能側で文字列の書き換えロジックを重複して記述したりしないでください。
- 表示される UI テキストは、英語、中国語（簡体字、香港繁体字、台湾繁体字）、日本語の 5 言語すべてを同時に更新する必要があります。
- `third_party` にあるサードパーティーのソースコードは変更しないでください。新しい依存関係を追加するには、明確な理由、CMake への統合、ドキュメントの更新、およびライセンス表記の更新が必要です。
- 重複した I/O、毎フレームの再構築、意図しない O(n²) の演算を避けてください。パフォーマンス改善を主張する場合は、同一条件下での変更前後の比較データを提示する必要があります。

**プロジェクト内にすでに存在する機能を重複して再実装してはなりません。** 実装前に、ソースコード、データテーブル、メニュー、ドキュメント、および `TODO.md` を確認し、すでに実装が存在しないか確かめてください。並行して別の処理系を作るのではなく、既存の共有ヘルパー関数や処理パイプラインを拡張してください。既存の機能が不完全である場合は、その箇所を直接改善し、プルリクエスト内でその理由を説明してください。

変更は最小限の範囲にとどめ、その機能を担当するモジュール内に配置してください。`build/`、`build_release/`、クローンした `third_party` ディレクトリ、生成された設定ファイル、CSV やテストの出力、一時的な路線／マップ／モデルファイル、および各環境固有のローカルパスはコミットしないでください。

### 6. ビルドと検証

環境のセットアップについては[開発者ガイド](docs/dev_jp.md)を参照してください。基本的な手順は以下のとおりです：

```bat
.\build_dev.bat
ctest --test-dir build --output-on-failure
```

- コードの変更については、Debug ビルドが成功し、登録されている CTest 規約テストに合格する必要があります。診断規約テストには `tests/` 配下のローカルテストデータ（構文が正常／不正な最小構成のマップ）が必要です。
- 変更された動作に決定論的な規約がある場合は、対象を絞ったテストを追加または更新してください。
- 変更がパッケージング、実行時の DLL 配置、または最適化に依存する動作に影響を与える場合は、`.\build_release.bat` も実行してください。
- 影響を受ける GUI の動作を手動で確認してください。編集機能については、保存、再読み込みを行い、文字コードや改行コードを含めてファイル内容を比較してください。
- 実際に実行したコマンドとその実際の結果のみを、確認済みの失敗項目を含めて報告してください。

### 7. ドキュメント

- 変更した動作に対応するドキュメントを更新してください：ユーザーに見える動作や構文の対応状況は README および `docs/` 配下の各言語版に、操作方法はユーザーマニュアルに、アーキテクチャや検証方法は開発者ガイドに、開発状況は `TODO.md` / `docs/TODO_done.md` に記載します。
- 多言語版があるドキュメントを変更する場合は、各言語版の間で内容の一貫性を保ってください。特定の言語の記述が困難な場合は、プルリクエスト内でその旨を注記してください。
- ドキュメントは BOM なしの UTF-8 で保存し、各ファイル既存の改行コード形式を維持してください。

### 8. コミットメッセージ

- コミットメッセージは英語で記述し、`Change Type: What was done` というフォーマットを使用してください（例：`Fix: preserve CRLF when saving Station lists`、`Docs: update build instructions` など）。
- 各コミットの目的を単一に保ってください。可能な限り、レビューを依頼する前に `wip` などの無意味なコミットを整理してください。

### 9. 問題の報告

- 不具合を報告する際は、komapedit のバージョンまたはコミットハッシュ、ビルド種別（Release/Debug）、Windows のバージョン、再現手順、期待される動作と実際の動作、および関連するログやスクリーンショットを提示してください。
- 可能な限り、最小限の再現用路線サンプルを添付してください。共有する権利を持っている路線ファイル、モデル、テクスチャのみを共有してください。
- 機能の追加・改善要望を提出する際は、ユースケースを説明してください。BVE 構文が関係する場合は、関連する [BVE 公式仕様](https://bvets.net/jp/edit/formats/route/map.html)へのリンクを添付してください。

### 10. レビューとマージ

- メンテナーは、PR の目的、プロジェクト基準の遵守、正確性、検証の証拠、保守性を審査します。ビルドに成功したというだけでマージが保証されるわけではありません。
- メンテナーから修正を求められたり、プルリクエストの分割を依頼されたり、クローズされたりする場合があります。長期間にわたってレビューへのフィードバックがないプルリクエストは、クローズされることがあります。
- 相手を尊重し、建設的な姿勢でコミュニケーションを行ってください。

### 11. ライセンス

- komapedit は [Apache License, Version 2.0](LICENSE) の下で公開されています。別段の明示的な記載がない限り、提出された貢献内容には同一のライセンスが適用されます。
- 自身が貢献する権利を有している内容のみを提出してください。互換性のないライセンスを持つコードや、適切な帰属表示のないコードをコピーしないでください。
- 既存の著作権表示やライセンス表記を削除しないでください。サードパーティーのコードを追加する際は、必要に応じて [`NOTICE`](NOTICE) または [`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md) を更新してください。

### プルリクエストのチェックリスト

- [ ] ブランチは `dev` をベースとしており、プルリクエストのターゲットブランチも `dev` になっている。
- [ ] タイトルと説明が英語で書かれている。
- [ ] プルリクエストに明確な目的があり、AI の補助を受けた部分を含め、すべての変更内容を理解し説明できる。
- [ ] この変更が既存の機能と重複していないことを確認した。
- [ ] 変更内容が `AGENTS.md` および開発者ガイドに準拠している。
- [ ] Debug ビルドが成功し、関連するテストに合格し、検証方法が記載されている。
- [ ] 表示される UI テキストを変更した場合、サポートされている 5 つの UI 言語すべてを同時に更新した。
- [ ] 必要に応じてドキュメントを更新した。
- [ ] ビルド成果物、生成ファイル、ローカルパス、または無関係な変更が含まれていない。