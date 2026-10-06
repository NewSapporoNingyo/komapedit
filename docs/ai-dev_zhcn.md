# 使用 AI 编程工具开发 komapedit

[人工开发指南](dev_zhcn.md) · [供 AI 工具阅读的仓库规范](../AGENTS.md) · [开发进度](../TODO.md)

本文面向使用 AI 编程工具开发 komapedit 的人员，介绍如何描述任务、选择工作流、跟进修改和验收结果。

开始前，建议熟悉受影响的程序操作、基础 C++、Windows 构建环境，以及代码差异和测试结果的阅读方法。这些知识有助于核对 AI 提出的方案，并作出产品与验收决策。

仓库为 AI 工具准备了 [`AGENTS.md`](../AGENTS.md) 和 [项目技能](../.agents/skills)，提示词可以直接引用相应技能。[项目经验](../.agents/memories/INDEX.md)可帮助理解历史决策；当前实现和开发进度分别以源码、测试及 [`TODO.md`](../TODO.md) 为准。项目专用技能与经验保存在 `.agents` 中，用户级指导用于跨项目内容。

## 把任务描述清楚

从具体操作和可观察的结果出发，写清以下内容：

- 已观察到的行为或要实现的功能；
- 复现步骤，以及相关输入、日志、截图或线路文件；
- 修改范围，以及需要保留的操作、兼容性和性能；
- 预期结果与验收标准；
- 构建与测试范围、可用于验证的线路，以及人工 GUI 检查的安排。

例如，修复卡顿时，提供触发操作、线路规模、构建类型和耗时，比笼统要求“提升性能”更便于定位和验收。涉及文件格式策略、兼容性迁移、公共 ABI 或新增依赖等重要选择时，可先安排只读调查，确认方案后再实施。

## 选择合适的工作流

仓库提供日常开发、问题修复、代码维护和文档编写四类工作流。客户端支持显式调用技能时，可以使用下面的提示词模板；其他客户端可用自然语言描述任务，并引用技能路径。场景技能会根据任务引入源码编辑、表格、2D/3D 预览、设置、多语言 UI 和验证等专项技能。

### 日常开发

[`komapedit-develop`](../.agents/skills/komapedit-develop/SKILL.md) 适合功能新增或行为调整。

```text
使用 $komapedit-develop。
需求：[功能、受影响工作流、约束与验收标准]
保留：[兼容性、性能、用户操作、文件或 API]
验证：[Debug 构建、相关 CTest/headless 检查、验证线路和人工 GUI 检查]
```

常规开发使用 Debug 构建。涉及发布打包、运行时依赖或优化相关问题时，再安排 Release 验证；具体命令见[人工开发指南](dev_zhcn.md)。

### 问题修复

[`komapedit-fix`](../.agents/skills/komapedit-fix/SKILL.md) 从复现问题和定位根因开始，再在负责该行为的模块中修复。根因尚不清楚时，可先要求只读诊断；验收时重点检查原始复现步骤和相关回归测试。

```text
使用 $komapedit-fix。
实际表现：[症状、日志、构建类型、线路/输入与复现步骤]
预期表现：[正确行为]
范围：[只诊断，或诊断并修复]
验证：[原始复现条件，以及相关 Debug/Release/headless 检查]
```

### 代码维护（slop-fix）

[`komapedit-slop-fix`](../.agents/skills/komapedit-slop-fix/SKILL.md) 用于清理有证据的代码质量问题，例如重复逻辑、无用状态、缓存错误和隐藏的崩溃风险。它先审计，再修复已确认的问题，并保持现有行为与 ABI。

```text
使用 $komapedit-slop-fix。
范围：[具体子系统、变更文件或整个项目]
先只读审计，列出问题证据、影响、修复方案和验证方法，再修复已确认的问题。
保留现有行为与 ABI。
验证：启用严格警告的 Debug、已注册 CTest、相关 headless 检查；涉及性能时提供同条件的前后数据。
```

审计也可以以“未发现需要修改的问题”结束。

### 文档编写

[`komapedit-write-docs`](../.agents/skills/komapedit-write-docs/SKILL.md) 适合文档修订和代码变更后的说明同步。指定读者、文件和语言范围，并以当前源码、测试及构建脚本为事实依据。

```text
使用 $komapedit-write-docs。
内容与读者：[需要说明的当前行为、工作流及目标读者]
文件与语言：[文档路径；需要同步的英语/简体中文版本]
修改范围：仅文档。
```

