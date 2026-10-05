/*
 * Copyright (c) 2026 Sapporo_ningyo
 *
 * Licensed under Apache License 2.0; see LICENSE and NOTICE.
 */

#include "multilanguage.h"

#include <cstddef>
#include <iostream>
#include <set>
#include <string>
#include <string_view>

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

std::multiset<std::string> placeholders(std::string_view text) {
    std::multiset<std::string> result;
    std::size_t position = 0;
    while ((position = text.find('{', position)) != std::string_view::npos) {
        const auto end = text.find('}', position);
        if (end == std::string_view::npos) {
            result.emplace(text.substr(position));
            break;
        }
        result.emplace(text.substr(position, end - position + 1));
        position = end + 1;
    }
    return result;
}

bool complete_translations(const Translation& translation) {
    constexpr std::size_t expected_key_count = 605;
    struct LanguageTable {
        const char* name;
        Language language;
        const decltype(translation.en)* texts;
    };
    const LanguageTable tables[] = {
        {"en", Language::En, &translation.en},
        {"zh", Language::Zh, &translation.zh},
        {"zh-TW", Language::ZhTw, &translation.zh_tw},
        {"ja", Language::Ja, &translation.ja}
    };
    for (const auto& table : tables) {
        if (table.texts->size() != expected_key_count) {
            std::cerr << "unexpected translation map size: " << table.name << '\n';
            return false;
        }
        for (const auto& [key, english] : translation.en) {
            const auto it = table.texts->find(key);
            if (it == table.texts->end() || it->second.empty()) {
                std::cerr << "missing or empty translation: " << table.name << ':' << key << '\n';
                return false;
            }
            if (placeholders(it->second) != placeholders(english)) {
                std::cerr << "translation placeholders differ: " << table.name << ':' << key << '\n';
                return false;
            }
            if (translation.get(table.language, key) != it->second) {
                std::cerr << "wrong language lookup: " << table.name << ':' << key << '\n';
                return false;
            }
        }
    }
    return true;
}

} // namespace

