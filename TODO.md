# Development Status / 开发进度

This file is the single project roadmap and implementation-status checklist. Update it whenever a feature is completed, added, removed, or rescheduled. User instructions belong in `README.md`; development rules belong in `docs/dev.md`, `docs/ai-dev.md`, and `AGENTS.md`.

本文档是项目路线图与实现状态清单的唯一记录。功能完成、新增、取消或调整计划时应同步更新。用户说明位于 `README.md`；开发规范位于 `docs/dev.md`、`docs/ai-dev.md` 和 `AGENTS.md`。

## English

### Map Loading and Parsing

- [x] Load `BveTs Map 2.0+` map files.
- [x] Handle UTF-8, UTF-8 with BOM, UTF-16LE, UTF-16BE, CP932/Shift_JIS, and related text encodings.
- [x] Support `Include` references to other map files.
- [x] Support `$variable = expression;`, the predefined `distance` variable, and basic math functions.
- [x] Support `#` and `//` comments.
- [x] Load maps asynchronously and show logs, warnings, and errors in the console window.
- [x] Expose source anchors and stable edit metadata for editable map/list statements through the versioned typed map snapshot.
- [x] Apply supported updates/deletions to an in-memory working copy and save them to source map/include/list files, preserving include structure, distance semantics, original encodings, and line endings where possible.
- [x] Block writeback when an edited value cannot be represented in the source encoding; there is currently no Save-as-UTF-8 fallback.
- [x] Reject ambiguous maps with multiple same-kind unkeyed resource-list `Load` statements or multiple case-insensitively matching `Train[].Enable` declarations.
- [x] Insert supported map placement and event/effect statements through the New Map Element wizard; resource/list definition rows remain inline-table-only.

### Own-Track and Other-Track Geometry

- [x] Parse and calculate own-track curves.
- [x] Parse and calculate own-track gradients, including their horizontal projection in plan geometry.
- [x] Support legacy syntax.
- [x] Parse the supported legacy own-track statements `Legacy.Turn`, `Legacy.Curve`, and `Legacy.Pitch`.
- [x] Parse and calculate parts of other-track position data, lateral/vertical interpolation, gauge, center offset, and cant.
- [x] Parse other-track `Track.Position`, `Track.X/Y.Interpolate`, `Track.Gauge`, and `Track.Cant.*` statements.
- [x] Support control-point range and interval settings, with geometry regeneration.
- [x] Load and display speed-limit sections.
- [x] Edit or delete existing own-track curve change points, including paired `Curve.BeginTransition` statements where applicable; insertion and method conversion are not supported.
- [x] Edit or delete existing own-track gradient change points, including paired `Gradient.BeginTransition` statements where applicable; insertion and method conversion are not supported.
- [x] Edit or delete supported existing other-track change statements from their edit-mode 2D/3D markers. Track key, method, and argument count remain read-only; insertion, dragging, gizmos, and method conversion are not supported.

### 2D Plan View and Charts

- [x] Display the own-track plan view.
- [x] Display enabled other tracks, with configurable visible range and color.
- [x] Display station positions, names, and mileage.
- [x] Display speed-limit markers.
- [x] Display curve-radius sections and transition-curve sections.
- [x] Display the profile/elevation chart.
- [x] Display the curve-radius chart.
- [x] Support panning, mouse-wheel zooming, rotation, and double-click fit-to-view in the plan view.
- [x] Support fixed grid, movable grid, and no-grid modes.
- [x] Support measurement mode, showing mileage, elevation, gradient, curve radius, and speed limit.
- [x] Support jumping to stations and numeric map distances.
- [x] Import a background image and adjust its position, size, rotation, and brightness.
- [x] Align a background image using two station positions.
- [x] Structure and repeater placement markers on the plan view.
- [x] Display signal position markers on the plan view.
- [x] Display `Section.Begin`/`Section.BeginNew` markers with their signal-index parameter labels on the plan view.
- [x] Display beacon position markers on the plan view.
- [x] Display PreTrain pass-point markers on the plan view.
- [x] Display other-train paths and stop-point markers on the plan view.
- [x] Display track-irregularity and adhesion change-point markers on the plan view.
- [x] Display map sound, fixed sound source, rolling-noise, flange-noise, and joint-noise markers on the plan view.
- [x] Display background change-point markers on the plan view.
- [x] Display cab-illuminance change-point markers on the plan view.
- [x] Display fog-effect change-point markers on the plan view.
- [x] Display draw-distance change-point markers on the plan view.
- [x] Open Properties/Edit or delete paired curve/gradient change points from the plan, profile, curve-radius, and 3D scene markers.
- [ ] Split 2D canvas internals into view state, marker cache, hit testing/context menus, background image handling, and drawing primitives without changing current behavior.

