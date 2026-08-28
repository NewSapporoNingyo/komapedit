# Completed Development Items / 已完成开发事项

This file archives completed items moved from [`TODO.md`](../TODO.md). Keep new and rescheduled work in `TODO.md`, and move an item here only after it is completed.

本文档归档从 [`TODO.md`](../TODO.md) 移出的已完成事项。新增或重新安排的工作应保留在 `TODO.md` 中，事项完成后再移至此处。

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
- [x] Insert `Repeater.Begin`/`Begin0`, `Repeater.End`, or an atomic Begin+End pair through the New Map Element wizard; preserve half-open same-name interval protection, confirmed Begin-only change points, and isolated End statements while blocking extra End statements inside explicitly closed intervals.
- [x] Add an Apply and Edit button to the New Map Element wizard that applies, closes the wizard, and opens `Properties/Edit` on the primary created statement; the button is disabled in wizards opened from a Repeater `Properties/Edit` window.
- [x] Open a map directly from a BVE Scenario file: the open dialog accepts `BveTs Scenario 2.00` files, resolves the official `Route` entry (single path or weighted candidates) relative to the scenario directory with declared-encoding decoding, loads the referenced map through the normal pipeline, and shows a fixed-width selection dialog when multiple candidates exist.
- [x] Add a read-only `Map Info List -> Other -> Scenario File` tab for every Scenario document, showing all eight official fields and source-relative Route/Vehicle paths with weights; direct map opens disable it, while a missing/invalid Route target or a cancelled candidate choice retains the standalone Scenario preview.

### Own-Track and Other-Track Geometry

- [x] Parse and calculate own-track curves.
- [x] Parse and calculate own-track gradients, including their horizontal projection in plan geometry.
- [x] Support legacy syntax.
- [x] Parse the supported legacy own-track statements `Legacy.Turn`, `Legacy.Curve`, and `Legacy.Pitch`.
- [x] Parse and calculate parts of other-track position data, lateral/vertical interpolation, gauge, center offset, and cant.
- [x] Parse other-track `Track.Position`, `Track.X/Y.Interpolate`, `Track.Gauge`, and `Track.Cant.*` statements.
- [x] Support control-point range and interval settings, with geometry regeneration.
- [x] Load and display speed-limit sections.
- [x] Edit or delete existing own-track curve change points, including paired `Curve.BeginTransition` statements where applicable; the New Map Element wizard inserts current `Curve.Begin(radius)`, paired `Curve.BeginTransition()` + `Curve.Begin(radius, cant)`, `Curve.Change(radius)`, and `Curve.End()` forms, while legacy aliases and `Interpolate` remain creation-disabled.
- [x] Edit or delete existing own-track gradient change points, including paired `Gradient.BeginTransition` statements where applicable; the New Map Element wizard inserts `Gradient.Begin(gradient)` and `Gradient.End()`, optionally atomically preceded by `Gradient.BeginTransition()`, while `Gradient.BeginConst` and `Interpolate` remain creation-disabled.
- [x] Allow the New Map Element wizard to set separate transition-start and consuming-statement distances for atomic own-track Curve/Gradient transition pairs while preserving BVE source order and full-reparse linkage validation.
- [x] Edit or delete supported existing other-track change statements from their edit-mode 2D/3D markers. Track key, method, and argument count remain read-only; insertion, dragging, gizmos, and method conversion are not supported.
- [x] Rename an other-track `trackKey` from the Other Tracks table across every same-key `Track[...]` statement in the root map and Includes, with whole-map duplicate protection and no cascading changes to dependent map elements; the change-point Inspector field remains read-only.

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
- [x] Validate `Station.Put` stop tolerances: loading warns with a source location when `margin1` is not negative or `margin2` is not positive; source-backed edits and creation block zero and wrong-sign values.
- [x] Edit, clear, reorder, or delete station-definition rows loaded through `Station.Load`.
- [x] Display `Signal Aspect List`, `Map Signal List`, and `Beacon List`.
- [x] Display `Speed Limit Point List`, `Track Irregularity List`, `Adhesion Change Point List`, rolling-noise, flange-noise, and joint-noise tables.
- [x] Display `Background Change Point List`, `Cab Illuminance Change Point List`, `Fog Change Point List`, and `Draw Distance Change Point List`.
- [x] Edit, clear, reorder, or delete `Signal.Load` aspect definitions and their optional glare rows through the source-backed inline table editor; adding structure-key columns is not supported.
- [x] Edit or delete existing `Beacon.Put` rows through the source-backed property inspector.
- [x] Provide a source-backed `Properties/Edit` inspector for supported Structure/Signal/Station/Repeater placements and Section, speed-limit, irregularity, beacon, sound/noise, background, adhesion, cab-illuminance, fog, and draw-distance rows; expose it from applicable tables and 2D/3D markers, with live X/Y/Z gizmos for editable Structure, Signal, and Repeater Begin placements.
- [x] Open Properties/Edit for Structure and Signal placements from their 2D plan markers.

