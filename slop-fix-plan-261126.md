# komapedit Slop Fix 执行计划 · 2026-11-26

审计基线为 `40c2f3429b3774352316372992759402c6b597cc`，开始时 Git 工作树干净。A、B 指两份独立审计。下文按当前源码的可达性、所有权和验收成本重新排序。同一缺陷只安排一次。此文件记录调研结论和后续实施步骤。

- 报告入口 [A 主报告](local_docs/slop-audit-A/slopA-261126.md)、[B 主报告](local_docs/slop-audit-B/slopB-261126.md)、[B 分项索引](local_docs/slop-audit-B/index-tables.md)。
- 执行依据 [仓库规范](AGENTS.md)、[Slop Fix 技能](.agents/skills/komapedit-slop-fix/SKILL.md)、[源码与验证入口](docs/dev.md)、[当前待办](TODO.md)。

## 审计范围与结论

以 `git ls-files` 建立独立清单，再对清单中的每个文本源文件逐行运行分支、回退、异常、范围、所有权、重复 I/O 和缓存失效扫描。按模块并行检查当前文件、函数及成员所有者，沿生产入口、失败路径和现有合同复核两份报告。大型模块的人工语义复核集中在发现、调用链和风险点，其全部物理行均纳入静态扫描。结果的置信度取决于当前调用路径，历史修复记录只用于定位。

| 清单 | 文件 | 物理行 | 复核重点 |
| --- | ---: | ---: | --- |
| `src/maploader` | 17 | 19,910 | 解析、Include、几何、身份、语义、诊断、事务、编码和 C 边界 |
| `src/shared` | 3 | 817 | 几何采样、缓存、检查点和事件游标 |
| `src/model_loader` | 1 | 324 | Assimp 输入契约、模型/材质转换、错误和释放 |
| `src/main_window` | 40 | 48,324 | 文档生命周期、草稿/预览/保存、设置、资源、Win32 入口及其中的 Debug 驱动 |
| `src/canvas2d` | 14 | 6,961 | 对齐投影、缓存、绘制、输入、背景和纵断面 |
| `src/canvas3d` | 29 | 13,516 | 资源、模型、线程、相机、场景、绘制、拾取、Gizmo 和配置 |
| `src/table` | 10 | 7,130 | 窗口、缓存、行身份、搜索、编辑、导航与基准 |
| `include` | 12 | 5,050 | ABI、链接规则、数值、计时、语言和资源 ID |
| 根目录与 `icons/` 的构建、资源注册及工具源码 | 10 | 954 | CMake 目标、BAT 入口、打包、资源 ID 映射、图标生成 |
| CMake 编入 Debug EXE 的 `src/canvas3d/tests` 两个驱动 | 2 | 1,686 | 渲染/加载合同、故障注入和 Debug 专用调用 |
| 合计 | 138 | 104,672 | 前八行的 126 个 C++ 文件共 102,032 行 |

C++ 基线为上表前八行的 102,032 行，涵盖其中的 Debug 实现与头文件。另行比较 954 行构建/资源工具，以及两个编入 Debug EXE 的合同驱动。其余五个测试翻译单元用于合同核对。第三方、构建输出、生成资源、路线夹具和历史报告排除出生产统计。后续每批用同一清单规则重算，记录自有生产代码净变化。新增复杂度须有具体验收收益。

| 判定 | 代表结论 |
| --- | --- |
| 当前源码已证实 | Windows 目标中的非 Windows 分支、固定数据形状上的重复探测、提交与 UI 样板的多个所有权、排序比较器的重复转换、PutBetween 后台工作线程的终止处理，以及下列能复核的契约漂移。 |
| 现象真实，报告推论需修正 | 两次事务哈希、若干空值与数值守卫、`parameter01` 的最终语义结果、未知轨道的既有诊断、3D 同帧两个采样时点及 Debug 故障覆盖。 |
| 有实现差异，行为待决 | 非官方 `Train[key].Add`、非法 `bufferCount`、未知轨道预览、部分可见交互与诊断文字。决策与验收集中在末节。 |
| 需测量 | 已能证明重复工作的条目可独立简化。换缓存结构、改变遍历算法以及改变资源保留策略，先证明成本和结果等价。 |

## 实施顺序

| 批次 | 范围 | 完成条件 |
| --- | --- | --- |
| 0 | 重现严格构建基线，修复两处遮蔽 | 严格 Debug 构建转绿，七项 CTest 通过 |
| 1 | 单一 Windows 路径、线程完成协议、内部回退清理 | 三个运行组件正常加载，失败路径能结束、重试及回滚 |
| 2 | maploader 私有行元数据、语义写入、编辑计划 | ABI、读写轮回、编辑身份、变量绑定与错并发拒绝一致 |
| 3 | 2D/3D 重复工作与缓存所有权 | 路线采样、绘制与拾取合同一致，性能结果可对比 |
| 4 | 表格和主窗口复用，缩短内部调用链 | 窗口、导航、搜索、编辑、保存与设置的合同一致 |
| 5 | Debug 工具面和文档验证 | 现有 CLI 协议与故障覆盖可对照。交付分批验收记录 |

批次 1 的后台线程与 Windows 路径可独立提交。批次 2 先完成语义核对再替换遍历。批次 3 先做等价的局部删减，再处理可观察的缓存变更。批次 4 先统一导航和行索引，再合并窗口。每个结构性问题替换原实现与旧调用点，保留一个所有者。跨组件 ABI 改动和行为待决项有各自的准入条件，其他批次可照常推进。

### 0 · 恢复严格警告基线

1. 在正常 Debug 构建基线之后启用 `KOMAPEDIT_STRICT_WARNINGS=ON` 和 `BUILD_TESTING=ON`。报告 A 的 [严格编译日志](local_docs/slop-audit-A/strict-warnings-build.log) 与现存声明吻合。重命名 [`profile_plots.cpp:344`](src/canvas2d/profile_plots.cpp) 的内层 `size` 及 [`plan_alignment_validation.cpp:555`](src/main_window/plan_alignment_validation.cpp) 的内层 `opened_on_press`，按实际含义取名。
2. 保存首轮完整告警结果及七项 CTest 结果。当前 `build/CMakeCache.txt` 为常规 Debug，严格开关关闭。A 已记录两处错误，B 的 `ninja: no work to do` 是增量状态，不能验证严格配置。把新发现按基线或本批回归记录。

### 1 · 单一平台路径与实际的失败边界

