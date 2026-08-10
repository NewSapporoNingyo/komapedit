# 开发者指南

[English](dev.md) · [用户指南](README_zhcn.md) · [路线图](../TODO.md) · [AI 辅助开发](ai-dev_zhcn.md)

本文档说明以人工方式开发 komapedit 时所需的环境、架构、规范与验证流程。如果变更中使用了 AI 编程工具，还必须遵守 [`ai-dev_zhcn.md`](ai-dev_zhcn.md)、[`AGENTS.md`](../AGENTS.md) 与 [`.agents/skills`](../.agents/skills) 中匹配的工作流。

## 范围与支持环境

komapedit 是使用 C++17 编写的 Windows 桌面应用，用于查看并逐步编辑 BVE Trainsim 地图。Windows、Win32、DirectX 11、WIC、Dear ImGui、ImPlot、CMake 和 Ninja 是当前支持的架构与工作流；不要将仓库视为通用跨平台 GUI 项目。

应用包含三个运行时组件：

- `maploader.dll`：解析地图和列表、处理 Include 与编码、生成自轨道和他轨道几何，并持有带版本的强类型地图/编辑快照。
- `model_loader.dll`：通过 Assimp 读取 Structure 网格、材质和纹理，并以 C ABI 提供数据。
- `komapedit.exe`：提供 Win32/DirectX 11 GUI、表格、二维图表、三维预览和编辑流程。

当前功能状态仅在 [`TODO.md`](../TODO.md) 中维护。不要根据计划中的 API 或未完成界面推断支持状态。

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

普通脚本默认关闭 `KOMAPEDIT_STRICT_WARNINGS`。诊断测试依赖被忽略的本地 `tests/` 固件；将干净检出中的失败归因于代码前，先确认这些固件存在。

运行时输出布局如下：