### 3D Canvas

- [x] 3D preview for Structure models.
- [x] Load model geometry, materials, and diffuse textures through `model_loader.dll`/Assimp.
- [x] Rotate and zoom the Structure model preview.
- [x] 3D scene preview canvas for track paths, Structure/Repeater instances, signals, map-element markers, background changes, and interpolated BVE fog effects.
- [x] Automatically load the 3D scene preview when opening or reloading a map when enabled in 3D Canvas Settings.
- [x] Display each FlangeNoise scene marker's `index` value in the 3D scene preview.
- [x] Highlight the nearest whole-metre position on the own-track plane in the 3D scene mileage-selection mode, show a mouse-following mileage label, and open the New Map Element wizard from its context menu with distance prefilled.
- [x] Jump the 3D scene camera from station selections and numeric distance jumps, and show the current 3D position on the plan view.
- [x] Locate Structure, Repeater, signal, and supported map-marker table rows in the 3D scene preview, and locate picked scene objects or markers back in their tables.
- [x] In non-edit mode, keep 3D curve/gradient change-marker context menus at the clicked marker and show only disabled Properties/Edit and Delete actions instead of incorrectly reporting an unpaired BeginTransition.
- [x] Display the current curve radius/cant, gradient, active speed limit, section-selected signal speeds, and distance to the next station in the 3D scene route overlay.
- [x] Edit `Structure.Put`, `Signal.Put`, and `Repeater.Begin` positions along X/Y/Z with live 3D gizmos and configurable gizmo size; Inspector buttons convert `Put`/`Put0` and `Begin`/`Begin0` in either direction, while Put0/Begin0 expose a Z-only whole-metre placement/begin-distance gizmo.
- [x] Place each `Sound3D.Put` label tip at its fixed X/Y source relative to the own track, and edit X/Y in 0.001 m steps or `distance` in whole metres through the live X/Y/Z gizmo without changing `Put(x, y)` syntax or adding audio playback.
- [x] Preview all `Structure.PutBetween` Inspector fields with exact live vertex deformation and edit `distance` through a Z-only, whole-metre 3D gizmo.
- [x] Edit an explicit Repeater EndDistance through a Z-only whole-metre gizmo on its track centerline, with live instance-count updates and own-track-distance scaling on other tracks.
- [x] Edit linked Repeater segments in the inspector, including Begin navigation, End/change boundaries, and linked deletion choices.
- [x] Rename `repeaterKey` across every Begin/Begin0/End in one linked chain, using half-open interval overlap validation to allow touching or disjoint same-name Repeaters and reject overlapping ones.

### Environmental Effects

- [x] Display `Sound File List`, `3D Sound File List`, `Map Sound List`, and `Map 3D Sound List`.
- [x] Edit, clear, reorder, or delete existing `Sound.Load` and `Sound3D.Load` file-list rows through source-backed inline tables; selecting a file writes a relative path where possible.
- [x] Edit or delete existing `Sound.Play`/`Sound3D.Put` placements and rolling/flange/joint-noise events; station definition announcement sound keys are editable, but audio playback remains unsupported.
- [x] Edit or delete existing cab-illuminance setting positions.
- [x] Edit or delete existing fog effects.
- [x] Parse the read-only legacy linear-fog statement `Legacy.Fog(start, end, red, green, blue)` into typed snapshot rows and show them in a dedicated table list plus plan/scene markers with their source values; editing, creation, and the 3D fog effect are not implemented.
- [x] Add source-backed edit and deferred deletion for `Light.Ambient`, `Light.Diffuse`, and `Light.Direction`, and add fixed-distance-0 Effects wizard templates in the trilingual Lighting Effects UI. Each kind remains unique across the root map and all Includes; RGB bounds, Direction distance, full reparse/semantic proof, expression preservation, and memory Apply/Save/Revert behavior remain enforced. Markers and 3D lighting simulation are not implemented.