| 对应报告与现状 | 实施 | 验收 |
| --- | --- | --- |
| A S1-1。`text_decoder.cpp:21–194,341–367`、`maploader_edits.cpp:4782–4952`、`maploader_internal.h` 与 `model_loader.h` 含非 Windows 替代实现。非 Windows 解码把非 UTF-8 当原始字节，事务替代分支使用不同语义。 | 直接删除替代实现及只服务这些分支的常量，保留一份 Win32 路径。按 C++17 简化两处 `char8_t` 条件。清理与固定 `_WIN32_WINNT` 重复的触摸预处理分支。保留 MinGW/MSVC 的构建差异、三组件的运行时 DLL 绑定、硬件设备到 WARP 的既有获取路径和第三方依赖部署。CMake 只移除能证明由 Windows 目标保障的冗余条件。 | UTF-8/BOM、CP932、不可写回字符、CRLF/LF、相对 Include 文件与多文件替换/回滚。用三个正常输出产物运行场景入口，涉及 Release 装载/打包的清理增加 Release 核对。 |
| A S2-1、S2-13、B S2-17、S2-32。`app_settings.cpp:34–60`、`file_structure_diagram.cpp:168–186`、`canvas3d_model_loader.cpp:79–91` 的内部 catch-all 从原始数据伪造成功结果。Win32 运行路径获取另有 CWD 回落。 | 归一化函数使用现有转换和确定性路径操作，文件系统可预期失败在实际边界用 `error_code` 或 Win32 错误形成原有结果、纹理键保持现有等价路径与大小写规则。将可执行文件路径获取交给单一 Win32 路径，扩容应对合法长路径，API 失败由启动入口报告。现有 `set_last_error` 若要去掉兜底，先在 DLL 边界以定长 TLS 原位保存真实信息并核对长度，在长消息仍需完整的合同下保留现有存储策略。 | 测绝对/相对/非 ASCII 路径、失效文件、长路径、报错文本和缓存键一致性、DLL 失败仍返回明确错误且可在随后加载成功。参见 [设置](src/main_window/app_settings.cpp)、[运行路径](src/main_window/runtime_paths.cpp)、[场景模型](src/canvas3d/canvas3d_model_loader.cpp)。 |
| 新复核。`canvas3d_put_between.cpp:278–375,413–451` 把 `joinable()` 当活性。每任务异常分支再次分配 `CpuModelData` 和错误文本，二次失败会退出线程。外层只发日志与唤醒，下一请求可能排给仍可 join 的已退出线程。 | 替换整个完成协议。每个被受理的序号在同一个 worker 边界发布成功或失败，分配失败有可分配前保存的状态码。终止时发布 terminal 状态并唤醒，所有者消费完成状态后 join，下一请求可启用 worker。保留只处理最新请求、generation 废弃、停止时 join、模型缓存和主线程资源上传。对上传/任务提交的同类嵌套 catch 一并归到各自边界。错误串组装留在能处理失败的出口。 | 越过正常 job 失败以及失败结果分配失败，检查当前请求结束、重新提交成功、换线和退出无遗留工作。用 Debug 合同覆盖失败，再用真实地图场景验正常资源/选中行为。参见 [PutBetween](src/canvas3d/canvas3d_put_between.cpp)、[模型加载](src/canvas3d/canvas3d_model_loader.cpp)。 |
| A S2-2、B S2-33、34。`model_loader.cpp:137–246` 有三处空元素跳过，材质数保底和索引改写、Assimp 的完整场景保证至少一个材质，坏输入来自第三方边界。`std::clamp` 无法把 NaN 变成合法 alpha。 | 用一个 importer 输出校验确立 complete scene、mesh/material/index 合同，使用当前 Assimp 版本的契约和样例确认 `AI_SCENE_FLAGS_INCOMPLETE` 的当前可达性、完整场景直接消费，删除空槽与材料 0 复写。若拒收当前能加载的 incomplete 资产，转末节确认。半径最小值与透明度归一化分别核对 DLL 值、EXE 相机/渲染用途后归到各自唯一负责层、含 NaN 与可选嵌入纹理的行为按独立契约验收。ABI v2 版本号可命名共享、保持三个入口和导出符号，`dllimport` 的更改须有实际编译/链接收益。 | 正常及缺材质、非法索引、退化包围盒、alpha 边界、一模型多网格、材质/索引、包围盒、颜色和错误在 DLL/EXE 两侧可对照。现有无法解析的模型仍由外部资源失败路径清理。参见 [DLL](src/model_loader/model_loader.cpp)、[ABI](include/model_loader.h)。 |

### 2 · 解析与事务内部收敛

执行者同时使用 [BVE 合规](.agents/skills/komapedit-bve-format-compliance/SKILL.md) 和 [源文本编辑](.agents/skills/komapedit-source-backed-editing/SKILL.md) 技能。先锁定 `sourceHash`／`expectedSourceHash`、源 span、稳定 ID、Include 顺序、全量重解析、目标语义与非目标语义。以下改动使用私有类型和现有 C ABI。

1. **替换行家族的重复枚举和内部语义分派。** A S2-11/12、B S1-4/S2-21。逐项对照 `merge_include_context` 的行搬移/索引、快照 `reserve`/登记/绑定、编辑子树收集/提交元数据、语义序列化和允许字段。先在私有代码用带类型的成员列表驱动确属同一集合的重复遍历，保留按语义存在的子集。完成一组后删除全套旧列表。`expected_target_semantic` 由解析后真实来源类型在一个分派点取得 typed row，加一次可报告的行身份校验，调用对应写器。Section／Signal／Repeater、链接与特例保留各写器。避免一个跨所有生命周期的通用描述表。覆盖 ABI 尺寸、count/pointer、顺序、目标身份、same-distance／Include 重复引用及插入/更新/删除/提交结果。参见 [解析](src/maploader/maploader_parser.cpp)、[快照](src/maploader/maploader_snapshot.cpp)、[语义](src/maploader/maploader_semantic.cpp)、[编辑](src/maploader/maploader_edits.cpp)。
2. **收敛 `parameterN` 与同义字段规则。** 当前更新的字段检查/源写入接受 `parameter01`（`maploader_semantic.cpp:854–862`、`maploader_edits.cpp:1770–1787`），期望语义仅查询规范 `parameterN`（`maploader_semantic.cpp:652–656`）。改动其值会在完整复核时报错。B 所谓更新成功接收比现场宽。插入入口 `maploader_edits.cpp:2945` 更早拒绝前导零。共用一个完整消费和溢出安全的索引解析，显式传入现有的阶段策略并保持结果。统一为早拒绝、是否保留同值操作现象由末节决定。用规范 `parameter0`、前导零、超大数、同值和异值检验，验证失败无源文件变化。
3. **收敛重复读写工作。** `maploader_geometry.cpp:22–26,118,315` 的 `sorted_unique` 按值复制传入的大向量再赋回同一对象，换成原地排序和去重，保持相同输出。删除 `strtod` 写入 `end` 后的重复空判、确认可移动的三处尾部行复制和没有语义的转发。共用能证明完全等价的数字/参数扫描和 operation 归一化。B S2-20 的四套数字格式有不同精度/格式合同，先记录每个出口，再在同一底层保留显式的精度、舍入、整数和字符区域要求。诊断排序预计算路径折叠键，保留排序稳定性和原始显示。距离计划索引与 O(N×D) 扫描用同一数据验证前后结果，热度不足的部分先留待测。参见 [几何](src/maploader/maploader_geometry.cpp)、[解析基础](src/maploader/maploader_core.cpp)、[诊断](src/maploader/diagnostics.cpp)。
4. **简化事务周边副本，保留两道冲突窗。** B S2-18 的两次文件读哈希处于全批 preflight 与每个替换前，用于不同的外部写入时窗（`maploader_edits.cpp:5029–5055`）。替换后的 backup 检查仍有独立作用。保留它们。B S2-19 的资源 `Load` 阶段必须重解析才有新列表行 edit ID（`:8691–8770`）。把大报告的手写复制替换为有明确冲突/顺序的 move 合并，保留 staged 与最终重解析。Include delete 与 replace 可共享变量写入索引/比较，保留各自距离序列与可变来源标准。用 `typed_edit_contract` 验多目标、并发过期、失败后回滚、重试和旧编码。对 Apply/Save/Delete 的成本只用同一路线、同一场景配置的 `edit-bench` 证明。

