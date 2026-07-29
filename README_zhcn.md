# komapedit

## 项目概述

komapedit 是一个面向 BVE Trainsim 地图文件的轻量级查看与编辑工具，基于 `kobushi-trackviewer` 的轨道几何计算思路改写为 C++/Win32 桌面程序。当前版本主要提供地图读取、轨道几何生成、2D 平面图、坡度/曲线半径图、信息表格查看、源码/Include 文件检查、有限的源文件关联元素编辑、布景模型与地图场景 3D 预览和轨道几何 CSV 导出能力。

程序由三个运行时核心部分组成：

- `maploader.dll`：读取 `BveTs Map` 文件、解析部分 BVE Map 语法、生成自轨道/他轨道几何数据，并通过固定宽度 C ABI 输出带版本的强类型快照。
- `model_loader.dll`：通过 Assimp 读取布景模型文件，并向 3D 预览提供网格/材质数据。
- `komapedit.exe`：基于 Dear ImGui、ImPlot、Win32、DirectX 11 和 WIC 的桌面 GUI。

随程序提供的 EXE 与 `maploader.dll` 统一使用 maploader API v2。`KvMapSnapshot` v2 传递全部地图数据、常规几何、source/edit metadata；独立失效的 `KvSceneGeometrySnapshot` v1 传递稠密 3D 自轨道/他轨道几何。编辑目标、dry-run、内存 Apply、direct Apply 和 Save/commit 均使用 typed batch 与由 map handle 持有的 typed report。所有快照只存在于进程内存中；Open/Reload 始终重新读取当前线路源文件，不向磁盘写入线路快照或几何缓存。

当前项目已经支持布景模型列表、`Structure.Put`/`Put0`/`PutBetween`、`Signal.Put`、`Station.Put` 和相互关联的 `Repeater.Begin`/`Begin0`/`End` 段的源文件关联编辑，并可在 3D 场景中实时拖动布景、信号机和 Repeater Begin 的 X/Y/Z 位置；但尚不是完整的地图编辑器，自轨道曲线/坡度、声音、环境效果和新建元素等编辑仍在开发计划中。

## 开发状况（TODO List）

### 地图读取与解析

- [x] 读取 `BveTs Map 2.0+` 地图文件
- [x] 支持 UTF-8、UTF-8 BOM、UTF-16LE、UTF-16BE、CP932/Shift_JIS 等文本编码处理
- [x] 支持 `Include` 引用其他地图文件
- [x] 支持 `$变量 = 表达式;`、`distance` 预定义变量和基础数学函数
- [x] 支持 `#`、`//` 注释
- [x] 支持异步加载地图，并在控制台窗口显示加载日志、警告和错误
- [x] 通过带版本的 typed map snapshot 为可编辑 map/list 语句提供源锚点和稳定编辑 metadata
- [x] 将已支持的修改/删除先应用到内存工作副本，再保存到源 map/include/list 文件，并尽量保留 include 结构、距离语义、原始编码和换行
- [ ] 支持新建元素，并将源文件关联编辑扩展到当前布景、`Station.Put` 和关联 Repeater 行以外的元素

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
- [x] 支持车站跳转和数字里程跳转
- [x] 支持导入背景图，并调整位置、尺寸、旋转角和亮度
- [x] 支持使用两个车站位置对齐背景图
- [x] 平面图上的布景与连续布景位置标记
- [x] 平面图上显示信号位置标记
- [x] 平面图上显示应答器位置标记
- [x] 平面图上显示先行列车通过点标记
- [x] 平面图上显示他列车路径和停止位置标记
- [x] 平面图上显示轨道变位和粘着特性变化点标记
- [x] 平面图上显示音效播放、固定音源、走行音、轮缘摩擦音效和道岔音效标记
- [x] 平面图上显示背景变化点标记
- [x] 平面图上显示驾驶台亮度变化点标记
- [x] 平面图上显示雾效果变化点标记
- [x] 平面图上显示绘制距离变化点标记
- [ ] 拆分 2D 画布内部的视图状态、marker cache、hit-test/context menu、背景图和绘制 primitive，拆分阶段不改变现有行为

### 地图信息表示