### User Interface and Utilities

- [x] Add `File -> New...` and a trilingual New File Wizard that creates header-only `BveTs Map 2.02` maps or Structure, Signal, Sound, Sound3D, and Station list files without overwriting existing files. A selected loaded map receives a preview-only typed `include`/`*.Load` reference that normal Save commits; Revert keeps the created file. Presets remain empty, and the wizard does not create Scenario files or list rows.
- [x] Add existing resource-list import/reuse to the five New File Wizard list templates and empty-state `New or Import File` entry points to their tables. Import fills editable path/name/suffix fields, existing regular files are referenced without modification, duplicate list references are disabled, and replacement remains the top-path `Change File...` action.
- [x] Dear ImGui docking-based multi-window layout.
- [x] UI language switching between Simplified Chinese, English, and Japanese.
- [x] Prefill the current New Map Element wizard template's matching key from the Structure Model, Sound File, or 3D Sound File List context menu while preserving its other draft fields and target source file.
- [x] Settings for font size, UI component size, station marker size, 2D line widths, theme color, 3D scene draw distance/fog/map-draw-distance, camera speed, gizmo size, and scene-instance performance warnings.
- [x] Recent-map history.
- [x] Save background-image parameters with recent-map entries in `settings/history.ini`.
- [x] Save settings to `settings/settings.ini` under the executable directory.
- [x] Include-file structure diagram and read-only source text preview using the active in-memory working copy.
- [x] In edit mode, import an existing child map or exclusively create a new UTF-8/no-BOM/CRLF `BveTs Map 2.02:utf-8` child map from the File Structure Diagram. The canonical Include is staged in the selected physical source after the last zero-distance Include, before the first local distance when needed, or at end of file; normal Save commits the parent map.
- [x] In edit mode, use `Change File...` on the top path of the Station, Structure Model, Signal Aspect, Sound File, or 3D Sound File List to replace the matching `*.Load` source path. The candidate uses the map loader's existing list-title/version checks (Station List 0.04+ under its compatibility rule, Structure List 1.00+, Signal Aspects List 2.00+, and Sound List 2.00+ for both sound lists), refreshes the working copy/list cache immediately, and saves through normal Save; confirmation discards only the target list's unapplied drafts or applied-but-unsaved old-content edits.
- [x] In edit mode, right-click any Station, Structure, Signal, Sound, or Sound3D resource-list content cell to insert a shared inline-draft row above or below. Insertions preserve source encoding/line endings and Apply/Save behavior, use fixed BVE CSV widths (Structure 2, Station 13, Signal 6, Sound/Sound3D 3), and keep Signal primary/glare pairs together; a new Signal primary has no glare until `Add Glare` is chosen.
- [x] In the File Structure Diagram, use `Unlink Include` on an Include node to delete the parent map's typed `include` statement through the normal Apply/Save path; keep the action disabled outside Edit mode and block it when surviving statements depend on variables or distance assigned by the removed subtree.
- [x] In the File Structure Diagram, use `Change Included File...` on an Include node to replace its parent map path argument with a selected `.txt`/`.csv` child map (relative path preferred, absolute fallback against the entry-map directory); refresh caches after memory Apply and block replacements that remove required variables or create duplicate declarations.
- [x] Edit mode with separate Apply-to-preview, Save-to-disk, global Revert of all pending changes, and Reload-from-disk behavior plus unsaved-change prompts.
- [x] Prompt to rebase whole metres into the begin distance when applying a coordinate-enabled `Repeater.Begin` whose absolute Z-axis offset exceeds 5 m, matching `Structure.Put` behavior.
- [x] Add an `Insert Change Point` action in Repeater `Properties/Edit` that opens a source-matched Begin/Begin0 wizard with Begin-only selected and current inspector drafts prefilled.
- [x] Show the six coordinate-offset fields for `Structure.Put`, while keeping them hidden for `Structure.Put0`, in the New Map Element wizard.
- [x] Group existing New Map Element wizard templates into expandable/collapsible Map Info-style categories, with Structures first.
- [x] Create all currently editable official other-track change forms from the New Map Element wizard, including normalized key-based automatic track creation/reuse; legacy `Track.Gauge` and `Track.Cant` aliases remain edit-only.
- [x] Export own-track and other-track geometry to CSV.

