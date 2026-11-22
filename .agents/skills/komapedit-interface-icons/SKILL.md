---
name: komapedit-interface-icons
description: Add, replace, or verify komapedit interface control icons. Use for button and toolbar glyphs, SVG and multi-DPI PNG assets, EXE resource registration, shared ImGui rendering, or icon appearance and tooltip checks.
---

# Komapedit Interface Icons

## Locate the control

- Trace the control from its render owner to the current action and state. Start with `App::render_mode_grid_controls()` in `src/canvas2d/canvas2D.cpp` for the 2D modes and `App::render_scene_preview_window()` in `src/main_window/scene_preview_lifecycle.cpp` for the 3D scene toolbar.
- Check [the existing glyphs](../../../icons/toolbar/) and the `toolbar_icons::Icon` enum before drawing. Reuse a registered glyph for the same action across views. Compare easily confused actions at 16 px, such as view movement and gizmo translation, or element selection and mileage selection.
- Use `IDI_KOMAPEDIT` and `icons/komapedit_logo.ico` for the Win32 application icon. Route map and scene marker artwork through `map_marker_visuals` and the owning canvas workflow.

## Prepare and register assets

1. Keep each standard control glyph as a single-color SVG with a 16-unit square viewBox in `icons/toolbar/`. Use the same lowercase hyphenated stem for its SVG and its `<stem>-<size>.png` files.
2. Export the page directly from the SVG with transparent antialiasing at 16, 20, 24, 28, 32, 40, 48, 64, and 80 px. Review the silhouettes and edges on dark and light backgrounds at native size. The [developer guide](../../../docs/dev.md#toolbar-icon-assets) gives the Inkscape export command.
3. When redrawing an existing glyph, update its SVG and all nine PNGs under the same stem and resource IDs. For a new glyph, add an `Icon` value before `Count` in `src/main_window/toolbar_icons.h` and add the matching entry in enum order in `src/main_window/toolbar_icons.cpp`. Reserve an unused block of nine consecutive IDs in `include/resource.h`. In `komapedit.rc`, register each PNG as `RCDATA` at the base plus its index in `source_sizes`.
4. Add a new stem to the toolbar PNG list in `CMakeLists.txt` so each export is an `OBJECT_DEPENDS` input of `komapedit.rc`. Track the SVG and PNGs together. The regular build embeds the exported PNGs in the EXE.

## Wire the control

- Use the App-owned `toolbar_icons_.button()` for icon-only controls. Extend the same renderer when the requested control pairs a glyph with visible text. Use a stable caller ID scope when the same glyph appears more than once in an ImGui window.
- Preserve the control's position, keyboard navigation, state and action for a visual replacement. Use the owning mode or action state for selected and disabled styling. Call the existing state transition on activation, and request a frame with `wake_main_window()` after a mode change so the selected outline updates while the pointer is still.
- Reuse the translated action name for the tooltip. Keep hover feedback immediate in the event-driven frame loop. Apply [the multilingual UI skill](../komapedit-trilingual-ui-menu-change/SKILL.md) when adding or changing visible wording.
- Keep rendering and resource lifetime in [the shared icon cache](../../../src/main_window/toolbar_icons.cpp). Round `16 × ImGui::GetWindowDpiScale()` to get the display size. Choose the smallest source that covers it, using the 80 px source for larger requests. Decode embedded PNGs with WIC on the first use of each icon and source size. Set every pixel's RGB to white, preserve straight alpha, tint with the current ImGui style, and align the image rectangle to whole pixels. Use the existing `LoadError` path for required resource failures.

## Verify the result

1. Check the affected PNG dimensions, transparency, and edges. Build Debug with `build_dev.bat` and run the applicable contracts, including `multilanguage_contract` for translation changes. When embedded PNGs or their registrations change, inspect the Ninja resource dependencies, build Release with `build_release.bat`, and confirm that every affected resource in the EXE matches its PNG. Launch the Release EXE with its runtime DLLs from an isolated distribution directory.
2. In the affected view, verify layout, mouse and keyboard activation, and selected or disabled styling. Check new or rewired tooltips in all six interface languages. Exercise the original action, such as measurement cleanup or scene picking, when replacing its control.
3. Compare the actual toolbar at 100% and a larger scale such as 150% for DPI acceptance. When changing source-size selection, verify boundaries such as 17 px using a 20 px source and 81 px using an 80 px source. For mixed-DPI acceptance, move the window to the other monitor and back, checking icon size, edges, and mode state. Record this check as pending when the required display pair is unavailable. Restore the original Windows scale after any temporary scale change in the task.
4. Use full-screen captures when window capture is unavailable. Before each full-screen capture, verify that komapedit is maximized and has foreground focus.
