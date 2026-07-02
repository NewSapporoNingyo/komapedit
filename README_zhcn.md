# komapedit

## 项目概述

komapedit 是一个面向 BVE Trainsim 地图文件的轻量级查看与编辑工具，基于 `kobushi-trackviewer` 的轨道几何计算思路改写为 C++/Win32 桌面程序。当前版本主要提供地图读取、轨道几何生成、2D 平面图、坡度/曲线半径图、信息表格查看、布景模型与地图场景 3D 预览和轨道几何 CSV 导出能力。

程序由三个运行时核心部分组成：

- `maploader.dll`：读取 `BveTs Map` 文件、解析部分 BVE Map 语法、生成自轨道/他轨道几何数据，并输出中间 JSON。
- `model_loader.dll`：通过 Assimp 读取布景模型文件，并向 3D 预览提供网格/材质数据。
- `komapedit.exe`：基于 Dear ImGui、ImPlot、Win32、DirectX 11 和 WIC 的桌面 GUI。

当前项目更接近“地图查看器/轨道几何可视化工具/数据检查器/3D 预览工具”。地图对象的完整编辑、3D 场景编辑和声音/环境效果编辑仍在开发计划中。

## 开发状况（TODO List）

### 地图读取与解析

- [x] 读取 `BveTs Map 2.0+` 地图文件
- [x] 支持 UTF-8、UTF-8 BOM、UTF-16LE、UTF-16BE、CP932/Shift_JIS 等文本编码处理
- [x] 支持 `Include` 引用其他地图文件
- [x] 支持 `$变量 = 表达式;`、`distance` 预定义变量和基础数学函数
- [x] 支持 `#`、`//` 注释
- [x] 支持异步加载地图，并在控制台窗口显示加载日志、警告和错误
- [ ] 支持编辑地图文件中的元素
- [ ] 保存修改的地图文件

### 自轨道与他轨道几何

- [x] 解析并计算自轨道曲线
- [x] 解析并计算自轨道坡度
- [x] 支持旧式语法
- [x] 解析并计算他轨道位置、横向/纵向插值、轨距、中心、超高等部分信息
- [x] 支持控制点范围和间隔设置，并可重新生成几何
- [x] 支持限速区间读取与显示
- [ ] 自轨道曲线编辑
- [ ] 自轨道坡度编辑

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
- [x] 支持车站跳转
- [x] 支持导入背景图，并调整位置、尺寸、旋转角和亮度
- [x] 支持使用两个车站位置对齐背景图
- [x] 平面图上的布景与连续布景位置标记
- [x] 平面图上显示信号位置标记
- [x] 平面图上显示应答器位置标记
- [x] 平面图上显示先行列车通过点标记
- [x] 平面图上显示轨道变位和粘着特性变化点标记
- [x] 平面图上显示音效播放、固定音源、走行音、轮缘摩擦音效和道岔音效标记
- [x] 平面图上显示背景变化点标记
- [x] 平面图上显示驾驶台亮度变化点标记
- [x] 平面图上显示雾效果变化点标记

### 地图信息表示

- [x] 读取 `Station.Load` 指定的车站列表 CSV
- [x] 显示车站列表与地图中 `Station.Put` 的放置位置
- [x] 显示他轨道列表，可切换显示、设置范围和颜色
- [x] 显示 `Structure.Put`、`Structure.Put0`、`Structure.PutBetween` 的地图布景放置表
- [x] 读取并显示 `Structure.Load` 指定的布景模型列表（`.txt` 或 `.csv`）
- [x] 显示 `Repeater.Begin`、`Repeater.Begin0`、`Repeater.End` 的连续布景表，并合并 Begin/End 距离
- [ ] 布景模型列表编辑
- [ ] 车站列表与车站位置编辑
- [x] 显示 `信号现示列表`、`地图信号列表` 和 `应答器列表`
- [x] 显示 `轨道变位列表`、`粘着特性变化点列表`、走行音、轮缘摩擦音效和道岔音效相关表格
- [x] 显示 `背景变化点列表`、`驾驶台亮度变化点列表` 和 `雾效果变化点列表`
- [ ] 信号和应答器列表编辑

### 3D画布

- [x] 布景模型 3D 预览
- [x] 通过 `model_loader.dll`/Assimp 读取模型和贴图
- [x] 支持旋转和缩放布景模型预览
- [x] 3D 画布场景预览，可显示轨道路径、布景、连续布景和背景变化
- [x] 可通过车站跳转移动 3D 场景相机，并在平面图上显示当前 3D 位置
- [ ] 3D 画布中的布景编辑功能

### 环境效果