### Bug Fixes

- [x] Fix Station.List serialization for BVE: when an edited or inserted station-definition row is written, blank `stoppageTime`, `signalFlag`, `alightingTime`, `passengers`, `doorReopen`, and `stuckInDoor` fields are emitted as `0`; station keys/names, times, and sound keys remain empty when blank, while untouched source rows retain their original text.
- [x] Fix the blank-map/new-reference workflow: `Include` and the five `*.Load` references may target any loaded non-resource-list map source file, including completely distance-free blank maps; the New Map Element wizard accepts the same targets and appends a canonical tail distance block for a source with zero or one numeric distance statement without moving existing statements; a header-only resource list takes its first row directly from an `Add Row` button or context-menu insertion, and maploader appends that row after the header with fixed CSV field counts; one ledger batch that mixes unsaved `*.Load` insertions with resource-list row edits is now planned in two stages so list-row editIds resolve against a working copy that already contains the new Loads, eliminating `unsupported or unknown editId` failures; and editable-list context-menu actions are deferred until after table rendering, fixing the crash when choosing `Insert Row Below`.

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
- [x] 通过“新建地图元素”向导单独新增 `Repeater.Begin`/`Begin0`、`Repeater.End`，或原子新增 Begin+End；保留半开同名区间保护和经确认的 Begin-only 变化点，允许孤立 End，并阻止在已有显式 End 的有效区间内增加 End
- [x] 在“新建地图元素”向导中新增“应用并编辑”按钮：应用成功后关闭向导，并直接打开本次新建主语句的“属性/编辑”窗口；从 Repeater“属性/编辑”窗口打开的向导中该按钮不可用
- [x] 通过场景文件直接打开地图：打开对话框接受 `BveTs Scenario 2.00` 场景文件，按官方 `Route` 条目（单路径或多候选加权）以场景文件目录为基准、按声明编码解码解析出地图入口文件，经正常流程加载；存在多个候选时弹出固定宽度选择对话框
- [x] 在“地图信息列表 -> 其它 -> 场景文件”新增只读标签页：每个场景文档均显示全部八个官方字段、源码中的相对 Route/Vehicle 路径及权重；直接打开地图时选项禁用，缺少/无效 Route 目标或取消候选选择时仍保留独立场景预览

### 自轨道与他轨道几何

