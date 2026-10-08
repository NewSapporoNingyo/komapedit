# Development Status / 开发进度

This file is the active project progress and contains unfinished items only. Completed items are archived in [`docs/TODO_done.md`](docs/TODO_done.md). Update both documents whenever a feature is completed, added, removed, or rescheduled. User instructions belong in `README.md`; development rules and reusable AI workflows belong in `docs/dev.md`, `docs/ai-dev.md`, `AGENTS.md`, and `.agents/skills`; historical project lessons belong in `.agents/memories`.This document is only written in Chinese.

本文档是当前项目开发进度，仅保留未完成事项。已完成事项归档于 [`docs/TODO_done.md`](docs/TODO_done.md)。功能完成、新增、取消或调整计划时应同步更新这两个文档。用户说明位于 `README.md`；开发规范与可复用 AI 工作流位于 `docs/dev.md`、`docs/ai-dev.md`、`AGENTS.md` 和 `.agents/skills`；历史项目经验位于 `.agents/memories`。此文档仅使用简体中文编写。

## 待办事项列表

- [ ] 3D模型预览可直接打开Structure List未列出的模型文件
- [ ] 支持通过3D操纵器编辑布景旋转
- [ ] 新建空白地图模板功能，包括1个场景文件、1个基本地图文件、各种资源列表文件
- [ ] 自定义工作区：保存多种UI布局作为预设，根据不同使用场景切换UI布局
- [ ] 他列车启用时间编辑与新建
- [ ] 他列车停止点编辑与新建
- [ ] 他列车定义文件编辑与新建
- [ ] 通过 `kme.json` 在标记文件类型和用途、语句组合等信息
- [ ] 通过在地图中插入被BVE忽略但本项目可读的”//--kme--“开头的注释，标记特定地图元素
- [ ] 资源列表注释功能：在资源列表内每个定义后可以添加`#--kme--comment:"content"`，鼠标停靠资源列表中对应键的单元格时，显示注释。
- [ ] 可将多个地图语句设为1组，通过 `kme.json` 保存地图元素预设组，应用后生成普通 BVE map 语句
- [ ] 可引用变量、运算符、数学函数设置参数
- [ ] 变量编辑功能
- [ ] 变量新建功能
- [ ] 可将多个不同distance的地图语句设为1组，以1个语句作为参照，记录其它语句的相对位置
- [ ] 根据坡度变化点前后坡度、工程规范等信息自动计算纵曲线长度
- [ ] 根据曲线半径、轨距、限速、工程规范等参数自动计算缓和曲线长度与超高
- [ ] 自动道岔计算：输入前后自轨道、他轨道相对位置信息，自动生成单开或Y字形道岔
- [ ] 线路发行版导出：展开 Include、可选常量化距离/变量表达式、只复制实际使用资源、输出报告，并保护开发线路目录不被覆盖（这是项目中的功能，与仓库中的release构建没有直接关系）
- [ ] 工程规范检查：提供日本/中国/欧洲地区的多种铁路工程规范标准预设，可检查道岔、最小曲线半径、曲线间隔、到发线与站台长度、股道间距等信息是否符合对应标准。
