# Development Status / 开发进度

This file is the active project progress and contains unfinished items only. Completed items are archived in [`docs/TODO_done.md`](docs/TODO_done.md). Update both documents whenever a feature is completed, added, removed, or rescheduled. User instructions belong in `README.md`; development rules and reusable AI workflows belong in `docs/dev.md`, `docs/ai-dev.md`, `AGENTS.md`, and `.agents/skills`; historical project lessons belong in `.agents/memories`.This document is only written in Chinese.

本文档是当前项目开发进度，仅保留未完成事项。已完成事项归档于 [`docs/TODO_done.md`](docs/TODO_done.md)。功能完成、新增、取消或调整计划时应同步更新这两个文档。用户说明位于 `README.md`；开发规范与可复用 AI 工作流位于 `docs/dev.md`、`docs/ai-dev.md`、`AGENTS.md` 和 `.agents/skills`；历史项目经验位于 `.agents/memories`。此文档仅使用简体中文编写。

## 待办事项列表

### 2D 平面图与图表显示

- [ ] 拆分 2D 画布内部的视图状态、marker cache、hit-test/context menu、背景图和绘制 primitive，拆分阶段不改变现有行为

### 3D画布

- [ ] 3D 场景画质设置：render scale、MSAA、纹理过滤和轮廓质量
- [ ] 为 3D 线路信息叠加层补充当前不支持的插值（interpolate）情况
- [ ] 支持通过 3D 操纵器编辑布景旋转和其他放置字段
- [ ] 支持打开线路时自动加载3D场景预览
- [ ] 3D场景中的FlangeNoise标牌增加index显示

### 用户界面与辅助功能

- [ ] 新建地图元素向导中增加分类
- [ ] 连续布景插入变化点功能：通过连续布景右键菜单中的选项，打开新建地图元素向导，向导中自动填充repeater信息，插入同名begin-only连续布景。
- [ ] 布景模型列表/音效文件列表中通过可通过右键菜单“使用此模型/使用此音效”选项，将对应的资源文件key直接填入新建地图元素向导
- [ ] 他轨道与他轨道变化点的新建功能
- [ ] 自轨道曲线变化点新建功能
- [ ] 自轨道坡度变化点新建功能
- [ ] 加入对Legacy.fog的支持
- [ ] include文件新建与导入、解除引用、删除源文件功能
- [ ] 资源列表文件新建与导入、解除引用、删除源文件功能
- [ ] 在车站列表、布景模型列表、音效文件列表中新增行
- [ ] 变量编辑功能
- [ ] 可引用变量、运算符、数学函数设置参数
- [ ] 变量新建功能
- [ ] Pretrain（先行列车）语句编辑与新建
- [ ] 他列车定义文件编辑与新建
- [ ] 他列车启用时间编辑与新建
- [ ] 他列车停止点编辑与新建
- [ ] 可将多个地图语句设为1组，通过 `kme.json` 保存地图元素预设组，应用后生成普通 BVE map 语句
- [ ] 自定义工作区：保存多种UI布局作为预设，根据不同使用场景切换UI布局
- [ ] 线路发行版导出：展开 Include、可选常量化距离/变量表达式、只复制实际使用资源、输出报告，并保护开发线路目录不被覆盖（这是项目中的功能，与仓库中的release构建没有直接关系）
