# 开发者指南

[English](dev.md) · [用户指南](README_zhcn.md) · [路线图](../TODO.md) · [AI 辅助开发](ai-dev_zhcn.md)

本文档说明以人工方式开发 komapedit 时所需的环境、架构、规范与验证流程。如果变更中使用了 AI 编程工具，还必须遵守 [`ai-dev_zhcn.md`](ai-dev_zhcn.md) 和 [`AGENTS.md`](../AGENTS.md)。

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
- 不得内置 Assimp；ImGui 使用 docking 分支，ImPlot 使用上游版本。
- 不得删除或绕过许可证/声明文件。新增依赖时同步更新 CMake、开发者文档和第三方声明。
- 线路发布导出与 `build_release.bat` 相互独立；实现时必须展开 Include、可选常量化表达式、仅复制已用资源、写报告，并通过临时输出保护开发目录。

## 变更流程

1. 复现或定义行为，检查所有相关所有者和调用方。
2. 确定组件边界和兼容义务。
3. 完成聚焦变更，不混入无关格式化或架构调整。
4. 同步更新所有受影响的界面语言、ABI 调用方、测试与文档。
5. 优先构建 Debug，并运行覆盖完整的最小静态、自动、无界面与手工验证集。
6. 仅在打包、分发、线路导出或 Release 特有行为变化时增加 Release 验证。
7. 检查差异中的生成文件、本机路径、源码保真、所有权、失效和声明变化。
8. 记录实际测试、跳过项、限制和剩余风险。