### 3 · 2D、3D 和共享几何

1. **2D 数据所有权。** A S2-4/5/17、B S2-1/2/3。`canvas2d_marker_cache.cpp:357–359,465–478,605` 的已登记 own-track alias 只查一次，删除同源重复采样。采样只传自轨空键时直接调用自轨采样器，保留真实其他轨道 `1` 覆盖兼容别名的规则。未知轨道是另一个行为决策。`canvas2D.cpp:1185–1250` 直接使用 `MarkerHit::row_index`，删去中间 optional 转存。合并同一视图时点的变换、可见 Repeater 计数和 OtherTrackChange 的可见性谓词。把半径标签的距离序列和几何位置随来源/草稿/geometry revision 更新。候选选取和屏幕裁剪仍按当前视图、里程窗口及画布范围计算。为 Fit 单列实际包围盒输入，从已有相同数据构造轻量 bounds，避免为 Fit 再构建 markers 和所有轨道采样。按模式移出与缩放无关的缓存依赖。Pan 的 LOD 同时服务绘制及部分下游输入，先证采样端点、选中、测距和 Fit 等价再搬出构造步骤。保留不同图元的剪裁 margin 和坐标语义，用共享绘制公式替换相同副本。参见 [Plan](src/canvas2d/canvas2D.cpp)、[对齐](src/canvas2d/canvas2d_alignment.cpp)、[标记](src/canvas2d/canvas2d_marker_cache.cpp)。
2. **2D 草稿范围按入口确定。** A S2-3 所称 Inspector 能调用 `set_distance` 写出轨范围缺少实际路径。该方法当前只由 Debug 合同直接调用。生产拖拽在 `canvas2d_alignment.cpp:617–638` 将约束与 `base_->distances` 相交，计算输入令原样本与草稿距离取并集（`:264–270`），同一投影调用传入两者。因此 `marker_cache.cpp:252–257` 的前向扫描对当前调用有端点依据。把投影索引明确绑定到该采样序列，可使用单次归并而保留端点，收敛同一个 optional 的两次访问。只在证实来源 marker 可越界时在 owner 用明确的范围语义处理，禁止把非法编辑值改成静默钳制。用端点、采样间距离和轨外输入测试结果。B C2D-B-02 的曲线 Interpolate 在 Edit 模式绘制/命中与右键菜单采用不同可见条件，修改交互结果前按末节确认。
3. **3D 渲染初始化与局部重复。** 目前 `ensure_scene_pipeline` 先发布基础 mesh/depth，再尝试 outline/pick/marker（`canvas3d_d3d_resources.cpp:315–366`），单模型绘制依赖基础管线。把分段初始化直接重写为基础 mesh 和场景专有管线两层明确所有权，保留基础成功/场景后续阶段失败的合法状态。共享 resize owner 做一次失效，mask/pick 在 render target 释放时同步释放。D3D 失败检查和重试进入唯一 owner。仅删已证明由帧入口保证的 `device`／draw-list 判空以及单模型只写的 `bounds_min/max`，保留仍供场景剔除使用的 AABB。渲染错误存储的删减见收尾清单。`scene_stats()` 的 loading 前采样和上传后的统计采样各有时点，可在证实语义后用精确的 loading 状态替换前一轮全扫描，保留两个时点。排序前计算一次归一化轨道键和 `(distance, order)`，维持同距覆盖与原始稳定顺序。标记文本保留测量和写出两轮，复用首轮宽度。参见 [D3D 资源](src/canvas3d/canvas3d_d3d_resources.cpp)、[渲染](src/canvas3d/canvas3d_scene_render.cpp)、[场景数据](src/canvas3d/canvas3d_scene_data.cpp)、[标记](src/canvas3d/canvas3d_scene_markers.cpp)。
4. **共享几何边界。** 将 `flag == "i" || previous.flag == "bt"` 的几何判定收敛到采样 owner，源编辑 linkage 继续使用方法/arity 和来源顺序。给 `1e6` 的曲线阈值共用常量并保留当前所有严格比较、符号和边界结果。删除 Repeater finalize 对 `chain_index`/`chain_begin_index` 的恒等重写，保留 `chain_begin_count` 更新。共享五处同义的 Repeater 方法分类，保留来源事件、大小写和 order 政策。`fresnel_cs` 的 NaN 分支有实际用途，删去会落入 `ceil` 后整数转换（`own_track_geometry.cpp:125–136`）。`-0` 的位模式归一化具有缓存意义。前缀 checkpoint 的唯一生产调用在已存在且受限的行号范围内，保留其合同。对整表清空上限、Repeater 链索引和 `speed_at` 做基数、命中率、扫描耗时及几何差分后再换算法。参见 [几何](src/shared/own_track_geometry.cpp)、[游标](src/shared/track_event_cursor.h)、[链接](include/repeater_linkage.h)。
5. **按视图阶段复用。** B C2D-B-08 与 C3D-B-02 的变换有同形构造。平移、缩放、Fit、相机/Gizmo 更新能处于输入和绘制之间，故同阶段共享一个变换，再用新的状态进入下一阶段。2D 实际半径分别由 marker scale、测量和半径标签用途决定，报告所称三份统一 `20 px` 在当前树对不上。保留各自边界，只收敛相同规则。3D 两个轨道查找路径保留现有未知键、首个重复键及实际 `1` 键覆盖规则，保留独立 Debug 对照。生产仅去掉纯转发或证实等价的矢量查找副本。背景同距序列从 maploader `stable_sort` 到 3D lifecycle `sort` 再由 `upper_bound` 取最后条，确有顺序合同风险。先建立同距的效果测试，确认采用源顺序最后一项后用同一个稳定排序入口。参见 [2D 交互](src/canvas2d/canvas2d_interaction.cpp)、[场景 UI](src/canvas3d/canvas3d_scene_ui.cpp)、[场景生命周期](src/canvas3d/canvas3d_scene_lifecycle.cpp)、[采样](src/canvas3d/scene_track_sampling.cpp)。