int main() {
    const Translation translation;
    bool ok = complete_translations(translation);
    ok = expect_value(translation.zh_tw, "menu.file", "檔案") && ok;
    ok = expect_value(translation.zh_tw, "menu.open", "開啟...") && ok;
    ok = expect_value(translation.zh_tw, "menu.reload", "重新載入") && ok;
    ok = expect_value(translation.zh_tw, "button.save", "儲存") && ok;
    ok = expect_value(translation.zh_tw, "button.apply", "套用") && ok;
    ok = expect_value(translation.zh_tw, "button.revert", "復原") && ok;
    ok = expect_value(translation.zh_tw, "button.reset", "重設") && ok;
    ok = expect_value(translation.zh_tw, "button.copy", "複製") && ok;
    ok = expect_value(translation.zh_tw, "button.select_directory", "選取資料夾") && ok;
    ok = expect_value(translation.zh_tw, "column.field", "欄位") && ok;
    ok = expect_value(translation.zh_tw, "label.source_section", "原始碼區段") && ok;
    ok = expect_value(translation.zh_tw, "frame.console", "主控台") && ok;
    ok = expect_value(translation.zh_tw, "dialog.element_properties", "內容/編輯") && ok;
    ok = expect_value(translation.zh_tw, "frame.scenario_file", "Scenario 檔案") && ok;
    ok = expect_value(translation.zh_tw, "frame.scene_preview", "3D-場景預覽") && ok;
    ok = expect_value(translation.zh_tw, "button.add_row", "新增列") && ok;
    ok = expect_value(translation.zh_tw, "context.station_list.insert_above", "在上方新增列") && ok;
    ok = expect_value(translation.zh_tw, "context.station_list.move_up", "上移整列") && ok;
    ok = expect_value(translation.zh_tw, "context.editable_list.delete_row", "刪除整列") && ok;
    ok = expect_value(translation.zh_tw, "context.editable_list.clear_cell", "清除儲存格") && ok;
    ok = expect_value(translation.zh_tw, "button.signal_aspect.align_columns", "對齊所有欄") && ok;
    ok = expect_value(translation.zh_tw, "button.signal_aspect.append_column", "在右側新增欄") && ok;
    ok = expect_value(translation.zh_tw, "button.signal_aspect.remove_last_column", "刪除最右側欄") && ok;
    ok = expect_value(translation.zh_tw, "dialog.signal_columns_scope_all", "所有列") && ok;
    ok = expect_value(translation.zh_tw, "dialog.signal_columns_scope_row", "目前列") && ok;
    ok = expect_value(translation.zh_tw, "menu.map_info.signal_aspects", "號誌顯示") && ok;
    ok = expect_value(translation.zh_tw, "frame.signal_aspects", "號誌顯示清單") && ok;
    ok = expect_value(translation.zh_tw, "resource_list.name.signal", "號誌顯示清單") && ok;
    ok = expect_value(translation.zh_tw, "dialog.apply_list_before_save",
        "儲存前請先套用所有清單表格中的變更。") && ok;
    ok = expect_value(translation.zh_tw, "dialog.revert_all_edits_message",
        "要復原所有未儲存的變更嗎？這會將記憶體中的地圖還原至上次儲存的狀態，且無法重做。") && ok;
    ok = expect_value(translation.en, "dialog.distance_environment_boundary_message",
        "The current placement would change the statement's evaluated values. Select a source position that preserves them.") && ok;
    ok = expect_value(translation.zh, "dialog.distance_environment_boundary_message",
        "当前位置会改变语句的求值结果。请选择能够保留原求值结果的源码位置。") && ok;
    ok = expect_value(translation.ja, "dialog.distance_environment_boundary_message",
        "現在の挿入位置では文の評価結果が変わります。元の評価結果を維持できるソース位置を選択してください。") && ok;
    ok = expect_value(translation.en, "status.edit.distance_choice_rejected",
        "This choice already failed. Change the expression, position, or edit fields before trying again.") && ok;
    ok = expect_value(translation.zh, "status.edit.distance_choice_rejected",
        "此选择验证失败。请修改表达式、位置或编辑字段后重试。") && ok;
    ok = expect_value(translation.ja, "status.edit.distance_choice_rejected",
        "この選択は既に失敗しています。式、挿入位置、または編集項目を変更してから再試行してください。") && ok;
    ok = expect_value(translation.en, "status.edit.distance_resolution_blocked",
        "The edit could not be applied. Review the Console details and adjust the edit fields.") && ok;
    ok = expect_value(translation.zh, "status.edit.distance_resolution_blocked",
        "无法应用此次编辑。请查看控制台中的具体原因并调整编辑字段。") && ok;
    ok = expect_value(translation.ja, "status.edit.distance_resolution_blocked",
        "編集を適用できませんでした。コンソールで原因を確認し、編集項目を調整してください。") && ok;
    ok = expect_value(translation.en, "frame.creator_messages", "Custom Messages") && ok;
    ok = expect_value(translation.zh, "frame.creator_messages", "自定义消息") && ok;
    ok = expect_value(translation.ja, "frame.creator_messages", "カスタムメッセージ") && ok;
    ok = expect_value(translation.en, "creator_message.do_not_show", "Do not show again") && ok;
    ok = expect_value(translation.zh, "creator_message.do_not_show", "下次不再显示") && ok;
    ok = expect_value(translation.ja, "creator_message.do_not_show", "次回から表示しない") && ok;
    ok = expect_value(translation.en, "table.section_values_truncated",
        "Only the first {shown} parameters are shown (largest row: {total}). All parameters remain available in Properties/Edit.") && ok;
    ok = expect_value(translation.zh, "table.section_values_truncated",
        "仅显示前 {shown} 个参数（最长行有 {total} 个）。完整参数可在属性/编辑中查看。") && ok;
    ok = expect_value(translation.ja, "table.section_values_truncated",
        "先頭の {shown} 個のパラメータのみ表示しています（最長行: {total} 個）。すべてのパラメータはプロパティ/編集で確認できます。") && ok;

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
    ok = expect_value(translation.en, "new_element.usage.curve.interpolate", "Add a curve interpolation point. Omitted radius and cant inherit the previous values.") && ok;
    ok = expect_value(translation.zh, "new_element.usage.curve.interpolate", "新建曲线插值点。省略的半径和超高继承前值。") && ok;
    ok = expect_value(translation.ja, "new_element.usage.curve.interpolate", "曲線の補間点を追加します。省略した半径とカントは直前の値を引き継ぎます。") && ok;
    ok = expect_value(translation.en, "value.curve_function.sine", "Sine half-wave transition") && ok;
    ok = expect_value(translation.en, "value.curve_function.linear", "Linear transition") && ok;
    ok = expect_value(translation.ja, "value.curve_function.sine", "サイン半波長逓減") && ok;
    ok = expect_value(translation.ja, "value.curve_function.linear", "直線逓減") && ok;
    ok = expect_value(translation.en, "new_file.usage.sound", "Create a blank Sound List file for Sound.Load.") && ok;
    ok = expect_value(translation.en, "new_file.usage.sound3d", "Create a blank Sound List file for Sound3D.Load.") && ok;
    ok = expect_value(translation.en, "new_file.category.scenario", "Scenarios") && ok;
    ok = expect_value(translation.en, "new_file.usage.scenario", "Create a BveTs Scenario 2.00 file with route and vehicle metadata.") && ok;
    ok = expect_value(translation.zh, "new_file.category.scenario", "场景文件") && ok;
    ok = expect_value(translation.zh, "new_file.usage.scenario", "新建含线路与车辆信息的 BveTs Scenario 2.00 场景文件。") && ok;
    ok = expect_value(translation.ja, "new_file.category.scenario", "シナリオファイル") && ok;
    ok = expect_value(translation.ja, "new_file.usage.scenario", "路線と車両情報を含む BveTs Scenario 2.00 シナリオファイルを作成します。") && ok;
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