### Map Data Tables

- [x] Load the station list specified by `Station.Load`.
- [x] Display `Station.Put` position rows separately from `Station.Load` definition rows.
- [x] Display the other-track list, with controls for visibility, range, and color.
- [x] Display other-train definitions and stop-point lists, including each group's unique read-only `Train.Enable` time, with plan-path visibility and stop-point location.
- [x] Display map Structure placement tables for `Structure.Put`, `Structure.Put0`, and `Structure.PutBetween`.
- [x] Load and display Structure model lists referenced by `Structure.Load` (`.txt` or `.csv`).
- [x] Display linked `Repeater.Begin`, `Repeater.Begin0`, and `Repeater.End` segments, with Begin/End/change boundaries merged for readability.
- [x] Display dynamic-column `Section.Begin`/`BeginNew` and `Section.SetSpeedLimit`/`Signal.SpeedLimit` tables with explicit `null` arguments and source files; edit or delete existing rows through the source-backed inspector, including add/remove of the variable-length parameters.
- [x] Display a read-only variable-assignment list grouped by case-insensitive name, preserving parse order, original expressions, and source files.
- [x] Show evaluated `Station.Load`, `Structure.Load`, `Signal.Load`, `Sound.Load`, and `Sound3D.Load` arguments with their raw expressions and resolved paths above the corresponding list tables.
- [x] Provide shared find and unused-entry search panels for Structure models, signal aspects, and sound lists.
- [x] Edit, clear, reorder, or delete existing Structure model-list keys and file paths through the source-backed inline table editor; selecting a file writes a relative path where possible.
- [x] Edit or delete existing `Station.Put` rows, including distance, `stationKey`, door side, and stop margins.
- [x] Edit, clear, reorder, or delete existing station-definition rows loaded through `Station.Load`; adding station rows is not supported.
- [x] Display `Signal Aspect List`, `Map Signal List`, and `Beacon List`.
- [x] Display `Speed Limit Point List`, `Track Irregularity List`, `Adhesion Change Point List`, rolling-noise, flange-noise, and joint-noise tables.
- [x] Display `Background Change Point List`, `Cab Illuminance Change Point List`, `Fog Change Point List`, and `Draw Distance Change Point List`.
- [x] Edit, clear, reorder, or delete existing `Signal.Load` aspect definitions and their optional glare rows through the source-backed inline table editor; adding rows or structure-key columns is not supported.
- [x] Edit or delete existing `Beacon.Put` rows through the source-backed property inspector.
- [x] Provide a source-backed `Properties/Edit` inspector for supported Structure/Signal/Station/Repeater placements and Section, speed-limit, irregularity, beacon, sound/noise, background, adhesion, cab-illuminance, fog, and draw-distance rows; expose it from applicable tables and 2D/3D markers, with live X/Y/Z gizmos for editable Structure, Signal, and Repeater Begin placements.
- [x] Open Properties/Edit for Structure and Signal placements from their 2D plan markers.
- [ ] Add direct 2D manipulation for Structure/Signal placements and extend the property inspector to remaining unsupported Map Info rows.

### 3D Canvas

- [x] 3D preview for Structure models.
- [x] Load model geometry, materials, and diffuse textures through `model_loader.dll`/Assimp.
- [x] Rotate and zoom the Structure model preview.
- [x] 3D scene preview canvas for track paths, Structure/Repeater instances, signals, map-element markers, background changes, and interpolated BVE fog effects.
- [x] Highlight the nearest whole-metre position on the own-track plane in the 3D scene mileage-selection mode, show a mouse-following mileage label, and open the New Map Element wizard from its context menu with distance prefilled.
- [x] Jump the 3D scene camera from station selections and numeric distance jumps, and show the current 3D position on the plan view.
- [x] Locate Structure, Repeater, signal, and supported map-marker table rows in the 3D scene preview, and locate picked scene objects or markers back in their tables.
- [ ] 3D scene quality settings for render scale, MSAA, texture filtering, and outline quality.
- [x] Display the current curve radius/cant, gradient, active speed limit, section-selected signal speeds, and distance to the next station in the 3D scene route overlay.
- [ ] Extend the 3D route overlay with previous-station information and unsupported interpolation cases.
- [x] Edit `Structure.Put`, `Signal.Put`, and `Repeater.Begin` positions along X/Y/Z with live 3D gizmos, including explicit `Put0`/`Begin0` conversion and configurable gizmo size.
- [ ] Add 3D gizmo editing for Structure rotation and other placement fields.
- [x] Edit linked Repeater segments in the inspector, including Begin navigation, End/change boundaries, and linked deletion choices.