### 4 · 表格、主窗口与设置

1. **先统一表格状态和导航。** A S2-7/8/9，B TBL-A-02、B-04/05/14/15/16。`table_navigation.cpp:128` 的 Repeater 场景前置判断比 `:479` 少 loading。表格菜单已调用统一谓词，Inspector（`element_inspector_data.cpp:1812`）绕过该门槛。复用共享入口前先锁定 Inspector 点击 loading 场景的期望结果。对 15 类标准标记用一处描述现有 cache、marker 可见性、窗口、焦点与滚动/高亮，删除成对薄转发和重复 switch。Structure Put/PutBetween 的共享索引、Repeater 链、其他列车和 Fog/LegacyFog 共享的可见性保留独立语义。固定列 `resize(N)` 与固定行身份已有所有者（`datatable_cache.cpp`、`table_navigation.cpp:73–78`），证明同源有序索引后去掉逐行重复 size 检查。保留标记位置可空、动态 Signal 草稿新列宽的默认值、动态 glare 行等真实状态。参见 [表格导航](src/table/table_navigation.cpp)、[路线表](src/table/datatable_route_tables.cpp)、[资源表](src/table/datatable_resource_lists.cpp)。
2. **一次计算、作用域明确的失效。** `datatable_cache.cpp:622–635` 的限速部分刷新把 distance 宽度写成 110 又立即覆写。提取同一距离/路径宽度计算，保留路径宽度的 200 下限，移除死写。全量失效会复位其他表和搜索，单行限速编辑只影响限速表和变化的 Plan 菜单。两种失效作用域有意义。集中该 family 的重建/滚动/高亮复位，测试独立编辑时其他表查找状态保持。B TBL-B-16 漏字段属于 Debug 指纹（`datatable_benchmark.cpp:74–231`），补现有摘要漏掉的 `creator_message_rows`、Section 动态 header、变量 `file_name`/多行标志等显示数据，生产快捷路径保持轻量。`ensure_table_cache` 的尺寸键依靠显式内容失效，优先核对一次变更是否通知缓存 owner，从可实际改变并复现的字体/语言/DPI 输入扩展键。其他列车停止组在构造中建立非空组及有效 row index，移除消费方对同一集合的重复检查，保留原窗口/表 ID 与滚动。参见 [构造](src/table/datatable_cache.cpp)、[表格基准](src/table/datatable_benchmark.cpp)、[编辑刷新](src/main_window/edit_ledger.cpp)。
3. **替换重复窗口前奏与单元格分派。** A S2-6/10、B TBL-A-06/07/08/09/13、B-07/08/10/12/17。拆出负责 `Begin/End`、Dock、消耗 focus 的表窗口入口，先迁 14 个事件表，再迁 5 个资源列表的缺失遮罩/来源行，最后复用到同形路线表。现有 `###` 及表/单元格 ID、焦点、占位、滚动、动态栏数均通过可观测对照保留。相同的四动作块改成一个明确的动作分派，删除九参数转发、单调用编辑包装、占位槽和重复 `replace_all`。继续在 `EndTable`／`EndPopup` 之后执行后端动作。对固定列表从一个列定义派生源字段映射，动态 Signal 主/辉光数据和 Section 参数分别保留真实计数。标签查询移到行循环外。待提交底色、文件名列头、截断文案虽有差异，作为可见行为单独验收。参见 [公共表控件](src/table/datatable.cpp)、[效果表](src/table/datatable_effect_tables.cpp)、[Scenario 表](src/table/datatable_scenario.cpp)。
4. **查找与已格式化单元格的内部契约。** B TBL-A-03/04/05/11/12、B-02/03/09/11/13/18/19/20。构造 `TableFindRowsView` 就绑定所需数据和列偏移，按当前草稿或缓存选来源，移除不可达的默认空对象。共用 Find／Find Unused 的结果复位，保留 `query` 和当前模式迁移，继续由用户显式触发搜索，是否停止 Unused 改 Exact 由末节确认。按草稿 revision 清除失效结果，切勿自动重搜。Signal 编辑查询后的单独旧结果判定有可见差异，合并前覆盖四个 find 窗口。固定列宽直接访问已构建数据。Signal 超过缓存列宽时使用真实的新草稿列，保留完整源形状和每家族现有限额。根据 `repeater_linkage::pair_linkage` 的 `display_index` 输出警告行号，复用已有链接结果或改用来源位置，保留原有未知键提示。`repeater_structure_keys` 的 count/序列来自同一个 setter，同时有 Inspector 中间行与空列表，先锁定生产写入和空形状再去掉重复哈希查找。字符串搜索可将 query 小写化移出比较循环，保持 ASCII 规则。验收逗号/空格键、同名键、主/辉光超列、删除草稿、不同资源表、上下查找、导航回跳和三种行高亮。参见 [查找](src/table/datatable_find.cpp)、[视图](src/table/datatable_internal.h)、[格式化及诊断](src/table/datatable.cpp)。
5. **Win32 文件对话框直接重写为一条入口。** A S2-14、B S1-6。`app_dialogs.cpp:81–275` 七处 `OPENFILENAMEW` 样板属实。无参 filter 的 `c_str()` 提供第二个 NUL，目前过滤器已正确终止。以一个私有 Windows 文件对话框建立过滤器、调用 open/save、返回转换路径。由调用方提供现有 owner、标题、初始目录、过滤器顺序和索引、flag、默认扩展名。保持取消及原选择范围。用手动流程检查 Map/图像/五类资源列表/Include 打开和保存。把单体弹窗中的同形确认逻辑逐组收敛，保留需要用户确认的动作与各自失效期。参见 [对话框](src/main_window/app_dialogs.cpp)。
6. **草稿与保存只设一次真实前提。** B MW-A-07/08/09/10、B-07/08/09/11/16。Map-only、Scenario-only、混合与无改动保存由文档入口统一预检，保留 Map 先提交、部分成功、未 Apply 列表草稿阻断、失败时留存账本的顺序。草稿 Revert 的共享清理写成成功 reset 后执行一次。合并 Inspector／Wizard／Lighting 必填/数值规则，显式保留变更后 canonicalize 与总 canonicalize 之差，并保持 Station 前后停站余量的符号、字段范围与默认处理。资源参数列表的实际同形渲染在 `element_inspector_render.cpp:309–437`，用一个有序列表编辑动作保持最少条目、baseline 与字段重编号。删 index 已检查后的重复判断、已保证的 Scene canvas 指针探测和纯转发 hydration 别名。保护 deferred Inspector/Delete、ABI span、来源身份和实际边界错误。Sound3D 不产生 rotation 事件，重绑先提交／清除现有草稿，因此报告的该空解引用入口未成立。参见 [保存](src/main_window/edit_ledger.cpp)、[列表草稿](src/main_window/editable_list_drafts.cpp)、[编辑控件](src/main_window/element_inspector_render.cpp)、[快照载入](src/main_window/map_snapshot_hydration.cpp)。
7. **合并设置的镜像规则，继续分文件持有。** A S2-16、B S2-16。沿 `app_settings.cpp` 的已有字段映射建立 window/view 同形读写和 App 成员映射，减少 `ui_elements.cpp:527–676` 与 equality 对同字段的手写副本。保留磁盘键、序列化精度/顺序、默认值和 settings/history/imgui 的所有权，实际变更才写。每帧构建比较可取样后迁到已知设置修改点，窗口关闭、拖拽间距和异步恢复同样参加变更触发。`save_history_entries` 的独立读属于 Debug 路径，生产已有完整 history owner。用规范节、失效数据、恢复失败重试、开关/菜单勾选和后台恢复测试。参见 [设置](src/main_window/app_settings.cpp)、[UI 状态](src/main_window/ui_elements.cpp)、[仓库设置规范](AGENTS.md)。

