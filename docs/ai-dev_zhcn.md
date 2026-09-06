# 使用 AI 编程工具开发 komapedit

[English version](ai-dev.md) · [人工开发指南](dev_zhcn.md) · [供 AI 工具阅读的仓库规范](../AGENTS.md) · [开发进度](../TODO.md)

本文档面向使用 AI 编程工具开发 komapedit 的人员，说明如何定义任务、选择仓库工作流、监督修改，以及判断结果是否可以安全接受。

AI 工具应自行阅读 [`AGENTS.md`](../AGENTS.md)。可重复执行的工作流位于 [`.agents/skills`](../.agents/skills)，长期历史经验位于 [`.agents/memories`](../.agents/memories/INDEX.md)。不要把这些文档全部复制进提示词。操作者仍需对范围、产品决策、验证和最终验收负责。

## 了解 AI 工具的局限性

不要将AI智能体当做“只需一句指令就可以完成任何工作”的神仙。AI 编程工具可能生成看似合理但实际错误的代码，在长任务中遗漏约束，过度适配某一条示例线路，或声称完成了实际并未执行的验证。使用 AI 并不意味着可以不了解基础 C/C++、Windows 构建环境、代码差异、测试与受影响的程序行为。

避免“把代码变得更好”“修复所有 bug”“提升性能”“重写这个模块”等模糊要求。有效任务应说明：

- 已观察到的行为或要实现的功能；
- 受影响的工作流，以及必须保持不变的内容；
- 相关输入、日志、截图或线路文件；
- 预期结果与验收标准；
- 允许的构建/测试范围，以及是否稍后进行人工 GUI 测试；
- 文件范围、兼容性、依赖或 Release 限制。

若当前代码无法决定文件格式策略、破坏性迁移、公共 ABI 变化或新增依赖等重要选择，应先要求只读调查，再由人作出决定后实施。

## BVE 格式合规是强制层

凡新增或修改 BVE 地图元素的读取、解析、校验、强类型表示、编辑、新建、序列化或写回逻辑，都必须使用 [`komapedit-bve-format-compliance`](../.agents/skills/komapedit-bve-format-compliance/SKILL.md)。其范围包括[官方来源基线](../.agents/skills/komapedit-bve-format-compliance/references/official-bve-format-baseline.md)所覆盖的 Map、Structure List、Signal Aspects List、Sound List、他列车和 Scenario 格式。

该合规技能必须与 `komapedit-develop`、`komapedit-fix`、`komapedit-source-backed-editing` 或其他匹配的子系统技能同时使用。它要求智能体先检查带日期的本地官方页面缓存，仅在时间戳无效或超过 30 天时刷新整套缓存，然后阅读受影响的已缓存页面，分别标明当前官方语法、官方旧式别名、项目兼容形式与未支持形式，并在实现前建立合规矩阵。若官方页面、需求与当前实现之间存在会改变行为的差异，智能体必须停止并请人决定。

```text
同时使用 $komapedit-bve-format-compliance 与 $komapedit-develop（或 $komapedit-fix）。
官方格式/元素：[受影响的文件、语句、列表行、节或 key]
操作：[读取 / 校验 / 编辑 / 新建 / 序列化 / 写回]
验收：[官方签名与语义、往返验证、反例、源码保真]
```

## 四种常见项目工作流

仓库提供四个场景技能及多个专项技能。客户端支持显式调用技能时，直接引用技能名称；否则清楚描述任务场景，智能体应根据 `AGENTS.md` 中的项目技能索引进行路由。

### 日常开发

范围明确的功能新增或行为修改使用 [`komapedit-develop`](../.agents/skills/komapedit-develop/SKILL.md)。

```text
使用 $komapedit-develop。
需求：[功能、受影响工作流、约束与验收标准]
验证：[仅 Debug 构建 / 相关 CTest / headless 模式与已授权线路 / 稍后人工 GUI 检查]
必须保持：[兼容性、性能、用户操作、文件或 API]
```

该技能会按需继续路由到源码编辑、表格、3D 预览、设置、三语 UI 与验证等专项技能。

### 问题修复

修复问题时使用 [`komapedit-fix`](../.agents/skills/komapedit-fix/SKILL.md)。

```text
使用 $komapedit-fix。
实际表现：[症状、日志、构建类型、线路/输入与复现步骤]
预期表现：[正确行为]
范围：[只诊断，或诊断并实现最小且证据充分的修复]
验证：[原始复现条件，以及相关 Debug/Release/headless 检查]
```

根因未知时不要授权大规模重写。修复结果应说明根因所在的负责层，并证明原始复现条件不再失败。