### Environmental Effects

- [x] Display `Sound File List`, `3D Sound File List`, `Map Sound List`, and `Map 3D Sound List`.
- [x] Edit, clear, reorder, or delete existing `Sound.Load` and `Sound3D.Load` file-list rows through source-backed inline tables; selecting a file writes a relative path where possible.
- [x] Edit or delete existing `Sound.Play`/`Sound3D.Put` placements and rolling/flange/joint-noise events; station definition announcement sound keys are editable, but Sound/Sound3D file-list row insertion and audio playback remain unsupported.
- [x] Edit or delete existing cab-illuminance setting positions.
- [x] Edit or delete existing fog effects.

### User Interface and Utilities

- [x] Dear ImGui docking-based multi-window layout.
- [x] UI language switching between Simplified Chinese, English, and Japanese.
- [x] Settings for font size, UI component size, station marker size, 2D line widths, theme color, 3D scene draw distance/fog/map-draw-distance, camera speed, gizmo size, and scene-instance performance warnings.
- [x] Recent-map history.
- [x] Save background-image parameters with recent-map entries in `settings/history.ini`.
- [x] Save settings to `settings/settings.ini` under the executable directory.
- [x] Include-file structure diagram and read-only source text preview using the active in-memory working copy.
- [x] Edit mode with separate Apply-to-preview, Save-to-disk, global Revert of all pending changes, and Reload-from-disk behavior plus unsaved-change prompts.
- [x] Export own-track and other-track geometry to CSV.
- [ ] Element preset groups stored as ordinary BVE map/list statements through `element_presets.json`.
- [ ] Route release export that expands includes, optionally constantizes distance/variable expressions, copies only used resources, writes a report, and protects development route directories from overwrite.

## 简体中文

### 地图读取与解析

- [x] 读取 `BveTs Map 2.0+` 地图文件
- [x] 支持 UTF-8、UTF-8 BOM、UTF-16LE、UTF-16BE、CP932/Shift_JIS 等文本编码处理
- [x] 支持 `Include` 引用其他地图文件
- [x] 支持 `$变量 = 表达式;`、`distance` 预定义变量和基础数学函数
- [x] 支持 `#`、`//` 注释
- [x] 支持异步加载地图，并在控制台窗口显示加载日志、警告和错误
- [x] 通过带版本的 typed map snapshot 为可编辑 map/list 语句提供源锚点和稳定编辑 metadata
- [x] 将已支持的修改/删除先应用到内存工作副本，再保存到源 map/include/list 文件，并尽量保留 include 结构、距离语义、原始编码和换行
- [x] 当修改后的值无法用源文件原编码表示时阻止回写；当前没有另存为 UTF-8 的替代路径
- [x] 拒绝含有多个同种无 key 资源列表 `Load` 语句，或多个名称仅大小写不同的 `Train[].Enable` 声明的歧义地图
- [x] 通过“新建地图元素”向导新增受支持的地图放置和事件/效果语句；资源/定义列表仍只能通过行内表格编辑

### 自轨道与他轨道几何

- [x] 解析并计算自轨道曲线
- [x] 解析并计算自轨道坡度，并在平面几何中考虑纵坡产生的水平投影缩短
- [x] 支持旧式语法
- [x] 解析受支持的旧式自轨道语句 `Legacy.Turn`、`Legacy.Curve` 和 `Legacy.Pitch`
- [x] 解析并计算他轨道位置、横向/纵向插值、轨距、中心、超高等部分信息
- [x] 解析他轨道 `Track.Position`、`Track.X/Y.Interpolate`、`Track.Gauge` 和 `Track.Cant.*` 语句
- [x] 支持控制点范围和间隔设置，并可重新生成几何
- [x] 支持限速区间读取与显示
- [x] 编辑或删除已有自轨道曲线变化点，并在适用时联动成对的 `Curve.BeginTransition`；暂不支持新建和方法转换
- [x] 编辑或删除已有自轨道坡度变化点，并在适用时联动成对的 `Gradient.BeginTransition`；暂不支持新建和方法转换
- [x] 从编辑模式下的 2D/3D 标记编辑或删除受支持的既有他轨道变化语句；track key、方法和参数个数只读，暂不支持新建、拖动、gizmo 或方法转换

