# Development Status / 开发进度

This file is the active project roadmap and contains unfinished items only. Completed items are archived in [`docs/TODO_done.md`](docs/TODO_done.md). Update both documents whenever a feature is completed, added, removed, or rescheduled. User instructions belong in `README.md`; development rules and reusable AI workflows belong in `docs/dev.md`, `docs/ai-dev.md`, `AGENTS.md`, and `.agents/skills`; historical project lessons belong in `.agents/memories`.

本文档是当前项目路线图，仅保留未完成事项。已完成事项归档于 [`docs/TODO_done.md`](docs/TODO_done.md)。功能完成、新增、取消或调整计划时应同步更新这两个文档。用户说明位于 `README.md`；开发规范与可复用 AI 工作流位于 `docs/dev.md`、`docs/ai-dev.md`、`AGENTS.md` 和 `.agents/skills`；历史项目经验位于 `.agents/memories`。

## English

### 2D Plan View and Charts

- [ ] Split 2D canvas internals into view state, marker cache, hit testing/context menus, background image handling, and drawing primitives without changing current behavior.

### Map Data Tables

- [ ] Add direct 2D manipulation for Structure/Signal placements and extend the property inspector to remaining unsupported Map Info rows.

### 3D Canvas

- [ ] 3D scene quality settings for render scale, MSAA, texture filtering, and outline quality.
- [ ] Extend the 3D route overlay with previous-station information and unsupported interpolation cases.
- [ ] Add 3D gizmo editing for Structure rotation and other placement fields.

### User Interface and Utilities

- [ ] Element preset groups stored as ordinary BVE map/list statements through `element_presets.json`.
- [ ] Route release export that expands includes, optionally constantizes distance/variable expressions, copies only used resources, writes a report, and protects development route directories from overwrite.

## 简体中文

### 2D 平面图与图表显示

- [ ] 拆分 2D 画布内部的视图状态、marker cache、hit-test/context menu、背景图和绘制 primitive，拆分阶段不改变现有行为

### 地图信息表示

- [ ] 为布景/信号机放置增加直接 2D 操纵，并将属性检查器扩展到其余尚不支持的地图信息行

### 3D画布

- [ ] 3D 场景画质设置：render scale、MSAA、纹理过滤和轮廓质量
- [ ] 为 3D 线路信息叠加层补充上一站信息和当前不支持的插值情况
- [ ] 支持通过 3D 操纵器编辑布景旋转和其他放置字段

### 用户界面与辅助功能

- [ ] 他轨道与他轨道变化点的新建功能
- [ ] 在车站列表、布景模型列表、音效文件列表中新增行。
- [ ] 可将多个地图语句设为1组，通过 `kme.json` 保存地图元素预设组，应用后生成普通 BVE map 语句
- [ ] 线路 release 导出：展开 Include、可选常量化距离/变量表达式、只复制实际使用资源、输出报告，并保护开发线路目录不被覆盖