### 5 · 计时边界与 Debug 工具

1. **计时改用单一可结束的生命周期。** B S1-8。`operation_timing.h:33–41,85–91` 和 `edit_ledger.cpp:81–124` 确有吞异常，失败后递增深度可形成无 trace 的时段。直接删除 catch 会令 `noexcept` 析构内的字符串格式化或日志 sink 在失败时终止进程。将两个域的已知阶段标识和按阶段定长的时长计数放在计时 owner，析构只记录时间和恢复 TLS／嵌套 scope。在明确的 GUI／ABI 出口发布已有诊断格式。保留正常、无变更、嵌套、运行失败与首帧时序。业务结果在日志发布前确定，日志失败由发布边界表示。公共入口仍负责异常到错误码转换。参见 [计时](include/operation_timing.h)、[编辑](src/main_window/edit_ledger.cpp)、[诊断](src/maploader/diagnostics.cpp)。
2. **故障注入移回测试所有者。** B S1-5、S2-5/8。七个 3D 注入状态与测试 TU hook 只进 Debug。Release 的背景 WIC 注入链确存在（`background_image.cpp:67–191`）。将 Debug 注入归到已有合同确实需要的最窄依赖接缝，以发布/失败/重试结果验证。保留线程启动、缓存分配失败、marker 编译/map 重试和背景保留旧 SRV 的覆盖。把 Debug 帧采集的所有者与生产渲染分开，明确 cmake Debug source 归属。移动目录、调整 namespace 分块及删除故障用例本身没有修复收益。仅当一条重复断言已被同一可达失败证明覆盖时再去掉。参见 [3D 私有状态](src/canvas3d/canvas3d_impl.h)、[加载合同](src/canvas3d/tests/scene_loader_contract.cpp)、[背景](src/main_window/background_image.cpp)。
3. **压缩 CLI 和 fixture 驱动。** A S2-15、B S1-6/S2-12、13。`win32_dx11_bootstrap.cpp:195–743` 的宏内分派，以及 `debug_headless.cpp`／`headless_entrypoints.cpp` 的重复 option、预热、文件读和输出。用一张 Debug 命令表或小型按模式注册块替换重复结构，保留模式优先级、各命令必选参数、usage、退出码 1／2、输出 BOM 与 stdout 现状。区分路线源读取、日志检查和 fixture 创建时原有的缺文件生成/报错策略，再用已有读取函数和单一结果检查。公共选项只解析一次，未命中落到原有启动入口。保留独立 CTest 与 Debug 模式身份。参见 [入口](src/main_window/win32_dx11_bootstrap.cpp)、[参数和驱动](src/main_window/debug_headless.cpp)、[生命周期驱动](src/main_window/headless_entrypoints.cpp)。

## 各文件责任内的低风险收尾

同一文件进入上述批次时，可一起完成以下已核实的删减。仅为风格、移动模块或统一命名的建议留在原处。