### 2D 平面图与图表显示

- [x] 显示自轨道平面图
- [x] 显示已启用的他轨道，并可设置显示范围和颜色
- [x] 显示站点位置、站名、站点里程
- [x] 显示限速标记
- [x] 显示曲线半径区间和缓和曲线区间
- [x] 显示纵断面/标高图
- [x] 显示曲线半径图
- [x] 支持平面图拖动、滚轮缩放、旋转、双击自适应范围
- [x] 支持固定网格、可动网格和关闭网格
- [x] 支持测量模式，显示里程、标高、坡度、曲线半径和限速
- [x] 支持车站跳转和数字里程跳转
- [x] 支持导入背景图，并调整位置、尺寸、旋转角和亮度
- [x] 支持使用两个车站位置对齐背景图
- [x] 平面图上的布景与连续布景位置标记
- [x] 平面图上显示信号位置标记
- [x] 平面图上显示 `Section.Begin`/`Section.BeginNew` 标记及其信号索引参数标签
- [x] 平面图上显示应答器位置标记
- [x] 平面图上显示先行列车通过点标记
- [x] 平面图上显示他列车路径和停止位置标记
- [x] 平面图上显示轨道变位和粘着特性变化点标记
- [x] 平面图上显示音效播放、固定音源、走行音、轮缘摩擦音效和道岔音效标记
- [x] 平面图上显示背景变化点标记
- [x] 平面图上显示驾驶台亮度变化点标记
- [x] 平面图上显示雾效果变化点标记
- [x] 平面图上显示绘制距离变化点标记
- [x] 从平面图、纵断面图、曲线半径图和 3D 场景标记打开曲线/坡度变化点的“属性/编辑”，或删除成对语句
- [ ] 拆分 2D 画布内部的视图状态、marker cache、hit-test/context menu、背景图和绘制 primitive，拆分阶段不改变现有行为

### 地图信息表示

- [x] 读取 `Station.Load` 指定的车站列表 CSV
- [x] 分别显示 `Station.Put` 位置行和 `Station.Load` 定义行
- [x] 显示他轨道列表，可切换显示、设置范围和颜色
- [x] 显示他列车定义和停止位置列表，包括各分组唯一的只读 `Train.Enable` 时间，并可切换路径显示、定位停止位置
- [x] 显示 `Structure.Put`、`Structure.Put0`、`Structure.PutBetween` 的地图布景放置表
- [x] 读取并显示 `Structure.Load` 指定的布景模型列表（`.txt` 或 `.csv`）
- [x] 显示 `Repeater.Begin`、`Repeater.Begin0`、`Repeater.End` 的关联连续布景段，并合并 Begin/End/变化边界
- [x] 以动态列分别显示 `Section.Begin`/`BeginNew` 和 `Section.SetSpeedLimit`/`Signal.SpeedLimit`，包括显式 `null` 参数和源文件；编辑模式下可通过基于源文件的属性检查器编辑或删除既有行，且参数个数可增加或删除
- [x] 以不区分大小写的名称分组显示只读变量赋值列表，并保留解析顺序、原表达式和源文件
- [x] 在对应列表顶部显示 `Station.Load`、`Structure.Load`、`Signal.Load`、`Sound.Load` 和 `Sound3D.Load` 求值后的参数、原表达式和解析路径
- [x] 为布景模型、信号现示和音效列表提供共享查找和未使用条目搜索面板
- [x] 通过源文件关联行内表格编辑器编辑、清空、调整顺序或删除已有的布景模型列表 key 和文件路径；选择文件时会尽可能写入相对路径
- [x] 编辑或删除已有 `Station.Put` 行，包括 distance、`stationKey`、车门侧和停车余量
- [x] 编辑、清空、调整顺序或删除由 `Station.Load` 载入的已有车站定义行；暂不支持新增车站行
- [x] 显示 `信号现示列表`、`地图信号列表` 和 `应答器列表`
- [x] 显示 `限速点列表`、`轨道变位列表`、`粘着特性变化点列表`、走行音、轮缘摩擦音效和道岔音效相关表格
- [x] 显示 `背景变化点列表`、`驾驶台亮度变化点列表`、`雾效果变化点列表` 和 `绘制距离变化点列表`
- [x] 通过源码回写的行内表格编辑器编辑、清空、调整顺序或删除已有的 `Signal.Load` 信号现示定义及可选 glare 行；暂不支持新增行或现示结构 key 列
- [x] 通过基于源锚点的属性检查器编辑或删除已有 `Beacon.Put` 行
- [x] 为已支持的布景/信号机/车站/Repeater 放置，以及限速点、轨道变位、应答器、音效/噪声、背景、粘着、驾驶台亮度、雾和绘制距离行提供“属性/编辑”检查器；可从适用的表格和 2D/3D 标记进入，并为可编辑的布景、信号机和 Repeater Begin 放置提供实时 X/Y/Z 操纵器
- [x] 从 2D 平面图的布景/信号机放置标记打开“属性/编辑”
- [ ] 为布景/信号机放置增加直接 2D 操纵，并将属性检查器扩展到其余尚不支持的地图信息行