- [x] 读取 `Station.Load` 指定的车站列表 CSV
- [x] 分别显示 `Station.Put` 位置行和 `Station.Load` 定义行
- [x] 显示他轨道列表，可切换显示、设置范围和颜色
- [x] 显示他列车定义和停止位置列表，可切换路径显示并定位停止位置
- [x] 显示 `Structure.Put`、`Structure.Put0`、`Structure.PutBetween` 的地图布景放置表
- [x] 读取并显示 `Structure.Load` 指定的布景模型列表（`.txt` 或 `.csv`）
- [x] 显示 `Repeater.Begin`、`Repeater.Begin0`、`Repeater.End` 的关联连续布景段，并合并 Begin/End/变化边界
- [x] 为布景模型、信号现示和音效列表提供共享查找和未使用条目搜索面板
- [x] 通过源文件关联属性检查器编辑布景模型列表中的 key 和文件路径
- [x] 编辑 `Station.Put` 的 distance 和 `stationKey`
- [ ] 编辑其余车站列表字段并支持新增车站行
- [x] 显示 `信号现示列表`、`地图信号列表` 和 `应答器列表`
- [x] 显示 `轨道变位列表`、`粘着特性变化点列表`、走行音、轮缘摩擦音效和道岔音效相关表格
- [x] 显示 `背景变化点列表`、`驾驶台亮度变化点列表`、`雾效果变化点列表` 和 `绘制距离变化点列表`
- [x] 通过源码回写的行内表格编辑器编辑 `Signal.Load` 信号现示定义；暂不支持新增行或列
- [ ] 编辑应答器列表
- [x] 为已支持的布景、`Signal.Put`、`Station.Put` 和关联 Repeater 行提供基于源锚点的“属性/编辑”检查器，并在 3D 场景中为可编辑的布景、信号机和 Repeater Begin 放置提供实时 X/Y/Z 操纵器
- [ ] 将属性检查器扩展到 2D 标记和其余地图信息行

### 3D画布

- [x] 布景模型 3D 预览
- [x] 通过 `model_loader.dll`/Assimp 读取模型和贴图
- [x] 支持旋转和缩放布景模型预览
- [x] 3D 画布场景预览，可显示轨道路径、布景/连续布景实例、信号、地图元素标记、背景变化和插值后的 BVE 雾效果
- [x] 可通过车站跳转和数字里程跳转移动 3D 场景相机，并在平面图上显示当前 3D 位置
- [x] 可从布景、连续布景、信号和支持的地图元素标记表格行定位到 3D 场景，也可从场景对象或标记定位回对应表格
- [ ] 3D 场景画质设置：render scale、MSAA、纹理过滤和轮廓质量
- [x] 在 3D 场景线路信息叠加层显示当前曲线半径/超高、坡度和距下一站距离
- [ ] 为 3D 线路信息叠加层补充上一站信息和当前不支持的插值情况
- [x] 通过实时 3D 操纵器编辑 `Structure.Put` 和 `Repeater.Begin` 的 X/Y/Z 位置，并支持显式 `Put0`/`Begin0` 转换和操纵器尺寸设置
- [ ] 支持通过 3D 操纵器编辑布景旋转和其他放置字段
- [x] 在属性检查器中编辑关联的连续布景段，支持 Begin 导航、End/变化边界和关联删除选项

### 环境效果

- [x] 显示 `音效文件列表`、`3D音效文件列表`、`地图音效列表` 和 `地图3D音效列表`
- [ ] Sound/Sound3D 文件列表、地图音效、走行音、道岔音、报站音字段和 3D 声音编辑
- [ ] 驾驶台亮度设定位置编辑
- [ ] 雾效果编辑

### 用户界面与辅助功能

- [x] Dear ImGui Docking 多窗口布局
- [x] 简体中文、英文、日文界面语言切换
- [x] 字体大小、组件大小、车站标记大小、主题色、3D 场景相机速度和场景实例性能警告设置
- [x] 最近打开地图历史记录
- [x] 背景图参数随最近地图保存到 `settings/history.ini`
- [x] 设置保存到程序目录下的 `settings/settings.ini`
- [x] Include 文件结构图，以及读取当前内存工作副本的只读源码文本预览
- [x] 编辑模式中分离“应用到预览”“保存到磁盘”“撤销全部待保存改动”和“从磁盘重新加载”，并在存在未保存更改时确认
- [x] 将自轨道和他轨道几何导出为 CSV
- [ ] 通过 `element_presets.json` 保存元素预设组，应用后生成普通 BVE map/list 语句
- [ ] 线路 release 导出：展开 Include、可选常量化距离/变量表达式、只复制实际使用资源、输出报告，并保护开发线路目录不被覆盖

