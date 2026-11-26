---
name: komapedit-trilingual-ui-menu-change
description: Make a small komapedit UI text, menu, toolbar, dialog, label, or Help-link change while preserving parity across all supported languages. Use when the translation dictionary and a focused GUI owner need synchronized edits.
---

# Komapedit Multilingual UI Change

## Edit the owning surface

1. Find the existing control/menu/dialog in its actual render owner before adding wiring.
2. Apply each translation-key addition, change, or removal to the English, Simplified Chinese, Taiwan Traditional Chinese, Hong Kong Traditional Chinese, Japanese, and Korean initializer blocks in `include/multilanguage.h`.
3. Reuse neighboring GUI patterns and keep the patch local. For external links, reuse the established Win32 `ShellExecuteW` path.
4. Keep console diagnostics English by default unless the existing surface is explicitly localized.
5. Avoid post-initializer translation assignments or a second localization mechanism.

## Preserve behavior and layout

1. Limit interface copy to control labels, short tooltips, prompts for the current step, and status or validation feedback. Put tutorials, general procedures, and gesture instructions in the user manual. Keep wording short enough for its control.
2. Separate copy changes from settings, docking, or interaction changes unless the request includes both.
3. Preserve UTF-8 text and check that the CJK strings decode correctly.
4. Use Taiwan terminology for `zh_tw`, including 列 for table rows and 欄 for columns. Preserve existing unlocalized BVE parameter labels.
5. Use standard Hong Kong written Chinese for `zh_hk`, with 行 for rows, 欄 for columns, and 單元格 for cells. Base translations on the Simplified Chinese text, consulting Japanese and English when needed. Keep wording concise and use consistent Hong Kong terms such as 檔案, 儲存, 套用, 還原, 重設, 控制台, 屬性, 字體, and 信號.
6. Use standard South Korean terminology for `ko`. Use 행, 열, and 셀 for rows, columns, and cells. Keep actions distinct, including 적용 for Apply, 저장 for Save, 되돌리기 for Revert, and 초기화 for Reset.
7. If the control persists state, use `komapedit-ui-persistence` as well.

## Validate

1. Confirm each added or changed key occurs exactly once in every language block. Search for each removed key across the codebase and verify it has zero references. Update the translation contract's expected key count when adding or removing keys.
2. Run the registered `multilanguage_contract` for translation-map parity, expected key count, nonempty text, matching placeholder names and counts, language lookup, and exact critical terminology.
3. Run `git diff --check` on the touched files.
4. Build Debug for code changes.
5. Leave actual truncation, alignment, menu ordering, and browser launch as manual checks unless exercised by an available runtime test.