- [x] 显示 `音效文件列表`、`3D音效文件列表`、`地图音效列表` 和 `地图3D音效列表`
- [ ] 走行音、道岔音、报站音和 3D 声音编辑
- [ ] 驾驶台亮度设定位置编辑
- [ ] 雾效果编辑

### 用户界面与辅助功能

- [x] Dear ImGui Docking 多窗口布局
- [x] 简体中文、英文、日文界面语言切换
- [x] 字体大小、组件大小、车站标记大小和主题色设置
- [x] 最近打开地图历史记录
- [x] 背景图参数随最近地图保存到 `history.ini`
- [x] 设置保存到程序目录下的 `settings.ini`
- [x] 将自轨道和他轨道几何导出为 CSV

## 安装与启动

当前仓库未提供发行版本，推荐从源代码构建后运行。

构建完成后，请确保 `komapedit.exe`、`maploader.dll`、`model_loader.dll` 和构建复制的 Assimp 运行时 DLL 位于同一目录，然后运行 `build_release\komapedit.exe`。

程序启动后会在可执行文件同目录创建或读取：

- `imgui.ini`：用户界面内的窗口位置等信息
- `settings.ini`：保存设置：界面语言、字体大小、组件大小、车站标记大小、主题色
- `history.ini`：最近打开地图和背景图对齐参数

## 使用方法

1. 通过菜单 `文件 -> 打开...` 或工具栏 `打开` 选择`.txt`地图文件
2. 地图加载完成后，中央窗口显示平面图、纵断面图和曲线半径图
3. 在平面图中：
   - 左键拖动平移
   - 鼠标滚轮缩放
   - 按住 `Shift` 滚轮旋转，或使用鼠标右键/`Ctrl + 左键` 拖动旋转
   - 双击平面图恢复自适应范围
4. 在工具栏的“车站跳转”中选择车站，可定位到对应里程
5. 在 `2D 视图` 菜单中切换 2D 视图窗口、坡度图、曲线半径图、坡度叠加、纵断面其他轨道和背景图相关操作。
6. 在 `辅助信息` 菜单中切换车站、轨道几何、信号、音效、效果和 3D 场景辅助标记。信号标记通过 `地图信号列表` 中的“显示”勾选框控制。
7. 在 `模式` 中选择“测量”，在轨道附近移动或双击，可查看里程、标高、坡度、曲线半径和限速
8. 在 `地图信息` 菜单中打开车站、轨道、布景、连续布景、信号、应答器、音效、轨道变位/粘着、背景、驾驶台亮度和雾效果相关表格。有平面位置的行可定位到平面图；模型和音效文件行可打开关联文件。
   - `信号现示列表`：查看信号现示定义
   - `地图信号列表`：查看信号位置，并使用行内“显示”勾选框控制平面图标记
   - `应答器列表`：查看应答器位置
   - `音效文件列表` 和 `3D音效文件列表`：查看已加载的音效文件条目、查找匹配 key、查找未使用条目，并打开关联文件
   - `地图音效列表`、`地图3D音效列表`、`走行音变化点列表`、`轮缘摩擦音效变化点列表` 和 `道岔音效播放点列表`：查看音效播放/变化位置，并定位到平面图
   - `轨道变位列表`、`粘着特性变化点列表`、`背景变化点列表`、`驾驶台亮度变化点列表` 和 `雾效果变化点列表`：查看对应变化点表
   - `其他轨道`：切换他轨道显示、调整显示范围和颜色
   - `车站列表`：查看车站列表和 `Station.Put` 放置数据
   - `地图布景列表`：查看地图中的 `Structure.Put`、`Structure.Put0`、`Structure.PutBetween`
   - `布景模型列表`：查看 `Structure.Load` 指定列表中的 structureKey 和模型文件；右键 structureKey 并选择 `预览模型` 可打开 3D 模型预览
   - `连续布景列表`：查看 `Repeater.Begin/End`
9. 在 `2D 视图 -> 背景图` 中导入背景图，可手动调整位置、尺寸、旋转和亮度，也可按两个车站对齐
10. 在 `3D 视图 -> 布景模型预览` 中显示或隐藏布景模型预览窗口。预览窗口内可用鼠标左键拖动旋转模型，用鼠标滚轮缩放
11. 在 `3D 视图 -> 3D场景预览` 中显示场景预览窗口，然后点击 `启动3D场景预览`。场景预览可在窗口中重新加载或关闭；加载场景后，车站跳转也会移动场景相机。
12. 在 `文件 -> 导出 CSV...` 中选择输出目录，导出自轨道和他轨道几何 CSV
13. 按 `F5` 或菜单 `文件 -> 重新加载` 可重新读取当前地图

## 项目文件结构