### 当前的BVE地图语法支持状况

- 预览：实际进入轨道几何、表格、标记或 3D 场景。
- 基本编辑：已有语句可通过属性检查器修改并写回；不代表支持新建该语句。
- 图形化编辑：可在 2D/3D 画布直接拖动或操纵；右键打开属性窗口不算。
- √ = 完整支持；△ = 部分或间接支持；✕ = 暂不支持；- = 没有计划支持或无必要性。

| 地图语法                                      | 预览  | 基本编辑 | 图形化编辑 | 当前实际情况                                                                         |
| --------------------------------------------- | :---: | :------: | :--------: | ------------------------------------------------------------------------------------ |
| 文件头、版本及编码                            |   △   |    ✕     |     -      | 可加载受支持编码，编码覆盖并不完整                                                   |
| 注释、赋值、函数调用、数组、键                |   √   |    ✕     |     -      | 作为解析基础使用，没有通用源码编辑器                                                 |
| 变量及参数变量                                |   √   |    ✕     |     -      | 可参与表达式求值                                                                     |
| 算术、比较及逻辑运算符                        |   △   |    ✕     |     -      | 大部分可求值，字符串和数值混合等边界情况不完整                                       |
| 数学函数                                      |   √   |    ✕     |     -      | 用于地图表达式求值                                                                   |
| 距离声明及距离表达式                          |   √   |    △     |     ✕      | 仅 `Structure.Put/Put0/PutBetween`、`Signal.Put`、`Station.Put` 和关联 Repeater Begin/End 等编辑目标可修改距离 |
| `include`、带距离偏移的 `include`             |   √   |    △     |     ✕      | 可加载包含文件；其中受支持元素可以写回，但 include 路径本身不可编辑                  |
| `Curve.*`                                     |   √   |    ✕     |     ✕      | 参与轨道几何生成                                                                     |
| `Gradient.*`                                  |   √   |    ✕     |     ✕      | 参与轨道高程及坡度生成                                                               |
| `Track['key'].X/Y/Position`                   |   √   |    ✕     |     ✕      | 生成其他轨道几何                                                                     |
| `Track['key'].Cant.*`                         |   √   |    ✕     |     ✕      | 进入轨道几何和超高数据                                                               |
| `Structure.Load`                              |   √   |    △     |     -      | 结构列表及模型会加载；可编辑被加载列表中的 key/path，但不能编辑 Load 路径            |
| `Structure.Put`                               |   √   |    √     |     △      | 属性字段可写回；3D 中仅支持 X/Y/Z 平移，不支持旋转、里程、轨道、倾斜和跨度的直接操纵 |
| `Structure.Put0`                              |   √   |    √     |     △      | 基本字段可编辑；确认转换成 `Structure.Put` 后才显示 X/Y/Z 操纵器                     |
| `Structure.PutBetween`                        |   √   |    √     |     ✕      | 属性检查器内可编辑，但没有 2D/3D 操纵器                                              |
| `Repeater.Begin` / `Begin0` / `End`           |   √   |    △     |     △      | 支持关联 Begin 字段、End 距离和关联删除；显式转换 `Begin0` 后可在 3D 中移动 Begin 的 X/Y/Z |
| `Background.Change`                           |   √   |    ✕     |     ✕      | 可进入背景数据及场景预览                                                             |
| `Station.Load`                                |   √   |    ✕     |     -      | 站名及站点列表可加载                                                                 |
| `Station.Put`                                 |   √   |    △     |     ✕      | 只可编辑距离和 `stationKey`，车门及停车余量等参数不可编辑                            |
| `Section.Begin` / `Section.BeginNew`          |   ✕   |    ✕     |     ✕      | 会被解析到 typed map snapshot，但当前 GUI 尚不显示                                   |
| `Section.SetSpeedLimit` / `Signal.SpeedLimit` |   ✕   |    ✕     |     ✕      | 会被解析到 typed map snapshot，但当前 GUI 尚不显示                                   |
| `Signal.Load`                                 |   √   |    √     |     -      | 信号现示定义支持源码安全的行内编辑；暂不支持新增行或列                               |
| `Signal.Put`                                  |   √   |    √     |     △      | 全部放置字段均可编辑；3D 支持 X/Y/Z 平移。短式编辑扩展字段前需确认转换为完整式       |
| `Beacon.Put`                                  |   √   |    ✕     |     ✕      | 可进入列表及地图标记                                                                 |
| `SpeedLimit.Begin` / `SpeedLimit.End`         |   √   |    ✕     |     ✕      | 可进入限速模型、列表及标记                                                           |
| `PreTrain.Pass`                               |   √   |    ✕     |     ✕      | 可进入列表及地图标记                                                                 |
| `Light.Ambient/Diffuse/Direction`             |   -   |    -     |     -      | 未支持光照相关语法                                                                   |
| `Fog.Interpolate` / `Fog.Set`                 |   √   |    ✕     |     ✕      | 可进入表格/标记，并在 3D 场景预览中显示线性插值后的指数雾；尚不支持雾效果编辑        |
| `DrawDistance.Change`                         |   √   |    ✕     |     ✕      | 可进入表格、平面/场景标记，并可选地控制场景绘制距离                                  |
| `CabIlluminance.Set`                          |   √   |    ✕     |     ✕      | 可进入列表/标记，不模拟驾驶台亮度效果                                                |
| `Irregularity.Change`                         |   √   |    ✕     |     ✕      | 可进入列表/标记，不模拟车辆振动                                                      |
| `Adhesion.Change`                             |   √   |    ✕     |     ✕      | 可进入列表/标记，不模拟车辆黏着效果                                                  |
| `Sound.Load` / `Sound.Play`                   |   √   |    ✕     |     ✕      | 声音资源和播放点可列出、定位，但不实际播放                                           |
| `Sound3D.Load` / `Sound3D.Put`                |   √   |    ✕     |     ✕      | 可进入资源列表和空间位置数据，但不实际播放                                           |
| `RollingNoise.Change`                         |   √   |    ✕     |     ✕      | 可进入列表/标记                                                                      |
| `FlangeNoise.Change`                          |   √   |    ✕     |     ✕      | 可进入列表/标记                                                                      |
| `JointNoise.Play`                             |   √   |    ✕     |     ✕      | 可进入列表/标记                                                                      |
| `Train.Add` / `Train.Load`                    |   △   |    ✕     |     ✕      | 其他列车定义可展示，但外部列车文件只被部分建模                                       |
| `Train.Enable`                                |   ✕   |    ✕     |     ✕      | 会被解析到 typed map snapshot，但当前 GUI 尚不显示                                   |
| `Train.Stop`                                  |   √   |    ✕     |     ✕      | 可生成其他列车停车表、路径和地图标记                                                 |

