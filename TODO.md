# Development Status / 开发进度

This file is the active project progress and contains unfinished items only. Completed items are archived in [`docs/TODO_done.md`](docs/TODO_done.md). Update both documents whenever a feature is completed, added, removed, or rescheduled. User instructions belong in `README.md`; development rules and reusable AI workflows belong in `docs/dev.md`, `docs/ai-dev.md`, `AGENTS.md`, and `.agents/skills`; historical project lessons belong in `.agents/memories`.This document is only written in Chinese.

本文档是当前项目开发进度，仅保留未完成事项。已完成事项归档于 [`docs/TODO_done.md`](docs/TODO_done.md)。功能完成、新增、取消或调整计划时应同步更新这两个文档。用户说明位于 `README.md`；开发规范与可复用 AI 工作流位于 `docs/dev.md`、`docs/ai-dev.md`、`AGENTS.md` 和 `.agents/skills`；历史项目经验位于 `.agents/memories`。此文档仅使用简体中文编写。

## 待办事项列表

### 3D画布

- [ ] 单独核查 Repeater 放置网格和模型循环的语义差异：官方 Map 文档按全局里程的 interval 整数倍描述放置与序列，当前 3D 实现按每段 Begin 起点累计 interval。
- [ ] 支持通过3D操纵器编辑布景旋转

### 用户界面与辅助功能

- [ ] 解决2个固定窗口大小的新建向导在高dpi情况下过小的问题
- [ ] 新建空白地图模板功能，包括1个场景文件、1个基本地图文件、各种资源列表文件
- [ ] 信号现示列表可新增/删除列
- [ ] 通过 `kme.json` 在资源文件列表中标记文件类型和用途
- [ ] 通过在地图中插入被BVE忽略但本项目可读的”//--kme--“开头的注释，标记特定地图元素
- [ ] 可将多个地图语句设为1组，通过 `kme.json` 保存地图元素预设组，应用后生成普通 BVE map 语句
- [ ] 可将多个不同distance的地图语句设为1组，以1个语句作为参照，记录其它语句的相对位置
- [ ] 优化地图语句移动和插入逻辑，减少手动选择插入位置的概率，并确保文件末尾里程可变
- [ ] 出现手动选择语句插入位置的情况时，控制台需要输出不可自动插入的明确原因
- [ ] 变量编辑功能
- [ ] 可引用变量、运算符、数学函数设置参数
- [ ] 变量新建功能
- [ ] Pretrain（先行列车）语句编辑与新建
- [ ] 他列车定义文件编辑与新建
- [ ] 他列车启用时间编辑与新建
- [ ] 他列车停止点编辑与新建
- [ ] 自定义工作区：保存多种UI布局作为预设，根据不同使用场景切换UI布局
- [ ] 线路发行版导出：展开 Include、可选常量化距离/变量表达式、只复制实际使用资源、输出报告，并保护开发线路目录不被覆盖（这是项目中的功能，与仓库中的release构建没有直接关系）
- [ ] Legacy.Fog 语句编辑与新建
- [ ] 3D 场景预览应用 Legacy.Fog 线性雾效果
- [ ] 根据曲线半径、轨距、限速等参数自动计算缓和曲线长度与超高
- [ ] 根据坡度变化点前后坡度自动计算纵曲线长度
