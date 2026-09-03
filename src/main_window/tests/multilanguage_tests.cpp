/*
 * Copyright (c) 2026 Sapporo_ningyo
 *
 * Licensed under Apache License 2.0; see LICENSE and NOTICE.
 */

#include "multilanguage.h"

#include <cstddef>
#include <iostream>
#include <string>

namespace {

template <typename Map>
bool expect_value(const Map& translations, const char* key, const char* expected) {
    const auto it = translations.find(key);
    if (it == translations.end()) {
        std::cerr << "missing translation key: " << key << '\n';
        return false;
    }
    if (it->second != expected) {
        std::cerr << "unexpected translation for " << key << ": expected '"
                  << expected << "', got '" << it->second << "'\n";
        return false;
    }
    return true;
}

template <typename Map>
bool expect_no_value_fragment(const Map& translations, const char* fragment) {
    for (const auto& [key, value] : translations) {
        if (value.find(fragment) != std::string::npos) {
            std::cerr << "forbidden translation fragment '" << fragment
                      << "' in " << key << '\n';
            return false;
        }
    }
    return true;
}

bool same_keys(const Translation& translation) {
    constexpr std::size_t expected_key_count = 575;
    if (translation.en.size() != expected_key_count ||
        translation.zh.size() != expected_key_count ||
        translation.ja.size() != expected_key_count) {
        std::cerr << "unexpected translation map size\n";
        return false;
    }
    if (translation.en.size() != translation.zh.size() ||
        translation.en.size() != translation.ja.size()) {
        std::cerr << "translation map sizes differ\n";
        return false;
    }
    for (const auto& entry : translation.en) {
        const auto& key = entry.first;
        if (translation.zh.find(key) == translation.zh.end() ||
            translation.ja.find(key) == translation.ja.end()) {
            std::cerr << "translation key is not present in all languages: " << key << '\n';
            return false;
        }
    }
    for (const auto& entry : translation.zh) {
        const auto& key = entry.first;
        if (translation.en.find(key) == translation.en.end() ||
            translation.ja.find(key) == translation.ja.end()) {
            std::cerr << "translation key is not present in all languages: " << key << '\n';
            return false;
        }
    }
    for (const auto& entry : translation.ja) {
        const auto& key = entry.first;
        if (translation.en.find(key) == translation.en.end() ||
            translation.zh.find(key) == translation.zh.end()) {
            std::cerr << "translation key is not present in all languages: " << key << '\n';
            return false;
        }
    }
    return true;
}

} // namespace

