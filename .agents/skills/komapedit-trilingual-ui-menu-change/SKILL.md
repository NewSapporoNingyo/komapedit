---
name: komapedit-trilingual-ui-menu-change
description: Make a small komapedit user-visible UI text, menu, toolbar, dialog, label, or Help-link change while preserving Simplified Chinese, English, and Japanese parity. Use when `include/multilanguage.h` and a focused GUI owner need synchronized edits without a broader UI refactor.
---

# Komapedit Trilingual UI Change

## Edit the owning surface

1. Find the existing control/menu/dialog in its actual render owner before adding wiring.
2. Add or change the translation key inside the existing English, Simplified Chinese, and Japanese initializer blocks in `include/multilanguage.h`.
3. Reuse neighboring GUI patterns and keep the patch local. For external links, reuse the established Win32 `ShellExecuteW` path.
4. Keep console diagnostics English by default unless the existing surface is explicitly localized.
5. Avoid post-initializer translation assignments or a second localization mechanism.

## Preserve behavior and layout

1. Keep wording short enough for the target menu/toolbar width.
2. Separate copy changes from settings, docking, or interaction changes unless the request includes both.
3. Preserve UTF-8 text and verify Japanese/Chinese characters are not mojibake.
4. If the control persists state, use `komapedit-ui-persistence` as well.

## Validate

1. Confirm the key exists exactly once in each of the three language blocks.
2. Run `git diff --check` on the touched files.
3. Build Debug for code changes.
4. Leave actual truncation, alignment, menu ordering, and browser launch as manual checks unless exercised by an available runtime test.