## 安装与启动

当前仓库未提供发行版本，推荐从源代码构建后运行。

构建完成后，运行 `build_release\komapedit.exe`。可执行文件保留在第 1 层，
`maploader.dll`、`model_loader.dll` 及构建复制的 Assimp/运行时依赖 DLL
统一从 `build_release\bin` 加载。

程序启动时会按需新建 `settings` 目录，并在其中创建或读取：

- `settings/imgui.ini`：用户界面内的窗口位置等信息
- `settings/settings.ini`：保存设置：界面语言、字体大小、组件大小、车站标记大小、主题色和 3D 画布设置
- `settings/history.ini`：最近打开地图和背景图对齐参数

构建及发布清理脚本会把旧版第 1 层 INI 迁移到 `settings`。如果新旧位置同时
存在同名文件，脚本会中止，不会覆盖其中任何一份。

## 使用方法

1. 通过菜单 `文件 -> 打开...` 或工具栏 `打开` 选择`.txt`地图文件
2. 地图加载完成后，中央窗口显示平面图、纵断面图和曲线半径图
3. 在平面图中：
   - 左键拖动平移
   - 鼠标滚轮缩放
   - 按住 `Shift` 滚轮旋转，或使用鼠标右键/`Ctrl + 左键` 拖动旋转
   - 双击平面图恢复自适应范围
