# komapedit Slop Fix 实施计划

2026-11-20 · 审计基线为 dev 的 `73b4b7714413f33d008569ddd3b186910bc91575`，调研开始时工作树干净。

## 结论与执行顺序

1. **先切断隐式写盘。** 六个 Debug 编辑命令用本机绝对路径补齐缺参，其中五个可在显式传入 `--commit` 后写入这条隐式选中的线路。另一个命令的固定输出无法区分临时夹具和输入线路上的提交。
2. **安排三个局部替换。** 替换编辑批次的字符串去重和逐目标全表扫描，替换 3D 模型重载中产生无任务 Pending 条目的流程，替换 Beacon、Irregularity 和 Station 定义表的重复绘制循环。每个替换都有独立的契约验收。
3. **分批消除已证明的内部冗余。** 优先处理 parser dispatch 的重复校验、资源交接中的空清理、2D 缓存和表格中的重复工作。性能改动以同条件基线决定取舍。

报告输入为 [A 报告](local_docs/slop-audit-A-261119/slopA-261119.md)和 [B 报告](local_docs/slop-audit-B-261119/slopB-261119.md)。初始请求中的第二个 A 路径已由用户确认是 B。下文的 A/B 编号指原报告编号，行号以基线提交为准。

本轮交付范围是本计划。结论来自当前源码、调用方、构建清单和已有契约，运行证据引用 11 月 19 日的日志。本轮执行了静态检查和文档校验，构建、CTest、headless 和 GUI 均留待实施验收。

## 核查范围

以 Git 跟踪文件和 CMake 源清单核对编译单元，并补齐头文件、资源脚本、根目录构建及分发脚本和图标工具。全文件静态扫描逐行覆盖分支、异常、资源释放、下标与窄化、文件 I/O、线程、缓存及重复代码。对疑点回读所在函数并追踪调用方、数据生产者和测试。表中的行数为物理行数，验证源码独立计数。

| 范围 | 程序源文件 / 行 | Debug 专用工具 / 行 | 本轮复核内容 |
| --- | ---: | ---: | --- |
| `include` | 12 / 4,987 | — | ABI、共享采样及链接、数值边界、计时、资源编号和六语词表 |
| `src/canvas2d` | 12 / 5,218 | — | 平面数据、坐标变换、背景、采样、测量、触摸、标记、剖面和绘制 |
| `src/canvas3d` | 29 / 13,562 | — | 公共门面、内部状态、D3D/着色器、加载、编辑、拼接、标记、相机、采样、雾、绘制和帧统计 |
| `src/main_window` | 29 / 24,866 | 8 / 21,803 | EXE 入口、路径和 DLL、加载与快照、编辑入口、对话框、持久化、WIC、文本预览、触摸及 Debug CLI |
| `src/maploader` | 17 / 20,827 | — | C ABI、IR、解码、诊断、解析、Include、几何、行身份、快照、编辑、事务、Scenario |
| `src/model_loader` | 1 / 325 | — | Assimp 导入、材质、边界、所有权和 C ABI |
| `src/table` | 9 / 6,873 | 1 / 589 | 表缓存、行列构造、菜单、搜索、草稿、Scenario、跨视图定位和基准 |
| **小计** | **109 / 76,658** | **9 / 22,392** | **118 个程序与 Debug 工具文件 / 99,050 行** |

另覆盖 7 个自有契约源文件共 15,965 行，以及 10 个构建、资源和工具文件共 675 行。本轮静态覆盖 **135 个文件 / 115,690 行**，所有受跟踪的 `.cpp` 均能在 CMake 找到归属。六语词表逐条检查了 3,690 个实际条目，每语 615 个唯一键，键集、非空值和占位符多重集一致，源码内 `tr("…")` 字面量键均存在。统计排除第三方、生成物、数据夹具和纯文档。覆盖方式为全文件静态筛查加重点调用链核验，缺陷触发和视觉等价仍由验收证明。[CMake 目标](CMakeLists.txt) · [六语词表](include/multilanguage.h) · [多语言契约](src/main_window/tests/multilanguage_tests.cpp)

## 实施批次

每批用当前源码核对引用和调用方，先固定现有输出及目标回归，再删除旧实现。共用运行契约见 [AGENTS.md](AGENTS.md) 和 [开发者指南](docs/dev_zhcn.md)。涉及源码编辑的批次沿用 typed ABI、工作副本、完整重解析、目标与非目标语义证明和事务写盘。UI 变更沿用六语、稳定行身份和 deferred 操作。维护前后按相同口径记录程序和 Debug 工具行数，分别记录测试与工具脚本。

### 1. Debug 编辑入口与真实源保护

