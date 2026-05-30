/*
 * Copyright (c) 2026 Sapporo_ningyo
 *
 * Licensed under Apache License 2.0; see LICENSE and NOTICE.
 */

#pragma once

#include <map>
#include <string>

enum class Language { Ja, En, Zh };

struct Translation {
    std::map<std::string, std::string> en;
    std::map<std::string, std::string> zh;
    std::map<std::string, std::string> ja;

    Translation() {
        en = {
            {"app.title", "komapedit"},
            {"menu.file", "File"}, {"menu.options", "Options"}, {"menu.map_info", "Map Info"}, {"menu.view_2d", "2D View"}, {"menu.lang", "Language"}, {"menu.help", "Help"},
            {"menu.open", "Open..."}, {"menu.recent_maps", "Recent Maps"}, {"menu.none", "None"}, {"menu.clear_recent_maps", "Clear List"}, {"menu.reload", "Reload"}, {"menu.export_csv", "Export CSV..."}, {"menu.exit", "Exit"},
            {"menu.controlpoints", "Control Points..."}, {"menu.plotlimit", "Plot Range..."}, {"menu.ui_settings", "UI Settings..."}, {"menu.font_size", "UI Settings..."}, {"menu.about", "About"},
            {"menu.online_docs", "Online Documentation"}, {"menu.open_in_explorer", "Open in File Explorer"},
            {"button.open", "Open"}, {"button.reload", "Reload"}, {"button.export_csv", "Export CSV"},
            {"button.structure_list", "Map Structure List"}, {"button.repeater_list", "Repeater List"},
            {"button.apply", "Apply"}, {"button.reset", "Reset"}, {"button.ok", "OK"}, {"button.cancel", "Cancel"}, {"button.clear", "Clear"},
            {"button.import_bg", "Import"}, {"button.adjust_bg", "Adjust"}, {"button.align_to_station", "Align to Station"},
            {"frame.controls", "Controls"}, {"frame.console", "Console"}, {"frame.plots", "2D View"}, {"frame.othertracks", "Other Tracks"}, {"frame.station_list", "Station List"},
            {"frame.structures", "Map Structure List"}, {"frame.structure_models", "Structure Model List"}, {"frame.repeaters", "Repeater List"}, {"frame.aux_info", "Auxiliary Info"},
            {"frame.chart_visibility", "Chart Visibility"}, {"frame.grid", "Grid"}, {"frame.mode", "Mode"}, {"frame.bgimage", "Background Image"},
            {"column.file_name", "File Name"},
            {"chk.station_pos", "Station Position"}, {"chk.station_name", "Station Name"}, {"chk.station_mileage", "Station Mileage"},
            {"chk.gradient_pos", "Gradient Change Points"}, {"chk.gradient_val", "Gradient Values"}, {"chk.curve_val", "Curve Radius"},
            {"chk.prof_othert", "Other Tracks (Profile)"}, {"chk.speedlimit", "Speed Limit"},
            {"chk.gradient_graph", "Profile"}, {"chk.curve_graph", "Radius"}, {"chk.bgimg_show", "Show"},
            {"grid.fixed", "Fixed"}, {"grid.movable", "Movable"}, {"grid.none", "None"},
            {"mode.pan", "Move"}, {"mode.measure", "Measure"}, {"label.station_jump", "Station Jump"},
            {"canvas.plan", "Plan"}, {"canvas.profile", "Gradient / Height"}, {"canvas.radius", "Curve Radius"},
            {"plot.profile", "Profile"}, {"plot.radius", "Curve Radius"}, {"plot.level", "Level"}, {"unit.m", "m"},
            {"info.mileage", "Mileage"}, {"info.elevation", "Elevation"}, {"info.gradient", "Gradient"}, {"info.radius", "Curve Radius"},
            {"info.speedlimit", "Speed Limit"}, {"info.no_limit", "No limit"},
            {"hint.pick_bg_station", "Double-click the station position on the background image"},
            {"label.bgimg_x", "X (m)"}, {"label.bgimg_y", "Y (m)"}, {"label.bgimg_width", "Width (m)"},
            {"label.bgimg_height", "Height (m)"}, {"label.bgimg_rotation", "Rotation (deg)"}, {"label.bgimg_brightness", "Brightness (%)"},
            {"label.font_size", "Text size"}, {"label.ui_component_size", "UI component size"}, {"label.station_marker_size", "Station marker size"}, {"label.ui_theme_color", "Interface theme color"},
            {"label.font_size_current", "Text size:"}, {"label.ui_component_size_current", "Component size:"}, {"label.ui_theme_color_current", "Theme color:"}, {"label.font_size_preview", "Preview text"},
            {"status.no_map", "No map loaded"}, {"status.loading", "Loading..."}, {"status.ready", "Ready"},
            {"dialog.ui_settings", "UI Settings"}, {"dialog.bgimage_adjust", "Adjust Background Image"}, {"dialog.align_to_station", "Align Background to Stations"}, {"button.pick_on_bg", "Pick station on Plan"}, {"button.pick_on_bg_ok", "Picked"},
            {"about.text", "komapedit\nVersion 0.6.0\nCopyright © 2026 Sapporo_ningyo\nBased on kobushi-trackviewer\nCopyright © 2021-2024 konawasabi\nLicense: Apache License 2.0\nThird-party: Dear ImGui (MIT), ImPlot (MIT)\nSee LICENSE, NOTICE, and THIRD_PARTY_NOTICES.md"}
        };
        zh = {
            {"app.title", "komapedit"},
            {"menu.file", "文件"}, {"menu.options", "选项"}, {"menu.map_info", "地图信息"}, {"menu.view_2d", "2D视图"}, {"menu.lang", "语言"}, {"menu.help", "帮助"},
            {"menu.open", "打开..."}, {"menu.recent_maps", "最近打开的地图"}, {"menu.none", "无"}, {"menu.clear_recent_maps", "清除列表"}, {"menu.reload", "重新加载"}, {"menu.export_csv", "导出 CSV..."}, {"menu.exit", "退出"},
            {"menu.controlpoints", "控制点间隔..."}, {"menu.plotlimit", "绘图范围..."}, {"menu.ui_settings", "用户界面设置..."}, {"menu.font_size", "用户界面设置..."}, {"menu.about", "关于"},
            {"menu.online_docs", "在线文档"}, {"menu.open_in_explorer", "在文件资源管理器中打开"},
            {"button.open", "打开"}, {"button.reload", "重新加载"}, {"button.export_csv", "导出 CSV"},
            {"button.structure_list", "地图布景列表"}, {"button.repeater_list", "连续布景列表"},
            {"button.apply", "应用"}, {"button.reset", "重置"}, {"button.ok", "确定"}, {"button.cancel", "取消"}, {"button.clear", "清除"},
            {"button.import_bg", "导入"}, {"button.adjust_bg", "调整"}, {"button.align_to_station", "按车站对齐"},
            {"frame.controls", "控制"}, {"frame.console", "控制台"}, {"frame.plots", "2D视图"}, {"frame.othertracks", "其他轨道"}, {"frame.station_list", "车站列表"},
            {"frame.structures", "地图布景列表"}, {"frame.structure_models", "布景模型列表"}, {"frame.repeaters", "连续布景列表"}, {"frame.aux_info", "辅助信息"},
            {"frame.chart_visibility", "图表显示"}, {"frame.grid", "网格"}, {"frame.mode", "模式"}, {"frame.bgimage", "背景图"},
            {"column.file_name", "文件名"},
            {"chk.station_pos", "车站位置"}, {"chk.station_name", "车站名"}, {"chk.station_mileage", "车站里程"},
            {"chk.gradient_pos", "坡度变化点"}, {"chk.gradient_val", "坡度值"}, {"chk.curve_val", "曲线半径"},
            {"chk.prof_othert", "纵断面其他轨道"}, {"chk.speedlimit", "限速"},
            {"chk.gradient_graph", "纵断面图"}, {"chk.curve_graph", "曲线半径图"}, {"chk.bgimg_show", "显示"},
            {"grid.fixed", "固定"}, {"grid.movable", "可移动"}, {"grid.none", "无"},
            {"mode.pan", "移动"}, {"mode.measure", "测量"}, {"label.station_jump", "车站跳转"},
            {"canvas.plan", "平面图"}, {"canvas.profile", "纵断面 / 标高"}, {"canvas.radius", "曲线半径"},
            {"plot.profile", "纵断面"}, {"plot.radius", "曲线半径"}, {"plot.level", "水平"}, {"unit.m", "m"},
            {"info.mileage", "里程"}, {"info.elevation", "标高"}, {"info.gradient", "坡度"}, {"info.radius", "曲线半径"},
            {"info.speedlimit", "限速"}, {"info.no_limit", "无限速"},
            {"hint.pick_bg_station", "双击背景图上的车站位置"},
            {"label.bgimg_x", "X (m)"}, {"label.bgimg_y", "Y (m)"}, {"label.bgimg_width", "宽度 (m)"},
            {"label.bgimg_height", "高度 (m)"}, {"label.bgimg_rotation", "旋转角度 (度)"}, {"label.bgimg_brightness", "亮度 (%)"},
            {"label.font_size", "文字大小"}, {"label.ui_component_size", "界面组件大小"}, {"label.station_marker_size", "车站标记大小"}, {"label.ui_theme_color", "界面主题色"},
            {"label.font_size_current", "文字大小："}, {"label.ui_component_size_current", "组件大小："}, {"label.ui_theme_color_current", "主题色："}, {"label.font_size_preview", "预览文字"},
            {"status.no_map", "未加载地图"}, {"status.loading", "加载中..."}, {"status.ready", "就绪"},
            {"dialog.ui_settings", "用户界面设置"}, {"dialog.bgimage_adjust", "调整背景图"}, {"dialog.align_to_station", "按车站对齐背景图"}, {"button.pick_on_bg", "在平面图选点"}, {"button.pick_on_bg_ok", "已选点"},
            {"about.text", "komapedit\nVersion 0.6.0\nCopyright © 2026 Sapporo_ningyo\n基于 kobushi-trackviewer\nCopyright © 2021-2024 konawasabi\n许可证：Apache License 2.0\n第三方：Dear ImGui (MIT)、ImPlot (MIT)\n详见 LICENSE、NOTICE 和 THIRD_PARTY_NOTICES.md"}
        };
        ja = {
            {"app.title", "komapedit"},
            {"menu.file", "ファイル"}, {"menu.options", "オプション"}, {"menu.map_info", "マップ情報"}, {"menu.view_2d", "2Dプロット"}, {"menu.lang", "言語"}, {"menu.help", "ヘルプ"},
            {"menu.open", "開く..."}, {"menu.recent_maps", "最近開いたマップ"}, {"menu.none", "なし"}, {"menu.clear_recent_maps", "リストをクリア"}, {"menu.reload", "再読込"}, {"menu.export_csv", "CSV 出力..."}, {"menu.exit", "終了"},
            {"menu.controlpoints", "制御点間隔..."}, {"menu.plotlimit", "描画範囲..."}, {"menu.ui_settings", "UI設定..."}, {"menu.font_size", "UI設定..."}, {"menu.about", "このアプリについて"},
            {"menu.online_docs", "オンラインドキュメント"}, {"menu.open_in_explorer", "エクスプローラーで開く"},
            {"button.open", "開く"}, {"button.reload", "再読込"}, {"button.export_csv", "CSV 出力"},
            {"button.structure_list", "マップストラクチャー一覧"}, {"button.repeater_list", "連続ストラクチャー一覧"},
            {"button.apply", "適用"}, {"button.reset", "リセット"}, {"button.ok", "OK"}, {"button.cancel", "キャンセル"}, {"button.clear", "クリア"},
            {"button.import_bg", "インポート"}, {"button.adjust_bg", "調整"}, {"button.align_to_station", "駅に合わせる"},
            {"frame.controls", "コントロール"}, {"frame.console", "コンソール"}, {"frame.plots", "2Dビュー"}, {"frame.othertracks", "他軌道"}, {"frame.station_list", "駅一覧"},
            {"frame.structures", "ストラクチャー一覧"}, {"frame.structure_models", "ストラクチャーモデル一覧"}, {"frame.repeaters", "連続ストラクチャー一覧"}, {"frame.aux_info", "補助情報"},
            {"frame.chart_visibility", "チャート表示"}, {"frame.grid", "グリッド"}, {"frame.mode", "モード"}, {"frame.bgimage", "背景画像"},
            {"column.file_name", "ファイル名"},
            {"chk.station_pos", "駅位置"}, {"chk.station_name", "駅名"}, {"chk.station_mileage", "駅キロ程"},
            {"chk.gradient_pos", "勾配変化点"}, {"chk.gradient_val", "勾配値"}, {"chk.curve_val", "曲線半径"},
            {"chk.prof_othert", "縦断面の他軌道"}, {"chk.speedlimit", "速度制限"},
            {"chk.gradient_graph", "縦断面図"}, {"chk.curve_graph", "曲線半径図"}, {"chk.bgimg_show", "表示"},
            {"grid.fixed", "固定"}, {"grid.movable", "可動"}, {"grid.none", "なし"},
            {"mode.pan", "移動"}, {"mode.measure", "測定"}, {"label.station_jump", "駅ジャンプ"},
            {"canvas.plan", "平面図"}, {"canvas.profile", "縦断面 / 標高"}, {"canvas.radius", "曲線半径"},
            {"plot.profile", "縦断面"}, {"plot.radius", "曲線半径"}, {"plot.level", "水平"}, {"unit.m", "m"},
            {"info.mileage", "キロ程"}, {"info.elevation", "標高"}, {"info.gradient", "勾配"}, {"info.radius", "曲線半径"},
            {"info.speedlimit", "速度制限"}, {"info.no_limit", "制限なし"},
            {"hint.pick_bg_station", "背景画像上の駅位置をダブルクリック"},
            {"label.bgimg_x", "X (m)"}, {"label.bgimg_y", "Y (m)"}, {"label.bgimg_width", "幅 (m)"},
            {"label.bgimg_height", "高さ (m)"}, {"label.bgimg_rotation", "回転角 (度)"}, {"label.bgimg_brightness", "明るさ (%)"},
            {"label.font_size", "文字サイズ"}, {"label.ui_component_size", "UI部品サイズ"}, {"label.station_marker_size", "駅マーカーサイズ"}, {"label.ui_theme_color", "UIテーマカラー"},
            {"label.font_size_current", "文字サイズ："}, {"label.ui_component_size_current", "部品サイズ："}, {"label.ui_theme_color_current", "テーマカラー："}, {"label.font_size_preview", "プレビューテキスト"},
            {"status.no_map", "地図が読み込まれていません"}, {"status.loading", "読込中..."}, {"status.ready", "準備完了"},
            {"dialog.ui_settings", "UI設定"}, {"dialog.bgimage_adjust", "背景画像の調整"}, {"dialog.align_to_station", "背景画像を駅に合わせる"}, {"button.pick_on_bg", "平面図で駅を選択"}, {"button.pick_on_bg_ok", "選択済み"},
            {"about.text", "komapedit\nVersion 0.6.0\nCopyright © 2026 Sapporo_ningyo\nkobushi-trackviewer に基づく\nCopyright © 2021-2024 konawasabi\nLicense: Apache License 2.0\nThird-party: Dear ImGui (MIT), ImPlot (MIT)\nSee LICENSE, NOTICE, and THIRD_PARTY_NOTICES.md"}
        };
    }

    const std::string& get(Language lang, const std::string& key) const {
        const auto* table = &en;
        if (lang == Language::Zh) table = &zh;
        else if (lang == Language::Ja) table = &ja;
        auto it = table->find(key);
        if (it != table->end()) return it->second;
        auto it_en = en.find(key);
        if (it_en != en.end()) return it_en->second;
        static std::string missing;
        missing = key;
        return missing;
    }
};