- [x] 解析并计算自轨道曲线
- [x] 解析并计算自轨道坡度，并在平面几何中考虑纵坡产生的水平投影缩短
- [x] 支持旧式语法
- [x] 解析受支持的旧式自轨道语句 `Legacy.Turn`、`Legacy.Curve` 和 `Legacy.Pitch`
- [x] 解析并计算他轨道位置、横向/纵向插值、轨距、中心、超高等部分信息
- [x] 解析他轨道 `Track.Position`、`Track.X/Y.Interpolate`、`Track.Gauge` 和 `Track.Cant.*` 语句
- [x] 支持控制点范围和间隔设置，并可重新生成几何
- [x] 支持限速区间读取与显示
- [x] 编辑或删除已有自轨道曲线变化点，并在适用时联动成对的 `Curve.BeginTransition`；“新建地图元素”向导可新建现行 `Curve.Begin(radius)`、成对的 `Curve.BeginTransition()` + `Curve.Begin(radius, cant)`、`Curve.Change(radius)` 和 `Curve.End()`，旧式别名与 `Interpolate` 仍不提供新建。
- [x] 编辑或删除已有自轨道坡度变化点，并在适用时联动成对的 `Gradient.BeginTransition`；“新建地图元素”向导可新建 `Gradient.Begin(gradient)` 与 `Gradient.End()`，可原子地前置 `Gradient.BeginTransition()`；`Gradient.BeginConst` 和 `Interpolate` 仍不提供新建。
- [x] “新建地图元素”向导可为原子新增的自轨道 Curve/Gradient 缓和曲线对分别设置起点和后续生效语句里程，同时保持 BVE 源语句顺序和全量重解析联动验证。
- [x] 从编辑模式下的 2D/3D 标记编辑或删除受支持的既有他轨道变化语句；track key、方法和参数个数只读，暂不支持新建、拖动、gizmo 或方法转换
- [x] 从“其他轨道”表统一重命名根地图及 Include 中同键的全部 `Track[...]` 语句；执行全地图重名保护且不级联修改依赖地图元素，他轨道变化点检查器中的 track key 保持只读

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
- [x] 校验 `Station.Put` 停车位置容差：`margin1` 非负或 `margin2` 非正时，加载会给出含源码位置的警告；基于源码的编辑与新建会阻止零值和错误符号的值
- [x] 编辑、清空、调整顺序或删除由 `Station.Load` 载入的车站定义行
- [x] 显示 `信号现示列表`、`地图信号列表` 和 `应答器列表`
- [x] 显示 `限速点列表`、`轨道变位列表`、`粘着特性变化点列表`、走行音、轮缘摩擦音效和道岔音效相关表格
- [x] 显示 `背景变化点列表`、`驾驶台亮度变化点列表`、`雾效果变化点列表` 和 `绘制距离变化点列表`
- [x] 通过源码回写的行内表格编辑器编辑、清空、调整顺序或删除 `Signal.Load` 信号现示定义及可选 glare 行；暂不支持新增现示结构 key 列
- [x] 通过基于源锚点的属性检查器编辑或删除已有 `Beacon.Put` 行
- [x] 为已支持的布景/信号机/车站/Repeater 放置，以及限速点、轨道变位、应答器、音效/噪声、背景、粘着、驾驶台亮度、雾和绘制距离行提供“属性/编辑”检查器；可从适用的表格和 2D/3D 标记进入，并为可编辑的布景、信号机和 Repeater Begin 放置提供实时 X/Y/Z 操纵器
- [x] 从 2D 平面图的布景/信号机放置标记打开“属性/编辑”

### 3D画布

- [x] 布景模型 3D 预览
- [x] 通过 `model_loader.dll`/Assimp 读取模型和贴图
- [x] 支持旋转和缩放布景模型预览
- [x] 3D 画布场景预览，可显示轨道路径、布景/连续布景实例、信号、地图元素标记、背景变化和插值后的 BVE 雾效果
- [x] 在 3D 画布设置中启用后，打开或重新加载地图时自动加载 3D 场景预览
- [x] 在 3D 场景预览中显示每个 FlangeNoise 标记的 `index` 值
- [x] 在 3D 场景里程选择模式中高亮自轨道平面上最近的整米位置，显示跟随鼠标的里程标签，并可通过右键菜单打开自动填写 distance 的“新建地图元素向导”
- [x] 可通过车站跳转和数字里程跳转移动 3D 场景相机，并在平面图上显示当前 3D 位置
- [x] 可从布景、连续布景、信号和支持的地图元素标记表格行定位到 3D 场景，也可从场景对象或标记定位回对应表格
- [x] 在编辑模式未开启时，右键点击3D场景中的曲线变化点或坡度变化点标记，右键菜单仅显示禁用的“属性/编辑”和“删除”，不再错误显示“此BeginTransition没有对应的Begin/End，无法编辑或删除”或发生位置偏移
- [x] 在 3D 场景线路信息叠加层显示当前曲线半径/超高、坡度、生效限速、闭塞选择出的信号限速和距下一站距离
- [x] 通过尺寸可调的实时 3D 操纵器编辑 `Structure.Put`、`Signal.Put` 和 `Repeater.Begin` 的 X/Y/Z 位置；检查器按钮可在 `Put`/`Put0`、`Begin`/`Begin0` 间双向转换，Put0/Begin0 提供仅 Z 轴、整米步进的放置/起始里程操纵器
- [x] 在 3D 场景中将 `Sound3D.Put` 的标签尖端定位到相对自轨道的固定 X/Y 音源；通过实时 X/Y/Z 操纵器编辑 X/Y（0.001 m）或 distance（Z 轴整米），不改变 `Put(x, y)` 语法或音频播放行为
- [x] 为 `Structure.PutBetween` 的全部检查器字段提供精确的实时顶点变形预览，并通过仅 Z 轴、整米步进的 3D 操纵器编辑 `distance`
- [x] 通过位于对应轨道中心线上的仅 Z 轴整米操纵器编辑显式 Repeater EndDistance，实时更新实例数量，并在他轨道上按自轨道 distance 比例换算拖动
- [x] 在属性检查器中编辑关联的连续布景段，支持 Begin 导航、End/变化边界和关联删除选项
- [x] 在一条关联链的全部 Begin/Begin0/End 中统一修改 `repeaterKey`；以半开区间重叠校验允许端点相接或完全分离的同名 Repeater，并拒绝重叠区间