- **证据。** `parse_headless_optional_map_edit_options` 给六个命令填入真实 Map。`other-track-edit`、`distance-edit-batch`、`repeater-edit-batch`、`section-edit-batch`、`insert-edit` 会接收 `--commit`。距离批处理最终调用 `typed_edit_headless::commit`。`own-track-edit` 的空回调会吞掉 `--commit`。`new-element-edit` 打印固定的 `mixed_save_scope=exclusive_temporary_fixtures`，该字段描述内建混合夹具。另一个显式 commit 分支会保存输入线路的目标源文件，记录路径的字段是 `committed_file`。A S-1、MW-3、MW-21、MW-22 · B D22。[参数入口](src/main_window/debug_headless.cpp#L225) · [距离批处理提交](src/main_window/debug_headless.cpp#L7529) · [新元素证据](src/main_window/headless_entrypoints.cpp#L1014) · [新元素提交](src/main_window/headless_entrypoints.cpp#L2685)
- **替换。** 删除 `k_default_edit_map_path` 及整个 optional-map 解析体。六个入口复用 required-map 解析，支持提交的命令保留各自显式开关，内存专用命令在本命令被选中后报告非法 `--commit`。保留 `--repeater-only` 和选项前后顺序。同步 bootstrap usage 及列有这些命令的开发文档。为 new-element 输出分开标明夹具及输入源操作范围，持续记录已提交文件。为距离批处理接入现有磁盘字节比对，区分内存 Reset 指纹和磁盘核验。场景生命周期在 `protected_sources` 为空时报告保护检查未运行，保留首个读取错误和失败退出码。A MW-22、MW-23。[通用解析入口](src/main_window/debug_headless.cpp#L263) · [场景保护输出](src/main_window/headless_entrypoints.cpp#L5503) · [命令分派与 usage](src/main_window/win32_dx11_bootstrap.cpp#L195)
- **验收。** 直接给解析函数输入六组无路径参数，含 `--commit` 与别的选项紧跟命令的组合，断言在打开 Map 前失败。用显式路径和空格/加号路径验证读取，用独占临时目录验证有提交选项的模式实际写入并可重开。以真实场景输入及解析出的 Map 做只读和内存验证，核对涉及源文件的盘上字节。
- **同批的契约修正。** Other-track Debug 合约在 `snapshot_ok` 后用 GUI 下标访问快照，行数不等只记录 FAIL。把快照取得、数量和同序 edit ID 改为访问前的失败出口，复用已有句柄 guard。2D marker 合约用外层报告输出异常和阶段，借现有上下文 RAII 清理。`beacons_common1/2.txt`、`$sig` 和 `stations121m.txt` 依赖仅在通用取样能覆盖同一语义时移除，保留不同 Include 上下文及重复 Station 行的证据。构造 BVE 测试语句时对必需字段直接验证，空键/零值按测试本意声明。A MW-1、MW-2、MW-19 · B D15。[快照下标](src/main_window/debug_headless.cpp#L3792) · [异常出口](src/main_window/debug_headless.cpp#L3915) · [距离取样](src/main_window/debug_headless.cpp#L5744) · [Station 取样](src/main_window/debug_headless.cpp#L8129)

### 2. Map 编辑批次、源版本和查找

1. **替换去重和插入入口。** `copy_edit_batch` 中同名普通字段由后值覆盖，`rowKind` 与 `repeaterPairId` 则有专门重复检查。`build_edit_report` 的文本签名漏掉这两个内部字段和 `confirm_repeater_change_point`，同 edit ID 的冲突项可被折叠。删除拼接签名，用 `edit_id` 索引首项，按规范化后的操作、目标源路径、行类型、锚点、替换文本、哈希、距离解法、确认位和字段映射直接比较。完全相同的项仍折叠，冲突走现有 blocking error。让字段解包阶段统一拒绝重复名。插入先验证结构化字段，删除通用插入的 `replacement_statement` 分支，保留现有更新格式和分阶段连接新 Include/资源列表。`validate_edit_report` 同步读取同一有效变更视图，移除深拷贝，保留完整重解析与语义证明。覆盖同 ID 不同 row kind、pair ID、确认位、换行值、相同项、有原文替换的 Insert 及前置连接后的内容行。A ML-3/4/5/7 · B §5.1。[字段解包](src/maploader/maploader_edits.cpp#L225) · [结构化验证](src/maploader/maploader_edits.cpp#L3163) · [去重](src/maploader/maploader_edits.cpp#L7411) · [插入旁路](src/maploader/maploader_edits.cpp#L7899) · [验证拷贝](src/maploader/maploader_edits.cpp#L8691)
2. **分清盘上基线和内存指纹。** `load_source_patch` 的 override 路径复用两份缓存哈希，后面的比较接受任一。外部修改在这条 Apply 路径可以暂时漏报，Save 的事务会再次核对盘上哈希并拒绝冲突。将两处编辑比较归到同一源文件准备步骤，`expectedSourceHash` 对照盘上基线，`sourceHash` 给工作副本。对于本批首次使用的每个 override 物理文件，做一次原始字节基线检查，复用事务哈希算法，空输入仍取现有默认基线。普通盘上路径复用已读字节。保留事务提交前及替换后校验。临时有 Include 的夹具验证首次 Apply、再次 Apply、外改后的拒绝、Save、Revert 和 Reload，报告 `plan.prepare` 的代价。A ML-9。[源准备](src/maploader/maploader_edits.cpp#L268) · [两处比较](src/maploader/maploader_edits.cpp#L7537) · [盘上事务](src/maploader/maploader_edits.cpp#L5079)
3. **替换逐目标扫描。** `find_editable_target` 对每个 edit ID 扫描 `parsed_statements` 和多个 typed row 容器，并逐行构造 `element_edit_id` 查找键。准备、语义校验和 Inspector 都会调用。`MapSnapshotStorage::elements` 是输出行数组，实施用 maploader 私有反向索引。以原函数的目标枚举为权威，值只保存 row kind 和行/语句索引，按命中位置恢复 `EditableTarget`，避免长期保存容器指针。当前/临时候选上下文各自按需建立，在内容替换及稳定 ID 重接处与现有 ID 缓存一起失效，几何更新复用，preview 不建。删除旧扫描，用同一索引服务目标快照、dry run、Apply 和非目标检查。验证 Include/资源引用、重复 Include、重复键、空列表首次插入、合成 Repeater 目标及 Apply/Save/Reset 后的旧 ID。A MW-7 · B A-M1。[目标枚举](src/maploader/maploader_edits.cpp#L2395) · [现有 ID 缓存](src/maploader/maploader_identity.cpp#L49) · [输出行数组](src/maploader/maploader_snapshot.cpp#L708)
4. **收敛插入距离与语义热路径。** `DistancePlanningIndex` 已服务距离分析，通用 Insert 仍对每项遍历全部语句筛同一物理文件。复用该索引，补物理文件到原有顺序的距离锚点视图。保留等距离择优、各 Include 实例环境、单调段与跨锚点边界。`text()` 把 snapshot 字段复制为 `std::string`，只向 `field(string_view)` 即时写入的点直接借用快照，拼接和离开快照生命周期的值保留所有权。一次快照构建中复用排好序的 Station 行。字段白名单与实际 edit writer 可验证重合的族先共用私有字段描述，保留插入、更新和只读行各自语义。用 `distance-plan-bench` 和 `edit-bench` 验证准备、验证以及总时间，守住现有严格预算。A ML-8/17/18 · B A-M5、§5.1。[插入锚点扫描](src/maploader/maploader_edits.cpp#L7760) · [语义 writer](src/maploader/maploader_semantic.cpp#L114) · [Station 快照](src/maploader/maploader_snapshot.cpp#L289)
5. **保证 preview 和编辑元数据来自同一源版本。** preview 已登记加载的源但省略哈希。外部更改只要保留行数及当前比较的自轨事件，就可能把新源的 edit ID/位置附到旧预览行。构建 `LoadedText` 时用已读原始字节生成 `KvSourceFileRow` 的摘要，复用现有字段。合并时比较双方物理源集合和摘要，保持 preview 省略语句和编辑注册表。验收在 preview 结束后改临时 Include 内一个 `Structure.Put` 参数，保持行数相同并启动元数据加载，结果应走现有丢弃提示。量化摘要成本。A MW-11。[哈希生成](src/maploader/maploader_core.cpp#L313) · [合并顺序](src/main_window/map_load_pipeline.cpp#L595)

### 3. 3D 模型重载、着色器和几何所有权

1. **先替换模型重载的 Pending 生产路径。** B A-K1 的 `start_scene_model_worker` 确实会丢弃活动 worker 期间提交的请求。首次加载已把信号的全部可用 `model_options` 纳入请求，且加载遮罩挡住打开新菜单，原报告的普通“加载中切换信号”缺少触发条件。当前可静态证明的路径在模型重载。PutBetween 实时预览把不同于正式模型键的临时键写入 `scene_models`。重载停止预览 worker 后把表内所有条目设为 Pending，只为 `collect_scene_model_load_requests()` 返回的正式键排任务。未被枚举的临时键永远不会完成，`scene_stats` 仍将它计入 `model_path_count`。工具栏在已有场景且无 Map load 时允许重载。[请求枚举](src/canvas3d/canvas3d_scene_lifecycle.cpp#L528) · [临时模型插入](src/canvas3d/canvas3d_put_between.cpp#L550) · [重载与统计](src/canvas3d/canvas3d_scene_lifecycle.cpp#L372) · [重载入口](src/main_window/scene_preview_lifecycle.cpp#L329)
2. **实现模型任务所有权。** 正式加载、刷新和重载都用正式请求集合建/收敛正式模型状态，删除“先把所有缓存置 Pending，再尝试为其中一部分排队”的循环。重载时用现有 PutBetween 生命周期解绑定旧预览键，保留 Inspector 草稿和当前目标，发布正式模型后按同代目标重排实时预览。已有选项从已注册模型中选择。凡仍需在工作期间追加的请求，由同一入口按键合并待启动集合，完成或取消时处理，清除场景或切换代时一并释放旧代。每个正式 Pending 对应已排队、运行中或等待 GPU 上传的请求，失败进入 Failed 且产生诊断。扩展现有 scene loader contract，用确定性事件覆盖 worker 活跃期间的同键/新键、Reload 与 PutBetween 临时预览、取消重开和失败注入。验收每个正式键的终态、`loading` 退出、相机和草稿保留、旧代结果失效。[工作入口](src/canvas3d/canvas3d_model_loader.cpp#L426) · [上传泵](src/canvas3d/canvas3d_model_loader.cpp#L689) · [加载契约](src/canvas3d/tests/scene_loader_contract.cpp#L176)
3. **替换着色器创建回滚链。** 四个 scene pipeline 分散编译 VS/PS、解析错误 blob、手工 release 部分初始化的资源。按现有 COM Release 约定用局部 RAII 持有资源，共用一次着色器编译/诊断函数。每套资源在全部成功后才发布，删除手工逐级回滚与部分初始化分支。保留普通、fog、outline、pick、marker、mask 的入口、input layout、深度和透明度。验证一个编译/创建失败可重试的契约，运行 scene render contract 的像素、拾取、缓存与旋转环案例。B A-K2。[D3D pipeline](src/canvas3d/canvas3d_d3d_resources.cpp#L213)
4. **替换 regular/scene 几何相互覆盖。** `kv_generate_scene_geometry` 为调用 `generate_geometry` 移出或备份 12 个 regular 字段，然后调用恢复函数。矩阵是 move，风险来自两种输出共享可变字段。把共用计算改为接收只读解析输入和显式几何输出，regular 路径发布规则矩阵、辅助缓冲与计时，scene 路径发布自/他轨道结果。Scene 临时持有计算输出和计时，保留自适应点与任意步长的组合逻辑。删除 `RegularGeometryState`、恢复 lambda 和内层 catch。沿用快照 ABI 和独立缓存修订契约，验证先读 regular 快照、生成/读 scene、复用参数、改参数重建、失败后重试及 regular 重建使 scene 失效。A MW-4。[覆写和恢复](src/maploader/maploader.cpp#L192) · [共用生成器](src/maploader/maploader_geometry.cpp#L1260) · [现有快照契约](src/maploader/tests/typed_snapshot_tests.cpp#L530)

### 4. 替换重复表格绘制与列映射

1. **Change-point。** `render_event_table`/`render_change_point_table` 是一条通用后端，Beacon 和 Irregularity 各有独立的列、滚动、裁剪、文件和里程菜单循环。现有通用后端只处理定位高亮，Irregularity 另有待删除/编辑色和双击入口。删除两份手写循环，沿现有通用后端传入对应行状态和单元格动作，保留 Irregularity 的双击与文件 tooltip、Beacon 的源行绑定和 2D/3D 定位。将 Irregularity 行生成并入 `append_change_point_rows`。同批修正 Signal、Beacon 的染色，使用已有的删除、编辑、定位优先级。用 `CachedTableRow` 的稳定 edit ID，沿用可见行滚动、长按和 deferred Inspector/Delete。A S-2、TB-2/4 · B A-T3/5/7。[通用渲染](src/table/datatable_effect_tables.cpp#L42) · [两表行为](src/table/datatable_effect_tables.cpp#L231) · [Signal 染色](src/table/datatable_route_tables.cpp#L630) · [Irregularity 缓存](src/table/datatable_cache.cpp#L547)
2. **Station 定义表与资源草稿。** 保留 Station 放置表与定义表的职责。删除定义子表的手写渲染循环，复用 `render_editable_list_table`，用现有接口表达 Station 子表高度、滚动和文件列差异，保留空列表首行、编辑缓冲提交、移动同文件行、双击、待删除色以及已有表/列 ID。`editable_list_visible_row_indices` 目前恒为 `[0,n)`，以逻辑行直接索引草稿，保留 Signal 主行/glare 到逻辑行和字段的映射。`editing_column` 和 `ClearCell` 使用字段号，`selected_column` 使用显示列号。延迟队列携带真正修改的字段号以及显示列号，Clear 后维持显示列和 secondary 状态。Station 的 offset 为 0，真实错位发生在有前置列或 glare 字段偏移的表。A S-2、TB-1、MW-41 · B A-T2。[通用列表](src/table/datatable_resource_lists.cpp#L356) · [Station 子表](src/table/datatable_resource_lists.cpp#L819) · [草稿索引](src/main_window/editable_list_drafts.cpp#L145) · [选中列赋值](src/main_window/editable_list_drafts.cpp#L919)
3. **动态列的所有权。** Signal.Aspect 的 `table_cache_` 先按正式模型建列头和宽度，窗口按草稿宽度对同一数组 `resize` 和 `push_back`，缩窄再扩宽会丢失正式列测得的宽度。把固定列和 `structureKeyN` 的布局生成归一个入口。正式布局在表缓存建立，草稿宽度在草稿布局变化时生成所需视图，绘制纯读。复用 `k_max_signal_aspect_structure_key_columns`，覆盖主行、glare、无草稿、缩列后撤销和达到上限。行号文本只在需要的显示行构造一次，保留既有编号。A MW-10 · B A-T2、§5.4。[正式布局](src/table/datatable_cache.cpp#L448) · [窗口更改](src/table/datatable_resource_lists.cpp#L1280)
4. **裁剪 Section 和 Variables。** Section 在滚动表中逐帧遍历全表，逐列 `to_string`，最大为 508 个动态值列。Variables 同样逐帧提交全表。两处替换为纵向 clipper，Section 把滚动目标用 `IncludeItemByIndex` 纳入，动态列头随表缓存建立，使用列可见性跳过无需提交的文本。先核对 Variables 组标题和普通行行高。验证 Section 的稀疏值、两种列前缀、选中远处行、菜单和键值，Variables 的组序与提示。用 table-cache-bench 比较冷建、暖缓存、绘制的行数与 median/p95。A TB-6/11 · B A-T1。[Section 渲染](src/table/datatable_route_tables.cpp#L722) · [Variables 渲染](src/table/datatable_route_tables.cpp#L842)
5. **验收。** `--debug-headless-table-cache-bench`、`--debug-headless-table-find`、`--debug-headless-signal-aspect-columns`，以及现有资源列表插入/空列表合约。专测 Station 的首次新增、同文件内移动、清空后选中格、Signal 主行/glare 的 Delete/Apply/Save/Revert、普通/稀疏列重建和 sourceHash。绘制变化做短程人工核对，使用只读源或临时副本。

### 5. 删除已证明的内部冗余

| 子任务 | 当前证据及实施改法 | 验收 |
| --- | --- | --- |
| Map dispatch | `validate_statement` 先校验方法、键与显式实参数量。删 Beacon、PreTrain、RollingNoise、FlangeNoise、JointNoise、Irregularity 的重复入参守卫，以及 Structure、Signal、Station、Sound 和 Train 各已匹配分支中由规则保证的最小数量/键守卫。按已验证的显式数量选择重载。保留 Null/Continue、正式重载、旧别名、PutBetween 可省 flag 和 Adhesion 一/三参等真实分支。`parse_map_args` 会把空实参加工成占位值，使用显式计数处理 0 参。共用 validator 与 deferred 记录对键/实参的只读访问。A MW-5 · B D4、A-M6、§5.1。[规则与验证](src/maploader/maploader_parser.cpp#L1103) · [dispatch](src/maploader/maploader_parser.cpp#L1418) | 用 diagnostics 和 typed snapshot 契约比较正确形状、错误键/数量、空值、旧式写法及诊断。用语句、源位置和几何指纹证明同一有效输入产出相同。 |
| Map 几何 | TrackPointer 及 CantProcessor 对他轨道事件复制成共享临时行。以具体行类型借用原事件，复用游标逻辑，保留同里程次序和连续值。收敛自轨道相同的直线/定半径步进分支，渐变仍用投影里程和两次积分差，保留零长度过渡的原赋值。已按有限里程排序的他轨范围取首尾。A ML-14、§6.6 · B A-M2/3/4。[事件访问](src/maploader/maploader_geometry.cpp#L73) · [过渡计算](src/maploader/maploader_geometry.cpp#L796) · [他轨范围](src/maploader/maploader_geometry.cpp#L1077) | 验证梯度投影、直/圆/缓和、有符号半径、零长段、其他轨道 cant、采样端点和快照几何。与批次 3 的输出所有权改动分别对比。 |
| 模型 DLL | `load_with_assimp` 用 guard 清理部分分配，只有成功返回后才整体赋到 `out_model`，因此删 ABI catch 内的两次空清理。按分配成功后写 `material_count` 的顺序发布数组，再删内部释放里的重复空判。复用已构造的路径取扩展名。计算半径时三个有限 float 坐标差在 double 的平方和范围内，保留窄化到 float 前的上界判定。A MLD-1 · B D11/12、§5.5、7.2。[边界与析构](src/model_loader/model_loader.cpp#L79) · [半径](src/model_loader/model_loader.cpp#L105) · [ABI](src/model_loader/model_loader.cpp#L297) | 验证失败后输出为空、DLL free 接受 NULL、成功材质/三角面/纹理与现有结果相同。 |
| D3D 契约 | EXE 正常入口与渲染契约均提供有效 device。在 Canvas3D 构造边界明确该前提后，删场景、预览和资源函数重复 device/context 判空。检查 render target 成功才使用成对存在的 RTV/DSV。沿 DLL、轨道和里程高亮产生路径证明每个 MeshPart 的材质索引，接收外部 mesh 时验证一次并报告导入错误，内部绘制和拾取用引用。A C3D-2 · B D5/6、A-K3。[构造](src/canvas3d/canvas3d_scene_lifecycle.cpp#L30) · [轨道材质](src/canvas3d/canvas3d_scene_geometry.cpp#L433) · [主绘制](src/canvas3d/canvas3d_scene_render.cpp#L130) | 运行 model/scene loader 合约及 D3D 像素/拾取验证，含 alpha mask、透明材质与 RTV/DSV 失败后的重建。 |
| 纹理键和错误出口 | `normalized_texture_cache_key` 的错误处理退回原路径，可跳过规范化。一次纹理失败会再算同一键。用已有严格 UTF 路径转换在纹理接收处生成一次稳定键，查找和失败日志复用，沿原有缺纹理诊断绘制无纹理材质。重写上传/worker 的重复错误收集，失败先让模型到 Failed，再在所属线程/UI/DLL 外部边界产出原始错误。删掉只用于自身赋值/比较/清空的 `last_error`、`scene_last_error`，需对外显示的失败接现有日志或返回状态。A C3D-2/9/10 · B D7/8/9、A-K4/5。[缓存键](src/canvas3d/canvas3d_model_loader.cpp#L79) · [上传](src/canvas3d/canvas3d_model_loader.cpp#L638) | 比较一图多引用、负缓存和失败诊断，复用加载契约的模型复制失败、线程启动失败、日志回调异常、上传失败、切图重开。 |
| DLL 日志 | 两个线程局部错误槽结构相同。共用私有实现，保留完整错误、成功状态以及 ABI 无分配失败出口。maploader 的日志组装和回调放在同一 noexcept 边界。`KME_MAPLOADER_LOG_ERROR(e.what())` 当前会在边界函数之前隐式构造字符串。入口收 `string_view`，字符串组装移入保护内。A MLD-4 · B D1/2、A-L1、A-H1。[DLL 日志](src/maploader/diagnostics.cpp#L16) · [形参和宏](src/maploader/diagnostics.h#L16) · [C ABI catch](src/maploader/maploader.cpp#L157) | 长诊断完整，异常无法从 C ABI 输出，回调异常与计时采样失败独立于解析和已完成的编辑。 |
| 表格内部契约 | `sync_marker_visibility_sizes` 后的 Structure/Repeater/Signal 导航，用 cache 行边界即可直接写标志位。经固定列缓存构造证明的四个 unused-key 列下标也可直取。复用 cell popup 基础函数，删除纯转发的 marker cell 包装及布局 `use_cached_columns` 别名。删 speed limit 无效宽度初值。把变量顺序键在稳定排序前解析一次。A TB-3/5/8/11 · B D10、A-T3/4/5/6。[导航](src/table/table_navigation.cpp#L73) · [搜索](src/table/datatable_find.cpp#L131) · [绘制 helper](src/table/datatable.cpp#L157) · [排序](src/table/datatable_cache.cpp#L317) | 验证表、2D、3D 导航，复用表缓存/查找指纹。动态行缺字段、草稿删除、切图换代仍由其生产状态检查。 |
| 2D 采样与标记 | Matrix 点采样入口和插值 helper 重复端点和范围判断。把端点、容差和空矩阵判断集中到唯一入口，游标调用传已确认的内段索引。复用 lower-bound 算法，保留 nearest 和插值区别。普通/旧 Fog 用已有 colored-marker 循环。测量格大小校验移到空间索引构建前一次，保留坐标到整数格的范围检查。生产测量只用 `current_plan_data()` 的缓存，收紧内部签名，让数据与 generation 从同一所有者取出。A C2D-5 · B A-C1/4/5、D18。[采样](src/canvas2d/canvas2d_marker_cache.cpp#L43) · [格索引](src/canvas2d/canvas2d_interaction.cpp#L145) · [Fog 循环](src/canvas2d/canvas2D.cpp#L1636) | 用现有 plan bench 的穷举对照测量，覆盖线性/空间索引、贴边端点、旋转缩放、只变 view 和源版本变化。 |
| 文本和路径 | GUI 与 DLL 重复 UTF 转换。Settings/History 的展示名、存储路径、去重 key 各有目的。复用 `kme_text_decoder` 的 UTF 和基本路径字节转换，保持路径归一语义在各所有者，明示缺值、非法编码和 I/O 错误的处理。删已验证的 `end &&`、C++17 下的 char8_t 死分支和 x64 上等宽整数恒假比较。文本预览仅对解码失败走既有 CP932 兼容尝试，内存异常走加载边界。仅合并字符集相同的 trim/lower。A MW-9/20/25/28/32/34/38、ML-2/19 · B D13/14、A-W4、A-H2。[GUI 转换](src/main_window/gui_common_utils.cpp#L245) · [共享解码](src/maploader/text_decoder.cpp#L83) · [路径所有者](src/main_window/app_settings.cpp#L35) | 验证 Unicode 路径、空选取、非法字节、相对路径、大小写，以及 Map/List/Scenario 的换行、BOM 和编码。 |
| 标记上传失败 | 全文复核新增。`set_scene_marker_visibility` 先写可见性，再重建显示与拾取索引。`Map` 失败会留下部分更新，下一帧相同输入会走相等快路。让索引缓存记录本代上传是否成功，失败时该代两个索引一起失效，下一次现有同步可按同值重试。显示和拾取共享本次发布结果。[可见性提交](src/canvas3d/canvas3d_scene_markers.cpp#L1236) · [D3D 失败点](src/canvas3d/canvas3d_scene_markers.cpp#L971) | 注入 index/pick buffer Map 失败并用同值重试，断言匹配标记数、选中和拾取，稳定帧保持短路。 |

### 6. 2D、3D 与 GUI 的剩余局部收敛

- **2D 拟合和帧内状态。** 初始拟合和双击各有一份 `build_plan_data(true)` 路径，发生在不同操作时点。以一次“算拟合边界并应用”取代重复代码，避免为只消费 bounds 的调用装配整份绘制数据，先锁定哪些自/他轨、标记及 LOD 进入边界。可见性四组数组每帧比较，渲染又数 Repeater。把写入集中到表格导航及可见性状态所有者，只有值或行身份变动才增 revision，变更点更新数目，plan cache 用 revision 和原有其它输入。渲染用同一次 LOD 判定。共用仿射计算并保留模型→局部平面→屏幕的坐标域与精度。A C2D-1/2/6/7 · B A-C2/3/6、§5.3。[缓存键](src/canvas2d/canvas2D.cpp#L522) · [拟合](src/canvas2d/canvas2D.cpp#L1199) · [坐标变换](src/canvas2d/canvas2d_primitives.cpp#L36)
- **2D 标记交互与对齐。** 同类型普通标记共用命中、触摸最近项和绘制清单，右键仍可收集同位置的多个可编辑候选。保留跨类及同距优先级、各类图标、special marker 的命中条件与调试阶段。hit transform 在拖动/旋转/双击拟合前构造，draw transform 在后，绘制阶段的半径检查有独立作用。等命中和绘制共享本帧同一变换与标记实例后再删。背景对齐以有位置的 Station.Put 源为候选，沿既有键匹配和相同 plan origin 变换取点。回归含站点在 Plot Range 外、只有定义的站、重复站键和无可用点。A C2D-3/7 · B D17。[命中时点](src/canvas2d/canvas2D.cpp#L945) · [交互及绘制时点](src/canvas2d/canvas2D.cpp#L1156) · [背景对齐](src/canvas2d/canvas2d_background.cpp#L132)
- **3D 帧内工作。** 用 stage profile 定位每帧实例分组和两次 `scene_stats` 扫描。单轨道实例用固定栈数据。若分组分配可测，按现有模型键/对象索引复用容量，重建场景时释放，切换窗口后有明确上限。`std::map::clear()` 会释放节点，单纯把 map 移成成员无法消除分配。模型状态转换归批次 3 的入口，测量后复用状态变化产生的计数。投影在输入后相机或 gizmo 可能移动，只复用完整输入相同的结果。可见性每帧同步已有相等短路，先量构造开销。复用 `PlacementTrackLookup`，只收敛语义相同的查找与采样。A C3D-3/4/11/12/13 · B §5.2、5.6。[实例分组](src/canvas3d/canvas3d_scene_render.cpp#L822) · [帧统计](src/canvas3d/canvas3d_scene_lifecycle.cpp#L477) · [可见性相等短路](src/canvas3d/canvas3d_scene_markers.cpp#L1236)
- **对话框与草稿。** 七个 `OPENFILENAMEW` 对话框使用一个本地装配函数，按调用点传 flags、初始目录、标题、过滤器和 Save 默认扩展名。第一份动态过滤器已有分隔 NUL 与 `c_str()` 结尾 NUL。同步六语图像标签。Inspector/向导的 Repeater/Section 列表共用同形的增删移动控件，保持联动语义、各字段验证和活跃输入缓冲。场景设置草稿仅在确认时写持久字段，取消保留 Canvas 端即时预览的回退动作。以当前已提交值和草稿比较，删除经全部写入点证明恒等的 before-dialog 标量。A MW-14/35/36/37 · B A-W2、§5.6。[文件对话框](src/main_window/app_dialogs.cpp#L80) · [场景设置确认和取消](src/main_window/app_dialogs.cpp#L1099) · [草稿](src/main_window/ui_elements.cpp#L600)
- **Settings 与 Win32。** 设置数字语法排除了十六进制、非法尾缀和非有限值，简单改为 `stod` 会扩大文法。保留同一十进制文法的单个转换出口及当前 saver 的范围、默认值。Settings/History 中分号、井号例子使用各自的有效键，对照 saver 精确文法验证。字体路径集中从 `GetSystemWindowsDirectoryW` 获取，保留各语言字体顺序。触摸以 Windows 10/11 的 WM_POINTER 入口为前提，删除目标 SDK 恒选的预处理分支。`GetPointerType` 失败需区分新接触与已有触点的释放/取消，覆盖鼠标、笔和触摸，之后再删除确证的测试专用 Release 代码。A MW-24/26/39 · B D14、§5.6。[设置入参](src/main_window/app_settings.cpp#L309) · [格式分支](src/main_window/app_settings.cpp#L537) · [触摸分流](src/main_window/touch_input.cpp#L288)
- **加载、编辑状态与背景图。** 把相同的文档切换清日志和清距离解析状态归同一个所有者，保留失败恢复、同一尝试重试、取消只删当前选择和清整批的差异。`apply_edit_ledger_to_preview` 的行类备份是 GUI/后端事务回滚所需。用 edit-bench 量到代价后，再以“先准备并验证目标行，成功后一次发布”替换备份流程，避免增加第二层回滚。WIC 解码与 GPU 上传的失败域各有职责。缩掉解码失败被反复解释的分支，保留贴图失败后的旧贴图和重试状态，异常分流和日志归文件/GPU 边界。A MW-12/13/15/30/31/42/43 · B D20/21、A-W5、§5.6。[编辑回滚](src/main_window/edit_ledger.cpp#L1441) · [距离状态](src/main_window/distance_resolution_workflow.cpp#L294) · [WIC/GPU 分界](src/main_window/background_image.cpp#L559)

### 7. 收敛 Debug 脚手架与工具脚本

1. 批次 1 完成后，把 bootstrap 的 43 个重复 usage 块及分派移到 Debug CLI 所有者。模式清单记录命令、usage、解析和现有 runner，保留单命令行为、诊断、初始化次序、默认值和返回码。通用参数只解析一次，模式独有的选择/布局保持明确。先对照整个 usage 清单，再按命令族切换并删除各族原分派。A MW-16/17/18/42 · B A-W1/3/6–12。[分派](src/main_window/win32_dx11_bootstrap.cpp#L195) · [通用参数](src/main_window/debug_headless.cpp#L229)
2. 使用已有二进制读取、快照读取/字符串视图、句柄 guard 与单一 ImGui/ImPlot 上下文所有者，收敛相同的 Debug 引导和输出代码。输出 helper 保留每模式既有 BOM/换行、字段与退出码，统一格式需先核对消费者。距离解析两份 settle 循环可共用。向导和 Repeater/他轨批量脚手架只共用完全一致的运转步骤，各自保留目标、选择、物理保护和断言。删除已置空的句柄死判、未使用别名和错误的数字边界。历史 load-map 的 preview 结果 `statements=0 elements=0` 来自预览跳过编辑注册表，基线按现有 profile 解释。A MW-16/17/18/20 · B D15/16、A-W3/6–12。[load profile](src/main_window/debug_headless.cpp#L1341) · [重复距离处理](src/main_window/headless_entrypoints.cpp#L946)
3. 构建/工具补查新增。`install_Assimp.bat` 首行清空已由开发者配置的 `VCPKG_ROOT`。使用现有环境变量，缺值即报告，保持该脚本的 MinGW triplet。`get_3rd_party_packages.bat` 在建目录/切换失败后仍执行 clone。首个 clone 失败也会执行第二个并掩盖退出码。补这两个实际边界的失败传递。分发脚本的目标目录、DLL 和许可证检查有实际用途。改动用隔离目录和 stub 命令核对缺环境、路径有空格、失败退出码和正常参数。[Assimp helper](install_Assimp.bat) · [第三方脚本](get_3rd_party_packages.bat) · [分发脚本](clear_build_release_dist.bat)
4. **基准副本体积。** A 附件 F11 指出的复制范围成立。`BenchmarkCopy` 会复制所有引用的共同祖先下的整棵子树。它保留模型的相对纹理等隐式资源，复制在计时区间之外。先记录复制根、用时和磁盘字节，若确实影响基准执行，再按已解析源和实际依赖构造副本。用 scene on/off 的贴图与快照指纹证明复制范围充足，保持拒绝重解析点以及可写路径边界。[复制根](src/main_window/edit_benchmark.cpp#L122) · [目录复制](src/main_window/edit_benchmark.cpp#L45)

## 两份报告的其余判断

### 当前代码的条件与边界

| 报告项 | 复核结论及实施取舍 |
| --- | --- |
| A MW-6、ML-12、ML-13 · B §5.1 | `as_number` 的 String 分支确实接受数值前缀和 `inf`。Number 路径也没有在此检查有限。报告举例的若干数值槽已由 `validate_statement` 拒绝 String。先用当前可到达的数值入口定位差异，保留各加载和编辑文法。Sound.List 读入把非法 bufferCount 当成 1，编辑要求空或正整数。可共享解析结果，维持读入默认和编辑拒绝，删除 LLP64 下无效果的 clamp。Scene 步长/误差当前会默认及夹取，收拢边界时保持现有值域。[数值转换](src/maploader/maploader_core.cpp#L329) · [Sound 计数](src/maploader/maploader_core.cpp#L483) · [自适应参数](src/maploader/maploader_geometry.cpp#L1153) |
| A ML-6、ML-8、ML-10/11 · B §5.1 | 建议表达式为空用于请求人工解析，保留这一输出，能证明的预期失败换成本地返回状态。Include 的 `external_variable_reads` 用于判断读值依赖，编辑模式还记录完整 entry/exit 和语句环境。保持全环境检查，只有具备环境重接和可测收益才简化。`physical_include_instances_are_compatible` 会跳过没有对应旧语句的插入，先用重复 Include 证明完整重解析是否漏校验，再决定是否扩展语义验证。[距离建议](src/maploader/maploader_edits.cpp#L618) · [多实例检查](src/maploader/maploader_edits.cpp#L7313) · [Include 环境](src/maploader/maploader_parser.cpp#L547) |
| A ML-15/16、§6.6 | Scenario 入口已在空 Route 时抛错，删 C ABI 候选相同分支，复用相邻的 checked size 算术，保持一次分配和相同 free。保存后重读发生在事务已验证新哈希之后，返回的是编码后的实际内容。直接复用编码前 `expected` 缺少编码回环证明。先从最终编码字节解码并证明元数据和语义等同，再考虑跳过重读。保留显式权重的序列化精度和空 Vehicle 约定。[候选分配](src/maploader/maploader.cpp#L395) · [保存](src/maploader/scenario_route.cpp#L500) |
| A §6.6 · B §5.1 | Win32 转换有 `size_t`→`int` 边界。在 Win32 入口统一拒绝超过可表示长度的输入，输出文件诊断。B 引用他列车的 `absolute(path, ec)` 是路径回退。Map/List 与 Scenario 的注释字符和引号规则各自保留。行号缓存先区分完整文件与 body 的基址再合并。BOM 检测只在同一原始字节及编码域复用。[Win32 解码](src/maploader/text_decoder.cpp#L170) · [他列车路径](src/maploader/maploader_parser.cpp#L1776) · [Scenario 注释](src/maploader/scenario_route.cpp#L212) |
| A MW-8、C3D-8 | `erase(lower_bound(...))` 要求成员实际存在。初建与更新共用 `scene_repeater_chunk_range` 和同一有序 membership 写入，现有调用链保持该不变量。用区间差移动和回滚契约覆盖，保持有序删除。实例规模运算保留已知范围及饱和判定，统计回退是否可删由初建/增量计数同源证明。[初建与 membership](src/canvas3d/canvas3d_scene_geometry.cpp#L312) · [增量](src/canvas3d/canvas3d_scene_edit.cpp#L464) |
| A C3D-5/6/7 · B D5/6 | `table_cell_number` 到 int 的 236/835 处需要追踪 `order` 与 `_structureKeyCount` 的 typed 生产者。必需序号提前按类型保存，外部 double 在入口校验后转换。979 处是大小转换。非空轨道的采样会返回点，可删重复的 camera 回退。`is_navigable` 已检查相同槽数时去重。`model_paths` 非空不能证明时间跨度为正，保留零长 Repeater 跳转语义。保留 ABI buffer 的指针/跨度检查。[顺序键](src/canvas3d/canvas3d_scene_data.cpp#L222) · [相机采样](src/canvas3d/canvas3d_scene_data.cpp#L977) · [跳转](src/canvas3d/canvas3d_scene_camera.cpp#L74) |
| A C2D-4 · B §5.3 | 旧曲线阴影按空 flag 和 `bt` 取段，`i` 由曲率采样/端点标记解释。仅收敛遍历中一致的部分，保持现有范围和端点。若让阴影显示 Interpolate 的新分段，先在 S3 明确绘制语义与验收。[阴影](src/canvas2d/canvas2D.cpp#L142) · [端点](src/canvas2d/canvas2D.cpp#L455) · [S3](TODO.md#s3) |
| A MW-11、MW-27 · B §5.4 | preview 源哈希由批次 2 补齐后，合并守卫才能要求比较双方。`Train.Enable` 走 noneditable ref，S4 负责正式编辑/新建。需要显示来源时只增只读描述。表缓存取首个 enable 值，多条行的解释留给 S4。[合并校验](src/main_window/map_load_pipeline.cpp#L501) · [只读来源](src/maploader/maploader_parser.cpp#L2128) · [S4](TODO.md#s4) |
| A TB-7/9/10/12/13 · B §5.4、§6 | 未使用搜索会强制 `exact=true`，单选会跳。保留清旧结果以避免同时高亮两种搜索，取消修改用户模式。Repeater 3D 跳转与共用菜单的 loading 策略有差异，按菜单和直接调用的输入契约确认后再合并。用现有 marker kind 映射收敛同形定位，保留特殊表的跳转。动态列表数量、空的 Cell/row 和 Lighting 可选单例是合法状态，检查归动作入口，删除只走已绑定 ID。草稿和 Scenario 各自的显示编号作为回归数据。[搜索](src/table/datatable_find.cpp#L42) · [Repeater 导航](src/table/table_navigation.cpp#L127) · [Lighting 控件](src/table/datatable_effect_tables.cpp#L1172) |
| B A-W4/5、A-H2、D3 · A MW-28/29 | ASCII lower/trim 重复，只有同一字符集和 locale 的实现合并。GUI Settings 的成员表与相等比较重叠，语义完全一致的 bool 部分共用，保留比较专属状态。字段 buffer 访问器有广泛使用，保留该接口。六语已登记键可共用同一直接查找。未登记的动态键按既有诊断/显示策略处理，替换前检查 Wizard、状态和对话框的键域。[GUI 工具](src/main_window/gui_common_utils.cpp#L170) · [Settings 所有者](src/main_window/app_settings.cpp) · [词典查找](include/multilanguage.h#L2565) |
| A MW-12/30/31/33/40 · B D20/21、§5.6 | 计时与主流程的异常域不同，析构/日志出口保留诊断隔离，避免 `noexcept` 函数被未捕获的分配异常终止。WIC fault 注入和只供 Debug 的 history 包装可随对应 CMake/NDEBUG 归属清理。stderr 与用户日志有不同读者，确认消费者再去重复。`new_file_name_is_safe` 已拒绝空名，保持 `back()` 的入口约束。[计时](include/operation_timing.h#L33) · [WIC 注入](src/main_window/background_image.cpp#L109) · [文件名](src/main_window/new_project.cpp#L193) |
| A MLD-2/3/5/6 · B A-L2、§5.5、7.2 | 模型路径在读取边界生成一次绝对路径，纹理装配复用父目录，失败接现有加载/纹理诊断。Assimp 三角化可保留点、线，因此面过滤有用。缺 UV、法线或材质来自外部导入，保留有明确含义的分支。空 mesh/material 指针契约从当前依赖头无法完全推出。维持默认材质槽、越界归零、嵌入纹理支持范围和精确 v2 DLL 契约。现有客户端通过函数指针消费 `ML_API` 声明的 v2。[模型读取](src/model_loader/model_loader.cpp#L154) · [外部材质](src/model_loader/model_loader.cpp#L218) · [模型 ABI](include/model_loader.h) |
| B D19 · A ML-19、§7.2 | 并行槽耗尽的 deferred 是既定限流，系统线程创建异常也走此算法。统一槽释放和异常报告。改变线程失败策略需先定契约。Release 的 headless 翻译单元由 CMake 排除。固定 Windows 目标可删确定不参与目标编译的内部支路，保留公开头及受支持工具链接口。C ABI catch、外部文件/COM 失败检查和输入缓冲区范围检查仍是边界职责。[Map 任务槽](src/maploader/maploader_internal.h#L75) · [Debug 门控](CMakeLists.txt#L339) |

### 局部清理

- Maploader 的薄转发、重复求和、同一 Station 排序、已证明恒真的 glare 形状表达式，以及只在 DLL 私有结构赋值的 `MapEditChange::change_id` 随对应批次清理，保留公开 `KvEditChange.change_id`。`KvUtf8View` 的空值规则随字段可空性解释，共用 span 算术。语义相同的 Repeater 事件构造可复用，保留各消费方所需排序与字段。共用物理 Include 身份生成，保留稳定 ID 与重接行为。A §6.1/2/5/6 · B §5.1、§6。[内部输入](src/maploader/maploader_edits.cpp#L171) · [ID](src/maploader/maploader_identity.cpp#L40)
- 2D 同值 LOD 别名、小包装、帧内重算和冗余 bounds 检查随批次 5–6 清理。3D 同值阈值只合并同义项，导入、碰撞及相机的半径下限各有职责。重复右键动作复用 action 构造，保留禁用条件、gizmo 拖动轴、旋转环及焦点。`pair_segments` 仍有调用，保留共享链接。profile、marker、camera、geometry 和 primitives 的文件拆分符合当前职责。A C3D-11/12、§6、§7.3 · B §5.2/3、§6。[共享链接](include/repeater_linkage.h) · [3D 划分](docs/dev.md#3d-preview-modules)
- 表格 Repeater 的文件名/tooltip 双构造可共享路径存在判断，保留文件名与完整路径。`CachedTableRow::tooltip_text` 对多数简单行与 `open_path` 相同，Repeater 跨源含 Begin/End 双值。查找小写副本优先让单次查询规范化一次，以基准决定是否缓存。SpeedLimit/LegacyFog cache refresh 的同构部分复用，保留不同输入和脏标记。A §6.4/5 · B A-T8、§5.4、§6。[Repeater 路径](src/table/datatable_cache.cpp#L129) · [查找](src/table/datatable_find.cpp)
- `LoadModelOptions` 的重复默认及转发重载、加载日志清零、文本预览同帧 `line_count`、重复的 `needs_expression` 和 Debug 计时可按批次收敛。`edit_number` 与展示格式各服务可回写精度和 UI。动态 required/value 约束按同一字段契约共享。`fog.change`/`adhesion.change` 的 Wizard 创建和 Inspector 修改分别验证正式形状、旧别名、字段默认和省略。相同 include 前缀按实际使用精简，`WndProcHandler` 声明归实际调用点。A MW-42/43、§6 · B §5.6、§6。[载入选项](src/main_window/map_load_pipeline.cpp#L704) · [检查器](src/main_window/element_inspector_data.cpp#L2074) · [向导](src/main_window/new_element_wizard.cpp#L1425)

### 实施时另行决定的行为

| 决策 | 可证事实与当前实施要求 |
| --- | --- |
| 解码兼容（A ML-1、§7.1/7.4） | 声明解码失败会报警并以猜测 encoding 读入，保存仍保留原头行。写回严格拒绝不可表示字符，同时可能继续保留声明和实际编码不一致的文件。用坏 UTF-8 字节、有效 CP932 头、UTF-8/UTF-16 BOM 的独占夹具定出触发条件。决定是否允许这种源编辑及是否保存两种编码身份后，再改策略。[回退](src/maploader/maploader_core.cpp#L292) · [写回](src/maploader/text_decoder.cpp#L331) |
| 3D 几何来源与 WARP（A C3D-1、§7.4） | Scene snapshot 检查失败时记录 warning 后用 regular typed geometry。该分支也为无 handle 调用服务。批次 3 先拆清输出所有权。是否取消生产 regular 后端或 GUI WARP，分别评估现有入口和出错行为。[Scene 后端](src/canvas3d/canvas3d_scene_data.cpp#L1041) · [GUI 设备](src/main_window/win32_dx11_bootstrap.cpp#L130) |
| 显式 `--commit` 范围（A MW-3/22、§7.4） | 现有入口允许显式路径与 `--commit`，批次 1 修正隐式输入及证据。把显式提交也限定到夹具会改变既有 CLI，需单独批准。计划验收的写入均落独占临时夹具。 |
| 编辑支持与数值兼容（A MW-6/27、ML-12/13 · B §5.1/5.6） | S4 决定他列车 Enable/Stop 编辑。数值强制文法、Scene 参数策略、畸形 Sound.List 读入政策独立于内部去重。嵌入纹理的公开 ABI 和依赖扩展按项目规则另行决定。 |

## BVE 契约核对

已核对本地官方页缓存，`last-updated.txt` 为 2026-11-13，审计日距刷新 7 天。各官方文件的首行是精确版本标识，可在其后紧接冒号和编码名，冒号两侧无空白，省略编码使用 UTF-8。表中列出本计划涉及语义的分支。其余经过同一索引、哈希及序列化管道的元素沿用官方签名和现有支持状态。实施前按实际触及字段补齐正向、反向及保真证据。维持现有旧文件版本、空值、CP932 别名和项目兼容语句的分类。[缓存入口](.agents/skills/komapedit-bve-format-compliance/SKILL.md) · [官方签名及语法基线](.agents/skills/komapedit-bve-format-compliance/references/official-bve-format-baseline.md)

| 官方缓存及头 | 已对照的形状及语义 | 当前路径及验收 |
| --- | --- | --- |
| `map.html` · `BveTs Map 2.02` | 分号结束语句，`#` 和 `//` 到行尾为注释，元素/方法名忽略大小写，参数字符串单引号。`include 'relativePath'` 从当前物理 Map 定位，子 Map 有自己的头。保留里程非负、变量及表达式求值、声明和摆放顺序。 | 当前入口最低 2.0。批次 2/5 保留类型值、Include 路径/栈/局部里程、最终变量绑定、同里程次序和写回字节。夹具覆盖表达式距离、错误数量/键、外改及多次 Apply/Save/Reset。 |
| `map.html` · 结构、信号与 Repeater | `Structure[key].Put(trackKey,x,y,z,rx,ry,rz,tilt,span)`、`Put0(trackKey,tilt,span)`、`PutBetween(trackKey1,trackKey2[,flag])`。`Signal[key].Put(section,trackKey,x,y)` 或加 `z,rx,ry,rz,tilt,span`。`Repeater[key].Begin(trackKey,x,y,z,rx,ry,rz,tilt,span,interval,keys...)`、`Begin0(trackKey,tilt,span,interval,keys...)`、`End()`。位移/弦长/间隔为米、角度为度、tilt 为 0–3、PutBetween flag 为 0/1，信号 section 为相对闭塞索引。紧凑形状等价于零偏移，资源键引用对应列表。 | 保留当前 typed 读/编辑/创建、正式重载、结构键序列、同距 Begin/End 链、信号关联和显式转换确认。验收短/全信号、Put0/Begin0、两种 PutBetween、重复 Include 源回连和负例，并锁定现有几何配置的结果。 |
| `map.html` · 值表、站和事件 | `Section.Begin(signal0,...)`、`Section.SetSpeedLimit(v0,...)` 分别有官方旧别名 `Section.BeginNew`、`Signal.SpeedLimit`，值是信号索引与 km/h，速度位 `null` 表示不限。`Station[key].Put(door,margin1,margin2)` 在当前里程设停点，官方 door 为 −1/1，后方容差为负值。`Beacon.Put(type,section,sendData)` 两端是整数，中间是相对闭塞。`Irregularity.Change(x,y,r,lx,ly,lr)` 单位依次 m、m、rad、m、m、m。`PreTrain.Pass(time)`/`Pass(second)` 接受时刻字符串或午夜起秒数。 | 保留值位置和稀疏项。断言 Beacon/Irregularity 的来源身份、Section 变长值含空位、Station 定义/放置关系、先行列车时间两种形式和旧别名。 |
| `map.html` · 环境、声音与他列车 | `Curve.Interpolate(radius,cant)`、`(radius)`、`()` 保留先前值与补间语义。`Fog.Interpolate(density,red,green,blue)`、`(density)`、`()`，RGB 为 0–1，旧四参 `Fog.Set`。`Adhesion.Change(a)` 或 `(a,b,c)`，a 是零速粘着系数，b/c 随速度变化。`Sound.Load(path)`/`Sound3D.Load(path)` 相对于语句源。`Sound[key].Play()`、`Sound3D[key].Put(x,y)` 的坐标为米。`RollingNoise.Change(index)`、`FlangeNoise.Change(index)`、`JointNoise.Play(index)` 引用相应车辆声音索引。`Train.Add(key,path,track,direction)`、`Train[key].Load(path,track,direction)` 用方向 −1/1。`Enable(time\|second)` 结合当前里程，`Stop(decelerate,stopTime,accelerate,speed)` 顺序固定。 | `Legacy.Fog` 属项目兼容形状，保留现有路径。按已验证的显式实参数量收敛处理。声音及他列车验证键、索引、诊断和只读来源，维持 Train Enable/Stop 的 S4 状态。 |
| `station.html` · `BveTs Station List 2.00` | `#` 注释。逗号行依次为 stationKey、stationName、arrivalTime、depertureTime、stoppageTime、defaultTime、signalFlag、alightingTime、passengers、arrivalSoundKey、depertureSoundKey、doorReopen、stuckInDoor。两个音效键引用 Sound List。 | 保留 0.04+ 兼容、定义顺序、重复键与空键的解析语义。验收首行新增、同文件移动、带空列编辑、与 Station.Put/背景匹配及重开。 |
| `structure.html` · `BveTs Structure List 2.00` | `#` 注释。两列是 key 和相对此列表的模型路径，键供 Map、Signal Aspects List、Other-train 使用。 | 保留 typed 资源行的路径、来源和表示。验证不同物理列表内同名 key、相对路径和缺模型诊断。 |
| `signal.html` · `BveTs Signal Aspects List 2.00` | `#` 注释。首列现示名，其后一个或多个结构键按信号索引排布。空首列紧跟主行，定义同一现示的 glare。 | 保留 main/glare 的逻辑行身份、位置编号、空列和独立宽度。验证插入、移动、清格、删 glare、超过显示上限、Apply/Revert/Save。 |
| `sound.html` · `BveTs Sound List 2.00` | `#` 注释。sound name、相对列表的 WAV 路径、可省 buffer count，省略为 1。同一文件格式供 Sound3D.Load。 | 保留普通/3D 行及引用检查。分别覆盖省略、正数和非法文本的现有读/编辑行为，以及站点和他列车引用。 |
| `train.html` · `BveTs Train 1.01` | `#` 或分号注释。`[Structure]` 的 Key/Distance/Span/Z，结构键引用 Structure List。`[Sound3d]` 的 Key/Distance1/Distance2/Function，声音键引用 Sound List，Function 有 Stationary、Rolling、Acceleration、Deceleration，后两类各最多一个。 | 当前子格式读取用于键及文件诊断，S5 拥有正式编辑。路径与诊断收敛验收相同读入结果和错误。 |
| `scenario.html` · `BveTs Scenario 2.00` | `#` 或分号注释。Title、Route、RouteTitle、Vehicle、VehicleTitle、Author、Image、Comment 为键值行。Route/Vehicle 是相对路径，竖线分隔多候选，`path * weight`，省略权重为 1。Image 为相对 Scenario 的路径。 | 保留直接 Save、候选和字段存在位、空 Vehicle、未知行、编码/BOM/行尾和旧源哈希。验收多候选权重、重开及盘上并发冲突。 |

## 实施时的验收命令和完成条件

1. **基线。** A 的 Debug 构建记录增量无工作，B 的日志记录完成链接和 7/7 CTest。B 的真实 Scenario 加载和生命周期输出均 PASS，生命周期记录 56 个受保护源不变。A 单次 plan median/p95 为 1.343/1.779 ms，scene 为 0.991/1.716 ms，scene 有一条缺纹理告警。实施前以相同配置生成可比基线。[B 构建](local_docs/slop-audit-B-261119/build.log) · [B CTest](local_docs/slop-audit-B-261119/ctest.log) · [B 生命周期](local_docs/slop-audit-B-261119/headless-scenario-lifecycle.txt) · [A 平面](local_docs/slop-audit-A-261119/slop-audit-A-subagents/plan-bench.txt) · [A 场景](local_docs/slop-audit-A-261119/slop-audit-A-subagents/scene3d-bench.txt)
2. **构建及契约。** 当前 `build/CMakeCache.txt` 为 Debug、`BUILD_TESTING=ON`、`KOMAPEDIT_STRICT_WARNINGS=ON`。每批用 `.\build_dev.bat` 编译。若缓存不同，保留当前生成器和工具链，在同一 Debug build 目录配置 `-DKOMAPEDIT_STRICT_WARNINGS=ON -DBUILD_TESTING=ON` 后用脚本构建。运行 `ctest --test-dir build -C Debug --output-on-failure`。应有 `multilanguage_contract`、`typed_snapshot_contract`、`maploader_gradient_projection_contract`、`typed_edit_contract`、`maploader_diagnostics_contract`、`canvas3d_camera_contract`、`route_value_sampling_contract`。诊断失败时核对本地夹具可用性。脚本/打包或 Debug 门控有调整时，另用 `.\build_release.bat` 核对 DLL 布局和目标归属。[CMake](CMakeLists.txt#L111) · [验证技能](.agents/skills/komapedit-debug-headless-validation/SKILL.md)
3. **真实输入。** 使用下面的 Scenario。其 `Route` 已核对为 `..\..\Scenarios_GSR\map\Config_Map121M-ATSP+Ps_Ask.txt`。支持 Scenario 的入口直接传该文件，仅接受 Map 的入口取 Scenario 输出的 `selected_route`。写入使用内建的独占临时夹具或保持相同相对依赖布局的测试副本。按源文件枚举保存 Scenario、入口 Map、Include 和编辑涉及列表的盘上字节哈希，验收运行前后真实源相同，Save 后从测试副本重开。

```powershell
$scenario = 'E:\Railway\BveTsWorkspace\BVE-Gensokyo-Railway\GSR\Timetables\++ 1-Gensokyo Loop Line Clockwise ++\121M-ATSP+Ps_Ask.txt'
$exe = (Resolve-Path -LiteralPath '.\build\komapedit.exe').Path
$logFile = Join-Path (Split-Path -Parent $exe) 'slop-scenario.txt'
$commandLine = '--headless-load-scenario "{0}" --scenario-index 0 --scenario-edit-roundtrip --unit-distance 25 --headless-output "{1}"' -f $scenario, $logFile
$run = Start-Process -FilePath $exe -ArgumentList $commandLine -Wait -WindowStyle Hidden -PassThru
if ($run.ExitCode -ne 0 -or !(Select-String -LiteralPath $logFile -Pattern '^result=PASS$' -Quiet)) {
    throw "Scenario contract failed. Exit code $($run.ExitCode). See $logFile"
}
$selection = Select-String -LiteralPath $logFile -Pattern '^selected_route="([^"]+)"$'
$map = $selection.Matches[0].Groups[1].Value
```

按上例传递完整的带引号参数，各模式使用独立日志。验收读取退出码、已有结果标记、关键阶段和工作量计数。内建夹具模式按现有 usage 启动。

| 批次 | 命令及重点参数 | 必查结果 |
| --- | --- | --- |
| 1、2 | `--debug-headless-scenario-lifecycle <scenario>`，`--headless-load-map <map> --load-profile preview` 和 `--load-profile edit`，`--debug-headless-edit-roundtrip <map>`，受影响的 other-track、distance、insert 和 new-element 模式 | Scope、诊断、语句/元素和源标记符合 profile。实际 Apply/Delete/Revert/Save 顺序、全量重解析、源哈希和临时副本重开。含缺路径、批次冲突及外改拒绝。 |
| 2、6 | `--debug-headless-edit-bench <map> --scene off --repeat 5 --unit-distance 25`，相同参数再测 `--scene on`，每种配置三进程顺序运行。必要时执行 `build\bin\typed_snapshot_tests.exe distance-plan-bench 4000 middle` / `eof` | 对比 prepare、validate、Apply、Delete、Save、Revert 的 median/p95，核对目标/非目标语义、缓存刷新、失败恢复和原始源哈希。 |
| 3、5、6 | `--debug-headless-scene-loader-contract`，`--debug-headless-scene-camera-transfer <map>`，`--debug-headless-scene3d-bench <map> --frames 300 --unit-distance 25 --window-back-m 100 --window-forward-m 1200 --interaction stationary`，另测 `--interaction moving` | 终态、缓存、上传、相机、标记、像素、拾取、模型及实例计数一致。CPU 帧严格 p95 16.67 ms。`--profile-stages` 单独定位阶段，整帧预算使用关闭 profiling 的数据。 |
| 4、5 | `--debug-headless-table-cache-bench <map> --repeat 5 --unit-distance 25`，`--debug-headless-table-find`，`--debug-headless-signal-aspect-columns <scenario>` | 冷建/暖缓存/绘制耗时及指纹，动态列边界、滚动目标、草稿身份、未使用查找、删除与 Apply/Save。 |
| 5、6 | `--debug-headless-plan-bench <map> --frames 300 --unit-distance 25 --interaction pan`，另测 `measure-stationary`、`measure-moving`。`--debug-headless-settings-persistence`、`--debug-headless-touch-input` | 2D 帧严格 p95 16.667 ms，穷举和空间索引命中一致。设置/历史以 saver 格式重开，触点生命周期完整。 |
| 7 | 重跑批次 1 的 CLI 负例及各类的代表命令，对照完整 usage 清单。改动共享 Debug 基础后重跑其受影响调用者 | 核对日志内容、编码、输出结尾、退出码、运行模式、资源释放和真实源保护。 |

4. **性能对比。** 每个配置在同机、同 Debug 选项、同一路线、同装载 profile 和同参数下记录基线与修改后结果。场景、表格及编辑各三次顺序进程，固定预热、视口、窗口、相机、线程和纹理缓存。核对工作量和缓存/像素/语义证据后比较 median 和 p95，超预算按失败记录。CPU 帧、GPU 区间和可见 FPS 各按原指标解释。
5. **人工验收。** GUI 使用人工操作。表格核对布局、长按/双击、颜色和跨视图定位。2D 核对平移、旋转、触摸、背景对齐和命中。3D 核对选择/拖动、PutBetween 草稿期间 Reload Models、旋转环、模型透明度和拾取。全屏截图前确认 komapedit 最大化并位于前台。
6. **完成。** 每批保存验收证据与前后生产代码行数，对比用户行为、源保真、ABI、缓存和六语。同步受影响的使用说明及开发者指南，保持运行布局和许可证文件。检视最终 diff、空白和生成物，记录执行前已知失败和待验收事项。
