<p align="center">
    <img src="../icons/titleimage.png" alt="komapedit" width="600">
</p>

# komapedit

komapedit は、BVE Trainsim のマップをプレビュー・編集する Windows 向けの軽量ツールです。2D の路線図、3D シーンプレビュー、ソースファイルに基づくマップ編集に対応しています。

編集機能は実験段階です。使用前に路線ファイルをバックアップするか、バージョン管理を利用してください。対応する構文は[BVE マップ構文の対応状況](#bve-マップ構文の対応状況)、開発の進捗は [TODO.md](../TODO.md) を参照してください。

## ドキュメント

- [ユーザーマニュアル](user-manual_jp.md)
- [開発状況（TODO リスト）](../TODO.md)
- [開発者ガイド](dev_jp.md)
- [AI コーディングツールを使った開発ガイド](ai-dev_jp.md)
- [AI コーディングツール向けのリポジトリ規則](../AGENTS.md)
- [ライセンス](../LICENSE)、[プロジェクトの権利表記](../NOTICE)、[サードパーティーの権利表記](../THIRD_PARTY_NOTICES.md)

## 主な機能

- **操作画面**：移動・ドッキングが可能なウィンドウ、データテーブルの検索、テーブル・2D ビュー・3D シーン間の位置移動。
- **プレビュー**：平面図、縦断面図、マーカー、ストラクチャーモデル、3D シーンの表示。背景画像の位置合わせ、計測、軌道形状の CSV 出力。
- **編集**：プロパティ画面、リソースリスト、ウィザードによる対応要素の変更・削除・追加。3D ギズモによる一部要素の位置調整。サブマップの管理と、マップ・シナリオ・リソースリストファイルの作成・編集。
- **表示言語**：簡体字中国語、英語、日本語。

## BVE マップ構文の対応状況

- 読み込み・プレビュー：データを解析し、軌道形状、テーブル、マーカー、3D シーンに表示します。
- 基本編集：プロパティ画面、テーブル、ファイル操作メニューで既存の内容を変更し、保存します。
- 新規作成：マップ要素追加ウィザード、新規ファイルウィザード、ファイル構成図、リソーステーブルで文・ファイル・リスト行を作成します。
- グラフィカル編集：2D/3D キャンバス上で要素をドラッグするか、ギズモを使って編集します。
- √：その行に記載した機能・形式に対応、△：一部の形式・項目・連動操作に対応、✕：未対応、-：対象外。

評価は、現在利用できるすべての操作画面を対象としています。複数の引数形式や別名を併記した行で、一部の形式だけを新規作成できる場合は △ としています。`*.Load` の行は、参照パス、リストファイル、リスト内のデータを含みます。具体的な範囲は備考を参照してください。[BVE の公式マップ構文](https://bvets.net/jp/edit/formats/route/map.html)に加え、旧形式の別名と、明示したプロジェクト互換構文・コメントを掲載しています。

| マップ構文 | 読み込み・プレビュー | 基本編集 | 新規作成 | グラフィカル編集 | 備考 |
| --- | --- | --- | --- | --- | --- |
| ファイルヘッダー、バージョン、文字コード | △ | ✕ | △ | - | UTF-8、UTF-16LE/BE、CP932/Shift_JIS の BVE Map 2.0 以降を読み込み。新規マップのヘッダーは `BveTs Map 2.02:utf-8` |
| コメントと基本的な文の構文 | √ | ✕ | - | - | `#`・`//` コメント、セミコロン区切り、キー付き・入れ子の要素、複数行にわたる文に対応。名前の大文字・小文字は区別しない |
| ［プロジェクト用コメント］`//--kme--message-from-creator:"content"` | √ | √ | √ | - | 通常の BVE コメントにメッセージを保存。マップを開く際に既定で表示し、「カスタムメッセージ」で編集・削除、ウィザードの「その他」で作成 |
| 代入・引数・キーに使用する変数 | √ | ✕ | ✕ | - | 解析時に評価。読み取り専用の変数テーブルに、名前ごとの代入値とソース位置を表示 |
| 算術演算子（`+`、`-`、`*`、`/`、`%`） | √ | △ | △ | - | 四則演算・剰余、単項符号、括弧、`+` による文字列連結に対応。編集・新規作成時の距離式確認で数式を入力可能 |
| 距離程の宣言と `distance` を使った式 | √ | △ | △ | △ | 対応要素を通じて距離程ブロックを編集・作成。確認画面では目的の距離程に評価される式を入力。一部の 3D オブジェクトは Z 軸ギズモで距離程を 1 m 単位で変更可能 |
| 数学関数 | √ | △ | △ | - | `rand`、`abs`、`sin`、`cos`、`atan2`、`sqrt`、`exp`、`log`、`floor`、`ceil`、`pow` に対応。距離式の確認時にも使用可能 |
| `include 'file';` | √ | √ | √ | - | ファイル構成図でサブマップ参照の置換・解除・インポート・新規作成。新規ファイルウィザードからも Include を追加可能。見つからない、または無効なサブマップは警告を出してスキップ |
| `Curve.SetGauge(value)` / ［旧形式］`Curve.Gauge(value)` | √ | √ | △ | ✕ | 距離程と軌間を編集。ウィザードは既定値 `1.067` の `SetGauge` を作成。2D/3D に `CG` 標識を表示可能 |
| `Curve.SetCenter(x)` | √ | √ | √ | ✕ | 距離程とカント中心のオフセットを編集。新規作成時の既定値は `0`。2D/3D に `CC` 標識を表示可能 |
| `Curve.SetFunction(id)` | √ | √ | √ | ✕ | 距離程と補間関数を編集。`id` は `0` または `1`、新規作成時の既定値は `0`。2D/3D に `CF` 標識を表示可能 |
| `Curve.BeginTransition()` | √ | △ | △ | ✕ | 対応する Begin/End から既存の緩和曲線開始点の距離程を編集。カント付き Begin または End と同時に作成。始点・終点の緩和曲線オプションは連動し、距離程は個別に指定 |
| `Curve.Begin(radius, cant)` / ［旧形式］`Curve.BeginCircular(radius, cant)` | √ | √ | △ | ✕ | ウィザードはカント付き Begin と、その手前の緩和曲線開始点を作成。既存の旧形式 BeginCircular も編集可能 |
| `Curve.Begin(radius)` / `Curve.Change(radius)` | √ | √ | √ | ✕ | ウィザードで Begin または Change を選択し、終点も同時に追加可能 |
| `Curve.End()` | √ | √ | √ | ✕ | 単独または始点と組み合わせて作成。手前の緩和曲線開始点も追加可能 |
| `Curve.Interpolate(radius, cant)` / `Curve.Interpolate(radius)` / `Curve.Interpolate()` | √ | √ | √ | ✕ | 引数 0・1・2 個の形式に対応し、編集時は引数の数を維持。「曲線半径」を有効にすると 2D/3D マーカーから編集・削除可能 |
| `Gradient.BeginTransition()` | √ | △ | △ | ✕ | 対応する Begin/End から既存の勾配変化開始点の距離程を編集。Begin/End と同時に作成。始点・終点の移行区間オプションは連動し、距離程は個別に指定 |
| `Gradient.Begin(gradient)` / ［旧形式］`Gradient.BeginConst(gradient)` | √ | √ | △ | ✕ | ウィザードで Begin を単独、または End と組み合わせて作成し、それぞれの手前に移行区間も追加可能。既存の旧形式 BeginConst も編集可能 |
| `Gradient.End()` | √ | √ | √ | ✕ | 単独または Begin と組み合わせて作成。手前の移行区間も追加可能 |
| `Gradient.Interpolate(gradient)` / `Gradient.Interpolate()` | √ | ✕ | ✕ | ✕ | 引数 0・1 個の両形式を軌道形状に反映 |
| `Legacy.Turn`、`Legacy.Curve`、`Legacy.Pitch` | √ | △ | ✕ | ✕ | プロジェクト互換構文。すべて自軌道の形状に反映。既存の Legacy.Curve は値と距離程を編集可能 |
| `Track[trackKey].X.Interpolate(x, radius)` / `Track[trackKey].X.Interpolate(x)` / `Track[trackKey].X.Interpolate()` | √ | △ | √ | ✕ | すべての引数形式に対応。距離程・値の編集と文の削除が可能。個々の文の `trackKey` は読み取り専用で、キーの変更は「他軌道」から軌道全体に対して行う |
| `Track[trackKey].Y.Interpolate(y, radius)` / `Track[trackKey].Y.Interpolate(y)` / `Track[trackKey].Y.Interpolate()` | √ | △ | √ | ✕ | 記載したすべての引数形式に対応。編集範囲は X.Interpolate と同じ |
| `Track[trackKey].Position(x, y, radiusH, radiusV)` / `Track[trackKey].Position(x, y, radiusH)` / `Track[trackKey].Position(x, y)` | √ | △ | √ | ✕ | すべての引数形式に対応。引数の数を維持した距離程・値の編集と、文の削除が可能。キー変更は X.Interpolate と同じ |
| `Track[trackKey].Cant.SetGauge(gauge)` / ［旧形式］`Track[trackKey].Gauge(gauge)` | √ | △ | △ | ✕ | 新規作成は Cant.SetGauge。両形式の値・距離程の編集と削除に対応。キー変更は X.Interpolate と同じ |
| `Track[trackKey].Cant.SetCenter(x)` | √ | △ | √ | ✕ | 値・距離程の編集と削除に対応。キー変更は X.Interpolate と同じ |
| `Track[trackKey].Cant.SetFunction(id)` | √ | △ | √ | ✕ | 新規作成の `id` は `0` または `1`。値・距離程の編集と削除に対応。キー変更は X.Interpolate と同じ |
| `Track[trackKey].Cant.BeginTransition()` | √ | △ | √ | ✕ | 距離程の編集と削除に対応。キー変更は X.Interpolate と同じ |
| `Track[trackKey].Cant.Begin(cant)` | √ | △ | √ | ✕ | 値・距離程の編集と削除に対応。キー変更は X.Interpolate と同じ |
| `Track[trackKey].Cant.End()` | √ | △ | √ | ✕ | 距離程の編集と削除に対応。キー変更は X.Interpolate と同じ |
| `Track[trackKey].Cant.Interpolate(cant)` / `Track[trackKey].Cant.Interpolate()` / ［旧形式］`Track[trackKey].Cant(cant)` | √ | △ | △ | ✕ | 新規作成は引数 0・1 個の Interpolate。既存の文は引数の数を維持して値・距離程を編集、または削除可能。キー変更は X.Interpolate と同じ |
| `Structure.Load(filePath)` | √ | √ | √ | - | リストパスの置換、リストの作成・インポート、Load 参照の追加。キー・パス行の編集・追加・削除・並べ替えと、空リストへの最初の行の追加に対応。リストの対応バージョンは 1.00 以降 |
| `Structure[structureKey].Put(trackKey, x, y, z, rx, ry, rz, tilt, span)` | √ | √ | √ | △ | 全項目を編集可能。3D ギズモで X/Y/Z を調整 |
| `Structure[structureKey].Put0(trackKey, tilt, span)` | √ | √ | √ | △ | 「プロパティ/編集」で Put0 と Put を相互変換。Put0 の Z 軸ギズモは距離程を 1 m 単位で変更 |
| `Structure[structureKey].PutBetween(trackKey1, trackKey2, flag)` / `Structure[structureKey].PutBetween(trackKey1, trackKey2)` | √ | √ | △ | △ | 両形式を編集可能。新規作成は引数 3 個の形式。プロパティの下書きを 3D 形状へ即時反映し、Z 軸ギズモで距離程を 1 m 単位で変更 |
| `Repeater[repeaterKey].Begin(trackKey, x, y, z, rx, ry, rz, tilt, span, interval, structureKey1, ...)` / `Repeater[repeaterKey].Begin0(trackKey, tilt, span, interval, structureKey1, ...)` | √ | √ | √ | △ | 全パラメーター、ストラクチャーキー一覧、一連の区間のキーを編集。Begin/Begin0 の相互変換、変化点、関連する文の一括削除、位置ギズモに対応。単独または End と組み合わせて作成。同じキーを持つ開始・終了の対は区間が重ならないように指定 |
| `Repeater[repeaterKey].End()` | √ | △ | √ | △ | 対応する Begin または End ギズモから終了距離程を編集。単独または対で作成。独立した End の専用プロパティ画面は未対応。同じキーの閉じた区間内への End 追加は不可 |
| `Background.Change(structureKey)` | √ | √ | √ | ✕ | 距離程とキーを編集。シーンで背景をプレビュー |
| `Station.Load(filePath)` | √ | √ | √ | - | リストパスの置換、リストの作成・インポート、Load 参照の追加。全 13 項目の編集、停車場行の追加・削除・並べ替えと、空リストへの最初の行の追加に対応。リストの対応バージョンは 0.04 以降 |
| `Station[stationKey].Put(door, margin1, margin2)` | √ | √ | √ | ✕ | 距離程、キー、開くドアの側、停止位置許容範囲を編集。`margin1 < 0`、`margin2 > 0` が必要 |
| `Section.Begin(...)` / ［旧形式］`Section.BeginNew(...)` | √ | √ | √ | ✕ | 距離程と信号インデックス一覧を編集。2D/3D にマーカーを表示 |
| `Section.SetSpeedLimit(...)` / ［旧形式］`Signal.SpeedLimit(...)` | √ | √ | √ | ✕ | 距離程と制限速度一覧を編集。有効な値を 3D の信号情報に表示 |
| `Signal.Load(filePath)` | √ | △ | √ | - | 現示リストの置換・作成・インポートと Load 参照の追加。通常行・glare 行・列を追加可能。ストラクチャーキーは最大 509 列を表示。複数の glare 行を維持する場合は合計フィールド数の維持が必要 |
| `Signal[signalAspectKey].Put(section, trackKey, x, y)` / `Signal[signalAspectKey].Put(section, trackKey, x, y, z, rx, ry, rz, tilt, span)` | √ | √ | △ | △ | 両形式を編集可能。新規作成は全引数を持つ形式。短縮形の拡張時は変換を確認。3D ギズモで X/Y/Z を調整 |
| `Beacon.Put(type, section, sendData)` | √ | √ | √ | ✕ | 距離程と全パラメーターを編集 |
| `SpeedLimit.Begin(v)` / `SpeedLimit.End()` | √ | √ | √ | ✕ | Begin/End をそれぞれ編集・作成・削除 |
| `PreTrain.Pass(time)` / `PreTrain.Pass(second)` | √ | √ | √ | ✕ | 2D/3D マーカーから距離程・通過時刻を編集、または削除。ウィザードの「信号」で作成。時刻は hh:mm:ss または秒数で入力し、`passTime` にポインターを合わせると書式の説明を表示 |
| `Light.Ambient(...)`、`Light.Diffuse(...)`、`Light.Direction(...)` | √ | √ | √ | ✕ | 「光源」でパラメーターを表示・編集。ウィザードの「効果」で距離程 `0` に作成。RGB は `[0, 1]`、Direction の距離程は `0`。各種類の文はルートマップと Include 全体で 1 つずつ指定可能 |
| `Fog.Interpolate(density, red, green, blue)` / `Fog.Interpolate(density)` / `Fog.Interpolate()` / ［旧形式］`Fog.Set(density, red, green, blue)` | √ | √ | √ | ✕ | 引数 0・1・4 個の Interpolate と旧形式 Set に対応。指数関数による霧とその変化を 3D でプレビュー |
| ［旧形式］`Legacy.Fog(start, end, red, green, blue)` | √ | √ | √ | ✕ | テーブルまたは 2D/3D マーカーから編集。線形の霧とその変化を 3D でプレビュー。Fog との混在にも対応 |
| `DrawDistance.Change(value)` | √ | √ | √ | ✕ | 距離程と値を編集。シーンの描画距離に反映可能 |
| `CabIlluminance.Interpolate(value)` / `CabIlluminance.Interpolate()` / ［旧形式］`CabIlluminance.Set(value)` | √ | √ | √ | ✕ | 距離程と明るさを編集。値を空欄にすると Interpolate() を出力。テーブルと 3D 標識には直前の有効値を表示し、有効値がない場合は空欄 |
| `Irregularity.Change(x, y, r, lx, ly, lr)` | √ | √ | √ | ✕ | テーブルとマーカーで値を表示。距離程と全 6 パラメーターを編集 |
| `Adhesion.Change(a)` / `Adhesion.Change(a, b, c)` | √ | √ | √ | ✕ | テーブルとマーカーで値を表示。引数 1・3 個の形式に対応 |
| `Sound.Load(filePath)` | √ | √ | √ | - | リストパスの置換、リストの作成・インポート、Load 参照の追加。キー・パス・バッファ数の編集、行の追加・削除・並べ替えと、空リストへの最初の行の追加に対応。リストの対応バージョンは 2.00 以降 |
| `Sound[soundKey].Play()` | √ | √ | √ | ✕ | テーブルとマーカーにサウンドイベントを表示。距離程とキーを編集 |
| `Sound3D.Load(filePath)` | √ | √ | √ | - | 3D サウンドリストの置換・作成・インポートと Load 参照の追加。キー・パス・バッファ数の編集、行の追加・削除・並べ替えと、空リストへの最初の行の追加に対応。リストの対応バージョンは 2.00 以降 |
| `Sound3D[soundKey].Put(x, y)` | √ | √ | √ | △ | 距離程、キー、X/Y を編集。3D 標識で音源位置を表示。ギズモで X/Y オフセットを調整し、距離程を 1 m 単位で変更 |
| `RollingNoise.Change(index)` | √ | √ | √ | ✕ | テーブルとマーカーに走行音のイベントを表示。距離程とインデックスを編集 |
| `FlangeNoise.Change(index)` | √ | √ | √ | ✕ | テーブルとマーカーにフランジきしり音のイベントを表示。距離程とインデックスを編集 |
| `JointNoise.Play(index)` | √ | √ | √ | ✕ | テーブルとマーカーに分岐器通過音のイベントを表示。距離程とインデックスを編集 |
| `Train.Add(trainKey, filePath, trackKey, direction)` / `Train[trainKey].Load(filePath, trackKey, direction)` | △ | ✕ | ✕ | ✕ | 他列車の定義を表示。外部定義ファイルの読み込みは一部対応 |
| `Train[trainKey].Enable(time)` / `Train[trainKey].Enable(second)` | √ | ✕ | ✕ | ✕ | 他列車の停止位置テーブル上部に有効化時刻を表示 |
| `Train[trainKey].Stop(decelerate, stopTime, accelerate, speed)` | √ | ✕ | ✕ | ✕ | 他列車の停止位置テーブル、走行経路、マップ上のマーカーを読み取り専用で表示 |

## インストールと起動

### リリース版をダウンロードする（推奨）

[GitHub Releases](https://github.com/NewSapporoNingyo/komapedit/releases) から、ビルド済みの実行ファイルを含むアーカイブをダウンロードします。全ファイルを展開し、`komapedit.exe` をダブルクリックして起動してください。

### ソースからビルドする

ソースからのビルドは、ソフトウェア開発の基礎知識がある方向けです。

[開発者ガイド](dev_jp.md)に従ってビルドし、Release 版は `build_release\komapedit.exe`、Debug 版は `build\komapedit.exe` を起動します。

実行ファイルと DLL は同じビルドのものを組み合わせてください。ビルドスクリプトが出力先の直下に古い INI や DLL を検出した場合は、案内に従って `bin`・`settings` に整理し、再度ビルドします。

### 起動時の処理と設定

`maploader.dll`、`model_loader.dll` とその依存 DLL は、`komapedit.exe` と同じ場所にある `bin` ディレクトリから読み込みます。

起動時に必要に応じて `settings` ディレクトリを作成し、次のファイルを作成または読み込みます。

- `settings/imgui.ini`：ウィンドウの位置とレイアウト。
- `settings/settings.ini`：表示言語、文字・UI 部品・停車場マーカーのサイズ、2D の線幅、テーマ色、編集モードの警告状態、3D 設定。3D 設定にはマップを開く際のシーン自動読み込み、霧、描画距離、ギズモのサイズ、カメラ速度、パフォーマンス警告を含みます。
- `settings/history.ini`：最近開いたマップ、背景画像の位置合わせ、マップごとのカスタムメッセージ表示設定。

設定は操作画面から変更してください。現在の設定書式に合う有効な値を読み込み、それ以外の項目には既定値を使います。保存時には設定ファイル全体を書き出します。

## ライセンスとサードパーティーの権利表記

komapedit は Apache License, Version 2.0 で配布しています。ライセンス本文は `LICENSE`、プロジェクトの権利表記は `NOTICE` を参照してください。

本プロジェクトは `kobushi-trackviewer` を基に開発した、BVE Trainsim マップの確認・編集ツールです。

参考プロジェクト：

| プロジェクト | 著作権 | ライセンス |
| --- | --- | --- |
| [kobushi-trackviewer](https://github.com/konawasabi/kobushi-trackviewer)（作者：konawasabi） | Copyright (c) 2021-2024 konawasabi | Apache License, Version 2.0 |

GUI とモデルプレビューに使用するライブラリ：

| ライブラリ | 用途 | 著作権 | ライセンス |
| --- | --- | --- | --- |
| [Dear ImGui](https://github.com/ocornut/imgui) | ドッキング対応 GUI、Win32・DirectX 11 バックエンド、C++ std::string ヘルパー | Copyright (c) 2014-2026 Omar Cornut | MIT License |
| [ImPlot](https://github.com/epezent/implot) | 2D グラフウィジェット | Copyright (c) 2020-2024 Evan Pezent; Copyright (c) 2025-2026 Breno Cunha Queiroz | MIT License |
| [Assimp / Open Asset Import Library](https://github.com/assimp/assimp) | ストラクチャーモデルの読み込み | Copyright (c) 2006-2026, assimp team | Modified BSD 3-Clause License |
| Dear ImGui 同梱の stb 単一ファイルライブラリ | Dear ImGui のフォント・テキスト・矩形パッキング処理 | Copyright (c) 2017 Sean Barrett | MIT License または Public Domain |

このリポジトリのソースやビルド済みバイナリを配布する際は、`LICENSE`、`NOTICE`、`THIRD_PARTY_NOTICES.md` を同梱してください。`third_party/` のソースツリーを配布する場合も、元のライセンスファイルと著作権表記を保持してください。

プロジェクトページ：[komapedit](https://github.com/NewSapporoNingyo/komapedit)

## スター数の推移

<a href="https://www.star-history.com/?repos=NewSapporoNingyo%2Fkomapedit&type=date&legend=top-left">
 <picture>
   <source media="(prefers-color-scheme: dark)" srcset="https://api.star-history.com/chart?repos=NewSapporoNingyo/komapedit&type=date&theme=dark&legend=top-left" />
   <source media="(prefers-color-scheme: light)" srcset="https://api.star-history.com/chart?repos=NewSapporoNingyo/komapedit&type=date&legend=top-left" />
   <img alt="スター数の推移" src="https://api.star-history.com/chart?repos=NewSapporoNingyo/komapedit&type=date&legend=top-left" />
 </picture>
</a>
