# 開発者ガイド

[README](README_jp.md) · [開発計画](../TODO.md) · [AI を使った開発](ai-dev_jp.md)

komapedit の開発環境、アーキテクチャ、ソースごとの役割、検証手順を説明します。AI コーディングツールを使う場合は、[AI 開発ガイド](ai-dev_jp.md)、[`AGENTS.md`](../AGENTS.md)、[`.agents/skills`](../.agents/skills) 内の該当するワークフローにも従ってください。

## 対象と対応環境

komapedit は、BVE Trainsim マップを表示・編集する、C++17 製の Windows デスクトップアプリケーションです。Win32、DirectX 11、WIC、Dear ImGui、ImPlot を使用し、CMake と Ninja でビルドします。

実行時の構成は次の 3 コンポーネントです。

- `maploader.dll`：マップとリストの解析、Include と文字コードの処理、自軌道・他軌道の形状生成、バージョン付きの型付きマップ・編集スナップショットの管理。
- `model_loader.dll`：Assimp によるストラクチャーのメッシュ・マテリアル・テクスチャの読み込みと、C ABI によるデータの提供。
- `komapedit.exe`：Win32/DirectX 11 の GUI、テーブル、2D グラフ、3D プレビュー、編集操作。

実装済みの動作はソースとユーザー向け文書を参照してください。未完了の作業は [`TODO.md`](../TODO.md)、完了済みの作業は [`TODO_done.md`](TODO_done.md) に記録します。

## 必要な環境

- Windows
- CMake 3.20 以降。汎用の Assimp 実行時 DLL コピー処理を使う場合は 3.21 以降を推奨
- Ninja
- MSVC、MinGW などの C++17 対応コンパイラー
- Windows SDK、DirectX 11、WIC の開発ライブラリ
- Git
- CMake から `assimp::assimp` として参照できる Assimp

Dear ImGui と ImPlot を取得します。

```bat
.\get_3rd_party_packages.bat
```

Assimp は別途インストールします。vcpkg を使う場合は `VCPKG_ROOT` を設定し、ツールチェーンに合う triplet を指定してください。

```bat
set VCPKG_ROOT=C:\path\to\vcpkg
%VCPKG_ROOT%\vcpkg install assimp:x64-mingw-dynamic
```

ビルドスクリプトは `VCPKG_ROOT` が設定されていれば vcpkg を使います。`VCPKG_DEFAULT_TRIPLET` の既定値は `x64-mingw-dynamic` です。MSVC では `x64-windows` など適切な値を選んでください。`install_Assimp.bat` は MinGW 向けの補助スクリプトで、ローカルの vcpkg パスへの調整が必要な場合があります。マシン固有のパスはコミットから除外します。

## ビルドとテスト

Debug ビルド：

```bat
.\build_dev.bat
```

Release ビルド：

```bat
.\build_release.bat
```

### ビルド完了通知

`build_dev.bat` と `build_release.bat` は、構成、コンパイル、実行時ファイルの確認、ライセンス表記のコピーを終えた後に通知します。

