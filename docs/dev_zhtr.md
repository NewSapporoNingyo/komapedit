# 開發者指南

[專案首頁](README_zhtr.md) · [開發進度](../TODO.md) · [AI 輔助開發](ai-dev_zhtr.md)

本文件介紹 komapedit 的開發環境、架構、原始碼職責與驗證流程。使用 AI 程式設計工具時，須遵守 [`ai-dev_zhtr.md`](ai-dev_zhtr.md)、[`AGENTS.md`](../AGENTS.md)，並採用 [`.agents/skills`](../.agents/skills) 中適用的工作流程。

## 範圍與支援環境

komapedit 是使用 C++17 編寫的 Windows 桌面應用程式，用於檢視和編輯 BVE Trainsim 地圖。應用程式採用 Win32、DirectX 11、WIC、Dear ImGui 和 ImPlot，使用 CMake 與 Ninja 建置。

應用程式包含三個執行階段元件：

- `maploader.dll`：剖析地圖和清單、處理 Include 與編碼、產生自軌道和其他軌道幾何，並持有帶版本的強型別地圖/編輯快照。
- `model_loader.dll`：透過 Assimp 讀取 Structure 網格、材質和貼圖，並以 C ABI 提供資料。
- `komapedit.exe`：提供 Win32/DirectX 11 GUI、表格、二維圖表、三維預覽和編輯流程。

已實作功能以原始碼和使用者指南為準；待辦事項見 [`TODO.md`](../TODO.md)，完成記錄見 [`TODO_done.md`](TODO_done.md)。

## 前置環境

- Windows
- CMake 3.20 或更高版本；使用通用 Assimp 執行階段 DLL 複製備援機制時建議 3.21 或更高版本
- Ninja
- MSVC、MinGW 等支援 C++17 的編譯器
- Windows SDK、DirectX 11 和 WIC 開發函式庫
- Git
- 可被 CMake 發現為 `assimp::assimp` 的 Assimp

取得 Dear ImGui 和 ImPlot：

```bat
.\get_3rd_party_packages.bat
```

Assimp 需要單獨安裝。使用 vcpkg 時，設定 `VCPKG_ROOT` 並安裝與工具鏈相容的 triplet：

```bat
set VCPKG_ROOT=C:\path\to\vcpkg
%VCPKG_ROOT%\vcpkg install assimp:x64-mingw-dynamic
```

設定 `VCPKG_ROOT` 後，建置指令碼會自動使用 vcpkg。未設定 `VCPKG_DEFAULT_TRIPLET` 時預設使用 `x64-mingw-dynamic`；MSVC 使用者應明確選擇 `x64-windows` 等合適 triplet。`install_Assimp.bat` 是面向 MinGW 的輔助指令碼，可能需要填寫本機 vcpkg 路徑；不得提交該本機路徑。

## 建置與測試

Debug 建置：

```bat
.\build_dev.bat
```

Release 建置：

```bat
.\build_release.bat
```

### 建置完成通知

`build_dev.bat` 和 `build_release.bat` 在配置、編譯、執行階段檔案檢查及授權聲明複製步驟完成後傳送建置完成通知。