| 涉及源码 | 可执行动作及边界 |
| --- | --- |
| [解码](src/maploader/text_decoder.cpp)、[解析](src/maploader/maploader_parser.cpp)、[编辑](src/maploader/maploader_edits.cpp) | Windows `wchar_t` 采用 UTF-16，删除 `WCHAR_MAX > 0xffff` 替代路径。在 `LoadedText`/Parser 共用每个文件的一份行索引，删除语句时通过已有 `SourcePatch::line_starts` 取行起点，验 CRLF/LF/CR、EOF、行首空格和多行删除。`copy_edit_batch` 已规范化 operation，直接使用它产生的值。`suggested_distance_expression_for_target` 对空表达式和 `distance` 自引用显式返回“需人工决议”，其他失败继续走编辑边界。集中资源表头规格和相同 CSV 词法，Signal 的主/辉光分类复用同一保留原文的入口。Map 嵌套表达式与编码保存按各自规则处理。 |
| [几何](src/maploader/maploader_geometry.cpp)、[Scenario](src/maploader/scenario_route.cpp)、[C 接口](src/maploader/maploader.cpp) | `append_arange` 的 `max_size` 检查保护已有行数加新增量，保留。将 Scenario 的八个字段从三份顺序映射收敛为私有表，保留字段位、原始键顺序、相对路径与无换行文件的现有默认值。共用输出 blob 的边界写法。生产 GUI 的 Scenario snapshot 和 Route resolve 目前各读取/解析一次，消除双读需要先找到现有 ABI 能覆盖的内部入口。否则单独决策。 |
| [画布原语](src/canvas2d/canvas2d_primitives.cpp)、[对齐](src/canvas2d/canvas2d_alignment.cpp)、[纵断面](src/canvas2d/profile_plots.cpp) | 共享实际相同的降采样和绘图状态清理，保留各图用途、轴限以及命中优先级。删除未使用的 touch 输出、只写的 `AlignmentSpan::radius` 和无意义 LOD 别名。已排序限速表的最后一项 `<= distance` 可用 `upper_bound`，同里程取最后一项。性能收益由悬停基准确认。对齐拖拽的一点/重复采样分母推论与现有采样去重和有效拖动步长不符。 |
| [模型预览](src/canvas3d/canvas3d_model_preview.cpp)、[场景模型](src/canvas3d/canvas3d_model_loader.cpp)、[Gizmo](src/canvas3d/canvas3d_scene_gizmo.cpp)、[帧渲染](src/canvas3d/canvas3d_scene_render.cpp) | 复用同一 MlMeshData→GpuVertex 转换和归一化后的纹理共享键，实际缺失纹理报告警告并保留可绘制网格。WIC 原始纹理加载与场景共享缓存入口职责各异。可把 Gizmo 轴索引在拾取时绑定为有效轴类型。先/后相机状态不同。删除无消费者的 Impl `last_error`/`scene_last_error` 及写入链（`scene_render.cpp:905–906` 只比较和覆盖）。实际日志、警告去重、返回错误和 `ModelLoaderApi::last_error` 函数指针继续由各边界持有。 |
| [主窗口图像](src/main_window/background_image.cpp)、[文本预览](src/main_window/text_preview.cpp)、[启动](src/main_window/win32_dx11_bootstrap.cpp) | 亮度为 100 时直接使用已经验证的 RGBA 像素上传，其他亮度保留 alpha、像素结果和旧 SRV 失败回滚。背景 WIC COM 资源采用已有 RAII 类型。无编码声明时只让明确的 UTF-8 无效进入既有 CP932 路径，保留合法日文文本、BOM 和错误报告。检查 `CoInitializeEx` 的 HRESULT，成功包括 `S_FALSE`，释放与成功配对。 |
| [快照](src/main_window/map_snapshot_hydration.cpp)、[文档加载](src/main_window/map_load_pipeline.cpp)、[Inspector](src/main_window/element_inspector_data.cpp)、[作者消息](src/main_window/creator_messages.cpp) | ABI 不匹配直接写入现有错误状态，去掉本地 throw/catch 往返。合并同一条件的空分支，保留 preview/full registry 同词法、同源 hash 的合并证明。把 PutBetween 的 `structure key → path` 查询归到现有模型/列表更新 owner 后比较每帧耗时。作者消息 UI 共用合法条件，DLL 边界仍验证该数据。Inspector session 持有 Repeater 链重开草稿，按实际生命周期保留。 |

## 报告逐项对应

用于从报告原编号定位实施批次。组合项分别实施，采用下方取舍的判定而非报告原分级。

| 报告位置 | 归属 |
| --- | --- |
| A S1-1、S2-1/2/13/14/16 | 批次 1 的 Windows、内部回退和 Assimp、批次 4 的对话框与设置。 |
| A S2-3/4/5/17 | 批次 3 的范围合同、命中样板、已注册别名、排序。未知轨道进入行为决策。 |
| A S2-6～10 | 批次 4 的窗口、固定缓存形状、场景导航和动作分派。 |
| A S2-11/12/15，§7，§9.1 | 批次 2 的类型行族及语义分派，批次 5 的 Debug 重复、已证明的局部问题并入对应模块，编译器项先做。 |
| B S1-1～4 | Train 键、buffer、未知轨道进入决策。`parameterN` 和行元数据的等价部分进入批次 2。 |
| B S1-5～8 | 保留故障覆盖再迁移接缝，文件对话框／CLI 各自重写，文字按用户现有生命周期核实，计时改所有权。 |
| B S2-1～10 | 批次 3 的 2D 缓存、半径标签、命中、场景管线、索引和变换。Debug 自检归批次 5。 |
| B S2-11～17 | 对话框、编辑规则和设置归批次 4。Debug 分派/文件读归批次 5。Win32 路径与可达分支归批次 1。 |
| B S2-18～23 | 事务窗口保留，重解析按阶段保留。其余差异按批次 2 的数字/字段/路径键方案处理。 |
| B S2-24～28 | 批次 4 的表格工作。底色属于可见差异，Debug 指纹遗漏与生产缓存失效分别核验。 |
| B S2-29～34 | 批次 3 的共享几何与测量，批次 1 的模型错误边界和导出合同。 |

### A 候选和低收益项的处置

- **§8 第 1～4 项。** 现有 rotation 事件由有效 kind 和字段保证，Inspector 重绑前处理旧 draft。重复 edit ID 由来源定位/登记合同约束。2D 按 row index 回退需在实际 ID 脱节时检验，目前只记录候选。3D Repeater 局部更新不适用时 `edit_ledger.cpp:730` 安排全场景刷新，保留这两条作用域。若现有路线上能复现错误行或失败恢复，优先用唯一身份修正。
- **§8 第 5～8 项。** marker pipeline 依赖 Render 初始化顺序，重写到同一 owner。WIC primitive 与缓存上传完成路径各司其职。模型与 PutBetween 的两路准备先以请求时序、同一 key、实际重复量核实，线程完成协议单独修。`scene_models.at()` 只在已有登记的新任务清理作用域使用，先追踪该作用域的清理异常再调整。
- **§8 第 9～13 项。** 字符串数值转换、除零、下溢涉及解析兼容。现有 NaN 回归测试保留。`suggested_distance_expression_for_target` 的有意人工决议路径进入低风险收尾。头文件按真实使用与编译结果缩减，`<random>` 是 Parser 的实际依赖。分块 namespace 与版权注释照原职责保留。
- **§8 第 14～18 项。** 几何缓存淘汰、链线性扫描和只请求 segments 时的 chains 构造均先测。缓存条数上限与键是不同问题。Repeater 的字段和表格 cache 的键依批次 4 查实际写入/失效，保留不同 owner 的合法形状条件。
- **§8 第 19～22 项。** History 重读属于 Debug，Inspector session 有实际草稿调用。背景保留 SRV 的失败处理有效，在正确的 I/O 边界收敛 catch 和资源释放。WARP 是当前 Windows 支持路径，保留两个设备获取结果的可诊断性。

## 待确认的行为与 BVE 合规矩阵

本地官方页缓存更新于 **2026-11-13**，本次复核日期 2026-11-26，距更新 13 天。依 [BVE 合规技能](.agents/skills/komapedit-bve-format-compliance/SKILL.md) 使用该缓存。下表的诊断和兼容选择由执行者在开始对应改动前请项目负责人确认。可独立完成的机械删减和合同内重写依上面的顺序推进。

