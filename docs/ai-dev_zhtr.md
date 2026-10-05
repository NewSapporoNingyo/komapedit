# 使用 AI 程式設計工具開發 komapedit

[開發者指南](dev_zhtr.md) · [供 AI 工具閱讀的儲存庫規範](../AGENTS.md) · [開發進度](../TODO.md)

本文供使用 AI 程式設計工具開發 komapedit 的人員閱讀，介紹如何描述任務、選擇工作流程、追蹤修改進度和驗收結果。

開始前，建議熟悉受影響的程式操作、基礎 C++、Windows 建置環境，以及程式碼差異和測試結果的閱讀方法。這些知識有助於核對 AI 提出的方案，並作出產品與驗收決策。

儲存庫為 AI 工具準備了 [`AGENTS.md`](../AGENTS.md) 和[專案技能](../.agents/skills)，提示詞可直接引用對應技能。[專案經驗](../.agents/memories/INDEX.md)有助於理解過往決策；目前實作以原始碼和測試為準，開發進度見 [`TODO.md`](../TODO.md)。專案專用技能與經驗儲存在 `.agents` 中，使用者層級的指引用於跨專案內容。

## 把任務描述清楚

從具體操作和可觀察的結果出發，寫清以下內容：

- 已觀察到的行為或要實作的功能；
- 重現步驟，以及相關輸入、記錄、截圖或路線檔案；
- 修改範圍，以及需要保留的操作、相容性和效能；
- 預期結果與驗收標準；
- 建置與測試範圍、可用於驗證的路線，以及人工 GUI 檢查的安排。

例如，修復卡頓時，提供觸發操作、路線規模、建置類型和耗時，比籠統要求「提升效能」更便於定位和驗收。涉及檔案格式策略、相容性遷移、公開 ABI 或新增相依項等重要選擇時，可先安排唯讀調查，確認方案後再實施。

## 選擇合適的工作流程

儲存庫提供日常開發、問題修復、程式碼維護和文件編寫四類工作流程。用戶端支援顯式呼叫技能時，可以使用下面的提示詞範本；其他用戶端可用自然語言描述任務，並引用技能路徑。場景技能會依據任務引入原始碼編輯、表格、2D/3D 預覽、設定、三語 UI 和驗證等專項技能。

### 日常開發

[`komapedit-develop`](../.agents/skills/komapedit-develop/SKILL.md) 適合功能新增或行為調整。

```text
使用 $komapedit-develop。
需求：[功能、受影響工作流程、約束與驗收標準]
保留：[相容性、效能、使用者操作、檔案或 API]
驗證：[Debug 建置、相關 CTest/headless 檢查、驗證路線和人工 GUI 檢查]
```

一般開發使用 Debug 建置。涉及發行封裝、執行階段相依項或最佳化相關問題時，再安排 Release 驗證；指令見[開發者指南](dev_zhtr.md)。

### 問題修復

[`komapedit-fix`](../.agents/skills/komapedit-fix/SKILL.md) 從重現問題和定位根因開始，再在負責該行為的模組中修復。根因尚不清楚時，可先要求唯讀診斷；驗收時重點檢查原始重現步驟和相關回歸測試。

```text
使用 $komapedit-fix。
實際表現：[症狀、記錄、建置類型、路線/輸入與重現步驟]
預期表現：[正確行為]
範圍：[只診斷，或診斷並修復]
驗證：[原始重現條件，以及相關 Debug/Release/headless 檢查]
```

### 程式碼維護（slop-fix）

[`komapedit-slop-fix`](../.agents/skills/komapedit-slop-fix/SKILL.md) 用於清理有證據的程式碼品質問題，例如重複邏輯、無用狀態、快取錯誤和隱藏的當機風險。它先檢查，再修復已確認的問題，並保持現有行為與 ABI。

```text
使用 $komapedit-slop-fix。
範圍：[具體子系統、變更檔案或整個專案]
先唯讀檢查，列出問題證據、影響、修復方案和驗證方法，再修復已確認的問題。
保留現有行為與 ABI。
驗證：啟用嚴格警告的 Debug、已註冊 CTest、相關 headless 檢查；涉及效能時提供同條件的前後資料。
```

檢查結果可記錄為「未發現需要修改的問題」。

### 文件編寫

[`komapedit-write-docs`](../.agents/skills/komapedit-write-docs/SKILL.md) 適合文件修訂和程式碼變更後的說明同步。指定讀者、檔案和語言範圍，並以目前原始碼、測試及建置指令碼為事實依據。

```text
使用 $komapedit-write-docs。
內容與讀者：[需要說明的目前行為、工作流程及目標讀者]
檔案與語言：[文件路徑；需要同步的語言版本]
修改範圍：僅文件。
```