### 环境效果

- [x] 显示 `音效文件列表`、`3D音效文件列表`、`地图音效列表` 和 `地图3D音效列表`
- [x] 通过源文件关联行内表格编辑器编辑、清空、调整顺序或删除已有的 `Sound.Load` 和 `Sound3D.Load` 文件列表行；选择文件时会尽可能写入相对路径
- [x] 编辑或删除已有 `Sound.Play`/`Sound3D.Put` 放置和走行音/轮缘摩擦音/道岔音事件；车站定义中的报站音 key 可编辑，程序不播放音频
- [x] 编辑或删除已有驾驶台亮度设定位置
- [x] 编辑或删除已有雾效果
- [x] 将只读旧式线性雾语句 `Legacy.Fog(start, end, red, green, blue)` 解析为类型化快照行，并在独立列表及平面图/3D 标牌中按源值显示；编辑、新建与 3D 雾效果未实现
- [x] 为 `Light.Ambient`、`Light.Diffuse` 和 `Light.Direction` 加入基于源码的编辑和延迟删除，并在三语“光照效果”界面加入固定里程 `0` 的“效果”向导模板。根地图及全部 Include 中每类仍只允许一条基础语法正确的语句；RGB 范围、Direction 里程、完整重解析/语义证明、原始表达式保持及内存 Apply/Save/Revert 行为均继续校验。不实现标记或 3D 光照模拟

### 用户界面与辅助功能

