<p align="center">
    <img src="../icons/titleimage.png" alt="komapedit" width="600">
</p>

# komapedit

komapedit 是一款面向 Windows 的轻量级 BVE Trainsim 地图查看与编辑工具，支持二维线路图、三维场景预览和基于源码的地图编辑。

编辑功能处于实验阶段，使用前请备份线路文件或使用版本控制管理。各类语句的支持范围见[支持的 BVE 地图语法](#支持的-bve-地图语法)，开发进度见 [TODO.md](../TODO.md)。

## 文档导航

- [用户手册](user-manual_zhcn.md)
- [开发进度（待办事项列表）](../TODO.md)
- [开发者指南](dev_zhcn.md)
- [AI 辅助开发指南](ai-dev_zhcn.md)
- [供 AI 编程工具使用的仓库规范](../AGENTS.md)
- [许可证](../LICENSE)、[项目声明](../NOTICE)与[第三方声明](../THIRD_PARTY_NOTICES.md)

## 主要功能

- **界面**：可拖动、停靠的多窗口布局，提供数据表格搜索，以及表格、二维图和三维场景之间的定位跳转。
- **预览**：显示线路平面图、纵断面图、辅助标记、布景模型和三维场景；支持背景图对齐、测量和轨道几何 CSV 导出。
- **编辑**：通过属性窗口、资源列表和新建向导修改、删除或添加受支持的地图元素，使用三维操纵器调整部分元素的位置；支持子地图管理，以及地图、Scenario 和资源列表文件的创建与编辑。
- **界面语言**：简体中文、繁体中文、英语和日语。语言菜单中的 `中文` 子菜单提供 `简体`、`台湾繁體` 和 `香港繁體`。

## 支持的 BVE 地图语法

- 读取/预览：解析数据，并通过轨道几何、表格、标记或 3D 场景显示。
- 基本编辑：通过属性窗口、表格或文件操作菜单修改已有内容并保存。
- 新建：通过地图元素向导、新建文件向导、文件结构图或资源表格创建语句、文件及列表行。
- 图形化编辑：在 2D/3D 画布中直接拖动元素或使用操纵器修改。
- √ = 支持本行列出的功能和形式；△ = 部分形式、字段或关联操作可用；✕ = 暂不支持；- = 不适用。

下表按程序现有的全部界面入口评定；同一行列出多个重载或别名时，仅能新建其中一部分形式记为 △。`*.Load` 行同时涵盖引用路径、列表文件和列表内容，具体范围见说明。语法包括 [BVE 官方地图语法](https://bvets.net/jp/edit/formats/route/map.html)、旧式别名及明确标注的项目兼容语句或注释。

| 地图语法                                                                                                                                                                                    | 读取/预览 | 基本编辑 | 新建 | 图形化编辑 | 说明 |
| ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | :---: | :------: | :------: | :--------: | -------------------------------------------------------------------------------------------------------------- |
| 文件头、版本及编码                                                                                                                                                                          | △ | ✕ | △ | - | 支持 BVE Map 2.0+ 及 UTF-8、UTF-16LE/BE、CP932/Shift_JIS；新建地图使用 `BveTs Map 2.02:utf-8` 文件头 |
| 注释及基本语句结构                                                                                                                                                                          |   √   |    ✕     |    -     |     -      | 支持 `#`/`//` 注释、分号分隔、带 key 及嵌套元素、多行语句；名称大小写不敏感 |
| [项目注释] `//--kme--message-from-creator:"content"` | √ | √ | √ | - | 使用普通 BVE 注释保存消息；默认在打开地图时展示，也可在自定义消息表中编辑或删除，在“其它”向导中新建 |
| 赋值、参数及 key 中的变量                                                                                                                                                                   | √ | ✕ | ✕ | - | 解析时求值；只读变量表按名称分组显示赋值及来源 |
| 算术运算符（`+`、`-`、`*`、`/`、`%`）                                                                                                                                                       | √ | △ | △ | - | 支持数值运算、单目正负号、括号及 `+` 字符串拼接；编辑或新建元素时，可在里程表达式确认流程中输入数值表达式 |
| 距离声明及 `distance` 表达式                                                                                                                                                                | √ | △ | △ | △ | 随受支持元素编辑或新建距离块；按提示填写里程表达式，求值须与目标里程一致；部分 3D 对象可通过 Z 轴按整米调整里程 |
| 数学函数                                                                                                                                                                                    | √ | △ | △ | - | 支持 `rand`、`abs`、`sin`、`cos`、`atan2`、`sqrt`、`exp`、`log`、`floor`、`ceil`、`pow`；可在里程表达式确认流程中使用 |
| `include 'file';`                                                                                                                                                                           | √ | √ | √ | - | 文件结构图可更换、解除、导入或新建子地图引用，新建文件向导也可添加 Include；缺失或无效的子地图会跳过并记录警告 |
| `Curve.SetGauge(value)` / `[旧式] Curve.Gauge(value)`                                                                                                                                       | √ | √ | △ | ✕ | 编辑里程与轨距；向导新建 `SetGauge`，默认 `1.067`；2D/3D 可显示 `CG` 标牌 |
| `Curve.SetCenter(x)`                                                                                                                                                                        |   √   |    √     |    √     |     ✕      | 编辑里程与超高中心偏移；新建默认值为 `0`；2D/3D 可显示 `CC` 标牌 |
| `Curve.SetFunction(id)`                                                                                                                                                                     |   √   |    √     |    √     |     ✕      | 编辑里程与插值函数，`id` 为 `0` 或 `1`；新建默认 `0`；2D/3D 可显示 `CF` 标牌 |
| `Curve.BeginTransition()`                                                                                                                                                                   |   √   |    △     |    △     |     ✕      | 已有缓和段通过配对 Begin/End 编辑里程；新建时随带超高的 Begin 或 End 添加，起止缓和选项联动、里程分别设置 |
| `Curve.Begin(radius, cant)` / `[旧式] Curve.BeginCircular(radius, cant)`                                                                                                                    |   √   |    √     |    △     |     ✕      | 向导将带超高的 Begin 与前置缓和曲线一同添加；旧式 BeginCircular 支持已有语句编辑 |
| `Curve.Begin(radius)` / `Curve.Change(radius)`                                                                                                                                              |   √   |    √     |    √     |     ✕      | 向导可选择 Begin 或 Change，并一同添加结束位置 |
| `Curve.End()`                                                                                                                                                                               |   √   |    √     |    √     |     ✕      | 可单独新建或随起点添加，也可设置前置缓和曲线 |
| `Curve.Interpolate(radius, cant)` / `Curve.Interpolate(radius)` / `Curve.Interpolate()`                                                                                                     |   √   |    √     |    √     |     ✕      | 支持 0/1/2 参数形式，编辑时保留参数个数；开启“曲线半径”后可从 2D/3D 标记编辑或删除 |
| `Gradient.BeginTransition()`                                                                                                                                                                |   √   |    △     |    △     |     ✕      | 已有缓和段通过配对 Begin/End 编辑里程；新建时随 Begin/End 添加，起止缓和选项联动、里程分别设置 |
| `Gradient.Begin(gradient)` / `[旧式] Gradient.BeginConst(gradient)`                                                                                                                         | √ | √ | △ | ✕ | 向导可单独添加 Begin，或一同添加 End、前置缓和曲线；旧式 BeginConst 支持已有语句编辑 |
| `Gradient.End()`                                                                                                                                                                            |   √   |    √     |    √     |     ✕      | 可单独新建或随 Begin 添加，也可设置前置缓和曲线 |
| `Gradient.Interpolate(gradient)` / `Gradient.Interpolate()`                                                                                                                                 |   √   |    ✕     |    ✕     |     ✕      | 0/1 参数形式均用于轨道几何计算 |
| `Legacy.Turn`、`Legacy.Curve`、`Legacy.Pitch`                                                                                                                                               |   √   |    △     |    ✕     |     ✕      | 项目兼容语句，均用于自轨道几何；已有 Legacy.Curve 可编辑数值与里程 |
| `Track[trackKey].X.Interpolate(x, radius)` / `Track[trackKey].X.Interpolate(x)` / `Track[trackKey].X.Interpolate()`                                                                         |   √   |    △     |    √     |     ✕      | 支持各参数形式；可编辑里程、数值或删除。单条 `trackKey` 只读，可在“其他轨道”表统一改名 |
| `Track[trackKey].Y.Interpolate(y, radius)` / `Track[trackKey].Y.Interpolate(y)` / `Track[trackKey].Y.Interpolate()`                                                                         |   √   |    △     |    √     |     ✕      | 支持各参数形式；编辑范围与 X.Interpolate 相同 |
| `Track[trackKey].Position(x, y, radiusH, radiusV)` / `Track[trackKey].Position(x, y, radiusH)` / `Track[trackKey].Position(x, y)`                                                           |   √   |    △     |    √     |     ✕      | 支持各参数形式；可编辑里程、数值或删除，保留参数个数；key 改名范围同 X.Interpolate |
| `Track[trackKey].Cant.SetGauge(gauge)` / `[旧式] Track[trackKey].Gauge(gauge)`                                                                                                              | √ | △ | △ | ✕ | 新建使用 Cant.SetGauge；两种形式均可编辑数值、里程或删除；key 改名范围同 X.Interpolate |
| `Track[trackKey].Cant.SetCenter(x)`                                                                                                                                                         |   √   |    △     |    √     |     ✕      | 可编辑数值、里程或删除；key 改名范围同 X.Interpolate |
| `Track[trackKey].Cant.SetFunction(id)`                                                                                                                                                      |   √   |    △     |    √     |     ✕      | 新建时 `id` 为 `0` 或 `1`；可编辑数值、里程或删除；key 改名范围同 X.Interpolate |
| `Track[trackKey].Cant.BeginTransition()`                                                                                                                                                    |   √   |    △     |    √     |     ✕      | 可编辑里程或删除；key 改名范围同 X.Interpolate |
| `Track[trackKey].Cant.Begin(cant)`                                                                                                                                                          |   √   |    △     |    √     |     ✕      | 可编辑数值、里程或删除；key 改名范围同 X.Interpolate |
| `Track[trackKey].Cant.End()`                                                                                                                                                                |   √   |    △     |    √     |     ✕      | 可编辑里程或删除；key 改名范围同 X.Interpolate |
| `Track[trackKey].Cant.Interpolate(cant)` / `Track[trackKey].Cant.Interpolate()` / `[旧式] Track[trackKey].Cant(cant)`                                                                       | √ | △ | △ | ✕ | 新建使用 Interpolate 的 0/1 参数形式；已有语句可编辑数值、里程或删除，保留参数个数；key 改名范围同 X.Interpolate |
| `Structure.Load(filePath)`                                                                                                                                                                  | √ | √ | √ | - | 可更换列表路径、新建或导入列表并添加 Load 引用；表格支持编辑、增删和排序 key/path 行，包括空列表第一行；兼容列表版本 1.00+ |
| `Structure[structureKey].Put(trackKey, x, y, z, rx, ry, rz, tilt, span)`                                                                                                                    |   √   |    √     |    √     |     △      | 全部字段可编辑；3D 操纵器调整 X/Y/Z |
| `Structure[structureKey].Put0(trackKey, tilt, span)`                                                                                                                                        |   √   |    √     |    √     |     △      | 属性窗口可在 Put0/Put 间转换；Put0 的 Z 轴操纵器按整米调整里程 |
| `Structure[structureKey].PutBetween(trackKey1, trackKey2, flag)` / `Structure[structureKey].PutBetween(trackKey1, trackKey2)`                                                               | √ | √ | △ | △ | 两种形式均可编辑，新建使用 3 参数形式；属性草稿实时更新 3D 形状，Z 轴操纵器按整米调整里程 |
| `Repeater[repeaterKey].Begin(trackKey, x, y, z, rx, ry, rz, tilt, span, interval, structureKey1, ...)` / `Repeater[repeaterKey].Begin0(trackKey, tilt, span, interval, structureKey1, ...)` | √ | √ | √ | △ | 可编辑全部参数、结构键列表及整链 key；支持 Begin/Begin0 转换、变化点、关联删除和位置操纵器；可单独或与 End 成对新建，同名成对区间须避免重叠 |
| `Repeater[repeaterKey].End()`                                                                                                                                                               |   √   |    △     |    √     |     △      | 通过所属 Begin 编辑结束里程或使用 End 操纵器；可单独或成对新建，孤立 End 无独立属性窗口；同名闭合区间内禁止重复添加 End |
| `Background.Change(structureKey)`                                                                                                                                                           |   √   |    √     |    √     |     ✕      | 编辑里程与 key；在场景中预览背景 |
| `Station.Load(filePath)`                                                                                                                                                                    | √ | √ | √ | - | 可更换列表路径、新建或导入列表并添加 Load 引用；表格支持编辑 13 个字段、增删和排序车站行，包括空列表第一行；兼容列表版本 0.04+ |
| `Station[stationKey].Put(door, margin1, margin2)`                                                                                                                                           |   √   |    √     |    √     |     ✕      | 编辑里程、key、车门侧及停车余量；余量要求 `margin1 < 0`、`margin2 > 0` |
| `Section.Begin(...)` / `[旧式] Section.BeginNew(...)`                                                                                                                                       |   √   |    √     |    √     |     ✕      | 编辑里程与信号索引列表；2D/3D 显示标记 |
| `Section.SetSpeedLimit(...)` / `[旧式] Signal.SpeedLimit(...)`                                                                                                                              |   √   |    √     |    √     |     ✕      | 编辑里程与限速列表；生效值显示在 3D 信号摘要中 |
| `Signal.Load(filePath)`                                                                                                                                                                     | √ | △ | √ | - | 可更换、新建或导入现示列表并添加 Load 引用，也可新增主行、眩光行及列；最多显示 509 个结构键列；保留多条眩光行时须维持合并字段数 |
| `Signal[signalAspectKey].Put(section, trackKey, x, y)` / `Signal[signalAspectKey].Put(section, trackKey, x, y, z, rx, ry, rz, tilt, span)`                                                  | √ | √ | △ | △ | 两种形式均可编辑，新建使用完整式；短式扩展需确认转换；3D 操纵器调整 X/Y/Z |
| `Beacon.Put(type, section, sendData)`                                                                                                                                                       |   √   |    √     |    √     |     ✕      | 编辑里程与全部参数 |
| `SpeedLimit.Begin(v)` / `SpeedLimit.End()`                                                                                                                                                  |   √   |    √     |    √     |     ✕      | Begin/End 分别编辑、新建和删除 |
| `PreTrain.Pass(time)` / `PreTrain.Pass(second)` | √ | √ | √ | ✕ | 通过 2D/3D 标记编辑里程、通行时间或删除；在“信号”向导中新建。时间输入为 hh:mm:ss 或秒数，悬停 `passTime` 可查看格式提示 |
| `Light.Ambient(...)`、`Light.Diffuse(...)`、`Light.Direction(...)`                                                                                                                          |   √   |    √     |    √     |     ✕      | 在“光照效果”表中查看和编辑参数，在“效果”向导中于里程 `0` 新建。RGB 范围 `[0, 1]`，Direction 位于里程 `0`；同类声明在根地图及 Include 中须唯一 |
| `Fog.Interpolate(density, red, green, blue)` / `Fog.Interpolate(density)` / `Fog.Interpolate()` / `[旧式] Fog.Set(density, red, green, blue)`                                               |   √   |    √     |    √     |     ✕      | 支持 Interpolate 的 0/1/4 参数形式及旧式 Set；3D 预览指数雾及其过渡 |
| `[兼容] Legacy.Fog(start, end, red, green, blue)`                                                                                                                                            |   √   |    √     |    √     |     ✕      | 在表格或 2D/3D 标记中编辑；3D 预览线性雾及其过渡，可与 Fog 混用 |
| `DrawDistance.Change(value)`                                                                                                                                                                |   √   |    √     |    √     |     ✕      | 编辑里程与数值；可用于控制场景绘制距离 |
| `CabIlluminance.Interpolate(value)` / `CabIlluminance.Interpolate()` / `[旧式] CabIlluminance.Set(value)`                                                                                     |   √   |    √     |    √     |     ✕      | 编辑里程与亮度；数值留空时写为 Interpolate()，表格与 3D 标牌显示前一有效值，无前值时留空 |
| `Irregularity.Change(x, y, r, lx, ly, lr)`                                                                                                                                                  |   √   |    √     |    √     |     ✕      | 在表格和标记中查看数据；可编辑里程及全部 6 个参数 |
| `Adhesion.Change(a)` / `Adhesion.Change(a, b, c)`                                                                                                                                           |   √   |    √     |    √     |     ✕      | 在表格和标记中查看数据；支持 1/3 参数形式 |
| `Sound.Load(filePath)`                                                                                                                                                                      | √ | √ | √ | - | 可更换列表路径、新建或导入列表并添加 Load 引用；表格支持编辑 key、路径、缓冲区数量及增删排序，包括空列表第一行；兼容列表版本 2.00+ |
| `Sound[soundKey].Play()`                                                                                                                                                                    |   √   |    √     |    √     |     ✕      | 在表格和标记中查看音效事件；编辑里程与 key |
| `Sound3D.Load(filePath)`                                                                                                                                                                    | √ | √ | √ | - | 可更换、新建或导入 3D 音效列表并添加 Load 引用；表格支持编辑 key、路径、缓冲区数量及增删排序，包括空列表第一行；兼容列表版本 2.00+ |
| `Sound3D[soundKey].Put(x, y)`                                                                                                                                                               |   √   |    √     |    √     |     △      | 编辑里程、key、X/Y；3D 标牌标示音源位置，操纵器调整 X/Y 和整米里程 |
| `RollingNoise.Change(index)`                                                                                                                                                                |   √   |    √     |    √     |     ✕      | 在表格和标记中查看噪声事件；编辑里程与 index |
| `FlangeNoise.Change(index)`                                                                                                                                                                 |   √   |    √     |    √     |     ✕      | 在表格和标记中查看噪声事件；编辑里程与 index |
| `JointNoise.Play(index)`                                                                                                                                                                    |   √   |    √     |    √     |     ✕      | 在表格和标记中查看噪声事件；编辑里程与 index |
| `Train.Add(trainKey, filePath, trackKey, direction)` / `Train[trainKey].Load(filePath, trackKey, direction)`                                                                                |   △   |    ✕     |    ✕     |     ✕      | 显示他列车定义，部分读取外部定义文件 |
| `Train[trainKey].Enable(time)` / `Train[trainKey].Enable(second)`                                                                                                                           |   √   |    ✕     |    ✕     |     ✕      | 在他列车停止位置表上方显示启用时间 |
| `Train[trainKey].Stop(decelerate, stopTime, accelerate, speed)`                                                                                                                             |   √   |    ✕     |    ✕     |     ✕      | 显示只读的他列车停止位置表、路径与地图标记 |

## 安装与启动

### 下载发行版（推荐）

从 [GitHub Releases](https://github.com/NewSapporoNingyo/komapedit/releases) 下载可执行文件压缩包，完整解压后双击 `komapedit.exe` 运行程序。

### 自行编译

如果没有计算机软件开发基础，不建议尝试自行编译。

按照[开发者指南](dev_zhcn.md)构建应用，然后运行 `build_release\komapedit.exe`（Release）或 `build\komapedit.exe`（Debug）。

可执行文件与 DLL 应使用同一构建版本。构建脚本若提示输出根目录中存在旧 INI 或 DLL，请按提示整理为 `bin`/`settings` 布局后重新构建。

### 启动与设置

程序从 `komapedit.exe` 同级的 `bin` 目录加载 `maploader.dll`、`model_loader.dll` 及其依赖。

程序启动时会按需新建 `settings` 目录，并在其中创建或读取：

- `settings/imgui.ini`：用户界面内的窗口位置等信息
- `settings/settings.ini`：保存界面语言、字体/组件/车站标记大小、2D 线宽、主题色、编辑模式警告状态，以及打开地图时自动加载场景预览、雾效果、绘制距离、操纵器尺寸、相机速度和性能警告等 3D 画布设置
- `settings/history.ini`：最近打开地图、背景图对齐参数，以及各地图的自定义消息显示偏好

建议通过界面修改设置。程序按当前设置格式读取有效项，其余项使用默认值；保存时写入完整设置。

## 版权、许可和第三方声明

komapedit 以 Apache License, Version 2.0 分发。许可证全文见 `LICENSE`，
项目版权与归属声明见 `NOTICE`。

本项目基于 `kobushi-trackviewer` 开发，用于辅助查看和编辑 BVE Trainsim
地图文件。

参考项目：

| 项目                                                                                   | 版权                               | 许可证                      |
| -------------------------------------------------------------------------------------- | ---------------------------------- | --------------------------- |
| konawasabi 的 [kobushi-trackviewer](https://github.com/konawasabi/kobushi-trackviewer) | Copyright (c) 2021-2024 konawasabi | Apache License, Version 2.0 |

GUI 和模型预览使用的第三方库：

| 库                                                                     | 用途                                                               | 版权                                                                             | 许可证                        |
| ---------------------------------------------------------------------- | ------------------------------------------------------------------ | -------------------------------------------------------------------------------- | ----------------------------- |
| [Dear ImGui](https://github.com/ocornut/imgui)                         | Docking GUI、Win32 后端、DirectX 11 后端、C++ std::string 辅助模块 | Copyright (c) 2014-2026 Omar Cornut                                              | MIT License                   |
| [ImPlot](https://github.com/epezent/implot)                            | 2D 图表控件                                                        | Copyright (c) 2020-2024 Evan Pezent；Copyright (c) 2025-2026 Breno Cunha Queiroz | MIT License                   |
| [Assimp / Open Asset Import Library](https://github.com/assimp/assimp) | 布景模型导入                                                       | Copyright (c) 2006-2026, assimp team                                             | Modified BSD 3-Clause License |
| Dear ImGui 随附的 stb 单文件库                                         | Dear ImGui 使用的字体、文本编辑、矩形打包支持                      | Copyright (c) 2017 Sean Barrett                                                  | MIT License 或 Public Domain  |

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