[`komapedit-doc-sync-validation`](../.agents/skills/komapedit-doc-sync-validation/SKILL.md) 用於檢查修改範圍、各語言版本的語意、編碼、連結和表格。原始碼導覽與驗證指令還需核對目前檔案、CMake 目標和命令列入口。

### 涉及 BVE 格式與原始碼編輯時

BVE 格式相關的開發和修復需配合 [`komapedit-bve-format-compliance`](../.agents/skills/komapedit-bve-format-compliance/SKILL.md)。它覆蓋讀取、剖析、驗證、強型別表示、編輯、新增、序列化和寫回；具體格式包括[官方來源基線](../.agents/skills/komapedit-bve-format-compliance/references/official-bve-format-baseline.md)中的 Map、Structure List、Signal Aspects List、Sound List、其他列車和 Scenario。

這類任務會先依據官方頁面整理合規矩陣，區分現行語法、舊式別名、專案相容形式與未支援形式，再進入實作。官方頁面使用帶日期的本機快取，時間戳記無效或超過 30 天時整套重新整理。遇到官方規範、需求與實作之間影響行為的差異時，由使用者確認處理方案。

```text
同時使用 $komapedit-bve-format-compliance 與 $komapedit-develop（或 $komapedit-fix）。
格式與元素：[受影響的檔案、陳述式、清單列、節或 key]
操作：[讀取 / 驗證 / 編輯 / 新增 / 序列化 / 寫回]
驗收：[官方語法簽章與語意、儲存後重新載入、有效及無效輸入、原始碼保真]
```

原始碼編輯使用 [`komapedit-source-backed-editing`](../.agents/skills/komapedit-source-backed-editing/SKILL.md)。涉及 Station、Structure、Signal、Sound 或 Sound3D 清單的匯入、替換、新增、空清單首列及列插入時，再配合 [`komapedit-resource-list-source-editing`](../.agents/skills/komapedit-resource-list-source-editing/SKILL.md)，共同檢查格式、編輯與儲存流程。

## 與 Pi Agent 協作

Pi Agent 是一款高效、簡潔的程式設計代理。需要 Pi 負責實作時，可直接在 Pi 中輸入提示詞，或明確要求其他程式設計代理「呼叫 pi agent」，也可使用 [`$collaborate-with-pi`](../.agents/skills/collaborate-with-pi/SKILL.md)。

統籌代理先完成儲存庫調查和開發計畫，將計畫寫入 `pi-prompts_local.txt`，再透過 `pi-agent-here(local).bat` 在可見的 Windows Terminal 中啟動 Pi。Pi 負責主要實作、相關測試和文件同步；統籌代理負責追蹤進度，並在 Pi 完成後審查實際差異、獨立重跑關鍵驗證，必要時安排修正。

出現 Pi 工作階段異常時，協作會終止並報告已完成的修改和剩餘工作，由使用者決定下一步。啟動、狀態檢查和修正細節見技能說明。

## 驗收修改結果

先把實際程式碼差異與任務範圍、驗收標準逐項對照，再依據改動選擇檢查重點：

- **功能與介面：** 重新測試受影響的操作，檢查英文、簡體中文和日文文字，以及表格、2D、3D 之間的選擇、標記、導覽和設定儲存。
- **路線編輯：** 檢查 Include、原始運算式、陳述式順序、編碼、BOM 和換行符的保留情況，並比較儲存、重新載入後的內容。
- **模組介面：** 涉及 DLL 時，核對帶版本的強型別 C ABI、結構尺寸、資料所有權、釋放函式和例外處理。
- **效能：** 用相同路線、參數、建置類型和負載比較前後資料，同時檢查重複讀寫、逐影格重建和快取失效邏輯。
- **相依項與散布：** 核對 Windows 與工具鏈相容性、授權條款及聲明檔案、執行階段 DLL 配置。

地圖和資源清單的 Apply 更新記憶體工作副本與預覽，Save 將修改寫入檔案，Revert 撤銷待儲存修改，Reload 從磁碟重新讀取，有未儲存修改時先請求確認。資源清單草稿需要先在表格中 Apply；Scenario 草稿則透過 Save 直接儲存。編輯類任務可據此安排完整的操作驗證。

驗收報告應列出實際執行的命令、結果和已知失敗，並分別記錄建置、CTest、headless 與人工 GUI 檢查。算繪、選單、對話方塊和拖曳效果需要結合實際介面確認。若任務偏離範圍、反覆卡在同一失敗操作或報告與證據不符，可暫停任務，審查目前差異後重新明確目標。
