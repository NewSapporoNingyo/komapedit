<p align="center">
    <img src="../icons/titleimage.png" alt="komapedit" width="600">
</p>

# komapedit

komapedit 是一款面向 Windows 的轻量级 BVE Trainsim 地图查看与编辑工具。它可读取线路地图及其 Include 文件，计算自轨道和他轨道几何，在二维图表与三维场景中显示线路，通过可搜索表格查看地图数据，预览 Structure 模型，并将计算后的轨道几何导出为 CSV。

基于源码的编辑功能仍处于实验阶段。启用编辑模式前，请备份线路文件或使用版本控制管理。komapedit 尚不是完整的地图编辑器：部分 BVE 语法仅支持预览或尚未支持，部分已支持语句可以编辑但不能新建或图形化操纵。当前边界见[支持的 BVE 地图语法](#支持的-bve-地图语法)，开发进度见 [TODO.md](../TODO.md)。

## 文档导航

- [English Readme](../README.md)
- [开发进度（待办事项列表）](../TODO.md)
- [开发者指南](dev_zhcn.md)（[English ver](dev.md)）
- [AI 辅助开发指南](ai-dev_zhcn.md)（[English ver](ai-dev.md)）
- [供 AI 编程工具使用的仓库规范](../AGENTS.md)
- [许可证](../LICENSE)、[项目声明](../NOTICE)与[第三方声明](../THIRD_PARTY_NOTICES.md)

## 主要功能

- 打开 BVE Trainsim 2.0+ 地图，支持 UTF-8、UTF-16、CP932/Shift_JIS 相关编码及嵌套 `Include` 文件。
- 显示平面图、纵断面图、曲线半径、车站、限速、他轨道、辅助标记和测量信息。
- 通过可搜索表格查看车站、轨道、布景、连续布景、信号、应答器、音效、列车及环境效果。
- 在三维窗口中预览 Structure 模型和线路场景。
- 对下表所列语句提供基于源码的应用、保存、撤销和重新加载流程，并支持部分三维 X/Y/Z 放置编辑。
- 将计算后的自轨道和他轨道几何导出为 CSV。
- 提供简体中文、英语和日语界面。

## 支持的 BVE 地图语法

- 预览：实际进入轨道几何、表格、标记或 3D 场景。
- 基本编辑：已有语句可通过属性检查器修改并写回；不代表支持新建该语句。
- 新建元素：“新建地图元素”向导可插入对应的源码语句。
- 图形化编辑：可在 2D/3D 画布直接拖动或操纵；右键打开属性窗口不算。
- √ = 完整支持；△ = 部分或间接支持；✕ = 暂不支持；- = 没有计划支持或无必要性。

下列语句名称及“[旧式]”别名以 [BVE 官方地图文件格式说明](https://bvets.net/jp/edit/formats/route/map.html)为准，并保留项目额外兼容的 `Legacy.*` 语句以完整描述当前支持范围。向导不新增 `Load` 或资源/定义列表行；数值目标 distance 使用现有源码表达式和距离边界流程，并尽可能保留或安全调整已有 `$` 表达式。

| 地图语法                                                                                                                                                                                    | 预览  | 基本编辑 | 新建元素 | 图形化编辑 | 当前实际情况                                                                                                   |
| ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | :---: | :------: | :------: | :--------: | -------------------------------------------------------------------------------------------------------------- |
| 文件头、版本及编码                                                                                                                                                                          |   △   |    ✕     |    -     |     -      | 可加载 BVE Map 2.0+ 及 UTF-8/BOM、UTF-16LE/BE、CP932/Shift_JIS 相关编码；不支持任意声明编码                    |
| 注释及基本语句结构                                                                                                                                                                          |   √   |    ✕     |    -     |     -      | 支持 `#`/`//` 注释、分号分隔调用、带 key/嵌套元素、空白、多行语句及名称大小写不敏感；没有通用源码编辑器        |
| 赋值、参数及 key 中的变量                                                                                                                                                                   |   √   |    ✕     |    -     |     -      | 解析时求值，并在只读变量列表中按大小写不敏感名称分组显示赋值与来源                                             |
| 算术运算符（`+`、`-`、`*`、`/`、`%`）                                                                                                                                                       |   √   |    ✕     |    -     |     -      | 支持数值算术、单目正负号、括号和使用 `+` 的字符串拼接；不支持比较/逻辑及复合赋值运算符                         |
| 距离声明及 `distance` 表达式                                                                                                                                                                |   √   |    △     |    -     |     ✕      | 可编辑已有受支持元素的距离；新建元素时可生成或复用距离块，但没有独立距离编辑器                                 |
| 数学函数                                                                                                                                                                                    |   √   |    ✕     |    -     |     -      | 支持 `rand`、`abs`、`sin`、`cos`、`atan2`、`sqrt`、`exp`、`log`、`floor`、`ceil` 和 `pow`                      |
| `include 'file';`                                                                                                                                                                           |   √   |    △     |    ✕     |     -      | 支持嵌套 Include，且可写回其中受支持元素；Include 语句及路径只读                                               |
| `Curve.SetGauge(value)` / `[旧式] Curve.Gauge(value)`                                                                                                                                       |   √   |    ✕     |    ✕     |     ✕      | 更新用于超高几何的自轨道轨距，不生成可编辑元素行                                                               |
| `Curve.SetCenter(x)`                                                                                                                                                                        |   √   |    ✕     |    ✕     |     ✕      | 更新自轨道超高旋转中心，不生成可编辑元素行                                                                     |
| `Curve.SetFunction(id)`                                                                                                                                                                     |   √   |    ✕     |    ✕     |     ✕      | 选择正弦或线性曲线/超高插值，不生成可编辑元素行                                                                |
| `Curve.BeginTransition()`                                                                                                                                                                   |   √   |    △     |    △     |     ✕      | 向导仅将其原子地置于双参数 `Curve.Begin` 或选定 `Curve.End` 前，且可单独设定起点里程；不提供独立模板         |
| `Curve.Begin(radius, cant)` / `[旧式] Curve.BeginCircular(radius, cant)`                                                                                                                    |   √   |    √     |    △     |     ✕      | 向导仅原子输出当前 `Curve.Begin(radius, cant)` 并带可单独设定里程的前置缓和曲线；旧式 `Curve.BeginCircular` 仍仅可编辑 |
| `Curve.Begin(radius)` / `Curve.Change(radius)`                                                                                                                                              |   √   |    √     |    √     |     ✕      | 向导可新建当前单参数 Begin 和 Change 形式；已有行保持原方法编辑或删除                                         |
| `Curve.End()`                                                                                                                                                                               |   √   |    √     |    √     |     ✕      | 向导可新建 End，并可原子地同时添加可单独设定里程的缓和曲线起点；已有行可编辑或删除                           |
| `Curve.Interpolate(radius, cant)` / `Curve.Interpolate(radius)` / `Curve.Interpolate()`                                                                                                     |   √   |    ✕     |    ✕     |     ✕      | 官方 0/1/2 参数形式均进入几何，但不生成可编辑曲线行                                                            |
| `Gradient.BeginTransition()`                                                                                                                                                                |   √   |    △     |    △     |     ✕      | 向导仅将其原子地置于选定 `Gradient.Begin` 或 `Gradient.End` 前，且可单独设定起点里程；不提供独立模板          |
| `Gradient.Begin(gradient)` / `[旧式] Gradient.BeginConst(gradient)`                                                                                                                         |   √   |    √     |    √     |     ✕      | 向导可新建当前 `Gradient.Begin(gradient)`，并可原子地前置可单独设定里程的缓和曲线；旧式 `Gradient.BeginConst` 仍仅可编辑 |
| `Gradient.End()`                                                                                                                                                                            |   √   |    √     |    √     |     ✕      | 向导可新建 End，并可原子地同时添加可单独设定里程的缓和曲线起点；已有行可编辑或删除                           |
| `Gradient.Interpolate(gradient)` / `Gradient.Interpolate()`                                                                                                                                 |   √   |    ✕     |    ✕     |     ✕      | 官方 0/1 参数形式均进入几何，但不生成可编辑坡度行                                                              |
| `Legacy.Turn`、`Legacy.Curve`、`Legacy.Pitch`                                                                                                                                               |   √   |    △     |    ✕     |     ✕      | 项目兼容语法：均进入自轨道几何；只有已有 `Legacy.Curve` 行支持基于源码的值/距离编辑                            |
| `Track[trackKey].X.Interpolate(x, radius)` / `Track[trackKey].X.Interpolate(x)` / `Track[trackKey].X.Interpolate()`                                                                         |   √   |    △     |    √     |     ✕      | 向导可新建全部现行形式；可编辑已有距离/数值字段或删除。`trackKey` 在“属性/编辑”中只读，但可从“其他轨道”表统一重命名整条轨道 |
| `Track[trackKey].Y.Interpolate(y, radius)` / `Track[trackKey].Y.Interpolate(y)` / `Track[trackKey].Y.Interpolate()`                                                                         |   √   |    △     |    √     |     ✕      | 向导可新建全部现行形式；已有行的编辑边界与 X 插值相同                                                        |
| `Track[trackKey].Position(x, y, radiusH, radiusV)` / `Track[trackKey].Position(x, y, radiusH)` / `Track[trackKey].Position(x, y)`                                                           |   √   |    △     |    √     |     ✕      | 向导可新建全部现行形式；可编辑已有数值/距离或删除，但语句形状及 track key 只读                                |
| `Track[trackKey].Cant.SetGauge(gauge)` / `[旧式] Track[trackKey].Gauge(gauge)`                                                                                                              |   √   |    △     |    √     |     ✕      | 向导只新建现行 `Cant.SetGauge`；旧式 `Gauge` 保持可读取/编辑/删除，方法/key 只读                              |
| `Track[trackKey].Cant.SetCenter(x)`                                                                                                                                                         |   √   |    △     |    √     |     ✕      | 向导新建现行形式；已有值/距离可编辑或删除，方法/key 只读                                                      |
| `Track[trackKey].Cant.SetFunction(id)`                                                                                                                                                      |   √   |    △     |    √     |     ✕      | 向导以 `0` 或 `1` 的 id 新建现行形式；已有值/距离可编辑或删除，方法/key 只读                                  |
| `Track[trackKey].Cant.BeginTransition()`                                                                                                                                                    |   √   |    △     |    √     |     ✕      | 向导新建现行形式；已有距离可编辑或删除，方法/key 只读                                                         |
| `Track[trackKey].Cant.Begin(cant)`                                                                                                                                                          |   √   |    △     |    √     |     ✕      | 向导新建现行形式；已有值/距离可编辑或删除，方法/key 只读                                                      |
| `Track[trackKey].Cant.End()`                                                                                                                                                                |   √   |    △     |    √     |     ✕      | 向导新建现行形式；已有距离可编辑或删除，方法/key 只读                                                         |
| `Track[trackKey].Cant.Interpolate(cant)` / `Track[trackKey].Cant.Interpolate()` / `[旧式] Track[trackKey].Cant(cant)`                                                                       |   √   |    △     |    √     |     ✕      | 向导只新建现行 0/1 参数形式；旧式 `Cant` 保持可读取/编辑/删除，形状/key 只读                                  |
| `Structure.Load(filePath)`                                                                                                                                                                  |   √   |    △     |    ✕     |     -      | 已加载列表中的已有 key/path 可行内编辑、清空、调整顺序或删除；不能编辑 Load 路径或新增行                       |
| `Structure[structureKey].Put(trackKey, x, y, z, rx, ry, rz, tilt, span)`                                                                                                                    |   √   |    √     |    √     |     △      | 所有字段均可写回或新建；3D 仅直接操纵 X/Y/Z                                                                    |
| `Structure[structureKey].Put0(trackKey, tilt, span)`                                                                                                                                        |   √   |    √     |    √     |     △      | “属性/编辑”可添加/去除坐标偏移，在 `Put0` 与 `Put` 间双向转换；`Put0` 使用仅 Z 轴、整米步进的里程操纵器        |
| `Structure[structureKey].PutBetween(trackKey1, trackKey2, flag)` / `Structure[structureKey].PutBetween(trackKey1, trackKey2)`                                                               |   √   |    √     |    √     |     △      | 官方两种形式均可预览/编辑；检查器草稿会实时更新变形后的 3D 顶点，且仅 Z 轴操纵器会以整米步进修改 `distance`     |
| `Repeater[repeaterKey].Begin(trackKey, x, y, z, rx, ry, rz, tilt, span, interval, structureKey1, ...)` / `Repeater[repeaterKey].Begin0(trackKey, tilt, span, interval, structureKey1, ...)` |   √   |    △     |    √     |     △      | 向导可单独新建 Begin/Begin0，或与 End 原子新建；同名区间内的 Begin-only 经确认后作为变化点，成对区间仍拒绝重叠。“属性/编辑”的“插入变化点”会打开与源语句对应的 Begin/Begin0 表单，默认仅添加 Begin，并预填当前检查器草稿参数。支持 `repeaterKey` 改名、关联删除/修剪、形式转换和操纵器 |
| `Repeater[repeaterKey].End()`                                                                                                                                                               |   √   |    △     |    √     |     △      | 向导可单独或随 Begin/Begin0 新建 End；未闭合 Repeater 的“属性/编辑”可打开已预填源文件和 key 的仅添加 End 表单。允许孤立 End，但拒绝在已有显式 End 的同名有效区间内增加 End。现有 End 距离和关联删除/修剪仍受支持 |
| `Background.Change(structureKey)`                                                                                                                                                           |   √   |    √     |    √     |     ✕      | 可编辑、新建或删除距离/key；数据进入背景及场景预览                                                             |
| `Station.Load(filePath)`                                                                                                                                                                    |   √   |    △     |    ✕     |     -      | 已有车站定义行可行内编辑、清空、调整顺序或删除；不能编辑 Load 路径或新增行                                     |
| `Station[stationKey].Put(door, margin1, margin2)`                                                                                                                                           |   √   |    √     |    √     |     ✕      | 可编辑、新建或删除距离、key、车门侧和停车余量                                                                  |
| `Section.Begin(...)` / `[旧式] Section.BeginNew(...)`                                                                                                                                       |   √   |    √     |    √     |     ✕      | 距离及可变数量信号索引参数支持编辑、新建和删除；2D/3D 显示标记                                                 |
| `Section.SetSpeedLimit(...)` / `[旧式] Signal.SpeedLimit(...)`                                                                                                                              |   √   |    √     |    √     |     ✕      | 距离及可变数量限速参数支持编辑、新建和删除；生效值进入 3D 信号摘要                                             |
| `Signal.Load(filePath)`                                                                                                                                                                     |   √   |    △     |    ✕     |     -      | 已有现示/glare 行可行内编辑、清空、调整顺序或删除；不支持 Load 路径、新行/列及显示超过 509 个 structure-key 列 |
| `Signal[signalAspectKey].Put(section, trackKey, x, y)` / `Signal[signalAspectKey].Put(section, trackKey, x, y, z, rx, ry, rz, tilt, span)`                                                  |   √   |    √     |    √     |     △      | 官方两种形式均可编辑；向导生成完整式。短式扩展编辑需确认转换；3D 可直接操纵 X/Y/Z                              |
| `Beacon.Put(type, section, sendData)`                                                                                                                                                       |   √   |    √     |    √     |     ✕      | 可编辑、新建或删除距离及全部参数                                                                               |
| `SpeedLimit.Begin(v)` / `SpeedLimit.End()`                                                                                                                                                  |   √   |    √     |    √     |     ✕      | Begin/End 独立支持编辑、新建和删除，不做配对或类型转换                                                         |
| `PreTrain.Pass(time)` / `PreTrain.Pass(second)`                                                                                                                                             |   √   |    ✕     |    ✕     |     ✕      | 进入只读列表及地图标记；不支持基于源码的编辑或新建                                                             |
| `Light.Ambient(...)`、`Light.Diffuse(...)`、`Light.Direction(...)`                                                                                                                          |   ✕   |    ✕     |    ✕     |     -      | 会校验参数形状，但语句不进入地图模型或 3D 渲染器                                                               |
| `Fog.Interpolate(density, red, green, blue)` / `Fog.Interpolate(density)` / `Fog.Interpolate()` / `[旧式] Fog.Set(density, red, green, blue)`                                               |   √   |    √     |    √     |     ✕      | 官方 0/1/4 参数 Interpolate 及旧式 Set 均支持编辑、新建和删除；3D 显示插值指数雾                               |
| `[旧式] Legacy.Fog(start, end, red, green, blue)`                                                                                                                                            |   √   |    ✕     |    ✕     |     ✕      | BVE 仍接受的旧式线性雾语句，只读；在独立列表及平面图/3D 标牌中按源值显示；编辑、新建与 3D 雾效果暂未实现      |
| `DrawDistance.Change(value)`                                                                                                                                                                |   √   |    √     |    √     |     ✕      | 距离/数值支持编辑、新建和删除；可选地控制场景绘制距离                                                          |
| `CabIlluminance.Interpolate(value)` / `[旧式] CabIlluminance.Set(value)`                                                                                                                    |   √   |    √     |    √     |     ✕      | 距离/数值支持编辑、新建和删除；不模拟驾驶台亮度效果                                                            |
| `CabIlluminance.Interpolate()`                                                                                                                                                              |   ✕   |    ✕     |    ✕     |     ✕      | 官方无参数形式可通过语法校验，但目前不生成预览/编辑行                                                          |
| `Irregularity.Change(x, y, r, lx, ly, lr)`                                                                                                                                                  |   √   |    √     |    √     |     ✕      | 距离及全部 6 个数值支持编辑、新建和删除；不模拟车辆振动                                                        |
| `Adhesion.Change(a)` / `Adhesion.Change(a, b, c)`                                                                                                                                           |   √   |    √     |    √     |     ✕      | 官方两种形状均支持编辑、新建和删除；不模拟粘着效果                                                             |
| `Sound.Load(filePath)`                                                                                                                                                                      |   √   |    △     |    ✕     |     -      | 已有音效列表行可行内编辑、清空、调整顺序或删除；不支持 Load 路径或新增行                                       |
| `Sound[soundKey].Play()`                                                                                                                                                                    |   √   |    √     |    √     |     ✕      | 距离/key 支持编辑、新建和删除；不实际播放音频                                                                  |
| `Sound3D.Load(filePath)`                                                                                                                                                                    |   √   |    △     |    ✕     |     -      | 已有 3D 音效列表行可行内编辑、清空、调整顺序或删除；不支持 Load 路径或新增行                                   |
| `Sound3D[soundKey].Put(x, y)`                                                                                                                                                               |   √   |    √     |    √     |     ✕      | 距离/key/X/Y 支持编辑、新建和删除；3D 标牌指向固定音源，X/Y/Z 操纵器编辑 X/Y 或整米里程；不实际播放音频        |
| `RollingNoise.Change(index)`                                                                                                                                                                |   √   |    √     |    √     |     ✕      | 距离/index 支持编辑、新建和删除；不实际播放音频                                                                |
| `FlangeNoise.Change(index)`                                                                                                                                                                 |   √   |    √     |    √     |     ✕      | 距离/index 支持编辑、新建和删除；不实际播放音频                                                                |
| `JointNoise.Play(index)`                                                                                                                                                                    |   √   |    √     |    √     |     ✕      | 距离/index 支持编辑、新建和删除；不实际播放音频                                                                |
| `Train.Add(trainKey, filePath, trackKey, direction)` / `Train[trainKey].Load(filePath, trackKey, direction)`                                                                                |   △   |    ✕     |    ✕     |     ✕      | 可显示定义，但外部他列车文件仅被部分建模                                                                       |
| `Train[trainKey].Enable(time)` / `Train[trainKey].Enable(second)`                                                                                                                           |   √   |    ✕     |    ✕     |     ✕      | 唯一 Enable 时间以只读形式显示在对应他列车停止位置表上方                                                       |
| `Train[trainKey].Stop(decelerate, stopTime, accelerate, speed)`                                                                                                                             |   √   |    ✕     |    ✕     |     ✕      | 生成只读他列车停止位置表、路径和地图标记                                                                       |

## 安装与启动

当前仓库未提供独立安装程序或预构建发行包。请按照[开发者指南](dev_zhcn.md)构建应用，然后运行生成的可执行文件。

构建完成后，运行 `build_release\komapedit.exe`。可执行文件保留在第 1 层，
`maploader.dll`、`model_loader.dll` 及构建复制的 Assimp/运行时依赖 DLL
统一从 `build_release\bin` 加载。

程序启动时会按需新建 `settings` 目录，并在其中创建或读取：

- `settings/imgui.ini`：用户界面内的窗口位置等信息
- `settings/settings.ini`：保存界面语言、字体/组件/车站标记大小、2D 线宽、主题色、编辑模式警告状态，以及打开地图时自动加载场景预览、雾效果、绘制距离、操纵器尺寸、相机速度和性能警告等 3D 画布设置
- `settings/history.ini`：最近打开地图和背景图对齐参数

设置读取器只接受当前程序写出的精确节名、键名和值格式。旧别名、错误节中的
键以及宽松值格式均被忽略并使用默认值。读取已有的不完整或旧格式文件时不会
自动改写；用户显式保存设置后，程序会写出完整的当前规范格式。

随附程序要求 maploader API v8，并只通过 `kv_load_map_ex()` 加载地图；精确
API 版本检查会拒绝旧 DLL。构建及发布清理脚本不会迁移或删除输出根目录中的
旧 INI 或 DLL；发现这些文件时会立即中止，并要求使用干净的 `bin`/`settings`
布局。

## 使用方法

1. 通过菜单 `文件 -> 打开...` 或工具栏 `打开` 选择`.txt`或`.csv`地图文件
2. 地图加载完成后，中央窗口显示平面图、纵断面图和曲线半径图
3. 在平面图中：
   - 左键拖动平移
   - 鼠标滚轮缩放
   - 按住 `Shift` 滚轮旋转，或使用鼠标右键/`Ctrl + 左键` 拖动旋转
   - 双击平面图恢复自适应范围
4. 在工具栏使用“车站跳转”或“跳转到里程(m)”，可定位到车站或数字里程
5. 在 `2D 视图` 菜单中切换 2D 视图窗口、坡度图、曲线半径图、坡度叠加、纵断面其他轨道和背景图相关操作。打开编辑模式后，可右键平面图、纵断面图或曲线半径图中已配对的曲线/坡度变化点标记，打开“属性/编辑”或删除对应源语句。
6. 在 `辅助信息` 菜单中切换车站、轨道几何、信号、音效、效果和 3D 场景辅助标记。他列车路径通过 `地图信息 -> 他列车列表` 控制。“闭塞标记”默认关闭，并同时控制 2D/3D 中的绿色 `S` 标记及其信号索引参数标签。在 `辅助信息 -> 其它` 中打开 `文件结构图`，可查看入口地图及其嵌套 Include 文件；右键源码文件节点可打开只读文本预览。信号标记通过 `地图信号列表` 中的“显示”勾选框控制。
7. 在 `模式` 中选择“测量”，在轨道附近移动或双击，可查看里程、标高、坡度、曲线半径和限速
8. 在 `地图信息` 菜单中打开车站、轨道、他列车、布景、连续布景、信号、应答器、音效、轨道变位/粘着、背景、驾驶台亮度、雾效果和绘制距离相关表格。有平面位置的行可定位到平面图；模型和音效文件行可打开关联文件。
   - `信号现示列表`：查看和查找信号现示定义；打开编辑模式后可编辑、调整顺序、清空或删除已有现示行
   - `地图信号列表`：查看信号位置，并使用行内“显示”勾选框控制平面图标记；打开编辑模式后可对已有 `Signal.Put` 行执行“属性/编辑”或删除
   - `应答器列表`：查看并定位应答器位置；打开编辑模式后可对已有 `Beacon.Put` 行执行“属性/编辑”或删除
   - `音效文件列表` 和 `3D音效文件列表`：查看已加载的音效文件条目、查找匹配 key、查找未使用条目，并打开关联文件。打开编辑模式后可编辑、调整顺序、清空或删除已有 key、路径和缓冲区数量；“选择文件”会尽可能写入相对路径
   - `地图音效列表`、`地图3D音效列表`、`走行音变化点列表`、`轮缘摩擦音效变化点列表` 和 `道岔音效播放点列表`：查看并定位音效播放/变化位置；打开编辑模式后可对已有事件执行“属性/编辑”或删除
    - `限速点列表`、`轨道变位列表`、`粘着特性变化点列表`、`背景变化点列表`、`驾驶台亮度变化点列表`、`雾效果变化点列表` 和 `绘制距离变化点列表`：查看并定位对应变化点；打开编辑模式后可对已支持的行执行“属性/编辑”或删除。限速 Begin/End 可独立存在，Begin 显示距离和限速值编辑，End 只显示距离编辑
   - `其他轨道`：切换他轨道显示、调整显示范围和颜色。打开编辑模式后，可右键 `Key` 单元格并选择“重命名”，统一修改地图及全部 Include 中同名 `Track[...]` 语句的 `trackKey`。该操作沿用现有“应用/保存”流程；Structure、Signal、Repeater 等地图元素中的轨道引用不会随之改名
   - `他列车列表`：查看他列车定义和停止位置、各停止位置分组唯一的只读 `Train.Enable` 时间，切换路径显示，并定位停止位置到平面图
   - `闭塞列表`：在两个动态列表中分别查看 `Section.Begin`/`BeginNew` 和 `Section.SetSpeedLimit`/`Signal.SpeedLimit`，包括显式 `null` 参数与源文件。编辑模式下右键距离单元格可打开“属性/编辑”或删除该行；检查器可编辑距离和可变数量参数，并提供增加/删除参数的按钮
   - `变量列表`：按大小写不敏感的变量名分组查看所有赋值；悬停赋值可见原表达式，源文件右键菜单可打开所在目录
   - 当入口地图及其 Include 中同一种无 key 列表 Load 超过一条，或存在大小写不敏感意义下重复的 `Train[].Enable` 时，本次地图加载会被拒绝
    - `车站列表`：分别查看 `Station.Put` 位置行和 `Station.Load` 定义行；打开编辑模式后，位置行支持“属性/编辑”和删除，已有定义行可编辑、调整顺序、清空或删除
   - `地图布景列表`：查看地图中的 `Structure.Put`、`Structure.Put0`、`Structure.PutBetween`；3D 场景已加载时可定位到对应场景对象
   - `布景模型列表`：查看 `Structure.Load` 指定列表中的 structureKey 和模型文件。车站、布景模型、信号现示、音效文件和 3D 音效文件列表顶部显示加载语句求值后保留原样的路径；悬停可见参数原式与解析绝对路径，右键可在资源管理器中打开。右键 structureKey 并选择 `预览模型` 可打开 3D 模型预览。打开编辑模式后可编辑、调整顺序、清空或删除已有 key 和路径
    - `连续布景列表`：查看关联的 `Repeater.Begin`/`Begin0`/`End` 段，通过“属性/编辑”修改，并在 3D 场景已加载时定位到对应连续布景实例
      - 连续布景“属性/编辑”中的“插入变化点”会打开对应 Begin/Begin0 的向导，默认仅添加 Begin，两个距离字段填入当前起始里程，并复制当前检查器草稿参数供继续修改；原有确认与“应用/保存”流程保持不变。
          - 打开工具栏“编辑模式”后，可从表格或适用的 2D/3D 标记对已支持的放置、Repeater、闭塞、限速点、轨道变位、应答器、音效/噪声、背景、粘着、驾驶台亮度、雾和绘制距离行使用“属性/编辑”。对于 `Structure.Put`/`Put0` 和 `Repeater.Begin`/`Begin0`，参数输入框上方的按钮可添加或去除全部六项坐标偏移；去除非零偏移时会先确认，因为这些值将被丢弃。使用“新建地图元素”向导新增受支持的地图放置和事件/效果语句。Repeater 表单可在一次“应用”中新增 Begin/Begin0、End 或两者；成对新增时 End 里程不得小于 Begin 里程。同名有效区间内的 Begin-only 需确认后作为变化点插入，成对区间仍执行普通重叠保护；已有显式 End 的同名有效区间内禁止增加 End，但允许孤立 End。“轨道几何”分类可新建 `Curve.Begin(radius)`、`Curve.Change(radius)`、`Curve.End()`、`Gradient.Begin(gradient)` 与 `Gradient.End()`；`Curve.Begin(radius, cant)` 始终与前置 `Curve.BeginTransition()` 原子创建，Curve End 和两种 Gradient 操作可选择同时添加缓和曲线起点。每个成对缓和曲线起点和后续 Curve/Gradient 生效语句均可分别设定里程；向导保持 BVE 要求的源语句顺序，且只输出当前官方形式。旧式别名和 Interpolate 不提供新建入口。数值目标 distance 沿用属性/编辑窗口的源码表达式、距离边界和环境检查流程。由 `Station.Load`、`Structure.Load`、`Signal.Load`、`Sound.Load` 和 `Sound3D.Load` 载入的已有定义/资源行在各自的行内表格中编辑；列表编辑器不会新增资源/定义行。保存前必须先应用各行内表格草稿，否则“保存”会被阻止。“应用”只更新内存预览，工具栏“保存”或 `Ctrl+S` 才写入源文件；“撤销”会丢弃全部待保存的内存改动，“重新加载”会重新读取磁盘文件。
          - “其他轨道”分类可新建当前所有可编辑的他轨道变化形式。每种 method 只有一个模板；可选尾参数通过默认勾选的“包含此参数”复选框控制，且必须连续（例如 `radiusV` 需要 `radiusH`）。`trackKey` 可自由输入：新的规范 key 自动创建一条他轨道，后续大小写不敏感的同 key 变化点复用该轨道；数值 key 与带引号的字符串 key 保持区分。向导只输出现行官方 `Track.*` 形式，不生成旧式 `Track.Gauge` 或 `Track.Cant` 别名。
     首次启用时会显示确认提示：编辑属于不稳定的测试性功能，可能会对地图文件产生破坏性更改。请先备份地图文件，或使用 Git 等版本控制工具管理；勾选“不再显示”并确认后，之后不再提示。
9. 在 `2D 视图 -> 背景图` 中导入背景图，可手动调整位置、尺寸、旋转和亮度，也可按两个车站对齐
10. 在 `3D 视图 -> 布景模型预览` 中显示或隐藏布景模型预览窗口。预览窗口内可用鼠标左键拖动旋转模型，用鼠标滚轮缩放
11. 在 `3D 视图 -> 3D场景预览` 中显示场景预览窗口。若启用 `选项 -> 3D画布设置 -> 打开地图时自动加载场景预览`（默认关闭），打开或重新加载地图时会自动启动场景；否则点击 `启动3D场景预览`。场景预览可在窗口中重新加载或关闭；加载场景后，车站跳转和里程跳转也会移动场景相机。叠加层会显示当前曲线/超高、坡度、生效中的 `SpeedLimit.Begin`/`End` 状态、当前 `Section.Begin` 索引从生效中的 `Section.SetSpeedLimit`/`Signal.SpeedLimit` 定义所选择的信号限速，以及下一站信息。`选项 -> 3D画布设置 -> 雾效果` 可即时切换场景预览中的线路雾效果，且默认开启；同一设置还可控制地图语句驱动的绘制距离、相机速度和场景实例性能警告。里程选择模式下，将鼠标停靠在画布上会以半透明横断面高亮自轨道平面上最近的整米位置，并显示跟随鼠标的里程标签。右键单击高亮位置可打开“新建地图元素向导”并自动填写 distance；启用编辑模式前，该菜单项保持禁用。选择模式下可从场景对象和支持的地图元素标记定位回对应表格；打开编辑模式后，受支持的他轨道变化点会在 2D 平面图中显示为轨道颜色圆点，并在已启动的 3D 场景中显示为轨道颜色标牌，右键可打开“属性/编辑”或删除。其他受支持的场景标记沿用相同菜单路径，`Structure.Put`、`Signal.Put` 和 `Repeater.Begin` 坐标还可使用 X/Y/Z 操纵器拖动。`Sound3D[soundKey].Put(x, y)` 标牌为尖端落在相对自轨道固定音源位置的标签；在“属性/编辑”中，X/Y 拖动以 0.001 m 更新 `x`/`y`，Z 拖动以整米更新 `distance`，标牌和操纵器都会实时跟随草稿。`Structure.Put0` 或 `Repeater.Begin0` 草稿会在其放置轨道上显示仅 Z 轴操纵器，以整米步进修改放置/起始里程并实时更新场景；检查器中的坐标偏移按钮会同步切换这两种操纵器模式。具有显式 `End` 的 Repeater 段还会在该 EndDistance 的轨道中心线上显示仅 Z 轴操纵器；它会把 EndDistance 吸附到整米值并实时更新连续布景实例，他轨道与自轨道不平行时也按自轨道 distance 比例换算拖动距离。`Structure.PutBetween` 的检查器草稿会实时重算变形模型顶点，其仅 Z 轴操纵器会把 `distance` 吸附并写入整米值。
12. 在 `文件 -> 导出 CSV...` 中选择输出目录，导出自轨道和他轨道几何 CSV
13. 按 `F5` 或菜单 `文件 -> 重新加载` 可重新读取当前地图

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

| 字段             | 说明                                             |
| ---------------- | ------------------------------------------------ |
| distance         | 地图绝对距离，单位 m                             |
| x                | 考虑纵坡投影后计算出的自轨道平面 X 坐标          |
| y                | 考虑纵坡投影后计算出的自轨道平面 Y 坐标          |
| z                | 标高                                             |
| direction        | 轨道方向角，单位 rad                             |
| radius           | 当前曲线半径                                     |
| gradient         | 当前坡度，按 BVE 的千分率语义处理                |
| interpolate_func | 插值类型，`0` 表示正弦半波缓动，`1` 表示线性缓动 |
| cant             | 超高                                             |
| center           | 轨道中心偏移                                     |
| gauge            | 轨距                                             |

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

| 字段             | 说明                                        |
| ---------------- | ------------------------------------------- |
| distance         | 地图绝对距离，单位 m                        |
| x                | 根据投影后的自轨道计算出的他轨道平面 X 坐标 |
| y                | 根据投影后的自轨道计算出的他轨道平面 Y 坐标 |
| z                | 他轨道标高                                  |
| interpolate_func | 插值类型，`0` 表示 `sin`，`1` 表示 `line`   |
| cant             | 超高                                        |
| center           | 轨道中心偏移                                |
| gauge            | 轨距                                        |

导出的数值使用固定 6 位小数。当前 CSV 导出仅包含轨道几何，不导出车站、布景、连续布景、信号、应答器、音效/噪声事件、轨道变位或粘着变化、背景变化点、驾驶台亮度变化点、雾、绘制距离变化或 3D 场景数据。

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