```text
komapedit/
├─ CMakeLists.txt                  # CMake 构建配置
├─ README.md                       # 项目说明
├─ LICENSE                         # Apache License 2.0
├─ NOTICE                          # 项目版权与 Apache 归属声明
├─ THIRD_PARTY_NOTICES.md          # 第三方库和参考项目声明
├─ build_dev.bat                   # Debug 构建脚本
├─ build_release.bat               # Release 构建脚本
├─ clear_build_release_dist.bat    # 清理 Release 目录，保留可执行文件、DLL 和声明文件
├─ get_3rd_party_packages.bat      # 拉取 ImGui 和 ImPlot
├─ install_Assimp.bat              # 使用 vcpkg 安装 Assimp 的辅助脚本
├─ include/
│  ├─ canvas3D.h                   # 3D 预览画布接口
│  ├─ maploader.h                  # maploader C ABI
│  ├─ model_loader.h               # model_loader C ABI
│  └─ multilanguage.h              # 界面多语言文本
├─ src/
│  ├─ main_window/
│  │  ├─ gui_kme.cpp               # 主窗口、Win32/DirectX 11 初始化和主循环
│  │  ├─ app_settings.cpp/.h       # 运行时设置、历史记录和 UI 样式辅助代码
│  │  ├─ debug_headless.cpp/.h     # 仅 Debug 构建使用的 headless 验证入口
│  │  └─ kme.h                     # App 声明与 GUI 共享状态
│  ├─ table/
│  │  ├─ datatable.cpp             # 数据表格列定义、缓存与表格窗口
│  │  └─ table_navigation.cpp      # 表格到平面图标记的定位与可见状态
│  ├─ canvas2d/
│  │  ├─ canvas2D.cpp              # 2D 平面画布、标记、测量和背景图
│  │  └─ profile_plots.cpp         # 纵断面和曲线半径图渲染
│  ├─ canvas3d/
│  │  └─ canvas3D.cpp              # DirectX 11 模型和场景预览画布渲染
│  ├─ maploader/
│  │  ├─ maploader.cpp             # BVE Map 解析、IR 组装、JSON 导出和几何生成
│  │  ├─ text_decoder.cpp/.h       # 文件读取、UTF-8 路径和文本解码
│  │  ├─ diagnostics.cpp/.h        # 加载器日志与最后错误状态
│  │  └─ c_api.cpp/.h              # C ABI 分配辅助代码
│  └─ model_loader/
│     └─ model_loader.cpp          # 基于 Assimp 的布景模型加载
├─ third_party/
│  ├─ imgui/                       # Dear ImGui，docking 分支
│  └─ implot/                      # ImPlot
├─ build/                          # Debug 构建输出（生成目录）
└─ build_release/                  # Release 构建输出（生成目录）
```

## 从源代码构建

### 环境要求