4. 在工具栏使用“车站跳转”或“跳转到里程(m)”，可定位到车站或数字里程
5. 在 `2D 视图` 菜单中切换 2D 视图窗口、坡度图、曲线半径图、坡度叠加、纵断面其他轨道和背景图相关操作。
6. 在 `辅助信息` 菜单中切换车站、轨道几何、他列车路径、信号、音效、效果和 3D 场景辅助标记。在 `辅助信息 -> 其它` 中打开 `文件结构图`，可查看入口地图及其嵌套 Include 文件；右键源码文件节点可打开只读文本预览。信号标记通过 `地图信号列表` 中的“显示”勾选框控制。
7. 在 `模式` 中选择“测量”，在轨道附近移动或双击，可查看里程、标高、坡度、曲线半径和限速
8. 在 `地图信息` 菜单中打开车站、轨道、他列车、布景、连续布景、信号、应答器、音效、轨道变位/粘着、背景、驾驶台亮度、雾效果和绘制距离相关表格。有平面位置的行可定位到平面图；模型和音效文件行可打开关联文件。
   - `信号现示列表`：查看信号现示定义
   - `地图信号列表`：查看信号位置，并使用行内“显示”勾选框控制平面图标记
   - `应答器列表`：查看应答器位置
   - `音效文件列表` 和 `3D音效文件列表`：查看已加载的音效文件条目、查找匹配 key、查找未使用条目，并打开关联文件
   - `地图音效列表`、`地图3D音效列表`、`走行音变化点列表`、`轮缘摩擦音效变化点列表` 和 `道岔音效播放点列表`：查看音效播放/变化位置，并定位到平面图
    - `轨道变位列表`、`粘着特性变化点列表`、`背景变化点列表`、`驾驶台亮度变化点列表`、`雾效果变化点列表` 和 `绘制距离变化点列表`：查看对应变化点表
   - `其他轨道`：切换他轨道显示、调整显示范围和颜色
   - `他列车列表`：查看他列车定义和停止位置，切换路径显示，并定位停止位置到平面图
    - `车站列表`：分别查看 `Station.Put` 位置行和 `Station.Load` 定义行
   - `地图布景列表`：查看地图中的 `Structure.Put`、`Structure.Put0`、`Structure.PutBetween`；3D 场景已加载时可定位到对应场景对象
   - `布景模型列表`：查看 `Structure.Load` 指定列表中的 structureKey 和模型文件；右键 structureKey 并选择 `预览模型` 可打开 3D 模型预览
    - `连续布景列表`：查看关联的 `Repeater.Begin`/`Begin0`/`End` 段，通过“属性/编辑”修改，并在 3D 场景已加载时定位到对应连续布景实例
    - 打开工具栏“编辑模式”后，可对已支持的 `Station.Put`、`Signal.Put`、布景模型/放置和关联连续布景行使用“属性/编辑”。“应用”只更新内存预览，工具栏“保存”才写入源文件；“撤销”会丢弃全部待保存的内存改动，“重新加载”会重新读取磁盘文件。
     首次启用时会显示确认提示：编辑属于不稳定的测试性功能，可能会对地图文件产生破坏性更改。请先备份地图文件，或使用 Git 等版本控制工具管理；勾选“不再显示”并确认后，之后不再提示。
9. 在 `2D 视图 -> 背景图` 中导入背景图，可手动调整位置、尺寸、旋转和亮度，也可按两个车站对齐
10. 在 `3D 视图 -> 布景模型预览` 中显示或隐藏布景模型预览窗口。预览窗口内可用鼠标左键拖动旋转模型，用鼠标滚轮缩放
11. 在 `3D 视图 -> 3D场景预览` 中显示场景预览窗口，然后点击 `启动3D场景预览`。场景预览可在窗口中重新加载或关闭；加载场景后，车站跳转和里程跳转也会移动场景相机。叠加层会显示当前曲线/超高、坡度和下一站信息。`选项 -> 3D画布设置 -> 雾效果` 可即时切换场景预览中的线路雾效果，且默认开启；同一设置还可控制地图语句驱动的绘制距离、相机速度和场景实例性能警告。选择模式下可从场景对象和支持的地图元素标记定位回对应表格；打开编辑模式后，右键布景、信号机或连续布景并选择“属性/编辑”可修改字段，受支持的 `Structure.Put`、`Signal.Put` 和 `Repeater.Begin` 坐标也可使用 X/Y/Z 操纵器拖动。
12. 在 `文件 -> 导出 CSV...` 中选择输出目录，导出自轨道和他轨道几何 CSV
13. 按 `F5` 或菜单 `文件 -> 重新加载` 可重新读取当前地图