[`komapedit-doc-sync-validation`](../.agents/skills/komapedit-doc-sync-validation/SKILL.md) 用于检查修改范围、双语含义、编码、链接和表格。源码导航与验证命令还需核对当前文件、CMake 目标和命令行入口。

### 涉及 BVE 格式与源码编辑时

BVE 格式相关的开发和修复需配合 [`komapedit-bve-format-compliance`](../.agents/skills/komapedit-bve-format-compliance/SKILL.md)。它覆盖读取、解析、校验、强类型表示、编辑、新建、序列化和写回；具体格式包括[官方来源基线](../.agents/skills/komapedit-bve-format-compliance/references/official-bve-format-baseline.md)中的 Map、Structure List、Signal Aspects List、Sound List、他列车和 Scenario。

这类任务会先依据官方页面整理合规矩阵，区分现行语法、旧式别名、项目兼容形式与未支持形式，再进入实现。官方页面使用带日期的本地缓存，时间戳无效或超过 30 天时整套刷新。遇到官方规范、需求与实现之间影响行为的差异时，由用户确认处理方案。

```text
同时使用 $komapedit-bve-format-compliance 与 $komapedit-develop（或 $komapedit-fix）。
格式与元素：[受影响的文件、语句、列表行、节或 key]
操作：[读取 / 校验 / 编辑 / 新建 / 序列化 / 写回]
验收：[官方签名与语义、保存后重新加载、有效及无效输入、源码保真]
```

源码编辑使用 [`komapedit-source-backed-editing`](../.agents/skills/komapedit-source-backed-editing/SKILL.md)。涉及 Station、Structure、Signal、Sound 或 Sound3D 列表的导入、替换、新建、空列表首行及行插入时，再配合 [`komapedit-resource-list-source-editing`](../.agents/skills/komapedit-resource-list-source-editing/SKILL.md)，共同检查格式、编辑与保存流程。

## 与 Pi Agent 协作

Pi Agent是一款高效且简洁的编程智能体。需要 Pi 承担实现时，除了自行在pi中输入提示词之外，还可向 Pi 以外的编程智能体明确提出“调用 pi agent”，或调用 [`$collaborate-with-pi`](../.agents/skills/collaborate-with-pi/SKILL.md)。

统筹智能体先完成仓库调研和开发计划，将计划写入 `pi-prompts_local.txt`，再通过 `pi-agent-here(local).bat` 在可见的 Windows Terminal 中启动 Pi。Pi 负责主要实现、相关测试和文档同步；统筹智能体负责跟进进度，并在 Pi 完成后审查实际差异、独立重跑关键验证，必要时安排返修。

出现 Pi 会话异常时，协作会终止并报告已完成的修改和剩余工作，由用户决定下一步。启动、状态检查和返修细节见技能说明。

## 验收修改结果

先把实际代码差异与任务范围、验收标准逐项对照，再根据改动选择检查重点：

- **功能与界面：** 复测受影响的操作，检查英语、简体中文、台湾繁体中文、香港繁体中文和日语文本，以及表格、2D、3D 之间的选择、标记、导航和设置保存。
- **线路编辑：** 检查 Include、原始表达式、语句顺序、编码、BOM 和换行符的保留情况，并比较保存、重新加载后的内容。
- **模块接口：** 涉及 DLL 时，核对带版本的强类型 C ABI、结构尺寸、数据所有权、释放函数和异常处理。
- **性能：** 用相同线路、参数、构建类型和负载比较前后数据，同时检查重复读写、逐帧重建和缓存失效逻辑。
- **依赖与分发：** 核对 Windows 与工具链兼容性、许可证及声明文件、运行时 DLL 布局。

地图和资源列表的 Apply 更新内存工作副本与预览，Save 将修改写入文件，Revert 撤销待保存修改，Reload 从磁盘重新读取，有未保存修改时先请求确认。资源列表草稿需要先在表格中 Apply；Scenario 草稿则通过 Save 直接保存。编辑类任务可据此安排完整的操作验证。

验收报告应列出实际执行的命令、结果和已知失败，并分别记录构建、CTest、headless 与人工 GUI 检查。渲染、菜单、对话框和拖动效果需要结合实际界面确认。若任务偏离范围、反复卡在同一失败操作或报告与证据不符，可暂停任务，审查当前差异后重新明确目标。
