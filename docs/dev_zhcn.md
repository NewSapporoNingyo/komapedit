# 开发者指南

[README](README_zhcn.md) · [路线图](../TODO.md) · [AI 辅助开发](ai-dev_zhcn.md)

本文档介绍 komapedit 的开发环境、架构、源码职责与验证流程。使用 AI 编程工具时，还须遵守 [`ai-dev_zhcn.md`](ai-dev_zhcn.md)、[`AGENTS.md`](../AGENTS.md) 与 [`.agents/skills`](../.agents/skills) 中匹配的工作流。

## 范围与支持环境

komapedit 是使用 C++17 编写的 Windows 桌面应用，用于查看和编辑 BVE Trainsim 地图。应用采用 Win32、DirectX 11、WIC、Dear ImGui 和 ImPlot，使用 CMake 与 Ninja 构建。

应用包含三个运行时组件：

- `maploader.dll`：解析地图和列表、处理 Include 与编码、生成自轨道和他轨道几何，并持有带版本的强类型地图/编辑快照。
- `model_loader.dll`：通过 Assimp 读取 Structure 网格、材质和纹理，并以 C ABI 提供数据。
- `komapedit.exe`：提供 Win32/DirectX 11 GUI、表格、二维图表、三维预览和编辑流程。

已实现功能以源码和用户指南为准；待办事项见 [`TODO.md`](../TODO.md)，完成记录见 [`TODO_done.md`](TODO_done.md)。

## 前置环境

- Windows
- CMake 3.20 或更高版本；使用通用 Assimp 运行时 DLL 复制回退时建议 3.21 或更高版本
- Ninja
- MSVC、MinGW 等支持 C++17 的编译器
- Windows SDK、DirectX 11 和 WIC 开发库
- Git
- 可被 CMake 发现为 `assimp::assimp` 的 Assimp

获取 Dear ImGui 和 ImPlot：

```bat
.\get_3rd_party_packages.bat
```

Assimp 需要单独安装。使用 vcpkg 时，设置 `VCPKG_ROOT` 并安装与工具链匹配的 triplet：

```bat
set VCPKG_ROOT=C:\path\to\vcpkg
%VCPKG_ROOT%\vcpkg install assimp:x64-mingw-dynamic
```

设置 `VCPKG_ROOT` 后，构建脚本会自动使用 vcpkg。未设置 `VCPKG_DEFAULT_TRIPLET` 时默认使用 `x64-mingw-dynamic`；MSVC 用户应明确选择 `x64-windows` 等合适 triplet。`install_Assimp.bat` 是面向 MinGW 的辅助脚本，可能需要填写本机 vcpkg 路径；不得提交该本机路径。

## 构建与测试

Debug 构建：

```bat
.\build_dev.bat
```

Release 构建：

```bat
.\build_release.bat
```

### 构建完成通知

`build_dev.bat` 和 `build_release.bat` 在配置、编译、运行时文件检查及许可声明复制步骤完成后发送构建完成通知。