## 项目文件结构

```text
komapedit/
├─ CMakeLists.txt                  # CMake 构建配置
├─ README.md                       # 项目说明
├─ README_zhcn.md                  # 简体中文项目说明
├─ LICENSE                         # Apache License 2.0
├─ NOTICE                          # 项目版权与 Apache 归属声明
├─ THIRD_PARTY_NOTICES.md          # 第三方库和参考项目声明
├─ build_dev.bat                   # Debug 构建脚本
├─ build_release.bat               # Release 构建脚本
├─ clear_build_release_dist.bat    # 清理 Release 目录，保留 bin、settings 和声明文件
├─ get_3rd_party_packages.bat      # 拉取 ImGui 和 ImPlot
├─ install_Assimp.bat              # 使用 vcpkg 安装 Assimp 的辅助脚本
├─ include/
│  ├─ canvas3D.h                   # 3D 预览画布接口
│  ├─ map_marker_visuals.h          # 2D/3D 共用的地图标记视觉配方
│  ├─ maploader.h                  # maploader C ABI
│  ├─ maploader_snapshot.h         # 固定宽度 typed snapshot/edit ABI 结构
│  ├─ model_loader.h               # model_loader C ABI
│  ├─ repeater_linkage.h            # 共用的 Repeater Begin/End 段配对
│  └─ multilanguage.h              # 界面多语言文本
├─ src/
│  ├─ main_window/
│  │  ├─ gui_kme.cpp               # 主窗口、Win32/DirectX 11 初始化和主循环
│  │  ├─ app_settings.cpp/.h       # 运行时设置、历史记录和 UI 样式辅助代码
│  │  ├─ runtime_paths.cpp/.h       # 可执行文件、DLL 和设置目录路径
│  │  ├─ maploader_runtime.cpp      # bin/maploader.dll 的缓存运行时分发
│  │  ├─ map_marker_visuals.cpp     # 2D/3D 共用的地图标记视觉
│  │  ├─ file_structure_diagram.cpp # Include 文件结构图和源码文件操作
│  │  ├─ text_preview.cpp            # 只读源码预览和 distance 边界选择
│  │  ├─ debug_headless.cpp/.h     # 仅 Debug 构建使用的 headless 验证入口
│  │  ├─ touch_input.cpp/.h         # Win32 触摸手势转换
│  │  └─ kme.h                     # App 声明与 GUI 共享状态
│  ├─ table/
│  │  ├─ datatable.cpp             # 数据表格列定义、缓存与表格窗口
│  │  └─ table_navigation.cpp      # 表格到平面图/3D 标记的定位与可见状态
│  ├─ canvas2d/
│  │  ├─ canvas2D.cpp              # 2D 平面画布、标记、测量和背景图
│  │  └─ profile_plots.cpp         # 纵断面和曲线半径图渲染
│  ├─ canvas3d/
│  │  └─ canvas3D.cpp              # DirectX 11 模型/场景预览和场景标记渲染
│  ├─ maploader/
│  │  ├─ maploader.cpp             # Public C ABI 入口和 map handle 生命周期管理
│  │  ├─ maploader_internal.h      # maploader 共享状态、行记录、源锚点和辅助声明
│  │  ├─ maploader_core.cpp        # 通用解析/value/source-span 工具和 MapContext 辅助逻辑
│  │  ├─ maploader_parser.cpp      # BVE Map/list 解析、Include、变量表达式和源锚点收集
│  │  ├─ maploader_geometry.cpp    # 自轨道/他轨道几何、relocate、曲线和场景控制点
│  │  ├─ maploader_identity.cpp    # 稳定 edit identity 与确定性哈希
│  │  ├─ maploader_snapshot.cpp    # map/scene 快照构建和 revision 失效
│  │  ├─ maploader_semantic.cpp    # typed 编辑语义验证与 fingerprint
│  │  ├─ maploader_edits.cpp       # edit dry-run、内存应用、源文件 patch 和 commit/writeback
│  │  ├─ tests/                     # typed snapshot/edit C ABI contract 测试
│  │  ├─ text_decoder.cpp/.h       # 文件读取、UTF-8 路径和文本解码
│  │  ├─ diagnostics.cpp/.h        # 加载器日志与最后错误状态
│  │  └─ c_api.cpp/.h              # C ABI 分配辅助代码
│  └─ model_loader/
│     └─ model_loader.cpp          # 基于 Assimp 的布景模型加载
├─ third_party/
│  ├─ imgui/                       # Dear ImGui，docking 分支
│  └─ implot/                      # ImPlot
├─ build/                          # Debug：komapedit.exe、bin/ DLL、settings/ INI
└─ build_release/                  # Release：komapedit.exe、bin/ DLL、settings/ INI
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

输出目录为 `build`，目录层级与下述 Release 相同：可执行文件位于第 1 层，
DLL 位于 `bin`，INI 位于 `settings`。

### Release 构建：build_release.bat

输出目录为 `build_release`。主要产物：

- `komapedit.exe`
- `bin/maploader.dll`
- `bin/model_loader.dll`
- `bin` 下的 Assimp/运行时 DLL，具体取决于所选工具链/包管理器
- `settings/`，设置写入前为空
- `LICENSE`
- `NOTICE`
- `THIRD_PARTY_NOTICES.md`

如需整理发布目录，可执行 `clear_build_release_dist.bat`。该脚本会保留
`komapedit.exe`、`bin` 目录及其中的 `.dll` 文件、`settings` 目录及其现有内容，
以及 `LICENSE`、`NOTICE` 和 `THIRD_PARTY_NOTICES.md`。


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

| 字段             | 说明                                                    |
| ---------------- | ------------------------------------------------------- |
| distance         | 地图绝对距离，单位 m                                    |
| x                | 自轨道计算后的平面 X 坐标                               |
| y                | 自轨道计算后的平面 Y 坐标                               |
| z                | 标高                                                    |
| direction        | 轨道方向角，单位 rad                                    |
| radius           | 当前曲线半径                                            |
| gradient         | 当前坡度，按 BVE 的千分率语义处理                       |
| interpolate_func | 插值类型，`0` 表示 `sin半波长递减`，`1` 表示 `线性递减` |
| cant             | 超高                                                    |
| center           | 轨道中心偏移                                            |
| gauge            | 轨距                                                    |

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

| 字段             | 说明                                      |
| ---------------- | ----------------------------------------- |
| distance         | 地图绝对距离，单位 m                      |
| x                | 他轨道计算后的平面 X 坐标                 |
| y                | 他轨道计算后的平面 Y 坐标                 |
| z                | 他轨道标高                                |
| interpolate_func | 插值类型，`0` 表示 `sin`，`1` 表示 `line` |
| cant             | 超高                                      |
| center           | 轨道中心偏移                              |
| gauge            | 轨距                                      |

导出的数值使用固定 6 位小数。当前 CSV 导出仅包含轨道几何，不导出车站、布景、连续布景、信号、应答器、音效、背景变化点、驾驶台亮度变化点、雾或 3D 场景数据。

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

| 库                                                                     | 用途                                                               | 版权                                 | 许可证                        |
| ---------------------------------------------------------------------- | ------------------------------------------------------------------ | ------------------------------------ | ----------------------------- |
| [Dear ImGui](https://github.com/ocornut/imgui)                         | Docking GUI、Win32 后端、DirectX 11 后端、C++ std::string 辅助模块 | Copyright (c) 2014-2026 Omar Cornut  | MIT License                   |
| [ImPlot](https://github.com/epezent/implot)                            | 2D 图表控件                                                        | Copyright (c) 2020 Evan Pezent       | MIT License                   |
| [Assimp / Open Asset Import Library](https://github.com/assimp/assimp) | 布景模型导入                                                       | Copyright (c) 2006-2026, assimp team | Modified BSD 3-Clause License |
| Dear ImGui 随附的 stb 单文件库                                         | Dear ImGui 使用的字体、文本编辑、矩形打包支持                      | Copyright (c) 2017 Sean Barrett      | MIT License 或 Public Domain  |

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