### 3D画布

- [x] 布景模型 3D 预览
- [x] 通过 `model_loader.dll`/Assimp 读取模型和贴图
- [x] 支持旋转和缩放布景模型预览
- [x] 3D 画布场景预览，可显示轨道路径、布景/连续布景实例、信号、地图元素标记、背景变化和插值后的 BVE 雾效果
- [x] 在 3D 场景里程选择模式中高亮自轨道平面上最近的整米位置，显示跟随鼠标的里程标签，并可通过右键菜单打开自动填写 distance 的“新建地图元素向导”
- [x] 可通过车站跳转和数字里程跳转移动 3D 场景相机，并在平面图上显示当前 3D 位置
- [x] 可从布景、连续布景、信号和支持的地图元素标记表格行定位到 3D 场景，也可从场景对象或标记定位回对应表格
- [ ] 3D 场景画质设置：render scale、MSAA、纹理过滤和轮廓质量
- [x] 在 3D 场景线路信息叠加层显示当前曲线半径/超高、坡度、生效限速、闭塞选择出的信号限速和距下一站距离
- [ ] 为 3D 线路信息叠加层补充上一站信息和当前不支持的插值情况
- [x] 通过实时 3D 操纵器编辑 `Structure.Put`、`Signal.Put` 和 `Repeater.Begin` 的 X/Y/Z 位置，并支持显式 `Put0`/`Begin0` 转换和操纵器尺寸设置
- [ ] 支持通过 3D 操纵器编辑布景旋转和其他放置字段
- [x] 在属性检查器中编辑关联的连续布景段，支持 Begin 导航、End/变化边界和关联删除选项

### 环境效果

- [x] 显示 `音效文件列表`、`3D音效文件列表`、`地图音效列表` 和 `地图3D音效列表`
- [x] 通过源文件关联行内表格编辑器编辑、清空、调整顺序或删除已有的 `Sound.Load` 和 `Sound3D.Load` 文件列表行；选择文件时会尽可能写入相对路径
- [x] 编辑或删除已有 `Sound.Play`/`Sound3D.Put` 放置和走行音/轮缘摩擦音/道岔音事件；车站定义中的报站音 key 可编辑，但仍不支持新增 Sound/Sound3D 文件列表行，程序也不播放音频
- [x] 编辑或删除已有驾驶台亮度设定位置
- [x] 编辑或删除已有雾效果

### 用户界面与辅助功能

- [x] Dear ImGui Docking 多窗口布局
- [x] 简体中文、英文、日文界面语言切换
- [x] 字体大小、组件大小、车站标记大小、2D 线宽、主题色、3D 场景绘制距离/雾效果/地图绘制距离、相机速度、操纵器尺寸和场景实例性能警告设置
- [x] 最近打开地图历史记录
- [x] 背景图参数随最近地图保存到 `settings/history.ini`
- [x] 设置保存到程序目录下的 `settings/settings.ini`
- [x] Include 文件结构图，以及读取当前内存工作副本的只读源码文本预览
- [x] 编辑模式中分离“应用到预览”“保存到磁盘”“撤销全部待保存改动”和“从磁盘重新加载”，并在存在未保存更改时确认
- [x] 将自轨道和他轨道几何导出为 CSV
- [ ] 通过 `element_presets.json` 保存元素预设组，应用后生成普通 BVE map/list 语句
- [ ] 线路 release 导出：展开 Include、可选常量化距离/变量表达式、只复制实际使用资源、输出报告，并保护开发线路目录不被覆盖