- [x] 新增 `文件 -> 新建...` 及三语“新建文件向导”，可在不覆盖已有文件的前提下新建仅含文件头的 `BveTs Map 2.02` 地图或 Structure、Signal、Sound、Sound3D、Station 列表文件。选择已加载地图时，会暂存类型化 `include`/`*.Load` 引用，正常“保存”才提交；“撤销”会保留已创建文件。预设仍为空，向导不新建场景文件或列表行。
- [x] 为五种资源列表“新建文件向导”模板加入已有文件导入/复用，并在对应表格空态加入“新建或导入文件”入口。导入会填入仍可编辑的路径/文件名/后缀，已有普通文件只会被引用而不修改，重复资源列表引用会禁用，替换仍通过顶部路径的“更换文件...”。
- [x] Dear ImGui Docking 多窗口布局
- [x] 简体中文、英文、日文界面语言切换
- [x] 通过布景模型、音效文件或 3D 音效文件列表的右键菜单，将当前资源 key 预填入“新建地图元素向导”的匹配字段，并保留其他草稿字段和目标源文件
- [x] 字体大小、组件大小、车站标记大小、2D 线宽、主题色、3D 场景绘制距离/雾效果/地图绘制距离、相机速度、操纵器尺寸和场景实例性能警告设置
- [x] 最近打开地图历史记录
- [x] 背景图参数随最近地图保存到 `settings/history.ini`
- [x] 设置保存到程序目录下的 `settings/settings.ini`
- [x] Include 文件结构图，以及读取当前内存工作副本的只读源码文本预览
- [x] 编辑模式下可从文件结构图导入已有子地图，或排他新建 UTF-8 无 BOM、CRLF 的 `BveTs Map 2.02:utf-8` 子地图；规范 Include 暂存到选定物理源中首个本地距离语句前最后一个零距离 Include 的下方、首个距离语句上方或文件末尾，正常“保存”才提交父地图
- [x] 编辑模式下可在车站、布景模型、信号现示、音效文件或 3D 音效文件列表顶部路径右键选择“更换文件...”，替换对应 `*.Load` 源路径。候选文件沿用地图加载器既有列表标题/版本校验（Station List 沿用 0.04+ 兼容规则、Structure List 1.00+、Signal Aspects List 2.00+，Sound 与 Sound3D 共用 Sound List 2.00+），立即刷新内存工作副本/列表缓存并经正常“保存”写盘；确认时只丢弃目标列表未应用草稿或已应用但未保存的旧内容修改。
- [x] 编辑模式下可在 Station、Structure、Signal、Sound、Sound3D 资源列表任意内容单元格右键，在上方或下方新增共享行内草稿；插入保留源码编码/换行并走既有应用/保存流程，固定 BVE CSV 字段数依次为 13、2、6、3、3，Signal 主行/glare 成对保持绑定，新 Signal 主行默认无 glare，需手动“新增眩光”。
- [x] 在文件结构图中右键 Include 文件节点并选择“解除引用”，经现有类型化删除/应用/保存流程从上级地图删除对应 `include` 语句；编辑模式关闭时菜单项禁用，后续语句仍依赖该子树内的变量或距离时删除被阻止
- [x] 在文件结构图中右键 Include 文件节点并选择“更换文件...”，选择 .txt/.csv 子地图后经类型化更新流程将上级地图对应 `include` 语句的路径改写为新引用（复用布景模型列表的“优先相对路径、失败时绝对路径”逻辑，基准目录与解析器一致取入口地图目录）；应用后缓存自动刷新且不直接保存；编辑模式关闭时菜单项禁用，后续语句仍依赖旧子树内变量或替换会产生重复声明时编辑被阻止
- [x] 编辑模式中分离“应用到预览”“保存到磁盘”“撤销全部待保存改动”和“从磁盘重新加载”，并在存在未保存更改时确认
- [x] 对启用坐标偏移且 Z 轴偏移绝对值大于 5 m 的 `Repeater.Begin`，在应用时像 `Structure.Put` 一样提示将整米部分重设到起始里程
- [x] 在连续布景“属性/编辑”中新增“插入变化点”，按源语句打开对应 Begin/Begin0 向导，默认仅添加 Begin，并预填当前检查器草稿
- [x] 在“新建地图元素向导”中为 `Structure.Put` 显示六个坐标偏移输入框，同时保持 `Structure.Put0` 隐藏这些字段
- [x] 在“新建地图元素向导”中将现有模板按可展开/折叠的“地图信息列表”式分类显示，并将布景置于最前
- [x] 在“新建地图元素向导”中新增当前所有可编辑的官方他轨道变化形式；按规范 key 自动创建/复用他轨道，旧式 `Track.Gauge` 和 `Track.Cant` 别名仍仅支持编辑
- [x] 将自轨道和他轨道几何导出为 CSV

### 问题修复

- [x] 修复 Station.List 写回的 BVE 兼容性：编辑或新增车站定义行写回时，空的 `stoppageTime`、`signalFlag`、`alightingTime`、`passengers`、`doorReopen` 和 `stuckInDoor` 统一输出为 `0`；station key/名称、时间和音效 key 为空时仍保留为空，未触及的源码行保持原始文本。
- [x] 修复空白地图/新建引用工作流：`Include` 与五种 `*.Load` 引用可选择任意已加载的非资源列表地图源文件，包括完全无距离语句的空白地图；“新建地图元素”向导也可选择这些目标，对于只有零或一条数值距离语句的源文件，会在不移动既有语句的前提下追加规范尾部距离块；仅有文件头的资源列表可通过“新增行”按钮或右键插入直接创建首行，maploader 将该行按固定 CSV 字段数追加到文件头之后；同一账本同时含有未保存 `*.Load` 插入与资源列表行编辑时改为两阶段规划，使列表行 editId 在已包含新 Load 的临时工作副本中解析，消除 `unsupported or unknown editId` 报错；资源列表右键菜单动作延迟到表格渲染结束后执行，修复点击“在下方新增行”时崩溃的问题。