- Debug 使用 `build\`，Release 使用 `build_release\`。
- 输出根目录包含 `komapedit.exe`、`LICENSE`、`NOTICE` 和 `THIRD_PARTY_NOTICES.md`。
- `bin\` 包含 `maploader.dll`、`model_loader.dll`、Assimp 和复制的运行时 DLL。
- `settings\` 包含应用生成的 `settings.ini`、`history.ini` 和 `imgui.ini`。

不得提交构建目录、克隆的 `third_party` 源码树、设置文件、生成的 CSV、本地测试输出或临时线路/地图/模型固件。

## 源码分区

| 区域 | 主要文件与职责 |
| --- | --- |
| 地图公共 ABI | `include/maploader.h`、`include/maploader_snapshot.h`：API v6 函数、定宽 POD 快照、编辑批次、报告、跨度、所有权、版本与结构尺寸 |
| 地图生命周期 | `src/maploader/maploader.cpp`：C ABI 入口、句柄、重建、分发、源码读取与边界错误处理 |
| 地图状态 | `maploader_internal.h`：`MapContext`、解析行、源码跨度、Include 栈、编辑引用、报告与计时 |
| 解析 | `maploader_core.cpp`、`maploader_parser.cpp`、`text_decoder.cpp/.h`：语句、值、Include、变量、编码、源码锚点、唯一性检查 |
| 几何 | `maploader_geometry.cpp`：自/他轨道几何、重定位、曲线、坡度、放置缓冲区与场景控制点 |
| 身份与快照 | `maploader_identity.cpp`、`maploader_snapshot.cpp`、`maploader_semantic.cpp`：稳定 ID、强类型快照、修订、比较与指纹 |
| 编辑 | `maploader_edits.cpp`：试运行、内存应用、直接应用、提交、重置、源码补丁、编码感知写回与距离调整 |
| 共享关联 | `include/repeater_linkage.h`、`include/own_track_transition_linkage.h`：Repeater 链与曲线/坡度过渡配对 |
| 模型读取 | `src/model_loader/model_loader.cpp`、`include/model_loader.h`：Assimp 隔离与 model-loader API v2 |
| 主窗口 | `gui_kme.cpp`、`kme.h`：应用循环、菜单、加载、编辑检查器、应用/保存/撤销/重载与共享 GUI 状态 |
| 运行时/设置 | `app_settings.cpp/.h`、`runtime_paths.cpp/.h`、`maploader_runtime.cpp`：INI、相对可执行文件路径、DLL 加载、精确 API 检查 |
| 源码工具 | `file_structure_diagram.cpp`、`text_preview.cpp`：Include 图、工作副本预览、源码操作与距离边界选择 |
| Debug 验证 | `debug_headless.cpp/.h`、`touch_input.cpp/.h`：无界面契约、基准、相机传递、查找与触摸检查 |
| 二维视图 | `src/canvas2d/canvas2D.cpp`、`profile_plots.cpp`：平面、图表、变换、标记、测量与背景图 |
| 三维视图 | `src/canvas3d/canvas3D.cpp`、`include/canvas3D.h`：模型/场景渲染、相机、拾取、标记、叠加层与操纵器 |
| 表格/导航 | `src/table/datatable.cpp`、`table_navigation.cpp`：缓存表格、行内编辑、查找与行/平面/场景导航 |
| 共享标记 | `include/map_marker_visuals.h`、`map_marker_visuals.cpp`：二维/三维标记的唯一视觉配方 |
| 本地化 | `include/multilanguage.h`：简体中文、英语和日语界面文本 |

优先使用既有边界和共享帮助函数。不得在单个界面路径中重复源码所有权、关联、标记配方、导航、解析、验证或写回逻辑。

## 详细代码解说

本节是对 `include/` 与 `src/` 中项目自有源码的静态导读。说明按文件和连续职责块组织；函数名以当前源码为准，匿名命名空间中的小型帮助函数按共同用途归组。阅读时应同时注意调用方向：GUI 只通过 C ABI 使用 `maploader.dll` 与 `model_loader.dll`，解析器拥有源码身份，快照负责跨 ABI 投影，编辑器负责生成、验证和提交源码补丁。

### 公共 ABI、共享算法与资源头文件

#### `include/maploader.h`

- **导出与日志接口**：`KV_API` 控制 DLL 导出/导入；`KvLogCallback` 与 `kv_set_log_callback()` 把 DLL 内的英文诊断交给宿主。`kv_api_version()` 返回 ABI 版本，EXE 必须精确匹配。
- **句柄创建与几何生成**：`kv_load_map()` 是默认入口，`kv_load_map_ex()` 通过 `KV_LOAD_PREVIEW`、`KV_LOAD_EDIT_METADATA` 选择轻量预览或完整编辑元数据。`kv_generate_geometry()` 生成常规轨道数据，`kv_generate_scene_geometry()` 生成独立失效周期的场景几何；两者都在既有句柄上更新缓存和修订号。
- **只读快照**：`kv_get_map_snapshot()`、`kv_get_scene_geometry_snapshot()` 校验请求版本与结构尺寸后返回句柄拥有的视图。调用方不得释放嵌套指针，并须在重解析或对应几何失效前完成复制。
- **编辑与源码访问**：`kv_get_edit_target_typed()` 取得一个稳定 edit id 的字段和源码信息，`kv_get_source_text()` 返回当前磁盘或内存覆盖层中的解码文本。`kv_edit_dry_run_typed()`、`kv_edit_apply_to_memory_typed()`、`kv_edit_apply_typed()`、`kv_edit_commit_typed()`、`kv_edit_reset_memory()` 分别承担验证、应用到工作副本、直接写盘、提交工作副本和撤销覆盖层。
- **错误与释放**：`kv_get_last_error()` 返回线程局部错误文本；`kv_free()` 释放地图句柄，`kv_free_string()` 释放 DLL 分配的独立字符串。

#### `include/maploader_snapshot.h`

- **版本与通用视图块**：文件开头定义 API/快照版本、`KV_INDEX_NONE`、能力位和编辑标志。`KvUtf8View` 是调用期 UTF-8 输入，`KvStringRef`、`KvSpan` 和 `KvDoubleBuffer` 是指向快照 arena/数组的定宽视图。
- **值与源码身份块**：`KvValueKind`、`KvValue` 表示 null、数值和字符串；`KvSourceFile`、`KvSourceSpan`、`KvParsedStatement`、`KvRowMetadata` 保存物理文件、Include 栈、字节/行列范围、原始参数、解析顺序和稳定 edit id。
- **强类型行块**：`KvTrack`、`KvStation*`、`KvStructure*`、`KvRepeater*`、`KvSignal*`、`KvSection*`、`KvSound*`、`KvOtherTrain*`、曲线/坡度/他轨道变化以及限速、应答器、噪声、背景、粘着、亮度、雾、绘制距离等 POD，逐类固定字段形状。可变参数通过 `KvSpan` 指向共享值数组，避免在 ABI 中暴露 STL。
- **根快照**：`KvMapSnapshot` 汇总字符串 arena、通用值、各类行数组、几何矩阵、源码文件/语句/元素注册表、能力位及 content/geometry revision。`KvSceneGeometrySnapshot` 单独承载场景控制点与轨道矩阵，使场景重建不会错误延长常规快照寿命。
- **编辑协议块**：`KvEditField`、`KvEditTargetSnapshot` 描述可编辑字段；`KvEditOperation`、`KvEditChange`、`KvEditBatch` 表示插入/更新/删除请求；距离消歧、补丁预览、已提交文件/行和 `KvEditReportSnapshot` 共同描述试运行及提交结果。所有结构都有版本或 `structureSize`，新增字段必须同步 EXE/DLL。

#### `include/model_loader.h`

- `ML_API_VERSION` 固定 model-loader ABI v2。`MlVertex` 保存位置、法线与 UV；`MlMaterialData` 保存漫反射色和贴图路径；`MlMeshPart` 将索引范围绑定到材质；`MlMeshData` 汇总顶点、索引、材质、分段和包围盒/中心/半径。
- `ml_api_version()`、`ml_load_model()`、`ml_free_model()`、`ml_get_last_error()` 构成完整 C ABI。所有数组由 DLL 分配，成功或部分失败后的唯一释放路径都是 `ml_free_model()`。

#### `include/repeater_linkage.h`

- `EventKind` 区分 `Begin`、`Begin0`、`End`，`BoundaryKind` 区分显式 End 和后续 Begin 形成的变化边界；`Event` 保留距离、源码顺序、轨道/key 和源行索引。
- `canonical_key()` 统一 key 的大小写比较。`pair_linkage()` 先按来源顺序稳定排序，再按“轨道 + repeater key”维护活动链，生成 `Chain`、`Segment`、事件到链/段的反向索引；遇到新 Begin 时封闭旧段，遇到 End 时结束链。
- `pair_segments()` 是只需要扁平段列表的适配入口。地图快照、表格、二维和三维代码应共用这些结果，不能各自猜测 Begin/End 配对。

#### `include/own_track_transition_linkage.h`

- `EventKind` 表示曲线/坡度的 `BeginTransition` 及其可能的 Begin/End 消费者，`Pair` 保存过渡行与消费行的索引。
- `consumes_curve_transition()`、`consumes_gradient_transition()` 定义哪些语句可消费待配对过渡。`pair_transitions()` 按源码顺序维护曲线和坡度两套挂起状态，输出配对及 orphan 列表；编辑和标记路径据此把过渡操作绑定到消费语句。

#### `include/map_marker_visuals.h`

- `MapMarkerVisualKind` 是二维/三维共同的元素视觉枚举，`map_marker_visual_bit()` 将其映射为可见性位。
- `MapMarkerPrimitiveKind`、`MapMarkerColorRole`、`MapMarkerIconVariant` 定义图标原语、主题色角色和变体；`MapMarkerIconPrimitive`、`MapMarkerIconRecipe` 保存归一化点、线宽、闭合/填充和 glyph 信息。
- `map_marker_theme_color()`、`map_marker_role_color()`、`map_marker_icon_recipe()` 与 `draw_map_marker_icon()` 是颜色、配方和 ImDrawList 绘制的公共接口，保证 2D 与 3D 不维护两套符号语义。

#### `include/canvas3D.h`

- **场景输入模型**：`Canvas3DTrackPoint/Path/Visibility` 描述轨道采样与显示；`Canvas3DSceneObject`、`Canvas3DModelInstance`、`Canvas3DRepeaterSegment`、背景/雾/绘制距离事件构成场景实体输入。
- **线路信息与标记**：`Canvas3DSceneRouteValueEvent`、站点、限速、Section 信号事件用于相机里程采样；`Canvas3DSceneMarker` 保存视觉 kind、里程、轨道位置、表格目标和 edit id，`Canvas3DSceneMarkerVisibility` 以分类型位控制索引重建。
- **构建与刷新结构**：`Canvas3DScene` 是完整不可知渲染器的 CPU 描述；`Canvas3DSceneBuildOptions/Result`、`Canvas3DSceneMapRefreshOptions` 区分首次构建、动态内容刷新和地图内容刷新；`Canvas3DSceneStats` 暴露实例、模型和帧率统计。
- **交互结构**：相机姿态、上下文动作、拾取目标、`Canvas3DPlacementEditTarget`、拖动轴与 `Canvas3DPlacementDragUpdate` 将渲染交互转换为 GUI 可应用的源码字段更新。
- **`Canvas3D` 门面类**：模型预览方法负责加载/重载/清理单模型；场景方法负责 `load_scene()`、刷新、轨道/标记可见性、窗口距离和质量偏好；跳转、placement/repeater edit target、`render_scene_preview()` 与调试读取方法委托给 PImpl，避免在头文件泄露 D3D/Assimp 实现。

#### `include/multilanguage.h`

- `Language` 指定日、英、简中，`Translation` 的三个字段保存同一 UI 文本。文件主体按窗口、菜单、工具栏、表格、属性编辑、错误提示和 2D/3D 操作分组声明翻译常量。
- `tr()`/语言选择帮助代码在运行时返回当前语言字段。增加用户可见字符串时必须在同一个 `Translation` 初始化器中同时填写三种语言，并保持格式占位符一致。

#### `include/resource.h`

- 这是 Windows 资源编译器与 C++ 共用的最小资源编号头；include guard 防止重复包含，`IDI_KOMAPEDIT` 把 `komapedit.rc` 中的应用图标绑定到固定 ID。

### maploader 内部状态、解析与快照

#### `src/maploader/maploader_internal.h`

- **基础工具与计时**：字符串辅助声明、`SteadyClock`、`LoadTiming`、`ScopedTimer`、`ActiveTimingScope` 记录读取、解析、合并、几何和快照阶段；并发任务槽声明限制昂贵加载任务同时运行。
- **解码与解析选项**：`LoadedText` 同时保存原始字节、UTF-8 正文、编码、BOM、换行和行起点；`MapParseOptions` 决定预览/编辑元数据水位；`SourceTextOverride(s)` 是内存工作副本覆盖层。
- **表达式值**：`ValueKind`、`Value`、`VariableEnvironment` 表示解析期 null/number/string 和变量绑定；环境快照以共享只读映射挂到语句，供距离移动验证语义环境。
- **源码模型**：`SourceFileRecord`、`FileStructureRecord`、`SourceSpan`、`ParsedStatement`、`EditSourceRef`、`MapDiagnostic` 保存物理文件、Include 调用身份、原始语句、行列/字节锚点、解析顺序和编辑身份。
- **解析行记录**：从 `CurveEditRow`、`GradientEditRow`、`OtherTrackChange` 到 Station、Structure、Repeater、Signal、Section、Sound、Train、限速和环境效果的结构体，都是解析器到几何/快照/编辑器之间的内部强类型事实。各结构的 `EditSourceRef` 是回写依据，而不是 GUI 显示文本。
- **矩阵与快照存储**：`Matrix` 管理行列连续 double 缓冲；`MapSnapshotStorage`、`SceneGeometrySnapshotStorage`、`EditTargetSnapshotStorage`、`EditReportSnapshotStorage` 拥有 ABI 指针背后的 vector/string arena。
- **`MapContext` 聚合根**：持有主路径、源码/Include 表、变量环境、所有解析行、轨道与场景矩阵、控制点、revision、快照缓存、工作副本覆盖、磁盘基线 hash、最近编辑报告和计时。解析、几何、快照与编辑都以它作为唯一所有权根。
- **活动语句与编辑 RAII**：`ActiveStatementScope` 在 dispatch 期间设置当前语句/源码环境并自动恢复；`MapEditChange` 及各专用字段结构承载复制后的 ABI 请求；语义快照、距离消歧、补丁/提交报告结构支持事务验证。
- **跨实现文件声明区**：文件尾声明 parse、geometry、snapshot、semantic、edit、identity 等模块入口。它们只在 DLL 内部使用，避免把内部类型放入公共 ABI。

#### `src/maploader/text_decoder.h`

- 声明 UTF-8 与 `std::filesystem::path` 的双向转换、二进制读取、UTF-16/代码页解码、BOM/首行检测和编码感知写回。
- `FileOpenFailureKind` 区分不存在、权限、目录和一般打开失败，使解析器与模型加载器可生成一致诊断。写回函数接收目标编码与 BOM 信息，并在字符不可表示时失败。

#### `src/maploader/text_decoder.cpp`

- `classify_file_open_failure()`、`file_open_failure_message()` 和 `read_binary_file()` 负责可靠读取及 Windows 错误分类；`path_to_utf8()`、`utf8_to_wide()`、`wide_to_utf8()`、`path_from_utf8()`、`join_utf8_path()` 隔离 Win32 宽字符路径细节。
- `decode_codepage()` 在 Windows 使用严格/宽松代码页转换；非 Windows 分支给出受限回退。`append_utf8_codepoint()` 与 `decode_utf16()` 手工处理端序、代理对和非法序列。
- `decode_text_bytes()` 按声明编码/BOM 选择 UTF-8、UTF-16 或 CP932 路径；`first_line_ascii()` 和 `has_utf8_bom()` 支持在完整解码前识别地图头。
- `append_utf16_bytes()` 与 `encode_text_for_writeback()` 把 UTF-8 工作副本编码回原始 UTF-8/UTF-16/代码页，保留 BOM；无法表示的字符会抛错而不会静默替换。

#### `src/maploader/maploader_core.cpp`

- **任务、时间和标量帮助函数**：`try_acquire_maploader_task_slot()`/`release_maploader_task_slot()` 控制加载并发；`ActiveTimingScope` 记录总活动时间；`ascii_lower()`、trim、`parse_finite_number()`、`canonical_number()`、版本/编码头解析提供统一标量规则。
- **文本装载**：`build_line_starts()`、`detect_newline()`、`make_loaded_header_text()`、两个 `load_header_text()` 重载把原始字节变成带编码、换行和正文位置信息的 `LoadedText`，并优先读取内存覆盖。
- **Value/key 转换**：`as_number()`、`as_text()`、`key_text()`、`track_key_display_text()`、`track_key_from_display_text()` 和 CSV/INI 字段帮助函数统一解析器、表格语义和编辑文本表示。
- **源码注册与定位**：`normalized_source_path/key()`、`current_source_text()`、`register_source_file_index()`、Include 栈和 invocation key intern 函数去重源码身份；`line_column_for_body_pos()`、`make_source_span()` 把正文偏移转换为稳定物理锚点。
- **语句与环境登记**：`current_variable_environment_snapshot()`、`rebuild_variable_environment_snapshot()`、`add_parsed_statement()`、`next_active_edit_ref()` 建立语句、环境和 edit ref；merge/offset 函数把 Include 子上下文合入父上下文而保持索引正确。
- **列表行源码块**：`add_loaded_line_statement()`、`extend_loaded_line_statement()` 为 CSV/list 物理行建立可编辑语句；`parse_signal_aspect_source_values()` 和字段名帮助函数保留可变结构 key 列及 glare 行形状。
- **依赖与状态更新**：`value_equal()`、变量读写/距离使用记录、`log_load_timing()` 支持语义验证和性能日志；`add_controlpoint()`、`set_distance()`、`put_own()`、`ensure_othertrack()`、`put_other()` 是 parser dispatch 写入轨道事件状态的统一入口。

#### `src/maploader/maploader_parser.cpp`

- **词法/语句循环**：`Parser::parse()` 驱动整文件；`eof()`、`peek()`、`skip()`、`accept()`、`expect()` 处理空白、注释和标点；诊断函数记录位置并在 `finish_statement()`/`synchronize_statement()` 中恢复到下一条语句。
- **对象、函数与表达式**：`parse_label()`、`parse_variable_name()`、`parse_map_object()`、`parse_map_function()`、`parse_map_args()` 构造 `MapObject`/`MapFunction`；`parse_expression()`、`parse_prefix()`、`parse_primary()`、`apply_binary()`、`call_function()` 实现优先级、变量、字符串、数值和受支持数学函数。
- **Include 流程**：`include_path_is_simple_string()` 检查可预览路径；`make_child_seed()` 继承变量/距离与 Include 身份；`parse_include_context()` 可并行解析子文件；`queue_include()`、`flush_pending_includes()`、stale 检测和 merge 函数按原始顺序合并子上下文、源码表、诊断和事件。
- **语法验证与总分派**：`method_rules()` 是方法参数个数/空值规则表；`object_path()`、`validate_statement()` 形成一般语法门；`dispatch()` 再按顶层对象路由到专用函数。`record_deferred_semantics()` 记录需要等资源列表全部读完后才可验证的 key。
- **自轨道与他轨道**：`dispatch_curve()`、`dispatch_gradient()`、`dispatch_legacy()` 记录曲线、坡度和旧式事件；`dispatch_track()`、`setposition_interpolate()`、`track_position()` 记录他轨道位置、插值、轨距、中心和超高事件。
- **资源列表**：`load_resource_list()` 与 `record_resource_list_load()` 保存 Load 的原表达式、求值路径和源码身份；`parse_station_list()`、`parse_structure_list()`、`parse_signal_aspect_list()`、`parse_sound_list()` 把物理列表行转成强类型且可回写的记录；`parse_other_train_file()` 读取他列车文件。
- **地图元素分派**：`dispatch_station/speedlimit/section/signal/beacon/pretrain/structure/sound/train/repeater/irregularity/background/adhesion/cab_illuminance/fog/draw_distance()` 及三个 noise 分派函数，检查方法形状、读取参数并追加对应行；`add_other_train_definition()` 统一 Train 定义登记。
- **解析后诊断**：`validate_unique_preview_statements()` 拒绝歧义 Load/Enable；`append_transition_diagnostics()` 报告未配对过渡；`append_deferred_key_diagnostics()` 检查资源 key；`emit_diagnostics()` 输出并把错误升级为失败。
- **模块入口**：`parse_map_context()` 创建 `MapContext`、装载主文件、运行 Parser、刷新环境/诊断并返回完整上下文，是所有加载和编辑后重解析的共同入口。

#### `src/maploader/maploader_geometry.cpp`

- **轨道状态机**：`LastPos` 保存上一采样点，`TrackPointer` 按距离推进 own-track 事件并给出当前 radius、gradient、cant、方向与坐标；它是常规采样和事件边界采样的基础。
- **曲线数学**：`rotate_xy()`、Gauss 积分、Fresnel 级数/渐近式实现局部坐标积分；`circular_curve*()` 计算圆曲线，`halfsin_intermediate()`、`linear_transition_curve_local()`、`transition_curve*()` 计算半正弦/线性缓和曲线。key/hash 结构缓存重复参数的曲线结果。
- **坡度投影**：`constant_gradient_projection()`、`sinc()`、`gradient_transition()` 计算线路长度到平面投影和高程变化；`build_gradient_projection_samples()`、`build_event_projected_distances()` 让事件里程在纵坡变化时仍映射到正确平面位置。
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
- `MapSnapshotBuilder::build()` 按固定次序调用 `add_root()`、`add_tracks()`、`add_stations()`、`add_structures()`、`add_other_trains()`、`add_sections_signals_and_sounds()`、`add_environment()`、`add_preview_rows()` 与 `add_edit_registry()`。
- `string_ref()` 把文本追加到共享 arena；`value()`/`append_values()`/`append_strings()` 生成通用值和 span；`metadata()` 将 `EditSourceRef` 转成 `KvRowMetadata`。各 `add_*` 块逐行复制强类型字段，并只在能力位允许时附加完整源码信息。
- `add_element(s)()` 建立 row kind/edit id 到行索引的注册表；`bind()` 在所有 vector 完成扩容后绑定裸指针；`finalize()` 写入版本、结构尺寸、数量、revision 和 capability。
- `invalidate_map_snapshot()`、`invalidate_scene_geometry_snapshot()` 明确内容、常规几何和场景几何的失效边界。`build_map_snapshot()`、`build_scene_geometry_snapshot()` 延迟重建缓存；`ordered_station_list_entries()` 保持车站列表物理顺序。

#### `src/maploader/maploader_semantic.cpp`

- `SemanticWriter` 以确定字段顺序写入类型标记和值，并同时更新哈希；`field()` 重载、`value_span()`、`begin_element()`、`emit_element()` 生成可比较的规范语义，不依赖源码排版。
- `changed_field()`、数字/字符串/value/track-key 读取函数把某个 `MapEditChange` 叠加到快照原值上，并拒绝非法数字或缺少的必需值。
- `write_structure_model()`、`write_sound_list()`、`write_structure_put()`、`write_structure_between()`、`write_station_put/list()`、`write_signal_aspect/put()`、`write_repeater()` 以及 beacon、sound、noise、background、adhesion、fog 等 `write_*` 函数，逐类定义“编辑前后应相等/应改变”的语义字段集合。
- `write_curve()`、`write_gradient()`、`write_other_track_change()` 保留方法、参数个数和配对信息；`write_section_row()` 支持可变值列表；`reject_unknown_target_fields()` 防止 GUI 或调用方提交未声明字段。
- `build_semantic_map_snapshot()` 遍历所有受保护元素，生成 edit id 到语义的索引及整图/环境指纹。`expected_target_semantic()` 计算更新/删除目标的期望结果；`FakeInsertSnapshotState`、`insert_semantic_container()`、`expected_insert_semantic()` 为新建语句构造同样可验证的期望语义。

#### `src/maploader/maploader_edits.cpp`

- **ABI 输入复制**：`copy_utf8_view()` 和 `copy_edit_batch()` 校验结构尺寸、指针/长度、operation、flags、字段重复与 UTF-8 view 生命周期，把调用期 POD 复制为内部 `MapEditChange`。
- **补丁定位**：`load_source_patch()` 读取当前工作副本；UTF-8 偏移、`source_range_in_text()`、`safe_statement_removal_range()` 和 preview 函数把 `SourceSpan` 转为不会误删相邻注释/语句的文本范围。
- **距离表达式调整**：表达式扫描函数识别 predefined `distance`、顶层加减号和安全数值加数；`find_safe_numeric_distance_addend()`、`apply_delta_to_distance_addend()`、`adjust_distance_expression_by_delta()` 优先保留变量表达式，只在安全时修改常量项，否则生成距离消歧建议。
- **参数与 CSV 构造**：BVE 参数分割/引用、数值/optional/value/key 帮助函数保留未改 raw arg；CSV 解析、等价比较和 `build_editable_csv_list_statement()` 保持分隔、尾部字段与编码可写性。
- **逐类语句生成器**：`build_structure_model/sound_list/station_list/signal_aspect_statement()` 负责列表行；`build_station_put/structure_put/signal_put/repeater_statement()` 处理显式短式转换；其余 `build_*` 覆盖曲线、坡度、他轨道、Section、限速、应答器、声音/噪声和环境效果，维持原方法与参数形状。
- **目标发现与编辑目标快照**：模板化 `match_edit_ref()`、`find_simple_target()` 和 `find_editable_target()` 在 MapContext 强类型行中定位 edit id；`build_edit_target_snapshot()` 输出字段、原值、raw arg、约束、sourceHash 和 expectedSourceHash。
- **插入验证**：`validate_insert_field_names()`、`validate_insert_method()`、`validate_insert_change()` 限定向导支持的 row kind、方法和字段；`build_insert_statement()` 只生成普通 BVE 语句，不接受任意 replacement 文本。
- **距离块规划**：`DistanceSectionAnalysis/PlanningIndex` 建立同文件、Include invocation 和距离段索引；boundary 函数寻找可复用距离块、锚点后空隙或 EOF；变量引用与环境比较函数判断移动语句是否改变求值环境；`append_resolution_request()` 把无法自动决定的位置暴露给 GUI。
- **报告与事务写盘**：`build_edit_report_snapshot()` 投影补丁、消歧和提交信息；hash/临时文件函数创建同目录暂存文件；`replace_files_transactionally()` 按阶段替换并在失败时回滚，`TransactionalWriteError` 保留主错误与回滚错误。
- **完整语义验证**：`parse_report_candidate()` 用补丁覆盖重解析；`validate_non_target_derived_state()`、`own_track_transition_state()`、`validate_edit_report()` 比较非目标元素、变量/距离终态、车站所有权、过渡配对和每个目标的期望语义。
- **批次主流程**：`build_edit_report()` 预处理目标、按物理上下文和目标距离分组，解决 boundary，生成替换/删除/插入，检测重叠补丁，重解析并验证。它是 dry-run、内存 Apply 和直接 Apply 的共同核心。
- **工作副本与提交**：`apply_patched_files_to_overrides()`、`reparse_context_with_overrides()`、`apply_edit_report_to_memory()` 更新内存覆盖且保留磁盘基线；`reset_memory_edits()` 回到磁盘；`populate_committed_edit_state()` 记录落盘身份；`commit_memory_edits()` 重新检查并事务写入所有覆盖文件。

#### `src/maploader/maploader.cpp`

- `parse_options_from_load_flags()` 把公共 flags 转为内部 profile，并拒绝未知组合。
- 所有 `kv_*` 导出函数都是异常边界：先验证 handle、版本、结构尺寸和参数，再调用内部 parse/geometry/snapshot/edit 函数；捕获 C++ 异常后写入 last error 并返回空指针或 0，绝不让异常跨 ABI。
- `kv_load_map_ex()` 创建 `MapContext`；两个 geometry 入口更新矩阵和修订；两个 snapshot 入口返回缓存视图；edit target/source text 入口返回工作副本信息。
- dry-run 只构建报告，memory apply 构建并应用覆盖，reset 丢弃覆盖，direct apply 对报告直接事务写盘，commit 保存已验证工作副本。`kv_free()`/`kv_free_string()` 与 DLL 分配所有权成对。

#### `src/maploader/diagnostics.h` 与 `src/maploader/diagnostics.cpp`

- 头文件声明日志 callback、last-error 和 info/warn/error 门面。实现使用互斥量保护全局 callback，用 `thread_local` 保存每个调用线程的最后错误。
- `emit_log()` 在 callback 存在时转发完整行；`log_info()`、`log_warn()`、`log_error()` 添加统一级别前缀。ABI 捕获块通过 `set_last_error()` 更新可查询错误，避免混用日志与返回值。

#### `src/maploader/c_api.h` 与 `src/maploader/c_api.cpp`

- 这是 DLL 内部的 C 字符串所有权帮助层。`copy_c_string()` 使用 `new[]` 复制带 NUL 的 UTF-8 文本，分配结果只交给公共 ABI，最终由 `kv_free_string()` 的匹配 `delete[]` 路径释放。

### 模型加载

#### `src/model_loader/model_loader.cpp`

- 路径/扩展帮助函数规范化 Assimp 格式提示，`copy_c_string()` 为材质贴图路径分配 ABI 字符串，`resolved_texture_path()` 将模型相对贴图解析为 UTF-8 路径。
- `free_mesh()` 对顶点、索引、材质字符串和 mesh parts 做完整幂等释放；`MeshCleanupGuard` 确保 `load_with_assimp()` 中途抛错也不会泄漏；`assign_bounds()` 从顶点计算 min/max、中心与半径。
- `load_with_assimp()` 先用共享二进制读取支持 Unicode 路径，再调用 Assimp importer；随后合并各 aiMesh 顶点/法线/UV/索引，建立材质和分段，解析第一张漫反射贴图并计算包围盒。
- `ml_api_version()` 返回 v2；`ml_load_model()` 清零输出、捕获异常并填充 `MlMeshData`；`ml_free_model()` 是公共释放入口；`ml_get_last_error()` 返回线程局部诊断。

### 主窗口状态、加载与编辑工作流

#### `src/main_window/kme.h`

- **通用数学与哈希**：`KmeByteHash64` 为 GUI 缓存/调试提供确定性字节哈希；`Matrix` 是从 ABI 复制后的 GUI 自有二维 double 缓冲。
- **地图模型**：`TrackEvent`、`OwnTrackEditMarker`、`OtherTrack`、Station/SpeedLimit/Section 和大量 `TableRow` 集合组成 `MapModel`。`MapModel` 同时保存 source files/statements/elements、resource-list 元数据、revision、能力位和常规/场景矩阵，但不拥有 DLL 的嵌套指针。
- **二维数据**：`View2D` 保存平移、比例和旋转；`TrackPoint`、`PlanMarker` 及各别名、`PlanRepeaterSegment`、`OtherTrainPathOverlay`、`PlanData`、`ProfileData` 是画布缓存和 hit-test 输入。
- **表格与源码工具状态**：`TableRow/ColumnDef`、`CachedTableRow`、`TableUiCache` 保存按 revision 构建的显示数据；File Structure、Text Preview、DistanceResolution 结构保存布局、选择和解析器确认的边界。
- **设置与运行状态**：`TextureImage` 管理 D3D 背景纹理；日志、窗口可见性、2D/3D 视图、`UserSettings`、最近地图和背景历史结构对应 INI 持久化字段。
- **编辑状态机**：字段约束、inspector session、pending change、preview snapshot、Repeater draft、NewElement template/wizard、delete mode、distance workflow 和 editable-list draft 结构，明确区分尚未 Apply 的 UI 草稿、已 Apply 工作副本和已保存磁盘状态。
- **`App` 类**：声明窗口渲染、异步加载、快照转换、几何/场景重建、所有表格和导航、编辑/保存/重载、对话框、设置、背景图、2D/3D 交互及缓存成员。其成员布局是各 GUI `.cpp` 文件共享的应用状态契约。

#### `src/main_window/maploader_runtime.cpp`

- `KME_MAPLOADER_FUNCTIONS` 宏是唯一符号清单；`MaploaderRuntime` 构造时从 `runtime_paths::dll_path()` 加载 `maploader.dll`，解析全部函数指针并检查 `kv_api_version()==KV_MAPLOADER_API_VERSION`。失败信息包含 Win32 错误文本。
- 文件后半的每个全局 `kv_*` 函数是薄转发器：先 `ensure_loaded()`，再调用缓存函数指针；加载失败时设置可供 GUI 查询的错误并返回失败值。这样 GUI 源码仍可按公共头函数名调用，而链接时不静态依赖 DLL import library。

#### `src/main_window/runtime_paths.h` 与 `src/main_window/runtime_paths.cpp`

- `executable_directory()` 首次调用 `GetModuleFileNameW()` 并缓存 exe 目录；`dll_directory()` 固定为其 `bin` 子目录；`settings_directory()` 创建并返回 `settings` 子目录。
- `dll_path()` 拼装依赖 DLL 路径；`load_dll()` 使用受限搜索标志从 `bin` 加载并可回传 Win32 error code，防止依赖解析意外落到当前工作目录。

#### `src/main_window/app_settings.h` 与 `src/main_window/app_settings.cpp`

- 头文件只暴露默认 INI 路径、用户设置/历史读写、ImGui layout 延迟保存和运行时样式应用函数。
- 实现前段规范化存储路径、最近地图 key/显示名，clamp 字体、控件、marker、线宽、场景距离/操纵器/实例警告阈值；颜色函数负责 hex 序列化、palette、透明度和混色。
- 语言、bool、2D mode、grid mode 的 to/from string 函数形成稳定 INI 文本协议。`save_user_settings()` 按 section 写入全部偏好；`load_user_settings()` 逐键读取、兼容旧别名、clamp 非法值并返回默认回退。
- `load_imgui_layout()`、`save_imgui_layout()`、`save_imgui_layout_if_requested()` 和 pending flag 管理 `imgui.ini` 的显式/延迟持久化。
- history 块解析 `[RecentN]`，`load_history_entries()` 保持顺序并读取每地图背景变换，`save_history_entries()` 原子化重写最近项；数字格式函数避免区域设置污染。
- `apply_ui_font_size()`、`apply_ui_theme_color()`、`apply_ui_component_size()`、`apply_ui_settings()` 把持久设置投影到 ImGui style，并按 DPI/viewports 修正圆角和尺寸。

#### `src/main_window/gui_kme.cpp`

- **启动辅助与文本/数值格式**：字体 glyph 合并、日志分级/颜色、UTF-8/UTF-16、路径、数值截断与 inspector numeric constraint 函数为 Win32 UI 提供统一格式。距离跳转过滤和文件名清洗避免各控件重复实现。
- **ABI 快照复制**：`typed_snapshot_string()`、span 校验、matrix copy 和一组 `map_snapshot_*` 函数安全读取 arena；随后的转换块把每类 `Kv*` 行复制进 `MapModel`，绑定 Station position edit id，重建限速运行时缓存并用共享算法标注曲线/坡度过渡配对。
- **异步加载**：worker 结构和 `wake_main_window()` 协调后台线程；`stop_loader()`、`poll_loader()`、`begin_edit_metadata_load()`、`load_map_worker()` 分离 Preview 首屏和 Edit metadata 水合；`apply_load_result()`、`merge_edit_metadata()`、`apply_edit_metadata_result()` 在 UI 线程替换模型并使缓存失效。
- **几何和加载计时**：`finish_pending_load_timing()`、`regenerate_geometry()` 调用 DLL 重建指定范围/步长数据；状态栏阶段函数只在相应数据真正可用后结束计时。
- **编辑模式与本地预览**：pending/unsaved 查询函数区分 inspector 草稿、inline-list 草稿和工作副本；`set_edit_mode_enabled()`/`apply_edit_mode_enabled()` 管理元数据加载与退出确认；`snapshot_local_preview_row()` 和按 row kind 的更新块让 inspector/gizmo 在正式重解析前同步可视模型。
- **检查器打开与 Repeater 导航**：request/process/open 函数从 `kv_get_edit_target_typed()` 构建字段 UI；row kind 支持表决定删除能力；Repeater chain、Section values、structure keys 重建函数维护可变字段；Put0/Begin0/短 Signal 显式转换只在用户确认后启用。
- **三维操纵器联动**：`sync_scene_placement_edit_from_inspector()` 把当前字段投影到 Canvas3D target；`apply_scene_placement_drag_update()` 将普通放置的 X/Y/Z 拖动或 `Structure.PutBetween` 的仅 Z 轴整米 `distance` 拖动写回 inspector buffer，不直接写盘。
- **Apply 与删除**：`apply_inspector_changes()` 组装 typed edit batch、先 dry-run、处理距离消歧，再 memory Apply 并用新快照刷新模型；`delete_element_target()` 根据普通行、成对过渡或 Repeater 删除模式形成一个或多个物理 change。
- **行内资源列表编辑**：draft 判断、spec、字段校验、重排/清空/删除、文件选择和 find-result 重置代码维护 Station/Structure/Signal/Sound/Sound3D 物理行草稿；批量 Apply 前保持原 source hash 和未显示列。
- **typed 报告适配**：`TypedEditBatchStorage` 拥有调用期字符串与 view；报告字符串/span 函数复制 DLL 视图；committed file/row state 处理重解析后 identity 迁移。距离 resolution choice 函数驱动 Text Preview 选择并重试同一批次。
- **保存、撤销与关闭**：`save_pending_edits()` 阻止遗漏 inline draft 后调用 commit；`discard_pending_edits()` 清 UI 草稿；`revert_all_pending_edits()` 调用 reset memory；close/reload 状态机根据未保存状态弹窗且不混淆 Save 与 Reload。
- **Inspector 与新建向导 UI**：字段/Section 可变参数渲染、`render_element_inspector()`、`new_element_templates()`、候选文件/方法/字段表、`apply_new_element_insert()` 和 `render_new_element_wizard()` 仅暴露已支持普通 BVE map 元素。
- **文件、历史、背景与导出**：打开 map/image/folder 对话框使用 Win32 shell；recent map 与 background history 函数同步 `history.ini`；图片装载/纹理重建使用 WIC；CSV 导出写 own/other track 矩阵。
- **布局、菜单和设置**：dockspace 初始化、window visibility 与 2D/3D settings 投影函数保持持久状态；`render_menu()`、`render_toolbar()`、`render_status_bar()`、站点/里程跳转、console 和 popups 构成顶层 UI。
- **三维/模型预览协调**：`start_scene_preview()`、`rebuild_scene_preview()`、动态刷新、track/marker visibility 同步和 `render_scene_preview_window()` 连接 `MapModel` 与 `Canvas3D`；model preview 函数从结构列表打开单模型。
- **每帧与 Win32 入口**：reload/shortcut 函数处理磁盘动作；`App::render()` 调用各窗口；`CreateDeviceD3D()`/render-target 清理函数管理主 swap chain；`WndProc()` 转发 ImGui 与触摸消息；`main()` 初始化窗口、D3D、ImGui/ImPlot、设置和消息循环，并按逆序释放资源。

#### `src/main_window/file_structure_diagram.cpp`

- 布局帮助代码按 Include depth 分组节点，测量文本与节点尺寸并缓存连线、总范围和 revision；`file_structure_layout_is_current()` 避免每帧重算，`rebuild_file_structure_layout()` 只在源码结构或字体/尺寸变化时重建。
- `open_parent_directory_in_explorer()` 通过 ShellExecute 打开物理目录。`render_source_file_context_menu()` 是结构图和属性检查器共享的“打开目录/源码预览”动作。
- `App::render_file_structure_window()` 绘制可平移画布、层级连接线、主文件/Include 节点、hover tooltip 和右键菜单，并把选中节点交给工作副本 Text Preview。

#### `src/main_window/text_preview.cpp`

- `build_text_preview_lines()` 建立行起始字节索引；`decode_preview_bytes()` 是非 parser-confirmed 文件的只读解码回退。正常 map/list 预览优先走 `kv_get_source_text()`，因此可显示内存 Apply 后的工作副本。
- boundary range/gap/EOF 函数把 `DistanceResolutionBoundary` 按行定位；marker style/render 函数在源码行间显示解析器确认的插入点，`utf8_byte_for_source_column()` 将源码列转换到 ImGui 文本选择字节。
- `open_text_preview()`、`refresh_text_preview_from_working_copy()`、`refresh_text_preview_after_map_load()` 管理普通预览；`open_text_preview_for_distance_resolution()` 注入候选边界和目标语句定位。
- `render_text_preview_window()` 绘制行号、只读 UTF-8 文本、当前选择、源码定位提示及边界按钮；用户只能选择报告提供的 token，再由主编辑状态机重试，不能任意指定文本偏移。

#### `src/main_window/touch_input.h` 与 `src/main_window/touch_input.cpp`

- 头文件的 `TouchFrame` 汇总单帧 tap、long press、scroll 和 pinch（含 `PinchAxis`）；公共函数负责 Win32 消息接入、每帧状态、区域消费、popup 手势和测试注入。
- 实现中的 `ActiveTouch`、`PairState`、`TouchManager` 跟踪 pointer id、按下位置/时间、移动阈值、双指中心和缩放。消息处理识别 down/update/up/capture lost，长按与滚动互斥，pinch 将距离变化映射为轴向缩放。
- `new_frame()` 发布并清理瞬时事件，`consume_*` 防止一次手势被多个窗口使用，`apply_touch_scroll_to_hovered_window()` 映射 ImGui 滚动；`debug_*` 函数用可控时钟与合成触点验证状态机。

#### `src/main_window/map_marker_visuals.cpp`

- 原语构造函数 `append_polyline/polygon/circle/glyph/sampled_arc()` 将标准化几何写入 recipe；station、curve、gradient、speed-limit、beacon、pretrain、sound/noise、background、adhesion、cab light、fog 和 other-track 等 `*_recipe()` 定义唯一图标形状。
- `map_marker_theme_color()` 按 visual kind 返回主题色，`map_marker_role_color()` 将 fill/outline/accent/text 角色与主题混合；`map_marker_icon_recipe()` 选择 kind/variant 的配方。
- `draw_map_marker_icon()` 通过 `transform_icon_point()` 把归一化坐标缩放、旋转、平移到 2D 屏幕，逐原语调用 ImDrawList。3D 代码读取同一 recipe 再生成 billboard 顶点。

### 二维视图与图表

#### `src/canvas2d/canvas2D.cpp`

- **采样与边界**：matrix row/sample/lower-bound 函数在 own/other track 矩阵上按里程插值；`offset_track_point()` 在局部横向/前向坐标放置 marker。Repeater LOD、bounds 和 segment point 函数构建连续布景覆盖范围。
- **marker cache**：`populate_speed_limit_marker_cache()`、`rebuild_speed_limit_marker_overlay_cache()`、`rebuild_marker_overlay_cache()` 将各强类型表行按 edit id、source row、轨道位置转为统一 `PlanMarker`，并构建 Repeater 段、他列车路径和 visibility 索引。
- **查询与业务数据**：`nearest_own_index()`、`interp_own_z()`、`track_info_at()`、`speed_at()`、`curve_sections()` 为测量和叠加层采样；`build_plan_data()` 合并 own/other track、站点、限速与 marker，`current_plan_data()` 以 model/geometry/visibility revision 缓存；profile 数据有相同 build/current 分层。
- **视图操作**：measure clear/update、center/focus、模型坐标到 plan 点、plot focus 和 `jump_to_distance()` 统一所有导航入口，避免表格和场景直接修改 pan。
- **背景图**：`background_uv_from_world()`、`draw_background()` 应用位置、尺寸、旋转、亮度；`apply_background_alignment()` 从两个站点的 map 坐标与图片点计算比例、旋转和平移。
- **屏幕变换与裁剪**：`PlanScreenTransform` 完成 world/screen 双向变换；`ScreenPolylineBuilder` 做有限值检查与线段裁剪；polyline、range overlap、screen bounds 和 Repeater chunk 绘制函数避免在超长线路上提交不可见几何。
- **网格与公共绘制**：grid step、比例尺格式/绘制、三角/菱形/信号/先行列车/方向箭头/文字函数组成低层 ImDrawList primitive。
- **`render_plan_canvas()` 主流程**：处理 hover、鼠标/触摸 pan/zoom/旋转、双击 fit、测量和背景交互；计算可见里程后按层绘制背景、网格、轨道、Repeater、站点/限速/各类 marker、标签、当前 3D 位置和 focus；同时完成 hit-test、tooltip 与 context target 收集。
- **上下文动作**：`render_plan_marker_context_menu()` 根据 marker kind 提供定位表格、打开属性/编辑或删除；`plan_context_source_for()` 从 edit/source metadata 生成共享源码文件动作。

#### `src/canvas2d/profile_plots.cpp`

- 基础函数绘制 vector 曲线、标题/单位、遮盖坐标轴边缘、半径左右标志、底部锁定标签和 profile 垂直 marker；RAII 类临时覆盖 ImPlot fit button 与 wheel zoom 行为。
- 触摸缩放块把 `TouchFrame` 转换为 X 或 XY 轴 limits，并用 `preserved_plot_span()` 在空数据/重建时保持合理跨度。
- `render_profile_plot()` 绘制距离-高程、坡度填色/标签、站点、限速和可编辑曲线/坡度 marker，处理共享 focus、双击测量、hover/context。
- `render_radius_plot()` 绘制距离-曲线半径，分离左右曲线显示并复用 transition marker 关联规则。`render_plots()` 根据当前 2D mode 分配窗口区域并共用缓存的 `ProfileData`。

### 三维渲染与场景构建

#### `src/canvas3d/canvas3D.cpp`

- **数学、采样与场景转换**：`Vec3/DVec3/Vec4/Mat4` 及矩阵、投影、包围盒帮助函数构建相机和 world transform；轨道采样、key 规范化、Repeater 区间和 route event 采样函数把 `Canvas3DScene` 转为可渲染数据。
- **CPU/GPU 数据结构**：vertex、material、mesh part、texture cache、model、track/marker chunk、instance、highlight batch、pick target 和 placement lookup 结构明确 CPU 装载、GPU 资源、按里程 chunk 及反向定位所有权。
- **着色器块**：内嵌 HLSL 分别实现模型/轨道的实例化顶点与材质采样、marker billboard、整数颜色 pick、highlight mask 和 outline composite。常量缓冲对应 view、fog、draw distance、pick id 和 outline 参数。
- **`ModelLoaderClient`**：从 `bin/model_loader.dll` 动态解析 v2 API，`prepare()` 检查版本，`load()`/`free_model()` 保证跨 DLL 所有权成对。
- **`Canvas3D::Impl` 单模型预览**：`load_model()`、`upload_model()`、`reload_model()` 建立 vertex/index/material/texture GPU 资源；`ensure_pipeline()`、render-target、相机输入和 `render()` 绘制可旋转缩放的 Structure 预览；clear/release 函数按逆序释放 COM 资源。
- **场景生命周期**：`load_scene()` 替换 CPU scene 并可保留模型/相机；dynamic/map/station refresh 函数只更新必要内容；`clear_scene()`、`reload_scene_models()`、track visibility 和场景参数 setter 管理精确失效。
- **异步模型加载**：收集唯一模型请求，worker 通过 `ModelLoaderClient` 复制 CPU 数据，主线程 `upload_pending_scene_models()` 创建 D3D 资源；warning/log summary、wake callback、取消与清理避免 UI 阻塞及线程持有 COM 资源。
- **管线与资源缓存**：`ensure_scene_*_pipeline()` 分别创建主场景、marker、pick、outline、depth/blend/rasterizer 状态；texture cache 复用贴图；instance buffer 按需扩容。所有 release 函数与创建块一一对应。
- **场景分块**：`build_scene_chunks()` 按里程组织 Structure/Signal/Repeater 实例；track chunk 函数生成轨道带状三角形；marker recipe 函数把共享 2D 原语转换为 3D billboard 顶点、glyph 和 index range；visibility 变化只调用 `rebuild_scene_marker_visible_indices()`。
- **相机与放置坐标**：own/other track sampling、cant frame、`make_track_placement_frame()`、`make_track_world()`、Repeater instance world 函数把 BVE distance/x/y/z/yaw/pitch/roll 转为世界矩阵；camera reset/jump 保持线路朝向和目标中心。
- **可见性、绘制和拾取**：visible range/chunk 筛选后批量绘制 track、model、marker；pick pass 写入 object/marker id 并回读单像素；highlight mask/batch 与 outline composite 绘制 hover、表格跳转和选择轮廓。
- **placement/repeater 实时编辑**：设置 target 时查找源实例/段并建立 edit state；update 函数仅改对应 chunk 的 world 数据。gizmo projection、mouse ray、轴最近点和 drag handler 为普通放置生成毫米截断的 `Canvas3DPlacementDragUpdate`；`Structure.PutBetween` 只启用沿自轨前向的 Z 轴，并把拖动吸附为整米 `distance`。其顶点预览在线程中按最新目标合并重算，按模型纵向 slice 复用轨道采样，完成后通过可复用动态顶点缓冲原子替换。
- **雾、背景和线路信息**：按相机距离采样 BVE fog、Map DrawDistance、背景模型和有效场景窗口；route overlay 采样 radius/cant、gradient、活动限速、Section signal speed 与下一站，metrics/loading overlay 显示性能和加载状态。
- **`render_scene_preview()`**：每帧处理异步上传、相机输入、gizmo、可见实例收集、主 pass、pick/highlight、marker/object context popup，并返回导航、编辑、删除或 drag action。文件末 `Canvas3D::*` 公共方法都是到 Impl 的薄委托。

### 数据表格与跨视图导航

#### `src/table/datatable.cpp`

- **通用单元格与菜单**：文本 cell、tooltip、源码/文件/编辑 context action 帮助函数统一普通行、Repeater 双源码行和资源路径行行为；大小写折叠、query match 与状态格式函数实现共享查找。
- **查找面板**：`TableFindRowsView`、result reset/step/exact-query 函数维护匹配索引；布局帮助函数在窄窗口动态换行并保持按钮宽度，unused search 以引用 key 集合反查定义表。
- **行内编辑控件**：`EditableCellInteraction` 及输入、clear、browse、move、delete 渲染块写入 `EditableListDraftRow`，校验字段是否可表达于原编码/列形状，并在 Apply 前保持源行 ID 和未显示 signal aspect 列。
- **通用表格绘制**：固定表头、选择/高亮色、all/range flag 和缓存 row metadata 帮助函数，避免每个窗口重复 ImGui table 样板。
- **模型到缓存**：`ensure_table_cache()` 以 model content revision 建立 Station、Structure、Repeater、Signal、Section、Train、Sound、变量和效果表；`merged_repeater_rows()` 使用共享 linkage 把 Begin/End/变化边界组合成显示行；Section 可变参数和 Signal 可变 key 列保持动态形状。
- **语义标注**：track key 检查函数标记 3D 场景不存在/非法轨道引用；source path/range formatter 为双端 Repeater 和 Include 行生成显示与 tooltip；`refresh_speed_limit_table_cache()` 处理运行态有效限速。
- **专用 find API**：Structure model、Signal aspect、Sound/Sound3D 的 reset/run/unused/find-for-key/step/status 函数组合，供表格内搜索及从放置行反查资源定义。
- **窗口函数**：`render_othertracks_window()` 管理轨道可见性/颜色/范围；Station、Structure Put/Between、Structure Model、Other Train、Sound list/3D list、Repeater、Signal aspect/put、Section、Variable、Beacon、Irregularity、各 noise/sound/environment 和 SpeedLimit 的 `render_*_window()` 分别定义列、排序、行选择、定位、源码菜单和编辑入口。
- 每个专用窗口只读取 `TableUiCache` 与轻量 visibility 状态；修改 draft、选择或导航时调用 App 的共享编辑/定位方法，不直接拼接源码或重建场景。

#### `src/table/table_navigation.cpp`

- `invalidate_table_cache()` 清除 revision 与派生行；`reset_marker_visibility()`、`sync_marker_visibility_sizes()` 保持二维 marker flags 与模型行数量同步。
- Structure、Repeater、Signal 的 `locate_*_on_plan/in_list/in_scene_preview()` 分别更新 plan focus、table highlight/window visibility 和 Canvas3D jump；Repeater 额外处理 End/变化边界。
- `locate_standard_marker_on_plan()`、`locate_standard_marker_in_list()` 是 Beacon、Section、Irregularity、Sound/Noise、Background、Adhesion、CabIlluminance、Fog、DrawDistance、SpeedLimit 等成对函数的公共实现。
- other-train stop 定位同时打开对应分组与 stop 行。`locate_scene_marker_row_in_list()`、`locate_scene_marker_row_in_scene_preview()` 把 Canvas3D marker enum 映射回正确表格/源行；`can_locate_scene_preview_row()` 检查场景、索引和可见性条件。

### 调试入口与契约测试

#### `src/main_window/debug_headless.h`

- 每个 `*Options` 结构对应一个命令行模式：基本加载、plan/scene/open benchmark、场景相机传递、source anchor、roundtrip、distance/own/other track、Station list、Repeater、Section、insert、table find 和 touch。
- 头文件声明各 `run_debug_headless_*()` 入口，生产 Release 可不启用这些路径；参数结构使 `main()` 的命令行解析与具体测试实现解耦。

#### `src/main_window/debug_headless.cpp`

- **公共设施**：COM RAII、UTF 路径、输出文件、耗时统计、hash、快照 matrix 汇总、日志捕获和 fixture 查找函数为所有无界面模式提供确定输出。
- **加载/几何/场景检查**：基础 load 验证 snapshot 结构和矩阵；plan/scene benchmark 重复构建缓存并输出分阶段时间、数量和 hash；camera-transfer 检查 rebuild 前后姿态；scene 调试读取像素与 fog 状态验证渲染结果。
- **`typed_edit_headless`**：`Field/Change/Batch/Report` 是公共编辑 ABI 的 RAII 包装，负责字符串 view 生命周期、dry-run/apply/commit 报告复制和失败信息。
- **距离/自轨道/他轨道批次**：`distance_batch_headless` 的 MapHandle、edit 选择、resolution choice 和 report facts 驱动多文件/Include/变量环境用例；own/other track 模式验证方法不转换、参数形状、Apply/Reset/Commit 和几何变化。
- **列表与关联编辑**：`station_list_edit_headless` 创建临时 CSV fixture，验证编辑/清空/重排/删除及原编码；`repeater_batch_headless` 验证 chain 更新、trim 转换和原子删除；`section_edit_batch_headless` 验证动态参数增删、null/表达式保留和 commit。
- **插入与源码锚点**：insert 模式验证允许模板、距离块选择和未知字段拒绝；source-anchor/roundtrip 模式检查物理文件、Include stack、行列/span、stable id 和保存后重载一致性。
- **UI 纯逻辑检查**：table-find 模式验证大小写、exact/step/unused 状态；touch 模式用 debug 注入检查 tap、long press、scroll、pinch 和消费语义。文件末各 `run_debug_headless_*()` 解析 options、运行对应场景并输出 PASS/FAIL。

#### `src/maploader/tests/typed_snapshot_tests.cpp`

- `TempFixture` 创建并清理临时 map/list，编码帮助函数生成 UTF-8/BOM、UTF-16 与 CP932 输入；`MapHandle` RAII 调用 `kv_free()`；`CHECK_ARRAY` 等断言同时检查 count 与空指针契约。
- snapshot 测试遍历所有根数组、字符串/span、metadata、capability、revision 和稳定 edit id，并对 Signal glare/可变 key、资源 Load、Include 和场景快照执行定向检查。
- geometry 测试构造坡度/曲线 fixture，比较线路长度、平面投影、高程和事件距离，防止纵坡投影回归。
- `UpdateBatch`、`RepeaterTrimBatch` 等包装器构造 typed edits；edit 测试覆盖 dry-run、memory Apply/Reset、直接 Apply、Commit、concurrency hash、距离消歧、方法/参数形状、语义保护、编码和事务回滚。
- diagnostics 测试装载 `tests/` 本地 fixture，验证缺文件、错误语法、重复 Load/Enable、未配对 transition、未知 key 及日志/last-error 文本。`main()` 根据 `snapshot`、`geometry`、`edit`、`diagnostics` 和专项参数选择测试组，返回进程状态供 CTest 使用。

### 静态调用链摘要

```text
main / App
  -> maploader_runtime 的 kv_* 转发器
     -> maploader.cpp 的 C ABI 异常边界
        -> parse_map_context -> Parser -> MapContext
        -> generate_geometry -> Matrix / scene control points
        -> MapSnapshotBuilder -> KvMapSnapshot
        -> build_edit_report -> 补丁重解析与语义验证 -> 内存覆盖或事务写盘

