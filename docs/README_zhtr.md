<p align="center">
    <img src="../icons/titleimage.png" alt="komapedit" width="600">
</p>

# komapedit

komapedit 是一款 Windows 輕量級 BVE Trainsim 地圖檢視與編輯工具，支援二維路線圖、三維場景預覽，以及以原始碼為依據的地圖編輯。

編輯功能處於實驗階段，使用前請備份路線檔案或使用版本控制管理。各類陳述式的支援範圍見[支援的 BVE 地圖語法](#支援的-bve-地圖語法)，開發進度見 [TODO.md](../TODO.md)。

## 文件導覽

- [使用者手冊](user-manual_zhtr.md)
- [開發進度（待辦事項清單）](../TODO.md)
- [開發者指南](dev_zhtr.md)
- [AI 輔助開發指南](ai-dev_zhtr.md)
- [供 AI 程式設計工具使用的儲存庫規範](../AGENTS.md)
- [授權條款](../LICENSE)、[專案聲明](../NOTICE)與[第三方聲明](../THIRD_PARTY_NOTICES.md)

## 主要功能

- **介面**：可拖曳、停駐的多視窗配置，提供資料表格搜尋，以及表格、二維圖和三維場景之間的定位。
- **預覽**：顯示路線平面圖、縱斷面圖、輔助標記、佈景模型和三維場景；支援背景圖對齊、測量和軌道幾何 CSV 匯出。
- **編輯**：透過屬性視窗、資源清單和新增精靈修改、刪除或新增受支援的地圖元素，使用三維操縱器調整部分元素的位置；支援子地圖管理，以及地圖、Scenario 和資源清單檔案的建立與編輯。
- **介面語言**：簡體中文、英文和日文。

## 支援的 BVE 地圖語法

- 讀取/預覽：剖析資料，並透過軌道幾何、表格、標記或 3D 場景顯示。
- 基本編輯：透過屬性視窗、表格或檔案操作選單修改已有內容並儲存。
- 新增：透過地圖元素精靈、新增檔案精靈、檔案結構圖或資源表格建立陳述式、檔案及清單列。
- 圖形化編輯：在 2D/3D 畫布中直接拖曳元素或使用操縱器修改。
- √ = 支援該列所列的功能和形式；△ = 部分形式、欄位或關聯操作可用；✕ = 尚未支援；- = 不適用。

下表涵蓋程式目前所有操作入口。同一列列出多種多載形式或別名時，若只能新增部分形式，標示為 △。`*.Load` 列涵蓋引用路徑、清單檔案及其內容，支援範圍見說明。語法包括 [BVE 官方地圖語法](https://bvets.net/jp/edit/formats/route/map.html)、舊式別名，以及明確標註的專案相容陳述式或註解。

| 地圖語法                                                                                                                                                                                    | 讀取/預覽 | 基本編輯 | 新增 | 圖形化編輯 | 說明 |
| ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | :---: | :------: | :------: | :--------: | -------------------------------------------------------------------------------------------------------------- |
| 檔頭、版本及編碼                                                                                                                                                                          | △ | ✕ | △ | - | 支援 BVE Map 2.0+ 及 UTF-8、UTF-16LE/BE、CP932/Shift_JIS；新增地圖使用 `BveTs Map 2.02:utf-8` 檔頭 |
| 註解及基本陳述式結構                                                                                                                                                                          |   √   |    ✕     |    -     |     -      | 支援 `#`/`//` 註解、分號分隔、帶 key 及巢狀元素、多行陳述式；名稱不區分大小寫 |
| [專案註解] `//--kme--message-from-creator:"content"` | √ | √ | √ | - | 使用普通 BVE 註解儲存訊息；預設在開啟地圖時展示，也可在自訂訊息表中編輯或刪除，在「其他」精靈中新增 |
| 指派、參數及 key 中的變數                                                                                                                                                                   | √ | ✕ | ✕ | - | 剖析時求值；唯讀變數表按名稱分組顯示指派及來源 |
| 算術運算子（`+`、`-`、`*`、`/`、`%`）                                                                                                                                                       | √ | △ | △ | - | 支援數值運算、一元正負運算子、括號及 `+` 字串串接；編輯或新增元素時，可在里程運算式確認流程中輸入數值運算式 |
| 距離宣告及 `distance` 運算式                                                                                                                                                                | √ | △ | △ | △ | 隨受支援元素編輯或新增距離區塊；按提示填寫里程運算式，求值須與目標里程一致；部分 3D 物件可透過 Z 軸按整公尺調整里程 |
| 數學函式                                                                                                                                                                                    | √ | △ | △ | - | 支援 `rand`、`abs`、`sin`、`cos`、`atan2`、`sqrt`、`exp`、`log`、`floor`、`ceil`、`pow`；可在里程運算式確認流程中使用 |
| `include 'file';`                                                                                                                                                                           | √ | √ | √ | - | 檔案結構圖可更換、解除、匯入或新增子地圖引用，新增檔案精靈也可新增 Include；缺失或無效的子地圖會跳過並記錄警告 |
| `Curve.SetGauge(value)` / `[舊式] Curve.Gauge(value)`                                                                                                                                       | √ | √ | △ | ✕ | 編輯里程與軌距；精靈新增 `SetGauge`，預設 `1.067`；2D/3D 可顯示 `CG` 標牌 |
| `Curve.SetCenter(x)`                                                                                                                                                                        |   √   |    √     |    √     |     ✕      | 編輯里程與超高中心偏移；新增預設值為 `0`；2D/3D 可顯示 `CC` 標牌 |
| `Curve.SetFunction(id)`                                                                                                                                                                     |   √   |    √     |    √     |     ✕      | 編輯里程與內插函式，`id` 為 `0` 或 `1`；新增預設 `0`；2D/3D 可顯示 `CF` 標牌 |
| `Curve.BeginTransition()`                                                                                                                                                                   |   √   |    △     |    △     |     ✕      | 已有緩和段透過配對 Begin/End 編輯里程；新增時隨帶超高的 Begin 或 End 新增，起止緩和選項聯動、里程分別設定 |
| `Curve.Begin(radius, cant)` / `[舊式] Curve.BeginCircular(radius, cant)`                                                                                                                    |   √   |    √     |    △     |     ✕      | 精靈將帶超高的 Begin 與前置緩和曲線一同新增；舊式 BeginCircular 支援已有陳述式編輯 |
| `Curve.Begin(radius)` / `Curve.Change(radius)`                                                                                                                                              |   √   |    √     |    √     |     ✕      | 精靈可選擇 Begin 或 Change，並一同新增結束位置 |
| `Curve.End()`                                                                                                                                                                               |   √   |    √     |    √     |     ✕      | 可單獨新增或隨起點新增，也可設定前置緩和曲線 |
| `Curve.Interpolate(radius, cant)` / `Curve.Interpolate(radius)` / `Curve.Interpolate()`                                                                                                     |   √   |    √     |    √     |     ✕      | 支援 0/1/2 參數形式，編輯時保留參數個數；開啟「曲線半徑」後可從 2D/3D 標記編輯或刪除 |
| `Gradient.BeginTransition()`                                                                                                                                                                |   √   |    △     |    △     |     ✕      | 已有緩和段透過配對 Begin/End 編輯里程；新增時隨 Begin/End 新增，起止緩和選項聯動、里程分別設定 |
| `Gradient.Begin(gradient)` / `[舊式] Gradient.BeginConst(gradient)`                                                                                                                         | √ | √ | △ | ✕ | 精靈可單獨新增 Begin，或一同新增 End、前置緩和曲線；舊式 BeginConst 支援已有陳述式編輯 |
| `Gradient.End()`                                                                                                                                                                            |   √   |    √     |    √     |     ✕      | 可單獨新增或隨 Begin 新增，也可設定前置緩和曲線 |
| `Gradient.Interpolate(gradient)` / `Gradient.Interpolate()`                                                                                                                                 |   √   |    ✕     |    ✕     |     ✕      | 0/1 參數形式均用於軌道幾何計算 |
| `Legacy.Turn`、`Legacy.Curve`、`Legacy.Pitch`                                                                                                                                               |   √   |    △     |    ✕     |     ✕      | 專案相容陳述式，均用於自軌道幾何；已有 Legacy.Curve 可編輯數值與里程 |
| `Track[trackKey].X.Interpolate(x, radius)` / `Track[trackKey].X.Interpolate(x)` / `Track[trackKey].X.Interpolate()`                                                                         |   √   |    △     |    √     |     ✕      | 支援各參數形式；可編輯里程、數值或刪除。單條 `trackKey` 唯讀，可在「其他軌道」表統一重新命名 |
| `Track[trackKey].Y.Interpolate(y, radius)` / `Track[trackKey].Y.Interpolate(y)` / `Track[trackKey].Y.Interpolate()`                                                                         |   √   |    △     |    √     |     ✕      | 支援各參數形式；編輯範圍與 X.Interpolate 相同 |
| `Track[trackKey].Position(x, y, radiusH, radiusV)` / `Track[trackKey].Position(x, y, radiusH)` / `Track[trackKey].Position(x, y)`                                                           |   √   |    △     |    √     |     ✕      | 支援各參數形式；可編輯里程、數值或刪除，保留參數個數；key 重新命名範圍同 X.Interpolate |
| `Track[trackKey].Cant.SetGauge(gauge)` / `[舊式] Track[trackKey].Gauge(gauge)`                                                                                                              | √ | △ | △ | ✕ | 新增使用 Cant.SetGauge；兩種形式均可編輯數值、里程或刪除；key 重新命名範圍同 X.Interpolate |
| `Track[trackKey].Cant.SetCenter(x)`                                                                                                                                                         |   √   |    △     |    √     |     ✕      | 可編輯數值、里程或刪除；key 重新命名範圍同 X.Interpolate |
| `Track[trackKey].Cant.SetFunction(id)`                                                                                                                                                      |   √   |    △     |    √     |     ✕      | 新增時 `id` 為 `0` 或 `1`；可編輯數值、里程或刪除；key 重新命名範圍同 X.Interpolate |
| `Track[trackKey].Cant.BeginTransition()`                                                                                                                                                    |   √   |    △     |    √     |     ✕      | 可編輯里程或刪除；key 重新命名範圍同 X.Interpolate |
| `Track[trackKey].Cant.Begin(cant)`                                                                                                                                                          |   √   |    △     |    √     |     ✕      | 可編輯數值、里程或刪除；key 重新命名範圍同 X.Interpolate |
| `Track[trackKey].Cant.End()`                                                                                                                                                                |   √   |    △     |    √     |     ✕      | 可編輯里程或刪除；key 重新命名範圍同 X.Interpolate |
| `Track[trackKey].Cant.Interpolate(cant)` / `Track[trackKey].Cant.Interpolate()` / `[舊式] Track[trackKey].Cant(cant)`                                                                       | √ | △ | △ | ✕ | 新增使用 Interpolate 的 0/1 參數形式；已有陳述式可編輯數值、里程或刪除，保留參數個數；key 重新命名範圍同 X.Interpolate |
| `Structure.Load(filePath)`                                                                                                                                                                  | √ | √ | √ | - | 可更換清單路徑、新增或匯入清單並新增 Load 引用；表格支援編輯、增刪和排序 key/path 列，包括空清單第一列；相容清單版本 1.00+ |
| `Structure[structureKey].Put(trackKey, x, y, z, rx, ry, rz, tilt, span)`                                                                                                                    |   √   |    √     |    √     |     △      | 全部欄位可編輯；3D 操縱器調整 X/Y/Z |
| `Structure[structureKey].Put0(trackKey, tilt, span)`                                                                                                                                        |   √   |    √     |    √     |     △      | 屬性視窗可在 Put0/Put 間轉換；Put0 的 Z 軸操縱器按整公尺調整里程 |
| `Structure[structureKey].PutBetween(trackKey1, trackKey2, flag)` / `Structure[structureKey].PutBetween(trackKey1, trackKey2)`                                                               | √ | √ | △ | △ | 兩種形式均可編輯，新增使用 3 參數形式；屬性草稿即時更新 3D 形狀，Z 軸操縱器按整公尺調整里程 |
| `Repeater[repeaterKey].Begin(trackKey, x, y, z, rx, ry, rz, tilt, span, interval, structureKey1, ...)` / `Repeater[repeaterKey].Begin0(trackKey, tilt, span, interval, structureKey1, ...)` | √ | √ | √ | △ | 可編輯全部參數、佈景鍵清單及整鏈 key；支援 Begin/Begin0 轉換、變化點、關聯刪除和位置操縱器；可單獨或與 End 成對新增，同名成對區間須避免重疊 |
| `Repeater[repeaterKey].End()`                                                                                                                                                               |   √   |    △     |    √     |     △      | 透過所屬 Begin 編輯結束里程或使用 End 操縱器；可單獨或成對新增，孤立 End 無獨立屬性視窗；同名閉合區間內禁止重複新增 End |
| `Background.Change(structureKey)`                                                                                                                                                           |   √   |    √     |    √     |     ✕      | 編輯里程與 key；在場景中預覽背景 |
| `Station.Load(filePath)`                                                                                                                                                                    | √ | √ | √ | - | 可更換清單路徑、新增或匯入清單並新增 Load 引用；表格支援編輯 13 個欄位、增刪和排序車站列，包括空清單第一列；相容清單版本 0.04+ |
| `Station[stationKey].Put(door, margin1, margin2)`                                                                                                                                           |   √   |    √     |    √     |     ✕      | 編輯里程、key、車門側及停車容許範圍；容許範圍要求 `margin1 < 0`、`margin2 > 0` |
| `Section.Begin(...)` / `[舊式] Section.BeginNew(...)`                                                                                                                                       |   √   |    √     |    √     |     ✕      | 編輯里程與號誌索引清單；2D/3D 顯示標記 |
| `Section.SetSpeedLimit(...)` / `[舊式] Signal.SpeedLimit(...)`                                                                                                                              |   √   |    √     |    √     |     ✕      | 編輯里程與限速清單；生效值顯示在 3D 號誌摘要中 |
| `Signal.Load(filePath)`                                                                                                                                                                     | √ | △ | √ | - | 可更換、新增或匯入顯示狀態清單並新增 Load 引用，也可新增主列、眩光列及欄位；最多顯示 509 個佈景鍵欄；保留多條眩光列時須維持合併欄位數 |
| `Signal[signalAspectKey].Put(section, trackKey, x, y)` / `Signal[signalAspectKey].Put(section, trackKey, x, y, z, rx, ry, rz, tilt, span)`                                                  | √ | √ | △ | △ | 兩種形式均可編輯，新增使用完整式；短式擴充需確認轉換；3D 操縱器調整 X/Y/Z |
| `Beacon.Put(type, section, sendData)`                                                                                                                                                       |   √   |    √     |    √     |     ✕      | 編輯里程與全部參數 |
| `SpeedLimit.Begin(v)` / `SpeedLimit.End()`                                                                                                                                                  |   √   |    √     |    √     |     ✕      | Begin/End 分別編輯、新增和刪除 |
| `PreTrain.Pass(time)` / `PreTrain.Pass(second)` | √ | √ | √ | ✕ | 透過 2D/3D 標記編輯里程、通行時間或刪除；在「號誌」精靈中新增。時間輸入為 hh:mm:ss 或秒數，將滑鼠停在 `passTime` 上 可檢視格式提示 |
| `Light.Ambient(...)`、`Light.Diffuse(...)`、`Light.Direction(...)`                                                                                                                          |   √   |    √     |    √     |     ✕      | 在「光照效果」表中檢視和編輯參數，在「效果」精靈中於里程 `0` 新增。RGB 範圍 `[0, 1]`，Direction 位於里程 `0`；同類宣告在根地圖及 Include 中須唯一 |
| `Fog.Interpolate(density, red, green, blue)` / `Fog.Interpolate(density)` / `Fog.Interpolate()` / `[舊式] Fog.Set(density, red, green, blue)`                                               |   √   |    √     |    √     |     ✕      | 支援 Interpolate 的 0/1/4 參數形式及舊式 Set；3D 預覽指數霧及其過渡 |
| `[相容] Legacy.Fog(start, end, red, green, blue)`                                                                                                                                            |   √   |    √     |    √     |     ✕      | 在表格或 2D/3D 標記中編輯；3D 預覽線性霧及其過渡，可與 Fog 混用 |
| `DrawDistance.Change(value)`                                                                                                                                                                |   √   |    √     |    √     |     ✕      | 編輯里程與數值；可用於控制場景繪製距離 |
| `CabIlluminance.Interpolate(value)` / `CabIlluminance.Interpolate()` / `[舊式] CabIlluminance.Set(value)`                                                                                     |   √   |    √     |    √     |     ✕      | 編輯里程與亮度；數值留空時寫為 Interpolate()，表格與 3D 標牌顯示前一有效值，無前值時留空 |
| `Irregularity.Change(x, y, r, lx, ly, lr)`                                                                                                                                                  |   √   |    √     |    √     |     ✕      | 在表格和標記中檢視資料；可編輯里程及全部 6 個參數 |
| `Adhesion.Change(a)` / `Adhesion.Change(a, b, c)`                                                                                                                                           |   √   |    √     |    √     |     ✕      | 在表格和標記中檢視資料；支援 1/3 參數形式 |
| `Sound.Load(filePath)`                                                                                                                                                                      | √ | √ | √ | - | 可更換清單路徑、新增或匯入清單並新增 Load 引用；表格支援編輯 key、路徑、緩衝區數量及增刪排序，包括空清單第一列；相容清單版本 2.00+ |
| `Sound[soundKey].Play()`                                                                                                                                                                    |   √   |    √     |    √     |     ✕      | 在表格和標記中檢視音效事件；編輯里程與 key |
| `Sound3D.Load(filePath)`                                                                                                                                                                    | √ | √ | √ | - | 可更換、新增或匯入 3D 音效清單並新增 Load 引用；表格支援編輯 key、路徑、緩衝區數量及增刪排序，包括空清單第一列；相容清單版本 2.00+ |
| `Sound3D[soundKey].Put(x, y)`                                                                                                                                                               |   √   |    √     |    √     |     △      | 編輯里程、key、X/Y；3D 標牌標示音源位置，操縱器調整 X/Y 和整公尺里程 |
| `RollingNoise.Change(index)`                                                                                                                                                                |   √   |    √     |    √     |     ✕      | 在表格和標記中檢視噪聲事件；編輯里程與 index |
| `FlangeNoise.Change(index)`                                                                                                                                                                 |   √   |    √     |    √     |     ✕      | 在表格和標記中檢視噪聲事件；編輯里程與 index |
| `JointNoise.Play(index)`                                                                                                                                                                    |   √   |    √     |    √     |     ✕      | 在表格和標記中檢視噪聲事件；編輯里程與 index |
| `Train.Add(trainKey, filePath, trackKey, direction)` / `Train[trainKey].Load(filePath, trackKey, direction)`                                                                                |   △   |    ✕     |    ✕     |     ✕      | 顯示其他列車定義，部分讀取外部定義檔案 |
| `Train[trainKey].Enable(time)` / `Train[trainKey].Enable(second)`                                                                                                                           |   √   |    ✕     |    ✕     |     ✕      | 在其他列車停止位置表上方顯示啟用時間 |
| `Train[trainKey].Stop(decelerate, stopTime, accelerate, speed)`                                                                                                                             |   √   |    ✕     |    ✕     |     ✕      | 顯示唯讀的其他列車停止位置表、路徑與地圖標記 |

## 安裝與啟動

### 下載發行版（建議）

從 [GitHub Releases](https://github.com/NewSapporoNingyo/komapedit/releases) 下載執行檔壓縮檔，完整解壓縮後按兩下 `komapedit.exe` 執行程式。

### 自行編譯

自行編譯適合具備軟體開發基礎的使用者。

按照[開發者指南](dev_zhtr.md)建置應用程式，然後執行 `build_release\komapedit.exe`（Release）或 `build\komapedit.exe`（Debug）。

執行檔與 DLL 應使用同一建置版本。建置指令碼若提示輸出根目錄中存在舊 INI 或 DLL，請依提示整理為 `bin`/`settings` 配置後重新建置。

### 啟動與設定

程式從 `komapedit.exe` 同級的 `bin` 目錄載入 `maploader.dll`、`model_loader.dll` 及其相依項。

程式啟動時會視需要新增 `settings` 目錄，並在其中建立或讀取：

- `settings/imgui.ini`：使用者介面內的視窗位置等資訊
- `settings/settings.ini`：儲存介面語言、字型/元件/車站標記大小、2D 線寬、主題色、編輯模式警告狀態，以及開啟地圖時自動載入場景預覽、霧效果、繪製距離、操縱器尺寸、攝影機速度和效能警告等 3D 畫布設定
- `settings/history.ini`：最近開啟地圖、背景圖對齊參數，以及各地圖的自訂訊息顯示偏好

建議透過介面修改設定。程式按目前設定格式讀取有效項，其餘項使用預設值；儲存時寫入完整設定。

## 著作權、授權和第三方聲明

komapedit 以 Apache License, Version 2.0 散布。授權條款全文見 `LICENSE`，
專案著作權與歸屬聲明見 `NOTICE`。

本專案基於 `kobushi-trackviewer` 開發，用於輔助檢視和編輯 BVE Trainsim
地圖檔案。

參考專案：

| 專案                                                                                   | 著作權                               | 授權條款                      |
| -------------------------------------------------------------------------------------- | ---------------------------------- | --------------------------- |
| konawasabi 的 [kobushi-trackviewer](https://github.com/konawasabi/kobushi-trackviewer) | Copyright (c) 2021-2024 konawasabi | Apache License, Version 2.0 |

GUI 和模型預覽使用的第三方函式庫：

| 函式庫                                                                     | 用途                                                               | 著作權                                                                             | 授權條款                        |
| ---------------------------------------------------------------------- | ------------------------------------------------------------------ | -------------------------------------------------------------------------------- | ----------------------------- |
| [Dear ImGui](https://github.com/ocornut/imgui)                         | Docking GUI、Win32 後端、DirectX 11 後端、C++ std::string 輔助模組 | Copyright (c) 2014-2026 Omar Cornut                                              | MIT License                   |
| [ImPlot](https://github.com/epezent/implot)                            | 2D 圖表控制項                                                        | Copyright (c) 2020-2024 Evan Pezent；Copyright (c) 2025-2026 Breno Cunha Queiroz | MIT License                   |
| [Assimp / Open Asset Import Library](https://github.com/assimp/assimp) | 佈景模型匯入                                                       | Copyright (c) 2006-2026, assimp team                                             | Modified BSD 3-Clause License |
| Dear ImGui 隨附的 stb 單檔函式庫                                         | Dear ImGui 使用的字型、文字編輯、矩形打包支援                      | Copyright (c) 2017 Sean Barrett                                                  | MIT License 或 Public Domain  |

散布本儲存庫原始碼或由本儲存庫建置的二進位檔案時，請一併包含 `LICENSE`、
`NOTICE` 和 `THIRD_PARTY_NOTICES.md`。如果散布 `third_party/` 原始碼目錄，
請保留其中原始授權條款檔案和著作權聲明。

專案網址：<https://github.com/NewSapporoNingyo/komapedit>

## Star History

<a href="https://www.star-history.com/?repos=NewSapporoNingyo%2Fkomapedit&type=date&legend=top-left">
 <picture>
   <source media="(prefers-color-scheme: dark)" srcset="https://api.star-history.com/chart?repos=NewSapporoNingyo/komapedit&type=date&theme=dark&legend=top-left" />
   <source media="(prefers-color-scheme: light)" srcset="https://api.star-history.com/chart?repos=NewSapporoNingyo/komapedit&type=date&legend=top-left" />
   <img alt="Star History Chart" src="https://api.star-history.com/chart?repos=NewSapporoNingyo/komapedit&type=date&legend=top-left" />
 </picture>
</a>