Map 的共同格式为 `BveTs Map 2.02`，可在 header 后无空格地加冒号和编码名，默认 UTF-8。语句以 `;` 结束，行注释使用 `#` 或 `//`。元素/函数名大小写不敏感，键按其格式和现有实现的独立合同处理。跨文件路径以拥有该语句/行的文件为基准。编辑合同同时验原编码、BOM、行结尾、Include 顺序及非目标行。

| 格式与官方合同 | 当前实现及建议 | 最小验收 |
| --- | --- | --- |
| Map `Train.Add(trainKey,filePath,trackKey,direction)`（4 参，无元素键）和 `Train[trainKey].Load(filePath,trackKey,direction)`（3 参，元素键）。`filePath` 是相对 Other-train 文件路径，方向 `-1` 对向／`1` 并走。Other-train header `BveTs Train 1.01`，`#`/`;` 注释，INI 式 `key=value`。缓存 `map.html:445–460`、`train.html`。 | 现有解析器的 `train.add` 漏 `K::Forbidden`，`dispatch_train` 静默跳过带键形状。当前官方形式仍各产生定义行。建议采用现有非法键诊断，再去掉分派层的静默跳过。进入编辑、创建或写回的官方形式继续用原有类型链路，带键 Add 作为非法句保留来源与诊断。 | 两个官方形式各产生一条定义行。带键 Add 产生定位到物理源的诊断、无伪造定义行。路径、顺序与同键情况下的后续 Enable/Stop 保持现有正确关联。参见 [Map 官方页](https://bvets.net/jp/edit/formats/route/map.html)、[他列车官方页](https://bvets.net/jp/edit/formats/route/train.html)。 |
| Sound/Sound3D 的资源表是 `BveTs Sound List 2.00`，`#` 注释，逗号分隔，声音名、相对表文件的 WAV 路径、可省略的同时播放数。官方只明确记录省略默认值 `1`。缓存 `sound.html:20–54`。 | `maploader_core.cpp:482–497` 静默把明确非法文本转为 `1`，编辑拒绝，等价检测又可能保留旧非法文本。建议确认空第三栏的兼容默认及显式非法行由诊断后继续保留、跳过还是拒绝。统一解析分类 `omitted`／合法正整数／非法，读取、编辑、语义比对用共同结果，按批准策略处理。只有确认行为后修改加载/编辑/回写。 | 分别验两列、省略栏、合法 1、其他正整数、零、负数、非数和溢出。含 `abc` 的旧行改成 `1` 时按照策略真正写回，未触碰的行保持文本，路径仍相对列表来源。参见 [Sound 官方页](https://bvets.net/jp/edit/formats/route/sound.html)。 |
| Map 结构 `Put`/`Put0`（9/3 参）、`PutBetween`（2/3 参），Repeater `Begin`/`Begin0`（10/4 个固定参数，随后是一个或多个结构键）与 `End()`，Signal `Put`（4/10 参）。官方规定放置轨道名，若明示 `0` 则为自轨。Put/Begin 的短式是对应空间偏移、旋转置零。位置单位 m、角度 deg、tilt 0–3、span 及 interval 用 m。资源键分别引用 Structure、Signal List。Structure List 与 Signal Aspects List 的官方版本均为 2.00，使用逗号分隔和 `#` 行注释，Signal 空首列与上一主行相邻表示辉光。缓存 `map.html:197–270,307–321`、`structure.html`、`signal.html`。 | 未知键回落自轨在 2D/3D 存在，GUI 还有 `annotate_scene_track_key_warnings` 和无效行标记，报告所称完全无诊断过宽。官方没有为未知键规定回落。空键、`1` 与其他兼容别名已有查找/覆盖规则，且各命令和两个轨道槽位不同。建议先确定是维持当前预览并准确诊断，还是显式跳过未知键的图元。统一 2D/3D 解析结果后保持存在的诊断、行身份、编辑入口、实际其他轨道布局和源语句。 | 覆盖 `0`、空键、可识别真实 `1` 轨道、有效其他轨道、未知键与两轨组合。逐项核对 2D/3D、导航、警告、`Put`/`Put0` 与 `Begin`/`Begin0` 往返。已登记别名的死分支按批次 3 独立删。参见 [Map 官方页](https://bvets.net/jp/edit/formats/route/map.html)、[结构表](https://bvets.net/jp/edit/formats/route/structure.html)、[信号表](https://bvets.net/jp/edit/formats/route/signal.html)。 |
| `parameterN` 是内部 typed edit 字段名。官方 Map 的 Section 位置式参数及各语句 arity 仍由各自源语句定义。 | 建议在单一字段入口拒绝前导零与不可表示的索引，明确诊断时点并确认是否保留旧的同值行为。既有 ABI 字段协议、语句变体、列表 cardinality 与非目标写回合同继续生效。 | 合法 `parameter0`、大索引、前导零、修改同值/异值、实际 source patch 和重解析核对。参见 [语义](src/maploader/maploader_semantic.cpp)、[写回](src/maploader/maploader_edits.cpp)。 |
| Map 注释可以消耗到换行。零实参的官方方法如 `Curve.End()`。数学函数的参数个数以 `map.html:569–618` 为准，`rand(value)` 为 `[0,value)`，`rand()` 为 `[0,1)`。 | 本次复核发现`parse_map_args` 已跳过注释，`parse_map_function` 再用 raw trim 推断 arity，可将只含注释的参数列表当一参。`call_function` 允许部分多余实参，`rand` 会将非法上界传给要求顺序的分布。分别提案。由 Lexer 直接给显式 arity。用单一分派核对函数实参数量和 `rand` 上界，保留有效随机求值及现有 NaN 几何合同。 | 真零参、只有 `#`/`//` 的零参、显式 `null`/空逗号槽、嵌套表达式、合法 `rand()`/`rand(value)`、余参、负上界／NaN、Include 下求值和源文本保持。参见 [Map 官方页](https://bvets.net/jp/edit/formats/route/map.html)、[解析器](src/maploader/maploader_parser.cpp)。 |

单独确认并验收的其余可见行为包括加载中的 Inspector Repeater 跳转、同里程 `Background.Change` 的显示优先级、Edit 模式下 Interpolate 的右键目标、Find Unused 是否改变下一次匹配模式、Signal 与其余列表的过期结果显示、所有事件表的 pending tint、列标题文本以及向导重复说明。针对 B S1-7，优先删除同一模板在行正文和 tooltip 的重复说明，保持语法/必填信息。编辑模式确认应准确表达 **Apply 更新内存，Save 才写盘**。保护现有确认动作。六语言同时更新并跑 `multilanguage_contract`。界面说明由 [用户手册](docs/user-manual.md) 持有。Assimp incomplete 的资产接收政策或改变 DLL 错误文本长度也用独立验收。ABI 迁移和扩大合法输入接受范围需明确设计决策。

## 验证步骤和输入

### 构建与非 Headless 合同

在对应批次开始前从普通 Debug 构建获得基线。已有工作树的 Debug cache 保留 Windows 工具链、生成器和依赖路径。普通构建用 `build_dev.bat`，严格构建在同一配置上打开两个开关。Release 专用于受到影响的运行时入口/打包及 Debug 隔离检查。

```powershell
cmd /d /c build_dev.bat
cmake -S . -B build -DKOMAPEDIT_STRICT_WARNINGS=ON -DBUILD_TESTING=ON
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
```

七项注册合同来自 [CMake](CMakeLists.txt)，为 `multilanguage_contract`、`typed_snapshot_contract`、`maploader_gradient_projection_contract`、`typed_edit_contract`、`maploader_diagnostics_contract`、`canvas3d_camera_contract` 与 `route_value_sampling_contract`。诊断夹具先按本机现有目录确认。专项负例优先加到能够直接重现问题的现有合同。区分解析/语义失败、夹具缺失、环境缺失和未达预算。构建保留足够时间，超时先查进程和输出，避免重叠编译。

### 使用指定的 Scenario 和 Map

真实 Scenario 的 `Route` 指向以下 Map。执行前用 Scenario 探针重新核对路径。真实文件只读。写回验证由已有工作流的临时副本完成，记录所有原始物理源的字节/哈希和副本清理结果。临时日志放入忽略的 build 子目录。

```powershell
$scenario = 'E:\Railway\BveTsWorkspace\BVE-Gensokyo-Railway\GSR\Timetables\++ 1-Gensokyo Loop Line Clockwise ++\121M-ATSP+Ps_Ask.txt'
$map = 'E:\Railway\BveTsWorkspace\BVE-Gensokyo-Railway\GSR\Scenarios_GSR\map\Config_Map121M-ATSP+Ps_Ask.txt'
$exe = (Resolve-Path -LiteralPath '.\build\komapedit.exe').Path
$run = 'before'
$evidence = (New-Item -ItemType Directory -Force -Path '.\build\slop-fix-261126').FullName

function Invoke-SlopHeadless([string]$Arguments, [string]$Name) {
    $log = Join-Path $evidence "$run-$Name.txt"
    $process = Start-Process -FilePath $exe -ArgumentList "$Arguments --headless-output `"$log`"" -Wait -WindowStyle Hidden -PassThru
    if ($process.ExitCode -ne 0 -or -not (Select-String -LiteralPath $log -Pattern '\bresult=PASS\b' -Quiet)) {
        throw "Headless failed ($($process.ExitCode)): $log"
    }
}

Invoke-SlopHeadless "--headless-load-scenario `"$scenario`"" 'scenario-load'
Invoke-SlopHeadless "--debug-headless-scenario-lifecycle `"$scenario`" --unit-distance 25" 'scenario-lifecycle'
```

保存和设置看生命周期日志的 `real_sources_unchanged=PASS`、`fixture_files_cleaned=PASS` 以及相应步骤标记。性能改动只选相应阶段。基线使用 `$run = 'before'`，修复后改成 `$run = 'after'`，两次使用相同参数和机器，不在计时段一起跑其他工作。示例参数固定 B 的 Plan/Scene 工作负载（Scene 使用默认自动 worker 数）及项目的 Edit 基准参数。若启用 `--profile-stages`，前后都启用并单列插桩结果。

```powershell
Invoke-SlopHeadless "--debug-headless-plan-bench `"$map`" --frames 60 --unit-distance 25 --pan-pixels 8 --interaction pan --max-frame-ms 16.667" 'plan-pan'
Invoke-SlopHeadless "--debug-headless-scene3d-bench `"$map`" --frames 30 --unit-distance 25 --window-back-m 100 --window-forward-m 1200 --interaction stationary --max-frame-ms 16.67" 'scene-stationary'
Invoke-SlopHeadless "--debug-headless-table-cache-bench `"$map`" --repeat 5 --unit-distance 25" 'table-cache'

foreach ($sceneState in 'off', 'on') {
    foreach ($sample in 1..3) {
        Invoke-SlopHeadless "--debug-headless-edit-bench `"$map`" --scene $sceneState --repeat 5 --unit-distance 25" "edit-$sceneState-$sample"
    }
}
```

- 验证范围只用上述真实场景/地图作为 Headless 输入。自动点击模式排除，包含注入 ImGui 按键事件的 `--interaction edit-drag` 和 `--debug-headless-own-track-edit`。需要命中或失败分支的证据时，调用生产数据入口的合同可基于指定 Map 及其隔离临时样本。纯夹具 Headless 模式可作为现有证据或开发入口参考。
- Plan 对照缓存命中、轨/标记数量、起终点、样本顺序、选取、Fit extents 与对应帧阶段。2D 优化前后分别记录构建和绘制。Scene 对照实际模型成功/失败数、可见实例、拾取 ID、像素、模型/纹理缓存与相机状态。表格比较冷构建、热命中、绘制与完整签名。Edit 分开验状态/磁盘与 time stages。用中位数、p95 和未改变的严格门槛判定。整体 p95、嵌套 CPU 阶段、GPU 时间和窗口 FPS 分别报告。
- 报告历史基线为 7/7 CTest、Scenario 56 个真实物理源保持。B 的 60 帧 Plan 为 median `1.132 ms`、p95 `1.603 ms`，30 帧 Scene 为 `1.475 ms`、`1.682 ms`。场景成功 534/534 个模型并报告路线缺 `Slab.png`。这些是已有日志，实施者用相同输入、参数和构建重新测量。参见 [A 生命周期日志](local_docs/slop-audit-A/headless-scenario-lifecycle.out.txt)、[B 计划图日志](local_docs/slop-audit-B/validation/plan-bench.txt)、[B 场景日志](local_docs/slop-audit-B/validation/scene3d-bench.txt)。
- 效果表的 dock、窗口位置、右键、长按、六语排版、打开/取消/保存对话框、真实鼠标拖拽和视觉效果记录为人工验收。提交前检查改动范围、UTF-8、空白、生产行数、非目标结果、公开符号与打包布局，记下实际未验证的项。

本轮证据来自当前源码、归档构建/测试日志、官方页缓存以及指定 Scenario 的 `Route` 字段。本计划为本轮唯一文件变更，构建和运行验证由实施阶段执行。

建议提交说明 `Docs: Add the verified slop fix plan`。