App / MapModel
  -> datatable、canvas2D、profile_plots 构建按 revision 缓存的二维视图
  -> Canvas3DScene -> Canvas3D::Impl -> D3D11 分块、异步模型、拾取与 gizmo
  -> inspector / inline draft -> KvEditBatch -> maploader 源码优先编辑链
```

这条调用链中的所有权边界是静态检查时最重要的约束：`MapContext` 拥有解析和快照存储，GUI 必须复制所需数据；`MapModel` 拥有 GUI 缓存输入，画布只保留与 revision 对应的派生缓存；DLL 分配的独立字符串/模型数组只能由同一 DLL 的匹配释放函数回收。

## 核心工程规则

### C++ 与 ABI

- 使用 C++17，优先小而聚焦的变更。
- 内部代码优先使用 RAII、标准容器、`std::filesystem` 和职责单一的帮助函数。
- 保持 `UNICODE`、`_UNICODE`、`NOMINMAX` 和 `WIN32_LEAN_AND_MEAN` 假设。
- 异常、STL 类型、C++ 类或所有权不明确的指针不得跨越公共 C ABI。
- DLL 通过 ABI 返回的已分配内存必须有配对释放函数。
- 随附 EXE 要求 maploader API v6 和 model-loader API v2 精确匹配。
- 强类型 ABI 输入视为调用期视图；嵌套快照存储由句柄持有，并按已记录的几何重建、编辑操作、重置、重解析和释放规则失效。
- 公共 ABI 变更必须明确决定版本/结构尺寸，同步修改 EXE、DLL 和调用方，并记录所有权与有效期。

### 解析、几何与源码保真

保持对 BVE Map 2.0+、当前支持的旧式语法、`Include`、变量、预定义 `distance`、数学函数、注释及 UTF-8/BOM、UTF-16LE/BE、CP932/Shift_JIS 相关输入的支持。

实现符合官方 BVE 语法的通用规则；不得为单条线路写特例或增加私有线路语法。预设必须生成普通 BVE 地图/列表语句。

可编辑行必须保留源码路径、Include 栈、源码跨度、原语句和参数、求值结果、距离表达式、解析顺序与稳定 ID。`KvMapSnapshot` 必须全面且强类型。写回尽量保留原编码与行尾；无法表示的新字符会阻止写入，当前没有“另存为 UTF-8”回退。

### 编辑模型

- 源码所有权保留在 maploader 结构中；不得从 GUI 表格文本重建锚点或创建平行的 GUI 文档模型。
- Preview 与 Edit 水合仅由既有 capability bit 区分。
- `kv_edit_dry_run_typed()` 负责验证，`kv_edit_apply_to_memory_typed()` 更新工作副本预览，`kv_edit_apply_typed()` 是直接写入路径，`kv_edit_commit_typed()` 保存已验证工作副本，`kv_edit_reset_memory()` 仅在需要时丢弃覆盖。
- “应用”不得写磁盘；“保存”不得隐式重载；“撤销”和“重新加载”保持现有确认与磁盘语义。
- `sourceHash` 标识工作副本；多次内存编辑期间 `expectedSourceHash` 始终是磁盘并发基线。
- 按源文件、Include 上下文/区段和目标距离规划批量移动；保持语句顺序及用户注释或空距离结构。
- 应用或保存前完整重解析，证明目标语义值，并拒绝非目标元素或最终变量/距离环境的意外变化。
- 除现有显式确认转换（`Structure.Put0`、`Repeater.Begin0`、短式 `Signal.Put` 或 Repeater 修剪转换）外，保持方法与参数形状。
- 已加载的 Station、Structure、Signal、Sound 和 Sound3D 行使用共享行内草稿流程，不能新增资源行。
- maploader、表格、二维和三维统一使用 `repeater_linkage` 与过渡关联规则。

### UI、表格与渲染

- 保持 Dear ImGui docking 布局及现有菜单/工具概念。
- 所有用户可见文本都要同步加入简体中文、英语和日语，并保持工具栏/菜单措辞简短。
- 诊断继续输出到现有控制台，默认使用英语。
- 真正的偏好存入 `settings/settings.ini`，最近地图/背景对齐存入 `settings/history.ini`，布局存入 `settings/imgui.ini`。
- 保持平移/缩放/旋转/适配、测量、网格、车站跳转、坐标变换、标记同步、上下文操作与背景图对齐行为。
- 缓存表格内容；保持 Section 动态参数与显式 `null`、变量列表顺序及行/平面/场景导航副作用。
- 将 Assimp 隔离在 `model_loader.dll`；纹理缺失、文件无效和模型不支持时不得崩溃。
- 保持场景相机传递、拾取/高亮、可见性同步、标记配方、线路叠加层和 X/Y/Z 操纵器同步。

### 性能

- 避免重复读取、解码、转换、哈希、路径解析、几何生成以及大型轨道数组上的 O(n²) 遍历。
- 大数组尽量连续，避免在紧密几何循环或逐帧路径中分配。
- 不得每帧重建未变化的快照、表格、标记块、标签或模型数据。
- 长时间解析/模型任务使用异步流程，并通过状态反馈保持界面响应。
- 每个缓存都要有完整 key、准确的失效所有者与修订，并覆盖失效和命中路径。

## 验证

按受影响组件选择检查，并准确报告实际运行内容；不得把未运行的手工检查写成已通过。

常用 Debug 无界面命令：

```bat
build\komapedit.exe --headless-load-map <map-path> --headless-output build\headless-load-map.txt
build\komapedit.exe --debug-headless-plan-bench <map-path> --headless-output build\headless-plan-bench.txt
build\komapedit.exe --debug-headless-open-bench <map-path> --repeat 3 --headless-output build\headless-open-bench.txt
build\komapedit.exe --debug-headless-scene3d-bench <map-path> --window-back-m 100 --window-forward-m 1200 --headless-output build\headless-scene3d-bench.txt
build\komapedit.exe --debug-headless-scene-camera-transfer <map-path> --headless-output build\scene-camera-transfer.txt
build\komapedit.exe --debug-headless-source-anchors <map-path> --headless-output build\source-anchors.txt
build\komapedit.exe --debug-headless-station-list-edit <map-path> --headless-output build\station-list-edit.txt
build\komapedit.exe --debug-headless-edit-roundtrip <map-path> --headless-output build\edit-roundtrip.txt
build\komapedit.exe --debug-headless-own-track-edit [map-path] --headless-output build\own-track-edit.txt
build\komapedit.exe --debug-headless-other-track-edit [map-path] [--commit] --headless-output build\other-track-edit.txt
build\komapedit.exe --debug-headless-distance-edit-batch [map-path] --headless-output build\distance-edit-batch.txt
build\komapedit.exe --debug-headless-repeater-edit-batch [map-path] --headless-output build\repeater-edit-batch.txt
build\komapedit.exe --debug-headless-section-edit-batch [map-path] [--commit] --headless-output build\section-edit-batch.txt
build\komapedit.exe --debug-headless-table-find --headless-output build\headless-table-find.txt
build\komapedit.exe --debug-headless-touch-input --headless-output build\headless-touch-input.txt
build\bin\typed_snapshot_tests.exe signal-glare <map-path> [--commit]
```

为保证可移植性，应显式传入地图路径；自轨道、他轨道、距离、Repeater 和 Section 工具在省略时会回退到开发者机器上的线路路径。

最低验证范围应按变更涵盖普通/Include 地图加载、重载、平面/纵断面/半径图、车站跳转、测量、CSV 导出、模型预览/错误、三维轨道/对象/标记/相机/叠加层、编辑的应用/撤销/保存/重载、源码往返、编码/行尾、行内草稿、设置持久化和 Release 内容。磁盘写回变更必须保存后重载比较；性能变更必须在相同线路、参数、构建类型与加载配置上做可重复前后对比。

`komapedit.exe` 是 GUI 子系统程序；PowerShell 中需要捕获输出时，应使用 `Start-Process -Wait -WindowStyle Hidden -PassThru` 并传入 `--headless-output`。

## 构建脚本、依赖与分发

- 以 CMake 为唯一构建真相，批处理脚本保持简单且适合 Windows。
- 保留 `NINJA_EXE`、`VCPKG_ROOT` 和 `x64-mingw-dynamic` 回退。
- EXE 与声明文件位于输出根目录，DLL 位于 `bin`，INI 位于 `settings`。
- 分发清理保留 `bin`、`settings`、`LICENSE`、`NOTICE` 与 `THIRD_PARTY_NOTICES.md`。
- 旧版根目录 INI 仅在目标不存在时迁入 `settings`；冲突时中止且不覆盖。
- ImGui 使用 docking 分支，ImPlot 使用上游版本。
- 不得删除或绕过许可证/声明文件。新增依赖时同步更新 CMake、开发者文档和第三方声明。
- 线路发布导出与 `build_release.bat` 相互独立；实现时必须展开 Include、可选常量化表达式、仅复制已用资源、写报告，并通过临时输出保护开发目录。