int main() {
    const Translation translation;
    bool ok = same_keys(translation);

    ok = expect_value(translation.en, "column.field", "Field") && ok;
    ok = expect_value(translation.zh, "column.field", "字段") && ok;
    ok = expect_value(translation.ja, "column.field", "項目") && ok;
    ok = expect_value(translation.en, "column.value", "Value") && ok;
    ok = expect_value(translation.zh, "column.value", "值") && ok;
    ok = expect_value(translation.ja, "column.value", "値") && ok;

    ok = expect_value(translation.en, "menu.map_info.structure_models", "Structure List") && ok;
    ok = expect_value(translation.en, "frame.structure_models", "Structure List") && ok;
    ok = expect_value(translation.en, "menu.find_in_structure_models", "Find in Structure List") && ok;
    ok = expect_value(translation.en, "resource_list.name.structure", "Structure List") && ok;
    ok = expect_value(translation.en, "button.model_list", "Structure List") && ok;
    ok = expect_value(translation.ja, "menu.map_info.structure_models", "ストラクチャーリスト") && ok;
    ok = expect_value(translation.ja, "frame.structure_models", "ストラクチャーリスト") && ok;
    ok = expect_value(translation.ja, "menu.find_in_structure_models", "ストラクチャーリストで検索") && ok;
    ok = expect_value(translation.ja, "resource_list.name.structure", "ストラクチャーリスト") && ok;
    ok = expect_value(translation.ja, "button.model_list", "ストラクチャーリスト") && ok;

    ok = expect_value(translation.en, "menu.map_info.signal_aspects", "Signal Aspects List") && ok;
    ok = expect_value(translation.en, "frame.signal_aspects", "Signal Aspects List") && ok;
    ok = expect_value(translation.en, "menu.find_in_signal_aspects", "Find in Signal Aspects List") && ok;
    ok = expect_value(translation.en, "resource_list.name.signal", "Signal Aspects List") && ok;
    ok = expect_value(translation.ja, "menu.map_info.signal_aspects", "信号現示リスト") && ok;
    ok = expect_value(translation.ja, "frame.signal_aspects", "信号現示リスト") && ok;
    ok = expect_value(translation.ja, "menu.find_in_signal_aspects", "信号現示リストで検索") && ok;
    ok = expect_value(translation.ja, "resource_list.name.signal", "信号現示リスト") && ok;

    ok = expect_value(translation.en, "menu.map_info.signals", "Ground Signal") && ok;
    ok = expect_value(translation.en, "frame.signals", "Ground Signal List") && ok;
    ok = expect_value(translation.en, "context.plan_marker.signal", "Ground Signal") && ok;
    ok = expect_value(translation.en, "menu.locate_in_signal_list", "Locate in Ground Signal List") && ok;
    ok = expect_value(translation.ja, "menu.map_info.signals", "地上信号機") && ok;
    ok = expect_value(translation.ja, "frame.signals", "地上信号機リスト") && ok;
    ok = expect_value(translation.ja, "context.plan_marker.signal", "地上信号機") && ok;
    ok = expect_value(translation.ja, "menu.locate_in_signal_list", "地上信号機リストへ移動") && ok;

    ok = expect_value(translation.en, "menu.map_info.map_sounds", "Sound Playback Point") && ok;
    ok = expect_value(translation.en, "frame.map_sounds", "Sound Playback Point List") && ok;
    ok = expect_value(translation.en, "button.map_sound_list", "Sound Playback Point List") && ok;
    ok = expect_value(translation.en, "menu.locate_in_map_sound_list", "Locate in Sound Playback Point List") && ok;
    ok = expect_value(translation.ja, "menu.map_info.map_sounds", "サウンド再生点") && ok;
    ok = expect_value(translation.ja, "frame.map_sounds", "サウンド再生点リスト") && ok;
    ok = expect_value(translation.ja, "button.map_sound_list", "サウンド再生点リスト") && ok;
    ok = expect_value(translation.ja, "menu.locate_in_map_sound_list", "サウンド再生点リストへ移動") && ok;

    ok = expect_value(translation.en, "menu.map_info.map_sound_3d", "Fixed Sound Source") && ok;
    ok = expect_value(translation.en, "frame.map_sound_3d", "Fixed Sound Source List") && ok;
    ok = expect_value(translation.en, "button.map_sound_3d_list", "Fixed Sound Source List") && ok;
    ok = expect_value(translation.en, "chk.map_sound_3d_markers", "Fixed Sound Source Positions") && ok;
    ok = expect_value(translation.en, "menu.locate_in_map_sound_3d_list", "Locate in Fixed Sound Source List") && ok;
    ok = expect_value(translation.ja, "menu.map_info.map_sound_3d", "固定音源") && ok;
    ok = expect_value(translation.ja, "frame.map_sound_3d", "固定音源リスト") && ok;
    ok = expect_value(translation.ja, "button.map_sound_3d_list", "固定音源リスト") && ok;
    ok = expect_value(translation.ja, "chk.map_sound_3d_markers", "固定音源位置") && ok;
    ok = expect_value(translation.ja, "menu.locate_in_map_sound_3d_list", "固定音源リストへ移動") && ok;

    ok = expect_value(translation.en, "menu.map_info.lighting", "Light Sources") && ok;
    ok = expect_value(translation.en, "frame.lighting", "Light Sources") && ok;
    ok = expect_value(translation.ja, "menu.map_info.lighting", "光源") && ok;
    ok = expect_value(translation.ja, "frame.lighting", "光源") && ok;
    ok = expect_value(translation.en, "menu.map_info.draw_distances", "Scenery Draw Distance Change Point") && ok;
    ok = expect_value(translation.en, "frame.draw_distances", "Scenery Draw Distance Change Point List") && ok;
    ok = expect_value(translation.en, "chk.draw_distance_markers", "Scenery Draw Distance Change Points") && ok;
    ok = expect_value(translation.en, "menu.locate_in_draw_distance_list", "Locate in Scenery Draw Distance Change Point List") && ok;
    ok = expect_value(translation.ja, "menu.map_info.draw_distances", "風景描画距離変化点") && ok;
    ok = expect_value(translation.ja, "frame.draw_distances", "風景描画距離変化点リスト") && ok;
    ok = expect_value(translation.ja, "chk.draw_distance_markers", "風景描画距離変化点") && ok;
    ok = expect_value(translation.ja, "menu.locate_in_draw_distance_list", "風景描画距離変化点リストへ移動") && ok;

    ok = expect_value(translation.ja, "menu.map_info.station", "停車場リスト") && ok;
    ok = expect_value(translation.ja, "resource_list.name.station", "停車場リスト") && ok;
    ok = expect_value(translation.ja, "new_file.usage.station", "停車場定義用の空の停車場リストファイルを作成します。") && ok;
    ok = expect_value(translation.ja, "status.edit.apply_station_list_before_save", "保存する前に停車場定義テーブルの変更を適用してください。") && ok;
    ok = expect_value(translation.ja, "dialog.apply_station_list_before_save", "保存する前に停車場定義テーブルの変更を適用してください。") && ok;
    ok = expect_value(translation.ja, "label.repeater_structure_keys", "ストラクチャーキー") && ok;
    ok = expect_value(translation.en, "value.curve_function.sine", "Sine half-wave transition") && ok;
    ok = expect_value(translation.en, "value.curve_function.linear", "Linear transition") && ok;
    ok = expect_value(translation.ja, "value.curve_function.sine", "サイン半波長逓減") && ok;
    ok = expect_value(translation.ja, "value.curve_function.linear", "直線逓減") && ok;
    ok = expect_value(translation.en, "new_file.usage.sound", "Create a blank Sound List file for Sound.Load.") && ok;
    ok = expect_value(translation.en, "new_file.usage.sound3d", "Create a blank Sound List file for Sound3D.Load.") && ok;
    ok = expect_value(translation.ja, "new_file.usage.structure", "マップストラクチャー用の空のストラクチャーリストファイルを作成します。") && ok;
    ok = expect_value(translation.ja, "new_file.usage.signal", "信号現示定義用の空の信号現示リストファイルを作成します。") && ok;
    ok = expect_value(translation.ja, "new_file.usage.sound", "Sound.Load 用の空のサウンドリストファイルを作成します。") && ok;
    ok = expect_value(translation.ja, "new_file.usage.sound3d", "Sound3D.Load 用の空のサウンドリストファイルを作成します。") && ok;

    ok = expect_value(translation.zh, "frame.scenario_file", "Scenario 文件") && ok;
    ok = expect_value(translation.zh, "menu.map_info.scenario_file", "Scenario 文件") && ok;
    ok = expect_value(translation.zh, "dialog.select_scenario_file", "选择 Scenario 文件") && ok;
    ok = expect_value(translation.zh, "dialog.select_scenario_image", "选择 Scenario 图像") && ok;
    ok = expect_value(translation.zh, "status.scenario_loaded", "Scenario 已加载") && ok;
    ok = expect_value(translation.zh, "status.scenario_saved", "Scenario 已保存") && ok;
    ok = expect_value(translation.zh, "status.scenario_save_failed", "Scenario 保存失败") && ok;
    ok = expect_value(translation.zh, "status.scenario_save_failed_after_map", "地图已保存，但 Scenario 未保存") && ok;
    ok = expect_value(translation.zh, "status.scenario_save_deferred", "Scenario 保存已延后，请再次保存") && ok;
    ok = expect_value(translation.zh, "status.scenario_route_changed", "Scenario 中的地图路径已更改；请重新加载以载入最新地图。") && ok;
    ok = expect_value(translation.zh, "status.scenario_path_absolute_fallback", "无法生成 Scenario 相对路径，已改用绝对路径。") && ok;
    ok = expect_value(translation.zh, "dialog.filter.map_files", "BVE 地图/Scenario 文件") && ok;
    ok = expect_value(translation.zh, "dialog.scenario_route_select_title", "Scenario 中有多个地图候选，请选择一个加载") && ok;
    ok = expect_value(translation.zh, "frame.scene_preview", "3D-场景预览") && ok;
    ok = expect_value(translation.en, "frame.scenario_file", "Scenario File") && ok;
    ok = expect_value(translation.ja, "frame.scenario_file", "シナリオファイル") && ok;
    ok = expect_value(translation.en, "context.scenario.add_candidate", "Add Candidate") && ok;
    ok = expect_value(translation.en, "context.scenario.delete_candidate", "Delete Candidate") && ok;
    ok = expect_value(translation.en, "context.scenario.move_up", "Move Up") && ok;
    ok = expect_value(translation.en, "context.scenario.move_down", "Move Down") && ok;
    ok = expect_value(translation.zh, "context.scenario.add_candidate", "新增候选项") && ok;
    ok = expect_value(translation.zh, "context.scenario.delete_candidate", "删除候选项") && ok;
    ok = expect_value(translation.zh, "context.scenario.move_up", "上移") && ok;
    ok = expect_value(translation.zh, "context.scenario.move_down", "下移") && ok;
    ok = expect_value(translation.ja, "context.scenario.add_candidate", "候補を追加") && ok;
    ok = expect_value(translation.ja, "context.scenario.delete_candidate", "候補を削除") && ok;
    ok = expect_value(translation.ja, "context.scenario.move_up", "上へ移動") && ok;
    ok = expect_value(translation.ja, "context.scenario.move_down", "下へ移動") && ok;
    ok = expect_value(translation.en, "frame.rolling_noises", "Rolling Noise Change Point List") && ok;
    ok = expect_value(translation.ja, "frame.rolling_noises", "走行音変化点リスト") && ok;
    ok = expect_no_value_fragment(translation.en, "Running Sound") && ok;
    ok = expect_no_value_fragment(translation.ja, "Running Sound") && ok;
    ok = expect_no_value_fragment(translation.en, "Scene File") && ok;
    ok = expect_no_value_fragment(translation.ja, "Scene File") && ok;

    return ok ? 0 : 1;
}