Windows のトースト通知を使う場合は、Windows PowerShell（`powershell.exe`）を開き、任意の追加モジュール [`BurntToast`](https://www.powershellgallery.com/packages/BurntToast) を現在のユーザーにインストールします。

```powershell
Install-Module -Name BurntToast -Scope CurrentUser
Get-Module -ListAvailable -Name BurntToast
```

スクリプトは `powershell.exe` からモジュールを検出し、`New-BurntToastNotification -Text 'Build finished'` を呼びます。その PowerShell 環境から利用できることを確認してください。

`BurntToast` がない場合は、Windows の [`msg.exe`](https://learn.microsoft.com/windows-server/administration/windows-commands/msg) で `%USERNAME%` に `build finished` を最大 10 秒間表示します。

登録済みの Debug テストを実行します。

```bat
ctest --test-dir build --output-on-failure
```

厳格な検証では、Debug ディレクトリを明示的に構成します。

```bat
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DKOMAPEDIT_STRICT_WARNINGS=ON -DBUILD_TESTING=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

`KOMAPEDIT_STRICT_WARNINGS` の既定値は無効です。CTest には `multilanguage_contract`、`typed_snapshot_contract`、`maploader_gradient_projection_contract`、`typed_edit_contract`、`maploader_diagnostics_contract`、`canvas3d_camera_contract`、`route_value_sampling_contract` の 7 契約テストを登録しています。

テスト実行ファイルとヘッドレス検証の実装は Debug でのみビルドします。`build_dev.bat` は `BUILD_TESTING` を有効化し、`build_release.bat` は無効化して出力先に古い `*_tests.exe` が残っていないか確認します。ヘッドレスコマンドは別途実行します。診断の契約テストには `tests/` 内の Git 管理対象外のローカルフィクスチャが必要なため、事前に確認してください。

`build\bin\typed_snapshot_tests.exe slop` は、オブジェクトキーの式、疎な Section インデックス、有限の距離程、パッチプレビューの回帰テストを実行します。これらはスナップショット・編集の契約テストにも含まれます。

`build\bin\typed_snapshot_tests.exe patch-bench 7` は、100・1,000・10,000 件の決定的な更新を完全に dry run し、`plan.patch_sources` を計測します。各規模で 1 回ウォームアップした後、全サンプルと中央値・p95 を出力します。反復回数の既定値は 7、範囲は 5–50 です。このベンチマークは明示的に実行します。

実行時ファイルの配置：

- Debug は `build\`、Release は `build_release\` を使います。
- 出力先直下に `komapedit.exe`、`LICENSE`、`NOTICE`、`THIRD_PARTY_NOTICES.md` を置きます。
- `bin\` に `maploader.dll`、`model_loader.dll`、Assimp、コピーした実行時 DLL を置きます。
- `settings\` に生成される `settings.ini`、`history.ini`、`imgui.ini` を置きます。
- ビルド・配布用整理スクリプトは、出力先直下に旧配置の INI・DLL を検出すると停止し、手動での整理を案内します。

ビルドディレクトリ、取得した `third_party` ツリー、生成された設定・CSV・テスト出力、一時的な路線・マップ・モデルのフィクスチャは Git 管理から除外してください。

## ソースの構成

| 分野 | 主なファイルと役割 |
| --- | --- |
| マップの公開 ABI | `include/maploader.h`、`include/maploader_snapshot.h`：API v13・マップスナップショット v9、固定幅 POD、Scenario v2 スナップショットと v2 直接保存用下書き、編集バッチ、レポート、span、所有権、バージョン、構造体サイズ |
| マップのライフサイクル | `src/maploader/maploader.cpp`：C ABI の入口、ハンドル、再構築、処理の振り分け、ソースへのアクセス、境界でのエラー処理 |
| マップの状態 | `maploader_internal.h`：`MapContext`、解析済み行、ソース範囲、Include スタック、編集参照、レポート、計時 |
| 解析 | `maploader_core.cpp`、`maploader_parser.cpp`、`text_decoder.cpp/.h`：文、値、Include、変数、文字コード、ソース位置、一意性検証 |
| シナリオの解析・直接保存 | `scenario_route.cpp/.h`：BVE ファイル種別、公式 Scenario 全項目のスナップショット、元の書式を保つ直接保存、マップを開くための `Route` 候補解決 |
| 形状 | `maploader_geometry.cpp`：自軌道・他軌道、座標移動、曲線、勾配、配置バッファ、シーン制御点 |
| 識別とスナップショット | `maploader_identity.cpp`、`maploader_snapshot.cpp`、`maploader_semantic.cpp`：安定した ID、型付きスナップショット、リビジョン、比較、フィンガープリント |
| 編集 | `maploader_edits.cpp`：dry run、メモリ適用、直接適用、commit、reset、ソースパッチ、文字コードを保つ書き戻し、距離程変更。制限付き Include パス更新は、旧・新サブツリーを比較対象から除いて完全再解析で検証 |
| 共通の関連付け | `include/repeater_linkage.h`、`include/own_track_transition_linkage.h`：Repeater の連続区間と、曲線・勾配の移行区間の対応付け |
| 路線値の共通サンプリング | `include/route_value_sampling.h`、`src/main_window/route_value_sampling.cpp`：評価済みイベントの安定した順序、BeginTransition/Interpolate 区間の分類、省略値の継承、2D/3D 共通の端点距離程 |
| 数値の安全性・編集計時 | `include/numeric_safety.h`：GUI・シーンでの double から整数への変換の有限性・範囲検証。`include/operation_timing.h`：GUI と maploader を分けた、任意に有効化する steady-clock の内包時間計測 |
| モデル読み込み | `src/model_loader/model_loader.cpp`、`include/model_loader.h`：Assimp の分離とモデルローダー API v2 |
| メインウィンドウ | `gui_kme.cpp`、`kme.h`、`src/main_window/` の機能別モジュール：App 状態、Win32/D3D11 初期化、共通処理・背景画像、スナップショットの GUI データへの展開・読み込み、編集・距離程・Inspector・リスト下書き・新規ファイル・要素追加、ダイアログ・UI、シーン、ヘッドレス入口。新規ファイル画面は `new_element_wizard.cpp`、内容生成・ダイアログ補助は `app_dialogs.cpp`、遅延作成は `element_inspector_data.cpp`、操作契約は `headless_entrypoints.cpp` |
| 実行時環境・設定 | `app_settings.cpp/.h`、`runtime_paths.cpp/.h`、`maploader_runtime.cpp`：INI、実行ファイル基準のパス、DLL 読み込み、API の完全一致確認 |
| ソース表示ツール | `file_structure_diagram.cpp`、`text_preview.cpp`：Include グラフ、作業コピーの表示、参照先の置換・解除、距離程境界の選択 |
| Debug 検証 | `debug_headless.cpp/.h`、`headless_entrypoints.cpp`、`edit_benchmark.cpp`、`src/table/datatable_benchmark.cpp`、`touch_input.cpp/.h`：引数解析、ヘッドレス契約、実際の操作経路、編集・テーブルキャッシュのベンチマーク、カメラ転送、検索、タッチ、編集、ファイル作成の検証 |
| 2D ビュー | `src/canvas2d/canvas2D.cpp`：平面図・縦断面データ、`Curve.Interpolate` 端点マーカーのキャッシュ構築、平面描画。`canvas2d_view_state.cpp/.h`：移動・拡大縮小・回転・座標変換。`canvas2d_marker_cache.cpp/.h`：軌道サンプリングとマーカー・Repeater キャッシュ。`canvas2d_interaction.cpp/.h`：計測、ヒット判定、右クリック対象・操作、ソース対応。`canvas2d_background.cpp/.h`：画像座標、描画、2 点位置合わせ。`canvas2d_primitives.cpp/.h`：画面変換、クリッピング付き折れ線、グリッド、スケール、マーカー。`profile_plots.cpp`：標高・半径グラフ |
| 3D ビュー | `include/canvas3D.h` と `src/canvas3d/canvas3D.cpp`：公開インターフェースと処理の委譲。非公開の `canvas3d_impl.h` と機能別 `canvas3d_*.cpp` は後述。`scene_track_sampling.cpp/.h`、`scene_route_overlay.cpp/.h`：CPU サンプリングと路線情報の純粋な整形処理 |
| テーブル・位置移動 | `src/table/datatable*.cpp`、`datatable_internal.h`、`table_navigation.cpp`：共通セル・列、統一したキャッシュ構築、検索、リソースのセル内編集、路線・効果・Scenario 画面、Debug ベンチマーク、行・平面図・シーン間の移動 |
| 共通マーカー | `include/map_marker_visuals.h`、`map_marker_visuals.cpp`：2D/3D 共通の外観定義 |
| 多言語化 | `include/multilanguage.h`：簡体字中国語・繁体字中国語（台湾）・英語・日本語の UI テキスト |

ソース所有権、関連付け、マーカー、位置移動、解析、検証、書き戻しは、担当コンポーネントと共通実装の境界を保って扱います。

## コードの詳細

`include/` と `src/` の主な役割を説明します。GUI は 2 つの DLL を C ABI 経由で呼び出します。ソースの識別情報はパーサーが所有し、スナップショットは ABI 用のデータビューを提供します。編集処理はソースパッチを生成・検証・保存します。

### 公開 ABI・共通アルゴリズム・リソースヘッダー

#### `include/maploader.h`

- **エクスポートとログ**：`KV_API` は DLL の export/import を制御します。`KvLogCallback` と `kv_set_log_callback()` は英語の診断をホストへ送ります。`kv_api_version()` の ABI バージョンは EXE と完全一致させます。
- **Scenario インターフェース**：`kv_probe_file_kind()` は Map と Scenario を判別します。`kv_resolve_scenario_routes()` は実在する参照先の Route 候補を返し、`kv_free_scenario_candidates()` で解放します。`kv_load_scenario_snapshot()` は独立した v2 スナップショットを返し、`kv_free_scenario_snapshot()` で解放します。v2 の `kv_save_scenario_document()` はソースハッシュを確認し、完全再解析後に元の文字コードでトランザクション書き込みします。既存の Route/Vehicle 候補の件数・順序の変更に対応します。
- **ハンドルと形状**：`kv_load_map_ex()` は `KV_LOAD_PREVIEW`・`KV_LOAD_EDIT_METADATA` でメタデータの範囲を選びます。`kv_generate_geometry()` と `kv_generate_scene_geometry()` は通常用・シーン用の軌道データを生成し、ハンドル内のキャッシュと各リビジョンを更新します。
- **読み取り専用スナップショット**：`kv_get_map_snapshot()` と `kv_get_scene_geometry_snapshot()` はバージョン・構造体サイズを確認し、ハンドル所有のビューを返します。呼び出し側は、再解析や対象形状の無効化までに必要なデータをコピーします。内部の入れ子データもハンドルが管理します。
- **編集とソースアクセス**：`kv_get_edit_target_typed()` は安定した編集 ID から項目とソース情報を取得し、`kv_get_source_text()` はディスクまたはメモリ上の上書きからデコード済みテキストを返します。`kv_edit_dry_run_typed()`、`kv_edit_apply_to_memory_typed()`、`kv_edit_apply_typed()`、`kv_edit_commit_typed()`、`kv_edit_reset_memory()` は順に、検証、作業コピーへの適用、直接書き込み、作業コピーの保存、上書きの破棄を担当します。
- **エラーと解放**：`kv_get_last_error()` はスレッドローカルのエラー文字列を返します。`kv_free()` はマップハンドル、`kv_free_string()` は DLL が個別に確保した文字列を解放します。

#### `include/maploader_snapshot.h`

- **バージョンと共通ビュー**：API・スナップショットのバージョン、`KV_INDEX_NONE`、機能ビット、編集フラグを定義します。`KvUtf8View` は呼び出し中のみ有効な UTF-8 入力、`KvStringRef`・`KvSpan`・`KvDoubleBuffer` はスナップショットのアリーナ・配列を指す固定幅のビューです。
- **Scenario スナップショット**：`KvScenarioPathWeightRow` は相対パス、重み、明示的な重みの有無を保持します。v2 の `KvScenarioSnapshot` はソースハッシュと 8 項目の存在ビットを持ちます。`KvScenarioEditDocument`・`KvScenarioEditPathRow` は `kv_save_scenario_document()` の呼び出し中に有効な入力です。
- **値とソース識別**：`KvValueKind`・`KvValue` は null、数値、文字列、continue を表します。`KvSourceFileRow`、`KvSourceSpanRow`、`KvStatementRow`、`KvElementRow`、`KvRowMetadata` は実ファイル、Include スタック、バイト・行・列の範囲、元の引数、解析順、安定した編集 ID を保持します。
- **型付き行**：`KvTrack`、`KvStation*`、`KvStructure*`、`KvRepeater*`、`KvSignal*`、`KvSection*`、`KvSound*`、`KvOtherTrain*` と、曲線・勾配・速度制限・環境効果の POD が行の項目を定義します。可変長引数は `KvSpan` で共通の値配列を参照します。
- **ルートスナップショット**：`KvMapSnapshot` は文字列アリーナ、値、型付き行配列、形状行列、ソース登録情報、機能ビット、内容・形状のリビジョンをまとめます。`KvSceneGeometrySnapshot` はシーン制御点と軌道行列を持ち、独立した無効化周期で管理します。
- **編集プロトコル**：`KvEditField`・`KvEditTargetSnapshot` は編集可能な項目、`KvEditOperation`・`KvEditChange`・`KvEditBatch` は挿入・更新・削除要求を表します。距離程解決、パッチプレビュー、保存済みファイル・行、`KvEditReportSnapshot` は検証・保存結果を表します。要求とスナップショットは公開ヘッダーに従ってバージョン・構造体サイズを確認し、項目変更時は EXE/DLL を同時に更新します。

#### `include/model_loader.h`

- `MlVertex` は位置・法線・UV、`MlMaterial` は拡散色・テクスチャパス、`MlMeshPart` はインデックス範囲とマテリアルの対応を保持します。`MlMeshData` は頂点、インデックス、マテリアル、パーツ、境界・中心・半径をまとめます。`ml_api_version()` は v2 を返し、EXE が完全一致を確認します。
- C ABI は `ml_api_version()`、`ml_load_model()`、`ml_free_model()`、`ml_get_last_error()` の 4 関数です。すべての配列を DLL が確保し、成功時も途中失敗時も `ml_free_model()` で解放します。

#### `include/repeater_linkage.h`

- `EventKind` は `Begin`・`Begin0` を Begin に分類し、End とその他のイベントを区別します。`BoundaryKind` は明示的 End、後続 Begin、開いた終端を区別します。`Event` は距離程、全体解析順、Repeater キー、ソース行インデックスを保持します。
- `canonical_key()` は大文字・小文字を区別しない比較を統一します。`pair_linkage()` は距離程・全体解析順・行インデックスで安定ソートし、キーごとの有効な連続区間を管理して `Chain`・`Segment` を生成します。新しい Begin は前区間を閉じ、End は連続区間全体を閉じます。同距離程では最後に解析されたイベントが有効になります。
- `pair_segments()` はスナップショット、テーブル、2D、3D で共有する平坦な区間リストを提供します。

#### `include/own_track_transition_linkage.h`

- `EventKind` は曲線・勾配の `BeginTransition` と、その移行区間を受け取る Begin/End を表します。`Pair` は開始点と対応先の行インデックスを保持します。
- `consumes_curve_transition()`・`consumes_gradient_transition()` は、待機中の移行区間を受け取る文を定義します。`pair_transitions()` はソース順に曲線・勾配の待機状態を別々に追い、対と孤立点を返します。編集・マーカー処理はこの結果で対応を結びます。

#### `include/map_marker_visuals.h`

- `MapMarkerVisualKind` は 2D/3D 共通のマーカー種類を定義し、`map_marker_visual_bit()` が表示ビットに対応付けます。
- `MapMarkerPrimitiveKind`、`MapMarkerColorRole`、`MapMarkerIconVariant` は図形要素・色の役割・形状の種類を定義します。`MapMarkerIconPrimitive`・`MapMarkerIconRecipe` は正規化した点、線幅、閉曲線・塗りつぶしフラグ、字形を保持します。
- `map_marker_theme_color()`、`map_marker_role_color()`、`map_marker_icon_recipe()`、`draw_map_marker_icon()` は、2D/3D の色、アイコン定義、ImDrawList インターフェースを統一します。

#### `include/route_value_sampling.h` と `src/main_window/route_value_sampling.cpp`

- `Event` は maploader が評価した路線値と種類を、距離程の安定した順序で保持します。`append_event()` は通常値、`BeginTransition`、`Interpolate` を識別し、引数のない Interpolate は直前の値を継承します。
- `sample()` は 2D/3D で共有します。一定区間では現在値、移行・補間区間では両端の距離程と値を返します。

#### `include/numeric_safety.h` と `include/operation_timing.h`

- `kme::truncating_int_or_zero()` は `int` の範囲内の有限な double を切り捨て変換し、非有限値・範囲外の値には `0` を返します。GUI とシーンで共通使用します。
- `kme::timing::Timing` は、明示的に有効化するスレッドローカルの段階別計時です。子処理の時間も含め、GUI と maploader を別の領域で計測し、ミリ秒と回数をコンソール診断用に集計します。

#### `include/canvas3D.h`

- **シーン入力**：`Canvas3DTrackPoint/Path/Visibility` は軌道サンプルと表示状態、`Canvas3DSceneObject`・`Canvas3DModelInstance`・`Canvas3DRepeaterSegment` と背景・霧・描画距離イベントはシーンへの入力を表します。
- **路線情報とマーカー**：`route_value_sampling::Event`、停車場、速度制限、Section 信号イベントからカメラ位置の値を取得します。`Canvas3DSceneMarker` は外観種類、距離程、軌道位置、テーブル移動先、編集 ID を持ちます。`Canvas3DSceneMarkerVisibility` は種類別ビットでインデックスの再構築を制御します。
- **構築・更新用構造体**：`Canvas3DScene` はレンダラーに依存しない CPU 上のシーン記述です。`Canvas3DSceneBuildOptions/Result` と `Canvas3DSceneMapRefreshOptions` は、初回構築、動的内容の更新、マップ内容の更新を区別します。`Canvas3DSceneStats` はインスタンス数・モデル数・フレームレートを公開します。
- **操作用構造体**：カメラ姿勢、右クリック操作、選択対象、`Canvas3DPlacementEditTarget`、ドラッグ軸、`Canvas3DPlacementDragUpdate` が、描画側の操作を GUI で適用できるソース項目更新に変換します。
- **`Canvas3D` の公開窓口**：単体モデルとシーンの読み込み・更新、表示切替、描画距離、霧、ギズモ、カメラ、性能警告、デバッグを提供し、実装は非公開の PImpl に委譲します。

#### `include/multilanguage.h`

- `Language` は日本語・英語・簡体字中国語・繁体字中国語（台湾）を選び、`Translation` の 4 言語分のフィールドは同じ意味の UI テキストを保持します。`Language::ZhTw` は `zh_tw` を選びます。定義はウィンドウ、メニュー、ツールバー、テーブル、プロパティ編集、エラー、2D/3D 操作ごとにまとめています。
- `tr()` と言語選択の補助関数は、選択中の言語のフィールドを返します。`Translation` の同じ初期化子に 4 言語を追加し、書式のプレースホルダーをそろえます。繁体字中国語は台湾の用語を使い、テーブルの行は「列」、列は「欄」と表記します。
- 既存の `[General] language` 設定には `ja`、`en`、`zh`、`zh-TW` を保存し、既定値は簡体字中国語のままです。`中文` サブメニュー内に `简体` と `台湾繁體` を置き、サブメニュー自体は英語・日本語の項目と同じ階層に並べます。
- `multilanguage_contract` はキー集合、空でない値、プレースホルダーの名前と出現回数、言語選択、重要用語を検証します。フォントは中国語の全グリフ範囲を維持し、既存の CJK 候補の後、Segoe UI の前に Microsoft JhengHei（`msjh.ttc`）を加えます。

#### `include/resource.h`

- Windows リソースのコンパイルと C++ でリソース ID を共有します。`IDI_KOMAPEDIT` は `komapedit.rc` のアプリアイコンを識別します。

### maploader の状態・解析・スナップショット

#### `src/maploader/maploader_internal.h`

- **共通処理と計時**：文字列ヘルパー、`SteadyClock`、`LoadTiming`、`ScopedTimer`、`ActiveTimingScope` が読み込み・解析・結合・形状生成・スナップショット生成を計測します。タスクスロットの宣言は負荷の高い読み込みの同時実行数を制限します。
- **デコードと解析設定**：`LoadedText` は生バイト、UTF-8 テキスト、文字コード、BOM、改行、行頭位置を保持します。`MapParseOptions` はプレビュー・編集メタデータの範囲を選び、`SourceTextOverride(s)` は作業コピーの上書きを保持します。
- **式の値**：`ValueKind`、`Value`、`VariableEnvironment` は解析時の null・数値・文字列と変数束縛を表します。共有する不変の環境スナップショットを文に付け、距離程移動時の意味検証に使います。
- **ソースモデル**：`SourceFileRecord`、`FileStructureRecord`、`SourceSpan`、`ParsedStatement`、`EditSourceRef`、`MapDiagnostic` は実ファイル、Include 呼び出しの識別、元の文、行・列・バイト位置、解析順、編集 ID を保持します。
- **解析済み行**：`CurveEditRow`、`GradientEditRow`、`OtherTrackChange` と、Station・Structure・Repeater・Signal・Section・Sound・Train・速度制限・効果の各レコードが、形状・スナップショット・編集へ型付きデータを提供します。各行の `EditSourceRef` が書き戻し先を示します。
- **行列とスナップショットの格納**：`Matrix` は連続した double の行列バッファを管理します。`MapSnapshotStorage`、`SceneGeometrySnapshotStorage`、`EditTargetSnapshotStorage`、`EditReportSnapshotStorage` は ABI ポインターの参照先となる vector・文字列アリーナを所有します。
- **`MapContext`**：メインパス、ソース・Include 表、変数環境、全解析行、軌道・シーン行列、制御点、リビジョン、スナップショットキャッシュ、作業コピーの上書き、ディスク基準ハッシュ、最新編集レポート、計時を所有します。解析・形状・スナップショット・編集の所有権の起点です。
- **処理中の文と編集の RAII**：`ActiveStatementScope` は振り分け中の文・ソース環境を設定し、終了時に戻します。`MapEditChange` と専用フィールド構造体は ABI 要求のコピーを保持します。意味スナップショット、距離程解決、パッチ・保存レポートがトランザクション検証を支えます。
- **内部宣言**：末尾に DLL 内部の解析、形状、スナップショット、意味検証、編集、識別の入口を宣言します。

#### `src/maploader/text_decoder.h`

- UTF-8 と `std::filesystem::path` の変換、バイナリ読み込み、UTF-16・コードページのデコード、BOM・先頭行の判別、文字コードに応じた書き戻しを宣言します。
- `FileOpenFailureKind` は、ファイル不在、権限不足、ディレクトリ、その他の読み込み失敗を区別し、パーサーとモデルローダーの診断を統一します。書き戻しは文字コードと BOM を受け取り、表現できない文字があれば失敗します。

#### `src/maploader/text_decoder.cpp`

- `classify_file_open_failure()`、`file_open_failure_message()`、`read_binary_file()` は読み込みと Windows エラーの分類を担当します。`path_to_utf8()`、`utf8_to_wide()`、`wide_to_utf8()`、`path_from_utf8()`、`join_utf8_path()` は Win32 のワイド文字パス処理を集約します。
- `decode_codepage()` は Windows の厳密・許容的なコードページ変換を使い、他環境では限定的な代替処理を行います。`append_utf8_codepoint()` と `decode_utf16()` はバイト順、サロゲートペア、不正な並びを扱います。
- `decode_text_bytes()` は宣言された文字コードと BOM から UTF-8・UTF-16・CP932 を選びます。`first_line_ascii()` と `has_utf8_bom()` は全体のデコード前にヘッダーを識別します。
- `append_utf16_bytes()` と `encode_text_for_writeback()` は UTF-8 の作業コピーを元の文字コードへ戻し、BOM を維持します。変換できない文字には例外を送出します。

#### `src/maploader/maploader_core.cpp`

- **タスク・計時・スカラー値**：`try_acquire_maploader_task_slot()`・`release_maploader_task_slot()` は同時読み込み数、`ActiveTimingScope` は全体の処理時間を管理します。`ascii_lower()`、空白除去、`parse_finite_number()`、`canonical_number()`、バージョン・文字コードヘッダーの解析が共通規則を提供します。
- **テキスト読み込み**：`build_line_starts()`、`detect_newline()`、`make_loaded_header_text()`、2 つの `load_header_text()` は、生バイトを文字コード・改行・本文位置付きの `LoadedText` に変換します。メモリ上の上書きを優先します。
- **値とキーの変換**：`as_number()`、`as_text()`、`key_text()`、`track_key_display_text()`、`track_key_from_display_text()`、CSV/INI ヘルパーが解析値・テーブルの意味・編集文字列を統一します。
- **ソース登録と位置**：`normalized_source_path/key()`、`current_source_text()`、`register_source_file_index()`、Include スタック・呼び出しキーのインターン処理が識別情報の重複を除きます。`line_column_for_body_pos()` と `make_source_span()` は本文オフセットを実ファイルの位置へ変換します。
- **文と環境**：`current_variable_environment_snapshot()`、`rebuild_variable_environment_snapshot()`、`add_parsed_statement()`、`next_active_edit_ref()` が文・環境・編集参照を登録します。結合・オフセット補助処理はインデックスを保って Include コンテキストを統合します。
- **リスト行のソース情報**：`add_loaded_line_statement()` と `extend_loaded_line_statement()` は実際の CSV・リスト行を編集可能な文として登録します。`parse_signal_aspect_source_values()` とフィールド名ヘルパーが可変のキー列数と glare 行の形を保持します。
- **依存関係と状態更新**：`value_equal()`、変数の読み書き記録、`log_load_timing()` は意味検証と性能ログに使います。`add_controlpoint()`、`set_distance()`、`put_own()`、`ensure_othertrack()`、`put_other()` は、軌道イベント状態を更新する共通の解析入口です。

#### `src/maploader/maploader_parser.cpp`

- **字句解析と文のループ**：`Parser::parse()` がファイルを処理し、`eof()`、`peek()`、`skip()`、`accept()`、`expect()` が空白・コメント・区切りを扱います。診断に位置を保持し、`finish_statement()`・`synchronize_statement()` で次の文から処理を再開します。
- **オブジェクト・関数・式**：`parse_label()`、`parse_variable_name()`、`parse_map_object()`、`parse_map_function()`、`parse_map_args()` が `MapObject`・`MapFunction` を構築します。`parse_expression()`、`parse_prefix()`、`parse_primary()`、`apply_binary()`、`call_function()` が優先順位、変数、文字列、数値、対応する数学関数を実装します。
- **Include**：`include_path_is_simple_string()` はプレビュー用パスを確認します。`make_child_seed()` は通常変数と Include 識別を継承し、子の距離程を 0、距離式を空で開始します。`parse_include_context()` は並列解析に対応し、`queue_include()`・`flush_pending_includes()` はソース・診断・イベント・変数書き込みを元の順序で結合します。親の距離程を維持し、変数依存が古くなった場合は再解析します。乱数器はプロセスセッションの seed、ソースパス、字句上の Include 順から生成し、再解析でも予約時の seed を維持します。これにより、Preview/Edit 読み込みと作業コピー再解析で、変更していない `rand()` 呼び出しを再現します。
- **構文検証と振り分け**：`method_rules()` は引数個数・null 規則、`object_path()`・`validate_statement()` は一般構文を検証し、`dispatch()` が種類別ハンドラーへ渡します。`record_deferred_semantics()` はリスト読み込み後に検証するキーを記録します。
- **自軌道・他軌道**：`dispatch_curve()`、`dispatch_gradient()`、`dispatch_legacy()` は曲線・勾配・旧形式のイベントを記録します。`dispatch_track()`、`setposition_interpolate()`、`track_position()` は他軌道の位置・補間・軌間・中心・カントを記録します。
- **リソースリスト**：`load_resource_list()` と `record_resource_list_load()` は Load の式、評価済みパス、ソース識別を保持します。`parse_station_list()`、`parse_structure_list()`、`parse_signal_aspect_list()`、`parse_sound_list()` は実ファイルの行を型付き編集レコードに変換します。`parse_other_train_file()` は他列車ファイルを読み込みます。
- **マップ要素の振り分け**：`dispatch_station/speedlimit/section/signal/beacon/pretrain/structure/sound/train/repeater/irregularity/background/adhesion/cab_illuminance/fog/draw_distance()` と 3 種類の noise ハンドラーがメソッド形式を確認し、引数を読んで行を追加します。`add_other_train_definition()` は Train 定義を登録します。
- **解析後の診断**：`validate_unique_preview_statements()` は Load/Enable の重複、`append_transition_diagnostics()` は対の関係、`append_deferred_key_diagnostics()` はリソースキーを確認します。停車場で到着・出発音を使う場合、`append_station_sound_load_order_diagnostic()` は `Sound.Load` が `Station.Load` より前か確認します。同一ファイルは行・列、別ファイルは Include 深度、同じ深度は全体解析順で比較し、逆順なら英語の `[WARN]` を出します。`emit_diagnostics()` は診断を送信し、エラーを読み込み失敗として扱います。
- **モジュールの入口**：`parse_map_context()` が `MapContext` を作り、メインファイルを読み込んで Parser を実行し、環境・診断を更新して返します。すべての読み込みと編集後の再解析で共有します。

#### `src/maploader/maploader_geometry.cpp`

- **軌道状態機械**：`LastPos` は前のサンプルを保持します。`TrackPointer` は距離程に沿って自軌道イベントを進め、通常サンプルとイベント境界で半径、勾配、カント、向き、座標を提供します。
- **曲線計算**：`rotate_xy()`、ガウス積分、フレネル級数・漸近式で局所座標を積分します。`circular_curve*()` は円曲線、`halfsin_intermediate()`、`linear_transition_curve_local()`、`transition_curve*()` はサイン半波長・直線逓減の緩和曲線を計算します。キー・ハッシュ構造で同じ曲線パラメーターの計算をキャッシュします。
- **勾配の投影**：`constant_gradient_projection()`、`sinc()`、`gradient_transition()` は路線長から水平投影距離と標高を求めます。`build_gradient_projection_samples()` と `build_event_projected_distances()` はイベント距離程を平面位置へ対応付けます。
- **自軌道生成**：`sorted_unique()` と `append_arange()` はイベント点と一定間隔の点を統合します。`generate_owntrack()` は距離程、XYZ、向き、半径、勾配、カントなどの列を出力し、`generate_curveradius()` は半径図のデータを作ります。
- **他軌道生成**：`relative_position()` は曲線に対する相対位置、`CantProcessor` はカントの Begin/End/Interpolate を扱います。`build_othertrack_buffer()` は位置、X/Y 補間、軌間、中心、カントを、自軌道に整列した行列と有効性データにまとめます。
- **座標移動と配置**：`relocate()` は形状を安定した局所座標へ平行移動します。`build_structure_put_buffer()` は距離程でサンプリングした配置用変換を提供します。
- **適応的なシーン制御点**：角度・行列ヘルパーと `build_scene_adaptive_controlpoints()` は、イベント、モデルの span、曲率、勾配、指定範囲に応じた密度で制御点を作ります。
- **入口**：`generate_geometry()` は自軌道、半径図、他軌道、配置バッファ、シーン制御点を生成し、計時、機能ビット、スナップショットのリビジョンを更新します。

#### `src/maploader/maploader_identity.cpp`

- `stable_hash64()` は決定的な 64 bit ハッシュ、`hex64()` は固定長の 16 進表記、`edit_kind_token()` は正規化した行種別を提供します。
- `make_edit_id()` は正規化ソースキー、全体解析順、文の種類、局所的な順番から安定した ID を作ります。`statement_edit_id()` は文 ID をキャッシュし、`native_element_edit_id()` と `element_edit_id()` はネイティブ行と派生要素の共通入口です。

#### `src/maploader/maploader_snapshot.cpp`

- `matrix_view()` と `data_or_null()` は連続した内部コンテナーを ABI ビューにします。
- `MapSnapshotBuilder::build()` はルート情報、軌道、停車場、ストラクチャー、他列車、閉そく・信号・サウンド、効果、作者メッセージ、プレビュー行、編集登録情報を追加し、`finalize()` でスナップショットを確定します。
- `string_ref()` は共通アリーナ内の文字列を再利用し、新規テキストを追加します。`value()`・`append_values()`・`append_strings()` は値と span、`metadata()` は `EditSourceRef` から `KvRowMetadata` を作ります。各 `add_*` は型付き項目をコピーし、機能ビットに応じたソース情報を添えます。
- `add_element(s)()` は行種別・編集 ID と行インデックスの対応を登録し、`bind()` は vector の拡張後にポインターを結びます。`finalize()` はバージョン、構造体サイズ、件数、リビジョン、機能ビットを書き込みます。
- `invalidate_map_snapshot()` と `invalidate_scene_geometry_snapshot()` は内容・通常形状・シーン形状の無効化境界を定めます。`build_map_snapshot()` と `build_scene_geometry_snapshot()` は必要時に再構築し、`ordered_station_list_entries()` は停車場リストの実ファイル上の順序を維持します。

#### `src/maploader/maploader_semantic.cpp`

- `SemanticWriter` は型と値を固定順で書きながらハッシュを計算します。`field()`、`value_span()`、`begin_element()`、`emit_element()` は正規化した意味表現を作ります。
- `changed_field()` と数値・文字列・値・軌道キーの読み取り関数は、スナップショットに `MapEditChange` を重ね、不正な数値や必須値の欠落を拒否します。
- `write_structure_model()`、`write_sound_list()`、`write_structure_put()`、`write_structure_between()`、`write_station_put/list()`、`write_signal_aspect/put()`、`write_repeater()` と、地上子・サウンド・noise・背景・粘着・霧の `write_*` が、種類ごとに維持・変更すべき項目を定めます。
- `write_curve()`、`write_gradient()`、`write_other_track_change()` はメソッド、引数個数、対応関係を保持します。`write_section_row()` は可変長の値一覧を扱い、`reject_unknown_target_fields()` は未宣言の項目を拒否します。
- `build_semantic_map_snapshot()` は保護対象を走査し、編集 ID と意味表現の対応、マップ・環境のフィンガープリントを作ります。`expected_target_semantic()` は更新・削除の期待値、`FakeInsertSnapshotState`、`insert_semantic_container()`、`expected_insert_semantic()` は挿入の期待値を作ります。

#### `src/maploader/maploader_edits.cpp`

- **ABI 入力のコピー**：`copy_utf8_view()` と `copy_edit_batch()` は構造体サイズ、ポインターと長さ、操作、フラグ、項目の重複、UTF-8 ビューの寿命を検証し、呼び出し中のみ有効な POD を内部の `MapEditChange` にコピーします。
- **パッチ位置**：`load_source_patch()` が作業コピーを読み、`source_range_in_text()`、`safe_statement_removal_range()`、プレビューヘルパーが、隣接するコメント・文を保って `SourceSpan` を UTF-8 テキスト範囲へ変換します。
- **距離式**：走査処理は定義済みの `distance`、最上位の加減算、安全に変更できる数値項を識別します。`find_safe_numeric_distance_addend()`、`apply_delta_to_distance_addend()`、`adjust_distance_expression_by_delta()` は変数式を保ち、可能なら定数だけを変更します。それ以外は距離程解決を要求します。
- **引数と CSV**：BVE 引数の分割・引用、数値・省略値・値・キーのヘルパーは未変更の生引数を保持します。CSV 解析、等価性検証、`build_editable_csv_list_statement()` は区切り、末尾フィールド、文字コードでの表現可能性を維持します。
- **文の生成**：`build_structure_model/sound_list/station_list/signal_aspect_statement()` はリスト行、`build_station_put/structure_put/signal_put/repeater_statement()` は明示的なメソッド・引数形式の変換を扱います。Structure/Repeater の通常形とオフセットなし形式の相互変換も含みます。他の `build_*` は曲線、勾配、他軌道、Section、速度制限、地上子、サウンド・noise、効果を、メソッドと引数形式を維持して生成します。
- **対象と対象スナップショット**：テンプレートの `match_edit_ref()`、`find_simple_target()`、`find_editable_target()` は MapContext の型付き行から編集 ID を探します。`build_edit_target_snapshot()` は項目、元の値、生引数、制約、sourceHash、expectedSourceHash を出力します。
- **挿入検証**：`validate_insert_field_names()`、`validate_insert_method()`、`validate_insert_change()` は行種別、メソッド、構造化項目を確認し、`build_insert_statement()` が通常の BVE 文を生成します。
- **距離程区間の配置計画**：`DistanceSectionAnalysis/PlanningIndex` は実ファイル、Include 呼び出し、距離程区間を索引化し、先頭ブロック、隙間、最終ブロック、EOF への配置を計画します。明確な最終区間内の移動はその方向へ延長できます。挿入は既存の一意な位置を優先し、曖昧な場合は候補をユーザーに示します。候補絞り込みは変数・実ファイル出現単位の検証を共有し、選択後は適用・書き込み前に完全な意味検証を行います。
- **レポートとトランザクション書き込み**：`build_edit_report_snapshot()` はパッチ・解決・保存結果を ABI に展開します。ハッシュ・一時ファイルのヘルパーは対象と同じディレクトリに準備用ファイルを作ります。`replace_files_transactionally()` は段階的に置換し、失敗時に戻します。`TransactionalWriteError` は元のエラーとロールバックのエラーを保持します。
- **完全な意味検証**：`parse_report_candidate()` はパッチ上書きで再解析します。`validate_non_target_derived_state()`、`own_track_transition_state()`、`validate_edit_report()` は非対象要素、最終変数束縛、停車場の所有関係、移行区間の対応、各対象の期待する意味を比較します。有効な編集では、最終的な現在の `distance` は変わり得ます。
- **バッチ処理**：`build_edit_report()` は対象の準備、実ファイルのコンテキスト・対象距離程のグループ化、境界解決、更新・削除・挿入の生成、重複検証、再解析、意味検証を行います。dry run、メモリ適用、直接適用で共有します。
- **作業コピーと保存**：`apply_patched_files_to_overrides()`、`reparse_context_with_overrides()`、`apply_edit_report_to_memory()` はディスク基準を保って上書きを更新します。`reset_memory_edits()` はディスク状態に戻し、`populate_committed_edit_state()` は保存済みの識別情報を記録します。`commit_memory_edits()` は全上書きを再検証し、トランザクションで書き込みます。

#### `src/maploader/maploader.cpp`

- `parse_options_from_load_flags()` は公開フラグを内部プロファイルへ変換し、不明な組み合わせを拒否します。
- `kv_*` のエクスポートは、ハンドル、バージョン、構造体サイズ、引数を検証して実装を呼びます。C ABI 境界で例外を捕捉し、最終エラーを設定して失敗値を返します。
- `kv_load_map_ex()` は `MapContext` を生成します。2 つの形状入口は行列・リビジョンを更新し、2 つのスナップショット入口はキャッシュ済みビュー、編集対象・ソーステキストの入口は作業コピー情報を返します。
- Scenario のエクスポートは `probe_bve_file_kind()`、`load_scenario_document()`、`resolve_scenario_route_candidates()` を呼びます。独立したスナップショット・候補ブロックの確保と対応する解放を DLL 内で管理します。
- dry run はレポートを生成し、メモリ適用は上書きを生成・設定、reset は上書きを破棄、直接適用はレポートをトランザクションで書き込み、commit は検証済み作業コピーを保存します。`kv_free()`・`kv_free_string()` は DLL 内の確保に対応します。

#### `src/maploader/scenario_route.h` と `src/maploader/scenario_route.cpp`

- `probe_bve_file_kind()` は先頭を読み、Map・Scenario・Unknown を判別します。読めないファイルは Unknown とし、通常の読み込み経路で詳細エラーを報告します。
- `load_scenario_document()` は宣言された文字コードで `BveTs Scenario 2.00` を解析し、`#`・`;` コメントと、重複項目の後勝ちを扱います。公式 8 項目、ソースハッシュ・存在ビット、ソース基準の Route/Vehicle パス、重み、その明示状態を保持します。
- `save_scenario_document()` はディスクを読み直し、期待するソースハッシュを確認します。候補数が同じなら最後に有効な項目内を最小限変更し、候補数が変わるならその項目の候補値全体を書き換えます。空パス、予約構文文字、0 以下・非有限の重みを拒否し、既存のパス項目には最低 1 候補を要求します。その後、完全再解析と共通のトランザクション書き戻しを行います。
- `resolve_scenario_route_candidates()` は解析済み Scenario を再利用し、そのディレクトリを基準に Route パスを解決して参照先の存在を確認します。スナップショットの読み込みでは宣言されたデータを保持します。

#### `src/maploader/diagnostics.h` と `src/maploader/diagnostics.cpp`

- ヘッダーはログコールバック、最終エラー、info/warn/error のインターフェースを宣言します。コールバックは atomic に公開し、各呼び出しスレッドの最終エラーは `thread_local` に保持します。
- `emit_log()` は完全なログ行を送り、`log_info_at()`、`log_warn_at()`、`log_error_at()` は重大度とソース名を付けます。ABI の catch 節は `set_last_error()` で取得可能なエラーを保存します。

#### `src/maploader/c_api.h` と `src/maploader/c_api.cpp`

- DLL 内部の補助層として C 文字列の所有権を扱います。`copy_c_string()` は `std::malloc()` で NUL 終端 UTF-8 を ABI 用にコピーし、`kv_free_string()` が対応する `std::free()` で解放します。

### モデル読み込み

#### `src/model_loader/model_loader.cpp`

- パス・拡張子ヘルパーは Assimp の形式ヒントを正規化します。`copy_c_string()` はマテリアルのテクスチャパスを ABI 文字列として確保し、`resolved_texture_path()` はモデル相対のパスを UTF-8 へ解決します。
- `free_mesh()` は頂点、インデックス、マテリアル文字列、メッシュパーツを冪等に解放します。`MeshCleanupGuard` は例外時の後始末、`assign_bounds()` は境界・中心・半径の計算を行います。
- `load_with_assimp()` は Unicode パスに共通バイナリリーダーを使い、Assimp を呼びます。aiMesh の頂点・法線・UV・インデックスをまとめ、マテリアルとパーツを生成し、最初の拡散テクスチャを解決して境界を計算します。
- `ml_api_version()` は v2 を返します。`ml_load_model()` は出力を初期化し、例外を捕捉して `MlMeshData` を設定します。`ml_free_model()` は公開解放関数、`ml_get_last_error()` はスレッドローカルの診断を返します。

### メインウィンドウの状態・読み込み・編集

#### `src/main_window/kme.h`

- **数値処理とハッシュ**：`KmeByteHash64` は GUI キャッシュ・デバッグ用の決定的なバイトハッシュ、`Matrix` は ABI からコピーする GUI 所有の double 行列です。
- **マップモデル**：`TrackEvent`、`OwnTrackEditMarker`、`OtherTrack`、Station/SpeedLimit/Section データ、`TableRow` 群が `MapModel` を構成します。GUI が必要とするソース登録情報、リストのメタデータ、リビジョン、機能ビット、通常・シーン行列をコピーして所有します。
- **2D データ**：`View2D` は移動・縮尺・回転を保持します。`TrackPoint`、`PlanMarker` とその別名、`PlanRepeaterSegment`、`OtherTrainPathOverlay`、`PlanData`、`ProfileData` がキャンバスのキャッシュとヒット判定に使われます。
- **テーブルとソース表示**：`TableRow/ColumnDef`、`CachedTableRow`、`TableUiCache` はリビジョンに基づく表示データです。File Structure、Text Preview、DistanceResolution の構造体は配置、選択、パーサーが確認した境界を保持します。
- **設定と実行状態**：`TextureImage` は D3D 背景テクスチャを管理します。ログ、表示状態、2D/3D ビュー、`UserSettings`、最近のマップ、背景履歴は INI 項目に対応します。`ScenarioRoutePickState` は候補選択・開き方、`ScenarioPreview` とそのディスク基準は編集可能なシナリオ下書きを保持します。
- **編集状態機械**：項目制約、Inspector セッション、保留変更、プレビュースナップショット、Repeater 下書き、NewElement テンプレート・ウィザード、削除方式、距離程処理、リスト下書きが、未適用の UI 入力、適用済み作業コピー、保存済みディスク状態を区別します。
- **`App` クラス**：描画、非同期読み込み、スナップショット変換、形状・シーン再構築、テーブル・位置移動、編集・保存・再読込、ダイアログ、設定、背景、2D/3D 操作、キャッシュを宣言します。メンバー構成が GUI の各 `.cpp` で共有する状態を定義します。

#### `src/main_window/maploader_runtime.cpp`

- `KME_MAPLOADER_FUNCTIONS` はシンボル一覧です。`MaploaderRuntime` は `runtime_paths::dll_path()` から `maploader.dll` を読み、唯一の読み込み入口 `kv_load_map_ex()` を含む全関数を解決して、`kv_api_version()==KV_MAPLOADER_API_VERSION` を確認します。失敗時には Win32 エラー文字列も含めます。
- グローバルの `kv_*` 関数は `ensure_loaded()` を通じてキャッシュ済み DLL ポインターを呼びます。失敗時は失敗値と GUI から取得できる診断を返します。GUI は公開ヘッダーの関数名を使い、DLL を実行時に読み込みます。

#### `src/main_window/runtime_paths.h` と `src/main_window/runtime_paths.cpp`

- `executable_directory()` は `GetModuleFileNameW()` を 1 回呼び、EXE のディレクトリをキャッシュします。`dll_directory()` はその `bin`、`settings_directory()` は作成済みの `settings` を返します。
- `dll_path()` は DLL パスを組み立てます。`load_dll()` は検索範囲を制限して `bin` から依存 DLL を読み、必要に応じて Win32 エラーコードを返します。

#### `src/main_window/app_settings.h` と `src/main_window/app_settings.cpp`

- ヘッダーは既定の INI パス、明示パスによる内部設定読み込み、設定・履歴の入出力、ImGui レイアウトの遅延保存、実行時スタイルの適用を公開します。
- 先頭のヘルパーは保存パス、履歴キー・表示名を正規化し、文字・部品・マーカー・線幅・描画距離・ギズモ・インスタンス警告しきい値を範囲内に収めます。色のヘルパーは 16 進表記、パレット、アルファ、合成を扱います。
- 言語、bool、2D モード、グリッドモードの変換が INI の値書式を定義します。`save_user_settings()` は `General`、`WindowVisibility`、`View2D`、`View3D` に正規のキーを書きます。`load_user_settings()` は正確な節名・キー・値書式を受け付け、不明・無効な項目には既定値を使います。保存はファイル全体を書き換えます。
- `load_imgui_layout()`、`save_imgui_layout()`、`save_imgui_layout_if_requested()` と保留フラグが、`imgui.ini` の明示・遅延保存を管理します。
- `load_history_state()` は `[Recent]`・`[MapN]` の最近のマップとパス・背景の 8 項目、`[CreatorMessages]`・`[CreatorMessageN]` の作者メッセージ設定を読みます。履歴パスは正規化・重複除去し、最大 10 件を保持します。`save_history_entries()` と `save_history_state()` はロケールに依存しない数値で正規書式を書きます。
- `apply_ui_font_size()`、`apply_ui_theme_color()`、`apply_ui_component_size()`、`apply_ui_settings()` は設定を ImGui スタイルに反映し、DPI・ビューポートに合わせて角丸やサイズを調整します。

#### `src/main_window/gui_kme.cpp` と関連モジュール

- `gui_kme.cpp` は `App` の構築・破棄とログコールバック、`kme.h` は EXE の共有状態と翻訳単位間インターフェースを担当します。
- `win32_dx11_bootstrap.cpp` は D3D11 デバイスと描画先、`WndProc()`、メッセージループ、`main()` を所有します。
- `gui_common_utils.cpp` はフォント、テーマ・ログ色、文字コード変換、数値書式、パス、距離程ジャンプを集約します。`background_image.cpp` は WIC デコード、背景テクスチャ再構築、履歴への背景保存を担当します。
- `map_snapshot_hydration.cpp` は型付きスナップショットから `MapModel`、行メタデータ、Station 編集 ID、速度制限キャッシュ、移行区間の対応を構築します。`map_load_pipeline.cpp` は Map/Scenario 判別、Scenario スナップショット・下書き基準、Route 候補、非同期マップ読み込み、入口の履歴、結果適用、メタデータ結合、読み込み計時、形状再生成を扱います。
- `edit_ledger.cpp` は型付きバッチ・レポート、変更台帳の同期、局所プレビュー、削除、Save、Revert、終了処理を担当します。`distance_resolution_workflow.cpp` は距離程解決要求と Apply の再開を担当します。
- `edit_benchmark.cpp` は独立した Debug 編集ベンチマークです。実入力をバイト単位で保護してメモリ内 Apply/Delete/Revert を測り、Save は相対依存関係を保った通常ファイルのコピーを専用一時ディレクトリで使います。更新の集約、ロールバック、段階別計時の契約も確認します。
- `element_inspector_data.cpp` は Inspector の開始・対象移動、項目・シーン編集データ、Apply を管理します。`element_inspector_render.cpp` は項目、省略可能な挿入引数、可変長の Repeater/Section 操作部品を描画します。
- `editable_list_drafts.cpp` はリソースリスト下書き、`new_element_wizard.cpp` はテンプレート、要素追加、構造化挿入、新規ファイルウィザードの状態・描画を担当します。`headless_entrypoints.cpp` は実際の App 操作経路を使い、要素追加、リスト置換・挿入、新規ファイル、Scenario 作成を検証します。
- `app_dialogs.cpp` はファイル選択、Scenario Route 選択、新規 Scenario の内容、その他のモーダル画面を担当します。`element_inspector_data.cpp` は遅延した新規ファイル要求、排他的作成、結果を開く処理を担当します。`ui_elements.cpp` はドックスペース、メニュー、ツールバー、ステータス、コンソール、ショートカット、設定の反映を、`scene_preview_lifecycle.cpp` はシーン・モデルの開始・終了・再構築・表示切替・ウィンドウ描画を担当します。

#### `src/main_window/file_structure_diagram.cpp`

- Include 深度でノードを分け、サイズ、接続、境界、リビジョンをキャッシュします。`file_structure_layout_is_current()` が有効性を確認し、`rebuild_file_structure_layout()` がソース構成・フォント・寸法の変更時に再構築します。
- `open_parent_directory_in_explorer()` は ShellExecute で実ディレクトリを開きます。`render_source_file_context_menu()` は構成図と Inspector でディレクトリ・ソース表示を共有し、編集モードの Include ノードでは参照先置換・解除を遅延要求として受け付けます。
- `App::render_file_structure_window()` は移動可能なキャンバス、階層線、メイン・Include ノード、ツールチップ、右クリックメニューを描き、選択ノードを作業コピーのテキストプレビューへ渡します。

#### `src/main_window/text_preview.cpp`

- `build_text_preview_lines()` は行頭バイト位置の索引を作ります。`decode_preview_bytes()` はパーサー未確認のファイルを表示用にデコードします。通常のマップ・リストは `kv_get_source_text()` を優先し、メモリ適用後の作業コピーを表示します。
- 範囲・隙間・EOF の関数は行から `DistanceResolutionBoundary` を探します。マーカーの書式・描画関数はパーサーが確認した行間の挿入位置を示し、`utf8_byte_for_source_column()` はソース列を ImGui 選択用バイト位置に変換します。
- `open_text_preview()`、`refresh_text_preview_from_working_copy()`、`refresh_text_preview_after_map_load()` が通常表示を管理します。`open_text_preview_for_distance_resolution()` は候補境界と対象文位置を渡します。
- `render_text_preview_window()` は行番号、読み取り専用 UTF-8、選択、ソース位置、境界ボタンを描画します。ユーザーがレポート内の境界トークンを選ぶと、主編集状態機械が操作を再試行します。

#### `src/main_window/touch_input.h` と `src/main_window/touch_input.cpp`

- `TouchFrame` は `PinchAxis` を含むフレームごとのタップ、長押し、スクロール、ピンチをまとめます。公開関数は Win32 メッセージ、フレーム状態、領域ごとの消費、ポップアップ操作、テスト入力を扱います。
- `ActiveTouch`、`PairState`、`TouchManager` はポインター ID、押下位置・時間、移動しきい値、2 指の中心と倍率を追跡します。down/update/up/capture-lost を処理し、長押しとスクロールを排他的に判定します。ピンチは指間距離の変化を軸倍率に変換します。
- `new_frame()` は一時イベントを公開・クリアし、`consume_*` は消費済みを記録します。`apply_touch_scroll_to_hovered_window()` は ImGui スクロールに対応付けます。`debug_*` は制御した時計と合成タッチ点で状態機械を検証します。

#### `src/main_window/map_marker_visuals.cpp`

- `append_polyline/polygon/circle/glyph/sampled_arc()` は正規化形状を描画定義へ追加します。停車場、曲線、勾配、速度制限、地上子、先行列車、サウンド・noise、背景、粘着、運転台照明、霧、他軌道の `*_recipe()` が共通アイコンを定義します。
- `map_marker_theme_color()` は種類ごとのテーマ色、`map_marker_role_color()` は塗り・輪郭・アクセント・文字の色を返し、`map_marker_icon_recipe()` が種類とバリエーションから定義を選びます。
- `draw_map_marker_icon()` は `transform_icon_point()` で正規化座標を拡大縮小・回転・移動し、ImDrawList で描画します。3D は同じ定義からビルボード頂点を作ります。

### 2D ビューとグラフ

#### `src/canvas2d/canvas2D.cpp`

- **問い合わせと表示データ**：`nearest_own_index()`、`interp_own_z()`、`track_info_at()`、`speed_at()`、`curve_sections()` は計測・重ね表示用の値を取得します。`build_plan_data()` は自軌道・他軌道、停車場、速度制限、マーカーをまとめ、`current_plan_data()` はモデル・形状・表示リビジョンでキャッシュします。縦断面データも構築と取得を分けます。
- **操作と描画**：計測、フォーカス、座標変換、`jump_to_distance()` は位置移動を共有します。`render_plan_canvas()` はマウス・タッチ、可視距離程の計算、背景・グリッド・軌道・Repeater・停車場・マーカー・ラベル・3D 位置・フォーカスの描画をまとめます。

#### `src/canvas2d/canvas2d_view_state.h` と `src/canvas2d/canvas2d_view_state.cpp`

- `App` 所有の `View2D` は中心、縮尺、回転、全体表示、ドラッグ状態を保持し、ワールド・画面変換、画面上の移動量によるパン、境界への全体表示を提供します。

#### `src/canvas2d/canvas2d_marker_cache.h` と `src/canvas2d/canvas2d_marker_cache.cpp`

- 行・サンプル・下限・上限の関数で自軌道・他軌道行列を距離程から取得し、局所オフセットでマーカーを配置します。Repeater の LOD、境界、チャンク構築が連続ストラクチャーの表示範囲を定めます。
- `rebuild_speed_limit_marker_overlay_cache()` と `rebuild_marker_overlay_cache()` は、編集 ID、ソース行、軌道位置から平面マーカー、Repeater 区間、他列車経路、表示索引を作ります。キャッシュと無効化は `App` が所有します。

#### `src/canvas2d/canvas2d_interaction.h` と `src/canvas2d/canvas2d_interaction.cpp`

- 計測のヒット判定は平面データの世代と縮尺で空間グリッドをキャッシュし、小規模データは全探索します。マーカーは画面上の半径、行順、重なり優先度で対象を選びます。
- 右クリック対象の収集、`render_plan_marker_context_menu()`、`plan_context_source_for()` は、各マーカーのテーブル移動、プロパティ編集、削除、ソース位置を提供します。

#### `src/canvas2d/canvas2d_background.h` と `src/canvas2d/canvas2d_background.cpp`

- ワールド・UV 変換と画面上の四角形描画を扱い、2 駅の座標と画像上の 2 点から縮尺・回転・移動を計算します。`App` のラッパーが背景履歴を保存します。

#### `src/canvas2d/canvas2d_primitives.h` と `src/canvas2d/canvas2d_primitives.cpp`

- `PlanScreenTransform` はモデル・平面・画面の座標を変換します。折れ線構築、範囲クリッピング、Repeater チャンク・概観用 LOD で可視形状を選びます。
- グリッド、スケール、三角形、ひし形、信号、先行列車、方向矢印、文字の関数が、低水準の ImDrawList 描画を包みます。

#### `src/canvas2d/profile_plots.cpp`

- ヘルパーはベクトル曲線、タイトル・単位、軸端のマスク、左右の半径符号、下端基準ラベル、縦断面マーカーを描きます。RAII クラスは ImPlot の全体表示ボタンとホイール拡大縮小を一時的に変更します。
- タッチ拡大縮小は `TouchFrame` を X・XY 軸の範囲へ変換し、`preserved_plot_span()` でデータが空または再構築中でも適切な幅を保ちます。
- `render_profile_plot()` は距離程・標高、勾配の塗り・ラベル、停車場、速度制限、編集用の曲線・勾配マーカーを描画し、共通フォーカス、ダブルクリック計測、ホバー、右クリック操作を扱います。
- `render_radius_plot()` は左右を分けた距離程・曲線半径を描き、移行区間マーカーの対応を共有します。`render_plots()` は現在の 2D モードに応じて領域を配分し、キャッシュ済み `ProfileData` を使います。

### 3D 描画とシーン構築

#### 3D プレビューのモジュール

`Canvas3D::Impl` はプレビュー状態、ワーカー、GPU リソース、キャッシュを所有します。非公開ヘッダーには状態とインターフェースを宣言し、インラインの数値処理と Repeater 訪問用テンプレートを置きます。CMake は機能別の `.cpp` を個別にコンパイルします。

| モジュール | 役割 |
| --- | --- |
| `canvas3D.cpp`、`canvas3d_impl.h` | 公開呼び出しの委譲と、共通の非公開メソッド・状態宣言 |
| `canvas3d_math.h`、`canvas3d_types.h` | インラインのベクトル・行列演算、CPU/GPU レコード、共通定数、判定関数 |
| `canvas3d_scene_data.cpp/.h` | 型付きマップからシーンへの変換、路線値・停車場、マーカーメタデータ、霧入力の変換、描画距離 |
| `scene_fog.cpp/.h`、`scene_shader_source.h` | CPU 上の霧キーフレーム構築・取得とシーン HLSL。`route_value_sampling_contract` で CPU 処理とシェーダーのコンパイルを検証 |
| `canvas3d_scene_lifecycle.cpp` | シーン置換、動的内容・マップ・停車場の更新、表示・設定、モデル要求、リソース寿命 |
| `canvas3d_model_loader.cpp/.h` | モデルローダー v2 クライアント、WIC テクスチャとキャッシュ、CPU モデルワーカー、アップロード待ち行列、診断 |
| `canvas3d_put_between.cpp/.h` | 元モデルの準備・変形、非同期 PutBetween プレビュー、要求順の確認後の結果公開 |
| `canvas3d_model_preview.cpp` | 単体モデルの読み込み、リソース解放、対話的プレビュー |
| `canvas3d_d3d_resources.cpp` | HLSL、シェーダーパイプライン、深度・合成・ラスタライザー状態、描画先、インスタンスバッファ |
| `canvas3d_scene_geometry.cpp/.h` | シーン・軌道チャンク、軌道配置座標系、Repeater インスタンスとキャッシュ |
| `canvas3d_scene_markers.cpp` | マーカー頂点、文字・アイコン、フォントキャッシュ、可視索引、マーカー描画 |
| `canvas3d_scene_camera.cpp` | 軌道サンプリング、カメラ移動・ジャンプ、フォーカス対象 |
| `canvas3d_scene_edit.cpp`、`canvas3d_scene_gizmo.cpp` | 配置プレビューの更新・無効化、ギズモの投影・ヒット判定・ドラッグ・描画 |
| `canvas3d_scene_render.cpp`、`canvas3d_scene_ui.cpp` | 描画パス、選択・強調表示、可視インスタンス、ImGui の処理順、情報表示、右クリックメニュー、遅延操作 |
| `tests/scene_render_contract.cpp`、`tests/scene_loader_contract.cpp` | Debug の描画・キャッシュ・ピクセル・選択契約と、モデルローダーの所有権・失敗契約 |

#### 描画と操作

- **軌道サンプリング**：`scene_track_sampling.cpp/.h` は通常の軌道取得、始点より前のカメラ位置の外挿、カメラ距離程の範囲を提供します。始点前の外挿はカメラ用です。
- **シーンデータ**：ベクトル・行列・境界ボックス関数でワールド変換を作ります。CPU/GPU レコードはモデル、マテリアル、テクスチャ、チャンク内インスタンス、マーカー、選択対象、逆方向の位置移動を管理します。
- **モデル読み込み**：`ModelLoaderClient` は `bin/model_loader.dll` の v2 API を読み、確保と解放を対応させます。ワーカーが CPU データをコピーし、メインスレッドが `upload_pending_scene_models()` で D3D リソースを作ります。ライフサイクル担当がキャンセル、join、起床、アップロードを調整します。
- **リソースとシーンの寿命**：`load_model()`、`upload_model()`、`reload_model()` は単体モデル、`load_scene()`、動的・マップ・停車場更新、`clear_scene()` はシーンの置換・更新を扱います。パイプラインは必要時に作り、テクスチャはキャッシュを再利用し、インスタンスバッファは必要量に拡張します。対応する関数で解放します。
- **チャンクと配置**：`build_scene_chunks()` は Structure、Signal、Repeater、軌道形状を距離程でまとめます。軌道サンプリング、カント座標系、`make_track_placement_frame()`、`make_track_world()` が BVE 配置値をワールド行列に変換します。描画はカメラ相対座標と reversed-Z 深度を使います。
- **マーカーと選択**：2D 共通のアイコン定義から 3D ビルボードを生成し、表示変更時に索引を再構築します。選択パスはオブジェクト・マーカー ID を書き、1 ピクセルを読み戻します。強調マスクと輪郭合成でホバー・選択中の枠を描きます。
- **路線情報**：`scene_route_overlay.cpp/.h` は共通サンプリングから半径、カント、勾配、速度制限、Section 信号速度、次駅を整形します。`Curve.Interpolate` 区間は両端の評価済み半径・カント、方向矢印、三角の区切りを表示し、両半径が 0 なら各言語の「直線」表示を使います。
- **フレーム全体の進行**：`canvas3d_scene_ui.cpp` の `render_scene_preview()` は非同期アップロード、カメラ、ギズモ、可視インスタンス、描画、選択、強調、右クリックメニューを処理し、位置移動・編集・削除を遅延操作として返します。描画パスは `canvas3d_scene_render.cpp` などが実行します。

ギズモは対象ごとに `Canvas3DPlacementDragUpdate` を生成します。通常配置の座標は mm 単位で切り捨て、Sound3D は X/Y をソースのオフセット、Z を 1 m 単位の距離程に反映します。明示的 Repeater End の Z は区間の終点を更新します。`Structure.PutBetween` は自軌道の前方を向く Z 軸を使い、距離程を 1 m 単位にそろえます。ワーカーは最新の対象に要求を集約し、モデルの長手方向の断面ごとに軌道サンプルを再利用して、再利用可能な動的頂点バッファに結果を公開します。

#### フレームレートと段階別計時

FPS はシーン描画の呼び出し頻度です。`canvas3d_scene_ui.cpp` は `0.1` 秒以下の正の間隔を加算し、有効時間が `0.2` 秒に達すると `interval_count / active_seconds` を表示します。長い空白期間では未完了の集計区間を破棄し、直前の表示を保持します。リセットでは基準時刻、集計値、表示値を消去します。`win32_dx11_bootstrap.cpp` はメインループの Present と描画再開を担当します。

`scene_frame_profile.h` は Debug の段階別計時を提供します。描画・ローダー契約は `NDEBUG` ガード付きで EXE に組み込み、シーンベンチマークとローダーのヘッドレスコマンドから実行します。

#### 霧

`scene_fog` は GUI データに展開済みの `Fog`・`Legacy.Fog` 行からキーフレームを作り、シーン更新時に再構築します。

- 距離程と全体ソース順に処理します。距離程 0 以外の Legacy 文は、元の距離程に旧状態ノード、`distance + 25` に目標ノードを作り、安定ソートします。
- 同距離程では最初のノードを前区間の補間先、最後のノードをその地点での有効値とします。省略した Fog 値は、未来の Legacy 目標を含め、すでに追加された最遠のノードから継承します。
- 二分探索で値を取得します。同じ方式内では密度、RGB、start/end を double で補間し、方式が異なる区間では前ノードを使います。イベントがなければ霧は無効で、初期の省略値には既定値を使います。
- 入力変換は無効な距離程・区間値を除き、色を 0–1、シェーダーへ渡す距離を安全な float 範囲に収めます。

シーンのピクセルシェーダーはカメラ空間の深度（投影の `w`）から指数霧、または線形霧 `clamp((end-depth)/(end-start), 0, 1)` を計算します。幅 0 の区間は `end` で切り替え、有限の逆向き区間にも同じ式を使います。霧は背景・モデル・軌道に適用し、UI、マーカー、強調マスクは別に描きます。式と深度の基準は Microsoft の[霧の数式](https://learn.microsoft.com/en-us/windows/win32/direct3d9/fog-formulas)と[ピクセル霧](https://learn.microsoft.com/en-us/windows/win32/direct3d9/pixel-fog)を参照してください。

`src/canvas3d/tests/scene_fog_tests.cpp` は `route_value_sampling_contract` に含まれます。Legacy・混在方式、隣接・同距離程境界、継承、表示切替、数値保護を検証し、`D3DCompile` で共通の頂点シェーダーと通常・霧用ピクセルシェーダー入口をコンパイルします。

### データテーブルとビュー間の移動

次のモジュールは `src/table/` にあり、CMake で個別にコンパイルします。`App` が `TableUiCache` を所有し、`datatable_cache.cpp` が局所的に構築して move で公開します。`table_navigation.cpp` は無効化後の状態リセットとビュー間の移動を担当します。

| モジュール | 役割 |
| --- | --- |
| `datatable.cpp` | 共通セル・数値変換、基本 UI ヘルパー、シーン軌道キーの意味情報 |
| `datatable_internal.h` | 非公開のインライン列定義、内部操作・ビュー・ヘルパー宣言。ウィンドウ、キャッシュ構築、専用テンプレートは各 `.cpp` が担当 |
| `datatable_cache.cpp` | `TableUiCache` 全体の構築、動的な Section/Signal 列、Repeater 表示行の統合、列幅計測、実行時の速度制限キャッシュ更新 |
| `datatable_find.cpp` | 大文字・小文字を区別しない検索・リセット・結果移動、未使用キー検索、Structure/Signal/Sound 用 App 検索 API |
| `datatable_resource_lists.cpp` | 共通の編集リスト描画と、Station・Structure モデル・Signal 現示・Sound・Sound3D のリソース画面 |
| `datatable_route_tables.cpp` | 他軌道、Station.Put、Structure、他列車、Repeater、Signal.Put、Section、変数の画面 |
| `datatable_effect_tables.cpp` | Beacon、Irregularity、サウンド・noise、Background、Adhesion、CabIlluminance、Fog、Lighting、DrawDistance、SpeedLimit の画面 |
| `datatable_scenario.cpp` | Scenario File テーブルとパス・候補編集 UI |
| `datatable_benchmark.cpp` | Debug 専用の実マップでのキャッシュ再構築、ウォームヒット、入力なしテーブル描画の計測。キャッシュ要約とソース完全性も検証 |
| `table_navigation.cpp` | キャッシュ無効化後の状態リセットとテーブル・平面図・シーン間の移動 |

マップテーブルは `TableUiCache` と表示状態を読み、下書き変更・選択・位置移動には App の共通メソッドを使います。シナリオテーブルは専用の下書きを使います。

#### `src/table/table_navigation.cpp`

- `invalidate_table_cache()` はリビジョンと派生行を消去し、`reset_marker_visibility()`・`sync_marker_visibility_sizes()` は 2D 表示フラグをモデルの行数に合わせます。
- Structure・Repeater・Signal の `locate_*_on_plan/in_list/in_scene_preview()` は、平面フォーカス、テーブル強調・表示、Canvas3D ジャンプを更新します。Repeater は End と変化点の境界も扱います。
- `locate_standard_marker_on_plan()` と `locate_standard_marker_in_list()` は Beacon、Section、Irregularity、Sound/Noise、Background、Adhesion、CabIlluminance、Fog、DrawDistance、SpeedLimit などの共通移動処理です。
- 他列車の停止位置へ移動すると対応グループと停止行を開きます。`locate_scene_marker_row_in_list()` と `locate_scene_marker_row_in_scene_preview()` は Canvas3D の種類を正しいテーブル・ソース行へ対応付け、`can_locate_scene_preview_row()` はシーン・インデックス・表示条件を確認します。

### Debug 入口と契約テスト

#### `src/main_window/debug_headless.h`

- 各 `*Options` はコマンドラインモードに対応します。Map/Scenario 読み込み、平面・シーン・開く・編集・テーブルキャッシュの計測、シーンローダー・カメラ転送、診断ポップアップ、ソース位置、往復編集、距離程・自軌道・他軌道、Station・リソースリスト、Repeater、Section、Include、新規ファイル・要素、検索、タッチ、設定保存を扱います。
- Debug 専用の `run_debug_headless_*()` を宣言し、オプション構造体が `main()` の引数解析と検証実装を結びます。

#### `src/main_window/debug_headless.cpp`

- **共通処理とオプション**：COM RAII、UTF パス、出力ファイル、統計、ハッシュ、スナップショット行列の要約、ログ収集、フィクスチャ検索で決定的な出力を作ります。各モードの引数も解析します。編集計測は `edit_benchmark.cpp`、テーブル計測は `src/table/datatable_benchmark.cpp` に分離しています。
- **読み込み・形状・シーン**：Map 読み込みはスナップショット構造と行列、Scenario は v2 スナップショット、往復編集、候補選択、解決したマップを検証します。平面・シーン計測はキャッシュ構築を繰り返して段階時間・件数・ハッシュを出し、カメラ転送は再構築前後の姿勢、シーン検証はピクセルと霧の状態を確認します。
- **`typed_edit_headless`**：`Field/Change/Batch/Report` は公開編集 ABI の RAII ラッパーです。文字列ビューの寿命、dry run・apply・commit レポートのコピー、失敗情報を管理します。
- **距離程・自軌道・他軌道バッチ**：`distance_batch_headless` はハンドル、対象、境界選択、レポートで複数ファイル・Include・変数のケースを実行します。自軌道・他軌道はメソッドと引数形式、Apply/Reset/Commit、形状変化を確認します。
- **リストと関連編集**：`station_list_edit_headless` は一時 CSV で編集・クリア・並べ替え・削除・文字コード維持を検証します。`repeater_batch_headless` は連続区間の更新、トリム変換、一括削除の原子性を、`section_edit_batch_headless` は可変引数の追加・削除、null・式の保持、commit を確認します。
- **挿入とソース位置**：挿入モードは許可テンプレート、距離程ブロック選択、不明項目の拒否を確認します。位置・往復編集モードは実ファイル、Include スタック、行・列・範囲、安定 ID、保存・再読込後の整合を確認します。
- **UI と永続化**：検索は大文字小文字、完全一致、結果移動、未使用状態を、タッチは合成入力でタップ・長押し・スクロール・ピンチ・消費を検証します。設定保存は一時ディレクトリで正規書式、不正値の既定動作、読み込み後のファイルバイト列、History の境界を確認します。各入口は PASS/FAIL を出力します。

#### `src/main_window/edit_benchmark.cpp`

- `App::run_debug_headless_edit_benchmark()` は実際の Inspector Apply、遅延 Delete、Save、Revert、更新、最初のシーンフレームを通り、操作別の内包時間と統計を出します。入力路線と全読み込み済み実ソースをバイト単位で保護します。Save は専用一時ルート配下の通常ファイルコピーで行い、Windows リパースポイントとルート外へのコピーを拒否します。
- 更新用フィクスチャは、全体・部分展開の集約、テーブル・平面キャッシュの一度だけの無効化、Apply 失敗からの復旧、空の Save を確認します。実装は `NDEBUG` ガードで Debug に限定します。

#### `src/main_window/headless_entrypoints.cpp`

- 実際の App 編集状態とダイアログ要求処理を再利用し、要素追加の Apply/Inspector/Delete、リスト置換・行挿入、Map・Scenario・5 種類のリソースリストの作成・再利用、参照保存・再読込・後始末、Scenario のライフサイクルを検証します。
- 新規ファイル検証は `tests/` 配下の未存在パスを要求し、作成したファイルを終了時に削除します。リスト挿入・置換はメモリ適用を検証し、置換では実際のファイル選択画面を使います。

#### `src/maploader/tests/typed_snapshot_tests.cpp`

- `TempFixture` は一時マップ・リストを作成・削除します。文字コードヘルパーは UTF-8/BOM、UTF-16、CP932 入力を作り、`MapHandle` は RAII で `kv_free()` を呼びます。`CHECK_ARRAY` などは件数と null ポインターの契約を確認します。
- スナップショットテストは全ルート配列、文字列・span、メタデータ、機能、リビジョン、安定 ID を走査します。Windows エクスポート表では唯一の読み込み入口 `kv_load_map_ex()` を確認し、Signal glare・変数キー、リソース Load、Include、シーンスナップショットも検証します。
- 形状テストは勾配・曲線フィクスチャで路線長、水平投影、標高、イベント距離程を比較します。
- `UpdateBatch`・`RepeaterTrimBatch` などが型付き編集を構築します。編集テストは dry run、メモリ Apply/Reset、直接 Apply、Commit、競合ハッシュ、距離程解決、メソッド・引数形式、意味保護、文字コード、ロールバックを扱います。
- 診断テストはローカルの `tests/` を読み、欠落ファイル、不正構文、Load/Enable 重複、孤立した移行点、不明キー、ログ・最終エラーを確認します。`main()` は `snapshot`、`geometry`、`edit`、`diagnostics` と個別オプションでグループを選び、CTest 用の終了状態を返します。

#### `src/main_window/tests/route_value_sampling_tests.cpp`

- CPU 契約は補間端点の所有関係、省略値の継承、BeginTransition、不正数値、3D 路線情報の正・負・0 半径表記、0 から 0 への補間の「直線」表示を検証します。

### 主な呼び出し経路

```text
main / App
  -> maploader_runtime の kv_* 転送関数
     -> maploader.cpp の C ABI 例外境界
        -> scenario_route -> KvScenarioSnapshot / Route 候補
        -> parse_map_context -> Parser -> MapContext
        -> generate_geometry -> Matrix / シーン制御点
        -> MapSnapshotBuilder -> KvMapSnapshot
        -> build_edit_report -> 変更ソースの再解析・意味検証 -> メモリ上書き、またはトランザクション書き込み

App / MapModel
  -> datatable, canvas2D, profile_plots がリビジョン別にキャッシュする 2D ビューを構築
  -> Canvas3DScene -> Canvas3D::Impl -> D3D11 チャンク、非同期モデル、選択、ギズモ
  -> inspector / セル内下書き -> KvEditBatch -> maploader のソースを基準とした編集経路
```

`MapContext` はパーサーとスナップショットの格納領域を所有し、GUI は必要なデータを `MapModel` にコピーします。キャンバスはリビジョンで派生キャッシュを管理します。DLL が返す独立した文字列やモデル配列は、その DLL の対応する解放関数で解放します。

## 開発上の基本規則

### C++ と ABI

- C++17 を使い、RAII、標準コンテナー、`std::filesystem`、役割の明確なヘルパーを優先します。
- `UNICODE`、`_UNICODE`、`NOMINMAX`、`WIN32_LEAN_AND_MEAN` の定義を維持します。
- 公開 C ABI は固定幅 POD と明示的な所有権を使います。境界で例外を捕捉し、DLL が確保したメモリは対応する関数で解放します。
- EXE は maploader API v13、マップスナップショット v9、モデルローダー API v2 の完全一致を要求します。マップ読み込み入口は `kv_load_map_ex()`、`KvScenarioSnapshot` と `KvScenarioEditDocument` は v2 です。マップ、シーン形状、編集対象、レポートはそれぞれバージョンと構造体サイズを管理します。
- ABI 入力は呼び出し中のみ有効なビューです。入れ子のスナップショットはハンドルが所有し、公開ヘッダーの規定に従い、形状再構築、編集、reset、再解析、解放で無効になります。Scenario は独立して確保し、`kv_free_scenario_snapshot()` で解放します。
- 公開 ABI の変更ではバージョン・構造体サイズの方針を明示し、EXE・DLL・呼び出し側を同時に更新して、所有権と寿命を文書化します。

### 解析・形状・元のソース形式の維持

BVE Map 2.0 以降、既存の旧形式、Include、変数、定義済み `distance`、数学関数、コメント、UTF-8/BOM、UTF-16LE/BE、CP932/Shift_JIS を扱います。解析とプリセットには公式の一般的な BVE 構文を使います。

各 Map コンテキストは距離程 0 と、そのファイル固有の距離式から始めます。Include は通常変数とソース識別を継承し、結合時は親の距離程・式を保持して子のイベント・制御点・変数書き込みを取り込みます。並列の事前解析結果は変数依存を検証し、必要に応じて再解析します。

編集可能な行は、実パス、Include スタック、ソース範囲、元の文・引数、評価済み値、距離式、解析順、安定 ID を保持し、`KvMapSnapshot` が型付きビューで提供します。書き戻しは元の文字コード、BOM、改行を維持し、表現できない新しい文字があれば中止します。

AI ツールで BVE の Map・リスト・Scenario の読み込み、検証、型付き表現、編集、新規作成、書き戻しを変える場合は、対象分野のスキルと [`komapedit-bve-format-compliance`](../.agents/skills/komapedit-bve-format-compliance/SKILL.md) を併用します。実装前に、取得日付き公式ページキャッシュを確認し、対象ページを読み、適合表を作成してください。

#### Scenario

`scenario_route.cpp/.h` が次の処理を管理します。

- **読み込み・表示**：種別、`BveTs Scenario 2.00` ヘッダー、宣言文字コード、`#`・`;` コメントを処理し、公式 8 項目の最後の出現、相対パス、重み、ソースハッシュ、存在ビットを保持します。Route 項目や参照先がない Scenario も表示できます。
- **マップを開く**：`kv_resolve_scenario_routes()` が Route 候補と参照先ファイルを確認し、マップローダーで検証します。Vehicle はプレビューに使います。Route 解決やマップ読み込みに失敗しても、GUI はシナリオ表示を維持します。
- **下書きの保存**：GUI の Save で直接保存します。既存の Route/Vehicle は最低 1 候補を下書き順に保持します。パスは空や予約構文文字を含む値を除き、重みは有限の正数とします。同じ候補数なら個別に変更し、件数変更時は項目の候補値をまとめて書き換えます。ハッシュ確認と完全再解析後に、文字コード・BOM・改行を保ってトランザクションで書き込みます。
- **ファイル作成**：新規ファイルウィザードは `build_new_scenario_file_content()` で公式のキー順に UTF-8/CRLF を生成し、排他的に作成して再解析します。Route/Vehicle は重みを省略した単一パスから始め、複数候補と重みは Scenario File タブで編集します。

#### 曲線パラメーターと光源

`Curve.SetGauge(value)`、`Curve.SetCenter(x)`、`Curve.SetFunction(id)`、旧形式 `Curve.Gauge(value)` は `CurveEditRow` → `KvCurveRow` → `MapModel::curve_rows` を共有し、自軌道の形状状態イベントを生成します。更新は元のメソッドとソース識別を維持します。3 つの作成テンプレートは現行メソッドを使い、既定値は順に `1.067`、`0`、`0` です。`SetFunction` の読み込み・更新・挿入には、評価結果が `0` または `1` となる数値引数 1 個を要求します。

`Light.Ambient`、`Light.Diffuse`、`Light.Direction` は、`light.ambient`・`light.diffuse`・`light.direction` 対象と `KvLightColorRow`・`KvLightDirectionRow` を通じ、テーブルの値表示とソース編集に対応します。3 種類を常時表示し、条件を満たすと入力を有効にします。Apply・Delete・New の可否は対象と下書き状態で決めます。Apply は変更した種類をまとめ、Delete は遅延要求を使い、ウィザードは選択したソースの距離程 `0` に作成します。

ルートマップと Include の結合後、各光源種別の有効な宣言は最大 1 件です。重複するとその種類の競合行をすべて無効化し、全ソース位置を英語の警告で報告します。Ambient/Diffuse の RGB は `[0, 1]`、Direction は距離程 `0` とします。有効行がスナップショットに入り、更新では未変更の引数式を保持して完全な意味検証を行います。

### 編集モデル

maploader がソースと編集 ID を所有し、GUI は型付き要求で作業コピーを操作します。Preview/Edit は機能ビットに応じてデータを展開します。リソースリストの未適用下書きは、保存前にテーブルで適用します。

| 操作 | 役割 |
| --- | --- |
| `kv_edit_dry_run_typed()` | パッチレポートの構築・検証 |
| `kv_edit_apply_to_memory_typed()` / GUI Apply | メモリ内の作業コピーとプレビューの更新 |
| `kv_edit_apply_typed()` | トランザクションによる直接書き込み |
| `kv_edit_commit_typed()` / GUI Save | 検証済み作業コピーの保存 |
| `kv_edit_reset_memory()` / GUI Revert | メモリ上書きを破棄してディスク基準に復元 |
| GUI Reload | 未保存変更を確認してディスクから再読み込み |

`sourceHash` は作業コピーを識別し、`expectedSourceHash` は Apply/Delete を繰り返してもディスクの競合検出基準を維持します。Apply・Save の前に完全再解析で各対象値、非対象要素、最終変数束縛を確認します。有効な編集では最終 `distance` は変わり得ます。

#### 距離程の配置計画とソースパッチ

距離程の移動・挿入はパーサーの共通境界プランナーを使い、実ファイル、Include 呼び出し、距離程区間、目的の距離程でまとめます。文順、コメント、空の距離程ブロックを保ち、暗黙の先頭ブロック、基準点間の隙間、最終ブロック、EOF を扱います。一意な最終区間は増加・減少の両方向へ延長できます。移動元はその区間内にある必要があり、挿入は既存の一意な位置を優先します。候補列挙とトークン検索は、折り返し区間の直後の隙間も含めて同じプランナーを使います。

各ファイル・Include 出現単位の入口・出口環境は末尾の代入と変数書き込みを記録し、編集メタデータと共に再構築します。環境検証で解決可能な問題は `evaluation_Environment_Requires_Boundary` 候補を生成し、有効位置がなければ編集を中止します。`ambiguous_Source_Section` などの理由をレポートへ付け、候補を絞った後、選択された計画をマップ全体の意味検証に通します。

GUI の初回処理とキャッシュ再利用は操作選択を共有し、処理を阻止するエラーを優先します。再試行の失敗記録は、作業コピーハッシュ、構造化変更、バッチ内の全手動選択をキーにします。距離程代入は有限値とし、負の有限値も許可します。未変更のオブジェクトキー式、コメント、改行は元のバイト列を維持します。Section の `values.N` は既存引数を指し、個数の変更には `values.count` を明示します。

#### 要素とリソースリスト

- **メソッド変換**：通常編集はメソッドと引数形式を保持します。Inspector の座標オフセット操作で `Put`・`Put0`、`Begin`・`Begin0` を明示的に切り替えます。非ゼロのオフセット破棄、短縮形 `Signal.Put` の変換、Repeater のトリムは各確認手順を通します。
- **Legacy.Fog**：`legacyFog.change` は `distance/start/end/red/green/blue` で更新・削除・挿入に対応します。文の 5 引数はすべて有限数で、負値・同値・逆区間・通常範囲外の有限 RGB も受け付けます。未変更の式を保持し、基準と編集で共通の意味書き出しを使います。Include 置換ではサブツリー除外規則で非対象値を保護し、更新時にテーブル、シーンの霧、マーカーを更新します。
- **リスト行**：Station、Structure、Signal、Sound、Sound3D はセル内下書きを共有します。右クリックで上下に挿入します。Structure は 2、Sound/Sound3D は 3、Station は 13 CSV フィールドを使います。Signal の通常行は 6 フィールドで始め、調整後の幅を保持します。通常行と glare 行は一単位で挿入し、glare はユーザーが明示的に追加します。
- **Signal の列**：実ファイルの末尾空フィールドを保持します。`KvSignalAspectRow::metadata.reserved` は通常行のストラクチャーフィールド数、残りの `structure_keys` は glare に属します。形状変更では `mainStructureKeyCount`、`glareStructureKeyCount`、全番号付き項目を渡し、セル編集は既存の形を維持します。各既存行には空欄でも最低 1 つのストラクチャーフィールドを残します。通常・glare 境界は意味検証と変更判定に含め、列操作は表示上限 509 列を超えた実幅全体に適用します。
- **Repeater の関連付け・改名**：全層で `repeater_linkage` と移行区間の関連付けを共有します。有効区間は半開区間 `[first Begin, End)` で、距離程・全体順序・ソース行で並べ、同距離程の最終イベントを有効にします。空区間もソース識別を保持します。改名バッチは一連の全 Begin/Begin0 と明示的 End を含み、名前の重複区間と区間の所有関係を確認します。再解析は非対象の境界も検証し、長さ 0 の新規区間は Begin、End の順に出力します。
- **Repeater のストラクチャーキー**：データ展開、Inspector、シーンは `repeater_structure_keys()`・`set_repeater_structure_keys()` を通じて `_structureKeys.count`・`_structureKeys.N` を共有します。連結した `structureKeys` 文字列は表示用です。モデル配列は欠落位置を保持し、元のリスト長で `k % N` を計算します。端点モデルが欠ける場合、カメラ移動には配置形状の基準点を使います。
- **他軌道の改名**：ルートと全 Include にある同キーの `Track[trackKey].*` を 1 つの型付きバッチに含めます。比較は数値・文字列型を区別し、大文字・小文字は区別しません。バッチが完全で新キーがマップ全体で一意なら受理し、そのキーを参照する他要素は非対象行の検証を受けます。

### UI・テーブル・描画

- Dear ImGui のドッキング、メニュー、ツールの構成を維持します。通常 UI は簡体字中国語・繁体字中国語（台湾）・英語・日本語を同時更新し、言語変更後も ImGui ID を保ちます。
- BVE パラメーター名は `distance`、`trackKey`、`x`、`ry` など公式の英語名・略称を使います。診断とヘッドレス出力は英語、コンソールの操作 UI は 4 言語に対応します。
- 2D の移動・拡大縮小・回転・全体表示、計測、グリッド、駅ジャンプ、背景位置合わせと、3D のカメラ転送、選択・強調、表示切替、マーカー、路線情報、連動ギズモを維持します。
- テーブルをリビジョンでキャッシュし、Section の可変引数と明示的 `null`、変数の順序、ビュー間移動を保ちます。Repeater の検索は順序付きの型付きストラクチャーキーを使います。未使用ストラクチャー検索前に、入力中の Signal 通常・glare セルを下書きへ確定します。
- Assimp は `model_loader.dll` 内に置き、読み込み失敗は診断と後始末の経路で処理します。モデル境界は double で計算し、非有限の位置や公開 float 範囲外の半径を拒否します。動的シーン更新に失敗した場合は、Repeater チャンクとキャッシュ総数を復元します。

#### 下書きと表示状態

`App` は既存行の更新・削除に対し元のディスク行を保持し、全体・部分展開の両方で基準にします。Apply 失敗時は直前状態に戻し、成功時は変更台帳から外れた項目の基準を削除します。Save/Revert は完了した項目を消去します。Inspector は項目ごとに変更を追い、元の値へ戻した項目だけを変更一覧から除きます。他軌道は正確なキーで表示・色・範囲を保持し、明示的な改名時は安定 ID を通じて状態を引き継ぎます。

#### 設定と作者メッセージ

アプリ設定は `settings/settings.ini`、最近のマップ・背景・作者メッセージ設定は `settings/history.ini`、ImGui レイアウトは `settings/imgui.ini` に保存します。読み込みは保存側が出す正確な節・キー・値書式を受け付け、不明・無効な値には既定値を使います。明示的な保存は全体を書き換え、全バイトの書き込みと close 成功後に保存済みとします。失敗時は `App::service_pending_persistence` が設定・レイアウトそれぞれの 1 秒後の期限で再試行し、ウィンドウが隠れている場合や待機中も処理します。レイアウトの変更状態は ImGui 要求フラグが管理します。

作者メッセージは独立した `//--kme--message-from-creator:` コメントから取得し、実ファイル位置と Include 識別を保持します。同じ実メッセージの重複を除いて Preview/Edit に `KvCreatorMessageRow` として公開します。`creator.message` 編集は生の `content` を受け取り、ヘッダー直下の挿入も共通のパッチ・再解析・保存を使います。`creator_messages.cpp` が下書き、テーブル、文書を開く際のポップアップを管理します。History は `[CreatorMessages] count` と `[CreatorMessageN] path/has_messages/suppressed` でメイン Map ごとの設定を保持し、メッセージが一時的に空でも非表示設定を記憶します。

#### 曲線マーカー

`CurveGauge`、`CurveCenter`、`CurveFunction` は行インデックスと編集 ID を持ちます。2D は白い `CG`・`CC`・`CF` の矩形、3D はコードと評価済み値の 2 行標識を描き、選択と青い強調表示を共有します。`[View2D]` の `show_curve_gauge_markers`、`show_curve_center_markers`、`show_curve_function_markers` は既定で無効です。

`Curve.Interpolate` は引数 0・1・2 個の形と編集 ID を維持します。データ展開はソースファイル索引と全体文順で評価済みイベントを結び、ソート済みイベントと同距離程の繰り返し Include を扱います。編集メタデータ結合後、2D 端点と `CurveCircularStart` の外観を使う 3D 標識は同じソース行を参照し、プロパティ編集と遅延削除を提供します。1 文につき 1 マーカーとし、評価済み半径・カントを表示します。半径 0 は `Intpl. 0` です。

Preview では平面ホバーにマーカー配列のインデックスを使い、ソース選択・編集は結び付け完了後に行います。3D は `curve` 分類と右クリックメニューを保持し、メタデータ準備後に編集・削除を有効にします。作成テンプレートは完全な `Curve.Interpolate(radius, cant);` を表示し、省略可能な末尾引数で 3 形式を生成します。

### 性能

- 入力とリビジョンに基づき、読み込み、デコード、ハッシュ、パス解決、スナップショット、テーブル、マーカー、形状を再利用します。
- 大きな配列は可能な限り連続配置とし、密なループや毎フレームのメモリ確保を抑え、大規模軌道配列の O(n²) 走査を避けます。
- 長い解析・モデル読み込みは進捗付きの非同期処理にします。各キャッシュに完全なキー、無効化の担当、リビジョンを設け、ヒットと無効化を検証します。

`refresh_local_preview_after_edits()` は Apply・ロールバック後の更新を集約します。全体展開が部分的な軌道・リスト変換を含み、シーン全再構築が古いシーンの更新を含みます。単一インスタンスと Repeater の座標編集は安定 ID による高速経路を使います。距離程・実ソース位置の索引は初回使用時に作り、同じバッチ内で再利用します。

ソースパッチはソートと重複確認後、元の断片と置換断片を順番に追加し、最終位置から識別用オフセットを計算します。レポートは降順の編集順序と前後各 80 バイトの文脈を保持し、右側に適用済みの編集も反映します。Save はエンコード済みバイト列をトランザクション要求へ move し、長さ、ハッシュ、書き込み検証、ロールバック情報を保持します。

Canvas3D はシーン置換時に配置用軌道索引を作り、自軌道の別名、正規化後に最初に一致する他軌道、不明キーの代替を扱います。Repeater は `begin + k * interval` に配置し、Begin ごとに再開して元のリスト長の `k % N` でモデルを選びます。double の変換は現在の表示範囲内のチャンクに共通上限 65,536 件でキャッシュし、超過分は必要時に計算します。編集は旧・新範囲を無効化し、シーン・チャンク置換はキャッシュをリセット、範囲外へ出たチャンクは解放します。配置形状は軌道の表示設定から独立しています。

背景画像は、要求する明るさがアップロード済みの値と同じなら、形状だけの変更時にテクスチャを再利用します。画像の置換は無効化し、アップロード成功時に明るさの記録を更新します。

編集計時は `operation_timing.h` の steady clock を使います。GUI と maploader は別領域で内包時間の `*_ms`・`*_count` を記録し、GUI 合計には DLL 時間を含めます。App の計時には遅延 Inspector 処理と必要な最初のシーンフレームまでを含め、シーンが非表示・折り畳み中ならその待機を即時終了します。`source.read_decode_hash` は呼び出しスレッドのソース処理、並列 Include は `parse.include_join_merge`・`parse.syntax_diagnostics` に計上します。モデル読み込みは別の非同期ログで記録します。

## 検証

変更したコンポーネントに応じて項目を選び、ビルド、契約テスト、ヘッドレス、手動 UI の結果を分けて記録します。書き戻しは Save と再読込後の内容を比較し、描画、メニュー、ダイアログ、ドラッグなどの見た目と操作は手動で確認します。

`komapedit.exe` は GUI サブシステムの実行ファイルです。PowerShell で結果を取得するには、`Start-Process -Wait -WindowStyle Hidden -PassThru` と `--headless-output` を使います。入力パスは明示してください。一部の自軌道、他軌道、距離程、Repeater、Section モードは、省略すると開発者のローカル路線を使います。

### 性能ベンチマーク

変更前後は同じ路線、パラメーター、ビルド種別、負荷で比較します。シーン、テーブルキャッシュ、編集の各ベンチマークは、それぞれ独立した 3 プロセスを順番に実行し、その間はビルドや他の計測を止めます。連続フレームの性能判定ではプロファイリングを無効にします。

#### シーンとローダー

`--debug-headless-scene3d-bench` の既定値は、固定カメラで 300 フレーム、25 m 間隔、後方 100 m・前方 1200 m、CPU フレーム p95 上限 16.67 ms です。`--interaction moving` は通常の終端制限を使い、1 フレームに 2 m 進みます。モデル読み込みと 5 フレームのウォームアップ後に計測し、アダプター、負荷、最も遅いフレーム、可視インスタンスのフィンガープリントを報告します。

`--profile-stages` は入れ子の CPU 段階時間と非同期 GPU タイムスタンプを記録します。クエリーは事前確保し、複数フレームにまたがって読み、準備済みの有効サンプルのみ集計します。GPU 時間にはコマンド間の空白、CPU の親段階には子処理を含みます。段階別データで原因を調べ、フレーム全体の CPU p95 で合否を判定します。

計測後のシーン契約は、直接計算とキャッシュ経路の double 精度のインスタンスのフィンガープリント、ピクセル、選択結果を比較します。カメラ移動・回転、チャンク間ジャンプと復帰、表示範囲変更、霧、軌道補助線を扱います。`--debug-headless-scene-loader-contract` は一時モデルとヘッドレス D3D で次を確認します。

- Repeater の距離程・モデル順、型付きリスト展開、Include 順、欠落要素、長さ 0 の区間、形状ジャンプ基準点、tilt/span、不正 interval、編集・復元時の無効化、軌道置換、キャッシュ上限。
- モデルコピーと PutBetween ワーカーの失敗、キャンセル、要求調整、DLL の確保・解放の釣り合い、同じパスのモデル再利用・再読込。
- モデル境界の有限性と表現可能性、失敗時の後始末、動的更新失敗後の Repeater キャッシュ復元。
- 入力不変、形状のみ変更、明るさ変更、画像置換、アップロード失敗・再試行時の背景テクスチャの同一性とピクセル。1024×1024 画像の計測では、形状 Apply の中央値・p95 とテクスチャ置換回数を出します。

#### 平面図とテーブル

`--debug-headless-plan-bench` は既定で `--interaction pan` を使い、曲線、緩和曲線、Interpolate 端点のキャッシュと直接計算を比較します。計測モードのヒット結果は全探索と比較し、小規模点群は線形走査、大規模点群は空間グリッドを使います。`measure-stationary` はポインターを固定し、`measure-moving` は決定的な経路を移動します。

平面、シーン、自軌道編集は、Interpolate の引数 0・1・2 個、Apply/Delete/Reset、ソース識別、一意な編集マーカーの一時 Map/Include 契約を共有します。実路線は存在する対象に応じて確認し、行・イベント・マーカーが空の場合はそれらが一致することを要求します。インスタンスに依存する項目は対象外として報告します。自軌道編集は、`rand()` でソースを選ぶマップも含めて Preview→Edit 結合を確認します。

`--debug-headless-table-cache-bench` は Edit メタデータと、INI 保存を無効にした ImGui/ImPlot コンテキストを使います。ウォームアップ後、コールドキャッシュ再構築、1 回のウォームヒット、全テーブルを描く 1 フレームを別々に測り、行、セル、識別、動的見出し、幅のフィンガープリントを確認します。ソースのバイト・ハッシュ検証は計測範囲外で行います。既定は 5 回（1–100）、25 m 間隔です。

`--debug-headless-diagnostics-popup-bench` は 100,000 件の混在ログで、並行処理時の順序、スナップショットのリビジョンキャッシュ、クリッピング付き描画を確認します。

#### 編集

`--debug-headless-edit-bench` は実際の Apply、遅延 Delete、Save、Revert、更新経路を呼びます。入力マップには編集可能な `Structure.Put`、`Repeater.Begin`、`Curve.SetGauge` が必要で、ソース順の最初の有効対象に固定量の座標・軌間変更を加えます。入力路線ではメモリ操作とバイト・ハッシュ確認を行い、Save は相対依存を保った通常ファイルのコピーを専用一時ディレクトリで使います。パス境界とリパースポイントを確認し、終了後に後始末します。

既定値は `--scene off`、`--repeat 5`（1–100）、`--unit-distance 25` です。シーン有効時は 1260×680 キャンバス、後方 100 m・前方 1200 m、路線最小距離程 + 500 m に通常 API で範囲調整したカメラを使います。各反復でコピーを復元して App を作り直し、初期モデル読み込み後から必要な最初のシーンフレームまで計測します。対象の識別、入れ子の段階時間、合計の中央値・p95・最大値、ソース確認、更新・ロールバック契約を報告します。3D の各状態について、5 反復ずつの 3 プロセスを実行します。

### コマンド一覧

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
build\komapedit.exe --debug-headless-new-file-wizard <nonexistent-map-path-under-tests> --headless-output build\new-file-wizard.txt
build\komapedit.exe --debug-headless-scenario-create <nonexistent-scenario-path-under-tests> --route <existing-map-path> --headless-output build\scenario-create.txt
build\komapedit.exe --debug-headless-fresh-resource-list-workflow <map-path> --headless-output build\fresh-resource-list.txt
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

### リソースリストとファイル操作

| コマンド | 入力と書き込み範囲 | 主な検証 |
| --- | --- | --- |
| `--debug-headless-resource-list-replace` | マップパス。Win32 ファイル選択画面で別の有効な Structure List を手動選択し、メモリ適用 | Preview/Edit メタデータ結合、安定 ID、完全再解析、リストキャッシュ・パス更新、再読込後のソースハッシュ。キャンセル、同じファイル、無効なリストは FAIL |
| `--debug-headless-resource-list-insert` | マップパスと `--kind structure` または `signal`。メモリ適用。`--commit` は拒否 | Structure は Include 経由のリストが必要で、2 フィールド行と上下挿入順を確認。Signal は通常・glare の挿入単位、6 フィールド通常行、明示的 glare 追加を確認。両方で Reset とソースハッシュを確認 |
| `--debug-headless-signal-aspect-columns` | Map/Scenario。入力路線はメモリ編集、Save・再読込は一時フィクスチャ。`--commit` は拒否 | 実際の列操作、確認・キャンセル、通常・glare の独立した幅、反復 Apply/Revert、表示上限 |
| `--debug-headless-new-file-wizard` | `tests/` 配下の未存在マップパス。作成後に後始末 | 空マップ再読込、排他的作成、既存リスト再利用、5 種類すべての Load の適用・保存、空リスト再読込 |
| `--debug-headless-scenario-create` | `tests/` 配下の未存在 Scenario パスと、既存マップを指す `--route`。終了後に作成物を削除 | 公式キー順、重複作成の拒否、項目存在ビット、相対パス・既定重み、Scenario 表示、非同期マップ読み込み、履歴維持 |
| `--debug-headless-scenario-lifecycle` | Scenario。入力ソースを読み、専用一時ディレクトリに書き込み | 履歴、メタデータ公開、Reload 時の表示復元、単一・複数 Route 選択、作成・読み込み、Map の後に Scenario を保存する順序、設定・レイアウトの再試行、CSV 出力、ファイル失敗処理 |
| `--debug-headless-fresh-resource-list-workflow` | マップパス。入力を読み、一時ディレクトリに Map・Structure・Station・Signal フィクスチャを作成 | 距離程文のないマップの Load 対象、複数の未保存 Load と空リスト先頭行の一括適用、Signal の反復 Apply、glare の削除・再追加、Revert、隣接行の Save・再読込。`input_map_bytes_unchanged` と `fixture_files_cleaned` を確認 |
| `--debug-headless-table-find` | 内蔵フィクスチャ | Station 下書きからの通常 Sound 参照と Sound3D の区別、カンマ・空白入り Repeater キー、検索前の入力中 Signal 通常・glare セルの下書き確定 |

### 要素の編集と挿入

`--debug-headless-new-element-edit` は実際のウィザード、Inspector、遅延 Delete/Cancel を使います。リソース、Repeater、Structure、他軌道、Curve/Gradient の組み合わせを対象に、始点・終点距離程、移行区間・カントの連動、ソース順、対象ファイル、作成後の編集を確認します。リソースリストのキーは対応テンプレートに入力されます。

既定では Reset/Reload 後にソースハッシュを確認します。`--commit` を付けると、選択したソースに曲線・勾配の対を保存し、実ファイルの差分確認用に変更を残します。複数種類を含む変更台帳のケースには、編集可能な `Structure.Put` と他軌道の position・X/Y 補間パラメーター行が必要です。挿入順、反復 x/y 編集、1 項目だけの復元、Apply 失敗・Revert、表示状態を確認します。関連する Save と新規 Reload の確認には 4 組の一時フィクスチャを使います。

`curve.interpolate` テンプレートは、省略可能な末尾引数、共通の型付き検証、意味フィンガープリントを使います。公式の引数 0・1・2 個を受け付け、カントのみの入力、非有限値、不明項目、未対応メソッドを拒否します。`typed_edit_contract` は Shift-JIS/CRLF の Include フィクスチャで dry run、Apply/Reset/Commit/Reload を実行し、式、コメント、順序、識別を確認します。要素追加のヘッドレス検証では、引数 2 個の既定形、連動チェックボックス、作成後の編集・キャンセル、一意なソースマーカーを確認します。

次のコマンドは入力パスを明示し、メモリ内編集で実際の操作経路を検証します。`--commit` は受け付けません。

| コマンド | 入力条件 | 検証内容 |
| --- | --- | --- |
| `--debug-headless-light-edit` | `tests\light_valid.txt` など、有効な Light 宣言を任意の組み合わせで含むマップ | メタデータ結合、全 3 種類の Apply、遅延削除、距離程 0 への全 3 種類の挿入、評価値とソース形式、元の組み合わせへの Revert とバイト確認 |
| `--debug-headless-pretrain-edit` | 既存の `PreTrain.Pass` を含むマップ | `headless_pretrain.cpp` で時刻・秒数の反復 Apply、距離程、不正入力、削除、作成、挿入後の編集・キャンセル、Revert、2D 識別・ラベル、3D マーカーを確認 |
| `--debug-headless-legacy-fog-edit` | Legacy.Fog 行が任意件数のマップ | `legacy_fog_edit_validation.cpp` で識別、不正入力、反復 Apply、削除、作成、Revert を確認。WARP シーンで霧の更新、加えてテーブル・平面キャッシュと全再構築の予約を確認 |
| `--debug-headless-curve-parameter-edit` | SetGauge、SetCenter、SetFunction を含むマップ | CG/CC/CF 識別、白い標識、独立した表示切替、Inspector の距離程・値、`SetFunction(2)` の拒否、削除・作成、Revert、ソースバイト列 |
| `--debug-headless-station-put-margin-edit` | 距離程 0 の編集可能な `Station.Put` | 0・誤符号の許容範囲の拒否、ウィザード既定値 `margin1=-5`・`margin2=5`、有効な挿入、Revert |
| `--debug-headless-sparse-new-element` | 数値距離程文が 0・1 個、または単調非減少で末尾が 866 未満の対象ソース | 実際の `DrawDistance.Change(500)` ウィザードを、疎なソースは距離程 25、単調な末尾は 866 で実行。ブロック再利用、前後挿入、元テキスト、新行の ID・値、Reset 後のハッシュ |
| `--debug-headless-auto-insert-diagnostics` | `testmap\auto_insert_failures` のフィクスチャディレクトリ | 実ファイル・行・種類・距離程・ID で対象を選び、自動成功、手動復旧、確定的な拒否、全候補の実 Apply、2 回目の Apply、再試行終了、Reset を確認。合格条件は `failed_cases=0` と `result=PASS` |

PreTrain の `passTime` は引用符なしの `hh:mm:ss` または有限の秒数を受け付け、24 時間を超える時刻や、前の値以下の時刻も許可します。一時的な型付き契約フィクスチャで、PreTrain と Legacy.Fog の文字コード、BOM・改行、Include、ディスク競合、Save・再読込を検証します。自動挿入契約は EOF を含む混在バッチと古い選択肢も扱います。

### 関連する文の編集と Include

次のモードは、`--commit` を付けると検証済み作業コピーを保存し、差分確認用に路線変更を残します。既定ではメモリ内の検証とソースハッシュ確認を行います。

| コマンド | 入力条件と検証範囲 |
| --- | --- |
| `--debug-headless-repeater-key-edit` | 明示的なマップパス。一連の区間全体の改名 |
| `--debug-headless-other-track-key-edit` | 明示的なマップパス。最低 2 文の文字列キーの他軌道を選び、軌道全体の原子性、マップ全体での名前重複、依存参照、Apply/Reset/Reload、対象・ファイルハッシュを確認 |
| `--debug-headless-insert-edit --repeater-only` | 一意なキーで Begin と Begin0 を個別に作成し、dry run と Apply/Reset、commit 時は Save/Reload を確認 |
| `--debug-headless-include-delete` | 明示的なマップパス。`--index` は既定 0。古いハッシュの拒否、サブツリー削除、非対象の意味、Reset を確認。残る文が削除対象に依存する場合は中止 |
| `--debug-headless-include-replace` | 明示的なマップパス、`--new-path <file>`、既定 0 の `--index`。パスを単一引用符の引数で書き、旧・新サブツリーを除く全マップの意味、構成更新、Reset/Reload を確認。旧変数への依存や Load 重複は拒否 |

`--debug-headless-include-import-create` は、明示した入力マップと同じ場所に一意な一時子マップを作成します。既存ファイルのインポート、新規作成、Include 挿入、距離程 0 の基準点、再解析、構成更新を確認します。子は BOM なし UTF-8、CRLF、`BveTs Map 2.02:utf-8` ヘッダーを使い、親はメモリ内 Apply/Reset で検証します。元ファイルのハッシュを確認し、終了後に一時子ファイルを削除します。

### 作者メッセージと設定保存

`--debug-headless-creator-message <map-or-scenario-path> [--scenario-index N] [--unit-distance M] --headless-output <report>` は入力を読み、ソースのバイト列を確認します。`--commit` は拒否します。別の一時マップ・履歴設定で Preview/Edit メタデータ、ウィザード、生の内容、未適用下書きがある場合の Save 中止、Apply/Revert/Save/Reload、Include メッセージ、文書を開く際のポップアップ、非表示設定を検証します。DLL 契約は実ファイル単位の重複除去とソース展開順を扱います。

`--debug-headless-settings-persistence` は正規の設定・履歴の保存と読み込み、不正値の既定動作、読み込み後のバイト列不変を確認します。作者メッセージの `has_messages`・`suppressed` は `0`・`1` で保存し、読み込みは正確な `1` だけを true とします。

### 手動での確認

変更に応じて、マップ・Include の読み込みと再読込、平面・縦断面・半径図、駅ジャンプ、計測、CSV 出力、モデル表示とエラー、3D オブジェクト・マーカー・カメラ・ギズモ、Apply/Revert/Save/Reload、セル内下書き、設定保存、Release 配布を確認します。UI ではヒット判定と強調表示、メニュー、確認ダイアログ、列のスクロール、最終的な描画結果を重点的に確認します。

## ビルドスクリプト・依存関係・配布

- 構成は CMake、Windows のビルド手順はバッチスクリプトが担当します。
- `NINJA_EXE`、`VCPKG_ROOT`、未指定時の `x64-mingw-dynamic` を維持します。
- EXE と権利表記は出力先直下、DLL は `bin`、INI は `settings` に配置します。
- 配布用整理では `bin`、`settings`、`LICENSE`、`NOTICE`、`THIRD_PARTY_NOTICES.md` を保持します。
- Debug・Release ビルドと配布用整理は配置検証を共有し、直下に旧配置の INI・DLL がある場合は中止します。
- ImGui の docking ブランチと、上流の ImPlot を使います。
- ライセンス・権利表記を維持します。新しい依存関係を追加する場合は、CMake、開発文書、サードパーティー表記を同時に更新します。
- 路線の配布用出力は `TODO.md` の計画項目です。Include の展開、必要に応じた式の定数化、使用リソースのコピー、レポート作成を予定しており、開発用路線の保護に一時出力ディレクトリを使う計画です。