如需使用 Windows Toast 通知，請開啟 Windows PowerShell（`powershell.exe`），為目前使用者安裝可選的 [`BurntToast`](https://www.powershellgallery.com/packages/BurntToast) 模組：

```powershell
Install-Module -Name BurntToast -Scope CurrentUser
Get-Module -ListAvailable -Name BurntToast
```

指令碼透過 `powershell.exe` 探測模組並呼叫 `New-BurntToastNotification -Text 'Build finished'`，因此應在該 PowerShell 環境中確認模組可用。

未安裝 `BurntToast` 時，指令碼使用 Windows 自帶的 [`msg.exe`](https://learn.microsoft.com/windows-server/administration/windows-commands/msg) 向 `%USERNAME%` 顯示最長 10 秒的 `build finished` 訊息。

執行已註冊的 Debug 測試：

```bat
ctest --test-dir build --output-on-failure
```

嚴格驗證需要顯式配置 Debug 目錄：

```bat
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DKOMAPEDIT_STRICT_WARNINGS=ON -DBUILD_TESTING=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

`KOMAPEDIT_STRICT_WARNINGS` 預設關閉。CTest 註冊了七項契約：`multilanguage_contract`、`typed_snapshot_contract`、`maploader_gradient_projection_contract`、`typed_edit_contract`、`maploader_diagnostics_contract`、`canvas3d_camera_contract` 和 `route_value_sampling_contract`。

測試程式和 headless 實作僅在 Debug 編譯：`build_dev.bat` 啟用 `BUILD_TESTING`，`build_release.bat` 關閉測試並檢查輸出中是否殘留 `*_tests.exe`。headless 命令須單獨執行。診斷契約使用 Git 忽略的本機 `tests/` 測試素材，執行前需確認測試素材齊全。

`build\bin\typed_snapshot_tests.exe slop` 執行物件鍵運算式、Section 稀疏索引、有限距離和修補預覽回歸，相關案例也納入 snapshot/edit 契約。

`build\bin\typed_snapshot_tests.exe patch-bench 7` 對 100、1,000、10,000 項確定性更新執行完整 dry-run，測量 `plan.patch_sources` 階段。每種規模預熱一次，再輸出各次樣本及 median/p95；重複次數預設 7，範圍 5–50。此基準透過命令顯式執行。

執行階段輸出配置如下：

- Debug 使用 `build\`，Release 使用 `build_release\`。
- 輸出根目錄包含 `komapedit.exe`、`LICENSE`、`NOTICE` 和 `THIRD_PARTY_NOTICES.md`。
- `bin\` 包含 `maploader.dll`、`model_loader.dll`、Assimp 和複製的執行階段 DLL。
- `settings\` 包含應用程式產生的 `settings.ini`、`history.ini` 和 `imgui.ini`。
- 建置及釋出清理指令碼發現輸出根目錄中有舊 INI 或 DLL 時立即中止，提示使用者手動處理。

建置目錄、複製的 `third_party` 原始碼樹、產生的設定/CSV/測試輸出及臨時路線、地圖和模型測試素材應保持在 Git 追蹤範圍之外。

## 原始碼導覽

| 區域 | 主要檔案與職責 |
| --- | --- |
| 地圖公開 ABI | `include/maploader.h`、`include/maploader_snapshot.h`：API v13 / 地圖快照 v9 函式、定寬 POD 快照、Scenario v2 快照與 v2 直寫草稿、編輯批次、報告、跨度、所有權、版本與結構尺寸 |
| 地圖生命週期 | `src/maploader/maploader.cpp`：C ABI 入口、控制代碼、重建、分派、原始碼讀取與邊界錯誤處理 |
| 地圖狀態 | `maploader_internal.h`：`MapContext`、剖析列、原始碼範圍、Include 堆疊、編輯引用、報告與計時 |
| 剖析 | `maploader_core.cpp`、`maploader_parser.cpp`、`text_decoder.cpp/.h`：陳述式、值、Include、變數、編碼、原始碼錨點、唯一性檢查 |
| 場景剖析與直接儲存 | `scenario_route.cpp/.h`：BVE 檔案類型探測、完整官方 Scenario 快照剖析、原始碼安全的直接儲存與 `Route` 候選剖析，支撐「從場景檔案開啟地圖」 |
| 幾何 | `maploader_geometry.cpp`：自/其他軌道幾何、重定位、曲線、坡度、放置緩衝區與場景控制點 |
| 識別資訊與快照 | `maploader_identity.cpp`、`maploader_snapshot.cpp`、`maploader_semantic.cpp`：穩定 ID、強型別快照、修訂、比較與指紋 |
| 編輯 | `maploader_edits.cpp`：試跑、記憶體套用、直接套用、提交、重設、原始碼修補、編碼感知寫回與距離調整；Include 陳述式支援受限的路徑參數更新，經新舊子樹遮罩的完整重新剖析驗證 |
| 共用關聯 | `include/repeater_linkage.h`、`include/own_track_transition_linkage.h`：Repeater 鏈與曲線/坡度過渡配對 |
| 共用路線值取樣 | `include/route_value_sampling.h`、`src/main_window/route_value_sampling.cpp`：穩定的求值事件序列、BeginTransition/Interpolate 區間分類、省略值繼承與 2D/3D 共用端點里程 |
| 共用數值安全與編輯計時 | `include/numeric_safety.h`：GUI/場景 double 轉整數時檢查數值是否有限且在範圍內；`include/operation_timing.h`：GUI 與 maploader 分域的可選穩態時鐘包含式計時 |
| 模型讀取 | `src/model_loader/model_loader.cpp`、`include/model_loader.h`：Assimp 隔離與 model-loader API v2 |
| 主視窗 | `gui_kme.cpp`、`kme.h` 及職責明確的 `src/main_window/` 模組：App 狀態協調、Win32/D3D11 啟動、通用工具/背景圖、快照資料填入/載入、編輯/距離/Inspector/清單草稿/新增檔案/新增元素工作流程、對話方塊/UI、場景預覽與無頭入口。新增檔案算繪位於 `new_element_wizard.cpp`，本文/對話方塊輔助位於 `app_dialogs.cpp`，延遲建立位於 `element_inspector_data.cpp`，工作流程契約位於 `headless_entrypoints.cpp` |
| 執行階段/設定 | `app_settings.cpp/.h`、`runtime_paths.cpp/.h`、`maploader_runtime.cpp`：INI、相對執行檔路徑、DLL 載入、精確 API 檢查 |
| 原始碼工具 | `file_structure_diagram.cpp`、`text_preview.cpp`：Include 圖、工作副本預覽、原始碼操作（更換 Include 檔案、解除引用）與距離邊界選擇 |
| Debug 驗證 | `debug_headless.cpp/.h`、`headless_entrypoints.cpp`、`edit_benchmark.cpp`、`src/table/datatable_benchmark.cpp`、`touch_input.cpp/.h`：參數剖析、無介面契約、正式工作流程、編輯與表格快取基準、攝影機傳遞、查詢、觸控、編輯與檔案建立檢查 |
| 二維檢視 | `src/canvas2d/canvas2D.cpp`：平面/profile 資料、快取化的 `Curve.Interpolate` 端點標記資料填入與平面繪製編排；`canvas2d_view_state.cpp/.h`：平移/縮放/旋轉和座標轉換；`canvas2d_marker_cache.cpp/.h`：軌道取樣與 marker/Repeater 疊加快取；`canvas2d_interaction.cpp/.h`：測量/marker 命中、上下文目標/動作和原始碼對應；`canvas2d_background.cpp/.h`：圖片座標、繪製和兩點對齊；`canvas2d_primitives.cpp/.h`：螢幕變換、裁剪折線、網格、比例尺和標記繪製；`profile_plots.cpp`：縱斷面與半徑圖表 |
| 三維檢視 | `include/canvas3D.h` 與 `src/canvas3d/canvas3D.cpp`：公開預覽介面和薄委託；私有 `canvas3d_impl.h` 狀態及按職責劃分的 `canvas3d_*.cpp` 實作，詳見下文三維模組表；`scene_track_sampling.cpp/.h` 與 `scene_route_overlay.cpp/.h`：CPU 取樣和純路線資訊格式化 |
| 表格/導覽 | 按功能劃分的 `src/table/datatable*.cpp`、`datatable_internal.h` 與 `table_navigation.cpp`：共用儲存格/欄、單一快取資料填入、查詢、資源清單列內編輯、路線/效果/Scenario 視窗、Debug 基準及列/平面/場景導覽 |
| 共用標記 | `include/map_marker_visuals.h`、`map_marker_visuals.cpp`：二維/三維標記的唯一視覺配方 |
| 在地化 | `include/multilanguage.h`：簡體中文、英文和日文介面文字 |

原始碼所有權、關聯、標記、導覽、剖析、驗證和寫回應沿用各自的元件邊界與共用實作。

## 詳細程式碼解說

本節按檔案介紹 `include/` 與 `src/` 中的主要職責。GUI 透過 C ABI 呼叫兩個 DLL；剖析器持有原始碼識別資訊，快照提供跨 ABI 資料檢視，編輯器產生、驗證並提交原始碼修補。

### 公開 ABI、共用演算法與資源標頭檔

#### `include/maploader.h`

- **匯出與記錄介面**：`KV_API` 控制 DLL 匯出/匯入；`KvLogCallback` 與 `kv_set_log_callback()` 把 DLL 內的英文診斷交給宿主。`kv_api_version()` 傳回 ABI 版本，EXE 必須精確符合。
- **場景檔案介面**：`kv_probe_file_kind()` 區分地圖與 Scenario；`kv_resolve_scenario_routes()` 傳回目標檔案存在的 Route 候選，配對 `kv_free_scenario_candidates()` 釋放；`kv_load_scenario_snapshot()` 傳回獨立 v2 快照，配對 `kv_free_scenario_snapshot()` 釋放；v2 `kv_save_scenario_document()` 驗證來源雜湊、完整重新剖析後按原編碼交易寫回，支援調整已有 Route/Vehicle 候選的數量和順序。
- **控制代碼與幾何產生**：`kv_load_map_ex()` 透過 `KV_LOAD_PREVIEW`、`KV_LOAD_EDIT_METADATA` 選擇預覽或編輯後設資料。`kv_generate_geometry()` 和 `kv_generate_scene_geometry()` 分別產生常規與場景軌道資料，更新控制代碼快取及各自修訂號。
- **唯讀快照**：`kv_get_map_snapshot()`、`kv_get_scene_geometry_snapshot()` 驗證版本與結構尺寸後傳回控制代碼擁有的檢視；呼叫端在重剖析或對應幾何失效前複製所需資料，巢狀儲存隨控制代碼管理。
- **編輯與原始碼存取**：`kv_get_edit_target_typed()` 取得一個穩定 edit id 的欄位和原始碼資訊，`kv_get_source_text()` 傳回目前磁碟或記憶體覆蓋層中的解碼文字。`kv_edit_dry_run_typed()`、`kv_edit_apply_to_memory_typed()`、`kv_edit_apply_typed()`、`kv_edit_commit_typed()`、`kv_edit_reset_memory()` 分別承擔驗證、套用到工作副本、直接寫入磁碟、提交工作副本和撤銷覆蓋層。
- **錯誤與釋放**：`kv_get_last_error()` 傳回執行緒區域錯誤文字；`kv_free()` 釋放地圖控制代碼，`kv_free_string()` 釋放 DLL 配置的獨立字串。

#### `include/maploader_snapshot.h`

- **版本與通用檢視區塊**：檔案開頭定義 API/快照版本、`KV_INDEX_NONE`、能力位元和編輯旗標。`KvUtf8View` 是呼叫期間 UTF-8 輸入，`KvStringRef`、`KvSpan` 和 `KvDoubleBuffer` 是指向快照 arena/陣列的定寬檢視。
- **Scenario 快照區塊**：`KvScenarioPathWeightRow` 儲存相對路徑、權重及顯式權重標記；v2 `KvScenarioSnapshot` 儲存來源雜湊與八欄位存在位元；`KvScenarioEditDocument`/`KvScenarioEditPathRow` 是 `kv_save_scenario_document()` 的呼叫期間輸入。
- **值與原始碼識別資訊區塊**：`KvValueKind`、`KvValue` 表示 null、數值、字串和 continue；`KvSourceFileRow`、`KvSourceSpanRow`、`KvStatementRow`、`KvElementRow` 與 `KvRowMetadata` 儲存實體檔案、Include 堆疊、位元組及行／欄範圍、原始參數、剖析順序和穩定 edit id。
- **強型別資料列區塊**：`KvTrack`、`KvStation*`、`KvStructure*`、`KvRepeater*`、`KvSignal*`、`KvSection*`、`KvSound*`、`KvOtherTrain*` 及曲線、坡度、限速和環境效果等 POD 固定各類欄位形狀；可變參數透過 `KvSpan` 引用共用值陣列。
- **根快照**：`KvMapSnapshot` 彙總字串 arena、通用值、各類資料列陣列、幾何矩陣、原始碼登錄表、能力位元及 content/geometry revision。`KvSceneGeometrySnapshot` 承載場景控制點與軌道矩陣，具有獨立的失效週期。
- **編輯協定區塊**：`KvEditField`、`KvEditTargetSnapshot` 描述可編輯欄位；`KvEditOperation`、`KvEditChange`、`KvEditBatch` 表示插入/更新/刪除請求；距離消歧、修補預覽、已提交檔案/資料列和 `KvEditReportSnapshot` 描述驗證及提交結果。請求與快照按公開標頭檔約定檢查版本和結構尺寸，欄位變更須同步 EXE/DLL。

#### `include/model_loader.h`

- `MlVertex` 儲存位置、法線與 UV；`MlMaterial` 儲存漫反射色和貼圖路徑；`MlMeshPart` 將索引範圍繫結到材質；`MlMeshData` 彙總頂點、索引、材質、分段和邊界框/中心/半徑。目前 `ml_api_version()` 傳回 v2，EXE 會做精確版本檢查。
- `ml_api_version()`、`ml_load_model()`、`ml_free_model()`、`ml_get_last_error()` 構成完整 C ABI。所有陣列由 DLL 配置，成功或部分失敗後的唯一釋放路徑都是 `ml_free_model()`。

#### `include/repeater_linkage.h`

- `EventKind` 將 `Begin`/`Begin0` 歸為 Begin，並區分 End 和其他事件；`BoundaryKind` 區分顯式 End、後續 Begin 和未閉合邊界；`Event` 保留距離、全域剖析順序、repeater key 和來源列索引。
- `canonical_key()` 統一 key 的大小寫比較。`pair_linkage()` 按距離、全域剖析順序、來源列索引穩定排序，按 repeater key 維護活動鏈並產生 `Chain` 和 `Segment`；遇到新 Begin 時封閉舊段，遇到 End 時結束鏈。同里程最後剖析的事件決定活動狀態。
- `pair_segments()` 提供扁平段清單，供地圖快照、表格、二維和三維共用。

#### `include/own_track_transition_linkage.h`

- `EventKind` 表示曲線/坡度的 `BeginTransition` 及其可能的 Begin/End 使用端，`Pair` 儲存過渡列與使用端列的索引。
- `consumes_curve_transition()`、`consumes_gradient_transition()` 定義哪些陳述式可取用待配對過渡。`pair_transitions()` 按原始碼順序維護曲線和坡度兩套待處理狀態，輸出配對及 orphan 清單；編輯和標記路徑據此把過渡操作繫結到取用陳述式。

#### `include/map_marker_visuals.h`

- `MapMarkerVisualKind` 是二維/三維共同的元素視覺列舉，`map_marker_visual_bit()` 將其對應為可見性位元。
- `MapMarkerPrimitiveKind`、`MapMarkerColorRole`、`MapMarkerIconVariant` 定義圖示圖元、主題色角色和變體；`MapMarkerIconPrimitive`、`MapMarkerIconRecipe` 儲存正規化點、線寬、閉合/填充和 glyph 資訊。
- `map_marker_theme_color()`、`map_marker_role_color()`、`map_marker_icon_recipe()` 與 `draw_map_marker_icon()` 統一 2D/3D 的顏色、圖示配方和 ImDrawList 繪製介面。

#### `include/route_value_sampling.h` 與 `src/main_window/route_value_sampling.cpp`

- `Event` 保留 maploader 已求值、按穩定里程順序輸出的路線值及事件型別；`append_event()` 統一識別普通值、`BeginTransition` 和 `Interpolate`，無參數 Interpolate 沿用前值。
- `sample()` 供 2D/3D 共用：常數區間傳回目前值，過渡或內插區間傳回起止里程與兩端值。

#### `include/numeric_safety.h` 與 `include/operation_timing.h`

- `kme::truncating_int_or_zero()` 只對有限且位於 `int` 範圍內的 double 做截斷轉換；非有限值或越界值統一傳回 `0`，GUI 與場景程式碼共用這一邊界。
- `kme::timing::Timing` 提供執行緒區域、顯式啟用的包含式階段計時。GUI 與 maploader 分域記錄，按階段累計毫秒數和次數，供主控台診斷使用。

#### `include/canvas3D.h`

- **場景輸入模型**：`Canvas3DTrackPoint/Path/Visibility` 描述軌道取樣與顯示；`Canvas3DSceneObject`、`Canvas3DModelInstance`、`Canvas3DRepeaterSegment`、背景/霧/繪製距離事件構成場景實體輸入。
- **路線資訊與標記**：`route_value_sampling::Event`、站點、限速、Section 號誌事件用於攝影機里程取樣；`Canvas3DSceneMarker` 儲存視覺 kind、里程、軌道位置、表格目標和 edit id，`Canvas3DSceneMarkerVisibility` 以分類位元控制索引重建。
- **建置與重新整理結構**：`Canvas3DScene` 是獨立於算繪器的 CPU 場景描述；`Canvas3DSceneBuildOptions/Result`、`Canvas3DSceneMapRefreshOptions` 區分首次建置、動態內容重新整理和地圖內容重新整理；`Canvas3DSceneStats` 提供執行個體、模型和影格率統計。
- **互動結構**：攝影機姿態、上下文動作、拾取目標、`Canvas3DPlacementEditTarget`、拖曳軸與 `Canvas3DPlacementDragUpdate` 將算繪互動轉換為 GUI 可套用的原始碼欄位更新。
- **`Canvas3D` 門面類別**：提供單模型與場景的載入、重新整理、可見性、視距、霧、操縱器、攝影機、效能警告及除錯介面，具體實作委託給私有 PImpl。

#### `include/multilanguage.h`

- `Language` 指定日、英、簡中，`Translation` 的三個欄位儲存同一 UI 文字。檔案主體按視窗、選單、工具列、表格、屬性編輯、錯誤提示和 2D/3D 操作分組宣告翻譯常數。
- `tr()`/語言選擇輔助程式碼在執行階段傳回目前語言欄位。增加使用者可見字串時必須在同一個 `Translation` 初始化器中同時填寫三種語言，並保持格式預留位置一致。

#### `include/resource.h`

- Windows 資源編譯器與 C++ 共用資源編號；`IDI_KOMAPEDIT` 對應 `komapedit.rc` 中的應用程式圖示。

### maploader 內部狀態、剖析與快照

#### `src/maploader/maploader_internal.h`

- **基礎工具與計時**：字串輔助宣告、`SteadyClock`、`LoadTiming`、`ScopedTimer`、`ActiveTimingScope` 記錄讀取、剖析、合併、幾何和快照階段；並行任務槽宣告限制昂貴載入任務同時執行。
- **解碼與剖析選項**：`LoadedText` 同時儲存原始位元組、UTF-8 本文、編碼、BOM、換行和行起點；`MapParseOptions` 決定預覽/編輯後設資料層級；`SourceTextOverride(s)` 是記憶體工作副本覆蓋層。
- **運算式值**：`ValueKind`、`Value`、`VariableEnvironment` 表示剖析期 null/number/string 和變數繫結；環境快照以共用唯讀對應掛到陳述式，供距離移動驗證語意環境。
- **原始碼模型**：`SourceFileRecord`、`FileStructureRecord`、`SourceSpan`、`ParsedStatement`、`EditSourceRef`、`MapDiagnostic` 儲存實體檔案、Include 呼叫識別資訊、原始陳述式、行／欄與位元組錨點、剖析順序和編輯識別資訊。
- **剖析列記錄**：`CurveEditRow`、`GradientEditRow`、`OtherTrackChange` 以及 Station、Structure、Repeater、Signal、Section、Sound、Train、限速和環境效果記錄，為幾何、快照和編輯器提供強型別資料；各列的 `EditSourceRef` 用於原始碼回寫。
- **矩陣與快照儲存**：`Matrix` 管理列／欄連續 double 緩衝；`MapSnapshotStorage`、`SceneGeometrySnapshotStorage`、`EditTargetSnapshotStorage`、`EditReportSnapshotStorage` 擁有 ABI 指標背後的 vector/string arena。
- **`MapContext` 聚合根**：持有主路徑、原始碼/Include 表、變數環境、所有剖析列、軌道與場景矩陣、控制點、revision、快照快取、工作副本覆蓋、磁碟基線 hash、最近編輯報告和計時。剖析、幾何、快照與編輯都以它作為唯一所有權根。
- **活動陳述式與編輯 RAII**：`ActiveStatementScope` 在 dispatch 期間設定目前陳述式/原始碼環境並自動恢復；`MapEditChange` 及各專用欄位結構承載複製後的 ABI 請求；語意快照、距離消歧、修補/提交報告結構支援交易驗證。
- **跨實作檔案宣告區**：檔案尾宣告 DLL 內部的 parse、geometry、snapshot、semantic、edit 和 identity 模組入口。

#### `src/maploader/text_decoder.h`

- 宣告 UTF-8 與 `std::filesystem::path` 的雙向轉換、二進位讀取、UTF-16/字碼頁解碼、BOM/首行檢測和編碼感知寫回。
- `FileOpenFailureKind` 區分不存在、權限、目錄和一般開啟失敗，使剖析器與模型載入器可產生一致診斷。寫回函式接收目標編碼與 BOM 資訊，並在字元不可表示時失敗。

#### `src/maploader/text_decoder.cpp`

- `classify_file_open_failure()`、`file_open_failure_message()` 和 `read_binary_file()` 負責可靠讀取及 Windows 錯誤分類；`path_to_utf8()`、`utf8_to_wide()`、`wide_to_utf8()`、`path_from_utf8()`、`join_utf8_path()` 隔離 Win32 寬字元路徑細節。
- `decode_codepage()` 在 Windows 使用嚴格/寬鬆字碼頁轉換；非 Windows 分支給出受限備援。`append_utf8_codepoint()` 與 `decode_utf16()` 手動處理端序、代理字元組和無效序列。
- `decode_text_bytes()` 按宣告編碼/BOM 選擇 UTF-8、UTF-16 或 CP932 路徑；`first_line_ascii()` 和 `has_utf8_bom()` 支援在完整解碼前識別地圖頭。
- `append_utf16_bytes()` 與 `encode_text_for_writeback()` 將 UTF-8 工作副本轉回原編碼並保留 BOM；字元無法表示時擲出錯誤。

#### `src/maploader/maploader_core.cpp`

- **任務、時間和純量輔助函式**：`try_acquire_maploader_task_slot()`/`release_maploader_task_slot()` 控制載入並行；`ActiveTimingScope` 記錄總活動時間；`ascii_lower()`、trim、`parse_finite_number()`、`canonical_number()`、版本/編碼頭剖析提供統一純量規則。
- **文字載入**：`build_line_starts()`、`detect_newline()`、`make_loaded_header_text()`、兩個 `load_header_text()` 多載把原始位元組變成帶編碼、換行和本文位置資訊的 `LoadedText`，並優先讀取記憶體覆蓋。
- **Value/key 轉換**：`as_number()`、`as_text()`、`key_text()`、`track_key_display_text()`、`track_key_from_display_text()` 和 CSV/INI 欄位輔助函式統一剖析器、表格語意和編輯文字表示。
- **原始碼註冊與定位**：`normalized_source_path/key()`、`current_source_text()`、`register_source_file_index()`、Include 堆疊和 invocation key intern 函式去除重複原始碼識別資訊；`line_column_for_body_pos()`、`make_source_span()` 把本文偏移轉換為穩定實體錨點。
- **陳述式與環境登記**：`current_variable_environment_snapshot()`、`rebuild_variable_environment_snapshot()`、`add_parsed_statement()`、`next_active_edit_ref()` 建立陳述式、環境和 edit ref；merge/offset 函式把 Include 子上下文合入父上下文而保持索引正確。
- **清單列原始碼區塊**：`add_loaded_line_statement()`、`extend_loaded_line_statement()` 為 CSV/list 實體列建立可編輯陳述式；`parse_signal_aspect_source_values()` 和欄位名輔助函式保留可變佈景 key 欄及 glare 列形狀。
- **相依項與狀態更新**：`value_equal()`、變數讀寫記錄、`log_load_timing()` 支援語意驗證和效能記錄；`add_controlpoint()`、`set_distance()`、`put_own()`、`ensure_othertrack()`、`put_other()` 是 parser dispatch 寫入軌道事件狀態的統一入口。

#### `src/maploader/maploader_parser.cpp`

- **詞法/陳述式迴圈**：`Parser::parse()` 驅動整檔案；`eof()`、`peek()`、`skip()`、`accept()`、`expect()` 處理空白、註解和標點；診斷函式記錄位置並在 `finish_statement()`/`synchronize_statement()` 中恢復到下一條陳述式。
- **物件、函式與運算式**：`parse_label()`、`parse_variable_name()`、`parse_map_object()`、`parse_map_function()`、`parse_map_args()` 建構 `MapObject`/`MapFunction`；`parse_expression()`、`parse_prefix()`、`parse_primary()`、`apply_binary()`、`call_function()` 實作優先順序、變數、字串、數值和受支援數學函式。
- **Include 流程**：`include_path_is_simple_string()` 檢查預覽路徑；`make_child_seed()` 繼承普通變數和 Include 識別資訊，並以零里程、空距離運算式建立子上下文。`parse_include_context()` 支援平行剖析；`queue_include()`、`flush_pending_includes()` 按原順序合併原始碼、診斷、事件和變數寫入，保留父檔案里程，並在變數相依項過期時重剖析。隨機引擎由處理程序工作階段種子、原始碼路徑和 Include 詞法順序派生，重剖析沿用排隊時的種子，使 Preview/Edit 載入及工作副本重剖析能夠重現未修改的 `rand()` 呼叫。
- **語法驗證與總分派**：`method_rules()` 是方法參數個數/空值規則表；`object_path()`、`validate_statement()` 形成一般語法門；`dispatch()` 再按頂層物件路由到專用函式。`record_deferred_semantics()` 記錄需要等資源清單全部讀完後才可驗證的 key。
- **自軌道與其他軌道**：`dispatch_curve()`、`dispatch_gradient()`、`dispatch_legacy()` 記錄曲線、坡度和舊式事件；`dispatch_track()`、`setposition_interpolate()`、`track_position()` 記錄其他軌道位置、內插、軌距、中心和超高事件。
- **資源清單**：`load_resource_list()` 與 `record_resource_list_load()` 儲存 Load 的原運算式、求值路徑和原始碼識別資訊；`parse_station_list()`、`parse_structure_list()`、`parse_signal_aspect_list()`、`parse_sound_list()` 把實體清單列轉成強型別且可回寫的記錄；`parse_other_train_file()` 讀取其他列車檔案。
- **地圖元素分派**：`dispatch_station/speedlimit/section/signal/beacon/pretrain/structure/sound/train/repeater/irregularity/background/adhesion/cab_illuminance/fog/draw_distance()` 及三個 noise 分派函式，檢查方法形狀、讀取參數並追加對應列；`add_other_train_definition()` 統一 Train 定義登記。
- **剖析後診斷**：`validate_unique_preview_statements()` 檢查重複 Load/Enable；`append_transition_diagnostics()` 檢查過渡配對；`append_deferred_key_diagnostics()` 檢查資源 key。車站使用到達/出發音效時，`append_station_sound_load_order_diagnostic()` 檢查 `Sound.Load` 是否早於 `Station.Load`，異常順序作為英文 `[WARN]` 輸出：同檔案比較行／欄位置，跨檔案比較 Include 深度，同深度再比較全域剖析順序。`emit_diagnostics()` 輸出診斷，並將錯誤升級為載入失敗。
- **模組入口**：`parse_map_context()` 建立 `MapContext`、載入主檔案、執行 Parser、重新整理環境/診斷並傳回完整上下文，是所有載入和編輯後重新剖析的共同入口。

#### `src/maploader/maploader_geometry.cpp`

- **軌道狀態機**：`LastPos` 儲存上一取樣點，`TrackPointer` 按距離推進 own-track 事件並給出目前 radius、gradient、cant、方向與座標；它是常規取樣和事件邊界取樣的基礎。
- **曲線數學**：`rotate_xy()`、Gauss 積分、Fresnel 級數/漸近式實作區域座標積分；`circular_curve*()` 計算圓曲線，`halfsin_intermediate()`、`linear_transition_curve_local()`、`transition_curve*()` 計算半正弦/線性緩和曲線。key/hash 結構快取重複參數的曲線結果。
- **坡度投影**：`constant_gradient_projection()`、`sinc()`、`gradient_transition()` 計算路線長度對應的平面投影和高程；`build_gradient_projection_samples()`、`build_event_projected_distances()` 將事件里程對應到平面位置。
- **自軌道產生**：`sorted_unique()`、`append_arange()` 匯合事件點和等間距點；`generate_owntrack()` 逐取樣寫出距離、XYZ、朝向、radius、gradient、cant 等欄；`generate_curveradius()` 形成曲率圖資料。
- **其他軌道產生**：`relative_position()` 計算曲線上的相對偏移；`CantProcessor` 處理超高 Begin/End/Interpolate；`build_othertrack_buffer()` 合併位置、X/Y 內插、軌距、中心和 cant，輸出與 own-track 對齊的矩陣及有效性。
- **重定位與放置**：`relocate()` 統一平移幾何到穩定區域座標；`build_structure_put_buffer()` 為佈景放置提供按里程取樣的變換基礎。
- **場景自適應控制點**：角度/矩陣輔助函式與 `build_scene_adaptive_controlpoints()` 把事件、模型跨度、曲率、坡度和請求範圍組合成密度自適應控制點。
- **入口**：`generate_geometry()` 依次產生 own track、曲率、other tracks、放置緩衝與場景控制點，更新耗時、能力位元和相應快照 revision。

#### `src/maploader/maploader_identity.cpp`

- `stable_hash64()` 實作確定性 64 位元雜湊，`hex64()` 輸出固定十六進位文字，`edit_kind_token()` 規範 row kind。
- `make_edit_id()` 把正規化原始碼 key、全域剖析順序、陳述式種類和區域序號組合成穩定 ID；`statement_edit_id()` 快取陳述式 ID；`native_element_edit_id()` 與 `element_edit_id()` 為原生列和必要的派生識別資訊提供統一入口。

#### `src/maploader/maploader_snapshot.cpp`

- `matrix_view()`、`data_or_null()` 把內部連續容器安全投影為 ABI 檢視。
- `MapSnapshotBuilder::build()` 依次建置根資料、軌道、車站、佈景、其他列車、區間/號誌/聲音、環境效果、作者訊息、預覽列和編輯登錄表，最後呼叫 `finalize()` 繫結快照。
- `string_ref()` 在共用 arena 中重用已有字串並追加新文字；`value()`/`append_values()`/`append_strings()` 產生值與 span；`metadata()` 將 `EditSourceRef` 轉為 `KvRowMetadata`。各 `add_*` 區塊複製型別化欄位，並按能力位元附加原始碼資訊。
- `add_element(s)()` 建立 row kind/edit id 到列索引的登錄表；`bind()` 在所有 vector 完成擴容後繫結裸指標；`finalize()` 寫入版本、結構尺寸、數量、revision 和 capability。
- `invalidate_map_snapshot()`、`invalidate_scene_geometry_snapshot()` 明確內容、常規幾何和場景幾何的失效邊界。`build_map_snapshot()`、`build_scene_geometry_snapshot()` 延遲重建快取；`ordered_station_list_entries()` 保持車站清單實體順序。

#### `src/maploader/maploader_semantic.cpp`

- `SemanticWriter` 按固定順序寫入型別和值並計算雜湊；`field()`、`value_span()`、`begin_element()`、`emit_element()` 產生正規化語意表示。
- `changed_field()`、數字/字串/value/track-key 讀取函式把某個 `MapEditChange` 疊加到快照原值上，並拒絕無效數字或缺少的必需值。
- `write_structure_model()`、`write_sound_list()`、`write_structure_put()`、`write_structure_between()`、`write_station_put/list()`、`write_signal_aspect/put()`、`write_repeater()` 以及 beacon、sound、noise、background、adhesion、fog 等 `write_*` 函式，逐類定義「編輯前後應相等/應改變」的語意欄位集合。
- `write_curve()`、`write_gradient()`、`write_other_track_change()` 保留方法、參數個數和配對資訊；`write_section_row()` 支援可變值清單；`reject_unknown_target_fields()` 拒絕未宣告欄位。
- `build_semantic_map_snapshot()` 遍歷所有受保護元素，產生 edit id 到語意的索引及整圖/環境指紋。`expected_target_semantic()` 計算更新/刪除目標的期望結果；`FakeInsertSnapshotState`、`insert_semantic_container()`、`expected_insert_semantic()` 為新增陳述式建構同樣可驗證的期望語意。

#### `src/maploader/maploader_edits.cpp`

- **ABI 輸入複製**：`copy_utf8_view()` 和 `copy_edit_batch()` 驗證結構尺寸、指標/長度、operation、flags、欄位重複與 UTF-8 view 生命週期，把呼叫期間 POD 複製為內部 `MapEditChange`。
- **修補定位**：`load_source_patch()` 讀取工作副本；`source_range_in_text()`、`safe_statement_removal_range()` 和預覽函式將 `SourceSpan` 轉為 UTF-8 文字範圍，並保留相鄰註解與陳述式。
- **距離運算式調整**：運算式掃描函式識別 predefined `distance`、頂層加減號和安全數值加數；`find_safe_numeric_distance_addend()`、`apply_delta_to_distance_addend()`、`adjust_distance_expression_by_delta()` 優先保留變數運算式，只在安全時修改常數項，否則產生距離消歧建議。
- **參數與 CSV 建構**：BVE 參數分割/引用、數值/optional/value/key 輔助函式保留未改 raw arg；CSV 剖析、等價比較和 `build_editable_csv_list_statement()` 保持分隔、尾部欄位與編碼可寫性。
- **逐類陳述式產生器**：`build_structure_model/sound_list/station_list/signal_aspect_statement()` 負責清單列；`build_station_put/structure_put/signal_put/repeater_statement()` 處理顯式方法/參數形狀轉換，其中 Structure 與 Repeater 支援普通形式和零偏移形式雙向轉換；其餘 `build_*` 覆蓋曲線、坡度、其他軌道、Section、限速、應答器、聲音/噪聲和環境效果，維持原方法與參數形狀。
- **目標發現與編輯目標快照**：範本化 `match_edit_ref()`、`find_simple_target()` 和 `find_editable_target()` 在 MapContext 強型別列中定位 edit id；`build_edit_target_snapshot()` 輸出欄位、原值、raw arg、約束、sourceHash 和 expectedSourceHash。
- **插入驗證**：`validate_insert_field_names()`、`validate_insert_method()`、`validate_insert_change()` 驗證 row kind、方法和結構化欄位；`build_insert_statement()` 據此產生普通 BVE 陳述式。
- **距離區塊規劃**：`DistanceSectionAnalysis/PlanningIndex` 按實體檔案、Include 呼叫執行個體和距離段建立索引，規劃初始區塊、普通間隙、最後區塊及 EOF。明確末段內的移動可沿該段方向擴充；新增優先使用已有唯一位置，歧義位置交由使用者選擇。候選篩選共用變數和實體執行個體檢查，選定方案在套用或寫入磁碟前執行完整語意驗證。
- **報告與交易寫入磁碟**：`build_edit_report_snapshot()` 投影修補、消歧和提交資訊；hash/臨時檔案函式建立同目錄暫存檔案；`replace_files_transactionally()` 按階段替換並在失敗時還原，`TransactionalWriteError` 保留主錯誤與還原錯誤。
- **完整語意驗證**：`parse_report_candidate()` 用修補覆蓋重剖析；`validate_non_target_derived_state()`、`own_track_transition_state()`、`validate_edit_report()` 比較非目標元素、最終變數繫結、車站所有權、過渡配對和每個目標的期望語意；有效編輯可改變最終目前 `distance`。
- **批次主流程**：`build_edit_report()` 預處理目標、按實體上下文和目標距離分組，解決 boundary，產生替換/刪除/插入，檢測重疊修補，重剖析並驗證。它是 dry-run、記憶體 Apply 和直接 Apply 的共同核心。
- **工作副本與提交**：`apply_patched_files_to_overrides()`、`reparse_context_with_overrides()`、`apply_edit_report_to_memory()` 更新記憶體覆蓋且保留磁碟基線；`reset_memory_edits()` 回到磁碟；`populate_committed_edit_state()` 記錄寫入磁碟後的識別資訊；`commit_memory_edits()` 重新檢查並交易寫入所有覆蓋檔案。

#### `src/maploader/maploader.cpp`

- `parse_options_from_load_flags()` 把公開 flags 轉為內部 profile，並拒絕未知組合。
- `kv_*` 匯出函式驗證控制代碼、版本、結構尺寸和參數後呼叫內部實作；在 C ABI 邊界捕捉例外，寫入 last error 並傳回失敗值。
- `kv_load_map_ex()` 建立 `MapContext`；兩個 geometry 入口更新矩陣和修訂；兩個 snapshot 入口傳回快取檢視；edit target/source text 入口傳回工作副本資訊。
- Scenario 相關匯出呼叫 `probe_bve_file_kind()`、`load_scenario_document()` 或 `resolve_scenario_route_candidates()`，把獨立配置的快照/候選區塊及對應的釋放函式保持在同一 DLL 所有權邊界內。
- dry-run 只建置報告，memory apply 建置並套用覆蓋，reset 丟棄覆蓋，direct apply 對報告直接交易寫入磁碟，commit 儲存已驗證工作副本。`kv_free()`/`kv_free_string()` 與 DLL 配置所有權成對。

#### `src/maploader/scenario_route.h` 與 `src/maploader/scenario_route.cpp`

- `probe_bve_file_kind()` 唯讀取檔案開頭，用於區分 Map、Scenario 和未知檔案；不可讀檔案保留為 Unknown，交由正常載入流程報告詳細錯誤。
- `load_scenario_document()` 按宣告編碼剖析 `BveTs Scenario 2.00`，處理 `#`/`;` 註解和重複欄位的末項優先規則，保留八個官方欄位、來源雜湊/存在位元以及 Route/Vehicle 的原始碼相對路徑、權重與顯式權重標記。
- `save_scenario_document()` 重新讀取磁碟並比較 expected source hash；候選數量不變時對最後生效欄位逐項做最小修補，數量變化時僅重寫該欄位的完整候選值；拒絕空路徑、保留語法字元和非正/非有限權重，要求每個已存在欄位至少一個候選，完整重新剖析後呼叫共用交易寫回。
- `resolve_scenario_route_candidates()` 重用 Scenario 剖析結果，按其所在目錄剖析 Route 相對路徑並驗證目標檔案存在；快照讀取負責保留宣告資料。

#### `src/maploader/diagnostics.h` 與 `src/maploader/diagnostics.cpp`

- 標頭檔宣告記錄回呼、last-error 和 info/warn/error 介面。回呼透過原子讀寫釋出，各呼叫執行緒以 `thread_local` 儲存最後錯誤。
- `emit_log()` 轉發完整記錄行；`log_info_at()`、`log_warn_at()`、`log_error_at()` 新增級別與原始檔名。ABI 捕捉區塊透過 `set_last_error()` 儲存可查詢的錯誤。

#### `src/maploader/c_api.h` 與 `src/maploader/c_api.cpp`

- 這是 DLL 內部的 C 字串所有權輔助層。`copy_c_string()` 使用 `std::malloc()` 複製帶 NUL 的 UTF-8 文字，配置結果只交給公開 ABI，最終由 `kv_free_string()` 的對應的 `std::free()` 路徑釋放。

### 模型載入

#### `src/model_loader/model_loader.cpp`

- 路徑/擴充輔助函式正規化 Assimp 格式提示，`copy_c_string()` 為材質貼圖路徑配置 ABI 字串，`resolved_texture_path()` 將模型相對貼圖剖析為 UTF-8 路徑。
- `free_mesh()` 冪等釋放頂點、索引、材質字串和 mesh parts；`MeshCleanupGuard` 負責例外清理；`assign_bounds()` 計算邊界框、中心與半徑。
- `load_with_assimp()` 先用共用二進位讀取支援 Unicode 路徑，再呼叫 Assimp importer；隨後合併各 aiMesh 頂點/法線/UV/索引，建立材質和分段，剖析第一張漫反射貼圖並計算邊界框。
- `ml_api_version()` 傳回 v2；`ml_load_model()` 清零輸出、捕捉例外並填充 `MlMeshData`；`ml_free_model()` 是公開釋放入口；`ml_get_last_error()` 傳回執行緒區域診斷。

### 主視窗狀態、載入與編輯工作流程

#### `src/main_window/kme.h`

- **通用數學與雜湊**：`KmeByteHash64` 為 GUI 快取/除錯提供確定性位元組雜湊；`Matrix` 是從 ABI 複製後的 GUI 自有二維 double 緩衝。
- **地圖模型**：`TrackEvent`、`OwnTrackEditMarker`、`OtherTrack`、Station/SpeedLimit/Section 和各類 `TableRow` 組成 `MapModel`；它複製並持有 GUI 所需的原始碼登錄表、資源清單後設資料、revision、能力位元和常規/場景矩陣。
- **二維資料**：`View2D` 儲存平移、比例和旋轉；`TrackPoint`、`PlanMarker` 及各別名、`PlanRepeaterSegment`、`OtherTrainPathOverlay`、`PlanData`、`ProfileData` 是畫布快取和 hit-test 輸入。
- **表格與原始碼工具狀態**：`TableRow/ColumnDef`、`CachedTableRow`、`TableUiCache` 儲存按 revision 建置的顯示資料；File Structure、Text Preview、DistanceResolution 結構儲存配置、選擇和剖析器確認的邊界。
- **設定與執行狀態**：`TextureImage` 管理 D3D 背景貼圖；記錄、視窗可見性、2D/3D 檢視、`UserSettings`、最近地圖和背景歷史結構對應 INI 持久化欄位；`ScenarioRoutePickState` 儲存候選選擇/入口選項，`ScenarioPreview` 與其磁碟基線儲存可編輯 Scenario 草稿。
- **編輯狀態機**：欄位約束、inspector session、pending change、preview snapshot、Repeater draft、NewElement template/wizard、delete mode、distance workflow 和 editable-list draft 結構，明確區分尚未 Apply 的 UI 草稿、已 Apply 工作副本和已儲存磁碟狀態。
- **`App` 類別**：宣告視窗算繪、非同步載入、快照轉換、幾何/場景重建、所有表格和導覽、編輯/儲存/重新載入、對話方塊、設定、背景圖、2D/3D 互動及快取成員。其成員配置是各 GUI `.cpp` 檔案共用的應用程式狀態契約。

#### `src/main_window/maploader_runtime.cpp`

- `KME_MAPLOADER_FUNCTIONS` 巨集是唯一符號清單；`MaploaderRuntime` 建構時從 `runtime_paths::dll_path()` 載入 `maploader.dll`，剖析包括唯一載入入口 `kv_load_map_ex()` 在內的全部函式指標，並檢查 `kv_api_version()==KV_MAPLOADER_API_VERSION`。失敗資訊包含 Win32 錯誤文字。
- 全域 `kv_*` 函式透過 `ensure_loaded()` 呼叫快取的 DLL 函式指標；失敗時傳回錯誤值並提供 GUI 可查詢的診斷。GUI 使用公開標頭檔中的函式名，執行階段動態載入 DLL。

#### `src/main_window/runtime_paths.h` 與 `src/main_window/runtime_paths.cpp`

- `executable_directory()` 首次呼叫 `GetModuleFileNameW()` 並快取 exe 目錄；`dll_directory()` 固定為其 `bin` 子目錄；`settings_directory()` 建立並傳回 `settings` 子目錄。
- `dll_path()` 組合 DLL 路徑；`load_dll()` 以受限搜尋旗標從 `bin` 載入相依項，並可傳回 Win32 錯誤碼。

#### `src/main_window/app_settings.h` 與 `src/main_window/app_settings.cpp`

- 標頭檔提供預設 INI 路徑、內部顯式路徑設定載入入口、使用者設定/歷史讀寫、ImGui layout 延遲儲存和執行階段樣式套用函式。
- 實作前段正規化儲存路徑、最近地圖 key/顯示名，clamp 字型、控制項、marker、線寬、場景距離/操縱器/執行個體警告門檻值；顏色函式負責 hex 序列化、palette、透明度和混色。
- 語言、bool、2D mode 和 grid mode 的轉換函式定義 INI 值語法。`save_user_settings()` 寫入 `General`、`WindowVisibility`、`View2D`、`View3D` 的規範鍵；`load_user_settings()` 按精確節名、鍵名和值語法讀取，未知或無效項採用預設值。已有檔案的重寫由儲存操作負責。
- `load_imgui_layout()`、`save_imgui_layout()`、`save_imgui_layout_if_requested()` 和 pending flag 管理 `imgui.ini` 的顯式/延遲持久化。
- `load_history_state()` 讀取 `[Recent]`、`[MapN]` 的最近地圖與八個路徑/背景欄位，以及 `[CreatorMessages]`、`[CreatorMessageN]` 的作者訊息偏好。最近地圖經過路徑正規化、去除重複後最多保留十項；`save_history_entries()` 和 `save_history_state()` 寫回規範格式，數值使用固定區域設定。
- `apply_ui_font_size()`、`apply_ui_theme_color()`、`apply_ui_component_size()`、`apply_ui_settings()` 把持久設定投影到 ImGui style，並按 DPI/viewports 修正圓角和尺寸。

#### `src/main_window/gui_kme.cpp` 與拆分模組

- `gui_kme.cpp` 管理 `App` 建構、解構與記錄回呼；`kme.h` 宣告 EXE 內共用狀態和跨翻譯單元介面。
- `win32_dx11_bootstrap.cpp` 擁有 D3D11 裝置/算繪目標、`WndProc()`、視窗訊息迴圈和 `main()`。
- `gui_common_utils.cpp` 集中字型、主題/記錄顏色、編碼轉換、數值格式、路徑及里程跳轉控制項輔助函式；`background_image.cpp` 擁有 WIC 解碼、背景貼圖重建與 history 背景持久化。
- `map_snapshot_hydration.cpp` 從 typed snapshot 建置 `MapModel`、列後設資料、Station edit id、限速快取與過渡關聯；`map_load_pipeline.cpp` 處理 Map/Scenario 探測、Scenario 快照/草稿基線與 Route 候選、非同步地圖載入、入口歷史、結果套用、後設資料合併、載入計時和幾何重新產生。
- `edit_ledger.cpp` 處理 typed 批次/報告、帳本同步、本機預覽、刪除、儲存/撤銷/關閉；`distance_resolution_workflow.cpp` 處理距離消歧請求與繼續套用。
- `edit_benchmark.cpp` 承載獨立的 Debug 編輯效能入口：真實輸入只做記憶體 Apply/Delete/Revert 與位元組保護，Save 僅在保持相對相依項配置的排他臨時普通檔案副本中執行，同時檢查重新整理合併、還原和階段計時契約。
- `element_inspector_data.cpp` 管理 Inspector 開啟、定位、欄位/場景編輯資料與 Apply；`element_inspector_render.cpp` 只算繪 Inspector 欄位、可選插入參數和可變 Repeater/Section UI。
- `editable_list_drafts.cpp` 管理資源清單草稿；`new_element_wizard.cpp` 擁有新元素範本/精靈、結構化插入以及新增檔案精靈的狀態與算繪；`headless_entrypoints.cpp` 承載重用正式 App 工作流程的新元素、資源清單替換/插入、新增檔案精靈和 Scenario 建立契約。
- `app_dialogs.cpp` 擁有檔案對話方塊、Scenario Route 候選選擇、Scenario 新檔案本文建置和其他模態彈出視窗；`element_inspector_data.cpp` 處理新檔案請求的延遲、排他建立與建立後開啟；`ui_elements.cpp` 擁有 dockspace、選單、工具列、狀態列、主控台、快速鍵和設定投影；`scene_preview_lifecycle.cpp` 擁有場景/模型預覽的啟停、重建、可見性和視窗算繪。

#### `src/main_window/file_structure_diagram.cpp`

- 配置按 Include depth 分組，快取節點尺寸、連線、範圍和 revision；`file_structure_layout_is_current()` 檢查快取，`rebuild_file_structure_layout()` 在原始碼結構或字型/尺寸變化時重建。
- `open_parent_directory_in_explorer()` 透過 ShellExecute 開啟實體目錄。`render_source_file_context_menu()` 是結構圖和屬性檢查器共用的「開啟目錄/原始碼預覽」動作；Include 節點在編輯模式下還提供「更換檔案...」與「解除引用」兩個延遲請求入口。
- `App::render_file_structure_window()` 繪製可平移畫布、層級連線、主檔案/Include 節點、hover tooltip 和右鍵選單，並把選中節點交給工作副本 Text Preview。

#### `src/main_window/text_preview.cpp`

- `build_text_preview_lines()` 建立行起始位元組索引；`decode_preview_bytes()` 是非 parser-confirmed 檔案的唯讀解碼備援。正常 map/list 預覽優先走 `kv_get_source_text()`，因此可顯示記憶體 Apply 後的工作副本。
- boundary range/gap/EOF 函式把 `DistanceResolutionBoundary` 按行定位；marker style/render 函式在原始碼行間顯示剖析器確認的插入點，`utf8_byte_for_source_column()` 將原始碼欄位位置轉換到 ImGui 文字選擇位元組。
- `open_text_preview()`、`refresh_text_preview_from_working_copy()`、`refresh_text_preview_after_map_load()` 管理普通預覽；`open_text_preview_for_distance_resolution()` 注入候選邊界和目標陳述式定位。
- `render_text_preview_window()` 繪製行號、唯讀 UTF-8 文字、選擇、原始碼定位及邊界按鈕；使用者選擇報告提供的邊界 token 後，由主編輯狀態機重試。

#### `src/main_window/touch_input.h` 與 `src/main_window/touch_input.cpp`

- 標頭檔的 `TouchFrame` 彙總單影格 tap、long press、scroll 和 pinch（含 `PinchAxis`）；公開函式負責 Win32 訊息接入、每個影格狀態、區域取用、popup 手勢和測試注入。
- 實作中的 `ActiveTouch`、`PairState`、`TouchManager` 追蹤 pointer id、按下位置/時間、移動門檻值、雙指中心和縮放。訊息處理識別 down/update/up/capture lost，長按與滾動互斥，pinch 將距離變化對應為軸向縮放。
- `new_frame()` 釋出並清理瞬時事件，`consume_*` 標記手勢取用，`apply_touch_scroll_to_hovered_window()` 對應 ImGui 滾動；`debug_*` 以可控時鐘和合成觸點驗證狀態機。

#### `src/main_window/map_marker_visuals.cpp`

- 圖元建構函式 `append_polyline/polygon/circle/glyph/sampled_arc()` 將標準化幾何寫入 recipe；station、curve、gradient、speed-limit、beacon、pretrain、sound/noise、background、adhesion、cab light、fog 和 other-track 等 `*_recipe()` 定義唯一圖示形狀。
- `map_marker_theme_color()` 按 visual kind 傳回主題色，`map_marker_role_color()` 將 fill/outline/accent/text 角色與主題混合；`map_marker_icon_recipe()` 選擇 kind/variant 的配方。
- `draw_map_marker_icon()` 透過 `transform_icon_point()` 把正規化座標縮放、旋轉、平移到 2D 螢幕，逐圖元呼叫 ImDrawList。3D 程式碼讀取同一 recipe 再產生 billboard 頂點。

### 二維檢視與圖表

#### `src/canvas2d/canvas2D.cpp`

- **查詢與業務資料**：`nearest_own_index()`、`interp_own_z()`、`track_info_at()`、`speed_at()`、`curve_sections()` 為測量和疊加層取樣；`build_plan_data()` 合併 own/other track、站點、限速與 marker，`current_plan_data()` 以 model/geometry/visibility revision 快取；profile 資料有相同 build/current 分層。
- **檢視操作與主流程**：測量、聚焦、座標轉換及 `jump_to_distance()` 統一導覽；`render_plan_canvas()` 編排滑鼠/觸控互動、可見里程計算，以及背景、網格、軌道、Repeater、車站、標記、標籤、3D 位置和焦點的繪製。

#### `src/canvas2d/canvas2d_view_state.h` 與 `src/canvas2d/canvas2d_view_state.cpp`

- `App` 持有的 `View2D` 儲存檢視中心、比例、旋轉、fit 與拖曳狀態，並提供 world/screen 轉換、螢幕增量平移和自適應範圍。

#### `src/canvas2d/canvas2d_marker_cache.h` 與 `src/canvas2d/canvas2d_marker_cache.cpp`

- matrix row/sample/lower/upper-bound 函式在 own/other track 矩陣上按里程取樣，區域偏移函式定位 marker；Repeater LOD、bounds 與 chunk 建置連續佈景覆蓋範圍。
- `rebuild_speed_limit_marker_overlay_cache()` 與 `rebuild_marker_overlay_cache()` 按 edit id、source row 和軌道位置建置平面標記、Repeater 段、其他列車路徑及可見性索引；`App` 管理快取及失效。

#### `src/canvas2d/canvas2d_interaction.h` 與 `src/canvas2d/canvas2d_interaction.cpp`

- 測量命中按 plan generation/scale 快取空間網格，小資料使用窮舉；標記命中按螢幕半徑、列順序和重疊優先順序選擇目標。
- 上下文目標收集、`render_plan_marker_context_menu()` 和 `plan_context_source_for()` 按 marker kind 提供表格定位、屬性/編輯、刪除和原始碼位置。

#### `src/canvas2d/canvas2d_background.h` 與 `src/canvas2d/canvas2d_background.cpp`

- 背景圖模組負責 world/UV 轉換和螢幕四邊形繪製，並以兩個站點座標與兩個圖片點計算比例、旋轉和平移；`App` 包裝層負責儲存背景歷史。

#### `src/canvas2d/canvas2d_primitives.h` 與 `src/canvas2d/canvas2d_primitives.cpp`

- `PlanScreenTransform` 轉換 model/plan/screen 座標；折線建置、範圍裁剪及 Repeater chunk/overview LOD 篩選可見幾何。
- 網格、比例尺及三角、菱形、號誌、先行列車、方向箭頭和文字函式封裝底層 ImDrawList 繪製。

#### `src/canvas2d/profile_plots.cpp`

- 基礎函式繪製 vector 曲線、標題/單位、遮蓋座標軸邊緣、半徑左右旗標、底部鎖定標籤和 profile 垂直 marker；RAII 類別臨時覆蓋 ImPlot fit button 與 wheel zoom 行為。
- 觸控縮放區塊把 `TouchFrame` 轉換為 X 或 XY 軸 limits，並用 `preserved_plot_span()` 在空資料/重建時保持合理跨度。
- `render_profile_plot()` 繪製距離-高程、坡度填色/標籤、站點、限速和可編輯曲線/坡度 marker，處理共用 focus、按兩下測量、hover/context。
- `render_radius_plot()` 繪製距離-曲線半徑，分離左右曲線顯示並重用 transition marker 關聯規則。`render_plots()` 依據目前 2D mode 配置視窗區域並共用快取的 `ProfileData`。

### 三維算繪與場景建置

#### 三維預覽模組

`Canvas3D::Impl` 統一持有預覽狀態、worker、GPU 資源和快取。私有標頭檔宣告狀態與介面，並保留行內數學運算和 Repeater visitor 範本；各功能 `.cpp` 由 CMake 獨立編譯。

| 模組 | 職責 |
| --- | --- |
| `canvas3D.cpp`、`canvas3d_impl.h` | 公開薄委託、共用私有方法與狀態宣告 |
| `canvas3d_math.h`、`canvas3d_types.h` | 行內向量/矩陣運算、CPU/GPU 記錄、共用常數與判定函式 |
| `canvas3d_scene_data.cpp/.h` | typed map/scene 轉換、路線值與車站、標記後設資料、霧輸入轉換和繪製距離 |
| `scene_fog.cpp/.h`、`scene_shader_source.h` | CPU 霧關鍵影格建置/取樣與場景 HLSL；CPU 邏輯及著色器編譯由 `route_value_sampling_contract` 驗證 |
| `canvas3d_scene_lifecycle.cpp` | 場景替換、動態/地圖/車站重新整理、可見性與設定、模型請求及資源生命週期 |
| `canvas3d_model_loader.cpp/.h` | 模型載入器 v2 用戶端、WIC 貼圖與快取、CPU 模型 worker、上傳佇列及診斷 |
| `canvas3d_put_between.cpp/.h` | 來源模型準備與變形、非同步 PutBetween 預覽及經過序號檢查的結果釋出 |
| `canvas3d_model_preview.cpp` | 單模型載入、資源清理與互動預覽 |
| `canvas3d_d3d_resources.cpp` | HLSL、shader 管線、深度/混合/光柵化狀態、算繪目標與執行個體緩衝 |
| `canvas3d_scene_geometry.cpp/.h` | 場景/軌道分塊、軌道放置座標系及 Repeater 執行個體與快取操作 |
| `canvas3d_scene_markers.cpp` | 標記頂點、文字與圖示、字型快取、可見索引與標記繪製 |
| `canvas3d_scene_camera.cpp` | 軌道取樣、攝影機移動與跳轉、聚焦目標狀態 |
| `canvas3d_scene_edit.cpp`、`canvas3d_scene_gizmo.cpp` | 放置預覽更新與失效；操縱器投影、命中、拖曳與繪製 |
| `canvas3d_scene_render.cpp`、`canvas3d_scene_ui.cpp` | 算繪 pass、拾取/醒目提示及可見執行個體；ImGui 編排、疊加資訊、右鍵選單與延遲動作 |
| `tests/scene_render_contract.cpp`、`tests/scene_loader_contract.cpp` | Debug 算繪、快取、畫素、拾取，以及模型載入器所有權與故障契約 |

#### 算繪與互動

- **軌道取樣**：`scene_track_sampling.cpp/.h` 提供普通軌道取樣、攝影機起點前外推和攝影機里程邊界；起點前外推專供攝影機使用。
- **場景資料**：向量、矩陣和邊界框函式建置世界變換；CPU/GPU 記錄分別管理模型、材質、貼圖、分塊執行個體、標記、拾取目標和反向定位。
- **模型載入**：`ModelLoaderClient` 從 `bin/model_loader.dll` 載入 v2 API，並配對配置與釋放。worker 複製 CPU 模型資料，主執行緒透過 `upload_pending_scene_models()` 建立 D3D 資源；取消、join、喚醒和上傳由生命週期模組協調。
- **資源與場景生命週期**：`load_model()`、`upload_model()`、`reload_model()` 管理單模型預覽；`load_scene()`、dynamic/map/station refresh 和 `clear_scene()` 管理場景替換與重新整理。管線視需要建立，貼圖重用快取，執行個體緩衝視需要擴容，資源由對應 release 函式釋放。
- **分塊與放置**：`build_scene_chunks()` 按里程組織 Structure、Signal、Repeater 和軌道幾何；軌道取樣、超高座標系及 `make_track_placement_frame()`、`make_track_world()` 將 BVE 放置參數轉換為世界矩陣。場景使用攝影機相對座標和 reversed-Z 深度。
- **標記與拾取**：共用 2D 圖示配方產生 3D billboard；可見性變化重建標記索引。pick pass 寫入物件/標記 ID 並回讀單畫素，highlight mask 和 outline composite 繪製停留與選擇輪廓。
- **路線資訊**：`scene_route_overlay.cpp/.h` 重用路線值取樣，格式化半徑、超高、坡度、限速、Section 號誌速度與下一站。`Curve.Interpolate` 區間顯示兩端求值後的半徑/超高、方向箭頭與三角分隔符；兩端半徑均顯示為零時使用在地化「直線」標籤。
- **影格編排**：`render_scene_preview()` 在 `canvas3d_scene_ui.cpp` 中處理非同步上傳、攝影機、操縱器、可見執行個體、繪製、拾取、醒目提示和上下文選單，並傳回延遲導覽/編輯/刪除動作；各算繪 pass 由 `canvas3d_scene_render.cpp` 等模組執行。

操縱器按編輯目標產生 `Canvas3DPlacementDragUpdate`：普通放置座標截斷到公釐；Sound3D 的 X/Y 更新音源偏移，Z 以整公尺更新里程；顯式 Repeater End 的 Z 更新段尾。`Structure.PutBetween` 使用沿自軌前向的 Z 軸，將里程吸附到整公尺；worker 合併最新目標、按模型縱向 slice 重用軌道取樣，再將結果釋出到可重用動態頂點緩衝。

#### 影格率與階段計時

畫面 FPS 表示場景算繪呼叫速率。`canvas3d_scene_ui.cpp` 累計不超過 `0.1` 秒的正間隔，活動時間達到 `0.2` 秒後釋出 `interval_count / active_seconds`。長間隔丟棄未完成視窗並保留上次讀數；reset 清空錨點、累計值和讀數。主迴圈的 Present 與算繪喚醒由 `win32_dx11_bootstrap.cpp` 負責。

`scene_frame_profile.h` 提供 Debug 階段計時。算繪與載入器契約透過 `NDEBUG` 條件編譯進 EXE，由 scene benchmark 和 loader headless 命令執行。

#### 霧效果

`scene_fog` 以已填入模型的 `Fog`、`Legacy.Fog` 列建置關鍵影格，並隨場景重新整理而重建：

- 按里程和全域原始碼順序處理事件；非零里程的 Legacy 陳述式展開為原里程處的舊狀態節點和 `distance + 25` 的目標節點，再穩定排序。
- 同里程的首節點是前一段的內插目標，末節點在該里程生效。省略的 Fog 參數繼承已插入的最遠節點，包括未來的 Legacy 目標。
- 取樣透過二分查詢定位；同模式間以 double 內插 density、RGB、start/end，跨模式沿用前節點。沒有霧事件時保持無霧，首個省略值使用預設值。
- 輸入階段過濾無效里程和區間值，顏色鉗制到 0–1，著色器距離限制在安全 float 範圍。

場景畫素著色器按攝影機空間深度（投影 `w`）計算指數霧或線性霧 `clamp((end-depth)/(end-start), 0, 1)`。零寬區間在 `end` 處階躍，有限反向區間使用同一公式。霧作用於背景、模型和軌道；UI、標記與醒目提示 mask 獨立繪製。公式與深度依據見 Microsoft [霧公式](https://learn.microsoft.com/en-us/windows/win32/direct3d9/fog-formulas)和[畫素霧深度](https://learn.microsoft.com/en-us/windows/win32/direct3d9/pixel-fog)。

`src/canvas3d/tests/scene_fog_tests.cpp` 納入 `route_value_sampling_contract`，覆蓋 Legacy/混合模式、相鄰及同里程邊界、繼承、開關和數值保護，並透過 `D3DCompile` 編譯場景共用的頂點及普通/霧畫素著色器入口。

### 資料表格與跨檢視導覽

下列檔案位於 `src/table/`，由 CMake 獨立編譯。`App` 持有 `TableUiCache`；`datatable_cache.cpp` 在區域變數中完成建置後以 move 釋出，`table_navigation.cpp` 管理失效後的狀態重設和跨檢視導覽。

| 模組 | 職責 |
| --- | --- |
| `datatable.cpp` | 共用儲存格/數值轉換、表格 UI 基礎輔助函式及場景軌道 key 語意標註 |
| `datatable_internal.h` | 私有 inline 欄定義及內部 action/view/helper 宣告；視窗、快取建置和專用範本位於各自 `.cpp` |
| `datatable_cache.cpp` | 完整 `TableUiCache` 資料填入、動態 Section/Signal 欄、Repeater 顯示列合併、欄寬測量與執行階段限速快取重新整理 |
| `datatable_find.cpp` | 不區分大小寫的 find/reset/step、unused-key 搜尋，以及 Structure/Signal/Sound 專用 App 查詢 API |
| `datatable_resource_lists.cpp` | 通用可編輯清單算繪，以及 Station、Structure model、Signal aspect、Sound、Sound3D 資源清單視窗 |
| `datatable_route_tables.cpp` | 其他軌道、Station.Put、Structure、其他列車、Repeater、Signal.Put、Section 與 Variable 視窗 |
| `datatable_effect_tables.cpp` | Beacon、Irregularity、聲音/噪聲、Background、Adhesion、CabIlluminance、Fog、Lighting、DrawDistance 與 SpeedLimit 視窗 |
| `datatable_scenario.cpp` | Scenario File 表格、路徑與候選編輯 UI |
| `datatable_benchmark.cpp` | 僅 Debug 的真實地圖快取重建、熱命中與無輸入表格影格基準，並驗證快取摘要和原始碼完整性 |
| `table_navigation.cpp` | 快取失效狀態重設及 table/plan/scene 跨檢視導覽 |

地圖表格讀取 `TableUiCache` 和可見性狀態，草稿修改、選擇與導覽呼叫 `App` 的共用方法。Scenario 表格使用獨立的 Scenario 草稿。

#### `src/table/table_navigation.cpp`

- `invalidate_table_cache()` 清除 revision 與衍生列；`reset_marker_visibility()`、`sync_marker_visibility_sizes()` 保持二維 marker flags 與模型列數量同步。
- Structure、Repeater、Signal 的 `locate_*_on_plan/in_list/in_scene_preview()` 分別更新 plan focus、table highlight/window visibility 和 Canvas3D jump；Repeater 額外處理 End/變化邊界。
- `locate_standard_marker_on_plan()`、`locate_standard_marker_in_list()` 是 Beacon、Section、Irregularity、Sound/Noise、Background、Adhesion、CabIlluminance、Fog、DrawDistance、SpeedLimit 等成對函式的公開實作。
- other-train stop 定位同時開啟對應分組與 stop 列。`locate_scene_marker_row_in_list()`、`locate_scene_marker_row_in_scene_preview()` 把 Canvas3D marker enum 對應回正確表格/來源列；`can_locate_scene_preview_row()` 檢查場景、索引和可見性條件。

### 除錯入口與契約測試

#### `src/main_window/debug_headless.h`

- 每個 `*Options` 結構對應一個命令列模式：Map/Scenario 載入、plan/scene/open/edit/table-cache benchmark、場景 loader/攝影機傳遞、diagnostics popup、source anchor、roundtrip、distance/own/other track、Station/資源清單、Repeater、Section、Include、新增檔案/元素、table find、touch 和 settings persistence。
- 標頭檔宣告 Debug 專用的 `run_debug_headless_*()` 入口；參數結構連線 `main()` 命令列剖析與各測試實作。

#### `src/main_window/debug_headless.cpp`

- **公開設施與參數**：COM RAII、UTF 路徑、輸出檔案、耗時統計、hash、快照 matrix 彙總、記錄捕獲和 fixture 查詢函式為無介面模式提供確定輸出；該檔案也剖析各模式參數。獨立拆分的編輯基準實作在 `edit_benchmark.cpp`，表格快取/繪製基準實作在 `src/table/datatable_benchmark.cpp`。
- **載入/幾何/場景檢查**：基礎 Map load 驗證 snapshot 結構和矩陣，Scenario load 驗證 v2 快照、編輯 roundtrip、候選選擇及剖析後地圖；plan/scene benchmark 重複建置快取並輸出分階段時間、數量和 hash；camera-transfer 檢查 rebuild 前後姿態；scene 除錯讀取畫素與 fog 狀態驗證算繪結果。
- **`typed_edit_headless`**：`Field/Change/Batch/Report` 是公開編輯 ABI 的 RAII 包裝，負責字串 view 生命週期、dry-run/apply/commit 報告複製和失敗資訊。
- **距離/自軌道/其他軌道批次**：`distance_batch_headless` 使用控制代碼、編輯目標、邊界選擇和報告驅動多檔案/Include/變數案例；own/other track 模式驗證方法與參數形狀保留、Apply/Reset/Commit 和幾何變化。
- **清單與關聯編輯**：`station_list_edit_headless` 建立臨時 CSV fixture，驗證編輯/清空/重排/刪除及原編碼；`repeater_batch_headless` 驗證 chain 更新、trim 轉換和原子刪除；`section_edit_batch_headless` 驗證動態參數增刪、null/運算式保留和 commit。
- **插入與原始碼錨點**：insert 模式驗證允許範本、距離區塊選擇和未知欄位拒絕；source-anchor/roundtrip 模式檢查實體檔案、Include stack、行／欄與 span、stable id 和儲存後重新載入一致性。
- **UI 與持久化檢查**：table-find 驗證大小寫、exact/step/unused 狀態；touch 使用合成輸入檢查 tap、long press、scroll、pinch 和取用語意；settings-persistence 在臨時目錄驗證規範格式、無效項預設值、讀取後的檔案位元組和 History 邊界。各入口輸出 PASS/FAIL。

#### `src/main_window/edit_benchmark.cpp`

- `App::run_debug_headless_edit_benchmark()` 使用正式 App 的 Inspector Apply、延遲 Delete、Save、Revert、重新整理與場景首影格路徑；按操作輸出包含式計時與彙總統計。輸入路線及全部已載入實體原始碼逐位元組保護，Save 僅在獨占臨時根目錄中的普通檔案副本上執行；複製流程拒絕 Windows reparse point 和逃逸臨時根目錄的目標。
- 重新整理測試素材驗證完整/區域資料填入的合併、表格/平面快取單次失效、失敗 Apply 恢復及空 Save。該檔案透過 `NDEBUG` 限定為 Debug 實作。

#### `src/main_window/headless_entrypoints.cpp`

- 重用正式 `App` 編輯狀態和對話方塊請求處理，驗證新元素的 Apply/Inspector/刪除路徑、資源清單檔案替換/列插入、新增 Map/Scenario/五種資源清單的建立或重用、引用提交、重新載入與清理，以及 Scenario 生命週期。
- 新增檔案驗證要求目標位於 `tests/` 下且尚未存在，結束後清理建立的檔案。資源清單插入和替換驗證記憶體 Apply；替換流程使用正式檔案選擇器。

#### `src/maploader/tests/typed_snapshot_tests.cpp`

- `TempFixture` 建立並清理臨時 map/list，編碼輔助函式產生 UTF-8/BOM、UTF-16 與 CP932 輸入；`MapHandle` RAII 呼叫 `kv_free()`；`CHECK_ARRAY` 等斷言同時檢查 count 與空指標契約。
- snapshot 測試遍歷所有根陣列、字串/span、metadata、capability、revision 和穩定 edit id，檢查 Windows 匯出表僅保留 `kv_load_map_ex()` 載入入口，並對 Signal glare/可變 key、資源 Load、Include 和場景快照執行定向檢查。
- geometry 測試以坡度/曲線測試素材比較路線長度、平面投影、高程和事件距離。
- `UpdateBatch`、`RepeaterTrimBatch` 等包裝器建構 typed edits；edit 測試覆蓋 dry-run、memory Apply/Reset、直接 Apply、Commit、concurrency hash、距離消歧、方法/參數形狀、語意保護、編碼和交易還原。
- diagnostics 測試載入 `tests/` 本機 fixture，驗證缺檔案、錯誤語法、重複 Load/Enable、未配對 transition、未知 key 及記錄/last-error 文字。`main()` 依據 `snapshot`、`geometry`、`edit`、`diagnostics` 和專項參數選擇測試組，傳回處理程序狀態供 CTest 使用。

#### `src/main_window/tests/route_value_sampling_tests.cpp`

- CPU 契約覆蓋內插區間端點歸屬、省略值繼承、BeginTransition、無效數值，以及 3D 路線資訊的正/負/零半徑格式和零到零內插的直線顯示。

### 靜態呼叫鏈摘要

```text
main / App
  -> maploader_runtime 的 kv_* 轉發器
     -> maploader.cpp 的 C ABI 例外邊界
        -> scenario_route -> KvScenarioSnapshot / Route 候選
        -> parse_map_context -> Parser -> MapContext
        -> generate_geometry -> Matrix / scene control points
        -> MapSnapshotBuilder -> KvMapSnapshot
        -> build_edit_report -> 修補重剖析與語意驗證 -> 記憶體覆蓋或交易寫入磁碟

App / MapModel
  -> datatable、canvas2D、profile_plots 建置按 revision 快取的二維檢視
  -> Canvas3DScene -> Canvas3D::Impl -> D3D11 分塊、非同步模型、拾取與 gizmo
  -> inspector / inline draft -> KvEditBatch -> maploader 原始碼優先編輯鏈
```

`MapContext` 擁有剖析與快照儲存，GUI 複製所需資料到 `MapModel`；畫布按 revision 管理派生快取。DLL 傳回的獨立字串和模型陣列由對應 DLL 的釋放函式回收。

## 核心工程規則

### C++ 與 ABI

- 使用 C++17，優先採用 RAII、標準容器、`std::filesystem` 和職責單一的輔助函式。
- 保持 `UNICODE`、`_UNICODE`、`NOMINMAX` 和 `WIN32_LEAN_AND_MEAN` 定義。
- 公開 C ABI 使用定寬 POD 和明確的記憶體所有權；在邊界捕捉例外，DLL 配置的記憶體由配對函式釋放。
- EXE 要求 maploader API v13、地圖快照 v9 和 model-loader API v2 精確符合。`kv_load_map_ex()` 是唯一地圖載入入口；`KvScenarioSnapshot` 和 `KvScenarioEditDocument` 使用 v2。地圖、場景幾何、編輯目標和報告分別管理版本與結構尺寸。
- ABI 輸入是呼叫期間檢視；巢狀快照由控制代碼持有，按公開標頭檔規定在幾何重建、編輯、重設、重剖析或釋放時失效。Scenario 快照獨立配置，由 `kv_free_scenario_snapshot()` 釋放。
- 公開 ABI 變更須明確版本/結構尺寸策略，同步 EXE、DLL 和呼叫端，並記錄所有權與有效期。

### 剖析、幾何與原始碼保真

支援 BVE Map 2.0+、已有舊式語法、Include、變數、預定義 `distance`、數學函式、註解，以及 UTF-8/BOM、UTF-16LE/BE、CP932/Shift_JIS 輸入。剖析和預設使用官方 BVE 通用語法。

每個 Map 上下文以零里程和檔案內的距離運算式開始。Include 繼承普通變數與原始碼識別資訊；合併時保留父檔案里程及運算式，匯入子檔案事件、控制點和變數寫入。並行預剖析結果按變數相依項檢查，必要時重剖析。

可編輯列保留實體路徑、Include 堆疊、原始碼範圍、原陳述式/參數、求值結果、距離運算式、剖析順序和穩定 ID；`KvMapSnapshot` 以強型別檢視傳遞這些資料。寫回保留原編碼、BOM 和行尾，新增字元無法用原編碼表示時阻止寫入。

AI 程式設計工具修改 BVE 地圖、清單或 Scenario 的讀取、驗證、型別表示、編輯、新增和寫回邏輯時，須同時使用適用的子系統技能和 [`komapedit-bve-format-compliance`](../.agents/skills/komapedit-bve-format-compliance/SKILL.md)。實作前按技能檢查帶日期的官方頁面快取、閱讀受影響頁面並完成合規矩陣。

#### Scenario

`scenario_route.cpp/.h` 管理以下流程：

- **讀取與預覽**：探測檔案類型，驗證 `BveTs Scenario 2.00` 頭部並按宣告編碼解碼，處理 `#`/`;` 註解，保留八個官方欄位的最後一項，以及相對路徑、權重、來源雜湊和欄位存在位元。快照可預覽缺少 Route 或目標檔案的 Scenario。
- **開啟地圖**：`kv_resolve_scenario_routes()` 檢查 Route 候選及目標檔案，再交給地圖載入器驗證。Vehicle 資料用於預覽。Route 剖析或地圖載入失敗時，GUI 保留 Scenario 預覽。
- **儲存草稿**：GUI 透過 Save 直接提交 Scenario。已有 Route/Vehicle 欄位至少保留一個候選，按草稿順序寫回；候選路徑須非空且避開保留語法字元，權重須為有限正數。數量相同時逐項修改，數量變化時重寫該欄位的候選值。寫入磁碟前核對來源雜湊並完整重新剖析，交易寫入保留原編碼、BOM 和行尾。
- **建立檔案**：新增精靈透過 `build_new_scenario_file_content()` 按官方鍵序產生 UTF-8/CRLF 檔案，排他建立後重剖析驗證。Route/Vehicle 初始值為單路徑、無權重；多候選和權重在 Scenario 檔案分頁編輯。

#### 曲線參數與光照

`Curve.SetGauge(value)`、`Curve.SetCenter(x)`、`Curve.SetFunction(id)` 和舊式 `Curve.Gauge(value)` 共用 `CurveEditRow` → `KvCurveRow` → `MapModel::curve_rows`，同時產生自軌道幾何狀態事件。更新保留原方法及原始碼識別資訊；三個新增範本使用現行方法，預設值依次為 `1.067`、`0`、`0`。`SetFunction` 在載入、更新和插入時均要求一個求值為 `0` 或 `1` 的數值參數。

`Light.Ambient`、`Light.Diffuse`、`Light.Direction` 用於表格參數預覽和原始碼編輯，對應 `light.ambient`、`light.diffuse`、`light.direction` 目標及 `KvLightColorRow`、`KvLightDirectionRow`。三組表單始終顯示；編輯條件滿足時可修改參數，Apply、Delete、New 按目標和草稿狀態啟用。Apply 合併已改表單，Delete 使用延遲請求；精靈在所選原始檔的里程 `0` 建立陳述式。

根地圖與 Include 合併後，每類光照最多保留一條語法正確的宣告。同類重複會使衝突列全部無效，並輸出包含各實體位置的英文警告；Ambient/Diffuse RGB 限於 `[0, 1]`，Direction 要求里程 `0`。有效列進入快照，更新保留未改參數運算式並接受完整語意驗證。

### 編輯模型

maploader 持有原始碼及編輯識別資訊，GUI 透過強型別請求操作工作副本。Preview/Edit 依 capability bit 填入資料；清單中的未套用草稿須先在表格中 Apply，再執行 Save。

| 操作 | 職責 |
| --- | --- |
| `kv_edit_dry_run_typed()` | 產生修補報告並驗證 |
| `kv_edit_apply_to_memory_typed()` / GUI「套用」 | 更新記憶體工作副本和預覽 |
| `kv_edit_apply_typed()` | 直接交易寫入磁碟 |
| `kv_edit_commit_typed()` / GUI「儲存」 | 提交已驗證工作副本 |
| `kv_edit_reset_memory()` / GUI「撤銷」 | 丟棄記憶體覆蓋，恢復磁碟基線 |
| GUI「重新載入」 | 確認未儲存更改後重新讀取磁碟 |

`sourceHash` 識別工作副本；`expectedSourceHash` 在多次 Apply/Delete 期間保持為磁碟並行基線。套用或儲存前完整重新剖析，驗證每個目標值、非目標元素及最終變數繫結；有效編輯可改變最終 `distance`。

#### 距離規劃與原始碼修補

里程移動和新增共用剖析器的邊界規劃，按實體檔案、Include 呼叫執行個體、距離段和目標里程分組，保留陳述式順序、註解與空距離區塊。規劃覆蓋隱式初始區塊、錨點間隙、最後區塊和 EOF；明確末段可沿遞增或遞減方向擴充，移動須源於該段，新增優先已有唯一位置。候選列舉與 token 查詢共用規劃，並包含轉折段的末尾鄰接間隙。

每檔案/Include 執行個體的入口、離開環境記錄末尾指派與變數寫入，隨編輯後設資料重建。環境檢查為可恢復問題產生 `evaluation_Environment_Requires_Boundary` 候選，無可行位置時阻斷；`ambiguous_Source_Section` 等原因隨報告傳回。候選篩選完成後，選定方案接受整圖語意驗證。

GUI 的首次處理和快取重用共用動作判斷，優先處理阻斷錯誤；失敗重試以工作副本 hash、結構化更改及整批人工選擇為鍵。距離指派須為有限值，允許有限負距離。未修改的物件鍵運算式、註解和換行按原位元組保留。Section 的 `values.N` 須指向已有參數，調整長度時顯式提供 `values.count`。

#### 元素與資源清單

- **方法轉換**：常規編輯保留方法與參數形狀。Inspector 座標偏移按鈕顯式切換 `Put`/`Put0`、`Begin`/`Begin0`；丟棄非零偏移、短式 `Signal.Put` 轉換及 Repeater 修剪按對應流程確認。
- **Legacy.Fog**：`legacyFog.change` 支援更新、刪除和插入，欄位為 `distance/start/end/red/green/blue`。五個陳述式參數均為必填有限數，允許負值、等值、反向區間及標度外的有限 RGB；未改運算式原樣保留。基線與修改共用語意寫入函式，Include 替換按子樹排除規則保護非目標值，重新整理覆蓋表格、場景霧與標記。
- **資源清單列**：Station、Structure、Signal、Sound、Sound3D 共用列內草稿。右鍵可在上下新增列；Structure、Sound/Sound3D、Station 分別使用 2、3、13 個 CSV 欄位。Signal 主列初始為 6 欄位，後續保留調整後的寬度；主列/glare 成對插入，glare 由使用者顯式新增。
- **Signal 欄**：保留實體列尾端的空欄位。`KvSignalAspectRow::metadata.reserved` 記錄主列的佈景欄位數，`structure_keys` 餘項屬於 glare。形狀編輯提供 `mainStructureKeyCount`、`glareStructureKeyCount` 及完整編號欄位，儲存格編輯保留原形狀；每個既有的實體列至少保留一個佈景欄位，允許全空。主列/glare 分界參與語意和待儲存狀態判斷；509 個佈景鍵欄的顯示上限之外，欄位操作仍使用實際完整寬度。
- **Repeater 關聯與重新命名**：各層共用 `repeater_linkage` 和過渡關聯。生命週期為半開區間 `[第一個 Begin, End)`，按距離、全域順序和來源列排序，同里程末事件決定活動狀態；空區間保留原始碼識別資訊。重新命名批次須包含整鏈 Begin/Begin0 與顯式 End，並透過重名區間及鏈歸屬檢查；重剖析驗證非目標邊界，零長度新段按 Begin、End 順序產生。
- **Repeater 佈景鍵**：資料填入、Inspector 和場景透過 `repeater_structure_keys()`、`set_repeater_structure_keys()` 共用 `_structureKeys.count`、`_structureKeys.N`；`structureKeys` 串接文字用於顯示。模型陣列保留缺失項位置，以原清單長度計算 `k % N`；端點模型缺失時，攝影機使用放置幾何錨點。
- **其他軌道重新命名**：一個 typed batch 須包含根地圖和全部 Include 中同鍵的所有 `Track[trackKey].*` 陳述式。鍵比較保留數值/字串型別並忽略大小寫；批次完整且新鍵全圖唯一時才接受重新命名，引用該鍵的其他元素按非目標列驗證。

### UI、表格與算繪

- 保持 Dear ImGui docking 配置、選單和工具概念。普通 UI 文字同步簡體中文、英文和日文；語言切換時保持 ImGui ID 穩定。
- BVE 參數標籤使用官方英文名或縮寫，如 `distance`、`trackKey`、`x`、`ry`；程式診斷本文和 headless 輸出使用英文，主控台周邊 UI 使用三語。
- 保持二維平移/縮放/旋轉/適配、測量、網格、車站跳轉、背景對齊，以及三維攝影機傳遞、拾取/醒目提示、可見性、標記、路線資訊和操縱器聯動。
- 表格按 revision 快取，保留 Section 動態參數和顯式 `null`、變數順序及跨檢視導覽。Repeater 查詢使用有序型別化佈景鍵；尋找未使用結構前，將活動 Signal 主列/glare 儲存格提交到草稿。
- Assimp 隔離在 `model_loader.dll`，載入錯誤經診斷和清理路徑傳回。模型包圍範圍以 double 計算，非有限位置或超出公開 float 範圍的半徑被拒絕；動態場景重新整理失敗時恢復 Repeater chunks 及快取總數。

#### 草稿與檢視狀態

`App` 為待儲存的既有列 update/delete 保留磁碟原始列，完整和區域資料填入均沿用此基線。Apply 失敗恢復先前狀態，成功後移除已離開帳本的基線，Save/Revert 清理完成的帳本。Inspector 按欄位記錄差異，欄位恢復原值時僅移除該項更改。其他軌道按精確鍵保留可見性、顏色和顯示範圍，顯式重新命名透過穩定 ID 轉移狀態。

#### 設定與作者訊息

應用程式偏好、最近地圖/背景及作者訊息偏好、ImGui 配置分別存入 `settings/settings.ini`、`settings/history.ini`、`settings/imgui.ini`。載入器接受儲存端的精確節、鍵和值語法，未知或無效項採用預設值；已有檔案由顯式儲存操作重寫。設定和配置在完整寫入並關閉成功後標記為已儲存，失敗後由 `App::service_pending_persistence` 按各自一秒期限重試，視窗遮擋或空閒時也適用。配置待儲存狀態由 ImGui 請求旗標持有。

作者訊息來自獨立的 `//--kme--message-from-creator:` 註解，保留實體位置與 Include 識別資訊，按實體訊息去除重複後以 `KvCreatorMessageRow` 提供給 Preview/Edit。`creator.message` 編輯接收原文 `content`，檔頭插入使用共用修補、重剖析和提交路徑。`creator_messages.cpp` 管理草稿、表格及檔案開啟時的彈出視窗；歷史使用 `[CreatorMessages] count` 和 `[CreatorMessageN] path/has_messages/suppressed`，按主 Map 保留顯示偏好，包括訊息暫時為空時的抑制設定。

#### 曲線標記

`CurveGauge`、`CurveCenter`、`CurveFunction` 標記攜帶列索引和 edit ID。2D 繪製白色 `CG`/`CC`/`CF` 矩形，3D 繪製程式碼與求值參數組成的白色雙行標牌，共用拾取和藍色醒目提示。`[View2D]` 的 `show_curve_gauge_markers`、`show_curve_center_markers`、`show_curve_function_markers` 預設關閉，各自控制標記可見性。

`Curve.Interpolate` 保留 0/1/2 參數形狀及編輯識別資訊。填入資料時，依原始檔索引和全域陳述式順序關聯求值事件，支援排序後的事件和同里程重複 Include。Edit 後設資料合併後，2D 端點與 `CurveCircularStart` 外觀的 3D 標牌使用同一來源列，提供屬性/編輯和延遲刪除，每條陳述式對應一個標記；標牌顯示求值半徑/超高，零半徑顯示 `Intpl. 0`。

Preview 階段，平面停留按標記陣列索引命中，原始碼選擇和編輯等待來源繫結；3D 保留 `curve` 分類和右鍵選單，編輯/刪除項在後設資料就緒後啟用。新增範本顯示完整語法簽章 `Curve.Interpolate(radius, cant);`，透過可選尾參數產生三種參數形式。

### 效能

- 按輸入和 revision 重用讀取、解碼、雜湊、路徑剖析、快照、表格、標記和幾何結果。
- 大陣列儘量連續，控制緊密迴圈及逐影格配置，避免大型軌道陣列上的 O(n²) 遍歷。
- 長時間剖析和模型載入使用非同步流程並提供進度；每個快取明確完整 key、失效所有者和修訂，覆蓋命中與失效路徑。

`refresh_local_preview_after_edits()` 合併 Apply/還原重新整理：完整資料填入覆蓋區域軌道/清單轉換，完整場景重建覆蓋舊場景更新；單執行個體和 Repeater 座標編輯使用穩定 ID 快速路徑。距離與實體錨點索引在首次使用時建置並由同批次重用。

原始碼修補排序及重疊檢查後，按順序追加原文和替換片段，由最終位置計算識別資訊偏移。報告保留降序編輯順序及前後各 80 位元組預覽上下文，包括右側已套用修改。Save 將編碼位元組移動到交易請求，保留長度、雜湊、寫入核驗及還原資訊。

Canvas3D 在替換場景時建立放置軌道索引，支援自軌道別名、正規化後第一個相符的其他軌道和缺失鍵備援。Repeater 按 `begin + k * interval` 放置，每個 Begin 重新起算，並按原結構清單的 `k % N` 選擇模型。雙精度變換快取在目前視窗的 chunk 中，共用上限為 65,536 個；超預算部分視需要計算。編輯使新舊 chunk 範圍失效，場景/chunk 替換重設快取，離開視窗時釋放快取；放置幾何獨立於軌道可見性。

背景圖僅修改幾何且請求亮度與已上傳值一致時重用貼圖；更換圖片使貼圖失效，上傳成功後更新亮度記錄。

編輯計時使用 `operation_timing.h` 的穩態時鐘，GUI 與 maploader 分域記錄包含式 `*_ms`、`*_count`，GUI 總用時包含 DLL 用時。App 計時持續到延遲 Inspector 處理及必要的首個場景影格完成；隱藏或摺疊的場景直接結束等待。`source.read_decode_hash` 記錄呼叫執行緒的原始碼處理，並行 Include 工作計入 `parse.include_join_merge` 和 `parse.syntax_diagnostics`，模型載入使用獨立非同步記錄。

## 驗證

按受影響元件選擇檢查，分別記錄建置、契約測試、headless 和人工介面驗收結果。磁碟寫回須儲存後重新載入比較；算繪、選單、對話方塊和拖曳等視覺互動另行驗收。

`komapedit.exe` 使用 GUI 子系統。在 PowerShell 中捕獲輸出時，使用 `Start-Process -Wait -WindowStyle Hidden -PassThru` 並傳入 `--headless-output`。驗證命令應顯式提供輸入路徑；部分自軌道、其他軌道、距離、Repeater 和 Section 模式的省略路徑時會使用開發者本機路線。

### 效能基準

前後比較使用相同路線、參數、建置類型與載入配置。scene、table-cache 和 edit 基準各順序執行三個獨立處理程序，期間暫停建置及其他基準；持續影格驗收關閉 profiling。

#### 場景與載入器

`--debug-headless-scene3d-bench` 預設測量 300 個固定攝影機影格，取樣間距 25 m，後方/前方視窗 100 m/1200 m，CPU 影格耗時 p95 上限 16.67 ms。`--interaction moving` 每個影格前進 2 m，使用正常末端鉗制。計時在模型載入完成及五個預熱影格之後開始，報告介面卡、負載、最慢影格和可見執行個體指紋。

`--profile-stages` 記錄巢狀 CPU 階段和非同步 GPU 時間戳記。查詢提前配置、跨影格讀取，僅統計有效且就緒的樣本；GPU 區間包含命令間隙，CPU 父階段包含子階段。分段資料用於定位瓶頸，驗收依據為整影格 CPU p95。

計時後，場景契約比較直接計算與快取路徑的雙精度執行個體指紋、畫素和拾取，覆蓋攝影機移動/旋轉、chunk 跳轉/傳回、視窗、霧及軌道輔助線變化。`--debug-headless-scene-loader-contract` 使用臨時模型和 headless D3D 驗證：

- Repeater 距離/模型序列、強型別清單資料填入、Include 順序、缺失項、零長度段、幾何跳轉錨點、tilt/span、無效間距、編輯/恢復失效、軌道替換及快取預算。
- 模型複製與 PutBetween worker 故障、取消、請求協調、DLL 配置/釋放平衡，以及同路徑模型重用與重新載入。
- 模型包圍範圍的有限值、可表示範圍及失敗清理，動態重新整理失敗後的 Repeater 快取恢復。
- 背景圖的貼圖識別資訊和畫素：無變化、僅幾何變化、亮度變化、圖片替換、上傳失敗/重試；1024×1024 圖片基準輸出幾何 Apply 的 median/p95 和貼圖替換次數。

#### 平面與表格

`--debug-headless-plan-bench` 預設 `--interaction pan`，比較快取與直接計算的曲線、緩和曲線和 Interpolate 端點。測量模式將命中結果與窮舉對照：小點集線性掃描，大點集使用空間網格；`measure-stationary` 固定指標，`measure-moving` 使用確定軌跡。

plan、scene 和 own-track-edit 共用臨時 Map/Include 契約，覆蓋 Interpolate 的 0/1/2 參數、Apply/Delete/Reset、原始碼識別資訊及唯一可編輯標記。真實路線按實際存在的目標驗證；空的列、事件和標記集合須一致，需要執行個體的檢查記為不適用。own-track-edit 還檢查 Preview→Edit 合併，包括透過 `rand()` 選擇原始碼檔案的地圖。

`--debug-headless-table-cache-bench` 使用 Edit 後設資料及關閉 INI 持久化的 ImGui/ImPlot context。預熱後分別測量冷快取重建、熱快取單次命中和全部表格的一影格繪製，核對列、儲存格、識別資訊、動態標題/寬度的指紋。原始碼位元組/雜湊檢查位於計時區間外。預設重複 5 次（範圍 1–100），取樣間距 25 m。

`--debug-headless-diagnostics-popup-bench` 使用 100,000 條混合記錄檢查並列順序、快照修訂快取和裁剪算繪。

#### 編輯

`--debug-headless-edit-bench` 呼叫正式 Apply、延遲 Delete、Save、Revert 和重新整理路徑。輸入地圖須包含可編輯的 `Structure.Put`、`Repeater.Begin`、`Curve.SetGauge`；按原始碼順序選擇首個有效目標，以固定座標/軌距增量編輯。輸入路線用於記憶體操作和位元組/雜湊核對；Save 使用保留相對相依項配置的獨占臨時普通檔案副本，並檢查路徑邊界與重新剖析點，結束後清理。

預設 `--scene off`、`--repeat 5`（1–100）、`--unit-distance 25`。啟用場景時使用 1260×680 畫布、後方/前方 100 m/1200 m 視窗，攝影機置於路線最小里程 + 500 m 並按正常 API 鉗制。每輪恢復副本並新增 App，待模型初始載入完成後計時，計入必要的首個場景影格。報告目標識別資訊、巢狀階段、總體 median/p95/最大值、原始碼檢查和重新整理/還原契約；每種 3D 狀態各執行三個處理程序、每個處理程序五輪。

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
build\komapedit.exe --debug-headless-new-file-wizard <tests目錄下尚不存在的地圖路徑> --headless-output build\new-file-wizard.txt
build\komapedit.exe --debug-headless-scenario-create <tests目錄下尚不存在的Scenario路徑> --route <已存在的地圖路徑> --headless-output build\scenario-create.txt
build\komapedit.exe --debug-headless-fresh-resource-list-workflow <地圖路徑> --headless-output build\fresh-resource-list.txt
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

### 資源清單與檔案工作流程

| 命令 | 輸入與寫入範圍 | 主要檢查 |
| --- | --- | --- |
| `--debug-headless-resource-list-replace` | 地圖路徑；開啟 Win32 選擇框，需手動選擇另一份有效 Structure List；記憶體 Apply | Preview/Edit 後設資料合併、穩定識別資訊、完整重新剖析、清單快取和路徑重新整理、重新載入後來源雜湊。取消、同檔案或無效清單傳回 FAIL |
| `--debug-headless-resource-list-insert` | 地圖路徑及 `--kind structure` 或 `signal`；記憶體 Apply，拒絕 `--commit` | structure 要求清單經 Include 載入，檢查 2 欄位列與上下順序；signal 檢查主列/glare 插入塊、6 欄位主列及顯式新增 glare；均檢查 Reset 和來源雜湊 |
| `--debug-headless-signal-aspect-columns` | Map/Scenario；輸入路線僅記憶體編輯，Save/reload 使用臨時測試素材；拒絕 `--commit` | 正式欄位操作、確認/取消、主列/glare 獨立寬度、多輪 Apply/Revert 和顯示上限 |
| `--debug-headless-new-file-wizard` | `tests/` 下尚不存在的地圖路徑；建立檔案後清理 | 空地圖重新載入、排他建立、重用已有清單、五類 Load 暫存/儲存、空清單重新載入 |
| `--debug-headless-scenario-create` | `tests/` 下尚不存在的 Scenario 路徑，`--route` 指向已有地圖；結束後刪除新 Scenario | 官方鍵序、重複建立拒絕、欄位存在位元、相對路徑/預設權重、Scenario 預覽與非同步地圖載入、歷史保持 |
| `--debug-headless-scenario-lifecycle` | Scenario；輸入原始碼唯讀，寫入使用獨占臨時目錄 | 歷史入口、後設資料釋出、Reload 檢視恢復、單/多 Route 選擇、新增/載入、先 Map 後 Scenario 儲存、設定/配置重試、CSV 匯出及檔案失敗處理 |
| `--debug-headless-fresh-resource-list-workflow` | 地圖路徑；唯讀輸入並在臨時目錄建立 Map、Structure、Station 和 Signal 測試素材 | 無距離地圖的 Load 目標、多個未儲存 Load 與空清單首列同批 Apply；Signal 多輪 Apply、glare 刪除/重加、Revert、相鄰列 Save/reload。檢查 `input_map_bytes_unchanged`、`fixture_files_cleaned` |
| `--debug-headless-table-find` | 內建測試素材 | 普通 Sound 的車站草稿引用與 Sound3D 區分；含逗號/空格的 Repeater 鍵；查詢前提交活動 Signal 主列/glare 儲存格 |

### 元素編輯與插入

`--debug-headless-new-element-edit` 驅動正式精靈、Inspector 和延遲刪除/取消流程，覆蓋資源、Repeater、Structure、其他軌道及組合 Curve/Gradient 範本，檢查起止里程、緩和/cant 聯動、原始碼順序、目標檔案和後續編輯。資源清單 key 僅預填符合範本的欄位。

此命令預設在 Reset/Reload 後核對來源雜湊；`--commit` 經 Save 向選定原始檔寫入成對曲線和坡度，並保留修改供實體 diff 檢查。混合帳本案例需要可編輯 `Structure.Put` 及其他軌道位置/X/Y 內插參數列，檢查插入順序、多輪 x/y 修改、單欄位恢復、失敗 Apply/Revert 和檢視狀態；相關 Save/fresh Reload 使用四組臨時測試素材。

`curve.interpolate` 範本使用可選尾參數、共用型別化驗證和語意指紋，接受官方 0/1/2 參數形式，拒絕僅含 cant、非有限值、未知欄位和不支援的方法。`typed_edit_contract` 在 Shift-JIS/CRLF Include 測試素材執行 dry-run、Apply/Reset/Commit/Reload，檢查運算式、註解、順序與識別資訊；新增元素 headless 驗證預設雙參數、核取方塊聯動、後續編輯/取消和唯一來源標記。

下列命令使用顯式輸入路徑，透過記憶體編輯驗證正式工作流程，拒絕 `--commit`：

| 命令 | 輸入條件 | 檢查內容 |
| --- | --- | --- |
| `--debug-headless-light-edit` | 含任意有效 Light 宣告子集的地圖，如 `tests\light_valid.txt` | 後設資料合併、三組表單 Apply、延遲刪除、里程 0 的三類精靈新增、求值和原始碼形式，Revert 到原子集並核對位元組 |
| `--debug-headless-pretrain-edit` | 含既有 `PreTrain.Pass` 的地圖 | `headless_pretrain.cpp` 驗證時間/秒數多輪 Apply、里程、無效輸入、刪除、精靈、插入後編輯/取消、Revert，以及 2D 識別資訊/標籤和 3D 標記資料 |
| `--debug-headless-legacy-fog-edit` | 舊式霧列數量不限的地圖 | `legacy_fog_edit_validation.cpp` 驗證識別資訊、無效輸入、多輪 Apply、刪除、精靈和 Revert；WARP 場景驗證霧重新整理，另檢查表格/平面快取和完整重建安排 |
| `--debug-headless-curve-parameter-edit` | 同時含 SetGauge、SetCenter、SetFunction 的地圖 | CG/CC/CF 識別資訊、白色標牌、獨立可見性、Inspector 里程/參數、`SetFunction(2)` 拒絕、刪除/新增、Revert 和來源位元組 |
| `--debug-headless-station-put-margin-edit` | 里程 0 有可編輯 `Station.Put` | 零值/錯誤符號容差拒絕，精靈預設 `margin1=-5`、`margin2=5`，有效插入與 Revert |
| `--debug-headless-sparse-new-element` | 目標源有零/一條數值距離陳述式，或錨點非遞減且末值小於 866 | 正式 `DrawDistance.Change(500)` 精靈；稀疏源用里程 25，單調尾部用 866，檢查塊重用/前插/尾插、原文、新列識別資訊和值，Reset 後核對雜湊 |
| `--debug-headless-auto-insert-diagnostics` | `testmap\auto_insert_failures` 測試素材目錄 | 按實體檔案、行、型別、里程及識別資訊選擇目標，驗證自動成功、人工恢復、硬拒絕、每個候選的實際 Apply、二次 Apply、重試終止和 Reset；成功條件為 `failed_cases=0`、`result=PASS` |

PreTrain 的 `passTime` 接受未加引號的 `hh:mm:ss` 或有限秒數，允許超過 24 小時及非遞增時刻。PreTrain 與 Legacy.Fog 的編碼、BOM/行尾、Include、磁碟並行及 Save/reload 由型別化契約的臨時測試素材覆蓋；自動插入契約另覆蓋混合 EOF 批次和過期選擇。

### 關聯編輯與 Include

下列模式的 `--commit` 會提交已驗證工作副本，並保留路線修改供 diff 檢查；預設執行記憶體驗證及來源雜湊檢查。

| 命令 | 輸入條件與驗證範圍 |
| --- | --- |
| `--debug-headless-repeater-key-edit` | 顯式地圖路徑；整鏈重新命名驗證 |
| `--debug-headless-other-track-key-edit` | 顯式地圖路徑；選擇至少含兩條陳述式的字串鍵其他軌道，檢查整軌原子性、全圖名稱重複、相依項引用、Apply/Reset/Reload 和目標/檔案雜湊 |
| `--debug-headless-insert-edit --repeater-only` | 分別新增唯一 key 的 Begin 和 Begin0，執行 dry-run、Apply/Reset；提交時驗證 Save/Reload |
| `--debug-headless-include-delete` | 顯式地圖路徑，`--index` 預設 0；檢查陳舊雜湊拒絕、子樹刪除、非目標語意和 Reset；存續陳述式依賴被刪除的子樹時阻斷 |
| `--debug-headless-include-replace` | 顯式地圖路徑、`--new-path <file>`、`--index` 預設 0；路徑文字按單引號參數寫入，檢查新舊子樹排除後的整圖語意、結構重新整理、Reset/Reload；依賴舊變數或重複 Load 時阻斷 |

`--debug-headless-include-import-create` 在顯式輸入地圖的同目錄建立唯一臨時子地圖，驗證已有檔案匯入、新增檔案、Include 插入、零距離錨點、重剖析和結構重新整理。子地圖使用 UTF-8 無 BOM、CRLF 和 `BveTs Map 2.02:utf-8` 檔頭；父地圖僅記憶體 Apply/Reset，結束時核對原檔案雜湊並清理臨時子地圖。

### 作者訊息與持久化

`--debug-headless-creator-message <map-or-scenario-path> [--scenario-index N] [--unit-distance M] --headless-output <report>` 唯讀載入輸入並核對原始檔位元組，拒絕 `--commit`。獨立臨時地圖和歷史配置用於驗證 Preview/Edit 後設資料、精靈、原文內容、未套用草稿阻斷 Save、Apply/Revert/Save/Reload、Include 訊息、開啟時彈出視窗和抑制偏好；實體去除重複與原始碼展開順序由 DLL 契約覆蓋。

`--debug-headless-settings-persistence` 驗證設定與歷史的規範往返、無效項預設值和讀取後的位元組保持。作者訊息的 `has_messages`、`suppressed` 以 `0`/`1` 儲存，載入時僅精確值 `1` 剖析為真。

### 人工驗收範圍

按改動選擇地圖/Include 載入與重新載入、平面/縱斷面/半徑圖、車站跳轉、測量、CSV 匯出、模型預覽與錯誤、三維物件/標記/攝影機/操縱器、Apply/Revert/Save/Reload、列內草稿、設定持久化及 Release 散布檢查。介面驗收重點包括命中與醒目提示、選單、確認框、列捲動和最終算繪畫素。

## 建置指令碼、相依項與散布

- 建置配置以 CMake 為準，批次指令碼負責 Windows 建置流程。
- 保留 `NINJA_EXE`、`VCPKG_ROOT` 和 `x64-mingw-dynamic` 備援。
- EXE 與聲明檔案位於輸出根目錄，DLL 位於 `bin`，INI 位於 `settings`。
- 散布清理保留 `bin`、`settings`、`LICENSE`、`NOTICE` 與 `THIRD_PARTY_NOTICES.md`。
- 開發建置、Release 建置和釋出清理共用目錄配置檢查，發現根目錄舊 INI 或 DLL 時立即中止。
- ImGui 使用 docking 分支，ImPlot 使用上游版本。
- 保留授權條款與聲明檔案；新增相依項須同步 CMake、開發者文件和第三方聲明。
- 路線釋出匯出列於 `TODO.md`：計畫展開 Include、可選常數化運算式、複製已用資源並輸出報告，使用臨時輸出目錄保護開發路線。