### slop-fix

即使要求严格遵循了项目规范并使用精确指令的提示词，AI编程工具也有可能在代码中留下瑕疵，这时便需要进行1次或多次slop-fix修复，使项目可以持续开发。开发可持续性检查使用 [`komapedit-slop-fix`](../.agents/skills/komapedit-slop-fix/SKILL.md)。

```text
使用 $komapedit-slop-fix。
范围：[项目中全部代码或具体子系统、变更文件]
先只读审计：重复逻辑、无用状态、不安全代码、重复工作、缓存/失效错误、隐藏崩溃或卡死风险、混乱逻辑。
只修复有证据的问题；保持行为与 ABI 不变。
验证：严格 Debug、已注册 CTest、相关 headless 检查，以及适用时受控的前后性能数据。
```

“没有代码修改”可以是有效结果。不要编造缺陷、压缩代码行、删除有用注释、降低性能门槛，或为了报告进展而改变用户行为。

### 文档编写

文档专用修改或代码变更后的文档同步使用 [`komapedit-write-docs`](../.agents/skills/komapedit-write-docs/SKILL.md)。

```text
使用 $komapedit-write-docs。
需要记录的事实：[当前已实现行为或工作流]
文档范围：[README、TODO、开发指南、AI 指南、AGENTS、skills 或 memories]
语言范围：[适用时同时更新英语与简体中文]
除非只需报告实现不一致，否则保持为纯文档任务。
```

文档内容必须来自当前源码/测试。最后使用 [`komapedit-doc-sync-validation`](../.agents/skills/komapedit-doc-sync-validation/SKILL.md) 检查范围、双语一致性、编码、链接与表格。

## 显式调用 Pi Agent 协作

[`collaborate-with-pi`](../.agents/skills/collaborate-with-pi/SKILL.md) 是只能显式触发的工作流。仅当当前用户明确要求“调用pi agent”（大小写和空格不敏感），或为此显式调用 `$collaborate-with-pi` 时才可使用。不得因为任务困难、仓库中存在 Pi 相关文件或只是讨论该技能而自动触发。当前请求没有这种明确授权时，当前智能体必须自行完成工作，不得启动 Pi。

此技能只能由 Pi Agent 以外的编程智能体使用，禁止在 Pi 中再调用另一个 Pi。上级智能体负责调研当前工作树、确定验收标准、把完整任务说明写入 `pi-prompts_local.txt`、在用户可见的 Windows Terminal 中启动 `pi-agent-here(local).bat`，并继续负责范围、产品决策、独立审查和返修轮次。Pi 负责主要实现、针对性测试、归属正确的文档同步和证据报告。Pi 工作期间，上级智能体只需约每 5 分钟检查一次状态，不得并行修改重叠文件或启动并发构建。

Pi 退出后，上级智能体必须审查实际差异，并独立重跑最小且有决定性的验证。若确认存在缺陷，应写入范围明确的返修提示词并再次启动可见的 Pi 轮次；不得把 Pi 的报告直接当成证明，也不得转移最终责任。

## 审查结果

即使差异很小，也应检查适用的项目风险：

- **范围：** 没有无关重写、生成文件或隐藏行为变化。
- **公共 ABI：** 版本/结构尺寸精确、所有权明确、释放函数配对，STL 类型与异常不跨 C 边界。
- **线路源码保真：** 官方 BVE 语法、Include 上下文、原始表达式、解析顺序、编码/BOM/行尾，以及 Apply/Revert/Save/Reload 行为。
- **UI：** 英语/简体中文/日语文本，表格/平面/场景身份与导航，标记命中优先级，设置持久化及尚未执行的人工视觉检查。
- **性能：** 没有新增逐帧 I/O/重建、重复解码/哈希、不完整缓存 key 或无依据的性能结论。
- **依赖与分发：** Windows/工具链兼容性、许可与 notices、ABI 影响、二进制/运行时布局及迁移成本。
- **证据：** 精确命令、相关输出、已知失败，并诚实区分自动验证与未执行的人工测试。

运行时线路数据与编辑命令必须继续使用带版本的强类型 ABI。替代解析器、文本序列化线路传输、回退数据模型或磁盘快照缓存需要人的明确架构决定。

## 停止并重新评估

若智能体明显偏离范围、重复执行同一失败操作、在没有证据时开始大规模重写、修改无关文件、添加未批准的框架/依赖、大量删除代码且没有可恢复方案，或声称执行了实际未运行的测试/视觉检查，应立即停止。先审查差异并重新取得控制，再决定是否继续。