如需使用 Windows Toast 通知，请打开 Windows PowerShell（`powershell.exe`），为当前用户安装可选的 [`BurntToast`](https://www.powershellgallery.com/packages/BurntToast) 模块：

```powershell
Install-Module -Name BurntToast -Scope CurrentUser
Get-Module -ListAvailable -Name BurntToast
```

脚本通过 `powershell.exe` 探测模块并调用 `New-BurntToastNotification -Text 'Build finished'`，因此应在该 PowerShell 环境中确认模块可用。

未安装 `BurntToast` 时，脚本使用 Windows 自带的 [`msg.exe`](https://learn.microsoft.com/windows-server/administration/windows-commands/msg) 向 `%USERNAME%` 显示最长 10 秒的 `build finished` 消息。

运行已注册的 Debug 测试：

```bat
ctest --test-dir build --output-on-failure
```

严格验证需要显式配置 Debug 目录：

```bat
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DKOMAPEDIT_STRICT_WARNINGS=ON -DBUILD_TESTING=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

`KOMAPEDIT_STRICT_WARNINGS` 默认关闭。CTest 注册了七项契约：`multilanguage_contract`、`typed_snapshot_contract`、`maploader_gradient_projection_contract`、`typed_edit_contract`、`maploader_diagnostics_contract`、`canvas3d_camera_contract` 和 `route_value_sampling_contract`。

测试程序和 headless 实现仅在 Debug 编译：`build_dev.bat` 启用 `BUILD_TESTING`，`build_release.bat` 关闭测试并检查输出中是否残留 `*_tests.exe`。headless 命令须单独运行。诊断契约使用 Git 忽略的本地 `tests/` 夹具，运行前需确认夹具齐全。

`build\bin\typed_snapshot_tests.exe slop` 运行对象键表达式、Section 稀疏索引、有限距离和补丁预览回归，相关用例也纳入 snapshot/edit 契约。

`build\bin\typed_snapshot_tests.exe patch-bench 7` 对 100、1,000、10,000 项确定性更新执行完整 dry-run，测量 `plan.patch_sources` 阶段。每种规模预热一次，再输出各次样本及 median/p95；重复次数默认 7，范围 5–50。此基准通过命令显式运行。

`build\bin\typed_snapshot_tests.exe distance-plan-bench 4000 middle` 测量含 1,000、2,000 或 4,000 个距离锚点的合成 Map 中的一次插入，目标可选 `middle` 或 `eof`。每个进程预热一次，再进行五次 dry-run，报告 `plan.prepare` 和总耗时的 median/p95、进程工作集峰值字节数，并检查内存 Apply/Reset 与磁盘字节保持性。

运行时输出布局如下：

- Debug 使用 `build\`，Release 使用 `build_release\`。
- 输出根目录包含 `komapedit.exe`、`LICENSE`、`NOTICE` 和 `THIRD_PARTY_NOTICES.md`。
- `bin\` 包含 `maploader.dll`、`model_loader.dll`、Assimp 和复制的运行时 DLL。
- `settings\` 包含应用生成的 `settings.ini`、`history.ini` 和 `imgui.ini`。
- 构建及发布清理脚本发现输出根目录中有旧 INI 或 DLL 时立即中止，提示用户手动处理。

构建目录、克隆的 `third_party` 源码树、生成的设置/CSV/测试输出及临时线路、地图和模型夹具应保持在 Git 跟踪范围之外。

## 源码分区

| 区域 | 主要文件与职责 |
| --- | --- |
| 地图公共 ABI | `include/maploader.h`、`include/maploader_snapshot.h`：API v13 / 地图快照 v9 函数、定宽 POD 快照、Scenario v2 快照与 v2 直写草稿、编辑批次、报告、跨度、所有权、版本与结构尺寸 |
| 地图生命周期 | `src/maploader/maploader.cpp`：C ABI 入口、句柄、重建、分发、源码读取与边界错误处理 |
| 地图状态 | `maploader_internal.h`：`MapContext`、解析行、源码跨度、Include 栈、编辑引用、报告与计时 |
| 解析 | `maploader_core.cpp`、`maploader_parser.cpp`、`text_decoder.cpp/.h`：语句、值、Include、变量、编码、源码锚点、唯一性检查 |
| 场景解析与直接保存 | `scenario_route.cpp/.h`：BVE 文件类型探测、完整官方 Scenario 快照解析、源码安全的直接保存与 `Route` 候选解析，支撑“从场景文件打开地图” |
| 几何 | `maploader_geometry.cpp`：自/他轨道几何、重定位、曲线、坡度、放置缓冲区与场景控制点 |
| 身份与快照 | `maploader_identity.cpp`、`maploader_snapshot.cpp`、`maploader_semantic.cpp`：稳定 ID、强类型快照、修订、比较与指纹 |
| 编辑 | `maploader_edits.cpp`：试运行、内存应用、直接应用、提交、重置、源码补丁、编码感知写回与距离调整；Include 语句支持受限的路径参数更新，经新旧子树掩码的全量重解析验证 |
| 共享关联 | `include/repeater_linkage.h`、`include/own_track_transition_linkage.h`：Repeater 链与曲线/坡度过渡配对 |
| 共享线路值采样 | `include/route_value_sampling.h`、`src/main_window/route_value_sampling.cpp`：稳定的求值事件序列、BeginTransition/Interpolate 区间分类、省略值继承与 2D/3D 共用端点里程 |
| 共享数值安全与编辑计时 | `include/numeric_safety.h`：GUI/场景 double 转整数时检查有限性和范围；`include/operation_timing.h`：GUI 与 maploader 分域的可选稳态时钟包含式计时 |
| 模型读取 | `src/model_loader/model_loader.cpp`、`include/model_loader.h`：Assimp 隔离与 model-loader API v2 |
| 主窗口 | `gui_kme.cpp`、`kme.h` 及职责明确的 `src/main_window/` 模块：App 状态协调、Win32/D3D11 启动、通用工具/背景图、快照水合/加载、编辑/距离/Inspector/列表草稿/新建文件/新建元素工作流、对话框/UI、场景预览与无头入口。新建文件渲染位于 `new_element_wizard.cpp`，正文/对话框辅助位于 `app_dialogs.cpp`，延迟创建位于 `element_inspector_data.cpp`，工作流契约位于 `headless_entrypoints.cpp` |
| 运行时/设置 | `app_settings.cpp/.h`、`runtime_paths.cpp/.h`、`maploader_runtime.cpp`：INI、相对可执行文件路径、DLL 加载、精确 API 检查 |
| 源码工具 | `file_structure_diagram.cpp`、`text_preview.cpp`：Include 图、工作副本预览、源码操作（更换 Include 文件、解除引用）与距离边界选择 |
| Debug 验证 | `debug_headless.cpp/.h`、`headless_entrypoints.cpp`、`edit_benchmark.cpp`、`src/table/datatable_benchmark.cpp`、`touch_input.cpp/.h`：参数解析、无界面契约、正式工作流、编辑与表格缓存基准、相机传递、查找、触摸、编辑与文件创建检查 |
| 二维视图 | `src/canvas2d/canvas2D.cpp`：平面/profile 数据、缓存化的 `Curve.Interpolate` 端点标记水合与平面绘制编排；`canvas2d_view_state.cpp/.h`：平移/缩放/旋转和坐标转换；`canvas2d_marker_cache.cpp/.h`：轨道采样与 marker/Repeater 叠加缓存；`canvas2d_interaction.cpp/.h`：测量/marker 命中、上下文目标/动作和源码映射；`canvas2d_background.cpp/.h`：图片坐标、绘制和两点对齐；`canvas2d_primitives.cpp/.h`：屏幕变换、裁剪折线、网格、比例尺和标记绘制；`profile_plots.cpp`：纵断面与半径图表 |
| 三维视图 | `include/canvas3D.h` 与 `src/canvas3d/canvas3D.cpp`：公共预览接口和薄委托；私有 `canvas3d_impl.h` 状态及按职责划分的 `canvas3d_*.cpp` 实现，详见下文三维模块表；`scene_track_sampling.cpp/.h` 与 `scene_route_overlay.cpp/.h`：CPU 采样和纯线路信息格式化 |
| 表格/导航 | 按功能划分的 `src/table/datatable*.cpp`、`datatable_internal.h` 与 `table_navigation.cpp`：共享单元格/列、单一缓存水合、查找、资源列表行内编辑、线路/效果/Scenario 窗口、Debug 基准及行/平面/场景导航 |
| 共享标记 | `include/map_marker_visuals.h`、`map_marker_visuals.cpp`：二维/三维标记的唯一视觉配方 |
| 本地化 | `include/multilanguage.h`：简体中文、台湾繁体中文、香港繁体中文、英语和日语界面文本 |

源码所有权、关联、标记、导航、解析、验证和写回应沿用各自的组件边界与共享实现。

## 详细代码解说

本节按文件介绍 `include/` 与 `src/` 中的主要职责。GUI 通过 C ABI 调用两个 DLL；解析器持有源码身份，快照提供跨 ABI 数据视图，编辑器生成、验证并提交源码补丁。

### 公共 ABI、共享算法与资源头文件

#### `include/maploader.h`

- **导出与日志接口**：`KV_API` 控制 DLL 导出/导入；`KvLogCallback` 与 `kv_set_log_callback()` 把 DLL 内的英文诊断交给宿主。`kv_api_version()` 返回 ABI 版本，EXE 必须精确匹配。
- **场景文件接口**：`kv_probe_file_kind()` 区分地图与 Scenario；`kv_resolve_scenario_routes()` 返回目标文件存在的 Route 候选，配对 `kv_free_scenario_candidates()` 释放；`kv_load_scenario_snapshot()` 返回独立 v2 快照，配对 `kv_free_scenario_snapshot()` 释放；v2 `kv_save_scenario_document()` 校验源哈希、完整重解析后按原编码事务写回，支持调整已有 Route/Vehicle 候选的数量和顺序。
- **句柄与几何生成**：`kv_load_map_ex()` 通过 `KV_LOAD_PREVIEW`、`KV_LOAD_EDIT_METADATA` 选择预览或编辑元数据。`kv_generate_geometry()` 和 `kv_generate_scene_geometry()` 分别生成常规与场景轨道数据，更新句柄缓存及各自修订号。
- **只读快照**：`kv_get_map_snapshot()`、`kv_get_scene_geometry_snapshot()` 校验版本与结构尺寸后返回句柄拥有的视图；调用方在重解析或对应几何失效前复制所需数据，嵌套存储随句柄管理。
- **编辑与源码访问**：`kv_get_edit_target_typed()` 取得一个稳定 edit id 的字段和源码信息，`kv_get_source_text()` 返回当前磁盘或内存覆盖层中的解码文本。`kv_edit_dry_run_typed()`、`kv_edit_apply_to_memory_typed()`、`kv_edit_apply_typed()`、`kv_edit_commit_typed()`、`kv_edit_reset_memory()` 分别承担验证、应用到工作副本、直接写盘、提交工作副本和撤销覆盖层。
- **错误与释放**：`kv_get_last_error()` 返回线程局部错误文本；`kv_free()` 释放地图句柄，`kv_free_string()` 释放 DLL 分配的独立字符串。

#### `include/maploader_snapshot.h`

- **版本与通用视图块**：文件开头定义 API/快照版本、`KV_INDEX_NONE`、能力位和编辑标志。`KvUtf8View` 是调用期 UTF-8 输入，`KvStringRef`、`KvSpan` 和 `KvDoubleBuffer` 是指向快照 arena/数组的定宽视图。
- **Scenario 快照块**：`KvScenarioPathWeightRow` 保存相对路径、权重及显式权重标记；v2 `KvScenarioSnapshot` 保存源哈希与八字段存在位；`KvScenarioEditDocument`/`KvScenarioEditPathRow` 是 `kv_save_scenario_document()` 的调用期输入。
- **值与源码身份块**：`KvValueKind`、`KvValue` 表示 null、数值、字符串和 continue；`KvSourceFileRow`、`KvSourceSpanRow`、`KvStatementRow`、`KvElementRow` 与 `KvRowMetadata` 保存物理文件、Include 栈、字节/行列范围、原始参数、解析顺序和稳定 edit id。
- **强类型行块**：`KvTrack`、`KvStation*`、`KvStructure*`、`KvRepeater*`、`KvSignal*`、`KvSection*`、`KvSound*`、`KvOtherTrain*` 及曲线、坡度、限速和环境效果等 POD 固定各类字段形状；可变参数通过 `KvSpan` 引用共享值数组。
- **根快照**：`KvMapSnapshot` 汇总字符串 arena、通用值、各类行数组、几何矩阵、源码注册表、能力位及 content/geometry revision。`KvSceneGeometrySnapshot` 承载场景控制点与轨道矩阵，具有独立的失效周期。
- **编辑协议块**：`KvEditField`、`KvEditTargetSnapshot` 描述可编辑字段；`KvEditOperation`、`KvEditChange`、`KvEditBatch` 表示插入/更新/删除请求；距离消歧、补丁预览、已提交文件/行和 `KvEditReportSnapshot` 描述验证及提交结果。请求与快照按公共头文件约定检查版本和结构尺寸，字段变更须同步 EXE/DLL。

#### `include/model_loader.h`

- `MlVertex` 保存位置、法线与 UV；`MlMaterial` 保存漫反射色和贴图路径；`MlMeshPart` 将索引范围绑定到材质；`MlMeshData` 汇总顶点、索引、材质、分段和包围盒/中心/半径。当前 `ml_api_version()` 返回 v2，EXE 会做精确版本检查。
- `ml_api_version()`、`ml_load_model()`、`ml_free_model()`、`ml_get_last_error()` 构成完整 C ABI。所有数组由 DLL 分配，成功或部分失败后的唯一释放路径都是 `ml_free_model()`。

#### `include/repeater_linkage.h`

- `EventKind` 将 `Begin`/`Begin0` 归为 Begin，并区分 End 和其他事件；`BoundaryKind` 区分显式 End、后续 Begin 和未闭合边界；`Event` 保留距离、全局解析顺序、repeater key 和源行索引。
- `canonical_key()` 统一 key 的大小写比较。`pair_linkage()` 按距离、全局解析顺序、源行索引稳定排序，按 repeater key 维护活动链并生成 `Chain` 和 `Segment`；遇到新 Begin 时封闭旧段，遇到 End 时结束链。同里程最后解析的事件决定活动状态。
- `pair_segments()` 提供扁平段列表，供地图快照、表格、二维和三维共用。

#### `include/own_track_transition_linkage.h`

- `EventKind` 表示曲线/坡度的 `BeginTransition` 及其可能的 Begin/End 消费者，`Pair` 保存过渡行与消费行的索引。
- `consumes_curve_transition()`、`consumes_gradient_transition()` 定义哪些语句可消费待配对过渡。`pair_transitions()` 按源码顺序维护曲线和坡度两套挂起状态，输出配对及 orphan 列表；编辑和标记路径据此把过渡操作绑定到消费语句。

#### `include/map_marker_visuals.h`

- `MapMarkerVisualKind` 是二维/三维共同的元素视觉枚举，`map_marker_visual_bit()` 将其映射为可见性位。
- `MapMarkerPrimitiveKind`、`MapMarkerColorRole`、`MapMarkerIconVariant` 定义图标原语、主题色角色和变体；`MapMarkerIconPrimitive`、`MapMarkerIconRecipe` 保存归一化点、线宽、闭合/填充和 glyph 信息。
- `map_marker_theme_color()`、`map_marker_role_color()`、`map_marker_icon_recipe()` 与 `draw_map_marker_icon()` 统一 2D/3D 的颜色、图标配方和 ImDrawList 绘制接口。

#### `include/route_value_sampling.h` 与 `src/main_window/route_value_sampling.cpp`

- `Event` 保留 maploader 已求值、按稳定里程顺序输出的线路值及事件类型；`append_event()` 统一识别普通值、`BeginTransition` 和 `Interpolate`，无参数 Interpolate 沿用前值。
- `sample()` 供 2D/3D 共用：常量区间返回当前值，过渡或插值区间返回起止里程与两端值。

#### `include/numeric_safety.h` 与 `include/operation_timing.h`

- `kme::truncating_int_or_zero()` 只对有限且位于 `int` 范围内的 double 做截断转换；非有限值或越界值统一返回 `0`，GUI 与场景代码共用这一边界。
- `kme::timing::Timing` 提供线程局部、显式激活的包含式阶段计时。GUI 与 maploader 分域记录，按阶段累计毫秒数和次数，供控制台诊断使用。

#### `include/canvas3D.h`

- **场景输入模型**：`Canvas3DTrackPoint/Path/Visibility` 描述轨道采样与显示；`Canvas3DSceneObject`、`Canvas3DModelInstance`、`Canvas3DRepeaterSegment`、背景/雾/绘制距离事件构成场景实体输入。
- **线路信息与标记**：`route_value_sampling::Event`、站点、限速、Section 信号事件用于相机里程采样；`Canvas3DSceneMarker` 保存视觉 kind、里程、轨道位置、表格目标和 edit id，`Canvas3DSceneMarkerVisibility` 以分类型位控制索引重建。
- **构建与刷新结构**：`Canvas3DScene` 是独立于渲染器的 CPU 场景描述；`Canvas3DSceneBuildOptions/Result`、`Canvas3DSceneMapRefreshOptions` 区分首次构建、动态内容刷新和地图内容刷新；`Canvas3DSceneStats` 提供实例、模型和帧率统计。
- **交互结构**：相机姿态、上下文动作、拾取目标、`Canvas3DPlacementEditTarget`、拖动轴与 `Canvas3DPlacementDragUpdate` 将渲染交互转换为 GUI 可应用的源码字段更新。
- **`Canvas3D` 门面类**：提供单模型与场景的加载、刷新、可见性、视距、雾、操纵器、相机、性能警告及调试接口，具体实现委托给私有 PImpl。

#### `include/multilanguage.h`

- `Language` 指定日语、英语、简体中文、台湾繁体中文或香港繁体中文，`Translation` 的五个字段保存同一 UI 文本，其中 `Language::ZhTw` 对应 `zh_tw`，`Language::ZhHk` 对应 `zh_hk`。文件主体按窗口、菜单、工具栏、表格、属性编辑、错误提示和 2D/3D 操作分组声明翻译常量。
- `tr()`/语言选择帮助代码在运行时返回当前语言字段。增加用户可见字符串时必须在同一个 `Translation` 初始化器中同时填写五种语言，并保持格式占位符一致。`zh_tw` 使用台湾用语，row 为「列」、column 为「欄」；`zh_hk` 使用香港用语，row 为「行」、column 为「欄」、cell 为「單元格」。香港版优先依据简体中文文案，必要时参考日语和英语术语。
- 现有 `[General] language` 设置保存 `ja`、`en`、`zh`、`zh-TW` 或 `zh-HK`，默认仍为简体中文。`中文` 子菜单提供 `简体`、`台湾繁體` 和 `香港繁體`，该子菜单与英语和日语选项并列。
- `multilanguage_contract` 检查键集一致、文本非空、占位符名称与次数、语言查找和关键术语。字体加载保留完整中文字集，在既有 CJK 字体候选之后、Segoe UI 之前加入微軟正黑體（`msjh.ttc`）。

#### `include/resource.h`

- Windows 资源编译器与 C++ 共用资源编号；`IDI_KOMAPEDIT` 对应 `komapedit.rc` 中的应用图标。

### maploader 内部状态、解析与快照

#### `src/maploader/maploader_internal.h`

- **基础工具与计时**：字符串辅助声明、`SteadyClock`、`LoadTiming`、`ScopedTimer`、`ActiveTimingScope` 记录读取、解析、合并、几何和快照阶段；并发任务槽声明限制昂贵加载任务同时运行。
- **解码与解析选项**：`LoadedText` 同时保存原始字节、UTF-8 正文、编码、BOM、换行和行起点；`MapParseOptions` 决定预览/编辑元数据水位；`SourceTextOverride(s)` 是内存工作副本覆盖层。
- **表达式值**：`ValueKind`、`Value`、`VariableEnvironment` 表示解析期 null/number/string 和变量绑定；环境快照以共享只读映射挂到语句，供距离移动验证语义环境。
- **源码模型**：`SourceFileRecord`、`FileStructureRecord`、`SourceSpan`、`ParsedStatement`、`EditSourceRef`、`MapDiagnostic` 保存物理文件、Include 调用身份、原始语句、行列/字节锚点、解析顺序和编辑身份。
- **解析行记录**：`CurveEditRow`、`GradientEditRow`、`OtherTrackChange` 以及 Station、Structure、Repeater、Signal、Section、Sound、Train、限速和环境效果记录，为几何、快照和编辑器提供强类型数据；各行的 `EditSourceRef` 用于源码回写。
- **矩阵与快照存储**：`Matrix` 管理行列连续 double 缓冲；`MapSnapshotStorage`、`SceneGeometrySnapshotStorage`、`EditTargetSnapshotStorage`、`EditReportSnapshotStorage` 拥有 ABI 指针背后的 vector/string arena。
- **`MapContext` 聚合根**：持有主路径、源码/Include 表、变量环境、所有解析行、轨道与场景矩阵、控制点、revision、快照缓存、工作副本覆盖、磁盘基线 hash、最近编辑报告和计时。解析、几何、快照与编辑都以它作为唯一所有权根。
- **活动语句与编辑 RAII**：`ActiveStatementScope` 在 dispatch 期间设置当前语句/源码环境并自动恢复；`MapEditChange` 及各专用字段结构承载复制后的 ABI 请求；语义快照、距离消歧、补丁/提交报告结构支持事务验证。
- **跨实现文件声明区**：文件尾声明 DLL 内部的 parse、geometry、snapshot、semantic、edit 和 identity 模块入口。

#### `src/maploader/text_decoder.h`

- 声明 UTF-8 与 `std::filesystem::path` 的双向转换、二进制读取、UTF-16/代码页解码、BOM/首行检测和编码感知写回。
- `FileOpenFailureKind` 区分不存在、权限、目录和一般打开失败，使解析器与模型加载器可生成一致诊断。写回函数接收目标编码与 BOM 信息，并在字符不可表示时失败。

#### `src/maploader/text_decoder.cpp`

- `classify_file_open_failure()`、`file_open_failure_message()` 和 `read_binary_file()` 负责可靠读取及 Windows 错误分类；`path_to_utf8()`、`utf8_to_wide()`、`wide_to_utf8()`、`path_from_utf8()`、`join_utf8_path()` 隔离 Win32 宽字符路径细节。
- `decode_codepage()` 在 Windows 使用严格/宽松代码页转换；非 Windows 分支给出受限回退。`append_utf8_codepoint()` 与 `decode_utf16()` 手工处理端序、代理对和非法序列。
- `decode_text_bytes()` 按声明编码/BOM 选择 UTF-8、UTF-16 或 CP932 路径；`first_line_ascii()` 和 `has_utf8_bom()` 支持在完整解码前识别地图头。
- `append_utf16_bytes()` 与 `encode_text_for_writeback()` 将 UTF-8 工作副本转回原编码并保留 BOM；字符无法表示时抛出错误。

#### `src/maploader/maploader_core.cpp`

- **任务、时间和标量帮助函数**：`try_acquire_maploader_task_slot()`/`release_maploader_task_slot()` 控制加载并发；`ActiveTimingScope` 记录总活动时间；`ascii_lower()`、trim、`parse_finite_number()`、`canonical_number()`、版本/编码头解析提供统一标量规则。
- **文本装载**：`build_line_starts()`、`detect_newline()`、`make_loaded_header_text()`、两个 `load_header_text()` 重载把原始字节变成带编码、换行和正文位置信息的 `LoadedText`，并优先读取内存覆盖。
- **Value/key 转换**：`as_number()`、`as_text()`、`key_text()`、`track_key_display_text()`、`track_key_from_display_text()` 和 CSV/INI 字段帮助函数统一解析器、表格语义和编辑文本表示。
- **源码注册与定位**：`normalized_source_path/key()`、`current_source_text()`、`register_source_file_index()`、Include 栈和 invocation key intern 函数去重源码身份；`line_column_for_body_pos()`、`make_source_span()` 把正文偏移转换为稳定物理锚点。
- **语句与环境登记**：`current_variable_environment_snapshot()`、`rebuild_variable_environment_snapshot()`、`add_parsed_statement()`、`next_active_edit_ref()` 建立语句、环境和 edit ref；merge/offset 函数把 Include 子上下文合入父上下文而保持索引正确。
- **列表行源码块**：`add_loaded_line_statement()`、`extend_loaded_line_statement()` 为 CSV/list 物理行建立可编辑语句；`parse_signal_aspect_source_values()` 和字段名帮助函数保留可变结构 key 列及 glare 行形状。
- **依赖与状态更新**：`value_equal()`、变量读写记录、`log_load_timing()` 支持语义验证和性能日志；`add_controlpoint()`、`set_distance()`、`put_own()`、`ensure_othertrack()`、`put_other()` 是 parser dispatch 写入轨道事件状态的统一入口。

#### `src/maploader/maploader_parser.cpp`

- **词法/语句循环**：`Parser::parse()` 驱动整文件；`eof()`、`peek()`、`skip()`、`accept()`、`expect()` 处理空白、注释和标点；诊断函数记录位置并在 `finish_statement()`/`synchronize_statement()` 中恢复到下一条语句。
- **对象、函数与表达式**：`parse_label()`、`parse_variable_name()`、`parse_map_object()`、`parse_map_function()`、`parse_map_args()` 构造 `MapObject`/`MapFunction`；`parse_expression()`、`parse_prefix()`、`parse_primary()`、`apply_binary()`、`call_function()` 实现优先级、变量、字符串、数值和受支持数学函数。
- **Include 流程**：`include_path_is_simple_string()` 检查预览路径；`make_child_seed()` 继承普通变量和 Include 身份，并以零里程、空距离表达式创建子上下文。`parse_include_context()` 支持并行解析；`queue_include()`、`flush_pending_includes()` 按原顺序合并源码、诊断、事件和变量写入，保留父文件里程，并在变量依赖过期时重解析。随机引擎由进程会话种子、源码路径和 Include 词法顺序派生，重解析沿用排队时的种子，使 Preview/Edit 加载及工作副本重解析能够重放未修改的 `rand()` 调用。
- **语法验证与总分派**：`method_rules()` 是方法参数个数/空值规则表；`object_path()`、`validate_statement()` 形成一般语法门；`dispatch()` 再按顶层对象路由到专用函数。`record_deferred_semantics()` 记录需要等资源列表全部读完后才可验证的 key。
- **自轨道与他轨道**：`dispatch_curve()`、`dispatch_gradient()`、`dispatch_legacy()` 记录曲线、坡度和旧式事件；`dispatch_track()`、`setposition_interpolate()`、`track_position()` 记录他轨道位置、插值、轨距、中心和超高事件。
- **资源列表**：`load_resource_list()` 与 `record_resource_list_load()` 保存 Load 的原表达式、求值路径和源码身份；`parse_station_list()`、`parse_structure_list()`、`parse_signal_aspect_list()`、`parse_sound_list()` 把物理列表行转成强类型且可回写的记录；`parse_other_train_file()` 读取他列车文件。
- **地图元素分派**：`dispatch_station/speedlimit/section/signal/beacon/pretrain/structure/sound/train/repeater/irregularity/background/adhesion/cab_illuminance/fog/draw_distance()` 及三个 noise 分派函数，检查方法形状、读取参数并追加对应行；`add_other_train_definition()` 统一 Train 定义登记。
- **解析后诊断**：`validate_unique_preview_statements()` 检查重复 Load/Enable；`append_transition_diagnostics()` 检查过渡配对；`append_deferred_key_diagnostics()` 检查资源 key。车站使用到达/出发音效时，`append_station_sound_load_order_diagnostic()` 检查 `Sound.Load` 是否早于 `Station.Load`，异常顺序作为英文 `[WARN]` 输出：同文件比较行列，跨文件比较 Include 深度，同深度再比较全局解析顺序。`emit_diagnostics()` 输出诊断，并将错误升级为加载失败。
- **模块入口**：`parse_map_context()` 创建 `MapContext`、装载主文件、运行 Parser、刷新环境/诊断并返回完整上下文，是所有加载和编辑后重解析的共同入口。

#### `src/maploader/maploader_geometry.cpp`

- **轨道状态机**：`LastPos` 保存上一采样点，`TrackPointer` 按距离推进 own-track 事件并给出当前 radius、gradient、cant、方向与坐标；它是常规采样和事件边界采样的基础。
- **曲线数学**：`rotate_xy()`、Gauss 积分、Fresnel 级数/渐近式实现局部坐标积分；`circular_curve*()` 计算圆曲线，`halfsin_intermediate()`、`linear_transition_curve_local()`、`transition_curve*()` 计算半正弦/线性缓和曲线。key/hash 结构缓存重复参数的曲线结果。
- **坡度投影**：`constant_gradient_projection()`、`sinc()`、`gradient_transition()` 计算线路长度对应的平面投影和高程；`build_gradient_projection_samples()`、`build_event_projected_distances()` 将事件里程映射到平面位置。
- **自轨道生成**：`sorted_unique()`、`append_arange()` 汇合事件点和等间距点；`generate_owntrack()` 逐采样写出距离、XYZ、朝向、radius、gradient、cant 等列；`generate_curveradius()` 形成曲率图数据。
- **他轨道生成**：`relative_position()` 计算曲线上的相对偏移；`CantProcessor` 处理超高 Begin/End/Interpolate；`build_othertrack_buffer()` 合并位置、X/Y 插值、轨距、中心和 cant，输出与 own-track 对齐的矩阵及有效性。
- **重定位与放置**：`relocate()` 统一平移几何到稳定局部坐标；`build_structure_put_buffer()` 为结构放置提供按里程采样的变换基础。
- **场景自适应控制点**：角度/矩阵帮助函数与 `build_scene_adaptive_controlpoints()` 把事件、模型跨度、曲率、坡度和请求范围组合成密度自适应控制点。
- **入口**：`generate_geometry()` 依次生成 own track、曲率、other tracks、放置缓冲与场景控制点，更新耗时、能力位和相应快照 revision。

#### `src/maploader/maploader_identity.cpp`

- `stable_hash64()` 实现确定性 64 位哈希，`hex64()` 输出固定十六进制文本，`edit_kind_token()` 规范 row kind。
- `make_edit_id()` 把规范化源码 key、全局解析顺序、语句种类和本地序号组合成稳定 ID；`statement_edit_id()` 缓存语句 ID；`native_element_edit_id()` 与 `element_edit_id()` 为原生行和必要的派生身份提供统一入口。

#### `src/maploader/maploader_snapshot.cpp`

- `matrix_view()`、`data_or_null()` 把内部连续容器安全投影为 ABI 视图。
- `MapSnapshotBuilder::build()` 依次构建根数据、轨道、车站、布景、他列车、区间/信号/声音、环境效果、作者消息、预览行和编辑注册表，最后调用 `finalize()` 绑定快照。
- `string_ref()` 在共享 arena 中复用已有字符串并追加新文本；`value()`/`append_values()`/`append_strings()` 生成值与 span；`metadata()` 将 `EditSourceRef` 转为 `KvRowMetadata`。各 `add_*` 块复制类型化字段，并按能力位附加源码信息。
- `add_element(s)()` 建立 row kind/edit id 到行索引的注册表；`bind()` 在所有 vector 完成扩容后绑定裸指针；`finalize()` 写入版本、结构尺寸、数量、revision 和 capability。
- `invalidate_map_snapshot()`、`invalidate_scene_geometry_snapshot()` 明确内容、常规几何和场景几何的失效边界。`build_map_snapshot()`、`build_scene_geometry_snapshot()` 延迟重建缓存；`ordered_station_list_entries()` 保持车站列表物理顺序。

#### `src/maploader/maploader_semantic.cpp`

- `SemanticWriter` 按固定顺序写入类型和值并计算哈希；`field()`、`value_span()`、`begin_element()`、`emit_element()` 生成规范化语义表示。
- `changed_field()`、数字/字符串/value/track-key 读取函数把某个 `MapEditChange` 叠加到快照原值上，并拒绝非法数字或缺少的必需值。
- `write_structure_model()`、`write_sound_list()`、`write_structure_put()`、`write_structure_between()`、`write_station_put/list()`、`write_signal_aspect/put()`、`write_repeater()` 以及 beacon、sound、noise、background、adhesion、fog 等 `write_*` 函数，逐类定义“编辑前后应相等/应改变”的语义字段集合。
- `write_curve()`、`write_gradient()`、`write_other_track_change()` 保留方法、参数个数和配对信息；`write_section_row()` 支持可变值列表；`reject_unknown_target_fields()` 拒绝未声明字段。
- `build_semantic_map_snapshot()` 遍历所有受保护元素，生成 edit id 到语义的索引及整图/环境指纹。`expected_target_semantic()` 计算更新/删除目标的期望结果；`FakeInsertSnapshotState`、`insert_semantic_container()`、`expected_insert_semantic()` 为新建语句构造同样可验证的期望语义。

#### `src/maploader/maploader_edits.cpp`

- **ABI 输入复制**：`copy_utf8_view()` 和 `copy_edit_batch()` 校验结构尺寸、指针/长度、operation、flags、字段重复与 UTF-8 view 生命周期，把调用期 POD 复制为内部 `MapEditChange`。
- **补丁定位**：`load_source_patch()` 读取工作副本；`source_range_in_text()`、`safe_statement_removal_range()` 和预览函数将 `SourceSpan` 转为 UTF-8 文本范围，并保留相邻注释与语句。
- **距离表达式调整**：表达式扫描函数识别 predefined `distance`、顶层加减号和安全数值加数；`find_safe_numeric_distance_addend()`、`apply_delta_to_distance_addend()`、`adjust_distance_expression_by_delta()` 优先保留变量表达式，只在安全时修改常量项，否则生成距离消歧建议。
- **参数与 CSV 构造**：BVE 参数分割/引用、数值/optional/value/key 帮助函数保留未改 raw arg；CSV 解析、等价比较和 `build_editable_csv_list_statement()` 保持分隔、尾部字段与编码可写性。
- **逐类语句生成器**：`build_structure_model/sound_list/station_list/signal_aspect_statement()` 负责列表行；`build_station_put/structure_put/signal_put/repeater_statement()` 处理显式方法/参数形状转换，其中 Structure 与 Repeater 支持普通形式和零偏移形式双向转换；其余 `build_*` 覆盖曲线、坡度、他轨道、Section、限速、应答器、声音/噪声和环境效果，维持原方法与参数形状。
- **目标发现与编辑目标快照**：模板化 `match_edit_ref()`、`find_simple_target()` 和 `find_editable_target()` 在 MapContext 强类型行中定位 edit id；`build_edit_target_snapshot()` 输出字段、原值、raw arg、约束、sourceHash 和 expectedSourceHash。
- **插入验证**：`validate_insert_field_names()`、`validate_insert_method()`、`validate_insert_change()` 校验 row kind、方法和结构化字段；`build_insert_statement()` 据此生成普通 BVE 语句。
- **距离块规划**：`DistanceSectionAnalysis/PlanningIndex` 按物理文件、Include 调用实例和距离段建立索引，规划初始块、普通间隙、末块及 EOF。明确末段内的移动可沿该段方向扩展；新建优先使用已有唯一位置，歧义位置交由用户选择。候选筛选共用变量和物理实例检查，选定方案在应用或写盘前执行完整语义验证。
- **报告与事务写盘**：`build_edit_report_snapshot()` 投影补丁、消歧和提交信息；hash/临时文件函数创建同目录暂存文件；`replace_files_transactionally()` 按阶段替换并在失败时回滚，`TransactionalWriteError` 保留主错误与回滚错误。
- **完整语义验证**：`parse_report_candidate()` 用补丁覆盖重解析；`validate_non_target_derived_state()`、`own_track_transition_state()`、`validate_edit_report()` 比较非目标元素、最终变量绑定、车站所有权、过渡配对和每个目标的期望语义；合法编辑可改变最终当前 `distance`。
- **批次主流程**：`build_edit_report()` 预处理目标、按物理上下文和目标距离分组，解决 boundary，生成替换/删除/插入，检测重叠补丁，重解析并验证。它是 dry-run、内存 Apply 和直接 Apply 的共同核心。
- **工作副本与提交**：`apply_patched_files_to_overrides()`、`reparse_context_with_overrides()`、`apply_edit_report_to_memory()` 更新内存覆盖且保留磁盘基线；`reset_memory_edits()` 回到磁盘；`populate_committed_edit_state()` 记录落盘身份；`commit_memory_edits()` 重新检查并事务写入所有覆盖文件。

#### `src/maploader/maploader.cpp`

- `parse_options_from_load_flags()` 把公共 flags 转为内部 profile，并拒绝未知组合。
- `kv_*` 导出函数验证句柄、版本、结构尺寸和参数后调用内部实现；在 C ABI 边界捕获异常，写入 last error 并返回失败值。
- `kv_load_map_ex()` 创建 `MapContext`；两个 geometry 入口更新矩阵和修订；两个 snapshot 入口返回缓存视图；edit target/source text 入口返回工作副本信息。
- Scenario 相关导出调用 `probe_bve_file_kind()`、`load_scenario_document()` 或 `resolve_scenario_route_candidates()`，把独立分配的快照/候选块及其匹配释放函数保持在同一 DLL 所有权边界内。
- dry-run 只构建报告，memory apply 构建并应用覆盖，reset 丢弃覆盖，direct apply 对报告直接事务写盘，commit 保存已验证工作副本。`kv_free()`/`kv_free_string()` 与 DLL 分配所有权成对。

#### `src/maploader/scenario_route.h` 与 `src/maploader/scenario_route.cpp`

- `probe_bve_file_kind()` 只读取文件开头，用于区分 Map、Scenario 和未知文件；不可读文件保留为 Unknown，交由正常加载流程报告详细错误。
- `load_scenario_document()` 按声明编码解析 `BveTs Scenario 2.00`，处理 `#`/`;` 注释和重复字段的末项优先规则，保留八个官方字段、源哈希/存在位以及 Route/Vehicle 的源码相对路径、权重与显式权重标记。
- `save_scenario_document()` 重新读取磁盘并比较 expected source hash；候选数量不变时对最后生效字段逐项做最小补丁，数量变化时仅重写该字段的完整候选值；拒绝空路径、保留语法字符和非正/非有限权重，要求每个已存在字段至少一个候选，完整重解析后调用共享事务写回。
- `resolve_scenario_route_candidates()` 复用 Scenario 解析结果，按其所在目录解析 Route 相对路径并验证目标文件存在；快照读取负责保留声明数据。

#### `src/maploader/diagnostics.h` 与 `src/maploader/diagnostics.cpp`

- 头文件声明日志回调、last-error 和 info/warn/error 接口。回调通过原子读写发布，各调用线程以 `thread_local` 保存最后错误。
- `emit_log()` 转发完整日志行；`log_info_at()`、`log_warn_at()`、`log_error_at()` 添加级别与源文件名。ABI 捕获块通过 `set_last_error()` 保存可查询的错误。

#### `src/maploader/c_api.h` 与 `src/maploader/c_api.cpp`

- 这是 DLL 内部的 C 字符串所有权帮助层。`copy_c_string()` 使用 `std::malloc()` 复制带 NUL 的 UTF-8 文本，分配结果只交给公共 ABI，最终由 `kv_free_string()` 的匹配 `std::free()` 路径释放。

### 模型加载

#### `src/model_loader/model_loader.cpp`

- 路径/扩展帮助函数规范化 Assimp 格式提示，`copy_c_string()` 为材质贴图路径分配 ABI 字符串，`resolved_texture_path()` 将模型相对贴图解析为 UTF-8 路径。
- `free_mesh()` 幂等释放顶点、索引、材质字符串和 mesh parts；`MeshCleanupGuard` 负责异常清理；`assign_bounds()` 计算包围盒、中心与半径。
- `load_with_assimp()` 先用共享二进制读取支持 Unicode 路径，再调用 Assimp importer；随后合并各 aiMesh 顶点/法线/UV/索引，建立材质和分段，解析第一张漫反射贴图并计算包围盒。
- `ml_api_version()` 返回 v2；`ml_load_model()` 清零输出、捕获异常并填充 `MlMeshData`；`ml_free_model()` 是公共释放入口；`ml_get_last_error()` 返回线程局部诊断。

### 主窗口状态、加载与编辑工作流

#### `src/main_window/kme.h`

- **通用数学与哈希**：`KmeByteHash64` 为 GUI 缓存/调试提供确定性字节哈希；`Matrix` 是从 ABI 复制后的 GUI 自有二维 double 缓冲。
- **地图模型**：`TrackEvent`、`OwnTrackEditMarker`、`OtherTrack`、Station/SpeedLimit/Section 和各类 `TableRow` 组成 `MapModel`；它复制并持有 GUI 所需的源码注册表、资源列表元数据、revision、能力位和常规/场景矩阵。
- **二维数据**：`View2D` 保存平移、比例和旋转；`TrackPoint`、`PlanMarker` 及各别名、`PlanRepeaterSegment`、`OtherTrainPathOverlay`、`PlanData`、`ProfileData` 是画布缓存和 hit-test 输入。
- **表格与源码工具状态**：`TableRow/ColumnDef`、`CachedTableRow`、`TableUiCache` 保存按 revision 构建的显示数据；File Structure、Text Preview、DistanceResolution 结构保存布局、选择和解析器确认的边界。
- **设置与运行状态**：`TextureImage` 管理 D3D 背景纹理；日志、窗口可见性、2D/3D 视图、`UserSettings`、最近地图和背景历史结构对应 INI 持久化字段；`ScenarioRoutePickState` 保存候选选择/入口选项，`ScenarioPreview` 与其磁盘基线保存可编辑 Scenario 草稿。
- **编辑状态机**：字段约束、inspector session、pending change、preview snapshot、Repeater draft、NewElement template/wizard、delete mode、distance workflow 和 editable-list draft 结构，明确区分尚未 Apply 的 UI 草稿、已 Apply 工作副本和已保存磁盘状态。
- **`App` 类**：声明窗口渲染、异步加载、快照转换、几何/场景重建、所有表格和导航、编辑/保存/重载、对话框、设置、背景图、2D/3D 交互及缓存成员。其成员布局是各 GUI `.cpp` 文件共享的应用状态契约。

#### `src/main_window/maploader_runtime.cpp`

- `KME_MAPLOADER_FUNCTIONS` 宏是唯一符号清单；`MaploaderRuntime` 构造时从 `runtime_paths::dll_path()` 加载 `maploader.dll`，解析包括唯一加载入口 `kv_load_map_ex()` 在内的全部函数指针，并检查 `kv_api_version()==KV_MAPLOADER_API_VERSION`。失败信息包含 Win32 错误文本。
- 全局 `kv_*` 函数通过 `ensure_loaded()` 调用缓存的 DLL 函数指针；失败时返回错误值并提供 GUI 可查询的诊断。GUI 使用公共头文件中的函数名，运行时动态加载 DLL。

#### `src/main_window/runtime_paths.h` 与 `src/main_window/runtime_paths.cpp`

- `executable_directory()` 首次调用 `GetModuleFileNameW()` 并缓存 exe 目录；`dll_directory()` 固定为其 `bin` 子目录；`settings_directory()` 创建并返回 `settings` 子目录。
- `dll_path()` 拼装 DLL 路径；`load_dll()` 以受限搜索标志从 `bin` 加载依赖，并可返回 Win32 错误码。

#### `src/main_window/app_settings.h` 与 `src/main_window/app_settings.cpp`

- 头文件暴露默认 INI 路径、内部显式路径设置加载入口、用户设置/历史读写、ImGui layout 延迟保存和运行时样式应用函数。
- 实现前段规范化存储路径、最近地图 key/显示名，clamp 字体、控件、marker、线宽、场景距离/操纵器/实例警告阈值；颜色函数负责 hex 序列化、palette、透明度和混色。
- 语言、bool、2D mode 和 grid mode 的转换函数定义 INI 值语法。`save_user_settings()` 写入 `General`、`WindowVisibility`、`View2D`、`View3D` 的规范键；`load_user_settings()` 按精确节名、键名和值语法读取，未知或无效项采用默认值。已有文件的重写由保存操作负责。
- `load_imgui_layout()`、`save_imgui_layout()`、`save_imgui_layout_if_requested()` 和 pending flag 管理 `imgui.ini` 的显式/延迟持久化。
- `load_history_state()` 读取 `[Recent]`、`[MapN]` 的最近地图与八个路径/背景字段，以及 `[CreatorMessages]`、`[CreatorMessageN]` 的作者消息偏好。最近地图经过路径规范化、去重后最多保留十项；`save_history_entries()` 和 `save_history_state()` 写回规范格式，数值使用固定区域设置。
- `apply_ui_font_size()`、`apply_ui_theme_color()`、`apply_ui_component_size()`、`apply_ui_settings()` 把持久设置投影到 ImGui style，并按 DPI/viewports 修正圆角和尺寸。

#### `src/main_window/gui_kme.cpp` 与拆分模块

- `gui_kme.cpp` 管理 `App` 构造、析构与日志回调；`kme.h` 声明 EXE 内共享状态和跨翻译单元接口。
- `win32_dx11_bootstrap.cpp` 拥有 D3D11 设备/渲染目标、`WndProc()`、窗口消息循环和 `main()`。
- `gui_common_utils.cpp` 集中字体、主题/日志颜色、编码转换、数值格式、路径及里程跳转控件帮助函数；`background_image.cpp` 拥有 WIC 解码、背景纹理重建与 history 背景持久化。
- `map_snapshot_hydration.cpp` 从 typed snapshot 构建 `MapModel`、行元数据、Station edit id、限速缓存与过渡关联，并为加载和保存共用 Scenario 快照复制；`map_load_pipeline.cpp` 处理 Map/Scenario 探测、Scenario 草稿基线与 Route 候选、异步地图加载、入口历史、结果应用、元数据合并、加载计时和几何再生成。
- `edit_ledger.cpp` 处理 typed 批次/报告、账本同步、本地预览、删除、保存/撤销/关闭；`distance_resolution_workflow.cpp` 处理距离消歧请求与继续应用。
- `edit_benchmark.cpp` 承载独立的 Debug 编辑性能入口：真实输入只做内存 Apply/Delete/Revert 与字节保护，Save 仅在保持相对依赖布局的排他临时普通文件副本中运行，同时检查刷新合并、回滚和阶段计时契约。
- `element_inspector_data.cpp` 管理 Inspector 打开、定位、字段/场景编辑数据与 Apply；`element_inspector_render.cpp` 只渲染 Inspector 字段、可选插入参数和可变 Repeater/Section UI。
- `editable_list_drafts.cpp` 管理资源列表草稿；`new_element_wizard.cpp` 拥有新元素模板/向导、结构化插入以及新建文件向导的状态与渲染；`headless_entrypoints.cpp` 承载复用正式 App 工作流的新元素、资源列表替换/插入、新建文件向导和 Scenario 创建契约。
- `app_dialogs.cpp` 拥有文件对话框、Scenario Route 候选选择、Scenario 新文件正文构建和其它模态弹窗；`element_inspector_data.cpp` 处理新文件请求的延迟、排他创建与创建后打开；`ui_elements.cpp` 拥有 dockspace、菜单、工具栏、状态栏、控制台、快捷键和设置投影；`scene_preview_lifecycle.cpp` 拥有场景/模型预览的启停、重建、可见性和窗口渲染。

#### `src/main_window/file_structure_diagram.cpp`

- 布局按 Include depth 分组，缓存节点尺寸、连线、范围和 revision；`file_structure_layout_is_current()` 检查缓存，`rebuild_file_structure_layout()` 在源码结构或字体/尺寸变化时重建。
- `open_parent_directory_in_explorer()` 通过 ShellExecute 打开物理目录。`render_source_file_context_menu()` 是结构图和属性检查器共享的“打开目录/源码预览”动作；Include 节点在编辑模式下还提供“更换文件...”与“解除引用”两个延迟请求入口。
- `App::render_file_structure_window()` 绘制可平移画布、层级连接线、主文件/Include 节点、hover tooltip 和右键菜单，并把选中节点交给工作副本 Text Preview。

#### `src/main_window/text_preview.cpp`

- `build_text_preview_lines()` 建立行起始字节索引；`decode_preview_bytes()` 是非 parser-confirmed 文件的只读解码回退。正常 map/list 预览优先走 `kv_get_source_text()`，因此可显示内存 Apply 后的工作副本。
- boundary range/gap/EOF 函数把 `DistanceResolutionBoundary` 按行定位；marker style/render 函数在源码行间显示解析器确认的插入点，`utf8_byte_for_source_column()` 将源码列转换到 ImGui 文本选择字节。
- `open_text_preview()`、`refresh_text_preview_from_working_copy()`、`refresh_text_preview_after_map_load()` 管理普通预览；`open_text_preview_for_distance_resolution()` 注入候选边界和目标语句定位。
- `render_text_preview_window()` 绘制行号、只读 UTF-8 文本、选择、源码定位及边界按钮；用户选择报告提供的边界 token 后，由主编辑状态机重试。

#### `src/main_window/touch_input.h` 与 `src/main_window/touch_input.cpp`

- 头文件的 `TouchFrame` 汇总单帧 tap、long press、scroll 和 pinch（含 `PinchAxis`）；公共函数负责 Win32 消息接入、每帧状态、区域消费、popup 手势和测试注入。
- 实现中的 `ActiveTouch`、`PairState`、`TouchManager` 跟踪 pointer id、按下位置/时间、移动阈值、双指中心和缩放。消息处理识别 down/update/up/capture lost，长按与滚动互斥，pinch 将距离变化映射为轴向缩放。
- `new_frame()` 发布并清理瞬时事件，`consume_*` 标记手势消费，`apply_touch_scroll_to_hovered_window()` 映射 ImGui 滚动；`debug_*` 以可控时钟和合成触点验证状态机。

#### `src/main_window/map_marker_visuals.cpp`

- 原语构造函数 `append_polyline/polygon/circle/glyph/sampled_arc()` 将标准化几何写入 recipe；station、curve、gradient、speed-limit、beacon、pretrain、sound/noise、background、adhesion、cab light、fog 和 other-track 等 `*_recipe()` 定义唯一图标形状。
- `map_marker_theme_color()` 按 visual kind 返回主题色，`map_marker_role_color()` 将 fill/outline/accent/text 角色与主题混合；`map_marker_icon_recipe()` 选择 kind/variant 的配方。
- `draw_map_marker_icon()` 通过 `transform_icon_point()` 把归一化坐标缩放、旋转、平移到 2D 屏幕，逐原语调用 ImDrawList。3D 代码读取同一 recipe 再生成 billboard 顶点。

### 二维视图与图表

#### `src/canvas2d/canvas2D.cpp`

- **查询与业务数据**：`nearest_own_index()`、`interp_own_z()`、`track_info_at()`、`speed_at()`、`curve_sections()` 为测量和叠加层采样；`build_plan_data()` 合并 own/other track、站点、限速与 marker，`current_plan_data()` 以 model/geometry/visibility revision 缓存；profile 数据有相同 build/current 分层。
- **视图操作与主流程**：测量、聚焦、坐标转换及 `jump_to_distance()` 统一导航；`render_plan_canvas()` 编排鼠标/触摸交互、可见里程计算，以及背景、网格、轨道、Repeater、车站、标记、标签、3D 位置和焦点的绘制。

#### `src/canvas2d/canvas2d_view_state.h` 与 `src/canvas2d/canvas2d_view_state.cpp`

- `App` 持有的 `View2D` 保存视图中心、比例、旋转、fit 与拖动状态，并提供 world/screen 转换、屏幕增量平移和自适应范围。

#### `src/canvas2d/canvas2d_marker_cache.h` 与 `src/canvas2d/canvas2d_marker_cache.cpp`

- matrix row/sample/lower/upper-bound 函数在 own/other track 矩阵上按里程采样，局部偏移函数定位 marker；Repeater LOD、bounds 与 chunk 构建连续布景覆盖范围。
- `rebuild_speed_limit_marker_overlay_cache()` 与 `rebuild_marker_overlay_cache()` 按 edit id、source row 和轨道位置构建平面标记、Repeater 段、他列车路径及可见性索引；`App` 管理缓存及失效。

#### `src/canvas2d/canvas2d_interaction.h` 与 `src/canvas2d/canvas2d_interaction.cpp`

- 测量命中按 plan generation/scale 缓存空间网格，小数据使用穷举；标记命中按屏幕半径、行顺序和重叠优先级选择目标。
- 上下文目标收集、`render_plan_marker_context_menu()` 和 `plan_context_source_for()` 按 marker kind 提供表格定位、属性/编辑、删除和源码位置。

#### `src/canvas2d/canvas2d_background.h` 与 `src/canvas2d/canvas2d_background.cpp`

- 背景图模块负责 world/UV 转换和屏幕四边形绘制，并以两个站点坐标与两个图片点计算比例、旋转和平移；`App` 包装层负责保存背景历史。

#### `src/canvas2d/canvas2d_primitives.h` 与 `src/canvas2d/canvas2d_primitives.cpp`

- `PlanScreenTransform` 转换 model/plan/screen 坐标；折线构建、范围裁剪及 Repeater chunk/overview LOD 筛选可见几何。
- 网格、比例尺及三角、菱形、信号、先行列车、方向箭头和文字函数封装底层 ImDrawList 绘制。

#### `src/canvas2d/profile_plots.cpp`

- 基础函数绘制 vector 曲线、标题/单位、遮盖坐标轴边缘、半径左右标志、底部锁定标签和 profile 垂直 marker；RAII 类临时覆盖 ImPlot fit button 与 wheel zoom 行为。
- 触摸缩放块把 `TouchFrame` 转换为 X 或 XY 轴 limits，并用 `preserved_plot_span()` 在空数据/重建时保持合理跨度。
- `render_profile_plot()` 绘制距离-高程、坡度填色/标签、站点、限速和可编辑曲线/坡度 marker，处理共享 focus、双击测量、hover/context。
- `render_radius_plot()` 绘制距离-曲线半径，分离左右曲线显示并复用 transition marker 关联规则。`render_plots()` 根据当前 2D mode 分配窗口区域并共用缓存的 `ProfileData`。

### 三维渲染与场景构建

#### 三维预览模块

`Canvas3D::Impl` 统一持有预览状态、worker、GPU 资源和缓存。私有头文件声明状态与接口，并保留内联数学运算和 Repeater visitor 模板；各功能 `.cpp` 由 CMake 独立编译。

| 模块 | 职责 |
| --- | --- |
| `canvas3D.cpp`、`canvas3d_impl.h` | 公共薄委托、共享私有方法与状态声明 |
| `canvas3d_math.h`、`canvas3d_types.h` | 内联向量/矩阵运算、CPU/GPU 记录、共享常量与判定函数 |
| `canvas3d_scene_data.cpp/.h` | typed map/scene 转换、线路值与车站、标记元数据、雾输入转换和绘制距离 |
| `scene_fog.cpp/.h`、`scene_shader_source.h` | CPU 雾关键帧构建/采样与场景 HLSL；CPU 逻辑及着色器编译由 `route_value_sampling_contract` 验证 |
| `canvas3d_scene_lifecycle.cpp` | 场景替换、动态/地图/车站刷新、可见性与设置、模型请求及资源生命周期 |
| `canvas3d_model_loader.cpp/.h` | 模型加载器 v2 客户端、WIC 纹理与缓存、CPU 模型 worker、上传队列及诊断 |
| `canvas3d_put_between.cpp/.h` | 源模型准备与变形、异步 PutBetween 预览及经过序号检查的结果发布 |
| `canvas3d_model_preview.cpp` | 单模型加载、资源清理与交互预览 |
| `canvas3d_d3d_resources.cpp` | HLSL、shader 管线、深度/混合/光栅化状态、渲染目标与实例缓冲 |
| `canvas3d_scene_geometry.cpp/.h` | 场景/轨道分块、轨道放置坐标系及 Repeater 实例与缓存操作 |
| `canvas3d_scene_markers.cpp` | 标记顶点、文字与图标、字体缓存、可见索引与标记绘制 |
| `canvas3d_scene_camera.cpp` | 轨道采样、相机移动与跳转、聚焦目标状态 |
| `canvas3d_scene_edit.cpp`、`canvas3d_scene_gizmo.cpp` | 放置预览更新与失效；操纵器投影、命中、拖动与绘制 |
| `canvas3d_scene_render.cpp`、`canvas3d_scene_ui.cpp` | 渲染 pass、拾取/高亮及可见实例；ImGui 编排、叠加信息、右键菜单与延迟动作 |
| `tests/scene_render_contract.cpp`、`tests/scene_loader_contract.cpp` | Debug 渲染、缓存、像素、拾取，以及模型加载器所有权与故障契约 |

#### 渲染与交互

- **轨道采样**：`scene_track_sampling.cpp/.h` 提供普通轨道采样、相机起点前外推和相机里程边界；起点前外推专供相机使用。
- **场景数据**：向量、矩阵和包围盒函数构建世界变换；CPU/GPU 记录分别管理模型、材质、纹理、分块实例、标记、拾取目标和反向定位。
- **模型加载**：`ModelLoaderClient` 从 `bin/model_loader.dll` 加载 v2 API，并配对分配与释放。worker 复制 CPU 模型数据，主线程通过 `upload_pending_scene_models()` 创建 D3D 资源；取消、join、唤醒和上传由生命周期模块协调。
- **资源与场景生命周期**：`load_model()`、`upload_model()`、`reload_model()` 管理单模型预览；`load_scene()`、dynamic/map/station refresh 和 `clear_scene()` 管理场景替换与刷新。管线按需创建，纹理复用缓存，实例缓冲按需扩容，资源由对应 release 函数释放。
- **分块与放置**：`build_scene_chunks()` 按里程组织 Structure、Signal、Repeater 和轨道几何；轨道采样、超高坐标系及 `make_track_placement_frame()`、`make_track_world()` 将 BVE 放置参数转换为世界矩阵。场景使用相机相对坐标和 reversed-Z 深度。
- **标记与拾取**：共享 2D 图标配方生成 3D billboard；可见性变化重建标记索引。pick pass 写入对象/标记 ID 并回读单像素，highlight mask 和 outline composite 绘制悬停与选择轮廓。
- **线路信息**：`scene_route_overlay.cpp/.h` 复用线路值采样，格式化半径、超高、坡度、限速、Section 信号速度与下一站。`Curve.Interpolate` 区间显示两端求值后的半径/超高、方向箭头与三角分隔符；两端半径均显示为零时使用本地化“直线”标签。
- **帧编排**：`render_scene_preview()` 在 `canvas3d_scene_ui.cpp` 中处理异步上传、相机、操纵器、可见实例、绘制、拾取、高亮和上下文菜单，并返回延迟导航/编辑/删除动作；各渲染 pass 由 `canvas3d_scene_render.cpp` 等模块执行。

操纵器按编辑目标生成 `Canvas3DPlacementDragUpdate`：普通放置坐标截断到毫米；Sound3D 的 X/Y 更新音源偏移，Z 以整米更新里程；显式 Repeater End 的 Z 更新段尾。`Structure.PutBetween` 使用沿自轨前向的 Z 轴，将里程吸附到整米；worker 合并最新目标、按模型纵向 slice 复用轨道采样，再将结果发布到可复用动态顶点缓冲。

#### 帧率与阶段计时

画面 FPS 表示场景渲染调用速率。`canvas3d_scene_ui.cpp` 累计不超过 `0.1` 秒的正间隔，活动时间达到 `0.2` 秒后发布 `interval_count / active_seconds`。长间隔丢弃未完成窗口并保留上次读数；reset 清空锚点、累计值和读数。主循环的 Present 与渲染唤醒由 `win32_dx11_bootstrap.cpp` 负责。

`scene_frame_profile.h` 提供 Debug 阶段计时。渲染与加载器契约通过 `NDEBUG` 条件编译进 EXE，由 scene benchmark 和 loader headless 命令运行。

#### 雾效果

`scene_fog` 从已水合的 `Fog`、`Legacy.Fog` 行构建关键帧，随场景刷新重建：

- 按里程和全局源码顺序处理事件；非零里程的 Legacy 语句展开为原里程处的旧状态节点和 `distance + 25` 的目标节点，再稳定排序。
- 同里程的首节点是前一段的插值目标，末节点在该里程生效。省略的 Fog 参数继承已插入的最远节点，包括未来的 Legacy 目标。
- 采样通过二分查找定位；同模式间以 double 插值 density、RGB、start/end，跨模式沿用前节点。没有雾事件时保持无雾，首个省略值使用默认值。
- 输入阶段过滤无效里程和区间值，颜色钳制到 0–1，着色器距离限制在安全 float 范围。

场景像素着色器按相机空间深度（投影 `w`）计算指数雾或线性雾 `clamp((end-depth)/(end-start), 0, 1)`。零宽区间在 `end` 处阶跃，有限反向区间使用同一公式。雾作用于背景、模型和轨道；UI、标记与高亮 mask 独立绘制。公式与深度依据见 Microsoft [雾公式](https://learn.microsoft.com/en-us/windows/win32/direct3d9/fog-formulas)和[像素雾深度](https://learn.microsoft.com/en-us/windows/win32/direct3d9/pixel-fog)。

`src/canvas3d/tests/scene_fog_tests.cpp` 纳入 `route_value_sampling_contract`，覆盖 Legacy/混合模式、相邻及同里程边界、继承、开关和数值保护，并通过 `D3DCompile` 编译场景共用的顶点及普通/雾像素着色器入口。

### 数据表格与跨视图导航

下列文件位于 `src/table/`，由 CMake 独立编译。`App` 持有 `TableUiCache`；`datatable_cache.cpp` 在局部完成构建后以 move 发布，`table_navigation.cpp` 管理失效后的状态复位和跨视图导航。

| 模块 | 职责 |
| --- | --- |
| `datatable.cpp` | 共享单元格/数值转换、表格 UI 基础帮助函数及场景轨道 key 语义标注 |
| `datatable_internal.h` | 私有 inline 列定义及内部 action/view/helper 声明；窗口、缓存构建和专用模板位于各自 `.cpp` |
| `datatable_cache.cpp` | 完整 `TableUiCache` 水合、动态 Section/Signal 列、Repeater 显示行合并、列宽测量与运行态限速缓存刷新 |
| `datatable_find.cpp` | 不区分大小写的 find/reset/step、unused-key 搜索，以及 Structure/Signal/Sound 专用 App 查找 API |
| `datatable_resource_lists.cpp` | 通用可编辑列表渲染，以及 Station、Structure model、Signal aspect、Sound、Sound3D 资源列表窗口 |
| `datatable_route_tables.cpp` | 他轨道、Station.Put、Structure、他列车、Repeater、Signal.Put、Section 与 Variable 窗口 |
| `datatable_effect_tables.cpp` | Beacon、Irregularity、声音/噪声、Background、Adhesion、CabIlluminance、Fog、Lighting、DrawDistance 与 SpeedLimit 窗口 |
| `datatable_scenario.cpp` | Scenario File 表格、路径与候选编辑 UI |
| `datatable_benchmark.cpp` | 仅 Debug 的真实地图缓存重建、热命中与无输入表格帧基准，并校验缓存摘要和源码完整性 |
| `table_navigation.cpp` | 缓存失效状态复位及 table/plan/scene 跨视图导航 |

地图表格读取 `TableUiCache` 和可见性状态，草稿修改、选择与导航调用 `App` 的共享方法。Scenario 表格使用独立的 Scenario 草稿。

#### `src/table/table_navigation.cpp`

- `invalidate_table_cache()` 清除 revision 与派生行；`reset_marker_visibility()`、`sync_marker_visibility_sizes()` 保持二维 marker flags 与模型行数量同步。
- Structure、Repeater、Signal 的 `locate_*_on_plan/in_list/in_scene_preview()` 分别更新 plan focus、table highlight/window visibility 和 Canvas3D jump；Repeater 额外处理 End/变化边界。
- `locate_standard_marker_on_plan()`、`locate_standard_marker_in_list()` 是 Beacon、Section、Irregularity、Sound/Noise、Background、Adhesion、CabIlluminance、Fog、DrawDistance、SpeedLimit 等成对函数的公共实现。
- other-train stop 定位同时打开对应分组与 stop 行。`locate_scene_marker_row_in_list()`、`locate_scene_marker_row_in_scene_preview()` 把 Canvas3D marker enum 映射回正确表格/源行；`can_locate_scene_preview_row()` 检查场景、索引和可见性条件。

### 调试入口与契约测试

#### `src/main_window/debug_headless.h`

- 每个 `*Options` 结构对应一个命令行模式：Map/Scenario 加载、plan/scene/open/edit/table-cache benchmark、场景 loader/相机传递、diagnostics popup、source anchor、roundtrip、distance/own/other track、Station/资源列表、Repeater、Section、Include、新建文件/元素、table find、touch 和 settings persistence。
- 头文件声明 Debug 专用的 `run_debug_headless_*()` 入口；参数结构连接 `main()` 命令行解析与各测试实现。

#### `src/main_window/debug_headless.cpp`

- **公共设施与参数**：COM RAII、UTF 路径、输出文件、耗时统计、hash、快照 matrix 汇总、日志捕获和 fixture 查找函数为无界面模式提供确定输出；该文件也解析各模式参数。独立拆分的编辑基准实现在 `edit_benchmark.cpp`，表格缓存/绘制基准实现在 `src/table/datatable_benchmark.cpp`。
- **加载/几何/场景检查**：基础 Map load 验证 snapshot 结构和矩阵，Scenario load 验证 v2 快照、编辑 roundtrip、候选选择及解析后地图；plan/scene benchmark 重复构建缓存并输出分阶段时间、数量和 hash；camera-transfer 检查 rebuild 前后姿态；scene 调试读取像素与 fog 状态验证渲染结果。
- **`typed_edit_headless`**：`Field/Change/Batch/Report` 是公共编辑 ABI 的 RAII 包装，负责字符串 view 生命周期、dry-run/apply/commit 报告复制和失败信息。
- **距离/自轨道/他轨道批次**：`distance_batch_headless` 使用句柄、编辑目标、边界选择和报告驱动多文件/Include/变量用例；own/other track 模式验证方法与参数形状保留、Apply/Reset/Commit 和几何变化。
- **列表与关联编辑**：`station_list_edit_headless` 创建临时 CSV fixture，验证编辑/清空/重排/删除及原编码；`repeater_batch_headless` 验证 chain 更新、trim 转换和原子删除；`section_edit_batch_headless` 验证动态参数增删、null/表达式保留和 commit。
- **插入与源码锚点**：insert 模式验证允许模板、距离块选择和未知字段拒绝；source-anchor/roundtrip 模式检查物理文件、Include stack、行列/span、stable id 和保存后重载一致性。
- **UI 与持久化检查**：table-find 验证大小写、exact/step/unused 状态；touch 使用合成输入检查 tap、long press、scroll、pinch 和消费语义；settings-persistence 在临时目录验证规范格式、无效项默认值、读取后的文件字节和 History 边界。各入口输出 PASS/FAIL。

#### `src/main_window/edit_benchmark.cpp`

- `App::run_debug_headless_edit_benchmark()` 使用正式 App 的 Inspector Apply、延迟 Delete、Save、Revert、刷新与场景首帧路径；按操作输出包含式计时与汇总统计。输入线路及全部已加载物理源码逐字节保护，Save 仅在独占临时根目录中的普通文件副本上运行；复制流程拒绝 Windows reparse point 和逃逸临时根目录的目标。
- 刷新夹具验证完整/局部水合的合并、表格/平面缓存单次失效、失败 Apply 恢复及空 Save。该文件通过 `NDEBUG` 限定为 Debug 实现。

#### `src/main_window/headless_entrypoints.cpp`

- 复用正式 `App` 编辑状态和对话框请求处理，验证新元素的 Apply/Inspector/删除路径、资源列表文件替换/行插入、新建 Map/Scenario/五种资源列表的创建或复用、引用提交、重载与清理，以及 Scenario 生命周期。
- 新建文件验证要求目标位于 `tests/` 下且尚未存在，结束后清理创建的文件。资源列表插入和替换验证内存 Apply；替换流程使用正式文件选择器。

#### `src/maploader/tests/typed_snapshot_tests.cpp`

- `TempFixture` 创建并清理临时 map/list，编码帮助函数生成 UTF-8/BOM、UTF-16 与 CP932 输入；`MapHandle` RAII 调用 `kv_free()`；`CHECK_ARRAY` 等断言同时检查 count 与空指针契约。
- snapshot 测试遍历所有根数组、字符串/span、metadata、capability、revision 和稳定 edit id，检查 Windows 导出表仅保留 `kv_load_map_ex()` 加载入口，并对 Signal glare/可变 key、资源 Load、Include 和场景快照执行定向检查。
- geometry 测试以坡度/曲线夹具比较线路长度、平面投影、高程和事件距离。
- `UpdateBatch`、`RepeaterTrimBatch` 等包装器构造 typed edits；edit 测试覆盖 dry-run、memory Apply/Reset、直接 Apply、Commit、concurrency hash、距离消歧、方法/参数形状、语义保护、编码和事务回滚。
- diagnostics 测试装载 `tests/` 本地 fixture，验证缺文件、错误语法、重复 Load/Enable、未配对 transition、未知 key 及日志/last-error 文本。`main()` 根据 `snapshot`、`geometry`、`edit`、`diagnostics` 和专项参数选择测试组，返回进程状态供 CTest 使用。

#### `src/main_window/tests/route_value_sampling_tests.cpp`

- CPU 契约覆盖插值区间端点归属、省略值继承、BeginTransition、非法数值，以及 3D 线路信息的正/负/零半径格式和零到零插值的直线显示。

### 静态调用链摘要

```text
main / App
  -> maploader_runtime 的 kv_* 转发器
     -> maploader.cpp 的 C ABI 异常边界
        -> scenario_route -> KvScenarioSnapshot / Route 候选
        -> parse_map_context -> Parser -> MapContext
        -> generate_geometry -> Matrix / scene control points
        -> MapSnapshotBuilder -> KvMapSnapshot
        -> build_edit_report -> 补丁重解析与语义验证 -> 内存覆盖或事务写盘

App / MapModel
  -> datatable、canvas2D、profile_plots 构建按 revision 缓存的二维视图
  -> Canvas3DScene -> Canvas3D::Impl -> D3D11 分块、异步模型、拾取与 gizmo
  -> inspector / inline draft -> KvEditBatch -> maploader 源码优先编辑链
```

`MapContext` 拥有解析与快照存储，GUI 复制所需数据到 `MapModel`；画布按 revision 管理派生缓存。DLL 返回的独立字符串和模型数组由对应 DLL 的释放函数回收。

## 核心工程规则

### C++ 与 ABI

- 使用 C++17，优先采用 RAII、标准容器、`std::filesystem` 和职责单一的帮助函数。
- 保持 `UNICODE`、`_UNICODE`、`NOMINMAX` 和 `WIN32_LEAN_AND_MEAN` 定义。
- 公共 C ABI 使用定宽 POD 和明确的内存所有权；异常在边界捕获，DLL 分配的内存由配对函数释放。
- EXE 要求 maploader API v13、地图快照 v9 和 model-loader API v2 精确匹配。`kv_load_map_ex()` 是唯一地图加载入口；`KvScenarioSnapshot` 和 `KvScenarioEditDocument` 使用 v2。地图、场景几何、编辑目标和报告分别管理版本与结构尺寸。
- ABI 输入是调用期视图；嵌套快照由句柄持有，按公共头文件规定在几何重建、编辑、重置、重解析或释放时失效。Scenario 快照独立分配，由 `kv_free_scenario_snapshot()` 释放。
- 公共 ABI 变更须明确版本/结构尺寸策略，同步 EXE、DLL 和调用方，并记录所有权与有效期。

### 解析、几何与源码保真

支持 BVE Map 2.0+、已有旧式语法、Include、变量、预定义 `distance`、数学函数、注释，以及 UTF-8/BOM、UTF-16LE/BE、CP932/Shift_JIS 输入。解析和预设使用官方 BVE 通用语法。

每个 Map 上下文以零里程和本地距离表达式开始。Include 继承普通变量与源码身份；合并时保留父文件里程及表达式，汇入子文件事件、控制点和变量写入。并行预解析结果按变量依赖检查，必要时重解析。

可编辑行保留物理路径、Include 栈、源码跨度、原语句/参数、求值结果、距离表达式、解析顺序和稳定 ID；`KvMapSnapshot` 以强类型视图传递这些数据。写回保留原编码、BOM 和行尾，新增字符无法用原编码表示时阻止写入。

AI 编程工具修改 BVE 地图、列表或 Scenario 的读取、校验、类型表示、编辑、新建和写回逻辑时，须同时使用匹配的子系统技能和 [`komapedit-bve-format-compliance`](../.agents/skills/komapedit-bve-format-compliance/SKILL.md)。实现前按技能检查带日期的官方页面缓存、阅读受影响页面并完成合规矩阵。

#### Scenario

`scenario_route.cpp/.h` 管理以下流程：

- **读取与预览**：探测文件类型，校验 `BveTs Scenario 2.00` 头部并按声明编码解码，处理 `#`/`;` 注释，保留八个官方字段的最后一项，以及相对路径、权重、源哈希和字段存在位。快照可预览缺少 Route 或目标文件的 Scenario。
- **打开地图**：`kv_resolve_scenario_routes()` 检查 Route 候选及目标文件，再交给地图加载器验证。Vehicle 数据用于预览。Route 解析或地图加载失败时，GUI 保留 Scenario 预览。
- **保存草稿**：GUI 通过 Save 直接提交 Scenario。已有 Route/Vehicle 字段至少保留一个候选，按草稿顺序写回；候选路径须非空且避开保留语法字符，权重须为有限正数。数量相同时逐项修改，数量变化时重写该字段的候选值。写盘前核对源哈希并完整重解析，事务写入保留原编码、BOM 和行尾。
- **创建文件**：新建向导通过 `build_new_scenario_file_content()` 按官方键序生成 UTF-8/CRLF 文件，排他创建后重解析验证。Route/Vehicle 初始值为单路径、无权重；多候选和权重在 Scenario 文件标签页编辑。

#### 曲线参数与光照

`Curve.SetGauge(value)`、`Curve.SetCenter(x)`、`Curve.SetFunction(id)` 和旧式 `Curve.Gauge(value)` 共用 `CurveEditRow` → `KvCurveRow` → `MapModel::curve_rows`，同时生成自轨道几何状态事件。更新保留原方法及源码身份；三个新建模板使用现行方法，默认值依次为 `1.067`、`0`、`0`。`SetFunction` 在加载、更新和插入时均要求一个求值为 `0` 或 `1` 的数值参数。

`Light.Ambient`、`Light.Diffuse`、`Light.Direction` 用于表格参数预览和源码编辑，对应 `light.ambient`、`light.diffuse`、`light.direction` 目标及 `KvLightColorRow`、`KvLightDirectionRow`。三组表单始终显示；编辑条件满足时可修改参数，Apply、Delete、New 按目标和草稿状态启用。Apply 合并已改表单，Delete 使用延迟请求；向导在所选源文件的里程 `0` 创建语句。

根地图与 Include 合并后，每类光照最多保留一条语法正确的声明。同类重复会使冲突行全部无效，并输出包含各物理位置的英文警告；Ambient/Diffuse RGB 限于 `[0, 1]`，Direction 要求里程 `0`。有效行进入快照，更新保留未改参数表达式并接受完整语义验证。

### 编辑模型

maploader 持有源码及编辑身份，GUI 通过类型化请求操作工作副本。Preview/Edit 按 capability bit 水合数据；列表中的未应用草稿须先在表格中 Apply，再执行 Save。

| 操作 | 职责 |
| --- | --- |
| `kv_edit_dry_run_typed()` | 生成补丁报告并验证 |
| `kv_edit_apply_to_memory_typed()` / GUI“应用” | 更新内存工作副本和预览 |
| `kv_edit_apply_typed()` | 直接事务写盘 |
| `kv_edit_commit_typed()` / GUI“保存” | 提交已验证工作副本 |
| `kv_edit_reset_memory()` / GUI“撤销” | 丢弃内存覆盖，恢复磁盘基线 |
| GUI“重新加载” | 确认未保存更改后重新读取磁盘 |

`sourceHash` 标识工作副本；`expectedSourceHash` 在多次 Apply/Delete 期间保持为磁盘并发基线。应用或保存前完整重解析，校验每个目标值、非目标元素及最终变量绑定；合法编辑可改变最终 `distance`。

#### 距离规划与源码补丁

里程移动和新建共用解析器的边界规划，按物理文件、Include 调用实例、距离段和目标里程分组，保留语句顺序、注释与空距离块。规划覆盖隐式初始块、锚点间隙、末块和 EOF；明确末段可沿递增或递减方向扩展，移动须源于该段，新建优先已有唯一位置。候选枚举与 token 查找共用规划，并包含转折段的末尾邻接间隙。

每文件/Include 实例的入口、退出环境记录末尾赋值与变量写入，随编辑元数据重建。环境检查为可恢复问题生成 `evaluation_Environment_Requires_Boundary` 候选，无可行位置时阻断；`ambiguous_Source_Section` 等原因随报告返回。候选筛选完成后，选定方案接受整图语义验证。

GUI 的首次处理和缓存复用共用动作判断，优先处理阻断错误；失败重试以工作副本 hash、结构化更改及整批人工选择为键。距离赋值须为有限值，允许有限负距离。未修改的对象键表达式、注释和换行按原字节保留。Section 的 `values.N` 须指向已有参数，调整长度时显式提供 `values.count`。

#### 元素与资源列表

- **方法转换**：常规编辑保留方法与参数形状。Inspector 坐标偏移按钮显式切换 `Put`/`Put0`、`Begin`/`Begin0`；丢弃非零偏移、短式 `Signal.Put` 转换及 Repeater 修剪按对应流程确认。
- **Legacy.Fog**：`legacyFog.change` 支持更新、删除和插入，字段为 `distance/start/end/red/green/blue`。五个语句参数均为必填有限数，允许负值、等值、反向区间及标度外的有限 RGB；未改表达式原样保留。基线与修改共用语义写入函数，Include 替换按子树排除规则保护非目标值，刷新覆盖表格、场景雾与标记。
- **资源列表行**：Station、Structure、Signal、Sound、Sound3D 共用行内草稿。右键可在上下新增行；Structure、Sound/Sound3D、Station 分别使用 2、3、13 个 CSV 字段。Signal 主行初始为 6 字段，后续保留调整后的宽度；主行/glare 成对插入，glare 由用户显式新增。
- **Signal 列**：保留物理行尾空字段。`KvSignalAspectRow::metadata.reserved` 记录主行结构字段数，`structure_keys` 余项属于 glare。形状编辑提供 `mainStructureKeyCount`、`glareStructureKeyCount` 及完整编号字段，单元格编辑保留原形状；每条已有物理行至少保留一个结构字段，允许全空。主行/glare 分界参与语义和脏状态判断；509 个结构列的显示上限之外，列操作仍使用实际完整宽度。
- **Repeater 关联与改名**：各层共用 `repeater_linkage` 和过渡关联。生命周期为半开区间 `[第一个 Begin, End)`，按距离、全局顺序和源行排序，同里程末事件决定活动状态；空区间保留源码身份。改名批次须包含整链 Begin/Begin0 与显式 End，并通过重名区间及链归属检查；重解析校验非目标边界，零长度新段按 Begin、End 顺序生成。
- **Repeater 结构键**：水合、Inspector 和场景通过 `repeater_structure_keys()`、`set_repeater_structure_keys()` 共用 `_structureKeys.count`、`_structureKeys.N`；`structureKeys` 拼接文本用于显示。模型数组保留缺失项位置，以原列表长度计算 `k % N`；端点模型缺失时，相机使用放置几何锚点。
- **他轨道改名**：一个 typed batch 须包含根地图和全部 Include 中同键的所有 `Track[trackKey].*` 语句。键比较保留数值/字符串类型并忽略大小写；批次完整且新键全图唯一时才接受改名，依赖该键的其他元素按非目标行校验。

### UI、表格与渲染

- 保持 Dear ImGui docking 布局、菜单和工具概念。普通 UI 文本同步简体中文、台湾繁体中文、香港繁体中文、英语和日语；语言切换时保持 ImGui ID 稳定。
- BVE 参数标签使用官方英文名或缩写，如 `distance`、`trackKey`、`x`、`ry`；程序诊断正文和 headless 输出使用英语，控制台周边 UI 使用五语。
- 保持二维平移/缩放/旋转/适配、测量、网格、车站跳转、背景对齐，以及三维相机传递、拾取/高亮、可见性、标记、线路信息和操纵器联动。
- 表格按 revision 缓存，保留 Section 动态参数和显式 `null`、变量顺序及跨视图导航。Repeater 查找使用有序类型化结构键；查找未使用结构前，将活动 Signal 主行/glare 单元格提交到草稿。
- Assimp 隔离在 `model_loader.dll`，加载错误经诊断和清理路径返回。模型包围范围以 double 计算，非有限位置或超出公开 float 范围的半径被拒绝；动态场景刷新失败时恢复 Repeater chunks 及缓存总数。

#### 草稿与视图状态

`App` 为待保存的既有行 update/delete 保留磁盘原始行，完整和局部水合均沿用此基线。Apply 失败恢复先前状态，成功后移除已退出账本的基线，Save/Revert 清理完成的账本。Inspector 按字段记录差异，字段恢复原值时仅移除该项更改。他轨道按精确键保留可见性、颜色和显示范围，显式改名通过稳定 ID 转移状态。

#### 设置与作者消息

应用偏好、最近地图/背景及作者消息偏好、ImGui 布局分别存入 `settings/settings.ini`、`settings/history.ini`、`settings/imgui.ini`。加载器接受保存端的精确节、键和值语法，未知或无效项采用默认值；已有文件由显式保存操作重写。设置和布局在完整写入并关闭成功后标记为已保存，失败后由 `App::service_pending_persistence` 按各自一秒期限重试，窗口遮挡或空闲时也适用。布局脏状态由 ImGui 请求标志持有。

作者消息来自独立的 `//--kme--message-from-creator:` 注释，保留物理位置与 Include 身份，按物理消息去重后以 `KvCreatorMessageRow` 提供给 Preview/Edit。`creator.message` 编辑接收原文 `content`，文件头插入使用共享补丁、重解析和提交路径。`creator_messages.cpp` 管理草稿、表格及文档打开时的弹窗；历史使用 `[CreatorMessages] count` 和 `[CreatorMessageN] path/has_messages/suppressed`，按主 Map 保留显示偏好，包括消息暂时为空时的抑制设置。

#### 曲线标记

`CurveGauge`、`CurveCenter`、`CurveFunction` 标记携带行索引和 edit ID。2D 绘制白色 `CG`/`CC`/`CF` 矩形，3D 绘制代码与求值参数组成的白色双行标牌，共用拾取和蓝色高亮。`[View2D]` 的 `show_curve_gauge_markers`、`show_curve_center_markers`、`show_curve_function_markers` 默认关闭，各自控制标记可见性。

`Curve.Interpolate` 保留 0/1/2 参数形状及编辑身份。水合按源文件索引和全局语句顺序关联求值事件，支持排序后的事件和同里程重复 Include。Edit 元数据合并后，2D 端点与 `CurveCircularStart` 外观的 3D 标牌使用同一来源行，提供属性/编辑和延迟删除，每条语句对应一个标记；标牌显示求值半径/超高，零半径显示 `Intpl. 0`。

Preview 阶段，平面悬停按标记数组索引命中，源码选择和编辑等待来源绑定；3D 保留 `curve` 分类和右键菜单，编辑/删除项在元数据就绪后启用。新建模板显示完整签名 `Curve.Interpolate(radius, cant);`，通过可选尾参数生成三种参数形式。

### 性能

- 按输入和 revision 复用读取、解码、哈希、路径解析、快照、表格、标记和几何结果。
- 大数组尽量连续，控制紧密循环及逐帧分配，避免大型轨道数组上的 O(n²) 遍历。
- 长时间解析和模型加载使用异步流程并提供进度；每个缓存明确完整 key、失效所有者和修订，覆盖命中与失效路径。

`refresh_local_preview_after_edits()` 合并 Apply/回滚刷新：完整水合覆盖局部轨道/列表转换，完整场景重建覆盖旧场景更新；单实例和 Repeater 坐标编辑使用稳定 ID 快速路径。距离与物理锚点索引在首次使用时构建并由同批次复用。

源码补丁排序及重叠检查后，按顺序追加原文和替换片段，由最终位置计算身份偏移。报告保留降序编辑顺序及前后各 80 字节预览上下文，包括右侧已应用修改。Save 将编码字节移动到事务请求，保留长度、哈希、写入核验及回滚信息。

Canvas3D 在替换场景时建立放置轨道索引，支持自轨道别名、规范化后首个他轨道匹配和缺失键回退。Repeater 按 `begin + k * interval` 放置，每个 Begin 重新起算，并按原结构列表的 `k % N` 选择模型。双精度变换缓存在当前窗口的 chunk 中，共享上限为 65,536 个；超预算部分按需计算。编辑使新旧 chunk 范围失效，场景/chunk 替换重置缓存，离开窗口时释放缓存；放置几何独立于轨道可见性。

背景图仅修改几何且请求亮度与已上传值一致时复用纹理；更换图片使纹理失效，上传成功后更新亮度记录。

编辑计时使用 `operation_timing.h` 的稳态时钟，GUI 与 maploader 分域记录包含式 `*_ms`、`*_count`，GUI 总用时包含 DLL 用时。App 计时持续到延迟 Inspector 处理及必要的首个场景帧完成；隐藏或折叠的场景直接结束等待。`source.read_decode_hash` 记录调用线程的源码处理，并行 Include 工作计入 `parse.include_join_merge` 和 `parse.syntax_diagnostics`，模型加载使用独立异步日志。

## 验证

按受影响组件选择检查，分别记录构建、契约测试、headless 和人工界面验收结果。磁盘写回须保存后重载比较；渲染、菜单、对话框和拖动等视觉交互另行验收。

`komapedit.exe` 使用 GUI 子系统。在 PowerShell 中捕获输出时，使用 `Start-Process -Wait -WindowStyle Hidden -PassThru` 并传入 `--headless-output`。验证命令应显式提供输入路径；部分自轨道、他轨道、距离、Repeater 和 Section 模式的省略路径会回退到开发者本机线路。

### 性能基准

前后对比使用相同线路、参数、构建类型与加载配置。scene、table-cache 和 edit 基准各顺序运行三个独立进程，期间暂停构建及其他基准；持续帧验收关闭 profiling。

#### 场景与加载器

`--debug-headless-scene3d-bench` 默认测量 300 个固定相机帧，采样间距 25 m，后方/前方窗口 100 m/1200 m，CPU 帧耗时 p95 上限 16.67 ms。`--interaction moving` 每帧前进 2 m，使用正常末端钳制。计时在模型加载完成及五个预热帧之后开始，报告适配器、负载、最慢帧和可见实例指纹。

`--profile-stages` 记录嵌套 CPU 阶段和异步 GPU 时间戳。查询提前分配、跨帧读取，仅统计有效且就绪的样本；GPU 区间包含命令间隙，CPU 父阶段包含子阶段。分段数据用于定位瓶颈，验收依据为整帧 CPU p95。

计时后，场景契约对比直接计算与缓存路径的双精度实例指纹、像素和拾取，覆盖相机移动/旋转、chunk 跳转/返回、窗口、雾及轨道辅助线变化。`--debug-headless-scene-loader-contract` 使用临时模型和 headless D3D 验证：

- Repeater 距离/模型序列、类型化列表水合、Include 顺序、缺失项、零长度段、几何跳转锚点、tilt/span、无效间距、编辑/恢复失效、轨道替换及缓存预算。
- 模型复制与 PutBetween worker 故障、取消、请求协调、DLL 分配/释放平衡，以及同路径模型复用与重载。
- 模型包围范围的有限性、可表示范围及失败清理，动态刷新失败后的 Repeater 缓存恢复。
- 背景图的纹理身份和像素：无变化、仅几何变化、亮度变化、图片替换、上传失败/重试；1024×1024 图片基准输出几何 Apply 的 median/p95 和纹理替换次数。

#### 平面与表格

`--debug-headless-plan-bench` 默认 `--interaction pan`，比较缓存与直接计算的曲线、缓和曲线和 Interpolate 端点。测量模式将命中结果与穷举对照：小点集线性扫描，大点集使用空间网格；`measure-stationary` 固定指针，`measure-moving` 使用确定轨迹。

plan、scene 和 own-track-edit 共用临时 Map/Include 契约，覆盖 Interpolate 的 0/1/2 参数、Apply/Delete/Reset、源码身份及唯一可编辑标记。真实线路按实际存在的目标验证；空的行、事件和标记集合须一致，依赖实例的检查记为不适用。own-track-edit 还检查 Preview→Edit 合并，包括通过 `rand()` 选择源码文件的地图。

`--debug-headless-table-cache-bench` 使用 Edit 元数据及关闭 INI 持久化的 ImGui/ImPlot context。预热后分别测量冷缓存重建、热缓存单次命中和全部表格的一帧绘制，核对行、单元格、身份、动态标题/宽度的指纹。源码字节/哈希检查位于计时区间外。默认重复 5 次（范围 1–100），采样间距 25 m。

`--debug-headless-diagnostics-popup-bench` 使用 100,000 条混合日志检查并发顺序、快照修订缓存和裁剪渲染。

#### 编辑

`--debug-headless-edit-bench` 调用正式 Apply、延迟 Delete、Save、Revert 和刷新路径。输入地图须包含可编辑的 `Structure.Put`、`Repeater.Begin`、`Curve.SetGauge`；按源码顺序选择首个有效目标，以固定坐标/轨距增量编辑。输入线路用于内存操作和字节/哈希核对；Save 使用保留相对依赖布局的独占临时普通文件副本，并检查路径边界与重解析点，结束后清理。

默认 `--scene off`、`--repeat 5`（1–100）、`--unit-distance 25`。启用场景时使用 1260×680 画布、后方/前方 100 m/1200 m 窗口，相机置于线路最小里程 + 500 m 并按正常 API 钳制。每轮恢复副本并新建 App，待模型初始加载完成后计时，计入必要的首个场景帧。报告目标身份、嵌套阶段、总体 median/p95/最大值、源码检查和刷新/回滚契约；每种 3D 状态各运行三个进程、每进程五轮。

### 命令入口

```bat
build\komapedit.exe --headless-load-map <map-path> --headless-output build\headless-load-map.txt
build\komapedit.exe --headless-load-scenario <scenario-path> [--scenario-index N] [--expect-no-map] [--scenario-edit-roundtrip] --headless-output build\headless-load-scenario.txt
build\komapedit.exe --debug-headless-scenario-lifecycle <scenario-path> [--scenario-index N] [--unit-distance M] --headless-output build\scenario-lifecycle.txt
build\komapedit.exe --debug-headless-plan-bench <map-path> --interaction pan|measure-stationary|measure-moving --headless-output build\headless-plan-bench.txt
build\komapedit.exe --debug-headless-open-bench <map-path> --repeat 3 --headless-output build\headless-open-bench.txt
build\komapedit.exe --debug-headless-table-cache-bench <map-path> --repeat 5 --unit-distance 25 --headless-output build\table-cache-bench.txt
build\komapedit.exe --debug-headless-scene3d-bench <map-path> --window-back-m 100 --window-forward-m 1200 [--interaction stationary|moving] [--profile-stages] --headless-output build\headless-scene3d-bench.txt
build\komapedit.exe --debug-headless-scene-loader-contract --headless-output build\scene-loader-contract.txt
build\komapedit.exe --debug-headless-diagnostics-popup-bench --headless-output build\diagnostics-popup-bench.txt
build\komapedit.exe --debug-headless-scene-camera-transfer <map-path> --headless-output build\scene-camera-transfer.txt
build\komapedit.exe --debug-headless-source-anchors <map-path> --headless-output build\source-anchors.txt
build\komapedit.exe --debug-headless-station-list-edit <map-path> --headless-output build\station-list-edit.txt
build\komapedit.exe --debug-headless-station-put-margin-edit <map-path> --headless-output build\station-put-margin-edit.txt
build\komapedit.exe --debug-headless-edit-roundtrip <map-path> --headless-output build\edit-roundtrip.txt
build\komapedit.exe --debug-headless-edit-bench <map-path> --scene off|on --repeat 5 --unit-distance 25 --headless-output build\edit-bench.txt
build\komapedit.exe --debug-headless-own-track-edit [map-path] --headless-output build\own-track-edit.txt
build\komapedit.exe --debug-headless-other-track-edit [map-path] [--commit] --headless-output build\other-track-edit.txt
build\komapedit.exe --debug-headless-distance-edit-batch [map-path] --headless-output build\distance-edit-batch.txt
build\komapedit.exe --debug-headless-auto-insert-diagnostics <fixture-directory> [--unit-distance M] --headless-output build\auto-insert-diagnostics.txt
build\komapedit.exe --debug-headless-repeater-edit-batch [map-path] --headless-output build\repeater-edit-batch.txt
build\komapedit.exe --debug-headless-repeater-key-edit <map-path> [--commit] --headless-output build\repeater-key-edit.txt
build\komapedit.exe --debug-headless-other-track-key-edit <map-path> [--commit] --headless-output build\other-track-key-edit.txt
build\komapedit.exe --debug-headless-insert-edit [map-path] --repeater-only [--commit] --headless-output build\repeater-insert-edit.txt
build\komapedit.exe --debug-headless-include-delete <map-path> [--index N] [--commit] --headless-output build\include-delete.txt
build\komapedit.exe --debug-headless-include-replace <map-path> --new-path <file> [--index N] [--commit] --headless-output build\include-replace.txt
build\komapedit.exe --debug-headless-resource-list-replace <map-path> --headless-output build\resource-list-replace.txt
build\komapedit.exe --debug-headless-resource-list-insert <map-path> --kind structure|signal --headless-output build\resource-list-insert.txt
build\komapedit.exe --debug-headless-signal-aspect-columns <map-or-scenario-path> --headless-output build\signal-aspect-columns.txt
build\komapedit.exe --debug-headless-new-file-wizard <tests目录下尚不存在的地图路径> --headless-output build\new-file-wizard.txt
build\komapedit.exe --debug-headless-scenario-create <tests目录下尚不存在的Scenario路径> --route <已存在的地图路径> --headless-output build\scenario-create.txt
build\komapedit.exe --debug-headless-fresh-resource-list-workflow <地图路径> --headless-output build\fresh-resource-list.txt
build\komapedit.exe --debug-headless-include-import-create <map-path> --headless-output build\include-import-create.txt
build\komapedit.exe --debug-headless-new-element-edit <map-path> [--commit] --headless-output build\new-element-edit.txt
build\komapedit.exe --debug-headless-light-edit <map-path> --headless-output build\headless-light-edit.txt
build\komapedit.exe --debug-headless-pretrain-edit <map-path> --headless-output build\headless-pretrain-edit.txt
build\komapedit.exe --debug-headless-legacy-fog-edit <map-path> --unit-distance 25 --headless-output build\headless-legacy-fog-edit.txt
build\komapedit.exe --debug-headless-sparse-new-element <map-path> --headless-output build\sparse-new-element.txt
build\komapedit.exe --debug-headless-section-edit-batch [map-path] [--commit] --headless-output build\section-edit-batch.txt
build\komapedit.exe --debug-headless-table-find --headless-output build\headless-table-find.txt
build\komapedit.exe --debug-headless-touch-input --headless-output build\headless-touch-input.txt
build\komapedit.exe --debug-headless-settings-persistence --headless-output build\settings-persistence.txt
build\komapedit.exe --debug-headless-curve-parameter-edit <map-path> --headless-output build\curve-parameter-edit.txt
build\bin\typed_snapshot_tests.exe signal-glare <map-path> [--commit]
```

### 资源列表与文件工作流

| 命令 | 输入与写入范围 | 主要检查 |
| --- | --- | --- |
| `--debug-headless-resource-list-replace` | 地图路径；打开 Win32 选择框，需手动选择另一份有效 Structure List；内存 Apply | Preview/Edit 元数据合并、稳定身份、完整重解析、列表缓存和路径刷新、重载后源哈希。取消、同文件或无效列表返回 FAIL |
| `--debug-headless-resource-list-insert` | 地图路径及 `--kind structure` 或 `signal`；内存 Apply，拒绝 `--commit` | structure 要求列表经 Include 加载，检查 2 字段行与上下顺序；signal 检查主行/glare 插入块、6 字段主行及显式新增 glare；均检查 Reset 和源哈希 |
| `--debug-headless-signal-aspect-columns` | Map/Scenario；输入线路仅内存编辑，Save/reload 使用临时夹具；拒绝 `--commit` | 正式列操作、确认/取消、主行/glare 独立宽度、多轮 Apply/Revert 和显示上限 |
| `--debug-headless-new-file-wizard` | `tests/` 下尚不存在的地图路径；创建文件后清理 | 空地图重载、排他创建、复用已有列表、五类 Load 暂存/保存、空列表重载 |
| `--debug-headless-scenario-create` | `tests/` 下尚不存在的 Scenario 路径，`--route` 指向已有地图；结束后删除新 Scenario | 官方键序、重复创建拒绝、字段存在位、相对路径/默认权重、Scenario 预览与异步地图加载、历史保持 |
| `--debug-headless-scenario-lifecycle` | Scenario；输入源码只读，写入使用独占临时目录 | 历史入口、元数据发布、Reload 视图恢复、单/多 Route 选择、新建/加载、先 Map 后 Scenario 保存、设置/布局重试、CSV 导出及文件失败处理 |
| `--debug-headless-fresh-resource-list-workflow` | 地图路径；只读输入并在临时目录创建 Map、Structure、Station 和 Signal 夹具 | 无距离地图的 Load 目标、多个未保存 Load 与空列表首行同批 Apply；Signal 多轮 Apply、glare 删除/重加、Revert、相邻行 Save/reload。检查 `input_map_bytes_unchanged`、`fixture_files_cleaned` |
| `--debug-headless-table-find` | 内置夹具 | 普通 Sound 的车站草稿引用与 Sound3D 区分；含逗号/空格的 Repeater 键；查找前提交活动 Signal 主行/glare 单元格 |

### 元素编辑与插入

`--debug-headless-new-element-edit` 驱动正式向导、Inspector 和延迟删除/取消流程，覆盖资源、Repeater、Structure、他轨道及组合 Curve/Gradient 模板，检查起止里程、缓和/cant 联动、源码顺序、目标文件和后续编辑。资源列表 key 仅预填匹配模板的字段。

此命令默认在 Reset/Reload 后核对源哈希；`--commit` 经 Save 向选定源文件写入成对曲线和坡度，并保留修改供物理 diff 检查。混合账本用例需要可编辑 `Structure.Put` 及他轨道位置/X/Y 插值参数行，检查插入顺序、多轮 x/y 修改、单字段恢复、失败 Apply/Revert 和视图状态；相关 Save/fresh Reload 使用四组临时夹具。

`curve.interpolate` 模板使用可选尾参数、共享类型化校验和语义指纹，接受官方 0/1/2 参数形式，拒绝仅含 cant、非有限值、未知字段和不支持的方法。`typed_edit_contract` 在 Shift-JIS/CRLF Include 夹具执行 dry-run、Apply/Reset/Commit/Reload，检查表达式、注释、顺序与身份；新建元素 headless 验证默认双参数、复选框联动、后续编辑/取消和唯一来源标记。

下列命令使用显式输入路径，通过内存编辑验证正式工作流，拒绝 `--commit`：

| 命令 | 输入条件 | 检查内容 |
| --- | --- | --- |
| `--debug-headless-light-edit` | 含任意有效 Light 声明子集的地图，如 `tests\light_valid.txt` | 元数据合并、三组表单 Apply、延迟删除、里程 0 的三类向导新建、求值和源码形式，Revert 到原子集并核对字节 |
| `--debug-headless-pretrain-edit` | 含既有 `PreTrain.Pass` 的地图 | `headless_pretrain.cpp` 验证时间/秒数多轮 Apply、里程、非法输入、删除、向导、插入后编辑/取消、Revert，以及 2D 身份/标签和 3D 标记数据 |
| `--debug-headless-legacy-fog-edit` | 旧式雾行数量不限的地图 | `legacy_fog_edit_validation.cpp` 验证身份、非法输入、多轮 Apply、删除、向导和 Revert；WARP 场景验证雾刷新，另检查表格/平面缓存和完整重建安排 |
| `--debug-headless-curve-parameter-edit` | 同时含 SetGauge、SetCenter、SetFunction 的地图 | CG/CC/CF 身份、白色标牌、独立可见性、Inspector 里程/参数、`SetFunction(2)` 拒绝、删除/新建、Revert 和源字节 |
| `--debug-headless-station-put-margin-edit` | 有可编辑 `Station.Put` 的地图 | 选择已有可编辑放置并报告其里程；检查零值/错误符号容差拒绝，向导默认 `margin1=-5`、`margin2=5`，合法插入与 Revert |
| `--debug-headless-sparse-new-element` | 目标源有零/一条数值距离语句，或锚点非递减且末值小于 866 | 正式 `DrawDistance.Change(500)` 向导；稀疏源用里程 25，单调尾部用 866，检查块复用/前插/尾插、原文、新行身份和值，Reset 后核对哈希 |
| `--debug-headless-auto-insert-diagnostics` | `testmap\auto_insert_failures` 夹具目录 | 按物理文件、行、类型、里程及身份选择目标，验证自动成功、人工恢复、硬拒绝、每个候选的实际 Apply、二次 Apply、重试终止和 Reset；成功条件为 `failed_cases=0`、`result=PASS` |

PreTrain 的 `passTime` 接受未加引号的 `hh:mm:ss` 或有限秒数，允许超过 24 小时及非递增时刻。PreTrain 与 Legacy.Fog 的编码、BOM/行尾、Include、磁盘并发及 Save/reload 由类型化契约的临时夹具覆盖；自动插入契约另覆盖混合 EOF 批次和过期选择。

### 关联编辑与 Include

下列模式的 `--commit` 会提交已验证工作副本，并保留线路修改供 diff 检查；默认运行内存验证及源哈希检查。

| 命令 | 输入条件与验证范围 |
| --- | --- |
| `--debug-headless-repeater-key-edit` | 显式地图路径；整链改名验证 |
| `--debug-headless-other-track-key-edit` | 显式地图路径；选择至少含两条语句的字符串键他轨道，检查整轨原子性、全图重名、依赖引用、Apply/Reset/Reload 和目标/文件哈希 |
| `--debug-headless-insert-edit --repeater-only` | 分别新建唯一 key 的 Begin 和 Begin0，执行 dry-run、Apply/Reset；提交时验证 Save/Reload |
| `--debug-headless-include-delete` | 显式地图路径，`--index` 默认 0；检查陈旧哈希拒绝、子树删除、非目标语义和 Reset；存续语句依赖被删子树时阻断 |
| `--debug-headless-include-replace` | 显式地图路径、`--new-path <file>`、`--index` 默认 0；路径文本按单引号参数写入，检查新旧子树排除后的整图语义、结构刷新、Reset/Reload；依赖旧变量或重复 Load 时阻断 |

`--debug-headless-include-import-create` 在显式输入地图的同目录创建唯一临时子地图，验证已有文件导入、新建文件、Include 插入、零距离锚点、重解析和结构刷新。子地图使用 UTF-8 无 BOM、CRLF 和 `BveTs Map 2.02:utf-8` 文件头；父地图仅内存 Apply/Reset，结束时核对原文件哈希并清理临时子地图。

### 作者消息与持久化

`--debug-headless-creator-message <map-or-scenario-path> [--scenario-index N] [--unit-distance M] --headless-output <report>` 只读加载输入并核对源文件字节，拒绝 `--commit`。独立临时地图和历史配置用于验证 Preview/Edit 元数据、向导、原文内容、未应用草稿阻断 Save、Apply/Revert/Save/Reload、Include 消息、打开时弹窗和抑制偏好；物理去重与源码展开顺序由 DLL 契约覆盖。

`--debug-headless-settings-persistence` 验证设置与历史的规范往返、无效项默认值和读取后的字节保持。作者消息的 `has_messages`、`suppressed` 以 `0`/`1` 保存，加载时仅精确值 `1` 解析为真。

### 人工验收范围

按改动选择地图/Include 加载与重载、平面/纵断面/半径图、车站跳转、测量、CSV 导出、模型预览与错误、三维对象/标记/相机/操纵器、Apply/Revert/Save/Reload、行内草稿、设置持久化及 Release 分发检查。界面验收重点包括命中与高亮、菜单、确认框、列滚动和最终渲染像素。

## 构建脚本、依赖与分发

- 构建配置以 CMake 为准，批处理脚本负责 Windows 构建流程。
- 保留 `NINJA_EXE`、`VCPKG_ROOT` 和 `x64-mingw-dynamic` 回退。
- EXE 与声明文件位于输出根目录，DLL 位于 `bin`，INI 位于 `settings`。
- 分发清理保留 `bin`、`settings`、`LICENSE`、`NOTICE` 与 `THIRD_PARTY_NOTICES.md`。
- 开发构建、Release 构建和发布清理共用目录布局检查，发现根目录旧 INI 或 DLL 时立即中止。
- ImGui 使用 docking 分支，ImPlot 使用上游版本。
- 保留许可证与声明文件；新增依赖须同步 CMake、开发者文档和第三方声明。
- 线路发布导出列于 `TODO.md`：计划展开 Include、可选常量化表达式、复制已用资源并输出报告，使用临时输出目录保护开发线路。