- Windows操作系统
- [CMake](https://cmake.org/) 3.20 或更新版本
- [Ninja](https://github.com/ninja-build/ninja)
- 支持 C++17 的编译器，例如 MSVC 或 [MinGW](https://www.mingw-w64.org/)
- Windows SDK / DirectX 11 / WIC 开发库
- Git，用于拉取第三方依赖
- Assimp，需能被 CMake 作为 `assimp::assimp` 找到

### 获取第三方依赖：get_3rd_party_packages.bat

该脚本会克隆以下2个项目（请确保Git已安装）：

- `third_party/imgui`→`ocornut/imgui` 的 `docking` 分支。
- `third_party/implot`→`epezent/implot`。

克隆得到的第三方源码目录会被 Git 忽略。其上游许可证文件保留在`third_party/` 目录中，随项目分发所需的声明汇总在`THIRD_PARTY_NOTICES.md`。

### 安装Assimp：install_Assimp.bat
Assimp 不放在 `third_party/` 目录中，需要在配置项目前单独安装。当前构建脚本在设置了 `VCPKG_ROOT` 时会自动使用 vcpkg；如果未设置 `VCPKG_DEFAULT_TRIPLET`，默认使用 `x64-mingw-dynamic`。
`install_Assimp.bat` 是用于通过 vcpkg 安装 `assimp:x64-mingw-dynamic` 的辅助脚本，使用前需要手动编辑脚本，在其中填入本地的vcpkg所在的目录。

### Debug 构建：build_dev.bat

输出目录为 `build`。

### Release 构建：build_release.bat

输出目录为 `build_release`。主要产物：

- `komapedit.exe`
- `maploader.dll`
- `model_loader.dll`
- Assimp 运行时 DLL，具体取决于所选工具链/包管理器
- `LICENSE`
- `NOTICE`
- `THIRD_PARTY_NOTICES.md`

如需整理发布目录，可执行 `clear_build_release_dist.bat`。该脚本会保留 `komapedit.exe`、所有 `.dll` 文件（包括 `maploader.dll`、`model_loader.dll`、Assimp 运行时 DLL 及其已复制的依赖 DLL），以及 `LICENSE`、`NOTICE` 和 `THIRD_PARTY_NOTICES.md`。


## 附录：CSV 数据格式

### 自轨道几何 CSV（导出）

通过 `文件 -> 导出 CSV...` 导出。文件名格式：

```text
<导出目录名>_owntrack.csv
```

表头：

```csv
#distance,x,y,z,direction,radius,gradient,interpolate_func,cant,center,gauge
```

字段说明：

| 字段 | 说明 |
| --- | --- |
| distance | 地图绝对距离，单位 m |
| x | 自轨道计算后的平面 X 坐标 |
| y | 自轨道计算后的平面 Y 坐标 |
| z | 标高 |
| direction | 轨道方向角，单位 rad |
| radius | 当前曲线半径 |
| gradient | 当前坡度，按 BVE 的千分率语义处理 |
| interpolate_func | 插值类型，`0` 表示 `sin半波长递减`，`1` 表示 `线性递减` |
| cant | 超高 |
| center | 轨道中心偏移 |
| gauge | 轨距 |

### 他轨道几何 CSV（导出）

每条他轨道会导出一个 CSV。文件名格式：

```text
<导出目录名>_<trackKey>.csv
```

表头：

```csv
#distance,x,y,z,interpolate_func,cant,center,gauge
```

字段说明：

| 字段 | 说明 |
| --- | --- |
| distance | 地图绝对距离，单位 m |
| x | 他轨道计算后的平面 X 坐标 |
| y | 他轨道计算后的平面 Y 坐标 |
| z | 他轨道标高 |
| interpolate_func | 插值类型，`0` 表示 `sin`，`1` 表示 `line` |
| cant | 超高 |
| center | 轨道中心偏移 |
| gauge | 轨距 |

导出的数值使用固定 6 位小数。当前 CSV 导出仅包含轨道几何，不导出车站、布景、连续布景、信号、应答器、音效、背景变化点、驾驶台亮度变化点、雾或 3D 场景数据。

## 版权、许可和第三方声明

komapedit 以 Apache License, Version 2.0 分发。许可证全文见 `LICENSE`，
项目版权与归属声明见 `NOTICE`。

本项目基于 `kobushi-trackviewer` 开发，用于辅助查看和编辑 BVE Trainsim
地图文件。

参考项目：

| 项目 | 版权 | 许可证 |
| --- | --- | --- |
| konawasabi 的 [kobushi-trackviewer](https://github.com/konawasabi/kobushi-trackviewer) | Copyright (c) 2021-2024 konawasabi | Apache License, Version 2.0 |

GUI 和模型预览使用的第三方库：

| 库 | 用途 | 版权 | 许可证 |
| --- | --- | --- | --- |
| [Dear ImGui](https://github.com/ocornut/imgui) | Docking GUI、Win32 后端、DirectX 11 后端、C++ std::string 辅助模块 | Copyright (c) 2014-2026 Omar Cornut | MIT License |
| [ImPlot](https://github.com/epezent/implot) | 2D 图表控件 | Copyright (c) 2020 Evan Pezent | MIT License |
| [Assimp / Open Asset Import Library](https://github.com/assimp/assimp) | 布景模型导入 | Copyright (c) 2006-2026, assimp team | Modified BSD 3-Clause License |
| Dear ImGui 随附的 stb 单文件库 | Dear ImGui 使用的字体、文本编辑、矩形打包支持 | Copyright (c) 2017 Sean Barrett | MIT License 或 Public Domain |

分发本仓库源码或由本仓库构建的二进制文件时，请一并包含 `LICENSE`、
`NOTICE` 和 `THIRD_PARTY_NOTICES.md`。如果分发 `third_party/` 源码目录，
请保留其中原始许可证文件和版权声明。

项目在线地址：<https://github.com/NewSapporoNingyo/komapedit>

## Star History

<a href="https://www.star-history.com/?repos=NewSapporoNingyo%2Fkomapedit&type=date&legend=top-left">
 <picture>
   <source media="(prefers-color-scheme: dark)" srcset="https://api.star-history.com/chart?repos=NewSapporoNingyo/komapedit&type=date&theme=dark&legend=top-left" />
   <source media="(prefers-color-scheme: light)" srcset="https://api.star-history.com/chart?repos=NewSapporoNingyo/komapedit&type=date&legend=top-left" />
   <img alt="Star History Chart" src="https://api.star-history.com/chart?repos=NewSapporoNingyo/komapedit&type=date&legend=top-left" />
 </picture>
</a>
